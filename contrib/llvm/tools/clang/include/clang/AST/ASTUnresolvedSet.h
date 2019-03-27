//===- ASTUnresolvedSet.h - Unresolved sets of declarations -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides an UnresolvedSet-like class, whose contents are
//  allocated using the allocator associated with an ASTContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTUNRESOLVEDSET_H
#define LLVM_CLANG_AST_ASTUNRESOLVEDSET_H

#include "clang/AST/ASTVector.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/Specifiers.h"
#include <cassert>
#include <cstdint>

namespace clang {

class NamedDecl;

/// An UnresolvedSet-like class which uses the ASTContext's allocator.
class ASTUnresolvedSet {
  friend class LazyASTUnresolvedSet;

  struct DeclsTy : ASTVector<DeclAccessPair> {
    DeclsTy() = default;
    DeclsTy(ASTContext &C, unsigned N) : ASTVector<DeclAccessPair>(C, N) {}

    bool isLazy() const { return getTag(); }
    void setLazy(bool Lazy) { setTag(Lazy); }
  };

  DeclsTy Decls;

public:
  ASTUnresolvedSet() = default;
  ASTUnresolvedSet(ASTContext &C, unsigned N) : Decls(C, N) {}

  using iterator = UnresolvedSetIterator;
  using const_iterator = UnresolvedSetIterator;

  iterator begin() { return iterator(Decls.begin()); }
  iterator end() { return iterator(Decls.end()); }

  const_iterator begin() const { return const_iterator(Decls.begin()); }
  const_iterator end() const { return const_iterator(Decls.end()); }

  void addDecl(ASTContext &C, NamedDecl *D, AccessSpecifier AS) {
    Decls.push_back(DeclAccessPair::make(D, AS), C);
  }

  /// Replaces the given declaration with the new one, once.
  ///
  /// \return true if the set changed
  bool replace(const NamedDecl *Old, NamedDecl *New, AccessSpecifier AS) {
    for (DeclsTy::iterator I = Decls.begin(), E = Decls.end(); I != E; ++I) {
      if (I->getDecl() == Old) {
        I->set(New, AS);
        return true;
      }
    }
    return false;
  }

  void erase(unsigned I) { Decls[I] = Decls.pop_back_val(); }

  void clear() { Decls.clear(); }

  bool empty() const { return Decls.empty(); }
  unsigned size() const { return Decls.size(); }

  void reserve(ASTContext &C, unsigned N) {
    Decls.reserve(C, N);
  }

  void append(ASTContext &C, iterator I, iterator E) {
    Decls.append(C, I.I, E.I);
  }

  DeclAccessPair &operator[](unsigned I) { return Decls[I]; }
  const DeclAccessPair &operator[](unsigned I) const { return Decls[I]; }
};

/// An UnresolvedSet-like class that might not have been loaded from the
/// external AST source yet.
class LazyASTUnresolvedSet {
  mutable ASTUnresolvedSet Impl;

  void getFromExternalSource(ASTContext &C) const;

public:
  ASTUnresolvedSet &get(ASTContext &C) const {
    if (Impl.Decls.isLazy())
      getFromExternalSource(C);
    return Impl;
  }

  void reserve(ASTContext &C, unsigned N) { Impl.reserve(C, N); }

  void addLazyDecl(ASTContext &C, uintptr_t ID, AccessSpecifier AS) {
    assert(Impl.empty() || Impl.Decls.isLazy());
    Impl.Decls.setLazy(true);
    Impl.addDecl(C, reinterpret_cast<NamedDecl *>(ID << 2), AS);
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTUNRESOLVEDSET_H
