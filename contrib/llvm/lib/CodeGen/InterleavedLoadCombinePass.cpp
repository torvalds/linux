//===- InterleavedLoadCombine.cpp - Combine Interleaved Loads ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
//
// This file defines the interleaved-load-combine pass. The pass searches for
// ShuffleVectorInstruction that execute interleaving loads. If a matching
// pattern is found, it adds a combined load and further instructions in a
// pattern that is detectable by InterleavedAccesPass. The old instructions are
// left dead to be removed later. The pass is specifically designed to be
// executed just before InterleavedAccesPass to find any left-over instances
// that are not detected within former passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#include <algorithm>
#include <cassert>
#include <list>

using namespace llvm;

#define DEBUG_TYPE "interleaved-load-combine"

namespace {

/// Statistic counter
STATISTIC(NumInterleavedLoadCombine, "Number of combined loads");

/// Option to disable the pass
static cl::opt<bool> DisableInterleavedLoadCombine(
    "disable-" DEBUG_TYPE, cl::init(false), cl::Hidden,
    cl::desc("Disable combining of interleaved loads"));

struct VectorInfo;

struct InterleavedLoadCombineImpl {
public:
  InterleavedLoadCombineImpl(Function &F, DominatorTree &DT, MemorySSA &MSSA,
                             TargetMachine &TM)
      : F(F), DT(DT), MSSA(MSSA),
        TLI(*TM.getSubtargetImpl(F)->getTargetLowering()),
        TTI(TM.getTargetTransformInfo(F)) {}

  /// Scan the function for interleaved load candidates and execute the
  /// replacement if applicable.
  bool run();

private:
  /// Function this pass is working on
  Function &F;

  /// Dominator Tree Analysis
  DominatorTree &DT;

  /// Memory Alias Analyses
  MemorySSA &MSSA;

  /// Target Lowering Information
  const TargetLowering &TLI;

  /// Target Transform Information
  const TargetTransformInfo TTI;

  /// Find the instruction in sets LIs that dominates all others, return nullptr
  /// if there is none.
  LoadInst *findFirstLoad(const std::set<LoadInst *> &LIs);

  /// Replace interleaved load candidates. It does additional
  /// analyses if this makes sense. Returns true on success and false
  /// of nothing has been changed.
  bool combine(std::list<VectorInfo> &InterleavedLoad,
               OptimizationRemarkEmitter &ORE);

  /// Given a set of VectorInfo containing candidates for a given interleave
  /// factor, find a set that represents a 'factor' interleaved load.
  bool findPattern(std::list<VectorInfo> &Candidates,
                   std::list<VectorInfo> &InterleavedLoad, unsigned Factor,
                   const DataLayout &DL);
}; // InterleavedLoadCombine

/// First Order Polynomial on an n-Bit Integer Value
///
/// Polynomial(Value) = Value * B + A + E*2^(n-e)
///
/// A and B are the coefficients. E*2^(n-e) is an error within 'e' most
/// significant bits. It is introduced if an exact computation cannot be proven
/// (e.q. division by 2).
///
/// As part of this optimization multiple loads will be combined. It necessary
/// to prove that loads are within some relative offset to each other. This
/// class is used to prove relative offsets of values loaded from memory.
///
/// Representing an integer in this form is sound since addition in two's
/// complement is associative (trivial) and multiplication distributes over the
/// addition (see Proof(1) in Polynomial::mul). Further, both operations
/// commute.
//
// Example:
// declare @fn(i64 %IDX, <4 x float>* %PTR) {
//   %Pa1 = add i64 %IDX, 2
//   %Pa2 = lshr i64 %Pa1, 1
//   %Pa3 = getelementptr inbounds <4 x float>, <4 x float>* %PTR, i64 %Pa2
//   %Va = load <4 x float>, <4 x float>* %Pa3
//
//   %Pb1 = add i64 %IDX, 4
//   %Pb2 = lshr i64 %Pb1, 1
//   %Pb3 = getelementptr inbounds <4 x float>, <4 x float>* %PTR, i64 %Pb2
//   %Vb = load <4 x float>, <4 x float>* %Pb3
// ... }
//
// The goal is to prove that two loads load consecutive addresses.
//
// In this case the polynomials are constructed by the following
// steps.
//
// The number tag #e specifies the error bits.
//
// Pa_0 = %IDX              #0
// Pa_1 = %IDX + 2          #0 | add 2
// Pa_2 = %IDX/2 + 1        #1 | lshr 1
// Pa_3 = %IDX/2 + 1        #1 | GEP, step signext to i64
// Pa_4 = (%IDX/2)*16 + 16  #0 | GEP, multiply index by sizeof(4) for floats
// Pa_5 = (%IDX/2)*16 + 16  #0 | GEP, add offset of leading components
//
// Pb_0 = %IDX              #0
// Pb_1 = %IDX + 4          #0 | add 2
// Pb_2 = %IDX/2 + 2        #1 | lshr 1
// Pb_3 = %IDX/2 + 2        #1 | GEP, step signext to i64
// Pb_4 = (%IDX/2)*16 + 32  #0 | GEP, multiply index by sizeof(4) for floats
// Pb_5 = (%IDX/2)*16 + 16  #0 | GEP, add offset of leading components
//
// Pb_5 - Pa_5 = 16         #0 | subtract to get the offset
//
// Remark: %PTR is not maintained within this class. So in this instance the
// offset of 16 can only be assumed if the pointers are equal.
//
class Polynomial {
  /// Operations on B
  enum BOps {
    LShr,
    Mul,
    SExt,
    Trunc,
  };

  /// Number of Error Bits e
  unsigned ErrorMSBs;

