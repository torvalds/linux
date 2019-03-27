//===--- ClangSACheckers.h - Registration functions for Checkers *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares the registation functions for the checkers defined in
// libclangStaticAnalyzerCheckers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_CLANGSACHECKERS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_CLANGSACHECKERS_H

#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"

namespace clang {

namespace ento {
class CheckerManager;
class CheckerRegistry;

#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI)                            \
  void register##CLASS(CheckerManager &mgr);
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS

} // end ento namespace

} // end clang namespace

#endif
