//======- ParsedAttr.h - Parsed attribute sets ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ParsedAttr class, which is used to collect
// parsed attributes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_ATTRIBUTELIST_H
#define LLVM_CLANG_SEMA_ATTRIBUTELIST_H

#include "clang/Basic/AttrSubjectMatchRules.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/VersionTuple.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>

namespace clang {

class ASTContext;
class Decl;
class Expr;
class IdentifierInfo;
class LangOptions;

/// Represents information about a change in availability for
/// an entity, which is part of the encoding of the 'availability'
/// attribute.
struct AvailabilityChange {
  /// The location of the keyword indicating the kind of change.
  SourceLocation KeywordLoc;

  /// The version number at which the change occurred.
  VersionTuple Version;

  /// The source range covering the version number.
  SourceRange VersionRange;

  /// Determine whether this availability change is valid.
  bool isValid() const { return !Version.empty(); }
};

namespace detail {
enum AvailabilitySlot {
  IntroducedSlot, DeprecatedSlot, ObsoletedSlot, NumAvailabilitySlots
};

/// Describes the trailing object for Availability attribute in ParsedAttr.
struct AvailabilityData {
  AvailabilityChange Changes[NumAvailabilitySlots];
  SourceLocation StrictLoc;
  const Expr *Replacement;

  AvailabilityData(const AvailabilityChange &Introduced,
                   const AvailabilityChange &Deprecated,
                   const AvailabilityChange &Obsoleted,
                   SourceLocation Strict, const Expr *ReplaceExpr)
    : StrictLoc(Strict), Replacement(ReplaceExpr) {
    Changes[IntroducedSlot] = Introduced;
    Changes[DeprecatedSlot] = Deprecated;
    Changes[ObsoletedSlot] = Obsoleted;
  }
};

struct TypeTagForDatatypeData {
  ParsedType MatchingCType;
  unsigned LayoutCompatible : 1;
  unsigned MustBeNull : 1;
};
struct PropertyData {
  IdentifierInfo *GetterId, *SetterId;

  PropertyData(IdentifierInfo *getterId, IdentifierInfo *setterId)
      : GetterId(getterId), SetterId(setterId) {}
};

} // namespace

/// Wraps an identifier and optional source location for the identifier.
struct IdentifierLoc {
  SourceLocation Loc;
  IdentifierInfo *Ident;

