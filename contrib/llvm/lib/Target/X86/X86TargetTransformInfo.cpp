//===-- X86TargetTransformInfo.cpp - X86 specific TTI pass ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// X86 target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//
/// About Cost Model numbers used below it's necessary to say the following:
/// the numbers correspond to some "generic" X86 CPU instead of usage of
/// concrete CPU model. Usually the numbers correspond to CPU where the feature
/// apeared at the first time. For example, if we do Subtarget.hasSSE42() in
/// the lookups below the cost is based on Nehalem as that was the first CPU
/// to support that feature level and thus has most likely the worst case cost.
/// Some examples of other technologies/CPUs:
///   SSE 3   - Pentium4 / Athlon64
///   SSE 4.1 - Penryn
///   SSE 4.2 - Nehalem
///   AVX     - Sandy Bridge
///   AVX2    - Haswell
///   AVX-512 - Xeon Phi / Skylake
/// And some examples of instruction target dependent costs (latency)
///                   divss     sqrtss          rsqrtss
///   AMD K7            11-16     19              3
///   Piledriver        9-24      13-15           5
///   Jaguar            14        16              2
///   Pentium II,III    18        30              2
///   Nehalem           7-14      7-18            3
///   Haswell           10-13     11              5
/// TODO: Develop and implement  the target dependent cost model and
/// specialize cost numbers for different Cost Model Targets such as throughput,
/// code size, latency and uop count.
//===----------------------------------------------------------------------===//

#include "X86TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "x86tti"

//===----------------------------------------------------------------------===//
//
// X86 cost model.
//
//===----------------------------------------------------------------------===//

TargetTransformInfo::PopcntSupportKind
X86TTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  // TODO: Currently the __builtin_popcount() implementation using SSE3
  //   instructions is inefficient. Once the problem is fixed, we should
  //   call ST->hasSSE3() instead of ST->hasPOPCNT().
  return ST->hasPOPCNT() ? TTI::PSK_FastHardware : TTI::PSK_Software;
}

llvm::Optional<unsigned> X86TTIImpl::getCacheSize(
  TargetTransformInfo::CacheLevel Level) const {
  switch (Level) {
  case TargetTransformInfo::CacheLevel::L1D:
    //   - Penryn
    //   - Nehalem
    //   - Westmere
    //   - Sandy Bridge
    //   - Ivy Bridge
    //   - Haswell
    //   - Broadwell
    //   - Skylake
    //   - Kabylake
    return 32 * 1024;  //  32 KByte
  case TargetTransformInfo::CacheLevel::L2D:
    //   - Penryn
    //   - Nehalem
    //   - Westmere
    //   - Sandy Bridge
    //   - Ivy Bridge
    //   - Haswell
    //   - Broadwell
    //   - Skylake
    //   - Kabylake
    return 256 * 1024; // 256 KByte
  }

  llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
}

llvm::Optional<unsigned> X86TTIImpl::getCacheAssociativity(
  TargetTransformInfo::CacheLevel Level) const {
  //   - Penryn
  //   - Nehalem
  //   - Westmere
  //   - Sandy Bridge
  //   - Ivy Bridge
  //   - Haswell
  //   - Broadwell
  //   - Skylake
  //   - Kabylake
  switch (Level) {
  case TargetTransformInfo::CacheLevel::L1D:
    LLVM_FALLTHROUGH;
  case TargetTransformInfo::CacheLevel::L2D:
    return 8;
  }

  llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
}

unsigned X86TTIImpl::getNumberOfRegisters(bool Vector) {
  if (Vector && !ST->hasSSE1())
    return 0;

  if (ST->is64Bit()) {
    if (Vector && ST->hasAVX512())
      return 32;
    return 16;
  }
  return 8;
}

unsigned X86TTIImpl::getRegisterBitWidth(bool Vector) const {
  unsigned PreferVectorWidth = ST->getPreferVectorWidth();
  if (Vector) {
    if (ST->hasAVX512() && PreferVectorWidth >= 512)
      return 512;
    if (ST->hasAVX() && PreferVectorWidth >= 256)
      return 256;
    if (ST->hasSSE1() && PreferVectorWidth >= 128)
      return 128;
    return 0;
  }

  if (ST->is64Bit())
    return 64;

  return 32;
}

unsigned X86TTIImpl::getLoadStoreVecRegBitWidth(unsigned) const {
  return getRegisterBitWidth(true);
}

unsigned X86TTIImpl::getMaxInterleaveFactor(unsigned VF) {
  // If the loop will not be vectorized, don't interleave the loop.
  // Let regular unroll to unroll the loop, which saves the overflow
  // check and memory check cost.
  if (VF == 1)
    return 1;

  if (ST->isAtom())
    return 1;

  // Sandybridge and Haswell have multiple execution ports and pipelined
  // vector units.
  if (ST->hasAVX())
    return 4;

  return 2;
}

