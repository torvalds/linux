//===- TypeLoc.h - Type Source Info Wrapper ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::TypeLoc interface and its subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TYPELOC_H
#define LLVM_CLANG_AST_TYPELOC_H

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace clang {

class ASTContext;
class CXXRecordDecl;
class Expr;
class ObjCInterfaceDecl;
class ObjCProtocolDecl;
class ObjCTypeParamDecl;
class TemplateTypeParmDecl;
class UnqualTypeLoc;
class UnresolvedUsingTypenameDecl;

// Predeclare all the type nodes.
#define ABSTRACT_TYPELOC(Class, Base)
#define TYPELOC(Class, Base) \
  class Class##TypeLoc;
#include "clang/AST/TypeLocNodes.def"

/// Base wrapper for a particular "section" of type source info.
///
/// A client should use the TypeLoc subclasses through castAs()/getAs()
/// in order to get at the actual information.
class TypeLoc {
protected:
  // The correctness of this relies on the property that, for Type *Ty,
  //   QualType(Ty, 0).getAsOpaquePtr() == (void*) Ty
  const void *Ty = nullptr;
  void *Data = nullptr;

public:
  TypeLoc() = default;
  TypeLoc(QualType ty, void *opaqueData)
      : Ty(ty.getAsOpaquePtr()), Data(opaqueData) {}
  TypeLoc(const Type *ty, void *opaqueData)
      : Ty(ty), Data(opaqueData) {}

  /// Convert to the specified TypeLoc type, asserting that this TypeLoc
  /// is of the desired type.
  ///
  /// \pre T::isKind(*this)
  template<typename T>
  T castAs() const {
    assert(T::isKind(*this));
    T t;
    TypeLoc& tl = t;
    tl = *this;
    return t;
  }

  /// Convert to the specified TypeLoc type, returning a null TypeLoc if
  /// this TypeLoc is not of the desired type.
  template<typename T>
  T getAs() const {
    if (!T::isKind(*this))
      return {};
    T t;
    TypeLoc& tl = t;
    tl = *this;
    return t;
  }

  /// Convert to the specified TypeLoc type, returning a null TypeLoc if
  /// this TypeLoc is not of the desired type. It will consider type
  /// adjustments from a type that was written as a T to another type that is
  /// still canonically a T (ignores parens, attributes, elaborated types, etc).
  template <typename T>
  T getAsAdjusted() const;

  /// The kinds of TypeLocs.  Equivalent to the Type::TypeClass enum,
  /// except it also defines a Qualified enum that corresponds to the
  /// QualifiedLoc class.
  enum TypeLocClass {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base) \
    Class = Type::Class,
#include "clang/AST/TypeNodes.def"
    Qualified
  };

  TypeLocClass getTypeLocClass() const {
    if (getType().hasLocalQualifiers()) return Qualified;
    return (TypeLocClass) getType()->getTypeClass();
  }

  bool isNull() const { return !Ty; }
  explicit operator bool() const { return Ty; }

  /// Returns the size of type source info data block for the given type.
  static unsigned getFullDataSizeForType(QualType Ty);

  /// Returns the alignment of type source info data block for
  /// the given type.
  static unsigned getLocalAlignmentForType(QualType Ty);

  /// Get the type for which this source info wrapper provides
  /// information.
  QualType getType() const {
    return QualType::getFromOpaquePtr(Ty);
  }

  const Type *getTypePtr() const {
    return QualType::getFromOpaquePtr(Ty).getTypePtr();
  }

  /// Get the pointer where source information is stored.
  void *getOpaqueData() const {
    return Data;
  }

  /// Get the begin source location.
  SourceLocation getBeginLoc() const;

  /// Get the end source location.
  SourceLocation getEndLoc() const;

  /// Get the full source range.
  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getBeginLoc(), getEndLoc());
  }


  /// Get the local source range.
  SourceRange getLocalSourceRange() const {
    return getLocalSourceRangeImpl(*this);
  }

  /// Returns the size of the type source info data block.
  unsigned getFullDataSize() const {
    return getFullDataSizeForType(getType());
  }

  /// Get the next TypeLoc pointed by this TypeLoc, e.g for "int*" the
  /// TypeLoc is a PointerLoc and next TypeLoc is for "int".
  TypeLoc getNextTypeLoc() const {
    return getNextTypeLocImpl(*this);
  }

  /// Skips past any qualifiers, if this is qualified.
  UnqualTypeLoc getUnqualifiedLoc() const; // implemented in this header

  TypeLoc IgnoreParens() const;

  /// Find a type with the location of an explicit type qualifier.
  ///
  /// The result, if non-null, will be one of:
  ///   QualifiedTypeLoc
  ///   AtomicTypeLoc
  ///   AttributedTypeLoc, for those type attributes that behave as qualifiers
  TypeLoc findExplicitQualifierLoc() const;

  /// Initializes this to state that every location in this
  /// type is the given location.
  ///
  /// This method exists to provide a simple transition for code that
  /// relies on location-less types.
  void initialize(ASTContext &Context, SourceLocation Loc) const {
    initializeImpl(Context, *this, Loc);
  }

  /// Initializes this by copying its information from another
  /// TypeLoc of the same type.
  void initializeFullCopy(TypeLoc Other) {
    assert(getType() == Other.getType());
    copy(Other);
  }

  /// Initializes this by copying its information from another
  /// TypeLoc of the same type.  The given size must be the full data
  /// size.
  void initializeFullCopy(TypeLoc Other, unsigned Size) {
    assert(getType() == Other.getType());
    assert(getFullDataSize() == Size);
    copy(Other);
  }

  /// Copies the other type loc into this one.
  void copy(TypeLoc other);

  friend bool operator==(const TypeLoc &LHS, const TypeLoc &RHS) {
    return LHS.Ty == RHS.Ty && LHS.Data == RHS.Data;
  }

  friend bool operator!=(const TypeLoc &LHS, const TypeLoc &RHS) {
    return !(LHS == RHS);
  }

  /// Find the location of the nullability specifier (__nonnull,
  /// __nullable, or __null_unspecifier), if there is one.
  SourceLocation findNullabilityLoc() const;

private:
  static bool isKind(const TypeLoc&) {
    return true;
  }

  static void initializeImpl(ASTContext &Context, TypeLoc TL,
                             SourceLocation Loc);
  static TypeLoc getNextTypeLocImpl(TypeLoc TL);
  static TypeLoc IgnoreParensImpl(TypeLoc TL);
  static SourceRange getLocalSourceRangeImpl(TypeLoc TL);
};

/// Return the TypeLoc for a type source info.
inline TypeLoc TypeSourceInfo::getTypeLoc() const {
  // TODO: is this alignment already sufficient?
  return TypeLoc(Ty, const_cast<void*>(static_cast<const void*>(this + 1)));
}

/// Wrapper of type source information for a type with
/// no direct qualifiers.
class UnqualTypeLoc : public TypeLoc {
public:
  UnqualTypeLoc() = default;
  UnqualTypeLoc(const Type *Ty, void *Data) : TypeLoc(Ty, Data) {}

  const Type *getTypePtr() const {
    return reinterpret_cast<const Type*>(Ty);
  }

  TypeLocClass getTypeLocClass() const {
    return (TypeLocClass) getTypePtr()->getTypeClass();
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers();
  }
};

/// Wrapper of type source information for a type with
/// non-trivial direct qualifiers.
///
/// Currently, we intentionally do not provide source location for
/// type qualifiers.
class QualifiedTypeLoc : public TypeLoc {
public:
  SourceRange getLocalSourceRange() const { return {}; }

  UnqualTypeLoc getUnqualifiedLoc() const {
    unsigned align =
        TypeLoc::getLocalAlignmentForType(QualType(getTypePtr(), 0));
    auto dataInt = reinterpret_cast<uintptr_t>(Data);
    dataInt = llvm::alignTo(dataInt, align);
    return UnqualTypeLoc(getTypePtr(), reinterpret_cast<void*>(dataInt));
  }