  static IdentifierLoc *create(ASTContext &Ctx, SourceLocation Loc,
                               IdentifierInfo *Ident);
};

/// A union of the various pointer types that can be passed to an
/// ParsedAttr as an argument.
using ArgsUnion = llvm::PointerUnion<Expr *, IdentifierLoc *>;
using ArgsVector = llvm::SmallVector<ArgsUnion, 12U>;

/// ParsedAttr - Represents a syntactic attribute.
///
/// For a GNU attribute, there are four forms of this construct:
///
/// 1: __attribute__(( const )). ParmName/Args/NumArgs will all be unused.
/// 2: __attribute__(( mode(byte) )). ParmName used, Args/NumArgs unused.
/// 3: __attribute__(( format(printf, 1, 2) )). ParmName/Args/NumArgs all used.
/// 4: __attribute__(( aligned(16) )). ParmName is unused, Args/Num used.
///
class ParsedAttr final
    : private llvm::TrailingObjects<
          ParsedAttr, ArgsUnion, detail::AvailabilityData,
          detail::TypeTagForDatatypeData, ParsedType, detail::PropertyData> {
  friend TrailingObjects;

  size_t numTrailingObjects(OverloadToken<ArgsUnion>) const { return NumArgs; }
  size_t numTrailingObjects(OverloadToken<detail::AvailabilityData>) const {
    return IsAvailability;
  }
  size_t
      numTrailingObjects(OverloadToken<detail::TypeTagForDatatypeData>) const {
    return IsTypeTagForDatatype;
  }
  size_t numTrailingObjects(OverloadToken<ParsedType>) const {
    return HasParsedType;
  }
  size_t numTrailingObjects(OverloadToken<detail::PropertyData>) const {
    return IsProperty;
  }

public:
  /// The style used to specify an attribute.
  enum Syntax {
    /// __attribute__((...))
    AS_GNU,

    /// [[...]]
    AS_CXX11,

    /// [[...]]
    AS_C2x,

    /// __declspec(...)
    AS_Declspec,

    /// [uuid("...")] class Foo
    AS_Microsoft,

    /// __ptr16, alignas(...), etc.
    AS_Keyword,

    /// #pragma ...
    AS_Pragma,

    // Note TableGen depends on the order above.  Do not add or change the order
    // without adding related code to TableGen/ClangAttrEmitter.cpp.
    /// Context-sensitive version of a keyword attribute.
    AS_ContextSensitiveKeyword,
  };

private:
  IdentifierInfo *AttrName;
  IdentifierInfo *ScopeName;
  SourceRange AttrRange;
  SourceLocation ScopeLoc;
  SourceLocation EllipsisLoc;

  unsigned AttrKind : 16;

  /// The number of expression arguments this attribute has.
  /// The expressions themselves are stored after the object.
  unsigned NumArgs : 16;

  /// Corresponds to the Syntax enum.
  unsigned SyntaxUsed : 3;

  /// True if already diagnosed as invalid.
  mutable unsigned Invalid : 1;

  /// True if this attribute was used as a type attribute.
  mutable unsigned UsedAsTypeAttr : 1;

  /// True if this has the extra information associated with an
  /// availability attribute.
  unsigned IsAvailability : 1;

  /// True if this has extra information associated with a
  /// type_tag_for_datatype attribute.
  unsigned IsTypeTagForDatatype : 1;

  /// True if this has extra information associated with a
  /// Microsoft __delcspec(property) attribute.
  unsigned IsProperty : 1;

  /// True if this has a ParsedType
  unsigned HasParsedType : 1;

  /// True if the processing cache is valid.
  mutable unsigned HasProcessingCache : 1;

  /// A cached value.
  mutable unsigned ProcessingCache : 8;

  /// The location of the 'unavailable' keyword in an
  /// availability attribute.
  SourceLocation UnavailableLoc;

  const Expr *MessageExpr;

  ArgsUnion *getArgsBuffer() { return getTrailingObjects<ArgsUnion>(); }
  ArgsUnion const *getArgsBuffer() const {
    return getTrailingObjects<ArgsUnion>();
  }

  detail::AvailabilityData *getAvailabilityData() {
    return getTrailingObjects<detail::AvailabilityData>();
  }
  const detail::AvailabilityData *getAvailabilityData() const {
    return getTrailingObjects<detail::AvailabilityData>();
  }

private:
  friend class AttributeFactory;
  friend class AttributePool;

  /// Constructor for attributes with expression arguments.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ArgsUnion *args, unsigned numArgs, Syntax syntaxUsed,
             SourceLocation ellipsisLoc)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), EllipsisLoc(ellipsisLoc), NumArgs(numArgs),
        SyntaxUsed(syntaxUsed), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(false), IsProperty(false),
        HasParsedType(false), HasProcessingCache(false) {
    if (numArgs) memcpy(getArgsBuffer(), args, numArgs * sizeof(ArgsUnion));
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Constructor for availability attributes.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *Parm, const AvailabilityChange &introduced,
             const AvailabilityChange &deprecated,
             const AvailabilityChange &obsoleted, SourceLocation unavailable,
             const Expr *messageExpr, Syntax syntaxUsed, SourceLocation strict,
             const Expr *replacementExpr)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), NumArgs(1), SyntaxUsed(syntaxUsed), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(true),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(false),
        HasProcessingCache(false), UnavailableLoc(unavailable),
        MessageExpr(messageExpr) {
    ArgsUnion PVal(Parm);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    new (getAvailabilityData()) detail::AvailabilityData(
        introduced, deprecated, obsoleted, strict, replacementExpr);
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Constructor for objc_bridge_related attributes.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *Parm1, IdentifierLoc *Parm2, IdentifierLoc *Parm3,
             Syntax syntaxUsed)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), NumArgs(3), SyntaxUsed(syntaxUsed), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(false),
        HasProcessingCache(false) {
    ArgsUnion *Args = getArgsBuffer();
    Args[0] = Parm1;
    Args[1] = Parm2;
    Args[2] = Parm3;
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Constructor for type_tag_for_datatype attribute.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *ArgKind, ParsedType matchingCType,
             bool layoutCompatible, bool mustBeNull, Syntax syntaxUsed)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), NumArgs(1), SyntaxUsed(syntaxUsed), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(true), IsProperty(false), HasParsedType(false),
        HasProcessingCache(false) {
    ArgsUnion PVal(ArgKind);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    detail::TypeTagForDatatypeData &ExtraData = getTypeTagForDatatypeDataSlot();
    new (&ExtraData.MatchingCType) ParsedType(matchingCType);
    ExtraData.LayoutCompatible = layoutCompatible;
    ExtraData.MustBeNull = mustBeNull;
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Constructor for attributes with a single type argument.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ParsedType typeArg, Syntax syntaxUsed)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), NumArgs(0), SyntaxUsed(syntaxUsed), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(true),
        HasProcessingCache(false) {
    new (&getTypeBuffer()) ParsedType(typeArg);
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Constructor for microsoft __declspec(property) attribute.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierInfo *getterId, IdentifierInfo *setterId,
             Syntax syntaxUsed)
      : AttrName(attrName), ScopeName(scopeName), AttrRange(attrRange),
        ScopeLoc(scopeLoc), NumArgs(0), SyntaxUsed(syntaxUsed), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(false), IsProperty(true), HasParsedType(false),
        HasProcessingCache(false) {
    new (&getPropertyDataBuffer()) detail::PropertyData(getterId, setterId);
    AttrKind = getKind(getName(), getScopeName(), syntaxUsed);
  }

  /// Type tag information is stored immediately following the arguments, if
  /// any, at the end of the object.  They are mutually exclusive with
  /// availability slots.
  detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }
  const detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() const {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }

  /// The type buffer immediately follows the object and are mutually exclusive
  /// with arguments.
  ParsedType &getTypeBuffer() { return *getTrailingObjects<ParsedType>(); }
  const ParsedType &getTypeBuffer() const {
    return *getTrailingObjects<ParsedType>();
  }

  /// The property data immediately follows the object is is mutually exclusive
  /// with arguments.
  detail::PropertyData &getPropertyDataBuffer() {
    assert(IsProperty);
    return *getTrailingObjects<detail::PropertyData>();
  }
  const detail::PropertyData &getPropertyDataBuffer() const {
    assert(IsProperty);
    return *getTrailingObjects<detail::PropertyData>();
  }

  size_t allocated_size() const;

