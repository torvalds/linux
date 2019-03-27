//===-- Decompressor.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_OBJECT_DECOMPRESSOR_H
#define LLVM_OBJECT_DECOMPRESSOR_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace object {

/// Decompressor helps to handle decompression of compressed sections.
class Decompressor {
public:
  /// Create decompressor object.
  /// @param Name        Section name.
  /// @param Data        Section content.
  /// @param IsLE        Flag determines if Data is in little endian form.
  /// @param Is64Bit     Flag determines if object is 64 bit.
  static Expected<Decompressor> create(StringRef Name, StringRef Data,
                                       bool IsLE, bool Is64Bit);

  /// Resize the buffer and uncompress section data into it.
  /// @param Out         Destination buffer.
  template <class T> Error resizeAndDecompress(T &Out) {
    Out.resize(DecompressedSize);
    return decompress({Out.data(), (size_t)DecompressedSize});
  }

  /// Uncompress section data to raw buffer provided.
  /// @param Buffer      Destination buffer.
  Error decompress(MutableArrayRef<char> Buffer);

  /// Return memory buffer size required for decompression.
  uint64_t getDecompressedSize() { return DecompressedSize; }

  /// Return true if section is compressed, including gnu-styled case.
  static bool isCompressed(const object::SectionRef &Section);

  /// Return true if section is a ELF compressed one.
  static bool isCompressedELFSection(uint64_t Flags, StringRef Name);

  /// Return true if section name matches gnu style compressed one.
  static bool isGnuStyle(StringRef Name);

private:
  Decompressor(StringRef Data);

  Error consumeCompressedGnuHeader();
  Error consumeCompressedZLibHeader(bool Is64Bit, bool IsLittleEndian);

  StringRef SectionData;
  uint64_t DecompressedSize;
};

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_DECOMPRESSOR_H