  /// Initializes the local data of this type source info block to
  /// provide no information.
  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    // do nothing
  }

  void copyLocal(TypeLoc other) {
    // do nothing
  }

  TypeLoc getNextTypeLoc() const {
    return getUnqualifiedLoc();
  }

  /// Returns the size of the type source info data block that is
  /// specific to this type.
  unsigned getLocalDataSize() const {
    // In fact, we don't currently preserve any location information
    // for qualifiers.
    return 0;
  }

  /// Returns the alignment of the type source info data block that is
  /// specific to this type.
  unsigned getLocalDataAlignment() const {
    // We don't preserve any location information.
    return 1;
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL) {
    return TL.getType().hasLocalQualifiers();
  }
};

inline UnqualTypeLoc TypeLoc::getUnqualifiedLoc() const {
  if (QualifiedTypeLoc Loc = getAs<QualifiedTypeLoc>())
    return Loc.getUnqualifiedLoc();
  return castAs<UnqualTypeLoc>();
}

/// A metaprogramming base class for TypeLoc classes which correspond
/// to a particular Type subclass.  It is accepted for a single
/// TypeLoc class to correspond to multiple Type classes.
///
/// \tparam Base a class from which to derive
/// \tparam Derived the class deriving from this one
/// \tparam TypeClass the concrete Type subclass associated with this
///   location type
/// \tparam LocalData the structure type of local location data for
///   this type
///
/// TypeLocs with non-constant amounts of local data should override
/// getExtraLocalDataSize(); getExtraLocalData() will then point to
/// this extra memory.
///
/// TypeLocs with an inner type should define
///   QualType getInnerType() const
/// and getInnerTypeLoc() will then point to this inner type's
/// location data.
///
/// A word about hierarchies: this template is not designed to be
/// derived from multiple times in a hierarchy.  It is also not
/// designed to be used for classes where subtypes might provide
/// different amounts of source information.  It should be subclassed
/// only at the deepest portion of the hierarchy where all children
/// have identical source information; if that's an abstract type,
/// then further descendents should inherit from
/// InheritingConcreteTypeLoc instead.
template <class Base, class Derived, class TypeClass, class LocalData>
class ConcreteTypeLoc : public Base {
  friend class TypeLoc;

  const Derived *asDerived() const {
    return static_cast<const Derived*>(this);
  }

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers() &&
           Derived::classofType(TL.getTypePtr());
  }

  static bool classofType(const Type *Ty) {
    return TypeClass::classof(Ty);
  }

public:
  unsigned getLocalDataAlignment() const {
    return std::max(unsigned(alignof(LocalData)),
                    asDerived()->getExtraLocalDataAlignment());
  }

  unsigned getLocalDataSize() const {
    unsigned size = sizeof(LocalData);
    unsigned extraAlign = asDerived()->getExtraLocalDataAlignment();
    size = llvm::alignTo(size, extraAlign);
    size += asDerived()->getExtraLocalDataSize();
    return size;
  }

  void copyLocal(Derived other) {
    // Some subclasses have no data to copy.
    if (asDerived()->getLocalDataSize() == 0) return;

    // Copy the fixed-sized local data.
    memcpy(getLocalData(), other.getLocalData(), sizeof(LocalData));

    // Copy the variable-sized local data. We need to do this
    // separately because the padding in the source and the padding in
    // the destination might be different.
    memcpy(getExtraLocalData(), other.getExtraLocalData(),
           asDerived()->getExtraLocalDataSize());
  }

  TypeLoc getNextTypeLoc() const {
    return getNextTypeLoc(asDerived()->getInnerType());
  }

  const TypeClass *getTypePtr() const {
    return cast<TypeClass>(Base::getTypePtr());
  }

protected:
  unsigned getExtraLocalDataSize() const {
    return 0;
  }

  unsigned getExtraLocalDataAlignment() const {
    return 1;
  }

  LocalData *getLocalData() const {
    return static_cast<LocalData*>(Base::Data);
  }

  /// Gets a pointer past the Info structure; useful for classes with
  /// local data that can't be captured in the Info (e.g. because it's
  /// of variable size).
  void *getExtraLocalData() const {
    unsigned size = sizeof(LocalData);
    unsigned extraAlign = asDerived()->getExtraLocalDataAlignment();
    size = llvm::alignTo(size, extraAlign);
    return reinterpret_cast<char*>(Base::Data) + size;
  }

  void *getNonLocalData() const {
    auto data = reinterpret_cast<uintptr_t>(Base::Data);
    data += asDerived()->getLocalDataSize();
    data = llvm::alignTo(data, getNextTypeAlign());
    return reinterpret_cast<void*>(data);
  }

  struct HasNoInnerType {};
  HasNoInnerType getInnerType() const { return HasNoInnerType(); }

  TypeLoc getInnerTypeLoc() const {
    return TypeLoc(asDerived()->getInnerType(), getNonLocalData());
  }

private:
  unsigned getInnerTypeSize() const {
    return getInnerTypeSize(asDerived()->getInnerType());
  }

  unsigned getInnerTypeSize(HasNoInnerType _) const {
    return 0;
  }

  unsigned getInnerTypeSize(QualType _) const {
    return getInnerTypeLoc().getFullDataSize();
  }

  unsigned getNextTypeAlign() const {
    return getNextTypeAlign(asDerived()->getInnerType());
  }

  unsigned getNextTypeAlign(HasNoInnerType _) const {
    return 1;
  }

  unsigned getNextTypeAlign(QualType T) const {
    return TypeLoc::getLocalAlignmentForType(T);
  }

  TypeLoc getNextTypeLoc(HasNoInnerType _) const { return {}; }

  TypeLoc getNextTypeLoc(QualType T) const {
    return TypeLoc(T, getNonLocalData());
  }
};

/// A metaprogramming class designed for concrete subtypes of abstract
/// types where all subtypes share equivalently-structured source
/// information.  See the note on ConcreteTypeLoc.
template <class Base, class Derived, class TypeClass>
class InheritingConcreteTypeLoc : public Base {
  friend class TypeLoc;

  static bool classofType(const Type *Ty) {
    return TypeClass::classof(Ty);
  }

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers() &&
           Derived::classofType(TL.getTypePtr());
  }
  static bool isKind(const UnqualTypeLoc &TL) {
    return Derived::classofType(TL.getTypePtr());
  }

public:
  const TypeClass *getTypePtr() const {
    return cast<TypeClass>(Base::getTypePtr());
  }
};

struct TypeSpecLocInfo {
  SourceLocation NameLoc;
};

/// A reasonable base class for TypeLocs that correspond to
/// types that are written as a type-specifier.
class TypeSpecTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                               TypeSpecTypeLoc,
                                               Type,
                                               TypeSpecLocInfo> {
public:
  enum {
    LocalDataSize = sizeof(TypeSpecLocInfo),
    LocalDataAlignment = alignof(TypeSpecLocInfo)
  };

  SourceLocation getNameLoc() const {
    return this->getLocalData()->NameLoc;
  }

  void setNameLoc(SourceLocation Loc) {
    this->getLocalData()->NameLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getNameLoc(), getNameLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setNameLoc(Loc);
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL);
};

struct BuiltinLocInfo {
  SourceRange BuiltinRange;
};