public:
  ParsedAttr(const ParsedAttr &) = delete;
  ParsedAttr(ParsedAttr &&) = delete;
  ParsedAttr &operator=(const ParsedAttr &) = delete;
  ParsedAttr &operator=(ParsedAttr &&) = delete;
  ~ParsedAttr() = delete;

  void operator delete(void *) = delete;

  enum Kind {
    #define PARSED_ATTR(NAME) AT_##NAME,
    #include "clang/Sema/AttrParsedAttrList.inc"
    #undef PARSED_ATTR
    IgnoredAttribute,
    UnknownAttribute
  };

  IdentifierInfo *getName() const { return AttrName; }
  SourceLocation getLoc() const { return AttrRange.getBegin(); }
  SourceRange getRange() const { return AttrRange; }

  bool hasScope() const { return ScopeName; }
  IdentifierInfo *getScopeName() const { return ScopeName; }
  SourceLocation getScopeLoc() const { return ScopeLoc; }

  bool isGNUScope() const {
    return ScopeName &&
           (ScopeName->isStr("gnu") || ScopeName->isStr("__gnu__"));
  }

  bool hasParsedType() const { return HasParsedType; }

  /// Is this the Microsoft __declspec(property) attribute?
  bool isDeclspecPropertyAttribute() const  {
    return IsProperty;
  }

  bool isAlignasAttribute() const {
    // FIXME: Use a better mechanism to determine this.
    return getKind() == AT_Aligned && isKeywordAttribute();
  }

  bool isDeclspecAttribute() const { return SyntaxUsed == AS_Declspec; }
  bool isMicrosoftAttribute() const { return SyntaxUsed == AS_Microsoft; }

  bool isCXX11Attribute() const {
    return SyntaxUsed == AS_CXX11 || isAlignasAttribute();
  }

  bool isC2xAttribute() const {
    return SyntaxUsed == AS_C2x;
  }

  bool isKeywordAttribute() const {
    return SyntaxUsed == AS_Keyword || SyntaxUsed == AS_ContextSensitiveKeyword;
  }

  bool isContextSensitiveKeywordAttribute() const {
    return SyntaxUsed == AS_ContextSensitiveKeyword;
  }

  bool isInvalid() const { return Invalid; }
  void setInvalid(bool b = true) const { Invalid = b; }

  bool hasProcessingCache() const { return HasProcessingCache; }

  unsigned getProcessingCache() const {
    assert(hasProcessingCache());
    return ProcessingCache;
  }

  void setProcessingCache(unsigned value) const {
    ProcessingCache = value;
    HasProcessingCache = true;
  }

  bool isUsedAsTypeAttr() const { return UsedAsTypeAttr; }
  void setUsedAsTypeAttr() { UsedAsTypeAttr = true; }

  bool isPackExpansion() const { return EllipsisLoc.isValid(); }
  SourceLocation getEllipsisLoc() const { return EllipsisLoc; }

  Kind getKind() const { return Kind(AttrKind); }
  static Kind getKind(const IdentifierInfo *Name, const IdentifierInfo *Scope,
                      Syntax SyntaxUsed);

  /// getNumArgs - Return the number of actual arguments to this attribute.
  unsigned getNumArgs() const { return NumArgs; }

  /// getArg - Return the specified argument.
  ArgsUnion getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return getArgsBuffer()[Arg];
  }

  bool isArgExpr(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<Expr*>();
  }

  Expr *getArgAsExpr(unsigned Arg) const {
    return getArg(Arg).get<Expr*>();
  }

  bool isArgIdent(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<IdentifierLoc*>();
  }

  IdentifierLoc *getArgAsIdent(unsigned Arg) const {
    return getArg(Arg).get<IdentifierLoc*>();
  }

  const AvailabilityChange &getAvailabilityIntroduced() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::IntroducedSlot];
  }

  const AvailabilityChange &getAvailabilityDeprecated() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::DeprecatedSlot];
  }

  const AvailabilityChange &getAvailabilityObsoleted() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::ObsoletedSlot];
  }

  SourceLocation getStrictLoc() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return getAvailabilityData()->StrictLoc;
  }

  SourceLocation getUnavailableLoc() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return UnavailableLoc;
  }

  const Expr * getMessageExpr() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return MessageExpr;
  }

  const Expr *getReplacementExpr() const {
    assert(getKind() == AT_Availability && "Not an availability attribute");
    return getAvailabilityData()->Replacement;
  }

  const ParsedType &getMatchingCType() const {
    assert(getKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MatchingCType;
  }

  bool getLayoutCompatible() const {
    assert(getKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().LayoutCompatible;
  }

  bool getMustBeNull() const {
    assert(getKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MustBeNull;
  }

  const ParsedType &getTypeArg() const {
    assert(HasParsedType && "Not a type attribute");
    return getTypeBuffer();
  }

  IdentifierInfo *getPropertyDataGetter() const {
    assert(isDeclspecPropertyAttribute() &&
           "Not a __delcspec(property) attribute");
    return getPropertyDataBuffer().GetterId;
  }

  IdentifierInfo *getPropertyDataSetter() const {
    assert(isDeclspecPropertyAttribute() &&
           "Not a __delcspec(property) attribute");
    return getPropertyDataBuffer().SetterId;
  }

  /// Get an index into the attribute spelling list
  /// defined in Attr.td. This index is used by an attribute
  /// to pretty print itself.
  unsigned getAttributeSpellingListIndex() const;

  bool isTargetSpecificAttr() const;
  bool isTypeAttr() const;
  bool isStmtAttr() const;

  bool hasCustomParsing() const;
  unsigned getMinArgs() const;
  unsigned getMaxArgs() const;
  bool hasVariadicArg() const;
  bool diagnoseAppertainsTo(class Sema &S, const Decl *D) const;
  bool appliesToDecl(const Decl *D, attr::SubjectMatchRule MatchRule) const;
  void getMatchRules(const LangOptions &LangOpts,
                     SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
                         &MatchRules) const;
  bool diagnoseLangOpts(class Sema &S) const;
  bool existsInTarget(const TargetInfo &Target) const;
  bool isKnownToGCC() const;
  bool isSupportedByPragmaAttribute() const;

  /// If the parsed attribute has a semantic equivalent, and it would
  /// have a semantic Spelling enumeration (due to having semantically-distinct
  /// spelling variations), return the value of that semantic spelling. If the
  /// parsed attribute does not have a semantic equivalent, or would not have
  /// a Spelling enumeration, the value UINT_MAX is returned.
  unsigned getSemanticSpelling() const;
};

class AttributePool;
/// A factory, from which one makes pools, from which one creates
/// individual attributes which are deallocated with the pool.
///
/// Note that it's tolerably cheap to create and destroy one of
/// these as long as you don't actually allocate anything in it.
class AttributeFactory {
public:
  enum {
    AvailabilityAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(1, 1, 0, 0, 0),
    TypeTagForDatatypeAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(1, 0, 1, 0, 0),
    PropertyAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(0, 0, 0, 0, 1),
  };

private:
  enum {
    /// The number of free lists we want to be sure to support
    /// inline.  This is just enough that availability attributes
    /// don't surpass it.  It's actually very unlikely we'll see an
    /// attribute that needs more than that; on x86-64 you'd need 10
    /// expression arguments, and on i386 you'd need 19.
    InlineFreeListsCapacity =
        1 + (AvailabilityAllocSize - sizeof(ParsedAttr)) / sizeof(void *)
  };

  llvm::BumpPtrAllocator Alloc;

  /// Free lists.  The index is determined by the following formula:
  ///   (size - sizeof(ParsedAttr)) / sizeof(void*)
  SmallVector<SmallVector<ParsedAttr *, 8>, InlineFreeListsCapacity> FreeLists;

  // The following are the private interface used by AttributePool.
  friend class AttributePool;

  /// Allocate an attribute of the given size.
  void *allocate(size_t size);

  void deallocate(ParsedAttr *AL);

  /// Reclaim all the attributes in the given pool chain, which is
  /// non-empty.  Note that the current implementation is safe
  /// against reclaiming things which were not actually allocated
  /// with the allocator, although of course it's important to make
  /// sure that their allocator lives at least as long as this one.
  void reclaimPool(AttributePool &head);

public:
  AttributeFactory();
  ~AttributeFactory();
};

class AttributePool {
  friend class AttributeFactory;
  AttributeFactory &Factory;
  llvm::TinyPtrVector<ParsedAttr *> Attrs;

  void *allocate(size_t size) {
    return Factory.allocate(size);
  }

  ParsedAttr *add(ParsedAttr *attr) {
    Attrs.push_back(attr);
    return attr;
  }

  void remove(ParsedAttr *attr) {
    assert(llvm::is_contained(Attrs, attr) &&
           "Can't take attribute from a pool that doesn't own it!");
    Attrs.erase(llvm::find(Attrs, attr));
  }

  void takePool(AttributePool &pool);

public:
  /// Create a new pool for a factory.
  AttributePool(AttributeFactory &factory) : Factory(factory) {}

  AttributePool(const AttributePool &) = delete;

  ~AttributePool() { Factory.reclaimPool(*this); }

  /// Move the given pool's allocations to this pool.
  AttributePool(AttributePool &&pool) = default;

  AttributeFactory &getFactory() const { return Factory; }

  void clear() {
    Factory.reclaimPool(*this);
    Attrs.clear();
  }

  /// Take the given pool's allocations and add them to this pool.
  void takeAllFrom(AttributePool &pool) {
    takePool(pool);
    pool.Attrs.clear();
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs,
                     ParsedAttr::Syntax syntax,
                     SourceLocation ellipsisLoc = SourceLocation()) {
    size_t temp =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(numArgs, 0, 0, 0, 0);
    (void)temp;
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(numArgs, 0, 0, 0,
                                                           0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       args, numArgs, syntax, ellipsisLoc));
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Syntax syntax, SourceLocation strict,
                     const Expr *ReplacementExpr) {
    void *memory = allocate(AttributeFactory::AvailabilityAllocSize);
    return add(new (memory) ParsedAttr(
        attrName, attrRange, scopeName, scopeLoc, Param, introduced, deprecated,
        obsoleted, unavailable, MessageExpr, syntax, strict, ReplacementExpr));
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param1, IdentifierLoc *Param2,
                     IdentifierLoc *Param3, ParsedAttr::Syntax syntax) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(3, 0, 0, 0, 0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       Param1, Param2, Param3, syntax));
  }

  ParsedAttr *
  createTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Syntax syntax) {
    void *memory = allocate(AttributeFactory::TypeTagForDatatypeAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       argumentKind, matchingCType,
                                       layoutCompatible, mustBeNull, syntax));
  }

  ParsedAttr *createTypeAttribute(IdentifierInfo *attrName,
                                  SourceRange attrRange,
                                  IdentifierInfo *scopeName,
                                  SourceLocation scopeLoc, ParsedType typeArg,
                                  ParsedAttr::Syntax syntaxUsed) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(0, 0, 0, 1, 0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       typeArg, syntaxUsed));
  }

  ParsedAttr *
  createPropertyAttribute(IdentifierInfo *attrName, SourceRange attrRange,
                          IdentifierInfo *scopeName, SourceLocation scopeLoc,
                          IdentifierInfo *getterId, IdentifierInfo *setterId,
                          ParsedAttr::Syntax syntaxUsed) {
    void *memory = allocate(AttributeFactory::PropertyAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       getterId, setterId, syntaxUsed));
  }
};

