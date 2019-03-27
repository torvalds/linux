//===- DeclObjC.h - Classes for representing declarations -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the DeclObjC interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLOBJC_H
#define LLVM_CLANG_AST_DECLOBJC_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/SelectorLocationsKind.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace clang {

class ASTContext;
class CompoundStmt;
class CXXCtorInitializer;
class Expr;
class ObjCCategoryDecl;
class ObjCCategoryImplDecl;
class ObjCImplementationDecl;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class ObjCPropertyDecl;
class ObjCPropertyImplDecl;
class ObjCProtocolDecl;
class Stmt;

class ObjCListBase {
protected:
  /// List is an array of pointers to objects that are not owned by this object.
  void **List = nullptr;
  unsigned NumElts = 0;

public:
  ObjCListBase() = default;
  ObjCListBase(const ObjCListBase &) = delete;
  ObjCListBase &operator=(const ObjCListBase &) = delete;

  unsigned size() const { return NumElts; }
  bool empty() const { return NumElts == 0; }

protected:
  void set(void *const* InList, unsigned Elts, ASTContext &Ctx);
};

/// ObjCList - This is a simple template class used to hold various lists of
/// decls etc, which is heavily used by the ObjC front-end.  This only use case
/// this supports is setting the list all at once and then reading elements out
/// of it.
template <typename T>
class ObjCList : public ObjCListBase {
public:
  void set(T* const* InList, unsigned Elts, ASTContext &Ctx) {
    ObjCListBase::set(reinterpret_cast<void*const*>(InList), Elts, Ctx);
  }

  using iterator = T* const *;

  iterator begin() const { return (iterator)List; }
  iterator end() const { return (iterator)List+NumElts; }

  T* operator[](unsigned Idx) const {
    assert(Idx < NumElts && "Invalid access");
    return (T*)List[Idx];
  }
};

/// A list of Objective-C protocols, along with the source
/// locations at which they were referenced.
class ObjCProtocolList : public ObjCList<ObjCProtocolDecl> {
  SourceLocation *Locations = nullptr;

  using ObjCList<ObjCProtocolDecl>::set;

public:
  ObjCProtocolList() = default;

  using loc_iterator = const SourceLocation *;

  loc_iterator loc_begin() const { return Locations; }
  loc_iterator loc_end() const { return Locations + size(); }

  void set(ObjCProtocolDecl* const* InList, unsigned Elts,
           const SourceLocation *Locs, ASTContext &Ctx);
};

/// ObjCMethodDecl - Represents an instance or class method declaration.
/// ObjC methods can be declared within 4 contexts: class interfaces,
/// categories, protocols, and class implementations. While C++ member
/// functions leverage C syntax, Objective-C method syntax is modeled after
/// Smalltalk (using colons to specify argument types/expressions).
/// Here are some brief examples:
///
/// Setter/getter instance methods:
/// - (void)setMenu:(NSMenu *)menu;
/// - (NSMenu *)menu;
///
/// Instance method that takes 2 NSView arguments:
/// - (void)replaceSubview:(NSView *)oldView with:(NSView *)newView;
///
/// Getter class method:
/// + (NSMenu *)defaultMenu;
///
/// A selector represents a unique name for a method. The selector names for
/// the above methods are setMenu:, menu, replaceSubview:with:, and defaultMenu.
///
class ObjCMethodDecl : public NamedDecl, public DeclContext {
  // This class stores some data in DeclContext::ObjCMethodDeclBits
  // to save some space. Use the provided accessors to access it.

public:
  enum ImplementationControl { None, Required, Optional };

private:
  /// Return type of this method.
  QualType MethodDeclType;

  /// Type source information for the return type.
  TypeSourceInfo *ReturnTInfo;

  /// Array of ParmVarDecls for the formal parameters of this method
  /// and optionally followed by selector locations.
  void *ParamsAndSelLocs = nullptr;
  unsigned NumParams = 0;

  /// List of attributes for this method declaration.
  SourceLocation DeclEndLoc; // the location of the ';' or '{'.

  /// The following are only used for method definitions, null otherwise.
  LazyDeclStmtPtr Body;

  /// SelfDecl - Decl for the implicit self parameter. This is lazily
  /// constructed by createImplicitParams.
  ImplicitParamDecl *SelfDecl = nullptr;

  /// CmdDecl - Decl for the implicit _cmd parameter. This is lazily
  /// constructed by createImplicitParams.
  ImplicitParamDecl *CmdDecl = nullptr;

  ObjCMethodDecl(SourceLocation beginLoc, SourceLocation endLoc,
                 Selector SelInfo, QualType T, TypeSourceInfo *ReturnTInfo,
                 DeclContext *contextDecl, bool isInstance = true,
                 bool isVariadic = false, bool isPropertyAccessor = false,
                 bool isImplicitlyDeclared = false, bool isDefined = false,
                 ImplementationControl impControl = None,
                 bool HasRelatedResultType = false);

  SelectorLocationsKind getSelLocsKind() const {
    return static_cast<SelectorLocationsKind>(ObjCMethodDeclBits.SelLocsKind);
  }

  void setSelLocsKind(SelectorLocationsKind Kind) {
    ObjCMethodDeclBits.SelLocsKind = Kind;
  }

  bool hasStandardSelLocs() const {
    return getSelLocsKind() != SelLoc_NonStandard;
  }

  /// Get a pointer to the stored selector identifiers locations array.
  /// No locations will be stored if HasStandardSelLocs is true.
  SourceLocation *getStoredSelLocs() {
    return reinterpret_cast<SourceLocation *>(getParams() + NumParams);
  }
  const SourceLocation *getStoredSelLocs() const {
    return reinterpret_cast<const SourceLocation *>(getParams() + NumParams);
  }

  /// Get a pointer to the stored selector identifiers locations array.
  /// No locations will be stored if HasStandardSelLocs is true.
  ParmVarDecl **getParams() {
    return reinterpret_cast<ParmVarDecl **>(ParamsAndSelLocs);
  }
  const ParmVarDecl *const *getParams() const {
    return reinterpret_cast<const ParmVarDecl *const *>(ParamsAndSelLocs);
  }

  /// Get the number of stored selector identifiers locations.
  /// No locations will be stored if HasStandardSelLocs is true.
  unsigned getNumStoredSelLocs() const {
    if (hasStandardSelLocs())
      return 0;
    return getNumSelectorLocs();
  }

  void setParamsAndSelLocs(ASTContext &C,
                           ArrayRef<ParmVarDecl*> Params,
                           ArrayRef<SourceLocation> SelLocs);

  /// A definition will return its interface declaration.
  /// An interface declaration will return its definition.
  /// Otherwise it will return itself.
  ObjCMethodDecl *getNextRedeclarationImpl() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ObjCMethodDecl *
  Create(ASTContext &C, SourceLocation beginLoc, SourceLocation endLoc,
         Selector SelInfo, QualType T, TypeSourceInfo *ReturnTInfo,
         DeclContext *contextDecl, bool isInstance = true,
         bool isVariadic = false, bool isPropertyAccessor = false,
         bool isImplicitlyDeclared = false, bool isDefined = false,
         ImplementationControl impControl = None,
         bool HasRelatedResultType = false);

  static ObjCMethodDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  ObjCMethodDecl *getCanonicalDecl() override;
  const ObjCMethodDecl *getCanonicalDecl() const {
    return const_cast<ObjCMethodDecl*>(this)->getCanonicalDecl();
  }

  ObjCDeclQualifier getObjCDeclQualifier() const {
    return static_cast<ObjCDeclQualifier>(ObjCMethodDeclBits.objcDeclQualifier);
  }

  void setObjCDeclQualifier(ObjCDeclQualifier QV) {
    ObjCMethodDeclBits.objcDeclQualifier = QV;
  }

  /// Determine whether this method has a result type that is related
  /// to the message receiver's type.
  bool hasRelatedResultType() const {
    return ObjCMethodDeclBits.RelatedResultType;
  }

  /// Note whether this method has a related result type.
  void setRelatedResultType(bool RRT = true) {
    ObjCMethodDeclBits.RelatedResultType = RRT;
  }

  /// True if this is a method redeclaration in the same interface.
  bool isRedeclaration() const { return ObjCMethodDeclBits.IsRedeclaration; }
  void setIsRedeclaration(bool RD) { ObjCMethodDeclBits.IsRedeclaration = RD; }
  void setAsRedeclaration(const ObjCMethodDecl *PrevMethod);

  /// True if redeclared in the same interface.
  bool hasRedeclaration() const { return ObjCMethodDeclBits.HasRedeclaration; }
  void setHasRedeclaration(bool HRD) const {
    ObjCMethodDeclBits.HasRedeclaration = HRD;
  }

  /// Returns the location where the declarator ends. It will be
  /// the location of ';' for a method declaration and the location of '{'
  /// for a method definition.
  SourceLocation getDeclaratorEndLoc() const { return DeclEndLoc; }

