//===--- ARM.cpp - ARM (not AArch64) Helpers for Tools ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/TargetParser.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// Get SubArch (vN).
int arm::getARMSubArchVersionNumber(const llvm::Triple &Triple) {
  llvm::StringRef Arch = Triple.getArchName();
  return llvm::ARM::parseArchVersion(Arch);
}

// True if M-profile.
bool arm::isARMMProfile(const llvm::Triple &Triple) {
  llvm::StringRef Arch = Triple.getArchName();
  return llvm::ARM::parseArchProfile(Arch) == llvm::ARM::ProfileKind::M;
}

// Get Arch/CPU from args.
void arm::getARMArchCPUFromArgs(const ArgList &Args, llvm::StringRef &Arch,
                                llvm::StringRef &CPU, bool FromAs) {
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ))
    CPU = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
    Arch = A->getValue();
  if (!FromAs)
    return;

  for (const Arg *A :
       Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
    StringRef Value = A->getValue();
    if (Value.startswith("-mcpu="))
      CPU = Value.substr(6);
    if (Value.startswith("-march="))
      Arch = Value.substr(7);
  }
}

// Handle -mhwdiv=.
// FIXME: Use ARMTargetParser.
static void getARMHWDivFeatures(const Driver &D, const Arg *A,
                                const ArgList &Args, StringRef HWDiv,
                                std::vector<StringRef> &Features) {
  unsigned HWDivID = llvm::ARM::parseHWDiv(HWDiv);
  if (!llvm::ARM::getHWDivFeatures(HWDivID, Features))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
}

// Handle -mfpu=.
static void getARMFPUFeatures(const Driver &D, const Arg *A,
                              const ArgList &Args, StringRef FPU,
                              std::vector<StringRef> &Features) {
  unsigned FPUID = llvm::ARM::parseFPU(FPU);
  if (!llvm::ARM::getFPUFeatures(FPUID, Features))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
}

// Decode ARM features from string like +[no]featureA+[no]featureB+...
static bool DecodeARMFeatures(const Driver &D, StringRef text,
                              std::vector<StringRef> &Features) {
  SmallVector<StringRef, 8> Split;
  text.split(Split, StringRef("+"), -1, false);

  for (StringRef Feature : Split) {
    StringRef FeatureName = llvm::ARM::getArchExtFeature(Feature);
    if (!FeatureName.empty())
      Features.push_back(FeatureName);
    else
      return false;
  }
  return true;
}

static void DecodeARMFeaturesFromCPU(const Driver &D, StringRef CPU,
                                     std::vector<StringRef> &Features) {
  if (CPU != "generic") {
    llvm::ARM::ArchKind ArchKind = llvm::ARM::parseCPUArch(CPU);
    unsigned Extension = llvm::ARM::getDefaultExtensions(CPU, ArchKind);
    llvm::ARM::getExtensionFeatures(Extension, Features);
  }
}

// Check if -march is valid by checking if it can be canonicalised and parsed.
// getARMArch is used here instead of just checking the -march value in order
// to handle -march=native correctly.
static void checkARMArchName(const Driver &D, const Arg *A, const ArgList &Args,
                             llvm::StringRef ArchName,
                             std::vector<StringRef> &Features,
                             const llvm::Triple &Triple) {
  std::pair<StringRef, StringRef> Split = ArchName.split("+");

  std::string MArch = arm::getARMArch(ArchName, Triple);
  if (llvm::ARM::parseArch(MArch) == llvm::ARM::ArchKind::INVALID ||
      (Split.second.size() && !DecodeARMFeatures(D, Split.second, Features)))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
}

// Check -mcpu=. Needs ArchName to handle -mcpu=generic.
static void checkARMCPUName(const Driver &D, const Arg *A, const ArgList &Args,
                            llvm::StringRef CPUName, llvm::StringRef ArchName,
                            std::vector<StringRef> &Features,
                            const llvm::Triple &Triple) {
  std::pair<StringRef, StringRef> Split = CPUName.split("+");

  std::string CPU = arm::getARMTargetCPU(CPUName, ArchName, Triple);
  if (arm::getLLVMArchSuffixForARM(CPU, ArchName, Triple).empty() ||
      (Split.second.size() && !DecodeARMFeatures(D, Split.second, Features)))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
}

