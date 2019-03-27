//===- FDRRecordProducer.cpp - XRay FDR Mode Record Producer --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

Error RecordInitializer::visit(BufferExtents &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr, sizeof(uint64_t)))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a buffer extent (%d).",
                             OffsetPtr);

  auto PreReadOffset = OffsetPtr;
  R.Size = E.getU64(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read buffer extent at offset %d.",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - PreReadOffset);
  return Error::success();
}

Error RecordInitializer::visit(WallclockRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a wallclock record (%d).",
                             OffsetPtr);
  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = OffsetPtr;
  R.Seconds = E.getU64(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read wall clock 'seconds' field at offset %d.", OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.Nanos = E.getU32(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read wall clock 'nanos' field at offset %d.", OffsetPtr);

  // Align to metadata record size boundary.
  assert(OffsetPtr - BeginOffset <= MetadataRecord::kMetadataBodySize);
  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - BeginOffset);
  return Error::success();
}

Error RecordInitializer::visit(NewCPUIDRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a new cpu id record (%d).",
                             OffsetPtr);
  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = OffsetPtr;
  R.CPUId = E.getU16(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read CPU id at offset %d.", OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.TSC = E.getU64(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read CPU TSC at offset %d.", OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - BeginOffset);
  return Error::success();
}

Error RecordInitializer::visit(TSCWrapRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a new TSC wrap record (%d).",
                             OffsetPtr);

  auto PreReadOffset = OffsetPtr;
  R.BaseTSC = E.getU64(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read TSC wrap record at offset %d.",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - PreReadOffset);
  return Error::success();
}

Error RecordInitializer::visit(CustomEventRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a custom event record (%d).",
                             OffsetPtr);

  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = OffsetPtr;
  R.Size = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a custom event record size field offset %d.", OffsetPtr);

  if (R.Size <= 0)
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Invalid size for custom event (size = %d) at offset %d.", R.Size,
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.TSC = E.getU64(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a custom event TSC field at offset %d.", OffsetPtr);

  // For version 4 onwards, of the FDR log, we want to also capture the CPU ID
  // of the custom event.
  if (Version >= 4) {
    PreReadOffset = OffsetPtr;
    R.CPU = E.getU16(&OffsetPtr);
    if (PreReadOffset == OffsetPtr)
      return createStringError(
          std::make_error_code(std::errc::invalid_argument),
          "Missing CPU field at offset %d", OffsetPtr);
  }

  assert(OffsetPtr > BeginOffset &&
         OffsetPtr - BeginOffset <= MetadataRecord::kMetadataBodySize);
  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - BeginOffset);

  // Next we read in a fixed chunk of data from the given offset.
  if (!E.isValidOffsetForDataOfSize(OffsetPtr, R.Size))
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Cannot read %d bytes of custom event data from offset %d.", R.Size,
        OffsetPtr);

  std::vector<uint8_t> Buffer;
  Buffer.resize(R.Size);
  PreReadOffset = OffsetPtr;
  if (E.getU8(&OffsetPtr, Buffer.data(), R.Size) != Buffer.data())
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading data into buffer of size %d at offset %d.", R.Size,
        OffsetPtr);

  assert(OffsetPtr >= PreReadOffset);
  if (OffsetPtr - PreReadOffset != static_cast<uint32_t>(R.Size))
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading enough bytes for the custom event payload -- read %d "
        "expecting %d bytes at offset %d.",
        OffsetPtr - PreReadOffset, R.Size, PreReadOffset);

  R.Data.assign(Buffer.begin(), Buffer.end());
  return Error::success();
}

Error RecordInitializer::visit(CustomEventRecordV5 &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a custom event record (%d).",
                             OffsetPtr);

  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = OffsetPtr;

  R.Size = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a custom event record size field offset %d.", OffsetPtr);

  if (R.Size <= 0)
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Invalid size for custom event (size = %d) at offset %d.", R.Size,
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.Delta = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a custom event record TSC delta field at offset %d.",
        OffsetPtr);

  assert(OffsetPtr > BeginOffset &&
         OffsetPtr - BeginOffset <= MetadataRecord::kMetadataBodySize);
  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - BeginOffset);

  // Next we read in a fixed chunk of data from the given offset.
  if (!E.isValidOffsetForDataOfSize(OffsetPtr, R.Size))
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Cannot read %d bytes of custom event data from offset %d.", R.Size,
        OffsetPtr);

  std::vector<uint8_t> Buffer;
  Buffer.resize(R.Size);
  PreReadOffset = OffsetPtr;
  if (E.getU8(&OffsetPtr, Buffer.data(), R.Size) != Buffer.data())
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading data into buffer of size %d at offset %d.", R.Size,
        OffsetPtr);

  assert(OffsetPtr >= PreReadOffset);
  if (OffsetPtr - PreReadOffset != static_cast<uint32_t>(R.Size))
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading enough bytes for the custom event payload -- read %d "
        "expecting %d bytes at offset %d.",
        OffsetPtr - PreReadOffset, R.Size, PreReadOffset);

  R.Data.assign(Buffer.begin(), Buffer.end());
  return Error::success();
}