  // Location information, modeled after the Stmt API.
  SourceLocation getBeginLoc() const LLVM_READONLY { return getLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY;
  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocation(), getEndLoc());
  }

  SourceLocation getSelectorStartLoc() const {
    if (isImplicit())
      return getBeginLoc();
    return getSelectorLoc(0);
  }

  SourceLocation getSelectorLoc(unsigned Index) const {
    assert(Index < getNumSelectorLocs() && "Index out of range!");
    if (hasStandardSelLocs())
      return getStandardSelectorLoc(Index, getSelector(),
                                   getSelLocsKind() == SelLoc_StandardWithSpace,
                                    parameters(),
                                   DeclEndLoc);
    return getStoredSelLocs()[Index];
  }

  void getSelectorLocs(SmallVectorImpl<SourceLocation> &SelLocs) const;

  unsigned getNumSelectorLocs() const {
    if (isImplicit())
      return 0;
    Selector Sel = getSelector();
    if (Sel.isUnarySelector())
      return 1;
    return Sel.getNumArgs();
  }

  ObjCInterfaceDecl *getClassInterface();
  const ObjCInterfaceDecl *getClassInterface() const {
    return const_cast<ObjCMethodDecl*>(this)->getClassInterface();
  }

  Selector getSelector() const { return getDeclName().getObjCSelector(); }

  QualType getReturnType() const { return MethodDeclType; }
  void setReturnType(QualType T) { MethodDeclType = T; }
  SourceRange getReturnTypeSourceRange() const;

  /// Determine the type of an expression that sends a message to this
  /// function. This replaces the type parameters with the types they would
  /// get if the receiver was parameterless (e.g. it may replace the type
  /// parameter with 'id').
  QualType getSendResultType() const;

  /// Determine the type of an expression that sends a message to this
  /// function with the given receiver type.
  QualType getSendResultType(QualType receiverType) const;

  TypeSourceInfo *getReturnTypeSourceInfo() const { return ReturnTInfo; }
  void setReturnTypeSourceInfo(TypeSourceInfo *TInfo) { ReturnTInfo = TInfo; }

  // Iterator access to formal parameters.
  unsigned param_size() const { return NumParams; }

  using param_const_iterator = const ParmVarDecl *const *;
  using param_iterator = ParmVarDecl *const *;
  using param_range = llvm::iterator_range<param_iterator>;
  using param_const_range = llvm::iterator_range<param_const_iterator>;

  param_const_iterator param_begin() const {
    return param_const_iterator(getParams());
  }

  param_const_iterator param_end() const {
    return param_const_iterator(getParams() + NumParams);
  }

  param_iterator param_begin() { return param_iterator(getParams()); }
  param_iterator param_end() { return param_iterator(getParams() + NumParams); }

  // This method returns and of the parameters which are part of the selector
  // name mangling requirements.
  param_const_iterator sel_param_end() const {
    return param_begin() + getSelector().getNumArgs();
  }

  // ArrayRef access to formal parameters.  This should eventually
  // replace the iterator interface above.
  ArrayRef<ParmVarDecl*> parameters() const {
    return llvm::makeArrayRef(const_cast<ParmVarDecl**>(getParams()),
                              NumParams);
  }

  ParmVarDecl *getParamDecl(unsigned Idx) {
    assert(Idx < NumParams && "Index out of bounds!");
    return getParams()[Idx];
  }
  const ParmVarDecl *getParamDecl(unsigned Idx) const {
    return const_cast<ObjCMethodDecl *>(this)->getParamDecl(Idx);
  }

  /// Sets the method's parameters and selector source locations.
  /// If the method is implicit (not coming from source) \p SelLocs is
  /// ignored.
  void setMethodParams(ASTContext &C,
                       ArrayRef<ParmVarDecl*> Params,
                       ArrayRef<SourceLocation> SelLocs = llvm::None);

  // Iterator access to parameter types.
  struct GetTypeFn {
    QualType operator()(const ParmVarDecl *PD) const { return PD->getType(); }
  };

  using param_type_iterator =
      llvm::mapped_iterator<param_const_iterator, GetTypeFn>;

  param_type_iterator param_type_begin() const {
    return llvm::map_iterator(param_begin(), GetTypeFn());
  }

  param_type_iterator param_type_end() const {
    return llvm::map_iterator(param_end(), GetTypeFn());
  }

  /// createImplicitParams - Used to lazily create the self and cmd
  /// implict parameters. This must be called prior to using getSelfDecl()
  /// or getCmdDecl(). The call is ignored if the implicit parameters
  /// have already been created.
  void createImplicitParams(ASTContext &Context, const ObjCInterfaceDecl *ID);

  /// \return the type for \c self and set \arg selfIsPseudoStrong and
  /// \arg selfIsConsumed accordingly.
  QualType getSelfType(ASTContext &Context, const ObjCInterfaceDecl *OID,
                       bool &selfIsPseudoStrong, bool &selfIsConsumed);

  ImplicitParamDecl * getSelfDecl() const { return SelfDecl; }
  void setSelfDecl(ImplicitParamDecl *SD) { SelfDecl = SD; }
  ImplicitParamDecl * getCmdDecl() const { return CmdDecl; }
  void setCmdDecl(ImplicitParamDecl *CD) { CmdDecl = CD; }

  /// Determines the family of this method.
  ObjCMethodFamily getMethodFamily() const;

  bool isInstanceMethod() const { return ObjCMethodDeclBits.IsInstance; }
  void setInstanceMethod(bool isInst) {
    ObjCMethodDeclBits.IsInstance = isInst;
  }

  bool isVariadic() const { return ObjCMethodDeclBits.IsVariadic; }
  void setVariadic(bool isVar) { ObjCMethodDeclBits.IsVariadic = isVar; }

  bool isClassMethod() const { return !isInstanceMethod(); }

  bool isPropertyAccessor() const {
    return ObjCMethodDeclBits.IsPropertyAccessor;
  }

  void setPropertyAccessor(bool isAccessor) {
    ObjCMethodDeclBits.IsPropertyAccessor = isAccessor;
  }

  bool isDefined() const { return ObjCMethodDeclBits.IsDefined; }
  void setDefined(bool isDefined) { ObjCMethodDeclBits.IsDefined = isDefined; }

  /// Whether this method overrides any other in the class hierarchy.
  ///
  /// A method is said to override any method in the class's
  /// base classes, its protocols, or its categories' protocols, that has
  /// the same selector and is of the same kind (class or instance).
  /// A method in an implementation is not considered as overriding the same
  /// method in the interface or its categories.
  bool isOverriding() const { return ObjCMethodDeclBits.IsOverriding; }
  void setOverriding(bool IsOver) { ObjCMethodDeclBits.IsOverriding = IsOver; }

  /// Return overridden methods for the given \p Method.
  ///
  /// An ObjC method is considered to override any method in the class's
  /// base classes (and base's categories), its protocols, or its categories'
  /// protocols, that has
  /// the same selector and is of the same kind (class or instance).
  /// A method in an implementation is not considered as overriding the same
  /// method in the interface or its categories.
  void getOverriddenMethods(
                     SmallVectorImpl<const ObjCMethodDecl *> &Overridden) const;

  /// True if the method was a definition but its body was skipped.
  bool hasSkippedBody() const { return ObjCMethodDeclBits.HasSkippedBody; }
  void setHasSkippedBody(bool Skipped = true) {
    ObjCMethodDeclBits.HasSkippedBody = Skipped;
  }

  /// Returns the property associated with this method's selector.
  ///
  /// Note that even if this particular method is not marked as a property
  /// accessor, it is still possible for it to match a property declared in a
  /// superclass. Pass \c false if you only want to check the current class.
  const ObjCPropertyDecl *findPropertyDecl(bool CheckOverrides = true) const;

  // Related to protocols declared in  \@protocol
  void setDeclImplementation(ImplementationControl ic) {
    ObjCMethodDeclBits.DeclImplementation = ic;
  }

  ImplementationControl getImplementationControl() const {
    return ImplementationControl(ObjCMethodDeclBits.DeclImplementation);
  }

  bool isOptional() const {
    return getImplementationControl() == Optional;
  }

  /// Returns true if this specific method declaration is marked with the
  /// designated initializer attribute.
  bool isThisDeclarationADesignatedInitializer() const;

  /// Returns true if the method selector resolves to a designated initializer
  /// in the class's interface.
  ///
  /// \param InitMethod if non-null and the function returns true, it receives
  /// the method declaration that was marked with the designated initializer
  /// attribute.
  bool isDesignatedInitializerForTheInterface(
      const ObjCMethodDecl **InitMethod = nullptr) const;

  /// Determine whether this method has a body.
  bool hasBody() const override { return Body.isValid(); }

  /// Retrieve the body of this method, if it has one.
  Stmt *getBody() const override;

  void setLazyBody(uint64_t Offset) { Body = Offset; }

  CompoundStmt *getCompoundBody() { return (CompoundStmt*)getBody(); }
  void setBody(Stmt *B) { Body = B; }

  /// Returns whether this specific method is a definition.
  bool isThisDeclarationADefinition() const { return hasBody(); }

  /// Is this method defined in the NSObject base class?
  bool definedInNSObject(const ASTContext &) const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCMethod; }

  static DeclContext *castToDeclContext(const ObjCMethodDecl *D) {
    return static_cast<DeclContext *>(const_cast<ObjCMethodDecl*>(D));
  }

  static ObjCMethodDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<ObjCMethodDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Describes the variance of a given generic parameter.
enum class ObjCTypeParamVariance : uint8_t {
  /// The parameter is invariant: must match exactly.
  Invariant,

  /// The parameter is covariant, e.g., X<T> is a subtype of X<U> when
  /// the type parameter is covariant and T is a subtype of U.
  Covariant,

  /// The parameter is contravariant, e.g., X<T> is a subtype of X<U>
  /// when the type parameter is covariant and U is a subtype of T.
  Contravariant,
};

/// Represents the declaration of an Objective-C type parameter.
///
/// \code
/// @interface NSDictionary<Key : id<NSCopying>, Value>
/// @end
/// \endcode
///
/// In the example above, both \c Key and \c Value are represented by
/// \c ObjCTypeParamDecl. \c Key has an explicit bound of \c id<NSCopying>,
/// while \c Value gets an implicit bound of \c id.
///
/// Objective-C type parameters are typedef-names in the grammar,
class ObjCTypeParamDecl : public TypedefNameDecl {
  /// Index of this type parameter in the type parameter list.
  unsigned Index : 14;

  /// The variance of the type parameter.
  unsigned Variance : 2;

  /// The location of the variance, if any.
  SourceLocation VarianceLoc;

  /// The location of the ':', which will be valid when the bound was
  /// explicitly specified.
  SourceLocation ColonLoc;

  ObjCTypeParamDecl(ASTContext &ctx, DeclContext *dc,
                    ObjCTypeParamVariance variance, SourceLocation varianceLoc,
                    unsigned index,
                    SourceLocation nameLoc, IdentifierInfo *name,
                    SourceLocation colonLoc, TypeSourceInfo *boundInfo)
      : TypedefNameDecl(ObjCTypeParam, ctx, dc, nameLoc, nameLoc, name,
                        boundInfo),
        Index(index), Variance(static_cast<unsigned>(variance)),
        VarianceLoc(varianceLoc), ColonLoc(colonLoc) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ObjCTypeParamDecl *Create(ASTContext &ctx, DeclContext *dc,
                                   ObjCTypeParamVariance variance,
                                   SourceLocation varianceLoc,
                                   unsigned index,
                                   SourceLocation nameLoc,
                                   IdentifierInfo *name,
                                   SourceLocation colonLoc,
                                   TypeSourceInfo *boundInfo);
  static ObjCTypeParamDecl *CreateDeserialized(ASTContext &ctx, unsigned ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Determine the variance of this type parameter.
  ObjCTypeParamVariance getVariance() const {
    return static_cast<ObjCTypeParamVariance>(Variance);
  }

  /// Set the variance of this type parameter.
  void setVariance(ObjCTypeParamVariance variance) {
    Variance = static_cast<unsigned>(variance);
  }

  /// Retrieve the location of the variance keyword.
  SourceLocation getVarianceLoc() const { return VarianceLoc; }

  /// Retrieve the index into its type parameter list.
  unsigned getIndex() const { return Index; }

  /// Whether this type parameter has an explicitly-written type bound, e.g.,
  /// "T : NSView".
  bool hasExplicitBound() const { return ColonLoc.isValid(); }

  /// Retrieve the location of the ':' separating the type parameter name
  /// from the explicitly-specified bound.
  SourceLocation getColonLoc() const { return ColonLoc; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCTypeParam; }
};

/// Stores a list of Objective-C type parameters for a parameterized class
/// or a category/extension thereof.
///
/// \code
/// @interface NSArray<T> // stores the <T>
/// @end
/// \endcode
class ObjCTypeParamList final
    : private llvm::TrailingObjects<ObjCTypeParamList, ObjCTypeParamDecl *> {
  /// Stores the components of a SourceRange as a POD.
  struct PODSourceRange {
    unsigned Begin;
    unsigned End;
  };

  union {
    /// Location of the left and right angle brackets.
    PODSourceRange Brackets;

    // Used only for alignment.
    ObjCTypeParamDecl *AlignmentHack;
  };

  /// The number of parameters in the list, which are tail-allocated.
  unsigned NumParams;

  ObjCTypeParamList(SourceLocation lAngleLoc,
                    ArrayRef<ObjCTypeParamDecl *> typeParams,
                    SourceLocation rAngleLoc);

public:
  friend TrailingObjects;

  /// Create a new Objective-C type parameter list.
  static ObjCTypeParamList *create(ASTContext &ctx,
                                   SourceLocation lAngleLoc,
                                   ArrayRef<ObjCTypeParamDecl *> typeParams,
                                   SourceLocation rAngleLoc);

  /// Iterate through the type parameters in the list.
  using iterator = ObjCTypeParamDecl **;

  iterator begin() { return getTrailingObjects<ObjCTypeParamDecl *>(); }

  iterator end() { return begin() + size(); }

  /// Determine the number of type parameters in this list.
  unsigned size() const { return NumParams; }

  // Iterate through the type parameters in the list.
  using const_iterator = ObjCTypeParamDecl * const *;

  const_iterator begin() const {
    return getTrailingObjects<ObjCTypeParamDecl *>();
  }

  const_iterator end() const {
    return begin() + size();
  }

  ObjCTypeParamDecl *front() const {
    assert(size() > 0 && "empty Objective-C type parameter list");
    return *begin();
  }

  ObjCTypeParamDecl *back() const {
    assert(size() > 0 && "empty Objective-C type parameter list");
    return *(end() - 1);
  }

  SourceLocation getLAngleLoc() const {
    return SourceLocation::getFromRawEncoding(Brackets.Begin);
  }

  SourceLocation getRAngleLoc() const {
    return SourceLocation::getFromRawEncoding(Brackets.End);
  }

  SourceRange getSourceRange() const {
    return SourceRange(getLAngleLoc(), getRAngleLoc());
  }

  /// Gather the default set of type arguments to be substituted for
  /// these type parameters when dealing with an unspecialized type.
  void gatherDefaultTypeArgs(SmallVectorImpl<QualType> &typeArgs) const;
};

enum class ObjCPropertyQueryKind : uint8_t {
  OBJC_PR_query_unknown = 0x00,
  OBJC_PR_query_instance,
  OBJC_PR_query_class
};

/// Represents one property declaration in an Objective-C interface.
///
/// For example:
/// \code{.mm}
/// \@property (assign, readwrite) int MyProperty;
/// \endcode
class ObjCPropertyDecl : public NamedDecl {
  void anchor() override;

public:
  enum PropertyAttributeKind {
    OBJC_PR_noattr    = 0x00,
    OBJC_PR_readonly  = 0x01,
    OBJC_PR_getter    = 0x02,
    OBJC_PR_assign    = 0x04,
    OBJC_PR_readwrite = 0x08,
    OBJC_PR_retain    = 0x10,
    OBJC_PR_copy      = 0x20,
    OBJC_PR_nonatomic = 0x40,
    OBJC_PR_setter    = 0x80,
    OBJC_PR_atomic    = 0x100,
    OBJC_PR_weak      = 0x200,
    OBJC_PR_strong    = 0x400,
    OBJC_PR_unsafe_unretained = 0x800,
    /// Indicates that the nullability of the type was spelled with a
    /// property attribute rather than a type qualifier.
    OBJC_PR_nullability = 0x1000,
    OBJC_PR_null_resettable = 0x2000,
    OBJC_PR_class = 0x4000
    // Adding a property should change NumPropertyAttrsBits
  };