bool arm::useAAPCSForMachO(const llvm::Triple &T) {
  // The backend is hardwired to assume AAPCS for M-class processors, ensure
  // the frontend matches that.
  return T.getEnvironment() == llvm::Triple::EABI ||
         T.getOS() == llvm::Triple::UnknownOS || isARMMProfile(T);
}

// Select mode for reading thread pointer (-mtp=soft/cp15).
arm::ReadTPMode arm::getReadTPMode(const ToolChain &TC, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_mtp_mode_EQ)) {
    const Driver &D = TC.getDriver();
    arm::ReadTPMode ThreadPointer =
        llvm::StringSwitch<arm::ReadTPMode>(A->getValue())
            .Case("cp15", ReadTPMode::Cp15)
            .Case("soft", ReadTPMode::Soft)
            .Default(ReadTPMode::Invalid);
    if (ThreadPointer != ReadTPMode::Invalid)
      return ThreadPointer;
    if (StringRef(A->getValue()).empty())
      D.Diag(diag::err_drv_missing_arg_mtp) << A->getAsString(Args);
    else
      D.Diag(diag::err_drv_invalid_mtp) << A->getAsString(Args);
    return ReadTPMode::Invalid;
  }
  return ReadTPMode::Soft;
}

// Select the float ABI as determined by -msoft-float, -mhard-float, and
// -mfloat-abi=.
arm::FloatABI arm::getARMFloatABI(const ToolChain &TC, const ArgList &Args) {
  const Driver &D = TC.getDriver();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  auto SubArch = getARMSubArchVersionNumber(Triple);
  arm::FloatABI ABI = FloatABI::Invalid;
  if (Arg *A =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(options::OPT_msoft_float)) {
      ABI = FloatABI::Soft;
    } else if (A->getOption().matches(options::OPT_mhard_float)) {
      ABI = FloatABI::Hard;
    } else {
      ABI = llvm::StringSwitch<arm::FloatABI>(A->getValue())
                .Case("soft", FloatABI::Soft)
                .Case("softfp", FloatABI::SoftFP)
                .Case("hard", FloatABI::Hard)
                .Default(FloatABI::Invalid);
      if (ABI == FloatABI::Invalid && !StringRef(A->getValue()).empty()) {
        D.Diag(diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = FloatABI::Soft;
      }
    }

    // It is incorrect to select hard float ABI on MachO platforms if the ABI is
    // "apcs-gnu".
    if (Triple.isOSBinFormatMachO() && !useAAPCSForMachO(Triple) &&
        ABI == FloatABI::Hard) {
      D.Diag(diag::err_drv_unsupported_opt_for_target) << A->getAsString(Args)
                                                       << Triple.getArchName();
    }
  }

  // If unspecified, choose the default based on the platform.
  if (ABI == FloatABI::Invalid) {
    switch (Triple.getOS()) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
    case llvm::Triple::IOS:
    case llvm::Triple::TvOS: {
      // Darwin defaults to "softfp" for v6 and v7.
      ABI = (SubArch == 6 || SubArch == 7) ? FloatABI::SoftFP : FloatABI::Soft;
      ABI = Triple.isWatchABI() ? FloatABI::Hard : ABI;
      break;
    }
    case llvm::Triple::WatchOS:
      ABI = FloatABI::Hard;
      break;

    // FIXME: this is invalid for WindowsCE
    case llvm::Triple::Win32:
      ABI = FloatABI::Hard;
      break;

    case llvm::Triple::NetBSD:
      switch (Triple.getEnvironment()) {
      case llvm::Triple::EABIHF:
      case llvm::Triple::GNUEABIHF:
        ABI = FloatABI::Hard;
        break;
      default:
        ABI = FloatABI::Soft;
        break;
      }
      break;

    case llvm::Triple::FreeBSD:
      switch (Triple.getEnvironment()) {
      case llvm::Triple::GNUEABIHF:
        ABI = FloatABI::Hard;
        break;
      default:
        // FreeBSD defaults to soft float
        ABI = FloatABI::Soft;
        break;
      }
      break;

    case llvm::Triple::OpenBSD:
      ABI = FloatABI::SoftFP;
      break;

    default:
      switch (Triple.getEnvironment()) {
      case llvm::Triple::GNUEABIHF:
      case llvm::Triple::MuslEABIHF:
      case llvm::Triple::EABIHF:
        ABI = FloatABI::Hard;
        break;
      case llvm::Triple::GNUEABI:
      case llvm::Triple::MuslEABI:
      case llvm::Triple::EABI:
        // EABI is always AAPCS, and if it was not marked 'hard', it's softfp
        ABI = FloatABI::SoftFP;
        break;
      case llvm::Triple::Android:
        ABI = (SubArch == 7) ? FloatABI::SoftFP : FloatABI::Soft;
        break;
      default:
        // Assume "soft", but warn the user we are guessing.
        if (Triple.isOSBinFormatMachO() &&
            Triple.getSubArch() == llvm::Triple::ARMSubArch_v7em)
          ABI = FloatABI::Hard;
        else
          ABI = FloatABI::Soft;

        if (Triple.getOS() != llvm::Triple::UnknownOS ||
            !Triple.isOSBinFormatMachO())
          D.Diag(diag::warn_drv_assuming_mfloat_abi_is) << "soft";
        break;
      }
    }
  }

  assert(ABI != FloatABI::Invalid && "must select an ABI");
  return ABI;
}

