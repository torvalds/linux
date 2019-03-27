//===- SIInstrInfo.cpp - SI Instruction Information  ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// SI Implementation of TargetInstrInfo.
//
//===----------------------------------------------------------------------===//

#include "SIInstrInfo.h"
#include "AMDGPU.h"
#include "AMDGPUIntrinsicInfo.h"
#include "AMDGPUSubtarget.h"
#include "GCNHazardRecognizer.h"
#include "SIDefines.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "AMDGPUGenInstrInfo.inc"

namespace llvm {
namespace AMDGPU {
#define GET_D16ImageDimIntrinsics_IMPL
#define GET_ImageDimIntrinsicTable_IMPL
#define GET_RsrcIntrinsics_IMPL
#include "AMDGPUGenSearchableTables.inc"
}
}


// Must be at least 4 to be able to branch over minimum unconditional branch
// code. This is only for making it possible to write reasonably small tests for
// long branches.
static cl::opt<unsigned>
BranchOffsetBits("amdgpu-s-branch-bits", cl::ReallyHidden, cl::init(16),
                 cl::desc("Restrict range of branch instructions (DEBUG)"));

SIInstrInfo::SIInstrInfo(const GCNSubtarget &ST)
  : AMDGPUGenInstrInfo(AMDGPU::ADJCALLSTACKUP, AMDGPU::ADJCALLSTACKDOWN),
    RI(ST), ST(ST) {}

//===----------------------------------------------------------------------===//
// TargetInstrInfo callbacks
//===----------------------------------------------------------------------===//

static unsigned getNumOperandsNoGlue(SDNode *Node) {
  unsigned N = Node->getNumOperands();
  while (N && Node->getOperand(N - 1).getValueType() == MVT::Glue)
    --N;
  return N;
}

static SDValue findChainOperand(SDNode *Load) {
  SDValue LastOp = Load->getOperand(getNumOperandsNoGlue(Load) - 1);
  assert(LastOp.getValueType() == MVT::Other && "Chain missing from load node");
  return LastOp;
}

/// Returns true if both nodes have the same value for the given
///        operand \p Op, or if both nodes do not have this operand.
static bool nodesHaveSameOperandValue(SDNode *N0, SDNode* N1, unsigned OpName) {
  unsigned Opc0 = N0->getMachineOpcode();
  unsigned Opc1 = N1->getMachineOpcode();

  int Op0Idx = AMDGPU::getNamedOperandIdx(Opc0, OpName);
  int Op1Idx = AMDGPU::getNamedOperandIdx(Opc1, OpName);

  if (Op0Idx == -1 && Op1Idx == -1)
    return true;


  if ((Op0Idx == -1 && Op1Idx != -1) ||
      (Op1Idx == -1 && Op0Idx != -1))
    return false;

  // getNamedOperandIdx returns the index for the MachineInstr's operands,
  // which includes the result as the first operand. We are indexing into the
  // MachineSDNode's operands, so we need to skip the result operand to get
  // the real index.
  --Op0Idx;
  --Op1Idx;

  return N0->getOperand(Op0Idx) == N1->getOperand(Op1Idx);
}

bool SIInstrInfo::isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                                    AliasAnalysis *AA) const {
  // TODO: The generic check fails for VALU instructions that should be
  // rematerializable due to implicit reads of exec. We really want all of the
  // generic logic for this except for this.
  switch (MI.getOpcode()) {
  case AMDGPU::V_MOV_B32_e32:
  case AMDGPU::V_MOV_B32_e64:
  case AMDGPU::V_MOV_B64_PSEUDO:
    return true;
  default:
    return false;
  }
}

bool SIInstrInfo::areLoadsFromSameBasePtr(SDNode *Load0, SDNode *Load1,
                                          int64_t &Offset0,
                                          int64_t &Offset1) const {
  if (!Load0->isMachineOpcode() || !Load1->isMachineOpcode())
    return false;

  unsigned Opc0 = Load0->getMachineOpcode();
  unsigned Opc1 = Load1->getMachineOpcode();

  // Make sure both are actually loads.
  if (!get(Opc0).mayLoad() || !get(Opc1).mayLoad())
    return false;

  if (isDS(Opc0) && isDS(Opc1)) {

    // FIXME: Handle this case:
    if (getNumOperandsNoGlue(Load0) != getNumOperandsNoGlue(Load1))
      return false;

    // Check base reg.
    if (Load0->getOperand(1) != Load1->getOperand(1))
      return false;

    // Check chain.
    if (findChainOperand(Load0) != findChainOperand(Load1))
      return false;

    // Skip read2 / write2 variants for simplicity.
    // TODO: We should report true if the used offsets are adjacent (excluded
    // st64 versions).
    if (AMDGPU::getNamedOperandIdx(Opc0, AMDGPU::OpName::data1) != -1 ||
        AMDGPU::getNamedOperandIdx(Opc1, AMDGPU::OpName::data1) != -1)
      return false;

    Offset0 = cast<ConstantSDNode>(Load0->getOperand(2))->getZExtValue();
    Offset1 = cast<ConstantSDNode>(Load1->getOperand(2))->getZExtValue();
    return true;
  }

  if (isSMRD(Opc0) && isSMRD(Opc1)) {
    // Skip time and cache invalidation instructions.
    if (AMDGPU::getNamedOperandIdx(Opc0, AMDGPU::OpName::sbase) == -1 ||
        AMDGPU::getNamedOperandIdx(Opc1, AMDGPU::OpName::sbase) == -1)
      return false;

    assert(getNumOperandsNoGlue(Load0) == getNumOperandsNoGlue(Load1));

    // Check base reg.
    if (Load0->getOperand(0) != Load1->getOperand(0))
      return false;

    const ConstantSDNode *Load0Offset =
        dyn_cast<ConstantSDNode>(Load0->getOperand(1));
    const ConstantSDNode *Load1Offset =
        dyn_cast<ConstantSDNode>(Load1->getOperand(1));

    if (!Load0Offset || !Load1Offset)
      return false;

    // Check chain.
    if (findChainOperand(Load0) != findChainOperand(Load1))
      return false;

    Offset0 = Load0Offset->getZExtValue();
    Offset1 = Load1Offset->getZExtValue();
    return true;
  }

  // MUBUF and MTBUF can access the same addresses.
  if ((isMUBUF(Opc0) || isMTBUF(Opc0)) && (isMUBUF(Opc1) || isMTBUF(Opc1))) {

    // MUBUF and MTBUF have vaddr at different indices.
    if (!nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::soffset) ||
        findChainOperand(Load0) != findChainOperand(Load1) ||
        !nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::vaddr) ||
        !nodesHaveSameOperandValue(Load0, Load1, AMDGPU::OpName::srsrc))
      return false;

    int OffIdx0 = AMDGPU::getNamedOperandIdx(Opc0, AMDGPU::OpName::offset);
    int OffIdx1 = AMDGPU::getNamedOperandIdx(Opc1, AMDGPU::OpName::offset);

    if (OffIdx0 == -1 || OffIdx1 == -1)
      return false;

    // getNamedOperandIdx returns the index for MachineInstrs.  Since they
    // inlcude the output in the operand list, but SDNodes don't, we need to
    // subtract the index by one.
    --OffIdx0;
    --OffIdx1;

    SDValue Off0 = Load0->getOperand(OffIdx0);
    SDValue Off1 = Load1->getOperand(OffIdx1);

    // The offset might be a FrameIndexSDNode.
    if (!isa<ConstantSDNode>(Off0) || !isa<ConstantSDNode>(Off1))
      return false;

    Offset0 = cast<ConstantSDNode>(Off0)->getZExtValue();
    Offset1 = cast<ConstantSDNode>(Off1)->getZExtValue();
    return true;
  }

  return false;
}

static bool isStride64(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::DS_READ2ST64_B32:
  case AMDGPU::DS_READ2ST64_B64:
  case AMDGPU::DS_WRITE2ST64_B32:
  case AMDGPU::DS_WRITE2ST64_B64:
    return true;
  default:
    return false;
  }
}

bool SIInstrInfo::getMemOperandWithOffset(MachineInstr &LdSt,
                                          MachineOperand *&BaseOp,
                                          int64_t &Offset,
                                          const TargetRegisterInfo *TRI) const {
  unsigned Opc = LdSt.getOpcode();

  if (isDS(LdSt)) {
    const MachineOperand *OffsetImm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset);
    if (OffsetImm) {
      // Normal, single offset LDS instruction.
      BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::addr);
      Offset = OffsetImm->getImm();
      assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                                "operands of type register.");
      return true;
    }

    // The 2 offset instructions use offset0 and offset1 instead. We can treat
    // these as a load with a single offset if the 2 offsets are consecutive. We
    // will use this for some partially aligned loads.
    const MachineOperand *Offset0Imm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset0);
    const MachineOperand *Offset1Imm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset1);

    uint8_t Offset0 = Offset0Imm->getImm();
    uint8_t Offset1 = Offset1Imm->getImm();

    if (Offset1 > Offset0 && Offset1 - Offset0 == 1) {
      // Each of these offsets is in element sized units, so we need to convert
      // to bytes of the individual reads.

      unsigned EltSize;
      if (LdSt.mayLoad())
        EltSize = TRI->getRegSizeInBits(*getOpRegClass(LdSt, 0)) / 16;
      else {
        assert(LdSt.mayStore());
        int Data0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::data0);
        EltSize = TRI->getRegSizeInBits(*getOpRegClass(LdSt, Data0Idx)) / 8;
      }

      if (isStride64(Opc))
        EltSize *= 64;

      BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::addr);
      Offset = EltSize * Offset0;
      assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                                "operands of type register.");
      return true;
    }

    return false;
  }

  if (isMUBUF(LdSt) || isMTBUF(LdSt)) {
    const MachineOperand *SOffset = getNamedOperand(LdSt, AMDGPU::OpName::soffset);
    if (SOffset && SOffset->isReg())
      return false;

    MachineOperand *AddrReg = getNamedOperand(LdSt, AMDGPU::OpName::vaddr);
    if (!AddrReg)
      return false;

    const MachineOperand *OffsetImm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset);
    BaseOp = AddrReg;
    Offset = OffsetImm->getImm();

    if (SOffset) // soffset can be an inline immediate.
      Offset += SOffset->getImm();

    assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                              "operands of type register.");
    return true;
  }

  if (isSMRD(LdSt)) {
    const MachineOperand *OffsetImm =
        getNamedOperand(LdSt, AMDGPU::OpName::offset);
    if (!OffsetImm)
      return false;

    MachineOperand *SBaseReg = getNamedOperand(LdSt, AMDGPU::OpName::sbase);
    BaseOp = SBaseReg;
    Offset = OffsetImm->getImm();
    assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                              "operands of type register.");
    return true;
  }

  if (isFLAT(LdSt)) {
    MachineOperand *VAddr = getNamedOperand(LdSt, AMDGPU::OpName::vaddr);
    if (VAddr) {
      // Can't analyze 2 offsets.
      if (getNamedOperand(LdSt, AMDGPU::OpName::saddr))
        return false;

      BaseOp = VAddr;
    } else {
      // scratch instructions have either vaddr or saddr.
      BaseOp = getNamedOperand(LdSt, AMDGPU::OpName::saddr);
    }

    Offset = getNamedOperand(LdSt, AMDGPU::OpName::offset)->getImm();
    assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                              "operands of type register.");
    return true;
  }

  return false;
}

static bool memOpsHaveSameBasePtr(const MachineInstr &MI1,
                                  const MachineOperand &BaseOp1,
                                  const MachineInstr &MI2,
                                  const MachineOperand &BaseOp2) {
  // Support only base operands with base registers.
  // Note: this could be extended to support FI operands.
  if (!BaseOp1.isReg() || !BaseOp2.isReg())
    return false;

  if (BaseOp1.isIdenticalTo(BaseOp2))
    return true;

  if (!MI1.hasOneMemOperand() || !MI2.hasOneMemOperand())
    return false;

  auto MO1 = *MI1.memoperands_begin();
  auto MO2 = *MI2.memoperands_begin();
  if (MO1->getAddrSpace() != MO2->getAddrSpace())
    return false;

  auto Base1 = MO1->getValue();
  auto Base2 = MO2->getValue();
  if (!Base1 || !Base2)
    return false;
  const MachineFunction &MF = *MI1.getParent()->getParent();
  const DataLayout &DL = MF.getFunction().getParent()->getDataLayout();
  Base1 = GetUnderlyingObject(Base1, DL);
  Base2 = GetUnderlyingObject(Base1, DL);

  if (isa<UndefValue>(Base1) || isa<UndefValue>(Base2))
    return false;

  return Base1 == Base2;
}

bool SIInstrInfo::shouldClusterMemOps(MachineOperand &BaseOp1,
                                      MachineOperand &BaseOp2,
                                      unsigned NumLoads) const {
  MachineInstr &FirstLdSt = *BaseOp1.getParent();
  MachineInstr &SecondLdSt = *BaseOp2.getParent();

  if (!memOpsHaveSameBasePtr(FirstLdSt, BaseOp1, SecondLdSt, BaseOp2))
    return false;

  const MachineOperand *FirstDst = nullptr;
  const MachineOperand *SecondDst = nullptr;

  if ((isMUBUF(FirstLdSt) && isMUBUF(SecondLdSt)) ||
      (isMTBUF(FirstLdSt) && isMTBUF(SecondLdSt)) ||
      (isFLAT(FirstLdSt) && isFLAT(SecondLdSt))) {
    const unsigned MaxGlobalLoadCluster = 6;
    if (NumLoads > MaxGlobalLoadCluster)
      return false;

    FirstDst = getNamedOperand(FirstLdSt, AMDGPU::OpName::vdata);
    if (!FirstDst)
      FirstDst = getNamedOperand(FirstLdSt, AMDGPU::OpName::vdst);
    SecondDst = getNamedOperand(SecondLdSt, AMDGPU::OpName::vdata);
    if (!SecondDst)
      SecondDst = getNamedOperand(SecondLdSt, AMDGPU::OpName::vdst);
  } else if (isSMRD(FirstLdSt) && isSMRD(SecondLdSt)) {
    FirstDst = getNamedOperand(FirstLdSt, AMDGPU::OpName::sdst);
    SecondDst = getNamedOperand(SecondLdSt, AMDGPU::OpName::sdst);
  } else if (isDS(FirstLdSt) && isDS(SecondLdSt)) {
    FirstDst = getNamedOperand(FirstLdSt, AMDGPU::OpName::vdst);
    SecondDst = getNamedOperand(SecondLdSt, AMDGPU::OpName::vdst);
  }

  if (!FirstDst || !SecondDst)
    return false;

  // Try to limit clustering based on the total number of bytes loaded
  // rather than the number of instructions.  This is done to help reduce
  // register pressure.  The method used is somewhat inexact, though,
  // because it assumes that all loads in the cluster will load the
  // same number of bytes as FirstLdSt.

  // The unit of this value is bytes.
  // FIXME: This needs finer tuning.
  unsigned LoadClusterThreshold = 16;

  const MachineRegisterInfo &MRI =
      FirstLdSt.getParent()->getParent()->getRegInfo();
  const TargetRegisterClass *DstRC = MRI.getRegClass(FirstDst->getReg());

  return (NumLoads * (RI.getRegSizeInBits(*DstRC) / 8)) <= LoadClusterThreshold;
}

// FIXME: This behaves strangely. If, for example, you have 32 load + stores,
// the first 16 loads will be interleaved with the stores, and the next 16 will
// be clustered as expected. It should really split into 2 16 store batches.
//
// Loads are clustered until this returns false, rather than trying to schedule
// groups of stores. This also means we have to deal with saying different
// address space loads should be clustered, and ones which might cause bank
// conflicts.
//
// This might be deprecated so it might not be worth that much effort to fix.
bool SIInstrInfo::shouldScheduleLoadsNear(SDNode *Load0, SDNode *Load1,
                                          int64_t Offset0, int64_t Offset1,
                                          unsigned NumLoads) const {
  assert(Offset1 > Offset0 &&
         "Second offset should be larger than first offset!");
  // If we have less than 16 loads in a row, and the offsets are within 64
  // bytes, then schedule together.

  // A cacheline is 64 bytes (for global memory).
  return (NumLoads <= 16 && (Offset1 - Offset0) < 64);
}

static void reportIllegalCopy(const SIInstrInfo *TII, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              const DebugLoc &DL, unsigned DestReg,
                              unsigned SrcReg, bool KillSrc) {
  MachineFunction *MF = MBB.getParent();
  DiagnosticInfoUnsupported IllegalCopy(MF->getFunction(),
                                        "illegal SGPR to VGPR copy",
                                        DL, DS_Error);
  LLVMContext &C = MF->getFunction().getContext();
  C.diagnose(IllegalCopy);

  BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_ILLEGAL_COPY), DestReg)
    .addReg(SrcReg, getKillRegState(KillSrc));
}

void SIInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              const DebugLoc &DL, unsigned DestReg,
                              unsigned SrcReg, bool KillSrc) const {
  const TargetRegisterClass *RC = RI.getPhysRegClass(DestReg);

  if (RC == &AMDGPU::VGPR_32RegClass) {
    assert(AMDGPU::VGPR_32RegClass.contains(SrcReg) ||
           AMDGPU::SReg_32RegClass.contains(SrcReg));
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (RC == &AMDGPU::SReg_32_XM0RegClass ||
      RC == &AMDGPU::SReg_32RegClass) {
    if (SrcReg == AMDGPU::SCC) {
      BuildMI(MBB, MI, DL, get(AMDGPU::S_CSELECT_B32), DestReg)
          .addImm(-1)
          .addImm(0);
      return;
    }

    if (!AMDGPU::SReg_32RegClass.contains(SrcReg)) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }

    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DestReg)
            .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (RC == &AMDGPU::SReg_64RegClass) {
    if (DestReg == AMDGPU::VCC) {
      if (AMDGPU::SReg_64RegClass.contains(SrcReg)) {
        BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), AMDGPU::VCC)
          .addReg(SrcReg, getKillRegState(KillSrc));
      } else {
        // FIXME: Hack until VReg_1 removed.
        assert(AMDGPU::VGPR_32RegClass.contains(SrcReg));
        BuildMI(MBB, MI, DL, get(AMDGPU::V_CMP_NE_U32_e32))
          .addImm(0)
          .addReg(SrcReg, getKillRegState(KillSrc));
      }

      return;
    }

    if (!AMDGPU::SReg_64RegClass.contains(SrcReg)) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }

    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), DestReg)
            .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (DestReg == AMDGPU::SCC) {
    assert(AMDGPU::SReg_32RegClass.contains(SrcReg));
    BuildMI(MBB, MI, DL, get(AMDGPU::S_CMP_LG_U32))
      .addReg(SrcReg, getKillRegState(KillSrc))
      .addImm(0);
    return;
  }

  unsigned EltSize = 4;
  unsigned Opcode = AMDGPU::V_MOV_B32_e32;
  if (RI.isSGPRClass(RC)) {
    if (RI.getRegSizeInBits(*RC) > 32) {
      Opcode =  AMDGPU::S_MOV_B64;
      EltSize = 8;
    } else {
      Opcode = AMDGPU::S_MOV_B32;
      EltSize = 4;
    }

    if (!RI.isSGPRClass(RI.getPhysRegClass(SrcReg))) {
      reportIllegalCopy(this, MBB, MI, DL, DestReg, SrcReg, KillSrc);
      return;
    }
  }

  ArrayRef<int16_t> SubIndices = RI.getRegSplitParts(RC, EltSize);
  bool Forward = RI.getHWRegIndex(DestReg) <= RI.getHWRegIndex(SrcReg);

  for (unsigned Idx = 0; Idx < SubIndices.size(); ++Idx) {
    unsigned SubIdx;
    if (Forward)
      SubIdx = SubIndices[Idx];
    else
      SubIdx = SubIndices[SubIndices.size() - Idx - 1];

    MachineInstrBuilder Builder = BuildMI(MBB, MI, DL,
      get(Opcode), RI.getSubReg(DestReg, SubIdx));

    Builder.addReg(RI.getSubReg(SrcReg, SubIdx));

    if (Idx == 0)
      Builder.addReg(DestReg, RegState::Define | RegState::Implicit);

    bool UseKill = KillSrc && Idx == SubIndices.size() - 1;
    Builder.addReg(SrcReg, getKillRegState(UseKill) | RegState::Implicit);
  }
}

int SIInstrInfo::commuteOpcode(unsigned Opcode) const {
  int NewOpc;

  // Try to map original to commuted opcode
  NewOpc = AMDGPU::getCommuteRev(Opcode);
  if (NewOpc != -1)
    // Check if the commuted (REV) opcode exists on the target.
    return pseudoToMCOpcode(NewOpc) != -1 ? NewOpc : -1;

  // Try to map commuted to original opcode
  NewOpc = AMDGPU::getCommuteOrig(Opcode);
  if (NewOpc != -1)
    // Check if the original (non-REV) opcode exists on the target.
    return pseudoToMCOpcode(NewOpc) != -1 ? NewOpc : -1;

  return Opcode;
}

void SIInstrInfo::materializeImmediate(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       const DebugLoc &DL, unsigned DestReg,
                                       int64_t Value) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RegClass = MRI.getRegClass(DestReg);
  if (RegClass == &AMDGPU::SReg_32RegClass ||
      RegClass == &AMDGPU::SGPR_32RegClass ||
      RegClass == &AMDGPU::SReg_32_XM0RegClass ||
      RegClass == &AMDGPU::SReg_32_XM0_XEXECRegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B32), DestReg)
      .addImm(Value);
    return;
  }

  if (RegClass == &AMDGPU::SReg_64RegClass ||
      RegClass == &AMDGPU::SGPR_64RegClass ||
      RegClass == &AMDGPU::SReg_64_XEXECRegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_MOV_B64), DestReg)
      .addImm(Value);
    return;
  }

  if (RegClass == &AMDGPU::VGPR_32RegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DestReg)
      .addImm(Value);
    return;
  }
  if (RegClass == &AMDGPU::VReg_64RegClass) {
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_PSEUDO), DestReg)
      .addImm(Value);
    return;
  }

  unsigned EltSize = 4;
  unsigned Opcode = AMDGPU::V_MOV_B32_e32;
  if (RI.isSGPRClass(RegClass)) {
    if (RI.getRegSizeInBits(*RegClass) > 32) {
      Opcode =  AMDGPU::S_MOV_B64;
      EltSize = 8;
    } else {
      Opcode = AMDGPU::S_MOV_B32;
      EltSize = 4;
    }
  }

  ArrayRef<int16_t> SubIndices = RI.getRegSplitParts(RegClass, EltSize);
  for (unsigned Idx = 0; Idx < SubIndices.size(); ++Idx) {
    int64_t IdxValue = Idx == 0 ? Value : 0;

    MachineInstrBuilder Builder = BuildMI(MBB, MI, DL,
      get(Opcode), RI.getSubReg(DestReg, Idx));
    Builder.addImm(IdxValue);
  }
}

const TargetRegisterClass *
SIInstrInfo::getPreferredSelectRegClass(unsigned Size) const {
  return &AMDGPU::VGPR_32RegClass;
}

void SIInstrInfo::insertVectorSelect(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     const DebugLoc &DL, unsigned DstReg,
                                     ArrayRef<MachineOperand> Cond,
                                     unsigned TrueReg,
                                     unsigned FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  assert(MRI.getRegClass(DstReg) == &AMDGPU::VGPR_32RegClass &&
         "Not a VGPR32 reg");

  if (Cond.size() == 1) {
    unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
    BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
      .add(Cond[0]);
    BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
      .addReg(FalseReg)
      .addReg(TrueReg)
      .addReg(SReg);
  } else if (Cond.size() == 2) {
    assert(Cond[0].isImm() && "Cond[0] is not an immediate");
    switch (Cond[0].getImm()) {
    case SIInstrInfo::SCC_TRUE: {
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), SReg)
        .addImm(-1)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addReg(FalseReg)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::SCC_FALSE: {
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), SReg)
        .addImm(0)
        .addImm(-1);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addReg(FalseReg)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::VCCNZ: {
      MachineOperand RegOp = Cond[1];
      RegOp.setImplicit(false);
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
        .add(RegOp);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addReg(FalseReg)
          .addReg(TrueReg)
          .addReg(SReg);
      break;
    }
    case SIInstrInfo::VCCZ: {
      MachineOperand RegOp = Cond[1];
      RegOp.setImplicit(false);
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::COPY), SReg)
        .add(RegOp);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addReg(TrueReg)
          .addReg(FalseReg)
          .addReg(SReg);
      break;
    }
    case SIInstrInfo::EXECNZ: {
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      unsigned SReg2 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::S_OR_SAVEEXEC_B64), SReg2)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), SReg)
        .addImm(-1)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addReg(FalseReg)
        .addReg(TrueReg)
        .addReg(SReg);
      break;
    }
    case SIInstrInfo::EXECZ: {
      unsigned SReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
      unsigned SReg2 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
      BuildMI(MBB, I, DL, get(AMDGPU::S_OR_SAVEEXEC_B64), SReg2)
        .addImm(0);
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), SReg)
        .addImm(0)
        .addImm(-1);
      BuildMI(MBB, I, DL, get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
        .addReg(FalseReg)
        .addReg(TrueReg)
        .addReg(SReg);
      llvm_unreachable("Unhandled branch predicate EXECZ");
      break;
    }
    default:
      llvm_unreachable("invalid branch predicate");
    }
  } else {
    llvm_unreachable("Can only handle Cond size 1 or 2");
  }
}

unsigned SIInstrInfo::insertEQ(MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL,
                               unsigned SrcReg, int Value) const {
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  unsigned Reg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  BuildMI(*MBB, I, DL, get(AMDGPU::V_CMP_EQ_I32_e64), Reg)
    .addImm(Value)
    .addReg(SrcReg);

  return Reg;
}

