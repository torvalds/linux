//===--- LLVM.cpp - Clang+LLVM ToolChain Implementations --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Clang.h"
#include "Arch/AArch64.h"
#include "Arch/ARM.h"
#include "Arch/Mips.h"
#include "Arch/PPC.h"
#include "Arch/RISCV.h"
#include "Arch/Sparc.h"
#include "Arch/SystemZ.h"
#include "Arch/X86.h"
#include "AMDGPU.h"
#include "CommonArgs.h"
#include "Hexagon.h"
#include "MSP430.h"
#include "InputInfo.h"
#include "PS4CPU.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/Distro.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/XRayArgs.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/YAMLParser.h"

#ifdef LLVM_ON_UNIX
#include <unistd.h> // For getuid().
#endif

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static void CheckPreprocessingOptions(const Driver &D, const ArgList &Args) {
  if (Arg *A =
          Args.getLastArg(clang::driver::options::OPT_C, options::OPT_CC)) {
    if (!Args.hasArg(options::OPT_E) && !Args.hasArg(options::OPT__SLASH_P) &&
        !Args.hasArg(options::OPT__SLASH_EP) && !D.CCCIsCPP()) {
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << A->getBaseArg().getAsString(Args)
          << (D.IsCLMode() ? "/E, /P or /EP" : "-E");
    }
  }
}

static void CheckCodeGenerationOptions(const Driver &D, const ArgList &Args) {
  // In gcc, only ARM checks this, but it seems reasonable to check universally.
  if (Args.hasArg(options::OPT_static))
    if (const Arg *A =
            Args.getLastArg(options::OPT_dynamic, options::OPT_mdynamic_no_pic))
      D.Diag(diag::err_drv_argument_not_allowed_with) << A->getAsString(Args)
                                                      << "-static";
}

// Add backslashes to escape spaces and other backslashes.
// This is used for the space-separated argument list specified with
// the -dwarf-debug-flags option.
static void EscapeSpacesAndBackslashes(const char *Arg,
                                       SmallVectorImpl<char> &Res) {
  for (; *Arg; ++Arg) {
    switch (*Arg) {
    default:
      break;
    case ' ':
    case '\\':
      Res.push_back('\\');
      break;
    }
    Res.push_back(*Arg);
  }
}

// Quote target names for inclusion in GNU Make dependency files.
// Only the characters '$', '#', ' ', '\t' are quoted.
static void QuoteTarget(StringRef Target, SmallVectorImpl<char> &Res) {
  for (unsigned i = 0, e = Target.size(); i != e; ++i) {
    switch (Target[i]) {
    case ' ':
    case '\t':
      // Escape the preceding backslashes
      for (int j = i - 1; j >= 0 && Target[j] == '\\'; --j)
        Res.push_back('\\');

      // Escape the space/tab
      Res.push_back('\\');
      break;
    case '$':
      Res.push_back('$');
      break;
    case '#':
      Res.push_back('\\');
      break;
    default:
      break;
    }

    Res.push_back(Target[i]);
  }
}

/// Apply \a Work on the current tool chain \a RegularToolChain and any other
/// offloading tool chain that is associated with the current action \a JA.
static void
forAllAssociatedToolChains(Compilation &C, const JobAction &JA,
                           const ToolChain &RegularToolChain,
                           llvm::function_ref<void(const ToolChain &)> Work) {
  // Apply Work on the current/regular tool chain.
  Work(RegularToolChain);

  // Apply Work on all the offloading tool chains associated with the current
  // action.
  if (JA.isHostOffloading(Action::OFK_Cuda))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Cuda>());
  else if (JA.isDeviceOffloading(Action::OFK_Cuda))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());
  else if (JA.isHostOffloading(Action::OFK_HIP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_HIP>());
  else if (JA.isDeviceOffloading(Action::OFK_HIP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());

  if (JA.isHostOffloading(Action::OFK_OpenMP)) {
    auto TCs = C.getOffloadToolChains<Action::OFK_OpenMP>();
    for (auto II = TCs.first, IE = TCs.second; II != IE; ++II)
      Work(*II->second);
  } else if (JA.isDeviceOffloading(Action::OFK_OpenMP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());

  //
  // TODO: Add support for other offloading programming models here.
  //
}

/// This is a helper function for validating the optional refinement step
/// parameter in reciprocal argument strings. Return false if there is an error
/// parsing the refinement step. Otherwise, return true and set the Position
/// of the refinement step in the input string.
static bool getRefinementStep(StringRef In, const Driver &D,
                              const Arg &A, size_t &Position) {
  const char RefinementStepToken = ':';
  Position = In.find(RefinementStepToken);
  if (Position != StringRef::npos) {
    StringRef Option = A.getOption().getName();
    StringRef RefStep = In.substr(Position + 1);
    // Allow exactly one numeric character for the additional refinement
    // step parameter. This is reasonable for all currently-supported
    // operations and architectures because we would expect that a larger value
    // of refinement steps would cause the estimate "optimization" to
    // under-perform the native operation. Also, if the estimate does not
    // converge quickly, it probably will not ever converge, so further
    // refinement steps will not produce a better answer.
    if (RefStep.size() != 1) {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
    char RefStepChar = RefStep[0];
    if (RefStepChar < '0' || RefStepChar > '9') {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
  }
  return true;
}

/// The -mrecip flag requires processing of many optional parameters.
static void ParseMRecip(const Driver &D, const ArgList &Args,
                        ArgStringList &OutStrings) {
  StringRef DisabledPrefixIn = "!";
  StringRef DisabledPrefixOut = "!";
  StringRef EnabledPrefixOut = "";
  StringRef Out = "-mrecip=";

  Arg *A = Args.getLastArg(options::OPT_mrecip, options::OPT_mrecip_EQ);
  if (!A)
    return;

  unsigned NumOptions = A->getNumValues();
  if (NumOptions == 0) {
    // No option is the same as "all".
    OutStrings.push_back(Args.MakeArgString(Out + "all"));
    return;
  }

  // Pass through "all", "none", or "default" with an optional refinement step.
  if (NumOptions == 1) {
    StringRef Val = A->getValue(0);
    size_t RefStepLoc;
    if (!getRefinementStep(Val, D, *A, RefStepLoc))
      return;
    StringRef ValBase = Val.slice(0, RefStepLoc);
    if (ValBase == "all" || ValBase == "none" || ValBase == "default") {
      OutStrings.push_back(Args.MakeArgString(Out + Val));
      return;
    }
  }

  // Each reciprocal type may be enabled or disabled individually.
  // Check each input value for validity, concatenate them all back together,
  // and pass through.

  llvm::StringMap<bool> OptionStrings;
  OptionStrings.insert(std::make_pair("divd", false));
  OptionStrings.insert(std::make_pair("divf", false));
  OptionStrings.insert(std::make_pair("vec-divd", false));
  OptionStrings.insert(std::make_pair("vec-divf", false));
  OptionStrings.insert(std::make_pair("sqrtd", false));
  OptionStrings.insert(std::make_pair("sqrtf", false));
  OptionStrings.insert(std::make_pair("vec-sqrtd", false));
  OptionStrings.insert(std::make_pair("vec-sqrtf", false));

  for (unsigned i = 0; i != NumOptions; ++i) {
    StringRef Val = A->getValue(i);

    bool IsDisabled = Val.startswith(DisabledPrefixIn);
    // Ignore the disablement token for string matching.
    if (IsDisabled)
      Val = Val.substr(1);

    size_t RefStep;
    if (!getRefinementStep(Val, D, *A, RefStep))
      return;

    StringRef ValBase = Val.slice(0, RefStep);
    llvm::StringMap<bool>::iterator OptionIter = OptionStrings.find(ValBase);
    if (OptionIter == OptionStrings.end()) {
      // Try again specifying float suffix.
      OptionIter = OptionStrings.find(ValBase.str() + 'f');
      if (OptionIter == OptionStrings.end()) {
        // The input name did not match any known option string.
        D.Diag(diag::err_drv_unknown_argument) << Val;
        return;
      }
      // The option was specified without a float or double suffix.
      // Make sure that the double entry was not already specified.
      // The float entry will be checked below.
      if (OptionStrings[ValBase.str() + 'd']) {
        D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
        return;
      }
    }

    if (OptionIter->second == true) {
      // Duplicate option specified.
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
      return;
    }

    // Mark the matched option as found. Do not allow duplicate specifiers.
    OptionIter->second = true;

    // If the precision was not specified, also mark the double entry as found.
    if (ValBase.back() != 'f' && ValBase.back() != 'd')
      OptionStrings[ValBase.str() + 'd'] = true;

    // Build the output string.
    StringRef Prefix = IsDisabled ? DisabledPrefixOut : EnabledPrefixOut;
    Out = Args.MakeArgString(Out + Prefix + Val);
    if (i != NumOptions - 1)
      Out = Args.MakeArgString(Out + ",");
  }

  OutStrings.push_back(Args.MakeArgString(Out));
}

/// The -mprefer-vector-width option accepts either a positive integer
/// or the string "none".
static void ParseMPreferVectorWidth(const Driver &D, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  Arg *A = Args.getLastArg(options::OPT_mprefer_vector_width_EQ);
  if (!A)
    return;

  StringRef Value = A->getValue();
  if (Value == "none") {
    CmdArgs.push_back("-mprefer-vector-width=none");
  } else {
    unsigned Width;
    if (Value.getAsInteger(10, Width)) {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    CmdArgs.push_back(Args.MakeArgString("-mprefer-vector-width=" + Value));
  }
}

static void getWebAssemblyTargetFeatures(const ArgList &Args,
                                         std::vector<StringRef> &Features) {
  handleTargetFeaturesGroup(Args, Features, options::OPT_m_wasm_Features_Group);
}

static void getTargetFeatures(const ToolChain &TC, const llvm::Triple &Triple,
                              const ArgList &Args, ArgStringList &CmdArgs,
                              bool ForAS) {
  const Driver &D = TC.getDriver();
  std::vector<StringRef> Features;
  switch (Triple.getArch()) {
  default:
    break;
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    mips::getMIPSTargetFeatures(D, Triple, Args, Features);
    break;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    arm::getARMTargetFeatures(TC, Triple, Args, CmdArgs, Features, ForAS);
    break;

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    ppc::getPPCTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    riscv::getRISCVTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::systemz:
    systemz::getSystemZTargetFeatures(Args, Features);
    break;
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    aarch64::getAArch64TargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    x86::getX86TargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::hexagon:
    hexagon::getHexagonTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    getWebAssemblyTargetFeatures(Args, Features);
    break;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    sparc::getSparcTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    amdgpu::getAMDGPUTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::msp430:
    msp430::getMSP430TargetFeatures(D, Args, Features);
  }

  // Find the last of each feature.
  llvm::StringMap<unsigned> LastOpt;
  for (unsigned I = 0, N = Features.size(); I < N; ++I) {
    StringRef Name = Features[I];
    assert(Name[0] == '-' || Name[0] == '+');
    LastOpt[Name.drop_front(1)] = I;
  }

  for (unsigned I = 0, N = Features.size(); I < N; ++I) {
    // If this feature was overridden, ignore it.
    StringRef Name = Features[I];
    llvm::StringMap<unsigned>::iterator LastI = LastOpt.find(Name.drop_front(1));
    assert(LastI != LastOpt.end());
    unsigned Last = LastI->second;
    if (Last != I)
      continue;

    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back(Name.data());
  }
}

static bool
shouldUseExceptionTablesForObjCExceptions(const ObjCRuntime &runtime,
                                          const llvm::Triple &Triple) {
  // We use the zero-cost exception tables for Objective-C if the non-fragile
  // ABI is enabled or when compiling for x86_64 and ARM on Snow Leopard and
  // later.
  if (runtime.isNonFragile())
    return true;

  if (!Triple.isMacOSX())
    return false;

  return (!Triple.isMacOSXVersionLT(10, 5) &&
          (Triple.getArch() == llvm::Triple::x86_64 ||
           Triple.getArch() == llvm::Triple::arm));
}

/// Adds exception related arguments to the driver command arguments. There's a
/// master flag, -fexceptions and also language specific flags to enable/disable
/// C++ and Objective-C exceptions. This makes it possible to for example
/// disable C++ exceptions but enable Objective-C exceptions.
static void addExceptionArgs(const ArgList &Args, types::ID InputType,
                             const ToolChain &TC, bool KernelOrKext,
                             const ObjCRuntime &objcRuntime,
                             ArgStringList &CmdArgs) {
  const llvm::Triple &Triple = TC.getTriple();

  if (KernelOrKext) {
    // -mkernel and -fapple-kext imply no exceptions, so claim exception related
    // arguments now to avoid warnings about unused arguments.
    Args.ClaimAllArgs(options::OPT_fexceptions);
    Args.ClaimAllArgs(options::OPT_fno_exceptions);
    Args.ClaimAllArgs(options::OPT_fobjc_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_objc_exceptions);
    Args.ClaimAllArgs(options::OPT_fcxx_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_cxx_exceptions);
    return;
  }

  // See if the user explicitly enabled exceptions.
  bool EH = Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                         false);

  // Obj-C exceptions are enabled by default, regardless of -fexceptions. This
  // is not necessarily sensible, but follows GCC.
  if (types::isObjC(InputType) &&
      Args.hasFlag(options::OPT_fobjc_exceptions,
                   options::OPT_fno_objc_exceptions, true)) {
    CmdArgs.push_back("-fobjc-exceptions");

    EH |= shouldUseExceptionTablesForObjCExceptions(objcRuntime, Triple);
  }

  if (types::isCXX(InputType)) {
    // Disable C++ EH by default on XCore and PS4.
    bool CXXExceptionsEnabled =
        Triple.getArch() != llvm::Triple::xcore && !Triple.isPS4CPU();
    Arg *ExceptionArg = Args.getLastArg(
        options::OPT_fcxx_exceptions, options::OPT_fno_cxx_exceptions,
        options::OPT_fexceptions, options::OPT_fno_exceptions);
    if (ExceptionArg)
      CXXExceptionsEnabled =
          ExceptionArg->getOption().matches(options::OPT_fcxx_exceptions) ||
          ExceptionArg->getOption().matches(options::OPT_fexceptions);

    if (CXXExceptionsEnabled) {
      CmdArgs.push_back("-fcxx-exceptions");

      EH = true;
    }
  }

  if (EH)
    CmdArgs.push_back("-fexceptions");
}

static bool ShouldDisableAutolink(const ArgList &Args, const ToolChain &TC) {
  bool Default = true;
  if (TC.getTriple().isOSDarwin()) {
    // The native darwin assembler doesn't support the linker_option directives,
    // so we disable them if we think the .s file will be passed to it.
    Default = TC.useIntegratedAs();
  }
  return !Args.hasFlag(options::OPT_fautolink, options::OPT_fno_autolink,
                       Default);
}

static bool ShouldDisableDwarfDirectory(const ArgList &Args,
                                        const ToolChain &TC) {
  bool UseDwarfDirectory =
      Args.hasFlag(options::OPT_fdwarf_directory_asm,
                   options::OPT_fno_dwarf_directory_asm, TC.useIntegratedAs());
  return !UseDwarfDirectory;
}

// Convert an arg of the form "-gN" or "-ggdbN" or one of their aliases
// to the corresponding DebugInfoKind.
static codegenoptions::DebugInfoKind DebugLevelToInfoKind(const Arg &A) {
  assert(A.getOption().matches(options::OPT_gN_Group) &&
         "Not a -g option that specifies a debug-info level");
  if (A.getOption().matches(options::OPT_g0) ||
      A.getOption().matches(options::OPT_ggdb0))
    return codegenoptions::NoDebugInfo;
  if (A.getOption().matches(options::OPT_gline_tables_only) ||
      A.getOption().matches(options::OPT_ggdb1))
    return codegenoptions::DebugLineTablesOnly;
  if (A.getOption().matches(options::OPT_gline_directives_only))
    return codegenoptions::DebugDirectivesOnly;
  return codegenoptions::LimitedDebugInfo;
}

static bool mustUseNonLeafFramePointerForTarget(const llvm::Triple &Triple) {
  switch (Triple.getArch()){
  default:
    return false;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    // ARM Darwin targets require a frame pointer to be always present to aid
    // offline debugging via backtraces.
    return Triple.isOSDarwin();
  }
}

static bool useFramePointerForTargetByDefault(const ArgList &Args,
                                              const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  case llvm::Triple::xcore:
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    // XCore never wants frame pointers, regardless of OS.
    // WebAssembly never wants frame pointers.
    return false;
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    return !areOptimizationsEnabled(Args);
  default:
    break;
  }

  if (Triple.isOSNetBSD()) {
    return !areOptimizationsEnabled(Args);
  }

  if (Triple.isOSLinux() || Triple.getOS() == llvm::Triple::CloudABI ||
      Triple.isOSHurd()) {
    switch (Triple.getArch()) {
    // Don't use a frame pointer on linux if optimizing for certain targets.
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
    case llvm::Triple::ppc64le:
    case llvm::Triple::systemz:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      return !areOptimizationsEnabled(Args);
    default:
      return true;
    }
  }

  if (Triple.isOSWindows()) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86:
      return !areOptimizationsEnabled(Args);
    case llvm::Triple::x86_64:
      return Triple.isOSBinFormatMachO();
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      // Windows on ARM builds with FPO disabled to aid fast stack walking
      return true;
    default:
      // All other supported Windows ISAs use xdata unwind information, so frame
      // pointers are not generally useful.
      return false;
    }
  }

  return true;
}

static bool shouldUseFramePointer(const ArgList &Args,
                                  const llvm::Triple &Triple) {
  if (Arg *A = Args.getLastArg(options::OPT_fno_omit_frame_pointer,
                               options::OPT_fomit_frame_pointer))
    return A->getOption().matches(options::OPT_fno_omit_frame_pointer) ||
           mustUseNonLeafFramePointerForTarget(Triple);

  if (Args.hasArg(options::OPT_pg))
    return true;

  return useFramePointerForTargetByDefault(Args, Triple);
}

static bool shouldUseLeafFramePointer(const ArgList &Args,
                                      const llvm::Triple &Triple) {
  if (Arg *A = Args.getLastArg(options::OPT_mno_omit_leaf_frame_pointer,
                               options::OPT_momit_leaf_frame_pointer))
    return A->getOption().matches(options::OPT_mno_omit_leaf_frame_pointer);

  if (Args.hasArg(options::OPT_pg))
    return true;

  if (Triple.isPS4CPU())
    return false;

  return useFramePointerForTargetByDefault(Args, Triple);
}

/// Add a CC1 option to specify the debug compilation directory.
static void addDebugCompDirArg(const ArgList &Args, ArgStringList &CmdArgs) {
  SmallString<128> cwd;
  if (!llvm::sys::fs::current_path(cwd)) {
    CmdArgs.push_back("-fdebug-compilation-dir");
    CmdArgs.push_back(Args.MakeArgString(cwd));
  }
}

/// Add a CC1 and CC1AS option to specify the debug file path prefix map.
static void addDebugPrefixMapArg(const Driver &D, const ArgList &Args, ArgStringList &CmdArgs) {
  for (const Arg *A : Args.filtered(options::OPT_fdebug_prefix_map_EQ)) {
    StringRef Map = A->getValue();
    if (Map.find('=') == StringRef::npos)
      D.Diag(diag::err_drv_invalid_argument_to_fdebug_prefix_map) << Map;
    else
      CmdArgs.push_back(Args.MakeArgString("-fdebug-prefix-map=" + Map));
    A->claim();
  }
}

/// Vectorize at all optimization levels greater than 1 except for -Oz.
/// For -Oz the loop vectorizer is disable, while the slp vectorizer is enabled.
static bool shouldEnableVectorizerAtOLevel(const ArgList &Args, bool isSlpVec) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      return true;

    if (A->getOption().matches(options::OPT_O0))
      return false;

    assert(A->getOption().matches(options::OPT_O) && "Must have a -O flag");

    // Vectorize -Os.
    StringRef S(A->getValue());
    if (S == "s")
      return true;

    // Don't vectorize -Oz, unless it's the slp vectorizer.
    if (S == "z")
      return isSlpVec;

    unsigned OptLevel = 0;
    if (S.getAsInteger(10, OptLevel))
      return false;

    return OptLevel > 1;
  }

  return false;
}

/// Add -x lang to \p CmdArgs for \p Input.
static void addDashXForInput(const ArgList &Args, const InputInfo &Input,
                             ArgStringList &CmdArgs) {
  // When using -verify-pch, we don't want to provide the type
  // 'precompiled-header' if it was inferred from the file extension
  if (Args.hasArg(options::OPT_verify_pch) && Input.getType() == types::TY_PCH)
    return;

  CmdArgs.push_back("-x");
  if (Args.hasArg(options::OPT_rewrite_objc))
    CmdArgs.push_back(types::getTypeName(types::TY_PP_ObjCXX));
  else {
    // Map the driver type to the frontend type. This is mostly an identity
    // mapping, except that the distinction between module interface units
    // and other source files does not exist at the frontend layer.
    const char *ClangType;
    switch (Input.getType()) {
    case types::TY_CXXModule:
      ClangType = "c++";
      break;
    case types::TY_PP_CXXModule:
      ClangType = "c++-cpp-output";
      break;
    default:
      ClangType = types::getTypeName(Input.getType());
      break;
    }
    CmdArgs.push_back(ClangType);
  }
}

static void appendUserToPath(SmallVectorImpl<char> &Result) {
#ifdef LLVM_ON_UNIX
  const char *Username = getenv("LOGNAME");
#else
  const char *Username = getenv("USERNAME");
#endif
  if (Username) {
    // Validate that LoginName can be used in a path, and get its length.
    size_t Len = 0;
    for (const char *P = Username; *P; ++P, ++Len) {
      if (!clang::isAlphanumeric(*P) && *P != '_') {
        Username = nullptr;
        break;
      }
    }

    if (Username && Len > 0) {
      Result.append(Username, Username + Len);
      return;
    }
  }

// Fallback to user id.
#ifdef LLVM_ON_UNIX
  std::string UID = llvm::utostr(getuid());
#else
  // FIXME: Windows seems to have an 'SID' that might work.
  std::string UID = "9999";
#endif
  Result.append(UID.begin(), UID.end());
}

