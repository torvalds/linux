//===-- CommandObjectLog.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectLog_h_
#define liblldb_CommandObjectLog_h_

#include <map>
#include <string>

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectLog
//-------------------------------------------------------------------------

class CommandObjectLog : public CommandObjectMultiword {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CommandObjectLog(CommandInterpreter &interpreter);

  ~CommandObjectLog() override;

private:
  //------------------------------------------------------------------
  // For CommandObjectLog only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(CommandObjectLog);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectLog_h_
