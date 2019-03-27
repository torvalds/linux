//===--- Triple.cpp - Target triple helper class --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Triple.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetParser.h"
#include <cstring>
using namespace llvm;

StringRef Triple::getArchTypeName(ArchType Kind) {
  switch (Kind) {
  case UnknownArch:    return "unknown";

  case aarch64:        return "aarch64";
  case aarch64_be:     return "aarch64_be";
  case arm:            return "arm";
  case armeb:          return "armeb";
  case arc:            return "arc";
  case avr:            return "avr";
  case bpfel:          return "bpfel";
  case bpfeb:          return "bpfeb";
  case hexagon:        return "hexagon";
  case mips:           return "mips";
  case mipsel:         return "mipsel";
  case mips64:         return "mips64";
  case mips64el:       return "mips64el";
  case msp430:         return "msp430";
  case ppc64:          return "powerpc64";
  case ppc64le:        return "powerpc64le";
  case ppc:            return "powerpc";
  case r600:           return "r600";
  case amdgcn:         return "amdgcn";
  case riscv32:        return "riscv32";
  case riscv64:        return "riscv64";
  case sparc:          return "sparc";
  case sparcv9:        return "sparcv9";
  case sparcel:        return "sparcel";
  case systemz:        return "s390x";
  case tce:            return "tce";
  case tcele:          return "tcele";
  case thumb:          return "thumb";
  case thumbeb:        return "thumbeb";
  case x86:            return "i386";
  case x86_64:         return "x86_64";
  case xcore:          return "xcore";
  case nvptx:          return "nvptx";
  case nvptx64:        return "nvptx64";
  case le32:           return "le32";
  case le64:           return "le64";
  case amdil:          return "amdil";
  case amdil64:        return "amdil64";
  case hsail:          return "hsail";
  case hsail64:        return "hsail64";
  case spir:           return "spir";
  case spir64:         return "spir64";
  case kalimba:        return "kalimba";
  case lanai:          return "lanai";
  case shave:          return "shave";
  case wasm32:         return "wasm32";
  case wasm64:         return "wasm64";
  case renderscript32: return "renderscript32";
  case renderscript64: return "renderscript64";
  }

  llvm_unreachable("Invalid ArchType!");
}

StringRef Triple::getArchTypePrefix(ArchType Kind) {
  switch (Kind) {
  default:
    return StringRef();

  case aarch64:
  case aarch64_be:  return "aarch64";

  case arc:         return "arc";

  case arm:
  case armeb:
  case thumb:
  case thumbeb:     return "arm";

  case avr:         return "avr";

  case ppc64:
  case ppc64le:
  case ppc:         return "ppc";

  case mips:
  case mipsel:
  case mips64:
  case mips64el:    return "mips";

  case hexagon:     return "hexagon";

  case amdgcn:      return "amdgcn";
  case r600:        return "r600";

  case bpfel:
  case bpfeb:       return "bpf";

  case sparcv9:
  case sparcel:
  case sparc:       return "sparc";

  case systemz:     return "s390";

  case x86:
  case x86_64:      return "x86";

  case xcore:       return "xcore";

  // NVPTX intrinsics are namespaced under nvvm.
  case nvptx:       return "nvvm";
  case nvptx64:     return "nvvm";

  case le32:        return "le32";
  case le64:        return "le64";

  case amdil:
  case amdil64:     return "amdil";

  case hsail:
  case hsail64:     return "hsail";

  case spir:
  case spir64:      return "spir";
  case kalimba:     return "kalimba";
  case lanai:       return "lanai";
  case shave:       return "shave";
  case wasm32:
  case wasm64:      return "wasm";

  case riscv32:
  case riscv64:     return "riscv";
  }
}

StringRef Triple::getVendorTypeName(VendorType Kind) {
  switch (Kind) {
  case UnknownVendor: return "unknown";

  case Apple: return "apple";
  case PC: return "pc";
  case SCEI: return "scei";
  case BGP: return "bgp";
  case BGQ: return "bgq";
  case Freescale: return "fsl";
  case IBM: return "ibm";
  case ImaginationTechnologies: return "img";
  case MipsTechnologies: return "mti";
  case NVIDIA: return "nvidia";
  case CSR: return "csr";
  case Myriad: return "myriad";
  case AMD: return "amd";
  case Mesa: return "mesa";
  case SUSE: return "suse";
  case OpenEmbedded: return "oe";
  }

  llvm_unreachable("Invalid VendorType!");
}

StringRef Triple::getOSTypeName(OSType Kind) {
  switch (Kind) {
  case UnknownOS: return "unknown";

  case Ananas: return "ananas";
  case CloudABI: return "cloudabi";
  case Darwin: return "darwin";
  case DragonFly: return "dragonfly";
  case FreeBSD: return "freebsd";
  case Fuchsia: return "fuchsia";
  case IOS: return "ios";
  case KFreeBSD: return "kfreebsd";
  case Linux: return "linux";
  case Lv2: return "lv2";
  case MacOSX: return "macosx";
  case NetBSD: return "netbsd";
  case OpenBSD: return "openbsd";
  case Solaris: return "solaris";
  case Win32: return "windows";
  case Haiku: return "haiku";
  case Minix: return "minix";
  case RTEMS: return "rtems";
  case NaCl: return "nacl";
  case CNK: return "cnk";
  case AIX: return "aix";
  case CUDA: return "cuda";
  case NVCL: return "nvcl";
  case AMDHSA: return "amdhsa";
  case PS4: return "ps4";
  case ELFIAMCU: return "elfiamcu";
  case TvOS: return "tvos";
  case WatchOS: return "watchos";
  case Mesa3D: return "mesa3d";
  case Contiki: return "contiki";
  case AMDPAL: return "amdpal";
  case HermitCore: return "hermit";
  case Hurd: return "hurd";
  case WASI: return "wasi";
  }

  llvm_unreachable("Invalid OSType");
}

StringRef Triple::getEnvironmentTypeName(EnvironmentType Kind) {
  switch (Kind) {
  case UnknownEnvironment: return "unknown";
  case GNU: return "gnu";
  case GNUABIN32: return "gnuabin32";
  case GNUABI64: return "gnuabi64";
  case GNUEABIHF: return "gnueabihf";
  case GNUEABI: return "gnueabi";
  case GNUX32: return "gnux32";
  case CODE16: return "code16";
  case EABI: return "eabi";
  case EABIHF: return "eabihf";
  case Android: return "android";
  case Musl: return "musl";
  case MuslEABI: return "musleabi";
  case MuslEABIHF: return "musleabihf";
  case MSVC: return "msvc";
  case Itanium: return "itanium";
  case Cygnus: return "cygnus";
  case CoreCLR: return "coreclr";
  case Simulator: return "simulator";
  }

  llvm_unreachable("Invalid EnvironmentType!");
}

static Triple::ArchType parseBPFArch(StringRef ArchName) {
  if (ArchName.equals("bpf")) {
    if (sys::IsLittleEndianHost)
      return Triple::bpfel;
    else
      return Triple::bpfeb;
  } else if (ArchName.equals("bpf_be") || ArchName.equals("bpfeb")) {
    return Triple::bpfeb;
  } else if (ArchName.equals("bpf_le") || ArchName.equals("bpfel")) {
    return Triple::bpfel;
  } else {
    return Triple::UnknownArch;
  }
}

