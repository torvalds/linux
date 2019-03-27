//===------- LegalizeVectorTypes.cpp - Legalization of vector types -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file performs vector type splitting and scalarization for LegalizeTypes.
// Scalarization is the act of changing a computation in an illegal one-element
// vector type to be a computation in its scalar element type.  For example,
// implementing <1 x f32> arithmetic in a scalar f32 register.  This is needed
// as a base case when scalarizing vector arithmetic like <4 x f32>, which
// eventually decomposes to scalars if the target doesn't support v4f32 or v2f32
// types.
// Splitting is the act of changing a computation in an invalid vector type to
// be a computation in two vectors of half the size.  For example, implementing
// <128 x f32> operations in terms of two <64 x f32> operations.
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "legalize-types"

//===----------------------------------------------------------------------===//
//  Result Vector Scalarization: <1 x ty> -> ty.
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::ScalarizeVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Scalarize node result " << ResNo << ": "; N->dump(&DAG);
             dbgs() << "\n");
  SDValue R = SDValue();

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ScalarizeVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to scalarize the result of this "
                       "operator!\n");

  case ISD::MERGE_VALUES:      R = ScalarizeVecRes_MERGE_VALUES(N, ResNo);break;
  case ISD::BITCAST:           R = ScalarizeVecRes_BITCAST(N); break;
  case ISD::BUILD_VECTOR:      R = ScalarizeVecRes_BUILD_VECTOR(N); break;
  case ISD::EXTRACT_SUBVECTOR: R = ScalarizeVecRes_EXTRACT_SUBVECTOR(N); break;
  case ISD::FP_ROUND:          R = ScalarizeVecRes_FP_ROUND(N); break;
  case ISD::FP_ROUND_INREG:    R = ScalarizeVecRes_InregOp(N); break;
  case ISD::FPOWI:             R = ScalarizeVecRes_FPOWI(N); break;
  case ISD::INSERT_VECTOR_ELT: R = ScalarizeVecRes_INSERT_VECTOR_ELT(N); break;
  case ISD::LOAD:           R = ScalarizeVecRes_LOAD(cast<LoadSDNode>(N));break;
  case ISD::SCALAR_TO_VECTOR:  R = ScalarizeVecRes_SCALAR_TO_VECTOR(N); break;
  case ISD::SIGN_EXTEND_INREG: R = ScalarizeVecRes_InregOp(N); break;
  case ISD::VSELECT:           R = ScalarizeVecRes_VSELECT(N); break;
  case ISD::SELECT:            R = ScalarizeVecRes_SELECT(N); break;
  case ISD::SELECT_CC:         R = ScalarizeVecRes_SELECT_CC(N); break;
  case ISD::SETCC:             R = ScalarizeVecRes_SETCC(N); break;
  case ISD::UNDEF:             R = ScalarizeVecRes_UNDEF(N); break;
  case ISD::VECTOR_SHUFFLE:    R = ScalarizeVecRes_VECTOR_SHUFFLE(N); break;
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    R = ScalarizeVecRes_VecInregOp(N);
    break;
  case ISD::ANY_EXTEND:
  case ISD::BITREVERSE:
  case ISD::BSWAP:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::FABS:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::FNEG:
  case ISD::FP_EXTEND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FSIN:
  case ISD::FSQRT:
  case ISD::FTRUNC:
  case ISD::SIGN_EXTEND:
  case ISD::SINT_TO_FP:
  case ISD::TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::ZERO_EXTEND:
  case ISD::FCANONICALIZE:
    R = ScalarizeVecRes_UnaryOp(N);
    break;

  case ISD::ADD:
  case ISD::AND:
  case ISD::FADD:
  case ISD::FCOPYSIGN:
  case ISD::FDIV:
  case ISD::FMUL:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:

  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:

  case ISD::FPOW:
  case ISD::FREM:
  case ISD::FSUB:
  case ISD::MUL:
  case ISD::OR:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::SUB:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    R = ScalarizeVecRes_BinOp(N);
    break;
  case ISD::FMA:
    R = ScalarizeVecRes_TernaryOp(N);
    break;
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FREM:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
  case ISD::STRICT_FPOW:
  case ISD::STRICT_FPOWI:
  case ISD::STRICT_FSIN:
  case ISD::STRICT_FCOS:
  case ISD::STRICT_FEXP:
  case ISD::STRICT_FEXP2:
  case ISD::STRICT_FLOG:
  case ISD::STRICT_FLOG10:
  case ISD::STRICT_FLOG2:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FMAXNUM:
  case ISD::STRICT_FMINNUM:
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FTRUNC:
    R = ScalarizeVecRes_StrictFPOp(N);
    break;
  case ISD::SMULFIX:
    R = ScalarizeVecRes_SMULFIX(N);
    break;
  }

  // If R is null, the sub-method took care of registering the result.
  if (R.getNode())
    SetScalarizedVector(SDValue(N, ResNo), R);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BinOp(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  SDValue RHS = GetScalarizedVector(N->getOperand(1));
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     LHS.getValueType(), LHS, RHS, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_TernaryOp(SDNode *N) {
  SDValue Op0 = GetScalarizedVector(N->getOperand(0));
  SDValue Op1 = GetScalarizedVector(N->getOperand(1));
  SDValue Op2 = GetScalarizedVector(N->getOperand(2));
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     Op0.getValueType(), Op0, Op1, Op2);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SMULFIX(SDNode *N) {
  SDValue Op0 = GetScalarizedVector(N->getOperand(0));
  SDValue Op1 = GetScalarizedVector(N->getOperand(1));
  SDValue Op2 = N->getOperand(2);
  return DAG.getNode(N->getOpcode(), SDLoc(N), Op0.getValueType(), Op0, Op1,
                     Op2);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_StrictFPOp(SDNode *N) {
  EVT VT = N->getValueType(0).getVectorElementType();
  unsigned NumOpers = N->getNumOperands();
  SDValue Chain = N->getOperand(0);
  EVT ValueVTs[] = {VT, MVT::Other};
  SDLoc dl(N);

  SmallVector<SDValue, 4> Opers;

  // The Chain is the first operand.
  Opers.push_back(Chain);

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOpers; ++i) {
    SDValue Oper = N->getOperand(i);

    if (Oper.getValueType().isVector())
      Oper = GetScalarizedVector(Oper);

    Opers.push_back(Oper);
  }

  SDValue Result = DAG.getNode(N->getOpcode(), dl, ValueVTs, Opers);

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Result.getValue(1));
  return Result;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_MERGE_VALUES(SDNode *N,
                                                       unsigned ResNo) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  return GetScalarizedVector(Op);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BITCAST(SDNode *N) {
  SDValue Op = N->getOperand(0);
  if (Op.getValueType().isVector()
      && Op.getValueType().getVectorNumElements() == 1
      && !isSimpleLegalType(Op.getValueType()))
    Op = GetScalarizedVector(Op);
  EVT NewVT = N->getValueType(0).getVectorElementType();
  return DAG.getNode(ISD::BITCAST, SDLoc(N),
                     NewVT, Op);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BUILD_VECTOR(SDNode *N) {
  EVT EltVT = N->getValueType(0).getVectorElementType();
  SDValue InOp = N->getOperand(0);
  // The BUILD_VECTOR operands may be of wider element types and
  // we may need to truncate them back to the requested return type.
  if (EltVT.isInteger())
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, InOp);
  return InOp;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_EXTRACT_SUBVECTOR(SDNode *N) {
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     N->getValueType(0).getVectorElementType(),
                     N->getOperand(0), N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FP_ROUND(SDNode *N) {
  EVT NewVT = N->getValueType(0).getVectorElementType();
  SDValue Op = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(ISD::FP_ROUND, SDLoc(N),
                     NewVT, Op, N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FPOWI(SDNode *N) {
  SDValue Op = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(ISD::FPOWI, SDLoc(N),
                     Op.getValueType(), Op, N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_INSERT_VECTOR_ELT(SDNode *N) {
  // The value to insert may have a wider type than the vector element type,
  // so be sure to truncate it to the element type if necessary.
  SDValue Op = N->getOperand(1);
  EVT EltVT = N->getValueType(0).getVectorElementType();
  if (Op.getValueType() != EltVT)
    // FIXME: Can this happen for floating point types?
    Op = DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, Op);
  return Op;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_LOAD(LoadSDNode *N) {
  assert(N->isUnindexed() && "Indexed vector load?");

  SDValue Result = DAG.getLoad(
      ISD::UNINDEXED, N->getExtensionType(),
      N->getValueType(0).getVectorElementType(), SDLoc(N), N->getChain(),
      N->getBasePtr(), DAG.getUNDEF(N->getBasePtr().getValueType()),
      N->getPointerInfo(), N->getMemoryVT().getVectorElementType(),
      N->getOriginalAlignment(), N->getMemOperand()->getFlags(),
      N->getAAInfo());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Result.getValue(1));
  return Result;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_UnaryOp(SDNode *N) {
  // Get the dest type - it doesn't always match the input type, e.g. int_to_fp.
  EVT DestVT = N->getValueType(0).getVectorElementType();
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op.getValueType();
  SDLoc DL(N);
  // The result needs scalarizing, but it's not a given that the source does.
  // This is a workaround for targets where it's impossible to scalarize the
  // result of a conversion, because the source type is legal.
  // For instance, this happens on AArch64: v1i1 is illegal but v1i{8,16,32}
  // are widened to v8i8, v4i16, and v2i32, which is legal, because v1i64 is
  // legal and was not scalarized.
  // See the similar logic in ScalarizeVecRes_SETCC
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Op = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, VT, Op,
        DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }
  return DAG.getNode(N->getOpcode(), SDLoc(N), DestVT, Op);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_InregOp(SDNode *N) {
  EVT EltVT = N->getValueType(0).getVectorElementType();
  EVT ExtVT = cast<VTSDNode>(N->getOperand(1))->getVT().getVectorElementType();
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), EltVT,
                     LHS, DAG.getValueType(ExtVT));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VecInregOp(SDNode *N) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(0);

  EVT OpVT = Op.getValueType();
  EVT OpEltVT = OpVT.getVectorElementType();
  EVT EltVT = N->getValueType(0).getVectorElementType();

  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    Op = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, OpEltVT, Op,
        DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }

  switch (N->getOpcode()) {
  case ISD::ANY_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::ANY_EXTEND, DL, EltVT, Op);
  case ISD::SIGN_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, EltVT, Op);
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, EltVT, Op);
  }

  llvm_unreachable("Illegal extend_vector_inreg opcode");
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SCALAR_TO_VECTOR(SDNode *N) {
  // If the operand is wider than the vector element type then it is implicitly
  // truncated.  Make that explicit here.
  EVT EltVT = N->getValueType(0).getVectorElementType();
  SDValue InOp = N->getOperand(0);
  if (InOp.getValueType() != EltVT)
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, InOp);
  return InOp;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VSELECT(SDNode *N) {
  SDValue Cond = N->getOperand(0);
  EVT OpVT = Cond.getValueType();
  SDLoc DL(N);
  // The vselect result and true/value operands needs scalarizing, but it's
  // not a given that the Cond does. For instance, in AVX512 v1i1 is legal.
  // See the similar logic in ScalarizeVecRes_SETCC
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Cond = GetScalarizedVector(Cond);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Cond = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, VT, Cond,
        DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }

  SDValue LHS = GetScalarizedVector(N->getOperand(1));
  TargetLowering::BooleanContent ScalarBool =
      TLI.getBooleanContents(false, false);
  TargetLowering::BooleanContent VecBool = TLI.getBooleanContents(true, false);

  // If integer and float booleans have different contents then we can't
  // reliably optimize in all cases. There is a full explanation for this in
  // DAGCombiner::visitSELECT() where the same issue affects folding
  // (select C, 0, 1) to (xor C, 1).
  if (TLI.getBooleanContents(false, false) !=
      TLI.getBooleanContents(false, true)) {
    // At least try the common case where the boolean is generated by a
    // comparison.
    if (Cond->getOpcode() == ISD::SETCC) {
      EVT OpVT = Cond->getOperand(0).getValueType();
      ScalarBool = TLI.getBooleanContents(OpVT.getScalarType());
      VecBool = TLI.getBooleanContents(OpVT);
    } else
      ScalarBool = TargetLowering::UndefinedBooleanContent;
  }

  EVT CondVT = Cond.getValueType();
  if (ScalarBool != VecBool) {
    switch (ScalarBool) {
      case TargetLowering::UndefinedBooleanContent:
        break;
      case TargetLowering::ZeroOrOneBooleanContent:
        assert(VecBool == TargetLowering::UndefinedBooleanContent ||
               VecBool == TargetLowering::ZeroOrNegativeOneBooleanContent);
        // Vector read from all ones, scalar expects a single 1 so mask.
        Cond = DAG.getNode(ISD::AND, SDLoc(N), CondVT,
                           Cond, DAG.getConstant(1, SDLoc(N), CondVT));
        break;
      case TargetLowering::ZeroOrNegativeOneBooleanContent:
        assert(VecBool == TargetLowering::UndefinedBooleanContent ||
               VecBool == TargetLowering::ZeroOrOneBooleanContent);
        // Vector reads from a one, scalar from all ones so sign extend.
        Cond = DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), CondVT,
                           Cond, DAG.getValueType(MVT::i1));
        break;
    }
  }

  // Truncate the condition if needed
  auto BoolVT = getSetCCResultType(CondVT);
  if (BoolVT.bitsLT(CondVT))
    Cond = DAG.getNode(ISD::TRUNCATE, SDLoc(N), BoolVT, Cond);

  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), Cond, LHS,
                       GetScalarizedVector(N->getOperand(2)));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SELECT(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(1));
  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), N->getOperand(0), LHS,
                       GetScalarizedVector(N->getOperand(2)));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SELECT_CC(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(2));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), LHS.getValueType(),
                     N->getOperand(0), N->getOperand(1),
                     LHS, GetScalarizedVector(N->getOperand(3)),
                     N->getOperand(4));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(N->getValueType(0).getVectorElementType());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VECTOR_SHUFFLE(SDNode *N) {
  // Figure out if the scalar is the LHS or RHS and return it.
  SDValue Arg = N->getOperand(2).getOperand(0);
  if (Arg.isUndef())
    return DAG.getUNDEF(N->getValueType(0).getVectorElementType());
  unsigned Op = !cast<ConstantSDNode>(Arg)->isNullValue();
  return GetScalarizedVector(N->getOperand(Op));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  EVT OpVT = LHS.getValueType();
  EVT NVT = N->getValueType(0).getVectorElementType();
  SDLoc DL(N);

  // The result needs scalarizing, but it's not a given that the source does.
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    LHS = GetScalarizedVector(LHS);
    RHS = GetScalarizedVector(RHS);
  } else {
    EVT VT = OpVT.getVectorElementType();
    LHS = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, VT, LHS,
        DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
    RHS = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, VT, RHS,
        DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }

  // Turn it into a scalar SETCC.
  SDValue Res = DAG.getNode(ISD::SETCC, DL, MVT::i1, LHS, RHS,
                            N->getOperand(2));
  // Vectors may have a different boolean contents to scalars.  Promote the
  // value appropriately.
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));
  return DAG.getNode(ExtendCode, DL, NVT, Res);
}


//===----------------------------------------------------------------------===//
//  Operand Vector Scalarization <1 x ty> -> ty.
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::ScalarizeVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Scalarize node operand " << OpNo << ": "; N->dump(&DAG);
             dbgs() << "\n");
  SDValue Res = SDValue();

  if (!Res.getNode()) {
    switch (N->getOpcode()) {
    default:
#ifndef NDEBUG
      dbgs() << "ScalarizeVectorOperand Op #" << OpNo << ": ";
      N->dump(&DAG);
      dbgs() << "\n";
#endif
      report_fatal_error("Do not know how to scalarize this operator's "
                         "operand!\n");
    case ISD::BITCAST:
      Res = ScalarizeVecOp_BITCAST(N);
      break;
    case ISD::ANY_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::TRUNCATE:
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:
      Res = ScalarizeVecOp_UnaryOp(N);
      break;
    case ISD::CONCAT_VECTORS:
      Res = ScalarizeVecOp_CONCAT_VECTORS(N);
      break;
    case ISD::EXTRACT_VECTOR_ELT:
      Res = ScalarizeVecOp_EXTRACT_VECTOR_ELT(N);
      break;
    case ISD::VSELECT:
      Res = ScalarizeVecOp_VSELECT(N);
      break;
    case ISD::SETCC:
      Res = ScalarizeVecOp_VSETCC(N);
      break;
    case ISD::STORE:
      Res = ScalarizeVecOp_STORE(cast<StoreSDNode>(N), OpNo);
      break;
    case ISD::FP_ROUND:
      Res = ScalarizeVecOp_FP_ROUND(N, OpNo);
      break;
    }
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

/// If the value to convert is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_BITCAST(SDNode *N) {
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(ISD::BITCAST, SDLoc(N),
                     N->getValueType(0), Elt);
}

/// If the input is a vector that needs to be scalarized, it must be <1 x ty>.
/// Do the operation on the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_UnaryOp(SDNode *N) {
  assert(N->getValueType(0).getVectorNumElements() == 1 &&
         "Unexpected vector type!");
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  SDValue Op = DAG.getNode(N->getOpcode(), SDLoc(N),
                           N->getValueType(0).getScalarType(), Elt);
  // Revectorize the result so the types line up with what the uses of this
  // expression expect.
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Op);
}

/// The vectors to concatenate have length one - use a BUILD_VECTOR instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_CONCAT_VECTORS(SDNode *N) {
  SmallVector<SDValue, 8> Ops(N->getNumOperands());
  for (unsigned i = 0, e = N->getNumOperands(); i < e; ++i)
    Ops[i] = GetScalarizedVector(N->getOperand(i));
  return DAG.getBuildVector(N->getValueType(0), SDLoc(N), Ops);
}

/// If the input is a vector that needs to be scalarized, it must be <1 x ty>,
/// so just return the element, ignoring the index.
SDValue DAGTypeLegalizer::ScalarizeVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue Res = GetScalarizedVector(N->getOperand(0));
  if (Res.getValueType() != VT)
    Res = VT.isFloatingPoint()
              ? DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, Res)
              : DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), VT, Res);
  return Res;
}

/// If the input condition is a vector that needs to be scalarized, it must be
/// <1 x i1>, so just convert to a normal ISD::SELECT
/// (still with vector output type since that was acceptable if we got here).
SDValue DAGTypeLegalizer::ScalarizeVecOp_VSELECT(SDNode *N) {
  SDValue ScalarCond = GetScalarizedVector(N->getOperand(0));
  EVT VT = N->getValueType(0);

  return DAG.getNode(ISD::SELECT, SDLoc(N), VT, ScalarCond, N->getOperand(1),
                     N->getOperand(2));
}

/// If the operand is a vector that needs to be scalarized then the
/// result must be v1i1, so just convert to a scalar SETCC and wrap
/// with a scalar_to_vector since the res type is legal if we got here
SDValue DAGTypeLegalizer::ScalarizeVecOp_VSETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");
  assert(N->getValueType(0) == MVT::v1i1 && "Expected v1i1 type");

  EVT VT = N->getValueType(0);
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  SDValue RHS = GetScalarizedVector(N->getOperand(1));

  EVT OpVT = N->getOperand(0).getValueType();
  EVT NVT = VT.getVectorElementType();
  SDLoc DL(N);
  // Turn it into a scalar SETCC.
  SDValue Res = DAG.getNode(ISD::SETCC, DL, MVT::i1, LHS, RHS,
      N->getOperand(2));

  // Vectors may have a different boolean contents to scalars.  Promote the
  // value appropriately.
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));

  Res = DAG.getNode(ExtendCode, DL, NVT, Res);

  return DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, VT, Res);
}

/// If the value to store is a vector that needs to be scalarized, it must be
/// <1 x ty>. Just store the element.
SDValue DAGTypeLegalizer::ScalarizeVecOp_STORE(StoreSDNode *N, unsigned OpNo){
  assert(N->isUnindexed() && "Indexed store of one-element vector?");
  assert(OpNo == 1 && "Do not know how to scalarize this operand!");
  SDLoc dl(N);

  if (N->isTruncatingStore())
    return DAG.getTruncStore(
        N->getChain(), dl, GetScalarizedVector(N->getOperand(1)),
        N->getBasePtr(), N->getPointerInfo(),
        N->getMemoryVT().getVectorElementType(), N->getAlignment(),
        N->getMemOperand()->getFlags(), N->getAAInfo());

  return DAG.getStore(N->getChain(), dl, GetScalarizedVector(N->getOperand(1)),
                      N->getBasePtr(), N->getPointerInfo(),
                      N->getOriginalAlignment(), N->getMemOperand()->getFlags(),
                      N->getAAInfo());
}