/// Wrapper for source info for builtin types.
class BuiltinTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                              BuiltinTypeLoc,
                                              BuiltinType,
                                              BuiltinLocInfo> {
public:
  SourceLocation getBuiltinLoc() const {
    return getLocalData()->BuiltinRange.getBegin();
  }

  void setBuiltinLoc(SourceLocation Loc) {
    getLocalData()->BuiltinRange = Loc;
  }

  void expandBuiltinRange(SourceRange Range) {
    SourceRange &BuiltinRange = getLocalData()->BuiltinRange;
    if (!BuiltinRange.getBegin().isValid()) {
      BuiltinRange = Range;
    } else {
      BuiltinRange.setBegin(std::min(Range.getBegin(), BuiltinRange.getBegin()));
      BuiltinRange.setEnd(std::max(Range.getEnd(), BuiltinRange.getEnd()));
    }
  }

  SourceLocation getNameLoc() const { return getBuiltinLoc(); }

  WrittenBuiltinSpecs& getWrittenBuiltinSpecs() {
    return *(static_cast<WrittenBuiltinSpecs*>(getExtraLocalData()));
  }
  const WrittenBuiltinSpecs& getWrittenBuiltinSpecs() const {
    return *(static_cast<WrittenBuiltinSpecs*>(getExtraLocalData()));
  }

  bool needsExtraLocalData() const {
    BuiltinType::Kind bk = getTypePtr()->getKind();
    return (bk >= BuiltinType::UShort && bk <= BuiltinType::UInt128)
      || (bk >= BuiltinType::Short && bk <= BuiltinType::Float128)
      || bk == BuiltinType::UChar
      || bk == BuiltinType::SChar;
  }

  unsigned getExtraLocalDataSize() const {
    return needsExtraLocalData() ? sizeof(WrittenBuiltinSpecs) : 0;
  }

  unsigned getExtraLocalDataAlignment() const {
    return needsExtraLocalData() ? alignof(WrittenBuiltinSpecs) : 1;
  }

  SourceRange getLocalSourceRange() const {
    return getLocalData()->BuiltinRange;
  }

  TypeSpecifierSign getWrittenSignSpec() const {
    if (needsExtraLocalData())
      return static_cast<TypeSpecifierSign>(getWrittenBuiltinSpecs().Sign);
    else
      return TSS_unspecified;
  }

  bool hasWrittenSignSpec() const {
    return getWrittenSignSpec() != TSS_unspecified;
  }

  void setWrittenSignSpec(TypeSpecifierSign written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Sign = written;
  }

  TypeSpecifierWidth getWrittenWidthSpec() const {
    if (needsExtraLocalData())
      return static_cast<TypeSpecifierWidth>(getWrittenBuiltinSpecs().Width);
    else
      return TSW_unspecified;
  }

  bool hasWrittenWidthSpec() const {
    return getWrittenWidthSpec() != TSW_unspecified;
  }

  void setWrittenWidthSpec(TypeSpecifierWidth written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Width = written;
  }

  TypeSpecifierType getWrittenTypeSpec() const;

  bool hasWrittenTypeSpec() const {
    return getWrittenTypeSpec() != TST_unspecified;
  }

  void setWrittenTypeSpec(TypeSpecifierType written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Type = written;
  }

  bool hasModeAttr() const {
    if (needsExtraLocalData())
      return getWrittenBuiltinSpecs().ModeAttr;
    else
      return false;
  }

  void setModeAttr(bool written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().ModeAttr = written;
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setBuiltinLoc(Loc);
    if (needsExtraLocalData()) {
      WrittenBuiltinSpecs &wbs = getWrittenBuiltinSpecs();
      wbs.Sign = TSS_unspecified;
      wbs.Width = TSW_unspecified;
      wbs.Type = TST_unspecified;
      wbs.ModeAttr = false;
    }
  }
};

/// Wrapper for source info for typedefs.
class TypedefTypeLoc : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                                        TypedefTypeLoc,
                                                        TypedefType> {
public:
  TypedefNameDecl *getTypedefNameDecl() const {
    return getTypePtr()->getDecl();
  }
};

/// Wrapper for source info for injected class names of class
/// templates.
class InjectedClassNameTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     InjectedClassNameTypeLoc,
                                     InjectedClassNameType> {
public:
  CXXRecordDecl *getDecl() const {
    return getTypePtr()->getDecl();
  }
};

/// Wrapper for source info for unresolved typename using decls.
class UnresolvedUsingTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     UnresolvedUsingTypeLoc,
                                     UnresolvedUsingType> {
public:
  UnresolvedUsingTypenameDecl *getDecl() const {
    return getTypePtr()->getDecl();
  }
};

/// Wrapper for source info for tag types.  Note that this only
/// records source info for the name itself; a type written 'struct foo'
/// should be represented as an ElaboratedTypeLoc.  We currently
/// only do that when C++ is enabled because of the expense of
/// creating an ElaboratedType node for so many type references in C.
class TagTypeLoc : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                                    TagTypeLoc,
                                                    TagType> {
public:
  TagDecl *getDecl() const { return getTypePtr()->getDecl(); }

  /// True if the tag was defined in this type specifier.
  bool isDefinition() const {
    TagDecl *D = getDecl();
    return D->isCompleteDefinition() &&
           (D->getIdentifier() == nullptr || D->getLocation() == getNameLoc());
  }
};

/// Wrapper for source info for record types.
class RecordTypeLoc : public InheritingConcreteTypeLoc<TagTypeLoc,
                                                       RecordTypeLoc,
                                                       RecordType> {
public:
  RecordDecl *getDecl() const { return getTypePtr()->getDecl(); }
};

/// Wrapper for source info for enum types.
class EnumTypeLoc : public InheritingConcreteTypeLoc<TagTypeLoc,
                                                     EnumTypeLoc,
                                                     EnumType> {
public:
  EnumDecl *getDecl() const { return getTypePtr()->getDecl(); }
};

/// Wrapper for template type parameters.
class TemplateTypeParmTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     TemplateTypeParmTypeLoc,
                                     TemplateTypeParmType> {
public:
  TemplateTypeParmDecl *getDecl() const { return getTypePtr()->getDecl(); }
};

struct ObjCTypeParamTypeLocInfo {
  SourceLocation NameLoc;
};

/// ProtocolLAngleLoc, ProtocolRAngleLoc, and the source locations for
/// protocol qualifiers are stored after Info.
class ObjCTypeParamTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                     ObjCTypeParamTypeLoc,
                                     ObjCTypeParamType,
                                     ObjCTypeParamTypeLocInfo> {
  // SourceLocations are stored after Info, one for each protocol qualifier.
  SourceLocation *getProtocolLocArray() const {
    return (SourceLocation*)this->getExtraLocalData() + 2;
  }

public:
  ObjCTypeParamDecl *getDecl() const { return getTypePtr()->getDecl(); }

  SourceLocation getNameLoc() const {
    return this->getLocalData()->NameLoc;
  }

  void setNameLoc(SourceLocation Loc) {
    this->getLocalData()->NameLoc = Loc;
  }

  SourceLocation getProtocolLAngleLoc() const {
    return getNumProtocols()  ?
      *((SourceLocation*)this->getExtraLocalData()) :
      SourceLocation();
  }

  void setProtocolLAngleLoc(SourceLocation Loc) {
    *((SourceLocation*)this->getExtraLocalData()) = Loc;
  }

  SourceLocation getProtocolRAngleLoc() const {
    return getNumProtocols()  ?
      *((SourceLocation*)this->getExtraLocalData() + 1) :
      SourceLocation();
  }

  void setProtocolRAngleLoc(SourceLocation Loc) {
    *((SourceLocation*)this->getExtraLocalData() + 1) = Loc;
  }

  unsigned getNumProtocols() const {
    return this->getTypePtr()->getNumProtocols();
  }

  SourceLocation getProtocolLoc(unsigned i) const {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    return getProtocolLocArray()[i];
  }

  void setProtocolLoc(unsigned i, SourceLocation Loc) {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    getProtocolLocArray()[i] = Loc;
  }

  ObjCProtocolDecl *getProtocol(unsigned i) const {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    return *(this->getTypePtr()->qual_begin() + i);
  }

  ArrayRef<SourceLocation> getProtocolLocs() const {
    return llvm::makeArrayRef(getProtocolLocArray(), getNumProtocols());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);

  unsigned getExtraLocalDataSize() const {
    if (!this->getNumProtocols()) return 0;
    // When there are protocol qualifers, we have LAngleLoc and RAngleLoc
    // as well.
    return (this->getNumProtocols() + 2) * sizeof(SourceLocation) ;
  }

  unsigned getExtraLocalDataAlignment() const {
    return alignof(SourceLocation);
  }

  SourceRange getLocalSourceRange() const {
    SourceLocation start = getNameLoc();
    SourceLocation end = getProtocolRAngleLoc();
    if (end.isInvalid()) return SourceRange(start, start);
    return SourceRange(start, end);
  }
};

/// Wrapper for substituted template type parameters.
class SubstTemplateTypeParmTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     SubstTemplateTypeParmTypeLoc,
                                     SubstTemplateTypeParmType> {
};

  /// Wrapper for substituted template type parameters.
class SubstTemplateTypeParmPackTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     SubstTemplateTypeParmPackTypeLoc,
                                     SubstTemplateTypeParmPackType> {
};

struct AttributedLocInfo {
  const Attr *TypeAttr;
};

/// Type source information for an attributed type.
class AttributedTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                                 AttributedTypeLoc,
                                                 AttributedType,
                                                 AttributedLocInfo> {
public:
  attr::Kind getAttrKind() const {
    return getTypePtr()->getAttrKind();
  }

  bool isQualifier() const {
    return getTypePtr()->isQualifier();
  }

  /// The modified type, which is generally canonically different from
  /// the attribute type.
  ///    int main(int, char**) __attribute__((noreturn))
  ///    ~~~     ~~~~~~~~~~~~~
  TypeLoc getModifiedLoc() const {
    return getInnerTypeLoc();
  }

  /// The type attribute.
  const Attr *getAttr() const {
    return getLocalData()->TypeAttr;
  }
  void setAttr(const Attr *A) {
    getLocalData()->TypeAttr = A;
  }

  template<typename T> const T *getAttrAs() {
    return dyn_cast_or_null<T>(getAttr());
  }

  SourceRange getLocalSourceRange() const {
    // Note that this does *not* include the range of the attribute
    // enclosure, e.g.:
    //    __attribute__((foo(bar)))
    //    ^~~~~~~~~~~~~~~        ~~
    // or
    //    [[foo(bar)]]
    //    ^~        ~~
    // That enclosure doesn't necessarily belong to a single attribute
    // anyway.
    return getAttr() ? getAttr()->getRange() : SourceRange();
  }

  void initializeLocal(ASTContext &Context, SourceLocation loc) {
    setAttr(nullptr);
  }

  QualType getInnerType() const {
    return getTypePtr()->getModifiedType();
  }
};

struct ObjCObjectTypeLocInfo {
  SourceLocation TypeArgsLAngleLoc;
  SourceLocation TypeArgsRAngleLoc;
  SourceLocation ProtocolLAngleLoc;
  SourceLocation ProtocolRAngleLoc;
  bool HasBaseTypeAsWritten;
};

// A helper class for defining ObjC TypeLocs that can qualified with
// protocols.
//
// TypeClass basically has to be either ObjCInterfaceType or
// ObjCObjectPointerType.
class ObjCObjectTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                                 ObjCObjectTypeLoc,
                                                 ObjCObjectType,
                                                 ObjCObjectTypeLocInfo> {
  // TypeSourceInfo*'s are stored after Info, one for each type argument.
  TypeSourceInfo **getTypeArgLocArray() const {
    return (TypeSourceInfo**)this->getExtraLocalData();
  }

  // SourceLocations are stored after the type argument information, one for
  // each Protocol.
  SourceLocation *getProtocolLocArray() const {
    return (SourceLocation*)(getTypeArgLocArray() + getNumTypeArgs());
  }

public:
  SourceLocation getTypeArgsLAngleLoc() const {
    return this->getLocalData()->TypeArgsLAngleLoc;
  }

  void setTypeArgsLAngleLoc(SourceLocation Loc) {
    this->getLocalData()->TypeArgsLAngleLoc = Loc;
  }

  SourceLocation getTypeArgsRAngleLoc() const {
    return this->getLocalData()->TypeArgsRAngleLoc;
  }

  void setTypeArgsRAngleLoc(SourceLocation Loc) {
    this->getLocalData()->TypeArgsRAngleLoc = Loc;
  }

  unsigned getNumTypeArgs() const {
    return this->getTypePtr()->getTypeArgsAsWritten().size();
  }

  TypeSourceInfo *getTypeArgTInfo(unsigned i) const {
    assert(i < getNumTypeArgs() && "Index is out of bounds!");
    return getTypeArgLocArray()[i];
  }

  void setTypeArgTInfo(unsigned i, TypeSourceInfo *TInfo) {
    assert(i < getNumTypeArgs() && "Index is out of bounds!");
    getTypeArgLocArray()[i] = TInfo;
  }

  SourceLocation getProtocolLAngleLoc() const {
    return this->getLocalData()->ProtocolLAngleLoc;
  }

  void setProtocolLAngleLoc(SourceLocation Loc) {
    this->getLocalData()->ProtocolLAngleLoc = Loc;
  }

  SourceLocation getProtocolRAngleLoc() const {
    return this->getLocalData()->ProtocolRAngleLoc;
  }

  void setProtocolRAngleLoc(SourceLocation Loc) {
    this->getLocalData()->ProtocolRAngleLoc = Loc;
  }

  unsigned getNumProtocols() const {
    return this->getTypePtr()->getNumProtocols();
  }

  SourceLocation getProtocolLoc(unsigned i) const {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    return getProtocolLocArray()[i];
  }

  void setProtocolLoc(unsigned i, SourceLocation Loc) {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    getProtocolLocArray()[i] = Loc;
  }

  ObjCProtocolDecl *getProtocol(unsigned i) const {
    assert(i < getNumProtocols() && "Index is out of bounds!");
    return *(this->getTypePtr()->qual_begin() + i);
  }


  ArrayRef<SourceLocation> getProtocolLocs() const {
    return llvm::makeArrayRef(getProtocolLocArray(), getNumProtocols());
  }

  bool hasBaseTypeAsWritten() const {
    return getLocalData()->HasBaseTypeAsWritten;
  }

  void setHasBaseTypeAsWritten(bool HasBaseType) {
    getLocalData()->HasBaseTypeAsWritten = HasBaseType;
  }

  TypeLoc getBaseLoc() const {
    return getInnerTypeLoc();
  }

  SourceRange getLocalSourceRange() const {
    SourceLocation start = getTypeArgsLAngleLoc();
    if (start.isInvalid())
      start = getProtocolLAngleLoc();
    SourceLocation end = getProtocolRAngleLoc();
    if (end.isInvalid())
      end = getTypeArgsRAngleLoc();
    return SourceRange(start, end);
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);

  unsigned getExtraLocalDataSize() const {
    return this->getNumTypeArgs() * sizeof(TypeSourceInfo *)
         + this->getNumProtocols() * sizeof(SourceLocation);
  }

  unsigned getExtraLocalDataAlignment() const {
    static_assert(alignof(ObjCObjectTypeLoc) >= alignof(TypeSourceInfo *),
                  "not enough alignment for tail-allocated data");
    return alignof(TypeSourceInfo *);
  }

  QualType getInnerType() const {
    return getTypePtr()->getBaseType();
  }
};

struct ObjCInterfaceLocInfo {
  SourceLocation NameLoc;
  SourceLocation NameEndLoc;
};

/// Wrapper for source info for ObjC interfaces.
class ObjCInterfaceTypeLoc : public ConcreteTypeLoc<ObjCObjectTypeLoc,
                                                    ObjCInterfaceTypeLoc,
                                                    ObjCInterfaceType,
                                                    ObjCInterfaceLocInfo> {
public:
  ObjCInterfaceDecl *getIFaceDecl() const {
    return getTypePtr()->getDecl();
  }

  SourceLocation getNameLoc() const {
    return getLocalData()->NameLoc;
  }

  void setNameLoc(SourceLocation Loc) {
    getLocalData()->NameLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getNameLoc(), getNameEndLoc());
  }

  SourceLocation getNameEndLoc() const {
    return getLocalData()->NameEndLoc;
  }

  void setNameEndLoc(SourceLocation Loc) {
    getLocalData()->NameEndLoc = Loc;
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setNameLoc(Loc);
    setNameEndLoc(Loc);
  }
};

struct ParenLocInfo {
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
};

class ParenTypeLoc
  : public ConcreteTypeLoc<UnqualTypeLoc, ParenTypeLoc, ParenType,
                           ParenLocInfo> {
public:
  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }

  TypeLoc getInnerLoc() const {
    return getInnerTypeLoc();
  }

  QualType getInnerType() const {
    return this->getTypePtr()->getInnerType();
  }
};

