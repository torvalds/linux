//===- ConstantPools.cpp - ConstantPool class -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ConstantPool and  AssemblerConstantPools classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/ConstantPools.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

//
// ConstantPool implementation
//
// Emit the contents of the constant pool using the provided streamer.
void ConstantPool::emitEntries(MCStreamer &Streamer) {
  if (Entries.empty())
    return;
  Streamer.EmitDataRegion(MCDR_DataRegion);
  for (const ConstantPoolEntry &Entry : Entries) {
    Streamer.EmitCodeAlignment(Entry.Size); // align naturally
    Streamer.EmitLabel(Entry.Label);
    Streamer.EmitValue(Entry.Value, Entry.Size, Entry.Loc);
  }
  Streamer.EmitDataRegion(MCDR_DataRegionEnd);
  Entries.clear();
}

const MCExpr *ConstantPool::addEntry(const MCExpr *Value, MCContext &Context,
                                     unsigned Size, SMLoc Loc) {
  const MCConstantExpr *C = dyn_cast<MCConstantExpr>(Value);

  // Check if there is existing entry for the same constant. If so, reuse it.
  auto Itr = C ? CachedEntries.find(C->getValue()) : CachedEntries.end();
  if (Itr != CachedEntries.end())
    return Itr->second;

  MCSymbol *CPEntryLabel = Context.createTempSymbol();

  Entries.push_back(ConstantPoolEntry(CPEntryLabel, Value, Size, Loc));
  const auto SymRef = MCSymbolRefExpr::create(CPEntryLabel, Context);
  if (C)
    CachedEntries[C->getValue()] = SymRef;
  return SymRef;
}

bool ConstantPool::empty() { return Entries.empty(); }

void ConstantPool::clearCache() {
  CachedEntries.clear();
}

//
// AssemblerConstantPools implementation
//
ConstantPool *AssemblerConstantPools::getConstantPool(MCSection *Section) {
  ConstantPoolMapTy::iterator CP = ConstantPools.find(Section);
  if (CP == ConstantPools.end())
    return nullptr;

  return &CP->second;
}

ConstantPool &
AssemblerConstantPools::getOrCreateConstantPool(MCSection *Section) {
  return ConstantPools[Section];
}

static void emitConstantPool(MCStreamer &Streamer, MCSection *Section,
                             ConstantPool &CP) {
  if (!CP.empty()) {
    Streamer.SwitchSection(Section);
    CP.emitEntries(Streamer);
  }
}

void AssemblerConstantPools::emitAll(MCStreamer &Streamer) {
  // Dump contents of assembler constant pools.
  for (auto &CPI : ConstantPools) {
    MCSection *Section = CPI.first;
    ConstantPool &CP = CPI.second;

    emitConstantPool(Streamer, Section, CP);
  }
}

void AssemblerConstantPools::emitForCurrentSection(MCStreamer &Streamer) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    emitConstantPool(Streamer, Section, *CP);
}

void AssemblerConstantPools::clearCacheForCurrentSection(MCStreamer &Streamer) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    CP->clearCache();
}

const MCExpr *AssemblerConstantPools::addEntry(MCStreamer &Streamer,
                                               const MCExpr *Expr,
                                               unsigned Size, SMLoc Loc) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  return getOrCreateConstantPool(Section).addEntry(Expr, Streamer.getContext(),
                                                   Size, Loc);
}
