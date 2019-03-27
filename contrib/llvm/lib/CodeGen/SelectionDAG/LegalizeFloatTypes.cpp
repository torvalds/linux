//===-------- LegalizeFloatTypes.cpp - Legalization of float types --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements float type expansion and softening for LegalizeTypes.
// Softening is the act of turning a computation in an illegal floating point
// type into a computation in an integer type of the same size; also known as
// "soft float".  For example, turning f32 arithmetic into operations using i32.
// The resulting integer value is the same as what you would get by performing
// the floating point operation and bitcasting the result to the integer type.
// Expansion is the act of changing a computation in an illegal type to be a
// computation in two identical registers of a smaller type.  For example,
// implementing ppcf128 arithmetic in two f64 registers.
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "legalize-types"

/// GetFPLibCall - Return the right libcall for the given floating point type.
static RTLIB::Libcall GetFPLibCall(EVT VT,
                                   RTLIB::Libcall Call_F32,
                                   RTLIB::Libcall Call_F64,
                                   RTLIB::Libcall Call_F80,
                                   RTLIB::Libcall Call_F128,
                                   RTLIB::Libcall Call_PPCF128) {
  return
    VT == MVT::f32 ? Call_F32 :
    VT == MVT::f64 ? Call_F64 :
    VT == MVT::f80 ? Call_F80 :
    VT == MVT::f128 ? Call_F128 :
    VT == MVT::ppcf128 ? Call_PPCF128 :
    RTLIB::UNKNOWN_LIBCALL;
}

