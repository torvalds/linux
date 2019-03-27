//===-- ARMTargetParser - Parser for ARM target features --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise ARM hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ARMTargetParser.h"
#include "llvm/ADT/StringSwitch.h"
#include <cctype>

using namespace llvm;

static StringRef getHWDivSynonym(StringRef HWDiv) {
  return StringSwitch<StringRef>(HWDiv)
      .Case("thumb,arm", "arm,thumb")
      .Default(HWDiv);
}

// Allows partial match, ex. "v7a" matches "armv7a".
ARM::ArchKind ARM::parseArch(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  StringRef Syn = getArchSynonym(Arch);
  for (const auto A : ARCHNames) {
    if (A.getName().endswith(Syn))
      return A.ID;
  }
  return ArchKind::INVALID;
}

// Version number (ex. v7 = 7).
unsigned ARM::parseArchVersion(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  switch (parseArch(Arch)) {
  case ArchKind::ARMV2:
  case ArchKind::ARMV2A:
    return 2;
  case ArchKind::ARMV3:
  case ArchKind::ARMV3M:
    return 3;
  case ArchKind::ARMV4:
  case ArchKind::ARMV4T:
    return 4;
  case ArchKind::ARMV5T:
  case ArchKind::ARMV5TE:
  case ArchKind::IWMMXT:
  case ArchKind::IWMMXT2:
  case ArchKind::XSCALE:
  case ArchKind::ARMV5TEJ:
    return 5;
  case ArchKind::ARMV6:
  case ArchKind::ARMV6K:
  case ArchKind::ARMV6T2:
  case ArchKind::ARMV6KZ:
  case ArchKind::ARMV6M:
    return 6;
  case ArchKind::ARMV7A:
  case ArchKind::ARMV7VE:
  case ArchKind::ARMV7R:
  case ArchKind::ARMV7M:
  case ArchKind::ARMV7S:
  case ArchKind::ARMV7EM:
  case ArchKind::ARMV7K:
    return 7;
  case ArchKind::ARMV8A:
  case ArchKind::ARMV8_1A:
  case ArchKind::ARMV8_2A:
  case ArchKind::ARMV8_3A:
  case ArchKind::ARMV8_4A:
  case ArchKind::ARMV8_5A:
  case ArchKind::ARMV8R:
  case ArchKind::ARMV8MBaseline:
  case ArchKind::ARMV8MMainline:
    return 8;
  case ArchKind::INVALID:
    return 0;
  }
  llvm_unreachable("Unhandled architecture");
}

// Profile A/R/M
ARM::ProfileKind ARM::parseArchProfile(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  switch (parseArch(Arch)) {
  case ArchKind::ARMV6M:
  case ArchKind::ARMV7M:
  case ArchKind::ARMV7EM:
  case ArchKind::ARMV8MMainline:
  case ArchKind::ARMV8MBaseline:
    return ProfileKind::M;
  case ArchKind::ARMV7R:
  case ArchKind::ARMV8R:
    return ProfileKind::R;
  case ArchKind::ARMV7A:
  case ArchKind::ARMV7VE:
  case ArchKind::ARMV7K:
  case ArchKind::ARMV8A:
  case ArchKind::ARMV8_1A:
  case ArchKind::ARMV8_2A:
  case ArchKind::ARMV8_3A:
  case ArchKind::ARMV8_4A:
  case ArchKind::ARMV8_5A:
    return ProfileKind::A;
  case ArchKind::ARMV2:
  case ArchKind::ARMV2A:
  case ArchKind::ARMV3:
  case ArchKind::ARMV3M:
  case ArchKind::ARMV4:
  case ArchKind::ARMV4T:
  case ArchKind::ARMV5T:
  case ArchKind::ARMV5TE:
  case ArchKind::ARMV5TEJ:
  case ArchKind::ARMV6:
  case ArchKind::ARMV6K:
  case ArchKind::ARMV6T2:
  case ArchKind::ARMV6KZ:
  case ArchKind::ARMV7S:
  case ArchKind::IWMMXT:
  case ArchKind::IWMMXT2:
  case ArchKind::XSCALE:
  case ArchKind::INVALID:
    return ProfileKind::INVALID;
  }
  llvm_unreachable("Unhandled architecture");
}

