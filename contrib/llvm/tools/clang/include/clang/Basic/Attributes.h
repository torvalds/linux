//===--- Attributes.h - Attributes header -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ATTRIBUTES_H
#define LLVM_CLANG_BASIC_ATTRIBUTES_H

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"

namespace clang {

class IdentifierInfo;

enum class AttrSyntax {
  /// Is the identifier known as a GNU-style attribute?
  GNU,
  /// Is the identifier known as a __declspec-style attribute?
  Declspec,
  /// Is the identifier known as a [] Microsoft-style attribute?
  Microsoft,
  // Is the identifier known as a C++-style attribute?
  CXX,
  // Is the identifier known as a C-style attribute?
  C,
  // Is the identifier known as a pragma attribute?
  Pragma
};

/// Return the version number associated with the attribute if we
/// recognize and implement the attribute specified by the given information.
int hasAttribute(AttrSyntax Syntax, const IdentifierInfo *Scope,
                 const IdentifierInfo *Attr, const TargetInfo &Target,
                 const LangOptions &LangOpts);

} // end namespace clang

#endif // LLVM_CLANG_BASIC_ATTRIBUTES_H
