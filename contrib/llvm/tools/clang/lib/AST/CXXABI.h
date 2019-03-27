//===----- CXXABI.h - Interface to C++ ABIs ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for C++ AST support. Concrete
// subclasses of this implement AST support for specific C++ ABIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_AST_CXXABI_H
#define LLVM_CLANG_LIB_AST_CXXABI_H

#include "clang/AST/Type.h"

namespace clang {

class ASTContext;
class CXXConstructorDecl;
class DeclaratorDecl;
class Expr;
class MemberPointerType;
class MangleNumberingContext;

/// Implements C++ ABI-specific semantic analysis functions.
class CXXABI {
public:
  virtual ~CXXABI();

  struct MemberPointerInfo {
    uint64_t Width;
    unsigned Align;
    bool HasPadding;
  };

  /// Returns the width and alignment of a member pointer in bits, as well as
  /// whether it has padding.
  virtual MemberPointerInfo
  getMemberPointerInfo(const MemberPointerType *MPT) const = 0;

  /// Returns the default calling convention for C++ methods.
  virtual CallingConv getDefaultMethodCallConv(bool isVariadic) const = 0;

  /// Returns whether the given class is nearly empty, with just virtual
  /// pointers and no data except possibly virtual bases.
  virtual bool isNearlyEmpty(const CXXRecordDecl *RD) const = 0;

  /// Returns a new mangling number context for this C++ ABI.
  virtual std::unique_ptr<MangleNumberingContext>
  createMangleNumberingContext() const = 0;

  /// Adds a mapping from class to copy constructor for this C++ ABI.
  virtual void addCopyConstructorForExceptionObject(CXXRecordDecl *,
                                                    CXXConstructorDecl *) = 0;

  /// Retrieves the mapping from class to copy constructor for this C++ ABI.
  virtual const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *) = 0;

  virtual void addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                               TypedefNameDecl *DD) = 0;

  virtual TypedefNameDecl *
  getTypedefNameForUnnamedTagDecl(const TagDecl *TD) = 0;

  virtual void addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                              DeclaratorDecl *DD) = 0;

  virtual DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD) = 0;
};

/// Creates an instance of a C++ ABI class.
CXXABI *CreateItaniumCXXABI(ASTContext &Ctx);
CXXABI *CreateMicrosoftCXXABI(ASTContext &Ctx);
}

#endif
