//===-- llvm/CodeGen/GlobalISel/MachineIRBuilder.cpp - MIBuilder--*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the MachineIRBuidler class.
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfo.h"

using namespace llvm;

void MachineIRBuilder::setMF(MachineFunction &MF) {
  State.MF = &MF;
  State.MBB = nullptr;
  State.MRI = &MF.getRegInfo();
  State.TII = MF.getSubtarget().getInstrInfo();
  State.DL = DebugLoc();
  State.II = MachineBasicBlock::iterator();
  State.Observer = nullptr;
}

void MachineIRBuilder::setMBB(MachineBasicBlock &MBB) {
  State.MBB = &MBB;
  State.II = MBB.end();
  assert(&getMF() == MBB.getParent() &&
         "Basic block is in a different function");
}

void MachineIRBuilder::setInstr(MachineInstr &MI) {
  assert(MI.getParent() && "Instruction is not part of a basic block");
  setMBB(*MI.getParent());
  State.II = MI.getIterator();
}

void MachineIRBuilder::setCSEInfo(GISelCSEInfo *Info) { State.CSEInfo = Info; }

void MachineIRBuilder::setInsertPt(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator II) {
  assert(MBB.getParent() == &getMF() &&
         "Basic block is in a different function");
  State.MBB = &MBB;
  State.II = II;
}

void MachineIRBuilder::recordInsertion(MachineInstr *InsertedInstr) const {
  if (State.Observer)
    State.Observer->createdInstr(*InsertedInstr);
}

void MachineIRBuilder::setChangeObserver(GISelChangeObserver &Observer) {
  State.Observer = &Observer;
}

void MachineIRBuilder::stopObservingChanges() { State.Observer = nullptr; }

//------------------------------------------------------------------------------
// Build instruction variants.
//------------------------------------------------------------------------------

MachineInstrBuilder MachineIRBuilder::buildInstr(unsigned Opcode) {
  return insertInstr(buildInstrNoInsert(Opcode));
}

MachineInstrBuilder MachineIRBuilder::buildInstrNoInsert(unsigned Opcode) {
  MachineInstrBuilder MIB = BuildMI(getMF(), getDL(), getTII().get(Opcode));
  return MIB;
}

MachineInstrBuilder MachineIRBuilder::insertInstr(MachineInstrBuilder MIB) {
  getMBB().insert(getInsertPt(), MIB);
  recordInsertion(MIB);
  return MIB;
}

MachineInstrBuilder
MachineIRBuilder::buildDirectDbgValue(unsigned Reg, const MDNode *Variable,
                                      const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(
      cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(getDL()) &&
      "Expected inlined-at fields to agree");
  return insertInstr(BuildMI(getMF(), getDL(),
                             getTII().get(TargetOpcode::DBG_VALUE),
                             /*IsIndirect*/ false, Reg, Variable, Expr));
}

MachineInstrBuilder
MachineIRBuilder::buildIndirectDbgValue(unsigned Reg, const MDNode *Variable,
                                        const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(
      cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(getDL()) &&
      "Expected inlined-at fields to agree");
  return insertInstr(BuildMI(getMF(), getDL(),
                             getTII().get(TargetOpcode::DBG_VALUE),
                             /*IsIndirect*/ true, Reg, Variable, Expr));
}

MachineInstrBuilder MachineIRBuilder::buildFIDbgValue(int FI,
                                                      const MDNode *Variable,
                                                      const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(
      cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(getDL()) &&
      "Expected inlined-at fields to agree");
  return buildInstr(TargetOpcode::DBG_VALUE)
      .addFrameIndex(FI)
      .addImm(0)
      .addMetadata(Variable)
      .addMetadata(Expr);
}

MachineInstrBuilder MachineIRBuilder::buildConstDbgValue(const Constant &C,
                                                         const MDNode *Variable,
                                                         const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(
      cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(getDL()) &&
      "Expected inlined-at fields to agree");
  auto MIB = buildInstr(TargetOpcode::DBG_VALUE);
  if (auto *CI = dyn_cast<ConstantInt>(&C)) {
    if (CI->getBitWidth() > 64)
      MIB.addCImm(CI);
    else
      MIB.addImm(CI->getZExtValue());
  } else if (auto *CFP = dyn_cast<ConstantFP>(&C)) {
    MIB.addFPImm(CFP);
  } else {
    // Insert %noreg if we didn't find a usable constant and had to drop it.
    MIB.addReg(0U);
  }

  return MIB.addImm(0).addMetadata(Variable).addMetadata(Expr);
}

MachineInstrBuilder MachineIRBuilder::buildDbgLabel(const MDNode *Label) {
  assert(isa<DILabel>(Label) && "not a label");
  assert(cast<DILabel>(Label)->isValidLocationForIntrinsic(State.DL) &&
         "Expected inlined-at fields to agree");
  auto MIB = buildInstr(TargetOpcode::DBG_LABEL);

  return MIB.addMetadata(Label);
}

MachineInstrBuilder MachineIRBuilder::buildFrameIndex(unsigned Res, int Idx) {
  assert(getMRI()->getType(Res).isPointer() && "invalid operand type");
  return buildInstr(TargetOpcode::G_FRAME_INDEX)
      .addDef(Res)
      .addFrameIndex(Idx);
}

