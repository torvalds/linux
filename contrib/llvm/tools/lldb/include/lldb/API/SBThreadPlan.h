//===-- SBThread.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBThreadPlan_h_
#define LLDB_SBThreadPlan_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>

namespace lldb {

class LLDB_API SBThreadPlan {

  friend class lldb_private::ThreadPlan;

public:
  SBThreadPlan();

  SBThreadPlan(const lldb::SBThreadPlan &threadPlan);

  SBThreadPlan(const lldb::ThreadPlanSP &lldb_object_sp);

  SBThreadPlan(lldb::SBThread &thread, const char *class_name);

  ~SBThreadPlan();

  bool IsValid() const;

  void Clear();

  lldb::StopReason GetStopReason();

  /// Get the number of words associated with the stop reason.
  /// See also GetStopReasonDataAtIndex().
  size_t GetStopReasonDataCount();

  //--------------------------------------------------------------------------
  /// Get information associated with a stop reason.
  ///
  /// Breakpoint stop reasons will have data that consists of pairs of
  /// breakpoint IDs followed by the breakpoint location IDs (they always come
  /// in pairs).
  ///
  /// Stop Reason              Count Data Type
  /// ======================== ===== =========================================
  /// eStopReasonNone          0
  /// eStopReasonTrace         0
  /// eStopReasonBreakpoint    N     duple: {breakpoint id, location id}
  /// eStopReasonWatchpoint    1     watchpoint id
  /// eStopReasonSignal        1     unix signal number
  /// eStopReasonException     N     exception data
  /// eStopReasonExec          0
  /// eStopReasonPlanComplete  0
  //--------------------------------------------------------------------------
  uint64_t GetStopReasonDataAtIndex(uint32_t idx);

  SBThread GetThread() const;

  const lldb::SBThreadPlan &operator=(const lldb::SBThreadPlan &rhs);

  bool GetDescription(lldb::SBStream &description) const;

  void SetPlanComplete(bool success);

  bool IsPlanComplete();

  bool IsPlanStale();

  bool IsValid();

  // This section allows an SBThreadPlan to push another of the common types of
  // plans...
  SBThreadPlan QueueThreadPlanForStepOverRange(SBAddress &start_address,
                                               lldb::addr_t range_size);
  SBThreadPlan QueueThreadPlanForStepOverRange(SBAddress &start_address,
                                               lldb::addr_t range_size,
                                               SBError &error);

  SBThreadPlan QueueThreadPlanForStepInRange(SBAddress &start_address,
                                             lldb::addr_t range_size);
  SBThreadPlan QueueThreadPlanForStepInRange(SBAddress &start_address,
                                             lldb::addr_t range_size,
                                             SBError &error);

  SBThreadPlan QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                         bool first_insn = false);
  SBThreadPlan QueueThreadPlanForStepOut(uint32_t frame_idx_to_step_to,
                                         bool first_insn, SBError &error);

  SBThreadPlan QueueThreadPlanForRunToAddress(SBAddress address);
  SBThreadPlan QueueThreadPlanForRunToAddress(SBAddress address,
                                              SBError &error);

  SBThreadPlan QueueThreadPlanForStepScripted(const char *script_class_name);
  SBThreadPlan QueueThreadPlanForStepScripted(const char *script_class_name,
                                              SBError &error);

#ifndef SWIG
  lldb_private::ThreadPlan *get();
#endif

protected:
  friend class SBBreakpoint;
  friend class SBBreakpointLocation;
  friend class SBFrame;
  friend class SBProcess;
  friend class SBDebugger;
  friend class SBValue;
  friend class lldb_private::QueueImpl;
  friend class SBQueueItem;

#ifndef SWIG
  void SetThreadPlan(const lldb::ThreadPlanSP &lldb_object_sp);
#endif

private:
  lldb::ThreadPlanSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBThreadPlan_h_
