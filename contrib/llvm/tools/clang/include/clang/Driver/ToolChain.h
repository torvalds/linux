//===- ToolChain.h - Collections of tools for one platform ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_TOOLCHAIN_H
#define LLVM_CLANG_DRIVER_TOOLCHAIN_H

#include "clang/Basic/DebugInfoOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Multilib.h"
#include "clang/Driver/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Target/TargetOptions.h"
#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace llvm {
namespace opt {

class Arg;
class ArgList;
class DerivedArgList;

} // namespace opt
namespace vfs {

class FileSystem;

} // namespace vfs
} // namespace llvm

namespace clang {

class ObjCRuntime;

namespace driver {

class Driver;
class InputInfo;
class SanitizerArgs;
class Tool;
class XRayArgs;

/// Helper structure used to pass information extracted from clang executable
/// name such as `i686-linux-android-g++`.
struct ParsedClangName {
  /// Target part of the executable name, as `i686-linux-android`.
  std::string TargetPrefix;

  /// Driver mode part of the executable name, as `g++`.
  std::string ModeSuffix;

  /// Corresponding driver mode argument, as '--driver-mode=g++'
  const char *DriverMode = nullptr;

  /// True if TargetPrefix is recognized as a registered target name.
  bool TargetIsValid = false;

  ParsedClangName() = default;
  ParsedClangName(std::string Suffix, const char *Mode)
      : ModeSuffix(Suffix), DriverMode(Mode) {}
  ParsedClangName(std::string Target, std::string Suffix, const char *Mode,
                  bool IsRegistered)
      : TargetPrefix(Target), ModeSuffix(Suffix), DriverMode(Mode),
        TargetIsValid(IsRegistered) {}

  bool isEmpty() const {
    return TargetPrefix.empty() && ModeSuffix.empty() && DriverMode == nullptr;
  }
};

/// ToolChain - Access to tools for a single platform.
class ToolChain {
public:
  using path_list = SmallVector<std::string, 16>;

  enum CXXStdlibType {
    CST_Libcxx,
    CST_Libstdcxx
  };

  enum RuntimeLibType {
    RLT_CompilerRT,
    RLT_Libgcc
  };

  enum RTTIMode {
    RM_Enabled,
    RM_Disabled,
  };

private:
  friend class RegisterEffectiveTriple;

  const Driver &D;
  llvm::Triple Triple;
  const llvm::opt::ArgList &Args;

  // We need to initialize CachedRTTIArg before CachedRTTIMode
  const llvm::opt::Arg *const CachedRTTIArg;

  const RTTIMode CachedRTTIMode;

  /// The list of toolchain specific path prefixes to search for libraries.
  path_list LibraryPaths;

  /// The list of toolchain specific path prefixes to search for files.
  path_list FilePaths;

  /// The list of toolchain specific path prefixes to search for programs.
  path_list ProgramPaths;

  mutable std::unique_ptr<Tool> Clang;
  mutable std::unique_ptr<Tool> Assemble;
  mutable std::unique_ptr<Tool> Link;
  mutable std::unique_ptr<Tool> OffloadBundler;

  Tool *getClang() const;
  Tool *getAssemble() const;
  Tool *getLink() const;
  Tool *getClangAs() const;
  Tool *getOffloadBundler() const;

  mutable std::unique_ptr<SanitizerArgs> SanitizerArguments;
  mutable std::unique_ptr<XRayArgs> XRayArguments;

  /// The effective clang triple for the current Job.
  mutable llvm::Triple EffectiveTriple;

  /// Set the toolchain's effective clang triple.
  void setEffectiveTriple(llvm::Triple ET) const {
    EffectiveTriple = std::move(ET);
  }

protected:
  MultilibSet Multilibs;
  Multilib SelectedMultilib;

  ToolChain(const Driver &D, const llvm::Triple &T,
            const llvm::opt::ArgList &Args);

  void setTripleEnvironment(llvm::Triple::EnvironmentType Env);

  virtual Tool *buildAssembler() const;
  virtual Tool *buildLinker() const;
  virtual Tool *getTool(Action::ActionClass AC) const;

