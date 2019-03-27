//===--- LocInfoType.h - Parsed Type with Location Information---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the LocInfoType class, which holds a type and its
// source-location information.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SEMA_LOCINFOTYPE_H
#define LLVM_CLANG_SEMA_LOCINFOTYPE_H

#include "clang/AST/Type.h"

namespace clang {

class TypeSourceInfo;

/// Holds a QualType and a TypeSourceInfo* that came out of a declarator
/// parsing.
///
/// LocInfoType is a "transient" type, only needed for passing to/from Parser
/// and Sema, when we want to preserve type source info for a parsed type.
/// It will not participate in the type system semantics in any way.
class LocInfoType : public Type {
  enum {
    // The last number that can fit in Type's TC.
    // Avoids conflict with an existing Type class.
    LocInfo = Type::TypeLast + 1
  };

  TypeSourceInfo *DeclInfo;

  LocInfoType(QualType ty, TypeSourceInfo *TInfo)
      : Type((TypeClass)LocInfo, ty, ty->isDependentType(),
             ty->isInstantiationDependentType(), ty->isVariablyModifiedType(),
             ty->containsUnexpandedParameterPack()),
        DeclInfo(TInfo) {
    assert(getTypeClass() == (TypeClass)LocInfo && "LocInfo didn't fit in TC?");
  }
  friend class Sema;

public:
  QualType getType() const { return getCanonicalTypeInternal(); }
  TypeSourceInfo *getTypeSourceInfo() const { return DeclInfo; }

  void getAsStringInternal(std::string &Str,
                           const PrintingPolicy &Policy) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == (TypeClass)LocInfo;
  }
};

} // end namespace clang

#endif // LLVM_CLANG_SEMA_LOCINFOTYPE_H