//===----------------------------------------------------------------------===//
//  Convert Float Results to Integer for Non-HW-supported Operations.
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::SoftenFloatResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Soften float result " << ResNo << ": "; N->dump(&DAG);
             dbgs() << "\n");
  SDValue R = SDValue();

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SoftenFloatResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to soften the result of this operator!");

    case ISD::Register:
    case ISD::CopyFromReg:
    case ISD::CopyToReg:
      assert(isLegalInHWReg(N->getValueType(ResNo)) &&
             "Unsupported SoftenFloatRes opcode!");
      // Only when isLegalInHWReg, we can skip check of the operands.
      R = SDValue(N, ResNo);
      break;
    case ISD::MERGE_VALUES:R = SoftenFloatRes_MERGE_VALUES(N, ResNo); break;
    case ISD::BITCAST:     R = SoftenFloatRes_BITCAST(N, ResNo); break;
    case ISD::BUILD_PAIR:  R = SoftenFloatRes_BUILD_PAIR(N); break;
    case ISD::ConstantFP:  R = SoftenFloatRes_ConstantFP(N, ResNo); break;
    case ISD::EXTRACT_VECTOR_ELT:
      R = SoftenFloatRes_EXTRACT_VECTOR_ELT(N, ResNo); break;
    case ISD::FABS:        R = SoftenFloatRes_FABS(N, ResNo); break;
    case ISD::FMINNUM:     R = SoftenFloatRes_FMINNUM(N); break;
    case ISD::FMAXNUM:     R = SoftenFloatRes_FMAXNUM(N); break;
    case ISD::FADD:        R = SoftenFloatRes_FADD(N); break;
    case ISD::FCEIL:       R = SoftenFloatRes_FCEIL(N); break;
    case ISD::FCOPYSIGN:   R = SoftenFloatRes_FCOPYSIGN(N, ResNo); break;
    case ISD::FCOS:        R = SoftenFloatRes_FCOS(N); break;
    case ISD::FDIV:        R = SoftenFloatRes_FDIV(N); break;
    case ISD::FEXP:        R = SoftenFloatRes_FEXP(N); break;
    case ISD::FEXP2:       R = SoftenFloatRes_FEXP2(N); break;
    case ISD::FFLOOR:      R = SoftenFloatRes_FFLOOR(N); break;
    case ISD::FLOG:        R = SoftenFloatRes_FLOG(N); break;
    case ISD::FLOG2:       R = SoftenFloatRes_FLOG2(N); break;
    case ISD::FLOG10:      R = SoftenFloatRes_FLOG10(N); break;
    case ISD::FMA:         R = SoftenFloatRes_FMA(N); break;
    case ISD::FMUL:        R = SoftenFloatRes_FMUL(N); break;
    case ISD::FNEARBYINT:  R = SoftenFloatRes_FNEARBYINT(N); break;
    case ISD::FNEG:        R = SoftenFloatRes_FNEG(N, ResNo); break;
    case ISD::FP_EXTEND:   R = SoftenFloatRes_FP_EXTEND(N); break;
    case ISD::FP_ROUND:    R = SoftenFloatRes_FP_ROUND(N); break;
    case ISD::FP16_TO_FP:  R = SoftenFloatRes_FP16_TO_FP(N); break;
    case ISD::FPOW:        R = SoftenFloatRes_FPOW(N); break;
    case ISD::FPOWI:       R = SoftenFloatRes_FPOWI(N); break;
    case ISD::FREM:        R = SoftenFloatRes_FREM(N); break;
    case ISD::FRINT:       R = SoftenFloatRes_FRINT(N); break;
    case ISD::FROUND:      R = SoftenFloatRes_FROUND(N); break;
    case ISD::FSIN:        R = SoftenFloatRes_FSIN(N); break;
    case ISD::FSQRT:       R = SoftenFloatRes_FSQRT(N); break;
    case ISD::FSUB:        R = SoftenFloatRes_FSUB(N); break;
    case ISD::FTRUNC:      R = SoftenFloatRes_FTRUNC(N); break;
    case ISD::LOAD:        R = SoftenFloatRes_LOAD(N, ResNo); break;
    case ISD::SELECT:      R = SoftenFloatRes_SELECT(N, ResNo); break;
    case ISD::SELECT_CC:   R = SoftenFloatRes_SELECT_CC(N, ResNo); break;
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:  R = SoftenFloatRes_XINT_TO_FP(N); break;
    case ISD::UNDEF:       R = SoftenFloatRes_UNDEF(N); break;
    case ISD::VAARG:       R = SoftenFloatRes_VAARG(N); break;
  }

  if (R.getNode() && R.getNode() != N) {
    SetSoftenedFloat(SDValue(N, ResNo), R);
    // Return true only if the node is changed, assuming that the operands
    // are also converted when necessary.
    return true;
  }

  // Otherwise, return false to tell caller to scan operands.
  return false;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_BITCAST(SDNode *N, unsigned ResNo) {
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  return BitConvertToInteger(N->getOperand(0));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_MERGE_VALUES(SDNode *N,
                                                      unsigned ResNo) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  return BitConvertToInteger(Op);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_BUILD_PAIR(SDNode *N) {
  // Convert the inputs to integers, and build a new pair out of them.
  return DAG.getNode(ISD::BUILD_PAIR, SDLoc(N),
                     TLI.getTypeToTransformTo(*DAG.getContext(),
                                              N->getValueType(0)),
                     BitConvertToInteger(N->getOperand(0)),
                     BitConvertToInteger(N->getOperand(1)));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_ConstantFP(SDNode *N, unsigned ResNo) {
  // When LegalInHWReg, we can load better from the constant pool.
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  ConstantFPSDNode *CN = cast<ConstantFPSDNode>(N);
  // In ppcf128, the high 64 bits are always first in memory regardless
  // of Endianness. LLVM's APFloat representation is not Endian sensitive,
  // and so always converts into a 128-bit APInt in a non-Endian-sensitive
  // way. However, APInt's are serialized in an Endian-sensitive fashion,
  // so on big-Endian targets, the two doubles are output in the wrong
  // order. Fix this by manually flipping the order of the high 64 bits
  // and the low 64 bits here.
  if (DAG.getDataLayout().isBigEndian() &&
      CN->getValueType(0).getSimpleVT() == llvm::MVT::ppcf128) {
    uint64_t words[2] = { CN->getValueAPF().bitcastToAPInt().getRawData()[1],
                          CN->getValueAPF().bitcastToAPInt().getRawData()[0] };
    APInt Val(128, words);
    return DAG.getConstant(Val, SDLoc(CN),
                           TLI.getTypeToTransformTo(*DAG.getContext(),
                                                    CN->getValueType(0)));
  } else {
    return DAG.getConstant(CN->getValueAPF().bitcastToAPInt(), SDLoc(CN),
                           TLI.getTypeToTransformTo(*DAG.getContext(),
                                                    CN->getValueType(0)));
  }
}

SDValue DAGTypeLegalizer::SoftenFloatRes_EXTRACT_VECTOR_ELT(SDNode *N, unsigned ResNo) {
  // When LegalInHWReg, keep the extracted value in register.
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  SDValue NewOp = BitConvertVectorToIntegerVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     NewOp.getValueType().getVectorElementType(),
                     NewOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FABS(SDNode *N, unsigned ResNo) {
  // When LegalInHWReg, FABS can be implemented as native bitwise operations.
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned Size = NVT.getSizeInBits();

  // Mask = ~(1 << (Size-1))
  APInt API = APInt::getAllOnesValue(Size);
  API.clearBit(Size - 1);
  SDValue Mask = DAG.getConstant(API, SDLoc(N), NVT);
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return DAG.getNode(ISD::AND, SDLoc(N), NVT, Op, Mask);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMINNUM(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::FMIN_F32,
                                           RTLIB::FMIN_F64,
                                           RTLIB::FMIN_F80,
                                           RTLIB::FMIN_F128,
                                           RTLIB::FMIN_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMAXNUM(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::FMAX_F32,
                                           RTLIB::FMAX_F64,
                                           RTLIB::FMAX_F80,
                                           RTLIB::FMAX_F128,
                                           RTLIB::FMAX_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FADD(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::ADD_F32,
                                           RTLIB::ADD_F64,
                                           RTLIB::ADD_F80,
                                           RTLIB::ADD_F128,
                                           RTLIB::ADD_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCEIL(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::CEIL_F32,
                                           RTLIB::CEIL_F64,
                                           RTLIB::CEIL_F80,
                                           RTLIB::CEIL_F128,
                                           RTLIB::CEIL_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCOPYSIGN(SDNode *N, unsigned ResNo) {
  // When LegalInHWReg, FCOPYSIGN can be implemented as native bitwise operations.
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  SDValue LHS = GetSoftenedFloat(N->getOperand(0));
  SDValue RHS = BitConvertToInteger(N->getOperand(1));
  SDLoc dl(N);

  EVT LVT = LHS.getValueType();
  EVT RVT = RHS.getValueType();

  unsigned LSize = LVT.getSizeInBits();
  unsigned RSize = RVT.getSizeInBits();

  // First get the sign bit of second operand.
  SDValue SignBit = DAG.getNode(
      ISD::SHL, dl, RVT, DAG.getConstant(1, dl, RVT),
      DAG.getConstant(RSize - 1, dl,
                      TLI.getShiftAmountTy(RVT, DAG.getDataLayout())));
  SignBit = DAG.getNode(ISD::AND, dl, RVT, RHS, SignBit);

  // Shift right or sign-extend it if the two operands have different types.
  int SizeDiff = RVT.getSizeInBits() - LVT.getSizeInBits();
  if (SizeDiff > 0) {
    SignBit =
        DAG.getNode(ISD::SRL, dl, RVT, SignBit,
                    DAG.getConstant(SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
    SignBit = DAG.getNode(ISD::TRUNCATE, dl, LVT, SignBit);
  } else if (SizeDiff < 0) {
    SignBit = DAG.getNode(ISD::ANY_EXTEND, dl, LVT, SignBit);
    SignBit =
        DAG.getNode(ISD::SHL, dl, LVT, SignBit,
                    DAG.getConstant(-SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
  }

  // Clear the sign bit of the first operand.
  SDValue Mask = DAG.getNode(
      ISD::SHL, dl, LVT, DAG.getConstant(1, dl, LVT),
      DAG.getConstant(LSize - 1, dl,
                      TLI.getShiftAmountTy(LVT, DAG.getDataLayout())));
  Mask = DAG.getNode(ISD::SUB, dl, LVT, Mask, DAG.getConstant(1, dl, LVT));
  LHS = DAG.getNode(ISD::AND, dl, LVT, LHS, Mask);

  // Or the value with the sign bit.
  return DAG.getNode(ISD::OR, dl, LVT, LHS, SignBit);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCOS(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::COS_F32,
                                           RTLIB::COS_F64,
                                           RTLIB::COS_F80,
                                           RTLIB::COS_F128,
                                           RTLIB::COS_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FDIV(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::DIV_F32,
                                           RTLIB::DIV_F64,
                                           RTLIB::DIV_F80,
                                           RTLIB::DIV_F128,
                                           RTLIB::DIV_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FEXP(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::EXP_F32,
                                           RTLIB::EXP_F64,
                                           RTLIB::EXP_F80,
                                           RTLIB::EXP_F128,
                                           RTLIB::EXP_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FEXP2(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::EXP2_F32,
                                           RTLIB::EXP2_F64,
                                           RTLIB::EXP2_F80,
                                           RTLIB::EXP2_F128,
                                           RTLIB::EXP2_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FFLOOR(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::FLOOR_F32,
                                           RTLIB::FLOOR_F64,
                                           RTLIB::FLOOR_F80,
                                           RTLIB::FLOOR_F128,
                                           RTLIB::FLOOR_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::LOG_F32,
                                           RTLIB::LOG_F64,
                                           RTLIB::LOG_F80,
                                           RTLIB::LOG_F128,
                                           RTLIB::LOG_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG2(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::LOG2_F32,
                                           RTLIB::LOG2_F64,
                                           RTLIB::LOG2_F80,
                                           RTLIB::LOG2_F128,
                                           RTLIB::LOG2_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG10(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::LOG10_F32,
                                           RTLIB::LOG10_F64,
                                           RTLIB::LOG10_F80,
                                           RTLIB::LOG10_F128,
                                           RTLIB::LOG10_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMA(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[3] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)),
                     GetSoftenedFloat(N->getOperand(2)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::FMA_F32,
                                           RTLIB::FMA_F64,
                                           RTLIB::FMA_F80,
                                           RTLIB::FMA_F128,
                                           RTLIB::FMA_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMUL(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::MUL_F32,
                                           RTLIB::MUL_F64,
                                           RTLIB::MUL_F80,
                                           RTLIB::MUL_F128,
                                           RTLIB::MUL_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FNEARBYINT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::NEARBYINT_F32,
                                           RTLIB::NEARBYINT_F64,
                                           RTLIB::NEARBYINT_F80,
                                           RTLIB::NEARBYINT_F128,
                                           RTLIB::NEARBYINT_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FNEG(SDNode *N, unsigned ResNo) {
  // When LegalInHWReg, FNEG can be implemented as native bitwise operations.
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  // Expand Y = FNEG(X) -> Y = SUB -0.0, X
  SDValue Ops[2] = { DAG.getConstantFP(-0.0, dl, N->getValueType(0)),
                     GetSoftenedFloat(N->getOperand(0)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::SUB_F32,
                                           RTLIB::SUB_F64,
                                           RTLIB::SUB_F80,
                                           RTLIB::SUB_F128,
                                           RTLIB::SUB_PPCF128),
                         NVT, Ops, false, dl).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FP_EXTEND(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = N->getOperand(0);

  // There's only a libcall for f16 -> f32, so proceed in two stages. Also, it's
  // entirely possible for both f16 and f32 to be legal, so use the fully
  // hard-float FP_EXTEND rather than FP16_TO_FP.
  if (Op.getValueType() == MVT::f16 && N->getValueType(0) != MVT::f32) {
    Op = DAG.getNode(ISD::FP_EXTEND, SDLoc(N), MVT::f32, Op);
    if (getTypeAction(MVT::f32) == TargetLowering::TypeSoftenFloat)
      AddToWorklist(Op.getNode());
  }

  if (getTypeAction(Op.getValueType()) == TargetLowering::TypePromoteFloat) {
    Op = GetPromotedFloat(Op);
    // If the promotion did the FP_EXTEND to the destination type for us,
    // there's nothing left to do here.
    if (Op.getValueType() == N->getValueType(0)) {
      return BitConvertToInteger(Op);
    }
  }

  RTLIB::Libcall LC = RTLIB::getFPEXT(Op.getValueType(), N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_EXTEND!");
  return TLI.makeLibCall(DAG, LC, NVT, Op, false, SDLoc(N)).first;
}

// FIXME: Should we just use 'normal' FP_EXTEND / FP_TRUNC instead of special
// nodes?
SDValue DAGTypeLegalizer::SoftenFloatRes_FP16_TO_FP(SDNode *N) {
  EVT MidVT = TLI.getTypeToTransformTo(*DAG.getContext(), MVT::f32);
  SDValue Op = N->getOperand(0);
  SDValue Res32 = TLI.makeLibCall(DAG, RTLIB::FPEXT_F16_F32, MidVT, Op,
                                  false, SDLoc(N)).first;
  if (N->getValueType(0) == MVT::f32)
    return Res32;

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  RTLIB::Libcall LC = RTLIB::getFPEXT(MVT::f32, N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_EXTEND!");
  return TLI.makeLibCall(DAG, LC, NVT, Res32, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FP_ROUND(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = N->getOperand(0);
  if (N->getValueType(0) == MVT::f16) {
    // Semi-soften first, to FP_TO_FP16, so that targets which support f16 as a
    // storage-only type get a chance to select things.
    return DAG.getNode(ISD::FP_TO_FP16, SDLoc(N), NVT, Op);
  }

  RTLIB::Libcall LC = RTLIB::getFPROUND(Op.getValueType(), N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_ROUND!");
  return TLI.makeLibCall(DAG, LC, NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FPOW(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::POW_F32,
                                           RTLIB::POW_F64,
                                           RTLIB::POW_F80,
                                           RTLIB::POW_F128,
                                           RTLIB::POW_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FPOWI(SDNode *N) {
  assert(N->getOperand(1).getValueType() == MVT::i32 &&
         "Unsupported power type!");
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)), N->getOperand(1) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::POWI_F32,
                                           RTLIB::POWI_F64,
                                           RTLIB::POWI_F80,
                                           RTLIB::POWI_F128,
                                           RTLIB::POWI_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FREM(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::REM_F32,
                                           RTLIB::REM_F64,
                                           RTLIB::REM_F80,
                                           RTLIB::REM_F128,
                                           RTLIB::REM_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FRINT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::RINT_F32,
                                           RTLIB::RINT_F64,
                                           RTLIB::RINT_F80,
                                           RTLIB::RINT_F128,
                                           RTLIB::RINT_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FROUND(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::ROUND_F32,
                                           RTLIB::ROUND_F64,
                                           RTLIB::ROUND_F80,
                                           RTLIB::ROUND_F128,
                                           RTLIB::ROUND_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSIN(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::SIN_F32,
                                           RTLIB::SIN_F64,
                                           RTLIB::SIN_F80,
                                           RTLIB::SIN_F128,
                                           RTLIB::SIN_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSQRT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::SQRT_F32,
                                           RTLIB::SQRT_F64,
                                           RTLIB::SQRT_F80,
                                           RTLIB::SQRT_F128,
                                           RTLIB::SQRT_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSUB(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0)),
                     GetSoftenedFloat(N->getOperand(1)) };
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::SUB_F32,
                                           RTLIB::SUB_F64,
                                           RTLIB::SUB_F80,
                                           RTLIB::SUB_F128,
                                           RTLIB::SUB_PPCF128),
                         NVT, Ops, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FTRUNC(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  if (N->getValueType(0) == MVT::f16)
    return DAG.getNode(ISD::FP_TO_FP16, SDLoc(N), NVT, N->getOperand(0));

  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                           RTLIB::TRUNC_F32,
                                           RTLIB::TRUNC_F64,
                                           RTLIB::TRUNC_F80,
                                           RTLIB::TRUNC_F128,
                                           RTLIB::TRUNC_PPCF128),
                         NVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_LOAD(SDNode *N, unsigned ResNo) {
  bool LegalInHWReg = isLegalInHWReg(N->getValueType(ResNo));
  LoadSDNode *L = cast<LoadSDNode>(N);
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  auto MMOFlags =
      L->getMemOperand()->getFlags() &
      ~(MachineMemOperand::MOInvariant | MachineMemOperand::MODereferenceable);
  SDValue NewL;
  if (L->getExtensionType() == ISD::NON_EXTLOAD) {
    NewL = DAG.getLoad(L->getAddressingMode(), L->getExtensionType(), NVT, dl,
                       L->getChain(), L->getBasePtr(), L->getOffset(),
                       L->getPointerInfo(), NVT, L->getAlignment(), MMOFlags,
                       L->getAAInfo());
    // Legalized the chain result - switch anything that used the old chain to
    // use the new one.
    if (N != NewL.getValue(1).getNode())
      ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
    return NewL;
  }

  // Do a non-extending load followed by FP_EXTEND.
  NewL = DAG.getLoad(L->getAddressingMode(), ISD::NON_EXTLOAD, L->getMemoryVT(),
                     dl, L->getChain(), L->getBasePtr(), L->getOffset(),
                     L->getPointerInfo(), L->getMemoryVT(), L->getAlignment(),
                     MMOFlags, L->getAAInfo());
  // Legalized the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
  auto ExtendNode = DAG.getNode(ISD::FP_EXTEND, dl, VT, NewL);
  if (LegalInHWReg)
    return ExtendNode;
  return BitConvertToInteger(ExtendNode);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_SELECT(SDNode *N, unsigned ResNo) {
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  SDValue LHS = GetSoftenedFloat(N->getOperand(1));
  SDValue RHS = GetSoftenedFloat(N->getOperand(2));
  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), N->getOperand(0), LHS, RHS);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_SELECT_CC(SDNode *N, unsigned ResNo) {
  if (isLegalInHWReg(N->getValueType(ResNo)))
    return SDValue(N, ResNo);
  SDValue LHS = GetSoftenedFloat(N->getOperand(2));
  SDValue RHS = GetSoftenedFloat(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     LHS.getValueType(), N->getOperand(0),
                     N->getOperand(1), LHS, RHS, N->getOperand(4));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(TLI.getTypeToTransformTo(*DAG.getContext(),
                                               N->getValueType(0)));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_VAARG(SDNode *N) {
  SDValue Chain = N->getOperand(0); // Get the chain.
  SDValue Ptr = N->getOperand(1); // Get the pointer.
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  SDValue NewVAARG;
  NewVAARG = DAG.getVAArg(NVT, dl, Chain, Ptr, N->getOperand(2),
                          N->getConstantOperandVal(3));

  // Legalized the chain result - switch anything that used the old chain to
  // use the new one.
  if (N != NewVAARG.getValue(1).getNode())
    ReplaceValueWith(SDValue(N, 1), NewVAARG.getValue(1));
  return NewVAARG;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_XINT_TO_FP(SDNode *N) {
  bool Signed = N->getOpcode() == ISD::SINT_TO_FP;
  EVT SVT = N->getOperand(0).getValueType();
  EVT RVT = N->getValueType(0);
  EVT NVT = EVT();
  SDLoc dl(N);

  // If the input is not legal, eg: i1 -> fp, then it needs to be promoted to
  // a larger type, eg: i8 -> fp.  Even if it is legal, no libcall may exactly
  // match.  Look for an appropriate libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  for (unsigned t = MVT::FIRST_INTEGER_VALUETYPE;
       t <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL; ++t) {
    NVT = (MVT::SimpleValueType)t;
    // The source needs to big enough to hold the operand.
    if (NVT.bitsGE(SVT))
      LC = Signed ? RTLIB::getSINTTOFP(NVT, RVT):RTLIB::getUINTTOFP (NVT, RVT);
  }
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported XINT_TO_FP!");

  // Sign/zero extend the argument if the libcall takes a larger type.
  SDValue Op = DAG.getNode(Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                           NVT, N->getOperand(0));
  return TLI.makeLibCall(DAG, LC,
                         TLI.getTypeToTransformTo(*DAG.getContext(), RVT),
                         Op, Signed, dl).first;
}


//===----------------------------------------------------------------------===//
//  Convert Float Operand to Integer for Non-HW-supported Operations.
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::SoftenFloatOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Soften float operand " << OpNo << ": "; N->dump(&DAG);
             dbgs() << "\n");
  SDValue Res = SDValue();

  switch (N->getOpcode()) {
  default:
    if (CanSkipSoftenFloatOperand(N, OpNo))
      return false;
#ifndef NDEBUG
    dbgs() << "SoftenFloatOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to soften this operator's operand!");

  case ISD::BITCAST:     Res = SoftenFloatOp_BITCAST(N); break;
  case ISD::CopyToReg:   Res = SoftenFloatOp_COPY_TO_REG(N); break;
  case ISD::BR_CC:       Res = SoftenFloatOp_BR_CC(N); break;
  case ISD::FABS:        Res = SoftenFloatOp_FABS(N); break;
  case ISD::FCOPYSIGN:   Res = SoftenFloatOp_FCOPYSIGN(N); break;
  case ISD::FNEG:        Res = SoftenFloatOp_FNEG(N); break;
  case ISD::FP_EXTEND:   Res = SoftenFloatOp_FP_EXTEND(N); break;
  case ISD::FP_TO_FP16:  // Same as FP_ROUND for softening purposes
  case ISD::FP_ROUND:    Res = SoftenFloatOp_FP_ROUND(N); break;
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:  Res = SoftenFloatOp_FP_TO_XINT(N); break;
  case ISD::SELECT:      Res = SoftenFloatOp_SELECT(N); break;
  case ISD::SELECT_CC:   Res = SoftenFloatOp_SELECT_CC(N); break;
  case ISD::SETCC:       Res = SoftenFloatOp_SETCC(N); break;
  case ISD::STORE:
    Res = SoftenFloatOp_STORE(N, OpNo);
    // Do not try to analyze or soften this node again if the value is
    // or can be held in a register. In that case, Res.getNode() should
    // be equal to N.
    if (Res.getNode() == N &&
        isLegalInHWReg(N->getOperand(OpNo).getValueType()))
      return false;
    // Otherwise, we need to reanalyze and lower the new Res nodes.
    break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this to re-analyze.
  if (Res.getNode() == N)
    return true;

  assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

bool DAGTypeLegalizer::CanSkipSoftenFloatOperand(SDNode *N, unsigned OpNo) {
  if (!isLegalInHWReg(N->getOperand(OpNo).getValueType()))
    return false;

  // When the operand type can be kept in registers there is nothing to do for
  // the following opcodes.
  switch (N->getOperand(OpNo).getOpcode()) {
    case ISD::BITCAST:
    case ISD::ConstantFP:
    case ISD::CopyFromReg:
    case ISD::CopyToReg:
    case ISD::FABS:
    case ISD::FCOPYSIGN:
    case ISD::FNEG:
    case ISD::Register:
    case ISD::SELECT:
    case ISD::SELECT_CC:
      return true;
  }

  switch (N->getOpcode()) {
    case ISD::ConstantFP:  // Leaf node.
    case ISD::CopyFromReg: // Operand is a register that we know to be left
                           // unchanged by SoftenFloatResult().
    case ISD::Register:    // Leaf node.
      return true;
  }
  return false;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_BITCAST(SDNode *N) {
  return DAG.getNode(ISD::BITCAST, SDLoc(N), N->getValueType(0),
                     GetSoftenedFloat(N->getOperand(0)));
}

SDValue DAGTypeLegalizer::SoftenFloatOp_COPY_TO_REG(SDNode *N) {
  SDValue Op1 = GetSoftenedFloat(N->getOperand(1));
  SDValue Op2 = GetSoftenedFloat(N->getOperand(2));

  if (Op1 == N->getOperand(1) && Op2 == N->getOperand(2))
    return SDValue();

  if (N->getNumOperands() == 3)
    return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Op1, Op2), 0);

  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Op1, Op2,
                                        N->getOperand(3)),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FP_EXTEND(SDNode *N) {
  // If we get here, the result must be legal but the source illegal.
  EVT SVT = N->getOperand(0).getValueType();
  EVT RVT = N->getValueType(0);
  SDValue Op = GetSoftenedFloat(N->getOperand(0));

  if (SVT == MVT::f16)
    return DAG.getNode(ISD::FP16_TO_FP, SDLoc(N), RVT, Op);

  RTLIB::Libcall LC = RTLIB::getFPEXT(SVT, RVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_EXTEND libcall");

  return TLI.makeLibCall(DAG, LC, RVT, Op, false, SDLoc(N)).first;
}


SDValue DAGTypeLegalizer::SoftenFloatOp_FP_ROUND(SDNode *N) {
  // We actually deal with the partially-softened FP_TO_FP16 node too, which
  // returns an i16 so doesn't meet the constraints necessary for FP_ROUND.
  assert(N->getOpcode() == ISD::FP_ROUND || N->getOpcode() == ISD::FP_TO_FP16);

  EVT SVT = N->getOperand(0).getValueType();
  EVT RVT = N->getValueType(0);
  EVT FloatRVT = N->getOpcode() == ISD::FP_TO_FP16 ? MVT::f16 : RVT;

  RTLIB::Libcall LC = RTLIB::getFPROUND(SVT, FloatRVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_ROUND libcall");

  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return TLI.makeLibCall(DAG, LC, RVT, Op, false, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_BR_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(2), NewRHS = N->getOperand(3);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(1))->get();

  EVT VT = NewLHS.getValueType();
  NewLHS = GetSoftenedFloat(NewLHS);
  NewRHS = GetSoftenedFloat(NewRHS);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N));

  // If softenSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                DAG.getCondCode(CCCode), NewLHS, NewRHS,
                                N->getOperand(4)),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FABS(SDNode *N) {
  SDValue Op = GetSoftenedFloat(N->getOperand(0));

  if (Op == N->getOperand(0))
    return SDValue();

  return SDValue(DAG.UpdateNodeOperands(N, Op), 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FCOPYSIGN(SDNode *N) {
  SDValue Op0 = GetSoftenedFloat(N->getOperand(0));
  SDValue Op1 = GetSoftenedFloat(N->getOperand(1));

  if (Op0 == N->getOperand(0) && Op1 == N->getOperand(1))
    return SDValue();

  return SDValue(DAG.UpdateNodeOperands(N, Op0, Op1), 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FNEG(SDNode *N) {
  SDValue Op = GetSoftenedFloat(N->getOperand(0));

  if (Op == N->getOperand(0))
    return SDValue();

  return SDValue(DAG.UpdateNodeOperands(N, Op), 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FP_TO_XINT(SDNode *N) {
  bool Signed = N->getOpcode() == ISD::FP_TO_SINT;
  EVT SVT = N->getOperand(0).getValueType();
  EVT RVT = N->getValueType(0);
  EVT NVT = EVT();
  SDLoc dl(N);

  // If the result is not legal, eg: fp -> i1, then it needs to be promoted to
  // a larger type, eg: fp -> i32. Even if it is legal, no libcall may exactly
  // match, eg. we don't have fp -> i8 conversions.
  // Look for an appropriate libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  for (unsigned IntVT = MVT::FIRST_INTEGER_VALUETYPE;
       IntVT <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL;
       ++IntVT) {
    NVT = (MVT::SimpleValueType)IntVT;
    // The type needs to big enough to hold the result.
    if (NVT.bitsGE(RVT))
      LC = Signed ? RTLIB::getFPTOSINT(SVT, NVT):RTLIB::getFPTOUINT(SVT, NVT);
  }
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_TO_XINT!");

  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  SDValue Res = TLI.makeLibCall(DAG, LC, NVT, Op, false, dl).first;

  // Truncate the result if the libcall returns a larger type.
  return DAG.getNode(ISD::TRUNCATE, dl, RVT, Res);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_SELECT(SDNode *N) {
  SDValue Op1 = GetSoftenedFloat(N->getOperand(1));
  SDValue Op2 = GetSoftenedFloat(N->getOperand(2));

  if (Op1 == N->getOperand(1) && Op2 == N->getOperand(2))
    return SDValue();

  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Op1, Op2),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_SELECT_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(4))->get();

  EVT VT = NewLHS.getValueType();
  NewLHS = GetSoftenedFloat(NewLHS);
  NewRHS = GetSoftenedFloat(NewRHS);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N));

  // If softenSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                N->getOperand(2), N->getOperand(3),
                                DAG.getCondCode(CCCode)),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_SETCC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();

  EVT VT = NewLHS.getValueType();
  NewLHS = GetSoftenedFloat(NewLHS);
  NewRHS = GetSoftenedFloat(NewRHS);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N));

  // If softenSetCCOperands returned a scalar, use it.
  if (!NewRHS.getNode()) {
    assert(NewLHS.getValueType() == N->getValueType(0) &&
           "Unexpected setcc expansion!");
    return NewLHS;
  }

  // Otherwise, update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                DAG.getCondCode(CCCode)),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_STORE(SDNode *N, unsigned OpNo) {
  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  assert(OpNo == 1 && "Can only soften the stored value!");
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue Val = ST->getValue();
  SDLoc dl(N);

  if (ST->isTruncatingStore())
    // Do an FP_ROUND followed by a non-truncating store.
    Val = BitConvertToInteger(DAG.getNode(ISD::FP_ROUND, dl, ST->getMemoryVT(),
                                          Val, DAG.getIntPtrConstant(0, dl)));
  else
    Val = GetSoftenedFloat(Val);

  return DAG.getStore(ST->getChain(), dl, Val, ST->getBasePtr(),
                      ST->getMemOperand());
}


//===----------------------------------------------------------------------===//
//  Float Result Expansion
//===----------------------------------------------------------------------===//

/// ExpandFloatResult - This method is called when the specified result of the
/// specified node is found to need expansion.  At this point, the node may also
/// have invalid operands or may have other results that need promotion, we just
/// know that (at least) one result needs expansion.
void DAGTypeLegalizer::ExpandFloatResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Expand float result: "; N->dump(&DAG); dbgs() << "\n");
  SDValue Lo, Hi;
  Lo = Hi = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true))
    return;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ExpandFloatResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to expand the result of this operator!");

  case ISD::UNDEF:        SplitRes_UNDEF(N, Lo, Hi); break;
  case ISD::SELECT:       SplitRes_SELECT(N, Lo, Hi); break;
  case ISD::SELECT_CC:    SplitRes_SELECT_CC(N, Lo, Hi); break;

  case ISD::MERGE_VALUES:       ExpandRes_MERGE_VALUES(N, ResNo, Lo, Hi); break;
  case ISD::BITCAST:            ExpandRes_BITCAST(N, Lo, Hi); break;
  case ISD::BUILD_PAIR:         ExpandRes_BUILD_PAIR(N, Lo, Hi); break;
  case ISD::EXTRACT_ELEMENT:    ExpandRes_EXTRACT_ELEMENT(N, Lo, Hi); break;
  case ISD::EXTRACT_VECTOR_ELT: ExpandRes_EXTRACT_VECTOR_ELT(N, Lo, Hi); break;
  case ISD::VAARG:              ExpandRes_VAARG(N, Lo, Hi); break;

  case ISD::ConstantFP: ExpandFloatRes_ConstantFP(N, Lo, Hi); break;
  case ISD::FABS:       ExpandFloatRes_FABS(N, Lo, Hi); break;
  case ISD::FMINNUM:    ExpandFloatRes_FMINNUM(N, Lo, Hi); break;
  case ISD::FMAXNUM:    ExpandFloatRes_FMAXNUM(N, Lo, Hi); break;
  case ISD::FADD:       ExpandFloatRes_FADD(N, Lo, Hi); break;
  case ISD::FCEIL:      ExpandFloatRes_FCEIL(N, Lo, Hi); break;
  case ISD::FCOPYSIGN:  ExpandFloatRes_FCOPYSIGN(N, Lo, Hi); break;
  case ISD::FCOS:       ExpandFloatRes_FCOS(N, Lo, Hi); break;
  case ISD::FDIV:       ExpandFloatRes_FDIV(N, Lo, Hi); break;
  case ISD::FEXP:       ExpandFloatRes_FEXP(N, Lo, Hi); break;
  case ISD::FEXP2:      ExpandFloatRes_FEXP2(N, Lo, Hi); break;
  case ISD::FFLOOR:     ExpandFloatRes_FFLOOR(N, Lo, Hi); break;
  case ISD::FLOG:       ExpandFloatRes_FLOG(N, Lo, Hi); break;
  case ISD::FLOG2:      ExpandFloatRes_FLOG2(N, Lo, Hi); break;
  case ISD::FLOG10:     ExpandFloatRes_FLOG10(N, Lo, Hi); break;
  case ISD::FMA:        ExpandFloatRes_FMA(N, Lo, Hi); break;
  case ISD::FMUL:       ExpandFloatRes_FMUL(N, Lo, Hi); break;
  case ISD::FNEARBYINT: ExpandFloatRes_FNEARBYINT(N, Lo, Hi); break;
  case ISD::FNEG:       ExpandFloatRes_FNEG(N, Lo, Hi); break;
  case ISD::FP_EXTEND:  ExpandFloatRes_FP_EXTEND(N, Lo, Hi); break;
  case ISD::FPOW:       ExpandFloatRes_FPOW(N, Lo, Hi); break;
  case ISD::FPOWI:      ExpandFloatRes_FPOWI(N, Lo, Hi); break;
  case ISD::FRINT:      ExpandFloatRes_FRINT(N, Lo, Hi); break;
  case ISD::FROUND:     ExpandFloatRes_FROUND(N, Lo, Hi); break;
  case ISD::FSIN:       ExpandFloatRes_FSIN(N, Lo, Hi); break;
  case ISD::FSQRT:      ExpandFloatRes_FSQRT(N, Lo, Hi); break;
  case ISD::FSUB:       ExpandFloatRes_FSUB(N, Lo, Hi); break;
  case ISD::FTRUNC:     ExpandFloatRes_FTRUNC(N, Lo, Hi); break;
  case ISD::LOAD:       ExpandFloatRes_LOAD(N, Lo, Hi); break;
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP: ExpandFloatRes_XINT_TO_FP(N, Lo, Hi); break;
  case ISD::FREM:       ExpandFloatRes_FREM(N, Lo, Hi); break;
  }

  // If Lo/Hi is null, the sub-method took care of registering results etc.
  if (Lo.getNode())
    SetExpandedFloat(SDValue(N, ResNo), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_ConstantFP(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  assert(NVT.getSizeInBits() == 64 &&
         "Do not know how to expand this float constant!");
  APInt C = cast<ConstantFPSDNode>(N)->getValueAPF().bitcastToAPInt();
  SDLoc dl(N);
  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(64, C.getRawData()[1])),
                         dl, NVT);
  Hi = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(64, C.getRawData()[0])),
                         dl, NVT);
}

void DAGTypeLegalizer::ExpandFloatRes_FABS(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  assert(N->getValueType(0) == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDLoc dl(N);
  SDValue Tmp;
  GetExpandedFloat(N->getOperand(0), Lo, Tmp);
  Hi = DAG.getNode(ISD::FABS, dl, Tmp.getValueType(), Tmp);
  // Lo = Hi==fabs(Hi) ? Lo : -Lo;
  Lo = DAG.getSelectCC(dl, Tmp, Hi, Lo,
                   DAG.getNode(ISD::FNEG, dl, Lo.getValueType(), Lo),
                   ISD::SETEQ);
}

void DAGTypeLegalizer::ExpandFloatRes_FMINNUM(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::FMIN_F32, RTLIB::FMIN_F64,
                                         RTLIB::FMIN_F80, RTLIB::FMIN_F128,
                                         RTLIB::FMIN_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMAXNUM(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::FMAX_F32, RTLIB::FMAX_F64,
                                         RTLIB::FMAX_F80, RTLIB::FMAX_F128,
                                         RTLIB::FMAX_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FADD(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::ADD_F32, RTLIB::ADD_F64,
                                         RTLIB::ADD_F80, RTLIB::ADD_F128,
                                         RTLIB::ADD_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCEIL(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::CEIL_F32, RTLIB::CEIL_F64,
                                         RTLIB::CEIL_F80, RTLIB::CEIL_F128,
                                         RTLIB::CEIL_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCOPYSIGN(SDNode *N,
                                                SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::COPYSIGN_F32,
                                         RTLIB::COPYSIGN_F64,
                                         RTLIB::COPYSIGN_F80,
                                         RTLIB::COPYSIGN_F128,
                                         RTLIB::COPYSIGN_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCOS(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::COS_F32, RTLIB::COS_F64,
                                         RTLIB::COS_F80, RTLIB::COS_F128,
                                         RTLIB::COS_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FDIV(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };
  SDValue Call = TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::DIV_F32,
                                                   RTLIB::DIV_F64,
                                                   RTLIB::DIV_F80,
                                                   RTLIB::DIV_F128,
                                                   RTLIB::DIV_PPCF128),
                                 N->getValueType(0), Ops, false,
                                 SDLoc(N)).first;
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FEXP(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::EXP_F32, RTLIB::EXP_F64,
                                         RTLIB::EXP_F80, RTLIB::EXP_F128,
                                         RTLIB::EXP_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FEXP2(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::EXP2_F32, RTLIB::EXP2_F64,
                                         RTLIB::EXP2_F80, RTLIB::EXP2_F128,
                                         RTLIB::EXP2_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FFLOOR(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::FLOOR_F32, RTLIB::FLOOR_F64,
                                         RTLIB::FLOOR_F80, RTLIB::FLOOR_F128,
                                         RTLIB::FLOOR_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::LOG_F32, RTLIB::LOG_F64,
                                         RTLIB::LOG_F80, RTLIB::LOG_F128,
                                         RTLIB::LOG_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG2(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::LOG2_F32, RTLIB::LOG2_F64,
                                         RTLIB::LOG2_F80, RTLIB::LOG2_F128,
                                         RTLIB::LOG2_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG10(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::LOG10_F32, RTLIB::LOG10_F64,
                                         RTLIB::LOG10_F80, RTLIB::LOG10_F128,
                                         RTLIB::LOG10_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMA(SDNode *N, SDValue &Lo,
                                          SDValue &Hi) {
  SDValue Ops[3] = { N->getOperand(0), N->getOperand(1), N->getOperand(2) };
  SDValue Call = TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::FMA_F32,
                                                   RTLIB::FMA_F64,
                                                   RTLIB::FMA_F80,
                                                   RTLIB::FMA_F128,
                                                   RTLIB::FMA_PPCF128),
                                 N->getValueType(0), Ops, false,
                                 SDLoc(N)).first;
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMUL(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };
  SDValue Call = TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::MUL_F32,
                                                   RTLIB::MUL_F64,
                                                   RTLIB::MUL_F80,
                                                   RTLIB::MUL_F128,
                                                   RTLIB::MUL_PPCF128),
                                 N->getValueType(0), Ops, false,
                                 SDLoc(N)).first;
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FNEARBYINT(SDNode *N,
                                                 SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::NEARBYINT_F32,
                                         RTLIB::NEARBYINT_F64,
                                         RTLIB::NEARBYINT_F80,
                                         RTLIB::NEARBYINT_F128,
                                         RTLIB::NEARBYINT_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FNEG(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedFloat(N->getOperand(0), Lo, Hi);
  Lo = DAG.getNode(ISD::FNEG, dl, Lo.getValueType(), Lo);
  Hi = DAG.getNode(ISD::FNEG, dl, Hi.getValueType(), Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FP_EXTEND(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  Hi = DAG.getNode(ISD::FP_EXTEND, dl, NVT, N->getOperand(0));
  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(NVT.getSizeInBits(), 0)), dl, NVT);
}

void DAGTypeLegalizer::ExpandFloatRes_FPOW(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::POW_F32, RTLIB::POW_F64,
                                         RTLIB::POW_F80, RTLIB::POW_F128,
                                         RTLIB::POW_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FPOWI(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::POWI_F32, RTLIB::POWI_F64,
                                         RTLIB::POWI_F80, RTLIB::POWI_F128,
                                         RTLIB::POWI_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FREM(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::REM_F32, RTLIB::REM_F64,
                                         RTLIB::REM_F80, RTLIB::REM_F128,
                                         RTLIB::REM_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FRINT(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::RINT_F32, RTLIB::RINT_F64,
                                         RTLIB::RINT_F80, RTLIB::RINT_F128,
                                         RTLIB::RINT_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FROUND(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::ROUND_F32,
                                         RTLIB::ROUND_F64,
                                         RTLIB::ROUND_F80,
                                         RTLIB::ROUND_F128,
                                         RTLIB::ROUND_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSIN(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::SIN_F32, RTLIB::SIN_F64,
                                         RTLIB::SIN_F80, RTLIB::SIN_F128,
                                         RTLIB::SIN_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSQRT(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::SQRT_F32, RTLIB::SQRT_F64,
                                         RTLIB::SQRT_F80, RTLIB::SQRT_F128,
                                         RTLIB::SQRT_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSUB(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };
  SDValue Call = TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::SUB_F32,
                                                   RTLIB::SUB_F64,
                                                   RTLIB::SUB_F80,
                                                   RTLIB::SUB_F128,
                                                   RTLIB::SUB_PPCF128),
                                 N->getValueType(0), Ops, false,
                                 SDLoc(N)).first;
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FTRUNC(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue Call = LibCallify(GetFPLibCall(N->getValueType(0),
                                         RTLIB::TRUNC_F32, RTLIB::TRUNC_F64,
                                         RTLIB::TRUNC_F80, RTLIB::TRUNC_F128,
                                         RTLIB::TRUNC_PPCF128),
                            N, false);
  GetPairElements(Call, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_LOAD(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  if (ISD::isNormalLoad(N)) {
    ExpandRes_NormalLoad(N, Lo, Hi);
    return;
  }

  assert(ISD::isUNINDEXEDLoad(N) && "Indexed load during type legalization!");
  LoadSDNode *LD = cast<LoadSDNode>(N);
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  SDLoc dl(N);

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), LD->getValueType(0));
  assert(NVT.isByteSized() && "Expanded type not byte sized!");
  assert(LD->getMemoryVT().bitsLE(NVT) && "Float type not round?");

  Hi = DAG.getExtLoad(LD->getExtensionType(), dl, NVT, Chain, Ptr,
                      LD->getMemoryVT(), LD->getMemOperand());

  // Remember the chain.
  Chain = Hi.getValue(1);

  // The low part is zero.
  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(NVT.getSizeInBits(), 0)), dl, NVT);

  // Modified the chain - switch anything that used the old chain to use the
  // new one.
  ReplaceValueWith(SDValue(LD, 1), Chain);
}

void DAGTypeLegalizer::ExpandFloatRes_XINT_TO_FP(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  assert(N->getValueType(0) == MVT::ppcf128 && "Unsupported XINT_TO_FP!");
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src.getValueType();
  bool isSigned = N->getOpcode() == ISD::SINT_TO_FP;
  SDLoc dl(N);

  // First do an SINT_TO_FP, whether the original was signed or unsigned.
  // When promoting partial word types to i32 we must honor the signedness,
  // though.
  if (SrcVT.bitsLE(MVT::i32)) {
    // The integer can be represented exactly in an f64.
    Src = DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                      MVT::i32, Src);
    Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                   APInt(NVT.getSizeInBits(), 0)), dl, NVT);
    Hi = DAG.getNode(ISD::SINT_TO_FP, dl, NVT, Src);
  } else {
    RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
    if (SrcVT.bitsLE(MVT::i64)) {
      Src = DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                        MVT::i64, Src);
      LC = RTLIB::SINTTOFP_I64_PPCF128;
    } else if (SrcVT.bitsLE(MVT::i128)) {
      Src = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i128, Src);
      LC = RTLIB::SINTTOFP_I128_PPCF128;
    }
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported XINT_TO_FP!");

    Hi = TLI.makeLibCall(DAG, LC, VT, Src, true, dl).first;
    GetPairElements(Hi, Lo, Hi);
  }

  if (isSigned)
    return;

  // Unsigned - fix up the SINT_TO_FP value just calculated.
  Hi = DAG.getNode(ISD::BUILD_PAIR, dl, VT, Lo, Hi);
  SrcVT = Src.getValueType();

  // x>=0 ? (ppcf128)(iN)x : (ppcf128)(iN)x + 2^N; N=32,64,128.
  static const uint64_t TwoE32[]  = { 0x41f0000000000000LL, 0 };
  static const uint64_t TwoE64[]  = { 0x43f0000000000000LL, 0 };
  static const uint64_t TwoE128[] = { 0x47f0000000000000LL, 0 };
  ArrayRef<uint64_t> Parts;

  switch (SrcVT.getSimpleVT().SimpleTy) {
  default:
    llvm_unreachable("Unsupported UINT_TO_FP!");
  case MVT::i32:
    Parts = TwoE32;
    break;
  case MVT::i64:
    Parts = TwoE64;
    break;
  case MVT::i128:
    Parts = TwoE128;
    break;
  }

  // TODO: Are there fast-math-flags to propagate to this FADD?
  Lo = DAG.getNode(ISD::FADD, dl, VT, Hi,
                   DAG.getConstantFP(APFloat(APFloat::PPCDoubleDouble(),
                                             APInt(128, Parts)),
                                     dl, MVT::ppcf128));
  Lo = DAG.getSelectCC(dl, Src, DAG.getConstant(0, dl, SrcVT),
                       Lo, Hi, ISD::SETLT);
  GetPairElements(Lo, Lo, Hi);
}


//===----------------------------------------------------------------------===//
//  Float Operand Expansion
//===----------------------------------------------------------------------===//

/// ExpandFloatOperand - This method is called when the specified operand of the
/// specified node is found to need expansion.  At this point, all of the result
/// types of the node are known to be legal, but other operands of the node may
/// need promotion or expansion as well as the specified one.
bool DAGTypeLegalizer::ExpandFloatOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Expand float operand: "; N->dump(&DAG); dbgs() << "\n");
  SDValue Res = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ExpandFloatOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to expand this operator's operand!");

  case ISD::BITCAST:         Res = ExpandOp_BITCAST(N); break;
  case ISD::BUILD_VECTOR:    Res = ExpandOp_BUILD_VECTOR(N); break;
  case ISD::EXTRACT_ELEMENT: Res = ExpandOp_EXTRACT_ELEMENT(N); break;

  case ISD::BR_CC:      Res = ExpandFloatOp_BR_CC(N); break;
  case ISD::FCOPYSIGN:  Res = ExpandFloatOp_FCOPYSIGN(N); break;
  case ISD::FP_ROUND:   Res = ExpandFloatOp_FP_ROUND(N); break;
  case ISD::FP_TO_SINT: Res = ExpandFloatOp_FP_TO_SINT(N); break;
  case ISD::FP_TO_UINT: Res = ExpandFloatOp_FP_TO_UINT(N); break;
  case ISD::SELECT_CC:  Res = ExpandFloatOp_SELECT_CC(N); break;
  case ISD::SETCC:      Res = ExpandFloatOp_SETCC(N); break;
  case ISD::STORE:      Res = ExpandFloatOp_STORE(cast<StoreSDNode>(N),
                                                  OpNo); break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this.
  if (Res.getNode() == N)
    return true;

  assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

/// FloatExpandSetCCOperands - Expand the operands of a comparison.  This code
/// is shared among BR_CC, SELECT_CC, and SETCC handlers.
void DAGTypeLegalizer::FloatExpandSetCCOperands(SDValue &NewLHS,
                                                SDValue &NewRHS,
                                                ISD::CondCode &CCCode,
                                                const SDLoc &dl) {
  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  GetExpandedFloat(NewLHS, LHSLo, LHSHi);
  GetExpandedFloat(NewRHS, RHSLo, RHSHi);

  assert(NewLHS.getValueType() == MVT::ppcf128 && "Unsupported setcc type!");

  // FIXME:  This generated code sucks.  We want to generate
  //         FCMPU crN, hi1, hi2
  //         BNE crN, L:
  //         FCMPU crN, lo1, lo2
  // The following can be improved, but not that much.
  SDValue Tmp1, Tmp2, Tmp3;
  Tmp1 = DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()),
                      LHSHi, RHSHi, ISD::SETOEQ);
  Tmp2 = DAG.getSetCC(dl, getSetCCResultType(LHSLo.getValueType()),
                      LHSLo, RHSLo, CCCode);
  Tmp3 = DAG.getNode(ISD::AND, dl, Tmp1.getValueType(), Tmp1, Tmp2);
  Tmp1 = DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()),
                      LHSHi, RHSHi, ISD::SETUNE);
  Tmp2 = DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()),
                      LHSHi, RHSHi, CCCode);
  Tmp1 = DAG.getNode(ISD::AND, dl, Tmp1.getValueType(), Tmp1, Tmp2);
  NewLHS = DAG.getNode(ISD::OR, dl, Tmp1.getValueType(), Tmp1, Tmp3);
  NewRHS = SDValue();   // LHS is the result, not a compare.
}

SDValue DAGTypeLegalizer::ExpandFloatOp_BR_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(2), NewRHS = N->getOperand(3);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(1))->get();
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                DAG.getCondCode(CCCode), NewLHS, NewRHS,
                                N->getOperand(4)), 0);
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FCOPYSIGN(SDNode *N) {
  assert(N->getOperand(1).getValueType() == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDValue Lo, Hi;
  GetExpandedFloat(N->getOperand(1), Lo, Hi);
  // The ppcf128 value is providing only the sign; take it from the
  // higher-order double (which must have the larger magnitude).
  return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N),
                     N->getValueType(0), N->getOperand(0), Hi);
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FP_ROUND(SDNode *N) {
  assert(N->getOperand(0).getValueType() == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDValue Lo, Hi;
  GetExpandedFloat(N->getOperand(0), Lo, Hi);
  // Round it the rest of the way (e.g. to f32) if needed.
  return DAG.getNode(ISD::FP_ROUND, SDLoc(N),
                     N->getValueType(0), Hi, N->getOperand(1));
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FP_TO_SINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  SDLoc dl(N);

  RTLIB::Libcall LC = RTLIB::getFPTOSINT(N->getOperand(0).getValueType(), RVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_TO_SINT!");
  return TLI.makeLibCall(DAG, LC, RVT, N->getOperand(0), false, dl).first;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FP_TO_UINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  SDLoc dl(N);

  RTLIB::Libcall LC = RTLIB::getFPTOUINT(N->getOperand(0).getValueType(), RVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_TO_UINT!");
  return TLI.makeLibCall(DAG, LC, N->getValueType(0), N->getOperand(0),
                         false, dl).first;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_SELECT_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(4))->get();
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                N->getOperand(2), N->getOperand(3),
                                DAG.getCondCode(CCCode)), 0);
}

SDValue DAGTypeLegalizer::ExpandFloatOp_SETCC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, use it.
  if (!NewRHS.getNode()) {
    assert(NewLHS.getValueType() == N->getValueType(0) &&
           "Unexpected setcc expansion!");
    return NewLHS;
  }

  // Otherwise, update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                DAG.getCondCode(CCCode)), 0);
}

SDValue DAGTypeLegalizer::ExpandFloatOp_STORE(SDNode *N, unsigned OpNo) {
  if (ISD::isNormalStore(N))
    return ExpandOp_NormalStore(N, OpNo);

  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  assert(OpNo == 1 && "Can only expand the stored value so far");
  StoreSDNode *ST = cast<StoreSDNode>(N);

  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(),
                                     ST->getValue().getValueType());
  assert(NVT.isByteSized() && "Expanded type not byte sized!");
  assert(ST->getMemoryVT().bitsLE(NVT) && "Float type not round?");
  (void)NVT;

  SDValue Lo, Hi;
  GetExpandedOp(ST->getValue(), Lo, Hi);

  return DAG.getTruncStore(Chain, SDLoc(N), Hi, Ptr,
                           ST->getMemoryVT(), ST->getMemOperand());
}

//===----------------------------------------------------------------------===//
//  Float Operand Promotion
//===----------------------------------------------------------------------===//
//

static ISD::NodeType GetPromotionOpcode(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
      return ISD::FP16_TO_FP;
  } else if (RetVT == MVT::f16) {
      return ISD::FP_TO_FP16;
  }

  report_fatal_error("Attempt at an invalid promotion-related conversion");
}

bool DAGTypeLegalizer::PromoteFloatOperand(SDNode *N, unsigned OpNo) {
  SDValue R = SDValue();

  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false)) {
    LLVM_DEBUG(dbgs() << "Node has been custom lowered, done\n");
    return false;
  }

  // Nodes that use a promotion-requiring floating point operand, but doesn't
  // produce a promotion-requiring floating point result, need to be legalized
  // to use the promoted float operand.  Nodes that produce at least one
  // promotion-requiring floating point result have their operands legalized as
  // a part of PromoteFloatResult.
  switch (N->getOpcode()) {
    default:
      llvm_unreachable("Do not know how to promote this operator's operand!");

    case ISD::BITCAST:    R = PromoteFloatOp_BITCAST(N, OpNo); break;
    case ISD::FCOPYSIGN:  R = PromoteFloatOp_FCOPYSIGN(N, OpNo); break;
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT: R = PromoteFloatOp_FP_TO_XINT(N, OpNo); break;
    case ISD::FP_EXTEND:  R = PromoteFloatOp_FP_EXTEND(N, OpNo); break;
    case ISD::SELECT_CC:  R = PromoteFloatOp_SELECT_CC(N, OpNo); break;
    case ISD::SETCC:      R = PromoteFloatOp_SETCC(N, OpNo); break;
    case ISD::STORE:      R = PromoteFloatOp_STORE(N, OpNo); break;
  }

  if (R.getNode())
    ReplaceValueWith(SDValue(N, 0), R);
  return false;
}

SDValue DAGTypeLegalizer::PromoteFloatOp_BITCAST(SDNode *N, unsigned OpNo) {
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op->getValueType(0);

  SDValue Promoted = GetPromotedFloat(N->getOperand(0));
  EVT PromotedVT = Promoted->getValueType(0);

  // Convert the promoted float value to the desired IVT.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), OpVT.getSizeInBits());
  SDValue Convert = DAG.getNode(GetPromotionOpcode(PromotedVT, OpVT), SDLoc(N),
                                IVT, Promoted);
  // The final result type might not be an scalar so we need a bitcast. The
  // bitcast will be further legalized if needed.
  return DAG.getBitcast(N->getValueType(0), Convert);
}

// Promote Operand 1 of FCOPYSIGN.  Operand 0 ought to be handled by
// PromoteFloatRes_FCOPYSIGN.
SDValue DAGTypeLegalizer::PromoteFloatOp_FCOPYSIGN(SDNode *N, unsigned OpNo) {
  assert (OpNo == 1 && "Only Operand 1 must need promotion here");
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));

  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0),
                     N->getOperand(0), Op1);
}

// Convert the promoted float value to the desired integer type
SDValue DAGTypeLegalizer::PromoteFloatOp_FP_TO_XINT(SDNode *N, unsigned OpNo) {
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0), Op);
}

SDValue DAGTypeLegalizer::PromoteFloatOp_FP_EXTEND(SDNode *N, unsigned OpNo) {
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  EVT VT = N->getValueType(0);

  // Desired VT is same as promoted type.  Use promoted float directly.
  if (VT == Op->getValueType(0))
    return Op;

  // Else, extend the promoted float value to the desired VT.
  return DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, Op);
}

// Promote the float operands used for comparison.  The true- and false-
// operands have the same type as the result and are promoted, if needed, by
// PromoteFloatRes_SELECT_CC
SDValue DAGTypeLegalizer::PromoteFloatOp_SELECT_CC(SDNode *N, unsigned OpNo) {
  SDValue LHS = GetPromotedFloat(N->getOperand(0));
  SDValue RHS = GetPromotedFloat(N->getOperand(1));

  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), N->getValueType(0),
                     LHS, RHS, N->getOperand(2), N->getOperand(3),
                     N->getOperand(4));
}

// Construct a SETCC that compares the promoted values and sets the conditional
// code.
SDValue DAGTypeLegalizer::PromoteFloatOp_SETCC(SDNode *N, unsigned OpNo) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();

  return DAG.getSetCC(SDLoc(N), NVT, Op0, Op1, CCCode);

}

// Lower the promoted Float down to the integer value of same size and construct
// a STORE of the integer value.
SDValue DAGTypeLegalizer::PromoteFloatOp_STORE(SDNode *N, unsigned OpNo) {
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue Val = ST->getValue();
  SDLoc DL(N);

  SDValue Promoted = GetPromotedFloat(Val);
  EVT VT = ST->getOperand(1).getValueType();
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  SDValue NewVal;
  NewVal = DAG.getNode(GetPromotionOpcode(Promoted.getValueType(), VT), DL,
                       IVT, Promoted);

  return DAG.getStore(ST->getChain(), DL, NewVal, ST->getBasePtr(),
                      ST->getMemOperand());
}

//===----------------------------------------------------------------------===//
//  Float Result Promotion
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::PromoteFloatResult(SDNode *N, unsigned ResNo) {
  SDValue R = SDValue();

  switch (N->getOpcode()) {
    // These opcodes cannot appear if promotion of FP16 is done in the backend
    // instead of Clang
    case ISD::FP16_TO_FP:
    case ISD::FP_TO_FP16:
    default:
      llvm_unreachable("Do not know how to promote this operator's result!");

    case ISD::BITCAST:    R = PromoteFloatRes_BITCAST(N); break;
    case ISD::ConstantFP: R = PromoteFloatRes_ConstantFP(N); break;
    case ISD::EXTRACT_VECTOR_ELT:
                          R = PromoteFloatRes_EXTRACT_VECTOR_ELT(N); break;
    case ISD::FCOPYSIGN:  R = PromoteFloatRes_FCOPYSIGN(N); break;

    // Unary FP Operations
    case ISD::FABS:
    case ISD::FCEIL:
    case ISD::FCOS:
    case ISD::FEXP:
    case ISD::FEXP2:
    case ISD::FFLOOR:
    case ISD::FLOG:
    case ISD::FLOG2:
    case ISD::FLOG10:
    case ISD::FNEARBYINT:
    case ISD::FNEG:
    case ISD::FRINT:
    case ISD::FROUND:
    case ISD::FSIN:
    case ISD::FSQRT:
    case ISD::FTRUNC:
    case ISD::FCANONICALIZE: R = PromoteFloatRes_UnaryOp(N); break;

    // Binary FP Operations
    case ISD::FADD:
    case ISD::FDIV:
    case ISD::FMAXIMUM:
    case ISD::FMINIMUM:
    case ISD::FMAXNUM:
    case ISD::FMINNUM:
    case ISD::FMUL:
    case ISD::FPOW:
    case ISD::FREM:
    case ISD::FSUB:       R = PromoteFloatRes_BinOp(N); break;

    case ISD::FMA:        // FMA is same as FMAD
    case ISD::FMAD:       R = PromoteFloatRes_FMAD(N); break;

    case ISD::FPOWI:      R = PromoteFloatRes_FPOWI(N); break;

    case ISD::FP_ROUND:   R = PromoteFloatRes_FP_ROUND(N); break;
    case ISD::LOAD:       R = PromoteFloatRes_LOAD(N); break;
    case ISD::SELECT:     R = PromoteFloatRes_SELECT(N); break;
    case ISD::SELECT_CC:  R = PromoteFloatRes_SELECT_CC(N); break;

    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP: R = PromoteFloatRes_XINT_TO_FP(N); break;
    case ISD::UNDEF:      R = PromoteFloatRes_UNDEF(N); break;

  }

  if (R.getNode())
    SetPromotedFloat(SDValue(N, ResNo), R);
}

// Bitcast from i16 to f16:  convert the i16 to a f32 value instead.
// At this point, it is not possible to determine if the bitcast value is
// eventually stored to memory or promoted to f32 or promoted to a floating
// point at a higher precision.  Some of these cases are handled by FP_EXTEND,
// STORE promotion handlers.
SDValue DAGTypeLegalizer::PromoteFloatRes_BITCAST(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  // Input type isn't guaranteed to be a scalar int so bitcast if not. The
  // bitcast will be legalized further if necessary.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(),
                              N->getOperand(0).getValueType().getSizeInBits());
  SDValue Cast = DAG.getBitcast(IVT, N->getOperand(0));
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, Cast);
}

SDValue DAGTypeLegalizer::PromoteFloatRes_ConstantFP(SDNode *N) {
  ConstantFPSDNode *CFPNode = cast<ConstantFPSDNode>(N);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // Get the (bit-cast) APInt of the APFloat and build an integer constant
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
  SDValue C = DAG.getConstant(CFPNode->getValueAPF().bitcastToAPInt(), DL,
                              IVT);

  // Convert the Constant to the desired FP type
  // FIXME We might be able to do the conversion during compilation and get rid
  // of it from the object code
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), DL, NVT, C);
}

