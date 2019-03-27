//== SMTConv.h --------------------------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a set of functions to create SMT expressions
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONV_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONV_H

#include "clang/AST/Expr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SMTSolver.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

namespace clang {
namespace ento {

class SMTConv {
public:
  // Returns an appropriate sort, given a QualType and it's bit width.
  static inline SMTSortRef mkSort(SMTSolverRef &Solver, const QualType &Ty,
                                  unsigned BitWidth) {
    if (Ty->isBooleanType())
      return Solver->getBoolSort();

    if (Ty->isRealFloatingType())
      return Solver->getFloatSort(BitWidth);

    return Solver->getBitvectorSort(BitWidth);
  }

  /// Constructs an SMTExprRef from an unary operator.
  static inline SMTExprRef fromUnOp(SMTSolverRef &Solver,
                                    const UnaryOperator::Opcode Op,
                                    const SMTExprRef &Exp) {
    switch (Op) {
    case UO_Minus:
      return Solver->mkBVNeg(Exp);

    case UO_Not:
      return Solver->mkBVNot(Exp);

    case UO_LNot:
      return Solver->mkNot(Exp);

    default:;
    }
    llvm_unreachable("Unimplemented opcode");
  }

  /// Constructs an SMTExprRef from a floating-point unary operator.
  static inline SMTExprRef fromFloatUnOp(SMTSolverRef &Solver,
                                         const UnaryOperator::Opcode Op,
                                         const SMTExprRef &Exp) {
    switch (Op) {
    case UO_Minus:
      return Solver->mkFPNeg(Exp);

    case UO_LNot:
      return fromUnOp(Solver, Op, Exp);

    default:;
    }
    llvm_unreachable("Unimplemented opcode");
  }

  /// Construct an SMTExprRef from a n-ary binary operator.
  static inline SMTExprRef fromNBinOp(SMTSolverRef &Solver,
                                      const BinaryOperator::Opcode Op,
                                      const std::vector<SMTExprRef> &ASTs) {
    assert(!ASTs.empty());

    if (Op != BO_LAnd && Op != BO_LOr)
      llvm_unreachable("Unimplemented opcode");

    SMTExprRef res = ASTs.front();
    for (std::size_t i = 1; i < ASTs.size(); ++i)
      res = (Op == BO_LAnd) ? Solver->mkAnd(res, ASTs[i])
                            : Solver->mkOr(res, ASTs[i]);
    return res;
  }

  /// Construct an SMTExprRef from a binary operator.
  static inline SMTExprRef fromBinOp(SMTSolverRef &Solver,
                                     const SMTExprRef &LHS,
                                     const BinaryOperator::Opcode Op,
                                     const SMTExprRef &RHS, bool isSigned) {
    assert(*Solver->getSort(LHS) == *Solver->getSort(RHS) &&
           "AST's must have the same sort!");

    switch (Op) {
    // Multiplicative operators
    case BO_Mul:
      return Solver->mkBVMul(LHS, RHS);

    case BO_Div:
      return isSigned ? Solver->mkBVSDiv(LHS, RHS) : Solver->mkBVUDiv(LHS, RHS);

    case BO_Rem:
      return isSigned ? Solver->mkBVSRem(LHS, RHS) : Solver->mkBVURem(LHS, RHS);

      // Additive operators
    case BO_Add:
      return Solver->mkBVAdd(LHS, RHS);

    case BO_Sub:
      return Solver->mkBVSub(LHS, RHS);

      // Bitwise shift operators
    case BO_Shl:
      return Solver->mkBVShl(LHS, RHS);

    case BO_Shr:
      return isSigned ? Solver->mkBVAshr(LHS, RHS) : Solver->mkBVLshr(LHS, RHS);

      // Relational operators
    case BO_LT:
      return isSigned ? Solver->mkBVSlt(LHS, RHS) : Solver->mkBVUlt(LHS, RHS);

    case BO_GT:
      return isSigned ? Solver->mkBVSgt(LHS, RHS) : Solver->mkBVUgt(LHS, RHS);

    case BO_LE:
      return isSigned ? Solver->mkBVSle(LHS, RHS) : Solver->mkBVUle(LHS, RHS);

    case BO_GE:
      return isSigned ? Solver->mkBVSge(LHS, RHS) : Solver->mkBVUge(LHS, RHS);

      // Equality operators
    case BO_EQ:
      return Solver->mkEqual(LHS, RHS);

    case BO_NE:
      return fromUnOp(Solver, UO_LNot,
                      fromBinOp(Solver, LHS, BO_EQ, RHS, isSigned));

      // Bitwise operators
    case BO_And:
      return Solver->mkBVAnd(LHS, RHS);

    case BO_Xor:
      return Solver->mkBVXor(LHS, RHS);

    case BO_Or:
      return Solver->mkBVOr(LHS, RHS);

      // Logical operators
    case BO_LAnd:
      return Solver->mkAnd(LHS, RHS);

    case BO_LOr:
      return Solver->mkOr(LHS, RHS);

    default:;
    }
    llvm_unreachable("Unimplemented opcode");
  }

