//===- Linkage.h - Linkage enumeration and utilities ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the Linkage enumeration and various utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_LINKAGE_H
#define LLVM_CLANG_BASIC_LINKAGE_H

#include <utility>

namespace clang {

/// Describes the different kinds of linkage
/// (C++ [basic.link], C99 6.2.2) that an entity may have.
enum Linkage : unsigned char {
  /// No linkage, which means that the entity is unique and
  /// can only be referred to from within its scope.
  NoLinkage = 0,

  /// Internal linkage, which indicates that the entity can
  /// be referred to from within the translation unit (but not other
  /// translation units).
  InternalLinkage,

  /// External linkage within a unique namespace.
  ///
  /// From the language perspective, these entities have external
  /// linkage. However, since they reside in an anonymous namespace,
  /// their names are unique to this translation unit, which is
  /// equivalent to having internal linkage from the code-generation
  /// point of view.
  UniqueExternalLinkage,

  /// No linkage according to the standard, but is visible from other
  /// translation units because of types defined in a inline function.
  VisibleNoLinkage,

  /// Internal linkage according to the Modules TS, but can be referred
  /// to from other translation units indirectly through inline functions and
  /// templates in the module interface.
  ModuleInternalLinkage,

  /// Module linkage, which indicates that the entity can be referred
  /// to from other translation units within the same module, and indirectly
  /// from arbitrary other translation units through inline functions and
  /// templates in the module interface.
  ModuleLinkage,

  /// External linkage, which indicates that the entity can
  /// be referred to from other translation units.
  ExternalLinkage
};

/// Describes the different kinds of language linkage
/// (C++ [dcl.link]) that an entity may have.
enum LanguageLinkage {
  CLanguageLinkage,
  CXXLanguageLinkage,
  NoLanguageLinkage
};

/// A more specific kind of linkage than enum Linkage.
///
/// This is relevant to CodeGen and AST file reading.
enum GVALinkage {
  GVA_Internal,
  GVA_AvailableExternally,
  GVA_DiscardableODR,
  GVA_StrongExternal,
  GVA_StrongODR
};

inline bool isDiscardableGVALinkage(GVALinkage L) {
  return L <= GVA_DiscardableODR;
}

inline bool isExternallyVisible(Linkage L) {
  return L >= VisibleNoLinkage;
}

inline Linkage getFormalLinkage(Linkage L) {
  switch (L) {
  case UniqueExternalLinkage:
    return ExternalLinkage;
  case VisibleNoLinkage:
    return NoLinkage;
  case ModuleInternalLinkage:
    return InternalLinkage;
  default:
    return L;
  }
}

inline bool isExternalFormalLinkage(Linkage L) {
  return getFormalLinkage(L) == ExternalLinkage;
}

/// Compute the minimum linkage given two linkages.
///
/// The linkage can be interpreted as a pair formed by the formal linkage and
/// a boolean for external visibility. This is just what getFormalLinkage and
/// isExternallyVisible return. We want the minimum of both components. The
/// Linkage enum is defined in an order that makes this simple, we just need
/// special cases for when VisibleNoLinkage would lose the visible bit and
/// become NoLinkage.
inline Linkage minLinkage(Linkage L1, Linkage L2) {
  if (L2 == VisibleNoLinkage)
    std::swap(L1, L2);
  if (L1 == VisibleNoLinkage) {
    if (L2 == InternalLinkage)
      return NoLinkage;
    if (L2 == UniqueExternalLinkage)
      return NoLinkage;
  }
  return L1 < L2 ? L1 : L2;
}

} // namespace clang

#endif // LLVM_CLANG_BASIC_LINKAGE_H
