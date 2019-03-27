//===- llvm/CodeGen/DwarfStringPool.cpp - Dwarf Debug Framework -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DwarfStringPool.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include <cassert>
#include <utility>

using namespace llvm;

DwarfStringPool::DwarfStringPool(BumpPtrAllocator &A, AsmPrinter &Asm,
                                 StringRef Prefix)
    : Pool(A), Prefix(Prefix),
      ShouldCreateSymbols(Asm.MAI->doesDwarfUseRelocationsAcrossSections()) {}

StringMapEntry<DwarfStringPool::EntryTy> &
DwarfStringPool::getEntryImpl(AsmPrinter &Asm, StringRef Str) {
  auto I = Pool.insert(std::make_pair(Str, EntryTy()));
  auto &Entry = I.first->second;
  if (I.second) {
    Entry.Index = EntryTy::NotIndexed;
    Entry.Offset = NumBytes;
    Entry.Symbol = ShouldCreateSymbols ? Asm.createTempSymbol(Prefix) : nullptr;

    NumBytes += Str.size() + 1;
    assert(NumBytes > Entry.Offset && "Unexpected overflow");
  }
  return *I.first;
}

DwarfStringPool::EntryRef DwarfStringPool::getEntry(AsmPrinter &Asm,
                                                    StringRef Str) {
  auto &MapEntry = getEntryImpl(Asm, Str);
  return EntryRef(MapEntry, false);
}

DwarfStringPool::EntryRef DwarfStringPool::getIndexedEntry(AsmPrinter &Asm,
                                                           StringRef Str) {
  auto &MapEntry = getEntryImpl(Asm, Str);
  if (!MapEntry.getValue().isIndexed())
    MapEntry.getValue().Index = NumIndexedStrings++;
  return EntryRef(MapEntry, true);
}

void DwarfStringPool::emitStringOffsetsTableHeader(AsmPrinter &Asm,
                                                   MCSection *Section,
                                                   MCSymbol *StartSym) {
  if (getNumIndexedStrings() == 0)
    return;
  Asm.OutStreamer->SwitchSection(Section);
  unsigned EntrySize = 4;
  // FIXME: DWARF64
  // We are emitting the header for a contribution to the string offsets
  // table. The header consists of an entry with the contribution's
  // size (not including the size of the length field), the DWARF version and
  // 2 bytes of padding.
  Asm.emitInt32(getNumIndexedStrings() * EntrySize + 4);
  Asm.emitInt16(Asm.getDwarfVersion());
  Asm.emitInt16(0);
  // Define the symbol that marks the start of the contribution. It is
  // referenced by most unit headers via DW_AT_str_offsets_base.
  // Split units do not use the attribute.
  if (StartSym)
    Asm.OutStreamer->EmitLabel(StartSym);
}

void DwarfStringPool::emit(AsmPrinter &Asm, MCSection *StrSection,
                           MCSection *OffsetSection, bool UseRelativeOffsets) {
  if (Pool.empty())
    return;

  // Start the dwarf str section.
  Asm.OutStreamer->SwitchSection(StrSection);

  // Get all of the string pool entries and sort them by their offset.
  SmallVector<const StringMapEntry<EntryTy> *, 64> Entries;
  Entries.reserve(Pool.size());

  for (const auto &E : Pool)
    Entries.push_back(&E);

  llvm::sort(Entries, [](const StringMapEntry<EntryTy> *A,
                         const StringMapEntry<EntryTy> *B) {
    return A->getValue().Offset < B->getValue().Offset;
  });

  for (const auto &Entry : Entries) {
    assert(ShouldCreateSymbols == static_cast<bool>(Entry->getValue().Symbol) &&
           "Mismatch between setting and entry");

    // Emit a label for reference from debug information entries.
    if (ShouldCreateSymbols)
      Asm.OutStreamer->EmitLabel(Entry->getValue().Symbol);

    // Emit the string itself with a terminating null byte.
    Asm.OutStreamer->AddComment("string offset=" +
                                Twine(Entry->getValue().Offset));
    Asm.OutStreamer->EmitBytes(
        StringRef(Entry->getKeyData(), Entry->getKeyLength() + 1));
  }

  // If we've got an offset section go ahead and emit that now as well.
  if (OffsetSection) {
    // Now only take the indexed entries and put them in an array by their ID so
    // we can emit them in order.
    Entries.resize(NumIndexedStrings);
    for (const auto &Entry : Pool) {
      if (Entry.getValue().isIndexed())
        Entries[Entry.getValue().Index] = &Entry;
    }

    Asm.OutStreamer->SwitchSection(OffsetSection);
    unsigned size = 4; // FIXME: DWARF64 is 8.
    for (const auto &Entry : Entries)
      if (UseRelativeOffsets)
        Asm.emitDwarfStringOffset(Entry->getValue());
      else
        Asm.OutStreamer->EmitIntValue(Entry->getValue().Offset, size);
  }
}
