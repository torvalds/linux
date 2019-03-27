//===-- CommandObjectGUI.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectGUI_h_
#define liblldb_CommandObjectGUI_h_

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectGUI
//-------------------------------------------------------------------------

class CommandObjectGUI : public CommandObjectParsed {
public:
  CommandObjectGUI(CommandInterpreter &interpreter);

  ~CommandObjectGUI() override;

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectGUI_h_
