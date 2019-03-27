//===-- AMDGPUISelLowering.cpp - AMDGPU Common DAG lowering functions -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the parent TargetLowering class for hardware code gen
/// targets.
//
//===----------------------------------------------------------------------===//

#define AMDGPU_LOG2E_F     1.44269504088896340735992468100189214f
#define AMDGPU_LN2_F       0.693147180559945309417232121458176568f
#define AMDGPU_LN10_F      2.30258509299404568401799145468436421f

#include "AMDGPUISelLowering.h"
#include "AMDGPU.h"
#include "AMDGPUCallLowering.h"
#include "AMDGPUFrameLowering.h"
#include "AMDGPUIntrinsicInfo.h"
#include "AMDGPURegisterInfo.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "R600MachineFunctionInfo.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/KnownBits.h"
using namespace llvm;

static bool allocateCCRegs(unsigned ValNo, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo,
                           ISD::ArgFlagsTy ArgFlags, CCState &State,
                           const TargetRegisterClass *RC,
                           unsigned NumRegs) {
  ArrayRef<MCPhysReg> RegList = makeArrayRef(RC->begin(), NumRegs);
  unsigned RegResult = State.AllocateReg(RegList);
  if (RegResult == AMDGPU::NoRegister)
    return false;

  State.addLoc(CCValAssign::getReg(ValNo, ValVT, RegResult, LocVT, LocInfo));
  return true;
}

static bool allocateSGPRTuple(unsigned ValNo, MVT ValVT, MVT LocVT,
                              CCValAssign::LocInfo LocInfo,
                              ISD::ArgFlagsTy ArgFlags, CCState &State) {
  switch (LocVT.SimpleTy) {
  case MVT::i64:
  case MVT::f64:
  case MVT::v2i32:
  case MVT::v2f32:
  case MVT::v4i16:
  case MVT::v4f16: {
    // Up to SGPR0-SGPR39
    return allocateCCRegs(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State,
                          &AMDGPU::SGPR_64RegClass, 20);
  }
  default:
    return false;
  }
}

// Allocate up to VGPR31.
//
// TODO: Since there are no VGPR alignent requirements would it be better to
// split into individual scalar registers?
static bool allocateVGPRTuple(unsigned ValNo, MVT ValVT, MVT LocVT,
                              CCValAssign::LocInfo LocInfo,
                              ISD::ArgFlagsTy ArgFlags, CCState &State) {
  switch (LocVT.SimpleTy) {
  case MVT::i64:
  case MVT::f64:
  case MVT::v2i32:
  case MVT::v2f32:
  case MVT::v4i16:
  case MVT::v4f16: {
    return allocateCCRegs(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State,
                          &AMDGPU::VReg_64RegClass, 31);
  }
  case MVT::v4i32:
  case MVT::v4f32:
  case MVT::v2i64:
  case MVT::v2f64: {
    return allocateCCRegs(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State,
                          &AMDGPU::VReg_128RegClass, 29);
  }
  case MVT::v8i32:
  case MVT::v8f32: {
    return allocateCCRegs(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State,
                          &AMDGPU::VReg_256RegClass, 25);

  }
  case MVT::v16i32:
  case MVT::v16f32: {
    return allocateCCRegs(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State,
                          &AMDGPU::VReg_512RegClass, 17);

  }
  default:
    return false;
  }
}

#include "AMDGPUGenCallingConv.inc"

// Find a larger type to do a load / store of a vector with.
EVT AMDGPUTargetLowering::getEquivalentMemType(LLVMContext &Ctx, EVT VT) {
  unsigned StoreSize = VT.getStoreSizeInBits();
  if (StoreSize <= 32)
    return EVT::getIntegerVT(Ctx, StoreSize);

  assert(StoreSize % 32 == 0 && "Store size not a multiple of 32");
  return EVT::getVectorVT(Ctx, MVT::i32, StoreSize / 32);
}

unsigned AMDGPUTargetLowering::numBitsUnsigned(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  KnownBits Known = DAG.computeKnownBits(Op);
  return VT.getSizeInBits() - Known.countMinLeadingZeros();
}

unsigned AMDGPUTargetLowering::numBitsSigned(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();

  // In order for this to be a signed 24-bit value, bit 23, must
  // be a sign bit.
  return VT.getSizeInBits() - DAG.ComputeNumSignBits(Op);
}

AMDGPUTargetLowering::AMDGPUTargetLowering(const TargetMachine &TM,
                                           const AMDGPUSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  // Lower floating point store/load to integer store/load to reduce the number
  // of patterns in tablegen.
  setOperationAction(ISD::LOAD, MVT::f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::f32, MVT::i32);

  setOperationAction(ISD::LOAD, MVT::v2f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v4f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v4f32, MVT::v4i32);

  setOperationAction(ISD::LOAD, MVT::v8f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v8f32, MVT::v8i32);

  setOperationAction(ISD::LOAD, MVT::v16f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v16f32, MVT::v16i32);

  setOperationAction(ISD::LOAD, MVT::i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::i64, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v2i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2i64, MVT::v4i32);

  setOperationAction(ISD::LOAD, MVT::f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::f64, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v2f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2f64, MVT::v4i32);

  // There are no 64-bit extloads. These should be done as a 32-bit extload and
  // an extension to 64-bit.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, MVT::i64, VT, Expand);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i64, VT, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, VT, Expand);
  }

  for (MVT VT : MVT::integer_valuetypes()) {
    if (VT == MVT::i64)
      continue;

    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i16, Legal);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i32, Expand);

    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i16, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i32, Expand);

    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i16, Legal);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i32, Expand);
  }

  for (MVT VT : MVT::integer_vector_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::v2i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::v2i8, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::v2i8, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::v4i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::v4i8, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::v4i8, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::v2i16, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::v2i16, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::v2i16, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::v4i16, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::v4i16, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::v4i16, Expand);
  }

  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f32, MVT::v8f16, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8f32, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8f16, Expand);

  setOperationAction(ISD::STORE, MVT::f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::f32, MVT::i32);

  setOperationAction(ISD::STORE, MVT::v2f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v4f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v4f32, MVT::v4i32);

  setOperationAction(ISD::STORE, MVT::v8f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v8f32, MVT::v8i32);

  setOperationAction(ISD::STORE, MVT::v16f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v16f32, MVT::v16i32);

  setOperationAction(ISD::STORE, MVT::i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::i64, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v2i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2i64, MVT::v4i32);

  setOperationAction(ISD::STORE, MVT::f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::f64, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v2f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2f64, MVT::v4i32);

  setTruncStoreAction(MVT::i64, MVT::i1, Expand);
  setTruncStoreAction(MVT::i64, MVT::i8, Expand);
  setTruncStoreAction(MVT::i64, MVT::i16, Expand);
  setTruncStoreAction(MVT::i64, MVT::i32, Expand);

  setTruncStoreAction(MVT::v2i64, MVT::v2i1, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i8, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i16, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i32, Expand);

  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::v2f32, MVT::v2f16, Expand);
  setTruncStoreAction(MVT::v4f32, MVT::v4f16, Expand);
  setTruncStoreAction(MVT::v8f32, MVT::v8f16, Expand);

  setTruncStoreAction(MVT::f64, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  setTruncStoreAction(MVT::v2f64, MVT::v2f32, Expand);
  setTruncStoreAction(MVT::v2f64, MVT::v2f16, Expand);

  setTruncStoreAction(MVT::v4f64, MVT::v4f32, Expand);
  setTruncStoreAction(MVT::v4f64, MVT::v4f16, Expand);

  setTruncStoreAction(MVT::v8f64, MVT::v8f32, Expand);
  setTruncStoreAction(MVT::v8f64, MVT::v8f16, Expand);


  setOperationAction(ISD::Constant, MVT::i32, Legal);
  setOperationAction(ISD::Constant, MVT::i64, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);

  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);

  // This is totally unsupported, just custom lower to produce an error.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);

  // Library functions.  These default to Expand, but we have instructions
  // for them.
  setOperationAction(ISD::FCEIL,  MVT::f32, Legal);
  setOperationAction(ISD::FEXP2,  MVT::f32, Legal);
  setOperationAction(ISD::FPOW,   MVT::f32, Legal);
  setOperationAction(ISD::FLOG2,  MVT::f32, Legal);
  setOperationAction(ISD::FABS,   MVT::f32, Legal);
  setOperationAction(ISD::FFLOOR, MVT::f32, Legal);
  setOperationAction(ISD::FRINT,  MVT::f32, Legal);
  setOperationAction(ISD::FTRUNC, MVT::f32, Legal);
  setOperationAction(ISD::FMINNUM, MVT::f32, Legal);
  setOperationAction(ISD::FMAXNUM, MVT::f32, Legal);

  setOperationAction(ISD::FROUND, MVT::f32, Custom);
  setOperationAction(ISD::FROUND, MVT::f64, Custom);

  setOperationAction(ISD::FLOG, MVT::f32, Custom);
  setOperationAction(ISD::FLOG10, MVT::f32, Custom);
  setOperationAction(ISD::FEXP, MVT::f32, Custom);


  setOperationAction(ISD::FNEARBYINT, MVT::f32, Custom);
  setOperationAction(ISD::FNEARBYINT, MVT::f64, Custom);

  setOperationAction(ISD::FREM, MVT::f32, Custom);
  setOperationAction(ISD::FREM, MVT::f64, Custom);

  // Expand to fneg + fadd.
  setOperationAction(ISD::FSUB, MVT::f64, Expand);

  setOperationAction(ISD::CONCAT_VECTORS, MVT::v4i32, Custom);
  setOperationAction(ISD::CONCAT_VECTORS, MVT::v4f32, Custom);
  setOperationAction(ISD::CONCAT_VECTORS, MVT::v8i32, Custom);
  setOperationAction(ISD::CONCAT_VECTORS, MVT::v8f32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v2f32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v2i32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v4f32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v4i32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v8f32, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, MVT::v8i32, Custom);

  setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
  setOperationAction(ISD::FP_TO_FP16, MVT::f64, Custom);
  setOperationAction(ISD::FP_TO_FP16, MVT::f32, Custom);

  const MVT ScalarIntVTs[] = { MVT::i32, MVT::i64 };
  for (MVT VT : ScalarIntVTs) {
    // These should use [SU]DIVREM, so set them to expand
    setOperationAction(ISD::SDIV, VT, Expand);
    setOperationAction(ISD::UDIV, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);

    // GPU does not have divrem function for signed or unsigned.
    setOperationAction(ISD::SDIVREM, VT, Custom);
    setOperationAction(ISD::UDIVREM, VT, Custom);

    // GPU does not have [S|U]MUL_LOHI functions as a single instruction.
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);

    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);

    // AMDGPU uses ADDC/SUBC/ADDE/SUBE
    setOperationAction(ISD::ADDC, VT, Legal);
    setOperationAction(ISD::SUBC, VT, Legal);
    setOperationAction(ISD::ADDE, VT, Legal);
    setOperationAction(ISD::SUBE, VT, Legal);
  }

  // The hardware supports 32-bit ROTR, but not ROTL.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);

  setOperationAction(ISD::MUL, MVT::i64, Expand);
  setOperationAction(ISD::MULHU, MVT::i64, Expand);
  setOperationAction(ISD::MULHS, MVT::i64, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);

  setOperationAction(ISD::SMIN, MVT::i32, Legal);
  setOperationAction(ISD::UMIN, MVT::i32, Legal);
  setOperationAction(ISD::SMAX, MVT::i32, Legal);
  setOperationAction(ISD::UMAX, MVT::i32, Legal);

  setOperationAction(ISD::CTTZ, MVT::i64, Custom);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i64, Custom);

  static const MVT::SimpleValueType VectorIntTypes[] = {
    MVT::v2i32, MVT::v4i32
  };

  for (MVT VT : VectorIntTypes) {
    // Expand the following operations for the current type by default.
    setOperationAction(ISD::ADD,  VT, Expand);
    setOperationAction(ISD::AND,  VT, Expand);
    setOperationAction(ISD::FP_TO_SINT, VT, Expand);
    setOperationAction(ISD::FP_TO_UINT, VT, Expand);
    setOperationAction(ISD::MUL,  VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::OR,   VT, Expand);
    setOperationAction(ISD::SHL,  VT, Expand);
    setOperationAction(ISD::SRA,  VT, Expand);
    setOperationAction(ISD::SRL,  VT, Expand);
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
    setOperationAction(ISD::SUB,  VT, Expand);
    setOperationAction(ISD::SINT_TO_FP, VT, Expand);
    setOperationAction(ISD::UINT_TO_FP, VT, Expand);
    setOperationAction(ISD::SDIV, VT, Expand);
    setOperationAction(ISD::UDIV, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Custom);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::VSELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    setOperationAction(ISD::XOR,  VT, Expand);
    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Expand);
    setOperationAction(ISD::SETCC, VT, Expand);
  }

  static const MVT::SimpleValueType FloatVectorTypes[] = {
    MVT::v2f32, MVT::v4f32
  };

  for (MVT VT : FloatVectorTypes) {
    setOperationAction(ISD::FABS, VT, Expand);
    setOperationAction(ISD::FMINNUM, VT, Expand);
    setOperationAction(ISD::FMAXNUM, VT, Expand);
    setOperationAction(ISD::FADD, VT, Expand);
    setOperationAction(ISD::FCEIL, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FDIV, VT, Expand);
    setOperationAction(ISD::FEXP2, VT, Expand);
    setOperationAction(ISD::FEXP, VT, Expand);
    setOperationAction(ISD::FLOG2, VT, Expand);
    setOperationAction(ISD::FREM, VT, Expand);
    setOperationAction(ISD::FLOG, VT, Expand);
    setOperationAction(ISD::FLOG10, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    setOperationAction(ISD::FFLOOR, VT, Expand);
    setOperationAction(ISD::FTRUNC, VT, Expand);
    setOperationAction(ISD::FMUL, VT, Expand);
    setOperationAction(ISD::FMA, VT, Expand);
    setOperationAction(ISD::FRINT, VT, Expand);
    setOperationAction(ISD::FNEARBYINT, VT, Expand);
    setOperationAction(ISD::FSQRT, VT, Expand);
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FSUB, VT, Expand);
    setOperationAction(ISD::FNEG, VT, Expand);
    setOperationAction(ISD::VSELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    setOperationAction(ISD::FCOPYSIGN, VT, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Expand);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::FCANONICALIZE, VT, Expand);
  }

  // This causes using an unrolled select operation rather than expansion with
  // bit operations. This is in general better, but the alternative using BFI
  // instructions may be better if the select sources are SGPRs.
  setOperationAction(ISD::SELECT, MVT::v2f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::SELECT, MVT::v4f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v4f32, MVT::v4i32);

  // There are no libcalls of any kind.
  for (int I = 0; I < RTLIB::UNKNOWN_LIBCALL; ++I)
    setLibcallName(static_cast<RTLIB::Libcall>(I), nullptr);

  setBooleanContents(ZeroOrNegativeOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  setSchedulingPreference(Sched::RegPressure);
  setJumpIsExpensive(true);

  // FIXME: This is only partially true. If we have to do vector compares, any
  // SGPR pair can be a condition register. If we have a uniform condition, we
  // are better off doing SALU operations, where there is only one SCC. For now,
  // we don't have a way of knowing during instruction selection if a condition
  // will be uniform and we always use vector compares. Assume we are using
  // vector compares until that is fixed.
  setHasMultipleConditionRegisters(true);

  PredictableSelectIsExpensive = false;

  // We want to find all load dependencies for long chains of stores to enable
  // merging into very wide vectors. The problem is with vectors with > 4
  // elements. MergeConsecutiveStores will attempt to merge these because x8/x16
  // vectors are a legal type, even though we have to split the loads
  // usually. When we can more precisely specify load legality per address
  // space, we should be able to make FindBetterChain/MergeConsecutiveStores
  // smarter so that they can figure out what to do in 2 iterations without all
  // N > 4 stores on the same chain.
  GatherAllAliasesMaxDepth = 16;

  // memcpy/memmove/memset are expanded in the IR, so we shouldn't need to worry
  // about these during lowering.
  MaxStoresPerMemcpy  = 0xffffffff;
  MaxStoresPerMemmove = 0xffffffff;
  MaxStoresPerMemset  = 0xffffffff;

  setTargetDAGCombine(ISD::BITCAST);
  setTargetDAGCombine(ISD::SHL);
  setTargetDAGCombine(ISD::SRA);
  setTargetDAGCombine(ISD::SRL);
  setTargetDAGCombine(ISD::TRUNCATE);
  setTargetDAGCombine(ISD::MUL);
  setTargetDAGCombine(ISD::MULHU);
  setTargetDAGCombine(ISD::MULHS);
  setTargetDAGCombine(ISD::SELECT);
  setTargetDAGCombine(ISD::SELECT_CC);
  setTargetDAGCombine(ISD::STORE);
  setTargetDAGCombine(ISD::FADD);
  setTargetDAGCombine(ISD::FSUB);
  setTargetDAGCombine(ISD::FNEG);
  setTargetDAGCombine(ISD::FABS);
  setTargetDAGCombine(ISD::AssertZext);
  setTargetDAGCombine(ISD::AssertSext);
}

//===----------------------------------------------------------------------===//
// Target Information
//===----------------------------------------------------------------------===//

LLVM_READNONE
static bool fnegFoldsIntoOp(unsigned Opc) {
  switch (Opc) {
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FMA:
  case ISD::FMAD:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FSIN:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FCANONICALIZE:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::FMUL_LEGACY:
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY:
  case AMDGPUISD::FMED3:
    return true;
  default:
    return false;
  }
}

/// \p returns true if the operation will definitely need to use a 64-bit
/// encoding, and thus will use a VOP3 encoding regardless of the source
/// modifiers.
LLVM_READONLY
static bool opMustUseVOP3Encoding(const SDNode *N, MVT VT) {
  return N->getNumOperands() > 2 || VT == MVT::f64;
}

// Most FP instructions support source modifiers, but this could be refined
// slightly.
LLVM_READONLY
static bool hasSourceMods(const SDNode *N) {
  if (isa<MemSDNode>(N))
    return false;

  switch (N->getOpcode()) {
  case ISD::CopyToReg:
  case ISD::SELECT:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::INLINEASM:
  case AMDGPUISD::INTERP_P1:
  case AMDGPUISD::INTERP_P2:
  case AMDGPUISD::DIV_SCALE:

  // TODO: Should really be looking at the users of the bitcast. These are
  // problematic because bitcasts are used to legalize all stores to integer
  // types.
  case ISD::BITCAST:
    return false;
  default:
    return true;
  }
}

bool AMDGPUTargetLowering::allUsesHaveSourceMods(const SDNode *N,
                                                 unsigned CostThreshold) {
  // Some users (such as 3-operand FMA/MAD) must use a VOP3 encoding, and thus
  // it is truly free to use a source modifier in all cases. If there are
  // multiple users but for each one will necessitate using VOP3, there will be
  // a code size increase. Try to avoid increasing code size unless we know it
  // will save on the instruction count.
  unsigned NumMayIncreaseSize = 0;
  MVT VT = N->getValueType(0).getScalarType().getSimpleVT();

  // XXX - Should this limit number of uses to check?
  for (const SDNode *U : N->uses()) {
    if (!hasSourceMods(U))
      return false;

    if (!opMustUseVOP3Encoding(U, VT)) {
      if (++NumMayIncreaseSize > CostThreshold)
        return false;
    }
  }

  return true;
}

MVT AMDGPUTargetLowering::getVectorIdxTy(const DataLayout &) const {
  return MVT::i32;
}

bool AMDGPUTargetLowering::isSelectSupported(SelectSupportKind SelType) const {
  return true;
}

// The backend supports 32 and 64 bit floating point immediates.
// FIXME: Why are we reporting vectors of FP immediates as legal?
bool AMDGPUTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  EVT ScalarVT = VT.getScalarType();
  return (ScalarVT == MVT::f32 || ScalarVT == MVT::f64 ||
         (ScalarVT == MVT::f16 && Subtarget->has16BitInsts()));
}

// We don't want to shrink f64 / f32 constants.
bool AMDGPUTargetLowering::ShouldShrinkFPConstant(EVT VT) const {
  EVT ScalarVT = VT.getScalarType();
  return (ScalarVT != MVT::f32 && ScalarVT != MVT::f64);
}

bool AMDGPUTargetLowering::shouldReduceLoadWidth(SDNode *N,
                                                 ISD::LoadExtType ExtTy,
                                                 EVT NewVT) const {
  // TODO: This may be worth removing. Check regression tests for diffs.
  if (!TargetLoweringBase::shouldReduceLoadWidth(N, ExtTy, NewVT))
    return false;

  unsigned NewSize = NewVT.getStoreSizeInBits();

  // If we are reducing to a 32-bit load, this is always better.
  if (NewSize == 32)
    return true;

  EVT OldVT = N->getValueType(0);
  unsigned OldSize = OldVT.getStoreSizeInBits();

  MemSDNode *MN = cast<MemSDNode>(N);
  unsigned AS = MN->getAddressSpace();
  // Do not shrink an aligned scalar load to sub-dword.
  // Scalar engine cannot do sub-dword loads.
  if (OldSize >= 32 && NewSize < 32 && MN->getAlignment() >= 4 &&
      (AS == AMDGPUAS::CONSTANT_ADDRESS ||
       AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
       (isa<LoadSDNode>(N) &&
        AS == AMDGPUAS::GLOBAL_ADDRESS && MN->isInvariant())) &&
      AMDGPUInstrInfo::isUniformMMO(MN->getMemOperand()))
    return false;

  // Don't produce extloads from sub 32-bit types. SI doesn't have scalar
  // extloads, so doing one requires using a buffer_load. In cases where we
  // still couldn't use a scalar load, using the wider load shouldn't really
  // hurt anything.

  // If the old size already had to be an extload, there's no harm in continuing
  // to reduce the width.
  return (OldSize < 32);
}