Triple::ArchType Triple::getArchTypeForLLVMName(StringRef Name) {
  Triple::ArchType BPFArch(parseBPFArch(Name));
  return StringSwitch<Triple::ArchType>(Name)
    .Case("aarch64", aarch64)
    .Case("aarch64_be", aarch64_be)
    .Case("arc", arc)
    .Case("arm64", aarch64) // "arm64" is an alias for "aarch64"
    .Case("arm", arm)
    .Case("armeb", armeb)
    .Case("avr", avr)
    .StartsWith("bpf", BPFArch)
    .Case("mips", mips)
    .Case("mipsel", mipsel)
    .Case("mips64", mips64)
    .Case("mips64el", mips64el)
    .Case("msp430", msp430)
    .Case("ppc64", ppc64)
    .Case("ppc32", ppc)
    .Case("ppc", ppc)
    .Case("ppc64le", ppc64le)
    .Case("r600", r600)
    .Case("amdgcn", amdgcn)
    .Case("riscv32", riscv32)
    .Case("riscv64", riscv64)
    .Case("hexagon", hexagon)
    .Case("sparc", sparc)
    .Case("sparcel", sparcel)
    .Case("sparcv9", sparcv9)
    .Case("systemz", systemz)
    .Case("tce", tce)
    .Case("tcele", tcele)
    .Case("thumb", thumb)
    .Case("thumbeb", thumbeb)
    .Case("x86", x86)
    .Case("x86-64", x86_64)
    .Case("xcore", xcore)
    .Case("nvptx", nvptx)
    .Case("nvptx64", nvptx64)
    .Case("le32", le32)
    .Case("le64", le64)
    .Case("amdil", amdil)
    .Case("amdil64", amdil64)
    .Case("hsail", hsail)
    .Case("hsail64", hsail64)
    .Case("spir", spir)
    .Case("spir64", spir64)
    .Case("kalimba", kalimba)
    .Case("lanai", lanai)
    .Case("shave", shave)
    .Case("wasm32", wasm32)
    .Case("wasm64", wasm64)
    .Case("renderscript32", renderscript32)
    .Case("renderscript64", renderscript64)
    .Default(UnknownArch);
}

static Triple::ArchType parseARMArch(StringRef ArchName) {
  ARM::ISAKind ISA = ARM::parseArchISA(ArchName);
  ARM::EndianKind ENDIAN = ARM::parseArchEndian(ArchName);

  Triple::ArchType arch = Triple::UnknownArch;
  switch (ENDIAN) {
  case ARM::EndianKind::LITTLE: {
    switch (ISA) {
    case ARM::ISAKind::ARM:
      arch = Triple::arm;
      break;
    case ARM::ISAKind::THUMB:
      arch = Triple::thumb;
      break;
    case ARM::ISAKind::AARCH64:
      arch = Triple::aarch64;
      break;
    case ARM::ISAKind::INVALID:
      break;
    }
    break;
  }
  case ARM::EndianKind::BIG: {
    switch (ISA) {
    case ARM::ISAKind::ARM:
      arch = Triple::armeb;
      break;
    case ARM::ISAKind::THUMB:
      arch = Triple::thumbeb;
      break;
    case ARM::ISAKind::AARCH64:
      arch = Triple::aarch64_be;
      break;
    case ARM::ISAKind::INVALID:
      break;
    }
    break;
  }
  case ARM::EndianKind::INVALID: {
    break;
  }
  }

  ArchName = ARM::getCanonicalArchName(ArchName);
  if (ArchName.empty())
    return Triple::UnknownArch;

  // Thumb only exists in v4+
  if (ISA == ARM::ISAKind::THUMB &&
      (ArchName.startswith("v2") || ArchName.startswith("v3")))
    return Triple::UnknownArch;

  // Thumb only for v6m
  ARM::ProfileKind Profile = ARM::parseArchProfile(ArchName);
  unsigned Version = ARM::parseArchVersion(ArchName);
  if (Profile == ARM::ProfileKind::M && Version == 6) {
    if (ENDIAN == ARM::EndianKind::BIG)
      return Triple::thumbeb;
    else
      return Triple::thumb;
  }

  return arch;
}

static Triple::ArchType parseArch(StringRef ArchName) {
  auto AT = StringSwitch<Triple::ArchType>(ArchName)
    .Cases("i386", "i486", "i586", "i686", Triple::x86)
    // FIXME: Do we need to support these?
    .Cases("i786", "i886", "i986", Triple::x86)
    .Cases("amd64", "x86_64", "x86_64h", Triple::x86_64)
    .Cases("powerpc", "ppc", "ppc32", Triple::ppc)
    .Cases("powerpc64", "ppu", "ppc64", Triple::ppc64)
    .Cases("powerpc64le", "ppc64le", Triple::ppc64le)
    .Case("xscale", Triple::arm)
    .Case("xscaleeb", Triple::armeb)
    .Case("aarch64", Triple::aarch64)
    .Case("aarch64_be", Triple::aarch64_be)
    .Case("arc", Triple::arc)
    .Case("arm64", Triple::aarch64)
    .Case("arm", Triple::arm)
    .Case("armeb", Triple::armeb)
    .Case("thumb", Triple::thumb)
    .Case("thumbeb", Triple::thumbeb)
    .Case("avr", Triple::avr)
    .Case("msp430", Triple::msp430)
    .Cases("mips", "mipseb", "mipsallegrex", "mipsisa32r6",
           "mipsr6", Triple::mips)
    .Cases("mipsel", "mipsallegrexel", "mipsisa32r6el", "mipsr6el",
           Triple::mipsel)
    .Cases("mips64", "mips64eb", "mipsn32", "mipsisa64r6",
           "mips64r6", "mipsn32r6", Triple::mips64)
    .Cases("mips64el", "mipsn32el", "mipsisa64r6el", "mips64r6el",
           "mipsn32r6el", Triple::mips64el)
    .Case("r600", Triple::r600)
    .Case("amdgcn", Triple::amdgcn)
    .Case("riscv32", Triple::riscv32)
    .Case("riscv64", Triple::riscv64)
    .Case("hexagon", Triple::hexagon)
    .Cases("s390x", "systemz", Triple::systemz)
    .Case("sparc", Triple::sparc)
    .Case("sparcel", Triple::sparcel)
    .Cases("sparcv9", "sparc64", Triple::sparcv9)
    .Case("tce", Triple::tce)
    .Case("tcele", Triple::tcele)
    .Case("xcore", Triple::xcore)
    .Case("nvptx", Triple::nvptx)
    .Case("nvptx64", Triple::nvptx64)
    .Case("le32", Triple::le32)
    .Case("le64", Triple::le64)
    .Case("amdil", Triple::amdil)
    .Case("amdil64", Triple::amdil64)
    .Case("hsail", Triple::hsail)
    .Case("hsail64", Triple::hsail64)
    .Case("spir", Triple::spir)
    .Case("spir64", Triple::spir64)
    .StartsWith("kalimba", Triple::kalimba)
    .Case("lanai", Triple::lanai)
    .Case("shave", Triple::shave)
    .Case("wasm32", Triple::wasm32)
    .Case("wasm64", Triple::wasm64)
    .Case("renderscript32", Triple::renderscript32)
    .Case("renderscript64", Triple::renderscript64)
    .Default(Triple::UnknownArch);

  // Some architectures require special parsing logic just to compute the
  // ArchType result.
  if (AT == Triple::UnknownArch) {
    if (ArchName.startswith("arm") || ArchName.startswith("thumb") ||
        ArchName.startswith("aarch64"))
      return parseARMArch(ArchName);
    if (ArchName.startswith("bpf"))
      return parseBPFArch(ArchName);
  }

  return AT;
}

