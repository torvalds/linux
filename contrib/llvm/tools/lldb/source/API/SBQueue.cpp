//===-- SBQueue.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>

#include "lldb/API/SBQueue.h"

#include "lldb/API/SBProcess.h"
#include "lldb/API/SBQueueItem.h"
#include "lldb/API/SBThread.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Queue.h"
#include "lldb/Target/QueueItem.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {

class QueueImpl {
public:
  QueueImpl()
      : m_queue_wp(), m_threads(), m_thread_list_fetched(false),
        m_pending_items(), m_pending_items_fetched(false) {}

  QueueImpl(const lldb::QueueSP &queue_sp)
      : m_queue_wp(), m_threads(), m_thread_list_fetched(false),
        m_pending_items(), m_pending_items_fetched(false) {
    m_queue_wp = queue_sp;
  }

  QueueImpl(const QueueImpl &rhs) {
    if (&rhs == this)
      return;
    m_queue_wp = rhs.m_queue_wp;
    m_threads = rhs.m_threads;
    m_thread_list_fetched = rhs.m_thread_list_fetched;
    m_pending_items = rhs.m_pending_items;
    m_pending_items_fetched = rhs.m_pending_items_fetched;
  }

  ~QueueImpl() {}

  bool IsValid() { return m_queue_wp.lock() != NULL; }

  void Clear() {
    m_queue_wp.reset();
    m_thread_list_fetched = false;
    m_threads.clear();
    m_pending_items_fetched = false;
    m_pending_items.clear();
  }

  void SetQueue(const lldb::QueueSP &queue_sp) {
    Clear();
    m_queue_wp = queue_sp;
  }

  lldb::queue_id_t GetQueueID() const {
    lldb::queue_id_t result = LLDB_INVALID_QUEUE_ID;
    lldb::QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp) {
      result = queue_sp->GetID();
    }
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf("SBQueue(%p)::GetQueueID () => 0x%" PRIx64,
                  static_cast<const void *>(this), result);
    return result;
  }

  uint32_t GetIndexID() const {
    uint32_t result = LLDB_INVALID_INDEX32;
    lldb::QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp) {
      result = queue_sp->GetIndexID();
    }
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf("SBQueueImpl(%p)::GetIndexID () => %d",
                  static_cast<const void *>(this), result);
    return result;
  }

  const char *GetName() const {
    const char *name = NULL;
    lldb::QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp.get()) {
      name = queue_sp->GetName();
    }

    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf("SBQueueImpl(%p)::GetName () => %s",
                  static_cast<const void *>(this), name ? name : "NULL");

    return name;
  }

  void FetchThreads() {
    if (!m_thread_list_fetched) {
      lldb::QueueSP queue_sp = m_queue_wp.lock();
      if (queue_sp) {
        Process::StopLocker stop_locker;
        if (stop_locker.TryLock(&queue_sp->GetProcess()->GetRunLock())) {
          const std::vector<ThreadSP> thread_list(queue_sp->GetThreads());
          m_thread_list_fetched = true;
          const uint32_t num_threads = thread_list.size();
          for (uint32_t idx = 0; idx < num_threads; ++idx) {
            ThreadSP thread_sp = thread_list[idx];
            if (thread_sp && thread_sp->IsValid()) {
              m_threads.push_back(thread_sp);
            }
          }
        }
      }
    }
  }

  void FetchItems() {
    if (!m_pending_items_fetched) {
      QueueSP queue_sp = m_queue_wp.lock();
      if (queue_sp) {
        Process::StopLocker stop_locker;
        if (stop_locker.TryLock(&queue_sp->GetProcess()->GetRunLock())) {
          const std::vector<QueueItemSP> queue_items(
              queue_sp->GetPendingItems());
          m_pending_items_fetched = true;
          const uint32_t num_pending_items = queue_items.size();
          for (uint32_t idx = 0; idx < num_pending_items; ++idx) {
            QueueItemSP item = queue_items[idx];
            if (item && item->IsValid()) {
              m_pending_items.push_back(item);
            }
          }
        }
      }
    }
  }

  uint32_t GetNumThreads() {
    uint32_t result = 0;

    FetchThreads();
    if (m_thread_list_fetched) {
      result = m_threads.size();
    }
    return result;
  }

  lldb::SBThread GetThreadAtIndex(uint32_t idx) {
    FetchThreads();

    SBThread sb_thread;
    QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp && idx < m_threads.size()) {
      ProcessSP process_sp = queue_sp->GetProcess();
      if (process_sp) {
        ThreadSP thread_sp = m_threads[idx].lock();
        if (thread_sp) {
          sb_thread.SetThread(thread_sp);
        }
      }
    }
    return sb_thread;
  }

  uint32_t GetNumPendingItems() {
    uint32_t result = 0;

    QueueSP queue_sp = m_queue_wp.lock();
    if (!m_pending_items_fetched && queue_sp) {
      result = queue_sp->GetNumPendingWorkItems();
    } else {
      result = m_pending_items.size();
    }
    return result;
  }

  lldb::SBQueueItem GetPendingItemAtIndex(uint32_t idx) {
    SBQueueItem result;
    FetchItems();
    if (m_pending_items_fetched && idx < m_pending_items.size()) {
      result.SetQueueItem(m_pending_items[idx]);
    }
    return result;
  }

  uint32_t GetNumRunningItems() {
    uint32_t result = 0;
    QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp)
      result = queue_sp->GetNumRunningWorkItems();
    return result;
  }

  lldb::SBProcess GetProcess() {
    SBProcess result;
    QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp) {
      result.SetSP(queue_sp->GetProcess());
    }
    return result;
  }

  lldb::QueueKind GetKind() {
    lldb::QueueKind kind = eQueueKindUnknown;
    QueueSP queue_sp = m_queue_wp.lock();
    if (queue_sp)
      kind = queue_sp->GetKind();

    return kind;
  }

