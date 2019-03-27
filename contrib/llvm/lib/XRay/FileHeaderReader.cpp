//===- FileHeaderReader.cpp - XRay File Header Reader  --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FileHeaderReader.h"

namespace llvm {
namespace xray {

// Populates the FileHeader reference by reading the first 32 bytes of the file.
Expected<XRayFileHeader> readBinaryFormatHeader(DataExtractor &HeaderExtractor,
                                                uint32_t &OffsetPtr) {
  // FIXME: Maybe deduce whether the data is little or big-endian using some
  // magic bytes in the beginning of the file?

  // First 32 bytes of the file will always be the header. We assume a certain
  // format here:
  //
  //   (2)   uint16 : version
  //   (2)   uint16 : type
  //   (4)   uint32 : bitfield
  //   (8)   uint64 : cycle frequency
  //   (16)  -      : padding
  XRayFileHeader FileHeader;
  auto PreReadOffset = OffsetPtr;
  FileHeader.Version = HeaderExtractor.getU16(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading version from file header at offset %d.", OffsetPtr);

  PreReadOffset = OffsetPtr;
  FileHeader.Type = HeaderExtractor.getU16(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading file type from file header at offset %d.", OffsetPtr);

  PreReadOffset = OffsetPtr;
  uint32_t Bitfield = HeaderExtractor.getU32(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading flag bits from file header at offset %d.", OffsetPtr);

  FileHeader.ConstantTSC = Bitfield & 1uL;
  FileHeader.NonstopTSC = Bitfield & 1uL << 1;
  PreReadOffset = OffsetPtr;
  FileHeader.CycleFrequency = HeaderExtractor.getU64(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading cycle frequency from file header at offset %d.",
        OffsetPtr);

  std::memcpy(&FileHeader.FreeFormData,
              HeaderExtractor.getData().bytes_begin() + OffsetPtr, 16);

  // Manually advance the offset pointer 16 bytes, after getting a raw memcpy
  // from the underlying data.
  OffsetPtr += 16;
  return std::move(FileHeader);
}

} // namespace xray
} // namespace llvm
