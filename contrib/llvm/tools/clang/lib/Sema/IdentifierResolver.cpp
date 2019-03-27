//===- IdentifierResolver.cpp - Lexical Scope Name lookup -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the IdentifierResolver class, which is used for lexical
// scoped lookup, based on declaration names.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/IdentifierResolver.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Scope.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace clang;

//===----------------------------------------------------------------------===//
// IdDeclInfoMap class
//===----------------------------------------------------------------------===//

/// IdDeclInfoMap - Associates IdDeclInfos with declaration names.
/// Allocates 'pools' (vectors of IdDeclInfos) to avoid allocating each
/// individual IdDeclInfo to heap.
class IdentifierResolver::IdDeclInfoMap {
  static const unsigned int POOL_SIZE = 512;

  /// We use our own linked-list implementation because it is sadly
  /// impossible to add something to a pre-C++0x STL container without
  /// a completely unnecessary copy.
  struct IdDeclInfoPool {
    IdDeclInfoPool *Next;
    IdDeclInfo Pool[POOL_SIZE];

    IdDeclInfoPool(IdDeclInfoPool *Next) : Next(Next) {}
  };

  IdDeclInfoPool *CurPool = nullptr;
  unsigned int CurIndex = POOL_SIZE;

public:
  IdDeclInfoMap() = default;

  ~IdDeclInfoMap() {
    IdDeclInfoPool *Cur = CurPool;
    while (IdDeclInfoPool *P = Cur) {
      Cur = Cur->Next;
      delete P;
    }
  }

  /// Returns the IdDeclInfo associated to the DeclarationName.
  /// It creates a new IdDeclInfo if one was not created before for this id.
  IdDeclInfo &operator[](DeclarationName Name);
};

//===----------------------------------------------------------------------===//
// IdDeclInfo Implementation
//===----------------------------------------------------------------------===//

/// RemoveDecl - Remove the decl from the scope chain.
/// The decl must already be part of the decl chain.
void IdentifierResolver::IdDeclInfo::RemoveDecl(NamedDecl *D) {
  for (DeclsTy::iterator I = Decls.end(); I != Decls.begin(); --I) {
    if (D == *(I-1)) {
      Decls.erase(I-1);
      return;
    }
  }

  llvm_unreachable("Didn't find this decl on its identifier's chain!");
}

//===----------------------------------------------------------------------===//
// IdentifierResolver Implementation
//===----------------------------------------------------------------------===//

IdentifierResolver::IdentifierResolver(Preprocessor &PP)
    : LangOpt(PP.getLangOpts()), PP(PP), IdDeclInfos(new IdDeclInfoMap) {}

IdentifierResolver::~IdentifierResolver() {
  delete IdDeclInfos;
}

/// isDeclInScope - If 'Ctx' is a function/method, isDeclInScope returns true
/// if 'D' is in Scope 'S', otherwise 'S' is ignored and isDeclInScope returns
/// true if 'D' belongs to the given declaration context.
bool IdentifierResolver::isDeclInScope(Decl *D, DeclContext *Ctx, Scope *S,
                                       bool AllowInlineNamespace) const {
  Ctx = Ctx->getRedeclContext();

  if (Ctx->isFunctionOrMethod() || (S && S->isFunctionPrototypeScope())) {
    // Ignore the scopes associated within transparent declaration contexts.
    while (S->getEntity() && S->getEntity()->isTransparentContext())
      S = S->getParent();

    if (S->isDeclScope(D))
      return true;
    if (LangOpt.CPlusPlus) {
      // C++ 3.3.2p3:
      // The name declared in a catch exception-declaration is local to the
      // handler and shall not be redeclared in the outermost block of the
      // handler.
      // C++ 3.3.2p4:
      // Names declared in the for-init-statement, and in the condition of if,
      // while, for, and switch statements are local to the if, while, for, or
      // switch statement (including the controlled statement), and shall not be
      // redeclared in a subsequent condition of that statement nor in the
      // outermost block (or, for the if statement, any of the outermost blocks)
      // of the controlled statement.
      //
      assert(S->getParent() && "No TUScope?");
      if (S->getParent()->getFlags() & Scope::ControlScope) {
        S = S->getParent();
        if (S->isDeclScope(D))
          return true;
      }
      if (S->getFlags() & Scope::FnTryCatchScope)
        return S->getParent()->isDeclScope(D);
    }
    return false;
  }

  // FIXME: If D is a local extern declaration, this check doesn't make sense;
  // we should be checking its lexical context instead in that case, because
  // that is its scope.
  DeclContext *DCtx = D->getDeclContext()->getRedeclContext();
  return AllowInlineNamespace ? Ctx->InEnclosingNamespaceSetOf(DCtx)
                              : Ctx->Equals(DCtx);
}