unsigned SIInstrInfo::insertNE(MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL,
                               unsigned SrcReg, int Value) const {
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  unsigned Reg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  BuildMI(*MBB, I, DL, get(AMDGPU::V_CMP_NE_I32_e64), Reg)
    .addImm(Value)
    .addReg(SrcReg);

  return Reg;
}

unsigned SIInstrInfo::getMovOpcode(const TargetRegisterClass *DstRC) const {

  if (RI.getRegSizeInBits(*DstRC) == 32) {
    return RI.isSGPRClass(DstRC) ? AMDGPU::S_MOV_B32 : AMDGPU::V_MOV_B32_e32;
  } else if (RI.getRegSizeInBits(*DstRC) == 64 && RI.isSGPRClass(DstRC)) {
    return AMDGPU::S_MOV_B64;
  } else if (RI.getRegSizeInBits(*DstRC) == 64 && !RI.isSGPRClass(DstRC)) {
    return  AMDGPU::V_MOV_B64_PSEUDO;
  }
  return AMDGPU::COPY;
}

static unsigned getSGPRSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_S32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_S64_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_S128_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_S256_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_S512_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getVGPRSpillSaveOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_V32_SAVE;
  case 8:
    return AMDGPU::SI_SPILL_V64_SAVE;
  case 12:
    return AMDGPU::SI_SPILL_V96_SAVE;
  case 16:
    return AMDGPU::SI_SPILL_V128_SAVE;
  case 32:
    return AMDGPU::SI_SPILL_V256_SAVE;
  case 64:
    return AMDGPU::SI_SPILL_V512_SAVE;
  default:
    llvm_unreachable("unknown register size");
  }
}

void SIInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MI,
                                      unsigned SrcReg, bool isKill,
                                      int FrameIndex,
                                      const TargetRegisterClass *RC,
                                      const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const DebugLoc &DL = MBB.findDebugLoc(MI);

  unsigned Size = FrameInfo.getObjectSize(FrameIndex);
  unsigned Align = FrameInfo.getObjectAlignment(FrameIndex);
  MachinePointerInfo PtrInfo
    = MachinePointerInfo::getFixedStack(*MF, FrameIndex);
  MachineMemOperand *MMO
    = MF->getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                               Size, Align);
  unsigned SpillSize = TRI->getSpillSize(*RC);

  if (RI.isSGPRClass(RC)) {
    MFI->setHasSpilledSGPRs();

    // We are only allowed to create one new instruction when spilling
    // registers, so we need to use pseudo instruction for spilling SGPRs.
    const MCInstrDesc &OpDesc = get(getSGPRSpillSaveOpcode(SpillSize));

    // The SGPR spill/restore instructions only work on number sgprs, so we need
    // to make sure we are using the correct register class.
    if (TargetRegisterInfo::isVirtualRegister(SrcReg) && SpillSize == 4) {
      MachineRegisterInfo &MRI = MF->getRegInfo();
      MRI.constrainRegClass(SrcReg, &AMDGPU::SReg_32_XM0RegClass);
    }

    MachineInstrBuilder Spill = BuildMI(MBB, MI, DL, OpDesc)
      .addReg(SrcReg, getKillRegState(isKill)) // data
      .addFrameIndex(FrameIndex)               // addr
      .addMemOperand(MMO)
      .addReg(MFI->getScratchRSrcReg(), RegState::Implicit)
      .addReg(MFI->getFrameOffsetReg(), RegState::Implicit);
    // Add the scratch resource registers as implicit uses because we may end up
    // needing them, and need to ensure that the reserved registers are
    // correctly handled.

    FrameInfo.setStackID(FrameIndex, SIStackID::SGPR_SPILL);
    if (ST.hasScalarStores()) {
      // m0 is used for offset to scalar stores if used to spill.
      Spill.addReg(AMDGPU::M0, RegState::ImplicitDefine | RegState::Dead);
    }

    return;
  }

  assert(RI.hasVGPRs(RC) && "Only VGPR spilling expected");

  unsigned Opcode = getVGPRSpillSaveOpcode(SpillSize);
  MFI->setHasSpilledVGPRs();
  BuildMI(MBB, MI, DL, get(Opcode))
    .addReg(SrcReg, getKillRegState(isKill)) // data
    .addFrameIndex(FrameIndex)               // addr
    .addReg(MFI->getScratchRSrcReg())        // scratch_rsrc
    .addReg(MFI->getFrameOffsetReg())        // scratch_offset
    .addImm(0)                               // offset
    .addMemOperand(MMO);
}

static unsigned getSGPRSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_S32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_S64_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_S128_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_S256_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_S512_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

static unsigned getVGPRSpillRestoreOpcode(unsigned Size) {
  switch (Size) {
  case 4:
    return AMDGPU::SI_SPILL_V32_RESTORE;
  case 8:
    return AMDGPU::SI_SPILL_V64_RESTORE;
  case 12:
    return AMDGPU::SI_SPILL_V96_RESTORE;
  case 16:
    return AMDGPU::SI_SPILL_V128_RESTORE;
  case 32:
    return AMDGPU::SI_SPILL_V256_RESTORE;
  case 64:
    return AMDGPU::SI_SPILL_V512_RESTORE;
  default:
    llvm_unreachable("unknown register size");
  }
}

void SIInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       unsigned DestReg, int FrameIndex,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const DebugLoc &DL = MBB.findDebugLoc(MI);
  unsigned Align = FrameInfo.getObjectAlignment(FrameIndex);
  unsigned Size = FrameInfo.getObjectSize(FrameIndex);
  unsigned SpillSize = TRI->getSpillSize(*RC);

  MachinePointerInfo PtrInfo
    = MachinePointerInfo::getFixedStack(*MF, FrameIndex);

  MachineMemOperand *MMO = MF->getMachineMemOperand(
    PtrInfo, MachineMemOperand::MOLoad, Size, Align);

  if (RI.isSGPRClass(RC)) {
    MFI->setHasSpilledSGPRs();

    // FIXME: Maybe this should not include a memoperand because it will be
    // lowered to non-memory instructions.
    const MCInstrDesc &OpDesc = get(getSGPRSpillRestoreOpcode(SpillSize));
    if (TargetRegisterInfo::isVirtualRegister(DestReg) && SpillSize == 4) {
      MachineRegisterInfo &MRI = MF->getRegInfo();
      MRI.constrainRegClass(DestReg, &AMDGPU::SReg_32_XM0RegClass);
    }

    FrameInfo.setStackID(FrameIndex, SIStackID::SGPR_SPILL);
    MachineInstrBuilder Spill = BuildMI(MBB, MI, DL, OpDesc, DestReg)
      .addFrameIndex(FrameIndex) // addr
      .addMemOperand(MMO)
      .addReg(MFI->getScratchRSrcReg(), RegState::Implicit)
      .addReg(MFI->getFrameOffsetReg(), RegState::Implicit);

    if (ST.hasScalarStores()) {
      // m0 is used for offset to scalar stores if used to spill.
      Spill.addReg(AMDGPU::M0, RegState::ImplicitDefine | RegState::Dead);
    }

    return;
  }

  assert(RI.hasVGPRs(RC) && "Only VGPR spilling expected");

  unsigned Opcode = getVGPRSpillRestoreOpcode(SpillSize);
  BuildMI(MBB, MI, DL, get(Opcode), DestReg)
    .addFrameIndex(FrameIndex)        // vaddr
    .addReg(MFI->getScratchRSrcReg()) // scratch_rsrc
    .addReg(MFI->getFrameOffsetReg()) // scratch_offset
    .addImm(0)                        // offset
    .addMemOperand(MMO);
}

/// \param @Offset Offset in bytes of the FrameIndex being spilled
unsigned SIInstrInfo::calculateLDSSpillAddress(
    MachineBasicBlock &MBB, MachineInstr &MI, RegScavenger *RS, unsigned TmpReg,
    unsigned FrameOffset, unsigned Size) const {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
  const DebugLoc &DL = MBB.findDebugLoc(MI);
  unsigned WorkGroupSize = MFI->getMaxFlatWorkGroupSize();
  unsigned WavefrontSize = ST.getWavefrontSize();

  unsigned TIDReg = MFI->getTIDReg();
  if (!MFI->hasCalculatedTID()) {
    MachineBasicBlock &Entry = MBB.getParent()->front();
    MachineBasicBlock::iterator Insert = Entry.front();
    const DebugLoc &DL = Insert->getDebugLoc();

    TIDReg = RI.findUnusedRegister(MF->getRegInfo(), &AMDGPU::VGPR_32RegClass,
                                   *MF);
    if (TIDReg == AMDGPU::NoRegister)
      return TIDReg;

    if (!AMDGPU::isShader(MF->getFunction().getCallingConv()) &&
        WorkGroupSize > WavefrontSize) {
      unsigned TIDIGXReg
        = MFI->getPreloadedReg(AMDGPUFunctionArgInfo::WORKGROUP_ID_X);
      unsigned TIDIGYReg
        = MFI->getPreloadedReg(AMDGPUFunctionArgInfo::WORKGROUP_ID_Y);
      unsigned TIDIGZReg
        = MFI->getPreloadedReg(AMDGPUFunctionArgInfo::WORKGROUP_ID_Z);
      unsigned InputPtrReg =
          MFI->getPreloadedReg(AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR);
      for (unsigned Reg : {TIDIGXReg, TIDIGYReg, TIDIGZReg}) {
        if (!Entry.isLiveIn(Reg))
          Entry.addLiveIn(Reg);
      }

      RS->enterBasicBlock(Entry);
      // FIXME: Can we scavenge an SReg_64 and access the subregs?
      unsigned STmp0 = RS->scavengeRegister(&AMDGPU::SGPR_32RegClass, 0);
      unsigned STmp1 = RS->scavengeRegister(&AMDGPU::SGPR_32RegClass, 0);
      BuildMI(Entry, Insert, DL, get(AMDGPU::S_LOAD_DWORD_IMM), STmp0)
              .addReg(InputPtrReg)
              .addImm(SI::KernelInputOffsets::NGROUPS_Z);
      BuildMI(Entry, Insert, DL, get(AMDGPU::S_LOAD_DWORD_IMM), STmp1)
              .addReg(InputPtrReg)
              .addImm(SI::KernelInputOffsets::NGROUPS_Y);

      // NGROUPS.X * NGROUPS.Y
      BuildMI(Entry, Insert, DL, get(AMDGPU::S_MUL_I32), STmp1)
              .addReg(STmp1)
              .addReg(STmp0);
      // (NGROUPS.X * NGROUPS.Y) * TIDIG.X
      BuildMI(Entry, Insert, DL, get(AMDGPU::V_MUL_U32_U24_e32), TIDReg)
              .addReg(STmp1)
              .addReg(TIDIGXReg);
      // NGROUPS.Z * TIDIG.Y + (NGROUPS.X * NGROPUS.Y * TIDIG.X)
      BuildMI(Entry, Insert, DL, get(AMDGPU::V_MAD_U32_U24), TIDReg)
              .addReg(STmp0)
              .addReg(TIDIGYReg)
              .addReg(TIDReg);
      // (NGROUPS.Z * TIDIG.Y + (NGROUPS.X * NGROPUS.Y * TIDIG.X)) + TIDIG.Z
      getAddNoCarry(Entry, Insert, DL, TIDReg)
        .addReg(TIDReg)
        .addReg(TIDIGZReg);
    } else {
      // Get the wave id
      BuildMI(Entry, Insert, DL, get(AMDGPU::V_MBCNT_LO_U32_B32_e64),
              TIDReg)
              .addImm(-1)
              .addImm(0);

      BuildMI(Entry, Insert, DL, get(AMDGPU::V_MBCNT_HI_U32_B32_e64),
              TIDReg)
              .addImm(-1)
              .addReg(TIDReg);
    }

    BuildMI(Entry, Insert, DL, get(AMDGPU::V_LSHLREV_B32_e32),
            TIDReg)
            .addImm(2)
            .addReg(TIDReg);
    MFI->setTIDReg(TIDReg);
  }

  // Add FrameIndex to LDS offset
  unsigned LDSOffset = MFI->getLDSSize() + (FrameOffset * WorkGroupSize);
  getAddNoCarry(MBB, MI, DL, TmpReg)
    .addImm(LDSOffset)
    .addReg(TIDReg);

  return TmpReg;
}

void SIInstrInfo::insertWaitStates(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   int Count) const {
  DebugLoc DL = MBB.findDebugLoc(MI);
  while (Count > 0) {
    int Arg;
    if (Count >= 8)
      Arg = 7;
    else
      Arg = Count - 1;
    Count -= 8;
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOP))
            .addImm(Arg);
  }
}

void SIInstrInfo::insertNoop(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MI) const {
  insertWaitStates(MBB, MI, 1);
}

void SIInstrInfo::insertReturn(MachineBasicBlock &MBB) const {
  auto MF = MBB.getParent();
  SIMachineFunctionInfo *Info = MF->getInfo<SIMachineFunctionInfo>();

  assert(Info->isEntryFunction());

  if (MBB.succ_empty()) {
    bool HasNoTerminator = MBB.getFirstTerminator() == MBB.end();
    if (HasNoTerminator)
      BuildMI(MBB, MBB.end(), DebugLoc(),
              get(Info->returnsVoid() ? AMDGPU::S_ENDPGM : AMDGPU::SI_RETURN_TO_EPILOG));
  }
}

unsigned SIInstrInfo::getNumWaitStates(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default: return 1; // FIXME: Do wait states equal cycles?

  case AMDGPU::S_NOP:
    return MI.getOperand(0).getImm() + 1;
  }
}

bool SIInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MBB.findDebugLoc(MI);
  switch (MI.getOpcode()) {
  default: return TargetInstrInfo::expandPostRAPseudo(MI);
  case AMDGPU::S_MOV_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_MOV_B64));
    break;

  case AMDGPU::S_XOR_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_XOR_B64));
    break;

  case AMDGPU::S_ANDN2_B64_term:
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(get(AMDGPU::S_ANDN2_B64));
    break;

  case AMDGPU::V_MOV_B64_PSEUDO: {
    unsigned Dst = MI.getOperand(0).getReg();
    unsigned DstLo = RI.getSubReg(Dst, AMDGPU::sub0);
    unsigned DstHi = RI.getSubReg(Dst, AMDGPU::sub1);

    const MachineOperand &SrcOp = MI.getOperand(1);
    // FIXME: Will this work for 64-bit floating point immediates?
    assert(!SrcOp.isFPImm());
    if (SrcOp.isImm()) {
      APInt Imm(64, SrcOp.getImm());
      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstLo)
        .addImm(Imm.getLoBits(32).getZExtValue())
        .addReg(Dst, RegState::Implicit | RegState::Define);
      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstHi)
        .addImm(Imm.getHiBits(32).getZExtValue())
        .addReg(Dst, RegState::Implicit | RegState::Define);
    } else {
      assert(SrcOp.isReg());
      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstLo)
        .addReg(RI.getSubReg(SrcOp.getReg(), AMDGPU::sub0))
        .addReg(Dst, RegState::Implicit | RegState::Define);
      BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), DstHi)
        .addReg(RI.getSubReg(SrcOp.getReg(), AMDGPU::sub1))
        .addReg(Dst, RegState::Implicit | RegState::Define);
    }
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_SET_INACTIVE_B32: {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOT_B64), AMDGPU::EXEC)
      .addReg(AMDGPU::EXEC);
    BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B32_e32), MI.getOperand(0).getReg())
      .add(MI.getOperand(2));
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOT_B64), AMDGPU::EXEC)
      .addReg(AMDGPU::EXEC);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_SET_INACTIVE_B64: {
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOT_B64), AMDGPU::EXEC)
      .addReg(AMDGPU::EXEC);
    MachineInstr *Copy = BuildMI(MBB, MI, DL, get(AMDGPU::V_MOV_B64_PSEUDO),
                                 MI.getOperand(0).getReg())
      .add(MI.getOperand(2));
    expandPostRAPseudo(*Copy);
    BuildMI(MBB, MI, DL, get(AMDGPU::S_NOT_B64), AMDGPU::EXEC)
      .addReg(AMDGPU::EXEC);
    MI.eraseFromParent();
    break;
  }
  case AMDGPU::V_MOVRELD_B32_V1:
  case AMDGPU::V_MOVRELD_B32_V2:
  case AMDGPU::V_MOVRELD_B32_V4:
  case AMDGPU::V_MOVRELD_B32_V8:
  case AMDGPU::V_MOVRELD_B32_V16: {
    const MCInstrDesc &MovRelDesc = get(AMDGPU::V_MOVRELD_B32_e32);
    unsigned VecReg = MI.getOperand(0).getReg();
    bool IsUndef = MI.getOperand(1).isUndef();
    unsigned SubReg = AMDGPU::sub0 + MI.getOperand(3).getImm();
    assert(VecReg == MI.getOperand(1).getReg());

    MachineInstr *MovRel =
        BuildMI(MBB, MI, DL, MovRelDesc)
            .addReg(RI.getSubReg(VecReg, SubReg), RegState::Undef)
            .add(MI.getOperand(2))
            .addReg(VecReg, RegState::ImplicitDefine)
            .addReg(VecReg,
                    RegState::Implicit | (IsUndef ? RegState::Undef : 0));

    const int ImpDefIdx =
        MovRelDesc.getNumOperands() + MovRelDesc.getNumImplicitUses();
    const int ImpUseIdx = ImpDefIdx + 1;
    MovRel->tieOperands(ImpDefIdx, ImpUseIdx);

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::SI_PC_ADD_REL_OFFSET: {
    MachineFunction &MF = *MBB.getParent();
    unsigned Reg = MI.getOperand(0).getReg();
    unsigned RegLo = RI.getSubReg(Reg, AMDGPU::sub0);
    unsigned RegHi = RI.getSubReg(Reg, AMDGPU::sub1);

    // Create a bundle so these instructions won't be re-ordered by the
    // post-RA scheduler.
    MIBundleBuilder Bundler(MBB, MI);
    Bundler.append(BuildMI(MF, DL, get(AMDGPU::S_GETPC_B64), Reg));

    // Add 32-bit offset from this instruction to the start of the
    // constant data.
    Bundler.append(BuildMI(MF, DL, get(AMDGPU::S_ADD_U32), RegLo)
                       .addReg(RegLo)
                       .add(MI.getOperand(1)));

    MachineInstrBuilder MIB = BuildMI(MF, DL, get(AMDGPU::S_ADDC_U32), RegHi)
                                  .addReg(RegHi);
    if (MI.getOperand(2).getTargetFlags() == SIInstrInfo::MO_NONE)
      MIB.addImm(0);
    else
      MIB.add(MI.getOperand(2));

    Bundler.append(MIB);
    finalizeBundle(MBB, Bundler.begin());

    MI.eraseFromParent();
    break;
  }
  case AMDGPU::EXIT_WWM: {
    // This only gets its own opcode so that SIFixWWMLiveness can tell when WWM
    // is exited.
    MI.setDesc(get(AMDGPU::S_MOV_B64));
    break;
  }
  case TargetOpcode::BUNDLE: {
    if (!MI.mayLoad())
      return false;

    // If it is a load it must be a memory clause
    for (MachineBasicBlock::instr_iterator I = MI.getIterator();
         I->isBundledWithSucc(); ++I) {
      I->unbundleFromSucc();
      for (MachineOperand &MO : I->operands())
        if (MO.isReg())
          MO.setIsInternalRead(false);
    }

    MI.eraseFromParent();
    break;
  }
  }
  return true;
}

bool SIInstrInfo::swapSourceModifiers(MachineInstr &MI,
                                      MachineOperand &Src0,
                                      unsigned Src0OpName,
                                      MachineOperand &Src1,
                                      unsigned Src1OpName) const {
  MachineOperand *Src0Mods = getNamedOperand(MI, Src0OpName);
  if (!Src0Mods)
    return false;

  MachineOperand *Src1Mods = getNamedOperand(MI, Src1OpName);
  assert(Src1Mods &&
         "All commutable instructions have both src0 and src1 modifiers");

  int Src0ModsVal = Src0Mods->getImm();
  int Src1ModsVal = Src1Mods->getImm();

  Src1Mods->setImm(Src0ModsVal);
  Src0Mods->setImm(Src1ModsVal);
  return true;
}

static MachineInstr *swapRegAndNonRegOperand(MachineInstr &MI,
                                             MachineOperand &RegOp,
                                             MachineOperand &NonRegOp) {
  unsigned Reg = RegOp.getReg();
  unsigned SubReg = RegOp.getSubReg();
  bool IsKill = RegOp.isKill();
  bool IsDead = RegOp.isDead();
  bool IsUndef = RegOp.isUndef();
  bool IsDebug = RegOp.isDebug();

  if (NonRegOp.isImm())
    RegOp.ChangeToImmediate(NonRegOp.getImm());
  else if (NonRegOp.isFI())
    RegOp.ChangeToFrameIndex(NonRegOp.getIndex());
  else
    return nullptr;

  NonRegOp.ChangeToRegister(Reg, false, false, IsKill, IsDead, IsUndef, IsDebug);
  NonRegOp.setSubReg(SubReg);

  return &MI;
}

MachineInstr *SIInstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                  unsigned Src0Idx,
                                                  unsigned Src1Idx) const {
  assert(!NewMI && "this should never be used");

  unsigned Opc = MI.getOpcode();
  int CommutedOpcode = commuteOpcode(Opc);
  if (CommutedOpcode == -1)
    return nullptr;

  assert(AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0) ==
           static_cast<int>(Src0Idx) &&
         AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1) ==
           static_cast<int>(Src1Idx) &&
         "inconsistency with findCommutedOpIndices");

  MachineOperand &Src0 = MI.getOperand(Src0Idx);
  MachineOperand &Src1 = MI.getOperand(Src1Idx);

  MachineInstr *CommutedMI = nullptr;
  if (Src0.isReg() && Src1.isReg()) {
    if (isOperandLegal(MI, Src1Idx, &Src0)) {
      // Be sure to copy the source modifiers to the right place.
      CommutedMI
        = TargetInstrInfo::commuteInstructionImpl(MI, NewMI, Src0Idx, Src1Idx);
    }

  } else if (Src0.isReg() && !Src1.isReg()) {
    // src0 should always be able to support any operand type, so no need to
    // check operand legality.
    CommutedMI = swapRegAndNonRegOperand(MI, Src0, Src1);
  } else if (!Src0.isReg() && Src1.isReg()) {
    if (isOperandLegal(MI, Src1Idx, &Src0))
      CommutedMI = swapRegAndNonRegOperand(MI, Src1, Src0);
  } else {
    // FIXME: Found two non registers to commute. This does happen.
    return nullptr;
  }

  if (CommutedMI) {
    swapSourceModifiers(MI, Src0, AMDGPU::OpName::src0_modifiers,
                        Src1, AMDGPU::OpName::src1_modifiers);

    CommutedMI->setDesc(get(CommutedOpcode));
  }

  return CommutedMI;
}

// This needs to be implemented because the source modifiers may be inserted
// between the true commutable operands, and the base
// TargetInstrInfo::commuteInstruction uses it.
bool SIInstrInfo::findCommutedOpIndices(MachineInstr &MI, unsigned &SrcOpIdx0,
                                        unsigned &SrcOpIdx1) const {
  return findCommutedOpIndices(MI.getDesc(), SrcOpIdx0, SrcOpIdx1);
}

bool SIInstrInfo::findCommutedOpIndices(MCInstrDesc Desc, unsigned &SrcOpIdx0,
                                        unsigned &SrcOpIdx1) const {
  if (!Desc.isCommutable())
    return false;

  unsigned Opc = Desc.getOpcode();
  int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  if (Src0Idx == -1)
    return false;

  int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
  if (Src1Idx == -1)
    return false;

  return fixCommutedOpIndices(SrcOpIdx0, SrcOpIdx1, Src0Idx, Src1Idx);
}

bool SIInstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                        int64_t BrOffset) const {
  // BranchRelaxation should never have to check s_setpc_b64 because its dest
  // block is unanalyzable.
  assert(BranchOp != AMDGPU::S_SETPC_B64);

  // Convert to dwords.
  BrOffset /= 4;

  // The branch instructions do PC += signext(SIMM16 * 4) + 4, so the offset is
  // from the next instruction.
  BrOffset -= 1;

  return isIntN(BranchOffsetBits, BrOffset);
}

MachineBasicBlock *SIInstrInfo::getBranchDestBlock(
  const MachineInstr &MI) const {
  if (MI.getOpcode() == AMDGPU::S_SETPC_B64) {
    // This would be a difficult analysis to perform, but can always be legal so
    // there's no need to analyze it.
    return nullptr;
  }

  return MI.getOperand(0).getMBB();
}

