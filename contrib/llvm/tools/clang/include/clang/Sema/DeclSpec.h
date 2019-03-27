//===--- DeclSpec.h - Parsed declaration specifiers -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the classes used to store parsed information about
/// declaration-specifiers and declarators.
///
/// \verbatim
///   static const int volatile x, *y, *(*(*z)[10])(const void *x);
///   ------------------------- -  --  ---------------------------
///     declaration-specifiers  \  |   /
///                            declarators
/// \endverbatim
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_DECLSPEC_H
#define LLVM_CLANG_SEMA_DECLSPEC_H

#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/ParsedAttr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {
  class ASTContext;
  class CXXRecordDecl;
  class TypeLoc;
  class LangOptions;
  class IdentifierInfo;
  class NamespaceAliasDecl;
  class NamespaceDecl;
  class ObjCDeclSpec;
  class Sema;
  class Declarator;
  struct TemplateIdAnnotation;

/// Represents a C++ nested-name-specifier or a global scope specifier.
///
/// These can be in 3 states:
///   1) Not present, identified by isEmpty()
///   2) Present, identified by isNotEmpty()
///      2.a) Valid, identified by isValid()
///      2.b) Invalid, identified by isInvalid().
///
/// isSet() is deprecated because it mostly corresponded to "valid" but was
/// often used as if it meant "present".
///
/// The actual scope is described by getScopeRep().
class CXXScopeSpec {
  SourceRange Range;
  NestedNameSpecifierLocBuilder Builder;

public:
  SourceRange getRange() const { return Range; }
  void setRange(SourceRange R) { Range = R; }
  void setBeginLoc(SourceLocation Loc) { Range.setBegin(Loc); }
  void setEndLoc(SourceLocation Loc) { Range.setEnd(Loc); }
  SourceLocation getBeginLoc() const { return Range.getBegin(); }
  SourceLocation getEndLoc() const { return Range.getEnd(); }

  /// Retrieve the representation of the nested-name-specifier.
  NestedNameSpecifier *getScopeRep() const {
    return Builder.getRepresentation();
  }

  /// Extend the current nested-name-specifier by another
  /// nested-name-specifier component of the form 'type::'.
  ///
  /// \param Context The AST context in which this nested-name-specifier
  /// resides.
  ///
  /// \param TemplateKWLoc The location of the 'template' keyword, if present.
  ///
  /// \param TL The TypeLoc that describes the type preceding the '::'.
  ///
  /// \param ColonColonLoc The location of the trailing '::'.
  void Extend(ASTContext &Context, SourceLocation TemplateKWLoc, TypeLoc TL,
              SourceLocation ColonColonLoc);

  /// Extend the current nested-name-specifier by another
  /// nested-name-specifier component of the form 'identifier::'.
  ///
  /// \param Context The AST context in which this nested-name-specifier
  /// resides.
  ///
  /// \param Identifier The identifier.
  ///
  /// \param IdentifierLoc The location of the identifier.
  ///
  /// \param ColonColonLoc The location of the trailing '::'.
  void Extend(ASTContext &Context, IdentifierInfo *Identifier,
              SourceLocation IdentifierLoc, SourceLocation ColonColonLoc);

  /// Extend the current nested-name-specifier by another
  /// nested-name-specifier component of the form 'namespace::'.
  ///
  /// \param Context The AST context in which this nested-name-specifier
  /// resides.
  ///
  /// \param Namespace The namespace.
  ///
  /// \param NamespaceLoc The location of the namespace name.
  ///
  /// \param ColonColonLoc The location of the trailing '::'.
  void Extend(ASTContext &Context, NamespaceDecl *Namespace,
              SourceLocation NamespaceLoc, SourceLocation ColonColonLoc);

  /// Extend the current nested-name-specifier by another
  /// nested-name-specifier component of the form 'namespace-alias::'.
  ///
  /// \param Context The AST context in which this nested-name-specifier
  /// resides.
  ///
  /// \param Alias The namespace alias.
  ///
  /// \param AliasLoc The location of the namespace alias
  /// name.
  ///
  /// \param ColonColonLoc The location of the trailing '::'.
  void Extend(ASTContext &Context, NamespaceAliasDecl *Alias,
              SourceLocation AliasLoc, SourceLocation ColonColonLoc);

  /// Turn this (empty) nested-name-specifier into the global
  /// nested-name-specifier '::'.
  void MakeGlobal(ASTContext &Context, SourceLocation ColonColonLoc);

  /// Turns this (empty) nested-name-specifier into '__super'
  /// nested-name-specifier.
  ///
  /// \param Context The AST context in which this nested-name-specifier
  /// resides.
  ///
  /// \param RD The declaration of the class in which nested-name-specifier
  /// appeared.
  ///
  /// \param SuperLoc The location of the '__super' keyword.
  /// name.
  ///
  /// \param ColonColonLoc The location of the trailing '::'.
  void MakeSuper(ASTContext &Context, CXXRecordDecl *RD,
                 SourceLocation SuperLoc, SourceLocation ColonColonLoc);

  /// Make a new nested-name-specifier from incomplete source-location
  /// information.
  ///
  /// FIXME: This routine should be used very, very rarely, in cases where we
  /// need to synthesize a nested-name-specifier. Most code should instead use
  /// \c Adopt() with a proper \c NestedNameSpecifierLoc.
  void MakeTrivial(ASTContext &Context, NestedNameSpecifier *Qualifier,
                   SourceRange R);

  /// Adopt an existing nested-name-specifier (with source-range
  /// information).
  void Adopt(NestedNameSpecifierLoc Other);

  /// Retrieve a nested-name-specifier with location information, copied
  /// into the given AST context.
  ///
  /// \param Context The context into which this nested-name-specifier will be
  /// copied.
  NestedNameSpecifierLoc getWithLocInContext(ASTContext &Context) const;

  /// Retrieve the location of the name in the last qualifier
  /// in this nested name specifier.
  ///
  /// For example, the location of \c bar
  /// in
  /// \verbatim
  ///   \::foo::bar<0>::
  ///           ^~~
  /// \endverbatim
  SourceLocation getLastQualifierNameLoc() const;

  /// No scope specifier.
  bool isEmpty() const { return !Range.isValid(); }
  /// A scope specifier is present, but may be valid or invalid.
  bool isNotEmpty() const { return !isEmpty(); }

  /// An error occurred during parsing of the scope specifier.
  bool isInvalid() const { return isNotEmpty() && getScopeRep() == nullptr; }
  /// A scope specifier is present, and it refers to a real scope.
  bool isValid() const { return isNotEmpty() && getScopeRep() != nullptr; }

  /// Indicate that this nested-name-specifier is invalid.
  void SetInvalid(SourceRange R) {
    assert(R.isValid() && "Must have a valid source range");
    if (Range.getBegin().isInvalid())
      Range.setBegin(R.getBegin());
    Range.setEnd(R.getEnd());
    Builder.Clear();
  }

  /// Deprecated.  Some call sites intend isNotEmpty() while others intend
  /// isValid().
  bool isSet() const { return getScopeRep() != nullptr; }

  void clear() {
    Range = SourceRange();
    Builder.Clear();
  }

  /// Retrieve the data associated with the source-location information.
  char *location_data() const { return Builder.getBuffer().first; }

  /// Retrieve the size of the data associated with source-location
  /// information.
  unsigned location_size() const { return Builder.getBuffer().second; }
};

/// Captures information about "declaration specifiers".
///
/// "Declaration specifiers" encompasses storage-class-specifiers,
/// type-specifiers, type-qualifiers, and function-specifiers.
class DeclSpec {
public:
  /// storage-class-specifier
  /// \note The order of these enumerators is important for diagnostics.
  enum SCS {
    SCS_unspecified = 0,
    SCS_typedef,
    SCS_extern,
    SCS_static,
    SCS_auto,
    SCS_register,
    SCS_private_extern,
    SCS_mutable
  };

  // Import thread storage class specifier enumeration and constants.
  // These can be combined with SCS_extern and SCS_static.
  typedef ThreadStorageClassSpecifier TSCS;
  static const TSCS TSCS_unspecified = clang::TSCS_unspecified;
  static const TSCS TSCS___thread = clang::TSCS___thread;
  static const TSCS TSCS_thread_local = clang::TSCS_thread_local;
  static const TSCS TSCS__Thread_local = clang::TSCS__Thread_local;

  // Import type specifier width enumeration and constants.
  typedef TypeSpecifierWidth TSW;
  static const TSW TSW_unspecified = clang::TSW_unspecified;
  static const TSW TSW_short = clang::TSW_short;
  static const TSW TSW_long = clang::TSW_long;
  static const TSW TSW_longlong = clang::TSW_longlong;

  enum TSC {
    TSC_unspecified,
    TSC_imaginary,
    TSC_complex
  };

  // Import type specifier sign enumeration and constants.
  typedef TypeSpecifierSign TSS;
  static const TSS TSS_unspecified = clang::TSS_unspecified;
  static const TSS TSS_signed = clang::TSS_signed;
  static const TSS TSS_unsigned = clang::TSS_unsigned;

  // Import type specifier type enumeration and constants.
  typedef TypeSpecifierType TST;
  static const TST TST_unspecified = clang::TST_unspecified;
  static const TST TST_void = clang::TST_void;
  static const TST TST_char = clang::TST_char;
  static const TST TST_wchar = clang::TST_wchar;
  static const TST TST_char8 = clang::TST_char8;
  static const TST TST_char16 = clang::TST_char16;
  static const TST TST_char32 = clang::TST_char32;
  static const TST TST_int = clang::TST_int;
  static const TST TST_int128 = clang::TST_int128;
  static const TST TST_half = clang::TST_half;
  static const TST TST_float = clang::TST_float;
  static const TST TST_double = clang::TST_double;
  static const TST TST_float16 = clang::TST_Float16;
  static const TST TST_accum = clang::TST_Accum;
  static const TST TST_fract = clang::TST_Fract;
  static const TST TST_float128 = clang::TST_float128;
  static const TST TST_bool = clang::TST_bool;
  static const TST TST_decimal32 = clang::TST_decimal32;
  static const TST TST_decimal64 = clang::TST_decimal64;
  static const TST TST_decimal128 = clang::TST_decimal128;
  static const TST TST_enum = clang::TST_enum;
  static const TST TST_union = clang::TST_union;
  static const TST TST_struct = clang::TST_struct;
  static const TST TST_interface = clang::TST_interface;
  static const TST TST_class = clang::TST_class;
  static const TST TST_typename = clang::TST_typename;
  static const TST TST_typeofType = clang::TST_typeofType;
  static const TST TST_typeofExpr = clang::TST_typeofExpr;
  static const TST TST_decltype = clang::TST_decltype;
  static const TST TST_decltype_auto = clang::TST_decltype_auto;
  static const TST TST_underlyingType = clang::TST_underlyingType;
  static const TST TST_auto = clang::TST_auto;
  static const TST TST_auto_type = clang::TST_auto_type;
  static const TST TST_unknown_anytype = clang::TST_unknown_anytype;
  static const TST TST_atomic = clang::TST_atomic;
#define GENERIC_IMAGE_TYPE(ImgType, Id) \
  static const TST TST_##ImgType##_t = clang::TST_##ImgType##_t;
#include "clang/Basic/OpenCLImageTypes.def"
  static const TST TST_error = clang::TST_error;

  // type-qualifiers
  enum TQ {   // NOTE: These flags must be kept in sync with Qualifiers::TQ.
    TQ_unspecified = 0,
    TQ_const       = 1,
    TQ_restrict    = 2,
    TQ_volatile    = 4,
    TQ_unaligned   = 8,
    // This has no corresponding Qualifiers::TQ value, because it's not treated
    // as a qualifier in our type system.
    TQ_atomic      = 16
  };

