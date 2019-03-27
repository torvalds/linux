//===--- ARM.h - ARM-specific (not AArch64) Tool Helpers --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_ARM_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_ARM_H

#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Option/Option.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace arm {

std::string getARMTargetCPU(StringRef CPU, llvm::StringRef Arch,
                            const llvm::Triple &Triple);
const std::string getARMArch(llvm::StringRef Arch, const llvm::Triple &Triple);
StringRef getARMCPUForMArch(llvm::StringRef Arch, const llvm::Triple &Triple);
StringRef getLLVMArchSuffixForARM(llvm::StringRef CPU, llvm::StringRef Arch,
                                  const llvm::Triple &Triple);

void appendBE8LinkFlag(const llvm::opt::ArgList &Args,
                       llvm::opt::ArgStringList &CmdArgs,
                       const llvm::Triple &Triple);
enum class ReadTPMode {
  Invalid,
  Soft,
  Cp15,
};

enum class FloatABI {
  Invalid,
  Soft,
  SoftFP,
  Hard,
};

FloatABI getARMFloatABI(const ToolChain &TC, const llvm::opt::ArgList &Args);
ReadTPMode getReadTPMode(const ToolChain &TC, const llvm::opt::ArgList &Args);

bool useAAPCSForMachO(const llvm::Triple &T);
void getARMArchCPUFromArgs(const llvm::opt::ArgList &Args,
                           llvm::StringRef &Arch, llvm::StringRef &CPU,
                           bool FromAs = false);
void getARMTargetFeatures(const ToolChain &TC, const llvm::Triple &Triple,
                          const llvm::opt::ArgList &Args,
                          llvm::opt::ArgStringList &CmdArgs,
                          std::vector<llvm::StringRef> &Features, bool ForAS);
int getARMSubArchVersionNumber(const llvm::Triple &Triple);
bool isARMMProfile(const llvm::Triple &Triple);

} // end namespace arm
} // end namespace tools
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_ARM_H
