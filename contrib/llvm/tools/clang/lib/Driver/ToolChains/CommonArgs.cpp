//===--- CommonArgs.cpp - Args handling for multiple toolchains -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommonArgs.h"
#include "Arch/AArch64.h"
#include "Arch/ARM.h"
#include "Arch/Mips.h"
#include "Arch/PPC.h"
#include "Arch/SystemZ.h"
#include "Arch/X86.h"
#include "HIP.h"
#include "Hexagon.h"
#include "InputInfo.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Util.h"
#include "clang/Driver/XRayArgs.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLParser.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

void tools::addPathIfExists(const Driver &D, const Twine &Path,
                            ToolChain::path_list &Paths) {
  if (D.getVFS().exists(Path))
    Paths.push_back(Path.str());
}

void tools::handleTargetFeaturesGroup(const ArgList &Args,
                                      std::vector<StringRef> &Features,
                                      OptSpecifier Group) {
  for (const Arg *A : Args.filtered(Group)) {
    StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.startswith("m") && "Invalid feature name.");
    Name = Name.substr(1);

    bool IsNegative = Name.startswith("no-");
    if (IsNegative)
      Name = Name.substr(3);
    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }
}

void tools::addDirectoryList(const ArgList &Args, ArgStringList &CmdArgs,
                             const char *ArgName, const char *EnvVar) {
  const char *DirList = ::getenv(EnvVar);
  bool CombinedArg = false;

  if (!DirList)
    return; // Nothing to do.

  StringRef Name(ArgName);
  if (Name.equals("-I") || Name.equals("-L"))
    CombinedArg = true;

  StringRef Dirs(DirList);
  if (Dirs.empty()) // Empty string should not add '.'.
    return;

  StringRef::size_type Delim;
  while ((Delim = Dirs.find(llvm::sys::EnvPathSeparator)) != StringRef::npos) {
    if (Delim == 0) { // Leading colon.
      if (CombinedArg) {
        CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(".");
      }
    } else {
      if (CombinedArg) {
        CmdArgs.push_back(
            Args.MakeArgString(std::string(ArgName) + Dirs.substr(0, Delim)));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(Args.MakeArgString(Dirs.substr(0, Delim)));
      }
    }
    Dirs = Dirs.substr(Delim + 1);
  }

  if (Dirs.empty()) { // Trailing colon.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(".");
    }
  } else { // Add the last path.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + Dirs));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(Args.MakeArgString(Dirs));
    }
  }
}

void tools::AddLinkerInputs(const ToolChain &TC, const InputInfoList &Inputs,
                            const ArgList &Args, ArgStringList &CmdArgs,
                            const JobAction &JA) {
  const Driver &D = TC.getDriver();

  // Add extra linker input arguments which are not treated as inputs
  // (constructed via -Xarch_).
  Args.AddAllArgValues(CmdArgs, options::OPT_Zlinker_input);

  for (const auto &II : Inputs) {
    // If the current tool chain refers to an OpenMP or HIP offloading host, we
    // should ignore inputs that refer to OpenMP or HIP offloading devices -
    // they will be embedded according to a proper linker script.
    if (auto *IA = II.getAction())
      if ((JA.isHostOffloading(Action::OFK_OpenMP) &&
           IA->isDeviceOffloading(Action::OFK_OpenMP)) ||
          (JA.isHostOffloading(Action::OFK_HIP) &&
           IA->isDeviceOffloading(Action::OFK_HIP)))
        continue;

    if (!TC.HasNativeLLVMSupport() && types::isLLVMIR(II.getType()))
      // Don't try to pass LLVM inputs unless we have native support.
      D.Diag(diag::err_drv_no_linker_llvm_support) << TC.getTripleString();

    // Add filenames immediately.
    if (II.isFilename()) {
      CmdArgs.push_back(II.getFilename());
      continue;
    }

    // Otherwise, this is a linker input argument.
    const Arg &A = II.getInputArg();

    // Handle reserved library options.
    if (A.getOption().matches(options::OPT_Z_reserved_lib_stdcxx))
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
    else if (A.getOption().matches(options::OPT_Z_reserved_lib_cckext))
      TC.AddCCKextLibArgs(Args, CmdArgs);
    else if (A.getOption().matches(options::OPT_z)) {
      // Pass -z prefix for gcc linker compatibility.
      A.claim();
      A.render(Args, CmdArgs);
    } else {
      A.renderAsInput(Args, CmdArgs);
    }
  }

  // LIBRARY_PATH - included following the user specified library paths.
  //                and only supported on native toolchains.
  if (!TC.isCrossCompiling()) {
    addDirectoryList(Args, CmdArgs, "-L", "LIBRARY_PATH");
  }
}

void tools::AddTargetFeature(const ArgList &Args,
                             std::vector<StringRef> &Features,
                             OptSpecifier OnOpt, OptSpecifier OffOpt,
                             StringRef FeatureName) {
  if (Arg *A = Args.getLastArg(OnOpt, OffOpt)) {
    if (A->getOption().matches(OnOpt))
      Features.push_back(Args.MakeArgString("+" + FeatureName));
    else
      Features.push_back(Args.MakeArgString("-" + FeatureName));
  }
}

/// Get the (LLVM) name of the R600 gpu we are targeting.
static std::string getR600TargetGPU(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    const char *GPUName = A->getValue();
    return llvm::StringSwitch<const char *>(GPUName)
        .Cases("rv630", "rv635", "r600")
        .Cases("rv610", "rv620", "rs780", "rs880")
        .Case("rv740", "rv770")
        .Case("palm", "cedar")
        .Cases("sumo", "sumo2", "sumo")
        .Case("hemlock", "cypress")
        .Case("aruba", "cayman")
        .Default(GPUName);
  }
  return "";
}

static std::string getLanaiTargetCPU(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    return A->getValue();
  }
  return "";
}

/// Get the (LLVM) name of the WebAssembly cpu we are targeting.
static StringRef getWebAssemblyTargetCPU(const ArgList &Args) {
  // If we have -mcpu=, use that.
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPU = A->getValue();

#ifdef __wasm__
    // Handle "native" by examining the host. "native" isn't meaningful when
    // cross compiling, so only support this when the host is also WebAssembly.
    if (CPU == "native")
      return llvm::sys::getHostCPUName();
#endif

    return CPU;
  }

  return "generic";
}

