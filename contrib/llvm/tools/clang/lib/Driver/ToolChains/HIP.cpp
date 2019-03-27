//===--- HIP.cpp - HIP Tool and ToolChain Implementations -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "HIP.h"
#include "CommonArgs.h"
#include "InputInfo.h"
#include "clang/Basic/Cuda.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

#if _WIN32 || _WIN64
#define NULL_FILE "nul"
#else
#define NULL_FILE "/dev/null"
#endif

namespace {

static void addBCLib(Compilation &C, const ArgList &Args,
                     ArgStringList &CmdArgs, ArgStringList LibraryPaths,
                     StringRef BCName) {
  StringRef FullName;
  for (std::string LibraryPath : LibraryPaths) {
    SmallString<128> Path(LibraryPath);
    llvm::sys::path::append(Path, BCName);
    FullName = Path;
    if (llvm::sys::fs::exists(FullName)) {
      CmdArgs.push_back(Args.MakeArgString(FullName));
      return;
    }
  }
  C.getDriver().Diag(diag::err_drv_no_such_file) << BCName;
}

} // namespace

const char *AMDGCN::Linker::constructLLVMLinkCommand(
    Compilation &C, const JobAction &JA, const InputInfoList &Inputs,
    const ArgList &Args, StringRef SubArchName,
    StringRef OutputFilePrefix) const {
  ArgStringList CmdArgs;
  // Add the input bc's created by compile step.
  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  ArgStringList LibraryPaths;

  // Find in --hip-device-lib-path and HIP_LIBRARY_PATH.
  for (auto Path : Args.getAllArgValues(options::OPT_hip_device_lib_path_EQ))
    LibraryPaths.push_back(Args.MakeArgString(Path));

  addDirectoryList(Args, LibraryPaths, "-L", "HIP_DEVICE_LIB_PATH");

  llvm::SmallVector<std::string, 10> BCLibs;

  // Add bitcode library in --hip-device-lib.
  for (auto Lib : Args.getAllArgValues(options::OPT_hip_device_lib_EQ)) {
    BCLibs.push_back(Args.MakeArgString(Lib));
  }

  // If --hip-device-lib is not set, add the default bitcode libraries.
  if (BCLibs.empty()) {
    // Get the bc lib file name for ISA version. For example,
    // gfx803 => oclc_isa_version_803.amdgcn.bc.
    std::string ISAVerBC =
        "oclc_isa_version_" + SubArchName.drop_front(3).str() + ".amdgcn.bc";

    llvm::StringRef FlushDenormalControlBC;
    if (Args.hasArg(options::OPT_fcuda_flush_denormals_to_zero))
      FlushDenormalControlBC = "oclc_daz_opt_on.amdgcn.bc";
    else
      FlushDenormalControlBC = "oclc_daz_opt_off.amdgcn.bc";

    BCLibs.append({"hip.amdgcn.bc", "opencl.amdgcn.bc",
                   "ocml.amdgcn.bc", "ockl.amdgcn.bc",
                   "oclc_finite_only_off.amdgcn.bc",
                   FlushDenormalControlBC,
                   "oclc_correctly_rounded_sqrt_on.amdgcn.bc",
                   "oclc_unsafe_math_off.amdgcn.bc", ISAVerBC});
  }
  for (auto Lib : BCLibs)
    addBCLib(C, Args, CmdArgs, LibraryPaths, Lib);

  // Add an intermediate output file.
  CmdArgs.push_back("-o");
  std::string TmpName =
      C.getDriver().GetTemporaryPath(OutputFilePrefix.str() + "-linked", "bc");
  const char *OutputFileName =
      C.addTempFile(C.getArgs().MakeArgString(TmpName));
  CmdArgs.push_back(OutputFileName);
  SmallString<128> ExecPath(C.getDriver().Dir);
  llvm::sys::path::append(ExecPath, "llvm-link");
  const char *Exec = Args.MakeArgString(ExecPath);
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
  return OutputFileName;
}

