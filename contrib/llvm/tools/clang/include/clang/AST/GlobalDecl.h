//===- GlobalDecl.h - Global declaration holder -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A GlobalDecl can hold either a regular variable/function or a C++ ctor/dtor
// together with its type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_GLOBALDECL_H
#define LLVM_CLANG_AST_GLOBALDECL_H

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/Basic/ABI.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/type_traits.h"
#include <cassert>

namespace clang {

/// GlobalDecl - represents a global declaration. This can either be a
/// CXXConstructorDecl and the constructor type (Base, Complete).
/// a CXXDestructorDecl and the destructor type (Base, Complete) or
/// a VarDecl, a FunctionDecl or a BlockDecl.
class GlobalDecl {
  llvm::PointerIntPair<const Decl *, 2> Value;
  unsigned MultiVersionIndex = 0;

  void Init(const Decl *D) {
    assert(!isa<CXXConstructorDecl>(D) && "Use other ctor with ctor decls!");
    assert(!isa<CXXDestructorDecl>(D) && "Use other ctor with dtor decls!");

    Value.setPointer(D);
  }

public:
  GlobalDecl() = default;
  GlobalDecl(const VarDecl *D) { Init(D);}
  GlobalDecl(const FunctionDecl *D, unsigned MVIndex = 0)
      : MultiVersionIndex(MVIndex) {
    Init(D);
  }
  GlobalDecl(const BlockDecl *D) { Init(D); }
  GlobalDecl(const CapturedDecl *D) { Init(D); }
  GlobalDecl(const ObjCMethodDecl *D) { Init(D); }
  GlobalDecl(const OMPDeclareReductionDecl *D) { Init(D); }
  GlobalDecl(const CXXConstructorDecl *D, CXXCtorType Type) : Value(D, Type) {}
  GlobalDecl(const CXXDestructorDecl *D, CXXDtorType Type) : Value(D, Type) {}

  GlobalDecl getCanonicalDecl() const {
    GlobalDecl CanonGD;
    CanonGD.Value.setPointer(Value.getPointer()->getCanonicalDecl());
    CanonGD.Value.setInt(Value.getInt());
    CanonGD.MultiVersionIndex = MultiVersionIndex;

    return CanonGD;
  }

  const Decl *getDecl() const { return Value.getPointer(); }

  CXXCtorType getCtorType() const {
    assert(isa<CXXConstructorDecl>(getDecl()) && "Decl is not a ctor!");
    return static_cast<CXXCtorType>(Value.getInt());
  }

  CXXDtorType getDtorType() const {
    assert(isa<CXXDestructorDecl>(getDecl()) && "Decl is not a dtor!");
    return static_cast<CXXDtorType>(Value.getInt());
  }

  unsigned getMultiVersionIndex() const {
    assert(isa<FunctionDecl>(getDecl()) &&
           !isa<CXXConstructorDecl>(getDecl()) &&
           !isa<CXXDestructorDecl>(getDecl()) &&
           "Decl is not a plain FunctionDecl!");
    return MultiVersionIndex;
  }

  friend bool operator==(const GlobalDecl &LHS, const GlobalDecl &RHS) {
    return LHS.Value == RHS.Value &&
           LHS.MultiVersionIndex == RHS.MultiVersionIndex;
  }

  void *getAsOpaquePtr() const { return Value.getOpaqueValue(); }

  static GlobalDecl getFromOpaquePtr(void *P) {
    GlobalDecl GD;
    GD.Value.setFromOpaqueValue(P);
    return GD;
  }

  GlobalDecl getWithDecl(const Decl *D) {
    GlobalDecl Result(*this);
    Result.Value.setPointer(D);
    return Result;
  }

  GlobalDecl getWithMultiVersionIndex(unsigned Index) {
    assert(isa<FunctionDecl>(getDecl()) &&
           !isa<CXXConstructorDecl>(getDecl()) &&
           !isa<CXXDestructorDecl>(getDecl()) &&
           "Decl is not a plain FunctionDecl!");
    GlobalDecl Result(*this);
    Result.MultiVersionIndex = Index;
    return Result;
  }
};

} // namespace clang

namespace llvm {

  template<> struct DenseMapInfo<clang::GlobalDecl> {
    static inline clang::GlobalDecl getEmptyKey() {
      return clang::GlobalDecl();
    }

    static inline clang::GlobalDecl getTombstoneKey() {
      return clang::GlobalDecl::
        getFromOpaquePtr(reinterpret_cast<void*>(-1));
    }

    static unsigned getHashValue(clang::GlobalDecl GD) {
      return DenseMapInfo<void*>::getHashValue(GD.getAsOpaquePtr());
    }

    static bool isEqual(clang::GlobalDecl LHS,
                        clang::GlobalDecl RHS) {
      return LHS == RHS;
    }
  };

  // GlobalDecl isn't *technically* a POD type. However, its copy constructor,
  // copy assignment operator, and destructor are all trivial.
  template <>
  struct isPodLike<clang::GlobalDecl> {
    static const bool value = true;
  };

} // namespace llvm

#endif // LLVM_CLANG_AST_GLOBALDECL_H
