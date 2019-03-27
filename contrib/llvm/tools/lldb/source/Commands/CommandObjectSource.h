//===-- CommandObjectSource.h.h -----------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectSource_h_
#define liblldb_CommandObjectSource_h_

#include "lldb/Core/STLUtils.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordSource
//-------------------------------------------------------------------------

class CommandObjectMultiwordSource : public CommandObjectMultiword {
public:
  CommandObjectMultiwordSource(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordSource() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectSource_h_