unsigned SIInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                           MachineBasicBlock &DestBB,
                                           const DebugLoc &DL,
                                           int64_t BrOffset,
                                           RegScavenger *RS) const {
  assert(RS && "RegScavenger required for long branching");
  assert(MBB.empty() &&
         "new block should be inserted for expanding unconditional branch");
  assert(MBB.pred_size() == 1);

  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  // FIXME: Virtual register workaround for RegScavenger not working with empty
  // blocks.
  unsigned PCReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);

  auto I = MBB.end();

  // We need to compute the offset relative to the instruction immediately after
  // s_getpc_b64. Insert pc arithmetic code before last terminator.
  MachineInstr *GetPC = BuildMI(MBB, I, DL, get(AMDGPU::S_GETPC_B64), PCReg);

  // TODO: Handle > 32-bit block address.
  if (BrOffset >= 0) {
    BuildMI(MBB, I, DL, get(AMDGPU::S_ADD_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub0)
      .addReg(PCReg, 0, AMDGPU::sub0)
      .addMBB(&DestBB, AMDGPU::TF_LONG_BRANCH_FORWARD);
    BuildMI(MBB, I, DL, get(AMDGPU::S_ADDC_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub1)
      .addReg(PCReg, 0, AMDGPU::sub1)
      .addImm(0);
  } else {
    // Backwards branch.
    BuildMI(MBB, I, DL, get(AMDGPU::S_SUB_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub0)
      .addReg(PCReg, 0, AMDGPU::sub0)
      .addMBB(&DestBB, AMDGPU::TF_LONG_BRANCH_BACKWARD);
    BuildMI(MBB, I, DL, get(AMDGPU::S_SUBB_U32))
      .addReg(PCReg, RegState::Define, AMDGPU::sub1)
      .addReg(PCReg, 0, AMDGPU::sub1)
      .addImm(0);
  }

  // Insert the indirect branch after the other terminator.
  BuildMI(&MBB, DL, get(AMDGPU::S_SETPC_B64))
    .addReg(PCReg);

  // FIXME: If spilling is necessary, this will fail because this scavenger has
  // no emergency stack slots. It is non-trivial to spill in this situation,
  // because the restore code needs to be specially placed after the
  // jump. BranchRelaxation then needs to be made aware of the newly inserted
  // block.
  //
  // If a spill is needed for the pc register pair, we need to insert a spill
  // restore block right before the destination block, and insert a short branch
  // into the old destination block's fallthrough predecessor.
  // e.g.:
  //
  // s_cbranch_scc0 skip_long_branch:
  //
  // long_branch_bb:
  //   spill s[8:9]
  //   s_getpc_b64 s[8:9]
  //   s_add_u32 s8, s8, restore_bb
  //   s_addc_u32 s9, s9, 0
  //   s_setpc_b64 s[8:9]
  //
  // skip_long_branch:
  //   foo;
  //
  // .....
  //
  // dest_bb_fallthrough_predecessor:
  // bar;
  // s_branch dest_bb
  //
  // restore_bb:
  //  restore s[8:9]
  //  fallthrough dest_bb
  ///
  // dest_bb:
  //   buzz;

  RS->enterBasicBlockEnd(MBB);
  unsigned Scav = RS->scavengeRegisterBackwards(
    AMDGPU::SReg_64RegClass,
    MachineBasicBlock::iterator(GetPC), false, 0);
  MRI.replaceRegWith(PCReg, Scav);
  MRI.clearVirtRegs();
  RS->setRegUsed(Scav);

  return 4 + 8 + 4 + 4;
}

unsigned SIInstrInfo::getBranchOpcode(SIInstrInfo::BranchPredicate Cond) {
  switch (Cond) {
  case SIInstrInfo::SCC_TRUE:
    return AMDGPU::S_CBRANCH_SCC1;
  case SIInstrInfo::SCC_FALSE:
    return AMDGPU::S_CBRANCH_SCC0;
  case SIInstrInfo::VCCNZ:
    return AMDGPU::S_CBRANCH_VCCNZ;
  case SIInstrInfo::VCCZ:
    return AMDGPU::S_CBRANCH_VCCZ;
  case SIInstrInfo::EXECNZ:
    return AMDGPU::S_CBRANCH_EXECNZ;
  case SIInstrInfo::EXECZ:
    return AMDGPU::S_CBRANCH_EXECZ;
  default:
    llvm_unreachable("invalid branch predicate");
  }
}

SIInstrInfo::BranchPredicate SIInstrInfo::getBranchPredicate(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_CBRANCH_SCC0:
    return SCC_FALSE;
  case AMDGPU::S_CBRANCH_SCC1:
    return SCC_TRUE;
  case AMDGPU::S_CBRANCH_VCCNZ:
    return VCCNZ;
  case AMDGPU::S_CBRANCH_VCCZ:
    return VCCZ;
  case AMDGPU::S_CBRANCH_EXECNZ:
    return EXECNZ;
  case AMDGPU::S_CBRANCH_EXECZ:
    return EXECZ;
  default:
    return INVALID_BR;
  }
}

bool SIInstrInfo::analyzeBranchImpl(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    MachineBasicBlock *&TBB,
                                    MachineBasicBlock *&FBB,
                                    SmallVectorImpl<MachineOperand> &Cond,
                                    bool AllowModify) const {
  if (I->getOpcode() == AMDGPU::S_BRANCH) {
    // Unconditional Branch
    TBB = I->getOperand(0).getMBB();
    return false;
  }

  MachineBasicBlock *CondBB = nullptr;

  if (I->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {
    CondBB = I->getOperand(1).getMBB();
    Cond.push_back(I->getOperand(0));
  } else {
    BranchPredicate Pred = getBranchPredicate(I->getOpcode());
    if (Pred == INVALID_BR)
      return true;

    CondBB = I->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(Pred));
    Cond.push_back(I->getOperand(1)); // Save the branch register.
  }
  ++I;

  if (I == MBB.end()) {
    // Conditional branch followed by fall-through.
    TBB = CondBB;
    return false;
  }

  if (I->getOpcode() == AMDGPU::S_BRANCH) {
    TBB = CondBB;
    FBB = I->getOperand(0).getMBB();
    return false;
  }

  return true;
}

bool SIInstrInfo::analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                                MachineBasicBlock *&FBB,
                                SmallVectorImpl<MachineOperand> &Cond,
                                bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();
  auto E = MBB.end();
  if (I == E)
    return false;

  // Skip over the instructions that are artificially terminators for special
  // exec management.
  while (I != E && !I->isBranch() && !I->isReturn() &&
         I->getOpcode() != AMDGPU::SI_MASK_BRANCH) {
    switch (I->getOpcode()) {
    case AMDGPU::SI_MASK_BRANCH:
    case AMDGPU::S_MOV_B64_term:
    case AMDGPU::S_XOR_B64_term:
    case AMDGPU::S_ANDN2_B64_term:
      break;
    case AMDGPU::SI_IF:
    case AMDGPU::SI_ELSE:
    case AMDGPU::SI_KILL_I1_TERMINATOR:
    case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
      // FIXME: It's messy that these need to be considered here at all.
      return true;
    default:
      llvm_unreachable("unexpected non-branch terminator inst");
    }

    ++I;
  }

  if (I == E)
    return false;

  if (I->getOpcode() != AMDGPU::SI_MASK_BRANCH)
    return analyzeBranchImpl(MBB, I, TBB, FBB, Cond, AllowModify);

  ++I;

  // TODO: Should be able to treat as fallthrough?
  if (I == MBB.end())
    return true;

  if (analyzeBranchImpl(MBB, I, TBB, FBB, Cond, AllowModify))
    return true;

  MachineBasicBlock *MaskBrDest = I->getOperand(0).getMBB();

  // Specifically handle the case where the conditional branch is to the same
  // destination as the mask branch. e.g.
  //
  // si_mask_branch BB8
  // s_cbranch_execz BB8
  // s_cbranch BB9
  //
  // This is required to understand divergent loops which may need the branches
  // to be relaxed.
  if (TBB != MaskBrDest || Cond.empty())
    return true;

  auto Pred = Cond[0].getImm();
  return (Pred != EXECZ && Pred != EXECNZ);
}

unsigned SIInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                   int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();

  unsigned Count = 0;
  unsigned RemovedSize = 0;
  while (I != MBB.end()) {
    MachineBasicBlock::iterator Next = std::next(I);
    if (I->getOpcode() == AMDGPU::SI_MASK_BRANCH) {
      I = Next;
      continue;
    }

    RemovedSize += getInstSizeInBytes(*I);
    I->eraseFromParent();
    ++Count;
    I = Next;
  }

  if (BytesRemoved)
    *BytesRemoved = RemovedSize;

  return Count;
}

// Copy the flags onto the implicit condition register operand.
static void preserveCondRegFlags(MachineOperand &CondReg,
                                 const MachineOperand &OrigCond) {
  CondReg.setIsUndef(OrigCond.isUndef());
  CondReg.setIsKill(OrigCond.isKill());
}

unsigned SIInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *TBB,
                                   MachineBasicBlock *FBB,
                                   ArrayRef<MachineOperand> Cond,
                                   const DebugLoc &DL,
                                   int *BytesAdded) const {
  if (!FBB && Cond.empty()) {
    BuildMI(&MBB, DL, get(AMDGPU::S_BRANCH))
      .addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = 4;
    return 1;
  }

  if(Cond.size() == 1 && Cond[0].isReg()) {
     BuildMI(&MBB, DL, get(AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO))
       .add(Cond[0])
       .addMBB(TBB);
     return 1;
  }

  assert(TBB && Cond[0].isImm());

  unsigned Opcode
    = getBranchOpcode(static_cast<BranchPredicate>(Cond[0].getImm()));

  if (!FBB) {
    Cond[1].isUndef();
    MachineInstr *CondBr =
      BuildMI(&MBB, DL, get(Opcode))
      .addMBB(TBB);

    // Copy the flags onto the implicit condition register operand.
    preserveCondRegFlags(CondBr->getOperand(1), Cond[1]);

    if (BytesAdded)
      *BytesAdded = 4;
    return 1;
  }

  assert(TBB && FBB);

  MachineInstr *CondBr =
    BuildMI(&MBB, DL, get(Opcode))
    .addMBB(TBB);
  BuildMI(&MBB, DL, get(AMDGPU::S_BRANCH))
    .addMBB(FBB);

  MachineOperand &CondReg = CondBr->getOperand(1);
  CondReg.setIsUndef(Cond[1].isUndef());
  CondReg.setIsKill(Cond[1].isKill());

  if (BytesAdded)
      *BytesAdded = 8;

  return 2;
}

bool SIInstrInfo::reverseBranchCondition(
  SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() != 2) {
    return true;
  }

  if (Cond[0].isImm()) {
    Cond[0].setImm(-Cond[0].getImm());
    return false;
  }

  return true;
}

bool SIInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                  ArrayRef<MachineOperand> Cond,
                                  unsigned TrueReg, unsigned FalseReg,
                                  int &CondCycles,
                                  int &TrueCycles, int &FalseCycles) const {
  switch (Cond[0].getImm()) {
  case VCCNZ:
  case VCCZ: {
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(TrueReg);
    assert(MRI.getRegClass(FalseReg) == RC);

    int NumInsts = AMDGPU::getRegBitWidth(RC->getID()) / 32;
    CondCycles = TrueCycles = FalseCycles = NumInsts; // ???

    // Limit to equal cost for branch vs. N v_cndmask_b32s.
    return !RI.isSGPRClass(RC) && NumInsts <= 6;
  }
  case SCC_TRUE:
  case SCC_FALSE: {
    // FIXME: We could insert for VGPRs if we could replace the original compare
    // with a vector one.
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(TrueReg);
    assert(MRI.getRegClass(FalseReg) == RC);

    int NumInsts = AMDGPU::getRegBitWidth(RC->getID()) / 32;

    // Multiples of 8 can do s_cselect_b64
    if (NumInsts % 2 == 0)
      NumInsts /= 2;

    CondCycles = TrueCycles = FalseCycles = NumInsts; // ???
    return RI.isSGPRClass(RC);
  }
  default:
    return false;
  }
}

void SIInstrInfo::insertSelect(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I, const DebugLoc &DL,
                               unsigned DstReg, ArrayRef<MachineOperand> Cond,
                               unsigned TrueReg, unsigned FalseReg) const {
  BranchPredicate Pred = static_cast<BranchPredicate>(Cond[0].getImm());
  if (Pred == VCCZ || Pred == SCC_FALSE) {
    Pred = static_cast<BranchPredicate>(-Pred);
    std::swap(TrueReg, FalseReg);
  }

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *DstRC = MRI.getRegClass(DstReg);
  unsigned DstSize = RI.getRegSizeInBits(*DstRC);

  if (DstSize == 32) {
    unsigned SelOp = Pred == SCC_TRUE ?
      AMDGPU::S_CSELECT_B32 : AMDGPU::V_CNDMASK_B32_e32;

    // Instruction's operands are backwards from what is expected.
    MachineInstr *Select =
      BuildMI(MBB, I, DL, get(SelOp), DstReg)
      .addReg(FalseReg)
      .addReg(TrueReg);

    preserveCondRegFlags(Select->getOperand(3), Cond[1]);
    return;
  }

  if (DstSize == 64 && Pred == SCC_TRUE) {
    MachineInstr *Select =
      BuildMI(MBB, I, DL, get(AMDGPU::S_CSELECT_B64), DstReg)
      .addReg(FalseReg)
      .addReg(TrueReg);

    preserveCondRegFlags(Select->getOperand(3), Cond[1]);
    return;
  }

  static const int16_t Sub0_15[] = {
    AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3,
    AMDGPU::sub4, AMDGPU::sub5, AMDGPU::sub6, AMDGPU::sub7,
    AMDGPU::sub8, AMDGPU::sub9, AMDGPU::sub10, AMDGPU::sub11,
    AMDGPU::sub12, AMDGPU::sub13, AMDGPU::sub14, AMDGPU::sub15,
  };

  static const int16_t Sub0_15_64[] = {
    AMDGPU::sub0_sub1, AMDGPU::sub2_sub3,
    AMDGPU::sub4_sub5, AMDGPU::sub6_sub7,
    AMDGPU::sub8_sub9, AMDGPU::sub10_sub11,
    AMDGPU::sub12_sub13, AMDGPU::sub14_sub15,
  };

  unsigned SelOp = AMDGPU::V_CNDMASK_B32_e32;
  const TargetRegisterClass *EltRC = &AMDGPU::VGPR_32RegClass;
  const int16_t *SubIndices = Sub0_15;
  int NElts = DstSize / 32;

  // 64-bit select is only avaialble for SALU.
  if (Pred == SCC_TRUE) {
    SelOp = AMDGPU::S_CSELECT_B64;
    EltRC = &AMDGPU::SGPR_64RegClass;
    SubIndices = Sub0_15_64;

    assert(NElts % 2 == 0);
    NElts /= 2;
  }

  MachineInstrBuilder MIB = BuildMI(
    MBB, I, DL, get(AMDGPU::REG_SEQUENCE), DstReg);

  I = MIB->getIterator();

  SmallVector<unsigned, 8> Regs;
  for (int Idx = 0; Idx != NElts; ++Idx) {
    unsigned DstElt = MRI.createVirtualRegister(EltRC);
    Regs.push_back(DstElt);

    unsigned SubIdx = SubIndices[Idx];

    MachineInstr *Select =
      BuildMI(MBB, I, DL, get(SelOp), DstElt)
      .addReg(FalseReg, 0, SubIdx)
      .addReg(TrueReg, 0, SubIdx);
    preserveCondRegFlags(Select->getOperand(3), Cond[1]);

    MIB.addReg(DstElt)
       .addImm(SubIdx);
  }
}

bool SIInstrInfo::isFoldableCopy(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case AMDGPU::V_MOV_B32_e32:
  case AMDGPU::V_MOV_B32_e64:
  case AMDGPU::V_MOV_B64_PSEUDO: {
    // If there are additional implicit register operands, this may be used for
    // register indexing so the source register operand isn't simply copied.
    unsigned NumOps = MI.getDesc().getNumOperands() +
      MI.getDesc().getNumImplicitUses();

    return MI.getNumOperands() == NumOps;
  }
  case AMDGPU::S_MOV_B32:
  case AMDGPU::S_MOV_B64:
  case AMDGPU::COPY:
    return true;
  default:
    return false;
  }
}

unsigned SIInstrInfo::getAddressSpaceForPseudoSourceKind(
    unsigned Kind) const {
  switch(Kind) {
  case PseudoSourceValue::Stack:
  case PseudoSourceValue::FixedStack:
    return AMDGPUAS::PRIVATE_ADDRESS;
  case PseudoSourceValue::ConstantPool:
  case PseudoSourceValue::GOT:
  case PseudoSourceValue::JumpTable:
  case PseudoSourceValue::GlobalValueCallEntry:
  case PseudoSourceValue::ExternalSymbolCallEntry:
  case PseudoSourceValue::TargetCustom:
    return AMDGPUAS::CONSTANT_ADDRESS;
  }
  return AMDGPUAS::FLAT_ADDRESS;
}

static void removeModOperands(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  int Src0ModIdx = AMDGPU::getNamedOperandIdx(Opc,
                                              AMDGPU::OpName::src0_modifiers);
  int Src1ModIdx = AMDGPU::getNamedOperandIdx(Opc,
                                              AMDGPU::OpName::src1_modifiers);
  int Src2ModIdx = AMDGPU::getNamedOperandIdx(Opc,
                                              AMDGPU::OpName::src2_modifiers);

  MI.RemoveOperand(Src2ModIdx);
  MI.RemoveOperand(Src1ModIdx);
  MI.RemoveOperand(Src0ModIdx);
}

bool SIInstrInfo::FoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                unsigned Reg, MachineRegisterInfo *MRI) const {
  if (!MRI->hasOneNonDBGUse(Reg))
    return false;

  switch (DefMI.getOpcode()) {
  default:
    return false;
  case AMDGPU::S_MOV_B64:
    // TODO: We could fold 64-bit immediates, but this get compilicated
    // when there are sub-registers.
    return false;

  case AMDGPU::V_MOV_B32_e32:
  case AMDGPU::S_MOV_B32:
    break;
  }

  const MachineOperand *ImmOp = getNamedOperand(DefMI, AMDGPU::OpName::src0);
  assert(ImmOp);
  // FIXME: We could handle FrameIndex values here.
  if (!ImmOp->isImm())
    return false;

  unsigned Opc = UseMI.getOpcode();
  if (Opc == AMDGPU::COPY) {
    bool isVGPRCopy = RI.isVGPR(*MRI, UseMI.getOperand(0).getReg());
    unsigned NewOpc = isVGPRCopy ? AMDGPU::V_MOV_B32_e32 : AMDGPU::S_MOV_B32;
    UseMI.setDesc(get(NewOpc));
    UseMI.getOperand(1).ChangeToImmediate(ImmOp->getImm());
    UseMI.addImplicitDefUseOperands(*UseMI.getParent()->getParent());
    return true;
  }

  if (Opc == AMDGPU::V_MAD_F32 || Opc == AMDGPU::V_MAC_F32_e64 ||
      Opc == AMDGPU::V_MAD_F16 || Opc == AMDGPU::V_MAC_F16_e64) {
    // Don't fold if we are using source or output modifiers. The new VOP2
    // instructions don't have them.
    if (hasAnyModifiersSet(UseMI))
      return false;

    // If this is a free constant, there's no reason to do this.
    // TODO: We could fold this here instead of letting SIFoldOperands do it
    // later.
    MachineOperand *Src0 = getNamedOperand(UseMI, AMDGPU::OpName::src0);

    // Any src operand can be used for the legality check.
    if (isInlineConstant(UseMI, *Src0, *ImmOp))
      return false;

    bool IsF32 = Opc == AMDGPU::V_MAD_F32 || Opc == AMDGPU::V_MAC_F32_e64;
    MachineOperand *Src1 = getNamedOperand(UseMI, AMDGPU::OpName::src1);
    MachineOperand *Src2 = getNamedOperand(UseMI, AMDGPU::OpName::src2);

    // Multiplied part is the constant: Use v_madmk_{f16, f32}.
    // We should only expect these to be on src0 due to canonicalizations.
    if (Src0->isReg() && Src0->getReg() == Reg) {
      if (!Src1->isReg() || RI.isSGPRClass(MRI->getRegClass(Src1->getReg())))
        return false;

      if (!Src2->isReg() || RI.isSGPRClass(MRI->getRegClass(Src2->getReg())))
        return false;

      // We need to swap operands 0 and 1 since madmk constant is at operand 1.

      const int64_t Imm = ImmOp->getImm();

      // FIXME: This would be a lot easier if we could return a new instruction
      // instead of having to modify in place.

      // Remove these first since they are at the end.
      UseMI.RemoveOperand(
          AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::omod));
      UseMI.RemoveOperand(
          AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::clamp));

      unsigned Src1Reg = Src1->getReg();
      unsigned Src1SubReg = Src1->getSubReg();
      Src0->setReg(Src1Reg);
      Src0->setSubReg(Src1SubReg);
      Src0->setIsKill(Src1->isKill());

      if (Opc == AMDGPU::V_MAC_F32_e64 ||
          Opc == AMDGPU::V_MAC_F16_e64)
        UseMI.untieRegOperand(
            AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2));

      Src1->ChangeToImmediate(Imm);

      removeModOperands(UseMI);
      UseMI.setDesc(get(IsF32 ? AMDGPU::V_MADMK_F32 : AMDGPU::V_MADMK_F16));

      bool DeleteDef = MRI->hasOneNonDBGUse(Reg);
      if (DeleteDef)
        DefMI.eraseFromParent();

      return true;
    }

    // Added part is the constant: Use v_madak_{f16, f32}.
    if (Src2->isReg() && Src2->getReg() == Reg) {
      // Not allowed to use constant bus for another operand.
      // We can however allow an inline immediate as src0.
      bool Src0Inlined = false;
      if (Src0->isReg()) {
        // Try to inline constant if possible.
        // If the Def moves immediate and the use is single
        // We are saving VGPR here.
        MachineInstr *Def = MRI->getUniqueVRegDef(Src0->getReg());
        if (Def && Def->isMoveImmediate() &&
          isInlineConstant(Def->getOperand(1)) &&
          MRI->hasOneUse(Src0->getReg())) {
          Src0->ChangeToImmediate(Def->getOperand(1).getImm());
          Src0Inlined = true;
        } else if ((RI.isPhysicalRegister(Src0->getReg()) &&
            RI.isSGPRClass(RI.getPhysRegClass(Src0->getReg()))) ||
            (RI.isVirtualRegister(Src0->getReg()) &&
            RI.isSGPRClass(MRI->getRegClass(Src0->getReg()))))
          return false;
          // VGPR is okay as Src0 - fallthrough
      }

      if (Src1->isReg() && !Src0Inlined ) {
        // We have one slot for inlinable constant so far - try to fill it
        MachineInstr *Def = MRI->getUniqueVRegDef(Src1->getReg());
        if (Def && Def->isMoveImmediate() &&
            isInlineConstant(Def->getOperand(1)) &&
            MRI->hasOneUse(Src1->getReg()) &&
            commuteInstruction(UseMI)) {
            Src0->ChangeToImmediate(Def->getOperand(1).getImm());
        } else if ((RI.isPhysicalRegister(Src1->getReg()) &&
            RI.isSGPRClass(RI.getPhysRegClass(Src1->getReg()))) ||
            (RI.isVirtualRegister(Src1->getReg()) &&
            RI.isSGPRClass(MRI->getRegClass(Src1->getReg()))))
          return false;
          // VGPR is okay as Src1 - fallthrough
      }

      const int64_t Imm = ImmOp->getImm();

      // FIXME: This would be a lot easier if we could return a new instruction
      // instead of having to modify in place.

      // Remove these first since they are at the end.
      UseMI.RemoveOperand(
          AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::omod));
      UseMI.RemoveOperand(
          AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::clamp));

      if (Opc == AMDGPU::V_MAC_F32_e64 ||
          Opc == AMDGPU::V_MAC_F16_e64)
        UseMI.untieRegOperand(
            AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2));

      // ChangingToImmediate adds Src2 back to the instruction.
      Src2->ChangeToImmediate(Imm);

      // These come before src2.
      removeModOperands(UseMI);
      UseMI.setDesc(get(IsF32 ? AMDGPU::V_MADAK_F32 : AMDGPU::V_MADAK_F16));

      bool DeleteDef = MRI->hasOneNonDBGUse(Reg);
      if (DeleteDef)
        DefMI.eraseFromParent();

      return true;
    }
  }

  return false;
}

static bool offsetsDoNotOverlap(int WidthA, int OffsetA,
                                int WidthB, int OffsetB) {
  int LowOffset = OffsetA < OffsetB ? OffsetA : OffsetB;
  int HighOffset = OffsetA < OffsetB ? OffsetB : OffsetA;
  int LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
  return LowOffset + LowWidth <= HighOffset;
}

bool SIInstrInfo::checkInstOffsetsDoNotOverlap(MachineInstr &MIa,
                                               MachineInstr &MIb) const {
  MachineOperand *BaseOp0, *BaseOp1;
  int64_t Offset0, Offset1;

  if (getMemOperandWithOffset(MIa, BaseOp0, Offset0, &RI) &&
      getMemOperandWithOffset(MIb, BaseOp1, Offset1, &RI)) {
    if (!BaseOp0->isIdenticalTo(*BaseOp1))
      return false;

    if (!MIa.hasOneMemOperand() || !MIb.hasOneMemOperand()) {
      // FIXME: Handle ds_read2 / ds_write2.
      return false;
    }
    unsigned Width0 = (*MIa.memoperands_begin())->getSize();
    unsigned Width1 = (*MIb.memoperands_begin())->getSize();
    if (offsetsDoNotOverlap(Width0, Offset0, Width1, Offset1)) {
      return true;
    }
  }

  return false;
}

