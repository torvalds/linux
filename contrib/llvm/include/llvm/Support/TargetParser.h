//===-- TargetParser - Parser for target features ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features such as
// FPU/CPU/ARCH names as well as specific support such as HDIV, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TARGETPARSER_H
#define LLVM_SUPPORT_TARGETPARSER_H

// FIXME: vector is used because that's what clang uses for subtarget feature
// lists, but SmallVector would probably be better
#include "llvm/ADT/Triple.h"
#include "llvm/Support/ARMTargetParser.h"
#include "llvm/Support/AArch64TargetParser.h"
#include <vector>

namespace llvm {
class StringRef;

// Target specific information in their own namespaces.
// (ARM/AArch64 are declared in ARM/AArch64TargetParser.h)
// These should be generated from TableGen because the information is already
// there, and there is where new information about targets will be added.
// FIXME: To TableGen this we need to make some table generated files available
// even if the back-end is not compiled with LLVM, plus we need to create a new
// back-end to TableGen to create these clean tables.
namespace X86 {

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorVendors : unsigned {
  VENDOR_DUMMY,
#define X86_VENDOR(ENUM, STRING) \
  ENUM,
#include "llvm/Support/X86TargetParser.def"
  VENDOR_OTHER
};

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorTypes : unsigned {
  CPU_TYPE_DUMMY,
#define X86_CPU_TYPE(ARCHNAME, ENUM) \
  ENUM,
#include "llvm/Support/X86TargetParser.def"
  CPU_TYPE_MAX
};

// This should be kept in sync with libcc/compiler-rt as its included by clang
// as a proxy for what's in libgcc/compiler-rt.
enum ProcessorSubtypes : unsigned {
  CPU_SUBTYPE_DUMMY,
#define X86_CPU_SUBTYPE(ARCHNAME, ENUM) \
  ENUM,
#include "llvm/Support/X86TargetParser.def"
  CPU_SUBTYPE_MAX
};

// This should be kept in sync with libcc/compiler-rt as it should be used
// by clang as a proxy for what's in libgcc/compiler-rt.
enum ProcessorFeatures {
#define X86_FEATURE(VAL, ENUM) \
  ENUM = VAL,
#include "llvm/Support/X86TargetParser.def"

};

} // namespace X86

namespace AMDGPU {

/// GPU kinds supported by the AMDGPU target.
enum GPUKind : uint32_t {
  // Not specified processor.
  GK_NONE = 0,

  // R600-based processors.
  GK_R600 = 1,
  GK_R630 = 2,
  GK_RS880 = 3,
  GK_RV670 = 4,
  GK_RV710 = 5,
  GK_RV730 = 6,
  GK_RV770 = 7,
  GK_CEDAR = 8,
  GK_CYPRESS = 9,
  GK_JUNIPER = 10,
  GK_REDWOOD = 11,
  GK_SUMO = 12,
  GK_BARTS = 13,
  GK_CAICOS = 14,
  GK_CAYMAN = 15,
  GK_TURKS = 16,

  GK_R600_FIRST = GK_R600,
  GK_R600_LAST = GK_TURKS,

  // AMDGCN-based processors.
  GK_GFX600 = 32,
  GK_GFX601 = 33,

  GK_GFX700 = 40,
  GK_GFX701 = 41,
  GK_GFX702 = 42,
  GK_GFX703 = 43,
  GK_GFX704 = 44,

  GK_GFX801 = 50,
  GK_GFX802 = 51,
  GK_GFX803 = 52,
  GK_GFX810 = 53,

  GK_GFX900 = 60,
  GK_GFX902 = 61,
  GK_GFX904 = 62,
  GK_GFX906 = 63,
  GK_GFX909 = 65,

  GK_AMDGCN_FIRST = GK_GFX600,
  GK_AMDGCN_LAST = GK_GFX909,
};

/// Instruction set architecture version.
struct IsaVersion {
  unsigned Major;
  unsigned Minor;
  unsigned Stepping;
};

// This isn't comprehensive for now, just things that are needed from the
// frontend driver.
enum ArchFeatureKind : uint32_t {
  FEATURE_NONE = 0,

  // These features only exist for r600, and are implied true for amdgcn.
  FEATURE_FMA = 1 << 1,
  FEATURE_LDEXP = 1 << 2,
  FEATURE_FP64 = 1 << 3,

  // Common features.
  FEATURE_FAST_FMA_F32 = 1 << 4,
  FEATURE_FAST_DENORMAL_F32 = 1 << 5
};

StringRef getArchNameAMDGCN(GPUKind AK);
StringRef getArchNameR600(GPUKind AK);
StringRef getCanonicalArchName(StringRef Arch);
GPUKind parseArchAMDGCN(StringRef CPU);
GPUKind parseArchR600(StringRef CPU);
unsigned getArchAttrAMDGCN(GPUKind AK);
unsigned getArchAttrR600(GPUKind AK);

void fillValidArchListAMDGCN(SmallVectorImpl<StringRef> &Values);
void fillValidArchListR600(SmallVectorImpl<StringRef> &Values);

IsaVersion getIsaVersion(StringRef GPU);

} // namespace AMDGPU

} // namespace llvm

#endif
