//===- SymbolSize.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SYMBOLSIZE_H
#define LLVM_OBJECT_SYMBOLSIZE_H

#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace object {

struct SymEntry {
  symbol_iterator I;
  uint64_t Address;
  unsigned Number;
  unsigned SectionID;
};

int compareAddress(const SymEntry *A, const SymEntry *B);

std::vector<std::pair<SymbolRef, uint64_t>>
computeSymbolSizes(const ObjectFile &O);

}
} // namespace llvm

#endif