  /// ParsedSpecifiers - Flags to query which specifiers were applied.  This is
  /// returned by getParsedSpecifiers.
  enum ParsedSpecifiers {
    PQ_None                  = 0,
    PQ_StorageClassSpecifier = 1,
    PQ_TypeSpecifier         = 2,
    PQ_TypeQualifier         = 4,
    PQ_FunctionSpecifier     = 8
    // FIXME: Attributes should be included here.
  };

private:
  // storage-class-specifier
  /*SCS*/unsigned StorageClassSpec : 3;
  /*TSCS*/unsigned ThreadStorageClassSpec : 2;
  unsigned SCS_extern_in_linkage_spec : 1;

  // type-specifier
  /*TSW*/unsigned TypeSpecWidth : 2;
  /*TSC*/unsigned TypeSpecComplex : 2;
  /*TSS*/unsigned TypeSpecSign : 2;
  /*TST*/unsigned TypeSpecType : 6;
  unsigned TypeAltiVecVector : 1;
  unsigned TypeAltiVecPixel : 1;
  unsigned TypeAltiVecBool : 1;
  unsigned TypeSpecOwned : 1;
  unsigned TypeSpecPipe : 1;
  unsigned TypeSpecSat : 1;

  // type-qualifiers
  unsigned TypeQualifiers : 5;  // Bitwise OR of TQ.

  // function-specifier
  unsigned FS_inline_specified : 1;
  unsigned FS_forceinline_specified: 1;
  unsigned FS_virtual_specified : 1;
  unsigned FS_explicit_specified : 1;
  unsigned FS_noreturn_specified : 1;

  // friend-specifier
  unsigned Friend_specified : 1;

  // constexpr-specifier
  unsigned Constexpr_specified : 1;

  union {
    UnionParsedType TypeRep;
    Decl *DeclRep;
    Expr *ExprRep;
  };

  // attributes.
  ParsedAttributes Attrs;

  // Scope specifier for the type spec, if applicable.
  CXXScopeSpec TypeScope;

  // SourceLocation info.  These are null if the item wasn't specified or if
  // the setting was synthesized.
  SourceRange Range;

  SourceLocation StorageClassSpecLoc, ThreadStorageClassSpecLoc;
  SourceRange TSWRange;
  SourceLocation TSCLoc, TSSLoc, TSTLoc, AltiVecLoc, TSSatLoc;
  /// TSTNameLoc - If TypeSpecType is any of class, enum, struct, union,
  /// typename, then this is the location of the named type (if present);
  /// otherwise, it is the same as TSTLoc. Hence, the pair TSTLoc and
  /// TSTNameLoc provides source range info for tag types.
  SourceLocation TSTNameLoc;
  SourceRange TypeofParensRange;
  SourceLocation TQ_constLoc, TQ_restrictLoc, TQ_volatileLoc, TQ_atomicLoc,
      TQ_unalignedLoc;
  SourceLocation FS_inlineLoc, FS_virtualLoc, FS_explicitLoc, FS_noreturnLoc;
  SourceLocation FS_forceinlineLoc;
  SourceLocation FriendLoc, ModulePrivateLoc, ConstexprLoc;
  SourceLocation TQ_pipeLoc;

  WrittenBuiltinSpecs writtenBS;
  void SaveWrittenBuiltinSpecs();

  ObjCDeclSpec *ObjCQualifiers;

  static bool isTypeRep(TST T) {
    return (T == TST_typename || T == TST_typeofType ||
            T == TST_underlyingType || T == TST_atomic);
  }
  static bool isExprRep(TST T) {
    return (T == TST_typeofExpr || T == TST_decltype);
  }

  DeclSpec(const DeclSpec &) = delete;
  void operator=(const DeclSpec &) = delete;
public:
  static bool isDeclRep(TST T) {
    return (T == TST_enum || T == TST_struct ||
            T == TST_interface || T == TST_union ||
            T == TST_class);
  }

  DeclSpec(AttributeFactory &attrFactory)
    : StorageClassSpec(SCS_unspecified),
      ThreadStorageClassSpec(TSCS_unspecified),
      SCS_extern_in_linkage_spec(false),
      TypeSpecWidth(TSW_unspecified),
      TypeSpecComplex(TSC_unspecified),
      TypeSpecSign(TSS_unspecified),
      TypeSpecType(TST_unspecified),
      TypeAltiVecVector(false),
      TypeAltiVecPixel(false),
      TypeAltiVecBool(false),
      TypeSpecOwned(false),
      TypeSpecPipe(false),
      TypeSpecSat(false),
      TypeQualifiers(TQ_unspecified),
      FS_inline_specified(false),
      FS_forceinline_specified(false),
      FS_virtual_specified(false),
      FS_explicit_specified(false),
      FS_noreturn_specified(false),
      Friend_specified(false),
      Constexpr_specified(false),
      Attrs(attrFactory),
      writtenBS(),
      ObjCQualifiers(nullptr) {
  }

  // storage-class-specifier
  SCS getStorageClassSpec() const { return (SCS)StorageClassSpec; }
  TSCS getThreadStorageClassSpec() const {
    return (TSCS)ThreadStorageClassSpec;
  }
  bool isExternInLinkageSpec() const { return SCS_extern_in_linkage_spec; }
  void setExternInLinkageSpec(bool Value) {
    SCS_extern_in_linkage_spec = Value;
  }

  SourceLocation getStorageClassSpecLoc() const { return StorageClassSpecLoc; }
  SourceLocation getThreadStorageClassSpecLoc() const {
    return ThreadStorageClassSpecLoc;
  }

  void ClearStorageClassSpecs() {
    StorageClassSpec           = DeclSpec::SCS_unspecified;
    ThreadStorageClassSpec     = DeclSpec::TSCS_unspecified;
    SCS_extern_in_linkage_spec = false;
    StorageClassSpecLoc        = SourceLocation();
    ThreadStorageClassSpecLoc  = SourceLocation();
  }

  void ClearTypeSpecType() {
    TypeSpecType = DeclSpec::TST_unspecified;
    TypeSpecOwned = false;
    TSTLoc = SourceLocation();
  }

  // type-specifier
  TSW getTypeSpecWidth() const { return (TSW)TypeSpecWidth; }
  TSC getTypeSpecComplex() const { return (TSC)TypeSpecComplex; }
  TSS getTypeSpecSign() const { return (TSS)TypeSpecSign; }
  TST getTypeSpecType() const { return (TST)TypeSpecType; }
  bool isTypeAltiVecVector() const { return TypeAltiVecVector; }
  bool isTypeAltiVecPixel() const { return TypeAltiVecPixel; }
  bool isTypeAltiVecBool() const { return TypeAltiVecBool; }
  bool isTypeSpecOwned() const { return TypeSpecOwned; }
  bool isTypeRep() const { return isTypeRep((TST) TypeSpecType); }
  bool isTypeSpecPipe() const { return TypeSpecPipe; }
  bool isTypeSpecSat() const { return TypeSpecSat; }

  ParsedType getRepAsType() const {
    assert(isTypeRep((TST) TypeSpecType) && "DeclSpec does not store a type");
    return TypeRep;
  }
  Decl *getRepAsDecl() const {
    assert(isDeclRep((TST) TypeSpecType) && "DeclSpec does not store a decl");
    return DeclRep;
  }
  Expr *getRepAsExpr() const {
    assert(isExprRep((TST) TypeSpecType) && "DeclSpec does not store an expr");
    return ExprRep;
  }
  CXXScopeSpec &getTypeSpecScope() { return TypeScope; }
  const CXXScopeSpec &getTypeSpecScope() const { return TypeScope; }

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  SourceLocation getTypeSpecWidthLoc() const { return TSWRange.getBegin(); }
  SourceRange getTypeSpecWidthRange() const { return TSWRange; }
  SourceLocation getTypeSpecComplexLoc() const { return TSCLoc; }
  SourceLocation getTypeSpecSignLoc() const { return TSSLoc; }
  SourceLocation getTypeSpecTypeLoc() const { return TSTLoc; }
  SourceLocation getAltiVecLoc() const { return AltiVecLoc; }
  SourceLocation getTypeSpecSatLoc() const { return TSSatLoc; }

  SourceLocation getTypeSpecTypeNameLoc() const {
    assert(isDeclRep((TST) TypeSpecType) || TypeSpecType == TST_typename);
    return TSTNameLoc;
  }

  SourceRange getTypeofParensRange() const { return TypeofParensRange; }
  void setTypeofParensRange(SourceRange range) { TypeofParensRange = range; }

  bool hasAutoTypeSpec() const {
    return (TypeSpecType == TST_auto || TypeSpecType == TST_auto_type ||
            TypeSpecType == TST_decltype_auto);
  }

  bool hasTagDefinition() const;

  /// Turn a type-specifier-type into a string like "_Bool" or "union".
  static const char *getSpecifierName(DeclSpec::TST T,
                                      const PrintingPolicy &Policy);
  static const char *getSpecifierName(DeclSpec::TQ Q);
  static const char *getSpecifierName(DeclSpec::TSS S);
  static const char *getSpecifierName(DeclSpec::TSC C);
  static const char *getSpecifierName(DeclSpec::TSW W);
  static const char *getSpecifierName(DeclSpec::SCS S);
  static const char *getSpecifierName(DeclSpec::TSCS S);

  // type-qualifiers

  /// getTypeQualifiers - Return a set of TQs.
  unsigned getTypeQualifiers() const { return TypeQualifiers; }
  SourceLocation getConstSpecLoc() const { return TQ_constLoc; }
  SourceLocation getRestrictSpecLoc() const { return TQ_restrictLoc; }
  SourceLocation getVolatileSpecLoc() const { return TQ_volatileLoc; }
  SourceLocation getAtomicSpecLoc() const { return TQ_atomicLoc; }
  SourceLocation getUnalignedSpecLoc() const { return TQ_unalignedLoc; }
  SourceLocation getPipeLoc() const { return TQ_pipeLoc; }

  /// Clear out all of the type qualifiers.
  void ClearTypeQualifiers() {
    TypeQualifiers = 0;
    TQ_constLoc = SourceLocation();
    TQ_restrictLoc = SourceLocation();
    TQ_volatileLoc = SourceLocation();
    TQ_atomicLoc = SourceLocation();
    TQ_unalignedLoc = SourceLocation();
    TQ_pipeLoc = SourceLocation();
  }

  // function-specifier
  bool isInlineSpecified() const {
    return FS_inline_specified | FS_forceinline_specified;
  }
  SourceLocation getInlineSpecLoc() const {
    return FS_inline_specified ? FS_inlineLoc : FS_forceinlineLoc;
  }

  bool isVirtualSpecified() const { return FS_virtual_specified; }
  SourceLocation getVirtualSpecLoc() const { return FS_virtualLoc; }

  bool isExplicitSpecified() const { return FS_explicit_specified; }
  SourceLocation getExplicitSpecLoc() const { return FS_explicitLoc; }

  bool isNoreturnSpecified() const { return FS_noreturn_specified; }
  SourceLocation getNoreturnSpecLoc() const { return FS_noreturnLoc; }

  void ClearFunctionSpecs() {
    FS_inline_specified = false;
    FS_inlineLoc = SourceLocation();
    FS_forceinline_specified = false;
    FS_forceinlineLoc = SourceLocation();
    FS_virtual_specified = false;
    FS_virtualLoc = SourceLocation();
    FS_explicit_specified = false;
    FS_explicitLoc = SourceLocation();
    FS_noreturn_specified = false;
    FS_noreturnLoc = SourceLocation();
  }

  /// This method calls the passed in handler on each CVRU qual being
  /// set.
  /// Handle - a handler to be invoked.
  void forEachCVRUQualifier(
      llvm::function_ref<void(TQ, StringRef, SourceLocation)> Handle);

