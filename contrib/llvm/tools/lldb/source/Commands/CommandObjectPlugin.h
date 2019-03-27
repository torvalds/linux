//===-- CommandObjectPlugin.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectPlugin_h_
#define liblldb_CommandObjectPlugin_h_



#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class CommandObjectPlugin : public CommandObjectMultiword {
public:
  CommandObjectPlugin(CommandInterpreter &interpreter);

  ~CommandObjectPlugin() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectPlugin_h_
