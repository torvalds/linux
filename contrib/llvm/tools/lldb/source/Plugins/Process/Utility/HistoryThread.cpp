//===-- HistoryThread.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"

#include "Plugins/Process/Utility/HistoryThread.h"
#include "Plugins/Process/Utility/HistoryUnwind.h"
#include "Plugins/Process/Utility/RegisterContextHistory.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

//  Constructor

HistoryThread::HistoryThread(lldb_private::Process &process, lldb::tid_t tid,
                             std::vector<lldb::addr_t> pcs, uint32_t stop_id,
                             bool stop_id_is_valid)
    : Thread(process, tid, true), m_framelist_mutex(), m_framelist(),
      m_pcs(pcs), m_stop_id(stop_id), m_stop_id_is_valid(stop_id_is_valid),
      m_extended_unwind_token(LLDB_INVALID_ADDRESS), m_queue_name(),
      m_thread_name(), m_originating_unique_thread_id(tid),
      m_queue_id(LLDB_INVALID_QUEUE_ID) {
  m_unwinder_ap.reset(new HistoryUnwind(*this, pcs, stop_id_is_valid));
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));
  if (log)
    log->Printf("%p HistoryThread::HistoryThread", static_cast<void *>(this));
}

//  Destructor

HistoryThread::~HistoryThread() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));
  if (log)
    log->Printf("%p HistoryThread::~HistoryThread (tid=0x%" PRIx64 ")",
                static_cast<void *>(this), GetID());
  DestroyThread();
}

lldb::RegisterContextSP HistoryThread::GetRegisterContext() {
  RegisterContextSP rctx;
  if (m_pcs.size() > 0) {
    rctx.reset(new RegisterContextHistory(
        *this, 0, GetProcess()->GetAddressByteSize(), m_pcs[0]));
  }
  return rctx;
}

lldb::RegisterContextSP
HistoryThread::CreateRegisterContextForFrame(StackFrame *frame) {
  return m_unwinder_ap->CreateRegisterContextForFrame(frame);
}

lldb::StackFrameListSP HistoryThread::GetStackFrameList() {
  // FIXME do not throw away the lock after we acquire it..
  std::unique_lock<std::mutex> lock(m_framelist_mutex);
  lock.unlock();
  if (m_framelist.get() == NULL) {
    m_framelist.reset(new StackFrameList(*this, StackFrameListSP(), true));
  }

  return m_framelist;
}

uint32_t HistoryThread::GetExtendedBacktraceOriginatingIndexID() {
  if (m_originating_unique_thread_id != LLDB_INVALID_THREAD_ID) {
    if (GetProcess()->HasAssignedIndexIDToThread(
            m_originating_unique_thread_id)) {
      return GetProcess()->AssignIndexIDToThread(
          m_originating_unique_thread_id);
    }
  }
  return LLDB_INVALID_THREAD_ID;
}