int X86TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty,
    TTI::OperandValueKind Op1Info, TTI::OperandValueKind Op2Info,
    TTI::OperandValueProperties Opd1PropInfo,
    TTI::OperandValueProperties Opd2PropInfo,
    ArrayRef<const Value *> Args) {
  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Ty);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  static const CostTblEntry GLMCostTable[] = {
    { ISD::FDIV,  MVT::f32,   18 }, // divss
    { ISD::FDIV,  MVT::v4f32, 35 }, // divps
    { ISD::FDIV,  MVT::f64,   33 }, // divsd
    { ISD::FDIV,  MVT::v2f64, 65 }, // divpd
  };

  if (ST->isGLM())
    if (const auto *Entry = CostTableLookup(GLMCostTable, ISD,
                                            LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SLMCostTable[] = {
    { ISD::MUL,   MVT::v4i32, 11 }, // pmulld
    { ISD::MUL,   MVT::v8i16, 2  }, // pmullw
    { ISD::MUL,   MVT::v16i8, 14 }, // extend/pmullw/trunc sequence.
    { ISD::FMUL,  MVT::f64,   2  }, // mulsd
    { ISD::FMUL,  MVT::v2f64, 4  }, // mulpd
    { ISD::FMUL,  MVT::v4f32, 2  }, // mulps
    { ISD::FDIV,  MVT::f32,   17 }, // divss
    { ISD::FDIV,  MVT::v4f32, 39 }, // divps
    { ISD::FDIV,  MVT::f64,   32 }, // divsd
    { ISD::FDIV,  MVT::v2f64, 69 }, // divpd
    { ISD::FADD,  MVT::v2f64, 2  }, // addpd
    { ISD::FSUB,  MVT::v2f64, 2  }, // subpd
    // v2i64/v4i64 mul is custom lowered as a series of long:
    // multiplies(3), shifts(3) and adds(2)
    // slm muldq version throughput is 2 and addq throughput 4
    // thus: 3X2 (muldq throughput) + 3X1 (shift throughput) +
    //       3X4 (addq throughput) = 17
    { ISD::MUL,   MVT::v2i64, 17 },
    // slm addq\subq throughput is 4
    { ISD::ADD,   MVT::v2i64, 4  },
    { ISD::SUB,   MVT::v2i64, 4  },
  };

  if (ST->isSLM()) {
    if (Args.size() == 2 && ISD == ISD::MUL && LT.second == MVT::v4i32) {
      // Check if the operands can be shrinked into a smaller datatype.
      bool Op1Signed = false;
      unsigned Op1MinSize = BaseT::minRequiredElementSize(Args[0], Op1Signed);
      bool Op2Signed = false;
      unsigned Op2MinSize = BaseT::minRequiredElementSize(Args[1], Op2Signed);

      bool signedMode = Op1Signed | Op2Signed;
      unsigned OpMinSize = std::max(Op1MinSize, Op2MinSize);

      if (OpMinSize <= 7)
        return LT.first * 3; // pmullw/sext
      if (!signedMode && OpMinSize <= 8)
        return LT.first * 3; // pmullw/zext
      if (OpMinSize <= 15)
        return LT.first * 5; // pmullw/pmulhw/pshuf
      if (!signedMode && OpMinSize <= 16)
        return LT.first * 5; // pmullw/pmulhw/pshuf
    }

    if (const auto *Entry = CostTableLookup(SLMCostTable, ISD,
                                            LT.second)) {
      return LT.first * Entry->Cost;
    }
  }

  if ((ISD == ISD::SDIV || ISD == ISD::SREM || ISD == ISD::UDIV ||
       ISD == ISD::UREM) &&
      (Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
       Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) &&
      Opd2PropInfo == TargetTransformInfo::OP_PowerOf2) {
    if (ISD == ISD::SDIV || ISD == ISD::SREM) {
      // On X86, vector signed division by constants power-of-two are
      // normally expanded to the sequence SRA + SRL + ADD + SRA.
      // The OperandValue properties may not be the same as that of the previous
      // operation; conservatively assume OP_None.
      int Cost =
          2 * getArithmeticInstrCost(Instruction::AShr, Ty, Op1Info, Op2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      Cost += getArithmeticInstrCost(Instruction::LShr, Ty, Op1Info, Op2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);
      Cost += getArithmeticInstrCost(Instruction::Add, Ty, Op1Info, Op2Info,
                                     TargetTransformInfo::OP_None,
                                     TargetTransformInfo::OP_None);

      if (ISD == ISD::SREM) {
        // For SREM: (X % C) is the equivalent of (X - (X/C)*C)
        Cost += getArithmeticInstrCost(Instruction::Mul, Ty, Op1Info, Op2Info);
        Cost += getArithmeticInstrCost(Instruction::Sub, Ty, Op1Info, Op2Info);
      }

      return Cost;
    }

    // Vector unsigned division/remainder will be simplified to shifts/masks.
    if (ISD == ISD::UDIV)
      return getArithmeticInstrCost(Instruction::LShr, Ty, Op1Info, Op2Info,
                                    TargetTransformInfo::OP_None,
                                    TargetTransformInfo::OP_None);

    if (ISD == ISD::UREM)
      return getArithmeticInstrCost(Instruction::And, Ty, Op1Info, Op2Info,
                                    TargetTransformInfo::OP_None,
                                    TargetTransformInfo::OP_None);
  }

  static const CostTblEntry AVX512BWUniformConstCostTable[] = {
    { ISD::SHL,  MVT::v64i8,   2 }, // psllw + pand.
    { ISD::SRL,  MVT::v64i8,   2 }, // psrlw + pand.
    { ISD::SRA,  MVT::v64i8,   4 }, // psrlw, pand, pxor, psubb.
  };

  if (Op2Info == TargetTransformInfo::OK_UniformConstantValue &&
      ST->hasBWI()) {
    if (const auto *Entry = CostTableLookup(AVX512BWUniformConstCostTable, ISD,
                                            LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX512UniformConstCostTable[] = {
    { ISD::SRA,  MVT::v2i64,   1 },
    { ISD::SRA,  MVT::v4i64,   1 },
    { ISD::SRA,  MVT::v8i64,   1 },
  };

  if (Op2Info == TargetTransformInfo::OK_UniformConstantValue &&
      ST->hasAVX512()) {
    if (const auto *Entry = CostTableLookup(AVX512UniformConstCostTable, ISD,
                                            LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX2UniformConstCostTable[] = {
    { ISD::SHL,  MVT::v32i8,   2 }, // psllw + pand.
    { ISD::SRL,  MVT::v32i8,   2 }, // psrlw + pand.
    { ISD::SRA,  MVT::v32i8,   4 }, // psrlw, pand, pxor, psubb.

    { ISD::SRA,  MVT::v4i64,   4 }, // 2 x psrad + shuffle.
  };

  if (Op2Info == TargetTransformInfo::OK_UniformConstantValue &&
      ST->hasAVX2()) {
    if (const auto *Entry = CostTableLookup(AVX2UniformConstCostTable, ISD,
                                            LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry SSE2UniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8,     2 }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8,     2 }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8,     4 }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v32i8,   4+2 }, // 2*(psllw + pand) + split.
    { ISD::SRL,  MVT::v32i8,   4+2 }, // 2*(psrlw + pand) + split.
    { ISD::SRA,  MVT::v32i8,   8+2 }, // 2*(psrlw, pand, pxor, psubb) + split.
  };

  // XOP has faster vXi8 shifts.
  if (Op2Info == TargetTransformInfo::OK_UniformConstantValue &&
      ST->hasSSE2() && !ST->hasXOP()) {
    if (const auto *Entry =
            CostTableLookup(SSE2UniformConstCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX512BWConstCostTable[] = {
    { ISD::SDIV, MVT::v64i8,  14 }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v64i8,  16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v64i8,  14 }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v64i8,  16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::SDIV, MVT::v32i16,  6 }, // vpmulhw sequence
    { ISD::SREM, MVT::v32i16,  8 }, // vpmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i16,  6 }, // vpmulhuw sequence
    { ISD::UREM, MVT::v32i16,  8 }, // vpmulhuw+mul+sub sequence
  };

  if ((Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
       Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) &&
      ST->hasBWI()) {
    if (const auto *Entry =
            CostTableLookup(AVX512BWConstCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX512ConstCostTable[] = {
    { ISD::SDIV, MVT::v16i32, 15 }, // vpmuldq sequence
    { ISD::SREM, MVT::v16i32, 17 }, // vpmuldq+mul+sub sequence
    { ISD::UDIV, MVT::v16i32, 15 }, // vpmuludq sequence
    { ISD::UREM, MVT::v16i32, 17 }, // vpmuludq+mul+sub sequence
  };

  if ((Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
       Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) &&
      ST->hasAVX512()) {
    if (const auto *Entry =
            CostTableLookup(AVX512ConstCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX2ConstCostTable[] = {
    { ISD::SDIV, MVT::v32i8,  14 }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v32i8,  16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i8,  14 }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v32i8,  16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::SDIV, MVT::v16i16,  6 }, // vpmulhw sequence
    { ISD::SREM, MVT::v16i16,  8 }, // vpmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v16i16,  6 }, // vpmulhuw sequence
    { ISD::UREM, MVT::v16i16,  8 }, // vpmulhuw+mul+sub sequence
    { ISD::SDIV, MVT::v8i32,  15 }, // vpmuldq sequence
    { ISD::SREM, MVT::v8i32,  19 }, // vpmuldq+mul+sub sequence
    { ISD::UDIV, MVT::v8i32,  15 }, // vpmuludq sequence
    { ISD::UREM, MVT::v8i32,  19 }, // vpmuludq+mul+sub sequence
  };

  if ((Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
       Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) &&
      ST->hasAVX2()) {
    if (const auto *Entry = CostTableLookup(AVX2ConstCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry SSE2ConstCostTable[] = {
    { ISD::SDIV, MVT::v32i8,  28+2 }, // 4*ext+4*pmulhw sequence + split.
    { ISD::SREM, MVT::v32i8,  32+2 }, // 4*ext+4*pmulhw+mul+sub sequence + split.
    { ISD::SDIV, MVT::v16i8,    14 }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v16i8,    16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i8,  28+2 }, // 4*ext+4*pmulhw sequence + split.
    { ISD::UREM, MVT::v32i8,  32+2 }, // 4*ext+4*pmulhw+mul+sub sequence + split.
    { ISD::UDIV, MVT::v16i8,    14 }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v16i8,    16 }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::SDIV, MVT::v16i16, 12+2 }, // 2*pmulhw sequence + split.
    { ISD::SREM, MVT::v16i16, 16+2 }, // 2*pmulhw+mul+sub sequence + split.
    { ISD::SDIV, MVT::v8i16,     6 }, // pmulhw sequence
    { ISD::SREM, MVT::v8i16,     8 }, // pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v16i16, 12+2 }, // 2*pmulhuw sequence + split.
    { ISD::UREM, MVT::v16i16, 16+2 }, // 2*pmulhuw+mul+sub sequence + split.
    { ISD::UDIV, MVT::v8i16,     6 }, // pmulhuw sequence
    { ISD::UREM, MVT::v8i16,     8 }, // pmulhuw+mul+sub sequence
    { ISD::SDIV, MVT::v8i32,  38+2 }, // 2*pmuludq sequence + split.
    { ISD::SREM, MVT::v8i32,  48+2 }, // 2*pmuludq+mul+sub sequence + split.
    { ISD::SDIV, MVT::v4i32,    19 }, // pmuludq sequence
    { ISD::SREM, MVT::v4i32,    24 }, // pmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v8i32,  30+2 }, // 2*pmuludq sequence + split.
    { ISD::UREM, MVT::v8i32,  40+2 }, // 2*pmuludq+mul+sub sequence + split.
    { ISD::UDIV, MVT::v4i32,    15 }, // pmuludq sequence
    { ISD::UREM, MVT::v4i32,    20 }, // pmuludq+mul+sub sequence
  };

  if ((Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
       Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) &&
      ST->hasSSE2()) {
    // pmuldq sequence.
    if (ISD == ISD::SDIV && LT.second == MVT::v8i32 && ST->hasAVX())
      return LT.first * 32;
    if (ISD == ISD::SREM && LT.second == MVT::v8i32 && ST->hasAVX())
      return LT.first * 38;
    if (ISD == ISD::SDIV && LT.second == MVT::v4i32 && ST->hasSSE41())
      return LT.first * 15;
    if (ISD == ISD::SREM && LT.second == MVT::v4i32 && ST->hasSSE41())
      return LT.first * 20;

    if (const auto *Entry = CostTableLookup(SSE2ConstCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX2UniformCostTable[] = {
    // Uniform splats are cheaper for the following instructions.
    { ISD::SHL,  MVT::v16i16, 1 }, // psllw.
    { ISD::SRL,  MVT::v16i16, 1 }, // psrlw.
    { ISD::SRA,  MVT::v16i16, 1 }, // psraw.
  };

  if (ST->hasAVX2() &&
      ((Op2Info == TargetTransformInfo::OK_UniformConstantValue) ||
       (Op2Info == TargetTransformInfo::OK_UniformValue))) {
    if (const auto *Entry =
            CostTableLookup(AVX2UniformCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry SSE2UniformCostTable[] = {
    // Uniform splats are cheaper for the following instructions.
    { ISD::SHL,  MVT::v8i16,  1 }, // psllw.
    { ISD::SHL,  MVT::v4i32,  1 }, // pslld
    { ISD::SHL,  MVT::v2i64,  1 }, // psllq.

    { ISD::SRL,  MVT::v8i16,  1 }, // psrlw.
    { ISD::SRL,  MVT::v4i32,  1 }, // psrld.
    { ISD::SRL,  MVT::v2i64,  1 }, // psrlq.

    { ISD::SRA,  MVT::v8i16,  1 }, // psraw.
    { ISD::SRA,  MVT::v4i32,  1 }, // psrad.
  };

  if (ST->hasSSE2() &&
      ((Op2Info == TargetTransformInfo::OK_UniformConstantValue) ||
       (Op2Info == TargetTransformInfo::OK_UniformValue))) {
    if (const auto *Entry =
            CostTableLookup(SSE2UniformCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry AVX512DQCostTable[] = {
    { ISD::MUL,  MVT::v2i64, 1 },
    { ISD::MUL,  MVT::v4i64, 1 },
    { ISD::MUL,  MVT::v8i64, 1 }
  };

  // Look for AVX512DQ lowering tricks for custom cases.
  if (ST->hasDQI())
    if (const auto *Entry = CostTableLookup(AVX512DQCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX512BWCostTable[] = {
    { ISD::SHL,   MVT::v8i16,      1 }, // vpsllvw
    { ISD::SRL,   MVT::v8i16,      1 }, // vpsrlvw
    { ISD::SRA,   MVT::v8i16,      1 }, // vpsravw

    { ISD::SHL,   MVT::v16i16,     1 }, // vpsllvw
    { ISD::SRL,   MVT::v16i16,     1 }, // vpsrlvw
    { ISD::SRA,   MVT::v16i16,     1 }, // vpsravw

    { ISD::SHL,   MVT::v32i16,     1 }, // vpsllvw
    { ISD::SRL,   MVT::v32i16,     1 }, // vpsrlvw
    { ISD::SRA,   MVT::v32i16,     1 }, // vpsravw

    { ISD::SHL,   MVT::v64i8,     11 }, // vpblendvb sequence.
    { ISD::SRL,   MVT::v64i8,     11 }, // vpblendvb sequence.
    { ISD::SRA,   MVT::v64i8,     24 }, // vpblendvb sequence.

    { ISD::MUL,   MVT::v64i8,     11 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,   MVT::v32i8,      4 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,   MVT::v16i8,      4 }, // extend/pmullw/trunc sequence.
  };

  // Look for AVX512BW lowering tricks for custom cases.
  if (ST->hasBWI())
    if (const auto *Entry = CostTableLookup(AVX512BWCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX512CostTable[] = {
    { ISD::SHL,     MVT::v16i32,     1 },
    { ISD::SRL,     MVT::v16i32,     1 },
    { ISD::SRA,     MVT::v16i32,     1 },

    { ISD::SHL,     MVT::v8i64,      1 },
    { ISD::SRL,     MVT::v8i64,      1 },

    { ISD::SRA,     MVT::v2i64,      1 },
    { ISD::SRA,     MVT::v4i64,      1 },
    { ISD::SRA,     MVT::v8i64,      1 },

    { ISD::MUL,     MVT::v32i8,     13 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,     MVT::v16i8,      5 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,     MVT::v16i32,     1 }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v8i32,      1 }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v4i32,      1 }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v8i64,      8 }, // 3*pmuludq/3*shift/2*add

    { ISD::FADD,    MVT::v8f64,      1 }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v8f64,      1 }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v8f64,      1 }, // Skylake from http://www.agner.org/

    { ISD::FADD,    MVT::v16f32,     1 }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v16f32,     1 }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v16f32,     1 }, // Skylake from http://www.agner.org/
  };

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX2ShiftCostTable[] = {
    // Shifts on v4i64/v8i32 on AVX2 is legal even though we declare to
    // customize them to detect the cases where shift amount is a scalar one.
    { ISD::SHL,     MVT::v4i32,    1 },
    { ISD::SRL,     MVT::v4i32,    1 },
    { ISD::SRA,     MVT::v4i32,    1 },
    { ISD::SHL,     MVT::v8i32,    1 },
    { ISD::SRL,     MVT::v8i32,    1 },
    { ISD::SRA,     MVT::v8i32,    1 },
    { ISD::SHL,     MVT::v2i64,    1 },
    { ISD::SRL,     MVT::v2i64,    1 },
    { ISD::SHL,     MVT::v4i64,    1 },
    { ISD::SRL,     MVT::v4i64,    1 },
  };

  // Look for AVX2 lowering tricks.
  if (ST->hasAVX2()) {
    if (ISD == ISD::SHL && LT.second == MVT::v16i16 &&
        (Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
         Op2Info == TargetTransformInfo::OK_NonUniformConstantValue))
      // On AVX2, a packed v16i16 shift left by a constant build_vector
      // is lowered into a vector multiply (vpmullw).
      return getArithmeticInstrCost(Instruction::Mul, Ty, Op1Info, Op2Info,
                                    TargetTransformInfo::OP_None,
                                    TargetTransformInfo::OP_None);

    if (const auto *Entry = CostTableLookup(AVX2ShiftCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry XOPShiftCostTable[] = {
    // 128bit shifts take 1cy, but right shifts require negation beforehand.
    { ISD::SHL,     MVT::v16i8,    1 },
    { ISD::SRL,     MVT::v16i8,    2 },
    { ISD::SRA,     MVT::v16i8,    2 },
    { ISD::SHL,     MVT::v8i16,    1 },
    { ISD::SRL,     MVT::v8i16,    2 },
    { ISD::SRA,     MVT::v8i16,    2 },
    { ISD::SHL,     MVT::v4i32,    1 },
    { ISD::SRL,     MVT::v4i32,    2 },
    { ISD::SRA,     MVT::v4i32,    2 },
    { ISD::SHL,     MVT::v2i64,    1 },
    { ISD::SRL,     MVT::v2i64,    2 },
    { ISD::SRA,     MVT::v2i64,    2 },
    // 256bit shifts require splitting if AVX2 didn't catch them above.
    { ISD::SHL,     MVT::v32i8,  2+2 },
    { ISD::SRL,     MVT::v32i8,  4+2 },
    { ISD::SRA,     MVT::v32i8,  4+2 },
    { ISD::SHL,     MVT::v16i16, 2+2 },
    { ISD::SRL,     MVT::v16i16, 4+2 },
    { ISD::SRA,     MVT::v16i16, 4+2 },
    { ISD::SHL,     MVT::v8i32,  2+2 },
    { ISD::SRL,     MVT::v8i32,  4+2 },
    { ISD::SRA,     MVT::v8i32,  4+2 },
    { ISD::SHL,     MVT::v4i64,  2+2 },
    { ISD::SRL,     MVT::v4i64,  4+2 },
    { ISD::SRA,     MVT::v4i64,  4+2 },
  };

  // Look for XOP lowering tricks.
  if (ST->hasXOP()) {
    // If the right shift is constant then we'll fold the negation so
    // it's as cheap as a left shift.
    int ShiftISD = ISD;
    if ((ShiftISD == ISD::SRL || ShiftISD == ISD::SRA) &&
        (Op2Info == TargetTransformInfo::OK_UniformConstantValue ||
         Op2Info == TargetTransformInfo::OK_NonUniformConstantValue))
      ShiftISD = ISD::SHL;
    if (const auto *Entry =
            CostTableLookup(XOPShiftCostTable, ShiftISD, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry SSE2UniformShiftCostTable[] = {
    // Uniform splats are cheaper for the following instructions.
    { ISD::SHL,  MVT::v16i16, 2+2 }, // 2*psllw + split.
    { ISD::SHL,  MVT::v8i32,  2+2 }, // 2*pslld + split.
    { ISD::SHL,  MVT::v4i64,  2+2 }, // 2*psllq + split.

    { ISD::SRL,  MVT::v16i16, 2+2 }, // 2*psrlw + split.
    { ISD::SRL,  MVT::v8i32,  2+2 }, // 2*psrld + split.
    { ISD::SRL,  MVT::v4i64,  2+2 }, // 2*psrlq + split.

    { ISD::SRA,  MVT::v16i16, 2+2 }, // 2*psraw + split.
    { ISD::SRA,  MVT::v8i32,  2+2 }, // 2*psrad + split.
    { ISD::SRA,  MVT::v2i64,    4 }, // 2*psrad + shuffle.
    { ISD::SRA,  MVT::v4i64,  8+2 }, // 2*(2*psrad + shuffle) + split.
  };

  if (ST->hasSSE2() &&
      ((Op2Info == TargetTransformInfo::OK_UniformConstantValue) ||
       (Op2Info == TargetTransformInfo::OK_UniformValue))) {

    // Handle AVX2 uniform v4i64 ISD::SRA, it's not worth a table.
    if (ISD == ISD::SRA && LT.second == MVT::v4i64 && ST->hasAVX2())
      return LT.first * 4; // 2*psrad + shuffle.

    if (const auto *Entry =
            CostTableLookup(SSE2UniformShiftCostTable, ISD, LT.second))
      return LT.first * Entry->Cost;
  }

  if (ISD == ISD::SHL &&
      Op2Info == TargetTransformInfo::OK_NonUniformConstantValue) {
    MVT VT = LT.second;
    // Vector shift left by non uniform constant can be lowered
    // into vector multiply.
    if (((VT == MVT::v8i16 || VT == MVT::v4i32) && ST->hasSSE2()) ||
        ((VT == MVT::v16i16 || VT == MVT::v8i32) && ST->hasAVX()))
      ISD = ISD::MUL;
  }

  static const CostTblEntry AVX2CostTable[] = {
    { ISD::SHL,  MVT::v32i8,     11 }, // vpblendvb sequence.
    { ISD::SHL,  MVT::v16i16,    10 }, // extend/vpsrlvd/pack sequence.

    { ISD::SRL,  MVT::v32i8,     11 }, // vpblendvb sequence.
    { ISD::SRL,  MVT::v16i16,    10 }, // extend/vpsrlvd/pack sequence.

    { ISD::SRA,  MVT::v32i8,     24 }, // vpblendvb sequence.
    { ISD::SRA,  MVT::v16i16,    10 }, // extend/vpsravd/pack sequence.
    { ISD::SRA,  MVT::v2i64,      4 }, // srl/xor/sub sequence.
    { ISD::SRA,  MVT::v4i64,      4 }, // srl/xor/sub sequence.

    { ISD::SUB,  MVT::v32i8,      1 }, // psubb
    { ISD::ADD,  MVT::v32i8,      1 }, // paddb
    { ISD::SUB,  MVT::v16i16,     1 }, // psubw
    { ISD::ADD,  MVT::v16i16,     1 }, // paddw
    { ISD::SUB,  MVT::v8i32,      1 }, // psubd
    { ISD::ADD,  MVT::v8i32,      1 }, // paddd
    { ISD::SUB,  MVT::v4i64,      1 }, // psubq
    { ISD::ADD,  MVT::v4i64,      1 }, // paddq

    { ISD::MUL,  MVT::v32i8,     17 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,  MVT::v16i8,      7 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,  MVT::v16i16,     1 }, // pmullw
    { ISD::MUL,  MVT::v8i32,      2 }, // pmulld (Haswell from agner.org)
    { ISD::MUL,  MVT::v4i64,      8 }, // 3*pmuludq/3*shift/2*add

    { ISD::FADD, MVT::v4f64,      1 }, // Haswell from http://www.agner.org/
    { ISD::FADD, MVT::v8f32,      1 }, // Haswell from http://www.agner.org/
    { ISD::FSUB, MVT::v4f64,      1 }, // Haswell from http://www.agner.org/
    { ISD::FSUB, MVT::v8f32,      1 }, // Haswell from http://www.agner.org/
    { ISD::FMUL, MVT::v4f64,      1 }, // Haswell from http://www.agner.org/
    { ISD::FMUL, MVT::v8f32,      1 }, // Haswell from http://www.agner.org/

    { ISD::FDIV, MVT::f32,        7 }, // Haswell from http://www.agner.org/
    { ISD::FDIV, MVT::v4f32,      7 }, // Haswell from http://www.agner.org/
    { ISD::FDIV, MVT::v8f32,     14 }, // Haswell from http://www.agner.org/
    { ISD::FDIV, MVT::f64,       14 }, // Haswell from http://www.agner.org/
    { ISD::FDIV, MVT::v2f64,     14 }, // Haswell from http://www.agner.org/
    { ISD::FDIV, MVT::v4f64,     28 }, // Haswell from http://www.agner.org/
  };

  // Look for AVX2 lowering tricks for custom cases.
  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX1CostTable[] = {
    // We don't have to scalarize unsupported ops. We can issue two half-sized
    // operations and we only need to extract the upper YMM half.
    // Two ops + 1 extract + 1 insert = 4.
    { ISD::MUL,     MVT::v16i16,     4 },
    { ISD::MUL,     MVT::v8i32,      4 },
    { ISD::SUB,     MVT::v32i8,      4 },
    { ISD::ADD,     MVT::v32i8,      4 },
    { ISD::SUB,     MVT::v16i16,     4 },
    { ISD::ADD,     MVT::v16i16,     4 },
    { ISD::SUB,     MVT::v8i32,      4 },
    { ISD::ADD,     MVT::v8i32,      4 },
    { ISD::SUB,     MVT::v4i64,      4 },
    { ISD::ADD,     MVT::v4i64,      4 },

    // A v4i64 multiply is custom lowered as two split v2i64 vectors that then
    // are lowered as a series of long multiplies(3), shifts(3) and adds(2)
    // Because we believe v4i64 to be a legal type, we must also include the
    // extract+insert in the cost table. Therefore, the cost here is 18
    // instead of 8.
    { ISD::MUL,     MVT::v4i64,     18 },

    { ISD::MUL,     MVT::v32i8,     26 }, // extend/pmullw/trunc sequence.

    { ISD::FDIV,    MVT::f32,       14 }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f32,     14 }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v8f32,     28 }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::f64,       22 }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v2f64,     22 }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f64,     44 }, // SNB from http://www.agner.org/
  };

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE42CostTable[] = {
    { ISD::FADD, MVT::f64,     1 }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::f32,     1 }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::v2f64,   1 }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::v4f32,   1 }, // Nehalem from http://www.agner.org/

    { ISD::FSUB, MVT::f64,     1 }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::f32 ,    1 }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::v2f64,   1 }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::v4f32,   1 }, // Nehalem from http://www.agner.org/

    { ISD::FMUL, MVT::f64,     1 }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::f32,     1 }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::v2f64,   1 }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::v4f32,   1 }, // Nehalem from http://www.agner.org/

    { ISD::FDIV,  MVT::f32,   14 }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::v4f32, 14 }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::f64,   22 }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::v2f64, 22 }, // Nehalem from http://www.agner.org/
  };

  if (ST->hasSSE42())
    if (const auto *Entry = CostTableLookup(SSE42CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE41CostTable[] = {
    { ISD::SHL,  MVT::v16i8,      11 }, // pblendvb sequence.
    { ISD::SHL,  MVT::v32i8,  2*11+2 }, // pblendvb sequence + split.
    { ISD::SHL,  MVT::v8i16,      14 }, // pblendvb sequence.
    { ISD::SHL,  MVT::v16i16, 2*14+2 }, // pblendvb sequence + split.
    { ISD::SHL,  MVT::v4i32,       4 }, // pslld/paddd/cvttps2dq/pmulld
    { ISD::SHL,  MVT::v8i32,   2*4+2 }, // pslld/paddd/cvttps2dq/pmulld + split

    { ISD::SRL,  MVT::v16i8,      12 }, // pblendvb sequence.
    { ISD::SRL,  MVT::v32i8,  2*12+2 }, // pblendvb sequence + split.
    { ISD::SRL,  MVT::v8i16,      14 }, // pblendvb sequence.
    { ISD::SRL,  MVT::v16i16, 2*14+2 }, // pblendvb sequence + split.
    { ISD::SRL,  MVT::v4i32,      11 }, // Shift each lane + blend.
    { ISD::SRL,  MVT::v8i32,  2*11+2 }, // Shift each lane + blend + split.

    { ISD::SRA,  MVT::v16i8,      24 }, // pblendvb sequence.
    { ISD::SRA,  MVT::v32i8,  2*24+2 }, // pblendvb sequence + split.
    { ISD::SRA,  MVT::v8i16,      14 }, // pblendvb sequence.
    { ISD::SRA,  MVT::v16i16, 2*14+2 }, // pblendvb sequence + split.
    { ISD::SRA,  MVT::v4i32,      12 }, // Shift each lane + blend.
    { ISD::SRA,  MVT::v8i32,  2*12+2 }, // Shift each lane + blend + split.

    { ISD::MUL,  MVT::v4i32,       2 }  // pmulld (Nehalem from agner.org)
  };

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE2CostTable[] = {
    // We don't correctly identify costs of casts because they are marked as
    // custom.
    { ISD::SHL,  MVT::v16i8,      26 }, // cmpgtb sequence.
    { ISD::SHL,  MVT::v8i16,      32 }, // cmpgtb sequence.
    { ISD::SHL,  MVT::v4i32,     2*5 }, // We optimized this using mul.
    { ISD::SHL,  MVT::v2i64,       4 }, // splat+shuffle sequence.
    { ISD::SHL,  MVT::v4i64,   2*4+2 }, // splat+shuffle sequence + split.

    { ISD::SRL,  MVT::v16i8,      26 }, // cmpgtb sequence.
    { ISD::SRL,  MVT::v8i16,      32 }, // cmpgtb sequence.
    { ISD::SRL,  MVT::v4i32,      16 }, // Shift each lane + blend.
    { ISD::SRL,  MVT::v2i64,       4 }, // splat+shuffle sequence.
    { ISD::SRL,  MVT::v4i64,   2*4+2 }, // splat+shuffle sequence + split.

    { ISD::SRA,  MVT::v16i8,      54 }, // unpacked cmpgtb sequence.
    { ISD::SRA,  MVT::v8i16,      32 }, // cmpgtb sequence.
    { ISD::SRA,  MVT::v4i32,      16 }, // Shift each lane + blend.
    { ISD::SRA,  MVT::v2i64,      12 }, // srl/xor/sub sequence.
    { ISD::SRA,  MVT::v4i64,  2*12+2 }, // srl/xor/sub sequence+split.

    { ISD::MUL,  MVT::v16i8,      12 }, // extend/pmullw/trunc sequence.
    { ISD::MUL,  MVT::v8i16,       1 }, // pmullw
    { ISD::MUL,  MVT::v4i32,       6 }, // 3*pmuludq/4*shuffle
    { ISD::MUL,  MVT::v2i64,       8 }, // 3*pmuludq/3*shift/2*add

    { ISD::FDIV, MVT::f32,        23 }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::v4f32,      39 }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::f64,        38 }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::v2f64,      69 }, // Pentium IV from http://www.agner.org/

    { ISD::FADD, MVT::f32,         2 }, // Pentium IV from http://www.agner.org/
    { ISD::FADD, MVT::f64,         2 }, // Pentium IV from http://www.agner.org/

    { ISD::FSUB, MVT::f32,         2 }, // Pentium IV from http://www.agner.org/
    { ISD::FSUB, MVT::f64,         2 }, // Pentium IV from http://www.agner.org/
  };

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE1CostTable[] = {
    { ISD::FDIV, MVT::f32,   17 }, // Pentium III from http://www.agner.org/
    { ISD::FDIV, MVT::v4f32, 34 }, // Pentium III from http://www.agner.org/

    { ISD::FADD, MVT::f32,    1 }, // Pentium III from http://www.agner.org/
    { ISD::FADD, MVT::v4f32,  2 }, // Pentium III from http://www.agner.org/

    { ISD::FSUB, MVT::f32,    1 }, // Pentium III from http://www.agner.org/
    { ISD::FSUB, MVT::v4f32,  2 }, // Pentium III from http://www.agner.org/

    { ISD::ADD, MVT::i8,      1 }, // Pentium III from http://www.agner.org/
    { ISD::ADD, MVT::i16,     1 }, // Pentium III from http://www.agner.org/
    { ISD::ADD, MVT::i32,     1 }, // Pentium III from http://www.agner.org/

    { ISD::SUB, MVT::i8,      1 }, // Pentium III from http://www.agner.org/
    { ISD::SUB, MVT::i16,     1 }, // Pentium III from http://www.agner.org/
    { ISD::SUB, MVT::i32,     1 }, // Pentium III from http://www.agner.org/
  };

  if (ST->hasSSE1())
    if (const auto *Entry = CostTableLookup(SSE1CostTable, ISD, LT.second))
      return LT.first * Entry->Cost;

  // It is not a good idea to vectorize division. We have to scalarize it and
  // in the process we will often end up having to spilling regular
  // registers. The overhead of division is going to dominate most kernels
  // anyways so try hard to prevent vectorization of division - it is
  // generally a bad idea. Assume somewhat arbitrarily that we have to be able
  // to hide "20 cycles" for each lane.
  if (LT.second.isVector() && (ISD == ISD::SDIV || ISD == ISD::SREM ||
                               ISD == ISD::UDIV || ISD == ISD::UREM)) {
    int ScalarCost = getArithmeticInstrCost(
        Opcode, Ty->getScalarType(), Op1Info, Op2Info,
        TargetTransformInfo::OP_None, TargetTransformInfo::OP_None);
    return 20 * LT.first * LT.second.getVectorNumElements() * ScalarCost;
  }

  // Fallback to the default implementation.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, Op1Info, Op2Info);
}

int X86TTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                               Type *SubTp) {
  // 64-bit packed float vectors (v2f32) are widened to type v4f32.
  // 64-bit packed integer vectors (v2i32) are promoted to type v2i64.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Tp);

  // Treat Transpose as 2-op shuffles - there's no difference in lowering.
  if (Kind == TTI::SK_Transpose)
    Kind = TTI::SK_PermuteTwoSrc;

  // For Broadcasts we are splatting the first element from the first input
  // register, so only need to reference that input and all the output
  // registers are the same.
  if (Kind == TTI::SK_Broadcast)
    LT.first = 1;

  // Subvector extractions are free if they start at the beginning of a
  // vector and cheap if the subvectors are aligned.
  if (Kind == TTI::SK_ExtractSubvector && LT.second.isVector()) {
    int NumElts = LT.second.getVectorNumElements();
    if ((Index % NumElts) == 0)
      return 0;
    std::pair<int, MVT> SubLT = TLI->getTypeLegalizationCost(DL, SubTp);
    if (SubLT.second.isVector()) {
      int NumSubElts = SubLT.second.getVectorNumElements();
      if ((Index % NumSubElts) == 0 && (NumElts % NumSubElts) == 0)
        return SubLT.first;
    }
  }

  // We are going to permute multiple sources and the result will be in multiple
  // destinations. Providing an accurate cost only for splits where the element
  // type remains the same.
  if (Kind == TTI::SK_PermuteSingleSrc && LT.first != 1) {
    MVT LegalVT = LT.second;
    if (LegalVT.isVector() &&
        LegalVT.getVectorElementType().getSizeInBits() ==
            Tp->getVectorElementType()->getPrimitiveSizeInBits() &&
        LegalVT.getVectorNumElements() < Tp->getVectorNumElements()) {

      unsigned VecTySize = DL.getTypeStoreSize(Tp);
      unsigned LegalVTSize = LegalVT.getStoreSize();
      // Number of source vectors after legalization:
      unsigned NumOfSrcs = (VecTySize + LegalVTSize - 1) / LegalVTSize;
      // Number of destination vectors after legalization:
      unsigned NumOfDests = LT.first;

      Type *SingleOpTy = VectorType::get(Tp->getVectorElementType(),
                                         LegalVT.getVectorNumElements());

      unsigned NumOfShuffles = (NumOfSrcs - 1) * NumOfDests;
      return NumOfShuffles *
             getShuffleCost(TTI::SK_PermuteTwoSrc, SingleOpTy, 0, nullptr);
    }

    return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
  }

  // For 2-input shuffles, we must account for splitting the 2 inputs into many.
  if (Kind == TTI::SK_PermuteTwoSrc && LT.first != 1) {
    // We assume that source and destination have the same vector type.
    int NumOfDests = LT.first;
    int NumOfShufflesPerDest = LT.first * 2 - 1;
    LT.first = NumOfDests * NumOfShufflesPerDest;
  }

  static const CostTblEntry AVX512VBMIShuffleTbl[] = {
      {TTI::SK_Reverse, MVT::v64i8, 1}, // vpermb
      {TTI::SK_Reverse, MVT::v32i8, 1}, // vpermb

      {TTI::SK_PermuteSingleSrc, MVT::v64i8, 1}, // vpermb
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 1}, // vpermb

      {TTI::SK_PermuteTwoSrc, MVT::v64i8, 1}, // vpermt2b
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 1}, // vpermt2b
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 1}  // vpermt2b
  };

  if (ST->hasVBMI())
    if (const auto *Entry =
            CostTableLookup(AVX512VBMIShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX512BWShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v32i16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v64i8, 1},  // vpbroadcastb

      {TTI::SK_Reverse, MVT::v32i16, 1}, // vpermw
      {TTI::SK_Reverse, MVT::v16i16, 1}, // vpermw
      {TTI::SK_Reverse, MVT::v64i8, 2},  // pshufb + vshufi64x2

      {TTI::SK_PermuteSingleSrc, MVT::v32i16, 1}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 1}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v8i16, 1},  // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v64i8, 8},  // extend to v32i16
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 3},  // vpermw + zext/trunc

      {TTI::SK_PermuteTwoSrc, MVT::v32i16, 1}, // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 1}, // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 1},  // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 3},  // zext + vpermt2w + trunc
      {TTI::SK_PermuteTwoSrc, MVT::v64i8, 19}, // 6 * v32i8 + 1
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 3}   // zext + vpermt2w + trunc
  };

  if (ST->hasBWI())
    if (const auto *Entry =
            CostTableLookup(AVX512BWShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX512ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v8f64, 1},  // vbroadcastpd
      {TTI::SK_Broadcast, MVT::v16f32, 1}, // vbroadcastps
      {TTI::SK_Broadcast, MVT::v8i64, 1},  // vpbroadcastq
      {TTI::SK_Broadcast, MVT::v16i32, 1}, // vpbroadcastd

      {TTI::SK_Reverse, MVT::v8f64, 1},  // vpermpd
      {TTI::SK_Reverse, MVT::v16f32, 1}, // vpermps
      {TTI::SK_Reverse, MVT::v8i64, 1},  // vpermq
      {TTI::SK_Reverse, MVT::v16i32, 1}, // vpermd

      {TTI::SK_PermuteSingleSrc, MVT::v8f64, 1},  // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 1},  // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v2f64, 1},  // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v16f32, 1}, // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 1},  // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v4f32, 1},  // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v8i64, 1},  // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 1},  // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v2i64, 1},  // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v16i32, 1}, // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 1},  // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v4i32, 1},  // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v16i8, 1},  // pshufb

      {TTI::SK_PermuteTwoSrc, MVT::v8f64, 1},  // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v16f32, 1}, // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v8i64, 1},  // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v16i32, 1}, // vpermt2d
      {TTI::SK_PermuteTwoSrc, MVT::v4f64, 1},  // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32, 1},  // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v4i64, 1},  // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v8i32, 1},  // vpermt2d
      {TTI::SK_PermuteTwoSrc, MVT::v2f64, 1},  // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v4f32, 1},  // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v2i64, 1},  // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v4i32, 1}   // vpermt2d
  };

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX2ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v4f64, 1},  // vbroadcastpd
      {TTI::SK_Broadcast, MVT::v8f32, 1},  // vbroadcastps
      {TTI::SK_Broadcast, MVT::v4i64, 1},  // vpbroadcastq
      {TTI::SK_Broadcast, MVT::v8i32, 1},  // vpbroadcastd
      {TTI::SK_Broadcast, MVT::v16i16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v32i8, 1},  // vpbroadcastb

      {TTI::SK_Reverse, MVT::v4f64, 1},  // vpermpd
      {TTI::SK_Reverse, MVT::v8f32, 1},  // vpermps
      {TTI::SK_Reverse, MVT::v4i64, 1},  // vpermq
      {TTI::SK_Reverse, MVT::v8i32, 1},  // vpermd
      {TTI::SK_Reverse, MVT::v16i16, 2}, // vperm2i128 + pshufb
      {TTI::SK_Reverse, MVT::v32i8, 2},  // vperm2i128 + pshufb

      {TTI::SK_Select, MVT::v16i16, 1}, // vpblendvb
      {TTI::SK_Select, MVT::v32i8, 1},  // vpblendvb

      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 1},  // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 1},  // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 1},  // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 1},  // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 4}, // vperm2i128 + 2*vpshufb
                                                  // + vpblendvb
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 4},  // vperm2i128 + 2*vpshufb
                                                  // + vpblendvb

      {TTI::SK_PermuteTwoSrc, MVT::v4f64, 3},  // 2*vpermpd + vblendpd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32, 3},  // 2*vpermps + vblendps
      {TTI::SK_PermuteTwoSrc, MVT::v4i64, 3},  // 2*vpermq + vpblendd
      {TTI::SK_PermuteTwoSrc, MVT::v8i32, 3},  // 2*vpermd + vpblendd
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 7}, // 2*vperm2i128 + 4*vpshufb
                                               // + vpblendvb
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 7},  // 2*vperm2i128 + 4*vpshufb
                                               // + vpblendvb
  };

  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry XOPShuffleTbl[] = {
      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 2},  // vperm2f128 + vpermil2pd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 2},  // vperm2f128 + vpermil2ps
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 2},  // vperm2f128 + vpermil2pd
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 2},  // vperm2f128 + vpermil2ps
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 4}, // vextractf128 + 2*vpperm
                                                  // + vinsertf128
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 4},  // vextractf128 + 2*vpperm
                                                  // + vinsertf128

      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 9}, // 2*vextractf128 + 6*vpperm
                                               // + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 1},  // vpperm
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 9},  // 2*vextractf128 + 6*vpperm
                                               // + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 1},  // vpperm
  };

  if (ST->hasXOP())
    if (const auto *Entry = CostTableLookup(XOPShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX1ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v4f64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Broadcast, MVT::v8f32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Broadcast, MVT::v4i64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Broadcast, MVT::v8i32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Broadcast, MVT::v16i16, 3}, // vpshuflw + vpshufd + vinsertf128
      {TTI::SK_Broadcast, MVT::v32i8, 2},  // vpshufb + vinsertf128

      {TTI::SK_Reverse, MVT::v4f64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Reverse, MVT::v8f32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Reverse, MVT::v4i64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Reverse, MVT::v8i32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Reverse, MVT::v16i16, 4}, // vextractf128 + 2*pshufb
                                         // + vinsertf128
      {TTI::SK_Reverse, MVT::v32i8, 4},  // vextractf128 + 2*pshufb
                                         // + vinsertf128

      {TTI::SK_Select, MVT::v4i64, 1},  // vblendpd
      {TTI::SK_Select, MVT::v4f64, 1},  // vblendpd
      {TTI::SK_Select, MVT::v8i32, 1},  // vblendps
      {TTI::SK_Select, MVT::v8f32, 1},  // vblendps
      {TTI::SK_Select, MVT::v16i16, 3}, // vpand + vpandn + vpor
      {TTI::SK_Select, MVT::v32i8, 3},  // vpand + vpandn + vpor

      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 2},  // vperm2f128 + vshufpd
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 2},  // vperm2f128 + vshufpd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 4},  // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 4},  // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 8}, // vextractf128 + 4*pshufb
                                                  // + 2*por + vinsertf128
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 8},  // vextractf128 + 4*pshufb
                                                  // + 2*por + vinsertf128

      {TTI::SK_PermuteTwoSrc, MVT::v4f64, 3},   // 2*vperm2f128 + vshufpd
      {TTI::SK_PermuteTwoSrc, MVT::v4i64, 3},   // 2*vperm2f128 + vshufpd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32, 4},   // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteTwoSrc, MVT::v8i32, 4},   // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 15}, // 2*vextractf128 + 8*pshufb
                                                // + 4*por + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 15},  // 2*vextractf128 + 8*pshufb
                                                // + 4*por + vinsertf128
  };

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE41ShuffleTbl[] = {
      {TTI::SK_Select, MVT::v2i64, 1}, // pblendw
      {TTI::SK_Select, MVT::v2f64, 1}, // movsd
      {TTI::SK_Select, MVT::v4i32, 1}, // pblendw
      {TTI::SK_Select, MVT::v4f32, 1}, // blendps
      {TTI::SK_Select, MVT::v8i16, 1}, // pblendw
      {TTI::SK_Select, MVT::v16i8, 1}  // pblendvb
  };

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSSE3ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v8i16, 1}, // pshufb
      {TTI::SK_Broadcast, MVT::v16i8, 1}, // pshufb

      {TTI::SK_Reverse, MVT::v8i16, 1}, // pshufb
      {TTI::SK_Reverse, MVT::v16i8, 1}, // pshufb

      {TTI::SK_Select, MVT::v8i16, 3}, // 2*pshufb + por
      {TTI::SK_Select, MVT::v16i8, 3}, // 2*pshufb + por

      {TTI::SK_PermuteSingleSrc, MVT::v8i16, 1}, // pshufb
      {TTI::SK_PermuteSingleSrc, MVT::v16i8, 1}, // pshufb

      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 3}, // 2*pshufb + por
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 3}, // 2*pshufb + por
  };

  if (ST->hasSSSE3())
    if (const auto *Entry = CostTableLookup(SSSE3ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE2ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v2f64, 1}, // shufpd
      {TTI::SK_Broadcast, MVT::v2i64, 1}, // pshufd
      {TTI::SK_Broadcast, MVT::v4i32, 1}, // pshufd
      {TTI::SK_Broadcast, MVT::v8i16, 2}, // pshuflw + pshufd
      {TTI::SK_Broadcast, MVT::v16i8, 3}, // unpck + pshuflw + pshufd

      {TTI::SK_Reverse, MVT::v2f64, 1}, // shufpd
      {TTI::SK_Reverse, MVT::v2i64, 1}, // pshufd
      {TTI::SK_Reverse, MVT::v4i32, 1}, // pshufd
      {TTI::SK_Reverse, MVT::v8i16, 3}, // pshuflw + pshufhw + pshufd
      {TTI::SK_Reverse, MVT::v16i8, 9}, // 2*pshuflw + 2*pshufhw
                                        // + 2*pshufd + 2*unpck + packus

      {TTI::SK_Select, MVT::v2i64, 1}, // movsd
      {TTI::SK_Select, MVT::v2f64, 1}, // movsd
      {TTI::SK_Select, MVT::v4i32, 2}, // 2*shufps
      {TTI::SK_Select, MVT::v8i16, 3}, // pand + pandn + por
      {TTI::SK_Select, MVT::v16i8, 3}, // pand + pandn + por

      {TTI::SK_PermuteSingleSrc, MVT::v2f64, 1}, // shufpd
      {TTI::SK_PermuteSingleSrc, MVT::v2i64, 1}, // pshufd
      {TTI::SK_PermuteSingleSrc, MVT::v4i32, 1}, // pshufd
      {TTI::SK_PermuteSingleSrc, MVT::v8i16, 5}, // 2*pshuflw + 2*pshufhw
                                                  // + pshufd/unpck
    { TTI::SK_PermuteSingleSrc, MVT::v16i8, 10 }, // 2*pshuflw + 2*pshufhw
                                                  // + 2*pshufd + 2*unpck + 2*packus

    { TTI::SK_PermuteTwoSrc,    MVT::v2f64,  1 }, // shufpd
    { TTI::SK_PermuteTwoSrc,    MVT::v2i64,  1 }, // shufpd
    { TTI::SK_PermuteTwoSrc,    MVT::v4i32,  2 }, // 2*{unpck,movsd,pshufd}
    { TTI::SK_PermuteTwoSrc,    MVT::v8i16,  8 }, // blend+permute
    { TTI::SK_PermuteTwoSrc,    MVT::v16i8, 13 }, // blend+permute
  };

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE1ShuffleTbl[] = {
    { TTI::SK_Broadcast,        MVT::v4f32, 1 }, // shufps
    { TTI::SK_Reverse,          MVT::v4f32, 1 }, // shufps
    { TTI::SK_Select,           MVT::v4f32, 2 }, // 2*shufps
    { TTI::SK_PermuteSingleSrc, MVT::v4f32, 1 }, // shufps
    { TTI::SK_PermuteTwoSrc,    MVT::v4f32, 2 }, // 2*shufps
  };

  if (ST->hasSSE1())
    if (const auto *Entry = CostTableLookup(SSE1ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
}

int X86TTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                 const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // FIXME: Need a better design of the cost table to handle non-simple types of
  // potential massive combinations (elem_num x src_type x dst_type).

  static const TypeConversionCostTblEntry AVX512BWConversionTbl[] {
    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v32i8, 1 },
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v32i8, 1 },

    // Mask sign extend has an instruction.
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i1,  1 },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v16i1, 1 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1, 1 },
    { ISD::SIGN_EXTEND, MVT::v32i8,  MVT::v32i1, 1 },
    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v32i1, 1 },
    { ISD::SIGN_EXTEND, MVT::v64i8,  MVT::v64i1, 1 },

    // Mask zero extend is a load + broadcast.
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i1,  2 },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v16i1, 2 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1, 2 },
    { ISD::ZERO_EXTEND, MVT::v32i8,  MVT::v32i1, 2 },
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v32i1, 2 },
    { ISD::ZERO_EXTEND, MVT::v64i8,  MVT::v64i1, 2 },
  };

  static const TypeConversionCostTblEntry AVX512DQConversionTbl[] = {
    { ISD::SINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  1 },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  1 },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  1 },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  1 },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i64,  1 },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  1 },

    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  1 },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  1 },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  1 },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  1 },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i64,  1 },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  1 },

    { ISD::FP_TO_SINT,  MVT::v2i64,  MVT::v2f32,  1 },
    { ISD::FP_TO_SINT,  MVT::v4i64,  MVT::v4f32,  1 },
    { ISD::FP_TO_SINT,  MVT::v8i64,  MVT::v8f32,  1 },
    { ISD::FP_TO_SINT,  MVT::v2i64,  MVT::v2f64,  1 },
    { ISD::FP_TO_SINT,  MVT::v4i64,  MVT::v4f64,  1 },
    { ISD::FP_TO_SINT,  MVT::v8i64,  MVT::v8f64,  1 },

    { ISD::FP_TO_UINT,  MVT::v2i64,  MVT::v2f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v4i64,  MVT::v4f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v8i64,  MVT::v8f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v2i64,  MVT::v2f64,  1 },
    { ISD::FP_TO_UINT,  MVT::v4i64,  MVT::v4f64,  1 },
    { ISD::FP_TO_UINT,  MVT::v8i64,  MVT::v8f64,  1 },
  };

  // TODO: For AVX512DQ + AVX512VL, we also have cheap casts for 128-bit and
  // 256-bit wide vectors.

  static const TypeConversionCostTblEntry AVX512FConversionTbl[] = {
    { ISD::FP_EXTEND, MVT::v8f64,   MVT::v8f32,  1 },
    { ISD::FP_EXTEND, MVT::v8f64,   MVT::v16f32, 3 },
    { ISD::FP_ROUND,  MVT::v8f32,   MVT::v8f64,  1 },

    { ISD::TRUNCATE,  MVT::v16i8,   MVT::v16i32, 1 },
    { ISD::TRUNCATE,  MVT::v16i16,  MVT::v16i32, 1 },
    { ISD::TRUNCATE,  MVT::v8i16,   MVT::v8i64,  1 },
    { ISD::TRUNCATE,  MVT::v8i32,   MVT::v8i64,  1 },

    // v16i1 -> v16i32 - load + broadcast
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i1,  2 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i1,  2 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8,  1 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8,  1 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i16, 1 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i16, 1 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i16,  1 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i16,  1 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i32,  1 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i32,  1 },

    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i1,   4 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i1,  3 },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i8,   2 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i8,  2 },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i16,  2 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i16, 2 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i32, 1 },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  1 },

    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i1,   4 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i1,  3 },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i8,   2 },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i8,   2 },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i8,   2 },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i8,   2 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i8,  2 },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i16,  5 },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i16,  2 },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  2 },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i16,  2 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  2 },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i32,  1 },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  1 },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  1 },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  1 },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  1 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i32, 1 },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  5 },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i64, 26 },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  5 },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  5 },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  5 },

    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    1 },

    { ISD::FP_TO_UINT,  MVT::v2i32,  MVT::v2f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f64,  1 },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f32,  1 },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v8f64,  2 },
    { ISD::FP_TO_UINT,  MVT::v8i8,   MVT::v8f64,  2 },
    { ISD::FP_TO_UINT,  MVT::v16i32, MVT::v16f32, 1 },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v16f32, 2 },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v16f32, 2 },
  };

  static const TypeConversionCostTblEntry AVX2ConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   3 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   3 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i8,   3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i8,   3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,   3 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,   3 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  1 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  1 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i16,  3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i16,  3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  1 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  1 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32,  1 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32,  1 },

    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i64,  2 },
    { ISD::TRUNCATE,    MVT::v4i16,  MVT::v4i64,  2 },
    { ISD::TRUNCATE,    MVT::v4i32,  MVT::v4i64,  2 },
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i32,  2 },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  2 },
    { ISD::TRUNCATE,    MVT::v8i32,  MVT::v8i64,  4 },

    { ISD::FP_EXTEND,   MVT::v8f64,  MVT::v8f32,  3 },
    { ISD::FP_ROUND,    MVT::v8f32,  MVT::v8f64,  3 },

    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  8 },
  };

  static const TypeConversionCostTblEntry AVXConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,  6 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,  4 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,  7 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,  4 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i8,  6 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i8,  4 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,  7 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,  4 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8, 4 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8, 4 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i16, 6 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i16, 3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16, 4 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16, 4 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32, 4 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32, 4 },

    { ISD::TRUNCATE,    MVT::v16i8, MVT::v16i16, 4 },
    { ISD::TRUNCATE,    MVT::v8i8,  MVT::v8i32,  4 },
    { ISD::TRUNCATE,    MVT::v8i16, MVT::v8i32,  5 },
    { ISD::TRUNCATE,    MVT::v4i8,  MVT::v4i64,  4 },
    { ISD::TRUNCATE,    MVT::v4i16, MVT::v4i64,  4 },
    { ISD::TRUNCATE,    MVT::v4i32, MVT::v4i64,  4 },
    { ISD::TRUNCATE,    MVT::v8i32, MVT::v8i64,  9 },

    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i1,  3 },
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i1,  3 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i1,  8 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i8,  3 },
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i8,  3 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i8,  8 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i16, 3 },
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i16, 3 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i16, 5 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i32, 1 },
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i32, 1 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i32, 1 },

    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i1,  7 },
    { ISD::UINT_TO_FP,  MVT::v4f64, MVT::v4i1,  7 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i1,  6 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i8,  2 },
    { ISD::UINT_TO_FP,  MVT::v4f64, MVT::v4i8,  2 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i8,  5 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v4f64, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i16, 5 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i32, 6 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i32, 6 },
    { ISD::UINT_TO_FP,  MVT::v4f64, MVT::v4i32, 6 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i32, 9 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i64, 5 },
    { ISD::UINT_TO_FP,  MVT::v4f64, MVT::v4i64, 6 },
    // The generic code to compute the scalar overhead is currently broken.
    // Workaround this limitation by estimating the scalarization overhead
    // here. We have roughly 10 instructions per scalar element.
    // Multiply that by the vector width.
    // FIXME: remove that when PR19268 is fixed.
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i64, 13 },
    { ISD::SINT_TO_FP,  MVT::v4f64, MVT::v4i64, 13 },

    { ISD::FP_TO_SINT,  MVT::v4i8,  MVT::v4f32, 1 },
    { ISD::FP_TO_SINT,  MVT::v8i8,  MVT::v8f32, 7 },
    // This node is expanded into scalarized operations but BasicTTI is overly
    // optimistic estimating its cost.  It computes 3 per element (one
    // vector-extract, one scalar conversion and one vector-insert).  The
    // problem is that the inserts form a read-modify-write chain so latency
    // should be factored in too.  Inflating the cost per element by 1.
    { ISD::FP_TO_UINT,  MVT::v8i32, MVT::v8f32, 8*4 },
    { ISD::FP_TO_UINT,  MVT::v4i32, MVT::v4f64, 4*4 },

    { ISD::FP_EXTEND,   MVT::v4f64,  MVT::v4f32,  1 },
    { ISD::FP_ROUND,    MVT::v4f32,  MVT::v4f64,  1 },
  };

  static const TypeConversionCostTblEntry SSE41ConversionTbl[] = {
    { ISD::ZERO_EXTEND, MVT::v4i64, MVT::v4i8,    2 },
    { ISD::SIGN_EXTEND, MVT::v4i64, MVT::v4i8,    2 },
    { ISD::ZERO_EXTEND, MVT::v4i64, MVT::v4i16,   2 },
    { ISD::SIGN_EXTEND, MVT::v4i64, MVT::v4i16,   2 },
    { ISD::ZERO_EXTEND, MVT::v4i64, MVT::v4i32,   2 },
    { ISD::SIGN_EXTEND, MVT::v4i64, MVT::v4i32,   2 },

    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i8,   1 },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i8,   2 },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i8,   1 },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i8,   1 },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i8,   1 },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i8,   1 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,   2 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,   2 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  2 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  2 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8,  4 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8,  4 },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i16,  1 },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i16,  1 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  2 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  2 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i16, 4 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i16, 4 },

    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i16,  2 },
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i16,  1 },
    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i32,  1 },
    { ISD::TRUNCATE,    MVT::v4i16,  MVT::v4i32,  1 },
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i32,  3 },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  3 },
    { ISD::TRUNCATE,    MVT::v16i16, MVT::v16i32, 6 },

    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    4 },
  };

  static const TypeConversionCostTblEntry SSE2ConversionTbl[] = {
    // These are somewhat magic numbers justified by looking at the output of
    // Intel's IACA, running some kernels and making sure when we take
    // legalization into account the throughput will be overestimated.
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v16i8, 8 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v16i8, 16*10 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v8i16, 15 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v8i16, 8*10 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i32, 5 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v4i32, 4*10 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v2i64, 15 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i64, 2*10 },

    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v16i8, 16*10 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v16i8, 8 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v8i16, 15 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v8i16, 8*10 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v4i32, 4*10 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i32, 8 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i64, 6 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v2i64, 15 },

    { ISD::FP_TO_SINT,  MVT::v2i32,  MVT::v2f64,  3 },

    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    6 },

    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i8,   1 },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i8,   6 },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i8,   2 },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i8,   3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i8,   4 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i8,   8 },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i8,   1 },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i8,   2 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,   6 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,   6 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  3 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  4 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8,  9 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8,  12 },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i16,  1 },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i16,  2 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i16,  3 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i16,  10 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  4 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i16, 6 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i16, 8 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32,  3 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32,  5 },

    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i16,  4 },
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i16,  2 },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i16, 3 },
    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i32,  3 },
    { ISD::TRUNCATE,    MVT::v4i16,  MVT::v4i32,  3 },
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i32,  4 },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i32, 7 },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  5 },
    { ISD::TRUNCATE,    MVT::v16i16, MVT::v16i32, 10 },
  };

  std::pair<int, MVT> LTSrc = TLI->getTypeLegalizationCost(DL, Src);
  std::pair<int, MVT> LTDest = TLI->getTypeLegalizationCost(DL, Dst);

  if (ST->hasSSE2() && !ST->hasAVX()) {
    if (const auto *Entry = ConvertCostTableLookup(SSE2ConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      return LTSrc.first * Entry->Cost;
  }

  EVT SrcTy = TLI->getValueType(DL, Src);
  EVT DstTy = TLI->getValueType(DL, Dst);

  // The function getSimpleVT only handles simple value types.
  if (!SrcTy.isSimple() || !DstTy.isSimple())
    return BaseT::getCastInstrCost(Opcode, Dst, Src);

  MVT SimpleSrcTy = SrcTy.getSimpleVT();
  MVT SimpleDstTy = DstTy.getSimpleVT();

  // Make sure that neither type is going to be split before using the
  // AVX512 tables. This handles -mprefer-vector-width=256
  // with -min-legal-vector-width<=256
  if (TLI->getTypeAction(SimpleSrcTy) != TargetLowering::TypeSplitVector &&
      TLI->getTypeAction(SimpleDstTy) != TargetLowering::TypeSplitVector) {
    if (ST->hasBWI())
      if (const auto *Entry = ConvertCostTableLookup(AVX512BWConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        return Entry->Cost;

    if (ST->hasDQI())
      if (const auto *Entry = ConvertCostTableLookup(AVX512DQConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        return Entry->Cost;

    if (ST->hasAVX512())
      if (const auto *Entry = ConvertCostTableLookup(AVX512FConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        return Entry->Cost;
  }

  if (ST->hasAVX2()) {
    if (const auto *Entry = ConvertCostTableLookup(AVX2ConversionTbl, ISD,
                                                   SimpleDstTy, SimpleSrcTy))
      return Entry->Cost;
  }

  if (ST->hasAVX()) {
    if (const auto *Entry = ConvertCostTableLookup(AVXConversionTbl, ISD,
                                                   SimpleDstTy, SimpleSrcTy))
      return Entry->Cost;
  }

  if (ST->hasSSE41()) {
    if (const auto *Entry = ConvertCostTableLookup(SSE41ConversionTbl, ISD,
                                                   SimpleDstTy, SimpleSrcTy))
      return Entry->Cost;
  }

  if (ST->hasSSE2()) {
    if (const auto *Entry = ConvertCostTableLookup(SSE2ConversionTbl, ISD,
                                                   SimpleDstTy, SimpleSrcTy))
      return Entry->Cost;
  }

  return BaseT::getCastInstrCost(Opcode, Dst, Src, I);
}

int X86TTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                   const Instruction *I) {
  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, ValTy);

  MVT MTy = LT.second;

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  static const CostTblEntry SSE2CostTbl[] = {
    { ISD::SETCC,   MVT::v2i64,   8 },
    { ISD::SETCC,   MVT::v4i32,   1 },
    { ISD::SETCC,   MVT::v8i16,   1 },
    { ISD::SETCC,   MVT::v16i8,   1 },
  };

  static const CostTblEntry SSE42CostTbl[] = {
    { ISD::SETCC,   MVT::v2f64,   1 },
    { ISD::SETCC,   MVT::v4f32,   1 },
    { ISD::SETCC,   MVT::v2i64,   1 },
  };

  static const CostTblEntry AVX1CostTbl[] = {
    { ISD::SETCC,   MVT::v4f64,   1 },
    { ISD::SETCC,   MVT::v8f32,   1 },
    // AVX1 does not support 8-wide integer compare.
    { ISD::SETCC,   MVT::v4i64,   4 },
    { ISD::SETCC,   MVT::v8i32,   4 },
    { ISD::SETCC,   MVT::v16i16,  4 },
    { ISD::SETCC,   MVT::v32i8,   4 },
  };

  static const CostTblEntry AVX2CostTbl[] = {
    { ISD::SETCC,   MVT::v4i64,   1 },
    { ISD::SETCC,   MVT::v8i32,   1 },
    { ISD::SETCC,   MVT::v16i16,  1 },
    { ISD::SETCC,   MVT::v32i8,   1 },
  };

  static const CostTblEntry AVX512CostTbl[] = {
    { ISD::SETCC,   MVT::v8i64,   1 },
    { ISD::SETCC,   MVT::v16i32,  1 },
    { ISD::SETCC,   MVT::v8f64,   1 },
    { ISD::SETCC,   MVT::v16f32,  1 },
  };

  static const CostTblEntry AVX512BWCostTbl[] = {
    { ISD::SETCC,   MVT::v32i16,  1 },
    { ISD::SETCC,   MVT::v64i8,   1 },
  };

  if (ST->hasBWI())
    if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  if (ST->hasSSE42())
    if (const auto *Entry = CostTableLookup(SSE42CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;

  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, I);
}

unsigned X86TTIImpl::getAtomicMemIntrinsicMaxElementSize() const { return 16; }

int X86TTIImpl::getIntrinsicInstrCost(Intrinsic::ID IID, Type *RetTy,
                                      ArrayRef<Type *> Tys, FastMathFlags FMF,
                                      unsigned ScalarizationCostPassed) {
  // Costs should match the codegen from:
  // BITREVERSE: llvm\test\CodeGen\X86\vector-bitreverse.ll
  // BSWAP: llvm\test\CodeGen\X86\bswap-vector.ll
  // CTLZ: llvm\test\CodeGen\X86\vector-lzcnt-*.ll
  // CTPOP: llvm\test\CodeGen\X86\vector-popcnt-*.ll
  // CTTZ: llvm\test\CodeGen\X86\vector-tzcnt-*.ll
  static const CostTblEntry AVX512CDCostTbl[] = {
    { ISD::CTLZ,       MVT::v8i64,   1 },
    { ISD::CTLZ,       MVT::v16i32,  1 },
    { ISD::CTLZ,       MVT::v32i16,  8 },
    { ISD::CTLZ,       MVT::v64i8,  20 },
    { ISD::CTLZ,       MVT::v4i64,   1 },
    { ISD::CTLZ,       MVT::v8i32,   1 },
    { ISD::CTLZ,       MVT::v16i16,  4 },
    { ISD::CTLZ,       MVT::v32i8,  10 },
    { ISD::CTLZ,       MVT::v2i64,   1 },
    { ISD::CTLZ,       MVT::v4i32,   1 },
    { ISD::CTLZ,       MVT::v8i16,   4 },
    { ISD::CTLZ,       MVT::v16i8,   4 },
  };
  static const CostTblEntry AVX512BWCostTbl[] = {
    { ISD::BITREVERSE, MVT::v8i64,   5 },
    { ISD::BITREVERSE, MVT::v16i32,  5 },
    { ISD::BITREVERSE, MVT::v32i16,  5 },
    { ISD::BITREVERSE, MVT::v64i8,   5 },
    { ISD::CTLZ,       MVT::v8i64,  23 },
    { ISD::CTLZ,       MVT::v16i32, 22 },
    { ISD::CTLZ,       MVT::v32i16, 18 },
    { ISD::CTLZ,       MVT::v64i8,  17 },
    { ISD::CTPOP,      MVT::v8i64,   7 },
    { ISD::CTPOP,      MVT::v16i32, 11 },
    { ISD::CTPOP,      MVT::v32i16,  9 },
    { ISD::CTPOP,      MVT::v64i8,   6 },
    { ISD::CTTZ,       MVT::v8i64,  10 },
    { ISD::CTTZ,       MVT::v16i32, 14 },
    { ISD::CTTZ,       MVT::v32i16, 12 },
    { ISD::CTTZ,       MVT::v64i8,   9 },
    { ISD::SADDSAT,    MVT::v32i16,  1 },
    { ISD::SADDSAT,    MVT::v64i8,   1 },
    { ISD::SSUBSAT,    MVT::v32i16,  1 },
    { ISD::SSUBSAT,    MVT::v64i8,   1 },
    { ISD::UADDSAT,    MVT::v32i16,  1 },
    { ISD::UADDSAT,    MVT::v64i8,   1 },
    { ISD::USUBSAT,    MVT::v32i16,  1 },
    { ISD::USUBSAT,    MVT::v64i8,   1 },
  };
  static const CostTblEntry AVX512CostTbl[] = {
    { ISD::BITREVERSE, MVT::v8i64,  36 },
    { ISD::BITREVERSE, MVT::v16i32, 24 },
    { ISD::CTLZ,       MVT::v8i64,  29 },
    { ISD::CTLZ,       MVT::v16i32, 35 },
    { ISD::CTPOP,      MVT::v8i64,  16 },
    { ISD::CTPOP,      MVT::v16i32, 24 },
    { ISD::CTTZ,       MVT::v8i64,  20 },
    { ISD::CTTZ,       MVT::v16i32, 28 },
    { ISD::USUBSAT,    MVT::v16i32,  2 }, // pmaxud + psubd
    { ISD::USUBSAT,    MVT::v2i64,   2 }, // pmaxuq + psubq
    { ISD::USUBSAT,    MVT::v4i64,   2 }, // pmaxuq + psubq
    { ISD::USUBSAT,    MVT::v8i64,   2 }, // pmaxuq + psubq
  };
  static const CostTblEntry XOPCostTbl[] = {
    { ISD::BITREVERSE, MVT::v4i64,   4 },
    { ISD::BITREVERSE, MVT::v8i32,   4 },
    { ISD::BITREVERSE, MVT::v16i16,  4 },
    { ISD::BITREVERSE, MVT::v32i8,   4 },
    { ISD::BITREVERSE, MVT::v2i64,   1 },
    { ISD::BITREVERSE, MVT::v4i32,   1 },
    { ISD::BITREVERSE, MVT::v8i16,   1 },
    { ISD::BITREVERSE, MVT::v16i8,   1 },
    { ISD::BITREVERSE, MVT::i64,     3 },
    { ISD::BITREVERSE, MVT::i32,     3 },
    { ISD::BITREVERSE, MVT::i16,     3 },
    { ISD::BITREVERSE, MVT::i8,      3 }
  };
  static const CostTblEntry AVX2CostTbl[] = {
    { ISD::BITREVERSE, MVT::v4i64,   5 },
    { ISD::BITREVERSE, MVT::v8i32,   5 },
    { ISD::BITREVERSE, MVT::v16i16,  5 },
    { ISD::BITREVERSE, MVT::v32i8,   5 },
    { ISD::BSWAP,      MVT::v4i64,   1 },
    { ISD::BSWAP,      MVT::v8i32,   1 },
    { ISD::BSWAP,      MVT::v16i16,  1 },
    { ISD::CTLZ,       MVT::v4i64,  23 },
    { ISD::CTLZ,       MVT::v8i32,  18 },
    { ISD::CTLZ,       MVT::v16i16, 14 },
    { ISD::CTLZ,       MVT::v32i8,   9 },
    { ISD::CTPOP,      MVT::v4i64,   7 },
    { ISD::CTPOP,      MVT::v8i32,  11 },
    { ISD::CTPOP,      MVT::v16i16,  9 },
    { ISD::CTPOP,      MVT::v32i8,   6 },
    { ISD::CTTZ,       MVT::v4i64,  10 },
    { ISD::CTTZ,       MVT::v8i32,  14 },
    { ISD::CTTZ,       MVT::v16i16, 12 },
    { ISD::CTTZ,       MVT::v32i8,   9 },
    { ISD::SADDSAT,    MVT::v16i16,  1 },
    { ISD::SADDSAT,    MVT::v32i8,   1 },
    { ISD::SSUBSAT,    MVT::v16i16,  1 },
    { ISD::SSUBSAT,    MVT::v32i8,   1 },
    { ISD::UADDSAT,    MVT::v16i16,  1 },
    { ISD::UADDSAT,    MVT::v32i8,   1 },
    { ISD::USUBSAT,    MVT::v16i16,  1 },
    { ISD::USUBSAT,    MVT::v32i8,   1 },
    { ISD::USUBSAT,    MVT::v8i32,   2 }, // pmaxud + psubd
    { ISD::FSQRT,      MVT::f32,     7 }, // Haswell from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,   7 }, // Haswell from http://www.agner.org/
    { ISD::FSQRT,      MVT::v8f32,  14 }, // Haswell from http://www.agner.org/
    { ISD::FSQRT,      MVT::f64,    14 }, // Haswell from http://www.agner.org/
    { ISD::FSQRT,      MVT::v2f64,  14 }, // Haswell from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f64,  28 }, // Haswell from http://www.agner.org/
  };
  static const CostTblEntry AVX1CostTbl[] = {
    { ISD::BITREVERSE, MVT::v4i64,  12 }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v8i32,  12 }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v16i16, 12 }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v32i8,  12 }, // 2 x 128-bit Op + extract/insert
    { ISD::BSWAP,      MVT::v4i64,   4 },
    { ISD::BSWAP,      MVT::v8i32,   4 },
    { ISD::BSWAP,      MVT::v16i16,  4 },
    { ISD::CTLZ,       MVT::v4i64,  48 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v8i32,  38 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v16i16, 30 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v32i8,  20 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v4i64,  16 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v8i32,  24 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v16i16, 20 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v32i8,  14 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v4i64,  22 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v8i32,  30 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v16i16, 26 }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v32i8,  20 }, // 2 x 128-bit Op + extract/insert
    { ISD::SADDSAT,    MVT::v16i16,  4 }, // 2 x 128-bit Op + extract/insert
    { ISD::SADDSAT,    MVT::v32i8,   4 }, // 2 x 128-bit Op + extract/insert
    { ISD::SSUBSAT,    MVT::v16i16,  4 }, // 2 x 128-bit Op + extract/insert
    { ISD::SSUBSAT,    MVT::v32i8,   4 }, // 2 x 128-bit Op + extract/insert
    { ISD::UADDSAT,    MVT::v16i16,  4 }, // 2 x 128-bit Op + extract/insert
    { ISD::UADDSAT,    MVT::v32i8,   4 }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v16i16,  4 }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v32i8,   4 }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v8i32,   6 }, // 2 x 128-bit Op + extract/insert
    { ISD::FSQRT,      MVT::f32,    14 }, // SNB from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,  14 }, // SNB from http://www.agner.org/
    { ISD::FSQRT,      MVT::v8f32,  28 }, // SNB from http://www.agner.org/
    { ISD::FSQRT,      MVT::f64,    21 }, // SNB from http://www.agner.org/
    { ISD::FSQRT,      MVT::v2f64,  21 }, // SNB from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f64,  43 }, // SNB from http://www.agner.org/
  };
  static const CostTblEntry GLMCostTbl[] = {
    { ISD::FSQRT, MVT::f32,   19 }, // sqrtss
    { ISD::FSQRT, MVT::v4f32, 37 }, // sqrtps
    { ISD::FSQRT, MVT::f64,   34 }, // sqrtsd
    { ISD::FSQRT, MVT::v2f64, 67 }, // sqrtpd
  };
  static const CostTblEntry SLMCostTbl[] = {
    { ISD::FSQRT, MVT::f32,   20 }, // sqrtss
    { ISD::FSQRT, MVT::v4f32, 40 }, // sqrtps
    { ISD::FSQRT, MVT::f64,   35 }, // sqrtsd
    { ISD::FSQRT, MVT::v2f64, 70 }, // sqrtpd
  };
  static const CostTblEntry SSE42CostTbl[] = {
    { ISD::USUBSAT,    MVT::v4i32,   2 }, // pmaxud + psubd
    { ISD::FSQRT,      MVT::f32,    18 }, // Nehalem from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,  18 }, // Nehalem from http://www.agner.org/
  };
  static const CostTblEntry SSSE3CostTbl[] = {
    { ISD::BITREVERSE, MVT::v2i64,   5 },
    { ISD::BITREVERSE, MVT::v4i32,   5 },
    { ISD::BITREVERSE, MVT::v8i16,   5 },
    { ISD::BITREVERSE, MVT::v16i8,   5 },
    { ISD::BSWAP,      MVT::v2i64,   1 },
    { ISD::BSWAP,      MVT::v4i32,   1 },
    { ISD::BSWAP,      MVT::v8i16,   1 },
    { ISD::CTLZ,       MVT::v2i64,  23 },
    { ISD::CTLZ,       MVT::v4i32,  18 },
    { ISD::CTLZ,       MVT::v8i16,  14 },
    { ISD::CTLZ,       MVT::v16i8,   9 },
    { ISD::CTPOP,      MVT::v2i64,   7 },
    { ISD::CTPOP,      MVT::v4i32,  11 },
    { ISD::CTPOP,      MVT::v8i16,   9 },
    { ISD::CTPOP,      MVT::v16i8,   6 },
    { ISD::CTTZ,       MVT::v2i64,  10 },
    { ISD::CTTZ,       MVT::v4i32,  14 },
    { ISD::CTTZ,       MVT::v8i16,  12 },
    { ISD::CTTZ,       MVT::v16i8,   9 }
  };
  static const CostTblEntry SSE2CostTbl[] = {
    { ISD::BITREVERSE, MVT::v2i64,  29 },
    { ISD::BITREVERSE, MVT::v4i32,  27 },
    { ISD::BITREVERSE, MVT::v8i16,  27 },
    { ISD::BITREVERSE, MVT::v16i8,  20 },
    { ISD::BSWAP,      MVT::v2i64,   7 },
    { ISD::BSWAP,      MVT::v4i32,   7 },
    { ISD::BSWAP,      MVT::v8i16,   7 },
    { ISD::CTLZ,       MVT::v2i64,  25 },
    { ISD::CTLZ,       MVT::v4i32,  26 },
    { ISD::CTLZ,       MVT::v8i16,  20 },
    { ISD::CTLZ,       MVT::v16i8,  17 },
    { ISD::CTPOP,      MVT::v2i64,  12 },
    { ISD::CTPOP,      MVT::v4i32,  15 },
    { ISD::CTPOP,      MVT::v8i16,  13 },
    { ISD::CTPOP,      MVT::v16i8,  10 },
    { ISD::CTTZ,       MVT::v2i64,  14 },
    { ISD::CTTZ,       MVT::v4i32,  18 },
    { ISD::CTTZ,       MVT::v8i16,  16 },
    { ISD::CTTZ,       MVT::v16i8,  13 },
    { ISD::SADDSAT,    MVT::v8i16,   1 },
    { ISD::SADDSAT,    MVT::v16i8,   1 },
    { ISD::SSUBSAT,    MVT::v8i16,   1 },
    { ISD::SSUBSAT,    MVT::v16i8,   1 },
    { ISD::UADDSAT,    MVT::v8i16,   1 },
    { ISD::UADDSAT,    MVT::v16i8,   1 },
    { ISD::USUBSAT,    MVT::v8i16,   1 },
    { ISD::USUBSAT,    MVT::v16i8,   1 },
    { ISD::FSQRT,      MVT::f64,    32 }, // Nehalem from http://www.agner.org/
    { ISD::FSQRT,      MVT::v2f64,  32 }, // Nehalem from http://www.agner.org/
  };
  static const CostTblEntry SSE1CostTbl[] = {
    { ISD::FSQRT,      MVT::f32,    28 }, // Pentium III from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,  56 }, // Pentium III from http://www.agner.org/
  };
  static const CostTblEntry X64CostTbl[] = { // 64-bit targets
    { ISD::BITREVERSE, MVT::i64,    14 }
  };
  static const CostTblEntry X86CostTbl[] = { // 32 or 64-bit targets
    { ISD::BITREVERSE, MVT::i32,    14 },
    { ISD::BITREVERSE, MVT::i16,    14 },
    { ISD::BITREVERSE, MVT::i8,     11 }
  };

  unsigned ISD = ISD::DELETED_NODE;
  switch (IID) {
  default:
    break;
  case Intrinsic::bitreverse:
    ISD = ISD::BITREVERSE;
    break;
  case Intrinsic::bswap:
    ISD = ISD::BSWAP;
    break;
  case Intrinsic::ctlz:
    ISD = ISD::CTLZ;
    break;
  case Intrinsic::ctpop:
    ISD = ISD::CTPOP;
    break;
  case Intrinsic::cttz:
    ISD = ISD::CTTZ;
    break;
  case Intrinsic::sadd_sat:
    ISD = ISD::SADDSAT;
    break;
  case Intrinsic::ssub_sat:
    ISD = ISD::SSUBSAT;
    break;
  case Intrinsic::uadd_sat:
    ISD = ISD::UADDSAT;
    break;
  case Intrinsic::usub_sat:
    ISD = ISD::USUBSAT;
    break;
  case Intrinsic::sqrt:
    ISD = ISD::FSQRT;
    break;
  }

  if (ISD != ISD::DELETED_NODE) {
    // Legalize the type.
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, RetTy);
    MVT MTy = LT.second;

    // Attempt to lookup cost.
    if (ST->isGLM())
      if (const auto *Entry = CostTableLookup(GLMCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->isSLM())
      if (const auto *Entry = CostTableLookup(SLMCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasCDI())
      if (const auto *Entry = CostTableLookup(AVX512CDCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasBWI())
      if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX512())
      if (const auto *Entry = CostTableLookup(AVX512CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasXOP())
      if (const auto *Entry = CostTableLookup(XOPCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSSE3())
      if (const auto *Entry = CostTableLookup(SSSE3CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE1())
      if (const auto *Entry = CostTableLookup(SSE1CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->is64Bit())
      if (const auto *Entry = CostTableLookup(X64CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (const auto *Entry = CostTableLookup(X86CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;
  }

  return BaseT::getIntrinsicInstrCost(IID, RetTy, Tys, FMF, ScalarizationCostPassed);
}

int X86TTIImpl::getIntrinsicInstrCost(Intrinsic::ID IID, Type *RetTy,
                                      ArrayRef<Value *> Args, FastMathFlags FMF,
                                      unsigned VF) {
  static const CostTblEntry AVX512CostTbl[] = {
    { ISD::ROTL,       MVT::v8i64,   1 },
    { ISD::ROTL,       MVT::v4i64,   1 },
    { ISD::ROTL,       MVT::v2i64,   1 },
    { ISD::ROTL,       MVT::v16i32,  1 },
    { ISD::ROTL,       MVT::v8i32,   1 },
    { ISD::ROTL,       MVT::v4i32,   1 },
    { ISD::ROTR,       MVT::v8i64,   1 },
    { ISD::ROTR,       MVT::v4i64,   1 },
    { ISD::ROTR,       MVT::v2i64,   1 },
    { ISD::ROTR,       MVT::v16i32,  1 },
    { ISD::ROTR,       MVT::v8i32,   1 },
    { ISD::ROTR,       MVT::v4i32,   1 }
  };
  // XOP: ROTL = VPROT(X,Y), ROTR = VPROT(X,SUB(0,Y))
  static const CostTblEntry XOPCostTbl[] = {
    { ISD::ROTL,       MVT::v4i64,   4 },
    { ISD::ROTL,       MVT::v8i32,   4 },
    { ISD::ROTL,       MVT::v16i16,  4 },
    { ISD::ROTL,       MVT::v32i8,   4 },
    { ISD::ROTL,       MVT::v2i64,   1 },
    { ISD::ROTL,       MVT::v4i32,   1 },
    { ISD::ROTL,       MVT::v8i16,   1 },
    { ISD::ROTL,       MVT::v16i8,   1 },
    { ISD::ROTR,       MVT::v4i64,   6 },
    { ISD::ROTR,       MVT::v8i32,   6 },
    { ISD::ROTR,       MVT::v16i16,  6 },
    { ISD::ROTR,       MVT::v32i8,   6 },
    { ISD::ROTR,       MVT::v2i64,   2 },
    { ISD::ROTR,       MVT::v4i32,   2 },
    { ISD::ROTR,       MVT::v8i16,   2 },
    { ISD::ROTR,       MVT::v16i8,   2 }
  };
  static const CostTblEntry X64CostTbl[] = { // 64-bit targets
    { ISD::ROTL,       MVT::i64,     1 },
    { ISD::ROTR,       MVT::i64,     1 },
    { ISD::FSHL,       MVT::i64,     4 }
  };
  static const CostTblEntry X86CostTbl[] = { // 32 or 64-bit targets
    { ISD::ROTL,       MVT::i32,     1 },
    { ISD::ROTL,       MVT::i16,     1 },
    { ISD::ROTL,       MVT::i8,      1 },
    { ISD::ROTR,       MVT::i32,     1 },
    { ISD::ROTR,       MVT::i16,     1 },
    { ISD::ROTR,       MVT::i8,      1 },
    { ISD::FSHL,       MVT::i32,     4 },
    { ISD::FSHL,       MVT::i16,     4 },
    { ISD::FSHL,       MVT::i8,      4 }
  };

  unsigned ISD = ISD::DELETED_NODE;
  switch (IID) {
  default:
    break;
  case Intrinsic::fshl:
    ISD = ISD::FSHL;
    if (Args[0] == Args[1])
      ISD = ISD::ROTL;
    break;
  case Intrinsic::fshr:
    // FSHR has same costs so don't duplicate.
    ISD = ISD::FSHL;
    if (Args[0] == Args[1])
      ISD = ISD::ROTR;
    break;
  }

  if (ISD != ISD::DELETED_NODE) {
    // Legalize the type.
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, RetTy);
    MVT MTy = LT.second;

    // Attempt to lookup cost.
    if (ST->hasAVX512())
      if (const auto *Entry = CostTableLookup(AVX512CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasXOP())
      if (const auto *Entry = CostTableLookup(XOPCostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->is64Bit())
      if (const auto *Entry = CostTableLookup(X64CostTbl, ISD, MTy))
        return LT.first * Entry->Cost;

    if (const auto *Entry = CostTableLookup(X86CostTbl, ISD, MTy))
      return LT.first * Entry->Cost;
  }

  return BaseT::getIntrinsicInstrCost(IID, RetTy, Args, FMF, VF);
}

int X86TTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index) {
  assert(Val->isVectorTy() && "This must be a vector type");

  Type *ScalarType = Val->getScalarType();

  if (Index != -1U) {
    // Legalize the type.
    std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Val);

    // This type is legalized to a scalar type.
    if (!LT.second.isVector())
      return 0;

    // The type may be split. Normalize the index to the new type.
    unsigned Width = LT.second.getVectorNumElements();
    Index = Index % Width;

    // Floating point scalars are already located in index #0.
    if (ScalarType->isFloatingPointTy() && Index == 0)
      return 0;
  }

  // Add to the base cost if we know that the extracted element of a vector is
  // destined to be moved to and used in the integer register file.
  int RegisterFileMoveCost = 0;
  if (Opcode == Instruction::ExtractElement && ScalarType->isPointerTy())
    RegisterFileMoveCost = 1;

  return BaseT::getVectorInstrCost(Opcode, Val, Index) + RegisterFileMoveCost;
}

int X86TTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                                unsigned AddressSpace, const Instruction *I) {
  // Handle non-power-of-two vectors such as <3 x float>
  if (VectorType *VTy = dyn_cast<VectorType>(Src)) {
    unsigned NumElem = VTy->getVectorNumElements();

    // Handle a few common cases:
    // <3 x float>
    if (NumElem == 3 && VTy->getScalarSizeInBits() == 32)
      // Cost = 64 bit store + extract + 32 bit store.
      return 3;

    // <3 x double>
    if (NumElem == 3 && VTy->getScalarSizeInBits() == 64)
      // Cost = 128 bit store + unpack + 64 bit store.
      return 3;

    // Assume that all other non-power-of-two numbers are scalarized.
    if (!isPowerOf2_32(NumElem)) {
      int Cost = BaseT::getMemoryOpCost(Opcode, VTy->getScalarType(), Alignment,
                                        AddressSpace);
      int SplitCost = getScalarizationOverhead(Src, Opcode == Instruction::Load,
                                               Opcode == Instruction::Store);
      return NumElem * Cost + SplitCost;
    }
  }

  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, Src);
  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");

  // Each load/store unit costs 1.
  int Cost = LT.first * 1;

  // This isn't exactly right. We're using slow unaligned 32-byte accesses as a
  // proxy for a double-pumped AVX memory interface such as on Sandybridge.
  if (LT.second.getStoreSize() == 32 && ST->isUnalignedMem32Slow())
    Cost *= 2;

  return Cost;
}

int X86TTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *SrcTy,
                                      unsigned Alignment,
                                      unsigned AddressSpace) {
  VectorType *SrcVTy = dyn_cast<VectorType>(SrcTy);
  if (!SrcVTy)
    // To calculate scalar take the regular cost, without mask
    return getMemoryOpCost(Opcode, SrcTy, Alignment, AddressSpace);

  unsigned NumElem = SrcVTy->getVectorNumElements();
  VectorType *MaskTy =
    VectorType::get(Type::getInt8Ty(SrcVTy->getContext()), NumElem);
  if ((Opcode == Instruction::Load && !isLegalMaskedLoad(SrcVTy)) ||
      (Opcode == Instruction::Store && !isLegalMaskedStore(SrcVTy)) ||
      !isPowerOf2_32(NumElem)) {
    // Scalarization
    int MaskSplitCost = getScalarizationOverhead(MaskTy, false, true);
    int ScalarCompareCost = getCmpSelInstrCost(
        Instruction::ICmp, Type::getInt8Ty(SrcVTy->getContext()), nullptr);
    int BranchCost = getCFInstrCost(Instruction::Br);
    int MaskCmpCost = NumElem * (BranchCost + ScalarCompareCost);

    int ValueSplitCost = getScalarizationOverhead(
        SrcVTy, Opcode == Instruction::Load, Opcode == Instruction::Store);
    int MemopCost =
        NumElem * BaseT::getMemoryOpCost(Opcode, SrcVTy->getScalarType(),
                                         Alignment, AddressSpace);
    return MemopCost + ValueSplitCost + MaskSplitCost + MaskCmpCost;
  }

  // Legalize the type.
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, SrcVTy);
  auto VT = TLI->getValueType(DL, SrcVTy);
  int Cost = 0;
  if (VT.isSimple() && LT.second != VT.getSimpleVT() &&
      LT.second.getVectorNumElements() == NumElem)
    // Promotion requires expand/truncate for data and a shuffle for mask.
    Cost += getShuffleCost(TTI::SK_Select, SrcVTy, 0, nullptr) +
            getShuffleCost(TTI::SK_Select, MaskTy, 0, nullptr);

  else if (LT.second.getVectorNumElements() > NumElem) {
    VectorType *NewMaskTy = VectorType::get(MaskTy->getVectorElementType(),
                                            LT.second.getVectorNumElements());
    // Expanding requires fill mask with zeroes
    Cost += getShuffleCost(TTI::SK_InsertSubvector, NewMaskTy, 0, MaskTy);
  }
  if (!ST->hasAVX512())
    return Cost + LT.first*4; // Each maskmov costs 4

  // AVX-512 masked load/store is cheapper
  return Cost+LT.first;
}

int X86TTIImpl::getAddressComputationCost(Type *Ty, ScalarEvolution *SE,
                                          const SCEV *Ptr) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  unsigned NumVectorInstToHideOverhead = 10;

  // Cost modeling of Strided Access Computation is hidden by the indexing
  // modes of X86 regardless of the stride value. We dont believe that there
  // is a difference between constant strided access in gerenal and constant
  // strided value which is less than or equal to 64.
  // Even in the case of (loop invariant) stride whose value is not known at
  // compile time, the address computation will not incur more than one extra
  // ADD instruction.
  if (Ty->isVectorTy() && SE) {
    if (!BaseT::isStridedAccess(Ptr))
      return NumVectorInstToHideOverhead;
    if (!BaseT::getConstantStrideStep(SE, Ptr))
      return 1;
  }

  return BaseT::getAddressComputationCost(Ty, SE, Ptr);
}

int X86TTIImpl::getArithmeticReductionCost(unsigned Opcode, Type *ValTy,
                                           bool IsPairwise) {

  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, ValTy);

  MVT MTy = LT.second;

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // We use the Intel Architecture Code Analyzer(IACA) to measure the throughput
  // and make it as the cost.

  static const CostTblEntry SSE42CostTblPairWise[] = {
    { ISD::FADD,  MVT::v2f64,   2 },
    { ISD::FADD,  MVT::v4f32,   4 },
    { ISD::ADD,   MVT::v2i64,   2 },      // The data reported by the IACA tool is "1.6".
    { ISD::ADD,   MVT::v4i32,   3 },      // The data reported by the IACA tool is "3.5".
    { ISD::ADD,   MVT::v8i16,   5 },
  };

  static const CostTblEntry AVX1CostTblPairWise[] = {
    { ISD::FADD,  MVT::v4f32,   4 },
    { ISD::FADD,  MVT::v4f64,   5 },
    { ISD::FADD,  MVT::v8f32,   7 },
    { ISD::ADD,   MVT::v2i64,   1 },      // The data reported by the IACA tool is "1.5".
    { ISD::ADD,   MVT::v4i32,   3 },      // The data reported by the IACA tool is "3.5".
    { ISD::ADD,   MVT::v4i64,   5 },      // The data reported by the IACA tool is "4.8".
    { ISD::ADD,   MVT::v8i16,   5 },
    { ISD::ADD,   MVT::v8i32,   5 },
  };

  static const CostTblEntry SSE42CostTblNoPairWise[] = {
    { ISD::FADD,  MVT::v2f64,   2 },
    { ISD::FADD,  MVT::v4f32,   4 },
    { ISD::ADD,   MVT::v2i64,   2 },      // The data reported by the IACA tool is "1.6".
    { ISD::ADD,   MVT::v4i32,   3 },      // The data reported by the IACA tool is "3.3".
    { ISD::ADD,   MVT::v8i16,   4 },      // The data reported by the IACA tool is "4.3".
  };

  static const CostTblEntry AVX1CostTblNoPairWise[] = {
    { ISD::FADD,  MVT::v4f32,   3 },
    { ISD::FADD,  MVT::v4f64,   3 },
    { ISD::FADD,  MVT::v8f32,   4 },
    { ISD::ADD,   MVT::v2i64,   1 },      // The data reported by the IACA tool is "1.5".
    { ISD::ADD,   MVT::v4i32,   3 },      // The data reported by the IACA tool is "2.8".
    { ISD::ADD,   MVT::v4i64,   3 },
    { ISD::ADD,   MVT::v8i16,   4 },
    { ISD::ADD,   MVT::v8i32,   5 },
  };

  if (IsPairwise) {
    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;
  } else {
    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;
  }

  return BaseT::getArithmeticReductionCost(Opcode, ValTy, IsPairwise);
}

int X86TTIImpl::getMinMaxReductionCost(Type *ValTy, Type *CondTy,
                                       bool IsPairwise, bool IsUnsigned) {
  std::pair<int, MVT> LT = TLI->getTypeLegalizationCost(DL, ValTy);

  MVT MTy = LT.second;

  int ISD;
  if (ValTy->isIntOrIntVectorTy()) {
    ISD = IsUnsigned ? ISD::UMIN : ISD::SMIN;
  } else {
    assert(ValTy->isFPOrFPVectorTy() &&
           "Expected float point or integer vector type.");
    ISD = ISD::FMINNUM;
  }

  // We use the Intel Architecture Code Analyzer(IACA) to measure the throughput
  // and make it as the cost.

  static const CostTblEntry SSE42CostTblPairWise[] = {
      {ISD::FMINNUM, MVT::v2f64, 3},
      {ISD::FMINNUM, MVT::v4f32, 2},
      {ISD::SMIN, MVT::v2i64, 7}, // The data reported by the IACA is "6.8"
      {ISD::UMIN, MVT::v2i64, 8}, // The data reported by the IACA is "8.6"
      {ISD::SMIN, MVT::v4i32, 1}, // The data reported by the IACA is "1.5"
      {ISD::UMIN, MVT::v4i32, 2}, // The data reported by the IACA is "1.8"
      {ISD::SMIN, MVT::v8i16, 2},
      {ISD::UMIN, MVT::v8i16, 2},
  };

  static const CostTblEntry AVX1CostTblPairWise[] = {
      {ISD::FMINNUM, MVT::v4f32, 1},
      {ISD::FMINNUM, MVT::v4f64, 1},
      {ISD::FMINNUM, MVT::v8f32, 2},
      {ISD::SMIN, MVT::v2i64, 3},
      {ISD::UMIN, MVT::v2i64, 3},
      {ISD::SMIN, MVT::v4i32, 1},
      {ISD::UMIN, MVT::v4i32, 1},
      {ISD::SMIN, MVT::v8i16, 1},
      {ISD::UMIN, MVT::v8i16, 1},
      {ISD::SMIN, MVT::v8i32, 3},
      {ISD::UMIN, MVT::v8i32, 3},
  };

  static const CostTblEntry AVX2CostTblPairWise[] = {
      {ISD::SMIN, MVT::v4i64, 2},
      {ISD::UMIN, MVT::v4i64, 2},
      {ISD::SMIN, MVT::v8i32, 1},
      {ISD::UMIN, MVT::v8i32, 1},
      {ISD::SMIN, MVT::v16i16, 1},
      {ISD::UMIN, MVT::v16i16, 1},
      {ISD::SMIN, MVT::v32i8, 2},
      {ISD::UMIN, MVT::v32i8, 2},
  };

  static const CostTblEntry AVX512CostTblPairWise[] = {
      {ISD::FMINNUM, MVT::v8f64, 1},
      {ISD::FMINNUM, MVT::v16f32, 2},
      {ISD::SMIN, MVT::v8i64, 2},
      {ISD::UMIN, MVT::v8i64, 2},
      {ISD::SMIN, MVT::v16i32, 1},
      {ISD::UMIN, MVT::v16i32, 1},
  };

  static const CostTblEntry SSE42CostTblNoPairWise[] = {
      {ISD::FMINNUM, MVT::v2f64, 3},
      {ISD::FMINNUM, MVT::v4f32, 3},
      {ISD::SMIN, MVT::v2i64, 7}, // The data reported by the IACA is "6.8"
      {ISD::UMIN, MVT::v2i64, 9}, // The data reported by the IACA is "8.6"
      {ISD::SMIN, MVT::v4i32, 1}, // The data reported by the IACA is "1.5"
      {ISD::UMIN, MVT::v4i32, 2}, // The data reported by the IACA is "1.8"
      {ISD::SMIN, MVT::v8i16, 1}, // The data reported by the IACA is "1.5"
      {ISD::UMIN, MVT::v8i16, 2}, // The data reported by the IACA is "1.8"
  };

  static const CostTblEntry AVX1CostTblNoPairWise[] = {
      {ISD::FMINNUM, MVT::v4f32, 1},
      {ISD::FMINNUM, MVT::v4f64, 1},
      {ISD::FMINNUM, MVT::v8f32, 1},
      {ISD::SMIN, MVT::v2i64, 3},
      {ISD::UMIN, MVT::v2i64, 3},
      {ISD::SMIN, MVT::v4i32, 1},
      {ISD::UMIN, MVT::v4i32, 1},
      {ISD::SMIN, MVT::v8i16, 1},
      {ISD::UMIN, MVT::v8i16, 1},
      {ISD::SMIN, MVT::v8i32, 2},
      {ISD::UMIN, MVT::v8i32, 2},
  };

  static const CostTblEntry AVX2CostTblNoPairWise[] = {
      {ISD::SMIN, MVT::v4i64, 1},
      {ISD::UMIN, MVT::v4i64, 1},
      {ISD::SMIN, MVT::v8i32, 1},
      {ISD::UMIN, MVT::v8i32, 1},
      {ISD::SMIN, MVT::v16i16, 1},
      {ISD::UMIN, MVT::v16i16, 1},
      {ISD::SMIN, MVT::v32i8, 1},
      {ISD::UMIN, MVT::v32i8, 1},
  };

  static const CostTblEntry AVX512CostTblNoPairWise[] = {
      {ISD::FMINNUM, MVT::v8f64, 1},
      {ISD::FMINNUM, MVT::v16f32, 2},
      {ISD::SMIN, MVT::v8i64, 1},
      {ISD::UMIN, MVT::v8i64, 1},
      {ISD::SMIN, MVT::v16i32, 1},
      {ISD::UMIN, MVT::v16i32, 1},
  };

  if (IsPairwise) {
    if (ST->hasAVX512())
      if (const auto *Entry = CostTableLookup(AVX512CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTblPairWise, ISD, MTy))
        return LT.first * Entry->Cost;
  } else {
    if (ST->hasAVX512())
      if (const auto *Entry =
              CostTableLookup(AVX512CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTblNoPairWise, ISD, MTy))
        return LT.first * Entry->Cost;
  }

  return BaseT::getMinMaxReductionCost(ValTy, CondTy, IsPairwise, IsUnsigned);
}

/// Calculate the cost of materializing a 64-bit value. This helper
/// method might only calculate a fraction of a larger immediate. Therefore it
/// is valid to return a cost of ZERO.
int X86TTIImpl::getIntImmCost(int64_t Val) {
  if (Val == 0)
    return TTI::TCC_Free;

  if (isInt<32>(Val))
    return TTI::TCC_Basic;

  return 2 * TTI::TCC_Basic;
}

int X86TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  // Never hoist constants larger than 128bit, because this might lead to
  // incorrect code generation or assertions in codegen.
  // Fixme: Create a cost model for types larger than i128 once the codegen
  // issues have been fixed.
  if (BitSize > 128)
    return TTI::TCC_Free;

  if (Imm == 0)
    return TTI::TCC_Free;

  // Sign-extend all constants to a multiple of 64-bit.
  APInt ImmVal = Imm;
  if (BitSize % 64 != 0)
    ImmVal = Imm.sext(alignTo(BitSize, 64));

  // Split the constant into 64-bit chunks and calculate the cost for each
  // chunk.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < BitSize; ShiftVal += 64) {
    APInt Tmp = ImmVal.ashr(ShiftVal).sextOrTrunc(64);
    int64_t Val = Tmp.getSExtValue();
    Cost += getIntImmCost(Val);
  }
  // We need at least one instruction to materialize the constant.
  return std::max(1, Cost);
}

