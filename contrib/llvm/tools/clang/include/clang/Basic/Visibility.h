//===--- Visibility.h - Visibility enumeration and utilities ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::Visibility enumeration and various utility
/// functions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_VISIBILITY_H
#define LLVM_CLANG_BASIC_VISIBILITY_H

#include "clang/Basic/Linkage.h"
#include <cassert>
#include <cstdint>

namespace clang {

/// Describes the different kinds of visibility that a declaration
/// may have.
///
/// Visibility determines how a declaration interacts with the dynamic
/// linker.  It may also affect whether the symbol can be found by runtime
/// symbol lookup APIs.
///
/// Visibility is not described in any language standard and
/// (nonetheless) sometimes has odd behavior.  Not all platforms
/// support all visibility kinds.
enum Visibility {
  /// Objects with "hidden" visibility are not seen by the dynamic
  /// linker.
  HiddenVisibility,

  /// Objects with "protected" visibility are seen by the dynamic
  /// linker but always dynamically resolve to an object within this
  /// shared object.
  ProtectedVisibility,

  /// Objects with "default" visibility are seen by the dynamic linker
  /// and act like normal objects.
  DefaultVisibility
};

inline Visibility minVisibility(Visibility L, Visibility R) {
  return L < R ? L : R;
}

class LinkageInfo {
  uint8_t linkage_    : 3;
  uint8_t visibility_ : 2;
  uint8_t explicit_   : 1;

  void setVisibility(Visibility V, bool E) { visibility_ = V; explicit_ = E; }
public:
  LinkageInfo() : linkage_(ExternalLinkage), visibility_(DefaultVisibility),
                  explicit_(false) {}
  LinkageInfo(Linkage L, Visibility V, bool E)
    : linkage_(L), visibility_(V), explicit_(E) {
    assert(getLinkage() == L && getVisibility() == V &&
           isVisibilityExplicit() == E && "Enum truncated!");
  }

  static LinkageInfo external() {
    return LinkageInfo();
  }
  static LinkageInfo internal() {
    return LinkageInfo(InternalLinkage, DefaultVisibility, false);
  }
  static LinkageInfo uniqueExternal() {
    return LinkageInfo(UniqueExternalLinkage, DefaultVisibility, false);
  }
  static LinkageInfo none() {
    return LinkageInfo(NoLinkage, DefaultVisibility, false);
  }
  static LinkageInfo visible_none() {
    return LinkageInfo(VisibleNoLinkage, DefaultVisibility, false);
  }

  Linkage getLinkage() const { return (Linkage)linkage_; }
  Visibility getVisibility() const { return (Visibility)visibility_; }
  bool isVisibilityExplicit() const { return explicit_; }

  void setLinkage(Linkage L) { linkage_ = L; }

  void mergeLinkage(Linkage L) {
    setLinkage(minLinkage(getLinkage(), L));
  }
  void mergeLinkage(LinkageInfo other) {
    mergeLinkage(other.getLinkage());
  }

  void mergeExternalVisibility(Linkage L) {
    Linkage ThisL = getLinkage();
    if (!isExternallyVisible(L)) {
      if (ThisL == VisibleNoLinkage)
        ThisL = NoLinkage;
      else if (ThisL == ExternalLinkage)
        ThisL = UniqueExternalLinkage;
    }
    setLinkage(ThisL);
  }
  void mergeExternalVisibility(LinkageInfo Other) {
    mergeExternalVisibility(Other.getLinkage());
  }

  /// Merge in the visibility 'newVis'.
  void mergeVisibility(Visibility newVis, bool newExplicit) {
    Visibility oldVis = getVisibility();

    // Never increase visibility.
    if (oldVis < newVis)
      return;

    // If the new visibility is the same as the old and the new
    // visibility isn't explicit, we have nothing to add.
    if (oldVis == newVis && !newExplicit)
      return;

    // Otherwise, we're either decreasing visibility or making our
    // existing visibility explicit.
    setVisibility(newVis, newExplicit);
  }
  void mergeVisibility(LinkageInfo other) {
    mergeVisibility(other.getVisibility(), other.isVisibilityExplicit());
  }

  /// Merge both linkage and visibility.
  void merge(LinkageInfo other) {
    mergeLinkage(other);
    mergeVisibility(other);
  }

  /// Merge linkage and conditionally merge visibility.
  void mergeMaybeWithVisibility(LinkageInfo other, bool withVis) {
    mergeLinkage(other);
    if (withVis) mergeVisibility(other);
  }
};
}

#endif // LLVM_CLANG_BASIC_VISIBILITY_H
