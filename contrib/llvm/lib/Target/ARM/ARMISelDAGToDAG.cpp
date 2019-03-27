//===-- ARMISelDAGToDAG.cpp - A dag to dag inst selector for ARM ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the ARM target.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMTargetMachine.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "arm-isel"

static cl::opt<bool>
DisableShifterOp("disable-shifter-op", cl::Hidden,
  cl::desc("Disable isel of shifter-op"),
  cl::init(false));

//===--------------------------------------------------------------------===//
/// ARMDAGToDAGISel - ARM specific code to select ARM machine
/// instructions for SelectionDAG operations.
///
namespace {

class ARMDAGToDAGISel : public SelectionDAGISel {
  /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
  /// make the right decision when generating code for different targets.
  const ARMSubtarget *Subtarget;

public:
  explicit ARMDAGToDAGISel(ARMBaseTargetMachine &tm, CodeGenOpt::Level OptLevel)
      : SelectionDAGISel(tm, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    // Reset the subtarget each time through.
    Subtarget = &MF.getSubtarget<ARMSubtarget>();
    SelectionDAGISel::runOnMachineFunction(MF);
    return true;
  }

  StringRef getPassName() const override { return "ARM Instruction Selection"; }

  void PreprocessISelDAG() override;

  /// getI32Imm - Return a target constant of type i32 with the specified
  /// value.
  inline SDValue getI32Imm(unsigned Imm, const SDLoc &dl) {
    return CurDAG->getTargetConstant(Imm, dl, MVT::i32);
  }

  void Select(SDNode *N) override;

  bool hasNoVMLxHazardUse(SDNode *N) const;
  bool isShifterOpProfitable(const SDValue &Shift,
                             ARM_AM::ShiftOpc ShOpcVal, unsigned ShAmt);
  bool SelectRegShifterOperand(SDValue N, SDValue &A,
                               SDValue &B, SDValue &C,
                               bool CheckProfitability = true);
  bool SelectImmShifterOperand(SDValue N, SDValue &A,
                               SDValue &B, bool CheckProfitability = true);
  bool SelectShiftRegShifterOperand(SDValue N, SDValue &A,
                                    SDValue &B, SDValue &C) {
    // Don't apply the profitability check
    return SelectRegShifterOperand(N, A, B, C, false);
  }
  bool SelectShiftImmShifterOperand(SDValue N, SDValue &A,
                                    SDValue &B) {
    // Don't apply the profitability check
    return SelectImmShifterOperand(N, A, B, false);
  }

  bool SelectAddLikeOr(SDNode *Parent, SDValue N, SDValue &Out);

  bool SelectAddrModeImm12(SDValue N, SDValue &Base, SDValue &OffImm);
  bool SelectLdStSOReg(SDValue N, SDValue &Base, SDValue &Offset, SDValue &Opc);

  bool SelectCMOVPred(SDValue N, SDValue &Pred, SDValue &Reg) {
    const ConstantSDNode *CN = cast<ConstantSDNode>(N);
    Pred = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(N), MVT::i32);
    Reg = CurDAG->getRegister(ARM::CPSR, MVT::i32);
    return true;
  }

  bool SelectAddrMode2OffsetReg(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode2OffsetImm(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode2OffsetImmPre(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool SelectAddrOffsetNone(SDValue N, SDValue &Base);
  bool SelectAddrMode3(SDValue N, SDValue &Base,
                       SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode3Offset(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool IsAddressingMode5(SDValue N, SDValue &Base, SDValue &Offset,
                         int Lwb, int Upb, bool FP16);
  bool SelectAddrMode5(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectAddrMode5FP16(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectAddrMode6(SDNode *Parent, SDValue N, SDValue &Addr,SDValue &Align);
  bool SelectAddrMode6Offset(SDNode *Op, SDValue N, SDValue &Offset);

  bool SelectAddrModePC(SDValue N, SDValue &Offset, SDValue &Label);

  // Thumb Addressing Modes:
  bool SelectThumbAddrModeRR(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectThumbAddrModeImm5S(SDValue N, unsigned Scale, SDValue &Base,
                                SDValue &OffImm);
  bool SelectThumbAddrModeImm5S1(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeImm5S2(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeImm5S4(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeSP(SDValue N, SDValue &Base, SDValue &OffImm);

  // Thumb 2 Addressing Modes:
  bool SelectT2AddrModeImm12(SDValue N, SDValue &Base, SDValue &OffImm);
  bool SelectT2AddrModeImm8(SDValue N, SDValue &Base,
                            SDValue &OffImm);
  bool SelectT2AddrModeImm8Offset(SDNode *Op, SDValue N,
                                 SDValue &OffImm);
  bool SelectT2AddrModeSoReg(SDValue N, SDValue &Base,
                             SDValue &OffReg, SDValue &ShImm);
  bool SelectT2AddrModeExclusive(SDValue N, SDValue &Base, SDValue &OffImm);

  inline bool is_so_imm(unsigned Imm) const {
    return ARM_AM::getSOImmVal(Imm) != -1;
  }

  inline bool is_so_imm_not(unsigned Imm) const {
    return ARM_AM::getSOImmVal(~Imm) != -1;
  }

  inline bool is_t2_so_imm(unsigned Imm) const {
    return ARM_AM::getT2SOImmVal(Imm) != -1;
  }

  inline bool is_t2_so_imm_not(unsigned Imm) const {
    return ARM_AM::getT2SOImmVal(~Imm) != -1;
  }

  // Include the pieces autogenerated from the target description.
#include "ARMGenDAGISel.inc"

private:
  void transferMemOperands(SDNode *Src, SDNode *Dst);

  /// Indexed (pre/post inc/dec) load matching code for ARM.
  bool tryARMIndexedLoad(SDNode *N);
  bool tryT1IndexedLoad(SDNode *N);
  bool tryT2IndexedLoad(SDNode *N);

  /// SelectVLD - Select NEON load intrinsics.  NumVecs should be
  /// 1, 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// loads of D registers and even subregs and odd subregs of Q registers.
  /// For NumVecs <= 2, QOpcodes1 is not used.
  void SelectVLD(SDNode *N, bool isUpdating, unsigned NumVecs,
                 const uint16_t *DOpcodes, const uint16_t *QOpcodes0,
                 const uint16_t *QOpcodes1);

  /// SelectVST - Select NEON store intrinsics.  NumVecs should
  /// be 1, 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// stores of D registers and even subregs and odd subregs of Q registers.
  /// For NumVecs <= 2, QOpcodes1 is not used.
  void SelectVST(SDNode *N, bool isUpdating, unsigned NumVecs,
                 const uint16_t *DOpcodes, const uint16_t *QOpcodes0,
                 const uint16_t *QOpcodes1);

  /// SelectVLDSTLane - Select NEON load/store lane intrinsics.  NumVecs should
  /// be 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// load/store of D registers and Q registers.
  void SelectVLDSTLane(SDNode *N, bool IsLoad, bool isUpdating,
                       unsigned NumVecs, const uint16_t *DOpcodes,
                       const uint16_t *QOpcodes);

  /// SelectVLDDup - Select NEON load-duplicate intrinsics.  NumVecs
  /// should be 1, 2, 3 or 4.  The opcode array specifies the instructions used
  /// for loading D registers.
  void SelectVLDDup(SDNode *N, bool IsIntrinsic, bool isUpdating,
                    unsigned NumVecs, const uint16_t *DOpcodes,
                    const uint16_t *QOpcodes0 = nullptr,
                    const uint16_t *QOpcodes1 = nullptr);

  /// Try to select SBFX/UBFX instructions for ARM.
  bool tryV6T2BitfieldExtractOp(SDNode *N, bool isSigned);

  // Select special operations if node forms integer ABS pattern
  bool tryABSOp(SDNode *N);

  bool tryReadRegister(SDNode *N);
  bool tryWriteRegister(SDNode *N);

  bool tryInlineAsm(SDNode *N);

  void SelectCMPZ(SDNode *N, bool &SwitchEQNEToPLMI);

  void SelectCMP_SWAP(SDNode *N);

  /// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
  /// inline asm expressions.
  bool SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

  // Form pairs of consecutive R, S, D, or Q registers.
  SDNode *createGPRPairNode(EVT VT, SDValue V0, SDValue V1);
  SDNode *createSRegPairNode(EVT VT, SDValue V0, SDValue V1);
  SDNode *createDRegPairNode(EVT VT, SDValue V0, SDValue V1);
  SDNode *createQRegPairNode(EVT VT, SDValue V0, SDValue V1);

  // Form sequences of 4 consecutive S, D, or Q registers.
  SDNode *createQuadSRegsNode(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);
  SDNode *createQuadDRegsNode(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);
  SDNode *createQuadQRegsNode(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);

  // Get the alignment operand for a NEON VLD or VST instruction.
  SDValue GetVLDSTAlign(SDValue Align, const SDLoc &dl, unsigned NumVecs,
                        bool is64BitVector);

  /// Returns the number of instructions required to materialize the given
  /// constant in a register, or 3 if a literal pool load is needed.
  unsigned ConstantMaterializationCost(unsigned Val) const;

  /// Checks if N is a multiplication by a constant where we can extract out a
  /// power of two from the constant so that it can be used in a shift, but only
  /// if it simplifies the materialization of the constant. Returns true if it
  /// is, and assigns to PowerOfTwo the power of two that should be extracted
  /// out and to NewMulConst the new constant to be multiplied by.
  bool canExtractShiftFromMul(const SDValue &N, unsigned MaxShift,
                              unsigned &PowerOfTwo, SDValue &NewMulConst) const;

  /// Replace N with M in CurDAG, in a way that also ensures that M gets
  /// selected when N would have been selected.
  void replaceDAGValue(const SDValue &N, SDValue M);
};
}

/// isInt32Immediate - This method tests to see if the node is a 32-bit constant
/// operand. If so Imm will receive the 32-bit value.
static bool isInt32Immediate(SDNode *N, unsigned &Imm) {
  if (N->getOpcode() == ISD::Constant && N->getValueType(0) == MVT::i32) {
    Imm = cast<ConstantSDNode>(N)->getZExtValue();
    return true;
  }
  return false;
}

// isInt32Immediate - This method tests to see if a constant operand.
// If so Imm will receive the 32 bit value.
static bool isInt32Immediate(SDValue N, unsigned &Imm) {
  return isInt32Immediate(N.getNode(), Imm);
}

// isOpcWithIntImmediate - This method tests to see if the node is a specific
// opcode and that it has a immediate integer right operand.
// If so Imm will receive the 32 bit value.
static bool isOpcWithIntImmediate(SDNode *N, unsigned Opc, unsigned& Imm) {
  return N->getOpcode() == Opc &&
         isInt32Immediate(N->getOperand(1).getNode(), Imm);
}

/// Check whether a particular node is a constant value representable as
/// (N * Scale) where (N in [\p RangeMin, \p RangeMax).
///
/// \param ScaledConstant [out] - On success, the pre-scaled constant value.
static bool isScaledConstantInRange(SDValue Node, int Scale,
                                    int RangeMin, int RangeMax,
                                    int &ScaledConstant) {
  assert(Scale > 0 && "Invalid scale!");

  // Check that this is a constant.
  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Node);
  if (!C)
    return false;

  ScaledConstant = (int) C->getZExtValue();
  if ((ScaledConstant % Scale) != 0)
    return false;

  ScaledConstant /= Scale;
  return ScaledConstant >= RangeMin && ScaledConstant < RangeMax;
}

void ARMDAGToDAGISel::PreprocessISelDAG() {
  if (!Subtarget->hasV6T2Ops())
    return;

  bool isThumb2 = Subtarget->isThumb();
  for (SelectionDAG::allnodes_iterator I = CurDAG->allnodes_begin(),
       E = CurDAG->allnodes_end(); I != E; ) {
    SDNode *N = &*I++; // Preincrement iterator to avoid invalidation issues.

    if (N->getOpcode() != ISD::ADD)
      continue;

    // Look for (add X1, (and (srl X2, c1), c2)) where c2 is constant with
    // leading zeros, followed by consecutive set bits, followed by 1 or 2
    // trailing zeros, e.g. 1020.
    // Transform the expression to
    // (add X1, (shl (and (srl X2, c1), (c2>>tz)), tz)) where tz is the number
    // of trailing zeros of c2. The left shift would be folded as an shifter
    // operand of 'add' and the 'and' and 'srl' would become a bits extraction
    // node (UBFX).

    SDValue N0 = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    unsigned And_imm = 0;
    if (!isOpcWithIntImmediate(N1.getNode(), ISD::AND, And_imm)) {
      if (isOpcWithIntImmediate(N0.getNode(), ISD::AND, And_imm))
        std::swap(N0, N1);
    }
    if (!And_imm)
      continue;

    // Check if the AND mask is an immediate of the form: 000.....1111111100
    unsigned TZ = countTrailingZeros(And_imm);
    if (TZ != 1 && TZ != 2)
      // Be conservative here. Shifter operands aren't always free. e.g. On
      // Swift, left shifter operand of 1 / 2 for free but others are not.
      // e.g.
      //  ubfx   r3, r1, #16, #8
      //  ldr.w  r3, [r0, r3, lsl #2]
      // vs.
      //  mov.w  r9, #1020
      //  and.w  r2, r9, r1, lsr #14
      //  ldr    r2, [r0, r2]
      continue;
    And_imm >>= TZ;
    if (And_imm & (And_imm + 1))
      continue;

    // Look for (and (srl X, c1), c2).
    SDValue Srl = N1.getOperand(0);
    unsigned Srl_imm = 0;
    if (!isOpcWithIntImmediate(Srl.getNode(), ISD::SRL, Srl_imm) ||
        (Srl_imm <= 2))
      continue;

    // Make sure first operand is not a shifter operand which would prevent
    // folding of the left shift.
    SDValue CPTmp0;
    SDValue CPTmp1;
    SDValue CPTmp2;
    if (isThumb2) {
      if (SelectImmShifterOperand(N0, CPTmp0, CPTmp1))
        continue;
    } else {
      if (SelectImmShifterOperand(N0, CPTmp0, CPTmp1) ||
          SelectRegShifterOperand(N0, CPTmp0, CPTmp1, CPTmp2))
        continue;
    }

    // Now make the transformation.
    Srl = CurDAG->getNode(ISD::SRL, SDLoc(Srl), MVT::i32,
                          Srl.getOperand(0),
                          CurDAG->getConstant(Srl_imm + TZ, SDLoc(Srl),
                                              MVT::i32));
    N1 = CurDAG->getNode(ISD::AND, SDLoc(N1), MVT::i32,
                         Srl,
                         CurDAG->getConstant(And_imm, SDLoc(Srl), MVT::i32));
    N1 = CurDAG->getNode(ISD::SHL, SDLoc(N1), MVT::i32,
                         N1, CurDAG->getConstant(TZ, SDLoc(Srl), MVT::i32));
    CurDAG->UpdateNodeOperands(N, N0, N1);
  }
}

/// hasNoVMLxHazardUse - Return true if it's desirable to select a FP MLA / MLS
/// node. VFP / NEON fp VMLA / VMLS instructions have special RAW hazards (at
/// least on current ARM implementations) which should be avoidded.
bool ARMDAGToDAGISel::hasNoVMLxHazardUse(SDNode *N) const {
  if (OptLevel == CodeGenOpt::None)
    return true;

  if (!Subtarget->hasVMLxHazards())
    return true;

  if (!N->hasOneUse())
    return false;

  SDNode *Use = *N->use_begin();
  if (Use->getOpcode() == ISD::CopyToReg)
    return true;
  if (Use->isMachineOpcode()) {
    const ARMBaseInstrInfo *TII = static_cast<const ARMBaseInstrInfo *>(
        CurDAG->getSubtarget().getInstrInfo());

    const MCInstrDesc &MCID = TII->get(Use->getMachineOpcode());
    if (MCID.mayStore())
      return true;
    unsigned Opcode = MCID.getOpcode();
    if (Opcode == ARM::VMOVRS || Opcode == ARM::VMOVRRD)
      return true;
    // vmlx feeding into another vmlx. We actually want to unfold
    // the use later in the MLxExpansion pass. e.g.
    // vmla
    // vmla (stall 8 cycles)
    //
    // vmul (5 cycles)
    // vadd (5 cycles)
    // vmla
    // This adds up to about 18 - 19 cycles.
    //
    // vmla
    // vmul (stall 4 cycles)
    // vadd adds up to about 14 cycles.
    return TII->isFpMLxInstruction(Opcode);
  }

  return false;
}

bool ARMDAGToDAGISel::isShifterOpProfitable(const SDValue &Shift,
                                            ARM_AM::ShiftOpc ShOpcVal,
                                            unsigned ShAmt) {
  if (!Subtarget->isLikeA9() && !Subtarget->isSwift())
    return true;
  if (Shift.hasOneUse())
    return true;
  // R << 2 is free.
  return ShOpcVal == ARM_AM::lsl &&
         (ShAmt == 2 || (Subtarget->isSwift() && ShAmt == 1));
}

unsigned ARMDAGToDAGISel::ConstantMaterializationCost(unsigned Val) const {
  if (Subtarget->isThumb()) {
    if (Val <= 255) return 1;                               // MOV
    if (Subtarget->hasV6T2Ops() &&
        (Val <= 0xffff || ARM_AM::getT2SOImmValSplatVal(Val) != -1))
      return 1; // MOVW
    if (Val <= 510) return 2;                               // MOV + ADDi8
    if (~Val <= 255) return 2;                              // MOV + MVN
    if (ARM_AM::isThumbImmShiftedVal(Val)) return 2;        // MOV + LSL
  } else {
    if (ARM_AM::getSOImmVal(Val) != -1) return 1;           // MOV
    if (ARM_AM::getSOImmVal(~Val) != -1) return 1;          // MVN
    if (Subtarget->hasV6T2Ops() && Val <= 0xffff) return 1; // MOVW
    if (ARM_AM::isSOImmTwoPartVal(Val)) return 2;           // two instrs
  }
  if (Subtarget->useMovt(*MF)) return 2; // MOVW + MOVT
  return 3; // Literal pool load
}

bool ARMDAGToDAGISel::canExtractShiftFromMul(const SDValue &N,
                                             unsigned MaxShift,
                                             unsigned &PowerOfTwo,
                                             SDValue &NewMulConst) const {
  assert(N.getOpcode() == ISD::MUL);
  assert(MaxShift > 0);

  // If the multiply is used in more than one place then changing the constant
  // will make other uses incorrect, so don't.
  if (!N.hasOneUse()) return false;
  // Check if the multiply is by a constant
  ConstantSDNode *MulConst = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!MulConst) return false;
  // If the constant is used in more than one place then modifying it will mean
  // we need to materialize two constants instead of one, which is a bad idea.
  if (!MulConst->hasOneUse()) return false;
  unsigned MulConstVal = MulConst->getZExtValue();
  if (MulConstVal == 0) return false;

  // Find the largest power of 2 that MulConstVal is a multiple of
  PowerOfTwo = MaxShift;
  while ((MulConstVal % (1 << PowerOfTwo)) != 0) {
    --PowerOfTwo;
    if (PowerOfTwo == 0) return false;
  }

  // Only optimise if the new cost is better
  unsigned NewMulConstVal = MulConstVal / (1 << PowerOfTwo);
  NewMulConst = CurDAG->getConstant(NewMulConstVal, SDLoc(N), MVT::i32);
  unsigned OldCost = ConstantMaterializationCost(MulConstVal);
  unsigned NewCost = ConstantMaterializationCost(NewMulConstVal);
  return NewCost < OldCost;
}

void ARMDAGToDAGISel::replaceDAGValue(const SDValue &N, SDValue M) {
  CurDAG->RepositionNode(N.getNode()->getIterator(), M.getNode());
  ReplaceUses(N, M);
}

bool ARMDAGToDAGISel::SelectImmShifterOperand(SDValue N,
                                              SDValue &BaseReg,
                                              SDValue &Opc,
                                              bool CheckProfitability) {
  if (DisableShifterOp)
    return false;

  // If N is a multiply-by-constant and it's profitable to extract a shift and
  // use it in a shifted operand do so.
  if (N.getOpcode() == ISD::MUL) {
    unsigned PowerOfTwo = 0;
    SDValue NewMulConst;
    if (canExtractShiftFromMul(N, 31, PowerOfTwo, NewMulConst)) {
      HandleSDNode Handle(N);
      SDLoc Loc(N);
      replaceDAGValue(N.getOperand(1), NewMulConst);
      BaseReg = Handle.getValue();
      Opc = CurDAG->getTargetConstant(
          ARM_AM::getSORegOpc(ARM_AM::lsl, PowerOfTwo), Loc, MVT::i32);
      return true;
    }
  }

  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOpcode());

  // Don't match base register only case. That is matched to a separate
  // lower complexity pattern with explicit register operand.
  if (ShOpcVal == ARM_AM::no_shift) return false;

  BaseReg = N.getOperand(0);
  unsigned ShImmVal = 0;
  ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!RHS) return false;
  ShImmVal = RHS->getZExtValue() & 31;
  Opc = CurDAG->getTargetConstant(ARM_AM::getSORegOpc(ShOpcVal, ShImmVal),
                                  SDLoc(N), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectRegShifterOperand(SDValue N,
                                              SDValue &BaseReg,
                                              SDValue &ShReg,
                                              SDValue &Opc,
                                              bool CheckProfitability) {
  if (DisableShifterOp)
    return false;

  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOpcode());

  // Don't match base register only case. That is matched to a separate
  // lower complexity pattern with explicit register operand.
  if (ShOpcVal == ARM_AM::no_shift) return false;

  BaseReg = N.getOperand(0);
  unsigned ShImmVal = 0;
  ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (RHS) return false;

  ShReg = N.getOperand(1);
  if (CheckProfitability && !isShifterOpProfitable(N, ShOpcVal, ShImmVal))
    return false;
  Opc = CurDAG->getTargetConstant(ARM_AM::getSORegOpc(ShOpcVal, ShImmVal),
                                  SDLoc(N), MVT::i32);
  return true;
}

// Determine whether an ISD::OR's operands are suitable to turn the operation
// into an addition, which often has more compact encodings.
bool ARMDAGToDAGISel::SelectAddLikeOr(SDNode *Parent, SDValue N, SDValue &Out) {
  assert(Parent->getOpcode() == ISD::OR && "unexpected parent");
  Out = N;
  return CurDAG->haveNoCommonBitsSet(N, Parent->getOperand(1));
}


bool ARMDAGToDAGISel::SelectAddrModeImm12(SDValue N,
                                          SDValue &Base,
                                          SDValue &OffImm) {
  // Match simple R + imm12 operands.

  // Base only.
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB &&
      !CurDAG->isBaseWithConstantOffset(N)) {
    if (N.getOpcode() == ISD::FrameIndex) {
      // Match frame index.
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
      return true;
    }

    if (N.getOpcode() == ARMISD::Wrapper &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalAddress &&
        N.getOperand(0).getOpcode() != ISD::TargetExternalSymbol &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalTLSAddress) {
      Base = N.getOperand(0);
    } else
      Base = N;
    OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
    return true;
  }

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int RHSC = (int)RHS->getSExtValue();
    if (N.getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if (RHSC > -0x1000 && RHSC < 0x1000) { // 12 bits
      Base   = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(
            FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      }
      OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32);
      return true;
    }
  }

  // Base only.
  Base = N;
  OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
  return true;
}



bool ARMDAGToDAGISel::SelectLdStSOReg(SDValue N, SDValue &Base, SDValue &Offset,
                                      SDValue &Opc) {
  if (N.getOpcode() == ISD::MUL &&
      ((!Subtarget->isLikeA9() && !Subtarget->isSwift()) || N.hasOneUse())) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      // X * [3,5,9] -> X + X * [2,4,8] etc.
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC & 1) {
        RHSC = RHSC & ~1;
        ARM_AM::AddrOpc AddSub = ARM_AM::add;
        if (RHSC < 0) {
          AddSub = ARM_AM::sub;
          RHSC = - RHSC;
        }
        if (isPowerOf2_32(RHSC)) {
          unsigned ShAmt = Log2_32(RHSC);
          Base = Offset = N.getOperand(0);
          Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt,
                                                            ARM_AM::lsl),
                                          SDLoc(N), MVT::i32);
          return true;
        }
      }
    }
  }

  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB &&
      // ISD::OR that is equivalent to an ISD::ADD.
      !CurDAG->isBaseWithConstantOffset(N))
    return false;

  // Leave simple R +/- imm12 operands for LDRi12
  if (N.getOpcode() == ISD::ADD || N.getOpcode() == ISD::OR) {
    int RHSC;
    if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/1,
                                -0x1000+1, 0x1000, RHSC)) // 12 bits.
      return false;
  }

  // Otherwise this is R +/- [possibly shifted] R.
  ARM_AM::AddrOpc AddSub = N.getOpcode() == ISD::SUB ? ARM_AM::sub:ARM_AM::add;
  ARM_AM::ShiftOpc ShOpcVal =
    ARM_AM::getShiftOpcForNode(N.getOperand(1).getOpcode());
  unsigned ShAmt = 0;

  Base   = N.getOperand(0);
  Offset = N.getOperand(1);

  if (ShOpcVal != ARM_AM::no_shift) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh =
           dyn_cast<ConstantSDNode>(N.getOperand(1).getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (isShifterOpProfitable(Offset, ShOpcVal, ShAmt))
        Offset = N.getOperand(1).getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  // Try matching (R shl C) + (R).
  if (N.getOpcode() != ISD::SUB && ShOpcVal == ARM_AM::no_shift &&
      !(Subtarget->isLikeA9() || Subtarget->isSwift() ||
        N.getOperand(0).hasOneUse())) {
    ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOperand(0).getOpcode());
    if (ShOpcVal != ARM_AM::no_shift) {
      // Check to see if the RHS of the shift is a constant, if not, we can't
      // fold it.
      if (ConstantSDNode *Sh =
          dyn_cast<ConstantSDNode>(N.getOperand(0).getOperand(1))) {
        ShAmt = Sh->getZExtValue();
        if (isShifterOpProfitable(N.getOperand(0), ShOpcVal, ShAmt)) {
          Offset = N.getOperand(0).getOperand(0);
          Base = N.getOperand(1);
        } else {
          ShAmt = 0;
          ShOpcVal = ARM_AM::no_shift;
        }
      } else {
        ShOpcVal = ARM_AM::no_shift;
      }
    }
  }

  // If Offset is a multiply-by-constant and it's profitable to extract a shift
  // and use it in a shifted operand do so.
  if (Offset.getOpcode() == ISD::MUL && N.hasOneUse()) {
    unsigned PowerOfTwo = 0;
    SDValue NewMulConst;
    if (canExtractShiftFromMul(Offset, 31, PowerOfTwo, NewMulConst)) {
      HandleSDNode Handle(Offset);
      replaceDAGValue(Offset.getOperand(1), NewMulConst);
      Offset = Handle.getValue();
      ShAmt = PowerOfTwo;
      ShOpcVal = ARM_AM::lsl;
    }
  }

  Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt, ShOpcVal),
                                  SDLoc(N), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode2OffsetReg(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x1000, Val))
    return false;

  Offset = N;
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOpcode());
  unsigned ShAmt = 0;
  if (ShOpcVal != ARM_AM::no_shift) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (isShifterOpProfitable(N, ShOpcVal, ShAmt))
        Offset = N.getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt, ShOpcVal),
                                  SDLoc(N), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode2OffsetImmPre(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x1000, Val)) { // 12 bits.
    if (AddSub == ARM_AM::sub) Val *= -1;
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(Val, SDLoc(Op), MVT::i32);
    return true;
  }

  return false;
}


