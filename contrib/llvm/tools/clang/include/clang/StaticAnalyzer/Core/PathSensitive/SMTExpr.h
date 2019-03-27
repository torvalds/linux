//== SMTExpr.h --------------------------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic Expr API, which will be the base class
//  for every SMT solver expr specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTEXPR_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTEXPR_H

#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/FoldingSet.h"

namespace clang {
namespace ento {

/// Generic base class for SMT exprs
class SMTExpr {
public:
  SMTExpr() = default;
  virtual ~SMTExpr() = default;

  bool operator<(const SMTExpr &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  virtual void Profile(llvm::FoldingSetNodeID &ID) const {
    static int Tag = 0;
    ID.AddPointer(&Tag);
  }

  friend bool operator==(SMTExpr const &LHS, SMTExpr const &RHS) {
    return LHS.equal_to(RHS);
  }

  virtual void print(raw_ostream &OS) const = 0;

  LLVM_DUMP_METHOD void dump() const { print(llvm::errs()); }

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  virtual bool equal_to(SMTExpr const &other) const = 0;
};

/// Shared pointer for SMTExprs, used by SMTSolver API.
using SMTExprRef = std::shared_ptr<SMTExpr>;

} // namespace ento
} // namespace clang

#endif
