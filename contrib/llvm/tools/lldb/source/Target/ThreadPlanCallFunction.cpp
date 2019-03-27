//===-- ThreadPlanCallFunction.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlanCallFunction: Plan to call a single function
//----------------------------------------------------------------------
bool ThreadPlanCallFunction::ConstructorSetup(
    Thread &thread, ABI *&abi, lldb::addr_t &start_load_addr,
    lldb::addr_t &function_load_addr) {
  SetIsMasterPlan(true);
  SetOkayToDiscard(false);
  SetPrivate(true);

  ProcessSP process_sp(thread.GetProcess());
  if (!process_sp)
    return false;

  abi = process_sp->GetABI().get();

  if (!abi)
    return false;

  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_STEP));

  SetBreakpoints();

  m_function_sp = thread.GetRegisterContext()->GetSP() - abi->GetRedZoneSize();
  // If we can't read memory at the point of the process where we are planning
  // to put our function, we're not going to get any further...
  Status error;
  process_sp->ReadUnsignedIntegerFromMemory(m_function_sp, 4, 0, error);
  if (!error.Success()) {
    m_constructor_errors.Printf(
        "Trying to put the stack in unreadable memory at: 0x%" PRIx64 ".",
        m_function_sp);
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): %s.", static_cast<void *>(this),
                  m_constructor_errors.GetData());
    return false;
  }

  Module *exe_module = GetTarget().GetExecutableModulePointer();

  if (exe_module == nullptr) {
    m_constructor_errors.Printf(
        "Can't execute code without an executable module.");
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): %s.", static_cast<void *>(this),
                  m_constructor_errors.GetData());
    return false;
  } else {
    ObjectFile *objectFile = exe_module->GetObjectFile();
    if (!objectFile) {
      m_constructor_errors.Printf(
          "Could not find object file for module \"%s\".",
          exe_module->GetFileSpec().GetFilename().AsCString());

      if (log)
        log->Printf("ThreadPlanCallFunction(%p): %s.",
                    static_cast<void *>(this), m_constructor_errors.GetData());
      return false;
    }

    m_start_addr = objectFile->GetEntryPointAddress();
    if (!m_start_addr.IsValid()) {
      m_constructor_errors.Printf(
          "Could not find entry point address for executable module \"%s\".",
          exe_module->GetFileSpec().GetFilename().AsCString());
      if (log)
        log->Printf("ThreadPlanCallFunction(%p): %s.",
                    static_cast<void *>(this), m_constructor_errors.GetData());
      return false;
    }
  }

  start_load_addr = m_start_addr.GetLoadAddress(&GetTarget());

  // Checkpoint the thread state so we can restore it later.
  if (log && log->GetVerbose())
    ReportRegisterState("About to checkpoint thread before function call.  "
                        "Original register state was:");

  if (!thread.CheckpointThreadState(m_stored_thread_state)) {
    m_constructor_errors.Printf("Setting up ThreadPlanCallFunction, failed to "
                                "checkpoint thread state.");
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): %s.", static_cast<void *>(this),
                  m_constructor_errors.GetData());
    return false;
  }
  function_load_addr = m_function_addr.GetLoadAddress(&GetTarget());

  return true;
}

