//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AARCH64TARGETPARSERCOMMON_H
#define LLVM_SUPPORT_AARCH64TARGETPARSERCOMMON_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/ARMTargetParser.h"
#include <vector>

// FIXME:This should be made into class design,to avoid dupplication.
namespace llvm {
namespace AArch64 {

// Arch extension modifiers for CPUs.
enum ArchExtKind : unsigned {
  AEK_INVALID =     0,
  AEK_NONE =        1,
  AEK_CRC =         1 << 1,
  AEK_CRYPTO =      1 << 2,
  AEK_FP =          1 << 3,
  AEK_SIMD =        1 << 4,
  AEK_FP16 =        1 << 5,
  AEK_PROFILE =     1 << 6,
  AEK_RAS =         1 << 7,
  AEK_LSE =         1 << 8,
  AEK_SVE =         1 << 9,
  AEK_DOTPROD =     1 << 10,
  AEK_RCPC =        1 << 11,
  AEK_RDM =         1 << 12,
  AEK_SM4 =         1 << 13,
  AEK_SHA3 =        1 << 14,
  AEK_SHA2 =        1 << 15,
  AEK_AES =         1 << 16,
  AEK_FP16FML =     1 << 17,
  AEK_RAND =        1 << 18,
  AEK_MTE =         1 << 19,
  AEK_SSBS =        1 << 20,
  AEK_SB =          1 << 21,
  AEK_PREDRES =     1 << 22,
};

enum class ArchKind {
#define AARCH64_ARCH(NAME, ID, CPU_ATTR, SUB_ARCH, ARCH_ATTR, ARCH_FPU, ARCH_BASE_EXT) ID,
#include "AArch64TargetParser.def"
};

const ARM::ArchNames<ArchKind> AArch64ARCHNames[] = {
#define AARCH64_ARCH(NAME, ID, CPU_ATTR, SUB_ARCH, ARCH_ATTR, ARCH_FPU,        \
                     ARCH_BASE_EXT)                                            \
  {NAME,                                                                       \
   sizeof(NAME) - 1,                                                           \
   CPU_ATTR,                                                                   \
   sizeof(CPU_ATTR) - 1,                                                       \
   SUB_ARCH,                                                                   \
   sizeof(SUB_ARCH) - 1,                                                       \
   ARM::FPUKind::ARCH_FPU,                                                     \
   ARCH_BASE_EXT,                                                              \
   AArch64::ArchKind::ID,                                                      \
   ARCH_ATTR},
#include "AArch64TargetParser.def"
};

const ARM::ExtName AArch64ARCHExtNames[] = {
#define AARCH64_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                   \
  {NAME, sizeof(NAME) - 1, ID, FEATURE, NEGFEATURE},
#include "AArch64TargetParser.def"
};

const ARM::CpuNames<ArchKind> AArch64CPUNames[] = {
#define AARCH64_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)       \
  {NAME, sizeof(NAME) - 1, AArch64::ArchKind::ID, IS_DEFAULT, DEFAULT_EXT},
#include "AArch64TargetParser.def"
};

const ArchKind ArchKinds[] = {
#define AARCH64_ARCH(NAME, ID, CPU_ATTR, SUB_ARCH, ARCH_ATTR, ARCH_FPU, ARCH_BASE_EXT) \
    ArchKind::ID,
#include "AArch64TargetParser.def"
};

// FIXME: These should be moved to TargetTuple once it exists
bool getExtensionFeatures(unsigned Extensions,
                          std::vector<StringRef> &Features);
bool getArchFeatures(ArchKind AK, std::vector<StringRef> &Features);

StringRef getArchName(ArchKind AK);
unsigned getArchAttr(ArchKind AK);
StringRef getCPUAttr(ArchKind AK);
StringRef getSubArch(ArchKind AK);
StringRef getArchExtName(unsigned ArchExtKind);
StringRef getArchExtFeature(StringRef ArchExt);

// Information by Name
unsigned getDefaultFPU(StringRef CPU, ArchKind AK);
unsigned getDefaultExtensions(StringRef CPU, ArchKind AK);
StringRef getDefaultCPU(StringRef Arch);
ArchKind getCPUArchKind(StringRef CPU);

// Parser
ArchKind parseArch(StringRef Arch);
ArchExtKind parseArchExt(StringRef ArchExt);
ArchKind parseCPUArch(StringRef CPU);
// Used by target parser tests
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

bool isX18ReservedByDefault(const Triple &TT);

} // namespace AArch64
} // namespace llvm

#endif
