//== SMTConstraintManager.h -------------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic API, which will be the base class for
//  every SMT solver specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONSTRAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONSTRAINTMANAGER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SMTConv.h"

namespace clang {
namespace ento {

template <typename ConstraintSMT, typename SMTExprTy>
class SMTConstraintManager : public clang::ento::SimpleConstraintManager {
  SMTSolverRef &Solver;

public:
  SMTConstraintManager(clang::ento::SubEngine *SE, clang::ento::SValBuilder &SB,
                       SMTSolverRef &S)
      : SimpleConstraintManager(SE, SB), Solver(S) {}
  virtual ~SMTConstraintManager() = default;

  //===------------------------------------------------------------------===//
  // Implementation for interface from SimpleConstraintManager.
  //===------------------------------------------------------------------===//

  ProgramStateRef assumeSym(ProgramStateRef State, SymbolRef Sym,
                            bool Assumption) override {
    ASTContext &Ctx = getBasicVals().getContext();

    QualType RetTy;
    bool hasComparison;

    SMTExprRef Exp = SMTConv::getExpr(Solver, Ctx, Sym, &RetTy, &hasComparison);

    // Create zero comparison for implicit boolean cast, with reversed
    // assumption
    if (!hasComparison && !RetTy->isBooleanType())
      return assumeExpr(
          State, Sym,
          SMTConv::getZeroExpr(Solver, Ctx, Exp, RetTy, !Assumption));

    return assumeExpr(State, Sym, Assumption ? Exp : Solver->mkNot(Exp));
  }

  ProgramStateRef assumeSymInclusiveRange(ProgramStateRef State, SymbolRef Sym,
                                          const llvm::APSInt &From,
                                          const llvm::APSInt &To,
                                          bool InRange) override {
    ASTContext &Ctx = getBasicVals().getContext();
    return assumeExpr(
        State, Sym, SMTConv::getRangeExpr(Solver, Ctx, Sym, From, To, InRange));
  }

  ProgramStateRef assumeSymUnsupported(ProgramStateRef State, SymbolRef Sym,
                                       bool Assumption) override {
    // Skip anything that is unsupported
    return State;
  }

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

  ConditionTruthVal checkNull(ProgramStateRef State, SymbolRef Sym) override {
    ASTContext &Ctx = getBasicVals().getContext();

    QualType RetTy;
    // The expression may be casted, so we cannot call getZ3DataExpr() directly
    SMTExprRef VarExp = SMTConv::getExpr(Solver, Ctx, Sym, &RetTy);
    SMTExprRef Exp =
        SMTConv::getZeroExpr(Solver, Ctx, VarExp, RetTy, /*Assumption=*/true);

    // Negate the constraint
    SMTExprRef NotExp =
        SMTConv::getZeroExpr(Solver, Ctx, VarExp, RetTy, /*Assumption=*/false);

    ConditionTruthVal isSat = checkModel(State, Sym, Exp);
    ConditionTruthVal isNotSat = checkModel(State, Sym, NotExp);

    // Zero is the only possible solution
    if (isSat.isConstrainedTrue() && isNotSat.isConstrainedFalse())
      return true;

    // Zero is not a solution
    if (isSat.isConstrainedFalse() && isNotSat.isConstrainedTrue())
      return false;

    // Zero may be a solution
    return ConditionTruthVal();
  }