bool AMDGPUTargetLowering::isLoadBitCastBeneficial(EVT LoadTy,
                                                   EVT CastTy) const {

  assert(LoadTy.getSizeInBits() == CastTy.getSizeInBits());

  if (LoadTy.getScalarType() == MVT::i32)
    return false;

  unsigned LScalarSize = LoadTy.getScalarSizeInBits();
  unsigned CastScalarSize = CastTy.getScalarSizeInBits();

  return (LScalarSize < CastScalarSize) ||
         (CastScalarSize >= 32);
}

// SI+ has instructions for cttz / ctlz for 32-bit values. This is probably also
// profitable with the expansion for 64-bit since it's generally good to
// speculate things.
// FIXME: These should really have the size as a parameter.
bool AMDGPUTargetLowering::isCheapToSpeculateCttz() const {
  return true;
}

bool AMDGPUTargetLowering::isCheapToSpeculateCtlz() const {
  return true;
}

bool AMDGPUTargetLowering::isSDNodeAlwaysUniform(const SDNode * N) const {
  switch (N->getOpcode()) {
    default:
    return false;
    case ISD::EntryToken:
    case ISD::TokenFactor:
      return true;
    case ISD::INTRINSIC_WO_CHAIN:
    {
      unsigned IntrID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
      switch (IntrID) {
        default:
        return false;
        case Intrinsic::amdgcn_readfirstlane:
        case Intrinsic::amdgcn_readlane:
          return true;
      }
    }
    break;
    case ISD::LOAD:
    {
      const LoadSDNode * L = dyn_cast<LoadSDNode>(N);
      if (L->getMemOperand()->getAddrSpace()
      == AMDGPUAS::CONSTANT_ADDRESS_32BIT)
        return true;
      return false;
    }
    break;
  }
}

//===---------------------------------------------------------------------===//
// Target Properties
//===---------------------------------------------------------------------===//

bool AMDGPUTargetLowering::isFAbsFree(EVT VT) const {
  assert(VT.isFloatingPoint());

  // Packed operations do not have a fabs modifier.
  return VT == MVT::f32 || VT == MVT::f64 ||
         (Subtarget->has16BitInsts() && VT == MVT::f16);
}

bool AMDGPUTargetLowering::isFNegFree(EVT VT) const {
  assert(VT.isFloatingPoint());
  return VT == MVT::f32 || VT == MVT::f64 ||
         (Subtarget->has16BitInsts() && VT == MVT::f16) ||
         (Subtarget->hasVOP3PInsts() && VT == MVT::v2f16);
}

bool AMDGPUTargetLowering:: storeOfVectorConstantIsCheap(EVT MemVT,
                                                         unsigned NumElem,
                                                         unsigned AS) const {
  return true;
}

bool AMDGPUTargetLowering::aggressivelyPreferBuildVectorSources(EVT VecVT) const {
  // There are few operations which truly have vector input operands. Any vector
  // operation is going to involve operations on each component, and a
  // build_vector will be a copy per element, so it always makes sense to use a
  // build_vector input in place of the extracted element to avoid a copy into a
  // super register.
  //
  // We should probably only do this if all users are extracts only, but this
  // should be the common case.
  return true;
}

bool AMDGPUTargetLowering::isTruncateFree(EVT Source, EVT Dest) const {
  // Truncate is just accessing a subregister.

  unsigned SrcSize = Source.getSizeInBits();
  unsigned DestSize = Dest.getSizeInBits();

  return DestSize < SrcSize && DestSize % 32 == 0 ;
}

bool AMDGPUTargetLowering::isTruncateFree(Type *Source, Type *Dest) const {
  // Truncate is just accessing a subregister.

  unsigned SrcSize = Source->getScalarSizeInBits();
  unsigned DestSize = Dest->getScalarSizeInBits();

  if (DestSize== 16 && Subtarget->has16BitInsts())
    return SrcSize >= 32;

  return DestSize < SrcSize && DestSize % 32 == 0;
}

bool AMDGPUTargetLowering::isZExtFree(Type *Src, Type *Dest) const {
  unsigned SrcSize = Src->getScalarSizeInBits();
  unsigned DestSize = Dest->getScalarSizeInBits();

  if (SrcSize == 16 && Subtarget->has16BitInsts())
    return DestSize >= 32;

  return SrcSize == 32 && DestSize == 64;
}

bool AMDGPUTargetLowering::isZExtFree(EVT Src, EVT Dest) const {
  // Any register load of a 64-bit value really requires 2 32-bit moves. For all
  // practical purposes, the extra mov 0 to load a 64-bit is free.  As used,
  // this will enable reducing 64-bit operations the 32-bit, which is always
  // good.

  if (Src == MVT::i16)
    return Dest == MVT::i32 ||Dest == MVT::i64 ;

  return Src == MVT::i32 && Dest == MVT::i64;
}

bool AMDGPUTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  return isZExtFree(Val.getValueType(), VT2);
}

bool AMDGPUTargetLowering::isNarrowingProfitable(EVT SrcVT, EVT DestVT) const {
  // There aren't really 64-bit registers, but pairs of 32-bit ones and only a
  // limited number of native 64-bit operations. Shrinking an operation to fit
  // in a single 32-bit register should always be helpful. As currently used,
  // this is much less general than the name suggests, and is only used in
  // places trying to reduce the sizes of loads. Shrinking loads to < 32-bits is
  // not profitable, and may actually be harmful.
  return SrcVT.getSizeInBits() > 32 && DestVT.getSizeInBits() == 32;
}

//===---------------------------------------------------------------------===//
// TargetLowering Callbacks
//===---------------------------------------------------------------------===//

CCAssignFn *AMDGPUCallLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                  bool IsVarArg) {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    llvm_unreachable("kernels should not be handled here");
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_LS:
    return CC_AMDGPU;
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return CC_AMDGPU_Func;
  default:
    report_fatal_error("Unsupported calling convention.");
  }
}

CCAssignFn *AMDGPUCallLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                    bool IsVarArg) {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    llvm_unreachable("kernels should not be handled here");
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_LS:
    return RetCC_SI_Shader;
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return RetCC_AMDGPU_Func;
  default:
    report_fatal_error("Unsupported calling convention.");
  }
}

/// The SelectionDAGBuilder will automatically promote function arguments
/// with illegal types.  However, this does not work for the AMDGPU targets
/// since the function arguments are stored in memory as these illegal types.
/// In order to handle this properly we need to get the original types sizes
/// from the LLVM IR Function and fixup the ISD:InputArg values before
/// passing them to AnalyzeFormalArguments()

/// When the SelectionDAGBuilder computes the Ins, it takes care of splitting
/// input values across multiple registers.  Each item in the Ins array
/// represents a single value that will be stored in registers.  Ins[x].VT is
/// the value type of the value that will be stored in the register, so
/// whatever SDNode we lower the argument to needs to be this type.
///
/// In order to correctly lower the arguments we need to know the size of each
/// argument.  Since Ins[x].VT gives us the size of the register that will
/// hold the value, we need to look at Ins[x].ArgVT to see the 'real' type
/// for the orignal function argument so that we can deduce the correct memory
/// type to use for Ins[x].  In most cases the correct memory type will be
/// Ins[x].ArgVT.  However, this will not always be the case.  If, for example,
/// we have a kernel argument of type v8i8, this argument will be split into
/// 8 parts and each part will be represented by its own item in the Ins array.
/// For each part the Ins[x].ArgVT will be the v8i8, which is the full type of
/// the argument before it was split.  From this, we deduce that the memory type
/// for each individual part is i8.  We pass the memory type as LocVT to the
/// calling convention analysis function and the register type (Ins[x].VT) as
/// the ValVT.
void AMDGPUTargetLowering::analyzeFormalArgumentsCompute(
  CCState &State,
  const SmallVectorImpl<ISD::InputArg> &Ins) const {
  const MachineFunction &MF = State.getMachineFunction();
  const Function &Fn = MF.getFunction();
  LLVMContext &Ctx = Fn.getParent()->getContext();
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(MF);
  const unsigned ExplicitOffset = ST.getExplicitKernelArgOffset(Fn);
  CallingConv::ID CC = Fn.getCallingConv();

  unsigned MaxAlign = 1;
  uint64_t ExplicitArgOffset = 0;
  const DataLayout &DL = Fn.getParent()->getDataLayout();

  unsigned InIndex = 0;

  for (const Argument &Arg : Fn.args()) {
    Type *BaseArgTy = Arg.getType();
    unsigned Align = DL.getABITypeAlignment(BaseArgTy);
    MaxAlign = std::max(Align, MaxAlign);
    unsigned AllocSize = DL.getTypeAllocSize(BaseArgTy);

    uint64_t ArgOffset = alignTo(ExplicitArgOffset, Align) + ExplicitOffset;
    ExplicitArgOffset = alignTo(ExplicitArgOffset, Align) + AllocSize;

    // We're basically throwing away everything passed into us and starting over
    // to get accurate in-memory offsets. The "PartOffset" is completely useless
    // to us as computed in Ins.
    //
    // We also need to figure out what type legalization is trying to do to get
    // the correct memory offsets.

    SmallVector<EVT, 16> ValueVTs;
    SmallVector<uint64_t, 16> Offsets;
    ComputeValueVTs(*this, DL, BaseArgTy, ValueVTs, &Offsets, ArgOffset);

    for (unsigned Value = 0, NumValues = ValueVTs.size();
         Value != NumValues; ++Value) {
      uint64_t BasePartOffset = Offsets[Value];

      EVT ArgVT = ValueVTs[Value];
      EVT MemVT = ArgVT;
      MVT RegisterVT = getRegisterTypeForCallingConv(Ctx, CC, ArgVT);
      unsigned NumRegs = getNumRegistersForCallingConv(Ctx, CC, ArgVT);

      if (NumRegs == 1) {
        // This argument is not split, so the IR type is the memory type.
        if (ArgVT.isExtended()) {
          // We have an extended type, like i24, so we should just use the
          // register type.
          MemVT = RegisterVT;
        } else {
          MemVT = ArgVT;
        }
      } else if (ArgVT.isVector() && RegisterVT.isVector() &&
                 ArgVT.getScalarType() == RegisterVT.getScalarType()) {
        assert(ArgVT.getVectorNumElements() > RegisterVT.getVectorNumElements());
        // We have a vector value which has been split into a vector with
        // the same scalar type, but fewer elements.  This should handle
        // all the floating-point vector types.
        MemVT = RegisterVT;
      } else if (ArgVT.isVector() &&
                 ArgVT.getVectorNumElements() == NumRegs) {
        // This arg has been split so that each element is stored in a separate
        // register.
        MemVT = ArgVT.getScalarType();
      } else if (ArgVT.isExtended()) {
        // We have an extended type, like i65.
        MemVT = RegisterVT;
      } else {
        unsigned MemoryBits = ArgVT.getStoreSizeInBits() / NumRegs;
        assert(ArgVT.getStoreSizeInBits() % NumRegs == 0);
        if (RegisterVT.isInteger()) {
          MemVT = EVT::getIntegerVT(State.getContext(), MemoryBits);
        } else if (RegisterVT.isVector()) {
          assert(!RegisterVT.getScalarType().isFloatingPoint());
          unsigned NumElements = RegisterVT.getVectorNumElements();
          assert(MemoryBits % NumElements == 0);
          // This vector type has been split into another vector type with
          // a different elements size.
          EVT ScalarVT = EVT::getIntegerVT(State.getContext(),
                                           MemoryBits / NumElements);
          MemVT = EVT::getVectorVT(State.getContext(), ScalarVT, NumElements);
        } else {
          llvm_unreachable("cannot deduce memory type.");
        }
      }

      // Convert one element vectors to scalar.
      if (MemVT.isVector() && MemVT.getVectorNumElements() == 1)
        MemVT = MemVT.getScalarType();

      if (MemVT.isExtended()) {
        // This should really only happen if we have vec3 arguments
        assert(MemVT.isVector() && MemVT.getVectorNumElements() == 3);
        MemVT = MemVT.getPow2VectorType(State.getContext());
      }

      unsigned PartOffset = 0;
      for (unsigned i = 0; i != NumRegs; ++i) {
        State.addLoc(CCValAssign::getCustomMem(InIndex++, RegisterVT,
                                               BasePartOffset + PartOffset,
                                               MemVT.getSimpleVT(),
                                               CCValAssign::Full));
        PartOffset += MemVT.getStoreSize();
      }
    }
  }
}

SDValue AMDGPUTargetLowering::LowerReturn(
  SDValue Chain, CallingConv::ID CallConv,
  bool isVarArg,
  const SmallVectorImpl<ISD::OutputArg> &Outs,
  const SmallVectorImpl<SDValue> &OutVals,
  const SDLoc &DL, SelectionDAG &DAG) const {
  // FIXME: Fails for r600 tests
  //assert(!isVarArg && Outs.empty() && OutVals.empty() &&
  // "wave terminate should not have return values");
  return DAG.getNode(AMDGPUISD::ENDPGM, DL, MVT::Other, Chain);
}

//===---------------------------------------------------------------------===//
// Target specific lowering
//===---------------------------------------------------------------------===//

/// Selects the correct CCAssignFn for a given CallingConvention value.
CCAssignFn *AMDGPUTargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                    bool IsVarArg) {
  return AMDGPUCallLowering::CCAssignFnForCall(CC, IsVarArg);
}

CCAssignFn *AMDGPUTargetLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                      bool IsVarArg) {
  return AMDGPUCallLowering::CCAssignFnForReturn(CC, IsVarArg);
}

SDValue AMDGPUTargetLowering::addTokenForArgument(SDValue Chain,
                                                  SelectionDAG &DAG,
                                                  MachineFrameInfo &MFI,
                                                  int ClobberedFI) const {
  SmallVector<SDValue, 8> ArgChains;
  int64_t FirstByte = MFI.getObjectOffset(ClobberedFI);
  int64_t LastByte = FirstByte + MFI.getObjectSize(ClobberedFI) - 1;

  // Include the original chain at the beginning of the list. When this is
  // used by target LowerCall hooks, this helps legalize find the
  // CALLSEQ_BEGIN node.
  ArgChains.push_back(Chain);

  // Add a chain value for each stack argument corresponding
  for (SDNode::use_iterator U = DAG.getEntryNode().getNode()->use_begin(),
                            UE = DAG.getEntryNode().getNode()->use_end();
       U != UE; ++U) {
    if (LoadSDNode *L = dyn_cast<LoadSDNode>(*U)) {
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(L->getBasePtr())) {
        if (FI->getIndex() < 0) {
          int64_t InFirstByte = MFI.getObjectOffset(FI->getIndex());
          int64_t InLastByte = InFirstByte;
          InLastByte += MFI.getObjectSize(FI->getIndex()) - 1;

          if ((InFirstByte <= FirstByte && FirstByte <= InLastByte) ||
              (FirstByte <= InFirstByte && InFirstByte <= LastByte))
            ArgChains.push_back(SDValue(L, 1));
        }
      }
    }
  }

  // Build a tokenfactor for all the chains.
  return DAG.getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ArgChains);
}

SDValue AMDGPUTargetLowering::lowerUnhandledCall(CallLoweringInfo &CLI,
                                                 SmallVectorImpl<SDValue> &InVals,
                                                 StringRef Reason) const {
  SDValue Callee = CLI.Callee;
  SelectionDAG &DAG = CLI.DAG;

  const Function &Fn = DAG.getMachineFunction().getFunction();

  StringRef FuncName("<unknown>");

  if (const ExternalSymbolSDNode *G = dyn_cast<ExternalSymbolSDNode>(Callee))
    FuncName = G->getSymbol();
  else if (const GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    FuncName = G->getGlobal()->getName();

  DiagnosticInfoUnsupported NoCalls(
    Fn, Reason + FuncName, CLI.DL.getDebugLoc());
  DAG.getContext()->diagnose(NoCalls);

  if (!CLI.IsTailCall) {
    for (unsigned I = 0, E = CLI.Ins.size(); I != E; ++I)
      InVals.push_back(DAG.getUNDEF(CLI.Ins[I].VT));
  }

  return DAG.getEntryNode();
}

SDValue AMDGPUTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  return lowerUnhandledCall(CLI, InVals, "unsupported call to function ");
}

SDValue AMDGPUTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                      SelectionDAG &DAG) const {
  const Function &Fn = DAG.getMachineFunction().getFunction();

  DiagnosticInfoUnsupported NoDynamicAlloca(Fn, "unsupported dynamic alloca",
                                            SDLoc(Op).getDebugLoc());
  DAG.getContext()->diagnose(NoDynamicAlloca);
  auto Ops = {DAG.getConstant(0, SDLoc(), Op.getValueType()), Op.getOperand(0)};
  return DAG.getMergeValues(Ops, SDLoc());
}

SDValue AMDGPUTargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    Op->print(errs(), &DAG);
    llvm_unreachable("Custom lowering code for this"
                     "instruction is not implemented yet!");
    break;
  case ISD::SIGN_EXTEND_INREG: return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::CONCAT_VECTORS: return LowerCONCAT_VECTORS(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR: return LowerEXTRACT_SUBVECTOR(Op, DAG);
  case ISD::UDIVREM: return LowerUDIVREM(Op, DAG);
  case ISD::SDIVREM: return LowerSDIVREM(Op, DAG);
  case ISD::FREM: return LowerFREM(Op, DAG);
  case ISD::FCEIL: return LowerFCEIL(Op, DAG);
  case ISD::FTRUNC: return LowerFTRUNC(Op, DAG);
  case ISD::FRINT: return LowerFRINT(Op, DAG);
  case ISD::FNEARBYINT: return LowerFNEARBYINT(Op, DAG);
  case ISD::FROUND: return LowerFROUND(Op, DAG);
  case ISD::FFLOOR: return LowerFFLOOR(Op, DAG);
  case ISD::FLOG:
    return LowerFLOG(Op, DAG, 1 / AMDGPU_LOG2E_F);
  case ISD::FLOG10:
    return LowerFLOG(Op, DAG, AMDGPU_LN2_F / AMDGPU_LN10_F);
  case ISD::FEXP:
    return lowerFEXP(Op, DAG);
  case ISD::SINT_TO_FP: return LowerSINT_TO_FP(Op, DAG);
  case ISD::UINT_TO_FP: return LowerUINT_TO_FP(Op, DAG);
  case ISD::FP_TO_FP16: return LowerFP_TO_FP16(Op, DAG);
  case ISD::FP_TO_SINT: return LowerFP_TO_SINT(Op, DAG);
  case ISD::FP_TO_UINT: return LowerFP_TO_UINT(Op, DAG);
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    return LowerCTLZ_CTTZ(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: return LowerDYNAMIC_STACKALLOC(Op, DAG);
  }
  return Op;
}

void AMDGPUTargetLowering::ReplaceNodeResults(SDNode *N,
                                              SmallVectorImpl<SDValue> &Results,
                                              SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  case ISD::SIGN_EXTEND_INREG:
    // Different parts of legalization seem to interpret which type of
    // sign_extend_inreg is the one to check for custom lowering. The extended
    // from type is what really matters, but some places check for custom
    // lowering of the result type. This results in trying to use
    // ReplaceNodeResults to sext_in_reg to an illegal type, so we'll just do
    // nothing here and let the illegal result integer be handled normally.
    return;
  default:
    return;
  }
}

static bool hasDefinedInitializer(const GlobalValue *GV) {
  const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV);
  if (!GVar || !GVar->hasInitializer())
    return false;

  return !isa<UndefValue>(GVar->getInitializer());
}

SDValue AMDGPUTargetLowering::LowerGlobalAddress(AMDGPUMachineFunction* MFI,
                                                 SDValue Op,
                                                 SelectionDAG &DAG) const {

  const DataLayout &DL = DAG.getDataLayout();
  GlobalAddressSDNode *G = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = G->getGlobal();

  if (G->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS ||
      G->getAddressSpace() == AMDGPUAS::REGION_ADDRESS) {
    if (!MFI->isEntryFunction()) {
      const Function &Fn = DAG.getMachineFunction().getFunction();
      DiagnosticInfoUnsupported BadLDSDecl(
        Fn, "local memory global used by non-kernel function", SDLoc(Op).getDebugLoc());
      DAG.getContext()->diagnose(BadLDSDecl);
    }

    // XXX: What does the value of G->getOffset() mean?
    assert(G->getOffset() == 0 &&
         "Do not know what to do with an non-zero offset");

    // TODO: We could emit code to handle the initialization somewhere.
    if (!hasDefinedInitializer(GV)) {
      unsigned Offset = MFI->allocateLDSGlobal(DL, *GV);
      return DAG.getConstant(Offset, SDLoc(Op), Op.getValueType());
    }
  }

  const Function &Fn = DAG.getMachineFunction().getFunction();
  DiagnosticInfoUnsupported BadInit(
      Fn, "unsupported initializer for address space", SDLoc(Op).getDebugLoc());
  DAG.getContext()->diagnose(BadInit);
  return SDValue();
}

SDValue AMDGPUTargetLowering::LowerCONCAT_VECTORS(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SmallVector<SDValue, 8> Args;

  EVT VT = Op.getValueType();
  if (VT == MVT::v4i16 || VT == MVT::v4f16) {
    SDLoc SL(Op);
    SDValue Lo = DAG.getNode(ISD::BITCAST, SL, MVT::i32, Op.getOperand(0));
    SDValue Hi = DAG.getNode(ISD::BITCAST, SL, MVT::i32, Op.getOperand(1));

    SDValue BV = DAG.getBuildVector(MVT::v2i32, SL, { Lo, Hi });
    return DAG.getNode(ISD::BITCAST, SL, VT, BV);
  }

  for (const SDUse &U : Op->ops())
    DAG.ExtractVectorElements(U.get(), Args);

  return DAG.getBuildVector(Op.getValueType(), SDLoc(Op), Args);
}