class ParsedAttributesView {
  using VecTy = llvm::TinyPtrVector<ParsedAttr *>;
  using SizeType = decltype(std::declval<VecTy>().size());

public:
  bool empty() const { return AttrList.empty(); }
  SizeType size() const { return AttrList.size(); }
  ParsedAttr &operator[](SizeType pos) { return *AttrList[pos]; }
  const ParsedAttr &operator[](SizeType pos) const { return *AttrList[pos]; }

  void addAtEnd(ParsedAttr *newAttr) {
    assert(newAttr);
    AttrList.push_back(newAttr);
  }

  void remove(ParsedAttr *ToBeRemoved) {
    assert(is_contained(AttrList, ToBeRemoved) &&
           "Cannot remove attribute that isn't in the list");
    AttrList.erase(llvm::find(AttrList, ToBeRemoved));
  }

  void clearListOnly() { AttrList.clear(); }

  struct iterator : llvm::iterator_adaptor_base<iterator, VecTy::iterator,
                                                std::random_access_iterator_tag,
                                                ParsedAttr> {
    iterator() : iterator_adaptor_base(nullptr) {}
    iterator(VecTy::iterator I) : iterator_adaptor_base(I) {}
    reference operator*() { return **I; }
    friend class ParsedAttributesView;
  };
  struct const_iterator
      : llvm::iterator_adaptor_base<const_iterator, VecTy::const_iterator,
                                    std::random_access_iterator_tag,
                                    ParsedAttr> {
    const_iterator() : iterator_adaptor_base(nullptr) {}
    const_iterator(VecTy::const_iterator I) : iterator_adaptor_base(I) {}

    reference operator*() const { return **I; }
    friend class ParsedAttributesView;
  };

