//===--- SystemZ.h - SystemZ-specific Tool Helpers --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_SYSTEMZ_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_SYSTEMZ_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace systemz {

const char *getSystemZTargetCPU(const llvm::opt::ArgList &Args);

void getSystemZTargetFeatures(const llvm::opt::ArgList &Args,
                              std::vector<llvm::StringRef> &Features);

} // end namespace systemz
} // end namespace target
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_SYSTEMZ_H
