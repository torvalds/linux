//===-- ThreadGDBRemote.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ThreadGDBRemote.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "ProcessGDBRemote.h"
#include "ProcessGDBRemoteLog.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

//----------------------------------------------------------------------
// Thread Registers
//----------------------------------------------------------------------

ThreadGDBRemote::ThreadGDBRemote(Process &process, lldb::tid_t tid)
    : Thread(process, tid), m_thread_name(), m_dispatch_queue_name(),
      m_thread_dispatch_qaddr(LLDB_INVALID_ADDRESS),
      m_dispatch_queue_t(LLDB_INVALID_ADDRESS), m_queue_kind(eQueueKindUnknown),
      m_queue_serial_number(LLDB_INVALID_QUEUE_ID),
      m_associated_with_libdispatch_queue(eLazyBoolCalculate) {
  Log *log(GetLogIfAnyCategoriesSet(GDBR_LOG_THREAD));
  LLDB_LOG(log, "this = {0}, pid = {1}, tid = {2}", this, process.GetID(),
           GetID());
}

ThreadGDBRemote::~ThreadGDBRemote() {
  ProcessSP process_sp(GetProcess());
  Log *log(GetLogIfAnyCategoriesSet(GDBR_LOG_THREAD));
  LLDB_LOG(log, "this = {0}, pid = {1}, tid = {2}", this,
           process_sp ? process_sp->GetID() : LLDB_INVALID_PROCESS_ID, GetID());
  DestroyThread();
}

const char *ThreadGDBRemote::GetName() {
  if (m_thread_name.empty())
    return nullptr;
  return m_thread_name.c_str();
}

void ThreadGDBRemote::ClearQueueInfo() {
  m_dispatch_queue_name.clear();
  m_queue_kind = eQueueKindUnknown;
  m_queue_serial_number = 0;
  m_dispatch_queue_t = LLDB_INVALID_ADDRESS;
  m_associated_with_libdispatch_queue = eLazyBoolCalculate;
}

void ThreadGDBRemote::SetQueueInfo(std::string &&queue_name,
                                   QueueKind queue_kind, uint64_t queue_serial,
                                   addr_t dispatch_queue_t,
                                   LazyBool associated_with_libdispatch_queue) {
  m_dispatch_queue_name = queue_name;
  m_queue_kind = queue_kind;
  m_queue_serial_number = queue_serial;
  m_dispatch_queue_t = dispatch_queue_t;
  m_associated_with_libdispatch_queue = associated_with_libdispatch_queue;
}

const char *ThreadGDBRemote::GetQueueName() {
  // If our cached queue info is valid, then someone called
  // ThreadGDBRemote::SetQueueInfo(...) with valid information that was gleaned
  // from the stop reply packet. In this case we trust that the info is valid
  // in m_dispatch_queue_name without refetching it
  if (CachedQueueInfoIsValid()) {
    if (m_dispatch_queue_name.empty())
      return nullptr;
    else
      return m_dispatch_queue_name.c_str();
  }
  // Always re-fetch the dispatch queue name since it can change

  if (m_associated_with_libdispatch_queue == eLazyBoolNo)
    return nullptr;

  if (m_thread_dispatch_qaddr != 0 &&
      m_thread_dispatch_qaddr != LLDB_INVALID_ADDRESS) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      SystemRuntime *runtime = process_sp->GetSystemRuntime();
      if (runtime)
        m_dispatch_queue_name =
            runtime->GetQueueNameFromThreadQAddress(m_thread_dispatch_qaddr);
      else
        m_dispatch_queue_name.clear();

      if (!m_dispatch_queue_name.empty())
        return m_dispatch_queue_name.c_str();
    }
  }
  return nullptr;
}

QueueKind ThreadGDBRemote::GetQueueKind() {
  // If our cached queue info is valid, then someone called
  // ThreadGDBRemote::SetQueueInfo(...) with valid information that was gleaned
  // from the stop reply packet. In this case we trust that the info is valid
  // in m_dispatch_queue_name without refetching it
  if (CachedQueueInfoIsValid()) {
    return m_queue_kind;
  }

  if (m_associated_with_libdispatch_queue == eLazyBoolNo)
    return eQueueKindUnknown;

  if (m_thread_dispatch_qaddr != 0 &&
      m_thread_dispatch_qaddr != LLDB_INVALID_ADDRESS) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      SystemRuntime *runtime = process_sp->GetSystemRuntime();
      if (runtime)
        m_queue_kind = runtime->GetQueueKind(m_thread_dispatch_qaddr);
      return m_queue_kind;
    }
  }
  return eQueueKindUnknown;
}

