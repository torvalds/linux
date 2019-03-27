//===--- Linux.h - Linux ToolChain Implementations --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LINUX_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LINUX_H

#include "Gnu.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Linux : public Generic_ELF {
public:
  Linux(const Driver &D, const llvm::Triple &Triple,
        const llvm::opt::ArgList &Args);

  bool HasNativeLLVMSupport() const override;

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addLibCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  void addLibStdCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                          llvm::opt::ArgStringList &CC1Args) const override;
  void AddIAMCUIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                           llvm::opt::ArgStringList &CC1Args) const override;
  CXXStdlibType GetDefaultCXXStdlibType() const override;
  bool isPIEDefault() const override;
  bool IsMathErrnoDefault() const override;
  SanitizerMask getSupportedSanitizers() const override;
  void addProfileRTLibs(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs) const override;
  virtual std::string computeSysRoot() const;

  virtual std::string getDynamicLinker(const llvm::opt::ArgList &Args) const;

  std::vector<std::string> ExtraOpts;

protected:
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LINUX_H
