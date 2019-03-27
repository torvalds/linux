//===-- llvm/CodeGen/GlobalISel/MachineIRBuilder.h - MIBuilder --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the MachineIRBuilder class.
/// This is a helper class to build MachineInstr.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H
#define LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H

#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Types.h"

#include "llvm/CodeGen/LowLevelType.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"


namespace llvm {

// Forward declarations.
class MachineFunction;
class MachineInstr;
class TargetInstrInfo;
class GISelChangeObserver;

/// Class which stores all the state required in a MachineIRBuilder.
/// Since MachineIRBuilders will only store state in this object, it allows
/// to transfer BuilderState between different kinds of MachineIRBuilders.
struct MachineIRBuilderState {
  /// MachineFunction under construction.
  MachineFunction *MF;
  /// Information used to access the description of the opcodes.
  const TargetInstrInfo *TII;
  /// Information used to verify types are consistent and to create virtual registers.
  MachineRegisterInfo *MRI;
  /// Debug location to be set to any instruction we create.
  DebugLoc DL;

  /// \name Fields describing the insertion point.
  /// @{
  MachineBasicBlock *MBB;
  MachineBasicBlock::iterator II;
  /// @}

  GISelChangeObserver *Observer;

  GISelCSEInfo *CSEInfo;
};

class DstOp {
  union {
    LLT LLTTy;
    unsigned Reg;
    const TargetRegisterClass *RC;
  };

public:
  enum class DstType { Ty_LLT, Ty_Reg, Ty_RC };
  DstOp(unsigned R) : Reg(R), Ty(DstType::Ty_Reg) {}
  DstOp(const LLT &T) : LLTTy(T), Ty(DstType::Ty_LLT) {}
  DstOp(const TargetRegisterClass *TRC) : RC(TRC), Ty(DstType::Ty_RC) {}

  void addDefToMIB(MachineRegisterInfo &MRI, MachineInstrBuilder &MIB) const {
    switch (Ty) {
    case DstType::Ty_Reg:
      MIB.addDef(Reg);
      break;
    case DstType::Ty_LLT:
      MIB.addDef(MRI.createGenericVirtualRegister(LLTTy));
      break;
    case DstType::Ty_RC:
      MIB.addDef(MRI.createVirtualRegister(RC));
      break;
    }
  }

  LLT getLLTTy(const MachineRegisterInfo &MRI) const {
    switch (Ty) {
    case DstType::Ty_RC:
      return LLT{};
    case DstType::Ty_LLT:
      return LLTTy;
    case DstType::Ty_Reg:
      return MRI.getType(Reg);
    }
    llvm_unreachable("Unrecognised DstOp::DstType enum");
  }

  unsigned getReg() const {
    assert(Ty == DstType::Ty_Reg && "Not a register");
    return Reg;
  }

  const TargetRegisterClass *getRegClass() const {
    switch (Ty) {
    case DstType::Ty_RC:
      return RC;
    default:
      llvm_unreachable("Not a RC Operand");
    }
  }

  DstType getDstOpKind() const { return Ty; }

private:
  DstType Ty;
};

class SrcOp {
  union {
    MachineInstrBuilder SrcMIB;
    unsigned Reg;
    CmpInst::Predicate Pred;
  };

public:
  enum class SrcType { Ty_Reg, Ty_MIB, Ty_Predicate };
  SrcOp(unsigned R) : Reg(R), Ty(SrcType::Ty_Reg) {}
  SrcOp(const MachineInstrBuilder &MIB) : SrcMIB(MIB), Ty(SrcType::Ty_MIB) {}
  SrcOp(const CmpInst::Predicate P) : Pred(P), Ty(SrcType::Ty_Predicate) {}

