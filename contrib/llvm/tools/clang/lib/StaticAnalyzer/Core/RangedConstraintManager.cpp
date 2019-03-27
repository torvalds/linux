//== RangedConstraintManager.cpp --------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines RangedConstraintManager, a class that provides a
//  range-based constraint manager interface.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"

namespace clang {

namespace ento {

RangedConstraintManager::~RangedConstraintManager() {}

ProgramStateRef RangedConstraintManager::assumeSym(ProgramStateRef State,
                                                   SymbolRef Sym,
                                                   bool Assumption) {
  // Handle SymbolData.
  if (isa<SymbolData>(Sym)) {
    return assumeSymUnsupported(State, Sym, Assumption);

    // Handle symbolic expression.
  } else if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(Sym)) {
    // We can only simplify expressions whose RHS is an integer.

    BinaryOperator::Opcode op = SIE->getOpcode();
    if (BinaryOperator::isComparisonOp(op) && op != BO_Cmp) {
      if (!Assumption)
        op = BinaryOperator::negateComparisonOp(op);

      return assumeSymRel(State, SIE->getLHS(), op, SIE->getRHS());
    }

  } else if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(Sym)) {
    // Translate "a != b" to "(b - a) != 0".
    // We invert the order of the operands as a heuristic for how loop
    // conditions are usually written ("begin != end") as compared to length
    // calculations ("end - begin"). The more correct thing to do would be to
    // canonicalize "a - b" and "b - a", which would allow us to treat
    // "a != b" and "b != a" the same.
    SymbolManager &SymMgr = getSymbolManager();
    BinaryOperator::Opcode Op = SSE->getOpcode();
    assert(BinaryOperator::isComparisonOp(Op));

    // For now, we only support comparing pointers.
    if (Loc::isLocType(SSE->getLHS()->getType()) &&
        Loc::isLocType(SSE->getRHS()->getType())) {
      QualType DiffTy = SymMgr.getContext().getPointerDiffType();
      SymbolRef Subtraction =
          SymMgr.getSymSymExpr(SSE->getRHS(), BO_Sub, SSE->getLHS(), DiffTy);

      const llvm::APSInt &Zero = getBasicVals().getValue(0, DiffTy);
      Op = BinaryOperator::reverseComparisonOp(Op);
      if (!Assumption)
        Op = BinaryOperator::negateComparisonOp(Op);
      return assumeSymRel(State, Subtraction, Op, Zero);
    }
  }

  // If we get here, there's nothing else we can do but treat the symbol as
  // opaque.
  return assumeSymUnsupported(State, Sym, Assumption);
}

ProgramStateRef RangedConstraintManager::assumeSymInclusiveRange(
    ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
    const llvm::APSInt &To, bool InRange) {
  // Get the type used for calculating wraparound.
  BasicValueFactory &BVF = getBasicVals();
  APSIntType WraparoundType = BVF.getAPSIntType(Sym->getType());

  llvm::APSInt Adjustment = WraparoundType.getZeroValue();
  SymbolRef AdjustedSym = Sym;
  computeAdjustment(AdjustedSym, Adjustment);

  // Convert the right-hand side integer as necessary.
  APSIntType ComparisonType = std::max(WraparoundType, APSIntType(From));
  llvm::APSInt ConvertedFrom = ComparisonType.convert(From);
  llvm::APSInt ConvertedTo = ComparisonType.convert(To);

  // Prefer unsigned comparisons.
  if (ComparisonType.getBitWidth() == WraparoundType.getBitWidth() &&
      ComparisonType.isUnsigned() && !WraparoundType.isUnsigned())
    Adjustment.setIsSigned(false);

  if (InRange)
    return assumeSymWithinInclusiveRange(State, AdjustedSym, ConvertedFrom,
                                         ConvertedTo, Adjustment);
  return assumeSymOutsideInclusiveRange(State, AdjustedSym, ConvertedFrom,
                                        ConvertedTo, Adjustment);
}

ProgramStateRef
RangedConstraintManager::assumeSymUnsupported(ProgramStateRef State,
                                              SymbolRef Sym, bool Assumption) {
  BasicValueFactory &BVF = getBasicVals();
  QualType T = Sym->getType();

  // Non-integer types are not supported.
  if (!T->isIntegralOrEnumerationType())
    return State;

  // Reverse the operation and add directly to state.
  const llvm::APSInt &Zero = BVF.getValue(0, T);
  if (Assumption)
    return assumeSymNE(State, Sym, Zero, Zero);
  else
    return assumeSymEQ(State, Sym, Zero, Zero);
}

