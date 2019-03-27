//===-- CommandObjectBreakpointCommand.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectBreakpointCommand_h_
#define liblldb_CommandObjectBreakpointCommand_h_



#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordBreakpoint
//-------------------------------------------------------------------------

class CommandObjectBreakpointCommand : public CommandObjectMultiword {
public:
  CommandObjectBreakpointCommand(CommandInterpreter &interpreter);

  ~CommandObjectBreakpointCommand() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectBreakpointCommand_h_
