//===- BinaryStreamWriter.cpp - Writes objects to a BinaryStream ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BinaryStreamWriter.h"

#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"

using namespace llvm;

BinaryStreamWriter::BinaryStreamWriter(WritableBinaryStreamRef Ref)
    : Stream(Ref) {}

BinaryStreamWriter::BinaryStreamWriter(WritableBinaryStream &Stream)
    : Stream(Stream) {}

BinaryStreamWriter::BinaryStreamWriter(MutableArrayRef<uint8_t> Data,
                                       llvm::support::endianness Endian)
    : Stream(Data, Endian) {}

Error BinaryStreamWriter::writeBytes(ArrayRef<uint8_t> Buffer) {
  if (auto EC = Stream.writeBytes(Offset, Buffer))
    return EC;
  Offset += Buffer.size();
  return Error::success();
}

Error BinaryStreamWriter::writeCString(StringRef Str) {
  if (auto EC = writeFixedString(Str))
    return EC;
  if (auto EC = writeObject('\0'))
    return EC;

  return Error::success();
}

Error BinaryStreamWriter::writeFixedString(StringRef Str) {

  return writeBytes(arrayRefFromStringRef(Str));
}

Error BinaryStreamWriter::writeStreamRef(BinaryStreamRef Ref) {
  return writeStreamRef(Ref, Ref.getLength());
}

Error BinaryStreamWriter::writeStreamRef(BinaryStreamRef Ref, uint32_t Length) {
  BinaryStreamReader SrcReader(Ref.slice(0, Length));
  // This is a bit tricky.  If we just call readBytes, we are requiring that it
  // return us the entire stream as a contiguous buffer.  There is no guarantee
  // this can be satisfied by returning a reference straight from the buffer, as
  // an implementation may not store all data in a single contiguous buffer.  So
  // we iterate over each contiguous chunk, writing each one in succession.
  while (SrcReader.bytesRemaining() > 0) {
    ArrayRef<uint8_t> Chunk;
    if (auto EC = SrcReader.readLongestContiguousChunk(Chunk))
      return EC;
    if (auto EC = writeBytes(Chunk))
      return EC;
  }
  return Error::success();
}

std::pair<BinaryStreamWriter, BinaryStreamWriter>
BinaryStreamWriter::split(uint32_t Off) const {
  assert(getLength() >= Off);

  WritableBinaryStreamRef First = Stream.drop_front(Offset);

  WritableBinaryStreamRef Second = First.drop_front(Off);
  First = First.keep_front(Off);
  BinaryStreamWriter W1{First};
  BinaryStreamWriter W2{Second};
  return std::make_pair(W1, W2);
}

Error BinaryStreamWriter::padToAlignment(uint32_t Align) {
  uint32_t NewOffset = alignTo(Offset, Align);
  if (NewOffset > getLength())
    return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
  while (Offset < NewOffset)
    if (auto EC = writeInteger('\0'))
      return EC;
  return Error::success();
}
