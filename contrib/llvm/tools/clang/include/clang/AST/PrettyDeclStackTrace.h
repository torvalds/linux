//===- PrettyDeclStackTrace.h - Stack trace for decl processing -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an llvm::PrettyStackTraceEntry object for showing
// that a particular declaration was being processed when a crash
// occurred.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_PRETTYDECLSTACKTRACE_H
#define LLVM_CLANG_AST_PRETTYDECLSTACKTRACE_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/PrettyStackTrace.h"

namespace clang {

class ASTContext;
class Decl;
class SourceManager;

/// PrettyDeclStackTraceEntry - If a crash occurs in the parser while
/// parsing something related to a declaration, include that
/// declaration in the stack trace.
class PrettyDeclStackTraceEntry : public llvm::PrettyStackTraceEntry {
  ASTContext &Context;
  Decl *TheDecl;
  SourceLocation Loc;
  const char *Message;

public:
  PrettyDeclStackTraceEntry(ASTContext &Ctx, Decl *D, SourceLocation Loc,
                            const char *Msg)
    : Context(Ctx), TheDecl(D), Loc(Loc), Message(Msg) {}

  void print(raw_ostream &OS) const override;
};

}

#endif