int X86TTIImpl::getIntImmCost(unsigned Opcode, unsigned Idx, const APInt &Imm,
                              Type *Ty) {
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
    // Always hoist the base address of a GetElementPtr. This prevents the
    // creation of new constants for every base constant that gets constant
    // folded with the offset.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Store:
    ImmIdx = 0;
    break;
  case Instruction::ICmp:
    // This is an imperfect hack to prevent constant hoisting of
    // compares that might be trying to check if a 64-bit value fits in
    // 32-bits. The backend can optimize these cases using a right shift by 32.
    // Ideally we would check the compare predicate here. There also other
    // similar immediates the backend can use shifts for.
    if (Idx == 1 && Imm.getBitWidth() == 64) {
      uint64_t ImmVal = Imm.getZExtValue();
      if (ImmVal == 0x100000000ULL || ImmVal == 0xffffffff)
        return TTI::TCC_Free;
    }
    ImmIdx = 1;
    break;
  case Instruction::And:
    // We support 64-bit ANDs with immediates with 32-bits of leading zeroes
    // by using a 32-bit operation with implicit zero extension. Detect such
    // immediates here as the normal path expects bit 31 to be sign extended.
    if (Idx == 1 && Imm.getBitWidth() == 64 && isUInt<32>(Imm.getZExtValue()))
      return TTI::TCC_Free;
    ImmIdx = 1;
    break;
  case Instruction::Add:
  case Instruction::Sub:
    // For add/sub, we can use the opposite instruction for INT32_MIN.
    if (Idx == 1 && Imm.getBitWidth() == 64 && Imm.getZExtValue() == 0x80000000)
      return TTI::TCC_Free;
    ImmIdx = 1;
    break;
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    // Division by constant is typically expanded later into a different
    // instruction sequence. This completely changes the constants.
    // Report them as "free" to stop ConstantHoist from marking them as opaque.
    return TTI::TCC_Free;
  case Instruction::Mul:
  case Instruction::Or:
  case Instruction::Xor:
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
    int NumConstants = divideCeil(BitSize, 64);
    int Cost = X86TTIImpl::getIntImmCost(Imm, Ty);
    return (Cost <= NumConstants * TTI::TCC_Basic)
               ? static_cast<int>(TTI::TCC_Free)
               : Cost;
  }

  return X86TTIImpl::getIntImmCost(Imm, Ty);
}