bool ARMDAGToDAGISel::SelectAddrMode2OffsetImm(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x1000, Val)) { // 12 bits.
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, Val,
                                                      ARM_AM::no_shift),
                                    SDLoc(Op), MVT::i32);
    return true;
  }

  return false;
}

bool ARMDAGToDAGISel::SelectAddrOffsetNone(SDValue N, SDValue &Base) {
  Base = N;
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode3(SDValue N,
                                      SDValue &Base, SDValue &Offset,
                                      SDValue &Opc) {
  if (N.getOpcode() == ISD::SUB) {
    // X - C  is canonicalize to X + -C, no need to handle it here.
    Base = N.getOperand(0);
    Offset = N.getOperand(1);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::sub, 0), SDLoc(N),
                                    MVT::i32);
    return true;
  }

  if (!CurDAG->isBaseWithConstantOffset(N)) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    }
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::add, 0), SDLoc(N),
                                    MVT::i32);
    return true;
  }

  // If the RHS is +/- imm8, fold into addr mode.
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/1,
                              -256 + 1, 256, RHSC)) { // 8 bits.
    Base = N.getOperand(0);
    if (Base.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(Base)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    }
    Offset = CurDAG->getRegister(0, MVT::i32);

    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (RHSC < 0) {
      AddSub = ARM_AM::sub;
      RHSC = -RHSC;
    }
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, RHSC), SDLoc(N),
                                    MVT::i32);
    return true;
  }

  Base = N.getOperand(0);
  Offset = N.getOperand(1);
  Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::add, 0), SDLoc(N),
                                  MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode3Offset(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 256, Val)) { // 12 bits.
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, Val), SDLoc(Op),
                                    MVT::i32);
    return true;
  }

  Offset = N;
  Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, 0), SDLoc(Op),
                                  MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::IsAddressingMode5(SDValue N, SDValue &Base, SDValue &Offset,
                                        int Lwb, int Upb, bool FP16) {
  if (!CurDAG->isBaseWithConstantOffset(N)) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    } else if (N.getOpcode() == ARMISD::Wrapper &&
               N.getOperand(0).getOpcode() != ISD::TargetGlobalAddress &&
               N.getOperand(0).getOpcode() != ISD::TargetExternalSymbol &&
               N.getOperand(0).getOpcode() != ISD::TargetGlobalTLSAddress) {
      Base = N.getOperand(0);
    }
    Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(ARM_AM::add, 0),
                                       SDLoc(N), MVT::i32);
    return true;
  }

  // If the RHS is +/- imm8, fold into addr mode.
  int RHSC;
  const int Scale = FP16 ? 2 : 4;

  if (isScaledConstantInRange(N.getOperand(1), Scale, Lwb, Upb, RHSC)) {
    Base = N.getOperand(0);
    if (Base.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(Base)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    }

    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (RHSC < 0) {
      AddSub = ARM_AM::sub;
      RHSC = -RHSC;
    }

    if (FP16)
      Offset = CurDAG->getTargetConstant(ARM_AM::getAM5FP16Opc(AddSub, RHSC),
                                         SDLoc(N), MVT::i32);
    else
      Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(AddSub, RHSC),
                                         SDLoc(N), MVT::i32);

    return true;
  }

  Base = N;

  if (FP16)
    Offset = CurDAG->getTargetConstant(ARM_AM::getAM5FP16Opc(ARM_AM::add, 0),
                                       SDLoc(N), MVT::i32);
  else
    Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(ARM_AM::add, 0),
                                       SDLoc(N), MVT::i32);

  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode5(SDValue N,
                                      SDValue &Base, SDValue &Offset) {
  int Lwb = -256 + 1;
  int Upb = 256;
  return IsAddressingMode5(N, Base, Offset, Lwb, Upb, /*FP16=*/ false);
}

bool ARMDAGToDAGISel::SelectAddrMode5FP16(SDValue N,
                                          SDValue &Base, SDValue &Offset) {
  int Lwb = -512 + 1;
  int Upb = 512;
  return IsAddressingMode5(N, Base, Offset, Lwb, Upb, /*FP16=*/ true);
}

bool ARMDAGToDAGISel::SelectAddrMode6(SDNode *Parent, SDValue N, SDValue &Addr,
                                      SDValue &Align) {
  Addr = N;

  unsigned Alignment = 0;

  MemSDNode *MemN = cast<MemSDNode>(Parent);

  if (isa<LSBaseSDNode>(MemN) ||
      ((MemN->getOpcode() == ARMISD::VST1_UPD ||
        MemN->getOpcode() == ARMISD::VLD1_UPD) &&
       MemN->getConstantOperandVal(MemN->getNumOperands() - 1) == 1)) {
    // This case occurs only for VLD1-lane/dup and VST1-lane instructions.
    // The maximum alignment is equal to the memory size being referenced.
    unsigned MMOAlign = MemN->getAlignment();
    unsigned MemSize = MemN->getMemoryVT().getSizeInBits() / 8;
    if (MMOAlign >= MemSize && MemSize > 1)
      Alignment = MemSize;
  } else {
    // All other uses of addrmode6 are for intrinsics.  For now just record
    // the raw alignment value; it will be refined later based on the legal
    // alignment operands for the intrinsic.
    Alignment = MemN->getAlignment();
  }

  Align = CurDAG->getTargetConstant(Alignment, SDLoc(N), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode6Offset(SDNode *Op, SDValue N,
                                            SDValue &Offset) {
  LSBaseSDNode *LdSt = cast<LSBaseSDNode>(Op);
  ISD::MemIndexedMode AM = LdSt->getAddressingMode();
  if (AM != ISD::POST_INC)
    return false;
  Offset = N;
  if (ConstantSDNode *NC = dyn_cast<ConstantSDNode>(N)) {
    if (NC->getZExtValue() * 8 == LdSt->getMemoryVT().getSizeInBits())
      Offset = CurDAG->getRegister(0, MVT::i32);
  }
  return true;
}

bool ARMDAGToDAGISel::SelectAddrModePC(SDValue N,
                                       SDValue &Offset, SDValue &Label) {
  if (N.getOpcode() == ARMISD::PIC_ADD && N.hasOneUse()) {
    Offset = N.getOperand(0);
    SDValue N1 = N.getOperand(1);
    Label = CurDAG->getTargetConstant(cast<ConstantSDNode>(N1)->getZExtValue(),
                                      SDLoc(N), MVT::i32);
    return true;
  }

  return false;
}


//===----------------------------------------------------------------------===//
//                         Thumb Addressing Modes
//===----------------------------------------------------------------------===//

bool ARMDAGToDAGISel::SelectThumbAddrModeRR(SDValue N,
                                            SDValue &Base, SDValue &Offset){
  if (N.getOpcode() != ISD::ADD && !CurDAG->isBaseWithConstantOffset(N)) {
    ConstantSDNode *NC = dyn_cast<ConstantSDNode>(N);
    if (!NC || !NC->isNullValue())
      return false;

    Base = Offset = N;
    return true;
  }

  Base = N.getOperand(0);
  Offset = N.getOperand(1);
  return true;
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S(SDValue N, unsigned Scale,
                                          SDValue &Base, SDValue &OffImm) {
  if (!CurDAG->isBaseWithConstantOffset(N)) {
    if (N.getOpcode() == ISD::ADD) {
      return false; // We want to select register offset instead
    } else if (N.getOpcode() == ARMISD::Wrapper &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalAddress &&
        N.getOperand(0).getOpcode() != ISD::TargetExternalSymbol &&
        N.getOperand(0).getOpcode() != ISD::TargetConstantPool &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalTLSAddress) {
      Base = N.getOperand(0);
    } else {
      Base = N;
    }

    OffImm = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
    return true;
  }

  // If the RHS is + imm5 * scale, fold into addr mode.
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), Scale, 0, 32, RHSC)) {
    Base = N.getOperand(0);
    OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32);
    return true;
  }

  // Offset is too large, so use register offset instead.
  return false;
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S4(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 4, Base, OffImm);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S2(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 2, Base, OffImm);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S1(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 1, Base, OffImm);
}

bool ARMDAGToDAGISel::SelectThumbAddrModeSP(SDValue N,
                                            SDValue &Base, SDValue &OffImm) {
  if (N.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    // Only multiples of 4 are allowed for the offset, so the frame object
    // alignment must be at least 4.
    MachineFrameInfo &MFI = MF->getFrameInfo();
    if (MFI.getObjectAlignment(FI) < 4)
      MFI.setObjectAlignment(FI, 4);
    Base = CurDAG->getTargetFrameIndex(
        FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    OffImm = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
    return true;
  }

  if (!CurDAG->isBaseWithConstantOffset(N))
    return false;

  RegisterSDNode *LHSR = dyn_cast<RegisterSDNode>(N.getOperand(0));
  if (N.getOperand(0).getOpcode() == ISD::FrameIndex ||
      (LHSR && LHSR->getReg() == ARM::SP)) {
    // If the RHS is + imm8 * scale, fold into addr mode.
    int RHSC;
    if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/4, 0, 256, RHSC)) {
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        // For LHS+RHS to result in an offset that's a multiple of 4 the object
        // indexed by the LHS must be 4-byte aligned.
        MachineFrameInfo &MFI = MF->getFrameInfo();
        if (MFI.getObjectAlignment(FI) < 4)
          MFI.setObjectAlignment(FI, 4);
        Base = CurDAG->getTargetFrameIndex(
            FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      }
      OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32);
      return true;
    }
  }

  return false;
}


//===----------------------------------------------------------------------===//
//                        Thumb 2 Addressing Modes
//===----------------------------------------------------------------------===//


bool ARMDAGToDAGISel::SelectT2AddrModeImm12(SDValue N,
                                            SDValue &Base, SDValue &OffImm) {
  // Match simple R + imm12 operands.

  // Base only.
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB &&
      !CurDAG->isBaseWithConstantOffset(N)) {
    if (N.getOpcode() == ISD::FrameIndex) {
      // Match frame index.
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(
          FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
      return true;
    }

    if (N.getOpcode() == ARMISD::Wrapper &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalAddress &&
        N.getOperand(0).getOpcode() != ISD::TargetExternalSymbol &&
        N.getOperand(0).getOpcode() != ISD::TargetGlobalTLSAddress) {
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::TargetConstantPool)
        return false;  // We want to select t2LDRpci instead.
    } else
      Base = N;
    OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
    return true;
  }

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    if (SelectT2AddrModeImm8(N, Base, OffImm))
      // Let t2LDRi8 handle (R - imm8).
      return false;

    int RHSC = (int)RHS->getZExtValue();
    if (N.getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if (RHSC >= 0 && RHSC < 0x1000) { // 12 bits (unsigned)
      Base   = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(
            FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      }
      OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32);
      return true;
    }
  }

  // Base only.
  Base = N;
  OffImm  = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectT2AddrModeImm8(SDValue N,
                                           SDValue &Base, SDValue &OffImm) {
  // Match simple R - imm8 operands.
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB &&
      !CurDAG->isBaseWithConstantOffset(N))
    return false;

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int RHSC = (int)RHS->getSExtValue();
    if (N.getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if ((RHSC >= -255) && (RHSC < 0)) { // 8 bits (always negative)
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(
            FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      }
      OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32);
      return true;
    }
  }

  return false;
}

bool ARMDAGToDAGISel::SelectT2AddrModeImm8Offset(SDNode *Op, SDValue N,
                                                 SDValue &OffImm){
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  int RHSC;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x100, RHSC)) { // 8 bits.
    OffImm = ((AM == ISD::PRE_INC) || (AM == ISD::POST_INC))
      ? CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i32)
      : CurDAG->getTargetConstant(-RHSC, SDLoc(N), MVT::i32);
    return true;
  }

  return false;
}

bool ARMDAGToDAGISel::SelectT2AddrModeSoReg(SDValue N,
                                            SDValue &Base,
                                            SDValue &OffReg, SDValue &ShImm) {
  // (R - imm8) should be handled by t2LDRi8. The rest are handled by t2LDRi12.
  if (N.getOpcode() != ISD::ADD && !CurDAG->isBaseWithConstantOffset(N))
    return false;

  // Leave (R + imm12) for t2LDRi12, (R - imm8) for t2LDRi8.
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int RHSC = (int)RHS->getZExtValue();
    if (RHSC >= 0 && RHSC < 0x1000) // 12 bits (unsigned)
      return false;
    else if (RHSC < 0 && RHSC >= -255) // 8 bits
      return false;
  }

  // Look for (R + R) or (R + (R << [1,2,3])).
  unsigned ShAmt = 0;
  Base   = N.getOperand(0);
  OffReg = N.getOperand(1);

  // Swap if it is ((R << c) + R).
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(OffReg.getOpcode());
  if (ShOpcVal != ARM_AM::lsl) {
    ShOpcVal = ARM_AM::getShiftOpcForNode(Base.getOpcode());
    if (ShOpcVal == ARM_AM::lsl)
      std::swap(Base, OffReg);
  }

  if (ShOpcVal == ARM_AM::lsl) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh = dyn_cast<ConstantSDNode>(OffReg.getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (ShAmt < 4 && isShifterOpProfitable(OffReg, ShOpcVal, ShAmt))
        OffReg = OffReg.getOperand(0);
      else {
        ShAmt = 0;
      }
    }
  }

  // If OffReg is a multiply-by-constant and it's profitable to extract a shift
  // and use it in a shifted operand do so.
  if (OffReg.getOpcode() == ISD::MUL && N.hasOneUse()) {
    unsigned PowerOfTwo = 0;
    SDValue NewMulConst;
    if (canExtractShiftFromMul(OffReg, 3, PowerOfTwo, NewMulConst)) {
      HandleSDNode Handle(OffReg);
      replaceDAGValue(OffReg.getOperand(1), NewMulConst);
      OffReg = Handle.getValue();
      ShAmt = PowerOfTwo;
    }
  }

  ShImm = CurDAG->getTargetConstant(ShAmt, SDLoc(N), MVT::i32);

  return true;
}

