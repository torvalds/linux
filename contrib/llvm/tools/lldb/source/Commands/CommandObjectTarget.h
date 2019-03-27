//===-- CommandObjectTarget.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectTarget_h_
#define liblldb_CommandObjectTarget_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordTarget
//-------------------------------------------------------------------------

class CommandObjectMultiwordTarget : public CommandObjectMultiword {
public:
  CommandObjectMultiwordTarget(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordTarget() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectTarget_h_
