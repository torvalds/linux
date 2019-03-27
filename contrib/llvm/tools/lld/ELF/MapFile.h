//===- MapFile.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_MAPFILE_H
#define LLD_ELF_MAPFILE_H

namespace lld {
namespace elf {
void writeMapFile();
void writeCrossReferenceTable();
} // namespace elf
} // namespace lld

#endif
