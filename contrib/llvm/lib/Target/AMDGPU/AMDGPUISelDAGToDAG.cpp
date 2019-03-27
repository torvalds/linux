//===-- AMDGPUISelDAGToDAG.cpp - A dag to dag inst selector for AMDGPU ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// Defines an instruction selector for the AMDGPU target.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUArgumentUsageInfo.h"
#include "AMDGPUISelLowering.h" // For AMDGPUISD
#include "AMDGPUInstrInfo.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "AMDGPURegisterInfo.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "SIDefines.h"
#include "SIISelLowering.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <new>
#include <vector>

using namespace llvm;

namespace llvm {

class R600InstrInfo;

} // end namespace llvm

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

namespace {

/// AMDGPU specific code to select AMDGPU machine instructions for
/// SelectionDAG operations.
class AMDGPUDAGToDAGISel : public SelectionDAGISel {
  // Subtarget - Keep a pointer to the AMDGPU Subtarget around so that we can
  // make the right decision when generating code for different targets.
  const GCNSubtarget *Subtarget;
  bool EnableLateStructurizeCFG;

public:
  explicit AMDGPUDAGToDAGISel(TargetMachine *TM = nullptr,
                              CodeGenOpt::Level OptLevel = CodeGenOpt::Default)
    : SelectionDAGISel(*TM, OptLevel) {
    EnableLateStructurizeCFG = AMDGPUTargetMachine::EnableLateStructurizeCFG;
  }
  ~AMDGPUDAGToDAGISel() override = default;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AMDGPUArgumentUsageInfo>();
    AU.addRequired<AMDGPUPerfHintAnalysis>();
    AU.addRequired<LegacyDivergenceAnalysis>();
    SelectionDAGISel::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void Select(SDNode *N) override;
  StringRef getPassName() const override;
  void PostprocessISelDAG() override;

protected:
  void SelectBuildVector(SDNode *N, unsigned RegClassID);

private:
  std::pair<SDValue, SDValue> foldFrameIndex(SDValue N) const;
  bool isNoNanSrc(SDValue N) const;
  bool isInlineImmediate(const SDNode *N) const;
  bool isVGPRImm(const SDNode *N) const;
  bool isUniformLoad(const SDNode *N) const;
  bool isUniformBr(const SDNode *N) const;

  MachineSDNode *buildSMovImm64(SDLoc &DL, uint64_t Val, EVT VT) const;

  SDNode *glueCopyToM0(SDNode *N) const;

  const TargetRegisterClass *getOperandRegClass(SDNode *N, unsigned OpNo) const;
  virtual bool SelectADDRVTX_READ(SDValue Addr, SDValue &Base, SDValue &Offset);
  virtual bool SelectADDRIndirect(SDValue Addr, SDValue &Base, SDValue &Offset);
  bool isDSOffsetLegal(const SDValue &Base, unsigned Offset,
                       unsigned OffsetBits) const;
  bool SelectDS1Addr1Offset(SDValue Ptr, SDValue &Base, SDValue &Offset) const;
  bool SelectDS64Bit4ByteAligned(SDValue Ptr, SDValue &Base, SDValue &Offset0,
                                 SDValue &Offset1) const;
  bool SelectMUBUF(SDValue Addr, SDValue &SRsrc, SDValue &VAddr,
                   SDValue &SOffset, SDValue &Offset, SDValue &Offen,
                   SDValue &Idxen, SDValue &Addr64, SDValue &GLC, SDValue &SLC,
                   SDValue &TFE) const;
  bool SelectMUBUFAddr64(SDValue Addr, SDValue &SRsrc, SDValue &VAddr,
                         SDValue &SOffset, SDValue &Offset, SDValue &GLC,
                         SDValue &SLC, SDValue &TFE) const;
  bool SelectMUBUFAddr64(SDValue Addr, SDValue &SRsrc,
                         SDValue &VAddr, SDValue &SOffset, SDValue &Offset,
                         SDValue &SLC) const;
  bool SelectMUBUFScratchOffen(SDNode *Parent,
                               SDValue Addr, SDValue &RSrc, SDValue &VAddr,
                               SDValue &SOffset, SDValue &ImmOffset) const;
  bool SelectMUBUFScratchOffset(SDNode *Parent,
                                SDValue Addr, SDValue &SRsrc, SDValue &Soffset,
                                SDValue &Offset) const;

  bool SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc, SDValue &SOffset,
                         SDValue &Offset, SDValue &GLC, SDValue &SLC,
                         SDValue &TFE) const;
  bool SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc, SDValue &Soffset,
                         SDValue &Offset, SDValue &SLC) const;
  bool SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc, SDValue &Soffset,
                         SDValue &Offset) const;

  bool SelectFlatAtomic(SDValue Addr, SDValue &VAddr,
                        SDValue &Offset, SDValue &SLC) const;
  bool SelectFlatAtomicSigned(SDValue Addr, SDValue &VAddr,
                              SDValue &Offset, SDValue &SLC) const;

  template <bool IsSigned>
  bool SelectFlatOffset(SDValue Addr, SDValue &VAddr,
                        SDValue &Offset, SDValue &SLC) const;

  bool SelectSMRDOffset(SDValue ByteOffsetNode, SDValue &Offset,
                        bool &Imm) const;
  SDValue Expand32BitAddress(SDValue Addr) const;
  bool SelectSMRD(SDValue Addr, SDValue &SBase, SDValue &Offset,
                  bool &Imm) const;
  bool SelectSMRDImm(SDValue Addr, SDValue &SBase, SDValue &Offset) const;
  bool SelectSMRDImm32(SDValue Addr, SDValue &SBase, SDValue &Offset) const;
  bool SelectSMRDSgpr(SDValue Addr, SDValue &SBase, SDValue &Offset) const;
  bool SelectSMRDBufferImm(SDValue Addr, SDValue &Offset) const;
  bool SelectSMRDBufferImm32(SDValue Addr, SDValue &Offset) const;
  bool SelectMOVRELOffset(SDValue Index, SDValue &Base, SDValue &Offset) const;

  bool SelectVOP3Mods_NNaN(SDValue In, SDValue &Src, SDValue &SrcMods) const;
  bool SelectVOP3ModsImpl(SDValue In, SDValue &Src, unsigned &SrcMods) const;
  bool SelectVOP3Mods(SDValue In, SDValue &Src, SDValue &SrcMods) const;
  bool SelectVOP3NoMods(SDValue In, SDValue &Src) const;
  bool SelectVOP3Mods0(SDValue In, SDValue &Src, SDValue &SrcMods,
                       SDValue &Clamp, SDValue &Omod) const;
  bool SelectVOP3NoMods0(SDValue In, SDValue &Src, SDValue &SrcMods,
                         SDValue &Clamp, SDValue &Omod) const;

  bool SelectVOP3Mods0Clamp0OMod(SDValue In, SDValue &Src, SDValue &SrcMods,
                                 SDValue &Clamp,
                                 SDValue &Omod) const;

  bool SelectVOP3OMods(SDValue In, SDValue &Src,
                       SDValue &Clamp, SDValue &Omod) const;

  bool SelectVOP3PMods(SDValue In, SDValue &Src, SDValue &SrcMods) const;
  bool SelectVOP3PMods0(SDValue In, SDValue &Src, SDValue &SrcMods,
                        SDValue &Clamp) const;

  bool SelectVOP3OpSel(SDValue In, SDValue &Src, SDValue &SrcMods) const;
  bool SelectVOP3OpSel0(SDValue In, SDValue &Src, SDValue &SrcMods,
                        SDValue &Clamp) const;

  bool SelectVOP3OpSelMods(SDValue In, SDValue &Src, SDValue &SrcMods) const;
  bool SelectVOP3OpSelMods0(SDValue In, SDValue &Src, SDValue &SrcMods,
                            SDValue &Clamp) const;
  bool SelectVOP3PMadMixModsImpl(SDValue In, SDValue &Src, unsigned &Mods) const;
  bool SelectVOP3PMadMixMods(SDValue In, SDValue &Src, SDValue &SrcMods) const;

  bool SelectHi16Elt(SDValue In, SDValue &Src) const;

  void SelectADD_SUB_I64(SDNode *N);
  void SelectUADDO_USUBO(SDNode *N);
  void SelectDIV_SCALE(SDNode *N);
  void SelectMAD_64_32(SDNode *N);
  void SelectFMA_W_CHAIN(SDNode *N);
  void SelectFMUL_W_CHAIN(SDNode *N);

  SDNode *getS_BFE(unsigned Opcode, const SDLoc &DL, SDValue Val,
                   uint32_t Offset, uint32_t Width);
  void SelectS_BFEFromShifts(SDNode *N);
  void SelectS_BFE(SDNode *N);
  bool isCBranchSCC(const SDNode *N) const;
  void SelectBRCOND(SDNode *N);
  void SelectFMAD_FMA(SDNode *N);
  void SelectATOMIC_CMP_SWAP(SDNode *N);

protected:
  // Include the pieces autogenerated from the target description.
#include "AMDGPUGenDAGISel.inc"
};

class R600DAGToDAGISel : public AMDGPUDAGToDAGISel {
  const R600Subtarget *Subtarget;

  bool isConstantLoad(const MemSDNode *N, int cbID) const;
  bool SelectGlobalValueConstantOffset(SDValue Addr, SDValue& IntPtr);
  bool SelectGlobalValueVariableOffset(SDValue Addr, SDValue &BaseReg,
                                       SDValue& Offset);
public:
  explicit R600DAGToDAGISel(TargetMachine *TM, CodeGenOpt::Level OptLevel) :
      AMDGPUDAGToDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

  bool SelectADDRIndirect(SDValue Addr, SDValue &Base,
                          SDValue &Offset) override;
  bool SelectADDRVTX_READ(SDValue Addr, SDValue &Base,
                          SDValue &Offset) override;

  bool runOnMachineFunction(MachineFunction &MF) override;
protected:
  // Include the pieces autogenerated from the target description.
#include "R600GenDAGISel.inc"
};

}  // end anonymous namespace

INITIALIZE_PASS_BEGIN(AMDGPUDAGToDAGISel, "amdgpu-isel",
                      "AMDGPU DAG->DAG Pattern Instruction Selection", false, false)
INITIALIZE_PASS_DEPENDENCY(AMDGPUArgumentUsageInfo)
INITIALIZE_PASS_DEPENDENCY(AMDGPUPerfHintAnalysis)
INITIALIZE_PASS_DEPENDENCY(LegacyDivergenceAnalysis)
INITIALIZE_PASS_END(AMDGPUDAGToDAGISel, "amdgpu-isel",
                    "AMDGPU DAG->DAG Pattern Instruction Selection", false, false)

