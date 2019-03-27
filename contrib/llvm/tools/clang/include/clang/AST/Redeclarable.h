//===- Redeclarable.h - Base for Decls that can be redeclared --*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Redeclarable interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_REDECLARABLE_H
#define LLVM_CLANG_AST_REDECLARABLE_H

#include "clang/AST/ExternalASTSource.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace clang {

class ASTContext;
class Decl;

// Some notes on redeclarables:
//
//  - Every redeclarable is on a circular linked list.
//
//  - Every decl has a pointer to the first element of the chain _and_ a
//    DeclLink that may point to one of 3 possible states:
//      - the "previous" (temporal) element in the chain
//      - the "latest" (temporal) element in the chain
//      - the "uninitialized-latest" value (when newly-constructed)
//
//  - The first element is also often called the canonical element. Every
//    element has a pointer to it so that "getCanonical" can be fast.
//
//  - Most links in the chain point to previous, except the link out of
//    the first; it points to latest.
//
//  - Elements are called "first", "previous", "latest" or
//    "most-recent" when referring to temporal order: order of addition
//    to the chain.
//
//  - It's easiest to just ignore the implementation of DeclLink when making
//    sense of the redeclaration chain.
//
//  - There's also a "definition" link for several types of
//    redeclarable, where only one definition should exist at any given
//    time (and the defn pointer is stored in the decl's "data" which
//    is copied to every element on the chain when it's changed).
//
//    Here is some ASCII art:
//
//      "first"                                     "latest"
//      "canonical"                                 "most recent"
//      +------------+         first                +--------------+
//      |            | <--------------------------- |              |
//      |            |                              |              |
//      |            |                              |              |
//      |            |       +--------------+       |              |
//      |            | first |              |       |              |
//      |            | <---- |              |       |              |
//      |            |       |              |       |              |
//      | @class A   |  link | @interface A |  link | @class A     |
//      | seen first | <---- | seen second  | <---- | seen third   |
//      |            |       |              |       |              |
//      +------------+       +--------------+       +--------------+
//      | data       | defn  | data         |  defn | data         |
//      |            | ----> |              | <---- |              |
//      +------------+       +--------------+       +--------------+
//        |                     |     ^                  ^
//        |                     |defn |                  |
//        | link                +-----+                  |
//        +-->-------------------------------------------+

/// Provides common interface for the Decls that can be redeclared.
template<typename decl_type>
class Redeclarable {
protected:
  class DeclLink {
    /// A pointer to a known latest declaration, either statically known or
    /// generationally updated as decls are added by an external source.
    using KnownLatest =
        LazyGenerationalUpdatePtr<const Decl *, Decl *,
                                  &ExternalASTSource::CompleteRedeclChain>;

    /// We store a pointer to the ASTContext in the UninitializedLatest
    /// pointer, but to avoid circular type dependencies when we steal the low
    /// bits of this pointer, we use a raw void* here.
    using UninitializedLatest = const void *;

    using Previous = Decl *;

    /// A pointer to either an uninitialized latest declaration (where either
    /// we've not yet set the previous decl or there isn't one), or to a known
    /// previous declaration.
    using NotKnownLatest = llvm::PointerUnion<Previous, UninitializedLatest>;

    mutable llvm::PointerUnion<NotKnownLatest, KnownLatest> Link;

  public:
    enum PreviousTag { PreviousLink };
    enum LatestTag { LatestLink };

    DeclLink(LatestTag, const ASTContext &Ctx)
        : Link(NotKnownLatest(reinterpret_cast<UninitializedLatest>(&Ctx))) {}
    DeclLink(PreviousTag, decl_type *D) : Link(NotKnownLatest(Previous(D))) {}

    bool isFirst() const {
      return Link.is<KnownLatest>() ||
             // FIXME: 'template' is required on the next line due to an
             // apparent clang bug.
             Link.get<NotKnownLatest>().template is<UninitializedLatest>();
    }

    decl_type *getPrevious(const decl_type *D) const {
      if (Link.is<NotKnownLatest>()) {
        NotKnownLatest NKL = Link.get<NotKnownLatest>();
        if (NKL.is<Previous>())
          return static_cast<decl_type*>(NKL.get<Previous>());

        // Allocate the generational 'most recent' cache now, if needed.
        Link = KnownLatest(*reinterpret_cast<const ASTContext *>(
                               NKL.get<UninitializedLatest>()),
                           const_cast<decl_type *>(D));
      }

      return static_cast<decl_type*>(Link.get<KnownLatest>().get(D));
    }

