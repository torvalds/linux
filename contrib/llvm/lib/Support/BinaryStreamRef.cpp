//===- BinaryStreamRef.cpp - ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/BinaryByteStream.h"

using namespace llvm;
using namespace llvm::support;

namespace {

class ArrayRefImpl : public BinaryStream {
public:
  ArrayRefImpl(ArrayRef<uint8_t> Data, endianness Endian) : BBS(Data, Endian) {}

  llvm::support::endianness getEndian() const override {
    return BBS.getEndian();
  }
  Error readBytes(uint32_t Offset, uint32_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    return BBS.readBytes(Offset, Size, Buffer);
  }
  Error readLongestContiguousChunk(uint32_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    return BBS.readLongestContiguousChunk(Offset, Buffer);
  }
  uint32_t getLength() override { return BBS.getLength(); }

private:
  BinaryByteStream BBS;
};

class MutableArrayRefImpl : public WritableBinaryStream {
public:
  MutableArrayRefImpl(MutableArrayRef<uint8_t> Data, endianness Endian)
      : BBS(Data, Endian) {}

  // Inherited via WritableBinaryStream
  llvm::support::endianness getEndian() const override {
    return BBS.getEndian();
  }
  Error readBytes(uint32_t Offset, uint32_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    return BBS.readBytes(Offset, Size, Buffer);
  }
  Error readLongestContiguousChunk(uint32_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    return BBS.readLongestContiguousChunk(Offset, Buffer);
  }
  uint32_t getLength() override { return BBS.getLength(); }

  Error writeBytes(uint32_t Offset, ArrayRef<uint8_t> Data) override {
    return BBS.writeBytes(Offset, Data);
  }
  Error commit() override { return BBS.commit(); }

private:
  MutableBinaryByteStream BBS;
};
}

BinaryStreamRef::BinaryStreamRef(BinaryStream &Stream)
    : BinaryStreamRefBase(Stream) {}
BinaryStreamRef::BinaryStreamRef(BinaryStream &Stream, uint32_t Offset,
                                 Optional<uint32_t> Length)
    : BinaryStreamRefBase(Stream, Offset, Length) {}
BinaryStreamRef::BinaryStreamRef(ArrayRef<uint8_t> Data, endianness Endian)
    : BinaryStreamRefBase(std::make_shared<ArrayRefImpl>(Data, Endian), 0,
                          Data.size()) {}
BinaryStreamRef::BinaryStreamRef(StringRef Data, endianness Endian)
    : BinaryStreamRef(makeArrayRef(Data.bytes_begin(), Data.bytes_end()),
                      Endian) {}

Error BinaryStreamRef::readBytes(uint32_t Offset, uint32_t Size,
                                 ArrayRef<uint8_t> &Buffer) const {
  if (auto EC = checkOffsetForRead(Offset, Size))
    return EC;
  return BorrowedImpl->readBytes(ViewOffset + Offset, Size, Buffer);
}

Error BinaryStreamRef::readLongestContiguousChunk(
    uint32_t Offset, ArrayRef<uint8_t> &Buffer) const {
  if (auto EC = checkOffsetForRead(Offset, 1))
    return EC;

  if (auto EC =
          BorrowedImpl->readLongestContiguousChunk(ViewOffset + Offset, Buffer))
    return EC;
  // This StreamRef might refer to a smaller window over a larger stream.  In
  // that case we will have read out more bytes than we should return, because
  // we should not read past the end of the current view.
  uint32_t MaxLength = getLength() - Offset;
  if (Buffer.size() > MaxLength)
    Buffer = Buffer.slice(0, MaxLength);
  return Error::success();
}

WritableBinaryStreamRef::WritableBinaryStreamRef(WritableBinaryStream &Stream)
    : BinaryStreamRefBase(Stream) {}

WritableBinaryStreamRef::WritableBinaryStreamRef(WritableBinaryStream &Stream,
                                                 uint32_t Offset,
                                                 Optional<uint32_t> Length)
    : BinaryStreamRefBase(Stream, Offset, Length) {}

WritableBinaryStreamRef::WritableBinaryStreamRef(MutableArrayRef<uint8_t> Data,
                                                 endianness Endian)
    : BinaryStreamRefBase(std::make_shared<MutableArrayRefImpl>(Data, Endian),
                          0, Data.size()) {}


Error WritableBinaryStreamRef::writeBytes(uint32_t Offset,
                                          ArrayRef<uint8_t> Data) const {
  if (auto EC = checkOffsetForWrite(Offset, Data.size()))
    return EC;

  return BorrowedImpl->writeBytes(ViewOffset + Offset, Data);
}

WritableBinaryStreamRef::operator BinaryStreamRef() const {
  return BinaryStreamRef(*BorrowedImpl, ViewOffset, Length);
}

/// For buffered streams, commits changes to the backing store.
Error WritableBinaryStreamRef::commit() { return BorrowedImpl->commit(); }