bool SIInstrInfo::areMemAccessesTriviallyDisjoint(MachineInstr &MIa,
                                                  MachineInstr &MIb,
                                                  AliasAnalysis *AA) const {
  assert((MIa.mayLoad() || MIa.mayStore()) &&
         "MIa must load from or modify a memory location");
  assert((MIb.mayLoad() || MIb.mayStore()) &&
         "MIb must load from or modify a memory location");

  if (MIa.hasUnmodeledSideEffects() || MIb.hasUnmodeledSideEffects())
    return false;

  // XXX - Can we relax this between address spaces?
  if (MIa.hasOrderedMemoryRef() || MIb.hasOrderedMemoryRef())
    return false;

  if (AA && MIa.hasOneMemOperand() && MIb.hasOneMemOperand()) {
    const MachineMemOperand *MMOa = *MIa.memoperands_begin();
    const MachineMemOperand *MMOb = *MIb.memoperands_begin();
    if (MMOa->getValue() && MMOb->getValue()) {
      MemoryLocation LocA(MMOa->getValue(), MMOa->getSize(), MMOa->getAAInfo());
      MemoryLocation LocB(MMOb->getValue(), MMOb->getSize(), MMOb->getAAInfo());
      if (!AA->alias(LocA, LocB))
        return true;
    }
  }

  // TODO: Should we check the address space from the MachineMemOperand? That
  // would allow us to distinguish objects we know don't alias based on the
  // underlying address space, even if it was lowered to a different one,
  // e.g. private accesses lowered to use MUBUF instructions on a scratch
  // buffer.
  if (isDS(MIa)) {
    if (isDS(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    return !isFLAT(MIb) || isSegmentSpecificFLAT(MIb);
  }

  if (isMUBUF(MIa) || isMTBUF(MIa)) {
    if (isMUBUF(MIb) || isMTBUF(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    return !isFLAT(MIb) && !isSMRD(MIb);
  }

  if (isSMRD(MIa)) {
    if (isSMRD(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    return !isFLAT(MIb) && !isMUBUF(MIa) && !isMTBUF(MIa);
  }

  if (isFLAT(MIa)) {
    if (isFLAT(MIb))
      return checkInstOffsetsDoNotOverlap(MIa, MIb);

    return false;
  }

  return false;
}

static int64_t getFoldableImm(const MachineOperand* MO) {
  if (!MO->isReg())
    return false;
  const MachineFunction *MF = MO->getParent()->getParent()->getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  auto Def = MRI.getUniqueVRegDef(MO->getReg());
  if (Def && Def->getOpcode() == AMDGPU::V_MOV_B32_e32 &&
      Def->getOperand(1).isImm())
    return Def->getOperand(1).getImm();
  return AMDGPU::NoRegister;
}

MachineInstr *SIInstrInfo::convertToThreeAddress(MachineFunction::iterator &MBB,
                                                 MachineInstr &MI,
                                                 LiveVariables *LV) const {
  unsigned Opc = MI.getOpcode();
  bool IsF16 = false;
  bool IsFMA = Opc == AMDGPU::V_FMAC_F32_e32 || Opc == AMDGPU::V_FMAC_F32_e64;

  switch (Opc) {
  default:
    return nullptr;
  case AMDGPU::V_MAC_F16_e64:
    IsF16 = true;
    LLVM_FALLTHROUGH;
  case AMDGPU::V_MAC_F32_e64:
  case AMDGPU::V_FMAC_F32_e64:
    break;
  case AMDGPU::V_MAC_F16_e32:
    IsF16 = true;
    LLVM_FALLTHROUGH;
  case AMDGPU::V_MAC_F32_e32:
  case AMDGPU::V_FMAC_F32_e32: {
    int Src0Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                             AMDGPU::OpName::src0);
    const MachineOperand *Src0 = &MI.getOperand(Src0Idx);
    if (!Src0->isReg() && !Src0->isImm())
      return nullptr;

    if (Src0->isImm() && !isInlineConstant(MI, Src0Idx, *Src0))
      return nullptr;

    break;
  }
  }

  const MachineOperand *Dst = getNamedOperand(MI, AMDGPU::OpName::vdst);
  const MachineOperand *Src0 = getNamedOperand(MI, AMDGPU::OpName::src0);
  const MachineOperand *Src0Mods =
    getNamedOperand(MI, AMDGPU::OpName::src0_modifiers);
  const MachineOperand *Src1 = getNamedOperand(MI, AMDGPU::OpName::src1);
  const MachineOperand *Src1Mods =
    getNamedOperand(MI, AMDGPU::OpName::src1_modifiers);
  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);
  const MachineOperand *Clamp = getNamedOperand(MI, AMDGPU::OpName::clamp);
  const MachineOperand *Omod = getNamedOperand(MI, AMDGPU::OpName::omod);

  if (!IsFMA && !Src0Mods && !Src1Mods && !Clamp && !Omod &&
      // If we have an SGPR input, we will violate the constant bus restriction.
      (!Src0->isReg() || !RI.isSGPRReg(MBB->getParent()->getRegInfo(), Src0->getReg()))) {
    if (auto Imm = getFoldableImm(Src2)) {
      return BuildMI(*MBB, MI, MI.getDebugLoc(),
                     get(IsF16 ? AMDGPU::V_MADAK_F16 : AMDGPU::V_MADAK_F32))
               .add(*Dst)
               .add(*Src0)
               .add(*Src1)
               .addImm(Imm);
    }
    if (auto Imm = getFoldableImm(Src1)) {
      return BuildMI(*MBB, MI, MI.getDebugLoc(),
                     get(IsF16 ? AMDGPU::V_MADMK_F16 : AMDGPU::V_MADMK_F32))
               .add(*Dst)
               .add(*Src0)
               .addImm(Imm)
               .add(*Src2);
    }
    if (auto Imm = getFoldableImm(Src0)) {
      if (isOperandLegal(MI, AMDGPU::getNamedOperandIdx(AMDGPU::V_MADMK_F32,
                           AMDGPU::OpName::src0), Src1))
        return BuildMI(*MBB, MI, MI.getDebugLoc(),
                       get(IsF16 ? AMDGPU::V_MADMK_F16 : AMDGPU::V_MADMK_F32))
                 .add(*Dst)
                 .add(*Src1)
                 .addImm(Imm)
                 .add(*Src2);
    }
  }

  assert((!IsFMA || !IsF16) && "fmac only expected with f32");
  unsigned NewOpc = IsFMA ? AMDGPU::V_FMA_F32 :
    (IsF16 ? AMDGPU::V_MAD_F16 : AMDGPU::V_MAD_F32);
  return BuildMI(*MBB, MI, MI.getDebugLoc(), get(NewOpc))
      .add(*Dst)
      .addImm(Src0Mods ? Src0Mods->getImm() : 0)
      .add(*Src0)
      .addImm(Src1Mods ? Src1Mods->getImm() : 0)
      .add(*Src1)
      .addImm(0) // Src mods
      .add(*Src2)
      .addImm(Clamp ? Clamp->getImm() : 0)
      .addImm(Omod ? Omod->getImm() : 0);
}

// It's not generally safe to move VALU instructions across these since it will
// start using the register as a base index rather than directly.
// XXX - Why isn't hasSideEffects sufficient for these?
static bool changesVGPRIndexingMode(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::S_SET_GPR_IDX_ON:
  case AMDGPU::S_SET_GPR_IDX_MODE:
  case AMDGPU::S_SET_GPR_IDX_OFF:
    return true;
  default:
    return false;
  }
}

bool SIInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                       const MachineBasicBlock *MBB,
                                       const MachineFunction &MF) const {
  // XXX - Do we want the SP check in the base implementation?

  // Target-independent instructions do not have an implicit-use of EXEC, even
  // when they operate on VGPRs. Treating EXEC modifications as scheduling
  // boundaries prevents incorrect movements of such instructions.
  return TargetInstrInfo::isSchedulingBoundary(MI, MBB, MF) ||
         MI.modifiesRegister(AMDGPU::EXEC, &RI) ||
         MI.getOpcode() == AMDGPU::S_SETREG_IMM32_B32 ||
         MI.getOpcode() == AMDGPU::S_SETREG_B32 ||
         changesVGPRIndexingMode(MI);
}

bool SIInstrInfo::isAlwaysGDS(uint16_t Opcode) const {
  return Opcode == AMDGPU::DS_ORDERED_COUNT ||
         Opcode == AMDGPU::DS_GWS_INIT ||
         Opcode == AMDGPU::DS_GWS_SEMA_V ||
         Opcode == AMDGPU::DS_GWS_SEMA_BR ||
         Opcode == AMDGPU::DS_GWS_SEMA_P ||
         Opcode == AMDGPU::DS_GWS_SEMA_RELEASE_ALL ||
         Opcode == AMDGPU::DS_GWS_BARRIER;
}

bool SIInstrInfo::hasUnwantedEffectsWhenEXECEmpty(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  if (MI.mayStore() && isSMRD(MI))
    return true; // scalar store or atomic

  // These instructions cause shader I/O that may cause hardware lockups
  // when executed with an empty EXEC mask.
  //
  // Note: exp with VM = DONE = 0 is automatically skipped by hardware when
  //       EXEC = 0, but checking for that case here seems not worth it
  //       given the typical code patterns.
  if (Opcode == AMDGPU::S_SENDMSG || Opcode == AMDGPU::S_SENDMSGHALT ||
      Opcode == AMDGPU::EXP || Opcode == AMDGPU::EXP_DONE ||
      Opcode == AMDGPU::DS_ORDERED_COUNT)
    return true;

  if (MI.isInlineAsm())
    return true; // conservative assumption

  // These are like SALU instructions in terms of effects, so it's questionable
  // whether we should return true for those.
  //
  // However, executing them with EXEC = 0 causes them to operate on undefined
  // data, which we avoid by returning true here.
  if (Opcode == AMDGPU::V_READFIRSTLANE_B32 || Opcode == AMDGPU::V_READLANE_B32)
    return true;

  return false;
}

bool SIInstrInfo::isInlineConstant(const APInt &Imm) const {
  switch (Imm.getBitWidth()) {
  case 32:
    return AMDGPU::isInlinableLiteral32(Imm.getSExtValue(),
                                        ST.hasInv2PiInlineImm());
  case 64:
    return AMDGPU::isInlinableLiteral64(Imm.getSExtValue(),
                                        ST.hasInv2PiInlineImm());
  case 16:
    return ST.has16BitInsts() &&
           AMDGPU::isInlinableLiteral16(Imm.getSExtValue(),
                                        ST.hasInv2PiInlineImm());
  default:
    llvm_unreachable("invalid bitwidth");
  }
}

bool SIInstrInfo::isInlineConstant(const MachineOperand &MO,
                                   uint8_t OperandType) const {
  if (!MO.isImm() ||
      OperandType < AMDGPU::OPERAND_SRC_FIRST ||
      OperandType > AMDGPU::OPERAND_SRC_LAST)
    return false;

  // MachineOperand provides no way to tell the true operand size, since it only
  // records a 64-bit value. We need to know the size to determine if a 32-bit
  // floating point immediate bit pattern is legal for an integer immediate. It
  // would be for any 32-bit integer operand, but would not be for a 64-bit one.

  int64_t Imm = MO.getImm();
  switch (OperandType) {
  case AMDGPU::OPERAND_REG_IMM_INT32:
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32: {
    int32_t Trunc = static_cast<int32_t>(Imm);
    return AMDGPU::isInlinableLiteral32(Trunc, ST.hasInv2PiInlineImm());
  }
  case AMDGPU::OPERAND_REG_IMM_INT64:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_INT64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
    return AMDGPU::isInlinableLiteral64(MO.getImm(),
                                        ST.hasInv2PiInlineImm());
  case AMDGPU::OPERAND_REG_IMM_INT16:
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_FP16: {
    if (isInt<16>(Imm) || isUInt<16>(Imm)) {
      // A few special case instructions have 16-bit operands on subtargets
      // where 16-bit instructions are not legal.
      // TODO: Do the 32-bit immediates work? We shouldn't really need to handle
      // constants in these cases
      int16_t Trunc = static_cast<int16_t>(Imm);
      return ST.has16BitInsts() &&
             AMDGPU::isInlinableLiteral16(Trunc, ST.hasInv2PiInlineImm());
    }

    return false;
  }
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16: {
    if (isUInt<16>(Imm)) {
      int16_t Trunc = static_cast<int16_t>(Imm);
      return ST.has16BitInsts() &&
             AMDGPU::isInlinableLiteral16(Trunc, ST.hasInv2PiInlineImm());
    }
    if (!(Imm & 0xffff)) {
      return ST.has16BitInsts() &&
             AMDGPU::isInlinableLiteral16(Imm >> 16, ST.hasInv2PiInlineImm());
    }
    uint32_t Trunc = static_cast<uint32_t>(Imm);
    return  AMDGPU::isInlinableLiteralV216(Trunc, ST.hasInv2PiInlineImm());
  }
  default:
    llvm_unreachable("invalid bitwidth");
  }
}

bool SIInstrInfo::isLiteralConstantLike(const MachineOperand &MO,
                                        const MCOperandInfo &OpInfo) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    return false;
  case MachineOperand::MO_Immediate:
    return !isInlineConstant(MO, OpInfo);
  case MachineOperand::MO_FrameIndex:
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_MCSymbol:
    return true;
  default:
    llvm_unreachable("unexpected operand type");
  }
}

static bool compareMachineOp(const MachineOperand &Op0,
                             const MachineOperand &Op1) {
  if (Op0.getType() != Op1.getType())
    return false;

  switch (Op0.getType()) {
  case MachineOperand::MO_Register:
    return Op0.getReg() == Op1.getReg();
  case MachineOperand::MO_Immediate:
    return Op0.getImm() == Op1.getImm();
  default:
    llvm_unreachable("Didn't expect to be comparing these operand types");
  }
}

bool SIInstrInfo::isImmOperandLegal(const MachineInstr &MI, unsigned OpNo,
                                    const MachineOperand &MO) const {
  const MCOperandInfo &OpInfo = get(MI.getOpcode()).OpInfo[OpNo];

  assert(MO.isImm() || MO.isTargetIndex() || MO.isFI());

  if (OpInfo.OperandType == MCOI::OPERAND_IMMEDIATE)
    return true;

  if (OpInfo.RegClass < 0)
    return false;

  if (MO.isImm() && isInlineConstant(MO, OpInfo))
    return RI.opCanUseInlineConstant(OpInfo.OperandType);

  return RI.opCanUseLiteralConstant(OpInfo.OperandType);
}

bool SIInstrInfo::hasVALU32BitEncoding(unsigned Opcode) const {
  int Op32 = AMDGPU::getVOPe32(Opcode);
  if (Op32 == -1)
    return false;

  return pseudoToMCOpcode(Op32) != -1;
}

bool SIInstrInfo::hasModifiers(unsigned Opcode) const {
  // The src0_modifier operand is present on all instructions
  // that have modifiers.

  return AMDGPU::getNamedOperandIdx(Opcode,
                                    AMDGPU::OpName::src0_modifiers) != -1;
}

bool SIInstrInfo::hasModifiersSet(const MachineInstr &MI,
                                  unsigned OpName) const {
  const MachineOperand *Mods = getNamedOperand(MI, OpName);
  return Mods && Mods->getImm();
}

bool SIInstrInfo::hasAnyModifiersSet(const MachineInstr &MI) const {
  return hasModifiersSet(MI, AMDGPU::OpName::src0_modifiers) ||
         hasModifiersSet(MI, AMDGPU::OpName::src1_modifiers) ||
         hasModifiersSet(MI, AMDGPU::OpName::src2_modifiers) ||
         hasModifiersSet(MI, AMDGPU::OpName::clamp) ||
         hasModifiersSet(MI, AMDGPU::OpName::omod);
}

bool SIInstrInfo::canShrink(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI) const {
  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);
  // Can't shrink instruction with three operands.
  // FIXME: v_cndmask_b32 has 3 operands and is shrinkable, but we need to add
  // a special case for it.  It can only be shrunk if the third operand
  // is vcc.  We should handle this the same way we handle vopc, by addding
  // a register allocation hint pre-regalloc and then do the shrinking
  // post-regalloc.
  if (Src2) {
    switch (MI.getOpcode()) {
      default: return false;

      case AMDGPU::V_ADDC_U32_e64:
      case AMDGPU::V_SUBB_U32_e64:
      case AMDGPU::V_SUBBREV_U32_e64: {
        const MachineOperand *Src1
          = getNamedOperand(MI, AMDGPU::OpName::src1);
        if (!Src1->isReg() || !RI.isVGPR(MRI, Src1->getReg()))
          return false;
        // Additional verification is needed for sdst/src2.
        return true;
      }
      case AMDGPU::V_MAC_F32_e64:
      case AMDGPU::V_MAC_F16_e64:
      case AMDGPU::V_FMAC_F32_e64:
        if (!Src2->isReg() || !RI.isVGPR(MRI, Src2->getReg()) ||
            hasModifiersSet(MI, AMDGPU::OpName::src2_modifiers))
          return false;
        break;

      case AMDGPU::V_CNDMASK_B32_e64:
        break;
    }
  }

  const MachineOperand *Src1 = getNamedOperand(MI, AMDGPU::OpName::src1);
  if (Src1 && (!Src1->isReg() || !RI.isVGPR(MRI, Src1->getReg()) ||
               hasModifiersSet(MI, AMDGPU::OpName::src1_modifiers)))
    return false;

  // We don't need to check src0, all input types are legal, so just make sure
  // src0 isn't using any modifiers.
  if (hasModifiersSet(MI, AMDGPU::OpName::src0_modifiers))
    return false;

  // Can it be shrunk to a valid 32 bit opcode?
  if (!hasVALU32BitEncoding(MI.getOpcode()))
    return false;

  // Check output modifiers
  return !hasModifiersSet(MI, AMDGPU::OpName::omod) &&
         !hasModifiersSet(MI, AMDGPU::OpName::clamp);
}

// Set VCC operand with all flags from \p Orig, except for setting it as
// implicit.
static void copyFlagsToImplicitVCC(MachineInstr &MI,
                                   const MachineOperand &Orig) {

  for (MachineOperand &Use : MI.implicit_operands()) {
    if (Use.isUse() && Use.getReg() == AMDGPU::VCC) {
      Use.setIsUndef(Orig.isUndef());
      Use.setIsKill(Orig.isKill());
      return;
    }
  }
}

MachineInstr *SIInstrInfo::buildShrunkInst(MachineInstr &MI,
                                           unsigned Op32) const {
  MachineBasicBlock *MBB = MI.getParent();;
  MachineInstrBuilder Inst32 =
    BuildMI(*MBB, MI, MI.getDebugLoc(), get(Op32));

  // Add the dst operand if the 32-bit encoding also has an explicit $vdst.
  // For VOPC instructions, this is replaced by an implicit def of vcc.
  int Op32DstIdx = AMDGPU::getNamedOperandIdx(Op32, AMDGPU::OpName::vdst);
  if (Op32DstIdx != -1) {
    // dst
    Inst32.add(MI.getOperand(0));
  } else {
    assert(MI.getOperand(0).getReg() == AMDGPU::VCC &&
           "Unexpected case");
  }

  Inst32.add(*getNamedOperand(MI, AMDGPU::OpName::src0));

  const MachineOperand *Src1 = getNamedOperand(MI, AMDGPU::OpName::src1);
  if (Src1)
    Inst32.add(*Src1);

  const MachineOperand *Src2 = getNamedOperand(MI, AMDGPU::OpName::src2);

  if (Src2) {
    int Op32Src2Idx = AMDGPU::getNamedOperandIdx(Op32, AMDGPU::OpName::src2);
    if (Op32Src2Idx != -1) {
      Inst32.add(*Src2);
    } else {
      // In the case of V_CNDMASK_B32_e32, the explicit operand src2 is
      // replaced with an implicit read of vcc. This was already added
      // during the initial BuildMI, so find it to preserve the flags.
      copyFlagsToImplicitVCC(*Inst32, *Src2);
    }
  }

  return Inst32;
}

bool SIInstrInfo::usesConstantBus(const MachineRegisterInfo &MRI,
                                  const MachineOperand &MO,
                                  const MCOperandInfo &OpInfo) const {
  // Literal constants use the constant bus.
  //if (isLiteralConstantLike(MO, OpInfo))
  // return true;
  if (MO.isImm())
    return !isInlineConstant(MO, OpInfo);

  if (!MO.isReg())
    return true; // Misc other operands like FrameIndex

  if (!MO.isUse())
    return false;

  if (TargetRegisterInfo::isVirtualRegister(MO.getReg()))
    return RI.isSGPRClass(MRI.getRegClass(MO.getReg()));

  // FLAT_SCR is just an SGPR pair.
  if (!MO.isImplicit() && (MO.getReg() == AMDGPU::FLAT_SCR))
    return true;

  // EXEC register uses the constant bus.
  if (!MO.isImplicit() && MO.getReg() == AMDGPU::EXEC)
    return true;

  // SGPRs use the constant bus
  return (MO.getReg() == AMDGPU::VCC || MO.getReg() == AMDGPU::M0 ||
          (!MO.isImplicit() &&
           (AMDGPU::SGPR_32RegClass.contains(MO.getReg()) ||
            AMDGPU::SGPR_64RegClass.contains(MO.getReg()))));
}

static unsigned findImplicitSGPRRead(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.implicit_operands()) {
    // We only care about reads.
    if (MO.isDef())
      continue;

    switch (MO.getReg()) {
    case AMDGPU::VCC:
    case AMDGPU::M0:
    case AMDGPU::FLAT_SCR:
      return MO.getReg();

    default:
      break;
    }
  }

  return AMDGPU::NoRegister;
}

static bool shouldReadExec(const MachineInstr &MI) {
  if (SIInstrInfo::isVALU(MI)) {
    switch (MI.getOpcode()) {
    case AMDGPU::V_READLANE_B32:
    case AMDGPU::V_READLANE_B32_si:
    case AMDGPU::V_READLANE_B32_vi:
    case AMDGPU::V_WRITELANE_B32:
    case AMDGPU::V_WRITELANE_B32_si:
    case AMDGPU::V_WRITELANE_B32_vi:
      return false;
    }

    return true;
  }

  if (SIInstrInfo::isGenericOpcode(MI.getOpcode()) ||
      SIInstrInfo::isSALU(MI) ||
      SIInstrInfo::isSMRD(MI))
    return false;

  return true;
}

static bool isSubRegOf(const SIRegisterInfo &TRI,
                       const MachineOperand &SuperVec,
                       const MachineOperand &SubReg) {
  if (TargetRegisterInfo::isPhysicalRegister(SubReg.getReg()))
    return TRI.isSubRegister(SuperVec.getReg(), SubReg.getReg());

  return SubReg.getSubReg() != AMDGPU::NoSubRegister &&
         SubReg.getReg() == SuperVec.getReg();
}

