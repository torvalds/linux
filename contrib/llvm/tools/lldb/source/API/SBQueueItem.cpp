//===-- SBQueueItem.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-forward.h"

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBQueueItem.h"
#include "lldb/API/SBThread.h"
#include "lldb/Core/Address.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/QueueItem.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------
SBQueueItem::SBQueueItem() : m_queue_item_sp() {}

SBQueueItem::SBQueueItem(const QueueItemSP &queue_item_sp)
    : m_queue_item_sp(queue_item_sp) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBQueueItem::~SBQueueItem() { m_queue_item_sp.reset(); }

bool SBQueueItem::IsValid() const {
  bool is_valid = m_queue_item_sp.get() != NULL;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueueItem(%p)::IsValid() == %s",
                static_cast<void *>(m_queue_item_sp.get()),
                is_valid ? "true" : "false");
  return is_valid;
}

void SBQueueItem::Clear() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBQueueItem(%p)::Clear()",
                static_cast<void *>(m_queue_item_sp.get()));
  m_queue_item_sp.reset();
}

void SBQueueItem::SetQueueItem(const QueueItemSP &queue_item_sp) {
  m_queue_item_sp = queue_item_sp;
}

lldb::QueueItemKind SBQueueItem::GetKind() const {
  QueueItemKind result = eQueueItemKindUnknown;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_queue_item_sp) {
    result = m_queue_item_sp->GetKind();
  }
  if (log)
    log->Printf("SBQueueItem(%p)::GetKind() == %d",
                static_cast<void *>(m_queue_item_sp.get()),
                static_cast<int>(result));
  return result;
}

void SBQueueItem::SetKind(lldb::QueueItemKind kind) {
  if (m_queue_item_sp) {
    m_queue_item_sp->SetKind(kind);
  }
}

SBAddress SBQueueItem::GetAddress() const {
  SBAddress result;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_queue_item_sp) {
    result.SetAddress(&m_queue_item_sp->GetAddress());
  }
  if (log) {
    StreamString sstr;
    const Address *addr = result.get();
    if (addr)
      addr->Dump(&sstr, NULL, Address::DumpStyleModuleWithFileAddress,
                 Address::DumpStyleInvalid, 4);
    log->Printf("SBQueueItem(%p)::GetAddress() == SBAddress(%p): %s",
                static_cast<void *>(m_queue_item_sp.get()),
                static_cast<void *>(result.get()), sstr.GetData());
  }
  return result;
}

void SBQueueItem::SetAddress(SBAddress addr) {
  if (m_queue_item_sp) {
    m_queue_item_sp->SetAddress(addr.ref());
  }
}

SBThread SBQueueItem::GetExtendedBacktraceThread(const char *type) {
  SBThread result;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_queue_item_sp) {
    ProcessSP process_sp = m_queue_item_sp->GetProcessSP();
    Process::StopLocker stop_locker;
    if (process_sp && stop_locker.TryLock(&process_sp->GetRunLock())) {
      ThreadSP thread_sp;
      ConstString type_const(type);
      thread_sp = m_queue_item_sp->GetExtendedBacktraceThread(type_const);
      if (thread_sp) {
        // Save this in the Process' ExtendedThreadList so a strong pointer
        // retains the object
        process_sp->GetExtendedThreadList().AddThread(thread_sp);
        result.SetThread(thread_sp);
        if (log) {
          const char *queue_name = thread_sp->GetQueueName();
          if (queue_name == NULL)
            queue_name = "";
          log->Printf(
              "SBQueueItem(%p)::GetExtendedBacktraceThread() = new extended "
              "Thread created (%p) with queue_id 0x%" PRIx64 " queue name '%s'",
              static_cast<void *>(m_queue_item_sp.get()),
              static_cast<void *>(thread_sp.get()),
              static_cast<uint64_t>(thread_sp->GetQueueID()), queue_name);
        }
      }
    }
  }
  return result;
}
