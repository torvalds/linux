//===-- PPCTargetTransformInfo.cpp - PPC specific TTI ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PPCTargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "ppctti"

static cl::opt<bool> DisablePPCConstHoist("disable-ppc-constant-hoisting",
cl::desc("disable constant hoisting on PPC"), cl::init(false), cl::Hidden);

// This is currently only used for the data prefetch pass which is only enabled
// for BG/Q by default.
static cl::opt<unsigned>
CacheLineSize("ppc-loop-prefetch-cache-line", cl::Hidden, cl::init(64),
              cl::desc("The loop prefetch cache line size"));

static cl::opt<bool>
EnablePPCColdCC("ppc-enable-coldcc", cl::Hidden, cl::init(false),
                cl::desc("Enable using coldcc calling conv for cold "
                         "internal functions"));

//===----------------------------------------------------------------------===//
//
// PPC cost model.
//
//===----------------------------------------------------------------------===//

TargetTransformInfo::PopcntSupportKind
PPCTTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  if (ST->hasPOPCNTD() != PPCSubtarget::POPCNTD_Unavailable && TyWidth <= 64)
    return ST->hasPOPCNTD() == PPCSubtarget::POPCNTD_Slow ?
             TTI::PSK_SlowHardware : TTI::PSK_FastHardware;
  return TTI::PSK_Software;
}

int PPCTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCost(Imm, Ty);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  if (Imm == 0)
    return TTI::TCC_Free;

  if (Imm.getBitWidth() <= 64) {
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;

    if (isInt<32>(Imm.getSExtValue())) {
      // A constant that can be materialized using lis.
      if ((Imm.getZExtValue() & 0xFFFF) == 0)
        return TTI::TCC_Basic;

      return 2 * TTI::TCC_Basic;
    }
  }

  return 4 * TTI::TCC_Basic;
}

int PPCTTIImpl::getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                              Type *Ty) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCost(IID, Idx, Imm, Ty);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
    if ((Idx == 1) && Imm.getBitWidth() <= 64 && isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Free;
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
  return PPCTTIImpl::getIntImmCost(Imm, Ty);
}

int PPCTTIImpl::getIntImmCost(unsigned Opcode, unsigned Idx, const APInt &Imm,
                              Type *Ty) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCost(Opcode, Idx, Imm, Ty);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  unsigned ImmIdx = ~0U;
  bool ShiftedFree = false, RunFree = false, UnsignedFree = false,
       ZeroFree = false;
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
  case Instruction::And:
    RunFree = true; // (for the rotate-and-mask instructions)
    LLVM_FALLTHROUGH;
  case Instruction::Add:
  case Instruction::Or:
  case Instruction::Xor:
    ShiftedFree = true;
    LLVM_FALLTHROUGH;
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    ImmIdx = 1;
    break;
  case Instruction::ICmp:
    UnsignedFree = true;
    ImmIdx = 1;
    // Zero comparisons can use record-form instructions.
    LLVM_FALLTHROUGH;
  case Instruction::Select:
    ZeroFree = true;
    break;
  case Instruction::PHI:
  case Instruction::Call:
  case Instruction::Ret:
  case Instruction::Load:
  case Instruction::Store:
    break;
  }

  if (ZeroFree && Imm == 0)
    return TTI::TCC_Free;

  if (Idx == ImmIdx && Imm.getBitWidth() <= 64) {
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Free;

    if (RunFree) {
      if (Imm.getBitWidth() <= 32 &&
          (isShiftedMask_32(Imm.getZExtValue()) ||
           isShiftedMask_32(~Imm.getZExtValue())))
        return TTI::TCC_Free;

      if (ST->isPPC64() &&
          (isShiftedMask_64(Imm.getZExtValue()) ||
           isShiftedMask_64(~Imm.getZExtValue())))
        return TTI::TCC_Free;
    }

    if (UnsignedFree && isUInt<16>(Imm.getZExtValue()))
      return TTI::TCC_Free;

    if (ShiftedFree && (Imm.getZExtValue() & 0xFFFF) == 0)
      return TTI::TCC_Free;
  }

  return PPCTTIImpl::getIntImmCost(Imm, Ty);
}

unsigned PPCTTIImpl::getUserCost(const User *U,
                                 ArrayRef<const Value *> Operands) {
  if (U->getType()->isVectorTy()) {
    // Instructions that need to be split should cost more.
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, U->getType());
    return LT.first * BaseT::getUserCost(U, Operands);
  }

  return BaseT::getUserCost(U, Operands);
}

void PPCTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::UnrollingPreferences &UP) {
  if (ST->getDarwinDirective() == PPC::DIR_A2) {
    // The A2 is in-order with a deep pipeline, and concatenation unrolling
    // helps expose latency-hiding opportunities to the instruction scheduler.
    UP.Partial = UP.Runtime = true;

    // We unroll a lot on the A2 (hundreds of instructions), and the benefits
    // often outweigh the cost of a division to compute the trip count.
    UP.AllowExpensiveTripCount = true;
  }

  BaseT::getUnrollingPreferences(L, SE, UP);
}