SDValue AMDGPUTargetLowering::LowerEXTRACT_SUBVECTOR(SDValue Op,
                                                     SelectionDAG &DAG) const {

  SmallVector<SDValue, 8> Args;
  unsigned Start = cast<ConstantSDNode>(Op.getOperand(1))->getZExtValue();
  EVT VT = Op.getValueType();
  DAG.ExtractVectorElements(Op.getOperand(0), Args, Start,
                            VT.getVectorNumElements());

  return DAG.getBuildVector(Op.getValueType(), SDLoc(Op), Args);
}

/// Generate Min/Max node
SDValue AMDGPUTargetLowering::combineFMinMaxLegacy(const SDLoc &DL, EVT VT,
                                                   SDValue LHS, SDValue RHS,
                                                   SDValue True, SDValue False,
                                                   SDValue CC,
                                                   DAGCombinerInfo &DCI) const {
  if (!(LHS == True && RHS == False) && !(LHS == False && RHS == True))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  ISD::CondCode CCOpcode = cast<CondCodeSDNode>(CC)->get();
  switch (CCOpcode) {
  case ISD::SETOEQ:
  case ISD::SETONE:
  case ISD::SETUNE:
  case ISD::SETNE:
  case ISD::SETUEQ:
  case ISD::SETEQ:
  case ISD::SETFALSE:
  case ISD::SETFALSE2:
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
  case ISD::SETUO:
  case ISD::SETO:
    break;
  case ISD::SETULE:
  case ISD::SETULT: {
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, RHS, LHS);
    return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, LHS, RHS);
  }
  case ISD::SETOLE:
  case ISD::SETOLT:
  case ISD::SETLE:
  case ISD::SETLT: {
    // Ordered. Assume ordered for undefined.

    // Only do this after legalization to avoid interfering with other combines
    // which might occur.
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG &&
        !DCI.isCalledByLegalizer())
      return SDValue();

    // We need to permute the operands to get the correct NaN behavior. The
    // selected operand is the second one based on the failing compare with NaN,
    // so permute it based on the compare type the hardware uses.
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, LHS, RHS);
    return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, RHS, LHS);
  }
  case ISD::SETUGE:
  case ISD::SETUGT: {
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, RHS, LHS);
    return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, LHS, RHS);
  }
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETOGE:
  case ISD::SETOGT: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG &&
        !DCI.isCalledByLegalizer())
      return SDValue();

    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, LHS, RHS);
    return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, RHS, LHS);
  }
  case ISD::SETCC_INVALID:
    llvm_unreachable("Invalid setcc condcode!");
  }
  return SDValue();
}

std::pair<SDValue, SDValue>
AMDGPUTargetLowering::split64BitValue(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);

  SDValue Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, Zero);
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, One);

  return std::make_pair(Lo, Hi);
}

SDValue AMDGPUTargetLowering::getLoHalf64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);
  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, Zero);
}

SDValue AMDGPUTargetLowering::getHiHalf64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, One);
}

SDValue AMDGPUTargetLowering::SplitVectorLoad(const SDValue Op,
                                              SelectionDAG &DAG) const {
  LoadSDNode *Load = cast<LoadSDNode>(Op);
  EVT VT = Op.getValueType();


  // If this is a 2 element vector, we really want to scalarize and not create
  // weird 1 element vectors.
  if (VT.getVectorNumElements() == 2)
    return scalarizeVectorLoad(Load, DAG);

  SDValue BasePtr = Load->getBasePtr();
  EVT MemVT = Load->getMemoryVT();
  SDLoc SL(Op);

  const MachinePointerInfo &SrcValue = Load->getMemOperand()->getPointerInfo();

  EVT LoVT, HiVT;
  EVT LoMemVT, HiMemVT;
  SDValue Lo, Hi;

  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VT);
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemVT);
  std::tie(Lo, Hi) = DAG.SplitVector(Op, SL, LoVT, HiVT);

  unsigned Size = LoMemVT.getStoreSize();
  unsigned BaseAlign = Load->getAlignment();
  unsigned HiAlign = MinAlign(BaseAlign, Size);

  SDValue LoLoad = DAG.getExtLoad(Load->getExtensionType(), SL, LoVT,
                                  Load->getChain(), BasePtr, SrcValue, LoMemVT,
                                  BaseAlign, Load->getMemOperand()->getFlags());
  SDValue HiPtr = DAG.getObjectPtrOffset(SL, BasePtr, Size);
  SDValue HiLoad =
      DAG.getExtLoad(Load->getExtensionType(), SL, HiVT, Load->getChain(),
                     HiPtr, SrcValue.getWithOffset(LoMemVT.getStoreSize()),
                     HiMemVT, HiAlign, Load->getMemOperand()->getFlags());

  SDValue Ops[] = {
    DAG.getNode(ISD::CONCAT_VECTORS, SL, VT, LoLoad, HiLoad),
    DAG.getNode(ISD::TokenFactor, SL, MVT::Other,
                LoLoad.getValue(1), HiLoad.getValue(1))
  };

  return DAG.getMergeValues(Ops, SL);
}

SDValue AMDGPUTargetLowering::SplitVectorStore(SDValue Op,
                                               SelectionDAG &DAG) const {
  StoreSDNode *Store = cast<StoreSDNode>(Op);
  SDValue Val = Store->getValue();
  EVT VT = Val.getValueType();

  // If this is a 2 element vector, we really want to scalarize and not create
  // weird 1 element vectors.
  if (VT.getVectorNumElements() == 2)
    return scalarizeVectorStore(Store, DAG);

  EVT MemVT = Store->getMemoryVT();
  SDValue Chain = Store->getChain();
  SDValue BasePtr = Store->getBasePtr();
  SDLoc SL(Op);

  EVT LoVT, HiVT;
  EVT LoMemVT, HiMemVT;
  SDValue Lo, Hi;

  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VT);
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemVT);
  std::tie(Lo, Hi) = DAG.SplitVector(Val, SL, LoVT, HiVT);

  SDValue HiPtr = DAG.getObjectPtrOffset(SL, BasePtr, LoMemVT.getStoreSize());

  const MachinePointerInfo &SrcValue = Store->getMemOperand()->getPointerInfo();
  unsigned BaseAlign = Store->getAlignment();
  unsigned Size = LoMemVT.getStoreSize();
  unsigned HiAlign = MinAlign(BaseAlign, Size);

  SDValue LoStore =
      DAG.getTruncStore(Chain, SL, Lo, BasePtr, SrcValue, LoMemVT, BaseAlign,
                        Store->getMemOperand()->getFlags());
  SDValue HiStore =
      DAG.getTruncStore(Chain, SL, Hi, HiPtr, SrcValue.getWithOffset(Size),
                        HiMemVT, HiAlign, Store->getMemOperand()->getFlags());

  return DAG.getNode(ISD::TokenFactor, SL, MVT::Other, LoStore, HiStore);
}

// This is a shortcut for integer division because we have fast i32<->f32
// conversions, and fast f32 reciprocal instructions. The fractional part of a
// float is enough to accurately represent up to a 24-bit signed integer.
SDValue AMDGPUTargetLowering::LowerDIVREM24(SDValue Op, SelectionDAG &DAG,
                                            bool Sign) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  MVT IntVT = MVT::i32;
  MVT FltVT = MVT::f32;

  unsigned LHSSignBits = DAG.ComputeNumSignBits(LHS);
  if (LHSSignBits < 9)
    return SDValue();

  unsigned RHSSignBits = DAG.ComputeNumSignBits(RHS);
  if (RHSSignBits < 9)
    return SDValue();

  unsigned BitSize = VT.getSizeInBits();
  unsigned SignBits = std::min(LHSSignBits, RHSSignBits);
  unsigned DivBits = BitSize - SignBits;
  if (Sign)
    ++DivBits;

  ISD::NodeType ToFp = Sign ? ISD::SINT_TO_FP : ISD::UINT_TO_FP;
  ISD::NodeType ToInt = Sign ? ISD::FP_TO_SINT : ISD::FP_TO_UINT;

  SDValue jq = DAG.getConstant(1, DL, IntVT);

  if (Sign) {
    // char|short jq = ia ^ ib;
    jq = DAG.getNode(ISD::XOR, DL, VT, LHS, RHS);

    // jq = jq >> (bitsize - 2)
    jq = DAG.getNode(ISD::SRA, DL, VT, jq,
                     DAG.getConstant(BitSize - 2, DL, VT));

    // jq = jq | 0x1
    jq = DAG.getNode(ISD::OR, DL, VT, jq, DAG.getConstant(1, DL, VT));
  }

  // int ia = (int)LHS;
  SDValue ia = LHS;

  // int ib, (int)RHS;
  SDValue ib = RHS;

  // float fa = (float)ia;
  SDValue fa = DAG.getNode(ToFp, DL, FltVT, ia);

  // float fb = (float)ib;
  SDValue fb = DAG.getNode(ToFp, DL, FltVT, ib);

  SDValue fq = DAG.getNode(ISD::FMUL, DL, FltVT,
                           fa, DAG.getNode(AMDGPUISD::RCP, DL, FltVT, fb));

  // fq = trunc(fq);
  fq = DAG.getNode(ISD::FTRUNC, DL, FltVT, fq);

  // float fqneg = -fq;
  SDValue fqneg = DAG.getNode(ISD::FNEG, DL, FltVT, fq);

  // float fr = mad(fqneg, fb, fa);
  unsigned OpCode = Subtarget->hasFP32Denormals() ?
                    (unsigned)AMDGPUISD::FMAD_FTZ :
                    (unsigned)ISD::FMAD;
  SDValue fr = DAG.getNode(OpCode, DL, FltVT, fqneg, fb, fa);

  // int iq = (int)fq;
  SDValue iq = DAG.getNode(ToInt, DL, IntVT, fq);

  // fr = fabs(fr);
  fr = DAG.getNode(ISD::FABS, DL, FltVT, fr);

  // fb = fabs(fb);
  fb = DAG.getNode(ISD::FABS, DL, FltVT, fb);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  // int cv = fr >= fb;
  SDValue cv = DAG.getSetCC(DL, SetCCVT, fr, fb, ISD::SETOGE);

  // jq = (cv ? jq : 0);
  jq = DAG.getNode(ISD::SELECT, DL, VT, cv, jq, DAG.getConstant(0, DL, VT));

  // dst = iq + jq;
  SDValue Div = DAG.getNode(ISD::ADD, DL, VT, iq, jq);

  // Rem needs compensation, it's easier to recompute it
  SDValue Rem = DAG.getNode(ISD::MUL, DL, VT, Div, RHS);
  Rem = DAG.getNode(ISD::SUB, DL, VT, LHS, Rem);

  // Truncate to number of bits this divide really is.
  if (Sign) {
    SDValue InRegSize
      = DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(), DivBits));
    Div = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Div, InRegSize);
    Rem = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Rem, InRegSize);
  } else {
    SDValue TruncMask = DAG.getConstant((UINT64_C(1) << DivBits) - 1, DL, VT);
    Div = DAG.getNode(ISD::AND, DL, VT, Div, TruncMask);
    Rem = DAG.getNode(ISD::AND, DL, VT, Rem, TruncMask);
  }

  return DAG.getMergeValues({ Div, Rem }, DL);
}

void AMDGPUTargetLowering::LowerUDIVREM64(SDValue Op,
                                      SelectionDAG &DAG,
                                      SmallVectorImpl<SDValue> &Results) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  assert(VT == MVT::i64 && "LowerUDIVREM64 expects an i64");

  EVT HalfVT = VT.getHalfSizedIntegerVT(*DAG.getContext());

  SDValue One = DAG.getConstant(1, DL, HalfVT);
  SDValue Zero = DAG.getConstant(0, DL, HalfVT);

  //HiLo split
  SDValue LHS = Op.getOperand(0);
  SDValue LHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, LHS, Zero);
  SDValue LHS_Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, LHS, One);

  SDValue RHS = Op.getOperand(1);
  SDValue RHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, RHS, Zero);
  SDValue RHS_Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, RHS, One);

  if (DAG.MaskedValueIsZero(RHS, APInt::getHighBitsSet(64, 32)) &&
      DAG.MaskedValueIsZero(LHS, APInt::getHighBitsSet(64, 32))) {

    SDValue Res = DAG.getNode(ISD::UDIVREM, DL, DAG.getVTList(HalfVT, HalfVT),
                              LHS_Lo, RHS_Lo);

    SDValue DIV = DAG.getBuildVector(MVT::v2i32, DL, {Res.getValue(0), Zero});
    SDValue REM = DAG.getBuildVector(MVT::v2i32, DL, {Res.getValue(1), Zero});

    Results.push_back(DAG.getNode(ISD::BITCAST, DL, MVT::i64, DIV));
    Results.push_back(DAG.getNode(ISD::BITCAST, DL, MVT::i64, REM));
    return;
  }

  if (isTypeLegal(MVT::i64)) {
    // Compute denominator reciprocal.
    unsigned FMAD = Subtarget->hasFP32Denormals() ?
                    (unsigned)AMDGPUISD::FMAD_FTZ :
                    (unsigned)ISD::FMAD;

    SDValue Cvt_Lo = DAG.getNode(ISD::UINT_TO_FP, DL, MVT::f32, RHS_Lo);
    SDValue Cvt_Hi = DAG.getNode(ISD::UINT_TO_FP, DL, MVT::f32, RHS_Hi);
    SDValue Mad1 = DAG.getNode(FMAD, DL, MVT::f32, Cvt_Hi,
      DAG.getConstantFP(APInt(32, 0x4f800000).bitsToFloat(), DL, MVT::f32),
      Cvt_Lo);
    SDValue Rcp = DAG.getNode(AMDGPUISD::RCP, DL, MVT::f32, Mad1);
    SDValue Mul1 = DAG.getNode(ISD::FMUL, DL, MVT::f32, Rcp,
      DAG.getConstantFP(APInt(32, 0x5f7ffffc).bitsToFloat(), DL, MVT::f32));
    SDValue Mul2 = DAG.getNode(ISD::FMUL, DL, MVT::f32, Mul1,
      DAG.getConstantFP(APInt(32, 0x2f800000).bitsToFloat(), DL, MVT::f32));
    SDValue Trunc = DAG.getNode(ISD::FTRUNC, DL, MVT::f32, Mul2);
    SDValue Mad2 = DAG.getNode(FMAD, DL, MVT::f32, Trunc,
      DAG.getConstantFP(APInt(32, 0xcf800000).bitsToFloat(), DL, MVT::f32),
      Mul1);
    SDValue Rcp_Lo = DAG.getNode(ISD::FP_TO_UINT, DL, HalfVT, Mad2);
    SDValue Rcp_Hi = DAG.getNode(ISD::FP_TO_UINT, DL, HalfVT, Trunc);
    SDValue Rcp64 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Rcp_Lo, Rcp_Hi}));

    SDValue Zero64 = DAG.getConstant(0, DL, VT);
    SDValue One64  = DAG.getConstant(1, DL, VT);
    SDValue Zero1 = DAG.getConstant(0, DL, MVT::i1);
    SDVTList HalfCarryVT = DAG.getVTList(HalfVT, MVT::i1);

    SDValue Neg_RHS = DAG.getNode(ISD::SUB, DL, VT, Zero64, RHS);
    SDValue Mullo1 = DAG.getNode(ISD::MUL, DL, VT, Neg_RHS, Rcp64);
    SDValue Mulhi1 = DAG.getNode(ISD::MULHU, DL, VT, Rcp64, Mullo1);
    SDValue Mulhi1_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mulhi1,
                                    Zero);
    SDValue Mulhi1_Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mulhi1,
                                    One);

    SDValue Add1_Lo = DAG.getNode(ISD::ADDCARRY, DL, HalfCarryVT, Rcp_Lo,
                                  Mulhi1_Lo, Zero1);
    SDValue Add1_Hi = DAG.getNode(ISD::ADDCARRY, DL, HalfCarryVT, Rcp_Hi,
                                  Mulhi1_Hi, Add1_Lo.getValue(1));
    SDValue Add1_HiNc = DAG.getNode(ISD::ADD, DL, HalfVT, Rcp_Hi, Mulhi1_Hi);
    SDValue Add1 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Add1_Lo, Add1_Hi}));

    SDValue Mullo2 = DAG.getNode(ISD::MUL, DL, VT, Neg_RHS, Add1);
    SDValue Mulhi2 = DAG.getNode(ISD::MULHU, DL, VT, Add1, Mullo2);
    SDValue Mulhi2_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mulhi2,
                                    Zero);
    SDValue Mulhi2_Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mulhi2,
                                    One);

    SDValue Add2_Lo = DAG.getNode(ISD::ADDCARRY, DL, HalfCarryVT, Add1_Lo,
                                  Mulhi2_Lo, Zero1);
    SDValue Add2_HiC = DAG.getNode(ISD::ADDCARRY, DL, HalfCarryVT, Add1_HiNc,
                                   Mulhi2_Hi, Add1_Lo.getValue(1));
    SDValue Add2_Hi = DAG.getNode(ISD::ADDCARRY, DL, HalfCarryVT, Add2_HiC,
                                  Zero, Add2_Lo.getValue(1));
    SDValue Add2 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Add2_Lo, Add2_Hi}));
    SDValue Mulhi3 = DAG.getNode(ISD::MULHU, DL, VT, LHS, Add2);

    SDValue Mul3 = DAG.getNode(ISD::MUL, DL, VT, RHS, Mulhi3);

    SDValue Mul3_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mul3, Zero);
    SDValue Mul3_Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, Mul3, One);
    SDValue Sub1_Lo = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, LHS_Lo,
                                  Mul3_Lo, Zero1);
    SDValue Sub1_Hi = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, LHS_Hi,
                                  Mul3_Hi, Sub1_Lo.getValue(1));
    SDValue Sub1_Mi = DAG.getNode(ISD::SUB, DL, HalfVT, LHS_Hi, Mul3_Hi);
    SDValue Sub1 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub1_Lo, Sub1_Hi}));

    SDValue MinusOne = DAG.getConstant(0xffffffffu, DL, HalfVT);
    SDValue C1 = DAG.getSelectCC(DL, Sub1_Hi, RHS_Hi, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C2 = DAG.getSelectCC(DL, Sub1_Lo, RHS_Lo, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C3 = DAG.getSelectCC(DL, Sub1_Hi, RHS_Hi, C2, C1, ISD::SETEQ);

    // TODO: Here and below portions of the code can be enclosed into if/endif.
    // Currently control flow is unconditional and we have 4 selects after
    // potential endif to substitute PHIs.

    // if C3 != 0 ...
    SDValue Sub2_Lo = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub1_Lo,
                                  RHS_Lo, Zero1);
    SDValue Sub2_Mi = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub1_Mi,
                                  RHS_Hi, Sub1_Lo.getValue(1));
    SDValue Sub2_Hi = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub2_Mi,
                                  Zero, Sub2_Lo.getValue(1));
    SDValue Sub2 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub2_Lo, Sub2_Hi}));

    SDValue Add3 = DAG.getNode(ISD::ADD, DL, VT, Mulhi3, One64);

    SDValue C4 = DAG.getSelectCC(DL, Sub2_Hi, RHS_Hi, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C5 = DAG.getSelectCC(DL, Sub2_Lo, RHS_Lo, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C6 = DAG.getSelectCC(DL, Sub2_Hi, RHS_Hi, C5, C4, ISD::SETEQ);

    // if (C6 != 0)
    SDValue Add4 = DAG.getNode(ISD::ADD, DL, VT, Add3, One64);

    SDValue Sub3_Lo = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub2_Lo,
                                  RHS_Lo, Zero1);
    SDValue Sub3_Mi = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub2_Mi,
                                  RHS_Hi, Sub2_Lo.getValue(1));
    SDValue Sub3_Hi = DAG.getNode(ISD::SUBCARRY, DL, HalfCarryVT, Sub3_Mi,
                                  Zero, Sub3_Lo.getValue(1));
    SDValue Sub3 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub3_Lo, Sub3_Hi}));

    // endif C6
    // endif C3

    SDValue Sel1 = DAG.getSelectCC(DL, C6, Zero, Add4, Add3, ISD::SETNE);
    SDValue Div  = DAG.getSelectCC(DL, C3, Zero, Sel1, Mulhi3, ISD::SETNE);

    SDValue Sel2 = DAG.getSelectCC(DL, C6, Zero, Sub3, Sub2, ISD::SETNE);
    SDValue Rem  = DAG.getSelectCC(DL, C3, Zero, Sel2, Sub1, ISD::SETNE);

    Results.push_back(Div);
    Results.push_back(Rem);

    return;
  }

  // r600 expandion.
  // Get Speculative values
  SDValue DIV_Part = DAG.getNode(ISD::UDIV, DL, HalfVT, LHS_Hi, RHS_Lo);
  SDValue REM_Part = DAG.getNode(ISD::UREM, DL, HalfVT, LHS_Hi, RHS_Lo);

  SDValue REM_Lo = DAG.getSelectCC(DL, RHS_Hi, Zero, REM_Part, LHS_Hi, ISD::SETEQ);
  SDValue REM = DAG.getBuildVector(MVT::v2i32, DL, {REM_Lo, Zero});
  REM = DAG.getNode(ISD::BITCAST, DL, MVT::i64, REM);

  SDValue DIV_Hi = DAG.getSelectCC(DL, RHS_Hi, Zero, DIV_Part, Zero, ISD::SETEQ);
  SDValue DIV_Lo = Zero;

  const unsigned halfBitWidth = HalfVT.getSizeInBits();

  for (unsigned i = 0; i < halfBitWidth; ++i) {
    const unsigned bitPos = halfBitWidth - i - 1;
    SDValue POS = DAG.getConstant(bitPos, DL, HalfVT);
    // Get value of high bit
    SDValue HBit = DAG.getNode(ISD::SRL, DL, HalfVT, LHS_Lo, POS);
    HBit = DAG.getNode(ISD::AND, DL, HalfVT, HBit, One);
    HBit = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, HBit);

    // Shift
    REM = DAG.getNode(ISD::SHL, DL, VT, REM, DAG.getConstant(1, DL, VT));
    // Add LHS high bit
    REM = DAG.getNode(ISD::OR, DL, VT, REM, HBit);

    SDValue BIT = DAG.getConstant(1ULL << bitPos, DL, HalfVT);
    SDValue realBIT = DAG.getSelectCC(DL, REM, RHS, BIT, Zero, ISD::SETUGE);

    DIV_Lo = DAG.getNode(ISD::OR, DL, HalfVT, DIV_Lo, realBIT);

    // Update REM
    SDValue REM_sub = DAG.getNode(ISD::SUB, DL, VT, REM, RHS);
    REM = DAG.getSelectCC(DL, REM, RHS, REM_sub, REM, ISD::SETUGE);
  }

  SDValue DIV = DAG.getBuildVector(MVT::v2i32, DL, {DIV_Lo, DIV_Hi});
  DIV = DAG.getNode(ISD::BITCAST, DL, MVT::i64, DIV);
  Results.push_back(DIV);
  Results.push_back(REM);
}

