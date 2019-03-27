//===-- AArch64TargetTransformInfo.cpp - AArch64 specific TTI -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetTransformInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "aarch64tti"

static cl::opt<bool> EnableFalkorHWPFUnrollFix("enable-falkor-hwpf-unroll-fix",
                                               cl::init(true), cl::Hidden);

bool AArch64TTIImpl::areInlineCompatible(const Function *Caller,
                                         const Function *Callee) const {
  const TargetMachine &TM = getTLI()->getTargetMachine();

  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  // Inline a callee if its target-features are a subset of the callers
  // target-features.
  return (CallerBits & CalleeBits) == CalleeBits;
}

/// Calculate the cost of materializing a 64-bit value. This helper
/// method might only calculate a fraction of a larger immediate. Therefore it
/// is valid to return a cost of ZERO.
int AArch64TTIImpl::getIntImmCost(int64_t Val) {
  // Check if the immediate can be encoded within an instruction.
  if (Val == 0 || AArch64_AM::isLogicalImmediate(Val, 64))
    return 0;

  if (Val < 0)
    Val = ~Val;

  // Calculate how many moves we will need to materialize this constant.
  unsigned LZ = countLeadingZeros((uint64_t)Val);
  return (64 - LZ + 15) / 16;
}

/// Calculate the cost of materializing the given constant.
int AArch64TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  // Sign-extend all constants to a multiple of 64-bit.
  APInt ImmVal = Imm;
  if (BitSize & 0x3f)
    ImmVal = Imm.sext((BitSize + 63) & ~0x3fU);

  // Split the constant into 64-bit chunks and calculate the cost for each
  // chunk.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < BitSize; ShiftVal += 64) {
    APInt Tmp = ImmVal.ashr(ShiftVal).sextOrTrunc(64);
    int64_t Val = Tmp.getSExtValue();
    Cost += getIntImmCost(Val);
  }
  // We need at least one instruction to materialze the constant.
  return std::max(1, Cost);
}

int AArch64TTIImpl::getIntImmCost(unsigned Opcode, unsigned Idx,
                                  const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;

  unsigned ImmIdx = ~0U;
  switch (Opcode) {
  default:
    return TTI::TCC_Free;
  case Instruction::GetElementPtr:
    // Always hoist the base address of a GetElementPtr.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Store:
    ImmIdx = 0;
    break;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::ICmp:
    ImmIdx = 1;
    break;
  // Always return TCC_Free for the shift value of a shift instruction.
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (Idx == 1)
      return TTI::TCC_Free;
    break;
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
  case Instruction::BitCast:
  case Instruction::PHI:
  case Instruction::Call:
  case Instruction::Select:
  case Instruction::Ret:
  case Instruction::Load:
    break;
  }

  if (Idx == ImmIdx) {
    int NumConstants = (BitSize + 63) / 64;
    int Cost = AArch64TTIImpl::getIntImmCost(Imm, Ty);
    return (Cost <= NumConstants * TTI::TCC_Basic)
               ? static_cast<int>(TTI::TCC_Free)
               : Cost;
  }
  return AArch64TTIImpl::getIntImmCost(Imm, Ty);
}

int AArch64TTIImpl::getIntImmCost(Intrinsic::ID IID, unsigned Idx,
                                  const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow:
    if (Idx == 1) {
      int NumConstants = (BitSize + 63) / 64;
      int Cost = AArch64TTIImpl::getIntImmCost(Imm, Ty);
      return (Cost <= NumConstants * TTI::TCC_Basic)
                 ? static_cast<int>(TTI::TCC_Free)
                 : Cost;
    }
    break;
  case Intrinsic::experimental_stackmap:
    if ((Idx < 2) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint_i64:
    if ((Idx < 4) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  }
  return AArch64TTIImpl::getIntImmCost(Imm, Ty);
}

TargetTransformInfo::PopcntSupportKind
AArch64TTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  if (TyWidth == 32 || TyWidth == 64)
    return TTI::PSK_FastHardware;
  // TODO: AArch64TargetLowering::LowerCTPOP() supports 128bit popcount.
  return TTI::PSK_Software;
}