MachineInstrBuilder MachineIRBuilder::buildGlobalValue(unsigned Res,
                                                       const GlobalValue *GV) {
  assert(getMRI()->getType(Res).isPointer() && "invalid operand type");
  assert(getMRI()->getType(Res).getAddressSpace() ==
             GV->getType()->getAddressSpace() &&
         "address space mismatch");

  return buildInstr(TargetOpcode::G_GLOBAL_VALUE)
      .addDef(Res)
      .addGlobalAddress(GV);
}

void MachineIRBuilder::validateBinaryOp(const LLT &Res, const LLT &Op0,
                                        const LLT &Op1) {
  assert((Res.isScalar() || Res.isVector()) && "invalid operand type");
  assert((Res == Op0 && Res == Op1) && "type mismatch");
}

MachineInstrBuilder MachineIRBuilder::buildGEP(unsigned Res, unsigned Op0,
                                               unsigned Op1) {
  assert(getMRI()->getType(Res).isPointer() &&
         getMRI()->getType(Res) == getMRI()->getType(Op0) && "type mismatch");
  assert(getMRI()->getType(Op1).isScalar() && "invalid offset type");

  return buildInstr(TargetOpcode::G_GEP)
      .addDef(Res)
      .addUse(Op0)
      .addUse(Op1);
}

Optional<MachineInstrBuilder>
MachineIRBuilder::materializeGEP(unsigned &Res, unsigned Op0,
                                 const LLT &ValueTy, uint64_t Value) {
  assert(Res == 0 && "Res is a result argument");
  assert(ValueTy.isScalar()  && "invalid offset type");

  if (Value == 0) {
    Res = Op0;
    return None;
  }

  Res = getMRI()->createGenericVirtualRegister(getMRI()->getType(Op0));
  unsigned TmpReg = getMRI()->createGenericVirtualRegister(ValueTy);

  buildConstant(TmpReg, Value);
  return buildGEP(Res, Op0, TmpReg);
}

MachineInstrBuilder MachineIRBuilder::buildPtrMask(unsigned Res, unsigned Op0,
                                                   uint32_t NumBits) {
  assert(getMRI()->getType(Res).isPointer() &&
         getMRI()->getType(Res) == getMRI()->getType(Op0) && "type mismatch");

  return buildInstr(TargetOpcode::G_PTR_MASK)
      .addDef(Res)
      .addUse(Op0)
      .addImm(NumBits);
}

MachineInstrBuilder MachineIRBuilder::buildBr(MachineBasicBlock &Dest) {
  return buildInstr(TargetOpcode::G_BR).addMBB(&Dest);
}

MachineInstrBuilder MachineIRBuilder::buildBrIndirect(unsigned Tgt) {
  assert(getMRI()->getType(Tgt).isPointer() && "invalid branch destination");
  return buildInstr(TargetOpcode::G_BRINDIRECT).addUse(Tgt);
}

