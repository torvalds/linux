//===--- DeclOpenMP.cpp - Declaration OpenMP AST Node Implementation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements OMPThreadPrivateDecl, OMPCapturedExprDecl
/// classes.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/Expr.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// OMPThreadPrivateDecl Implementation.
//===----------------------------------------------------------------------===//

void OMPThreadPrivateDecl::anchor() { }

OMPThreadPrivateDecl *OMPThreadPrivateDecl::Create(ASTContext &C,
                                                   DeclContext *DC,
                                                   SourceLocation L,
                                                   ArrayRef<Expr *> VL) {
  OMPThreadPrivateDecl *D =
      new (C, DC, additionalSizeToAlloc<Expr *>(VL.size()))
          OMPThreadPrivateDecl(OMPThreadPrivate, DC, L);
  D->NumVars = VL.size();
  D->setVars(VL);
  return D;
}

OMPThreadPrivateDecl *OMPThreadPrivateDecl::CreateDeserialized(ASTContext &C,
                                                               unsigned ID,
                                                               unsigned N) {
  OMPThreadPrivateDecl *D = new (C, ID, additionalSizeToAlloc<Expr *>(N))
      OMPThreadPrivateDecl(OMPThreadPrivate, nullptr, SourceLocation());
  D->NumVars = N;
  return D;
}

void OMPThreadPrivateDecl::setVars(ArrayRef<Expr *> VL) {
  assert(VL.size() == NumVars &&
         "Number of variables is not the same as the preallocated buffer");
  std::uninitialized_copy(VL.begin(), VL.end(), getTrailingObjects<Expr *>());
}

//===----------------------------------------------------------------------===//
// OMPRequiresDecl Implementation.
//===----------------------------------------------------------------------===//

void OMPRequiresDecl::anchor() {}

OMPRequiresDecl *OMPRequiresDecl::Create(ASTContext &C, DeclContext *DC,
                                         SourceLocation L,
                                         ArrayRef<OMPClause *> CL) {
  OMPRequiresDecl *D =
      new (C, DC, additionalSizeToAlloc<OMPClause *>(CL.size()))
      OMPRequiresDecl(OMPRequires, DC, L);
  D->NumClauses = CL.size();
  D->setClauses(CL);
  return D;
}

OMPRequiresDecl *OMPRequiresDecl::CreateDeserialized(ASTContext &C, unsigned ID,
                                                     unsigned N) {
  OMPRequiresDecl *D = new (C, ID, additionalSizeToAlloc<OMPClause *>(N))
      OMPRequiresDecl(OMPRequires, nullptr, SourceLocation());
  D->NumClauses = N;
  return D;
}

void OMPRequiresDecl::setClauses(ArrayRef<OMPClause *> CL) {
  assert(CL.size() == NumClauses &&
         "Number of clauses is not the same as the preallocated buffer");
  std::uninitialized_copy(CL.begin(), CL.end(),
                          getTrailingObjects<OMPClause *>());
}

//===----------------------------------------------------------------------===//
// OMPDeclareReductionDecl Implementation.
//===----------------------------------------------------------------------===//

OMPDeclareReductionDecl::OMPDeclareReductionDecl(
    Kind DK, DeclContext *DC, SourceLocation L, DeclarationName Name,
    QualType Ty, OMPDeclareReductionDecl *PrevDeclInScope)
    : ValueDecl(DK, DC, L, Name, Ty), DeclContext(DK), Combiner(nullptr),
      PrevDeclInScope(PrevDeclInScope) {
  setInitializer(nullptr, CallInit);
}

void OMPDeclareReductionDecl::anchor() {}

OMPDeclareReductionDecl *OMPDeclareReductionDecl::Create(
    ASTContext &C, DeclContext *DC, SourceLocation L, DeclarationName Name,
    QualType T, OMPDeclareReductionDecl *PrevDeclInScope) {
  return new (C, DC) OMPDeclareReductionDecl(OMPDeclareReduction, DC, L, Name,
                                             T, PrevDeclInScope);
}

OMPDeclareReductionDecl *
OMPDeclareReductionDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) OMPDeclareReductionDecl(
      OMPDeclareReduction, /*DC=*/nullptr, SourceLocation(), DeclarationName(),
      QualType(), /*PrevDeclInScope=*/nullptr);
}

OMPDeclareReductionDecl *OMPDeclareReductionDecl::getPrevDeclInScope() {
  return cast_or_null<OMPDeclareReductionDecl>(
      PrevDeclInScope.get(getASTContext().getExternalSource()));
}
const OMPDeclareReductionDecl *
OMPDeclareReductionDecl::getPrevDeclInScope() const {
  return cast_or_null<OMPDeclareReductionDecl>(
      PrevDeclInScope.get(getASTContext().getExternalSource()));
}

//===----------------------------------------------------------------------===//
// OMPCapturedExprDecl Implementation.
//===----------------------------------------------------------------------===//

void OMPCapturedExprDecl::anchor() {}

OMPCapturedExprDecl *OMPCapturedExprDecl::Create(ASTContext &C, DeclContext *DC,
                                                 IdentifierInfo *Id, QualType T,
                                                 SourceLocation StartLoc) {
  return new (C, DC) OMPCapturedExprDecl(
      C, DC, Id, T, C.getTrivialTypeSourceInfo(T), StartLoc);
}

OMPCapturedExprDecl *OMPCapturedExprDecl::CreateDeserialized(ASTContext &C,
                                                             unsigned ID) {
  return new (C, ID) OMPCapturedExprDecl(C, nullptr, nullptr, QualType(),
                                         /*TInfo=*/nullptr, SourceLocation());
}

SourceRange OMPCapturedExprDecl::getSourceRange() const {
  assert(hasInit());
  return SourceRange(getInit()->getBeginLoc(), getInit()->getEndLoc());
}
