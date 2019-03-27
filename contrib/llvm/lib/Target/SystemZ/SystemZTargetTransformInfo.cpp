//===-- SystemZTargetTransformInfo.cpp - SystemZ-specific TTI -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a TargetTransformInfo analysis pass specific to the
// SystemZ target machine. It uses the target's detailed information to provide
// more precise answers to certain TTI queries, while letting the target
// independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "systemztti"

//===----------------------------------------------------------------------===//
//
// SystemZ cost model.
//
//===----------------------------------------------------------------------===//

int SystemZTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;
  // No cost model for operations on integers larger than 64 bit implemented yet.
  if (BitSize > 64)
    return TTI::TCC_Free;

  if (Imm == 0)
    return TTI::TCC_Free;

  if (Imm.getBitWidth() <= 64) {
    // Constants loaded via lgfi.
    if (isInt<32>(Imm.getSExtValue()))
      return TTI::TCC_Basic;
    // Constants loaded via llilf.
    if (isUInt<32>(Imm.getZExtValue()))
      return TTI::TCC_Basic;
    // Constants loaded via llihf:
    if ((Imm.getZExtValue() & 0xffffffff) == 0)
      return TTI::TCC_Basic;

    return 2 * TTI::TCC_Basic;
  }

  return 4 * TTI::TCC_Basic;
}

int SystemZTTIImpl::getIntImmCost(unsigned Opcode, unsigned Idx,
                                  const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;
  // No cost model for operations on integers larger than 64 bit implemented yet.
  if (BitSize > 64)
    return TTI::TCC_Free;

  switch (Opcode) {
  default:
    return TTI::TCC_Free;
  case Instruction::GetElementPtr:
    // Always hoist the base address of a GetElementPtr. This prevents the
    // creation of new constants for every base constant that gets constant
    // folded with the offset.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Store:
    if (Idx == 0 && Imm.getBitWidth() <= 64) {
      // Any 8-bit immediate store can by implemented via mvi.
      if (BitSize == 8)
        return TTI::TCC_Free;
      // 16-bit immediate values can be stored via mvhhi/mvhi/mvghi.
      if (isInt<16>(Imm.getSExtValue()))
        return TTI::TCC_Free;
    }
    break;
  case Instruction::ICmp:
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      // Comparisons against signed 32-bit immediates implemented via cgfi.
      if (isInt<32>(Imm.getSExtValue()))
        return TTI::TCC_Free;
      // Comparisons against unsigned 32-bit immediates implemented via clgfi.
      if (isUInt<32>(Imm.getZExtValue()))
        return TTI::TCC_Free;
    }
    break;
  case Instruction::Add:
  case Instruction::Sub:
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      // We use algfi/slgfi to add/subtract 32-bit unsigned immediates.
      if (isUInt<32>(Imm.getZExtValue()))
        return TTI::TCC_Free;
      // Or their negation, by swapping addition vs. subtraction.
      if (isUInt<32>(-Imm.getSExtValue()))
        return TTI::TCC_Free;
    }
    break;
  case Instruction::Mul:
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      // We use msgfi to multiply by 32-bit signed immediates.
      if (isInt<32>(Imm.getSExtValue()))
        return TTI::TCC_Free;
    }
    break;
  case Instruction::Or:
  case Instruction::Xor:
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      // Masks supported by oilf/xilf.
      if (isUInt<32>(Imm.getZExtValue()))
        return TTI::TCC_Free;
      // Masks supported by oihf/xihf.
      if ((Imm.getZExtValue() & 0xffffffff) == 0)
        return TTI::TCC_Free;
    }
    break;
  case Instruction::And:
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      // Any 32-bit AND operation can by implemented via nilf.
      if (BitSize <= 32)
        return TTI::TCC_Free;
      // 64-bit masks supported by nilf.
      if (isUInt<32>(~Imm.getZExtValue()))
        return TTI::TCC_Free;
      // 64-bit masks supported by nilh.
      if ((Imm.getZExtValue() & 0xffffffff) == 0xffffffff)
        return TTI::TCC_Free;
      // Some 64-bit AND operations can be implemented via risbg.
      const SystemZInstrInfo *TII = ST->getInstrInfo();
      unsigned Start, End;
      if (TII->isRxSBGMask(Imm.getZExtValue(), BitSize, Start, End))
        return TTI::TCC_Free;
    }
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    // Always return TCC_Free for the shift value of a shift instruction.
    if (Idx == 1)
      return TTI::TCC_Free;
    break;
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
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

  return SystemZTTIImpl::getIntImmCost(Imm, Ty);
}

int SystemZTTIImpl::getIntImmCost(Intrinsic::ID IID, unsigned Idx,
                                  const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;
  // No cost model for operations on integers larger than 64 bit implemented yet.
  if (BitSize > 64)
    return TTI::TCC_Free;

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
    // These get expanded to include a normal addition/subtraction.
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      if (isUInt<32>(Imm.getZExtValue()))
        return TTI::TCC_Free;
      if (isUInt<32>(-Imm.getSExtValue()))
        return TTI::TCC_Free;
    }
    break;
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow:
    // These get expanded to include a normal multiplication.
    if (Idx == 1 && Imm.getBitWidth() <= 64) {
      if (isInt<32>(Imm.getSExtValue()))
        return TTI::TCC_Free;
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
  return SystemZTTIImpl::getIntImmCost(Imm, Ty);
}