/// This pass converts a legalized DAG into a AMDGPU-specific
// DAG, ready for instruction scheduling.
FunctionPass *llvm::createAMDGPUISelDag(TargetMachine *TM,
                                        CodeGenOpt::Level OptLevel) {
  return new AMDGPUDAGToDAGISel(TM, OptLevel);
}

/// This pass converts a legalized DAG into a R600-specific
// DAG, ready for instruction scheduling.
FunctionPass *llvm::createR600ISelDag(TargetMachine *TM,
                                      CodeGenOpt::Level OptLevel) {
  return new R600DAGToDAGISel(TM, OptLevel);
}

bool AMDGPUDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<GCNSubtarget>();
  return SelectionDAGISel::runOnMachineFunction(MF);
}

bool AMDGPUDAGToDAGISel::isNoNanSrc(SDValue N) const {
  if (TM.Options.NoNaNsFPMath)
    return true;

  // TODO: Move into isKnownNeverNaN
  if (N->getFlags().isDefined())
    return N->getFlags().hasNoNaNs();

  return CurDAG->isKnownNeverNaN(N);
}

bool AMDGPUDAGToDAGISel::isInlineImmediate(const SDNode *N) const {
  const SIInstrInfo *TII = Subtarget->getInstrInfo();

  if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N))
    return TII->isInlineConstant(C->getAPIntValue());

  if (const ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(N))
    return TII->isInlineConstant(C->getValueAPF().bitcastToAPInt());

  return false;
}

/// Determine the register class for \p OpNo
/// \returns The register class of the virtual register that will be used for
/// the given operand number \OpNo or NULL if the register class cannot be
/// determined.
const TargetRegisterClass *AMDGPUDAGToDAGISel::getOperandRegClass(SDNode *N,
                                                          unsigned OpNo) const {
  if (!N->isMachineOpcode()) {
    if (N->getOpcode() == ISD::CopyToReg) {
      unsigned Reg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        MachineRegisterInfo &MRI = CurDAG->getMachineFunction().getRegInfo();
        return MRI.getRegClass(Reg);
      }

      const SIRegisterInfo *TRI
        = static_cast<const GCNSubtarget *>(Subtarget)->getRegisterInfo();
      return TRI->getPhysRegClass(Reg);
    }

    return nullptr;
  }

  switch (N->getMachineOpcode()) {
  default: {
    const MCInstrDesc &Desc =
        Subtarget->getInstrInfo()->get(N->getMachineOpcode());
    unsigned OpIdx = Desc.getNumDefs() + OpNo;
    if (OpIdx >= Desc.getNumOperands())
      return nullptr;
    int RegClass = Desc.OpInfo[OpIdx].RegClass;
    if (RegClass == -1)
      return nullptr;

    return Subtarget->getRegisterInfo()->getRegClass(RegClass);
  }
  case AMDGPU::REG_SEQUENCE: {
    unsigned RCID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
    const TargetRegisterClass *SuperRC =
        Subtarget->getRegisterInfo()->getRegClass(RCID);

    SDValue SubRegOp = N->getOperand(OpNo + 1);
    unsigned SubRegIdx = cast<ConstantSDNode>(SubRegOp)->getZExtValue();
    return Subtarget->getRegisterInfo()->getSubClassWithSubReg(SuperRC,
                                                              SubRegIdx);
  }
  }
}

SDNode *AMDGPUDAGToDAGISel::glueCopyToM0(SDNode *N) const {
  if (cast<MemSDNode>(N)->getAddressSpace() != AMDGPUAS::LOCAL_ADDRESS ||
      !Subtarget->ldsRequiresM0Init())
    return N;

  const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());

  // Write max value to m0 before each load operation

  SDValue M0 = Lowering.copyToM0(*CurDAG, CurDAG->getEntryNode(), SDLoc(N),
                                 CurDAG->getTargetConstant(-1, SDLoc(N), MVT::i32));

  SDValue Glue = M0.getValue(1);

  SmallVector <SDValue, 8> Ops;
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
     Ops.push_back(N->getOperand(i));
  }
  Ops.push_back(Glue);
  return CurDAG->MorphNodeTo(N, N->getOpcode(), N->getVTList(), Ops);
}

MachineSDNode *AMDGPUDAGToDAGISel::buildSMovImm64(SDLoc &DL, uint64_t Imm,
                                                  EVT VT) const {
  SDNode *Lo = CurDAG->getMachineNode(
      AMDGPU::S_MOV_B32, DL, MVT::i32,
      CurDAG->getConstant(Imm & 0xFFFFFFFF, DL, MVT::i32));
  SDNode *Hi =
      CurDAG->getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32,
                             CurDAG->getConstant(Imm >> 32, DL, MVT::i32));
  const SDValue Ops[] = {
      CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32),
      SDValue(Lo, 0), CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32),
      SDValue(Hi, 0), CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32)};

  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, VT, Ops);
}

static unsigned selectSGPRVectorRegClassID(unsigned NumVectorElts) {
  switch (NumVectorElts) {
  case 1:
    return AMDGPU::SReg_32_XM0RegClassID;
  case 2:
    return AMDGPU::SReg_64RegClassID;
  case 4:
    return AMDGPU::SReg_128RegClassID;
  case 8:
    return AMDGPU::SReg_256RegClassID;
  case 16:
    return AMDGPU::SReg_512RegClassID;
  }

  llvm_unreachable("invalid vector size");
}

static bool getConstantValue(SDValue N, uint32_t &Out) {
  if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N)) {
    Out = C->getAPIntValue().getZExtValue();
    return true;
  }

  if (const ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(N)) {
    Out = C->getValueAPF().bitcastToAPInt().getZExtValue();
    return true;
  }

  return false;
}

void AMDGPUDAGToDAGISel::SelectBuildVector(SDNode *N, unsigned RegClassID) {
  EVT VT = N->getValueType(0);
  unsigned NumVectorElts = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();
  SDLoc DL(N);
  SDValue RegClass = CurDAG->getTargetConstant(RegClassID, DL, MVT::i32);

  if (NumVectorElts == 1) {
    CurDAG->SelectNodeTo(N, AMDGPU::COPY_TO_REGCLASS, EltVT, N->getOperand(0),
                         RegClass);
    return;
  }

  assert(NumVectorElts <= 16 && "Vectors with more than 16 elements not "
                                  "supported yet");
  // 16 = Max Num Vector Elements
  // 2 = 2 REG_SEQUENCE operands per element (value, subreg index)
  // 1 = Vector Register Class
  SmallVector<SDValue, 16 * 2 + 1> RegSeqArgs(NumVectorElts * 2 + 1);

  RegSeqArgs[0] = CurDAG->getTargetConstant(RegClassID, DL, MVT::i32);
  bool IsRegSeq = true;
  unsigned NOps = N->getNumOperands();
  for (unsigned i = 0; i < NOps; i++) {
    // XXX: Why is this here?
    if (isa<RegisterSDNode>(N->getOperand(i))) {
      IsRegSeq = false;
      break;
    }
    unsigned Sub = AMDGPURegisterInfo::getSubRegFromChannel(i);
    RegSeqArgs[1 + (2 * i)] = N->getOperand(i);
    RegSeqArgs[1 + (2 * i) + 1] = CurDAG->getTargetConstant(Sub, DL, MVT::i32);
  }
  if (NOps != NumVectorElts) {
    // Fill in the missing undef elements if this was a scalar_to_vector.
    assert(N->getOpcode() == ISD::SCALAR_TO_VECTOR && NOps < NumVectorElts);
    MachineSDNode *ImpDef = CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF,
                                                   DL, EltVT);
    for (unsigned i = NOps; i < NumVectorElts; ++i) {
      unsigned Sub = AMDGPURegisterInfo::getSubRegFromChannel(i);
      RegSeqArgs[1 + (2 * i)] = SDValue(ImpDef, 0);
      RegSeqArgs[1 + (2 * i) + 1] =
          CurDAG->getTargetConstant(Sub, DL, MVT::i32);
    }
  }

  if (!IsRegSeq)
    SelectCode(N);
  CurDAG->SelectNodeTo(N, AMDGPU::REG_SEQUENCE, N->getVTList(), RegSeqArgs);
}

