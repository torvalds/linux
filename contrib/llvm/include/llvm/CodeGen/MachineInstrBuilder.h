//===- CodeGen/MachineInstrBuilder.h - Simplify creation of MIs --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes a function named BuildMI, which is useful for dramatically
// simplifying how MachineInstr's are created.  It allows use of code like this:
//
//   M = BuildMI(MBB, MI, DL, TII.get(X86::ADD8rr), Dst)
//           .addReg(argVal1)
//           .addReg(argVal2);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUILDER_H
#define LLVM_CODEGEN_MACHINEINSTRBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

class MCInstrDesc;
class MDNode;

namespace RegState {

  enum {
    Define         = 0x2,
    Implicit       = 0x4,
    Kill           = 0x8,
    Dead           = 0x10,
    Undef          = 0x20,
    EarlyClobber   = 0x40,
    Debug          = 0x80,
    InternalRead   = 0x100,
    Renamable      = 0x200,
    DefineNoRead   = Define | Undef,
    ImplicitDefine = Implicit | Define,
    ImplicitKill   = Implicit | Kill
  };

} // end namespace RegState

class MachineInstrBuilder {
  MachineFunction *MF = nullptr;
  MachineInstr *MI = nullptr;

public:
  MachineInstrBuilder() = default;

  /// Create a MachineInstrBuilder for manipulating an existing instruction.
  /// F must be the machine function that was used to allocate I.
  MachineInstrBuilder(MachineFunction &F, MachineInstr *I) : MF(&F), MI(I) {}
  MachineInstrBuilder(MachineFunction &F, MachineBasicBlock::iterator I)
      : MF(&F), MI(&*I) {}

  /// Allow automatic conversion to the machine instruction we are working on.
  operator MachineInstr*() const { return MI; }
  MachineInstr *operator->() const { return MI; }
  operator MachineBasicBlock::iterator() const { return MI; }

  /// If conversion operators fail, use this method to get the MachineInstr
  /// explicitly.
  MachineInstr *getInstr() const { return MI; }

  /// Add a new virtual register operand.
  const MachineInstrBuilder &addReg(unsigned RegNo, unsigned flags = 0,
                                    unsigned SubReg = 0) const {
    assert((flags & 0x1) == 0 &&
           "Passing in 'true' to addReg is forbidden! Use enums instead.");
    MI->addOperand(*MF, MachineOperand::CreateReg(RegNo,
                                               flags & RegState::Define,
                                               flags & RegState::Implicit,
                                               flags & RegState::Kill,
                                               flags & RegState::Dead,
                                               flags & RegState::Undef,
                                               flags & RegState::EarlyClobber,
                                               SubReg,
                                               flags & RegState::Debug,
                                               flags & RegState::InternalRead,
                                               flags & RegState::Renamable));
    return *this;
  }

  /// Add a virtual register definition operand.
  const MachineInstrBuilder &addDef(unsigned RegNo, unsigned Flags = 0,
                                    unsigned SubReg = 0) const {
    return addReg(RegNo, Flags | RegState::Define, SubReg);
  }

  /// Add a virtual register use operand. It is an error for Flags to contain
  /// `RegState::Define` when calling this function.
  const MachineInstrBuilder &addUse(unsigned RegNo, unsigned Flags = 0,
                                    unsigned SubReg = 0) const {
    assert(!(Flags & RegState::Define) &&
           "Misleading addUse defines register, use addReg instead.");
    return addReg(RegNo, Flags, SubReg);
  }

  /// Add a new immediate operand.
  const MachineInstrBuilder &addImm(int64_t Val) const {
    MI->addOperand(*MF, MachineOperand::CreateImm(Val));
    return *this;
  }

  const MachineInstrBuilder &addCImm(const ConstantInt *Val) const {
    MI->addOperand(*MF, MachineOperand::CreateCImm(Val));
    return *this;
  }

  const MachineInstrBuilder &addFPImm(const ConstantFP *Val) const {
    MI->addOperand(*MF, MachineOperand::CreateFPImm(Val));
    return *this;
  }

