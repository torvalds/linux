//===--- Designator.h - Initialization Designator ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces used to represent designators (a la
// C99 designated initializers) during parsing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_DESIGNATOR_H
#define LLVM_CLANG_SEMA_DESIGNATOR_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class Expr;
class IdentifierInfo;
class Sema;

/// Designator - A designator in a C99 designated initializer.
///
/// This class is a discriminated union which holds the various
/// different sorts of designators possible.  A Designation is an array of
/// these.  An example of a designator are things like this:
///     [8] .field [47]        // C99 designation: 3 designators
///     [8 ... 47]  field:     // GNU extensions: 2 designators
/// These occur in initializers, e.g.:
///  int a[10] = {2, 4, [8]=9, 10};
///
class Designator {
public:
  enum DesignatorKind {
    FieldDesignator, ArrayDesignator, ArrayRangeDesignator
  };
private:
  DesignatorKind Kind;

  struct FieldDesignatorInfo {
    const IdentifierInfo *II;
    unsigned DotLoc;
    unsigned NameLoc;
  };
  struct ArrayDesignatorInfo {
    Expr *Index;
    unsigned LBracketLoc;
    mutable unsigned  RBracketLoc;
  };
  struct ArrayRangeDesignatorInfo {
    Expr *Start, *End;
    unsigned LBracketLoc, EllipsisLoc;
    mutable unsigned RBracketLoc;
  };

  union {
    FieldDesignatorInfo FieldInfo;
    ArrayDesignatorInfo ArrayInfo;
    ArrayRangeDesignatorInfo ArrayRangeInfo;
  };

public:

  DesignatorKind getKind() const { return Kind; }
  bool isFieldDesignator() const { return Kind == FieldDesignator; }
  bool isArrayDesignator() const { return Kind == ArrayDesignator; }
  bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

  const IdentifierInfo *getField() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return FieldInfo.II;
  }

  SourceLocation getDotLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(FieldInfo.DotLoc);
  }

  SourceLocation getFieldLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(FieldInfo.NameLoc);
  }

  Expr *getArrayIndex() const {
    assert(isArrayDesignator() && "Invalid accessor");
    return ArrayInfo.Index;
  }

  Expr *getArrayRangeStart() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.Start;
  }
  Expr *getArrayRangeEnd() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.End;
  }

  SourceLocation getLBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    if (isArrayDesignator())
      return SourceLocation::getFromRawEncoding(ArrayInfo.LBracketLoc);
    else
      return SourceLocation::getFromRawEncoding(ArrayRangeInfo.LBracketLoc);
  }

  SourceLocation getRBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    if (isArrayDesignator())
      return SourceLocation::getFromRawEncoding(ArrayInfo.RBracketLoc);
    else
      return SourceLocation::getFromRawEncoding(ArrayRangeInfo.RBracketLoc);
  }

  SourceLocation getEllipsisLoc() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(ArrayRangeInfo.EllipsisLoc);
  }

  static Designator getField(const IdentifierInfo *II, SourceLocation DotLoc,
                             SourceLocation NameLoc) {
    Designator D;
    D.Kind = FieldDesignator;
    D.FieldInfo.II = II;
    D.FieldInfo.DotLoc = DotLoc.getRawEncoding();
    D.FieldInfo.NameLoc = NameLoc.getRawEncoding();
    return D;
  }

  static Designator getArray(Expr *Index,
                             SourceLocation LBracketLoc) {
    Designator D;
    D.Kind = ArrayDesignator;
    D.ArrayInfo.Index = Index;
    D.ArrayInfo.LBracketLoc = LBracketLoc.getRawEncoding();
    D.ArrayInfo.RBracketLoc = 0;
    return D;
  }

  static Designator getArrayRange(Expr *Start,
                                  Expr *End,
                                  SourceLocation LBracketLoc,
                                  SourceLocation EllipsisLoc) {
    Designator D;
    D.Kind = ArrayRangeDesignator;
    D.ArrayRangeInfo.Start = Start;
    D.ArrayRangeInfo.End = End;
    D.ArrayRangeInfo.LBracketLoc = LBracketLoc.getRawEncoding();
    D.ArrayRangeInfo.EllipsisLoc = EllipsisLoc.getRawEncoding();
    D.ArrayRangeInfo.RBracketLoc = 0;
    return D;
  }

  void setRBracketLoc(SourceLocation RBracketLoc) const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    if (isArrayDesignator())
      ArrayInfo.RBracketLoc = RBracketLoc.getRawEncoding();
    else
      ArrayRangeInfo.RBracketLoc = RBracketLoc.getRawEncoding();
  }

  /// ClearExprs - Null out any expression references, which prevents
  /// them from being 'delete'd later.
  void ClearExprs(Sema &Actions) {}

  /// FreeExprs - Release any unclaimed memory for the expressions in
  /// this designator.
  void FreeExprs(Sema &Actions) {}
};


/// Designation - Represent a full designation, which is a sequence of
/// designators.  This class is mostly a helper for InitListDesignations.
class Designation {
  /// Designators - The actual designators for this initializer.
  SmallVector<Designator, 2> Designators;

public:
  /// AddDesignator - Add a designator to the end of this list.
  void AddDesignator(Designator D) {
    Designators.push_back(D);
  }

  bool empty() const { return Designators.empty(); }

  unsigned getNumDesignators() const { return Designators.size(); }
  const Designator &getDesignator(unsigned Idx) const {
    assert(Idx < Designators.size());
    return Designators[Idx];
  }

  /// ClearExprs - Null out any expression references, which prevents them from
  /// being 'delete'd later.
  void ClearExprs(Sema &Actions) {}

  /// FreeExprs - Release any unclaimed memory for the expressions in this
  /// designation.
  void FreeExprs(Sema &Actions) {}
};

} // end namespace clang

#endif
