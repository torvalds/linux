//===- StatepointLowering.h - SDAGBuilder's statepoint code ---*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file includes support code use by SelectionDAGBuilder when lowering a
// statepoint sequence in SelectionDAG IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_STATEPOINTLOWERING_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_STATEPOINTLOWERING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include <cassert>

namespace llvm {

class CallInst;
class SelectionDAGBuilder;

/// This class tracks both per-statepoint and per-selectiondag information.
/// For each statepoint it tracks locations of it's gc valuess (incoming and
/// relocated) and list of gcreloc calls scheduled for visiting (this is
/// used for a debug mode consistency check only).  The spill slot tracking
/// works in concert with information in FunctionLoweringInfo.
class StatepointLoweringState {
public:
  StatepointLoweringState() = default;

  /// Reset all state tracking for a newly encountered safepoint.  Also
  /// performs some consistency checking.
  void startNewStatepoint(SelectionDAGBuilder &Builder);

  /// Clear the memory usage of this object.  This is called from
  /// SelectionDAGBuilder::clear.  We require this is never called in the
  /// midst of processing a statepoint sequence.
  void clear();

  /// Returns the spill location of a value incoming to the current
  /// statepoint.  Will return SDValue() if this value hasn't been
  /// spilled.  Otherwise, the value has already been spilled and no
  /// further action is required by the caller.
  SDValue getLocation(SDValue Val) {
    auto I = Locations.find(Val);
    if (I == Locations.end())
      return SDValue();
    return I->second;
  }

  void setLocation(SDValue Val, SDValue Location) {
    assert(!Locations.count(Val) &&
           "Trying to allocate already allocated location");
    Locations[Val] = Location;
  }

  /// Record the fact that we expect to encounter a given gc_relocate
  /// before the next statepoint.  If we don't see it, we'll report
  /// an assertion.
  void scheduleRelocCall(const CallInst &RelocCall) {
    PendingGCRelocateCalls.push_back(&RelocCall);
  }

  /// Remove this gc_relocate from the list we're expecting to see
  /// before the next statepoint.  If we weren't expecting to see
  /// it, we'll report an assertion.
  void relocCallVisited(const CallInst &RelocCall) {
    auto I = llvm::find(PendingGCRelocateCalls, &RelocCall);
    assert(I != PendingGCRelocateCalls.end() &&
           "Visited unexpected gcrelocate call");
    PendingGCRelocateCalls.erase(I);
  }

  // TODO: Should add consistency tracking to ensure we encounter
  // expected gc_result calls too.

  /// Get a stack slot we can use to store an value of type ValueType.  This
  /// will hopefully be a recylced slot from another statepoint.
  SDValue allocateStackSlot(EVT ValueType, SelectionDAGBuilder &Builder);

  void reserveStackSlot(int Offset) {
    assert(Offset >= 0 && Offset < (int)AllocatedStackSlots.size() &&
           "out of bounds");
    assert(!AllocatedStackSlots.test(Offset) && "already reserved!");
    assert(NextSlotToAllocate <= (unsigned)Offset && "consistency!");
    AllocatedStackSlots.set(Offset);
  }

  bool isStackSlotAllocated(int Offset) {
    assert(Offset >= 0 && Offset < (int)AllocatedStackSlots.size() &&
           "out of bounds");
    return AllocatedStackSlots.test(Offset);
  }

private:
  /// Maps pre-relocation value (gc pointer directly incoming into statepoint)
  /// into it's location (currently only stack slots)
  DenseMap<SDValue, SDValue> Locations;

  /// A boolean indicator for each slot listed in the FunctionInfo as to
  /// whether it has been used in the current statepoint.  Since we try to
  /// preserve stack slots across safepoints, there can be gaps in which
  /// slots have been allocated.
  SmallBitVector AllocatedStackSlots;

  /// Points just beyond the last slot known to have been allocated
  unsigned NextSlotToAllocate = 0;

  /// Keep track of pending gcrelocate calls for consistency check
  SmallVector<const CallInst *, 10> PendingGCRelocateCalls;
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SELECTIONDAG_STATEPOINTLOWERING_H