bool AArch64TTIImpl::isWideningInstruction(Type *DstTy, unsigned Opcode,
                                           ArrayRef<const Value *> Args) {

  // A helper that returns a vector type from the given type. The number of
  // elements in type Ty determine the vector width.
  auto toVectorTy = [&](Type *ArgTy) {
    return VectorType::get(ArgTy->getScalarType(),
                           DstTy->getVectorNumElements());
  };

  // Exit early if DstTy is not a vector type whose elements are at least
  // 16-bits wide.
  if (!DstTy->isVectorTy() || DstTy->getScalarSizeInBits() < 16)
    return false;

  // Determine if the operation has a widening variant. We consider both the
  // "long" (e.g., usubl) and "wide" (e.g., usubw) versions of the
  // instructions.
  //
  // TODO: Add additional widening operations (e.g., mul, shl, etc.) once we
  //       verify that their extending operands are eliminated during code
  //       generation.
  switch (Opcode) {
  case Instruction::Add: // UADDL(2), SADDL(2), UADDW(2), SADDW(2).
  case Instruction::Sub: // USUBL(2), SSUBL(2), USUBW(2), SSUBW(2).
    break;
  default:
    return false;
  }

  // To be a widening instruction (either the "wide" or "long" versions), the
  // second operand must be a sign- or zero extend having a single user. We
  // only consider extends having a single user because they may otherwise not
  // be eliminated.
  if (Args.size() != 2 ||
      (!isa<SExtInst>(Args[1]) && !isa<ZExtInst>(Args[1])) ||
      !Args[1]->hasOneUse())
    return false;
  auto *Extend = cast<CastInst>(Args[1]);

  // Legalize the destination type and ensure it can be used in a widening
  // operation.
  auto DstTyL = TLI->getTypeLegalizationCost(DL, DstTy);
  unsigned DstElTySize = DstTyL.second.getScalarSizeInBits();
  if (!DstTyL.second.isVector() || DstElTySize != DstTy->getScalarSizeInBits())
    return false;

  // Legalize the source type and ensure it can be used in a widening
  // operation.
  Type *SrcTy = toVectorTy(Extend->getSrcTy());
  auto SrcTyL = TLI->getTypeLegalizationCost(DL, SrcTy);
  unsigned SrcElTySize = SrcTyL.second.getScalarSizeInBits();
  if (!SrcTyL.second.isVector() || SrcElTySize != SrcTy->getScalarSizeInBits())
    return false;

  // Get the total number of vector elements in the legalized types.
  unsigned NumDstEls = DstTyL.first * DstTyL.second.getVectorNumElements();
  unsigned NumSrcEls = SrcTyL.first * SrcTyL.second.getVectorNumElements();

  // Return true if the legalized types have the same number of vector elements
  // and the destination element type size is twice that of the source type.
  return NumDstEls == NumSrcEls && 2 * SrcElTySize == DstElTySize;
}