  /// Value
  Value *V;

  /// Coefficient B
  SmallVector<std::pair<BOps, APInt>, 4> B;

  /// Coefficient A
  APInt A;

public:
  Polynomial(Value *V) : ErrorMSBs((unsigned)-1), V(V), B(), A() {
    IntegerType *Ty = dyn_cast<IntegerType>(V->getType());
    if (Ty) {
      ErrorMSBs = 0;
      this->V = V;
      A = APInt(Ty->getBitWidth(), 0);
    }
  }

  Polynomial(const APInt &A, unsigned ErrorMSBs = 0)
      : ErrorMSBs(ErrorMSBs), V(NULL), B(), A(A) {}

  Polynomial(unsigned BitWidth, uint64_t A, unsigned ErrorMSBs = 0)
      : ErrorMSBs(ErrorMSBs), V(NULL), B(), A(BitWidth, A) {}

  Polynomial() : ErrorMSBs((unsigned)-1), V(NULL), B(), A() {}

  /// Increment and clamp the number of undefined bits.
  void incErrorMSBs(unsigned amt) {
    if (ErrorMSBs == (unsigned)-1)
      return;

    ErrorMSBs += amt;
    if (ErrorMSBs > A.getBitWidth())
      ErrorMSBs = A.getBitWidth();
  }

  /// Decrement and clamp the number of undefined bits.
  void decErrorMSBs(unsigned amt) {
    if (ErrorMSBs == (unsigned)-1)
      return;

    if (ErrorMSBs > amt)
      ErrorMSBs -= amt;
    else
      ErrorMSBs = 0;
  }

  /// Apply an add on the polynomial
  Polynomial &add(const APInt &C) {
    // Note: Addition is associative in two's complement even when in case of
    // signed overflow.
    //
    // Error bits can only propagate into higher significant bits. As these are
    // already regarded as undefined, there is no change.
    //
    // Theorem: Adding a constant to a polynomial does not change the error
    // term.
    //
    // Proof:
    //
    //   Since the addition is associative and commutes:
    //
    //   (B + A + E*2^(n-e)) + C = B + (A + C) + E*2^(n-e)
    // [qed]

    if (C.getBitWidth() != A.getBitWidth()) {
      ErrorMSBs = (unsigned)-1;
      return *this;
    }

    A += C;
    return *this;
  }

  /// Apply a multiplication onto the polynomial.
  Polynomial &mul(const APInt &C) {
    // Note: Multiplication distributes over the addition
    //
    // Theorem: Multiplication distributes over the addition
    //
    // Proof(1):
    //
    //   (B+A)*C =-
    //        = (B + A) + (B + A) + .. {C Times}
    //         addition is associative and commutes, hence
    //        = B + B + .. {C Times} .. + A + A + .. {C times}
    //        = B*C + A*C
    //   (see (function add) for signed values and overflows)
    // [qed]
    //
    // Theorem: If C has c trailing zeros, errors bits in A or B are shifted out
    // to the left.
    //
    // Proof(2):
    //
    //   Let B' and A' be the n-Bit inputs with some unknown errors EA,
    //   EB at e leading bits. B' and A' can be written down as:
    //
    //     B' = B + 2^(n-e)*EB
    //     A' = A + 2^(n-e)*EA
    //
    //   Let C' be an input with c trailing zero bits. C' can be written as
    //
    //     C' = C*2^c
    //
    //   Therefore we can compute the result by using distributivity and
    //   commutativity.
    //
    //     (B'*C' + A'*C') = [B + 2^(n-e)*EB] * C' + [A + 2^(n-e)*EA] * C' =
    //                     = [B + 2^(n-e)*EB + A + 2^(n-e)*EA] * C' =
    //                     = (B'+A') * C' =
    //                     = [B + 2^(n-e)*EB + A + 2^(n-e)*EA] * C' =
    //                     = [B + A + 2^(n-e)*EB + 2^(n-e)*EA] * C' =
    //                     = (B + A) * C' + [2^(n-e)*EB + 2^(n-e)*EA)] * C' =
    //                     = (B + A) * C' + [2^(n-e)*EB + 2^(n-e)*EA)] * C*2^c =
    //                     = (B + A) * C' + C*(EB + EA)*2^(n-e)*2^c =
    //
    //   Let EC be the final error with EC = C*(EB + EA)
    //
    //                     = (B + A)*C' + EC*2^(n-e)*2^c =
    //                     = (B + A)*C' + EC*2^(n-(e-c))
    //
    //   Since EC is multiplied by 2^(n-(e-c)) the resulting error contains c
    //   less error bits than the input. c bits are shifted out to the left.
    // [qed]

    if (C.getBitWidth() != A.getBitWidth()) {
      ErrorMSBs = (unsigned)-1;
      return *this;
    }

    // Multiplying by one is a no-op.
    if (C.isOneValue()) {
      return *this;
    }

    // Multiplying by zero removes the coefficient B and defines all bits.
    if (C.isNullValue()) {
      ErrorMSBs = 0;
      deleteB();
    }

    // See Proof(2): Trailing zero bits indicate a left shift. This removes
    // leading bits from the result even if they are undefined.
    decErrorMSBs(C.countTrailingZeros());

    A *= C;
    pushBOperation(Mul, C);
    return *this;
  }

