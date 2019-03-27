//== DynamicTypeInfo.h - Runtime type information ----------------*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEINFO_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEINFO_H

#include "clang/AST/Type.h"

namespace clang {
namespace ento {

/// Stores the currently inferred strictest bound on the runtime type
/// of a region in a given state along the analysis path.
class DynamicTypeInfo {
private:
  QualType T;
  bool CanBeASubClass;

public:

  DynamicTypeInfo() : T(QualType()) {}
  DynamicTypeInfo(QualType WithType, bool CanBeSub = true)
    : T(WithType), CanBeASubClass(CanBeSub) {}

  /// Return false if no dynamic type info is available.
  bool isValid() const { return !T.isNull(); }

  /// Returns the currently inferred upper bound on the runtime type.
  QualType getType() const { return T; }

  /// Returns false if the type information is precise (the type T is
  /// the only type in the lattice), true otherwise.
  bool canBeASubClass() const { return CanBeASubClass; }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(T);
    ID.AddInteger((unsigned)CanBeASubClass);
  }
  bool operator==(const DynamicTypeInfo &X) const {
    return T == X.T && CanBeASubClass == X.CanBeASubClass;
  }
};

} // end ento
} // end clang

#endif
