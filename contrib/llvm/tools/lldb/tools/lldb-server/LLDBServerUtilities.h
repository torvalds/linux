//===-- LLDBServerUtilities.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {
namespace lldb_server {

class LLDBServerUtilities {
public:
  static bool SetupLogging(const std::string &log_file,
                           const llvm::StringRef &log_channels,
                           uint32_t log_options);
};
}
}
