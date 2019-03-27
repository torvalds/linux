//===-- OptionValueArgs.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueArgs_h_
#define liblldb_OptionValueArgs_h_

#include "lldb/Interpreter/OptionValueArray.h"

namespace lldb_private {

class OptionValueArgs : public OptionValueArray {
public:
  OptionValueArgs()
      : OptionValueArray(
            OptionValue::ConvertTypeToMask(OptionValue::eTypeString)) {}

  ~OptionValueArgs() override {}

  size_t GetArgs(Args &args);

  Type GetType() const override { return eTypeArgs; }
};

} // namespace lldb_private

#endif // liblldb_OptionValueArgs_h_