void AMDGPUDAGToDAGISel::Select(SDNode *N) {
  unsigned int Opc = N->getOpcode();
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;   // Already selected.
  }

  if (isa<AtomicSDNode>(N) ||
      (Opc == AMDGPUISD::ATOMIC_INC || Opc == AMDGPUISD::ATOMIC_DEC ||
       Opc == AMDGPUISD::ATOMIC_LOAD_FADD ||
       Opc == AMDGPUISD::ATOMIC_LOAD_FMIN ||
       Opc == AMDGPUISD::ATOMIC_LOAD_FMAX))
    N = glueCopyToM0(N);

  switch (Opc) {
  default:
    break;
  // We are selecting i64 ADD here instead of custom lower it during
  // DAG legalization, so we can fold some i64 ADDs used for address
  // calculation into the LOAD and STORE instructions.
  case ISD::ADDC:
  case ISD::ADDE:
  case ISD::SUBC:
  case ISD::SUBE: {
    if (N->getValueType(0) != MVT::i64)
      break;

    SelectADD_SUB_I64(N);
    return;
  }
  case ISD::UADDO:
  case ISD::USUBO: {
    SelectUADDO_USUBO(N);
    return;
  }
  case AMDGPUISD::FMUL_W_CHAIN: {
    SelectFMUL_W_CHAIN(N);
    return;
  }
  case AMDGPUISD::FMA_W_CHAIN: {
    SelectFMA_W_CHAIN(N);
    return;
  }

  case ISD::SCALAR_TO_VECTOR:
  case ISD::BUILD_VECTOR: {
    EVT VT = N->getValueType(0);
    unsigned NumVectorElts = VT.getVectorNumElements();
    if (VT.getScalarSizeInBits() == 16) {
      if (Opc == ISD::BUILD_VECTOR && NumVectorElts == 2) {
        uint32_t LHSVal, RHSVal;
        if (getConstantValue(N->getOperand(0), LHSVal) &&
            getConstantValue(N->getOperand(1), RHSVal)) {
          uint32_t K = LHSVal | (RHSVal << 16);
          CurDAG->SelectNodeTo(N, AMDGPU::S_MOV_B32, VT,
                               CurDAG->getTargetConstant(K, SDLoc(N), MVT::i32));
          return;
        }
      }

      break;
    }

    assert(VT.getVectorElementType().bitsEq(MVT::i32));
    unsigned RegClassID = selectSGPRVectorRegClassID(NumVectorElts);
    SelectBuildVector(N, RegClassID);
    return;
  }
  case ISD::BUILD_PAIR: {
    SDValue RC, SubReg0, SubReg1;
    SDLoc DL(N);
    if (N->getValueType(0) == MVT::i128) {
      RC = CurDAG->getTargetConstant(AMDGPU::SReg_128RegClassID, DL, MVT::i32);
      SubReg0 = CurDAG->getTargetConstant(AMDGPU::sub0_sub1, DL, MVT::i32);
      SubReg1 = CurDAG->getTargetConstant(AMDGPU::sub2_sub3, DL, MVT::i32);
    } else if (N->getValueType(0) == MVT::i64) {
      RC = CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32);
      SubReg0 = CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32);
      SubReg1 = CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32);
    } else {
      llvm_unreachable("Unhandled value type for BUILD_PAIR");
    }
    const SDValue Ops[] = { RC, N->getOperand(0), SubReg0,
                            N->getOperand(1), SubReg1 };
    ReplaceNode(N, CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL,
                                          N->getValueType(0), Ops));
    return;
  }

  case ISD::Constant:
  case ISD::ConstantFP: {
    if (N->getValueType(0).getSizeInBits() != 64 || isInlineImmediate(N))
      break;

    uint64_t Imm;
    if (ConstantFPSDNode *FP = dyn_cast<ConstantFPSDNode>(N))
      Imm = FP->getValueAPF().bitcastToAPInt().getZExtValue();
    else {
      ConstantSDNode *C = cast<ConstantSDNode>(N);
      Imm = C->getZExtValue();
    }

    SDLoc DL(N);
    ReplaceNode(N, buildSMovImm64(DL, Imm, N->getValueType(0)));
    return;
  }
  case ISD::LOAD:
  case ISD::STORE:
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE: {
    N = glueCopyToM0(N);
    break;
  }

  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    // There is a scalar version available, but unlike the vector version which
    // has a separate operand for the offset and width, the scalar version packs
    // the width and offset into a single operand. Try to move to the scalar
    // version if the offsets are constant, so that we can try to keep extended
    // loads of kernel arguments in SGPRs.

    // TODO: Technically we could try to pattern match scalar bitshifts of
    // dynamic values, but it's probably not useful.
    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!Offset)
      break;

    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(N->getOperand(2));
    if (!Width)
      break;

    bool Signed = Opc == AMDGPUISD::BFE_I32;

    uint32_t OffsetVal = Offset->getZExtValue();
    uint32_t WidthVal = Width->getZExtValue();

    ReplaceNode(N, getS_BFE(Signed ? AMDGPU::S_BFE_I32 : AMDGPU::S_BFE_U32,
                            SDLoc(N), N->getOperand(0), OffsetVal, WidthVal));
    return;
  }
  case AMDGPUISD::DIV_SCALE: {
    SelectDIV_SCALE(N);
    return;
  }
  case AMDGPUISD::MAD_I64_I32:
  case AMDGPUISD::MAD_U64_U32: {
    SelectMAD_64_32(N);
    return;
  }
  case ISD::CopyToReg: {
    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());
    N = Lowering.legalizeTargetIndependentNode(N, *CurDAG);
    break;
  }
  case ISD::AND:
  case ISD::SRL:
  case ISD::SRA:
  case ISD::SIGN_EXTEND_INREG:
    if (N->getValueType(0) != MVT::i32)
      break;

    SelectS_BFE(N);
    return;
  case ISD::BRCOND:
    SelectBRCOND(N);
    return;
  case ISD::FMAD:
  case ISD::FMA:
    SelectFMAD_FMA(N);
    return;
  case AMDGPUISD::ATOMIC_CMP_SWAP:
    SelectATOMIC_CMP_SWAP(N);
    return;
  case AMDGPUISD::CVT_PKRTZ_F16_F32:
  case AMDGPUISD::CVT_PKNORM_I16_F32:
  case AMDGPUISD::CVT_PKNORM_U16_F32:
  case AMDGPUISD::CVT_PK_U16_U32:
  case AMDGPUISD::CVT_PK_I16_I32: {
    // Hack around using a legal type if f16 is illegal.
    if (N->getValueType(0) == MVT::i32) {
      MVT NewVT = Opc == AMDGPUISD::CVT_PKRTZ_F16_F32 ? MVT::v2f16 : MVT::v2i16;
      N = CurDAG->MorphNodeTo(N, N->getOpcode(), CurDAG->getVTList(NewVT),
                              { N->getOperand(0), N->getOperand(1) });
      SelectCode(N);
      return;
    }
  }
  }

  SelectCode(N);
}

bool AMDGPUDAGToDAGISel::isUniformBr(const SDNode *N) const {
  const BasicBlock *BB = FuncInfo->MBB->getBasicBlock();
  const Instruction *Term = BB->getTerminator();
  return Term->getMetadata("amdgpu.uniform") ||
         Term->getMetadata("structurizecfg.uniform");
}

StringRef AMDGPUDAGToDAGISel::getPassName() const {
  return "AMDGPU DAG->DAG Pattern Instruction Selection";
}

//===----------------------------------------------------------------------===//
// Complex Patterns
//===----------------------------------------------------------------------===//

bool AMDGPUDAGToDAGISel::SelectADDRVTX_READ(SDValue Addr, SDValue &Base,
                                            SDValue &Offset) {
  return false;
}

bool AMDGPUDAGToDAGISel::SelectADDRIndirect(SDValue Addr, SDValue &Base,
                                            SDValue &Offset) {
  ConstantSDNode *C;
  SDLoc DL(Addr);

  if ((C = dyn_cast<ConstantSDNode>(Addr))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == AMDGPUISD::DWORDADDR) &&
             (C = dyn_cast<ConstantSDNode>(Addr.getOperand(0)))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == ISD::ADD || Addr.getOpcode() == ISD::OR) &&
            (C = dyn_cast<ConstantSDNode>(Addr.getOperand(1)))) {
    Base = Addr.getOperand(0);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else {
    Base = Addr;
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  }

  return true;
}

// FIXME: Should only handle addcarry/subcarry
void AMDGPUDAGToDAGISel::SelectADD_SUB_I64(SDNode *N) {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  unsigned Opcode = N->getOpcode();
  bool ConsumeCarry = (Opcode == ISD::ADDE || Opcode == ISD::SUBE);
  bool ProduceCarry =
      ConsumeCarry || Opcode == ISD::ADDC || Opcode == ISD::SUBC;
  bool IsAdd = Opcode == ISD::ADD || Opcode == ISD::ADDC || Opcode == ISD::ADDE;

  SDValue Sub0 = CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32);
  SDValue Sub1 = CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32);

  SDNode *Lo0 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, LHS, Sub0);
  SDNode *Hi0 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, LHS, Sub1);

  SDNode *Lo1 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, RHS, Sub0);
  SDNode *Hi1 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, RHS, Sub1);

  SDVTList VTList = CurDAG->getVTList(MVT::i32, MVT::Glue);

  unsigned Opc = IsAdd ? AMDGPU::S_ADD_U32 : AMDGPU::S_SUB_U32;
  unsigned CarryOpc = IsAdd ? AMDGPU::S_ADDC_U32 : AMDGPU::S_SUBB_U32;

  SDNode *AddLo;
  if (!ConsumeCarry) {
    SDValue Args[] = { SDValue(Lo0, 0), SDValue(Lo1, 0) };
    AddLo = CurDAG->getMachineNode(Opc, DL, VTList, Args);
  } else {
    SDValue Args[] = { SDValue(Lo0, 0), SDValue(Lo1, 0), N->getOperand(2) };
    AddLo = CurDAG->getMachineNode(CarryOpc, DL, VTList, Args);
  }
  SDValue AddHiArgs[] = {
    SDValue(Hi0, 0),
    SDValue(Hi1, 0),
    SDValue(AddLo, 1)
  };
  SDNode *AddHi = CurDAG->getMachineNode(CarryOpc, DL, VTList, AddHiArgs);

  SDValue RegSequenceArgs[] = {
    CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32),
    SDValue(AddLo,0),
    Sub0,
    SDValue(AddHi,0),
    Sub1,
  };
  SDNode *RegSequence = CurDAG->getMachineNode(AMDGPU::REG_SEQUENCE, DL,
                                               MVT::i64, RegSequenceArgs);

  if (ProduceCarry) {
    // Replace the carry-use
    ReplaceUses(SDValue(N, 1), SDValue(AddHi, 1));
  }

  // Replace the remaining uses.
  ReplaceNode(N, RegSequence);
}

void AMDGPUDAGToDAGISel::SelectUADDO_USUBO(SDNode *N) {
  // The name of the opcodes are misleading. v_add_i32/v_sub_i32 have unsigned
  // carry out despite the _i32 name. These were renamed in VI to _U32.
  // FIXME: We should probably rename the opcodes here.
  unsigned Opc = N->getOpcode() == ISD::UADDO ?
    AMDGPU::V_ADD_I32_e64 : AMDGPU::V_SUB_I32_e64;

  CurDAG->SelectNodeTo(N, Opc, N->getVTList(),
                       { N->getOperand(0), N->getOperand(1) });
}

void AMDGPUDAGToDAGISel::SelectFMA_W_CHAIN(SDNode *N) {
  SDLoc SL(N);
  //  src0_modifiers, src0,  src1_modifiers, src1, src2_modifiers, src2, clamp, omod
  SDValue Ops[10];

  SelectVOP3Mods0(N->getOperand(1), Ops[1], Ops[0], Ops[6], Ops[7]);
  SelectVOP3Mods(N->getOperand(2), Ops[3], Ops[2]);
  SelectVOP3Mods(N->getOperand(3), Ops[5], Ops[4]);
  Ops[8] = N->getOperand(0);
  Ops[9] = N->getOperand(4);

  CurDAG->SelectNodeTo(N, AMDGPU::V_FMA_F32, N->getVTList(), Ops);
}

void AMDGPUDAGToDAGISel::SelectFMUL_W_CHAIN(SDNode *N) {
  SDLoc SL(N);
  //    src0_modifiers, src0,  src1_modifiers, src1, clamp, omod
  SDValue Ops[8];

  SelectVOP3Mods0(N->getOperand(1), Ops[1], Ops[0], Ops[4], Ops[5]);
  SelectVOP3Mods(N->getOperand(2), Ops[3], Ops[2]);
  Ops[6] = N->getOperand(0);
  Ops[7] = N->getOperand(3);

  CurDAG->SelectNodeTo(N, AMDGPU::V_MUL_F32_e64, N->getVTList(), Ops);
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectDIV_SCALE(SDNode *N) {
  SDLoc SL(N);
  EVT VT = N->getValueType(0);

  assert(VT == MVT::f32 || VT == MVT::f64);

  unsigned Opc
    = (VT == MVT::f64) ? AMDGPU::V_DIV_SCALE_F64 : AMDGPU::V_DIV_SCALE_F32;

  SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2) };
  CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectMAD_64_32(SDNode *N) {
  SDLoc SL(N);
  bool Signed = N->getOpcode() == AMDGPUISD::MAD_I64_I32;
  unsigned Opc = Signed ? AMDGPU::V_MAD_I64_I32 : AMDGPU::V_MAD_U64_U32;

  SDValue Clamp = CurDAG->getTargetConstant(0, SL, MVT::i1);
  SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                    Clamp };
  CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
}