ProgramStateRef RangedConstraintManager::assumeSymRel(ProgramStateRef State,
                                                      SymbolRef Sym,
                                                      BinaryOperator::Opcode Op,
                                                      const llvm::APSInt &Int) {
  assert(BinaryOperator::isComparisonOp(Op) &&
         "Non-comparison ops should be rewritten as comparisons to zero.");

  // Simplification: translate an assume of a constraint of the form
  // "(exp comparison_op expr) != 0" to true into an assume of
  // "exp comparison_op expr" to true. (And similarly, an assume of the form
  // "(exp comparison_op expr) == 0" to true into an assume of
  // "exp comparison_op expr" to false.)
  if (Int == 0 && (Op == BO_EQ || Op == BO_NE)) {
    if (const BinarySymExpr *SE = dyn_cast<BinarySymExpr>(Sym))
      if (BinaryOperator::isComparisonOp(SE->getOpcode()))
        return assumeSym(State, Sym, (Op == BO_NE ? true : false));
  }

  // Get the type used for calculating wraparound.
  BasicValueFactory &BVF = getBasicVals();
  APSIntType WraparoundType = BVF.getAPSIntType(Sym->getType());

  // We only handle simple comparisons of the form "$sym == constant"
  // or "($sym+constant1) == constant2".
  // The adjustment is "constant1" in the above expression. It's used to
  // "slide" the solution range around for modular arithmetic. For example,
  // x < 4 has the solution [0, 3]. x+2 < 4 has the solution [0-2, 3-2], which
  // in modular arithmetic is [0, 1] U [UINT_MAX-1, UINT_MAX]. It's up to
  // the subclasses of SimpleConstraintManager to handle the adjustment.
  llvm::APSInt Adjustment = WraparoundType.getZeroValue();
  computeAdjustment(Sym, Adjustment);

  // Convert the right-hand side integer as necessary.
  APSIntType ComparisonType = std::max(WraparoundType, APSIntType(Int));
  llvm::APSInt ConvertedInt = ComparisonType.convert(Int);

  // Prefer unsigned comparisons.
  if (ComparisonType.getBitWidth() == WraparoundType.getBitWidth() &&
      ComparisonType.isUnsigned() && !WraparoundType.isUnsigned())
    Adjustment.setIsSigned(false);

  switch (Op) {
  default:
    llvm_unreachable("invalid operation not caught by assertion above");

  case BO_EQ:
    return assumeSymEQ(State, Sym, ConvertedInt, Adjustment);

  case BO_NE:
    return assumeSymNE(State, Sym, ConvertedInt, Adjustment);

  case BO_GT:
    return assumeSymGT(State, Sym, ConvertedInt, Adjustment);

  case BO_GE:
    return assumeSymGE(State, Sym, ConvertedInt, Adjustment);

  case BO_LT:
    return assumeSymLT(State, Sym, ConvertedInt, Adjustment);

  case BO_LE:
    return assumeSymLE(State, Sym, ConvertedInt, Adjustment);
  } // end switch
}

void RangedConstraintManager::computeAdjustment(SymbolRef &Sym,
                                                llvm::APSInt &Adjustment) {
  // Is it a "($sym+constant1)" expression?
  if (const SymIntExpr *SE = dyn_cast<SymIntExpr>(Sym)) {
    BinaryOperator::Opcode Op = SE->getOpcode();
    if (Op == BO_Add || Op == BO_Sub) {
      Sym = SE->getLHS();
      Adjustment = APSIntType(Adjustment).convert(SE->getRHS());

      // Don't forget to negate the adjustment if it's being subtracted.
      // This should happen /after/ promotion, in case the value being
      // subtracted is, say, CHAR_MIN, and the promoted type is 'int'.
      if (Op == BO_Sub)
        Adjustment = -Adjustment;
    }
  }
}

void *ProgramStateTrait<ConstraintRange>::GDMIndex() {
  static int Index;
  return &Index;
}

} // end of namespace ento

} // end of namespace clang