  enum {
    /// Number of bits fitting all the property attributes.
    NumPropertyAttrsBits = 15
  };

  enum SetterKind { Assign, Retain, Copy, Weak };
  enum PropertyControl { None, Required, Optional };

private:
  // location of \@property
  SourceLocation AtLoc;

  // location of '(' starting attribute list or null.
  SourceLocation LParenLoc;

  QualType DeclType;
  TypeSourceInfo *DeclTypeSourceInfo;
  unsigned PropertyAttributes : NumPropertyAttrsBits;
  unsigned PropertyAttributesAsWritten : NumPropertyAttrsBits;

  // \@required/\@optional
  unsigned PropertyImplementation : 2;

  // getter name of NULL if no getter
  Selector GetterName;

  // setter name of NULL if no setter
  Selector SetterName;

  // location of the getter attribute's value
  SourceLocation GetterNameLoc;

  // location of the setter attribute's value
  SourceLocation SetterNameLoc;

  // Declaration of getter instance method
  ObjCMethodDecl *GetterMethodDecl = nullptr;

  // Declaration of setter instance method
  ObjCMethodDecl *SetterMethodDecl = nullptr;

  // Synthesize ivar for this property
  ObjCIvarDecl *PropertyIvarDecl = nullptr;

  ObjCPropertyDecl(DeclContext *DC, SourceLocation L, IdentifierInfo *Id,
                   SourceLocation AtLocation,  SourceLocation LParenLocation,
                   QualType T, TypeSourceInfo *TSI,
                   PropertyControl propControl)
    : NamedDecl(ObjCProperty, DC, L, Id), AtLoc(AtLocation),
      LParenLoc(LParenLocation), DeclType(T), DeclTypeSourceInfo(TSI),
      PropertyAttributes(OBJC_PR_noattr),
      PropertyAttributesAsWritten(OBJC_PR_noattr),
      PropertyImplementation(propControl), GetterName(Selector()),
      SetterName(Selector()) {}

public:
  static ObjCPropertyDecl *Create(ASTContext &C, DeclContext *DC,
                                  SourceLocation L,
                                  IdentifierInfo *Id, SourceLocation AtLocation,
                                  SourceLocation LParenLocation,
                                  QualType T,
                                  TypeSourceInfo *TSI,
                                  PropertyControl propControl = None);

  static ObjCPropertyDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  SourceLocation getAtLoc() const { return AtLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }

  TypeSourceInfo *getTypeSourceInfo() const { return DeclTypeSourceInfo; }

  QualType getType() const { return DeclType; }

  void setType(QualType T, TypeSourceInfo *TSI) {
    DeclType = T;
    DeclTypeSourceInfo = TSI;
  }

  /// Retrieve the type when this property is used with a specific base object
  /// type.
  QualType getUsageType(QualType objectType) const;

  PropertyAttributeKind getPropertyAttributes() const {
    return PropertyAttributeKind(PropertyAttributes);
  }

  void setPropertyAttributes(PropertyAttributeKind PRVal) {
    PropertyAttributes |= PRVal;
  }

  void overwritePropertyAttributes(unsigned PRVal) {
    PropertyAttributes = PRVal;
  }

  PropertyAttributeKind getPropertyAttributesAsWritten() const {
    return PropertyAttributeKind(PropertyAttributesAsWritten);
  }

  void setPropertyAttributesAsWritten(PropertyAttributeKind PRVal) {
    PropertyAttributesAsWritten = PRVal;
  }

  // Helper methods for accessing attributes.

  /// isReadOnly - Return true iff the property has a setter.
  bool isReadOnly() const {
    return (PropertyAttributes & OBJC_PR_readonly);
  }

  /// isAtomic - Return true if the property is atomic.
  bool isAtomic() const {
    return (PropertyAttributes & OBJC_PR_atomic);
  }

  /// isRetaining - Return true if the property retains its value.
  bool isRetaining() const {
    return (PropertyAttributes &
            (OBJC_PR_retain | OBJC_PR_strong | OBJC_PR_copy));
  }

  bool isInstanceProperty() const { return !isClassProperty(); }
  bool isClassProperty() const { return PropertyAttributes & OBJC_PR_class; }

  ObjCPropertyQueryKind getQueryKind() const {
    return isClassProperty() ? ObjCPropertyQueryKind::OBJC_PR_query_class :
                               ObjCPropertyQueryKind::OBJC_PR_query_instance;
  }

  static ObjCPropertyQueryKind getQueryKind(bool isClassProperty) {
    return isClassProperty ? ObjCPropertyQueryKind::OBJC_PR_query_class :
                             ObjCPropertyQueryKind::OBJC_PR_query_instance;
  }

  /// getSetterKind - Return the method used for doing assignment in
  /// the property setter. This is only valid if the property has been
  /// defined to have a setter.
  SetterKind getSetterKind() const {
    if (PropertyAttributes & OBJC_PR_strong)
      return getType()->isBlockPointerType() ? Copy : Retain;
    if (PropertyAttributes & OBJC_PR_retain)
      return Retain;
    if (PropertyAttributes & OBJC_PR_copy)
      return Copy;
    if (PropertyAttributes & OBJC_PR_weak)
      return Weak;
    return Assign;
  }

  Selector getGetterName() const { return GetterName; }
  SourceLocation getGetterNameLoc() const { return GetterNameLoc; }

  void setGetterName(Selector Sel, SourceLocation Loc = SourceLocation()) {
    GetterName = Sel;
    GetterNameLoc = Loc;
  }

  Selector getSetterName() const { return SetterName; }
  SourceLocation getSetterNameLoc() const { return SetterNameLoc; }

  void setSetterName(Selector Sel, SourceLocation Loc = SourceLocation()) {
    SetterName = Sel;
    SetterNameLoc = Loc;
  }

  ObjCMethodDecl *getGetterMethodDecl() const { return GetterMethodDecl; }
  void setGetterMethodDecl(ObjCMethodDecl *gDecl) { GetterMethodDecl = gDecl; }

  ObjCMethodDecl *getSetterMethodDecl() const { return SetterMethodDecl; }
  void setSetterMethodDecl(ObjCMethodDecl *gDecl) { SetterMethodDecl = gDecl; }

  // Related to \@optional/\@required declared in \@protocol
  void setPropertyImplementation(PropertyControl pc) {
    PropertyImplementation = pc;
  }

  PropertyControl getPropertyImplementation() const {
    return PropertyControl(PropertyImplementation);
  }

  bool isOptional() const {
    return getPropertyImplementation() == PropertyControl::Optional;
  }

  void setPropertyIvarDecl(ObjCIvarDecl *Ivar) {
    PropertyIvarDecl = Ivar;
  }

  ObjCIvarDecl *getPropertyIvarDecl() const {
    return PropertyIvarDecl;
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(AtLoc, getLocation());
  }

  /// Get the default name of the synthesized ivar.
  IdentifierInfo *getDefaultSynthIvarName(ASTContext &Ctx) const;

  /// Lookup a property by name in the specified DeclContext.
  static ObjCPropertyDecl *findPropertyDecl(const DeclContext *DC,
                                            const IdentifierInfo *propertyID,
                                            ObjCPropertyQueryKind queryKind);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCProperty; }
};

/// ObjCContainerDecl - Represents a container for method declarations.
/// Current sub-classes are ObjCInterfaceDecl, ObjCCategoryDecl,
/// ObjCProtocolDecl, and ObjCImplDecl.
///
class ObjCContainerDecl : public NamedDecl, public DeclContext {
  // This class stores some data in DeclContext::ObjCContainerDeclBits
  // to save some space. Use the provided accessors to access it.

  // These two locations in the range mark the end of the method container.
  // The first points to the '@' token, and the second to the 'end' token.
  SourceRange AtEnd;

  void anchor() override;

public:
  ObjCContainerDecl(Kind DK, DeclContext *DC, IdentifierInfo *Id,
                    SourceLocation nameLoc, SourceLocation atStartLoc);

  // Iterator access to instance/class properties.
  using prop_iterator = specific_decl_iterator<ObjCPropertyDecl>;
  using prop_range =
      llvm::iterator_range<specific_decl_iterator<ObjCPropertyDecl>>;

  prop_range properties() const { return prop_range(prop_begin(), prop_end()); }

  prop_iterator prop_begin() const {
    return prop_iterator(decls_begin());
  }

  prop_iterator prop_end() const {
    return prop_iterator(decls_end());
  }

  using instprop_iterator =
      filtered_decl_iterator<ObjCPropertyDecl,
                             &ObjCPropertyDecl::isInstanceProperty>;
  using instprop_range = llvm::iterator_range<instprop_iterator>;

  instprop_range instance_properties() const {
    return instprop_range(instprop_begin(), instprop_end());
  }

  instprop_iterator instprop_begin() const {
    return instprop_iterator(decls_begin());
  }

  instprop_iterator instprop_end() const {
    return instprop_iterator(decls_end());
  }

  using classprop_iterator =
      filtered_decl_iterator<ObjCPropertyDecl,
                             &ObjCPropertyDecl::isClassProperty>;
  using classprop_range = llvm::iterator_range<classprop_iterator>;

  classprop_range class_properties() const {
    return classprop_range(classprop_begin(), classprop_end());
  }

  classprop_iterator classprop_begin() const {
    return classprop_iterator(decls_begin());
  }

  classprop_iterator classprop_end() const {
    return classprop_iterator(decls_end());
  }

  // Iterator access to instance/class methods.
  using method_iterator = specific_decl_iterator<ObjCMethodDecl>;
  using method_range =
      llvm::iterator_range<specific_decl_iterator<ObjCMethodDecl>>;

  method_range methods() const {
    return method_range(meth_begin(), meth_end());
  }

  method_iterator meth_begin() const {
    return method_iterator(decls_begin());
  }

  method_iterator meth_end() const {
    return method_iterator(decls_end());
  }

  using instmeth_iterator =
      filtered_decl_iterator<ObjCMethodDecl,
                             &ObjCMethodDecl::isInstanceMethod>;
  using instmeth_range = llvm::iterator_range<instmeth_iterator>;

  instmeth_range instance_methods() const {
    return instmeth_range(instmeth_begin(), instmeth_end());
  }

  instmeth_iterator instmeth_begin() const {
    return instmeth_iterator(decls_begin());
  }

  instmeth_iterator instmeth_end() const {
    return instmeth_iterator(decls_end());
  }

  using classmeth_iterator =
      filtered_decl_iterator<ObjCMethodDecl,
                             &ObjCMethodDecl::isClassMethod>;
  using classmeth_range = llvm::iterator_range<classmeth_iterator>;

  classmeth_range class_methods() const {
    return classmeth_range(classmeth_begin(), classmeth_end());
  }