  void addSrcToMIB(MachineInstrBuilder &MIB) const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      MIB.addPredicate(Pred);
      break;
    case SrcType::Ty_Reg:
      MIB.addUse(Reg);
      break;
    case SrcType::Ty_MIB:
      MIB.addUse(SrcMIB->getOperand(0).getReg());
      break;
    }
  }

  LLT getLLTTy(const MachineRegisterInfo &MRI) const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      llvm_unreachable("Not a register operand");
    case SrcType::Ty_Reg:
      return MRI.getType(Reg);
    case SrcType::Ty_MIB:
      return MRI.getType(SrcMIB->getOperand(0).getReg());
    }
    llvm_unreachable("Unrecognised SrcOp::SrcType enum");
  }

  unsigned getReg() const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      llvm_unreachable("Not a register operand");
    case SrcType::Ty_Reg:
      return Reg;
    case SrcType::Ty_MIB:
      return SrcMIB->getOperand(0).getReg();
    }
    llvm_unreachable("Unrecognised SrcOp::SrcType enum");
  }

  CmpInst::Predicate getPredicate() const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      return Pred;
    default:
      llvm_unreachable("Not a register operand");
    }
  }

  SrcType getSrcOpKind() const { return Ty; }

private:
  SrcType Ty;
};

class FlagsOp {
  Optional<unsigned> Flags;

public:
  explicit FlagsOp(unsigned F) : Flags(F) {}
  FlagsOp() : Flags(None) {}
  Optional<unsigned> getFlags() const { return Flags; }
};
/// Helper class to build MachineInstr.
/// It keeps internally the insertion point and debug location for all
/// the new instructions we want to create.
/// This information can be modify via the related setters.
class MachineIRBuilder {

  MachineIRBuilderState State;

protected:
  void validateTruncExt(const LLT &Dst, const LLT &Src, bool IsExtend);

  void validateBinaryOp(const LLT &Res, const LLT &Op0, const LLT &Op1);

  void validateSelectOp(const LLT &ResTy, const LLT &TstTy, const LLT &Op0Ty,
                        const LLT &Op1Ty);
  void recordInsertion(MachineInstr *MI) const;

public:
  /// Some constructors for easy use.
  MachineIRBuilder() = default;
  MachineIRBuilder(MachineFunction &MF) { setMF(MF); }
  MachineIRBuilder(MachineInstr &MI) : MachineIRBuilder(*MI.getMF()) {
    setInstr(MI);
  }

  virtual ~MachineIRBuilder() = default;

  MachineIRBuilder(const MachineIRBuilderState &BState) : State(BState) {}

  const TargetInstrInfo &getTII() {
    assert(State.TII && "TargetInstrInfo is not set");
    return *State.TII;
  }

  /// Getter for the function we currently build.
  MachineFunction &getMF() {
    assert(State.MF && "MachineFunction is not set");
    return *State.MF;
  }

  /// Getter for DebugLoc
  const DebugLoc &getDL() { return State.DL; }

  /// Getter for MRI
  MachineRegisterInfo *getMRI() { return State.MRI; }
  const MachineRegisterInfo *getMRI() const { return State.MRI; }

  /// Getter for the State
  MachineIRBuilderState &getState() { return State; }

  /// Getter for the basic block we currently build.
  const MachineBasicBlock &getMBB() const {
    assert(State.MBB && "MachineBasicBlock is not set");
    return *State.MBB;
  }

  MachineBasicBlock &getMBB() {
    return const_cast<MachineBasicBlock &>(
        const_cast<const MachineIRBuilder *>(this)->getMBB());
  }

  GISelCSEInfo *getCSEInfo() { return State.CSEInfo; }
  const GISelCSEInfo *getCSEInfo() const { return State.CSEInfo; }

  /// Current insertion point for new instructions.
  MachineBasicBlock::iterator getInsertPt() { return State.II; }

  /// Set the insertion point before the specified position.
  /// \pre MBB must be in getMF().
  /// \pre II must be a valid iterator in MBB.
  void setInsertPt(MachineBasicBlock &MBB, MachineBasicBlock::iterator II);
  /// @}