  /// Apply a logical shift right on the polynomial
  Polynomial &lshr(const APInt &C) {
    // Theorem(1): (B + A + E*2^(n-e)) >> 1 => (B >> 1) + (A >> 1) + E'*2^(n-e')
    //          where
    //             e' = e + 1,
    //             E is a e-bit number,
    //             E' is a e'-bit number,
    //   holds under the following precondition:
    //          pre(1): A % 2 = 0
    //          pre(2): e < n, (see Theorem(2) for the trivial case with e=n)
    //   where >> expresses a logical shift to the right, with adding zeros.
    //
    //  We need to show that for every, E there is a E'
    //
    //  B = b_h * 2^(n-1) + b_m * 2 + b_l
    //  A = a_h * 2^(n-1) + a_m * 2         (pre(1))
    //
    //  where a_h, b_h, b_l are single bits, and a_m, b_m are (n-2) bit numbers
    //
    //  Let X = (B + A + E*2^(n-e)) >> 1
    //  Let Y = (B >> 1) + (A >> 1) + E*2^(n-e) >> 1
    //
    //    X = [B + A + E*2^(n-e)] >> 1 =
    //      = [  b_h * 2^(n-1) + b_m * 2 + b_l +
    //         + a_h * 2^(n-1) + a_m * 2 +
    //         + E * 2^(n-e) ] >> 1 =
    //
    //    The sum is built by putting the overflow of [a_m + b+n] into the term
    //    2^(n-1). As there are no more bits beyond 2^(n-1) the overflow within
    //    this bit is discarded. This is expressed by % 2.
    //
    //    The bit in position 0 cannot overflow into the term (b_m + a_m).
    //
    //      = [  ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-1) +
    //         + ((b_m + a_m) % 2^(n-2)) * 2 +
    //         + b_l + E * 2^(n-e) ] >> 1 =
    //
    //    The shift is computed by dividing the terms by 2 and by cutting off
    //    b_l.
    //
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + E * 2^(n-(e+1)) =
    //
    //    by the definition in the Theorem e+1 = e'
    //
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + E * 2^(n-e') =
    //
    //    Compute Y by applying distributivity first
    //
    //    Y =  (B >> 1) + (A >> 1) + E*2^(n-e') =
    //      =    (b_h * 2^(n-1) + b_m * 2 + b_l) >> 1 +
    //         + (a_h * 2^(n-1) + a_m * 2) >> 1 +
    //         + E * 2^(n-e) >> 1 =
    //
    //    Again, the shift is computed by dividing the terms by 2 and by cutting
    //    off b_l.
    //
    //      =     b_h * 2^(n-2) + b_m +
    //         +  a_h * 2^(n-2) + a_m +
    //         +  E * 2^(n-(e+1)) =
    //
    //    Again, the sum is built by putting the overflow of [a_m + b+n] into
    //    the term 2^(n-1). But this time there is room for a second bit in the
    //    term 2^(n-2) we add this bit to a new term and denote it o_h in a
    //    second step.
    //
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] >> 1) * 2^(n-1) +
    //         + ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + E * 2^(n-(e+1)) =
    //
    //    Let o_h = [b_h + a_h + (b_m + a_m) >> (n-2)] >> 1
    //    Further replace e+1 by e'.
    //
    //      =    o_h * 2^(n-1) +
    //         + ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + E * 2^(n-e') =
    //
    //    Move o_h into the error term and construct E'. To ensure that there is
    //    no 2^x with negative x, this step requires pre(2) (e < n).
    //
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + o_h * 2^(e'-1) * 2^(n-e') +               | pre(2), move 2^(e'-1)
    //                                                     | out of the old exponent
    //         + E * 2^(n-e') =
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + [o_h * 2^(e'-1) + E] * 2^(n-e') +         | move 2^(e'-1) out of
    //                                                     | the old exponent
    //
    //    Let E' = o_h * 2^(e'-1) + E
    //
    //      =    ([b_h + a_h + (b_m + a_m) >> (n-2)] % 2) * 2^(n-2) +
    //         + ((b_m + a_m) % 2^(n-2)) +
    //         + E' * 2^(n-e')
    //
    //    Because X and Y are distinct only in there error terms and E' can be
    //    constructed as shown the theorem holds.
    // [qed]
    //
    // For completeness in case of the case e=n it is also required to show that
    // distributivity can be applied.
    //
    // In this case Theorem(1) transforms to (the pre-condition on A can also be
    // dropped)
    //
    // Theorem(2): (B + A + E) >> 1 => (B >> 1) + (A >> 1) + E'
    //          where
    //             A, B, E, E' are two's complement numbers with the same bit
    //             width
    //
    //   Let A + B + E = X
    //   Let (B >> 1) + (A >> 1) = Y
    //
    //   Therefore we need to show that for every X and Y there is an E' which
    //   makes the equation
    //
    //     X = Y + E'
    //
    //   hold. This is trivially the case for E' = X - Y.
    //
    // [qed]
    //
    // Remark: Distributing lshr with and arbitrary number n can be expressed as
    //   ((((B + A) lshr 1) lshr 1) ... ) {n times}.
    // This construction induces n additional error bits at the left.

    if (C.getBitWidth() != A.getBitWidth()) {
      ErrorMSBs = (unsigned)-1;
      return *this;
    }

    if (C.isNullValue())
      return *this;

    // Test if the result will be zero
    unsigned shiftAmt = C.getZExtValue();
    if (shiftAmt >= C.getBitWidth())
      return mul(APInt(C.getBitWidth(), 0));

    // The proof that shiftAmt LSBs are zero for at least one summand is only
    // possible for the constant number.
    //
    // If this can be proven add shiftAmt to the error counter
    // `ErrorMSBs`. Otherwise set all bits as undefined.
    if (A.countTrailingZeros() < shiftAmt)
      ErrorMSBs = A.getBitWidth();
    else
      incErrorMSBs(shiftAmt);

    // Apply the operation.
    pushBOperation(LShr, C);
    A = A.lshr(shiftAmt);

    return *this;
  }

  /// Apply a sign-extend or truncate operation on the polynomial.
  Polynomial &sextOrTrunc(unsigned n) {
    if (n < A.getBitWidth()) {
      // Truncate: Clearly undefined Bits on the MSB side are removed
      // if there are any.
      decErrorMSBs(A.getBitWidth() - n);
      A = A.trunc(n);
      pushBOperation(Trunc, APInt(sizeof(n) * 8, n));
    }
    if (n > A.getBitWidth()) {
      // Extend: Clearly extending first and adding later is different
      // to adding first and extending later in all extended bits.
      incErrorMSBs(n - A.getBitWidth());
      A = A.sext(n);
      pushBOperation(SExt, APInt(sizeof(n) * 8, n));
    }

    return *this;
  }

  /// Test if there is a coefficient B.
  bool isFirstOrder() const { return V != nullptr; }

  /// Test coefficient B of two Polynomials are equal.
  bool isCompatibleTo(const Polynomial &o) const {
    // The polynomial use different bit width.
    if (A.getBitWidth() != o.A.getBitWidth())
      return false;

    // If neither Polynomial has the Coefficient B.
    if (!isFirstOrder() && !o.isFirstOrder())
      return true;

    // The index variable is different.
    if (V != o.V)
      return false;

    // Check the operations.
    if (B.size() != o.B.size())
      return false;

    auto ob = o.B.begin();
    for (auto &b : B) {
      if (b != *ob)
        return false;
      ob++;
    }

    return true;
  }

  /// Subtract two polynomials, return an undefined polynomial if
  /// subtraction is not possible.
  Polynomial operator-(const Polynomial &o) const {
    // Return an undefined polynomial if incompatible.
    if (!isCompatibleTo(o))
      return Polynomial();

    // If the polynomials are compatible (meaning they have the same
    // coefficient on B), B is eliminated. Thus a polynomial solely
    // containing A is returned
    return Polynomial(A - o.A, std::max(ErrorMSBs, o.ErrorMSBs));
  }

  /// Subtract a constant from a polynomial,
  Polynomial operator-(uint64_t C) const {
    Polynomial Result(*this);
    Result.A -= C;
    return Result;
  }

  /// Add a constant to a polynomial,
  Polynomial operator+(uint64_t C) const {
    Polynomial Result(*this);
    Result.A += C;
    return Result;
  }

  /// Returns true if it can be proven that two Polynomials are equal.
  bool isProvenEqualTo(const Polynomial &o) {
    // Subtract both polynomials and test if it is fully defined and zero.
    Polynomial r = *this - o;
    return (r.ErrorMSBs == 0) && (!r.isFirstOrder()) && (r.A.isNullValue());
  }

  /// Print the polynomial into a stream.
  void print(raw_ostream &OS) const {
    OS << "[{#ErrBits:" << ErrorMSBs << "} ";

    if (V) {
      for (auto b : B)
        OS << "(";
      OS << "(" << *V << ") ";

      for (auto b : B) {
        switch (b.first) {
        case LShr:
          OS << "LShr ";
          break;
        case Mul:
          OS << "Mul ";
          break;
        case SExt:
          OS << "SExt ";
          break;
        case Trunc:
          OS << "Trunc ";
          break;
        }

        OS << b.second << ") ";
      }
    }

    OS << "+ " << A << "]";
  }

private:
  void deleteB() {
    V = nullptr;
    B.clear();
  }

  void pushBOperation(const BOps Op, const APInt &C) {
    if (isFirstOrder()) {
      B.push_back(std::make_pair(Op, C));
      return;
    }
  }
};

#ifndef NDEBUG
static raw_ostream &operator<<(raw_ostream &OS, const Polynomial &S) {
  S.print(OS);
  return OS;
}
#endif

/// VectorInfo stores abstract the following information for each vector
/// element:
///
/// 1) The the memory address loaded into the element as Polynomial
/// 2) a set of load instruction necessary to construct the vector,
/// 3) a set of all other instructions that are necessary to create the vector and
/// 4) a pointer value that can be used as relative base for all elements.
struct VectorInfo {
private:
  VectorInfo(const VectorInfo &c) : VTy(c.VTy) {
    llvm_unreachable(
        "Copying VectorInfo is neither implemented nor necessary,");
  }

public:
  /// Information of a Vector Element
  struct ElementInfo {
    /// Offset Polynomial.
    Polynomial Ofs;

    /// The Load Instruction used to Load the entry. LI is null if the pointer
    /// of the load instruction does not point on to the entry
    LoadInst *LI;

    ElementInfo(Polynomial Offset = Polynomial(), LoadInst *LI = nullptr)
        : Ofs(Offset), LI(LI) {}
  };

  /// Basic-block the load instructions are within
  BasicBlock *BB;

  /// Pointer value of all participation load instructions
  Value *PV;

  /// Participating load instructions
  std::set<LoadInst *> LIs;

  /// Participating instructions
  std::set<Instruction *> Is;

  /// Final shuffle-vector instruction
  ShuffleVectorInst *SVI;

  /// Information of the offset for each vector element
  ElementInfo *EI;

  /// Vector Type
  VectorType *const VTy;

  VectorInfo(VectorType *VTy)
      : BB(nullptr), PV(nullptr), LIs(), Is(), SVI(nullptr), VTy(VTy) {
    EI = new ElementInfo[VTy->getNumElements()];
  }

  virtual ~VectorInfo() { delete[] EI; }

  unsigned getDimension() const { return VTy->getNumElements(); }

  /// Test if the VectorInfo can be part of an interleaved load with the
  /// specified factor.
  ///
  /// \param Factor of the interleave
  /// \param DL Targets Datalayout
  ///
  /// \returns true if this is possible and false if not
  bool isInterleaved(unsigned Factor, const DataLayout &DL) const {
    unsigned Size = DL.getTypeAllocSize(VTy->getElementType());
    for (unsigned i = 1; i < getDimension(); i++) {
      if (!EI[i].Ofs.isProvenEqualTo(EI[0].Ofs + i * Factor * Size)) {
        return false;
      }
    }
    return true;
  }

  /// Recursively computes the vector information stored in V.
  ///
  /// This function delegates the work to specialized implementations
  ///
  /// \param V Value to operate on
  /// \param Result Result of the computation
  ///
  /// \returns false if no sensible information can be gathered.
  static bool compute(Value *V, VectorInfo &Result, const DataLayout &DL) {
    ShuffleVectorInst *SVI = dyn_cast<ShuffleVectorInst>(V);
    if (SVI)
      return computeFromSVI(SVI, Result, DL);
    LoadInst *LI = dyn_cast<LoadInst>(V);
    if (LI)
      return computeFromLI(LI, Result, DL);
    BitCastInst *BCI = dyn_cast<BitCastInst>(V);
    if (BCI)
      return computeFromBCI(BCI, Result, DL);
    return false;
  }

  /// BitCastInst specialization to compute the vector information.
  ///
  /// \param BCI BitCastInst to operate on
  /// \param Result Result of the computation
  ///
  /// \returns false if no sensible information can be gathered.
  static bool computeFromBCI(BitCastInst *BCI, VectorInfo &Result,
                             const DataLayout &DL) {
    Instruction *Op = dyn_cast<Instruction>(BCI->getOperand(0));

    if (!Op)
      return false;

    VectorType *VTy = dyn_cast<VectorType>(Op->getType());
    if (!VTy)
      return false;

    // We can only cast from large to smaller vectors
    if (Result.VTy->getNumElements() % VTy->getNumElements())
      return false;

    unsigned Factor = Result.VTy->getNumElements() / VTy->getNumElements();
    unsigned NewSize = DL.getTypeAllocSize(Result.VTy->getElementType());
    unsigned OldSize = DL.getTypeAllocSize(VTy->getElementType());

    if (NewSize * Factor != OldSize)
      return false;

    VectorInfo Old(VTy);
    if (!compute(Op, Old, DL))
      return false;

    for (unsigned i = 0; i < Result.VTy->getNumElements(); i += Factor) {
      for (unsigned j = 0; j < Factor; j++) {
        Result.EI[i + j] =
            ElementInfo(Old.EI[i / Factor].Ofs + j * NewSize,
                        j == 0 ? Old.EI[i / Factor].LI : nullptr);
      }
    }

    Result.BB = Old.BB;
    Result.PV = Old.PV;
    Result.LIs.insert(Old.LIs.begin(), Old.LIs.end());
    Result.Is.insert(Old.Is.begin(), Old.Is.end());
    Result.Is.insert(BCI);
    Result.SVI = nullptr;

    return true;
  }

  /// ShuffleVectorInst specialization to compute vector information.
  ///
  /// \param SVI ShuffleVectorInst to operate on
  /// \param Result Result of the computation
  ///
  /// Compute the left and the right side vector information and merge them by
  /// applying the shuffle operation. This function also ensures that the left
  /// and right side have compatible loads. This means that all loads are with
  /// in the same basic block and are based on the same pointer.
  ///
  /// \returns false if no sensible information can be gathered.
  static bool computeFromSVI(ShuffleVectorInst *SVI, VectorInfo &Result,
                             const DataLayout &DL) {
    VectorType *ArgTy = dyn_cast<VectorType>(SVI->getOperand(0)->getType());
    assert(ArgTy && "ShuffleVector Operand is not a VectorType");

    // Compute the left hand vector information.
    VectorInfo LHS(ArgTy);
    if (!compute(SVI->getOperand(0), LHS, DL))
      LHS.BB = nullptr;

    // Compute the right hand vector information.
    VectorInfo RHS(ArgTy);
    if (!compute(SVI->getOperand(1), RHS, DL))
      RHS.BB = nullptr;

    // Neither operand produced sensible results?
    if (!LHS.BB && !RHS.BB)
      return false;
    // Only RHS produced sensible results?
    else if (!LHS.BB) {
      Result.BB = RHS.BB;
      Result.PV = RHS.PV;
    }
    // Only LHS produced sensible results?
    else if (!RHS.BB) {
      Result.BB = LHS.BB;
      Result.PV = LHS.PV;
    }
    // Both operands produced sensible results?
    else if ((LHS.BB == RHS.BB) && (LHS.PV == RHS.PV)) {
      Result.BB = LHS.BB;
      Result.PV = LHS.PV;
    }
    // Both operands produced sensible results but they are incompatible.
    else {
      return false;
    }

    // Merge and apply the operation on the offset information.
    if (LHS.BB) {
      Result.LIs.insert(LHS.LIs.begin(), LHS.LIs.end());
      Result.Is.insert(LHS.Is.begin(), LHS.Is.end());
    }
    if (RHS.BB) {
      Result.LIs.insert(RHS.LIs.begin(), RHS.LIs.end());
      Result.Is.insert(RHS.Is.begin(), RHS.Is.end());
    }
    Result.Is.insert(SVI);
    Result.SVI = SVI;

    int j = 0;
    for (int i : SVI->getShuffleMask()) {
      assert((i < 2 * (signed)ArgTy->getNumElements()) &&
             "Invalid ShuffleVectorInst (index out of bounds)");

      if (i < 0)
        Result.EI[j] = ElementInfo();
      else if (i < (signed)ArgTy->getNumElements()) {
        if (LHS.BB)
          Result.EI[j] = LHS.EI[i];
        else
          Result.EI[j] = ElementInfo();
      } else {
        if (RHS.BB)
          Result.EI[j] = RHS.EI[i - ArgTy->getNumElements()];
        else
          Result.EI[j] = ElementInfo();
      }
      j++;
    }

    return true;
  }

  /// LoadInst specialization to compute vector information.
  ///
  /// This function also acts as abort condition to the recursion.
  ///
  /// \param LI LoadInst to operate on
  /// \param Result Result of the computation
  ///
  /// \returns false if no sensible information can be gathered.
  static bool computeFromLI(LoadInst *LI, VectorInfo &Result,
                            const DataLayout &DL) {
    Value *BasePtr;
    Polynomial Offset;

    if (LI->isVolatile())
      return false;

    if (LI->isAtomic())
      return false;

    // Get the base polynomial
    computePolynomialFromPointer(*LI->getPointerOperand(), Offset, BasePtr, DL);

    Result.BB = LI->getParent();
    Result.PV = BasePtr;
    Result.LIs.insert(LI);
    Result.Is.insert(LI);

    for (unsigned i = 0; i < Result.getDimension(); i++) {
      Value *Idx[2] = {
          ConstantInt::get(Type::getInt32Ty(LI->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(LI->getContext()), i),
      };
      int64_t Ofs = DL.getIndexedOffsetInType(Result.VTy, makeArrayRef(Idx, 2));
      Result.EI[i] = ElementInfo(Offset + Ofs, i == 0 ? LI : nullptr);
    }

    return true;
  }

  /// Recursively compute polynomial of a value.
  ///
  /// \param BO Input binary operation
  /// \param Result Result polynomial
  static void computePolynomialBinOp(BinaryOperator &BO, Polynomial &Result) {
    Value *LHS = BO.getOperand(0);
    Value *RHS = BO.getOperand(1);

    // Find the RHS Constant if any
    ConstantInt *C = dyn_cast<ConstantInt>(RHS);
    if ((!C) && BO.isCommutative()) {
      C = dyn_cast<ConstantInt>(LHS);
      if (C)
        std::swap(LHS, RHS);
    }

    switch (BO.getOpcode()) {
    case Instruction::Add:
      if (!C)
        break;

      computePolynomial(*LHS, Result);
      Result.add(C->getValue());
      return;

    case Instruction::LShr:
      if (!C)
        break;

      computePolynomial(*LHS, Result);
      Result.lshr(C->getValue());
      return;

    default:
      break;
    }

    Result = Polynomial(&BO);
  }

  /// Recursively compute polynomial of a value
  ///
  /// \param V input value
  /// \param Result result polynomial
  static void computePolynomial(Value &V, Polynomial &Result) {
    if (isa<BinaryOperator>(&V))
      computePolynomialBinOp(*dyn_cast<BinaryOperator>(&V), Result);
    else
      Result = Polynomial(&V);
  }

  /// Compute the Polynomial representation of a Pointer type.
  ///
  /// \param Ptr input pointer value
  /// \param Result result polynomial
  /// \param BasePtr pointer the polynomial is based on
  /// \param DL Datalayout of the target machine
  static void computePolynomialFromPointer(Value &Ptr, Polynomial &Result,
                                           Value *&BasePtr,
                                           const DataLayout &DL) {
    // Not a pointer type? Return an undefined polynomial
    PointerType *PtrTy = dyn_cast<PointerType>(Ptr.getType());
    if (!PtrTy) {
      Result = Polynomial();
      BasePtr = nullptr;
    }
    unsigned PointerBits =
        DL.getIndexSizeInBits(PtrTy->getPointerAddressSpace());

    /// Skip pointer casts. Return Zero polynomial otherwise
    if (isa<CastInst>(&Ptr)) {
      CastInst &CI = *cast<CastInst>(&Ptr);
      switch (CI.getOpcode()) {
      case Instruction::BitCast:
        computePolynomialFromPointer(*CI.getOperand(0), Result, BasePtr, DL);
        break;
      default:
        BasePtr = &Ptr;
        Polynomial(PointerBits, 0);
        break;
      }
    }
    /// Resolve GetElementPtrInst.
    else if (isa<GetElementPtrInst>(&Ptr)) {
      GetElementPtrInst &GEP = *cast<GetElementPtrInst>(&Ptr);

      APInt BaseOffset(PointerBits, 0);

      // Check if we can compute the Offset with accumulateConstantOffset
      if (GEP.accumulateConstantOffset(DL, BaseOffset)) {
        Result = Polynomial(BaseOffset);
        BasePtr = GEP.getPointerOperand();
        return;
      } else {
        // Otherwise we allow that the last index operand of the GEP is
        // non-constant.
        unsigned idxOperand, e;
        SmallVector<Value *, 4> Indices;
        for (idxOperand = 1, e = GEP.getNumOperands(); idxOperand < e;
             idxOperand++) {
          ConstantInt *IDX = dyn_cast<ConstantInt>(GEP.getOperand(idxOperand));
          if (!IDX)
            break;
          Indices.push_back(IDX);
        }

        // It must also be the last operand.
        if (idxOperand + 1 != e) {
          Result = Polynomial();
          BasePtr = nullptr;
          return;
        }

        // Compute the polynomial of the index operand.
        computePolynomial(*GEP.getOperand(idxOperand), Result);

        // Compute base offset from zero based index, excluding the last
        // variable operand.
        BaseOffset =
            DL.getIndexedOffsetInType(GEP.getSourceElementType(), Indices);

        // Apply the operations of GEP to the polynomial.
        unsigned ResultSize = DL.getTypeAllocSize(GEP.getResultElementType());
        Result.sextOrTrunc(PointerBits);
        Result.mul(APInt(PointerBits, ResultSize));
        Result.add(BaseOffset);
        BasePtr = GEP.getPointerOperand();
      }
    }
    // All other instructions are handled by using the value as base pointer and
    // a zero polynomial.
    else {
      BasePtr = &Ptr;
      Polynomial(DL.getIndexSizeInBits(PtrTy->getPointerAddressSpace()), 0);
    }
  }

#ifndef NDEBUG
  void print(raw_ostream &OS) const {
    if (PV)
      OS << *PV;
    else
      OS << "(none)";
    OS << " + ";
    for (unsigned i = 0; i < getDimension(); i++)
      OS << ((i == 0) ? "[" : ", ") << EI[i].Ofs;
    OS << "]";
  }
#endif
};

} // anonymous namespace

bool InterleavedLoadCombineImpl::findPattern(
    std::list<VectorInfo> &Candidates, std::list<VectorInfo> &InterleavedLoad,
    unsigned Factor, const DataLayout &DL) {
  for (auto C0 = Candidates.begin(), E0 = Candidates.end(); C0 != E0; ++C0) {
    unsigned i;
    // Try to find an interleaved load using the front of Worklist as first line
    unsigned Size = DL.getTypeAllocSize(C0->VTy->getElementType());

    // List containing iterators pointing to the VectorInfos of the candidates
    std::vector<std::list<VectorInfo>::iterator> Res(Factor, Candidates.end());

    for (auto C = Candidates.begin(), E = Candidates.end(); C != E; C++) {
      if (C->VTy != C0->VTy)
        continue;
      if (C->BB != C0->BB)
        continue;
      if (C->PV != C0->PV)
        continue;

      // Check the current value matches any of factor - 1 remaining lines
      for (i = 1; i < Factor; i++) {
        if (C->EI[0].Ofs.isProvenEqualTo(C0->EI[0].Ofs + i * Size)) {
          Res[i] = C;
        }
      }

      for (i = 1; i < Factor; i++) {
        if (Res[i] == Candidates.end())
          break;
      }
      if (i == Factor) {
        Res[0] = C0;
        break;
      }
    }

    if (Res[0] != Candidates.end()) {
      // Move the result into the output
      for (unsigned i = 0; i < Factor; i++) {
        InterleavedLoad.splice(InterleavedLoad.end(), Candidates, Res[i]);
      }

      return true;
    }
  }
  return false;
}

LoadInst *
InterleavedLoadCombineImpl::findFirstLoad(const std::set<LoadInst *> &LIs) {
  assert(!LIs.empty() && "No load instructions given.");

  // All LIs are within the same BB. Select the first for a reference.
  BasicBlock *BB = (*LIs.begin())->getParent();
  BasicBlock::iterator FLI =
      std::find_if(BB->begin(), BB->end(), [&LIs](Instruction &I) -> bool {
        return is_contained(LIs, &I);
      });
  assert(FLI != BB->end());

  return cast<LoadInst>(FLI);
}

bool InterleavedLoadCombineImpl::combine(std::list<VectorInfo> &InterleavedLoad,
                                         OptimizationRemarkEmitter &ORE) {
  LLVM_DEBUG(dbgs() << "Checking interleaved load\n");

  // The insertion point is the LoadInst which loads the first values. The
  // following tests are used to proof that the combined load can be inserted
  // just before InsertionPoint.
  LoadInst *InsertionPoint = InterleavedLoad.front().EI[0].LI;

  // Test if the offset is computed
  if (!InsertionPoint)
    return false;

  std::set<LoadInst *> LIs;
  std::set<Instruction *> Is;
  std::set<Instruction *> SVIs;

  unsigned InterleavedCost;
  unsigned InstructionCost = 0;

  // Get the interleave factor
  unsigned Factor = InterleavedLoad.size();

  // Merge all input sets used in analysis
  for (auto &VI : InterleavedLoad) {
    // Generate a set of all load instructions to be combined
    LIs.insert(VI.LIs.begin(), VI.LIs.end());

    // Generate a set of all instructions taking part in load
    // interleaved. This list excludes the instructions necessary for the
    // polynomial construction.
    Is.insert(VI.Is.begin(), VI.Is.end());

    // Generate the set of the final ShuffleVectorInst.
    SVIs.insert(VI.SVI);
  }

  // There is nothing to combine.
  if (LIs.size() < 2)
    return false;

  // Test if all participating instruction will be dead after the
  // transformation. If intermediate results are used, no performance gain can
  // be expected. Also sum the cost of the Instructions beeing left dead.
  for (auto &I : Is) {
    // Compute the old cost
    InstructionCost +=
        TTI.getInstructionCost(I, TargetTransformInfo::TCK_Latency);

    // The final SVIs are allowed not to be dead, all uses will be replaced
    if (SVIs.find(I) != SVIs.end())
      continue;

    // If there are users outside the set to be eliminated, we abort the
    // transformation. No gain can be expected.
    for (const auto &U : I->users()) {
      if (Is.find(dyn_cast<Instruction>(U)) == Is.end())
        return false;
    }
  }

  // We know that all LoadInst are within the same BB. This guarantees that
  // either everything or nothing is loaded.
  LoadInst *First = findFirstLoad(LIs);

  // To be safe that the loads can be combined, iterate over all loads and test
  // that the corresponding defining access dominates first LI. This guarantees
  // that there are no aliasing stores in between the loads.
  auto FMA = MSSA.getMemoryAccess(First);
  for (auto LI : LIs) {
    auto MADef = MSSA.getMemoryAccess(LI)->getDefiningAccess();
    if (!MSSA.dominates(MADef, FMA))
      return false;
  }
  assert(!LIs.empty() && "There are no LoadInst to combine");

  // It is necessary that insertion point dominates all final ShuffleVectorInst.
  for (auto &VI : InterleavedLoad) {
    if (!DT.dominates(InsertionPoint, VI.SVI))
      return false;
  }

  // All checks are done. Add instructions detectable by InterleavedAccessPass
  // The old instruction will are left dead.
  IRBuilder<> Builder(InsertionPoint);
  Type *ETy = InterleavedLoad.front().SVI->getType()->getElementType();
  unsigned ElementsPerSVI =
      InterleavedLoad.front().SVI->getType()->getNumElements();
  VectorType *ILTy = VectorType::get(ETy, Factor * ElementsPerSVI);

  SmallVector<unsigned, 4> Indices;
  for (unsigned i = 0; i < Factor; i++)
    Indices.push_back(i);
  InterleavedCost = TTI.getInterleavedMemoryOpCost(
      Instruction::Load, ILTy, Factor, Indices, InsertionPoint->getAlignment(),
      InsertionPoint->getPointerAddressSpace());

  if (InterleavedCost >= InstructionCost) {
    return false;
  }

  // Create a pointer cast for the wide load.
  auto CI = Builder.CreatePointerCast(InsertionPoint->getOperand(0),
                                      ILTy->getPointerTo(),
                                      "interleaved.wide.ptrcast");

  // Create the wide load and update the MemorySSA.
  auto LI = Builder.CreateAlignedLoad(CI, InsertionPoint->getAlignment(),
                                      "interleaved.wide.load");
  auto MSSAU = MemorySSAUpdater(&MSSA);
  MemoryUse *MSSALoad = cast<MemoryUse>(MSSAU.createMemoryAccessBefore(
      LI, nullptr, MSSA.getMemoryAccess(InsertionPoint)));
  MSSAU.insertUse(MSSALoad);

  // Create the final SVIs and replace all uses.
  int i = 0;
  for (auto &VI : InterleavedLoad) {
    SmallVector<uint32_t, 4> Mask;
    for (unsigned j = 0; j < ElementsPerSVI; j++)
      Mask.push_back(i + j * Factor);

    Builder.SetInsertPoint(VI.SVI);
    auto SVI = Builder.CreateShuffleVector(LI, UndefValue::get(LI->getType()),
                                           Mask, "interleaved.shuffle");
    VI.SVI->replaceAllUsesWith(SVI);
    i++;
  }

  NumInterleavedLoadCombine++;
  ORE.emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "Combined Interleaved Load", LI)
           << "Load interleaved combined with factor "
           << ore::NV("Factor", Factor);
  });

