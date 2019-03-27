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

#ifndef LLVM_SUPPORT_ARMTARGETPARSER_H
#define LLVM_SUPPORT_ARMTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include <vector>

namespace llvm {
namespace ARM {

// Arch extension modifiers for CPUs.
// Note that this is not the same as the AArch64 list
enum ArchExtKind : unsigned {
  AEK_INVALID =     0,
  AEK_NONE =        1,
  AEK_CRC =         1 << 1,
  AEK_CRYPTO =      1 << 2,
  AEK_FP =          1 << 3,
  AEK_HWDIVTHUMB =  1 << 4,
  AEK_HWDIVARM =    1 << 5,
  AEK_MP =          1 << 6,
  AEK_SIMD =        1 << 7,
  AEK_SEC =         1 << 8,
  AEK_VIRT =        1 << 9,
  AEK_DSP =         1 << 10,
  AEK_FP16 =        1 << 11,
  AEK_RAS =         1 << 12,
  AEK_SVE =         1 << 13,
  AEK_DOTPROD =     1 << 14,
  AEK_SHA2    =     1 << 15,
  AEK_AES     =     1 << 16,
  AEK_FP16FML =     1 << 17,
  AEK_SB      =     1 << 18,
  // Unsupported extensions.
  AEK_OS = 0x8000000,
  AEK_IWMMXT = 0x10000000,
  AEK_IWMMXT2 = 0x20000000,
  AEK_MAVERICK = 0x40000000,
  AEK_XSCALE = 0x80000000,
};

// List of Arch Extension names.
// FIXME: TableGen this.
struct ExtName {
  const char *NameCStr;
  size_t NameLength;
  unsigned ID;
  const char *Feature;
  const char *NegFeature;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

const ExtName ARCHExtNames[] = {
#define ARM_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                       \
  {NAME, sizeof(NAME) - 1, ID, FEATURE, NEGFEATURE},
#include "ARMTargetParser.def"
};

// List of HWDiv names (use getHWDivSynonym) and which architectural
// features they correspond to (use getHWDivFeatures).
// FIXME: TableGen this.
const struct {
  const char *NameCStr;
  size_t NameLength;
  unsigned ID;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
} HWDivNames[] = {
#define ARM_HW_DIV_NAME(NAME, ID) {NAME, sizeof(NAME) - 1, ID},
#include "ARMTargetParser.def"
};

// Arch names.
enum class ArchKind {
#define ARM_ARCH(NAME, ID, CPU_ATTR, SUB_ARCH, ARCH_ATTR, ARCH_FPU, ARCH_BASE_EXT) ID,
#include "ARMTargetParser.def"
};

// List of CPU names and their arches.
// The same CPU can have multiple arches and can be default on multiple arches.
// When finding the Arch for a CPU, first-found prevails. Sort them accordingly.
// When this becomes table-generated, we'd probably need two tables.
// FIXME: TableGen this.
template <typename T> struct CpuNames {
  const char *NameCStr;
  size_t NameLength;
  T ArchID;
  bool Default; // is $Name the default CPU for $ArchID ?
  unsigned DefaultExtensions;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

const CpuNames<ArchKind> CPUNames[] = {
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  {NAME, sizeof(NAME) - 1, ARM::ArchKind::ID, IS_DEFAULT, DEFAULT_EXT},
#include "ARMTargetParser.def"
};

// FPU names.
enum FPUKind {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION) KIND,
#include "ARMTargetParser.def"
  FK_LAST
};

// FPU Version
enum class FPUVersion {
  NONE,
  VFPV2,
  VFPV3,
  VFPV3_FP16,
  VFPV4,
  VFPV5
};

// An FPU name restricts the FPU in one of three ways:
enum class FPURestriction {
  None = 0, ///< No restriction
  D16,      ///< Only 16 D registers
  SP_D16    ///< Only single-precision instructions, with 16 D registers
};

// An FPU name implies one of three levels of Neon support:
enum class NeonSupportLevel {
  None = 0, ///< No Neon
  Neon,     ///< Neon
  Crypto    ///< Neon with Crypto
};

// ISA kinds.
enum class ISAKind { INVALID = 0, ARM, THUMB, AARCH64 };

// Endianness
// FIXME: BE8 vs. BE32?
enum class EndianKind { INVALID = 0, LITTLE, BIG };

// v6/v7/v8 Profile
enum class ProfileKind { INVALID = 0, A, R, M };

// List of canonical FPU names (use getFPUSynonym) and which architectural
// features they correspond to (use getFPUFeatures).
// FIXME: TableGen this.
// The entries must appear in the order listed in ARM::FPUKind for correct
// indexing
struct FPUName {
  const char *NameCStr;
  size_t NameLength;
  FPUKind ID;
  FPUVersion FPUVer;
  NeonSupportLevel NeonSupport;
  FPURestriction Restriction;

  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

static const FPUName FPUNames[] = {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION)                \
  {NAME, sizeof(NAME) - 1, KIND, VERSION, NEON_SUPPORT, RESTRICTION},
#include "llvm/Support/ARMTargetParser.def"
};

// List of canonical arch names (use getArchSynonym).
// This table also provides the build attribute fields for CPU arch
// and Arch ID, according to the Addenda to the ARM ABI, chapters
// 2.4 and 2.3.5.2 respectively.
// FIXME: SubArch values were simplified to fit into the expectations
// of the triples and are not conforming with their official names.
// Check to see if the expectation should be changed.
// FIXME: TableGen this.
template <typename T> struct ArchNames {
  const char *NameCStr;
  size_t NameLength;
  const char *CPUAttrCStr;
  size_t CPUAttrLength;
  const char *SubArchCStr;
  size_t SubArchLength;
  unsigned DefaultFPU;
  unsigned ArchBaseExtensions;
  T ID;
  ARMBuildAttrs::CPUArch ArchAttr; // Arch ID in build attributes.

  StringRef getName() const { return StringRef(NameCStr, NameLength); }

  // CPU class in build attributes.
  StringRef getCPUAttr() const { return StringRef(CPUAttrCStr, CPUAttrLength); }

  // Sub-Arch name.
  StringRef getSubArch() const { return StringRef(SubArchCStr, SubArchLength); }
};

static const ArchNames<ArchKind> ARCHNames[] = {
#define ARM_ARCH(NAME, ID, CPU_ATTR, SUB_ARCH, ARCH_ATTR, ARCH_FPU,            \
                 ARCH_BASE_EXT)                                                \
  {NAME,         sizeof(NAME) - 1,                                             \
   CPU_ATTR,     sizeof(CPU_ATTR) - 1,                                         \
   SUB_ARCH,     sizeof(SUB_ARCH) - 1,                                         \
   ARCH_FPU,     ARCH_BASE_EXT,                                                \
   ArchKind::ID, ARCH_ATTR},
#include "llvm/Support/ARMTargetParser.def"
};

// Information by ID
StringRef getFPUName(unsigned FPUKind);
FPUVersion getFPUVersion(unsigned FPUKind);
NeonSupportLevel getFPUNeonSupportLevel(unsigned FPUKind);
FPURestriction getFPURestriction(unsigned FPUKind);

// FIXME: These should be moved to TargetTuple once it exists
bool getFPUFeatures(unsigned FPUKind, std::vector<StringRef> &Features);
bool getHWDivFeatures(unsigned HWDivKind, std::vector<StringRef> &Features);
bool getExtensionFeatures(unsigned Extensions,
                          std::vector<StringRef> &Features);

StringRef getArchName(ArchKind AK);
unsigned getArchAttr(ArchKind AK);
StringRef getCPUAttr(ArchKind AK);
StringRef getSubArch(ArchKind AK);
StringRef getArchExtName(unsigned ArchExtKind);
StringRef getArchExtFeature(StringRef ArchExt);
StringRef getHWDivName(unsigned HWDivKind);

// Information by Name
unsigned getDefaultFPU(StringRef CPU, ArchKind AK);
unsigned getDefaultExtensions(StringRef CPU, ArchKind AK);
StringRef getDefaultCPU(StringRef Arch);
StringRef getCanonicalArchName(StringRef Arch);
StringRef getFPUSynonym(StringRef FPU);
StringRef getArchSynonym(StringRef Arch);

// Parser
unsigned parseHWDiv(StringRef HWDiv);
unsigned parseFPU(StringRef FPU);
ArchKind parseArch(StringRef Arch);
unsigned parseArchExt(StringRef ArchExt);
ArchKind parseCPUArch(StringRef CPU);
ISAKind parseArchISA(StringRef Arch);
EndianKind parseArchEndian(StringRef Arch);
ProfileKind parseArchProfile(StringRef Arch);
unsigned parseArchVersion(StringRef Arch);

void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);
StringRef computeDefaultTargetABI(const Triple &TT, StringRef CPU);

} // namespace ARM
} // namespace llvm

#endif
