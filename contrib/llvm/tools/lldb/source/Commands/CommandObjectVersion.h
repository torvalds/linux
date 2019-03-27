//===-- CommandObjectVersion.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectVersion_h_
#define liblldb_CommandObjectVersion_h_

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectVersion
//-------------------------------------------------------------------------

class CommandObjectVersion : public CommandObjectParsed {
public:
  CommandObjectVersion(CommandInterpreter &interpreter);

  ~CommandObjectVersion() override;

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectVersion_h_