void arm::getARMTargetFeatures(const ToolChain &TC,
                               const llvm::Triple &Triple,
                               const ArgList &Args,
                               ArgStringList &CmdArgs,
                               std::vector<StringRef> &Features,
                               bool ForAS) {
  const Driver &D = TC.getDriver();

  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);
  arm::FloatABI ABI = arm::getARMFloatABI(TC, Args);
  arm::ReadTPMode ThreadPointer = arm::getReadTPMode(TC, Args);
  const Arg *WaCPU = nullptr, *WaFPU = nullptr;
  const Arg *WaHDiv = nullptr, *WaArch = nullptr;

  if (!ForAS) {
    // FIXME: Note, this is a hack, the LLVM backend doesn't actually use these
    // yet (it uses the -mfloat-abi and -msoft-float options), and it is
    // stripped out by the ARM target. We should probably pass this a new
    // -target-option, which is handled by the -cc1/-cc1as invocation.
    //
    // FIXME2:  For consistency, it would be ideal if we set up the target
    // machine state the same when using the frontend or the assembler. We don't
    // currently do that for the assembler, we pass the options directly to the
    // backend and never even instantiate the frontend TargetInfo. If we did,
    // and used its handleTargetFeatures hook, then we could ensure the
    // assembler and the frontend behave the same.

    // Use software floating point operations?
    if (ABI == arm::FloatABI::Soft)
      Features.push_back("+soft-float");

    // Use software floating point argument passing?
    if (ABI != arm::FloatABI::Hard)
      Features.push_back("+soft-float-abi");
  } else {
    // Here, we make sure that -Wa,-mfpu/cpu/arch/hwdiv will be passed down
    // to the assembler correctly.
    for (const Arg *A :
         Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
      StringRef Value = A->getValue();
      if (Value.startswith("-mfpu=")) {
        WaFPU = A;
      } else if (Value.startswith("-mcpu=")) {
        WaCPU = A;
      } else if (Value.startswith("-mhwdiv=")) {
        WaHDiv = A;
      } else if (Value.startswith("-march=")) {
        WaArch = A;
      }
    }
  }

  if (ThreadPointer == arm::ReadTPMode::Cp15)
    Features.push_back("+read-tp-hard");

  // Check -march. ClangAs gives preference to -Wa,-march=.
  const Arg *ArchArg = Args.getLastArg(options::OPT_march_EQ);
  StringRef ArchName;
  if (WaArch) {
    if (ArchArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << ArchArg->getAsString(Args);
    ArchName = StringRef(WaArch->getValue()).substr(7);
    checkARMArchName(D, WaArch, Args, ArchName, Features, Triple);
    // FIXME: Set Arch.
    D.Diag(clang::diag::warn_drv_unused_argument) << WaArch->getAsString(Args);
  } else if (ArchArg) {
    ArchName = ArchArg->getValue();
    checkARMArchName(D, ArchArg, Args, ArchName, Features, Triple);
  }

  // Check -mcpu. ClangAs gives preference to -Wa,-mcpu=.
  const Arg *CPUArg = Args.getLastArg(options::OPT_mcpu_EQ);
  StringRef CPUName;
  if (WaCPU) {
    if (CPUArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << CPUArg->getAsString(Args);
    CPUName = StringRef(WaCPU->getValue()).substr(6);
    checkARMCPUName(D, WaCPU, Args, CPUName, ArchName, Features, Triple);
  } else if (CPUArg) {
    CPUName = CPUArg->getValue();
    checkARMCPUName(D, CPUArg, Args, CPUName, ArchName, Features, Triple);
  }

  // Add CPU features for generic CPUs
  if (CPUName == "native") {
    llvm::StringMap<bool> HostFeatures;
    if (llvm::sys::getHostCPUFeatures(HostFeatures))
      for (auto &F : HostFeatures)
        Features.push_back(
            Args.MakeArgString((F.second ? "+" : "-") + F.first()));
  } else if (!CPUName.empty()) {
    DecodeARMFeaturesFromCPU(D, CPUName, Features);
  }

  // Honor -mfpu=. ClangAs gives preference to -Wa,-mfpu=.
  const Arg *FPUArg = Args.getLastArg(options::OPT_mfpu_EQ);
  if (WaFPU) {
    if (FPUArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << FPUArg->getAsString(Args);
    getARMFPUFeatures(D, WaFPU, Args, StringRef(WaFPU->getValue()).substr(6),
                      Features);
  } else if (FPUArg) {
    getARMFPUFeatures(D, FPUArg, Args, FPUArg->getValue(), Features);
  } else if (Triple.isAndroid() && getARMSubArchVersionNumber(Triple) >= 7) {
    // Android mandates minimum FPU requirements based on OS version.
    const char *AndroidFPU =
        Triple.isAndroidVersionLT(23) ? "vfpv3-d16" : "neon";
    if (!llvm::ARM::getFPUFeatures(llvm::ARM::parseFPU(AndroidFPU), Features))
      D.Diag(clang::diag::err_drv_clang_unsupported)
          << std::string("-mfpu=") + AndroidFPU;
  }

  // Honor -mhwdiv=. ClangAs gives preference to -Wa,-mhwdiv=.
  const Arg *HDivArg = Args.getLastArg(options::OPT_mhwdiv_EQ);
  if (WaHDiv) {
    if (HDivArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << HDivArg->getAsString(Args);
    getARMHWDivFeatures(D, WaHDiv, Args,
                        StringRef(WaHDiv->getValue()).substr(8), Features);
  } else if (HDivArg)
    getARMHWDivFeatures(D, HDivArg, Args, HDivArg->getValue(), Features);

  // Handle (arch-dependent) fp16fml/fullfp16 relationship.
  // Must happen before any features are disabled due to soft-float.
  // FIXME: this fp16fml option handling will be reimplemented after the
  // TargetParser rewrite.
  const auto ItRNoFullFP16 = std::find(Features.rbegin(), Features.rend(), "-fullfp16");
  const auto ItRFP16FML = std::find(Features.rbegin(), Features.rend(), "+fp16fml");
  if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8_4a) {
    const auto ItRFullFP16  = std::find(Features.rbegin(), Features.rend(), "+fullfp16");
    if (ItRFullFP16 < ItRNoFullFP16 && ItRFullFP16 < ItRFP16FML) {
      // Only entangled feature that can be to the right of this +fullfp16 is -fp16fml.
      // Only append the +fp16fml if there is no -fp16fml after the +fullfp16.
      if (std::find(Features.rbegin(), ItRFullFP16, "-fp16fml") == ItRFullFP16)
        Features.push_back("+fp16fml");
    }
    else
      goto fp16_fml_fallthrough;
  }
  else {
fp16_fml_fallthrough:
    // In both of these cases, putting the 'other' feature on the end of the vector will
    // result in the same effect as placing it immediately after the current feature.
    if (ItRNoFullFP16 < ItRFP16FML)
      Features.push_back("-fp16fml");
    else if (ItRNoFullFP16 > ItRFP16FML)
      Features.push_back("+fullfp16");
  }

  // Setting -msoft-float/-mfloat-abi=soft effectively disables the FPU (GCC
  // ignores the -mfpu options in this case).
  // Note that the ABI can also be set implicitly by the target selected.
  if (ABI == arm::FloatABI::Soft) {
    llvm::ARM::getFPUFeatures(llvm::ARM::FK_NONE, Features);

    // Disable hardware FP features which have been enabled.
    // FIXME: Disabling vfp2 and neon should be enough as all the other
    //        features are dependent on these 2 features in LLVM. However
    //        there is currently no easy way to test this in clang, so for
    //        now just be explicit and disable all known dependent features
    //        as well.
    for (std::string Feature : {"vfp2", "vfp3", "vfp4", "fp-armv8", "fullfp16",
                                "neon", "crypto", "dotprod", "fp16fml"})
      if (std::find(std::begin(Features), std::end(Features), "+" + Feature) != std::end(Features))
        Features.push_back(Args.MakeArgString("-" + Feature));
  }

  // En/disable crc code generation.
  if (Arg *A = Args.getLastArg(options::OPT_mcrc, options::OPT_mnocrc)) {
    if (A->getOption().matches(options::OPT_mcrc))
      Features.push_back("+crc");
    else
      Features.push_back("-crc");
  }

  // For Arch >= ARMv8.0:  crypto = sha2 + aes
  // FIXME: this needs reimplementation after the TargetParser rewrite
  if (ArchName.find_lower("armv8a") != StringRef::npos ||
      ArchName.find_lower("armv8.1a") != StringRef::npos ||
      ArchName.find_lower("armv8.2a") != StringRef::npos ||
      ArchName.find_lower("armv8.3a") != StringRef::npos ||
      ArchName.find_lower("armv8.4a") != StringRef::npos) {
    if (ArchName.find_lower("+crypto") != StringRef::npos) {
      if (ArchName.find_lower("+nosha2") == StringRef::npos)
        Features.push_back("+sha2");
      if (ArchName.find_lower("+noaes") == StringRef::npos)
        Features.push_back("+aes");
    } else if (ArchName.find_lower("-crypto") != StringRef::npos) {
      if (ArchName.find_lower("+sha2") == StringRef::npos)
        Features.push_back("-sha2");
      if (ArchName.find_lower("+aes") == StringRef::npos)
        Features.push_back("-aes");
    }
  }

  // Look for the last occurrence of -mlong-calls or -mno-long-calls. If
  // neither options are specified, see if we are compiling for kernel/kext and
  // decide whether to pass "+long-calls" based on the OS and its version.
  if (Arg *A = Args.getLastArg(options::OPT_mlong_calls,
                               options::OPT_mno_long_calls)) {
    if (A->getOption().matches(options::OPT_mlong_calls))
      Features.push_back("+long-calls");
  } else if (KernelOrKext && (!Triple.isiOS() || Triple.isOSVersionLT(6)) &&
             !Triple.isWatchOS()) {
      Features.push_back("+long-calls");
  }

  // Generate execute-only output (no data access to code sections).
  // This only makes sense for the compiler, not for the assembler.
  if (!ForAS) {
    // Supported only on ARMv6T2 and ARMv7 and above.
    // Cannot be combined with -mno-movt or -mlong-calls
    if (Arg *A = Args.getLastArg(options::OPT_mexecute_only, options::OPT_mno_execute_only)) {
      if (A->getOption().matches(options::OPT_mexecute_only)) {
        if (getARMSubArchVersionNumber(Triple) < 7 &&
            llvm::ARM::parseArch(Triple.getArchName()) != llvm::ARM::ArchKind::ARMV6T2)
              D.Diag(diag::err_target_unsupported_execute_only) << Triple.getArchName();
        else if (Arg *B = Args.getLastArg(options::OPT_mno_movt))
          D.Diag(diag::err_opt_not_valid_with_opt) << A->getAsString(Args) << B->getAsString(Args);
        // Long calls create constant pool entries and have not yet been fixed up
        // to play nicely with execute-only. Hence, they cannot be used in
        // execute-only code for now
        else if (Arg *B = Args.getLastArg(options::OPT_mlong_calls, options::OPT_mno_long_calls)) {
          if (B->getOption().matches(options::OPT_mlong_calls))
            D.Diag(diag::err_opt_not_valid_with_opt) << A->getAsString(Args) << B->getAsString(Args);
        }
        Features.push_back("+execute-only");
      }
    }
  }

  // Kernel code has more strict alignment requirements.
  if (KernelOrKext)
    Features.push_back("+strict-align");
  else if (Arg *A = Args.getLastArg(options::OPT_mno_unaligned_access,
                                    options::OPT_munaligned_access)) {
    if (A->getOption().matches(options::OPT_munaligned_access)) {
      // No v6M core supports unaligned memory access (v6M ARM ARM A3.2).
      if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6m)
        D.Diag(diag::err_target_unsupported_unaligned) << "v6m";
      // v8M Baseline follows on from v6M, so doesn't support unaligned memory
      // access either.
      else if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8m_baseline)
        D.Diag(diag::err_target_unsupported_unaligned) << "v8m.base";
    } else
      Features.push_back("+strict-align");
  } else {
    // Assume pre-ARMv6 doesn't support unaligned accesses.
    //
    // ARMv6 may or may not support unaligned accesses depending on the
    // SCTLR.U bit, which is architecture-specific. We assume ARMv6
    // Darwin and NetBSD targets support unaligned accesses, and others don't.
    //
    // ARMv7 always has SCTLR.U set to 1, but it has a new SCTLR.A bit
    // which raises an alignment fault on unaligned accesses. Linux
    // defaults this bit to 0 and handles it as a system-wide (not
    // per-process) setting. It is therefore safe to assume that ARMv7+
    // Linux targets support unaligned accesses. The same goes for NaCl.
    //
    // The above behavior is consistent with GCC.
    int VersionNum = getARMSubArchVersionNumber(Triple);
    if (Triple.isOSDarwin() || Triple.isOSNetBSD()) {
      if (VersionNum < 6 ||
          Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6m)
        Features.push_back("+strict-align");
    } else if (Triple.isOSLinux() || Triple.isOSNaCl()) {
      if (VersionNum < 7)
        Features.push_back("+strict-align");
    } else
      Features.push_back("+strict-align");
  }

  // llvm does not support reserving registers in general. There is support
  // for reserving r9 on ARM though (defined as a platform-specific register
  // in ARM EABI).
  if (Args.hasArg(options::OPT_ffixed_r9))
    Features.push_back("+reserve-r9");

  // The kext linker doesn't know how to deal with movw/movt.
  if (KernelOrKext || Args.hasArg(options::OPT_mno_movt))
    Features.push_back("+no-movt");

  if (Args.hasArg(options::OPT_mno_neg_immediates))
    Features.push_back("+no-neg-immediates");
}

