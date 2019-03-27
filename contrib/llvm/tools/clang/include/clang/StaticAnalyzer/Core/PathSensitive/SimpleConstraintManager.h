//== SimpleConstraintManager.h ----------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Simplified constraint manager backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SIMPLECONSTRAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SIMPLECONSTRAINTMANAGER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {

namespace ento {

class SimpleConstraintManager : public ConstraintManager {
  SubEngine *SU;
  SValBuilder &SVB;

public:
  SimpleConstraintManager(SubEngine *subengine, SValBuilder &SB)
      : SU(subengine), SVB(SB) {}

  ~SimpleConstraintManager() override;

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

  /// Ensures that the DefinedSVal conditional is expressed as a NonLoc by
  /// creating boolean casts to handle Loc's.
  ProgramStateRef assume(ProgramStateRef State, DefinedSVal Cond,
                         bool Assumption) override;

  ProgramStateRef assumeInclusiveRange(ProgramStateRef State, NonLoc Value,
                                       const llvm::APSInt &From,
                                       const llvm::APSInt &To,
                                       bool InRange) override;

protected:
  //===------------------------------------------------------------------===//
  // Interface that subclasses must implement.
  //===------------------------------------------------------------------===//

  /// Given a symbolic expression that can be reasoned about, assume that it is
  /// true/false and generate the new program state.
  virtual ProgramStateRef assumeSym(ProgramStateRef State, SymbolRef Sym,
                                    bool Assumption) = 0;

  /// Given a symbolic expression within the range [From, To], assume that it is
  /// true/false and generate the new program state.
  /// This function is used to handle case ranges produced by a language
  /// extension for switch case statements.
  virtual ProgramStateRef assumeSymInclusiveRange(ProgramStateRef State,
                                                  SymbolRef Sym,
                                                  const llvm::APSInt &From,
                                                  const llvm::APSInt &To,
                                                  bool InRange) = 0;

  /// Given a symbolic expression that cannot be reasoned about, assume that
  /// it is zero/nonzero and add it directly to the solver state.
  virtual ProgramStateRef assumeSymUnsupported(ProgramStateRef State,
                                               SymbolRef Sym,
                                               bool Assumption) = 0;

  //===------------------------------------------------------------------===//
  // Internal implementation.
  //===------------------------------------------------------------------===//

  SValBuilder &getSValBuilder() const { return SVB; }
  BasicValueFactory &getBasicVals() const { return SVB.getBasicValueFactory(); }
  SymbolManager &getSymbolManager() const { return SVB.getSymbolManager(); }

private:
  ProgramStateRef assume(ProgramStateRef State, NonLoc Cond, bool Assumption);

  ProgramStateRef assumeAux(ProgramStateRef State, NonLoc Cond,
                            bool Assumption);
};

} // end namespace ento

} // end namespace clang

#endif