StringRef ARM::getArchSynonym(StringRef Arch) {
  return StringSwitch<StringRef>(Arch)
      .Case("v5", "v5t")
      .Case("v5e", "v5te")
      .Case("v6j", "v6")
      .Case("v6hl", "v6k")
      .Cases("v6m", "v6sm", "v6s-m", "v6-m")
      .Cases("v6z", "v6zk", "v6kz")
      .Cases("v7", "v7a", "v7hl", "v7l", "v7-a")
      .Case("v7r", "v7-r")
      .Case("v7m", "v7-m")
      .Case("v7em", "v7e-m")
      .Cases("v8", "v8a", "v8l", "aarch64", "arm64", "v8-a")
      .Case("v8.1a", "v8.1-a")
      .Case("v8.2a", "v8.2-a")
      .Case("v8.3a", "v8.3-a")
      .Case("v8.4a", "v8.4-a")
      .Case("v8.5a", "v8.5-a")
      .Case("v8r", "v8-r")
      .Case("v8m.base", "v8-m.base")
      .Case("v8m.main", "v8-m.main")
      .Default(Arch);
}

bool ARM::getFPUFeatures(unsigned FPUKind, std::vector<StringRef> &Features) {

  if (FPUKind >= FK_LAST || FPUKind == FK_INVALID)
    return false;

  // fp-only-sp and d16 subtarget features are independent of each other, so we
  // must enable/disable both.
  switch (FPUNames[FPUKind].Restriction) {
  case FPURestriction::SP_D16:
    Features.push_back("+fp-only-sp");
    Features.push_back("+d16");
    break;
  case FPURestriction::D16:
    Features.push_back("-fp-only-sp");
    Features.push_back("+d16");
    break;
  case FPURestriction::None:
    Features.push_back("-fp-only-sp");
    Features.push_back("-d16");
    break;
  }

  // FPU version subtarget features are inclusive of lower-numbered ones, so
  // enable the one corresponding to this version and disable all that are
  // higher. We also have to make sure to disable fp16 when vfp4 is disabled,
  // as +vfp4 implies +fp16 but -vfp4 does not imply -fp16.
  switch (FPUNames[FPUKind].FPUVer) {
  case FPUVersion::VFPV5:
    Features.push_back("+fp-armv8");
    break;
  case FPUVersion::VFPV4:
    Features.push_back("+vfp4");
    Features.push_back("-fp-armv8");
    break;
  case FPUVersion::VFPV3_FP16:
    Features.push_back("+vfp3");
    Features.push_back("+fp16");
    Features.push_back("-vfp4");
    Features.push_back("-fp-armv8");
    break;
  case FPUVersion::VFPV3:
    Features.push_back("+vfp3");
    Features.push_back("-fp16");
    Features.push_back("-vfp4");
    Features.push_back("-fp-armv8");
    break;
  case FPUVersion::VFPV2:
    Features.push_back("+vfp2");
    Features.push_back("-vfp3");
    Features.push_back("-fp16");
    Features.push_back("-vfp4");
    Features.push_back("-fp-armv8");
    break;
  case FPUVersion::NONE:
    Features.push_back("-vfp2");
    Features.push_back("-vfp3");
    Features.push_back("-fp16");
    Features.push_back("-vfp4");
    Features.push_back("-fp-armv8");
    break;
  }

  // crypto includes neon, so we handle this similarly to FPU version.
  switch (FPUNames[FPUKind].NeonSupport) {
  case NeonSupportLevel::Crypto:
    Features.push_back("+neon");
    Features.push_back("+crypto");
    break;
  case NeonSupportLevel::Neon:
    Features.push_back("+neon");
    Features.push_back("-crypto");
    break;
  case NeonSupportLevel::None:
    Features.push_back("-neon");
    Features.push_back("-crypto");
    break;
  }

  return true;
}