/// If the value to round is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_FP_ROUND(SDNode *N, unsigned OpNo) {
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  SDValue Res = DAG.getNode(ISD::FP_ROUND, SDLoc(N),
                            N->getValueType(0).getVectorElementType(), Elt,
                            N->getOperand(1));
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);
}

//===----------------------------------------------------------------------===//
//  Result Vector Splitting
//===----------------------------------------------------------------------===//

/// This method is called when the specified result of the specified node is
/// found to need vector splitting. At this point, the node may also have
/// invalid operands or may have other results that need legalization, we just
/// know that (at least) one result needs vector splitting.
void DAGTypeLegalizer::SplitVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Split node result: "; N->dump(&DAG); dbgs() << "\n");
  SDValue Lo, Hi;

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true))
    return;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SplitVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to split the result of this "
                       "operator!\n");

  case ISD::MERGE_VALUES: SplitRes_MERGE_VALUES(N, ResNo, Lo, Hi); break;
  case ISD::VSELECT:
  case ISD::SELECT:       SplitRes_SELECT(N, Lo, Hi); break;
  case ISD::SELECT_CC:    SplitRes_SELECT_CC(N, Lo, Hi); break;
  case ISD::UNDEF:        SplitRes_UNDEF(N, Lo, Hi); break;
  case ISD::BITCAST:           SplitVecRes_BITCAST(N, Lo, Hi); break;
  case ISD::BUILD_VECTOR:      SplitVecRes_BUILD_VECTOR(N, Lo, Hi); break;
  case ISD::CONCAT_VECTORS:    SplitVecRes_CONCAT_VECTORS(N, Lo, Hi); break;
  case ISD::EXTRACT_SUBVECTOR: SplitVecRes_EXTRACT_SUBVECTOR(N, Lo, Hi); break;
  case ISD::INSERT_SUBVECTOR:  SplitVecRes_INSERT_SUBVECTOR(N, Lo, Hi); break;
  case ISD::FP_ROUND_INREG:    SplitVecRes_InregOp(N, Lo, Hi); break;
  case ISD::FPOWI:             SplitVecRes_FPOWI(N, Lo, Hi); break;
  case ISD::FCOPYSIGN:         SplitVecRes_FCOPYSIGN(N, Lo, Hi); break;
  case ISD::INSERT_VECTOR_ELT: SplitVecRes_INSERT_VECTOR_ELT(N, Lo, Hi); break;
  case ISD::SCALAR_TO_VECTOR:  SplitVecRes_SCALAR_TO_VECTOR(N, Lo, Hi); break;
  case ISD::SIGN_EXTEND_INREG: SplitVecRes_InregOp(N, Lo, Hi); break;
  case ISD::LOAD:
    SplitVecRes_LOAD(cast<LoadSDNode>(N), Lo, Hi);
    break;
  case ISD::MLOAD:
    SplitVecRes_MLOAD(cast<MaskedLoadSDNode>(N), Lo, Hi);
    break;
  case ISD::MGATHER:
    SplitVecRes_MGATHER(cast<MaskedGatherSDNode>(N), Lo, Hi);
    break;
  case ISD::SETCC:
    SplitVecRes_SETCC(N, Lo, Hi);
    break;
  case ISD::VECTOR_SHUFFLE:
    SplitVecRes_VECTOR_SHUFFLE(cast<ShuffleVectorSDNode>(N), Lo, Hi);
    break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    SplitVecRes_ExtVecInRegOp(N, Lo, Hi);
    break;

  case ISD::BITREVERSE:
  case ISD::BSWAP:
  case ISD::CTLZ:
  case ISD::CTTZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::FABS:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::FNEG:
  case ISD::FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FSIN:
  case ISD::FSQRT:
  case ISD::FTRUNC:
  case ISD::SINT_TO_FP:
  case ISD::TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::FCANONICALIZE:
    SplitVecRes_UnaryOp(N, Lo, Hi);
    break;

  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    SplitVecRes_ExtendOp(N, Lo, Hi);
    break;

  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::FDIV:
  case ISD::FPOW:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::UREM:
  case ISD::SREM:
  case ISD::FREM:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
    SplitVecRes_BinOp(N, Lo, Hi);
    break;
  case ISD::FMA:
    SplitVecRes_TernaryOp(N, Lo, Hi);
    break;
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FREM:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
  case ISD::STRICT_FPOW:
  case ISD::STRICT_FPOWI:
  case ISD::STRICT_FSIN:
  case ISD::STRICT_FCOS:
  case ISD::STRICT_FEXP:
  case ISD::STRICT_FEXP2:
  case ISD::STRICT_FLOG:
  case ISD::STRICT_FLOG10:
  case ISD::STRICT_FLOG2:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FMAXNUM:
  case ISD::STRICT_FMINNUM:
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FTRUNC:
    SplitVecRes_StrictFPOp(N, Lo, Hi);
    break;
  case ISD::SMULFIX:
    SplitVecRes_SMULFIX(N, Lo, Hi);
    break;
  }

  // If Lo/Hi is null, the sub-method took care of registering results etc.
  if (Lo.getNode())
    SetSplitVector(SDValue(N, ResNo), Lo, Hi);
}

void DAGTypeLegalizer::SplitVecRes_BinOp(SDNode *N, SDValue &Lo,
                                         SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDValue RHSLo, RHSHi;
  GetSplitVector(N->getOperand(1), RHSLo, RHSHi);
  SDLoc dl(N);

  const SDNodeFlags Flags = N->getFlags();
  unsigned Opcode = N->getOpcode();
  Lo = DAG.getNode(Opcode, dl, LHSLo.getValueType(), LHSLo, RHSLo, Flags);
  Hi = DAG.getNode(Opcode, dl, LHSHi.getValueType(), LHSHi, RHSHi, Flags);
}

void DAGTypeLegalizer::SplitVecRes_TernaryOp(SDNode *N, SDValue &Lo,
                                             SDValue &Hi) {
  SDValue Op0Lo, Op0Hi;
  GetSplitVector(N->getOperand(0), Op0Lo, Op0Hi);
  SDValue Op1Lo, Op1Hi;
  GetSplitVector(N->getOperand(1), Op1Lo, Op1Hi);
  SDValue Op2Lo, Op2Hi;
  GetSplitVector(N->getOperand(2), Op2Lo, Op2Hi);
  SDLoc dl(N);

  Lo = DAG.getNode(N->getOpcode(), dl, Op0Lo.getValueType(),
                   Op0Lo, Op1Lo, Op2Lo);
  Hi = DAG.getNode(N->getOpcode(), dl, Op0Hi.getValueType(),
                   Op0Hi, Op1Hi, Op2Hi);
}

void DAGTypeLegalizer::SplitVecRes_SMULFIX(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDValue RHSLo, RHSHi;
  GetSplitVector(N->getOperand(1), RHSLo, RHSHi);
  SDLoc dl(N);
  SDValue Op2 = N->getOperand(2);

  unsigned Opcode = N->getOpcode();
  Lo = DAG.getNode(Opcode, dl, LHSLo.getValueType(), LHSLo, RHSLo, Op2);
  Hi = DAG.getNode(Opcode, dl, LHSHi.getValueType(), LHSHi, RHSHi, Op2);
}

void DAGTypeLegalizer::SplitVecRes_BITCAST(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  // We know the result is a vector.  The input may be either a vector or a
  // scalar value.
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  SDLoc dl(N);

  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();

  // Handle some special cases efficiently.
  switch (getTypeAction(InVT)) {
  case TargetLowering::TypeLegal:
  case TargetLowering::TypePromoteInteger:
  case TargetLowering::TypePromoteFloat:
  case TargetLowering::TypeSoftenFloat:
  case TargetLowering::TypeScalarizeVector:
  case TargetLowering::TypeWidenVector:
    break;
  case TargetLowering::TypeExpandInteger:
  case TargetLowering::TypeExpandFloat:
    // A scalar to vector conversion, where the scalar needs expansion.
    // If the vector is being split in two then we can just convert the
    // expanded pieces.
    if (LoVT == HiVT) {
      GetExpandedOp(InOp, Lo, Hi);
      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
      return;
    }
    break;
  case TargetLowering::TypeSplitVector:
    // If the input is a vector that needs to be split, convert each split
    // piece of the input now.
    GetSplitVector(InOp, Lo, Hi);
    Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
    Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
    return;
  }

  // In the general case, convert the input to an integer and split it by hand.
  EVT LoIntVT = EVT::getIntegerVT(*DAG.getContext(), LoVT.getSizeInBits());
  EVT HiIntVT = EVT::getIntegerVT(*DAG.getContext(), HiVT.getSizeInBits());
  if (DAG.getDataLayout().isBigEndian())
    std::swap(LoIntVT, HiIntVT);

  SplitInteger(BitConvertToInteger(InOp), LoIntVT, HiIntVT, Lo, Hi);

  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);
  Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
  Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
}

void DAGTypeLegalizer::SplitVecRes_BUILD_VECTOR(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  unsigned LoNumElts = LoVT.getVectorNumElements();
  SmallVector<SDValue, 8> LoOps(N->op_begin(), N->op_begin()+LoNumElts);
  Lo = DAG.getBuildVector(LoVT, dl, LoOps);

  SmallVector<SDValue, 8> HiOps(N->op_begin()+LoNumElts, N->op_end());
  Hi = DAG.getBuildVector(HiVT, dl, HiOps);
}

void DAGTypeLegalizer::SplitVecRes_CONCAT_VECTORS(SDNode *N, SDValue &Lo,
                                                  SDValue &Hi) {
  assert(!(N->getNumOperands() & 1) && "Unsupported CONCAT_VECTORS");
  SDLoc dl(N);
  unsigned NumSubvectors = N->getNumOperands() / 2;
  if (NumSubvectors == 1) {
    Lo = N->getOperand(0);
    Hi = N->getOperand(1);
    return;
  }

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  SmallVector<SDValue, 8> LoOps(N->op_begin(), N->op_begin()+NumSubvectors);
  Lo = DAG.getNode(ISD::CONCAT_VECTORS, dl, LoVT, LoOps);

  SmallVector<SDValue, 8> HiOps(N->op_begin()+NumSubvectors, N->op_end());
  Hi = DAG.getNode(ISD::CONCAT_VECTORS, dl, HiVT, HiOps);
}

void DAGTypeLegalizer::SplitVecRes_EXTRACT_SUBVECTOR(SDNode *N, SDValue &Lo,
                                                     SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue Idx = N->getOperand(1);
  SDLoc dl(N);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, LoVT, Vec, Idx);
  uint64_t IdxVal = cast<ConstantSDNode>(Idx)->getZExtValue();
  Hi = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, HiVT, Vec,
                   DAG.getConstant(IdxVal + LoVT.getVectorNumElements(), dl,
                                   TLI.getVectorIdxTy(DAG.getDataLayout())));
}

void DAGTypeLegalizer::SplitVecRes_INSERT_SUBVECTOR(SDNode *N, SDValue &Lo,
                                                    SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue SubVec = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);
  GetSplitVector(Vec, Lo, Hi);

  EVT VecVT = Vec.getValueType();
  unsigned VecElems = VecVT.getVectorNumElements();
  unsigned SubElems = SubVec.getValueType().getVectorNumElements();

  // If we know the index is 0, and we know the subvector doesn't cross the
  // boundary between the halves, we can avoid spilling the vector, and insert
  // into the lower half of the split vector directly.
  // TODO: The IdxVal == 0 constraint is artificial, we could do this whenever
  // the index is constant and there is no boundary crossing. But those cases
  // don't seem to get hit in practice.
  if (ConstantSDNode *ConstIdx = dyn_cast<ConstantSDNode>(Idx)) {
    unsigned IdxVal = ConstIdx->getZExtValue();
    if ((IdxVal == 0) && (IdxVal + SubElems <= VecElems / 2)) {
      EVT LoVT, HiVT;
      std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
      Lo = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, LoVT, Lo, SubVec, Idx);
      return;
    }
  }

  // Spill the vector to the stack.
  SDValue StackPtr = DAG.CreateStackTemporary(VecVT);
  SDValue Store =
      DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, MachinePointerInfo());

  // Store the new subvector into the specified index.
  SDValue SubVecPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);
  Type *VecType = VecVT.getTypeForEVT(*DAG.getContext());
  unsigned Alignment = DAG.getDataLayout().getPrefTypeAlignment(VecType);
  Store = DAG.getStore(Store, dl, SubVec, SubVecPtr, MachinePointerInfo());

  // Load the Lo part from the stack slot.
  Lo =
      DAG.getLoad(Lo.getValueType(), dl, Store, StackPtr, MachinePointerInfo());

  // Increment the pointer to the other part.
  unsigned IncrementSize = Lo.getValueSizeInBits() / 8;
  StackPtr =
      DAG.getNode(ISD::ADD, dl, StackPtr.getValueType(), StackPtr,
                  DAG.getConstant(IncrementSize, dl, StackPtr.getValueType()));

  // Load the Hi part from the stack slot.
  Hi = DAG.getLoad(Hi.getValueType(), dl, Store, StackPtr, MachinePointerInfo(),
                   MinAlign(Alignment, IncrementSize));
}

void DAGTypeLegalizer::SplitVecRes_FPOWI(SDNode *N, SDValue &Lo,
                                         SDValue &Hi) {
  SDLoc dl(N);
  GetSplitVector(N->getOperand(0), Lo, Hi);
  Lo = DAG.getNode(ISD::FPOWI, dl, Lo.getValueType(), Lo, N->getOperand(1));
  Hi = DAG.getNode(ISD::FPOWI, dl, Hi.getValueType(), Hi, N->getOperand(1));
}

void DAGTypeLegalizer::SplitVecRes_FCOPYSIGN(SDNode *N, SDValue &Lo,
                                             SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDLoc DL(N);

  SDValue RHSLo, RHSHi;
  SDValue RHS = N->getOperand(1);
  EVT RHSVT = RHS.getValueType();
  if (getTypeAction(RHSVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(RHS, RHSLo, RHSHi);
  else
    std::tie(RHSLo, RHSHi) = DAG.SplitVector(RHS, SDLoc(RHS));


  Lo = DAG.getNode(ISD::FCOPYSIGN, DL, LHSLo.getValueType(), LHSLo, RHSLo);
  Hi = DAG.getNode(ISD::FCOPYSIGN, DL, LHSHi.getValueType(), LHSHi, RHSHi);
}

void DAGTypeLegalizer::SplitVecRes_InregOp(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDLoc dl(N);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) =
    DAG.GetSplitDestVTs(cast<VTSDNode>(N->getOperand(1))->getVT());

  Lo = DAG.getNode(N->getOpcode(), dl, LHSLo.getValueType(), LHSLo,
                   DAG.getValueType(LoVT));
  Hi = DAG.getNode(N->getOpcode(), dl, LHSHi.getValueType(), LHSHi,
                   DAG.getValueType(HiVT));
}

void DAGTypeLegalizer::SplitVecRes_ExtVecInRegOp(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);

  SDLoc dl(N);
  SDValue InLo, InHi;

  if (getTypeAction(N0.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(N0, InLo, InHi);
  else
    std::tie(InLo, InHi) = DAG.SplitVectorOperand(N, 0);

  EVT InLoVT = InLo.getValueType();
  unsigned InNumElements = InLoVT.getVectorNumElements();

  EVT OutLoVT, OutHiVT;
  std::tie(OutLoVT, OutHiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  unsigned OutNumElements = OutLoVT.getVectorNumElements();
  assert((2 * OutNumElements) <= InNumElements &&
         "Illegal extend vector in reg split");

  // *_EXTEND_VECTOR_INREG instructions extend the lowest elements of the
  // input vector (i.e. we only use InLo):
  // OutLo will extend the first OutNumElements from InLo.
  // OutHi will extend the next OutNumElements from InLo.

  // Shuffle the elements from InLo for OutHi into the bottom elements to
  // create a 'fake' InHi.
  SmallVector<int, 8> SplitHi(InNumElements, -1);
  for (unsigned i = 0; i != OutNumElements; ++i)
    SplitHi[i] = i + OutNumElements;
  InHi = DAG.getVectorShuffle(InLoVT, dl, InLo, DAG.getUNDEF(InLoVT), SplitHi);

  Lo = DAG.getNode(Opcode, dl, OutLoVT, InLo);
  Hi = DAG.getNode(Opcode, dl, OutHiVT, InHi);
}

void DAGTypeLegalizer::SplitVecRes_StrictFPOp(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  unsigned NumOps = N->getNumOperands();
  SDValue Chain = N->getOperand(0);
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  SmallVector<SDValue, 4> OpsLo;
  SmallVector<SDValue, 4> OpsHi;

  // The Chain is the first operand.
  OpsLo.push_back(Chain);
  OpsHi.push_back(Chain);

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOps; ++i) {
    SDValue Op = N->getOperand(i);
    SDValue OpLo = Op;
    SDValue OpHi = Op;

    EVT InVT = Op.getValueType();
    if (InVT.isVector()) {
      // If the input also splits, handle it directly for a
      // compile time speedup. Otherwise split it by hand.
      if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
        GetSplitVector(Op, OpLo, OpHi);
      else
        std::tie(OpLo, OpHi) = DAG.SplitVectorOperand(N, i);
    }

    OpsLo.push_back(OpLo);
    OpsHi.push_back(OpHi);
  }

  EVT LoValueVTs[] = {LoVT, MVT::Other};
  EVT HiValueVTs[] = {HiVT, MVT::Other};
  Lo = DAG.getNode(N->getOpcode(), dl, LoValueVTs, OpsLo);
  Hi = DAG.getNode(N->getOpcode(), dl, HiValueVTs, OpsHi);

  // Build a factor node to remember that this Op is independent of the
  // other one.
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                      Lo.getValue(1), Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}

void DAGTypeLegalizer::SplitVecRes_INSERT_VECTOR_ELT(SDNode *N, SDValue &Lo,
                                                     SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue Elt = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);
  GetSplitVector(Vec, Lo, Hi);

  if (ConstantSDNode *CIdx = dyn_cast<ConstantSDNode>(Idx)) {
    unsigned IdxVal = CIdx->getZExtValue();
    unsigned LoNumElts = Lo.getValueType().getVectorNumElements();
    if (IdxVal < LoNumElts)
      Lo = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl,
                       Lo.getValueType(), Lo, Elt, Idx);
    else
      Hi =
          DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, Hi.getValueType(), Hi, Elt,
                      DAG.getConstant(IdxVal - LoNumElts, dl,
                                      TLI.getVectorIdxTy(DAG.getDataLayout())));
    return;
  }

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(0), true))
    return;

  // Make the vector elements byte-addressable if they aren't already.
  EVT VecVT = Vec.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  if (VecVT.getScalarSizeInBits() < 8) {
    EltVT = MVT::i8;
    VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                             VecVT.getVectorNumElements());
    Vec = DAG.getNode(ISD::ANY_EXTEND, dl, VecVT, Vec);
    // Extend the element type to match if needed.
    if (EltVT.bitsGT(Elt.getValueType()))
      Elt = DAG.getNode(ISD::ANY_EXTEND, dl, EltVT, Elt);
  }

  // Spill the vector to the stack.
  SDValue StackPtr = DAG.CreateStackTemporary(VecVT);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo);

  // Store the new element.  This may be larger than the vector element type,
  // so use a truncating store.
  SDValue EltPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);
  Type *VecType = VecVT.getTypeForEVT(*DAG.getContext());
  unsigned Alignment = DAG.getDataLayout().getPrefTypeAlignment(VecType);
  Store = DAG.getTruncStore(Store, dl, Elt, EltPtr,
                            MachinePointerInfo::getUnknownStack(MF), EltVT);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VecVT);

  // Load the Lo part from the stack slot.
  Lo = DAG.getLoad(LoVT, dl, Store, StackPtr, PtrInfo);

  // Increment the pointer to the other part.
  unsigned IncrementSize = LoVT.getSizeInBits() / 8;
  StackPtr = DAG.getNode(ISD::ADD, dl, StackPtr.getValueType(), StackPtr,
                         DAG.getConstant(IncrementSize, dl,
                                         StackPtr.getValueType()));

  // Load the Hi part from the stack slot.
  Hi = DAG.getLoad(HiVT, dl, Store, StackPtr,
                   PtrInfo.getWithOffset(IncrementSize),
                   MinAlign(Alignment, IncrementSize));

  // If we adjusted the original type, we need to truncate the results.
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  if (LoVT != Lo.getValueType())
    Lo = DAG.getNode(ISD::TRUNCATE, dl, LoVT, Lo);
  if (HiVT != Hi.getValueType())
    Hi = DAG.getNode(ISD::TRUNCATE, dl, HiVT, Hi);
}