MachineInstrBuilder MachineIRBuilder::buildCopy(const DstOp &Res,
                                                const SrcOp &Op) {
  return buildInstr(TargetOpcode::COPY, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildConstant(const DstOp &Res,
                                                    const ConstantInt &Val) {
  LLT Ty = Res.getLLTTy(*getMRI());

  assert((Ty.isScalar() || Ty.isPointer()) && "invalid operand type");

  const ConstantInt *NewVal = &Val;
  if (Ty.getSizeInBits() != Val.getBitWidth())
    NewVal = ConstantInt::get(getMF().getFunction().getContext(),
                              Val.getValue().sextOrTrunc(Ty.getSizeInBits()));

  auto MIB = buildInstr(TargetOpcode::G_CONSTANT);
  Res.addDefToMIB(*getMRI(), MIB);
  MIB.addCImm(NewVal);
  return MIB;
}

MachineInstrBuilder MachineIRBuilder::buildConstant(const DstOp &Res,
                                                    int64_t Val) {
  auto IntN = IntegerType::get(getMF().getFunction().getContext(),
                               Res.getLLTTy(*getMRI()).getSizeInBits());
  ConstantInt *CI = ConstantInt::get(IntN, Val, true);
  return buildConstant(Res, *CI);
}

MachineInstrBuilder MachineIRBuilder::buildFConstant(const DstOp &Res,
                                                     const ConstantFP &Val) {
  assert(Res.getLLTTy(*getMRI()).isScalar() && "invalid operand type");

  auto MIB = buildInstr(TargetOpcode::G_FCONSTANT);
  Res.addDefToMIB(*getMRI(), MIB);
  MIB.addFPImm(&Val);
  return MIB;
}

MachineInstrBuilder MachineIRBuilder::buildFConstant(const DstOp &Res,
                                                     double Val) {
  LLT DstTy = Res.getLLTTy(*getMRI());
  auto &Ctx = getMF().getFunction().getContext();
  auto *CFP =
      ConstantFP::get(Ctx, getAPFloatFromSize(Val, DstTy.getSizeInBits()));
  return buildFConstant(Res, *CFP);
}

MachineInstrBuilder MachineIRBuilder::buildBrCond(unsigned Tst,
                                                  MachineBasicBlock &Dest) {
  assert(getMRI()->getType(Tst).isScalar() && "invalid operand type");

  return buildInstr(TargetOpcode::G_BRCOND).addUse(Tst).addMBB(&Dest);
}

MachineInstrBuilder MachineIRBuilder::buildLoad(unsigned Res, unsigned Addr,
                                                MachineMemOperand &MMO) {
  return buildLoadInstr(TargetOpcode::G_LOAD, Res, Addr, MMO);
}

MachineInstrBuilder MachineIRBuilder::buildLoadInstr(unsigned Opcode,
                                                     unsigned Res,
                                                     unsigned Addr,
                                                     MachineMemOperand &MMO) {
  assert(getMRI()->getType(Res).isValid() && "invalid operand type");
  assert(getMRI()->getType(Addr).isPointer() && "invalid operand type");

  return buildInstr(Opcode)
      .addDef(Res)
      .addUse(Addr)
      .addMemOperand(&MMO);
}

MachineInstrBuilder MachineIRBuilder::buildStore(unsigned Val, unsigned Addr,
                                                 MachineMemOperand &MMO) {
  assert(getMRI()->getType(Val).isValid() && "invalid operand type");
  assert(getMRI()->getType(Addr).isPointer() && "invalid operand type");

  return buildInstr(TargetOpcode::G_STORE)
      .addUse(Val)
      .addUse(Addr)
      .addMemOperand(&MMO);
}

MachineInstrBuilder MachineIRBuilder::buildUAdde(const DstOp &Res,
                                                 const DstOp &CarryOut,
                                                 const SrcOp &Op0,
                                                 const SrcOp &Op1,
                                                 const SrcOp &CarryIn) {
  return buildInstr(TargetOpcode::G_UADDE, {Res, CarryOut},
                    {Op0, Op1, CarryIn});
}

MachineInstrBuilder MachineIRBuilder::buildAnyExt(const DstOp &Res,
                                                  const SrcOp &Op) {
  return buildInstr(TargetOpcode::G_ANYEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildSExt(const DstOp &Res,
                                                const SrcOp &Op) {
  return buildInstr(TargetOpcode::G_SEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildZExt(const DstOp &Res,
                                                const SrcOp &Op) {
  return buildInstr(TargetOpcode::G_ZEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildExtOrTrunc(unsigned ExtOpc,
                                                      const DstOp &Res,
                                                      const SrcOp &Op) {
  assert((TargetOpcode::G_ANYEXT == ExtOpc || TargetOpcode::G_ZEXT == ExtOpc ||
          TargetOpcode::G_SEXT == ExtOpc) &&
         "Expecting Extending Opc");
  assert(Res.getLLTTy(*getMRI()).isScalar() ||
         Res.getLLTTy(*getMRI()).isVector());
  assert(Res.getLLTTy(*getMRI()).isScalar() ==
         Op.getLLTTy(*getMRI()).isScalar());

  unsigned Opcode = TargetOpcode::COPY;
  if (Res.getLLTTy(*getMRI()).getSizeInBits() >
      Op.getLLTTy(*getMRI()).getSizeInBits())
    Opcode = ExtOpc;
  else if (Res.getLLTTy(*getMRI()).getSizeInBits() <
           Op.getLLTTy(*getMRI()).getSizeInBits())
    Opcode = TargetOpcode::G_TRUNC;
  else
    assert(Res.getLLTTy(*getMRI()) == Op.getLLTTy(*getMRI()));

  return buildInstr(Opcode, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildSExtOrTrunc(const DstOp &Res,
                                                       const SrcOp &Op) {
  return buildExtOrTrunc(TargetOpcode::G_SEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildZExtOrTrunc(const DstOp &Res,
                                                       const SrcOp &Op) {
  return buildExtOrTrunc(TargetOpcode::G_ZEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildAnyExtOrTrunc(const DstOp &Res,
                                                         const SrcOp &Op) {
  return buildExtOrTrunc(TargetOpcode::G_ANYEXT, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildCast(const DstOp &Dst,
                                                const SrcOp &Src) {
  LLT SrcTy = Src.getLLTTy(*getMRI());
  LLT DstTy = Dst.getLLTTy(*getMRI());
  if (SrcTy == DstTy)
    return buildCopy(Dst, Src);

  unsigned Opcode;
  if (SrcTy.isPointer() && DstTy.isScalar())
    Opcode = TargetOpcode::G_PTRTOINT;
  else if (DstTy.isPointer() && SrcTy.isScalar())
    Opcode = TargetOpcode::G_INTTOPTR;
  else {
    assert(!SrcTy.isPointer() && !DstTy.isPointer() && "n G_ADDRCAST yet");
    Opcode = TargetOpcode::G_BITCAST;
  }

  return buildInstr(Opcode, Dst, Src);
}

MachineInstrBuilder MachineIRBuilder::buildExtract(unsigned Res, unsigned Src,
                                                   uint64_t Index) {
#ifndef NDEBUG
  assert(getMRI()->getType(Src).isValid() && "invalid operand type");
  assert(getMRI()->getType(Res).isValid() && "invalid operand type");
  assert(Index + getMRI()->getType(Res).getSizeInBits() <=
             getMRI()->getType(Src).getSizeInBits() &&
         "extracting off end of register");
#endif

  if (getMRI()->getType(Res).getSizeInBits() ==
      getMRI()->getType(Src).getSizeInBits()) {
    assert(Index == 0 && "insertion past the end of a register");
    return buildCast(Res, Src);
  }

  return buildInstr(TargetOpcode::G_EXTRACT)
      .addDef(Res)
      .addUse(Src)
      .addImm(Index);
}

void MachineIRBuilder::buildSequence(unsigned Res, ArrayRef<unsigned> Ops,
                                     ArrayRef<uint64_t> Indices) {
#ifndef NDEBUG
  assert(Ops.size() == Indices.size() && "incompatible args");
  assert(!Ops.empty() && "invalid trivial sequence");
  assert(std::is_sorted(Indices.begin(), Indices.end()) &&
         "sequence offsets must be in ascending order");

  assert(getMRI()->getType(Res).isValid() && "invalid operand type");
  for (auto Op : Ops)
    assert(getMRI()->getType(Op).isValid() && "invalid operand type");
#endif

  LLT ResTy = getMRI()->getType(Res);
  LLT OpTy = getMRI()->getType(Ops[0]);
  unsigned OpSize = OpTy.getSizeInBits();
  bool MaybeMerge = true;
  for (unsigned i = 0; i < Ops.size(); ++i) {
    if (getMRI()->getType(Ops[i]) != OpTy || Indices[i] != i * OpSize) {
      MaybeMerge = false;
      break;
    }
  }

  if (MaybeMerge && Ops.size() * OpSize == ResTy.getSizeInBits()) {
    buildMerge(Res, Ops);
    return;
  }

  unsigned ResIn = getMRI()->createGenericVirtualRegister(ResTy);
  buildUndef(ResIn);

  for (unsigned i = 0; i < Ops.size(); ++i) {
    unsigned ResOut = i + 1 == Ops.size()
                          ? Res
                          : getMRI()->createGenericVirtualRegister(ResTy);
    buildInsert(ResOut, ResIn, Ops[i], Indices[i]);
    ResIn = ResOut;
  }
}

MachineInstrBuilder MachineIRBuilder::buildUndef(const DstOp &Res) {
  return buildInstr(TargetOpcode::G_IMPLICIT_DEF, {Res}, {});
}

MachineInstrBuilder MachineIRBuilder::buildMerge(const DstOp &Res,
                                                 ArrayRef<unsigned> Ops) {
  // Unfortunately to convert from ArrayRef<LLT> to ArrayRef<SrcOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<SrcOp, 8> TmpVec(Ops.begin(), Ops.end());
  return buildInstr(TargetOpcode::G_MERGE_VALUES, Res, TmpVec);
}

MachineInstrBuilder MachineIRBuilder::buildUnmerge(ArrayRef<LLT> Res,
                                                   const SrcOp &Op) {
  // Unfortunately to convert from ArrayRef<LLT> to ArrayRef<DstOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<DstOp, 8> TmpVec(Res.begin(), Res.end());
  return buildInstr(TargetOpcode::G_UNMERGE_VALUES, TmpVec, Op);
}

MachineInstrBuilder MachineIRBuilder::buildUnmerge(ArrayRef<unsigned> Res,
                                                   const SrcOp &Op) {
  // Unfortunately to convert from ArrayRef<unsigned> to ArrayRef<DstOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<DstOp, 8> TmpVec(Res.begin(), Res.end());
  return buildInstr(TargetOpcode::G_UNMERGE_VALUES, TmpVec, Op);
}

MachineInstrBuilder MachineIRBuilder::buildBuildVector(const DstOp &Res,
                                                       ArrayRef<unsigned> Ops) {
  // Unfortunately to convert from ArrayRef<unsigned> to ArrayRef<SrcOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<SrcOp, 8> TmpVec(Ops.begin(), Ops.end());
  return buildInstr(TargetOpcode::G_BUILD_VECTOR, Res, TmpVec);
}

MachineInstrBuilder
MachineIRBuilder::buildBuildVectorTrunc(const DstOp &Res,
                                        ArrayRef<unsigned> Ops) {
  // Unfortunately to convert from ArrayRef<unsigned> to ArrayRef<SrcOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<SrcOp, 8> TmpVec(Ops.begin(), Ops.end());
  return buildInstr(TargetOpcode::G_BUILD_VECTOR_TRUNC, Res, TmpVec);
}

MachineInstrBuilder
MachineIRBuilder::buildConcatVectors(const DstOp &Res, ArrayRef<unsigned> Ops) {
  // Unfortunately to convert from ArrayRef<unsigned> to ArrayRef<SrcOp>,
  // we need some temporary storage for the DstOp objects. Here we use a
  // sufficiently large SmallVector to not go through the heap.
  SmallVector<SrcOp, 8> TmpVec(Ops.begin(), Ops.end());
  return buildInstr(TargetOpcode::G_CONCAT_VECTORS, Res, TmpVec);
}

MachineInstrBuilder MachineIRBuilder::buildInsert(unsigned Res, unsigned Src,
                                                  unsigned Op, unsigned Index) {
  assert(Index + getMRI()->getType(Op).getSizeInBits() <=
             getMRI()->getType(Res).getSizeInBits() &&
         "insertion past the end of a register");

  if (getMRI()->getType(Res).getSizeInBits() ==
      getMRI()->getType(Op).getSizeInBits()) {
    return buildCast(Res, Op);
  }

  return buildInstr(TargetOpcode::G_INSERT)
      .addDef(Res)
      .addUse(Src)
      .addUse(Op)
      .addImm(Index);
}

MachineInstrBuilder MachineIRBuilder::buildIntrinsic(Intrinsic::ID ID,
                                                     unsigned Res,
                                                     bool HasSideEffects) {
  auto MIB =
      buildInstr(HasSideEffects ? TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS
                                : TargetOpcode::G_INTRINSIC);
  if (Res)
    MIB.addDef(Res);
  MIB.addIntrinsicID(ID);
  return MIB;
}

MachineInstrBuilder MachineIRBuilder::buildTrunc(const DstOp &Res,
                                                 const SrcOp &Op) {
  return buildInstr(TargetOpcode::G_TRUNC, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildFPTrunc(const DstOp &Res,
                                                   const SrcOp &Op) {
  return buildInstr(TargetOpcode::G_FPTRUNC, Res, Op);
}

MachineInstrBuilder MachineIRBuilder::buildICmp(CmpInst::Predicate Pred,
                                                const DstOp &Res,
                                                const SrcOp &Op0,
                                                const SrcOp &Op1) {
  return buildInstr(TargetOpcode::G_ICMP, Res, {Pred, Op0, Op1});
}

MachineInstrBuilder MachineIRBuilder::buildFCmp(CmpInst::Predicate Pred,
                                                const DstOp &Res,
                                                const SrcOp &Op0,
                                                const SrcOp &Op1) {

  return buildInstr(TargetOpcode::G_FCMP, Res, {Pred, Op0, Op1});
}

MachineInstrBuilder MachineIRBuilder::buildSelect(const DstOp &Res,
                                                  const SrcOp &Tst,
                                                  const SrcOp &Op0,
                                                  const SrcOp &Op1) {

  return buildInstr(TargetOpcode::G_SELECT, {Res}, {Tst, Op0, Op1});
}

MachineInstrBuilder
MachineIRBuilder::buildInsertVectorElement(const DstOp &Res, const SrcOp &Val,
                                           const SrcOp &Elt, const SrcOp &Idx) {
  return buildInstr(TargetOpcode::G_INSERT_VECTOR_ELT, Res, {Val, Elt, Idx});
}

MachineInstrBuilder
MachineIRBuilder::buildExtractVectorElement(const DstOp &Res, const SrcOp &Val,
                                            const SrcOp &Idx) {
  return buildInstr(TargetOpcode::G_EXTRACT_VECTOR_ELT, Res, {Val, Idx});
}

MachineInstrBuilder MachineIRBuilder::buildAtomicCmpXchgWithSuccess(
    unsigned OldValRes, unsigned SuccessRes, unsigned Addr, unsigned CmpVal,
    unsigned NewVal, MachineMemOperand &MMO) {
#ifndef NDEBUG
  LLT OldValResTy = getMRI()->getType(OldValRes);
  LLT SuccessResTy = getMRI()->getType(SuccessRes);
  LLT AddrTy = getMRI()->getType(Addr);
  LLT CmpValTy = getMRI()->getType(CmpVal);
  LLT NewValTy = getMRI()->getType(NewVal);
  assert(OldValResTy.isScalar() && "invalid operand type");
  assert(SuccessResTy.isScalar() && "invalid operand type");
  assert(AddrTy.isPointer() && "invalid operand type");
  assert(CmpValTy.isValid() && "invalid operand type");
  assert(NewValTy.isValid() && "invalid operand type");
  assert(OldValResTy == CmpValTy && "type mismatch");
  assert(OldValResTy == NewValTy && "type mismatch");
#endif

  return buildInstr(TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS)
      .addDef(OldValRes)
      .addDef(SuccessRes)
      .addUse(Addr)
      .addUse(CmpVal)
      .addUse(NewVal)
      .addMemOperand(&MMO);
}

MachineInstrBuilder
MachineIRBuilder::buildAtomicCmpXchg(unsigned OldValRes, unsigned Addr,
                                     unsigned CmpVal, unsigned NewVal,
                                     MachineMemOperand &MMO) {
#ifndef NDEBUG
  LLT OldValResTy = getMRI()->getType(OldValRes);
  LLT AddrTy = getMRI()->getType(Addr);
  LLT CmpValTy = getMRI()->getType(CmpVal);
  LLT NewValTy = getMRI()->getType(NewVal);
  assert(OldValResTy.isScalar() && "invalid operand type");
  assert(AddrTy.isPointer() && "invalid operand type");
  assert(CmpValTy.isValid() && "invalid operand type");
  assert(NewValTy.isValid() && "invalid operand type");
  assert(OldValResTy == CmpValTy && "type mismatch");
  assert(OldValResTy == NewValTy && "type mismatch");
#endif

  return buildInstr(TargetOpcode::G_ATOMIC_CMPXCHG)
      .addDef(OldValRes)
      .addUse(Addr)
      .addUse(CmpVal)
      .addUse(NewVal)
      .addMemOperand(&MMO);
}

MachineInstrBuilder MachineIRBuilder::buildAtomicRMW(unsigned Opcode,
                                                     unsigned OldValRes,
                                                     unsigned Addr,
                                                     unsigned Val,
                                                     MachineMemOperand &MMO) {
#ifndef NDEBUG
  LLT OldValResTy = getMRI()->getType(OldValRes);
  LLT AddrTy = getMRI()->getType(Addr);
  LLT ValTy = getMRI()->getType(Val);
  assert(OldValResTy.isScalar() && "invalid operand type");
  assert(AddrTy.isPointer() && "invalid operand type");
  assert(ValTy.isValid() && "invalid operand type");
  assert(OldValResTy == ValTy && "type mismatch");
#endif

  return buildInstr(Opcode)
      .addDef(OldValRes)
      .addUse(Addr)
      .addUse(Val)
      .addMemOperand(&MMO);
}

MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWXchg(unsigned OldValRes, unsigned Addr,
                                     unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_XCHG, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWAdd(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_ADD, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWSub(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_SUB, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWAnd(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_AND, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWNand(unsigned OldValRes, unsigned Addr,
                                     unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_NAND, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder MachineIRBuilder::buildAtomicRMWOr(unsigned OldValRes,
                                                       unsigned Addr,
                                                       unsigned Val,
                                                       MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_OR, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWXor(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_XOR, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWMax(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_MAX, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWMin(unsigned OldValRes, unsigned Addr,
                                    unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_MIN, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWUmax(unsigned OldValRes, unsigned Addr,
                                     unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_UMAX, OldValRes, Addr, Val,
                        MMO);
}
MachineInstrBuilder
MachineIRBuilder::buildAtomicRMWUmin(unsigned OldValRes, unsigned Addr,
                                     unsigned Val, MachineMemOperand &MMO) {
  return buildAtomicRMW(TargetOpcode::G_ATOMICRMW_UMIN, OldValRes, Addr, Val,
                        MMO);
}

MachineInstrBuilder
MachineIRBuilder::buildBlockAddress(unsigned Res, const BlockAddress *BA) {
#ifndef NDEBUG
  assert(getMRI()->getType(Res).isPointer() && "invalid res type");
#endif

  return buildInstr(TargetOpcode::G_BLOCK_ADDR).addDef(Res).addBlockAddress(BA);
}

void MachineIRBuilder::validateTruncExt(const LLT &DstTy, const LLT &SrcTy,
                                        bool IsExtend) {
#ifndef NDEBUG
  if (DstTy.isVector()) {
    assert(SrcTy.isVector() && "mismatched cast between vector and non-vector");
    assert(SrcTy.getNumElements() == DstTy.getNumElements() &&
           "different number of elements in a trunc/ext");
  } else
    assert(DstTy.isScalar() && SrcTy.isScalar() && "invalid extend/trunc");

  if (IsExtend)
    assert(DstTy.getSizeInBits() > SrcTy.getSizeInBits() &&
           "invalid narrowing extend");
  else
    assert(DstTy.getSizeInBits() < SrcTy.getSizeInBits() &&
           "invalid widening trunc");
#endif
}

void MachineIRBuilder::validateSelectOp(const LLT &ResTy, const LLT &TstTy,
                                        const LLT &Op0Ty, const LLT &Op1Ty) {
#ifndef NDEBUG
  assert((ResTy.isScalar() || ResTy.isVector() || ResTy.isPointer()) &&
         "invalid operand type");
  assert((ResTy == Op0Ty && ResTy == Op1Ty) && "type mismatch");
  if (ResTy.isScalar() || ResTy.isPointer())
    assert(TstTy.isScalar() && "type mismatch");
  else
    assert((TstTy.isScalar() ||
            (TstTy.isVector() &&
             TstTy.getNumElements() == Op0Ty.getNumElements())) &&
           "type mismatch");
#endif
}

MachineInstrBuilder MachineIRBuilder::buildInstr(unsigned Opc,
                                                 ArrayRef<DstOp> DstOps,
                                                 ArrayRef<SrcOp> SrcOps,
                                                 Optional<unsigned> Flags) {
  switch (Opc) {
  default:
    break;
  case TargetOpcode::G_SELECT: {
    assert(DstOps.size() == 1 && "Invalid select");
    assert(SrcOps.size() == 3 && "Invalid select");
    validateSelectOp(
        DstOps[0].getLLTTy(*getMRI()), SrcOps[0].getLLTTy(*getMRI()),
        SrcOps[1].getLLTTy(*getMRI()), SrcOps[2].getLLTTy(*getMRI()));
    break;
  }
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_AND:
  case TargetOpcode::G_ASHR:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_XOR:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UREM:
  case TargetOpcode::G_SREM: {
    // All these are binary ops.
    assert(DstOps.size() == 1 && "Invalid Dst");
    assert(SrcOps.size() == 2 && "Invalid Srcs");
    validateBinaryOp(DstOps[0].getLLTTy(*getMRI()),
                     SrcOps[0].getLLTTy(*getMRI()),
                     SrcOps[1].getLLTTy(*getMRI()));
    break;
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_ANYEXT:
    assert(DstOps.size() == 1 && "Invalid Dst");
    assert(SrcOps.size() == 1 && "Invalid Srcs");
    validateTruncExt(DstOps[0].getLLTTy(*getMRI()),
                     SrcOps[0].getLLTTy(*getMRI()), true);
    break;
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_FPTRUNC:
    assert(DstOps.size() == 1 && "Invalid Dst");
    assert(SrcOps.size() == 1 && "Invalid Srcs");
    validateTruncExt(DstOps[0].getLLTTy(*getMRI()),
                     SrcOps[0].getLLTTy(*getMRI()), false);
    break;
  }
  case TargetOpcode::COPY:
    assert(DstOps.size() == 1 && "Invalid Dst");
    assert(SrcOps.size() == 1 && "Invalid Srcs");
    assert(DstOps[0].getLLTTy(*getMRI()) == LLT() ||
           SrcOps[0].getLLTTy(*getMRI()) == LLT() ||
           DstOps[0].getLLTTy(*getMRI()) == SrcOps[0].getLLTTy(*getMRI()));
    break;
  case TargetOpcode::G_FCMP:
  case TargetOpcode::G_ICMP: {
    assert(DstOps.size() == 1 && "Invalid Dst Operands");
    assert(SrcOps.size() == 3 && "Invalid Src Operands");
    // For F/ICMP, the first src operand is the predicate, followed by
    // the two comparands.
    assert(SrcOps[0].getSrcOpKind() == SrcOp::SrcType::Ty_Predicate &&
           "Expecting predicate");
    assert([&]() -> bool {
      CmpInst::Predicate Pred = SrcOps[0].getPredicate();
      return Opc == TargetOpcode::G_ICMP ? CmpInst::isIntPredicate(Pred)
                                         : CmpInst::isFPPredicate(Pred);
    }() && "Invalid predicate");
    assert(SrcOps[1].getLLTTy(*getMRI()) == SrcOps[2].getLLTTy(*getMRI()) &&
           "Type mismatch");
    assert([&]() -> bool {
      LLT Op0Ty = SrcOps[1].getLLTTy(*getMRI());
      LLT DstTy = DstOps[0].getLLTTy(*getMRI());
      if (Op0Ty.isScalar() || Op0Ty.isPointer())
        return DstTy.isScalar();
      else
        return DstTy.isVector() &&
               DstTy.getNumElements() == Op0Ty.getNumElements();
    }() && "Type Mismatch");
    break;
  }
  case TargetOpcode::G_UNMERGE_VALUES: {
    assert(!DstOps.empty() && "Invalid trivial sequence");
    assert(SrcOps.size() == 1 && "Invalid src for Unmerge");
    assert(std::all_of(DstOps.begin(), DstOps.end(),
                       [&, this](const DstOp &Op) {
                         return Op.getLLTTy(*getMRI()) ==
                                DstOps[0].getLLTTy(*getMRI());
                       }) &&
           "type mismatch in output list");
    assert(DstOps.size() * DstOps[0].getLLTTy(*getMRI()).getSizeInBits() ==
               SrcOps[0].getLLTTy(*getMRI()).getSizeInBits() &&
           "input operands do not cover output register");
    break;
  }
  case TargetOpcode::G_MERGE_VALUES: {
    assert(!SrcOps.empty() && "invalid trivial sequence");
    assert(DstOps.size() == 1 && "Invalid Dst");
    assert(std::all_of(SrcOps.begin(), SrcOps.end(),
                       [&, this](const SrcOp &Op) {
                         return Op.getLLTTy(*getMRI()) ==
                                SrcOps[0].getLLTTy(*getMRI());
                       }) &&
           "type mismatch in input list");
    assert(SrcOps.size() * SrcOps[0].getLLTTy(*getMRI()).getSizeInBits() ==
               DstOps[0].getLLTTy(*getMRI()).getSizeInBits() &&
           "input operands do not cover output register");
    if (SrcOps.size() == 1)
      return buildCast(DstOps[0], SrcOps[0]);
    if (DstOps[0].getLLTTy(*getMRI()).isVector())
      return buildInstr(TargetOpcode::G_CONCAT_VECTORS, DstOps, SrcOps);
    break;
  }
  case TargetOpcode::G_EXTRACT_VECTOR_ELT: {
    assert(DstOps.size() == 1 && "Invalid Dst size");
    assert(SrcOps.size() == 2 && "Invalid Src size");
    assert(SrcOps[0].getLLTTy(*getMRI()).isVector() && "Invalid operand type");
    assert((DstOps[0].getLLTTy(*getMRI()).isScalar() ||
            DstOps[0].getLLTTy(*getMRI()).isPointer()) &&
           "Invalid operand type");
    assert(SrcOps[1].getLLTTy(*getMRI()).isScalar() && "Invalid operand type");
    assert(SrcOps[0].getLLTTy(*getMRI()).getElementType() ==
               DstOps[0].getLLTTy(*getMRI()) &&
           "Type mismatch");
    break;
  }
  case TargetOpcode::G_INSERT_VECTOR_ELT: {
    assert(DstOps.size() == 1 && "Invalid dst size");
    assert(SrcOps.size() == 3 && "Invalid src size");
    assert(DstOps[0].getLLTTy(*getMRI()).isVector() &&
           SrcOps[0].getLLTTy(*getMRI()).isVector() && "Invalid operand type");
    assert(DstOps[0].getLLTTy(*getMRI()).getElementType() ==
               SrcOps[1].getLLTTy(*getMRI()) &&
           "Type mismatch");
    assert(SrcOps[2].getLLTTy(*getMRI()).isScalar() && "Invalid index");
    assert(DstOps[0].getLLTTy(*getMRI()).getNumElements() ==
               SrcOps[0].getLLTTy(*getMRI()).getNumElements() &&
           "Type mismatch");
    break;
  }
  case TargetOpcode::G_BUILD_VECTOR: {
    assert((!SrcOps.empty() || SrcOps.size() < 2) &&
           "Must have at least 2 operands");
    assert(DstOps.size() == 1 && "Invalid DstOps");
    assert(DstOps[0].getLLTTy(*getMRI()).isVector() &&
           "Res type must be a vector");
    assert(std::all_of(SrcOps.begin(), SrcOps.end(),
                       [&, this](const SrcOp &Op) {
                         return Op.getLLTTy(*getMRI()) ==
                                SrcOps[0].getLLTTy(*getMRI());
                       }) &&
           "type mismatch in input list");
    assert(SrcOps.size() * SrcOps[0].getLLTTy(*getMRI()).getSizeInBits() ==
               DstOps[0].getLLTTy(*getMRI()).getSizeInBits() &&
           "input scalars do not exactly cover the outpur vector register");
    break;
  }
  case TargetOpcode::G_BUILD_VECTOR_TRUNC: {
    assert((!SrcOps.empty() || SrcOps.size() < 2) &&
           "Must have at least 2 operands");
    assert(DstOps.size() == 1 && "Invalid DstOps");
    assert(DstOps[0].getLLTTy(*getMRI()).isVector() &&
           "Res type must be a vector");
    assert(std::all_of(SrcOps.begin(), SrcOps.end(),
                       [&, this](const SrcOp &Op) {
                         return Op.getLLTTy(*getMRI()) ==
                                SrcOps[0].getLLTTy(*getMRI());
                       }) &&
           "type mismatch in input list");
    if (SrcOps[0].getLLTTy(*getMRI()).getSizeInBits() ==
        DstOps[0].getLLTTy(*getMRI()).getElementType().getSizeInBits())
      return buildInstr(TargetOpcode::G_BUILD_VECTOR, DstOps, SrcOps);
    break;
  }
  case TargetOpcode::G_CONCAT_VECTORS: {
    assert(DstOps.size() == 1 && "Invalid DstOps");
    assert((!SrcOps.empty() || SrcOps.size() < 2) &&
           "Must have at least 2 operands");
    assert(std::all_of(SrcOps.begin(), SrcOps.end(),
                       [&, this](const SrcOp &Op) {
                         return (Op.getLLTTy(*getMRI()).isVector() &&
                                 Op.getLLTTy(*getMRI()) ==
                                     SrcOps[0].getLLTTy(*getMRI()));
                       }) &&
           "type mismatch in input list");
    assert(SrcOps.size() * SrcOps[0].getLLTTy(*getMRI()).getSizeInBits() ==
               DstOps[0].getLLTTy(*getMRI()).getSizeInBits() &&
           "input vectors do not exactly cover the outpur vector register");
    break;
  }
  case TargetOpcode::G_UADDE: {
    assert(DstOps.size() == 2 && "Invalid no of dst operands");
    assert(SrcOps.size() == 3 && "Invalid no of src operands");
    assert(DstOps[0].getLLTTy(*getMRI()).isScalar() && "Invalid operand");
    assert((DstOps[0].getLLTTy(*getMRI()) == SrcOps[0].getLLTTy(*getMRI())) &&
           (DstOps[0].getLLTTy(*getMRI()) == SrcOps[1].getLLTTy(*getMRI())) &&
           "Invalid operand");
    assert(DstOps[1].getLLTTy(*getMRI()).isScalar() && "Invalid operand");
    assert(DstOps[1].getLLTTy(*getMRI()) == SrcOps[2].getLLTTy(*getMRI()) &&
           "type mismatch");
    break;
  }
  }

  auto MIB = buildInstr(Opc);
  for (const DstOp &Op : DstOps)
    Op.addDefToMIB(*getMRI(), MIB);
  for (const SrcOp &Op : SrcOps)
    Op.addSrcToMIB(MIB);
  if (Flags)
    MIB->setFlags(*Flags);
  return MIB;
}