queue_id_t ThreadGDBRemote::GetQueueID() {
  // If our cached queue info is valid, then someone called
  // ThreadGDBRemote::SetQueueInfo(...) with valid information that was gleaned
  // from the stop reply packet. In this case we trust that the info is valid
  // in m_dispatch_queue_name without refetching it
  if (CachedQueueInfoIsValid())
    return m_queue_serial_number;

  if (m_associated_with_libdispatch_queue == eLazyBoolNo)
    return LLDB_INVALID_QUEUE_ID;

  if (m_thread_dispatch_qaddr != 0 &&
      m_thread_dispatch_qaddr != LLDB_INVALID_ADDRESS) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      SystemRuntime *runtime = process_sp->GetSystemRuntime();
      if (runtime) {
        return runtime->GetQueueIDFromThreadQAddress(m_thread_dispatch_qaddr);
      }
    }
  }
  return LLDB_INVALID_QUEUE_ID;
}

QueueSP ThreadGDBRemote::GetQueue() {
  queue_id_t queue_id = GetQueueID();
  QueueSP queue;
  if (queue_id != LLDB_INVALID_QUEUE_ID) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      queue = process_sp->GetQueueList().FindQueueByID(queue_id);
    }
  }
  return queue;
}

addr_t ThreadGDBRemote::GetQueueLibdispatchQueueAddress() {
  if (m_dispatch_queue_t == LLDB_INVALID_ADDRESS) {
    if (m_thread_dispatch_qaddr != 0 &&
        m_thread_dispatch_qaddr != LLDB_INVALID_ADDRESS) {
      ProcessSP process_sp(GetProcess());
      if (process_sp) {
        SystemRuntime *runtime = process_sp->GetSystemRuntime();
        if (runtime) {
          m_dispatch_queue_t =
              runtime->GetLibdispatchQueueAddressFromThreadQAddress(
                  m_thread_dispatch_qaddr);
        }
      }
    }
  }
  return m_dispatch_queue_t;
}

void ThreadGDBRemote::SetQueueLibdispatchQueueAddress(
    lldb::addr_t dispatch_queue_t) {
  m_dispatch_queue_t = dispatch_queue_t;
}

bool ThreadGDBRemote::ThreadHasQueueInformation() const {
  return m_thread_dispatch_qaddr != 0 &&
         m_thread_dispatch_qaddr != LLDB_INVALID_ADDRESS &&
         m_dispatch_queue_t != LLDB_INVALID_ADDRESS &&
         m_queue_kind != eQueueKindUnknown && m_queue_serial_number != 0;
}

LazyBool ThreadGDBRemote::GetAssociatedWithLibdispatchQueue() {
  return m_associated_with_libdispatch_queue;
}

void ThreadGDBRemote::SetAssociatedWithLibdispatchQueue(
    LazyBool associated_with_libdispatch_queue) {
  m_associated_with_libdispatch_queue = associated_with_libdispatch_queue;
}

StructuredData::ObjectSP ThreadGDBRemote::FetchThreadExtendedInfo() {
  StructuredData::ObjectSP object_sp;
  const lldb::user_id_t tid = GetProtocolID();
  Log *log(GetLogIfAnyCategoriesSet(GDBR_LOG_THREAD));
  if (log)
    log->Printf("Fetching extended information for thread %4.4" PRIx64, tid);
  ProcessSP process_sp(GetProcess());
  if (process_sp) {
    ProcessGDBRemote *gdb_process =
        static_cast<ProcessGDBRemote *>(process_sp.get());
    object_sp = gdb_process->GetExtendedInfoForThread(tid);
  }
  return object_sp;
}