  /// This method calls the passed in handler on each qual being
  /// set.
  /// Handle - a handler to be invoked.
  void forEachQualifier(
      llvm::function_ref<void(TQ, StringRef, SourceLocation)> Handle);

  /// Return true if any type-specifier has been found.
  bool hasTypeSpecifier() const {
    return getTypeSpecType() != DeclSpec::TST_unspecified ||
           getTypeSpecWidth() != DeclSpec::TSW_unspecified ||
           getTypeSpecComplex() != DeclSpec::TSC_unspecified ||
           getTypeSpecSign() != DeclSpec::TSS_unspecified;
  }

  /// Return a bitmask of which flavors of specifiers this
  /// DeclSpec includes.
  unsigned getParsedSpecifiers() const;

  /// isEmpty - Return true if this declaration specifier is completely empty:
  /// no tokens were parsed in the production of it.
  bool isEmpty() const {
    return getParsedSpecifiers() == DeclSpec::PQ_None;
  }

  void SetRangeStart(SourceLocation Loc) { Range.setBegin(Loc); }
  void SetRangeEnd(SourceLocation Loc) { Range.setEnd(Loc); }

  /// These methods set the specified attribute of the DeclSpec and
  /// return false if there was no error.  If an error occurs (for
  /// example, if we tried to set "auto" on a spec with "extern"
  /// already set), they return true and set PrevSpec and DiagID
  /// such that
  ///   Diag(Loc, DiagID) << PrevSpec;
  /// will yield a useful result.
  ///
  /// TODO: use a more general approach that still allows these
  /// diagnostics to be ignored when desired.
  bool SetStorageClassSpec(Sema &S, SCS SC, SourceLocation Loc,
                           const char *&PrevSpec, unsigned &DiagID,
                           const PrintingPolicy &Policy);
  bool SetStorageClassSpecThread(TSCS TSC, SourceLocation Loc,
                                 const char *&PrevSpec, unsigned &DiagID);
  bool SetTypeSpecWidth(TSW W, SourceLocation Loc, const char *&PrevSpec,
                        unsigned &DiagID, const PrintingPolicy &Policy);
  bool SetTypeSpecComplex(TSC C, SourceLocation Loc, const char *&PrevSpec,
                          unsigned &DiagID);
  bool SetTypeSpecSign(TSS S, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, ParsedType Rep,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, Decl *Rep, bool Owned,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                       SourceLocation TagNameLoc, const char *&PrevSpec,
                       unsigned &DiagID, ParsedType Rep,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                       SourceLocation TagNameLoc, const char *&PrevSpec,
                       unsigned &DiagID, Decl *Rep, bool Owned,
                       const PrintingPolicy &Policy);

  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, Expr *Rep,
                       const PrintingPolicy &policy);
  bool SetTypeAltiVecVector(bool isAltiVecVector, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID,
                       const PrintingPolicy &Policy);
  bool SetTypeAltiVecPixel(bool isAltiVecPixel, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID,
                       const PrintingPolicy &Policy);
  bool SetTypeAltiVecBool(bool isAltiVecBool, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID,
                       const PrintingPolicy &Policy);
  bool SetTypePipe(bool isPipe, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecSat(SourceLocation Loc, const char *&PrevSpec,
                      unsigned &DiagID);
  bool SetTypeSpecError();
  void UpdateDeclRep(Decl *Rep) {
    assert(isDeclRep((TST) TypeSpecType));
    DeclRep = Rep;
  }
  void UpdateTypeRep(ParsedType Rep) {
    assert(isTypeRep((TST) TypeSpecType));
    TypeRep = Rep;
  }
  void UpdateExprRep(Expr *Rep) {
    assert(isExprRep((TST) TypeSpecType));
    ExprRep = Rep;
  }

  bool SetTypeQual(TQ T, SourceLocation Loc);

  bool SetTypeQual(TQ T, SourceLocation Loc, const char *&PrevSpec,
                   unsigned &DiagID, const LangOptions &Lang);

  bool setFunctionSpecInline(SourceLocation Loc, const char *&PrevSpec,
                             unsigned &DiagID);
  bool setFunctionSpecForceInline(SourceLocation Loc, const char *&PrevSpec,
                                  unsigned &DiagID);
  bool setFunctionSpecVirtual(SourceLocation Loc, const char *&PrevSpec,
                              unsigned &DiagID);
  bool setFunctionSpecExplicit(SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID);
  bool setFunctionSpecNoreturn(SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID);

  bool SetFriendSpec(SourceLocation Loc, const char *&PrevSpec,
                     unsigned &DiagID);
  bool setModulePrivateSpec(SourceLocation Loc, const char *&PrevSpec,
                            unsigned &DiagID);
  bool SetConstexprSpec(SourceLocation Loc, const char *&PrevSpec,
                        unsigned &DiagID);

  bool isFriendSpecified() const { return Friend_specified; }
  SourceLocation getFriendSpecLoc() const { return FriendLoc; }

  bool isModulePrivateSpecified() const { return ModulePrivateLoc.isValid(); }
  SourceLocation getModulePrivateSpecLoc() const { return ModulePrivateLoc; }

  bool isConstexprSpecified() const { return Constexpr_specified; }
  SourceLocation getConstexprSpecLoc() const { return ConstexprLoc; }

  void ClearConstexprSpec() {
    Constexpr_specified = false;
    ConstexprLoc = SourceLocation();
  }

  AttributePool &getAttributePool() const {
    return Attrs.getPool();
  }

  /// Concatenates two attribute lists.
  ///
  /// The GCC attribute syntax allows for the following:
  ///
  /// \code
  /// short __attribute__(( unused, deprecated ))
  /// int __attribute__(( may_alias, aligned(16) )) var;
  /// \endcode
  ///
  /// This declares 4 attributes using 2 lists. The following syntax is
  /// also allowed and equivalent to the previous declaration.
  ///
  /// \code
  /// short __attribute__((unused)) __attribute__((deprecated))
  /// int __attribute__((may_alias)) __attribute__((aligned(16))) var;
  /// \endcode
  ///
  void addAttributes(ParsedAttributesView &AL) {
    Attrs.addAll(AL.begin(), AL.end());
  }

  bool hasAttributes() const { return !Attrs.empty(); }

  ParsedAttributes &getAttributes() { return Attrs; }
  const ParsedAttributes &getAttributes() const { return Attrs; }

  void takeAttributesFrom(ParsedAttributes &attrs) {
    Attrs.takeAllFrom(attrs);
  }

  /// Finish - This does final analysis of the declspec, issuing diagnostics for
  /// things like "_Imaginary" (lacking an FP type).  After calling this method,
  /// DeclSpec is guaranteed self-consistent, even if an error occurred.
  void Finish(Sema &S, const PrintingPolicy &Policy);

  const WrittenBuiltinSpecs& getWrittenBuiltinSpecs() const {
    return writtenBS;
  }

  ObjCDeclSpec *getObjCQualifiers() const { return ObjCQualifiers; }
  void setObjCQualifiers(ObjCDeclSpec *quals) { ObjCQualifiers = quals; }

  /// Checks if this DeclSpec can stand alone, without a Declarator.
  ///
  /// Only tag declspecs can stand alone.
  bool isMissingDeclaratorOk();
};

/// Captures information about "declaration specifiers" specific to
/// Objective-C.
class ObjCDeclSpec {
public:
  /// ObjCDeclQualifier - Qualifier used on types in method
  /// declarations.  Not all combinations are sensible.  Parameters
  /// can be one of { in, out, inout } with one of { bycopy, byref }.
  /// Returns can either be { oneway } or not.
  ///
  /// This should be kept in sync with Decl::ObjCDeclQualifier.
  enum ObjCDeclQualifier {
    DQ_None = 0x0,
    DQ_In = 0x1,
    DQ_Inout = 0x2,
    DQ_Out = 0x4,
    DQ_Bycopy = 0x8,
    DQ_Byref = 0x10,
    DQ_Oneway = 0x20,
    DQ_CSNullability = 0x40
  };

  /// PropertyAttributeKind - list of property attributes.
  /// Keep this list in sync with LLVM's Dwarf.h ApplePropertyAttributes.
  enum ObjCPropertyAttributeKind {
    DQ_PR_noattr = 0x0,
    DQ_PR_readonly = 0x01,
    DQ_PR_getter = 0x02,
    DQ_PR_assign = 0x04,
    DQ_PR_readwrite = 0x08,
    DQ_PR_retain = 0x10,
    DQ_PR_copy = 0x20,
    DQ_PR_nonatomic = 0x40,
    DQ_PR_setter = 0x80,
    DQ_PR_atomic = 0x100,
    DQ_PR_weak =   0x200,
    DQ_PR_strong = 0x400,
    DQ_PR_unsafe_unretained = 0x800,
    DQ_PR_nullability = 0x1000,
    DQ_PR_null_resettable = 0x2000,
    DQ_PR_class = 0x4000
  };

  ObjCDeclSpec()
    : objcDeclQualifier(DQ_None), PropertyAttributes(DQ_PR_noattr),
      Nullability(0), GetterName(nullptr), SetterName(nullptr) { }

  ObjCDeclQualifier getObjCDeclQualifier() const {
    return (ObjCDeclQualifier)objcDeclQualifier;
  }
  void setObjCDeclQualifier(ObjCDeclQualifier DQVal) {
    objcDeclQualifier = (ObjCDeclQualifier) (objcDeclQualifier | DQVal);
  }
  void clearObjCDeclQualifier(ObjCDeclQualifier DQVal) {
    objcDeclQualifier = (ObjCDeclQualifier) (objcDeclQualifier & ~DQVal);
  }

  ObjCPropertyAttributeKind getPropertyAttributes() const {
    return ObjCPropertyAttributeKind(PropertyAttributes);
  }
  void setPropertyAttributes(ObjCPropertyAttributeKind PRVal) {
    PropertyAttributes =
      (ObjCPropertyAttributeKind)(PropertyAttributes | PRVal);
  }

  NullabilityKind getNullability() const {
    assert(((getObjCDeclQualifier() & DQ_CSNullability) ||
            (getPropertyAttributes() & DQ_PR_nullability)) &&
           "Objective-C declspec doesn't have nullability");
    return static_cast<NullabilityKind>(Nullability);
  }

  SourceLocation getNullabilityLoc() const {
    assert(((getObjCDeclQualifier() & DQ_CSNullability) ||
            (getPropertyAttributes() & DQ_PR_nullability)) &&
           "Objective-C declspec doesn't have nullability");
    return NullabilityLoc;
  }

  void setNullability(SourceLocation loc, NullabilityKind kind) {
    assert(((getObjCDeclQualifier() & DQ_CSNullability) ||
            (getPropertyAttributes() & DQ_PR_nullability)) &&
           "Set the nullability declspec or property attribute first");
    Nullability = static_cast<unsigned>(kind);
    NullabilityLoc = loc;
  }

  const IdentifierInfo *getGetterName() const { return GetterName; }
  IdentifierInfo *getGetterName() { return GetterName; }
  SourceLocation getGetterNameLoc() const { return GetterNameLoc; }
  void setGetterName(IdentifierInfo *name, SourceLocation loc) {
    GetterName = name;
    GetterNameLoc = loc;
  }

  const IdentifierInfo *getSetterName() const { return SetterName; }
  IdentifierInfo *getSetterName() { return SetterName; }
  SourceLocation getSetterNameLoc() const { return SetterNameLoc; }
  void setSetterName(IdentifierInfo *name, SourceLocation loc) {
    SetterName = name;
    SetterNameLoc = loc;
  }

private:
  // FIXME: These two are unrelated and mutually exclusive. So perhaps
  // we can put them in a union to reflect their mutual exclusivity
  // (space saving is negligible).
  unsigned objcDeclQualifier : 7;