  classmeth_iterator classmeth_begin() const {
    return classmeth_iterator(decls_begin());
  }

  classmeth_iterator classmeth_end() const {
    return classmeth_iterator(decls_end());
  }

  // Get the local instance/class method declared in this interface.
  ObjCMethodDecl *getMethod(Selector Sel, bool isInstance,
                            bool AllowHidden = false) const;

  ObjCMethodDecl *getInstanceMethod(Selector Sel,
                                    bool AllowHidden = false) const {
    return getMethod(Sel, true/*isInstance*/, AllowHidden);
  }

  ObjCMethodDecl *getClassMethod(Selector Sel, bool AllowHidden = false) const {
    return getMethod(Sel, false/*isInstance*/, AllowHidden);
  }

  bool HasUserDeclaredSetterMethod(const ObjCPropertyDecl *P) const;
  ObjCIvarDecl *getIvarDecl(IdentifierInfo *Id) const;

  ObjCPropertyDecl *
  FindPropertyDeclaration(const IdentifierInfo *PropertyId,
                          ObjCPropertyQueryKind QueryKind) const;

  using PropertyMap =
      llvm::DenseMap<std::pair<IdentifierInfo *, unsigned/*isClassProperty*/>,
                     ObjCPropertyDecl *>;
  using ProtocolPropertySet = llvm::SmallDenseSet<const ObjCProtocolDecl *, 8>;
  using PropertyDeclOrder = llvm::SmallVector<ObjCPropertyDecl *, 8>;

  /// This routine collects list of properties to be implemented in the class.
  /// This includes, class's and its conforming protocols' properties.
  /// Note, the superclass's properties are not included in the list.
  virtual void collectPropertiesToImplement(PropertyMap &PM,
                                            PropertyDeclOrder &PO) const {}

  SourceLocation getAtStartLoc() const { return ObjCContainerDeclBits.AtStart; }

  void setAtStartLoc(SourceLocation Loc) {
    ObjCContainerDeclBits.AtStart = Loc;
  }

  // Marks the end of the container.
  SourceRange getAtEndRange() const { return AtEnd; }

  void setAtEndRange(SourceRange atEnd) { AtEnd = atEnd; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getAtStartLoc(), getAtEndRange().getEnd());
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstObjCContainer &&
           K <= lastObjCContainer;
  }

  static DeclContext *castToDeclContext(const ObjCContainerDecl *D) {
    return static_cast<DeclContext *>(const_cast<ObjCContainerDecl*>(D));
  }

  static ObjCContainerDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<ObjCContainerDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents an ObjC class declaration.
///
/// For example:
///
/// \code
///   // MostPrimitive declares no super class (not particularly useful).
///   \@interface MostPrimitive
///     // no instance variables or methods.
///   \@end
///
///   // NSResponder inherits from NSObject & implements NSCoding (a protocol).
///   \@interface NSResponder : NSObject \<NSCoding>
///   { // instance variables are represented by ObjCIvarDecl.
///     id nextResponder; // nextResponder instance variable.
///   }
///   - (NSResponder *)nextResponder; // return a pointer to NSResponder.
///   - (void)mouseMoved:(NSEvent *)theEvent; // return void, takes a pointer
///   \@end                                    // to an NSEvent.
/// \endcode
///
///   Unlike C/C++, forward class declarations are accomplished with \@class.
///   Unlike C/C++, \@class allows for a list of classes to be forward declared.
///   Unlike C++, ObjC is a single-rooted class model. In Cocoa, classes
///   typically inherit from NSObject (an exception is NSProxy).
///
class ObjCInterfaceDecl : public ObjCContainerDecl
                        , public Redeclarable<ObjCInterfaceDecl> {
  friend class ASTContext;

  /// TypeForDecl - This indicates the Type object that represents this
  /// TypeDecl.  It is a cache maintained by ASTContext::getObjCInterfaceType
  mutable const Type *TypeForDecl = nullptr;

  struct DefinitionData {
    /// The definition of this class, for quick access from any
    /// declaration.
    ObjCInterfaceDecl *Definition = nullptr;

    /// When non-null, this is always an ObjCObjectType.
    TypeSourceInfo *SuperClassTInfo = nullptr;

    /// Protocols referenced in the \@interface  declaration
    ObjCProtocolList ReferencedProtocols;

    /// Protocols reference in both the \@interface and class extensions.
    ObjCList<ObjCProtocolDecl> AllReferencedProtocols;

    /// List of categories and class extensions defined for this class.
    ///
    /// Categories are stored as a linked list in the AST, since the categories
    /// and class extensions come long after the initial interface declaration,
    /// and we avoid dynamically-resized arrays in the AST wherever possible.
    ObjCCategoryDecl *CategoryList = nullptr;

    /// IvarList - List of all ivars defined by this class; including class
    /// extensions and implementation. This list is built lazily.
    ObjCIvarDecl *IvarList = nullptr;

    /// Indicates that the contents of this Objective-C class will be
    /// completed by the external AST source when required.
    mutable unsigned ExternallyCompleted : 1;

    /// Indicates that the ivar cache does not yet include ivars
    /// declared in the implementation.
    mutable unsigned IvarListMissingImplementation : 1;

    /// Indicates that this interface decl contains at least one initializer
    /// marked with the 'objc_designated_initializer' attribute.
    unsigned HasDesignatedInitializers : 1;

    enum InheritedDesignatedInitializersState {
      /// We didn't calculate whether the designated initializers should be
      /// inherited or not.
      IDI_Unknown = 0,

      /// Designated initializers are inherited for the super class.
      IDI_Inherited = 1,

      /// The class does not inherit designated initializers.
      IDI_NotInherited = 2
    };

    /// One of the \c InheritedDesignatedInitializersState enumeratos.
    mutable unsigned InheritedDesignatedInitializers : 2;

    /// The location of the last location in this declaration, before
    /// the properties/methods. For example, this will be the '>', '}', or
    /// identifier,
    SourceLocation EndLoc;

    DefinitionData()
        : ExternallyCompleted(false), IvarListMissingImplementation(true),
          HasDesignatedInitializers(false),
          InheritedDesignatedInitializers(IDI_Unknown) {}
  };

  /// The type parameters associated with this class, if any.
  ObjCTypeParamList *TypeParamList = nullptr;

  /// Contains a pointer to the data associated with this class,
  /// which will be NULL if this class has not yet been defined.
  ///
  /// The bit indicates when we don't need to check for out-of-date
  /// declarations. It will be set unless modules are enabled.
  llvm::PointerIntPair<DefinitionData *, 1, bool> Data;

  ObjCInterfaceDecl(const ASTContext &C, DeclContext *DC, SourceLocation AtLoc,
                    IdentifierInfo *Id, ObjCTypeParamList *typeParamList,
                    SourceLocation CLoc, ObjCInterfaceDecl *PrevDecl,
                    bool IsInternal);

  void anchor() override;

  void LoadExternalDefinition() const;

  DefinitionData &data() const {
    assert(Data.getPointer() && "Declaration has no definition!");
    return *Data.getPointer();
  }

  /// Allocate the definition data for this class.
  void allocateDefinitionData();

  using redeclarable_base = Redeclarable<ObjCInterfaceDecl>;

  ObjCInterfaceDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  ObjCInterfaceDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  ObjCInterfaceDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  static ObjCInterfaceDecl *Create(const ASTContext &C, DeclContext *DC,
                                   SourceLocation atLoc,
                                   IdentifierInfo *Id,
                                   ObjCTypeParamList *typeParamList,
                                   ObjCInterfaceDecl *PrevDecl,
                                   SourceLocation ClassLoc = SourceLocation(),
                                   bool isInternal = false);

  static ObjCInterfaceDecl *CreateDeserialized(const ASTContext &C, unsigned ID);

  /// Retrieve the type parameters of this class.
  ///
  /// This function looks for a type parameter list for the given
  /// class; if the class has been declared (with \c \@class) but not
  /// defined (with \c \@interface), it will search for a declaration that
  /// has type parameters, skipping any declarations that do not.
  ObjCTypeParamList *getTypeParamList() const;

  /// Set the type parameters of this class.
  ///
  /// This function is used by the AST importer, which must import the type
  /// parameters after creating their DeclContext to avoid loops.
  void setTypeParamList(ObjCTypeParamList *TPL);

  /// Retrieve the type parameters written on this particular declaration of
  /// the class.
  ObjCTypeParamList *getTypeParamListAsWritten() const {
    return TypeParamList;
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    if (isThisDeclarationADefinition())
      return ObjCContainerDecl::getSourceRange();

    return SourceRange(getAtStartLoc(), getLocation());
  }

  /// Indicate that this Objective-C class is complete, but that
  /// the external AST source will be responsible for filling in its contents
  /// when a complete class is required.
  void setExternallyCompleted();

  /// Indicate that this interface decl contains at least one initializer
  /// marked with the 'objc_designated_initializer' attribute.
  void setHasDesignatedInitializers();

  /// Returns true if this interface decl contains at least one initializer
  /// marked with the 'objc_designated_initializer' attribute.
  bool hasDesignatedInitializers() const;

  /// Returns true if this interface decl declares a designated initializer
  /// or it inherites one from its super class.
  bool declaresOrInheritsDesignatedInitializers() const {
    return hasDesignatedInitializers() || inheritsDesignatedInitializers();
  }

  const ObjCProtocolList &getReferencedProtocols() const {
    assert(hasDefinition() && "Caller did not check for forward reference!");
    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().ReferencedProtocols;
  }

  ObjCImplementationDecl *getImplementation() const;
  void setImplementation(ObjCImplementationDecl *ImplD);

  ObjCCategoryDecl *FindCategoryDeclaration(IdentifierInfo *CategoryId) const;

  // Get the local instance/class method declared in a category.
  ObjCMethodDecl *getCategoryInstanceMethod(Selector Sel) const;
  ObjCMethodDecl *getCategoryClassMethod(Selector Sel) const;

  ObjCMethodDecl *getCategoryMethod(Selector Sel, bool isInstance) const {
    return isInstance ? getCategoryInstanceMethod(Sel)
                      : getCategoryClassMethod(Sel);
  }

  using protocol_iterator = ObjCProtocolList::iterator;
  using protocol_range = llvm::iterator_range<protocol_iterator>;

  protocol_range protocols() const {
    return protocol_range(protocol_begin(), protocol_end());
  }

  protocol_iterator protocol_begin() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return protocol_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().ReferencedProtocols.begin();
  }

  protocol_iterator protocol_end() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return protocol_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().ReferencedProtocols.end();
  }

  using protocol_loc_iterator = ObjCProtocolList::loc_iterator;
  using protocol_loc_range = llvm::iterator_range<protocol_loc_iterator>;

  protocol_loc_range protocol_locs() const {
    return protocol_loc_range(protocol_loc_begin(), protocol_loc_end());
  }

  protocol_loc_iterator protocol_loc_begin() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return protocol_loc_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().ReferencedProtocols.loc_begin();
  }

  protocol_loc_iterator protocol_loc_end() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return protocol_loc_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().ReferencedProtocols.loc_end();
  }

  using all_protocol_iterator = ObjCList<ObjCProtocolDecl>::iterator;
  using all_protocol_range = llvm::iterator_range<all_protocol_iterator>;

  all_protocol_range all_referenced_protocols() const {
    return all_protocol_range(all_referenced_protocol_begin(),
                              all_referenced_protocol_end());
  }

  all_protocol_iterator all_referenced_protocol_begin() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return all_protocol_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().AllReferencedProtocols.empty()
             ? protocol_begin()
             : data().AllReferencedProtocols.begin();
  }

  all_protocol_iterator all_referenced_protocol_end() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return all_protocol_iterator();

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().AllReferencedProtocols.empty()
             ? protocol_end()
             : data().AllReferencedProtocols.end();
  }

  using ivar_iterator = specific_decl_iterator<ObjCIvarDecl>;
  using ivar_range = llvm::iterator_range<specific_decl_iterator<ObjCIvarDecl>>;

  ivar_range ivars() const { return ivar_range(ivar_begin(), ivar_end()); }

  ivar_iterator ivar_begin() const {
    if (const ObjCInterfaceDecl *Def = getDefinition())
      return ivar_iterator(Def->decls_begin());

    // FIXME: Should make sure no callers ever do this.
    return ivar_iterator();
  }

  ivar_iterator ivar_end() const {
    if (const ObjCInterfaceDecl *Def = getDefinition())
      return ivar_iterator(Def->decls_end());

    // FIXME: Should make sure no callers ever do this.
    return ivar_iterator();
  }

  unsigned ivar_size() const {
    return std::distance(ivar_begin(), ivar_end());
  }

  bool ivar_empty() const { return ivar_begin() == ivar_end(); }

  ObjCIvarDecl *all_declared_ivar_begin();
  const ObjCIvarDecl *all_declared_ivar_begin() const {
    // Even though this modifies IvarList, it's conceptually const:
    // the ivar chain is essentially a cached property of ObjCInterfaceDecl.
    return const_cast<ObjCInterfaceDecl *>(this)->all_declared_ivar_begin();
  }
  void setIvarList(ObjCIvarDecl *ivar) { data().IvarList = ivar; }

  /// setProtocolList - Set the list of protocols that this interface
  /// implements.
  void setProtocolList(ObjCProtocolDecl *const* List, unsigned Num,
                       const SourceLocation *Locs, ASTContext &C) {
    data().ReferencedProtocols.set(List, Num, Locs, C);
  }

  /// mergeClassExtensionProtocolList - Merge class extension's protocol list
  /// into the protocol list for this class.
  void mergeClassExtensionProtocolList(ObjCProtocolDecl *const* List,
                                       unsigned Num,
                                       ASTContext &C);

  /// Produce a name to be used for class's metadata. It comes either via
  /// objc_runtime_name attribute or class name.
  StringRef getObjCRuntimeNameAsString() const;

  /// Returns the designated initializers for the interface.
  ///
  /// If this declaration does not have methods marked as designated
  /// initializers then the interface inherits the designated initializers of
  /// its super class.
  void getDesignatedInitializers(
                  llvm::SmallVectorImpl<const ObjCMethodDecl *> &Methods) const;

  /// Returns true if the given selector is a designated initializer for the
  /// interface.
  ///
  /// If this declaration does not have methods marked as designated
  /// initializers then the interface inherits the designated initializers of
  /// its super class.
  ///
  /// \param InitMethod if non-null and the function returns true, it receives
  /// the method that was marked as a designated initializer.
  bool
  isDesignatedInitializer(Selector Sel,
                          const ObjCMethodDecl **InitMethod = nullptr) const;

  /// Determine whether this particular declaration of this class is
  /// actually also a definition.
  bool isThisDeclarationADefinition() const {
    return getDefinition() == this;
  }

  /// Determine whether this class has been defined.
  bool hasDefinition() const {
    // If the name of this class is out-of-date, bring it up-to-date, which
    // might bring in a definition.
    // Note: a null value indicates that we don't have a definition and that
    // modules are enabled.
    if (!Data.getOpaqueValue())
      getMostRecentDecl();

    return Data.getPointer();
  }

  /// Retrieve the definition of this class, or NULL if this class
  /// has been forward-declared (with \@class) but not yet defined (with
  /// \@interface).
  ObjCInterfaceDecl *getDefinition() {
    return hasDefinition()? Data.getPointer()->Definition : nullptr;
  }

  /// Retrieve the definition of this class, or NULL if this class
  /// has been forward-declared (with \@class) but not yet defined (with
  /// \@interface).
  const ObjCInterfaceDecl *getDefinition() const {
    return hasDefinition()? Data.getPointer()->Definition : nullptr;
  }

  /// Starts the definition of this Objective-C class, taking it from
  /// a forward declaration (\@class) to a definition (\@interface).
  void startDefinition();

  /// Retrieve the superclass type.
  const ObjCObjectType *getSuperClassType() const {
    if (TypeSourceInfo *TInfo = getSuperClassTInfo())
      return TInfo->getType()->castAs<ObjCObjectType>();

    return nullptr;
  }

  // Retrieve the type source information for the superclass.
  TypeSourceInfo *getSuperClassTInfo() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return nullptr;

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().SuperClassTInfo;
  }

  // Retrieve the declaration for the superclass of this class, which
  // does not include any type arguments that apply to the superclass.
  ObjCInterfaceDecl *getSuperClass() const;

  void setSuperClass(TypeSourceInfo *superClass) {
    data().SuperClassTInfo = superClass;
  }

  /// Iterator that walks over the list of categories, filtering out
  /// those that do not meet specific criteria.
  ///
  /// This class template is used for the various permutations of category
  /// and extension iterators.
  template<bool (*Filter)(ObjCCategoryDecl *)>
  class filtered_category_iterator {
    ObjCCategoryDecl *Current = nullptr;

    void findAcceptableCategory();

  public:
    using value_type = ObjCCategoryDecl *;
    using reference = value_type;
    using pointer = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    filtered_category_iterator() = default;
    explicit filtered_category_iterator(ObjCCategoryDecl *Current)
        : Current(Current) {
      findAcceptableCategory();
    }

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    filtered_category_iterator &operator++();

    filtered_category_iterator operator++(int) {
      filtered_category_iterator Tmp = *this;
      ++(*this);
      return Tmp;
    }

    friend bool operator==(filtered_category_iterator X,
                           filtered_category_iterator Y) {
      return X.Current == Y.Current;
    }

    friend bool operator!=(filtered_category_iterator X,
                           filtered_category_iterator Y) {
      return X.Current != Y.Current;
    }
  };