bool AMDGPUDAGToDAGISel::isDSOffsetLegal(const SDValue &Base, unsigned Offset,
                                         unsigned OffsetBits) const {
  if ((OffsetBits == 16 && !isUInt<16>(Offset)) ||
      (OffsetBits == 8 && !isUInt<8>(Offset)))
    return false;

  if (Subtarget->getGeneration() >= AMDGPUSubtarget::SEA_ISLANDS ||
      Subtarget->unsafeDSOffsetFoldingEnabled())
    return true;

  // On Southern Islands instruction with a negative base value and an offset
  // don't seem to work.
  return CurDAG->SignBitIsZero(Base);
}

bool AMDGPUDAGToDAGISel::SelectDS1Addr1Offset(SDValue Addr, SDValue &Base,
                                              SDValue &Offset) const {
  SDLoc DL(Addr);
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);
    if (isDSOffsetLegal(N0, C1->getSExtValue(), 16)) {
      // (add n0, c0)
      Base = N0;
      Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i16);
      return true;
    }
  } else if (Addr.getOpcode() == ISD::SUB) {
    // sub C, x -> add (sub 0, x), C
    if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Addr.getOperand(0))) {
      int64_t ByteOffset = C->getSExtValue();
      if (isUInt<16>(ByteOffset)) {
        SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);

        // XXX - This is kind of hacky. Create a dummy sub node so we can check
        // the known bits in isDSOffsetLegal. We need to emit the selected node
        // here, so this is thrown away.
        SDValue Sub = CurDAG->getNode(ISD::SUB, DL, MVT::i32,
                                      Zero, Addr.getOperand(1));

        if (isDSOffsetLegal(Sub, ByteOffset, 16)) {
          // FIXME: Select to VOP3 version for with-carry.
          unsigned SubOp = Subtarget->hasAddNoCarry() ?
            AMDGPU::V_SUB_U32_e64 : AMDGPU::V_SUB_I32_e32;

          MachineSDNode *MachineSub
            = CurDAG->getMachineNode(SubOp, DL, MVT::i32,
                                     Zero, Addr.getOperand(1));

          Base = SDValue(MachineSub, 0);
          Offset = CurDAG->getTargetConstant(ByteOffset, DL, MVT::i16);
          return true;
        }
      }
    }
  } else if (const ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    // If we have a constant address, prefer to put the constant into the
    // offset. This can save moves to load the constant address since multiple
    // operations can share the zero base address register, and enables merging
    // into read2 / write2 instructions.

    SDLoc DL(Addr);

    if (isUInt<16>(CAddr->getZExtValue())) {
      SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
      MachineSDNode *MovZero = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                 DL, MVT::i32, Zero);
      Base = SDValue(MovZero, 0);
      Offset = CurDAG->getTargetConstant(CAddr->getZExtValue(), DL, MVT::i16);
      return true;
    }
  }

  // default case
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i16);
  return true;
}

// TODO: If offset is too big, put low 16-bit into offset.
bool AMDGPUDAGToDAGISel::SelectDS64Bit4ByteAligned(SDValue Addr, SDValue &Base,
                                                   SDValue &Offset0,
                                                   SDValue &Offset1) const {
  SDLoc DL(Addr);

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);
    unsigned DWordOffset0 = C1->getZExtValue() / 4;
    unsigned DWordOffset1 = DWordOffset0 + 1;
    // (add n0, c0)
    if (isDSOffsetLegal(N0, DWordOffset1, 8)) {
      Base = N0;
      Offset0 = CurDAG->getTargetConstant(DWordOffset0, DL, MVT::i8);
      Offset1 = CurDAG->getTargetConstant(DWordOffset1, DL, MVT::i8);
      return true;
    }
  } else if (Addr.getOpcode() == ISD::SUB) {
    // sub C, x -> add (sub 0, x), C
    if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Addr.getOperand(0))) {
      unsigned DWordOffset0 = C->getZExtValue() / 4;
      unsigned DWordOffset1 = DWordOffset0 + 1;

      if (isUInt<8>(DWordOffset0)) {
        SDLoc DL(Addr);
        SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);

        // XXX - This is kind of hacky. Create a dummy sub node so we can check
        // the known bits in isDSOffsetLegal. We need to emit the selected node
        // here, so this is thrown away.
        SDValue Sub = CurDAG->getNode(ISD::SUB, DL, MVT::i32,
                                      Zero, Addr.getOperand(1));

        if (isDSOffsetLegal(Sub, DWordOffset1, 8)) {
          unsigned SubOp = Subtarget->hasAddNoCarry() ?
            AMDGPU::V_SUB_U32_e64 : AMDGPU::V_SUB_I32_e32;

          MachineSDNode *MachineSub
            = CurDAG->getMachineNode(SubOp, DL, MVT::i32,
                                     Zero, Addr.getOperand(1));

          Base = SDValue(MachineSub, 0);
          Offset0 = CurDAG->getTargetConstant(DWordOffset0, DL, MVT::i8);
          Offset1 = CurDAG->getTargetConstant(DWordOffset1, DL, MVT::i8);
          return true;
        }
      }
    }
  } else if (const ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    unsigned DWordOffset0 = CAddr->getZExtValue() / 4;
    unsigned DWordOffset1 = DWordOffset0 + 1;
    assert(4 * DWordOffset0 == CAddr->getZExtValue());

    if (isUInt<8>(DWordOffset0) && isUInt<8>(DWordOffset1)) {
      SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
      MachineSDNode *MovZero
        = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                 DL, MVT::i32, Zero);
      Base = SDValue(MovZero, 0);
      Offset0 = CurDAG->getTargetConstant(DWordOffset0, DL, MVT::i8);
      Offset1 = CurDAG->getTargetConstant(DWordOffset1, DL, MVT::i8);
      return true;
    }
  }

  // default case

  Base = Addr;
  Offset0 = CurDAG->getTargetConstant(0, DL, MVT::i8);
  Offset1 = CurDAG->getTargetConstant(1, DL, MVT::i8);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUF(SDValue Addr, SDValue &Ptr,
                                     SDValue &VAddr, SDValue &SOffset,
                                     SDValue &Offset, SDValue &Offen,
                                     SDValue &Idxen, SDValue &Addr64,
                                     SDValue &GLC, SDValue &SLC,
                                     SDValue &TFE) const {
  // Subtarget prefers to use flat instruction
  if (Subtarget->useFlatForGlobal())
    return false;

  SDLoc DL(Addr);

  if (!GLC.getNode())
    GLC = CurDAG->getTargetConstant(0, DL, MVT::i1);
  if (!SLC.getNode())
    SLC = CurDAG->getTargetConstant(0, DL, MVT::i1);
  TFE = CurDAG->getTargetConstant(0, DL, MVT::i1);

  Idxen = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Offen = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Addr64 = CurDAG->getTargetConstant(0, DL, MVT::i1);
  SOffset = CurDAG->getTargetConstant(0, DL, MVT::i32);

  ConstantSDNode *C1 = nullptr;
  SDValue N0 = Addr;
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    C1 = cast<ConstantSDNode>(Addr.getOperand(1));
    if (isUInt<32>(C1->getZExtValue()))
      N0 = Addr.getOperand(0);
    else
      C1 = nullptr;
  }

  if (N0.getOpcode() == ISD::ADD) {
    // (add N2, N3) -> addr64, or
    // (add (add N2, N3), C1) -> addr64
    SDValue N2 = N0.getOperand(0);
    SDValue N3 = N0.getOperand(1);
    Addr64 = CurDAG->getTargetConstant(1, DL, MVT::i1);

    if (N2->isDivergent()) {
      if (N3->isDivergent()) {
        // Both N2 and N3 are divergent. Use N0 (the result of the add) as the
        // addr64, and construct the resource from a 0 address.
        Ptr = SDValue(buildSMovImm64(DL, 0, MVT::v2i32), 0);
        VAddr = N0;
      } else {
        // N2 is divergent, N3 is not.
        Ptr = N3;
        VAddr = N2;
      }
    } else {
      // N2 is not divergent.
      Ptr = N2;
      VAddr = N3;
    }
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i16);
  } else if (N0->isDivergent()) {
    // N0 is divergent. Use it as the addr64, and construct the resource from a
    // 0 address.
    Ptr = SDValue(buildSMovImm64(DL, 0, MVT::v2i32), 0);
    VAddr = N0;
    Addr64 = CurDAG->getTargetConstant(1, DL, MVT::i1);
  } else {
    // N0 -> offset, or
    // (N0 + C1) -> offset
    VAddr = CurDAG->getTargetConstant(0, DL, MVT::i32);
    Ptr = N0;
  }

  if (!C1) {
    // No offset.
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i16);
    return true;
  }

  if (SIInstrInfo::isLegalMUBUFImmOffset(C1->getZExtValue())) {
    // Legal offset for instruction.
    Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i16);
    return true;
  }

  // Illegal offset, store it in soffset.
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i16);
  SOffset =
      SDValue(CurDAG->getMachineNode(
                  AMDGPU::S_MOV_B32, DL, MVT::i32,
                  CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i32)),
              0);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFAddr64(SDValue Addr, SDValue &SRsrc,
                                           SDValue &VAddr, SDValue &SOffset,
                                           SDValue &Offset, SDValue &GLC,
                                           SDValue &SLC, SDValue &TFE) const {
  SDValue Ptr, Offen, Idxen, Addr64;

  // addr64 bit was removed for volcanic islands.
  if (Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
    return false;

  if (!SelectMUBUF(Addr, Ptr, VAddr, SOffset, Offset, Offen, Idxen, Addr64,
              GLC, SLC, TFE))
    return false;

  ConstantSDNode *C = cast<ConstantSDNode>(Addr64);
  if (C->getSExtValue()) {
    SDLoc DL(Addr);

    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());

    SRsrc = SDValue(Lowering.wrapAddr64Rsrc(*CurDAG, DL, Ptr), 0);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFAddr64(SDValue Addr, SDValue &SRsrc,
                                           SDValue &VAddr, SDValue &SOffset,
                                           SDValue &Offset,
                                           SDValue &SLC) const {
  SLC = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i1);
  SDValue GLC, TFE;

  return SelectMUBUFAddr64(Addr, SRsrc, VAddr, SOffset, Offset, GLC, SLC, TFE);
}

static bool isStackPtrRelative(const MachinePointerInfo &PtrInfo) {
  auto PSV = PtrInfo.V.dyn_cast<const PseudoSourceValue *>();
  return PSV && PSV->isStack();
}

std::pair<SDValue, SDValue> AMDGPUDAGToDAGISel::foldFrameIndex(SDValue N) const {
  const MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  if (auto FI = dyn_cast<FrameIndexSDNode>(N)) {
    SDValue TFI = CurDAG->getTargetFrameIndex(FI->getIndex(),
                                              FI->getValueType(0));

    // If we can resolve this to a frame index access, this is relative to the
    // frame pointer SGPR.
    return std::make_pair(TFI, CurDAG->getRegister(Info->getFrameOffsetReg(),
                                                   MVT::i32));
  }

  // If we don't know this private access is a local stack object, it needs to
  // be relative to the entry point's scratch wave offset register.
  return std::make_pair(N, CurDAG->getRegister(Info->getScratchWaveOffsetReg(),
                                               MVT::i32));
}

bool AMDGPUDAGToDAGISel::SelectMUBUFScratchOffen(SDNode *Parent,
                                                 SDValue Addr, SDValue &Rsrc,
                                                 SDValue &VAddr, SDValue &SOffset,
                                                 SDValue &ImmOffset) const {

  SDLoc DL(Addr);
  MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  Rsrc = CurDAG->getRegister(Info->getScratchRSrcReg(), MVT::v4i32);

  if (ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    unsigned Imm = CAddr->getZExtValue();

    SDValue HighBits = CurDAG->getTargetConstant(Imm & ~4095, DL, MVT::i32);
    MachineSDNode *MovHighBits = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                                        DL, MVT::i32, HighBits);
    VAddr = SDValue(MovHighBits, 0);

    // In a call sequence, stores to the argument stack area are relative to the
    // stack pointer.
    const MachinePointerInfo &PtrInfo = cast<MemSDNode>(Parent)->getPointerInfo();
    unsigned SOffsetReg = isStackPtrRelative(PtrInfo) ?
      Info->getStackPtrOffsetReg() : Info->getScratchWaveOffsetReg();

    SOffset = CurDAG->getRegister(SOffsetReg, MVT::i32);
    ImmOffset = CurDAG->getTargetConstant(Imm & 4095, DL, MVT::i16);
    return true;
  }

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    // (add n0, c1)

    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);

    // Offsets in vaddr must be positive if range checking is enabled.
    //
    // The total computation of vaddr + soffset + offset must not overflow.  If
    // vaddr is negative, even if offset is 0 the sgpr offset add will end up
    // overflowing.
    //
    // Prior to gfx9, MUBUF instructions with the vaddr offset enabled would
    // always perform a range check. If a negative vaddr base index was used,
    // this would fail the range check. The overall address computation would
    // compute a valid address, but this doesn't happen due to the range
    // check. For out-of-bounds MUBUF loads, a 0 is returned.
    //
    // Therefore it should be safe to fold any VGPR offset on gfx9 into the
    // MUBUF vaddr, but not on older subtargets which can only do this if the
    // sign bit is known 0.
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);
    if (SIInstrInfo::isLegalMUBUFImmOffset(C1->getZExtValue()) &&
        (!Subtarget->privateMemoryResourceIsRangeChecked() ||
         CurDAG->SignBitIsZero(N0))) {
      std::tie(VAddr, SOffset) = foldFrameIndex(N0);
      ImmOffset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i16);
      return true;
    }
  }

  // (node)
  std::tie(VAddr, SOffset) = foldFrameIndex(Addr);
  ImmOffset = CurDAG->getTargetConstant(0, DL, MVT::i16);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFScratchOffset(SDNode *Parent,
                                                  SDValue Addr,
                                                  SDValue &SRsrc,
                                                  SDValue &SOffset,
                                                  SDValue &Offset) const {
  ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr);
  if (!CAddr || !SIInstrInfo::isLegalMUBUFImmOffset(CAddr->getZExtValue()))
    return false;

  SDLoc DL(Addr);
  MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  SRsrc = CurDAG->getRegister(Info->getScratchRSrcReg(), MVT::v4i32);

  const MachinePointerInfo &PtrInfo = cast<MemSDNode>(Parent)->getPointerInfo();
  unsigned SOffsetReg = isStackPtrRelative(PtrInfo) ?
    Info->getStackPtrOffsetReg() : Info->getScratchWaveOffsetReg();

  // FIXME: Get from MachinePointerInfo? We should only be using the frame
  // offset if we know this is in a call sequence.
  SOffset = CurDAG->getRegister(SOffsetReg, MVT::i32);

  Offset = CurDAG->getTargetConstant(CAddr->getZExtValue(), DL, MVT::i16);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc,
                                           SDValue &SOffset, SDValue &Offset,
                                           SDValue &GLC, SDValue &SLC,
                                           SDValue &TFE) const {
  SDValue Ptr, VAddr, Offen, Idxen, Addr64;
  const SIInstrInfo *TII =
    static_cast<const SIInstrInfo *>(Subtarget->getInstrInfo());

  if (!SelectMUBUF(Addr, Ptr, VAddr, SOffset, Offset, Offen, Idxen, Addr64,
              GLC, SLC, TFE))
    return false;

  if (!cast<ConstantSDNode>(Offen)->getSExtValue() &&
      !cast<ConstantSDNode>(Idxen)->getSExtValue() &&
      !cast<ConstantSDNode>(Addr64)->getSExtValue()) {
    uint64_t Rsrc = TII->getDefaultRsrcDataFormat() |
                    APInt::getAllOnesValue(32).getZExtValue(); // Size
    SDLoc DL(Addr);

    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());

    SRsrc = SDValue(Lowering.buildRSRC(*CurDAG, DL, Ptr, 0, Rsrc), 0);
    return true;
  }
  return false;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc,
                                           SDValue &Soffset, SDValue &Offset
                                           ) const {
  SDValue GLC, SLC, TFE;

  return SelectMUBUFOffset(Addr, SRsrc, Soffset, Offset, GLC, SLC, TFE);
}
bool AMDGPUDAGToDAGISel::SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc,
                                           SDValue &Soffset, SDValue &Offset,
                                           SDValue &SLC) const {
  SDValue GLC, TFE;

  return SelectMUBUFOffset(Addr, SRsrc, Soffset, Offset, GLC, SLC, TFE);
}