int AArch64TTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                     const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // If the cast is observable, and it is used by a widening instruction (e.g.,
  // uaddl, saddw, etc.), it may be free.
  if (I && I->hasOneUse()) {
    auto *SingleUser = cast<Instruction>(*I->user_begin());
    SmallVector<const Value *, 4> Operands(SingleUser->operand_values());
    if (isWideningInstruction(Dst, SingleUser->getOpcode(), Operands)) {
      // If the cast is the second operand, it is free. We will generate either
      // a "wide" or "long" version of the widening instruction.
      if (I == SingleUser->getOperand(1))
        return 0;
      // If the cast is not the second operand, it will be free if it looks the
      // same as the second operand. In this case, we will generate a "long"
      // version of the widening instruction.
      if (auto *Cast = dyn_cast<CastInst>(SingleUser->getOperand(1)))
        if (I->getOpcode() == unsigned(Cast->getOpcode()) &&
            cast<CastInst>(I)->getSrcTy() == Cast->getSrcTy())
          return 0;
    }
  }

  EVT SrcTy = TLI->getValueType(DL, Src);
  EVT DstTy = TLI->getValueType(DL, Dst);

  if (!SrcTy.isSimple() || !DstTy.isSimple())
    return BaseT::getCastInstrCost(Opcode, Dst, Src);

  static const TypeConversionCostTblEntry
  ConversionTbl[] = {
    { ISD::TRUNCATE, MVT::v4i16, MVT::v4i32,  1 },
    { ISD::TRUNCATE, MVT::v4i32, MVT::v4i64,  0 },
    { ISD::TRUNCATE, MVT::v8i8,  MVT::v8i32,  3 },
    { ISD::TRUNCATE, MVT::v16i8, MVT::v16i32, 6 },

    // The number of shll instructions for the extension.
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i16, 3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i16, 3 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32, 2 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32, 2 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,  3 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,  3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16, 2 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16, 2 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i8,  7 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i8,  7 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i16, 6 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i16, 6 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8, 2 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8, 2 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8, 6 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8, 6 },

    // LowerVectorINT_TO_FP:
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i32, 1 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i32, 1 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i64, 1 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i32, 1 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i32, 1 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i64, 1 },

    // Complex: to v2f32
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i8,  3 },
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i16, 3 },
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i64, 2 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i8,  3 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i16, 3 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i64, 2 },

    // Complex: to v4f32
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i8,  4 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i8,  3 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i16, 2 },

    // Complex: to v8f32
    { ISD::SINT_TO_FP, MVT::v8f32, MVT::v8i8,  10 },
    { ISD::SINT_TO_FP, MVT::v8f32, MVT::v8i16, 4 },
    { ISD::UINT_TO_FP, MVT::v8f32, MVT::v8i8,  10 },
    { ISD::UINT_TO_FP, MVT::v8f32, MVT::v8i16, 4 },

    // Complex: to v16f32
    { ISD::SINT_TO_FP, MVT::v16f32, MVT::v16i8, 21 },
    { ISD::UINT_TO_FP, MVT::v16f32, MVT::v16i8, 21 },

    // Complex: to v2f64
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i8,  4 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i16, 4 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i8,  4 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i16, 4 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i32, 2 },


    // LowerVectorFP_TO_INT
    { ISD::FP_TO_SINT, MVT::v2i32, MVT::v2f32, 1 },
    { ISD::FP_TO_SINT, MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_SINT, MVT::v2i64, MVT::v2f64, 1 },
    { ISD::FP_TO_UINT, MVT::v2i32, MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i64, MVT::v2f64, 1 },

    // Complex, from v2f32: legal type is v2i32 (no cost) or v2i64 (1 ext).
    { ISD::FP_TO_SINT, MVT::v2i64, MVT::v2f32, 2 },
    { ISD::FP_TO_SINT, MVT::v2i16, MVT::v2f32, 1 },
    { ISD::FP_TO_SINT, MVT::v2i8,  MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i64, MVT::v2f32, 2 },
    { ISD::FP_TO_UINT, MVT::v2i16, MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i8,  MVT::v2f32, 1 },

    // Complex, from v4f32: legal type is v4i16, 1 narrowing => ~2
    { ISD::FP_TO_SINT, MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_SINT, MVT::v4i8,  MVT::v4f32, 2 },
    { ISD::FP_TO_UINT, MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_UINT, MVT::v4i8,  MVT::v4f32, 2 },

    // Complex, from v2f64: legal type is v2i32, 1 narrowing => ~2.
    { ISD::FP_TO_SINT, MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT, MVT::v2i16, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT, MVT::v2i8,  MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i16, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i8,  MVT::v2f64, 2 },
  };

  if (const auto *Entry = ConvertCostTableLookup(ConversionTbl, ISD,
                                                 DstTy.getSimpleVT(),
                                                 SrcTy.getSimpleVT()))
    return Entry->Cost;

  return BaseT::getCastInstrCost(Opcode, Dst, Src);
}

int AArch64TTIImpl::getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                             VectorType *VecTy,
                                             unsigned Index) {

  // Make sure we were given a valid extend opcode.
  assert((Opcode == Instruction::SExt || Opcode == Instruction::ZExt) &&
         "Invalid opcode");

  // We are extending an element we extract from a vector, so the source type
  // of the extend is the element type of the vector.
  auto *Src = VecTy->getElementType();

  // Sign- and zero-extends are for integer types only.
  assert(isa<IntegerType>(Dst) && isa<IntegerType>(Src) && "Invalid type");

  // Get the cost for the extract. We compute the cost (if any) for the extend
  // below.
  auto Cost = getVectorInstrCost(Instruction::ExtractElement, VecTy, Index);

  // Legalize the types.
  auto VecLT = TLI->getTypeLegalizationCost(DL, VecTy);
  auto DstVT = TLI->getValueType(DL, Dst);
  auto SrcVT = TLI->getValueType(DL, Src);

  // If the resulting type is still a vector and the destination type is legal,
  // we may get the extension for free. If not, get the default cost for the
  // extend.
  if (!VecLT.second.isVector() || !TLI->isTypeLegal(DstVT))
    return Cost + getCastInstrCost(Opcode, Dst, Src);

  // The destination type should be larger than the element type. If not, get
  // the default cost for the extend.
  if (DstVT.getSizeInBits() < SrcVT.getSizeInBits())
    return Cost + getCastInstrCost(Opcode, Dst, Src);

  switch (Opcode) {
  default:
    llvm_unreachable("Opcode should be either SExt or ZExt");

  // For sign-extends, we only need a smov, which performs the extension
  // automatically.
  case Instruction::SExt:
    return Cost;

  // For zero-extends, the extend is performed automatically by a umov unless
  // the destination type is i64 and the element type is i8 or i16.
  case Instruction::ZExt:
    if (DstVT.getSizeInBits() != 64u || SrcVT.getSizeInBits() == 32u)
      return Cost;
  }

  // If we are unable to perform the extend for free, get the default cost.
  return Cost + getCastInstrCost(Opcode, Dst, Src);
}

