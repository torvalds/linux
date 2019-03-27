//===----- Linkage.h - Linkage calculation-related utilities ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides AST-internal utilities for linkage and visibility
// calculation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_AST_LINKAGE_H
#define LLVM_CLANG_LIB_AST_LINKAGE_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang {
/// Kinds of LV computation.  The linkage side of the computation is
/// always the same, but different things can change how visibility is
/// computed.
struct LVComputationKind {
  /// The kind of entity whose visibility is ultimately being computed;
  /// visibility computations for types and non-types follow different rules.
  unsigned ExplicitKind : 1;
  /// Whether explicit visibility attributes should be ignored. When set,
  /// visibility may only be restricted by the visibility of template arguments.
  unsigned IgnoreExplicitVisibility : 1;
  /// Whether all visibility should be ignored. When set, we're only interested
  /// in computing linkage.
  unsigned IgnoreAllVisibility : 1;

  enum { NumLVComputationKindBits = 3 };

  explicit LVComputationKind(NamedDecl::ExplicitVisibilityKind EK)
      : ExplicitKind(EK), IgnoreExplicitVisibility(false),
        IgnoreAllVisibility(false) {}

  NamedDecl::ExplicitVisibilityKind getExplicitVisibilityKind() const {
    return static_cast<NamedDecl::ExplicitVisibilityKind>(ExplicitKind);
  }

  bool isTypeVisibility() const {
    return getExplicitVisibilityKind() == NamedDecl::VisibilityForType;
  }
  bool isValueVisibility() const {
    return getExplicitVisibilityKind() == NamedDecl::VisibilityForValue;
  }

  /// Do an LV computation when we only care about the linkage.
  static LVComputationKind forLinkageOnly() {
    LVComputationKind Result(NamedDecl::VisibilityForValue);
    Result.IgnoreExplicitVisibility = true;
    Result.IgnoreAllVisibility = true;
    return Result;
  }

  unsigned toBits() {
    unsigned Bits = 0;
    Bits = (Bits << 1) | ExplicitKind;
    Bits = (Bits << 1) | IgnoreExplicitVisibility;
    Bits = (Bits << 1) | IgnoreAllVisibility;
    return Bits;
  }
};

class LinkageComputer {
  // We have a cache for repeated linkage/visibility computations. This saves us
  // from exponential behavior in heavily templated code, such as:
  //
  // template <typename T, typename V> struct {};
  // using A = int;
  // using B = Foo<A, A>;
  // using C = Foo<B, B>;
  // using D = Foo<C, C>;
  //
  // The integer represents an LVComputationKind.
  using QueryType =
      llvm::PointerIntPair<const NamedDecl *,
                           LVComputationKind::NumLVComputationKindBits>;
  llvm::SmallDenseMap<QueryType, LinkageInfo, 8> CachedLinkageInfo;

  static QueryType makeCacheKey(const NamedDecl *ND, LVComputationKind Kind) {
    return QueryType(ND, Kind.toBits());
  }

  llvm::Optional<LinkageInfo> lookup(const NamedDecl *ND,
                                     LVComputationKind Kind) const {
    auto Iter = CachedLinkageInfo.find(makeCacheKey(ND, Kind));
    if (Iter == CachedLinkageInfo.end())
      return None;
    return Iter->second;
  }

  void cache(const NamedDecl *ND, LVComputationKind Kind, LinkageInfo Info) {
    CachedLinkageInfo[makeCacheKey(ND, Kind)] = Info;
  }

  LinkageInfo getLVForTemplateArgumentList(ArrayRef<TemplateArgument> Args,
                                           LVComputationKind computation);

  LinkageInfo getLVForTemplateArgumentList(const TemplateArgumentList &TArgs,
                                           LVComputationKind computation);

  void mergeTemplateLV(LinkageInfo &LV, const FunctionDecl *fn,
                       const FunctionTemplateSpecializationInfo *specInfo,
                       LVComputationKind computation);

  void mergeTemplateLV(LinkageInfo &LV,
                       const ClassTemplateSpecializationDecl *spec,
                       LVComputationKind computation);

  void mergeTemplateLV(LinkageInfo &LV,
                       const VarTemplateSpecializationDecl *spec,
                       LVComputationKind computation);

  LinkageInfo getLVForNamespaceScopeDecl(const NamedDecl *D,
                                         LVComputationKind computation,
                                         bool IgnoreVarTypeLinkage);

  LinkageInfo getLVForClassMember(const NamedDecl *D,
                                  LVComputationKind computation,
                                  bool IgnoreVarTypeLinkage);

  LinkageInfo getLVForClosure(const DeclContext *DC, Decl *ContextDecl,
                              LVComputationKind computation);

  LinkageInfo getLVForLocalDecl(const NamedDecl *D,
                                LVComputationKind computation);

  LinkageInfo getLVForType(const Type &T, LVComputationKind computation);

  LinkageInfo getLVForTemplateParameterList(const TemplateParameterList *Params,
                                            LVComputationKind computation);

public:
  LinkageInfo computeLVForDecl(const NamedDecl *D,
                               LVComputationKind computation,
                               bool IgnoreVarTypeLinkage = false);

  LinkageInfo getLVForDecl(const NamedDecl *D, LVComputationKind computation);

  LinkageInfo computeTypeLinkageInfo(const Type *T);
  LinkageInfo computeTypeLinkageInfo(QualType T) {
    return computeTypeLinkageInfo(T.getTypePtr());
  }

  LinkageInfo getDeclLinkageAndVisibility(const NamedDecl *D);

  LinkageInfo getTypeLinkageAndVisibility(const Type *T);
  LinkageInfo getTypeLinkageAndVisibility(QualType T) {
    return getTypeLinkageAndVisibility(T.getTypePtr());
  }
};
} // namespace clang

#endif
