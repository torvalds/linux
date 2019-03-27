//===-- StopInfo.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StopInfo_h_
#define liblldb_StopInfo_h_

#include <string>

#include "lldb/Target/Process.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class StopInfo {
  friend class Process::ProcessEventData;
  friend class ThreadPlanBase;

public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  StopInfo(Thread &thread, uint64_t value);

  virtual ~StopInfo() {}

  bool IsValid() const;

  void SetThread(const lldb::ThreadSP &thread_sp) { m_thread_wp = thread_sp; }

  lldb::ThreadSP GetThread() const { return m_thread_wp.lock(); }

  // The value of the StopInfo depends on the StopReason. StopReason
  // Meaning ----------------------------------------------
  // eStopReasonBreakpoint       BreakpointSiteID eStopReasonSignal
  // Signal number eStopReasonWatchpoint       WatchpointLocationID
  // eStopReasonPlanComplete     No significance

  uint64_t GetValue() const { return m_value; }

  virtual lldb::StopReason GetStopReason() const = 0;

  // ShouldStopSynchronous will get called before any thread plans are
  // consulted, and if it says we should resume the target, then we will just
  // immediately resume.  This should not run any code in or resume the target.

  virtual bool ShouldStopSynchronous(Event *event_ptr) { return true; }

  void OverrideShouldNotify(bool override_value) {
    m_override_should_notify = override_value ? eLazyBoolYes : eLazyBoolNo;
  }

  // If should stop returns false, check if we should notify of this event
  virtual bool ShouldNotify(Event *event_ptr) {
    if (m_override_should_notify == eLazyBoolCalculate)
      return DoShouldNotify(event_ptr);
    else
      return m_override_should_notify == eLazyBoolYes;
  }

  virtual void WillResume(lldb::StateType resume_state) {
    // By default, don't do anything
  }

  virtual const char *GetDescription() { return m_description.c_str(); }

  virtual void SetDescription(const char *desc_cstr) {
    if (desc_cstr && desc_cstr[0])
      m_description.assign(desc_cstr);
    else
      m_description.clear();
  }

  virtual bool IsValidForOperatingSystemThread(Thread &thread) { return true; }

  // Sometimes the thread plan logic will know that it wants a given stop to
  // stop or not, regardless of what the ordinary logic for that StopInfo would
  // dictate.  The main example of this is the ThreadPlanCallFunction, which
  // for instance knows - based on how that particular expression was executed
  // - whether it wants all breakpoints to auto-continue or not. Use
  // OverrideShouldStop on the StopInfo to implement this.

  void OverrideShouldStop(bool override_value) {
    m_override_should_stop = override_value ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetOverrideShouldStop() {
    return m_override_should_stop != eLazyBoolCalculate;
  }

  bool GetOverriddenShouldStopValue() {
    return m_override_should_stop == eLazyBoolYes;
  }

  StructuredData::ObjectSP GetExtendedInfo() { return m_extended_info; }

  static lldb::StopInfoSP
  CreateStopReasonWithBreakpointSiteID(Thread &thread,
                                       lldb::break_id_t break_id);

  // This creates a StopInfo for the thread where the should_stop is already
  // set, and won't be recalculated.
  static lldb::StopInfoSP CreateStopReasonWithBreakpointSiteID(
      Thread &thread, lldb::break_id_t break_id, bool should_stop);

  static lldb::StopInfoSP CreateStopReasonWithWatchpointID(
      Thread &thread, lldb::break_id_t watch_id,
      lldb::addr_t watch_hit_addr = LLDB_INVALID_ADDRESS);

  static lldb::StopInfoSP
  CreateStopReasonWithSignal(Thread &thread, int signo,
                             const char *description = nullptr);

  static lldb::StopInfoSP CreateStopReasonToTrace(Thread &thread);

  static lldb::StopInfoSP
  CreateStopReasonWithPlan(lldb::ThreadPlanSP &plan,
                           lldb::ValueObjectSP return_valobj_sp,
                           lldb::ExpressionVariableSP expression_variable_sp);

  static lldb::StopInfoSP
  CreateStopReasonWithException(Thread &thread, const char *description);

  static lldb::StopInfoSP CreateStopReasonWithExec(Thread &thread);

  static lldb::ValueObjectSP
  GetReturnValueObject(lldb::StopInfoSP &stop_info_sp);

  static lldb::ExpressionVariableSP
  GetExpressionVariable(lldb::StopInfoSP &stop_info_sp);

  static lldb::ValueObjectSP
  GetCrashingDereference(lldb::StopInfoSP &stop_info_sp,
                         lldb::addr_t *crashing_address = nullptr);

protected:
  // Perform any action that is associated with this stop.  This is done as the
  // Event is removed from the event queue.  ProcessEventData::DoOnRemoval does
  // the job.

  virtual void PerformAction(Event *event_ptr) {}

  virtual bool DoShouldNotify(Event *event_ptr) { return false; }

  // Stop the thread by default. Subclasses can override this to allow the
  // thread to continue if desired.  The ShouldStop method should not do
  // anything that might run code.  If you need to run code when deciding
  // whether to stop at this StopInfo, that must be done in the PerformAction.
  // The PerformAction will always get called before the ShouldStop.  This is
  // done by the ProcessEventData::DoOnRemoval, though the ThreadPlanBase needs
  // to consult this later on.
  virtual bool ShouldStop(Event *event_ptr) { return true; }

  //------------------------------------------------------------------
  // Classes that inherit from StackID can see and modify these
  //------------------------------------------------------------------
  lldb::ThreadWP m_thread_wp; // The thread corresponding to the stop reason.
  uint32_t m_stop_id;   // The process stop ID for which this stop info is valid
  uint32_t m_resume_id; // This is the resume ID when we made this stop ID.
  uint64_t m_value; // A generic value that can be used for things pertaining to
                    // this stop info
  std::string m_description; // A textual description describing this stop.
  LazyBool m_override_should_notify;
  LazyBool m_override_should_stop;

  StructuredData::ObjectSP
      m_extended_info; // The extended info for this stop info

  // This determines whether the target has run since this stop info. N.B.
  // running to evaluate a user expression does not count.
  bool HasTargetRunSinceMe();

  // MakeStopInfoValid is necessary to allow saved stop infos to resurrect
  // themselves as valid. It should only be used by
  // Thread::RestoreThreadStateFromCheckpoint and to make sure the one-step
  // needed for before-the-fact watchpoints does not prevent us from stopping
  void MakeStopInfoValid();

private:
  friend class Thread;

  DISALLOW_COPY_AND_ASSIGN(StopInfo);
};

} // namespace lldb_private

#endif // liblldb_StopInfo_h_