inline TypeLoc TypeLoc::IgnoreParens() const {
  if (ParenTypeLoc::isKind(*this))
    return IgnoreParensImpl(*this);
  return *this;
}

struct AdjustedLocInfo {}; // Nothing.

class AdjustedTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, AdjustedTypeLoc,
                                               AdjustedType, AdjustedLocInfo> {
public:
  TypeLoc getOriginalLoc() const {
    return getInnerTypeLoc();
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    // do nothing
  }

  QualType getInnerType() const {
    // The inner type is the undecayed type, since that's what we have source
    // location information for.
    return getTypePtr()->getOriginalType();
  }

  SourceRange getLocalSourceRange() const { return {}; }

  unsigned getLocalDataSize() const {
    // sizeof(AdjustedLocInfo) is 1, but we don't need its address to be unique
    // anyway.  TypeLocBuilder can't handle data sizes of 1.
    return 0;  // No data.
  }
};

/// Wrapper for source info for pointers decayed from arrays and
/// functions.
class DecayedTypeLoc : public InheritingConcreteTypeLoc<
                           AdjustedTypeLoc, DecayedTypeLoc, DecayedType> {
};

struct PointerLikeLocInfo {
  SourceLocation StarLoc;
};

/// A base class for
template <class Derived, class TypeClass, class LocalData = PointerLikeLocInfo>
class PointerLikeTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, Derived,
                                                  TypeClass, LocalData> {
public:
  SourceLocation getSigilLoc() const {
    return this->getLocalData()->StarLoc;
  }

  void setSigilLoc(SourceLocation Loc) {
    this->getLocalData()->StarLoc = Loc;
  }

  TypeLoc getPointeeLoc() const {
    return this->getInnerTypeLoc();
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getSigilLoc(), getSigilLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setSigilLoc(Loc);
  }

  QualType getInnerType() const {
    return this->getTypePtr()->getPointeeType();
  }
};

/// Wrapper for source info for pointers.
class PointerTypeLoc : public PointerLikeTypeLoc<PointerTypeLoc,
                                                 PointerType> {
public:
  SourceLocation getStarLoc() const {
    return getSigilLoc();
  }

  void setStarLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }
};

/// Wrapper for source info for block pointers.
class BlockPointerTypeLoc : public PointerLikeTypeLoc<BlockPointerTypeLoc,
                                                      BlockPointerType> {
public:
  SourceLocation getCaretLoc() const {
    return getSigilLoc();
  }

  void setCaretLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }
};

struct MemberPointerLocInfo : public PointerLikeLocInfo {
  TypeSourceInfo *ClassTInfo;
};

/// Wrapper for source info for member pointers.
class MemberPointerTypeLoc : public PointerLikeTypeLoc<MemberPointerTypeLoc,
                                                       MemberPointerType,
                                                       MemberPointerLocInfo> {
public:
  SourceLocation getStarLoc() const {
    return getSigilLoc();
  }

  void setStarLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }

  const Type *getClass() const {
    return getTypePtr()->getClass();
  }

  TypeSourceInfo *getClassTInfo() const {
    return getLocalData()->ClassTInfo;
  }

  void setClassTInfo(TypeSourceInfo* TI) {
    getLocalData()->ClassTInfo = TI;
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setSigilLoc(Loc);
    setClassTInfo(nullptr);
  }

  SourceRange getLocalSourceRange() const {
    if (TypeSourceInfo *TI = getClassTInfo())
      return SourceRange(TI->getTypeLoc().getBeginLoc(), getStarLoc());
    else
      return SourceRange(getStarLoc());
  }
};

/// Wraps an ObjCPointerType with source location information.
class ObjCObjectPointerTypeLoc :
    public PointerLikeTypeLoc<ObjCObjectPointerTypeLoc,
                              ObjCObjectPointerType> {
public:
  SourceLocation getStarLoc() const {
    return getSigilLoc();
  }

  void setStarLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }
};

class ReferenceTypeLoc : public PointerLikeTypeLoc<ReferenceTypeLoc,
                                                   ReferenceType> {
public:
  QualType getInnerType() const {
    return getTypePtr()->getPointeeTypeAsWritten();
  }
};

class LValueReferenceTypeLoc :
    public InheritingConcreteTypeLoc<ReferenceTypeLoc,
                                     LValueReferenceTypeLoc,
                                     LValueReferenceType> {
public:
  SourceLocation getAmpLoc() const {
    return getSigilLoc();
  }

  void setAmpLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }
};

class RValueReferenceTypeLoc :
    public InheritingConcreteTypeLoc<ReferenceTypeLoc,
                                     RValueReferenceTypeLoc,
                                     RValueReferenceType> {
public:
  SourceLocation getAmpAmpLoc() const {
    return getSigilLoc();
  }

  void setAmpAmpLoc(SourceLocation Loc) {
    setSigilLoc(Loc);
  }
};

struct FunctionLocInfo {
  SourceLocation LocalRangeBegin;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
  SourceLocation LocalRangeEnd;
};

/// Wrapper for source info for functions.
class FunctionTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                               FunctionTypeLoc,
                                               FunctionType,
                                               FunctionLocInfo> {
  bool hasExceptionSpec() const {
    if (auto *FPT = dyn_cast<FunctionProtoType>(getTypePtr())) {
      return FPT->hasExceptionSpec();
    }
    return false;
  }

  SourceRange *getExceptionSpecRangePtr() const {
    assert(hasExceptionSpec() && "No exception spec range");
    // After the Info comes the ParmVarDecl array, and after that comes the
    // exception specification information.
    return (SourceRange *)(getParmArray() + getNumParams());
  }

public:
  SourceLocation getLocalRangeBegin() const {
    return getLocalData()->LocalRangeBegin;
  }

  void setLocalRangeBegin(SourceLocation L) {
    getLocalData()->LocalRangeBegin = L;
  }

  SourceLocation getLocalRangeEnd() const {
    return getLocalData()->LocalRangeEnd;
  }

  void setLocalRangeEnd(SourceLocation L) {
    getLocalData()->LocalRangeEnd = L;
  }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  SourceRange getExceptionSpecRange() const {
    if (hasExceptionSpec())
      return *getExceptionSpecRangePtr();
    return {};
  }

  void setExceptionSpecRange(SourceRange R) {
    if (hasExceptionSpec())
      *getExceptionSpecRangePtr() = R;
  }

  ArrayRef<ParmVarDecl *> getParams() const {
    return llvm::makeArrayRef(getParmArray(), getNumParams());
  }

  // ParmVarDecls* are stored after Info, one for each parameter.
  ParmVarDecl **getParmArray() const {
    return (ParmVarDecl**) getExtraLocalData();
  }

  unsigned getNumParams() const {
    if (isa<FunctionNoProtoType>(getTypePtr()))
      return 0;
    return cast<FunctionProtoType>(getTypePtr())->getNumParams();
  }

  ParmVarDecl *getParam(unsigned i) const { return getParmArray()[i]; }
  void setParam(unsigned i, ParmVarDecl *VD) { getParmArray()[i] = VD; }

  TypeLoc getReturnLoc() const {
    return getInnerTypeLoc();
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLocalRangeBegin(), getLocalRangeEnd());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setLocalRangeBegin(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
    setLocalRangeEnd(Loc);
    for (unsigned i = 0, e = getNumParams(); i != e; ++i)
      setParam(i, nullptr);
    if (hasExceptionSpec())
      setExceptionSpecRange(Loc);
  }

  /// Returns the size of the type source info data block that is
  /// specific to this type.
  unsigned getExtraLocalDataSize() const {
    unsigned ExceptSpecSize = hasExceptionSpec() ? sizeof(SourceRange) : 0;
    return (getNumParams() * sizeof(ParmVarDecl *)) + ExceptSpecSize;
  }

  unsigned getExtraLocalDataAlignment() const { return alignof(ParmVarDecl *); }

  QualType getInnerType() const { return getTypePtr()->getReturnType(); }
};

class FunctionProtoTypeLoc :
    public InheritingConcreteTypeLoc<FunctionTypeLoc,
                                     FunctionProtoTypeLoc,
                                     FunctionProtoType> {
};

