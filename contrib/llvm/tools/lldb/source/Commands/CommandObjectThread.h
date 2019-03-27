//===-- CommandObjectThread.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectThread_h_
#define liblldb_CommandObjectThread_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

class CommandObjectMultiwordThread : public CommandObjectMultiword {
public:
  CommandObjectMultiwordThread(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordThread() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectThread_h_
