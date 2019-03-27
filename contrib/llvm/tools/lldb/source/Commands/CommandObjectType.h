//===-- CommandObjectType.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectType_h_
#define liblldb_CommandObjectType_h_



#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class CommandObjectType : public CommandObjectMultiword {
public:
  CommandObjectType(CommandInterpreter &interpreter);

  ~CommandObjectType() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectType_h_