class FunctionNoProtoTypeLoc :
    public InheritingConcreteTypeLoc<FunctionTypeLoc,
                                     FunctionNoProtoTypeLoc,
                                     FunctionNoProtoType> {
};

struct ArrayLocInfo {
  SourceLocation LBracketLoc, RBracketLoc;
  Expr *Size;
};

/// Wrapper for source info for arrays.
class ArrayTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                            ArrayTypeLoc,
                                            ArrayType,
                                            ArrayLocInfo> {
public:
  SourceLocation getLBracketLoc() const {
    return getLocalData()->LBracketLoc;
  }

  void setLBracketLoc(SourceLocation Loc) {
    getLocalData()->LBracketLoc = Loc;
  }

  SourceLocation getRBracketLoc() const {
    return getLocalData()->RBracketLoc;
  }

  void setRBracketLoc(SourceLocation Loc) {
    getLocalData()->RBracketLoc = Loc;
  }

  SourceRange getBracketsRange() const {
    return SourceRange(getLBracketLoc(), getRBracketLoc());
  }

  Expr *getSizeExpr() const {
    return getLocalData()->Size;
  }

  void setSizeExpr(Expr *Size) {
    getLocalData()->Size = Size;
  }

  TypeLoc getElementLoc() const {
    return getInnerTypeLoc();
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLBracketLoc(), getRBracketLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setLBracketLoc(Loc);
    setRBracketLoc(Loc);
    setSizeExpr(nullptr);
  }

  QualType getInnerType() const { return getTypePtr()->getElementType(); }
};

class ConstantArrayTypeLoc :
    public InheritingConcreteTypeLoc<ArrayTypeLoc,
                                     ConstantArrayTypeLoc,
                                     ConstantArrayType> {
};

class IncompleteArrayTypeLoc :
    public InheritingConcreteTypeLoc<ArrayTypeLoc,
                                     IncompleteArrayTypeLoc,
                                     IncompleteArrayType> {
};

class DependentSizedArrayTypeLoc :
    public InheritingConcreteTypeLoc<ArrayTypeLoc,
                                     DependentSizedArrayTypeLoc,
                                     DependentSizedArrayType> {
public:
  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    ArrayTypeLoc::initializeLocal(Context, Loc);
    setSizeExpr(getTypePtr()->getSizeExpr());
  }
};

class VariableArrayTypeLoc :
    public InheritingConcreteTypeLoc<ArrayTypeLoc,
                                     VariableArrayTypeLoc,
                                     VariableArrayType> {
};

// Location information for a TemplateName.  Rudimentary for now.
struct TemplateNameLocInfo {
  SourceLocation NameLoc;
};

struct TemplateSpecializationLocInfo : TemplateNameLocInfo {
  SourceLocation TemplateKWLoc;
  SourceLocation LAngleLoc;
  SourceLocation RAngleLoc;
};

class TemplateSpecializationTypeLoc :
    public ConcreteTypeLoc<UnqualTypeLoc,
                           TemplateSpecializationTypeLoc,
                           TemplateSpecializationType,
                           TemplateSpecializationLocInfo> {
public:
  SourceLocation getTemplateKeywordLoc() const {
    return getLocalData()->TemplateKWLoc;
  }

  void setTemplateKeywordLoc(SourceLocation Loc) {
    getLocalData()->TemplateKWLoc = Loc;
  }

  SourceLocation getLAngleLoc() const {
    return getLocalData()->LAngleLoc;
  }

  void setLAngleLoc(SourceLocation Loc) {
    getLocalData()->LAngleLoc = Loc;
  }

  SourceLocation getRAngleLoc() const {
    return getLocalData()->RAngleLoc;
  }

  void setRAngleLoc(SourceLocation Loc) {
    getLocalData()->RAngleLoc = Loc;
  }

  unsigned getNumArgs() const {
    return getTypePtr()->getNumArgs();
  }

  void setArgLocInfo(unsigned i, TemplateArgumentLocInfo AI) {
    getArgInfos()[i] = AI;
  }

  TemplateArgumentLocInfo getArgLocInfo(unsigned i) const {
    return getArgInfos()[i];
  }

  TemplateArgumentLoc getArgLoc(unsigned i) const {
    return TemplateArgumentLoc(getTypePtr()->getArg(i), getArgLocInfo(i));
  }

  SourceLocation getTemplateNameLoc() const {
    return getLocalData()->NameLoc;
  }

  void setTemplateNameLoc(SourceLocation Loc) {
    getLocalData()->NameLoc = Loc;
  }

  /// - Copy the location information from the given info.
  void copy(TemplateSpecializationTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());

    // We're potentially copying Expr references here.  We don't
    // bother retaining them because TypeSourceInfos live forever, so
    // as long as the Expr was retained when originally written into
    // the TypeLoc, we're okay.
    memcpy(Data, Loc.Data, size);
  }

  SourceRange getLocalSourceRange() const {
    if (getTemplateKeywordLoc().isValid())
      return SourceRange(getTemplateKeywordLoc(), getRAngleLoc());
    else
      return SourceRange(getTemplateNameLoc(), getRAngleLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setTemplateKeywordLoc(Loc);
    setTemplateNameLoc(Loc);
    setLAngleLoc(Loc);
    setRAngleLoc(Loc);
    initializeArgLocs(Context, getNumArgs(), getTypePtr()->getArgs(),
                      getArgInfos(), Loc);
  }

  static void initializeArgLocs(ASTContext &Context, unsigned NumArgs,
                                const TemplateArgument *Args,
                                TemplateArgumentLocInfo *ArgInfos,
                                SourceLocation Loc);

  unsigned getExtraLocalDataSize() const {
    return getNumArgs() * sizeof(TemplateArgumentLocInfo);
  }

  unsigned getExtraLocalDataAlignment() const {
    return alignof(TemplateArgumentLocInfo);
  }

private:
  TemplateArgumentLocInfo *getArgInfos() const {
    return static_cast<TemplateArgumentLocInfo*>(getExtraLocalData());
  }
};

struct DependentAddressSpaceLocInfo {
  Expr *ExprOperand;
  SourceRange OperandParens;
  SourceLocation AttrLoc;
};

class DependentAddressSpaceTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc,
                             DependentAddressSpaceTypeLoc,
                             DependentAddressSpaceType,
                             DependentAddressSpaceLocInfo> {
public:
  /// The location of the attribute name, i.e.
  ///    int * __attribute__((address_space(11)))
  ///                         ^~~~~~~~~~~~~
  SourceLocation getAttrNameLoc() const {
    return getLocalData()->AttrLoc;
  }
  void setAttrNameLoc(SourceLocation loc) {
    getLocalData()->AttrLoc = loc;
  }

  /// The attribute's expression operand, if it has one.
  ///    int * __attribute__((address_space(11)))
  ///                                       ^~
  Expr *getAttrExprOperand() const {
    return getLocalData()->ExprOperand;
  }
  void setAttrExprOperand(Expr *e) {
    getLocalData()->ExprOperand = e;
  }

  /// The location of the parentheses around the operand, if there is
  /// an operand.
  ///    int * __attribute__((address_space(11)))
  ///                                      ^  ^
  SourceRange getAttrOperandParensRange() const {
    return getLocalData()->OperandParens;
  }
  void setAttrOperandParensRange(SourceRange range) {
    getLocalData()->OperandParens = range;
  }

  SourceRange getLocalSourceRange() const {
    SourceRange range(getAttrNameLoc());
    range.setEnd(getAttrOperandParensRange().getEnd());
    return range;
  }

  ///  Returns the type before the address space attribute application
  ///  area.
  ///    int * __attribute__((address_space(11))) *
  ///    ^   ^
  QualType getInnerType() const {
    return this->getTypePtr()->getPointeeType();
  }

  TypeLoc getPointeeTypeLoc() const {
    return this->getInnerTypeLoc();
  }

  void initializeLocal(ASTContext &Context, SourceLocation loc) {
    setAttrNameLoc(loc);
    setAttrOperandParensRange(SourceRange(loc));
    setAttrExprOperand(getTypePtr()->getAddrSpaceExpr());
  }
};

//===----------------------------------------------------------------------===//
//
//  All of these need proper implementations.
//
//===----------------------------------------------------------------------===//