  /// Construct an SMTExprRef from a special floating-point binary operator.
  static inline SMTExprRef
  fromFloatSpecialBinOp(SMTSolverRef &Solver, const SMTExprRef &LHS,
                        const BinaryOperator::Opcode Op,
                        const llvm::APFloat::fltCategory &RHS) {
    switch (Op) {
    // Equality operators
    case BO_EQ:
      switch (RHS) {
      case llvm::APFloat::fcInfinity:
        return Solver->mkFPIsInfinite(LHS);

      case llvm::APFloat::fcNaN:
        return Solver->mkFPIsNaN(LHS);

      case llvm::APFloat::fcNormal:
        return Solver->mkFPIsNormal(LHS);

      case llvm::APFloat::fcZero:
        return Solver->mkFPIsZero(LHS);
      }
      break;

    case BO_NE:
      return fromFloatUnOp(Solver, UO_LNot,
                           fromFloatSpecialBinOp(Solver, LHS, BO_EQ, RHS));

    default:;
    }

    llvm_unreachable("Unimplemented opcode");
  }

  /// Construct an SMTExprRef from a floating-point binary operator.
  static inline SMTExprRef fromFloatBinOp(SMTSolverRef &Solver,
                                          const SMTExprRef &LHS,
                                          const BinaryOperator::Opcode Op,
                                          const SMTExprRef &RHS) {
    assert(*Solver->getSort(LHS) == *Solver->getSort(RHS) &&
           "AST's must have the same sort!");

    switch (Op) {
    // Multiplicative operators
    case BO_Mul:
      return Solver->mkFPMul(LHS, RHS);

    case BO_Div:
      return Solver->mkFPDiv(LHS, RHS);

    case BO_Rem:
      return Solver->mkFPRem(LHS, RHS);

      // Additive operators
    case BO_Add:
      return Solver->mkFPAdd(LHS, RHS);

    case BO_Sub:
      return Solver->mkFPSub(LHS, RHS);

      // Relational operators
    case BO_LT:
      return Solver->mkFPLt(LHS, RHS);

    case BO_GT:
      return Solver->mkFPGt(LHS, RHS);

    case BO_LE:
      return Solver->mkFPLe(LHS, RHS);

    case BO_GE:
      return Solver->mkFPGe(LHS, RHS);

      // Equality operators
    case BO_EQ:
      return Solver->mkFPEqual(LHS, RHS);

    case BO_NE:
      return fromFloatUnOp(Solver, UO_LNot,
                           fromFloatBinOp(Solver, LHS, BO_EQ, RHS));

      // Logical operators
    case BO_LAnd:
    case BO_LOr:
      return fromBinOp(Solver, LHS, Op, RHS, /*isSigned=*/false);

    default:;
    }

    llvm_unreachable("Unimplemented opcode");
  }