  void setCSEInfo(GISelCSEInfo *Info);

  /// \name Setters for the insertion point.
  /// @{
  /// Set the MachineFunction where to build instructions.
  void setMF(MachineFunction &MF);

  /// Set the insertion point to the  end of \p MBB.
  /// \pre \p MBB must be contained by getMF().
  void setMBB(MachineBasicBlock &MBB);

  /// Set the insertion point to before MI.
  /// \pre MI must be in getMF().
  void setInstr(MachineInstr &MI);
  /// @}

  void setChangeObserver(GISelChangeObserver &Observer);
  void stopObservingChanges();
  /// @}

  /// Set the debug location to \p DL for all the next build instructions.
  void setDebugLoc(const DebugLoc &DL) { this->State.DL = DL; }

  /// Get the current instruction's debug location.
  DebugLoc getDebugLoc() { return State.DL; }

  /// Build and insert <empty> = \p Opcode <empty>.
  /// The insertion point is the one set by the last call of either
  /// setBasicBlock or setMI.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInstr(unsigned Opcode);

  /// Build but don't insert <empty> = \p Opcode <empty>.
  ///
  /// \pre setMF, setBasicBlock or setMI  must have been called.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInstrNoInsert(unsigned Opcode);

  /// Insert an existing instruction at the insertion point.
  MachineInstrBuilder insertInstr(MachineInstrBuilder MIB);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in \p Reg (suitably modified by \p Expr).
  MachineInstrBuilder buildDirectDbgValue(unsigned Reg, const MDNode *Variable,
                                          const MDNode *Expr);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in memory at \p Reg (suitably modified by \p
  /// Expr).
  MachineInstrBuilder buildIndirectDbgValue(unsigned Reg,
                                            const MDNode *Variable,
                                            const MDNode *Expr);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in the stack slot specified by \p FI
  /// (suitably modified by \p Expr).
  MachineInstrBuilder buildFIDbgValue(int FI, const MDNode *Variable,
                                      const MDNode *Expr);

  /// Build and insert a DBG_VALUE instructions specifying that \p Variable is
  /// given by \p C (suitably modified by \p Expr).
  MachineInstrBuilder buildConstDbgValue(const Constant &C,
                                         const MDNode *Variable,
                                         const MDNode *Expr);

  /// Build and insert a DBG_LABEL instructions specifying that \p Label is
  /// given. Convert "llvm.dbg.label Label" to "DBG_LABEL Label".
  MachineInstrBuilder buildDbgLabel(const MDNode *Label);

  /// Build and insert \p Res = G_FRAME_INDEX \p Idx
  ///
  /// G_FRAME_INDEX materializes the address of an alloca value or other
  /// stack-based object.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFrameIndex(unsigned Res, int Idx);

  /// Build and insert \p Res = G_GLOBAL_VALUE \p GV
  ///
  /// G_GLOBAL_VALUE materializes the address of the specified global
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type
  ///      in the same address space as \p GV.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGlobalValue(unsigned Res, const GlobalValue *GV);


  /// Build and insert \p Res = G_GEP \p Op0, \p Op1
  ///
  /// G_GEP adds \p Op1 bytes to the pointer specified by \p Op0,
  /// storing the resulting pointer in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with pointer
  ///      type.
  /// \pre \p Op1 must be a generic virtual register with scalar type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGEP(unsigned Res, unsigned Op0,
                               unsigned Op1);

