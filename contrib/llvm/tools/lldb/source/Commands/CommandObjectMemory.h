//===-- CommandObjectMemory.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectMemory_h_
#define liblldb_CommandObjectMemory_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

class CommandObjectMemory : public CommandObjectMultiword {
public:
  CommandObjectMemory(CommandInterpreter &interpreter);

  ~CommandObjectMemory() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectMemory_h_
