//===-- WatchpointOptions.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/WatchpointOptions.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

bool WatchpointOptions::NullCallback(void *baton,
                                     StoppointCallbackContext *context,
                                     lldb::user_id_t watch_id) {
  return true;
}

//----------------------------------------------------------------------
// WatchpointOptions constructor
//----------------------------------------------------------------------
WatchpointOptions::WatchpointOptions()
    : m_callback(WatchpointOptions::NullCallback), m_callback_baton_sp(),
      m_callback_is_synchronous(false), m_thread_spec_ap() {}

//----------------------------------------------------------------------
// WatchpointOptions copy constructor
//----------------------------------------------------------------------
WatchpointOptions::WatchpointOptions(const WatchpointOptions &rhs)
    : m_callback(rhs.m_callback), m_callback_baton_sp(rhs.m_callback_baton_sp),
      m_callback_is_synchronous(rhs.m_callback_is_synchronous),
      m_thread_spec_ap() {
  if (rhs.m_thread_spec_ap.get() != nullptr)
    m_thread_spec_ap.reset(new ThreadSpec(*rhs.m_thread_spec_ap.get()));
}

//----------------------------------------------------------------------
// WatchpointOptions assignment operator
//----------------------------------------------------------------------
const WatchpointOptions &WatchpointOptions::
operator=(const WatchpointOptions &rhs) {
  m_callback = rhs.m_callback;
  m_callback_baton_sp = rhs.m_callback_baton_sp;
  m_callback_is_synchronous = rhs.m_callback_is_synchronous;
  if (rhs.m_thread_spec_ap.get() != nullptr)
    m_thread_spec_ap.reset(new ThreadSpec(*rhs.m_thread_spec_ap.get()));
  return *this;
}

WatchpointOptions *
WatchpointOptions::CopyOptionsNoCallback(WatchpointOptions &orig) {
  WatchpointHitCallback orig_callback = orig.m_callback;
  lldb::BatonSP orig_callback_baton_sp = orig.m_callback_baton_sp;
  bool orig_is_sync = orig.m_callback_is_synchronous;

  orig.ClearCallback();
  WatchpointOptions *ret_val = new WatchpointOptions(orig);

  orig.SetCallback(orig_callback, orig_callback_baton_sp, orig_is_sync);

  return ret_val;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
WatchpointOptions::~WatchpointOptions() = default;

//------------------------------------------------------------------
// Callbacks
//------------------------------------------------------------------
void WatchpointOptions::SetCallback(WatchpointHitCallback callback,
                                    const BatonSP &callback_baton_sp,
                                    bool callback_is_synchronous) {
  m_callback_is_synchronous = callback_is_synchronous;
  m_callback = callback;
  m_callback_baton_sp = callback_baton_sp;
}

void WatchpointOptions::ClearCallback() {
  m_callback = WatchpointOptions::NullCallback;
  m_callback_is_synchronous = false;
  m_callback_baton_sp.reset();
}

Baton *WatchpointOptions::GetBaton() { return m_callback_baton_sp.get(); }

const Baton *WatchpointOptions::GetBaton() const {
  return m_callback_baton_sp.get();
}

bool WatchpointOptions::InvokeCallback(StoppointCallbackContext *context,
                                       lldb::user_id_t watch_id) {
  if (m_callback && context->is_synchronous == IsCallbackSynchronous()) {
    return m_callback(m_callback_baton_sp ? m_callback_baton_sp->data()
                                          : nullptr,
                      context, watch_id);
  } else
    return true;
}

bool WatchpointOptions::HasCallback() {
  return m_callback != WatchpointOptions::NullCallback;
}

const ThreadSpec *WatchpointOptions::GetThreadSpecNoCreate() const {
  return m_thread_spec_ap.get();
}

ThreadSpec *WatchpointOptions::GetThreadSpec() {
  if (m_thread_spec_ap.get() == nullptr)
    m_thread_spec_ap.reset(new ThreadSpec());

  return m_thread_spec_ap.get();
}

void WatchpointOptions::SetThreadID(lldb::tid_t thread_id) {
  GetThreadSpec()->SetTID(thread_id);
}

void WatchpointOptions::GetCallbackDescription(
    Stream *s, lldb::DescriptionLevel level) const {
  if (m_callback_baton_sp.get()) {
    s->EOL();
    m_callback_baton_sp->GetDescription(s, level);
  }
}

void WatchpointOptions::GetDescription(Stream *s,
                                       lldb::DescriptionLevel level) const {
  // Figure out if there are any options not at their default value, and only
  // print anything if there are:

  if ((GetThreadSpecNoCreate() != nullptr &&
       GetThreadSpecNoCreate()->HasSpecification())) {
    if (level == lldb::eDescriptionLevelVerbose) {
      s->EOL();
      s->IndentMore();
      s->Indent();
      s->PutCString("Watchpoint Options:\n");
      s->IndentMore();
      s->Indent();
    } else
      s->PutCString(" Options: ");

    if (m_thread_spec_ap.get())
      m_thread_spec_ap->GetDescription(s, level);
    else if (level == eDescriptionLevelBrief)
      s->PutCString("thread spec: no ");
    if (level == lldb::eDescriptionLevelFull) {
      s->IndentLess();
      s->IndentMore();
    }
  }

  GetCallbackDescription(s, level);
}

void WatchpointOptions::CommandBaton::GetDescription(
    Stream *s, lldb::DescriptionLevel level) const {
  const CommandData *data = getItem();

  if (level == eDescriptionLevelBrief) {
    s->Printf(", commands = %s",
              (data && data->user_source.GetSize() > 0) ? "yes" : "no");
    return;
  }

  s->IndentMore();
  s->Indent("watchpoint commands:\n");

  s->IndentMore();
  if (data && data->user_source.GetSize() > 0) {
    const size_t num_strings = data->user_source.GetSize();
    for (size_t i = 0; i < num_strings; ++i) {
      s->Indent(data->user_source.GetStringAtIndex(i));
      s->EOL();
    }
  } else {
    s->PutCString("No commands.\n");
  }
  s->IndentLess();
  s->IndentLess();
}
