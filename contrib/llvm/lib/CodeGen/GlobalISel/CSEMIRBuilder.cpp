//===-- llvm/CodeGen/GlobalISel/CSEMIRBuilder.cpp - MIBuilder--*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the CSEMIRBuilder class which CSEs as it builds
/// instructions.
//===----------------------------------------------------------------------===//
//

#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"

using namespace llvm;

bool CSEMIRBuilder::dominates(MachineBasicBlock::const_iterator A,
                              MachineBasicBlock::const_iterator B) const {
  auto MBBEnd = getMBB().end();
  if (B == MBBEnd)
    return true;
  assert(A->getParent() == B->getParent() &&
         "Iterators should be in same block");
  const MachineBasicBlock *BBA = A->getParent();
  MachineBasicBlock::const_iterator I = BBA->begin();
  for (; &*I != A && &*I != B; ++I)
    ;
  return &*I == A;
}

MachineInstrBuilder
CSEMIRBuilder::getDominatingInstrForID(FoldingSetNodeID &ID,
                                       void *&NodeInsertPos) {
  GISelCSEInfo *CSEInfo = getCSEInfo();
  assert(CSEInfo && "Can't get here without setting CSEInfo");
  MachineBasicBlock *CurMBB = &getMBB();
  MachineInstr *MI =
      CSEInfo->getMachineInstrIfExists(ID, CurMBB, NodeInsertPos);
  if (MI) {
    auto CurrPos = getInsertPt();
    if (!dominates(MI, CurrPos))
      CurMBB->splice(CurrPos, CurMBB, MI);
    return MachineInstrBuilder(getMF(), MI);
  }
  return MachineInstrBuilder();
}

bool CSEMIRBuilder::canPerformCSEForOpc(unsigned Opc) const {
  const GISelCSEInfo *CSEInfo = getCSEInfo();
  if (!CSEInfo || !CSEInfo->shouldCSE(Opc))
    return false;
  return true;
}

void CSEMIRBuilder::profileDstOp(const DstOp &Op,
                                 GISelInstProfileBuilder &B) const {
  switch (Op.getDstOpKind()) {
  case DstOp::DstType::Ty_RC:
    B.addNodeIDRegType(Op.getRegClass());
    break;
  default:
    B.addNodeIDRegType(Op.getLLTTy(*getMRI()));
    break;
  }
}

void CSEMIRBuilder::profileSrcOp(const SrcOp &Op,
                                 GISelInstProfileBuilder &B) const {
  switch (Op.getSrcOpKind()) {
  case SrcOp::SrcType::Ty_Predicate:
    B.addNodeIDImmediate(static_cast<int64_t>(Op.getPredicate()));
    break;
  default:
    B.addNodeIDRegType(Op.getReg());
    break;
  }
}

void CSEMIRBuilder::profileMBBOpcode(GISelInstProfileBuilder &B,
                                     unsigned Opc) const {
  // First add the MBB (Local CSE).
  B.addNodeIDMBB(&getMBB());
  // Then add the opcode.
  B.addNodeIDOpcode(Opc);
}

void CSEMIRBuilder::profileEverything(unsigned Opc, ArrayRef<DstOp> DstOps,
                                      ArrayRef<SrcOp> SrcOps,
                                      Optional<unsigned> Flags,
                                      GISelInstProfileBuilder &B) const {

  profileMBBOpcode(B, Opc);
  // Then add the DstOps.
  profileDstOps(DstOps, B);
  // Then add the SrcOps.
  profileSrcOps(SrcOps, B);
  // Add Flags if passed in.
  if (Flags)
    B.addNodeIDFlag(*Flags);
}

MachineInstrBuilder CSEMIRBuilder::memoizeMI(MachineInstrBuilder MIB,
                                             void *NodeInsertPos) {
  assert(canPerformCSEForOpc(MIB->getOpcode()) &&
         "Attempting to CSE illegal op");
  MachineInstr *MIBInstr = MIB;
  getCSEInfo()->insertInstr(MIBInstr, NodeInsertPos);
  return MIB;
}

