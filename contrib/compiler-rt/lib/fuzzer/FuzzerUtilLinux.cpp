//===- FuzzerUtilLinux.cpp - Misc utils for Linux. ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Misc utils for Linux.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_LINUX || LIBFUZZER_NETBSD || LIBFUZZER_FREEBSD ||                \
    LIBFUZZER_OPENBSD
#include "FuzzerCommand.h"

#include <stdlib.h>

namespace fuzzer {

int ExecuteCommand(const Command &Cmd) {
  std::string CmdLine = Cmd.toString();
  return system(CmdLine.c_str());
}

} // namespace fuzzer

#endif
