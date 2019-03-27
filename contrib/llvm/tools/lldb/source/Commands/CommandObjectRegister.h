//===-- CommandObjectRegister.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectRegister_h_
#define liblldb_CommandObjectRegister_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectRegister
//-------------------------------------------------------------------------

class CommandObjectRegister : public CommandObjectMultiword {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CommandObjectRegister(CommandInterpreter &interpreter);

  ~CommandObjectRegister() override;

private:
  //------------------------------------------------------------------
  // For CommandObjectRegister only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(CommandObjectRegister);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectRegister_h_