std::string tools::getCPUName(const ArgList &Args, const llvm::Triple &T,
                              bool FromAs) {
  Arg *A;

  switch (T.getArch()) {
  default:
    return "";

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    return aarch64::getAArch64TargetCPU(Args, T, A);

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    StringRef MArch, MCPU;
    arm::getARMArchCPUFromArgs(Args, MArch, MCPU, FromAs);
    return arm::getARMTargetCPU(MCPU, MArch, T);
  }

  case llvm::Triple::avr:
    if (const Arg *A = Args.getLastArg(options::OPT_mmcu_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, T, CPUName, ABIName);
    return CPUName;
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le: {
    std::string TargetCPUName = ppc::getPPCTargetCPU(Args);
    // LLVM may default to generating code for the native CPU,
    // but, like gcc, we default to a more generic option for
    // each architecture. (except on Darwin)
    if (TargetCPUName.empty() && !T.isOSDarwin()) {
      if (T.getArch() == llvm::Triple::ppc64)
        TargetCPUName = "ppc64";
      else if (T.getArch() == llvm::Triple::ppc64le)
        TargetCPUName = "ppc64le";
      else
        TargetCPUName = "ppc";
    }
    return TargetCPUName;
  }

  case llvm::Triple::bpfel:
  case llvm::Triple::bpfeb:
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return x86::getX86TargetCPU(Args, T);

  case llvm::Triple::hexagon:
    return "hexagon" +
           toolchains::HexagonToolChain::GetTargetCPUVersion(Args).str();

  case llvm::Triple::lanai:
    return getLanaiTargetCPU(Args);

  case llvm::Triple::systemz:
    return systemz::getSystemZTargetCPU(Args);

  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    return getR600TargetGPU(Args);

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return getWebAssemblyTargetCPU(Args);
  }
}

unsigned tools::getLTOParallelism(const ArgList &Args, const Driver &D) {
  unsigned Parallelism = 0;
  Arg *LtoJobsArg = Args.getLastArg(options::OPT_flto_jobs_EQ);
  if (LtoJobsArg &&
      StringRef(LtoJobsArg->getValue()).getAsInteger(10, Parallelism))
    D.Diag(diag::err_drv_invalid_int_value) << LtoJobsArg->getAsString(Args)
                                            << LtoJobsArg->getValue();
  return Parallelism;
}

// CloudABI uses -ffunction-sections and -fdata-sections by default.
bool tools::isUseSeparateSections(const llvm::Triple &Triple) {
  return Triple.getOS() == llvm::Triple::CloudABI;
}

void tools::AddGoldPlugin(const ToolChain &ToolChain, const ArgList &Args,
                          ArgStringList &CmdArgs, const InputInfo &Output,
                          const InputInfo &Input, bool IsThinLTO) {
  // Tell the linker to load the plugin. This has to come before AddLinkerInputs
  // as gold requires -plugin to come before any -plugin-opt that -Wl might
  // forward.
  CmdArgs.push_back("-plugin");

#if defined(_WIN32)
  const char *Suffix = ".dll";
#elif defined(__APPLE__)
  const char *Suffix = ".dylib";
#else
  const char *Suffix = ".so";
#endif

  SmallString<1024> Plugin;
  llvm::sys::path::native(Twine(ToolChain.getDriver().Dir) +
                              "/../lib" CLANG_LIBDIR_SUFFIX "/LLVMgold" +
                              Suffix,
                          Plugin);
  CmdArgs.push_back(Args.MakeArgString(Plugin));

  // Try to pass driver level flags relevant to LTO code generation down to
  // the plugin.

  // Handle flags for selecting CPU variants.
  std::string CPU = getCPUName(Args, ToolChain.getTriple());
  if (!CPU.empty())
    CmdArgs.push_back(Args.MakeArgString(Twine("-plugin-opt=mcpu=") + CPU));

  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    StringRef OOpt;
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      OOpt = "3";
    else if (A->getOption().matches(options::OPT_O))
      OOpt = A->getValue();
    else if (A->getOption().matches(options::OPT_O0))
      OOpt = "0";
    if (!OOpt.empty())
      CmdArgs.push_back(Args.MakeArgString(Twine("-plugin-opt=O") + OOpt));
  }

  if (Args.hasArg(options::OPT_gsplit_dwarf)) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-plugin-opt=dwo_dir=") +
            Output.getFilename() + "_dwo"));
  }

  if (IsThinLTO)
    CmdArgs.push_back("-plugin-opt=thinlto");

  if (unsigned Parallelism = getLTOParallelism(Args, ToolChain.getDriver()))
    CmdArgs.push_back(
        Args.MakeArgString("-plugin-opt=jobs=" + Twine(Parallelism)));

  // If an explicit debugger tuning argument appeared, pass it along.
  if (Arg *A = Args.getLastArg(options::OPT_gTune_Group,
                               options::OPT_ggdbN_Group)) {
    if (A->getOption().matches(options::OPT_glldb))
      CmdArgs.push_back("-plugin-opt=-debugger-tune=lldb");
    else if (A->getOption().matches(options::OPT_gsce))
      CmdArgs.push_back("-plugin-opt=-debugger-tune=sce");
    else
      CmdArgs.push_back("-plugin-opt=-debugger-tune=gdb");
  }

  bool UseSeparateSections =
      isUseSeparateSections(ToolChain.getEffectiveTriple());

  if (Args.hasFlag(options::OPT_ffunction_sections,
                   options::OPT_fno_function_sections, UseSeparateSections)) {
    CmdArgs.push_back("-plugin-opt=-function-sections");
  }

  if (Args.hasFlag(options::OPT_fdata_sections, options::OPT_fno_data_sections,
                   UseSeparateSections)) {
    CmdArgs.push_back("-plugin-opt=-data-sections");
  }

  if (Arg *A = getLastProfileSampleUseArg(Args)) {
    StringRef FName = A->getValue();
    if (!llvm::sys::fs::exists(FName))
      ToolChain.getDriver().Diag(diag::err_drv_no_such_file) << FName;
    else
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-plugin-opt=sample-profile=") + FName));
  }

  // Need this flag to turn on new pass manager via Gold plugin.
  if (Args.hasFlag(options::OPT_fexperimental_new_pass_manager,
                   options::OPT_fno_experimental_new_pass_manager,
                   /* Default */ ENABLE_EXPERIMENTAL_NEW_PASS_MANAGER)) {
    CmdArgs.push_back("-plugin-opt=new-pass-manager");
  }

  // Setup statistics file output.
  SmallString<128> StatsFile =
      getStatsFileName(Args, Output, Input, ToolChain.getDriver());
  if (!StatsFile.empty())
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-plugin-opt=stats-file=") + StatsFile));
}

void tools::addArchSpecificRPath(const ToolChain &TC, const ArgList &Args,
                                 ArgStringList &CmdArgs) {
  if (!Args.hasFlag(options::OPT_frtlib_add_rpath,
                    options::OPT_fno_rtlib_add_rpath, false))
    return;

  std::string CandidateRPath = TC.getArchSpecificLibPath();
  if (TC.getVFS().exists(CandidateRPath)) {
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back(Args.MakeArgString(CandidateRPath.c_str()));
  }
}

