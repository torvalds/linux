//===-- CommandObjectBreakpoint.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectBreakpoint_h_
#define liblldb_CommandObjectBreakpoint_h_


#include <utility>
#include <vector>

#include "lldb/lldb-private.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/STLUtils.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"


namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordBreakpoint
//-------------------------------------------------------------------------

class CommandObjectMultiwordBreakpoint : public CommandObjectMultiword {
public:
  CommandObjectMultiwordBreakpoint(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordBreakpoint() override;

  static void VerifyBreakpointOrLocationIDs(Args &args, Target *target,
                                            CommandReturnObject &result,
                                            BreakpointIDList *valid_ids,
                                            BreakpointName::Permissions
                                                 ::PermissionKinds purpose) {
    VerifyIDs(args, target, true, result, valid_ids, purpose);
  }

  static void VerifyBreakpointIDs(Args &args, Target *target,
                                  CommandReturnObject &result,
                                  BreakpointIDList *valid_ids,
                                  BreakpointName::Permissions::PermissionKinds 
                                      purpose) {
    VerifyIDs(args, target, false, result, valid_ids, purpose);
  }

private:
  static void VerifyIDs(Args &args, Target *target, bool allow_locations,
                        CommandReturnObject &result,
                        BreakpointIDList *valid_ids,
                        BreakpointName::Permissions::PermissionKinds 
                                      purpose);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectBreakpoint_h_