static void addPGOAndCoverageFlags(Compilation &C, const Driver &D,
                                   const InputInfo &Output, const ArgList &Args,
                                   ArgStringList &CmdArgs) {

  auto *PGOGenerateArg = Args.getLastArg(options::OPT_fprofile_generate,
                                         options::OPT_fprofile_generate_EQ,
                                         options::OPT_fno_profile_generate);
  if (PGOGenerateArg &&
      PGOGenerateArg->getOption().matches(options::OPT_fno_profile_generate))
    PGOGenerateArg = nullptr;

  auto *ProfileGenerateArg = Args.getLastArg(
      options::OPT_fprofile_instr_generate,
      options::OPT_fprofile_instr_generate_EQ,
      options::OPT_fno_profile_instr_generate);
  if (ProfileGenerateArg &&
      ProfileGenerateArg->getOption().matches(
          options::OPT_fno_profile_instr_generate))
    ProfileGenerateArg = nullptr;

  if (PGOGenerateArg && ProfileGenerateArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << PGOGenerateArg->getSpelling() << ProfileGenerateArg->getSpelling();

  auto *ProfileUseArg = getLastProfileUseArg(Args);

  if (PGOGenerateArg && ProfileUseArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << ProfileUseArg->getSpelling() << PGOGenerateArg->getSpelling();

  if (ProfileGenerateArg && ProfileUseArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << ProfileGenerateArg->getSpelling() << ProfileUseArg->getSpelling();

  if (ProfileGenerateArg) {
    if (ProfileGenerateArg->getOption().matches(
            options::OPT_fprofile_instr_generate_EQ))
      CmdArgs.push_back(Args.MakeArgString(Twine("-fprofile-instrument-path=") +
                                           ProfileGenerateArg->getValue()));
    // The default is to use Clang Instrumentation.
    CmdArgs.push_back("-fprofile-instrument=clang");
  }

  if (PGOGenerateArg) {
    CmdArgs.push_back("-fprofile-instrument=llvm");
    if (PGOGenerateArg->getOption().matches(
            options::OPT_fprofile_generate_EQ)) {
      SmallString<128> Path(PGOGenerateArg->getValue());
      llvm::sys::path::append(Path, "default_%m.profraw");
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-fprofile-instrument-path=") + Path));
    }
  }

  if (ProfileUseArg) {
    if (ProfileUseArg->getOption().matches(options::OPT_fprofile_instr_use_EQ))
      CmdArgs.push_back(Args.MakeArgString(
          Twine("-fprofile-instrument-use-path=") + ProfileUseArg->getValue()));
    else if ((ProfileUseArg->getOption().matches(
                  options::OPT_fprofile_use_EQ) ||
              ProfileUseArg->getOption().matches(
                  options::OPT_fprofile_instr_use))) {
      SmallString<128> Path(
          ProfileUseArg->getNumValues() == 0 ? "" : ProfileUseArg->getValue());
      if (Path.empty() || llvm::sys::fs::is_directory(Path))
        llvm::sys::path::append(Path, "default.profdata");
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-fprofile-instrument-use-path=") + Path));
    }
  }

  if (Args.hasArg(options::OPT_ftest_coverage) ||
      Args.hasArg(options::OPT_coverage))
    CmdArgs.push_back("-femit-coverage-notes");
  if (Args.hasFlag(options::OPT_fprofile_arcs, options::OPT_fno_profile_arcs,
                   false) ||
      Args.hasArg(options::OPT_coverage))
    CmdArgs.push_back("-femit-coverage-data");

  if (Args.hasFlag(options::OPT_fcoverage_mapping,
                   options::OPT_fno_coverage_mapping, false)) {
    if (!ProfileGenerateArg)
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fcoverage-mapping"
          << "-fprofile-instr-generate";

    CmdArgs.push_back("-fcoverage-mapping");
  }

  if (Args.hasArg(options::OPT_fprofile_exclude_files_EQ)) {
    auto *Arg = Args.getLastArg(options::OPT_fprofile_exclude_files_EQ);
    if (!Args.hasArg(options::OPT_coverage))
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fprofile-exclude-files="
          << "--coverage";

    StringRef v = Arg->getValue();
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fprofile-exclude-files=" + v)));
  }

  if (Args.hasArg(options::OPT_fprofile_filter_files_EQ)) {
    auto *Arg = Args.getLastArg(options::OPT_fprofile_filter_files_EQ);
    if (!Args.hasArg(options::OPT_coverage))
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fprofile-filter-files="
          << "--coverage";

    StringRef v = Arg->getValue();
    CmdArgs.push_back(Args.MakeArgString(Twine("-fprofile-filter-files=" + v)));
  }

  if (C.getArgs().hasArg(options::OPT_c) ||
      C.getArgs().hasArg(options::OPT_S)) {
    if (Output.isFilename()) {
      CmdArgs.push_back("-coverage-notes-file");
      SmallString<128> OutputFilename;
      if (Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o))
        OutputFilename = FinalOutput->getValue();
      else
        OutputFilename = llvm::sys::path::filename(Output.getBaseInput());
      SmallString<128> CoverageFilename = OutputFilename;
      if (llvm::sys::path::is_relative(CoverageFilename)) {
        SmallString<128> Pwd;
        if (!llvm::sys::fs::current_path(Pwd)) {
          llvm::sys::path::append(Pwd, CoverageFilename);
          CoverageFilename.swap(Pwd);
        }
      }
      llvm::sys::path::replace_extension(CoverageFilename, "gcno");
      CmdArgs.push_back(Args.MakeArgString(CoverageFilename));

      // Leave -fprofile-dir= an unused argument unless .gcda emission is
      // enabled. To be polite, with '-fprofile-arcs -fno-profile-arcs' consider
      // the flag used. There is no -fno-profile-dir, so the user has no
      // targeted way to suppress the warning.
      if (Args.hasArg(options::OPT_fprofile_arcs) ||
          Args.hasArg(options::OPT_coverage)) {
        CmdArgs.push_back("-coverage-data-file");
        if (Arg *FProfileDir = Args.getLastArg(options::OPT_fprofile_dir)) {
          CoverageFilename = FProfileDir->getValue();
          llvm::sys::path::append(CoverageFilename, OutputFilename);
        }
        llvm::sys::path::replace_extension(CoverageFilename, "gcda");
        CmdArgs.push_back(Args.MakeArgString(CoverageFilename));
      }
    }
  }
}

/// Check whether the given input tree contains any compilation actions.
static bool ContainsCompileAction(const Action *A) {
  if (isa<CompileJobAction>(A) || isa<BackendJobAction>(A))
    return true;

  for (const auto &AI : A->inputs())
    if (ContainsCompileAction(AI))
      return true;

  return false;
}

/// Check if -relax-all should be passed to the internal assembler.
/// This is done by default when compiling non-assembler source with -O0.
static bool UseRelaxAll(Compilation &C, const ArgList &Args) {
  bool RelaxDefault = true;

  if (Arg *A = Args.getLastArg(options::OPT_O_Group))
    RelaxDefault = A->getOption().matches(options::OPT_O0);

  if (RelaxDefault) {
    RelaxDefault = false;
    for (const auto &Act : C.getActions()) {
      if (ContainsCompileAction(Act)) {
        RelaxDefault = true;
        break;
      }
    }
  }

  return Args.hasFlag(options::OPT_mrelax_all, options::OPT_mno_relax_all,
                      RelaxDefault);
}

// Extract the integer N from a string spelled "-dwarf-N", returning 0
// on mismatch. The StringRef input (rather than an Arg) allows
// for use by the "-Xassembler" option parser.
static unsigned DwarfVersionNum(StringRef ArgValue) {
  return llvm::StringSwitch<unsigned>(ArgValue)
      .Case("-gdwarf-2", 2)
      .Case("-gdwarf-3", 3)
      .Case("-gdwarf-4", 4)
      .Case("-gdwarf-5", 5)
      .Default(0);
}

static void RenderDebugEnablingArgs(const ArgList &Args, ArgStringList &CmdArgs,
                                    codegenoptions::DebugInfoKind DebugInfoKind,
                                    unsigned DwarfVersion,
                                    llvm::DebuggerKind DebuggerTuning) {
  switch (DebugInfoKind) {
  case codegenoptions::DebugDirectivesOnly:
    CmdArgs.push_back("-debug-info-kind=line-directives-only");
    break;
  case codegenoptions::DebugLineTablesOnly:
    CmdArgs.push_back("-debug-info-kind=line-tables-only");
    break;
  case codegenoptions::LimitedDebugInfo:
    CmdArgs.push_back("-debug-info-kind=limited");
    break;
  case codegenoptions::FullDebugInfo:
    CmdArgs.push_back("-debug-info-kind=standalone");
    break;
  default:
    break;
  }
  if (DwarfVersion > 0)
    CmdArgs.push_back(
        Args.MakeArgString("-dwarf-version=" + Twine(DwarfVersion)));
  switch (DebuggerTuning) {
  case llvm::DebuggerKind::GDB:
    CmdArgs.push_back("-debugger-tuning=gdb");
    break;
  case llvm::DebuggerKind::LLDB:
    CmdArgs.push_back("-debugger-tuning=lldb");
    break;
  case llvm::DebuggerKind::SCE:
    CmdArgs.push_back("-debugger-tuning=sce");
    break;
  default:
    break;
  }
}

static bool checkDebugInfoOption(const Arg *A, const ArgList &Args,
                                 const Driver &D, const ToolChain &TC) {
  assert(A && "Expected non-nullptr argument.");
  if (TC.supportsDebugInfoOption(A))
    return true;
  D.Diag(diag::warn_drv_unsupported_debug_info_opt_for_target)
      << A->getAsString(Args) << TC.getTripleString();
  return false;
}

static void RenderDebugInfoCompressionArgs(const ArgList &Args,
                                           ArgStringList &CmdArgs,
                                           const Driver &D,
                                           const ToolChain &TC) {
  const Arg *A = Args.getLastArg(options::OPT_gz, options::OPT_gz_EQ);
  if (!A)
    return;
  if (checkDebugInfoOption(A, Args, D, TC)) {
    if (A->getOption().getID() == options::OPT_gz) {
      if (llvm::zlib::isAvailable())
        CmdArgs.push_back("-compress-debug-sections");
      else
        D.Diag(diag::warn_debug_compression_unavailable);
      return;
    }

    StringRef Value = A->getValue();
    if (Value == "none") {
      CmdArgs.push_back("-compress-debug-sections=none");
    } else if (Value == "zlib" || Value == "zlib-gnu") {
      if (llvm::zlib::isAvailable()) {
        CmdArgs.push_back(
            Args.MakeArgString("-compress-debug-sections=" + Twine(Value)));
      } else {
        D.Diag(diag::warn_debug_compression_unavailable);
      }
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getOption().getName() << Value;
    }
  }
}

static const char *RelocationModelName(llvm::Reloc::Model Model) {
  switch (Model) {
  case llvm::Reloc::Static:
    return "static";
  case llvm::Reloc::PIC_:
    return "pic";
  case llvm::Reloc::DynamicNoPIC:
    return "dynamic-no-pic";
  case llvm::Reloc::ROPI:
    return "ropi";
  case llvm::Reloc::RWPI:
    return "rwpi";
  case llvm::Reloc::ROPI_RWPI:
    return "ropi-rwpi";
  }
  llvm_unreachable("Unknown Reloc::Model kind");
}

void Clang::AddPreprocessingOptions(Compilation &C, const JobAction &JA,
                                    const Driver &D, const ArgList &Args,
                                    ArgStringList &CmdArgs,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs) const {
  Arg *A;
  const bool IsIAMCU = getToolChain().getTriple().isOSIAMCU();

  CheckPreprocessingOptions(D, Args);

  Args.AddLastArg(CmdArgs, options::OPT_C);
  Args.AddLastArg(CmdArgs, options::OPT_CC);

  // Handle dependency file generation.
  if ((A = Args.getLastArg(options::OPT_M, options::OPT_MM)) ||
      (A = Args.getLastArg(options::OPT_MD)) ||
      (A = Args.getLastArg(options::OPT_MMD))) {
    // Determine the output location.
    const char *DepFile;
    if (Arg *MF = Args.getLastArg(options::OPT_MF)) {
      DepFile = MF->getValue();
      C.addFailureResultFile(DepFile, &JA);
    } else if (Output.getType() == types::TY_Dependencies) {
      DepFile = Output.getFilename();
    } else if (A->getOption().matches(options::OPT_M) ||
               A->getOption().matches(options::OPT_MM)) {
      DepFile = "-";
    } else {
      DepFile = getDependencyFileName(Args, Inputs);
      C.addFailureResultFile(DepFile, &JA);
    }
    CmdArgs.push_back("-dependency-file");
    CmdArgs.push_back(DepFile);

    // Add a default target if one wasn't specified.
    if (!Args.hasArg(options::OPT_MT) && !Args.hasArg(options::OPT_MQ)) {
      const char *DepTarget;

      // If user provided -o, that is the dependency target, except
      // when we are only generating a dependency file.
      Arg *OutputOpt = Args.getLastArg(options::OPT_o);
      if (OutputOpt && Output.getType() != types::TY_Dependencies) {
        DepTarget = OutputOpt->getValue();
      } else {
        // Otherwise derive from the base input.
        //
        // FIXME: This should use the computed output file location.
        SmallString<128> P(Inputs[0].getBaseInput());
        llvm::sys::path::replace_extension(P, "o");
        DepTarget = Args.MakeArgString(llvm::sys::path::filename(P));
      }

      if (!A->getOption().matches(options::OPT_MD) && !A->getOption().matches(options::OPT_MMD)) {
        CmdArgs.push_back("-w");
      }
      CmdArgs.push_back("-MT");
      SmallString<128> Quoted;
      QuoteTarget(DepTarget, Quoted);
      CmdArgs.push_back(Args.MakeArgString(Quoted));
    }

    if (A->getOption().matches(options::OPT_M) ||
        A->getOption().matches(options::OPT_MD))
      CmdArgs.push_back("-sys-header-deps");
    if ((isa<PrecompileJobAction>(JA) &&
         !Args.hasArg(options::OPT_fno_module_file_deps)) ||
        Args.hasArg(options::OPT_fmodule_file_deps))
      CmdArgs.push_back("-module-file-deps");
  }

  if (Args.hasArg(options::OPT_MG)) {
    if (!A || A->getOption().matches(options::OPT_MD) ||
        A->getOption().matches(options::OPT_MMD))
      D.Diag(diag::err_drv_mg_requires_m_or_mm);
    CmdArgs.push_back("-MG");
  }

  Args.AddLastArg(CmdArgs, options::OPT_MP);
  Args.AddLastArg(CmdArgs, options::OPT_MV);

  // Convert all -MQ <target> args to -MT <quoted target>
  for (const Arg *A : Args.filtered(options::OPT_MT, options::OPT_MQ)) {
    A->claim();

    if (A->getOption().matches(options::OPT_MQ)) {
      CmdArgs.push_back("-MT");
      SmallString<128> Quoted;
      QuoteTarget(A->getValue(), Quoted);
      CmdArgs.push_back(Args.MakeArgString(Quoted));

      // -MT flag - no change
    } else {
      A->render(Args, CmdArgs);
    }
  }

  // Add offload include arguments specific for CUDA.  This must happen before
  // we -I or -include anything else, because we must pick up the CUDA headers
  // from the particular CUDA installation, rather than from e.g.
  // /usr/local/include.
  if (JA.isOffloading(Action::OFK_Cuda))
    getToolChain().AddCudaIncludeArgs(Args, CmdArgs);

  // Add -i* options, and automatically translate to
  // -include-pch/-include-pth for transparent PCH support. It's
  // wonky, but we include looking for .gch so we can support seamless
  // replacement into a build system already set up to be generating
  // .gch files.

  if (getToolChain().getDriver().IsCLMode()) {
    const Arg *YcArg = Args.getLastArg(options::OPT__SLASH_Yc);
    const Arg *YuArg = Args.getLastArg(options::OPT__SLASH_Yu);
    if (YcArg && JA.getKind() >= Action::PrecompileJobClass &&
        JA.getKind() <= Action::AssembleJobClass) {
      CmdArgs.push_back(Args.MakeArgString("-building-pch-with-obj"));
    }
    if (YcArg || YuArg) {
      StringRef ThroughHeader = YcArg ? YcArg->getValue() : YuArg->getValue();
      if (!isa<PrecompileJobAction>(JA)) {
        CmdArgs.push_back("-include-pch");
        CmdArgs.push_back(Args.MakeArgString(D.GetClPchPath(
            C, !ThroughHeader.empty()
                   ? ThroughHeader
                   : llvm::sys::path::filename(Inputs[0].getBaseInput()))));
      }

      if (ThroughHeader.empty()) {
        CmdArgs.push_back(Args.MakeArgString(
            Twine("-pch-through-hdrstop-") + (YcArg ? "create" : "use")));
      } else {
        CmdArgs.push_back(
            Args.MakeArgString(Twine("-pch-through-header=") + ThroughHeader));
      }
    }
  }

  bool RenderedImplicitInclude = false;
  for (const Arg *A : Args.filtered(options::OPT_clang_i_Group)) {
    if (A->getOption().matches(options::OPT_include)) {
      // Handling of gcc-style gch precompiled headers.
      bool IsFirstImplicitInclude = !RenderedImplicitInclude;
      RenderedImplicitInclude = true;

      bool FoundPCH = false;
      SmallString<128> P(A->getValue());
      // We want the files to have a name like foo.h.pch. Add a dummy extension
      // so that replace_extension does the right thing.
      P += ".dummy";
      llvm::sys::path::replace_extension(P, "pch");
      if (llvm::sys::fs::exists(P))
        FoundPCH = true;

      if (!FoundPCH) {
        llvm::sys::path::replace_extension(P, "gch");
        if (llvm::sys::fs::exists(P)) {
          FoundPCH = true;
        }
      }

      if (FoundPCH) {
        if (IsFirstImplicitInclude) {
          A->claim();
          CmdArgs.push_back("-include-pch");
          CmdArgs.push_back(Args.MakeArgString(P));
          continue;
        } else {
          // Ignore the PCH if not first on command line and emit warning.
          D.Diag(diag::warn_drv_pch_not_first_include) << P
                                                       << A->getAsString(Args);
        }
      }
    } else if (A->getOption().matches(options::OPT_isystem_after)) {
      // Handling of paths which must come late.  These entries are handled by
      // the toolchain itself after the resource dir is inserted in the right
      // search order.
      // Do not claim the argument so that the use of the argument does not
      // silently go unnoticed on toolchains which do not honour the option.
      continue;
    }

    // Not translated, render as usual.
    A->claim();
    A->render(Args, CmdArgs);
  }

  Args.AddAllArgs(CmdArgs,
                  {options::OPT_D, options::OPT_U, options::OPT_I_Group,
                   options::OPT_F, options::OPT_index_header_map});

  // Add -Wp, and -Xpreprocessor if using the preprocessor.

  // FIXME: There is a very unfortunate problem here, some troubled
  // souls abuse -Wp, to pass preprocessor options in gcc syntax. To
  // really support that we would have to parse and then translate
  // those options. :(
  Args.AddAllArgValues(CmdArgs, options::OPT_Wp_COMMA,
                       options::OPT_Xpreprocessor);

  // -I- is a deprecated GCC feature, reject it.
  if (Arg *A = Args.getLastArg(options::OPT_I_))
    D.Diag(diag::err_drv_I_dash_not_supported) << A->getAsString(Args);

  // If we have a --sysroot, and don't have an explicit -isysroot flag, add an
  // -isysroot to the CC1 invocation.
  StringRef sysroot = C.getSysRoot();
  if (sysroot != "") {
    if (!Args.hasArg(options::OPT_isysroot)) {
      CmdArgs.push_back("-isysroot");
      CmdArgs.push_back(C.getArgs().MakeArgString(sysroot));
    }
  }

  // Parse additional include paths from environment variables.
  // FIXME: We should probably sink the logic for handling these from the
  // frontend into the driver. It will allow deleting 4 otherwise unused flags.
  // CPATH - included following the user specified includes (but prior to
  // builtin and standard includes).
  addDirectoryList(Args, CmdArgs, "-I", "CPATH");
  // C_INCLUDE_PATH - system includes enabled when compiling C.
  addDirectoryList(Args, CmdArgs, "-c-isystem", "C_INCLUDE_PATH");
  // CPLUS_INCLUDE_PATH - system includes enabled when compiling C++.
  addDirectoryList(Args, CmdArgs, "-cxx-isystem", "CPLUS_INCLUDE_PATH");
  // OBJC_INCLUDE_PATH - system includes enabled when compiling ObjC.
  addDirectoryList(Args, CmdArgs, "-objc-isystem", "OBJC_INCLUDE_PATH");
  // OBJCPLUS_INCLUDE_PATH - system includes enabled when compiling ObjC++.
  addDirectoryList(Args, CmdArgs, "-objcxx-isystem", "OBJCPLUS_INCLUDE_PATH");

  // While adding the include arguments, we also attempt to retrieve the
  // arguments of related offloading toolchains or arguments that are specific
  // of an offloading programming model.

  // Add C++ include arguments, if needed.
  if (types::isCXX(Inputs[0].getType()))
    forAllAssociatedToolChains(C, JA, getToolChain(),
                               [&Args, &CmdArgs](const ToolChain &TC) {
                                 TC.AddClangCXXStdlibIncludeArgs(Args, CmdArgs);
                               });

  // Add system include arguments for all targets but IAMCU.
  if (!IsIAMCU)
    forAllAssociatedToolChains(C, JA, getToolChain(),
                               [&Args, &CmdArgs](const ToolChain &TC) {
                                 TC.AddClangSystemIncludeArgs(Args, CmdArgs);
                               });
  else {
    // For IAMCU add special include arguments.
    getToolChain().AddIAMCUIncludeArgs(Args, CmdArgs);
  }
}

// FIXME: Move to target hook.
static bool isSignedCharDefault(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  default:
    return true;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    if (Triple.isOSDarwin() || Triple.isOSWindows())
      return true;
    return false;

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
    if (Triple.isOSDarwin())
      return true;
    return false;

  case llvm::Triple::hexagon:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::systemz:
  case llvm::Triple::xcore:
    return false;
  }
}

static bool isNoCommonDefault(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  default:
    if (Triple.isOSFuchsia())
      return true;
    return false;

  case llvm::Triple::xcore:
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return true;
  }
}

namespace {
void RenderARMABI(const llvm::Triple &Triple, const ArgList &Args,
                  ArgStringList &CmdArgs) {
  // Select the ABI to use.
  // FIXME: Support -meabi.
  // FIXME: Parts of this are duplicated in the backend, unify this somehow.
  const char *ABIName = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ)) {
    ABIName = A->getValue();
  } else {
    std::string CPU = getCPUName(Args, Triple, /*FromAs*/ false);
    ABIName = llvm::ARM::computeDefaultTargetABI(Triple, CPU).data();
  }

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}
}

void Clang::AddARMTargetArgs(const llvm::Triple &Triple, const ArgList &Args,
                             ArgStringList &CmdArgs, bool KernelOrKext) const {
  RenderARMABI(Triple, Args, CmdArgs);

  // Determine floating point ABI from the options & target defaults.
  arm::FloatABI ABI = arm::getARMFloatABI(getToolChain(), Args);
  if (ABI == arm::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    // FIXME: This changes CPP defines, we need -target-soft-float.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else if (ABI == arm::FloatABI::SoftFP) {
    // Floating point operations are hard, but argument passing is soft.
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(ABI == arm::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  // Forward the -mglobal-merge option for explicit control over the pass.
  if (Arg *A = Args.getLastArg(options::OPT_mglobal_merge,
                               options::OPT_mno_global_merge)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mno_global_merge))
      CmdArgs.push_back("-arm-global-merge=false");
    else
      CmdArgs.push_back("-arm-global-merge=true");
  }

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");
}

void Clang::RenderTargetOptions(const llvm::Triple &EffectiveTriple,
                                const ArgList &Args, bool KernelOrKext,
                                ArgStringList &CmdArgs) const {
  const ToolChain &TC = getToolChain();

  // Add the target features
  getTargetFeatures(TC, EffectiveTriple, Args, CmdArgs, false);

  // Add target specific flags.
  switch (TC.getArch()) {
  default:
    break;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    // Use the effective triple, which takes into account the deployment target.
    AddARMTargetArgs(EffectiveTriple, Args, CmdArgs, KernelOrKext);
    CmdArgs.push_back("-fallow-half-arguments-and-returns");
    break;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    AddAArch64TargetArgs(Args, CmdArgs);
    CmdArgs.push_back("-fallow-half-arguments-and-returns");
    break;

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    AddMIPSTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    AddPPCTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    AddRISCVTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    AddSparcTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::systemz:
    AddSystemZTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::lanai:
    AddLanaiTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::hexagon:
    AddHexagonTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    AddWebAssemblyTargetArgs(Args, CmdArgs);
    break;
  }
}

// Parse -mbranch-protection=<protection>[+<protection>]* where
//   <protection> ::= standard | none | [bti,pac-ret[+b-key,+leaf]*]
// Returns a triple of (return address signing Scope, signing key, require
// landing pads)
static std::tuple<StringRef, StringRef, bool>
ParseAArch64BranchProtection(const Driver &D, const ArgList &Args,
                             const Arg *A) {
  StringRef Scope = "none";
  StringRef Key = "a_key";
  bool IndirectBranches = false;

  StringRef Value = A->getValue();
  // This maps onto -mbranch-protection=<scope>+<key>

  if (Value.equals("standard")) {
    Scope = "non-leaf";
    Key = "a_key";
    IndirectBranches = true;

  } else if (!Value.equals("none")) {
    SmallVector<StringRef, 4> BranchProtection;
    StringRef(A->getValue()).split(BranchProtection, '+');

    auto Protection = BranchProtection.begin();
    while (Protection != BranchProtection.end()) {
      if (Protection->equals("bti"))
        IndirectBranches = true;
      else if (Protection->equals("pac-ret")) {
        Scope = "non-leaf";
        while (++Protection != BranchProtection.end()) {
          // Inner loop as "leaf" and "b-key" options must only appear attached
          // to pac-ret.
          if (Protection->equals("leaf"))
            Scope = "all";
          else if (Protection->equals("b-key"))
            Key = "b_key";
          else
            break;
        }
        Protection--;
      } else
        D.Diag(diag::err_invalid_branch_protection)
            << *Protection << A->getAsString(Args);
      Protection++;
    }
  }

  return std::make_tuple(Scope, Key, IndirectBranches);
}

namespace {
void RenderAArch64ABI(const llvm::Triple &Triple, const ArgList &Args,
                      ArgStringList &CmdArgs) {
  const char *ABIName = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    ABIName = A->getValue();
  else if (Triple.isOSDarwin())
    ABIName = "darwinpcs";
  else
    ABIName = "aapcs";

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}
}

void Clang::AddAArch64TargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fapple_kext))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");

  RenderAArch64ABI(Triple, Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_mfix_cortex_a53_835769,
                               options::OPT_mno_fix_cortex_a53_835769)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mfix_cortex_a53_835769))
      CmdArgs.push_back("-aarch64-fix-cortex-a53-835769=1");
    else
      CmdArgs.push_back("-aarch64-fix-cortex-a53-835769=0");
  } else if (Triple.isAndroid()) {
    // Enabled A53 errata (835769) workaround by default on android
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-aarch64-fix-cortex-a53-835769=1");
  }

  // Forward the -mglobal-merge option for explicit control over the pass.
  if (Arg *A = Args.getLastArg(options::OPT_mglobal_merge,
                               options::OPT_mno_global_merge)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mno_global_merge))
      CmdArgs.push_back("-aarch64-enable-global-merge=false");
    else
      CmdArgs.push_back("-aarch64-enable-global-merge=true");
  }

  // Enable/disable return address signing and indirect branch targets.
  if (Arg *A = Args.getLastArg(options::OPT_msign_return_address_EQ,
                               options::OPT_mbranch_protection_EQ)) {

    const Driver &D = getToolChain().getDriver();

    StringRef Scope, Key;
    bool IndirectBranches;

    if (A->getOption().matches(options::OPT_msign_return_address_EQ)) {
      Scope = A->getValue();
      if (!Scope.equals("none") && !Scope.equals("non-leaf") &&
          !Scope.equals("all"))
        D.Diag(diag::err_invalid_branch_protection)
            << Scope << A->getAsString(Args);
      Key = "a_key";
      IndirectBranches = false;
    } else
      std::tie(Scope, Key, IndirectBranches) =
          ParseAArch64BranchProtection(D, Args, A);

    CmdArgs.push_back(
        Args.MakeArgString(Twine("-msign-return-address=") + Scope));
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-msign-return-address-key=") + Key));
    if (IndirectBranches)
      CmdArgs.push_back("-mbranch-target-enforce");
  }
}

void Clang::AddMIPSTargetArgs(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();
  StringRef CPUName;
  StringRef ABIName;
  const llvm::Triple &Triple = getToolChain().getTriple();
  mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());

  mips::FloatABI ABI = mips::getMipsFloatABI(D, Args);
  if (ABI == mips::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(ABI == mips::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mxgot, options::OPT_mno_xgot)) {
    if (A->getOption().matches(options::OPT_mxgot)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mxgot");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mldc1_sdc1,
                               options::OPT_mno_ldc1_sdc1)) {
    if (A->getOption().matches(options::OPT_mno_ldc1_sdc1)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mno-ldc1-sdc1");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mcheck_zero_division,
                               options::OPT_mno_check_zero_division)) {
    if (A->getOption().matches(options::OPT_mno_check_zero_division)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mno-check-zero-division");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_G)) {
    StringRef v = A->getValue();
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-mips-ssection-threshold=" + v));
    A->claim();
  }

  Arg *GPOpt = Args.getLastArg(options::OPT_mgpopt, options::OPT_mno_gpopt);
  Arg *ABICalls =
      Args.getLastArg(options::OPT_mabicalls, options::OPT_mno_abicalls);

  // -mabicalls is the default for many MIPS environments, even with -fno-pic.
  // -mgpopt is the default for static, -fno-pic environments but these two
  // options conflict. We want to be certain that -mno-abicalls -mgpopt is
  // the only case where -mllvm -mgpopt is passed.
  // NOTE: We need a warning here or in the backend to warn when -mgpopt is
  //       passed explicitly when compiling something with -mabicalls
  //       (implictly) in affect. Currently the warning is in the backend.
  //
  // When the ABI in use is  N64, we also need to determine the PIC mode that
  // is in use, as -fno-pic for N64 implies -mno-abicalls.
  bool NoABICalls =
      ABICalls && ABICalls->getOption().matches(options::OPT_mno_abicalls);

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  NoABICalls = NoABICalls ||
               (RelocationModel == llvm::Reloc::Static && ABIName == "n64");

  bool WantGPOpt = GPOpt && GPOpt->getOption().matches(options::OPT_mgpopt);
  // We quietly ignore -mno-gpopt as the backend defaults to -mno-gpopt.
  if (NoABICalls && (!GPOpt || WantGPOpt)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-mgpopt");

    Arg *LocalSData = Args.getLastArg(options::OPT_mlocal_sdata,
                                      options::OPT_mno_local_sdata);
    Arg *ExternSData = Args.getLastArg(options::OPT_mextern_sdata,
                                       options::OPT_mno_extern_sdata);
    Arg *EmbeddedData = Args.getLastArg(options::OPT_membedded_data,
                                        options::OPT_mno_embedded_data);
    if (LocalSData) {
      CmdArgs.push_back("-mllvm");
      if (LocalSData->getOption().matches(options::OPT_mlocal_sdata)) {
        CmdArgs.push_back("-mlocal-sdata=1");
      } else {
        CmdArgs.push_back("-mlocal-sdata=0");
      }
      LocalSData->claim();
    }

    if (ExternSData) {
      CmdArgs.push_back("-mllvm");
      if (ExternSData->getOption().matches(options::OPT_mextern_sdata)) {
        CmdArgs.push_back("-mextern-sdata=1");
      } else {
        CmdArgs.push_back("-mextern-sdata=0");
      }
      ExternSData->claim();
    }

    if (EmbeddedData) {
      CmdArgs.push_back("-mllvm");
      if (EmbeddedData->getOption().matches(options::OPT_membedded_data)) {
        CmdArgs.push_back("-membedded-data=1");
      } else {
        CmdArgs.push_back("-membedded-data=0");
      }
      EmbeddedData->claim();
    }

  } else if ((!ABICalls || (!NoABICalls && ABICalls)) && WantGPOpt)
    D.Diag(diag::warn_drv_unsupported_gpopt) << (ABICalls ? 0 : 1);

  if (GPOpt)
    GPOpt->claim();

  if (Arg *A = Args.getLastArg(options::OPT_mcompact_branches_EQ)) {
    StringRef Val = StringRef(A->getValue());
    if (mips::hasCompactBranches(CPUName)) {
      if (Val == "never" || Val == "always" || Val == "optimal") {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back(Args.MakeArgString("-mips-compact-branches=" + Val));
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Val;
    } else
      D.Diag(diag::warn_target_unsupported_compact_branches) << CPUName;
  }

  if (Arg *A = Args.getLastArg(options::OPT_mrelax_pic_calls,
                               options::OPT_mno_relax_pic_calls)) {
    if (A->getOption().matches(options::OPT_mno_relax_pic_calls)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mips-jalr-reloc=0");
    }
  }
}

void Clang::AddPPCTargetArgs(const ArgList &Args,
                             ArgStringList &CmdArgs) const {
  // Select the ABI to use.
  const char *ABIName = nullptr;
  if (getToolChain().getTriple().isOSLinux())
    switch (getToolChain().getArch()) {
    case llvm::Triple::ppc64: {
      // When targeting a processor that supports QPX, or if QPX is
      // specifically enabled, default to using the ABI that supports QPX (so
      // long as it is not specifically disabled).
      bool HasQPX = false;
      if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
        HasQPX = A->getValue() == StringRef("a2q");
      HasQPX = Args.hasFlag(options::OPT_mqpx, options::OPT_mno_qpx, HasQPX);
      if (HasQPX) {
        ABIName = "elfv1-qpx";
        break;
      }

      ABIName = "elfv1";
      break;
    }
    case llvm::Triple::ppc64le:
      ABIName = "elfv2";
      break;
    default:
      break;
    }

  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    // The ppc64 linux abis are all "altivec" abis by default. Accept and ignore
    // the option if given as we don't have backend support for any targets
    // that don't use the altivec abi.
    if (StringRef(A->getValue()) != "altivec")
      ABIName = A->getValue();

  ppc::FloatABI FloatABI =
      ppc::getPPCFloatABI(getToolChain().getDriver(), Args);

  if (FloatABI == ppc::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(FloatABI == ppc::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  if (ABIName) {
    CmdArgs.push_back("-target-abi");
    CmdArgs.push_back(ABIName);
  }
}

void Clang::AddRISCVTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  // FIXME: currently defaults to the soft-float ABIs. Will need to be
  // expanded to select ilp32f, ilp32d, lp64f, lp64d when appropriate.
  const char *ABIName = nullptr;
  const llvm::Triple &Triple = getToolChain().getTriple();
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    ABIName = A->getValue();
  else if (Triple.getArch() == llvm::Triple::riscv32)
    ABIName = "ilp32";
  else if (Triple.getArch() == llvm::Triple::riscv64)
    ABIName = "lp64";
  else
    llvm_unreachable("Unexpected triple!");

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}

void Clang::AddSparcTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  sparc::FloatABI FloatABI =
      sparc::getSparcFloatABI(getToolChain().getDriver(), Args);

  if (FloatABI == sparc::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(FloatABI == sparc::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }
}

void Clang::AddSystemZTargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  if (Args.hasFlag(options::OPT_mbackchain, options::OPT_mno_backchain, false))
    CmdArgs.push_back("-mbackchain");
}

void Clang::AddX86TargetArgs(const ArgList &Args,
                             ArgStringList &CmdArgs) const {
  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fapple_kext))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mtls_direct_seg_refs,
                    options::OPT_mno_tls_direct_seg_refs, true))
    CmdArgs.push_back("-mno-tls-direct-seg-refs");

  // Default to avoid implicit floating-point for kernel/kext code, but allow
  // that to be overridden with -mno-soft-float.
  bool NoImplicitFloat = (Args.hasArg(options::OPT_mkernel) ||
                          Args.hasArg(options::OPT_fapple_kext));
  if (Arg *A = Args.getLastArg(
          options::OPT_msoft_float, options::OPT_mno_soft_float,
          options::OPT_mimplicit_float, options::OPT_mno_implicit_float)) {
    const Option &O = A->getOption();
    NoImplicitFloat = (O.matches(options::OPT_mno_implicit_float) ||
                       O.matches(options::OPT_msoft_float));
  }
  if (NoImplicitFloat)
    CmdArgs.push_back("-no-implicit-float");

  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
    } else {
      getToolChain().getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getOption().getName() << Value;
    }
  } else if (getToolChain().getDriver().IsCLMode()) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-x86-asm-syntax=intel");
  }

  // Set flags to support MCU ABI.
  if (Args.hasFlag(options::OPT_miamcu, options::OPT_mno_iamcu, false)) {
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
    CmdArgs.push_back("-mstack-alignment=4");
  }
}

