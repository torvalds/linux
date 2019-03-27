//===-- CodeGen/MachineInstBundle.h - MI bundle utilities -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provide utility functions to manipulate machine instruction
// bundles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUNDLE_H
#define LLVM_CODEGEN_MACHINEINSTRBUNDLE_H

#include "llvm/CodeGen/MachineBasicBlock.h"

namespace llvm {

/// finalizeBundle - Finalize a machine instruction bundle which includes
/// a sequence of instructions starting from FirstMI to LastMI (exclusive).
/// This routine adds a BUNDLE instruction to represent the bundle, it adds
/// IsInternalRead markers to MachineOperands which are defined inside the
/// bundle, and it copies externally visible defs and uses to the BUNDLE
/// instruction.
void finalizeBundle(MachineBasicBlock &MBB,
                    MachineBasicBlock::instr_iterator FirstMI,
                    MachineBasicBlock::instr_iterator LastMI);

/// finalizeBundle - Same functionality as the previous finalizeBundle except
/// the last instruction in the bundle is not provided as an input. This is
/// used in cases where bundles are pre-determined by marking instructions
/// with 'InsideBundle' marker. It returns the MBB instruction iterator that
/// points to the end of the bundle.
MachineBasicBlock::instr_iterator finalizeBundle(MachineBasicBlock &MBB,
                    MachineBasicBlock::instr_iterator FirstMI);

/// finalizeBundles - Finalize instruction bundles in the specified
/// MachineFunction. Return true if any bundles are finalized.
bool finalizeBundles(MachineFunction &MF);

/// Returns an iterator to the first instruction in the bundle containing \p I.
inline MachineBasicBlock::instr_iterator getBundleStart(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator to the first instruction in the bundle containing \p I.
inline MachineBasicBlock::const_instr_iterator getBundleStart(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithPred())
    --I;
  return I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
inline MachineBasicBlock::instr_iterator getBundleEnd(
    MachineBasicBlock::instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  return ++I;
}

/// Returns an iterator pointing beyond the bundle containing \p I.
inline MachineBasicBlock::const_instr_iterator getBundleEnd(
    MachineBasicBlock::const_instr_iterator I) {
  while (I->isBundledWithSucc())
    ++I;
  return ++I;
}

//===----------------------------------------------------------------------===//
// MachineOperand iterator
//

/// MachineOperandIteratorBase - Iterator that can visit all operands on a
/// MachineInstr, or all operands on a bundle of MachineInstrs.  This class is
/// not intended to be used directly, use one of the sub-classes instead.
///
/// Intended use:
///
///   for (MIBundleOperands MIO(MI); MIO.isValid(); ++MIO) {
///     if (!MIO->isReg())
///       continue;
///     ...
///   }
///
class MachineOperandIteratorBase {
  MachineBasicBlock::instr_iterator InstrI, InstrE;
  MachineInstr::mop_iterator OpI, OpE;

  // If the operands on InstrI are exhausted, advance InstrI to the next
  // bundled instruction with operands.
  void advance() {
    while (OpI == OpE) {
      // Don't advance off the basic block, or into a new bundle.
      if (++InstrI == InstrE || !InstrI->isInsideBundle())
        break;
      OpI = InstrI->operands_begin();
      OpE = InstrI->operands_end();
    }
  }

protected:
  /// MachineOperandIteratorBase - Create an iterator that visits all operands
  /// on MI, or all operands on every instruction in the bundle containing MI.
  ///
  /// @param MI The instruction to examine.
  /// @param WholeBundle When true, visit all operands on the entire bundle.
  ///
  explicit MachineOperandIteratorBase(MachineInstr &MI, bool WholeBundle) {
    if (WholeBundle) {
      InstrI = getBundleStart(MI.getIterator());
      InstrE = MI.getParent()->instr_end();
    } else {
      InstrI = InstrE = MI.getIterator();
      ++InstrE;
    }
    OpI = InstrI->operands_begin();
    OpE = InstrI->operands_end();
    if (WholeBundle)
      advance();
  }

  MachineOperand &deref() const { return *OpI; }

public:
  /// isValid - Returns true until all the operands have been visited.
  bool isValid() const { return OpI != OpE; }

  /// Preincrement.  Move to the next operand.
  void operator++() {
    assert(isValid() && "Cannot advance MIOperands beyond the last operand");
    ++OpI;
    advance();
  }

  /// getOperandNo - Returns the number of the current operand relative to its
  /// instruction.
  ///
  unsigned getOperandNo() const {
    return OpI - InstrI->operands_begin();
  }

  /// VirtRegInfo - Information about a virtual register used by a set of operands.
  ///
  struct VirtRegInfo {
    /// Reads - One of the operands read the virtual register.  This does not
    /// include undef or internal use operands, see MO::readsReg().
    bool Reads;

    /// Writes - One of the operands writes the virtual register.
    bool Writes;

    /// Tied - Uses and defs must use the same register. This can be because of
    /// a two-address constraint, or there may be a partial redefinition of a
    /// sub-register.
    bool Tied;
  };

  /// Information about how a physical register Reg is used by a set of
  /// operands.
  struct PhysRegInfo {
    /// There is a regmask operand indicating Reg is clobbered.
    /// \see MachineOperand::CreateRegMask().
    bool Clobbered;

    /// Reg or one of its aliases is defined. The definition may only cover
    /// parts of the register.
    bool Defined;
    /// Reg or a super-register is defined. The definition covers the full
    /// register.
    bool FullyDefined;

    /// Reg or one of its aliases is read. The register may only be read
    /// partially.
    bool Read;
    /// Reg or a super-register is read. The full register is read.
    bool FullyRead;

    /// Either:
    /// - Reg is FullyDefined and all defs of reg or an overlapping
    ///   register are dead, or
    /// - Reg is completely dead because "defined" by a clobber.
    bool DeadDef;

    /// Reg is Defined and all defs of reg or an overlapping register are
    /// dead.
    bool PartialDeadDef;

    /// There is a use operand of reg or a super-register with kill flag set.
    bool Killed;
  };

  /// analyzeVirtReg - Analyze how the current instruction or bundle uses a
  /// virtual register.  This function should not be called after operator++(),
  /// it expects a fresh iterator.
  ///
  /// @param Reg The virtual register to analyze.
  /// @param Ops When set, this vector will receive an (MI, OpNum) entry for
  ///            each operand referring to Reg.
  /// @returns A filled-in RegInfo struct.
  VirtRegInfo analyzeVirtReg(unsigned Reg,
           SmallVectorImpl<std::pair<MachineInstr*, unsigned> > *Ops = nullptr);

  /// analyzePhysReg - Analyze how the current instruction or bundle uses a
  /// physical register.  This function should not be called after operator++(),
  /// it expects a fresh iterator.
  ///
  /// @param Reg The physical register to analyze.
  /// @returns A filled-in PhysRegInfo struct.
  PhysRegInfo analyzePhysReg(unsigned Reg, const TargetRegisterInfo *TRI);
};

/// MIOperands - Iterate over operands of a single instruction.
///
class MIOperands : public MachineOperandIteratorBase {
public:
  MIOperands(MachineInstr &MI) : MachineOperandIteratorBase(MI, false) {}
  MachineOperand &operator* () const { return deref(); }
  MachineOperand *operator->() const { return &deref(); }
};

/// ConstMIOperands - Iterate over operands of a single const instruction.
///
class ConstMIOperands : public MachineOperandIteratorBase {
public:
  ConstMIOperands(const MachineInstr &MI)
      : MachineOperandIteratorBase(const_cast<MachineInstr &>(MI), false) {}
  const MachineOperand &operator* () const { return deref(); }
  const MachineOperand *operator->() const { return &deref(); }
};

/// MIBundleOperands - Iterate over all operands in a bundle of machine
/// instructions.
///
class MIBundleOperands : public MachineOperandIteratorBase {
public:
  MIBundleOperands(MachineInstr &MI) : MachineOperandIteratorBase(MI, true) {}
  MachineOperand &operator* () const { return deref(); }
  MachineOperand *operator->() const { return &deref(); }
};

/// ConstMIBundleOperands - Iterate over all operands in a const bundle of
/// machine instructions.
///
class ConstMIBundleOperands : public MachineOperandIteratorBase {
public:
  ConstMIBundleOperands(const MachineInstr &MI)
      : MachineOperandIteratorBase(const_cast<MachineInstr &>(MI), true) {}
  const MachineOperand &operator* () const { return deref(); }
  const MachineOperand *operator->() const { return &deref(); }
};

} // End llvm namespace

#endif
