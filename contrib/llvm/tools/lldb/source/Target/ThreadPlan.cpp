//===-- ThreadPlan.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlan.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlan constructor
//----------------------------------------------------------------------
ThreadPlan::ThreadPlan(ThreadPlanKind kind, const char *name, Thread &thread,
                       Vote stop_vote, Vote run_vote)
    : m_thread(thread), m_stop_vote(stop_vote), m_run_vote(run_vote),
      m_takes_iteration_count(false), m_could_not_resolve_hw_bp(false),
      m_kind(kind), m_name(name), m_plan_complete_mutex(),
      m_cached_plan_explains_stop(eLazyBoolCalculate), m_plan_complete(false),
      m_plan_private(false), m_okay_to_discard(true), m_is_master_plan(false),
      m_plan_succeeded(true) {
  SetID(GetNextID());
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ThreadPlan::~ThreadPlan() = default;

bool ThreadPlan::PlanExplainsStop(Event *event_ptr) {
  if (m_cached_plan_explains_stop == eLazyBoolCalculate) {
    bool actual_value = DoPlanExplainsStop(event_ptr);
    m_cached_plan_explains_stop = actual_value ? eLazyBoolYes : eLazyBoolNo;
    return actual_value;
  } else {
    return m_cached_plan_explains_stop == eLazyBoolYes;
  }
}

bool ThreadPlan::IsPlanComplete() {
  std::lock_guard<std::recursive_mutex> guard(m_plan_complete_mutex);
  return m_plan_complete;
}

void ThreadPlan::SetPlanComplete(bool success) {
  std::lock_guard<std::recursive_mutex> guard(m_plan_complete_mutex);
  m_plan_complete = true;
  m_plan_succeeded = success;
}

bool ThreadPlan::MischiefManaged() {
  std::lock_guard<std::recursive_mutex> guard(m_plan_complete_mutex);
  // Mark the plan is complete, but don't override the success flag.
  m_plan_complete = true;
  return true;
}

Vote ThreadPlan::ShouldReportStop(Event *event_ptr) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  if (m_stop_vote == eVoteNoOpinion) {
    ThreadPlan *prev_plan = GetPreviousPlan();
    if (prev_plan) {
      Vote prev_vote = prev_plan->ShouldReportStop(event_ptr);
      LLDB_LOG(log, "returning previous thread plan vote: {0}", prev_vote);
      return prev_vote;
    }
  }
  LLDB_LOG(log, "Returning vote: {0}", m_stop_vote);
  return m_stop_vote;
}

Vote ThreadPlan::ShouldReportRun(Event *event_ptr) {
  if (m_run_vote == eVoteNoOpinion) {
    ThreadPlan *prev_plan = GetPreviousPlan();
    if (prev_plan)
      return prev_plan->ShouldReportRun(event_ptr);
  }
  return m_run_vote;
}

bool ThreadPlan::StopOthers() {
  ThreadPlan *prev_plan;
  prev_plan = GetPreviousPlan();
  return (prev_plan == nullptr) ? false : prev_plan->StopOthers();
}

void ThreadPlan::SetStopOthers(bool new_value) {
  // SetStopOthers doesn't work up the hierarchy.  You have to set the explicit
  // ThreadPlan you want to affect.
}

bool ThreadPlan::WillResume(StateType resume_state, bool current_plan) {
  m_cached_plan_explains_stop = eLazyBoolCalculate;

  if (current_plan) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

    if (log) {
      RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
      assert(reg_ctx);
      addr_t pc = reg_ctx->GetPC();
      addr_t sp = reg_ctx->GetSP();
      addr_t fp = reg_ctx->GetFP();
      log->Printf(
          "%s Thread #%u (0x%p): tid = 0x%4.4" PRIx64 ", pc = 0x%8.8" PRIx64
          ", sp = 0x%8.8" PRIx64 ", fp = 0x%8.8" PRIx64 ", "
          "plan = '%s', state = %s, stop others = %d",
          __FUNCTION__, m_thread.GetIndexID(), static_cast<void *>(&m_thread),
          m_thread.GetID(), static_cast<uint64_t>(pc),
          static_cast<uint64_t>(sp), static_cast<uint64_t>(fp), m_name.c_str(),
          StateAsCString(resume_state), StopOthers());
    }
  }
  return DoWillResume(resume_state, current_plan);
}

lldb::user_id_t ThreadPlan::GetNextID() {
  static uint32_t g_nextPlanID = 0;
  return ++g_nextPlanID;
}

void ThreadPlan::DidPush() {}

void ThreadPlan::WillPop() {}

bool ThreadPlan::OkayToDiscard() {
  return IsMasterPlan() ? m_okay_to_discard : true;
}

lldb::StateType ThreadPlan::RunState() {
  if (m_tracer_sp && m_tracer_sp->TracingEnabled() &&
      m_tracer_sp->SingleStepEnabled())
    return eStateStepping;
  else
    return GetPlanRunState();
}

bool ThreadPlan::IsUsuallyUnexplainedStopReason(lldb::StopReason reason) {
  switch (reason) {
  case eStopReasonWatchpoint:
  case eStopReasonSignal:
  case eStopReasonException:
  case eStopReasonExec:
  case eStopReasonThreadExiting:
  case eStopReasonInstrumentation:
    return true;
  default:
    return false;
  }
}

//----------------------------------------------------------------------
// ThreadPlanNull
//----------------------------------------------------------------------

ThreadPlanNull::ThreadPlanNull(Thread &thread)
    : ThreadPlan(ThreadPlan::eKindNull, "Null Thread Plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion) {}

ThreadPlanNull::~ThreadPlanNull() = default;

void ThreadPlanNull::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  s->PutCString("Null thread plan - thread has been destroyed.");
}

bool ThreadPlanNull::ValidatePlan(Stream *error) {
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return true;
}

bool ThreadPlanNull::ShouldStop(Event *event_ptr) {
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return true;
}

bool ThreadPlanNull::WillStop() {
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return true;
}

bool ThreadPlanNull::DoPlanExplainsStop(Event *event_ptr) {
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return true;
}

// The null plan is never done.
bool ThreadPlanNull::MischiefManaged() {
// The null plan is never done.
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return false;
}

lldb::StateType ThreadPlanNull::GetPlanRunState() {
// Not sure what to return here.  This is a dead thread.
#ifdef LLDB_CONFIGURATION_DEBUG
  fprintf(stderr,
          "error: %s called on thread that has been destroyed (tid = 0x%" PRIx64
          ", ptid = 0x%" PRIx64 ")",
          LLVM_PRETTY_FUNCTION, m_thread.GetID(), m_thread.GetProtocolID());
#else
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Error("%s called on thread that has been destroyed (tid = 0x%" PRIx64
               ", ptid = 0x%" PRIx64 ")",
               LLVM_PRETTY_FUNCTION, m_thread.GetID(),
               m_thread.GetProtocolID());
#endif
  return eStateRunning;
}