void Clang::AddHexagonTargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-mqdsp6-compat");
  CmdArgs.push_back("-Wreturn-type");

  if (auto G = toolchains::HexagonToolChain::getSmallDataThreshold(Args)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-hexagon-small-data-threshold=" +
                                         Twine(G.getValue())));
  }

  if (!Args.hasArg(options::OPT_fno_short_enums))
    CmdArgs.push_back("-fshort-enums");
  if (Args.getLastArg(options::OPT_mieee_rnd_near)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-enable-hexagon-ieee-rnd-near");
  }
  CmdArgs.push_back("-mllvm");
  CmdArgs.push_back("-machine-sink-split=0");
}

void Clang::AddLanaiTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPUName = A->getValue();

    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPUName));
  }
  if (Arg *A = Args.getLastArg(options::OPT_mregparm_EQ)) {
    StringRef Value = A->getValue();
    // Only support mregparm=4 to support old usage. Report error for all other
    // cases.
    int Mregparm;
    if (Value.getAsInteger(10, Mregparm)) {
      if (Mregparm != 4) {
        getToolChain().getDriver().Diag(
            diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Value;
      }
    }
  }
}

void Clang::AddWebAssemblyTargetArgs(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  // Default to "hidden" visibility.
  if (!Args.hasArg(options::OPT_fvisibility_EQ,
                   options::OPT_fvisibility_ms_compat)) {
    CmdArgs.push_back("-fvisibility");
    CmdArgs.push_back("hidden");
  }
}

void Clang::DumpCompilationDatabase(Compilation &C, StringRef Filename,
                                    StringRef Target, const InputInfo &Output,
                                    const InputInfo &Input, const ArgList &Args) const {
  // If this is a dry run, do not create the compilation database file.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH))
    return;

  using llvm::yaml::escape;
  const Driver &D = getToolChain().getDriver();

  if (!CompilationDatabase) {
    std::error_code EC;
    auto File = llvm::make_unique<llvm::raw_fd_ostream>(Filename, EC, llvm::sys::fs::F_Text);
    if (EC) {
      D.Diag(clang::diag::err_drv_compilationdatabase) << Filename
                                                       << EC.message();
      return;
    }
    CompilationDatabase = std::move(File);
  }
  auto &CDB = *CompilationDatabase;
  SmallString<128> Buf;
  if (llvm::sys::fs::current_path(Buf))
    Buf = ".";
  CDB << "{ \"directory\": \"" << escape(Buf) << "\"";
  CDB << ", \"file\": \"" << escape(Input.getFilename()) << "\"";
  CDB << ", \"output\": \"" << escape(Output.getFilename()) << "\"";
  CDB << ", \"arguments\": [\"" << escape(D.ClangExecutable) << "\"";
  Buf = "-x";
  Buf += types::getTypeName(Input.getType());
  CDB << ", \"" << escape(Buf) << "\"";
  if (!D.SysRoot.empty() && !Args.hasArg(options::OPT__sysroot_EQ)) {
    Buf = "--sysroot=";
    Buf += D.SysRoot;
    CDB << ", \"" << escape(Buf) << "\"";
  }
  CDB << ", \"" << escape(Input.getFilename()) << "\"";
  for (auto &A: Args) {
    auto &O = A->getOption();
    // Skip language selection, which is positional.
    if (O.getID() == options::OPT_x)
      continue;
    // Skip writing dependency output and the compilation database itself.
    if (O.getGroup().isValid() && O.getGroup().getID() == options::OPT_M_Group)
      continue;
    // Skip inputs.
    if (O.getKind() == Option::InputClass)
      continue;
    // All other arguments are quoted and appended.
    ArgStringList ASL;
    A->render(Args, ASL);
    for (auto &it: ASL)
      CDB << ", \"" << escape(it) << "\"";
  }
  Buf = "--target=";
  Buf += Target;
  CDB << ", \"" << escape(Buf) << "\"]},\n";
}

static void CollectArgsForIntegratedAssembler(Compilation &C,
                                              const ArgList &Args,
                                              ArgStringList &CmdArgs,
                                              const Driver &D) {
  if (UseRelaxAll(C, Args))
    CmdArgs.push_back("-mrelax-all");

  // Only default to -mincremental-linker-compatible if we think we are
  // targeting the MSVC linker.
  bool DefaultIncrementalLinkerCompatible =
      C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment();
  if (Args.hasFlag(options::OPT_mincremental_linker_compatible,
                   options::OPT_mno_incremental_linker_compatible,
                   DefaultIncrementalLinkerCompatible))
    CmdArgs.push_back("-mincremental-linker-compatible");

  switch (C.getDefaultToolChain().getArch()) {
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    if (Arg *A = Args.getLastArg(options::OPT_mimplicit_it_EQ)) {
      StringRef Value = A->getValue();
      if (Value == "always" || Value == "never" || Value == "arm" ||
          Value == "thumb") {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back(Args.MakeArgString("-arm-implicit-it=" + Value));
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Value;
      }
    }
    break;
  default:
    break;
  }

  // When passing -I arguments to the assembler we sometimes need to
  // unconditionally take the next argument.  For example, when parsing
  // '-Wa,-I -Wa,foo' we need to accept the -Wa,foo arg after seeing the
  // -Wa,-I arg and when parsing '-Wa,-I,foo' we need to accept the 'foo'
  // arg after parsing the '-I' arg.
  bool TakeNextArg = false;

  bool UseRelaxRelocations = C.getDefaultToolChain().useRelaxRelocations();
  const char *MipsTargetFeature = nullptr;
  for (const Arg *A :
       Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
    A->claim();

    for (StringRef Value : A->getValues()) {
      if (TakeNextArg) {
        CmdArgs.push_back(Value.data());
        TakeNextArg = false;
        continue;
      }

      if (C.getDefaultToolChain().getTriple().isOSBinFormatCOFF() &&
          Value == "-mbig-obj")
        continue; // LLVM handles bigobj automatically

      switch (C.getDefaultToolChain().getArch()) {
      default:
        break;
      case llvm::Triple::thumb:
      case llvm::Triple::thumbeb:
      case llvm::Triple::arm:
      case llvm::Triple::armeb:
        if (Value == "-mthumb")
          // -mthumb has already been processed in ComputeLLVMTriple()
          // recognize but skip over here.
          continue;
        break;
      case llvm::Triple::mips:
      case llvm::Triple::mipsel:
      case llvm::Triple::mips64:
      case llvm::Triple::mips64el:
        if (Value == "--trap") {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("+use-tcc-in-div");
          continue;
        }
        if (Value == "--break") {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("-use-tcc-in-div");
          continue;
        }
        if (Value.startswith("-msoft-float")) {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("+soft-float");
          continue;
        }
        if (Value.startswith("-mhard-float")) {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("-soft-float");
          continue;
        }

        MipsTargetFeature = llvm::StringSwitch<const char *>(Value)
                                .Case("-mips1", "+mips1")
                                .Case("-mips2", "+mips2")
                                .Case("-mips3", "+mips3")
                                .Case("-mips4", "+mips4")
                                .Case("-mips5", "+mips5")
                                .Case("-mips32", "+mips32")
                                .Case("-mips32r2", "+mips32r2")
                                .Case("-mips32r3", "+mips32r3")
                                .Case("-mips32r5", "+mips32r5")
                                .Case("-mips32r6", "+mips32r6")
                                .Case("-mips64", "+mips64")
                                .Case("-mips64r2", "+mips64r2")
                                .Case("-mips64r3", "+mips64r3")
                                .Case("-mips64r5", "+mips64r5")
                                .Case("-mips64r6", "+mips64r6")
                                .Default(nullptr);
        if (MipsTargetFeature)
          continue;
      }

      if (Value == "-force_cpusubtype_ALL") {
        // Do nothing, this is the default and we don't support anything else.
      } else if (Value == "-L") {
        CmdArgs.push_back("-msave-temp-labels");
      } else if (Value == "--fatal-warnings") {
        CmdArgs.push_back("-massembler-fatal-warnings");
      } else if (Value == "--noexecstack") {
        CmdArgs.push_back("-mnoexecstack");
      } else if (Value.startswith("-compress-debug-sections") ||
                 Value.startswith("--compress-debug-sections") ||
                 Value == "-nocompress-debug-sections" ||
                 Value == "--nocompress-debug-sections") {
        CmdArgs.push_back(Value.data());
      } else if (Value == "-mrelax-relocations=yes" ||
                 Value == "--mrelax-relocations=yes") {
        UseRelaxRelocations = true;
      } else if (Value == "-mrelax-relocations=no" ||
                 Value == "--mrelax-relocations=no") {
        UseRelaxRelocations = false;
      } else if (Value.startswith("-I")) {
        CmdArgs.push_back(Value.data());
        // We need to consume the next argument if the current arg is a plain
        // -I. The next arg will be the include directory.
        if (Value == "-I")
          TakeNextArg = true;
      } else if (Value.startswith("-gdwarf-")) {
        // "-gdwarf-N" options are not cc1as options.
        unsigned DwarfVersion = DwarfVersionNum(Value);
        if (DwarfVersion == 0) { // Send it onward, and let cc1as complain.
          CmdArgs.push_back(Value.data());
        } else {
          RenderDebugEnablingArgs(Args, CmdArgs,
                                  codegenoptions::LimitedDebugInfo,
                                  DwarfVersion, llvm::DebuggerKind::Default);
        }
      } else if (Value.startswith("-mcpu") || Value.startswith("-mfpu") ||
                 Value.startswith("-mhwdiv") || Value.startswith("-march")) {
        // Do nothing, we'll validate it later.
      } else if (Value == "-defsym") {
          if (A->getNumValues() != 2) {
            D.Diag(diag::err_drv_defsym_invalid_format) << Value;
            break;
          }
          const char *S = A->getValue(1);
          auto Pair = StringRef(S).split('=');
          auto Sym = Pair.first;
          auto SVal = Pair.second;

          if (Sym.empty() || SVal.empty()) {
            D.Diag(diag::err_drv_defsym_invalid_format) << S;
            break;
          }
          int64_t IVal;
          if (SVal.getAsInteger(0, IVal)) {
            D.Diag(diag::err_drv_defsym_invalid_symval) << SVal;
            break;
          }
          CmdArgs.push_back(Value.data());
          TakeNextArg = true;
      } else if (Value == "-fdebug-compilation-dir") {
        CmdArgs.push_back("-fdebug-compilation-dir");
        TakeNextArg = true;
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Value;
      }
    }
  }
  if (UseRelaxRelocations)
    CmdArgs.push_back("--mrelax-relocations");
  if (MipsTargetFeature != nullptr) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back(MipsTargetFeature);
  }

  // forward -fembed-bitcode to assmebler
  if (C.getDriver().embedBitcodeEnabled() ||
      C.getDriver().embedBitcodeMarkerOnly())
    Args.AddLastArg(CmdArgs, options::OPT_fembed_bitcode_EQ);
}

static void RenderFloatingPointOptions(const ToolChain &TC, const Driver &D,
                                       bool OFastEnabled, const ArgList &Args,
                                       ArgStringList &CmdArgs) {
  // Handle various floating point optimization flags, mapping them to the
  // appropriate LLVM code generation flags. This is complicated by several
  // "umbrella" flags, so we do this by stepping through the flags incrementally
  // adjusting what we think is enabled/disabled, then at the end setting the
  // LLVM flags based on the final state.
  bool HonorINFs = true;
  bool HonorNaNs = true;
  // -fmath-errno is the default on some platforms, e.g. BSD-derived OSes.
  bool MathErrno = TC.IsMathErrnoDefault();
  bool AssociativeMath = false;
  bool ReciprocalMath = false;
  bool SignedZeros = true;
  bool TrappingMath = true;
  StringRef DenormalFPMath = "";
  StringRef FPContract = "";

  if (const Arg *A = Args.getLastArg(options::OPT_flimited_precision_EQ)) {
    CmdArgs.push_back("-mlimit-float-precision");
    CmdArgs.push_back(A->getValue());
  }

  for (const Arg *A : Args) {
    switch (A->getOption().getID()) {
    // If this isn't an FP option skip the claim below
    default: continue;

    // Options controlling individual features
    case options::OPT_fhonor_infinities:    HonorINFs = true;         break;
    case options::OPT_fno_honor_infinities: HonorINFs = false;        break;
    case options::OPT_fhonor_nans:          HonorNaNs = true;         break;
    case options::OPT_fno_honor_nans:       HonorNaNs = false;        break;
    case options::OPT_fmath_errno:          MathErrno = true;         break;
    case options::OPT_fno_math_errno:       MathErrno = false;        break;
    case options::OPT_fassociative_math:    AssociativeMath = true;   break;
    case options::OPT_fno_associative_math: AssociativeMath = false;  break;
    case options::OPT_freciprocal_math:     ReciprocalMath = true;    break;
    case options::OPT_fno_reciprocal_math:  ReciprocalMath = false;   break;
    case options::OPT_fsigned_zeros:        SignedZeros = true;       break;
    case options::OPT_fno_signed_zeros:     SignedZeros = false;      break;
    case options::OPT_ftrapping_math:       TrappingMath = true;      break;
    case options::OPT_fno_trapping_math:    TrappingMath = false;     break;

    case options::OPT_fdenormal_fp_math_EQ:
      DenormalFPMath = A->getValue();
      break;

    // Validate and pass through -fp-contract option.
    case options::OPT_ffp_contract: {
      StringRef Val = A->getValue();
      if (Val == "fast" || Val == "on" || Val == "off")
        FPContract = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Val;
      break;
    }

    case options::OPT_ffinite_math_only:
      HonorINFs = false;
      HonorNaNs = false;
      break;
    case options::OPT_fno_finite_math_only:
      HonorINFs = true;
      HonorNaNs = true;
      break;

    case options::OPT_funsafe_math_optimizations:
      AssociativeMath = true;
      ReciprocalMath = true;
      SignedZeros = false;
      TrappingMath = false;
      break;
    case options::OPT_fno_unsafe_math_optimizations:
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      TrappingMath = true;
      // -fno_unsafe_math_optimizations restores default denormal handling
      DenormalFPMath = "";
      break;

    case options::OPT_Ofast:
      // If -Ofast is the optimization level, then -ffast-math should be enabled
      if (!OFastEnabled)
        continue;
      LLVM_FALLTHROUGH;
    case options::OPT_ffast_math:
      HonorINFs = false;
      HonorNaNs = false;
      MathErrno = false;
      AssociativeMath = true;
      ReciprocalMath = true;
      SignedZeros = false;
      TrappingMath = false;
      // If fast-math is set then set the fp-contract mode to fast.
      FPContract = "fast";
      break;
    case options::OPT_fno_fast_math:
      HonorINFs = true;
      HonorNaNs = true;
      // Turning on -ffast-math (with either flag) removes the need for
      // MathErrno. However, turning *off* -ffast-math merely restores the
      // toolchain default (which may be false).
      MathErrno = TC.IsMathErrnoDefault();
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      TrappingMath = true;
      // -fno_fast_math restores default denormal and fpcontract handling
      DenormalFPMath = "";
      FPContract = "";
      break;
    }

    // If we handled this option claim it
    A->claim();
  }

  if (!HonorINFs)
    CmdArgs.push_back("-menable-no-infs");

  if (!HonorNaNs)
    CmdArgs.push_back("-menable-no-nans");

  if (MathErrno)
    CmdArgs.push_back("-fmath-errno");

  if (!MathErrno && AssociativeMath && ReciprocalMath && !SignedZeros &&
      !TrappingMath)
    CmdArgs.push_back("-menable-unsafe-fp-math");

  if (!SignedZeros)
    CmdArgs.push_back("-fno-signed-zeros");

  if (AssociativeMath && !SignedZeros && !TrappingMath)
    CmdArgs.push_back("-mreassociate");

  if (ReciprocalMath)
    CmdArgs.push_back("-freciprocal-math");

  if (!TrappingMath)
    CmdArgs.push_back("-fno-trapping-math");

  if (!DenormalFPMath.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fdenormal-fp-math=" + DenormalFPMath));

  if (!FPContract.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-contract=" + FPContract));

  ParseMRecip(D, Args, CmdArgs);

  // -ffast-math enables the __FAST_MATH__ preprocessor macro, but check for the
  // individual features enabled by -ffast-math instead of the option itself as
  // that's consistent with gcc's behaviour.
  if (!HonorINFs && !HonorNaNs && !MathErrno && AssociativeMath &&
      ReciprocalMath && !SignedZeros && !TrappingMath)
    CmdArgs.push_back("-ffast-math");

  // Handle __FINITE_MATH_ONLY__ similarly.
  if (!HonorINFs && !HonorNaNs)
    CmdArgs.push_back("-ffinite-math-only");

  if (const Arg *A = Args.getLastArg(options::OPT_mfpmath_EQ)) {
    CmdArgs.push_back("-mfpmath");
    CmdArgs.push_back(A->getValue());
  }

  // Disable a codegen optimization for floating-point casts.
  if (Args.hasFlag(options::OPT_fno_strict_float_cast_overflow,
                   options::OPT_fstrict_float_cast_overflow, false))
    CmdArgs.push_back("-fno-strict-float-cast-overflow");
}

static void RenderAnalyzerOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                  const llvm::Triple &Triple,
                                  const InputInfo &Input) {
  // Enable region store model by default.
  CmdArgs.push_back("-analyzer-store=region");

  // Treat blocks as analysis entry points.
  CmdArgs.push_back("-analyzer-opt-analyze-nested-blocks");

  // Add default argument set.
  if (!Args.hasArg(options::OPT__analyzer_no_default_checks)) {
    CmdArgs.push_back("-analyzer-checker=core");
    CmdArgs.push_back("-analyzer-checker=apiModeling");

    if (!Triple.isWindowsMSVCEnvironment()) {
      CmdArgs.push_back("-analyzer-checker=unix");
    } else {
      // Enable "unix" checkers that also work on Windows.
      CmdArgs.push_back("-analyzer-checker=unix.API");
      CmdArgs.push_back("-analyzer-checker=unix.Malloc");
      CmdArgs.push_back("-analyzer-checker=unix.MallocSizeof");
      CmdArgs.push_back("-analyzer-checker=unix.MismatchedDeallocator");
      CmdArgs.push_back("-analyzer-checker=unix.cstring.BadSizeArg");
      CmdArgs.push_back("-analyzer-checker=unix.cstring.NullArg");
    }

    // Disable some unix checkers for PS4.
    if (Triple.isPS4CPU()) {
      CmdArgs.push_back("-analyzer-disable-checker=unix.API");
      CmdArgs.push_back("-analyzer-disable-checker=unix.Vfork");
    }

    if (Triple.isOSDarwin())
      CmdArgs.push_back("-analyzer-checker=osx");

    CmdArgs.push_back("-analyzer-checker=deadcode");

    if (types::isCXX(Input.getType()))
      CmdArgs.push_back("-analyzer-checker=cplusplus");

    if (!Triple.isPS4CPU()) {
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.UncheckedReturn");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.getpw");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.gets");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.mktemp");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.mkstemp");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.vfork");
    }

    // Default nullability checks.
    CmdArgs.push_back("-analyzer-checker=nullability.NullPassedToNonnull");
    CmdArgs.push_back("-analyzer-checker=nullability.NullReturnedFromNonnull");
  }

  // Set the output format. The default is plist, for (lame) historical reasons.
  CmdArgs.push_back("-analyzer-output");
  if (Arg *A = Args.getLastArg(options::OPT__analyzer_output))
    CmdArgs.push_back(A->getValue());
  else
    CmdArgs.push_back("plist");

  // Disable the presentation of standard compiler warnings when using
  // --analyze.  We only want to show static analyzer diagnostics or frontend
  // errors.
  CmdArgs.push_back("-w");

  // Add -Xanalyzer arguments when running as analyzer.
  Args.AddAllArgValues(CmdArgs, options::OPT_Xanalyzer);
}

static void RenderSSPOptions(const ToolChain &TC, const ArgList &Args,
                             ArgStringList &CmdArgs, bool KernelOrKext) {
  const llvm::Triple &EffectiveTriple = TC.getEffectiveTriple();

  // NVPTX doesn't support stack protectors; from the compiler's perspective, it
  // doesn't even have a stack!
  if (EffectiveTriple.isNVPTX())
    return;

  // -stack-protector=0 is default.
  unsigned StackProtectorLevel = 0;
  unsigned DefaultStackProtectorLevel =
      TC.GetDefaultStackProtectorLevel(KernelOrKext);

  if (Arg *A = Args.getLastArg(options::OPT_fno_stack_protector,
                               options::OPT_fstack_protector_all,
                               options::OPT_fstack_protector_strong,
                               options::OPT_fstack_protector)) {
    if (A->getOption().matches(options::OPT_fstack_protector))
      StackProtectorLevel =
          std::max<unsigned>(LangOptions::SSPOn, DefaultStackProtectorLevel);
    else if (A->getOption().matches(options::OPT_fstack_protector_strong))
      StackProtectorLevel = LangOptions::SSPStrong;
    else if (A->getOption().matches(options::OPT_fstack_protector_all))
      StackProtectorLevel = LangOptions::SSPReq;
  } else {
    StackProtectorLevel = DefaultStackProtectorLevel;
  }

  if (StackProtectorLevel) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(Twine(StackProtectorLevel)));
  }

  // --param ssp-buffer-size=
  for (const Arg *A : Args.filtered(options::OPT__param)) {
    StringRef Str(A->getValue());
    if (Str.startswith("ssp-buffer-size=")) {
      if (StackProtectorLevel) {
        CmdArgs.push_back("-stack-protector-buffer-size");
        // FIXME: Verify the argument is a valid integer.
        CmdArgs.push_back(Args.MakeArgString(Str.drop_front(16)));
      }
      A->claim();
    }
  }
}

static void RenderTrivialAutoVarInitOptions(const Driver &D,
                                            const ToolChain &TC,
                                            const ArgList &Args,
                                            ArgStringList &CmdArgs) {
  auto DefaultTrivialAutoVarInit = TC.GetDefaultTrivialAutoVarInit();
  StringRef TrivialAutoVarInit = "";

  for (const Arg *A : Args) {
    switch (A->getOption().getID()) {
    default:
      continue;
    case options::OPT_ftrivial_auto_var_init: {
      A->claim();
      StringRef Val = A->getValue();
      if (Val == "uninitialized" || Val == "zero" || Val == "pattern")
        TrivialAutoVarInit = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Val;
      break;
    }
    }
  }

  if (TrivialAutoVarInit.empty())
    switch (DefaultTrivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      break;
    case LangOptions::TrivialAutoVarInitKind::Pattern:
      TrivialAutoVarInit = "pattern";
      break;
    case LangOptions::TrivialAutoVarInitKind::Zero:
      TrivialAutoVarInit = "zero";
      break;
    }

  if (!TrivialAutoVarInit.empty()) {
    if (TrivialAutoVarInit == "zero" && !Args.hasArg(options::OPT_enable_trivial_var_init_zero))
      D.Diag(diag::err_drv_trivial_auto_var_init_zero_disabled);
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init=" + TrivialAutoVarInit));
  }
}

static void RenderOpenCLOptions(const ArgList &Args, ArgStringList &CmdArgs) {
  const unsigned ForwardedArguments[] = {
      options::OPT_cl_opt_disable,
      options::OPT_cl_strict_aliasing,
      options::OPT_cl_single_precision_constant,
      options::OPT_cl_finite_math_only,
      options::OPT_cl_kernel_arg_info,
      options::OPT_cl_unsafe_math_optimizations,
      options::OPT_cl_fast_relaxed_math,
      options::OPT_cl_mad_enable,
      options::OPT_cl_no_signed_zeros,
      options::OPT_cl_denorms_are_zero,
      options::OPT_cl_fp32_correctly_rounded_divide_sqrt,
      options::OPT_cl_uniform_work_group_size
  };

  if (Arg *A = Args.getLastArg(options::OPT_cl_std_EQ)) {
    std::string CLStdStr = std::string("-cl-std=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(CLStdStr));
  }

  for (const auto &Arg : ForwardedArguments)
    if (const auto *A = Args.getLastArg(Arg))
      CmdArgs.push_back(Args.MakeArgString(A->getOption().getPrefixedName()));
}

static void RenderARCMigrateToolOptions(const Driver &D, const ArgList &Args,
                                        ArgStringList &CmdArgs) {
  bool ARCMTEnabled = false;
  if (!Args.hasArg(options::OPT_fno_objc_arc, options::OPT_fobjc_arc)) {
    if (const Arg *A = Args.getLastArg(options::OPT_ccc_arcmt_check,
                                       options::OPT_ccc_arcmt_modify,
                                       options::OPT_ccc_arcmt_migrate)) {
      ARCMTEnabled = true;
      switch (A->getOption().getID()) {
      default: llvm_unreachable("missed a case");
      case options::OPT_ccc_arcmt_check:
        CmdArgs.push_back("-arcmt-check");
        break;
      case options::OPT_ccc_arcmt_modify:
        CmdArgs.push_back("-arcmt-modify");
        break;
      case options::OPT_ccc_arcmt_migrate:
        CmdArgs.push_back("-arcmt-migrate");
        CmdArgs.push_back("-mt-migrate-directory");
        CmdArgs.push_back(A->getValue());

        Args.AddLastArg(CmdArgs, options::OPT_arcmt_migrate_report_output);
        Args.AddLastArg(CmdArgs, options::OPT_arcmt_migrate_emit_arc_errors);
        break;
      }
    }
  } else {
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_check);
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_modify);
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_migrate);
  }

  if (const Arg *A = Args.getLastArg(options::OPT_ccc_objcmt_migrate)) {
    if (ARCMTEnabled)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << "-ccc-arcmt-migrate";

    CmdArgs.push_back("-mt-migrate-directory");
    CmdArgs.push_back(A->getValue());

    if (!Args.hasArg(options::OPT_objcmt_migrate_literals,
                     options::OPT_objcmt_migrate_subscripting,
                     options::OPT_objcmt_migrate_property)) {
      // None specified, means enable them all.
      CmdArgs.push_back("-objcmt-migrate-literals");
      CmdArgs.push_back("-objcmt-migrate-subscripting");
      CmdArgs.push_back("-objcmt-migrate-property");
    } else {
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_literals);
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_subscripting);
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property);
    }
  } else {
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_literals);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_subscripting);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_all);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_readonly_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_readwrite_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property_dot_syntax);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_annotation);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_instancetype);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_nsmacros);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_protocol_conformance);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_atomic_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_returns_innerpointer_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_ns_nonatomic_iosonly);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_designated_init);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_whitelist_dir_path);
  }
}

static void RenderBuiltinOptions(const ToolChain &TC, const llvm::Triple &T,
                                 const ArgList &Args, ArgStringList &CmdArgs) {
  // -fbuiltin is default unless -mkernel is used.
  bool UseBuiltins =
      Args.hasFlag(options::OPT_fbuiltin, options::OPT_fno_builtin,
                   !Args.hasArg(options::OPT_mkernel));
  if (!UseBuiltins)
    CmdArgs.push_back("-fno-builtin");

  // -ffreestanding implies -fno-builtin.
  if (Args.hasArg(options::OPT_ffreestanding))
    UseBuiltins = false;

  // Process the -fno-builtin-* options.
  for (const auto &Arg : Args) {
    const Option &O = Arg->getOption();
    if (!O.matches(options::OPT_fno_builtin_))
      continue;

    Arg->claim();

    // If -fno-builtin is specified, then there's no need to pass the option to
    // the frontend.
    if (!UseBuiltins)
      continue;

    StringRef FuncName = Arg->getValue();
    CmdArgs.push_back(Args.MakeArgString("-fno-builtin-" + FuncName));
  }

  // le32-specific flags:
  //  -fno-math-builtin: clang should not convert math builtins to intrinsics
  //                     by default.
  if (TC.getArch() == llvm::Triple::le32)
    CmdArgs.push_back("-fno-math-builtin");
}

