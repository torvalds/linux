//===--- Attr.h - Classes for representing attributes ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Attr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTR_H
#define LLVM_CLANG_AST_ATTR_H

#include "clang/AST/ASTContextAllocate.h"  // For Attrs.inc
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>

namespace clang {
  class ASTContext;
  class IdentifierInfo;
  class ObjCInterfaceDecl;
  class Expr;
  class QualType;
  class FunctionDecl;
  class TypeSourceInfo;

/// Attr - This represents one attribute.
class Attr {
private:
  SourceRange Range;
  unsigned AttrKind : 16;

protected:
  /// An index into the spelling list of an
  /// attribute defined in Attr.td file.
  unsigned SpellingListIndex : 4;
  unsigned Inherited : 1;
  unsigned IsPackExpansion : 1;
  unsigned Implicit : 1;
  // FIXME: These are properties of the attribute kind, not state for this
  // instance of the attribute.
  unsigned IsLateParsed : 1;
  unsigned InheritEvenIfAlreadyPresent : 1;

  void *operator new(size_t bytes) noexcept {
    llvm_unreachable("Attrs cannot be allocated with regular 'new'.");
  }
  void operator delete(void *data) noexcept {
    llvm_unreachable("Attrs cannot be released with regular 'delete'.");
  }

public:
  // Forward so that the regular new and delete do not hide global ones.
  void *operator new(size_t Bytes, ASTContext &C,
                     size_t Alignment = 8) noexcept {
    return ::operator new(Bytes, C, Alignment);
  }
  void operator delete(void *Ptr, ASTContext &C, size_t Alignment) noexcept {
    return ::operator delete(Ptr, C, Alignment);
  }

protected:
  Attr(attr::Kind AK, SourceRange R, unsigned SpellingListIndex,
       bool IsLateParsed)
    : Range(R), AttrKind(AK), SpellingListIndex(SpellingListIndex),
      Inherited(false), IsPackExpansion(false), Implicit(false),
      IsLateParsed(IsLateParsed), InheritEvenIfAlreadyPresent(false) {}

public:

  attr::Kind getKind() const {
    return static_cast<attr::Kind>(AttrKind);
  }

  unsigned getSpellingListIndex() const { return SpellingListIndex; }
  const char *getSpelling() const;

  SourceLocation getLocation() const { return Range.getBegin(); }
  SourceRange getRange() const { return Range; }
  void setRange(SourceRange R) { Range = R; }

  bool isInherited() const { return Inherited; }

  /// Returns true if the attribute has been implicitly created instead
  /// of explicitly written by the user.
  bool isImplicit() const { return Implicit; }
  void setImplicit(bool I) { Implicit = I; }

  void setPackExpansion(bool PE) { IsPackExpansion = PE; }
  bool isPackExpansion() const { return IsPackExpansion; }

  // Clone this attribute.
  Attr *clone(ASTContext &C) const;

  bool isLateParsed() const { return IsLateParsed; }

  // Pretty print this attribute.
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;
};

class TypeAttr : public Attr {
protected:
  TypeAttr(attr::Kind AK, SourceRange R, unsigned SpellingListIndex,
           bool IsLateParsed)
      : Attr(AK, R, SpellingListIndex, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstTypeAttr &&
           A->getKind() <= attr::LastTypeAttr;
  }
};

class StmtAttr : public Attr {
protected:
  StmtAttr(attr::Kind AK, SourceRange R, unsigned SpellingListIndex,
                  bool IsLateParsed)
      : Attr(AK, R, SpellingListIndex, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstStmtAttr &&
           A->getKind() <= attr::LastStmtAttr;
  }
};

class InheritableAttr : public Attr {
protected:
  InheritableAttr(attr::Kind AK, SourceRange R, unsigned SpellingListIndex,
                  bool IsLateParsed, bool InheritEvenIfAlreadyPresent)
      : Attr(AK, R, SpellingListIndex, IsLateParsed) {
    this->InheritEvenIfAlreadyPresent = InheritEvenIfAlreadyPresent;
  }

public:
  void setInherited(bool I) { Inherited = I; }

