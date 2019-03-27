//===-- VerifyDecl.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_VariableList_h_
#define lldb_VariableList_h_

#include "lldb/Core/ClangForward.h"

namespace lldb_private {
void VerifyDecl(clang::Decl *decl);
}

#endif