SDValue AMDGPUTargetLowering::LowerUDIVREM(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (VT == MVT::i64) {
    SmallVector<SDValue, 2> Results;
    LowerUDIVREM64(Op, DAG, Results);
    return DAG.getMergeValues(Results, DL);
  }

  if (VT == MVT::i32) {
    if (SDValue Res = LowerDIVREM24(Op, DAG, false))
      return Res;
  }

  SDValue Num = Op.getOperand(0);
  SDValue Den = Op.getOperand(1);

  // RCP =  URECIP(Den) = 2^32 / Den + e
  // e is rounding error.
  SDValue RCP = DAG.getNode(AMDGPUISD::URECIP, DL, VT, Den);

  // RCP_LO = mul(RCP, Den) */
  SDValue RCP_LO = DAG.getNode(ISD::MUL, DL, VT, RCP, Den);

  // RCP_HI = mulhu (RCP, Den) */
  SDValue RCP_HI = DAG.getNode(ISD::MULHU, DL, VT, RCP, Den);

  // NEG_RCP_LO = -RCP_LO
  SDValue NEG_RCP_LO = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT),
                                                     RCP_LO);

  // ABS_RCP_LO = (RCP_HI == 0 ? NEG_RCP_LO : RCP_LO)
  SDValue ABS_RCP_LO = DAG.getSelectCC(DL, RCP_HI, DAG.getConstant(0, DL, VT),
                                           NEG_RCP_LO, RCP_LO,
                                           ISD::SETEQ);
  // Calculate the rounding error from the URECIP instruction
  // E = mulhu(ABS_RCP_LO, RCP)
  SDValue E = DAG.getNode(ISD::MULHU, DL, VT, ABS_RCP_LO, RCP);

  // RCP_A_E = RCP + E
  SDValue RCP_A_E = DAG.getNode(ISD::ADD, DL, VT, RCP, E);

  // RCP_S_E = RCP - E
  SDValue RCP_S_E = DAG.getNode(ISD::SUB, DL, VT, RCP, E);

  // Tmp0 = (RCP_HI == 0 ? RCP_A_E : RCP_SUB_E)
  SDValue Tmp0 = DAG.getSelectCC(DL, RCP_HI, DAG.getConstant(0, DL, VT),
                                     RCP_A_E, RCP_S_E,
                                     ISD::SETEQ);
  // Quotient = mulhu(Tmp0, Num)
  SDValue Quotient = DAG.getNode(ISD::MULHU, DL, VT, Tmp0, Num);

  // Num_S_Remainder = Quotient * Den
  SDValue Num_S_Remainder = DAG.getNode(ISD::MUL, DL, VT, Quotient, Den);

  // Remainder = Num - Num_S_Remainder
  SDValue Remainder = DAG.getNode(ISD::SUB, DL, VT, Num, Num_S_Remainder);

  // Remainder_GE_Den = (Remainder >= Den ? -1 : 0)
  SDValue Remainder_GE_Den = DAG.getSelectCC(DL, Remainder, Den,
                                                 DAG.getConstant(-1, DL, VT),
                                                 DAG.getConstant(0, DL, VT),
                                                 ISD::SETUGE);
  // Remainder_GE_Zero = (Num >= Num_S_Remainder ? -1 : 0)
  SDValue Remainder_GE_Zero = DAG.getSelectCC(DL, Num,
                                                  Num_S_Remainder,
                                                  DAG.getConstant(-1, DL, VT),
                                                  DAG.getConstant(0, DL, VT),
                                                  ISD::SETUGE);
  // Tmp1 = Remainder_GE_Den & Remainder_GE_Zero
  SDValue Tmp1 = DAG.getNode(ISD::AND, DL, VT, Remainder_GE_Den,
                                               Remainder_GE_Zero);

  // Calculate Division result:

  // Quotient_A_One = Quotient + 1
  SDValue Quotient_A_One = DAG.getNode(ISD::ADD, DL, VT, Quotient,
                                       DAG.getConstant(1, DL, VT));

  // Quotient_S_One = Quotient - 1
  SDValue Quotient_S_One = DAG.getNode(ISD::SUB, DL, VT, Quotient,
                                       DAG.getConstant(1, DL, VT));

  // Div = (Tmp1 == 0 ? Quotient : Quotient_A_One)
  SDValue Div = DAG.getSelectCC(DL, Tmp1, DAG.getConstant(0, DL, VT),
                                     Quotient, Quotient_A_One, ISD::SETEQ);

  // Div = (Remainder_GE_Zero == 0 ? Quotient_S_One : Div)
  Div = DAG.getSelectCC(DL, Remainder_GE_Zero, DAG.getConstant(0, DL, VT),
                            Quotient_S_One, Div, ISD::SETEQ);

  // Calculate Rem result:

  // Remainder_S_Den = Remainder - Den
  SDValue Remainder_S_Den = DAG.getNode(ISD::SUB, DL, VT, Remainder, Den);

  // Remainder_A_Den = Remainder + Den
  SDValue Remainder_A_Den = DAG.getNode(ISD::ADD, DL, VT, Remainder, Den);

  // Rem = (Tmp1 == 0 ? Remainder : Remainder_S_Den)
  SDValue Rem = DAG.getSelectCC(DL, Tmp1, DAG.getConstant(0, DL, VT),
                                    Remainder, Remainder_S_Den, ISD::SETEQ);

  // Rem = (Remainder_GE_Zero == 0 ? Remainder_A_Den : Rem)
  Rem = DAG.getSelectCC(DL, Remainder_GE_Zero, DAG.getConstant(0, DL, VT),
                            Remainder_A_Den, Rem, ISD::SETEQ);
  SDValue Ops[2] = {
    Div,
    Rem
  };
  return DAG.getMergeValues(Ops, DL);
}

SDValue AMDGPUTargetLowering::LowerSDIVREM(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue NegOne = DAG.getConstant(-1, DL, VT);

  if (VT == MVT::i32) {
    if (SDValue Res = LowerDIVREM24(Op, DAG, true))
      return Res;
  }

  if (VT == MVT::i64 &&
      DAG.ComputeNumSignBits(LHS) > 32 &&
      DAG.ComputeNumSignBits(RHS) > 32) {
    EVT HalfVT = VT.getHalfSizedIntegerVT(*DAG.getContext());

    //HiLo split
    SDValue LHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, LHS, Zero);
    SDValue RHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, RHS, Zero);
    SDValue DIVREM = DAG.getNode(ISD::SDIVREM, DL, DAG.getVTList(HalfVT, HalfVT),
                                 LHS_Lo, RHS_Lo);
    SDValue Res[2] = {
      DAG.getNode(ISD::SIGN_EXTEND, DL, VT, DIVREM.getValue(0)),
      DAG.getNode(ISD::SIGN_EXTEND, DL, VT, DIVREM.getValue(1))
    };
    return DAG.getMergeValues(Res, DL);
  }

  SDValue LHSign = DAG.getSelectCC(DL, LHS, Zero, NegOne, Zero, ISD::SETLT);
  SDValue RHSign = DAG.getSelectCC(DL, RHS, Zero, NegOne, Zero, ISD::SETLT);
  SDValue DSign = DAG.getNode(ISD::XOR, DL, VT, LHSign, RHSign);
  SDValue RSign = LHSign; // Remainder sign is the same as LHS

  LHS = DAG.getNode(ISD::ADD, DL, VT, LHS, LHSign);
  RHS = DAG.getNode(ISD::ADD, DL, VT, RHS, RHSign);

  LHS = DAG.getNode(ISD::XOR, DL, VT, LHS, LHSign);
  RHS = DAG.getNode(ISD::XOR, DL, VT, RHS, RHSign);

  SDValue Div = DAG.getNode(ISD::UDIVREM, DL, DAG.getVTList(VT, VT), LHS, RHS);
  SDValue Rem = Div.getValue(1);

  Div = DAG.getNode(ISD::XOR, DL, VT, Div, DSign);
  Rem = DAG.getNode(ISD::XOR, DL, VT, Rem, RSign);

  Div = DAG.getNode(ISD::SUB, DL, VT, Div, DSign);
  Rem = DAG.getNode(ISD::SUB, DL, VT, Rem, RSign);

  SDValue Res[2] = {
    Div,
    Rem
  };
  return DAG.getMergeValues(Res, DL);
}

// (frem x, y) -> (fsub x, (fmul (ftrunc (fdiv x, y)), y))
SDValue AMDGPUTargetLowering::LowerFREM(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  EVT VT = Op.getValueType();
  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);

  // TODO: Should this propagate fast-math-flags?

  SDValue Div = DAG.getNode(ISD::FDIV, SL, VT, X, Y);
  SDValue Floor = DAG.getNode(ISD::FTRUNC, SL, VT, Div);
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, VT, Floor, Y);

  return DAG.getNode(ISD::FSUB, SL, VT, X, Mul);
}

SDValue AMDGPUTargetLowering::LowerFCEIL(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  // result = trunc(src)
  // if (src > 0.0 && src != result)
  //   result += 1.0

  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, MVT::f64, Src);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, MVT::f64);
  const SDValue One = DAG.getConstantFP(1.0, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);

  SDValue Lt0 = DAG.getSetCC(SL, SetCCVT, Src, Zero, ISD::SETOGT);
  SDValue NeTrunc = DAG.getSetCC(SL, SetCCVT, Src, Trunc, ISD::SETONE);
  SDValue And = DAG.getNode(ISD::AND, SL, SetCCVT, Lt0, NeTrunc);

  SDValue Add = DAG.getNode(ISD::SELECT, SL, MVT::f64, And, One, Zero);
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, Trunc, Add);
}

static SDValue extractF64Exponent(SDValue Hi, const SDLoc &SL,
                                  SelectionDAG &DAG) {
  const unsigned FractBits = 52;
  const unsigned ExpBits = 11;

  SDValue ExpPart = DAG.getNode(AMDGPUISD::BFE_U32, SL, MVT::i32,
                                Hi,
                                DAG.getConstant(FractBits - 32, SL, MVT::i32),
                                DAG.getConstant(ExpBits, SL, MVT::i32));
  SDValue Exp = DAG.getNode(ISD::SUB, SL, MVT::i32, ExpPart,
                            DAG.getConstant(1023, SL, MVT::i32));

  return Exp;
}

SDValue AMDGPUTargetLowering::LowerFTRUNC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  assert(Op.getValueType() == MVT::f64);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);

  SDValue VecSrc = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Src);

  // Extract the upper half, since this is where we will find the sign and
  // exponent.
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, VecSrc, One);

  SDValue Exp = extractF64Exponent(Hi, SL, DAG);

  const unsigned FractBits = 52;

  // Extract the sign bit.
  const SDValue SignBitMask = DAG.getConstant(UINT32_C(1) << 31, SL, MVT::i32);
  SDValue SignBit = DAG.getNode(ISD::AND, SL, MVT::i32, Hi, SignBitMask);

  // Extend back to 64-bits.
  SDValue SignBit64 = DAG.getBuildVector(MVT::v2i32, SL, {Zero, SignBit});
  SignBit64 = DAG.getNode(ISD::BITCAST, SL, MVT::i64, SignBit64);

  SDValue BcInt = DAG.getNode(ISD::BITCAST, SL, MVT::i64, Src);
  const SDValue FractMask
    = DAG.getConstant((UINT64_C(1) << FractBits) - 1, SL, MVT::i64);

  SDValue Shr = DAG.getNode(ISD::SRA, SL, MVT::i64, FractMask, Exp);
  SDValue Not = DAG.getNOT(SL, Shr, MVT::i64);
  SDValue Tmp0 = DAG.getNode(ISD::AND, SL, MVT::i64, BcInt, Not);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::i32);

  const SDValue FiftyOne = DAG.getConstant(FractBits - 1, SL, MVT::i32);

  SDValue ExpLt0 = DAG.getSetCC(SL, SetCCVT, Exp, Zero, ISD::SETLT);
  SDValue ExpGt51 = DAG.getSetCC(SL, SetCCVT, Exp, FiftyOne, ISD::SETGT);

  SDValue Tmp1 = DAG.getNode(ISD::SELECT, SL, MVT::i64, ExpLt0, SignBit64, Tmp0);
  SDValue Tmp2 = DAG.getNode(ISD::SELECT, SL, MVT::i64, ExpGt51, BcInt, Tmp1);

  return DAG.getNode(ISD::BITCAST, SL, MVT::f64, Tmp2);
}

SDValue AMDGPUTargetLowering::LowerFRINT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  assert(Op.getValueType() == MVT::f64);

  APFloat C1Val(APFloat::IEEEdouble(), "0x1.0p+52");
  SDValue C1 = DAG.getConstantFP(C1Val, SL, MVT::f64);
  SDValue CopySign = DAG.getNode(ISD::FCOPYSIGN, SL, MVT::f64, C1, Src);

  // TODO: Should this propagate fast-math-flags?

  SDValue Tmp1 = DAG.getNode(ISD::FADD, SL, MVT::f64, Src, CopySign);
  SDValue Tmp2 = DAG.getNode(ISD::FSUB, SL, MVT::f64, Tmp1, CopySign);

  SDValue Fabs = DAG.getNode(ISD::FABS, SL, MVT::f64, Src);

  APFloat C2Val(APFloat::IEEEdouble(), "0x1.fffffffffffffp+51");
  SDValue C2 = DAG.getConstantFP(C2Val, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);
  SDValue Cond = DAG.getSetCC(SL, SetCCVT, Fabs, C2, ISD::SETOGT);

  return DAG.getSelect(SL, MVT::f64, Cond, Src, Tmp2);
}

SDValue AMDGPUTargetLowering::LowerFNEARBYINT(SDValue Op, SelectionDAG &DAG) const {
  // FNEARBYINT and FRINT are the same, except in their handling of FP
  // exceptions. Those aren't really meaningful for us, and OpenCL only has
  // rint, so just treat them as equivalent.
  return DAG.getNode(ISD::FRINT, SDLoc(Op), Op.getValueType(), Op.getOperand(0));
}

// XXX - May require not supporting f32 denormals?

// Don't handle v2f16. The extra instructions to scalarize and repack around the
// compare and vselect end up producing worse code than scalarizing the whole
// operation.
SDValue AMDGPUTargetLowering::LowerFROUND32_16(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);
  EVT VT = Op.getValueType();

  SDValue T = DAG.getNode(ISD::FTRUNC, SL, VT, X);

  // TODO: Should this propagate fast-math-flags?

  SDValue Diff = DAG.getNode(ISD::FSUB, SL, VT, X, T);

  SDValue AbsDiff = DAG.getNode(ISD::FABS, SL, VT, Diff);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, VT);
  const SDValue One = DAG.getConstantFP(1.0, SL, VT);
  const SDValue Half = DAG.getConstantFP(0.5, SL, VT);

  SDValue SignOne = DAG.getNode(ISD::FCOPYSIGN, SL, VT, One, X);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue Cmp = DAG.getSetCC(SL, SetCCVT, AbsDiff, Half, ISD::SETOGE);

  SDValue Sel = DAG.getNode(ISD::SELECT, SL, VT, Cmp, SignOne, Zero);

  return DAG.getNode(ISD::FADD, SL, VT, T, Sel);
}

SDValue AMDGPUTargetLowering::LowerFROUND64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);

  SDValue L = DAG.getNode(ISD::BITCAST, SL, MVT::i64, X);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);
  const SDValue NegOne = DAG.getConstant(-1, SL, MVT::i32);
  const SDValue FiftyOne = DAG.getConstant(51, SL, MVT::i32);
  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::i32);

  SDValue BC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, X);

  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, BC, One);

  SDValue Exp = extractF64Exponent(Hi, SL, DAG);

  const SDValue Mask = DAG.getConstant(INT64_C(0x000fffffffffffff), SL,
                                       MVT::i64);

  SDValue M = DAG.getNode(ISD::SRA, SL, MVT::i64, Mask, Exp);
  SDValue D = DAG.getNode(ISD::SRA, SL, MVT::i64,
                          DAG.getConstant(INT64_C(0x0008000000000000), SL,
                                          MVT::i64),
                          Exp);

  SDValue Tmp0 = DAG.getNode(ISD::AND, SL, MVT::i64, L, M);
  SDValue Tmp1 = DAG.getSetCC(SL, SetCCVT,
                              DAG.getConstant(0, SL, MVT::i64), Tmp0,
                              ISD::SETNE);

  SDValue Tmp2 = DAG.getNode(ISD::SELECT, SL, MVT::i64, Tmp1,
                             D, DAG.getConstant(0, SL, MVT::i64));
  SDValue K = DAG.getNode(ISD::ADD, SL, MVT::i64, L, Tmp2);

  K = DAG.getNode(ISD::AND, SL, MVT::i64, K, DAG.getNOT(SL, M, MVT::i64));
  K = DAG.getNode(ISD::BITCAST, SL, MVT::f64, K);

  SDValue ExpLt0 = DAG.getSetCC(SL, SetCCVT, Exp, Zero, ISD::SETLT);
  SDValue ExpGt51 = DAG.getSetCC(SL, SetCCVT, Exp, FiftyOne, ISD::SETGT);
  SDValue ExpEqNegOne = DAG.getSetCC(SL, SetCCVT, NegOne, Exp, ISD::SETEQ);

  SDValue Mag = DAG.getNode(ISD::SELECT, SL, MVT::f64,
                            ExpEqNegOne,
                            DAG.getConstantFP(1.0, SL, MVT::f64),
                            DAG.getConstantFP(0.0, SL, MVT::f64));

  SDValue S = DAG.getNode(ISD::FCOPYSIGN, SL, MVT::f64, Mag, X);

  K = DAG.getNode(ISD::SELECT, SL, MVT::f64, ExpLt0, S, K);
  K = DAG.getNode(ISD::SELECT, SL, MVT::f64, ExpGt51, X, K);

  return K;
}

SDValue AMDGPUTargetLowering::LowerFROUND(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();

  if (VT == MVT::f32 || VT == MVT::f16)
    return LowerFROUND32_16(Op, DAG);

  if (VT == MVT::f64)
    return LowerFROUND64(Op, DAG);

  llvm_unreachable("unhandled type");
}

SDValue AMDGPUTargetLowering::LowerFFLOOR(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  // result = trunc(src);
  // if (src < 0.0 && src != result)
  //   result += -1.0.

  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, MVT::f64, Src);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, MVT::f64);
  const SDValue NegOne = DAG.getConstantFP(-1.0, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);

  SDValue Lt0 = DAG.getSetCC(SL, SetCCVT, Src, Zero, ISD::SETOLT);
  SDValue NeTrunc = DAG.getSetCC(SL, SetCCVT, Src, Trunc, ISD::SETONE);
  SDValue And = DAG.getNode(ISD::AND, SL, SetCCVT, Lt0, NeTrunc);

  SDValue Add = DAG.getNode(ISD::SELECT, SL, MVT::f64, And, NegOne, Zero);
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, Trunc, Add);
}

SDValue AMDGPUTargetLowering::LowerFLOG(SDValue Op, SelectionDAG &DAG,
                                        double Log2BaseInverted) const {
  EVT VT = Op.getValueType();

  SDLoc SL(Op);
  SDValue Operand = Op.getOperand(0);
  SDValue Log2Operand = DAG.getNode(ISD::FLOG2, SL, VT, Operand);
  SDValue Log2BaseInvertedOperand = DAG.getConstantFP(Log2BaseInverted, SL, VT);

  return DAG.getNode(ISD::FMUL, SL, VT, Log2Operand, Log2BaseInvertedOperand);
}

// Return M_LOG2E of appropriate type
static SDValue getLog2EVal(SelectionDAG &DAG, const SDLoc &SL, EVT VT) {
  switch (VT.getScalarType().getSimpleVT().SimpleTy) {
  case MVT::f32:
    return DAG.getConstantFP(1.44269504088896340735992468100189214f, SL, VT);
  case MVT::f16:
    return DAG.getConstantFP(
      APFloat(APFloat::IEEEhalf(), "1.44269504088896340735992468100189214"),
      SL, VT);
  case MVT::f64:
    return DAG.getConstantFP(
      APFloat(APFloat::IEEEdouble(), "0x1.71547652b82fep+0"), SL, VT);
  default:
    llvm_unreachable("unsupported fp type");
  }
}

// exp2(M_LOG2E_F * f);
SDValue AMDGPUTargetLowering::lowerFEXP(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  const SDValue K = getLog2EVal(DAG, SL, VT);
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, VT, Src, K, Op->getFlags());
  return DAG.getNode(ISD::FEXP2, SL, VT, Mul, Op->getFlags());
}

static bool isCtlzOpc(unsigned Opc) {
  return Opc == ISD::CTLZ || Opc == ISD::CTLZ_ZERO_UNDEF;
}