bool ARMDAGToDAGISel::SelectT2AddrModeExclusive(SDValue N, SDValue &Base,
                                                SDValue &OffImm) {
  // This *must* succeed since it's used for the irreplaceable ldrex and strex
  // instructions.
  Base = N;
  OffImm = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);

  if (N.getOpcode() != ISD::ADD || !CurDAG->isBaseWithConstantOffset(N))
    return true;

  ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!RHS)
    return true;

  uint32_t RHSC = (int)RHS->getZExtValue();
  if (RHSC > 1020 || RHSC % 4 != 0)
    return true;

  Base = N.getOperand(0);
  if (Base.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Base)->getIndex();
    Base = CurDAG->getTargetFrameIndex(
        FI, TLI->getPointerTy(CurDAG->getDataLayout()));
  }

  OffImm = CurDAG->getTargetConstant(RHSC/4, SDLoc(N), MVT::i32);
  return true;
}

//===--------------------------------------------------------------------===//

/// getAL - Returns a ARMCC::AL immediate node.
static inline SDValue getAL(SelectionDAG *CurDAG, const SDLoc &dl) {
  return CurDAG->getTargetConstant((uint64_t)ARMCC::AL, dl, MVT::i32);
}

void ARMDAGToDAGISel::transferMemOperands(SDNode *N, SDNode *Result) {
  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});
}

bool ARMDAGToDAGISel::tryARMIndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM == ISD::UNINDEXED)
    return false;

  EVT LoadedVT = LD->getMemoryVT();
  SDValue Offset, AMOpc;
  bool isPre = (AM == ISD::PRE_INC) || (AM == ISD::PRE_DEC);
  unsigned Opcode = 0;
  bool Match = false;
  if (LoadedVT == MVT::i32 && isPre &&
      SelectAddrMode2OffsetImmPre(N, LD->getOffset(), Offset, AMOpc)) {
    Opcode = ARM::LDR_PRE_IMM;
    Match = true;
  } else if (LoadedVT == MVT::i32 && !isPre &&
      SelectAddrMode2OffsetImm(N, LD->getOffset(), Offset, AMOpc)) {
    Opcode = ARM::LDR_POST_IMM;
    Match = true;
  } else if (LoadedVT == MVT::i32 &&
      SelectAddrMode2OffsetReg(N, LD->getOffset(), Offset, AMOpc)) {
    Opcode = isPre ? ARM::LDR_PRE_REG : ARM::LDR_POST_REG;
    Match = true;

  } else if (LoadedVT == MVT::i16 &&
             SelectAddrMode3Offset(N, LD->getOffset(), Offset, AMOpc)) {
    Match = true;
    Opcode = (LD->getExtensionType() == ISD::SEXTLOAD)
      ? (isPre ? ARM::LDRSH_PRE : ARM::LDRSH_POST)
      : (isPre ? ARM::LDRH_PRE : ARM::LDRH_POST);
  } else if (LoadedVT == MVT::i8 || LoadedVT == MVT::i1) {
    if (LD->getExtensionType() == ISD::SEXTLOAD) {
      if (SelectAddrMode3Offset(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = isPre ? ARM::LDRSB_PRE : ARM::LDRSB_POST;
      }
    } else {
      if (isPre &&
          SelectAddrMode2OffsetImmPre(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = ARM::LDRB_PRE_IMM;
      } else if (!isPre &&
                  SelectAddrMode2OffsetImm(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = ARM::LDRB_POST_IMM;
      } else if (SelectAddrMode2OffsetReg(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = isPre ? ARM::LDRB_PRE_REG : ARM::LDRB_POST_REG;
      }
    }
  }

  if (Match) {
    if (Opcode == ARM::LDR_PRE_IMM || Opcode == ARM::LDRB_PRE_IMM) {
      SDValue Chain = LD->getChain();
      SDValue Base = LD->getBasePtr();
      SDValue Ops[]= { Base, AMOpc, getAL(CurDAG, SDLoc(N)),
                       CurDAG->getRegister(0, MVT::i32), Chain };
      SDNode *New = CurDAG->getMachineNode(Opcode, SDLoc(N), MVT::i32, MVT::i32,
                                           MVT::Other, Ops);
      transferMemOperands(N, New);
      ReplaceNode(N, New);
      return true;
    } else {
      SDValue Chain = LD->getChain();
      SDValue Base = LD->getBasePtr();
      SDValue Ops[]= { Base, Offset, AMOpc, getAL(CurDAG, SDLoc(N)),
                       CurDAG->getRegister(0, MVT::i32), Chain };
      SDNode *New = CurDAG->getMachineNode(Opcode, SDLoc(N), MVT::i32, MVT::i32,
                                           MVT::Other, Ops);
      transferMemOperands(N, New);
      ReplaceNode(N, New);
      return true;
    }
  }

  return false;
}

bool ARMDAGToDAGISel::tryT1IndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  EVT LoadedVT = LD->getMemoryVT();
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM != ISD::POST_INC || LD->getExtensionType() != ISD::NON_EXTLOAD ||
      LoadedVT.getSimpleVT().SimpleTy != MVT::i32)
    return false;

  auto *COffs = dyn_cast<ConstantSDNode>(LD->getOffset());
  if (!COffs || COffs->getZExtValue() != 4)
    return false;

  // A T1 post-indexed load is just a single register LDM: LDM r0!, {r1}.
  // The encoding of LDM is not how the rest of ISel expects a post-inc load to
  // look however, so we use a pseudo here and switch it for a tLDMIA_UPD after
  // ISel.
  SDValue Chain = LD->getChain();
  SDValue Base = LD->getBasePtr();
  SDValue Ops[]= { Base, getAL(CurDAG, SDLoc(N)),
                   CurDAG->getRegister(0, MVT::i32), Chain };
  SDNode *New = CurDAG->getMachineNode(ARM::tLDR_postidx, SDLoc(N), MVT::i32,
                                       MVT::i32, MVT::Other, Ops);
  transferMemOperands(N, New);
  ReplaceNode(N, New);
  return true;
}

bool ARMDAGToDAGISel::tryT2IndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM == ISD::UNINDEXED)
    return false;

  EVT LoadedVT = LD->getMemoryVT();
  bool isSExtLd = LD->getExtensionType() == ISD::SEXTLOAD;
  SDValue Offset;
  bool isPre = (AM == ISD::PRE_INC) || (AM == ISD::PRE_DEC);
  unsigned Opcode = 0;
  bool Match = false;
  if (SelectT2AddrModeImm8Offset(N, LD->getOffset(), Offset)) {
    switch (LoadedVT.getSimpleVT().SimpleTy) {
    case MVT::i32:
      Opcode = isPre ? ARM::t2LDR_PRE : ARM::t2LDR_POST;
      break;
    case MVT::i16:
      if (isSExtLd)
        Opcode = isPre ? ARM::t2LDRSH_PRE : ARM::t2LDRSH_POST;
      else
        Opcode = isPre ? ARM::t2LDRH_PRE : ARM::t2LDRH_POST;
      break;
    case MVT::i8:
    case MVT::i1:
      if (isSExtLd)
        Opcode = isPre ? ARM::t2LDRSB_PRE : ARM::t2LDRSB_POST;
      else
        Opcode = isPre ? ARM::t2LDRB_PRE : ARM::t2LDRB_POST;
      break;
    default:
      return false;
    }
    Match = true;
  }

  if (Match) {
    SDValue Chain = LD->getChain();
    SDValue Base = LD->getBasePtr();
    SDValue Ops[]= { Base, Offset, getAL(CurDAG, SDLoc(N)),
                     CurDAG->getRegister(0, MVT::i32), Chain };
    SDNode *New = CurDAG->getMachineNode(Opcode, SDLoc(N), MVT::i32, MVT::i32,
                                         MVT::Other, Ops);
    transferMemOperands(N, New);
    ReplaceNode(N, New);
    return true;
  }

  return false;
}