ThreadPlanCallFunction::ThreadPlanCallFunction(
    Thread &thread, const Address &function, const CompilerType &return_type,
    llvm::ArrayRef<addr_t> args, const EvaluateExpressionOptions &options)
    : ThreadPlan(ThreadPlan::eKindCallFunction, "Call function plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_valid(false), m_stop_other_threads(options.GetStopOthers()),
      m_unwind_on_error(options.DoesUnwindOnError()),
      m_ignore_breakpoints(options.DoesIgnoreBreakpoints()),
      m_debug_execution(options.GetDebug()),
      m_trap_exceptions(options.GetTrapExceptions()), m_function_addr(function),
      m_function_sp(0), m_takedown_done(false),
      m_should_clear_objc_exception_bp(false),
      m_should_clear_cxx_exception_bp(false),
      m_stop_address(LLDB_INVALID_ADDRESS), m_return_type(return_type) {
  lldb::addr_t start_load_addr = LLDB_INVALID_ADDRESS;
  lldb::addr_t function_load_addr = LLDB_INVALID_ADDRESS;
  ABI *abi = nullptr;

  if (!ConstructorSetup(thread, abi, start_load_addr, function_load_addr))
    return;

  if (!abi->PrepareTrivialCall(thread, m_function_sp, function_load_addr,
                               start_load_addr, args))
    return;

  ReportRegisterState("Function call was set up.  Register state was:");

  m_valid = true;
}

ThreadPlanCallFunction::ThreadPlanCallFunction(
    Thread &thread, const Address &function,
    const EvaluateExpressionOptions &options)
    : ThreadPlan(ThreadPlan::eKindCallFunction, "Call function plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_valid(false), m_stop_other_threads(options.GetStopOthers()),
      m_unwind_on_error(options.DoesUnwindOnError()),
      m_ignore_breakpoints(options.DoesIgnoreBreakpoints()),
      m_debug_execution(options.GetDebug()),
      m_trap_exceptions(options.GetTrapExceptions()), m_function_addr(function),
      m_function_sp(0), m_takedown_done(false),
      m_should_clear_objc_exception_bp(false),
      m_should_clear_cxx_exception_bp(false),
      m_stop_address(LLDB_INVALID_ADDRESS), m_return_type(CompilerType()) {}

ThreadPlanCallFunction::~ThreadPlanCallFunction() {
  DoTakedown(PlanSucceeded());
}

void ThreadPlanCallFunction::ReportRegisterState(const char *message) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log && log->GetVerbose()) {
    StreamString strm;
    RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();

    log->PutCString(message);

    RegisterValue reg_value;

    for (uint32_t reg_idx = 0, num_registers = reg_ctx->GetRegisterCount();
         reg_idx < num_registers; ++reg_idx) {
      const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoAtIndex(reg_idx);
      if (reg_ctx->ReadRegister(reg_info, reg_value)) {
        DumpRegisterValue(reg_value, &strm, reg_info, true, false,
                          eFormatDefault);
        strm.EOL();
      }
    }
    log->PutString(strm.GetString());
  }
}

void ThreadPlanCallFunction::DoTakedown(bool success) {
  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_STEP));

  if (!m_valid) {
    // Don't call DoTakedown if we were never valid to begin with.
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): Log called on "
                  "ThreadPlanCallFunction that was never valid.",
                  static_cast<void *>(this));
    return;
  }

  if (!m_takedown_done) {
    if (success) {
      SetReturnValue();
    }
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): DoTakedown called for thread "
                  "0x%4.4" PRIx64 ", m_valid: %d complete: %d.\n",
                  static_cast<void *>(this), m_thread.GetID(), m_valid,
                  IsPlanComplete());
    m_takedown_done = true;
    m_stop_address =
        m_thread.GetStackFrameAtIndex(0)->GetRegisterContext()->GetPC();
    m_real_stop_info_sp = GetPrivateStopInfo();
    if (!m_thread.RestoreRegisterStateFromCheckpoint(m_stored_thread_state)) {
      if (log)
        log->Printf("ThreadPlanCallFunction(%p): DoTakedown failed to restore "
                    "register state",
                    static_cast<void *>(this));
    }
    SetPlanComplete(success);
    ClearBreakpoints();
    if (log && log->GetVerbose())
      ReportRegisterState("Restoring thread state after function call.  "
                          "Restored register state:");
  } else {
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): DoTakedown called as no-op for "
                  "thread 0x%4.4" PRIx64 ", m_valid: %d complete: %d.\n",
                  static_cast<void *>(this), m_thread.GetID(), m_valid,
                  IsPlanComplete());
  }
}

void ThreadPlanCallFunction::WillPop() { DoTakedown(PlanSucceeded()); }

void ThreadPlanCallFunction::GetDescription(Stream *s, DescriptionLevel level) {
  if (level == eDescriptionLevelBrief) {
    s->Printf("Function call thread plan");
  } else {
    TargetSP target_sp(m_thread.CalculateTarget());
    s->Printf("Thread plan to call 0x%" PRIx64,
              m_function_addr.GetLoadAddress(target_sp.get()));
  }
}

bool ThreadPlanCallFunction::ValidatePlan(Stream *error) {
  if (!m_valid) {
    if (error) {
      if (m_constructor_errors.GetSize() > 0)
        error->PutCString(m_constructor_errors.GetString());
      else
        error->PutCString("Unknown error");
    }
    return false;
  }

  return true;
}

Vote ThreadPlanCallFunction::ShouldReportStop(Event *event_ptr) {
  if (m_takedown_done || IsPlanComplete())
    return eVoteYes;
  else
    return ThreadPlan::ShouldReportStop(event_ptr);
}