int AArch64TTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                       unsigned Index) {
  assert(Val->isVectorTy() && "This must be a vector type");

  if (Index != -1U) {
    // Legalize the type.
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Val);

    // This type is legalized to a scalar type.
    if (!LT.second.isVector())
      return 0;

    // The type may be split. Normalize the index to the new type.
    unsigned Width = LT.second.getVectorNumElements();
    Index = Index % Width;

    // The element at index zero is already inside the vector.
    if (Index == 0)
      return 0;
  }

  // All other insert/extracts cost this much.
  return ST->getVectorInsertExtractBaseCost();
}

int AArch64TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::OperandValueKind Opd1Info,
    TTI::OperandValueKind Opd2Info, TTI::OperandValueProperties Opd1PropInfo,
    TTI::OperandValueProperties Opd2PropInfo, ArrayRef<const Value *> Args) {
  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Ty);

  // If the instruction is a widening instruction (e.g., uaddl, saddw, etc.),
  // add in the widening overhead specified by the sub-target. Since the
  // extends feeding widening instructions are performed automatically, they
  // aren't present in the generated code and have a zero cost. By adding a
  // widening overhead here, we attach the total cost of the combined operation
  // to the widening instruction.
  int Cost = 0;
  if (isWideningInstruction(Ty, Opcode, Args))
    Cost += ST->getWideningBaseCost();

  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  switch (ISD) {
  default:
    return Cost + BaseT::getArithmeticInstrCost(Opcode, Ty, Opd1Info, Opd2Info,
                                                Opd1PropInfo, Opd2PropInfo);
  case ISD::SDIV:
    if (Opd2Info == TargetTransformInfo::OK_UniformConstantValue &&
        Opd2PropInfo == TargetTransformInfo::OP_PowerOf2) {
      // On AArch64, scalar signed division by constants power-of-two are
      // normally expanded to the sequence ADD + CMP + SELECT + SRA.
      // The OperandValue properties many not be same as that of previous
      // operation; conservatively assume OP_None.
      Cost += getArithmeticInstrCost(Instruction::Add, Ty, Opd1Info, Opd2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      Cost += getArithmeticInstrCost(Instruction::Sub, Ty, Opd1Info, Opd2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      Cost += getArithmeticInstrCost(Instruction::Select, Ty, Opd1Info, Opd2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      Cost += getArithmeticInstrCost(Instruction::AShr, Ty, Opd1Info, Opd2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      return Cost;
    }
    LLVM_FALLTHROUGH;
  case ISD::UDIV:
    if (Opd2Info == TargetTransformInfo::OK_UniformConstantValue) {
      auto VT = TLI->getValueType(DL, Ty);
      if (TLI->isOperationLegalOrCustom(ISD::MULHU, VT)) {
        // Vector signed division by constant are expanded to the
        // sequence MULHS + ADD/SUB + SRA + SRL + ADD, and unsigned division
        // to MULHS + SUB + SRL + ADD + SRL.
        int MulCost = getArithmeticInstrCost(Instruction::Mul, Ty, Opd1Info,
                                             Opd2Info,
                                             TargetTransformInfo::OP_None,
                                             TargetTransformInfo::OP_None);
        int AddCost = getArithmeticInstrCost(Instruction::Add, Ty, Opd1Info,
                                             Opd2Info,
                                             TargetTransformInfo::OP_None,
                                             TargetTransformInfo::OP_None);
        int ShrCost = getArithmeticInstrCost(Instruction::AShr, Ty, Opd1Info,
                                             Opd2Info,
                                             TargetTransformInfo::OP_None,
                                             TargetTransformInfo::OP_None);
        return MulCost * 2 + AddCost * 2 + ShrCost * 2 + 1;
      }
    }

    Cost += BaseT::getArithmeticInstrCost(Opcode, Ty, Opd1Info, Opd2Info,
                                          Opd1PropInfo, Opd2PropInfo);
    if (Ty->isVectorTy()) {
      // On AArch64, vector divisions are not supported natively and are
      // expanded into scalar divisions of each pair of elements.
      Cost += getArithmeticInstrCost(Instruction::ExtractElement, Ty, Opd1Info,
                                     Opd2Info, Opd1PropInfo, Opd2PropInfo);
      Cost += getArithmeticInstrCost(Instruction::InsertElement, Ty, Opd1Info,
                                     Opd2Info, Opd1PropInfo, Opd2PropInfo);
      // TODO: if one of the arguments is scalar, then it's not necessary to
      // double the cost of handling the vector elements.
      Cost += Cost;
    }
    return Cost;

  case ISD::ADD:
  case ISD::MUL:
  case ISD::XOR:
  case ISD::OR:
  case ISD::AND:
    // These nodes are marked as 'custom' for combining purposes only.
    // We know that they are legal. See LowerAdd in ISelLowering.
    return (Cost + 1) * LT.first;
  }
}

int AArch64TTIImpl::getAddressComputationCost(Type *Ty, ScalarEvolution *SE,
                                              const SCEV *Ptr) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  unsigned NumVectorInstToHideOverhead = 10;
  int MaxMergeDistance = 64;

  if (Ty->isVectorTy() && SE &&
      !BaseT::isConstantStridedAccessLessThan(SE, Ptr, MaxMergeDistance + 1))
    return NumVectorInstToHideOverhead;

  // In many cases the address computation is not merged into the instruction
  // addressing mode.
  return 1;
}

int AArch64TTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                       Type *CondTy, const Instruction *I) {

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  // We don't lower some vector selects well that are wider than the register
  // width.
  if (ValTy->isVectorTy() && ISD == ISD::SELECT) {
    // We would need this many instructions to hide the scalarization happening.
    const int AmortizationCost = 20;
    static const TypeConversionCostTblEntry
    VectorSelectTbl[] = {
      { ISD::SELECT, MVT::v16i1, MVT::v16i16, 16 },
      { ISD::SELECT, MVT::v8i1, MVT::v8i32, 8 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i32, 16 },
      { ISD::SELECT, MVT::v4i1, MVT::v4i64, 4 * AmortizationCost },
      { ISD::SELECT, MVT::v8i1, MVT::v8i64, 8 * AmortizationCost },
      { ISD::SELECT, MVT::v16i1, MVT::v16i64, 16 * AmortizationCost }
    };

    EVT SelCondTy = TLI->getValueType(DL, CondTy);
    EVT SelValTy = TLI->getValueType(DL, ValTy);
    if (SelCondTy.isSimple() && SelValTy.isSimple()) {
      if (const auto *Entry = ConvertCostTableLookup(VectorSelectTbl, ISD,
                                                     SelCondTy.getSimpleVT(),
                                                     SelValTy.getSimpleVT()))
        return Entry->Cost;
    }
  }
  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, I);
}

