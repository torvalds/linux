//===- SafeStackLayout.cpp - SafeStack frame layout -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SafeStackLayout.h"
#include "SafeStackColoring.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>

using namespace llvm;
using namespace llvm::safestack;

#define DEBUG_TYPE "safestacklayout"

static cl::opt<bool> ClLayout("safe-stack-layout",
                              cl::desc("enable safe stack layout"), cl::Hidden,
                              cl::init(true));

LLVM_DUMP_METHOD void StackLayout::print(raw_ostream &OS) {
  OS << "Stack regions:\n";
  for (unsigned i = 0; i < Regions.size(); ++i) {
    OS << "  " << i << ": [" << Regions[i].Start << ", " << Regions[i].End
       << "), range " << Regions[i].Range << "\n";
  }
  OS << "Stack objects:\n";
  for (auto &IT : ObjectOffsets) {
    OS << "  at " << IT.getSecond() << ": " << *IT.getFirst() << "\n";
  }
}

void StackLayout::addObject(const Value *V, unsigned Size, unsigned Alignment,
                            const StackColoring::LiveRange &Range) {
  StackObjects.push_back({V, Size, Alignment, Range});
  ObjectAlignments[V] = Alignment;
  MaxAlignment = std::max(MaxAlignment, Alignment);
}

static unsigned AdjustStackOffset(unsigned Offset, unsigned Size,
                                  unsigned Alignment) {
  return alignTo(Offset + Size, Alignment) - Size;
}

void StackLayout::layoutObject(StackObject &Obj) {
  if (!ClLayout) {
    // If layout is disabled, just grab the next aligned address.
    // This effectively disables stack coloring as well.
    unsigned LastRegionEnd = Regions.empty() ? 0 : Regions.back().End;
    unsigned Start = AdjustStackOffset(LastRegionEnd, Obj.Size, Obj.Alignment);
    unsigned End = Start + Obj.Size;
    Regions.emplace_back(Start, End, Obj.Range);
    ObjectOffsets[Obj.Handle] = End;
    return;
  }

  LLVM_DEBUG(dbgs() << "Layout: size " << Obj.Size << ", align "
                    << Obj.Alignment << ", range " << Obj.Range << "\n");
  assert(Obj.Alignment <= MaxAlignment);
  unsigned Start = AdjustStackOffset(0, Obj.Size, Obj.Alignment);
  unsigned End = Start + Obj.Size;
  LLVM_DEBUG(dbgs() << "  First candidate: " << Start << " .. " << End << "\n");
  for (const StackRegion &R : Regions) {
    LLVM_DEBUG(dbgs() << "  Examining region: " << R.Start << " .. " << R.End
                      << ", range " << R.Range << "\n");
    assert(End >= R.Start);
    if (Start >= R.End) {
      LLVM_DEBUG(dbgs() << "  Does not intersect, skip.\n");
      continue;
    }
    if (Obj.Range.Overlaps(R.Range)) {
      // Find the next appropriate location.
      Start = AdjustStackOffset(R.End, Obj.Size, Obj.Alignment);
      End = Start + Obj.Size;
      LLVM_DEBUG(dbgs() << "  Overlaps. Next candidate: " << Start << " .. "
                        << End << "\n");
      continue;
    }
    if (End <= R.End) {
      LLVM_DEBUG(dbgs() << "  Reusing region(s).\n");
      break;
    }
  }

  unsigned LastRegionEnd = Regions.empty() ? 0 : Regions.back().End;
  if (End > LastRegionEnd) {
    // Insert a new region at the end. Maybe two.
    if (Start > LastRegionEnd) {
      LLVM_DEBUG(dbgs() << "  Creating gap region: " << LastRegionEnd << " .. "
                        << Start << "\n");
      Regions.emplace_back(LastRegionEnd, Start, StackColoring::LiveRange());
      LastRegionEnd = Start;
    }
    LLVM_DEBUG(dbgs() << "  Creating new region: " << LastRegionEnd << " .. "
                      << End << ", range " << Obj.Range << "\n");
    Regions.emplace_back(LastRegionEnd, End, Obj.Range);
    LastRegionEnd = End;
  }

  // Split starting and ending regions if necessary.
  for (unsigned i = 0; i < Regions.size(); ++i) {
    StackRegion &R = Regions[i];
    if (Start > R.Start && Start < R.End) {
      StackRegion R0 = R;
      R.Start = R0.End = Start;
      Regions.insert(&R, R0);
      continue;
    }
    if (End > R.Start && End < R.End) {
      StackRegion R0 = R;
      R0.End = R.Start = End;
      Regions.insert(&R, R0);
      break;
    }
  }

  // Update live ranges for all affected regions.
  for (StackRegion &R : Regions) {
    if (Start < R.End && End > R.Start)
      R.Range.Join(Obj.Range);
    if (End <= R.End)
      break;
  }

  ObjectOffsets[Obj.Handle] = End;
}

void StackLayout::computeLayout() {
  // Simple greedy algorithm.
  // If this is replaced with something smarter, it must preserve the property
  // that the first object is always at the offset 0 in the stack frame (for
  // StackProtectorSlot), or handle stack protector in some other way.

  // Sort objects by size (largest first) to reduce fragmentation.
  if (StackObjects.size() > 2)
    std::stable_sort(StackObjects.begin() + 1, StackObjects.end(),
                     [](const StackObject &a, const StackObject &b) {
                       return a.Size > b.Size;
                     });

  for (auto &Obj : StackObjects)
    layoutObject(Obj);

  LLVM_DEBUG(print(dbgs()));
}