// Little/Big endian
ARM::EndianKind ARM::parseArchEndian(StringRef Arch) {
  if (Arch.startswith("armeb") || Arch.startswith("thumbeb") ||
      Arch.startswith("aarch64_be"))
    return EndianKind::BIG;

  if (Arch.startswith("arm") || Arch.startswith("thumb")) {
    if (Arch.endswith("eb"))
      return EndianKind::BIG;
    else
      return EndianKind::LITTLE;
  }

  if (Arch.startswith("aarch64"))
    return EndianKind::LITTLE;

  return EndianKind::INVALID;
}

// ARM, Thumb, AArch64
ARM::ISAKind ARM::parseArchISA(StringRef Arch) {
  return StringSwitch<ISAKind>(Arch)
      .StartsWith("aarch64", ISAKind::AARCH64)
      .StartsWith("arm64", ISAKind::AARCH64)
      .StartsWith("thumb", ISAKind::THUMB)
      .StartsWith("arm", ISAKind::ARM)
      .Default(ISAKind::INVALID);
}

unsigned ARM::parseFPU(StringRef FPU) {
  StringRef Syn = getFPUSynonym(FPU);
  for (const auto F : FPUNames) {
    if (Syn == F.getName())
      return F.ID;
  }
  return FK_INVALID;
}

ARM::NeonSupportLevel ARM::getFPUNeonSupportLevel(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return NeonSupportLevel::None;
  return FPUNames[FPUKind].NeonSupport;
}

// MArch is expected to be of the form (arm|thumb)?(eb)?(v.+)?(eb)?, but
// (iwmmxt|xscale)(eb)? is also permitted. If the former, return
// "v.+", if the latter, return unmodified string, minus 'eb'.
// If invalid, return empty string.
StringRef ARM::getCanonicalArchName(StringRef Arch) {
  size_t offset = StringRef::npos;
  StringRef A = Arch;
  StringRef Error = "";

  // Begins with "arm" / "thumb", move past it.
  if (A.startswith("arm64"))
    offset = 5;
  else if (A.startswith("arm"))
    offset = 3;
  else if (A.startswith("thumb"))
    offset = 5;
  else if (A.startswith("aarch64")) {
    offset = 7;
    // AArch64 uses "_be", not "eb" suffix.
    if (A.find("eb") != StringRef::npos)
      return Error;
    if (A.substr(offset, 3) == "_be")
      offset += 3;
  }

  // Ex. "armebv7", move past the "eb".
  if (offset != StringRef::npos && A.substr(offset, 2) == "eb")
    offset += 2;
  // Or, if it ends with eb ("armv7eb"), chop it off.
  else if (A.endswith("eb"))
    A = A.substr(0, A.size() - 2);
  // Trim the head
  if (offset != StringRef::npos)
    A = A.substr(offset);

  // Empty string means offset reached the end, which means it's valid.
  if (A.empty())
    return Arch;

  // Only match non-marketing names
  if (offset != StringRef::npos) {
    // Must start with 'vN'.
    if (A.size() >= 2 && (A[0] != 'v' || !std::isdigit(A[1])))
      return Error;
    // Can't have an extra 'eb'.
    if (A.find("eb") != StringRef::npos)
      return Error;
  }

  // Arch will either be a 'v' name (v7a) or a marketing name (xscale).
  return A;
}