bool tools::addOpenMPRuntime(ArgStringList &CmdArgs, const ToolChain &TC,
                             const ArgList &Args, bool IsOffloadingHost,
                             bool GompNeedsRT) {
  if (!Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                    options::OPT_fno_openmp, false))
    return false;

  switch (TC.getDriver().getOpenMPRuntime(Args)) {
  case Driver::OMPRT_OMP:
    CmdArgs.push_back("-lomp");
    break;
  case Driver::OMPRT_GOMP:
    CmdArgs.push_back("-lgomp");

    if (GompNeedsRT)
      CmdArgs.push_back("-lrt");
    break;
  case Driver::OMPRT_IOMP5:
    CmdArgs.push_back("-liomp5");
    break;
  case Driver::OMPRT_Unknown:
    // Already diagnosed.
    return false;
  }

  if (IsOffloadingHost)
    CmdArgs.push_back("-lomptarget");

  addArchSpecificRPath(TC, Args, CmdArgs);

  return true;
}

static void addSanitizerRuntime(const ToolChain &TC, const ArgList &Args,
                                ArgStringList &CmdArgs, StringRef Sanitizer,
                                bool IsShared, bool IsWhole) {
  // Wrap any static runtimes that must be forced into executable in
  // whole-archive.
  if (IsWhole) CmdArgs.push_back("--whole-archive");
  CmdArgs.push_back(TC.getCompilerRTArgString(Args, Sanitizer, IsShared));
  if (IsWhole) CmdArgs.push_back("--no-whole-archive");

  if (IsShared) {
    addArchSpecificRPath(TC, Args, CmdArgs);
  }
}

// Tries to use a file with the list of dynamic symbols that need to be exported
// from the runtime library. Returns true if the file was found.
static bool addSanitizerDynamicList(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs,
                                    StringRef Sanitizer) {
  // Solaris ld defaults to --export-dynamic behaviour but doesn't support
  // the option, so don't try to pass it.
  if (TC.getTriple().getOS() == llvm::Triple::Solaris)
    return true;
  // Myriad is static linking only.  Furthermore, some versions of its
  // linker have the bug where --export-dynamic overrides -static, so
  // don't use --export-dynamic on that platform.
  if (TC.getTriple().getVendor() == llvm::Triple::Myriad)
    return true;
  SmallString<128> SanRT(TC.getCompilerRT(Args, Sanitizer));
  if (llvm::sys::fs::exists(SanRT + ".syms")) {
    CmdArgs.push_back(Args.MakeArgString("--dynamic-list=" + SanRT + ".syms"));
    return true;
  }
  return false;
}

static void addSanitizerLibPath(const ToolChain &TC, const ArgList &Args,
                                ArgStringList &CmdArgs, StringRef Name) {
  for (const auto &LibPath : TC.getLibraryPaths()) {
    if (!LibPath.empty()) {
      SmallString<128> P(LibPath);
      llvm::sys::path::append(P, Name);
      if (TC.getVFS().exists(P))
        CmdArgs.push_back(Args.MakeArgString(StringRef("-L") + P));
    }
  }
}

void tools::addSanitizerPathLibArgs(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs();
  if (SanArgs.needsAsanRt()) {
    addSanitizerLibPath(TC, Args, CmdArgs, "asan");
  }
  if (SanArgs.needsHwasanRt()) {
    addSanitizerLibPath(TC, Args, CmdArgs, "hwasan");
  }
  if (SanArgs.needsLsanRt()) {
    addSanitizerLibPath(TC, Args, CmdArgs, "lsan");
  }
  if (SanArgs.needsMsanRt()) {
    addSanitizerLibPath(TC, Args, CmdArgs, "msan");
  }
  if (SanArgs.needsTsanRt()) {
    addSanitizerLibPath(TC, Args, CmdArgs, "tsan");
  }
}



void tools::linkSanitizerRuntimeDeps(const ToolChain &TC,
                                     ArgStringList &CmdArgs) {
  // Force linking against the system libraries sanitizers depends on
  // (see PR15823 why this is necessary).
  CmdArgs.push_back("--no-as-needed");
  // There's no libpthread or librt on RTEMS & Android.
  if (TC.getTriple().getOS() != llvm::Triple::RTEMS &&
      !TC.getTriple().isAndroid()) {
    CmdArgs.push_back("-lpthread");
    if (!TC.getTriple().isOSOpenBSD())
      CmdArgs.push_back("-lrt");
  }
  CmdArgs.push_back("-lm");
  // There's no libdl on all OSes.
  if (!TC.getTriple().isOSFreeBSD() &&
      !TC.getTriple().isOSNetBSD() &&
      !TC.getTriple().isOSOpenBSD() &&
       TC.getTriple().getOS() != llvm::Triple::RTEMS)
    CmdArgs.push_back("-ldl");
  // Required for backtrace on some OSes
  if (TC.getTriple().isOSFreeBSD() ||
      TC.getTriple().isOSNetBSD())
    CmdArgs.push_back("-lexecinfo");
}

