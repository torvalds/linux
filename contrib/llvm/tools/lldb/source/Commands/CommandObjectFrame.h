//===-- CommandObjectFrame.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectFrame_h_
#define liblldb_CommandObjectFrame_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordFrame
//-------------------------------------------------------------------------

class CommandObjectMultiwordFrame : public CommandObjectMultiword {
public:
  CommandObjectMultiwordFrame(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordFrame() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectFrame_h_
