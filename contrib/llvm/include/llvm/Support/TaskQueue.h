//===-- llvm/Support/TaskQueue.h - A TaskQueue implementation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a crude C++11 based task queue.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TASK_QUEUE_H
#define LLVM_SUPPORT_TASK_QUEUE_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/thread.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>

namespace llvm {
/// TaskQueue executes serialized work on a user-defined Thread Pool.  It
/// guarantees that if task B is enqueued after task A, task B begins after
/// task A completes and there is no overlap between the two.
class TaskQueue {
  // Because we don't have init capture to use move-only local variables that
  // are captured into a lambda, we create the promise inside an explicit
  // callable struct. We want to do as much of the wrapping in the
  // type-specialized domain (before type erasure) and then erase this into a
  // std::function.
  template <typename Callable> struct Task {
    using ResultTy = typename std::result_of<Callable()>::type;
    explicit Task(Callable C, TaskQueue &Parent)
        : C(std::move(C)), P(std::make_shared<std::promise<ResultTy>>()),
          Parent(&Parent) {}

    template<typename T>
    void invokeCallbackAndSetPromise(T*) {
      P->set_value(C());
    }

    void invokeCallbackAndSetPromise(void*) {
      C();
      P->set_value();
    }

    void operator()() noexcept {
      ResultTy *Dummy = nullptr;
      invokeCallbackAndSetPromise(Dummy);
      Parent->completeTask();
    }

    Callable C;
    std::shared_ptr<std::promise<ResultTy>> P;
    TaskQueue *Parent;
  };

public:
  /// Construct a task queue with no work.
  TaskQueue(ThreadPool &Scheduler) : Scheduler(Scheduler) { (void)Scheduler; }

  /// Blocking destructor: the queue will wait for all work to complete.
  ~TaskQueue() {
    Scheduler.wait();
    assert(Tasks.empty());
  }

  /// Asynchronous submission of a task to the queue. The returned future can be
  /// used to wait for the task (and all previous tasks that have not yet
  /// completed) to finish.
  template <typename Callable>
  std::future<typename std::result_of<Callable()>::type> async(Callable &&C) {
#if !LLVM_ENABLE_THREADS
    static_assert(false,
                  "TaskQueue requires building with LLVM_ENABLE_THREADS!");
#endif
    Task<Callable> T{std::move(C), *this};
    using ResultTy = typename std::result_of<Callable()>::type;
    std::future<ResultTy> F = T.P->get_future();
    {
      std::lock_guard<std::mutex> Lock(QueueLock);
      // If there's already a task in flight, just queue this one up.  If
      // there is not a task in flight, bypass the queue and schedule this
      // task immediately.
      if (IsTaskInFlight)
        Tasks.push_back(std::move(T));
      else {
        Scheduler.async(std::move(T));
        IsTaskInFlight = true;
      }
    }
    return std::move(F);
  }

private:
  void completeTask() {
    // We just completed a task.  If there are no more tasks in the queue,
    // update IsTaskInFlight to false and stop doing work.  Otherwise
    // schedule the next task (while not holding the lock).
    std::function<void()> Continuation;
    {
      std::lock_guard<std::mutex> Lock(QueueLock);
      if (Tasks.empty()) {
        IsTaskInFlight = false;
        return;
      }

      Continuation = std::move(Tasks.front());
      Tasks.pop_front();
    }
    Scheduler.async(std::move(Continuation));
  }

  /// The thread pool on which to run the work.
  ThreadPool &Scheduler;

  /// State which indicates whether the queue currently is currently processing
  /// any work.
  bool IsTaskInFlight = false;

  /// Mutex for synchronizing access to the Tasks array.
  std::mutex QueueLock;

  /// Tasks waiting for execution in the queue.
  std::deque<std::function<void()>> Tasks;
};
} // namespace llvm

#endif // LLVM_SUPPORT_TASK_QUEUE_H