    void setPrevious(decl_type *D) {
      assert(!isFirst() && "decl became non-canonical unexpectedly");
      Link = Previous(D);
    }

    void setLatest(decl_type *D) {
      assert(isFirst() && "decl became canonical unexpectedly");
      if (Link.is<NotKnownLatest>()) {
        NotKnownLatest NKL = Link.get<NotKnownLatest>();
        Link = KnownLatest(*reinterpret_cast<const ASTContext *>(
                               NKL.get<UninitializedLatest>()),
                           D);
      } else {
        auto Latest = Link.get<KnownLatest>();
        Latest.set(D);
        Link = Latest;
      }
    }

    void markIncomplete() { Link.get<KnownLatest>().markIncomplete(); }

    Decl *getLatestNotUpdated() const {
      assert(isFirst() && "expected a canonical decl");
      if (Link.is<NotKnownLatest>())
        return nullptr;
      return Link.get<KnownLatest>().getNotUpdated();
    }
  };

  static DeclLink PreviousDeclLink(decl_type *D) {
    return DeclLink(DeclLink::PreviousLink, D);
  }

  static DeclLink LatestDeclLink(const ASTContext &Ctx) {
    return DeclLink(DeclLink::LatestLink, Ctx);
  }

  /// Points to the next redeclaration in the chain.
  ///
  /// If isFirst() is false, this is a link to the previous declaration
  /// of this same Decl. If isFirst() is true, this is the first
  /// declaration and Link points to the latest declaration. For example:
  ///
  ///  #1 int f(int x, int y = 1); // <pointer to #3, true>
  ///  #2 int f(int x = 0, int y); // <pointer to #1, false>
  ///  #3 int f(int x, int y) { return x + y; } // <pointer to #2, false>
  ///
  /// If there is only one declaration, it is <pointer to self, true>
  DeclLink RedeclLink;

  decl_type *First;

