//===--- ObjCMethodList.h - A singly linked list of methods -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ObjCMethodList, a singly-linked list of methods.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_OBJCMETHODLIST_H
#define LLVM_CLANG_SEMA_OBJCMETHODLIST_H

#include "clang/AST/DeclObjC.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang {

class ObjCMethodDecl;

/// a linked list of methods with the same selector name but different
/// signatures.
struct ObjCMethodList {
  // NOTE: If you add any members to this struct, make sure to serialize them.
  /// If there is more than one decl with this signature.
  llvm::PointerIntPair<ObjCMethodDecl *, 1> MethodAndHasMoreThanOneDecl;
  /// The next list object and 2 bits for extra info.
  llvm::PointerIntPair<ObjCMethodList *, 2> NextAndExtraBits;

  ObjCMethodList() { }
  ObjCMethodList(ObjCMethodDecl *M)
      : MethodAndHasMoreThanOneDecl(M, 0) {}
  ObjCMethodList(const ObjCMethodList &L)
      : MethodAndHasMoreThanOneDecl(L.MethodAndHasMoreThanOneDecl),
        NextAndExtraBits(L.NextAndExtraBits) {}

  ObjCMethodList *getNext() const { return NextAndExtraBits.getPointer(); }
  unsigned getBits() const { return NextAndExtraBits.getInt(); }
  void setNext(ObjCMethodList *L) { NextAndExtraBits.setPointer(L); }
  void setBits(unsigned B) { NextAndExtraBits.setInt(B); }

  ObjCMethodDecl *getMethod() const {
    return MethodAndHasMoreThanOneDecl.getPointer();
  }
  void setMethod(ObjCMethodDecl *M) {
    return MethodAndHasMoreThanOneDecl.setPointer(M);
  }

  bool hasMoreThanOneDecl() const {
    return MethodAndHasMoreThanOneDecl.getInt();
  }
  void setHasMoreThanOneDecl(bool B) {
    return MethodAndHasMoreThanOneDecl.setInt(B);
  }
};

}

#endif