private:
  /// Test whether the given category is visible.
  ///
  /// Used in the \c visible_categories_iterator.
  static bool isVisibleCategory(ObjCCategoryDecl *Cat);

public:
  /// Iterator that walks over the list of categories and extensions
  /// that are visible, i.e., not hidden in a non-imported submodule.
  using visible_categories_iterator =
      filtered_category_iterator<isVisibleCategory>;

  using visible_categories_range =
      llvm::iterator_range<visible_categories_iterator>;

  visible_categories_range visible_categories() const {
    return visible_categories_range(visible_categories_begin(),
                                    visible_categories_end());
  }

  /// Retrieve an iterator to the beginning of the visible-categories
  /// list.
  visible_categories_iterator visible_categories_begin() const {
    return visible_categories_iterator(getCategoryListRaw());
  }

  /// Retrieve an iterator to the end of the visible-categories list.
  visible_categories_iterator visible_categories_end() const {
    return visible_categories_iterator();
  }

  /// Determine whether the visible-categories list is empty.
  bool visible_categories_empty() const {
    return visible_categories_begin() == visible_categories_end();
  }

private:
  /// Test whether the given category... is a category.
  ///
  /// Used in the \c known_categories_iterator.
  static bool isKnownCategory(ObjCCategoryDecl *) { return true; }

public:
  /// Iterator that walks over all of the known categories and
  /// extensions, including those that are hidden.
  using known_categories_iterator = filtered_category_iterator<isKnownCategory>;
  using known_categories_range =
     llvm::iterator_range<known_categories_iterator>;

  known_categories_range known_categories() const {
    return known_categories_range(known_categories_begin(),
                                  known_categories_end());
  }

  /// Retrieve an iterator to the beginning of the known-categories
  /// list.
  known_categories_iterator known_categories_begin() const {
    return known_categories_iterator(getCategoryListRaw());
  }

  /// Retrieve an iterator to the end of the known-categories list.
  known_categories_iterator known_categories_end() const {
    return known_categories_iterator();
  }

  /// Determine whether the known-categories list is empty.
  bool known_categories_empty() const {
    return known_categories_begin() == known_categories_end();
  }

private:
  /// Test whether the given category is a visible extension.
  ///
  /// Used in the \c visible_extensions_iterator.
  static bool isVisibleExtension(ObjCCategoryDecl *Cat);

public:
  /// Iterator that walks over all of the visible extensions, skipping
  /// any that are known but hidden.
  using visible_extensions_iterator =
      filtered_category_iterator<isVisibleExtension>;

  using visible_extensions_range =
      llvm::iterator_range<visible_extensions_iterator>;

  visible_extensions_range visible_extensions() const {
    return visible_extensions_range(visible_extensions_begin(),
                                    visible_extensions_end());
  }

  /// Retrieve an iterator to the beginning of the visible-extensions
  /// list.
  visible_extensions_iterator visible_extensions_begin() const {
    return visible_extensions_iterator(getCategoryListRaw());
  }

  /// Retrieve an iterator to the end of the visible-extensions list.
  visible_extensions_iterator visible_extensions_end() const {
    return visible_extensions_iterator();
  }

  /// Determine whether the visible-extensions list is empty.
  bool visible_extensions_empty() const {
    return visible_extensions_begin() == visible_extensions_end();
  }

private:
  /// Test whether the given category is an extension.
  ///
  /// Used in the \c known_extensions_iterator.
  static bool isKnownExtension(ObjCCategoryDecl *Cat);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class ASTReader;

  /// Iterator that walks over all of the known extensions.
  using known_extensions_iterator =
      filtered_category_iterator<isKnownExtension>;
  using known_extensions_range =
      llvm::iterator_range<known_extensions_iterator>;

  known_extensions_range known_extensions() const {
    return known_extensions_range(known_extensions_begin(),
                                  known_extensions_end());
  }

  /// Retrieve an iterator to the beginning of the known-extensions
  /// list.
  known_extensions_iterator known_extensions_begin() const {
    return known_extensions_iterator(getCategoryListRaw());
  }

  /// Retrieve an iterator to the end of the known-extensions list.
  known_extensions_iterator known_extensions_end() const {
    return known_extensions_iterator();
  }

  /// Determine whether the known-extensions list is empty.
  bool known_extensions_empty() const {
    return known_extensions_begin() == known_extensions_end();
  }

  /// Retrieve the raw pointer to the start of the category/extension
  /// list.
  ObjCCategoryDecl* getCategoryListRaw() const {
    // FIXME: Should make sure no callers ever do this.
    if (!hasDefinition())
      return nullptr;

    if (data().ExternallyCompleted)
      LoadExternalDefinition();

    return data().CategoryList;
  }

  /// Set the raw pointer to the start of the category/extension
  /// list.
  void setCategoryListRaw(ObjCCategoryDecl *category) {
    data().CategoryList = category;
  }

  ObjCPropertyDecl
    *FindPropertyVisibleInPrimaryClass(IdentifierInfo *PropertyId,
                                       ObjCPropertyQueryKind QueryKind) const;

  void collectPropertiesToImplement(PropertyMap &PM,
                                    PropertyDeclOrder &PO) const override;

  /// isSuperClassOf - Return true if this class is the specified class or is a
  /// super class of the specified interface class.
  bool isSuperClassOf(const ObjCInterfaceDecl *I) const {
    // If RHS is derived from LHS it is OK; else it is not OK.
    while (I != nullptr) {
      if (declaresSameEntity(this, I))
        return true;

      I = I->getSuperClass();
    }
    return false;
  }

  /// isArcWeakrefUnavailable - Checks for a class or one of its super classes
  /// to be incompatible with __weak references. Returns true if it is.
  bool isArcWeakrefUnavailable() const;

  /// isObjCRequiresPropertyDefs - Checks that a class or one of its super
  /// classes must not be auto-synthesized. Returns class decl. if it must not
  /// be; 0, otherwise.
  const ObjCInterfaceDecl *isObjCRequiresPropertyDefs() const;

  ObjCIvarDecl *lookupInstanceVariable(IdentifierInfo *IVarName,
                                       ObjCInterfaceDecl *&ClassDeclared);
  ObjCIvarDecl *lookupInstanceVariable(IdentifierInfo *IVarName) {
    ObjCInterfaceDecl *ClassDeclared;
    return lookupInstanceVariable(IVarName, ClassDeclared);
  }

  ObjCProtocolDecl *lookupNestedProtocol(IdentifierInfo *Name);

  // Lookup a method. First, we search locally. If a method isn't
  // found, we search referenced protocols and class categories.
  ObjCMethodDecl *lookupMethod(Selector Sel, bool isInstance,
                               bool shallowCategoryLookup = false,
                               bool followSuper = true,
                               const ObjCCategoryDecl *C = nullptr) const;

  /// Lookup an instance method for a given selector.
  ObjCMethodDecl *lookupInstanceMethod(Selector Sel) const {
    return lookupMethod(Sel, true/*isInstance*/);
  }

  /// Lookup a class method for a given selector.
  ObjCMethodDecl *lookupClassMethod(Selector Sel) const {
    return lookupMethod(Sel, false/*isInstance*/);
  }

  ObjCInterfaceDecl *lookupInheritedClass(const IdentifierInfo *ICName);

  /// Lookup a method in the classes implementation hierarchy.
  ObjCMethodDecl *lookupPrivateMethod(const Selector &Sel,
                                      bool Instance=true) const;

  ObjCMethodDecl *lookupPrivateClassMethod(const Selector &Sel) {
    return lookupPrivateMethod(Sel, false);
  }

  /// Lookup a setter or getter in the class hierarchy,
  /// including in all categories except for category passed
  /// as argument.
  ObjCMethodDecl *lookupPropertyAccessor(const Selector Sel,
                                         const ObjCCategoryDecl *Cat,
                                         bool IsClassProperty) const {
    return lookupMethod(Sel, !IsClassProperty/*isInstance*/,
                        false/*shallowCategoryLookup*/,
                        true /* followsSuper */,
                        Cat);
  }

  SourceLocation getEndOfDefinitionLoc() const {
    if (!hasDefinition())
      return getLocation();

    return data().EndLoc;
  }

  void setEndOfDefinitionLoc(SourceLocation LE) { data().EndLoc = LE; }

  /// Retrieve the starting location of the superclass.
  SourceLocation getSuperClassLoc() const;

  /// isImplicitInterfaceDecl - check that this is an implicitly declared
  /// ObjCInterfaceDecl node. This is for legacy objective-c \@implementation
  /// declaration without an \@interface declaration.
  bool isImplicitInterfaceDecl() const {
    return hasDefinition() ? data().Definition->isImplicit() : isImplicit();
  }

  /// ClassImplementsProtocol - Checks that 'lProto' protocol
  /// has been implemented in IDecl class, its super class or categories (if
  /// lookupCategory is true).
  bool ClassImplementsProtocol(ObjCProtocolDecl *lProto,
                               bool lookupCategory,
                               bool RHSIsQualifiedID = false);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  /// Retrieves the canonical declaration of this Objective-C class.
  ObjCInterfaceDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const ObjCInterfaceDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Low-level accessor
  const Type *getTypeForDecl() const { return TypeForDecl; }
  void setTypeForDecl(const Type *TD) const { TypeForDecl = TD; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCInterface; }

