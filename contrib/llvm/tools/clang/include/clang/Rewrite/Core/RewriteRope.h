//===- RewriteRope.h - Rope specialized for rewriter ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the RewriteRope class, which is a powerful string class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_REWRITEROPE_H
#define LLVM_CLANG_REWRITE_CORE_REWRITEROPE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <utility>

namespace clang {

  //===--------------------------------------------------------------------===//
  // RopeRefCountString Class
  //===--------------------------------------------------------------------===//

  /// RopeRefCountString - This struct is allocated with 'new char[]' from the
  /// heap, and represents a reference counted chunk of string data.  When its
  /// ref count drops to zero, it is delete[]'d.  This is primarily managed
  /// through the RopePiece class below.
  struct RopeRefCountString {
    unsigned RefCount;
    char Data[1];  //  Variable sized.

    void Retain() { ++RefCount; }

    void Release() {
      assert(RefCount > 0 && "Reference count is already zero.");
      if (--RefCount == 0)
        delete [] (char*)this;
    }
  };

  //===--------------------------------------------------------------------===//
  // RopePiece Class
  //===--------------------------------------------------------------------===//

  /// RopePiece - This class represents a view into a RopeRefCountString object.
  /// This allows references to string data to be efficiently chopped up and
  /// moved around without having to push around the string data itself.
  ///
  /// For example, we could have a 1M RopePiece and want to insert something
  /// into the middle of it.  To do this, we split it into two RopePiece objects
  /// that both refer to the same underlying RopeRefCountString (just with
  /// different offsets) which is a nice constant time operation.
  struct RopePiece {
    llvm::IntrusiveRefCntPtr<RopeRefCountString> StrData;
    unsigned StartOffs = 0;
    unsigned EndOffs = 0;

    RopePiece() = default;
    RopePiece(llvm::IntrusiveRefCntPtr<RopeRefCountString> Str, unsigned Start,
              unsigned End)
        : StrData(std::move(Str)), StartOffs(Start), EndOffs(End) {}

    const char &operator[](unsigned Offset) const {
      return StrData->Data[Offset+StartOffs];
    }
    char &operator[](unsigned Offset) {
      return StrData->Data[Offset+StartOffs];
    }

    unsigned size() const { return EndOffs-StartOffs; }
  };

  //===--------------------------------------------------------------------===//
  // RopePieceBTreeIterator Class
  //===--------------------------------------------------------------------===//

  /// RopePieceBTreeIterator - This class provides read-only forward iteration
  /// over bytes that are in a RopePieceBTree.  This first iterates over bytes
  /// in a RopePiece, then iterates over RopePiece's in a RopePieceBTreeLeaf,
  /// then iterates over RopePieceBTreeLeaf's in a RopePieceBTree.
  class RopePieceBTreeIterator :
      public std::iterator<std::forward_iterator_tag, const char, ptrdiff_t> {
    /// CurNode - The current B+Tree node that we are inspecting.
    const void /*RopePieceBTreeLeaf*/ *CurNode = nullptr;

    /// CurPiece - The current RopePiece in the B+Tree node that we're
    /// inspecting.
    const RopePiece *CurPiece = nullptr;

    /// CurChar - The current byte in the RopePiece we are pointing to.
    unsigned CurChar = 0;

  public:
    RopePieceBTreeIterator() = default;
    RopePieceBTreeIterator(const void /*RopePieceBTreeNode*/ *N);

    char operator*() const {
      return (*CurPiece)[CurChar];
    }

    bool operator==(const RopePieceBTreeIterator &RHS) const {
      return CurPiece == RHS.CurPiece && CurChar == RHS.CurChar;
    }
    bool operator!=(const RopePieceBTreeIterator &RHS) const {
      return !operator==(RHS);
    }

    RopePieceBTreeIterator& operator++() {   // Preincrement
      if (CurChar+1 < CurPiece->size())
        ++CurChar;
      else
        MoveToNextPiece();
      return *this;
    }

    RopePieceBTreeIterator operator++(int) { // Postincrement
      RopePieceBTreeIterator tmp = *this; ++*this; return tmp;
    }

    llvm::StringRef piece() const {
      return llvm::StringRef(&(*CurPiece)[0], CurPiece->size());
    }

    void MoveToNextPiece();
  };

  //===--------------------------------------------------------------------===//
  // RopePieceBTree Class
  //===--------------------------------------------------------------------===//

  class RopePieceBTree {
    void /*RopePieceBTreeNode*/ *Root;

  public:
    RopePieceBTree();
    RopePieceBTree(const RopePieceBTree &RHS);
    RopePieceBTree &operator=(const RopePieceBTree &) = delete;
    ~RopePieceBTree();

    using iterator = RopePieceBTreeIterator;

    iterator begin() const { return iterator(Root); }
    iterator end() const { return iterator(); }
    unsigned size() const;
    unsigned empty() const { return size() == 0; }

    void clear();

    void insert(unsigned Offset, const RopePiece &R);

    void erase(unsigned Offset, unsigned NumBytes);
  };

  //===--------------------------------------------------------------------===//
  // RewriteRope Class
  //===--------------------------------------------------------------------===//

/// RewriteRope - A powerful string class.  This class supports extremely
/// efficient insertions and deletions into the middle of it, even for
/// ridiculously long strings.
class RewriteRope {
  RopePieceBTree Chunks;

  /// We allocate space for string data out of a buffer of size AllocChunkSize.
  /// This keeps track of how much space is left.
  llvm::IntrusiveRefCntPtr<RopeRefCountString> AllocBuffer;
  enum { AllocChunkSize = 4080 };
  unsigned AllocOffs = AllocChunkSize;

public:
  RewriteRope() = default;
  RewriteRope(const RewriteRope &RHS) : Chunks(RHS.Chunks) {}

  using iterator = RopePieceBTree::iterator;
  using const_iterator = RopePieceBTree::iterator;

  iterator begin() const { return Chunks.begin(); }
  iterator end() const { return Chunks.end(); }
  unsigned size() const { return Chunks.size(); }

  void clear() {
    Chunks.clear();
  }

  void assign(const char *Start, const char *End) {
    clear();
    if (Start != End)
      Chunks.insert(0, MakeRopeString(Start, End));
  }

  void insert(unsigned Offset, const char *Start, const char *End) {
    assert(Offset <= size() && "Invalid position to insert!");
    if (Start == End) return;
    Chunks.insert(Offset, MakeRopeString(Start, End));
  }

  void erase(unsigned Offset, unsigned NumBytes) {
    assert(Offset+NumBytes <= size() && "Invalid region to erase!");
    if (NumBytes == 0) return;
    Chunks.erase(Offset, NumBytes);
  }

private:
  RopePiece MakeRopeString(const char *Start, const char *End);
};

} // namespace clang

#endif // LLVM_CLANG_REWRITE_CORE_REWRITEROPE_H