bool ThreadPlanCallFunction::DoPlanExplainsStop(Event *event_ptr) {
  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_STEP |
                                                  LIBLLDB_LOG_PROCESS));
  m_real_stop_info_sp = GetPrivateStopInfo();

  // If our subplan knows why we stopped, even if it's done (which would
  // forward the question to us) we answer yes.
  if (m_subplan_sp && m_subplan_sp->PlanExplainsStop(event_ptr)) {
    SetPlanComplete();
    return true;
  }

  // Check if the breakpoint is one of ours.

  StopReason stop_reason;
  if (!m_real_stop_info_sp)
    stop_reason = eStopReasonNone;
  else
    stop_reason = m_real_stop_info_sp->GetStopReason();
  if (log)
    log->Printf(
        "ThreadPlanCallFunction::PlanExplainsStop: Got stop reason - %s.",
        Thread::StopReasonAsCString(stop_reason));

  if (stop_reason == eStopReasonBreakpoint && BreakpointsExplainStop())
    return true;

  // One more quirk here.  If this event was from Halt interrupting the target,
  // then we should not consider ourselves complete.  Return true to
  // acknowledge the stop.
  if (Process::ProcessEventData::GetInterruptedFromEvent(event_ptr)) {
    if (log)
      log->Printf("ThreadPlanCallFunction::PlanExplainsStop: The event is an "
                  "Interrupt, returning true.");
    return true;
  }
  // We control breakpoints separately from other "stop reasons."  So first,
  // check the case where we stopped for an internal breakpoint, in that case,
  // continue on. If it is not an internal breakpoint, consult
  // m_ignore_breakpoints.

  if (stop_reason == eStopReasonBreakpoint) {
    ProcessSP process_sp(m_thread.CalculateProcess());
    uint64_t break_site_id = m_real_stop_info_sp->GetValue();
    BreakpointSiteSP bp_site_sp;
    if (process_sp)
      bp_site_sp = process_sp->GetBreakpointSiteList().FindByID(break_site_id);
    if (bp_site_sp) {
      uint32_t num_owners = bp_site_sp->GetNumberOfOwners();
      bool is_internal = true;
      for (uint32_t i = 0; i < num_owners; i++) {
        Breakpoint &bp = bp_site_sp->GetOwnerAtIndex(i)->GetBreakpoint();
        if (log)
          log->Printf("ThreadPlanCallFunction::PlanExplainsStop: hit "
                      "breakpoint %d while calling function",
                      bp.GetID());

        if (!bp.IsInternal()) {
          is_internal = false;
          break;
        }
      }
      if (is_internal) {
        if (log)
          log->Printf("ThreadPlanCallFunction::PlanExplainsStop hit an "
                      "internal breakpoint, not stopping.");
        return false;
      }
    }

    if (m_ignore_breakpoints) {
      if (log)
        log->Printf("ThreadPlanCallFunction::PlanExplainsStop: we are ignoring "
                    "breakpoints, overriding breakpoint stop info ShouldStop, "
                    "returning true");
      m_real_stop_info_sp->OverrideShouldStop(false);
      return true;
    } else {
      if (log)
        log->Printf("ThreadPlanCallFunction::PlanExplainsStop: we are not "
                    "ignoring breakpoints, overriding breakpoint stop info "
                    "ShouldStop, returning true");
      m_real_stop_info_sp->OverrideShouldStop(true);
      return false;
    }
  } else if (!m_unwind_on_error) {
    // If we don't want to discard this plan, than any stop we don't understand
    // should be propagated up the stack.
    return false;
  } else {
    // If the subplan is running, any crashes are attributable to us. If we
    // want to discard the plan, then we say we explain the stop but if we are
    // going to be discarded, let whoever is above us explain the stop. But
    // don't discard the plan if the stop would restart itself (for instance if
    // it is a signal that is set not to stop.  Check that here first.  We just
    // say we explain the stop but aren't done and everything will continue on
    // from there.

    if (m_real_stop_info_sp &&
        m_real_stop_info_sp->ShouldStopSynchronous(event_ptr)) {
      SetPlanComplete(false);
      return m_subplan_sp ? m_unwind_on_error : false;
    } else
      return true;
  }
}