int X86TTIImpl::getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                              Type *Ty) {
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
    if ((Idx == 1) && Imm.getBitWidth() <= 64 && isInt<32>(Imm.getSExtValue()))
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
  return X86TTIImpl::getIntImmCost(Imm, Ty);
}

unsigned X86TTIImpl::getUserCost(const User *U,
                                 ArrayRef<const Value *> Operands) {
  if (isa<StoreInst>(U)) {
    Value *Ptr = U->getOperand(1);
    // Store instruction with index and scale costs 2 Uops.
    // Check the preceding GEP to identify non-const indices.
    if (auto GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
      if (!all_of(GEP->indices(), [](Value *V) { return isa<Constant>(V); }))
        return TTI::TCC_Basic * 2;
    }
    return TTI::TCC_Basic;
  }
  return BaseT::getUserCost(U, Operands);
}

// Return an average cost of Gather / Scatter instruction, maybe improved later
int X86TTIImpl::getGSVectorCost(unsigned Opcode, Type *SrcVTy, Value *Ptr,
                                unsigned Alignment, unsigned AddressSpace) {

  assert(isa<VectorType>(SrcVTy) && "Unexpected type in getGSVectorCost");
  unsigned VF = SrcVTy->getVectorNumElements();

  // Try to reduce index size from 64 bit (default for GEP)
  // to 32. It is essential for VF 16. If the index can't be reduced to 32, the
  // operation will use 16 x 64 indices which do not fit in a zmm and needs
  // to split. Also check that the base pointer is the same for all lanes,
  // and that there's at most one variable index.
  auto getIndexSizeInBits = [](Value *Ptr, const DataLayout& DL) {
    unsigned IndexSize = DL.getPointerSizeInBits();
    GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
    if (IndexSize < 64 || !GEP)
      return IndexSize;

    unsigned NumOfVarIndices = 0;
    Value *Ptrs = GEP->getPointerOperand();
    if (Ptrs->getType()->isVectorTy() && !getSplatValue(Ptrs))
      return IndexSize;
    for (unsigned i = 1; i < GEP->getNumOperands(); ++i) {
      if (isa<Constant>(GEP->getOperand(i)))
        continue;
      Type *IndxTy = GEP->getOperand(i)->getType();
      if (IndxTy->isVectorTy())
        IndxTy = IndxTy->getVectorElementType();
      if ((IndxTy->getPrimitiveSizeInBits() == 64 &&
          !isa<SExtInst>(GEP->getOperand(i))) ||
         ++NumOfVarIndices > 1)
        return IndexSize; // 64
    }
    return (unsigned)32;
  };


  // Trying to reduce IndexSize to 32 bits for vector 16.
  // By default the IndexSize is equal to pointer size.
  unsigned IndexSize = (ST->hasAVX512() && VF >= 16)
                           ? getIndexSizeInBits(Ptr, DL)
                           : DL.getPointerSizeInBits();

  Type *IndexVTy = VectorType::get(IntegerType::get(SrcVTy->getContext(),
                                                    IndexSize), VF);
  std::pair<int, MVT> IdxsLT = TLI->getTypeLegalizationCost(DL, IndexVTy);
  std::pair<int, MVT> SrcLT = TLI->getTypeLegalizationCost(DL, SrcVTy);
  int SplitFactor = std::max(IdxsLT.first, SrcLT.first);
  if (SplitFactor > 1) {
    // Handle splitting of vector of pointers
    Type *SplitSrcTy = VectorType::get(SrcVTy->getScalarType(), VF / SplitFactor);
    return SplitFactor * getGSVectorCost(Opcode, SplitSrcTy, Ptr, Alignment,
                                         AddressSpace);
  }

  // The gather / scatter cost is given by Intel architects. It is a rough
  // number since we are looking at one instruction in a time.
  const int GSOverhead = (Opcode == Instruction::Load)
                             ? ST->getGatherOverhead()
                             : ST->getScatterOverhead();
  return GSOverhead + VF * getMemoryOpCost(Opcode, SrcVTy->getScalarType(),
                                           Alignment, AddressSpace);
}

