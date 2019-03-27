//===-- SBThread.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBThread.h"

#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Queue.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepRange.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StructuredData.h"

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBThreadCollection.h"
#include "lldb/API/SBThreadPlan.h"
#include "lldb/API/SBValue.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

const char *SBThread::GetBroadcasterClassName() {
  return Thread::GetStaticBroadcasterClass().AsCString();
}

//----------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------
SBThread::SBThread() : m_opaque_sp(new ExecutionContextRef()) {}

SBThread::SBThread(const ThreadSP &lldb_object_sp)
    : m_opaque_sp(new ExecutionContextRef(lldb_object_sp)) {}

SBThread::SBThread(const SBThread &rhs)
    : m_opaque_sp(new ExecutionContextRef(*rhs.m_opaque_sp)) {}

//----------------------------------------------------------------------
// Assignment operator
//----------------------------------------------------------------------

const lldb::SBThread &SBThread::operator=(const SBThread &rhs) {
  if (this != &rhs)
    *m_opaque_sp = *rhs.m_opaque_sp;
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBThread::~SBThread() {}

lldb::SBQueue SBThread::GetQueue() const {
  SBQueue sb_queue;
  QueueSP queue_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      queue_sp = exe_ctx.GetThreadPtr()->GetQueue();
      if (queue_sp) {
        sb_queue.SetQueue(queue_sp);
      }
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetQueue() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetQueue () => SBQueue(%p)",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                static_cast<void *>(queue_sp.get()));

  return sb_queue;
}

bool SBThread::IsValid() const {
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock()))
      return m_opaque_sp->GetThreadSP().get() != NULL;
  }
  // Without a valid target & process, this thread can't be valid.
  return false;
}

void SBThread::Clear() { m_opaque_sp->Clear(); }