// FIXME: size expression and attribute locations (or keyword if we
// ever fully support altivec syntax).
class VectorTypeLoc : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                                       VectorTypeLoc,
                                                       VectorType> {
};

// FIXME: size expression and attribute locations (or keyword if we
// ever fully support altivec syntax).
class DependentVectorTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                       DependentVectorTypeLoc,
                                       DependentVectorType> {};

// FIXME: size expression and attribute locations.
class ExtVectorTypeLoc : public InheritingConcreteTypeLoc<VectorTypeLoc,
                                                          ExtVectorTypeLoc,
                                                          ExtVectorType> {
};

// FIXME: attribute locations.
// For some reason, this isn't a subtype of VectorType.
class DependentSizedExtVectorTypeLoc :
    public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                     DependentSizedExtVectorTypeLoc,
                                     DependentSizedExtVectorType> {
};

// FIXME: location of the '_Complex' keyword.
class ComplexTypeLoc : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                                        ComplexTypeLoc,
                                                        ComplexType> {
};

struct TypeofLocInfo {
  SourceLocation TypeofLoc;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
};

struct TypeOfExprTypeLocInfo : public TypeofLocInfo {
};

struct TypeOfTypeLocInfo : public TypeofLocInfo {
  TypeSourceInfo* UnderlyingTInfo;
};

template <class Derived, class TypeClass, class LocalData = TypeofLocInfo>
class TypeofLikeTypeLoc
  : public ConcreteTypeLoc<UnqualTypeLoc, Derived, TypeClass, LocalData> {
public:
  SourceLocation getTypeofLoc() const {
    return this->getLocalData()->TypeofLoc;
  }

  void setTypeofLoc(SourceLocation Loc) {
    this->getLocalData()->TypeofLoc = Loc;
  }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void setParensRange(SourceRange range) {
      setLParenLoc(range.getBegin());
      setRParenLoc(range.getEnd());
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getTypeofLoc(), getRParenLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setTypeofLoc(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }
};

class TypeOfExprTypeLoc : public TypeofLikeTypeLoc<TypeOfExprTypeLoc,
                                                   TypeOfExprType,
                                                   TypeOfExprTypeLocInfo> {
public:
  Expr* getUnderlyingExpr() const {
    return getTypePtr()->getUnderlyingExpr();
  }

  // Reimplemented to account for GNU/C++ extension
  //     typeof unary-expression
  // where there are no parentheses.
  SourceRange getLocalSourceRange() const;
};

class TypeOfTypeLoc
  : public TypeofLikeTypeLoc<TypeOfTypeLoc, TypeOfType, TypeOfTypeLocInfo> {
public:
  QualType getUnderlyingType() const {
    return this->getTypePtr()->getUnderlyingType();
  }

  TypeSourceInfo* getUnderlyingTInfo() const {
    return this->getLocalData()->UnderlyingTInfo;
  }

  void setUnderlyingTInfo(TypeSourceInfo* TI) const {
    this->getLocalData()->UnderlyingTInfo = TI;
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);
};

// FIXME: location of the 'decltype' and parens.
class DecltypeTypeLoc : public InheritingConcreteTypeLoc<TypeSpecTypeLoc,
                                                         DecltypeTypeLoc,
                                                         DecltypeType> {
public:
  Expr *getUnderlyingExpr() const { return getTypePtr()->getUnderlyingExpr(); }
};

struct UnaryTransformTypeLocInfo {
  // FIXME: While there's only one unary transform right now, future ones may
  // need different representations
  SourceLocation KWLoc, LParenLoc, RParenLoc;
  TypeSourceInfo *UnderlyingTInfo;
};

class UnaryTransformTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                                    UnaryTransformTypeLoc,
                                                    UnaryTransformType,
                                                    UnaryTransformTypeLocInfo> {
public:
  SourceLocation getKWLoc() const { return getLocalData()->KWLoc; }
  void setKWLoc(SourceLocation Loc) { getLocalData()->KWLoc = Loc; }

  SourceLocation getLParenLoc() const { return getLocalData()->LParenLoc; }
  void setLParenLoc(SourceLocation Loc) { getLocalData()->LParenLoc = Loc; }

  SourceLocation getRParenLoc() const { return getLocalData()->RParenLoc; }
  void setRParenLoc(SourceLocation Loc) { getLocalData()->RParenLoc = Loc; }

  TypeSourceInfo* getUnderlyingTInfo() const {
    return getLocalData()->UnderlyingTInfo;
  }

  void setUnderlyingTInfo(TypeSourceInfo *TInfo) {
    getLocalData()->UnderlyingTInfo = TInfo;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getKWLoc(), getRParenLoc());
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void setParensRange(SourceRange Range) {
    setLParenLoc(Range.getBegin());
    setRParenLoc(Range.getEnd());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);
};

class DeducedTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, DeducedTypeLoc,
                                       DeducedType> {};

class AutoTypeLoc
    : public InheritingConcreteTypeLoc<DeducedTypeLoc, AutoTypeLoc, AutoType> {
};

class DeducedTemplateSpecializationTypeLoc
    : public InheritingConcreteTypeLoc<DeducedTypeLoc,
                                       DeducedTemplateSpecializationTypeLoc,
                                       DeducedTemplateSpecializationType> {
public:
  SourceLocation getTemplateNameLoc() const {
    return getNameLoc();
  }

  void setTemplateNameLoc(SourceLocation Loc) {
    setNameLoc(Loc);
  }
};

struct ElaboratedLocInfo {
  SourceLocation ElaboratedKWLoc;

  /// Data associated with the nested-name-specifier location.
  void *QualifierData;
};

class ElaboratedTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                                 ElaboratedTypeLoc,
                                                 ElaboratedType,
                                                 ElaboratedLocInfo> {
public:
  SourceLocation getElaboratedKeywordLoc() const {
    return this->getLocalData()->ElaboratedKWLoc;
  }

  void setElaboratedKeywordLoc(SourceLocation Loc) {
    this->getLocalData()->ElaboratedKWLoc = Loc;
  }

  NestedNameSpecifierLoc getQualifierLoc() const {
    return NestedNameSpecifierLoc(getTypePtr()->getQualifier(),
                                  getLocalData()->QualifierData);
  }

  void setQualifierLoc(NestedNameSpecifierLoc QualifierLoc) {
    assert(QualifierLoc.getNestedNameSpecifier()
                                            == getTypePtr()->getQualifier() &&
           "Inconsistent nested-name-specifier pointer");
    getLocalData()->QualifierData = QualifierLoc.getOpaqueData();
  }

  SourceRange getLocalSourceRange() const {
    if (getElaboratedKeywordLoc().isValid())
      if (getQualifierLoc())
        return SourceRange(getElaboratedKeywordLoc(),
                           getQualifierLoc().getEndLoc());
      else
        return SourceRange(getElaboratedKeywordLoc());
    else
      return getQualifierLoc().getSourceRange();
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);

  TypeLoc getNamedTypeLoc() const {
    return getInnerTypeLoc();
  }

  QualType getInnerType() const {
    return getTypePtr()->getNamedType();
  }

  void copy(ElaboratedTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());
    memcpy(Data, Loc.Data, size);
  }
};

// This is exactly the structure of an ElaboratedTypeLoc whose inner
// type is some sort of TypeDeclTypeLoc.
struct DependentNameLocInfo : ElaboratedLocInfo {
  SourceLocation NameLoc;
};

class DependentNameTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc,
                                                    DependentNameTypeLoc,
                                                    DependentNameType,
                                                    DependentNameLocInfo> {
public:
  SourceLocation getElaboratedKeywordLoc() const {
    return this->getLocalData()->ElaboratedKWLoc;
  }

  void setElaboratedKeywordLoc(SourceLocation Loc) {
    this->getLocalData()->ElaboratedKWLoc = Loc;
  }

  NestedNameSpecifierLoc getQualifierLoc() const {
    return NestedNameSpecifierLoc(getTypePtr()->getQualifier(),
                                  getLocalData()->QualifierData);
  }

  void setQualifierLoc(NestedNameSpecifierLoc QualifierLoc) {
    assert(QualifierLoc.getNestedNameSpecifier()
                                            == getTypePtr()->getQualifier() &&
           "Inconsistent nested-name-specifier pointer");
    getLocalData()->QualifierData = QualifierLoc.getOpaqueData();
  }

  SourceLocation getNameLoc() const {
    return this->getLocalData()->NameLoc;
  }

  void setNameLoc(SourceLocation Loc) {
    this->getLocalData()->NameLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    if (getElaboratedKeywordLoc().isValid())
      return SourceRange(getElaboratedKeywordLoc(), getNameLoc());
    else
      return SourceRange(getQualifierLoc().getBeginLoc(), getNameLoc());
  }

  void copy(DependentNameTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());
    memcpy(Data, Loc.Data, size);
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);
};

struct DependentTemplateSpecializationLocInfo : DependentNameLocInfo {
  SourceLocation TemplateKWLoc;
  SourceLocation LAngleLoc;
  SourceLocation RAngleLoc;
  // followed by a TemplateArgumentLocInfo[]
};

class DependentTemplateSpecializationTypeLoc :
    public ConcreteTypeLoc<UnqualTypeLoc,
                           DependentTemplateSpecializationTypeLoc,
                           DependentTemplateSpecializationType,
                           DependentTemplateSpecializationLocInfo> {
public:
  SourceLocation getElaboratedKeywordLoc() const {
    return this->getLocalData()->ElaboratedKWLoc;
  }

  void setElaboratedKeywordLoc(SourceLocation Loc) {
    this->getLocalData()->ElaboratedKWLoc = Loc;
  }

  NestedNameSpecifierLoc getQualifierLoc() const {
    if (!getLocalData()->QualifierData)
      return NestedNameSpecifierLoc();

    return NestedNameSpecifierLoc(getTypePtr()->getQualifier(),
                                  getLocalData()->QualifierData);
  }

  void setQualifierLoc(NestedNameSpecifierLoc QualifierLoc) {
    if (!QualifierLoc) {
      // Even if we have a nested-name-specifier in the dependent
      // template specialization type, we won't record the nested-name-specifier
      // location information when this type-source location information is
      // part of a nested-name-specifier.
      getLocalData()->QualifierData = nullptr;
      return;
    }

    assert(QualifierLoc.getNestedNameSpecifier()
                                        == getTypePtr()->getQualifier() &&
           "Inconsistent nested-name-specifier pointer");
    getLocalData()->QualifierData = QualifierLoc.getOpaqueData();
  }

  SourceLocation getTemplateKeywordLoc() const {
    return getLocalData()->TemplateKWLoc;
  }

  void setTemplateKeywordLoc(SourceLocation Loc) {
    getLocalData()->TemplateKWLoc = Loc;
  }

  SourceLocation getTemplateNameLoc() const {
    return this->getLocalData()->NameLoc;
  }

  void setTemplateNameLoc(SourceLocation Loc) {
    this->getLocalData()->NameLoc = Loc;
  }

  SourceLocation getLAngleLoc() const {
    return this->getLocalData()->LAngleLoc;
  }

  void setLAngleLoc(SourceLocation Loc) {
    this->getLocalData()->LAngleLoc = Loc;
  }

  SourceLocation getRAngleLoc() const {
    return this->getLocalData()->RAngleLoc;
  }

  void setRAngleLoc(SourceLocation Loc) {
    this->getLocalData()->RAngleLoc = Loc;
  }

  unsigned getNumArgs() const {
    return getTypePtr()->getNumArgs();
  }

  void setArgLocInfo(unsigned i, TemplateArgumentLocInfo AI) {
    getArgInfos()[i] = AI;
  }

  TemplateArgumentLocInfo getArgLocInfo(unsigned i) const {
    return getArgInfos()[i];
  }

  TemplateArgumentLoc getArgLoc(unsigned i) const {
    return TemplateArgumentLoc(getTypePtr()->getArg(i), getArgLocInfo(i));
  }

  SourceRange getLocalSourceRange() const {
    if (getElaboratedKeywordLoc().isValid())
      return SourceRange(getElaboratedKeywordLoc(), getRAngleLoc());
    else if (getQualifierLoc())
      return SourceRange(getQualifierLoc().getBeginLoc(), getRAngleLoc());
    else if (getTemplateKeywordLoc().isValid())
      return SourceRange(getTemplateKeywordLoc(), getRAngleLoc());
    else
      return SourceRange(getTemplateNameLoc(), getRAngleLoc());
  }

  void copy(DependentTemplateSpecializationTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());
    memcpy(Data, Loc.Data, size);
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc);

  unsigned getExtraLocalDataSize() const {
    return getNumArgs() * sizeof(TemplateArgumentLocInfo);
  }

  unsigned getExtraLocalDataAlignment() const {
    return alignof(TemplateArgumentLocInfo);
  }

private:
  TemplateArgumentLocInfo *getArgInfos() const {
    return static_cast<TemplateArgumentLocInfo*>(getExtraLocalData());
  }
};

struct PackExpansionTypeLocInfo {
  SourceLocation EllipsisLoc;
};

class PackExpansionTypeLoc
  : public ConcreteTypeLoc<UnqualTypeLoc, PackExpansionTypeLoc,
                           PackExpansionType, PackExpansionTypeLocInfo> {
public:
  SourceLocation getEllipsisLoc() const {
    return this->getLocalData()->EllipsisLoc;
  }

  void setEllipsisLoc(SourceLocation Loc) {
    this->getLocalData()->EllipsisLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getEllipsisLoc(), getEllipsisLoc());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setEllipsisLoc(Loc);
  }

  TypeLoc getPatternLoc() const {
    return getInnerTypeLoc();
  }

  QualType getInnerType() const {
    return this->getTypePtr()->getPattern();
  }
};

struct AtomicTypeLocInfo {
  SourceLocation KWLoc, LParenLoc, RParenLoc;
};

class AtomicTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, AtomicTypeLoc,
                                             AtomicType, AtomicTypeLocInfo> {
public:
  TypeLoc getValueLoc() const {
    return this->getInnerTypeLoc();
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getKWLoc(), getRParenLoc());
  }

  SourceLocation getKWLoc() const {
    return this->getLocalData()->KWLoc;
  }

  void setKWLoc(SourceLocation Loc) {
    this->getLocalData()->KWLoc = Loc;
  }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void setParensRange(SourceRange Range) {
    setLParenLoc(Range.getBegin());
    setRParenLoc(Range.getEnd());
  }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setKWLoc(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }

  QualType getInnerType() const {
    return this->getTypePtr()->getValueType();
  }
};

struct PipeTypeLocInfo {
  SourceLocation KWLoc;
};

class PipeTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, PipeTypeLoc, PipeType,
                                           PipeTypeLocInfo> {
public:
  TypeLoc getValueLoc() const { return this->getInnerTypeLoc(); }

  SourceRange getLocalSourceRange() const { return SourceRange(getKWLoc()); }

  SourceLocation getKWLoc() const { return this->getLocalData()->KWLoc; }
  void setKWLoc(SourceLocation Loc) { this->getLocalData()->KWLoc = Loc; }

  void initializeLocal(ASTContext &Context, SourceLocation Loc) {
    setKWLoc(Loc);
  }

  QualType getInnerType() const { return this->getTypePtr()->getElementType(); }
};

template <typename T>
inline T TypeLoc::getAsAdjusted() const {
  TypeLoc Cur = *this;
  while (!T::isKind(Cur)) {
    if (auto PTL = Cur.getAs<ParenTypeLoc>())
      Cur = PTL.getInnerLoc();
    else if (auto ATL = Cur.getAs<AttributedTypeLoc>())
      Cur = ATL.getModifiedLoc();
    else if (auto ETL = Cur.getAs<ElaboratedTypeLoc>())
      Cur = ETL.getNamedTypeLoc();
    else if (auto ATL = Cur.getAs<AdjustedTypeLoc>())
      Cur = ATL.getOriginalLoc();
    else
      break;
  }
  return Cur.getAs<T>();
}

} // namespace clang

#endif // LLVM_CLANG_AST_TYPELOC_H
