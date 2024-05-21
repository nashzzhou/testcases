///---用futex实现--》互斥锁

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <linux/futex.h>
#include <syscall.h>
#include <unistd.h>
#include <chrono>
/*
FUTEX_WAIT_PRIVATE和FUTEX_WAIT
他俩都具备阻塞挂起当前线程的效果，直到futex变量的值不等于expected值。
但是加了_PRIVATE代表私有，这是在当前进程内有效，但是他可以避免系统内核级别夸进程的处理，增加性能。
如果你只是多线程那就用FUTEX_WAIT_PRIVATE，如果是多进程那就用：FUTEX_WAIT
*/

// 使用系统调用进行futex等待和唤醒
int futex_wait(void* addr, int expected) {
    return syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
}

int futex_wake(void* addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
}

class FutexMutex {
private:
    std::atomic<int> flag;

public:
    FutexMutex() : flag(0) {}

    void lock() {
        // 尝试将flag设置为1，如果flag原本为0，则设置成功，表示获取了锁
        while (flag.exchange(1, std::memory_order_acquire) != 0) {
            // 如果flag原本不为0，说明锁已被其他线程持有
            // 使用futex_wait等待，直到flag变为0
            while (flag.load(std::memory_order_relaxed) != 0) {
                syscall(SYS_futex, &flag, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 1, nullptr, nullptr, 0);
            }
        }
    }

    void unlock() {
        // 将flag设置为0，表示释放锁
        // 并唤醒一个等待的线程
        flag.store(0, std::memory_order_release);
        syscall(SYS_futex, &flag, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, nullptr, nullptr, 0);
    }
};

// 一个简单的示例函数，每个线程都会尝试获取锁并打印消息
void thread_work(FutexMutex& mutex, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        mutex.lock();
        std::this_thread::yield();
        mutex.unlock();
    }
}

int main(int argc, char* argv[]) {
    int num_locks = std::stoi(argv[1]);
    int num_threads_per_lock = std::stoi(argv[2]);
    int iterations = std::stoi(argv[3]);

    std::vector<FutexMutex> mutexes(num_locks);
    // 创建线程
    std::vector<std::thread> threads;
    for (int i = 0; i < num_locks; ++i) {
        for (int j = 0; j < num_threads_per_lock; ++j) {
            // 将mutexes作为引用传递给线程函数
            threads.emplace_back(thread_work, std::ref(mutexes[i]), iterations);
        }
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    // 计算总耗时
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Total time taken: " << duration.count() << " milliseconds" << std::endl;
    return 0;
}