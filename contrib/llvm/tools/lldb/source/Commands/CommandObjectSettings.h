//===-- CommandObjectSettings.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectSettings_h_
#define liblldb_CommandObjectSettings_h_

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordSettings
//-------------------------------------------------------------------------

class CommandObjectMultiwordSettings : public CommandObjectMultiword {
public:
  CommandObjectMultiwordSettings(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordSettings() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectSettings_h_