StopReason SBThread::GetStopReason() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  StopReason reason = eStopReasonInvalid;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      return exe_ctx.GetThreadPtr()->GetStopReason();
    } else {
      if (log)
        log->Printf(
            "SBThread(%p)::GetStopReason() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetStopReason () => %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                Thread::StopReasonAsCString(reason));

  return reason;
}

size_t SBThread::GetStopReasonDataCount() {
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      StopInfoSP stop_info_sp = exe_ctx.GetThreadPtr()->GetStopInfo();
      if (stop_info_sp) {
        StopReason reason = stop_info_sp->GetStopReason();
        switch (reason) {
        case eStopReasonInvalid:
        case eStopReasonNone:
        case eStopReasonTrace:
        case eStopReasonExec:
        case eStopReasonPlanComplete:
        case eStopReasonThreadExiting:
        case eStopReasonInstrumentation:
          // There is no data for these stop reasons.
          return 0;

        case eStopReasonBreakpoint: {
          break_id_t site_id = stop_info_sp->GetValue();
          lldb::BreakpointSiteSP bp_site_sp(
              exe_ctx.GetProcessPtr()->GetBreakpointSiteList().FindByID(
                  site_id));
          if (bp_site_sp)
            return bp_site_sp->GetNumberOfOwners() * 2;
          else
            return 0; // Breakpoint must have cleared itself...
        } break;

        case eStopReasonWatchpoint:
          return 1;

        case eStopReasonSignal:
          return 1;

        case eStopReasonException:
          return 1;
        }
      }
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBThread(%p)::GetStopReasonDataCount() => error: process "
                    "is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }
  return 0;
}

uint64_t SBThread::GetStopReasonDataAtIndex(uint32_t idx) {
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      Thread *thread = exe_ctx.GetThreadPtr();
      StopInfoSP stop_info_sp = thread->GetStopInfo();
      if (stop_info_sp) {
        StopReason reason = stop_info_sp->GetStopReason();
        switch (reason) {
        case eStopReasonInvalid:
        case eStopReasonNone:
        case eStopReasonTrace:
        case eStopReasonExec:
        case eStopReasonPlanComplete:
        case eStopReasonThreadExiting:
        case eStopReasonInstrumentation:
          // There is no data for these stop reasons.
          return 0;

        case eStopReasonBreakpoint: {
          break_id_t site_id = stop_info_sp->GetValue();
          lldb::BreakpointSiteSP bp_site_sp(
              exe_ctx.GetProcessPtr()->GetBreakpointSiteList().FindByID(
                  site_id));
          if (bp_site_sp) {
            uint32_t bp_index = idx / 2;
            BreakpointLocationSP bp_loc_sp(
                bp_site_sp->GetOwnerAtIndex(bp_index));
            if (bp_loc_sp) {
              if (idx & 1) {
                // Odd idx, return the breakpoint location ID
                return bp_loc_sp->GetID();
              } else {
                // Even idx, return the breakpoint ID
                return bp_loc_sp->GetBreakpoint().GetID();
              }
            }
          }
          return LLDB_INVALID_BREAK_ID;
        } break;

        case eStopReasonWatchpoint:
          return stop_info_sp->GetValue();

        case eStopReasonSignal:
          return stop_info_sp->GetValue();

        case eStopReasonException:
          return stop_info_sp->GetValue();
        }
      }
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf("SBThread(%p)::GetStopReasonDataAtIndex() => error: "
                    "process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }
  return 0;
}

bool SBThread::GetStopReasonExtendedInfoAsJSON(lldb::SBStream &stream) {
  Stream &strm = stream.ref();

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (!exe_ctx.HasThreadScope())
    return false;

  StopInfoSP stop_info = exe_ctx.GetThreadPtr()->GetStopInfo();
  StructuredData::ObjectSP info = stop_info->GetExtendedInfo();
  if (!info)
    return false;

  info->Dump(strm);

  return true;
}

SBThreadCollection
SBThread::GetStopReasonExtendedBacktraces(InstrumentationRuntimeType type) {
  ThreadCollectionSP threads;
  threads.reset(new ThreadCollection());

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (!exe_ctx.HasThreadScope())
    return threads;

  ProcessSP process_sp = exe_ctx.GetProcessSP();

  StopInfoSP stop_info = exe_ctx.GetThreadPtr()->GetStopInfo();
  StructuredData::ObjectSP info = stop_info->GetExtendedInfo();
  if (!info)
    return threads;

  return process_sp->GetInstrumentationRuntime(type)
      ->GetBacktracesFromExtendedStopInfo(info);
}

size_t SBThread::GetStopDescription(char *dst, size_t dst_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {

      StopInfoSP stop_info_sp = exe_ctx.GetThreadPtr()->GetStopInfo();
      if (stop_info_sp) {
        const char *stop_desc = stop_info_sp->GetDescription();
        if (stop_desc) {
          if (log)
            log->Printf(
                "SBThread(%p)::GetStopDescription (dst, dst_len) => \"%s\"",
                static_cast<void *>(exe_ctx.GetThreadPtr()), stop_desc);
          if (dst)
            return ::snprintf(dst, dst_len, "%s", stop_desc);
          else {
            // NULL dst passed in, return the length needed to contain the
            // description
            return ::strlen(stop_desc) + 1; // Include the NULL byte for size
          }
        } else {
          size_t stop_desc_len = 0;
          switch (stop_info_sp->GetStopReason()) {
          case eStopReasonTrace:
          case eStopReasonPlanComplete: {
            static char trace_desc[] = "step";
            stop_desc = trace_desc;
            stop_desc_len =
                sizeof(trace_desc); // Include the NULL byte for size
          } break;

          case eStopReasonBreakpoint: {
            static char bp_desc[] = "breakpoint hit";
            stop_desc = bp_desc;
            stop_desc_len = sizeof(bp_desc); // Include the NULL byte for size
          } break;

          case eStopReasonWatchpoint: {
            static char wp_desc[] = "watchpoint hit";
            stop_desc = wp_desc;
            stop_desc_len = sizeof(wp_desc); // Include the NULL byte for size
          } break;

          case eStopReasonSignal: {
            stop_desc =
                exe_ctx.GetProcessPtr()->GetUnixSignals()->GetSignalAsCString(
                    stop_info_sp->GetValue());
            if (stop_desc == NULL || stop_desc[0] == '\0') {
              static char signal_desc[] = "signal";
              stop_desc = signal_desc;
              stop_desc_len =
                  sizeof(signal_desc); // Include the NULL byte for size
            }
          } break;

          case eStopReasonException: {
            char exc_desc[] = "exception";
            stop_desc = exc_desc;
            stop_desc_len = sizeof(exc_desc); // Include the NULL byte for size
          } break;

          case eStopReasonExec: {
            char exc_desc[] = "exec";
            stop_desc = exc_desc;
            stop_desc_len = sizeof(exc_desc); // Include the NULL byte for size
          } break;

          case eStopReasonThreadExiting: {
            char limbo_desc[] = "thread exiting";
            stop_desc = limbo_desc;
            stop_desc_len = sizeof(limbo_desc);
          } break;
          default:
            break;
          }

          if (stop_desc && stop_desc[0]) {
            if (log)
              log->Printf(
                  "SBThread(%p)::GetStopDescription (dst, dst_len) => '%s'",
                  static_cast<void *>(exe_ctx.GetThreadPtr()), stop_desc);

            if (dst)
              return ::snprintf(dst, dst_len, "%s", stop_desc) +
                     1; // Include the NULL byte

            if (stop_desc_len == 0)
              stop_desc_len = ::strlen(stop_desc) + 1; // Include the NULL byte

            return stop_desc_len;
          }
        }
      }
    } else {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
      if (log)
        log->Printf(
            "SBThread(%p)::GetStopDescription() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }
  if (dst)
    *dst = 0;
  return 0;
}

SBValue SBThread::GetStopReturnValue() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ValueObjectSP return_valobj_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      StopInfoSP stop_info_sp = exe_ctx.GetThreadPtr()->GetStopInfo();
      if (stop_info_sp) {
        return_valobj_sp = StopInfo::GetReturnValueObject(stop_info_sp);
      }
    } else {
      if (log)
        log->Printf(
            "SBThread(%p)::GetStopReturnValue() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetStopReturnValue () => %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                return_valobj_sp.get() ? return_valobj_sp->GetValueAsCString()
                                       : "<no return value>");

  return SBValue(return_valobj_sp);
}

void SBThread::SetThread(const ThreadSP &lldb_object_sp) {
  m_opaque_sp->SetThreadSP(lldb_object_sp);
}

lldb::tid_t SBThread::GetThreadID() const {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp->GetID();
  return LLDB_INVALID_THREAD_ID;
}

uint32_t SBThread::GetIndexID() const {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp->GetIndexID();
  return LLDB_INVALID_INDEX32;
}

const char *SBThread::GetName() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *name = NULL;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      name = exe_ctx.GetThreadPtr()->GetName();
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetName() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetName () => %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                name ? name : "NULL");

  return name;
}

const char *SBThread::GetQueueName() const {
  const char *name = NULL;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      name = exe_ctx.GetThreadPtr()->GetQueueName();
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetQueueName() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetQueueName () => %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                name ? name : "NULL");

  return name;
}