  const llvm::APSInt *getSymVal(ProgramStateRef State,
                                SymbolRef Sym) const override {
    BasicValueFactory &BVF = getBasicVals();
    ASTContext &Ctx = BVF.getContext();

    if (const SymbolData *SD = dyn_cast<SymbolData>(Sym)) {
      QualType Ty = Sym->getType();
      assert(!Ty->isRealFloatingType());
      llvm::APSInt Value(Ctx.getTypeSize(Ty),
                         !Ty->isSignedIntegerOrEnumerationType());

      // TODO: this should call checkModel so we can use the cache, however,
      // this method tries to get the interpretation (the actual value) from
      // the solver, which is currently not cached.

      SMTExprRef Exp =
          SMTConv::fromData(Solver, SD->getSymbolID(), Ty, Ctx.getTypeSize(Ty));

      Solver->reset();
      addStateConstraints(State);

      // Constraints are unsatisfiable
      Optional<bool> isSat = Solver->check();
      if (!isSat.hasValue() || !isSat.getValue())
        return nullptr;

      // Model does not assign interpretation
      if (!Solver->getInterpretation(Exp, Value))
        return nullptr;

      // A value has been obtained, check if it is the only value
      SMTExprRef NotExp = SMTConv::fromBinOp(
          Solver, Exp, BO_NE,
          Ty->isBooleanType() ? Solver->mkBoolean(Value.getBoolValue())
                              : Solver->mkBitvector(Value, Value.getBitWidth()),
          /*isSigned=*/false);

      Solver->addConstraint(NotExp);

      Optional<bool> isNotSat = Solver->check();
      if (!isSat.hasValue() || isNotSat.getValue())
        return nullptr;

      // This is the only solution, store it
      return &BVF.getValue(Value);
    }

    if (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym)) {
      SymbolRef CastSym = SC->getOperand();
      QualType CastTy = SC->getType();
      // Skip the void type
      if (CastTy->isVoidType())
        return nullptr;

      const llvm::APSInt *Value;
      if (!(Value = getSymVal(State, CastSym)))
        return nullptr;
      return &BVF.Convert(SC->getType(), *Value);
    }

    if (const BinarySymExpr *BSE = dyn_cast<BinarySymExpr>(Sym)) {
      const llvm::APSInt *LHS, *RHS;
      if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(BSE)) {
        LHS = getSymVal(State, SIE->getLHS());
        RHS = &SIE->getRHS();
      } else if (const IntSymExpr *ISE = dyn_cast<IntSymExpr>(BSE)) {
        LHS = &ISE->getLHS();
        RHS = getSymVal(State, ISE->getRHS());
      } else if (const SymSymExpr *SSM = dyn_cast<SymSymExpr>(BSE)) {
        // Early termination to avoid expensive call
        LHS = getSymVal(State, SSM->getLHS());
        RHS = LHS ? getSymVal(State, SSM->getRHS()) : nullptr;
      } else {
        llvm_unreachable("Unsupported binary expression to get symbol value!");
      }

      if (!LHS || !RHS)
        return nullptr;

      llvm::APSInt ConvertedLHS, ConvertedRHS;
      QualType LTy, RTy;
      std::tie(ConvertedLHS, LTy) = SMTConv::fixAPSInt(Ctx, *LHS);
      std::tie(ConvertedRHS, RTy) = SMTConv::fixAPSInt(Ctx, *RHS);
      SMTConv::doIntTypeConversion<llvm::APSInt, &SMTConv::castAPSInt>(
          Solver, Ctx, ConvertedLHS, LTy, ConvertedRHS, RTy);
      return BVF.evalAPSInt(BSE->getOpcode(), ConvertedLHS, ConvertedRHS);
    }

    llvm_unreachable("Unsupported expression to get symbol value!");
  }

  ProgramStateRef removeDeadBindings(ProgramStateRef State,
                                     SymbolReaper &SymReaper) override {
    auto CZ = State->get<ConstraintSMT>();
    auto &CZFactory = State->get_context<ConstraintSMT>();

    for (auto I = CZ.begin(), E = CZ.end(); I != E; ++I) {
      if (SymReaper.isDead(I->first))
        CZ = CZFactory.remove(CZ, *I);
    }

    return State->set<ConstraintSMT>(CZ);
  }

  void print(ProgramStateRef St, raw_ostream &OS, const char *nl,
             const char *sep) override {

    auto CZ = St->get<ConstraintSMT>();

    OS << nl << sep << "Constraints:";
    for (auto I = CZ.begin(), E = CZ.end(); I != E; ++I) {
      OS << nl << ' ' << I->first << " : ";
      I->second.print(OS);
    }
    OS << nl;
  }