const char *AMDGCN::Linker::constructOptCommand(
    Compilation &C, const JobAction &JA, const InputInfoList &Inputs,
    const llvm::opt::ArgList &Args, llvm::StringRef SubArchName,
    llvm::StringRef OutputFilePrefix, const char *InputFileName) const {
  // Construct opt command.
  ArgStringList OptArgs;
  // The input to opt is the output from llvm-link.
  OptArgs.push_back(InputFileName);
  // Pass optimization arg to opt.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    StringRef OOpt = "3";
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      OOpt = "3";
    else if (A->getOption().matches(options::OPT_O0))
      OOpt = "0";
    else if (A->getOption().matches(options::OPT_O)) {
      // -Os, -Oz, and -O(anything else) map to -O2
      OOpt = llvm::StringSwitch<const char *>(A->getValue())
                 .Case("1", "1")
                 .Case("2", "2")
                 .Case("3", "3")
                 .Case("s", "2")
                 .Case("z", "2")
                 .Default("2");
    }
    OptArgs.push_back(Args.MakeArgString("-O" + OOpt));
  }
  OptArgs.push_back("-mtriple=amdgcn-amd-amdhsa");
  OptArgs.push_back(Args.MakeArgString("-mcpu=" + SubArchName));
  OptArgs.push_back("-o");
  std::string TmpFileName = C.getDriver().GetTemporaryPath(
      OutputFilePrefix.str() + "-optimized", "bc");
  const char *OutputFileName =
      C.addTempFile(C.getArgs().MakeArgString(TmpFileName));
  OptArgs.push_back(OutputFileName);
  SmallString<128> OptPath(C.getDriver().Dir);
  llvm::sys::path::append(OptPath, "opt");
  const char *OptExec = Args.MakeArgString(OptPath);
  C.addCommand(llvm::make_unique<Command>(JA, *this, OptExec, OptArgs, Inputs));
  return OutputFileName;
}

const char *AMDGCN::Linker::constructLlcCommand(
    Compilation &C, const JobAction &JA, const InputInfoList &Inputs,
    const llvm::opt::ArgList &Args, llvm::StringRef SubArchName,
    llvm::StringRef OutputFilePrefix, const char *InputFileName) const {
  // Construct llc command.
  ArgStringList LlcArgs{InputFileName, "-mtriple=amdgcn-amd-amdhsa",
                        "-filetype=obj", "-mattr=-code-object-v3",
                        Args.MakeArgString("-mcpu=" + SubArchName), "-o"};
  std::string LlcOutputFileName =
      C.getDriver().GetTemporaryPath(OutputFilePrefix, "o");
  const char *LlcOutputFile =
      C.addTempFile(C.getArgs().MakeArgString(LlcOutputFileName));
  LlcArgs.push_back(LlcOutputFile);
  SmallString<128> LlcPath(C.getDriver().Dir);
  llvm::sys::path::append(LlcPath, "llc");
  const char *Llc = Args.MakeArgString(LlcPath);
  C.addCommand(llvm::make_unique<Command>(JA, *this, Llc, LlcArgs, Inputs));
  return LlcOutputFile;
}

void AMDGCN::Linker::constructLldCommand(Compilation &C, const JobAction &JA,
                                          const InputInfoList &Inputs,
                                          const InputInfo &Output,
                                          const llvm::opt::ArgList &Args,
                                          const char *InputFileName) const {
  // Construct lld command.
  // The output from ld.lld is an HSA code object file.
  ArgStringList LldArgs{"-flavor",    "gnu", "--no-undefined",
                        "-shared",    "-o",  Output.getFilename(),
                        InputFileName};
  SmallString<128> LldPath(C.getDriver().Dir);
  llvm::sys::path::append(LldPath, "lld");
  const char *Lld = Args.MakeArgString(LldPath);
  C.addCommand(llvm::make_unique<Command>(JA, *this, Lld, LldArgs, Inputs));
}