void Driver::getDefaultModuleCachePath(SmallVectorImpl<char> &Result) {
  llvm::sys::path::system_temp_directory(/*erasedOnReboot=*/false, Result);
  llvm::sys::path::append(Result, "org.llvm.clang.");
  appendUserToPath(Result);
  llvm::sys::path::append(Result, "ModuleCache");
}

static void RenderModulesOptions(Compilation &C, const Driver &D,
                                 const ArgList &Args, const InputInfo &Input,
                                 const InputInfo &Output,
                                 ArgStringList &CmdArgs, bool &HaveModules) {
  // -fmodules enables the use of precompiled modules (off by default).
  // Users can pass -fno-cxx-modules to turn off modules support for
  // C++/Objective-C++ programs.
  bool HaveClangModules = false;
  if (Args.hasFlag(options::OPT_fmodules, options::OPT_fno_modules, false)) {
    bool AllowedInCXX = Args.hasFlag(options::OPT_fcxx_modules,
                                     options::OPT_fno_cxx_modules, true);
    if (AllowedInCXX || !types::isCXX(Input.getType())) {
      CmdArgs.push_back("-fmodules");
      HaveClangModules = true;
    }
  }

  HaveModules = HaveClangModules;
  if (Args.hasArg(options::OPT_fmodules_ts)) {
    CmdArgs.push_back("-fmodules-ts");
    HaveModules = true;
  }

  // -fmodule-maps enables implicit reading of module map files. By default,
  // this is enabled if we are using Clang's flavor of precompiled modules.
  if (Args.hasFlag(options::OPT_fimplicit_module_maps,
                   options::OPT_fno_implicit_module_maps, HaveClangModules))
    CmdArgs.push_back("-fimplicit-module-maps");

  // -fmodules-decluse checks that modules used are declared so (off by default)
  if (Args.hasFlag(options::OPT_fmodules_decluse,
                   options::OPT_fno_modules_decluse, false))
    CmdArgs.push_back("-fmodules-decluse");

  // -fmodules-strict-decluse is like -fmodule-decluse, but also checks that
  // all #included headers are part of modules.
  if (Args.hasFlag(options::OPT_fmodules_strict_decluse,
                   options::OPT_fno_modules_strict_decluse, false))
    CmdArgs.push_back("-fmodules-strict-decluse");

  // -fno-implicit-modules turns off implicitly compiling modules on demand.
  bool ImplicitModules = false;
  if (!Args.hasFlag(options::OPT_fimplicit_modules,
                    options::OPT_fno_implicit_modules, HaveClangModules)) {
    if (HaveModules)
      CmdArgs.push_back("-fno-implicit-modules");
  } else if (HaveModules) {
    ImplicitModules = true;
    // -fmodule-cache-path specifies where our implicitly-built module files
    // should be written.
    SmallString<128> Path;
    if (Arg *A = Args.getLastArg(options::OPT_fmodules_cache_path))
      Path = A->getValue();

    if (C.isForDiagnostics()) {
      // When generating crash reports, we want to emit the modules along with
      // the reproduction sources, so we ignore any provided module path.
      Path = Output.getFilename();
      llvm::sys::path::replace_extension(Path, ".cache");
      llvm::sys::path::append(Path, "modules");
    } else if (Path.empty()) {
      // No module path was provided: use the default.
      Driver::getDefaultModuleCachePath(Path);
    }

    const char Arg[] = "-fmodules-cache-path=";
    Path.insert(Path.begin(), Arg, Arg + strlen(Arg));
    CmdArgs.push_back(Args.MakeArgString(Path));
  }

  if (HaveModules) {
    // -fprebuilt-module-path specifies where to load the prebuilt module files.
    for (const Arg *A : Args.filtered(options::OPT_fprebuilt_module_path)) {
      CmdArgs.push_back(Args.MakeArgString(
          std::string("-fprebuilt-module-path=") + A->getValue()));
      A->claim();
    }
  }

  // -fmodule-name specifies the module that is currently being built (or
  // used for header checking by -fmodule-maps).
  Args.AddLastArg(CmdArgs, options::OPT_fmodule_name_EQ);

  // -fmodule-map-file can be used to specify files containing module
  // definitions.
  Args.AddAllArgs(CmdArgs, options::OPT_fmodule_map_file);

  // -fbuiltin-module-map can be used to load the clang
  // builtin headers modulemap file.
  if (Args.hasArg(options::OPT_fbuiltin_module_map)) {
    SmallString<128> BuiltinModuleMap(D.ResourceDir);
    llvm::sys::path::append(BuiltinModuleMap, "include");
    llvm::sys::path::append(BuiltinModuleMap, "module.modulemap");
    if (llvm::sys::fs::exists(BuiltinModuleMap))
      CmdArgs.push_back(
          Args.MakeArgString("-fmodule-map-file=" + BuiltinModuleMap));
  }

  // The -fmodule-file=<name>=<file> form specifies the mapping of module
  // names to precompiled module files (the module is loaded only if used).
  // The -fmodule-file=<file> form can be used to unconditionally load
  // precompiled module files (whether used or not).
  if (HaveModules)
    Args.AddAllArgs(CmdArgs, options::OPT_fmodule_file);
  else
    Args.ClaimAllArgs(options::OPT_fmodule_file);

  // When building modules and generating crashdumps, we need to dump a module
  // dependency VFS alongside the output.
  if (HaveClangModules && C.isForDiagnostics()) {
    SmallString<128> VFSDir(Output.getFilename());
    llvm::sys::path::replace_extension(VFSDir, ".cache");
    // Add the cache directory as a temp so the crash diagnostics pick it up.
    C.addTempFile(Args.MakeArgString(VFSDir));

    llvm::sys::path::append(VFSDir, "vfs");
    CmdArgs.push_back("-module-dependency-dir");
    CmdArgs.push_back(Args.MakeArgString(VFSDir));
  }

  if (HaveClangModules)
    Args.AddLastArg(CmdArgs, options::OPT_fmodules_user_build_path);

  // Pass through all -fmodules-ignore-macro arguments.
  Args.AddAllArgs(CmdArgs, options::OPT_fmodules_ignore_macro);
  Args.AddLastArg(CmdArgs, options::OPT_fmodules_prune_interval);
  Args.AddLastArg(CmdArgs, options::OPT_fmodules_prune_after);

  Args.AddLastArg(CmdArgs, options::OPT_fbuild_session_timestamp);

  if (Arg *A = Args.getLastArg(options::OPT_fbuild_session_file)) {
    if (Args.hasArg(options::OPT_fbuild_session_timestamp))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << "-fbuild-session-timestamp";

    llvm::sys::fs::file_status Status;
    if (llvm::sys::fs::status(A->getValue(), Status))
      D.Diag(diag::err_drv_no_such_file) << A->getValue();
    CmdArgs.push_back(
        Args.MakeArgString("-fbuild-session-timestamp=" +
                           Twine((uint64_t)Status.getLastModificationTime()
                                     .time_since_epoch()
                                     .count())));
  }

  if (Args.getLastArg(options::OPT_fmodules_validate_once_per_build_session)) {
    if (!Args.getLastArg(options::OPT_fbuild_session_timestamp,
                         options::OPT_fbuild_session_file))
      D.Diag(diag::err_drv_modules_validate_once_requires_timestamp);

    Args.AddLastArg(CmdArgs,
                    options::OPT_fmodules_validate_once_per_build_session);
  }

  if (Args.hasFlag(options::OPT_fmodules_validate_system_headers,
                   options::OPT_fno_modules_validate_system_headers,
                   ImplicitModules))
    CmdArgs.push_back("-fmodules-validate-system-headers");

  Args.AddLastArg(CmdArgs, options::OPT_fmodules_disable_diagnostic_validation);
}

static void RenderCharacterOptions(const ArgList &Args, const llvm::Triple &T,
                                   ArgStringList &CmdArgs) {
  // -fsigned-char is default.
  if (const Arg *A = Args.getLastArg(options::OPT_fsigned_char,
                                     options::OPT_fno_signed_char,
                                     options::OPT_funsigned_char,
                                     options::OPT_fno_unsigned_char)) {
    if (A->getOption().matches(options::OPT_funsigned_char) ||
        A->getOption().matches(options::OPT_fno_signed_char)) {
      CmdArgs.push_back("-fno-signed-char");
    }
  } else if (!isSignedCharDefault(T)) {
    CmdArgs.push_back("-fno-signed-char");
  }

  // The default depends on the language standard.
  if (const Arg *A =
          Args.getLastArg(options::OPT_fchar8__t, options::OPT_fno_char8__t))
    A->render(Args, CmdArgs);

  if (const Arg *A = Args.getLastArg(options::OPT_fshort_wchar,
                                     options::OPT_fno_short_wchar)) {
    if (A->getOption().matches(options::OPT_fshort_wchar)) {
      CmdArgs.push_back("-fwchar-type=short");
      CmdArgs.push_back("-fno-signed-wchar");
    } else {
      bool IsARM = T.isARM() || T.isThumb() || T.isAArch64();
      CmdArgs.push_back("-fwchar-type=int");
      if (IsARM && !(T.isOSWindows() || T.isOSNetBSD() ||
                     T.isOSOpenBSD()))
        CmdArgs.push_back("-fno-signed-wchar");
      else
        CmdArgs.push_back("-fsigned-wchar");
    }
  }
}

static void RenderObjCOptions(const ToolChain &TC, const Driver &D,
                              const llvm::Triple &T, const ArgList &Args,
                              ObjCRuntime &Runtime, bool InferCovariantReturns,
                              const InputInfo &Input, ArgStringList &CmdArgs) {
  const llvm::Triple::ArchType Arch = TC.getArch();

  // -fobjc-dispatch-method is only relevant with the nonfragile-abi, and legacy
  // is the default. Except for deployment target of 10.5, next runtime is
  // always legacy dispatch and -fno-objc-legacy-dispatch gets ignored silently.
  if (Runtime.isNonFragile()) {
    if (!Args.hasFlag(options::OPT_fobjc_legacy_dispatch,
                      options::OPT_fno_objc_legacy_dispatch,
                      Runtime.isLegacyDispatchDefaultForArch(Arch))) {
      if (TC.UseObjCMixedDispatch())
        CmdArgs.push_back("-fobjc-dispatch-method=mixed");
      else
        CmdArgs.push_back("-fobjc-dispatch-method=non-legacy");
    }
  }

  // When ObjectiveC legacy runtime is in effect on MacOSX, turn on the option
  // to do Array/Dictionary subscripting by default.
  if (Arch == llvm::Triple::x86 && T.isMacOSX() &&
      Runtime.getKind() == ObjCRuntime::FragileMacOSX && Runtime.isNeXTFamily())
    CmdArgs.push_back("-fobjc-subscripting-legacy-runtime");

  // Allow -fno-objc-arr to trump -fobjc-arr/-fobjc-arc.
  // NOTE: This logic is duplicated in ToolChains.cpp.
  if (isObjCAutoRefCount(Args)) {
    TC.CheckObjCARC();

    CmdArgs.push_back("-fobjc-arc");

    // FIXME: It seems like this entire block, and several around it should be
    // wrapped in isObjC, but for now we just use it here as this is where it
    // was being used previously.
    if (types::isCXX(Input.getType()) && types::isObjC(Input.getType())) {
      if (TC.GetCXXStdlibType(Args) == ToolChain::CST_Libcxx)
        CmdArgs.push_back("-fobjc-arc-cxxlib=libc++");
      else
        CmdArgs.push_back("-fobjc-arc-cxxlib=libstdc++");
    }

    // Allow the user to enable full exceptions code emission.
    // We default off for Objective-C, on for Objective-C++.
    if (Args.hasFlag(options::OPT_fobjc_arc_exceptions,
                     options::OPT_fno_objc_arc_exceptions,
                     /*default=*/types::isCXX(Input.getType())))
      CmdArgs.push_back("-fobjc-arc-exceptions");
  }

  // Silence warning for full exception code emission options when explicitly
  // set to use no ARC.
  if (Args.hasArg(options::OPT_fno_objc_arc)) {
    Args.ClaimAllArgs(options::OPT_fobjc_arc_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_objc_arc_exceptions);
  }

  // Allow the user to control whether messages can be converted to runtime
  // functions.
  if (types::isObjC(Input.getType())) {
    auto *Arg = Args.getLastArg(
        options::OPT_fobjc_convert_messages_to_runtime_calls,
        options::OPT_fno_objc_convert_messages_to_runtime_calls);
    if (Arg &&
        Arg->getOption().matches(
            options::OPT_fno_objc_convert_messages_to_runtime_calls))
      CmdArgs.push_back("-fno-objc-convert-messages-to-runtime-calls");
  }

  // -fobjc-infer-related-result-type is the default, except in the Objective-C
  // rewriter.
  if (InferCovariantReturns)
    CmdArgs.push_back("-fno-objc-infer-related-result-type");

  // Pass down -fobjc-weak or -fno-objc-weak if present.
  if (types::isObjC(Input.getType())) {
    auto WeakArg =
        Args.getLastArg(options::OPT_fobjc_weak, options::OPT_fno_objc_weak);
    if (!WeakArg) {
      // nothing to do
    } else if (!Runtime.allowsWeak()) {
      if (WeakArg->getOption().matches(options::OPT_fobjc_weak))
        D.Diag(diag::err_objc_weak_unsupported);
    } else {
      WeakArg->render(Args, CmdArgs);
    }
  }
}

static void RenderDiagnosticsOptions(const Driver &D, const ArgList &Args,
                                     ArgStringList &CmdArgs) {
  bool CaretDefault = true;
  bool ColumnDefault = true;

  if (const Arg *A = Args.getLastArg(options::OPT__SLASH_diagnostics_classic,
                                     options::OPT__SLASH_diagnostics_column,
                                     options::OPT__SLASH_diagnostics_caret)) {
    switch (A->getOption().getID()) {
    case options::OPT__SLASH_diagnostics_caret:
      CaretDefault = true;
      ColumnDefault = true;
      break;
    case options::OPT__SLASH_diagnostics_column:
      CaretDefault = false;
      ColumnDefault = true;
      break;
    case options::OPT__SLASH_diagnostics_classic:
      CaretDefault = false;
      ColumnDefault = false;
      break;
    }
  }

  // -fcaret-diagnostics is default.
  if (!Args.hasFlag(options::OPT_fcaret_diagnostics,
                    options::OPT_fno_caret_diagnostics, CaretDefault))
    CmdArgs.push_back("-fno-caret-diagnostics");

  // -fdiagnostics-fixit-info is default, only pass non-default.
  if (!Args.hasFlag(options::OPT_fdiagnostics_fixit_info,
                    options::OPT_fno_diagnostics_fixit_info))
    CmdArgs.push_back("-fno-diagnostics-fixit-info");

  // Enable -fdiagnostics-show-option by default.
  if (Args.hasFlag(options::OPT_fdiagnostics_show_option,
                   options::OPT_fno_diagnostics_show_option))
    CmdArgs.push_back("-fdiagnostics-show-option");

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_show_category_EQ)) {
    CmdArgs.push_back("-fdiagnostics-show-category");
    CmdArgs.push_back(A->getValue());
  }

  if (Args.hasFlag(options::OPT_fdiagnostics_show_hotness,
                   options::OPT_fno_diagnostics_show_hotness, false))
    CmdArgs.push_back("-fdiagnostics-show-hotness");

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    std::string Opt =
        std::string("-fdiagnostics-hotness-threshold=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(Opt));
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    CmdArgs.push_back(A->getValue());
  }

  if (const Arg *A = Args.getLastArg(
          options::OPT_fdiagnostics_show_note_include_stack,
          options::OPT_fno_diagnostics_show_note_include_stack)) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fdiagnostics_show_note_include_stack))
      CmdArgs.push_back("-fdiagnostics-show-note-include-stack");
    else
      CmdArgs.push_back("-fno-diagnostics-show-note-include-stack");
  }

  // Color diagnostics are parsed by the driver directly from argv and later
  // re-parsed to construct this job; claim any possible color diagnostic here
  // to avoid warn_drv_unused_argument and diagnose bad
  // OPT_fdiagnostics_color_EQ values.
  for (const Arg *A : Args) {
    const Option &O = A->getOption();
    if (!O.matches(options::OPT_fcolor_diagnostics) &&
        !O.matches(options::OPT_fdiagnostics_color) &&
        !O.matches(options::OPT_fno_color_diagnostics) &&
        !O.matches(options::OPT_fno_diagnostics_color) &&
        !O.matches(options::OPT_fdiagnostics_color_EQ))
      continue;

    if (O.matches(options::OPT_fdiagnostics_color_EQ)) {
      StringRef Value(A->getValue());
      if (Value != "always" && Value != "never" && Value != "auto")
        D.Diag(diag::err_drv_clang_unsupported)
            << ("-fdiagnostics-color=" + Value).str();
    }
    A->claim();
  }

  if (D.getDiags().getDiagnosticOptions().ShowColors)
    CmdArgs.push_back("-fcolor-diagnostics");

  if (Args.hasArg(options::OPT_fansi_escape_codes))
    CmdArgs.push_back("-fansi-escape-codes");

  if (!Args.hasFlag(options::OPT_fshow_source_location,
                    options::OPT_fno_show_source_location))
    CmdArgs.push_back("-fno-show-source-location");

  if (Args.hasArg(options::OPT_fdiagnostics_absolute_paths))
    CmdArgs.push_back("-fdiagnostics-absolute-paths");

  if (!Args.hasFlag(options::OPT_fshow_column, options::OPT_fno_show_column,
                    ColumnDefault))
    CmdArgs.push_back("-fno-show-column");

  if (!Args.hasFlag(options::OPT_fspell_checking,
                    options::OPT_fno_spell_checking))
    CmdArgs.push_back("-fno-spell-checking");
}

enum class DwarfFissionKind { None, Split, Single };

static DwarfFissionKind getDebugFissionKind(const Driver &D,
                                            const ArgList &Args, Arg *&Arg) {
  Arg =
      Args.getLastArg(options::OPT_gsplit_dwarf, options::OPT_gsplit_dwarf_EQ);
  if (!Arg)
    return DwarfFissionKind::None;

  if (Arg->getOption().matches(options::OPT_gsplit_dwarf))
    return DwarfFissionKind::Split;

  StringRef Value = Arg->getValue();
  if (Value == "split")
    return DwarfFissionKind::Split;
  if (Value == "single")
    return DwarfFissionKind::Single;

  D.Diag(diag::err_drv_unsupported_option_argument)
      << Arg->getOption().getName() << Arg->getValue();
  return DwarfFissionKind::None;
}

static void RenderDebugOptions(const ToolChain &TC, const Driver &D,
                               const llvm::Triple &T, const ArgList &Args,
                               bool EmitCodeView, bool IsWindowsMSVC,
                               ArgStringList &CmdArgs,
                               codegenoptions::DebugInfoKind &DebugInfoKind,
                               DwarfFissionKind &DwarfFission) {
  if (Args.hasFlag(options::OPT_fdebug_info_for_profiling,
                   options::OPT_fno_debug_info_for_profiling, false) &&
      checkDebugInfoOption(
          Args.getLastArg(options::OPT_fdebug_info_for_profiling), Args, D, TC))
    CmdArgs.push_back("-fdebug-info-for-profiling");

  // The 'g' groups options involve a somewhat intricate sequence of decisions
  // about what to pass from the driver to the frontend, but by the time they
  // reach cc1 they've been factored into three well-defined orthogonal choices:
  //  * what level of debug info to generate
  //  * what dwarf version to write
  //  * what debugger tuning to use
  // This avoids having to monkey around further in cc1 other than to disable
  // codeview if not running in a Windows environment. Perhaps even that
  // decision should be made in the driver as well though.
  unsigned DWARFVersion = 0;
  llvm::DebuggerKind DebuggerTuning = TC.getDefaultDebuggerTuning();

  bool SplitDWARFInlining =
      Args.hasFlag(options::OPT_fsplit_dwarf_inlining,
                   options::OPT_fno_split_dwarf_inlining, true);

  Args.ClaimAllArgs(options::OPT_g_Group);

  Arg* SplitDWARFArg;
  DwarfFission = getDebugFissionKind(D, Args, SplitDWARFArg);

  if (DwarfFission != DwarfFissionKind::None &&
      !checkDebugInfoOption(SplitDWARFArg, Args, D, TC)) {
    DwarfFission = DwarfFissionKind::None;
    SplitDWARFInlining = false;
  }

  if (const Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    if (checkDebugInfoOption(A, Args, D, TC)) {
      // If the last option explicitly specified a debug-info level, use it.
      if (A->getOption().matches(options::OPT_gN_Group)) {
        DebugInfoKind = DebugLevelToInfoKind(*A);
        // If you say "-gsplit-dwarf -gline-tables-only", -gsplit-dwarf loses.
        // But -gsplit-dwarf is not a g_group option, hence we have to check the
        // order explicitly. If -gsplit-dwarf wins, we fix DebugInfoKind later.
        // This gets a bit more complicated if you've disabled inline info in
        // the skeleton CUs (SplitDWARFInlining) - then there's value in
        // composing split-dwarf and line-tables-only, so let those compose
        // naturally in that case. And if you just turned off debug info,
        // (-gsplit-dwarf -g0) - do that.
        if (DwarfFission != DwarfFissionKind::None) {
          if (A->getIndex() > SplitDWARFArg->getIndex()) {
            if (DebugInfoKind == codegenoptions::NoDebugInfo ||
                DebugInfoKind == codegenoptions::DebugDirectivesOnly ||
                (DebugInfoKind == codegenoptions::DebugLineTablesOnly &&
                 SplitDWARFInlining))
              DwarfFission = DwarfFissionKind::None;
          } else if (SplitDWARFInlining)
            DebugInfoKind = codegenoptions::NoDebugInfo;
        }
      } else {
        // For any other 'g' option, use Limited.
        DebugInfoKind = codegenoptions::LimitedDebugInfo;
      }
    } else {
      DebugInfoKind = codegenoptions::LimitedDebugInfo;
    }
  }

  // If a debugger tuning argument appeared, remember it.
  if (const Arg *A =
          Args.getLastArg(options::OPT_gTune_Group, options::OPT_ggdbN_Group)) {
    if (checkDebugInfoOption(A, Args, D, TC)) {
      if (A->getOption().matches(options::OPT_glldb))
        DebuggerTuning = llvm::DebuggerKind::LLDB;
      else if (A->getOption().matches(options::OPT_gsce))
        DebuggerTuning = llvm::DebuggerKind::SCE;
      else
        DebuggerTuning = llvm::DebuggerKind::GDB;
    }
  }

  // If a -gdwarf argument appeared, remember it.
  if (const Arg *A =
          Args.getLastArg(options::OPT_gdwarf_2, options::OPT_gdwarf_3,
                          options::OPT_gdwarf_4, options::OPT_gdwarf_5))
    if (checkDebugInfoOption(A, Args, D, TC))
      DWARFVersion = DwarfVersionNum(A->getSpelling());

  if (const Arg *A = Args.getLastArg(options::OPT_gcodeview)) {
    if (checkDebugInfoOption(A, Args, D, TC))
      EmitCodeView = true;
  }

  // If the user asked for debug info but did not explicitly specify -gcodeview
  // or -gdwarf, ask the toolchain for the default format.
  if (!EmitCodeView && DWARFVersion == 0 &&
      DebugInfoKind != codegenoptions::NoDebugInfo) {
    switch (TC.getDefaultDebugFormat()) {
    case codegenoptions::DIF_CodeView:
      EmitCodeView = true;
      break;
    case codegenoptions::DIF_DWARF:
      DWARFVersion = TC.GetDefaultDwarfVersion();
      break;
    }
  }

  // -gline-directives-only supported only for the DWARF debug info.
  if (DWARFVersion == 0 && DebugInfoKind == codegenoptions::DebugDirectivesOnly)
    DebugInfoKind = codegenoptions::NoDebugInfo;

  // We ignore flag -gstrict-dwarf for now.
  // And we handle flag -grecord-gcc-switches later with DWARFDebugFlags.
  Args.ClaimAllArgs(options::OPT_g_flags_Group);

  // Column info is included by default for everything except SCE and
  // CodeView. Clang doesn't track end columns, just starting columns, which,
  // in theory, is fine for CodeView (and PDB).  In practice, however, the
  // Microsoft debuggers don't handle missing end columns well, so it's better
  // not to include any column info.
  if (const Arg *A = Args.getLastArg(options::OPT_gcolumn_info))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (Args.hasFlag(options::OPT_gcolumn_info, options::OPT_gno_column_info,
                   /*Default=*/!EmitCodeView &&
                       DebuggerTuning != llvm::DebuggerKind::SCE))
    CmdArgs.push_back("-dwarf-column-info");

  // FIXME: Move backend command line options to the module.
  // If -gline-tables-only or -gline-directives-only is the last option it wins.
  if (const Arg *A = Args.getLastArg(options::OPT_gmodules))
    if (checkDebugInfoOption(A, Args, D, TC)) {
      if (DebugInfoKind != codegenoptions::DebugLineTablesOnly &&
          DebugInfoKind != codegenoptions::DebugDirectivesOnly) {
        DebugInfoKind = codegenoptions::LimitedDebugInfo;
        CmdArgs.push_back("-dwarf-ext-refs");
        CmdArgs.push_back("-fmodule-format=obj");
      }
    }

  // -gsplit-dwarf should turn on -g and enable the backend dwarf
  // splitting and extraction.
  // FIXME: Currently only works on Linux and Fuchsia.
  if (T.isOSLinux() || T.isOSFuchsia()) {
    if (!SplitDWARFInlining)
      CmdArgs.push_back("-fno-split-dwarf-inlining");

    if (DwarfFission != DwarfFissionKind::None) {
      if (DebugInfoKind == codegenoptions::NoDebugInfo)
        DebugInfoKind = codegenoptions::LimitedDebugInfo;

      if (DwarfFission == DwarfFissionKind::Single)
        CmdArgs.push_back("-enable-split-dwarf=single");
      else
        CmdArgs.push_back("-enable-split-dwarf");
    }
  }

  // After we've dealt with all combinations of things that could
  // make DebugInfoKind be other than None or DebugLineTablesOnly,
  // figure out if we need to "upgrade" it to standalone debug info.
  // We parse these two '-f' options whether or not they will be used,
  // to claim them even if you wrote "-fstandalone-debug -gline-tables-only"
  bool NeedFullDebug = Args.hasFlag(options::OPT_fstandalone_debug,
                                    options::OPT_fno_standalone_debug,
                                    TC.GetDefaultStandaloneDebug());
  if (const Arg *A = Args.getLastArg(options::OPT_fstandalone_debug))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (DebugInfoKind == codegenoptions::LimitedDebugInfo && NeedFullDebug)
    DebugInfoKind = codegenoptions::FullDebugInfo;

  if (Args.hasFlag(options::OPT_gembed_source, options::OPT_gno_embed_source,
                   false)) {
    // Source embedding is a vendor extension to DWARF v5. By now we have
    // checked if a DWARF version was stated explicitly, and have otherwise
    // fallen back to the target default, so if this is still not at least 5
    // we emit an error.
    const Arg *A = Args.getLastArg(options::OPT_gembed_source);
    if (DWARFVersion < 5)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-gdwarf-5";
    else if (checkDebugInfoOption(A, Args, D, TC))
      CmdArgs.push_back("-gembed-source");
  }

  if (EmitCodeView) {
    CmdArgs.push_back("-gcodeview");

    // Emit codeview type hashes if requested.
    if (Args.hasFlag(options::OPT_gcodeview_ghash,
                     options::OPT_gno_codeview_ghash, false)) {
      CmdArgs.push_back("-gcodeview-ghash");
    }
  }

  // Adjust the debug info kind for the given toolchain.
  TC.adjustDebugInfoKind(DebugInfoKind, Args);

  RenderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, DWARFVersion,
                          DebuggerTuning);

  // -fdebug-macro turns on macro debug info generation.
  if (Args.hasFlag(options::OPT_fdebug_macro, options::OPT_fno_debug_macro,
                   false))
    if (checkDebugInfoOption(Args.getLastArg(options::OPT_fdebug_macro), Args,
                             D, TC))
      CmdArgs.push_back("-debug-info-macro");

  // -ggnu-pubnames turns on gnu style pubnames in the backend.
  const auto *PubnamesArg =
      Args.getLastArg(options::OPT_ggnu_pubnames, options::OPT_gno_gnu_pubnames,
                      options::OPT_gpubnames, options::OPT_gno_pubnames);
  if (DwarfFission != DwarfFissionKind::None ||
      DebuggerTuning == llvm::DebuggerKind::LLDB ||
      (PubnamesArg && checkDebugInfoOption(PubnamesArg, Args, D, TC)))
    if (!PubnamesArg ||
        (!PubnamesArg->getOption().matches(options::OPT_gno_gnu_pubnames) &&
         !PubnamesArg->getOption().matches(options::OPT_gno_pubnames)))
      CmdArgs.push_back(PubnamesArg && PubnamesArg->getOption().matches(
                                           options::OPT_gpubnames)
                            ? "-gpubnames"
                            : "-ggnu-pubnames");

  if (Args.hasFlag(options::OPT_fdebug_ranges_base_address,
                   options::OPT_fno_debug_ranges_base_address, false)) {
    CmdArgs.push_back("-fdebug-ranges-base-address");
  }

  // -gdwarf-aranges turns on the emission of the aranges section in the
  // backend.
  // Always enabled for SCE tuning.
  bool NeedAranges = DebuggerTuning == llvm::DebuggerKind::SCE;
  if (const Arg *A = Args.getLastArg(options::OPT_gdwarf_aranges))
    NeedAranges = checkDebugInfoOption(A, Args, D, TC) || NeedAranges;
  if (NeedAranges) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-generate-arange-section");
  }

  if (Args.hasFlag(options::OPT_fdebug_types_section,
                   options::OPT_fno_debug_types_section, false)) {
    if (!T.isOSBinFormatELF()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << Args.getLastArg(options::OPT_fdebug_types_section)
                 ->getAsString(Args)
          << T.getTriple();
    } else if (checkDebugInfoOption(
                   Args.getLastArg(options::OPT_fdebug_types_section), Args, D,
                   TC)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-generate-type-units");
    }
  }

  // Decide how to render forward declarations of template instantiations.
  // SCE wants full descriptions, others just get them in the name.
  if (DebuggerTuning == llvm::DebuggerKind::SCE)
    CmdArgs.push_back("-debug-forward-template-params");

  // Do we need to explicitly import anonymous namespaces into the parent
  // scope?
  if (DebuggerTuning == llvm::DebuggerKind::SCE)
    CmdArgs.push_back("-dwarf-explicit-import");

  RenderDebugInfoCompressionArgs(Args, CmdArgs, D, TC);
}

