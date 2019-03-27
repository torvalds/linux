//===--- SymbolOccurrences.cpp - Clang refactoring library ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/SymbolOccurrences.h"
#include "clang/Tooling/Refactoring/Rename/SymbolName.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;
using namespace tooling;

SymbolOccurrence::SymbolOccurrence(const SymbolName &Name, OccurrenceKind Kind,
                                   ArrayRef<SourceLocation> Locations)
    : Kind(Kind) {
  ArrayRef<std::string> NamePieces = Name.getNamePieces();
  assert(Locations.size() == NamePieces.size() &&
         "mismatching number of locations and lengths");
  assert(!Locations.empty() && "no locations");
  if (Locations.size() == 1) {
    RangeOrNumRanges = SourceRange(
        Locations[0], Locations[0].getLocWithOffset(NamePieces[0].size()));
    return;
  }
  MultipleRanges = llvm::make_unique<SourceRange[]>(Locations.size());
  RangeOrNumRanges.setBegin(
      SourceLocation::getFromRawEncoding(Locations.size()));
  for (const auto &Loc : llvm::enumerate(Locations)) {
    MultipleRanges[Loc.index()] = SourceRange(
        Loc.value(),
        Loc.value().getLocWithOffset(NamePieces[Loc.index()].size()));
  }
}
