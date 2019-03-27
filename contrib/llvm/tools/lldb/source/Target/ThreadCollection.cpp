//===-- ThreadCollection.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <stdlib.h>

#include <algorithm>
#include <mutex>

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadCollection.h"

using namespace lldb;
using namespace lldb_private;

ThreadCollection::ThreadCollection() : m_threads(), m_mutex() {}

ThreadCollection::ThreadCollection(collection threads)
    : m_threads(threads), m_mutex() {}

void ThreadCollection::AddThread(const ThreadSP &thread_sp) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  m_threads.push_back(thread_sp);
}

void ThreadCollection::AddThreadSortedByIndexID(const ThreadSP &thread_sp) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  // Make sure we always keep the threads sorted by thread index ID
  const uint32_t thread_index_id = thread_sp->GetIndexID();
  if (m_threads.empty() || m_threads.back()->GetIndexID() < thread_index_id)
    m_threads.push_back(thread_sp);
  else {
    m_threads.insert(
        std::upper_bound(m_threads.begin(), m_threads.end(), thread_sp,
                         [](const ThreadSP &lhs, const ThreadSP &rhs) -> bool {
                           return lhs->GetIndexID() < rhs->GetIndexID();
                         }),
        thread_sp);
  }
}

void ThreadCollection::InsertThread(const lldb::ThreadSP &thread_sp,
                                    uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  if (idx < m_threads.size())
    m_threads.insert(m_threads.begin() + idx, thread_sp);
  else
    m_threads.push_back(thread_sp);
}

uint32_t ThreadCollection::GetSize() {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  return m_threads.size();
}

ThreadSP ThreadCollection::GetThreadAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetMutex());
  ThreadSP thread_sp;
  if (idx < m_threads.size())
    thread_sp = m_threads[idx];
  return thread_sp;
}