lldb::queue_id_t SBThread::GetQueueID() const {
  queue_id_t id = LLDB_INVALID_QUEUE_ID;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      id = exe_ctx.GetThreadPtr()->GetQueueID();
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetQueueID() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetQueueID () => 0x%" PRIx64,
                static_cast<void *>(exe_ctx.GetThreadPtr()), id);

  return id;
}

bool SBThread::GetInfoItemByPathAsString(const char *path, SBStream &strm) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool success = false;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      Thread *thread = exe_ctx.GetThreadPtr();
      StructuredData::ObjectSP info_root_sp = thread->GetExtendedInfo();
      if (info_root_sp) {
        StructuredData::ObjectSP node =
            info_root_sp->GetObjectForDotSeparatedPath(path);
        if (node) {
          if (node->GetType() == eStructuredDataTypeString) {
            strm.Printf("%s", node->GetAsString()->GetValue().str().c_str());
            success = true;
          }
          if (node->GetType() == eStructuredDataTypeInteger) {
            strm.Printf("0x%" PRIx64, node->GetAsInteger()->GetValue());
            success = true;
          }
          if (node->GetType() == eStructuredDataTypeFloat) {
            strm.Printf("0x%f", node->GetAsFloat()->GetValue());
            success = true;
          }
          if (node->GetType() == eStructuredDataTypeBoolean) {
            if (node->GetAsBoolean()->GetValue())
              strm.Printf("true");
            else
              strm.Printf("false");
            success = true;
          }
          if (node->GetType() == eStructuredDataTypeNull) {
            strm.Printf("null");
            success = true;
          }
        }
      }
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetInfoItemByPathAsString() => error: "
                    "process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetInfoItemByPathAsString (\"%s\") => \"%s\"",
                static_cast<void *>(exe_ctx.GetThreadPtr()), path, strm.GetData());

  return success;
}

SBError SBThread::ResumeNewPlan(ExecutionContext &exe_ctx,
                                ThreadPlan *new_plan) {
  SBError sb_error;

  Process *process = exe_ctx.GetProcessPtr();
  if (!process) {
    sb_error.SetErrorString("No process in SBThread::ResumeNewPlan");
    return sb_error;
  }

  Thread *thread = exe_ctx.GetThreadPtr();
  if (!thread) {
    sb_error.SetErrorString("No thread in SBThread::ResumeNewPlan");
    return sb_error;
  }

  // User level plans should be Master Plans so they can be interrupted, other
  // plans executed, and then a "continue" will resume the plan.
  if (new_plan != NULL) {
    new_plan->SetIsMasterPlan(true);
    new_plan->SetOkayToDiscard(false);
  }

  // Why do we need to set the current thread by ID here???
  process->GetThreadList().SetSelectedThreadByID(thread->GetID());

  if (process->GetTarget().GetDebugger().GetAsyncExecution())
    sb_error.ref() = process->Resume();
  else
    sb_error.ref() = process->ResumeSynchronous(NULL);

  return sb_error;
}

