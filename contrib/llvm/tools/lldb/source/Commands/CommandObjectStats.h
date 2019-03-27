//===-- CommandObjectStats.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectStats_h_
#define liblldb_CommandObjectStats_h_

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {
class CommandObjectStats : public CommandObjectMultiword {
public:
  CommandObjectStats(CommandInterpreter &interpreter);

  ~CommandObjectStats() override;
};
} // namespace lldb_private

#endif // liblldb_CommandObjectLanguage_h_
