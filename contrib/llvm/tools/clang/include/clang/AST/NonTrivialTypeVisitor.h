//===-- NonTrivialTypeVisitor.h - Visitor for non-trivial Types *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the visitor classes that are used to traverse non-trivial
//  structs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_NON_TRIVIAL_TYPE_VISITOR_H
#define LLVM_CLANG_NON_TRIVIAL_TYPE_VISITOR_H

#include "clang/AST/Type.h"

namespace clang {

template <class Derived, class RetTy = void> struct DestructedTypeVisitor {
  template <class... Ts> RetTy visit(QualType FT, Ts &&... Args) {
    return asDerived().visitWithKind(FT.isDestructedType(), FT,
                                     std::forward<Ts>(Args)...);
  }

  template <class... Ts>
  RetTy visitWithKind(QualType::DestructionKind DK, QualType FT,
                      Ts &&... Args) {
    switch (DK) {
    case QualType::DK_objc_strong_lifetime:
      return asDerived().visitARCStrong(FT, std::forward<Ts>(Args)...);
    case QualType::DK_nontrivial_c_struct:
      return asDerived().visitStruct(FT, std::forward<Ts>(Args)...);
    case QualType::DK_none:
      return asDerived().visitTrivial(FT, std::forward<Ts>(Args)...);
    case QualType::DK_cxx_destructor:
      return asDerived().visitCXXDestructor(FT, std::forward<Ts>(Args)...);
    case QualType::DK_objc_weak_lifetime:
      return asDerived().visitARCWeak(FT, std::forward<Ts>(Args)...);
    }

    llvm_unreachable("unknown destruction kind");
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }
};

template <class Derived, class RetTy = void>
struct DefaultInitializedTypeVisitor {
  template <class... Ts> RetTy visit(QualType FT, Ts &&... Args) {
    return asDerived().visitWithKind(
        FT.isNonTrivialToPrimitiveDefaultInitialize(), FT,
        std::forward<Ts>(Args)...);
  }

  template <class... Ts>
  RetTy visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK,
                      QualType FT, Ts &&... Args) {
    switch (PDIK) {
    case QualType::PDIK_ARCStrong:
      return asDerived().visitARCStrong(FT, std::forward<Ts>(Args)...);
    case QualType::PDIK_ARCWeak:
      return asDerived().visitARCWeak(FT, std::forward<Ts>(Args)...);
    case QualType::PDIK_Struct:
      return asDerived().visitStruct(FT, std::forward<Ts>(Args)...);
    case QualType::PDIK_Trivial:
      return asDerived().visitTrivial(FT, std::forward<Ts>(Args)...);
    }

    llvm_unreachable("unknown default-initialize kind");
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }
};

template <class Derived, bool IsMove, class RetTy = void>
struct CopiedTypeVisitor {
  template <class... Ts> RetTy visit(QualType FT, Ts &&... Args) {
    QualType::PrimitiveCopyKind PCK =
        IsMove ? FT.isNonTrivialToPrimitiveDestructiveMove()
               : FT.isNonTrivialToPrimitiveCopy();
    return asDerived().visitWithKind(PCK, FT, std::forward<Ts>(Args)...);
  }

  template <class... Ts>
  RetTy visitWithKind(QualType::PrimitiveCopyKind PCK, QualType FT,
                      Ts &&... Args) {
    asDerived().preVisit(PCK, FT, std::forward<Ts>(Args)...);

    switch (PCK) {
    case QualType::PCK_ARCStrong:
      return asDerived().visitARCStrong(FT, std::forward<Ts>(Args)...);
    case QualType::PCK_ARCWeak:
      return asDerived().visitARCWeak(FT, std::forward<Ts>(Args)...);
    case QualType::PCK_Struct:
      return asDerived().visitStruct(FT, std::forward<Ts>(Args)...);
    case QualType::PCK_Trivial:
      return asDerived().visitTrivial(FT, std::forward<Ts>(Args)...);
    case QualType::PCK_VolatileTrivial:
      return asDerived().visitVolatileTrivial(FT, std::forward<Ts>(Args)...);
    }

    llvm_unreachable("unknown primitive copy kind");
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }
};

} // end namespace clang

#endif