  const MachineInstrBuilder &addMBB(MachineBasicBlock *MBB,
                                    unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateMBB(MBB, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addFrameIndex(int Idx) const {
    MI->addOperand(*MF, MachineOperand::CreateFI(Idx));
    return *this;
  }

  const MachineInstrBuilder &addConstantPoolIndex(unsigned Idx,
                                                  int Offset = 0,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateCPI(Idx, Offset, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addTargetIndex(unsigned Idx, int64_t Offset = 0,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateTargetIndex(Idx, Offset,
                                                          TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addJumpTableIndex(unsigned Idx,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateJTI(Idx, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addGlobalAddress(const GlobalValue *GV,
                                              int64_t Offset = 0,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateGA(GV, Offset, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addExternalSymbol(const char *FnName,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateES(FnName, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addBlockAddress(const BlockAddress *BA,
                                             int64_t Offset = 0,
                                          unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateBA(BA, Offset, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &addRegMask(const uint32_t *Mask) const {
    MI->addOperand(*MF, MachineOperand::CreateRegMask(Mask));
    return *this;
  }

  const MachineInstrBuilder &addMemOperand(MachineMemOperand *MMO) const {
    MI->addMemOperand(*MF, MMO);
    return *this;
  }

  const MachineInstrBuilder &
  setMemRefs(ArrayRef<MachineMemOperand *> MMOs) const {
    MI->setMemRefs(*MF, MMOs);
    return *this;
  }

  const MachineInstrBuilder &cloneMemRefs(const MachineInstr &OtherMI) const {
    MI->cloneMemRefs(*MF, OtherMI);
    return *this;
  }

  const MachineInstrBuilder &
  cloneMergedMemRefs(ArrayRef<const MachineInstr *> OtherMIs) const {
    MI->cloneMergedMemRefs(*MF, OtherMIs);
    return *this;
  }

  const MachineInstrBuilder &add(const MachineOperand &MO) const {
    MI->addOperand(*MF, MO);
    return *this;
  }

  const MachineInstrBuilder &add(ArrayRef<MachineOperand> MOs) const {
    for (const MachineOperand &MO : MOs) {
      MI->addOperand(*MF, MO);
    }
    return *this;
  }

  const MachineInstrBuilder &addMetadata(const MDNode *MD) const {
    MI->addOperand(*MF, MachineOperand::CreateMetadata(MD));
    assert((MI->isDebugValue() ? static_cast<bool>(MI->getDebugVariable())
                               : true) &&
           "first MDNode argument of a DBG_VALUE not a variable");
    assert((MI->isDebugLabel() ? static_cast<bool>(MI->getDebugLabel())
                               : true) &&
           "first MDNode argument of a DBG_LABEL not a label");
    return *this;
  }

  const MachineInstrBuilder &addCFIIndex(unsigned CFIIndex) const {
    MI->addOperand(*MF, MachineOperand::CreateCFIIndex(CFIIndex));
    return *this;
  }

  const MachineInstrBuilder &addIntrinsicID(Intrinsic::ID ID) const {
    MI->addOperand(*MF, MachineOperand::CreateIntrinsicID(ID));
    return *this;
  }

  const MachineInstrBuilder &addPredicate(CmpInst::Predicate Pred) const {
    MI->addOperand(*MF, MachineOperand::CreatePredicate(Pred));
    return *this;
  }

  const MachineInstrBuilder &addSym(MCSymbol *Sym,
                                    unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateMCSymbol(Sym, TargetFlags));
    return *this;
  }

  const MachineInstrBuilder &setMIFlags(unsigned Flags) const {
    MI->setFlags(Flags);
    return *this;
  }

  const MachineInstrBuilder &setMIFlag(MachineInstr::MIFlag Flag) const {
    MI->setFlag(Flag);
    return *this;
  }

  // Add a displacement from an existing MachineOperand with an added offset.
  const MachineInstrBuilder &addDisp(const MachineOperand &Disp, int64_t off,
                                     unsigned char TargetFlags = 0) const {
    // If caller specifies new TargetFlags then use it, otherwise the
    // default behavior is to copy the target flags from the existing
    // MachineOperand. This means if the caller wants to clear the
    // target flags it needs to do so explicitly.
    if (0 == TargetFlags)
      TargetFlags = Disp.getTargetFlags();

    switch (Disp.getType()) {
      default:
        llvm_unreachable("Unhandled operand type in addDisp()");
      case MachineOperand::MO_Immediate:
        return addImm(Disp.getImm() + off);
      case MachineOperand::MO_ConstantPoolIndex:
        return addConstantPoolIndex(Disp.getIndex(), Disp.getOffset() + off,
                                    TargetFlags);
      case MachineOperand::MO_GlobalAddress:
        return addGlobalAddress(Disp.getGlobal(), Disp.getOffset() + off,
                                TargetFlags);
    }
  }

  /// Copy all the implicit operands from OtherMI onto this one.
  const MachineInstrBuilder &
  copyImplicitOps(const MachineInstr &OtherMI) const {
    MI->copyImplicitOps(*MF, OtherMI);
    return *this;
  }

  bool constrainAllUses(const TargetInstrInfo &TII,
                        const TargetRegisterInfo &TRI,
                        const RegisterBankInfo &RBI) const {
    return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
  }
};

/// Builder interface. Specify how to create the initial instruction itself.
inline MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  return MachineInstrBuilder(MF, MF.CreateMachineInstr(MCID, DL));
}

/// This version of the builder sets up the first operand as a
/// destination virtual register.
inline MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                   const MCInstrDesc &MCID, unsigned DestReg) {
  return MachineInstrBuilder(MF, MF.CreateMachineInstr(MCID, DL))
           .addReg(DestReg, RegState::Define);
}

/// This version of the builder inserts the newly-built instruction before
/// the given position in the given MachineBasicBlock, and sets up the first
/// operand as a destination virtual register.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, const MCInstrDesc &MCID,
                                   unsigned DestReg) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, DL);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).addReg(DestReg, RegState::Define);
}

/// This version of the builder inserts the newly-built instruction before
/// the given position in the given MachineBasicBlock, and sets up the first
/// operand as a destination virtual register.
///
/// If \c I is inside a bundle, then the newly inserted \a MachineInstr is
/// added to the same bundle.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::instr_iterator I,
                                   const DebugLoc &DL, const MCInstrDesc &MCID,
                                   unsigned DestReg) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, DL);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).addReg(DestReg, RegState::Define);
}

inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr &I,
                                   const DebugLoc &DL, const MCInstrDesc &MCID,
                                   unsigned DestReg) {
  // Calling the overload for instr_iterator is always correct.  However, the
  // definition is not available in headers, so inline the check.
  if (I.isInsideBundle())
    return BuildMI(BB, MachineBasicBlock::instr_iterator(I), DL, MCID, DestReg);
  return BuildMI(BB, MachineBasicBlock::iterator(I), DL, MCID, DestReg);
}

inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr *I,
                                   const DebugLoc &DL, const MCInstrDesc &MCID,
                                   unsigned DestReg) {
  return BuildMI(BB, *I, DL, MCID, DestReg);
}

/// This version of the builder inserts the newly-built instruction before the
/// given position in the given MachineBasicBlock, and does NOT take a
/// destination register.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, DL);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI);
}

inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::instr_iterator I,
                                   const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, DL);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI);
}

inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr &I,
                                   const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  // Calling the overload for instr_iterator is always correct.  However, the
  // definition is not available in headers, so inline the check.
  if (I.isInsideBundle())
    return BuildMI(BB, MachineBasicBlock::instr_iterator(I), DL, MCID);
  return BuildMI(BB, MachineBasicBlock::iterator(I), DL, MCID);
}

inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr *I,
                                   const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  return BuildMI(BB, *I, DL, MCID);
}

/// This version of the builder inserts the newly-built instruction at the end
/// of the given MachineBasicBlock, and does NOT take a destination register.
inline MachineInstrBuilder BuildMI(MachineBasicBlock *BB, const DebugLoc &DL,
                                   const MCInstrDesc &MCID) {
  return BuildMI(*BB, BB->end(), DL, MCID);
}

/// This version of the builder inserts the newly-built instruction at the
/// end of the given MachineBasicBlock, and sets up the first operand as a
/// destination virtual register.
inline MachineInstrBuilder BuildMI(MachineBasicBlock *BB, const DebugLoc &DL,
                                   const MCInstrDesc &MCID, unsigned DestReg) {
  return BuildMI(*BB, BB->end(), DL, MCID, DestReg);
}

/// This version of the builder builds a DBG_VALUE intrinsic
/// for either a value in a register or a register-indirect
/// address.  The convention is that a DBG_VALUE is indirect iff the
/// second operand is an immediate.
MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                            const MCInstrDesc &MCID, bool IsIndirect,
                            unsigned Reg, const MDNode *Variable,
                            const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE intrinsic
/// for a MachineOperand.
MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                            const MCInstrDesc &MCID, bool IsIndirect,
                            MachineOperand &MO, const MDNode *Variable,
                            const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE intrinsic
/// for either a value in a register or a register-indirect
/// address and inserts it at position I.
MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            const MCInstrDesc &MCID, bool IsIndirect,
                            unsigned Reg, const MDNode *Variable,
                            const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE intrinsic
/// for a machine operand and inserts it at position I.
MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            const MCInstrDesc &MCID, bool IsIndirect,
                            MachineOperand &MO, const MDNode *Variable,
                            const MDNode *Expr);

/// Clone a DBG_VALUE whose value has been spilled to FrameIndex.
MachineInstr *buildDbgValueForSpill(MachineBasicBlock &BB,
                                    MachineBasicBlock::iterator I,
                                    const MachineInstr &Orig, int FrameIndex);

/// Update a DBG_VALUE whose value has been spilled to FrameIndex. Useful when
/// modifying an instruction in place while iterating over a basic block.
void updateDbgValueForSpill(MachineInstr &Orig, int FrameIndex);

