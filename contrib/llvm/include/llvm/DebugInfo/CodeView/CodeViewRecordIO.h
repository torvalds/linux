//===- CodeViewRecordIO.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace llvm {
namespace codeview {

class CodeViewRecordIO {
  uint32_t getCurrentOffset() const {
    return (isWriting()) ? Writer->getOffset() : Reader->getOffset();
  }

public:
  explicit CodeViewRecordIO(BinaryStreamReader &Reader) : Reader(&Reader) {}
  explicit CodeViewRecordIO(BinaryStreamWriter &Writer) : Writer(&Writer) {}

  Error beginRecord(Optional<uint32_t> MaxLength);
  Error endRecord();

  Error mapInteger(TypeIndex &TypeInd);

  bool isReading() const { return Reader != nullptr; }
  bool isWriting() const { return !isReading(); }

  uint32_t maxFieldLength() const;

  template <typename T> Error mapObject(T &Value) {
    if (isWriting())
      return Writer->writeObject(Value);

    const T *ValuePtr;
    if (auto EC = Reader->readObject(ValuePtr))
      return EC;
    Value = *ValuePtr;
    return Error::success();
  }

  template <typename T> Error mapInteger(T &Value) {
    if (isWriting())
      return Writer->writeInteger(Value);

    return Reader->readInteger(Value);
  }

  template <typename T> Error mapEnum(T &Value) {
    if (sizeof(Value) > maxFieldLength())
      return make_error<CodeViewError>(cv_error_code::insufficient_buffer);

    using U = typename std::underlying_type<T>::type;
    U X;
    if (isWriting())
      X = static_cast<U>(Value);

    if (auto EC = mapInteger(X))
      return EC;
    if (isReading())
      Value = static_cast<T>(X);
    return Error::success();
  }

  Error mapEncodedInteger(int64_t &Value);
  Error mapEncodedInteger(uint64_t &Value);
  Error mapEncodedInteger(APSInt &Value);
  Error mapStringZ(StringRef &Value);
  Error mapGuid(GUID &Guid);

  Error mapStringZVectorZ(std::vector<StringRef> &Value);

  template <typename SizeType, typename T, typename ElementMapper>
  Error mapVectorN(T &Items, const ElementMapper &Mapper) {
    SizeType Size;
    if (isWriting()) {
      Size = static_cast<SizeType>(Items.size());
      if (auto EC = Writer->writeInteger(Size))
        return EC;

      for (auto &X : Items) {
        if (auto EC = Mapper(*this, X))
          return EC;
      }
    } else {
      if (auto EC = Reader->readInteger(Size))
        return EC;
      for (SizeType I = 0; I < Size; ++I) {
        typename T::value_type Item;
        if (auto EC = Mapper(*this, Item))
          return EC;
        Items.push_back(Item);
      }
    }

    return Error::success();
  }

  template <typename T, typename ElementMapper>
  Error mapVectorTail(T &Items, const ElementMapper &Mapper) {
    if (isWriting()) {
      for (auto &Item : Items) {
        if (auto EC = Mapper(*this, Item))
          return EC;
      }
    } else {
      typename T::value_type Field;
      // Stop when we run out of bytes or we hit record padding bytes.
      while (!Reader->empty() && Reader->peek() < 0xf0 /* LF_PAD0 */) {
        if (auto EC = Mapper(*this, Field))
          return EC;
        Items.push_back(Field);
      }
    }
    return Error::success();
  }

  Error mapByteVectorTail(ArrayRef<uint8_t> &Bytes);
  Error mapByteVectorTail(std::vector<uint8_t> &Bytes);

  Error padToAlignment(uint32_t Align);
  Error skipPadding();

private:
  Error writeEncodedSignedInteger(const int64_t &Value);
  Error writeEncodedUnsignedInteger(const uint64_t &Value);

  struct RecordLimit {
    uint32_t BeginOffset;
    Optional<uint32_t> MaxLength;

    Optional<uint32_t> bytesRemaining(uint32_t CurrentOffset) const {
      if (!MaxLength.hasValue())
        return None;
      assert(CurrentOffset >= BeginOffset);

      uint32_t BytesUsed = CurrentOffset - BeginOffset;
      if (BytesUsed >= *MaxLength)
        return 0;
      return *MaxLength - BytesUsed;
    }
  };

  SmallVector<RecordLimit, 2> Limits;

  BinaryStreamReader *Reader = nullptr;
  BinaryStreamWriter *Writer = nullptr;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
