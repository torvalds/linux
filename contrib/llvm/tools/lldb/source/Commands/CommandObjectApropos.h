//===-- CommandObjectApropos.h -----------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectApropos_h_
#define liblldb_CommandObjectApropos_h_

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectApropos
//-------------------------------------------------------------------------

class CommandObjectApropos : public CommandObjectParsed {
public:
  CommandObjectApropos(CommandInterpreter &interpreter);

  ~CommandObjectApropos() override;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectApropos_h_