static Triple::VendorType parseVendor(StringRef VendorName) {
  return StringSwitch<Triple::VendorType>(VendorName)
    .Case("apple", Triple::Apple)
    .Case("pc", Triple::PC)
    .Case("scei", Triple::SCEI)
    .Case("bgp", Triple::BGP)
    .Case("bgq", Triple::BGQ)
    .Case("fsl", Triple::Freescale)
    .Case("ibm", Triple::IBM)
    .Case("img", Triple::ImaginationTechnologies)
    .Case("mti", Triple::MipsTechnologies)
    .Case("nvidia", Triple::NVIDIA)
    .Case("csr", Triple::CSR)
    .Case("myriad", Triple::Myriad)
    .Case("amd", Triple::AMD)
    .Case("mesa", Triple::Mesa)
    .Case("suse", Triple::SUSE)
    .Case("oe", Triple::OpenEmbedded)
    .Default(Triple::UnknownVendor);
}

static Triple::OSType parseOS(StringRef OSName) {
  return StringSwitch<Triple::OSType>(OSName)
    .StartsWith("ananas", Triple::Ananas)
    .StartsWith("cloudabi", Triple::CloudABI)
    .StartsWith("darwin", Triple::Darwin)
    .StartsWith("dragonfly", Triple::DragonFly)
    .StartsWith("freebsd", Triple::FreeBSD)
    .StartsWith("fuchsia", Triple::Fuchsia)
    .StartsWith("ios", Triple::IOS)
    .StartsWith("kfreebsd", Triple::KFreeBSD)
    .StartsWith("linux", Triple::Linux)
    .StartsWith("lv2", Triple::Lv2)
    .StartsWith("macos", Triple::MacOSX)
    .StartsWith("netbsd", Triple::NetBSD)
    .StartsWith("openbsd", Triple::OpenBSD)
    .StartsWith("solaris", Triple::Solaris)
    .StartsWith("win32", Triple::Win32)
    .StartsWith("windows", Triple::Win32)
    .StartsWith("haiku", Triple::Haiku)
    .StartsWith("minix", Triple::Minix)
    .StartsWith("rtems", Triple::RTEMS)
    .StartsWith("nacl", Triple::NaCl)
    .StartsWith("cnk", Triple::CNK)
    .StartsWith("aix", Triple::AIX)
    .StartsWith("cuda", Triple::CUDA)
    .StartsWith("nvcl", Triple::NVCL)
    .StartsWith("amdhsa", Triple::AMDHSA)
    .StartsWith("ps4", Triple::PS4)
    .StartsWith("elfiamcu", Triple::ELFIAMCU)
    .StartsWith("tvos", Triple::TvOS)
    .StartsWith("watchos", Triple::WatchOS)
    .StartsWith("mesa3d", Triple::Mesa3D)
    .StartsWith("contiki", Triple::Contiki)
    .StartsWith("amdpal", Triple::AMDPAL)
    .StartsWith("hermit", Triple::HermitCore)
    .StartsWith("hurd", Triple::Hurd)
    .StartsWith("wasi", Triple::WASI)
    .Default(Triple::UnknownOS);
}

static Triple::EnvironmentType parseEnvironment(StringRef EnvironmentName) {
  return StringSwitch<Triple::EnvironmentType>(EnvironmentName)
    .StartsWith("eabihf", Triple::EABIHF)
    .StartsWith("eabi", Triple::EABI)
    .StartsWith("gnuabin32", Triple::GNUABIN32)
    .StartsWith("gnuabi64", Triple::GNUABI64)
    .StartsWith("gnueabihf", Triple::GNUEABIHF)
    .StartsWith("gnueabi", Triple::GNUEABI)
    .StartsWith("gnux32", Triple::GNUX32)
    .StartsWith("code16", Triple::CODE16)
    .StartsWith("gnu", Triple::GNU)
    .StartsWith("android", Triple::Android)
    .StartsWith("musleabihf", Triple::MuslEABIHF)
    .StartsWith("musleabi", Triple::MuslEABI)
    .StartsWith("musl", Triple::Musl)
    .StartsWith("msvc", Triple::MSVC)
    .StartsWith("itanium", Triple::Itanium)
    .StartsWith("cygnus", Triple::Cygnus)
    .StartsWith("coreclr", Triple::CoreCLR)
    .StartsWith("simulator", Triple::Simulator)
    .Default(Triple::UnknownEnvironment);
}

static Triple::ObjectFormatType parseFormat(StringRef EnvironmentName) {
  return StringSwitch<Triple::ObjectFormatType>(EnvironmentName)
    .EndsWith("coff", Triple::COFF)
    .EndsWith("elf", Triple::ELF)
    .EndsWith("macho", Triple::MachO)
    .EndsWith("wasm", Triple::Wasm)
    .Default(Triple::UnknownObjectFormat);
}

