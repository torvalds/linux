//===- GISelWorkList.h - Worklist for GISel passes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_GISEL_WORKLIST_H
#define LLVM_GISEL_WORKLIST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/Debug.h"

namespace llvm {

class MachineInstr;
class MachineFunction;

// Worklist which mostly works similar to InstCombineWorkList, but on
// MachineInstrs. The main difference with something like a SetVector is that
// erasing an element doesn't move all elements over one place - instead just
// nulls out the element of the vector.
//
// FIXME: Does it make sense to factor out common code with the
// instcombinerWorkList?
template<unsigned N>
class GISelWorkList {
  SmallVector<MachineInstr *, N> Worklist;
  DenseMap<MachineInstr *, unsigned> WorklistMap;

public:
  GISelWorkList() {}

  bool empty() const { return WorklistMap.empty(); }

  unsigned size() const { return WorklistMap.size(); }

  /// Add the specified instruction to the worklist if it isn't already in it.
  void insert(MachineInstr *I) {
    if (WorklistMap.try_emplace(I, Worklist.size()).second)
      Worklist.push_back(I);
  }

  /// Remove I from the worklist if it exists.
  void remove(const MachineInstr *I) {
    auto It = WorklistMap.find(I);
    if (It == WorklistMap.end()) return; // Not in worklist.

    // Don't bother moving everything down, just null out the slot.
    Worklist[It->second] = nullptr;

    WorklistMap.erase(It);
  }

  void clear() {
    Worklist.clear();
    WorklistMap.clear();
  }

  MachineInstr *pop_back_val() {
    MachineInstr *I;
    do {
      I = Worklist.pop_back_val();
    } while(!I);
    assert(I && "Pop back on empty worklist");
    WorklistMap.erase(I);
    return I;
  }
};

} // end namespace llvm.

#endif
