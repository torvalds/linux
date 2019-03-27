//===- DWARFUnitIndex.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>
#include <cstdint>

using namespace llvm;

bool DWARFUnitIndex::Header::parse(DataExtractor IndexData,
                                   uint32_t *OffsetPtr) {
  if (!IndexData.isValidOffsetForDataOfSize(*OffsetPtr, 16))
    return false;
  Version = IndexData.getU32(OffsetPtr);
  NumColumns = IndexData.getU32(OffsetPtr);
  NumUnits = IndexData.getU32(OffsetPtr);
  NumBuckets = IndexData.getU32(OffsetPtr);
  return Version <= 2;
}

void DWARFUnitIndex::Header::dump(raw_ostream &OS) const {
  OS << format("version = %u slots = %u\n\n", Version, NumBuckets);
}

bool DWARFUnitIndex::parse(DataExtractor IndexData) {
  bool b = parseImpl(IndexData);
  if (!b) {
    // Make sure we don't try to dump anything
    Header.NumBuckets = 0;
    // Release any partially initialized data.
    ColumnKinds.reset();
    Rows.reset();
  }
  return b;
}

bool DWARFUnitIndex::parseImpl(DataExtractor IndexData) {
  uint32_t Offset = 0;
  if (!Header.parse(IndexData, &Offset))
    return false;

  if (!IndexData.isValidOffsetForDataOfSize(
          Offset, Header.NumBuckets * (8 + 4) +
                      (2 * Header.NumUnits + 1) * 4 * Header.NumColumns))
    return false;

  Rows = llvm::make_unique<Entry[]>(Header.NumBuckets);
  auto Contribs =
      llvm::make_unique<Entry::SectionContribution *[]>(Header.NumUnits);
  ColumnKinds = llvm::make_unique<DWARFSectionKind[]>(Header.NumColumns);

  // Read Hash Table of Signatures
  for (unsigned i = 0; i != Header.NumBuckets; ++i)
    Rows[i].Signature = IndexData.getU64(&Offset);

  // Read Parallel Table of Indexes
  for (unsigned i = 0; i != Header.NumBuckets; ++i) {
    auto Index = IndexData.getU32(&Offset);
    if (!Index)
      continue;
    Rows[i].Index = this;
    Rows[i].Contributions =
        llvm::make_unique<Entry::SectionContribution[]>(Header.NumColumns);
    Contribs[Index - 1] = Rows[i].Contributions.get();
  }

  // Read the Column Headers
  for (unsigned i = 0; i != Header.NumColumns; ++i) {
    ColumnKinds[i] = static_cast<DWARFSectionKind>(IndexData.getU32(&Offset));
    if (ColumnKinds[i] == InfoColumnKind) {
      if (InfoColumn != -1)
        return false;
      InfoColumn = i;
    }
  }

  if (InfoColumn == -1)
    return false;

  // Read Table of Section Offsets
  for (unsigned i = 0; i != Header.NumUnits; ++i) {
    auto *Contrib = Contribs[i];
    for (unsigned i = 0; i != Header.NumColumns; ++i)
      Contrib[i].Offset = IndexData.getU32(&Offset);
  }

  // Read Table of Section Sizes
  for (unsigned i = 0; i != Header.NumUnits; ++i) {
    auto *Contrib = Contribs[i];
    for (unsigned i = 0; i != Header.NumColumns; ++i)
      Contrib[i].Length = IndexData.getU32(&Offset);
  }

  return true;
}

StringRef DWARFUnitIndex::getColumnHeader(DWARFSectionKind DS) {
#define CASE(DS)                                                               \
  case DW_SECT_##DS:                                                           \
    return #DS;
  switch (DS) {
    CASE(INFO);
    CASE(TYPES);
    CASE(ABBREV);
    CASE(LINE);
    CASE(LOC);
    CASE(STR_OFFSETS);
    CASE(MACINFO);
    CASE(MACRO);
  }
  llvm_unreachable("unknown DWARFSectionKind");
}

void DWARFUnitIndex::dump(raw_ostream &OS) const {
  if (!*this)
    return;

  Header.dump(OS);
  OS << "Index Signature         ";
  for (unsigned i = 0; i != Header.NumColumns; ++i)
    OS << ' ' << left_justify(getColumnHeader(ColumnKinds[i]), 24);
  OS << "\n----- ------------------";
  for (unsigned i = 0; i != Header.NumColumns; ++i)
    OS << " ------------------------";
  OS << '\n';
  for (unsigned i = 0; i != Header.NumBuckets; ++i) {
    auto &Row = Rows[i];
    if (auto *Contribs = Row.Contributions.get()) {
      OS << format("%5u 0x%016" PRIx64 " ", i + 1, Row.Signature);
      for (unsigned i = 0; i != Header.NumColumns; ++i) {
        auto &Contrib = Contribs[i];
        OS << format("[0x%08x, 0x%08x) ", Contrib.Offset,
                     Contrib.Offset + Contrib.Length);
      }
      OS << '\n';
    }
  }
}

const DWARFUnitIndex::Entry::SectionContribution *
DWARFUnitIndex::Entry::getOffset(DWARFSectionKind Sec) const {
  uint32_t i = 0;
  for (; i != Index->Header.NumColumns; ++i)
    if (Index->ColumnKinds[i] == Sec)
      return &Contributions[i];
  return nullptr;
}

const DWARFUnitIndex::Entry::SectionContribution *
DWARFUnitIndex::Entry::getOffset() const {
  return &Contributions[Index->InfoColumn];
}

const DWARFUnitIndex::Entry *
DWARFUnitIndex::getFromOffset(uint32_t Offset) const {
  if (OffsetLookup.empty()) {
    for (uint32_t i = 0; i != Header.NumBuckets; ++i)
      if (Rows[i].Contributions)
        OffsetLookup.push_back(&Rows[i]);
    llvm::sort(OffsetLookup, [&](Entry *E1, Entry *E2) {
      return E1->Contributions[InfoColumn].Offset <
             E2->Contributions[InfoColumn].Offset;
    });
  }
  auto I =
      llvm::upper_bound(OffsetLookup, Offset, [&](uint32_t Offset, Entry *E2) {
        return Offset < E2->Contributions[InfoColumn].Offset;
      });
  if (I == OffsetLookup.begin())
    return nullptr;
  --I;
  const auto *E = *I;
  const auto &InfoContrib = E->Contributions[InfoColumn];
  if ((InfoContrib.Offset + InfoContrib.Length) <= Offset)
    return nullptr;
  return E;
}

const DWARFUnitIndex::Entry *DWARFUnitIndex::getFromHash(uint64_t S) const {
  uint64_t Mask = Header.NumBuckets - 1;

  auto H = S & Mask;
  auto HP = ((S >> 32) & Mask) | 1;
  while (Rows[H].getSignature() != S && Rows[H].getSignature() != 0)
    H = (H + HP) & Mask;

  if (Rows[H].getSignature() != S)
    return nullptr;

  return &Rows[H];
}
