//===-- SBBreakpointName.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpointName.h"
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBThread.h"

#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "lldb/lldb-enumerations.h"

#include "SBBreakpointOptionCommon.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

SBBreakpointCallbackBaton::SBBreakpointCallbackBaton(SBBreakpointHitCallback 
                                                         callback,
                                                     void *baton)
      : TypedBaton(llvm::make_unique<CallbackData>()) {
    getItem()->callback = callback;
    getItem()->callback_baton = baton;
  }

 bool SBBreakpointCallbackBaton::PrivateBreakpointHitCallback(void *baton,
                                                  StoppointCallbackContext *ctx,
                                                  lldb::user_id_t break_id,
                                                  lldb::user_id_t break_loc_id)
{
  ExecutionContext exe_ctx(ctx->exe_ctx_ref);
  BreakpointSP bp_sp(
      exe_ctx.GetTargetRef().GetBreakpointList().FindBreakpointByID(break_id));
  if (baton && bp_sp) {
    CallbackData *data = (CallbackData *)baton;
    lldb_private::Breakpoint *bp = bp_sp.get();
    if (bp && data->callback) {
      Process *process = exe_ctx.GetProcessPtr();
      if (process) {
        SBProcess sb_process(process->shared_from_this());
        SBThread sb_thread;
        SBBreakpointLocation sb_location;
        assert(bp_sp);
        sb_location.SetLocation(bp_sp->FindLocationByID(break_loc_id));
        Thread *thread = exe_ctx.GetThreadPtr();
        if (thread)
          sb_thread.SetThread(thread->shared_from_this());

        return data->callback(data->callback_baton, sb_process, sb_thread,
                              sb_location);
      }
    }
  }
  return true; // Return true if we should stop at this breakpoint
}

SBBreakpointCallbackBaton::~SBBreakpointCallbackBaton() = default;
