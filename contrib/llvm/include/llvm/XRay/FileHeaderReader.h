//===- FileHeaderReader.h - XRay Trace File Header Reading Function -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares functions that can load an XRay log header from various
// sources.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_XRAY_FILEHEADERREADER_H_
#define LLVM_LIB_XRAY_FILEHEADERREADER_H_

#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/XRay/XRayRecord.h"
#include <cstdint>

namespace llvm {
namespace xray {

/// Convenience function for loading the file header given a data extractor at a
/// specified offset.
Expected<XRayFileHeader> readBinaryFormatHeader(DataExtractor &HeaderExtractor,
                                                uint32_t &OffsetPtr);

} // namespace xray
} // namespace llvm

#endif // LLVM_LIB_XRAY_FILEHEADERREADER_H_
