//===--- AArch64.cpp - AArch64 (not ARM) Helpers for Tools ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/TargetParser.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// \returns true if the given triple can determine the default CPU type even
/// if -arch is not specified.
static bool isCPUDeterminedByTriple(const llvm::Triple &Triple) {
  return Triple.isOSDarwin();
}

/// getAArch64TargetCPU - Get the (LLVM) name of the AArch64 cpu we are
/// targeting. Set \p A to the Arg corresponding to the -mcpu argument if it is
/// provided, or to nullptr otherwise.
std::string aarch64::getAArch64TargetCPU(const ArgList &Args,
                                         const llvm::Triple &Triple, Arg *&A) {
  std::string CPU;
  // If we have -mcpu, use that.
  if ((A = Args.getLastArg(options::OPT_mcpu_EQ))) {
    StringRef Mcpu = A->getValue();
    CPU = Mcpu.split("+").first.lower();
  }

  // Handle CPU name is 'native'.
  if (CPU == "native")
    return llvm::sys::getHostCPUName();
  else if (CPU.size())
    return CPU;

  // Make sure we pick "cyclone" if -arch is used or when targetting a Darwin
  // OS.
  if (Args.getLastArg(options::OPT_arch) || Triple.isOSDarwin())
    return "cyclone";

  return "generic";
}

// Decode AArch64 features from string like +[no]featureA+[no]featureB+...
static bool DecodeAArch64Features(const Driver &D, StringRef text,
                                  std::vector<StringRef> &Features) {
  SmallVector<StringRef, 8> Split;
  text.split(Split, StringRef("+"), -1, false);

  for (StringRef Feature : Split) {
    StringRef FeatureName = llvm::AArch64::getArchExtFeature(Feature);
    if (!FeatureName.empty())
      Features.push_back(FeatureName);
    else if (Feature == "neon" || Feature == "noneon")
      D.Diag(clang::diag::err_drv_no_neon_modifier);
    else
      return false;
  }
  return true;
}

// Check if the CPU name and feature modifiers in -mcpu are legal. If yes,
// decode CPU and feature.
static bool DecodeAArch64Mcpu(const Driver &D, StringRef Mcpu, StringRef &CPU,
                              std::vector<StringRef> &Features) {
  std::pair<StringRef, StringRef> Split = Mcpu.split("+");
  CPU = Split.first;

  if (CPU == "native")
    CPU = llvm::sys::getHostCPUName();

  if (CPU == "generic") {
    Features.push_back("+neon");
  } else {
    llvm::AArch64::ArchKind ArchKind = llvm::AArch64::parseCPUArch(CPU);
    if (!llvm::AArch64::getArchFeatures(ArchKind, Features))
      return false;

    unsigned Extension = llvm::AArch64::getDefaultExtensions(CPU, ArchKind);
    if (!llvm::AArch64::getExtensionFeatures(Extension, Features))
      return false;
   }

  if (Split.second.size() && !DecodeAArch64Features(D, Split.second, Features))
    return false;

  return true;
}

static bool
getAArch64ArchFeaturesFromMarch(const Driver &D, StringRef March,
                                const ArgList &Args,
                                std::vector<StringRef> &Features) {
  std::string MarchLowerCase = March.lower();
  std::pair<StringRef, StringRef> Split = StringRef(MarchLowerCase).split("+");

  llvm::AArch64::ArchKind ArchKind = llvm::AArch64::parseArch(Split.first);
  if (ArchKind == llvm::AArch64::ArchKind::INVALID ||
      !llvm::AArch64::getArchFeatures(ArchKind, Features) ||
      (Split.second.size() && !DecodeAArch64Features(D, Split.second, Features)))
    return false;

  return true;
}

static bool
getAArch64ArchFeaturesFromMcpu(const Driver &D, StringRef Mcpu,
                               const ArgList &Args,
                               std::vector<StringRef> &Features) {
  StringRef CPU;
  std::string McpuLowerCase = Mcpu.lower();
  if (!DecodeAArch64Mcpu(D, McpuLowerCase, CPU, Features))
    return false;

  return true;
}

static bool
getAArch64MicroArchFeaturesFromMtune(const Driver &D, StringRef Mtune,
                                     const ArgList &Args,
                                     std::vector<StringRef> &Features) {
  std::string MtuneLowerCase = Mtune.lower();
  // Check CPU name is valid
  std::vector<StringRef> MtuneFeatures;
  StringRef Tune;
  if (!DecodeAArch64Mcpu(D, MtuneLowerCase, Tune, MtuneFeatures))
    return false;

  // Handle CPU name is 'native'.
  if (MtuneLowerCase == "native")
    MtuneLowerCase = llvm::sys::getHostCPUName();
  if (MtuneLowerCase == "cyclone") {
    Features.push_back("+zcm");
    Features.push_back("+zcz");
  }
  return true;
}