static void
collectSanitizerRuntimes(const ToolChain &TC, const ArgList &Args,
                         SmallVectorImpl<StringRef> &SharedRuntimes,
                         SmallVectorImpl<StringRef> &StaticRuntimes,
                         SmallVectorImpl<StringRef> &NonWholeStaticRuntimes,
                         SmallVectorImpl<StringRef> &HelperStaticRuntimes,
                         SmallVectorImpl<StringRef> &RequiredSymbols) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs();
  // Collect shared runtimes.
  if (SanArgs.needsSharedRt()) {
    if (SanArgs.needsAsanRt()) {
      SharedRuntimes.push_back("asan");
      if (!Args.hasArg(options::OPT_shared) && !TC.getTriple().isAndroid())
        HelperStaticRuntimes.push_back("asan-preinit");
    }
    if (SanArgs.needsUbsanRt()) {
      if (SanArgs.requiresMinimalRuntime())
        SharedRuntimes.push_back("ubsan_minimal");
      else
        SharedRuntimes.push_back("ubsan_standalone");
    }
    if (SanArgs.needsScudoRt()) {
      if (SanArgs.requiresMinimalRuntime())
        SharedRuntimes.push_back("scudo_minimal");
      else
        SharedRuntimes.push_back("scudo");
    }
    if (SanArgs.needsHwasanRt())
      SharedRuntimes.push_back("hwasan");
  }

  // The stats_client library is also statically linked into DSOs.
  if (SanArgs.needsStatsRt())
    StaticRuntimes.push_back("stats_client");

  // Collect static runtimes.
  if (Args.hasArg(options::OPT_shared) || SanArgs.needsSharedRt()) {
    // Don't link static runtimes into DSOs or if -shared-libasan.
    return;
  }
  if (SanArgs.needsAsanRt()) {
    StaticRuntimes.push_back("asan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("asan_cxx");
  }

  if (SanArgs.needsHwasanRt()) {
    StaticRuntimes.push_back("hwasan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("hwasan_cxx");
  }
  if (SanArgs.needsDfsanRt())
    StaticRuntimes.push_back("dfsan");
  if (SanArgs.needsLsanRt())
    StaticRuntimes.push_back("lsan");
  if (SanArgs.needsMsanRt()) {
    StaticRuntimes.push_back("msan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("msan_cxx");
  }
  if (SanArgs.needsTsanRt()) {
    StaticRuntimes.push_back("tsan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("tsan_cxx");
  }
  if (SanArgs.needsUbsanRt()) {
    if (SanArgs.requiresMinimalRuntime()) {
      StaticRuntimes.push_back("ubsan_minimal");
    } else {
      StaticRuntimes.push_back("ubsan_standalone");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("ubsan_standalone_cxx");
    }
  }
  if (SanArgs.needsSafeStackRt()) {
    NonWholeStaticRuntimes.push_back("safestack");
    RequiredSymbols.push_back("__safestack_init");
  }
  if (SanArgs.needsCfiRt())
    StaticRuntimes.push_back("cfi");
  if (SanArgs.needsCfiDiagRt()) {
    StaticRuntimes.push_back("cfi_diag");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("ubsan_standalone_cxx");
  }
  if (SanArgs.needsStatsRt()) {
    NonWholeStaticRuntimes.push_back("stats");
    RequiredSymbols.push_back("__sanitizer_stats_register");
  }
  if (SanArgs.needsEsanRt())
    StaticRuntimes.push_back("esan");
  if (SanArgs.needsScudoRt()) {
    if (SanArgs.requiresMinimalRuntime()) {
      StaticRuntimes.push_back("scudo_minimal");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("scudo_cxx_minimal");
    } else {
      StaticRuntimes.push_back("scudo");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("scudo_cxx");
    }
  }
}

// Should be called before we add system libraries (C++ ABI, libstdc++/libc++,
// C runtime, etc). Returns true if sanitizer system deps need to be linked in.
bool tools::addSanitizerRuntimes(const ToolChain &TC, const ArgList &Args,
                                 ArgStringList &CmdArgs) {
  SmallVector<StringRef, 4> SharedRuntimes, StaticRuntimes,
      NonWholeStaticRuntimes, HelperStaticRuntimes, RequiredSymbols;
  collectSanitizerRuntimes(TC, Args, SharedRuntimes, StaticRuntimes,
                           NonWholeStaticRuntimes, HelperStaticRuntimes,
                           RequiredSymbols);

  // Inject libfuzzer dependencies.
  if (TC.getSanitizerArgs().needsFuzzer()
      && !Args.hasArg(options::OPT_shared)) {

    addSanitizerRuntime(TC, Args, CmdArgs, "fuzzer", false, true);
    if (!Args.hasArg(clang::driver::options::OPT_nostdlibxx))
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
  }

  for (auto RT : SharedRuntimes)
    addSanitizerRuntime(TC, Args, CmdArgs, RT, true, false);
  for (auto RT : HelperStaticRuntimes)
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, true);
  bool AddExportDynamic = false;
  for (auto RT : StaticRuntimes) {
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, true);
    AddExportDynamic |= !addSanitizerDynamicList(TC, Args, CmdArgs, RT);
  }
  for (auto RT : NonWholeStaticRuntimes) {
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, false);
    AddExportDynamic |= !addSanitizerDynamicList(TC, Args, CmdArgs, RT);
  }
  for (auto S : RequiredSymbols) {
    CmdArgs.push_back("-u");
    CmdArgs.push_back(Args.MakeArgString(S));
  }
  // If there is a static runtime with no dynamic list, force all the symbols
  // to be dynamic to be sure we export sanitizer interface functions.
  if (AddExportDynamic)
    CmdArgs.push_back("--export-dynamic");

  const SanitizerArgs &SanArgs = TC.getSanitizerArgs();
  if (SanArgs.hasCrossDsoCfi() && !AddExportDynamic)
    CmdArgs.push_back("-export-dynamic-symbol=__cfi_check");

  return !StaticRuntimes.empty() || !NonWholeStaticRuntimes.empty();
}

bool tools::addXRayRuntime(const ToolChain&TC, const ArgList &Args, ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_shared))
    return false;

  if (TC.getXRayArgs().needsXRayRt()) {
    CmdArgs.push_back("-whole-archive");
    CmdArgs.push_back(TC.getCompilerRTArgString(Args, "xray", false));
    for (const auto &Mode : TC.getXRayArgs().modeList())
      CmdArgs.push_back(TC.getCompilerRTArgString(Args, Mode, false));
    CmdArgs.push_back("-no-whole-archive");
    return true;
  }

  return false;
}

void tools::linkXRayRuntimeDeps(const ToolChain &TC, ArgStringList &CmdArgs) {
  CmdArgs.push_back("--no-as-needed");
  CmdArgs.push_back("-lpthread");
  if (!TC.getTriple().isOSOpenBSD())
    CmdArgs.push_back("-lrt");
  CmdArgs.push_back("-lm");

  if (!TC.getTriple().isOSFreeBSD() &&
      !TC.getTriple().isOSNetBSD() &&
      !TC.getTriple().isOSOpenBSD())
    CmdArgs.push_back("-ldl");
}

bool tools::areOptimizationsEnabled(const ArgList &Args) {
  // Find the last -O arg and see if it is non-zero.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group))
    return !A->getOption().matches(options::OPT_O0);
  // Defaults to -O0.
  return false;
}

const char *tools::SplitDebugName(const ArgList &Args,
                                  const InputInfo &Output) {
  SmallString<128> F(Output.isFilename()
                         ? Output.getFilename()
                         : llvm::sys::path::stem(Output.getBaseInput()));

  if (Arg *A = Args.getLastArg(options::OPT_gsplit_dwarf_EQ))
    if (StringRef(A->getValue()) == "single")
      return Args.MakeArgString(F);

  llvm::sys::path::replace_extension(F, "dwo");
  return Args.MakeArgString(F);
}