  void addAll(iterator B, iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAll(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAllAtEnd(iterator B, iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  void addAllAtEnd(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  iterator begin() { return iterator(AttrList.begin()); }
  const_iterator begin() const { return const_iterator(AttrList.begin()); }
  iterator end() { return iterator(AttrList.end()); }
  const_iterator end() const { return const_iterator(AttrList.end()); }

  ParsedAttr &front() {
    assert(!empty());
    return *AttrList.front();
  }
  const ParsedAttr &front() const {
    assert(!empty());
    return *AttrList.front();
  }
  ParsedAttr &back() {
    assert(!empty());
    return *AttrList.back();
  }
  const ParsedAttr &back() const {
    assert(!empty());
    return *AttrList.back();
  }

  bool hasAttribute(ParsedAttr::Kind K) const {
    return llvm::any_of(
        AttrList, [K](const ParsedAttr *AL) { return AL->getKind() == K; });
  }

private:
  VecTy AttrList;
};

/// ParsedAttributes - A collection of parsed attributes.  Currently
/// we don't differentiate between the various attribute syntaxes,
/// which is basically silly.
///
/// Right now this is a very lightweight container, but the expectation
/// is that this will become significantly more serious.
class ParsedAttributes : public ParsedAttributesView {
public:
  ParsedAttributes(AttributeFactory &factory) : pool(factory) {}
  ParsedAttributes(const ParsedAttributes &) = delete;

  AttributePool &getPool() const { return pool; }

  void takeAllFrom(ParsedAttributes &attrs) {
    addAll(attrs.begin(), attrs.end());
    attrs.clearListOnly();
    pool.takeAllFrom(attrs.pool);
  }

  void clear() {
    clearListOnly();
    pool.clear();
  }

  /// Add attribute with expression arguments.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs,
                     ParsedAttr::Syntax syntax,
                     SourceLocation ellipsisLoc = SourceLocation()) {
    ParsedAttr *attr = pool.create(attrName, attrRange, scopeName, scopeLoc,
                                   args, numArgs, syntax, ellipsisLoc);
    addAtEnd(attr);
    return attr;
  }

  /// Add availability attribute.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Syntax syntax, SourceLocation strict,
                     const Expr *ReplacementExpr) {
    ParsedAttr *attr = pool.create(
        attrName, attrRange, scopeName, scopeLoc, Param, introduced, deprecated,
        obsoleted, unavailable, MessageExpr, syntax, strict, ReplacementExpr);
    addAtEnd(attr);
    return attr;
  }

  /// Add objc_bridge_related attribute.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param1, IdentifierLoc *Param2,
                     IdentifierLoc *Param3, ParsedAttr::Syntax syntax) {
    ParsedAttr *attr = pool.create(attrName, attrRange, scopeName, scopeLoc,
                                   Param1, Param2, Param3, syntax);
    addAtEnd(attr);
    return attr;
  }

  /// Add type_tag_for_datatype attribute.
  ParsedAttr *
  addNewTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Syntax syntax) {
    ParsedAttr *attr = pool.createTypeTagForDatatype(
        attrName, attrRange, scopeName, scopeLoc, argumentKind, matchingCType,
        layoutCompatible, mustBeNull, syntax);
    addAtEnd(attr);
    return attr;
  }

