//===-- CommandObjectProcess.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectProcess_h_
#define liblldb_CommandObjectProcess_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordProcess
//-------------------------------------------------------------------------

class CommandObjectMultiwordProcess : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcess(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordProcess() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectProcess_h_
