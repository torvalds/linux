//===-- CommandObjectQuit.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectQuit_h_
#define liblldb_CommandObjectQuit_h_

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectQuit
//-------------------------------------------------------------------------

class CommandObjectQuit : public CommandObjectParsed {
public:
  CommandObjectQuit(CommandInterpreter &interpreter);

  ~CommandObjectQuit() override;

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override;

  bool ShouldAskForConfirmation(bool &is_a_detach);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectQuit_h_