  return true;
}

bool InterleavedLoadCombineImpl::run() {
  OptimizationRemarkEmitter ORE(&F);
  bool changed = false;
  unsigned MaxFactor = TLI.getMaxSupportedInterleaveFactor();

  auto &DL = F.getParent()->getDataLayout();

  // Start with the highest factor to avoid combining and recombining.
  for (unsigned Factor = MaxFactor; Factor >= 2; Factor--) {
    std::list<VectorInfo> Candidates;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto SVI = dyn_cast<ShuffleVectorInst>(&I)) {

          Candidates.emplace_back(SVI->getType());

          if (!VectorInfo::computeFromSVI(SVI, Candidates.back(), DL)) {
            Candidates.pop_back();
            continue;
          }

          if (!Candidates.back().isInterleaved(Factor, DL)) {
            Candidates.pop_back();
          }
        }
      }
    }

    std::list<VectorInfo> InterleavedLoad;
    while (findPattern(Candidates, InterleavedLoad, Factor, DL)) {
      if (combine(InterleavedLoad, ORE)) {
        changed = true;
      } else {
        // Remove the first element of the Interleaved Load but put the others
        // back on the list and continue searching
        Candidates.splice(Candidates.begin(), InterleavedLoad,
                          std::next(InterleavedLoad.begin()),
                          InterleavedLoad.end());
      }
      InterleavedLoad.clear();
    }
  }

  return changed;
}