// If the Index operand is a constant, try to redirect the extract operation to
// the correct legalized vector.  If not, bit-convert the input vector to
// equivalent integer vector.  Extract the element as an (bit-cast) integer
// value and convert it to the promoted type.
SDValue DAGTypeLegalizer::PromoteFloatRes_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDLoc DL(N);

  // If the index is constant, try to extract the value from the legalized
  // vector type.
  if (isa<ConstantSDNode>(N->getOperand(1))) {
    SDValue Vec = N->getOperand(0);
    SDValue Idx = N->getOperand(1);
    EVT VecVT = Vec->getValueType(0);
    EVT EltVT = VecVT.getVectorElementType();

    uint64_t IdxVal = cast<ConstantSDNode>(Idx)->getZExtValue();

    switch (getTypeAction(VecVT)) {
    default: break;
    case TargetLowering::TypeScalarizeVector: {
      SDValue Res = GetScalarizedVector(N->getOperand(0));
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }
    case TargetLowering::TypeWidenVector: {
      Vec = GetWidenedVector(Vec);
      SDValue Res = DAG.getNode(N->getOpcode(), DL, EltVT, Vec, Idx);
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }
    case TargetLowering::TypeSplitVector: {
      SDValue Lo, Hi;
      GetSplitVector(Vec, Lo, Hi);

      uint64_t LoElts = Lo.getValueType().getVectorNumElements();
      SDValue Res;
      if (IdxVal < LoElts)
        Res = DAG.getNode(N->getOpcode(), DL, EltVT, Lo, Idx);
      else
        Res = DAG.getNode(N->getOpcode(), DL, EltVT, Hi,
                          DAG.getConstant(IdxVal - LoElts, DL,
                                          Idx.getValueType()));
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }

    }
  }

  // Bit-convert the input vector to the equivalent integer vector
  SDValue NewOp = BitConvertVectorToIntegerVector(N->getOperand(0));
  EVT IVT = NewOp.getValueType().getVectorElementType();

  // Extract the element as an (bit-cast) integer value
  SDValue NewVal = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, IVT,
                               NewOp, N->getOperand(1));

  // Convert the element to the desired FP type
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, NewVal);
}

