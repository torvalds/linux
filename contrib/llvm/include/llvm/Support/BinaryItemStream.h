//===- BinaryItemStream.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYITEMSTREAM_H
#define LLVM_SUPPORT_BINARYITEMSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

template <typename T> struct BinaryItemTraits {
  static size_t length(const T &Item) = delete;
  static ArrayRef<uint8_t> bytes(const T &Item) = delete;
};

/// BinaryItemStream represents a sequence of objects stored in some kind of
/// external container but for which it is useful to view as a stream of
/// contiguous bytes.  An example of this might be if you have a collection of
/// records and you serialize each one into a buffer, and store these serialized
/// records in a container.  The pointers themselves are not laid out
/// contiguously in memory, but we may wish to read from or write to these
/// records as if they were.
template <typename T, typename Traits = BinaryItemTraits<T>>
class BinaryItemStream : public BinaryStream {
public:
  explicit BinaryItemStream(llvm::support::endianness Endian)
      : Endian(Endian) {}

  llvm::support::endianness getEndian() const override { return Endian; }

  Error readBytes(uint32_t Offset, uint32_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    auto ExpectedIndex = translateOffsetIndex(Offset);
    if (!ExpectedIndex)
      return ExpectedIndex.takeError();
    const auto &Item = Items[*ExpectedIndex];
    if (auto EC = checkOffsetForRead(Offset, Size))
      return EC;
    if (Size > Traits::length(Item))
      return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
    Buffer = Traits::bytes(Item).take_front(Size);
    return Error::success();
  }

  Error readLongestContiguousChunk(uint32_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    auto ExpectedIndex = translateOffsetIndex(Offset);
    if (!ExpectedIndex)
      return ExpectedIndex.takeError();
    Buffer = Traits::bytes(Items[*ExpectedIndex]);
    return Error::success();
  }

  void setItems(ArrayRef<T> ItemArray) {
    Items = ItemArray;
    computeItemOffsets();
  }

  uint32_t getLength() override {
    return ItemEndOffsets.empty() ? 0 : ItemEndOffsets.back();
  }

private:
  void computeItemOffsets() {
    ItemEndOffsets.clear();
    ItemEndOffsets.reserve(Items.size());
    uint32_t CurrentOffset = 0;
    for (const auto &Item : Items) {
      uint32_t Len = Traits::length(Item);
      assert(Len > 0 && "no empty items");
      CurrentOffset += Len;
      ItemEndOffsets.push_back(CurrentOffset);
    }
  }

  Expected<uint32_t> translateOffsetIndex(uint32_t Offset) {
    // Make sure the offset is somewhere in our items array.
    if (Offset >= getLength())
      return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
    ++Offset;
    auto Iter =
        std::lower_bound(ItemEndOffsets.begin(), ItemEndOffsets.end(), Offset);
    size_t Idx = std::distance(ItemEndOffsets.begin(), Iter);
    assert(Idx < Items.size() && "binary search for offset failed");
    return Idx;
  }

  llvm::support::endianness Endian;
  ArrayRef<T> Items;

  // Sorted vector of offsets to accelerate lookup.
  std::vector<uint32_t> ItemEndOffsets;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYITEMSTREAM_H