template <bool IsSigned>
bool AMDGPUDAGToDAGISel::SelectFlatOffset(SDValue Addr,
                                          SDValue &VAddr,
                                          SDValue &Offset,
                                          SDValue &SLC) const {
  int64_t OffsetVal = 0;

  if (Subtarget->hasFlatInstOffsets() &&
      CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);
    int64_t COffsetVal = cast<ConstantSDNode>(N1)->getSExtValue();

    if ((IsSigned && isInt<13>(COffsetVal)) ||
        (!IsSigned && isUInt<12>(COffsetVal))) {
      Addr = N0;
      OffsetVal = COffsetVal;
    }
  }

  VAddr = Addr;
  Offset = CurDAG->getTargetConstant(OffsetVal, SDLoc(), MVT::i16);
  SLC = CurDAG->getTargetConstant(0, SDLoc(), MVT::i1);

  return true;
}

bool AMDGPUDAGToDAGISel::SelectFlatAtomic(SDValue Addr,
                                          SDValue &VAddr,
                                          SDValue &Offset,
                                          SDValue &SLC) const {
  return SelectFlatOffset<false>(Addr, VAddr, Offset, SLC);
}

bool AMDGPUDAGToDAGISel::SelectFlatAtomicSigned(SDValue Addr,
                                          SDValue &VAddr,
                                          SDValue &Offset,
                                          SDValue &SLC) const {
  return SelectFlatOffset<true>(Addr, VAddr, Offset, SLC);
}

bool AMDGPUDAGToDAGISel::SelectSMRDOffset(SDValue ByteOffsetNode,
                                          SDValue &Offset, bool &Imm) const {

  // FIXME: Handle non-constant offsets.
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(ByteOffsetNode);
  if (!C)
    return false;

  SDLoc SL(ByteOffsetNode);
  GCNSubtarget::Generation Gen = Subtarget->getGeneration();
  int64_t ByteOffset = C->getSExtValue();
  int64_t EncodedOffset = AMDGPU::getSMRDEncodedOffset(*Subtarget, ByteOffset);

  if (AMDGPU::isLegalSMRDImmOffset(*Subtarget, ByteOffset)) {
    Offset = CurDAG->getTargetConstant(EncodedOffset, SL, MVT::i32);
    Imm = true;
    return true;
  }

  if (!isUInt<32>(EncodedOffset) || !isUInt<32>(ByteOffset))
    return false;

  if (Gen == AMDGPUSubtarget::SEA_ISLANDS && isUInt<32>(EncodedOffset)) {
    // 32-bit Immediates are supported on Sea Islands.
    Offset = CurDAG->getTargetConstant(EncodedOffset, SL, MVT::i32);
  } else {
    SDValue C32Bit = CurDAG->getTargetConstant(ByteOffset, SL, MVT::i32);
    Offset = SDValue(CurDAG->getMachineNode(AMDGPU::S_MOV_B32, SL, MVT::i32,
                                            C32Bit), 0);
  }
  Imm = false;
  return true;
}

SDValue AMDGPUDAGToDAGISel::Expand32BitAddress(SDValue Addr) const {
  if (Addr.getValueType() != MVT::i32)
    return Addr;

  // Zero-extend a 32-bit address.
  SDLoc SL(Addr);

  const MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  unsigned AddrHiVal = Info->get32BitAddressHighBits();
  SDValue AddrHi = CurDAG->getTargetConstant(AddrHiVal, SL, MVT::i32);

  const SDValue Ops[] = {
    CurDAG->getTargetConstant(AMDGPU::SReg_64_XEXECRegClassID, SL, MVT::i32),
    Addr,
    CurDAG->getTargetConstant(AMDGPU::sub0, SL, MVT::i32),
    SDValue(CurDAG->getMachineNode(AMDGPU::S_MOV_B32, SL, MVT::i32, AddrHi),
            0),
    CurDAG->getTargetConstant(AMDGPU::sub1, SL, MVT::i32),
  };

  return SDValue(CurDAG->getMachineNode(AMDGPU::REG_SEQUENCE, SL, MVT::i64,
                                        Ops), 0);
}