// Construct a clang-offload-bundler command to bundle code objects for
// different GPU's into a HIP fat binary.
void AMDGCN::constructHIPFatbinCommand(Compilation &C, const JobAction &JA,
                  StringRef OutputFileName, const InputInfoList &Inputs,
                  const llvm::opt::ArgList &Args, const Tool& T) {
  // Construct clang-offload-bundler command to bundle object files for
  // for different GPU archs.
  ArgStringList BundlerArgs;
  BundlerArgs.push_back(Args.MakeArgString("-type=o"));

  // ToDo: Remove the dummy host binary entry which is required by
  // clang-offload-bundler.
  std::string BundlerTargetArg = "-targets=host-x86_64-unknown-linux";
  std::string BundlerInputArg = "-inputs=" NULL_FILE;

  for (const auto &II : Inputs) {
    const auto* A = II.getAction();
    BundlerTargetArg = BundlerTargetArg + ",hip-amdgcn-amd-amdhsa-" +
                       StringRef(A->getOffloadingArch()).str();
    BundlerInputArg = BundlerInputArg + "," + II.getFilename();
  }
  BundlerArgs.push_back(Args.MakeArgString(BundlerTargetArg));
  BundlerArgs.push_back(Args.MakeArgString(BundlerInputArg));

  auto BundlerOutputArg =
      Args.MakeArgString(std::string("-outputs=").append(OutputFileName));
  BundlerArgs.push_back(BundlerOutputArg);

  SmallString<128> BundlerPath(C.getDriver().Dir);
  llvm::sys::path::append(BundlerPath, "clang-offload-bundler");
  const char *Bundler = Args.MakeArgString(BundlerPath);
  C.addCommand(llvm::make_unique<Command>(JA, T, Bundler, BundlerArgs, Inputs));
}

// For amdgcn the inputs of the linker job are device bitcode and output is
// object file. It calls llvm-link, opt, llc, then lld steps.
void AMDGCN::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {

  if (JA.getType() == types::TY_HIP_FATBIN)
    return constructHIPFatbinCommand(C, JA, Output.getFilename(), Inputs, Args, *this);

  assert(getToolChain().getTriple().getArch() == llvm::Triple::amdgcn &&
         "Unsupported target");

  std::string SubArchName = JA.getOffloadingArch();
  assert(StringRef(SubArchName).startswith("gfx") && "Unsupported sub arch");

  // Prefix for temporary file name.
  std::string Prefix =
      llvm::sys::path::stem(Inputs[0].getFilename()).str() + "-" + SubArchName;

  // Each command outputs different files.
  const char *LLVMLinkCommand =
      constructLLVMLinkCommand(C, JA, Inputs, Args, SubArchName, Prefix);
  const char *OptCommand = constructOptCommand(C, JA, Inputs, Args, SubArchName,
                                               Prefix, LLVMLinkCommand);
  const char *LlcCommand =
      constructLlcCommand(C, JA, Inputs, Args, SubArchName, Prefix, OptCommand);
  constructLldCommand(C, JA, Inputs, Output, Args, LlcCommand);
}

HIPToolChain::HIPToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ToolChain &HostTC, const ArgList &Args)
    : ToolChain(D, Triple, Args), HostTC(HostTC) {
  // Lookup binaries into the driver directory, this is used to
  // discover the clang-offload-bundler executable.
  getProgramPaths().push_back(getDriver().Dir);
}

void HIPToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  HostTC.addClangTargetOptions(DriverArgs, CC1Args, DeviceOffloadingKind);

  StringRef GpuArch = DriverArgs.getLastArgValue(options::OPT_march_EQ);
  assert(!GpuArch.empty() && "Must have an explicit GPU arch.");
  (void) GpuArch;
  assert(DeviceOffloadingKind == Action::OFK_HIP &&
         "Only HIP offloading kinds are supported for GPUs.");

  CC1Args.push_back("-target-cpu");
  CC1Args.push_back(DriverArgs.MakeArgStringRef(GpuArch));
  CC1Args.push_back("-fcuda-is-device");

  if (DriverArgs.hasFlag(options::OPT_fcuda_flush_denormals_to_zero,
                         options::OPT_fno_cuda_flush_denormals_to_zero, false))
    CC1Args.push_back("-fcuda-flush-denormals-to-zero");

  if (DriverArgs.hasFlag(options::OPT_fcuda_approx_transcendentals,
                         options::OPT_fno_cuda_approx_transcendentals, false))
    CC1Args.push_back("-fcuda-approx-transcendentals");

  if (DriverArgs.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                         false))
    CC1Args.push_back("-fgpu-rdc");

  // Default to "hidden" visibility, as object level linking will not be
  // supported for the foreseeable future.
  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat))
    CC1Args.append({"-fvisibility", "hidden"});
}