  /// \name Utilities for implementing subclasses.
  ///@{
  static void addSystemInclude(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args,
                               const Twine &Path);
  static void addExternCSystemInclude(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args,
                                      const Twine &Path);
  static void
      addExternCSystemIncludeIfExists(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args,
                                      const Twine &Path);
  static void addSystemIncludes(const llvm::opt::ArgList &DriverArgs,
                                llvm::opt::ArgStringList &CC1Args,
                                ArrayRef<StringRef> Paths);
  ///@}

public:
  virtual ~ToolChain();

  // Accessors

  const Driver &getDriver() const { return D; }
  llvm::vfs::FileSystem &getVFS() const;
  const llvm::Triple &getTriple() const { return Triple; }

  /// Get the toolchain's aux triple, if it has one.
  ///
  /// Exactly what the aux triple represents depends on the toolchain, but for
  /// example when compiling CUDA code for the GPU, the triple might be NVPTX,
  /// while the aux triple is the host (CPU) toolchain, e.g. x86-linux-gnu.
  virtual const llvm::Triple *getAuxTriple() const { return nullptr; }

  /// Some toolchains need to modify the file name, for example to replace the
  /// extension for object files with .cubin for OpenMP offloading to Nvidia
  /// GPUs.
  virtual std::string getInputFilename(const InputInfo &Input) const;

  llvm::Triple::ArchType getArch() const { return Triple.getArch(); }
  StringRef getArchName() const { return Triple.getArchName(); }
  StringRef getPlatform() const { return Triple.getVendorName(); }
  StringRef getOS() const { return Triple.getOSName(); }

  /// Provide the default architecture name (as expected by -arch) for
  /// this toolchain.
  StringRef getDefaultUniversalArchName() const;

  std::string getTripleString() const {
    return Triple.getTriple();
  }

  /// Get the toolchain's effective clang triple.
  const llvm::Triple &getEffectiveTriple() const {
    assert(!EffectiveTriple.getTriple().empty() && "No effective triple");
    return EffectiveTriple;
  }

  path_list &getLibraryPaths() { return LibraryPaths; }
  const path_list &getLibraryPaths() const { return LibraryPaths; }

  path_list &getFilePaths() { return FilePaths; }
  const path_list &getFilePaths() const { return FilePaths; }

  path_list &getProgramPaths() { return ProgramPaths; }
  const path_list &getProgramPaths() const { return ProgramPaths; }

  const MultilibSet &getMultilibs() const { return Multilibs; }

  const Multilib &getMultilib() const { return SelectedMultilib; }

  const SanitizerArgs& getSanitizerArgs() const;

  const XRayArgs& getXRayArgs() const;

  // Returns the Arg * that explicitly turned on/off rtti, or nullptr.
  const llvm::opt::Arg *getRTTIArg() const { return CachedRTTIArg; }

  // Returns the RTTIMode for the toolchain with the current arguments.
  RTTIMode getRTTIMode() const { return CachedRTTIMode; }

  /// Return any implicit target and/or mode flag for an invocation of
  /// the compiler driver as `ProgName`.
  ///
  /// For example, when called with i686-linux-android-g++, the first element
  /// of the return value will be set to `"i686-linux-android"` and the second
  /// will be set to "--driver-mode=g++"`.
  /// It is OK if the target name is not registered. In this case the return
  /// value contains false in the field TargetIsValid.
  ///
  /// \pre `llvm::InitializeAllTargets()` has been called.
  /// \param ProgName The name the Clang driver was invoked with (from,
  /// e.g., argv[0]).
  /// \return A structure of type ParsedClangName that contains the executable
  /// name parts.
  static ParsedClangName getTargetAndModeFromProgramName(StringRef ProgName);

  // Tool access.

  /// TranslateArgs - Create a new derived argument list for any argument
  /// translations this ToolChain may wish to perform, or 0 if no tool chain
  /// specific translations are needed. If \p DeviceOffloadKind is specified
  /// the translation specific for that offload kind is performed.
  ///
  /// \param BoundArch - The bound architecture name, or 0.
  /// \param DeviceOffloadKind - The device offload kind used for the
  /// translation.
  virtual llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const {
    return nullptr;
  }

