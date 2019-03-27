//===--- Myriad.h - Myriad ToolChain Implementations ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MYRIAD_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MYRIAD_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {

/// SHAVE tools -- Directly call moviCompile and moviAsm
namespace SHAVE {
class LLVM_LIBRARY_VISIBILITY Compiler : public Tool {
public:
  Compiler(const ToolChain &TC) : Tool("moviCompile", "movicompile", TC) {}

  bool hasIntegratedCPP() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Assembler : public Tool {
public:
  Assembler(const ToolChain &TC) : Tool("moviAsm", "moviAsm", TC) {}

  bool hasIntegratedCPP() const override { return false; } // not sure.

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace SHAVE

/// The Myriad toolchain uses tools that are in two different namespaces.
/// The Compiler and Assembler as defined above are in the SHAVE namespace,
/// whereas the linker, which accepts code for a mixture of Sparc and SHAVE,
/// is in the Myriad namespace.
namespace Myriad {
class LLVM_LIBRARY_VISIBILITY Linker : public GnuTool {
public:
  Linker(const ToolChain &TC) : GnuTool("shave::Linker", "ld", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace Myriad
} // end namespace tools

namespace toolchains {

/// MyriadToolChain - A tool chain using either clang or the external compiler
/// installed by the Movidius SDK to perform all subcommands.
class LLVM_LIBRARY_VISIBILITY MyriadToolChain : public Generic_ELF {
public:
  MyriadToolChain(const Driver &D, const llvm::Triple &Triple,
                  const llvm::opt::ArgList &Args);
  ~MyriadToolChain() override;

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addLibCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  void addLibStdCxxIncludePaths(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;
  Tool *SelectTool(const JobAction &JA) const override;
  unsigned GetDefaultDwarfVersion() const override { return 2; }
  SanitizerMask getSupportedSanitizers() const override;

protected:
  Tool *buildLinker() const override;
  bool isShaveCompilation(const llvm::Triple &T) const {
    return T.getArch() == llvm::Triple::shave;
  }

private:
  mutable std::unique_ptr<Tool> Compiler;
  mutable std::unique_ptr<Tool> Assembler;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MYRIAD_H
