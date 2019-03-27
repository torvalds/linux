//===- LiveStacks.h - Live Stack Slot Analysis ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the live stack slot analysis pass. It is analogous to
// live interval analysis except it's analyzing liveness of stack slots rather
// than registers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVESTACKS_H
#define LLVM_CODEGEN_LIVESTACKS_H

#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Pass.h"
#include <cassert>
#include <map>
#include <unordered_map>

namespace llvm {

class TargetRegisterClass;
class TargetRegisterInfo;

class LiveStacks : public MachineFunctionPass {
  const TargetRegisterInfo *TRI;

  /// Special pool allocator for VNInfo's (LiveInterval val#).
  ///
  VNInfo::Allocator VNInfoAllocator;

  /// S2IMap - Stack slot indices to live interval mapping.
  using SS2IntervalMap = std::unordered_map<int, LiveInterval>;
  SS2IntervalMap S2IMap;

  /// S2RCMap - Stack slot indices to register class mapping.
  std::map<int, const TargetRegisterClass *> S2RCMap;

public:
  static char ID; // Pass identification, replacement for typeid

  LiveStacks() : MachineFunctionPass(ID) {
    initializeLiveStacksPass(*PassRegistry::getPassRegistry());
  }

  using iterator = SS2IntervalMap::iterator;
  using const_iterator = SS2IntervalMap::const_iterator;

  const_iterator begin() const { return S2IMap.begin(); }
  const_iterator end() const { return S2IMap.end(); }
  iterator begin() { return S2IMap.begin(); }
  iterator end() { return S2IMap.end(); }

  unsigned getNumIntervals() const { return (unsigned)S2IMap.size(); }

  LiveInterval &getOrCreateInterval(int Slot, const TargetRegisterClass *RC);

  LiveInterval &getInterval(int Slot) {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    SS2IntervalMap::iterator I = S2IMap.find(Slot);
    assert(I != S2IMap.end() && "Interval does not exist for stack slot");
    return I->second;
  }

  const LiveInterval &getInterval(int Slot) const {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    SS2IntervalMap::const_iterator I = S2IMap.find(Slot);
    assert(I != S2IMap.end() && "Interval does not exist for stack slot");
    return I->second;
  }

  bool hasInterval(int Slot) const { return S2IMap.count(Slot); }

  const TargetRegisterClass *getIntervalRegClass(int Slot) const {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    std::map<int, const TargetRegisterClass *>::const_iterator I =
        S2RCMap.find(Slot);
    assert(I != S2RCMap.end() &&
           "Register class info does not exist for stack slot");
    return I->second;
  }

  VNInfo::Allocator &getVNInfoAllocator() { return VNInfoAllocator; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;

  /// runOnMachineFunction - pass entry point
  bool runOnMachineFunction(MachineFunction &) override;

  /// print - Implement the dump method.
  void print(raw_ostream &O, const Module * = nullptr) const override;
};

} // end namespace llvm

#endif
