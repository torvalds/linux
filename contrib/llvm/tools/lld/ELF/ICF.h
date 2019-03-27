//===- ICF.h --------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_ICF_H
#define LLD_ELF_ICF_H

namespace lld {
namespace elf {

template <class ELFT> void doIcf();

} // namespace elf
} // namespace lld

#endif
