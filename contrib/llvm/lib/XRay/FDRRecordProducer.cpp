//===- FDRRecordProducer.cpp - XRay FDR Mode Record Producer --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRRecordProducer.h"
#include "llvm/Support/DataExtractor.h"

#include <cstdint>

namespace llvm {
namespace xray {

namespace {

// Keep this in sync with the values written in the XRay FDR mode runtime in
// compiler-rt.
enum MetadataRecordKinds : uint8_t {
  NewBufferKind,
  EndOfBufferKind,
  NewCPUIdKind,
  TSCWrapKind,
  WalltimeMarkerKind,
  CustomEventMarkerKind,
  CallArgumentKind,
  BufferExtentsKind,
  TypedEventMarkerKind,
  PidKind,
  // This is an end marker, used to identify the upper bound for this enum.
  EnumEndMarker,
};

Expected<std::unique_ptr<Record>>
metadataRecordType(const XRayFileHeader &Header, uint8_t T) {

  if (T >= static_cast<uint8_t>(MetadataRecordKinds::EnumEndMarker))
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Invalid metadata record type: %d", T);
  switch (T) {
  case MetadataRecordKinds::NewBufferKind:
    return make_unique<NewBufferRecord>();
  case MetadataRecordKinds::EndOfBufferKind:
    if (Header.Version >= 2)
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "End of buffer records are no longer supported starting version "
          "2 of the log.");
    return make_unique<EndBufferRecord>();
  case MetadataRecordKinds::NewCPUIdKind:
    return make_unique<NewCPUIDRecord>();
  case MetadataRecordKinds::TSCWrapKind:
    return make_unique<TSCWrapRecord>();
  case MetadataRecordKinds::WalltimeMarkerKind:
    return make_unique<WallclockRecord>();
  case MetadataRecordKinds::CustomEventMarkerKind:
    if (Header.Version >= 5)
      return make_unique<CustomEventRecordV5>();
    return make_unique<CustomEventRecord>();
  case MetadataRecordKinds::CallArgumentKind:
    return make_unique<CallArgRecord>();
  case MetadataRecordKinds::BufferExtentsKind:
    return make_unique<BufferExtents>();
  case MetadataRecordKinds::TypedEventMarkerKind:
    return make_unique<TypedEventRecord>();
  case MetadataRecordKinds::PidKind:
    return make_unique<PIDRecord>();
  case MetadataRecordKinds::EnumEndMarker:
    llvm_unreachable("Invalid MetadataRecordKind");
  }
  llvm_unreachable("Unhandled MetadataRecordKinds enum value");
}

constexpr bool isMetadataIntroducer(uint8_t FirstByte) {
  return FirstByte & 0x01u;
}

} // namespace

Expected<std::unique_ptr<Record>>
FileBasedRecordProducer::findNextBufferExtent() {
  // We seek one byte at a time until we find a suitable buffer extents metadata
  // record introducer.
  std::unique_ptr<Record> R;
  while (!R) {
    auto PreReadOffset = OffsetPtr;
    uint8_t FirstByte = E.getU8(&OffsetPtr);
    if (OffsetPtr == PreReadOffset)
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "Failed reading one byte from offset %d.", OffsetPtr);

    if (isMetadataIntroducer(FirstByte)) {
      auto LoadedType = FirstByte >> 1;
      if (LoadedType == MetadataRecordKinds::BufferExtentsKind) {
        auto MetadataRecordOrErr = metadataRecordType(Header, LoadedType);
        if (!MetadataRecordOrErr)
          return MetadataRecordOrErr.takeError();

        R = std::move(MetadataRecordOrErr.get());
        RecordInitializer RI(E, OffsetPtr);
        if (auto Err = R->apply(RI))
          return std::move(Err);
        return std::move(R);
      }
    }
  }
  llvm_unreachable("Must always terminate with either an error or a record.");
}

Expected<std::unique_ptr<Record>> FileBasedRecordProducer::produce() {
  // First, we set up our result record.
  std::unique_ptr<Record> R;

  // Before we do any further reading, we should check whether we're at the end
  // of the current buffer we're been consuming. In FDR logs version >= 3, we
  // rely on the buffer extents record to determine how many bytes we should be
  // considering as valid records.
  if (Header.Version >= 3 && CurrentBufferBytes == 0) {
    // Find the next buffer extents record.
    auto BufferExtentsOrError = findNextBufferExtent();
    if (!BufferExtentsOrError)
      return joinErrors(
          BufferExtentsOrError.takeError(),
          createStringError(
              std::make_error_code(std::errc::executable_format_error),
              "Failed to find the next BufferExtents record."));

    R = std::move(BufferExtentsOrError.get());
    assert(R != nullptr);
    assert(isa<BufferExtents>(R.get()));
    auto BE = dyn_cast<BufferExtents>(R.get());
    CurrentBufferBytes = BE->size();
    return std::move(R);
  }

  //
  // At the top level, we read one byte to determine the type of the record to
  // create. This byte will comprise of the following bits:
  //
  //   - offset 0: A '1' indicates a metadata record, a '0' indicates a function
  //     record.
  //   - offsets 1-7: For metadata records, this will indicate the kind of
  //     metadata record should be loaded.
  //
  // We read first byte, then create the appropriate type of record to consume
  // the rest of the bytes.
  auto PreReadOffset = OffsetPtr;
  uint8_t FirstByte = E.getU8(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::executable_format_error),
        "Failed reading one byte from offset %d.", OffsetPtr);

  // For metadata records, handle especially here.
  if (isMetadataIntroducer(FirstByte)) {
    auto LoadedType = FirstByte >> 1;
    auto MetadataRecordOrErr = metadataRecordType(Header, LoadedType);
    if (!MetadataRecordOrErr)
      return joinErrors(
          MetadataRecordOrErr.takeError(),
          createStringError(
              std::make_error_code(std::errc::executable_format_error),
              "Encountered an unsupported metadata record (%d) at offset %d.",
              LoadedType, PreReadOffset));
    R = std::move(MetadataRecordOrErr.get());
  } else {
    R = llvm::make_unique<FunctionRecord>();
  }
  RecordInitializer RI(E, OffsetPtr);

  if (auto Err = R->apply(RI))
    return std::move(Err);

  // If we encountered a BufferExtents record, we should record the remaining
  // bytes for the current buffer, to determine when we should start ignoring
  // potentially malformed data and looking for buffer extents records.
  if (auto BE = dyn_cast<BufferExtents>(R.get())) {
    CurrentBufferBytes = BE->size();
  } else if (Header.Version >= 3) {
    if (OffsetPtr - PreReadOffset > CurrentBufferBytes)
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "Buffer over-read at offset %d (over-read by %d bytes); Record Type "
          "= %s.",
          OffsetPtr, (OffsetPtr - PreReadOffset) - CurrentBufferBytes,
          Record::kindToString(R->getRecordType()).data());

    CurrentBufferBytes -= OffsetPtr - PreReadOffset;
  }
  assert(R != nullptr);
  return std::move(R);
}

} // namespace xray
} // namespace llvm