void Clang::ConstructJob(Compilation &C, const JobAction &JA,
                         const InputInfo &Output, const InputInfoList &Inputs,
                         const ArgList &Args, const char *LinkingOutput) const {
  const auto &TC = getToolChain();
  const llvm::Triple &RawTriple = TC.getTriple();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();

  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  // Check number of inputs for sanity. We need at least one input.
  assert(Inputs.size() >= 1 && "Must have at least one input.");
  // CUDA/HIP compilation may have multiple inputs (source file + results of
  // device-side compilations). OpenMP device jobs also take the host IR as a
  // second input. Module precompilation accepts a list of header files to
  // include as part of the module. All other jobs are expected to have exactly
  // one input.
  bool IsCuda = JA.isOffloading(Action::OFK_Cuda);
  bool IsHIP = JA.isOffloading(Action::OFK_HIP);
  bool IsOpenMPDevice = JA.isDeviceOffloading(Action::OFK_OpenMP);
  bool IsHeaderModulePrecompile = isa<HeaderModulePrecompileJobAction>(JA);

  // A header module compilation doesn't have a main input file, so invent a
  // fake one as a placeholder.
  const char *ModuleName = [&]{
    auto *ModuleNameArg = Args.getLastArg(options::OPT_fmodule_name_EQ);
    return ModuleNameArg ? ModuleNameArg->getValue() : "";
  }();
  InputInfo HeaderModuleInput(Inputs[0].getType(), ModuleName, ModuleName);

  const InputInfo &Input =
      IsHeaderModulePrecompile ? HeaderModuleInput : Inputs[0];

  InputInfoList ModuleHeaderInputs;
  const InputInfo *CudaDeviceInput = nullptr;
  const InputInfo *OpenMPDeviceInput = nullptr;
  for (const InputInfo &I : Inputs) {
    if (&I == &Input) {
      // This is the primary input.
    } else if (IsHeaderModulePrecompile &&
               types::getPrecompiledType(I.getType()) == types::TY_PCH) {
      types::ID Expected = HeaderModuleInput.getType();
      if (I.getType() != Expected) {
        D.Diag(diag::err_drv_module_header_wrong_kind)
            << I.getFilename() << types::getTypeName(I.getType())
            << types::getTypeName(Expected);
      }
      ModuleHeaderInputs.push_back(I);
    } else if ((IsCuda || IsHIP) && !CudaDeviceInput) {
      CudaDeviceInput = &I;
    } else if (IsOpenMPDevice && !OpenMPDeviceInput) {
      OpenMPDeviceInput = &I;
    } else {
      llvm_unreachable("unexpectedly given multiple inputs");
    }
  }

  const llvm::Triple *AuxTriple = IsCuda ? TC.getAuxTriple() : nullptr;
  bool IsWindowsGNU = RawTriple.isWindowsGNUEnvironment();
  bool IsWindowsCygnus = RawTriple.isWindowsCygwinEnvironment();
  bool IsWindowsMSVC = RawTriple.isWindowsMSVCEnvironment();
  bool IsIAMCU = RawTriple.isOSIAMCU();

  // Adjust IsWindowsXYZ for CUDA/HIP compilations.  Even when compiling in
  // device mode (i.e., getToolchain().getTriple() is NVPTX/AMDGCN, not
  // Windows), we need to pass Windows-specific flags to cc1.
  if (IsCuda || IsHIP) {
    IsWindowsMSVC |= AuxTriple && AuxTriple->isWindowsMSVCEnvironment();
    IsWindowsGNU |= AuxTriple && AuxTriple->isWindowsGNUEnvironment();
    IsWindowsCygnus |= AuxTriple && AuxTriple->isWindowsCygwinEnvironment();
  }

  // C++ is not supported for IAMCU.
  if (IsIAMCU && types::isCXX(Input.getType()))
    D.Diag(diag::err_drv_clang_unsupported) << "C++ for IAMCU";

  // Invoke ourselves in -cc1 mode.
  //
  // FIXME: Implement custom jobs for internal actions.
  CmdArgs.push_back("-cc1");

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  if (const Arg *MJ = Args.getLastArg(options::OPT_MJ)) {
    DumpCompilationDatabase(C, MJ->getValue(), TripleStr, Output, Input, Args);
    Args.ClaimAllArgs(options::OPT_MJ);
  }

  if (IsCuda || IsHIP) {
    // We have to pass the triple of the host if compiling for a CUDA/HIP device
    // and vice-versa.
    std::string NormalizedTriple;
    if (JA.isDeviceOffloading(Action::OFK_Cuda) ||
        JA.isDeviceOffloading(Action::OFK_HIP))
      NormalizedTriple = C.getSingleOffloadToolChain<Action::OFK_Host>()
                             ->getTriple()
                             .normalize();
    else
      NormalizedTriple =
          (IsCuda ? C.getSingleOffloadToolChain<Action::OFK_Cuda>()
                  : C.getSingleOffloadToolChain<Action::OFK_HIP>())
              ->getTriple()
              .normalize();

    CmdArgs.push_back("-aux-triple");
    CmdArgs.push_back(Args.MakeArgString(NormalizedTriple));
  }

  if (IsOpenMPDevice) {
    // We have to pass the triple of the host if compiling for an OpenMP device.
    std::string NormalizedTriple =
        C.getSingleOffloadToolChain<Action::OFK_Host>()
            ->getTriple()
            .normalize();
    CmdArgs.push_back("-aux-triple");
    CmdArgs.push_back(Args.MakeArgString(NormalizedTriple));
  }

  if (Triple.isOSWindows() && (Triple.getArch() == llvm::Triple::arm ||
                               Triple.getArch() == llvm::Triple::thumb)) {
    unsigned Offset = Triple.getArch() == llvm::Triple::arm ? 4 : 6;
    unsigned Version;
    Triple.getArchName().substr(Offset).getAsInteger(10, Version);
    if (Version < 7)
      D.Diag(diag::err_target_unsupported_arch) << Triple.getArchName()
                                                << TripleStr;
  }

  // Push all default warning arguments that are specific to
  // the given target.  These come before user provided warning options
  // are provided.
  TC.addClangWarningOptions(CmdArgs);

  // Select the appropriate action.
  RewriteKind rewriteKind = RK_None;

  if (isa<AnalyzeJobAction>(JA)) {
    assert(JA.getType() == types::TY_Plist && "Invalid output type.");
    CmdArgs.push_back("-analyze");
  } else if (isa<MigrateJobAction>(JA)) {
    CmdArgs.push_back("-migrate");
  } else if (isa<PreprocessJobAction>(JA)) {
    if (Output.getType() == types::TY_Dependencies)
      CmdArgs.push_back("-Eonly");
    else {
      CmdArgs.push_back("-E");
      if (Args.hasArg(options::OPT_rewrite_objc) &&
          !Args.hasArg(options::OPT_g_Group))
        CmdArgs.push_back("-P");
    }
  } else if (isa<AssembleJobAction>(JA)) {
    CmdArgs.push_back("-emit-obj");

    CollectArgsForIntegratedAssembler(C, Args, CmdArgs, D);

    // Also ignore explicit -force_cpusubtype_ALL option.
    (void)Args.hasArg(options::OPT_force__cpusubtype__ALL);
  } else if (isa<PrecompileJobAction>(JA)) {
    if (JA.getType() == types::TY_Nothing)
      CmdArgs.push_back("-fsyntax-only");
    else if (JA.getType() == types::TY_ModuleFile)
      CmdArgs.push_back(IsHeaderModulePrecompile
                            ? "-emit-header-module"
                            : "-emit-module-interface");
    else
      CmdArgs.push_back("-emit-pch");
  } else if (isa<VerifyPCHJobAction>(JA)) {
    CmdArgs.push_back("-verify-pch");
  } else {
    assert((isa<CompileJobAction>(JA) || isa<BackendJobAction>(JA)) &&
           "Invalid action for clang tool.");
    if (JA.getType() == types::TY_Nothing) {
      CmdArgs.push_back("-fsyntax-only");
    } else if (JA.getType() == types::TY_LLVM_IR ||
               JA.getType() == types::TY_LTO_IR) {
      CmdArgs.push_back("-emit-llvm");
    } else if (JA.getType() == types::TY_LLVM_BC ||
               JA.getType() == types::TY_LTO_BC) {
      CmdArgs.push_back("-emit-llvm-bc");
    } else if (JA.getType() == types::TY_PP_Asm) {
      CmdArgs.push_back("-S");
    } else if (JA.getType() == types::TY_AST) {
      CmdArgs.push_back("-emit-pch");
    } else if (JA.getType() == types::TY_ModuleFile) {
      CmdArgs.push_back("-module-file-info");
    } else if (JA.getType() == types::TY_RewrittenObjC) {
      CmdArgs.push_back("-rewrite-objc");
      rewriteKind = RK_NonFragile;
    } else if (JA.getType() == types::TY_RewrittenLegacyObjC) {
      CmdArgs.push_back("-rewrite-objc");
      rewriteKind = RK_Fragile;
    } else {
      assert(JA.getType() == types::TY_PP_Asm && "Unexpected output type!");
    }

    // Preserve use-list order by default when emitting bitcode, so that
    // loading the bitcode up in 'opt' or 'llc' and running passes gives the
    // same result as running passes here.  For LTO, we don't need to preserve
    // the use-list order, since serialization to bitcode is part of the flow.
    if (JA.getType() == types::TY_LLVM_BC)
      CmdArgs.push_back("-emit-llvm-uselists");

    // Device-side jobs do not support LTO.
    bool isDeviceOffloadAction = !(JA.isDeviceOffloading(Action::OFK_None) ||
                                   JA.isDeviceOffloading(Action::OFK_Host));

    if (D.isUsingLTO() && !isDeviceOffloadAction) {
      Args.AddLastArg(CmdArgs, options::OPT_flto, options::OPT_flto_EQ);

      // The Darwin and PS4 linkers currently use the legacy LTO API, which
      // does not support LTO unit features (CFI, whole program vtable opt)
      // under ThinLTO.
      if (!(RawTriple.isOSDarwin() || RawTriple.isPS4()) ||
          D.getLTOMode() == LTOK_Full)
        CmdArgs.push_back("-flto-unit");
    }
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fthinlto_index_EQ)) {
    if (!types::isLLVMIR(Input.getType()))
      D.Diag(diag::err_drv_argument_only_allowed_with) << A->getAsString(Args)
                                                       << "-x ir";
    Args.AddLastArg(CmdArgs, options::OPT_fthinlto_index_EQ);
  }

  if (Args.getLastArg(options::OPT_save_temps_EQ))
    Args.AddLastArg(CmdArgs, options::OPT_save_temps_EQ);

  // Embed-bitcode option.
  // Only white-listed flags below are allowed to be embedded.
  if (C.getDriver().embedBitcodeInObject() && !C.getDriver().isUsingLTO() &&
      (isa<BackendJobAction>(JA) || isa<AssembleJobAction>(JA))) {
    // Add flags implied by -fembed-bitcode.
    Args.AddLastArg(CmdArgs, options::OPT_fembed_bitcode_EQ);
    // Disable all llvm IR level optimizations.
    CmdArgs.push_back("-disable-llvm-passes");

    // reject options that shouldn't be supported in bitcode
    // also reject kernel/kext
    static const constexpr unsigned kBitcodeOptionBlacklist[] = {
        options::OPT_mkernel,
        options::OPT_fapple_kext,
        options::OPT_ffunction_sections,
        options::OPT_fno_function_sections,
        options::OPT_fdata_sections,
        options::OPT_fno_data_sections,
        options::OPT_funique_section_names,
        options::OPT_fno_unique_section_names,
        options::OPT_mrestrict_it,
        options::OPT_mno_restrict_it,
        options::OPT_mstackrealign,
        options::OPT_mno_stackrealign,
        options::OPT_mstack_alignment,
        options::OPT_mcmodel_EQ,
        options::OPT_mlong_calls,
        options::OPT_mno_long_calls,
        options::OPT_ggnu_pubnames,
        options::OPT_gdwarf_aranges,
        options::OPT_fdebug_types_section,
        options::OPT_fno_debug_types_section,
        options::OPT_fdwarf_directory_asm,
        options::OPT_fno_dwarf_directory_asm,
        options::OPT_mrelax_all,
        options::OPT_mno_relax_all,
        options::OPT_ftrap_function_EQ,
        options::OPT_ffixed_r9,
        options::OPT_mfix_cortex_a53_835769,
        options::OPT_mno_fix_cortex_a53_835769,
        options::OPT_ffixed_x18,
        options::OPT_mglobal_merge,
        options::OPT_mno_global_merge,
        options::OPT_mred_zone,
        options::OPT_mno_red_zone,
        options::OPT_Wa_COMMA,
        options::OPT_Xassembler,
        options::OPT_mllvm,
    };
    for (const auto &A : Args)
      if (std::find(std::begin(kBitcodeOptionBlacklist),
                    std::end(kBitcodeOptionBlacklist),
                    A->getOption().getID()) !=
          std::end(kBitcodeOptionBlacklist))
        D.Diag(diag::err_drv_unsupported_embed_bitcode) << A->getSpelling();

    // Render the CodeGen options that need to be passed.
    if (!Args.hasFlag(options::OPT_foptimize_sibling_calls,
                      options::OPT_fno_optimize_sibling_calls))
      CmdArgs.push_back("-mdisable-tail-calls");

    RenderFloatingPointOptions(TC, D, isOptimizationLevelFast(Args), Args,
                               CmdArgs);

    // Render ABI arguments
    switch (TC.getArch()) {
    default: break;
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumbeb:
      RenderARMABI(Triple, Args, CmdArgs);
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_be:
      RenderAArch64ABI(Triple, Args, CmdArgs);
      break;
    }

    // Optimization level for CodeGen.
    if (const Arg *A = Args.getLastArg(options::OPT_O_Group)) {
      if (A->getOption().matches(options::OPT_O4)) {
        CmdArgs.push_back("-O3");
        D.Diag(diag::warn_O4_is_O3);
      } else {
        A->render(Args, CmdArgs);
      }
    }

    // Input/Output file.
    if (Output.getType() == types::TY_Dependencies) {
      // Handled with other dependency code.
    } else if (Output.isFilename()) {
      CmdArgs.push_back("-o");
      CmdArgs.push_back(Output.getFilename());
    } else {
      assert(Output.isNothing() && "Input output.");
    }

    for (const auto &II : Inputs) {
      addDashXForInput(Args, II, CmdArgs);
      if (II.isFilename())
        CmdArgs.push_back(II.getFilename());
      else
        II.getInputArg().renderAsInput(Args, CmdArgs);
    }

    C.addCommand(llvm::make_unique<Command>(JA, *this, D.getClangProgramPath(),
                                            CmdArgs, Inputs));
    return;
  }

  if (C.getDriver().embedBitcodeMarkerOnly() && !C.getDriver().isUsingLTO())
    CmdArgs.push_back("-fembed-bitcode=marker");

  // We normally speed up the clang process a bit by skipping destructors at
  // exit, but when we're generating diagnostics we can rely on some of the
  // cleanup.
  if (!C.isForDiagnostics())
    CmdArgs.push_back("-disable-free");

#ifdef NDEBUG
  const bool IsAssertBuild = false;
#else
  const bool IsAssertBuild = true;
