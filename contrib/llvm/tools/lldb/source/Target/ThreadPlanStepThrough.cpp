//===-- ThreadPlanStepThrough.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanStepThrough.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Target/CPPLanguageRuntime.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlanStepThrough: If the current instruction is a trampoline, step
// through it If it is the beginning of the prologue of a function, step
// through that as well.
// FIXME: At present only handles DYLD trampolines.
//----------------------------------------------------------------------

ThreadPlanStepThrough::ThreadPlanStepThrough(Thread &thread,
                                             StackID &m_stack_id,
                                             bool stop_others)
    : ThreadPlan(ThreadPlan::eKindStepThrough,
                 "Step through trampolines and prologues", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_start_address(0), m_backstop_bkpt_id(LLDB_INVALID_BREAK_ID),
      m_backstop_addr(LLDB_INVALID_ADDRESS), m_return_stack_id(m_stack_id),
      m_stop_others(stop_others) {
  LookForPlanToStepThroughFromCurrentPC();

  // If we don't get a valid step through plan, don't bother to set up a
  // backstop.
  if (m_sub_plan_sp) {
    m_start_address = GetThread().GetRegisterContext()->GetPC(0);

    // We are going to return back to the concrete frame 1, we might pass by
    // some inlined code that we're in the middle of by doing this, but it's
    // easier than trying to figure out where the inlined code might return to.

    StackFrameSP return_frame_sp = m_thread.GetFrameWithStackID(m_stack_id);

    if (return_frame_sp) {
      m_backstop_addr = return_frame_sp->GetFrameCodeAddress().GetLoadAddress(
          m_thread.CalculateTarget().get());
      Breakpoint *return_bp =
          m_thread.GetProcess()
              ->GetTarget()
              .CreateBreakpoint(m_backstop_addr, true, false)
              .get();

      if (return_bp != nullptr) {
        if (return_bp->IsHardware() && !return_bp->HasResolvedLocations())
          m_could_not_resolve_hw_bp = true;
        return_bp->SetThreadID(m_thread.GetID());
        m_backstop_bkpt_id = return_bp->GetID();
        return_bp->SetBreakpointKind("step-through-backstop");
      }
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
      if (log) {
        log->Printf("Setting backstop breakpoint %d at address: 0x%" PRIx64,
                    m_backstop_bkpt_id, m_backstop_addr);
      }
    }
  }
}

ThreadPlanStepThrough::~ThreadPlanStepThrough() { ClearBackstopBreakpoint(); }

void ThreadPlanStepThrough::DidPush() {
  if (m_sub_plan_sp)
    PushPlan(m_sub_plan_sp);
}

void ThreadPlanStepThrough::LookForPlanToStepThroughFromCurrentPC() {
  DynamicLoader *loader = m_thread.GetProcess()->GetDynamicLoader();
  if (loader)
    m_sub_plan_sp =
        loader->GetStepThroughTrampolinePlan(m_thread, m_stop_others);

  // If that didn't come up with anything, try the ObjC runtime plugin:
  if (!m_sub_plan_sp.get()) {
    ObjCLanguageRuntime *objc_runtime =
        m_thread.GetProcess()->GetObjCLanguageRuntime();
    if (objc_runtime)
      m_sub_plan_sp =
          objc_runtime->GetStepThroughTrampolinePlan(m_thread, m_stop_others);

    CPPLanguageRuntime *cpp_runtime =
        m_thread.GetProcess()->GetCPPLanguageRuntime();

    // If the ObjC runtime did not provide us with a step though plan then if we
    // have it check the C++ runtime for a step though plan.
    if (!m_sub_plan_sp.get() && cpp_runtime)
      m_sub_plan_sp =
          cpp_runtime->GetStepThroughTrampolinePlan(m_thread, m_stop_others);
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log) {
    lldb::addr_t current_address = GetThread().GetRegisterContext()->GetPC(0);
    if (m_sub_plan_sp) {
      StreamString s;
      m_sub_plan_sp->GetDescription(&s, lldb::eDescriptionLevelFull);
      log->Printf("Found step through plan from 0x%" PRIx64 ": %s",
                  current_address, s.GetData());
    } else {
      log->Printf("Couldn't find step through plan from address 0x%" PRIx64 ".",
                  current_address);
    }
  }
}

void ThreadPlanStepThrough::GetDescription(Stream *s,
                                           lldb::DescriptionLevel level) {
  if (level == lldb::eDescriptionLevelBrief)
    s->Printf("Step through");
  else {
    s->PutCString("Stepping through trampoline code from: ");
    s->Address(m_start_address, sizeof(addr_t));
    if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
      s->Printf(" with backstop breakpoint ID: %d at address: ",
                m_backstop_bkpt_id);
      s->Address(m_backstop_addr, sizeof(addr_t));
    } else
      s->PutCString(" unable to set a backstop breakpoint.");
  }
}

