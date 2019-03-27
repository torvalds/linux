//===---- CodeCompleteOptions.h - Code Completion Options -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_CODECOMPLETEOPTIONS_H
#define LLVM_CLANG_SEMA_CODECOMPLETEOPTIONS_H

namespace clang {

/// Options controlling the behavior of code completion.
class CodeCompleteOptions {
public:
  /// Show macros in code completion results.
  unsigned IncludeMacros : 1;

  /// Show code patterns in code completion results.
  unsigned IncludeCodePatterns : 1;

  /// Show top-level decls in code completion results.
  unsigned IncludeGlobals : 1;

  /// Show decls in namespace (including the global namespace) in code
  /// completion results. If this is 0, `IncludeGlobals` will be ignored.
  ///
  /// Currently, this only works when completing qualified IDs (i.e.
  /// `Sema::CodeCompleteQualifiedId`).
  /// FIXME: consider supporting more completion cases with this option.
  unsigned IncludeNamespaceLevelDecls : 1;

  /// Show brief documentation comments in code completion results.
  unsigned IncludeBriefComments : 1;

  /// Hint whether to load data from the external AST to provide full results.
  /// If false, namespace-level declarations and macros from the preamble may be
  /// omitted.
  unsigned LoadExternal : 1;

  /// Include results after corrections (small fix-its), e.g. change '.' to '->'
  /// on member access, etc.
  unsigned IncludeFixIts : 1;

  CodeCompleteOptions()
      : IncludeMacros(0), IncludeCodePatterns(0), IncludeGlobals(1),
        IncludeNamespaceLevelDecls(1), IncludeBriefComments(0),
        LoadExternal(1), IncludeFixIts(0) {}
};

} // namespace clang

#endif

