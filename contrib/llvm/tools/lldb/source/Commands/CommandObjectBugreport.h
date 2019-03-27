//===-- CommandObjectBugreport.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectBugreport_h_
#define liblldb_CommandObjectBugreport_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordBugreport
//-------------------------------------------------------------------------

class CommandObjectMultiwordBugreport : public CommandObjectMultiword {
public:
  CommandObjectMultiwordBugreport(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordBugreport() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectBugreport_h_