bool ThreadPlanStepThrough::ValidatePlan(Stream *error) {
  if (m_could_not_resolve_hw_bp) {
    if (error)
      error->PutCString(
          "Could not create hardware breakpoint for thread plan.");
    return false;
  }

  if (m_backstop_bkpt_id == LLDB_INVALID_BREAK_ID) {
    if (error)
      error->PutCString("Could not create backstop breakpoint.");
    return false;
  }

  if (!m_sub_plan_sp.get()) {
    if (error)
      error->PutCString("Does not have a subplan.");
    return false;
  }

  return true;
}

bool ThreadPlanStepThrough::DoPlanExplainsStop(Event *event_ptr) {
  // If we have a sub-plan, it will have been asked first if we explain the
  // stop, and we won't get asked.  The only time we would be the one directly
  // asked this question is if we hit our backstop breakpoint.

  return HitOurBackstopBreakpoint();
}

bool ThreadPlanStepThrough::ShouldStop(Event *event_ptr) {
  // If we've already marked ourselves done, then we're done...
  if (IsPlanComplete())
    return true;

  // First, did we hit the backstop breakpoint?
  if (HitOurBackstopBreakpoint()) {
    SetPlanComplete(true);
    return true;
  }

  // If we don't have a sub-plan, then we're also done (can't see how we would
  // ever get here without a plan, but just in case.

  if (!m_sub_plan_sp) {
    SetPlanComplete();
    return true;
  }

  // If the current sub plan is not done, we don't want to stop.  Actually, we
  // probably won't ever get here in this state, since we generally won't get
  // asked any questions if out current sub-plan is not done...
  if (!m_sub_plan_sp->IsPlanComplete())
    return false;

  // If our current sub plan failed, then let's just run to our backstop.  If
  // we can't do that then just stop.
  if (!m_sub_plan_sp->PlanSucceeded()) {
    if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
      m_sub_plan_sp.reset();
      return false;
    } else {
      SetPlanComplete(false);
      return true;
    }
  }

  // Next see if there is a specific step through plan at our current pc (these
  // might chain, for instance stepping through a dylib trampoline to the objc
  // dispatch function...)
  LookForPlanToStepThroughFromCurrentPC();
  if (m_sub_plan_sp) {
    PushPlan(m_sub_plan_sp);
    return false;
  } else {
    SetPlanComplete();
    return true;
  }
}

bool ThreadPlanStepThrough::StopOthers() { return m_stop_others; }

StateType ThreadPlanStepThrough::GetPlanRunState() { return eStateRunning; }

bool ThreadPlanStepThrough::DoWillResume(StateType resume_state,
                                         bool current_plan) {
  return true;
}

bool ThreadPlanStepThrough::WillStop() { return true; }

void ThreadPlanStepThrough::ClearBackstopBreakpoint() {
  if (m_backstop_bkpt_id != LLDB_INVALID_BREAK_ID) {
    m_thread.GetProcess()->GetTarget().RemoveBreakpointByID(m_backstop_bkpt_id);
    m_backstop_bkpt_id = LLDB_INVALID_BREAK_ID;
    m_could_not_resolve_hw_bp = false;
  }
}

bool ThreadPlanStepThrough::MischiefManaged() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  if (!IsPlanComplete()) {
    return false;
  } else {
    if (log)
      log->Printf("Completed step through step plan.");

    ClearBackstopBreakpoint();
    ThreadPlan::MischiefManaged();
    return true;
  }
}

bool ThreadPlanStepThrough::HitOurBackstopBreakpoint() {
  StopInfoSP stop_info_sp(m_thread.GetStopInfo());
  if (stop_info_sp && stop_info_sp->GetStopReason() == eStopReasonBreakpoint) {
    break_id_t stop_value = (break_id_t)stop_info_sp->GetValue();
    BreakpointSiteSP cur_site_sp =
        m_thread.GetProcess()->GetBreakpointSiteList().FindByID(stop_value);
    if (cur_site_sp &&
        cur_site_sp->IsBreakpointAtThisSite(m_backstop_bkpt_id)) {
      StackID cur_frame_zero_id =
          m_thread.GetStackFrameAtIndex(0)->GetStackID();

      if (cur_frame_zero_id == m_return_stack_id) {
        Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
        if (log)
          log->PutCString("ThreadPlanStepThrough hit backstop breakpoint.");
        return true;
      }
    }
  }
  return false;
}