static bool isCttzOpc(unsigned Opc) {
  return Opc == ISD::CTTZ || Opc == ISD::CTTZ_ZERO_UNDEF;
}

SDValue AMDGPUTargetLowering::LowerCTLZ_CTTZ(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);
  bool ZeroUndef = Op.getOpcode() == ISD::CTTZ_ZERO_UNDEF ||
                   Op.getOpcode() == ISD::CTLZ_ZERO_UNDEF;

  unsigned ISDOpc, NewOpc;
  if (isCtlzOpc(Op.getOpcode())) {
    ISDOpc = ISD::CTLZ_ZERO_UNDEF;
    NewOpc = AMDGPUISD::FFBH_U32;
  } else if (isCttzOpc(Op.getOpcode())) {
    ISDOpc = ISD::CTTZ_ZERO_UNDEF;
    NewOpc = AMDGPUISD::FFBL_B32;
  } else
    llvm_unreachable("Unexpected OPCode!!!");


  if (ZeroUndef && Src.getValueType() == MVT::i32)
    return DAG.getNode(NewOpc, SL, MVT::i32, Src);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Src);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);

  SDValue Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, Zero);
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, One);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(),
                                   *DAG.getContext(), MVT::i32);

  SDValue HiOrLo = isCtlzOpc(Op.getOpcode()) ? Hi : Lo;
  SDValue Hi0orLo0 = DAG.getSetCC(SL, SetCCVT, HiOrLo, Zero, ISD::SETEQ);

  SDValue OprLo = DAG.getNode(ISDOpc, SL, MVT::i32, Lo);
  SDValue OprHi = DAG.getNode(ISDOpc, SL, MVT::i32, Hi);

  const SDValue Bits32 = DAG.getConstant(32, SL, MVT::i32);
  SDValue Add, NewOpr;
  if (isCtlzOpc(Op.getOpcode())) {
    Add = DAG.getNode(ISD::ADD, SL, MVT::i32, OprLo, Bits32);
    // ctlz(x) = hi_32(x) == 0 ? ctlz(lo_32(x)) + 32 : ctlz(hi_32(x))
    NewOpr = DAG.getNode(ISD::SELECT, SL, MVT::i32, Hi0orLo0, Add, OprHi);
  } else {
    Add = DAG.getNode(ISD::ADD, SL, MVT::i32, OprHi, Bits32);
    // cttz(x) = lo_32(x) == 0 ? cttz(hi_32(x)) + 32 : cttz(lo_32(x))
    NewOpr = DAG.getNode(ISD::SELECT, SL, MVT::i32, Hi0orLo0, Add, OprLo);
  }

  if (!ZeroUndef) {
    // Test if the full 64-bit input is zero.

    // FIXME: DAG combines turn what should be an s_and_b64 into a v_or_b32,
    // which we probably don't want.
    SDValue LoOrHi = isCtlzOpc(Op.getOpcode()) ? Lo : Hi;
    SDValue Lo0OrHi0 = DAG.getSetCC(SL, SetCCVT, LoOrHi, Zero, ISD::SETEQ);
    SDValue SrcIsZero = DAG.getNode(ISD::AND, SL, SetCCVT, Lo0OrHi0, Hi0orLo0);

    // TODO: If i64 setcc is half rate, it can result in 1 fewer instruction
    // with the same cycles, otherwise it is slower.
    // SDValue SrcIsZero = DAG.getSetCC(SL, SetCCVT, Src,
    // DAG.getConstant(0, SL, MVT::i64), ISD::SETEQ);

    const SDValue Bits32 = DAG.getConstant(64, SL, MVT::i32);

    // The instruction returns -1 for 0 input, but the defined intrinsic
    // behavior is to return the number of bits.
    NewOpr = DAG.getNode(ISD::SELECT, SL, MVT::i32,
                         SrcIsZero, Bits32, NewOpr);
  }

  return DAG.getNode(ISD::ZERO_EXTEND, SL, MVT::i64, NewOpr);
}

SDValue AMDGPUTargetLowering::LowerINT_TO_FP32(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  // Unsigned
  // cul2f(ulong u)
  //{
  //  uint lz = clz(u);
  //  uint e = (u != 0) ? 127U + 63U - lz : 0;
  //  u = (u << lz) & 0x7fffffffffffffffUL;
  //  ulong t = u & 0xffffffffffUL;
  //  uint v = (e << 23) | (uint)(u >> 40);
  //  uint r = t > 0x8000000000UL ? 1U : (t == 0x8000000000UL ? v & 1U : 0U);
  //  return as_float(v + r);
  //}
  // Signed
  // cl2f(long l)
  //{
  //  long s = l >> 63;
  //  float r = cul2f((l + s) ^ s);
  //  return s ? -r : r;
  //}

  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);
  SDValue L = Src;

  SDValue S;
  if (Signed) {
    const SDValue SignBit = DAG.getConstant(63, SL, MVT::i64);
    S = DAG.getNode(ISD::SRA, SL, MVT::i64, L, SignBit);

    SDValue LPlusS = DAG.getNode(ISD::ADD, SL, MVT::i64, L, S);
    L = DAG.getNode(ISD::XOR, SL, MVT::i64, LPlusS, S);
  }

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(),
                                   *DAG.getContext(), MVT::f32);


  SDValue ZeroI32 = DAG.getConstant(0, SL, MVT::i32);
  SDValue ZeroI64 = DAG.getConstant(0, SL, MVT::i64);
  SDValue LZ = DAG.getNode(ISD::CTLZ_ZERO_UNDEF, SL, MVT::i64, L);
  LZ = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, LZ);

  SDValue K = DAG.getConstant(127U + 63U, SL, MVT::i32);
  SDValue E = DAG.getSelect(SL, MVT::i32,
    DAG.getSetCC(SL, SetCCVT, L, ZeroI64, ISD::SETNE),
    DAG.getNode(ISD::SUB, SL, MVT::i32, K, LZ),
    ZeroI32);

  SDValue U = DAG.getNode(ISD::AND, SL, MVT::i64,
    DAG.getNode(ISD::SHL, SL, MVT::i64, L, LZ),
    DAG.getConstant((-1ULL) >> 1, SL, MVT::i64));

  SDValue T = DAG.getNode(ISD::AND, SL, MVT::i64, U,
                          DAG.getConstant(0xffffffffffULL, SL, MVT::i64));

  SDValue UShl = DAG.getNode(ISD::SRL, SL, MVT::i64,
                             U, DAG.getConstant(40, SL, MVT::i64));

  SDValue V = DAG.getNode(ISD::OR, SL, MVT::i32,
    DAG.getNode(ISD::SHL, SL, MVT::i32, E, DAG.getConstant(23, SL, MVT::i32)),
    DAG.getNode(ISD::TRUNCATE, SL, MVT::i32,  UShl));

  SDValue C = DAG.getConstant(0x8000000000ULL, SL, MVT::i64);
  SDValue RCmp = DAG.getSetCC(SL, SetCCVT, T, C, ISD::SETUGT);
  SDValue TCmp = DAG.getSetCC(SL, SetCCVT, T, C, ISD::SETEQ);

  SDValue One = DAG.getConstant(1, SL, MVT::i32);

  SDValue VTrunc1 = DAG.getNode(ISD::AND, SL, MVT::i32, V, One);

  SDValue R = DAG.getSelect(SL, MVT::i32,
    RCmp,
    One,
    DAG.getSelect(SL, MVT::i32, TCmp, VTrunc1, ZeroI32));
  R = DAG.getNode(ISD::ADD, SL, MVT::i32, V, R);
  R = DAG.getNode(ISD::BITCAST, SL, MVT::f32, R);

  if (!Signed)
    return R;

  SDValue RNeg = DAG.getNode(ISD::FNEG, SL, MVT::f32, R);
  return DAG.getSelect(SL, MVT::f32, DAG.getSExtOrTrunc(S, SL, SetCCVT), RNeg, R);
}

SDValue AMDGPUTargetLowering::LowerINT_TO_FP64(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  SDValue BC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Src);

  SDValue Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, BC,
                           DAG.getConstant(0, SL, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, BC,
                           DAG.getConstant(1, SL, MVT::i32));

  SDValue CvtHi = DAG.getNode(Signed ? ISD::SINT_TO_FP : ISD::UINT_TO_FP,
                              SL, MVT::f64, Hi);

  SDValue CvtLo = DAG.getNode(ISD::UINT_TO_FP, SL, MVT::f64, Lo);

  SDValue LdExp = DAG.getNode(AMDGPUISD::LDEXP, SL, MVT::f64, CvtHi,
                              DAG.getConstant(32, SL, MVT::i32));
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, LdExp, CvtLo);
}

SDValue AMDGPUTargetLowering::LowerUINT_TO_FP(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Op.getOperand(0).getValueType() == MVT::i64 &&
         "operation should be legal");

  // TODO: Factor out code common with LowerSINT_TO_FP.

  EVT DestVT = Op.getValueType();
  if (Subtarget->has16BitInsts() && DestVT == MVT::f16) {
    SDLoc DL(Op);
    SDValue Src = Op.getOperand(0);

    SDValue IntToFp32 = DAG.getNode(Op.getOpcode(), DL, MVT::f32, Src);
    SDValue FPRoundFlag = DAG.getIntPtrConstant(0, SDLoc(Op));
    SDValue FPRound =
        DAG.getNode(ISD::FP_ROUND, DL, MVT::f16, IntToFp32, FPRoundFlag);

    return FPRound;
  }

  if (DestVT == MVT::f32)
    return LowerINT_TO_FP32(Op, DAG, false);

  assert(DestVT == MVT::f64);
  return LowerINT_TO_FP64(Op, DAG, false);
}

SDValue AMDGPUTargetLowering::LowerSINT_TO_FP(SDValue Op,
                                              SelectionDAG &DAG) const {
  assert(Op.getOperand(0).getValueType() == MVT::i64 &&
         "operation should be legal");

  // TODO: Factor out code common with LowerUINT_TO_FP.

  EVT DestVT = Op.getValueType();
  if (Subtarget->has16BitInsts() && DestVT == MVT::f16) {
    SDLoc DL(Op);
    SDValue Src = Op.getOperand(0);

    SDValue IntToFp32 = DAG.getNode(Op.getOpcode(), DL, MVT::f32, Src);
    SDValue FPRoundFlag = DAG.getIntPtrConstant(0, SDLoc(Op));
    SDValue FPRound =
        DAG.getNode(ISD::FP_ROUND, DL, MVT::f16, IntToFp32, FPRoundFlag);

    return FPRound;
  }

  if (DestVT == MVT::f32)
    return LowerINT_TO_FP32(Op, DAG, true);

  assert(DestVT == MVT::f64);
  return LowerINT_TO_FP64(Op, DAG, true);
}

SDValue AMDGPUTargetLowering::LowerFP64_TO_INT(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  SDLoc SL(Op);

  SDValue Src = Op.getOperand(0);

  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, MVT::f64, Src);

  SDValue K0 = DAG.getConstantFP(BitsToDouble(UINT64_C(0x3df0000000000000)), SL,
                                 MVT::f64);
  SDValue K1 = DAG.getConstantFP(BitsToDouble(UINT64_C(0xc1f0000000000000)), SL,
                                 MVT::f64);
  // TODO: Should this propagate fast-math-flags?
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, MVT::f64, Trunc, K0);

  SDValue FloorMul = DAG.getNode(ISD::FFLOOR, SL, MVT::f64, Mul);


  SDValue Fma = DAG.getNode(ISD::FMA, SL, MVT::f64, FloorMul, K1, Trunc);

  SDValue Hi = DAG.getNode(Signed ? ISD::FP_TO_SINT : ISD::FP_TO_UINT, SL,
                           MVT::i32, FloorMul);
  SDValue Lo = DAG.getNode(ISD::FP_TO_UINT, SL, MVT::i32, Fma);

  SDValue Result = DAG.getBuildVector(MVT::v2i32, SL, {Lo, Hi});

  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Result);
}

SDValue AMDGPUTargetLowering::LowerFP_TO_FP16(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue N0 = Op.getOperand(0);

  // Convert to target node to get known bits
  if (N0.getValueType() == MVT::f32)
    return DAG.getNode(AMDGPUISD::FP_TO_FP16, DL, Op.getValueType(), N0);

  if (getTargetMachine().Options.UnsafeFPMath) {
    // There is a generic expand for FP_TO_FP16 with unsafe fast math.
    return SDValue();
  }

  assert(N0.getSimpleValueType() == MVT::f64);

  // f64 -> f16 conversion using round-to-nearest-even rounding mode.
  const unsigned ExpMask = 0x7ff;
  const unsigned ExpBiasf64 = 1023;
  const unsigned ExpBiasf16 = 15;
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue One = DAG.getConstant(1, DL, MVT::i32);
  SDValue U = DAG.getNode(ISD::BITCAST, DL, MVT::i64, N0);
  SDValue UH = DAG.getNode(ISD::SRL, DL, MVT::i64, U,
                           DAG.getConstant(32, DL, MVT::i64));
  UH = DAG.getZExtOrTrunc(UH, DL, MVT::i32);
  U = DAG.getZExtOrTrunc(U, DL, MVT::i32);
  SDValue E = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                          DAG.getConstant(20, DL, MVT::i64));
  E = DAG.getNode(ISD::AND, DL, MVT::i32, E,
                  DAG.getConstant(ExpMask, DL, MVT::i32));
  // Subtract the fp64 exponent bias (1023) to get the real exponent and
  // add the f16 bias (15) to get the biased exponent for the f16 format.
  E = DAG.getNode(ISD::ADD, DL, MVT::i32, E,
                  DAG.getConstant(-ExpBiasf64 + ExpBiasf16, DL, MVT::i32));

  SDValue M = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                          DAG.getConstant(8, DL, MVT::i32));
  M = DAG.getNode(ISD::AND, DL, MVT::i32, M,
                  DAG.getConstant(0xffe, DL, MVT::i32));

  SDValue MaskedSig = DAG.getNode(ISD::AND, DL, MVT::i32, UH,
                                  DAG.getConstant(0x1ff, DL, MVT::i32));
  MaskedSig = DAG.getNode(ISD::OR, DL, MVT::i32, MaskedSig, U);

  SDValue Lo40Set = DAG.getSelectCC(DL, MaskedSig, Zero, Zero, One, ISD::SETEQ);
  M = DAG.getNode(ISD::OR, DL, MVT::i32, M, Lo40Set);

  // (M != 0 ? 0x0200 : 0) | 0x7c00;
  SDValue I = DAG.getNode(ISD::OR, DL, MVT::i32,
      DAG.getSelectCC(DL, M, Zero, DAG.getConstant(0x0200, DL, MVT::i32),
                      Zero, ISD::SETNE), DAG.getConstant(0x7c00, DL, MVT::i32));

  // N = M | (E << 12);
  SDValue N = DAG.getNode(ISD::OR, DL, MVT::i32, M,
      DAG.getNode(ISD::SHL, DL, MVT::i32, E,
                  DAG.getConstant(12, DL, MVT::i32)));

  // B = clamp(1-E, 0, 13);
  SDValue OneSubExp = DAG.getNode(ISD::SUB, DL, MVT::i32,
                                  One, E);
  SDValue B = DAG.getNode(ISD::SMAX, DL, MVT::i32, OneSubExp, Zero);
  B = DAG.getNode(ISD::SMIN, DL, MVT::i32, B,
                  DAG.getConstant(13, DL, MVT::i32));

  SDValue SigSetHigh = DAG.getNode(ISD::OR, DL, MVT::i32, M,
                                   DAG.getConstant(0x1000, DL, MVT::i32));

  SDValue D = DAG.getNode(ISD::SRL, DL, MVT::i32, SigSetHigh, B);
  SDValue D0 = DAG.getNode(ISD::SHL, DL, MVT::i32, D, B);
  SDValue D1 = DAG.getSelectCC(DL, D0, SigSetHigh, One, Zero, ISD::SETNE);
  D = DAG.getNode(ISD::OR, DL, MVT::i32, D, D1);

  SDValue V = DAG.getSelectCC(DL, E, One, D, N, ISD::SETLT);
  SDValue VLow3 = DAG.getNode(ISD::AND, DL, MVT::i32, V,
                              DAG.getConstant(0x7, DL, MVT::i32));
  V = DAG.getNode(ISD::SRL, DL, MVT::i32, V,
                  DAG.getConstant(2, DL, MVT::i32));
  SDValue V0 = DAG.getSelectCC(DL, VLow3, DAG.getConstant(3, DL, MVT::i32),
                               One, Zero, ISD::SETEQ);
  SDValue V1 = DAG.getSelectCC(DL, VLow3, DAG.getConstant(5, DL, MVT::i32),
                               One, Zero, ISD::SETGT);
  V1 = DAG.getNode(ISD::OR, DL, MVT::i32, V0, V1);
  V = DAG.getNode(ISD::ADD, DL, MVT::i32, V, V1);

  V = DAG.getSelectCC(DL, E, DAG.getConstant(30, DL, MVT::i32),
                      DAG.getConstant(0x7c00, DL, MVT::i32), V, ISD::SETGT);
  V = DAG.getSelectCC(DL, E, DAG.getConstant(1039, DL, MVT::i32),
                      I, V, ISD::SETEQ);

  // Extract the sign bit.
  SDValue Sign = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                            DAG.getConstant(16, DL, MVT::i32));
  Sign = DAG.getNode(ISD::AND, DL, MVT::i32, Sign,
                     DAG.getConstant(0x8000, DL, MVT::i32));

  V = DAG.getNode(ISD::OR, DL, MVT::i32, Sign, V);
  return DAG.getZExtOrTrunc(V, DL, Op.getValueType());
}

SDValue AMDGPUTargetLowering::LowerFP_TO_SINT(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue Src = Op.getOperand(0);

  // TODO: Factor out code common with LowerFP_TO_UINT.

  EVT SrcVT = Src.getValueType();
  if (Subtarget->has16BitInsts() && SrcVT == MVT::f16) {
    SDLoc DL(Op);

    SDValue FPExtend = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Src);
    SDValue FpToInt32 =
        DAG.getNode(Op.getOpcode(), DL, MVT::i64, FPExtend);

    return FpToInt32;
  }

  if (Op.getValueType() == MVT::i64 && Src.getValueType() == MVT::f64)
    return LowerFP64_TO_INT(Op, DAG, true);

  return SDValue();
}

SDValue AMDGPUTargetLowering::LowerFP_TO_UINT(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue Src = Op.getOperand(0);

  // TODO: Factor out code common with LowerFP_TO_SINT.

  EVT SrcVT = Src.getValueType();
  if (Subtarget->has16BitInsts() && SrcVT == MVT::f16) {
    SDLoc DL(Op);

    SDValue FPExtend = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Src);
    SDValue FpToInt32 =
        DAG.getNode(Op.getOpcode(), DL, MVT::i64, FPExtend);

    return FpToInt32;
  }

  if (Op.getValueType() == MVT::i64 && Src.getValueType() == MVT::f64)
    return LowerFP64_TO_INT(Op, DAG, false);

  return SDValue();
}

SDValue AMDGPUTargetLowering::LowerSIGN_EXTEND_INREG(SDValue Op,
                                                     SelectionDAG &DAG) const {
  EVT ExtraVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  MVT VT = Op.getSimpleValueType();
  MVT ScalarVT = VT.getScalarType();

  assert(VT.isVector());

  SDValue Src = Op.getOperand(0);
  SDLoc DL(Op);

  // TODO: Don't scalarize on Evergreen?
  unsigned NElts = VT.getVectorNumElements();
  SmallVector<SDValue, 8> Args;
  DAG.ExtractVectorElements(Src, Args, 0, NElts);

  SDValue VTOp = DAG.getValueType(ExtraVT.getScalarType());
  for (unsigned I = 0; I < NElts; ++I)
    Args[I] = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, ScalarVT, Args[I], VTOp);

  return DAG.getBuildVector(VT, DL, Args);
}

//===----------------------------------------------------------------------===//
// Custom DAG optimizations
//===----------------------------------------------------------------------===//

static bool isU24(SDValue Op, SelectionDAG &DAG) {
  return AMDGPUTargetLowering::numBitsUnsigned(Op, DAG) <= 24;
}

static bool isI24(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  return VT.getSizeInBits() >= 24 && // Types less than 24-bit should be treated
                                     // as unsigned 24-bit values.
    AMDGPUTargetLowering::numBitsSigned(Op, DAG) < 24;
}

static SDValue simplifyI24(SDNode *Node24,
                           TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  SDValue LHS = Node24->getOperand(0);
  SDValue RHS = Node24->getOperand(1);

  APInt Demanded = APInt::getLowBitsSet(LHS.getValueSizeInBits(), 24);

  // First try to simplify using GetDemandedBits which allows the operands to
  // have other uses, but will only perform simplifications that involve
  // bypassing some nodes for this user.
  SDValue DemandedLHS = DAG.GetDemandedBits(LHS, Demanded);
  SDValue DemandedRHS = DAG.GetDemandedBits(RHS, Demanded);
  if (DemandedLHS || DemandedRHS)
    return DAG.getNode(Node24->getOpcode(), SDLoc(Node24), Node24->getVTList(),
                       DemandedLHS ? DemandedLHS : LHS,
                       DemandedRHS ? DemandedRHS : RHS);

  // Now try SimplifyDemandedBits which can simplify the nodes used by our
  // operands if this node is the only user.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.SimplifyDemandedBits(LHS, Demanded, DCI))
    return SDValue(Node24, 0);
  if (TLI.SimplifyDemandedBits(RHS, Demanded, DCI))
    return SDValue(Node24, 0);

  return SDValue();
}

