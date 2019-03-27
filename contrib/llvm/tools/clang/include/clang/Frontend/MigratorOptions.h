//===--- MigratorOptions.h - MigratorOptions Options ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header contains the structures necessary for a front-end to specify
// various migration analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H
#define LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H

namespace clang {

class MigratorOptions {
public:
  unsigned NoNSAllocReallocError : 1;
  unsigned NoFinalizeRemoval : 1;
  MigratorOptions() {
    NoNSAllocReallocError = 0;
    NoFinalizeRemoval = 0;
  }
};

}
#endif
