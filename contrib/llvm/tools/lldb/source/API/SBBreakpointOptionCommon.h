//===-- SBBreakpointOptionCommon.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBBreakpointOptionCommons_h_
#define LLDB_SBBreakpointOptionCommons_h_

#include "lldb/API/SBDefines.h"
#include "lldb/Utility/Baton.h"

namespace lldb
{
struct CallbackData {
  SBBreakpointHitCallback callback;
  void *callback_baton;
};

class SBBreakpointCallbackBaton : public lldb_private::TypedBaton<CallbackData> {
public:
  SBBreakpointCallbackBaton(SBBreakpointHitCallback callback,
                            void *baton);

  ~SBBreakpointCallbackBaton();

  static bool PrivateBreakpointHitCallback(void *baton,
                                           lldb_private::StoppointCallbackContext *ctx,
                                           lldb::user_id_t break_id,
                                           lldb::user_id_t break_loc_id);
};

} // namespace lldb
#endif // LLDB_SBBreakpointOptionCommons_h_
