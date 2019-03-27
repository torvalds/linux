//===-- Debug.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Debug_h_
#define liblldb_Debug_h_

#include <vector>

#include "lldb/lldb-private.h"

namespace lldb_private {

//------------------------------------------------------------------
// Tells a thread what it needs to do when the process is resumed.
//------------------------------------------------------------------
struct ResumeAction {
  lldb::tid_t tid;       // The thread ID that this action applies to,
                         // LLDB_INVALID_THREAD_ID for the default thread
                         // action
  lldb::StateType state; // Valid values are eStateStopped/eStateSuspended,
                         // eStateRunning, and eStateStepping.
  int signal; // When resuming this thread, resume it with this signal if this
              // value is > 0
};

//------------------------------------------------------------------
// A class that contains instructions for all threads for
// NativeProcessProtocol::Resume(). Each thread can either run, stay suspended,
// or step when the process is resumed. We optionally have the ability to also
// send a signal to the thread when the action is run or step.
//------------------------------------------------------------------
class ResumeActionList {
public:
  ResumeActionList() : m_actions(), m_signal_handled() {}

  ResumeActionList(lldb::StateType default_action, int signal)
      : m_actions(), m_signal_handled() {
    SetDefaultThreadActionIfNeeded(default_action, signal);
  }

  ResumeActionList(const ResumeAction *actions, size_t num_actions)
      : m_actions(), m_signal_handled() {
    if (actions && num_actions) {
      m_actions.assign(actions, actions + num_actions);
      m_signal_handled.assign(num_actions, false);
    }
  }

  ~ResumeActionList() = default;

  bool IsEmpty() const { return m_actions.empty(); }

  void Append(const ResumeAction &action) {
    m_actions.push_back(action);
    m_signal_handled.push_back(false);
  }

  void AppendAction(lldb::tid_t tid, lldb::StateType state, int signal = 0) {
    ResumeAction action = {tid, state, signal};
    Append(action);
  }

  void AppendResumeAll() {
    AppendAction(LLDB_INVALID_THREAD_ID, lldb::eStateRunning);
  }

  void AppendSuspendAll() {
    AppendAction(LLDB_INVALID_THREAD_ID, lldb::eStateStopped);
  }

  void AppendStepAll() {
    AppendAction(LLDB_INVALID_THREAD_ID, lldb::eStateStepping);
  }

  const ResumeAction *GetActionForThread(lldb::tid_t tid,
                                         bool default_ok) const {
    const size_t num_actions = m_actions.size();
    for (size_t i = 0; i < num_actions; ++i) {
      if (m_actions[i].tid == tid)
        return &m_actions[i];
    }
    if (default_ok && tid != LLDB_INVALID_THREAD_ID)
      return GetActionForThread(LLDB_INVALID_THREAD_ID, false);
    return nullptr;
  }

  size_t NumActionsWithState(lldb::StateType state) const {
    size_t count = 0;
    const size_t num_actions = m_actions.size();
    for (size_t i = 0; i < num_actions; ++i) {
      if (m_actions[i].state == state)
        ++count;
    }
    return count;
  }

  bool SetDefaultThreadActionIfNeeded(lldb::StateType action, int signal) {
    if (GetActionForThread(LLDB_INVALID_THREAD_ID, true) == nullptr) {
      // There isn't a default action so we do need to set it.
      ResumeAction default_action = {LLDB_INVALID_THREAD_ID, action, signal};
      m_actions.push_back(default_action);
      m_signal_handled.push_back(false);
      return true; // Return true as we did add the default action
    }
    return false;
  }

  void SetSignalHandledForThread(lldb::tid_t tid) const {
    if (tid != LLDB_INVALID_THREAD_ID) {
      const size_t num_actions = m_actions.size();
      for (size_t i = 0; i < num_actions; ++i) {
        if (m_actions[i].tid == tid)
          m_signal_handled[i] = true;
      }
    }
  }

  const ResumeAction *GetFirst() const { return m_actions.data(); }

  size_t GetSize() const { return m_actions.size(); }

  void Clear() {
    m_actions.clear();
    m_signal_handled.clear();
  }

protected:
  std::vector<ResumeAction> m_actions;
  mutable std::vector<bool> m_signal_handled;
};

struct ThreadStopInfo {
  lldb::StopReason reason;
  union {
    // eStopReasonSignal
    struct {
      uint32_t signo;
    } signal;

    // eStopReasonException
    struct {
      uint64_t type;
      uint32_t data_count;
      lldb::addr_t data[8];
    } exception;
  } details;
};
}

#endif // liblldb_Debug_h_