void SBThread::StepOver(lldb::RunMode stop_other_threads) {
  SBError error; // Ignored
  StepOver(stop_other_threads, error);
}

void SBThread::StepOver(lldb::RunMode stop_other_threads, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::StepOver (stop_other_threads='%s')",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                Thread::RunModeAsCString(stop_other_threads));

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  Thread *thread = exe_ctx.GetThreadPtr();
  bool abort_other_plans = false;
  StackFrameSP frame_sp(thread->GetStackFrameAtIndex(0));

  Status new_plan_status;
  ThreadPlanSP new_plan_sp;
  if (frame_sp) {
    if (frame_sp->HasDebugInformation()) {
      const LazyBool avoid_no_debug = eLazyBoolCalculate;
      SymbolContext sc(frame_sp->GetSymbolContext(eSymbolContextEverything));
      new_plan_sp = thread->QueueThreadPlanForStepOverRange(
          abort_other_plans, sc.line_entry, sc, stop_other_threads,
          new_plan_status, avoid_no_debug);
    } else {
      new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
          true, abort_other_plans, stop_other_threads, new_plan_status);
    }
  }
  error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
}

void SBThread::StepInto(lldb::RunMode stop_other_threads) {
  StepInto(NULL, stop_other_threads);
}

void SBThread::StepInto(const char *target_name,
                        lldb::RunMode stop_other_threads) {
  SBError error; // Ignored
  StepInto(target_name, LLDB_INVALID_LINE_NUMBER, error, stop_other_threads);
}

void SBThread::StepInto(const char *target_name, uint32_t end_line,
                        SBError &error, lldb::RunMode stop_other_threads) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf(
        "SBThread(%p)::StepInto (target_name='%s', stop_other_threads='%s')",
        static_cast<void *>(exe_ctx.GetThreadPtr()),
        target_name ? target_name : "<NULL>",
        Thread::RunModeAsCString(stop_other_threads));

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  bool abort_other_plans = false;

  Thread *thread = exe_ctx.GetThreadPtr();
  StackFrameSP frame_sp(thread->GetStackFrameAtIndex(0));
  ThreadPlanSP new_plan_sp;
  Status new_plan_status;

  if (frame_sp && frame_sp->HasDebugInformation()) {
    SymbolContext sc(frame_sp->GetSymbolContext(eSymbolContextEverything));
    AddressRange range;
    if (end_line == LLDB_INVALID_LINE_NUMBER)
      range = sc.line_entry.range;
    else {
      if (!sc.GetAddressRangeFromHereToEndLine(end_line, range, error.ref()))
        return;
    }

    const LazyBool step_out_avoids_code_without_debug_info =
        eLazyBoolCalculate;
    const LazyBool step_in_avoids_code_without_debug_info =
        eLazyBoolCalculate;
    new_plan_sp = thread->QueueThreadPlanForStepInRange(
        abort_other_plans, range, sc, target_name, stop_other_threads,
        new_plan_status, step_in_avoids_code_without_debug_info,
        step_out_avoids_code_without_debug_info);
  } else {
    new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
        false, abort_other_plans, stop_other_threads, new_plan_status);
  }

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());
}

void SBThread::StepOut() {
  SBError error; // Ignored
  StepOut(error);
}

void SBThread::StepOut(SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::StepOut ()",
                static_cast<void *>(exe_ctx.GetThreadPtr()));

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  bool abort_other_plans = false;
  bool stop_other_threads = false;

  Thread *thread = exe_ctx.GetThreadPtr();

  const LazyBool avoid_no_debug = eLazyBoolCalculate;
  Status new_plan_status;
  ThreadPlanSP new_plan_sp(thread->QueueThreadPlanForStepOut(
      abort_other_plans, NULL, false, stop_other_threads, eVoteYes,
      eVoteNoOpinion, 0, new_plan_status, avoid_no_debug));

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());
}

void SBThread::StepOutOfFrame(SBFrame &sb_frame) {
  SBError error; // Ignored
  StepOutOfFrame(sb_frame, error);
}