/// AddDecl - Link the decl to its shadowed decl chain.
void IdentifierResolver::AddDecl(NamedDecl *D) {
  DeclarationName Name = D->getDeclName();
  if (IdentifierInfo *II = Name.getAsIdentifierInfo())
    updatingIdentifier(*II);

  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    Name.setFETokenInfo(D);
    return;
  }

  IdDeclInfo *IDI;

  if (isDeclPtr(Ptr)) {
    Name.setFETokenInfo(nullptr);
    IDI = &(*IdDeclInfos)[Name];
    NamedDecl *PrevD = static_cast<NamedDecl*>(Ptr);
    IDI->AddDecl(PrevD);
  } else
    IDI = toIdDeclInfo(Ptr);

  IDI->AddDecl(D);
}

void IdentifierResolver::InsertDeclAfter(iterator Pos, NamedDecl *D) {
  DeclarationName Name = D->getDeclName();
  if (IdentifierInfo *II = Name.getAsIdentifierInfo())
    updatingIdentifier(*II);

  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    AddDecl(D);
    return;
  }

  if (isDeclPtr(Ptr)) {
    // We only have a single declaration: insert before or after it,
    // as appropriate.
    if (Pos == iterator()) {
      // Add the new declaration before the existing declaration.
      NamedDecl *PrevD = static_cast<NamedDecl*>(Ptr);
      RemoveDecl(PrevD);
      AddDecl(D);
      AddDecl(PrevD);
    } else {
      // Add new declaration after the existing declaration.
      AddDecl(D);
    }

    return;
  }

  // General case: insert the declaration at the appropriate point in the
  // list, which already has at least two elements.
  IdDeclInfo *IDI = toIdDeclInfo(Ptr);
  if (Pos.isIterator()) {
    IDI->InsertDecl(Pos.getIterator() + 1, D);
  } else
    IDI->InsertDecl(IDI->decls_begin(), D);
}

/// RemoveDecl - Unlink the decl from its shadowed decl chain.
/// The decl must already be part of the decl chain.
void IdentifierResolver::RemoveDecl(NamedDecl *D) {
  assert(D && "null param passed");
  DeclarationName Name = D->getDeclName();
  if (IdentifierInfo *II = Name.getAsIdentifierInfo())
    updatingIdentifier(*II);

  void *Ptr = Name.getFETokenInfo();

  assert(Ptr && "Didn't find this decl on its identifier's chain!");

  if (isDeclPtr(Ptr)) {
    assert(D == Ptr && "Didn't find this decl on its identifier's chain!");
    Name.setFETokenInfo(nullptr);
    return;
  }

  return toIdDeclInfo(Ptr)->RemoveDecl(D);
}

/// begin - Returns an iterator for decls with name 'Name'.
IdentifierResolver::iterator
IdentifierResolver::begin(DeclarationName Name) {
  if (IdentifierInfo *II = Name.getAsIdentifierInfo())
    readingIdentifier(*II);

  void *Ptr = Name.getFETokenInfo();
  if (!Ptr) return end();

  if (isDeclPtr(Ptr))
    return iterator(static_cast<NamedDecl*>(Ptr));

  IdDeclInfo *IDI = toIdDeclInfo(Ptr);

  IdDeclInfo::DeclsTy::iterator I = IDI->decls_end();
  if (I != IDI->decls_begin())
    return iterator(I-1);
  // No decls found.
  return end();
}

namespace {

enum DeclMatchKind {
  DMK_Different,
  DMK_Replace,
  DMK_Ignore
};

} // namespace