#endif

  // Disable the verification pass in -asserts builds.
  if (!IsAssertBuild)
    CmdArgs.push_back("-disable-llvm-verifier");

  // Discard value names in assert builds unless otherwise specified.
  if (Args.hasFlag(options::OPT_fdiscard_value_names,
                   options::OPT_fno_discard_value_names, !IsAssertBuild))
    CmdArgs.push_back("-discard-value-names");

  // Set the main file name, so that debug info works even with
  // -save-temps.
  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(getBaseInputName(Args, Input));

  // Some flags which affect the language (via preprocessor
  // defines).
  if (Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-static-define");

  if (Args.hasArg(options::OPT_municode))
    CmdArgs.push_back("-DUNICODE");

  if (isa<AnalyzeJobAction>(JA))
    RenderAnalyzerOptions(Args, CmdArgs, Triple, Input);

  // Enable compatilibily mode to avoid analyzer-config related errors.
  // Since we can't access frontend flags through hasArg, let's manually iterate
  // through them.
  bool FoundAnalyzerConfig = false;
  for (auto Arg : Args.filtered(options::OPT_Xclang))
    if (StringRef(Arg->getValue()) == "-analyzer-config") {
      FoundAnalyzerConfig = true;
      break;
    }
  if (!FoundAnalyzerConfig)
    for (auto Arg : Args.filtered(options::OPT_Xanalyzer))
      if (StringRef(Arg->getValue()) == "-analyzer-config") {
        FoundAnalyzerConfig = true;
        break;
      }
  if (FoundAnalyzerConfig)
    CmdArgs.push_back("-analyzer-config-compatibility-mode=true");

  CheckCodeGenerationOptions(D, Args);

  unsigned FunctionAlignment = ParseFunctionAlignment(TC, Args);
  assert(FunctionAlignment <= 31 && "function alignment will be truncated!");
  if (FunctionAlignment) {
    CmdArgs.push_back("-function-alignment");
    CmdArgs.push_back(Args.MakeArgString(std::to_string(FunctionAlignment)));
  }

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) = ParsePICArgs(TC, Args);

  const char *RMName = RelocationModelName(RelocationModel);

  if ((RelocationModel == llvm::Reloc::ROPI ||
       RelocationModel == llvm::Reloc::ROPI_RWPI) &&
      types::isCXX(Input.getType()) &&
      !Args.hasArg(options::OPT_fallow_unsupported))
    D.Diag(diag::err_drv_ropi_incompatible_with_cxx);

  if (RMName) {
    CmdArgs.push_back("-mrelocation-model");
    CmdArgs.push_back(RMName);
  }
  if (PICLevel > 0) {
    CmdArgs.push_back("-pic-level");
    CmdArgs.push_back(PICLevel == 1 ? "1" : "2");
    if (IsPIE)
      CmdArgs.push_back("-pic-is-pie");
  }

  if (Arg *A = Args.getLastArg(options::OPT_meabi)) {
    CmdArgs.push_back("-meabi");
    CmdArgs.push_back(A->getValue());
  }

  CmdArgs.push_back("-mthread-model");
  if (Arg *A = Args.getLastArg(options::OPT_mthread_model)) {
    if (!TC.isThreadModelSupported(A->getValue()))
      D.Diag(diag::err_drv_invalid_thread_model_for_target)
          << A->getValue() << A->getAsString(Args);
    CmdArgs.push_back(A->getValue());
  }
  else
    CmdArgs.push_back(Args.MakeArgString(TC.getThreadModel()));

  Args.AddLastArg(CmdArgs, options::OPT_fveclib);

  if (Args.hasFlag(options::OPT_fmerge_all_constants,
                   options::OPT_fno_merge_all_constants, false))
    CmdArgs.push_back("-fmerge-all-constants");

  if (Args.hasFlag(options::OPT_fno_delete_null_pointer_checks,
                   options::OPT_fdelete_null_pointer_checks, false))
    CmdArgs.push_back("-fno-delete-null-pointer-checks");

  // LLVM Code Generator Options.

  if (Args.hasArg(options::OPT_frewrite_map_file) ||
      Args.hasArg(options::OPT_frewrite_map_file_EQ)) {
    for (const Arg *A : Args.filtered(options::OPT_frewrite_map_file,
                                      options::OPT_frewrite_map_file_EQ)) {
      StringRef Map = A->getValue();
      if (!llvm::sys::fs::exists(Map)) {
        D.Diag(diag::err_drv_no_such_file) << Map;
      } else {
        CmdArgs.push_back("-frewrite-map-file");
        CmdArgs.push_back(A->getValue());
        A->claim();
      }
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_Wframe_larger_than_EQ)) {
    StringRef v = A->getValue();
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-warn-stack-size=" + v));
    A->claim();
  }

  if (!Args.hasFlag(options::OPT_fjump_tables, options::OPT_fno_jump_tables,
                    true))
    CmdArgs.push_back("-fno-jump-tables");

  if (Args.hasFlag(options::OPT_fprofile_sample_accurate,
                   options::OPT_fno_profile_sample_accurate, false))
    CmdArgs.push_back("-fprofile-sample-accurate");

  if (!Args.hasFlag(options::OPT_fpreserve_as_comments,
                    options::OPT_fno_preserve_as_comments, true))
    CmdArgs.push_back("-fno-preserve-as-comments");

  if (Arg *A = Args.getLastArg(options::OPT_mregparm_EQ)) {
    CmdArgs.push_back("-mregparm");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fpcc_struct_return,
                               options::OPT_freg_struct_return)) {
    if (TC.getArch() != llvm::Triple::x86) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << RawTriple.str();
    } else if (A->getOption().matches(options::OPT_fpcc_struct_return)) {
      CmdArgs.push_back("-fpcc-struct-return");
    } else {
      assert(A->getOption().matches(options::OPT_freg_struct_return));
      CmdArgs.push_back("-freg-struct-return");
    }
  }

  if (Args.hasFlag(options::OPT_mrtd, options::OPT_mno_rtd, false))
    CmdArgs.push_back("-fdefault-calling-conv=stdcall");

  if (shouldUseFramePointer(Args, RawTriple))
    CmdArgs.push_back("-mdisable-fp-elim");
  if (!Args.hasFlag(options::OPT_fzero_initialized_in_bss,
                    options::OPT_fno_zero_initialized_in_bss))
    CmdArgs.push_back("-mno-zero-initialized-in-bss");

  bool OFastEnabled = isOptimizationLevelFast(Args);
  // If -Ofast is the optimization level, then -fstrict-aliasing should be
  // enabled.  This alias option is being used to simplify the hasFlag logic.
  OptSpecifier StrictAliasingAliasOption =
      OFastEnabled ? options::OPT_Ofast : options::OPT_fstrict_aliasing;
  // We turn strict aliasing off by default if we're in CL mode, since MSVC
  // doesn't do any TBAA.
  bool TBAAOnByDefault = !D.IsCLMode();
  if (!Args.hasFlag(options::OPT_fstrict_aliasing, StrictAliasingAliasOption,
                    options::OPT_fno_strict_aliasing, TBAAOnByDefault))
    CmdArgs.push_back("-relaxed-aliasing");
  if (!Args.hasFlag(options::OPT_fstruct_path_tbaa,
                    options::OPT_fno_struct_path_tbaa))
    CmdArgs.push_back("-no-struct-path-tbaa");
  if (Args.hasFlag(options::OPT_fstrict_enums, options::OPT_fno_strict_enums,
                   false))
    CmdArgs.push_back("-fstrict-enums");
  if (!Args.hasFlag(options::OPT_fstrict_return, options::OPT_fno_strict_return,
                    true))
    CmdArgs.push_back("-fno-strict-return");
  if (Args.hasFlag(options::OPT_fallow_editor_placeholders,
                   options::OPT_fno_allow_editor_placeholders, false))
    CmdArgs.push_back("-fallow-editor-placeholders");
  if (Args.hasFlag(options::OPT_fstrict_vtable_pointers,
                   options::OPT_fno_strict_vtable_pointers,
                   false))
    CmdArgs.push_back("-fstrict-vtable-pointers");
  if (Args.hasFlag(options::OPT_fforce_emit_vtables,
                   options::OPT_fno_force_emit_vtables,
                   false))
    CmdArgs.push_back("-fforce-emit-vtables");
  if (!Args.hasFlag(options::OPT_foptimize_sibling_calls,
                    options::OPT_fno_optimize_sibling_calls))
    CmdArgs.push_back("-mdisable-tail-calls");
  if (Args.hasFlag(options::OPT_fno_escaping_block_tail_calls,
                   options::OPT_fescaping_block_tail_calls, false))
    CmdArgs.push_back("-fno-escaping-block-tail-calls");

  Args.AddLastArg(CmdArgs, options::OPT_ffine_grained_bitfield_accesses,
                  options::OPT_fno_fine_grained_bitfield_accesses);

  // Handle segmented stacks.
  if (Args.hasArg(options::OPT_fsplit_stack))
    CmdArgs.push_back("-split-stacks");

  RenderFloatingPointOptions(TC, D, OFastEnabled, Args, CmdArgs);

  // Decide whether to use verbose asm. Verbose assembly is the default on
  // toolchains which have the integrated assembler on by default.
  bool IsIntegratedAssemblerDefault = TC.IsIntegratedAssemblerDefault();
  if (Args.hasFlag(options::OPT_fverbose_asm, options::OPT_fno_verbose_asm,
                   IsIntegratedAssemblerDefault) ||
      Args.hasArg(options::OPT_dA))
    CmdArgs.push_back("-masm-verbose");

  if (!TC.useIntegratedAs())
    CmdArgs.push_back("-no-integrated-as");

  if (Args.hasArg(options::OPT_fdebug_pass_structure)) {
    CmdArgs.push_back("-mdebug-pass");
    CmdArgs.push_back("Structure");
  }
  if (Args.hasArg(options::OPT_fdebug_pass_arguments)) {
    CmdArgs.push_back("-mdebug-pass");
    CmdArgs.push_back("Arguments");
  }

  // Enable -mconstructor-aliases except on darwin, where we have to work around
  // a linker bug (see <rdar://problem/7651567>), and CUDA device code, where
  // aliases aren't supported.
  if (!RawTriple.isOSDarwin() && !RawTriple.isNVPTX())
    CmdArgs.push_back("-mconstructor-aliases");

  // Darwin's kernel doesn't support guard variables; just die if we
  // try to use them.
  if (KernelOrKext && RawTriple.isOSDarwin())
    CmdArgs.push_back("-fforbid-guard-variables");

  if (Args.hasFlag(options::OPT_mms_bitfields, options::OPT_mno_ms_bitfields,
                   false)) {
    CmdArgs.push_back("-mms-bitfields");
  }

  if (Args.hasFlag(options::OPT_mpie_copy_relocations,
                   options::OPT_mno_pie_copy_relocations,
                   false)) {
    CmdArgs.push_back("-mpie-copy-relocations");
  }

  if (Args.hasFlag(options::OPT_fno_plt, options::OPT_fplt, false)) {
    CmdArgs.push_back("-fno-plt");
  }

  // -fhosted is default.
  // TODO: Audit uses of KernelOrKext and see where it'd be more appropriate to
  // use Freestanding.
  bool Freestanding =
      Args.hasFlag(options::OPT_ffreestanding, options::OPT_fhosted, false) ||
      KernelOrKext;
  if (Freestanding)
    CmdArgs.push_back("-ffreestanding");

  // This is a coarse approximation of what llvm-gcc actually does, both
  // -fasynchronous-unwind-tables and -fnon-call-exceptions interact in more
  // complicated ways.
  bool AsynchronousUnwindTables =
      Args.hasFlag(options::OPT_fasynchronous_unwind_tables,
                   options::OPT_fno_asynchronous_unwind_tables,
                   (TC.IsUnwindTablesDefault(Args) ||
                    TC.getSanitizerArgs().needsUnwindTables()) &&
                       !Freestanding);
  if (Args.hasFlag(options::OPT_funwind_tables, options::OPT_fno_unwind_tables,
                   AsynchronousUnwindTables))
    CmdArgs.push_back("-munwind-tables");

  TC.addClangTargetOptions(Args, CmdArgs, JA.getOffloadingDeviceKind());

  // FIXME: Handle -mtune=.
  (void)Args.hasArg(options::OPT_mtune_EQ);

  if (Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ)) {
    CmdArgs.push_back("-mcode-model");
    CmdArgs.push_back(A->getValue());
  }

  // Add the target cpu
  std::string CPU = getCPUName(Args, Triple, /*FromAs*/ false);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  RenderTargetOptions(Triple, Args, KernelOrKext, CmdArgs);

  // These two are potentially updated by AddClangCLArgs.
  codegenoptions::DebugInfoKind DebugInfoKind = codegenoptions::NoDebugInfo;
  bool EmitCodeView = false;

  // Add clang-cl arguments.
  types::ID InputType = Input.getType();
  if (D.IsCLMode())
    AddClangCLArgs(Args, InputType, CmdArgs, &DebugInfoKind, &EmitCodeView);

  DwarfFissionKind DwarfFission;
  RenderDebugOptions(TC, D, RawTriple, Args, EmitCodeView, IsWindowsMSVC,
                     CmdArgs, DebugInfoKind, DwarfFission);

  // Add the split debug info name to the command lines here so we
  // can propagate it to the backend.
  bool SplitDWARF = (DwarfFission != DwarfFissionKind::None) &&
                    (RawTriple.isOSLinux() || RawTriple.isOSFuchsia()) &&
                    (isa<AssembleJobAction>(JA) || isa<CompileJobAction>(JA) ||
                     isa<BackendJobAction>(JA));
  const char *SplitDWARFOut;
  if (SplitDWARF) {
    CmdArgs.push_back("-split-dwarf-file");
    SplitDWARFOut = SplitDebugName(Args, Output);
    CmdArgs.push_back(SplitDWARFOut);
  }

  // Pass the linker version in use.
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ)) {
    CmdArgs.push_back("-target-linker-version");
    CmdArgs.push_back(A->getValue());
  }

  if (!shouldUseLeafFramePointer(Args, RawTriple))
    CmdArgs.push_back("-momit-leaf-frame-pointer");

  // Explicitly error on some things we know we don't support and can't just
  // ignore.
  if (!Args.hasArg(options::OPT_fallow_unsupported)) {
    Arg *Unsupported;
    if (types::isCXX(InputType) && RawTriple.isOSDarwin() &&
        TC.getArch() == llvm::Triple::x86) {
      if ((Unsupported = Args.getLastArg(options::OPT_fapple_kext)) ||
          (Unsupported = Args.getLastArg(options::OPT_mkernel)))
        D.Diag(diag::err_drv_clang_unsupported_opt_cxx_darwin_i386)
            << Unsupported->getOption().getName();
    }
    // The faltivec option has been superseded by the maltivec option.
    if ((Unsupported = Args.getLastArg(options::OPT_faltivec)))
      D.Diag(diag::err_drv_clang_unsupported_opt_faltivec)
          << Unsupported->getOption().getName()
          << "please use -maltivec and include altivec.h explicitly";
    if ((Unsupported = Args.getLastArg(options::OPT_fno_altivec)))
      D.Diag(diag::err_drv_clang_unsupported_opt_faltivec)
          << Unsupported->getOption().getName() << "please use -mno-altivec";
  }

  Args.AddAllArgs(CmdArgs, options::OPT_v);
  Args.AddLastArg(CmdArgs, options::OPT_H);
  if (D.CCPrintHeaders && !D.CCGenDiagnostics) {
    CmdArgs.push_back("-header-include-file");
    CmdArgs.push_back(D.CCPrintHeadersFilename ? D.CCPrintHeadersFilename
                                               : "-");
  }
  Args.AddLastArg(CmdArgs, options::OPT_P);
  Args.AddLastArg(CmdArgs, options::OPT_print_ivar_layout);

  if (D.CCLogDiagnostics && !D.CCGenDiagnostics) {
    CmdArgs.push_back("-diagnostic-log-file");
    CmdArgs.push_back(D.CCLogDiagnosticsFilename ? D.CCLogDiagnosticsFilename
                                                 : "-");
  }

  bool UseSeparateSections = isUseSeparateSections(Triple);

  if (Args.hasFlag(options::OPT_ffunction_sections,
                   options::OPT_fno_function_sections, UseSeparateSections)) {
    CmdArgs.push_back("-ffunction-sections");
  }

  if (Args.hasFlag(options::OPT_fdata_sections, options::OPT_fno_data_sections,
                   UseSeparateSections)) {
    CmdArgs.push_back("-fdata-sections");
  }

  if (!Args.hasFlag(options::OPT_funique_section_names,
                    options::OPT_fno_unique_section_names, true))
    CmdArgs.push_back("-fno-unique-section-names");

  if (auto *A = Args.getLastArg(
      options::OPT_finstrument_functions,
      options::OPT_finstrument_functions_after_inlining,
      options::OPT_finstrument_function_entry_bare))
    A->render(Args, CmdArgs);

  // NVPTX doesn't support PGO or coverage. There's no runtime support for
  // sampling, overhead of call arc collection is way too high and there's no
  // way to collect the output.
  if (!Triple.isNVPTX())
    addPGOAndCoverageFlags(C, D, Output, Args, CmdArgs);

  if (auto *ABICompatArg = Args.getLastArg(options::OPT_fclang_abi_compat_EQ))
    ABICompatArg->render(Args, CmdArgs);

  // Add runtime flag for PS4 when PGO, coverage, or sanitizers are enabled.
  if (RawTriple.isPS4CPU() &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    PS4cpu::addProfileRTArgs(TC, Args, CmdArgs);
    PS4cpu::addSanitizerArgs(TC, CmdArgs);
  }

  // Pass options for controlling the default header search paths.
  if (Args.hasArg(options::OPT_nostdinc)) {
    CmdArgs.push_back("-nostdsysteminc");
    CmdArgs.push_back("-nobuiltininc");
  } else {
    if (Args.hasArg(options::OPT_nostdlibinc))
      CmdArgs.push_back("-nostdsysteminc");
    Args.AddLastArg(CmdArgs, options::OPT_nostdincxx);
    Args.AddLastArg(CmdArgs, options::OPT_nobuiltininc);
  }

  // Pass the path to compiler resource files.
  CmdArgs.push_back("-resource-dir");
  CmdArgs.push_back(D.ResourceDir.c_str());

  Args.AddLastArg(CmdArgs, options::OPT_working_directory);

  RenderARCMigrateToolOptions(D, Args, CmdArgs);

  // Add preprocessing options like -I, -D, etc. if we are using the
  // preprocessor.
  //
  // FIXME: Support -fpreprocessed
  if (types::getPreprocessedType(InputType) != types::TY_INVALID)
    AddPreprocessingOptions(C, JA, D, Args, CmdArgs, Output, Inputs);

  // Don't warn about "clang -c -DPIC -fPIC test.i" because libtool.m4 assumes
  // that "The compiler can only warn and ignore the option if not recognized".
  // When building with ccache, it will pass -D options to clang even on
  // preprocessed inputs and configure concludes that -fPIC is not supported.
  Args.ClaimAllArgs(options::OPT_D);

  // Manually translate -O4 to -O3; let clang reject others.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4)) {
      CmdArgs.push_back("-O3");
      D.Diag(diag::warn_O4_is_O3);
    } else {
      A->render(Args, CmdArgs);
    }
  }

  // Warn about ignored options to clang.
  for (const Arg *A :
       Args.filtered(options::OPT_clang_ignored_gcc_optimization_f_Group)) {
    D.Diag(diag::warn_ignored_gcc_optimization) << A->getAsString(Args);
    A->claim();
  }

  for (const Arg *A :
       Args.filtered(options::OPT_clang_ignored_legacy_options_Group)) {
    D.Diag(diag::warn_ignored_clang_option) << A->getAsString(Args);
    A->claim();
  }

  claimNoWarnArgs(Args);

  Args.AddAllArgs(CmdArgs, options::OPT_R_Group);

  Args.AddAllArgs(CmdArgs, options::OPT_W_Group);
  if (Args.hasFlag(options::OPT_pedantic, options::OPT_no_pedantic, false))
    CmdArgs.push_back("-pedantic");
  Args.AddLastArg(CmdArgs, options::OPT_pedantic_errors);
  Args.AddLastArg(CmdArgs, options::OPT_w);

  // Fixed point flags
  if (Args.hasFlag(options::OPT_ffixed_point, options::OPT_fno_fixed_point,
                   /*Default=*/false))
    Args.AddLastArg(CmdArgs, options::OPT_ffixed_point);

  // Handle -{std, ansi, trigraphs} -- take the last of -{std, ansi}
  // (-ansi is equivalent to -std=c89 or -std=c++98).
  //
  // If a std is supplied, only add -trigraphs if it follows the
  // option.
  bool ImplyVCPPCXXVer = false;
  if (Arg *Std = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi)) {
    if (Std->getOption().matches(options::OPT_ansi))
      if (types::isCXX(InputType))
        CmdArgs.push_back("-std=c++98");
      else
        CmdArgs.push_back("-std=c89");
    else
      Std->render(Args, CmdArgs);

    // If -f(no-)trigraphs appears after the language standard flag, honor it.
    if (Arg *A = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi,
                                 options::OPT_ftrigraphs,
                                 options::OPT_fno_trigraphs))
      if (A != Std)
        A->render(Args, CmdArgs);
  } else {
    // Honor -std-default.
    //
    // FIXME: Clang doesn't correctly handle -std= when the input language
    // doesn't match. For the time being just ignore this for C++ inputs;
    // eventually we want to do all the standard defaulting here instead of
    // splitting it between the driver and clang -cc1.
    if (!types::isCXX(InputType))
      Args.AddAllArgsTranslated(CmdArgs, options::OPT_std_default_EQ, "-std=",
                                /*Joined=*/true);
    else if (IsWindowsMSVC)
      ImplyVCPPCXXVer = true;

    Args.AddLastArg(CmdArgs, options::OPT_ftrigraphs,
                    options::OPT_fno_trigraphs);
  }

  // GCC's behavior for -Wwrite-strings is a bit strange:
  //  * In C, this "warning flag" changes the types of string literals from
  //    'char[N]' to 'const char[N]', and thus triggers an unrelated warning
  //    for the discarded qualifier.
  //  * In C++, this is just a normal warning flag.
  //
  // Implementing this warning correctly in C is hard, so we follow GCC's
  // behavior for now. FIXME: Directly diagnose uses of a string literal as
  // a non-const char* in C, rather than using this crude hack.
  if (!types::isCXX(InputType)) {
    // FIXME: This should behave just like a warning flag, and thus should also
    // respect -Weverything, -Wno-everything, -Werror=write-strings, and so on.
    Arg *WriteStrings =
        Args.getLastArg(options::OPT_Wwrite_strings,
                        options::OPT_Wno_write_strings, options::OPT_w);
    if (WriteStrings &&
        WriteStrings->getOption().matches(options::OPT_Wwrite_strings))
      CmdArgs.push_back("-fconst-strings");
  }

  // GCC provides a macro definition '__DEPRECATED' when -Wdeprecated is active
  // during C++ compilation, which it is by default. GCC keeps this define even
  // in the presence of '-w', match this behavior bug-for-bug.
  if (types::isCXX(InputType) &&
      Args.hasFlag(options::OPT_Wdeprecated, options::OPT_Wno_deprecated,
                   true)) {
    CmdArgs.push_back("-fdeprecated-macro");
  }

  // Translate GCC's misnamer '-fasm' arguments to '-fgnu-keywords'.
  if (Arg *Asm = Args.getLastArg(options::OPT_fasm, options::OPT_fno_asm)) {
    if (Asm->getOption().matches(options::OPT_fasm))
      CmdArgs.push_back("-fgnu-keywords");
    else
      CmdArgs.push_back("-fno-gnu-keywords");
  }

  if (ShouldDisableDwarfDirectory(Args, TC))
    CmdArgs.push_back("-fno-dwarf-directory-asm");

  if (ShouldDisableAutolink(Args, TC))
    CmdArgs.push_back("-fno-autolink");

  // Add in -fdebug-compilation-dir if necessary.
  addDebugCompDirArg(Args, CmdArgs);

  addDebugPrefixMapArg(D, Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_ftemplate_depth_,
                               options::OPT_ftemplate_depth_EQ)) {
    CmdArgs.push_back("-ftemplate-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_foperator_arrow_depth_EQ)) {
    CmdArgs.push_back("-foperator-arrow-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fconstexpr_depth_EQ)) {
    CmdArgs.push_back("-fconstexpr-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fconstexpr_steps_EQ)) {
    CmdArgs.push_back("-fconstexpr-steps");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fbracket_depth_EQ)) {
    CmdArgs.push_back("-fbracket-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_Wlarge_by_value_copy_EQ,
                               options::OPT_Wlarge_by_value_copy_def)) {
    if (A->getNumValues()) {
      StringRef bytes = A->getValue();
      CmdArgs.push_back(Args.MakeArgString("-Wlarge-by-value-copy=" + bytes));
    } else
      CmdArgs.push_back("-Wlarge-by-value-copy=64"); // default value
  }

  if (Args.hasArg(options::OPT_relocatable_pch))
    CmdArgs.push_back("-relocatable-pch");

  if (const Arg *A = Args.getLastArg(options::OPT_fcf_runtime_abi_EQ)) {
    static const char *kCFABIs[] = {
      "standalone", "objc", "swift", "swift-5.0", "swift-4.2", "swift-4.1",
    };

    if (find(kCFABIs, StringRef(A->getValue())) == std::end(kCFABIs))
      D.Diag(diag::err_drv_invalid_cf_runtime_abi) << A->getValue();
    else
      A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_fconstant_string_class_EQ)) {
    CmdArgs.push_back("-fconstant-string-class");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftabstop_EQ)) {
    CmdArgs.push_back("-ftabstop");
    CmdArgs.push_back(A->getValue());
  }

  if (Args.hasFlag(options::OPT_fstack_size_section,
                   options::OPT_fno_stack_size_section, RawTriple.isPS4()))
    CmdArgs.push_back("-fstack-size-section");

  CmdArgs.push_back("-ferror-limit");
  if (Arg *A = Args.getLastArg(options::OPT_ferror_limit_EQ))
    CmdArgs.push_back(A->getValue());
  else
    CmdArgs.push_back("19");

  if (Arg *A = Args.getLastArg(options::OPT_fmacro_backtrace_limit_EQ)) {
    CmdArgs.push_back("-fmacro-backtrace-limit");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftemplate_backtrace_limit_EQ)) {
    CmdArgs.push_back("-ftemplate-backtrace-limit");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fconstexpr_backtrace_limit_EQ)) {
    CmdArgs.push_back("-fconstexpr-backtrace-limit");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_fspell_checking_limit_EQ)) {
    CmdArgs.push_back("-fspell-checking-limit");
    CmdArgs.push_back(A->getValue());
  }

  // Pass -fmessage-length=.
  CmdArgs.push_back("-fmessage-length");
  if (Arg *A = Args.getLastArg(options::OPT_fmessage_length_EQ)) {
    CmdArgs.push_back(A->getValue());
  } else {
    // If -fmessage-length=N was not specified, determine whether this is a
    // terminal and, if so, implicitly define -fmessage-length appropriately.
    unsigned N = llvm::sys::Process::StandardErrColumns();
    CmdArgs.push_back(Args.MakeArgString(Twine(N)));
  }

  // -fvisibility= and -fvisibility-ms-compat are of a piece.
  if (const Arg *A = Args.getLastArg(options::OPT_fvisibility_EQ,
                                     options::OPT_fvisibility_ms_compat)) {
    if (A->getOption().matches(options::OPT_fvisibility_EQ)) {
      CmdArgs.push_back("-fvisibility");
      CmdArgs.push_back(A->getValue());
    } else {
      assert(A->getOption().matches(options::OPT_fvisibility_ms_compat));
      CmdArgs.push_back("-fvisibility");
      CmdArgs.push_back("hidden");
      CmdArgs.push_back("-ftype-visibility");
      CmdArgs.push_back("default");
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_fvisibility_inlines_hidden);
  Args.AddLastArg(CmdArgs, options::OPT_fvisibility_global_new_delete_hidden);

  Args.AddLastArg(CmdArgs, options::OPT_ftlsmodel_EQ);

  // Forward -f (flag) options which we can pass directly.
  Args.AddLastArg(CmdArgs, options::OPT_femit_all_decls);
  Args.AddLastArg(CmdArgs, options::OPT_fheinous_gnu_extensions);
  Args.AddLastArg(CmdArgs, options::OPT_fdigraphs, options::OPT_fno_digraphs);
  Args.AddLastArg(CmdArgs, options::OPT_fno_operator_names);
  Args.AddLastArg(CmdArgs, options::OPT_femulated_tls,
                  options::OPT_fno_emulated_tls);
  Args.AddLastArg(CmdArgs, options::OPT_fkeep_static_consts);

  // AltiVec-like language extensions aren't relevant for assembling.
  if (!isa<PreprocessJobAction>(JA) || Output.getType() != types::TY_PP_Asm)
    Args.AddLastArg(CmdArgs, options::OPT_fzvector);

  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_show_template_tree);
  Args.AddLastArg(CmdArgs, options::OPT_fno_elide_type);

  // Forward flags for OpenMP. We don't do this if the current action is an
  // device offloading action other than OpenMP.
  if (Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                   options::OPT_fno_openmp, false) &&
      (JA.isDeviceOffloading(Action::OFK_None) ||
       JA.isDeviceOffloading(Action::OFK_OpenMP))) {
    switch (D.getOpenMPRuntime(Args)) {
    case Driver::OMPRT_OMP:
    case Driver::OMPRT_IOMP5:
      // Clang can generate useful OpenMP code for these two runtime libraries.
      CmdArgs.push_back("-fopenmp");

      // If no option regarding the use of TLS in OpenMP codegeneration is
      // given, decide a default based on the target. Otherwise rely on the
      // options and pass the right information to the frontend.
      if (!Args.hasFlag(options::OPT_fopenmp_use_tls,
                        options::OPT_fnoopenmp_use_tls, /*Default=*/true))
        CmdArgs.push_back("-fnoopenmp-use-tls");
      Args.AddLastArg(CmdArgs, options::OPT_fopenmp_simd,
                      options::OPT_fno_openmp_simd);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_version_EQ);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_cuda_number_of_sm_EQ);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_cuda_blocks_per_sm_EQ);
      if (Args.hasFlag(options::OPT_fopenmp_optimistic_collapse,
                       options::OPT_fno_openmp_optimistic_collapse,
                       /*Default=*/false))
        CmdArgs.push_back("-fopenmp-optimistic-collapse");

      // When in OpenMP offloading mode with NVPTX target, forward
      // cuda-mode flag
      if (Args.hasFlag(options::OPT_fopenmp_cuda_mode,
                       options::OPT_fno_openmp_cuda_mode, /*Default=*/false))
        CmdArgs.push_back("-fopenmp-cuda-mode");

      // When in OpenMP offloading mode with NVPTX target, check if full runtime
      // is required.
      if (Args.hasFlag(options::OPT_fopenmp_cuda_force_full_runtime,
                       options::OPT_fno_openmp_cuda_force_full_runtime,
                       /*Default=*/false))
        CmdArgs.push_back("-fopenmp-cuda-force-full-runtime");
      break;
    default:
      // By default, if Clang doesn't know how to generate useful OpenMP code
      // for a specific runtime library, we just don't pass the '-fopenmp' flag
      // down to the actual compilation.
      // FIXME: It would be better to have a mode which *only* omits IR
      // generation based on the OpenMP support so that we get consistent
      // semantic analysis, etc.
      break;
    }
  } else {
    Args.AddLastArg(CmdArgs, options::OPT_fopenmp_simd,
                    options::OPT_fno_openmp_simd);
    Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_version_EQ);
  }

  const SanitizerArgs &Sanitize = TC.getSanitizerArgs();
  Sanitize.addArgs(TC, Args, CmdArgs, InputType);

  const XRayArgs &XRay = TC.getXRayArgs();
  XRay.addArgs(TC, Args, CmdArgs, InputType);

  if (TC.SupportsProfiling())
    Args.AddLastArg(CmdArgs, options::OPT_pg);

  if (TC.SupportsProfiling())
    Args.AddLastArg(CmdArgs, options::OPT_mfentry);

  // -flax-vector-conversions is default.
  if (!Args.hasFlag(options::OPT_flax_vector_conversions,
                    options::OPT_fno_lax_vector_conversions))
    CmdArgs.push_back("-fno-lax-vector-conversions");

  if (Args.getLastArg(options::OPT_fapple_kext) ||
      (Args.hasArg(options::OPT_mkernel) && types::isCXX(InputType)))
    CmdArgs.push_back("-fapple-kext");

  Args.AddLastArg(CmdArgs, options::OPT_fobjc_sender_dependent_dispatch);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_print_source_range_info);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_parseable_fixits);
  Args.AddLastArg(CmdArgs, options::OPT_ftime_report);
  Args.AddLastArg(CmdArgs, options::OPT_ftrapv);

  if (Arg *A = Args.getLastArg(options::OPT_ftrapv_handler_EQ)) {
    CmdArgs.push_back("-ftrapv-handler");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddLastArg(CmdArgs, options::OPT_ftrap_function_EQ);

  // -fno-strict-overflow implies -fwrapv if it isn't disabled, but
  // -fstrict-overflow won't turn off an explicitly enabled -fwrapv.
  if (Arg *A = Args.getLastArg(options::OPT_fwrapv, options::OPT_fno_wrapv)) {
    if (A->getOption().matches(options::OPT_fwrapv))
      CmdArgs.push_back("-fwrapv");
  } else if (Arg *A = Args.getLastArg(options::OPT_fstrict_overflow,
                                      options::OPT_fno_strict_overflow)) {
    if (A->getOption().matches(options::OPT_fno_strict_overflow))
      CmdArgs.push_back("-fwrapv");
  }

  if (Arg *A = Args.getLastArg(options::OPT_freroll_loops,
                               options::OPT_fno_reroll_loops))
    if (A->getOption().matches(options::OPT_freroll_loops))
      CmdArgs.push_back("-freroll-loops");

  Args.AddLastArg(CmdArgs, options::OPT_fwritable_strings);
  Args.AddLastArg(CmdArgs, options::OPT_funroll_loops,
                  options::OPT_fno_unroll_loops);

  Args.AddLastArg(CmdArgs, options::OPT_pthread);

  if (Args.hasFlag(options::OPT_mspeculative_load_hardening, options::OPT_mno_speculative_load_hardening,
                   false))
    CmdArgs.push_back(Args.MakeArgString("-mspeculative-load-hardening"));

  RenderSSPOptions(TC, Args, CmdArgs, KernelOrKext);
  RenderTrivialAutoVarInitOptions(D, TC, Args, CmdArgs);

  // Translate -mstackrealign
  if (Args.hasFlag(options::OPT_mstackrealign, options::OPT_mno_stackrealign,
                   false))
    CmdArgs.push_back(Args.MakeArgString("-mstackrealign"));

  if (Args.hasArg(options::OPT_mstack_alignment)) {
    StringRef alignment = Args.getLastArgValue(options::OPT_mstack_alignment);
    CmdArgs.push_back(Args.MakeArgString("-mstack-alignment=" + alignment));
  }

  if (Args.hasArg(options::OPT_mstack_probe_size)) {
    StringRef Size = Args.getLastArgValue(options::OPT_mstack_probe_size);

    if (!Size.empty())
      CmdArgs.push_back(Args.MakeArgString("-mstack-probe-size=" + Size));
    else
      CmdArgs.push_back("-mstack-probe-size=0");
  }

  if (!Args.hasFlag(options::OPT_mstack_arg_probe,
                    options::OPT_mno_stack_arg_probe, true))
    CmdArgs.push_back(Args.MakeArgString("-mno-stack-arg-probe"));

  if (Arg *A = Args.getLastArg(options::OPT_mrestrict_it,
                               options::OPT_mno_restrict_it)) {
    if (A->getOption().matches(options::OPT_mrestrict_it)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-arm-restrict-it");
    } else {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-arm-no-restrict-it");
    }
  } else if (Triple.isOSWindows() &&
             (Triple.getArch() == llvm::Triple::arm ||
              Triple.getArch() == llvm::Triple::thumb)) {
    // Windows on ARM expects restricted IT blocks
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-arm-restrict-it");
  }

  // Forward -cl options to -cc1
  RenderOpenCLOptions(Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_fcf_protection_EQ)) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fcf-protection=") + A->getValue()));
  }

  // Forward -f options with positive and negative forms; we translate
  // these by hand.
  if (Arg *A = getLastProfileSampleUseArg(Args)) {
    StringRef fname = A->getValue();
    if (!llvm::sys::fs::exists(fname))
      D.Diag(diag::err_drv_no_such_file) << fname;
    else
      A->render(Args, CmdArgs);
  }
  Args.AddLastArg(CmdArgs, options::OPT_fprofile_remapping_file_EQ);

  RenderBuiltinOptions(TC, RawTriple, Args, CmdArgs);

  if (!Args.hasFlag(options::OPT_fassume_sane_operator_new,
                    options::OPT_fno_assume_sane_operator_new))
    CmdArgs.push_back("-fno-assume-sane-operator-new");

  // -fblocks=0 is default.
  if (Args.hasFlag(options::OPT_fblocks, options::OPT_fno_blocks,
                   TC.IsBlocksDefault()) ||
      (Args.hasArg(options::OPT_fgnu_runtime) &&
       Args.hasArg(options::OPT_fobjc_nonfragile_abi) &&
       !Args.hasArg(options::OPT_fno_blocks))) {
    CmdArgs.push_back("-fblocks");

    if (!Args.hasArg(options::OPT_fgnu_runtime) && !TC.hasBlocksRuntime())
      CmdArgs.push_back("-fblocks-runtime-optional");
  }

  // -fencode-extended-block-signature=1 is default.
  if (TC.IsEncodeExtendedBlockSignatureDefault())
    CmdArgs.push_back("-fencode-extended-block-signature");

  if (Args.hasFlag(options::OPT_fcoroutines_ts, options::OPT_fno_coroutines_ts,
                   false) &&
      types::isCXX(InputType)) {
    CmdArgs.push_back("-fcoroutines-ts");
  }

  Args.AddLastArg(CmdArgs, options::OPT_fdouble_square_bracket_attributes,
                  options::OPT_fno_double_square_bracket_attributes);

  bool HaveModules = false;
  RenderModulesOptions(C, D, Args, Input, Output, CmdArgs, HaveModules);

  // -faccess-control is default.
  if (Args.hasFlag(options::OPT_fno_access_control,
                   options::OPT_faccess_control, false))
    CmdArgs.push_back("-fno-access-control");

  // -felide-constructors is the default.
  if (Args.hasFlag(options::OPT_fno_elide_constructors,
                   options::OPT_felide_constructors, false))
    CmdArgs.push_back("-fno-elide-constructors");

  ToolChain::RTTIMode RTTIMode = TC.getRTTIMode();

  if (KernelOrKext || (types::isCXX(InputType) &&
                       (RTTIMode == ToolChain::RM_Disabled)))
    CmdArgs.push_back("-fno-rtti");

  // -fshort-enums=0 is default for all architectures except Hexagon.
  if (Args.hasFlag(options::OPT_fshort_enums, options::OPT_fno_short_enums,
                   TC.getArch() == llvm::Triple::hexagon))
    CmdArgs.push_back("-fshort-enums");

  RenderCharacterOptions(Args, AuxTriple ? *AuxTriple : RawTriple, CmdArgs);

  // -fuse-cxa-atexit is default.
  if (!Args.hasFlag(
          options::OPT_fuse_cxa_atexit, options::OPT_fno_use_cxa_atexit,
          !RawTriple.isOSWindows() &&
              RawTriple.getOS() != llvm::Triple::Solaris &&
              TC.getArch() != llvm::Triple::xcore &&
              ((RawTriple.getVendor() != llvm::Triple::MipsTechnologies) ||
               RawTriple.hasEnvironment())) ||
      KernelOrKext)
    CmdArgs.push_back("-fno-use-cxa-atexit");

  if (Args.hasFlag(options::OPT_fregister_global_dtors_with_atexit,
                   options::OPT_fno_register_global_dtors_with_atexit,
                   RawTriple.isOSDarwin() && !KernelOrKext))
    CmdArgs.push_back("-fregister-global-dtors-with-atexit");

  // -fms-extensions=0 is default.
  if (Args.hasFlag(options::OPT_fms_extensions, options::OPT_fno_ms_extensions,
                   IsWindowsMSVC))
    CmdArgs.push_back("-fms-extensions");

  // -fno-use-line-directives is default.
  if (Args.hasFlag(options::OPT_fuse_line_directives,
                   options::OPT_fno_use_line_directives, false))
    CmdArgs.push_back("-fuse-line-directives");

  // -fms-compatibility=0 is default.
  if (Args.hasFlag(options::OPT_fms_compatibility,
                   options::OPT_fno_ms_compatibility,
                   (IsWindowsMSVC &&
                    Args.hasFlag(options::OPT_fms_extensions,
                                 options::OPT_fno_ms_extensions, true))))
    CmdArgs.push_back("-fms-compatibility");

  VersionTuple MSVT = TC.computeMSVCVersion(&D, Args);
  if (!MSVT.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fms-compatibility-version=" + MSVT.getAsString()));

  bool IsMSVC2015Compatible = MSVT.getMajor() >= 19;
  if (ImplyVCPPCXXVer) {
    StringRef LanguageStandard;
    if (const Arg *StdArg = Args.getLastArg(options::OPT__SLASH_std)) {
      LanguageStandard = llvm::StringSwitch<StringRef>(StdArg->getValue())
                             .Case("c++14", "-std=c++14")
                             .Case("c++17", "-std=c++17")
                             .Case("c++latest", "-std=c++2a")
                             .Default("");
      if (LanguageStandard.empty())
        D.Diag(clang::diag::warn_drv_unused_argument)
            << StdArg->getAsString(Args);
    }

    if (LanguageStandard.empty()) {
      if (IsMSVC2015Compatible)
        LanguageStandard = "-std=c++14";
      else
        LanguageStandard = "-std=c++11";
    }

    CmdArgs.push_back(LanguageStandard.data());
  }

  // -fno-borland-extensions is default.
  if (Args.hasFlag(options::OPT_fborland_extensions,
                   options::OPT_fno_borland_extensions, false))
    CmdArgs.push_back("-fborland-extensions");

  // -fno-declspec is default, except for PS4.
  if (Args.hasFlag(options::OPT_fdeclspec, options::OPT_fno_declspec,
                   RawTriple.isPS4()))
    CmdArgs.push_back("-fdeclspec");
  else if (Args.hasArg(options::OPT_fno_declspec))
    CmdArgs.push_back("-fno-declspec"); // Explicitly disabling __declspec.

  // -fthreadsafe-static is default, except for MSVC compatibility versions less
  // than 19.
  if (!Args.hasFlag(options::OPT_fthreadsafe_statics,
                    options::OPT_fno_threadsafe_statics,
                    !IsWindowsMSVC || IsMSVC2015Compatible))
    CmdArgs.push_back("-fno-threadsafe-statics");

  // -fno-delayed-template-parsing is default, except when targeting MSVC.
  // Many old Windows SDK versions require this to parse.
  // FIXME: MSVC introduced /Zc:twoPhase- to disable this behavior in their
  // compiler. We should be able to disable this by default at some point.
  if (Args.hasFlag(options::OPT_fdelayed_template_parsing,
                   options::OPT_fno_delayed_template_parsing, IsWindowsMSVC))
    CmdArgs.push_back("-fdelayed-template-parsing");

  // -fgnu-keywords default varies depending on language; only pass if
  // specified.
  if (Arg *A = Args.getLastArg(options::OPT_fgnu_keywords,
                               options::OPT_fno_gnu_keywords))
    A->render(Args, CmdArgs);

  if (Args.hasFlag(options::OPT_fgnu89_inline, options::OPT_fno_gnu89_inline,
                   false))
    CmdArgs.push_back("-fgnu89-inline");

  if (Args.hasArg(options::OPT_fno_inline))
    CmdArgs.push_back("-fno-inline");

  if (Arg* InlineArg = Args.getLastArg(options::OPT_finline_functions,
                                       options::OPT_finline_hint_functions,
                                       options::OPT_fno_inline_functions))
    InlineArg->render(Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_new_pass_manager,
                  options::OPT_fno_experimental_new_pass_manager);

  ObjCRuntime Runtime = AddObjCRuntimeArgs(Args, CmdArgs, rewriteKind);
  RenderObjCOptions(TC, D, RawTriple, Args, Runtime, rewriteKind != RK_None,
                    Input, CmdArgs);

  if (Args.hasFlag(options::OPT_fapplication_extension,
                   options::OPT_fno_application_extension, false))
    CmdArgs.push_back("-fapplication-extension");

  // Handle GCC-style exception args.
  if (!C.getDriver().IsCLMode())
    addExceptionArgs(Args, InputType, TC, KernelOrKext, Runtime, CmdArgs);

  // Handle exception personalities
  Arg *A = Args.getLastArg(options::OPT_fsjlj_exceptions,
                           options::OPT_fseh_exceptions,
                           options::OPT_fdwarf_exceptions);
  if (A) {
    const Option &Opt = A->getOption();
    if (Opt.matches(options::OPT_fsjlj_exceptions))
      CmdArgs.push_back("-fsjlj-exceptions");
    if (Opt.matches(options::OPT_fseh_exceptions))
      CmdArgs.push_back("-fseh-exceptions");
    if (Opt.matches(options::OPT_fdwarf_exceptions))
      CmdArgs.push_back("-fdwarf-exceptions");
  } else {
    switch (TC.GetExceptionModel(Args)) {
    default:
      break;
    case llvm::ExceptionHandling::DwarfCFI:
      CmdArgs.push_back("-fdwarf-exceptions");
      break;
    case llvm::ExceptionHandling::SjLj:
      CmdArgs.push_back("-fsjlj-exceptions");
      break;
    case llvm::ExceptionHandling::WinEH:
      CmdArgs.push_back("-fseh-exceptions");
      break;
    }
  }

  // C++ "sane" operator new.
  if (!Args.hasFlag(options::OPT_fassume_sane_operator_new,
                    options::OPT_fno_assume_sane_operator_new))
    CmdArgs.push_back("-fno-assume-sane-operator-new");

  // -frelaxed-template-template-args is off by default, as it is a severe
  // breaking change until a corresponding change to template partial ordering
  // is provided.
  if (Args.hasFlag(options::OPT_frelaxed_template_template_args,
                   options::OPT_fno_relaxed_template_template_args, false))
    CmdArgs.push_back("-frelaxed-template-template-args");

  // -fsized-deallocation is off by default, as it is an ABI-breaking change for
  // most platforms.
  if (Args.hasFlag(options::OPT_fsized_deallocation,
                   options::OPT_fno_sized_deallocation, false))
    CmdArgs.push_back("-fsized-deallocation");

  // -faligned-allocation is on by default in C++17 onwards and otherwise off
  // by default.
  if (Arg *A = Args.getLastArg(options::OPT_faligned_allocation,
                               options::OPT_fno_aligned_allocation,
                               options::OPT_faligned_new_EQ)) {
    if (A->getOption().matches(options::OPT_fno_aligned_allocation))
      CmdArgs.push_back("-fno-aligned-allocation");
    else
      CmdArgs.push_back("-faligned-allocation");
  }

  // The default new alignment can be specified using a dedicated option or via
  // a GCC-compatible option that also turns on aligned allocation.
  if (Arg *A = Args.getLastArg(options::OPT_fnew_alignment_EQ,
                               options::OPT_faligned_new_EQ))
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fnew-alignment=") + A->getValue()));

  // -fconstant-cfstrings is default, and may be subject to argument translation
  // on Darwin.
  if (!Args.hasFlag(options::OPT_fconstant_cfstrings,
                    options::OPT_fno_constant_cfstrings) ||
      !Args.hasFlag(options::OPT_mconstant_cfstrings,
                    options::OPT_mno_constant_cfstrings))
    CmdArgs.push_back("-fno-constant-cfstrings");

  // -fno-pascal-strings is default, only pass non-default.
  if (Args.hasFlag(options::OPT_fpascal_strings,
                   options::OPT_fno_pascal_strings, false))
    CmdArgs.push_back("-fpascal-strings");

  // Honor -fpack-struct= and -fpack-struct, if given. Note that
  // -fno-pack-struct doesn't apply to -fpack-struct=.
  if (Arg *A = Args.getLastArg(options::OPT_fpack_struct_EQ)) {
    std::string PackStructStr = "-fpack-struct=";
    PackStructStr += A->getValue();
    CmdArgs.push_back(Args.MakeArgString(PackStructStr));
  } else if (Args.hasFlag(options::OPT_fpack_struct,
                          options::OPT_fno_pack_struct, false)) {
    CmdArgs.push_back("-fpack-struct=1");
  }

  // Handle -fmax-type-align=N and -fno-type-align
  bool SkipMaxTypeAlign = Args.hasArg(options::OPT_fno_max_type_align);
  if (Arg *A = Args.getLastArg(options::OPT_fmax_type_align_EQ)) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=";
      MaxTypeAlignStr += A->getValue();
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  } else if (RawTriple.isOSDarwin()) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=16";
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  }

  if (!Args.hasFlag(options::OPT_Qy, options::OPT_Qn, true))
    CmdArgs.push_back("-Qn");

  // -fcommon is the default unless compiling kernel code or the target says so
  bool NoCommonDefault = KernelOrKext || isNoCommonDefault(RawTriple);
  if (!Args.hasFlag(options::OPT_fcommon, options::OPT_fno_common,
                    !NoCommonDefault))
    CmdArgs.push_back("-fno-common");

  // -fsigned-bitfields is default, and clang doesn't yet support
  // -funsigned-bitfields.
  if (!Args.hasFlag(options::OPT_fsigned_bitfields,
                    options::OPT_funsigned_bitfields))
    D.Diag(diag::warn_drv_clang_unsupported)
        << Args.getLastArg(options::OPT_funsigned_bitfields)->getAsString(Args);

  // -fsigned-bitfields is default, and clang doesn't support -fno-for-scope.
  if (!Args.hasFlag(options::OPT_ffor_scope, options::OPT_fno_for_scope))
    D.Diag(diag::err_drv_clang_unsupported)
        << Args.getLastArg(options::OPT_fno_for_scope)->getAsString(Args);

  // -finput_charset=UTF-8 is default. Reject others
  if (Arg *inputCharset = Args.getLastArg(options::OPT_finput_charset_EQ)) {
    StringRef value = inputCharset->getValue();
    if (!value.equals_lower("utf-8"))
      D.Diag(diag::err_drv_invalid_value) << inputCharset->getAsString(Args)
                                          << value;
  }

  // -fexec_charset=UTF-8 is default. Reject others
  if (Arg *execCharset = Args.getLastArg(options::OPT_fexec_charset_EQ)) {
    StringRef value = execCharset->getValue();
    if (!value.equals_lower("utf-8"))
      D.Diag(diag::err_drv_invalid_value) << execCharset->getAsString(Args)
                                          << value;
  }

  RenderDiagnosticsOptions(D, Args, CmdArgs);

  // -fno-asm-blocks is default.
  if (Args.hasFlag(options::OPT_fasm_blocks, options::OPT_fno_asm_blocks,
                   false))
    CmdArgs.push_back("-fasm-blocks");

  // -fgnu-inline-asm is default.
  if (!Args.hasFlag(options::OPT_fgnu_inline_asm,
                    options::OPT_fno_gnu_inline_asm, true))
    CmdArgs.push_back("-fno-gnu-inline-asm");

  // Enable vectorization per default according to the optimization level
  // selected. For optimization levels that want vectorization we use the alias
  // option to simplify the hasFlag logic.
  bool EnableVec = shouldEnableVectorizerAtOLevel(Args, false);
  OptSpecifier VectorizeAliasOption =
      EnableVec ? options::OPT_O_Group : options::OPT_fvectorize;
  if (Args.hasFlag(options::OPT_fvectorize, VectorizeAliasOption,
                   options::OPT_fno_vectorize, EnableVec))
    CmdArgs.push_back("-vectorize-loops");

  // -fslp-vectorize is enabled based on the optimization level selected.
  bool EnableSLPVec = shouldEnableVectorizerAtOLevel(Args, true);
  OptSpecifier SLPVectAliasOption =
      EnableSLPVec ? options::OPT_O_Group : options::OPT_fslp_vectorize;
  if (Args.hasFlag(options::OPT_fslp_vectorize, SLPVectAliasOption,
                   options::OPT_fno_slp_vectorize, EnableSLPVec))
    CmdArgs.push_back("-vectorize-slp");

  ParseMPreferVectorWidth(D, Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_fshow_overloads_EQ))
    A->render(Args, CmdArgs);

  if (Arg *A = Args.getLastArg(
          options::OPT_fsanitize_undefined_strip_path_components_EQ))
    A->render(Args, CmdArgs);

  // -fdollars-in-identifiers default varies depending on platform and
  // language; only pass if specified.
  if (Arg *A = Args.getLastArg(options::OPT_fdollars_in_identifiers,
                               options::OPT_fno_dollars_in_identifiers)) {
    if (A->getOption().matches(options::OPT_fdollars_in_identifiers))
      CmdArgs.push_back("-fdollars-in-identifiers");
    else
      CmdArgs.push_back("-fno-dollars-in-identifiers");
  }

  // -funit-at-a-time is default, and we don't support -fno-unit-at-a-time for
  // practical purposes.
  if (Arg *A = Args.getLastArg(options::OPT_funit_at_a_time,
                               options::OPT_fno_unit_at_a_time)) {
    if (A->getOption().matches(options::OPT_fno_unit_at_a_time))
      D.Diag(diag::warn_drv_clang_unsupported) << A->getAsString(Args);
  }

  if (Args.hasFlag(options::OPT_fapple_pragma_pack,
                   options::OPT_fno_apple_pragma_pack, false))
    CmdArgs.push_back("-fapple-pragma-pack");

  if (Args.hasFlag(options::OPT_fsave_optimization_record,
                   options::OPT_foptimization_record_file_EQ,
                   options::OPT_fno_save_optimization_record, false)) {
    CmdArgs.push_back("-opt-record-file");

    const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
    if (A) {
      CmdArgs.push_back(A->getValue());
    } else {
      SmallString<128> F;

      if (Args.hasArg(options::OPT_c) || Args.hasArg(options::OPT_S)) {
        if (Arg *FinalOutput = Args.getLastArg(options::OPT_o))
          F = FinalOutput->getValue();
      }

      if (F.empty()) {
        // Use the input filename.
        F = llvm::sys::path::stem(Input.getBaseInput());

        // If we're compiling for an offload architecture (i.e. a CUDA device),
        // we need to make the file name for the device compilation different
        // from the host compilation.
        if (!JA.isDeviceOffloading(Action::OFK_None) &&
            !JA.isDeviceOffloading(Action::OFK_Host)) {
          llvm::sys::path::replace_extension(F, "");
          F += Action::GetOffloadingFileNamePrefix(JA.getOffloadingDeviceKind(),
                                                   Triple.normalize());
          F += "-";
          F += JA.getOffloadingArch();
        }
      }

      llvm::sys::path::replace_extension(F, "opt.yaml");
      CmdArgs.push_back(Args.MakeArgString(F));
    }
  }

  bool RewriteImports = Args.hasFlag(options::OPT_frewrite_imports,
                                     options::OPT_fno_rewrite_imports, false);
  if (RewriteImports)
    CmdArgs.push_back("-frewrite-imports");

  // Enable rewrite includes if the user's asked for it or if we're generating
  // diagnostics.
  // TODO: Once -module-dependency-dir works with -frewrite-includes it'd be
  // nice to enable this when doing a crashdump for modules as well.
  if (Args.hasFlag(options::OPT_frewrite_includes,
                   options::OPT_fno_rewrite_includes, false) ||
      (C.isForDiagnostics() && !HaveModules))
    CmdArgs.push_back("-frewrite-includes");

  // Only allow -traditional or -traditional-cpp outside in preprocessing modes.
  if (Arg *A = Args.getLastArg(options::OPT_traditional,
                               options::OPT_traditional_cpp)) {
    if (isa<PreprocessJobAction>(JA))
      CmdArgs.push_back("-traditional-cpp");
    else
      D.Diag(diag::err_drv_clang_unsupported) << A->getAsString(Args);
  }

  Args.AddLastArg(CmdArgs, options::OPT_dM);
  Args.AddLastArg(CmdArgs, options::OPT_dD);

  // Handle serialized diagnostics.
  if (Arg *A = Args.getLastArg(options::OPT__serialize_diags)) {
    CmdArgs.push_back("-serialize-diagnostic-file");
    CmdArgs.push_back(Args.MakeArgString(A->getValue()));
  }

  if (Args.hasArg(options::OPT_fretain_comments_from_system_headers))
    CmdArgs.push_back("-fretain-comments-from-system-headers");

  // Forward -fcomment-block-commands to -cc1.
  Args.AddAllArgs(CmdArgs, options::OPT_fcomment_block_commands);
  // Forward -fparse-all-comments to -cc1.
  Args.AddAllArgs(CmdArgs, options::OPT_fparse_all_comments);

  // Turn -fplugin=name.so into -load name.so
  for (const Arg *A : Args.filtered(options::OPT_fplugin_EQ)) {
    CmdArgs.push_back("-load");
    CmdArgs.push_back(A->getValue());
    A->claim();
  }

  // Setup statistics file output.
  SmallString<128> StatsFile = getStatsFileName(Args, Output, Input, D);
  if (!StatsFile.empty())
    CmdArgs.push_back(Args.MakeArgString(Twine("-stats-file=") + StatsFile));

  // Forward -Xclang arguments to -cc1, and -mllvm arguments to the LLVM option
  // parser.
  // -finclude-default-header flag is for preprocessor,
  // do not pass it to other cc1 commands when save-temps is enabled
  if (C.getDriver().isSaveTempsEnabled() &&
      !isa<PreprocessJobAction>(JA)) {
    for (auto Arg : Args.filtered(options::OPT_Xclang)) {
      Arg->claim();
      if (StringRef(Arg->getValue()) != "-finclude-default-header")
        CmdArgs.push_back(Arg->getValue());
    }
  }
  else {
    Args.AddAllArgValues(CmdArgs, options::OPT_Xclang);
  }
  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    A->claim();

    // We translate this by hand to the -cc1 argument, since nightly test uses
    // it and developers have been trained to spell it with -mllvm. Both
    // spellings are now deprecated and should be removed.
    if (StringRef(A->getValue(0)) == "-disable-llvm-optzns") {
      CmdArgs.push_back("-disable-llvm-optzns");
    } else {
      A->render(Args, CmdArgs);
    }
  }

  // With -save-temps, we want to save the unoptimized bitcode output from the
  // CompileJobAction, use -disable-llvm-passes to get pristine IR generated
  // by the frontend.
  // When -fembed-bitcode is enabled, optimized bitcode is emitted because it
  // has slightly different breakdown between stages.
  // FIXME: -fembed-bitcode -save-temps will save optimized bitcode instead of
  // pristine IR generated by the frontend. Ideally, a new compile action should
  // be added so both IR can be captured.
  if (C.getDriver().isSaveTempsEnabled() &&
      !(C.getDriver().embedBitcodeInObject() && !C.getDriver().isUsingLTO()) &&
      isa<CompileJobAction>(JA))
    CmdArgs.push_back("-disable-llvm-passes");

  if (Output.getType() == types::TY_Dependencies) {
    // Handled with other dependency code.
  } else if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  addDashXForInput(Args, Input, CmdArgs);

  ArrayRef<InputInfo> FrontendInputs = Input;
  if (IsHeaderModulePrecompile)
    FrontendInputs = ModuleHeaderInputs;
  else if (Input.isNothing())
    FrontendInputs = {};

  for (const InputInfo &Input : FrontendInputs) {
    if (Input.isFilename())
      CmdArgs.push_back(Input.getFilename());
    else
      Input.getInputArg().renderAsInput(Args, CmdArgs);
  }

  Args.AddAllArgs(CmdArgs, options::OPT_undef);

  const char *Exec = D.getClangProgramPath();

  // Optionally embed the -cc1 level arguments into the debug info or a
  // section, for build analysis.
  // Also record command line arguments into the debug info if
  // -grecord-gcc-switches options is set on.
  // By default, -gno-record-gcc-switches is set on and no recording.
  auto GRecordSwitches =
      Args.hasFlag(options::OPT_grecord_command_line,
                   options::OPT_gno_record_command_line, false);
  auto FRecordSwitches =
      Args.hasFlag(options::OPT_frecord_command_line,
                   options::OPT_fno_record_command_line, false);
  if (FRecordSwitches && !Triple.isOSBinFormatELF())
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << Args.getLastArg(options::OPT_frecord_command_line)->getAsString(Args)
        << TripleStr;
  if (TC.UseDwarfDebugFlags() || GRecordSwitches || FRecordSwitches) {
    ArgStringList OriginalArgs;
    for (const auto &Arg : Args)
      Arg->render(Args, OriginalArgs);

    SmallString<256> Flags;
    Flags += Exec;
    for (const char *OriginalArg : OriginalArgs) {
      SmallString<128> EscapedArg;
      EscapeSpacesAndBackslashes(OriginalArg, EscapedArg);
      Flags += " ";
      Flags += EscapedArg;
    }
    auto FlagsArgString = Args.MakeArgString(Flags);
    if (TC.UseDwarfDebugFlags() || GRecordSwitches) {
      CmdArgs.push_back("-dwarf-debug-flags");
      CmdArgs.push_back(FlagsArgString);
    }
    if (FRecordSwitches) {
      CmdArgs.push_back("-record-command-line");
      CmdArgs.push_back(FlagsArgString);
    }
  }

  // Host-side cuda compilation receives all device-side outputs in a single
  // fatbin as Inputs[1]. Include the binary with -fcuda-include-gpubinary.
  if ((IsCuda || IsHIP) && CudaDeviceInput) {
      CmdArgs.push_back("-fcuda-include-gpubinary");
      CmdArgs.push_back(CudaDeviceInput->getFilename());
      if (Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc, false))
        CmdArgs.push_back("-fgpu-rdc");
  }

  if (IsCuda) {
    if (Args.hasFlag(options::OPT_fcuda_short_ptr,
                     options::OPT_fno_cuda_short_ptr, false))
      CmdArgs.push_back("-fcuda-short-ptr");
  }

  // OpenMP offloading device jobs take the argument -fopenmp-host-ir-file-path
  // to specify the result of the compile phase on the host, so the meaningful
  // device declarations can be identified. Also, -fopenmp-is-device is passed
  // along to tell the frontend that it is generating code for a device, so that
  // only the relevant declarations are emitted.
  if (IsOpenMPDevice) {
    CmdArgs.push_back("-fopenmp-is-device");
    if (OpenMPDeviceInput) {
      CmdArgs.push_back("-fopenmp-host-ir-file-path");
      CmdArgs.push_back(Args.MakeArgString(OpenMPDeviceInput->getFilename()));
    }
  }

  // For all the host OpenMP offloading compile jobs we need to pass the targets
  // information using -fopenmp-targets= option.
  if (JA.isHostOffloading(Action::OFK_OpenMP)) {
    SmallString<128> TargetInfo("-fopenmp-targets=");

    Arg *Tgts = Args.getLastArg(options::OPT_fopenmp_targets_EQ);
    assert(Tgts && Tgts->getNumValues() &&
           "OpenMP offloading has to have targets specified.");
    for (unsigned i = 0; i < Tgts->getNumValues(); ++i) {
      if (i)
        TargetInfo += ',';
      // We need to get the string from the triple because it may be not exactly
      // the same as the one we get directly from the arguments.
      llvm::Triple T(Tgts->getValue(i));
      TargetInfo += T.getTriple();
    }
    CmdArgs.push_back(Args.MakeArgString(TargetInfo.str()));
  }

  bool WholeProgramVTables =
      Args.hasFlag(options::OPT_fwhole_program_vtables,
                   options::OPT_fno_whole_program_vtables, false);
  if (WholeProgramVTables) {
    if (!D.isUsingLTO())
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << "-fwhole-program-vtables"
          << "-flto";
    CmdArgs.push_back("-fwhole-program-vtables");
  }

  bool RequiresSplitLTOUnit = WholeProgramVTables || Sanitize.needsLTO();
  bool SplitLTOUnit =
      Args.hasFlag(options::OPT_fsplit_lto_unit,
                   options::OPT_fno_split_lto_unit, RequiresSplitLTOUnit);
  if (RequiresSplitLTOUnit && !SplitLTOUnit)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << "-fno-split-lto-unit"
        << (WholeProgramVTables ? "-fwhole-program-vtables" : "-fsanitize=cfi");
  if (SplitLTOUnit)
    CmdArgs.push_back("-fsplit-lto-unit");

  if (Arg *A = Args.getLastArg(options::OPT_fexperimental_isel,
                               options::OPT_fno_experimental_isel)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_fexperimental_isel)) {
      CmdArgs.push_back("-global-isel=1");

      // GISel is on by default on AArch64 -O0, so don't bother adding
      // the fallback remarks for it. Other combinations will add a warning of
      // some kind.
      bool IsArchSupported = Triple.getArch() == llvm::Triple::aarch64;
      bool IsOptLevelSupported = false;

      Arg *A = Args.getLastArg(options::OPT_O_Group);
      if (Triple.getArch() == llvm::Triple::aarch64) {
        if (!A || A->getOption().matches(options::OPT_O0))
          IsOptLevelSupported = true;
      }
      if (!IsArchSupported || !IsOptLevelSupported) {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-global-isel-abort=2");

        if (!IsArchSupported)
          D.Diag(diag::warn_drv_experimental_isel_incomplete) << Triple.getArchName();
        else
          D.Diag(diag::warn_drv_experimental_isel_incomplete_opt);
      }
    } else {
      CmdArgs.push_back("-global-isel=0");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fforce_enable_int128,
                               options::OPT_fno_force_enable_int128)) {
    if (A->getOption().matches(options::OPT_fforce_enable_int128))
      CmdArgs.push_back("-fforce-enable-int128");
  }

  if (Args.hasFlag(options::OPT_fcomplete_member_pointers,
                   options::OPT_fno_complete_member_pointers, false))
    CmdArgs.push_back("-fcomplete-member-pointers");

  if (!Args.hasFlag(options::OPT_fcxx_static_destructors,
                    options::OPT_fno_cxx_static_destructors, true))
    CmdArgs.push_back("-fno-c++-static-destructors");

  if (Arg *A = Args.getLastArg(options::OPT_moutline,
                               options::OPT_mno_outline)) {
    if (A->getOption().matches(options::OPT_moutline)) {
      // We only support -moutline in AArch64 right now. If we're not compiling
      // for AArch64, emit a warning and ignore the flag. Otherwise, add the
      // proper mllvm flags.
      if (Triple.getArch() != llvm::Triple::aarch64) {
        D.Diag(diag::warn_drv_moutline_unsupported_opt) << Triple.getArchName();
      } else {
          CmdArgs.push_back("-mllvm");
          CmdArgs.push_back("-enable-machine-outliner");
      }
    } else {
      // Disable all outlining behaviour.
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-enable-machine-outliner=never");
    }
  }

  if (Args.hasFlag(options::OPT_faddrsig, options::OPT_fno_addrsig,
                   (TC.getTriple().isOSBinFormatELF() ||
                    TC.getTriple().isOSBinFormatCOFF()) &&
                      !TC.getTriple().isPS4() &&
                      !TC.getTriple().isOSNetBSD() &&
                      !Distro(D.getVFS()).IsGentoo() &&
                      !TC.getTriple().isAndroid() &&
                       TC.useIntegratedAs()))
    CmdArgs.push_back("-faddrsig");

  // Finally add the compile command to the compilation.
  if (Args.hasArg(options::OPT__SLASH_fallback) &&
      Output.getType() == types::TY_Object &&
      (InputType == types::TY_C || InputType == types::TY_CXX)) {
    auto CLCommand =
        getCLFallback()->GetCommand(C, JA, Output, Inputs, Args, LinkingOutput);
    C.addCommand(llvm::make_unique<FallbackCommand>(
        JA, *this, Exec, CmdArgs, Inputs, std::move(CLCommand)));
  } else if (Args.hasArg(options::OPT__SLASH_fallback) &&
             isa<PrecompileJobAction>(JA)) {
    // In /fallback builds, run the main compilation even if the pch generation
    // fails, so that the main compilation's fallback to cl.exe runs.
    C.addCommand(llvm::make_unique<ForceSuccessCommand>(JA, *this, Exec,
                                                        CmdArgs, Inputs));
  } else {
    C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
  }

  // Make the compile command echo its inputs for /showFilenames.
  if (Output.getType() == types::TY_Object &&
      Args.hasFlag(options::OPT__SLASH_showFilenames,
                   options::OPT__SLASH_showFilenames_, false)) {
    C.getJobs().getJobs().back()->setPrintInputFilenames(true);
  }

  if (Arg *A = Args.getLastArg(options::OPT_pg))
    if (!shouldUseFramePointer(Args, Triple))
      D.Diag(diag::err_drv_argument_not_allowed_with) << "-fomit-frame-pointer"
                                                      << A->getAsString(Args);

  // Claim some arguments which clang supports automatically.

  // -fpch-preprocess is used with gcc to add a special marker in the output to
  // include the PCH file.
  Args.ClaimAllArgs(options::OPT_fpch_preprocess);

  // Claim some arguments which clang doesn't support, but we don't
  // care to warn the user about.
  Args.ClaimAllArgs(options::OPT_clang_ignored_f_Group);
  Args.ClaimAllArgs(options::OPT_clang_ignored_m_Group);

  // Disable warnings for clang -E -emit-llvm foo.c
  Args.ClaimAllArgs(options::OPT_emit_llvm);
}

