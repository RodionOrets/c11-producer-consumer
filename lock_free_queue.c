template <typename T>
class Queue : public IQueue<T>
{
public:
    explicit Queue( int capacity );
    ~Queue();

    bool try_push( T value );
    bool try_pop( T& value );
private:
    typedef struct
    {
        bool readable;
        T value;
    } Item;

    std::atomic<int> m_head;
    std::atomic<int> m_tail;
    int m_capacity;
    Item* m_items;
};

template <typename T>
Queue<T>::Queue( int capacity ) :
m_head( 0 ),
m_tail( 0 ),
m_capacity(capacity),
m_items( new Item[capacity] )
{
    for( int i = 0; i < capacity; ++i )
    {
        m_items[i].readable = false;
    }
}

template <typename T>
Queue<T>::~Queue()
{
    delete[] m_items;
}

template <typename T>
bool Queue<T>::try_push( T value )
{
    while( true )
    {
        // See that there's room
        int tail = m_tail.load(std::memory_order_acquire);
        int new_tail = ( tail + 1 );
        int head = m_head.load(std::memory_order_acquire);

        if( ( new_tail - head ) >= m_capacity )
        {
            return false;
        }

        if( m_tail.compare_exchange_weak( tail, new_tail, std::memory_order_acq_rel ) )
        {
            // In try_pop, m_head is incremented before the reading of the value has completed,
            // so though we've acquired this slot, a consumer thread may be in the middle of reading
            tail %= m_capacity;

            std::atomic_thread_fence( std::memory_order_acquire );
            while( m_items[tail].readable )
            {
            }

            m_items[tail].value = value;
            std::atomic_thread_fence( std::memory_order_release );
            m_items[tail].readable = true;

            return true;
        }
    }
}

template <typename T>
bool Queue<T>::try_pop( T& value )
{
    while( true )
    {
        int head = m_head.load(std::memory_order_acquire);
        int tail = m_tail.load(std::memory_order_acquire);

        if( head == tail )
        {
            return false;
        }

        int new_head = ( head + 1 );

        if( m_head.compare_exchange_weak( head, new_head, std::memory_order_acq_rel ) )
        {
            head %= m_capacity;

            std::atomic_thread_fence( std::memory_order_acquire );
            while( !m_items[head].readable )
            {
            }

            value = m_items[head].value;
            std::atomic_thread_fence( std::memory_order_release );
            m_items[head].readable = false;

            return true;
        }
    }
}

void Test( std::string name, Queue<int>& queue )
{
    const int NUM_PRODUCERS = 64;
    const int NUM_CONSUMERS = 2;
    const int NUM_ITERATIONS = 512;
    bool table[NUM_PRODUCERS*NUM_ITERATIONS];
    memset(table, 0, NUM_PRODUCERS*NUM_ITERATIONS*sizeof(bool));

    std::vector<std::thread> threads(NUM_PRODUCERS+NUM_CONSUMERS);

    std::chrono::system_clock::time_point start, end;
    start = std::chrono::system_clock::now();

    std::atomic<int> pop_count (NUM_PRODUCERS * NUM_ITERATIONS);
    std::atomic<int> push_count (0);

    for( int thread_id = 0; thread_id < NUM_PRODUCERS; ++thread_id )
    {
        threads[thread_id] = std::thread([&queue,thread_id,&push_count]()
                                 {
                                     int base = thread_id * NUM_ITERATIONS;

                                     for( int i = 0; i < NUM_ITERATIONS; ++i )
                                     {
                                         while( !queue.try_push( base + i ) ){};
                                         push_count.fetch_add(1);
                                     }
                                 });
    }

    for( int thread_id = 0; thread_id < ( NUM_CONSUMERS ); ++thread_id )
    {
        threads[thread_id+NUM_PRODUCERS] = std::thread([&]()
                                         {
                                             int v;

                                             while( pop_count.load() > 0 )
                                             {
                                                 if( queue.try_pop( v ) )
                                                 {
                                                     if( table[v] )
                                                     {
                                                         std::cout << v << " already set" << std::endl;
                                                     }
                                                     table[v] = true;
                                                     pop_count.fetch_sub(1);
                                                 }
                                             }
                                         });

    }

    for( int i = 0; i < ( NUM_PRODUCERS + NUM_CONSUMERS ); ++i )
    {
        threads[i].join();
    }

    end = std::chrono::system_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << name << " " << duration.count() << std::endl;

    std::atomic_thread_fence( std::memory_order_acq_rel );

    bool result = true;
    for( int i = 0; i < NUM_PRODUCERS * NUM_ITERATIONS; ++i )
    {
        if( !table[i] )
        {
            std::cout << "failed at " << i << std::endl;
            result = false;
        }
    }
    std::cout << name << " " << ( result? "success" : "fail" ) << std::endl;
}
