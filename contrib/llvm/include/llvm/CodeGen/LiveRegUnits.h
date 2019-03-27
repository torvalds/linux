//===- llvm/CodeGen/LiveRegUnits.h - Register Unit Set ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// A set of register units. It is intended for register liveness tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEREGUNITS_H
#define LLVM_CODEGEN_LIVEREGUNITS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cstdint>

namespace llvm {

class MachineInstr;
class MachineBasicBlock;

/// A set of register units used to track register liveness.
class LiveRegUnits {
  const TargetRegisterInfo *TRI = nullptr;
  BitVector Units;

public:
  /// Constructs a new empty LiveRegUnits set.
  LiveRegUnits() = default;

  /// Constructs and initialize an empty LiveRegUnits set.
  LiveRegUnits(const TargetRegisterInfo &TRI) {
    init(TRI);
  }

  /// For a machine instruction \p MI, adds all register units used in
  /// \p UsedRegUnits and defined or clobbered in \p ModifiedRegUnits. This is
  /// useful when walking over a range of instructions to track registers
  /// used or defined seperately.
  static void accumulateUsedDefed(const MachineInstr &MI,
                                  LiveRegUnits &ModifiedRegUnits,
                                  LiveRegUnits &UsedRegUnits,
                                  const TargetRegisterInfo *TRI) {
    for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
      if (O->isRegMask())
        ModifiedRegUnits.addRegsInMask(O->getRegMask());
      if (!O->isReg())
        continue;
      unsigned Reg = O->getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(Reg))
        continue;
      if (O->isDef()) {
        // Some architectures (e.g. AArch64 XZR/WZR) have registers that are
        // constant and may be used as destinations to indicate the generated
        // value is discarded. No need to track such case as a def.
        if (!TRI->isConstantPhysReg(Reg))
          ModifiedRegUnits.addReg(Reg);
      } else {
        assert(O->isUse() && "Reg operand not a def and not a use");
        UsedRegUnits.addReg(Reg);
      }
    }
    return;
  }

  /// Initialize and clear the set.
  void init(const TargetRegisterInfo &TRI) {
    this->TRI = &TRI;
    Units.reset();
    Units.resize(TRI.getNumRegUnits());
  }

  /// Clears the set.
  void clear() { Units.reset(); }

  /// Returns true if the set is empty.
  bool empty() const { return Units.none(); }

  /// Adds register units covered by physical register \p Reg.
  void addReg(MCPhysReg Reg) {
    for (MCRegUnitIterator Unit(Reg, TRI); Unit.isValid(); ++Unit)
      Units.set(*Unit);
  }

  /// Adds register units covered by physical register \p Reg that are
  /// part of the lanemask \p Mask.
  void addRegMasked(MCPhysReg Reg, LaneBitmask Mask) {
    for (MCRegUnitMaskIterator Unit(Reg, TRI); Unit.isValid(); ++Unit) {
      LaneBitmask UnitMask = (*Unit).second;
      if (UnitMask.none() || (UnitMask & Mask).any())
        Units.set((*Unit).first);
    }
  }

  /// Removes all register units covered by physical register \p Reg.
  void removeReg(MCPhysReg Reg) {
    for (MCRegUnitIterator Unit(Reg, TRI); Unit.isValid(); ++Unit)
      Units.reset(*Unit);
  }

  /// Removes register units not preserved by the regmask \p RegMask.
  /// The regmask has the same format as the one in the RegMask machine operand.
  void removeRegsNotPreserved(const uint32_t *RegMask);

  /// Adds register units not preserved by the regmask \p RegMask.
  /// The regmask has the same format as the one in the RegMask machine operand.
  void addRegsInMask(const uint32_t *RegMask);

  /// Returns true if no part of physical register \p Reg is live.
  bool available(MCPhysReg Reg) const {
    for (MCRegUnitIterator Unit(Reg, TRI); Unit.isValid(); ++Unit) {
      if (Units.test(*Unit))
        return false;
    }
    return true;
  }

  /// Updates liveness when stepping backwards over the instruction \p MI.
  /// This removes all register units defined or clobbered in \p MI and then
  /// adds the units used (as in use operands) in \p MI.
  void stepBackward(const MachineInstr &MI);

  /// Adds all register units used, defined or clobbered in \p MI.
  /// This is useful when walking over a range of instruction to find registers
  /// unused over the whole range.
  void accumulate(const MachineInstr &MI);

  /// Adds registers living out of block \p MBB.
  /// Live out registers are the union of the live-in registers of the successor
  /// blocks and pristine registers. Live out registers of the end block are the
  /// callee saved registers.
  void addLiveOuts(const MachineBasicBlock &MBB);

  /// Adds registers living into block \p MBB.
  void addLiveIns(const MachineBasicBlock &MBB);

  /// Adds all register units marked in the bitvector \p RegUnits.
  void addUnits(const BitVector &RegUnits) {
    Units |= RegUnits;
  }
  /// Removes all register units marked in the bitvector \p RegUnits.
  void removeUnits(const BitVector &RegUnits) {
    Units.reset(RegUnits);
  }
  /// Return the internal bitvector representation of the set.
  const BitVector &getBitVector() const {
    return Units;
  }

private:
  /// Adds pristine registers. Pristine registers are callee saved registers
  /// that are unused in the function.
  void addPristines(const MachineFunction &MF);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEREGUNITS_H
