//===- SymbolSize.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/SymbolSize.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"

using namespace llvm;
using namespace object;

// Orders increasingly by (SectionID, Address).
int llvm::object::compareAddress(const SymEntry *A, const SymEntry *B) {
  if (A->SectionID != B->SectionID)
    return A->SectionID < B->SectionID ? -1 : 1;
  if (A->Address != B->Address)
    return A->Address < B->Address ? -1 : 1;
  return 0;
}

static unsigned getSectionID(const ObjectFile &O, SectionRef Sec) {
  if (auto *M = dyn_cast<MachOObjectFile>(&O))
    return M->getSectionID(Sec);
  return cast<COFFObjectFile>(O).getSectionID(Sec);
}

static unsigned getSymbolSectionID(const ObjectFile &O, SymbolRef Sym) {
  if (auto *M = dyn_cast<MachOObjectFile>(&O))
    return M->getSymbolSectionID(Sym);
  return cast<COFFObjectFile>(O).getSymbolSectionID(Sym);
}

std::vector<std::pair<SymbolRef, uint64_t>>
llvm::object::computeSymbolSizes(const ObjectFile &O) {
  std::vector<std::pair<SymbolRef, uint64_t>> Ret;

  if (const auto *E = dyn_cast<ELFObjectFileBase>(&O)) {
    auto Syms = E->symbols();
    if (Syms.begin() == Syms.end())
      Syms = E->getDynamicSymbolIterators();
    for (ELFSymbolRef Sym : Syms)
      Ret.push_back({Sym, Sym.getSize()});
    return Ret;
  }

  // Collect sorted symbol addresses. Include dummy addresses for the end
  // of each section.
  std::vector<SymEntry> Addresses;
  unsigned SymNum = 0;
  for (symbol_iterator I = O.symbol_begin(), E = O.symbol_end(); I != E; ++I) {
    SymbolRef Sym = *I;
    uint64_t Value = Sym.getValue();
    Addresses.push_back({I, Value, SymNum, getSymbolSectionID(O, Sym)});
    ++SymNum;
  }
  for (SectionRef Sec : O.sections()) {
    uint64_t Address = Sec.getAddress();
    uint64_t Size = Sec.getSize();
    Addresses.push_back(
        {O.symbol_end(), Address + Size, 0, getSectionID(O, Sec)});
  }

  if (Addresses.empty())
    return Ret;

  array_pod_sort(Addresses.begin(), Addresses.end(), compareAddress);

  // Compute the size as the gap to the next symbol
  for (unsigned I = 0, N = Addresses.size() - 1; I < N; ++I) {
    auto &P = Addresses[I];
    if (P.I == O.symbol_end())
      continue;

    // If multiple symbol have the same address, give both the same size.
    unsigned NextI = I + 1;
    while (NextI < N && Addresses[NextI].Address == P.Address)
      ++NextI;

    uint64_t Size = Addresses[NextI].Address - P.Address;
    P.Address = Size;
  }

  // Assign the sorted symbols in the original order.
  Ret.resize(SymNum);
  for (SymEntry &P : Addresses) {
    if (P.I == O.symbol_end())
      continue;
    Ret[P.Number] = {*P.I, P.Address};
  }
  return Ret;
}
