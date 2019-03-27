//===-- VerifyDecl.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/VerifyDecl.h"
#include "clang/AST/DeclBase.h"

void lldb_private::VerifyDecl(clang::Decl *decl) { decl->getAccess(); }
