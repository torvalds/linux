//===--------------------- TaskPool.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/TaskPool.h"
#include "lldb/Host/ThreadLauncher.h"

#include <cstdint>
#include <queue>
#include <thread>

namespace lldb_private {

namespace {
class TaskPoolImpl {
public:
  static TaskPoolImpl &GetInstance();

  void AddTask(std::function<void()> &&task_fn);

private:
  TaskPoolImpl();

  static lldb::thread_result_t WorkerPtr(void *pool);

  static void Worker(TaskPoolImpl *pool);

  std::queue<std::function<void()>> m_tasks;
  std::mutex m_tasks_mutex;
  uint32_t m_thread_count;
};

} // end of anonymous namespace

TaskPoolImpl &TaskPoolImpl::GetInstance() {
  static TaskPoolImpl g_task_pool_impl;
  return g_task_pool_impl;
}

void TaskPool::AddTaskImpl(std::function<void()> &&task_fn) {
  TaskPoolImpl::GetInstance().AddTask(std::move(task_fn));
}

TaskPoolImpl::TaskPoolImpl() : m_thread_count(0) {}

unsigned GetHardwareConcurrencyHint() {
  // std::thread::hardware_concurrency may return 0 if the value is not well
  // defined or not computable.
  static const unsigned g_hardware_concurrency = 
    std::max(1u, std::thread::hardware_concurrency());
  return g_hardware_concurrency;
}

void TaskPoolImpl::AddTask(std::function<void()> &&task_fn) {
  const size_t min_stack_size = 8 * 1024 * 1024;

  std::unique_lock<std::mutex> lock(m_tasks_mutex);
  m_tasks.emplace(std::move(task_fn));
  if (m_thread_count < GetHardwareConcurrencyHint()) {
    m_thread_count++;
    // Note that this detach call needs to happen with the m_tasks_mutex held.
    // This prevents the thread from exiting prematurely and triggering a linux
    // libc bug (https://sourceware.org/bugzilla/show_bug.cgi?id=19951).
    lldb_private::ThreadLauncher::LaunchThread("task-pool.worker", WorkerPtr,
                                               this, nullptr, min_stack_size)
        .Release();
  }
}

lldb::thread_result_t TaskPoolImpl::WorkerPtr(void *pool) {
  Worker((TaskPoolImpl *)pool);
  return 0;
}

void TaskPoolImpl::Worker(TaskPoolImpl *pool) {
  while (true) {
    std::unique_lock<std::mutex> lock(pool->m_tasks_mutex);
    if (pool->m_tasks.empty()) {
      pool->m_thread_count--;
      break;
    }

    std::function<void()> f = std::move(pool->m_tasks.front());
    pool->m_tasks.pop();
    lock.unlock();

    f();
  }
}

void TaskMapOverInt(size_t begin, size_t end,
                    const llvm::function_ref<void(size_t)> &func) {
  const size_t num_workers = std::min<size_t>(end, GetHardwareConcurrencyHint());
  std::atomic<size_t> idx{begin};
  
  auto wrapper = [&idx, end, &func]() {
    while (true) {
      size_t i = idx.fetch_add(1);
      if (i >= end)
        break;
      func(i);
    }
  };

  std::vector<std::future<void>> futures;
  futures.reserve(num_workers);
  for (size_t i = 0; i < num_workers; i++)
    futures.push_back(TaskPool::AddTask(wrapper));
  for (size_t i = 0; i < num_workers; i++)
    futures[i].wait();
}

} // namespace lldb_private