bool CSEMIRBuilder::checkCopyToDefsPossible(ArrayRef<DstOp> DstOps) {
  if (DstOps.size() == 1)
    return true; // always possible to emit copy to just 1 vreg.

  return std::all_of(DstOps.begin(), DstOps.end(), [](const DstOp &Op) {
    DstOp::DstType DT = Op.getDstOpKind();
    return DT == DstOp::DstType::Ty_LLT || DT == DstOp::DstType::Ty_RC;
  });
}

MachineInstrBuilder
CSEMIRBuilder::generateCopiesIfRequired(ArrayRef<DstOp> DstOps,
                                        MachineInstrBuilder &MIB) {
  assert(checkCopyToDefsPossible(DstOps) &&
         "Impossible return a single MIB with copies to multiple defs");
  if (DstOps.size() == 1) {
    const DstOp &Op = DstOps[0];
    if (Op.getDstOpKind() == DstOp::DstType::Ty_Reg)
      return buildCopy(Op.getReg(), MIB->getOperand(0).getReg());
  }
  return MIB;
}

MachineInstrBuilder CSEMIRBuilder::buildInstr(unsigned Opc,
                                              ArrayRef<DstOp> DstOps,
                                              ArrayRef<SrcOp> SrcOps,
                                              Optional<unsigned> Flag) {
  switch (Opc) {
  default:
    break;
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
    // Try to constant fold these.
    assert(SrcOps.size() == 2 && "Invalid sources");
    assert(DstOps.size() == 1 && "Invalid dsts");
    if (Optional<APInt> Cst = ConstantFoldBinOp(Opc, SrcOps[0].getReg(),
                                                SrcOps[1].getReg(), *getMRI()))
      return buildConstant(DstOps[0], Cst->getSExtValue());
    break;
  }
  }
  bool CanCopy = checkCopyToDefsPossible(DstOps);
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
  // If we can CSE this instruction, but involves generating copies to multiple
  // regs, give up. This frequently happens to UNMERGEs.
  if (!CanCopy) {
    auto MIB = MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
    // CSEInfo would have tracked this instruction. Remove it from the temporary
    // insts.
    getCSEInfo()->handleRemoveInst(&*MIB);
    return MIB;
  }
  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileEverything(Opc, DstOps, SrcOps, Flag, ProfBuilder);
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired(DstOps, MIB);
  }
  // This instruction does not exist in the CSEInfo. Build it and CSE it.
  MachineInstrBuilder NewMIB =
      MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
  return memoizeMI(NewMIB, InsertPos);
}

MachineInstrBuilder CSEMIRBuilder::buildConstant(const DstOp &Res,
                                                 const ConstantInt &Val) {
  constexpr unsigned Opc = TargetOpcode::G_CONSTANT;
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildConstant(Res, Val);
  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileMBBOpcode(ProfBuilder, Opc);
  profileDstOp(Res, ProfBuilder);
  ProfBuilder.addNodeIDMachineOperand(MachineOperand::CreateCImm(&Val));
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired({Res}, MIB);
  }
  MachineInstrBuilder NewMIB = MachineIRBuilder::buildConstant(Res, Val);
  return memoizeMI(NewMIB, InsertPos);
}

MachineInstrBuilder CSEMIRBuilder::buildFConstant(const DstOp &Res,
                                                  const ConstantFP &Val) {
  constexpr unsigned Opc = TargetOpcode::G_FCONSTANT;
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildFConstant(Res, Val);
  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileMBBOpcode(ProfBuilder, Opc);
  profileDstOp(Res, ProfBuilder);
  ProfBuilder.addNodeIDMachineOperand(MachineOperand::CreateFPImm(&Val));
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired({Res}, MIB);
  }
  MachineInstrBuilder NewMIB = MachineIRBuilder::buildFConstant(Res, Val);
  return memoizeMI(NewMIB, InsertPos);
}