/// Form a GPRPair pseudo register from a pair of GPR regs.
SDNode *ARMDAGToDAGISel::createGPRPairNode(EVT VT, SDValue V0, SDValue V1) {
  SDLoc dl(V0.getNode());
  SDValue RegClass =
    CurDAG->getTargetConstant(ARM::GPRPairRegClassID, dl, MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::gsub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::gsub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form a D register from a pair of S registers.
SDNode *ARMDAGToDAGISel::createSRegPairNode(EVT VT, SDValue V0, SDValue V1) {
  SDLoc dl(V0.getNode());
  SDValue RegClass =
    CurDAG->getTargetConstant(ARM::DPR_VFP2RegClassID, dl, MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::ssub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::ssub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form a quad register from a pair of D registers.
SDNode *ARMDAGToDAGISel::createDRegPairNode(EVT VT, SDValue V0, SDValue V1) {
  SDLoc dl(V0.getNode());
  SDValue RegClass = CurDAG->getTargetConstant(ARM::QPRRegClassID, dl,
                                               MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::dsub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::dsub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form 4 consecutive D registers from a pair of Q registers.
SDNode *ARMDAGToDAGISel::createQRegPairNode(EVT VT, SDValue V0, SDValue V1) {
  SDLoc dl(V0.getNode());
  SDValue RegClass = CurDAG->getTargetConstant(ARM::QQPRRegClassID, dl,
                                               MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::qsub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::qsub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form 4 consecutive S registers.
SDNode *ARMDAGToDAGISel::createQuadSRegsNode(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  SDLoc dl(V0.getNode());
  SDValue RegClass =
    CurDAG->getTargetConstant(ARM::QPR_VFP2RegClassID, dl, MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::ssub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::ssub_1, dl, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::ssub_2, dl, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::ssub_3, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1,
                                    V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form 4 consecutive D registers.
SDNode *ARMDAGToDAGISel::createQuadDRegsNode(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  SDLoc dl(V0.getNode());
  SDValue RegClass = CurDAG->getTargetConstant(ARM::QQPRRegClassID, dl,
                                               MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::dsub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::dsub_1, dl, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::dsub_2, dl, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::dsub_3, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1,
                                    V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// Form 4 consecutive Q registers.
SDNode *ARMDAGToDAGISel::createQuadQRegsNode(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  SDLoc dl(V0.getNode());
  SDValue RegClass = CurDAG->getTargetConstant(ARM::QQQQPRRegClassID, dl,
                                               MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::qsub_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::qsub_1, dl, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::qsub_2, dl, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::qsub_3, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, V0, SubReg0, V1, SubReg1,
                                    V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

/// GetVLDSTAlign - Get the alignment (in bytes) for the alignment operand
/// of a NEON VLD or VST instruction.  The supported values depend on the
/// number of registers being loaded.
SDValue ARMDAGToDAGISel::GetVLDSTAlign(SDValue Align, const SDLoc &dl,
                                       unsigned NumVecs, bool is64BitVector) {
  unsigned NumRegs = NumVecs;
  if (!is64BitVector && NumVecs < 3)
    NumRegs *= 2;

  unsigned Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
  if (Alignment >= 32 && NumRegs == 4)
    Alignment = 32;
  else if (Alignment >= 16 && (NumRegs == 2 || NumRegs == 4))
    Alignment = 16;
  else if (Alignment >= 8)
    Alignment = 8;
  else
    Alignment = 0;

  return CurDAG->getTargetConstant(Alignment, dl, MVT::i32);
}

static bool isVLDfixed(unsigned Opc)
{
  switch (Opc) {
  default: return false;
  case ARM::VLD1d8wb_fixed : return true;
  case ARM::VLD1d16wb_fixed : return true;
  case ARM::VLD1d64Qwb_fixed : return true;
  case ARM::VLD1d32wb_fixed : return true;
  case ARM::VLD1d64wb_fixed : return true;
  case ARM::VLD1d64TPseudoWB_fixed : return true;
  case ARM::VLD1d64QPseudoWB_fixed : return true;
  case ARM::VLD1q8wb_fixed : return true;
  case ARM::VLD1q16wb_fixed : return true;
  case ARM::VLD1q32wb_fixed : return true;
  case ARM::VLD1q64wb_fixed : return true;
  case ARM::VLD1DUPd8wb_fixed : return true;
  case ARM::VLD1DUPd16wb_fixed : return true;
  case ARM::VLD1DUPd32wb_fixed : return true;
  case ARM::VLD1DUPq8wb_fixed : return true;
  case ARM::VLD1DUPq16wb_fixed : return true;
  case ARM::VLD1DUPq32wb_fixed : return true;
  case ARM::VLD2d8wb_fixed : return true;
  case ARM::VLD2d16wb_fixed : return true;
  case ARM::VLD2d32wb_fixed : return true;
  case ARM::VLD2q8PseudoWB_fixed : return true;
  case ARM::VLD2q16PseudoWB_fixed : return true;
  case ARM::VLD2q32PseudoWB_fixed : return true;
  case ARM::VLD2DUPd8wb_fixed : return true;
  case ARM::VLD2DUPd16wb_fixed : return true;
  case ARM::VLD2DUPd32wb_fixed : return true;
  }
}

static bool isVSTfixed(unsigned Opc)
{
  switch (Opc) {
  default: return false;
  case ARM::VST1d8wb_fixed : return true;
  case ARM::VST1d16wb_fixed : return true;
  case ARM::VST1d32wb_fixed : return true;
  case ARM::VST1d64wb_fixed : return true;
  case ARM::VST1q8wb_fixed : return true;
  case ARM::VST1q16wb_fixed : return true;
  case ARM::VST1q32wb_fixed : return true;
  case ARM::VST1q64wb_fixed : return true;
  case ARM::VST1d64TPseudoWB_fixed : return true;
  case ARM::VST1d64QPseudoWB_fixed : return true;
  case ARM::VST2d8wb_fixed : return true;
  case ARM::VST2d16wb_fixed : return true;
  case ARM::VST2d32wb_fixed : return true;
  case ARM::VST2q8PseudoWB_fixed : return true;
  case ARM::VST2q16PseudoWB_fixed : return true;
  case ARM::VST2q32PseudoWB_fixed : return true;
  }
}

// Get the register stride update opcode of a VLD/VST instruction that
// is otherwise equivalent to the given fixed stride updating instruction.
static unsigned getVLDSTRegisterUpdateOpcode(unsigned Opc) {
  assert((isVLDfixed(Opc) || isVSTfixed(Opc))
    && "Incorrect fixed stride updating instruction.");
  switch (Opc) {
  default: break;
  case ARM::VLD1d8wb_fixed: return ARM::VLD1d8wb_register;
  case ARM::VLD1d16wb_fixed: return ARM::VLD1d16wb_register;
  case ARM::VLD1d32wb_fixed: return ARM::VLD1d32wb_register;
  case ARM::VLD1d64wb_fixed: return ARM::VLD1d64wb_register;
  case ARM::VLD1q8wb_fixed: return ARM::VLD1q8wb_register;
  case ARM::VLD1q16wb_fixed: return ARM::VLD1q16wb_register;
  case ARM::VLD1q32wb_fixed: return ARM::VLD1q32wb_register;
  case ARM::VLD1q64wb_fixed: return ARM::VLD1q64wb_register;
  case ARM::VLD1d64Twb_fixed: return ARM::VLD1d64Twb_register;
  case ARM::VLD1d64Qwb_fixed: return ARM::VLD1d64Qwb_register;
  case ARM::VLD1d64TPseudoWB_fixed: return ARM::VLD1d64TPseudoWB_register;
  case ARM::VLD1d64QPseudoWB_fixed: return ARM::VLD1d64QPseudoWB_register;
  case ARM::VLD1DUPd8wb_fixed : return ARM::VLD1DUPd8wb_register;
  case ARM::VLD1DUPd16wb_fixed : return ARM::VLD1DUPd16wb_register;
  case ARM::VLD1DUPd32wb_fixed : return ARM::VLD1DUPd32wb_register;
  case ARM::VLD1DUPq8wb_fixed : return ARM::VLD1DUPq8wb_register;
  case ARM::VLD1DUPq16wb_fixed : return ARM::VLD1DUPq16wb_register;
  case ARM::VLD1DUPq32wb_fixed : return ARM::VLD1DUPq32wb_register;

  case ARM::VST1d8wb_fixed: return ARM::VST1d8wb_register;
  case ARM::VST1d16wb_fixed: return ARM::VST1d16wb_register;
  case ARM::VST1d32wb_fixed: return ARM::VST1d32wb_register;
  case ARM::VST1d64wb_fixed: return ARM::VST1d64wb_register;
  case ARM::VST1q8wb_fixed: return ARM::VST1q8wb_register;
  case ARM::VST1q16wb_fixed: return ARM::VST1q16wb_register;
  case ARM::VST1q32wb_fixed: return ARM::VST1q32wb_register;
  case ARM::VST1q64wb_fixed: return ARM::VST1q64wb_register;
  case ARM::VST1d64TPseudoWB_fixed: return ARM::VST1d64TPseudoWB_register;
  case ARM::VST1d64QPseudoWB_fixed: return ARM::VST1d64QPseudoWB_register;

  case ARM::VLD2d8wb_fixed: return ARM::VLD2d8wb_register;
  case ARM::VLD2d16wb_fixed: return ARM::VLD2d16wb_register;
  case ARM::VLD2d32wb_fixed: return ARM::VLD2d32wb_register;
  case ARM::VLD2q8PseudoWB_fixed: return ARM::VLD2q8PseudoWB_register;
  case ARM::VLD2q16PseudoWB_fixed: return ARM::VLD2q16PseudoWB_register;
  case ARM::VLD2q32PseudoWB_fixed: return ARM::VLD2q32PseudoWB_register;

  case ARM::VST2d8wb_fixed: return ARM::VST2d8wb_register;
  case ARM::VST2d16wb_fixed: return ARM::VST2d16wb_register;
  case ARM::VST2d32wb_fixed: return ARM::VST2d32wb_register;
  case ARM::VST2q8PseudoWB_fixed: return ARM::VST2q8PseudoWB_register;
  case ARM::VST2q16PseudoWB_fixed: return ARM::VST2q16PseudoWB_register;
  case ARM::VST2q32PseudoWB_fixed: return ARM::VST2q32PseudoWB_register;

  case ARM::VLD2DUPd8wb_fixed: return ARM::VLD2DUPd8wb_register;
  case ARM::VLD2DUPd16wb_fixed: return ARM::VLD2DUPd16wb_register;
  case ARM::VLD2DUPd32wb_fixed: return ARM::VLD2DUPd32wb_register;
  }
  return Opc; // If not one we handle, return it unchanged.
}

/// Returns true if the given increment is a Constant known to be equal to the
/// access size performed by a NEON load/store. This means the "[rN]!" form can
/// be used.
static bool isPerfectIncrement(SDValue Inc, EVT VecTy, unsigned NumVecs) {
  auto C = dyn_cast<ConstantSDNode>(Inc);
  return C && C->getZExtValue() == VecTy.getSizeInBits() / 8 * NumVecs;
}

void ARMDAGToDAGISel::SelectVLD(SDNode *N, bool isUpdating, unsigned NumVecs,
                                const uint16_t *DOpcodes,
                                const uint16_t *QOpcodes0,
                                const uint16_t *QOpcodes1) {
  assert(NumVecs >= 1 && NumVecs <= 4 && "VLD NumVecs out-of-range");
  SDLoc dl(N);

  SDValue MemAddr, Align;
  bool IsIntrinsic = !isUpdating;  // By coincidence, all supported updating
                                   // nodes are not intrinsics.
  unsigned AddrOpIdx = IsIntrinsic ? 2 : 1;
  if (!SelectAddrMode6(N, N->getOperand(AddrOpIdx), MemAddr, Align))
    return;

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getValueType(0);
  bool is64BitVector = VT.is64BitVector();
  Align = GetVLDSTAlign(Align, dl, NumVecs, is64BitVector);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4f16:
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
  case MVT::v1i64: OpcodeIndex = 3; break;
    // Quad-register operations:
  case MVT::v16i8: OpcodeIndex = 0; break;
  case MVT::v8f16:
  case MVT::v8i16: OpcodeIndex = 1; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 2; break;
  case MVT::v2f64:
  case MVT::v2i64: OpcodeIndex = 3; break;
  }

  EVT ResTy;
  if (NumVecs == 1)
    ResTy = VT;
  else {
    unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
    if (!is64BitVector)
      ResTyElts *= 2;
    ResTy = EVT::getVectorVT(*CurDAG->getContext(), MVT::i64, ResTyElts);
  }
  std::vector<EVT> ResTys;
  ResTys.push_back(ResTy);
  if (isUpdating)
    ResTys.push_back(MVT::i32);
  ResTys.push_back(MVT::Other);

  SDValue Pred = getAL(CurDAG, dl);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
  SDNode *VLd;
  SmallVector<SDValue, 7> Ops;

  // Double registers and VLD1/VLD2 quad registers are directly supported.
  if (is64BitVector || NumVecs <= 2) {
    unsigned Opc = (is64BitVector ? DOpcodes[OpcodeIndex] :
                    QOpcodes0[OpcodeIndex]);
    Ops.push_back(MemAddr);
    Ops.push_back(Align);
    if (isUpdating) {
      SDValue Inc = N->getOperand(AddrOpIdx + 1);
      bool IsImmUpdate = isPerfectIncrement(Inc, VT, NumVecs);
      if (!IsImmUpdate) {
        // We use a VLD1 for v1i64 even if the pseudo says vld2/3/4, so
        // check for the opcode rather than the number of vector elements.
        if (isVLDfixed(Opc))
          Opc = getVLDSTRegisterUpdateOpcode(Opc);
        Ops.push_back(Inc);
      // VLD1/VLD2 fixed increment does not need Reg0 so only include it in
      // the operands if not such an opcode.
      } else if (!isVLDfixed(Opc))
        Ops.push_back(Reg0);
    }
    Ops.push_back(Pred);
    Ops.push_back(Reg0);
    Ops.push_back(Chain);
    VLd = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

  } else {
    // Otherwise, quad registers are loaded with two separate instructions,
    // where one loads the even registers and the other loads the odd registers.
    EVT AddrTy = MemAddr.getValueType();

    // Load the even subregs.  This is always an updating load, so that it
    // provides the address to the second load for the odd subregs.
    SDValue ImplDef =
      SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, ResTy), 0);
    const SDValue OpsA[] = { MemAddr, Align, Reg0, ImplDef, Pred, Reg0, Chain };
    SDNode *VLdA = CurDAG->getMachineNode(QOpcodes0[OpcodeIndex], dl,
                                          ResTy, AddrTy, MVT::Other, OpsA);
    Chain = SDValue(VLdA, 2);

    // Load the odd subregs.
    Ops.push_back(SDValue(VLdA, 1));
    Ops.push_back(Align);
    if (isUpdating) {
      SDValue Inc = N->getOperand(AddrOpIdx + 1);
      assert(isa<ConstantSDNode>(Inc.getNode()) &&
             "only constant post-increment update allowed for VLD3/4");
      (void)Inc;
      Ops.push_back(Reg0);
    }
    Ops.push_back(SDValue(VLdA, 0));
    Ops.push_back(Pred);
    Ops.push_back(Reg0);
    Ops.push_back(Chain);
    VLd = CurDAG->getMachineNode(QOpcodes1[OpcodeIndex], dl, ResTys, Ops);
  }

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(VLd), {MemOp});

  if (NumVecs == 1) {
    ReplaceNode(N, VLd);
    return;
  }

  // Extract out the subregisters.
  SDValue SuperReg = SDValue(VLd, 0);
  static_assert(ARM::dsub_7 == ARM::dsub_0 + 7 &&
                    ARM::qsub_3 == ARM::qsub_0 + 3,
                "Unexpected subreg numbering");
  unsigned Sub0 = (is64BitVector ? ARM::dsub_0 : ARM::qsub_0);
  for (unsigned Vec = 0; Vec < NumVecs; ++Vec)
    ReplaceUses(SDValue(N, Vec),
                CurDAG->getTargetExtractSubreg(Sub0 + Vec, dl, VT, SuperReg));
  ReplaceUses(SDValue(N, NumVecs), SDValue(VLd, 1));
  if (isUpdating)
    ReplaceUses(SDValue(N, NumVecs + 1), SDValue(VLd, 2));
  CurDAG->RemoveDeadNode(N);
}

void ARMDAGToDAGISel::SelectVST(SDNode *N, bool isUpdating, unsigned NumVecs,
                                const uint16_t *DOpcodes,
                                const uint16_t *QOpcodes0,
                                const uint16_t *QOpcodes1) {
  assert(NumVecs >= 1 && NumVecs <= 4 && "VST NumVecs out-of-range");
  SDLoc dl(N);

  SDValue MemAddr, Align;
  bool IsIntrinsic = !isUpdating;  // By coincidence, all supported updating
                                   // nodes are not intrinsics.
  unsigned AddrOpIdx = IsIntrinsic ? 2 : 1;
  unsigned Vec0Idx = 3; // AddrOpIdx + (isUpdating ? 2 : 1)
  if (!SelectAddrMode6(N, N->getOperand(AddrOpIdx), MemAddr, Align))
    return;

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getOperand(Vec0Idx).getValueType();
  bool is64BitVector = VT.is64BitVector();
  Align = GetVLDSTAlign(Align, dl, NumVecs, is64BitVector);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vst type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4f16:
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
  case MVT::v1i64: OpcodeIndex = 3; break;
    // Quad-register operations:
  case MVT::v16i8: OpcodeIndex = 0; break;
  case MVT::v8f16:
  case MVT::v8i16: OpcodeIndex = 1; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 2; break;
  case MVT::v2f64:
  case MVT::v2i64: OpcodeIndex = 3; break;
  }

  std::vector<EVT> ResTys;
  if (isUpdating)
    ResTys.push_back(MVT::i32);
  ResTys.push_back(MVT::Other);

  SDValue Pred = getAL(CurDAG, dl);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
  SmallVector<SDValue, 7> Ops;

  // Double registers and VST1/VST2 quad registers are directly supported.
  if (is64BitVector || NumVecs <= 2) {
    SDValue SrcReg;
    if (NumVecs == 1) {
      SrcReg = N->getOperand(Vec0Idx);
    } else if (is64BitVector) {
      // Form a REG_SEQUENCE to force register allocation.
      SDValue V0 = N->getOperand(Vec0Idx + 0);
      SDValue V1 = N->getOperand(Vec0Idx + 1);
      if (NumVecs == 2)
        SrcReg = SDValue(createDRegPairNode(MVT::v2i64, V0, V1), 0);
      else {
        SDValue V2 = N->getOperand(Vec0Idx + 2);
        // If it's a vst3, form a quad D-register and leave the last part as
        // an undef.
        SDValue V3 = (NumVecs == 3)
          ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF,dl,VT), 0)
          : N->getOperand(Vec0Idx + 3);
        SrcReg = SDValue(createQuadDRegsNode(MVT::v4i64, V0, V1, V2, V3), 0);
      }
    } else {
      // Form a QQ register.
      SDValue Q0 = N->getOperand(Vec0Idx);
      SDValue Q1 = N->getOperand(Vec0Idx + 1);
      SrcReg = SDValue(createQRegPairNode(MVT::v4i64, Q0, Q1), 0);
    }

    unsigned Opc = (is64BitVector ? DOpcodes[OpcodeIndex] :
                    QOpcodes0[OpcodeIndex]);
    Ops.push_back(MemAddr);
    Ops.push_back(Align);
    if (isUpdating) {
      SDValue Inc = N->getOperand(AddrOpIdx + 1);
      bool IsImmUpdate = isPerfectIncrement(Inc, VT, NumVecs);
      if (!IsImmUpdate) {
        // We use a VST1 for v1i64 even if the pseudo says VST2/3/4, so
        // check for the opcode rather than the number of vector elements.
        if (isVSTfixed(Opc))
          Opc = getVLDSTRegisterUpdateOpcode(Opc);
        Ops.push_back(Inc);
      }
      // VST1/VST2 fixed increment does not need Reg0 so only include it in
      // the operands if not such an opcode.
      else if (!isVSTfixed(Opc))
        Ops.push_back(Reg0);
    }
    Ops.push_back(SrcReg);
    Ops.push_back(Pred);
    Ops.push_back(Reg0);
    Ops.push_back(Chain);
    SDNode *VSt = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

    // Transfer memoperands.
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(VSt), {MemOp});

    ReplaceNode(N, VSt);
    return;
  }

  // Otherwise, quad registers are stored with two separate instructions,
  // where one stores the even registers and the other stores the odd registers.

  // Form the QQQQ REG_SEQUENCE.
  SDValue V0 = N->getOperand(Vec0Idx + 0);
  SDValue V1 = N->getOperand(Vec0Idx + 1);
  SDValue V2 = N->getOperand(Vec0Idx + 2);
  SDValue V3 = (NumVecs == 3)
    ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, VT), 0)
    : N->getOperand(Vec0Idx + 3);
  SDValue RegSeq = SDValue(createQuadQRegsNode(MVT::v8i64, V0, V1, V2, V3), 0);

  // Store the even D registers.  This is always an updating store, so that it
  // provides the address to the second store for the odd subregs.
  const SDValue OpsA[] = { MemAddr, Align, Reg0, RegSeq, Pred, Reg0, Chain };
  SDNode *VStA = CurDAG->getMachineNode(QOpcodes0[OpcodeIndex], dl,
                                        MemAddr.getValueType(),
                                        MVT::Other, OpsA);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(VStA), {MemOp});
  Chain = SDValue(VStA, 1);

  // Store the odd D registers.
  Ops.push_back(SDValue(VStA, 0));
  Ops.push_back(Align);
  if (isUpdating) {
    SDValue Inc = N->getOperand(AddrOpIdx + 1);
    assert(isa<ConstantSDNode>(Inc.getNode()) &&
           "only constant post-increment update allowed for VST3/4");
    (void)Inc;
    Ops.push_back(Reg0);
  }
  Ops.push_back(RegSeq);
  Ops.push_back(Pred);
  Ops.push_back(Reg0);
  Ops.push_back(Chain);
  SDNode *VStB = CurDAG->getMachineNode(QOpcodes1[OpcodeIndex], dl, ResTys,
                                        Ops);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(VStB), {MemOp});
  ReplaceNode(N, VStB);
}

void ARMDAGToDAGISel::SelectVLDSTLane(SDNode *N, bool IsLoad, bool isUpdating,
                                      unsigned NumVecs,
                                      const uint16_t *DOpcodes,
                                      const uint16_t *QOpcodes) {
  assert(NumVecs >=2 && NumVecs <= 4 && "VLDSTLane NumVecs out-of-range");
  SDLoc dl(N);

  SDValue MemAddr, Align;
  bool IsIntrinsic = !isUpdating;  // By coincidence, all supported updating
                                   // nodes are not intrinsics.
  unsigned AddrOpIdx = IsIntrinsic ? 2 : 1;
  unsigned Vec0Idx = 3; // AddrOpIdx + (isUpdating ? 2 : 1)
  if (!SelectAddrMode6(N, N->getOperand(AddrOpIdx), MemAddr, Align))
    return;

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();

  SDValue Chain = N->getOperand(0);
  unsigned Lane =
    cast<ConstantSDNode>(N->getOperand(Vec0Idx + NumVecs))->getZExtValue();
  EVT VT = N->getOperand(Vec0Idx).getValueType();
  bool is64BitVector = VT.is64BitVector();

  unsigned Alignment = 0;
  if (NumVecs != 3) {
    Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
    unsigned NumBytes = NumVecs * VT.getScalarSizeInBits() / 8;
    if (Alignment > NumBytes)
      Alignment = NumBytes;
    if (Alignment < 8 && Alignment < NumBytes)
      Alignment = 0;
    // Alignment must be a power of two; make sure of that.
    Alignment = (Alignment & -Alignment);
    if (Alignment == 1)
      Alignment = 0;
  }
  Align = CurDAG->getTargetConstant(Alignment, dl, MVT::i32);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld/vst lane type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
    // Quad-register operations:
  case MVT::v8i16: OpcodeIndex = 0; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 1; break;
  }

  std::vector<EVT> ResTys;
  if (IsLoad) {
    unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
    if (!is64BitVector)
      ResTyElts *= 2;
    ResTys.push_back(EVT::getVectorVT(*CurDAG->getContext(),
                                      MVT::i64, ResTyElts));
  }
  if (isUpdating)
    ResTys.push_back(MVT::i32);
  ResTys.push_back(MVT::Other);

  SDValue Pred = getAL(CurDAG, dl);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(MemAddr);
  Ops.push_back(Align);
  if (isUpdating) {
    SDValue Inc = N->getOperand(AddrOpIdx + 1);
    bool IsImmUpdate =
        isPerfectIncrement(Inc, VT.getVectorElementType(), NumVecs);
    Ops.push_back(IsImmUpdate ? Reg0 : Inc);
  }

  SDValue SuperReg;
  SDValue V0 = N->getOperand(Vec0Idx + 0);
  SDValue V1 = N->getOperand(Vec0Idx + 1);
  if (NumVecs == 2) {
    if (is64BitVector)
      SuperReg = SDValue(createDRegPairNode(MVT::v2i64, V0, V1), 0);
    else
      SuperReg = SDValue(createQRegPairNode(MVT::v4i64, V0, V1), 0);
  } else {
    SDValue V2 = N->getOperand(Vec0Idx + 2);
    SDValue V3 = (NumVecs == 3)
      ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, VT), 0)
      : N->getOperand(Vec0Idx + 3);
    if (is64BitVector)
      SuperReg = SDValue(createQuadDRegsNode(MVT::v4i64, V0, V1, V2, V3), 0);
    else
      SuperReg = SDValue(createQuadQRegsNode(MVT::v8i64, V0, V1, V2, V3), 0);
  }
  Ops.push_back(SuperReg);
  Ops.push_back(getI32Imm(Lane, dl));
  Ops.push_back(Pred);
  Ops.push_back(Reg0);
  Ops.push_back(Chain);

  unsigned Opc = (is64BitVector ? DOpcodes[OpcodeIndex] :
                                  QOpcodes[OpcodeIndex]);
  SDNode *VLdLn = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(VLdLn), {MemOp});
  if (!IsLoad) {
    ReplaceNode(N, VLdLn);
    return;
  }

  // Extract the subregisters.
  SuperReg = SDValue(VLdLn, 0);
  static_assert(ARM::dsub_7 == ARM::dsub_0 + 7 &&
                    ARM::qsub_3 == ARM::qsub_0 + 3,
                "Unexpected subreg numbering");
  unsigned Sub0 = is64BitVector ? ARM::dsub_0 : ARM::qsub_0;
  for (unsigned Vec = 0; Vec < NumVecs; ++Vec)
    ReplaceUses(SDValue(N, Vec),
                CurDAG->getTargetExtractSubreg(Sub0 + Vec, dl, VT, SuperReg));
  ReplaceUses(SDValue(N, NumVecs), SDValue(VLdLn, 1));
  if (isUpdating)
    ReplaceUses(SDValue(N, NumVecs + 1), SDValue(VLdLn, 2));
  CurDAG->RemoveDeadNode(N);
}

void ARMDAGToDAGISel::SelectVLDDup(SDNode *N, bool IsIntrinsic,
                                   bool isUpdating, unsigned NumVecs,
                                   const uint16_t *DOpcodes,
                                   const uint16_t *QOpcodes0,
                                   const uint16_t *QOpcodes1) {
  assert(NumVecs >= 1 && NumVecs <= 4 && "VLDDup NumVecs out-of-range");
  SDLoc dl(N);

  SDValue MemAddr, Align;
  unsigned AddrOpIdx = IsIntrinsic ? 2 : 1;
  if (!SelectAddrMode6(N, N->getOperand(AddrOpIdx), MemAddr, Align))
    return;

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getValueType(0);
  bool is64BitVector = VT.is64BitVector();

  unsigned Alignment = 0;
  if (NumVecs != 3) {
    Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
    unsigned NumBytes = NumVecs * VT.getScalarSizeInBits() / 8;
    if (Alignment > NumBytes)
      Alignment = NumBytes;
    if (Alignment < 8 && Alignment < NumBytes)
      Alignment = 0;
    // Alignment must be a power of two; make sure of that.
    Alignment = (Alignment & -Alignment);
    if (Alignment == 1)
      Alignment = 0;
  }
  Align = CurDAG->getTargetConstant(Alignment, dl, MVT::i32);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld-dup type");
  case MVT::v8i8:
  case MVT::v16i8: OpcodeIndex = 0; break;
  case MVT::v4i16:
  case MVT::v8i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32:
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 2; break;
  case MVT::v1f64:
  case MVT::v1i64: OpcodeIndex = 3; break;
  }

  unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
  if (!is64BitVector)
    ResTyElts *= 2;
  EVT ResTy = EVT::getVectorVT(*CurDAG->getContext(), MVT::i64, ResTyElts);

  std::vector<EVT> ResTys;
  ResTys.push_back(ResTy);
  if (isUpdating)
    ResTys.push_back(MVT::i32);
  ResTys.push_back(MVT::Other);

  SDValue Pred = getAL(CurDAG, dl);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);

  SDNode *VLdDup;
  if (is64BitVector || NumVecs == 1) {
    SmallVector<SDValue, 6> Ops;
    Ops.push_back(MemAddr);
    Ops.push_back(Align);
    unsigned Opc = is64BitVector ? DOpcodes[OpcodeIndex] :
                                   QOpcodes0[OpcodeIndex];
    if (isUpdating) {
      // fixed-stride update instructions don't have an explicit writeback
      // operand. It's implicit in the opcode itself.
      SDValue Inc = N->getOperand(2);
      bool IsImmUpdate =
          isPerfectIncrement(Inc, VT.getVectorElementType(), NumVecs);
      if (NumVecs <= 2 && !IsImmUpdate)
        Opc = getVLDSTRegisterUpdateOpcode(Opc);
      if (!IsImmUpdate)
        Ops.push_back(Inc);
      // FIXME: VLD3 and VLD4 haven't been updated to that form yet.
      else if (NumVecs > 2)
        Ops.push_back(Reg0);
    }
    Ops.push_back(Pred);
    Ops.push_back(Reg0);
    Ops.push_back(Chain);
    VLdDup = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);
  } else if (NumVecs == 2) {
    const SDValue OpsA[] = { MemAddr, Align, Pred, Reg0, Chain };
    SDNode *VLdA = CurDAG->getMachineNode(QOpcodes0[OpcodeIndex],
                                          dl, ResTys, OpsA);

    Chain = SDValue(VLdA, 1);
    const SDValue OpsB[] = { MemAddr, Align, Pred, Reg0, Chain };
    VLdDup = CurDAG->getMachineNode(QOpcodes1[OpcodeIndex], dl, ResTys, OpsB);
  } else {
    SDValue ImplDef =
      SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, ResTy), 0);
    const SDValue OpsA[] = { MemAddr, Align, ImplDef, Pred, Reg0, Chain };
    SDNode *VLdA = CurDAG->getMachineNode(QOpcodes0[OpcodeIndex],
                                          dl, ResTys, OpsA);

    SDValue SuperReg = SDValue(VLdA, 0);
    Chain = SDValue(VLdA, 1);
    const SDValue OpsB[] = { MemAddr, Align, SuperReg, Pred, Reg0, Chain };
    VLdDup = CurDAG->getMachineNode(QOpcodes1[OpcodeIndex], dl, ResTys, OpsB);
  }

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(VLdDup), {MemOp});

  // Extract the subregisters.
  if (NumVecs == 1) {
    ReplaceUses(SDValue(N, 0), SDValue(VLdDup, 0));
  } else {
    SDValue SuperReg = SDValue(VLdDup, 0);
    static_assert(ARM::dsub_7 == ARM::dsub_0 + 7, "Unexpected subreg numbering");
    unsigned SubIdx = is64BitVector ? ARM::dsub_0 : ARM::qsub_0;
    for (unsigned Vec = 0; Vec != NumVecs; ++Vec) {
      ReplaceUses(SDValue(N, Vec),
                  CurDAG->getTargetExtractSubreg(SubIdx+Vec, dl, VT, SuperReg));
    }
  }
  ReplaceUses(SDValue(N, NumVecs), SDValue(VLdDup, 1));
  if (isUpdating)
    ReplaceUses(SDValue(N, NumVecs + 1), SDValue(VLdDup, 2));
  CurDAG->RemoveDeadNode(N);
}

