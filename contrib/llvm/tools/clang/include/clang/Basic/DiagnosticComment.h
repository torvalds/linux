//===--- DiagnosticComment.h - Diagnostics for the AST library --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICCOMMENT_H
#define LLVM_CLANG_BASIC_DIAGNOSTICCOMMENT_H

#include "clang/Basic/Diagnostic.h"

namespace clang {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, SFINAE, NOWERROR,      \
             SHOWINSYSHEADER, CATEGORY)                                        \
  ENUM,
#define COMMENTSTART
#include "clang/Basic/DiagnosticCommentKinds.inc"
#undef DIAG
  NUM_BUILTIN_COMMENT_DIAGNOSTICS
};
} // end namespace diag
} // end namespace clang

#endif // LLVM_CLANG_BASIC_DIAGNOSTICCOMMENT_H
