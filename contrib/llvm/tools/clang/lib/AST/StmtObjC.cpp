//===--- StmtObjC.cpp - Classes for representing ObjC statements ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Stmt class declared in StmtObjC.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtObjC.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ASTContext.h"

using namespace clang;

ObjCForCollectionStmt::ObjCForCollectionStmt(Stmt *Elem, Expr *Collect,
                                             Stmt *Body, SourceLocation FCL,
                                             SourceLocation RPL)
    : Stmt(ObjCForCollectionStmtClass) {
  SubExprs[ELEM] = Elem;
  SubExprs[COLLECTION] = Collect;
  SubExprs[BODY] = Body;
  ForLoc = FCL;
  RParenLoc = RPL;
}

ObjCAtTryStmt::ObjCAtTryStmt(SourceLocation atTryLoc, Stmt *atTryStmt,
                             Stmt **CatchStmts, unsigned NumCatchStmts,
                             Stmt *atFinallyStmt)
    : Stmt(ObjCAtTryStmtClass), AtTryLoc(atTryLoc),
      NumCatchStmts(NumCatchStmts), HasFinally(atFinallyStmt != nullptr) {
  Stmt **Stmts = getStmts();
  Stmts[0] = atTryStmt;
  for (unsigned I = 0; I != NumCatchStmts; ++I)
    Stmts[I + 1] = CatchStmts[I];

  if (HasFinally)
    Stmts[NumCatchStmts + 1] = atFinallyStmt;
}

ObjCAtTryStmt *ObjCAtTryStmt::Create(const ASTContext &Context,
                                     SourceLocation atTryLoc, Stmt *atTryStmt,
                                     Stmt **CatchStmts, unsigned NumCatchStmts,
                                     Stmt *atFinallyStmt) {
  unsigned Size =
      sizeof(ObjCAtTryStmt) +
      (1 + NumCatchStmts + (atFinallyStmt != nullptr)) * sizeof(Stmt *);
  void *Mem = Context.Allocate(Size, alignof(ObjCAtTryStmt));
  return new (Mem) ObjCAtTryStmt(atTryLoc, atTryStmt, CatchStmts, NumCatchStmts,
                                 atFinallyStmt);
}

ObjCAtTryStmt *ObjCAtTryStmt::CreateEmpty(const ASTContext &Context,
                                          unsigned NumCatchStmts,
                                          bool HasFinally) {
  unsigned Size =
      sizeof(ObjCAtTryStmt) + (1 + NumCatchStmts + HasFinally) * sizeof(Stmt *);
  void *Mem = Context.Allocate(Size, alignof(ObjCAtTryStmt));
  return new (Mem) ObjCAtTryStmt(EmptyShell(), NumCatchStmts, HasFinally);
}

SourceLocation ObjCAtTryStmt::getEndLoc() const {
  if (HasFinally)
    return getFinallyStmt()->getEndLoc();
  if (NumCatchStmts)
    return getCatchStmt(NumCatchStmts - 1)->getEndLoc();
  return getTryBody()->getEndLoc();
}
