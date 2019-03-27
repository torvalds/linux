//===-- StoppointCallbackContext.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"

using namespace lldb_private;

StoppointCallbackContext::StoppointCallbackContext()
    : event(nullptr), exe_ctx_ref(), is_synchronous(false) {}

StoppointCallbackContext::StoppointCallbackContext(
    Event *e, const ExecutionContext &exe_ctx, bool synchronously)
    : event(e), exe_ctx_ref(exe_ctx), is_synchronous(synchronously) {}

void StoppointCallbackContext::Clear() {
  event = nullptr;
  exe_ctx_ref.Clear();
  is_synchronous = false;
}