TargetTransformInfo::PopcntSupportKind
SystemZTTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Type width must be power of 2");
  if (ST->hasPopulationCount() && TyWidth <= 64)
    return TTI::PSK_FastHardware;
  return TTI::PSK_Software;
}

void SystemZTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                             TTI::UnrollingPreferences &UP) {
  // Find out if L contains a call, what the machine instruction count
  // estimate is, and how many stores there are.
  bool HasCall = false;
  unsigned NumStores = 0;
  for (auto &BB : L->blocks())
    for (auto &I : *BB) {
      if (isa<CallInst>(&I) || isa<InvokeInst>(&I)) {
        ImmutableCallSite CS(&I);
        if (const Function *F = CS.getCalledFunction()) {
          if (isLoweredToCall(F))
            HasCall = true;
          if (F->getIntrinsicID() == Intrinsic::memcpy ||
              F->getIntrinsicID() == Intrinsic::memset)
            NumStores++;
        } else { // indirect call.
          HasCall = true;
        }
      }
      if (isa<StoreInst>(&I)) {
        Type *MemAccessTy = I.getOperand(0)->getType();
        NumStores += getMemoryOpCost(Instruction::Store, MemAccessTy, 0, 0);
      }
    }

  // The z13 processor will run out of store tags if too many stores
  // are fed into it too quickly. Therefore make sure there are not
  // too many stores in the resulting unrolled loop.
  unsigned const Max = (NumStores ? (12 / NumStores) : UINT_MAX);

  if (HasCall) {
    // Only allow full unrolling if loop has any calls.
    UP.FullUnrollMaxCount = Max;
    UP.MaxCount = 1;
    return;
  }

  UP.MaxCount = Max;
  if (UP.MaxCount <= 1)
    return;

  // Allow partial and runtime trip count unrolling.
  UP.Partial = UP.Runtime = true;

  UP.PartialThreshold = 75;
  UP.DefaultUnrollRuntimeCount = 4;

  // Allow expensive instructions in the pre-header of the loop.
  UP.AllowExpensiveTripCount = true;

  UP.Force = true;
}


bool SystemZTTIImpl::isLSRCostLess(TargetTransformInfo::LSRCost &C1,
                                   TargetTransformInfo::LSRCost &C2) {
  // SystemZ specific: check instruction count (first), and don't care about
  // ImmCost, since offsets are checked explicitly.
  return std::tie(C1.Insns, C1.NumRegs, C1.AddRecCost,
                  C1.NumIVMuls, C1.NumBaseAdds,
                  C1.ScaleCost, C1.SetupCost) <
    std::tie(C2.Insns, C2.NumRegs, C2.AddRecCost,
             C2.NumIVMuls, C2.NumBaseAdds,
             C2.ScaleCost, C2.SetupCost);
}

unsigned SystemZTTIImpl::getNumberOfRegisters(bool Vector) {
  if (!Vector)
    // Discount the stack pointer.  Also leave out %r0, since it can't
    // be used in an address.
    return 14;
  if (ST->hasVector())
    return 32;
  return 0;
}

unsigned SystemZTTIImpl::getRegisterBitWidth(bool Vector) const {
  if (!Vector)
    return 64;
  if (ST->hasVector())
    return 128;
  return 0;
}

bool SystemZTTIImpl::hasDivRemOp(Type *DataType, bool IsSigned) {
  EVT VT = TLI->getValueType(DL, DataType);
  return (VT.isScalarInteger() && TLI->isTypeLegal(VT));
}

// Return the bit size for the scalar type or vector element
// type. getScalarSizeInBits() returns 0 for a pointer type.
static unsigned getScalarSizeInBits(Type *Ty) {
  unsigned Size =
    (Ty->isPtrOrPtrVectorTy() ? 64U : Ty->getScalarSizeInBits());
  assert(Size > 0 && "Element must have non-zero size.");
  return Size;
}

// getNumberOfParts() calls getTypeLegalizationCost() which splits the vector
// type until it is legal. This would e.g. return 4 for <6 x i64>, instead of
// 3.
static unsigned getNumVectorRegs(Type *Ty) {
  assert(Ty->isVectorTy() && "Expected vector type");
  unsigned WideBits = getScalarSizeInBits(Ty) * Ty->getVectorNumElements();
  assert(WideBits > 0 && "Could not compute size of vector");
  return ((WideBits % 128U) ? ((WideBits / 128U) + 1) : (WideBits / 128U));
}

int SystemZTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty,
    TTI::OperandValueKind Op1Info, TTI::OperandValueKind Op2Info,
    TTI::OperandValueProperties Opd1PropInfo,
    TTI::OperandValueProperties Opd2PropInfo,
    ArrayRef<const Value *> Args) {

  // TODO: return a good value for BB-VECTORIZER that includes the
  // immediate loads, which we do not want to count for the loop
  // vectorizer, since they are hopefully hoisted out of the loop. This
  // would require a new parameter 'InLoop', but not sure if constant
  // args are common enough to motivate this.

  unsigned ScalarBits = Ty->getScalarSizeInBits();

  // There are thre cases of division and remainder: Dividing with a register
  // needs a divide instruction. A divisor which is a power of two constant
  // can be implemented with a sequence of shifts. Any other constant needs a
  // multiply and shifts.
  const unsigned DivInstrCost = 20;
  const unsigned DivMulSeqCost = 10;
  const unsigned SDivPow2Cost = 4;

  bool SignedDivRem =
      Opcode == Instruction::SDiv || Opcode == Instruction::SRem;
  bool UnsignedDivRem =
      Opcode == Instruction::UDiv || Opcode == Instruction::URem;

  // Check for a constant divisor.
  bool DivRemConst = false;
  bool DivRemConstPow2 = false;
  if ((SignedDivRem || UnsignedDivRem) && Args.size() == 2) {
    if (const Constant *C = dyn_cast<Constant>(Args[1])) {
      const ConstantInt *CVal =
          (C->getType()->isVectorTy()
               ? dyn_cast_or_null<const ConstantInt>(C->getSplatValue())
               : dyn_cast<const ConstantInt>(C));
      if (CVal != nullptr &&
          (CVal->getValue().isPowerOf2() || (-CVal->getValue()).isPowerOf2()))
        DivRemConstPow2 = true;
      else
        DivRemConst = true;
    }
  }

  if (Ty->isVectorTy()) {
    assert(ST->hasVector() &&
           "getArithmeticInstrCost() called with vector type.");
    unsigned VF = Ty->getVectorNumElements();
    unsigned NumVectors = getNumVectorRegs(Ty);

    // These vector operations are custom handled, but are still supported
    // with one instruction per vector, regardless of element size.
    if (Opcode == Instruction::Shl || Opcode == Instruction::LShr ||
        Opcode == Instruction::AShr) {
      return NumVectors;
    }

    if (DivRemConstPow2)
      return (NumVectors * (SignedDivRem ? SDivPow2Cost : 1));
    if (DivRemConst)
      return VF * DivMulSeqCost + getScalarizationOverhead(Ty, Args);
    if ((SignedDivRem || UnsignedDivRem) && VF > 4)
      // Temporary hack: disable high vectorization factors with integer
      // division/remainder, which will get scalarized and handled with
      // GR128 registers. The mischeduler is not clever enough to avoid
      // spilling yet.
      return 1000;

    // These FP operations are supported with a single vector instruction for
    // double (base implementation assumes float generally costs 2). For
    // FP128, the scalar cost is 1, and there is no overhead since the values
    // are already in scalar registers.
    if (Opcode == Instruction::FAdd || Opcode == Instruction::FSub ||
        Opcode == Instruction::FMul || Opcode == Instruction::FDiv) {
      switch (ScalarBits) {
      case 32: {
        // The vector enhancements facility 1 provides v4f32 instructions.
        if (ST->hasVectorEnhancements1())
          return NumVectors;
        // Return the cost of multiple scalar invocation plus the cost of
        // inserting and extracting the values.
        unsigned ScalarCost =
            getArithmeticInstrCost(Opcode, Ty->getScalarType());
        unsigned Cost = (VF * ScalarCost) + getScalarizationOverhead(Ty, Args);
        // FIXME: VF 2 for these FP operations are currently just as
        // expensive as for VF 4.
        if (VF == 2)
          Cost *= 2;
        return Cost;
      }
      case 64:
      case 128:
        return NumVectors;
      default:
        break;
      }
    }

    // There is no native support for FRem.
    if (Opcode == Instruction::FRem) {
      unsigned Cost = (VF * LIBCALL_COST) + getScalarizationOverhead(Ty, Args);
      // FIXME: VF 2 for float is currently just as expensive as for VF 4.
      if (VF == 2 && ScalarBits == 32)
        Cost *= 2;
      return Cost;
    }
  }
  else {  // Scalar:
    // These FP operations are supported with a dedicated instruction for
    // float, double and fp128 (base implementation assumes float generally
    // costs 2).
    if (Opcode == Instruction::FAdd || Opcode == Instruction::FSub ||
        Opcode == Instruction::FMul || Opcode == Instruction::FDiv)
      return 1;

    // There is no native support for FRem.
    if (Opcode == Instruction::FRem)
      return LIBCALL_COST;

    // Or requires one instruction, although it has custom handling for i64.
    if (Opcode == Instruction::Or)
      return 1;

    if (Opcode == Instruction::Xor && ScalarBits == 1) {
      if (ST->hasLoadStoreOnCond2())
        return 5; // 2 * (li 0; loc 1); xor
      return 7; // 2 * ipm sequences ; xor ; shift ; compare
    }

    if (DivRemConstPow2)
      return (SignedDivRem ? SDivPow2Cost : 1);
    if (DivRemConst)
      return DivMulSeqCost;
    if (SignedDivRem || UnsignedDivRem)
      return DivInstrCost;
  }

  // Fallback to the default implementation.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, Op1Info, Op2Info,
                                       Opd1PropInfo, Opd2PropInfo, Args);
}

int SystemZTTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                                   Type *SubTp) {
  assert (Tp->isVectorTy());
  assert (ST->hasVector() && "getShuffleCost() called.");
  unsigned NumVectors = getNumVectorRegs(Tp);

  // TODO: Since fp32 is expanded, the shuffle cost should always be 0.

  // FP128 values are always in scalar registers, so there is no work
  // involved with a shuffle, except for broadcast. In that case register
  // moves are done with a single instruction per element.
  if (Tp->getScalarType()->isFP128Ty())
    return (Kind == TargetTransformInfo::SK_Broadcast ? NumVectors - 1 : 0);

  switch (Kind) {
  case  TargetTransformInfo::SK_ExtractSubvector:
    // ExtractSubvector Index indicates start offset.

    // Extracting a subvector from first index is a noop.
    return (Index == 0 ? 0 : NumVectors);

  case TargetTransformInfo::SK_Broadcast:
    // Loop vectorizer calls here to figure out the extra cost of
    // broadcasting a loaded value to all elements of a vector. Since vlrep
    // loads and replicates with a single instruction, adjust the returned
    // value.
    return NumVectors - 1;

  default:

    // SystemZ supports single instruction permutation / replication.
    return NumVectors;
  }

  return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
}

// Return the log2 difference of the element sizes of the two vector types.
static unsigned getElSizeLog2Diff(Type *Ty0, Type *Ty1) {
  unsigned Bits0 = Ty0->getScalarSizeInBits();
  unsigned Bits1 = Ty1->getScalarSizeInBits();

  if (Bits1 >  Bits0)
    return (Log2_32(Bits1) - Log2_32(Bits0));

  return (Log2_32(Bits0) - Log2_32(Bits1));
}

// Return the number of instructions needed to truncate SrcTy to DstTy.
unsigned SystemZTTIImpl::
getVectorTruncCost(Type *SrcTy, Type *DstTy) {
  assert (SrcTy->isVectorTy() && DstTy->isVectorTy());
  assert (SrcTy->getPrimitiveSizeInBits() > DstTy->getPrimitiveSizeInBits() &&
          "Packing must reduce size of vector type.");
  assert (SrcTy->getVectorNumElements() == DstTy->getVectorNumElements() &&
          "Packing should not change number of elements.");

  // TODO: Since fp32 is expanded, the extract cost should always be 0.

  unsigned NumParts = getNumVectorRegs(SrcTy);
  if (NumParts <= 2)
    // Up to 2 vector registers can be truncated efficiently with pack or
    // permute. The latter requires an immediate mask to be loaded, which
    // typically gets hoisted out of a loop.  TODO: return a good value for
    // BB-VECTORIZER that includes the immediate loads, which we do not want
    // to count for the loop vectorizer.
    return 1;

  unsigned Cost = 0;
  unsigned Log2Diff = getElSizeLog2Diff(SrcTy, DstTy);
  unsigned VF = SrcTy->getVectorNumElements();
  for (unsigned P = 0; P < Log2Diff; ++P) {
    if (NumParts > 1)
      NumParts /= 2;
    Cost += NumParts;
  }

  // Currently, a general mix of permutes and pack instructions is output by
  // isel, which follow the cost computation above except for this case which
  // is one instruction less:
  if (VF == 8 && SrcTy->getScalarSizeInBits() == 64 &&
      DstTy->getScalarSizeInBits() == 8)
    Cost--;

  return Cost;
}

// Return the cost of converting a vector bitmask produced by a compare
// (SrcTy), to the type of the select or extend instruction (DstTy).
unsigned SystemZTTIImpl::
getVectorBitmaskConversionCost(Type *SrcTy, Type *DstTy) {
  assert (SrcTy->isVectorTy() && DstTy->isVectorTy() &&
          "Should only be called with vector types.");

  unsigned PackCost = 0;
  unsigned SrcScalarBits = SrcTy->getScalarSizeInBits();
  unsigned DstScalarBits = DstTy->getScalarSizeInBits();
  unsigned Log2Diff = getElSizeLog2Diff(SrcTy, DstTy);
  if (SrcScalarBits > DstScalarBits)
    // The bitmask will be truncated.
    PackCost = getVectorTruncCost(SrcTy, DstTy);
  else if (SrcScalarBits < DstScalarBits) {
    unsigned DstNumParts = getNumVectorRegs(DstTy);
    // Each vector select needs its part of the bitmask unpacked.
    PackCost = Log2Diff * DstNumParts;
    // Extra cost for moving part of mask before unpacking.
    PackCost += DstNumParts - 1;
  }

  return PackCost;
}

// Return the type of the compared operands. This is needed to compute the
// cost for a Select / ZExt or SExt instruction.
static Type *getCmpOpsType(const Instruction *I, unsigned VF = 1) {
  Type *OpTy = nullptr;
  if (CmpInst *CI = dyn_cast<CmpInst>(I->getOperand(0)))
    OpTy = CI->getOperand(0)->getType();
  else if (Instruction *LogicI = dyn_cast<Instruction>(I->getOperand(0)))
    if (LogicI->getNumOperands() == 2)
      if (CmpInst *CI0 = dyn_cast<CmpInst>(LogicI->getOperand(0)))
        if (isa<CmpInst>(LogicI->getOperand(1)))
          OpTy = CI0->getOperand(0)->getType();

  if (OpTy != nullptr) {
    if (VF == 1) {
      assert (!OpTy->isVectorTy() && "Expected scalar type");
      return OpTy;
    }
    // Return the potentially vectorized type based on 'I' and 'VF'.  'I' may
    // be either scalar or already vectorized with a same or lesser VF.
    Type *ElTy = OpTy->getScalarType();
    return VectorType::get(ElTy, VF);
  }

  return nullptr;
}

