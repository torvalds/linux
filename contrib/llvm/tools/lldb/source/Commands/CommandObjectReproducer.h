//===-- CommandObjectReproducer.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectReproducer_h_
#define liblldb_CommandObjectReproducer_h_

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectReproducer
//-------------------------------------------------------------------------

class CommandObjectReproducer : public CommandObjectMultiword {
public:
  CommandObjectReproducer(CommandInterpreter &interpreter);

  ~CommandObjectReproducer() override;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectReproducer_h_
