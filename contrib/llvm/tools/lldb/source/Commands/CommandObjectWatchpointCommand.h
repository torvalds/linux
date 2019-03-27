//===-- CommandObjectWatchpointCommand.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectWatchpointCommand_h_
#define liblldb_CommandObjectWatchpointCommand_h_



#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordWatchpoint
//-------------------------------------------------------------------------

class CommandObjectWatchpointCommand : public CommandObjectMultiword {
public:
  CommandObjectWatchpointCommand(CommandInterpreter &interpreter);

  ~CommandObjectWatchpointCommand() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectWatchpointCommand_h_