void DAGTypeLegalizer::SplitVecRes_SCALAR_TO_VECTOR(SDNode *N, SDValue &Lo,
                                                    SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  Lo = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, LoVT, N->getOperand(0));
  Hi = DAG.getUNDEF(HiVT);
}

void DAGTypeLegalizer::SplitVecRes_LOAD(LoadSDNode *LD, SDValue &Lo,
                                        SDValue &Hi) {
  assert(ISD::isUNINDEXEDLoad(LD) && "Indexed load during type legalization!");
  EVT LoVT, HiVT;
  SDLoc dl(LD);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(LD->getValueType(0));

  ISD::LoadExtType ExtType = LD->getExtensionType();
  SDValue Ch = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  SDValue Offset = DAG.getUNDEF(Ptr.getValueType());
  EVT MemoryVT = LD->getMemoryVT();
  unsigned Alignment = LD->getOriginalAlignment();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  Lo = DAG.getLoad(ISD::UNINDEXED, ExtType, LoVT, dl, Ch, Ptr, Offset,
                   LD->getPointerInfo(), LoMemVT, Alignment, MMOFlags, AAInfo);

  unsigned IncrementSize = LoMemVT.getSizeInBits()/8;
  Ptr = DAG.getObjectPtrOffset(dl, Ptr, IncrementSize);
  Hi = DAG.getLoad(ISD::UNINDEXED, ExtType, HiVT, dl, Ch, Ptr, Offset,
                   LD->getPointerInfo().getWithOffset(IncrementSize), HiMemVT,
                   Alignment, MMOFlags, AAInfo);

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(LD, 1), Ch);
}

void DAGTypeLegalizer::SplitVecRes_MLOAD(MaskedLoadSDNode *MLD,
                                         SDValue &Lo, SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(MLD);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MLD->getValueType(0));

  SDValue Ch = MLD->getChain();
  SDValue Ptr = MLD->getBasePtr();
  SDValue Mask = MLD->getMask();
  SDValue PassThru = MLD->getPassThru();
  unsigned Alignment = MLD->getOriginalAlignment();
  ISD::LoadExtType ExtType = MLD->getExtensionType();

  // if Alignment is equal to the vector size,
  // take the half of it for the second part
  unsigned SecondHalfAlignment =
    (Alignment == MLD->getValueType(0).getSizeInBits()/8) ?
     Alignment/2 : Alignment;

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, dl);

  EVT MemoryVT = MLD->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue PassThruLo, PassThruHi;
  if (getTypeAction(PassThru.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(PassThru, PassThruLo, PassThruHi);
  else
    std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MLD->getPointerInfo(),
                         MachineMemOperand::MOLoad,  LoMemVT.getStoreSize(),
                         Alignment, MLD->getAAInfo(), MLD->getRanges());

  Lo = DAG.getMaskedLoad(LoVT, dl, Ch, Ptr, MaskLo, PassThruLo, LoMemVT, MMO,
                         ExtType, MLD->isExpandingLoad());

  Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, dl, LoMemVT, DAG,
                                   MLD->isExpandingLoad());
  unsigned HiOffset = LoMemVT.getStoreSize();

  MMO = DAG.getMachineFunction().getMachineMemOperand(
      MLD->getPointerInfo().getWithOffset(HiOffset), MachineMemOperand::MOLoad,
      HiMemVT.getStoreSize(), SecondHalfAlignment, MLD->getAAInfo(),
      MLD->getRanges());

  Hi = DAG.getMaskedLoad(HiVT, dl, Ch, Ptr, MaskHi, PassThruHi, HiMemVT, MMO,
                         ExtType, MLD->isExpandingLoad());

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(MLD, 1), Ch);

}

void DAGTypeLegalizer::SplitVecRes_MGATHER(MaskedGatherSDNode *MGT,
                                         SDValue &Lo, SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(MGT);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MGT->getValueType(0));

  SDValue Ch = MGT->getChain();
  SDValue Ptr = MGT->getBasePtr();
  SDValue Mask = MGT->getMask();
  SDValue PassThru = MGT->getPassThru();
  SDValue Index = MGT->getIndex();
  SDValue Scale = MGT->getScale();
  unsigned Alignment = MGT->getOriginalAlignment();

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, dl);

  EVT MemoryVT = MGT->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  // Split MemoryVT
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue PassThruLo, PassThruHi;
  if (getTypeAction(PassThru.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(PassThru, PassThruLo, PassThruHi);
  else
    std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, dl);

  SDValue IndexHi, IndexLo;
  if (getTypeAction(Index.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Index, IndexLo, IndexHi);
  else
    std::tie(IndexLo, IndexHi) = DAG.SplitVector(Index, dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MGT->getPointerInfo(),
                         MachineMemOperand::MOLoad,  LoMemVT.getStoreSize(),
                         Alignment, MGT->getAAInfo(), MGT->getRanges());

  SDValue OpsLo[] = {Ch, PassThruLo, MaskLo, Ptr, IndexLo, Scale};
  Lo = DAG.getMaskedGather(DAG.getVTList(LoVT, MVT::Other), LoVT, dl, OpsLo,
                           MMO);

  SDValue OpsHi[] = {Ch, PassThruHi, MaskHi, Ptr, IndexHi, Scale};
  Hi = DAG.getMaskedGather(DAG.getVTList(HiVT, MVT::Other), HiVT, dl, OpsHi,
                           MMO);

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(MGT, 1), Ch);
}


void DAGTypeLegalizer::SplitVecRes_SETCC(SDNode *N, SDValue &Lo, SDValue &Hi) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");

  EVT LoVT, HiVT;
  SDLoc DL(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  // If the input also splits, handle it directly. Otherwise split it by hand.
  SDValue LL, LH, RL, RH;
  if (getTypeAction(N->getOperand(0).getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), LL, LH);
  else
    std::tie(LL, LH) = DAG.SplitVectorOperand(N, 0);

  if (getTypeAction(N->getOperand(1).getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(1), RL, RH);
  else
    std::tie(RL, RH) = DAG.SplitVectorOperand(N, 1);

  Lo = DAG.getNode(N->getOpcode(), DL, LoVT, LL, RL, N->getOperand(2));
  Hi = DAG.getNode(N->getOpcode(), DL, HiVT, LH, RH, N->getOperand(2));
}

void DAGTypeLegalizer::SplitVecRes_UnaryOp(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  // Get the dest types - they may not match the input types, e.g. int_to_fp.
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  // If the input also splits, handle it directly for a compile time speedup.
  // Otherwise split it by hand.
  EVT InVT = N->getOperand(0).getValueType();
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), Lo, Hi);
  else
    std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);

  if (N->getOpcode() == ISD::FP_ROUND) {
    Lo = DAG.getNode(N->getOpcode(), dl, LoVT, Lo, N->getOperand(1));
    Hi = DAG.getNode(N->getOpcode(), dl, HiVT, Hi, N->getOperand(1));
  } else {
    Lo = DAG.getNode(N->getOpcode(), dl, LoVT, Lo);
    Hi = DAG.getNode(N->getOpcode(), dl, HiVT, Hi);
  }
}

void DAGTypeLegalizer::SplitVecRes_ExtendOp(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  SDLoc dl(N);
  EVT SrcVT = N->getOperand(0).getValueType();
  EVT DestVT = N->getValueType(0);
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(DestVT);

  // We can do better than a generic split operation if the extend is doing
  // more than just doubling the width of the elements and the following are
  // true:
  //   - The number of vector elements is even,
  //   - the source type is legal,
  //   - the type of a split source is illegal,
  //   - the type of an extended (by doubling element size) source is legal, and
  //   - the type of that extended source when split is legal.
  //
  // This won't necessarily completely legalize the operation, but it will
  // more effectively move in the right direction and prevent falling down
  // to scalarization in many cases due to the input vector being split too
  // far.
  unsigned NumElements = SrcVT.getVectorNumElements();
  if ((NumElements & 1) == 0 &&
      SrcVT.getSizeInBits() * 2 < DestVT.getSizeInBits()) {
    LLVMContext &Ctx = *DAG.getContext();
    EVT NewSrcVT = SrcVT.widenIntegerVectorElementType(Ctx);
    EVT SplitSrcVT = SrcVT.getHalfNumVectorElementsVT(Ctx);

    EVT SplitLoVT, SplitHiVT;
    std::tie(SplitLoVT, SplitHiVT) = DAG.GetSplitDestVTs(NewSrcVT);
    if (TLI.isTypeLegal(SrcVT) && !TLI.isTypeLegal(SplitSrcVT) &&
        TLI.isTypeLegal(NewSrcVT) && TLI.isTypeLegal(SplitLoVT)) {
      LLVM_DEBUG(dbgs() << "Split vector extend via incremental extend:";
                 N->dump(&DAG); dbgs() << "\n");
      // Extend the source vector by one step.
      SDValue NewSrc =
          DAG.getNode(N->getOpcode(), dl, NewSrcVT, N->getOperand(0));
      // Get the low and high halves of the new, extended one step, vector.
      std::tie(Lo, Hi) = DAG.SplitVector(NewSrc, dl);
      // Extend those vector halves the rest of the way.
      Lo = DAG.getNode(N->getOpcode(), dl, LoVT, Lo);
      Hi = DAG.getNode(N->getOpcode(), dl, HiVT, Hi);
      return;
    }
  }
  // Fall back to the generic unary operator splitting otherwise.
  SplitVecRes_UnaryOp(N, Lo, Hi);
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_SHUFFLE(ShuffleVectorSDNode *N,
                                                  SDValue &Lo, SDValue &Hi) {
  // The low and high parts of the original input give four input vectors.
  SDValue Inputs[4];
  SDLoc dl(N);
  GetSplitVector(N->getOperand(0), Inputs[0], Inputs[1]);
  GetSplitVector(N->getOperand(1), Inputs[2], Inputs[3]);
  EVT NewVT = Inputs[0].getValueType();
  unsigned NewElts = NewVT.getVectorNumElements();

  // If Lo or Hi uses elements from at most two of the four input vectors, then
  // express it as a vector shuffle of those two inputs.  Otherwise extract the
  // input elements by hand and construct the Lo/Hi output using a BUILD_VECTOR.
  SmallVector<int, 16> Ops;
  for (unsigned High = 0; High < 2; ++High) {
    SDValue &Output = High ? Hi : Lo;

    // Build a shuffle mask for the output, discovering on the fly which
    // input vectors to use as shuffle operands (recorded in InputUsed).
    // If building a suitable shuffle vector proves too hard, then bail
    // out with useBuildVector set.
    unsigned InputUsed[2] = { -1U, -1U }; // Not yet discovered.
    unsigned FirstMaskIdx = High * NewElts;
    bool useBuildVector = false;
    for (unsigned MaskOffset = 0; MaskOffset < NewElts; ++MaskOffset) {
      // The mask element.  This indexes into the input.
      int Idx = N->getMaskElt(FirstMaskIdx + MaskOffset);

      // The input vector this mask element indexes into.
      unsigned Input = (unsigned)Idx / NewElts;

      if (Input >= array_lengthof(Inputs)) {
        // The mask element does not index into any input vector.
        Ops.push_back(-1);
        continue;
      }

      // Turn the index into an offset from the start of the input vector.
      Idx -= Input * NewElts;

      // Find or create a shuffle vector operand to hold this input.
      unsigned OpNo;
      for (OpNo = 0; OpNo < array_lengthof(InputUsed); ++OpNo) {
        if (InputUsed[OpNo] == Input) {
          // This input vector is already an operand.
          break;
        } else if (InputUsed[OpNo] == -1U) {
          // Create a new operand for this input vector.
          InputUsed[OpNo] = Input;
          break;
        }
      }

      if (OpNo >= array_lengthof(InputUsed)) {
        // More than two input vectors used!  Give up on trying to create a
        // shuffle vector.  Insert all elements into a BUILD_VECTOR instead.
        useBuildVector = true;
        break;
      }

      // Add the mask index for the new shuffle vector.
      Ops.push_back(Idx + OpNo * NewElts);
    }

    if (useBuildVector) {
      EVT EltVT = NewVT.getVectorElementType();
      SmallVector<SDValue, 16> SVOps;

      // Extract the input elements by hand.
      for (unsigned MaskOffset = 0; MaskOffset < NewElts; ++MaskOffset) {
        // The mask element.  This indexes into the input.
        int Idx = N->getMaskElt(FirstMaskIdx + MaskOffset);

        // The input vector this mask element indexes into.
        unsigned Input = (unsigned)Idx / NewElts;

        if (Input >= array_lengthof(Inputs)) {
          // The mask element is "undef" or indexes off the end of the input.
          SVOps.push_back(DAG.getUNDEF(EltVT));
          continue;
        }

        // Turn the index into an offset from the start of the input vector.
        Idx -= Input * NewElts;

        // Extract the vector element by hand.
        SVOps.push_back(DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Inputs[Input],
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout()))));
      }

      // Construct the Lo/Hi output using a BUILD_VECTOR.
      Output = DAG.getBuildVector(NewVT, dl, SVOps);
    } else if (InputUsed[0] == -1U) {
      // No input vectors were used!  The result is undefined.
      Output = DAG.getUNDEF(NewVT);
    } else {
      SDValue Op0 = Inputs[InputUsed[0]];
      // If only one input was used, use an undefined vector for the other.
      SDValue Op1 = InputUsed[1] == -1U ?
        DAG.getUNDEF(NewVT) : Inputs[InputUsed[1]];
      // At least one input vector was used.  Create a new shuffle vector.
      Output =  DAG.getVectorShuffle(NewVT, dl, Op0, Op1, Ops);
    }

    Ops.clear();
  }
}


//===----------------------------------------------------------------------===//
//  Operand Vector Splitting
//===----------------------------------------------------------------------===//

/// This method is called when the specified operand of the specified node is
/// found to need vector splitting. At this point, all of the result types of
/// the node are known to be legal, but other operands of the node may need
/// legalization as well as the specified one.
bool DAGTypeLegalizer::SplitVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Split node operand: "; N->dump(&DAG); dbgs() << "\n");
  SDValue Res = SDValue();

  // See if the target wants to custom split this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  if (!Res.getNode()) {
    switch (N->getOpcode()) {
    default:
#ifndef NDEBUG
      dbgs() << "SplitVectorOperand Op #" << OpNo << ": ";
      N->dump(&DAG);
      dbgs() << "\n";
#endif
      report_fatal_error("Do not know how to split this operator's "
                         "operand!\n");

    case ISD::SETCC:             Res = SplitVecOp_VSETCC(N); break;
    case ISD::BITCAST:           Res = SplitVecOp_BITCAST(N); break;
    case ISD::EXTRACT_SUBVECTOR: Res = SplitVecOp_EXTRACT_SUBVECTOR(N); break;
    case ISD::EXTRACT_VECTOR_ELT:Res = SplitVecOp_EXTRACT_VECTOR_ELT(N); break;
    case ISD::CONCAT_VECTORS:    Res = SplitVecOp_CONCAT_VECTORS(N); break;
    case ISD::TRUNCATE:
      Res = SplitVecOp_TruncateHelper(N);
      break;
    case ISD::FP_ROUND:          Res = SplitVecOp_FP_ROUND(N); break;
    case ISD::FCOPYSIGN:         Res = SplitVecOp_FCOPYSIGN(N); break;
    case ISD::STORE:
      Res = SplitVecOp_STORE(cast<StoreSDNode>(N), OpNo);
      break;
    case ISD::MSTORE:
      Res = SplitVecOp_MSTORE(cast<MaskedStoreSDNode>(N), OpNo);
      break;
    case ISD::MSCATTER:
      Res = SplitVecOp_MSCATTER(cast<MaskedScatterSDNode>(N), OpNo);
      break;
    case ISD::MGATHER:
      Res = SplitVecOp_MGATHER(cast<MaskedGatherSDNode>(N), OpNo);
      break;
    case ISD::VSELECT:
      Res = SplitVecOp_VSELECT(N, OpNo);
      break;
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:
      if (N->getValueType(0).bitsLT(N->getOperand(0).getValueType()))
        Res = SplitVecOp_TruncateHelper(N);
      else
        Res = SplitVecOp_UnaryOp(N);
      break;
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
    case ISD::CTTZ:
    case ISD::CTLZ:
    case ISD::CTPOP:
    case ISD::FP_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::ANY_EXTEND:
    case ISD::FTRUNC:
    case ISD::FCANONICALIZE:
      Res = SplitVecOp_UnaryOp(N);
      break;

    case ISD::ANY_EXTEND_VECTOR_INREG:
    case ISD::SIGN_EXTEND_VECTOR_INREG:
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      Res = SplitVecOp_ExtVecInRegOp(N);
      break;

    case ISD::VECREDUCE_FADD:
    case ISD::VECREDUCE_FMUL:
    case ISD::VECREDUCE_ADD:
    case ISD::VECREDUCE_MUL:
    case ISD::VECREDUCE_AND:
    case ISD::VECREDUCE_OR:
    case ISD::VECREDUCE_XOR:
    case ISD::VECREDUCE_SMAX:
    case ISD::VECREDUCE_SMIN:
    case ISD::VECREDUCE_UMAX:
    case ISD::VECREDUCE_UMIN:
    case ISD::VECREDUCE_FMAX:
    case ISD::VECREDUCE_FMIN:
      Res = SplitVecOp_VECREDUCE(N, OpNo);
      break;
    }
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

SDValue DAGTypeLegalizer::SplitVecOp_VSELECT(SDNode *N, unsigned OpNo) {
  // The only possibility for an illegal operand is the mask, since result type
  // legalization would have handled this node already otherwise.
  assert(OpNo == 0 && "Illegal operand must be mask");

  SDValue Mask = N->getOperand(0);
  SDValue Src0 = N->getOperand(1);
  SDValue Src1 = N->getOperand(2);
  EVT Src0VT = Src0.getValueType();
  SDLoc DL(N);
  assert(Mask.getValueType().isVector() && "VSELECT without a vector mask?");

  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(0), Lo, Hi);
  assert(Lo.getValueType() == Hi.getValueType() &&
         "Lo and Hi have differing types");

  EVT LoOpVT, HiOpVT;
  std::tie(LoOpVT, HiOpVT) = DAG.GetSplitDestVTs(Src0VT);
  assert(LoOpVT == HiOpVT && "Asymmetric vector split?");

  SDValue LoOp0, HiOp0, LoOp1, HiOp1, LoMask, HiMask;
  std::tie(LoOp0, HiOp0) = DAG.SplitVector(Src0, DL);
  std::tie(LoOp1, HiOp1) = DAG.SplitVector(Src1, DL);
  std::tie(LoMask, HiMask) = DAG.SplitVector(Mask, DL);

  SDValue LoSelect =
    DAG.getNode(ISD::VSELECT, DL, LoOpVT, LoMask, LoOp0, LoOp1);
  SDValue HiSelect =
    DAG.getNode(ISD::VSELECT, DL, HiOpVT, HiMask, HiOp0, HiOp1);

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, Src0VT, LoSelect, HiSelect);
}

