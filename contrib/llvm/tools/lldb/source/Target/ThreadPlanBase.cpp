//===-- ThreadPlanBase.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanBase.h"

//
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlanBase: This one always stops, and never has anything particular to
// do.
// FIXME: The "signal handling" policies should probably go here.
//----------------------------------------------------------------------

ThreadPlanBase::ThreadPlanBase(Thread &thread)
    : ThreadPlan(ThreadPlan::eKindBase, "base plan", thread, eVoteYes,
                 eVoteNoOpinion) {
// Set the tracer to a default tracer.
// FIXME: need to add a thread settings variable to pix various tracers...
#define THREAD_PLAN_USE_ASSEMBLY_TRACER 1

#ifdef THREAD_PLAN_USE_ASSEMBLY_TRACER
  ThreadPlanTracerSP new_tracer_sp(new ThreadPlanAssemblyTracer(m_thread));
#else
  ThreadPlanTracerSP new_tracer_sp(new ThreadPlanTracer(m_thread));
#endif
  new_tracer_sp->EnableTracing(m_thread.GetTraceEnabledState());
  SetThreadPlanTracer(new_tracer_sp);
  SetIsMasterPlan(true);
}

ThreadPlanBase::~ThreadPlanBase() {}

void ThreadPlanBase::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  s->Printf("Base thread plan.");
}

bool ThreadPlanBase::ValidatePlan(Stream *error) { return true; }

bool ThreadPlanBase::DoPlanExplainsStop(Event *event_ptr) {
  // The base plan should defer to its tracer, since by default it always
  // handles the stop.
  return !TracerExplainsStop();
}

Vote ThreadPlanBase::ShouldReportStop(Event *event_ptr) {
  StopInfoSP stop_info_sp = m_thread.GetStopInfo();
  if (stop_info_sp) {
    bool should_notify = stop_info_sp->ShouldNotify(event_ptr);
    if (should_notify)
      return eVoteYes;
    else
      return eVoteNoOpinion;
  } else
    return eVoteNoOpinion;
}

bool ThreadPlanBase::ShouldStop(Event *event_ptr) {
  m_stop_vote = eVoteYes;
  m_run_vote = eVoteYes;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  StopInfoSP stop_info_sp = GetPrivateStopInfo();
  if (stop_info_sp) {
    StopReason reason = stop_info_sp->GetStopReason();
    switch (reason) {
    case eStopReasonInvalid:
    case eStopReasonNone:
      // This
      m_run_vote = eVoteNoOpinion;
      m_stop_vote = eVoteNo;
      return false;

    case eStopReasonBreakpoint:
    case eStopReasonWatchpoint:
      if (stop_info_sp->ShouldStopSynchronous(event_ptr)) {
        // If we are going to stop for a breakpoint, then unship the other
        // plans at this point.  Don't force the discard, however, so Master
        // plans can stay in place if they want to.
        if (log)
          log->Printf(
              "Base plan discarding thread plans for thread tid = 0x%4.4" PRIx64
              " (breakpoint hit.)",
              m_thread.GetID());
        m_thread.DiscardThreadPlans(false);
        return true;
      }
      // If we aren't going to stop at this breakpoint, and it is internal,
      // don't report this stop or the subsequent running event. Otherwise we
      // will post the stopped & running, but the stopped event will get marked
      // with "restarted" so the UI will know to wait and expect the consequent
      // "running".
      if (stop_info_sp->ShouldNotify(event_ptr)) {
        m_stop_vote = eVoteYes;
        m_run_vote = eVoteYes;
      } else {
        m_stop_vote = eVoteNo;
        m_run_vote = eVoteNo;
      }
      return false;

      // TODO: the break below was missing, was this intentional??? If so
      // please mention it
      break;

    case eStopReasonException:
      // If we crashed, discard thread plans and stop.  Don't force the
      // discard, however, since on rerun the target may clean up this
      // exception and continue normally from there.
      if (log)
        log->Printf(
            "Base plan discarding thread plans for thread tid = 0x%4.4" PRIx64
            " (exception: %s)",
            m_thread.GetID(), stop_info_sp->GetDescription());
      m_thread.DiscardThreadPlans(false);
      return true;

    case eStopReasonExec:
      // If we crashed, discard thread plans and stop.  Don't force the
      // discard, however, since on rerun the target may clean up this
      // exception and continue normally from there.
      if (log)
        log->Printf(
            "Base plan discarding thread plans for thread tid = 0x%4.4" PRIx64
            " (exec.)",
            m_thread.GetID());
      m_thread.DiscardThreadPlans(false);
      return true;

    case eStopReasonThreadExiting:
    case eStopReasonSignal:
      if (stop_info_sp->ShouldStop(event_ptr)) {
        if (log)
          log->Printf(
              "Base plan discarding thread plans for thread tid = 0x%4.4" PRIx64
              " (signal: %s)",
              m_thread.GetID(), stop_info_sp->GetDescription());
        m_thread.DiscardThreadPlans(false);
        return true;
      } else {
        // We're not going to stop, but while we are here, let's figure out
        // whether to report this.
        if (stop_info_sp->ShouldNotify(event_ptr))
          m_stop_vote = eVoteYes;
        else
          m_stop_vote = eVoteNo;
      }
      return false;

    default:
      return true;
    }

  } else {
    m_run_vote = eVoteNoOpinion;
    m_stop_vote = eVoteNo;
  }

  // If there's no explicit reason to stop, then we will continue.
  return false;
}

bool ThreadPlanBase::StopOthers() { return false; }

StateType ThreadPlanBase::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanBase::WillStop() { return true; }

bool ThreadPlanBase::DoWillResume(lldb::StateType resume_state,
                                  bool current_plan) {
  // Reset these to the default values so we don't set them wrong, then not get
  // asked for a while, then return the wrong answer.
  m_run_vote = eVoteNoOpinion;
  m_stop_vote = eVoteNo;
  return true;
}

// The base plan is never done.
bool ThreadPlanBase::MischiefManaged() {
  // The base plan is never done.
  return false;
}