bool SIInstrInfo::verifyInstruction(const MachineInstr &MI,
                                    StringRef &ErrInfo) const {
  uint16_t Opcode = MI.getOpcode();
  if (SIInstrInfo::isGenericOpcode(MI.getOpcode()))
    return true;

  const MachineFunction *MF = MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();

  int Src0Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src0);
  int Src1Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src1);
  int Src2Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::src2);

  // Make sure the number of operands is correct.
  const MCInstrDesc &Desc = get(Opcode);
  if (!Desc.isVariadic() &&
      Desc.getNumOperands() != MI.getNumExplicitOperands()) {
    ErrInfo = "Instruction has wrong number of operands.";
    return false;
  }

  if (MI.isInlineAsm()) {
    // Verify register classes for inlineasm constraints.
    for (unsigned I = InlineAsm::MIOp_FirstOperand, E = MI.getNumOperands();
         I != E; ++I) {
      const TargetRegisterClass *RC = MI.getRegClassConstraint(I, this, &RI);
      if (!RC)
        continue;

      const MachineOperand &Op = MI.getOperand(I);
      if (!Op.isReg())
        continue;

      unsigned Reg = Op.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(Reg) && !RC->contains(Reg)) {
        ErrInfo = "inlineasm operand has incorrect register class.";
        return false;
      }
    }

    return true;
  }

  // Make sure the register classes are correct.
  for (int i = 0, e = Desc.getNumOperands(); i != e; ++i) {
    if (MI.getOperand(i).isFPImm()) {
      ErrInfo = "FPImm Machine Operands are not supported. ISel should bitcast "
                "all fp values to integers.";
      return false;
    }

    int RegClass = Desc.OpInfo[i].RegClass;

    switch (Desc.OpInfo[i].OperandType) {
    case MCOI::OPERAND_REGISTER:
      if (MI.getOperand(i).isImm()) {
        ErrInfo = "Illegal immediate value for operand.";
        return false;
      }
      break;
    case AMDGPU::OPERAND_REG_IMM_INT32:
    case AMDGPU::OPERAND_REG_IMM_FP32:
      break;
    case AMDGPU::OPERAND_REG_INLINE_C_INT32:
    case AMDGPU::OPERAND_REG_INLINE_C_FP32:
    case AMDGPU::OPERAND_REG_INLINE_C_INT64:
    case AMDGPU::OPERAND_REG_INLINE_C_FP64:
    case AMDGPU::OPERAND_REG_INLINE_C_INT16:
    case AMDGPU::OPERAND_REG_INLINE_C_FP16: {
      const MachineOperand &MO = MI.getOperand(i);
      if (!MO.isReg() && (!MO.isImm() || !isInlineConstant(MI, i))) {
        ErrInfo = "Illegal immediate value for operand.";
        return false;
      }
      break;
    }
    case MCOI::OPERAND_IMMEDIATE:
    case AMDGPU::OPERAND_KIMM32:
      // Check if this operand is an immediate.
      // FrameIndex operands will be replaced by immediates, so they are
      // allowed.
      if (!MI.getOperand(i).isImm() && !MI.getOperand(i).isFI()) {
        ErrInfo = "Expected immediate, but got non-immediate";
        return false;
      }
      LLVM_FALLTHROUGH;
    default:
      continue;
    }

    if (!MI.getOperand(i).isReg())
      continue;

    if (RegClass != -1) {
      unsigned Reg = MI.getOperand(i).getReg();
      if (Reg == AMDGPU::NoRegister ||
          TargetRegisterInfo::isVirtualRegister(Reg))
        continue;

      const TargetRegisterClass *RC = RI.getRegClass(RegClass);
      if (!RC->contains(Reg)) {
        ErrInfo = "Operand has incorrect register class.";
        return false;
      }
    }
  }

  // Verify SDWA
  if (isSDWA(MI)) {
    if (!ST.hasSDWA()) {
      ErrInfo = "SDWA is not supported on this target";
      return false;
    }

    int DstIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdst);

    const int OpIndicies[] = { DstIdx, Src0Idx, Src1Idx, Src2Idx };

    for (int OpIdx: OpIndicies) {
      if (OpIdx == -1)
        continue;
      const MachineOperand &MO = MI.getOperand(OpIdx);

      if (!ST.hasSDWAScalar()) {
        // Only VGPRS on VI
        if (!MO.isReg() || !RI.hasVGPRs(RI.getRegClassForReg(MRI, MO.getReg()))) {
          ErrInfo = "Only VGPRs allowed as operands in SDWA instructions on VI";
          return false;
        }
      } else {
        // No immediates on GFX9
        if (!MO.isReg()) {
          ErrInfo = "Only reg allowed as operands in SDWA instructions on GFX9";
          return false;
        }
      }
    }

    if (!ST.hasSDWAOmod()) {
      // No omod allowed on VI
      const MachineOperand *OMod = getNamedOperand(MI, AMDGPU::OpName::omod);
      if (OMod != nullptr &&
        (!OMod->isImm() || OMod->getImm() != 0)) {
        ErrInfo = "OMod not allowed in SDWA instructions on VI";
        return false;
      }
    }

    uint16_t BasicOpcode = AMDGPU::getBasicFromSDWAOp(Opcode);
    if (isVOPC(BasicOpcode)) {
      if (!ST.hasSDWASdst() && DstIdx != -1) {
        // Only vcc allowed as dst on VI for VOPC
        const MachineOperand &Dst = MI.getOperand(DstIdx);
        if (!Dst.isReg() || Dst.getReg() != AMDGPU::VCC) {
          ErrInfo = "Only VCC allowed as dst in SDWA instructions on VI";
          return false;
        }
      } else if (!ST.hasSDWAOutModsVOPC()) {
        // No clamp allowed on GFX9 for VOPC
        const MachineOperand *Clamp = getNamedOperand(MI, AMDGPU::OpName::clamp);
        if (Clamp && (!Clamp->isImm() || Clamp->getImm() != 0)) {
          ErrInfo = "Clamp not allowed in VOPC SDWA instructions on VI";
          return false;
        }

        // No omod allowed on GFX9 for VOPC
        const MachineOperand *OMod = getNamedOperand(MI, AMDGPU::OpName::omod);
        if (OMod && (!OMod->isImm() || OMod->getImm() != 0)) {
          ErrInfo = "OMod not allowed in VOPC SDWA instructions on VI";
          return false;
        }
      }
    }

    const MachineOperand *DstUnused = getNamedOperand(MI, AMDGPU::OpName::dst_unused);
    if (DstUnused && DstUnused->isImm() &&
        DstUnused->getImm() == AMDGPU::SDWA::UNUSED_PRESERVE) {
      const MachineOperand &Dst = MI.getOperand(DstIdx);
      if (!Dst.isReg() || !Dst.isTied()) {
        ErrInfo = "Dst register should have tied register";
        return false;
      }

      const MachineOperand &TiedMO =
          MI.getOperand(MI.findTiedOperandIdx(DstIdx));
      if (!TiedMO.isReg() || !TiedMO.isImplicit() || !TiedMO.isUse()) {
        ErrInfo =
            "Dst register should be tied to implicit use of preserved register";
        return false;
      } else if (TargetRegisterInfo::isPhysicalRegister(TiedMO.getReg()) &&
                 Dst.getReg() != TiedMO.getReg()) {
        ErrInfo = "Dst register should use same physical register as preserved";
        return false;
      }
    }
  }

  // Verify MIMG
  if (isMIMG(MI.getOpcode()) && !MI.mayStore()) {
    // Ensure that the return type used is large enough for all the options
    // being used TFE/LWE require an extra result register.
    const MachineOperand *DMask = getNamedOperand(MI, AMDGPU::OpName::dmask);
    if (DMask) {
      uint64_t DMaskImm = DMask->getImm();
      uint32_t RegCount =
          isGather4(MI.getOpcode()) ? 4 : countPopulation(DMaskImm);
      const MachineOperand *TFE = getNamedOperand(MI, AMDGPU::OpName::tfe);
      const MachineOperand *LWE = getNamedOperand(MI, AMDGPU::OpName::lwe);
      const MachineOperand *D16 = getNamedOperand(MI, AMDGPU::OpName::d16);

      // Adjust for packed 16 bit values
      if (D16 && D16->getImm() && !ST.hasUnpackedD16VMem())
        RegCount >>= 1;

      // Adjust if using LWE or TFE
      if ((LWE && LWE->getImm()) || (TFE && TFE->getImm()))
        RegCount += 1;

      const uint32_t DstIdx =
          AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vdata);
      const MachineOperand &Dst = MI.getOperand(DstIdx);
      if (Dst.isReg()) {
        const TargetRegisterClass *DstRC = getOpRegClass(MI, DstIdx);
        uint32_t DstSize = RI.getRegSizeInBits(*DstRC) / 32;
        if (RegCount > DstSize) {
          ErrInfo = "MIMG instruction returns too many registers for dst "
                    "register class";
          return false;
        }
      }
    }
  }

  // Verify VOP*. Ignore multiple sgpr operands on writelane.
  if (Desc.getOpcode() != AMDGPU::V_WRITELANE_B32
      && (isVOP1(MI) || isVOP2(MI) || isVOP3(MI) || isVOPC(MI) || isSDWA(MI))) {
    // Only look at the true operands. Only a real operand can use the constant
    // bus, and we don't want to check pseudo-operands like the source modifier
    // flags.
    const int OpIndices[] = { Src0Idx, Src1Idx, Src2Idx };

    unsigned ConstantBusCount = 0;
    unsigned LiteralCount = 0;

    if (AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::imm) != -1)
      ++ConstantBusCount;

    unsigned SGPRUsed = findImplicitSGPRRead(MI);
    if (SGPRUsed != AMDGPU::NoRegister)
      ++ConstantBusCount;

    for (int OpIdx : OpIndices) {
      if (OpIdx == -1)
        break;
      const MachineOperand &MO = MI.getOperand(OpIdx);
      if (usesConstantBus(MRI, MO, MI.getDesc().OpInfo[OpIdx])) {
        if (MO.isReg()) {
          if (MO.getReg() != SGPRUsed)
            ++ConstantBusCount;
          SGPRUsed = MO.getReg();
        } else {
          ++ConstantBusCount;
          ++LiteralCount;
        }
      }
    }
    if (ConstantBusCount > 1) {
      ErrInfo = "VOP* instruction uses the constant bus more than once";
      return false;
    }

    if (isVOP3(MI) && LiteralCount) {
      ErrInfo = "VOP3 instruction uses literal";
      return false;
    }
  }

  // Verify misc. restrictions on specific instructions.
  if (Desc.getOpcode() == AMDGPU::V_DIV_SCALE_F32 ||
      Desc.getOpcode() == AMDGPU::V_DIV_SCALE_F64) {
    const MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const MachineOperand &Src1 = MI.getOperand(Src1Idx);
    const MachineOperand &Src2 = MI.getOperand(Src2Idx);
    if (Src0.isReg() && Src1.isReg() && Src2.isReg()) {
      if (!compareMachineOp(Src0, Src1) &&
          !compareMachineOp(Src0, Src2)) {
        ErrInfo = "v_div_scale_{f32|f64} require src0 = src1 or src2";
        return false;
      }
    }
  }

  if (isSOPK(MI)) {
    int64_t Imm = getNamedOperand(MI, AMDGPU::OpName::simm16)->getImm();
    if (sopkIsZext(MI)) {
      if (!isUInt<16>(Imm)) {
        ErrInfo = "invalid immediate for SOPK instruction";
        return false;
      }
    } else {
      if (!isInt<16>(Imm)) {
        ErrInfo = "invalid immediate for SOPK instruction";
        return false;
      }
    }
  }

  if (Desc.getOpcode() == AMDGPU::V_MOVRELS_B32_e32 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELS_B32_e64 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e32 ||
      Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e64) {
    const bool IsDst = Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e32 ||
                       Desc.getOpcode() == AMDGPU::V_MOVRELD_B32_e64;

    const unsigned StaticNumOps = Desc.getNumOperands() +
      Desc.getNumImplicitUses();
    const unsigned NumImplicitOps = IsDst ? 2 : 1;

    // Allow additional implicit operands. This allows a fixup done by the post
    // RA scheduler where the main implicit operand is killed and implicit-defs
    // are added for sub-registers that remain live after this instruction.
    if (MI.getNumOperands() < StaticNumOps + NumImplicitOps) {
      ErrInfo = "missing implicit register operands";
      return false;
    }

    const MachineOperand *Dst = getNamedOperand(MI, AMDGPU::OpName::vdst);
    if (IsDst) {
      if (!Dst->isUse()) {
        ErrInfo = "v_movreld_b32 vdst should be a use operand";
        return false;
      }

      unsigned UseOpIdx;
      if (!MI.isRegTiedToUseOperand(StaticNumOps, &UseOpIdx) ||
          UseOpIdx != StaticNumOps + 1) {
        ErrInfo = "movrel implicit operands should be tied";
        return false;
      }
    }

    const MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const MachineOperand &ImpUse
      = MI.getOperand(StaticNumOps + NumImplicitOps - 1);
    if (!ImpUse.isReg() || !ImpUse.isUse() ||
        !isSubRegOf(RI, ImpUse, IsDst ? *Dst : Src0)) {
      ErrInfo = "src0 should be subreg of implicit vector use";
      return false;
    }
  }

  // Make sure we aren't losing exec uses in the td files. This mostly requires
  // being careful when using let Uses to try to add other use registers.
  if (shouldReadExec(MI)) {
    if (!MI.hasRegisterImplicitUseOperand(AMDGPU::EXEC)) {
      ErrInfo = "VALU instruction does not implicitly read exec mask";
      return false;
    }
  }

  if (isSMRD(MI)) {
    if (MI.mayStore()) {
      // The register offset form of scalar stores may only use m0 as the
      // soffset register.
      const MachineOperand *Soff = getNamedOperand(MI, AMDGPU::OpName::soff);
      if (Soff && Soff->getReg() != AMDGPU::M0) {
        ErrInfo = "scalar stores must use m0 as offset register";
        return false;
      }
    }
  }

  if (isFLAT(MI) && !MF->getSubtarget<GCNSubtarget>().hasFlatInstOffsets()) {
    const MachineOperand *Offset = getNamedOperand(MI, AMDGPU::OpName::offset);
    if (Offset->getImm() != 0) {
      ErrInfo = "subtarget does not support offsets in flat instructions";
      return false;
    }
  }

  const MachineOperand *DppCt = getNamedOperand(MI, AMDGPU::OpName::dpp_ctrl);
  if (DppCt) {
    using namespace AMDGPU::DPP;

    unsigned DC = DppCt->getImm();
    if (DC == DppCtrl::DPP_UNUSED1 || DC == DppCtrl::DPP_UNUSED2 ||
        DC == DppCtrl::DPP_UNUSED3 || DC > DppCtrl::DPP_LAST ||
        (DC >= DppCtrl::DPP_UNUSED4_FIRST && DC <= DppCtrl::DPP_UNUSED4_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED5_FIRST && DC <= DppCtrl::DPP_UNUSED5_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED6_FIRST && DC <= DppCtrl::DPP_UNUSED6_LAST) ||
        (DC >= DppCtrl::DPP_UNUSED7_FIRST && DC <= DppCtrl::DPP_UNUSED7_LAST)) {
      ErrInfo = "Invalid dpp_ctrl value";
      return false;
    }
  }

  return true;
}

unsigned SIInstrInfo::getVALUOp(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default: return AMDGPU::INSTRUCTION_LIST_END;
  case AMDGPU::REG_SEQUENCE: return AMDGPU::REG_SEQUENCE;
  case AMDGPU::COPY: return AMDGPU::COPY;
  case AMDGPU::PHI: return AMDGPU::PHI;
  case AMDGPU::INSERT_SUBREG: return AMDGPU::INSERT_SUBREG;
  case AMDGPU::WQM: return AMDGPU::WQM;
  case AMDGPU::WWM: return AMDGPU::WWM;
  case AMDGPU::S_MOV_B32:
    return MI.getOperand(1).isReg() ?
           AMDGPU::COPY : AMDGPU::V_MOV_B32_e32;
  case AMDGPU::S_ADD_I32:
    return ST.hasAddNoCarry() ? AMDGPU::V_ADD_U32_e64 : AMDGPU::V_ADD_I32_e32;
  case AMDGPU::S_ADDC_U32:
    return AMDGPU::V_ADDC_U32_e32;
  case AMDGPU::S_SUB_I32:
    return ST.hasAddNoCarry() ? AMDGPU::V_SUB_U32_e64 : AMDGPU::V_SUB_I32_e32;
    // FIXME: These are not consistently handled, and selected when the carry is
    // used.
  case AMDGPU::S_ADD_U32:
    return AMDGPU::V_ADD_I32_e32;
  case AMDGPU::S_SUB_U32:
    return AMDGPU::V_SUB_I32_e32;
  case AMDGPU::S_SUBB_U32: return AMDGPU::V_SUBB_U32_e32;
  case AMDGPU::S_MUL_I32: return AMDGPU::V_MUL_LO_I32;
  case AMDGPU::S_AND_B32: return AMDGPU::V_AND_B32_e64;
  case AMDGPU::S_OR_B32: return AMDGPU::V_OR_B32_e64;
  case AMDGPU::S_XOR_B32: return AMDGPU::V_XOR_B32_e64;
  case AMDGPU::S_XNOR_B32:
    return ST.hasDLInsts() ? AMDGPU::V_XNOR_B32_e64 : AMDGPU::INSTRUCTION_LIST_END;
  case AMDGPU::S_MIN_I32: return AMDGPU::V_MIN_I32_e64;
  case AMDGPU::S_MIN_U32: return AMDGPU::V_MIN_U32_e64;
  case AMDGPU::S_MAX_I32: return AMDGPU::V_MAX_I32_e64;
  case AMDGPU::S_MAX_U32: return AMDGPU::V_MAX_U32_e64;
  case AMDGPU::S_ASHR_I32: return AMDGPU::V_ASHR_I32_e32;
  case AMDGPU::S_ASHR_I64: return AMDGPU::V_ASHR_I64;
  case AMDGPU::S_LSHL_B32: return AMDGPU::V_LSHL_B32_e32;
  case AMDGPU::S_LSHL_B64: return AMDGPU::V_LSHL_B64;
  case AMDGPU::S_LSHR_B32: return AMDGPU::V_LSHR_B32_e32;
  case AMDGPU::S_LSHR_B64: return AMDGPU::V_LSHR_B64;
  case AMDGPU::S_SEXT_I32_I8: return AMDGPU::V_BFE_I32;
  case AMDGPU::S_SEXT_I32_I16: return AMDGPU::V_BFE_I32;
  case AMDGPU::S_BFE_U32: return AMDGPU::V_BFE_U32;
  case AMDGPU::S_BFE_I32: return AMDGPU::V_BFE_I32;
  case AMDGPU::S_BFM_B32: return AMDGPU::V_BFM_B32_e64;
  case AMDGPU::S_BREV_B32: return AMDGPU::V_BFREV_B32_e32;
  case AMDGPU::S_NOT_B32: return AMDGPU::V_NOT_B32_e32;
  case AMDGPU::S_NOT_B64: return AMDGPU::V_NOT_B32_e32;
  case AMDGPU::S_CMP_EQ_I32: return AMDGPU::V_CMP_EQ_I32_e32;
  case AMDGPU::S_CMP_LG_I32: return AMDGPU::V_CMP_NE_I32_e32;
  case AMDGPU::S_CMP_GT_I32: return AMDGPU::V_CMP_GT_I32_e32;
  case AMDGPU::S_CMP_GE_I32: return AMDGPU::V_CMP_GE_I32_e32;
  case AMDGPU::S_CMP_LT_I32: return AMDGPU::V_CMP_LT_I32_e32;
  case AMDGPU::S_CMP_LE_I32: return AMDGPU::V_CMP_LE_I32_e32;
  case AMDGPU::S_CMP_EQ_U32: return AMDGPU::V_CMP_EQ_U32_e32;
  case AMDGPU::S_CMP_LG_U32: return AMDGPU::V_CMP_NE_U32_e32;
  case AMDGPU::S_CMP_GT_U32: return AMDGPU::V_CMP_GT_U32_e32;
  case AMDGPU::S_CMP_GE_U32: return AMDGPU::V_CMP_GE_U32_e32;
  case AMDGPU::S_CMP_LT_U32: return AMDGPU::V_CMP_LT_U32_e32;
  case AMDGPU::S_CMP_LE_U32: return AMDGPU::V_CMP_LE_U32_e32;
  case AMDGPU::S_CMP_EQ_U64: return AMDGPU::V_CMP_EQ_U64_e32;
  case AMDGPU::S_CMP_LG_U64: return AMDGPU::V_CMP_NE_U64_e32;
  case AMDGPU::S_BCNT1_I32_B32: return AMDGPU::V_BCNT_U32_B32_e64;
  case AMDGPU::S_FF1_I32_B32: return AMDGPU::V_FFBL_B32_e32;
  case AMDGPU::S_FLBIT_I32_B32: return AMDGPU::V_FFBH_U32_e32;
  case AMDGPU::S_FLBIT_I32: return AMDGPU::V_FFBH_I32_e64;
  case AMDGPU::S_CBRANCH_SCC0: return AMDGPU::S_CBRANCH_VCCZ;
  case AMDGPU::S_CBRANCH_SCC1: return AMDGPU::S_CBRANCH_VCCNZ;
  }
}

const TargetRegisterClass *SIInstrInfo::getOpRegClass(const MachineInstr &MI,
                                                      unsigned OpNo) const {
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  const MCInstrDesc &Desc = get(MI.getOpcode());
  if (MI.isVariadic() || OpNo >= Desc.getNumOperands() ||
      Desc.OpInfo[OpNo].RegClass == -1) {
    unsigned Reg = MI.getOperand(OpNo).getReg();

    if (TargetRegisterInfo::isVirtualRegister(Reg))
      return MRI.getRegClass(Reg);
    return RI.getPhysRegClass(Reg);
  }

  unsigned RCID = Desc.OpInfo[OpNo].RegClass;
  return RI.getRegClass(RCID);
}

bool SIInstrInfo::canReadVGPR(const MachineInstr &MI, unsigned OpNo) const {
  switch (MI.getOpcode()) {
  case AMDGPU::COPY:
  case AMDGPU::REG_SEQUENCE:
  case AMDGPU::PHI:
  case AMDGPU::INSERT_SUBREG:
    return RI.hasVGPRs(getOpRegClass(MI, 0));
  default:
    return RI.hasVGPRs(getOpRegClass(MI, OpNo));
  }
}

void SIInstrInfo::legalizeOpWithMove(MachineInstr &MI, unsigned OpIdx) const {
  MachineBasicBlock::iterator I = MI;
  MachineBasicBlock *MBB = MI.getParent();
  MachineOperand &MO = MI.getOperand(OpIdx);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  unsigned RCID = get(MI.getOpcode()).OpInfo[OpIdx].RegClass;
  const TargetRegisterClass *RC = RI.getRegClass(RCID);
  unsigned Opcode = AMDGPU::V_MOV_B32_e32;
  if (MO.isReg())
    Opcode = AMDGPU::COPY;
  else if (RI.isSGPRClass(RC))
    Opcode = AMDGPU::S_MOV_B32;

  const TargetRegisterClass *VRC = RI.getEquivalentVGPRClass(RC);
  if (RI.getCommonSubClass(&AMDGPU::VReg_64RegClass, VRC))
    VRC = &AMDGPU::VReg_64RegClass;
  else
    VRC = &AMDGPU::VGPR_32RegClass;

  unsigned Reg = MRI.createVirtualRegister(VRC);
  DebugLoc DL = MBB->findDebugLoc(I);
  BuildMI(*MI.getParent(), I, DL, get(Opcode), Reg).add(MO);
  MO.ChangeToRegister(Reg, false);
}

unsigned SIInstrInfo::buildExtractSubReg(MachineBasicBlock::iterator MI,
                                         MachineRegisterInfo &MRI,
                                         MachineOperand &SuperReg,
                                         const TargetRegisterClass *SuperRC,
                                         unsigned SubIdx,
                                         const TargetRegisterClass *SubRC)
                                         const {
  MachineBasicBlock *MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned SubReg = MRI.createVirtualRegister(SubRC);

  if (SuperReg.getSubReg() == AMDGPU::NoSubRegister) {
    BuildMI(*MBB, MI, DL, get(TargetOpcode::COPY), SubReg)
      .addReg(SuperReg.getReg(), 0, SubIdx);
    return SubReg;
  }

  // Just in case the super register is itself a sub-register, copy it to a new
  // value so we don't need to worry about merging its subreg index with the
  // SubIdx passed to this function. The register coalescer should be able to
  // eliminate this extra copy.
  unsigned NewSuperReg = MRI.createVirtualRegister(SuperRC);

  BuildMI(*MBB, MI, DL, get(TargetOpcode::COPY), NewSuperReg)
    .addReg(SuperReg.getReg(), 0, SuperReg.getSubReg());

  BuildMI(*MBB, MI, DL, get(TargetOpcode::COPY), SubReg)
    .addReg(NewSuperReg, 0, SubIdx);

  return SubReg;
}

MachineOperand SIInstrInfo::buildExtractSubRegOrImm(
  MachineBasicBlock::iterator MII,
  MachineRegisterInfo &MRI,
  MachineOperand &Op,
  const TargetRegisterClass *SuperRC,
  unsigned SubIdx,
  const TargetRegisterClass *SubRC) const {
  if (Op.isImm()) {
    if (SubIdx == AMDGPU::sub0)
      return MachineOperand::CreateImm(static_cast<int32_t>(Op.getImm()));
    if (SubIdx == AMDGPU::sub1)
      return MachineOperand::CreateImm(static_cast<int32_t>(Op.getImm() >> 32));

    llvm_unreachable("Unhandled register index for immediate");
  }

  unsigned SubReg = buildExtractSubReg(MII, MRI, Op, SuperRC,
                                       SubIdx, SubRC);
  return MachineOperand::CreateReg(SubReg, false);
}

// Change the order of operands from (0, 1, 2) to (0, 2, 1)
void SIInstrInfo::swapOperands(MachineInstr &Inst) const {
  assert(Inst.getNumExplicitOperands() == 3);
  MachineOperand Op1 = Inst.getOperand(1);
  Inst.RemoveOperand(1);
  Inst.addOperand(Op1);
}

bool SIInstrInfo::isLegalRegOperand(const MachineRegisterInfo &MRI,
                                    const MCOperandInfo &OpInfo,
                                    const MachineOperand &MO) const {
  if (!MO.isReg())
    return false;

  unsigned Reg = MO.getReg();
  const TargetRegisterClass *RC =
    TargetRegisterInfo::isVirtualRegister(Reg) ?
    MRI.getRegClass(Reg) :
    RI.getPhysRegClass(Reg);

  const SIRegisterInfo *TRI =
      static_cast<const SIRegisterInfo*>(MRI.getTargetRegisterInfo());
  RC = TRI->getSubRegClass(RC, MO.getSubReg());

  // In order to be legal, the common sub-class must be equal to the
  // class of the current operand.  For example:
  //
  // v_mov_b32 s0 ; Operand defined as vsrc_b32
  //              ; RI.getCommonSubClass(s0,vsrc_b32) = sgpr ; LEGAL
  //
  // s_sendmsg 0, s0 ; Operand defined as m0reg
  //                 ; RI.getCommonSubClass(s0,m0reg) = m0reg ; NOT LEGAL

  return RI.getCommonSubClass(RC, RI.getRegClass(OpInfo.RegClass)) == RC;
}

bool SIInstrInfo::isLegalVSrcOperand(const MachineRegisterInfo &MRI,
                                     const MCOperandInfo &OpInfo,
                                     const MachineOperand &MO) const {
  if (MO.isReg())
    return isLegalRegOperand(MRI, OpInfo, MO);

  // Handle non-register types that are treated like immediates.
  assert(MO.isImm() || MO.isTargetIndex() || MO.isFI());
  return true;
}

bool SIInstrInfo::isOperandLegal(const MachineInstr &MI, unsigned OpIdx,
                                 const MachineOperand *MO) const {
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  const MCInstrDesc &InstDesc = MI.getDesc();
  const MCOperandInfo &OpInfo = InstDesc.OpInfo[OpIdx];
  const TargetRegisterClass *DefinedRC =
      OpInfo.RegClass != -1 ? RI.getRegClass(OpInfo.RegClass) : nullptr;
  if (!MO)
    MO = &MI.getOperand(OpIdx);

  if (isVALU(MI) && usesConstantBus(MRI, *MO, OpInfo)) {

    RegSubRegPair SGPRUsed;
    if (MO->isReg())
      SGPRUsed = RegSubRegPair(MO->getReg(), MO->getSubReg());

    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      if (i == OpIdx)
        continue;
      const MachineOperand &Op = MI.getOperand(i);
      if (Op.isReg()) {
        if ((Op.getReg() != SGPRUsed.Reg || Op.getSubReg() != SGPRUsed.SubReg) &&
            usesConstantBus(MRI, Op, InstDesc.OpInfo[i])) {
          return false;
        }
      } else if (InstDesc.OpInfo[i].OperandType == AMDGPU::OPERAND_KIMM32) {
        return false;
      }
    }
  }

  if (MO->isReg()) {
    assert(DefinedRC);
    return isLegalRegOperand(MRI, OpInfo, *MO);
  }

  // Handle non-register types that are treated like immediates.
  assert(MO->isImm() || MO->isTargetIndex() || MO->isFI());

  if (!DefinedRC) {
    // This operand expects an immediate.
    return true;
  }

  return isImmOperandLegal(MI, OpIdx, *MO);
}