bool ThreadPlanCallFunction::ShouldStop(Event *event_ptr) {
  // We do some computation in DoPlanExplainsStop that may or may not set the
  // plan as complete. We need to do that here to make sure our state is
  // correct.
  DoPlanExplainsStop(event_ptr);

  if (IsPlanComplete()) {
    ReportRegisterState("Function completed.  Register state was:");
    return true;
  } else {
    return false;
  }
}

bool ThreadPlanCallFunction::StopOthers() { return m_stop_other_threads; }

StateType ThreadPlanCallFunction::GetPlanRunState() { return eStateRunning; }

void ThreadPlanCallFunction::DidPush() {
  //#define SINGLE_STEP_EXPRESSIONS

  // Now set the thread state to "no reason" so we don't run with whatever
  // signal was outstanding... Wait till the plan is pushed so we aren't
  // changing the stop info till we're about to run.

  GetThread().SetStopInfoToNothing();

#ifndef SINGLE_STEP_EXPRESSIONS
  m_subplan_sp.reset(
      new ThreadPlanRunToAddress(m_thread, m_start_addr, m_stop_other_threads));

  m_thread.QueueThreadPlan(m_subplan_sp, false);
  m_subplan_sp->SetPrivate(true);
#endif
}

bool ThreadPlanCallFunction::WillStop() { return true; }

bool ThreadPlanCallFunction::MischiefManaged() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  if (IsPlanComplete()) {
    if (log)
      log->Printf("ThreadPlanCallFunction(%p): Completed call function plan.",
                  static_cast<void *>(this));

    ThreadPlan::MischiefManaged();
    return true;
  } else {
    return false;
  }
}

void ThreadPlanCallFunction::SetBreakpoints() {
  ProcessSP process_sp(m_thread.CalculateProcess());
  if (m_trap_exceptions && process_sp) {
    m_cxx_language_runtime =
        process_sp->GetLanguageRuntime(eLanguageTypeC_plus_plus);
    m_objc_language_runtime = process_sp->GetLanguageRuntime(eLanguageTypeObjC);

    if (m_cxx_language_runtime) {
      m_should_clear_cxx_exception_bp =
          !m_cxx_language_runtime->ExceptionBreakpointsAreSet();
      m_cxx_language_runtime->SetExceptionBreakpoints();
    }
    if (m_objc_language_runtime) {
      m_should_clear_objc_exception_bp =
          !m_objc_language_runtime->ExceptionBreakpointsAreSet();
      m_objc_language_runtime->SetExceptionBreakpoints();
    }
  }
}

void ThreadPlanCallFunction::ClearBreakpoints() {
  if (m_trap_exceptions) {
    if (m_cxx_language_runtime && m_should_clear_cxx_exception_bp)
      m_cxx_language_runtime->ClearExceptionBreakpoints();
    if (m_objc_language_runtime && m_should_clear_objc_exception_bp)
      m_objc_language_runtime->ClearExceptionBreakpoints();
  }
}

bool ThreadPlanCallFunction::BreakpointsExplainStop() {
  StopInfoSP stop_info_sp = GetPrivateStopInfo();

  if (m_trap_exceptions) {
    if ((m_cxx_language_runtime &&
         m_cxx_language_runtime->ExceptionBreakpointsExplainStop(
             stop_info_sp)) ||
        (m_objc_language_runtime &&
         m_objc_language_runtime->ExceptionBreakpointsExplainStop(
             stop_info_sp))) {
      Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_STEP));
      if (log)
        log->Printf("ThreadPlanCallFunction::BreakpointsExplainStop - Hit an "
                    "exception breakpoint, setting plan complete.");

      SetPlanComplete(false);

      // If the user has set the ObjC language breakpoint, it would normally
      // get priority over our internal catcher breakpoint, but in this case we
      // can't let that happen, so force the ShouldStop here.
      stop_info_sp->OverrideShouldStop(true);
      return true;
    }
  }

  return false;
}

void ThreadPlanCallFunction::SetStopOthers(bool new_value) {
  m_subplan_sp->SetStopOthers(new_value);
}

bool ThreadPlanCallFunction::RestoreThreadState() {
  return GetThread().RestoreThreadStateFromCheckpoint(m_stored_thread_state);
}

void ThreadPlanCallFunction::SetReturnValue() {
  ProcessSP process_sp(m_thread.GetProcess());
  const ABI *abi = process_sp ? process_sp->GetABI().get() : nullptr;
  if (abi && m_return_type.IsValid()) {
    const bool persistent = false;
    m_return_valobj_sp =
        abi->GetReturnValueObject(m_thread, m_return_type, persistent);
  }
}