const std::string arm::getARMArch(StringRef Arch, const llvm::Triple &Triple) {
  std::string MArch;
  if (!Arch.empty())
    MArch = Arch;
  else
    MArch = Triple.getArchName();
  MArch = StringRef(MArch).split("+").first.lower();

  // Handle -march=native.
  if (MArch == "native") {
    std::string CPU = llvm::sys::getHostCPUName();
    if (CPU != "generic") {
      // Translate the native cpu into the architecture suffix for that CPU.
      StringRef Suffix = arm::getLLVMArchSuffixForARM(CPU, MArch, Triple);
      // If there is no valid architecture suffix for this CPU we don't know how
      // to handle it, so return no architecture.
      if (Suffix.empty())
        MArch = "";
      else
        MArch = std::string("arm") + Suffix.str();
    }
  }

  return MArch;
}

/// Get the (LLVM) name of the minimum ARM CPU for the arch we are targeting.
StringRef arm::getARMCPUForMArch(StringRef Arch, const llvm::Triple &Triple) {
  std::string MArch = getARMArch(Arch, Triple);
  // getARMCPUForArch defaults to the triple if MArch is empty, but empty MArch
  // here means an -march=native that we can't handle, so instead return no CPU.
  if (MArch.empty())
    return StringRef();

  // We need to return an empty string here on invalid MArch values as the
  // various places that call this function can't cope with a null result.
  return Triple.getARMCPUForArch(MArch);
}

