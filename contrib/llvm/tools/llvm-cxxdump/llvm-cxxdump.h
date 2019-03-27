//===-- llvm-cxxdump.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_CXXDUMP_LLVM_CXXDUMP_H
#define LLVM_TOOLS_LLVM_CXXDUMP_LLVM_CXXDUMP_H

#include "llvm/Support/CommandLine.h"
#include <string>

namespace opts {
extern llvm::cl::list<std::string> InputFilenames;
} // namespace opts

#define LLVM_CXXDUMP_ENUM_ENT(ns, enum)                                        \
  { #enum, ns::enum }

#endif