  // NOTE: VC++ treats enums as signed, avoid using ObjCPropertyAttributeKind
  unsigned PropertyAttributes : 15;

  unsigned Nullability : 2;

  SourceLocation NullabilityLoc;

  IdentifierInfo *GetterName;    // getter name or NULL if no getter
  IdentifierInfo *SetterName;    // setter name or NULL if no setter
  SourceLocation GetterNameLoc; // location of the getter attribute's value
  SourceLocation SetterNameLoc; // location of the setter attribute's value

};

/// Describes the kind of unqualified-id parsed.
enum class UnqualifiedIdKind {
  /// An identifier.
  IK_Identifier,
  /// An overloaded operator name, e.g., operator+.
  IK_OperatorFunctionId,
  /// A conversion function name, e.g., operator int.
  IK_ConversionFunctionId,
  /// A user-defined literal name, e.g., operator "" _i.
  IK_LiteralOperatorId,
  /// A constructor name.
  IK_ConstructorName,
  /// A constructor named via a template-id.
  IK_ConstructorTemplateId,
  /// A destructor name.
  IK_DestructorName,
  /// A template-id, e.g., f<int>.
  IK_TemplateId,
  /// An implicit 'self' parameter
  IK_ImplicitSelfParam,
  /// A deduction-guide name (a template-name)
  IK_DeductionGuideName
};

/// Represents a C++ unqualified-id that has been parsed.
class UnqualifiedId {
private:
  UnqualifiedId(const UnqualifiedId &Other) = delete;
  const UnqualifiedId &operator=(const UnqualifiedId &) = delete;

public:
  /// Describes the kind of unqualified-id parsed.
  UnqualifiedIdKind Kind;

  struct OFI {
    /// The kind of overloaded operator.
    OverloadedOperatorKind Operator;

    /// The source locations of the individual tokens that name
    /// the operator, e.g., the "new", "[", and "]" tokens in
    /// operator new [].
    ///
    /// Different operators have different numbers of tokens in their name,
    /// up to three. Any remaining source locations in this array will be
    /// set to an invalid value for operators with fewer than three tokens.
    unsigned SymbolLocations[3];
  };

  /// Anonymous union that holds extra data associated with the
  /// parsed unqualified-id.
  union {
    /// When Kind == IK_Identifier, the parsed identifier, or when
    /// Kind == IK_UserLiteralId, the identifier suffix.
    IdentifierInfo *Identifier;

    /// When Kind == IK_OperatorFunctionId, the overloaded operator
    /// that we parsed.
    struct OFI OperatorFunctionId;

    /// When Kind == IK_ConversionFunctionId, the type that the
    /// conversion function names.
    UnionParsedType ConversionFunctionId;

    /// When Kind == IK_ConstructorName, the class-name of the type
    /// whose constructor is being referenced.
    UnionParsedType ConstructorName;

    /// When Kind == IK_DestructorName, the type referred to by the
    /// class-name.
    UnionParsedType DestructorName;

    /// When Kind == IK_DeductionGuideName, the parsed template-name.
    UnionParsedTemplateTy TemplateName;

    /// When Kind == IK_TemplateId or IK_ConstructorTemplateId,
    /// the template-id annotation that contains the template name and
    /// template arguments.
    TemplateIdAnnotation *TemplateId;
  };

  /// The location of the first token that describes this unqualified-id,
  /// which will be the location of the identifier, "operator" keyword,
  /// tilde (for a destructor), or the template name of a template-id.
  SourceLocation StartLocation;

  /// The location of the last token that describes this unqualified-id.
  SourceLocation EndLocation;

  UnqualifiedId()
      : Kind(UnqualifiedIdKind::IK_Identifier), Identifier(nullptr) {}

  /// Clear out this unqualified-id, setting it to default (invalid)
  /// state.
  void clear() {
    Kind = UnqualifiedIdKind::IK_Identifier;
    Identifier = nullptr;
    StartLocation = SourceLocation();
    EndLocation = SourceLocation();
  }

  /// Determine whether this unqualified-id refers to a valid name.
  bool isValid() const { return StartLocation.isValid(); }

  /// Determine whether this unqualified-id refers to an invalid name.
  bool isInvalid() const { return !isValid(); }

  /// Determine what kind of name we have.
  UnqualifiedIdKind getKind() const { return Kind; }
  void setKind(UnqualifiedIdKind kind) { Kind = kind; }

  /// Specify that this unqualified-id was parsed as an identifier.
  ///
  /// \param Id the parsed identifier.
  /// \param IdLoc the location of the parsed identifier.
  void setIdentifier(const IdentifierInfo *Id, SourceLocation IdLoc) {
    Kind = UnqualifiedIdKind::IK_Identifier;
    Identifier = const_cast<IdentifierInfo *>(Id);
    StartLocation = EndLocation = IdLoc;
  }

  /// Specify that this unqualified-id was parsed as an
  /// operator-function-id.
  ///
  /// \param OperatorLoc the location of the 'operator' keyword.
  ///
  /// \param Op the overloaded operator.
  ///
  /// \param SymbolLocations the locations of the individual operator symbols
  /// in the operator.
  void setOperatorFunctionId(SourceLocation OperatorLoc,
                             OverloadedOperatorKind Op,
                             SourceLocation SymbolLocations[3]);

  /// Specify that this unqualified-id was parsed as a
  /// conversion-function-id.
  ///
  /// \param OperatorLoc the location of the 'operator' keyword.
  ///
  /// \param Ty the type to which this conversion function is converting.
  ///
  /// \param EndLoc the location of the last token that makes up the type name.
  void setConversionFunctionId(SourceLocation OperatorLoc,
                               ParsedType Ty,
                               SourceLocation EndLoc) {
    Kind = UnqualifiedIdKind::IK_ConversionFunctionId;
    StartLocation = OperatorLoc;
    EndLocation = EndLoc;
    ConversionFunctionId = Ty;
  }

  /// Specific that this unqualified-id was parsed as a
  /// literal-operator-id.
  ///
  /// \param Id the parsed identifier.
  ///
  /// \param OpLoc the location of the 'operator' keyword.
  ///
  /// \param IdLoc the location of the identifier.
  void setLiteralOperatorId(const IdentifierInfo *Id, SourceLocation OpLoc,
                              SourceLocation IdLoc) {
    Kind = UnqualifiedIdKind::IK_LiteralOperatorId;
    Identifier = const_cast<IdentifierInfo *>(Id);
    StartLocation = OpLoc;
    EndLocation = IdLoc;
  }

  /// Specify that this unqualified-id was parsed as a constructor name.
  ///
  /// \param ClassType the class type referred to by the constructor name.
  ///
  /// \param ClassNameLoc the location of the class name.
  ///
  /// \param EndLoc the location of the last token that makes up the type name.
  void setConstructorName(ParsedType ClassType,
                          SourceLocation ClassNameLoc,
                          SourceLocation EndLoc) {
    Kind = UnqualifiedIdKind::IK_ConstructorName;
    StartLocation = ClassNameLoc;
    EndLocation = EndLoc;
    ConstructorName = ClassType;
  }

  /// Specify that this unqualified-id was parsed as a
  /// template-id that names a constructor.
  ///
  /// \param TemplateId the template-id annotation that describes the parsed
  /// template-id. This UnqualifiedId instance will take ownership of the
  /// \p TemplateId and will free it on destruction.
  void setConstructorTemplateId(TemplateIdAnnotation *TemplateId);

  /// Specify that this unqualified-id was parsed as a destructor name.
  ///
  /// \param TildeLoc the location of the '~' that introduces the destructor
  /// name.
  ///
  /// \param ClassType the name of the class referred to by the destructor name.
  void setDestructorName(SourceLocation TildeLoc,
                         ParsedType ClassType,
                         SourceLocation EndLoc) {
    Kind = UnqualifiedIdKind::IK_DestructorName;
    StartLocation = TildeLoc;
    EndLocation = EndLoc;
    DestructorName = ClassType;
  }

  /// Specify that this unqualified-id was parsed as a template-id.
  ///
  /// \param TemplateId the template-id annotation that describes the parsed
  /// template-id. This UnqualifiedId instance will take ownership of the
  /// \p TemplateId and will free it on destruction.
  void setTemplateId(TemplateIdAnnotation *TemplateId);

  /// Specify that this unqualified-id was parsed as a template-name for
  /// a deduction-guide.
  ///
  /// \param Template The parsed template-name.
  /// \param TemplateLoc The location of the parsed template-name.
  void setDeductionGuideName(ParsedTemplateTy Template,
                             SourceLocation TemplateLoc) {
    Kind = UnqualifiedIdKind::IK_DeductionGuideName;
    TemplateName = Template;
    StartLocation = EndLocation = TemplateLoc;
  }

  /// Return the source range that covers this unqualified-id.
  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(StartLocation, EndLocation);
  }
  SourceLocation getBeginLoc() const LLVM_READONLY { return StartLocation; }
  SourceLocation getEndLoc() const LLVM_READONLY { return EndLocation; }
};

/// A set of tokens that has been cached for later parsing.
typedef SmallVector<Token, 4> CachedTokens;

/// One instance of this struct is used for each type in a
/// declarator that is parsed.
///
/// This is intended to be a small value object.
struct DeclaratorChunk {
  enum {
    Pointer, Reference, Array, Function, BlockPointer, MemberPointer, Paren, Pipe
  } Kind;

  /// Loc - The place where this type was defined.
  SourceLocation Loc;
  /// EndLoc - If valid, the place where this chunck ends.
  SourceLocation EndLoc;

  SourceRange getSourceRange() const {
    if (EndLoc.isInvalid())
      return SourceRange(Loc, Loc);
    return SourceRange(Loc, EndLoc);
  }

  ParsedAttributesView AttrList;

  struct PointerTypeInfo {
    /// The type qualifiers: const/volatile/restrict/unaligned/atomic.
    unsigned TypeQuals : 5;

    /// The location of the const-qualifier, if any.
    unsigned ConstQualLoc;

    /// The location of the volatile-qualifier, if any.
    unsigned VolatileQualLoc;

    /// The location of the restrict-qualifier, if any.
    unsigned RestrictQualLoc;

    /// The location of the _Atomic-qualifier, if any.
    unsigned AtomicQualLoc;

    /// The location of the __unaligned-qualifier, if any.
    unsigned UnalignedQualLoc;

    void destroy() {
    }
  };

  struct ReferenceTypeInfo {
    /// The type qualifier: restrict. [GNU] C++ extension
    bool HasRestrict : 1;
    /// True if this is an lvalue reference, false if it's an rvalue reference.
    bool LValueRef : 1;
    void destroy() {
    }
  };

  struct ArrayTypeInfo {
    /// The type qualifiers for the array:
    /// const/volatile/restrict/__unaligned/_Atomic.
    unsigned TypeQuals : 5;

    /// True if this dimension included the 'static' keyword.
    unsigned hasStatic : 1;

    /// True if this dimension was [*].  In this case, NumElts is null.
    unsigned isStar : 1;

    /// This is the size of the array, or null if [] or [*] was specified.
    /// Since the parser is multi-purpose, and we don't want to impose a root
    /// expression class on all clients, NumElts is untyped.
    Expr *NumElts;

    void destroy() {}
  };

  /// ParamInfo - An array of paraminfo objects is allocated whenever a function
  /// declarator is parsed.  There are two interesting styles of parameters
  /// here:
  /// K&R-style identifier lists and parameter type lists.  K&R-style identifier
  /// lists will have information about the identifier, but no type information.
  /// Parameter type lists will have type info (if the actions module provides
  /// it), but may have null identifier info: e.g. for 'void foo(int X, int)'.
  struct ParamInfo {
    IdentifierInfo *Ident;
    SourceLocation IdentLoc;
    Decl *Param;

