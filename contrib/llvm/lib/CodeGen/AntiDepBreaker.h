//===- llvm/CodeGen/AntiDepBreaker.h - Anti-Dependence Breaking -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AntiDepBreaker class, which implements
// anti-dependence breaking heuristics for post-register-allocation scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ANTIDEPBREAKER_H
#define LLVM_LIB_CODEGEN_ANTIDEPBREAKER_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

/// This class works in conjunction with the post-RA scheduler to rename
/// registers to break register anti-dependencies (WAR hazards).
class LLVM_LIBRARY_VISIBILITY AntiDepBreaker {
public:
  using DbgValueVector =
      std::vector<std::pair<MachineInstr *, MachineInstr *>>;

  virtual ~AntiDepBreaker();

  /// Initialize anti-dep breaking for a new basic block.
  virtual void StartBlock(MachineBasicBlock *BB) = 0;

  /// Identifiy anti-dependencies within a basic-block region and break them by
  /// renaming registers. Return the number of anti-dependencies broken.
  virtual unsigned BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                                         MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End,
                                         unsigned InsertPosIndex,
                                         DbgValueVector &DbgValues) = 0;

  /// Update liveness information to account for the current
  /// instruction, which will not be scheduled.
  virtual void Observe(MachineInstr &MI, unsigned Count,
                       unsigned InsertPosIndex) = 0;

  /// Finish anti-dep breaking for a basic block.
  virtual void FinishBlock() = 0;

  /// Update DBG_VALUE if dependency breaker is updating
  /// other machine instruction to use NewReg.
  void UpdateDbgValue(MachineInstr &MI, unsigned OldReg, unsigned NewReg) {
    assert(MI.isDebugValue() && "MI is not DBG_VALUE!");
    if (MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == OldReg)
      MI.getOperand(0).setReg(NewReg);
  }

  /// Update all DBG_VALUE instructions that may be affected by the dependency
  /// breaker's update of ParentMI to use NewReg.
  void UpdateDbgValues(const DbgValueVector &DbgValues, MachineInstr *ParentMI,
                       unsigned OldReg, unsigned NewReg) {
    // The following code is dependent on the order in which the DbgValues are
    // constructed in ScheduleDAGInstrs::buildSchedGraph.
    MachineInstr *PrevDbgMI = nullptr;
    for (const auto &DV : make_range(DbgValues.crbegin(), DbgValues.crend())) {
      MachineInstr *PrevMI = DV.second;
      if ((PrevMI == ParentMI) || (PrevMI == PrevDbgMI)) {
        MachineInstr *DbgMI = DV.first;
        UpdateDbgValue(*DbgMI, OldReg, NewReg);
        PrevDbgMI = DbgMI;
      } else if (PrevDbgMI) {
        break; // If no match and already found a DBG_VALUE, we're done.
      }
    }
  }
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ANTIDEPBREAKER_H