void SBThread::StepOutOfFrame(SBFrame &sb_frame, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (!sb_frame.IsValid()) {
    if (log)
      log->Printf(
          "SBThread(%p)::StepOutOfFrame passed an invalid frame, returning.",
          static_cast<void *>(exe_ctx.GetThreadPtr()));
    error.SetErrorString("passed invalid SBFrame object");
    return;
  }

  StackFrameSP frame_sp(sb_frame.GetFrameSP());
  if (log) {
    SBStream frame_desc_strm;
    sb_frame.GetDescription(frame_desc_strm);
    log->Printf("SBThread(%p)::StepOutOfFrame (frame = SBFrame(%p): %s)",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                static_cast<void *>(frame_sp.get()), frame_desc_strm.GetData());
  }

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  bool abort_other_plans = false;
  bool stop_other_threads = false;
  Thread *thread = exe_ctx.GetThreadPtr();
  if (sb_frame.GetThread().GetThreadID() != thread->GetID()) {
    log->Printf("SBThread(%p)::StepOutOfFrame passed a frame from another "
                "thread (0x%" PRIx64 " vrs. 0x%" PRIx64 ", returning.",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                sb_frame.GetThread().GetThreadID(), thread->GetID());
    error.SetErrorString("passed a frame from another thread");
    return;
  }

  Status new_plan_status;
  ThreadPlanSP new_plan_sp(thread->QueueThreadPlanForStepOut(
      abort_other_plans, NULL, false, stop_other_threads, eVoteYes,
      eVoteNoOpinion, frame_sp->GetFrameIndex(), new_plan_status));

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());
}

void SBThread::StepInstruction(bool step_over) {
  SBError error; // Ignored
  StepInstruction(step_over, error);
}

void SBThread::StepInstruction(bool step_over, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::StepInstruction (step_over=%i)",
                static_cast<void *>(exe_ctx.GetThreadPtr()), step_over);

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  Thread *thread = exe_ctx.GetThreadPtr();
  Status new_plan_status;
  ThreadPlanSP new_plan_sp(thread->QueueThreadPlanForStepSingleInstruction(
      step_over, true, true, new_plan_status));

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());
}

void SBThread::RunToAddress(lldb::addr_t addr) {
  SBError error; // Ignored
  RunToAddress(addr, error);
}

void SBThread::RunToAddress(lldb::addr_t addr, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::RunToAddress (addr=0x%" PRIx64 ")",
                static_cast<void *>(exe_ctx.GetThreadPtr()), addr);

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return;
  }

  bool abort_other_plans = false;
  bool stop_other_threads = true;

  Address target_addr(addr);

  Thread *thread = exe_ctx.GetThreadPtr();

  Status new_plan_status;
  ThreadPlanSP new_plan_sp(thread->QueueThreadPlanForRunToAddress(
      abort_other_plans, target_addr, stop_other_threads, new_plan_status));

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());
}

SBError SBThread::StepOverUntil(lldb::SBFrame &sb_frame,
                                lldb::SBFileSpec &sb_file_spec, uint32_t line) {
  SBError sb_error;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  char path[PATH_MAX];

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrameSP frame_sp(sb_frame.GetFrameSP());

  if (log) {
    SBStream frame_desc_strm;
    sb_frame.GetDescription(frame_desc_strm);
    sb_file_spec->GetPath(path, sizeof(path));
    log->Printf("SBThread(%p)::StepOverUntil (frame = SBFrame(%p): %s, "
                "file+line = %s:%u)",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                static_cast<void *>(frame_sp.get()), frame_desc_strm.GetData(),
                path, line);
  }

  if (exe_ctx.HasThreadScope()) {
    Target *target = exe_ctx.GetTargetPtr();
    Thread *thread = exe_ctx.GetThreadPtr();

    if (line == 0) {
      sb_error.SetErrorString("invalid line argument");
      return sb_error;
    }

    if (!frame_sp) {
      frame_sp = thread->GetSelectedFrame();
      if (!frame_sp)
        frame_sp = thread->GetStackFrameAtIndex(0);
    }

    SymbolContext frame_sc;
    if (!frame_sp) {
      sb_error.SetErrorString("no valid frames in thread to step");
      return sb_error;
    }

    // If we have a frame, get its line
    frame_sc = frame_sp->GetSymbolContext(
        eSymbolContextCompUnit | eSymbolContextFunction |
        eSymbolContextLineEntry | eSymbolContextSymbol);

    if (frame_sc.comp_unit == NULL) {
      sb_error.SetErrorStringWithFormat(
          "frame %u doesn't have debug information", frame_sp->GetFrameIndex());
      return sb_error;
    }

    FileSpec step_file_spec;
    if (sb_file_spec.IsValid()) {
      // The file spec passed in was valid, so use it
      step_file_spec = sb_file_spec.ref();
    } else {
      if (frame_sc.line_entry.IsValid())
        step_file_spec = frame_sc.line_entry.file;
      else {
        sb_error.SetErrorString("invalid file argument or no file for frame");
        return sb_error;
      }
    }

    // Grab the current function, then we will make sure the "until" address is
    // within the function.  We discard addresses that are out of the current
    // function, and then if there are no addresses remaining, give an
    // appropriate error message.

    bool all_in_function = true;
    AddressRange fun_range = frame_sc.function->GetAddressRange();

    std::vector<addr_t> step_over_until_addrs;
    const bool abort_other_plans = false;
    const bool stop_other_threads = false;
    const bool check_inlines = true;
    const bool exact = false;

    SymbolContextList sc_list;
    const uint32_t num_matches = frame_sc.comp_unit->ResolveSymbolContext(
        step_file_spec, line, check_inlines, exact, eSymbolContextLineEntry,
        sc_list);
    if (num_matches > 0) {
      SymbolContext sc;
      for (uint32_t i = 0; i < num_matches; ++i) {
        if (sc_list.GetContextAtIndex(i, sc)) {
          addr_t step_addr =
              sc.line_entry.range.GetBaseAddress().GetLoadAddress(target);
          if (step_addr != LLDB_INVALID_ADDRESS) {
            if (fun_range.ContainsLoadAddress(step_addr, target))
              step_over_until_addrs.push_back(step_addr);
            else
              all_in_function = false;
          }
        }
      }
    }

    if (step_over_until_addrs.empty()) {
      if (all_in_function) {
        step_file_spec.GetPath(path, sizeof(path));
        sb_error.SetErrorStringWithFormat("No line entries for %s:%u", path,
                                          line);
      } else
        sb_error.SetErrorString("step until target not in current function");
    } else {
      Status new_plan_status;
      ThreadPlanSP new_plan_sp(thread->QueueThreadPlanForStepUntil(
          abort_other_plans, &step_over_until_addrs[0],
          step_over_until_addrs.size(), stop_other_threads,
          frame_sp->GetFrameIndex(), new_plan_status));

      if (new_plan_status.Success())
        sb_error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
      else
        sb_error.SetErrorString(new_plan_status.AsCString());
    }
  } else {
    sb_error.SetErrorString("this SBThread object is invalid");
  }
  return sb_error;
}