bool AMDGPUDAGToDAGISel::SelectSMRD(SDValue Addr, SDValue &SBase,
                                     SDValue &Offset, bool &Imm) const {
  SDLoc SL(Addr);

  // A 32-bit (address + offset) should not cause unsigned 32-bit integer
  // wraparound, because s_load instructions perform the addition in 64 bits.
  if ((Addr.getValueType() != MVT::i32 ||
       Addr->getFlags().hasNoUnsignedWrap()) &&
      CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);

    if (SelectSMRDOffset(N1, Offset, Imm)) {
      SBase = Expand32BitAddress(N0);
      return true;
    }
  }
  SBase = Expand32BitAddress(Addr);
  Offset = CurDAG->getTargetConstant(0, SL, MVT::i32);
  Imm = true;
  return true;
}

bool AMDGPUDAGToDAGISel::SelectSMRDImm(SDValue Addr, SDValue &SBase,
                                       SDValue &Offset) const {
  bool Imm;
  return SelectSMRD(Addr, SBase, Offset, Imm) && Imm;
}

bool AMDGPUDAGToDAGISel::SelectSMRDImm32(SDValue Addr, SDValue &SBase,
                                         SDValue &Offset) const {

  if (Subtarget->getGeneration() != AMDGPUSubtarget::SEA_ISLANDS)
    return false;

  bool Imm;
  if (!SelectSMRD(Addr, SBase, Offset, Imm))
    return false;

  return !Imm && isa<ConstantSDNode>(Offset);
}

bool AMDGPUDAGToDAGISel::SelectSMRDSgpr(SDValue Addr, SDValue &SBase,
                                        SDValue &Offset) const {
  bool Imm;
  return SelectSMRD(Addr, SBase, Offset, Imm) && !Imm &&
         !isa<ConstantSDNode>(Offset);
}

bool AMDGPUDAGToDAGISel::SelectSMRDBufferImm(SDValue Addr,
                                             SDValue &Offset) const {
  bool Imm;
  return SelectSMRDOffset(Addr, Offset, Imm) && Imm;
}

bool AMDGPUDAGToDAGISel::SelectSMRDBufferImm32(SDValue Addr,
                                               SDValue &Offset) const {
  if (Subtarget->getGeneration() != AMDGPUSubtarget::SEA_ISLANDS)
    return false;

  bool Imm;
  if (!SelectSMRDOffset(Addr, Offset, Imm))
    return false;

  return !Imm && isa<ConstantSDNode>(Offset);
}

bool AMDGPUDAGToDAGISel::SelectMOVRELOffset(SDValue Index,
                                            SDValue &Base,
                                            SDValue &Offset) const {
  SDLoc DL(Index);

  if (CurDAG->isBaseWithConstantOffset(Index)) {
    SDValue N0 = Index.getOperand(0);
    SDValue N1 = Index.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);

    // (add n0, c0)
    // Don't peel off the offset (c0) if doing so could possibly lead
    // the base (n0) to be negative.
    if (C1->getSExtValue() <= 0 || CurDAG->SignBitIsZero(N0)) {
      Base = N0;
      Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i32);
      return true;
    }
  }

  if (isa<ConstantSDNode>(Index))
    return false;

  Base = Index;
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

SDNode *AMDGPUDAGToDAGISel::getS_BFE(unsigned Opcode, const SDLoc &DL,
                                     SDValue Val, uint32_t Offset,
                                     uint32_t Width) {
  // Transformation function, pack the offset and width of a BFE into
  // the format expected by the S_BFE_I32 / S_BFE_U32. In the second
  // source, bits [5:0] contain the offset and bits [22:16] the width.
  uint32_t PackedVal = Offset | (Width << 16);
  SDValue PackedConst = CurDAG->getTargetConstant(PackedVal, DL, MVT::i32);

  return CurDAG->getMachineNode(Opcode, DL, MVT::i32, Val, PackedConst);
}

void AMDGPUDAGToDAGISel::SelectS_BFEFromShifts(SDNode *N) {
  // "(a << b) srl c)" ---> "BFE_U32 a, (c-b), (32-c)
  // "(a << b) sra c)" ---> "BFE_I32 a, (c-b), (32-c)
  // Predicate: 0 < b <= c < 32

  const SDValue &Shl = N->getOperand(0);
  ConstantSDNode *B = dyn_cast<ConstantSDNode>(Shl->getOperand(1));
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));

  if (B && C) {
    uint32_t BVal = B->getZExtValue();
    uint32_t CVal = C->getZExtValue();

    if (0 < BVal && BVal <= CVal && CVal < 32) {
      bool Signed = N->getOpcode() == ISD::SRA;
      unsigned Opcode = Signed ? AMDGPU::S_BFE_I32 : AMDGPU::S_BFE_U32;

      ReplaceNode(N, getS_BFE(Opcode, SDLoc(N), Shl.getOperand(0), CVal - BVal,
                              32 - CVal));
      return;
    }
  }
  SelectCode(N);
}

void AMDGPUDAGToDAGISel::SelectS_BFE(SDNode *N) {
  switch (N->getOpcode()) {
  case ISD::AND:
    if (N->getOperand(0).getOpcode() == ISD::SRL) {
      // "(a srl b) & mask" ---> "BFE_U32 a, b, popcount(mask)"
      // Predicate: isMask(mask)
      const SDValue &Srl = N->getOperand(0);
      ConstantSDNode *Shift = dyn_cast<ConstantSDNode>(Srl.getOperand(1));
      ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(N->getOperand(1));

      if (Shift && Mask) {
        uint32_t ShiftVal = Shift->getZExtValue();
        uint32_t MaskVal = Mask->getZExtValue();

        if (isMask_32(MaskVal)) {
          uint32_t WidthVal = countPopulation(MaskVal);

          ReplaceNode(N, getS_BFE(AMDGPU::S_BFE_U32, SDLoc(N),
                                  Srl.getOperand(0), ShiftVal, WidthVal));
          return;
        }
      }
    }
    break;
  case ISD::SRL:
    if (N->getOperand(0).getOpcode() == ISD::AND) {
      // "(a & mask) srl b)" ---> "BFE_U32 a, b, popcount(mask >> b)"
      // Predicate: isMask(mask >> b)
      const SDValue &And = N->getOperand(0);
      ConstantSDNode *Shift = dyn_cast<ConstantSDNode>(N->getOperand(1));
      ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(And->getOperand(1));

      if (Shift && Mask) {
        uint32_t ShiftVal = Shift->getZExtValue();
        uint32_t MaskVal = Mask->getZExtValue() >> ShiftVal;

        if (isMask_32(MaskVal)) {
          uint32_t WidthVal = countPopulation(MaskVal);

          ReplaceNode(N, getS_BFE(AMDGPU::S_BFE_U32, SDLoc(N),
                                  And.getOperand(0), ShiftVal, WidthVal));
          return;
        }
      }
    } else if (N->getOperand(0).getOpcode() == ISD::SHL) {
      SelectS_BFEFromShifts(N);
      return;
    }
    break;
  case ISD::SRA:
    if (N->getOperand(0).getOpcode() == ISD::SHL) {
      SelectS_BFEFromShifts(N);
      return;
    }
    break;

  case ISD::SIGN_EXTEND_INREG: {
    // sext_inreg (srl x, 16), i8 -> bfe_i32 x, 16, 8
    SDValue Src = N->getOperand(0);
    if (Src.getOpcode() != ISD::SRL)
      break;

    const ConstantSDNode *Amt = dyn_cast<ConstantSDNode>(Src.getOperand(1));
    if (!Amt)
      break;

    unsigned Width = cast<VTSDNode>(N->getOperand(1))->getVT().getSizeInBits();
    ReplaceNode(N, getS_BFE(AMDGPU::S_BFE_I32, SDLoc(N), Src.getOperand(0),
                            Amt->getZExtValue(), Width));
    return;
  }
  }

  SelectCode(N);
}

bool AMDGPUDAGToDAGISel::isCBranchSCC(const SDNode *N) const {
  assert(N->getOpcode() == ISD::BRCOND);
  if (!N->hasOneUse())
    return false;

  SDValue Cond = N->getOperand(1);
  if (Cond.getOpcode() == ISD::CopyToReg)
    Cond = Cond.getOperand(2);

  if (Cond.getOpcode() != ISD::SETCC || !Cond.hasOneUse())
    return false;

  MVT VT = Cond.getOperand(0).getSimpleValueType();
  if (VT == MVT::i32)
    return true;

  if (VT == MVT::i64) {
    auto ST = static_cast<const GCNSubtarget *>(Subtarget);

    ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
    return (CC == ISD::SETEQ || CC == ISD::SETNE) && ST->hasScalarCompareEq64();
  }

  return false;
}

void AMDGPUDAGToDAGISel::SelectBRCOND(SDNode *N) {
  SDValue Cond = N->getOperand(1);

  if (Cond.isUndef()) {
    CurDAG->SelectNodeTo(N, AMDGPU::SI_BR_UNDEF, MVT::Other,
                         N->getOperand(2), N->getOperand(0));
    return;
  }

  bool UseSCCBr = isCBranchSCC(N) && isUniformBr(N);
  unsigned BrOp = UseSCCBr ? AMDGPU::S_CBRANCH_SCC1 : AMDGPU::S_CBRANCH_VCCNZ;
  unsigned CondReg = UseSCCBr ? AMDGPU::SCC : AMDGPU::VCC;
  SDLoc SL(N);

  if (!UseSCCBr) {
    // This is the case that we are selecting to S_CBRANCH_VCCNZ.  We have not
    // analyzed what generates the vcc value, so we do not know whether vcc
    // bits for disabled lanes are 0.  Thus we need to mask out bits for
    // disabled lanes.
    //
    // For the case that we select S_CBRANCH_SCC1 and it gets
    // changed to S_CBRANCH_VCCNZ in SIFixSGPRCopies, SIFixSGPRCopies calls
    // SIInstrInfo::moveToVALU which inserts the S_AND).
    //
    // We could add an analysis of what generates the vcc value here and omit
    // the S_AND when is unnecessary. But it would be better to add a separate
    // pass after SIFixSGPRCopies to do the unnecessary S_AND removal, so it
    // catches both cases.
    Cond = SDValue(CurDAG->getMachineNode(AMDGPU::S_AND_B64, SL, MVT::i1,
                               CurDAG->getRegister(AMDGPU::EXEC, MVT::i1),
                               Cond),
                   0);
  }

  SDValue VCC = CurDAG->getCopyToReg(N->getOperand(0), SL, CondReg, Cond);
  CurDAG->SelectNodeTo(N, BrOp, MVT::Other,
                       N->getOperand(2), // Basic Block
                       VCC.getValue(0));
}

