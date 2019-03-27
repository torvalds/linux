//===--- TCE.h - TCE Tool and ToolChain Implementations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H

#include "clang/Driver/Driver.h"
#include "clang/Driver/ToolChain.h"
#include <set>

namespace clang {
namespace driver {
namespace toolchains {

/// TCEToolChain - A tool chain using the llvm bitcode tools to perform
/// all subcommands. See http://tce.cs.tut.fi for our peculiar target.
class LLVM_LIBRARY_VISIBILITY TCEToolChain : public ToolChain {
public:
  TCEToolChain(const Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args);
  ~TCEToolChain() override;

  bool IsMathErrnoDefault() const override;
  bool isPICDefault() const override;
  bool isPIEDefault() const override;
  bool isPICDefaultForced() const override;
};

/// Toolchain for little endian TCE cores.
class LLVM_LIBRARY_VISIBILITY TCELEToolChain : public TCEToolChain {
public:
  TCELEToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args);
  ~TCELEToolChain() override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_TCE_H