  /// Add an attribute with a single type argument.
  ParsedAttr *addNewTypeAttr(IdentifierInfo *attrName, SourceRange attrRange,
                             IdentifierInfo *scopeName, SourceLocation scopeLoc,
                             ParsedType typeArg,
                             ParsedAttr::Syntax syntaxUsed) {
    ParsedAttr *attr = pool.createTypeAttribute(attrName, attrRange, scopeName,
                                                scopeLoc, typeArg, syntaxUsed);
    addAtEnd(attr);
    return attr;
  }

  /// Add microsoft __delspec(property) attribute.
  ParsedAttr *
  addNewPropertyAttr(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierInfo *getterId, IdentifierInfo *setterId,
                     ParsedAttr::Syntax syntaxUsed) {
    ParsedAttr *attr =
        pool.createPropertyAttribute(attrName, attrRange, scopeName, scopeLoc,
                                     getterId, setterId, syntaxUsed);
    addAtEnd(attr);
    return attr;
  }

private:
  mutable AttributePool pool;
};

/// These constants match the enumerated choices of
/// err_attribute_argument_n_type and err_attribute_argument_type.
enum AttributeArgumentNType {
  AANT_ArgumentIntOrBool,
  AANT_ArgumentIntegerConstant,
  AANT_ArgumentString,
  AANT_ArgumentIdentifier
};