Clang::Clang(const ToolChain &TC)
    // CAUTION! The first constructor argument ("clang") is not arbitrary,
    // as it is for other tools. Some operations on a Tool actually test
    // whether that tool is Clang based on the Tool's Name as a string.
    : Tool("clang", "clang frontend", TC, RF_Full) {}

Clang::~Clang() {}

/// Add options related to the Objective-C runtime/ABI.
///
/// Returns true if the runtime is non-fragile.
ObjCRuntime Clang::AddObjCRuntimeArgs(const ArgList &args,
                                      ArgStringList &cmdArgs,
                                      RewriteKind rewriteKind) const {
  // Look for the controlling runtime option.
  Arg *runtimeArg =
      args.getLastArg(options::OPT_fnext_runtime, options::OPT_fgnu_runtime,
                      options::OPT_fobjc_runtime_EQ);

  // Just forward -fobjc-runtime= to the frontend.  This supercedes
  // options about fragility.
  if (runtimeArg &&
      runtimeArg->getOption().matches(options::OPT_fobjc_runtime_EQ)) {
    ObjCRuntime runtime;
    StringRef value = runtimeArg->getValue();
    if (runtime.tryParse(value)) {
      getToolChain().getDriver().Diag(diag::err_drv_unknown_objc_runtime)
          << value;
    }
    if ((runtime.getKind() == ObjCRuntime::GNUstep) &&
        (runtime.getVersion() >= VersionTuple(2, 0)))
      if (!getToolChain().getTriple().isOSBinFormatELF() &&
          !getToolChain().getTriple().isOSBinFormatCOFF()) {
        getToolChain().getDriver().Diag(
            diag::err_drv_gnustep_objc_runtime_incompatible_binary)
          << runtime.getVersion().getMajor();
      }

    runtimeArg->render(args, cmdArgs);
    return runtime;
  }

  // Otherwise, we'll need the ABI "version".  Version numbers are
  // slightly confusing for historical reasons:
  //   1 - Traditional "fragile" ABI
  //   2 - Non-fragile ABI, version 1
  //   3 - Non-fragile ABI, version 2
  unsigned objcABIVersion = 1;
  // If -fobjc-abi-version= is present, use that to set the version.
  if (Arg *abiArg = args.getLastArg(options::OPT_fobjc_abi_version_EQ)) {
    StringRef value = abiArg->getValue();
    if (value == "1")
      objcABIVersion = 1;
    else if (value == "2")
      objcABIVersion = 2;
    else if (value == "3")
      objcABIVersion = 3;
    else
      getToolChain().getDriver().Diag(diag::err_drv_clang_unsupported) << value;
  } else {
    // Otherwise, determine if we are using the non-fragile ABI.
    bool nonFragileABIIsDefault =
        (rewriteKind == RK_NonFragile ||
         (rewriteKind == RK_None &&
          getToolChain().IsObjCNonFragileABIDefault()));
    if (args.hasFlag(options::OPT_fobjc_nonfragile_abi,
                     options::OPT_fno_objc_nonfragile_abi,
                     nonFragileABIIsDefault)) {
// Determine the non-fragile ABI version to use.
#ifdef DISABLE_DEFAULT_NONFRAGILEABI_TWO
      unsigned nonFragileABIVersion = 1;
#else
      unsigned nonFragileABIVersion = 2;
#endif

      if (Arg *abiArg =
              args.getLastArg(options::OPT_fobjc_nonfragile_abi_version_EQ)) {
        StringRef value = abiArg->getValue();
        if (value == "1")
          nonFragileABIVersion = 1;
        else if (value == "2")
          nonFragileABIVersion = 2;
        else
          getToolChain().getDriver().Diag(diag::err_drv_clang_unsupported)
              << value;
      }

      objcABIVersion = 1 + nonFragileABIVersion;
    } else {
      objcABIVersion = 1;
    }
  }

  // We don't actually care about the ABI version other than whether
  // it's non-fragile.
  bool isNonFragile = objcABIVersion != 1;

  // If we have no runtime argument, ask the toolchain for its default runtime.
  // However, the rewriter only really supports the Mac runtime, so assume that.
  ObjCRuntime runtime;
  if (!runtimeArg) {
    switch (rewriteKind) {
    case RK_None:
      runtime = getToolChain().getDefaultObjCRuntime(isNonFragile);
      break;
    case RK_Fragile:
      runtime = ObjCRuntime(ObjCRuntime::FragileMacOSX, VersionTuple());
      break;
    case RK_NonFragile:
      runtime = ObjCRuntime(ObjCRuntime::MacOSX, VersionTuple());
      break;
    }

    // -fnext-runtime
  } else if (runtimeArg->getOption().matches(options::OPT_fnext_runtime)) {
    // On Darwin, make this use the default behavior for the toolchain.
    if (getToolChain().getTriple().isOSDarwin()) {
      runtime = getToolChain().getDefaultObjCRuntime(isNonFragile);

      // Otherwise, build for a generic macosx port.
    } else {
      runtime = ObjCRuntime(ObjCRuntime::MacOSX, VersionTuple());
    }

    // -fgnu-runtime
  } else {
    assert(runtimeArg->getOption().matches(options::OPT_fgnu_runtime));
    // Legacy behaviour is to target the gnustep runtime if we are in
    // non-fragile mode or the GCC runtime in fragile mode.
    if (isNonFragile)
      runtime = ObjCRuntime(ObjCRuntime::GNUstep, VersionTuple(2, 0));
    else
      runtime = ObjCRuntime(ObjCRuntime::GCC, VersionTuple());
  }

  cmdArgs.push_back(
      args.MakeArgString("-fobjc-runtime=" + runtime.getAsString()));
  return runtime;
}