static Triple::SubArchType parseSubArch(StringRef SubArchName) {
  if (SubArchName.startswith("mips") &&
      (SubArchName.endswith("r6el") || SubArchName.endswith("r6")))
    return Triple::MipsSubArch_r6;

  StringRef ARMSubArch = ARM::getCanonicalArchName(SubArchName);

  // For now, this is the small part. Early return.
  if (ARMSubArch.empty())
    return StringSwitch<Triple::SubArchType>(SubArchName)
      .EndsWith("kalimba3", Triple::KalimbaSubArch_v3)
      .EndsWith("kalimba4", Triple::KalimbaSubArch_v4)
      .EndsWith("kalimba5", Triple::KalimbaSubArch_v5)
      .Default(Triple::NoSubArch);

  // ARM sub arch.
  switch(ARM::parseArch(ARMSubArch)) {
  case ARM::ArchKind::ARMV4:
    return Triple::NoSubArch;
  case ARM::ArchKind::ARMV4T:
    return Triple::ARMSubArch_v4t;
  case ARM::ArchKind::ARMV5T:
    return Triple::ARMSubArch_v5;
  case ARM::ArchKind::ARMV5TE:
  case ARM::ArchKind::IWMMXT:
  case ARM::ArchKind::IWMMXT2:
  case ARM::ArchKind::XSCALE:
  case ARM::ArchKind::ARMV5TEJ:
    return Triple::ARMSubArch_v5te;
  case ARM::ArchKind::ARMV6:
    return Triple::ARMSubArch_v6;
  case ARM::ArchKind::ARMV6K:
  case ARM::ArchKind::ARMV6KZ:
    return Triple::ARMSubArch_v6k;
  case ARM::ArchKind::ARMV6T2:
    return Triple::ARMSubArch_v6t2;
  case ARM::ArchKind::ARMV6M:
    return Triple::ARMSubArch_v6m;
  case ARM::ArchKind::ARMV7A:
  case ARM::ArchKind::ARMV7R:
    return Triple::ARMSubArch_v7;
  case ARM::ArchKind::ARMV7VE:
    return Triple::ARMSubArch_v7ve;
  case ARM::ArchKind::ARMV7K:
    return Triple::ARMSubArch_v7k;
  case ARM::ArchKind::ARMV7M:
    return Triple::ARMSubArch_v7m;
  case ARM::ArchKind::ARMV7S:
    return Triple::ARMSubArch_v7s;
  case ARM::ArchKind::ARMV7EM:
    return Triple::ARMSubArch_v7em;
  case ARM::ArchKind::ARMV8A:
    return Triple::ARMSubArch_v8;
  case ARM::ArchKind::ARMV8_1A:
    return Triple::ARMSubArch_v8_1a;
  case ARM::ArchKind::ARMV8_2A:
    return Triple::ARMSubArch_v8_2a;
  case ARM::ArchKind::ARMV8_3A:
    return Triple::ARMSubArch_v8_3a;
  case ARM::ArchKind::ARMV8_4A:
    return Triple::ARMSubArch_v8_4a;
  case ARM::ArchKind::ARMV8_5A:
    return Triple::ARMSubArch_v8_5a;
  case ARM::ArchKind::ARMV8R:
    return Triple::ARMSubArch_v8r;
  case ARM::ArchKind::ARMV8MBaseline:
    return Triple::ARMSubArch_v8m_baseline;
  case ARM::ArchKind::ARMV8MMainline:
    return Triple::ARMSubArch_v8m_mainline;
  default:
    return Triple::NoSubArch;
  }
}

static StringRef getObjectFormatTypeName(Triple::ObjectFormatType Kind) {
  switch (Kind) {
  case Triple::UnknownObjectFormat: return "";
  case Triple::COFF: return "coff";
  case Triple::ELF: return "elf";
  case Triple::MachO: return "macho";
  case Triple::Wasm: return "wasm";
  }
  llvm_unreachable("unknown object format type");
}

static Triple::ObjectFormatType getDefaultFormat(const Triple &T) {
  switch (T.getArch()) {
  case Triple::UnknownArch:
  case Triple::aarch64:
  case Triple::arm:
  case Triple::thumb:
  case Triple::x86:
  case Triple::x86_64:
    if (T.isOSDarwin())
      return Triple::MachO;
    else if (T.isOSWindows())
      return Triple::COFF;
    return Triple::ELF;

  case Triple::aarch64_be:
  case Triple::arc:
  case Triple::amdgcn:
  case Triple::amdil:
  case Triple::amdil64:
  case Triple::armeb:
  case Triple::avr:
  case Triple::bpfeb:
  case Triple::bpfel:
  case Triple::hexagon:
  case Triple::lanai:
  case Triple::hsail:
  case Triple::hsail64:
  case Triple::kalimba:
  case Triple::le32:
  case Triple::le64:
  case Triple::mips:
  case Triple::mips64:
  case Triple::mips64el:
  case Triple::mipsel:
  case Triple::msp430:
  case Triple::nvptx:
  case Triple::nvptx64:
  case Triple::ppc64le:
  case Triple::r600:
  case Triple::renderscript32:
  case Triple::renderscript64:
  case Triple::riscv32:
  case Triple::riscv64:
  case Triple::shave:
  case Triple::sparc:
  case Triple::sparcel:
  case Triple::sparcv9:
  case Triple::spir:
  case Triple::spir64:
  case Triple::systemz:
  case Triple::tce:
  case Triple::tcele:
  case Triple::thumbeb:
  case Triple::xcore:
    return Triple::ELF;

  case Triple::ppc:
  case Triple::ppc64:
    if (T.isOSDarwin())
      return Triple::MachO;
    return Triple::ELF;

  case Triple::wasm32:
  case Triple::wasm64:
    return Triple::Wasm;
  }
  llvm_unreachable("unknown architecture");
}

/// Construct a triple from the string representation provided.
///
/// This stores the string representation and parses the various pieces into
/// enum members.
Triple::Triple(const Twine &Str)
    : Data(Str.str()), Arch(UnknownArch), SubArch(NoSubArch),
      Vendor(UnknownVendor), OS(UnknownOS), Environment(UnknownEnvironment),
      ObjectFormat(UnknownObjectFormat) {
  // Do minimal parsing by hand here.
  SmallVector<StringRef, 4> Components;
  StringRef(Data).split(Components, '-', /*MaxSplit*/ 3);
  if (Components.size() > 0) {
    Arch = parseArch(Components[0]);
    SubArch = parseSubArch(Components[0]);
    if (Components.size() > 1) {
      Vendor = parseVendor(Components[1]);
      if (Components.size() > 2) {
        OS = parseOS(Components[2]);
        if (Components.size() > 3) {
          Environment = parseEnvironment(Components[3]);
          ObjectFormat = parseFormat(Components[3]);
        }
      }
    } else {
      Environment =
          StringSwitch<Triple::EnvironmentType>(Components[0])
              .StartsWith("mipsn32", Triple::GNUABIN32)
              .StartsWith("mips64", Triple::GNUABI64)
              .StartsWith("mipsisa64", Triple::GNUABI64)
              .StartsWith("mipsisa32", Triple::GNU)
              .Cases("mips", "mipsel", "mipsr6", "mipsr6el", Triple::GNU)
              .Default(UnknownEnvironment);
    }
  }
  if (ObjectFormat == UnknownObjectFormat)
    ObjectFormat = getDefaultFormat(*this);
}

/// Construct a triple from string representations of the architecture,
/// vendor, and OS.
///
/// This joins each argument into a canonical string representation and parses
/// them into enum members. It leaves the environment unknown and omits it from
/// the string representation.
Triple::Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr)
    : Data((ArchStr + Twine('-') + VendorStr + Twine('-') + OSStr).str()),
      Arch(parseArch(ArchStr.str())),
      SubArch(parseSubArch(ArchStr.str())),
      Vendor(parseVendor(VendorStr.str())),
      OS(parseOS(OSStr.str())),
      Environment(), ObjectFormat(Triple::UnknownObjectFormat) {
  ObjectFormat = getDefaultFormat(*this);
}

/// Construct a triple from string representations of the architecture,
/// vendor, OS, and environment.
///
/// This joins each argument into a canonical string representation and parses
/// them into enum members.
Triple::Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr,
               const Twine &EnvironmentStr)
    : Data((ArchStr + Twine('-') + VendorStr + Twine('-') + OSStr + Twine('-') +
            EnvironmentStr).str()),
      Arch(parseArch(ArchStr.str())),
      SubArch(parseSubArch(ArchStr.str())),
      Vendor(parseVendor(VendorStr.str())),
      OS(parseOS(OSStr.str())),
      Environment(parseEnvironment(EnvironmentStr.str())),
      ObjectFormat(parseFormat(EnvironmentStr.str())) {
  if (ObjectFormat == Triple::UnknownObjectFormat)
    ObjectFormat = getDefaultFormat(*this);
}

