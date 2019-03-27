//===-- ThreadPlanStepInstruction.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanStepInstruction_h_
#define liblldb_ThreadPlanStepInstruction_h_

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ThreadPlanStepInstruction : public ThreadPlan {
public:
  ThreadPlanStepInstruction(Thread &thread, bool step_over, bool stop_others,
                            Vote stop_vote, Vote run_vote);

  ~ThreadPlanStepInstruction() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  bool MischiefManaged() override;
  bool IsPlanStale() override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  void SetUpState();

private:
  friend lldb::ThreadPlanSP Thread::QueueThreadPlanForStepSingleInstruction(
      bool step_over, bool abort_other_plans, bool stop_other_threads,
      Status &status);

  lldb::addr_t m_instruction_addr;
  bool m_stop_other_threads;
  bool m_step_over;
  // These two are used only for the step over case.
  bool m_start_has_symbol;
  StackID m_stack_id;
  StackID m_parent_frame_id;

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanStepInstruction);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanStepInstruction_h_
