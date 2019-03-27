//===--- ParentMap.cpp - Mappings from Stmts to their Parents ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ParentMap class.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ParentMap.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtObjC.h"
#include "llvm/ADT/DenseMap.h"

using namespace clang;

typedef llvm::DenseMap<Stmt*, Stmt*> MapTy;

enum OpaqueValueMode {
  OV_Transparent,
  OV_Opaque
};

static void BuildParentMap(MapTy& M, Stmt* S,
                           OpaqueValueMode OVMode = OV_Transparent) {
  if (!S)
    return;

  switch (S->getStmtClass()) {
  case Stmt::PseudoObjectExprClass: {
    assert(OVMode == OV_Transparent && "Should not appear alongside OVEs");
    PseudoObjectExpr *POE = cast<PseudoObjectExpr>(S);

    // If we are rebuilding the map, clear out any existing state.
    if (M[POE->getSyntacticForm()])
      for (Stmt *SubStmt : S->children())
        M[SubStmt] = nullptr;

    M[POE->getSyntacticForm()] = S;
    BuildParentMap(M, POE->getSyntacticForm(), OV_Transparent);

    for (PseudoObjectExpr::semantics_iterator I = POE->semantics_begin(),
                                              E = POE->semantics_end();
         I != E; ++I) {
      M[*I] = S;
      BuildParentMap(M, *I, OV_Opaque);
    }
    break;
  }
  case Stmt::BinaryConditionalOperatorClass: {
    assert(OVMode == OV_Transparent && "Should not appear alongside OVEs");
    BinaryConditionalOperator *BCO = cast<BinaryConditionalOperator>(S);

    M[BCO->getCommon()] = S;
    BuildParentMap(M, BCO->getCommon(), OV_Transparent);

    M[BCO->getCond()] = S;
    BuildParentMap(M, BCO->getCond(), OV_Opaque);

    M[BCO->getTrueExpr()] = S;
    BuildParentMap(M, BCO->getTrueExpr(), OV_Opaque);

    M[BCO->getFalseExpr()] = S;
    BuildParentMap(M, BCO->getFalseExpr(), OV_Transparent);

    break;
  }
  case Stmt::OpaqueValueExprClass: {
    // FIXME: This isn't correct; it assumes that multiple OpaqueValueExprs
    // share a single source expression, but in the AST a single
    // OpaqueValueExpr is shared among multiple parent expressions.
    // The right thing to do is to give the OpaqueValueExpr its syntactic
    // parent, then not reassign that when traversing the semantic expressions.
    OpaqueValueExpr *OVE = cast<OpaqueValueExpr>(S);
    if (OVMode == OV_Transparent || !M[OVE->getSourceExpr()]) {
      M[OVE->getSourceExpr()] = S;
      BuildParentMap(M, OVE->getSourceExpr(), OV_Transparent);
    }
    break;
  }
  default:
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt) {
        M[SubStmt] = S;
        BuildParentMap(M, SubStmt, OVMode);
      }
    }
    break;
  }
}

ParentMap::ParentMap(Stmt *S) : Impl(nullptr) {
  if (S) {
    MapTy *M = new MapTy();
    BuildParentMap(*M, S);
    Impl = M;
  }
}

ParentMap::~ParentMap() {
  delete (MapTy*) Impl;
}

void ParentMap::addStmt(Stmt* S) {
  if (S) {
    BuildParentMap(*(MapTy*) Impl, S);
  }
}

void ParentMap::setParent(const Stmt *S, const Stmt *Parent) {
  assert(S);
  assert(Parent);
  MapTy *M = reinterpret_cast<MapTy *>(Impl);
  M->insert(std::make_pair(const_cast<Stmt *>(S), const_cast<Stmt *>(Parent)));
}

Stmt* ParentMap::getParent(Stmt* S) const {
  MapTy* M = (MapTy*) Impl;
  MapTy::iterator I = M->find(S);
  return I == M->end() ? nullptr : I->second;
}

Stmt *ParentMap::getParentIgnoreParens(Stmt *S) const {
  do { S = getParent(S); } while (S && isa<ParenExpr>(S));
  return S;
}

Stmt *ParentMap::getParentIgnoreParenCasts(Stmt *S) const {
  do {
    S = getParent(S);
  }
  while (S && (isa<ParenExpr>(S) || isa<CastExpr>(S)));

  return S;
}

Stmt *ParentMap::getParentIgnoreParenImpCasts(Stmt *S) const {
  do {
    S = getParent(S);
  } while (S && isa<Expr>(S) && cast<Expr>(S)->IgnoreParenImpCasts() != S);

  return S;
}

Stmt *ParentMap::getOuterParenParent(Stmt *S) const {
  Stmt *Paren = nullptr;
  while (isa<ParenExpr>(S)) {
    Paren = S;
    S = getParent(S);
  };
  return Paren;
}

bool ParentMap::isConsumedExpr(Expr* E) const {
  Stmt *P = getParent(E);
  Stmt *DirectChild = E;

  // Ignore parents that don't guarantee consumption.
  while (P && (isa<ParenExpr>(P) || isa<CastExpr>(P) ||
               isa<FullExpr>(P))) {
    DirectChild = P;
    P = getParent(P);
  }

  if (!P)
    return false;

  switch (P->getStmtClass()) {
    default:
      return isa<Expr>(P);
    case Stmt::DeclStmtClass:
      return true;
    case Stmt::BinaryOperatorClass: {
      BinaryOperator *BE = cast<BinaryOperator>(P);
      // If it is a comma, only the right side is consumed.
      // If it isn't a comma, both sides are consumed.
      return BE->getOpcode()!=BO_Comma ||DirectChild==BE->getRHS();
    }
    case Stmt::ForStmtClass:
      return DirectChild == cast<ForStmt>(P)->getCond();
    case Stmt::WhileStmtClass:
      return DirectChild == cast<WhileStmt>(P)->getCond();
    case Stmt::DoStmtClass:
      return DirectChild == cast<DoStmt>(P)->getCond();
    case Stmt::IfStmtClass:
      return DirectChild == cast<IfStmt>(P)->getCond();
    case Stmt::IndirectGotoStmtClass:
      return DirectChild == cast<IndirectGotoStmt>(P)->getTarget();
    case Stmt::SwitchStmtClass:
      return DirectChild == cast<SwitchStmt>(P)->getCond();
    case Stmt::ObjCForCollectionStmtClass:
      return DirectChild == cast<ObjCForCollectionStmt>(P)->getCollection();
    case Stmt::ReturnStmtClass:
      return true;
  }
}

