//===- DWARFDebugAranges.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugAranges.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugArangeSet.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/WithColor.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <vector>

using namespace llvm;

void DWARFDebugAranges::extract(DataExtractor DebugArangesData) {
  if (!DebugArangesData.isValidOffset(0))
    return;
  uint32_t Offset = 0;
  DWARFDebugArangeSet Set;

  while (Set.extract(DebugArangesData, &Offset)) {
    uint32_t CUOffset = Set.getCompileUnitDIEOffset();
    for (const auto &Desc : Set.descriptors()) {
      uint64_t LowPC = Desc.Address;
      uint64_t HighPC = Desc.getEndAddress();
      appendRange(CUOffset, LowPC, HighPC);
    }
    ParsedCUOffsets.insert(CUOffset);
  }
}

void DWARFDebugAranges::generate(DWARFContext *CTX) {
  clear();
  if (!CTX)
    return;

  // Extract aranges from .debug_aranges section.
  DataExtractor ArangesData(CTX->getDWARFObj().getARangeSection(),
                            CTX->isLittleEndian(), 0);
  extract(ArangesData);

  // Generate aranges from DIEs: even if .debug_aranges section is present,
  // it may describe only a small subset of compilation units, so we need to
  // manually build aranges for the rest of them.
  for (const auto &CU : CTX->compile_units()) {
    uint32_t CUOffset = CU->getOffset();
    if (ParsedCUOffsets.insert(CUOffset).second) {
      Expected<DWARFAddressRangesVector> CURanges = CU->collectAddressRanges();
      if (!CURanges)
        WithColor::error() << toString(CURanges.takeError()) << '\n';
      else
        for (const auto &R : *CURanges)
          appendRange(CUOffset, R.LowPC, R.HighPC);
    }
  }

  construct();
}

void DWARFDebugAranges::clear() {
  Endpoints.clear();
  Aranges.clear();
  ParsedCUOffsets.clear();
}

void DWARFDebugAranges::appendRange(uint32_t CUOffset, uint64_t LowPC,
                                    uint64_t HighPC) {
  if (LowPC >= HighPC)
    return;
  Endpoints.emplace_back(LowPC, CUOffset, true);
  Endpoints.emplace_back(HighPC, CUOffset, false);
}

void DWARFDebugAranges::construct() {
  std::multiset<uint32_t> ValidCUs;  // Maintain the set of CUs describing
                                     // a current address range.
  llvm::sort(Endpoints);
  uint64_t PrevAddress = -1ULL;
  for (const auto &E : Endpoints) {
    if (PrevAddress < E.Address && !ValidCUs.empty()) {
      // If the address range between two endpoints is described by some
      // CU, first try to extend the last range in Aranges. If we can't
      // do it, start a new range.
      if (!Aranges.empty() && Aranges.back().HighPC() == PrevAddress &&
          ValidCUs.find(Aranges.back().CUOffset) != ValidCUs.end()) {
        Aranges.back().setHighPC(E.Address);
      } else {
        Aranges.emplace_back(PrevAddress, E.Address, *ValidCUs.begin());
      }
    }
    // Update the set of valid CUs.
    if (E.IsRangeStart) {
      ValidCUs.insert(E.CUOffset);
    } else {
      auto CUPos = ValidCUs.find(E.CUOffset);
      assert(CUPos != ValidCUs.end());
      ValidCUs.erase(CUPos);
    }
    PrevAddress = E.Address;
  }
  assert(ValidCUs.empty());

  // Endpoints are not needed now.
  Endpoints.clear();
  Endpoints.shrink_to_fit();
}

uint32_t DWARFDebugAranges::findAddress(uint64_t Address) const {
  if (!Aranges.empty()) {
    Range range(Address);
    RangeCollIterator begin = Aranges.begin();
    RangeCollIterator end = Aranges.end();
    RangeCollIterator pos =
        std::lower_bound(begin, end, range);

    if (pos != end && pos->containsAddress(Address)) {
      return pos->CUOffset;
    } else if (pos != begin) {
      --pos;
      if (pos->containsAddress(Address))
        return pos->CUOffset;
    }
  }
  return -1U;
}
