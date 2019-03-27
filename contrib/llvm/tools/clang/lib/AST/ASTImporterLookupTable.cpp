//===- ASTImporterLookupTable.cpp - ASTImporter specific lookup -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporterLookupTable class which implements a
//  lookup procedure for the import mechanism.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTImporterLookupTable.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang {

namespace {

struct Builder : RecursiveASTVisitor<Builder> {
  ASTImporterLookupTable &LT;
  Builder(ASTImporterLookupTable &LT) : LT(LT) {}
  bool VisitNamedDecl(NamedDecl *D) {
    LT.add(D);
    return true;
  }
  bool VisitFriendDecl(FriendDecl *D) {
    if (D->getFriendType()) {
      QualType Ty = D->getFriendType()->getType();
      // FIXME Can this be other than elaborated?
      QualType NamedTy = cast<ElaboratedType>(Ty)->getNamedType();
      if (!NamedTy->isDependentType()) {
        if (const auto *RTy = dyn_cast<RecordType>(NamedTy))
          LT.add(RTy->getAsCXXRecordDecl());
        else if (const auto *SpecTy =
                     dyn_cast<TemplateSpecializationType>(NamedTy)) {
          LT.add(SpecTy->getAsCXXRecordDecl());
        }
      }
    }
    return true;
  }

  // Override default settings of base.
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
};

} // anonymous namespace

ASTImporterLookupTable::ASTImporterLookupTable(TranslationUnitDecl &TU) {
  Builder B(*this);
  B.TraverseDecl(&TU);
}

void ASTImporterLookupTable::add(DeclContext *DC, NamedDecl *ND) {
  DeclList &Decls = LookupTable[DC][ND->getDeclName()];
  // Inserts if and only if there is no element in the container equal to it.
  Decls.insert(ND);
}

void ASTImporterLookupTable::remove(DeclContext *DC, NamedDecl *ND) {
  DeclList &Decls = LookupTable[DC][ND->getDeclName()];
  bool EraseResult = Decls.remove(ND);
  (void)EraseResult;
  assert(EraseResult == true && "Trying to remove not contained Decl");
}

void ASTImporterLookupTable::add(NamedDecl *ND) {
  assert(ND);
  DeclContext *DC = ND->getDeclContext()->getPrimaryContext();
  add(DC, ND);
  DeclContext *ReDC = DC->getRedeclContext()->getPrimaryContext();
  if (DC != ReDC)
    add(ReDC, ND);
}

void ASTImporterLookupTable::remove(NamedDecl *ND) {
  assert(ND);
  DeclContext *DC = ND->getDeclContext()->getPrimaryContext();
  remove(DC, ND);
  DeclContext *ReDC = DC->getRedeclContext()->getPrimaryContext();
  if (DC != ReDC)
    remove(ReDC, ND);
}

ASTImporterLookupTable::LookupResult
ASTImporterLookupTable::lookup(DeclContext *DC, DeclarationName Name) const {
  auto DCI = LookupTable.find(DC->getPrimaryContext());
  if (DCI == LookupTable.end())
    return {};

  const auto &FoundNameMap = DCI->second;
  auto NamesI = FoundNameMap.find(Name);
  if (NamesI == FoundNameMap.end())
    return {};

  return NamesI->second;
}

void ASTImporterLookupTable::dump(DeclContext *DC) const {
  auto DCI = LookupTable.find(DC->getPrimaryContext());
  if (DCI == LookupTable.end())
    llvm::errs() << "empty\n";
  const auto &FoundNameMap = DCI->second;
  for (const auto &Entry : FoundNameMap) {
    DeclarationName Name = Entry.first;
    llvm::errs() << "==== Name: ";
    Name.dump();
    const DeclList& List = Entry.second;
    for (NamedDecl *ND : List) {
      ND->dump();
    }
  }
}

void ASTImporterLookupTable::dump() const {
  for (const auto &Entry : LookupTable) {
    DeclContext *DC = Entry.first;
    StringRef Primary = DC->getPrimaryContext() ? " primary" : "";
    llvm::errs() << "== DC:" << cast<Decl>(DC) << Primary << "\n";
    dump(DC);
  }
}

} // namespace clang