  /// Materialize and insert \p Res = G_GEP \p Op0, (G_CONSTANT \p Value)
  ///
  /// G_GEP adds \p Value bytes to the pointer specified by \p Op0,
  /// storing the resulting pointer in \p Res. If \p Value is zero then no
  /// G_GEP or G_CONSTANT will be created and \pre Op0 will be assigned to
  /// \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Op0 must be a generic virtual register with pointer type.
  /// \pre \p ValueTy must be a scalar type.
  /// \pre \p Res must be 0. This is to detect confusion between
  ///      materializeGEP() and buildGEP().
  /// \post \p Res will either be a new generic virtual register of the same
  ///       type as \p Op0 or \p Op0 itself.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  Optional<MachineInstrBuilder> materializeGEP(unsigned &Res, unsigned Op0,
                                               const LLT &ValueTy,
                                               uint64_t Value);

  /// Build and insert \p Res = G_PTR_MASK \p Op0, \p NumBits
  ///
  /// G_PTR_MASK clears the low bits of a pointer operand without destroying its
  /// pointer properties. This has the effect of rounding the address *down* to
  /// a specified alignment in bits.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with pointer
  ///      type.
  /// \pre \p NumBits must be an integer representing the number of low bits to
  ///      be cleared in \p Op0.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildPtrMask(unsigned Res, unsigned Op0,
                                   uint32_t NumBits);

  /// Build and insert \p Res, \p CarryOut = G_UADDE \p Op0,
  /// \p Op1, \p CarryIn
  ///
  /// G_UADDE sets \p Res to \p Op0 + \p Op1 + \p CarryIn (truncated to the bit
  /// width) and sets \p CarryOut to 1 if the result overflowed in unsigned
  /// arithmetic.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same scalar type.
  /// \pre \p CarryOut and \p CarryIn must be generic virtual
  ///      registers with the same scalar type (typically s1)
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildUAdde(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1,
                                 const SrcOp &CarryIn);

  /// Build and insert \p Res = G_ANYEXT \p Op0
  ///
  /// G_ANYEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are unspecified
  /// (i.e. this is neither zero nor sign-extension). For a vector register,
  /// each element is extended individually.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \return The newly created instruction.

  MachineInstrBuilder buildAnyExt(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_SEXT \p Op
  ///
  /// G_SEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are duplicated from the
  /// high bit of \p Op (i.e. 2s-complement sign extended).
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildSExt(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_ZEXT \p Op
  ///
  /// G_ZEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are 0. For a vector
  /// register, each element is extended individually.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildZExt(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_SEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildSExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_ZEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildZExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  // Build and insert \p Res = G_ANYEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildAnyExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = \p ExtOpc, \p Res = G_TRUNC \p
  /// Op, or \p Res = COPY \p Op depending on the differing sizes of \p Res and
  /// \p Op.
  ///  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildExtOrTrunc(unsigned ExtOpc, const DstOp &Res,
                                      const SrcOp &Op);

  /// Build and insert an appropriate cast between two registers of equal size.
  MachineInstrBuilder buildCast(const DstOp &Dst, const SrcOp &Src);

  /// Build and insert G_BR \p Dest
  ///
  /// G_BR is an unconditional branch to \p Dest.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBr(MachineBasicBlock &Dest);

  /// Build and insert G_BRCOND \p Tst, \p Dest
  ///
  /// G_BRCOND is a conditional branch to \p Dest.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Tst must be a generic virtual register with scalar
  ///      type. At the beginning of legalization, this will be a single
  ///      bit (s1). Targets with interesting flags registers may change
  ///      this. For a wider type, whether the branch is taken must only
  ///      depend on bit 0 (for now).
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildBrCond(unsigned Tst, MachineBasicBlock &Dest);

  /// Build and insert G_BRINDIRECT \p Tgt
  ///
  /// G_BRINDIRECT is an indirect branch to \p Tgt.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Tgt must be a generic virtual register with pointer type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBrIndirect(unsigned Tgt);

  /// Build and insert \p Res = G_CONSTANT \p Val
  ///
  /// G_CONSTANT is an integer constant with the specified size and value. \p
  /// Val will be extended or truncated to the size of \p Reg.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or pointer
  ///      type.
  ///
  /// \return The newly created instruction.
  virtual MachineInstrBuilder buildConstant(const DstOp &Res,
                                            const ConstantInt &Val);

  /// Build and insert \p Res = G_CONSTANT \p Val
  ///
  /// G_CONSTANT is an integer constant with the specified size and value.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildConstant(const DstOp &Res, int64_t Val);

  /// Build and insert \p Res = G_FCONSTANT \p Val
  ///
  /// G_FCONSTANT is a floating-point constant with the specified size and
  /// value.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \return The newly created instruction.
  virtual MachineInstrBuilder buildFConstant(const DstOp &Res,
                                             const ConstantFP &Val);

  MachineInstrBuilder buildFConstant(const DstOp &Res, double Val);

  /// Build and insert \p Res = COPY Op
  ///
  /// Register-to-register COPY sets \p Res to \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCopy(const DstOp &Res, const SrcOp &Op);

  /// Build and insert `Res = G_LOAD Addr, MMO`.
  ///
  /// Loads the value stored at \p Addr. Puts the result in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLoad(unsigned Res, unsigned Addr,
                                MachineMemOperand &MMO);

  /// Build and insert `Res = <opcode> Addr, MMO`.
  ///
  /// Loads the value stored at \p Addr. Puts the result in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLoadInstr(unsigned Opcode, unsigned Res,
                                     unsigned Addr, MachineMemOperand &MMO);

  /// Build and insert `G_STORE Val, Addr, MMO`.
  ///
  /// Stores the value \p Val to \p Addr.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Val must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildStore(unsigned Val, unsigned Addr,
                                 MachineMemOperand &MMO);

  /// Build and insert `Res0, ... = G_EXTRACT Src, Idx0`.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Src must be generic virtual registers.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildExtract(unsigned Res, unsigned Src, uint64_t Index);

  /// Build and insert \p Res = IMPLICIT_DEF.
  MachineInstrBuilder buildUndef(const DstOp &Res);

  /// Build and insert instructions to put \p Ops together at the specified p
  /// Indices to form a larger register.
  ///
  /// If the types of the input registers are uniform and cover the entirity of
  /// \p Res then a G_MERGE_VALUES will be produced. Otherwise an IMPLICIT_DEF
  /// followed by a sequence of G_INSERT instructions.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The final element of the sequence must not extend past the end of the
  ///      destination register.
  /// \pre The bits defined by each Op (derived from index and scalar size) must
  ///      not overlap.
  /// \pre \p Indices must be in ascending order of bit position.
  void buildSequence(unsigned Res, ArrayRef<unsigned> Ops,
                     ArrayRef<uint64_t> Indices);

  /// Build and insert \p Res = G_MERGE_VALUES \p Op0, ...
  ///
  /// G_MERGE_VALUES combines the input elements contiguously into a larger
  /// register.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMerge(const DstOp &Res, ArrayRef<unsigned> Ops);

  /// Build and insert \p Res0, ... = G_UNMERGE_VALUES \p Op
  ///
  /// G_UNMERGE_VALUES splits contiguous bits of the input into multiple
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all \p Res registers must be identical.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUnmerge(ArrayRef<LLT> Res, const SrcOp &Op);
  MachineInstrBuilder buildUnmerge(ArrayRef<unsigned> Res, const SrcOp &Op);

  /// Build and insert \p Res = G_BUILD_VECTOR \p Op0, ...
  ///
  /// G_BUILD_VECTOR creates a vector value from multiple scalar registers.
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the
  ///      input scalar registers.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBuildVector(const DstOp &Res,
                                       ArrayRef<unsigned> Ops);

  /// Build and insert \p Res = G_BUILD_VECTOR_TRUNC \p Op0, ...
  ///
  /// G_BUILD_VECTOR_TRUNC creates a vector value from multiple scalar registers
  /// which have types larger than the destination vector element type, and
  /// truncates the values to fit.
  ///
  /// If the operands given are already the same size as the vector elt type,
  /// then this method will instead create a G_BUILD_VECTOR instruction.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBuildVectorTrunc(const DstOp &Res,
                                            ArrayRef<unsigned> Ops);

  /// Build and insert \p Res = G_CONCAT_VECTORS \p Op0, ...
  ///
  /// G_CONCAT_VECTORS creates a vector from the concatenation of 2 or more
  /// vectors.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all source operands must be identical.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConcatVectors(const DstOp &Res,
                                         ArrayRef<unsigned> Ops);

  MachineInstrBuilder buildInsert(unsigned Res, unsigned Src,
                                  unsigned Op, unsigned Index);

  /// Build and insert either a G_INTRINSIC (if \p HasSideEffects is false) or
  /// G_INTRINSIC_W_SIDE_EFFECTS instruction. Its first operand will be the
  /// result register definition unless \p Reg is NoReg (== 0). The second
  /// operand will be the intrinsic's ID.
  ///
  /// Callers are expected to add the required definitions and uses afterwards.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntrinsic(Intrinsic::ID ID, unsigned Res,
                                     bool HasSideEffects);

  /// Build and insert \p Res = G_FPTRUNC \p Op
  ///
  /// G_FPTRUNC converts a floating-point value into one with a smaller type.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Res must be smaller than \p Op
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildFPTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_TRUNC \p Op
  ///
  /// G_TRUNC extracts the low bits of a type. For a vector type each element is
  /// truncated independently before being packed into the destination.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Res must be smaller than \p Op
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert a \p Res = G_ICMP \p Pred, \p Op0, \p Op1
  ///
  /// \pre setBasicBlock or setMI must have been called.

  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s1 or <N x s1>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res. If \p Res is a scalar,
  ///      \p Op0 must be either a scalar or pointer.
  /// \pre \p Pred must be an integer predicate.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildICmp(CmpInst::Predicate Pred, const DstOp &Res,
                                const SrcOp &Op0, const SrcOp &Op1);

  /// Build and insert a \p Res = G_FCMP \p Pred\p Op0, \p Op1
  ///
  /// \pre setBasicBlock or setMI must have been called.

  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s1 or <N x s1>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res (or scalar, if \p Res is
  ///      scalar).
  /// \pre \p Pred must be a floating-point predicate.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFCmp(CmpInst::Predicate Pred, const DstOp &Res,
                                const SrcOp &Op0, const SrcOp &Op1);

  /// Build and insert a \p Res = G_SELECT \p Tst, \p Op0, \p Op1
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same type.
  /// \pre \p Tst must be a generic virtual register with scalar, pointer or
  ///      vector type. If vector then it must have the same number of
  ///      elements as the other parameters.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSelect(const DstOp &Res, const SrcOp &Tst,
                                  const SrcOp &Op0, const SrcOp &Op1);

  /// Build and insert \p Res = G_INSERT_VECTOR_ELT \p Val,
  /// \p Elt, \p Idx
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Val must be a generic virtual register
  //       with the same vector type.
  /// \pre \p Elt and \p Idx must be a generic virtual register
  ///      with scalar type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildInsertVectorElement(const DstOp &Res,
                                               const SrcOp &Val,
                                               const SrcOp &Elt,
                                               const SrcOp &Idx);

  /// Build and insert \p Res = G_EXTRACT_VECTOR_ELT \p Val, \p Idx
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  /// \pre \p Val must be a generic virtual register with vector type.
  /// \pre \p Idx must be a generic virtual register with scalar type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildExtractVectorElement(const DstOp &Res,
                                                const SrcOp &Val,
                                                const SrcOp &Idx);

  /// Build and insert `OldValRes<def>, SuccessRes<def> =
  /// G_ATOMIC_CMPXCHG_WITH_SUCCESS Addr, CmpVal, NewVal, MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p NewVal if it is currently
  /// \p CmpVal otherwise leaves it unchanged. Puts the original value from \p
  /// Addr in \p Res, along with an s1 indicating whether it was replaced.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register of scalar type.
  /// \pre \p SuccessRes must be a generic virtual register of scalar type. It
  ///      will be assigned 0 on failure and 1 on success.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, \p CmpVal, and \p NewVal must be generic virtual
  ///      registers of the same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildAtomicCmpXchgWithSuccess(unsigned OldValRes, unsigned SuccessRes,
                                unsigned Addr, unsigned CmpVal, unsigned NewVal,
                                MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMIC_CMPXCHG Addr, CmpVal, NewVal,
  /// MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p NewVal if it is currently
  /// \p CmpVal otherwise leaves it unchanged. Puts the original value from \p
  /// Addr in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register of scalar type.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, \p CmpVal, and \p NewVal must be generic virtual
  ///      registers of the same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicCmpXchg(unsigned OldValRes, unsigned Addr,
                                         unsigned CmpVal, unsigned NewVal,
                                         MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_<Opcode> Addr, Val, MMO`.
  ///
  /// Atomically read-modify-update the value at \p Addr with \p Val. Puts the
  /// original value from \p Addr in \p OldValRes. The modification is
  /// determined by the opcode.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMW(unsigned Opcode, unsigned OldValRes,
                                     unsigned Addr, unsigned Val,
                                     MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_XCHG Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p Val. Puts the original
  /// value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWXchg(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_ADD Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the addition of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWAdd(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_SUB Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the subtraction of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWSub(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_AND Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise and of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWAnd(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_NAND Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise nand of \p Val
  /// and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWNand(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_OR Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise or of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWOr(unsigned OldValRes, unsigned Addr,
                                       unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_XOR Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise xor of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWXor(unsigned OldValRes, unsigned Addr,
                                        unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_MAX Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the signed maximum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWMax(unsigned OldValRes, unsigned Addr,
                                        unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_MIN Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the signed minimum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWMin(unsigned OldValRes, unsigned Addr,
                                        unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_UMAX Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the unsigned maximum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWUmax(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_UMIN Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the unsigned minimum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWUmin(unsigned OldValRes, unsigned Addr,
                                         unsigned Val, MachineMemOperand &MMO);

  /// Build and insert \p Res = G_BLOCK_ADDR \p BA
  ///
  /// G_BLOCK_ADDR computes the address of a basic block.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register of a pointer type.
  ///
  /// \return The newly created instruction.
  MachineInstrBuilder buildBlockAddress(unsigned Res, const BlockAddress *BA);

  /// Build and insert \p Res = G_ADD \p Op0, \p Op1
  ///
  /// G_ADD sets \p Res to the sum of integer parameters \p Op0 and \p Op1,
  /// truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildAdd(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               Optional<unsigned> Flags = None) {
    return buildInstr(TargetOpcode::G_ADD, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_SUB \p Op0, \p Op1
  ///
  /// G_SUB sets \p Res to the sum of integer parameters \p Op0 and \p Op1,
  /// truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildSub(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               Optional<unsigned> Flags = None) {
    return buildInstr(TargetOpcode::G_SUB, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_MUL \p Op0, \p Op1
  ///
  /// G_MUL sets \p Res to the sum of integer parameters \p Op0 and \p Op1,
  /// truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMul(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               Optional<unsigned> Flags = None) {
    return buildInstr(TargetOpcode::G_MUL, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_AND \p Op0, \p Op1
  ///
  /// G_AND sets \p Res to the bitwise and of integer parameters \p Op0 and \p
  /// Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildAnd(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_AND, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_OR \p Op0, \p Op1
  ///
  /// G_OR sets \p Res to the bitwise or of integer parameters \p Op0 and \p
  /// Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildOr(const DstOp &Dst, const SrcOp &Src0,
                              const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_OR, {Dst}, {Src0, Src1});
  }

  virtual MachineInstrBuilder buildInstr(unsigned Opc, ArrayRef<DstOp> DstOps,
                                         ArrayRef<SrcOp> SrcOps,
                                         Optional<unsigned> Flags = None);
};

} // End namespace llvm.
#endif // LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H