/// Return the cost of full scalarization of gather / scatter operation.
///
/// Opcode - Load or Store instruction.
/// SrcVTy - The type of the data vector that should be gathered or scattered.
/// VariableMask - The mask is non-constant at compile time.
/// Alignment - Alignment for one element.
/// AddressSpace - pointer[s] address space.
///
int X86TTIImpl::getGSScalarCost(unsigned Opcode, Type *SrcVTy,
                                bool VariableMask, unsigned Alignment,
                                unsigned AddressSpace) {
  unsigned VF = SrcVTy->getVectorNumElements();

  int MaskUnpackCost = 0;
  if (VariableMask) {
    VectorType *MaskTy =
      VectorType::get(Type::getInt1Ty(SrcVTy->getContext()), VF);
    MaskUnpackCost = getScalarizationOverhead(MaskTy, false, true);
    int ScalarCompareCost =
      getCmpSelInstrCost(Instruction::ICmp, Type::getInt1Ty(SrcVTy->getContext()),
                         nullptr);
    int BranchCost = getCFInstrCost(Instruction::Br);
    MaskUnpackCost += VF * (BranchCost + ScalarCompareCost);
  }

  // The cost of the scalar loads/stores.
  int MemoryOpCost = VF * getMemoryOpCost(Opcode, SrcVTy->getScalarType(),
                                          Alignment, AddressSpace);

  int InsertExtractCost = 0;
  if (Opcode == Instruction::Load)
    for (unsigned i = 0; i < VF; ++i)
      // Add the cost of inserting each scalar load into the vector
      InsertExtractCost +=
        getVectorInstrCost(Instruction::InsertElement, SrcVTy, i);
  else
    for (unsigned i = 0; i < VF; ++i)
      // Add the cost of extracting each element out of the data vector
      InsertExtractCost +=
        getVectorInstrCost(Instruction::ExtractElement, SrcVTy, i);

  return MemoryOpCost + MaskUnpackCost + InsertExtractCost;
}