namespace {
/// This pass combines interleaved loads into a pattern detectable by
/// InterleavedAccessPass.
struct InterleavedLoadCombine : public FunctionPass {
  static char ID;

  InterleavedLoadCombine() : FunctionPass(ID) {
    initializeInterleavedLoadCombinePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Interleaved Load Combine Pass";
  }

  bool runOnFunction(Function &F) override {
    if (DisableInterleavedLoadCombine)
      return false;

    auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
    if (!TPC)
      return false;

    LLVM_DEBUG(dbgs() << "*** " << getPassName() << ": " << F.getName()
                      << "\n");

    return InterleavedLoadCombineImpl(
               F, getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
               getAnalysis<MemorySSAWrapperPass>().getMSSA(),
               TPC->getTM<TargetMachine>())
        .run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

private:
};
} // anonymous namespace

char InterleavedLoadCombine::ID = 0;

INITIALIZE_PASS_BEGIN(
    InterleavedLoadCombine, DEBUG_TYPE,
    "Combine interleaved loads into wide loads and shufflevector instructions",
    false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(
    InterleavedLoadCombine, DEBUG_TYPE,
    "Combine interleaved loads into wide loads and shufflevector instructions",
    false, false)

FunctionPass *
llvm::createInterleavedLoadCombinePass() {
  auto P = new InterleavedLoadCombine();
  return P;
}
