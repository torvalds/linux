//===--- MSVC.h - MSVC ToolChain Implementations ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H

#include "Cuda.h"
#include "clang/Basic/DebugInfoOptions.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {

/// Visual studio tools.
namespace visualstudio {
class LLVM_LIBRARY_VISIBILITY Linker : public Tool {
public:
  Linker(const ToolChain &TC)
      : Tool("visualstudio::Linker", "linker", TC, RF_Full,
             llvm::sys::WEM_UTF16) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Compiler : public Tool {
public:
  Compiler(const ToolChain &TC)
      : Tool("visualstudio::Compiler", "compiler", TC, RF_Full,
             llvm::sys::WEM_UTF16) {}

  bool hasIntegratedAssembler() const override { return true; }
  bool hasIntegratedCPP() const override { return true; }
  bool isLinkJob() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;

  std::unique_ptr<Command> GetCommand(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const llvm::opt::ArgList &TCArgs,
                                      const char *LinkingOutput) const;
};
} // end namespace visualstudio

} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MSVCToolChain : public ToolChain {
public:
  MSVCToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;

  bool IsIntegratedAssemblerDefault() const override;
  bool IsUnwindTablesDefault(const llvm::opt::ArgList &Args) const override;
  bool isPICDefault() const override;
  bool isPIEDefault() const override;
  bool isPICDefaultForced() const override;

  /// Set CodeView as the default debug info format. Users can use -gcodeview
  /// and -gdwarf to override the default.
  codegenoptions::DebugInfoFormat getDefaultDebugFormat() const override {
    return codegenoptions::DIF_CodeView;
  }

  /// Set the debugger tuning to "default", since we're definitely not tuning
  /// for GDB.
  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::Default;
  }

  enum class SubDirectoryType {
    Bin,
    Include,
    Lib,
  };
  std::string getSubDirectoryPath(SubDirectoryType Type,
                                  llvm::Triple::ArchType TargetArch) const;

  // Convenience overload.
  // Uses the current target arch.
  std::string getSubDirectoryPath(SubDirectoryType Type) const {
    return getSubDirectoryPath(Type, getArch());
  }

  enum class ToolsetLayout {
    OlderVS,
    VS2017OrNewer,
    DevDivInternal,
  };
  bool getIsVS2017OrNewer() const { return VSLayout == ToolsetLayout::VS2017OrNewer; }

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                          llvm::opt::ArgStringList &CC1Args) const override;

  bool getWindowsSDKLibraryPath(std::string &path) const;
  /// Check if Universal CRT should be used if available
  bool getUniversalCRTLibraryPath(std::string &path) const;
  bool useUniversalCRT() const;
  VersionTuple
  computeMSVCVersion(const Driver *D,
                     const llvm::opt::ArgList &Args) const override;

  std::string ComputeEffectiveClangTriple(const llvm::opt::ArgList &Args,
                                          types::ID InputType) const override;
  SanitizerMask getSupportedSanitizers() const override;

  void printVerboseInfo(raw_ostream &OS) const override;

  bool FoundMSVCInstall() const { return !VCToolChainPath.empty(); }

protected:
  void AddSystemIncludeWithSubfolder(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args,
                                     const std::string &folder,
                                     const Twine &subfolder1,
                                     const Twine &subfolder2 = "",
                                     const Twine &subfolder3 = "") const;

  Tool *buildLinker() const override;
  Tool *buildAssembler() const override;
private:
  std::string VCToolChainPath;
  ToolsetLayout VSLayout = ToolsetLayout::OlderVS;
  CudaInstallationDetector CudaInstallation;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H