    /// DefaultArgTokens - When the parameter's default argument
    /// cannot be parsed immediately (because it occurs within the
    /// declaration of a member function), it will be stored here as a
    /// sequence of tokens to be parsed once the class definition is
    /// complete. Non-NULL indicates that there is a default argument.
    std::unique_ptr<CachedTokens> DefaultArgTokens;

    ParamInfo() = default;
    ParamInfo(IdentifierInfo *ident, SourceLocation iloc,
              Decl *param,
              std::unique_ptr<CachedTokens> DefArgTokens = nullptr)
      : Ident(ident), IdentLoc(iloc), Param(param),
        DefaultArgTokens(std::move(DefArgTokens)) {}
  };

  struct TypeAndRange {
    ParsedType Ty;
    SourceRange Range;
  };

  struct FunctionTypeInfo {
    /// hasPrototype - This is true if the function had at least one typed
    /// parameter.  If the function is () or (a,b,c), then it has no prototype,
    /// and is treated as a K&R-style function.
    unsigned hasPrototype : 1;

    /// isVariadic - If this function has a prototype, and if that
    /// proto ends with ',...)', this is true. When true, EllipsisLoc
    /// contains the location of the ellipsis.
    unsigned isVariadic : 1;

    /// Can this declaration be a constructor-style initializer?
    unsigned isAmbiguous : 1;

    /// Whether the ref-qualifier (if any) is an lvalue reference.
    /// Otherwise, it's an rvalue reference.
    unsigned RefQualifierIsLValueRef : 1;

    /// ExceptionSpecType - An ExceptionSpecificationType value.
    unsigned ExceptionSpecType : 4;

    /// DeleteParams - If this is true, we need to delete[] Params.
    unsigned DeleteParams : 1;

    /// HasTrailingReturnType - If this is true, a trailing return type was
    /// specified.
    unsigned HasTrailingReturnType : 1;

    /// The location of the left parenthesis in the source.
    unsigned LParenLoc;

    /// When isVariadic is true, the location of the ellipsis in the source.
    unsigned EllipsisLoc;

    /// The location of the right parenthesis in the source.
    unsigned RParenLoc;

    /// NumParams - This is the number of formal parameters specified by the
    /// declarator.
    unsigned NumParams;

    /// NumExceptionsOrDecls - This is the number of types in the
    /// dynamic-exception-decl, if the function has one. In C, this is the
    /// number of declarations in the function prototype.
    unsigned NumExceptionsOrDecls;

    /// The location of the ref-qualifier, if any.
    ///
    /// If this is an invalid location, there is no ref-qualifier.
    unsigned RefQualifierLoc;

    /// The location of the 'mutable' qualifer in a lambda-declarator, if
    /// any.
    unsigned MutableLoc;

    /// The beginning location of the exception specification, if any.
    unsigned ExceptionSpecLocBeg;

    /// The end location of the exception specification, if any.
    unsigned ExceptionSpecLocEnd;

    /// Params - This is a pointer to a new[]'d array of ParamInfo objects that
    /// describe the parameters specified by this function declarator.  null if
    /// there are no parameters specified.
    ParamInfo *Params;

    /// DeclSpec for the function with the qualifier related info.
    DeclSpec *MethodQualifiers;

    /// AtttibuteFactory for the MethodQualifiers.
    AttributeFactory *QualAttrFactory;

    union {
      /// Pointer to a new[]'d array of TypeAndRange objects that
      /// contain the types in the function's dynamic exception specification
      /// and their locations, if there is one.
      TypeAndRange *Exceptions;

      /// Pointer to the expression in the noexcept-specifier of this
      /// function, if it has one.
      Expr *NoexceptExpr;

      /// Pointer to the cached tokens for an exception-specification
      /// that has not yet been parsed.
      CachedTokens *ExceptionSpecTokens;

      /// Pointer to a new[]'d array of declarations that need to be available
      /// for lookup inside the function body, if one exists. Does not exist in
      /// C++.
      NamedDecl **DeclsInPrototype;
    };

    /// If HasTrailingReturnType is true, this is the trailing return
    /// type specified.
    UnionParsedType TrailingReturnType;

    /// Reset the parameter list to having zero parameters.
    ///
    /// This is used in various places for error recovery.
    void freeParams() {
      for (unsigned I = 0; I < NumParams; ++I)
        Params[I].DefaultArgTokens.reset();
      if (DeleteParams) {
        delete[] Params;
        DeleteParams = false;
      }
      NumParams = 0;
    }

    void destroy() {
      freeParams();
      delete QualAttrFactory;
      delete MethodQualifiers;
      switch (getExceptionSpecType()) {
      default:
        break;
      case EST_Dynamic:
        delete[] Exceptions;
        break;
      case EST_Unparsed:
        delete ExceptionSpecTokens;
        break;
      case EST_None:
        if (NumExceptionsOrDecls != 0)
          delete[] DeclsInPrototype;
        break;
      }
    }

    DeclSpec &getOrCreateMethodQualifiers() {
      if (!MethodQualifiers) {
        QualAttrFactory = new AttributeFactory();
        MethodQualifiers = new DeclSpec(*QualAttrFactory);
      }
      return *MethodQualifiers;
    }

    /// isKNRPrototype - Return true if this is a K&R style identifier list,
    /// like "void foo(a,b,c)".  In a function definition, this will be followed
    /// by the parameter type definitions.
    bool isKNRPrototype() const { return !hasPrototype && NumParams != 0; }

    SourceLocation getLParenLoc() const {
      return SourceLocation::getFromRawEncoding(LParenLoc);
    }

    SourceLocation getEllipsisLoc() const {
      return SourceLocation::getFromRawEncoding(EllipsisLoc);
    }

    SourceLocation getRParenLoc() const {
      return SourceLocation::getFromRawEncoding(RParenLoc);
    }

    SourceLocation getExceptionSpecLocBeg() const {
      return SourceLocation::getFromRawEncoding(ExceptionSpecLocBeg);
    }

    SourceLocation getExceptionSpecLocEnd() const {
      return SourceLocation::getFromRawEncoding(ExceptionSpecLocEnd);
    }

    SourceRange getExceptionSpecRange() const {
      return SourceRange(getExceptionSpecLocBeg(), getExceptionSpecLocEnd());
    }

    /// Retrieve the location of the ref-qualifier, if any.
    SourceLocation getRefQualifierLoc() const {
      return SourceLocation::getFromRawEncoding(RefQualifierLoc);
    }

    /// Retrieve the location of the 'const' qualifier.
    SourceLocation getConstQualifierLoc() const {
      assert(MethodQualifiers);
      return MethodQualifiers->getConstSpecLoc();
    }

    /// Retrieve the location of the 'volatile' qualifier.
    SourceLocation getVolatileQualifierLoc() const {
      assert(MethodQualifiers);
      return MethodQualifiers->getVolatileSpecLoc();
    }

    /// Retrieve the location of the 'restrict' qualifier.
    SourceLocation getRestrictQualifierLoc() const {
      assert(MethodQualifiers);
      return MethodQualifiers->getRestrictSpecLoc();
    }

    /// Retrieve the location of the 'mutable' qualifier, if any.
    SourceLocation getMutableLoc() const {
      return SourceLocation::getFromRawEncoding(MutableLoc);
    }

    /// Determine whether this function declaration contains a
    /// ref-qualifier.
    bool hasRefQualifier() const { return getRefQualifierLoc().isValid(); }

    /// Determine whether this lambda-declarator contains a 'mutable'
    /// qualifier.
    bool hasMutableQualifier() const { return getMutableLoc().isValid(); }

    /// Determine whether this method has qualifiers.
    bool hasMethodTypeQualifiers() const {
      return MethodQualifiers && (MethodQualifiers->getTypeQualifiers() ||
                                  MethodQualifiers->getAttributes().size());
    }

    /// Get the type of exception specification this function has.
    ExceptionSpecificationType getExceptionSpecType() const {
      return static_cast<ExceptionSpecificationType>(ExceptionSpecType);
    }

    /// Get the number of dynamic exception specifications.
    unsigned getNumExceptions() const {
      assert(ExceptionSpecType != EST_None);
      return NumExceptionsOrDecls;
    }

    /// Get the non-parameter decls defined within this function
    /// prototype. Typically these are tag declarations.
    ArrayRef<NamedDecl *> getDeclsInPrototype() const {
      assert(ExceptionSpecType == EST_None);
      return llvm::makeArrayRef(DeclsInPrototype, NumExceptionsOrDecls);
    }

    /// Determine whether this function declarator had a
    /// trailing-return-type.
    bool hasTrailingReturnType() const { return HasTrailingReturnType; }

    /// Get the trailing-return-type for this function declarator.
    ParsedType getTrailingReturnType() const { return TrailingReturnType; }
  };

  struct BlockPointerTypeInfo {
    /// For now, sema will catch these as invalid.
    /// The type qualifiers: const/volatile/restrict/__unaligned/_Atomic.
    unsigned TypeQuals : 5;

    void destroy() {
    }
  };

  struct MemberPointerTypeInfo {
    /// The type qualifiers: const/volatile/restrict/__unaligned/_Atomic.
    unsigned TypeQuals : 5;
    // CXXScopeSpec has a constructor, so it can't be a direct member.
    // So we need some pointer-aligned storage and a bit of trickery.
    alignas(CXXScopeSpec) char ScopeMem[sizeof(CXXScopeSpec)];
    CXXScopeSpec &Scope() {
      return *reinterpret_cast<CXXScopeSpec *>(ScopeMem);
    }
    const CXXScopeSpec &Scope() const {
      return *reinterpret_cast<const CXXScopeSpec *>(ScopeMem);
    }
    void destroy() {
      Scope().~CXXScopeSpec();
    }
  };

  struct PipeTypeInfo {
    /// The access writes.
    unsigned AccessWrites : 3;

    void destroy() {}
  };

  union {
    PointerTypeInfo       Ptr;
    ReferenceTypeInfo     Ref;
    ArrayTypeInfo         Arr;
    FunctionTypeInfo      Fun;
    BlockPointerTypeInfo  Cls;
    MemberPointerTypeInfo Mem;
    PipeTypeInfo          PipeInfo;
  };

  void destroy() {
    switch (Kind) {
    case DeclaratorChunk::Function:      return Fun.destroy();
    case DeclaratorChunk::Pointer:       return Ptr.destroy();
    case DeclaratorChunk::BlockPointer:  return Cls.destroy();
    case DeclaratorChunk::Reference:     return Ref.destroy();
    case DeclaratorChunk::Array:         return Arr.destroy();
    case DeclaratorChunk::MemberPointer: return Mem.destroy();
    case DeclaratorChunk::Paren:         return;
    case DeclaratorChunk::Pipe:          return PipeInfo.destroy();
    }
  }

  /// If there are attributes applied to this declaratorchunk, return
  /// them.
  const ParsedAttributesView &getAttrs() const { return AttrList; }
  ParsedAttributesView &getAttrs() { return AttrList; }

  /// Return a DeclaratorChunk for a pointer.
  static DeclaratorChunk getPointer(unsigned TypeQuals, SourceLocation Loc,
                                    SourceLocation ConstQualLoc,
                                    SourceLocation VolatileQualLoc,
                                    SourceLocation RestrictQualLoc,
                                    SourceLocation AtomicQualLoc,
                                    SourceLocation UnalignedQualLoc) {
    DeclaratorChunk I;
    I.Kind                = Pointer;
    I.Loc                 = Loc;
    I.Ptr.TypeQuals       = TypeQuals;
    I.Ptr.ConstQualLoc    = ConstQualLoc.getRawEncoding();
    I.Ptr.VolatileQualLoc = VolatileQualLoc.getRawEncoding();
    I.Ptr.RestrictQualLoc = RestrictQualLoc.getRawEncoding();
    I.Ptr.AtomicQualLoc   = AtomicQualLoc.getRawEncoding();
    I.Ptr.UnalignedQualLoc = UnalignedQualLoc.getRawEncoding();
    return I;
  }

