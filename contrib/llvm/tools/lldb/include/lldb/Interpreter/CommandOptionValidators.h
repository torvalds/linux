//===-- CommandOptionValidators.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandOptionValidators_h_
#define liblldb_CommandOptionValidators_h_

#include "lldb/lldb-private-types.h"

namespace lldb_private {

class Platform;
class ExecutionContext;

class PosixPlatformCommandOptionValidator : public OptionValidator {
  bool IsValid(Platform &platform,
               const ExecutionContext &target) const override;
  const char *ShortConditionString() const override;
  const char *LongConditionString() const override;
};

} // namespace lldb_private

#endif // liblldb_CommandOptionValidators_h_