void ThreadGDBRemote::WillResume(StateType resume_state) {
  int signo = GetResumeSignal();
  const lldb::user_id_t tid = GetProtocolID();
  Log *log(GetLogIfAnyCategoriesSet(GDBR_LOG_THREAD));
  if (log)
    log->Printf("Resuming thread: %4.4" PRIx64 " with state: %s.", tid,
                StateAsCString(resume_state));

  ProcessSP process_sp(GetProcess());
  if (process_sp) {
    ProcessGDBRemote *gdb_process =
        static_cast<ProcessGDBRemote *>(process_sp.get());
    switch (resume_state) {
    case eStateSuspended:
    case eStateStopped:
      // Don't append anything for threads that should stay stopped.
      break;

    case eStateRunning:
      if (gdb_process->GetUnixSignals()->SignalIsValid(signo))
        gdb_process->m_continue_C_tids.push_back(std::make_pair(tid, signo));
      else
        gdb_process->m_continue_c_tids.push_back(tid);
      break;

    case eStateStepping:
      if (gdb_process->GetUnixSignals()->SignalIsValid(signo))
        gdb_process->m_continue_S_tids.push_back(std::make_pair(tid, signo));
      else
        gdb_process->m_continue_s_tids.push_back(tid);
      break;

    default:
      break;
    }
  }
}

void ThreadGDBRemote::RefreshStateAfterStop() {
  // Invalidate all registers in our register context. We don't set "force" to
  // true because the stop reply packet might have had some register values
  // that were expedited and these will already be copied into the register
  // context by the time this function gets called. The
  // GDBRemoteRegisterContext class has been made smart enough to detect when
  // it needs to invalidate which registers are valid by putting hooks in the
  // register read and register supply functions where they check the process
  // stop ID and do the right thing.
  const bool force = false;
  GetRegisterContext()->InvalidateIfNeeded(force);
}

bool ThreadGDBRemote::ThreadIDIsValid(lldb::tid_t thread) {
  return thread != 0;
}

void ThreadGDBRemote::Dump(Log *log, uint32_t index) {}

bool ThreadGDBRemote::ShouldStop(bool &step_more) { return true; }
lldb::RegisterContextSP ThreadGDBRemote::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  return m_reg_context_sp;
}

lldb::RegisterContextSP
ThreadGDBRemote::CreateRegisterContextForFrame(StackFrame *frame) {
  lldb::RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    ProcessSP process_sp(GetProcess());
    if (process_sp) {
      ProcessGDBRemote *gdb_process =
          static_cast<ProcessGDBRemote *>(process_sp.get());
      // read_all_registers_at_once will be true if 'p' packet is not
      // supported.
      bool read_all_registers_at_once =
          !gdb_process->GetGDBRemote().GetpPacketSupported(GetID());
      reg_ctx_sp.reset(new GDBRemoteRegisterContext(
          *this, concrete_frame_idx, gdb_process->m_register_info,
          read_all_registers_at_once));
    }
  } else {
    Unwind *unwinder = GetUnwinder();
    if (unwinder != nullptr)
      reg_ctx_sp = unwinder->CreateRegisterContextForFrame(frame);
  }
  return reg_ctx_sp;
}

bool ThreadGDBRemote::PrivateSetRegisterValue(uint32_t reg,
                                              llvm::ArrayRef<uint8_t> data) {
  GDBRemoteRegisterContext *gdb_reg_ctx =
      static_cast<GDBRemoteRegisterContext *>(GetRegisterContext().get());
  assert(gdb_reg_ctx);
  return gdb_reg_ctx->PrivateSetRegisterValue(reg, data);
}

bool ThreadGDBRemote::PrivateSetRegisterValue(uint32_t reg, uint64_t regval) {
  GDBRemoteRegisterContext *gdb_reg_ctx =
      static_cast<GDBRemoteRegisterContext *>(GetRegisterContext().get());
  assert(gdb_reg_ctx);
  return gdb_reg_ctx->PrivateSetRegisterValue(reg, regval);
}

bool ThreadGDBRemote::CalculateStopInfo() {
  ProcessSP process_sp(GetProcess());
  if (process_sp)
    return static_cast<ProcessGDBRemote *>(process_sp.get())
        ->CalculateThreadStopInfo(this);
  return false;
}