// FCOPYSIGN(X, Y) returns the value of X with the sign of Y.  If the result
// needs promotion, so does the argument X.  Note that Y, if needed, will be
// handled during operand promotion.
SDValue DAGTypeLegalizer::PromoteFloatRes_FCOPYSIGN(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));

  SDValue Op1 = N->getOperand(1);

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1);
}

// Unary operation where the result and the operand have PromoteFloat type
// action.  Construct a new SDNode with the promoted float value of the old
// operand.
SDValue DAGTypeLegalizer::PromoteFloatRes_UnaryOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op = GetPromotedFloat(N->getOperand(0));

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op);
}

// Binary operations where the result and both operands have PromoteFloat type
// action.  Construct a new SDNode with the promoted float values of the old
// operands.
SDValue DAGTypeLegalizer::PromoteFloatRes_BinOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1, N->getFlags());
}

SDValue DAGTypeLegalizer::PromoteFloatRes_FMAD(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  SDValue Op2 = GetPromotedFloat(N->getOperand(2));

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1, Op2);
}

// Promote the Float (first) operand and retain the Integer (second) operand
SDValue DAGTypeLegalizer::PromoteFloatRes_FPOWI(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = N->getOperand(1);

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1);
}

// Explicit operation to reduce precision.  Reduce the value to half precision
// and promote it back to the legal type.
SDValue DAGTypeLegalizer::PromoteFloatRes_FP_ROUND(SDNode *N) {
  SDLoc DL(N);

  SDValue Op = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT OpVT = Op->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  // Round promoted float to desired precision
  SDValue Round = DAG.getNode(GetPromotionOpcode(OpVT, VT), DL, IVT, Op);
  // Promote it back to the legal output type
  return DAG.getNode(GetPromotionOpcode(VT, NVT), DL, NVT, Round);
}

