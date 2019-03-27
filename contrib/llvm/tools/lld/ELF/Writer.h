//===- Writer.h -------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_WRITER_H
#define LLD_ELF_WRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <memory>

namespace lld {
namespace elf {
class InputFile;
class OutputSection;
class InputSectionBase;
template <class ELFT> class ObjFile;
class SymbolTable;
template <class ELFT> void writeResult();

// This describes a program header entry.
// Each contains type, access flags and range of output sections that will be
// placed in it.
struct PhdrEntry {
  PhdrEntry(unsigned Type, unsigned Flags) : p_type(Type), p_flags(Flags) {}
  void add(OutputSection *Sec);

  uint64_t p_paddr = 0;
  uint64_t p_vaddr = 0;
  uint64_t p_memsz = 0;
  uint64_t p_filesz = 0;
  uint64_t p_offset = 0;
  uint32_t p_align = 0;
  uint32_t p_type = 0;
  uint32_t p_flags = 0;

  OutputSection *FirstSec = nullptr;
  OutputSection *LastSec = nullptr;
  bool HasLMA = false;

  uint64_t LMAOffset = 0;
};

void addReservedSymbols();
llvm::StringRef getOutputSectionName(const InputSectionBase *S);

template <class ELFT> uint32_t calcMipsEFlags();

uint8_t getMipsFpAbiFlag(uint8_t OldFlag, uint8_t NewFlag,
                         llvm::StringRef FileName);

bool isMipsN32Abi(const InputFile *F);
bool isMicroMips();
bool isMipsR6();
} // namespace elf
} // namespace lld

#endif
