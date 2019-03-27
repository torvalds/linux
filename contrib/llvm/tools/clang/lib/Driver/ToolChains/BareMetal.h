//===--- BareMetal.h - Bare Metal Tool and ToolChain -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_BAREMETAL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_BAREMETAL_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

#include <string>

namespace clang {
namespace driver {

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY BareMetal : public ToolChain {
public:
  BareMetal(const Driver &D, const llvm::Triple &Triple,
            const llvm::opt::ArgList &Args);
  ~BareMetal() override;

  static bool handlesTarget(const llvm::Triple &Triple);
protected:
  Tool *buildLinker() const override;

public:
  bool useIntegratedAs() const override { return true; }
  bool isCrossCompiling() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault() const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }

  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  CXXStdlibType GetDefaultCXXStdlibType() const override {
    return ToolChain::CST_Libcxx;
  }

  const char *getDefaultLinker() const override { return "ld.lld"; }

  std::string getRuntimesDir() const;
  void AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                 llvm::opt::ArgStringList &CC1Args) const override;
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;
  void AddLinkRuntimeLib(const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs) const;
};

} // namespace toolchains

namespace tools {
namespace baremetal {

class LLVM_LIBRARY_VISIBILITY Linker : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("baremetal::Linker", "ld.lld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // namespace baremetal
} // namespace tools

} // namespace driver
} // namespace clang

#endif