void AMDGPUDAGToDAGISel::SelectFMAD_FMA(SDNode *N) {
  MVT VT = N->getSimpleValueType(0);
  bool IsFMA = N->getOpcode() == ISD::FMA;
  if (VT != MVT::f32 || (!Subtarget->hasMadMixInsts() &&
                         !Subtarget->hasFmaMixInsts()) ||
      ((IsFMA && Subtarget->hasMadMixInsts()) ||
       (!IsFMA && Subtarget->hasFmaMixInsts()))) {
    SelectCode(N);
    return;
  }

  SDValue Src0 = N->getOperand(0);
  SDValue Src1 = N->getOperand(1);
  SDValue Src2 = N->getOperand(2);
  unsigned Src0Mods, Src1Mods, Src2Mods;

  // Avoid using v_mad_mix_f32/v_fma_mix_f32 unless there is actually an operand
  // using the conversion from f16.
  bool Sel0 = SelectVOP3PMadMixModsImpl(Src0, Src0, Src0Mods);
  bool Sel1 = SelectVOP3PMadMixModsImpl(Src1, Src1, Src1Mods);
  bool Sel2 = SelectVOP3PMadMixModsImpl(Src2, Src2, Src2Mods);

  assert((IsFMA || !Subtarget->hasFP32Denormals()) &&
         "fmad selected with denormals enabled");
  // TODO: We can select this with f32 denormals enabled if all the sources are
  // converted from f16 (in which case fmad isn't legal).

  if (Sel0 || Sel1 || Sel2) {
    // For dummy operands.
    SDValue Zero = CurDAG->getTargetConstant(0, SDLoc(), MVT::i32);
    SDValue Ops[] = {
      CurDAG->getTargetConstant(Src0Mods, SDLoc(), MVT::i32), Src0,
      CurDAG->getTargetConstant(Src1Mods, SDLoc(), MVT::i32), Src1,
      CurDAG->getTargetConstant(Src2Mods, SDLoc(), MVT::i32), Src2,
      CurDAG->getTargetConstant(0, SDLoc(), MVT::i1),
      Zero, Zero
    };

    CurDAG->SelectNodeTo(N,
                         IsFMA ? AMDGPU::V_FMA_MIX_F32 : AMDGPU::V_MAD_MIX_F32,
                         MVT::f32, Ops);
  } else {
    SelectCode(N);
  }
}

// This is here because there isn't a way to use the generated sub0_sub1 as the
// subreg index to EXTRACT_SUBREG in tablegen.
void AMDGPUDAGToDAGISel::SelectATOMIC_CMP_SWAP(SDNode *N) {
  MemSDNode *Mem = cast<MemSDNode>(N);
  unsigned AS = Mem->getAddressSpace();
  if (AS == AMDGPUAS::FLAT_ADDRESS) {
    SelectCode(N);
    return;
  }

  MVT VT = N->getSimpleValueType(0);
  bool Is32 = (VT == MVT::i32);
  SDLoc SL(N);

  MachineSDNode *CmpSwap = nullptr;
  if (Subtarget->hasAddr64()) {
    SDValue SRsrc, VAddr, SOffset, Offset, SLC;

    if (SelectMUBUFAddr64(Mem->getBasePtr(), SRsrc, VAddr, SOffset, Offset, SLC)) {
      unsigned Opcode = Is32 ? AMDGPU::BUFFER_ATOMIC_CMPSWAP_ADDR64_RTN :
        AMDGPU::BUFFER_ATOMIC_CMPSWAP_X2_ADDR64_RTN;
      SDValue CmpVal = Mem->getOperand(2);

      // XXX - Do we care about glue operands?

      SDValue Ops[] = {
        CmpVal, VAddr, SRsrc, SOffset, Offset, SLC, Mem->getChain()
      };

      CmpSwap = CurDAG->getMachineNode(Opcode, SL, Mem->getVTList(), Ops);
    }
  }

  if (!CmpSwap) {
    SDValue SRsrc, SOffset, Offset, SLC;
    if (SelectMUBUFOffset(Mem->getBasePtr(), SRsrc, SOffset, Offset, SLC)) {
      unsigned Opcode = Is32 ? AMDGPU::BUFFER_ATOMIC_CMPSWAP_OFFSET_RTN :
        AMDGPU::BUFFER_ATOMIC_CMPSWAP_X2_OFFSET_RTN;

      SDValue CmpVal = Mem->getOperand(2);
      SDValue Ops[] = {
        CmpVal, SRsrc, SOffset, Offset, SLC, Mem->getChain()
      };

      CmpSwap = CurDAG->getMachineNode(Opcode, SL, Mem->getVTList(), Ops);
    }
  }

  if (!CmpSwap) {
    SelectCode(N);
    return;
  }

  MachineMemOperand *MMO = Mem->getMemOperand();
  CurDAG->setNodeMemRefs(CmpSwap, {MMO});

  unsigned SubReg = Is32 ? AMDGPU::sub0 : AMDGPU::sub0_sub1;
  SDValue Extract
    = CurDAG->getTargetExtractSubreg(SubReg, SL, VT, SDValue(CmpSwap, 0));

  ReplaceUses(SDValue(N, 0), Extract);
  ReplaceUses(SDValue(N, 1), SDValue(CmpSwap, 1));
  CurDAG->RemoveDeadNode(N);
}

