//===- ASTContextAllocate.h - ASTContext allocate functions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares ASTContext allocation functions separate from the main
//  code in ASTContext.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONTEXTALLOCATE_H
#define LLVM_CLANG_AST_ASTCONTEXTALLOCATE_H

#include <cstddef>

namespace clang {

class ASTContext;

} // namespace clang

// Defined in ASTContext.h
void *operator new(size_t Bytes, const clang::ASTContext &C,
                   size_t Alignment = 8);
void *operator new[](size_t Bytes, const clang::ASTContext &C,
                     size_t Alignment = 8);

// It is good practice to pair new/delete operators.  Also, MSVC gives many
// warnings if a matching delete overload is not declared, even though the
// throw() spec guarantees it will not be implicitly called.
void operator delete(void *Ptr, const clang::ASTContext &C, size_t);
void operator delete[](void *Ptr, const clang::ASTContext &C, size_t);

#endif // LLVM_CLANG_AST_ASTCONTEXTALLOCATE_H