static bool
getAArch64MicroArchFeaturesFromMcpu(const Driver &D, StringRef Mcpu,
                                    const ArgList &Args,
                                    std::vector<StringRef> &Features) {
  StringRef CPU;
  std::vector<StringRef> DecodedFeature;
  std::string McpuLowerCase = Mcpu.lower();
  if (!DecodeAArch64Mcpu(D, McpuLowerCase, CPU, DecodedFeature))
    return false;

  return getAArch64MicroArchFeaturesFromMtune(D, CPU, Args, Features);
}

void aarch64::getAArch64TargetFeatures(const Driver &D,
                                       const llvm::Triple &Triple,
                                       const ArgList &Args,
                                       std::vector<StringRef> &Features) {
  Arg *A;
  bool success = true;
  // Enable NEON by default.
  Features.push_back("+neon");
  if ((A = Args.getLastArg(options::OPT_march_EQ)))
    success = getAArch64ArchFeaturesFromMarch(D, A->getValue(), Args, Features);
  else if ((A = Args.getLastArg(options::OPT_mcpu_EQ)))
    success = getAArch64ArchFeaturesFromMcpu(D, A->getValue(), Args, Features);
  else if (Args.hasArg(options::OPT_arch) || isCPUDeterminedByTriple(Triple))
    success = getAArch64ArchFeaturesFromMcpu(
        D, getAArch64TargetCPU(Args, Triple, A), Args, Features);

  if (success && (A = Args.getLastArg(clang::driver::options::OPT_mtune_EQ)))
    success =
        getAArch64MicroArchFeaturesFromMtune(D, A->getValue(), Args, Features);
  else if (success && (A = Args.getLastArg(options::OPT_mcpu_EQ)))
    success =
        getAArch64MicroArchFeaturesFromMcpu(D, A->getValue(), Args, Features);
  else if (success &&
           (Args.hasArg(options::OPT_arch) || isCPUDeterminedByTriple(Triple)))
    success = getAArch64MicroArchFeaturesFromMcpu(
        D, getAArch64TargetCPU(Args, Triple, A), Args, Features);

  if (!success)
    D.Diag(diag::err_drv_clang_unsupported) << A->getAsString(Args);

  if (Args.getLastArg(options::OPT_mgeneral_regs_only)) {
    Features.push_back("-fp-armv8");
    Features.push_back("-crypto");
    Features.push_back("-neon");
  }

  // En/disable crc
  if (Arg *A = Args.getLastArg(options::OPT_mcrc, options::OPT_mnocrc)) {
    if (A->getOption().matches(options::OPT_mcrc))
      Features.push_back("+crc");
    else
      Features.push_back("-crc");
  }

  // Handle (arch-dependent) fp16fml/fullfp16 relationship.
  // FIXME: this fp16fml option handling will be reimplemented after the
  // TargetParser rewrite.
  const auto ItRNoFullFP16 = std::find(Features.rbegin(), Features.rend(), "-fullfp16");
  const auto ItRFP16FML = std::find(Features.rbegin(), Features.rend(), "+fp16fml");
  if (std::find(Features.begin(), Features.end(), "+v8.4a") != Features.end()) {
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

  // FIXME: this needs reimplementation too after the TargetParser rewrite
  //
  // Context sensitive meaning of Crypto:
  // 1) For Arch >= ARMv8.4a:  crypto = sm4 + sha3 + sha2 + aes
  // 2) For Arch <= ARMv8.3a:  crypto = sha2 + aes
  const auto ItBegin = Features.begin();
  const auto ItEnd = Features.end();
  const auto ItRBegin = Features.rbegin();
  const auto ItREnd = Features.rend();
  const auto ItRCrypto = std::find(ItRBegin, ItREnd, "+crypto");
  const auto ItRNoCrypto = std::find(ItRBegin, ItREnd, "-crypto");
  const auto HasCrypto  = ItRCrypto != ItREnd;
  const auto HasNoCrypto = ItRNoCrypto != ItREnd;
  const ptrdiff_t PosCrypto = ItRCrypto - ItRBegin;
  const ptrdiff_t PosNoCrypto = ItRNoCrypto - ItRBegin;

  bool NoCrypto = false;
  if (HasCrypto && HasNoCrypto) {
    if (PosNoCrypto < PosCrypto)
      NoCrypto = true;
  }

  if (std::find(ItBegin, ItEnd, "+v8.4a") != ItEnd) {
    if (HasCrypto && !NoCrypto) {
      // Check if we have NOT disabled an algorithm with something like:
      //   +crypto, -algorithm
      // And if "-algorithm" does not occur, we enable that crypto algorithm.
      const bool HasSM4  = (std::find(ItBegin, ItEnd, "-sm4") == ItEnd);
      const bool HasSHA3 = (std::find(ItBegin, ItEnd, "-sha3") == ItEnd);
      const bool HasSHA2 = (std::find(ItBegin, ItEnd, "-sha2") == ItEnd);
      const bool HasAES  = (std::find(ItBegin, ItEnd, "-aes") == ItEnd);
      if (HasSM4)
        Features.push_back("+sm4");
      if (HasSHA3)
        Features.push_back("+sha3");
      if (HasSHA2)
        Features.push_back("+sha2");
      if (HasAES)
        Features.push_back("+aes");
    } else if (HasNoCrypto) {
      // Check if we have NOT enabled a crypto algorithm with something like:
      //   -crypto, +algorithm
      // And if "+algorithm" does not occur, we disable that crypto algorithm.
      const bool HasSM4  = (std::find(ItBegin, ItEnd, "+sm4") != ItEnd);
      const bool HasSHA3 = (std::find(ItBegin, ItEnd, "+sha3") != ItEnd);
      const bool HasSHA2 = (std::find(ItBegin, ItEnd, "+sha2") != ItEnd);
      const bool HasAES  = (std::find(ItBegin, ItEnd, "+aes") != ItEnd);
      if (!HasSM4)
        Features.push_back("-sm4");
      if (!HasSHA3)
        Features.push_back("-sha3");
      if (!HasSHA2)
        Features.push_back("-sha2");
      if (!HasAES)
        Features.push_back("-aes");
    }
  } else {
    if (HasCrypto && !NoCrypto) {
      const bool HasSHA2 = (std::find(ItBegin, ItEnd, "-sha2") == ItEnd);
      const bool HasAES = (std::find(ItBegin, ItEnd, "-aes") == ItEnd);
      if (HasSHA2)
        Features.push_back("+sha2");
      if (HasAES)
        Features.push_back("+aes");
    } else if (HasNoCrypto) {
      const bool HasSHA2 = (std::find(ItBegin, ItEnd, "+sha2") != ItEnd);
      const bool HasAES  = (std::find(ItBegin, ItEnd, "+aes") != ItEnd);
      const bool HasV82a = (std::find(ItBegin, ItEnd, "+v8.2a") != ItEnd);
      const bool HasV83a = (std::find(ItBegin, ItEnd, "+v8.3a") != ItEnd);
      const bool HasV84a = (std::find(ItBegin, ItEnd, "+v8.4a") != ItEnd);
      if (!HasSHA2)
        Features.push_back("-sha2");
      if (!HasAES)
        Features.push_back("-aes");
      if (HasV82a || HasV83a || HasV84a) {
        Features.push_back("-sm4");
        Features.push_back("-sha3");
      }
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mno_unaligned_access,
                               options::OPT_munaligned_access))
    if (A->getOption().matches(options::OPT_mno_unaligned_access))
      Features.push_back("+strict-align");

  if (Args.hasArg(options::OPT_ffixed_x1))
    Features.push_back("+reserve-x1");

  if (Args.hasArg(options::OPT_ffixed_x2))
    Features.push_back("+reserve-x2");

  if (Args.hasArg(options::OPT_ffixed_x3))
    Features.push_back("+reserve-x3");

  if (Args.hasArg(options::OPT_ffixed_x4))
    Features.push_back("+reserve-x4");

  if (Args.hasArg(options::OPT_ffixed_x5))
    Features.push_back("+reserve-x5");

  if (Args.hasArg(options::OPT_ffixed_x6))
    Features.push_back("+reserve-x6");

  if (Args.hasArg(options::OPT_ffixed_x7))
    Features.push_back("+reserve-x7");

  if (Args.hasArg(options::OPT_ffixed_x18))
    Features.push_back("+reserve-x18");

  if (Args.hasArg(options::OPT_ffixed_x20))
    Features.push_back("+reserve-x20");

  if (Args.hasArg(options::OPT_fcall_saved_x8))
    Features.push_back("+call-saved-x8");

  if (Args.hasArg(options::OPT_fcall_saved_x9))
    Features.push_back("+call-saved-x9");

  if (Args.hasArg(options::OPT_fcall_saved_x10))
    Features.push_back("+call-saved-x10");

  if (Args.hasArg(options::OPT_fcall_saved_x11))
    Features.push_back("+call-saved-x11");

  if (Args.hasArg(options::OPT_fcall_saved_x12))
    Features.push_back("+call-saved-x12");

  if (Args.hasArg(options::OPT_fcall_saved_x13))
    Features.push_back("+call-saved-x13");

  if (Args.hasArg(options::OPT_fcall_saved_x14))
    Features.push_back("+call-saved-x14");

  if (Args.hasArg(options::OPT_fcall_saved_x15))
    Features.push_back("+call-saved-x15");

  if (Args.hasArg(options::OPT_fcall_saved_x18))
    Features.push_back("+call-saved-x18");

  if (Args.hasArg(options::OPT_mno_neg_immediates))
    Features.push_back("+no-neg-immediates");
}