bool AMDGPUDAGToDAGISel::SelectVOP3ModsImpl(SDValue In, SDValue &Src,
                                            unsigned &Mods) const {
  Mods = 0;
  Src = In;

  if (Src.getOpcode() == ISD::FNEG) {
    Mods |= SISrcMods::NEG;
    Src = Src.getOperand(0);
  }

  if (Src.getOpcode() == ISD::FABS) {
    Mods |= SISrcMods::ABS;
    Src = Src.getOperand(0);
  }

  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods(SDValue In, SDValue &Src,
                                        SDValue &SrcMods) const {
  unsigned Mods;
  if (SelectVOP3ModsImpl(In, Src, Mods)) {
    SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods_NNaN(SDValue In, SDValue &Src,
                                             SDValue &SrcMods) const {
  SelectVOP3Mods(In, Src, SrcMods);
  return isNoNanSrc(Src);
}

bool AMDGPUDAGToDAGISel::SelectVOP3NoMods(SDValue In, SDValue &Src) const {
  if (In.getOpcode() == ISD::FABS || In.getOpcode() == ISD::FNEG)
    return false;

  Src = In;
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods0(SDValue In, SDValue &Src,
                                         SDValue &SrcMods, SDValue &Clamp,
                                         SDValue &Omod) const {
  SDLoc DL(In);
  Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Omod = CurDAG->getTargetConstant(0, DL, MVT::i1);

  return SelectVOP3Mods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods0Clamp0OMod(SDValue In, SDValue &Src,
                                                   SDValue &SrcMods,
                                                   SDValue &Clamp,
                                                   SDValue &Omod) const {
  Clamp = Omod = CurDAG->getTargetConstant(0, SDLoc(In), MVT::i32);
  return SelectVOP3Mods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3OMods(SDValue In, SDValue &Src,
                                         SDValue &Clamp, SDValue &Omod) const {
  Src = In;

  SDLoc DL(In);
  Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Omod = CurDAG->getTargetConstant(0, DL, MVT::i1);

  return true;
}

static SDValue stripBitcast(SDValue Val) {
  return Val.getOpcode() == ISD::BITCAST ? Val.getOperand(0) : Val;
}

// Figure out if this is really an extract of the high 16-bits of a dword.
static bool isExtractHiElt(SDValue In, SDValue &Out) {
  In = stripBitcast(In);
  if (In.getOpcode() != ISD::TRUNCATE)
    return false;

  SDValue Srl = In.getOperand(0);
  if (Srl.getOpcode() == ISD::SRL) {
    if (ConstantSDNode *ShiftAmt = dyn_cast<ConstantSDNode>(Srl.getOperand(1))) {
      if (ShiftAmt->getZExtValue() == 16) {
        Out = stripBitcast(Srl.getOperand(0));
        return true;
      }
    }
  }

  return false;
}

// Look through operations that obscure just looking at the low 16-bits of the
// same register.
static SDValue stripExtractLoElt(SDValue In) {
  if (In.getOpcode() == ISD::TRUNCATE) {
    SDValue Src = In.getOperand(0);
    if (Src.getValueType().getSizeInBits() == 32)
      return stripBitcast(Src);
  }

  return In;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMods(SDValue In, SDValue &Src,
                                         SDValue &SrcMods) const {
  unsigned Mods = 0;
  Src = In;

  if (Src.getOpcode() == ISD::FNEG) {
    Mods ^= (SISrcMods::NEG | SISrcMods::NEG_HI);
    Src = Src.getOperand(0);
  }

  if (Src.getOpcode() == ISD::BUILD_VECTOR) {
    unsigned VecMods = Mods;

    SDValue Lo = stripBitcast(Src.getOperand(0));
    SDValue Hi = stripBitcast(Src.getOperand(1));

    if (Lo.getOpcode() == ISD::FNEG) {
      Lo = stripBitcast(Lo.getOperand(0));
      Mods ^= SISrcMods::NEG;
    }

    if (Hi.getOpcode() == ISD::FNEG) {
      Hi = stripBitcast(Hi.getOperand(0));
      Mods ^= SISrcMods::NEG_HI;
    }

    if (isExtractHiElt(Lo, Lo))
      Mods |= SISrcMods::OP_SEL_0;

    if (isExtractHiElt(Hi, Hi))
      Mods |= SISrcMods::OP_SEL_1;

    Lo = stripExtractLoElt(Lo);
    Hi = stripExtractLoElt(Hi);

    if (Lo == Hi && !isInlineImmediate(Lo.getNode())) {
      // Really a scalar input. Just select from the low half of the register to
      // avoid packing.

      Src = Lo;
      SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
      return true;
    }

    Mods = VecMods;
  }

  // Packed instructions do not have abs modifiers.
  Mods |= SISrcMods::OP_SEL_1;

  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMods0(SDValue In, SDValue &Src,
                                          SDValue &SrcMods,
                                          SDValue &Clamp) const {
  SDLoc SL(In);

  // FIXME: Handle clamp and op_sel
  Clamp = CurDAG->getTargetConstant(0, SL, MVT::i32);

  return SelectVOP3PMods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSel(SDValue In, SDValue &Src,
                                         SDValue &SrcMods) const {
  Src = In;
  // FIXME: Handle op_sel
  SrcMods = CurDAG->getTargetConstant(0, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSel0(SDValue In, SDValue &Src,
                                          SDValue &SrcMods,
                                          SDValue &Clamp) const {
  SDLoc SL(In);

  // FIXME: Handle clamp
  Clamp = CurDAG->getTargetConstant(0, SL, MVT::i32);

  return SelectVOP3OpSel(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSelMods(SDValue In, SDValue &Src,
                                             SDValue &SrcMods) const {
  // FIXME: Handle op_sel
  return SelectVOP3Mods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSelMods0(SDValue In, SDValue &Src,
                                              SDValue &SrcMods,
                                              SDValue &Clamp) const {
  SDLoc SL(In);

  // FIXME: Handle clamp
  Clamp = CurDAG->getTargetConstant(0, SL, MVT::i32);

  return SelectVOP3OpSelMods(In, Src, SrcMods);
}

// The return value is not whether the match is possible (which it always is),
// but whether or not it a conversion is really used.
bool AMDGPUDAGToDAGISel::SelectVOP3PMadMixModsImpl(SDValue In, SDValue &Src,
                                                   unsigned &Mods) const {
  Mods = 0;
  SelectVOP3ModsImpl(In, Src, Mods);

  if (Src.getOpcode() == ISD::FP_EXTEND) {
    Src = Src.getOperand(0);
    assert(Src.getValueType() == MVT::f16);
    Src = stripBitcast(Src);

    // Be careful about folding modifiers if we already have an abs. fneg is
    // applied last, so we don't want to apply an earlier fneg.
    if ((Mods & SISrcMods::ABS) == 0) {
      unsigned ModsTmp;
      SelectVOP3ModsImpl(Src, Src, ModsTmp);

      if ((ModsTmp & SISrcMods::NEG) != 0)
        Mods ^= SISrcMods::NEG;

      if ((ModsTmp & SISrcMods::ABS) != 0)
        Mods |= SISrcMods::ABS;
    }

    // op_sel/op_sel_hi decide the source type and source.
    // If the source's op_sel_hi is set, it indicates to do a conversion from fp16.
    // If the sources's op_sel is set, it picks the high half of the source
    // register.

    Mods |= SISrcMods::OP_SEL_1;
    if (isExtractHiElt(Src, Src)) {
      Mods |= SISrcMods::OP_SEL_0;

      // TODO: Should we try to look for neg/abs here?
    }

    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMadMixMods(SDValue In, SDValue &Src,
                                               SDValue &SrcMods) const {
  unsigned Mods = 0;
  SelectVOP3PMadMixModsImpl(In, Src, Mods);
  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

// TODO: Can we identify things like v_mad_mixhi_f16?
bool AMDGPUDAGToDAGISel::SelectHi16Elt(SDValue In, SDValue &Src) const {
  if (In.isUndef()) {
    Src = In;
    return true;
  }

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(In)) {
    SDLoc SL(In);
    SDValue K = CurDAG->getTargetConstant(C->getZExtValue() << 16, SL, MVT::i32);
    MachineSDNode *MovK = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                                 SL, MVT::i32, K);
    Src = SDValue(MovK, 0);
    return true;
  }

  if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(In)) {
    SDLoc SL(In);
    SDValue K = CurDAG->getTargetConstant(
      C->getValueAPF().bitcastToAPInt().getZExtValue() << 16, SL, MVT::i32);
    MachineSDNode *MovK = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                                 SL, MVT::i32, K);
    Src = SDValue(MovK, 0);
    return true;
  }

  return isExtractHiElt(In, Src);
}

bool AMDGPUDAGToDAGISel::isVGPRImm(const SDNode * N) const {
  if (Subtarget->getGeneration() < AMDGPUSubtarget::SOUTHERN_ISLANDS) {
    return false;
  }
  const SIRegisterInfo *SIRI =
    static_cast<const SIRegisterInfo *>(Subtarget->getRegisterInfo());
  const SIInstrInfo * SII =
    static_cast<const SIInstrInfo *>(Subtarget->getInstrInfo());

  unsigned Limit = 0;
  bool AllUsesAcceptSReg = true;
  for (SDNode::use_iterator U = N->use_begin(), E = SDNode::use_end();
    Limit < 10 && U != E; ++U, ++Limit) {
    const TargetRegisterClass *RC = getOperandRegClass(*U, U.getOperandNo());

    // If the register class is unknown, it could be an unknown
    // register class that needs to be an SGPR, e.g. an inline asm
    // constraint
    if (!RC || SIRI->isSGPRClass(RC))
      return false;

    if (RC != &AMDGPU::VS_32RegClass) {
      AllUsesAcceptSReg = false;
      SDNode * User = *U;
      if (User->isMachineOpcode()) {
        unsigned Opc = User->getMachineOpcode();
        MCInstrDesc Desc = SII->get(Opc);
        if (Desc.isCommutable()) {
          unsigned OpIdx = Desc.getNumDefs() + U.getOperandNo();
          unsigned CommuteIdx1 = TargetInstrInfo::CommuteAnyOperandIndex;
          if (SII->findCommutedOpIndices(Desc, OpIdx, CommuteIdx1)) {
            unsigned CommutedOpNo = CommuteIdx1 - Desc.getNumDefs();
            const TargetRegisterClass *CommutedRC = getOperandRegClass(*U, CommutedOpNo);
            if (CommutedRC == &AMDGPU::VS_32RegClass)
              AllUsesAcceptSReg = true;
          }
        }
      }
      // If "AllUsesAcceptSReg == false" so far we haven't suceeded
      // commuting current user. This means have at least one use
      // that strictly require VGPR. Thus, we will not attempt to commute
      // other user instructions.
      if (!AllUsesAcceptSReg)
        break;
    }
  }
  return !AllUsesAcceptSReg && (Limit < 10);
}

bool AMDGPUDAGToDAGISel::isUniformLoad(const SDNode * N) const {
  auto Ld = cast<LoadSDNode>(N);

  return Ld->getAlignment() >= 4 &&
        (
          (
            (
              Ld->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS       ||
              Ld->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT
            )
            &&
            !N->isDivergent()
          )
          ||
          (
            Subtarget->getScalarizeGlobalBehavior() &&
            Ld->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS &&
            !Ld->isVolatile() &&
            !N->isDivergent() &&
            static_cast<const SITargetLowering *>(
              getTargetLowering())->isMemOpHasNoClobberedMemOperand(N)
          )
        );
}

void AMDGPUDAGToDAGISel::PostprocessISelDAG() {
  const AMDGPUTargetLowering& Lowering =
    *static_cast<const AMDGPUTargetLowering*>(getTargetLowering());
  bool IsModified = false;
  do {
    IsModified = false;

    // Go over all selected nodes and try to fold them a bit more
    SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_begin();
    while (Position != CurDAG->allnodes_end()) {
      SDNode *Node = &*Position++;
      MachineSDNode *MachineNode = dyn_cast<MachineSDNode>(Node);
      if (!MachineNode)
        continue;

      SDNode *ResNode = Lowering.PostISelFolding(MachineNode, *CurDAG);
      if (ResNode != Node) {
        if (ResNode)
          ReplaceUses(Node, ResNode);
        IsModified = true;
      }
    }
    CurDAG->RemoveDeadNodes();
  } while (IsModified);
}

bool R600DAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<R600Subtarget>();
  return SelectionDAGISel::runOnMachineFunction(MF);
}

bool R600DAGToDAGISel::isConstantLoad(const MemSDNode *N, int CbId) const {
  if (!N->readMem())
    return false;
  if (CbId == -1)
    return N->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS ||
           N->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT;

  return N->getAddressSpace() == AMDGPUAS::CONSTANT_BUFFER_0 + CbId;
}

bool R600DAGToDAGISel::SelectGlobalValueConstantOffset(SDValue Addr,
                                                         SDValue& IntPtr) {
  if (ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(Addr)) {
    IntPtr = CurDAG->getIntPtrConstant(Cst->getZExtValue() / 4, SDLoc(Addr),
                                       true);
    return true;
  }
  return false;
}

bool R600DAGToDAGISel::SelectGlobalValueVariableOffset(SDValue Addr,
    SDValue& BaseReg, SDValue &Offset) {
  if (!isa<ConstantSDNode>(Addr)) {
    BaseReg = Addr;
    Offset = CurDAG->getIntPtrConstant(0, SDLoc(Addr), true);
    return true;
  }
  return false;
}

void R600DAGToDAGISel::Select(SDNode *N) {
  unsigned int Opc = N->getOpcode();
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;   // Already selected.
  }

  switch (Opc) {
  default: break;
  case AMDGPUISD::BUILD_VERTICAL_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
  case ISD::BUILD_VECTOR: {
    EVT VT = N->getValueType(0);
    unsigned NumVectorElts = VT.getVectorNumElements();
    unsigned RegClassID;
    // BUILD_VECTOR was lowered into an IMPLICIT_DEF + 4 INSERT_SUBREG
    // that adds a 128 bits reg copy when going through TwoAddressInstructions
    // pass. We want to avoid 128 bits copies as much as possible because they
    // can't be bundled by our scheduler.
    switch(NumVectorElts) {
    case 2: RegClassID = R600::R600_Reg64RegClassID; break;
    case 4:
      if (Opc == AMDGPUISD::BUILD_VERTICAL_VECTOR)
        RegClassID = R600::R600_Reg128VerticalRegClassID;
      else
        RegClassID = R600::R600_Reg128RegClassID;
      break;
    default: llvm_unreachable("Do not know how to lower this BUILD_VECTOR");
    }
    SelectBuildVector(N, RegClassID);
    return;
  }
  }

  SelectCode(N);
}

bool R600DAGToDAGISel::SelectADDRIndirect(SDValue Addr, SDValue &Base,
                                          SDValue &Offset) {
  ConstantSDNode *C;
  SDLoc DL(Addr);

  if ((C = dyn_cast<ConstantSDNode>(Addr))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == AMDGPUISD::DWORDADDR) &&
             (C = dyn_cast<ConstantSDNode>(Addr.getOperand(0)))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == ISD::ADD || Addr.getOpcode() == ISD::OR) &&
            (C = dyn_cast<ConstantSDNode>(Addr.getOperand(1)))) {
    Base = Addr.getOperand(0);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else {
    Base = Addr;
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  }

  return true;
}

bool R600DAGToDAGISel::SelectADDRVTX_READ(SDValue Addr, SDValue &Base,
                                          SDValue &Offset) {
  ConstantSDNode *IMMOffset;

  if (Addr.getOpcode() == ISD::ADD
      && (IMMOffset = dyn_cast<ConstantSDNode>(Addr.getOperand(1)))
      && isInt<16>(IMMOffset->getZExtValue())) {

      Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(IMMOffset->getZExtValue(), SDLoc(Addr),
                                         MVT::i32);
      return true;
  // If the pointer address is constant, we can move it to the offset field.
  } else if ((IMMOffset = dyn_cast<ConstantSDNode>(Addr))
             && isInt<16>(IMMOffset->getZExtValue())) {
    Base = CurDAG->getCopyFromReg(CurDAG->getEntryNode(),
                                  SDLoc(CurDAG->getEntryNode()),
                                  R600::ZERO, MVT::i32);
    Offset = CurDAG->getTargetConstant(IMMOffset->getZExtValue(), SDLoc(Addr),
                                       MVT::i32);
    return true;
  }

  // Default case, no offset
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i32);
  return true;
}