private:
  const ObjCInterfaceDecl *findInterfaceWithDesignatedInitializers() const;
  bool inheritsDesignatedInitializers() const;
};

/// ObjCIvarDecl - Represents an ObjC instance variable. In general, ObjC
/// instance variables are identical to C. The only exception is Objective-C
/// supports C++ style access control. For example:
///
///   \@interface IvarExample : NSObject
///   {
///     id defaultToProtected;
///   \@public:
///     id canBePublic; // same as C++.
///   \@protected:
///     id canBeProtected; // same as C++.
///   \@package:
///     id canBePackage; // framework visibility (not available in C++).
///   }
///
class ObjCIvarDecl : public FieldDecl {
  void anchor() override;

public:
  enum AccessControl {
    None, Private, Protected, Public, Package
  };

private:
  ObjCIvarDecl(ObjCContainerDecl *DC, SourceLocation StartLoc,
               SourceLocation IdLoc, IdentifierInfo *Id,
               QualType T, TypeSourceInfo *TInfo, AccessControl ac, Expr *BW,
               bool synthesized)
      : FieldDecl(ObjCIvar, DC, StartLoc, IdLoc, Id, T, TInfo, BW,
                  /*Mutable=*/false, /*HasInit=*/ICIS_NoInit),
        DeclAccess(ac), Synthesized(synthesized) {}

public:
  static ObjCIvarDecl *Create(ASTContext &C, ObjCContainerDecl *DC,
                              SourceLocation StartLoc, SourceLocation IdLoc,
                              IdentifierInfo *Id, QualType T,
                              TypeSourceInfo *TInfo,
                              AccessControl ac, Expr *BW = nullptr,
                              bool synthesized=false);

  static ObjCIvarDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Return the class interface that this ivar is logically contained
  /// in; this is either the interface where the ivar was declared, or the
  /// interface the ivar is conceptually a part of in the case of synthesized
  /// ivars.
  const ObjCInterfaceDecl *getContainingInterface() const;

  ObjCIvarDecl *getNextIvar() { return NextIvar; }
  const ObjCIvarDecl *getNextIvar() const { return NextIvar; }
  void setNextIvar(ObjCIvarDecl *ivar) { NextIvar = ivar; }

  void setAccessControl(AccessControl ac) { DeclAccess = ac; }

  AccessControl getAccessControl() const { return AccessControl(DeclAccess); }

  AccessControl getCanonicalAccessControl() const {
    return DeclAccess == None ? Protected : AccessControl(DeclAccess);
  }

  void setSynthesize(bool synth) { Synthesized = synth; }
  bool getSynthesize() const { return Synthesized; }

  /// Retrieve the type of this instance variable when viewed as a member of a
  /// specific object type.
  QualType getUsageType(QualType objectType) const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCIvar; }

private:
  /// NextIvar - Next Ivar in the list of ivars declared in class; class's
  /// extensions and class's implementation
  ObjCIvarDecl *NextIvar = nullptr;

  // NOTE: VC++ treats enums as signed, avoid using the AccessControl enum
  unsigned DeclAccess : 3;
  unsigned Synthesized : 1;
};

/// Represents a field declaration created by an \@defs(...).
class ObjCAtDefsFieldDecl : public FieldDecl {
  ObjCAtDefsFieldDecl(DeclContext *DC, SourceLocation StartLoc,
                      SourceLocation IdLoc, IdentifierInfo *Id,
                      QualType T, Expr *BW)
      : FieldDecl(ObjCAtDefsField, DC, StartLoc, IdLoc, Id, T,
                  /*TInfo=*/nullptr, // FIXME: Do ObjCAtDefs have declarators ?
                  BW, /*Mutable=*/false, /*HasInit=*/ICIS_NoInit) {}

  void anchor() override;

public:
  static ObjCAtDefsFieldDecl *Create(ASTContext &C, DeclContext *DC,
                                     SourceLocation StartLoc,
                                     SourceLocation IdLoc, IdentifierInfo *Id,
                                     QualType T, Expr *BW);

  static ObjCAtDefsFieldDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCAtDefsField; }
};

/// Represents an Objective-C protocol declaration.
///
/// Objective-C protocols declare a pure abstract type (i.e., no instance
/// variables are permitted).  Protocols originally drew inspiration from
/// C++ pure virtual functions (a C++ feature with nice semantics and lousy
/// syntax:-). Here is an example:
///
/// \code
/// \@protocol NSDraggingInfo <refproto1, refproto2>
/// - (NSWindow *)draggingDestinationWindow;
/// - (NSImage *)draggedImage;
/// \@end
/// \endcode
///
/// This says that NSDraggingInfo requires two methods and requires everything
/// that the two "referenced protocols" 'refproto1' and 'refproto2' require as
/// well.
///
/// \code
/// \@interface ImplementsNSDraggingInfo : NSObject \<NSDraggingInfo>
/// \@end
/// \endcode
///
/// ObjC protocols inspired Java interfaces. Unlike Java, ObjC classes and
/// protocols are in distinct namespaces. For example, Cocoa defines both
/// an NSObject protocol and class (which isn't allowed in Java). As a result,
/// protocols are referenced using angle brackets as follows:
///
/// id \<NSDraggingInfo> anyObjectThatImplementsNSDraggingInfo;
class ObjCProtocolDecl : public ObjCContainerDecl,
                         public Redeclarable<ObjCProtocolDecl> {
  struct DefinitionData {
    // The declaration that defines this protocol.
    ObjCProtocolDecl *Definition;

    /// Referenced protocols
    ObjCProtocolList ReferencedProtocols;
  };

  /// Contains a pointer to the data associated with this class,
  /// which will be NULL if this class has not yet been defined.
  ///
  /// The bit indicates when we don't need to check for out-of-date
  /// declarations. It will be set unless modules are enabled.
  llvm::PointerIntPair<DefinitionData *, 1, bool> Data;

  ObjCProtocolDecl(ASTContext &C, DeclContext *DC, IdentifierInfo *Id,
                   SourceLocation nameLoc, SourceLocation atStartLoc,
                   ObjCProtocolDecl *PrevDecl);

  void anchor() override;

  DefinitionData &data() const {
    assert(Data.getPointer() && "Objective-C protocol has no definition!");
    return *Data.getPointer();
  }

  void allocateDefinitionData();

  using redeclarable_base = Redeclarable<ObjCProtocolDecl>;

  ObjCProtocolDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  ObjCProtocolDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  ObjCProtocolDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class ASTReader;

  static ObjCProtocolDecl *Create(ASTContext &C, DeclContext *DC,
                                  IdentifierInfo *Id,
                                  SourceLocation nameLoc,
                                  SourceLocation atStartLoc,
                                  ObjCProtocolDecl *PrevDecl);

  static ObjCProtocolDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  const ObjCProtocolList &getReferencedProtocols() const {
    assert(hasDefinition() && "No definition available!");
    return data().ReferencedProtocols;
  }

  using protocol_iterator = ObjCProtocolList::iterator;
  using protocol_range = llvm::iterator_range<protocol_iterator>;

  protocol_range protocols() const {
    return protocol_range(protocol_begin(), protocol_end());
  }

  protocol_iterator protocol_begin() const {
    if (!hasDefinition())
      return protocol_iterator();

    return data().ReferencedProtocols.begin();
  }

  protocol_iterator protocol_end() const {
    if (!hasDefinition())
      return protocol_iterator();

    return data().ReferencedProtocols.end();
  }

  using protocol_loc_iterator = ObjCProtocolList::loc_iterator;
  using protocol_loc_range = llvm::iterator_range<protocol_loc_iterator>;

  protocol_loc_range protocol_locs() const {
    return protocol_loc_range(protocol_loc_begin(), protocol_loc_end());
  }

  protocol_loc_iterator protocol_loc_begin() const {
    if (!hasDefinition())
      return protocol_loc_iterator();

    return data().ReferencedProtocols.loc_begin();
  }

  protocol_loc_iterator protocol_loc_end() const {
    if (!hasDefinition())
      return protocol_loc_iterator();

    return data().ReferencedProtocols.loc_end();
  }

  unsigned protocol_size() const {
    if (!hasDefinition())
      return 0;

    return data().ReferencedProtocols.size();
  }

  /// setProtocolList - Set the list of protocols that this interface
  /// implements.
  void setProtocolList(ObjCProtocolDecl *const*List, unsigned Num,
                       const SourceLocation *Locs, ASTContext &C) {
    assert(hasDefinition() && "Protocol is not defined");
    data().ReferencedProtocols.set(List, Num, Locs, C);
  }

  ObjCProtocolDecl *lookupProtocolNamed(IdentifierInfo *PName);

  // Lookup a method. First, we search locally. If a method isn't
  // found, we search referenced protocols and class categories.
  ObjCMethodDecl *lookupMethod(Selector Sel, bool isInstance) const;

  ObjCMethodDecl *lookupInstanceMethod(Selector Sel) const {
    return lookupMethod(Sel, true/*isInstance*/);
  }

  ObjCMethodDecl *lookupClassMethod(Selector Sel) const {
    return lookupMethod(Sel, false/*isInstance*/);
  }

  /// Determine whether this protocol has a definition.
  bool hasDefinition() const {
    // If the name of this protocol is out-of-date, bring it up-to-date, which
    // might bring in a definition.
    // Note: a null value indicates that we don't have a definition and that
    // modules are enabled.
    if (!Data.getOpaqueValue())
      getMostRecentDecl();

    return Data.getPointer();
  }

  /// Retrieve the definition of this protocol, if any.
  ObjCProtocolDecl *getDefinition() {
    return hasDefinition()? Data.getPointer()->Definition : nullptr;
  }

  /// Retrieve the definition of this protocol, if any.
  const ObjCProtocolDecl *getDefinition() const {
    return hasDefinition()? Data.getPointer()->Definition : nullptr;
  }

  /// Determine whether this particular declaration is also the
  /// definition.
  bool isThisDeclarationADefinition() const {
    return getDefinition() == this;
  }

  /// Starts the definition of this Objective-C protocol.
  void startDefinition();

  /// Produce a name to be used for protocol's metadata. It comes either via
  /// objc_runtime_name attribute or protocol name.
  StringRef getObjCRuntimeNameAsString() const;

  SourceRange getSourceRange() const override LLVM_READONLY {
    if (isThisDeclarationADefinition())
      return ObjCContainerDecl::getSourceRange();

    return SourceRange(getAtStartLoc(), getLocation());
  }

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  /// Retrieves the canonical declaration of this Objective-C protocol.
  ObjCProtocolDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const ObjCProtocolDecl *getCanonicalDecl() const { return getFirstDecl(); }

  void collectPropertiesToImplement(PropertyMap &PM,
                                    PropertyDeclOrder &PO) const override;

  void collectInheritedProtocolProperties(const ObjCPropertyDecl *Property,
                                          ProtocolPropertySet &PS,
                                          PropertyDeclOrder &PO) const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCProtocol; }
};