StringRef ARM::getFPUSynonym(StringRef FPU) {
  return StringSwitch<StringRef>(FPU)
      .Cases("fpa", "fpe2", "fpe3", "maverick", "invalid") // Unsupported
      .Case("vfp2", "vfpv2")
      .Case("vfp3", "vfpv3")
      .Case("vfp4", "vfpv4")
      .Case("vfp3-d16", "vfpv3-d16")
      .Case("vfp4-d16", "vfpv4-d16")
      .Cases("fp4-sp-d16", "vfpv4-sp-d16", "fpv4-sp-d16")
      .Cases("fp4-dp-d16", "fpv4-dp-d16", "vfpv4-d16")
      .Case("fp5-sp-d16", "fpv5-sp-d16")
      .Cases("fp5-dp-d16", "fpv5-dp-d16", "fpv5-d16")
      // FIXME: Clang uses it, but it's bogus, since neon defaults to vfpv3.
      .Case("neon-vfpv3", "neon")
      .Default(FPU);
}

StringRef ARM::getFPUName(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return StringRef();
  return FPUNames[FPUKind].getName();
}

ARM::FPUVersion ARM::getFPUVersion(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return FPUVersion::NONE;
  return FPUNames[FPUKind].FPUVer;
}

ARM::FPURestriction ARM::getFPURestriction(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return FPURestriction::None;
  return FPUNames[FPUKind].Restriction;
}

unsigned ARM::getDefaultFPU(StringRef CPU, ARM::ArchKind AK) {
  if (CPU == "generic")
    return ARM::ARCHNames[static_cast<unsigned>(AK)].DefaultFPU;

  return StringSwitch<unsigned>(CPU)
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  .Case(NAME, DEFAULT_FPU)
#include "llvm/Support/ARMTargetParser.def"
   .Default(ARM::FK_INVALID);
}

unsigned ARM::getDefaultExtensions(StringRef CPU, ARM::ArchKind AK) {
  if (CPU == "generic")
    return ARM::ARCHNames[static_cast<unsigned>(AK)].ArchBaseExtensions;

  return StringSwitch<unsigned>(CPU)
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  .Case(NAME,                                                                  \
        ARCHNames[static_cast<unsigned>(ArchKind::ID)].ArchBaseExtensions |    \
            DEFAULT_EXT)
#include "llvm/Support/ARMTargetParser.def"
  .Default(ARM::AEK_INVALID);
}

bool ARM::getHWDivFeatures(unsigned HWDivKind,
                           std::vector<StringRef> &Features) {

  if (HWDivKind == AEK_INVALID)
    return false;

  if (HWDivKind & AEK_HWDIVARM)
    Features.push_back("+hwdiv-arm");
  else
    Features.push_back("-hwdiv-arm");

  if (HWDivKind & AEK_HWDIVTHUMB)
    Features.push_back("+hwdiv");
  else
    Features.push_back("-hwdiv");

  return true;
}

bool ARM::getExtensionFeatures(unsigned Extensions,
                               std::vector<StringRef> &Features) {

  if (Extensions == AEK_INVALID)
    return false;

  if (Extensions & AEK_CRC)
    Features.push_back("+crc");
  else
    Features.push_back("-crc");

  if (Extensions & AEK_DSP)
    Features.push_back("+dsp");
  else
    Features.push_back("-dsp");

  if (Extensions & AEK_FP16FML)
    Features.push_back("+fp16fml");
  else
    Features.push_back("-fp16fml");

  if (Extensions & AEK_RAS)
    Features.push_back("+ras");
  else
    Features.push_back("-ras");

  if (Extensions & AEK_DOTPROD)
    Features.push_back("+dotprod");
  else
    Features.push_back("-dotprod");

  return getHWDivFeatures(Extensions, Features);
}

StringRef ARM::getArchName(ARM::ArchKind AK) {
  return ARCHNames[static_cast<unsigned>(AK)].getName();
}

StringRef ARM::getCPUAttr(ARM::ArchKind AK) {
  return ARCHNames[static_cast<unsigned>(AK)].getCPUAttr();
}

