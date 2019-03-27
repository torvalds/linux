//===-- CommandObjectLanguage.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectLanguage_h_
#define liblldb_CommandObjectLanguage_h_



#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/lldb-types.h"

namespace lldb_private {
class CommandObjectLanguage : public CommandObjectMultiword {
public:
  CommandObjectLanguage(CommandInterpreter &interpreter);

  ~CommandObjectLanguage() override;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result);
};
} // namespace lldb_private

#endif // liblldb_CommandObjectLanguage_h_
