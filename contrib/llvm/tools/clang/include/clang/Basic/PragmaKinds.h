//===--- PragmaKinds.h - #pragma comment() kinds  ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_PRAGMA_KINDS_H
#define LLVM_CLANG_BASIC_PRAGMA_KINDS_H

namespace clang {

enum PragmaMSCommentKind {
  PCK_Unknown,
  PCK_Linker,   // #pragma comment(linker, ...)
  PCK_Lib,      // #pragma comment(lib, ...)
  PCK_Compiler, // #pragma comment(compiler, ...)
  PCK_ExeStr,   // #pragma comment(exestr, ...)
  PCK_User      // #pragma comment(user, ...)
};

enum PragmaMSStructKind {
  PMSST_OFF, // #pragms ms_struct off
  PMSST_ON   // #pragms ms_struct on
};

}

#endif
