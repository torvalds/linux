//===--------------------- TaskPool.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_TaskPool_h_
#define utility_TaskPool_h_

#include "llvm/ADT/STLExtras.h"
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>

namespace lldb_private {

// Global TaskPool class for running tasks in parallel on a set of worker
// thread created the first time the task pool is used. The TaskPool provide no
// guarantee about the order the task will be run and about what tasks will run
// in parallel. None of the task added to the task pool should block on
// something (mutex, future, condition variable) what will be set only by the
// completion of an other task on the task pool as they may run on the same
// thread sequentally.
class TaskPool {
public:
  // Add a new task to the task pool and return a std::future belonging to the
  // newly created task. The caller of this function has to wait on the future
  // for this task to complete.
  template <typename F, typename... Args>
  static std::future<typename std::result_of<F(Args...)>::type>
  AddTask(F &&f, Args &&... args);

  // Run all of the specified tasks on the task pool and wait until all of them
  // are finished before returning. This method is intended to be used for
  // small number tasks where listing them as function arguments is acceptable.
  // For running large number of tasks you should use AddTask for each task and
  // then call wait() on each returned future.
  template <typename... T> static void RunTasks(T &&... tasks);

private:
  TaskPool() = delete;

  template <typename... T> struct RunTaskImpl;

  static void AddTaskImpl(std::function<void()> &&task_fn);
};

template <typename F, typename... Args>
std::future<typename std::result_of<F(Args...)>::type>
TaskPool::AddTask(F &&f, Args &&... args) {
  auto task_sp = std::make_shared<
      std::packaged_task<typename std::result_of<F(Args...)>::type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  AddTaskImpl([task_sp]() { (*task_sp)(); });

  return task_sp->get_future();
}

template <typename... T> void TaskPool::RunTasks(T &&... tasks) {
  RunTaskImpl<T...>::Run(std::forward<T>(tasks)...);
}

template <typename Head, typename... Tail>
struct TaskPool::RunTaskImpl<Head, Tail...> {
  static void Run(Head &&h, Tail &&... t) {
    auto f = AddTask(std::forward<Head>(h));
    RunTaskImpl<Tail...>::Run(std::forward<Tail>(t)...);
    f.wait();
  }
};

template <> struct TaskPool::RunTaskImpl<> {
  static void Run() {}
};

// Run 'func' on every value from begin .. end-1.  Each worker will grab
// 'batch_size' numbers at a time to work on, so for very fast functions, batch
// should be large enough to avoid too much cache line contention.
void TaskMapOverInt(size_t begin, size_t end,
                    const llvm::function_ref<void(size_t)> &func);

unsigned GetHardwareConcurrencyHint();

} // namespace lldb_private

#endif // #ifndef utility_TaskPool_h_