void tools::SplitDebugInfo(const ToolChain &TC, Compilation &C, const Tool &T,
                           const JobAction &JA, const ArgList &Args,
                           const InputInfo &Output, const char *OutFile) {
  ArgStringList ExtractArgs;
  ExtractArgs.push_back("--extract-dwo");

  ArgStringList StripArgs;
  StripArgs.push_back("--strip-dwo");

  // Grabbing the output of the earlier compile step.
  StripArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(OutFile);

  const char *Exec =
      Args.MakeArgString(TC.GetProgramPath(CLANG_DEFAULT_OBJCOPY));
  InputInfo II(types::TY_Object, Output.getFilename(), Output.getFilename());

  // First extract the dwo sections.
  C.addCommand(llvm::make_unique<Command>(JA, T, Exec, ExtractArgs, II));

  // Then remove them from the original .o file.
  C.addCommand(llvm::make_unique<Command>(JA, T, Exec, StripArgs, II));
}

// Claim options we don't want to warn if they are unused. We do this for
// options that build systems might add but are unused when assembling or only
// running the preprocessor for example.
void tools::claimNoWarnArgs(const ArgList &Args) {
  // Don't warn about unused -f(no-)?lto.  This can happen when we're
  // preprocessing, precompiling or assembling.
  Args.ClaimAllArgs(options::OPT_flto_EQ);
  Args.ClaimAllArgs(options::OPT_flto);
  Args.ClaimAllArgs(options::OPT_fno_lto);
}

Arg *tools::getLastProfileUseArg(const ArgList &Args) {
  auto *ProfileUseArg = Args.getLastArg(
      options::OPT_fprofile_instr_use, options::OPT_fprofile_instr_use_EQ,
      options::OPT_fprofile_use, options::OPT_fprofile_use_EQ,
      options::OPT_fno_profile_instr_use);

  if (ProfileUseArg &&
      ProfileUseArg->getOption().matches(options::OPT_fno_profile_instr_use))
    ProfileUseArg = nullptr;

  return ProfileUseArg;
}

Arg *tools::getLastProfileSampleUseArg(const ArgList &Args) {
  auto *ProfileSampleUseArg = Args.getLastArg(
      options::OPT_fprofile_sample_use, options::OPT_fprofile_sample_use_EQ,
      options::OPT_fauto_profile, options::OPT_fauto_profile_EQ,
      options::OPT_fno_profile_sample_use, options::OPT_fno_auto_profile);

  if (ProfileSampleUseArg &&
      (ProfileSampleUseArg->getOption().matches(
           options::OPT_fno_profile_sample_use) ||
       ProfileSampleUseArg->getOption().matches(options::OPT_fno_auto_profile)))
    return nullptr;

  return Args.getLastArg(options::OPT_fprofile_sample_use_EQ,
                         options::OPT_fauto_profile_EQ);
}

