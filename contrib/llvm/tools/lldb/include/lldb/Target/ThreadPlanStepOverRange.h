//===-- ThreadPlanStepOverRange.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanStepOverRange_h_
#define liblldb_ThreadPlanStepOverRange_h_

#include "lldb/Core/AddressRange.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanStepRange.h"

namespace lldb_private {

class ThreadPlanStepOverRange : public ThreadPlanStepRange,
                                ThreadPlanShouldStopHere {
public:
  ThreadPlanStepOverRange(Thread &thread, const AddressRange &range,
                          const SymbolContext &addr_context,
                          lldb::RunMode stop_others,
                          LazyBool step_out_avoids_no_debug);

  ~ThreadPlanStepOverRange() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;
  bool ShouldStop(Event *event_ptr) override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;

  void SetFlagsToDefault() override {
    GetFlags().Set(ThreadPlanStepOverRange::s_default_flag_values);
  }

private:
  static uint32_t s_default_flag_values;

  void SetupAvoidNoDebug(LazyBool step_out_avoids_code_without_debug_info);
  bool IsEquivalentContext(const SymbolContext &context);

  bool m_first_resume;

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanStepOverRange);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanStepOverRange_h_