std::string Triple::normalize(StringRef Str) {
  bool IsMinGW32 = false;
  bool IsCygwin = false;

  // Parse into components.
  SmallVector<StringRef, 4> Components;
  Str.split(Components, '-');

  // If the first component corresponds to a known architecture, preferentially
  // use it for the architecture.  If the second component corresponds to a
  // known vendor, preferentially use it for the vendor, etc.  This avoids silly
  // component movement when a component parses as (eg) both a valid arch and a
  // valid os.
  ArchType Arch = UnknownArch;
  if (Components.size() > 0)
    Arch = parseArch(Components[0]);
  VendorType Vendor = UnknownVendor;
  if (Components.size() > 1)
    Vendor = parseVendor(Components[1]);
  OSType OS = UnknownOS;
  if (Components.size() > 2) {
    OS = parseOS(Components[2]);
    IsCygwin = Components[2].startswith("cygwin");
    IsMinGW32 = Components[2].startswith("mingw");
  }
  EnvironmentType Environment = UnknownEnvironment;
  if (Components.size() > 3)
    Environment = parseEnvironment(Components[3]);
  ObjectFormatType ObjectFormat = UnknownObjectFormat;
  if (Components.size() > 4)
    ObjectFormat = parseFormat(Components[4]);

  // Note which components are already in their final position.  These will not
  // be moved.
  bool Found[4];
  Found[0] = Arch != UnknownArch;
  Found[1] = Vendor != UnknownVendor;
  Found[2] = OS != UnknownOS;
  Found[3] = Environment != UnknownEnvironment;

  // If they are not there already, permute the components into their canonical
  // positions by seeing if they parse as a valid architecture, and if so moving
  // the component to the architecture position etc.
  for (unsigned Pos = 0; Pos != array_lengthof(Found); ++Pos) {
    if (Found[Pos])
      continue; // Already in the canonical position.

    for (unsigned Idx = 0; Idx != Components.size(); ++Idx) {
      // Do not reparse any components that already matched.
      if (Idx < array_lengthof(Found) && Found[Idx])
        continue;

      // Does this component parse as valid for the target position?
      bool Valid = false;
      StringRef Comp = Components[Idx];
      switch (Pos) {
      default: llvm_unreachable("unexpected component type!");
      case 0:
        Arch = parseArch(Comp);
        Valid = Arch != UnknownArch;
        break;
      case 1:
        Vendor = parseVendor(Comp);
        Valid = Vendor != UnknownVendor;
        break;
      case 2:
        OS = parseOS(Comp);
        IsCygwin = Comp.startswith("cygwin");
        IsMinGW32 = Comp.startswith("mingw");
        Valid = OS != UnknownOS || IsCygwin || IsMinGW32;
        break;
      case 3:
        Environment = parseEnvironment(Comp);
        Valid = Environment != UnknownEnvironment;
        if (!Valid) {
          ObjectFormat = parseFormat(Comp);
          Valid = ObjectFormat != UnknownObjectFormat;
        }
        break;
      }
      if (!Valid)
        continue; // Nope, try the next component.

      // Move the component to the target position, pushing any non-fixed
      // components that are in the way to the right.  This tends to give
      // good results in the common cases of a forgotten vendor component
      // or a wrongly positioned environment.
      if (Pos < Idx) {
        // Insert left, pushing the existing components to the right.  For
        // example, a-b-i386 -> i386-a-b when moving i386 to the front.
        StringRef CurrentComponent(""); // The empty component.
        // Replace the component we are moving with an empty component.
        std::swap(CurrentComponent, Components[Idx]);
        // Insert the component being moved at Pos, displacing any existing
        // components to the right.
        for (unsigned i = Pos; !CurrentComponent.empty(); ++i) {
          // Skip over any fixed components.
          while (i < array_lengthof(Found) && Found[i])
            ++i;
          // Place the component at the new position, getting the component
          // that was at this position - it will be moved right.
          std::swap(CurrentComponent, Components[i]);
        }
      } else if (Pos > Idx) {
        // Push right by inserting empty components until the component at Idx
        // reaches the target position Pos.  For example, pc-a -> -pc-a when
        // moving pc to the second position.
        do {
          // Insert one empty component at Idx.
          StringRef CurrentComponent(""); // The empty component.
          for (unsigned i = Idx; i < Components.size();) {
            // Place the component at the new position, getting the component
            // that was at this position - it will be moved right.
            std::swap(CurrentComponent, Components[i]);
            // If it was placed on top of an empty component then we are done.
            if (CurrentComponent.empty())
              break;
            // Advance to the next component, skipping any fixed components.
            while (++i < array_lengthof(Found) && Found[i])
              ;
          }
          // The last component was pushed off the end - append it.
          if (!CurrentComponent.empty())
            Components.push_back(CurrentComponent);

          // Advance Idx to the component's new position.
          while (++Idx < array_lengthof(Found) && Found[Idx])
            ;
        } while (Idx < Pos); // Add more until the final position is reached.
      }
      assert(Pos < Components.size() && Components[Pos] == Comp &&
             "Component moved wrong!");
      Found[Pos] = true;
      break;
    }
  }

  // Replace empty components with "unknown" value.
  for (unsigned i = 0, e = Components.size(); i < e; ++i) {
    if (Components[i].empty())
      Components[i] = "unknown";
  }

  // Special case logic goes here.  At this point Arch, Vendor and OS have the
  // correct values for the computed components.
  std::string NormalizedEnvironment;
  if (Environment == Triple::Android && Components[3].startswith("androideabi")) {
    StringRef AndroidVersion = Components[3].drop_front(strlen("androideabi"));
    if (AndroidVersion.empty()) {
      Components[3] = "android";
    } else {
      NormalizedEnvironment = Twine("android", AndroidVersion).str();
      Components[3] = NormalizedEnvironment;
    }
  }

  // SUSE uses "gnueabi" to mean "gnueabihf"
  if (Vendor == Triple::SUSE && Environment == llvm::Triple::GNUEABI)
    Components[3] = "gnueabihf";

  if (OS == Triple::Win32) {
    Components.resize(4);
    Components[2] = "windows";
    if (Environment == UnknownEnvironment) {
      if (ObjectFormat == UnknownObjectFormat || ObjectFormat == Triple::COFF)
        Components[3] = "msvc";
      else
        Components[3] = getObjectFormatTypeName(ObjectFormat);
    }
  } else if (IsMinGW32) {
    Components.resize(4);
    Components[2] = "windows";
    Components[3] = "gnu";
  } else if (IsCygwin) {
    Components.resize(4);
    Components[2] = "windows";
    Components[3] = "cygnus";
  }
  if (IsMinGW32 || IsCygwin ||
      (OS == Triple::Win32 && Environment != UnknownEnvironment)) {
    if (ObjectFormat != UnknownObjectFormat && ObjectFormat != Triple::COFF) {
      Components.resize(5);
      Components[4] = getObjectFormatTypeName(ObjectFormat);
    }
  }

  // Stick the corrected components back together to form the normalized string.
  std::string Normalized;
  for (unsigned i = 0, e = Components.size(); i != e; ++i) {
    if (i) Normalized += '-';
    Normalized += Components[i];
  }
  return Normalized;
}