SDValue DAGTypeLegalizer::SplitVecOp_VECREDUCE(SDNode *N, unsigned OpNo) {
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);

  SDValue VecOp = N->getOperand(OpNo);
  EVT VecVT = VecOp.getValueType();
  assert(VecVT.isVector() && "Can only split reduce vector operand");
  GetSplitVector(VecOp, Lo, Hi);
  EVT LoOpVT, HiOpVT;
  std::tie(LoOpVT, HiOpVT) = DAG.GetSplitDestVTs(VecVT);

  bool NoNaN = N->getFlags().hasNoNaNs();
  unsigned CombineOpc = 0;
  switch (N->getOpcode()) {
  case ISD::VECREDUCE_FADD: CombineOpc = ISD::FADD; break;
  case ISD::VECREDUCE_FMUL: CombineOpc = ISD::FMUL; break;
  case ISD::VECREDUCE_ADD:  CombineOpc = ISD::ADD; break;
  case ISD::VECREDUCE_MUL:  CombineOpc = ISD::MUL; break;
  case ISD::VECREDUCE_AND:  CombineOpc = ISD::AND; break;
  case ISD::VECREDUCE_OR:   CombineOpc = ISD::OR; break;
  case ISD::VECREDUCE_XOR:  CombineOpc = ISD::XOR; break;
  case ISD::VECREDUCE_SMAX: CombineOpc = ISD::SMAX; break;
  case ISD::VECREDUCE_SMIN: CombineOpc = ISD::SMIN; break;
  case ISD::VECREDUCE_UMAX: CombineOpc = ISD::UMAX; break;
  case ISD::VECREDUCE_UMIN: CombineOpc = ISD::UMIN; break;
  case ISD::VECREDUCE_FMAX:
    CombineOpc = NoNaN ? ISD::FMAXNUM : ISD::FMAXIMUM;
    break;
  case ISD::VECREDUCE_FMIN:
    CombineOpc = NoNaN ? ISD::FMINNUM : ISD::FMINIMUM;
    break;
  default:
    llvm_unreachable("Unexpected reduce ISD node");
  }

  // Use the appropriate scalar instruction on the split subvectors before
  // reducing the now partially reduced smaller vector.
  SDValue Partial = DAG.getNode(CombineOpc, dl, LoOpVT, Lo, Hi, N->getFlags());
  return DAG.getNode(N->getOpcode(), dl, ResVT, Partial, N->getFlags());
}

SDValue DAGTypeLegalizer::SplitVecOp_UnaryOp(SDNode *N) {
  // The result has a legal vector type, but the input needs splitting.
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);
  GetSplitVector(N->getOperand(0), Lo, Hi);
  EVT InVT = Lo.getValueType();

  EVT OutVT = EVT::getVectorVT(*DAG.getContext(), ResVT.getVectorElementType(),
                               InVT.getVectorNumElements());

  Lo = DAG.getNode(N->getOpcode(), dl, OutVT, Lo);
  Hi = DAG.getNode(N->getOpcode(), dl, OutVT, Hi);

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, ResVT, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_BITCAST(SDNode *N) {
  // For example, i64 = BITCAST v4i16 on alpha.  Typically the vector will
  // end up being split all the way down to individual components.  Convert the
  // split pieces into integers and reassemble.
  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(0), Lo, Hi);
  Lo = BitConvertToInteger(Lo);
  Hi = BitConvertToInteger(Hi);

  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);

  return DAG.getNode(ISD::BITCAST, SDLoc(N), N->getValueType(0),
                     JoinIntegers(Lo, Hi));
}

SDValue DAGTypeLegalizer::SplitVecOp_EXTRACT_SUBVECTOR(SDNode *N) {
  // We know that the extracted result type is legal.
  EVT SubVT = N->getValueType(0);
  SDValue Idx = N->getOperand(1);
  SDLoc dl(N);
  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(0), Lo, Hi);

  uint64_t LoElts = Lo.getValueType().getVectorNumElements();
  uint64_t IdxVal = cast<ConstantSDNode>(Idx)->getZExtValue();

  if (IdxVal < LoElts) {
    assert(IdxVal + SubVT.getVectorNumElements() <= LoElts &&
           "Extracted subvector crosses vector split!");
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SubVT, Lo, Idx);
  } else {
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SubVT, Hi,
                       DAG.getConstant(IdxVal - LoElts, dl,
                                       Idx.getValueType()));
  }
}

SDValue DAGTypeLegalizer::SplitVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue Vec = N->getOperand(0);
  SDValue Idx = N->getOperand(1);
  EVT VecVT = Vec.getValueType();

  if (isa<ConstantSDNode>(Idx)) {
    uint64_t IdxVal = cast<ConstantSDNode>(Idx)->getZExtValue();
    assert(IdxVal < VecVT.getVectorNumElements() && "Invalid vector index!");

    SDValue Lo, Hi;
    GetSplitVector(Vec, Lo, Hi);

    uint64_t LoElts = Lo.getValueType().getVectorNumElements();

    if (IdxVal < LoElts)
      return SDValue(DAG.UpdateNodeOperands(N, Lo, Idx), 0);
    return SDValue(DAG.UpdateNodeOperands(N, Hi,
                                  DAG.getConstant(IdxVal - LoElts, SDLoc(N),
                                                  Idx.getValueType())), 0);
  }

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(0), true))
    return SDValue();

  // Make the vector elements byte-addressable if they aren't already.
  SDLoc dl(N);
  EVT EltVT = VecVT.getVectorElementType();
  if (VecVT.getScalarSizeInBits() < 8) {
    EltVT = MVT::i8;
    VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                             VecVT.getVectorNumElements());
    Vec = DAG.getNode(ISD::ANY_EXTEND, dl, VecVT, Vec);
  }

  // Store the vector to the stack.
  SDValue StackPtr = DAG.CreateStackTemporary(VecVT);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo);

  // Load back the required element.
  StackPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);

  // FIXME: This is to handle i1 vectors with elements promoted to i8.
  // i1 vector handling needs general improvement.
  if (N->getValueType(0).bitsLT(EltVT)) {
    SDValue Load = DAG.getLoad(EltVT, dl, Store, StackPtr,
      MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
    return DAG.getZExtOrTrunc(Load, dl, N->getValueType(0));
  }

  return DAG.getExtLoad(
      ISD::EXTLOAD, dl, N->getValueType(0), Store, StackPtr,
      MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()), EltVT);
}

SDValue DAGTypeLegalizer::SplitVecOp_ExtVecInRegOp(SDNode *N) {
  SDValue Lo, Hi;

  // *_EXTEND_VECTOR_INREG only reference the lower half of the input, so
  // splitting the result has the same effect as splitting the input operand.
  SplitVecRes_ExtVecInRegOp(N, Lo, Hi);

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), N->getValueType(0), Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_MGATHER(MaskedGatherSDNode *MGT,
                                             unsigned OpNo) {
  EVT LoVT, HiVT;
  SDLoc dl(MGT);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MGT->getValueType(0));

  SDValue Ch = MGT->getChain();
  SDValue Ptr = MGT->getBasePtr();
  SDValue Index = MGT->getIndex();
  SDValue Scale = MGT->getScale();
  SDValue Mask = MGT->getMask();
  SDValue PassThru = MGT->getPassThru();
  unsigned Alignment = MGT->getOriginalAlignment();

  SDValue MaskLo, MaskHi;
  if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Mask operand
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, dl);

  EVT MemoryVT = MGT->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue PassThruLo, PassThruHi;
  if (getTypeAction(PassThru.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(PassThru, PassThruLo, PassThruHi);
  else
    std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, dl);

  SDValue IndexHi, IndexLo;
  if (getTypeAction(Index.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Index, IndexLo, IndexHi);
  else
    std::tie(IndexLo, IndexHi) = DAG.SplitVector(Index, dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MGT->getPointerInfo(),
                         MachineMemOperand::MOLoad,  LoMemVT.getStoreSize(),
                         Alignment, MGT->getAAInfo(), MGT->getRanges());

  SDValue OpsLo[] = {Ch, PassThruLo, MaskLo, Ptr, IndexLo, Scale};
  SDValue Lo = DAG.getMaskedGather(DAG.getVTList(LoVT, MVT::Other), LoVT, dl,
                                   OpsLo, MMO);

  MMO = DAG.getMachineFunction().
    getMachineMemOperand(MGT->getPointerInfo(),
                         MachineMemOperand::MOLoad,  HiMemVT.getStoreSize(),
                         Alignment, MGT->getAAInfo(),
                         MGT->getRanges());

  SDValue OpsHi[] = {Ch, PassThruHi, MaskHi, Ptr, IndexHi, Scale};
  SDValue Hi = DAG.getMaskedGather(DAG.getVTList(HiVT, MVT::Other), HiVT, dl,
                                   OpsHi, MMO);

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(MGT, 1), Ch);

  SDValue Res = DAG.getNode(ISD::CONCAT_VECTORS, dl, MGT->getValueType(0), Lo,
                            Hi);
  ReplaceValueWith(SDValue(MGT, 0), Res);
  return SDValue();
}

SDValue DAGTypeLegalizer::SplitVecOp_MSTORE(MaskedStoreSDNode *N,
                                            unsigned OpNo) {
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  SDValue Mask = N->getMask();
  SDValue Data = N->getValue();
  EVT MemoryVT = N->getMemoryVT();
  unsigned Alignment = N->getOriginalAlignment();
  SDLoc DL(N);

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue DataLo, DataHi;
  if (getTypeAction(Data.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Data operand
    GetSplitVector(Data, DataLo, DataHi);
  else
    std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

  SDValue MaskLo, MaskHi;
  if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Mask operand
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, DL);

  // if Alignment is equal to the vector size,
  // take the half of it for the second part
  unsigned SecondHalfAlignment =
    (Alignment == Data->getValueType(0).getSizeInBits()/8) ?
       Alignment/2 : Alignment;

  SDValue Lo, Hi;
  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(N->getPointerInfo(),
                         MachineMemOperand::MOStore, LoMemVT.getStoreSize(),
                         Alignment, N->getAAInfo(), N->getRanges());

  Lo = DAG.getMaskedStore(Ch, DL, DataLo, Ptr, MaskLo, LoMemVT, MMO,
                          N->isTruncatingStore(),
                          N->isCompressingStore());

  Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, DL, LoMemVT, DAG,
                                   N->isCompressingStore());
  unsigned HiOffset = LoMemVT.getStoreSize();

  MMO = DAG.getMachineFunction().getMachineMemOperand(
      N->getPointerInfo().getWithOffset(HiOffset), MachineMemOperand::MOStore,
      HiMemVT.getStoreSize(), SecondHalfAlignment, N->getAAInfo(),
      N->getRanges());

  Hi = DAG.getMaskedStore(Ch, DL, DataHi, Ptr, MaskHi, HiMemVT, MMO,
                          N->isTruncatingStore(), N->isCompressingStore());

  // Build a factor node to remember that this store is independent of the
  // other one.
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_MSCATTER(MaskedScatterSDNode *N,
                                              unsigned OpNo) {
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  SDValue Mask = N->getMask();
  SDValue Index = N->getIndex();
  SDValue Scale = N->getScale();
  SDValue Data = N->getValue();
  EVT MemoryVT = N->getMemoryVT();
  unsigned Alignment = N->getOriginalAlignment();
  SDLoc DL(N);

  // Split all operands
  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue DataLo, DataHi;
  if (getTypeAction(Data.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Data operand
    GetSplitVector(Data, DataLo, DataHi);
  else
    std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

  SDValue MaskLo, MaskHi;
  if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Mask operand
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, DL);

  SDValue IndexHi, IndexLo;
  if (getTypeAction(Index.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Index, IndexLo, IndexHi);
  else
    std::tie(IndexLo, IndexHi) = DAG.SplitVector(Index, DL);

  SDValue Lo;
  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(N->getPointerInfo(),
                         MachineMemOperand::MOStore, LoMemVT.getStoreSize(),
                         Alignment, N->getAAInfo(), N->getRanges());

  SDValue OpsLo[] = {Ch, DataLo, MaskLo, Ptr, IndexLo, Scale};
  Lo = DAG.getMaskedScatter(DAG.getVTList(MVT::Other), DataLo.getValueType(),
                            DL, OpsLo, MMO);

  MMO = DAG.getMachineFunction().
    getMachineMemOperand(N->getPointerInfo(),
                         MachineMemOperand::MOStore,  HiMemVT.getStoreSize(),
                         Alignment, N->getAAInfo(), N->getRanges());

  // The order of the Scatter operation after split is well defined. The "Hi"
  // part comes after the "Lo". So these two operations should be chained one
  // after another.
  SDValue OpsHi[] = {Lo, DataHi, MaskHi, Ptr, IndexHi, Scale};
  return DAG.getMaskedScatter(DAG.getVTList(MVT::Other), DataHi.getValueType(),
                              DL, OpsHi, MMO);
}

SDValue DAGTypeLegalizer::SplitVecOp_STORE(StoreSDNode *N, unsigned OpNo) {
  assert(N->isUnindexed() && "Indexed store of vector?");
  assert(OpNo == 1 && "Can only split the stored value");
  SDLoc DL(N);

  bool isTruncating = N->isTruncatingStore();
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  EVT MemoryVT = N->getMemoryVT();
  unsigned Alignment = N->getOriginalAlignment();
  MachineMemOperand::Flags MMOFlags = N->getMemOperand()->getFlags();
  AAMDNodes AAInfo = N->getAAInfo();
  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(1), Lo, Hi);

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  // Scalarize if the split halves are not byte-sized.
  if (!LoMemVT.isByteSized() || !HiMemVT.isByteSized())
    return TLI.scalarizeVectorStore(N, DAG);

  unsigned IncrementSize = LoMemVT.getSizeInBits()/8;

  if (isTruncating)
    Lo = DAG.getTruncStore(Ch, DL, Lo, Ptr, N->getPointerInfo(), LoMemVT,
                           Alignment, MMOFlags, AAInfo);
  else
    Lo = DAG.getStore(Ch, DL, Lo, Ptr, N->getPointerInfo(), Alignment, MMOFlags,
                      AAInfo);

  // Increment the pointer to the other half.
  Ptr = DAG.getObjectPtrOffset(DL, Ptr, IncrementSize);

  if (isTruncating)
    Hi = DAG.getTruncStore(Ch, DL, Hi, Ptr,
                           N->getPointerInfo().getWithOffset(IncrementSize),
                           HiMemVT, Alignment, MMOFlags, AAInfo);
  else
    Hi = DAG.getStore(Ch, DL, Hi, Ptr,
                      N->getPointerInfo().getWithOffset(IncrementSize),
                      Alignment, MMOFlags, AAInfo);

  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_CONCAT_VECTORS(SDNode *N) {
  SDLoc DL(N);

  // The input operands all must have the same type, and we know the result
  // type is valid.  Convert this to a buildvector which extracts all the
  // input elements.
  // TODO: If the input elements are power-two vectors, we could convert this to
  // a new CONCAT_VECTORS node with elements that are half-wide.
  SmallVector<SDValue, 32> Elts;
  EVT EltVT = N->getValueType(0).getVectorElementType();
  for (const SDValue &Op : N->op_values()) {
    for (unsigned i = 0, e = Op.getValueType().getVectorNumElements();
         i != e; ++i) {
      Elts.push_back(DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Op,
          DAG.getConstant(i, DL, TLI.getVectorIdxTy(DAG.getDataLayout()))));
    }
  }

  return DAG.getBuildVector(N->getValueType(0), DL, Elts);
}

SDValue DAGTypeLegalizer::SplitVecOp_TruncateHelper(SDNode *N) {
  // The result type is legal, but the input type is illegal.  If splitting
  // ends up with the result type of each half still being legal, just
  // do that.  If, however, that would result in an illegal result type,
  // we can try to get more clever with power-two vectors. Specifically,
  // split the input type, but also widen the result element size, then
  // concatenate the halves and truncate again.  For example, consider a target
  // where v8i8 is legal and v8i32 is not (ARM, which doesn't have 256-bit
  // vectors). To perform a "%res = v8i8 trunc v8i32 %in" we do:
  //   %inlo = v4i32 extract_subvector %in, 0
  //   %inhi = v4i32 extract_subvector %in, 4
  //   %lo16 = v4i16 trunc v4i32 %inlo
  //   %hi16 = v4i16 trunc v4i32 %inhi
  //   %in16 = v8i16 concat_vectors v4i16 %lo16, v4i16 %hi16
  //   %res = v8i8 trunc v8i16 %in16
  //
  // Without this transform, the original truncate would end up being
  // scalarized, which is pretty much always a last resort.
  SDValue InVec = N->getOperand(0);
  EVT InVT = InVec->getValueType(0);
  EVT OutVT = N->getValueType(0);
  unsigned NumElements = OutVT.getVectorNumElements();
  bool IsFloat = OutVT.isFloatingPoint();

  // Widening should have already made sure this is a power-two vector
  // if we're trying to split it at all. assert() that's true, just in case.
  assert(!(NumElements & 1) && "Splitting vector, but not in half!");

  unsigned InElementSize = InVT.getScalarSizeInBits();
  unsigned OutElementSize = OutVT.getScalarSizeInBits();

  // Determine the split output VT. If its legal we can just split dirctly.
  EVT LoOutVT, HiOutVT;
  std::tie(LoOutVT, HiOutVT) = DAG.GetSplitDestVTs(OutVT);
  assert(LoOutVT == HiOutVT && "Unequal split?");

  // If the input elements are only 1/2 the width of the result elements,
  // just use the normal splitting. Our trick only work if there's room
  // to split more than once.
  if (isTypeLegal(LoOutVT) ||
      InElementSize <= OutElementSize * 2)
    return SplitVecOp_UnaryOp(N);
  SDLoc DL(N);

  // Don't touch if this will be scalarized.
  EVT FinalVT = InVT;
  while (getTypeAction(FinalVT) == TargetLowering::TypeSplitVector)
    FinalVT = FinalVT.getHalfNumVectorElementsVT(*DAG.getContext());

  if (getTypeAction(FinalVT) == TargetLowering::TypeScalarizeVector)
    return SplitVecOp_UnaryOp(N);

  // Get the split input vector.
  SDValue InLoVec, InHiVec;
  GetSplitVector(InVec, InLoVec, InHiVec);

  // Truncate them to 1/2 the element size.
  EVT HalfElementVT = IsFloat ?
    EVT::getFloatingPointVT(InElementSize/2) :
    EVT::getIntegerVT(*DAG.getContext(), InElementSize/2);
  EVT HalfVT = EVT::getVectorVT(*DAG.getContext(), HalfElementVT,
                                NumElements/2);
  SDValue HalfLo = DAG.getNode(N->getOpcode(), DL, HalfVT, InLoVec);
  SDValue HalfHi = DAG.getNode(N->getOpcode(), DL, HalfVT, InHiVec);
  // Concatenate them to get the full intermediate truncation result.
  EVT InterVT = EVT::getVectorVT(*DAG.getContext(), HalfElementVT, NumElements);
  SDValue InterVec = DAG.getNode(ISD::CONCAT_VECTORS, DL, InterVT, HalfLo,
                                 HalfHi);
  // Now finish up by truncating all the way down to the original result
  // type. This should normally be something that ends up being legal directly,
  // but in theory if a target has very wide vectors and an annoyingly
  // restricted set of legal types, this split can chain to build things up.
  return IsFloat
             ? DAG.getNode(ISD::FP_ROUND, DL, OutVT, InterVec,
                           DAG.getTargetConstant(
                               0, DL, TLI.getPointerTy(DAG.getDataLayout())))
             : DAG.getNode(ISD::TRUNCATE, DL, OutVT, InterVec);
}

SDValue DAGTypeLegalizer::SplitVecOp_VSETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");
  // The result has a legal vector type, but the input needs splitting.
  SDValue Lo0, Hi0, Lo1, Hi1, LoRes, HiRes;
  SDLoc DL(N);
  GetSplitVector(N->getOperand(0), Lo0, Hi0);
  GetSplitVector(N->getOperand(1), Lo1, Hi1);
  unsigned PartElements = Lo0.getValueType().getVectorNumElements();
  EVT PartResVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1, PartElements);
  EVT WideResVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1, 2*PartElements);

  LoRes = DAG.getNode(ISD::SETCC, DL, PartResVT, Lo0, Lo1, N->getOperand(2));
  HiRes = DAG.getNode(ISD::SETCC, DL, PartResVT, Hi0, Hi1, N->getOperand(2));
  SDValue Con = DAG.getNode(ISD::CONCAT_VECTORS, DL, WideResVT, LoRes, HiRes);
  return PromoteTargetBoolean(Con, N->getValueType(0));
}