inline unsigned getDefRegState(bool B) {
  return B ? RegState::Define : 0;
}
inline unsigned getImplRegState(bool B) {
  return B ? RegState::Implicit : 0;
}
inline unsigned getKillRegState(bool B) {
  return B ? RegState::Kill : 0;
}
inline unsigned getDeadRegState(bool B) {
  return B ? RegState::Dead : 0;
}
inline unsigned getUndefRegState(bool B) {
  return B ? RegState::Undef : 0;
}
inline unsigned getInternalReadRegState(bool B) {
  return B ? RegState::InternalRead : 0;
}
inline unsigned getDebugRegState(bool B) {
  return B ? RegState::Debug : 0;
}
inline unsigned getRenamableRegState(bool B) {
  return B ? RegState::Renamable : 0;
}

/// Get all register state flags from machine operand \p RegOp.
inline unsigned getRegState(const MachineOperand &RegOp) {
  assert(RegOp.isReg() && "Not a register operand");
  return getDefRegState(RegOp.isDef())                    |
         getImplRegState(RegOp.isImplicit())              |
         getKillRegState(RegOp.isKill())                  |
         getDeadRegState(RegOp.isDead())                  |
         getUndefRegState(RegOp.isUndef())                |
         getInternalReadRegState(RegOp.isInternalRead())  |
         getDebugRegState(RegOp.isDebug())                |
         getRenamableRegState(
             TargetRegisterInfo::isPhysicalRegister(RegOp.getReg()) &&
             RegOp.isRenamable());
}

/// Helper class for constructing bundles of MachineInstrs.
///
/// MIBundleBuilder can create a bundle from scratch by inserting new
/// MachineInstrs one at a time, or it can create a bundle from a sequence of
/// existing MachineInstrs in a basic block.
class MIBundleBuilder {
  MachineBasicBlock &MBB;
  MachineBasicBlock::instr_iterator Begin;
  MachineBasicBlock::instr_iterator End;

public:
  /// Create an MIBundleBuilder that inserts instructions into a new bundle in
  /// BB above the bundle or instruction at Pos.
  MIBundleBuilder(MachineBasicBlock &BB, MachineBasicBlock::iterator Pos)
      : MBB(BB), Begin(Pos.getInstrIterator()), End(Begin) {}

  /// Create a bundle from the sequence of instructions between B and E.
  MIBundleBuilder(MachineBasicBlock &BB, MachineBasicBlock::iterator B,
                  MachineBasicBlock::iterator E)
      : MBB(BB), Begin(B.getInstrIterator()), End(E.getInstrIterator()) {
    assert(B != E && "No instructions to bundle");
    ++B;
    while (B != E) {
      MachineInstr &MI = *B;
      ++B;
      MI.bundleWithPred();
    }
  }

  /// Create an MIBundleBuilder representing an existing instruction or bundle
  /// that has MI as its head.
  explicit MIBundleBuilder(MachineInstr *MI)
      : MBB(*MI->getParent()), Begin(MI),
        End(getBundleEnd(MI->getIterator())) {}

  /// Return a reference to the basic block containing this bundle.
  MachineBasicBlock &getMBB() const { return MBB; }

  /// Return true if no instructions have been inserted in this bundle yet.
  /// Empty bundles aren't representable in a MachineBasicBlock.
  bool empty() const { return Begin == End; }

  /// Return an iterator to the first bundled instruction.
  MachineBasicBlock::instr_iterator begin() const { return Begin; }

  /// Return an iterator beyond the last bundled instruction.
  MachineBasicBlock::instr_iterator end() const { return End; }

  /// Insert MI into this bundle before I which must point to an instruction in
  /// the bundle, or end().
  MIBundleBuilder &insert(MachineBasicBlock::instr_iterator I,
                          MachineInstr *MI) {
    MBB.insert(I, MI);
    if (I == Begin) {
      if (!empty())
        MI->bundleWithSucc();
      Begin = MI->getIterator();
      return *this;
    }
    if (I == End) {
      MI->bundleWithPred();
      return *this;
    }
    // MI was inserted in the middle of the bundle, so its neighbors' flags are
    // already fine. Update MI's bundle flags manually.
    MI->setFlag(MachineInstr::BundledPred);
    MI->setFlag(MachineInstr::BundledSucc);
    return *this;
  }

  /// Insert MI into MBB by prepending it to the instructions in the bundle.
  /// MI will become the first instruction in the bundle.
  MIBundleBuilder &prepend(MachineInstr *MI) {
    return insert(begin(), MI);
  }

  /// Insert MI into MBB by appending it to the instructions in the bundle.
  /// MI will become the last instruction in the bundle.
  MIBundleBuilder &append(MachineInstr *MI) {
    return insert(end(), MI);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTRBUILDER_H