SBError SBThread::StepUsingScriptedThreadPlan(const char *script_class_name) {
  return StepUsingScriptedThreadPlan(script_class_name, true);
}

SBError SBThread::StepUsingScriptedThreadPlan(const char *script_class_name,
                                              bool resume_immediately) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  SBError error;

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log) {
    log->Printf("SBThread(%p)::StepUsingScriptedThreadPlan: class name: %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()), script_class_name);
  }

  if (!exe_ctx.HasThreadScope()) {
    error.SetErrorString("this SBThread object is invalid");
    return error;
  }

  Thread *thread = exe_ctx.GetThreadPtr();
  Status new_plan_status;
  ThreadPlanSP new_plan_sp = thread->QueueThreadPlanForStepScripted(
      false, script_class_name, false, new_plan_status);

  if (new_plan_status.Fail()) {
    error.SetErrorString(new_plan_status.AsCString());
    return error;
  }

  if (!resume_immediately)
    return error;

  if (new_plan_status.Success())
    error = ResumeNewPlan(exe_ctx, new_plan_sp.get());
  else
    error.SetErrorString(new_plan_status.AsCString());

  return error;
}

SBError SBThread::JumpToLine(lldb::SBFileSpec &file_spec, uint32_t line) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  SBError sb_error;

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::JumpToLine (file+line = %s:%u)",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                file_spec->GetPath().c_str(), line);

  if (!exe_ctx.HasThreadScope()) {
    sb_error.SetErrorString("this SBThread object is invalid");
    return sb_error;
  }

  Thread *thread = exe_ctx.GetThreadPtr();

  Status err = thread->JumpToLine(file_spec.get(), line, true);
  sb_error.SetError(err);
  return sb_error;
}

SBError SBThread::ReturnFromFrame(SBFrame &frame, SBValue &return_value) {
  SBError sb_error;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::ReturnFromFrame (frame=%d)",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                frame.GetFrameID());

  if (exe_ctx.HasThreadScope()) {
    Thread *thread = exe_ctx.GetThreadPtr();
    sb_error.SetError(
        thread->ReturnFromFrame(frame.GetFrameSP(), return_value.GetSP()));
  }

  return sb_error;
}

SBError SBThread::UnwindInnermostExpression() {
  SBError sb_error;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (log)
    log->Printf("SBThread(%p)::UnwindExpressionEvaluation",
                static_cast<void *>(exe_ctx.GetThreadPtr()));

  if (exe_ctx.HasThreadScope()) {
    Thread *thread = exe_ctx.GetThreadPtr();
    sb_error.SetError(thread->UnwindInnermostExpression());
    if (sb_error.Success())
      thread->SetSelectedFrameByIndex(0, false);
  }

  return sb_error;
}

bool SBThread::Suspend() {
  SBError error; // Ignored
  return Suspend(error);
}