  /// Construct an SMTExprRef from a QualType FromTy to a QualType ToTy, and
  /// their bit widths.
  static inline SMTExprRef fromCast(SMTSolverRef &Solver, const SMTExprRef &Exp,
                                    QualType ToTy, uint64_t ToBitWidth,
                                    QualType FromTy, uint64_t FromBitWidth) {
    if ((FromTy->isIntegralOrEnumerationType() &&
         ToTy->isIntegralOrEnumerationType()) ||
        (FromTy->isAnyPointerType() ^ ToTy->isAnyPointerType()) ||
        (FromTy->isBlockPointerType() ^ ToTy->isBlockPointerType()) ||
        (FromTy->isReferenceType() ^ ToTy->isReferenceType())) {

      if (FromTy->isBooleanType()) {
        assert(ToBitWidth > 0 && "BitWidth must be positive!");
        return Solver->mkIte(
            Exp, Solver->mkBitvector(llvm::APSInt("1"), ToBitWidth),
            Solver->mkBitvector(llvm::APSInt("0"), ToBitWidth));
      }

      if (ToBitWidth > FromBitWidth)
        return FromTy->isSignedIntegerOrEnumerationType()
                   ? Solver->mkBVSignExt(ToBitWidth - FromBitWidth, Exp)
                   : Solver->mkBVZeroExt(ToBitWidth - FromBitWidth, Exp);

      if (ToBitWidth < FromBitWidth)
        return Solver->mkBVExtract(ToBitWidth - 1, 0, Exp);

      // Both are bitvectors with the same width, ignore the type cast
      return Exp;
    }

    if (FromTy->isRealFloatingType() && ToTy->isRealFloatingType()) {
      if (ToBitWidth != FromBitWidth)
        return Solver->mkFPtoFP(Exp, Solver->getFloatSort(ToBitWidth));

      return Exp;
    }

    if (FromTy->isIntegralOrEnumerationType() && ToTy->isRealFloatingType()) {
      SMTSortRef Sort = Solver->getFloatSort(ToBitWidth);
      return FromTy->isSignedIntegerOrEnumerationType()
                 ? Solver->mkSBVtoFP(Exp, Sort)
                 : Solver->mkUBVtoFP(Exp, Sort);
    }

    if (FromTy->isRealFloatingType() && ToTy->isIntegralOrEnumerationType())
      return ToTy->isSignedIntegerOrEnumerationType()
                 ? Solver->mkFPtoSBV(Exp, ToBitWidth)
                 : Solver->mkFPtoUBV(Exp, ToBitWidth);

    llvm_unreachable("Unsupported explicit type cast!");
  }

  // Callback function for doCast parameter on APSInt type.
  static inline llvm::APSInt castAPSInt(SMTSolverRef &Solver,
                                        const llvm::APSInt &V, QualType ToTy,
                                        uint64_t ToWidth, QualType FromTy,
                                        uint64_t FromWidth) {
    APSIntType TargetType(ToWidth, !ToTy->isSignedIntegerOrEnumerationType());
    return TargetType.convert(V);
  }

  /// Construct an SMTExprRef from a SymbolData.
  static inline SMTExprRef fromData(SMTSolverRef &Solver, const SymbolID ID,
                                    const QualType &Ty, uint64_t BitWidth) {
    llvm::Twine Name = "$" + llvm::Twine(ID);
    return Solver->mkSymbol(Name.str().c_str(), mkSort(Solver, Ty, BitWidth));
  }

  // Wrapper to generate SMTExprRef from SymbolCast data.
  static inline SMTExprRef getCastExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                       const SMTExprRef &Exp, QualType FromTy,
                                       QualType ToTy) {
    return fromCast(Solver, Exp, ToTy, Ctx.getTypeSize(ToTy), FromTy,
                    Ctx.getTypeSize(FromTy));
  }

  // Wrapper to generate SMTExprRef from unpacked binary symbolic expression.
  // Sets the RetTy parameter. See getSMTExprRef().
  static inline SMTExprRef getBinExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                      const SMTExprRef &LHS, QualType LTy,
                                      BinaryOperator::Opcode Op,
                                      const SMTExprRef &RHS, QualType RTy,
                                      QualType *RetTy) {
    SMTExprRef NewLHS = LHS;
    SMTExprRef NewRHS = RHS;
    doTypeConversion(Solver, Ctx, NewLHS, NewRHS, LTy, RTy);

    // Update the return type parameter if the output type has changed.
    if (RetTy) {
      // A boolean result can be represented as an integer type in C/C++, but at
      // this point we only care about the SMT sorts. Set it as a boolean type
      // to avoid subsequent SMT errors.
      if (BinaryOperator::isComparisonOp(Op) ||
          BinaryOperator::isLogicalOp(Op)) {
        *RetTy = Ctx.BoolTy;
      } else {
        *RetTy = LTy;
      }

      // If the two operands are pointers and the operation is a subtraction,
      // the result is of type ptrdiff_t, which is signed
      if (LTy->isAnyPointerType() && RTy->isAnyPointerType() && Op == BO_Sub) {
        *RetTy = Ctx.getPointerDiffType();
      }
    }

    return LTy->isRealFloatingType()
               ? fromFloatBinOp(Solver, NewLHS, Op, NewRHS)
               : fromBinOp(Solver, NewLHS, Op, NewRHS,
                           LTy->isSignedIntegerOrEnumerationType());
  }