int AArch64TTIImpl::getMemoryOpCost(unsigned Opcode, Type *Ty,
                                    unsigned Alignment, unsigned AddressSpace,
                                    const Instruction *I) {
  auto LT = TLI->getTypeLegalizationCost(DL, Ty);

  if (ST->isMisaligned128StoreSlow() && Opcode == Instruction::Store &&
      LT.second.is128BitVector() && Alignment < 16) {
    // Unaligned stores are extremely inefficient. We don't split all
    // unaligned 128-bit stores because the negative impact that has shown in
    // practice on inlined block copy code.
    // We make such stores expensive so that we will only vectorize if there
    // are 6 other instructions getting vectorized.
    const int AmortizationCost = 6;

    return LT.first * 2 * AmortizationCost;
  }

  if (Ty->isVectorTy() && Ty->getVectorElementType()->isIntegerTy(8)) {
    unsigned ProfitableNumElements;
    if (Opcode == Instruction::Store)
      // We use a custom trunc store lowering so v.4b should be profitable.
      ProfitableNumElements = 4;
    else
      // We scalarize the loads because there is not v.4b register and we
      // have to promote the elements to v.2.
      ProfitableNumElements = 8;

    if (Ty->getVectorNumElements() < ProfitableNumElements) {
      unsigned NumVecElts = Ty->getVectorNumElements();
      unsigned NumVectorizableInstsToAmortize = NumVecElts * 2;
      // We generate 2 instructions per vector element.
      return NumVectorizableInstsToAmortize * NumVecElts * 2;
    }
  }

  return LT.first;
}