/// Calculate the cost of Gather / Scatter operation
int X86TTIImpl::getGatherScatterOpCost(unsigned Opcode, Type *SrcVTy,
                                       Value *Ptr, bool VariableMask,
                                       unsigned Alignment) {
  assert(SrcVTy->isVectorTy() && "Unexpected data type for Gather/Scatter");
  unsigned VF = SrcVTy->getVectorNumElements();
  PointerType *PtrTy = dyn_cast<PointerType>(Ptr->getType());
  if (!PtrTy && Ptr->getType()->isVectorTy())
    PtrTy = dyn_cast<PointerType>(Ptr->getType()->getVectorElementType());
  assert(PtrTy && "Unexpected type for Ptr argument");
  unsigned AddressSpace = PtrTy->getAddressSpace();

  bool Scalarize = false;
  if ((Opcode == Instruction::Load && !isLegalMaskedGather(SrcVTy)) ||
      (Opcode == Instruction::Store && !isLegalMaskedScatter(SrcVTy)))
    Scalarize = true;
  // Gather / Scatter for vector 2 is not profitable on KNL / SKX
  // Vector-4 of gather/scatter instruction does not exist on KNL.
  // We can extend it to 8 elements, but zeroing upper bits of
  // the mask vector will add more instructions. Right now we give the scalar
  // cost of vector-4 for KNL. TODO: Check, maybe the gather/scatter instruction
  // is better in the VariableMask case.
  if (ST->hasAVX512() && (VF == 2 || (VF == 4 && !ST->hasVLX())))
    Scalarize = true;

  if (Scalarize)
    return getGSScalarCost(Opcode, SrcVTy, VariableMask, Alignment,
                           AddressSpace);

  return getGSVectorCost(Opcode, SrcVTy, Ptr, Alignment, AddressSpace);
}

