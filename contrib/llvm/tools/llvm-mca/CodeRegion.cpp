//===-------------------------- CodeRegion.cpp -----------------*- C++ -* -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the CodeRegions interface.
///
//===----------------------------------------------------------------------===//

#include "CodeRegion.h"

namespace llvm {
namespace mca {

bool CodeRegion::isLocInRange(llvm::SMLoc Loc) const {
  if (RangeEnd.isValid() && Loc.getPointer() > RangeEnd.getPointer())
    return false;
  if (RangeStart.isValid() && Loc.getPointer() < RangeStart.getPointer())
    return false;
  return true;
}

void CodeRegions::beginRegion(llvm::StringRef Description, llvm::SMLoc Loc) {
  assert(!Regions.empty() && "Missing Default region");
  const CodeRegion &CurrentRegion = *Regions.back();
  if (CurrentRegion.startLoc().isValid() && !CurrentRegion.endLoc().isValid()) {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Warning,
                    "Ignoring invalid region start");
    return;
  }

  // Remove the default region if there are user defined regions.
  if (!CurrentRegion.startLoc().isValid())
    Regions.erase(Regions.begin());
  addRegion(Description, Loc);
}

void CodeRegions::endRegion(llvm::SMLoc Loc) {
  assert(!Regions.empty() && "Missing Default region");
  CodeRegion &CurrentRegion = *Regions.back();
  if (CurrentRegion.endLoc().isValid()) {
    SM.PrintMessage(Loc, llvm::SourceMgr::DK_Warning,
                    "Ignoring invalid region end");
    return;
  }

  CurrentRegion.setEndLocation(Loc);
}

void CodeRegions::addInstruction(const llvm::MCInst &Instruction) {
  const llvm::SMLoc &Loc = Instruction.getLoc();
  const auto It =
      std::find_if(Regions.rbegin(), Regions.rend(),
                   [Loc](const std::unique_ptr<CodeRegion> &Region) {
                     return Region->isLocInRange(Loc);
                   });
  if (It != Regions.rend())
    (*It)->addInstruction(Instruction);
}

} // namespace mca
} // namespace llvm
