//===-- CommandObjectPlatform.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectPlatform_h_
#define liblldb_CommandObjectPlatform_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectPlatform
//-------------------------------------------------------------------------

class CommandObjectPlatform : public CommandObjectMultiword {
public:
  CommandObjectPlatform(CommandInterpreter &interpreter);

  ~CommandObjectPlatform() override;

private:
  DISALLOW_COPY_AND_ASSIGN(CommandObjectPlatform);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectPlatform_h_