StringRef Triple::getArchName() const {
  return StringRef(Data).split('-').first;           // Isolate first component
}

StringRef Triple::getVendorName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  return Tmp.split('-').first;                       // Isolate second component
}

StringRef Triple::getOSName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  Tmp = Tmp.split('-').second;                       // Strip second component
  return Tmp.split('-').first;                       // Isolate third component
}

StringRef Triple::getEnvironmentName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  Tmp = Tmp.split('-').second;                       // Strip second component
  return Tmp.split('-').second;                      // Strip third component
}

StringRef Triple::getOSAndEnvironmentName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  return Tmp.split('-').second;                      // Strip second component
}

static unsigned EatNumber(StringRef &Str) {
  assert(!Str.empty() && Str[0] >= '0' && Str[0] <= '9' && "Not a number");
  unsigned Result = 0;

  do {
    // Consume the leading digit.
    Result = Result*10 + (Str[0] - '0');

    // Eat the digit.
    Str = Str.substr(1);
  } while (!Str.empty() && Str[0] >= '0' && Str[0] <= '9');

  return Result;
}

static void parseVersionFromName(StringRef Name, unsigned &Major,
                                 unsigned &Minor, unsigned &Micro) {
  // Any unset version defaults to 0.
  Major = Minor = Micro = 0;

  // Parse up to three components.
  unsigned *Components[3] = {&Major, &Minor, &Micro};
  for (unsigned i = 0; i != 3; ++i) {
    if (Name.empty() || Name[0] < '0' || Name[0] > '9')
      break;

    // Consume the leading number.
    *Components[i] = EatNumber(Name);

    // Consume the separator, if present.
    if (Name.startswith("."))
      Name = Name.substr(1);
  }
}

void Triple::getEnvironmentVersion(unsigned &Major, unsigned &Minor,
                                   unsigned &Micro) const {
  StringRef EnvironmentName = getEnvironmentName();
  StringRef EnvironmentTypeName = getEnvironmentTypeName(getEnvironment());
  if (EnvironmentName.startswith(EnvironmentTypeName))
    EnvironmentName = EnvironmentName.substr(EnvironmentTypeName.size());

  parseVersionFromName(EnvironmentName, Major, Minor, Micro);
}

void Triple::getOSVersion(unsigned &Major, unsigned &Minor,
                          unsigned &Micro) const {
  StringRef OSName = getOSName();
  // Assume that the OS portion of the triple starts with the canonical name.
  StringRef OSTypeName = getOSTypeName(getOS());
  if (OSName.startswith(OSTypeName))
    OSName = OSName.substr(OSTypeName.size());
  else if (getOS() == MacOSX)
    OSName.consume_front("macos");

  parseVersionFromName(OSName, Major, Minor, Micro);
}

bool Triple::getMacOSXVersion(unsigned &Major, unsigned &Minor,
                              unsigned &Micro) const {
  getOSVersion(Major, Minor, Micro);

  switch (getOS()) {
  default: llvm_unreachable("unexpected OS for Darwin triple");
  case Darwin:
    // Default to darwin8, i.e., MacOSX 10.4.
    if (Major == 0)
      Major = 8;
    // Darwin version numbers are skewed from OS X versions.
    if (Major < 4)
      return false;
    Micro = 0;
    Minor = Major - 4;
    Major = 10;
    break;
  case MacOSX:
    // Default to 10.4.
    if (Major == 0) {
      Major = 10;
      Minor = 4;
    }
    if (Major != 10)
      return false;
    break;
  case IOS:
  case TvOS:
  case WatchOS:
    // Ignore the version from the triple.  This is only handled because the
    // the clang driver combines OS X and IOS support into a common Darwin
    // toolchain that wants to know the OS X version number even when targeting
    // IOS.
    Major = 10;
    Minor = 4;
    Micro = 0;
    break;
  }
  return true;
}

void Triple::getiOSVersion(unsigned &Major, unsigned &Minor,
                           unsigned &Micro) const {
  switch (getOS()) {
  default: llvm_unreachable("unexpected OS for Darwin triple");
  case Darwin:
  case MacOSX:
    // Ignore the version from the triple.  This is only handled because the
    // the clang driver combines OS X and IOS support into a common Darwin
    // toolchain that wants to know the iOS version number even when targeting
    // OS X.
    Major = 5;
    Minor = 0;
    Micro = 0;
    break;
  case IOS:
  case TvOS:
    getOSVersion(Major, Minor, Micro);
    // Default to 5.0 (or 7.0 for arm64).
    if (Major == 0)
      Major = (getArch() == aarch64) ? 7 : 5;
    break;
  case WatchOS:
    llvm_unreachable("conflicting triple info");
  }
}

void Triple::getWatchOSVersion(unsigned &Major, unsigned &Minor,
                               unsigned &Micro) const {
  switch (getOS()) {
  default: llvm_unreachable("unexpected OS for Darwin triple");
  case Darwin:
  case MacOSX:
    // Ignore the version from the triple.  This is only handled because the
    // the clang driver combines OS X and IOS support into a common Darwin
    // toolchain that wants to know the iOS version number even when targeting
    // OS X.
    Major = 2;
    Minor = 0;
    Micro = 0;
    break;
  case WatchOS:
    getOSVersion(Major, Minor, Micro);
    if (Major == 0)
      Major = 2;
    break;
  case IOS:
    llvm_unreachable("conflicting triple info");
  }
}

void Triple::setTriple(const Twine &Str) {
  *this = Triple(Str);
}

void Triple::setArch(ArchType Kind) {
  setArchName(getArchTypeName(Kind));
}

void Triple::setVendor(VendorType Kind) {
  setVendorName(getVendorTypeName(Kind));
}

void Triple::setOS(OSType Kind) {
  setOSName(getOSTypeName(Kind));
}

void Triple::setEnvironment(EnvironmentType Kind) {
  if (ObjectFormat == getDefaultFormat(*this))
    return setEnvironmentName(getEnvironmentTypeName(Kind));

  setEnvironmentName((getEnvironmentTypeName(Kind) + Twine("-") +
                      getObjectFormatTypeName(ObjectFormat)).str());
}

void Triple::setObjectFormat(ObjectFormatType Kind) {
  if (Environment == UnknownEnvironment)
    return setEnvironmentName(getObjectFormatTypeName(Kind));

  setEnvironmentName((getEnvironmentTypeName(Environment) + Twine("-") +
                      getObjectFormatTypeName(Kind)).str());
}

void Triple::setArchName(StringRef Str) {
  // Work around a miscompilation bug for Twines in gcc 4.0.3.
  SmallString<64> Triple;
  Triple += Str;
  Triple += "-";
  Triple += getVendorName();
  Triple += "-";
  Triple += getOSAndEnvironmentName();
  setTriple(Triple);
}

