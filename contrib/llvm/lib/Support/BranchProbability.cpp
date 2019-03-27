//===-------------- lib/Support/BranchProbability.cpp -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements Branch Probability class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BranchProbability.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

const uint32_t BranchProbability::D;

raw_ostream &BranchProbability::print(raw_ostream &OS) const {
  if (isUnknown())
    return OS << "?%";

  // Get a percentage rounded to two decimal digits. This avoids
  // implementation-defined rounding inside printf.
  double Percent = rint(((double)N / D) * 100.0 * 100.0) / 100.0;
  return OS << format("0x%08" PRIx32 " / 0x%08" PRIx32 " = %.2f%%", N, D,
                      Percent);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void BranchProbability::dump() const { print(dbgs()) << '\n'; }
#endif

BranchProbability::BranchProbability(uint32_t Numerator, uint32_t Denominator) {
  assert(Denominator > 0 && "Denominator cannot be 0!");
  assert(Numerator <= Denominator && "Probability cannot be bigger than 1!");
  if (Denominator == D)
    N = Numerator;
  else {
    uint64_t Prob64 =
        (Numerator * static_cast<uint64_t>(D) + Denominator / 2) / Denominator;
    N = static_cast<uint32_t>(Prob64);
  }
}

BranchProbability
BranchProbability::getBranchProbability(uint64_t Numerator,
                                        uint64_t Denominator) {
  assert(Numerator <= Denominator && "Probability cannot be bigger than 1!");
  // Scale down Denominator to fit in a 32-bit integer.
  int Scale = 0;
  while (Denominator > UINT32_MAX) {
    Denominator >>= 1;
    Scale++;
  }
  return BranchProbability(Numerator >> Scale, Denominator);
}

// If ConstD is not zero, then replace D by ConstD so that division and modulo
// operations by D can be optimized, in case this function is not inlined by the
// compiler.
template <uint32_t ConstD>
static uint64_t scale(uint64_t Num, uint32_t N, uint32_t D) {
  if (ConstD > 0)
    D = ConstD;

  assert(D && "divide by 0");

  // Fast path for multiplying by 1.0.
  if (!Num || D == N)
    return Num;

  // Split Num into upper and lower parts to multiply, then recombine.
  uint64_t ProductHigh = (Num >> 32) * N;
  uint64_t ProductLow = (Num & UINT32_MAX) * N;

  // Split into 32-bit digits.
  uint32_t Upper32 = ProductHigh >> 32;
  uint32_t Lower32 = ProductLow & UINT32_MAX;
  uint32_t Mid32Partial = ProductHigh & UINT32_MAX;
  uint32_t Mid32 = Mid32Partial + (ProductLow >> 32);

  // Carry.
  Upper32 += Mid32 < Mid32Partial;

  // Check for overflow.
  if (Upper32 >= D)
    return UINT64_MAX;

  uint64_t Rem = (uint64_t(Upper32) << 32) | Mid32;
  uint64_t UpperQ = Rem / D;

  // Check for overflow.
  if (UpperQ > UINT32_MAX)
    return UINT64_MAX;

  Rem = ((Rem % D) << 32) | Lower32;
  uint64_t LowerQ = Rem / D;
  uint64_t Q = (UpperQ << 32) + LowerQ;

  // Check for overflow.
  return Q < LowerQ ? UINT64_MAX : Q;
}

uint64_t BranchProbability::scale(uint64_t Num) const {
  return ::scale<D>(Num, N, D);
}

uint64_t BranchProbability::scaleByInverse(uint64_t Num) const {
  return ::scale<0>(Num, D, N);
}