bool SBThread::Suspend(SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  bool result = false;
  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      exe_ctx.GetThreadPtr()->SetResumeState(eStateSuspended);
      result = true;
    } else {
      error.SetErrorString("process is running");
      if (log)
        log->Printf("SBThread(%p)::Suspend() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  } else
    error.SetErrorString("this SBThread object is invalid");
  if (log)
    log->Printf("SBThread(%p)::Suspend() => %i",
                static_cast<void *>(exe_ctx.GetThreadPtr()), result);
  return result;
}

bool SBThread::Resume() {
  SBError error; // Ignored
  return Resume(error);
}

bool SBThread::Resume(SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  bool result = false;
  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      const bool override_suspend = true;
      exe_ctx.GetThreadPtr()->SetResumeState(eStateRunning, override_suspend);
      result = true;
    } else {
      error.SetErrorString("process is running");
      if (log)
        log->Printf("SBThread(%p)::Resume() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  } else
    error.SetErrorString("this SBThread object is invalid");
  if (log)
    log->Printf("SBThread(%p)::Resume() => %i",
                static_cast<void *>(exe_ctx.GetThreadPtr()), result);
  return result;
}

bool SBThread::IsSuspended() {
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope())
    return exe_ctx.GetThreadPtr()->GetResumeState() == eStateSuspended;
  return false;
}

bool SBThread::IsStopped() {
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope())
    return StateIsStoppedState(exe_ctx.GetThreadPtr()->GetState(), true);
  return false;
}

SBProcess SBThread::GetProcess() {
  SBProcess sb_process;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    // Have to go up to the target so we can get a shared pointer to our
    // process...
    sb_process.SetSP(exe_ctx.GetProcessSP());
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    SBStream frame_desc_strm;
    sb_process.GetDescription(frame_desc_strm);
    log->Printf("SBThread(%p)::GetProcess () => SBProcess(%p): %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                static_cast<void *>(sb_process.GetSP().get()),
                frame_desc_strm.GetData());
  }

  return sb_process;
}

uint32_t SBThread::GetNumFrames() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  uint32_t num_frames = 0;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      num_frames = exe_ctx.GetThreadPtr()->GetStackFrameCount();
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetNumFrames() => error: process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log)
    log->Printf("SBThread(%p)::GetNumFrames () => %u",
                static_cast<void *>(exe_ctx.GetThreadPtr()), num_frames);

  return num_frames;
}

SBFrame SBThread::GetFrameAtIndex(uint32_t idx) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFrame sb_frame;
  StackFrameSP frame_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      frame_sp = exe_ctx.GetThreadPtr()->GetStackFrameAtIndex(idx);
      sb_frame.SetFrameSP(frame_sp);
    } else {
      if (log)
        log->Printf(
            "SBThread(%p)::GetFrameAtIndex() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log) {
    SBStream frame_desc_strm;
    sb_frame.GetDescription(frame_desc_strm);
    log->Printf("SBThread(%p)::GetFrameAtIndex (idx=%d) => SBFrame(%p): %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()), idx,
                static_cast<void *>(frame_sp.get()), frame_desc_strm.GetData());
  }

  return sb_frame;
}

lldb::SBFrame SBThread::GetSelectedFrame() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFrame sb_frame;
  StackFrameSP frame_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      frame_sp = exe_ctx.GetThreadPtr()->GetSelectedFrame();
      sb_frame.SetFrameSP(frame_sp);
    } else {
      if (log)
        log->Printf(
            "SBThread(%p)::GetSelectedFrame() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log) {
    SBStream frame_desc_strm;
    sb_frame.GetDescription(frame_desc_strm);
    log->Printf("SBThread(%p)::GetSelectedFrame () => SBFrame(%p): %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()),
                static_cast<void *>(frame_sp.get()), frame_desc_strm.GetData());
  }

  return sb_frame;
}

lldb::SBFrame SBThread::SetSelectedFrame(uint32_t idx) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBFrame sb_frame;
  StackFrameSP frame_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      Thread *thread = exe_ctx.GetThreadPtr();
      frame_sp = thread->GetStackFrameAtIndex(idx);
      if (frame_sp) {
        thread->SetSelectedFrame(frame_sp.get());
        sb_frame.SetFrameSP(frame_sp);
      }
    } else {
      if (log)
        log->Printf(
            "SBThread(%p)::SetSelectedFrame() => error: process is running",
            static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log) {
    SBStream frame_desc_strm;
    sb_frame.GetDescription(frame_desc_strm);
    log->Printf("SBThread(%p)::SetSelectedFrame (idx=%u) => SBFrame(%p): %s",
                static_cast<void *>(exe_ctx.GetThreadPtr()), idx,
                static_cast<void *>(frame_sp.get()), frame_desc_strm.GetData());
  }
  return sb_frame;
}