  /// Return a DeclaratorChunk for a reference.
  static DeclaratorChunk getReference(unsigned TypeQuals, SourceLocation Loc,
                                      bool lvalue) {
    DeclaratorChunk I;
    I.Kind            = Reference;
    I.Loc             = Loc;
    I.Ref.HasRestrict = (TypeQuals & DeclSpec::TQ_restrict) != 0;
    I.Ref.LValueRef   = lvalue;
    return I;
  }

  /// Return a DeclaratorChunk for an array.
  static DeclaratorChunk getArray(unsigned TypeQuals,
                                  bool isStatic, bool isStar, Expr *NumElts,
                                  SourceLocation LBLoc, SourceLocation RBLoc) {
    DeclaratorChunk I;
    I.Kind          = Array;
    I.Loc           = LBLoc;
    I.EndLoc        = RBLoc;
    I.Arr.TypeQuals = TypeQuals;
    I.Arr.hasStatic = isStatic;
    I.Arr.isStar    = isStar;
    I.Arr.NumElts   = NumElts;
    return I;
  }

  /// DeclaratorChunk::getFunction - Return a DeclaratorChunk for a function.
  /// "TheDeclarator" is the declarator that this will be added to.
  static DeclaratorChunk getFunction(bool HasProto,
                                     bool IsAmbiguous,
                                     SourceLocation LParenLoc,
                                     ParamInfo *Params, unsigned NumParams,
                                     SourceLocation EllipsisLoc,
                                     SourceLocation RParenLoc,
                                     bool RefQualifierIsLvalueRef,
                                     SourceLocation RefQualifierLoc,
                                     SourceLocation MutableLoc,
                                     ExceptionSpecificationType ESpecType,
                                     SourceRange ESpecRange,
                                     ParsedType *Exceptions,
                                     SourceRange *ExceptionRanges,
                                     unsigned NumExceptions,
                                     Expr *NoexceptExpr,
                                     CachedTokens *ExceptionSpecTokens,
                                     ArrayRef<NamedDecl *> DeclsInPrototype,
                                     SourceLocation LocalRangeBegin,
                                     SourceLocation LocalRangeEnd,
                                     Declarator &TheDeclarator,
                                     TypeResult TrailingReturnType =
                                                    TypeResult(),
                                     DeclSpec *MethodQualifiers = nullptr);

  /// Return a DeclaratorChunk for a block.
  static DeclaratorChunk getBlockPointer(unsigned TypeQuals,
                                         SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind          = BlockPointer;
    I.Loc           = Loc;
    I.Cls.TypeQuals = TypeQuals;
    return I;
  }

  /// Return a DeclaratorChunk for a block.
  static DeclaratorChunk getPipe(unsigned TypeQuals,
                                 SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind          = Pipe;
    I.Loc           = Loc;
    I.Cls.TypeQuals = TypeQuals;
    return I;
  }

  static DeclaratorChunk getMemberPointer(const CXXScopeSpec &SS,
                                          unsigned TypeQuals,
                                          SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind          = MemberPointer;
    I.Loc           = SS.getBeginLoc();
    I.EndLoc        = Loc;
    I.Mem.TypeQuals = TypeQuals;
    new (I.Mem.ScopeMem) CXXScopeSpec(SS);
    return I;
  }

  /// Return a DeclaratorChunk for a paren.
  static DeclaratorChunk getParen(SourceLocation LParenLoc,
                                  SourceLocation RParenLoc) {
    DeclaratorChunk I;
    I.Kind          = Paren;
    I.Loc           = LParenLoc;
    I.EndLoc        = RParenLoc;
    return I;
  }

  bool isParen() const {
    return Kind == Paren;
  }
};

/// A parsed C++17 decomposition declarator of the form
///   '[' identifier-list ']'
class DecompositionDeclarator {
public:
  struct Binding {
    IdentifierInfo *Name;
    SourceLocation NameLoc;
  };

private:
  /// The locations of the '[' and ']' tokens.
  SourceLocation LSquareLoc, RSquareLoc;

  /// The bindings.
  Binding *Bindings;
  unsigned NumBindings : 31;
  unsigned DeleteBindings : 1;

  friend class Declarator;

public:
  DecompositionDeclarator()
      : Bindings(nullptr), NumBindings(0), DeleteBindings(false) {}
  DecompositionDeclarator(const DecompositionDeclarator &G) = delete;
  DecompositionDeclarator &operator=(const DecompositionDeclarator &G) = delete;
  ~DecompositionDeclarator() {
    if (DeleteBindings)
      delete[] Bindings;
  }

  void clear() {
    LSquareLoc = RSquareLoc = SourceLocation();
    if (DeleteBindings)
      delete[] Bindings;
    Bindings = nullptr;
    NumBindings = 0;
    DeleteBindings = false;
  }

  ArrayRef<Binding> bindings() const {
    return llvm::makeArrayRef(Bindings, NumBindings);
  }

  bool isSet() const { return LSquareLoc.isValid(); }

  SourceLocation getLSquareLoc() const { return LSquareLoc; }
  SourceLocation getRSquareLoc() const { return RSquareLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(LSquareLoc, RSquareLoc);
  }
};

/// Described the kind of function definition (if any) provided for
/// a function.
enum FunctionDefinitionKind {
  FDK_Declaration,
  FDK_Definition,
  FDK_Defaulted,
  FDK_Deleted
};

enum class DeclaratorContext {
    FileContext,         // File scope declaration.
    PrototypeContext,    // Within a function prototype.
    ObjCResultContext,   // An ObjC method result type.
    ObjCParameterContext,// An ObjC method parameter type.
    KNRTypeListContext,  // K&R type definition list for formals.
    TypeNameContext,     // Abstract declarator for types.
    FunctionalCastContext, // Type in a C++ functional cast expression.
    MemberContext,       // Struct/Union field.
    BlockContext,        // Declaration within a block in a function.
    ForContext,          // Declaration within first part of a for loop.
    InitStmtContext,     // Declaration within optional init stmt of if/switch.
    ConditionContext,    // Condition declaration in a C++ if/switch/while/for.
    TemplateParamContext,// Within a template parameter list.
    CXXNewContext,       // C++ new-expression.
    CXXCatchContext,     // C++ catch exception-declaration
    ObjCCatchContext,    // Objective-C catch exception-declaration
    BlockLiteralContext, // Block literal declarator.
    LambdaExprContext,   // Lambda-expression declarator.
    LambdaExprParameterContext, // Lambda-expression parameter declarator.
    ConversionIdContext, // C++ conversion-type-id.
    TrailingReturnContext, // C++11 trailing-type-specifier.
    TrailingReturnVarContext, // C++11 trailing-type-specifier for variable.
    TemplateArgContext,  // Any template argument (in template argument list).
    TemplateTypeArgContext, // Template type argument (in default argument).
    AliasDeclContext,    // C++11 alias-declaration.
    AliasTemplateContext // C++11 alias-declaration template.
};


/// Information about one declarator, including the parsed type
/// information and the identifier.
///
/// When the declarator is fully formed, this is turned into the appropriate
/// Decl object.
///
/// Declarators come in two types: normal declarators and abstract declarators.
/// Abstract declarators are used when parsing types, and don't have an
/// identifier.  Normal declarators do have ID's.
///
/// Instances of this class should be a transient object that lives on the
/// stack, not objects that are allocated in large quantities on the heap.
class Declarator {

private:
  const DeclSpec &DS;
  CXXScopeSpec SS;
  UnqualifiedId Name;
  SourceRange Range;

  /// Where we are parsing this declarator.
  DeclaratorContext Context;

  /// The C++17 structured binding, if any. This is an alternative to a Name.
  DecompositionDeclarator BindingGroup;

  /// DeclTypeInfo - This holds each type that the declarator includes as it is
  /// parsed.  This is pushed from the identifier out, which means that element
  /// #0 will be the most closely bound to the identifier, and
  /// DeclTypeInfo.back() will be the least closely bound.
  SmallVector<DeclaratorChunk, 8> DeclTypeInfo;

  /// InvalidType - Set by Sema::GetTypeForDeclarator().
  unsigned InvalidType : 1;

  /// GroupingParens - Set by Parser::ParseParenDeclarator().
  unsigned GroupingParens : 1;

  /// FunctionDefinition - Is this Declarator for a function or member
  /// definition and, if so, what kind?
  ///
  /// Actually a FunctionDefinitionKind.
  unsigned FunctionDefinition : 2;

  /// Is this Declarator a redeclaration?
  unsigned Redeclaration : 1;

  /// true if the declaration is preceded by \c __extension__.
  unsigned Extension : 1;

  /// Indicates whether this is an Objective-C instance variable.
  unsigned ObjCIvar : 1;

  /// Indicates whether this is an Objective-C 'weak' property.
  unsigned ObjCWeakProperty : 1;

  /// Indicates whether the InlineParams / InlineBindings storage has been used.
  unsigned InlineStorageUsed : 1;

  /// Attrs - Attributes.
  ParsedAttributes Attrs;

  /// The asm label, if specified.
  Expr *AsmLabel;

#ifndef _MSC_VER
  union {
#endif
    /// InlineParams - This is a local array used for the first function decl
    /// chunk to avoid going to the heap for the common case when we have one
    /// function chunk in the declarator.
    DeclaratorChunk::ParamInfo InlineParams[16];
    DecompositionDeclarator::Binding InlineBindings[16];
#ifndef _MSC_VER
  };
#endif

  /// If this is the second or subsequent declarator in this declaration,
  /// the location of the comma before this declarator.
  SourceLocation CommaLoc;

  /// If provided, the source location of the ellipsis used to describe
  /// this declarator as a parameter pack.
  SourceLocation EllipsisLoc;

  friend struct DeclaratorChunk;

public:
  Declarator(const DeclSpec &ds, DeclaratorContext C)
      : DS(ds), Range(ds.getSourceRange()), Context(C),
        InvalidType(DS.getTypeSpecType() == DeclSpec::TST_error),
        GroupingParens(false), FunctionDefinition(FDK_Declaration),
        Redeclaration(false), Extension(false), ObjCIvar(false),
        ObjCWeakProperty(false), InlineStorageUsed(false),
        Attrs(ds.getAttributePool().getFactory()), AsmLabel(nullptr) {}

  ~Declarator() {
    clear();
  }
  /// getDeclSpec - Return the declaration-specifier that this declarator was
  /// declared with.
  const DeclSpec &getDeclSpec() const { return DS; }

  /// getMutableDeclSpec - Return a non-const version of the DeclSpec.  This
  /// should be used with extreme care: declspecs can often be shared between
  /// multiple declarators, so mutating the DeclSpec affects all of the
  /// Declarators.  This should only be done when the declspec is known to not
  /// be shared or when in error recovery etc.
  DeclSpec &getMutableDeclSpec() { return const_cast<DeclSpec &>(DS); }

  AttributePool &getAttributePool() const {
    return Attrs.getPool();
  }

  /// getCXXScopeSpec - Return the C++ scope specifier (global scope or
  /// nested-name-specifier) that is part of the declarator-id.
  const CXXScopeSpec &getCXXScopeSpec() const { return SS; }
  CXXScopeSpec &getCXXScopeSpec() { return SS; }

  /// Retrieve the name specified by this declarator.
  UnqualifiedId &getName() { return Name; }

  const DecompositionDeclarator &getDecompositionDeclarator() const {
    return BindingGroup;
  }