// Get the cost of converting a boolean vector to a vector with same width
// and element size as Dst, plus the cost of zero extending if needed.
unsigned SystemZTTIImpl::
getBoolVecToIntConversionCost(unsigned Opcode, Type *Dst,
                              const Instruction *I) {
  assert (Dst->isVectorTy());
  unsigned VF = Dst->getVectorNumElements();
  unsigned Cost = 0;
  // If we know what the widths of the compared operands, get any cost of
  // converting it to match Dst. Otherwise assume same widths.
  Type *CmpOpTy = ((I != nullptr) ? getCmpOpsType(I, VF) : nullptr);
  if (CmpOpTy != nullptr)
    Cost = getVectorBitmaskConversionCost(CmpOpTy, Dst);
  if (Opcode == Instruction::ZExt || Opcode == Instruction::UIToFP)
    // One 'vn' per dst vector with an immediate mask.
    Cost += getNumVectorRegs(Dst);
  return Cost;
}

int SystemZTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                     const Instruction *I) {
  unsigned DstScalarBits = Dst->getScalarSizeInBits();
  unsigned SrcScalarBits = Src->getScalarSizeInBits();

  if (Src->isVectorTy()) {
    assert (ST->hasVector() && "getCastInstrCost() called with vector type.");
    assert (Dst->isVectorTy());
    unsigned VF = Src->getVectorNumElements();
    unsigned NumDstVectors = getNumVectorRegs(Dst);
    unsigned NumSrcVectors = getNumVectorRegs(Src);

    if (Opcode == Instruction::Trunc) {
      if (Src->getScalarSizeInBits() == Dst->getScalarSizeInBits())
        return 0; // Check for NOOP conversions.
      return getVectorTruncCost(Src, Dst);
    }

    if (Opcode == Instruction::ZExt || Opcode == Instruction::SExt) {
      if (SrcScalarBits >= 8) {
        // ZExt/SExt will be handled with one unpack per doubling of width.
        unsigned NumUnpacks = getElSizeLog2Diff(Src, Dst);

        // For types that spans multiple vector registers, some additional
        // instructions are used to setup the unpacking.
        unsigned NumSrcVectorOps =
          (NumUnpacks > 1 ? (NumDstVectors - NumSrcVectors)
                          : (NumDstVectors / 2));

        return (NumUnpacks * NumDstVectors) + NumSrcVectorOps;
      }
      else if (SrcScalarBits == 1)
        return getBoolVecToIntConversionCost(Opcode, Dst, I);
    }

    if (Opcode == Instruction::SIToFP || Opcode == Instruction::UIToFP ||
        Opcode == Instruction::FPToSI || Opcode == Instruction::FPToUI) {
      // TODO: Fix base implementation which could simplify things a bit here
      // (seems to miss on differentiating on scalar/vector types).

      // Only 64 bit vector conversions are natively supported.
      if (DstScalarBits == 64) {
        if (SrcScalarBits == 64)
          return NumDstVectors;

        if (SrcScalarBits == 1)
          return getBoolVecToIntConversionCost(Opcode, Dst, I) + NumDstVectors;
      }

      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values. Base implementation does not
      // realize float->int gets scalarized.
      unsigned ScalarCost = getCastInstrCost(Opcode, Dst->getScalarType(),
                                             Src->getScalarType());
      unsigned TotCost = VF * ScalarCost;
      bool NeedsInserts = true, NeedsExtracts = true;
      // FP128 registers do not get inserted or extracted.
      if (DstScalarBits == 128 &&
          (Opcode == Instruction::SIToFP || Opcode == Instruction::UIToFP))
        NeedsInserts = false;
      if (SrcScalarBits == 128 &&
          (Opcode == Instruction::FPToSI || Opcode == Instruction::FPToUI))
        NeedsExtracts = false;

      TotCost += getScalarizationOverhead(Src, false, NeedsExtracts);
      TotCost += getScalarizationOverhead(Dst, NeedsInserts, false);

      // FIXME: VF 2 for float<->i32 is currently just as expensive as for VF 4.
      if (VF == 2 && SrcScalarBits == 32 && DstScalarBits == 32)
        TotCost *= 2;

      return TotCost;
    }

    if (Opcode == Instruction::FPTrunc) {
      if (SrcScalarBits == 128)  // fp128 -> double/float + inserts of elements.
        return VF /*ldxbr/lexbr*/ + getScalarizationOverhead(Dst, true, false);
      else // double -> float
        return VF / 2 /*vledb*/ + std::max(1U, VF / 4 /*vperm*/);
    }

    if (Opcode == Instruction::FPExt) {
      if (SrcScalarBits == 32 && DstScalarBits == 64) {
        // float -> double is very rare and currently unoptimized. Instead of
        // using vldeb, which can do two at a time, all conversions are
        // scalarized.
        return VF * 2;
      }
      // -> fp128.  VF * lxdb/lxeb + extraction of elements.
      return VF + getScalarizationOverhead(Src, false, true);
    }
  }
  else { // Scalar
    assert (!Dst->isVectorTy());

    if (Opcode == Instruction::SIToFP || Opcode == Instruction::UIToFP) {
      if (SrcScalarBits >= 32 ||
          (I != nullptr && isa<LoadInst>(I->getOperand(0))))
        return 1;
      return SrcScalarBits > 1 ? 2 /*i8/i16 extend*/ : 5 /*branch seq.*/;
    }

    if ((Opcode == Instruction::ZExt || Opcode == Instruction::SExt) &&
        Src->isIntegerTy(1)) {
      if (ST->hasLoadStoreOnCond2())
        return 2; // li 0; loc 1

      // This should be extension of a compare i1 result, which is done with
      // ipm and a varying sequence of instructions.
      unsigned Cost = 0;
      if (Opcode == Instruction::SExt)
        Cost = (DstScalarBits < 64 ? 3 : 4);
      if (Opcode == Instruction::ZExt)
        Cost = 3;
      Type *CmpOpTy = ((I != nullptr) ? getCmpOpsType(I) : nullptr);
      if (CmpOpTy != nullptr && CmpOpTy->isFloatingPointTy())
        // If operands of an fp-type was compared, this costs +1.
        Cost++;
      return Cost;
    }
  }

  return BaseT::getCastInstrCost(Opcode, Dst, Src, I);
}