bool ARMDAGToDAGISel::tryV6T2BitfieldExtractOp(SDNode *N, bool isSigned) {
  if (!Subtarget->hasV6T2Ops())
    return false;

  unsigned Opc = isSigned
    ? (Subtarget->isThumb() ? ARM::t2SBFX : ARM::SBFX)
    : (Subtarget->isThumb() ? ARM::t2UBFX : ARM::UBFX);
  SDLoc dl(N);

  // For unsigned extracts, check for a shift right and mask
  unsigned And_imm = 0;
  if (N->getOpcode() == ISD::AND) {
    if (isOpcWithIntImmediate(N, ISD::AND, And_imm)) {

      // The immediate is a mask of the low bits iff imm & (imm+1) == 0
      if (And_imm & (And_imm + 1))
        return false;

      unsigned Srl_imm = 0;
      if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SRL,
                                Srl_imm)) {
        assert(Srl_imm > 0 && Srl_imm < 32 && "bad amount in shift node!");

        // Mask off the unnecessary bits of the AND immediate; normally
        // DAGCombine will do this, but that might not happen if
        // targetShrinkDemandedConstant chooses a different immediate.
        And_imm &= -1U >> Srl_imm;

        // Note: The width operand is encoded as width-1.
        unsigned Width = countTrailingOnes(And_imm) - 1;
        unsigned LSB = Srl_imm;

        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);

        if ((LSB + Width + 1) == N->getValueType(0).getSizeInBits()) {
          // It's cheaper to use a right shift to extract the top bits.
          if (Subtarget->isThumb()) {
            Opc = isSigned ? ARM::t2ASRri : ARM::t2LSRri;
            SDValue Ops[] = { N->getOperand(0).getOperand(0),
                              CurDAG->getTargetConstant(LSB, dl, MVT::i32),
                              getAL(CurDAG, dl), Reg0, Reg0 };
            CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
            return true;
          }

          // ARM models shift instructions as MOVsi with shifter operand.
          ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(ISD::SRL);
          SDValue ShOpc =
            CurDAG->getTargetConstant(ARM_AM::getSORegOpc(ShOpcVal, LSB), dl,
                                      MVT::i32);
          SDValue Ops[] = { N->getOperand(0).getOperand(0), ShOpc,
                            getAL(CurDAG, dl), Reg0, Reg0 };
          CurDAG->SelectNodeTo(N, ARM::MOVsi, MVT::i32, Ops);
          return true;
        }

        assert(LSB + Width + 1 <= 32 && "Shouldn't create an invalid ubfx");
        SDValue Ops[] = { N->getOperand(0).getOperand(0),
                          CurDAG->getTargetConstant(LSB, dl, MVT::i32),
                          CurDAG->getTargetConstant(Width, dl, MVT::i32),
                          getAL(CurDAG, dl), Reg0 };
        CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
        return true;
      }
    }
    return false;
  }

  // Otherwise, we're looking for a shift of a shift
  unsigned Shl_imm = 0;
  if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SHL, Shl_imm)) {
    assert(Shl_imm > 0 && Shl_imm < 32 && "bad amount in shift node!");
    unsigned Srl_imm = 0;
    if (isInt32Immediate(N->getOperand(1), Srl_imm)) {
      assert(Srl_imm > 0 && Srl_imm < 32 && "bad amount in shift node!");
      // Note: The width operand is encoded as width-1.
      unsigned Width = 32 - Srl_imm - 1;
      int LSB = Srl_imm - Shl_imm;
      if (LSB < 0)
        return false;
      SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
      assert(LSB + Width + 1 <= 32 && "Shouldn't create an invalid ubfx");
      SDValue Ops[] = { N->getOperand(0).getOperand(0),
                        CurDAG->getTargetConstant(LSB, dl, MVT::i32),
                        CurDAG->getTargetConstant(Width, dl, MVT::i32),
                        getAL(CurDAG, dl), Reg0 };
      CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
      return true;
    }
  }

  // Or we are looking for a shift of an and, with a mask operand
  if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::AND, And_imm) &&
      isShiftedMask_32(And_imm)) {
    unsigned Srl_imm = 0;
    unsigned LSB = countTrailingZeros(And_imm);
    // Shift must be the same as the ands lsb
    if (isInt32Immediate(N->getOperand(1), Srl_imm) && Srl_imm == LSB) {
      assert(Srl_imm > 0 && Srl_imm < 32 && "bad amount in shift node!");
      unsigned MSB = 31 - countLeadingZeros(And_imm);
      // Note: The width operand is encoded as width-1.
      unsigned Width = MSB - LSB;
      SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
      assert(Srl_imm + Width + 1 <= 32 && "Shouldn't create an invalid ubfx");
      SDValue Ops[] = { N->getOperand(0).getOperand(0),
                        CurDAG->getTargetConstant(Srl_imm, dl, MVT::i32),
                        CurDAG->getTargetConstant(Width, dl, MVT::i32),
                        getAL(CurDAG, dl), Reg0 };
      CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
      return true;
    }
  }

  if (N->getOpcode() == ISD::SIGN_EXTEND_INREG) {
    unsigned Width = cast<VTSDNode>(N->getOperand(1))->getVT().getSizeInBits();
    unsigned LSB = 0;
    if (!isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SRL, LSB) &&
        !isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SRA, LSB))
      return false;

    if (LSB + Width > 32)
      return false;

    SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
    assert(LSB + Width <= 32 && "Shouldn't create an invalid ubfx");
    SDValue Ops[] = { N->getOperand(0).getOperand(0),
                      CurDAG->getTargetConstant(LSB, dl, MVT::i32),
                      CurDAG->getTargetConstant(Width - 1, dl, MVT::i32),
                      getAL(CurDAG, dl), Reg0 };
    CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
    return true;
  }

  return false;
}

/// Target-specific DAG combining for ISD::XOR.
/// Target-independent combining lowers SELECT_CC nodes of the form
/// select_cc setg[ge] X,  0,  X, -X
/// select_cc setgt    X, -1,  X, -X
/// select_cc setl[te] X,  0, -X,  X
/// select_cc setlt    X,  1, -X,  X
/// which represent Integer ABS into:
/// Y = sra (X, size(X)-1); xor (add (X, Y), Y)
/// ARM instruction selection detects the latter and matches it to
/// ARM::ABS or ARM::t2ABS machine node.
bool ARMDAGToDAGISel::tryABSOp(SDNode *N){
  SDValue XORSrc0 = N->getOperand(0);
  SDValue XORSrc1 = N->getOperand(1);
  EVT VT = N->getValueType(0);

  if (Subtarget->isThumb1Only())
    return false;

  if (XORSrc0.getOpcode() != ISD::ADD || XORSrc1.getOpcode() != ISD::SRA)
    return false;

  SDValue ADDSrc0 = XORSrc0.getOperand(0);
  SDValue ADDSrc1 = XORSrc0.getOperand(1);
  SDValue SRASrc0 = XORSrc1.getOperand(0);
  SDValue SRASrc1 = XORSrc1.getOperand(1);
  ConstantSDNode *SRAConstant =  dyn_cast<ConstantSDNode>(SRASrc1);
  EVT XType = SRASrc0.getValueType();
  unsigned Size = XType.getSizeInBits() - 1;

  if (ADDSrc1 == XORSrc1 && ADDSrc0 == SRASrc0 &&
      XType.isInteger() && SRAConstant != nullptr &&
      Size == SRAConstant->getZExtValue()) {
    unsigned Opcode = Subtarget->isThumb2() ? ARM::t2ABS : ARM::ABS;
    CurDAG->SelectNodeTo(N, Opcode, VT, ADDSrc0);
    return true;
  }

  return false;
}

/// We've got special pseudo-instructions for these
void ARMDAGToDAGISel::SelectCMP_SWAP(SDNode *N) {
  unsigned Opcode;
  EVT MemTy = cast<MemSDNode>(N)->getMemoryVT();
  if (MemTy == MVT::i8)
    Opcode = ARM::CMP_SWAP_8;
  else if (MemTy == MVT::i16)
    Opcode = ARM::CMP_SWAP_16;
  else if (MemTy == MVT::i32)
    Opcode = ARM::CMP_SWAP_32;
  else
    llvm_unreachable("Unknown AtomicCmpSwap type");

  SDValue Ops[] = {N->getOperand(1), N->getOperand(2), N->getOperand(3),
                   N->getOperand(0)};
  SDNode *CmpSwap = CurDAG->getMachineNode(
      Opcode, SDLoc(N),
      CurDAG->getVTList(MVT::i32, MVT::i32, MVT::Other), Ops);

  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(CmpSwap), {MemOp});

  ReplaceUses(SDValue(N, 0), SDValue(CmpSwap, 0));
  ReplaceUses(SDValue(N, 1), SDValue(CmpSwap, 2));
  CurDAG->RemoveDeadNode(N);
}

static Optional<std::pair<unsigned, unsigned>>
getContiguousRangeOfSetBits(const APInt &A) {
  unsigned FirstOne = A.getBitWidth() - A.countLeadingZeros() - 1;
  unsigned LastOne = A.countTrailingZeros();
  if (A.countPopulation() != (FirstOne - LastOne + 1))
    return Optional<std::pair<unsigned,unsigned>>();
  return std::make_pair(FirstOne, LastOne);
}

void ARMDAGToDAGISel::SelectCMPZ(SDNode *N, bool &SwitchEQNEToPLMI) {
  assert(N->getOpcode() == ARMISD::CMPZ);
  SwitchEQNEToPLMI = false;

  if (!Subtarget->isThumb())
    // FIXME: Work out whether it is profitable to do this in A32 mode - LSL and
    // LSR don't exist as standalone instructions - they need the barrel shifter.
    return;

  // select (cmpz (and X, C), #0) -> (LSLS X) or (LSRS X) or (LSRS (LSLS X))
  SDValue And = N->getOperand(0);
  if (!And->hasOneUse())
    return;

  SDValue Zero = N->getOperand(1);
  if (!isa<ConstantSDNode>(Zero) || !cast<ConstantSDNode>(Zero)->isNullValue() ||
      And->getOpcode() != ISD::AND)
    return;
  SDValue X = And.getOperand(0);
  auto C = dyn_cast<ConstantSDNode>(And.getOperand(1));

  if (!C)
    return;
  auto Range = getContiguousRangeOfSetBits(C->getAPIntValue());
  if (!Range)
    return;

  // There are several ways to lower this:
  SDNode *NewN;
  SDLoc dl(N);

  auto EmitShift = [&](unsigned Opc, SDValue Src, unsigned Imm) -> SDNode* {
    if (Subtarget->isThumb2()) {
      Opc = (Opc == ARM::tLSLri) ? ARM::t2LSLri : ARM::t2LSRri;
      SDValue Ops[] = { Src, CurDAG->getTargetConstant(Imm, dl, MVT::i32),
                        getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->getMachineNode(Opc, dl, MVT::i32, Ops);
    } else {
      SDValue Ops[] = {CurDAG->getRegister(ARM::CPSR, MVT::i32), Src,
                       CurDAG->getTargetConstant(Imm, dl, MVT::i32),
                       getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32)};
      return CurDAG->getMachineNode(Opc, dl, MVT::i32, Ops);
    }
  };

  if (Range->second == 0) {
    //  1. Mask includes the LSB -> Simply shift the top N bits off
    NewN = EmitShift(ARM::tLSLri, X, 31 - Range->first);
    ReplaceNode(And.getNode(), NewN);
  } else if (Range->first == 31) {
    //  2. Mask includes the MSB -> Simply shift the bottom N bits off
    NewN = EmitShift(ARM::tLSRri, X, Range->second);
    ReplaceNode(And.getNode(), NewN);
  } else if (Range->first == Range->second) {
    //  3. Only one bit is set. We can shift this into the sign bit and use a
    //     PL/MI comparison.
    NewN = EmitShift(ARM::tLSLri, X, 31 - Range->first);
    ReplaceNode(And.getNode(), NewN);

    SwitchEQNEToPLMI = true;
  } else if (!Subtarget->hasV6T2Ops()) {
    //  4. Do a double shift to clear bottom and top bits, but only in
    //     thumb-1 mode as in thumb-2 we can use UBFX.
    NewN = EmitShift(ARM::tLSLri, X, 31 - Range->first);
    NewN = EmitShift(ARM::tLSRri, SDValue(NewN, 0),
                     Range->second + (31 - Range->first));
    ReplaceNode(And.getNode(), NewN);
  }

}

