//===--- VarBypassDetector.cpp - Bypass jumps detector ------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains VarBypassDetector class, which is used to detect
// local variable declarations which can be bypassed by jumps.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_VARBYPASSDETECTOR_H
#define LLVM_CLANG_LIB_CODEGEN_VARBYPASSDETECTOR_H

#include "clang/AST/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class Decl;
class Stmt;
class VarDecl;

namespace CodeGen {

/// The class detects jumps which bypass local variables declaration:
///    goto L;
///    int a;
///  L:
///
/// This is simplified version of JumpScopeChecker. Primary differences:
///  * Detects only jumps into the scope local variables.
///  * Does not detect jumps out of the scope of local variables.
///  * Not limited to variables with initializers, JumpScopeChecker is limited.
class VarBypassDetector {
  // Scope information. Contains a parent scope and related variable
  // declaration.
  llvm::SmallVector<std::pair<unsigned, const VarDecl *>, 48> Scopes;
  // List of jumps with scopes.
  llvm::SmallVector<std::pair<const Stmt *, unsigned>, 16> FromScopes;
  // Lookup map to find scope for destinations.
  llvm::DenseMap<const Stmt *, unsigned> ToScopes;
  // Set of variables which were bypassed by some jump.
  llvm::DenseSet<const VarDecl *> Bypasses;
  // If true assume that all variables are being bypassed.
  bool AlwaysBypassed = false;

public:
  void Init(const Stmt *Body);

  /// Returns true if the variable declaration was by bypassed by any goto or
  /// switch statement.
  bool IsBypassed(const VarDecl *D) const {
    return AlwaysBypassed || Bypasses.find(D) != Bypasses.end();
  }

private:
  bool BuildScopeInformation(const Decl *D, unsigned &ParentScope);
  bool BuildScopeInformation(const Stmt *S, unsigned &origParentScope);
  void Detect();
  void Detect(unsigned From, unsigned To);
};
}
}

#endif