  // Wrapper to generate SMTExprRef from BinarySymExpr.
  // Sets the hasComparison and RetTy parameters. See getSMTExprRef().
  static inline SMTExprRef getSymBinExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                         const BinarySymExpr *BSE,
                                         bool *hasComparison, QualType *RetTy) {
    QualType LTy, RTy;
    BinaryOperator::Opcode Op = BSE->getOpcode();

    if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(BSE)) {
      SMTExprRef LHS =
          getSymExpr(Solver, Ctx, SIE->getLHS(), &LTy, hasComparison);
      llvm::APSInt NewRInt;
      std::tie(NewRInt, RTy) = fixAPSInt(Ctx, SIE->getRHS());
      SMTExprRef RHS = Solver->mkBitvector(NewRInt, NewRInt.getBitWidth());
      return getBinExpr(Solver, Ctx, LHS, LTy, Op, RHS, RTy, RetTy);
    }

    if (const IntSymExpr *ISE = dyn_cast<IntSymExpr>(BSE)) {
      llvm::APSInt NewLInt;
      std::tie(NewLInt, LTy) = fixAPSInt(Ctx, ISE->getLHS());
      SMTExprRef LHS = Solver->mkBitvector(NewLInt, NewLInt.getBitWidth());
      SMTExprRef RHS =
          getSymExpr(Solver, Ctx, ISE->getRHS(), &RTy, hasComparison);
      return getBinExpr(Solver, Ctx, LHS, LTy, Op, RHS, RTy, RetTy);
    }

    if (const SymSymExpr *SSM = dyn_cast<SymSymExpr>(BSE)) {
      SMTExprRef LHS =
          getSymExpr(Solver, Ctx, SSM->getLHS(), &LTy, hasComparison);
      SMTExprRef RHS =
          getSymExpr(Solver, Ctx, SSM->getRHS(), &RTy, hasComparison);
      return getBinExpr(Solver, Ctx, LHS, LTy, Op, RHS, RTy, RetTy);
    }

    llvm_unreachable("Unsupported BinarySymExpr type!");
  }

  // Recursive implementation to unpack and generate symbolic expression.
  // Sets the hasComparison and RetTy parameters. See getExpr().
  static inline SMTExprRef getSymExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                      SymbolRef Sym, QualType *RetTy,
                                      bool *hasComparison) {
    if (const SymbolData *SD = dyn_cast<SymbolData>(Sym)) {
      if (RetTy)
        *RetTy = Sym->getType();

      return fromData(Solver, SD->getSymbolID(), Sym->getType(),
                      Ctx.getTypeSize(Sym->getType()));
    }

    if (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym)) {
      if (RetTy)
        *RetTy = Sym->getType();

      QualType FromTy;
      SMTExprRef Exp =
          getSymExpr(Solver, Ctx, SC->getOperand(), &FromTy, hasComparison);

      // Casting an expression with a comparison invalidates it. Note that this
      // must occur after the recursive call above.
      // e.g. (signed char) (x > 0)
      if (hasComparison)
        *hasComparison = false;
      return getCastExpr(Solver, Ctx, Exp, FromTy, Sym->getType());
    }

    if (const BinarySymExpr *BSE = dyn_cast<BinarySymExpr>(Sym)) {
      SMTExprRef Exp = getSymBinExpr(Solver, Ctx, BSE, hasComparison, RetTy);
      // Set the hasComparison parameter, in post-order traversal order.
      if (hasComparison)
        *hasComparison = BinaryOperator::isComparisonOp(BSE->getOpcode());
      return Exp;
    }

    llvm_unreachable("Unsupported SymbolRef type!");
  }

  // Generate an SMTExprRef that represents the given symbolic expression.
  // Sets the hasComparison parameter if the expression has a comparison
  // operator. Sets the RetTy parameter to the final return type after
  // promotions and casts.
  static inline SMTExprRef getExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                   SymbolRef Sym, QualType *RetTy = nullptr,
                                   bool *hasComparison = nullptr) {
    if (hasComparison) {
      *hasComparison = false;
    }

    return getSymExpr(Solver, Ctx, Sym, RetTy, hasComparison);
  }

  // Generate an SMTExprRef that compares the expression to zero.
  static inline SMTExprRef getZeroExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                       const SMTExprRef &Exp, QualType Ty,
                                       bool Assumption) {

    if (Ty->isRealFloatingType()) {
      llvm::APFloat Zero =
          llvm::APFloat::getZero(Ctx.getFloatTypeSemantics(Ty));
      return fromFloatBinOp(Solver, Exp, Assumption ? BO_EQ : BO_NE,
                            Solver->mkFloat(Zero));
    }

    if (Ty->isIntegralOrEnumerationType() || Ty->isAnyPointerType() ||
        Ty->isBlockPointerType() || Ty->isReferenceType()) {

      // Skip explicit comparison for boolean types
      bool isSigned = Ty->isSignedIntegerOrEnumerationType();
      if (Ty->isBooleanType())
        return Assumption ? fromUnOp(Solver, UO_LNot, Exp) : Exp;

      return fromBinOp(
          Solver, Exp, Assumption ? BO_EQ : BO_NE,
          Solver->mkBitvector(llvm::APSInt("0"), Ctx.getTypeSize(Ty)),
          isSigned);
    }

    llvm_unreachable("Unsupported type for zero value!");
  }

  // Wrapper to generate SMTExprRef from a range. If From == To, an equality
  // will be created instead.
  static inline SMTExprRef getRangeExpr(SMTSolverRef &Solver, ASTContext &Ctx,
                                        SymbolRef Sym, const llvm::APSInt &From,
                                        const llvm::APSInt &To, bool InRange) {
    // Convert lower bound
    QualType FromTy;
    llvm::APSInt NewFromInt;
    std::tie(NewFromInt, FromTy) = fixAPSInt(Ctx, From);
    SMTExprRef FromExp =
        Solver->mkBitvector(NewFromInt, NewFromInt.getBitWidth());

    // Convert symbol
    QualType SymTy;
    SMTExprRef Exp = getExpr(Solver, Ctx, Sym, &SymTy);

    // Construct single (in)equality
    if (From == To)
      return getBinExpr(Solver, Ctx, Exp, SymTy, InRange ? BO_EQ : BO_NE,
                        FromExp, FromTy, /*RetTy=*/nullptr);

    QualType ToTy;
    llvm::APSInt NewToInt;
    std::tie(NewToInt, ToTy) = fixAPSInt(Ctx, To);
    SMTExprRef ToExp = Solver->mkBitvector(NewToInt, NewToInt.getBitWidth());
    assert(FromTy == ToTy && "Range values have different types!");

    // Construct two (in)equalities, and a logical and/or
    SMTExprRef LHS =
        getBinExpr(Solver, Ctx, Exp, SymTy, InRange ? BO_GE : BO_LT, FromExp,
                   FromTy, /*RetTy=*/nullptr);
    SMTExprRef RHS = getBinExpr(Solver, Ctx, Exp, SymTy,
                                InRange ? BO_LE : BO_GT, ToExp, ToTy,
                                /*RetTy=*/nullptr);

    return fromBinOp(Solver, LHS, InRange ? BO_LAnd : BO_LOr, RHS,
                     SymTy->isSignedIntegerOrEnumerationType());
  }

  // Recover the QualType of an APSInt.
  // TODO: Refactor to put elsewhere
  static inline QualType getAPSIntType(ASTContext &Ctx,
                                       const llvm::APSInt &Int) {
    return Ctx.getIntTypeForBitwidth(Int.getBitWidth(), Int.isSigned());
  }

  // Get the QualTy for the input APSInt, and fix it if it has a bitwidth of 1.
  static inline std::pair<llvm::APSInt, QualType>
  fixAPSInt(ASTContext &Ctx, const llvm::APSInt &Int) {
    llvm::APSInt NewInt;

    // FIXME: This should be a cast from a 1-bit integer type to a boolean type,
    // but the former is not available in Clang. Instead, extend the APSInt
    // directly.
    if (Int.getBitWidth() == 1 && getAPSIntType(Ctx, Int).isNull()) {
      NewInt = Int.extend(Ctx.getTypeSize(Ctx.BoolTy));
    } else
      NewInt = Int;

    return std::make_pair(NewInt, getAPSIntType(Ctx, NewInt));
  }

  // Perform implicit type conversion on binary symbolic expressions.
  // May modify all input parameters.
  // TODO: Refactor to use built-in conversion functions
  static inline void doTypeConversion(SMTSolverRef &Solver, ASTContext &Ctx,
                                      SMTExprRef &LHS, SMTExprRef &RHS,
                                      QualType &LTy, QualType &RTy) {
    assert(!LTy.isNull() && !RTy.isNull() && "Input type is null!");

    // Perform type conversion
    if ((LTy->isIntegralOrEnumerationType() &&
         RTy->isIntegralOrEnumerationType()) &&
        (LTy->isArithmeticType() && RTy->isArithmeticType())) {
      SMTConv::doIntTypeConversion<SMTExprRef, &fromCast>(Solver, Ctx, LHS, LTy,
                                                          RHS, RTy);
      return;
    }

    if (LTy->isRealFloatingType() || RTy->isRealFloatingType()) {
      SMTConv::doFloatTypeConversion<SMTExprRef, &fromCast>(Solver, Ctx, LHS,
                                                            LTy, RHS, RTy);
      return;
    }

    if ((LTy->isAnyPointerType() || RTy->isAnyPointerType()) ||
        (LTy->isBlockPointerType() || RTy->isBlockPointerType()) ||
        (LTy->isReferenceType() || RTy->isReferenceType())) {
      // TODO: Refactor to Sema::FindCompositePointerType(), and
      // Sema::CheckCompareOperands().

      uint64_t LBitWidth = Ctx.getTypeSize(LTy);
      uint64_t RBitWidth = Ctx.getTypeSize(RTy);

      // Cast the non-pointer type to the pointer type.
      // TODO: Be more strict about this.
      if ((LTy->isAnyPointerType() ^ RTy->isAnyPointerType()) ||
          (LTy->isBlockPointerType() ^ RTy->isBlockPointerType()) ||
          (LTy->isReferenceType() ^ RTy->isReferenceType())) {
        if (LTy->isNullPtrType() || LTy->isBlockPointerType() ||
            LTy->isReferenceType()) {
          LHS = fromCast(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
          LTy = RTy;
        } else {
          RHS = fromCast(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
          RTy = LTy;
        }
      }

      // Cast the void pointer type to the non-void pointer type.
      // For void types, this assumes that the casted value is equal to the
      // value of the original pointer, and does not account for alignment
      // requirements.
      if (LTy->isVoidPointerType() ^ RTy->isVoidPointerType()) {
        assert((Ctx.getTypeSize(LTy) == Ctx.getTypeSize(RTy)) &&
               "Pointer types have different bitwidths!");
        if (RTy->isVoidPointerType())
          RTy = LTy;
        else
          LTy = RTy;
      }

      if (LTy == RTy)
        return;
    }

    // Fallback: for the solver, assume that these types don't really matter
    if ((LTy.getCanonicalType() == RTy.getCanonicalType()) ||
        (LTy->isObjCObjectPointerType() && RTy->isObjCObjectPointerType())) {
      LTy = RTy;
      return;
    }

    // TODO: Refine behavior for invalid type casts
  }

  // Perform implicit integer type conversion.
  // May modify all input parameters.
  // TODO: Refactor to use Sema::handleIntegerConversion()
  template <typename T, T (*doCast)(SMTSolverRef &Solver, const T &, QualType,
                                    uint64_t, QualType, uint64_t)>
  static inline void doIntTypeConversion(SMTSolverRef &Solver, ASTContext &Ctx,
                                         T &LHS, QualType &LTy, T &RHS,
                                         QualType &RTy) {

    uint64_t LBitWidth = Ctx.getTypeSize(LTy);
    uint64_t RBitWidth = Ctx.getTypeSize(RTy);

    assert(!LTy.isNull() && !RTy.isNull() && "Input type is null!");
    // Always perform integer promotion before checking type equality.
    // Otherwise, e.g. (bool) a + (bool) b could trigger a backend assertion
    if (LTy->isPromotableIntegerType()) {
      QualType NewTy = Ctx.getPromotedIntegerType(LTy);
      uint64_t NewBitWidth = Ctx.getTypeSize(NewTy);
      LHS = (*doCast)(Solver, LHS, NewTy, NewBitWidth, LTy, LBitWidth);
      LTy = NewTy;
      LBitWidth = NewBitWidth;
    }
    if (RTy->isPromotableIntegerType()) {
      QualType NewTy = Ctx.getPromotedIntegerType(RTy);
      uint64_t NewBitWidth = Ctx.getTypeSize(NewTy);
      RHS = (*doCast)(Solver, RHS, NewTy, NewBitWidth, RTy, RBitWidth);
      RTy = NewTy;
      RBitWidth = NewBitWidth;
    }

    if (LTy == RTy)
      return;

    // Perform integer type conversion
    // Note: Safe to skip updating bitwidth because this must terminate
    bool isLSignedTy = LTy->isSignedIntegerOrEnumerationType();
    bool isRSignedTy = RTy->isSignedIntegerOrEnumerationType();

    int order = Ctx.getIntegerTypeOrder(LTy, RTy);
    if (isLSignedTy == isRSignedTy) {
      // Same signedness; use the higher-ranked type
      if (order == 1) {
        RHS = (*doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
        RTy = LTy;
      } else {
        LHS = (*doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
        LTy = RTy;
      }
    } else if (order != (isLSignedTy ? 1 : -1)) {
      // The unsigned type has greater than or equal rank to the
      // signed type, so use the unsigned type
      if (isRSignedTy) {
        RHS = (*doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
        RTy = LTy;
      } else {
        LHS = (*doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
        LTy = RTy;
      }
    } else if (LBitWidth != RBitWidth) {
      // The two types are different widths; if we are here, that
      // means the signed type is larger than the unsigned type, so
      // use the signed type.
      if (isLSignedTy) {
        RHS = (doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
        RTy = LTy;
      } else {
        LHS = (*doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
        LTy = RTy;
      }
    } else {
      // The signed type is higher-ranked than the unsigned type,
      // but isn't actually any bigger (like unsigned int and long
      // on most 32-bit systems).  Use the unsigned type corresponding
      // to the signed type.
      QualType NewTy =
          Ctx.getCorrespondingUnsignedType(isLSignedTy ? LTy : RTy);
      RHS = (*doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
      RTy = NewTy;
      LHS = (doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
      LTy = NewTy;
    }
  }

  // Perform implicit floating-point type conversion.
  // May modify all input parameters.
  // TODO: Refactor to use Sema::handleFloatConversion()
  template <typename T, T (*doCast)(SMTSolverRef &Solver, const T &, QualType,
                                    uint64_t, QualType, uint64_t)>
  static inline void
  doFloatTypeConversion(SMTSolverRef &Solver, ASTContext &Ctx, T &LHS,
                        QualType &LTy, T &RHS, QualType &RTy) {

    uint64_t LBitWidth = Ctx.getTypeSize(LTy);
    uint64_t RBitWidth = Ctx.getTypeSize(RTy);

    // Perform float-point type promotion
    if (!LTy->isRealFloatingType()) {
      LHS = (*doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
      LTy = RTy;
      LBitWidth = RBitWidth;
    }
    if (!RTy->isRealFloatingType()) {
      RHS = (*doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
      RTy = LTy;
      RBitWidth = LBitWidth;
    }

    if (LTy == RTy)
      return;

    // If we have two real floating types, convert the smaller operand to the
    // bigger result
    // Note: Safe to skip updating bitwidth because this must terminate
    int order = Ctx.getFloatingTypeOrder(LTy, RTy);
    if (order > 0) {
      RHS = (*doCast)(Solver, RHS, LTy, LBitWidth, RTy, RBitWidth);
      RTy = LTy;
    } else if (order == 0) {
      LHS = (*doCast)(Solver, LHS, RTy, RBitWidth, LTy, LBitWidth);
      LTy = RTy;
    } else {
      llvm_unreachable("Unsupported floating-point type cast!");
    }
  }
};
} // namespace ento
} // namespace clang

#endif