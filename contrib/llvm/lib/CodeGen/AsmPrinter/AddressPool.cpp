//===- llvm/CodeGen/AddressPool.cpp - Dwarf Debug Framework ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AddressPool.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <utility>

using namespace llvm;

unsigned AddressPool::getIndex(const MCSymbol *Sym, bool TLS) {
  HasBeenUsed = true;
  auto IterBool =
      Pool.insert(std::make_pair(Sym, AddressPoolEntry(Pool.size(), TLS)));
  return IterBool.first->second.Number;
}


void AddressPool::emitHeader(AsmPrinter &Asm, MCSection *Section) {
  static const uint8_t AddrSize = Asm.getDataLayout().getPointerSize();
  uint64_t Length = sizeof(uint16_t) // version
                  + sizeof(uint8_t)  // address_size
                  + sizeof(uint8_t)  // segment_selector_size
                  + AddrSize * Pool.size(); // entries
  Asm.OutStreamer->AddComment("Length of contribution");
  Asm.emitInt32(Length); // TODO: Support DWARF64 format.
  Asm.OutStreamer->AddComment("DWARF version number");
  Asm.emitInt16(Asm.getDwarfVersion());
  Asm.OutStreamer->AddComment("Address size");
  Asm.emitInt8(AddrSize);
  Asm.OutStreamer->AddComment("Segment selector size");
  Asm.emitInt8(0); // TODO: Support non-zero segment_selector_size.
}

// Emit addresses into the section given.
void AddressPool::emit(AsmPrinter &Asm, MCSection *AddrSection) {
  if (isEmpty())
    return;

  // Start the dwarf addr section.
  Asm.OutStreamer->SwitchSection(AddrSection);

  if (Asm.getDwarfVersion() >= 5)
    emitHeader(Asm, AddrSection);

  // Define the symbol that marks the start of the contribution.
  // It is referenced via DW_AT_addr_base.
  Asm.OutStreamer->EmitLabel(AddressTableBaseSym);

  // Order the address pool entries by ID
  SmallVector<const MCExpr *, 64> Entries(Pool.size());

  for (const auto &I : Pool)
    Entries[I.second.Number] =
        I.second.TLS
            ? Asm.getObjFileLowering().getDebugThreadLocalSymbol(I.first)
            : MCSymbolRefExpr::create(I.first, Asm.OutContext);

  for (const MCExpr *Entry : Entries)
    Asm.OutStreamer->EmitValue(Entry, Asm.getDataLayout().getPointerSize());
}