/// Parses the various -fpic/-fPIC/-fpie/-fPIE arguments.  Then,
/// smooshes them together with platform defaults, to decide whether
/// this compile should be using PIC mode or not. Returns a tuple of
/// (RelocationModel, PICLevel, IsPIE).
std::tuple<llvm::Reloc::Model, unsigned, bool>
tools::ParsePICArgs(const ToolChain &ToolChain, const ArgList &Args) {
  const llvm::Triple &EffectiveTriple = ToolChain.getEffectiveTriple();
  const llvm::Triple &Triple = ToolChain.getTriple();

  bool PIE = ToolChain.isPIEDefault();
  bool PIC = PIE || ToolChain.isPICDefault();
  // The Darwin/MachO default to use PIC does not apply when using -static.
  if (Triple.isOSBinFormatMachO() && Args.hasArg(options::OPT_static))
    PIE = PIC = false;
  bool IsPICLevelTwo = PIC;

  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);

  // Android-specific defaults for PIC/PIE
  if (Triple.isAndroid()) {
    switch (Triple.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
    case llvm::Triple::aarch64:
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      PIC = true; // "-fpic"
      break;

    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      PIC = true; // "-fPIC"
      IsPICLevelTwo = true;
      break;

    default:
      break;
    }
  }

  // OpenBSD-specific defaults for PIE
  if (Triple.isOSOpenBSD()) {
    switch (ToolChain.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::aarch64:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      IsPICLevelTwo = false; // "-fpie"
      break;

    case llvm::Triple::ppc:
    case llvm::Triple::sparc:
    case llvm::Triple::sparcel:
    case llvm::Triple::sparcv9:
      IsPICLevelTwo = true; // "-fPIE"
      break;

    default:
      break;
    }
  }

  // AMDGPU-specific defaults for PIC.
  if (Triple.getArch() == llvm::Triple::amdgcn)
    PIC = true;

  // The last argument relating to either PIC or PIE wins, and no
  // other argument is used. If the last argument is any flavor of the
  // '-fno-...' arguments, both PIC and PIE are disabled. Any PIE
  // option implicitly enables PIC at the same level.
  Arg *LastPICArg = Args.getLastArg(options::OPT_fPIC, options::OPT_fno_PIC,
                                    options::OPT_fpic, options::OPT_fno_pic,
                                    options::OPT_fPIE, options::OPT_fno_PIE,
                                    options::OPT_fpie, options::OPT_fno_pie);
  if (Triple.isOSWindows() && LastPICArg &&
      LastPICArg ==
          Args.getLastArg(options::OPT_fPIC, options::OPT_fpic,
                          options::OPT_fPIE, options::OPT_fpie)) {
    ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
        << LastPICArg->getSpelling() << Triple.str();
    if (Triple.getArch() == llvm::Triple::x86_64)
      return std::make_tuple(llvm::Reloc::PIC_, 2U, false);
    return std::make_tuple(llvm::Reloc::Static, 0U, false);
  }

  // Check whether the tool chain trumps the PIC-ness decision. If the PIC-ness
  // is forced, then neither PIC nor PIE flags will have no effect.
  if (!ToolChain.isPICDefaultForced()) {
    if (LastPICArg) {
      Option O = LastPICArg->getOption();
      if (O.matches(options::OPT_fPIC) || O.matches(options::OPT_fpic) ||
          O.matches(options::OPT_fPIE) || O.matches(options::OPT_fpie)) {
        PIE = O.matches(options::OPT_fPIE) || O.matches(options::OPT_fpie);
        PIC =
            PIE || O.matches(options::OPT_fPIC) || O.matches(options::OPT_fpic);
        IsPICLevelTwo =
            O.matches(options::OPT_fPIE) || O.matches(options::OPT_fPIC);
      } else {
        PIE = PIC = false;
        if (EffectiveTriple.isPS4CPU()) {
          Arg *ModelArg = Args.getLastArg(options::OPT_mcmodel_EQ);
          StringRef Model = ModelArg ? ModelArg->getValue() : "";
          if (Model != "kernel") {
            PIC = true;
            ToolChain.getDriver().Diag(diag::warn_drv_ps4_force_pic)
                << LastPICArg->getSpelling();
          }
        }
      }
    }
  }

  // Introduce a Darwin and PS4-specific hack. If the default is PIC, but the
  // PIC level would've been set to level 1, force it back to level 2 PIC
  // instead.
  if (PIC && (Triple.isOSDarwin() || EffectiveTriple.isPS4CPU()))
    IsPICLevelTwo |= ToolChain.isPICDefault();

  // This kernel flags are a trump-card: they will disable PIC/PIE
  // generation, independent of the argument order.
  if (KernelOrKext &&
      ((!EffectiveTriple.isiOS() || EffectiveTriple.isOSVersionLT(6)) &&
       !EffectiveTriple.isWatchOS()))
    PIC = PIE = false;

  if (Arg *A = Args.getLastArg(options::OPT_mdynamic_no_pic)) {
    // This is a very special mode. It trumps the other modes, almost no one
    // uses it, and it isn't even valid on any OS but Darwin.
    if (!Triple.isOSDarwin())
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << Triple.str();

    // FIXME: Warn when this flag trumps some other PIC or PIE flag.

    // Only a forced PIC mode can cause the actual compile to have PIC defines
    // etc., no flags are sufficient. This behavior was selected to closely
    // match that of llvm-gcc and Apple GCC before that.
    PIC = ToolChain.isPICDefault() && ToolChain.isPICDefaultForced();

    return std::make_tuple(llvm::Reloc::DynamicNoPIC, PIC ? 2U : 0U, false);
  }

  bool EmbeddedPISupported;
  switch (Triple.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      EmbeddedPISupported = true;
      break;
    default:
      EmbeddedPISupported = false;
      break;
  }

  bool ROPI = false, RWPI = false;
  Arg* LastROPIArg = Args.getLastArg(options::OPT_fropi, options::OPT_fno_ropi);
  if (LastROPIArg && LastROPIArg->getOption().matches(options::OPT_fropi)) {
    if (!EmbeddedPISupported)
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << LastROPIArg->getSpelling() << Triple.str();
    ROPI = true;
  }
  Arg *LastRWPIArg = Args.getLastArg(options::OPT_frwpi, options::OPT_fno_rwpi);
  if (LastRWPIArg && LastRWPIArg->getOption().matches(options::OPT_frwpi)) {
    if (!EmbeddedPISupported)
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << LastRWPIArg->getSpelling() << Triple.str();
    RWPI = true;
  }

  // ROPI and RWPI are not compatible with PIC or PIE.
  if ((ROPI || RWPI) && (PIC || PIE))
    ToolChain.getDriver().Diag(diag::err_drv_ropi_rwpi_incompatible_with_pic);

  if (Triple.isMIPS()) {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);
    // When targeting the N64 ABI, PIC is the default, except in the case
    // when the -mno-abicalls option is used. In that case we exit
    // at next check regardless of PIC being set below.
    if (ABIName == "n64")
      PIC = true;
    // When targettng MIPS with -mno-abicalls, it's always static.
    if(Args.hasArg(options::OPT_mno_abicalls))
      return std::make_tuple(llvm::Reloc::Static, 0U, false);
    // Unlike other architectures, MIPS, even with -fPIC/-mxgot/multigot,
    // does not use PIC level 2 for historical reasons.
    IsPICLevelTwo = false;
  }

  if (PIC)
    return std::make_tuple(llvm::Reloc::PIC_, IsPICLevelTwo ? 2U : 1U, PIE);

  llvm::Reloc::Model RelocM = llvm::Reloc::Static;
  if (ROPI && RWPI)
    RelocM = llvm::Reloc::ROPI_RWPI;
  else if (ROPI)
    RelocM = llvm::Reloc::ROPI;
  else if (RWPI)
    RelocM = llvm::Reloc::RWPI;

  return std::make_tuple(RelocM, 0U, false);
}

// `-falign-functions` indicates that the functions should be aligned to a
// 16-byte boundary.
//
// `-falign-functions=1` is the same as `-fno-align-functions`.
//
// The scalar `n` in `-falign-functions=n` must be an integral value between
// [0, 65536].  If the value is not a power-of-two, it will be rounded up to
// the nearest power-of-two.
//
// If we return `0`, the frontend will default to the backend's preferred
// alignment.
//
// NOTE: icc only allows values between [0, 4096].  icc uses `-falign-functions`
// to mean `-falign-functions=16`.  GCC defaults to the backend's preferred
// alignment.  For unaligned functions, we default to the backend's preferred
// alignment.
unsigned tools::ParseFunctionAlignment(const ToolChain &TC,
                                       const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_falign_functions,
                                 options::OPT_falign_functions_EQ,
                                 options::OPT_fno_align_functions);
  if (!A || A->getOption().matches(options::OPT_fno_align_functions))
    return 0;

  if (A->getOption().matches(options::OPT_falign_functions))
    return 0;

  unsigned Value = 0;
  if (StringRef(A->getValue()).getAsInteger(10, Value) || Value > 65536)
    TC.getDriver().Diag(diag::err_drv_invalid_int_value)
        << A->getAsString(Args) << A->getValue();
  return Value ? llvm::Log2_32_Ceil(std::min(Value, 65536u)) : Value;
}

void tools::AddAssemblerKPIC(const ToolChain &ToolChain, const ArgList &Args,
                             ArgStringList &CmdArgs) {
  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) = ParsePICArgs(ToolChain, Args);

  if (RelocationModel != llvm::Reloc::Static)
    CmdArgs.push_back("-KPIC");
}

/// Determine whether Objective-C automated reference counting is
/// enabled.
bool tools::isObjCAutoRefCount(const ArgList &Args) {
  return Args.hasFlag(options::OPT_fobjc_arc, options::OPT_fno_objc_arc, false);
}

