//===- BinaryStreamReader.cpp - Reads objects from a binary stream --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BinaryStreamReader.h"

#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/BinaryStreamRef.h"

using namespace llvm;
using endianness = llvm::support::endianness;

BinaryStreamReader::BinaryStreamReader(BinaryStreamRef Ref) : Stream(Ref) {}

BinaryStreamReader::BinaryStreamReader(BinaryStream &Stream) : Stream(Stream) {}

BinaryStreamReader::BinaryStreamReader(ArrayRef<uint8_t> Data,
                                       endianness Endian)
    : Stream(Data, Endian) {}

BinaryStreamReader::BinaryStreamReader(StringRef Data, endianness Endian)
    : Stream(Data, Endian) {}

Error BinaryStreamReader::readLongestContiguousChunk(
    ArrayRef<uint8_t> &Buffer) {
  if (auto EC = Stream.readLongestContiguousChunk(Offset, Buffer))
    return EC;
  Offset += Buffer.size();
  return Error::success();
}

Error BinaryStreamReader::readBytes(ArrayRef<uint8_t> &Buffer, uint32_t Size) {
  if (auto EC = Stream.readBytes(Offset, Size, Buffer))
    return EC;
  Offset += Size;
  return Error::success();
}

Error BinaryStreamReader::readCString(StringRef &Dest) {
  uint32_t OriginalOffset = getOffset();
  uint32_t FoundOffset = 0;
  while (true) {
    uint32_t ThisOffset = getOffset();
    ArrayRef<uint8_t> Buffer;
    if (auto EC = readLongestContiguousChunk(Buffer))
      return EC;
    StringRef S(reinterpret_cast<const char *>(Buffer.begin()), Buffer.size());
    size_t Pos = S.find_first_of('\0');
    if (LLVM_LIKELY(Pos != StringRef::npos)) {
      FoundOffset = Pos + ThisOffset;
      break;
    }
  }
  assert(FoundOffset >= OriginalOffset);

  setOffset(OriginalOffset);
  size_t Length = FoundOffset - OriginalOffset;

  if (auto EC = readFixedString(Dest, Length))
    return EC;

  // Now set the offset back to after the null terminator.
  setOffset(FoundOffset + 1);
  return Error::success();
}

Error BinaryStreamReader::readWideString(ArrayRef<UTF16> &Dest) {
  uint32_t Length = 0;
  uint32_t OriginalOffset = getOffset();
  const UTF16 *C;
  while (true) {
    if (auto EC = readObject(C))
      return EC;
    if (*C == 0x0000)
      break;
    ++Length;
  }
  uint32_t NewOffset = getOffset();
  setOffset(OriginalOffset);

  if (auto EC = readArray(Dest, Length))
    return EC;
  setOffset(NewOffset);
  return Error::success();
}

Error BinaryStreamReader::readFixedString(StringRef &Dest, uint32_t Length) {
  ArrayRef<uint8_t> Bytes;
  if (auto EC = readBytes(Bytes, Length))
    return EC;
  Dest = StringRef(reinterpret_cast<const char *>(Bytes.begin()), Bytes.size());
  return Error::success();
}

Error BinaryStreamReader::readStreamRef(BinaryStreamRef &Ref) {
  return readStreamRef(Ref, bytesRemaining());
}

Error BinaryStreamReader::readStreamRef(BinaryStreamRef &Ref, uint32_t Length) {
  if (bytesRemaining() < Length)
    return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
  Ref = Stream.slice(Offset, Length);
  Offset += Length;
  return Error::success();
}

Error BinaryStreamReader::readSubstream(BinarySubstreamRef &Stream,
                                        uint32_t Size) {
  Stream.Offset = getOffset();
  return readStreamRef(Stream.StreamData, Size);
}

Error BinaryStreamReader::skip(uint32_t Amount) {
  if (Amount > bytesRemaining())
    return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
  Offset += Amount;
  return Error::success();
}

Error BinaryStreamReader::padToAlignment(uint32_t Align) {
  uint32_t NewOffset = alignTo(Offset, Align);
  return skip(NewOffset - Offset);
}

uint8_t BinaryStreamReader::peek() const {
  ArrayRef<uint8_t> Buffer;
  auto EC = Stream.readBytes(Offset, 1, Buffer);
  assert(!EC && "Cannot peek an empty buffer!");
  llvm::consumeError(std::move(EC));
  return Buffer[0];
}

std::pair<BinaryStreamReader, BinaryStreamReader>
BinaryStreamReader::split(uint32_t Off) const {
  assert(getLength() >= Off);

  BinaryStreamRef First = Stream.drop_front(Offset);

  BinaryStreamRef Second = First.drop_front(Off);
  First = First.keep_front(Off);
  BinaryStreamReader W1{First};
  BinaryStreamReader W2{Second};
  return std::make_pair(W1, W2);
}