  DeclaratorContext getContext() const { return Context; }

  bool isPrototypeContext() const {
    return (Context == DeclaratorContext::PrototypeContext ||
            Context == DeclaratorContext::ObjCParameterContext ||
            Context == DeclaratorContext::ObjCResultContext ||
            Context == DeclaratorContext::LambdaExprParameterContext);
  }

  /// Get the source range that spans this declarator.
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  void SetSourceRange(SourceRange R) { Range = R; }
  /// SetRangeBegin - Set the start of the source range to Loc, unless it's
  /// invalid.
  void SetRangeBegin(SourceLocation Loc) {
    if (!Loc.isInvalid())
      Range.setBegin(Loc);
  }
  /// SetRangeEnd - Set the end of the source range to Loc, unless it's invalid.
  void SetRangeEnd(SourceLocation Loc) {
    if (!Loc.isInvalid())
      Range.setEnd(Loc);
  }
  /// ExtendWithDeclSpec - Extend the declarator source range to include the
  /// given declspec, unless its location is invalid. Adopts the range start if
  /// the current range start is invalid.
  void ExtendWithDeclSpec(const DeclSpec &DS) {
    SourceRange SR = DS.getSourceRange();
    if (Range.getBegin().isInvalid())
      Range.setBegin(SR.getBegin());
    if (!SR.getEnd().isInvalid())
      Range.setEnd(SR.getEnd());
  }

  /// Reset the contents of this Declarator.
  void clear() {
    SS.clear();
    Name.clear();
    Range = DS.getSourceRange();
    BindingGroup.clear();

    for (unsigned i = 0, e = DeclTypeInfo.size(); i != e; ++i)
      DeclTypeInfo[i].destroy();
    DeclTypeInfo.clear();
    Attrs.clear();
    AsmLabel = nullptr;
    InlineStorageUsed = false;
    ObjCIvar = false;
    ObjCWeakProperty = false;
    CommaLoc = SourceLocation();
    EllipsisLoc = SourceLocation();
  }

  /// mayOmitIdentifier - Return true if the identifier is either optional or
  /// not allowed.  This is true for typenames, prototypes, and template
  /// parameter lists.
  bool mayOmitIdentifier() const {
    switch (Context) {
    case DeclaratorContext::FileContext:
    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::MemberContext:
    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
    case DeclaratorContext::ConditionContext:
      return false;

    case DeclaratorContext::TypeNameContext:
    case DeclaratorContext::FunctionalCastContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::TemplateParamContext:
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TemplateArgContext:
    case DeclaratorContext::TemplateTypeArgContext:
    case DeclaratorContext::TrailingReturnContext:
    case DeclaratorContext::TrailingReturnVarContext:
      return true;
    }
    llvm_unreachable("unknown context kind!");
  }

  /// mayHaveIdentifier - Return true if the identifier is either optional or
  /// required.  This is true for normal declarators and prototypes, but not
  /// typenames.
  bool mayHaveIdentifier() const {
    switch (Context) {
    case DeclaratorContext::FileContext:
    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::MemberContext:
    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
    case DeclaratorContext::ConditionContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::TemplateParamContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
      return true;

    case DeclaratorContext::TypeNameContext:
    case DeclaratorContext::FunctionalCastContext:
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TemplateArgContext:
    case DeclaratorContext::TemplateTypeArgContext:
    case DeclaratorContext::TrailingReturnContext:
    case DeclaratorContext::TrailingReturnVarContext:
      return false;
    }
    llvm_unreachable("unknown context kind!");
  }

  /// Return true if the context permits a C++17 decomposition declarator.
  bool mayHaveDecompositionDeclarator() const {
    switch (Context) {
    case DeclaratorContext::FileContext:
      // FIXME: It's not clear that the proposal meant to allow file-scope
      // structured bindings, but it does.
    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
    case DeclaratorContext::ConditionContext:
      return true;

    case DeclaratorContext::MemberContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::TemplateParamContext:
      // Maybe one day...
      return false;

    // These contexts don't allow any kind of non-abstract declarator.
    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::TypeNameContext:
    case DeclaratorContext::FunctionalCastContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TemplateArgContext:
    case DeclaratorContext::TemplateTypeArgContext:
    case DeclaratorContext::TrailingReturnContext:
    case DeclaratorContext::TrailingReturnVarContext:
      return false;
    }
    llvm_unreachable("unknown context kind!");
  }

  /// mayBeFollowedByCXXDirectInit - Return true if the declarator can be
  /// followed by a C++ direct initializer, e.g. "int x(1);".
  bool mayBeFollowedByCXXDirectInit() const {
    if (hasGroupingParens()) return false;

    if (getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef)
      return false;

    if (getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_extern &&
        Context != DeclaratorContext::FileContext)
      return false;

    // Special names can't have direct initializers.
    if (Name.getKind() != UnqualifiedIdKind::IK_Identifier)
      return false;

    switch (Context) {
    case DeclaratorContext::FileContext:
    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
    case DeclaratorContext::TrailingReturnVarContext:
      return true;

    case DeclaratorContext::ConditionContext:
      // This may not be followed by a direct initializer, but it can't be a
      // function declaration either, and we'd prefer to perform a tentative
      // parse in order to produce the right diagnostic.
      return true;

    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::MemberContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::TemplateParamContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
    case DeclaratorContext::TypeNameContext:
    case DeclaratorContext::FunctionalCastContext: // FIXME
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TemplateArgContext:
    case DeclaratorContext::TemplateTypeArgContext:
    case DeclaratorContext::TrailingReturnContext:
      return false;
    }
    llvm_unreachable("unknown context kind!");
  }

  /// isPastIdentifier - Return true if we have parsed beyond the point where
  /// the name would appear. (This may happen even if we haven't actually parsed
  /// a name, perhaps because this context doesn't require one.)
  bool isPastIdentifier() const { return Name.isValid(); }

  /// hasName - Whether this declarator has a name, which might be an
  /// identifier (accessible via getIdentifier()) or some kind of
  /// special C++ name (constructor, destructor, etc.), or a structured
  /// binding (which is not exactly a name, but occupies the same position).
  bool hasName() const {
    return Name.getKind() != UnqualifiedIdKind::IK_Identifier ||
           Name.Identifier || isDecompositionDeclarator();
  }

  /// Return whether this declarator is a decomposition declarator.
  bool isDecompositionDeclarator() const {
    return BindingGroup.isSet();
  }

  IdentifierInfo *getIdentifier() const {
    if (Name.getKind() == UnqualifiedIdKind::IK_Identifier)
      return Name.Identifier;

    return nullptr;
  }
  SourceLocation getIdentifierLoc() const { return Name.StartLocation; }

  /// Set the name of this declarator to be the given identifier.
  void SetIdentifier(IdentifierInfo *Id, SourceLocation IdLoc) {
    Name.setIdentifier(Id, IdLoc);
  }

  /// Set the decomposition bindings for this declarator.
  void
  setDecompositionBindings(SourceLocation LSquareLoc,
                           ArrayRef<DecompositionDeclarator::Binding> Bindings,
                           SourceLocation RSquareLoc);

  /// AddTypeInfo - Add a chunk to this declarator. Also extend the range to
  /// EndLoc, which should be the last token of the chunk.
  /// This function takes attrs by R-Value reference because it takes ownership
  /// of those attributes from the parameter.
  void AddTypeInfo(const DeclaratorChunk &TI, ParsedAttributes &&attrs,
                   SourceLocation EndLoc) {
    DeclTypeInfo.push_back(TI);
    DeclTypeInfo.back().getAttrs().addAll(attrs.begin(), attrs.end());
    getAttributePool().takeAllFrom(attrs.getPool());

    if (!EndLoc.isInvalid())
      SetRangeEnd(EndLoc);
  }

  /// AddTypeInfo - Add a chunk to this declarator. Also extend the range to
  /// EndLoc, which should be the last token of the chunk.
  void AddTypeInfo(const DeclaratorChunk &TI, SourceLocation EndLoc) {
    DeclTypeInfo.push_back(TI);

    if (!EndLoc.isInvalid())
      SetRangeEnd(EndLoc);
  }

  /// Add a new innermost chunk to this declarator.
  void AddInnermostTypeInfo(const DeclaratorChunk &TI) {
    DeclTypeInfo.insert(DeclTypeInfo.begin(), TI);
  }

  /// Return the number of types applied to this declarator.
  unsigned getNumTypeObjects() const { return DeclTypeInfo.size(); }

  /// Return the specified TypeInfo from this declarator.  TypeInfo #0 is
  /// closest to the identifier.
  const DeclaratorChunk &getTypeObject(unsigned i) const {
    assert(i < DeclTypeInfo.size() && "Invalid type chunk");
    return DeclTypeInfo[i];
  }
  DeclaratorChunk &getTypeObject(unsigned i) {
    assert(i < DeclTypeInfo.size() && "Invalid type chunk");
    return DeclTypeInfo[i];
  }

  typedef SmallVectorImpl<DeclaratorChunk>::const_iterator type_object_iterator;
  typedef llvm::iterator_range<type_object_iterator> type_object_range;

  /// Returns the range of type objects, from the identifier outwards.
  type_object_range type_objects() const {
    return type_object_range(DeclTypeInfo.begin(), DeclTypeInfo.end());
  }

  void DropFirstTypeObject() {
    assert(!DeclTypeInfo.empty() && "No type chunks to drop.");
    DeclTypeInfo.front().destroy();
    DeclTypeInfo.erase(DeclTypeInfo.begin());
  }

  /// Return the innermost (closest to the declarator) chunk of this
  /// declarator that is not a parens chunk, or null if there are no
  /// non-parens chunks.
  const DeclaratorChunk *getInnermostNonParenChunk() const {
    for (unsigned i = 0, i_end = DeclTypeInfo.size(); i < i_end; ++i) {
      if (!DeclTypeInfo[i].isParen())
        return &DeclTypeInfo[i];
    }
    return nullptr;
  }

  /// Return the outermost (furthest from the declarator) chunk of
  /// this declarator that is not a parens chunk, or null if there are
  /// no non-parens chunks.
  const DeclaratorChunk *getOutermostNonParenChunk() const {
    for (unsigned i = DeclTypeInfo.size(), i_end = 0; i != i_end; --i) {
      if (!DeclTypeInfo[i-1].isParen())
        return &DeclTypeInfo[i-1];
    }
    return nullptr;
  }

  /// isArrayOfUnknownBound - This method returns true if the declarator
  /// is a declarator for an array of unknown bound (looking through
  /// parentheses).
  bool isArrayOfUnknownBound() const {
    const DeclaratorChunk *chunk = getInnermostNonParenChunk();
    return (chunk && chunk->Kind == DeclaratorChunk::Array &&
            !chunk->Arr.NumElts);
  }

  /// isFunctionDeclarator - This method returns true if the declarator
  /// is a function declarator (looking through parentheses).
  /// If true is returned, then the reference type parameter idx is
  /// assigned with the index of the declaration chunk.
  bool isFunctionDeclarator(unsigned& idx) const {
    for (unsigned i = 0, i_end = DeclTypeInfo.size(); i < i_end; ++i) {
      switch (DeclTypeInfo[i].Kind) {
      case DeclaratorChunk::Function:
        idx = i;
        return true;
      case DeclaratorChunk::Paren:
        continue;
      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::Reference:
      case DeclaratorChunk::Array:
      case DeclaratorChunk::BlockPointer:
      case DeclaratorChunk::MemberPointer:
      case DeclaratorChunk::Pipe:
        return false;
      }
      llvm_unreachable("Invalid type chunk");
    }
    return false;
  }

