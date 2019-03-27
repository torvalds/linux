//===- DeclContextInternals.h - DeclContext Representation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the data structures used in the implementation
//  of DeclContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLCONTEXTINTERNALS_H
#define LLVM_CLANG_AST_DECLCONTEXTINTERNALS_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cassert>

namespace clang {

class DependentDiagnostic;

/// An array of decls optimized for the common case of only containing
/// one entry.
struct StoredDeclsList {
  /// When in vector form, this is what the Data pointer points to.
  using DeclsTy = SmallVector<NamedDecl *, 4>;

  /// A collection of declarations, with a flag to indicate if we have
  /// further external declarations.
  using DeclsAndHasExternalTy = llvm::PointerIntPair<DeclsTy *, 1, bool>;

  /// The stored data, which will be either a pointer to a NamedDecl,
  /// or a pointer to a vector with a flag to indicate if there are further
  /// external declarations.
  llvm::PointerUnion<NamedDecl *, DeclsAndHasExternalTy> Data;

public:
  StoredDeclsList() = default;

  StoredDeclsList(StoredDeclsList &&RHS) : Data(RHS.Data) {
    RHS.Data = (NamedDecl *)nullptr;
  }

  ~StoredDeclsList() {
    // If this is a vector-form, free the vector.
    if (DeclsTy *Vector = getAsVector())
      delete Vector;
  }

  StoredDeclsList &operator=(StoredDeclsList &&RHS) {
    if (DeclsTy *Vector = getAsVector())
      delete Vector;
    Data = RHS.Data;
    RHS.Data = (NamedDecl *)nullptr;
    return *this;
  }

  bool isNull() const { return Data.isNull(); }

  NamedDecl *getAsDecl() const {
    return Data.dyn_cast<NamedDecl *>();
  }

  DeclsAndHasExternalTy getAsVectorAndHasExternal() const {
    return Data.dyn_cast<DeclsAndHasExternalTy>();
  }

  DeclsTy *getAsVector() const {
    return getAsVectorAndHasExternal().getPointer();
  }

  bool hasExternalDecls() const {
    return getAsVectorAndHasExternal().getInt();
  }

  void setHasExternalDecls() {
    if (DeclsTy *Vec = getAsVector())
      Data = DeclsAndHasExternalTy(Vec, true);
    else {
      DeclsTy *VT = new DeclsTy();
      if (NamedDecl *OldD = getAsDecl())
        VT->push_back(OldD);
      Data = DeclsAndHasExternalTy(VT, true);
    }
  }

  void setOnlyValue(NamedDecl *ND) {
    assert(!getAsVector() && "Not inline");
    Data = ND;
    // Make sure that Data is a plain NamedDecl* so we can use its address
    // at getLookupResult.
    assert(*(NamedDecl **)&Data == ND &&
           "PointerUnion mangles the NamedDecl pointer!");
  }

  void remove(NamedDecl *D) {
    assert(!isNull() && "removing from empty list");
    if (NamedDecl *Singleton = getAsDecl()) {
      assert(Singleton == D && "list is different singleton");
      (void)Singleton;
      Data = (NamedDecl *)nullptr;
      return;
    }

    DeclsTy &Vec = *getAsVector();
    DeclsTy::iterator I = std::find(Vec.begin(), Vec.end(), D);
    assert(I != Vec.end() && "list does not contain decl");
    Vec.erase(I);

    assert(std::find(Vec.begin(), Vec.end(), D)
             == Vec.end() && "list still contains decl");
  }

  /// Remove any declarations which were imported from an external
  /// AST source.
  void removeExternalDecls() {
    if (isNull()) {
      // Nothing to do.
    } else if (NamedDecl *Singleton = getAsDecl()) {
      if (Singleton->isFromASTFile())
        *this = StoredDeclsList();
    } else {
      DeclsTy &Vec = *getAsVector();
      Vec.erase(std::remove_if(Vec.begin(), Vec.end(),
                               [](Decl *D) { return D->isFromASTFile(); }),
                Vec.end());
      // Don't have any external decls any more.
      Data = DeclsAndHasExternalTy(&Vec, false);
    }
  }

