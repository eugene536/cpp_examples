#include <condition_variable>
#include <cstdio>
#include <sched.h>
#include <thread>
#include <mutex>

class semaphore {
    std::mutex m;
    std::condition_variable cv;
    uint32_t count;
public:
    semaphore(uint32_t count = 0) : count(count) {}

    void acquire(uint32_t acquire_count = 1) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [=] { return count >= acquire_count; });
        count -= acquire_count;
    }

    void release(uint32_t release_count = 1) {
        std::unique_lock<std::mutex> lock(m);
        count += release_count;
        lock.unlock();
        cv.notify_all();
    }
};

// Force thread affinities to the same cpu core.
void set_affinity(pthread_t t) {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(0, &cpus);
    pthread_setaffinity_np(t, sizeof(cpu_set_t), &cpus);
}

struct State {
    int read_var{0};
    int write_var{0};
    semaphore sem;

    void wait() { sem.acquire(); }
    void start() { sem.release(); }
};

semaphore finish_sem;
std::array<State, 2> states{};

template<size_t id, bool use_cpu_barrier = false, size_t second_id = (id + 1) % 2>
void repeat() {
    while(true) {
        states[id].wait();

        states[id].write_var = 1;

        if constexpr (use_cpu_barrier) {
            asm volatile("mfence" ::: "memory");  // Prevent CPU reordering
        } else {
            asm volatile("" ::: "memory");  // Prevent compiler reordering
        }

        states[id].read_var = states[second_id].write_var;

        finish_sem.release();
    }
}

int main() {
    std::thread t1(repeat<0>);
    std::thread t2(repeat<1>);

    constexpr bool use_single_hw_thread{false};
    if (use_single_hw_thread) {
        set_affinity(t1.native_handle());
        set_affinity(t2.native_handle());
    }

    for (int i = 1; ; i++) {
        states[0].write_var = 0;
        states[1].write_var = 0;

        states[0].start();
        states[1].start();

        finish_sem.acquire(2);

        // Check if there was a simultaneous reorder
        if (states[0].read_var == 0 && states[1].read_var == 0) {
            printf("reorders detected after iteration: %d\n", i);
        }
    }
}
