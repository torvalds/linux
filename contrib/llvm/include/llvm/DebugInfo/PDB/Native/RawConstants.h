//===- RawConstants.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBRAWCONSTANTS_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBRAWCONSTANTS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include <cstdint>

namespace llvm {
namespace pdb {

const uint16_t kInvalidStreamIndex = 0xFFFF;

enum PdbRaw_ImplVer : uint32_t {
  PdbImplVC2 = 19941610,
  PdbImplVC4 = 19950623,
  PdbImplVC41 = 19950814,
  PdbImplVC50 = 19960307,
  PdbImplVC98 = 19970604,
  PdbImplVC70Dep = 19990604, // deprecated
  PdbImplVC70 = 20000404,
  PdbImplVC80 = 20030901,
  PdbImplVC110 = 20091201,
  PdbImplVC140 = 20140508,
};

enum class PdbRaw_SrcHeaderBlockVer : uint32_t { SrcVerOne = 19980827 };

enum class PdbRaw_FeatureSig : uint32_t {
  VC110 = PdbImplVC110,
  VC140 = PdbImplVC140,
  NoTypeMerge = 0x4D544F4E,
  MinimalDebugInfo = 0x494E494D,
};

enum PdbRaw_Features : uint32_t {
  PdbFeatureNone = 0x0,
  PdbFeatureContainsIdStream = 0x1,
  PdbFeatureMinimalDebugInfo = 0x2,
  PdbFeatureNoTypeMerging = 0x4,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ PdbFeatureNoTypeMerging)
};

enum PdbRaw_DbiVer : uint32_t {
  PdbDbiVC41 = 930803,
  PdbDbiV50 = 19960307,
  PdbDbiV60 = 19970606,
  PdbDbiV70 = 19990903,
  PdbDbiV110 = 20091201
};

enum PdbRaw_TpiVer : uint32_t {
  PdbTpiV40 = 19950410,
  PdbTpiV41 = 19951122,
  PdbTpiV50 = 19961031,
  PdbTpiV70 = 19990903,
  PdbTpiV80 = 20040203,
};

enum PdbRaw_DbiSecContribVer : uint32_t {
  DbiSecContribVer60 = 0xeffe0000 + 19970605,
  DbiSecContribV2 = 0xeffe0000 + 20140516
};

enum SpecialStream : uint32_t {
  // Stream 0 contains the copy of previous version of the MSF directory.
  // We are not currently using it, but technically if we find the main
  // MSF is corrupted, we could fallback to it.
  OldMSFDirectory = 0,

  StreamPDB = 1,
  StreamTPI = 2,
  StreamDBI = 3,
  StreamIPI = 4,

  kSpecialStreamCount
};

enum class DbgHeaderType : uint16_t {
  FPO,
  Exception,
  Fixup,
  OmapToSrc,
  OmapFromSrc,
  SectionHdr,
  TokenRidMap,
  Xdata,
  Pdata,
  NewFPO,
  SectionHdrOrig,
  Max
};

enum class OMFSegDescFlags : uint16_t {
  None = 0,
  Read = 1 << 0,              // Segment is readable.
  Write = 1 << 1,             // Segment is writable.
  Execute = 1 << 2,           // Segment is executable.
  AddressIs32Bit = 1 << 3,    // Descriptor describes a 32-bit linear address.
  IsSelector = 1 << 8,        // Frame represents a selector.
  IsAbsoluteAddress = 1 << 9, // Frame represents an absolute address.
  IsGroup = 1 << 10,          // If set, descriptor represents a group.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ IsGroup)
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_RAW_PDBRAWCONSTANTS_H
