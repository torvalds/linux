//===-- ThreadPlanBase.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanFundamental_h_
#define liblldb_ThreadPlanFundamental_h_

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"

namespace lldb_private {

//------------------------------------------------------------------
//  Base thread plans:
//  This is the generic version of the bottom most plan on the plan stack.  It
//  should
//  be able to handle generic breakpoint hitting, and signals and exceptions.
//------------------------------------------------------------------

class ThreadPlanBase : public ThreadPlan {
  friend class Process; // RunThreadPlan manages "stopper" base plans.
public:
  ~ThreadPlanBase() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override;
  Vote ShouldReportStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  bool MischiefManaged() override;

  bool OkayToDiscard() override { return false; }

  bool IsBasePlan() override { return true; }

protected:
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;
  bool DoPlanExplainsStop(Event *event_ptr) override;
  ThreadPlanBase(Thread &thread);

private:
  friend lldb::ThreadPlanSP
  Thread::QueueFundamentalPlan(bool abort_other_plans);

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanBase);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanFundamental_h_