static bool maybeConsumeDash(const std::string &EH, size_t &I) {
  bool HaveDash = (I + 1 < EH.size() && EH[I + 1] == '-');
  I += HaveDash;
  return !HaveDash;
}

namespace {
struct EHFlags {
  bool Synch = false;
  bool Asynch = false;
  bool NoUnwindC = false;
};
} // end anonymous namespace

/// /EH controls whether to run destructor cleanups when exceptions are
/// thrown.  There are three modifiers:
/// - s: Cleanup after "synchronous" exceptions, aka C++ exceptions.
/// - a: Cleanup after "asynchronous" exceptions, aka structured exceptions.
///      The 'a' modifier is unimplemented and fundamentally hard in LLVM IR.
/// - c: Assume that extern "C" functions are implicitly nounwind.
/// The default is /EHs-c-, meaning cleanups are disabled.
static EHFlags parseClangCLEHFlags(const Driver &D, const ArgList &Args) {
  EHFlags EH;

  std::vector<std::string> EHArgs =
      Args.getAllArgValues(options::OPT__SLASH_EH);
  for (auto EHVal : EHArgs) {
    for (size_t I = 0, E = EHVal.size(); I != E; ++I) {
      switch (EHVal[I]) {
      case 'a':
        EH.Asynch = maybeConsumeDash(EHVal, I);
        if (EH.Asynch)
          EH.Synch = false;
        continue;
      case 'c':
        EH.NoUnwindC = maybeConsumeDash(EHVal, I);
        continue;
      case 's':
        EH.Synch = maybeConsumeDash(EHVal, I);
        if (EH.Synch)
          EH.Asynch = false;
        continue;
      default:
        break;
      }
      D.Diag(clang::diag::err_drv_invalid_value) << "/EH" << EHVal;
      break;
    }
  }
  // The /GX, /GX- flags are only processed if there are not /EH flags.
  // The default is that /GX is not specified.
  if (EHArgs.empty() &&
      Args.hasFlag(options::OPT__SLASH_GX, options::OPT__SLASH_GX_,
                   /*default=*/false)) {
    EH.Synch = true;
    EH.NoUnwindC = true;
  }

  return EH;
}

void Clang::AddClangCLArgs(const ArgList &Args, types::ID InputType,
                           ArgStringList &CmdArgs,
                           codegenoptions::DebugInfoKind *DebugInfoKind,
                           bool *EmitCodeView) const {
  unsigned RTOptionID = options::OPT__SLASH_MT;

  if (Args.hasArg(options::OPT__SLASH_LDd))
    // The /LDd option implies /MTd. The dependent lib part can be overridden,
    // but defining _DEBUG is sticky.
    RTOptionID = options::OPT__SLASH_MTd;

  if (Arg *A = Args.getLastArg(options::OPT__SLASH_M_Group))
    RTOptionID = A->getOption().getID();

  StringRef FlagForCRT;
  switch (RTOptionID) {
  case options::OPT__SLASH_MD:
    if (Args.hasArg(options::OPT__SLASH_LDd))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrt";
    break;
  case options::OPT__SLASH_MDd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrtd";
    break;
  case options::OPT__SLASH_MT:
    if (Args.hasArg(options::OPT__SLASH_LDd))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-flto-visibility-public-std");
    FlagForCRT = "--dependent-lib=libcmt";
    break;
  case options::OPT__SLASH_MTd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-flto-visibility-public-std");
    FlagForCRT = "--dependent-lib=libcmtd";
    break;
  default:
    llvm_unreachable("Unexpected option ID.");
  }

  if (Args.hasArg(options::OPT__SLASH_Zl)) {
    CmdArgs.push_back("-D_VC_NODEFAULTLIB");
  } else {
    CmdArgs.push_back(FlagForCRT.data());

    // This provides POSIX compatibility (maps 'open' to '_open'), which most
    // users want.  The /Za flag to cl.exe turns this off, but it's not
    // implemented in clang.
    CmdArgs.push_back("--dependent-lib=oldnames");
  }

  if (Arg *A = Args.getLastArg(options::OPT_show_includes))
    A->render(Args, CmdArgs);

  // This controls whether or not we emit RTTI data for polymorphic types.
  if (Args.hasFlag(options::OPT__SLASH_GR_, options::OPT__SLASH_GR,
                   /*default=*/false))
    CmdArgs.push_back("-fno-rtti-data");

  // This controls whether or not we emit stack-protector instrumentation.
  // In MSVC, Buffer Security Check (/GS) is on by default.
  if (Args.hasFlag(options::OPT__SLASH_GS, options::OPT__SLASH_GS_,
                   /*default=*/true)) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(Twine(LangOptions::SSPStrong)));
  }

  // Emit CodeView if -Z7, -Zd, or -gline-tables-only are present.
  if (Arg *DebugInfoArg =
          Args.getLastArg(options::OPT__SLASH_Z7, options::OPT__SLASH_Zd,
                          options::OPT_gline_tables_only)) {
    *EmitCodeView = true;
    if (DebugInfoArg->getOption().matches(options::OPT__SLASH_Z7))
      *DebugInfoKind = codegenoptions::LimitedDebugInfo;
    else
      *DebugInfoKind = codegenoptions::DebugLineTablesOnly;
  } else {
    *EmitCodeView = false;
  }

  const Driver &D = getToolChain().getDriver();
  EHFlags EH = parseClangCLEHFlags(D, Args);
  if (EH.Synch || EH.Asynch) {
    if (types::isCXX(InputType))
      CmdArgs.push_back("-fcxx-exceptions");
    CmdArgs.push_back("-fexceptions");
  }
  if (types::isCXX(InputType) && EH.Synch && EH.NoUnwindC)
    CmdArgs.push_back("-fexternc-nounwind");

  // /EP should expand to -E -P.
  if (Args.hasArg(options::OPT__SLASH_EP)) {
    CmdArgs.push_back("-E");
    CmdArgs.push_back("-P");
  }

  unsigned VolatileOptionID;
  if (getToolChain().getArch() == llvm::Triple::x86_64 ||
      getToolChain().getArch() == llvm::Triple::x86)
    VolatileOptionID = options::OPT__SLASH_volatile_ms;
  else
    VolatileOptionID = options::OPT__SLASH_volatile_iso;

  if (Arg *A = Args.getLastArg(options::OPT__SLASH_volatile_Group))
    VolatileOptionID = A->getOption().getID();

  if (VolatileOptionID == options::OPT__SLASH_volatile_ms)
    CmdArgs.push_back("-fms-volatile");

 if (Args.hasFlag(options::OPT__SLASH_Zc_dllexportInlines_,
                  options::OPT__SLASH_Zc_dllexportInlines,
                  false)) {
   if (Args.hasArg(options::OPT__SLASH_fallback)) {
     D.Diag(clang::diag::err_drv_dllexport_inlines_and_fallback);
   } else {
    CmdArgs.push_back("-fno-dllexport-inlines");
   }
 }

  Arg *MostGeneralArg = Args.getLastArg(options::OPT__SLASH_vmg);
  Arg *BestCaseArg = Args.getLastArg(options::OPT__SLASH_vmb);
  if (MostGeneralArg && BestCaseArg)
    D.Diag(clang::diag::err_drv_argument_not_allowed_with)
        << MostGeneralArg->getAsString(Args) << BestCaseArg->getAsString(Args);

  if (MostGeneralArg) {
    Arg *SingleArg = Args.getLastArg(options::OPT__SLASH_vms);
    Arg *MultipleArg = Args.getLastArg(options::OPT__SLASH_vmm);
    Arg *VirtualArg = Args.getLastArg(options::OPT__SLASH_vmv);

    Arg *FirstConflict = SingleArg ? SingleArg : MultipleArg;
    Arg *SecondConflict = VirtualArg ? VirtualArg : MultipleArg;
    if (FirstConflict && SecondConflict && FirstConflict != SecondConflict)
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << FirstConflict->getAsString(Args)
          << SecondConflict->getAsString(Args);

    if (SingleArg)
      CmdArgs.push_back("-fms-memptr-rep=single");
    else if (MultipleArg)
      CmdArgs.push_back("-fms-memptr-rep=multiple");
    else
      CmdArgs.push_back("-fms-memptr-rep=virtual");
  }

  // Parse the default calling convention options.
  if (Arg *CCArg =
          Args.getLastArg(options::OPT__SLASH_Gd, options::OPT__SLASH_Gr,
                          options::OPT__SLASH_Gz, options::OPT__SLASH_Gv,
                          options::OPT__SLASH_Gregcall)) {
    unsigned DCCOptId = CCArg->getOption().getID();
    const char *DCCFlag = nullptr;
    bool ArchSupported = true;
    llvm::Triple::ArchType Arch = getToolChain().getArch();
    switch (DCCOptId) {
    case options::OPT__SLASH_Gd:
      DCCFlag = "-fdefault-calling-conv=cdecl";
      break;
    case options::OPT__SLASH_Gr:
      ArchSupported = Arch == llvm::Triple::x86;
      DCCFlag = "-fdefault-calling-conv=fastcall";
      break;
    case options::OPT__SLASH_Gz:
      ArchSupported = Arch == llvm::Triple::x86;
      DCCFlag = "-fdefault-calling-conv=stdcall";
      break;
    case options::OPT__SLASH_Gv:
      ArchSupported = Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=vectorcall";
      break;
    case options::OPT__SLASH_Gregcall:
      ArchSupported = Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=regcall";
      break;
    }

    // MSVC doesn't warn if /Gr or /Gz is used on x64, so we don't either.
    if (ArchSupported && DCCFlag)
      CmdArgs.push_back(DCCFlag);
  }

  if (Arg *A = Args.getLastArg(options::OPT_vtordisp_mode_EQ))
    A->render(Args, CmdArgs);

  if (!Args.hasArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    if (Args.hasArg(options::OPT__SLASH_fallback))
      CmdArgs.push_back("msvc-fallback");
    else
      CmdArgs.push_back("msvc");
  }

  if (Arg *A = Args.getLastArg(options::OPT__SLASH_guard)) {
    SmallVector<StringRef, 1> SplitArgs;
    StringRef(A->getValue()).split(SplitArgs, ",");
    bool Instrument = false;
    bool NoChecks = false;
    for (StringRef Arg : SplitArgs) {
      if (Arg.equals_lower("cf"))
        Instrument = true;
      else if (Arg.equals_lower("cf-"))
        Instrument = false;
      else if (Arg.equals_lower("nochecks"))
        NoChecks = true;
      else if (Arg.equals_lower("nochecks-"))
        NoChecks = false;
      else
        D.Diag(diag::err_drv_invalid_value) << A->getSpelling() << Arg;
    }
    // Currently there's no support emitting CFG instrumentation; the flag only
    // emits the table of address-taken functions.
    if (Instrument || NoChecks)
      CmdArgs.push_back("-cfguard");
  }
}

visualstudio::Compiler *Clang::getCLFallback() const {
  if (!CLFallback)
    CLFallback.reset(new visualstudio::Compiler(getToolChain()));
  return CLFallback.get();
}


const char *Clang::getBaseInputName(const ArgList &Args,
                                    const InputInfo &Input) {
  return Args.MakeArgString(llvm::sys::path::filename(Input.getBaseInput()));
}

const char *Clang::getBaseInputStem(const ArgList &Args,
                                    const InputInfoList &Inputs) {
  const char *Str = getBaseInputName(Args, Inputs[0]);

  if (const char *End = strrchr(Str, '.'))
    return Args.MakeArgString(std::string(Str, End));

  return Str;
}

const char *Clang::getDependencyFileName(const ArgList &Args,
                                         const InputInfoList &Inputs) {
  // FIXME: Think about this more.
  std::string Res;

  if (Arg *OutputOpt = Args.getLastArg(options::OPT_o)) {
    std::string Str(OutputOpt->getValue());
    Res = Str.substr(0, Str.rfind('.'));
  } else {
    Res = getBaseInputStem(Args, Inputs);
  }
  return Args.MakeArgString(Res + ".d");
}

// Begin ClangAs

void ClangAs::AddMIPSTargetArgs(const ArgList &Args,
                                ArgStringList &CmdArgs) const {
  StringRef CPUName;
  StringRef ABIName;
  const llvm::Triple &Triple = getToolChain().getTriple();
  mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());
}

void ClangAs::AddX86TargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
    } else {
      getToolChain().getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getOption().getName() << Value;
    }
  }
}

void ClangAs::ConstructJob(Compilation &C, const JobAction &JA,
                           const InputInfo &Output, const InputInfoList &Inputs,
                           const ArgList &Args,
                           const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();
  const auto &D = getToolChain().getDriver();

  // Don't warn about "clang -w -c foo.s"
  Args.ClaimAllArgs(options::OPT_w);
  // and "clang -emit-llvm -c foo.s"
  Args.ClaimAllArgs(options::OPT_emit_llvm);

  claimNoWarnArgs(Args);

  // Invoke ourselves in -cc1as mode.
  //
  // FIXME: Implement custom jobs for internal actions.
  CmdArgs.push_back("-cc1as");

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  // Set the output mode, we currently only expect to be used as a real
  // assembler.
  CmdArgs.push_back("-filetype");
  CmdArgs.push_back("obj");

  // Set the main file name, so that debug info works even with
  // -save-temps or preprocessed assembly.
  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(Clang::getBaseInputName(Args, Input));

  // Add the target cpu
  std::string CPU = getCPUName(Args, Triple, /*FromAs*/ true);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  // Add the target features
  getTargetFeatures(getToolChain(), Triple, Args, CmdArgs, true);

  // Ignore explicit -force_cpusubtype_ALL option.
  (void)Args.hasArg(options::OPT_force__cpusubtype__ALL);

  // Pass along any -I options so we get proper .include search paths.
  Args.AddAllArgs(CmdArgs, options::OPT_I_Group);

  // Determine the original source input.
  const Action *SourceAction = &JA;
  while (SourceAction->getKind() != Action::InputClass) {
    assert(!SourceAction->getInputs().empty() && "unexpected root action!");
    SourceAction = SourceAction->getInputs()[0];
  }

  // Forward -g and handle debug info related flags, assuming we are dealing
  // with an actual assembly file.
  bool WantDebug = false;
  unsigned DwarfVersion = 0;
  Args.ClaimAllArgs(options::OPT_g_Group);
  if (Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    WantDebug = !A->getOption().matches(options::OPT_g0) &&
                !A->getOption().matches(options::OPT_ggdb0);
    if (WantDebug)
      DwarfVersion = DwarfVersionNum(A->getSpelling());
  }
  if (DwarfVersion == 0)
    DwarfVersion = getToolChain().GetDefaultDwarfVersion();

  codegenoptions::DebugInfoKind DebugInfoKind = codegenoptions::NoDebugInfo;

  if (SourceAction->getType() == types::TY_Asm ||
      SourceAction->getType() == types::TY_PP_Asm) {
    // You might think that it would be ok to set DebugInfoKind outside of
    // the guard for source type, however there is a test which asserts
    // that some assembler invocation receives no -debug-info-kind,
    // and it's not clear whether that test is just overly restrictive.
    DebugInfoKind = (WantDebug ? codegenoptions::LimitedDebugInfo
                               : codegenoptions::NoDebugInfo);
    // Add the -fdebug-compilation-dir flag if needed.
    addDebugCompDirArg(Args, CmdArgs);

    addDebugPrefixMapArg(getToolChain().getDriver(), Args, CmdArgs);

    // Set the AT_producer to the clang version when using the integrated
    // assembler on assembly source files.
    CmdArgs.push_back("-dwarf-debug-producer");
    CmdArgs.push_back(Args.MakeArgString(getClangFullVersion()));

    // And pass along -I options
    Args.AddAllArgs(CmdArgs, options::OPT_I);
  }
  RenderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, DwarfVersion,
                          llvm::DebuggerKind::Default);
  RenderDebugInfoCompressionArgs(Args, CmdArgs, D, getToolChain());


  // Handle -fPIC et al -- the relocation-model affects the assembler
  // for some targets.
  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  const char *RMName = RelocationModelName(RelocationModel);
  if (RMName) {
    CmdArgs.push_back("-mrelocation-model");
    CmdArgs.push_back(RMName);
  }

  // Optionally embed the -cc1as level arguments into the debug info, for build
  // analysis.
  if (getToolChain().UseDwarfDebugFlags()) {
    ArgStringList OriginalArgs;
    for (const auto &Arg : Args)
      Arg->render(Args, OriginalArgs);

    SmallString<256> Flags;
    const char *Exec = getToolChain().getDriver().getClangProgramPath();
    Flags += Exec;
    for (const char *OriginalArg : OriginalArgs) {
      SmallString<128> EscapedArg;
      EscapeSpacesAndBackslashes(OriginalArg, EscapedArg);
      Flags += " ";
      Flags += EscapedArg;
    }
    CmdArgs.push_back("-dwarf-debug-flags");
    CmdArgs.push_back(Args.MakeArgString(Flags));
  }

  // FIXME: Add -static support, once we have it.

  // Add target specific flags.
  switch (getToolChain().getArch()) {
  default:
    break;

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    AddMIPSTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    // This isn't in AddARMTargetArgs because we want to do this for assembly
    // only, not C/C++.
    if (Args.hasFlag(options::OPT_mdefault_build_attributes,
                     options::OPT_mno_default_build_attributes, true)) {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-arm-add-build-attributes");
    }
    break;
  }

  // Consume all the warning flags. Usually this would be handled more
  // gracefully by -cc1 (warning about unknown warning flags, etc) but -cc1as
  // doesn't handle that so rather than warning about unused flags that are
  // actually used, we'll lie by omission instead.
  // FIXME: Stop lying and consume only the appropriate driver flags
  Args.ClaimAllArgs(options::OPT_W_Group);

  CollectArgsForIntegratedAssembler(C, Args, CmdArgs,
                                    getToolChain().getDriver());

  Args.AddAllArgs(CmdArgs, options::OPT_mllvm);

  assert(Output.isFilename() && "Unexpected lipo output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  const llvm::Triple &T = getToolChain().getTriple();
  Arg *A;
  if ((getDebugFissionKind(D, Args, A) == DwarfFissionKind::Split) &&
      (T.isOSLinux() || T.isOSFuchsia())) {
    CmdArgs.push_back("-split-dwarf-file");
    CmdArgs.push_back(SplitDebugName(Args, Output));
  }

  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec = getToolChain().getDriver().getClangProgramPath();
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

// Begin OffloadBundler

void OffloadBundler::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const llvm::opt::ArgList &TCArgs,
                                  const char *LinkingOutput) const {
  // The version with only one output is expected to refer to a bundling job.
  assert(isa<OffloadBundlingJobAction>(JA) && "Expecting bundling job!");

  // The bundling command looks like this:
  // clang-offload-bundler -type=bc
  //   -targets=host-triple,openmp-triple1,openmp-triple2
  //   -outputs=input_file
  //   -inputs=unbundle_file_host,unbundle_file_tgt1,unbundle_file_tgt2"

  ArgStringList CmdArgs;

  // Get the type.
  CmdArgs.push_back(TCArgs.MakeArgString(
      Twine("-type=") + types::getTypeTempSuffix(Output.getType())));

  assert(JA.getInputs().size() == Inputs.size() &&
         "Not have inputs for all dependence actions??");

  // Get the targets.
  SmallString<128> Triples;
  Triples += "-targets=";
  for (unsigned I = 0; I < Inputs.size(); ++I) {
    if (I)
      Triples += ',';

    // Find ToolChain for this input.
    Action::OffloadKind CurKind = Action::OFK_Host;
    const ToolChain *CurTC = &getToolChain();
    const Action *CurDep = JA.getInputs()[I];

    if (const auto *OA = dyn_cast<OffloadAction>(CurDep)) {
      CurTC = nullptr;
      OA->doOnEachDependence([&](Action *A, const ToolChain *TC, const char *) {
        assert(CurTC == nullptr && "Expected one dependence!");
        CurKind = A->getOffloadingDeviceKind();
        CurTC = TC;
      });
    }
    Triples += Action::GetOffloadKindName(CurKind);
    Triples += '-';
    Triples += CurTC->getTriple().normalize();
    if (CurKind == Action::OFK_HIP && CurDep->getOffloadingArch()) {
      Triples += '-';
      Triples += CurDep->getOffloadingArch();
    }
  }
  CmdArgs.push_back(TCArgs.MakeArgString(Triples));

  // Get bundled file command.
  CmdArgs.push_back(
      TCArgs.MakeArgString(Twine("-outputs=") + Output.getFilename()));

  // Get unbundled files command.
  SmallString<128> UB;
  UB += "-inputs=";
  for (unsigned I = 0; I < Inputs.size(); ++I) {
    if (I)
      UB += ',';

    // Find ToolChain for this input.
    const ToolChain *CurTC = &getToolChain();
    if (const auto *OA = dyn_cast<OffloadAction>(JA.getInputs()[I])) {
      CurTC = nullptr;
      OA->doOnEachDependence([&](Action *, const ToolChain *TC, const char *) {
        assert(CurTC == nullptr && "Expected one dependence!");
        CurTC = TC;
      });
    }
    UB += CurTC->getInputFilename(Inputs[I]);
  }
  CmdArgs.push_back(TCArgs.MakeArgString(UB));

  // All the inputs are encoded as commands.
  C.addCommand(llvm::make_unique<Command>(
      JA, *this,
      TCArgs.MakeArgString(getToolChain().GetProgramPath(getShortName())),
      CmdArgs, None));
}

void OffloadBundler::ConstructJobMultipleOutputs(
    Compilation &C, const JobAction &JA, const InputInfoList &Outputs,
    const InputInfoList &Inputs, const llvm::opt::ArgList &TCArgs,
    const char *LinkingOutput) const {
  // The version with multiple outputs is expected to refer to a unbundling job.
  auto &UA = cast<OffloadUnbundlingJobAction>(JA);

  // The unbundling command looks like this:
  // clang-offload-bundler -type=bc
  //   -targets=host-triple,openmp-triple1,openmp-triple2
  //   -inputs=input_file
  //   -outputs=unbundle_file_host,unbundle_file_tgt1,unbundle_file_tgt2"
  //   -unbundle

  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Expecting to unbundle a single file!");
  InputInfo Input = Inputs.front();

  // Get the type.
  CmdArgs.push_back(TCArgs.MakeArgString(
      Twine("-type=") + types::getTypeTempSuffix(Input.getType())));

  // Get the targets.
  SmallString<128> Triples;
  Triples += "-targets=";
  auto DepInfo = UA.getDependentActionsInfo();
  for (unsigned I = 0; I < DepInfo.size(); ++I) {
    if (I)
      Triples += ',';

    auto &Dep = DepInfo[I];
    Triples += Action::GetOffloadKindName(Dep.DependentOffloadKind);
    Triples += '-';
    Triples += Dep.DependentToolChain->getTriple().normalize();
    if (Dep.DependentOffloadKind == Action::OFK_HIP &&
        !Dep.DependentBoundArch.empty()) {
      Triples += '-';
      Triples += Dep.DependentBoundArch;
    }
  }

  CmdArgs.push_back(TCArgs.MakeArgString(Triples));

  // Get bundled file command.
  CmdArgs.push_back(
      TCArgs.MakeArgString(Twine("-inputs=") + Input.getFilename()));

  // Get unbundled files command.
  SmallString<128> UB;
  UB += "-outputs=";
  for (unsigned I = 0; I < Outputs.size(); ++I) {
    if (I)
      UB += ',';
    UB += DepInfo[I].DependentToolChain->getInputFilename(Outputs[I]);
  }
  CmdArgs.push_back(TCArgs.MakeArgString(UB));
  CmdArgs.push_back("-unbundle");

  // All the inputs are encoded as commands.
  C.addCommand(llvm::make_unique<Command>(
      JA, *this,
      TCArgs.MakeArgString(getToolChain().GetProgramPath(getShortName())),
      CmdArgs, None));
}