void SIInstrInfo::legalizeOperandsVOP2(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  const MCInstrDesc &InstrDesc = get(Opc);

  int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
  MachineOperand &Src1 = MI.getOperand(Src1Idx);

  // If there is an implicit SGPR use such as VCC use for v_addc_u32/v_subb_u32
  // we need to only have one constant bus use.
  //
  // Note we do not need to worry about literal constants here. They are
  // disabled for the operand type for instructions because they will always
  // violate the one constant bus use rule.
  bool HasImplicitSGPR = findImplicitSGPRRead(MI) != AMDGPU::NoRegister;
  if (HasImplicitSGPR) {
    int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
    MachineOperand &Src0 = MI.getOperand(Src0Idx);

    if (Src0.isReg() && RI.isSGPRReg(MRI, Src0.getReg()))
      legalizeOpWithMove(MI, Src0Idx);
  }

  // Special case: V_WRITELANE_B32 accepts only immediate or SGPR operands for
  // both the value to write (src0) and lane select (src1).  Fix up non-SGPR
  // src0/src1 with V_READFIRSTLANE.
  if (Opc == AMDGPU::V_WRITELANE_B32) {
    int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
    MachineOperand &Src0 = MI.getOperand(Src0Idx);
    const DebugLoc &DL = MI.getDebugLoc();
    if (Src0.isReg() && RI.isVGPR(MRI, Src0.getReg())) {
      unsigned Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
          .add(Src0);
      Src0.ChangeToRegister(Reg, false);
    }
    if (Src1.isReg() && RI.isVGPR(MRI, Src1.getReg())) {
      unsigned Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      const DebugLoc &DL = MI.getDebugLoc();
      BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
          .add(Src1);
      Src1.ChangeToRegister(Reg, false);
    }
    return;
  }

  // VOP2 src0 instructions support all operand types, so we don't need to check
  // their legality. If src1 is already legal, we don't need to do anything.
  if (isLegalRegOperand(MRI, InstrDesc.OpInfo[Src1Idx], Src1))
    return;

  // Special case: V_READLANE_B32 accepts only immediate or SGPR operands for
  // lane select. Fix up using V_READFIRSTLANE, since we assume that the lane
  // select is uniform.
  if (Opc == AMDGPU::V_READLANE_B32 && Src1.isReg() &&
      RI.isVGPR(MRI, Src1.getReg())) {
    unsigned Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
    const DebugLoc &DL = MI.getDebugLoc();
    BuildMI(*MI.getParent(), MI, DL, get(AMDGPU::V_READFIRSTLANE_B32), Reg)
        .add(Src1);
    Src1.ChangeToRegister(Reg, false);
    return;
  }

  // We do not use commuteInstruction here because it is too aggressive and will
  // commute if it is possible. We only want to commute here if it improves
  // legality. This can be called a fairly large number of times so don't waste
  // compile time pointlessly swapping and checking legality again.
  if (HasImplicitSGPR || !MI.isCommutable()) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
  MachineOperand &Src0 = MI.getOperand(Src0Idx);

  // If src0 can be used as src1, commuting will make the operands legal.
  // Otherwise we have to give up and insert a move.
  //
  // TODO: Other immediate-like operand kinds could be commuted if there was a
  // MachineOperand::ChangeTo* for them.
  if ((!Src1.isImm() && !Src1.isReg()) ||
      !isLegalRegOperand(MRI, InstrDesc.OpInfo[Src1Idx], Src0)) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  int CommutedOpc = commuteOpcode(MI);
  if (CommutedOpc == -1) {
    legalizeOpWithMove(MI, Src1Idx);
    return;
  }

  MI.setDesc(get(CommutedOpc));

  unsigned Src0Reg = Src0.getReg();
  unsigned Src0SubReg = Src0.getSubReg();
  bool Src0Kill = Src0.isKill();

  if (Src1.isImm())
    Src0.ChangeToImmediate(Src1.getImm());
  else if (Src1.isReg()) {
    Src0.ChangeToRegister(Src1.getReg(), false, false, Src1.isKill());
    Src0.setSubReg(Src1.getSubReg());
  } else
    llvm_unreachable("Should only have register or immediate operands");

  Src1.ChangeToRegister(Src0Reg, false, false, Src0Kill);
  Src1.setSubReg(Src0SubReg);
}

// Legalize VOP3 operands. Because all operand types are supported for any
// operand, and since literal constants are not allowed and should never be
// seen, we only need to worry about inserting copies if we use multiple SGPR
// operands.
void SIInstrInfo::legalizeOperandsVOP3(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  int VOP3Idx[3] = {
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0),
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1),
    AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2)
  };

  // Find the one SGPR operand we are allowed to use.
  unsigned SGPRReg = findUsedSGPR(MI, VOP3Idx);

  for (unsigned i = 0; i < 3; ++i) {
    int Idx = VOP3Idx[i];
    if (Idx == -1)
      break;
    MachineOperand &MO = MI.getOperand(Idx);

    // We should never see a VOP3 instruction with an illegal immediate operand.
    if (!MO.isReg())
      continue;

    if (!RI.isSGPRClass(MRI.getRegClass(MO.getReg())))
      continue; // VGPRs are legal

    if (SGPRReg == AMDGPU::NoRegister || SGPRReg == MO.getReg()) {
      SGPRReg = MO.getReg();
      // We can use one SGPR in each VOP3 instruction.
      continue;
    }

    // If we make it this far, then the operand is not legal and we must
    // legalize it.
    legalizeOpWithMove(MI, Idx);
  }
}

unsigned SIInstrInfo::readlaneVGPRToSGPR(unsigned SrcReg, MachineInstr &UseMI,
                                         MachineRegisterInfo &MRI) const {
  const TargetRegisterClass *VRC = MRI.getRegClass(SrcReg);
  const TargetRegisterClass *SRC = RI.getEquivalentSGPRClass(VRC);
  unsigned DstReg = MRI.createVirtualRegister(SRC);
  unsigned SubRegs = RI.getRegSizeInBits(*VRC) / 32;

  if (SubRegs == 1) {
    BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
            get(AMDGPU::V_READFIRSTLANE_B32), DstReg)
        .addReg(SrcReg);
    return DstReg;
  }

  SmallVector<unsigned, 8> SRegs;
  for (unsigned i = 0; i < SubRegs; ++i) {
    unsigned SGPR = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
    BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
            get(AMDGPU::V_READFIRSTLANE_B32), SGPR)
        .addReg(SrcReg, 0, RI.getSubRegFromChannel(i));
    SRegs.push_back(SGPR);
  }

  MachineInstrBuilder MIB =
      BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(),
              get(AMDGPU::REG_SEQUENCE), DstReg);
  for (unsigned i = 0; i < SubRegs; ++i) {
    MIB.addReg(SRegs[i]);
    MIB.addImm(RI.getSubRegFromChannel(i));
  }
  return DstReg;
}

void SIInstrInfo::legalizeOperandsSMRD(MachineRegisterInfo &MRI,
                                       MachineInstr &MI) const {

  // If the pointer is store in VGPRs, then we need to move them to
  // SGPRs using v_readfirstlane.  This is safe because we only select
  // loads with uniform pointers to SMRD instruction so we know the
  // pointer value is uniform.
  MachineOperand *SBase = getNamedOperand(MI, AMDGPU::OpName::sbase);
  if (SBase && !RI.isSGPRClass(MRI.getRegClass(SBase->getReg()))) {
    unsigned SGPR = readlaneVGPRToSGPR(SBase->getReg(), MI, MRI);
    SBase->setReg(SGPR);
  }
  MachineOperand *SOff = getNamedOperand(MI, AMDGPU::OpName::soff);
  if (SOff && !RI.isSGPRClass(MRI.getRegClass(SOff->getReg()))) {
    unsigned SGPR = readlaneVGPRToSGPR(SOff->getReg(), MI, MRI);
    SOff->setReg(SGPR);
  }
}

void SIInstrInfo::legalizeGenericOperand(MachineBasicBlock &InsertMBB,
                                         MachineBasicBlock::iterator I,
                                         const TargetRegisterClass *DstRC,
                                         MachineOperand &Op,
                                         MachineRegisterInfo &MRI,
                                         const DebugLoc &DL) const {
  unsigned OpReg = Op.getReg();
  unsigned OpSubReg = Op.getSubReg();

  const TargetRegisterClass *OpRC = RI.getSubClassWithSubReg(
      RI.getRegClassForReg(MRI, OpReg), OpSubReg);

  // Check if operand is already the correct register class.
  if (DstRC == OpRC)
    return;

  unsigned DstReg = MRI.createVirtualRegister(DstRC);
  MachineInstr *Copy =
      BuildMI(InsertMBB, I, DL, get(AMDGPU::COPY), DstReg).add(Op);

  Op.setReg(DstReg);
  Op.setSubReg(0);

  MachineInstr *Def = MRI.getVRegDef(OpReg);
  if (!Def)
    return;

  // Try to eliminate the copy if it is copying an immediate value.
  if (Def->isMoveImmediate())
    FoldImmediate(*Copy, *Def, OpReg, &MRI);
}

// Emit the actual waterfall loop, executing the wrapped instruction for each
// unique value of \p Rsrc across all lanes. In the best case we execute 1
// iteration, in the worst case we execute 64 (once per lane).
static void
emitLoadSRsrcFromVGPRLoop(const SIInstrInfo &TII, MachineRegisterInfo &MRI,
                          MachineBasicBlock &OrigBB, MachineBasicBlock &LoopBB,
                          const DebugLoc &DL, MachineOperand &Rsrc) {
  MachineBasicBlock::iterator I = LoopBB.begin();

  unsigned VRsrc = Rsrc.getReg();
  unsigned VRsrcUndef = getUndefRegState(Rsrc.isUndef());

  unsigned SaveExec = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  unsigned CondReg0 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  unsigned CondReg1 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  unsigned AndCond = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  unsigned SRsrcSub0 = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned SRsrcSub1 = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned SRsrcSub2 = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned SRsrcSub3 = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned SRsrc = MRI.createVirtualRegister(&AMDGPU::SReg_128RegClass);

  // Beginning of the loop, read the next Rsrc variant.
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), SRsrcSub0)
      .addReg(VRsrc, VRsrcUndef, AMDGPU::sub0);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), SRsrcSub1)
      .addReg(VRsrc, VRsrcUndef, AMDGPU::sub1);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), SRsrcSub2)
      .addReg(VRsrc, VRsrcUndef, AMDGPU::sub2);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_READFIRSTLANE_B32), SRsrcSub3)
      .addReg(VRsrc, VRsrcUndef, AMDGPU::sub3);

  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::REG_SEQUENCE), SRsrc)
      .addReg(SRsrcSub0)
      .addImm(AMDGPU::sub0)
      .addReg(SRsrcSub1)
      .addImm(AMDGPU::sub1)
      .addReg(SRsrcSub2)
      .addImm(AMDGPU::sub2)
      .addReg(SRsrcSub3)
      .addImm(AMDGPU::sub3);

  // Update Rsrc operand to use the SGPR Rsrc.
  Rsrc.setReg(SRsrc);
  Rsrc.setIsKill(true);

  // Identify all lanes with identical Rsrc operands in their VGPRs.
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_CMP_EQ_U64_e64), CondReg0)
      .addReg(SRsrc, 0, AMDGPU::sub0_sub1)
      .addReg(VRsrc, 0, AMDGPU::sub0_sub1);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::V_CMP_EQ_U64_e64), CondReg1)
      .addReg(SRsrc, 0, AMDGPU::sub2_sub3)
      .addReg(VRsrc, 0, AMDGPU::sub2_sub3);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::S_AND_B64), AndCond)
      .addReg(CondReg0)
      .addReg(CondReg1);

  MRI.setSimpleHint(SaveExec, AndCond);

  // Update EXEC to matching lanes, saving original to SaveExec.
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::S_AND_SAVEEXEC_B64), SaveExec)
      .addReg(AndCond, RegState::Kill);

  // The original instruction is here; we insert the terminators after it.
  I = LoopBB.end();

  // Update EXEC, switch all done bits to 0 and all todo bits to 1.
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::S_XOR_B64_term), AMDGPU::EXEC)
      .addReg(AMDGPU::EXEC)
      .addReg(SaveExec);
  BuildMI(LoopBB, I, DL, TII.get(AMDGPU::S_CBRANCH_EXECNZ)).addMBB(&LoopBB);
}

// Build a waterfall loop around \p MI, replacing the VGPR \p Rsrc register
// with SGPRs by iterating over all unique values across all lanes.
static void loadSRsrcFromVGPR(const SIInstrInfo &TII, MachineInstr &MI,
                              MachineOperand &Rsrc, MachineDominatorTree *MDT) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineBasicBlock::iterator I(&MI);
  const DebugLoc &DL = MI.getDebugLoc();

  unsigned SaveExec = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);

  // Save the EXEC mask
  BuildMI(MBB, I, DL, TII.get(AMDGPU::S_MOV_B64), SaveExec)
      .addReg(AMDGPU::EXEC);

  // Killed uses in the instruction we are waterfalling around will be
  // incorrect due to the added control-flow.
  for (auto &MO : MI.uses()) {
    if (MO.isReg() && MO.isUse()) {
      MRI.clearKillFlags(MO.getReg());
    }
  }

  // To insert the loop we need to split the block. Move everything after this
  // point to a new block, and insert a new empty block between the two.
  MachineBasicBlock *LoopBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *RemainderBB = MF.CreateMachineBasicBlock();
  MachineFunction::iterator MBBI(MBB);
  ++MBBI;

  MF.insert(MBBI, LoopBB);
  MF.insert(MBBI, RemainderBB);

  LoopBB->addSuccessor(LoopBB);
  LoopBB->addSuccessor(RemainderBB);

  // Move MI to the LoopBB, and the remainder of the block to RemainderBB.
  MachineBasicBlock::iterator J = I++;
  RemainderBB->transferSuccessorsAndUpdatePHIs(&MBB);
  RemainderBB->splice(RemainderBB->begin(), &MBB, I, MBB.end());
  LoopBB->splice(LoopBB->begin(), &MBB, J);

  MBB.addSuccessor(LoopBB);

  // Update dominators. We know that MBB immediately dominates LoopBB, that
  // LoopBB immediately dominates RemainderBB, and that RemainderBB immediately
  // dominates all of the successors transferred to it from MBB that MBB used
  // to dominate.
  if (MDT) {
    MDT->addNewBlock(LoopBB, &MBB);
    MDT->addNewBlock(RemainderBB, LoopBB);
    for (auto &Succ : RemainderBB->successors()) {
      if (MDT->dominates(&MBB, Succ)) {
        MDT->changeImmediateDominator(Succ, RemainderBB);
      }
    }
  }

  emitLoadSRsrcFromVGPRLoop(TII, MRI, MBB, *LoopBB, DL, Rsrc);

  // Restore the EXEC mask
  MachineBasicBlock::iterator First = RemainderBB->begin();
  BuildMI(*RemainderBB, First, DL, TII.get(AMDGPU::S_MOV_B64), AMDGPU::EXEC)
      .addReg(SaveExec);
}

// Extract pointer from Rsrc and return a zero-value Rsrc replacement.
static std::tuple<unsigned, unsigned>
extractRsrcPtr(const SIInstrInfo &TII, MachineInstr &MI, MachineOperand &Rsrc) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Extract the ptr from the resource descriptor.
  unsigned RsrcPtr =
      TII.buildExtractSubReg(MI, MRI, Rsrc, &AMDGPU::VReg_128RegClass,
                             AMDGPU::sub0_sub1, &AMDGPU::VReg_64RegClass);

  // Create an empty resource descriptor
  unsigned Zero64 = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  unsigned SRsrcFormatLo = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned SRsrcFormatHi = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  unsigned NewSRsrc = MRI.createVirtualRegister(&AMDGPU::SReg_128RegClass);
  uint64_t RsrcDataFormat = TII.getDefaultRsrcDataFormat();

  // Zero64 = 0
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B64), Zero64)
      .addImm(0);

  // SRsrcFormatLo = RSRC_DATA_FORMAT{31-0}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B32), SRsrcFormatLo)
      .addImm(RsrcDataFormat & 0xFFFFFFFF);

  // SRsrcFormatHi = RSRC_DATA_FORMAT{63-32}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::S_MOV_B32), SRsrcFormatHi)
      .addImm(RsrcDataFormat >> 32);

  // NewSRsrc = {Zero64, SRsrcFormat}
  BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(AMDGPU::REG_SEQUENCE), NewSRsrc)
      .addReg(Zero64)
      .addImm(AMDGPU::sub0_sub1)
      .addReg(SRsrcFormatLo)
      .addImm(AMDGPU::sub2)
      .addReg(SRsrcFormatHi)
      .addImm(AMDGPU::sub3);

  return std::make_tuple(RsrcPtr, NewSRsrc);
}

void SIInstrInfo::legalizeOperands(MachineInstr &MI,
                                   MachineDominatorTree *MDT) const {
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Legalize VOP2
  if (isVOP2(MI) || isVOPC(MI)) {
    legalizeOperandsVOP2(MRI, MI);
    return;
  }

  // Legalize VOP3
  if (isVOP3(MI)) {
    legalizeOperandsVOP3(MRI, MI);
    return;
  }

  // Legalize SMRD
  if (isSMRD(MI)) {
    legalizeOperandsSMRD(MRI, MI);
    return;
  }

  // Legalize REG_SEQUENCE and PHI
  // The register class of the operands much be the same type as the register
  // class of the output.
  if (MI.getOpcode() == AMDGPU::PHI) {
    const TargetRegisterClass *RC = nullptr, *SRC = nullptr, *VRC = nullptr;
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2) {
      if (!MI.getOperand(i).isReg() ||
          !TargetRegisterInfo::isVirtualRegister(MI.getOperand(i).getReg()))
        continue;
      const TargetRegisterClass *OpRC =
          MRI.getRegClass(MI.getOperand(i).getReg());
      if (RI.hasVGPRs(OpRC)) {
        VRC = OpRC;
      } else {
        SRC = OpRC;
      }
    }

    // If any of the operands are VGPR registers, then they all most be
    // otherwise we will create illegal VGPR->SGPR copies when legalizing
    // them.
    if (VRC || !RI.isSGPRClass(getOpRegClass(MI, 0))) {
      if (!VRC) {
        assert(SRC);
        VRC = RI.getEquivalentVGPRClass(SRC);
      }
      RC = VRC;
    } else {
      RC = SRC;
    }

    // Update all the operands so they have the same type.
    for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2) {
      MachineOperand &Op = MI.getOperand(I);
      if (!Op.isReg() || !TargetRegisterInfo::isVirtualRegister(Op.getReg()))
        continue;

      // MI is a PHI instruction.
      MachineBasicBlock *InsertBB = MI.getOperand(I + 1).getMBB();
      MachineBasicBlock::iterator Insert = InsertBB->getFirstTerminator();

      // Avoid creating no-op copies with the same src and dst reg class.  These
      // confuse some of the machine passes.
      legalizeGenericOperand(*InsertBB, Insert, RC, Op, MRI, MI.getDebugLoc());
    }
  }

  // REG_SEQUENCE doesn't really require operand legalization, but if one has a
  // VGPR dest type and SGPR sources, insert copies so all operands are
  // VGPRs. This seems to help operand folding / the register coalescer.
  if (MI.getOpcode() == AMDGPU::REG_SEQUENCE) {
    MachineBasicBlock *MBB = MI.getParent();
    const TargetRegisterClass *DstRC = getOpRegClass(MI, 0);
    if (RI.hasVGPRs(DstRC)) {
      // Update all the operands so they are VGPR register classes. These may
      // not be the same register class because REG_SEQUENCE supports mixing
      // subregister index types e.g. sub0_sub1 + sub2 + sub3
      for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2) {
        MachineOperand &Op = MI.getOperand(I);
        if (!Op.isReg() || !TargetRegisterInfo::isVirtualRegister(Op.getReg()))
          continue;

        const TargetRegisterClass *OpRC = MRI.getRegClass(Op.getReg());
        const TargetRegisterClass *VRC = RI.getEquivalentVGPRClass(OpRC);
        if (VRC == OpRC)
          continue;

        legalizeGenericOperand(*MBB, MI, VRC, Op, MRI, MI.getDebugLoc());
        Op.setIsKill();
      }
    }

    return;
  }

  // Legalize INSERT_SUBREG
  // src0 must have the same register class as dst
  if (MI.getOpcode() == AMDGPU::INSERT_SUBREG) {
    unsigned Dst = MI.getOperand(0).getReg();
    unsigned Src0 = MI.getOperand(1).getReg();
    const TargetRegisterClass *DstRC = MRI.getRegClass(Dst);
    const TargetRegisterClass *Src0RC = MRI.getRegClass(Src0);
    if (DstRC != Src0RC) {
      MachineBasicBlock *MBB = MI.getParent();
      MachineOperand &Op = MI.getOperand(1);
      legalizeGenericOperand(*MBB, MI, DstRC, Op, MRI, MI.getDebugLoc());
    }
    return;
  }

  // Legalize SI_INIT_M0
  if (MI.getOpcode() == AMDGPU::SI_INIT_M0) {
    MachineOperand &Src = MI.getOperand(0);
    if (Src.isReg() && RI.hasVGPRs(MRI.getRegClass(Src.getReg())))
      Src.setReg(readlaneVGPRToSGPR(Src.getReg(), MI, MRI));
    return;
  }

  // Legalize MIMG and MUBUF/MTBUF for shaders.
  //
  // Shaders only generate MUBUF/MTBUF instructions via intrinsics or via
  // scratch memory access. In both cases, the legalization never involves
  // conversion to the addr64 form.
  if (isMIMG(MI) ||
      (AMDGPU::isShader(MF.getFunction().getCallingConv()) &&
       (isMUBUF(MI) || isMTBUF(MI)))) {
    MachineOperand *SRsrc = getNamedOperand(MI, AMDGPU::OpName::srsrc);
    if (SRsrc && !RI.isSGPRClass(MRI.getRegClass(SRsrc->getReg()))) {
      unsigned SGPR = readlaneVGPRToSGPR(SRsrc->getReg(), MI, MRI);
      SRsrc->setReg(SGPR);
    }

    MachineOperand *SSamp = getNamedOperand(MI, AMDGPU::OpName::ssamp);
    if (SSamp && !RI.isSGPRClass(MRI.getRegClass(SSamp->getReg()))) {
      unsigned SGPR = readlaneVGPRToSGPR(SSamp->getReg(), MI, MRI);
      SSamp->setReg(SGPR);
    }
    return;
  }

  // Legalize MUBUF* instructions.
  int RsrcIdx =
      AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::srsrc);
  if (RsrcIdx != -1) {
    // We have an MUBUF instruction
    MachineOperand *Rsrc = &MI.getOperand(RsrcIdx);
    unsigned RsrcRC = get(MI.getOpcode()).OpInfo[RsrcIdx].RegClass;
    if (RI.getCommonSubClass(MRI.getRegClass(Rsrc->getReg()),
                             RI.getRegClass(RsrcRC))) {
      // The operands are legal.
      // FIXME: We may need to legalize operands besided srsrc.
      return;
    }

    // Legalize a VGPR Rsrc.
    //
    // If the instruction is _ADDR64, we can avoid a waterfall by extracting
    // the base pointer from the VGPR Rsrc, adding it to the VAddr, then using
    // a zero-value SRsrc.
    //
    // If the instruction is _OFFSET (both idxen and offen disabled), and we
    // support ADDR64 instructions, we can convert to ADDR64 and do the same as
    // above.
    //
    // Otherwise we are on non-ADDR64 hardware, and/or we have
    // idxen/offen/bothen and we fall back to a waterfall loop.

    MachineBasicBlock &MBB = *MI.getParent();

    MachineOperand *VAddr = getNamedOperand(MI, AMDGPU::OpName::vaddr);
    if (VAddr && AMDGPU::getIfAddr64Inst(MI.getOpcode()) != -1) {
      // This is already an ADDR64 instruction so we need to add the pointer
      // extracted from the resource descriptor to the current value of VAddr.
      unsigned NewVAddrLo = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      unsigned NewVAddrHi = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      unsigned NewVAddr = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

      unsigned RsrcPtr, NewSRsrc;
      std::tie(RsrcPtr, NewSRsrc) = extractRsrcPtr(*this, MI, *Rsrc);

      // NewVaddrLo = RsrcPtr:sub0 + VAddr:sub0
      DebugLoc DL = MI.getDebugLoc();
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ADD_I32_e32), NewVAddrLo)
          .addReg(RsrcPtr, 0, AMDGPU::sub0)
          .addReg(VAddr->getReg(), 0, AMDGPU::sub0);

      // NewVaddrHi = RsrcPtr:sub1 + VAddr:sub1
      BuildMI(MBB, MI, DL, get(AMDGPU::V_ADDC_U32_e32), NewVAddrHi)
          .addReg(RsrcPtr, 0, AMDGPU::sub1)
          .addReg(VAddr->getReg(), 0, AMDGPU::sub1);

      // NewVaddr = {NewVaddrHi, NewVaddrLo}
      BuildMI(MBB, MI, MI.getDebugLoc(), get(AMDGPU::REG_SEQUENCE), NewVAddr)
          .addReg(NewVAddrLo)
          .addImm(AMDGPU::sub0)
          .addReg(NewVAddrHi)
          .addImm(AMDGPU::sub1);

      VAddr->setReg(NewVAddr);
      Rsrc->setReg(NewSRsrc);
    } else if (!VAddr && ST.hasAddr64()) {
      // This instructions is the _OFFSET variant, so we need to convert it to
      // ADDR64.
      assert(MBB.getParent()->getSubtarget<GCNSubtarget>().getGeneration()
             < AMDGPUSubtarget::VOLCANIC_ISLANDS &&
             "FIXME: Need to emit flat atomics here");

      unsigned RsrcPtr, NewSRsrc;
      std::tie(RsrcPtr, NewSRsrc) = extractRsrcPtr(*this, MI, *Rsrc);

      unsigned NewVAddr = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);
      MachineOperand *VData = getNamedOperand(MI, AMDGPU::OpName::vdata);
      MachineOperand *Offset = getNamedOperand(MI, AMDGPU::OpName::offset);
      MachineOperand *SOffset = getNamedOperand(MI, AMDGPU::OpName::soffset);
      unsigned Addr64Opcode = AMDGPU::getAddr64Inst(MI.getOpcode());

      // Atomics rith return have have an additional tied operand and are
      // missing some of the special bits.
      MachineOperand *VDataIn = getNamedOperand(MI, AMDGPU::OpName::vdata_in);
      MachineInstr *Addr64;

      if (!VDataIn) {
        // Regular buffer load / store.
        MachineInstrBuilder MIB =
            BuildMI(MBB, MI, MI.getDebugLoc(), get(Addr64Opcode))
                .add(*VData)
                .addReg(NewVAddr)
                .addReg(NewSRsrc)
                .add(*SOffset)
                .add(*Offset);

        // Atomics do not have this operand.
        if (const MachineOperand *GLC =
                getNamedOperand(MI, AMDGPU::OpName::glc)) {
          MIB.addImm(GLC->getImm());
        }

        MIB.addImm(getNamedImmOperand(MI, AMDGPU::OpName::slc));

        if (const MachineOperand *TFE =
                getNamedOperand(MI, AMDGPU::OpName::tfe)) {
          MIB.addImm(TFE->getImm());
        }

        MIB.cloneMemRefs(MI);
        Addr64 = MIB;
      } else {
        // Atomics with return.
        Addr64 = BuildMI(MBB, MI, MI.getDebugLoc(), get(Addr64Opcode))
                     .add(*VData)
                     .add(*VDataIn)
                     .addReg(NewVAddr)
                     .addReg(NewSRsrc)
                     .add(*SOffset)
                     .add(*Offset)
                     .addImm(getNamedImmOperand(MI, AMDGPU::OpName::slc))
                     .cloneMemRefs(MI);
      }

      MI.removeFromParent();

      // NewVaddr = {NewVaddrHi, NewVaddrLo}
      BuildMI(MBB, Addr64, Addr64->getDebugLoc(), get(AMDGPU::REG_SEQUENCE),
              NewVAddr)
          .addReg(RsrcPtr, 0, AMDGPU::sub0)
          .addImm(AMDGPU::sub0)
          .addReg(RsrcPtr, 0, AMDGPU::sub1)
          .addImm(AMDGPU::sub1);
    } else {
      // This is another variant; legalize Rsrc with waterfall loop from VGPRs
      // to SGPRs.
      loadSRsrcFromVGPR(*this, MI, *Rsrc, MDT);
    }
  }
}