int AArch64TTIImpl::getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                               unsigned Factor,
                                               ArrayRef<unsigned> Indices,
                                               unsigned Alignment,
                                               unsigned AddressSpace,
                                               bool UseMaskForCond,
                                               bool UseMaskForGaps) {
  assert(Factor >= 2 && "Invalid interleave factor");
  assert(isa<VectorType>(VecTy) && "Expect a vector type");

  if (!UseMaskForCond && !UseMaskForGaps && 
      Factor <= TLI->getMaxSupportedInterleaveFactor()) {
    unsigned NumElts = VecTy->getVectorNumElements();
    auto *SubVecTy = VectorType::get(VecTy->getScalarType(), NumElts / Factor);

    // ldN/stN only support legal vector types of size 64 or 128 in bits.
    // Accesses having vector types that are a multiple of 128 bits can be
    // matched to more than one ldN/stN instruction.
    if (NumElts % Factor == 0 &&
        TLI->isLegalInterleavedAccessType(SubVecTy, DL))
      return Factor * TLI->getNumInterleavedAccesses(SubVecTy, DL);
  }

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace,
                                           UseMaskForCond, UseMaskForGaps);
}

int AArch64TTIImpl::getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) {
  int Cost = 0;
  for (auto *I : Tys) {
    if (!I->isVectorTy())
      continue;
    if (I->getScalarSizeInBits() * I->getVectorNumElements() == 128)
      Cost += getMemoryOpCost(Instruction::Store, I, 128, 0) +
        getMemoryOpCost(Instruction::Load, I, 128, 0);
  }
  return Cost;
}

unsigned AArch64TTIImpl::getMaxInterleaveFactor(unsigned VF) {
  return ST->getMaxInterleaveFactor();
}

// For Falkor, we want to avoid having too many strided loads in a loop since
// that can exhaust the HW prefetcher resources.  We adjust the unroller
// MaxCount preference below to attempt to ensure unrolling doesn't create too
// many strided loads.
static void
getFalkorUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                              TargetTransformInfo::UnrollingPreferences &UP) {
  enum { MaxStridedLoads = 7 };
  auto countStridedLoads = [](Loop *L, ScalarEvolution &SE) {
    int StridedLoads = 0;
    // FIXME? We could make this more precise by looking at the CFG and
    // e.g. not counting loads in each side of an if-then-else diamond.
    for (const auto BB : L->blocks()) {
      for (auto &I : *BB) {
        LoadInst *LMemI = dyn_cast<LoadInst>(&I);
        if (!LMemI)
          continue;

        Value *PtrValue = LMemI->getPointerOperand();
        if (L->isLoopInvariant(PtrValue))
          continue;

        const SCEV *LSCEV = SE.getSCEV(PtrValue);
        const SCEVAddRecExpr *LSCEVAddRec = dyn_cast<SCEVAddRecExpr>(LSCEV);
        if (!LSCEVAddRec || !LSCEVAddRec->isAffine())
          continue;

        // FIXME? We could take pairing of unrolled load copies into account
        // by looking at the AddRec, but we would probably have to limit this
        // to loops with no stores or other memory optimization barriers.
        ++StridedLoads;
        // We've seen enough strided loads that seeing more won't make a
        // difference.
        if (StridedLoads > MaxStridedLoads / 2)
          return StridedLoads;
      }
    }
    return StridedLoads;
  };

  int StridedLoads = countStridedLoads(L, SE);
  LLVM_DEBUG(dbgs() << "falkor-hwpf: detected " << StridedLoads
                    << " strided loads\n");
  // Pick the largest power of 2 unroll count that won't result in too many
  // strided loads.
  if (StridedLoads) {
    UP.MaxCount = 1 << Log2_32(MaxStridedLoads / StridedLoads);
    LLVM_DEBUG(dbgs() << "falkor-hwpf: setting unroll MaxCount to "
                      << UP.MaxCount << '\n');
  }
}

void AArch64TTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                             TTI::UnrollingPreferences &UP) {
  // Enable partial unrolling and runtime unrolling.
  BaseT::getUnrollingPreferences(L, SE, UP);

  // For inner loop, it is more likely to be a hot one, and the runtime check
  // can be promoted out from LICM pass, so the overhead is less, let's try
  // a larger threshold to unroll more loops.
  if (L->getLoopDepth() > 1)
    UP.PartialThreshold *= 2;

  // Disable partial & runtime unrolling on -Os.
  UP.PartialOptSizeThreshold = 0;

  if (ST->getProcFamily() == AArch64Subtarget::Falkor &&
      EnableFalkorHWPFUnrollFix)
    getFalkorUnrollingPreferences(L, SE, UP);
}