  /// getLookupResult - Return an array of all the decls that this list
  /// represents.
  DeclContext::lookup_result getLookupResult() {
    if (isNull())
      return DeclContext::lookup_result();

    // If we have a single NamedDecl, return it.
    if (NamedDecl *ND = getAsDecl()) {
      assert(!isNull() && "Empty list isn't allowed");

      // Data is a raw pointer to a NamedDecl*, return it.
      return DeclContext::lookup_result(ND);
    }

    assert(getAsVector() && "Must have a vector at this point");
    DeclsTy &Vector = *getAsVector();

    // Otherwise, we have a range result.
    return DeclContext::lookup_result(Vector);
  }

  /// HandleRedeclaration - If this is a redeclaration of an existing decl,
  /// replace the old one with D and return true.  Otherwise return false.
  bool HandleRedeclaration(NamedDecl *D, bool IsKnownNewer) {
    // Most decls only have one entry in their list, special case it.
    if (NamedDecl *OldD = getAsDecl()) {
      if (!D->declarationReplaces(OldD, IsKnownNewer))
        return false;
      setOnlyValue(D);
      return true;
    }

    // Determine if this declaration is actually a redeclaration.
    DeclsTy &Vec = *getAsVector();
    for (DeclsTy::iterator OD = Vec.begin(), ODEnd = Vec.end();
         OD != ODEnd; ++OD) {
      NamedDecl *OldD = *OD;
      if (D->declarationReplaces(OldD, IsKnownNewer)) {
        *OD = D;
        return true;
      }
    }

    return false;
  }

  /// AddSubsequentDecl - This is called on the second and later decl when it is
  /// not a redeclaration to merge it into the appropriate place in our list.
  void AddSubsequentDecl(NamedDecl *D) {
    assert(!isNull() && "don't AddSubsequentDecl when we have no decls");

    // If this is the second decl added to the list, convert this to vector
    // form.
    if (NamedDecl *OldD = getAsDecl()) {
      DeclsTy *VT = new DeclsTy();
      VT->push_back(OldD);
      Data = DeclsAndHasExternalTy(VT, false);
    }

    DeclsTy &Vec = *getAsVector();

    // Using directives end up in a special entry which contains only
    // other using directives, so all this logic is wasted for them.
    // But avoiding the logic wastes time in the far-more-common case
    // that we're *not* adding a new using directive.

    // Tag declarations always go at the end of the list so that an
    // iterator which points at the first tag will start a span of
    // decls that only contains tags.
    if (D->hasTagIdentifierNamespace())
      Vec.push_back(D);

    // Resolved using declarations go at the front of the list so that
    // they won't show up in other lookup results.  Unresolved using
    // declarations (which are always in IDNS_Using | IDNS_Ordinary)
    // follow that so that the using declarations will be contiguous.
    else if (D->getIdentifierNamespace() & Decl::IDNS_Using) {
      DeclsTy::iterator I = Vec.begin();
      if (D->getIdentifierNamespace() != Decl::IDNS_Using) {
        while (I != Vec.end() &&
               (*I)->getIdentifierNamespace() == Decl::IDNS_Using)
          ++I;
      }
      Vec.insert(I, D);

    // All other declarations go at the end of the list, but before any
    // tag declarations.  But we can be clever about tag declarations
    // because there can only ever be one in a scope.
    } else if (!Vec.empty() && Vec.back()->hasTagIdentifierNamespace()) {
      NamedDecl *TagD = Vec.back();
      Vec.back() = D;
      Vec.push_back(TagD);
    } else
      Vec.push_back(D);
  }
};

class StoredDeclsMap
    : public llvm::SmallDenseMap<DeclarationName, StoredDeclsList, 4> {
public:
  static void DestroyAll(StoredDeclsMap *Map, bool Dependent);

private:
  friend class ASTContext; // walks the chain deleting these
  friend class DeclContext;

  llvm::PointerIntPair<StoredDeclsMap*, 1> Previous;
};

class DependentStoredDeclsMap : public StoredDeclsMap {
public:
  DependentStoredDeclsMap() = default;

private:
  friend class DeclContext; // iterates over diagnostics
  friend class DependentDiagnostic;

  DependentDiagnostic *FirstDiagnostic = nullptr;
};

} // namespace clang

#endif // LLVM_CLANG_AST_DECLCONTEXTINTERNALS_H