// Scalar i8 / i16 operations will typically be made after first extending
// the operands to i32.
static unsigned getOperandsExtensionCost(const Instruction *I) {
  unsigned ExtCost = 0;
  for (Value *Op : I->operands())
    // A load of i8 or i16 sign/zero extends to i32.
    if (!isa<LoadInst>(Op) && !isa<ConstantInt>(Op))
      ExtCost++;

  return ExtCost;
}

int SystemZTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                       Type *CondTy, const Instruction *I) {
  if (ValTy->isVectorTy()) {
    assert (ST->hasVector() && "getCmpSelInstrCost() called with vector type.");
    unsigned VF = ValTy->getVectorNumElements();

    // Called with a compare instruction.
    if (Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) {
      unsigned PredicateExtraCost = 0;
      if (I != nullptr) {
        // Some predicates cost one or two extra instructions.
        switch (cast<CmpInst>(I)->getPredicate()) {
        case CmpInst::Predicate::ICMP_NE:
        case CmpInst::Predicate::ICMP_UGE:
        case CmpInst::Predicate::ICMP_ULE:
        case CmpInst::Predicate::ICMP_SGE:
        case CmpInst::Predicate::ICMP_SLE:
          PredicateExtraCost = 1;
          break;
        case CmpInst::Predicate::FCMP_ONE:
        case CmpInst::Predicate::FCMP_ORD:
        case CmpInst::Predicate::FCMP_UEQ:
        case CmpInst::Predicate::FCMP_UNO:
          PredicateExtraCost = 2;
          break;
        default:
          break;
        }
      }

      // Float is handled with 2*vmr[lh]f + 2*vldeb + vfchdb for each pair of
      // floats.  FIXME: <2 x float> generates same code as <4 x float>.
      unsigned CmpCostPerVector = (ValTy->getScalarType()->isFloatTy() ? 10 : 1);
      unsigned NumVecs_cmp = getNumVectorRegs(ValTy);

      unsigned Cost = (NumVecs_cmp * (CmpCostPerVector + PredicateExtraCost));
      return Cost;
    }
    else { // Called with a select instruction.
      assert (Opcode == Instruction::Select);

      // We can figure out the extra cost of packing / unpacking if the
      // instruction was passed and the compare instruction is found.
      unsigned PackCost = 0;
      Type *CmpOpTy = ((I != nullptr) ? getCmpOpsType(I, VF) : nullptr);
      if (CmpOpTy != nullptr)
        PackCost =
          getVectorBitmaskConversionCost(CmpOpTy, ValTy);

      return getNumVectorRegs(ValTy) /*vsel*/ + PackCost;
    }
  }
  else { // Scalar
    switch (Opcode) {
    case Instruction::ICmp: {
      // A loaded value compared with 0 with multiple users becomes Load and
      // Test. The load is then not foldable, so return 0 cost for the ICmp.
      unsigned ScalarBits = ValTy->getScalarSizeInBits();
      if (I != nullptr && ScalarBits >= 32)
        if (LoadInst *Ld = dyn_cast<LoadInst>(I->getOperand(0)))
          if (const ConstantInt *C = dyn_cast<ConstantInt>(I->getOperand(1)))
            if (!Ld->hasOneUse() && Ld->getParent() == I->getParent() &&
                C->getZExtValue() == 0)
              return 0;

      unsigned Cost = 1;
      if (ValTy->isIntegerTy() && ValTy->getScalarSizeInBits() <= 16)
        Cost += (I != nullptr ? getOperandsExtensionCost(I) : 2);
      return Cost;
    }
    case Instruction::Select:
      if (ValTy->isFloatingPointTy())
        return 4; // No load on condition for FP - costs a conditional jump.
      return 1; // Load On Condition.
    }
  }

  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, nullptr);
}

int SystemZTTIImpl::
getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index) {
  // vlvgp will insert two grs into a vector register, so only count half the
  // number of instructions.
  if (Opcode == Instruction::InsertElement && Val->isIntOrIntVectorTy(64))
    return ((Index % 2 == 0) ? 1 : 0);

  if (Opcode == Instruction::ExtractElement) {
    int Cost = ((getScalarSizeInBits(Val) == 1) ? 2 /*+test-under-mask*/ : 1);

    // Give a slight penalty for moving out of vector pipeline to FXU unit.
    if (Index == 0 && Val->isIntOrIntVectorTy())
      Cost += 1;

    return Cost;
  }

  return BaseT::getVectorInstrCost(Opcode, Val, Index);
}