/// ObjCCategoryDecl - Represents a category declaration. A category allows
/// you to add methods to an existing class (without subclassing or modifying
/// the original class interface or implementation:-). Categories don't allow
/// you to add instance data. The following example adds "myMethod" to all
/// NSView's within a process:
///
/// \@interface NSView (MyViewMethods)
/// - myMethod;
/// \@end
///
/// Categories also allow you to split the implementation of a class across
/// several files (a feature more naturally supported in C++).
///
/// Categories were originally inspired by dynamic languages such as Common
/// Lisp and Smalltalk.  More traditional class-based languages (C++, Java)
/// don't support this level of dynamism, which is both powerful and dangerous.
class ObjCCategoryDecl : public ObjCContainerDecl {
  /// Interface belonging to this category
  ObjCInterfaceDecl *ClassInterface;

  /// The type parameters associated with this category, if any.
  ObjCTypeParamList *TypeParamList = nullptr;

  /// referenced protocols in this category.
  ObjCProtocolList ReferencedProtocols;

  /// Next category belonging to this class.
  /// FIXME: this should not be a singly-linked list.  Move storage elsewhere.
  ObjCCategoryDecl *NextClassCategory = nullptr;

  /// The location of the category name in this declaration.
  SourceLocation CategoryNameLoc;

  /// class extension may have private ivars.
  SourceLocation IvarLBraceLoc;
  SourceLocation IvarRBraceLoc;

  ObjCCategoryDecl(DeclContext *DC, SourceLocation AtLoc,
                   SourceLocation ClassNameLoc, SourceLocation CategoryNameLoc,
                   IdentifierInfo *Id, ObjCInterfaceDecl *IDecl,
                   ObjCTypeParamList *typeParamList,
                   SourceLocation IvarLBraceLoc = SourceLocation(),
                   SourceLocation IvarRBraceLoc = SourceLocation());

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ObjCCategoryDecl *Create(ASTContext &C, DeclContext *DC,
                                  SourceLocation AtLoc,
                                  SourceLocation ClassNameLoc,
                                  SourceLocation CategoryNameLoc,
                                  IdentifierInfo *Id,
                                  ObjCInterfaceDecl *IDecl,
                                  ObjCTypeParamList *typeParamList,
                                  SourceLocation IvarLBraceLoc=SourceLocation(),
                                  SourceLocation IvarRBraceLoc=SourceLocation());
  static ObjCCategoryDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  ObjCInterfaceDecl *getClassInterface() { return ClassInterface; }
  const ObjCInterfaceDecl *getClassInterface() const { return ClassInterface; }

  /// Retrieve the type parameter list associated with this category or
  /// extension.
  ObjCTypeParamList *getTypeParamList() const { return TypeParamList; }

  /// Set the type parameters of this category.
  ///
  /// This function is used by the AST importer, which must import the type
  /// parameters after creating their DeclContext to avoid loops.
  void setTypeParamList(ObjCTypeParamList *TPL);


  ObjCCategoryImplDecl *getImplementation() const;
  void setImplementation(ObjCCategoryImplDecl *ImplD);

  /// setProtocolList - Set the list of protocols that this interface
  /// implements.
  void setProtocolList(ObjCProtocolDecl *const*List, unsigned Num,
                       const SourceLocation *Locs, ASTContext &C) {
    ReferencedProtocols.set(List, Num, Locs, C);
  }

  const ObjCProtocolList &getReferencedProtocols() const {
    return ReferencedProtocols;
  }

  using protocol_iterator = ObjCProtocolList::iterator;
  using protocol_range = llvm::iterator_range<protocol_iterator>;

  protocol_range protocols() const {
    return protocol_range(protocol_begin(), protocol_end());
  }

  protocol_iterator protocol_begin() const {
    return ReferencedProtocols.begin();
  }

  protocol_iterator protocol_end() const { return ReferencedProtocols.end(); }
  unsigned protocol_size() const { return ReferencedProtocols.size(); }

  using protocol_loc_iterator = ObjCProtocolList::loc_iterator;
  using protocol_loc_range = llvm::iterator_range<protocol_loc_iterator>;

  protocol_loc_range protocol_locs() const {
    return protocol_loc_range(protocol_loc_begin(), protocol_loc_end());
  }

  protocol_loc_iterator protocol_loc_begin() const {
    return ReferencedProtocols.loc_begin();
  }

  protocol_loc_iterator protocol_loc_end() const {
    return ReferencedProtocols.loc_end();
  }

  ObjCCategoryDecl *getNextClassCategory() const { return NextClassCategory; }

  /// Retrieve the pointer to the next stored category (or extension),
  /// which may be hidden.
  ObjCCategoryDecl *getNextClassCategoryRaw() const {
    return NextClassCategory;
  }

  bool IsClassExtension() const { return getIdentifier() == nullptr; }

  using ivar_iterator = specific_decl_iterator<ObjCIvarDecl>;
  using ivar_range = llvm::iterator_range<specific_decl_iterator<ObjCIvarDecl>>;

  ivar_range ivars() const { return ivar_range(ivar_begin(), ivar_end()); }

  ivar_iterator ivar_begin() const {
    return ivar_iterator(decls_begin());
  }

  ivar_iterator ivar_end() const {
    return ivar_iterator(decls_end());
  }

  unsigned ivar_size() const {
    return std::distance(ivar_begin(), ivar_end());
  }

  bool ivar_empty() const {
    return ivar_begin() == ivar_end();
  }

  SourceLocation getCategoryNameLoc() const { return CategoryNameLoc; }
  void setCategoryNameLoc(SourceLocation Loc) { CategoryNameLoc = Loc; }

  void setIvarLBraceLoc(SourceLocation Loc) { IvarLBraceLoc = Loc; }
  SourceLocation getIvarLBraceLoc() const { return IvarLBraceLoc; }
  void setIvarRBraceLoc(SourceLocation Loc) { IvarRBraceLoc = Loc; }
  SourceLocation getIvarRBraceLoc() const { return IvarRBraceLoc; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCCategory; }
};

class ObjCImplDecl : public ObjCContainerDecl {
  /// Class interface for this class/category implementation
  ObjCInterfaceDecl *ClassInterface;

  void anchor() override;

protected:
  ObjCImplDecl(Kind DK, DeclContext *DC,
               ObjCInterfaceDecl *classInterface,
               IdentifierInfo *Id,
               SourceLocation nameLoc, SourceLocation atStartLoc)
      : ObjCContainerDecl(DK, DC, Id, nameLoc, atStartLoc),
        ClassInterface(classInterface) {}

public:
  const ObjCInterfaceDecl *getClassInterface() const { return ClassInterface; }
  ObjCInterfaceDecl *getClassInterface() { return ClassInterface; }
  void setClassInterface(ObjCInterfaceDecl *IFace);

  void addInstanceMethod(ObjCMethodDecl *method) {
    // FIXME: Context should be set correctly before we get here.
    method->setLexicalDeclContext(this);
    addDecl(method);
  }

  void addClassMethod(ObjCMethodDecl *method) {
    // FIXME: Context should be set correctly before we get here.
    method->setLexicalDeclContext(this);
    addDecl(method);
  }

  void addPropertyImplementation(ObjCPropertyImplDecl *property);

  ObjCPropertyImplDecl *FindPropertyImplDecl(IdentifierInfo *propertyId,
                            ObjCPropertyQueryKind queryKind) const;
  ObjCPropertyImplDecl *FindPropertyImplIvarDecl(IdentifierInfo *ivarId) const;

  // Iterator access to properties.
  using propimpl_iterator = specific_decl_iterator<ObjCPropertyImplDecl>;
  using propimpl_range =
      llvm::iterator_range<specific_decl_iterator<ObjCPropertyImplDecl>>;

  propimpl_range property_impls() const {
    return propimpl_range(propimpl_begin(), propimpl_end());
  }

  propimpl_iterator propimpl_begin() const {
    return propimpl_iterator(decls_begin());
  }

  propimpl_iterator propimpl_end() const {
    return propimpl_iterator(decls_end());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstObjCImpl && K <= lastObjCImpl;
  }
};

/// ObjCCategoryImplDecl - An object of this class encapsulates a category
/// \@implementation declaration. If a category class has declaration of a
/// property, its implementation must be specified in the category's
/// \@implementation declaration. Example:
/// \@interface I \@end
/// \@interface I(CATEGORY)
///    \@property int p1, d1;
/// \@end
/// \@implementation I(CATEGORY)
///  \@dynamic p1,d1;
/// \@end
///
/// ObjCCategoryImplDecl
class ObjCCategoryImplDecl : public ObjCImplDecl {
  // Category name location
  SourceLocation CategoryNameLoc;

  ObjCCategoryImplDecl(DeclContext *DC, IdentifierInfo *Id,
                       ObjCInterfaceDecl *classInterface,
                       SourceLocation nameLoc, SourceLocation atStartLoc,
                       SourceLocation CategoryNameLoc)
      : ObjCImplDecl(ObjCCategoryImpl, DC, classInterface, Id,
                     nameLoc, atStartLoc),
        CategoryNameLoc(CategoryNameLoc) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ObjCCategoryImplDecl *Create(ASTContext &C, DeclContext *DC,
                                      IdentifierInfo *Id,
                                      ObjCInterfaceDecl *classInterface,
                                      SourceLocation nameLoc,
                                      SourceLocation atStartLoc,
                                      SourceLocation CategoryNameLoc);
  static ObjCCategoryImplDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  ObjCCategoryDecl *getCategoryDecl() const;

  SourceLocation getCategoryNameLoc() const { return CategoryNameLoc; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCCategoryImpl;}
};

raw_ostream &operator<<(raw_ostream &OS, const ObjCCategoryImplDecl &CID);

/// ObjCImplementationDecl - Represents a class definition - this is where
/// method definitions are specified. For example:
///
/// @code
/// \@implementation MyClass
/// - (void)myMethod { /* do something */ }
/// \@end
/// @endcode
///
/// In a non-fragile runtime, instance variables can appear in the class
/// interface, class extensions (nameless categories), and in the implementation
/// itself, as well as being synthesized as backing storage for properties.
///
/// In a fragile runtime, instance variables are specified in the class
/// interface, \em not in the implementation. Nevertheless (for legacy reasons),
/// we allow instance variables to be specified in the implementation. When
/// specified, they need to be \em identical to the interface.
class ObjCImplementationDecl : public ObjCImplDecl {
  /// Implementation Class's super class.
  ObjCInterfaceDecl *SuperClass;
  SourceLocation SuperLoc;

  /// \@implementation may have private ivars.
  SourceLocation IvarLBraceLoc;
  SourceLocation IvarRBraceLoc;

