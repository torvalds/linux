//===--- Fuchsia.h - Fuchsia ToolChain Implementations ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FUCHSIA_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FUCHSIA_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace fuchsia {
class LLVM_LIBRARY_VISIBILITY Linker : public GnuTool {
public:
  Linker(const ToolChain &TC) : GnuTool("fuchsia::Linker", "ld.lld", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace fuchsia
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Fuchsia : public ToolChain {
public:
  Fuchsia(const Driver &D, const llvm::Triple &Triple,
          const llvm::opt::ArgList &Args);

  bool HasNativeLLVMSupport() const override { return true; }
  bool IsIntegratedAssemblerDefault() const override { return true; }
  bool IsMathErrnoDefault() const override { return false; }
  bool useRelaxRelocations() const override { return true; };
  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }
  CXXStdlibType GetDefaultCXXStdlibType() const override {
    return ToolChain::CST_Libcxx;
  }
  bool IsUnwindTablesDefault(const llvm::opt::ArgList &Args) const override {
    return true;
  }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault() const override { return true; }
  bool isPICDefaultForced() const override { return false; }
  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::GDB;
  }

  unsigned GetDefaultStackProtectorLevel(bool KernelOrKext) const override {
    return 2; // SSPStrong
  }

  std::string ComputeEffectiveClangTriple(const llvm::opt::ArgList &Args,
                                          types::ID InputType) const override;

  SanitizerMask getSupportedSanitizers() const override;
  SanitizerMask getDefaultSanitizers() const override;

  RuntimeLibType
  GetRuntimeLibType(const llvm::opt::ArgList &Args) const override;
  CXXStdlibType
  GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;

  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void
  AddClangCXXStdlibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args) const override;
  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;

  const char *getDefaultLinker() const override {
    return "ld.lld";
  }

protected:
  Tool *buildLinker() const override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_FUCHSIA_H
