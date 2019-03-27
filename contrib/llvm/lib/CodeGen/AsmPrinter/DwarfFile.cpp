//===- llvm/CodeGen/DwarfFile.cpp - Dwarf Debug Framework -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DwarfFile.h"
#include "DwarfCompileUnit.h"
#include "DwarfDebug.h"
#include "DwarfUnit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/MC/MCStreamer.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;

DwarfFile::DwarfFile(AsmPrinter *AP, StringRef Pref, BumpPtrAllocator &DA)
    : Asm(AP), Abbrevs(AbbrevAllocator), StrPool(DA, *Asm, Pref) {}

void DwarfFile::addUnit(std::unique_ptr<DwarfCompileUnit> U) {
  CUs.push_back(std::move(U));
}

// Emit the various dwarf units to the unit section USection with
// the abbreviations going into ASection.
void DwarfFile::emitUnits(bool UseOffsets) {
  for (const auto &TheU : CUs)
    emitUnit(TheU.get(), UseOffsets);
}

void DwarfFile::emitUnit(DwarfUnit *TheU, bool UseOffsets) {
  if (TheU->getCUNode()->isDebugDirectivesOnly())
    return;

  MCSection *S = TheU->getSection();

  if (!S)
    return;

  Asm->OutStreamer->SwitchSection(S);
  TheU->emitHeader(UseOffsets);
  Asm->emitDwarfDIE(TheU->getUnitDie());

  if (MCSymbol *EndLabel = TheU->getEndLabel())
    Asm->OutStreamer->EmitLabel(EndLabel);
}

// Compute the size and offset for each DIE.
void DwarfFile::computeSizeAndOffsets() {
  // Offset from the first CU in the debug info section is 0 initially.
  unsigned SecOffset = 0;

  // Iterate over each compile unit and set the size and offsets for each
  // DIE within each compile unit. All offsets are CU relative.
  for (const auto &TheU : CUs) {
    if (TheU->getCUNode()->isDebugDirectivesOnly())
      continue;

    TheU->setDebugSectionOffset(SecOffset);
    SecOffset += computeSizeAndOffsetsForUnit(TheU.get());
  }
}

unsigned DwarfFile::computeSizeAndOffsetsForUnit(DwarfUnit *TheU) {
  // CU-relative offset is reset to 0 here.
  unsigned Offset = sizeof(int32_t) +      // Length of Unit Info
                    TheU->getHeaderSize(); // Unit-specific headers

  // The return value here is CU-relative, after laying out
  // all of the CU DIE.
  return computeSizeAndOffset(TheU->getUnitDie(), Offset);
}

// Compute the size and offset of a DIE. The offset is relative to start of the
// CU. It returns the offset after laying out the DIE.
unsigned DwarfFile::computeSizeAndOffset(DIE &Die, unsigned Offset) {
  return Die.computeOffsetsAndAbbrevs(Asm, Abbrevs, Offset);
}

void DwarfFile::emitAbbrevs(MCSection *Section) { Abbrevs.Emit(Asm, Section); }

// Emit strings into a string section.
void DwarfFile::emitStrings(MCSection *StrSection, MCSection *OffsetSection,
                            bool UseRelativeOffsets) {
  StrPool.emit(*Asm, StrSection, OffsetSection, UseRelativeOffsets);
}

bool DwarfFile::addScopeVariable(LexicalScope *LS, DbgVariable *Var) {
  auto &ScopeVars = ScopeVariables[LS];
  const DILocalVariable *DV = Var->getVariable();
  if (unsigned ArgNum = DV->getArg()) {
    auto Cached = ScopeVars.Args.find(ArgNum);
    if (Cached == ScopeVars.Args.end())
      ScopeVars.Args[ArgNum] = Var;
    else {
      Cached->second->addMMIEntry(*Var);
      return false;
    }
  } else {
    ScopeVars.Locals.push_back(Var);
  }
  return true;
}

void DwarfFile::addScopeLabel(LexicalScope *LS, DbgLabel *Label) {
  SmallVectorImpl<DbgLabel *> &Labels = ScopeLabels[LS];
  Labels.push_back(Label);
}

std::pair<uint32_t, RangeSpanList *>
DwarfFile::addRange(const DwarfCompileUnit &CU, SmallVector<RangeSpan, 2> R) {
  CURangeLists.push_back(
      RangeSpanList(Asm->createTempSymbol("debug_ranges"), CU, std::move(R)));
  return std::make_pair(CURangeLists.size() - 1, &CURangeLists.back());
}