template <typename IntTy>
static SDValue constantFoldBFE(SelectionDAG &DAG, IntTy Src0, uint32_t Offset,
                               uint32_t Width, const SDLoc &DL) {
  if (Width + Offset < 32) {
    uint32_t Shl = static_cast<uint32_t>(Src0) << (32 - Offset - Width);
    IntTy Result = static_cast<IntTy>(Shl) >> (32 - Width);
    return DAG.getConstant(Result, DL, MVT::i32);
  }

  return DAG.getConstant(Src0 >> Offset, DL, MVT::i32);
}

static bool hasVolatileUser(SDNode *Val) {
  for (SDNode *U : Val->uses()) {
    if (MemSDNode *M = dyn_cast<MemSDNode>(U)) {
      if (M->isVolatile())
        return true;
    }
  }

  return false;
}

bool AMDGPUTargetLowering::shouldCombineMemoryType(EVT VT) const {
  // i32 vectors are the canonical memory type.
  if (VT.getScalarType() == MVT::i32 || isTypeLegal(VT))
    return false;

  if (!VT.isByteSized())
    return false;

  unsigned Size = VT.getStoreSize();

  if ((Size == 1 || Size == 2 || Size == 4) && !VT.isVector())
    return false;

  if (Size == 3 || (Size > 4 && (Size % 4 != 0)))
    return false;

  return true;
}

// Replace load of an illegal type with a store of a bitcast to a friendlier
// type.
SDValue AMDGPUTargetLowering::performLoadCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();

  LoadSDNode *LN = cast<LoadSDNode>(N);
  if (LN->isVolatile() || !ISD::isNormalLoad(LN) || hasVolatileUser(LN))
    return SDValue();

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = LN->getMemoryVT();

  unsigned Size = VT.getStoreSize();
  unsigned Align = LN->getAlignment();
  if (Align < Size && isTypeLegal(VT)) {
    bool IsFast;
    unsigned AS = LN->getAddressSpace();

    // Expand unaligned loads earlier than legalization. Due to visitation order
    // problems during legalization, the emitted instructions to pack and unpack
    // the bytes again are not eliminated in the case of an unaligned copy.
    if (!allowsMisalignedMemoryAccesses(VT, AS, Align, &IsFast)) {
      if (VT.isVector())
        return scalarizeVectorLoad(LN, DAG);

      SDValue Ops[2];
      std::tie(Ops[0], Ops[1]) = expandUnalignedLoad(LN, DAG);
      return DAG.getMergeValues(Ops, SDLoc(N));
    }

    if (!IsFast)
      return SDValue();
  }

  if (!shouldCombineMemoryType(VT))
    return SDValue();

  EVT NewVT = getEquivalentMemType(*DAG.getContext(), VT);

  SDValue NewLoad
    = DAG.getLoad(NewVT, SL, LN->getChain(),
                  LN->getBasePtr(), LN->getMemOperand());

  SDValue BC = DAG.getNode(ISD::BITCAST, SL, VT, NewLoad);
  DCI.CombineTo(N, BC, NewLoad.getValue(1));
  return SDValue(N, 0);
}

// Replace store of an illegal type with a store of a bitcast to a friendlier
// type.
SDValue AMDGPUTargetLowering::performStoreCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();

  StoreSDNode *SN = cast<StoreSDNode>(N);
  if (SN->isVolatile() || !ISD::isNormalStore(SN))
    return SDValue();

  EVT VT = SN->getMemoryVT();
  unsigned Size = VT.getStoreSize();

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  unsigned Align = SN->getAlignment();
  if (Align < Size && isTypeLegal(VT)) {
    bool IsFast;
    unsigned AS = SN->getAddressSpace();

    // Expand unaligned stores earlier than legalization. Due to visitation
    // order problems during legalization, the emitted instructions to pack and
    // unpack the bytes again are not eliminated in the case of an unaligned
    // copy.
    if (!allowsMisalignedMemoryAccesses(VT, AS, Align, &IsFast)) {
      if (VT.isVector())
        return scalarizeVectorStore(SN, DAG);

      return expandUnalignedStore(SN, DAG);
    }

    if (!IsFast)
      return SDValue();
  }

  if (!shouldCombineMemoryType(VT))
    return SDValue();

  EVT NewVT = getEquivalentMemType(*DAG.getContext(), VT);
  SDValue Val = SN->getValue();

  //DCI.AddToWorklist(Val.getNode());

  bool OtherUses = !Val.hasOneUse();
  SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, NewVT, Val);
  if (OtherUses) {
    SDValue CastBack = DAG.getNode(ISD::BITCAST, SL, VT, CastVal);
    DAG.ReplaceAllUsesOfValueWith(Val, CastBack);
  }

  return DAG.getStore(SN->getChain(), SL, CastVal,
                      SN->getBasePtr(), SN->getMemOperand());
}

// FIXME: This should go in generic DAG combiner with an isTruncateFree check,
// but isTruncateFree is inaccurate for i16 now because of SALU vs. VALU
// issues.
SDValue AMDGPUTargetLowering::performAssertSZExtCombine(SDNode *N,
                                                        DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);

  // (vt2 (assertzext (truncate vt0:x), vt1)) ->
  //     (vt2 (truncate (assertzext vt0:x, vt1)))
  if (N0.getOpcode() == ISD::TRUNCATE) {
    SDValue N1 = N->getOperand(1);
    EVT ExtVT = cast<VTSDNode>(N1)->getVT();
    SDLoc SL(N);

    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();
    if (SrcVT.bitsGE(ExtVT)) {
      SDValue NewInReg = DAG.getNode(N->getOpcode(), SL, SrcVT, Src, N1);
      return DAG.getNode(ISD::TRUNCATE, SL, N->getValueType(0), NewInReg);
    }
  }

  return SDValue();
}
/// Split the 64-bit value \p LHS into two 32-bit components, and perform the
/// binary operation \p Opc to it with the corresponding constant operands.
SDValue AMDGPUTargetLowering::splitBinaryBitConstantOpImpl(
  DAGCombinerInfo &DCI, const SDLoc &SL,
  unsigned Opc, SDValue LHS,
  uint32_t ValLo, uint32_t ValHi) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = split64BitValue(LHS, DAG);

  SDValue LoRHS = DAG.getConstant(ValLo, SL, MVT::i32);
  SDValue HiRHS = DAG.getConstant(ValHi, SL, MVT::i32);

  SDValue LoAnd = DAG.getNode(Opc, SL, MVT::i32, Lo, LoRHS);
  SDValue HiAnd = DAG.getNode(Opc, SL, MVT::i32, Hi, HiRHS);

  // Re-visit the ands. It's possible we eliminated one of them and it could
  // simplify the vector.
  DCI.AddToWorklist(Lo.getNode());
  DCI.AddToWorklist(Hi.getNode());

  SDValue Vec = DAG.getBuildVector(MVT::v2i32, SL, {LoAnd, HiAnd});
  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
}

SDValue AMDGPUTargetLowering::performShlCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  SDValue LHS = N->getOperand(0);
  unsigned RHSVal = RHS->getZExtValue();
  if (!RHSVal)
    return LHS;

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;

  switch (LHS->getOpcode()) {
  default:
    break;
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ANY_EXTEND: {
    SDValue X = LHS->getOperand(0);

    if (VT == MVT::i32 && RHSVal == 16 && X.getValueType() == MVT::i16 &&
        isOperationLegal(ISD::BUILD_VECTOR, MVT::v2i16)) {
      // Prefer build_vector as the canonical form if packed types are legal.
      // (shl ([asz]ext i16:x), 16 -> build_vector 0, x
      SDValue Vec = DAG.getBuildVector(MVT::v2i16, SL,
       { DAG.getConstant(0, SL, MVT::i16), LHS->getOperand(0) });
      return DAG.getNode(ISD::BITCAST, SL, MVT::i32, Vec);
    }

    // shl (ext x) => zext (shl x), if shift does not overflow int
    if (VT != MVT::i64)
      break;
    KnownBits Known = DAG.computeKnownBits(X);
    unsigned LZ = Known.countMinLeadingZeros();
    if (LZ < RHSVal)
      break;
    EVT XVT = X.getValueType();
    SDValue Shl = DAG.getNode(ISD::SHL, SL, XVT, X, SDValue(RHS, 0));
    return DAG.getZExtOrTrunc(Shl, SL, VT);
  }
  }

  if (VT != MVT::i64)
    return SDValue();

  // i64 (shl x, C) -> (build_pair 0, (shl x, C -32))

  // On some subtargets, 64-bit shift is a quarter rate instruction. In the
  // common case, splitting this into a move and a 32-bit shift is faster and
  // the same code size.
  if (RHSVal < 32)
    return SDValue();

  SDValue ShiftAmt = DAG.getConstant(RHSVal - 32, SL, MVT::i32);

  SDValue Lo = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, LHS);
  SDValue NewShift = DAG.getNode(ISD::SHL, SL, MVT::i32, Lo, ShiftAmt);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);

  SDValue Vec = DAG.getBuildVector(MVT::v2i32, SL, {Zero, NewShift});
  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
}

SDValue AMDGPUTargetLowering::performSraCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  if (N->getValueType(0) != MVT::i64)
    return SDValue();

  const ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  unsigned RHSVal = RHS->getZExtValue();

  // (sra i64:x, 32) -> build_pair x, (sra hi_32(x), 31)
  if (RHSVal == 32) {
    SDValue Hi = getHiHalf64(N->getOperand(0), DAG);
    SDValue NewShift = DAG.getNode(ISD::SRA, SL, MVT::i32, Hi,
                                   DAG.getConstant(31, SL, MVT::i32));

    SDValue BuildVec = DAG.getBuildVector(MVT::v2i32, SL, {Hi, NewShift});
    return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildVec);
  }

  // (sra i64:x, 63) -> build_pair (sra hi_32(x), 31), (sra hi_32(x), 31)
  if (RHSVal == 63) {
    SDValue Hi = getHiHalf64(N->getOperand(0), DAG);
    SDValue NewShift = DAG.getNode(ISD::SRA, SL, MVT::i32, Hi,
                                   DAG.getConstant(31, SL, MVT::i32));
    SDValue BuildVec = DAG.getBuildVector(MVT::v2i32, SL, {NewShift, NewShift});
    return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildVec);
  }

  return SDValue();
}

SDValue AMDGPUTargetLowering::performSrlCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  if (N->getValueType(0) != MVT::i64)
    return SDValue();

  const ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  unsigned ShiftAmt = RHS->getZExtValue();
  if (ShiftAmt < 32)
    return SDValue();

  // srl i64:x, C for C >= 32
  // =>
  //   build_pair (srl hi_32(x), C - 32), 0

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  SDValue One = DAG.getConstant(1, SL, MVT::i32);
  SDValue Zero = DAG.getConstant(0, SL, MVT::i32);

  SDValue VecOp = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, N->getOperand(0));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32,
                           VecOp, One);

  SDValue NewConst = DAG.getConstant(ShiftAmt - 32, SL, MVT::i32);
  SDValue NewShift = DAG.getNode(ISD::SRL, SL, MVT::i32, Hi, NewConst);

  SDValue BuildPair = DAG.getBuildVector(MVT::v2i32, SL, {NewShift, Zero});

  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildPair);
}

SDValue AMDGPUTargetLowering::performTruncateCombine(
  SDNode *N, DAGCombinerInfo &DCI) const {
  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDValue Src = N->getOperand(0);

  // vt1 (truncate (bitcast (build_vector vt0:x, ...))) -> vt1 (bitcast vt0:x)
  if (Src.getOpcode() == ISD::BITCAST) {
    SDValue Vec = Src.getOperand(0);
    if (Vec.getOpcode() == ISD::BUILD_VECTOR) {
      SDValue Elt0 = Vec.getOperand(0);
      EVT EltVT = Elt0.getValueType();
      if (VT.getSizeInBits() <= EltVT.getSizeInBits()) {
        if (EltVT.isFloatingPoint()) {
          Elt0 = DAG.getNode(ISD::BITCAST, SL,
                             EltVT.changeTypeToInteger(), Elt0);
        }

        return DAG.getNode(ISD::TRUNCATE, SL, VT, Elt0);
      }
    }
  }

  // Equivalent of above for accessing the high element of a vector as an
  // integer operation.
  // trunc (srl (bitcast (build_vector x, y))), 16 -> trunc (bitcast y)
  if (Src.getOpcode() == ISD::SRL && !VT.isVector()) {
    if (auto K = isConstOrConstSplat(Src.getOperand(1))) {
      if (2 * K->getZExtValue() == Src.getValueType().getScalarSizeInBits()) {
        SDValue BV = stripBitcast(Src.getOperand(0));
        if (BV.getOpcode() == ISD::BUILD_VECTOR &&
            BV.getValueType().getVectorNumElements() == 2) {
          SDValue SrcElt = BV.getOperand(1);
          EVT SrcEltVT = SrcElt.getValueType();
          if (SrcEltVT.isFloatingPoint()) {
            SrcElt = DAG.getNode(ISD::BITCAST, SL,
                                 SrcEltVT.changeTypeToInteger(), SrcElt);
          }

          return DAG.getNode(ISD::TRUNCATE, SL, VT, SrcElt);
        }
      }
    }
  }

  // Partially shrink 64-bit shifts to 32-bit if reduced to 16-bit.
  //
  // i16 (trunc (srl i64:x, K)), K <= 16 ->
  //     i16 (trunc (srl (i32 (trunc x), K)))
  if (VT.getScalarSizeInBits() < 32) {
    EVT SrcVT = Src.getValueType();
    if (SrcVT.getScalarSizeInBits() > 32 &&
        (Src.getOpcode() == ISD::SRL ||
         Src.getOpcode() == ISD::SRA ||
         Src.getOpcode() == ISD::SHL)) {
      SDValue Amt = Src.getOperand(1);
      KnownBits Known = DAG.computeKnownBits(Amt);
      unsigned Size = VT.getScalarSizeInBits();
      if ((Known.isConstant() && Known.getConstant().ule(Size)) ||
          (Known.getBitWidth() - Known.countMinLeadingZeros() <= Log2_32(Size))) {
        EVT MidVT = VT.isVector() ?
          EVT::getVectorVT(*DAG.getContext(), MVT::i32,
                           VT.getVectorNumElements()) : MVT::i32;

        EVT NewShiftVT = getShiftAmountTy(MidVT, DAG.getDataLayout());
        SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, MidVT,
                                    Src.getOperand(0));
        DCI.AddToWorklist(Trunc.getNode());

        if (Amt.getValueType() != NewShiftVT) {
          Amt = DAG.getZExtOrTrunc(Amt, SL, NewShiftVT);
          DCI.AddToWorklist(Amt.getNode());
        }

        SDValue ShrunkShift = DAG.getNode(Src.getOpcode(), SL, MidVT,
                                          Trunc, Amt);
        return DAG.getNode(ISD::TRUNCATE, SL, VT, ShrunkShift);
      }
    }
  }

  return SDValue();
}

// We need to specifically handle i64 mul here to avoid unnecessary conversion
// instructions. If we only match on the legalized i64 mul expansion,
// SimplifyDemandedBits will be unable to remove them because there will be
// multiple uses due to the separate mul + mulh[su].
static SDValue getMul24(SelectionDAG &DAG, const SDLoc &SL,
                        SDValue N0, SDValue N1, unsigned Size, bool Signed) {
  if (Size <= 32) {
    unsigned MulOpc = Signed ? AMDGPUISD::MUL_I24 : AMDGPUISD::MUL_U24;
    return DAG.getNode(MulOpc, SL, MVT::i32, N0, N1);
  }

  // Because we want to eliminate extension instructions before the
  // operation, we need to create a single user here (i.e. not the separate
  // mul_lo + mul_hi) so that SimplifyDemandedBits will deal with it.

  unsigned MulOpc = Signed ? AMDGPUISD::MUL_LOHI_I24 : AMDGPUISD::MUL_LOHI_U24;

  SDValue Mul = DAG.getNode(MulOpc, SL,
                            DAG.getVTList(MVT::i32, MVT::i32), N0, N1);

  return DAG.getNode(ISD::BUILD_PAIR, SL, MVT::i64,
                     Mul.getValue(0), Mul.getValue(1));
}

SDValue AMDGPUTargetLowering::performMulCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  unsigned Size = VT.getSizeInBits();
  if (VT.isVector() || Size > 64)
    return SDValue();

  // There are i16 integer mul/mad.
  if (Subtarget->has16BitInsts() && VT.getScalarType().bitsLE(MVT::i16))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // SimplifyDemandedBits has the annoying habit of turning useful zero_extends
  // in the source into any_extends if the result of the mul is truncated. Since
  // we can assume the high bits are whatever we want, use the underlying value
  // to avoid the unknown high bits from interfering.
  if (N0.getOpcode() == ISD::ANY_EXTEND)
    N0 = N0.getOperand(0);

  if (N1.getOpcode() == ISD::ANY_EXTEND)
    N1 = N1.getOperand(0);

  SDValue Mul;

  if (Subtarget->hasMulU24() && isU24(N0, DAG) && isU24(N1, DAG)) {
    N0 = DAG.getZExtOrTrunc(N0, DL, MVT::i32);
    N1 = DAG.getZExtOrTrunc(N1, DL, MVT::i32);
    Mul = getMul24(DAG, DL, N0, N1, Size, false);
  } else if (Subtarget->hasMulI24() && isI24(N0, DAG) && isI24(N1, DAG)) {
    N0 = DAG.getSExtOrTrunc(N0, DL, MVT::i32);
    N1 = DAG.getSExtOrTrunc(N1, DL, MVT::i32);
    Mul = getMul24(DAG, DL, N0, N1, Size, true);
  } else {
    return SDValue();
  }

  // We need to use sext even for MUL_U24, because MUL_U24 is used
  // for signed multiply of 8 and 16-bit types.
  return DAG.getSExtOrTrunc(Mul, DL, VT);
}

SDValue AMDGPUTargetLowering::performMulhsCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  if (!Subtarget->hasMulI24() || VT.isVector())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (!isI24(N0, DAG) || !isI24(N1, DAG))
    return SDValue();

  N0 = DAG.getSExtOrTrunc(N0, DL, MVT::i32);
  N1 = DAG.getSExtOrTrunc(N1, DL, MVT::i32);

  SDValue Mulhi = DAG.getNode(AMDGPUISD::MULHI_I24, DL, MVT::i32, N0, N1);
  DCI.AddToWorklist(Mulhi.getNode());
  return DAG.getSExtOrTrunc(Mulhi, DL, VT);
}

SDValue AMDGPUTargetLowering::performMulhuCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  if (!Subtarget->hasMulU24() || VT.isVector() || VT.getSizeInBits() > 32)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (!isU24(N0, DAG) || !isU24(N1, DAG))
    return SDValue();

  N0 = DAG.getZExtOrTrunc(N0, DL, MVT::i32);
  N1 = DAG.getZExtOrTrunc(N1, DL, MVT::i32);

  SDValue Mulhi = DAG.getNode(AMDGPUISD::MULHI_U24, DL, MVT::i32, N0, N1);
  DCI.AddToWorklist(Mulhi.getNode());
  return DAG.getZExtOrTrunc(Mulhi, DL, VT);
}

SDValue AMDGPUTargetLowering::performMulLoHi24Combine(
  SDNode *N, DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;

  // Simplify demanded bits before splitting into multiple users.
  if (SDValue V = simplifyI24(N, DCI))
    return V;

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  bool Signed = (N->getOpcode() == AMDGPUISD::MUL_LOHI_I24);

  unsigned MulLoOpc = Signed ? AMDGPUISD::MUL_I24 : AMDGPUISD::MUL_U24;
  unsigned MulHiOpc = Signed ? AMDGPUISD::MULHI_I24 : AMDGPUISD::MULHI_U24;

  SDLoc SL(N);

  SDValue MulLo = DAG.getNode(MulLoOpc, SL, MVT::i32, N0, N1);
  SDValue MulHi = DAG.getNode(MulHiOpc, SL, MVT::i32, N0, N1);
  return DAG.getMergeValues({ MulLo, MulHi }, SL);
}

static bool isNegativeOne(SDValue Val) {
  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val))
    return C->isAllOnesValue();
  return false;
}

SDValue AMDGPUTargetLowering::getFFBX_U32(SelectionDAG &DAG,
                                          SDValue Op,
                                          const SDLoc &DL,
                                          unsigned Opc) const {
  EVT VT = Op.getValueType();
  EVT LegalVT = getTypeToTransformTo(*DAG.getContext(), VT);
  if (LegalVT != MVT::i32 && (Subtarget->has16BitInsts() &&
                              LegalVT != MVT::i16))
    return SDValue();

  if (VT != MVT::i32)
    Op = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Op);

  SDValue FFBX = DAG.getNode(Opc, DL, MVT::i32, Op);
  if (VT != MVT::i32)
    FFBX = DAG.getNode(ISD::TRUNCATE, DL, VT, FFBX);

  return FFBX;
}

// The native instructions return -1 on 0 input. Optimize out a select that
// produces -1 on 0.
//
// TODO: If zero is not undef, we could also do this if the output is compared
// against the bitwidth.
//
// TODO: Should probably combine against FFBH_U32 instead of ctlz directly.
SDValue AMDGPUTargetLowering::performCtlz_CttzCombine(const SDLoc &SL, SDValue Cond,
                                                 SDValue LHS, SDValue RHS,
                                                 DAGCombinerInfo &DCI) const {
  ConstantSDNode *CmpRhs = dyn_cast<ConstantSDNode>(Cond.getOperand(1));
  if (!CmpRhs || !CmpRhs->isNullValue())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  ISD::CondCode CCOpcode = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
  SDValue CmpLHS = Cond.getOperand(0);

  unsigned Opc = isCttzOpc(RHS.getOpcode()) ? AMDGPUISD::FFBL_B32 :
                                           AMDGPUISD::FFBH_U32;

  // select (setcc x, 0, eq), -1, (ctlz_zero_undef x) -> ffbh_u32 x
  // select (setcc x, 0, eq), -1, (cttz_zero_undef x) -> ffbl_u32 x
  if (CCOpcode == ISD::SETEQ &&
      (isCtlzOpc(RHS.getOpcode()) || isCttzOpc(RHS.getOpcode())) &&
      RHS.getOperand(0) == CmpLHS &&
      isNegativeOne(LHS)) {
    return getFFBX_U32(DAG, CmpLHS, SL, Opc);
  }

  // select (setcc x, 0, ne), (ctlz_zero_undef x), -1 -> ffbh_u32 x
  // select (setcc x, 0, ne), (cttz_zero_undef x), -1 -> ffbl_u32 x
  if (CCOpcode == ISD::SETNE &&
      (isCtlzOpc(LHS.getOpcode()) || isCttzOpc(RHS.getOpcode())) &&
      LHS.getOperand(0) == CmpLHS &&
      isNegativeOne(RHS)) {
    return getFFBX_U32(DAG, CmpLHS, SL, Opc);
  }

  return SDValue();
}