SDValue DAGTypeLegalizer::SplitVecOp_FP_ROUND(SDNode *N) {
  // The result has a legal vector type, but the input needs splitting.
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc DL(N);
  GetSplitVector(N->getOperand(0), Lo, Hi);
  EVT InVT = Lo.getValueType();

  EVT OutVT = EVT::getVectorVT(*DAG.getContext(), ResVT.getVectorElementType(),
                               InVT.getVectorNumElements());

  Lo = DAG.getNode(ISD::FP_ROUND, DL, OutVT, Lo, N->getOperand(1));
  Hi = DAG.getNode(ISD::FP_ROUND, DL, OutVT, Hi, N->getOperand(1));

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, ResVT, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_FCOPYSIGN(SDNode *N) {
  // The result (and the first input) has a legal vector type, but the second
  // input needs splitting.
  return DAG.UnrollVectorOp(N, N->getValueType(0).getVectorNumElements());
}


//===----------------------------------------------------------------------===//
//  Result Vector Widening
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::WidenVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Widen node result " << ResNo << ": "; N->dump(&DAG);
             dbgs() << "\n");

  // See if the target wants to custom widen this node.
  if (CustomWidenLowerNode(N, N->getValueType(ResNo)))
    return;

  SDValue Res = SDValue();
  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "WidenVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to widen the result of this operator!");

  case ISD::MERGE_VALUES:      Res = WidenVecRes_MERGE_VALUES(N, ResNo); break;
  case ISD::BITCAST:           Res = WidenVecRes_BITCAST(N); break;
  case ISD::BUILD_VECTOR:      Res = WidenVecRes_BUILD_VECTOR(N); break;
  case ISD::CONCAT_VECTORS:    Res = WidenVecRes_CONCAT_VECTORS(N); break;
  case ISD::EXTRACT_SUBVECTOR: Res = WidenVecRes_EXTRACT_SUBVECTOR(N); break;
  case ISD::FP_ROUND_INREG:    Res = WidenVecRes_InregOp(N); break;
  case ISD::INSERT_VECTOR_ELT: Res = WidenVecRes_INSERT_VECTOR_ELT(N); break;
  case ISD::LOAD:              Res = WidenVecRes_LOAD(N); break;
  case ISD::SCALAR_TO_VECTOR:  Res = WidenVecRes_SCALAR_TO_VECTOR(N); break;
  case ISD::SIGN_EXTEND_INREG: Res = WidenVecRes_InregOp(N); break;
  case ISD::VSELECT:
  case ISD::SELECT:            Res = WidenVecRes_SELECT(N); break;
  case ISD::SELECT_CC:         Res = WidenVecRes_SELECT_CC(N); break;
  case ISD::SETCC:             Res = WidenVecRes_SETCC(N); break;
  case ISD::UNDEF:             Res = WidenVecRes_UNDEF(N); break;
  case ISD::VECTOR_SHUFFLE:
    Res = WidenVecRes_VECTOR_SHUFFLE(cast<ShuffleVectorSDNode>(N));
    break;
  case ISD::MLOAD:
    Res = WidenVecRes_MLOAD(cast<MaskedLoadSDNode>(N));
    break;
  case ISD::MGATHER:
    Res = WidenVecRes_MGATHER(cast<MaskedGatherSDNode>(N));
    break;

  case ISD::ADD:
  case ISD::AND:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::OR:
  case ISD::SUB:
  case ISD::XOR:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::UADDSAT:
  case ISD::SADDSAT:
  case ISD::USUBSAT:
  case ISD::SSUBSAT:
    Res = WidenVecRes_Binary(N);
    break;

  case ISD::FADD:
  case ISD::FMUL:
  case ISD::FPOW:
  case ISD::FSUB:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    Res = WidenVecRes_BinaryCanTrap(N);
    break;

  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FREM:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
  case ISD::STRICT_FPOW:
  case ISD::STRICT_FPOWI:
  case ISD::STRICT_FSIN:
  case ISD::STRICT_FCOS:
  case ISD::STRICT_FEXP:
  case ISD::STRICT_FEXP2:
  case ISD::STRICT_FLOG:
  case ISD::STRICT_FLOG10:
  case ISD::STRICT_FLOG2:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FMAXNUM:
  case ISD::STRICT_FMINNUM:
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FTRUNC:
    Res = WidenVecRes_StrictFP(N);
    break;

  case ISD::FCOPYSIGN:
    Res = WidenVecRes_FCOPYSIGN(N);
    break;

  case ISD::FPOWI:
    Res = WidenVecRes_POWI(N);
    break;

  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    Res = WidenVecRes_Shift(N);
    break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    Res = WidenVecRes_EXTEND_VECTOR_INREG(N);
    break;

  case ISD::ANY_EXTEND:
  case ISD::FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::SIGN_EXTEND:
  case ISD::SINT_TO_FP:
  case ISD::TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::ZERO_EXTEND:
    Res = WidenVecRes_Convert(N);
    break;

  case ISD::FABS:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FSIN:
  case ISD::FSQRT:
  case ISD::FTRUNC: {
    // We're going to widen this vector op to a legal type by padding with undef
    // elements. If the wide vector op is eventually going to be expanded to
    // scalar libcalls, then unroll into scalar ops now to avoid unnecessary
    // libcalls on the undef elements. We are assuming that if the scalar op
    // requires expanding, then the vector op needs expanding too.
    EVT VT = N->getValueType(0);
    if (TLI.isOperationExpand(N->getOpcode(), VT.getScalarType())) {
      EVT WideVecVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
      assert(!TLI.isOperationLegalOrCustom(N->getOpcode(), WideVecVT) &&
             "Target supports vector op, but scalar requires expansion?");
      Res = DAG.UnrollVectorOp(N, WideVecVT.getVectorNumElements());
      break;
    }
  }
  // If the target has custom/legal support for the scalar FP intrinsic ops
  // (they are probably not destined to become libcalls), then widen those like
  // any other unary ops.
  LLVM_FALLTHROUGH;

  case ISD::BITREVERSE:
  case ISD::BSWAP:
  case ISD::CTLZ:
  case ISD::CTPOP:
  case ISD::CTTZ:
  case ISD::FNEG:
  case ISD::FCANONICALIZE:
    Res = WidenVecRes_Unary(N);
    break;
  case ISD::FMA:
    Res = WidenVecRes_Ternary(N);
    break;
  }

  // If Res is null, the sub-method took care of registering the result.
  if (Res.getNode())
    SetWidenedVector(SDValue(N, ResNo), Res);
}

SDValue DAGTypeLegalizer::WidenVecRes_Ternary(SDNode *N) {
  // Ternary op widening.
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  SDValue InOp3 = GetWidenedVector(N->getOperand(2));
  return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, InOp3);
}

SDValue DAGTypeLegalizer::WidenVecRes_Binary(SDNode *N) {
  // Binary op widening.
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, N->getFlags());
}

// Given a vector of operations that have been broken up to widen, see
// if we can collect them together into the next widest legal VT. This
// implementation is trap-safe.
static SDValue CollectOpsToWiden(SelectionDAG &DAG, const TargetLowering &TLI,
                                 SmallVectorImpl<SDValue> &ConcatOps,
                                 unsigned ConcatEnd, EVT VT, EVT MaxVT,
                                 EVT WidenVT) {
  // Check to see if we have a single operation with the widen type.
  if (ConcatEnd == 1) {
    VT = ConcatOps[0].getValueType();
    if (VT == WidenVT)
      return ConcatOps[0];
  }

  SDLoc dl(ConcatOps[0]);
  EVT WidenEltVT = WidenVT.getVectorElementType();
  int Idx = 0;

  // while (Some element of ConcatOps is not of type MaxVT) {
  //   From the end of ConcatOps, collect elements of the same type and put
  //   them into an op of the next larger supported type
  // }
  while (ConcatOps[ConcatEnd-1].getValueType() != MaxVT) {
    Idx = ConcatEnd - 1;
    VT = ConcatOps[Idx--].getValueType();
    while (Idx >= 0 && ConcatOps[Idx].getValueType() == VT)
      Idx--;

    int NextSize = VT.isVector() ? VT.getVectorNumElements() : 1;
    EVT NextVT;
    do {
      NextSize *= 2;
      NextVT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NextSize);
    } while (!TLI.isTypeLegal(NextVT));

    if (!VT.isVector()) {
      // Scalar type, create an INSERT_VECTOR_ELEMENT of type NextVT
      SDValue VecOp = DAG.getUNDEF(NextVT);
      unsigned NumToInsert = ConcatEnd - Idx - 1;
      for (unsigned i = 0, OpIdx = Idx+1; i < NumToInsert; i++, OpIdx++) {
        VecOp = DAG.getNode(
            ISD::INSERT_VECTOR_ELT, dl, NextVT, VecOp, ConcatOps[OpIdx],
            DAG.getConstant(i, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
      }
      ConcatOps[Idx+1] = VecOp;
      ConcatEnd = Idx + 2;
    } else {
      // Vector type, create a CONCAT_VECTORS of type NextVT
      SDValue undefVec = DAG.getUNDEF(VT);
      unsigned OpsToConcat = NextSize/VT.getVectorNumElements();
      SmallVector<SDValue, 16> SubConcatOps(OpsToConcat);
      unsigned RealVals = ConcatEnd - Idx - 1;
      unsigned SubConcatEnd = 0;
      unsigned SubConcatIdx = Idx + 1;
      while (SubConcatEnd < RealVals)
        SubConcatOps[SubConcatEnd++] = ConcatOps[++Idx];
      while (SubConcatEnd < OpsToConcat)
        SubConcatOps[SubConcatEnd++] = undefVec;
      ConcatOps[SubConcatIdx] = DAG.getNode(ISD::CONCAT_VECTORS, dl,
                                            NextVT, SubConcatOps);
      ConcatEnd = SubConcatIdx + 1;
    }
  }

  // Check to see if we have a single operation with the widen type.
  if (ConcatEnd == 1) {
    VT = ConcatOps[0].getValueType();
    if (VT == WidenVT)
      return ConcatOps[0];
  }

  // add undefs of size MaxVT until ConcatOps grows to length of WidenVT
  unsigned NumOps = WidenVT.getVectorNumElements()/MaxVT.getVectorNumElements();
  if (NumOps != ConcatEnd ) {
    SDValue UndefVal = DAG.getUNDEF(MaxVT);
    for (unsigned j = ConcatEnd; j < NumOps; ++j)
      ConcatOps[j] = UndefVal;
  }
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT,
                     makeArrayRef(ConcatOps.data(), NumOps));
}

SDValue DAGTypeLegalizer::WidenVecRes_BinaryCanTrap(SDNode *N) {
  // Binary op widening for operations that can trap.
  unsigned Opcode = N->getOpcode();
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenEltVT = WidenVT.getVectorElementType();
  EVT VT = WidenVT;
  unsigned NumElts =  VT.getVectorNumElements();
  const SDNodeFlags Flags = N->getFlags();
  while (!TLI.isTypeLegal(VT) && NumElts != 1) {
    NumElts = NumElts / 2;
    VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
  }

  if (NumElts != 1 && !TLI.canOpTrap(N->getOpcode(), VT)) {
    // Operation doesn't trap so just widen as normal.
    SDValue InOp1 = GetWidenedVector(N->getOperand(0));
    SDValue InOp2 = GetWidenedVector(N->getOperand(1));
    return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, Flags);
  }

  // No legal vector version so unroll the vector operation and then widen.
  if (NumElts == 1)
    return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());

  // Since the operation can trap, apply operation on the original vector.
  EVT MaxVT = VT;
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  unsigned CurNumElts = N->getValueType(0).getVectorNumElements();

  SmallVector<SDValue, 16> ConcatOps(CurNumElts);
  unsigned ConcatEnd = 0;  // Current ConcatOps index.
  int Idx = 0;        // Current Idx into input vectors.

  // NumElts := greatest legal vector size (at most WidenVT)
  // while (orig. vector has unhandled elements) {
  //   take munches of size NumElts from the beginning and add to ConcatOps
  //   NumElts := next smaller supported vector size or 1
  // }
  while (CurNumElts != 0) {
    while (CurNumElts >= NumElts) {
      SDValue EOp1 = DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, dl, VT, InOp1,
          DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
      SDValue EOp2 = DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, dl, VT, InOp2,
          DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
      ConcatOps[ConcatEnd++] = DAG.getNode(Opcode, dl, VT, EOp1, EOp2, Flags);
      Idx += NumElts;
      CurNumElts -= NumElts;
    }
    do {
      NumElts = NumElts / 2;
      VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
    } while (!TLI.isTypeLegal(VT) && NumElts != 1);

    if (NumElts == 1) {
      for (unsigned i = 0; i != CurNumElts; ++i, ++Idx) {
        SDValue EOp1 = DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, WidenEltVT, InOp1,
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
        SDValue EOp2 = DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, WidenEltVT, InOp2,
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
        ConcatOps[ConcatEnd++] = DAG.getNode(Opcode, dl, WidenEltVT,
                                             EOp1, EOp2, Flags);
      }
      CurNumElts = 0;
    }
  }

  return CollectOpsToWiden(DAG, TLI, ConcatOps, ConcatEnd, VT, MaxVT, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_StrictFP(SDNode *N) {
  // StrictFP op widening for operations that can trap.
  unsigned NumOpers = N->getNumOperands();
  unsigned Opcode = N->getOpcode();
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenEltVT = WidenVT.getVectorElementType();
  EVT VT = WidenVT;
  unsigned NumElts = VT.getVectorNumElements();
  while (!TLI.isTypeLegal(VT) && NumElts != 1) {
    NumElts = NumElts / 2;
    VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
  }

  // No legal vector version so unroll the vector operation and then widen.
  if (NumElts == 1)
    return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());

  // Since the operation can trap, apply operation on the original vector.
  EVT MaxVT = VT;
  SmallVector<SDValue, 4> InOps;
  unsigned CurNumElts = N->getValueType(0).getVectorNumElements();

  SmallVector<SDValue, 16> ConcatOps(CurNumElts);
  SmallVector<SDValue, 16> Chains;
  unsigned ConcatEnd = 0;  // Current ConcatOps index.
  int Idx = 0;        // Current Idx into input vectors.

  // The Chain is the first operand.
  InOps.push_back(N->getOperand(0));

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOpers; ++i) {
    SDValue Oper = N->getOperand(i);

    if (Oper.getValueType().isVector()) {
      assert(Oper.getValueType() == N->getValueType(0) && 
             "Invalid operand type to widen!");
      Oper = GetWidenedVector(Oper);
    }

    InOps.push_back(Oper);
  }

  // NumElts := greatest legal vector size (at most WidenVT)
  // while (orig. vector has unhandled elements) {
  //   take munches of size NumElts from the beginning and add to ConcatOps
  //   NumElts := next smaller supported vector size or 1
  // }
  while (CurNumElts != 0) {
    while (CurNumElts >= NumElts) {
      SmallVector<SDValue, 4> EOps;
      
      for (unsigned i = 0; i < NumOpers; ++i) {
        SDValue Op = InOps[i];
        
        if (Op.getValueType().isVector()) 
          Op = DAG.getNode(
            ISD::EXTRACT_SUBVECTOR, dl, VT, Op,
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));

        EOps.push_back(Op);
      }

      EVT OperVT[] = {VT, MVT::Other};
      SDValue Oper = DAG.getNode(Opcode, dl, OperVT, EOps);
      ConcatOps[ConcatEnd++] = Oper;
      Chains.push_back(Oper.getValue(1));
      Idx += NumElts;
      CurNumElts -= NumElts;
    }
    do {
      NumElts = NumElts / 2;
      VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
    } while (!TLI.isTypeLegal(VT) && NumElts != 1);

    if (NumElts == 1) {
      for (unsigned i = 0; i != CurNumElts; ++i, ++Idx) {
        SmallVector<SDValue, 4> EOps;

        for (unsigned i = 0; i < NumOpers; ++i) {
          SDValue Op = InOps[i];

          if (Op.getValueType().isVector())
            Op = DAG.getNode(
              ISD::EXTRACT_VECTOR_ELT, dl, WidenEltVT, Op,
              DAG.getConstant(Idx, dl, 
                              TLI.getVectorIdxTy(DAG.getDataLayout())));

          EOps.push_back(Op);
        }

        EVT WidenVT[] = {WidenEltVT, MVT::Other}; 
        SDValue Oper = DAG.getNode(Opcode, dl, WidenVT, EOps);
        ConcatOps[ConcatEnd++] = Oper;
        Chains.push_back(Oper.getValue(1));
      }
      CurNumElts = 0;
    }
  }

  // Build a factor node to remember all the Ops that have been created.
  SDValue NewChain;
  if (Chains.size() == 1)
    NewChain = Chains[0];
  else
    NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return CollectOpsToWiden(DAG, TLI, ConcatOps, ConcatEnd, VT, MaxVT, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_Convert(SDNode *N) {
  SDValue InOp = N->getOperand(0);
  SDLoc DL(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  EVT InVT = InOp.getValueType();
  EVT InEltVT = InVT.getVectorElementType();
  EVT InWidenVT = EVT::getVectorVT(*DAG.getContext(), InEltVT, WidenNumElts);

  unsigned Opcode = N->getOpcode();
  unsigned InVTNumElts = InVT.getVectorNumElements();
  const SDNodeFlags Flags = N->getFlags();
  if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
    InOp = GetWidenedVector(N->getOperand(0));
    InVT = InOp.getValueType();
    InVTNumElts = InVT.getVectorNumElements();
    if (InVTNumElts == WidenNumElts) {
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InOp);
      return DAG.getNode(Opcode, DL, WidenVT, InOp, N->getOperand(1), Flags);
    }
    if (WidenVT.getSizeInBits() == InVT.getSizeInBits()) {
      // If both input and result vector types are of same width, extend
      // operations should be done with SIGN/ZERO_EXTEND_VECTOR_INREG, which
      // accepts fewer elements in the result than in the input.
      if (Opcode == ISD::ANY_EXTEND)
        return DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
      if (Opcode == ISD::SIGN_EXTEND)
        return DAG.getNode(ISD::SIGN_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
      if (Opcode == ISD::ZERO_EXTEND)
        return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
    }
  }

  if (TLI.isTypeLegal(InWidenVT)) {
    // Because the result and the input are different vector types, widening
    // the result could create a legal type but widening the input might make
    // it an illegal type that might lead to repeatedly splitting the input
    // and then widening it. To avoid this, we widen the input only if
    // it results in a legal type.
    if (WidenNumElts % InVTNumElts == 0) {
      // Widen the input and call convert on the widened input vector.
      unsigned NumConcat = WidenNumElts/InVTNumElts;
      SmallVector<SDValue, 16> Ops(NumConcat, DAG.getUNDEF(InVT));
      Ops[0] = InOp;
      SDValue InVec = DAG.getNode(ISD::CONCAT_VECTORS, DL, InWidenVT, Ops);
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InVec);
      return DAG.getNode(Opcode, DL, WidenVT, InVec, N->getOperand(1), Flags);
    }

    if (InVTNumElts % WidenNumElts == 0) {
      SDValue InVal = DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, DL, InWidenVT, InOp,
          DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
      // Extract the input and convert the shorten input vector.
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InVal);
      return DAG.getNode(Opcode, DL, WidenVT, InVal, N->getOperand(1), Flags);
    }
  }

  // Otherwise unroll into some nasty scalar code and rebuild the vector.
  EVT EltVT = WidenVT.getVectorElementType();
  SmallVector<SDValue, 16> Ops(WidenNumElts, DAG.getUNDEF(EltVT));
  // Use the original element count so we don't do more scalar opts than
  // necessary.
  unsigned MinElts = N->getValueType(0).getVectorNumElements();
  for (unsigned i=0; i < MinElts; ++i) {
    SDValue Val = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, DL, InEltVT, InOp,
        DAG.getConstant(i, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
    if (N->getNumOperands() == 1)
      Ops[i] = DAG.getNode(Opcode, DL, EltVT, Val);
    else
      Ops[i] = DAG.getNode(Opcode, DL, EltVT, Val, N->getOperand(1), Flags);
  }

  return DAG.getBuildVector(WidenVT, DL, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_EXTEND_VECTOR_INREG(SDNode *N) {
  unsigned Opcode = N->getOpcode();
  SDValue InOp = N->getOperand(0);
  SDLoc DL(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenSVT = WidenVT.getVectorElementType();
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  EVT InVT = InOp.getValueType();
  EVT InSVT = InVT.getVectorElementType();
  unsigned InVTNumElts = InVT.getVectorNumElements();

  if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
    InOp = GetWidenedVector(InOp);
    InVT = InOp.getValueType();
    if (InVT.getSizeInBits() == WidenVT.getSizeInBits()) {
      switch (Opcode) {
      case ISD::ANY_EXTEND_VECTOR_INREG:
      case ISD::SIGN_EXTEND_VECTOR_INREG:
      case ISD::ZERO_EXTEND_VECTOR_INREG:
        return DAG.getNode(Opcode, DL, WidenVT, InOp);
      }
    }
  }

  // Unroll, extend the scalars and rebuild the vector.
  SmallVector<SDValue, 16> Ops;
  for (unsigned i = 0, e = std::min(InVTNumElts, WidenNumElts); i != e; ++i) {
    SDValue Val = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, InSVT, InOp,
      DAG.getConstant(i, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
    switch (Opcode) {
    case ISD::ANY_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, WidenSVT, Val);
      break;
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::SIGN_EXTEND, DL, WidenSVT, Val);
      break;
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, WidenSVT, Val);
      break;
    default:
      llvm_unreachable("A *_EXTEND_VECTOR_INREG node was expected");
    }
    Ops.push_back(Val);
  }

  while (Ops.size() != WidenNumElts)
    Ops.push_back(DAG.getUNDEF(WidenSVT));

  return DAG.getBuildVector(WidenVT, DL, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_FCOPYSIGN(SDNode *N) {
  // If this is an FCOPYSIGN with same input types, we can treat it as a
  // normal (can trap) binary op.
  if (N->getOperand(0).getValueType() == N->getOperand(1).getValueType())
    return WidenVecRes_BinaryCanTrap(N);

  // If the types are different, fall back to unrolling.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());
}

SDValue DAGTypeLegalizer::WidenVecRes_POWI(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  SDValue ShOp = N->getOperand(1);
  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, InOp, ShOp);
}

SDValue DAGTypeLegalizer::WidenVecRes_Shift(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  SDValue ShOp = N->getOperand(1);

  EVT ShVT = ShOp.getValueType();
  if (getTypeAction(ShVT) == TargetLowering::TypeWidenVector) {
    ShOp = GetWidenedVector(ShOp);
    ShVT = ShOp.getValueType();
  }
  EVT ShWidenVT = EVT::getVectorVT(*DAG.getContext(),
                                   ShVT.getVectorElementType(),
                                   WidenVT.getVectorNumElements());
  if (ShVT != ShWidenVT)
    ShOp = ModifyToType(ShOp, ShWidenVT);

  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, InOp, ShOp);
}