StringRef ARM::getSubArch(ARM::ArchKind AK) {
  return ARCHNames[static_cast<unsigned>(AK)].getSubArch();
}

unsigned ARM::getArchAttr(ARM::ArchKind AK) {
  return ARCHNames[static_cast<unsigned>(AK)].ArchAttr;
}

StringRef ARM::getArchExtName(unsigned ArchExtKind) {
  for (const auto AE : ARCHExtNames) {
    if (ArchExtKind == AE.ID)
      return AE.getName();
  }
  return StringRef();
}

StringRef ARM::getArchExtFeature(StringRef ArchExt) {
  if (ArchExt.startswith("no")) {
    StringRef ArchExtBase(ArchExt.substr(2));
    for (const auto AE : ARCHExtNames) {
      if (AE.NegFeature && ArchExtBase == AE.getName())
        return StringRef(AE.NegFeature);
    }
  }
  for (const auto AE : ARCHExtNames) {
    if (AE.Feature && ArchExt == AE.getName())
      return StringRef(AE.Feature);
  }

  return StringRef();
}

StringRef ARM::getHWDivName(unsigned HWDivKind) {
  for (const auto D : HWDivNames) {
    if (HWDivKind == D.ID)
      return D.getName();
  }
  return StringRef();
}

StringRef ARM::getDefaultCPU(StringRef Arch) {
  ArchKind AK = parseArch(Arch);
  if (AK == ArchKind::INVALID)
    return StringRef();

  // Look for multiple AKs to find the default for pair AK+Name.
  for (const auto CPU : CPUNames) {
    if (CPU.ArchID == AK && CPU.Default)
      return CPU.getName();
  }

  // If we can't find a default then target the architecture instead
  return "generic";
}

unsigned ARM::parseHWDiv(StringRef HWDiv) {
  StringRef Syn = getHWDivSynonym(HWDiv);
  for (const auto D : HWDivNames) {
    if (Syn == D.getName())
      return D.ID;
  }
  return AEK_INVALID;
}

unsigned ARM::parseArchExt(StringRef ArchExt) {
  for (const auto A : ARCHExtNames) {
    if (ArchExt == A.getName())
      return A.ID;
  }
  return AEK_INVALID;
}

ARM::ArchKind ARM::parseCPUArch(StringRef CPU) {
  for (const auto C : CPUNames) {
    if (CPU == C.getName())
      return C.ArchID;
  }
  return ArchKind::INVALID;
}

void ARM::fillValidCPUArchList(SmallVectorImpl<StringRef> &Values) {
  for (const CpuNames<ArchKind> &Arch : CPUNames) {
    if (Arch.ArchID != ArchKind::INVALID)
      Values.push_back(Arch.getName());
  }
}

StringRef ARM::computeDefaultTargetABI(const Triple &TT, StringRef CPU) {
  StringRef ArchName =
      CPU.empty() ? TT.getArchName() : getArchName(parseCPUArch(CPU));

  if (TT.isOSBinFormatMachO()) {
    if (TT.getEnvironment() == Triple::EABI ||
        TT.getOS() == Triple::UnknownOS ||
        parseArchProfile(ArchName) == ProfileKind::M)
      return "aapcs";
    if (TT.isWatchABI())
      return "aapcs16";
    return "apcs-gnu";
  } else if (TT.isOSWindows())
    // FIXME: this is invalid for WindowsCE.
    return "aapcs";

  // Select the default based on the platform.
  switch (TT.getEnvironment()) {
  case Triple::Android:
  case Triple::GNUEABI:
  case Triple::GNUEABIHF:
  case Triple::MuslEABI:
  case Triple::MuslEABIHF:
    return "aapcs-linux";
  case Triple::EABIHF:
  case Triple::EABI:
    return "aapcs";
  default:
    if (TT.isOSNetBSD())
      return "apcs-gnu";
    if (TT.isOSOpenBSD())
      return "aapcs-linux";
    return "aapcs";
  }
}
