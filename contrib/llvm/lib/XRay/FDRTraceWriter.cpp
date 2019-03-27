//===- FDRTraceWriter.cpp - XRay FDR Trace Writer ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Test a utility that can write out XRay FDR Mode formatted trace files.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRTraceWriter.h"
#include <tuple>

namespace llvm {
namespace xray {

namespace {

template <size_t Index> struct IndexedWriter {
  template <
      class Tuple,
      typename std::enable_if<
          (Index <
           std::tuple_size<typename std::remove_reference<Tuple>::type>::value),
          int>::type = 0>
  static size_t write(support::endian::Writer &OS, Tuple &&T) {
    OS.write(std::get<Index>(T));
    return sizeof(std::get<Index>(T)) + IndexedWriter<Index + 1>::write(OS, T);
  }

  template <
      class Tuple,
      typename std::enable_if<
          (Index >=
           std::tuple_size<typename std::remove_reference<Tuple>::type>::value),
          int>::type = 0>
  static size_t write(support::endian::Writer &OS, Tuple &&) {
    return 0;
  }
};

template <uint8_t Kind, class... Values>
Error writeMetadata(support::endian::Writer &OS, Values &&... Ds) {
  // The first bit in the first byte of metadata records is always set to 1, so
  // we ensure this is the case when we write out the first byte of the record.
  uint8_t FirstByte = (static_cast<uint8_t>(Kind) << 1) | uint8_t{0x01u};
  auto T = std::make_tuple(std::forward<Values>(std::move(Ds))...);
  // Write in field order.
  OS.write(FirstByte);
  auto Bytes = IndexedWriter<0>::write(OS, T);
  assert(Bytes <= 15 && "Must only ever write at most 16 byte metadata!");
  // Pad out with appropriate numbers of zero's.
  for (; Bytes < 15; ++Bytes)
    OS.write('\0');
  return Error::success();
}

} // namespace

FDRTraceWriter::FDRTraceWriter(raw_ostream &O, const XRayFileHeader &H)
    : OS(O, support::endianness::native) {
  // We need to re-construct a header, by writing the fields we care about for
  // traces, in the format that the runtime would have written.
  uint32_t BitField =
      (H.ConstantTSC ? 0x01 : 0x0) | (H.NonstopTSC ? 0x02 : 0x0);

  // For endian-correctness, we need to write these fields in the order they
  // appear and that we expect, instead of blasting bytes of the struct through.
  OS.write(H.Version);
  OS.write(H.Type);
  OS.write(BitField);
  OS.write(H.CycleFrequency);
  ArrayRef<char> FreeFormBytes(H.FreeFormData,
                               sizeof(XRayFileHeader::FreeFormData));
  OS.write(FreeFormBytes);
}

FDRTraceWriter::~FDRTraceWriter() {}

Error FDRTraceWriter::visit(BufferExtents &R) {
  return writeMetadata<7u>(OS, R.size());
}

Error FDRTraceWriter::visit(WallclockRecord &R) {
  return writeMetadata<4u>(OS, R.seconds(), R.nanos());
}

Error FDRTraceWriter::visit(NewCPUIDRecord &R) {
  return writeMetadata<2u>(OS, R.cpuid(), R.tsc());
}

Error FDRTraceWriter::visit(TSCWrapRecord &R) {
  return writeMetadata<3u>(OS, R.tsc());
}

Error FDRTraceWriter::visit(CustomEventRecord &R) {
  if (auto E = writeMetadata<5u>(OS, R.size(), R.tsc(), R.cpu()))
    return E;
  auto D = R.data();
  ArrayRef<char> Bytes(D.data(), D.size());
  OS.write(Bytes);
  return Error::success();
}

Error FDRTraceWriter::visit(CustomEventRecordV5 &R) {
  if (auto E = writeMetadata<5u>(OS, R.size(), R.delta()))
    return E;
  auto D = R.data();
  ArrayRef<char> Bytes(D.data(), D.size());
  OS.write(Bytes);
  return Error::success();
}

Error FDRTraceWriter::visit(TypedEventRecord &R) {
  if (auto E = writeMetadata<8u>(OS, R.size(), R.delta(), R.eventType()))
    return E;
  auto D = R.data();
  ArrayRef<char> Bytes(D.data(), D.size());
  OS.write(Bytes);
  return Error::success();
}

Error FDRTraceWriter::visit(CallArgRecord &R) {
  return writeMetadata<6u>(OS, R.arg());
}

Error FDRTraceWriter::visit(PIDRecord &R) {
  return writeMetadata<9u>(OS, R.pid());
}

Error FDRTraceWriter::visit(NewBufferRecord &R) {
  return writeMetadata<0u>(OS, R.tid());
}

Error FDRTraceWriter::visit(EndBufferRecord &R) {
  return writeMetadata<1u>(OS, 0);
}

Error FDRTraceWriter::visit(FunctionRecord &R) {
  // Write out the data in "field" order, to be endian-aware.
  uint32_t TypeRecordFuncId = uint32_t{R.functionId() & ~uint32_t{0x0Fu << 28}};
  TypeRecordFuncId <<= 3;
  TypeRecordFuncId |= static_cast<uint32_t>(R.recordType());
  TypeRecordFuncId <<= 1;
  TypeRecordFuncId &= ~uint32_t{0x01};
  OS.write(TypeRecordFuncId);
  OS.write(R.delta());
  return Error::success();
}

} // namespace xray
} // namespace llvm