SDValue DAGTypeLegalizer::WidenVecRes_Unary(SDNode *N) {
  // Unary op widening.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, InOp);
}

SDValue DAGTypeLegalizer::WidenVecRes_InregOp(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT ExtVT = EVT::getVectorVT(*DAG.getContext(),
                               cast<VTSDNode>(N->getOperand(1))->getVT()
                                 .getVectorElementType(),
                               WidenVT.getVectorNumElements());
  SDValue WidenLHS = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     WidenVT, WidenLHS, DAG.getValueType(ExtVT));
}

SDValue DAGTypeLegalizer::WidenVecRes_MERGE_VALUES(SDNode *N, unsigned ResNo) {
  SDValue WidenVec = DisintegrateMERGE_VALUES(N, ResNo);
  return GetWidenedVector(WidenVec);
}

SDValue DAGTypeLegalizer::WidenVecRes_BITCAST(SDNode *N) {
  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();
  EVT VT = N->getValueType(0);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  switch (getTypeAction(InVT)) {
  case TargetLowering::TypeLegal:
    break;
  case TargetLowering::TypePromoteInteger:
    // If the incoming type is a vector that is being promoted, then
    // we know that the elements are arranged differently and that we
    // must perform the conversion using a stack slot.
    if (InVT.isVector())
      break;

    // If the InOp is promoted to the same size, convert it.  Otherwise,
    // fall out of the switch and widen the promoted input.
    InOp = GetPromotedInteger(InOp);
    InVT = InOp.getValueType();
    if (WidenVT.bitsEq(InVT))
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, InOp);
    break;
  case TargetLowering::TypeSoftenFloat:
  case TargetLowering::TypePromoteFloat:
  case TargetLowering::TypeExpandInteger:
  case TargetLowering::TypeExpandFloat:
  case TargetLowering::TypeScalarizeVector:
  case TargetLowering::TypeSplitVector:
    break;
  case TargetLowering::TypeWidenVector:
    // If the InOp is widened to the same size, convert it.  Otherwise, fall
    // out of the switch and widen the widened input.
    InOp = GetWidenedVector(InOp);
    InVT = InOp.getValueType();
    if (WidenVT.bitsEq(InVT))
      // The input widens to the same size. Convert to the widen value.
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, InOp);
    break;
  }

  unsigned WidenSize = WidenVT.getSizeInBits();
  unsigned InSize = InVT.getSizeInBits();
  // x86mmx is not an acceptable vector element type, so don't try.
  if (WidenSize % InSize == 0 && InVT != MVT::x86mmx) {
    // Determine new input vector type.  The new input vector type will use
    // the same element type (if its a vector) or use the input type as a
    // vector.  It is the same size as the type to widen to.
    EVT NewInVT;
    unsigned NewNumElts = WidenSize / InSize;
    if (InVT.isVector()) {
      EVT InEltVT = InVT.getVectorElementType();
      NewInVT = EVT::getVectorVT(*DAG.getContext(), InEltVT,
                                 WidenSize / InEltVT.getSizeInBits());
    } else {
      NewInVT = EVT::getVectorVT(*DAG.getContext(), InVT, NewNumElts);
    }

    if (TLI.isTypeLegal(NewInVT)) {
      SDValue NewVec;
      if (InVT.isVector()) {
        // Because the result and the input are different vector types, widening
        // the result could create a legal type but widening the input might make
        // it an illegal type that might lead to repeatedly splitting the input
        // and then widening it. To avoid this, we widen the input only if
        // it results in a legal type.
        SmallVector<SDValue, 16> Ops(NewNumElts, DAG.getUNDEF(InVT));
        Ops[0] = InOp;

        NewVec = DAG.getNode(ISD::CONCAT_VECTORS, dl, NewInVT, Ops);
      } else {
        NewVec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewInVT, InOp);
      }
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, NewVec);
    }
  }

  return CreateStackStoreLoad(InOp, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_BUILD_VECTOR(SDNode *N) {
  SDLoc dl(N);
  // Build a vector with undefined for the new nodes.
  EVT VT = N->getValueType(0);

  // Integer BUILD_VECTOR operands may be larger than the node's vector element
  // type. The UNDEFs need to have the same type as the existing operands.
  EVT EltVT = N->getOperand(0).getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SmallVector<SDValue, 16> NewOps(N->op_begin(), N->op_end());
  assert(WidenNumElts >= NumElts && "Shrinking vector instead of widening!");
  NewOps.append(WidenNumElts - NumElts, DAG.getUNDEF(EltVT));

  return DAG.getBuildVector(WidenVT, dl, NewOps);
}

SDValue DAGTypeLegalizer::WidenVecRes_CONCAT_VECTORS(SDNode *N) {
  EVT InVT = N->getOperand(0).getValueType();
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  unsigned NumInElts = InVT.getVectorNumElements();
  unsigned NumOperands = N->getNumOperands();

  bool InputWidened = false; // Indicates we need to widen the input.
  if (getTypeAction(InVT) != TargetLowering::TypeWidenVector) {
    if (WidenVT.getVectorNumElements() % InVT.getVectorNumElements() == 0) {
      // Add undef vectors to widen to correct length.
      unsigned NumConcat = WidenVT.getVectorNumElements() /
                           InVT.getVectorNumElements();
      SDValue UndefVal = DAG.getUNDEF(InVT);
      SmallVector<SDValue, 16> Ops(NumConcat);
      for (unsigned i=0; i < NumOperands; ++i)
        Ops[i] = N->getOperand(i);
      for (unsigned i = NumOperands; i != NumConcat; ++i)
        Ops[i] = UndefVal;
      return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, Ops);
    }
  } else {
    InputWidened = true;
    if (WidenVT == TLI.getTypeToTransformTo(*DAG.getContext(), InVT)) {
      // The inputs and the result are widen to the same value.
      unsigned i;
      for (i=1; i < NumOperands; ++i)
        if (!N->getOperand(i).isUndef())
          break;

      if (i == NumOperands)
        // Everything but the first operand is an UNDEF so just return the
        // widened first operand.
        return GetWidenedVector(N->getOperand(0));

      if (NumOperands == 2) {
        // Replace concat of two operands with a shuffle.
        SmallVector<int, 16> MaskOps(WidenNumElts, -1);
        for (unsigned i = 0; i < NumInElts; ++i) {
          MaskOps[i] = i;
          MaskOps[i + NumInElts] = i + WidenNumElts;
        }
        return DAG.getVectorShuffle(WidenVT, dl,
                                    GetWidenedVector(N->getOperand(0)),
                                    GetWidenedVector(N->getOperand(1)),
                                    MaskOps);
      }
    }
  }

  // Fall back to use extracts and build vector.
  EVT EltVT = WidenVT.getVectorElementType();
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  unsigned Idx = 0;
  for (unsigned i=0; i < NumOperands; ++i) {
    SDValue InOp = N->getOperand(i);
    if (InputWidened)
      InOp = GetWidenedVector(InOp);
    for (unsigned j=0; j < NumInElts; ++j)
      Ops[Idx++] = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
          DAG.getConstant(j, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }
  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; Idx < WidenNumElts; ++Idx)
    Ops[Idx] = UndefVal;
  return DAG.getBuildVector(WidenVT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_EXTRACT_SUBVECTOR(SDNode *N) {
  EVT      VT = N->getValueType(0);
  EVT      WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  SDValue  InOp = N->getOperand(0);
  SDValue  Idx  = N->getOperand(1);
  SDLoc dl(N);

  if (getTypeAction(InOp.getValueType()) == TargetLowering::TypeWidenVector)
    InOp = GetWidenedVector(InOp);

  EVT InVT = InOp.getValueType();

  // Check if we can just return the input vector after widening.
  uint64_t IdxVal = cast<ConstantSDNode>(Idx)->getZExtValue();
  if (IdxVal == 0 && InVT == WidenVT)
    return InOp;

  // Check if we can extract from the vector.
  unsigned InNumElts = InVT.getVectorNumElements();
  if (IdxVal % WidenNumElts == 0 && IdxVal + WidenNumElts < InNumElts)
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, WidenVT, InOp, Idx);

  // We could try widening the input to the right length but for now, extract
  // the original elements, fill the rest with undefs and build a vector.
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  EVT EltVT = VT.getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();
  unsigned i;
  for (i=0; i < NumElts; ++i)
    Ops[i] =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
                    DAG.getConstant(IdxVal + i, dl,
                                    TLI.getVectorIdxTy(DAG.getDataLayout())));

  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; i < WidenNumElts; ++i)
    Ops[i] = UndefVal;
  return DAG.getBuildVector(WidenVT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_INSERT_VECTOR_ELT(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, SDLoc(N),
                     InOp.getValueType(), InOp,
                     N->getOperand(1), N->getOperand(2));
}

SDValue DAGTypeLegalizer::WidenVecRes_LOAD(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();

  SDValue Result;
  SmallVector<SDValue, 16> LdChain;  // Chain for the series of load
  if (ExtType != ISD::NON_EXTLOAD)
    Result = GenWidenVectorExtLoads(LdChain, LD, ExtType);
  else
    Result = GenWidenVectorLoads(LdChain, LD);

  // If we generate a single load, we can use that for the chain.  Otherwise,
  // build a factor node to remember the multiple loads are independent and
  // chain to that.
  SDValue NewChain;
  if (LdChain.size() == 1)
    NewChain = LdChain[0];
  else
    NewChain = DAG.getNode(ISD::TokenFactor, SDLoc(LD), MVT::Other, LdChain);

  // Modified the chain - switch anything that used the old chain to use
  // the new one.
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return Result;
}

SDValue DAGTypeLegalizer::WidenVecRes_MLOAD(MaskedLoadSDNode *N) {

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),N->getValueType(0));
  SDValue Mask = N->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue PassThru = GetWidenedVector(N->getPassThru());
  ISD::LoadExtType ExtType = N->getExtensionType();
  SDLoc dl(N);

  // The mask should be widened as well
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                    MaskVT.getVectorElementType(),
                                    WidenVT.getVectorNumElements());
  Mask = ModifyToType(Mask, WideMaskVT, true);

  SDValue Res = DAG.getMaskedLoad(WidenVT, dl, N->getChain(), N->getBasePtr(),
                                  Mask, PassThru, N->getMemoryVT(),
                                  N->getMemOperand(), ExtType,
                                  N->isExpandingLoad());
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_MGATHER(MaskedGatherSDNode *N) {

  EVT WideVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Mask = N->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue PassThru = GetWidenedVector(N->getPassThru());
  SDValue Scale = N->getScale();
  unsigned NumElts = WideVT.getVectorNumElements();
  SDLoc dl(N);

  // The mask should be widened as well
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                    MaskVT.getVectorElementType(),
                                    WideVT.getVectorNumElements());
  Mask = ModifyToType(Mask, WideMaskVT, true);

  // Widen the Index operand
  SDValue Index = N->getIndex();
  EVT WideIndexVT = EVT::getVectorVT(*DAG.getContext(),
                                     Index.getValueType().getScalarType(),
                                     NumElts);
  Index = ModifyToType(Index, WideIndexVT);
  SDValue Ops[] = { N->getChain(), PassThru, Mask, N->getBasePtr(), Index,
                    Scale };
  SDValue Res = DAG.getMaskedGather(DAG.getVTList(WideVT, MVT::Other),
                                    N->getMemoryVT(), dl, Ops,
                                    N->getMemOperand());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_SCALAR_TO_VECTOR(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N),
                     WidenVT, N->getOperand(0));
}

// Return true if this is a node that could have two SETCCs as operands.
static inline bool isLogicalMaskOp(unsigned Opcode) {
  switch (Opcode) {
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    return true;
  }
  return false;
}

// This is used just for the assert in convertMask(). Check that this either
// a SETCC or a previously handled SETCC by convertMask().
#ifndef NDEBUG
static inline bool isSETCCorConvertedSETCC(SDValue N) {
  if (N.getOpcode() == ISD::EXTRACT_SUBVECTOR)
    N = N.getOperand(0);
  else if (N.getOpcode() == ISD::CONCAT_VECTORS) {
    for (unsigned i = 1; i < N->getNumOperands(); ++i)
      if (!N->getOperand(i)->isUndef())
        return false;
    N = N.getOperand(0);
  }

  if (N.getOpcode() == ISD::TRUNCATE)
    N = N.getOperand(0);
  else if (N.getOpcode() == ISD::SIGN_EXTEND)
    N = N.getOperand(0);

  if (isLogicalMaskOp(N.getOpcode()))
    return isSETCCorConvertedSETCC(N.getOperand(0)) &&
           isSETCCorConvertedSETCC(N.getOperand(1));

  return (N.getOpcode() == ISD::SETCC ||
          ISD::isBuildVectorOfConstantSDNodes(N.getNode()));
}
#endif

// Return a mask of vector type MaskVT to replace InMask. Also adjust MaskVT
// to ToMaskVT if needed with vector extension or truncation.
SDValue DAGTypeLegalizer::convertMask(SDValue InMask, EVT MaskVT,
                                      EVT ToMaskVT) {
  // Currently a SETCC or a AND/OR/XOR with two SETCCs are handled.
  // FIXME: This code seems to be too restrictive, we might consider
  // generalizing it or dropping it.
  assert(isSETCCorConvertedSETCC(InMask) && "Unexpected mask argument.");

  // Make a new Mask node, with a legal result VT.
  SmallVector<SDValue, 4> Ops;
  for (unsigned i = 0, e = InMask->getNumOperands(); i < e; ++i)
    Ops.push_back(InMask->getOperand(i));
  SDValue Mask = DAG.getNode(InMask->getOpcode(), SDLoc(InMask), MaskVT, Ops);

  // If MaskVT has smaller or bigger elements than ToMaskVT, a vector sign
  // extend or truncate is needed.
  LLVMContext &Ctx = *DAG.getContext();
  unsigned MaskScalarBits = MaskVT.getScalarSizeInBits();
  unsigned ToMaskScalBits = ToMaskVT.getScalarSizeInBits();
  if (MaskScalarBits < ToMaskScalBits) {
    EVT ExtVT = EVT::getVectorVT(Ctx, ToMaskVT.getVectorElementType(),
                                 MaskVT.getVectorNumElements());
    Mask = DAG.getNode(ISD::SIGN_EXTEND, SDLoc(Mask), ExtVT, Mask);
  } else if (MaskScalarBits > ToMaskScalBits) {
    EVT TruncVT = EVT::getVectorVT(Ctx, ToMaskVT.getVectorElementType(),
                                   MaskVT.getVectorNumElements());
    Mask = DAG.getNode(ISD::TRUNCATE, SDLoc(Mask), TruncVT, Mask);
  }

  assert(Mask->getValueType(0).getScalarSizeInBits() ==
             ToMaskVT.getScalarSizeInBits() &&
         "Mask should have the right element size by now.");

  // Adjust Mask to the right number of elements.
  unsigned CurrMaskNumEls = Mask->getValueType(0).getVectorNumElements();
  if (CurrMaskNumEls > ToMaskVT.getVectorNumElements()) {
    MVT IdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
    SDValue ZeroIdx = DAG.getConstant(0, SDLoc(Mask), IdxTy);
    Mask = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(Mask), ToMaskVT, Mask,
                       ZeroIdx);
  } else if (CurrMaskNumEls < ToMaskVT.getVectorNumElements()) {
    unsigned NumSubVecs = (ToMaskVT.getVectorNumElements() / CurrMaskNumEls);
    EVT SubVT = Mask->getValueType(0);
    SmallVector<SDValue, 16> SubOps(NumSubVecs, DAG.getUNDEF(SubVT));
    SubOps[0] = Mask;
    Mask = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(Mask), ToMaskVT, SubOps);
  }

  assert((Mask->getValueType(0) == ToMaskVT) &&
         "A mask of ToMaskVT should have been produced by now.");

  return Mask;
}