  decl_type *getNextRedeclaration() const {
    return RedeclLink.getPrevious(static_cast<const decl_type *>(this));
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  Redeclarable(const ASTContext &Ctx)
      : RedeclLink(LatestDeclLink(Ctx)),
        First(static_cast<decl_type *>(this)) {}

  /// Return the previous declaration of this declaration or NULL if this
  /// is the first declaration.
  decl_type *getPreviousDecl() {
    if (!RedeclLink.isFirst())
      return getNextRedeclaration();
    return nullptr;
  }
  const decl_type *getPreviousDecl() const {
    return const_cast<decl_type *>(
                 static_cast<const decl_type*>(this))->getPreviousDecl();
  }

  /// Return the first declaration of this declaration or itself if this
  /// is the only declaration.
  decl_type *getFirstDecl() { return First; }

  /// Return the first declaration of this declaration or itself if this
  /// is the only declaration.
  const decl_type *getFirstDecl() const { return First; }

  /// True if this is the first declaration in its redeclaration chain.
  bool isFirstDecl() const { return RedeclLink.isFirst(); }

  /// Returns the most recent (re)declaration of this declaration.
  decl_type *getMostRecentDecl() {
    return getFirstDecl()->getNextRedeclaration();
  }

  /// Returns the most recent (re)declaration of this declaration.
  const decl_type *getMostRecentDecl() const {
    return getFirstDecl()->getNextRedeclaration();
  }

  /// Set the previous declaration. If PrevDecl is NULL, set this as the
  /// first and only declaration.
  void setPreviousDecl(decl_type *PrevDecl);

  /// Iterates through all the redeclarations of the same decl.
  class redecl_iterator {
    /// Current - The current declaration.
    decl_type *Current = nullptr;
    decl_type *Starter;
    bool PassedFirst = false;

  public:
    using value_type = decl_type *;
    using reference = decl_type *;
    using pointer = decl_type *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    redecl_iterator() = default;
    explicit redecl_iterator(decl_type *C) : Current(C), Starter(C) {}

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    redecl_iterator& operator++() {
      assert(Current && "Advancing while iterator has reached end");
      // Sanity check to avoid infinite loop on invalid redecl chain.
      if (Current->isFirstDecl()) {
        if (PassedFirst) {
          assert(0 && "Passed first decl twice, invalid redecl chain!");
          Current = nullptr;
          return *this;
        }
        PassedFirst = true;
      }

      // Get either previous decl or latest decl.
      decl_type *Next = Current->getNextRedeclaration();
      Current = (Next != Starter) ? Next : nullptr;
      return *this;
    }

    redecl_iterator operator++(int) {
      redecl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(redecl_iterator x, redecl_iterator y) {
      return x.Current == y.Current;
    }
    friend bool operator!=(redecl_iterator x, redecl_iterator y) {
      return x.Current != y.Current;
    }
  };

  using redecl_range = llvm::iterator_range<redecl_iterator>;

  /// Returns an iterator range for all the redeclarations of the same
  /// decl. It will iterate at least once (when this decl is the only one).
  redecl_range redecls() const {
    return redecl_range(redecl_iterator(const_cast<decl_type *>(
                            static_cast<const decl_type *>(this))),
                        redecl_iterator());
  }

  redecl_iterator redecls_begin() const { return redecls().begin(); }
  redecl_iterator redecls_end() const { return redecls().end(); }
};

/// Get the primary declaration for a declaration from an AST file. That
/// will be the first-loaded declaration.
Decl *getPrimaryMergedDecl(Decl *D);

/// Provides common interface for the Decls that cannot be redeclared,
/// but can be merged if the same declaration is brought in from multiple
/// modules.
template<typename decl_type>
class Mergeable {
public:
  Mergeable() = default;

  /// Return the first declaration of this declaration or itself if this
  /// is the only declaration.
  decl_type *getFirstDecl() {
    auto *D = static_cast<decl_type *>(this);
    if (!D->isFromASTFile())
      return D;
    return cast<decl_type>(getPrimaryMergedDecl(const_cast<decl_type*>(D)));
  }

  /// Return the first declaration of this declaration or itself if this
  /// is the only declaration.
  const decl_type *getFirstDecl() const {
    const auto *D = static_cast<const decl_type *>(this);
    if (!D->isFromASTFile())
      return D;
    return cast<decl_type>(getPrimaryMergedDecl(const_cast<decl_type*>(D)));
  }

  /// Returns true if this is the first declaration.
  bool isFirstDecl() const { return getFirstDecl() == this; }
};

/// A wrapper class around a pointer that always points to its canonical
/// declaration.
///
/// CanonicalDeclPtr<decl_type> behaves just like decl_type*, except we call
/// decl_type::getCanonicalDecl() on construction.
///
/// This is useful for hashtables that you want to be keyed on a declaration's
/// canonical decl -- if you use CanonicalDeclPtr as the key, you don't need to
/// remember to call getCanonicalDecl() everywhere.
template <typename decl_type> class CanonicalDeclPtr {
public:
  CanonicalDeclPtr() = default;
  CanonicalDeclPtr(decl_type *Ptr)
      : Ptr(Ptr ? Ptr->getCanonicalDecl() : nullptr) {}
  CanonicalDeclPtr(const CanonicalDeclPtr &) = default;
  CanonicalDeclPtr &operator=(const CanonicalDeclPtr &) = default;

  operator decl_type *() { return Ptr; }
  operator const decl_type *() const { return Ptr; }

  decl_type *operator->() { return Ptr; }
  const decl_type *operator->() const { return Ptr; }

  decl_type &operator*() { return *Ptr; }
  const decl_type &operator*() const { return *Ptr; }

private:
  friend struct llvm::DenseMapInfo<CanonicalDeclPtr<decl_type>>;

  decl_type *Ptr = nullptr;
};

} // namespace clang

namespace llvm {

template <typename decl_type>
struct DenseMapInfo<clang::CanonicalDeclPtr<decl_type>> {
  using CanonicalDeclPtr = clang::CanonicalDeclPtr<decl_type>;
  using BaseInfo = DenseMapInfo<decl_type *>;

  static CanonicalDeclPtr getEmptyKey() {
    // Construct our CanonicalDeclPtr this way because the regular constructor
    // would dereference P.Ptr, which is not allowed.
    CanonicalDeclPtr P;
    P.Ptr = BaseInfo::getEmptyKey();
    return P;
  }

  static CanonicalDeclPtr getTombstoneKey() {
    CanonicalDeclPtr P;
    P.Ptr = BaseInfo::getTombstoneKey();
    return P;
  }

  static unsigned getHashValue(const CanonicalDeclPtr &P) {
    return BaseInfo::getHashValue(P);
  }

  static bool isEqual(const CanonicalDeclPtr &LHS,
                      const CanonicalDeclPtr &RHS) {
    return BaseInfo::isEqual(LHS, RHS);
  }
};

} // namespace llvm

#endif // LLVM_CLANG_AST_REDECLARABLE_H
