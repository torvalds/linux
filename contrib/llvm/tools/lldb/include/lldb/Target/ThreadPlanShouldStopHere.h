//===-- ThreadPlanShouldStopHere.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanShouldStopHere_h_
#define liblldb_ThreadPlanShouldStopHere_h_

#include "lldb/Target/ThreadPlan.h"

namespace lldb_private {

// This is an interface that ThreadPlans can adopt to allow flexible
// modifications of the behavior when a thread plan comes to a place where it
// would ordinarily stop.  If such modification makes sense for your plan,
// inherit from this class, and when you would be about to stop (in your
// ShouldStop method), call InvokeShouldStopHereCallback, passing in the frame
// comparison between where the step operation started and where you arrived.
// If it returns true, then QueueStepOutFromHere will queue the plan to execute
// instead of stopping.
//
// The classic example of the use of this is ThreadPlanStepInRange not stopping
// in frames that have no debug information.
//
// This class also defines a set of flags to control general aspects of this
// "ShouldStop" behavior.
// A class implementing this protocol needs to define a default set of flags,
// and can provide access to
// changing that default flag set if it wishes.

class ThreadPlanShouldStopHere {
public:
  struct ThreadPlanShouldStopHereCallbacks {
    ThreadPlanShouldStopHereCallbacks() {
      should_stop_here_callback = nullptr;
      step_from_here_callback = nullptr;
    }

    ThreadPlanShouldStopHereCallbacks(
        ThreadPlanShouldStopHereCallback should_stop,
        ThreadPlanStepFromHereCallback step_from_here) {
      should_stop_here_callback = should_stop;
      step_from_here_callback = step_from_here;
    }

    void Clear() {
      should_stop_here_callback = nullptr;
      step_from_here_callback = nullptr;
    }

    ThreadPlanShouldStopHereCallback should_stop_here_callback;
    ThreadPlanStepFromHereCallback step_from_here_callback;
  };

  enum {
    eNone = 0,
    eAvoidInlines = (1 << 0),
    eStepInAvoidNoDebug = (1 << 1),
    eStepOutAvoidNoDebug = (1 << 2)
  };

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ThreadPlanShouldStopHere(ThreadPlan *owner);

  ThreadPlanShouldStopHere(ThreadPlan *owner,
                           const ThreadPlanShouldStopHereCallbacks *callbacks,
                           void *baton = nullptr);
  virtual ~ThreadPlanShouldStopHere();

  // Set the ShouldStopHere callbacks.  Pass in null to clear them and have no
  // special behavior (though you can also call ClearShouldStopHereCallbacks
  // for that purpose.  If you pass in a valid pointer, it will adopt the non-
  // null fields, and any null fields will be set to the default values.

  void
  SetShouldStopHereCallbacks(const ThreadPlanShouldStopHereCallbacks *callbacks,
                             void *baton) {
    if (callbacks) {
      m_callbacks = *callbacks;
      if (!m_callbacks.should_stop_here_callback)
        m_callbacks.should_stop_here_callback =
            ThreadPlanShouldStopHere::DefaultShouldStopHereCallback;
      if (!m_callbacks.step_from_here_callback)
        m_callbacks.step_from_here_callback =
            ThreadPlanShouldStopHere::DefaultStepFromHereCallback;
    } else {
      ClearShouldStopHereCallbacks();
    }
    m_baton = baton;
  }

  void ClearShouldStopHereCallbacks() { m_callbacks.Clear(); }

  bool InvokeShouldStopHereCallback(lldb::FrameComparison operation,
                                    Status &status);

  lldb::ThreadPlanSP
  CheckShouldStopHereAndQueueStepOut(lldb::FrameComparison operation,
                                     Status &status);

  lldb_private::Flags &GetFlags() { return m_flags; }

  const lldb_private::Flags &GetFlags() const { return m_flags; }

protected:
  static bool DefaultShouldStopHereCallback(ThreadPlan *current_plan,
                                            Flags &flags,
                                            lldb::FrameComparison operation,
                                            Status &status, void *baton);

  static lldb::ThreadPlanSP
  DefaultStepFromHereCallback(ThreadPlan *current_plan, Flags &flags,
                              lldb::FrameComparison operation, Status &status,
                              void *baton);

  virtual lldb::ThreadPlanSP
  QueueStepOutFromHerePlan(Flags &flags, lldb::FrameComparison operation,
                           Status &status);

  // Implement this, and call it in the plan's constructor to set the default
  // flags.
  virtual void SetFlagsToDefault() = 0;

  ThreadPlanShouldStopHereCallbacks m_callbacks;
  void *m_baton;
  ThreadPlan *m_owner;
  lldb_private::Flags m_flags;

private:
  DISALLOW_COPY_AND_ASSIGN(ThreadPlanShouldStopHere);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanShouldStopHere_h_