bool X86TTIImpl::isLSRCostLess(TargetTransformInfo::LSRCost &C1,
                               TargetTransformInfo::LSRCost &C2) {
    // X86 specific here are "instruction number 1st priority".
    return std::tie(C1.Insns, C1.NumRegs, C1.AddRecCost,
                    C1.NumIVMuls, C1.NumBaseAdds,
                    C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.Insns, C2.NumRegs, C2.AddRecCost,
                    C2.NumIVMuls, C2.NumBaseAdds,
                    C2.ScaleCost, C2.ImmCost, C2.SetupCost);
}

bool X86TTIImpl::canMacroFuseCmp() {
  return ST->hasMacroFusion();
}

bool X86TTIImpl::isLegalMaskedLoad(Type *DataTy) {
  // The backend can't handle a single element vector.
  if (isa<VectorType>(DataTy) && DataTy->getVectorNumElements() == 1)
    return false;
  Type *ScalarTy = DataTy->getScalarType();
  int DataWidth = isa<PointerType>(ScalarTy) ?
    DL.getPointerSizeInBits() : ScalarTy->getPrimitiveSizeInBits();

  return ((DataWidth == 32 || DataWidth == 64) && ST->hasAVX()) ||
         ((DataWidth == 8 || DataWidth == 16) && ST->hasBWI());
}

bool X86TTIImpl::isLegalMaskedStore(Type *DataType) {
  return isLegalMaskedLoad(DataType);
}