llvm::opt::DerivedArgList *
HIPToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                             StringRef BoundArch,
                             Action::OffloadKind DeviceOffloadKind) const {
  DerivedArgList *DAL =
      HostTC.TranslateArgs(Args, BoundArch, DeviceOffloadKind);
  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_Xarch__)) {
      // Skip this argument unless the architecture matches BoundArch.
      if (BoundArch.empty() || A->getValue(0) != BoundArch)
        continue;

      unsigned Index = Args.getBaseArgs().MakeIndex(A->getValue(1));
      unsigned Prev = Index;
      std::unique_ptr<Arg> XarchArg(Opts.ParseOneArg(Args, Index));

      // If the argument parsing failed or more than one argument was
      // consumed, the -Xarch_ argument's parameter tried to consume
      // extra arguments. Emit an error and ignore.
      //
      // We also want to disallow any options which would alter the
      // driver behavior; that isn't going to work in our model. We
      // use isDriverOption() as an approximation, although things
      // like -O4 are going to slip through.
      if (!XarchArg || Index > Prev + 1) {
        getDriver().Diag(diag::err_drv_invalid_Xarch_argument_with_args)
            << A->getAsString(Args);
        continue;
      } else if (XarchArg->getOption().hasFlag(options::DriverOption)) {
        getDriver().Diag(diag::err_drv_invalid_Xarch_argument_isdriver)
            << A->getAsString(Args);
        continue;
      }
      XarchArg->setBaseArg(A);
      A = XarchArg.release();
      DAL->AddSynthesizedArg(A);
    }
    DAL->append(A);
  }

  if (!BoundArch.empty()) {
    DAL->eraseArg(options::OPT_march_EQ);
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ), BoundArch);
  }

  return DAL;
}

Tool *HIPToolChain::buildLinker() const {
  assert(getTriple().getArch() == llvm::Triple::amdgcn);
  return new tools::AMDGCN::Linker(*this);
}

void HIPToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  HostTC.addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
HIPToolChain::GetCXXStdlibType(const ArgList &Args) const {
  return HostTC.GetCXXStdlibType(Args);
}

void HIPToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  HostTC.AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}

void HIPToolChain::AddClangCXXStdlibIncludeArgs(const ArgList &Args,
                                                 ArgStringList &CC1Args) const {
  HostTC.AddClangCXXStdlibIncludeArgs(Args, CC1Args);
}

void HIPToolChain::AddIAMCUIncludeArgs(const ArgList &Args,
                                        ArgStringList &CC1Args) const {
  HostTC.AddIAMCUIncludeArgs(Args, CC1Args);
}

SanitizerMask HIPToolChain::getSupportedSanitizers() const {
  // The HIPToolChain only supports sanitizers in the sense that it allows
  // sanitizer arguments on the command line if they are supported by the host
  // toolchain. The HIPToolChain will actually ignore any command line
  // arguments for any of these "supported" sanitizers. That means that no
  // sanitization of device code is actually supported at this time.
  //
  // This behavior is necessary because the host and device toolchains
  // invocations often share the command line, so the device toolchain must
  // tolerate flags meant only for the host toolchain.
  return HostTC.getSupportedSanitizers();
}

VersionTuple HIPToolChain::computeMSVCVersion(const Driver *D,
                                               const ArgList &Args) const {
  return HostTC.computeMSVCVersion(D, Args);
}