void SIInstrInfo::moveToVALU(MachineInstr &TopInst,
                             MachineDominatorTree *MDT) const {
  SetVectorType Worklist;
  Worklist.insert(&TopInst);

  while (!Worklist.empty()) {
    MachineInstr &Inst = *Worklist.pop_back_val();
    MachineBasicBlock *MBB = Inst.getParent();
    MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

    unsigned Opcode = Inst.getOpcode();
    unsigned NewOpcode = getVALUOp(Inst);

    // Handle some special cases
    switch (Opcode) {
    default:
      break;
    case AMDGPU::S_ADD_U64_PSEUDO:
    case AMDGPU::S_SUB_U64_PSEUDO:
      splitScalar64BitAddSub(Worklist, Inst, MDT);
      Inst.eraseFromParent();
      continue;
    case AMDGPU::S_ADD_I32:
    case AMDGPU::S_SUB_I32:
      // FIXME: The u32 versions currently selected use the carry.
      if (moveScalarAddSub(Worklist, Inst, MDT))
        continue;

      // Default handling
      break;
    case AMDGPU::S_AND_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_AND_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_OR_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_OR_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_XOR_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_XOR_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_NAND_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_NAND_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_NOR_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_NOR_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_XNOR_B64:
      if (ST.hasDLInsts())
        splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_XNOR_B32, MDT);
      else
        splitScalar64BitXnor(Worklist, Inst, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_ANDN2_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_ANDN2_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_ORN2_B64:
      splitScalar64BitBinaryOp(Worklist, Inst, AMDGPU::S_ORN2_B32, MDT);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_NOT_B64:
      splitScalar64BitUnaryOp(Worklist, Inst, AMDGPU::S_NOT_B32);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_BCNT1_I32_B64:
      splitScalar64BitBCNT(Worklist, Inst);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_BFE_I64:
      splitScalar64BitBFE(Worklist, Inst);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_LSHL_B32:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_LSHLREV_B32_e64;
        swapOperands(Inst);
      }
      break;
    case AMDGPU::S_ASHR_I32:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_ASHRREV_I32_e64;
        swapOperands(Inst);
      }
      break;
    case AMDGPU::S_LSHR_B32:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_LSHRREV_B32_e64;
        swapOperands(Inst);
      }
      break;
    case AMDGPU::S_LSHL_B64:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_LSHLREV_B64;
        swapOperands(Inst);
      }
      break;
    case AMDGPU::S_ASHR_I64:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_ASHRREV_I64;
        swapOperands(Inst);
      }
      break;
    case AMDGPU::S_LSHR_B64:
      if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
        NewOpcode = AMDGPU::V_LSHRREV_B64;
        swapOperands(Inst);
      }
      break;

    case AMDGPU::S_ABS_I32:
      lowerScalarAbs(Worklist, Inst);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_CBRANCH_SCC0:
    case AMDGPU::S_CBRANCH_SCC1:
      // Clear unused bits of vcc
      BuildMI(*MBB, Inst, Inst.getDebugLoc(), get(AMDGPU::S_AND_B64),
              AMDGPU::VCC)
          .addReg(AMDGPU::EXEC)
          .addReg(AMDGPU::VCC);
      break;

    case AMDGPU::S_BFE_U64:
    case AMDGPU::S_BFM_B64:
      llvm_unreachable("Moving this op to VALU not implemented");

    case AMDGPU::S_PACK_LL_B32_B16:
    case AMDGPU::S_PACK_LH_B32_B16:
    case AMDGPU::S_PACK_HH_B32_B16:
      movePackToVALU(Worklist, MRI, Inst);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_XNOR_B32:
      lowerScalarXnor(Worklist, Inst);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_NAND_B32:
      splitScalarNotBinop(Worklist, Inst, AMDGPU::S_AND_B32);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_NOR_B32:
      splitScalarNotBinop(Worklist, Inst, AMDGPU::S_OR_B32);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_ANDN2_B32:
      splitScalarBinOpN2(Worklist, Inst, AMDGPU::S_AND_B32);
      Inst.eraseFromParent();
      continue;

    case AMDGPU::S_ORN2_B32:
      splitScalarBinOpN2(Worklist, Inst, AMDGPU::S_OR_B32);
      Inst.eraseFromParent();
      continue;
    }

    if (NewOpcode == AMDGPU::INSTRUCTION_LIST_END) {
      // We cannot move this instruction to the VALU, so we should try to
      // legalize its operands instead.
      legalizeOperands(Inst, MDT);
      continue;
    }

    // Use the new VALU Opcode.
    const MCInstrDesc &NewDesc = get(NewOpcode);
    Inst.setDesc(NewDesc);

    // Remove any references to SCC. Vector instructions can't read from it, and
    // We're just about to add the implicit use / defs of VCC, and we don't want
    // both.
    for (unsigned i = Inst.getNumOperands() - 1; i > 0; --i) {
      MachineOperand &Op = Inst.getOperand(i);
      if (Op.isReg() && Op.getReg() == AMDGPU::SCC) {
        Inst.RemoveOperand(i);
        addSCCDefUsersToVALUWorklist(Inst, Worklist);
      }
    }

    if (Opcode == AMDGPU::S_SEXT_I32_I8 || Opcode == AMDGPU::S_SEXT_I32_I16) {
      // We are converting these to a BFE, so we need to add the missing
      // operands for the size and offset.
      unsigned Size = (Opcode == AMDGPU::S_SEXT_I32_I8) ? 8 : 16;
      Inst.addOperand(MachineOperand::CreateImm(0));
      Inst.addOperand(MachineOperand::CreateImm(Size));

    } else if (Opcode == AMDGPU::S_BCNT1_I32_B32) {
      // The VALU version adds the second operand to the result, so insert an
      // extra 0 operand.
      Inst.addOperand(MachineOperand::CreateImm(0));
    }

    Inst.addImplicitDefUseOperands(*Inst.getParent()->getParent());

    if (Opcode == AMDGPU::S_BFE_I32 || Opcode == AMDGPU::S_BFE_U32) {
      const MachineOperand &OffsetWidthOp = Inst.getOperand(2);
      // If we need to move this to VGPRs, we need to unpack the second operand
      // back into the 2 separate ones for bit offset and width.
      assert(OffsetWidthOp.isImm() &&
             "Scalar BFE is only implemented for constant width and offset");
      uint32_t Imm = OffsetWidthOp.getImm();

      uint32_t Offset = Imm & 0x3f; // Extract bits [5:0].
      uint32_t BitWidth = (Imm & 0x7f0000) >> 16; // Extract bits [22:16].
      Inst.RemoveOperand(2);                     // Remove old immediate.
      Inst.addOperand(MachineOperand::CreateImm(Offset));
      Inst.addOperand(MachineOperand::CreateImm(BitWidth));
    }

    bool HasDst = Inst.getOperand(0).isReg() && Inst.getOperand(0).isDef();
    unsigned NewDstReg = AMDGPU::NoRegister;
    if (HasDst) {
      unsigned DstReg = Inst.getOperand(0).getReg();
      if (TargetRegisterInfo::isPhysicalRegister(DstReg))
        continue;

      // Update the destination register class.
      const TargetRegisterClass *NewDstRC = getDestEquivalentVGPRClass(Inst);
      if (!NewDstRC)
        continue;

      if (Inst.isCopy() &&
          TargetRegisterInfo::isVirtualRegister(Inst.getOperand(1).getReg()) &&
          NewDstRC == RI.getRegClassForReg(MRI, Inst.getOperand(1).getReg())) {
        // Instead of creating a copy where src and dst are the same register
        // class, we just replace all uses of dst with src.  These kinds of
        // copies interfere with the heuristics MachineSink uses to decide
        // whether or not to split a critical edge.  Since the pass assumes
        // that copies will end up as machine instructions and not be
        // eliminated.
        addUsersToMoveToVALUWorklist(DstReg, MRI, Worklist);
        MRI.replaceRegWith(DstReg, Inst.getOperand(1).getReg());
        MRI.clearKillFlags(Inst.getOperand(1).getReg());
        Inst.getOperand(0).setReg(DstReg);

        // Make sure we don't leave around a dead VGPR->SGPR copy. Normally
        // these are deleted later, but at -O0 it would leave a suspicious
        // looking illegal copy of an undef register.
        for (unsigned I = Inst.getNumOperands() - 1; I != 0; --I)
          Inst.RemoveOperand(I);
        Inst.setDesc(get(AMDGPU::IMPLICIT_DEF));
        continue;
      }

      NewDstReg = MRI.createVirtualRegister(NewDstRC);
      MRI.replaceRegWith(DstReg, NewDstReg);
    }

    // Legalize the operands
    legalizeOperands(Inst, MDT);

    if (HasDst)
     addUsersToMoveToVALUWorklist(NewDstReg, MRI, Worklist);
  }
}

// Add/sub require special handling to deal with carry outs.
bool SIInstrInfo::moveScalarAddSub(SetVectorType &Worklist, MachineInstr &Inst,
                                   MachineDominatorTree *MDT) const {
  if (ST.hasAddNoCarry()) {
    // Assume there is no user of scc since we don't select this in that case.
    // Since scc isn't used, it doesn't really matter if the i32 or u32 variant
    // is used.

    MachineBasicBlock &MBB = *Inst.getParent();
    MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

    unsigned OldDstReg = Inst.getOperand(0).getReg();
    unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

    unsigned Opc = Inst.getOpcode();
    assert(Opc == AMDGPU::S_ADD_I32 || Opc == AMDGPU::S_SUB_I32);

    unsigned NewOpc = Opc == AMDGPU::S_ADD_I32 ?
      AMDGPU::V_ADD_U32_e64 : AMDGPU::V_SUB_U32_e64;

    assert(Inst.getOperand(3).getReg() == AMDGPU::SCC);
    Inst.RemoveOperand(3);

    Inst.setDesc(get(NewOpc));
    Inst.addImplicitDefUseOperands(*MBB.getParent());
    MRI.replaceRegWith(OldDstReg, ResultReg);
    legalizeOperands(Inst, MDT);

    addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
    return true;
  }

  return false;
}

void SIInstrInfo::lowerScalarAbs(SetVectorType &Worklist,
                                 MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  DebugLoc DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src = Inst.getOperand(1);
  unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  unsigned SubOp = ST.hasAddNoCarry() ?
    AMDGPU::V_SUB_U32_e32 : AMDGPU::V_SUB_I32_e32;

  BuildMI(MBB, MII, DL, get(SubOp), TmpReg)
    .addImm(0)
    .addReg(Src.getReg());

  BuildMI(MBB, MII, DL, get(AMDGPU::V_MAX_I32_e64), ResultReg)
    .addReg(Src.getReg())
    .addReg(TmpReg);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::lowerScalarXnor(SetVectorType &Worklist,
                                  MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  if (ST.hasDLInsts()) {
    unsigned NewDest = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    legalizeGenericOperand(MBB, MII, &AMDGPU::VGPR_32RegClass, Src0, MRI, DL);
    legalizeGenericOperand(MBB, MII, &AMDGPU::VGPR_32RegClass, Src1, MRI, DL);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_XNOR_B32_e64), NewDest)
      .add(Src0)
      .add(Src1);

    MRI.replaceRegWith(Dest.getReg(), NewDest);
    addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
  } else {
    // Using the identity !(x ^ y) == (!x ^ y) == (x ^ !y), we can
    // invert either source and then perform the XOR. If either source is a
    // scalar register, then we can leave the inversion on the scalar unit to
    // acheive a better distrubution of scalar and vector instructions.
    bool Src0IsSGPR = Src0.isReg() &&
                      RI.isSGPRClass(MRI.getRegClass(Src0.getReg()));
    bool Src1IsSGPR = Src1.isReg() &&
                      RI.isSGPRClass(MRI.getRegClass(Src1.getReg()));
    MachineInstr *Not = nullptr;
    MachineInstr *Xor = nullptr;
    unsigned Temp = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
    unsigned NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

    // Build a pair of scalar instructions and add them to the work list.
    // The next iteration over the work list will lower these to the vector
    // unit as necessary.
    if (Src0IsSGPR) {
      Not = BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Temp)
        .add(Src0);
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), NewDest)
      .addReg(Temp)
      .add(Src1);
    } else if (Src1IsSGPR) {
      Not = BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Temp)
        .add(Src1);
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), NewDest)
      .add(Src0)
      .addReg(Temp);
    } else {
      Xor = BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B32), Temp)
        .add(Src0)
        .add(Src1);
      Not = BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), NewDest)
        .addReg(Temp);
      Worklist.insert(Not);
    }

    MRI.replaceRegWith(Dest.getReg(), NewDest);

    Worklist.insert(Xor);

    addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
  }
}

void SIInstrInfo::splitScalarNotBinop(SetVectorType &Worklist,
                                      MachineInstr &Inst,
                                      unsigned Opcode) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  unsigned NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
  unsigned Interm = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  MachineInstr &Op = *BuildMI(MBB, MII, DL, get(Opcode), Interm)
    .add(Src0)
    .add(Src1);

  MachineInstr &Not = *BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), NewDest)
    .addReg(Interm);

  Worklist.insert(&Op);
  Worklist.insert(&Not);

  MRI.replaceRegWith(Dest.getReg(), NewDest);
  addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
}

void SIInstrInfo::splitScalarBinOpN2(SetVectorType& Worklist,
                                     MachineInstr &Inst,
                                     unsigned Opcode) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);

  unsigned NewDest = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
  unsigned Interm = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  MachineInstr &Not = *BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B32), Interm)
    .add(Src1);

  MachineInstr &Op = *BuildMI(MBB, MII, DL, get(Opcode), NewDest)
    .add(Src0)
    .addReg(Interm);

  Worklist.insert(&Not);
  Worklist.insert(&Op);

  MRI.replaceRegWith(Dest.getReg(), NewDest);
  addUsersToMoveToVALUWorklist(NewDest, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitUnaryOp(
    SetVectorType &Worklist, MachineInstr &Inst,
    unsigned Opcode) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  DebugLoc DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const MCInstrDesc &InstDesc = get(Opcode);
  const TargetRegisterClass *Src0RC = Src0.isReg() ?
    MRI.getRegClass(Src0.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src0SubRC = RI.getSubRegClass(Src0RC, AMDGPU::sub0);

  MachineOperand SrcReg0Sub0 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub0, Src0SubRC);

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());
  const TargetRegisterClass *NewDestRC = RI.getEquivalentVGPRClass(DestRC);
  const TargetRegisterClass *NewDestSubRC = RI.getSubRegClass(NewDestRC, AMDGPU::sub0);

  unsigned DestSub0 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &LoHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub0).add(SrcReg0Sub0);

  MachineOperand SrcReg0Sub1 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub1, Src0SubRC);

  unsigned DestSub1 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &HiHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub1).add(SrcReg0Sub1);

  unsigned FullDestReg = MRI.createVirtualRegister(NewDestRC);
  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
    .addReg(DestSub0)
    .addImm(AMDGPU::sub0)
    .addReg(DestSub1)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  Worklist.insert(&LoHalf);
  Worklist.insert(&HiHalf);

  // We don't need to legalizeOperands here because for a single operand, src0
  // will support any kind of input.

  // Move all users of this moved value.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitAddSub(SetVectorType &Worklist,
                                         MachineInstr &Inst,
                                         MachineDominatorTree *MDT) const {
  bool IsAdd = (Inst.getOpcode() == AMDGPU::S_ADD_U64_PSEUDO);

  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  unsigned FullDestReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);
  unsigned DestSub0 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  unsigned DestSub1 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  unsigned CarryReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
  unsigned DeadCarryReg = MRI.createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();
  MachineBasicBlock::iterator MII = Inst;

  const TargetRegisterClass *Src0RC = MRI.getRegClass(Src0.getReg());
  const TargetRegisterClass *Src1RC = MRI.getRegClass(Src1.getReg());
  const TargetRegisterClass *Src0SubRC = RI.getSubRegClass(Src0RC, AMDGPU::sub0);
  const TargetRegisterClass *Src1SubRC = RI.getSubRegClass(Src1RC, AMDGPU::sub0);

  MachineOperand SrcReg0Sub0 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub0, Src0SubRC);
  MachineOperand SrcReg1Sub0 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub0, Src1SubRC);


  MachineOperand SrcReg0Sub1 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub1, Src0SubRC);
  MachineOperand SrcReg1Sub1 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub1, Src1SubRC);

  unsigned LoOpc = IsAdd ? AMDGPU::V_ADD_I32_e64 : AMDGPU::V_SUB_I32_e64;
  MachineInstr *LoHalf =
    BuildMI(MBB, MII, DL, get(LoOpc), DestSub0)
    .addReg(CarryReg, RegState::Define)
    .add(SrcReg0Sub0)
    .add(SrcReg1Sub0);

  unsigned HiOpc = IsAdd ? AMDGPU::V_ADDC_U32_e64 : AMDGPU::V_SUBB_U32_e64;
  MachineInstr *HiHalf =
    BuildMI(MBB, MII, DL, get(HiOpc), DestSub1)
    .addReg(DeadCarryReg, RegState::Define | RegState::Dead)
    .add(SrcReg0Sub1)
    .add(SrcReg1Sub1)
    .addReg(CarryReg, RegState::Kill);

  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
    .addReg(DestSub0)
    .addImm(AMDGPU::sub0)
    .addReg(DestSub1)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  // Try to legalize the operands in case we need to swap the order to keep it
  // valid.
  legalizeOperands(*LoHalf, MDT);
  legalizeOperands(*HiHalf, MDT);

  // Move all users of this moved vlaue.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitBinaryOp(SetVectorType &Worklist,
                                           MachineInstr &Inst, unsigned Opcode,
                                           MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  DebugLoc DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const MCInstrDesc &InstDesc = get(Opcode);
  const TargetRegisterClass *Src0RC = Src0.isReg() ?
    MRI.getRegClass(Src0.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src0SubRC = RI.getSubRegClass(Src0RC, AMDGPU::sub0);
  const TargetRegisterClass *Src1RC = Src1.isReg() ?
    MRI.getRegClass(Src1.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  const TargetRegisterClass *Src1SubRC = RI.getSubRegClass(Src1RC, AMDGPU::sub0);

  MachineOperand SrcReg0Sub0 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub0, Src0SubRC);
  MachineOperand SrcReg1Sub0 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub0, Src1SubRC);
  MachineOperand SrcReg0Sub1 = buildExtractSubRegOrImm(MII, MRI, Src0, Src0RC,
                                                       AMDGPU::sub1, Src0SubRC);
  MachineOperand SrcReg1Sub1 = buildExtractSubRegOrImm(MII, MRI, Src1, Src1RC,
                                                       AMDGPU::sub1, Src1SubRC);

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());
  const TargetRegisterClass *NewDestRC = RI.getEquivalentVGPRClass(DestRC);
  const TargetRegisterClass *NewDestSubRC = RI.getSubRegClass(NewDestRC, AMDGPU::sub0);

  unsigned DestSub0 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &LoHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub0)
                              .add(SrcReg0Sub0)
                              .add(SrcReg1Sub0);

  unsigned DestSub1 = MRI.createVirtualRegister(NewDestSubRC);
  MachineInstr &HiHalf = *BuildMI(MBB, MII, DL, InstDesc, DestSub1)
                              .add(SrcReg0Sub1)
                              .add(SrcReg1Sub1);

  unsigned FullDestReg = MRI.createVirtualRegister(NewDestRC);
  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), FullDestReg)
    .addReg(DestSub0)
    .addImm(AMDGPU::sub0)
    .addReg(DestSub1)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), FullDestReg);

  Worklist.insert(&LoHalf);
  Worklist.insert(&HiHalf);

  // Move all users of this moved vlaue.
  addUsersToMoveToVALUWorklist(FullDestReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitXnor(SetVectorType &Worklist,
                                       MachineInstr &Inst,
                                       MachineDominatorTree *MDT) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineBasicBlock::iterator MII = Inst;

  const TargetRegisterClass *DestRC = MRI.getRegClass(Dest.getReg());

  unsigned Interm = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);

  MachineOperand* Op0;
  MachineOperand* Op1;

  if (Src0.isReg() && RI.isSGPRReg(MRI, Src0.getReg())) {
    Op0 = &Src0;
    Op1 = &Src1;
  } else {
    Op0 = &Src1;
    Op1 = &Src0;
  }

  BuildMI(MBB, MII, DL, get(AMDGPU::S_NOT_B64), Interm)
    .add(*Op0);

  unsigned NewDest = MRI.createVirtualRegister(DestRC);

  MachineInstr &Xor = *BuildMI(MBB, MII, DL, get(AMDGPU::S_XOR_B64), NewDest)
    .addReg(Interm)
    .add(*Op1);

  MRI.replaceRegWith(Dest.getReg(), NewDest);

  Worklist.insert(&Xor);
}

void SIInstrInfo::splitScalar64BitBCNT(
    SetVectorType &Worklist, MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  MachineOperand &Src = Inst.getOperand(1);

  const MCInstrDesc &InstDesc = get(AMDGPU::V_BCNT_U32_B32_e64);
  const TargetRegisterClass *SrcRC = Src.isReg() ?
    MRI.getRegClass(Src.getReg()) :
    &AMDGPU::SGPR_32RegClass;

  unsigned MidReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  const TargetRegisterClass *SrcSubRC = RI.getSubRegClass(SrcRC, AMDGPU::sub0);

  MachineOperand SrcRegSub0 = buildExtractSubRegOrImm(MII, MRI, Src, SrcRC,
                                                      AMDGPU::sub0, SrcSubRC);
  MachineOperand SrcRegSub1 = buildExtractSubRegOrImm(MII, MRI, Src, SrcRC,
                                                      AMDGPU::sub1, SrcSubRC);

  BuildMI(MBB, MII, DL, InstDesc, MidReg).add(SrcRegSub0).addImm(0);

  BuildMI(MBB, MII, DL, InstDesc, ResultReg).add(SrcRegSub1).addReg(MidReg);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);

  // We don't need to legalize operands here. src0 for etiher instruction can be
  // an SGPR, and the second input is unused or determined here.
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::splitScalar64BitBFE(SetVectorType &Worklist,
                                      MachineInstr &Inst) const {
  MachineBasicBlock &MBB = *Inst.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineBasicBlock::iterator MII = Inst;
  const DebugLoc &DL = Inst.getDebugLoc();

  MachineOperand &Dest = Inst.getOperand(0);
  uint32_t Imm = Inst.getOperand(2).getImm();
  uint32_t Offset = Imm & 0x3f; // Extract bits [5:0].
  uint32_t BitWidth = (Imm & 0x7f0000) >> 16; // Extract bits [22:16].

  (void) Offset;

  // Only sext_inreg cases handled.
  assert(Inst.getOpcode() == AMDGPU::S_BFE_I64 && BitWidth <= 32 &&
         Offset == 0 && "Not implemented");

  if (BitWidth < 32) {
    unsigned MidRegLo = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    unsigned MidRegHi = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_BFE_I32), MidRegLo)
        .addReg(Inst.getOperand(1).getReg(), 0, AMDGPU::sub0)
        .addImm(0)
        .addImm(BitWidth);

    BuildMI(MBB, MII, DL, get(AMDGPU::V_ASHRREV_I32_e32), MidRegHi)
      .addImm(31)
      .addReg(MidRegLo);

    BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), ResultReg)
      .addReg(MidRegLo)
      .addImm(AMDGPU::sub0)
      .addReg(MidRegHi)
      .addImm(AMDGPU::sub1);

    MRI.replaceRegWith(Dest.getReg(), ResultReg);
    addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
    return;
  }

  MachineOperand &Src = Inst.getOperand(1);
  unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VReg_64RegClass);

  BuildMI(MBB, MII, DL, get(AMDGPU::V_ASHRREV_I32_e64), TmpReg)
    .addImm(31)
    .addReg(Src.getReg(), 0, AMDGPU::sub0);

  BuildMI(MBB, MII, DL, get(TargetOpcode::REG_SEQUENCE), ResultReg)
    .addReg(Src.getReg(), 0, AMDGPU::sub0)
    .addImm(AMDGPU::sub0)
    .addReg(TmpReg)
    .addImm(AMDGPU::sub1);

  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::addUsersToMoveToVALUWorklist(
  unsigned DstReg,
  MachineRegisterInfo &MRI,
  SetVectorType &Worklist) const {
  for (MachineRegisterInfo::use_iterator I = MRI.use_begin(DstReg),
         E = MRI.use_end(); I != E;) {
    MachineInstr &UseMI = *I->getParent();
    if (!canReadVGPR(UseMI, I.getOperandNo())) {
      Worklist.insert(&UseMI);

      do {
        ++I;
      } while (I != E && I->getParent() == &UseMI);
    } else {
      ++I;
    }
  }
}