static void AddLibgcc(const llvm::Triple &Triple, const Driver &D,
                      ArgStringList &CmdArgs, const ArgList &Args) {
  bool isAndroid = Triple.isAndroid();
  bool isCygMing = Triple.isOSCygMing();
  bool IsIAMCU = Triple.isOSIAMCU();
  bool StaticLibgcc = Args.hasArg(options::OPT_static_libgcc) ||
                      Args.hasArg(options::OPT_static);

  bool SharedLibgcc = Args.hasArg(options::OPT_shared_libgcc);
  bool UnspecifiedLibgcc = !StaticLibgcc && !SharedLibgcc;

  // Gcc adds libgcc arguments in various ways:
  //
  // gcc <none>: -lgcc --as-needed -lgcc_s --no-as-needed
  // g++ <none>:                   -lgcc_s               -lgcc
  // gcc shared:                   -lgcc_s               -lgcc
  // g++ shared:                   -lgcc_s               -lgcc
  // gcc static: -lgcc             -lgcc_eh
  // g++ static: -lgcc             -lgcc_eh
  //
  // Also, certain targets need additional adjustments.

  bool LibGccFirst = (D.CCCIsCC() && UnspecifiedLibgcc) || StaticLibgcc;
  if (LibGccFirst)
    CmdArgs.push_back("-lgcc");

  bool AsNeeded = D.CCCIsCC() && UnspecifiedLibgcc && !isAndroid && !isCygMing;
  if (AsNeeded)
    CmdArgs.push_back("--as-needed");

  if ((UnspecifiedLibgcc || SharedLibgcc) && !isAndroid)
    CmdArgs.push_back("-lgcc_s");

  else if (StaticLibgcc && !isAndroid && !IsIAMCU)
    CmdArgs.push_back("-lgcc_eh");

  if (AsNeeded)
    CmdArgs.push_back("--no-as-needed");

  if (!LibGccFirst)
    CmdArgs.push_back("-lgcc");

  // According to Android ABI, we have to link with libdl if we are
  // linking with non-static libgcc.
  //
  // NOTE: This fixes a link error on Android MIPS as well.  The non-static
  // libgcc for MIPS relies on _Unwind_Find_FDE and dl_iterate_phdr from libdl.
  if (isAndroid && !StaticLibgcc)
    CmdArgs.push_back("-ldl");
}

void tools::AddRunTimeLibs(const ToolChain &TC, const Driver &D,
                           ArgStringList &CmdArgs, const ArgList &Args) {
  // Make use of compiler-rt if --rtlib option is used
  ToolChain::RuntimeLibType RLT = TC.GetRuntimeLibType(Args);

  switch (RLT) {
  case ToolChain::RLT_CompilerRT:
    CmdArgs.push_back(TC.getCompilerRTArgString(Args, "builtins"));
    break;
  case ToolChain::RLT_Libgcc:
    // Make sure libgcc is not used under MSVC environment by default
    if (TC.getTriple().isKnownWindowsMSVCEnvironment()) {
      // Issue error diagnostic if libgcc is explicitly specified
      // through command line as --rtlib option argument.
      if (Args.hasArg(options::OPT_rtlib_EQ)) {
        TC.getDriver().Diag(diag::err_drv_unsupported_rtlib_for_platform)
            << Args.getLastArg(options::OPT_rtlib_EQ)->getValue() << "MSVC";
      }
    } else
      AddLibgcc(TC.getTriple(), D, CmdArgs, Args);
    break;
  }
}

/// Add OpenMP linker script arguments at the end of the argument list so that
/// the fat binary is built by embedding each of the device images into the
/// host. The linker script also defines a few symbols required by the code
/// generation so that the images can be easily retrieved at runtime by the
/// offloading library. This should be used only in tool chains that support
/// linker scripts.
void tools::AddOpenMPLinkerScript(const ToolChain &TC, Compilation &C,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args, ArgStringList &CmdArgs,
                                  const JobAction &JA) {

  // If this is not an OpenMP host toolchain, we don't need to do anything.
  if (!JA.isHostOffloading(Action::OFK_OpenMP))
    return;

  // Create temporary linker script. Keep it if save-temps is enabled.
  const char *LKS;
  SmallString<256> Name = llvm::sys::path::filename(Output.getFilename());
  if (C.getDriver().isSaveTempsEnabled()) {
    llvm::sys::path::replace_extension(Name, "lk");
    LKS = C.getArgs().MakeArgString(Name.c_str());
  } else {
    llvm::sys::path::replace_extension(Name, "");
    Name = C.getDriver().GetTemporaryPath(Name, "lk");
    LKS = C.addTempFile(C.getArgs().MakeArgString(Name.c_str()));
  }

  // Add linker script option to the command.
  CmdArgs.push_back("-T");
  CmdArgs.push_back(LKS);

  // Create a buffer to write the contents of the linker script.
  std::string LksBuffer;
  llvm::raw_string_ostream LksStream(LksBuffer);

  // Get the OpenMP offload tool chains so that we can extract the triple
  // associated with each device input.
  auto OpenMPToolChains = C.getOffloadToolChains<Action::OFK_OpenMP>();
  assert(OpenMPToolChains.first != OpenMPToolChains.second &&
         "No OpenMP toolchains??");

  // Track the input file name and device triple in order to build the script,
  // inserting binaries in the designated sections.
  SmallVector<std::pair<std::string, const char *>, 8> InputBinaryInfo;

  // Add commands to embed target binaries. We ensure that each section and
  // image is 16-byte aligned. This is not mandatory, but increases the
  // likelihood of data to be aligned with a cache block in several main host
  // machines.
  LksStream << "/*\n";
  LksStream << "       OpenMP Offload Linker Script\n";
  LksStream << " *** Automatically generated by Clang ***\n";
  LksStream << "*/\n";
  LksStream << "TARGET(binary)\n";
  auto DTC = OpenMPToolChains.first;
  for (auto &II : Inputs) {
    const Action *A = II.getAction();
    // Is this a device linking action?
    if (A && isa<LinkJobAction>(A) &&
        A->isDeviceOffloading(Action::OFK_OpenMP)) {
      assert(DTC != OpenMPToolChains.second &&
             "More device inputs than device toolchains??");
      InputBinaryInfo.push_back(std::make_pair(
          DTC->second->getTriple().normalize(), II.getFilename()));
      ++DTC;
      LksStream << "INPUT(" << II.getFilename() << ")\n";
    }
  }

  assert(DTC == OpenMPToolChains.second &&
         "Less device inputs than device toolchains??");

  LksStream << "SECTIONS\n";
  LksStream << "{\n";

  // Put each target binary into a separate section.
  for (const auto &BI : InputBinaryInfo) {
    LksStream << "  .omp_offloading." << BI.first << " :\n";
    LksStream << "  ALIGN(0x10)\n";
    LksStream << "  {\n";
    LksStream << "    PROVIDE_HIDDEN(.omp_offloading.img_start." << BI.first
              << " = .);\n";
    LksStream << "    " << BI.second << "\n";
    LksStream << "    PROVIDE_HIDDEN(.omp_offloading.img_end." << BI.first
              << " = .);\n";
    LksStream << "  }\n";
  }

  // Add commands to define host entries begin and end. We use 1-byte subalign
  // so that the linker does not add any padding and the elements in this
  // section form an array.
  LksStream << "  .omp_offloading.entries :\n";
  LksStream << "  ALIGN(0x10)\n";
  LksStream << "  SUBALIGN(0x01)\n";
  LksStream << "  {\n";
  LksStream << "    PROVIDE_HIDDEN(.omp_offloading.entries_begin = .);\n";
  LksStream << "    *(.omp_offloading.entries)\n";
  LksStream << "    PROVIDE_HIDDEN(.omp_offloading.entries_end = .);\n";
  LksStream << "  }\n";
  LksStream << "}\n";
  LksStream << "INSERT BEFORE .data\n";
  LksStream.flush();

  // Dump the contents of the linker script if the user requested that. We
  // support this option to enable testing of behavior with -###.
  if (C.getArgs().hasArg(options::OPT_fopenmp_dump_offload_linker_script))
    llvm::errs() << LksBuffer;

  // If this is a dry run, do not create the linker script file.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH))
    return;

  // Open script file and write the contents.
  std::error_code EC;
  llvm::raw_fd_ostream Lksf(LKS, EC, llvm::sys::fs::F_None);

  if (EC) {
    C.getDriver().Diag(clang::diag::err_unable_to_make_temp) << EC.message();
    return;
  }

  Lksf << LksBuffer;
}

