//===- llvm/Support/Parallel.cpp - Parallel algorithms --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Parallel.h"
#include "llvm/Config/llvm-config.h"

#if LLVM_ENABLE_THREADS

#include "llvm/Support/Threading.h"

#include <atomic>
#include <stack>
#include <thread>

using namespace llvm;

namespace {

/// An abstract class that takes closures and runs them asynchronously.
class Executor {
public:
  virtual ~Executor() = default;
  virtual void add(std::function<void()> func) = 0;

  static Executor *getDefaultExecutor();
};

#if defined(_MSC_VER)
/// An Executor that runs tasks via ConcRT.
class ConcRTExecutor : public Executor {
  struct Taskish {
    Taskish(std::function<void()> Task) : Task(Task) {}

    std::function<void()> Task;

    static void run(void *P) {
      Taskish *Self = static_cast<Taskish *>(P);
      Self->Task();
      concurrency::Free(Self);
    }
  };

public:
  virtual void add(std::function<void()> F) {
    Concurrency::CurrentScheduler::ScheduleTask(
        Taskish::run, new (concurrency::Alloc(sizeof(Taskish))) Taskish(F));
  }
};

Executor *Executor::getDefaultExecutor() {
  static ConcRTExecutor exec;
  return &exec;
}

#else
/// An implementation of an Executor that runs closures on a thread pool
///   in filo order.
class ThreadPoolExecutor : public Executor {
public:
  explicit ThreadPoolExecutor(unsigned ThreadCount = hardware_concurrency())
      : Done(ThreadCount) {
    // Spawn all but one of the threads in another thread as spawning threads
    // can take a while.
    std::thread([&, ThreadCount] {
      for (size_t i = 1; i < ThreadCount; ++i) {
        std::thread([=] { work(); }).detach();
      }
      work();
    }).detach();
  }

  ~ThreadPoolExecutor() override {
    std::unique_lock<std::mutex> Lock(Mutex);
    Stop = true;
    Lock.unlock();
    Cond.notify_all();
    // Wait for ~Latch.
  }

  void add(std::function<void()> F) override {
    std::unique_lock<std::mutex> Lock(Mutex);
    WorkStack.push(F);
    Lock.unlock();
    Cond.notify_one();
  }

private:
  void work() {
    while (true) {
      std::unique_lock<std::mutex> Lock(Mutex);
      Cond.wait(Lock, [&] { return Stop || !WorkStack.empty(); });
      if (Stop)
        break;
      auto Task = WorkStack.top();
      WorkStack.pop();
      Lock.unlock();
      Task();
    }
    Done.dec();
  }

  std::atomic<bool> Stop{false};
  std::stack<std::function<void()>> WorkStack;
  std::mutex Mutex;
  std::condition_variable Cond;
  parallel::detail::Latch Done;
};

Executor *Executor::getDefaultExecutor() {
  static ThreadPoolExecutor exec;
  return &exec;
}
#endif
}

void parallel::detail::TaskGroup::spawn(std::function<void()> F) {
  L.inc();
  Executor::getDefaultExecutor()->add([&, F] {
    F();
    L.dec();
  });
}
#endif // LLVM_ENABLE_THREADS
