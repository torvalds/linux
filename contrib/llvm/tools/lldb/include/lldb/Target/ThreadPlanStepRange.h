//===-- ThreadPlanStepRange.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanStepRange_h_
#define liblldb_ThreadPlanStepRange_h_

#include "lldb/Core/AddressRange.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanShouldStopHere.h"

namespace lldb_private {

class ThreadPlanStepRange : public ThreadPlan {
public:
  ThreadPlanStepRange(ThreadPlanKind kind, const char *name, Thread &thread,
                      const AddressRange &range,
                      const SymbolContext &addr_context,
                      lldb::RunMode stop_others,
                      bool given_ranges_only = false);

  ~ThreadPlanStepRange() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override = 0;
  bool ValidatePlan(Stream *error) override;
  bool ShouldStop(Event *event_ptr) override = 0;
  Vote ShouldReportStop(Event *event_ptr) override;
  bool StopOthers() override;
  lldb::StateType GetPlanRunState() override;
  bool WillStop() override;
  bool MischiefManaged() override;
  void DidPush() override;
  bool IsPlanStale() override;

  void AddRange(const AddressRange &new_range);

protected:
  bool InRange();
  lldb::FrameComparison CompareCurrentFrameToStartFrame();
  bool InSymbol();
  void DumpRanges(Stream *s);

  Disassembler *GetDisassembler();

  InstructionList *GetInstructionsForAddress(lldb::addr_t addr,
                                             size_t &range_index,
                                             size_t &insn_offset);

  // Pushes a plan to proceed through the next section of instructions in the
  // range - usually just a RunToAddress plan to run to the next branch.
  // Returns true if it pushed such a plan.  If there was no available 'quick
  // run' plan, then just single step.
  bool SetNextBranchBreakpoint();

  void ClearNextBranchBreakpoint();

  bool NextRangeBreakpointExplainsStop(lldb::StopInfoSP stop_info_sp);

  SymbolContext m_addr_context;
  std::vector<AddressRange> m_address_ranges;
  lldb::RunMode m_stop_others;
  StackID m_stack_id; // Use the stack ID so we can tell step out from step in.
  StackID m_parent_stack_id; // Use the parent stack ID so we can identify tail
                             // calls and the like.
  bool m_no_more_plans;   // Need this one so we can tell if we stepped into a
                          // call,
                          // but can't continue, in which case we are done.
  bool m_first_run_event; // We want to broadcast only one running event, our
                          // first.
  lldb::BreakpointSP m_next_branch_bp_sp;
  bool m_use_fast_step;
  bool m_given_ranges_only;

private:
  std::vector<lldb::DisassemblerSP> m_instruction_ranges;

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanStepRange);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanStepRange_h_