Value *AArch64TTIImpl::getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst,
                                                         Type *ExpectedType) {
  switch (Inst->getIntrinsicID()) {
  default:
    return nullptr;
  case Intrinsic::aarch64_neon_st2:
  case Intrinsic::aarch64_neon_st3:
  case Intrinsic::aarch64_neon_st4: {
    // Create a struct type
    StructType *ST = dyn_cast<StructType>(ExpectedType);
    if (!ST)
      return nullptr;
    unsigned NumElts = Inst->getNumArgOperands() - 1;
    if (ST->getNumElements() != NumElts)
      return nullptr;
    for (unsigned i = 0, e = NumElts; i != e; ++i) {
      if (Inst->getArgOperand(i)->getType() != ST->getElementType(i))
        return nullptr;
    }
    Value *Res = UndefValue::get(ExpectedType);
    IRBuilder<> Builder(Inst);
    for (unsigned i = 0, e = NumElts; i != e; ++i) {
      Value *L = Inst->getArgOperand(i);
      Res = Builder.CreateInsertValue(Res, L, i);
    }
    return Res;
  }
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_ld4:
    if (Inst->getType() == ExpectedType)
      return Inst;
    return nullptr;
  }
}

bool AArch64TTIImpl::getTgtMemIntrinsic(IntrinsicInst *Inst,
                                        MemIntrinsicInfo &Info) {
  switch (Inst->getIntrinsicID()) {
  default:
    break;
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_ld4:
    Info.ReadMem = true;
    Info.WriteMem = false;
    Info.PtrVal = Inst->getArgOperand(0);
    break;
  case Intrinsic::aarch64_neon_st2:
  case Intrinsic::aarch64_neon_st3:
  case Intrinsic::aarch64_neon_st4:
    Info.ReadMem = false;
    Info.WriteMem = true;
    Info.PtrVal = Inst->getArgOperand(Inst->getNumArgOperands() - 1);
    break;
  }

  switch (Inst->getIntrinsicID()) {
  default:
    return false;
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_st2:
    Info.MatchingId = VECTOR_LDST_TWO_ELEMENTS;
    break;
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_st3:
    Info.MatchingId = VECTOR_LDST_THREE_ELEMENTS;
    break;
  case Intrinsic::aarch64_neon_ld4:
  case Intrinsic::aarch64_neon_st4:
    Info.MatchingId = VECTOR_LDST_FOUR_ELEMENTS;
    break;
  }
  return true;
}

/// See if \p I should be considered for address type promotion. We check if \p
/// I is a sext with right type and used in memory accesses. If it used in a
/// "complex" getelementptr, we allow it to be promoted without finding other
/// sext instructions that sign extended the same initial value. A getelementptr
/// is considered as "complex" if it has more than 2 operands.
bool AArch64TTIImpl::shouldConsiderAddressTypePromotion(
    const Instruction &I, bool &AllowPromotionWithoutCommonHeader) {
  bool Considerable = false;
  AllowPromotionWithoutCommonHeader = false;
  if (!isa<SExtInst>(&I))
    return false;
  Type *ConsideredSExtType =
      Type::getInt64Ty(I.getParent()->getParent()->getContext());
  if (I.getType() != ConsideredSExtType)
    return false;
  // See if the sext is the one with the right type and used in at least one
  // GetElementPtrInst.
  for (const User *U : I.users()) {
    if (const GetElementPtrInst *GEPInst = dyn_cast<GetElementPtrInst>(U)) {
      Considerable = true;
      // A getelementptr is considered as "complex" if it has more than 2
      // operands. We will promote a SExt used in such complex GEP as we
      // expect some computation to be merged if they are done on 64 bits.
      if (GEPInst->getNumOperands() > 2) {
        AllowPromotionWithoutCommonHeader = true;
        break;
      }
    }
  }
  return Considerable;
}

unsigned AArch64TTIImpl::getCacheLineSize() {
  return ST->getCacheLineSize();
}

unsigned AArch64TTIImpl::getPrefetchDistance() {
  return ST->getPrefetchDistance();
}

unsigned AArch64TTIImpl::getMinPrefetchStride() {
  return ST->getMinPrefetchStride();
}

unsigned AArch64TTIImpl::getMaxPrefetchIterationsAhead() {
  return ST->getMaxPrefetchIterationsAhead();
}

bool AArch64TTIImpl::useReductionIntrinsic(unsigned Opcode, Type *Ty,
                                           TTI::ReductionFlags Flags) const {
  assert(isa<VectorType>(Ty) && "Expected Ty to be a vector type");
  unsigned ScalarBits = Ty->getScalarSizeInBits();
  switch (Opcode) {
  case Instruction::FAdd:
  case Instruction::FMul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Mul:
    return false;
  case Instruction::Add:
    return ScalarBits * Ty->getVectorNumElements() >= 128;
  case Instruction::ICmp:
    return (ScalarBits < 64) &&
           (ScalarBits * Ty->getVectorNumElements() >= 128);
  case Instruction::FCmp:
    return Flags.NoNaN;
  default:
    llvm_unreachable("Unhandled reduction opcode");
  }
  return false;
}