  /// TranslateOpenMPTargetArgs - Create a new derived argument list for
  /// that contains the OpenMP target specific flags passed via
  /// -Xopenmp-target -opt=val OR -Xopenmp-target=<triple> -opt=val
  virtual llvm::opt::DerivedArgList *TranslateOpenMPTargetArgs(
      const llvm::opt::DerivedArgList &Args, bool SameTripleAsHost,
      SmallVectorImpl<llvm::opt::Arg *> &AllocatedArgs) const;

  /// Choose a tool to use to handle the action \p JA.
  ///
  /// This can be overridden when a particular ToolChain needs to use
  /// a compiler other than Clang.
  virtual Tool *SelectTool(const JobAction &JA) const;

  // Helper methods

  std::string GetFilePath(const char *Name) const;
  std::string GetProgramPath(const char *Name) const;

  /// Returns the linker path, respecting the -fuse-ld= argument to determine
  /// the linker suffix or name.
  std::string GetLinkerPath() const;

  /// Dispatch to the specific toolchain for verbose printing.
  ///
  /// This is used when handling the verbose option to print detailed,
  /// toolchain-specific information useful for understanding the behavior of
  /// the driver on a specific platform.
  virtual void printVerboseInfo(raw_ostream &OS) const {}

  // Platform defaults information

  /// Returns true if the toolchain is targeting a non-native
  /// architecture.
  virtual bool isCrossCompiling() const;

  /// HasNativeLTOLinker - Check whether the linker and related tools have
  /// native LLVM support.
  virtual bool HasNativeLLVMSupport() const;

  /// LookupTypeForExtension - Return the default language type to use for the
  /// given extension.
  virtual types::ID LookupTypeForExtension(StringRef Ext) const;

  /// IsBlocksDefault - Does this tool chain enable -fblocks by default.
  virtual bool IsBlocksDefault() const { return false; }

  /// IsIntegratedAssemblerDefault - Does this tool chain enable -integrated-as
  /// by default.
  virtual bool IsIntegratedAssemblerDefault() const { return false; }

  /// Check if the toolchain should use the integrated assembler.
  virtual bool useIntegratedAs() const;

  /// IsMathErrnoDefault - Does this tool chain use -fmath-errno by default.
  virtual bool IsMathErrnoDefault() const { return true; }

  /// IsEncodeExtendedBlockSignatureDefault - Does this tool chain enable
  /// -fencode-extended-block-signature by default.
  virtual bool IsEncodeExtendedBlockSignatureDefault() const { return false; }

  /// IsObjCNonFragileABIDefault - Does this tool chain set
  /// -fobjc-nonfragile-abi by default.
  virtual bool IsObjCNonFragileABIDefault() const { return false; }

  /// UseObjCMixedDispatchDefault - When using non-legacy dispatch, should the
  /// mixed dispatch method be used?
  virtual bool UseObjCMixedDispatch() const { return false; }

  /// Check whether to enable x86 relax relocations by default.
  virtual bool useRelaxRelocations() const;

  /// GetDefaultStackProtectorLevel - Get the default stack protector level for
  /// this tool chain (0=off, 1=on, 2=strong, 3=all).
  virtual unsigned GetDefaultStackProtectorLevel(bool KernelOrKext) const {
    return 0;
  }

  /// Get the default trivial automatic variable initialization.
  virtual LangOptions::TrivialAutoVarInitKind
  GetDefaultTrivialAutoVarInit() const {
    return LangOptions::TrivialAutoVarInitKind::Uninitialized;
  }

  /// GetDefaultLinker - Get the default linker to use.
  virtual const char *getDefaultLinker() const { return "ld"; }

  /// GetDefaultRuntimeLibType - Get the default runtime library variant to use.
  virtual RuntimeLibType GetDefaultRuntimeLibType() const {
    return ToolChain::RLT_Libgcc;
  }

  virtual CXXStdlibType GetDefaultCXXStdlibType() const {
    return ToolChain::CST_Libstdcxx;
  }

  virtual std::string getCompilerRTPath() const;

  virtual std::string getCompilerRT(const llvm::opt::ArgList &Args,
                                    StringRef Component,
                                    bool Shared = false) const;

  const char *getCompilerRTArgString(const llvm::opt::ArgList &Args,
                                     StringRef Component,
                                     bool Shared = false) const;

  // Returns <ResourceDir>/lib/<OSName>/<arch>.  This is used by runtimes (such
  // as OpenMP) to find arch-specific libraries.
  std::string getArchSpecificLibPath() const;