  /// Support for ivar initialization.
  /// The arguments used to initialize the ivars
  LazyCXXCtorInitializersPtr IvarInitializers;
  unsigned NumIvarInitializers = 0;

  /// Do the ivars of this class require initialization other than
  /// zero-initialization?
  bool HasNonZeroConstructors : 1;

  /// Do the ivars of this class require non-trivial destruction?
  bool HasDestructors : 1;

  ObjCImplementationDecl(DeclContext *DC,
                         ObjCInterfaceDecl *classInterface,
                         ObjCInterfaceDecl *superDecl,
                         SourceLocation nameLoc, SourceLocation atStartLoc,
                         SourceLocation superLoc = SourceLocation(),
                         SourceLocation IvarLBraceLoc=SourceLocation(),
                         SourceLocation IvarRBraceLoc=SourceLocation())
      : ObjCImplDecl(ObjCImplementation, DC, classInterface,
                     classInterface ? classInterface->getIdentifier()
                                    : nullptr,
                     nameLoc, atStartLoc),
         SuperClass(superDecl), SuperLoc(superLoc),
         IvarLBraceLoc(IvarLBraceLoc), IvarRBraceLoc(IvarRBraceLoc),
         HasNonZeroConstructors(false), HasDestructors(false) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ObjCImplementationDecl *Create(ASTContext &C, DeclContext *DC,
                                        ObjCInterfaceDecl *classInterface,
                                        ObjCInterfaceDecl *superDecl,
                                        SourceLocation nameLoc,
                                        SourceLocation atStartLoc,
                                     SourceLocation superLoc = SourceLocation(),
                                        SourceLocation IvarLBraceLoc=SourceLocation(),
                                        SourceLocation IvarRBraceLoc=SourceLocation());

  static ObjCImplementationDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// init_iterator - Iterates through the ivar initializer list.
  using init_iterator = CXXCtorInitializer **;

  /// init_const_iterator - Iterates through the ivar initializer list.
  using init_const_iterator = CXXCtorInitializer * const *;

  using init_range = llvm::iterator_range<init_iterator>;
  using init_const_range = llvm::iterator_range<init_const_iterator>;

  init_range inits() { return init_range(init_begin(), init_end()); }

  init_const_range inits() const {
    return init_const_range(init_begin(), init_end());
  }

  /// init_begin() - Retrieve an iterator to the first initializer.
  init_iterator init_begin() {
    const auto *ConstThis = this;
    return const_cast<init_iterator>(ConstThis->init_begin());
  }

  /// begin() - Retrieve an iterator to the first initializer.
  init_const_iterator init_begin() const;

  /// init_end() - Retrieve an iterator past the last initializer.
  init_iterator       init_end()       {
    return init_begin() + NumIvarInitializers;
  }

  /// end() - Retrieve an iterator past the last initializer.
  init_const_iterator init_end() const {
    return init_begin() + NumIvarInitializers;
  }

  /// getNumArgs - Number of ivars which must be initialized.
  unsigned getNumIvarInitializers() const {
    return NumIvarInitializers;
  }

  void setNumIvarInitializers(unsigned numNumIvarInitializers) {
    NumIvarInitializers = numNumIvarInitializers;
  }

  void setIvarInitializers(ASTContext &C,
                           CXXCtorInitializer ** initializers,
                           unsigned numInitializers);

  /// Do any of the ivars of this class (not counting its base classes)
  /// require construction other than zero-initialization?
  bool hasNonZeroConstructors() const { return HasNonZeroConstructors; }
  void setHasNonZeroConstructors(bool val) { HasNonZeroConstructors = val; }

  /// Do any of the ivars of this class (not counting its base classes)
  /// require non-trivial destruction?
  bool hasDestructors() const { return HasDestructors; }
  void setHasDestructors(bool val) { HasDestructors = val; }

  /// getIdentifier - Get the identifier that names the class
  /// interface associated with this implementation.
  IdentifierInfo *getIdentifier() const {
    return getClassInterface()->getIdentifier();
  }

  /// getName - Get the name of identifier for the class interface associated
  /// with this implementation as a StringRef.
  //
  // FIXME: This is a bad API, we are hiding NamedDecl::getName with a different
  // meaning.
  StringRef getName() const {
    assert(getIdentifier() && "Name is not a simple identifier");
    return getIdentifier()->getName();
  }

  /// Get the name of the class associated with this interface.
  //
  // FIXME: Move to StringRef API.
  std::string getNameAsString() const {
    return getName();
  }

  /// Produce a name to be used for class's metadata. It comes either via
  /// class's objc_runtime_name attribute or class name.
  StringRef getObjCRuntimeNameAsString() const;

  const ObjCInterfaceDecl *getSuperClass() const { return SuperClass; }
  ObjCInterfaceDecl *getSuperClass() { return SuperClass; }
  SourceLocation getSuperClassLoc() const { return SuperLoc; }

  void setSuperClass(ObjCInterfaceDecl * superCls) { SuperClass = superCls; }

  void setIvarLBraceLoc(SourceLocation Loc) { IvarLBraceLoc = Loc; }
  SourceLocation getIvarLBraceLoc() const { return IvarLBraceLoc; }
  void setIvarRBraceLoc(SourceLocation Loc) { IvarRBraceLoc = Loc; }
  SourceLocation getIvarRBraceLoc() const { return IvarRBraceLoc; }

  using ivar_iterator = specific_decl_iterator<ObjCIvarDecl>;
  using ivar_range = llvm::iterator_range<specific_decl_iterator<ObjCIvarDecl>>;

  ivar_range ivars() const { return ivar_range(ivar_begin(), ivar_end()); }

  ivar_iterator ivar_begin() const {
    return ivar_iterator(decls_begin());
  }

  ivar_iterator ivar_end() const {
    return ivar_iterator(decls_end());
  }

  unsigned ivar_size() const {
    return std::distance(ivar_begin(), ivar_end());
  }

  bool ivar_empty() const {
    return ivar_begin() == ivar_end();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCImplementation; }
};

raw_ostream &operator<<(raw_ostream &OS, const ObjCImplementationDecl &ID);

/// ObjCCompatibleAliasDecl - Represents alias of a class. This alias is
/// declared as \@compatibility_alias alias class.
class ObjCCompatibleAliasDecl : public NamedDecl {
  /// Class that this is an alias of.
  ObjCInterfaceDecl *AliasedClass;

  ObjCCompatibleAliasDecl(DeclContext *DC, SourceLocation L, IdentifierInfo *Id,
                          ObjCInterfaceDecl* aliasedClass)
      : NamedDecl(ObjCCompatibleAlias, DC, L, Id), AliasedClass(aliasedClass) {}

  void anchor() override;

public:
  static ObjCCompatibleAliasDecl *Create(ASTContext &C, DeclContext *DC,
                                         SourceLocation L, IdentifierInfo *Id,
                                         ObjCInterfaceDecl* aliasedClass);

  static ObjCCompatibleAliasDecl *CreateDeserialized(ASTContext &C,
                                                     unsigned ID);

  const ObjCInterfaceDecl *getClassInterface() const { return AliasedClass; }
  ObjCInterfaceDecl *getClassInterface() { return AliasedClass; }
  void setClassInterface(ObjCInterfaceDecl *D) { AliasedClass = D; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ObjCCompatibleAlias; }
};

/// ObjCPropertyImplDecl - Represents implementation declaration of a property
/// in a class or category implementation block. For example:
/// \@synthesize prop1 = ivar1;
///
class ObjCPropertyImplDecl : public Decl {
public:
  enum Kind {
    Synthesize,
    Dynamic
  };

private:
  SourceLocation AtLoc;   // location of \@synthesize or \@dynamic

  /// For \@synthesize, the location of the ivar, if it was written in
  /// the source code.
  ///
  /// \code
  /// \@synthesize int a = b
  /// \endcode
  SourceLocation IvarLoc;

  /// Property declaration being implemented
  ObjCPropertyDecl *PropertyDecl;

  /// Null for \@dynamic. Required for \@synthesize.
  ObjCIvarDecl *PropertyIvarDecl;

  /// Null for \@dynamic. Non-null if property must be copy-constructed in
  /// getter.
  Expr *GetterCXXConstructor = nullptr;

  /// Null for \@dynamic. Non-null if property has assignment operator to call
  /// in Setter synthesis.
  Expr *SetterCXXAssignment = nullptr;

  ObjCPropertyImplDecl(DeclContext *DC, SourceLocation atLoc, SourceLocation L,
                       ObjCPropertyDecl *property,
                       Kind PK,
                       ObjCIvarDecl *ivarDecl,
                       SourceLocation ivarLoc)
      : Decl(ObjCPropertyImpl, DC, L), AtLoc(atLoc),
        IvarLoc(ivarLoc), PropertyDecl(property), PropertyIvarDecl(ivarDecl) {
    assert(PK == Dynamic || PropertyIvarDecl);
  }

public:
  friend class ASTDeclReader;

  static ObjCPropertyImplDecl *Create(ASTContext &C, DeclContext *DC,
                                      SourceLocation atLoc, SourceLocation L,
                                      ObjCPropertyDecl *property,
                                      Kind PK,
                                      ObjCIvarDecl *ivarDecl,
                                      SourceLocation ivarLoc);

  static ObjCPropertyImplDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  SourceLocation getBeginLoc() const LLVM_READONLY { return AtLoc; }
  void setAtLoc(SourceLocation Loc) { AtLoc = Loc; }

  ObjCPropertyDecl *getPropertyDecl() const {
    return PropertyDecl;
  }
  void setPropertyDecl(ObjCPropertyDecl *Prop) { PropertyDecl = Prop; }

  Kind getPropertyImplementation() const {
    return PropertyIvarDecl ? Synthesize : Dynamic;
  }

  ObjCIvarDecl *getPropertyIvarDecl() const {
    return PropertyIvarDecl;
  }
  SourceLocation getPropertyIvarDeclLoc() const { return IvarLoc; }

  void setPropertyIvarDecl(ObjCIvarDecl *Ivar,
                           SourceLocation IvarLoc) {
    PropertyIvarDecl = Ivar;
    this->IvarLoc = IvarLoc;
  }

  /// For \@synthesize, returns true if an ivar name was explicitly
  /// specified.
  ///
  /// \code
  /// \@synthesize int a = b; // true
  /// \@synthesize int a; // false
  /// \endcode
  bool isIvarNameSpecified() const {
    return IvarLoc.isValid() && IvarLoc != getLocation();
  }

  Expr *getGetterCXXConstructor() const {
    return GetterCXXConstructor;
  }

  void setGetterCXXConstructor(Expr *getterCXXConstructor) {
    GetterCXXConstructor = getterCXXConstructor;
  }

  Expr *getSetterCXXAssignment() const {
    return SetterCXXAssignment;
  }

  void setSetterCXXAssignment(Expr *setterCXXAssignment) {
    SetterCXXAssignment = setterCXXAssignment;
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Decl::Kind K) { return K == ObjCPropertyImpl; }
};

template<bool (*Filter)(ObjCCategoryDecl *)>
void
ObjCInterfaceDecl::filtered_category_iterator<Filter>::
findAcceptableCategory() {
  while (Current && !Filter(Current))
    Current = Current->getNextClassCategoryRaw();
}

template<bool (*Filter)(ObjCCategoryDecl *)>
inline ObjCInterfaceDecl::filtered_category_iterator<Filter> &
ObjCInterfaceDecl::filtered_category_iterator<Filter>::operator++() {
  Current = Current->getNextClassCategoryRaw();
  findAcceptableCategory();
  return *this;
}

inline bool ObjCInterfaceDecl::isVisibleCategory(ObjCCategoryDecl *Cat) {
  return !Cat->isHidden();
}

inline bool ObjCInterfaceDecl::isVisibleExtension(ObjCCategoryDecl *Cat) {
  return Cat->IsClassExtension() && !Cat->isHidden();
}

inline bool ObjCInterfaceDecl::isKnownExtension(ObjCCategoryDecl *Cat) {
  return Cat->IsClassExtension();
}

} // namespace clang

#endif // LLVM_CLANG_AST_DECLOBJC_H
