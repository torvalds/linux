//===-- ThreadPlanStepUntil.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanStepUntil_h_
#define liblldb_ThreadPlanStepUntil_h_

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"

namespace lldb_private {

class ThreadPlanStepUntil : public ThreadPlan {
public:
  ~ThreadPlanStepUntil() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  bool MischiefManaged() override;

protected:
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;
  bool DoPlanExplainsStop(Event *event_ptr) override;

  ThreadPlanStepUntil(Thread &thread, lldb::addr_t *address_list,
                      size_t num_addresses, bool stop_others,
                      uint32_t frame_idx = 0);

  void AnalyzeStop();

private:
  StackID m_stack_id;
  lldb::addr_t m_step_from_insn;
  lldb::break_id_t m_return_bp_id;
  lldb::addr_t m_return_addr;
  bool m_stepped_out;
  bool m_should_stop;
  bool m_ran_analyze;
  bool m_explains_stop;

  typedef std::map<lldb::addr_t, lldb::break_id_t> until_collection;
  until_collection m_until_points;
  bool m_stop_others;

  void Clear();

  friend lldb::ThreadPlanSP Thread::QueueThreadPlanForStepUntil(
      bool abort_other_plans, lldb::addr_t *address_list, size_t num_addresses,
      bool stop_others, uint32_t frame_idx, Status &status);

  // Need an appropriate marker for the current stack so we can tell step out
  // from step in.

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanStepUntil);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanStepUntil_h_
