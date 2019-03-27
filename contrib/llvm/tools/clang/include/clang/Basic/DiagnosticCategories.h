//===- DiagnosticCategories.h - Diagnostic Categories Enumerators-*- C++ -*===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICCATEGORIES_H
#define LLVM_CLANG_BASIC_DIAGNOSTICCATEGORIES_H

namespace clang {
  namespace diag {
    enum {
#define GET_CATEGORY_TABLE
#define CATEGORY(X, ENUM) ENUM,
#include "clang/Basic/DiagnosticGroups.inc"
#undef CATEGORY
#undef GET_CATEGORY_TABLE
      DiagCat_NUM_CATEGORIES
    };
  }  // end namespace diag
}  // end namespace clang

#endif