void SIInstrInfo::movePackToVALU(SetVectorType &Worklist,
                                 MachineRegisterInfo &MRI,
                                 MachineInstr &Inst) const {
  unsigned ResultReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineBasicBlock *MBB = Inst.getParent();
  MachineOperand &Src0 = Inst.getOperand(1);
  MachineOperand &Src1 = Inst.getOperand(2);
  const DebugLoc &DL = Inst.getDebugLoc();

  switch (Inst.getOpcode()) {
  case AMDGPU::S_PACK_LL_B32_B16: {
    unsigned ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

    // FIXME: Can do a lot better if we know the high bits of src0 or src1 are
    // 0.
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff);

    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_AND_B32_e64), TmpReg)
      .addReg(ImmReg, RegState::Kill)
      .add(Src0);

    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHL_OR_B32), ResultReg)
      .add(Src1)
      .addImm(16)
      .addReg(TmpReg, RegState::Kill);
    break;
  }
  case AMDGPU::S_PACK_LH_B32_B16: {
    unsigned ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_BFI_B32), ResultReg)
      .addReg(ImmReg, RegState::Kill)
      .add(Src0)
      .add(Src1);
    break;
  }
  case AMDGPU::S_PACK_HH_B32_B16: {
    unsigned ImmReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_LSHRREV_B32_e64), TmpReg)
      .addImm(16)
      .add(Src0);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_MOV_B32_e32), ImmReg)
      .addImm(0xffff0000);
    BuildMI(*MBB, Inst, DL, get(AMDGPU::V_AND_OR_B32), ResultReg)
      .add(Src1)
      .addReg(ImmReg, RegState::Kill)
      .addReg(TmpReg, RegState::Kill);
    break;
  }
  default:
    llvm_unreachable("unhandled s_pack_* instruction");
  }

  MachineOperand &Dest = Inst.getOperand(0);
  MRI.replaceRegWith(Dest.getReg(), ResultReg);
  addUsersToMoveToVALUWorklist(ResultReg, MRI, Worklist);
}

void SIInstrInfo::addSCCDefUsersToVALUWorklist(
    MachineInstr &SCCDefInst, SetVectorType &Worklist) const {
  // This assumes that all the users of SCC are in the same block
  // as the SCC def.
  for (MachineInstr &MI :
       make_range(MachineBasicBlock::iterator(SCCDefInst),
                      SCCDefInst.getParent()->end())) {
    // Exit if we find another SCC def.
    if (MI.findRegisterDefOperandIdx(AMDGPU::SCC, false, false, &RI) != -1)
      return;

    if (MI.findRegisterUseOperandIdx(AMDGPU::SCC, false, &RI) != -1)
      Worklist.insert(&MI);
  }
}

const TargetRegisterClass *SIInstrInfo::getDestEquivalentVGPRClass(
  const MachineInstr &Inst) const {
  const TargetRegisterClass *NewDstRC = getOpRegClass(Inst, 0);

  switch (Inst.getOpcode()) {
  // For target instructions, getOpRegClass just returns the virtual register
  // class associated with the operand, so we need to find an equivalent VGPR
  // register class in order to move the instruction to the VALU.
  case AMDGPU::COPY:
  case AMDGPU::PHI:
  case AMDGPU::REG_SEQUENCE:
  case AMDGPU::INSERT_SUBREG:
  case AMDGPU::WQM:
  case AMDGPU::WWM:
    if (RI.hasVGPRs(NewDstRC))
      return nullptr;

    NewDstRC = RI.getEquivalentVGPRClass(NewDstRC);
    if (!NewDstRC)
      return nullptr;
    return NewDstRC;
  default:
    return NewDstRC;
  }
}

// Find the one SGPR operand we are allowed to use.
unsigned SIInstrInfo::findUsedSGPR(const MachineInstr &MI,
                                   int OpIndices[3]) const {
  const MCInstrDesc &Desc = MI.getDesc();

  // Find the one SGPR operand we are allowed to use.
  //
  // First we need to consider the instruction's operand requirements before
  // legalizing. Some operands are required to be SGPRs, such as implicit uses
  // of VCC, but we are still bound by the constant bus requirement to only use
  // one.
  //
  // If the operand's class is an SGPR, we can never move it.

  unsigned SGPRReg = findImplicitSGPRRead(MI);
  if (SGPRReg != AMDGPU::NoRegister)
    return SGPRReg;

  unsigned UsedSGPRs[3] = { AMDGPU::NoRegister };
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();

  for (unsigned i = 0; i < 3; ++i) {
    int Idx = OpIndices[i];
    if (Idx == -1)
      break;

    const MachineOperand &MO = MI.getOperand(Idx);
    if (!MO.isReg())
      continue;

    // Is this operand statically required to be an SGPR based on the operand
    // constraints?
    const TargetRegisterClass *OpRC = RI.getRegClass(Desc.OpInfo[Idx].RegClass);
    bool IsRequiredSGPR = RI.isSGPRClass(OpRC);
    if (IsRequiredSGPR)
      return MO.getReg();

    // If this could be a VGPR or an SGPR, Check the dynamic register class.
    unsigned Reg = MO.getReg();
    const TargetRegisterClass *RegRC = MRI.getRegClass(Reg);
    if (RI.isSGPRClass(RegRC))
      UsedSGPRs[i] = Reg;
  }

  // We don't have a required SGPR operand, so we have a bit more freedom in
  // selecting operands to move.

  // Try to select the most used SGPR. If an SGPR is equal to one of the
  // others, we choose that.
  //
  // e.g.
  // V_FMA_F32 v0, s0, s0, s0 -> No moves
  // V_FMA_F32 v0, s0, s1, s0 -> Move s1

  // TODO: If some of the operands are 64-bit SGPRs and some 32, we should
  // prefer those.

  if (UsedSGPRs[0] != AMDGPU::NoRegister) {
    if (UsedSGPRs[0] == UsedSGPRs[1] || UsedSGPRs[0] == UsedSGPRs[2])
      SGPRReg = UsedSGPRs[0];
  }

  if (SGPRReg == AMDGPU::NoRegister && UsedSGPRs[1] != AMDGPU::NoRegister) {
    if (UsedSGPRs[1] == UsedSGPRs[2])
      SGPRReg = UsedSGPRs[1];
  }

  return SGPRReg;
}

MachineOperand *SIInstrInfo::getNamedOperand(MachineInstr &MI,
                                             unsigned OperandName) const {
  int Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), OperandName);
  if (Idx == -1)
    return nullptr;

  return &MI.getOperand(Idx);
}

uint64_t SIInstrInfo::getDefaultRsrcDataFormat() const {
  uint64_t RsrcDataFormat = AMDGPU::RSRC_DATA_FORMAT;
  if (ST.isAmdHsaOS()) {
    // Set ATC = 1. GFX9 doesn't have this bit.
    if (ST.getGeneration() <= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      RsrcDataFormat |= (1ULL << 56);

    // Set MTYPE = 2 (MTYPE_UC = uncached). GFX9 doesn't have this.
    // BTW, it disables TC L2 and therefore decreases performance.
    if (ST.getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS)
      RsrcDataFormat |= (2ULL << 59);
  }

  return RsrcDataFormat;
}

uint64_t SIInstrInfo::getScratchRsrcWords23() const {
  uint64_t Rsrc23 = getDefaultRsrcDataFormat() |
                    AMDGPU::RSRC_TID_ENABLE |
                    0xffffffff; // Size;

  // GFX9 doesn't have ELEMENT_SIZE.
  if (ST.getGeneration() <= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
    uint64_t EltSizeValue = Log2_32(ST.getMaxPrivateElementSize()) - 1;
    Rsrc23 |= EltSizeValue << AMDGPU::RSRC_ELEMENT_SIZE_SHIFT;
  }

  // IndexStride = 64.
  Rsrc23 |= UINT64_C(3) << AMDGPU::RSRC_INDEX_STRIDE_SHIFT;

  // If TID_ENABLE is set, DATA_FORMAT specifies stride bits [14:17].
  // Clear them unless we want a huge stride.
  if (ST.getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
    Rsrc23 &= ~AMDGPU::RSRC_DATA_FORMAT;

  return Rsrc23;
}

bool SIInstrInfo::isLowLatencyInstruction(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  return isSMRD(Opc);
}

bool SIInstrInfo::isHighLatencyInstruction(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  return isMUBUF(Opc) || isMTBUF(Opc) || isMIMG(Opc);
}

unsigned SIInstrInfo::isStackAccess(const MachineInstr &MI,
                                    int &FrameIndex) const {
  const MachineOperand *Addr = getNamedOperand(MI, AMDGPU::OpName::vaddr);
  if (!Addr || !Addr->isFI())
    return AMDGPU::NoRegister;

  assert(!MI.memoperands_empty() &&
         (*MI.memoperands_begin())->getAddrSpace() == AMDGPUAS::PRIVATE_ADDRESS);

  FrameIndex = Addr->getIndex();
  return getNamedOperand(MI, AMDGPU::OpName::vdata)->getReg();
}

unsigned SIInstrInfo::isSGPRStackAccess(const MachineInstr &MI,
                                        int &FrameIndex) const {
  const MachineOperand *Addr = getNamedOperand(MI, AMDGPU::OpName::addr);
  assert(Addr && Addr->isFI());
  FrameIndex = Addr->getIndex();
  return getNamedOperand(MI, AMDGPU::OpName::data)->getReg();
}

unsigned SIInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  if (!MI.mayLoad())
    return AMDGPU::NoRegister;

  if (isMUBUF(MI) || isVGPRSpill(MI))
    return isStackAccess(MI, FrameIndex);

  if (isSGPRSpill(MI))
    return isSGPRStackAccess(MI, FrameIndex);

  return AMDGPU::NoRegister;
}

unsigned SIInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                         int &FrameIndex) const {
  if (!MI.mayStore())
    return AMDGPU::NoRegister;

  if (isMUBUF(MI) || isVGPRSpill(MI))
    return isStackAccess(MI, FrameIndex);

  if (isSGPRSpill(MI))
    return isSGPRStackAccess(MI, FrameIndex);

  return AMDGPU::NoRegister;
}

unsigned SIInstrInfo::getInstBundleSize(const MachineInstr &MI) const {
  unsigned Size = 0;
  MachineBasicBlock::const_instr_iterator I = MI.getIterator();
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
  while (++I != E && I->isInsideBundle()) {
    assert(!I->isBundle() && "No nested bundle!");
    Size += getInstSizeInBytes(*I);
  }

  return Size;
}

unsigned SIInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  const MCInstrDesc &Desc = getMCOpcodeFromPseudo(Opc);
  unsigned DescSize = Desc.getSize();

  // If we have a definitive size, we can use it. Otherwise we need to inspect
  // the operands to know the size.
  if (isFixedSize(MI))
    return DescSize;

  // 4-byte instructions may have a 32-bit literal encoded after them. Check
  // operands that coud ever be literals.
  if (isVALU(MI) || isSALU(MI)) {
    int Src0Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0);
    if (Src0Idx == -1)
      return DescSize; // No operands.

    if (isLiteralConstantLike(MI.getOperand(Src0Idx), Desc.OpInfo[Src0Idx]))
      return DescSize + 4;

    int Src1Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1);
    if (Src1Idx == -1)
      return DescSize;

    if (isLiteralConstantLike(MI.getOperand(Src1Idx), Desc.OpInfo[Src1Idx]))
      return DescSize + 4;

    int Src2Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);
    if (Src2Idx == -1)
      return DescSize;

    if (isLiteralConstantLike(MI.getOperand(Src2Idx), Desc.OpInfo[Src2Idx]))
      return DescSize + 4;

    return DescSize;
  }

  switch (Opc) {
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE:
  case TargetOpcode::EH_LABEL:
    return 0;
  case TargetOpcode::BUNDLE:
    return getInstBundleSize(MI);
  case TargetOpcode::INLINEASM: {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  default:
    return DescSize;
  }
}

bool SIInstrInfo::mayAccessFlatAddressSpace(const MachineInstr &MI) const {
  if (!isFLAT(MI))
    return false;

  if (MI.memoperands_empty())
    return true;

  for (const MachineMemOperand *MMO : MI.memoperands()) {
    if (MMO->getAddrSpace() == AMDGPUAS::FLAT_ADDRESS)
      return true;
  }
  return false;
}

bool SIInstrInfo::isNonUniformBranchInstr(MachineInstr &Branch) const {
  return Branch.getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO;
}

void SIInstrInfo::convertNonUniformIfRegion(MachineBasicBlock *IfEntry,
                                            MachineBasicBlock *IfEnd) const {
  MachineBasicBlock::iterator TI = IfEntry->getFirstTerminator();
  assert(TI != IfEntry->end());

  MachineInstr *Branch = &(*TI);
  MachineFunction *MF = IfEntry->getParent();
  MachineRegisterInfo &MRI = IfEntry->getParent()->getRegInfo();

  if (Branch->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {
    unsigned DstReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
    MachineInstr *SIIF =
        BuildMI(*MF, Branch->getDebugLoc(), get(AMDGPU::SI_IF), DstReg)
            .add(Branch->getOperand(0))
            .add(Branch->getOperand(1));
    MachineInstr *SIEND =
        BuildMI(*MF, Branch->getDebugLoc(), get(AMDGPU::SI_END_CF))
            .addReg(DstReg);

    IfEntry->erase(TI);
    IfEntry->insert(IfEntry->end(), SIIF);
    IfEnd->insert(IfEnd->getFirstNonPHI(), SIEND);
  }
}

void SIInstrInfo::convertNonUniformLoopRegion(
    MachineBasicBlock *LoopEntry, MachineBasicBlock *LoopEnd) const {
  MachineBasicBlock::iterator TI = LoopEnd->getFirstTerminator();
  // We expect 2 terminators, one conditional and one unconditional.
  assert(TI != LoopEnd->end());

  MachineInstr *Branch = &(*TI);
  MachineFunction *MF = LoopEnd->getParent();
  MachineRegisterInfo &MRI = LoopEnd->getParent()->getRegInfo();

  if (Branch->getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO) {

    unsigned DstReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
    unsigned BackEdgeReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
    MachineInstrBuilder HeaderPHIBuilder =
        BuildMI(*(MF), Branch->getDebugLoc(), get(TargetOpcode::PHI), DstReg);
    for (MachineBasicBlock::pred_iterator PI = LoopEntry->pred_begin(),
                                          E = LoopEntry->pred_end();
         PI != E; ++PI) {
      if (*PI == LoopEnd) {
        HeaderPHIBuilder.addReg(BackEdgeReg);
      } else {
        MachineBasicBlock *PMBB = *PI;
        unsigned ZeroReg = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
        materializeImmediate(*PMBB, PMBB->getFirstTerminator(), DebugLoc(),
                             ZeroReg, 0);
        HeaderPHIBuilder.addReg(ZeroReg);
      }
      HeaderPHIBuilder.addMBB(*PI);
    }
    MachineInstr *HeaderPhi = HeaderPHIBuilder;
    MachineInstr *SIIFBREAK = BuildMI(*(MF), Branch->getDebugLoc(),
                                      get(AMDGPU::SI_IF_BREAK), BackEdgeReg)
                                  .addReg(DstReg)
                                  .add(Branch->getOperand(0));
    MachineInstr *SILOOP =
        BuildMI(*(MF), Branch->getDebugLoc(), get(AMDGPU::SI_LOOP))
            .addReg(BackEdgeReg)
            .addMBB(LoopEntry);

    LoopEntry->insert(LoopEntry->begin(), HeaderPhi);
    LoopEnd->erase(TI);
    LoopEnd->insert(LoopEnd->end(), SIIFBREAK);
    LoopEnd->insert(LoopEnd->end(), SILOOP);
  }
}

ArrayRef<std::pair<int, const char *>>
SIInstrInfo::getSerializableTargetIndices() const {
  static const std::pair<int, const char *> TargetIndices[] = {
      {AMDGPU::TI_CONSTDATA_START, "amdgpu-constdata-start"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD0, "amdgpu-scratch-rsrc-dword0"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD1, "amdgpu-scratch-rsrc-dword1"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD2, "amdgpu-scratch-rsrc-dword2"},
      {AMDGPU::TI_SCRATCH_RSRC_DWORD3, "amdgpu-scratch-rsrc-dword3"}};
  return makeArrayRef(TargetIndices);
}

/// This is used by the post-RA scheduler (SchedulePostRAList.cpp).  The
/// post-RA version of misched uses CreateTargetMIHazardRecognizer.
ScheduleHazardRecognizer *
SIInstrInfo::CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                            const ScheduleDAG *DAG) const {
  return new GCNHazardRecognizer(DAG->MF);
}

/// This is the hazard recognizer used at -O0 by the PostRAHazardRecognizer
/// pass.
ScheduleHazardRecognizer *
SIInstrInfo::CreateTargetPostRAHazardRecognizer(const MachineFunction &MF) const {
  return new GCNHazardRecognizer(MF);
}

std::pair<unsigned, unsigned>
SIInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::make_pair(TF & MO_MASK, TF & ~MO_MASK);
}

ArrayRef<std::pair<unsigned, const char *>>
SIInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  static const std::pair<unsigned, const char *> TargetFlags[] = {
    { MO_GOTPCREL, "amdgpu-gotprel" },
    { MO_GOTPCREL32_LO, "amdgpu-gotprel32-lo" },
    { MO_GOTPCREL32_HI, "amdgpu-gotprel32-hi" },
    { MO_REL32_LO, "amdgpu-rel32-lo" },
    { MO_REL32_HI, "amdgpu-rel32-hi" }
  };

  return makeArrayRef(TargetFlags);
}

bool SIInstrInfo::isBasicBlockPrologue(const MachineInstr &MI) const {
  return !MI.isTerminator() && MI.getOpcode() != AMDGPU::COPY &&
         MI.modifiesRegister(AMDGPU::EXEC, &RI);
}

MachineInstrBuilder
SIInstrInfo::getAddNoCarry(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I,
                           const DebugLoc &DL,
                           unsigned DestReg) const {
  if (ST.hasAddNoCarry())
    return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_U32_e64), DestReg);

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  unsigned UnusedCarry = MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
  MRI.setRegAllocationHint(UnusedCarry, 0, AMDGPU::VCC);

  return BuildMI(MBB, I, DL, get(AMDGPU::V_ADD_I32_e64), DestReg)
           .addReg(UnusedCarry, RegState::Define | RegState::Dead);
}

bool SIInstrInfo::isKillTerminator(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
  case AMDGPU::SI_KILL_I1_TERMINATOR:
    return true;
  default:
    return false;
  }
}

const MCInstrDesc &SIInstrInfo::getKillTerminatorFromPseudo(unsigned Opcode) const {
  switch (Opcode) {
  case AMDGPU::SI_KILL_F32_COND_IMM_PSEUDO:
    return get(AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR);
  case AMDGPU::SI_KILL_I1_PSEUDO:
    return get(AMDGPU::SI_KILL_I1_TERMINATOR);
  default:
    llvm_unreachable("invalid opcode, expected SI_KILL_*_PSEUDO");
  }
}

bool SIInstrInfo::isBufferSMRD(const MachineInstr &MI) const {
  if (!isSMRD(MI))
    return false;

  // Check that it is using a buffer resource.
  int Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::sbase);
  if (Idx == -1) // e.g. s_memtime
    return false;

  const auto RCID = MI.getDesc().OpInfo[Idx].RegClass;
  return RCID == AMDGPU::SReg_128RegClassID;
}

// This must be kept in sync with the SIEncodingFamily class in SIInstrInfo.td
enum SIEncodingFamily {
  SI = 0,
  VI = 1,
  SDWA = 2,
  SDWA9 = 3,
  GFX80 = 4,
  GFX9 = 5
};

static SIEncodingFamily subtargetEncodingFamily(const GCNSubtarget &ST) {
  switch (ST.getGeneration()) {
  default:
    break;
  case AMDGPUSubtarget::SOUTHERN_ISLANDS:
  case AMDGPUSubtarget::SEA_ISLANDS:
    return SIEncodingFamily::SI;
  case AMDGPUSubtarget::VOLCANIC_ISLANDS:
  case AMDGPUSubtarget::GFX9:
    return SIEncodingFamily::VI;
  }
  llvm_unreachable("Unknown subtarget generation!");
}

int SIInstrInfo::pseudoToMCOpcode(int Opcode) const {
  SIEncodingFamily Gen = subtargetEncodingFamily(ST);

  if ((get(Opcode).TSFlags & SIInstrFlags::renamedInGFX9) != 0 &&
    ST.getGeneration() >= AMDGPUSubtarget::GFX9)
    Gen = SIEncodingFamily::GFX9;

  if (get(Opcode).TSFlags & SIInstrFlags::SDWA)
    Gen = ST.getGeneration() == AMDGPUSubtarget::GFX9 ? SIEncodingFamily::SDWA9
                                                      : SIEncodingFamily::SDWA;
  // Adjust the encoding family to GFX80 for D16 buffer instructions when the
  // subtarget has UnpackedD16VMem feature.
  // TODO: remove this when we discard GFX80 encoding.
  if (ST.hasUnpackedD16VMem() && (get(Opcode).TSFlags & SIInstrFlags::D16Buf))
    Gen = SIEncodingFamily::GFX80;

  int MCOp = AMDGPU::getMCOpcode(Opcode, Gen);

  // -1 means that Opcode is already a native instruction.
  if (MCOp == -1)
    return Opcode;

  // (uint16_t)-1 means that Opcode is a pseudo instruction that has
  // no encoding in the given subtarget generation.
  if (MCOp == (uint16_t)-1)
    return -1;

  return MCOp;
}

static
TargetInstrInfo::RegSubRegPair getRegOrUndef(const MachineOperand &RegOpnd) {
  assert(RegOpnd.isReg());
  return RegOpnd.isUndef() ? TargetInstrInfo::RegSubRegPair() :
                             getRegSubRegPair(RegOpnd);
}

TargetInstrInfo::RegSubRegPair
llvm::getRegSequenceSubReg(MachineInstr &MI, unsigned SubReg) {
  assert(MI.isRegSequence());
  for (unsigned I = 0, E = (MI.getNumOperands() - 1)/ 2; I < E; ++I)
    if (MI.getOperand(1 + 2 * I + 1).getImm() == SubReg) {
      auto &RegOp = MI.getOperand(1 + 2 * I);
      return getRegOrUndef(RegOp);
    }
  return TargetInstrInfo::RegSubRegPair();
}

// Try to find the definition of reg:subreg in subreg-manipulation pseudos
// Following a subreg of reg:subreg isn't supported
static bool followSubRegDef(MachineInstr &MI,
                            TargetInstrInfo::RegSubRegPair &RSR) {
  if (!RSR.SubReg)
    return false;
  switch (MI.getOpcode()) {
  default: break;
  case AMDGPU::REG_SEQUENCE:
    RSR = getRegSequenceSubReg(MI, RSR.SubReg);
    return true;
  // EXTRACT_SUBREG ins't supported as this would follow a subreg of subreg
  case AMDGPU::INSERT_SUBREG:
    if (RSR.SubReg == (unsigned)MI.getOperand(3).getImm())
      // inserted the subreg we're looking for
      RSR = getRegOrUndef(MI.getOperand(2));
    else { // the subreg in the rest of the reg
      auto R1 = getRegOrUndef(MI.getOperand(1));
      if (R1.SubReg) // subreg of subreg isn't supported
        return false;
      RSR.Reg = R1.Reg;
    }
    return true;
  }
  return false;
}

MachineInstr *llvm::getVRegSubRegDef(const TargetInstrInfo::RegSubRegPair &P,
                                     MachineRegisterInfo &MRI) {
  assert(MRI.isSSA());
  if (!TargetRegisterInfo::isVirtualRegister(P.Reg))
    return nullptr;

  auto RSR = P;
  auto *DefInst = MRI.getVRegDef(RSR.Reg);
  while (auto *MI = DefInst) {
    DefInst = nullptr;
    switch (MI->getOpcode()) {
    case AMDGPU::COPY:
    case AMDGPU::V_MOV_B32_e32: {
      auto &Op1 = MI->getOperand(1);
      if (Op1.isReg() &&
        TargetRegisterInfo::isVirtualRegister(Op1.getReg())) {
        if (Op1.isUndef())
          return nullptr;
        RSR = getRegSubRegPair(Op1);
        DefInst = MRI.getVRegDef(RSR.Reg);
      }
      break;
    }
    default:
      if (followSubRegDef(*MI, RSR)) {
        if (!RSR.Reg)
          return nullptr;
        DefInst = MRI.getVRegDef(RSR.Reg);
      }
    }
    if (!DefInst)
      return MI;
  }
  return nullptr;
}