/// These constants match the enumerated choices of
/// warn_attribute_wrong_decl_type and err_attribute_wrong_decl_type.
enum AttributeDeclKind {
  ExpectedFunction,
  ExpectedUnion,
  ExpectedVariableOrFunction,
  ExpectedFunctionOrMethod,
  ExpectedFunctionMethodOrBlock,
  ExpectedFunctionMethodOrParameter,
  ExpectedVariable,
  ExpectedVariableOrField,
  ExpectedVariableFieldOrTag,
  ExpectedTypeOrNamespace,
  ExpectedFunctionVariableOrClass,
  ExpectedKernelFunction,
  ExpectedFunctionWithProtoType,
};

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const ParsedAttr &At) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(At.getName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

inline const PartialDiagnostic &operator<<(const PartialDiagnostic &PD,
                                           const ParsedAttr &At) {
  PD.AddTaggedVal(reinterpret_cast<intptr_t>(At.getName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return PD;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const ParsedAttr *At) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(At->getName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

inline const PartialDiagnostic &operator<<(const PartialDiagnostic &PD,
                                           const ParsedAttr *At) {
  PD.AddTaggedVal(reinterpret_cast<intptr_t>(At->getName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return PD;
}

} // namespace clang

#endif // LLVM_CLANG_SEMA_ATTRIBUTELIST_H
