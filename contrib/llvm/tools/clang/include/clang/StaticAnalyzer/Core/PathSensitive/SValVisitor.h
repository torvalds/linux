//===--- SValVisitor.h - Visitor for SVal subclasses ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SValVisitor, SymExprVisitor, and MemRegionVisitor
//  interfaces, and also FullSValVisitor, which visits all three hierarchies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALVISITOR_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALVISITOR_H

#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"

namespace clang {

namespace ento {

/// SValVisitor - this class implements a simple visitor for SVal
/// subclasses.
template <typename ImplClass, typename RetTy = void> class SValVisitor {
public:

#define DISPATCH(NAME, CLASS) \
  return static_cast<ImplClass *>(this)->Visit ## NAME(V.castAs<CLASS>())

  RetTy Visit(SVal V) {
    // Dispatch to VisitFooVal for each FooVal.
    // Take namespaces (loc:: and nonloc::) into account.
    switch (V.getBaseKind()) {
#define BASIC_SVAL(Id, Parent) case SVal::Id ## Kind: DISPATCH(Id, Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
    case SVal::LocKind:
      switch (V.getSubKind()) {
#define LOC_SVAL(Id, Parent) \
      case loc::Id ## Kind: DISPATCH(Loc ## Id, loc :: Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
      }
      llvm_unreachable("Unknown Loc sub-kind!");
    case SVal::NonLocKind:
      switch (V.getSubKind()) {
#define NONLOC_SVAL(Id, Parent) \
      case nonloc::Id ## Kind: DISPATCH(NonLoc ## Id, nonloc :: Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
      }
      llvm_unreachable("Unknown NonLoc sub-kind!");
    }
    llvm_unreachable("Unknown SVal kind!");
  }

#define BASIC_SVAL(Id, Parent) \
  RetTy Visit ## Id(Id V) { DISPATCH(Parent, Id); }
#define ABSTRACT_SVAL(Id, Parent) \
  BASIC_SVAL(Id, Parent)
#define LOC_SVAL(Id, Parent) \
  RetTy VisitLoc ## Id(loc::Id V) { DISPATCH(Parent, Parent); }
#define NONLOC_SVAL(Id, Parent) \
  RetTy VisitNonLoc ## Id(nonloc::Id V) { DISPATCH(Parent, Parent); }
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"

  // Base case, ignore it. :)
  RetTy VisitSVal(SVal V) { return RetTy(); }

#undef DISPATCH
};

/// SymExprVisitor - this class implements a simple visitor for SymExpr
/// subclasses.
template <typename ImplClass, typename RetTy = void> class SymExprVisitor {
public:

#define DISPATCH(CLASS) \
    return static_cast<ImplClass *>(this)->Visit ## CLASS(cast<CLASS>(S))

  RetTy Visit(SymbolRef S) {
    // Dispatch to VisitSymbolFoo for each SymbolFoo.
    switch (S->getKind()) {
#define SYMBOL(Id, Parent) \
    case SymExpr::Id ## Kind: DISPATCH(Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"
    }
    llvm_unreachable("Unknown SymExpr kind!");
  }

  // If the implementation chooses not to implement a certain visit method, fall
  // back on visiting the superclass.
#define SYMBOL(Id, Parent) RetTy Visit ## Id(const Id *S) { DISPATCH(Parent); }
#define ABSTRACT_SYMBOL(Id, Parent) SYMBOL(Id, Parent)
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"

  // Base case, ignore it. :)
  RetTy VisitSymExpr(SymbolRef S) { return RetTy(); }

#undef DISPATCH
};

/// MemRegionVisitor - this class implements a simple visitor for MemRegion
/// subclasses.
template <typename ImplClass, typename RetTy = void> class MemRegionVisitor {
public:

#define DISPATCH(CLASS) \
  return static_cast<ImplClass *>(this)->Visit ## CLASS(cast<CLASS>(R))

  RetTy Visit(const MemRegion *R) {
    // Dispatch to VisitFooRegion for each FooRegion.
    switch (R->getKind()) {
#define REGION(Id, Parent) case MemRegion::Id ## Kind: DISPATCH(Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"
    }
    llvm_unreachable("Unknown MemRegion kind!");
  }

  // If the implementation chooses not to implement a certain visit method, fall
  // back on visiting the superclass.
#define REGION(Id, Parent) \
  RetTy Visit ## Id(const Id *R) { DISPATCH(Parent); }
#define ABSTRACT_REGION(Id, Parent) \
  REGION(Id, Parent)
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"

  // Base case, ignore it. :)
  RetTy VisitMemRegion(const MemRegion *R) { return RetTy(); }

#undef DISPATCH
};

/// FullSValVisitor - a convenient mixed visitor for all three:
/// SVal, SymExpr and MemRegion subclasses.
template <typename ImplClass, typename RetTy = void>
class FullSValVisitor : public SValVisitor<ImplClass, RetTy>,
                        public SymExprVisitor<ImplClass, RetTy>,
                        public MemRegionVisitor<ImplClass, RetTy> {
public:
  using SValVisitor<ImplClass, RetTy>::Visit;
  using SymExprVisitor<ImplClass, RetTy>::Visit;
  using MemRegionVisitor<ImplClass, RetTy>::Visit;
};

} // end namespace ento

} // end namespace clang

#endif