// This method tries to handle VSELECT and its mask by legalizing operands
// (which may require widening) and if needed adjusting the mask vector type
// to match that of the VSELECT. Without it, many cases end up with
// scalarization of the SETCC, with many unnecessary instructions.
SDValue DAGTypeLegalizer::WidenVSELECTAndMask(SDNode *N) {
  LLVMContext &Ctx = *DAG.getContext();
  SDValue Cond = N->getOperand(0);

  if (N->getOpcode() != ISD::VSELECT)
    return SDValue();

  if (Cond->getOpcode() != ISD::SETCC && !isLogicalMaskOp(Cond->getOpcode()))
    return SDValue();

  // If this is a splitted VSELECT that was previously already handled, do
  // nothing.
  EVT CondVT = Cond->getValueType(0);
  if (CondVT.getScalarSizeInBits() != 1)
    return SDValue();

  EVT VSelVT = N->getValueType(0);
  // Only handle vector types which are a power of 2.
  if (!isPowerOf2_64(VSelVT.getSizeInBits()))
    return SDValue();

  // Don't touch if this will be scalarized.
  EVT FinalVT = VSelVT;
  while (getTypeAction(FinalVT) == TargetLowering::TypeSplitVector)
    FinalVT = FinalVT.getHalfNumVectorElementsVT(Ctx);

  if (FinalVT.getVectorNumElements() == 1)
    return SDValue();

  // If there is support for an i1 vector mask, don't touch.
  if (Cond.getOpcode() == ISD::SETCC) {
    EVT SetCCOpVT = Cond->getOperand(0).getValueType();
    while (TLI.getTypeAction(Ctx, SetCCOpVT) != TargetLowering::TypeLegal)
      SetCCOpVT = TLI.getTypeToTransformTo(Ctx, SetCCOpVT);
    EVT SetCCResVT = getSetCCResultType(SetCCOpVT);
    if (SetCCResVT.getScalarSizeInBits() == 1)
      return SDValue();
  } else if (CondVT.getScalarType() == MVT::i1) {
    // If there is support for an i1 vector mask (or only scalar i1 conditions),
    // don't touch.
    while (TLI.getTypeAction(Ctx, CondVT) != TargetLowering::TypeLegal)
      CondVT = TLI.getTypeToTransformTo(Ctx, CondVT);

    if (CondVT.getScalarType() == MVT::i1)
      return SDValue();
  }

  // Get the VT and operands for VSELECT, and widen if needed.
  SDValue VSelOp1 = N->getOperand(1);
  SDValue VSelOp2 = N->getOperand(2);
  if (getTypeAction(VSelVT) == TargetLowering::TypeWidenVector) {
    VSelVT = TLI.getTypeToTransformTo(Ctx, VSelVT);
    VSelOp1 = GetWidenedVector(VSelOp1);
    VSelOp2 = GetWidenedVector(VSelOp2);
  }

  // The mask of the VSELECT should have integer elements.
  EVT ToMaskVT = VSelVT;
  if (!ToMaskVT.getScalarType().isInteger())
    ToMaskVT = ToMaskVT.changeVectorElementTypeToInteger();

  SDValue Mask;
  if (Cond->getOpcode() == ISD::SETCC) {
    EVT MaskVT = getSetCCResultType(Cond.getOperand(0).getValueType());
    Mask = convertMask(Cond, MaskVT, ToMaskVT);
  } else if (isLogicalMaskOp(Cond->getOpcode()) &&
             Cond->getOperand(0).getOpcode() == ISD::SETCC &&
             Cond->getOperand(1).getOpcode() == ISD::SETCC) {
    // Cond is (AND/OR/XOR (SETCC, SETCC))
    SDValue SETCC0 = Cond->getOperand(0);
    SDValue SETCC1 = Cond->getOperand(1);
    EVT VT0 = getSetCCResultType(SETCC0.getOperand(0).getValueType());
    EVT VT1 = getSetCCResultType(SETCC1.getOperand(0).getValueType());
    unsigned ScalarBits0 = VT0.getScalarSizeInBits();
    unsigned ScalarBits1 = VT1.getScalarSizeInBits();
    unsigned ScalarBits_ToMask = ToMaskVT.getScalarSizeInBits();
    EVT MaskVT;
    // If the two SETCCs have different VTs, either extend/truncate one of
    // them to the other "towards" ToMaskVT, or truncate one and extend the
    // other to ToMaskVT.
    if (ScalarBits0 != ScalarBits1) {
      EVT NarrowVT = ((ScalarBits0 < ScalarBits1) ? VT0 : VT1);
      EVT WideVT = ((NarrowVT == VT0) ? VT1 : VT0);
      if (ScalarBits_ToMask >= WideVT.getScalarSizeInBits())
        MaskVT = WideVT;
      else if (ScalarBits_ToMask <= NarrowVT.getScalarSizeInBits())
        MaskVT = NarrowVT;
      else
        MaskVT = ToMaskVT;
    } else
      // If the two SETCCs have the same VT, don't change it.
      MaskVT = VT0;

    // Make new SETCCs and logical nodes.
    SETCC0 = convertMask(SETCC0, VT0, MaskVT);
    SETCC1 = convertMask(SETCC1, VT1, MaskVT);
    Cond = DAG.getNode(Cond->getOpcode(), SDLoc(Cond), MaskVT, SETCC0, SETCC1);

    // Convert the logical op for VSELECT if needed.
    Mask = convertMask(Cond, MaskVT, ToMaskVT);
  } else
    return SDValue();

  return DAG.getNode(ISD::VSELECT, SDLoc(N), VSelVT, Mask, VSelOp1, VSelOp2);
}

SDValue DAGTypeLegalizer::WidenVecRes_SELECT(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SDValue Cond1 = N->getOperand(0);
  EVT CondVT = Cond1.getValueType();
  if (CondVT.isVector()) {
    if (SDValue Res = WidenVSELECTAndMask(N))
      return Res;

    EVT CondEltVT = CondVT.getVectorElementType();
    EVT CondWidenVT =  EVT::getVectorVT(*DAG.getContext(),
                                        CondEltVT, WidenNumElts);
    if (getTypeAction(CondVT) == TargetLowering::TypeWidenVector)
      Cond1 = GetWidenedVector(Cond1);

    // If we have to split the condition there is no point in widening the
    // select. This would result in an cycle of widening the select ->
    // widening the condition operand -> splitting the condition operand ->
    // splitting the select -> widening the select. Instead split this select
    // further and widen the resulting type.
    if (getTypeAction(CondVT) == TargetLowering::TypeSplitVector) {
      SDValue SplitSelect = SplitVecOp_VSELECT(N, 0);
      SDValue Res = ModifyToType(SplitSelect, WidenVT);
      return Res;
    }

    if (Cond1.getValueType() != CondWidenVT)
      Cond1 = ModifyToType(Cond1, CondWidenVT);
  }

  SDValue InOp1 = GetWidenedVector(N->getOperand(1));
  SDValue InOp2 = GetWidenedVector(N->getOperand(2));
  assert(InOp1.getValueType() == WidenVT && InOp2.getValueType() == WidenVT);
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     WidenVT, Cond1, InOp1, InOp2);
}

SDValue DAGTypeLegalizer::WidenVecRes_SELECT_CC(SDNode *N) {
  SDValue InOp1 = GetWidenedVector(N->getOperand(2));
  SDValue InOp2 = GetWidenedVector(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     InOp1.getValueType(), N->getOperand(0),
                     N->getOperand(1), InOp1, InOp2, N->getOperand(4));
}

SDValue DAGTypeLegalizer::WidenVecRes_UNDEF(SDNode *N) {
 EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
 return DAG.getUNDEF(WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_VECTOR_SHUFFLE(ShuffleVectorSDNode *N) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned NumElts = VT.getVectorNumElements();
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));

  // Adjust mask based on new input vector length.
  SmallVector<int, 16> NewMask;
  for (unsigned i = 0; i != NumElts; ++i) {
    int Idx = N->getMaskElt(i);
    if (Idx < (int)NumElts)
      NewMask.push_back(Idx);
    else
      NewMask.push_back(Idx - NumElts + WidenNumElts);
  }
  for (unsigned i = NumElts; i != WidenNumElts; ++i)
    NewMask.push_back(-1);
  return DAG.getVectorShuffle(WidenVT, dl, InOp1, InOp2, NewMask);
}

SDValue DAGTypeLegalizer::WidenVecRes_SETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operands must be vectors");
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SDValue InOp1 = N->getOperand(0);
  EVT InVT = InOp1.getValueType();
  assert(InVT.isVector() && "can not widen non-vector type");
  EVT WidenInVT = EVT::getVectorVT(*DAG.getContext(),
                                   InVT.getVectorElementType(), WidenNumElts);

  // The input and output types often differ here, and it could be that while
  // we'd prefer to widen the result type, the input operands have been split.
  // In this case, we also need to split the result of this node as well.
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector) {
    SDValue SplitVSetCC = SplitVecOp_VSETCC(N);
    SDValue Res = ModifyToType(SplitVSetCC, WidenVT);
    return Res;
  }

  InOp1 = GetWidenedVector(InOp1);
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));

  // Assume that the input and output will be widen appropriately.  If not,
  // we will have to unroll it at some point.
  assert(InOp1.getValueType() == WidenInVT &&
         InOp2.getValueType() == WidenInVT &&
         "Input not widened to expected type!");
  (void)WidenInVT;
  return DAG.getNode(ISD::SETCC, SDLoc(N),
                     WidenVT, InOp1, InOp2, N->getOperand(2));
}


//===----------------------------------------------------------------------===//
// Widen Vector Operand
//===----------------------------------------------------------------------===//
bool DAGTypeLegalizer::WidenVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Widen node operand " << OpNo << ": "; N->dump(&DAG);
             dbgs() << "\n");
  SDValue Res = SDValue();

  // See if the target wants to custom widen this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "WidenVectorOperand op #" << OpNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to widen this operator's operand!");

  case ISD::BITCAST:            Res = WidenVecOp_BITCAST(N); break;
  case ISD::CONCAT_VECTORS:     Res = WidenVecOp_CONCAT_VECTORS(N); break;
  case ISD::EXTRACT_SUBVECTOR:  Res = WidenVecOp_EXTRACT_SUBVECTOR(N); break;
  case ISD::EXTRACT_VECTOR_ELT: Res = WidenVecOp_EXTRACT_VECTOR_ELT(N); break;
  case ISD::STORE:              Res = WidenVecOp_STORE(N); break;
  case ISD::MSTORE:             Res = WidenVecOp_MSTORE(N, OpNo); break;
  case ISD::MGATHER:            Res = WidenVecOp_MGATHER(N, OpNo); break;
  case ISD::MSCATTER:           Res = WidenVecOp_MSCATTER(N, OpNo); break;
  case ISD::SETCC:              Res = WidenVecOp_SETCC(N); break;
  case ISD::FCOPYSIGN:          Res = WidenVecOp_FCOPYSIGN(N); break;

  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    Res = WidenVecOp_EXTEND(N);
    break;

  case ISD::FP_EXTEND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::TRUNCATE:
    Res = WidenVecOp_Convert(N);
    break;
  }

  // If Res is null, the sub-method took care of registering the result.
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

SDValue DAGTypeLegalizer::WidenVecOp_EXTEND(SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  SDValue InOp = N->getOperand(0);
  assert(getTypeAction(InOp.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unexpected type action");
  InOp = GetWidenedVector(InOp);
  assert(VT.getVectorNumElements() <
             InOp.getValueType().getVectorNumElements() &&
         "Input wasn't widened!");

  // We may need to further widen the operand until it has the same total
  // vector size as the result.
  EVT InVT = InOp.getValueType();
  if (InVT.getSizeInBits() != VT.getSizeInBits()) {
    EVT InEltVT = InVT.getVectorElementType();
    for (int i = MVT::FIRST_VECTOR_VALUETYPE, e = MVT::LAST_VECTOR_VALUETYPE; i < e; ++i) {
      EVT FixedVT = (MVT::SimpleValueType)i;
      EVT FixedEltVT = FixedVT.getVectorElementType();
      if (TLI.isTypeLegal(FixedVT) &&
          FixedVT.getSizeInBits() == VT.getSizeInBits() &&
          FixedEltVT == InEltVT) {
        assert(FixedVT.getVectorNumElements() >= VT.getVectorNumElements() &&
               "Not enough elements in the fixed type for the operand!");
        assert(FixedVT.getVectorNumElements() != InVT.getVectorNumElements() &&
               "We can't have the same type as we started with!");
        if (FixedVT.getVectorNumElements() > InVT.getVectorNumElements())
          InOp = DAG.getNode(
              ISD::INSERT_SUBVECTOR, DL, FixedVT, DAG.getUNDEF(FixedVT), InOp,
              DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
        else
          InOp = DAG.getNode(
              ISD::EXTRACT_SUBVECTOR, DL, FixedVT, InOp,
              DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
        break;
      }
    }
    InVT = InOp.getValueType();
    if (InVT.getSizeInBits() != VT.getSizeInBits())
      // We couldn't find a legal vector type that was a widening of the input
      // and could be extended in-register to the result type, so we have to
      // scalarize.
      return WidenVecOp_Convert(N);
  }

  // Use special DAG nodes to represent the operation of extending the
  // low lanes.
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Extend legalization on extend operation!");
  case ISD::ANY_EXTEND:
    return DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG, DL, VT, InOp);
  case ISD::SIGN_EXTEND:
    return DAG.getNode(ISD::SIGN_EXTEND_VECTOR_INREG, DL, VT, InOp);
  case ISD::ZERO_EXTEND:
    return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, DL, VT, InOp);
  }
}

SDValue DAGTypeLegalizer::WidenVecOp_FCOPYSIGN(SDNode *N) {
  // The result (and first input) is legal, but the second input is illegal.
  // We can't do much to fix that, so just unroll and let the extracts off of
  // the second input be widened as needed later.
  return DAG.UnrollVectorOp(N);
}

SDValue DAGTypeLegalizer::WidenVecOp_Convert(SDNode *N) {
  // Since the result is legal and the input is illegal.
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(N);
  unsigned NumElts = VT.getVectorNumElements();
  SDValue InOp = N->getOperand(0);
  assert(getTypeAction(InOp.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unexpected type action");
  InOp = GetWidenedVector(InOp);
  EVT InVT = InOp.getValueType();
  unsigned Opcode = N->getOpcode();

  // See if a widened result type would be legal, if so widen the node.
  EVT WideVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                                InVT.getVectorNumElements());
  if (TLI.isTypeLegal(WideVT)) {
    SDValue Res = DAG.getNode(Opcode, dl, WideVT, InOp);
    return DAG.getNode(
        ISD::EXTRACT_SUBVECTOR, dl, VT, Res,
        DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }

  EVT InEltVT = InVT.getVectorElementType();

  // Unroll the convert into some scalar code and create a nasty build vector.
  SmallVector<SDValue, 16> Ops(NumElts);
  for (unsigned i=0; i < NumElts; ++i)
    Ops[i] = DAG.getNode(
        Opcode, dl, EltVT,
        DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, InEltVT, InOp,
            DAG.getConstant(i, dl, TLI.getVectorIdxTy(DAG.getDataLayout()))));

  return DAG.getBuildVector(VT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecOp_BITCAST(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  EVT InWidenVT = InOp.getValueType();
  SDLoc dl(N);

  // Check if we can convert between two legal vector types and extract.
  unsigned InWidenSize = InWidenVT.getSizeInBits();
  unsigned Size = VT.getSizeInBits();
  // x86mmx is not an acceptable vector element type, so don't try.
  if (InWidenSize % Size == 0 && !VT.isVector() && VT != MVT::x86mmx) {
    unsigned NewNumElts = InWidenSize / Size;
    EVT NewVT = EVT::getVectorVT(*DAG.getContext(), VT, NewNumElts);
    if (TLI.isTypeLegal(NewVT)) {
      SDValue BitOp = DAG.getNode(ISD::BITCAST, dl, NewVT, InOp);
      return DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, VT, BitOp,
          DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
    }
  }

  return CreateStackStoreLoad(InOp, VT);
}

SDValue DAGTypeLegalizer::WidenVecOp_CONCAT_VECTORS(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  EVT InVT = N->getOperand(0).getValueType();
  SDLoc dl(N);

  // If the widen width for this operand is the same as the width of the concat
  // and all but the first operand is undef, just use the widened operand.
  unsigned NumOperands = N->getNumOperands();
  if (VT == TLI.getTypeToTransformTo(*DAG.getContext(), InVT)) {
    unsigned i;
    for (i = 1; i < NumOperands; ++i)
      if (!N->getOperand(i).isUndef())
        break;

    if (i == NumOperands)
      return GetWidenedVector(N->getOperand(0));
  }

  // Otherwise, fall back to a nasty build vector.
  unsigned NumElts = VT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumElts);

  unsigned NumInElts = InVT.getVectorNumElements();

  unsigned Idx = 0;
  for (unsigned i=0; i < NumOperands; ++i) {
    SDValue InOp = N->getOperand(i);
    assert(getTypeAction(InOp.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unexpected type action");
    InOp = GetWidenedVector(InOp);
    for (unsigned j=0; j < NumInElts; ++j)
      Ops[Idx++] = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
          DAG.getConstant(j, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }
  return DAG.getBuildVector(VT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTRACT_SUBVECTOR(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N),
                     N->getValueType(0), InOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     N->getValueType(0), InOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecOp_STORE(SDNode *N) {
  // We have to widen the value, but we want only to store the original
  // vector type.
  StoreSDNode *ST = cast<StoreSDNode>(N);

  if (!ST->getMemoryVT().getScalarType().isByteSized())
    return TLI.scalarizeVectorStore(ST, DAG);

  SmallVector<SDValue, 16> StChain;
  if (ST->isTruncatingStore())
    GenWidenVectorTruncStores(StChain, ST);
  else
    GenWidenVectorStores(StChain, ST);

  if (StChain.size() == 1)
    return StChain[0];
  else
    return DAG.getNode(ISD::TokenFactor, SDLoc(ST), MVT::Other, StChain);
}

SDValue DAGTypeLegalizer::WidenVecOp_MSTORE(SDNode *N, unsigned OpNo) {
  assert((OpNo == 1 || OpNo == 3) &&
         "Can widen only data or mask operand of mstore");
  MaskedStoreSDNode *MST = cast<MaskedStoreSDNode>(N);
  SDValue Mask = MST->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue StVal = MST->getValue();
  SDLoc dl(N);

  if (OpNo == 1) {
    // Widen the value.
    StVal = GetWidenedVector(StVal);

    // The mask should be widened as well.
    EVT WideVT = StVal.getValueType();
    EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                      MaskVT.getVectorElementType(),
                                      WideVT.getVectorNumElements());
    Mask = ModifyToType(Mask, WideMaskVT, true);
  } else {
    // Widen the mask.
    EVT WideMaskVT = TLI.getTypeToTransformTo(*DAG.getContext(), MaskVT);
    Mask = ModifyToType(Mask, WideMaskVT, true);

    EVT ValueVT = StVal.getValueType();
    EVT WideVT = EVT::getVectorVT(*DAG.getContext(),
                                  ValueVT.getVectorElementType(),
                                  WideMaskVT.getVectorNumElements());
    StVal = ModifyToType(StVal, WideVT);
  }

  assert(Mask.getValueType().getVectorNumElements() ==
         StVal.getValueType().getVectorNumElements() &&
         "Mask and data vectors should have the same number of elements");
  return DAG.getMaskedStore(MST->getChain(), dl, StVal, MST->getBasePtr(),
                            Mask, MST->getMemoryVT(), MST->getMemOperand(),
                            false, MST->isCompressingStore());
}

SDValue DAGTypeLegalizer::WidenVecOp_MGATHER(SDNode *N, unsigned OpNo) {
  assert(OpNo == 4 && "Can widen only the index of mgather");
  auto *MG = cast<MaskedGatherSDNode>(N);
  SDValue DataOp = MG->getPassThru();
  SDValue Mask = MG->getMask();
  SDValue Scale = MG->getScale();

  // Just widen the index. It's allowed to have extra elements.
  SDValue Index = GetWidenedVector(MG->getIndex());

  SDLoc dl(N);
  SDValue Ops[] = {MG->getChain(), DataOp, Mask, MG->getBasePtr(), Index,
                   Scale};
  SDValue Res = DAG.getMaskedGather(MG->getVTList(), MG->getMemoryVT(), dl, Ops,
                                    MG->getMemOperand());
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  ReplaceValueWith(SDValue(N, 0), Res.getValue(0));
  return SDValue();
}

SDValue DAGTypeLegalizer::WidenVecOp_MSCATTER(SDNode *N, unsigned OpNo) {
  MaskedScatterSDNode *MSC = cast<MaskedScatterSDNode>(N);
  SDValue DataOp = MSC->getValue();
  SDValue Mask = MSC->getMask();
  SDValue Index = MSC->getIndex();
  SDValue Scale = MSC->getScale();

  unsigned NumElts;
  if (OpNo == 1) {
    DataOp = GetWidenedVector(DataOp);
    NumElts = DataOp.getValueType().getVectorNumElements();

    // Widen index.
    EVT IndexVT = Index.getValueType();
    EVT WideIndexVT = EVT::getVectorVT(*DAG.getContext(),
                                       IndexVT.getVectorElementType(), NumElts);
    Index = ModifyToType(Index, WideIndexVT);

    // The mask should be widened as well.
    EVT MaskVT = Mask.getValueType();
    EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                      MaskVT.getVectorElementType(), NumElts);
    Mask = ModifyToType(Mask, WideMaskVT, true);
  } else if (OpNo == 4) {
    // Just widen the index. It's allowed to have extra elements.
    Index = GetWidenedVector(Index);
  } else
    llvm_unreachable("Can't widen this operand of mscatter");

  SDValue Ops[] = {MSC->getChain(), DataOp, Mask, MSC->getBasePtr(), Index,
                   Scale};
  return DAG.getMaskedScatter(DAG.getVTList(MVT::Other),
                              MSC->getMemoryVT(), SDLoc(N), Ops,
                              MSC->getMemOperand());
}

SDValue DAGTypeLegalizer::WidenVecOp_SETCC(SDNode *N) {
  SDValue InOp0 = GetWidenedVector(N->getOperand(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  // WARNING: In this code we widen the compare instruction with garbage.
  // This garbage may contain denormal floats which may be slow. Is this a real
  // concern ? Should we zero the unused lanes if this is a float compare ?

  // Get a new SETCC node to compare the newly widened operands.
  // Only some of the compared elements are legal.
  EVT SVT = TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                   InOp0.getValueType());
  // The result type is legal, if its vXi1, keep vXi1 for the new SETCC.
  if (VT.getScalarType() == MVT::i1)
    SVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                           SVT.getVectorNumElements());

  SDValue WideSETCC = DAG.getNode(ISD::SETCC, SDLoc(N),
                                  SVT, InOp0, InOp1, N->getOperand(2));

  // Extract the needed results from the result vector.
  EVT ResVT = EVT::getVectorVT(*DAG.getContext(),
                               SVT.getVectorElementType(),
                               VT.getVectorNumElements());
  SDValue CC = DAG.getNode(
      ISD::EXTRACT_SUBVECTOR, dl, ResVT, WideSETCC,
      DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));

  return PromoteTargetBoolean(CC, VT);
}


//===----------------------------------------------------------------------===//
// Vector Widening Utilities
//===----------------------------------------------------------------------===//

// Utility function to find the type to chop up a widen vector for load/store
//  TLI:       Target lowering used to determine legal types.
//  Width:     Width left need to load/store.
//  WidenVT:   The widen vector type to load to/store from
//  Align:     If 0, don't allow use of a wider type
//  WidenEx:   If Align is not 0, the amount additional we can load/store from.

static EVT FindMemType(SelectionDAG& DAG, const TargetLowering &TLI,
                       unsigned Width, EVT WidenVT,
                       unsigned Align = 0, unsigned WidenEx = 0) {
  EVT WidenEltVT = WidenVT.getVectorElementType();
  unsigned WidenWidth = WidenVT.getSizeInBits();
  unsigned WidenEltWidth = WidenEltVT.getSizeInBits();
  unsigned AlignInBits = Align*8;

  // If we have one element to load/store, return it.
  EVT RetVT = WidenEltVT;
  if (Width == WidenEltWidth)
    return RetVT;

  // See if there is larger legal integer than the element type to load/store.
  unsigned VT;
  for (VT = (unsigned)MVT::LAST_INTEGER_VALUETYPE;
       VT >= (unsigned)MVT::FIRST_INTEGER_VALUETYPE; --VT) {
    EVT MemVT((MVT::SimpleValueType) VT);
    unsigned MemVTWidth = MemVT.getSizeInBits();
    if (MemVT.getSizeInBits() <= WidenEltWidth)
      break;
    auto Action = TLI.getTypeAction(*DAG.getContext(), MemVT);
    if ((Action == TargetLowering::TypeLegal ||
         Action == TargetLowering::TypePromoteInteger) &&
        (WidenWidth % MemVTWidth) == 0 &&
        isPowerOf2_32(WidenWidth / MemVTWidth) &&
        (MemVTWidth <= Width ||
         (Align!=0 && MemVTWidth<=AlignInBits && MemVTWidth<=Width+WidenEx))) {
      RetVT = MemVT;
      break;
    }
  }

  // See if there is a larger vector type to load/store that has the same vector
  // element type and is evenly divisible with the WidenVT.
  for (VT = (unsigned)MVT::LAST_VECTOR_VALUETYPE;
       VT >= (unsigned)MVT::FIRST_VECTOR_VALUETYPE; --VT) {
    EVT MemVT = (MVT::SimpleValueType) VT;
    unsigned MemVTWidth = MemVT.getSizeInBits();
    if (TLI.isTypeLegal(MemVT) && WidenEltVT == MemVT.getVectorElementType() &&
        (WidenWidth % MemVTWidth) == 0 &&
        isPowerOf2_32(WidenWidth / MemVTWidth) &&
        (MemVTWidth <= Width ||
         (Align!=0 && MemVTWidth<=AlignInBits && MemVTWidth<=Width+WidenEx))) {
      if (RetVT.getSizeInBits() < MemVTWidth || MemVT == WidenVT)
        return MemVT;
    }
  }

  return RetVT;
}

// Builds a vector type from scalar loads
//  VecTy: Resulting Vector type
//  LDOps: Load operators to build a vector type
//  [Start,End) the list of loads to use.
static SDValue BuildVectorFromScalar(SelectionDAG& DAG, EVT VecTy,
                                     SmallVectorImpl<SDValue> &LdOps,
                                     unsigned Start, unsigned End) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(LdOps[Start]);
  EVT LdTy = LdOps[Start].getValueType();
  unsigned Width = VecTy.getSizeInBits();
  unsigned NumElts = Width / LdTy.getSizeInBits();
  EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), LdTy, NumElts);

  unsigned Idx = 1;
  SDValue VecOp = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewVecVT,LdOps[Start]);

  for (unsigned i = Start + 1; i != End; ++i) {
    EVT NewLdTy = LdOps[i].getValueType();
    if (NewLdTy != LdTy) {
      NumElts = Width / NewLdTy.getSizeInBits();
      NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewLdTy, NumElts);
      VecOp = DAG.getNode(ISD::BITCAST, dl, NewVecVT, VecOp);
      // Readjust position and vector position based on new load type.
      Idx = Idx * LdTy.getSizeInBits() / NewLdTy.getSizeInBits();
      LdTy = NewLdTy;
    }
    VecOp = DAG.getNode(
        ISD::INSERT_VECTOR_ELT, dl, NewVecVT, VecOp, LdOps[i],
        DAG.getConstant(Idx++, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }
  return DAG.getNode(ISD::BITCAST, dl, VecTy, VecOp);
}

SDValue DAGTypeLegalizer::GenWidenVectorLoads(SmallVectorImpl<SDValue> &LdChain,
                                              LoadSDNode *LD) {
  // The strategy assumes that we can efficiently load power-of-two widths.
  // The routine chops the vector into the largest vector loads with the same
  // element type or scalar loads and then recombines it to the widen vector
  // type.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),LD->getValueType(0));
  unsigned WidenWidth = WidenVT.getSizeInBits();
  EVT LdVT    = LD->getMemoryVT();
  SDLoc dl(LD);
  assert(LdVT.isVector() && WidenVT.isVector());
  assert(LdVT.getVectorElementType() == WidenVT.getVectorElementType());

  // Load information
  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  unsigned Align = LD->getAlignment();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  int LdWidth = LdVT.getSizeInBits();
  int WidthDiff = WidenWidth - LdWidth;
  unsigned LdAlign = LD->isVolatile() ? 0 : Align; // Allow wider loads.

  // Find the vector type that can load from.
  EVT NewVT = FindMemType(DAG, TLI, LdWidth, WidenVT, LdAlign, WidthDiff);
  int NewVTWidth = NewVT.getSizeInBits();
  SDValue LdOp = DAG.getLoad(NewVT, dl, Chain, BasePtr, LD->getPointerInfo(),
                             Align, MMOFlags, AAInfo);
  LdChain.push_back(LdOp.getValue(1));

  // Check if we can load the element with one instruction.
  if (LdWidth <= NewVTWidth) {
    if (!NewVT.isVector()) {
      unsigned NumElts = WidenWidth / NewVTWidth;
      EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewVT, NumElts);
      SDValue VecOp = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewVecVT, LdOp);
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, VecOp);
    }
    if (NewVT == WidenVT)
      return LdOp;

    assert(WidenWidth % NewVTWidth == 0);
    unsigned NumConcat = WidenWidth / NewVTWidth;
    SmallVector<SDValue, 16> ConcatOps(NumConcat);
    SDValue UndefVal = DAG.getUNDEF(NewVT);
    ConcatOps[0] = LdOp;
    for (unsigned i = 1; i != NumConcat; ++i)
      ConcatOps[i] = UndefVal;
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, ConcatOps);
  }

  // Load vector by using multiple loads from largest vector to scalar.
  SmallVector<SDValue, 16> LdOps;
  LdOps.push_back(LdOp);

  LdWidth -= NewVTWidth;
  unsigned Offset = 0;

  while (LdWidth > 0) {
    unsigned Increment = NewVTWidth / 8;
    Offset += Increment;
    BasePtr = DAG.getObjectPtrOffset(dl, BasePtr, Increment);

    SDValue L;
    if (LdWidth < NewVTWidth) {
      // The current type we are using is too large. Find a better size.
      NewVT = FindMemType(DAG, TLI, LdWidth, WidenVT, LdAlign, WidthDiff);
      NewVTWidth = NewVT.getSizeInBits();
      L = DAG.getLoad(NewVT, dl, Chain, BasePtr,
                      LD->getPointerInfo().getWithOffset(Offset),
                      MinAlign(Align, Increment), MMOFlags, AAInfo);
      LdChain.push_back(L.getValue(1));
      if (L->getValueType(0).isVector() && NewVTWidth >= LdWidth) {
        // Later code assumes the vector loads produced will be mergeable, so we
        // must pad the final entry up to the previous width. Scalars are
        // combined separately.
        SmallVector<SDValue, 16> Loads;
        Loads.push_back(L);
        unsigned size = L->getValueSizeInBits(0);
        while (size < LdOp->getValueSizeInBits(0)) {
          Loads.push_back(DAG.getUNDEF(L->getValueType(0)));
          size += L->getValueSizeInBits(0);
        }
        L = DAG.getNode(ISD::CONCAT_VECTORS, dl, LdOp->getValueType(0), Loads);
      }
    } else {
      L = DAG.getLoad(NewVT, dl, Chain, BasePtr,
                      LD->getPointerInfo().getWithOffset(Offset),
                      MinAlign(Align, Increment), MMOFlags, AAInfo);
      LdChain.push_back(L.getValue(1));
    }

    LdOps.push_back(L);
    LdOp = L;

    LdWidth -= NewVTWidth;
  }

  // Build the vector from the load operations.
  unsigned End = LdOps.size();
  if (!LdOps[0].getValueType().isVector())
    // All the loads are scalar loads.
    return BuildVectorFromScalar(DAG, WidenVT, LdOps, 0, End);

  // If the load contains vectors, build the vector using concat vector.
  // All of the vectors used to load are power-of-2, and the scalar loads can be
  // combined to make a power-of-2 vector.
  SmallVector<SDValue, 16> ConcatOps(End);
  int i = End - 1;
  int Idx = End;
  EVT LdTy = LdOps[i].getValueType();
  // First, combine the scalar loads to a vector.
  if (!LdTy.isVector())  {
    for (--i; i >= 0; --i) {
      LdTy = LdOps[i].getValueType();
      if (LdTy.isVector())
        break;
    }
    ConcatOps[--Idx] = BuildVectorFromScalar(DAG, LdTy, LdOps, i + 1, End);
  }
  ConcatOps[--Idx] = LdOps[i];
  for (--i; i >= 0; --i) {
    EVT NewLdTy = LdOps[i].getValueType();
    if (NewLdTy != LdTy) {
      // Create a larger vector.
      ConcatOps[End-1] = DAG.getNode(ISD::CONCAT_VECTORS, dl, NewLdTy,
                                     makeArrayRef(&ConcatOps[Idx], End - Idx));
      Idx = End - 1;
      LdTy = NewLdTy;
    }
    ConcatOps[--Idx] = LdOps[i];
  }

  if (WidenWidth == LdTy.getSizeInBits() * (End - Idx))
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT,
                       makeArrayRef(&ConcatOps[Idx], End - Idx));

  // We need to fill the rest with undefs to build the vector.
  unsigned NumOps = WidenWidth / LdTy.getSizeInBits();
  SmallVector<SDValue, 16> WidenOps(NumOps);
  SDValue UndefVal = DAG.getUNDEF(LdTy);
  {
    unsigned i = 0;
    for (; i != End-Idx; ++i)
      WidenOps[i] = ConcatOps[Idx+i];
    for (; i != NumOps; ++i)
      WidenOps[i] = UndefVal;
  }
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, WidenOps);
}