void Triple::setVendorName(StringRef Str) {
  setTriple(getArchName() + "-" + Str + "-" + getOSAndEnvironmentName());
}

void Triple::setOSName(StringRef Str) {
  if (hasEnvironment())
    setTriple(getArchName() + "-" + getVendorName() + "-" + Str +
              "-" + getEnvironmentName());
  else
    setTriple(getArchName() + "-" + getVendorName() + "-" + Str);
}

void Triple::setEnvironmentName(StringRef Str) {
  setTriple(getArchName() + "-" + getVendorName() + "-" + getOSName() +
            "-" + Str);
}

void Triple::setOSAndEnvironmentName(StringRef Str) {
  setTriple(getArchName() + "-" + getVendorName() + "-" + Str);
}

static unsigned getArchPointerBitWidth(llvm::Triple::ArchType Arch) {
  switch (Arch) {
  case llvm::Triple::UnknownArch:
    return 0;

  case llvm::Triple::avr:
  case llvm::Triple::msp430:
    return 16;

  case llvm::Triple::arc:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::hexagon:
  case llvm::Triple::le32:
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::nvptx:
  case llvm::Triple::ppc:
  case llvm::Triple::r600:
  case llvm::Triple::riscv32:
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::tce:
  case llvm::Triple::tcele:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
  case llvm::Triple::x86:
  case llvm::Triple::xcore:
  case llvm::Triple::amdil:
  case llvm::Triple::hsail:
  case llvm::Triple::spir:
  case llvm::Triple::kalimba:
  case llvm::Triple::lanai:
  case llvm::Triple::shave:
  case llvm::Triple::wasm32:
  case llvm::Triple::renderscript32:
    return 32;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::amdgcn:
  case llvm::Triple::bpfel:
  case llvm::Triple::bpfeb:
  case llvm::Triple::le64:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::nvptx64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv64:
  case llvm::Triple::sparcv9:
  case llvm::Triple::systemz:
  case llvm::Triple::x86_64:
  case llvm::Triple::amdil64:
  case llvm::Triple::hsail64:
  case llvm::Triple::spir64:
  case llvm::Triple::wasm64:
  case llvm::Triple::renderscript64:
    return 64;
  }
  llvm_unreachable("Invalid architecture value");
}

bool Triple::isArch64Bit() const {
  return getArchPointerBitWidth(getArch()) == 64;
}

bool Triple::isArch32Bit() const {
  return getArchPointerBitWidth(getArch()) == 32;
}

bool Triple::isArch16Bit() const {
  return getArchPointerBitWidth(getArch()) == 16;
}

Triple Triple::get32BitArchVariant() const {
  Triple T(*this);
  switch (getArch()) {
  case Triple::UnknownArch:
  case Triple::amdgcn:
  case Triple::avr:
  case Triple::bpfel:
  case Triple::bpfeb:
  case Triple::msp430:
  case Triple::systemz:
  case Triple::ppc64le:
    T.setArch(UnknownArch);
    break;

  case Triple::amdil:
  case Triple::hsail:
  case Triple::spir:
  case Triple::arc:
  case Triple::arm:
  case Triple::armeb:
  case Triple::hexagon:
  case Triple::kalimba:
  case Triple::le32:
  case Triple::mips:
  case Triple::mipsel:
  case Triple::nvptx:
  case Triple::ppc:
  case Triple::r600:
  case Triple::riscv32:
  case Triple::sparc:
  case Triple::sparcel:
  case Triple::tce:
  case Triple::tcele:
  case Triple::thumb:
  case Triple::thumbeb:
  case Triple::x86:
  case Triple::xcore:
  case Triple::lanai:
  case Triple::shave:
  case Triple::wasm32:
  case Triple::renderscript32:
    // Already 32-bit.
    break;

  case Triple::aarch64:        T.setArch(Triple::arm);     break;
  case Triple::aarch64_be:     T.setArch(Triple::armeb);   break;
  case Triple::le64:           T.setArch(Triple::le32);    break;
  case Triple::mips64:         T.setArch(Triple::mips);    break;
  case Triple::mips64el:       T.setArch(Triple::mipsel);  break;
  case Triple::nvptx64:        T.setArch(Triple::nvptx);   break;
  case Triple::ppc64:          T.setArch(Triple::ppc);     break;
  case Triple::sparcv9:        T.setArch(Triple::sparc);   break;
  case Triple::riscv64:        T.setArch(Triple::riscv32); break;
  case Triple::x86_64:         T.setArch(Triple::x86);     break;
  case Triple::amdil64:        T.setArch(Triple::amdil);   break;
  case Triple::hsail64:        T.setArch(Triple::hsail);   break;
  case Triple::spir64:         T.setArch(Triple::spir);    break;
  case Triple::wasm64:         T.setArch(Triple::wasm32);  break;
  case Triple::renderscript64: T.setArch(Triple::renderscript32); break;
  }
  return T;
}

Triple Triple::get64BitArchVariant() const {
  Triple T(*this);
  switch (getArch()) {
  case Triple::UnknownArch:
  case Triple::arc:
  case Triple::avr:
  case Triple::hexagon:
  case Triple::kalimba:
  case Triple::lanai:
  case Triple::msp430:
  case Triple::r600:
  case Triple::tce:
  case Triple::tcele:
  case Triple::xcore:
  case Triple::sparcel:
  case Triple::shave:
    T.setArch(UnknownArch);
    break;

  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::bpfel:
  case Triple::bpfeb:
  case Triple::le64:
  case Triple::amdil64:
  case Triple::amdgcn:
  case Triple::hsail64:
  case Triple::spir64:
  case Triple::mips64:
  case Triple::mips64el:
  case Triple::nvptx64:
  case Triple::ppc64:
  case Triple::ppc64le:
  case Triple::riscv64:
  case Triple::sparcv9:
  case Triple::systemz:
  case Triple::x86_64:
  case Triple::wasm64:
  case Triple::renderscript64:
    // Already 64-bit.
    break;

  case Triple::arm:             T.setArch(Triple::aarch64);    break;
  case Triple::armeb:           T.setArch(Triple::aarch64_be); break;
  case Triple::le32:            T.setArch(Triple::le64);       break;
  case Triple::mips:            T.setArch(Triple::mips64);     break;
  case Triple::mipsel:          T.setArch(Triple::mips64el);   break;
  case Triple::nvptx:           T.setArch(Triple::nvptx64);    break;
  case Triple::ppc:             T.setArch(Triple::ppc64);      break;
  case Triple::sparc:           T.setArch(Triple::sparcv9);    break;
  case Triple::riscv32:         T.setArch(Triple::riscv64);    break;
  case Triple::x86:             T.setArch(Triple::x86_64);     break;
  case Triple::amdil:           T.setArch(Triple::amdil64);    break;
  case Triple::hsail:           T.setArch(Triple::hsail64);    break;
  case Triple::spir:            T.setArch(Triple::spir64);     break;
  case Triple::thumb:           T.setArch(Triple::aarch64);    break;
  case Triple::thumbeb:         T.setArch(Triple::aarch64_be); break;
  case Triple::wasm32:          T.setArch(Triple::wasm64);     break;
  case Triple::renderscript32:  T.setArch(Triple::renderscript64);     break;
  }
  return T;
}