static SDValue distributeOpThroughSelect(TargetLowering::DAGCombinerInfo &DCI,
                                         unsigned Op,
                                         const SDLoc &SL,
                                         SDValue Cond,
                                         SDValue N1,
                                         SDValue N2) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N1.getValueType();

  SDValue NewSelect = DAG.getNode(ISD::SELECT, SL, VT, Cond,
                                  N1.getOperand(0), N2.getOperand(0));
  DCI.AddToWorklist(NewSelect.getNode());
  return DAG.getNode(Op, SL, VT, NewSelect);
}

// Pull a free FP operation out of a select so it may fold into uses.
//
// select c, (fneg x), (fneg y) -> fneg (select c, x, y)
// select c, (fneg x), k -> fneg (select c, x, (fneg k))
//
// select c, (fabs x), (fabs y) -> fabs (select c, x, y)
// select c, (fabs x), +k -> fabs (select c, x, k)
static SDValue foldFreeOpFromSelect(TargetLowering::DAGCombinerInfo &DCI,
                                    SDValue N) {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Cond = N.getOperand(0);
  SDValue LHS = N.getOperand(1);
  SDValue RHS = N.getOperand(2);

  EVT VT = N.getValueType();
  if ((LHS.getOpcode() == ISD::FABS && RHS.getOpcode() == ISD::FABS) ||
      (LHS.getOpcode() == ISD::FNEG && RHS.getOpcode() == ISD::FNEG)) {
    return distributeOpThroughSelect(DCI, LHS.getOpcode(),
                                     SDLoc(N), Cond, LHS, RHS);
  }

  bool Inv = false;
  if (RHS.getOpcode() == ISD::FABS || RHS.getOpcode() == ISD::FNEG) {
    std::swap(LHS, RHS);
    Inv = true;
  }

  // TODO: Support vector constants.
  ConstantFPSDNode *CRHS = dyn_cast<ConstantFPSDNode>(RHS);
  if ((LHS.getOpcode() == ISD::FNEG || LHS.getOpcode() == ISD::FABS) && CRHS) {
    SDLoc SL(N);
    // If one side is an fneg/fabs and the other is a constant, we can push the
    // fneg/fabs down. If it's an fabs, the constant needs to be non-negative.
    SDValue NewLHS = LHS.getOperand(0);
    SDValue NewRHS = RHS;

    // Careful: if the neg can be folded up, don't try to pull it back down.
    bool ShouldFoldNeg = true;

    if (NewLHS.hasOneUse()) {
      unsigned Opc = NewLHS.getOpcode();
      if (LHS.getOpcode() == ISD::FNEG && fnegFoldsIntoOp(Opc))
        ShouldFoldNeg = false;
      if (LHS.getOpcode() == ISD::FABS && Opc == ISD::FMUL)
        ShouldFoldNeg = false;
    }

    if (ShouldFoldNeg) {
      if (LHS.getOpcode() == ISD::FNEG)
        NewRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
      else if (CRHS->isNegative())
        return SDValue();

      if (Inv)
        std::swap(NewLHS, NewRHS);

      SDValue NewSelect = DAG.getNode(ISD::SELECT, SL, VT,
                                      Cond, NewLHS, NewRHS);
      DCI.AddToWorklist(NewSelect.getNode());
      return DAG.getNode(LHS.getOpcode(), SL, VT, NewSelect);
    }
  }

  return SDValue();
}


SDValue AMDGPUTargetLowering::performSelectCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  if (SDValue Folded = foldFreeOpFromSelect(DCI, SDValue(N, 0)))
    return Folded;

  SDValue Cond = N->getOperand(0);
  if (Cond.getOpcode() != ISD::SETCC)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue LHS = Cond.getOperand(0);
  SDValue RHS = Cond.getOperand(1);
  SDValue CC = Cond.getOperand(2);

  SDValue True = N->getOperand(1);
  SDValue False = N->getOperand(2);

  if (Cond.hasOneUse()) { // TODO: Look for multiple select uses.
    SelectionDAG &DAG = DCI.DAG;
    if ((DAG.isConstantValueOfAnyType(True) ||
         DAG.isConstantValueOfAnyType(True)) &&
        (!DAG.isConstantValueOfAnyType(False) &&
         !DAG.isConstantValueOfAnyType(False))) {
      // Swap cmp + select pair to move constant to false input.
      // This will allow using VOPC cndmasks more often.
      // select (setcc x, y), k, x -> select (setcc y, x) x, x

      SDLoc SL(N);
      ISD::CondCode NewCC = getSetCCInverse(cast<CondCodeSDNode>(CC)->get(),
                                            LHS.getValueType().isInteger());

      SDValue NewCond = DAG.getSetCC(SL, Cond.getValueType(), LHS, RHS, NewCC);
      return DAG.getNode(ISD::SELECT, SL, VT, NewCond, False, True);
    }

    if (VT == MVT::f32 && Subtarget->hasFminFmaxLegacy()) {
      SDValue MinMax
        = combineFMinMaxLegacy(SDLoc(N), VT, LHS, RHS, True, False, CC, DCI);
      // Revisit this node so we can catch min3/max3/med3 patterns.
      //DCI.AddToWorklist(MinMax.getNode());
      return MinMax;
    }
  }

  // There's no reason to not do this if the condition has other uses.
  return performCtlz_CttzCombine(SDLoc(N), Cond, True, False, DCI);
}

static bool isInv2Pi(const APFloat &APF) {
  static const APFloat KF16(APFloat::IEEEhalf(), APInt(16, 0x3118));
  static const APFloat KF32(APFloat::IEEEsingle(), APInt(32, 0x3e22f983));
  static const APFloat KF64(APFloat::IEEEdouble(), APInt(64, 0x3fc45f306dc9c882));

  return APF.bitwiseIsEqual(KF16) ||
         APF.bitwiseIsEqual(KF32) ||
         APF.bitwiseIsEqual(KF64);
}

// 0 and 1.0 / (0.5 * pi) do not have inline immmediates, so there is an
// additional cost to negate them.
bool AMDGPUTargetLowering::isConstantCostlierToNegate(SDValue N) const {
  if (const ConstantFPSDNode *C = isConstOrConstSplatFP(N)) {
    if (C->isZero() && !C->isNegative())
      return true;

    if (Subtarget->hasInv2PiInlineImm() && isInv2Pi(C->getValueAPF()))
      return true;
  }

  return false;
}

static unsigned inverseMinMax(unsigned Opc) {
  switch (Opc) {
  case ISD::FMAXNUM:
    return ISD::FMINNUM;
  case ISD::FMINNUM:
    return ISD::FMAXNUM;
  case ISD::FMAXNUM_IEEE:
    return ISD::FMINNUM_IEEE;
  case ISD::FMINNUM_IEEE:
    return ISD::FMAXNUM_IEEE;
  case AMDGPUISD::FMAX_LEGACY:
    return AMDGPUISD::FMIN_LEGACY;
  case AMDGPUISD::FMIN_LEGACY:
    return  AMDGPUISD::FMAX_LEGACY;
  default:
    llvm_unreachable("invalid min/max opcode");
  }
}

SDValue AMDGPUTargetLowering::performFNegCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  unsigned Opc = N0.getOpcode();

  // If the input has multiple uses and we can either fold the negate down, or
  // the other uses cannot, give up. This both prevents unprofitable
  // transformations and infinite loops: we won't repeatedly try to fold around
  // a negate that has no 'good' form.
  if (N0.hasOneUse()) {
    // This may be able to fold into the source, but at a code size cost. Don't
    // fold if the fold into the user is free.
    if (allUsesHaveSourceMods(N, 0))
      return SDValue();
  } else {
    if (fnegFoldsIntoOp(Opc) &&
        (allUsesHaveSourceMods(N) || !allUsesHaveSourceMods(N0.getNode())))
      return SDValue();
  }

  SDLoc SL(N);
  switch (Opc) {
  case ISD::FADD: {
    if (!mayIgnoreSignedZero(N0))
      return SDValue();

    // (fneg (fadd x, y)) -> (fadd (fneg x), (fneg y))
    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    if (LHS.getOpcode() != ISD::FNEG)
      LHS = DAG.getNode(ISD::FNEG, SL, VT, LHS);
    else
      LHS = LHS.getOperand(0);

    if (RHS.getOpcode() != ISD::FNEG)
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    else
      RHS = RHS.getOperand(0);

    SDValue Res = DAG.getNode(ISD::FADD, SL, VT, LHS, RHS, N0->getFlags());
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMUL:
  case AMDGPUISD::FMUL_LEGACY: {
    // (fneg (fmul x, y)) -> (fmul x, (fneg y))
    // (fneg (fmul_legacy x, y)) -> (fmul_legacy x, (fneg y))
    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    if (LHS.getOpcode() == ISD::FNEG)
      LHS = LHS.getOperand(0);
    else if (RHS.getOpcode() == ISD::FNEG)
      RHS = RHS.getOperand(0);
    else
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);

    SDValue Res = DAG.getNode(Opc, SL, VT, LHS, RHS, N0->getFlags());
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMA:
  case ISD::FMAD: {
    if (!mayIgnoreSignedZero(N0))
      return SDValue();

    // (fneg (fma x, y, z)) -> (fma x, (fneg y), (fneg z))
    SDValue LHS = N0.getOperand(0);
    SDValue MHS = N0.getOperand(1);
    SDValue RHS = N0.getOperand(2);

    if (LHS.getOpcode() == ISD::FNEG)
      LHS = LHS.getOperand(0);
    else if (MHS.getOpcode() == ISD::FNEG)
      MHS = MHS.getOperand(0);
    else
      MHS = DAG.getNode(ISD::FNEG, SL, VT, MHS);

    if (RHS.getOpcode() != ISD::FNEG)
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    else
      RHS = RHS.getOperand(0);

    SDValue Res = DAG.getNode(Opc, SL, VT, LHS, MHS, RHS);
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMAXNUM:
  case ISD::FMINNUM:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINNUM_IEEE:
  case AMDGPUISD::FMAX_LEGACY:
  case AMDGPUISD::FMIN_LEGACY: {
    // fneg (fmaxnum x, y) -> fminnum (fneg x), (fneg y)
    // fneg (fminnum x, y) -> fmaxnum (fneg x), (fneg y)
    // fneg (fmax_legacy x, y) -> fmin_legacy (fneg x), (fneg y)
    // fneg (fmin_legacy x, y) -> fmax_legacy (fneg x), (fneg y)

    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    // 0 doesn't have a negated inline immediate.
    // TODO: This constant check should be generalized to other operations.
    if (isConstantCostlierToNegate(RHS))
      return SDValue();

    SDValue NegLHS = DAG.getNode(ISD::FNEG, SL, VT, LHS);
    SDValue NegRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    unsigned Opposite = inverseMinMax(Opc);

    SDValue Res = DAG.getNode(Opposite, SL, VT, NegLHS, NegRHS, N0->getFlags());
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case AMDGPUISD::FMED3: {
    SDValue Ops[3];
    for (unsigned I = 0; I < 3; ++I)
      Ops[I] = DAG.getNode(ISD::FNEG, SL, VT, N0->getOperand(I), N0->getFlags());

    SDValue Res = DAG.getNode(AMDGPUISD::FMED3, SL, VT, Ops, N0->getFlags());
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FP_EXTEND:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT: // XXX - Should fround be handled?
  case ISD::FSIN:
  case ISD::FCANONICALIZE:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::SIN_HW: {
    SDValue CvtSrc = N0.getOperand(0);
    if (CvtSrc.getOpcode() == ISD::FNEG) {
      // (fneg (fp_extend (fneg x))) -> (fp_extend x)
      // (fneg (rcp (fneg x))) -> (rcp x)
      return DAG.getNode(Opc, SL, VT, CvtSrc.getOperand(0));
    }

    if (!N0.hasOneUse())
      return SDValue();

    // (fneg (fp_extend x)) -> (fp_extend (fneg x))
    // (fneg (rcp x)) -> (rcp (fneg x))
    SDValue Neg = DAG.getNode(ISD::FNEG, SL, CvtSrc.getValueType(), CvtSrc);
    return DAG.getNode(Opc, SL, VT, Neg, N0->getFlags());
  }
  case ISD::FP_ROUND: {
    SDValue CvtSrc = N0.getOperand(0);

    if (CvtSrc.getOpcode() == ISD::FNEG) {
      // (fneg (fp_round (fneg x))) -> (fp_round x)
      return DAG.getNode(ISD::FP_ROUND, SL, VT,
                         CvtSrc.getOperand(0), N0.getOperand(1));
    }

    if (!N0.hasOneUse())
      return SDValue();

    // (fneg (fp_round x)) -> (fp_round (fneg x))
    SDValue Neg = DAG.getNode(ISD::FNEG, SL, CvtSrc.getValueType(), CvtSrc);
    return DAG.getNode(ISD::FP_ROUND, SL, VT, Neg, N0.getOperand(1));
  }
  case ISD::FP16_TO_FP: {
    // v_cvt_f32_f16 supports source modifiers on pre-VI targets without legal
    // f16, but legalization of f16 fneg ends up pulling it out of the source.
    // Put the fneg back as a legal source operation that can be matched later.
    SDLoc SL(N);

    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();

    // fneg (fp16_to_fp x) -> fp16_to_fp (xor x, 0x8000)
    SDValue IntFNeg = DAG.getNode(ISD::XOR, SL, SrcVT, Src,
                                  DAG.getConstant(0x8000, SL, SrcVT));
    return DAG.getNode(ISD::FP16_TO_FP, SL, N->getValueType(0), IntFNeg);
  }
  default:
    return SDValue();
  }
}

SDValue AMDGPUTargetLowering::performFAbsCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);

  if (!N0.hasOneUse())
    return SDValue();

  switch (N0.getOpcode()) {
  case ISD::FP16_TO_FP: {
    assert(!Subtarget->has16BitInsts() && "should only see if f16 is illegal");
    SDLoc SL(N);
    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();

    // fabs (fp16_to_fp x) -> fp16_to_fp (and x, 0x7fff)
    SDValue IntFAbs = DAG.getNode(ISD::AND, SL, SrcVT, Src,
                                  DAG.getConstant(0x7fff, SL, SrcVT));
    return DAG.getNode(ISD::FP16_TO_FP, SL, N->getValueType(0), IntFAbs);
  }
  default:
    return SDValue();
  }
}

SDValue AMDGPUTargetLowering::performRcpCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  const auto *CFP = dyn_cast<ConstantFPSDNode>(N->getOperand(0));
  if (!CFP)
    return SDValue();

  // XXX - Should this flush denormals?
  const APFloat &Val = CFP->getValueAPF();
  APFloat One(Val.getSemantics(), "1.0");
  return DCI.DAG.getConstantFP(One / Val, SDLoc(N), N->getValueType(0));
}

SDValue AMDGPUTargetLowering::PerformDAGCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  switch(N->getOpcode()) {
  default:
    break;
  case ISD::BITCAST: {
    EVT DestVT = N->getValueType(0);

    // Push casts through vector builds. This helps avoid emitting a large
    // number of copies when materializing floating point vector constants.
    //
    // vNt1 bitcast (vNt0 (build_vector t0:x, t0:y)) =>
    //   vnt1 = build_vector (t1 (bitcast t0:x)), (t1 (bitcast t0:y))
    if (DestVT.isVector()) {
      SDValue Src = N->getOperand(0);
      if (Src.getOpcode() == ISD::BUILD_VECTOR) {
        EVT SrcVT = Src.getValueType();
        unsigned NElts = DestVT.getVectorNumElements();

        if (SrcVT.getVectorNumElements() == NElts) {
          EVT DestEltVT = DestVT.getVectorElementType();

          SmallVector<SDValue, 8> CastedElts;
          SDLoc SL(N);
          for (unsigned I = 0, E = SrcVT.getVectorNumElements(); I != E; ++I) {
            SDValue Elt = Src.getOperand(I);
            CastedElts.push_back(DAG.getNode(ISD::BITCAST, DL, DestEltVT, Elt));
          }

          return DAG.getBuildVector(DestVT, SL, CastedElts);
        }
      }
    }

    if (DestVT.getSizeInBits() != 64 && !DestVT.isVector())
      break;

    // Fold bitcasts of constants.
    //
    // v2i32 (bitcast i64:k) -> build_vector lo_32(k), hi_32(k)
    // TODO: Generalize and move to DAGCombiner
    SDValue Src = N->getOperand(0);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Src)) {
      if (Src.getValueType() == MVT::i64) {
        SDLoc SL(N);
        uint64_t CVal = C->getZExtValue();
        SDValue BV = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32,
                                 DAG.getConstant(Lo_32(CVal), SL, MVT::i32),
                                 DAG.getConstant(Hi_32(CVal), SL, MVT::i32));
        return DAG.getNode(ISD::BITCAST, SL, DestVT, BV);
      }
    }

    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Src)) {
      const APInt &Val = C->getValueAPF().bitcastToAPInt();
      SDLoc SL(N);
      uint64_t CVal = Val.getZExtValue();
      SDValue Vec = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32,
                                DAG.getConstant(Lo_32(CVal), SL, MVT::i32),
                                DAG.getConstant(Hi_32(CVal), SL, MVT::i32));

      return DAG.getNode(ISD::BITCAST, SL, DestVT, Vec);
    }

    break;
  }
  case ISD::SHL: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performShlCombine(N, DCI);
  }
  case ISD::SRL: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performSrlCombine(N, DCI);
  }
  case ISD::SRA: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performSraCombine(N, DCI);
  }
  case ISD::TRUNCATE:
    return performTruncateCombine(N, DCI);
  case ISD::MUL:
    return performMulCombine(N, DCI);
  case ISD::MULHS:
    return performMulhsCombine(N, DCI);
  case ISD::MULHU:
    return performMulhuCombine(N, DCI);
  case AMDGPUISD::MUL_I24:
  case AMDGPUISD::MUL_U24:
  case AMDGPUISD::MULHI_I24:
  case AMDGPUISD::MULHI_U24: {
    if (SDValue V = simplifyI24(N, DCI))
      return V;
    return SDValue();
  }
  case AMDGPUISD::MUL_LOHI_I24:
  case AMDGPUISD::MUL_LOHI_U24:
    return performMulLoHi24Combine(N, DCI);
  case ISD::SELECT:
    return performSelectCombine(N, DCI);
  case ISD::FNEG:
    return performFNegCombine(N, DCI);
  case ISD::FABS:
    return performFAbsCombine(N, DCI);
  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    assert(!N->getValueType(0).isVector() &&
           "Vector handling of BFE not implemented");
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(N->getOperand(2));
    if (!Width)
      break;

    uint32_t WidthVal = Width->getZExtValue() & 0x1f;
    if (WidthVal == 0)
      return DAG.getConstant(0, DL, MVT::i32);

    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!Offset)
      break;

    SDValue BitsFrom = N->getOperand(0);
    uint32_t OffsetVal = Offset->getZExtValue() & 0x1f;

    bool Signed = N->getOpcode() == AMDGPUISD::BFE_I32;

    if (OffsetVal == 0) {
      // This is already sign / zero extended, so try to fold away extra BFEs.
      unsigned SignBits =  Signed ? (32 - WidthVal + 1) : (32 - WidthVal);

      unsigned OpSignBits = DAG.ComputeNumSignBits(BitsFrom);
      if (OpSignBits >= SignBits)
        return BitsFrom;

      EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), WidthVal);
      if (Signed) {
        // This is a sign_extend_inreg. Replace it to take advantage of existing
        // DAG Combines. If not eliminated, we will match back to BFE during
        // selection.

        // TODO: The sext_inreg of extended types ends, although we can could
        // handle them in a single BFE.
        return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i32, BitsFrom,
                           DAG.getValueType(SmallVT));
      }

      return DAG.getZeroExtendInReg(BitsFrom, DL, SmallVT);
    }

    if (ConstantSDNode *CVal = dyn_cast<ConstantSDNode>(BitsFrom)) {
      if (Signed) {
        return constantFoldBFE<int32_t>(DAG,
                                        CVal->getSExtValue(),
                                        OffsetVal,
                                        WidthVal,
                                        DL);
      }

      return constantFoldBFE<uint32_t>(DAG,
                                       CVal->getZExtValue(),
                                       OffsetVal,
                                       WidthVal,
                                       DL);
    }

    if ((OffsetVal + WidthVal) >= 32 &&
        !(Subtarget->hasSDWA() && OffsetVal == 16 && WidthVal == 16)) {
      SDValue ShiftVal = DAG.getConstant(OffsetVal, DL, MVT::i32);
      return DAG.getNode(Signed ? ISD::SRA : ISD::SRL, DL, MVT::i32,
                         BitsFrom, ShiftVal);
    }

    if (BitsFrom.hasOneUse()) {
      APInt Demanded = APInt::getBitsSet(32,
                                         OffsetVal,
                                         OffsetVal + WidthVal);

      KnownBits Known;
      TargetLowering::TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                                            !DCI.isBeforeLegalizeOps());
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
      if (TLI.ShrinkDemandedConstant(BitsFrom, Demanded, TLO) ||
          TLI.SimplifyDemandedBits(BitsFrom, Demanded, Known, TLO)) {
        DCI.CommitTargetLoweringOpt(TLO);
      }
    }

    break;
  }
  case ISD::LOAD:
    return performLoadCombine(N, DCI);
  case ISD::STORE:
    return performStoreCombine(N, DCI);
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_IFLAG:
    return performRcpCombine(N, DCI);
  case ISD::AssertZext:
  case ISD::AssertSext:
    return performAssertSZExtCombine(N, DCI);
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//

