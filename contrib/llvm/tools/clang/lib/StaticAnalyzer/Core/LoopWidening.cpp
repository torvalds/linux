//===--- LoopWidening.cpp - Widen loops -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This file contains functions which are used to widen loops. A loop may be
/// widened to approximate the exit state(s), without analyzing every
/// iteration. The widening is done by invalidating anything which might be
/// modified by the body of the loop.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopWidening.h"

using namespace clang;
using namespace ento;
using namespace clang::ast_matchers;

const auto MatchRef = "matchref";

/// Return the loops condition Stmt or NULL if LoopStmt is not a loop
static const Expr *getLoopCondition(const Stmt *LoopStmt) {
  switch (LoopStmt->getStmtClass()) {
  default:
    return nullptr;
  case Stmt::ForStmtClass:
    return cast<ForStmt>(LoopStmt)->getCond();
  case Stmt::WhileStmtClass:
    return cast<WhileStmt>(LoopStmt)->getCond();
  case Stmt::DoStmtClass:
    return cast<DoStmt>(LoopStmt)->getCond();
  }
}

namespace clang {
namespace ento {

ProgramStateRef getWidenedLoopState(ProgramStateRef PrevState,
                                    const LocationContext *LCtx,
                                    unsigned BlockCount, const Stmt *LoopStmt) {

  assert(isa<ForStmt>(LoopStmt) || isa<WhileStmt>(LoopStmt) ||
         isa<DoStmt>(LoopStmt));

  // Invalidate values in the current state.
  // TODO Make this more conservative by only invalidating values that might
  //      be modified by the body of the loop.
  // TODO Nested loops are currently widened as a result of the invalidation
  //      being so inprecise. When the invalidation is improved, the handling
  //      of nested loops will also need to be improved.
  ASTContext &ASTCtx = LCtx->getAnalysisDeclContext()->getASTContext();
  const StackFrameContext *STC = LCtx->getStackFrame();
  MemRegionManager &MRMgr = PrevState->getStateManager().getRegionManager();
  const MemRegion *Regions[] = {MRMgr.getStackLocalsRegion(STC),
                                MRMgr.getStackArgumentsRegion(STC),
                                MRMgr.getGlobalsRegion()};
  RegionAndSymbolInvalidationTraits ITraits;
  for (auto *Region : Regions) {
    ITraits.setTrait(Region,
                     RegionAndSymbolInvalidationTraits::TK_EntireMemSpace);
  }

  // References should not be invalidated.
  auto Matches = match(findAll(stmt(hasDescendant(varDecl(hasType(referenceType())).bind(MatchRef)))),
                       *LCtx->getDecl()->getBody(), ASTCtx);
  for (BoundNodes Match : Matches) {
    const VarDecl *VD = Match.getNodeAs<VarDecl>(MatchRef);
    assert(VD);
    const VarRegion *VarMem = MRMgr.getVarRegion(VD, LCtx);
    ITraits.setTrait(VarMem,
                     RegionAndSymbolInvalidationTraits::TK_PreserveContents);
  }


  // 'this' pointer is not an lvalue, we should not invalidate it. If the loop
  // is located in a method, constructor or destructor, the value of 'this'
  // pointer should remain unchanged.  Ignore static methods, since they do not
  // have 'this' pointers.
  const CXXMethodDecl *CXXMD = dyn_cast<CXXMethodDecl>(STC->getDecl());
  if (CXXMD && !CXXMD->isStatic()) {
    const CXXThisRegion *ThisR =
        MRMgr.getCXXThisRegion(CXXMD->getThisType(), STC);
    ITraits.setTrait(ThisR,
                     RegionAndSymbolInvalidationTraits::TK_PreserveContents);
  }

  return PrevState->invalidateRegions(Regions, getLoopCondition(LoopStmt),
                                      BlockCount, LCtx, true, nullptr, nullptr,
                                      &ITraits);
}

} // end namespace ento
} // end namespace clang
