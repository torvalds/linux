//===--- ASTDiagnostic.h - Diagnostics for the AST library ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTDIAGNOSTIC_H
#define LLVM_CLANG_AST_ASTDIAGNOSTIC_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticAST.h"

namespace clang {
  /// DiagnosticsEngine argument formatting function for diagnostics that
  /// involve AST nodes.
  ///
  /// This function formats diagnostic arguments for various AST nodes,
  /// including types, declaration names, nested name specifiers, and
  /// declaration contexts, into strings that can be printed as part of
  /// diagnostics. It is meant to be used as the argument to
  /// \c DiagnosticsEngine::SetArgToStringFn(), where the cookie is an \c
  /// ASTContext pointer.
  void FormatASTNodeDiagnosticArgument(
      DiagnosticsEngine::ArgumentKind Kind,
      intptr_t Val,
      StringRef Modifier,
      StringRef Argument,
      ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
      SmallVectorImpl<char> &Output,
      void *Cookie,
      ArrayRef<intptr_t> QualTypeVals);
}  // end namespace clang

#endif
