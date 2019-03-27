//===-- ThreadPlanPython.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlan.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanPython.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlanPython
//----------------------------------------------------------------------

ThreadPlanPython::ThreadPlanPython(Thread &thread, const char *class_name)
    : ThreadPlan(ThreadPlan::eKindPython, "Python based Thread Plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_class_name(class_name), m_did_push(false) {
  SetIsMasterPlan(true);
  SetOkayToDiscard(true);
  SetPrivate(false);
}

ThreadPlanPython::~ThreadPlanPython() {
  // FIXME, do I need to decrement the ref count on this implementation object
  // to make it go away?
}

bool ThreadPlanPython::ValidatePlan(Stream *error) {
  if (!m_did_push)
    return true;

  if (!m_implementation_sp) {
    if (error)
      error->Printf("Python thread plan does not have an implementation");
    return false;
  }

  return true;
}

void ThreadPlanPython::DidPush() {
  // We set up the script side in DidPush, so that it can push other plans in
  // the constructor, and doesn't have to care about the details of DidPush.
  m_did_push = true;
  if (!m_class_name.empty()) {
    ScriptInterpreter *script_interp = m_thread.GetProcess()
                                           ->GetTarget()
                                           .GetDebugger()
                                           .GetCommandInterpreter()
                                           .GetScriptInterpreter();
    if (script_interp) {
      m_implementation_sp = script_interp->CreateScriptedThreadPlan(
          m_class_name.c_str(), this->shared_from_this());
    }
  }
}

bool ThreadPlanPython::ShouldStop(Event *event_ptr) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());

  bool should_stop = true;
  if (m_implementation_sp) {
    ScriptInterpreter *script_interp = m_thread.GetProcess()
                                           ->GetTarget()
                                           .GetDebugger()
                                           .GetCommandInterpreter()
                                           .GetScriptInterpreter();
    if (script_interp) {
      bool script_error;
      should_stop = script_interp->ScriptedThreadPlanShouldStop(
          m_implementation_sp, event_ptr, script_error);
      if (script_error)
        SetPlanComplete(false);
    }
  }
  return should_stop;
}

bool ThreadPlanPython::IsPlanStale() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());

  bool is_stale = true;
  if (m_implementation_sp) {
    ScriptInterpreter *script_interp = m_thread.GetProcess()
                                           ->GetTarget()
                                           .GetDebugger()
                                           .GetCommandInterpreter()
                                           .GetScriptInterpreter();
    if (script_interp) {
      bool script_error;
      is_stale = script_interp->ScriptedThreadPlanIsStale(m_implementation_sp,
                                                          script_error);
      if (script_error)
        SetPlanComplete(false);
    }
  }
  return is_stale;
}

bool ThreadPlanPython::DoPlanExplainsStop(Event *event_ptr) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());

  bool explains_stop = true;
  if (m_implementation_sp) {
    ScriptInterpreter *script_interp = m_thread.GetProcess()
                                           ->GetTarget()
                                           .GetDebugger()
                                           .GetCommandInterpreter()
                                           .GetScriptInterpreter();
    if (script_interp) {
      bool script_error;
      explains_stop = script_interp->ScriptedThreadPlanExplainsStop(
          m_implementation_sp, event_ptr, script_error);
      if (script_error)
        SetPlanComplete(false);
    }
  }
  return explains_stop;
}

bool ThreadPlanPython::MischiefManaged() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());
  bool mischief_managed = true;
  if (m_implementation_sp) {
    // I don't really need mischief_managed, since it's simpler to just call
    // SetPlanComplete in should_stop.
    mischief_managed = IsPlanComplete();
    if (mischief_managed)
      m_implementation_sp.reset();
  }
  return mischief_managed;
}

lldb::StateType ThreadPlanPython::GetPlanRunState() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());
  lldb::StateType run_state = eStateRunning;
  if (m_implementation_sp) {
    ScriptInterpreter *script_interp = m_thread.GetProcess()
                                           ->GetTarget()
                                           .GetDebugger()
                                           .GetCommandInterpreter()
                                           .GetScriptInterpreter();
    if (script_interp) {
      bool script_error;
      run_state = script_interp->ScriptedThreadPlanGetRunState(
          m_implementation_sp, script_error);
    }
  }
  return run_state;
}

// The ones below are not currently exported to Python.

bool ThreadPlanPython::StopOthers() {
  // For now Python plans run all threads, but we should add some controls for
  // this.
  return false;
}

void ThreadPlanPython::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  s->Printf("Python thread plan implemented by class %s.",
            m_class_name.c_str());
}

bool ThreadPlanPython::WillStop() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));
  if (log)
    log->Printf("%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
                m_class_name.c_str());
  return true;
}