SDValue
DAGTypeLegalizer::GenWidenVectorExtLoads(SmallVectorImpl<SDValue> &LdChain,
                                         LoadSDNode *LD,
                                         ISD::LoadExtType ExtType) {
  // For extension loads, it may not be more efficient to chop up the vector
  // and then extend it. Instead, we unroll the load and build a new vector.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),LD->getValueType(0));
  EVT LdVT    = LD->getMemoryVT();
  SDLoc dl(LD);
  assert(LdVT.isVector() && WidenVT.isVector());

  // Load information
  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  unsigned Align = LD->getAlignment();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  EVT EltVT = WidenVT.getVectorElementType();
  EVT LdEltVT = LdVT.getVectorElementType();
  unsigned NumElts = LdVT.getVectorNumElements();

  // Load each element and widen.
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  unsigned Increment = LdEltVT.getSizeInBits() / 8;
  Ops[0] =
      DAG.getExtLoad(ExtType, dl, EltVT, Chain, BasePtr, LD->getPointerInfo(),
                     LdEltVT, Align, MMOFlags, AAInfo);
  LdChain.push_back(Ops[0].getValue(1));
  unsigned i = 0, Offset = Increment;
  for (i=1; i < NumElts; ++i, Offset += Increment) {
    SDValue NewBasePtr = DAG.getObjectPtrOffset(dl, BasePtr, Offset);
    Ops[i] = DAG.getExtLoad(ExtType, dl, EltVT, Chain, NewBasePtr,
                            LD->getPointerInfo().getWithOffset(Offset), LdEltVT,
                            Align, MMOFlags, AAInfo);
    LdChain.push_back(Ops[i].getValue(1));
  }

  // Fill the rest with undefs.
  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; i != WidenNumElts; ++i)
    Ops[i] = UndefVal;

  return DAG.getBuildVector(WidenVT, dl, Ops);
}

void DAGTypeLegalizer::GenWidenVectorStores(SmallVectorImpl<SDValue> &StChain,
                                            StoreSDNode *ST) {
  // The strategy assumes that we can efficiently store power-of-two widths.
  // The routine chops the vector into the largest vector stores with the same
  // element type or scalar stores.
  SDValue  Chain = ST->getChain();
  SDValue  BasePtr = ST->getBasePtr();
  unsigned Align = ST->getAlignment();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();
  SDValue  ValOp = GetWidenedVector(ST->getValue());
  SDLoc dl(ST);

  EVT StVT = ST->getMemoryVT();
  unsigned StWidth = StVT.getSizeInBits();
  EVT ValVT = ValOp.getValueType();
  unsigned ValWidth = ValVT.getSizeInBits();
  EVT ValEltVT = ValVT.getVectorElementType();
  unsigned ValEltWidth = ValEltVT.getSizeInBits();
  assert(StVT.getVectorElementType() == ValEltVT);

  int Idx = 0;          // current index to store
  unsigned Offset = 0;  // offset from base to store
  while (StWidth != 0) {
    // Find the largest vector type we can store with.
    EVT NewVT = FindMemType(DAG, TLI, StWidth, ValVT);
    unsigned NewVTWidth = NewVT.getSizeInBits();
    unsigned Increment = NewVTWidth / 8;
    if (NewVT.isVector()) {
      unsigned NumVTElts = NewVT.getVectorNumElements();
      do {
        SDValue EOp = DAG.getNode(
            ISD::EXTRACT_SUBVECTOR, dl, NewVT, ValOp,
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
        StChain.push_back(DAG.getStore(
            Chain, dl, EOp, BasePtr, ST->getPointerInfo().getWithOffset(Offset),
            MinAlign(Align, Offset), MMOFlags, AAInfo));
        StWidth -= NewVTWidth;
        Offset += Increment;
        Idx += NumVTElts;

        BasePtr = DAG.getObjectPtrOffset(dl, BasePtr, Increment);
      } while (StWidth != 0 && StWidth >= NewVTWidth);
    } else {
      // Cast the vector to the scalar type we can store.
      unsigned NumElts = ValWidth / NewVTWidth;
      EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewVT, NumElts);
      SDValue VecOp = DAG.getNode(ISD::BITCAST, dl, NewVecVT, ValOp);
      // Readjust index position based on new vector type.
      Idx = Idx * ValEltWidth / NewVTWidth;
      do {
        SDValue EOp = DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, NewVT, VecOp,
            DAG.getConstant(Idx++, dl,
                            TLI.getVectorIdxTy(DAG.getDataLayout())));
        StChain.push_back(DAG.getStore(
            Chain, dl, EOp, BasePtr, ST->getPointerInfo().getWithOffset(Offset),
            MinAlign(Align, Offset), MMOFlags, AAInfo));
        StWidth -= NewVTWidth;
        Offset += Increment;
        BasePtr = DAG.getObjectPtrOffset(dl, BasePtr, Increment);
      } while (StWidth != 0 && StWidth >= NewVTWidth);
      // Restore index back to be relative to the original widen element type.
      Idx = Idx * NewVTWidth / ValEltWidth;
    }
  }
}

void
DAGTypeLegalizer::GenWidenVectorTruncStores(SmallVectorImpl<SDValue> &StChain,
                                            StoreSDNode *ST) {
  // For extension loads, it may not be more efficient to truncate the vector
  // and then store it. Instead, we extract each element and then store it.
  SDValue Chain = ST->getChain();
  SDValue BasePtr = ST->getBasePtr();
  unsigned Align = ST->getAlignment();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();
  SDValue ValOp = GetWidenedVector(ST->getValue());
  SDLoc dl(ST);

  EVT StVT = ST->getMemoryVT();
  EVT ValVT = ValOp.getValueType();

  // It must be true that the wide vector type is bigger than where we need to
  // store.
  assert(StVT.isVector() && ValOp.getValueType().isVector());
  assert(StVT.bitsLT(ValOp.getValueType()));

  // For truncating stores, we can not play the tricks of chopping legal vector
  // types and bitcast it to the right type. Instead, we unroll the store.
  EVT StEltVT  = StVT.getVectorElementType();
  EVT ValEltVT = ValVT.getVectorElementType();
  unsigned Increment = ValEltVT.getSizeInBits() / 8;
  unsigned NumElts = StVT.getVectorNumElements();
  SDValue EOp = DAG.getNode(
      ISD::EXTRACT_VECTOR_ELT, dl, ValEltVT, ValOp,
      DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  StChain.push_back(DAG.getTruncStore(Chain, dl, EOp, BasePtr,
                                      ST->getPointerInfo(), StEltVT, Align,
                                      MMOFlags, AAInfo));
  unsigned Offset = Increment;
  for (unsigned i=1; i < NumElts; ++i, Offset += Increment) {
    SDValue NewBasePtr = DAG.getObjectPtrOffset(dl, BasePtr, Offset);
    SDValue EOp = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, dl, ValEltVT, ValOp,
        DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
    StChain.push_back(DAG.getTruncStore(
        Chain, dl, EOp, NewBasePtr, ST->getPointerInfo().getWithOffset(Offset),
        StEltVT, MinAlign(Align, Offset), MMOFlags, AAInfo));
  }
}

/// Modifies a vector input (widen or narrows) to a vector of NVT.  The
/// input vector must have the same element type as NVT.
/// FillWithZeroes specifies that the vector should be widened with zeroes.
SDValue DAGTypeLegalizer::ModifyToType(SDValue InOp, EVT NVT,
                                       bool FillWithZeroes) {
  // Note that InOp might have been widened so it might already have
  // the right width or it might need be narrowed.
  EVT InVT = InOp.getValueType();
  assert(InVT.getVectorElementType() == NVT.getVectorElementType() &&
         "input and widen element type must match");
  SDLoc dl(InOp);

  // Check if InOp already has the right width.
  if (InVT == NVT)
    return InOp;

  unsigned InNumElts = InVT.getVectorNumElements();
  unsigned WidenNumElts = NVT.getVectorNumElements();
  if (WidenNumElts > InNumElts && WidenNumElts % InNumElts == 0) {
    unsigned NumConcat = WidenNumElts / InNumElts;
    SmallVector<SDValue, 16> Ops(NumConcat);
    SDValue FillVal = FillWithZeroes ? DAG.getConstant(0, dl, InVT) :
      DAG.getUNDEF(InVT);
    Ops[0] = InOp;
    for (unsigned i = 1; i != NumConcat; ++i)
      Ops[i] = FillVal;

    return DAG.getNode(ISD::CONCAT_VECTORS, dl, NVT, Ops);
  }

  if (WidenNumElts < InNumElts && InNumElts % WidenNumElts)
    return DAG.getNode(
        ISD::EXTRACT_SUBVECTOR, dl, NVT, InOp,
        DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));

  // Fall back to extract and build.
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  EVT EltVT = NVT.getVectorElementType();
  unsigned MinNumElts = std::min(WidenNumElts, InNumElts);
  unsigned Idx;
  for (Idx = 0; Idx < MinNumElts; ++Idx)
    Ops[Idx] = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
        DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));

  SDValue FillVal = FillWithZeroes ? DAG.getConstant(0, dl, EltVT) :
    DAG.getUNDEF(EltVT);
  for ( ; Idx < WidenNumElts; ++Idx)
    Ops[Idx] = FillVal;
  return DAG.getBuildVector(NVT, dl, Ops);
}
