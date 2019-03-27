//===-- CommandObjectWatchpoint.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectWatchpoint_h_
#define liblldb_CommandObjectWatchpoint_h_


#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/OptionGroupWatchpoint.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectMultiwordWatchpoint
//-------------------------------------------------------------------------

class CommandObjectMultiwordWatchpoint : public CommandObjectMultiword {
public:
  CommandObjectMultiwordWatchpoint(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordWatchpoint() override;

  static bool VerifyWatchpointIDs(Target *target, Args &args,
                                  std::vector<uint32_t> &wp_ids);
};

} // namespace lldb_private

#endif // liblldb_CommandObjectWatchpoint_h_