bool SBThread::EventIsThreadEvent(const SBEvent &event) {
  return Thread::ThreadEventData::GetEventDataFromEvent(event.get()) != NULL;
}

SBFrame SBThread::GetStackFrameFromEvent(const SBEvent &event) {
  return Thread::ThreadEventData::GetStackFrameFromEvent(event.get());
}

SBThread SBThread::GetThreadFromEvent(const SBEvent &event) {
  return Thread::ThreadEventData::GetThreadFromEvent(event.get());
}

bool SBThread::operator==(const SBThread &rhs) const {
  return m_opaque_sp->GetThreadSP().get() ==
         rhs.m_opaque_sp->GetThreadSP().get();
}

bool SBThread::operator!=(const SBThread &rhs) const {
  return m_opaque_sp->GetThreadSP().get() !=
         rhs.m_opaque_sp->GetThreadSP().get();
}

bool SBThread::GetStatus(SBStream &status) const {
  Stream &strm = status.ref();

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    exe_ctx.GetThreadPtr()->GetStatus(strm, 0, 1, 1, true);
  } else
    strm.PutCString("No status");

  return true;
}

bool SBThread::GetDescription(SBStream &description) const {
    return GetDescription(description, false);
}

bool SBThread::GetDescription(SBStream &description, bool stop_format) const {
  Stream &strm = description.ref();

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  if (exe_ctx.HasThreadScope()) {
    exe_ctx.GetThreadPtr()->DumpUsingSettingsFormat(strm,
                                                    LLDB_INVALID_THREAD_ID,
                                                    stop_format);
    // strm.Printf("SBThread: tid = 0x%4.4" PRIx64,
    // exe_ctx.GetThreadPtr()->GetID());
  } else
    strm.PutCString("No value");

  return true;
}

SBThread SBThread::GetExtendedBacktraceThread(const char *type) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);
  SBThread sb_origin_thread;

  if (exe_ctx.HasThreadScope()) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&exe_ctx.GetProcessPtr()->GetRunLock())) {
      ThreadSP real_thread(exe_ctx.GetThreadSP());
      if (real_thread) {
        ConstString type_const(type);
        Process *process = exe_ctx.GetProcessPtr();
        if (process) {
          SystemRuntime *runtime = process->GetSystemRuntime();
          if (runtime) {
            ThreadSP new_thread_sp(
                runtime->GetExtendedBacktraceThread(real_thread, type_const));
            if (new_thread_sp) {
              // Save this in the Process' ExtendedThreadList so a strong
              // pointer retains the object.
              process->GetExtendedThreadList().AddThread(new_thread_sp);
              sb_origin_thread.SetThread(new_thread_sp);
              if (log) {
                const char *queue_name = new_thread_sp->GetQueueName();
                if (queue_name == NULL)
                  queue_name = "";
                log->Printf("SBThread(%p)::GetExtendedBacktraceThread() => new "
                            "extended Thread "
                            "created (%p) with queue_id 0x%" PRIx64
                            " queue name '%s'",
                            static_cast<void *>(exe_ctx.GetThreadPtr()),
                            static_cast<void *>(new_thread_sp.get()),
                            new_thread_sp->GetQueueID(), queue_name);
              }
            }
          }
        }
      }
    } else {
      if (log)
        log->Printf("SBThread(%p)::GetExtendedBacktraceThread() => error: "
                    "process is running",
                    static_cast<void *>(exe_ctx.GetThreadPtr()));
    }
  }

  if (log && !sb_origin_thread.IsValid())
    log->Printf("SBThread(%p)::GetExtendedBacktraceThread() is not returning a "
                "Valid thread",
                static_cast<void *>(exe_ctx.GetThreadPtr()));
  return sb_origin_thread;
}

uint32_t SBThread::GetExtendedBacktraceOriginatingIndexID() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp->GetExtendedBacktraceOriginatingIndexID();
  return LLDB_INVALID_INDEX32;
}

SBValue SBThread::GetCurrentException() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (!thread_sp) return SBValue();

  return SBValue(thread_sp->GetCurrentException());
}

SBThread SBThread::GetCurrentExceptionBacktrace() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (!thread_sp) return SBThread();

  return SBThread(thread_sp->GetCurrentExceptionBacktrace());
}

bool SBThread::SafeToCallFunctions() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp->SafeToCallFunctions();
  return true;
}

lldb_private::Thread *SBThread::operator->() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp.get();
  else
    return NULL;
}

lldb_private::Thread *SBThread::get() {
  ThreadSP thread_sp(m_opaque_sp->GetThreadSP());
  if (thread_sp)
    return thread_sp.get();
  else
    return NULL;
}
