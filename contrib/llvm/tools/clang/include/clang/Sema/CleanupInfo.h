//===--- CleanupInfo.cpp - Cleanup Control in Sema ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements a set of operations on whether generating an
//  ExprWithCleanups in a full expression.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_CLEANUP_INFO_H
#define LLVM_CLANG_SEMA_CLEANUP_INFO_H

namespace clang {

class CleanupInfo {
  bool ExprNeedsCleanups = false;
  bool CleanupsHaveSideEffects = false;

public:
  bool exprNeedsCleanups() const { return ExprNeedsCleanups; }

  bool cleanupsHaveSideEffects() const { return CleanupsHaveSideEffects; }

  void setExprNeedsCleanups(bool SideEffects) {
    ExprNeedsCleanups = true;
    CleanupsHaveSideEffects |= SideEffects;
  }

  void reset() {
    ExprNeedsCleanups = false;
    CleanupsHaveSideEffects = false;
  }

  void mergeFrom(CleanupInfo Rhs) {
    ExprNeedsCleanups |= Rhs.ExprNeedsCleanups;
    CleanupsHaveSideEffects |= Rhs.CleanupsHaveSideEffects;
  }
};

} // end namespace clang

#endif