int AArch64TTIImpl::getArithmeticReductionCost(unsigned Opcode, Type *ValTy,
                                               bool IsPairwiseForm) {

  if (IsPairwiseForm)
    return BaseT::getArithmeticReductionCost(Opcode, ValTy, IsPairwiseForm);

  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, ValTy);
  MVT MTy = LT.second;
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // Horizontal adds can use the 'addv' instruction. We model the cost of these
  // instructions as normal vector adds. This is the only arithmetic vector
  // reduction operation for which we have an instruction.
  static const CostTblEntry CostTblNoPairwise[]{
      {ISD::ADD, MVT::v8i8,  1},
      {ISD::ADD, MVT::v16i8, 1},
      {ISD::ADD, MVT::v4i16, 1},
      {ISD::ADD, MVT::v8i16, 1},
      {ISD::ADD, MVT::v4i32, 1},
  };

  if (const auto *Entry = CostTableLookup(CostTblNoPairwise, ISD, MTy))
    return LT.first * Entry->Cost;

  return BaseT::getArithmeticReductionCost(Opcode, ValTy, IsPairwiseForm);
}

int AArch64TTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                                   Type *SubTp) {
  if (Kind == TTI::SK_Broadcast || Kind == TTI::SK_Transpose ||
      Kind == TTI::SK_Select || Kind == TTI::SK_PermuteSingleSrc) {
    static const CostTblEntry ShuffleTbl[] = {
      // Broadcast shuffle kinds can be performed with 'dup'.
      { TTI::SK_Broadcast, MVT::v8i8,  1 },
      { TTI::SK_Broadcast, MVT::v16i8, 1 },
      { TTI::SK_Broadcast, MVT::v4i16, 1 },
      { TTI::SK_Broadcast, MVT::v8i16, 1 },
      { TTI::SK_Broadcast, MVT::v2i32, 1 },
      { TTI::SK_Broadcast, MVT::v4i32, 1 },
      { TTI::SK_Broadcast, MVT::v2i64, 1 },
      { TTI::SK_Broadcast, MVT::v2f32, 1 },
      { TTI::SK_Broadcast, MVT::v4f32, 1 },
      { TTI::SK_Broadcast, MVT::v2f64, 1 },
      // Transpose shuffle kinds can be performed with 'trn1/trn2' and
      // 'zip1/zip2' instructions.
      { TTI::SK_Transpose, MVT::v8i8,  1 },
      { TTI::SK_Transpose, MVT::v16i8, 1 },
      { TTI::SK_Transpose, MVT::v4i16, 1 },
      { TTI::SK_Transpose, MVT::v8i16, 1 },
      { TTI::SK_Transpose, MVT::v2i32, 1 },
      { TTI::SK_Transpose, MVT::v4i32, 1 },
      { TTI::SK_Transpose, MVT::v2i64, 1 },
      { TTI::SK_Transpose, MVT::v2f32, 1 },
      { TTI::SK_Transpose, MVT::v4f32, 1 },
      { TTI::SK_Transpose, MVT::v2f64, 1 },
      // Select shuffle kinds.
      // TODO: handle vXi8/vXi16.
      { TTI::SK_Select, MVT::v2i32, 1 }, // mov.
      { TTI::SK_Select, MVT::v4i32, 2 }, // rev+trn (or similar).
      { TTI::SK_Select, MVT::v2i64, 1 }, // mov.
      { TTI::SK_Select, MVT::v2f32, 1 }, // mov.
      { TTI::SK_Select, MVT::v4f32, 2 }, // rev+trn (or similar).
      { TTI::SK_Select, MVT::v2f64, 1 }, // mov.
      // PermuteSingleSrc shuffle kinds.
      // TODO: handle vXi8/vXi16.
      { TTI::SK_PermuteSingleSrc, MVT::v2i32, 1 }, // mov.
      { TTI::SK_PermuteSingleSrc, MVT::v4i32, 3 }, // perfectshuffle worst case.
      { TTI::SK_PermuteSingleSrc, MVT::v2i64, 1 }, // mov.
      { TTI::SK_PermuteSingleSrc, MVT::v2f32, 1 }, // mov.
      { TTI::SK_PermuteSingleSrc, MVT::v4f32, 3 }, // perfectshuffle worst case.
      { TTI::SK_PermuteSingleSrc, MVT::v2f64, 1 }, // mov.
    };
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Tp);
    if (const auto *Entry = CostTableLookup(ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;
  }

  return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
}