Triple Triple::getBigEndianArchVariant() const {
  Triple T(*this);
  // Already big endian.
  if (!isLittleEndian())
    return T;
  switch (getArch()) {
  case Triple::UnknownArch:
  case Triple::amdgcn:
  case Triple::amdil64:
  case Triple::amdil:
  case Triple::avr:
  case Triple::hexagon:
  case Triple::hsail64:
  case Triple::hsail:
  case Triple::kalimba:
  case Triple::le32:
  case Triple::le64:
  case Triple::msp430:
  case Triple::nvptx64:
  case Triple::nvptx:
  case Triple::r600:
  case Triple::riscv32:
  case Triple::riscv64:
  case Triple::shave:
  case Triple::spir64:
  case Triple::spir:
  case Triple::wasm32:
  case Triple::wasm64:
  case Triple::x86:
  case Triple::x86_64:
  case Triple::xcore:
  case Triple::renderscript32:
  case Triple::renderscript64:

  // ARM is intentionally unsupported here, changing the architecture would
  // drop any arch suffixes.
  case Triple::arm:
  case Triple::thumb:
    T.setArch(UnknownArch);
    break;

  case Triple::tcele:   T.setArch(Triple::tce);        break;
  case Triple::aarch64: T.setArch(Triple::aarch64_be); break;
  case Triple::bpfel:   T.setArch(Triple::bpfeb);      break;
  case Triple::mips64el:T.setArch(Triple::mips64);     break;
  case Triple::mipsel:  T.setArch(Triple::mips);       break;
  case Triple::ppc64le: T.setArch(Triple::ppc64);      break;
  case Triple::sparcel: T.setArch(Triple::sparc);      break;
  default:
    llvm_unreachable("getBigEndianArchVariant: unknown triple.");
  }
  return T;
}

Triple Triple::getLittleEndianArchVariant() const {
  Triple T(*this);
  if (isLittleEndian())
    return T;

  switch (getArch()) {
  case Triple::UnknownArch:
  case Triple::lanai:
  case Triple::ppc:
  case Triple::sparcv9:
  case Triple::systemz:

  // ARM is intentionally unsupported here, changing the architecture would
  // drop any arch suffixes.
  case Triple::armeb:
  case Triple::thumbeb:
    T.setArch(UnknownArch);
    break;

  case Triple::tce:        T.setArch(Triple::tcele);    break;
  case Triple::aarch64_be: T.setArch(Triple::aarch64);  break;
  case Triple::bpfeb:      T.setArch(Triple::bpfel);    break;
  case Triple::mips64:     T.setArch(Triple::mips64el); break;
  case Triple::mips:       T.setArch(Triple::mipsel);   break;
  case Triple::ppc64:      T.setArch(Triple::ppc64le);  break;
  case Triple::sparc:      T.setArch(Triple::sparcel);  break;
  default:
    llvm_unreachable("getLittleEndianArchVariant: unknown triple.");
  }
  return T;
}

bool Triple::isLittleEndian() const {
  switch (getArch()) {
  case Triple::aarch64:
  case Triple::amdgcn:
  case Triple::amdil64:
  case Triple::amdil:
  case Triple::arm:
  case Triple::avr:
  case Triple::bpfel:
  case Triple::hexagon:
  case Triple::hsail64:
  case Triple::hsail:
  case Triple::kalimba:
  case Triple::le32:
  case Triple::le64:
  case Triple::mips64el:
  case Triple::mipsel:
  case Triple::msp430:
  case Triple::nvptx64:
  case Triple::nvptx:
  case Triple::ppc64le:
  case Triple::r600:
  case Triple::riscv32:
  case Triple::riscv64:
  case Triple::shave:
  case Triple::sparcel:
  case Triple::spir64:
  case Triple::spir:
  case Triple::thumb:
  case Triple::wasm32:
  case Triple::wasm64:
  case Triple::x86:
  case Triple::x86_64:
  case Triple::xcore:
  case Triple::tcele:
  case Triple::renderscript32:
  case Triple::renderscript64:
    return true;
  default:
    return false;
  }
}

bool Triple::isCompatibleWith(const Triple &Other) const {
  // ARM and Thumb triples are compatible, if subarch, vendor and OS match.
  if ((getArch() == Triple::thumb && Other.getArch() == Triple::arm) ||
      (getArch() == Triple::arm && Other.getArch() == Triple::thumb) ||
      (getArch() == Triple::thumbeb && Other.getArch() == Triple::armeb) ||
      (getArch() == Triple::armeb && Other.getArch() == Triple::thumbeb)) {
    if (getVendor() == Triple::Apple)
      return getSubArch() == Other.getSubArch() &&
             getVendor() == Other.getVendor() && getOS() == Other.getOS();
    else
      return getSubArch() == Other.getSubArch() &&
             getVendor() == Other.getVendor() && getOS() == Other.getOS() &&
             getEnvironment() == Other.getEnvironment() &&
             getObjectFormat() == Other.getObjectFormat();
  }

  // If vendor is apple, ignore the version number.
  if (getVendor() == Triple::Apple)
    return getArch() == Other.getArch() && getSubArch() == Other.getSubArch() &&
           getVendor() == Other.getVendor() && getOS() == Other.getOS();

  return *this == Other;
}

std::string Triple::merge(const Triple &Other) const {
  // If vendor is apple, pick the triple with the larger version number.
  if (getVendor() == Triple::Apple)
    if (Other.isOSVersionLT(*this))
      return str();

  return Other.str();
}

StringRef Triple::getARMCPUForArch(StringRef MArch) const {
  if (MArch.empty())
    MArch = getArchName();
  MArch = ARM::getCanonicalArchName(MArch);

  // Some defaults are forced.
  switch (getOS()) {
  case llvm::Triple::FreeBSD:
  case llvm::Triple::NetBSD:
    if (!MArch.empty() && MArch == "v6")
      return "arm1176jzf-s";
    break;
  case llvm::Triple::Win32:
    // FIXME: this is invalid for WindowsCE
    return "cortex-a9";
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::WatchOS:
  case llvm::Triple::TvOS:
    if (MArch == "v7k")
      return "cortex-a7";
    break;
  default:
    break;
  }

  if (MArch.empty())
    return StringRef();

  StringRef CPU = ARM::getDefaultCPU(MArch);
  if (!CPU.empty() && !CPU.equals("invalid"))
    return CPU;

  // If no specific architecture version is requested, return the minimum CPU
  // required by the OS and environment.
  switch (getOS()) {
  case llvm::Triple::NetBSD:
    switch (getEnvironment()) {
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABI:
    case llvm::Triple::EABIHF:
    case llvm::Triple::EABI:
      return "arm926ej-s";
    default:
      return "strongarm";
    }
  case llvm::Triple::NaCl:
  case llvm::Triple::OpenBSD:
    return "cortex-a8";
  default:
    switch (getEnvironment()) {
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::MuslEABIHF:
      return "arm1176jzf-s";
    default:
      return "arm7tdmi";
    }
  }

  llvm_unreachable("invalid arch name");
}