/// getARMTargetCPU - Get the (LLVM) name of the ARM cpu we are targeting.
std::string arm::getARMTargetCPU(StringRef CPU, StringRef Arch,
                                 const llvm::Triple &Triple) {
  // FIXME: Warn on inconsistent use of -mcpu and -march.
  // If we have -mcpu=, use that.
  if (!CPU.empty()) {
    std::string MCPU = StringRef(CPU).split("+").first.lower();
    // Handle -mcpu=native.
    if (MCPU == "native")
      return llvm::sys::getHostCPUName();
    else
      return MCPU;
  }

  return getARMCPUForMArch(Arch, Triple);
}

/// getLLVMArchSuffixForARM - Get the LLVM arch name to use for a particular
/// CPU  (or Arch, if CPU is generic).
// FIXME: This is redundant with -mcpu, why does LLVM use this.
StringRef arm::getLLVMArchSuffixForARM(StringRef CPU, StringRef Arch,
                                       const llvm::Triple &Triple) {
  llvm::ARM::ArchKind ArchKind;
  if (CPU == "generic") {
    std::string ARMArch = tools::arm::getARMArch(Arch, Triple);
    ArchKind = llvm::ARM::parseArch(ARMArch);
    if (ArchKind == llvm::ARM::ArchKind::INVALID)
      // In case of generic Arch, i.e. "arm",
      // extract arch from default cpu of the Triple
      ArchKind = llvm::ARM::parseCPUArch(Triple.getARMCPUForArch(ARMArch));
  } else {
    // FIXME: horrible hack to get around the fact that Cortex-A7 is only an
    // armv7k triple if it's actually been specified via "-arch armv7k".
    ArchKind = (Arch == "armv7k" || Arch == "thumbv7k")
                          ? llvm::ARM::ArchKind::ARMV7K
                          : llvm::ARM::parseCPUArch(CPU);
  }
  if (ArchKind == llvm::ARM::ArchKind::INVALID)
    return "";
  return llvm::ARM::getSubArch(ArchKind);
}

void arm::appendBE8LinkFlag(const ArgList &Args, ArgStringList &CmdArgs,
                            const llvm::Triple &Triple) {
  if (Args.hasArg(options::OPT_r))
    return;

  // ARMv7 (and later) and ARMv6-M do not support BE-32, so instruct the linker
  // to generate BE-8 executables.
  if (arm::getARMSubArchVersionNumber(Triple) >= 7 || arm::isARMMProfile(Triple))
    CmdArgs.push_back("--be8");
}
