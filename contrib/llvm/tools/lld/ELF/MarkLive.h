//===- MarkLive.h -----------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_MARKLIVE_H
#define LLD_ELF_MARKLIVE_H

namespace lld {
namespace elf {

template <class ELFT> void markLive();

} // namespace elf
} // namespace lld

#endif // LLD_ELF_MARKLIVE_H