Error RecordInitializer::visit(TypedEventRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a typed event record (%d).",
                             OffsetPtr);

  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = OffsetPtr;

  R.Size = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a typed event record size field offset %d.", OffsetPtr);

  if (R.Size <= 0)
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Invalid size for typed event (size = %d) at offset %d.", R.Size,
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.Delta = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a typed event record TSC delta field at offset %d.",
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  R.EventType = E.getU16(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Cannot read a typed event record type field at offset %d.", OffsetPtr);

  assert(OffsetPtr > BeginOffset &&
         OffsetPtr - BeginOffset <= MetadataRecord::kMetadataBodySize);
  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - BeginOffset);

  // Next we read in a fixed chunk of data from the given offset.
  if (!E.isValidOffsetForDataOfSize(OffsetPtr, R.Size))
    return createStringError(
        std::make_error_code(std::errc::bad_address),
        "Cannot read %d bytes of custom event data from offset %d.", R.Size,
        OffsetPtr);

  std::vector<uint8_t> Buffer;
  Buffer.resize(R.Size);
  PreReadOffset = OffsetPtr;
  if (E.getU8(&OffsetPtr, Buffer.data(), R.Size) != Buffer.data())
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading data into buffer of size %d at offset %d.", R.Size,
        OffsetPtr);

  assert(OffsetPtr >= PreReadOffset);
  if (OffsetPtr - PreReadOffset != static_cast<uint32_t>(R.Size))
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading enough bytes for the typed event payload -- read %d "
        "expecting %d bytes at offset %d.",
        OffsetPtr - PreReadOffset, R.Size, PreReadOffset);

  R.Data.assign(Buffer.begin(), Buffer.end());
  return Error::success();
}

Error RecordInitializer::visit(CallArgRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a call argument record (%d).",
                             OffsetPtr);

  auto PreReadOffset = OffsetPtr;
  R.Arg = E.getU64(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read a call arg record at offset %d.",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - PreReadOffset);
  return Error::success();
}

Error RecordInitializer::visit(PIDRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a process ID record (%d).",
                             OffsetPtr);

  auto PreReadOffset = OffsetPtr;
  R.PID = E.getSigned(&OffsetPtr, 4);
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read a process ID record at offset %d.",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - PreReadOffset);
  return Error::success();
}

Error RecordInitializer::visit(NewBufferRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a new buffer record (%d).",
                             OffsetPtr);

  auto PreReadOffset = OffsetPtr;
  R.TID = E.getSigned(&OffsetPtr, sizeof(int32_t));
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Cannot read a new buffer record at offset %d.",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize - (OffsetPtr - PreReadOffset);
  return Error::success();
}

Error RecordInitializer::visit(EndBufferRecord &R) {
  if (!E.isValidOffsetForDataOfSize(OffsetPtr,
                                    MetadataRecord::kMetadataBodySize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for an end-of-buffer record (%d).",
                             OffsetPtr);

  OffsetPtr += MetadataRecord::kMetadataBodySize;
  return Error::success();
}

Error RecordInitializer::visit(FunctionRecord &R) {
  // For function records, we need to retreat one byte back to read a full
  // unsigned 32-bit value. The first four bytes will have the following
  // layout:
  //
  //   bit  0     : function record indicator (must be 0)
  //   bits 1..3  : function record type
  //   bits 4..32 : function id
  //
  if (OffsetPtr == 0 || !E.isValidOffsetForDataOfSize(
                            --OffsetPtr, FunctionRecord::kFunctionRecordSize))
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Invalid offset for a function record (%d).",
                             OffsetPtr);

  auto BeginOffset = OffsetPtr;
  auto PreReadOffset = BeginOffset;
  uint32_t Buffer = E.getU32(&OffsetPtr);
  if (PreReadOffset == OffsetPtr)
    return createStringError(std::make_error_code(std::errc::bad_address),
                             "Cannot read function id field from offset %d.",
                             OffsetPtr);

  // To get the function record type, we shift the buffer one to the right
  // (truncating the function record indicator) then take the three bits
  // (0b0111) to get the record type as an unsigned value.
  unsigned FunctionType = (Buffer >> 1) & 0x07u;
  switch (FunctionType) {
  case static_cast<unsigned>(RecordTypes::ENTER):
  case static_cast<unsigned>(RecordTypes::ENTER_ARG):
  case static_cast<unsigned>(RecordTypes::EXIT):
  case static_cast<unsigned>(RecordTypes::TAIL_EXIT):
    R.Kind = static_cast<RecordTypes>(FunctionType);
    break;
  default:
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Unknown function record type '%d' at offset %d.",
                             FunctionType, BeginOffset);
  }

  R.FuncId = Buffer >> 4;
  PreReadOffset = OffsetPtr;
  R.Delta = E.getU32(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Failed reading TSC delta from offset %d.",
                             OffsetPtr);
  assert(FunctionRecord::kFunctionRecordSize == (OffsetPtr - BeginOffset));
  return Error::success();
}

} // namespace xray
} // namespace llvm
