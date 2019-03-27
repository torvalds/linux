//===--- Haiku.cpp - Haiku ToolChain Implementations ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Haiku.h"
#include "CommonArgs.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// Haiku - Haiku tool chain which can call as(1) and ld(1) directly.

Haiku::Haiku(const Driver &D, const llvm::Triple& Triple, const ArgList &Args)
  : Generic_ELF(D, Triple, Args) {

}

void Haiku::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                  llvm::opt::ArgStringList &CC1Args) const {
  addSystemInclude(DriverArgs, CC1Args,
                   getDriver().SysRoot + "/system/develop/headers/c++/v1");
}

void Haiku::addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args) const {
  addLibStdCXXIncludePaths(getDriver().SysRoot, "/system/develop/headers/c++",
                           getTriple().str(), "", "", "", DriverArgs, CC1Args);
}