bool X86TTIImpl::isLegalMaskedGather(Type *DataTy) {
  // This function is called now in two cases: from the Loop Vectorizer
  // and from the Scalarizer.
  // When the Loop Vectorizer asks about legality of the feature,
  // the vectorization factor is not calculated yet. The Loop Vectorizer
  // sends a scalar type and the decision is based on the width of the
  // scalar element.
  // Later on, the cost model will estimate usage this intrinsic based on
  // the vector type.
  // The Scalarizer asks again about legality. It sends a vector type.
  // In this case we can reject non-power-of-2 vectors.
  // We also reject single element vectors as the type legalizer can't
  // scalarize it.
  if (isa<VectorType>(DataTy)) {
    unsigned NumElts = DataTy->getVectorNumElements();
    if (NumElts == 1 || !isPowerOf2_32(NumElts))
      return false;
  }
  Type *ScalarTy = DataTy->getScalarType();
  int DataWidth = isa<PointerType>(ScalarTy) ?
    DL.getPointerSizeInBits() : ScalarTy->getPrimitiveSizeInBits();

  // Some CPUs have better gather performance than others.
  // TODO: Remove the explicit ST->hasAVX512()?, That would mean we would only
  // enable gather with a -march.
  return (DataWidth == 32 || DataWidth == 64) &&
         (ST->hasAVX512() || (ST->hasFastGather() && ST->hasAVX2()));
}

bool X86TTIImpl::isLegalMaskedScatter(Type *DataType) {
  // AVX2 doesn't support scatter
  if (!ST->hasAVX512())
    return false;
  return isLegalMaskedGather(DataType);
}

bool X86TTIImpl::hasDivRemOp(Type *DataType, bool IsSigned) {
  EVT VT = TLI->getValueType(DL, DataType);
  return TLI->isOperationLegal(IsSigned ? ISD::SDIVREM : ISD::UDIVREM, VT);
}

bool X86TTIImpl::isFCmpOrdCheaperThanFCmpZero(Type *Ty) {
  return false;
}

bool X86TTIImpl::areInlineCompatible(const Function *Caller,
                                     const Function *Callee) const {
  const TargetMachine &TM = getTLI()->getTargetMachine();

  // Work this as a subsetting of subtarget features.
  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  // FIXME: This is likely too limiting as it will include subtarget features
  // that we might not care about for inlining, but it is conservatively
  // correct.
  return (CallerBits & CalleeBits) == CalleeBits;
}

const X86TTIImpl::TTI::MemCmpExpansionOptions *
X86TTIImpl::enableMemCmpExpansion(bool IsZeroCmp) const {
  // Only enable vector loads for equality comparison.
  // Right now the vector version is not as fast, see #33329.
  static const auto ThreeWayOptions = [this]() {
    TTI::MemCmpExpansionOptions Options;
    if (ST->is64Bit()) {
      Options.LoadSizes.push_back(8);
    }
    Options.LoadSizes.push_back(4);
    Options.LoadSizes.push_back(2);
    Options.LoadSizes.push_back(1);
    return Options;
  }();
  static const auto EqZeroOptions = [this]() {
    TTI::MemCmpExpansionOptions Options;
    // TODO: enable AVX512 when the DAG is ready.
    // if (ST->hasAVX512()) Options.LoadSizes.push_back(64);
    if (ST->hasAVX2()) Options.LoadSizes.push_back(32);
    if (ST->hasSSE2()) Options.LoadSizes.push_back(16);
    if (ST->is64Bit()) {
      Options.LoadSizes.push_back(8);
    }
    Options.LoadSizes.push_back(4);
    Options.LoadSizes.push_back(2);
    Options.LoadSizes.push_back(1);
    // All GPR and vector loads can be unaligned. SIMD compare requires integer
    // vectors (SSE2/AVX2).
    Options.AllowOverlappingLoads = true;
    return Options;
  }();
  return IsZeroCmp ? &EqZeroOptions : &ThreeWayOptions;
}

bool X86TTIImpl::enableInterleavedAccessVectorization() {
  // TODO: We expect this to be beneficial regardless of arch,
  // but there are currently some unexplained performance artifacts on Atom.
  // As a temporary solution, disable on Atom.
  return !(ST->isAtom());
}

// Get estimation for interleaved load/store operations for AVX2.
// \p Factor is the interleaved-access factor (stride) - number of
// (interleaved) elements in the group.
// \p Indices contains the indices for a strided load: when the
// interleaved load has gaps they indicate which elements are used.
// If Indices is empty (or if the number of indices is equal to the size
// of the interleaved-access as given in \p Factor) the access has no gaps.
//
// As opposed to AVX-512, AVX2 does not have generic shuffles that allow
// computing the cost using a generic formula as a function of generic
// shuffles. We therefore use a lookup table instead, filled according to
// the instruction sequences that codegen currently generates.
int X86TTIImpl::getInterleavedMemoryOpCostAVX2(unsigned Opcode, Type *VecTy,
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

  // We currently Support only fully-interleaved groups, with no gaps.
  // TODO: Support also strided loads (interleaved-groups with gaps).
  if (Indices.size() && Indices.size() != Factor)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace);

  // VecTy for interleave memop is <VF*Factor x Elt>.
  // So, for VF=4, Interleave Factor = 3, Element type = i32 we have
  // VecTy = <12 x i32>.
  MVT LegalVT = getTLI()->getTypeLegalizationCost(DL, VecTy).second;

  // This function can be called with VecTy=<6xi128>, Factor=3, in which case
  // the VF=2, while v2i128 is an unsupported MVT vector type
  // (see MachineValueType.h::getVectorVT()).
  if (!LegalVT.isVector())
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace);

  unsigned VF = VecTy->getVectorNumElements() / Factor;
  Type *ScalarTy = VecTy->getVectorElementType();

  // Calculate the number of memory operations (NumOfMemOps), required
  // for load/store the VecTy.
  unsigned VecTySize = DL.getTypeStoreSize(VecTy);
  unsigned LegalVTSize = LegalVT.getStoreSize();
  unsigned NumOfMemOps = (VecTySize + LegalVTSize - 1) / LegalVTSize;

  // Get the cost of one memory operation.
  Type *SingleMemOpTy = VectorType::get(VecTy->getVectorElementType(),
                                        LegalVT.getVectorNumElements());
  unsigned MemOpCost =
      getMemoryOpCost(Opcode, SingleMemOpTy, Alignment, AddressSpace);

  VectorType *VT = VectorType::get(ScalarTy, VF);
  EVT ETy = TLI->getValueType(DL, VT);
  if (!ETy.isSimple())
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace);

  // TODO: Complete for other data-types and strides.
  // Each combination of Stride, ElementTy and VF results in a different
  // sequence; The cost tables are therefore accessed with:
  // Factor (stride) and VectorType=VFxElemType.
  // The Cost accounts only for the shuffle sequence;
  // The cost of the loads/stores is accounted for separately.
  //
  static const CostTblEntry AVX2InterleavedLoadTbl[] = {
    { 2, MVT::v4i64, 6 }, //(load 8i64 and) deinterleave into 2 x 4i64
    { 2, MVT::v4f64, 6 }, //(load 8f64 and) deinterleave into 2 x 4f64

    { 3, MVT::v2i8,  10 }, //(load 6i8 and)  deinterleave into 3 x 2i8
    { 3, MVT::v4i8,  4 },  //(load 12i8 and) deinterleave into 3 x 4i8
    { 3, MVT::v8i8,  9 },  //(load 24i8 and) deinterleave into 3 x 8i8
    { 3, MVT::v16i8, 11},  //(load 48i8 and) deinterleave into 3 x 16i8
    { 3, MVT::v32i8, 13},  //(load 96i8 and) deinterleave into 3 x 32i8
    { 3, MVT::v8f32, 17 }, //(load 24f32 and)deinterleave into 3 x 8f32

    { 4, MVT::v2i8,  12 }, //(load 8i8 and)   deinterleave into 4 x 2i8
    { 4, MVT::v4i8,  4 },  //(load 16i8 and)  deinterleave into 4 x 4i8
    { 4, MVT::v8i8,  20 }, //(load 32i8 and)  deinterleave into 4 x 8i8
    { 4, MVT::v16i8, 39 }, //(load 64i8 and)  deinterleave into 4 x 16i8
    { 4, MVT::v32i8, 80 }, //(load 128i8 and) deinterleave into 4 x 32i8

    { 8, MVT::v8f32, 40 }  //(load 64f32 and)deinterleave into 8 x 8f32
  };

  static const CostTblEntry AVX2InterleavedStoreTbl[] = {
    { 2, MVT::v4i64, 6 }, //interleave into 2 x 4i64 into 8i64 (and store)
    { 2, MVT::v4f64, 6 }, //interleave into 2 x 4f64 into 8f64 (and store)

    { 3, MVT::v2i8,  7 },  //interleave 3 x 2i8  into 6i8 (and store)
    { 3, MVT::v4i8,  8 },  //interleave 3 x 4i8  into 12i8 (and store)
    { 3, MVT::v8i8,  11 }, //interleave 3 x 8i8  into 24i8 (and store)
    { 3, MVT::v16i8, 11 }, //interleave 3 x 16i8 into 48i8 (and store)
    { 3, MVT::v32i8, 13 }, //interleave 3 x 32i8 into 96i8 (and store)

    { 4, MVT::v2i8,  12 }, //interleave 4 x 2i8  into 8i8 (and store)
    { 4, MVT::v4i8,  9 },  //interleave 4 x 4i8  into 16i8 (and store)
    { 4, MVT::v8i8,  10 }, //interleave 4 x 8i8  into 32i8 (and store)
    { 4, MVT::v16i8, 10 }, //interleave 4 x 16i8 into 64i8 (and store)
    { 4, MVT::v32i8, 12 }  //interleave 4 x 32i8 into 128i8 (and store)
  };

  if (Opcode == Instruction::Load) {
    if (const auto *Entry =
            CostTableLookup(AVX2InterleavedLoadTbl, Factor, ETy.getSimpleVT()))
      return NumOfMemOps * MemOpCost + Entry->Cost;
  } else {
    assert(Opcode == Instruction::Store &&
           "Expected Store Instruction at this  point");
    if (const auto *Entry =
            CostTableLookup(AVX2InterleavedStoreTbl, Factor, ETy.getSimpleVT()))
      return NumOfMemOps * MemOpCost + Entry->Cost;
  }

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace);
}

// Get estimation for interleaved load/store operations and strided load.
// \p Indices contains indices for strided load.
// \p Factor - the factor of interleaving.
// AVX-512 provides 3-src shuffles that significantly reduces the cost.
int X86TTIImpl::getInterleavedMemoryOpCostAVX512(unsigned Opcode, Type *VecTy,
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

  // VecTy for interleave memop is <VF*Factor x Elt>.
  // So, for VF=4, Interleave Factor = 3, Element type = i32 we have
  // VecTy = <12 x i32>.

  // Calculate the number of memory operations (NumOfMemOps), required
  // for load/store the VecTy.
  MVT LegalVT = getTLI()->getTypeLegalizationCost(DL, VecTy).second;
  unsigned VecTySize = DL.getTypeStoreSize(VecTy);
  unsigned LegalVTSize = LegalVT.getStoreSize();
  unsigned NumOfMemOps = (VecTySize + LegalVTSize - 1) / LegalVTSize;

  // Get the cost of one memory operation.
  Type *SingleMemOpTy = VectorType::get(VecTy->getVectorElementType(),
                                        LegalVT.getVectorNumElements());
  unsigned MemOpCost =
      getMemoryOpCost(Opcode, SingleMemOpTy, Alignment, AddressSpace);

  unsigned VF = VecTy->getVectorNumElements() / Factor;
  MVT VT = MVT::getVectorVT(MVT::getVT(VecTy->getScalarType()), VF);

  if (Opcode == Instruction::Load) {
    // The tables (AVX512InterleavedLoadTbl and AVX512InterleavedStoreTbl)
    // contain the cost of the optimized shuffle sequence that the
    // X86InterleavedAccess pass will generate.
    // The cost of loads and stores are computed separately from the table.

    // X86InterleavedAccess support only the following interleaved-access group.
    static const CostTblEntry AVX512InterleavedLoadTbl[] = {
        {3, MVT::v16i8, 12}, //(load 48i8 and) deinterleave into 3 x 16i8
        {3, MVT::v32i8, 14}, //(load 96i8 and) deinterleave into 3 x 32i8
        {3, MVT::v64i8, 22}, //(load 96i8 and) deinterleave into 3 x 32i8
    };

    if (const auto *Entry =
            CostTableLookup(AVX512InterleavedLoadTbl, Factor, VT))
      return NumOfMemOps * MemOpCost + Entry->Cost;
    //If an entry does not exist, fallback to the default implementation.

    // Kind of shuffle depends on number of loaded values.
    // If we load the entire data in one register, we can use a 1-src shuffle.
    // Otherwise, we'll merge 2 sources in each operation.
    TTI::ShuffleKind ShuffleKind =
        (NumOfMemOps > 1) ? TTI::SK_PermuteTwoSrc : TTI::SK_PermuteSingleSrc;

    unsigned ShuffleCost =
        getShuffleCost(ShuffleKind, SingleMemOpTy, 0, nullptr);

    unsigned NumOfLoadsInInterleaveGrp =
        Indices.size() ? Indices.size() : Factor;
    Type *ResultTy = VectorType::get(VecTy->getVectorElementType(),
                                     VecTy->getVectorNumElements() / Factor);
    unsigned NumOfResults =
        getTLI()->getTypeLegalizationCost(DL, ResultTy).first *
        NumOfLoadsInInterleaveGrp;

    // About a half of the loads may be folded in shuffles when we have only
    // one result. If we have more than one result, we do not fold loads at all.
    unsigned NumOfUnfoldedLoads =
        NumOfResults > 1 ? NumOfMemOps : NumOfMemOps / 2;

    // Get a number of shuffle operations per result.
    unsigned NumOfShufflesPerResult =
        std::max((unsigned)1, (unsigned)(NumOfMemOps - 1));

    // The SK_MergeTwoSrc shuffle clobbers one of src operands.
    // When we have more than one destination, we need additional instructions
    // to keep sources.
    unsigned NumOfMoves = 0;
    if (NumOfResults > 1 && ShuffleKind == TTI::SK_PermuteTwoSrc)
      NumOfMoves = NumOfResults * NumOfShufflesPerResult / 2;

    int Cost = NumOfResults * NumOfShufflesPerResult * ShuffleCost +
               NumOfUnfoldedLoads * MemOpCost + NumOfMoves;

    return Cost;
  }

  // Store.
  assert(Opcode == Instruction::Store &&
         "Expected Store Instruction at this  point");
  // X86InterleavedAccess support only the following interleaved-access group.
  static const CostTblEntry AVX512InterleavedStoreTbl[] = {
      {3, MVT::v16i8, 12}, // interleave 3 x 16i8 into 48i8 (and store)
      {3, MVT::v32i8, 14}, // interleave 3 x 32i8 into 96i8 (and store)
      {3, MVT::v64i8, 26}, // interleave 3 x 64i8 into 96i8 (and store)

      {4, MVT::v8i8, 10},  // interleave 4 x 8i8  into 32i8  (and store)
      {4, MVT::v16i8, 11}, // interleave 4 x 16i8 into 64i8  (and store)
      {4, MVT::v32i8, 14}, // interleave 4 x 32i8 into 128i8 (and store)
      {4, MVT::v64i8, 24}  // interleave 4 x 32i8 into 256i8 (and store)
  };

  if (const auto *Entry =
          CostTableLookup(AVX512InterleavedStoreTbl, Factor, VT))
    return NumOfMemOps * MemOpCost + Entry->Cost;
  //If an entry does not exist, fallback to the default implementation.

  // There is no strided stores meanwhile. And store can't be folded in
  // shuffle.
  unsigned NumOfSources = Factor; // The number of values to be merged.
  unsigned ShuffleCost =
      getShuffleCost(TTI::SK_PermuteTwoSrc, SingleMemOpTy, 0, nullptr);
  unsigned NumOfShufflesPerStore = NumOfSources - 1;

  // The SK_MergeTwoSrc shuffle clobbers one of src operands.
  // We need additional instructions to keep sources.
  unsigned NumOfMoves = NumOfMemOps * NumOfShufflesPerStore / 2;
  int Cost = NumOfMemOps * (MemOpCost + NumOfShufflesPerStore * ShuffleCost) +
             NumOfMoves;
  return Cost;
}

int X86TTIImpl::getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                           unsigned Factor,
                                           ArrayRef<unsigned> Indices,
                                           unsigned Alignment,
                                           unsigned AddressSpace,
                                           bool UseMaskForCond,
                                           bool UseMaskForGaps) {
  auto isSupportedOnAVX512 = [](Type *VecTy, bool HasBW) {
    Type *EltTy = VecTy->getVectorElementType();
    if (EltTy->isFloatTy() || EltTy->isDoubleTy() || EltTy->isIntegerTy(64) ||
        EltTy->isIntegerTy(32) || EltTy->isPointerTy())
      return true;
    if (EltTy->isIntegerTy(16) || EltTy->isIntegerTy(8))
      return HasBW;
    return false;
  };
  if (ST->hasAVX512() && isSupportedOnAVX512(VecTy, ST->hasBWI()))
    return getInterleavedMemoryOpCostAVX512(Opcode, VecTy, Factor, Indices,
                                            Alignment, AddressSpace,
                                            UseMaskForCond, UseMaskForGaps);
  if (ST->hasAVX2())
    return getInterleavedMemoryOpCostAVX2(Opcode, VecTy, Factor, Indices,
                                          Alignment, AddressSpace,
                                          UseMaskForCond, UseMaskForGaps);

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace,
                                           UseMaskForCond, UseMaskForGaps);
}