  // Returns <OSname> part of above.
  StringRef getOSLibName() const;

  /// needsProfileRT - returns true if instrumentation profile is on.
  static bool needsProfileRT(const llvm::opt::ArgList &Args);

  /// Returns true if gcov instrumentation (-fprofile-arcs or --coverage) is on.
  static bool needsGCovInstrumentation(const llvm::opt::ArgList &Args);

  /// IsUnwindTablesDefault - Does this tool chain use -funwind-tables
  /// by default.
  virtual bool IsUnwindTablesDefault(const llvm::opt::ArgList &Args) const;

  /// Test whether this toolchain defaults to PIC.
  virtual bool isPICDefault() const = 0;

  /// Test whether this toolchain defaults to PIE.
  virtual bool isPIEDefault() const = 0;

  /// Tests whether this toolchain forces its default for PIC, PIE or
  /// non-PIC.  If this returns true, any PIC related flags should be ignored
  /// and instead the results of \c isPICDefault() and \c isPIEDefault() are
  /// used exclusively.
  virtual bool isPICDefaultForced() const = 0;

  /// SupportsProfiling - Does this tool chain support -pg.
  virtual bool SupportsProfiling() const { return true; }

  /// Complain if this tool chain doesn't support Objective-C ARC.
  virtual void CheckObjCARC() const {}

  /// Get the default debug info format. Typically, this is DWARF.
  virtual codegenoptions::DebugInfoFormat getDefaultDebugFormat() const {
    return codegenoptions::DIF_DWARF;
  }

  /// UseDwarfDebugFlags - Embed the compile options to clang into the Dwarf
  /// compile unit information.
  virtual bool UseDwarfDebugFlags() const { return false; }

  // Return the DWARF version to emit, in the absence of arguments
  // to the contrary.
  virtual unsigned GetDefaultDwarfVersion() const { return 4; }

  // True if the driver should assume "-fstandalone-debug"
  // in the absence of an option specifying otherwise,
  // provided that debugging was requested in the first place.
  // i.e. a value of 'true' does not imply that debugging is wanted.
  virtual bool GetDefaultStandaloneDebug() const { return false; }

  // Return the default debugger "tuning."
  virtual llvm::DebuggerKind getDefaultDebuggerTuning() const {
    return llvm::DebuggerKind::GDB;
  }

  /// Does this toolchain supports given debug info option or not.
  virtual bool supportsDebugInfoOption(const llvm::opt::Arg *) const {
    return true;
  }

  /// Adjust debug information kind considering all passed options.
  virtual void adjustDebugInfoKind(codegenoptions::DebugInfoKind &DebugInfoKind,
                                   const llvm::opt::ArgList &Args) const {}

  /// GetExceptionModel - Return the tool chain exception model.
  virtual llvm::ExceptionHandling
  GetExceptionModel(const llvm::opt::ArgList &Args) const;

  /// SupportsEmbeddedBitcode - Does this tool chain support embedded bitcode.
  virtual bool SupportsEmbeddedBitcode() const { return false; }

  /// getThreadModel() - Which thread model does this target use?
  virtual std::string getThreadModel() const { return "posix"; }

  /// isThreadModelSupported() - Does this target support a thread model?
  virtual bool isThreadModelSupported(const StringRef Model) const;

  /// ComputeLLVMTriple - Return the LLVM target triple to use, after taking
  /// command line arguments into account.
  virtual std::string
  ComputeLLVMTriple(const llvm::opt::ArgList &Args,
                    types::ID InputType = types::TY_INVALID) const;

  /// ComputeEffectiveClangTriple - Return the Clang triple to use for this
  /// target, which may take into account the command line arguments. For
  /// example, on Darwin the -mmacosx-version-min= command line argument (which
  /// sets the deployment target) determines the version in the triple passed to
  /// Clang.
  virtual std::string ComputeEffectiveClangTriple(
      const llvm::opt::ArgList &Args,
      types::ID InputType = types::TY_INVALID) const;

  /// getDefaultObjCRuntime - Return the default Objective-C runtime
  /// for this platform.
  ///
  /// FIXME: this really belongs on some sort of DeploymentTarget abstraction
  virtual ObjCRuntime getDefaultObjCRuntime(bool isNonFragile) const;

