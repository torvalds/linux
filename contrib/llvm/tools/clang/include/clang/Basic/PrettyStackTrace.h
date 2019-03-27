//===- clang/Basic/PrettyStackTrace.h - Pretty Crash Handling --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the PrettyStackTraceEntry class, which is used to make
/// crashes give more contextual information about what the program was doing
/// when it crashed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_PRETTYSTACKTRACE_H
#define LLVM_CLANG_BASIC_PRETTYSTACKTRACE_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/PrettyStackTrace.h"

namespace clang {

  /// If a crash happens while one of these objects are live, the message
  /// is printed out along with the specified source location.
  class PrettyStackTraceLoc : public llvm::PrettyStackTraceEntry {
    SourceManager &SM;
    SourceLocation Loc;
    const char *Message;
  public:
    PrettyStackTraceLoc(SourceManager &sm, SourceLocation L, const char *Msg)
      : SM(sm), Loc(L), Message(Msg) {}
    void print(raw_ostream &OS) const override;
  };
}

#endif