SDValue AMDGPUTargetLowering::CreateLiveInRegister(SelectionDAG &DAG,
                                                   const TargetRegisterClass *RC,
                                                   unsigned Reg, EVT VT,
                                                   const SDLoc &SL,
                                                   bool RawReg) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned VReg;

  if (!MRI.isLiveIn(Reg)) {
    VReg = MRI.createVirtualRegister(RC);
    MRI.addLiveIn(Reg, VReg);
  } else {
    VReg = MRI.getLiveInVirtReg(Reg);
  }

  if (RawReg)
    return DAG.getRegister(VReg, VT);

  return DAG.getCopyFromReg(DAG.getEntryNode(), SL, VReg, VT);
}

SDValue AMDGPUTargetLowering::loadStackInputValue(SelectionDAG &DAG,
                                                  EVT VT,
                                                  const SDLoc &SL,
                                                  int64_t Offset) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  int FI = MFI.CreateFixedObject(VT.getStoreSize(), Offset, true);
  auto SrcPtrInfo = MachinePointerInfo::getStack(MF, Offset);
  SDValue Ptr = DAG.getFrameIndex(FI, MVT::i32);

  return DAG.getLoad(VT, SL, DAG.getEntryNode(), Ptr, SrcPtrInfo, 4,
                     MachineMemOperand::MODereferenceable |
                     MachineMemOperand::MOInvariant);
}

SDValue AMDGPUTargetLowering::storeStackInputValue(SelectionDAG &DAG,
                                                   const SDLoc &SL,
                                                   SDValue Chain,
                                                   SDValue ArgVal,
                                                   int64_t Offset) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachinePointerInfo DstInfo = MachinePointerInfo::getStack(MF, Offset);

  SDValue Ptr = DAG.getConstant(Offset, SL, MVT::i32);
  SDValue Store = DAG.getStore(Chain, SL, ArgVal, Ptr, DstInfo, 4,
                               MachineMemOperand::MODereferenceable);
  return Store;
}

SDValue AMDGPUTargetLowering::loadInputValue(SelectionDAG &DAG,
                                             const TargetRegisterClass *RC,
                                             EVT VT, const SDLoc &SL,
                                             const ArgDescriptor &Arg) const {
  assert(Arg && "Attempting to load missing argument");

  if (Arg.isRegister())
    return CreateLiveInRegister(DAG, RC, Arg.getRegister(), VT, SL);
  return loadStackInputValue(DAG, VT, SL, Arg.getStackOffset());
}

uint32_t AMDGPUTargetLowering::getImplicitParameterOffset(
    const MachineFunction &MF, const ImplicitParameter Param) const {
  const AMDGPUMachineFunction *MFI = MF.getInfo<AMDGPUMachineFunction>();
  const AMDGPUSubtarget &ST =
      AMDGPUSubtarget::get(getTargetMachine(), MF.getFunction());
  unsigned ExplicitArgOffset = ST.getExplicitKernelArgOffset(MF.getFunction());
  unsigned Alignment = ST.getAlignmentForImplicitArgPtr();
  uint64_t ArgOffset = alignTo(MFI->getExplicitKernArgSize(), Alignment) +
                       ExplicitArgOffset;
  switch (Param) {
  case GRID_DIM:
    return ArgOffset;
  case GRID_OFFSET:
    return ArgOffset + 4;
  }
  llvm_unreachable("unexpected implicit parameter type");
}

#define NODE_NAME_CASE(node) case AMDGPUISD::node: return #node;

const char* AMDGPUTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((AMDGPUISD::NodeType)Opcode) {
  case AMDGPUISD::FIRST_NUMBER: break;
  // AMDIL DAG nodes
  NODE_NAME_CASE(UMUL);
  NODE_NAME_CASE(BRANCH_COND);

  // AMDGPU DAG nodes
  NODE_NAME_CASE(IF)
  NODE_NAME_CASE(ELSE)
  NODE_NAME_CASE(LOOP)
  NODE_NAME_CASE(CALL)
  NODE_NAME_CASE(TC_RETURN)
  NODE_NAME_CASE(TRAP)
  NODE_NAME_CASE(RET_FLAG)
  NODE_NAME_CASE(RETURN_TO_EPILOG)
  NODE_NAME_CASE(ENDPGM)
  NODE_NAME_CASE(DWORDADDR)
  NODE_NAME_CASE(FRACT)
  NODE_NAME_CASE(SETCC)
  NODE_NAME_CASE(SETREG)
  NODE_NAME_CASE(FMA_W_CHAIN)
  NODE_NAME_CASE(FMUL_W_CHAIN)
  NODE_NAME_CASE(CLAMP)
  NODE_NAME_CASE(COS_HW)
  NODE_NAME_CASE(SIN_HW)
  NODE_NAME_CASE(FMAX_LEGACY)
  NODE_NAME_CASE(FMIN_LEGACY)
  NODE_NAME_CASE(FMAX3)
  NODE_NAME_CASE(SMAX3)
  NODE_NAME_CASE(UMAX3)
  NODE_NAME_CASE(FMIN3)
  NODE_NAME_CASE(SMIN3)
  NODE_NAME_CASE(UMIN3)
  NODE_NAME_CASE(FMED3)
  NODE_NAME_CASE(SMED3)
  NODE_NAME_CASE(UMED3)
  NODE_NAME_CASE(FDOT2)
  NODE_NAME_CASE(URECIP)
  NODE_NAME_CASE(DIV_SCALE)
  NODE_NAME_CASE(DIV_FMAS)
  NODE_NAME_CASE(DIV_FIXUP)
  NODE_NAME_CASE(FMAD_FTZ)
  NODE_NAME_CASE(TRIG_PREOP)
  NODE_NAME_CASE(RCP)
  NODE_NAME_CASE(RSQ)
  NODE_NAME_CASE(RCP_LEGACY)
  NODE_NAME_CASE(RSQ_LEGACY)
  NODE_NAME_CASE(RCP_IFLAG)
  NODE_NAME_CASE(FMUL_LEGACY)
  NODE_NAME_CASE(RSQ_CLAMP)
  NODE_NAME_CASE(LDEXP)
  NODE_NAME_CASE(FP_CLASS)
  NODE_NAME_CASE(DOT4)
  NODE_NAME_CASE(CARRY)
  NODE_NAME_CASE(BORROW)
  NODE_NAME_CASE(BFE_U32)
  NODE_NAME_CASE(BFE_I32)
  NODE_NAME_CASE(BFI)
  NODE_NAME_CASE(BFM)
  NODE_NAME_CASE(FFBH_U32)
  NODE_NAME_CASE(FFBH_I32)
  NODE_NAME_CASE(FFBL_B32)
  NODE_NAME_CASE(MUL_U24)
  NODE_NAME_CASE(MUL_I24)
  NODE_NAME_CASE(MULHI_U24)
  NODE_NAME_CASE(MULHI_I24)
  NODE_NAME_CASE(MUL_LOHI_U24)
  NODE_NAME_CASE(MUL_LOHI_I24)
  NODE_NAME_CASE(MAD_U24)
  NODE_NAME_CASE(MAD_I24)
  NODE_NAME_CASE(MAD_I64_I32)
  NODE_NAME_CASE(MAD_U64_U32)
  NODE_NAME_CASE(PERM)
  NODE_NAME_CASE(TEXTURE_FETCH)
  NODE_NAME_CASE(EXPORT)
  NODE_NAME_CASE(EXPORT_DONE)
  NODE_NAME_CASE(R600_EXPORT)
  NODE_NAME_CASE(CONST_ADDRESS)
  NODE_NAME_CASE(REGISTER_LOAD)
  NODE_NAME_CASE(REGISTER_STORE)
  NODE_NAME_CASE(SAMPLE)
  NODE_NAME_CASE(SAMPLEB)
  NODE_NAME_CASE(SAMPLED)
  NODE_NAME_CASE(SAMPLEL)
  NODE_NAME_CASE(CVT_F32_UBYTE0)
  NODE_NAME_CASE(CVT_F32_UBYTE1)
  NODE_NAME_CASE(CVT_F32_UBYTE2)
  NODE_NAME_CASE(CVT_F32_UBYTE3)
  NODE_NAME_CASE(CVT_PKRTZ_F16_F32)
  NODE_NAME_CASE(CVT_PKNORM_I16_F32)
  NODE_NAME_CASE(CVT_PKNORM_U16_F32)
  NODE_NAME_CASE(CVT_PK_I16_I32)
  NODE_NAME_CASE(CVT_PK_U16_U32)
  NODE_NAME_CASE(FP_TO_FP16)
  NODE_NAME_CASE(FP16_ZEXT)
  NODE_NAME_CASE(BUILD_VERTICAL_VECTOR)
  NODE_NAME_CASE(CONST_DATA_PTR)
  NODE_NAME_CASE(PC_ADD_REL_OFFSET)
  NODE_NAME_CASE(KILL)
  NODE_NAME_CASE(DUMMY_CHAIN)
  case AMDGPUISD::FIRST_MEM_OPCODE_NUMBER: break;
  NODE_NAME_CASE(INIT_EXEC)
  NODE_NAME_CASE(INIT_EXEC_FROM_INPUT)
  NODE_NAME_CASE(SENDMSG)
  NODE_NAME_CASE(SENDMSGHALT)
  NODE_NAME_CASE(INTERP_MOV)
  NODE_NAME_CASE(INTERP_P1)
  NODE_NAME_CASE(INTERP_P2)
  NODE_NAME_CASE(STORE_MSKOR)
  NODE_NAME_CASE(LOAD_CONSTANT)
  NODE_NAME_CASE(TBUFFER_STORE_FORMAT)
  NODE_NAME_CASE(TBUFFER_STORE_FORMAT_X3)
  NODE_NAME_CASE(TBUFFER_STORE_FORMAT_D16)
  NODE_NAME_CASE(TBUFFER_LOAD_FORMAT)
  NODE_NAME_CASE(TBUFFER_LOAD_FORMAT_D16)
  NODE_NAME_CASE(DS_ORDERED_COUNT)
  NODE_NAME_CASE(ATOMIC_CMP_SWAP)
  NODE_NAME_CASE(ATOMIC_INC)
  NODE_NAME_CASE(ATOMIC_DEC)
  NODE_NAME_CASE(ATOMIC_LOAD_FADD)
  NODE_NAME_CASE(ATOMIC_LOAD_FMIN)
  NODE_NAME_CASE(ATOMIC_LOAD_FMAX)
  NODE_NAME_CASE(BUFFER_LOAD)
  NODE_NAME_CASE(BUFFER_LOAD_FORMAT)
  NODE_NAME_CASE(BUFFER_LOAD_FORMAT_D16)
  NODE_NAME_CASE(SBUFFER_LOAD)
  NODE_NAME_CASE(BUFFER_STORE)
  NODE_NAME_CASE(BUFFER_STORE_FORMAT)
  NODE_NAME_CASE(BUFFER_STORE_FORMAT_D16)
  NODE_NAME_CASE(BUFFER_ATOMIC_SWAP)
  NODE_NAME_CASE(BUFFER_ATOMIC_ADD)
  NODE_NAME_CASE(BUFFER_ATOMIC_SUB)
  NODE_NAME_CASE(BUFFER_ATOMIC_SMIN)
  NODE_NAME_CASE(BUFFER_ATOMIC_UMIN)
  NODE_NAME_CASE(BUFFER_ATOMIC_SMAX)
  NODE_NAME_CASE(BUFFER_ATOMIC_UMAX)
  NODE_NAME_CASE(BUFFER_ATOMIC_AND)
  NODE_NAME_CASE(BUFFER_ATOMIC_OR)
  NODE_NAME_CASE(BUFFER_ATOMIC_XOR)
  NODE_NAME_CASE(BUFFER_ATOMIC_CMPSWAP)

  case AMDGPUISD::LAST_AMDGPU_ISD_NUMBER: break;
  }
  return nullptr;
}

SDValue AMDGPUTargetLowering::getSqrtEstimate(SDValue Operand,
                                              SelectionDAG &DAG, int Enabled,
                                              int &RefinementSteps,
                                              bool &UseOneConstNR,
                                              bool Reciprocal) const {
  EVT VT = Operand.getValueType();

  if (VT == MVT::f32) {
    RefinementSteps = 0;
    return DAG.getNode(AMDGPUISD::RSQ, SDLoc(Operand), VT, Operand);
  }

  // TODO: There is also f64 rsq instruction, but the documentation is less
  // clear on its precision.

  return SDValue();
}

SDValue AMDGPUTargetLowering::getRecipEstimate(SDValue Operand,
                                               SelectionDAG &DAG, int Enabled,
                                               int &RefinementSteps) const {
  EVT VT = Operand.getValueType();

  if (VT == MVT::f32) {
    // Reciprocal, < 1 ulp error.
    //
    // This reciprocal approximation converges to < 0.5 ulp error with one
    // newton rhapson performed with two fused multiple adds (FMAs).

    RefinementSteps = 0;
    return DAG.getNode(AMDGPUISD::RCP, SDLoc(Operand), VT, Operand);
  }

  // TODO: There is also f64 rcp instruction, but the documentation is less
  // clear on its precision.

  return SDValue();
}

void AMDGPUTargetLowering::computeKnownBitsForTargetNode(
    const SDValue Op, KnownBits &Known,
    const APInt &DemandedElts, const SelectionDAG &DAG, unsigned Depth) const {

  Known.resetAll(); // Don't know anything.

  unsigned Opc = Op.getOpcode();

  switch (Opc) {
  default:
    break;
  case AMDGPUISD::CARRY:
  case AMDGPUISD::BORROW: {
    Known.Zero = APInt::getHighBitsSet(32, 31);
    break;
  }

  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    ConstantSDNode *CWidth = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!CWidth)
      return;

    uint32_t Width = CWidth->getZExtValue() & 0x1f;

    if (Opc == AMDGPUISD::BFE_U32)
      Known.Zero = APInt::getHighBitsSet(32, 32 - Width);

    break;
  }
  case AMDGPUISD::FP_TO_FP16:
  case AMDGPUISD::FP16_ZEXT: {
    unsigned BitWidth = Known.getBitWidth();

    // High bits are zero.
    Known.Zero = APInt::getHighBitsSet(BitWidth, BitWidth - 16);
    break;
  }
  case AMDGPUISD::MUL_U24:
  case AMDGPUISD::MUL_I24: {
    KnownBits LHSKnown = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    KnownBits RHSKnown = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    unsigned TrailZ = LHSKnown.countMinTrailingZeros() +
                      RHSKnown.countMinTrailingZeros();
    Known.Zero.setLowBits(std::min(TrailZ, 32u));

    // Truncate to 24 bits.
    LHSKnown = LHSKnown.trunc(24);
    RHSKnown = RHSKnown.trunc(24);

    bool Negative = false;
    if (Opc == AMDGPUISD::MUL_I24) {
      unsigned LHSValBits = 24 - LHSKnown.countMinSignBits();
      unsigned RHSValBits = 24 - RHSKnown.countMinSignBits();
      unsigned MaxValBits = std::min(LHSValBits + RHSValBits, 32u);
      if (MaxValBits >= 32)
        break;
      bool LHSNegative = LHSKnown.isNegative();
      bool LHSPositive = LHSKnown.isNonNegative();
      bool RHSNegative = RHSKnown.isNegative();
      bool RHSPositive = RHSKnown.isNonNegative();
      if ((!LHSNegative && !LHSPositive) || (!RHSNegative && !RHSPositive))
        break;
      Negative = (LHSNegative && RHSPositive) || (LHSPositive && RHSNegative);
      if (Negative)
        Known.One.setHighBits(32 - MaxValBits);
      else
        Known.Zero.setHighBits(32 - MaxValBits);
    } else {
      unsigned LHSValBits = 24 - LHSKnown.countMinLeadingZeros();
      unsigned RHSValBits = 24 - RHSKnown.countMinLeadingZeros();
      unsigned MaxValBits = std::min(LHSValBits + RHSValBits, 32u);
      if (MaxValBits >= 32)
        break;
      Known.Zero.setHighBits(32 - MaxValBits);
    }
    break;
  }
  case AMDGPUISD::PERM: {
    ConstantSDNode *CMask = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!CMask)
      return;

    KnownBits LHSKnown = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    KnownBits RHSKnown = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    unsigned Sel = CMask->getZExtValue();

    for (unsigned I = 0; I < 32; I += 8) {
      unsigned SelBits = Sel & 0xff;
      if (SelBits < 4) {
        SelBits *= 8;
        Known.One |= ((RHSKnown.One.getZExtValue() >> SelBits) & 0xff) << I;
        Known.Zero |= ((RHSKnown.Zero.getZExtValue() >> SelBits) & 0xff) << I;
      } else if (SelBits < 7) {
        SelBits = (SelBits & 3) * 8;
        Known.One |= ((LHSKnown.One.getZExtValue() >> SelBits) & 0xff) << I;
        Known.Zero |= ((LHSKnown.Zero.getZExtValue() >> SelBits) & 0xff) << I;
      } else if (SelBits == 0x0c) {
        Known.Zero |= 0xff << I;
      } else if (SelBits > 0x0c) {
        Known.One |= 0xff << I;
      }
      Sel >>= 8;
    }
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IID = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
    switch (IID) {
    case Intrinsic::amdgcn_mbcnt_lo:
    case Intrinsic::amdgcn_mbcnt_hi: {
      const GCNSubtarget &ST =
          DAG.getMachineFunction().getSubtarget<GCNSubtarget>();
      // These return at most the wavefront size - 1.
      unsigned Size = Op.getValueType().getSizeInBits();
      Known.Zero.setHighBits(Size - ST.getWavefrontSizeLog2());
      break;
    }
    default:
      break;
    }
  }
  }
}

unsigned AMDGPUTargetLowering::ComputeNumSignBitsForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    unsigned Depth) const {
  switch (Op.getOpcode()) {
  case AMDGPUISD::BFE_I32: {
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!Width)
      return 1;

    unsigned SignBits = 32 - Width->getZExtValue() + 1;
    if (!isNullConstant(Op.getOperand(1)))
      return SignBits;

    // TODO: Could probably figure something out with non-0 offsets.
    unsigned Op0SignBits = DAG.ComputeNumSignBits(Op.getOperand(0), Depth + 1);
    return std::max(SignBits, Op0SignBits);
  }

  case AMDGPUISD::BFE_U32: {
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    return Width ? 32 - (Width->getZExtValue() & 0x1f) : 1;
  }

  case AMDGPUISD::CARRY:
  case AMDGPUISD::BORROW:
    return 31;
  case AMDGPUISD::FP_TO_FP16:
  case AMDGPUISD::FP16_ZEXT:
    return 16;
  default:
    return 1;
  }
}

bool AMDGPUTargetLowering::isKnownNeverNaNForTargetNode(SDValue Op,
                                                        const SelectionDAG &DAG,
                                                        bool SNaN,
                                                        unsigned Depth) const {
  unsigned Opcode = Op.getOpcode();
  switch (Opcode) {
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY: {
    if (SNaN)
      return true;

    // TODO: Can check no nans on one of the operands for each one, but which
    // one?
    return false;
  }
  case AMDGPUISD::FMUL_LEGACY:
  case AMDGPUISD::CVT_PKRTZ_F16_F32: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1);
  }
  case AMDGPUISD::FMED3:
  case AMDGPUISD::FMIN3:
  case AMDGPUISD::FMAX3:
  case AMDGPUISD::FMAD_FTZ: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(2), SNaN, Depth + 1);
  }
  case AMDGPUISD::CVT_F32_UBYTE0:
  case AMDGPUISD::CVT_F32_UBYTE1:
  case AMDGPUISD::CVT_F32_UBYTE2:
  case AMDGPUISD::CVT_F32_UBYTE3:
    return true;

  case AMDGPUISD::RCP:
  case AMDGPUISD::RSQ:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RSQ_LEGACY:
  case AMDGPUISD::RSQ_CLAMP: {
    if (SNaN)
      return true;

    // TODO: Need is known positive check.
    return false;
  }
  case AMDGPUISD::LDEXP:
  case AMDGPUISD::FRACT: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1);
  }
  case AMDGPUISD::DIV_SCALE:
  case AMDGPUISD::DIV_FMAS:
  case AMDGPUISD::DIV_FIXUP:
  case AMDGPUISD::TRIG_PREOP:
    // TODO: Refine on operands.
    return SNaN;
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::COS_HW: {
    // TODO: Need check for infinity
    return SNaN;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrinsicID
      = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
    // TODO: Handle more intrinsics
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_cubeid:
      return true;

    case Intrinsic::amdgcn_frexp_mant: {
      if (SNaN)
        return true;
      return DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1);
    }
    case Intrinsic::amdgcn_cvt_pkrtz: {
      if (SNaN)
        return true;
      return DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1) &&
             DAG.isKnownNeverNaN(Op.getOperand(2), SNaN, Depth + 1);
    }
    case Intrinsic::amdgcn_fdot2:
      // TODO: Refine on operand
      return SNaN;
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

TargetLowering::AtomicExpansionKind
AMDGPUTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  if (RMW->getOperation() == AtomicRMWInst::Nand)
    return AtomicExpansionKind::CmpXChg;
  return AtomicExpansionKind::None;
}
