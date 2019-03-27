//===-- Decompressor.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Decompressor.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace object;

Expected<Decompressor> Decompressor::create(StringRef Name, StringRef Data,
                                            bool IsLE, bool Is64Bit) {
  if (!zlib::isAvailable())
    return createError("zlib is not available");

  Decompressor D(Data);
  Error Err = isGnuStyle(Name) ? D.consumeCompressedGnuHeader()
                               : D.consumeCompressedZLibHeader(Is64Bit, IsLE);
  if (Err)
    return std::move(Err);
  return D;
}

Decompressor::Decompressor(StringRef Data)
    : SectionData(Data), DecompressedSize(0) {}

Error Decompressor::consumeCompressedGnuHeader() {
  if (!SectionData.startswith("ZLIB"))
    return createError("corrupted compressed section header");

  SectionData = SectionData.substr(4);

  // Consume uncompressed section size (big-endian 8 bytes).
  if (SectionData.size() < 8)
    return createError("corrupted uncompressed section size");
  DecompressedSize = read64be(SectionData.data());
  SectionData = SectionData.substr(8);

  return Error::success();
}

Error Decompressor::consumeCompressedZLibHeader(bool Is64Bit,
                                                bool IsLittleEndian) {
  using namespace ELF;
  uint64_t HdrSize = Is64Bit ? sizeof(Elf64_Chdr) : sizeof(Elf32_Chdr);
  if (SectionData.size() < HdrSize)
    return createError("corrupted compressed section header");

  DataExtractor Extractor(SectionData, IsLittleEndian, 0);
  uint32_t Offset = 0;
  if (Extractor.getUnsigned(&Offset, Is64Bit ? sizeof(Elf64_Word)
                                             : sizeof(Elf32_Word)) !=
      ELFCOMPRESS_ZLIB)
    return createError("unsupported compression type");

  // Skip Elf64_Chdr::ch_reserved field.
  if (Is64Bit)
    Offset += sizeof(Elf64_Word);

  DecompressedSize = Extractor.getUnsigned(
      &Offset, Is64Bit ? sizeof(Elf64_Xword) : sizeof(Elf32_Word));
  SectionData = SectionData.substr(HdrSize);
  return Error::success();
}

bool Decompressor::isGnuStyle(StringRef Name) {
  return Name.startswith(".zdebug");
}

bool Decompressor::isCompressed(const object::SectionRef &Section) {
  StringRef Name;
  if (Section.getName(Name))
    return false;
  return Section.isCompressed() || isGnuStyle(Name);
}

bool Decompressor::isCompressedELFSection(uint64_t Flags, StringRef Name) {
  return (Flags & ELF::SHF_COMPRESSED) || isGnuStyle(Name);
}

Error Decompressor::decompress(MutableArrayRef<char> Buffer) {
  size_t Size = Buffer.size();
  return zlib::uncompress(SectionData, Buffer.data(), Size);
}
