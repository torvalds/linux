//===--- Cuda.h - Cuda ToolChain Implementations ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_CUDA_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_CUDA_H

#include "clang/Basic/Cuda.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Multilib.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"
#include <set>
#include <vector>

namespace clang {
namespace driver {

/// A class to find a viable CUDA installation
class CudaInstallationDetector {
private:
  const Driver &D;
  bool IsValid = false;
  CudaVersion Version = CudaVersion::UNKNOWN;
  std::string InstallPath;
  std::string BinPath;
  std::string LibPath;
  std::string LibDevicePath;
  std::string IncludePath;
  llvm::StringMap<std::string> LibDeviceMap;

  // CUDA architectures for which we have raised an error in
  // CheckCudaVersionSupportsArch.
  mutable llvm::SmallSet<CudaArch, 4> ArchsWithBadVersion;

public:
  CudaInstallationDetector(const Driver &D, const llvm::Triple &HostTriple,
                           const llvm::opt::ArgList &Args);

  void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                          llvm::opt::ArgStringList &CC1Args) const;

  /// Emit an error if Version does not support the given Arch.
  ///
  /// If either Version or Arch is unknown, does not emit an error.  Emits at
  /// most one error per Arch.
  void CheckCudaVersionSupportsArch(CudaArch Arch) const;

  /// Check whether we detected a valid Cuda install.
  bool isValid() const { return IsValid; }
  /// Print information about the detected CUDA installation.
  void print(raw_ostream &OS) const;

  /// Get the detected Cuda install's version.
  CudaVersion version() const { return Version; }
  /// Get the detected Cuda installation path.
  StringRef getInstallPath() const { return InstallPath; }
  /// Get the detected path to Cuda's bin directory.
  StringRef getBinPath() const { return BinPath; }
  /// Get the detected Cuda Include path.
  StringRef getIncludePath() const { return IncludePath; }
  /// Get the detected Cuda library path.
  StringRef getLibPath() const { return LibPath; }
  /// Get the detected Cuda device library path.
  StringRef getLibDevicePath() const { return LibDevicePath; }
  /// Get libdevice file for given architecture
  std::string getLibDeviceFile(StringRef Gpu) const {
    return LibDeviceMap.lookup(Gpu);
  }
};

namespace tools {
namespace NVPTX {

// Run ptxas, the NVPTX assembler.
class LLVM_LIBRARY_VISIBILITY Assembler : public Tool {
 public:
   Assembler(const ToolChain &TC)
       : Tool("NVPTX::Assembler", "ptxas", TC, RF_Full, llvm::sys::WEM_UTF8,
              "--options-file") {}

   bool hasIntegratedCPP() const override { return false; }

   void ConstructJob(Compilation &C, const JobAction &JA,
                     const InputInfo &Output, const InputInfoList &Inputs,
                     const llvm::opt::ArgList &TCArgs,
                     const char *LinkingOutput) const override;
};

// Runs fatbinary, which combines GPU object files ("cubin" files) and/or PTX
// assembly into a single output file.
class LLVM_LIBRARY_VISIBILITY Linker : public Tool {
 public:
   Linker(const ToolChain &TC)
       : Tool("NVPTX::Linker", "fatbinary", TC, RF_Full, llvm::sys::WEM_UTF8,
              "--options-file") {}

   bool hasIntegratedCPP() const override { return false; }

   void ConstructJob(Compilation &C, const JobAction &JA,
                     const InputInfo &Output, const InputInfoList &Inputs,
                     const llvm::opt::ArgList &TCArgs,
                     const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY OpenMPLinker : public Tool {
 public:
   OpenMPLinker(const ToolChain &TC)
       : Tool("NVPTX::OpenMPLinker", "nvlink", TC, RF_Full, llvm::sys::WEM_UTF8,
              "--options-file") {}

   bool hasIntegratedCPP() const override { return false; }

   void ConstructJob(Compilation &C, const JobAction &JA,
                     const InputInfo &Output, const InputInfoList &Inputs,
                     const llvm::opt::ArgList &TCArgs,
                     const char *LinkingOutput) const override;
};

} // end namespace NVPTX
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY CudaToolChain : public ToolChain {
public:
  CudaToolChain(const Driver &D, const llvm::Triple &Triple,
                const ToolChain &HostTC, const llvm::opt::ArgList &Args,
                const Action::OffloadKind OK);

  const llvm::Triple *getAuxTriple() const override {
    return &HostTC.getTriple();
  }

  std::string getInputFilename(const InputInfo &Input) const override;

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;

  // Never try to use the integrated assembler with CUDA; always fork out to
  // ptxas.
  bool useIntegratedAs() const override { return false; }
  bool isCrossCompiling() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault() const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }
  bool supportsDebugInfoOption(const llvm::opt::Arg *A) const override;
  void adjustDebugInfoKind(codegenoptions::DebugInfoKind &DebugInfoKind,
                           const llvm::opt::ArgList &Args) const override;
  bool IsMathErrnoDefault() const override { return false; }

  void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                          llvm::opt::ArgStringList &CC1Args) const override;

  void addClangWarningOptions(llvm::opt::ArgStringList &CC1Args) const override;
  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &CC1Args) const override;
  void AddIAMCUIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                           llvm::opt::ArgStringList &CC1Args) const override;

  SanitizerMask getSupportedSanitizers() const override;

  VersionTuple
  computeMSVCVersion(const Driver *D,
                     const llvm::opt::ArgList &Args) const override;

  unsigned GetDefaultDwarfVersion() const override { return 2; }

  const ToolChain &HostTC;
  CudaInstallationDetector CudaInstallation;

protected:
  Tool *buildAssembler() const override;  // ptxas
  Tool *buildLinker() const override;     // fatbinary (ok, not really a linker)

private:
  const Action::OffloadKind OK;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_CUDA_H