void ARMDAGToDAGISel::Select(SDNode *N) {
  SDLoc dl(N);

  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;   // Already selected.
  }

  switch (N->getOpcode()) {
  default: break;
  case ISD::WRITE_REGISTER:
    if (tryWriteRegister(N))
      return;
    break;
  case ISD::READ_REGISTER:
    if (tryReadRegister(N))
      return;
    break;
  case ISD::INLINEASM:
    if (tryInlineAsm(N))
      return;
    break;
  case ISD::XOR:
    // Select special operations if XOR node forms integer ABS pattern
    if (tryABSOp(N))
      return;
    // Other cases are autogenerated.
    break;
  case ISD::Constant: {
    unsigned Val = cast<ConstantSDNode>(N)->getZExtValue();
    // If we can't materialize the constant we need to use a literal pool
    if (ConstantMaterializationCost(Val) > 2) {
      SDValue CPIdx = CurDAG->getTargetConstantPool(
          ConstantInt::get(Type::getInt32Ty(*CurDAG->getContext()), Val),
          TLI->getPointerTy(CurDAG->getDataLayout()));

      SDNode *ResNode;
      if (Subtarget->isThumb()) {
        SDValue Ops[] = {
          CPIdx,
          getAL(CurDAG, dl),
          CurDAG->getRegister(0, MVT::i32),
          CurDAG->getEntryNode()
        };
        ResNode = CurDAG->getMachineNode(ARM::tLDRpci, dl, MVT::i32, MVT::Other,
                                         Ops);
      } else {
        SDValue Ops[] = {
          CPIdx,
          CurDAG->getTargetConstant(0, dl, MVT::i32),
          getAL(CurDAG, dl),
          CurDAG->getRegister(0, MVT::i32),
          CurDAG->getEntryNode()
        };
        ResNode = CurDAG->getMachineNode(ARM::LDRcp, dl, MVT::i32, MVT::Other,
                                         Ops);
      }
      // Annotate the Node with memory operand information so that MachineInstr
      // queries work properly. This e.g. gives the register allocation the
      // required information for rematerialization.
      MachineFunction& MF = CurDAG->getMachineFunction();
      MachineMemOperand *MemOp =
          MF.getMachineMemOperand(MachinePointerInfo::getConstantPool(MF),
                                  MachineMemOperand::MOLoad, 4, 4);

      CurDAG->setNodeMemRefs(cast<MachineSDNode>(ResNode), {MemOp});

      ReplaceNode(N, ResNode);
      return;
    }

    // Other cases are autogenerated.
    break;
  }
  case ISD::FrameIndex: {
    // Selects to ADDri FI, 0 which in turn will become ADDri SP, imm.
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(
        FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    if (Subtarget->isThumb1Only()) {
      // Set the alignment of the frame object to 4, to avoid having to generate
      // more than one ADD
      MachineFrameInfo &MFI = MF->getFrameInfo();
      if (MFI.getObjectAlignment(FI) < 4)
        MFI.setObjectAlignment(FI, 4);
      CurDAG->SelectNodeTo(N, ARM::tADDframe, MVT::i32, TFI,
                           CurDAG->getTargetConstant(0, dl, MVT::i32));
      return;
    } else {
      unsigned Opc = ((Subtarget->isThumb() && Subtarget->hasThumb2()) ?
                      ARM::t2ADDri : ARM::ADDri);
      SDValue Ops[] = { TFI, CurDAG->getTargetConstant(0, dl, MVT::i32),
                        getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops);
      return;
    }
  }
  case ISD::SRL:
    if (tryV6T2BitfieldExtractOp(N, false))
      return;
    break;
  case ISD::SIGN_EXTEND_INREG:
  case ISD::SRA:
    if (tryV6T2BitfieldExtractOp(N, true))
      return;
    break;
  case ISD::MUL:
    if (Subtarget->isThumb1Only())
      break;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
      unsigned RHSV = C->getZExtValue();
      if (!RHSV) break;
      if (isPowerOf2_32(RHSV-1)) {  // 2^n+1?
        unsigned ShImm = Log2_32(RHSV-1);
        if (ShImm >= 32)
          break;
        SDValue V = N->getOperand(0);
        ShImm = ARM_AM::getSORegOpc(ARM_AM::lsl, ShImm);
        SDValue ShImmOp = CurDAG->getTargetConstant(ShImm, dl, MVT::i32);
        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
        if (Subtarget->isThumb()) {
          SDValue Ops[] = { V, V, ShImmOp, getAL(CurDAG, dl), Reg0, Reg0 };
          CurDAG->SelectNodeTo(N, ARM::t2ADDrs, MVT::i32, Ops);
          return;
        } else {
          SDValue Ops[] = { V, V, Reg0, ShImmOp, getAL(CurDAG, dl), Reg0,
                            Reg0 };
          CurDAG->SelectNodeTo(N, ARM::ADDrsi, MVT::i32, Ops);
          return;
        }
      }
      if (isPowerOf2_32(RHSV+1)) {  // 2^n-1?
        unsigned ShImm = Log2_32(RHSV+1);
        if (ShImm >= 32)
          break;
        SDValue V = N->getOperand(0);
        ShImm = ARM_AM::getSORegOpc(ARM_AM::lsl, ShImm);
        SDValue ShImmOp = CurDAG->getTargetConstant(ShImm, dl, MVT::i32);
        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
        if (Subtarget->isThumb()) {
          SDValue Ops[] = { V, V, ShImmOp, getAL(CurDAG, dl), Reg0, Reg0 };
          CurDAG->SelectNodeTo(N, ARM::t2RSBrs, MVT::i32, Ops);
          return;
        } else {
          SDValue Ops[] = { V, V, Reg0, ShImmOp, getAL(CurDAG, dl), Reg0,
                            Reg0 };
          CurDAG->SelectNodeTo(N, ARM::RSBrsi, MVT::i32, Ops);
          return;
        }
      }
    }
    break;
  case ISD::AND: {
    // Check for unsigned bitfield extract
    if (tryV6T2BitfieldExtractOp(N, false))
      return;

    // If an immediate is used in an AND node, it is possible that the immediate
    // can be more optimally materialized when negated. If this is the case we
    // can negate the immediate and use a BIC instead.
    auto *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (N1C && N1C->hasOneUse() && Subtarget->isThumb()) {
      uint32_t Imm = (uint32_t) N1C->getZExtValue();

      // In Thumb2 mode, an AND can take a 12-bit immediate. If this
      // immediate can be negated and fit in the immediate operand of
      // a t2BIC, don't do any manual transform here as this can be
      // handled by the generic ISel machinery.
      bool PreferImmediateEncoding =
        Subtarget->hasThumb2() && (is_t2_so_imm(Imm) || is_t2_so_imm_not(Imm));
      if (!PreferImmediateEncoding &&
          ConstantMaterializationCost(Imm) >
              ConstantMaterializationCost(~Imm)) {
        // The current immediate costs more to materialize than a negated
        // immediate, so negate the immediate and use a BIC.
        SDValue NewImm =
          CurDAG->getConstant(~N1C->getZExtValue(), dl, MVT::i32);
        // If the new constant didn't exist before, reposition it in the topological
        // ordering so it is just before N. Otherwise, don't touch its location.
        if (NewImm->getNodeId() == -1)
          CurDAG->RepositionNode(N->getIterator(), NewImm.getNode());

        if (!Subtarget->hasThumb2()) {
          SDValue Ops[] = {CurDAG->getRegister(ARM::CPSR, MVT::i32),
                           N->getOperand(0), NewImm, getAL(CurDAG, dl),
                           CurDAG->getRegister(0, MVT::i32)};
          ReplaceNode(N, CurDAG->getMachineNode(ARM::tBIC, dl, MVT::i32, Ops));
          return;
        } else {
          SDValue Ops[] = {N->getOperand(0), NewImm, getAL(CurDAG, dl),
                           CurDAG->getRegister(0, MVT::i32),
                           CurDAG->getRegister(0, MVT::i32)};
          ReplaceNode(N,
                      CurDAG->getMachineNode(ARM::t2BICrr, dl, MVT::i32, Ops));
          return;
        }
      }
    }

    // (and (or x, c2), c1) and top 16-bits of c1 and c2 match, lower 16-bits
    // of c1 are 0xffff, and lower 16-bit of c2 are 0. That is, the top 16-bits
    // are entirely contributed by c2 and lower 16-bits are entirely contributed
    // by x. That's equal to (or (and x, 0xffff), (and c1, 0xffff0000)).
    // Select it to: "movt x, ((c1 & 0xffff) >> 16)
    EVT VT = N->getValueType(0);
    if (VT != MVT::i32)
      break;
    unsigned Opc = (Subtarget->isThumb() && Subtarget->hasThumb2())
      ? ARM::t2MOVTi16
      : (Subtarget->hasV6T2Ops() ? ARM::MOVTi16 : 0);
    if (!Opc)
      break;
    SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);
    N1C = dyn_cast<ConstantSDNode>(N1);
    if (!N1C)
      break;
    if (N0.getOpcode() == ISD::OR && N0.getNode()->hasOneUse()) {
      SDValue N2 = N0.getOperand(1);
      ConstantSDNode *N2C = dyn_cast<ConstantSDNode>(N2);
      if (!N2C)
        break;
      unsigned N1CVal = N1C->getZExtValue();
      unsigned N2CVal = N2C->getZExtValue();
      if ((N1CVal & 0xffff0000U) == (N2CVal & 0xffff0000U) &&
          (N1CVal & 0xffffU) == 0xffffU &&
          (N2CVal & 0xffffU) == 0x0U) {
        SDValue Imm16 = CurDAG->getTargetConstant((N2CVal & 0xFFFF0000U) >> 16,
                                                  dl, MVT::i32);
        SDValue Ops[] = { N0.getOperand(0), Imm16,
                          getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32) };
        ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, VT, Ops));
        return;
      }
    }

    break;
  }
  case ARMISD::UMAAL: {
    unsigned Opc = Subtarget->isThumb() ? ARM::t2UMAAL : ARM::UMAAL;
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1),
                      N->getOperand(2), N->getOperand(3),
                      getAL(CurDAG, dl),
                      CurDAG->getRegister(0, MVT::i32) };
    ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, MVT::i32, MVT::i32, Ops));
    return;
  }
  case ARMISD::UMLAL:{
    if (Subtarget->isThumb()) {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                        N->getOperand(3), getAL(CurDAG, dl),
                        CurDAG->getRegister(0, MVT::i32)};
      ReplaceNode(
          N, CurDAG->getMachineNode(ARM::t2UMLAL, dl, MVT::i32, MVT::i32, Ops));
      return;
    }else{
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                        N->getOperand(3), getAL(CurDAG, dl),
                        CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      ReplaceNode(N, CurDAG->getMachineNode(
                         Subtarget->hasV6Ops() ? ARM::UMLAL : ARM::UMLALv5, dl,
                         MVT::i32, MVT::i32, Ops));
      return;
    }
  }
  case ARMISD::SMLAL:{
    if (Subtarget->isThumb()) {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                        N->getOperand(3), getAL(CurDAG, dl),
                        CurDAG->getRegister(0, MVT::i32)};
      ReplaceNode(
          N, CurDAG->getMachineNode(ARM::t2SMLAL, dl, MVT::i32, MVT::i32, Ops));
      return;
    }else{
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                        N->getOperand(3), getAL(CurDAG, dl),
                        CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      ReplaceNode(N, CurDAG->getMachineNode(
                         Subtarget->hasV6Ops() ? ARM::SMLAL : ARM::SMLALv5, dl,
                         MVT::i32, MVT::i32, Ops));
      return;
    }
  }
  case ARMISD::SUBE: {
    if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP())
      break;
    // Look for a pattern to match SMMLS
    // (sube a, (smul_loHi a, b), (subc 0, (smul_LOhi(a, b))))
    if (N->getOperand(1).getOpcode() != ISD::SMUL_LOHI ||
        N->getOperand(2).getOpcode() != ARMISD::SUBC ||
        !SDValue(N, 1).use_empty())
      break;

    if (Subtarget->isThumb())
      assert(Subtarget->hasThumb2() &&
             "This pattern should not be generated for Thumb");

    SDValue SmulLoHi = N->getOperand(1);
    SDValue Subc = N->getOperand(2);
    auto *Zero = dyn_cast<ConstantSDNode>(Subc.getOperand(0));

    if (!Zero || Zero->getZExtValue() != 0 ||
        Subc.getOperand(1) != SmulLoHi.getValue(0) ||
        N->getOperand(1) != SmulLoHi.getValue(1) ||
        N->getOperand(2) != Subc.getValue(1))
      break;

    unsigned Opc = Subtarget->isThumb2() ? ARM::t2SMMLS : ARM::SMMLS;
    SDValue Ops[] = { SmulLoHi.getOperand(0), SmulLoHi.getOperand(1),
                      N->getOperand(0), getAL(CurDAG, dl),
                      CurDAG->getRegister(0, MVT::i32) };
    ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, MVT::i32, Ops));
    return;
  }
  case ISD::LOAD: {
    if (Subtarget->isThumb() && Subtarget->hasThumb2()) {
      if (tryT2IndexedLoad(N))
        return;
    } else if (Subtarget->isThumb()) {
      if (tryT1IndexedLoad(N))
        return;
    } else if (tryARMIndexedLoad(N))
      return;
    // Other cases are autogenerated.
    break;
  }
  case ARMISD::BRCOND: {
    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (Bcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (tBcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (t2Bcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    unsigned Opc = Subtarget->isThumb() ?
      ((Subtarget->hasThumb2()) ? ARM::t2Bcc : ARM::tBcc) : ARM::Bcc;
    SDValue Chain = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    SDValue N3 = N->getOperand(3);
    SDValue InFlag = N->getOperand(4);
    assert(N1.getOpcode() == ISD::BasicBlock);
    assert(N2.getOpcode() == ISD::Constant);
    assert(N3.getOpcode() == ISD::Register);

    unsigned CC = (unsigned) cast<ConstantSDNode>(N2)->getZExtValue();

    if (InFlag.getOpcode() == ARMISD::CMPZ) {
      bool SwitchEQNEToPLMI;
      SelectCMPZ(InFlag.getNode(), SwitchEQNEToPLMI);
      InFlag = N->getOperand(4);

      if (SwitchEQNEToPLMI) {
        switch ((ARMCC::CondCodes)CC) {
        default: llvm_unreachable("CMPZ must be either NE or EQ!");
        case ARMCC::NE:
          CC = (unsigned)ARMCC::MI;
          break;
        case ARMCC::EQ:
          CC = (unsigned)ARMCC::PL;
          break;
        }
      }
    }

    SDValue Tmp2 = CurDAG->getTargetConstant(CC, dl, MVT::i32);
    SDValue Ops[] = { N1, Tmp2, N3, Chain, InFlag };
    SDNode *ResNode = CurDAG->getMachineNode(Opc, dl, MVT::Other,
                                             MVT::Glue, Ops);
    Chain = SDValue(ResNode, 0);
    if (N->getNumValues() == 2) {
      InFlag = SDValue(ResNode, 1);
      ReplaceUses(SDValue(N, 1), InFlag);
    }
    ReplaceUses(SDValue(N, 0),
                SDValue(Chain.getNode(), Chain.getResNo()));
    CurDAG->RemoveDeadNode(N);
    return;
  }

  case ARMISD::CMPZ: {
    // select (CMPZ X, #-C) -> (CMPZ (ADDS X, #C), #0)
    //   This allows us to avoid materializing the expensive negative constant.
    //   The CMPZ #0 is useless and will be peepholed away but we need to keep it
    //   for its glue output.
    SDValue X = N->getOperand(0);
    auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1).getNode());
    if (C && C->getSExtValue() < 0 && Subtarget->isThumb()) {
      int64_t Addend = -C->getSExtValue();

      SDNode *Add = nullptr;
      // ADDS can be better than CMN if the immediate fits in a
      // 16-bit ADDS, which means either [0,256) for tADDi8 or [0,8) for tADDi3.
      // Outside that range we can just use a CMN which is 32-bit but has a
      // 12-bit immediate range.
      if (Addend < 1<<8) {
        if (Subtarget->isThumb2()) {
          SDValue Ops[] = { X, CurDAG->getTargetConstant(Addend, dl, MVT::i32),
                            getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32),
                            CurDAG->getRegister(0, MVT::i32) };
          Add = CurDAG->getMachineNode(ARM::t2ADDri, dl, MVT::i32, Ops);
        } else {
          unsigned Opc = (Addend < 1<<3) ? ARM::tADDi3 : ARM::tADDi8;
          SDValue Ops[] = {CurDAG->getRegister(ARM::CPSR, MVT::i32), X,
                           CurDAG->getTargetConstant(Addend, dl, MVT::i32),
                           getAL(CurDAG, dl), CurDAG->getRegister(0, MVT::i32)};
          Add = CurDAG->getMachineNode(Opc, dl, MVT::i32, Ops);
        }
      }
      if (Add) {
        SDValue Ops2[] = {SDValue(Add, 0), CurDAG->getConstant(0, dl, MVT::i32)};
        CurDAG->MorphNodeTo(N, ARMISD::CMPZ, CurDAG->getVTList(MVT::Glue), Ops2);
      }
    }
    // Other cases are autogenerated.
    break;
  }

  case ARMISD::CMOV: {
    SDValue InFlag = N->getOperand(4);

    if (InFlag.getOpcode() == ARMISD::CMPZ) {
      bool SwitchEQNEToPLMI;
      SelectCMPZ(InFlag.getNode(), SwitchEQNEToPLMI);

      if (SwitchEQNEToPLMI) {
        SDValue ARMcc = N->getOperand(2);
        ARMCC::CondCodes CC =
          (ARMCC::CondCodes)cast<ConstantSDNode>(ARMcc)->getZExtValue();

        switch (CC) {
        default: llvm_unreachable("CMPZ must be either NE or EQ!");
        case ARMCC::NE:
          CC = ARMCC::MI;
          break;
        case ARMCC::EQ:
          CC = ARMCC::PL;
          break;
        }
        SDValue NewARMcc = CurDAG->getConstant((unsigned)CC, dl, MVT::i32);
        SDValue Ops[] = {N->getOperand(0), N->getOperand(1), NewARMcc,
                         N->getOperand(3), N->getOperand(4)};
        CurDAG->MorphNodeTo(N, ARMISD::CMOV, N->getVTList(), Ops);
      }

    }
    // Other cases are autogenerated.
    break;
  }

  case ARMISD::VZIP: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return;
    case MVT::v8i8:  Opc = ARM::VZIPd8; break;
    case MVT::v4f16:
    case MVT::v4i16: Opc = ARM::VZIPd16; break;
    case MVT::v2f32:
    // vzip.32 Dd, Dm is a pseudo-instruction expanded to vtrn.32 Dd, Dm.
    case MVT::v2i32: Opc = ARM::VTRNd32; break;
    case MVT::v16i8: Opc = ARM::VZIPq8; break;
    case MVT::v8f16:
    case MVT::v8i16: Opc = ARM::VZIPq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VZIPq32; break;
    }
    SDValue Pred = getAL(CurDAG, dl);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, VT, VT, Ops));
    return;
  }
  case ARMISD::VUZP: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return;
    case MVT::v8i8:  Opc = ARM::VUZPd8; break;
    case MVT::v4f16:
    case MVT::v4i16: Opc = ARM::VUZPd16; break;
    case MVT::v2f32:
    // vuzp.32 Dd, Dm is a pseudo-instruction expanded to vtrn.32 Dd, Dm.
    case MVT::v2i32: Opc = ARM::VTRNd32; break;
    case MVT::v16i8: Opc = ARM::VUZPq8; break;
    case MVT::v8f16:
    case MVT::v8i16: Opc = ARM::VUZPq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VUZPq32; break;
    }
    SDValue Pred = getAL(CurDAG, dl);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, VT, VT, Ops));
    return;
  }
  case ARMISD::VTRN: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return;
    case MVT::v8i8:  Opc = ARM::VTRNd8; break;
    case MVT::v4f16:
    case MVT::v4i16: Opc = ARM::VTRNd16; break;
    case MVT::v2f32:
    case MVT::v2i32: Opc = ARM::VTRNd32; break;
    case MVT::v16i8: Opc = ARM::VTRNq8; break;
    case MVT::v8f16:
    case MVT::v8i16: Opc = ARM::VTRNq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VTRNq32; break;
    }
    SDValue Pred = getAL(CurDAG, dl);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, VT, VT, Ops));
    return;
  }
  case ARMISD::BUILD_VECTOR: {
    EVT VecVT = N->getValueType(0);
    EVT EltVT = VecVT.getVectorElementType();
    unsigned NumElts = VecVT.getVectorNumElements();
    if (EltVT == MVT::f64) {
      assert(NumElts == 2 && "unexpected type for BUILD_VECTOR");
      ReplaceNode(
          N, createDRegPairNode(VecVT, N->getOperand(0), N->getOperand(1)));
      return;
    }
    assert(EltVT == MVT::f32 && "unexpected type for BUILD_VECTOR");
    if (NumElts == 2) {
      ReplaceNode(
          N, createSRegPairNode(VecVT, N->getOperand(0), N->getOperand(1)));
      return;
    }
    assert(NumElts == 4 && "unexpected type for BUILD_VECTOR");
    ReplaceNode(N,
                createQuadSRegsNode(VecVT, N->getOperand(0), N->getOperand(1),
                                    N->getOperand(2), N->getOperand(3)));
    return;
  }

  case ARMISD::VLD1DUP: {
    static const uint16_t DOpcodes[] = { ARM::VLD1DUPd8, ARM::VLD1DUPd16,
                                         ARM::VLD1DUPd32 };
    static const uint16_t QOpcodes[] = { ARM::VLD1DUPq8, ARM::VLD1DUPq16,
                                         ARM::VLD1DUPq32 };
    SelectVLDDup(N, /* IsIntrinsic= */ false, false, 1, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VLD2DUP: {
    static const uint16_t Opcodes[] = { ARM::VLD2DUPd8, ARM::VLD2DUPd16,
                                        ARM::VLD2DUPd32 };
    SelectVLDDup(N, /* IsIntrinsic= */ false, false, 2, Opcodes);
    return;
  }

  case ARMISD::VLD3DUP: {
    static const uint16_t Opcodes[] = { ARM::VLD3DUPd8Pseudo,
                                        ARM::VLD3DUPd16Pseudo,
                                        ARM::VLD3DUPd32Pseudo };
    SelectVLDDup(N, /* IsIntrinsic= */ false, false, 3, Opcodes);
    return;
  }

  case ARMISD::VLD4DUP: {
    static const uint16_t Opcodes[] = { ARM::VLD4DUPd8Pseudo,
                                        ARM::VLD4DUPd16Pseudo,
                                        ARM::VLD4DUPd32Pseudo };
    SelectVLDDup(N, /* IsIntrinsic= */ false, false, 4, Opcodes);
    return;
  }

  case ARMISD::VLD1DUP_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD1DUPd8wb_fixed,
                                         ARM::VLD1DUPd16wb_fixed,
                                         ARM::VLD1DUPd32wb_fixed };
    static const uint16_t QOpcodes[] = { ARM::VLD1DUPq8wb_fixed,
                                         ARM::VLD1DUPq16wb_fixed,
                                         ARM::VLD1DUPq32wb_fixed };
    SelectVLDDup(N, /* IsIntrinsic= */ false, true, 1, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VLD2DUP_UPD: {
    static const uint16_t Opcodes[] = { ARM::VLD2DUPd8wb_fixed,
                                        ARM::VLD2DUPd16wb_fixed,
                                        ARM::VLD2DUPd32wb_fixed };
    SelectVLDDup(N, /* IsIntrinsic= */ false, true, 2, Opcodes);
    return;
  }

  case ARMISD::VLD3DUP_UPD: {
    static const uint16_t Opcodes[] = { ARM::VLD3DUPd8Pseudo_UPD,
                                        ARM::VLD3DUPd16Pseudo_UPD,
                                        ARM::VLD3DUPd32Pseudo_UPD };
    SelectVLDDup(N, /* IsIntrinsic= */ false, true, 3, Opcodes);
    return;
  }

  case ARMISD::VLD4DUP_UPD: {
    static const uint16_t Opcodes[] = { ARM::VLD4DUPd8Pseudo_UPD,
                                        ARM::VLD4DUPd16Pseudo_UPD,
                                        ARM::VLD4DUPd32Pseudo_UPD };
    SelectVLDDup(N, /* IsIntrinsic= */ false, true, 4, Opcodes);
    return;
  }

  case ARMISD::VLD1_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD1d8wb_fixed,
                                         ARM::VLD1d16wb_fixed,
                                         ARM::VLD1d32wb_fixed,
                                         ARM::VLD1d64wb_fixed };
    static const uint16_t QOpcodes[] = { ARM::VLD1q8wb_fixed,
                                         ARM::VLD1q16wb_fixed,
                                         ARM::VLD1q32wb_fixed,
                                         ARM::VLD1q64wb_fixed };
    SelectVLD(N, true, 1, DOpcodes, QOpcodes, nullptr);
    return;
  }

  case ARMISD::VLD2_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD2d8wb_fixed,
                                         ARM::VLD2d16wb_fixed,
                                         ARM::VLD2d32wb_fixed,
                                         ARM::VLD1q64wb_fixed};
    static const uint16_t QOpcodes[] = { ARM::VLD2q8PseudoWB_fixed,
                                         ARM::VLD2q16PseudoWB_fixed,
                                         ARM::VLD2q32PseudoWB_fixed };
    SelectVLD(N, true, 2, DOpcodes, QOpcodes, nullptr);
    return;
  }

  case ARMISD::VLD3_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD3d8Pseudo_UPD,
                                         ARM::VLD3d16Pseudo_UPD,
                                         ARM::VLD3d32Pseudo_UPD,
                                         ARM::VLD1d64TPseudoWB_fixed};
    static const uint16_t QOpcodes0[] = { ARM::VLD3q8Pseudo_UPD,
                                          ARM::VLD3q16Pseudo_UPD,
                                          ARM::VLD3q32Pseudo_UPD };
    static const uint16_t QOpcodes1[] = { ARM::VLD3q8oddPseudo_UPD,
                                          ARM::VLD3q16oddPseudo_UPD,
                                          ARM::VLD3q32oddPseudo_UPD };
    SelectVLD(N, true, 3, DOpcodes, QOpcodes0, QOpcodes1);
    return;
  }

  case ARMISD::VLD4_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD4d8Pseudo_UPD,
                                         ARM::VLD4d16Pseudo_UPD,
                                         ARM::VLD4d32Pseudo_UPD,
                                         ARM::VLD1d64QPseudoWB_fixed};
    static const uint16_t QOpcodes0[] = { ARM::VLD4q8Pseudo_UPD,
                                          ARM::VLD4q16Pseudo_UPD,
                                          ARM::VLD4q32Pseudo_UPD };
    static const uint16_t QOpcodes1[] = { ARM::VLD4q8oddPseudo_UPD,
                                          ARM::VLD4q16oddPseudo_UPD,
                                          ARM::VLD4q32oddPseudo_UPD };
    SelectVLD(N, true, 4, DOpcodes, QOpcodes0, QOpcodes1);
    return;
  }

  case ARMISD::VLD2LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD2LNd8Pseudo_UPD,
                                         ARM::VLD2LNd16Pseudo_UPD,
                                         ARM::VLD2LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VLD2LNq16Pseudo_UPD,
                                         ARM::VLD2LNq32Pseudo_UPD };
    SelectVLDSTLane(N, true, true, 2, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VLD3LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD3LNd8Pseudo_UPD,
                                         ARM::VLD3LNd16Pseudo_UPD,
                                         ARM::VLD3LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VLD3LNq16Pseudo_UPD,
                                         ARM::VLD3LNq32Pseudo_UPD };
    SelectVLDSTLane(N, true, true, 3, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VLD4LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VLD4LNd8Pseudo_UPD,
                                         ARM::VLD4LNd16Pseudo_UPD,
                                         ARM::VLD4LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VLD4LNq16Pseudo_UPD,
                                         ARM::VLD4LNq32Pseudo_UPD };
    SelectVLDSTLane(N, true, true, 4, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VST1_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST1d8wb_fixed,
                                         ARM::VST1d16wb_fixed,
                                         ARM::VST1d32wb_fixed,
                                         ARM::VST1d64wb_fixed };
    static const uint16_t QOpcodes[] = { ARM::VST1q8wb_fixed,
                                         ARM::VST1q16wb_fixed,
                                         ARM::VST1q32wb_fixed,
                                         ARM::VST1q64wb_fixed };
    SelectVST(N, true, 1, DOpcodes, QOpcodes, nullptr);
    return;
  }

  case ARMISD::VST2_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST2d8wb_fixed,
                                         ARM::VST2d16wb_fixed,
                                         ARM::VST2d32wb_fixed,
                                         ARM::VST1q64wb_fixed};
    static const uint16_t QOpcodes[] = { ARM::VST2q8PseudoWB_fixed,
                                         ARM::VST2q16PseudoWB_fixed,
                                         ARM::VST2q32PseudoWB_fixed };
    SelectVST(N, true, 2, DOpcodes, QOpcodes, nullptr);
    return;
  }

  case ARMISD::VST3_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST3d8Pseudo_UPD,
                                         ARM::VST3d16Pseudo_UPD,
                                         ARM::VST3d32Pseudo_UPD,
                                         ARM::VST1d64TPseudoWB_fixed};
    static const uint16_t QOpcodes0[] = { ARM::VST3q8Pseudo_UPD,
                                          ARM::VST3q16Pseudo_UPD,
                                          ARM::VST3q32Pseudo_UPD };
    static const uint16_t QOpcodes1[] = { ARM::VST3q8oddPseudo_UPD,
                                          ARM::VST3q16oddPseudo_UPD,
                                          ARM::VST3q32oddPseudo_UPD };
    SelectVST(N, true, 3, DOpcodes, QOpcodes0, QOpcodes1);
    return;
  }

  case ARMISD::VST4_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST4d8Pseudo_UPD,
                                         ARM::VST4d16Pseudo_UPD,
                                         ARM::VST4d32Pseudo_UPD,
                                         ARM::VST1d64QPseudoWB_fixed};
    static const uint16_t QOpcodes0[] = { ARM::VST4q8Pseudo_UPD,
                                          ARM::VST4q16Pseudo_UPD,
                                          ARM::VST4q32Pseudo_UPD };
    static const uint16_t QOpcodes1[] = { ARM::VST4q8oddPseudo_UPD,
                                          ARM::VST4q16oddPseudo_UPD,
                                          ARM::VST4q32oddPseudo_UPD };
    SelectVST(N, true, 4, DOpcodes, QOpcodes0, QOpcodes1);
    return;
  }

  case ARMISD::VST2LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST2LNd8Pseudo_UPD,
                                         ARM::VST2LNd16Pseudo_UPD,
                                         ARM::VST2LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VST2LNq16Pseudo_UPD,
                                         ARM::VST2LNq32Pseudo_UPD };
    SelectVLDSTLane(N, false, true, 2, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VST3LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST3LNd8Pseudo_UPD,
                                         ARM::VST3LNd16Pseudo_UPD,
                                         ARM::VST3LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VST3LNq16Pseudo_UPD,
                                         ARM::VST3LNq32Pseudo_UPD };
    SelectVLDSTLane(N, false, true, 3, DOpcodes, QOpcodes);
    return;
  }

  case ARMISD::VST4LN_UPD: {
    static const uint16_t DOpcodes[] = { ARM::VST4LNd8Pseudo_UPD,
                                         ARM::VST4LNd16Pseudo_UPD,
                                         ARM::VST4LNd32Pseudo_UPD };
    static const uint16_t QOpcodes[] = { ARM::VST4LNq16Pseudo_UPD,
                                         ARM::VST4LNq32Pseudo_UPD };
    SelectVLDSTLane(N, false, true, 4, DOpcodes, QOpcodes);
    return;
  }

  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    switch (IntNo) {
    default:
      break;

    case Intrinsic::arm_mrrc:
    case Intrinsic::arm_mrrc2: {
      SDLoc dl(N);
      SDValue Chain = N->getOperand(0);
      unsigned Opc;

      if (Subtarget->isThumb())
        Opc = (IntNo == Intrinsic::arm_mrrc ? ARM::t2MRRC : ARM::t2MRRC2);
      else
        Opc = (IntNo == Intrinsic::arm_mrrc ? ARM::MRRC : ARM::MRRC2);

      SmallVector<SDValue, 5> Ops;
      Ops.push_back(getI32Imm(cast<ConstantSDNode>(N->getOperand(2))->getZExtValue(), dl)); /* coproc */
      Ops.push_back(getI32Imm(cast<ConstantSDNode>(N->getOperand(3))->getZExtValue(), dl)); /* opc */
      Ops.push_back(getI32Imm(cast<ConstantSDNode>(N->getOperand(4))->getZExtValue(), dl)); /* CRm */

      // The mrrc2 instruction in ARM doesn't allow predicates, the top 4 bits of the encoded
      // instruction will always be '1111' but it is possible in assembly language to specify
      // AL as a predicate to mrrc2 but it doesn't make any difference to the encoded instruction.
      if (Opc != ARM::MRRC2) {
        Ops.push_back(getAL(CurDAG, dl));
        Ops.push_back(CurDAG->getRegister(0, MVT::i32));
      }

      Ops.push_back(Chain);

      // Writes to two registers.
      const EVT RetType[] = {MVT::i32, MVT::i32, MVT::Other};

      ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, RetType, Ops));
      return;
    }
    case Intrinsic::arm_ldaexd:
    case Intrinsic::arm_ldrexd: {
      SDLoc dl(N);
      SDValue Chain = N->getOperand(0);
      SDValue MemAddr = N->getOperand(2);
      bool isThumb = Subtarget->isThumb() && Subtarget->hasV8MBaselineOps();

      bool IsAcquire = IntNo == Intrinsic::arm_ldaexd;
      unsigned NewOpc = isThumb ? (IsAcquire ? ARM::t2LDAEXD : ARM::t2LDREXD)
                                : (IsAcquire ? ARM::LDAEXD : ARM::LDREXD);

      // arm_ldrexd returns a i64 value in {i32, i32}
      std::vector<EVT> ResTys;
      if (isThumb) {
        ResTys.push_back(MVT::i32);
        ResTys.push_back(MVT::i32);
      } else
        ResTys.push_back(MVT::Untyped);
      ResTys.push_back(MVT::Other);

      // Place arguments in the right order.
      SDValue Ops[] = {MemAddr, getAL(CurDAG, dl),
                       CurDAG->getRegister(0, MVT::i32), Chain};
      SDNode *Ld = CurDAG->getMachineNode(NewOpc, dl, ResTys, Ops);
      // Transfer memoperands.
      MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Ld), {MemOp});

      // Remap uses.
      SDValue OutChain = isThumb ? SDValue(Ld, 2) : SDValue(Ld, 1);
      if (!SDValue(N, 0).use_empty()) {
        SDValue Result;
        if (isThumb)
          Result = SDValue(Ld, 0);
        else {
          SDValue SubRegIdx =
            CurDAG->getTargetConstant(ARM::gsub_0, dl, MVT::i32);
          SDNode *ResNode = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
              dl, MVT::i32, SDValue(Ld, 0), SubRegIdx);
          Result = SDValue(ResNode,0);
        }
        ReplaceUses(SDValue(N, 0), Result);
      }
      if (!SDValue(N, 1).use_empty()) {
        SDValue Result;
        if (isThumb)
          Result = SDValue(Ld, 1);
        else {
          SDValue SubRegIdx =
            CurDAG->getTargetConstant(ARM::gsub_1, dl, MVT::i32);
          SDNode *ResNode = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
              dl, MVT::i32, SDValue(Ld, 0), SubRegIdx);
          Result = SDValue(ResNode,0);
        }
        ReplaceUses(SDValue(N, 1), Result);
      }
      ReplaceUses(SDValue(N, 2), OutChain);
      CurDAG->RemoveDeadNode(N);
      return;
    }
    case Intrinsic::arm_stlexd:
    case Intrinsic::arm_strexd: {
      SDLoc dl(N);
      SDValue Chain = N->getOperand(0);
      SDValue Val0 = N->getOperand(2);
      SDValue Val1 = N->getOperand(3);
      SDValue MemAddr = N->getOperand(4);

      // Store exclusive double return a i32 value which is the return status
      // of the issued store.
      const EVT ResTys[] = {MVT::i32, MVT::Other};

      bool isThumb = Subtarget->isThumb() && Subtarget->hasThumb2();
      // Place arguments in the right order.
      SmallVector<SDValue, 7> Ops;
      if (isThumb) {
        Ops.push_back(Val0);
        Ops.push_back(Val1);
      } else
        // arm_strexd uses GPRPair.
        Ops.push_back(SDValue(createGPRPairNode(MVT::Untyped, Val0, Val1), 0));
      Ops.push_back(MemAddr);
      Ops.push_back(getAL(CurDAG, dl));
      Ops.push_back(CurDAG->getRegister(0, MVT::i32));
      Ops.push_back(Chain);

      bool IsRelease = IntNo == Intrinsic::arm_stlexd;
      unsigned NewOpc = isThumb ? (IsRelease ? ARM::t2STLEXD : ARM::t2STREXD)
                                : (IsRelease ? ARM::STLEXD : ARM::STREXD);

      SDNode *St = CurDAG->getMachineNode(NewOpc, dl, ResTys, Ops);
      // Transfer memoperands.
      MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(St), {MemOp});

      ReplaceNode(N, St);
      return;
    }

    case Intrinsic::arm_neon_vld1: {
      static const uint16_t DOpcodes[] = { ARM::VLD1d8, ARM::VLD1d16,
                                           ARM::VLD1d32, ARM::VLD1d64 };
      static const uint16_t QOpcodes[] = { ARM::VLD1q8, ARM::VLD1q16,
                                           ARM::VLD1q32, ARM::VLD1q64};
      SelectVLD(N, false, 1, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vld1x2: {
      static const uint16_t DOpcodes[] = { ARM::VLD1q8, ARM::VLD1q16,
                                           ARM::VLD1q32, ARM::VLD1q64 };
      static const uint16_t QOpcodes[] = { ARM::VLD1d8QPseudo,
                                           ARM::VLD1d16QPseudo,
                                           ARM::VLD1d32QPseudo,
                                           ARM::VLD1d64QPseudo };
      SelectVLD(N, false, 2, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vld1x3: {
      static const uint16_t DOpcodes[] = { ARM::VLD1d8TPseudo,
                                           ARM::VLD1d16TPseudo,
                                           ARM::VLD1d32TPseudo,
                                           ARM::VLD1d64TPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD1q8LowTPseudo_UPD,
                                            ARM::VLD1q16LowTPseudo_UPD,
                                            ARM::VLD1q32LowTPseudo_UPD,
                                            ARM::VLD1q64LowTPseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VLD1q8HighTPseudo,
                                            ARM::VLD1q16HighTPseudo,
                                            ARM::VLD1q32HighTPseudo,
                                            ARM::VLD1q64HighTPseudo };
      SelectVLD(N, false, 3, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld1x4: {
      static const uint16_t DOpcodes[] = { ARM::VLD1d8QPseudo,
                                           ARM::VLD1d16QPseudo,
                                           ARM::VLD1d32QPseudo,
                                           ARM::VLD1d64QPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD1q8LowQPseudo_UPD,
                                            ARM::VLD1q16LowQPseudo_UPD,
                                            ARM::VLD1q32LowQPseudo_UPD,
                                            ARM::VLD1q64LowQPseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VLD1q8HighQPseudo,
                                            ARM::VLD1q16HighQPseudo,
                                            ARM::VLD1q32HighQPseudo,
                                            ARM::VLD1q64HighQPseudo };
      SelectVLD(N, false, 4, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld2: {
      static const uint16_t DOpcodes[] = { ARM::VLD2d8, ARM::VLD2d16,
                                           ARM::VLD2d32, ARM::VLD1q64 };
      static const uint16_t QOpcodes[] = { ARM::VLD2q8Pseudo, ARM::VLD2q16Pseudo,
                                           ARM::VLD2q32Pseudo };
      SelectVLD(N, false, 2, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vld3: {
      static const uint16_t DOpcodes[] = { ARM::VLD3d8Pseudo,
                                           ARM::VLD3d16Pseudo,
                                           ARM::VLD3d32Pseudo,
                                           ARM::VLD1d64TPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD3q8Pseudo_UPD,
                                            ARM::VLD3q16Pseudo_UPD,
                                            ARM::VLD3q32Pseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VLD3q8oddPseudo,
                                            ARM::VLD3q16oddPseudo,
                                            ARM::VLD3q32oddPseudo };
      SelectVLD(N, false, 3, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld4: {
      static const uint16_t DOpcodes[] = { ARM::VLD4d8Pseudo,
                                           ARM::VLD4d16Pseudo,
                                           ARM::VLD4d32Pseudo,
                                           ARM::VLD1d64QPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD4q8Pseudo_UPD,
                                            ARM::VLD4q16Pseudo_UPD,
                                            ARM::VLD4q32Pseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VLD4q8oddPseudo,
                                            ARM::VLD4q16oddPseudo,
                                            ARM::VLD4q32oddPseudo };
      SelectVLD(N, false, 4, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld2dup: {
      static const uint16_t DOpcodes[] = { ARM::VLD2DUPd8, ARM::VLD2DUPd16,
                                           ARM::VLD2DUPd32, ARM::VLD1q64 };
      static const uint16_t QOpcodes0[] = { ARM::VLD2DUPq8EvenPseudo,
                                            ARM::VLD2DUPq16EvenPseudo,
                                            ARM::VLD2DUPq32EvenPseudo };
      static const uint16_t QOpcodes1[] = { ARM::VLD2DUPq8OddPseudo,
                                            ARM::VLD2DUPq16OddPseudo,
                                            ARM::VLD2DUPq32OddPseudo };
      SelectVLDDup(N, /* IsIntrinsic= */ true, false, 2,
                   DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld3dup: {
      static const uint16_t DOpcodes[] = { ARM::VLD3DUPd8Pseudo,
                                           ARM::VLD3DUPd16Pseudo,
                                           ARM::VLD3DUPd32Pseudo,
                                           ARM::VLD1d64TPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD3DUPq8EvenPseudo,
                                            ARM::VLD3DUPq16EvenPseudo,
                                            ARM::VLD3DUPq32EvenPseudo };
      static const uint16_t QOpcodes1[] = { ARM::VLD3DUPq8OddPseudo,
                                            ARM::VLD3DUPq16OddPseudo,
                                            ARM::VLD3DUPq32OddPseudo };
      SelectVLDDup(N, /* IsIntrinsic= */ true, false, 3,
                   DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld4dup: {
      static const uint16_t DOpcodes[] = { ARM::VLD4DUPd8Pseudo,
                                           ARM::VLD4DUPd16Pseudo,
                                           ARM::VLD4DUPd32Pseudo,
                                           ARM::VLD1d64QPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VLD4DUPq8EvenPseudo,
                                            ARM::VLD4DUPq16EvenPseudo,
                                            ARM::VLD4DUPq32EvenPseudo };
      static const uint16_t QOpcodes1[] = { ARM::VLD4DUPq8OddPseudo,
                                            ARM::VLD4DUPq16OddPseudo,
                                            ARM::VLD4DUPq32OddPseudo };
      SelectVLDDup(N, /* IsIntrinsic= */ true, false, 4,
                   DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vld2lane: {
      static const uint16_t DOpcodes[] = { ARM::VLD2LNd8Pseudo,
                                           ARM::VLD2LNd16Pseudo,
                                           ARM::VLD2LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VLD2LNq16Pseudo,
                                           ARM::VLD2LNq32Pseudo };
      SelectVLDSTLane(N, true, false, 2, DOpcodes, QOpcodes);
      return;
    }

    case Intrinsic::arm_neon_vld3lane: {
      static const uint16_t DOpcodes[] = { ARM::VLD3LNd8Pseudo,
                                           ARM::VLD3LNd16Pseudo,
                                           ARM::VLD3LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VLD3LNq16Pseudo,
                                           ARM::VLD3LNq32Pseudo };
      SelectVLDSTLane(N, true, false, 3, DOpcodes, QOpcodes);
      return;
    }

    case Intrinsic::arm_neon_vld4lane: {
      static const uint16_t DOpcodes[] = { ARM::VLD4LNd8Pseudo,
                                           ARM::VLD4LNd16Pseudo,
                                           ARM::VLD4LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VLD4LNq16Pseudo,
                                           ARM::VLD4LNq32Pseudo };
      SelectVLDSTLane(N, true, false, 4, DOpcodes, QOpcodes);
      return;
    }

    case Intrinsic::arm_neon_vst1: {
      static const uint16_t DOpcodes[] = { ARM::VST1d8, ARM::VST1d16,
                                           ARM::VST1d32, ARM::VST1d64 };
      static const uint16_t QOpcodes[] = { ARM::VST1q8, ARM::VST1q16,
                                           ARM::VST1q32, ARM::VST1q64 };
      SelectVST(N, false, 1, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vst1x2: {
      static const uint16_t DOpcodes[] = { ARM::VST1q8, ARM::VST1q16,
                                           ARM::VST1q32, ARM::VST1q64 };
      static const uint16_t QOpcodes[] = { ARM::VST1d8QPseudo,
                                           ARM::VST1d16QPseudo,
                                           ARM::VST1d32QPseudo,
                                           ARM::VST1d64QPseudo };
      SelectVST(N, false, 2, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vst1x3: {
      static const uint16_t DOpcodes[] = { ARM::VST1d8TPseudo,
                                           ARM::VST1d16TPseudo,
                                           ARM::VST1d32TPseudo,
                                           ARM::VST1d64TPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VST1q8LowTPseudo_UPD,
                                            ARM::VST1q16LowTPseudo_UPD,
                                            ARM::VST1q32LowTPseudo_UPD,
                                            ARM::VST1q64LowTPseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VST1q8HighTPseudo,
                                            ARM::VST1q16HighTPseudo,
                                            ARM::VST1q32HighTPseudo,
                                            ARM::VST1q64HighTPseudo };
      SelectVST(N, false, 3, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vst1x4: {
      static const uint16_t DOpcodes[] = { ARM::VST1d8QPseudo,
                                           ARM::VST1d16QPseudo,
                                           ARM::VST1d32QPseudo,
                                           ARM::VST1d64QPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VST1q8LowQPseudo_UPD,
                                            ARM::VST1q16LowQPseudo_UPD,
                                            ARM::VST1q32LowQPseudo_UPD,
                                            ARM::VST1q64LowQPseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VST1q8HighQPseudo,
                                            ARM::VST1q16HighQPseudo,
                                            ARM::VST1q32HighQPseudo,
                                            ARM::VST1q64HighQPseudo };
      SelectVST(N, false, 4, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vst2: {
      static const uint16_t DOpcodes[] = { ARM::VST2d8, ARM::VST2d16,
                                           ARM::VST2d32, ARM::VST1q64 };
      static const uint16_t QOpcodes[] = { ARM::VST2q8Pseudo, ARM::VST2q16Pseudo,
                                           ARM::VST2q32Pseudo };
      SelectVST(N, false, 2, DOpcodes, QOpcodes, nullptr);
      return;
    }

    case Intrinsic::arm_neon_vst3: {
      static const uint16_t DOpcodes[] = { ARM::VST3d8Pseudo,
                                           ARM::VST3d16Pseudo,
                                           ARM::VST3d32Pseudo,
                                           ARM::VST1d64TPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VST3q8Pseudo_UPD,
                                            ARM::VST3q16Pseudo_UPD,
                                            ARM::VST3q32Pseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VST3q8oddPseudo,
                                            ARM::VST3q16oddPseudo,
                                            ARM::VST3q32oddPseudo };
      SelectVST(N, false, 3, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vst4: {
      static const uint16_t DOpcodes[] = { ARM::VST4d8Pseudo,
                                           ARM::VST4d16Pseudo,
                                           ARM::VST4d32Pseudo,
                                           ARM::VST1d64QPseudo };
      static const uint16_t QOpcodes0[] = { ARM::VST4q8Pseudo_UPD,
                                            ARM::VST4q16Pseudo_UPD,
                                            ARM::VST4q32Pseudo_UPD };
      static const uint16_t QOpcodes1[] = { ARM::VST4q8oddPseudo,
                                            ARM::VST4q16oddPseudo,
                                            ARM::VST4q32oddPseudo };
      SelectVST(N, false, 4, DOpcodes, QOpcodes0, QOpcodes1);
      return;
    }

    case Intrinsic::arm_neon_vst2lane: {
      static const uint16_t DOpcodes[] = { ARM::VST2LNd8Pseudo,
                                           ARM::VST2LNd16Pseudo,
                                           ARM::VST2LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VST2LNq16Pseudo,
                                           ARM::VST2LNq32Pseudo };
      SelectVLDSTLane(N, false, false, 2, DOpcodes, QOpcodes);
      return;
    }

    case Intrinsic::arm_neon_vst3lane: {
      static const uint16_t DOpcodes[] = { ARM::VST3LNd8Pseudo,
                                           ARM::VST3LNd16Pseudo,
                                           ARM::VST3LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VST3LNq16Pseudo,
                                           ARM::VST3LNq32Pseudo };
      SelectVLDSTLane(N, false, false, 3, DOpcodes, QOpcodes);
      return;
    }

    case Intrinsic::arm_neon_vst4lane: {
      static const uint16_t DOpcodes[] = { ARM::VST4LNd8Pseudo,
                                           ARM::VST4LNd16Pseudo,
                                           ARM::VST4LNd32Pseudo };
      static const uint16_t QOpcodes[] = { ARM::VST4LNq16Pseudo,
                                           ARM::VST4LNq32Pseudo };
      SelectVLDSTLane(N, false, false, 4, DOpcodes, QOpcodes);
      return;
    }
    }
    break;
  }

  case ISD::ATOMIC_CMP_SWAP:
    SelectCMP_SWAP(N);
    return;
  }

  SelectCode(N);
}

// Inspect a register string of the form
// cp<coprocessor>:<opc1>:c<CRn>:c<CRm>:<opc2> (32bit) or
// cp<coprocessor>:<opc1>:c<CRm> (64bit) inspect the fields of the string
// and obtain the integer operands from them, adding these operands to the
// provided vector.
static void getIntOperandsFromRegisterString(StringRef RegString,
                                             SelectionDAG *CurDAG,
                                             const SDLoc &DL,
                                             std::vector<SDValue> &Ops) {
  SmallVector<StringRef, 5> Fields;
  RegString.split(Fields, ':');

  if (Fields.size() > 1) {
    bool AllIntFields = true;

    for (StringRef Field : Fields) {
      // Need to trim out leading 'cp' characters and get the integer field.
      unsigned IntField;
      AllIntFields &= !Field.trim("CPcp").getAsInteger(10, IntField);
      Ops.push_back(CurDAG->getTargetConstant(IntField, DL, MVT::i32));
    }

    assert(AllIntFields &&
            "Unexpected non-integer value in special register string.");
  }
}

// Maps a Banked Register string to its mask value. The mask value returned is
// for use in the MRSbanked / MSRbanked instruction nodes as the Banked Register
// mask operand, which expresses which register is to be used, e.g. r8, and in
// which mode it is to be used, e.g. usr. Returns -1 to signify that the string
// was invalid.
static inline int getBankedRegisterMask(StringRef RegString) {
  auto TheReg = ARMBankedReg::lookupBankedRegByName(RegString.lower());
  if (!TheReg)
     return -1;
  return TheReg->Encoding;
}

// The flags here are common to those allowed for apsr in the A class cores and
// those allowed for the special registers in the M class cores. Returns a
// value representing which flags were present, -1 if invalid.
static inline int getMClassFlagsMask(StringRef Flags) {
  return StringSwitch<int>(Flags)
          .Case("", 0x2) // no flags means nzcvq for psr registers, and 0x2 is
                         // correct when flags are not permitted
          .Case("g", 0x1)
          .Case("nzcvq", 0x2)
          .Case("nzcvqg", 0x3)
          .Default(-1);
}

// Maps MClass special registers string to its value for use in the
// t2MRS_M/t2MSR_M instruction nodes as the SYSm value operand.
// Returns -1 to signify that the string was invalid.
static int getMClassRegisterMask(StringRef Reg, const ARMSubtarget *Subtarget) {
  auto TheReg = ARMSysReg::lookupMClassSysRegByName(Reg);
  const FeatureBitset &FeatureBits = Subtarget->getFeatureBits();
  if (!TheReg || !TheReg->hasRequiredFeatures(FeatureBits))
    return -1;
  return (int)(TheReg->Encoding & 0xFFF); // SYSm value
}

static int getARClassRegisterMask(StringRef Reg, StringRef Flags) {
  // The mask operand contains the special register (R Bit) in bit 4, whether
  // the register is spsr (R bit is 1) or one of cpsr/apsr (R bit is 0), and
  // bits 3-0 contains the fields to be accessed in the special register, set by
  // the flags provided with the register.
  int Mask = 0;
  if (Reg == "apsr") {
    // The flags permitted for apsr are the same flags that are allowed in
    // M class registers. We get the flag value and then shift the flags into
    // the correct place to combine with the mask.
    Mask = getMClassFlagsMask(Flags);
    if (Mask == -1)
      return -1;
    return Mask << 2;
  }

  if (Reg != "cpsr" && Reg != "spsr") {
    return -1;
  }

  // This is the same as if the flags were "fc"
  if (Flags.empty() || Flags == "all")
    return Mask | 0x9;

  // Inspect the supplied flags string and set the bits in the mask for
  // the relevant and valid flags allowed for cpsr and spsr.
  for (char Flag : Flags) {
    int FlagVal;
    switch (Flag) {
      case 'c':
        FlagVal = 0x1;
        break;
      case 'x':
        FlagVal = 0x2;
        break;
      case 's':
        FlagVal = 0x4;
        break;
      case 'f':
        FlagVal = 0x8;
        break;
      default:
        FlagVal = 0;
    }

    // This avoids allowing strings where the same flag bit appears twice.
    if (!FlagVal || (Mask & FlagVal))
      return -1;
    Mask |= FlagVal;
  }

  // If the register is spsr then we need to set the R bit.
  if (Reg == "spsr")
    Mask |= 0x10;

  return Mask;
}

// Lower the read_register intrinsic to ARM specific DAG nodes
// using the supplied metadata string to select the instruction node to use
// and the registers/masks to construct as operands for the node.
bool ARMDAGToDAGISel::tryReadRegister(SDNode *N){
  const MDNodeSDNode *MD = dyn_cast<MDNodeSDNode>(N->getOperand(1));
  const MDString *RegString = dyn_cast<MDString>(MD->getMD()->getOperand(0));
  bool IsThumb2 = Subtarget->isThumb2();
  SDLoc DL(N);

  std::vector<SDValue> Ops;
  getIntOperandsFromRegisterString(RegString->getString(), CurDAG, DL, Ops);

  if (!Ops.empty()) {
    // If the special register string was constructed of fields (as defined
    // in the ACLE) then need to lower to MRC node (32 bit) or
    // MRRC node(64 bit), we can make the distinction based on the number of
    // operands we have.
    unsigned Opcode;
    SmallVector<EVT, 3> ResTypes;
    if (Ops.size() == 5){
      Opcode = IsThumb2 ? ARM::t2MRC : ARM::MRC;
      ResTypes.append({ MVT::i32, MVT::Other });
    } else {
      assert(Ops.size() == 3 &&
              "Invalid number of fields in special register string.");
      Opcode = IsThumb2 ? ARM::t2MRRC : ARM::MRRC;
      ResTypes.append({ MVT::i32, MVT::i32, MVT::Other });
    }

    Ops.push_back(getAL(CurDAG, DL));
    Ops.push_back(CurDAG->getRegister(0, MVT::i32));
    Ops.push_back(N->getOperand(0));
    ReplaceNode(N, CurDAG->getMachineNode(Opcode, DL, ResTypes, Ops));
    return true;
  }

  std::string SpecialReg = RegString->getString().lower();

  int BankedReg = getBankedRegisterMask(SpecialReg);
  if (BankedReg != -1) {
    Ops = { CurDAG->getTargetConstant(BankedReg, DL, MVT::i32),
            getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(
        N, CurDAG->getMachineNode(IsThumb2 ? ARM::t2MRSbanked : ARM::MRSbanked,
                                  DL, MVT::i32, MVT::Other, Ops));
    return true;
  }

  // The VFP registers are read by creating SelectionDAG nodes with opcodes
  // corresponding to the register that is being read from. So we switch on the
  // string to find which opcode we need to use.
  unsigned Opcode = StringSwitch<unsigned>(SpecialReg)
                    .Case("fpscr", ARM::VMRS)
                    .Case("fpexc", ARM::VMRS_FPEXC)
                    .Case("fpsid", ARM::VMRS_FPSID)
                    .Case("mvfr0", ARM::VMRS_MVFR0)
                    .Case("mvfr1", ARM::VMRS_MVFR1)
                    .Case("mvfr2", ARM::VMRS_MVFR2)
                    .Case("fpinst", ARM::VMRS_FPINST)
                    .Case("fpinst2", ARM::VMRS_FPINST2)
                    .Default(0);

  // If an opcode was found then we can lower the read to a VFP instruction.
  if (Opcode) {
    if (!Subtarget->hasVFP2())
      return false;
    if (Opcode == ARM::VMRS_MVFR2 && !Subtarget->hasFPARMv8())
      return false;

    Ops = { getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(N,
                CurDAG->getMachineNode(Opcode, DL, MVT::i32, MVT::Other, Ops));
    return true;
  }

  // If the target is M Class then need to validate that the register string
  // is an acceptable value, so check that a mask can be constructed from the
  // string.
  if (Subtarget->isMClass()) {
    int SYSmValue = getMClassRegisterMask(SpecialReg, Subtarget);
    if (SYSmValue == -1)
      return false;

    SDValue Ops[] = { CurDAG->getTargetConstant(SYSmValue, DL, MVT::i32),
                      getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
                      N->getOperand(0) };
    ReplaceNode(
        N, CurDAG->getMachineNode(ARM::t2MRS_M, DL, MVT::i32, MVT::Other, Ops));
    return true;
  }

  // Here we know the target is not M Class so we need to check if it is one
  // of the remaining possible values which are apsr, cpsr or spsr.
  if (SpecialReg == "apsr" || SpecialReg == "cpsr") {
    Ops = { getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(N, CurDAG->getMachineNode(IsThumb2 ? ARM::t2MRS_AR : ARM::MRS,
                                          DL, MVT::i32, MVT::Other, Ops));
    return true;
  }

  if (SpecialReg == "spsr") {
    Ops = { getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(
        N, CurDAG->getMachineNode(IsThumb2 ? ARM::t2MRSsys_AR : ARM::MRSsys, DL,
                                  MVT::i32, MVT::Other, Ops));
    return true;
  }

  return false;
}

// Lower the write_register intrinsic to ARM specific DAG nodes
// using the supplied metadata string to select the instruction node to use
// and the registers/masks to use in the nodes
bool ARMDAGToDAGISel::tryWriteRegister(SDNode *N){
  const MDNodeSDNode *MD = dyn_cast<MDNodeSDNode>(N->getOperand(1));
  const MDString *RegString = dyn_cast<MDString>(MD->getMD()->getOperand(0));
  bool IsThumb2 = Subtarget->isThumb2();
  SDLoc DL(N);

  std::vector<SDValue> Ops;
  getIntOperandsFromRegisterString(RegString->getString(), CurDAG, DL, Ops);

  if (!Ops.empty()) {
    // If the special register string was constructed of fields (as defined
    // in the ACLE) then need to lower to MCR node (32 bit) or
    // MCRR node(64 bit), we can make the distinction based on the number of
    // operands we have.
    unsigned Opcode;
    if (Ops.size() == 5) {
      Opcode = IsThumb2 ? ARM::t2MCR : ARM::MCR;
      Ops.insert(Ops.begin()+2, N->getOperand(2));
    } else {
      assert(Ops.size() == 3 &&
              "Invalid number of fields in special register string.");
      Opcode = IsThumb2 ? ARM::t2MCRR : ARM::MCRR;
      SDValue WriteValue[] = { N->getOperand(2), N->getOperand(3) };
      Ops.insert(Ops.begin()+2, WriteValue, WriteValue+2);
    }

    Ops.push_back(getAL(CurDAG, DL));
    Ops.push_back(CurDAG->getRegister(0, MVT::i32));
    Ops.push_back(N->getOperand(0));

    ReplaceNode(N, CurDAG->getMachineNode(Opcode, DL, MVT::Other, Ops));
    return true;
  }

  std::string SpecialReg = RegString->getString().lower();
  int BankedReg = getBankedRegisterMask(SpecialReg);
  if (BankedReg != -1) {
    Ops = { CurDAG->getTargetConstant(BankedReg, DL, MVT::i32), N->getOperand(2),
            getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(
        N, CurDAG->getMachineNode(IsThumb2 ? ARM::t2MSRbanked : ARM::MSRbanked,
                                  DL, MVT::Other, Ops));
    return true;
  }

  // The VFP registers are written to by creating SelectionDAG nodes with
  // opcodes corresponding to the register that is being written. So we switch
  // on the string to find which opcode we need to use.
  unsigned Opcode = StringSwitch<unsigned>(SpecialReg)
                    .Case("fpscr", ARM::VMSR)
                    .Case("fpexc", ARM::VMSR_FPEXC)
                    .Case("fpsid", ARM::VMSR_FPSID)
                    .Case("fpinst", ARM::VMSR_FPINST)
                    .Case("fpinst2", ARM::VMSR_FPINST2)
                    .Default(0);

  if (Opcode) {
    if (!Subtarget->hasVFP2())
      return false;
    Ops = { N->getOperand(2), getAL(CurDAG, DL),
            CurDAG->getRegister(0, MVT::i32), N->getOperand(0) };
    ReplaceNode(N, CurDAG->getMachineNode(Opcode, DL, MVT::Other, Ops));
    return true;
  }

  std::pair<StringRef, StringRef> Fields;
  Fields = StringRef(SpecialReg).rsplit('_');
  std::string Reg = Fields.first.str();
  StringRef Flags = Fields.second;

  // If the target was M Class then need to validate the special register value
  // and retrieve the mask for use in the instruction node.
  if (Subtarget->isMClass()) {
    int SYSmValue = getMClassRegisterMask(SpecialReg, Subtarget);
    if (SYSmValue == -1)
      return false;

    SDValue Ops[] = { CurDAG->getTargetConstant(SYSmValue, DL, MVT::i32),
                      N->getOperand(2), getAL(CurDAG, DL),
                      CurDAG->getRegister(0, MVT::i32), N->getOperand(0) };
    ReplaceNode(N, CurDAG->getMachineNode(ARM::t2MSR_M, DL, MVT::Other, Ops));
    return true;
  }

  // We then check to see if a valid mask can be constructed for one of the
  // register string values permitted for the A and R class cores. These values
  // are apsr, spsr and cpsr; these are also valid on older cores.
  int Mask = getARClassRegisterMask(Reg, Flags);
  if (Mask != -1) {
    Ops = { CurDAG->getTargetConstant(Mask, DL, MVT::i32), N->getOperand(2),
            getAL(CurDAG, DL), CurDAG->getRegister(0, MVT::i32),
            N->getOperand(0) };
    ReplaceNode(N, CurDAG->getMachineNode(IsThumb2 ? ARM::t2MSR_AR : ARM::MSR,
                                          DL, MVT::Other, Ops));
    return true;
  }

  return false;
}

bool ARMDAGToDAGISel::tryInlineAsm(SDNode *N){
  std::vector<SDValue> AsmNodeOperands;
  unsigned Flag, Kind;
  bool Changed = false;
  unsigned NumOps = N->getNumOperands();

  // Normally, i64 data is bounded to two arbitrary GRPs for "%r" constraint.
  // However, some instrstions (e.g. ldrexd/strexd in ARM mode) require
  // (even/even+1) GPRs and use %n and %Hn to refer to the individual regs
  // respectively. Since there is no constraint to explicitly specify a
  // reg pair, we use GPRPair reg class for "%r" for 64-bit data. For Thumb,
  // the 64-bit data may be referred by H, Q, R modifiers, so we still pack
  // them into a GPRPair.

  SDLoc dl(N);
  SDValue Glue = N->getGluedNode() ? N->getOperand(NumOps-1)
                                   : SDValue(nullptr,0);

  SmallVector<bool, 8> OpChanged;
  // Glue node will be appended late.
  for(unsigned i = 0, e = N->getGluedNode() ? NumOps - 1 : NumOps; i < e; ++i) {
    SDValue op = N->getOperand(i);
    AsmNodeOperands.push_back(op);

    if (i < InlineAsm::Op_FirstOperand)
      continue;

    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(i))) {
      Flag = C->getZExtValue();
      Kind = InlineAsm::getKind(Flag);
    }
    else
      continue;

    // Immediate operands to inline asm in the SelectionDAG are modeled with
    // two operands. The first is a constant of value InlineAsm::Kind_Imm, and
    // the second is a constant with the value of the immediate. If we get here
    // and we have a Kind_Imm, skip the next operand, and continue.
    if (Kind == InlineAsm::Kind_Imm) {
      SDValue op = N->getOperand(++i);
      AsmNodeOperands.push_back(op);
      continue;
    }

    unsigned NumRegs = InlineAsm::getNumOperandRegisters(Flag);
    if (NumRegs)
      OpChanged.push_back(false);

    unsigned DefIdx = 0;
    bool IsTiedToChangedOp = false;
    // If it's a use that is tied with a previous def, it has no
    // reg class constraint.
    if (Changed && InlineAsm::isUseOperandTiedToDef(Flag, DefIdx))
      IsTiedToChangedOp = OpChanged[DefIdx];

    // Memory operands to inline asm in the SelectionDAG are modeled with two
    // operands: a constant of value InlineAsm::Kind_Mem followed by the input
    // operand. If we get here and we have a Kind_Mem, skip the next operand (so
    // it doesn't get misinterpreted), and continue. We do this here because
    // it's important to update the OpChanged array correctly before moving on.
    if (Kind == InlineAsm::Kind_Mem) {
      SDValue op = N->getOperand(++i);
      AsmNodeOperands.push_back(op);
      continue;
    }

    if (Kind != InlineAsm::Kind_RegUse && Kind != InlineAsm::Kind_RegDef
        && Kind != InlineAsm::Kind_RegDefEarlyClobber)
      continue;

    unsigned RC;
    bool HasRC = InlineAsm::hasRegClassConstraint(Flag, RC);
    if ((!IsTiedToChangedOp && (!HasRC || RC != ARM::GPRRegClassID))
        || NumRegs != 2)
      continue;

    assert((i+2 < NumOps) && "Invalid number of operands in inline asm");
    SDValue V0 = N->getOperand(i+1);
    SDValue V1 = N->getOperand(i+2);
    unsigned Reg0 = cast<RegisterSDNode>(V0)->getReg();
    unsigned Reg1 = cast<RegisterSDNode>(V1)->getReg();
    SDValue PairedReg;
    MachineRegisterInfo &MRI = MF->getRegInfo();

    if (Kind == InlineAsm::Kind_RegDef ||
        Kind == InlineAsm::Kind_RegDefEarlyClobber) {
      // Replace the two GPRs with 1 GPRPair and copy values from GPRPair to
      // the original GPRs.

      unsigned GPVR = MRI.createVirtualRegister(&ARM::GPRPairRegClass);
      PairedReg = CurDAG->getRegister(GPVR, MVT::Untyped);
      SDValue Chain = SDValue(N,0);

      SDNode *GU = N->getGluedUser();
      SDValue RegCopy = CurDAG->getCopyFromReg(Chain, dl, GPVR, MVT::Untyped,
                                               Chain.getValue(1));

      // Extract values from a GPRPair reg and copy to the original GPR reg.
      SDValue Sub0 = CurDAG->getTargetExtractSubreg(ARM::gsub_0, dl, MVT::i32,
                                                    RegCopy);
      SDValue Sub1 = CurDAG->getTargetExtractSubreg(ARM::gsub_1, dl, MVT::i32,
                                                    RegCopy);
      SDValue T0 = CurDAG->getCopyToReg(Sub0, dl, Reg0, Sub0,
                                        RegCopy.getValue(1));
      SDValue T1 = CurDAG->getCopyToReg(Sub1, dl, Reg1, Sub1, T0.getValue(1));

      // Update the original glue user.
      std::vector<SDValue> Ops(GU->op_begin(), GU->op_end()-1);
      Ops.push_back(T1.getValue(1));
      CurDAG->UpdateNodeOperands(GU, Ops);
    }
    else {
      // For Kind  == InlineAsm::Kind_RegUse, we first copy two GPRs into a
      // GPRPair and then pass the GPRPair to the inline asm.
      SDValue Chain = AsmNodeOperands[InlineAsm::Op_InputChain];

      // As REG_SEQ doesn't take RegisterSDNode, we copy them first.
      SDValue T0 = CurDAG->getCopyFromReg(Chain, dl, Reg0, MVT::i32,
                                          Chain.getValue(1));
      SDValue T1 = CurDAG->getCopyFromReg(Chain, dl, Reg1, MVT::i32,
                                          T0.getValue(1));
      SDValue Pair = SDValue(createGPRPairNode(MVT::Untyped, T0, T1), 0);

      // Copy REG_SEQ into a GPRPair-typed VR and replace the original two
      // i32 VRs of inline asm with it.
      unsigned GPVR = MRI.createVirtualRegister(&ARM::GPRPairRegClass);
      PairedReg = CurDAG->getRegister(GPVR, MVT::Untyped);
      Chain = CurDAG->getCopyToReg(T1, dl, GPVR, Pair, T1.getValue(1));

      AsmNodeOperands[InlineAsm::Op_InputChain] = Chain;
      Glue = Chain.getValue(1);
    }

    Changed = true;

    if(PairedReg.getNode()) {
      OpChanged[OpChanged.size() -1 ] = true;
      Flag = InlineAsm::getFlagWord(Kind, 1 /* RegNum*/);
      if (IsTiedToChangedOp)
        Flag = InlineAsm::getFlagWordForMatchingOp(Flag, DefIdx);
      else
        Flag = InlineAsm::getFlagWordForRegClass(Flag, ARM::GPRPairRegClassID);
      // Replace the current flag.
      AsmNodeOperands[AsmNodeOperands.size() -1] = CurDAG->getTargetConstant(
          Flag, dl, MVT::i32);
      // Add the new register node and skip the original two GPRs.
      AsmNodeOperands.push_back(PairedReg);
      // Skip the next two GPRs.
      i += 2;
    }
  }

  if (Glue.getNode())
    AsmNodeOperands.push_back(Glue);
  if (!Changed)
    return false;

  SDValue New = CurDAG->getNode(ISD::INLINEASM, SDLoc(N),
      CurDAG->getVTList(MVT::Other, MVT::Glue), AsmNodeOperands);
  New->setNodeId(-1);
  ReplaceNode(N, New.getNode());
  return true;
}


bool ARMDAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintID,
                             std::vector<SDValue> &OutOps) {
  switch(ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::Constraint_i:
    // FIXME: It seems strange that 'i' is needed here since it's supposed to
    //        be an immediate and not a memory constraint.
    LLVM_FALLTHROUGH;
  case InlineAsm::Constraint_m:
  case InlineAsm::Constraint_o:
  case InlineAsm::Constraint_Q:
  case InlineAsm::Constraint_Um:
  case InlineAsm::Constraint_Un:
  case InlineAsm::Constraint_Uq:
  case InlineAsm::Constraint_Us:
  case InlineAsm::Constraint_Ut:
  case InlineAsm::Constraint_Uv:
  case InlineAsm::Constraint_Uy:
    // Require the address to be in a register.  That is safe for all ARM
    // variants and it is hard to do anything much smarter without knowing
    // how the operand is used.
    OutOps.push_back(Op);
    return false;
  }
  return true;
}

/// createARMISelDag - This pass converts a legalized DAG into a
/// ARM-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createARMISelDag(ARMBaseTargetMachine &TM,
                                     CodeGenOpt::Level OptLevel) {
  return new ARMDAGToDAGISel(TM, OptLevel);
}