// This function returns true to allow using coldcc calling convention.
// Returning true results in coldcc being used for functions which are cold at
// all call sites when the callers of the functions are not calling any other
// non coldcc functions.
bool PPCTTIImpl::useColdCCForColdCall(Function &F) {
  return EnablePPCColdCC;
}

bool PPCTTIImpl::enableAggressiveInterleaving(bool LoopHasReductions) {
  // On the A2, always unroll aggressively. For QPX unaligned loads, we depend
  // on combining the loads generated for consecutive accesses, and failure to
  // do so is particularly expensive. This makes it much more likely (compared
  // to only using concatenation unrolling).
  if (ST->getDarwinDirective() == PPC::DIR_A2)
    return true;

  return LoopHasReductions;
}

const PPCTTIImpl::TTI::MemCmpExpansionOptions *
PPCTTIImpl::enableMemCmpExpansion(bool IsZeroCmp) const {
  static const auto Options = []() {
    TTI::MemCmpExpansionOptions Options;
    Options.LoadSizes.push_back(8);
    Options.LoadSizes.push_back(4);
    Options.LoadSizes.push_back(2);
    Options.LoadSizes.push_back(1);
    return Options;
  }();
  return &Options;
}

bool PPCTTIImpl::enableInterleavedAccessVectorization() {
  return true;
}

unsigned PPCTTIImpl::getNumberOfRegisters(bool Vector) {
  if (Vector && !ST->hasAltivec() && !ST->hasQPX())
    return 0;
  return ST->hasVSX() ? 64 : 32;
}

unsigned PPCTTIImpl::getRegisterBitWidth(bool Vector) const {
  if (Vector) {
    if (ST->hasQPX()) return 256;
    if (ST->hasAltivec()) return 128;
    return 0;
  }

  if (ST->isPPC64())
    return 64;
  return 32;

}

unsigned PPCTTIImpl::getCacheLineSize() {
  // Check first if the user specified a custom line size.
  if (CacheLineSize.getNumOccurrences() > 0)
    return CacheLineSize;

  // On P7, P8 or P9 we have a cache line size of 128.
  unsigned Directive = ST->getDarwinDirective();
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8 ||
      Directive == PPC::DIR_PWR9)
    return 128;

  // On other processors return a default of 64 bytes.
  return 64;
}

unsigned PPCTTIImpl::getPrefetchDistance() {
  // This seems like a reasonable default for the BG/Q (this pass is enabled, by
  // default, only on the BG/Q).
  return 300;
}

unsigned PPCTTIImpl::getMaxInterleaveFactor(unsigned VF) {
  unsigned Directive = ST->getDarwinDirective();
  // The 440 has no SIMD support, but floating-point instructions
  // have a 5-cycle latency, so unroll by 5x for latency hiding.
  if (Directive == PPC::DIR_440)
    return 5;

  // The A2 has no SIMD support, but floating-point instructions
  // have a 6-cycle latency, so unroll by 6x for latency hiding.
  if (Directive == PPC::DIR_A2)
    return 6;

  // FIXME: For lack of any better information, do no harm...
  if (Directive == PPC::DIR_E500mc || Directive == PPC::DIR_E5500)
    return 1;

  // For P7 and P8, floating-point instructions have a 6-cycle latency and
  // there are two execution units, so unroll by 12x for latency hiding.
  // FIXME: the same for P9 as previous gen until POWER9 scheduling is ready
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8 ||
      Directive == PPC::DIR_PWR9)
    return 12;

  // For most things, modern systems have two execution units (and
  // out-of-order execution).
  return 2;
}

int PPCTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::OperandValueKind Op1Info,
    TTI::OperandValueKind Op2Info, TTI::OperandValueProperties Opd1PropInfo,
    TTI::OperandValueProperties Opd2PropInfo, ArrayRef<const Value *> Args) {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  // Fallback to the default implementation.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, Op1Info, Op2Info,
                                       Opd1PropInfo, Opd2PropInfo);
}

int PPCTTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                               Type *SubTp) {
  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Tp);

  // PPC, for both Altivec/VSX and QPX, support cheap arbitrary permutations
  // (at least in the sense that there need only be one non-loop-invariant
  // instruction). We need one such shuffle instruction for each actual
  // register (this is not true for arbitrary shuffles, but is true for the
  // structured types of shuffles covered by TTI::ShuffleKind).
  return LT.first;
}

int PPCTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                 const Instruction *I) {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  return BaseT::getCastInstrCost(Opcode, Dst, Src);
}

int PPCTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                   const Instruction *I) {
  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, I);
}

int PPCTTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index) {
  assert(Val->isVectorTy() && "This must be a vector type");

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  if (ST->hasVSX() && Val->getScalarType()->isDoubleTy()) {
    // Double-precision scalars are already located in index #0.
    if (Index == 0)
      return 0;

    return BaseT::getVectorInstrCost(Opcode, Val, Index);
  } else if (ST->hasQPX() && Val->getScalarType()->isFloatingPointTy()) {
    // Floating point scalars are already located in index #0.
    if (Index == 0)
      return 0;

    return BaseT::getVectorInstrCost(Opcode, Val, Index);
  }

  // Estimated cost of a load-hit-store delay.  This was obtained
  // experimentally as a minimum needed to prevent unprofitable
  // vectorization for the paq8p benchmark.  It may need to be
  // raised further if other unprofitable cases remain.
  unsigned LHSPenalty = 2;
  if (ISD == ISD::INSERT_VECTOR_ELT)
    LHSPenalty += 7;

  // Vector element insert/extract with Altivec is very expensive,
  // because they require store and reload with the attendant
  // processor stall for load-hit-store.  Until VSX is available,
  // these need to be estimated as very costly.
  if (ISD == ISD::EXTRACT_VECTOR_ELT ||
      ISD == ISD::INSERT_VECTOR_ELT)
    return LHSPenalty + BaseT::getVectorInstrCost(Opcode, Val, Index);

  return BaseT::getVectorInstrCost(Opcode, Val, Index);
}

int PPCTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                                unsigned AddressSpace, const Instruction *I) {
  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Src);
  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");

  int Cost = BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace);

  bool IsAltivecType = ST->hasAltivec() &&
                       (LT.second == MVT::v16i8 || LT.second == MVT::v8i16 ||
                        LT.second == MVT::v4i32 || LT.second == MVT::v4f32);
  bool IsVSXType = ST->hasVSX() &&
                   (LT.second == MVT::v2f64 || LT.second == MVT::v2i64);
  bool IsQPXType = ST->hasQPX() &&
                   (LT.second == MVT::v4f64 || LT.second == MVT::v4f32);

  // VSX has 32b/64b load instructions. Legalization can handle loading of
  // 32b/64b to VSR correctly and cheaply. But BaseT::getMemoryOpCost and
  // PPCTargetLowering can't compute the cost appropriately. So here we
  // explicitly check this case.
  unsigned MemBytes = Src->getPrimitiveSizeInBits();
  if (Opcode == Instruction::Load && ST->hasVSX() && IsAltivecType &&
      (MemBytes == 64 || (ST->hasP8Vector() && MemBytes == 32)))
    return 1;

  // Aligned loads and stores are easy.
  unsigned SrcBytes = LT.second.getStoreSize();
  if (!SrcBytes || !Alignment || Alignment >= SrcBytes)
    return Cost;

  // If we can use the permutation-based load sequence, then this is also
  // relatively cheap (not counting loop-invariant instructions): one load plus
  // one permute (the last load in a series has extra cost, but we're
  // neglecting that here). Note that on the P7, we could do unaligned loads
  // for Altivec types using the VSX instructions, but that's more expensive
  // than using the permutation-based load sequence. On the P8, that's no
  // longer true.
  if (Opcode == Instruction::Load &&
      ((!ST->hasP8Vector() && IsAltivecType) || IsQPXType) &&
      Alignment >= LT.second.getScalarType().getStoreSize())
    return Cost + LT.first; // Add the cost of the permutations.

  // For VSX, we can do unaligned loads and stores on Altivec/VSX types. On the
  // P7, unaligned vector loads are more expensive than the permutation-based
  // load sequence, so that might be used instead, but regardless, the net cost
  // is about the same (not counting loop-invariant instructions).
  if (IsVSXType || (ST->hasVSX() && IsAltivecType))
    return Cost;

  // Newer PPC supports unaligned memory access.
  if (TLI->allowsMisalignedMemoryAccesses(LT.second, 0))
    return Cost;

  // PPC in general does not support unaligned loads and stores. They'll need
  // to be decomposed based on the alignment factor.

  // Add the cost of each scalar load or store.
  Cost += LT.first*(SrcBytes/Alignment-1);

  // For a vector type, there is also scalarization overhead (only for
  // stores, loads are expanded using the vector-load + permutation sequence,
  // which is much less expensive).
  if (Src->isVectorTy() && Opcode == Instruction::Store)
    for (int i = 0, e = Src->getVectorNumElements(); i < e; ++i)
      Cost += getVectorInstrCost(Instruction::ExtractElement, Src, i);

  return Cost;
}

int PPCTTIImpl::getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
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

  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, VecTy);

  // Firstly, the cost of load/store operation.
  int Cost = getMemoryOpCost(Opcode, VecTy, Alignment, AddressSpace);

  // PPC, for both Altivec/VSX and QPX, support cheap arbitrary permutations
  // (at least in the sense that there need only be one non-loop-invariant
  // instruction). For each result vector, we need one shuffle per incoming
  // vector (except that the first shuffle can take two incoming vectors
  // because it does not need to take itself).
  Cost += Factor*(LT.first-1);

  return Cost;
}