/// Compare two declarations to see whether they are different or,
/// if they are the same, whether the new declaration should replace the
/// existing declaration.
static DeclMatchKind compareDeclarations(NamedDecl *Existing, NamedDecl *New) {
  // If the declarations are identical, ignore the new one.
  if (Existing == New)
    return DMK_Ignore;

  // If the declarations have different kinds, they're obviously different.
  if (Existing->getKind() != New->getKind())
    return DMK_Different;

  // If the declarations are redeclarations of each other, keep the newest one.
  if (Existing->getCanonicalDecl() == New->getCanonicalDecl()) {
    // If we're adding an imported declaration, don't replace another imported
    // declaration.
    if (Existing->isFromASTFile() && New->isFromASTFile())
      return DMK_Different;

    // If either of these is the most recent declaration, use it.
    Decl *MostRecent = Existing->getMostRecentDecl();
    if (Existing == MostRecent)
      return DMK_Ignore;

    if (New == MostRecent)
      return DMK_Replace;

    // If the existing declaration is somewhere in the previous declaration
    // chain of the new declaration, then prefer the new declaration.
    for (auto RD : New->redecls()) {
      if (RD == Existing)
        return DMK_Replace;

      if (RD->isCanonicalDecl())
        break;
    }

    return DMK_Ignore;
  }

  return DMK_Different;
}

bool IdentifierResolver::tryAddTopLevelDecl(NamedDecl *D, DeclarationName Name){
  if (IdentifierInfo *II = Name.getAsIdentifierInfo())
    readingIdentifier(*II);

  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    Name.setFETokenInfo(D);
    return true;
  }

  IdDeclInfo *IDI;

  if (isDeclPtr(Ptr)) {
    NamedDecl *PrevD = static_cast<NamedDecl*>(Ptr);

    switch (compareDeclarations(PrevD, D)) {
    case DMK_Different:
      break;

    case DMK_Ignore:
      return false;

    case DMK_Replace:
      Name.setFETokenInfo(D);
      return true;
    }

    Name.setFETokenInfo(nullptr);
    IDI = &(*IdDeclInfos)[Name];

    // If the existing declaration is not visible in translation unit scope,
    // then add the new top-level declaration first.
    if (!PrevD->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      IDI->AddDecl(D);
      IDI->AddDecl(PrevD);
    } else {
      IDI->AddDecl(PrevD);
      IDI->AddDecl(D);
    }
    return true;
  }

  IDI = toIdDeclInfo(Ptr);

  // See whether this declaration is identical to any existing declarations.
  // If not, find the right place to insert it.
  for (IdDeclInfo::DeclsTy::iterator I = IDI->decls_begin(),
                                  IEnd = IDI->decls_end();
       I != IEnd; ++I) {

    switch (compareDeclarations(*I, D)) {
    case DMK_Different:
      break;

    case DMK_Ignore:
      return false;

    case DMK_Replace:
      *I = D;
      return true;
    }

    if (!(*I)->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      // We've found a declaration that is not visible from the translation
      // unit (it's in an inner scope). Insert our declaration here.
      IDI->InsertDecl(I, D);
      return true;
    }
  }

  // Add the declaration to the end.
  IDI->AddDecl(D);
  return true;
}

void IdentifierResolver::readingIdentifier(IdentifierInfo &II) {
  if (II.isOutOfDate())
    PP.getExternalSource()->updateOutOfDateIdentifier(II);
}

void IdentifierResolver::updatingIdentifier(IdentifierInfo &II) {
  if (II.isOutOfDate())
    PP.getExternalSource()->updateOutOfDateIdentifier(II);

  if (II.isFromAST())
    II.setFETokenInfoChangedSinceDeserialization();
}

//===----------------------------------------------------------------------===//
// IdDeclInfoMap Implementation
//===----------------------------------------------------------------------===//

/// Returns the IdDeclInfo associated to the DeclarationName.
/// It creates a new IdDeclInfo if one was not created before for this id.
IdentifierResolver::IdDeclInfo &
IdentifierResolver::IdDeclInfoMap::operator[](DeclarationName Name) {
  void *Ptr = Name.getFETokenInfo();

  if (Ptr) return *toIdDeclInfo(Ptr);

  if (CurIndex == POOL_SIZE) {
    CurPool = new IdDeclInfoPool(CurPool);
    CurIndex = 0;
  }
  IdDeclInfo *IDI = &CurPool->Pool[CurIndex];
  Name.setFETokenInfo(reinterpret_cast<void*>(
                              reinterpret_cast<uintptr_t>(IDI) | 0x1)
                                                                     );
  ++CurIndex;
  return *IDI;
}

void IdentifierResolver::iterator::incrementSlowCase() {
  NamedDecl *D = **this;
  void *InfoPtr = D->getDeclName().getFETokenInfo();
  assert(!isDeclPtr(InfoPtr) && "Decl with wrong id ?");
  IdDeclInfo *Info = toIdDeclInfo(InfoPtr);

  BaseIter I = getIterator();
  if (I != Info->decls_begin())
    *this = iterator(I-1);
  else // No more decls.
    *this = iterator();
}