  /// isFunctionDeclarator - Once this declarator is fully parsed and formed,
  /// this method returns true if the identifier is a function declarator
  /// (looking through parentheses).
  bool isFunctionDeclarator() const {
    unsigned index;
    return isFunctionDeclarator(index);
  }

  /// getFunctionTypeInfo - Retrieves the function type info object
  /// (looking through parentheses).
  DeclaratorChunk::FunctionTypeInfo &getFunctionTypeInfo() {
    assert(isFunctionDeclarator() && "Not a function declarator!");
    unsigned index = 0;
    isFunctionDeclarator(index);
    return DeclTypeInfo[index].Fun;
  }

  /// getFunctionTypeInfo - Retrieves the function type info object
  /// (looking through parentheses).
  const DeclaratorChunk::FunctionTypeInfo &getFunctionTypeInfo() const {
    return const_cast<Declarator*>(this)->getFunctionTypeInfo();
  }

  /// Determine whether the declaration that will be produced from
  /// this declaration will be a function.
  ///
  /// A declaration can declare a function even if the declarator itself
  /// isn't a function declarator, if the type specifier refers to a function
  /// type. This routine checks for both cases.
  bool isDeclarationOfFunction() const;

  /// Return true if this declaration appears in a context where a
  /// function declarator would be a function declaration.
  bool isFunctionDeclarationContext() const {
    if (getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef)
      return false;

    switch (Context) {
    case DeclaratorContext::FileContext:
    case DeclaratorContext::MemberContext:
    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
      return true;

    case DeclaratorContext::ConditionContext:
    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::TypeNameContext:
    case DeclaratorContext::FunctionalCastContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::TemplateParamContext:
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TemplateArgContext:
    case DeclaratorContext::TemplateTypeArgContext:
    case DeclaratorContext::TrailingReturnContext:
    case DeclaratorContext::TrailingReturnVarContext:
      return false;
    }
    llvm_unreachable("unknown context kind!");
  }

  /// Determine whether this declaration appears in a context where an
  /// expression could appear.
  bool isExpressionContext() const {
    switch (Context) {
    case DeclaratorContext::FileContext:
    case DeclaratorContext::KNRTypeListContext:
    case DeclaratorContext::MemberContext:

    // FIXME: sizeof(...) permits an expression.
    case DeclaratorContext::TypeNameContext:

    case DeclaratorContext::FunctionalCastContext:
    case DeclaratorContext::AliasDeclContext:
    case DeclaratorContext::AliasTemplateContext:
    case DeclaratorContext::PrototypeContext:
    case DeclaratorContext::LambdaExprParameterContext:
    case DeclaratorContext::ObjCParameterContext:
    case DeclaratorContext::ObjCResultContext:
    case DeclaratorContext::TemplateParamContext:
    case DeclaratorContext::CXXNewContext:
    case DeclaratorContext::CXXCatchContext:
    case DeclaratorContext::ObjCCatchContext:
    case DeclaratorContext::BlockLiteralContext:
    case DeclaratorContext::LambdaExprContext:
    case DeclaratorContext::ConversionIdContext:
    case DeclaratorContext::TrailingReturnContext:
    case DeclaratorContext::TrailingReturnVarContext:
    case DeclaratorContext::TemplateTypeArgContext:
      return false;

    case DeclaratorContext::BlockContext:
    case DeclaratorContext::ForContext:
    case DeclaratorContext::InitStmtContext:
    case DeclaratorContext::ConditionContext:
    case DeclaratorContext::TemplateArgContext:
      return true;
    }

    llvm_unreachable("unknown context kind!");
  }

  /// Return true if a function declarator at this position would be a
  /// function declaration.
  bool isFunctionDeclaratorAFunctionDeclaration() const {
    if (!isFunctionDeclarationContext())
      return false;

    for (unsigned I = 0, N = getNumTypeObjects(); I != N; ++I)
      if (getTypeObject(I).Kind != DeclaratorChunk::Paren)
        return false;

    return true;
  }

  /// Determine whether a trailing return type was written (at any
  /// level) within this declarator.
  bool hasTrailingReturnType() const {
    for (const auto &Chunk : type_objects())
      if (Chunk.Kind == DeclaratorChunk::Function &&
          Chunk.Fun.hasTrailingReturnType())
        return true;
    return false;
  }

  /// takeAttributes - Takes attributes from the given parsed-attributes
  /// set and add them to this declarator.
  ///
  /// These examples both add 3 attributes to "var":
  ///  short int var __attribute__((aligned(16),common,deprecated));
  ///  short int x, __attribute__((aligned(16)) var
  ///                                 __attribute__((common,deprecated));
  ///
  /// Also extends the range of the declarator.
  void takeAttributes(ParsedAttributes &attrs, SourceLocation lastLoc) {
    Attrs.takeAllFrom(attrs);

    if (!lastLoc.isInvalid())
      SetRangeEnd(lastLoc);
  }

  const ParsedAttributes &getAttributes() const { return Attrs; }
  ParsedAttributes &getAttributes() { return Attrs; }

  /// hasAttributes - do we contain any attributes?
  bool hasAttributes() const {
    if (!getAttributes().empty() || getDeclSpec().hasAttributes())
      return true;
    for (unsigned i = 0, e = getNumTypeObjects(); i != e; ++i)
      if (!getTypeObject(i).getAttrs().empty())
        return true;
    return false;
  }

  /// Return a source range list of C++11 attributes associated
  /// with the declarator.
  void getCXX11AttributeRanges(SmallVectorImpl<SourceRange> &Ranges) {
    for (const ParsedAttr &AL : Attrs)
      if (AL.isCXX11Attribute())
        Ranges.push_back(AL.getRange());
  }

  void setAsmLabel(Expr *E) { AsmLabel = E; }
  Expr *getAsmLabel() const { return AsmLabel; }

  void setExtension(bool Val = true) { Extension = Val; }
  bool getExtension() const { return Extension; }

  void setObjCIvar(bool Val = true) { ObjCIvar = Val; }
  bool isObjCIvar() const { return ObjCIvar; }

  void setObjCWeakProperty(bool Val = true) { ObjCWeakProperty = Val; }
  bool isObjCWeakProperty() const { return ObjCWeakProperty; }

  void setInvalidType(bool Val = true) { InvalidType = Val; }
  bool isInvalidType() const {
    return InvalidType || DS.getTypeSpecType() == DeclSpec::TST_error;
  }

  void setGroupingParens(bool flag) { GroupingParens = flag; }
  bool hasGroupingParens() const { return GroupingParens; }

  bool isFirstDeclarator() const { return !CommaLoc.isValid(); }
  SourceLocation getCommaLoc() const { return CommaLoc; }
  void setCommaLoc(SourceLocation CL) { CommaLoc = CL; }

  bool hasEllipsis() const { return EllipsisLoc.isValid(); }
  SourceLocation getEllipsisLoc() const { return EllipsisLoc; }
  void setEllipsisLoc(SourceLocation EL) { EllipsisLoc = EL; }

  void setFunctionDefinitionKind(FunctionDefinitionKind Val) {
    FunctionDefinition = Val;
  }

  bool isFunctionDefinition() const {
    return getFunctionDefinitionKind() != FDK_Declaration;
  }

  FunctionDefinitionKind getFunctionDefinitionKind() const {
    return (FunctionDefinitionKind)FunctionDefinition;
  }

  /// Returns true if this declares a real member and not a friend.
  bool isFirstDeclarationOfMember() {
    return getContext() == DeclaratorContext::MemberContext &&
           !getDeclSpec().isFriendSpecified();
  }

  /// Returns true if this declares a static member.  This cannot be called on a
  /// declarator outside of a MemberContext because we won't know until
  /// redeclaration time if the decl is static.
  bool isStaticMember();

  /// Returns true if this declares a constructor or a destructor.
  bool isCtorOrDtor();

  void setRedeclaration(bool Val) { Redeclaration = Val; }
  bool isRedeclaration() const { return Redeclaration; }
};

/// This little struct is used to capture information about
/// structure field declarators, which is basically just a bitfield size.
struct FieldDeclarator {
  Declarator D;
  Expr *BitfieldSize;
  explicit FieldDeclarator(const DeclSpec &DS)
      : D(DS, DeclaratorContext::MemberContext),
        BitfieldSize(nullptr) {}
};

/// Represents a C++11 virt-specifier-seq.
class VirtSpecifiers {
public:
  enum Specifier {
    VS_None = 0,
    VS_Override = 1,
    VS_Final = 2,
    VS_Sealed = 4,
    // Represents the __final keyword, which is legal for gcc in pre-C++11 mode.
    VS_GNU_Final = 8
  };

  VirtSpecifiers() : Specifiers(0), LastSpecifier(VS_None) { }

  bool SetSpecifier(Specifier VS, SourceLocation Loc,
                    const char *&PrevSpec);

  bool isUnset() const { return Specifiers == 0; }

  bool isOverrideSpecified() const { return Specifiers & VS_Override; }
  SourceLocation getOverrideLoc() const { return VS_overrideLoc; }

  bool isFinalSpecified() const { return Specifiers & (VS_Final | VS_Sealed | VS_GNU_Final); }
  bool isFinalSpelledSealed() const { return Specifiers & VS_Sealed; }
  SourceLocation getFinalLoc() const { return VS_finalLoc; }

  void clear() { Specifiers = 0; }

  static const char *getSpecifierName(Specifier VS);

  SourceLocation getFirstLocation() const { return FirstLocation; }
  SourceLocation getLastLocation() const { return LastLocation; }
  Specifier getLastSpecifier() const { return LastSpecifier; }

private:
  unsigned Specifiers;
  Specifier LastSpecifier;

  SourceLocation VS_overrideLoc, VS_finalLoc;
  SourceLocation FirstLocation;
  SourceLocation LastLocation;
};

enum class LambdaCaptureInitKind {
  NoInit,     //!< [a]
  CopyInit,   //!< [a = b], [a = {b}]
  DirectInit, //!< [a(b)]
  ListInit    //!< [a{b}]
};

/// Represents a complete lambda introducer.
struct LambdaIntroducer {
  /// An individual capture in a lambda introducer.
  struct LambdaCapture {
    LambdaCaptureKind Kind;
    SourceLocation Loc;
    IdentifierInfo *Id;
    SourceLocation EllipsisLoc;
    LambdaCaptureInitKind InitKind;
    ExprResult Init;
    ParsedType InitCaptureType;
    SourceRange ExplicitRange;

    LambdaCapture(LambdaCaptureKind Kind, SourceLocation Loc,
                  IdentifierInfo *Id, SourceLocation EllipsisLoc,
                  LambdaCaptureInitKind InitKind, ExprResult Init,
                  ParsedType InitCaptureType,
                  SourceRange ExplicitRange)
        : Kind(Kind), Loc(Loc), Id(Id), EllipsisLoc(EllipsisLoc),
          InitKind(InitKind), Init(Init), InitCaptureType(InitCaptureType),
          ExplicitRange(ExplicitRange) {}
  };

  SourceRange Range;
  SourceLocation DefaultLoc;
  LambdaCaptureDefault Default;
  SmallVector<LambdaCapture, 4> Captures;

  LambdaIntroducer()
    : Default(LCD_None) {}

  /// Append a capture in a lambda introducer.
  void addCapture(LambdaCaptureKind Kind,
                  SourceLocation Loc,
                  IdentifierInfo* Id,
                  SourceLocation EllipsisLoc,
                  LambdaCaptureInitKind InitKind,
                  ExprResult Init,
                  ParsedType InitCaptureType,
                  SourceRange ExplicitRange) {
    Captures.push_back(LambdaCapture(Kind, Loc, Id, EllipsisLoc, InitKind, Init,
                                     InitCaptureType, ExplicitRange));
  }
};

} // end namespace clang

#endif // LLVM_CLANG_SEMA_DECLSPEC_H