  /// hasBlocksRuntime - Given that the user is compiling with
  /// -fblocks, does this tool chain guarantee the existence of a
  /// blocks runtime?
  ///
  /// FIXME: this really belongs on some sort of DeploymentTarget abstraction
  virtual bool hasBlocksRuntime() const { return true; }

  /// Add the clang cc1 arguments for system include paths.
  ///
  /// This routine is responsible for adding the necessary cc1 arguments to
  /// include headers from standard system header directories.
  virtual void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const;

  /// Add options that need to be passed to cc1 for this target.
  virtual void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args,
                                     Action::OffloadKind DeviceOffloadKind) const;

  /// Add warning options that need to be passed to cc1 for this target.
  virtual void addClangWarningOptions(llvm::opt::ArgStringList &CC1Args) const;

  // GetRuntimeLibType - Determine the runtime library type to use with the
  // given compilation arguments.
  virtual RuntimeLibType
  GetRuntimeLibType(const llvm::opt::ArgList &Args) const;

  // GetCXXStdlibType - Determine the C++ standard library type to use with the
  // given compilation arguments.
  virtual CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const;

  /// AddClangCXXStdlibIncludeArgs - Add the clang -cc1 level arguments to set
  /// the include paths to use for the given C++ standard library type.
  virtual void
  AddClangCXXStdlibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args) const;

  /// Returns if the C++ standard library should be linked in.
  /// Note that e.g. -lm should still be linked even if this returns false.
  bool ShouldLinkCXXStdlib(const llvm::opt::ArgList &Args) const;

  /// AddCXXStdlibLibArgs - Add the system specific linker arguments to use
  /// for the given C++ standard library type.
  virtual void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                                   llvm::opt::ArgStringList &CmdArgs) const;

  /// AddFilePathLibArgs - Add each thing in getFilePaths() as a "-L" option.
  void AddFilePathLibArgs(const llvm::opt::ArgList &Args,
                          llvm::opt::ArgStringList &CmdArgs) const;

  /// AddCCKextLibArgs - Add the system specific linker arguments to use
  /// for kernel extensions (Darwin-specific).
  virtual void AddCCKextLibArgs(const llvm::opt::ArgList &Args,
                                llvm::opt::ArgStringList &CmdArgs) const;

  /// AddFastMathRuntimeIfAvailable - If a runtime library exists that sets
  /// global flags for unsafe floating point math, add it and return true.
  ///
  /// This checks for presence of the -Ofast, -ffast-math or -funsafe-math flags.
  virtual bool AddFastMathRuntimeIfAvailable(
      const llvm::opt::ArgList &Args, llvm::opt::ArgStringList &CmdArgs) const;

  /// addProfileRTLibs - When -fprofile-instr-profile is specified, try to pass
  /// a suitable profile runtime library to the linker.
  virtual void addProfileRTLibs(const llvm::opt::ArgList &Args,
                                llvm::opt::ArgStringList &CmdArgs) const;

  /// Add arguments to use system-specific CUDA includes.
  virtual void AddCudaIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                  llvm::opt::ArgStringList &CC1Args) const;

  /// Add arguments to use MCU GCC toolchain includes.
  virtual void AddIAMCUIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args) const;

  /// On Windows, returns the MSVC compatibility version.
  virtual VersionTuple computeMSVCVersion(const Driver *D,
                                          const llvm::opt::ArgList &Args) const;

  /// Return sanitizers which are available in this toolchain.
  virtual SanitizerMask getSupportedSanitizers() const;

  /// Return sanitizers which are enabled by default.
  virtual SanitizerMask getDefaultSanitizers() const { return 0; }
};

/// Set a ToolChain's effective triple. Reset it when the registration object
/// is destroyed.
class RegisterEffectiveTriple {
  const ToolChain &TC;

public:
  RegisterEffectiveTriple(const ToolChain &TC, llvm::Triple T) : TC(TC) {
    TC.setEffectiveTriple(std::move(T));
  }

  ~RegisterEffectiveTriple() { TC.setEffectiveTriple(llvm::Triple()); }
};

} // namespace driver

} // namespace clang

#endif // LLVM_CLANG_DRIVER_TOOLCHAIN_H
