//===- lib/ReaderWriter/MachO/MachONormalizedFileBinaryUtils.h ------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H
#define LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H

#include "MachONormalizedFile.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/Error.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/LEB128.h"
#include <system_error>

namespace lld {
namespace mach_o {
namespace normalized {

class ByteBuffer {
public:
  ByteBuffer() : _ostream(_bytes) { }

  void append_byte(uint8_t b) {
    _ostream << b;
  }
  void append_uleb128(uint64_t value) {
    llvm::encodeULEB128(value, _ostream);
  }
  void append_uleb128Fixed(uint64_t value, unsigned byteCount) {
    unsigned min = llvm::getULEB128Size(value);
    assert(min <= byteCount);
    unsigned pad = byteCount - min;
    llvm::encodeULEB128(value, _ostream, pad);
  }
  void append_sleb128(int64_t value) {
    llvm::encodeSLEB128(value, _ostream);
  }
  void append_string(StringRef str) {
    _ostream << str;
    append_byte(0);
  }
  void align(unsigned alignment) {
    while ( (_ostream.tell() % alignment) != 0 )
      append_byte(0);
  }
  size_t size() {
    return _ostream.tell();
  }
  const uint8_t *bytes() {
    return reinterpret_cast<const uint8_t*>(_ostream.str().data());
  }

private:
  SmallVector<char, 128>        _bytes;
  // Stream ivar must be after SmallVector ivar to construct properly.
  llvm::raw_svector_ostream     _ostream;
};

using namespace llvm::support::endian;
using llvm::sys::getSwappedBytes;

template<typename T>
static inline uint16_t read16(const T *loc, bool isBig) {
  assert((uint64_t)loc % alignof(T) == 0 && "invalid pointer alignment");
  return isBig ? read16be(loc) : read16le(loc);
}

template<typename T>
static inline uint32_t read32(const T *loc, bool isBig) {
  assert((uint64_t)loc % alignof(T) == 0 && "invalid pointer alignment");
  return isBig ? read32be(loc) : read32le(loc);
}

template<typename T>
static inline uint64_t read64(const T *loc, bool isBig) {
  assert((uint64_t)loc % alignof(T) == 0 && "invalid pointer alignment");
  return isBig ? read64be(loc) : read64le(loc);
}

inline void write16(uint8_t *loc, uint16_t value, bool isBig) {
  if (isBig)
    write16be(loc, value);
  else
    write16le(loc, value);
}

inline void write32(uint8_t *loc, uint32_t value, bool isBig) {
  if (isBig)
    write32be(loc, value);
  else
    write32le(loc, value);
}

inline void write64(uint8_t *loc, uint64_t value, bool isBig) {
  if (isBig)
    write64be(loc, value);
  else
    write64le(loc, value);
}

inline uint32_t
bitFieldExtract(uint32_t value, bool isBigEndianBigField, uint8_t firstBit,
                                                          uint8_t bitCount) {
  const uint32_t mask = ((1<<bitCount)-1);
  const uint8_t shift = isBigEndianBigField ? (32-firstBit-bitCount) : firstBit;
  return (value >> shift) & mask;
}

inline void
bitFieldSet(uint32_t &bits, bool isBigEndianBigField, uint32_t newBits,
                            uint8_t firstBit, uint8_t bitCount) {
  const uint32_t mask = ((1<<bitCount)-1);
  assert((newBits & mask) == newBits);
  const uint8_t shift = isBigEndianBigField ? (32-firstBit-bitCount) : firstBit;
  bits &= ~(mask << shift);
  bits |= (newBits << shift);
}

inline Relocation unpackRelocation(const llvm::MachO::any_relocation_info &r,
                                   bool isBigEndian) {
  uint32_t r0 = read32(&r.r_word0, isBigEndian);
  uint32_t r1 = read32(&r.r_word1, isBigEndian);

  Relocation result;
  if (r0 & llvm::MachO::R_SCATTERED) {
    // scattered relocation record always laid out like big endian bit field
    result.offset     = bitFieldExtract(r0, true, 8, 24);
    result.scattered  = true;
    result.type       = (RelocationInfoType)
                        bitFieldExtract(r0, true, 4, 4);
    result.length     = bitFieldExtract(r0, true, 2, 2);
    result.pcRel      = bitFieldExtract(r0, true, 1, 1);
    result.isExtern   = false;
    result.value      = r1;
    result.symbol     = 0;
  } else {
    result.offset     = r0;
    result.scattered  = false;
    result.type       = (RelocationInfoType)
                        bitFieldExtract(r1, isBigEndian, 28, 4);
    result.length     = bitFieldExtract(r1, isBigEndian, 25, 2);
    result.pcRel      = bitFieldExtract(r1, isBigEndian, 24, 1);
    result.isExtern   = bitFieldExtract(r1, isBigEndian, 27, 1);
    result.value      = 0;
    result.symbol     = bitFieldExtract(r1, isBigEndian, 0, 24);
  }
  return result;
}


inline llvm::MachO::any_relocation_info
packRelocation(const Relocation &r, bool swap, bool isBigEndian) {
  uint32_t r0 = 0;
  uint32_t r1 = 0;

  if (r.scattered) {
    r1 = r.value;
    bitFieldSet(r0, true, r.offset,    8, 24);
    bitFieldSet(r0, true, r.type,      4, 4);
    bitFieldSet(r0, true, r.length,    2, 2);
    bitFieldSet(r0, true, r.pcRel,     1, 1);
    bitFieldSet(r0, true, r.scattered, 0, 1); // R_SCATTERED
  } else {
    r0 = r.offset;
    bitFieldSet(r1, isBigEndian, r.type,     28, 4);
    bitFieldSet(r1, isBigEndian, r.isExtern, 27, 1);
    bitFieldSet(r1, isBigEndian, r.length,   25, 2);
    bitFieldSet(r1, isBigEndian, r.pcRel,    24, 1);
    bitFieldSet(r1, isBigEndian, r.symbol,   0,  24);
  }

  llvm::MachO::any_relocation_info result;
  result.r_word0 = swap ? getSwappedBytes(r0) : r0;
  result.r_word1 = swap ? getSwappedBytes(r1) : r1;
  return result;
}

inline StringRef getString16(const char s[16]) {
  // The StringRef(const char *) constructor passes the const char * to
  // strlen(), so we can't use this constructor here, because if there is no
  // null terminator in s, then strlen() will read past the end of the array.
  return StringRef(s, strnlen(s, 16));
}

inline void setString16(StringRef str, char s[16]) {
  memset(s, 0, 16);
  memcpy(s, str.begin(), (str.size() > 16) ? 16: str.size());
}

// Implemented in normalizedToAtoms() and used by normalizedFromAtoms() so
// that the same table can be used to map mach-o sections to and from
// DefinedAtom::ContentType.
void relocatableSectionInfoForContentType(DefinedAtom::ContentType atomType,
                                          StringRef &segmentName,
                                          StringRef &sectionName,
                                          SectionType &sectionType,
                                          SectionAttr &sectionAttrs,
                                          bool &relocsToDefinedCanBeImplicit);

} // namespace normalized
} // namespace mach_o
} // namespace lld

#endif // LLD_READER_WRITER_MACHO_NORMALIZED_FILE_BINARY_UTILS_H
