//===- Bits.h ---------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_BITS_H
#define LLD_ELF_BITS_H

#include "Config.h"
#include "llvm/Support/Endian.h"

namespace lld {
namespace elf {

inline uint64_t readUint(uint8_t *Buf) {
  if (Config->Is64)
    return llvm::support::endian::read64(Buf, Config->Endianness);
  return llvm::support::endian::read32(Buf, Config->Endianness);
}

inline void writeUint(uint8_t *Buf, uint64_t Val) {
  if (Config->Is64)
    llvm::support::endian::write64(Buf, Val, Config->Endianness);
  else
    llvm::support::endian::write32(Buf, Val, Config->Endianness);
}

} // namespace elf
} // namespace lld

#endif