// Check if a load may be folded as a memory operand in its user.
bool SystemZTTIImpl::
isFoldableLoad(const LoadInst *Ld, const Instruction *&FoldedValue) {
  if (!Ld->hasOneUse())
    return false;
  FoldedValue = Ld;
  const Instruction *UserI = cast<Instruction>(*Ld->user_begin());
  unsigned LoadedBits = getScalarSizeInBits(Ld->getType());
  unsigned TruncBits = 0;
  unsigned SExtBits = 0;
  unsigned ZExtBits = 0;
  if (UserI->hasOneUse()) {
    unsigned UserBits = UserI->getType()->getScalarSizeInBits();
    if (isa<TruncInst>(UserI))
      TruncBits = UserBits;
    else if (isa<SExtInst>(UserI))
      SExtBits = UserBits;
    else if (isa<ZExtInst>(UserI))
      ZExtBits = UserBits;
  }
  if (TruncBits || SExtBits || ZExtBits) {
    FoldedValue = UserI;
    UserI = cast<Instruction>(*UserI->user_begin());
    // Load (single use) -> trunc/extend (single use) -> UserI
  }
  if ((UserI->getOpcode() == Instruction::Sub ||
       UserI->getOpcode() == Instruction::SDiv ||
       UserI->getOpcode() == Instruction::UDiv) &&
      UserI->getOperand(1) != FoldedValue)
    return false; // Not commutative, only RHS foldable.
  // LoadOrTruncBits holds the number of effectively loaded bits, but 0 if an
  // extension was made of the load.
  unsigned LoadOrTruncBits =
      ((SExtBits || ZExtBits) ? 0 : (TruncBits ? TruncBits : LoadedBits));
  switch (UserI->getOpcode()) {
  case Instruction::Add: // SE: 16->32, 16/32->64, z14:16->64. ZE: 32->64
  case Instruction::Sub:
  case Instruction::ICmp:
    if (LoadedBits == 32 && ZExtBits == 64)
      return true;
    LLVM_FALLTHROUGH;
  case Instruction::Mul: // SE: 16->32, 32->64, z14:16->64
    if (UserI->getOpcode() != Instruction::ICmp) {
      if (LoadedBits == 16 &&
          (SExtBits == 32 ||
           (SExtBits == 64 && ST->hasMiscellaneousExtensions2())))
        return true;
      if (LoadOrTruncBits == 16)
        return true;
    }
    LLVM_FALLTHROUGH;
  case Instruction::SDiv:// SE: 32->64
    if (LoadedBits == 32 && SExtBits == 64)
      return true;
    LLVM_FALLTHROUGH;
  case Instruction::UDiv:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // This also makes sense for float operations, but disabled for now due
    // to regressions.
    // case Instruction::FCmp:
    // case Instruction::FAdd:
    // case Instruction::FSub:
    // case Instruction::FMul:
    // case Instruction::FDiv:

    // All possible extensions of memory checked above.

    // Comparison between memory and immediate.
    if (UserI->getOpcode() == Instruction::ICmp)
      if (ConstantInt *CI = dyn_cast<ConstantInt>(UserI->getOperand(1)))
        if (isUInt<16>(CI->getZExtValue()))
          return true;
    return (LoadOrTruncBits == 32 || LoadOrTruncBits == 64);
    break;
  }
  return false;
}

static bool isBswapIntrinsicCall(const Value *V) {
  if (const Instruction *I = dyn_cast<Instruction>(V))
    if (auto *CI = dyn_cast<CallInst>(I))
      if (auto *F = CI->getCalledFunction())
        if (F->getIntrinsicID() == Intrinsic::bswap)
          return true;
  return false;
}

int SystemZTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                    unsigned Alignment, unsigned AddressSpace,
                                    const Instruction *I) {
  assert(!Src->isVoidTy() && "Invalid type");

  if (!Src->isVectorTy() && Opcode == Instruction::Load && I != nullptr) {
    // Store the load or its truncated or extended value in FoldedValue.
    const Instruction *FoldedValue = nullptr;
    if (isFoldableLoad(cast<LoadInst>(I), FoldedValue)) {
      const Instruction *UserI = cast<Instruction>(*FoldedValue->user_begin());
      assert (UserI->getNumOperands() == 2 && "Expected a binop.");

      // UserI can't fold two loads, so in that case return 0 cost only
      // half of the time.
      for (unsigned i = 0; i < 2; ++i) {
        if (UserI->getOperand(i) == FoldedValue)
          continue;

        if (Instruction *OtherOp = dyn_cast<Instruction>(UserI->getOperand(i))){
          LoadInst *OtherLoad = dyn_cast<LoadInst>(OtherOp);
          if (!OtherLoad &&
              (isa<TruncInst>(OtherOp) || isa<SExtInst>(OtherOp) ||
               isa<ZExtInst>(OtherOp)))
            OtherLoad = dyn_cast<LoadInst>(OtherOp->getOperand(0));
          if (OtherLoad && isFoldableLoad(OtherLoad, FoldedValue/*dummy*/))
            return i == 0; // Both operands foldable.
        }
      }

      return 0; // Only I is foldable in user.
    }
  }

  unsigned NumOps =
    (Src->isVectorTy() ? getNumVectorRegs(Src) : getNumberOfParts(Src));

  // Store/Load reversed saves one instruction.
  if (!Src->isVectorTy() && NumOps == 1 && I != nullptr) {
    if (Opcode == Instruction::Load && I->hasOneUse()) {
      const Instruction *LdUser = cast<Instruction>(*I->user_begin());
      // In case of load -> bswap -> store, return normal cost for the load.
      if (isBswapIntrinsicCall(LdUser) &&
          (!LdUser->hasOneUse() || !isa<StoreInst>(*LdUser->user_begin())))
        return 0;
    }
    else if (const StoreInst *SI = dyn_cast<StoreInst>(I)) {
      const Value *StoredVal = SI->getValueOperand();
      if (StoredVal->hasOneUse() && isBswapIntrinsicCall(StoredVal))
        return 0;
    }
  }

  if (Src->getScalarSizeInBits() == 128)
    // 128 bit scalars are held in a pair of two 64 bit registers.
    NumOps *= 2;

  return  NumOps;
}