SDValue DAGTypeLegalizer::PromoteFloatRes_LOAD(SDNode *N) {
  LoadSDNode *L = cast<LoadSDNode>(N);
  EVT VT = N->getValueType(0);

  // Load the value as an integer value with the same number of bits.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
  SDValue newL = DAG.getLoad(L->getAddressingMode(), L->getExtensionType(), IVT,
                             SDLoc(N), L->getChain(), L->getBasePtr(),
                             L->getOffset(), L->getPointerInfo(), IVT,
                             L->getAlignment(),
                             L->getMemOperand()->getFlags(),
                             L->getAAInfo());
  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), newL.getValue(1));

  // Convert the integer value to the desired FP type
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, newL);
}

// Construct a new SELECT node with the promoted true- and false- values.
SDValue DAGTypeLegalizer::PromoteFloatRes_SELECT(SDNode *N) {
  SDValue TrueVal = GetPromotedFloat(N->getOperand(1));
  SDValue FalseVal = GetPromotedFloat(N->getOperand(2));

  return DAG.getNode(ISD::SELECT, SDLoc(N), TrueVal->getValueType(0),
                     N->getOperand(0), TrueVal, FalseVal);
}

// Construct a new SELECT_CC node with the promoted true- and false- values.
// The operands used for comparison are promoted by PromoteFloatOp_SELECT_CC.
SDValue DAGTypeLegalizer::PromoteFloatRes_SELECT_CC(SDNode *N) {
  SDValue TrueVal = GetPromotedFloat(N->getOperand(2));
  SDValue FalseVal = GetPromotedFloat(N->getOperand(3));

  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     TrueVal.getNode()->getValueType(0), N->getOperand(0),
                     N->getOperand(1), TrueVal, FalseVal, N->getOperand(4));
}

// Construct a SDNode that transforms the SINT or UINT operand to the promoted
// float type.
SDValue DAGTypeLegalizer::PromoteFloatRes_XINT_TO_FP(SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue NV = DAG.getNode(N->getOpcode(), DL, NVT, N->getOperand(0));
  // Round the value to the desired precision (that of the source type).
  return DAG.getNode(
      ISD::FP_EXTEND, DL, NVT,
      DAG.getNode(ISD::FP_ROUND, DL, VT, NV, DAG.getIntPtrConstant(0, DL)));
}

SDValue DAGTypeLegalizer::PromoteFloatRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(TLI.getTypeToTransformTo(*DAG.getContext(),
                                               N->getValueType(0)));
}