  /// Should this attribute be inherited from a prior declaration even if it's
  /// explicitly provided in the current declaration?
  bool shouldInheritEvenIfAlreadyPresent() const {
    return InheritEvenIfAlreadyPresent;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableAttr &&
           A->getKind() <= attr::LastInheritableAttr;
  }
};

class InheritableParamAttr : public InheritableAttr {
protected:
  InheritableParamAttr(attr::Kind AK, SourceRange R, unsigned SpellingListIndex,
                       bool IsLateParsed, bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(AK, R, SpellingListIndex, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableParamAttr &&
           A->getKind() <= attr::LastInheritableParamAttr;
  }
};

/// A parameter attribute which changes the argument-passing ABI rule
/// for the parameter.
class ParameterABIAttr : public InheritableParamAttr {
protected:
  ParameterABIAttr(attr::Kind AK, SourceRange R,
                   unsigned SpellingListIndex, bool IsLateParsed,
                   bool InheritEvenIfAlreadyPresent)
    : InheritableParamAttr(AK, R, SpellingListIndex, IsLateParsed,
                           InheritEvenIfAlreadyPresent) {}

public:
  ParameterABI getABI() const {
    switch (getKind()) {
    case attr::SwiftContext:
      return ParameterABI::SwiftContext;
    case attr::SwiftErrorResult:
      return ParameterABI::SwiftErrorResult;
    case attr::SwiftIndirectResult:
      return ParameterABI::SwiftIndirectResult;
    default:
      llvm_unreachable("bad parameter ABI attribute kind");
    }
  }

  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstParameterABIAttr &&
           A->getKind() <= attr::LastParameterABIAttr;
   }
};

/// A single parameter index whose accessors require each use to make explicit
/// the parameter index encoding needed.
class ParamIdx {
  // Idx is exposed only via accessors that specify specific encodings.
  unsigned Idx : 30;
  unsigned HasThis : 1;
  unsigned IsValid : 1;

  void assertComparable(const ParamIdx &I) const {
    assert(isValid() && I.isValid() &&
           "ParamIdx must be valid to be compared");
    // It's possible to compare indices from separate functions, but so far
    // it's not proven useful.  Moreover, it might be confusing because a
    // comparison on the results of getASTIndex might be inconsistent with a
    // comparison on the ParamIdx objects themselves.
    assert(HasThis == I.HasThis &&
           "ParamIdx must be for the same function to be compared");
  }

public:
  /// Construct an invalid parameter index (\c isValid returns false and
  /// accessors fail an assert).
  ParamIdx() : Idx(0), HasThis(false), IsValid(false) {}

  /// \param Idx is the parameter index as it is normally specified in
  /// attributes in the source: one-origin including any C++ implicit this
  /// parameter.
  ///
  /// \param D is the declaration containing the parameters.  It is used to
  /// determine if there is a C++ implicit this parameter.
  ParamIdx(unsigned Idx, const Decl *D)
      : Idx(Idx), HasThis(false), IsValid(true) {
    assert(Idx >= 1 && "Idx must be one-origin");
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      HasThis = FD->isCXXInstanceMember();
  }

  /// A type into which \c ParamIdx can be serialized.
  ///
  /// A static assertion that it's of the correct size follows the \c ParamIdx
  /// class definition.
  typedef uint32_t SerialType;

  /// Produce a representation that can later be passed to \c deserialize to
  /// construct an equivalent \c ParamIdx.
  SerialType serialize() const {
    return *reinterpret_cast<const SerialType *>(this);
  }

  /// Construct from a result from \c serialize.
  static ParamIdx deserialize(SerialType S) {
    ParamIdx P(*reinterpret_cast<ParamIdx *>(&S));
    assert((!P.IsValid || P.Idx >= 1) && "valid Idx must be one-origin");
    return P;
  }

  /// Is this parameter index valid?
  bool isValid() const { return IsValid; }

  /// Get the parameter index as it would normally be encoded for attributes at
  /// the source level of representation: one-origin including any C++ implicit
  /// this parameter.
  ///
  /// This encoding thus makes sense for diagnostics, pretty printing, and
  /// constructing new attributes from a source-like specification.
  unsigned getSourceIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    return Idx;
  }

  /// Get the parameter index as it would normally be encoded at the AST level
  /// of representation: zero-origin not including any C++ implicit this
  /// parameter.
  ///
  /// This is the encoding primarily used in Sema.  However, in diagnostics,
  /// Sema uses \c getSourceIndex instead.
  unsigned getASTIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 + HasThis &&
           "stored index must be base-1 and not specify C++ implicit this");
    return Idx - 1 - HasThis;
  }

  /// Get the parameter index as it would normally be encoded at the LLVM level
  /// of representation: zero-origin including any C++ implicit this parameter.
  ///
  /// This is the encoding primarily used in CodeGen.
  unsigned getLLVMIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 && "stored index must be base-1");
    return Idx - 1;
  }

  bool operator==(const ParamIdx &I) const {
    assertComparable(I);
    return Idx == I.Idx;
  }
  bool operator!=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx != I.Idx;
  }
  bool operator<(const ParamIdx &I) const {
    assertComparable(I);
    return Idx < I.Idx;
  }
  bool operator>(const ParamIdx &I) const {
    assertComparable(I);
    return Idx > I.Idx;
  }
  bool operator<=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx <= I.Idx;
  }
  bool operator>=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx >= I.Idx;
  }
};

static_assert(sizeof(ParamIdx) == sizeof(ParamIdx::SerialType),
              "ParamIdx does not fit its serialization type");

#include "clang/AST/Attrs.inc"

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const Attr *At) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(At),
                  DiagnosticsEngine::ak_attr);
  return DB;
}

inline const PartialDiagnostic &operator<<(const PartialDiagnostic &PD,
                                           const Attr *At) {
  PD.AddTaggedVal(reinterpret_cast<intptr_t>(At),
                  DiagnosticsEngine::ak_attr);
  return PD;
}
}  // end namespace clang

#endif