  bool canReasonAbout(SVal X) const override {
    const TargetInfo &TI = getBasicVals().getContext().getTargetInfo();

    Optional<nonloc::SymbolVal> SymVal = X.getAs<nonloc::SymbolVal>();
    if (!SymVal)
      return true;

    const SymExpr *Sym = SymVal->getSymbol();
    QualType Ty = Sym->getType();

    // Complex types are not modeled
    if (Ty->isComplexType() || Ty->isComplexIntegerType())
      return false;

    // Non-IEEE 754 floating-point types are not modeled
    if ((Ty->isSpecificBuiltinType(BuiltinType::LongDouble) &&
         (&TI.getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended() ||
          &TI.getLongDoubleFormat() == &llvm::APFloat::PPCDoubleDouble())))
      return false;

    if (Ty->isRealFloatingType())
      return Solver->isFPSupported();

    if (isa<SymbolData>(Sym))
      return true;

    SValBuilder &SVB = getSValBuilder();

    if (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym))
      return canReasonAbout(SVB.makeSymbolVal(SC->getOperand()));

    if (const BinarySymExpr *BSE = dyn_cast<BinarySymExpr>(Sym)) {
      if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(SIE->getLHS()));

      if (const IntSymExpr *ISE = dyn_cast<IntSymExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(ISE->getRHS()));

      if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(SSE->getLHS())) &&
               canReasonAbout(SVB.makeSymbolVal(SSE->getRHS()));
    }

    llvm_unreachable("Unsupported expression to reason about!");
  }

  /// Dumps SMT formula
  LLVM_DUMP_METHOD void dump() const { Solver->dump(); }

protected:
  // Check whether a new model is satisfiable, and update the program state.
  virtual ProgramStateRef assumeExpr(ProgramStateRef State, SymbolRef Sym,
                                     const SMTExprRef &Exp) {
    // Check the model, avoid simplifying AST to save time
    if (checkModel(State, Sym, Exp).isConstrainedTrue())
      return State->add<ConstraintSMT>(
          std::make_pair(Sym, static_cast<const SMTExprTy &>(*Exp)));

    return nullptr;
  }

  /// Given a program state, construct the logical conjunction and add it to
  /// the solver
  virtual void addStateConstraints(ProgramStateRef State) const {
    // TODO: Don't add all the constraints, only the relevant ones
    auto CZ = State->get<ConstraintSMT>();
    auto I = CZ.begin(), IE = CZ.end();

    // Construct the logical AND of all the constraints
    if (I != IE) {
      std::vector<SMTExprRef> ASTs;

      SMTExprRef Constraint = Solver->newExprRef(I++->second);
      while (I != IE) {
        Constraint = Solver->mkAnd(Constraint, Solver->newExprRef(I++->second));
      }

      Solver->addConstraint(Constraint);
    }
  }

  // Generate and check a Z3 model, using the given constraint.
  ConditionTruthVal checkModel(ProgramStateRef State, SymbolRef Sym,
                               const SMTExprRef &Exp) const {
    ProgramStateRef NewState = State->add<ConstraintSMT>(
        std::make_pair(Sym, static_cast<const SMTExprTy &>(*Exp)));

    llvm::FoldingSetNodeID ID;
    NewState->get<ConstraintSMT>().Profile(ID);

    unsigned hash = ID.ComputeHash();
    auto I = Cached.find(hash);
    if (I != Cached.end())
      return I->second;

    Solver->reset();
    addStateConstraints(NewState);

    Optional<bool> res = Solver->check();
    if (!res.hasValue())
      Cached[hash] = ConditionTruthVal();
    else
      Cached[hash] = ConditionTruthVal(res.getValue());

    return Cached[hash];
  }

  // Cache the result of an SMT query (true, false, unknown). The key is the
  // hash of the constraints in a state
  mutable llvm::DenseMap<unsigned, ConditionTruthVal> Cached;
}; // end class SMTConstraintManager

} // namespace ento
} // namespace clang

#endif
