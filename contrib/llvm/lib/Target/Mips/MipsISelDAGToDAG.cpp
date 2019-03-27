//===-- MipsISelDAGToDAG.cpp - A Dag to Dag Inst Selector for Mips --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the MIPS target.
//
//===----------------------------------------------------------------------===//

#include "MipsISelDAGToDAG.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips.h"
#include "Mips16ISelDAGToDAG.h"
#include "MipsMachineFunction.h"
#include "MipsRegisterInfo.h"
#include "MipsSEISelDAGToDAG.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "mips-isel"

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MipsDAGToDAGISel - MIPS specific code to select MIPS machine
// instructions for SelectionDAG operations.
//===----------------------------------------------------------------------===//

void MipsDAGToDAGISel::getAnalysisUsage(AnalysisUsage &AU) const {
  // There are multiple MipsDAGToDAGISel instances added to the pass pipeline.
  // We need to preserve StackProtector for the next one.
  AU.addPreserved<StackProtector>();
  SelectionDAGISel::getAnalysisUsage(AU);
}

bool MipsDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &static_cast<const MipsSubtarget &>(MF.getSubtarget());
  bool Ret = SelectionDAGISel::runOnMachineFunction(MF);

  processFunctionAfterISel(MF);

  return Ret;
}

/// getGlobalBaseReg - Output the instructions required to put the
/// GOT address into a register.
SDNode *MipsDAGToDAGISel::getGlobalBaseReg() {
  unsigned GlobalBaseReg = MF->getInfo<MipsFunctionInfo>()->getGlobalBaseReg();
  return CurDAG->getRegister(GlobalBaseReg, getTargetLowering()->getPointerTy(
                                                CurDAG->getDataLayout()))
      .getNode();
}

/// ComplexPattern used on MipsInstrInfo
/// Used on Mips Load/Store instructions
bool MipsDAGToDAGISel::selectAddrRegImm(SDValue Addr, SDValue &Base,
                                        SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddrDefault(SDValue Addr, SDValue &Base,
                                         SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr(SDValue Addr, SDValue &Base,
                                     SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr11MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr12MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr16MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrLSL2MM(SDValue Addr, SDValue &Base,
                                           SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10(SDValue Addr, SDValue &Base,
                                           SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl1(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl2(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl3(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddr16(SDValue Addr, SDValue &Base,
                                    SDValue &Offset) {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddr16SP(SDValue Addr, SDValue &Base,
                                      SDValue &Offset) {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplat(SDNode *N, APInt &Imm,
                                    unsigned MinSizeInBits) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm1(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm3(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm4(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm5(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm6(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm8(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatSimm5(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimmPow2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimmInvPow2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatMaskL(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatMaskR(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

/// Select instructions not customized! Used for
/// expanded, promoted and normal instructions
void MipsDAGToDAGISel::Select(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();

  // If we have a custom node, we already have selected!
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  // See if subclasses can handle this node.
  if (trySelect(Node))
    return;

  switch(Opcode) {
  default: break;

  // Get target GOT address.
  case ISD::GLOBAL_OFFSET_TABLE:
    ReplaceNode(Node, getGlobalBaseReg());
    return;

#ifndef NDEBUG
  case ISD::LOAD:
  case ISD::STORE:
    assert((Subtarget->systemSupportsUnalignedAccess() ||
            cast<MemSDNode>(Node)->getMemoryVT().getSizeInBits() / 8 <=
            cast<MemSDNode>(Node)->getAlignment()) &&
           "Unexpected unaligned loads/stores.");
    break;
#endif
  }

  // Select the default instruction
  SelectCode(Node);
}

bool MipsDAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                             std::vector<SDValue> &OutOps) {
  // All memory constraints can at least accept raw pointers.
  switch(ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::Constraint_i:
  case InlineAsm::Constraint_m:
  case InlineAsm::Constraint_R:
  case InlineAsm::Constraint_ZC:
    OutOps.push_back(Op);
    return false;
  }
  return true;
}
