//===-- AppleThreadPlanStepThroughObjCTrampoline.h --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_AppleThreadPlanStepThroughObjCTrampoline_h_
#define lldb_AppleThreadPlanStepThroughObjCTrampoline_h_

#include "AppleObjCTrampolineHandler.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class AppleThreadPlanStepThroughObjCTrampoline : public ThreadPlan {
public:
  AppleThreadPlanStepThroughObjCTrampoline(
      Thread &thread, AppleObjCTrampolineHandler *trampoline_handler,
      ValueList &values, lldb::addr_t isa_addr, lldb::addr_t sel_addr,
      bool stop_others);

  ~AppleThreadPlanStepThroughObjCTrampoline() override;

  static bool PreResumeInitializeFunctionCaller(void *myself);

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  lldb::StateType GetPlanRunState() override;

  bool ShouldStop(Event *event_ptr) override;

  bool StopOthers() override { return m_stop_others; }

  // The base class MischiefManaged does some cleanup - so you have to call it
  // in your MischiefManaged derived class.
  bool MischiefManaged() override;

  void DidPush() override;

  bool WillStop() override;

protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

private:
  bool InitializeFunctionCaller();

  AppleObjCTrampolineHandler *m_trampoline_handler; // FIXME - ensure this
                                                    // doesn't go away on us?
                                                    // SP maybe?
  lldb::addr_t m_args_addr; // Stores the address for our step through function
                            // result structure.
  // lldb::addr_t m_object_addr;  // This is only for Description.
  ValueList m_input_values;
  lldb::addr_t m_isa_addr; // isa_addr and sel_addr are the keys we will use to
                           // cache the implementation.
  lldb::addr_t m_sel_addr;
  lldb::ThreadPlanSP m_func_sp; // This is the function call plan.  We fill it
                                // at start, then set it
  // to NULL when this plan is done.  That way we know to go to:
  lldb::ThreadPlanSP m_run_to_sp;  // The plan that runs to the target.
  FunctionCaller *m_impl_function; // This is a pointer to a impl function that
  // is owned by the client that pushes this plan.
  bool m_stop_others;
};

} // namespace lldb_private

#endif // lldb_AppleThreadPlanStepThroughObjCTrampoline_h_
