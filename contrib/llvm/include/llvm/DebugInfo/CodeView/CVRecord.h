//===- RecordIterator.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_RECORDITERATOR_H
#define LLVM_DEBUGINFO_CODEVIEW_RECORDITERATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

namespace codeview {

template <typename Kind> class CVRecord {
public:
  CVRecord() : Type(static_cast<Kind>(0)) {}

  CVRecord(Kind K, ArrayRef<uint8_t> Data) : Type(K), RecordData(Data) {}

  bool valid() const { return Type != static_cast<Kind>(0); }

  uint32_t length() const { return RecordData.size(); }
  Kind kind() const { return Type; }
  ArrayRef<uint8_t> data() const { return RecordData; }
  StringRef str_data() const {
    return StringRef(reinterpret_cast<const char *>(RecordData.data()),
                     RecordData.size());
  }

  ArrayRef<uint8_t> content() const {
    return RecordData.drop_front(sizeof(RecordPrefix));
  }

  Kind Type;
  ArrayRef<uint8_t> RecordData;
};

template <typename Kind> struct RemappedRecord {
  explicit RemappedRecord(const CVRecord<Kind> &R) : OriginalRecord(R) {}

  CVRecord<Kind> OriginalRecord;
  SmallVector<std::pair<uint32_t, TypeIndex>, 8> Mappings;
};

template <typename Record, typename Func>
Error forEachCodeViewRecord(ArrayRef<uint8_t> StreamBuffer, Func F) {
  while (!StreamBuffer.empty()) {
    if (StreamBuffer.size() < sizeof(RecordPrefix))
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    const RecordPrefix *Prefix =
        reinterpret_cast<const RecordPrefix *>(StreamBuffer.data());

    size_t RealLen = Prefix->RecordLen + 2;
    if (StreamBuffer.size() < RealLen)
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    ArrayRef<uint8_t> Data = StreamBuffer.take_front(RealLen);
    StreamBuffer = StreamBuffer.drop_front(RealLen);

    Record R(static_cast<decltype(Record::Type)>((uint16_t)Prefix->RecordKind),
             Data);
    if (auto EC = F(R))
      return EC;
  }
  return Error::success();
}

/// Read a complete record from a stream at a random offset.
template <typename Kind>
inline Expected<CVRecord<Kind>> readCVRecordFromStream(BinaryStreamRef Stream,
                                                       uint32_t Offset) {
  const RecordPrefix *Prefix = nullptr;
  BinaryStreamReader Reader(Stream);
  Reader.setOffset(Offset);

  if (auto EC = Reader.readObject(Prefix))
    return std::move(EC);
  if (Prefix->RecordLen < 2)
    return make_error<CodeViewError>(cv_error_code::corrupt_record);
  Kind K = static_cast<Kind>(uint16_t(Prefix->RecordKind));

  Reader.setOffset(Offset);
  ArrayRef<uint8_t> RawData;
  if (auto EC = Reader.readBytes(RawData, Prefix->RecordLen + sizeof(uint16_t)))
    return std::move(EC);
  return codeview::CVRecord<Kind>(K, RawData);
}

} // end namespace codeview

template <typename Kind>
struct VarStreamArrayExtractor<codeview::CVRecord<Kind>> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   codeview::CVRecord<Kind> &Item) {
    auto ExpectedRec = codeview::readCVRecordFromStream<Kind>(Stream, 0);
    if (!ExpectedRec)
      return ExpectedRec.takeError();
    Item = *ExpectedRec;
    Len = ExpectedRec->length();
    return Error::success();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_RECORDITERATOR_H
