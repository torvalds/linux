//===-- ClangUtil.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// A collection of helper methods and data structures for manipulating clang
// types and decls.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_CLANGUTIL_H
#define LLDB_SYMBOL_CLANGUTIL_H

#include "clang/AST/Type.h"

#include "lldb/Symbol/CompilerType.h"

namespace clang {
class TagDecl;
}

namespace lldb_private {
struct ClangUtil {
  static bool IsClangType(const CompilerType &ct);

  static clang::QualType GetQualType(const CompilerType &ct);

  static clang::QualType GetCanonicalQualType(const CompilerType &ct);

  static CompilerType RemoveFastQualifiers(const CompilerType &ct);

  static clang::TagDecl *GetAsTagDecl(const CompilerType &type);
};
}

#endif