private:
  lldb::QueueWP m_queue_wp;
  std::vector<lldb::ThreadWP>
      m_threads; // threads currently executing this queue's items
  bool
      m_thread_list_fetched; // have we tried to fetch the threads list already?
  std::vector<lldb::QueueItemSP> m_pending_items; // items currently enqueued
  bool m_pending_items_fetched; // have we tried to fetch the item list already?
};
}

SBQueue::SBQueue() : m_opaque_sp(new QueueImpl()) {}

SBQueue::SBQueue(const QueueSP &queue_sp)
    : m_opaque_sp(new QueueImpl(queue_sp)) {}

SBQueue::SBQueue(const SBQueue &rhs) {
  if (&rhs == this)
    return;

  m_opaque_sp = rhs.m_opaque_sp;
}

const lldb::SBQueue &SBQueue::operator=(const lldb::SBQueue &rhs) {
  m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBQueue::~SBQueue() {}

bool SBQueue::IsValid() const {
  bool is_valid = m_opaque_sp->IsValid();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::IsValid() == %s",
                m_opaque_sp->GetQueueID(), is_valid ? "true" : "false");
  return is_valid;
}

void SBQueue::Clear() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::Clear()", m_opaque_sp->GetQueueID());
  m_opaque_sp->Clear();
}

void SBQueue::SetQueue(const QueueSP &queue_sp) {
  m_opaque_sp->SetQueue(queue_sp);
}

lldb::queue_id_t SBQueue::GetQueueID() const {
  lldb::queue_id_t qid = m_opaque_sp->GetQueueID();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetQueueID() == 0x%" PRIx64,
                m_opaque_sp->GetQueueID(), (uint64_t)qid);
  return qid;
}

uint32_t SBQueue::GetIndexID() const {
  uint32_t index_id = m_opaque_sp->GetIndexID();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetIndexID() == 0x%" PRIx32,
                m_opaque_sp->GetQueueID(), index_id);
  return index_id;
}

const char *SBQueue::GetName() const {
  const char *name = m_opaque_sp->GetName();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetName() == %s",
                m_opaque_sp->GetQueueID(), name ? name : "");
  return name;
}

uint32_t SBQueue::GetNumThreads() {
  uint32_t numthreads = m_opaque_sp->GetNumThreads();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetNumThreads() == %d",
                m_opaque_sp->GetQueueID(), numthreads);
  return numthreads;
}

SBThread SBQueue::GetThreadAtIndex(uint32_t idx) {
  SBThread th = m_opaque_sp->GetThreadAtIndex(idx);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetThreadAtIndex(%d)",
                m_opaque_sp->GetQueueID(), idx);
  return th;
}

uint32_t SBQueue::GetNumPendingItems() {
  uint32_t pending_items = m_opaque_sp->GetNumPendingItems();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetNumPendingItems() == %d",
                m_opaque_sp->GetQueueID(), pending_items);
  return pending_items;
}

SBQueueItem SBQueue::GetPendingItemAtIndex(uint32_t idx) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetPendingItemAtIndex(%d)",
                m_opaque_sp->GetQueueID(), idx);
  return m_opaque_sp->GetPendingItemAtIndex(idx);
}

uint32_t SBQueue::GetNumRunningItems() {
  uint32_t running_items = m_opaque_sp->GetNumRunningItems();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueue(0x%" PRIx64 ")::GetNumRunningItems() == %d",
                m_opaque_sp->GetQueueID(), running_items);
  return running_items;
}

SBProcess SBQueue::GetProcess() { return m_opaque_sp->GetProcess(); }

lldb::QueueKind SBQueue::GetKind() { return m_opaque_sp->GetKind(); }