// The generic implementation of getInterleavedMemoryOpCost() is based on
// adding costs of the memory operations plus all the extracts and inserts
// needed for using / defining the vector operands. The SystemZ version does
// roughly the same but bases the computations on vector permutations
// instead.
int SystemZTTIImpl::getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                               unsigned Factor,
                                               ArrayRef<unsigned> Indices,
                                               unsigned Alignment,
                                               unsigned AddressSpace,
                                               bool UseMaskForCond,
                                               bool UseMaskForGaps) {
  if (UseMaskForCond || UseMaskForGaps)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace,
                                             UseMaskForCond, UseMaskForGaps);
  assert(isa<VectorType>(VecTy) &&
         "Expect a vector type for interleaved memory op");

  // Return the ceiling of dividing A by B.
  auto ceil = [](unsigned A, unsigned B) { return (A + B - 1) / B; };

  unsigned NumElts = VecTy->getVectorNumElements();
  assert(Factor > 1 && NumElts % Factor == 0 && "Invalid interleave factor");
  unsigned VF = NumElts / Factor;
  unsigned NumEltsPerVecReg = (128U / getScalarSizeInBits(VecTy));
  unsigned NumVectorMemOps = getNumVectorRegs(VecTy);
  unsigned NumPermutes = 0;

  if (Opcode == Instruction::Load) {
    // Loading interleave groups may have gaps, which may mean fewer
    // loads. Find out how many vectors will be loaded in total, and in how
    // many of them each value will be in.
    BitVector UsedInsts(NumVectorMemOps, false);
    std::vector<BitVector> ValueVecs(Factor, BitVector(NumVectorMemOps, false));
    for (unsigned Index : Indices)
      for (unsigned Elt = 0; Elt < VF; ++Elt) {
        unsigned Vec = (Index + Elt * Factor) / NumEltsPerVecReg;
        UsedInsts.set(Vec);
        ValueVecs[Index].set(Vec);
      }
    NumVectorMemOps = UsedInsts.count();

    for (unsigned Index : Indices) {
      // Estimate that each loaded source vector containing this Index
      // requires one operation, except that vperm can handle two input
      // registers first time for each dst vector.
      unsigned NumSrcVecs = ValueVecs[Index].count();
      unsigned NumDstVecs = ceil(VF * getScalarSizeInBits(VecTy), 128U);
      assert (NumSrcVecs >= NumDstVecs && "Expected at least as many sources");
      NumPermutes += std::max(1U, NumSrcVecs - NumDstVecs);
    }
  } else {
    // Estimate the permutes for each stored vector as the smaller of the
    // number of elements and the number of source vectors. Subtract one per
    // dst vector for vperm (S.A.).
    unsigned NumSrcVecs = std::min(NumEltsPerVecReg, Factor);
    unsigned NumDstVecs = NumVectorMemOps;
    assert (NumSrcVecs > 1 && "Expected at least two source vectors.");
    NumPermutes += (NumDstVecs * NumSrcVecs) - NumDstVecs;
  }

  // Cost of load/store operations and the permutations needed.
  return NumVectorMemOps + NumPermutes;
}

static int getVectorIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy) {
  if (RetTy->isVectorTy() && ID == Intrinsic::bswap)
    return getNumVectorRegs(RetTy); // VPERM
  return -1;
}

int SystemZTTIImpl::getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
                                          ArrayRef<Value *> Args,
                                          FastMathFlags FMF, unsigned VF) {
  int Cost = getVectorIntrinsicInstrCost(ID, RetTy);
  if (Cost != -1)
    return Cost;
  return BaseT::getIntrinsicInstrCost(ID, RetTy, Args, FMF, VF);
}

int SystemZTTIImpl::getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
                                          ArrayRef<Type *> Tys,
                                          FastMathFlags FMF,
                                          unsigned ScalarizationCostPassed) {
  int Cost = getVectorIntrinsicInstrCost(ID, RetTy);
  if (Cost != -1)
    return Cost;
  return BaseT::getIntrinsicInstrCost(ID, RetTy, Tys,
                                      FMF, ScalarizationCostPassed);
}