/// Add HIP linker script arguments at the end of the argument list so that
/// the fat binary is built by embedding the device images into the host. The
/// linker script also defines a symbol required by the code generation so that
/// the image can be retrieved at runtime. This should be used only in tool
/// chains that support linker scripts.
void tools::AddHIPLinkerScript(const ToolChain &TC, Compilation &C,
                               const InputInfo &Output,
                               const InputInfoList &Inputs, const ArgList &Args,
                               ArgStringList &CmdArgs, const JobAction &JA,
                               const Tool &T) {

  // If this is not a HIP host toolchain, we don't need to do anything.
  if (!JA.isHostOffloading(Action::OFK_HIP))
    return;

  InputInfoList DeviceInputs;
  for (const auto &II : Inputs) {
    const Action *A = II.getAction();
    // Is this a device linking action?
    if (A && isa<LinkJobAction>(A) && A->isDeviceOffloading(Action::OFK_HIP)) {
      DeviceInputs.push_back(II);
    }
  }

  if (DeviceInputs.empty())
    return;

  // Create temporary linker script. Keep it if save-temps is enabled.
  const char *LKS;
  SmallString<256> Name = llvm::sys::path::filename(Output.getFilename());
  if (C.getDriver().isSaveTempsEnabled()) {
    llvm::sys::path::replace_extension(Name, "lk");
    LKS = C.getArgs().MakeArgString(Name.c_str());
  } else {
    llvm::sys::path::replace_extension(Name, "");
    Name = C.getDriver().GetTemporaryPath(Name, "lk");
    LKS = C.addTempFile(C.getArgs().MakeArgString(Name.c_str()));
  }

  // Add linker script option to the command.
  CmdArgs.push_back("-T");
  CmdArgs.push_back(LKS);

  // Create a buffer to write the contents of the linker script.
  std::string LksBuffer;
  llvm::raw_string_ostream LksStream(LksBuffer);

  // Get the HIP offload tool chain.
  auto *HIPTC = static_cast<const toolchains::CudaToolChain *>(
      C.getSingleOffloadToolChain<Action::OFK_HIP>());
  assert(HIPTC->getTriple().getArch() == llvm::Triple::amdgcn &&
         "Wrong platform");
  (void)HIPTC;

  // The output file name needs to persist through the compilation, therefore
  // it needs to be created through MakeArgString.
  std::string BundleFileName = C.getDriver().GetTemporaryPath("BUNDLE", "hipfb");
  const char *BundleFile =
      C.addTempFile(C.getArgs().MakeArgString(BundleFileName.c_str()));
  AMDGCN::constructHIPFatbinCommand(C, JA, BundleFile, DeviceInputs, Args, T);

  // Add commands to embed target binaries. We ensure that each section and
  // image is 16-byte aligned. This is not mandatory, but increases the
  // likelihood of data to be aligned with a cache block in several main host
  // machines.
  LksStream << "/*\n";
  LksStream << "       HIP Offload Linker Script\n";
  LksStream << " *** Automatically generated by Clang ***\n";
  LksStream << "*/\n";
  LksStream << "TARGET(binary)\n";
  LksStream << "INPUT(" << BundleFileName << ")\n";
  LksStream << "SECTIONS\n";
  LksStream << "{\n";
  LksStream << "  .hip_fatbin :\n";
  LksStream << "  ALIGN(0x10)\n";
  LksStream << "  {\n";
  LksStream << "    PROVIDE_HIDDEN(__hip_fatbin = .);\n";
  LksStream << "    " << BundleFileName << "\n";
  LksStream << "  }\n";
  LksStream << "  /DISCARD/ :\n";
  LksStream << "  {\n";
  LksStream << "    * ( __CLANG_OFFLOAD_BUNDLE__* )\n";
  LksStream << "  }\n";
  LksStream << "}\n";
  LksStream << "INSERT BEFORE .data\n";
  LksStream.flush();

  // Dump the contents of the linker script if the user requested that. We
  // support this option to enable testing of behavior with -###.
  if (C.getArgs().hasArg(options::OPT_fhip_dump_offload_linker_script))
    llvm::errs() << LksBuffer;

  // If this is a dry run, do not create the linker script file.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH))
    return;

  // Open script file and write the contents.
  std::error_code EC;
  llvm::raw_fd_ostream Lksf(LKS, EC, llvm::sys::fs::F_None);

  if (EC) {
    C.getDriver().Diag(clang::diag::err_unable_to_make_temp) << EC.message();
    return;
  }

  Lksf << LksBuffer;
}

SmallString<128> tools::getStatsFileName(const llvm::opt::ArgList &Args,
                                         const InputInfo &Output,
                                         const InputInfo &Input,
                                         const Driver &D) {
  const Arg *A = Args.getLastArg(options::OPT_save_stats_EQ);
  if (!A)
    return {};

  StringRef SaveStats = A->getValue();
  SmallString<128> StatsFile;
  if (SaveStats == "obj" && Output.isFilename()) {
    StatsFile.assign(Output.getFilename());
    llvm::sys::path::remove_filename(StatsFile);
  } else if (SaveStats != "cwd") {
    D.Diag(diag::err_drv_invalid_value) << A->getAsString(Args) << SaveStats;
    return {};
  }

  StringRef BaseName = llvm::sys::path::filename(Input.getBaseInput());
  llvm::sys::path::append(StatsFile, BaseName);
  llvm::sys::path::replace_extension(StatsFile, "stats");
  return StatsFile;
}
