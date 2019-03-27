//===- DeclTemplate.h - Classes for representing C++ templates --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the C++ template declaration subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLTEMPLATE_H
#define LLVM_CLANG_AST_DECLTEMPLATE_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

namespace clang {

enum BuiltinTemplateKind : int;
class ClassTemplateDecl;
class ClassTemplatePartialSpecializationDecl;
class Expr;
class FunctionTemplateDecl;
class IdentifierInfo;
class NonTypeTemplateParmDecl;
class TemplateDecl;
class TemplateTemplateParmDecl;
class TemplateTypeParmDecl;
class UnresolvedSetImpl;
class VarTemplateDecl;
class VarTemplatePartialSpecializationDecl;

/// Stores a template parameter of any kind.
using TemplateParameter =
    llvm::PointerUnion3<TemplateTypeParmDecl *, NonTypeTemplateParmDecl *,
                        TemplateTemplateParmDecl *>;

NamedDecl *getAsNamedDecl(TemplateParameter P);

/// Stores a list of template parameters for a TemplateDecl and its
/// derived classes.
class TemplateParameterList final
    : private llvm::TrailingObjects<TemplateParameterList, NamedDecl *,
                                    Expr *> {
  /// The location of the 'template' keyword.
  SourceLocation TemplateLoc;

  /// The locations of the '<' and '>' angle brackets.
  SourceLocation LAngleLoc, RAngleLoc;

  /// The number of template parameters in this template
  /// parameter list.
  unsigned NumParams : 30;

  /// Whether this template parameter list contains an unexpanded parameter
  /// pack.
  unsigned ContainsUnexpandedParameterPack : 1;

  /// Whether this template parameter list has an associated requires-clause
  unsigned HasRequiresClause : 1;

protected:
  TemplateParameterList(SourceLocation TemplateLoc, SourceLocation LAngleLoc,
                        ArrayRef<NamedDecl *> Params, SourceLocation RAngleLoc,
                        Expr *RequiresClause);

  size_t numTrailingObjects(OverloadToken<NamedDecl *>) const {
    return NumParams;
  }

  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    return HasRequiresClause;
  }

public:
  template <size_t N, bool HasRequiresClause>
  friend class FixedSizeTemplateParameterListStorage;
  friend TrailingObjects;

  static TemplateParameterList *Create(const ASTContext &C,
                                       SourceLocation TemplateLoc,
                                       SourceLocation LAngleLoc,
                                       ArrayRef<NamedDecl *> Params,
                                       SourceLocation RAngleLoc,
                                       Expr *RequiresClause);

  /// Iterates through the template parameters in this list.
  using iterator = NamedDecl **;

  /// Iterates through the template parameters in this list.
  using const_iterator = NamedDecl * const *;

  iterator begin() { return getTrailingObjects<NamedDecl *>(); }
  const_iterator begin() const { return getTrailingObjects<NamedDecl *>(); }
  iterator end() { return begin() + NumParams; }
  const_iterator end() const { return begin() + NumParams; }

  unsigned size() const { return NumParams; }

  ArrayRef<NamedDecl*> asArray() {
    return llvm::makeArrayRef(begin(), end());
  }
  ArrayRef<const NamedDecl*> asArray() const {
    return llvm::makeArrayRef(begin(), size());
  }

  NamedDecl* getParam(unsigned Idx) {
    assert(Idx < size() && "Template parameter index out-of-range");
    return begin()[Idx];
  }
  const NamedDecl* getParam(unsigned Idx) const {
    assert(Idx < size() && "Template parameter index out-of-range");
    return begin()[Idx];
  }

  /// Returns the minimum number of arguments needed to form a
  /// template specialization.
  ///
  /// This may be fewer than the number of template parameters, if some of
  /// the parameters have default arguments or if there is a parameter pack.
  unsigned getMinRequiredArguments() const;

  /// Get the depth of this template parameter list in the set of
  /// template parameter lists.
  ///
  /// The first template parameter list in a declaration will have depth 0,
  /// the second template parameter list will have depth 1, etc.
  unsigned getDepth() const;

  /// Determine whether this template parameter list contains an
  /// unexpanded parameter pack.
  bool containsUnexpandedParameterPack() const {
    return ContainsUnexpandedParameterPack;
  }

  /// The constraint-expression of the associated requires-clause.
  Expr *getRequiresClause() {
    return HasRequiresClause ? *getTrailingObjects<Expr *>() : nullptr;
  }

  /// The constraint-expression of the associated requires-clause.
  const Expr *getRequiresClause() const {
    return HasRequiresClause ? *getTrailingObjects<Expr *>() : nullptr;
  }

  SourceLocation getTemplateLoc() const { return TemplateLoc; }
  SourceLocation getLAngleLoc() const { return LAngleLoc; }
  SourceLocation getRAngleLoc() const { return RAngleLoc; }

  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(TemplateLoc, RAngleLoc);
  }

public:
  // FIXME: workaround for MSVC 2013; remove when no longer needed
  using FixedSizeStorageOwner = TrailingObjects::FixedSizeStorageOwner;
};

/// Stores a list of template parameters and the associated
/// requires-clause (if any) for a TemplateDecl and its derived classes.
/// Suitable for creating on the stack.
template <size_t N, bool HasRequiresClause>
class FixedSizeTemplateParameterListStorage
    : public TemplateParameterList::FixedSizeStorageOwner {
  typename TemplateParameterList::FixedSizeStorage<
      NamedDecl *, Expr *>::with_counts<
      N, HasRequiresClause ? 1u : 0u
      >::type storage;

public:
  FixedSizeTemplateParameterListStorage(SourceLocation TemplateLoc,
                                        SourceLocation LAngleLoc,
                                        ArrayRef<NamedDecl *> Params,
                                        SourceLocation RAngleLoc,
                                        Expr *RequiresClause)
      : FixedSizeStorageOwner(
            (assert(N == Params.size()),
             assert(HasRequiresClause == static_cast<bool>(RequiresClause)),
             new (static_cast<void *>(&storage)) TemplateParameterList(
                 TemplateLoc, LAngleLoc, Params, RAngleLoc, RequiresClause))) {}
};

/// A template argument list.
class TemplateArgumentList final
    : private llvm::TrailingObjects<TemplateArgumentList, TemplateArgument> {
  /// The template argument list.
  const TemplateArgument *Arguments;

  /// The number of template arguments in this template
  /// argument list.
  unsigned NumArguments;

  // Constructs an instance with an internal Argument list, containing
  // a copy of the Args array. (Called by CreateCopy)
  TemplateArgumentList(ArrayRef<TemplateArgument> Args);

public:
  friend TrailingObjects;

  TemplateArgumentList(const TemplateArgumentList &) = delete;
  TemplateArgumentList &operator=(const TemplateArgumentList &) = delete;

  /// Type used to indicate that the template argument list itself is a
  /// stack object. It does not own its template arguments.
  enum OnStackType { OnStack };

  /// Create a new template argument list that copies the given set of
  /// template arguments.
  static TemplateArgumentList *CreateCopy(ASTContext &Context,
                                          ArrayRef<TemplateArgument> Args);

  /// Construct a new, temporary template argument list on the stack.
  ///
  /// The template argument list does not own the template arguments
  /// provided.
  explicit TemplateArgumentList(OnStackType, ArrayRef<TemplateArgument> Args)
      : Arguments(Args.data()), NumArguments(Args.size()) {}

  /// Produces a shallow copy of the given template argument list.
  ///
  /// This operation assumes that the input argument list outlives it.
  /// This takes the list as a pointer to avoid looking like a copy
  /// constructor, since this really really isn't safe to use that
  /// way.
  explicit TemplateArgumentList(const TemplateArgumentList *Other)
      : Arguments(Other->data()), NumArguments(Other->size()) {}

  /// Retrieve the template argument at a given index.
  const TemplateArgument &get(unsigned Idx) const {
    assert(Idx < NumArguments && "Invalid template argument index");
    return data()[Idx];
  }

  /// Retrieve the template argument at a given index.
  const TemplateArgument &operator[](unsigned Idx) const { return get(Idx); }

  /// Produce this as an array ref.
  ArrayRef<TemplateArgument> asArray() const {
    return llvm::makeArrayRef(data(), size());
  }

  /// Retrieve the number of template arguments in this
  /// template argument list.
  unsigned size() const { return NumArguments; }

  /// Retrieve a pointer to the template argument list.
  const TemplateArgument *data() const { return Arguments; }
};

void *allocateDefaultArgStorageChain(const ASTContext &C);

/// Storage for a default argument. This is conceptually either empty, or an
/// argument value, or a pointer to a previous declaration that had a default
/// argument.
///
/// However, this is complicated by modules: while we require all the default
/// arguments for a template to be equivalent, there may be more than one, and
/// we need to track all the originating parameters to determine if the default
/// argument is visible.
template<typename ParmDecl, typename ArgType>
class DefaultArgStorage {
  /// Storage for both the value *and* another parameter from which we inherit
  /// the default argument. This is used when multiple default arguments for a
  /// parameter are merged together from different modules.
  struct Chain {
    ParmDecl *PrevDeclWithDefaultArg;
    ArgType Value;
  };
  static_assert(sizeof(Chain) == sizeof(void *) * 2,
                "non-pointer argument type?");

  llvm::PointerUnion3<ArgType, ParmDecl*, Chain*> ValueOrInherited;

  static ParmDecl *getParmOwningDefaultArg(ParmDecl *Parm) {
    const DefaultArgStorage &Storage = Parm->getDefaultArgStorage();
    if (auto *Prev = Storage.ValueOrInherited.template dyn_cast<ParmDecl *>())
      Parm = Prev;
    assert(!Parm->getDefaultArgStorage()
                .ValueOrInherited.template is<ParmDecl *>() &&
           "should only be one level of indirection");
    return Parm;
  }

public:
  DefaultArgStorage() : ValueOrInherited(ArgType()) {}

  /// Determine whether there is a default argument for this parameter.
  bool isSet() const { return !ValueOrInherited.isNull(); }

  /// Determine whether the default argument for this parameter was inherited
  /// from a previous declaration of the same entity.
  bool isInherited() const { return ValueOrInherited.template is<ParmDecl*>(); }

  /// Get the default argument's value. This does not consider whether the
  /// default argument is visible.
  ArgType get() const {
    const DefaultArgStorage *Storage = this;
    if (const auto *Prev = ValueOrInherited.template dyn_cast<ParmDecl *>())
      Storage = &Prev->getDefaultArgStorage();
    if (const auto *C = Storage->ValueOrInherited.template dyn_cast<Chain *>())
      return C->Value;
    return Storage->ValueOrInherited.template get<ArgType>();
  }

  /// Get the parameter from which we inherit the default argument, if any.
  /// This is the parameter on which the default argument was actually written.
  const ParmDecl *getInheritedFrom() const {
    if (const auto *D = ValueOrInherited.template dyn_cast<ParmDecl *>())
      return D;
    if (const auto *C = ValueOrInherited.template dyn_cast<Chain *>())
      return C->PrevDeclWithDefaultArg;
    return nullptr;
  }

  /// Set the default argument.
  void set(ArgType Arg) {
    assert(!isSet() && "default argument already set");
    ValueOrInherited = Arg;
  }

  /// Set that the default argument was inherited from another parameter.
  void setInherited(const ASTContext &C, ParmDecl *InheritedFrom) {
    assert(!isInherited() && "default argument already inherited");
    InheritedFrom = getParmOwningDefaultArg(InheritedFrom);
    if (!isSet())
      ValueOrInherited = InheritedFrom;
    else
      ValueOrInherited = new (allocateDefaultArgStorageChain(C))
          Chain{InheritedFrom, ValueOrInherited.template get<ArgType>()};
  }

  /// Remove the default argument, even if it was inherited.
  void clear() {
    ValueOrInherited = ArgType();
  }
};

//===----------------------------------------------------------------------===//
// Kinds of Templates
//===----------------------------------------------------------------------===//

/// Stores the template parameter list and associated constraints for
/// \c TemplateDecl objects that track associated constraints.
class ConstrainedTemplateDeclInfo {
  friend TemplateDecl;

public:
  ConstrainedTemplateDeclInfo() = default;

  TemplateParameterList *getTemplateParameters() const {
    return TemplateParams;
  }

  Expr *getAssociatedConstraints() const { return AssociatedConstraints; }

protected:
  void setTemplateParameters(TemplateParameterList *TParams) {
    TemplateParams = TParams;
  }

  void setAssociatedConstraints(Expr *AC) { AssociatedConstraints = AC; }

  TemplateParameterList *TemplateParams = nullptr;
  Expr *AssociatedConstraints = nullptr;
};


/// The base class of all kinds of template declarations (e.g.,
/// class, function, etc.).
///
/// The TemplateDecl class stores the list of template parameters and a
/// reference to the templated scoped declaration: the underlying AST node.
class TemplateDecl : public NamedDecl {
  void anchor() override;

protected:
  // Construct a template decl with the given name and parameters.
  // Used when there is no templated element (e.g., for tt-params).
  TemplateDecl(ConstrainedTemplateDeclInfo *CTDI, Kind DK, DeclContext *DC,
               SourceLocation L, DeclarationName Name,
               TemplateParameterList *Params)
      : NamedDecl(DK, DC, L, Name), TemplatedDecl(nullptr),
        TemplateParams(CTDI) {
    this->setTemplateParameters(Params);
  }

  TemplateDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName Name,
               TemplateParameterList *Params)
      : TemplateDecl(nullptr, DK, DC, L, Name, Params) {}

  // Construct a template decl with name, parameters, and templated element.
  TemplateDecl(ConstrainedTemplateDeclInfo *CTDI, Kind DK, DeclContext *DC,
               SourceLocation L, DeclarationName Name,
               TemplateParameterList *Params, NamedDecl *Decl)
      : NamedDecl(DK, DC, L, Name), TemplatedDecl(Decl),
        TemplateParams(CTDI) {
    this->setTemplateParameters(Params);
  }

  TemplateDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName Name,
               TemplateParameterList *Params, NamedDecl *Decl)
      : TemplateDecl(nullptr, DK, DC, L, Name, Params, Decl) {}

public:
  /// Get the list of template parameters
  TemplateParameterList *getTemplateParameters() const {
    const auto *const CTDI =
        TemplateParams.dyn_cast<ConstrainedTemplateDeclInfo *>();
    return CTDI ? CTDI->getTemplateParameters()
                : TemplateParams.get<TemplateParameterList *>();
  }

  /// Get the constraint-expression from the associated requires-clause (if any)
  const Expr *getRequiresClause() const {
    const TemplateParameterList *const TP = getTemplateParameters();
    return TP ? TP->getRequiresClause() : nullptr;
  }

  Expr *getAssociatedConstraints() const {
    const auto *const C = cast<TemplateDecl>(getCanonicalDecl());
    const auto *const CTDI =
        C->TemplateParams.dyn_cast<ConstrainedTemplateDeclInfo *>();
    return CTDI ? CTDI->getAssociatedConstraints() : nullptr;
  }

  /// Get the underlying, templated declaration.
  NamedDecl *getTemplatedDecl() const { return TemplatedDecl; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstTemplate && K <= lastTemplate;
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getTemplateParameters()->getTemplateLoc(),
                       TemplatedDecl->getSourceRange().getEnd());
  }

protected:
  NamedDecl *TemplatedDecl;

  /// The template parameter list and optional requires-clause
  /// associated with this declaration; alternatively, a
  /// \c ConstrainedTemplateDeclInfo if the associated constraints of the
  /// template are being tracked by this particular declaration.
  llvm::PointerUnion<TemplateParameterList *,
                     ConstrainedTemplateDeclInfo *>
      TemplateParams;

  void setTemplateParameters(TemplateParameterList *TParams) {
    if (auto *const CTDI =
            TemplateParams.dyn_cast<ConstrainedTemplateDeclInfo *>()) {
      CTDI->setTemplateParameters(TParams);
    } else {
      TemplateParams = TParams;
    }
  }

  void setAssociatedConstraints(Expr *AC) {
    assert(isCanonicalDecl() &&
           "Attaching associated constraints to non-canonical Decl");
    TemplateParams.get<ConstrainedTemplateDeclInfo *>()
        ->setAssociatedConstraints(AC);
  }

public:
  /// Initialize the underlying templated declaration and
  /// template parameters.
  void init(NamedDecl *templatedDecl, TemplateParameterList* templateParams) {
    assert(!TemplatedDecl && "TemplatedDecl already set!");
    assert(!TemplateParams && "TemplateParams already set!");
    TemplatedDecl = templatedDecl;
    TemplateParams = templateParams;
  }
};

/// Provides information about a function template specialization,
/// which is a FunctionDecl that has been explicitly specialization or
/// instantiated from a function template.
class FunctionTemplateSpecializationInfo : public llvm::FoldingSetNode {
  FunctionTemplateSpecializationInfo(FunctionDecl *FD,
                                     FunctionTemplateDecl *Template,
                                     TemplateSpecializationKind TSK,
                                     const TemplateArgumentList *TemplateArgs,
                       const ASTTemplateArgumentListInfo *TemplateArgsAsWritten,
                                     SourceLocation POI)
      : Function(FD), Template(Template, TSK - 1),
        TemplateArguments(TemplateArgs),
        TemplateArgumentsAsWritten(TemplateArgsAsWritten),
        PointOfInstantiation(POI) {}

public:
  static FunctionTemplateSpecializationInfo *
  Create(ASTContext &C, FunctionDecl *FD, FunctionTemplateDecl *Template,
         TemplateSpecializationKind TSK,
         const TemplateArgumentList *TemplateArgs,
         const TemplateArgumentListInfo *TemplateArgsAsWritten,
         SourceLocation POI);

  /// The function template specialization that this structure
  /// describes.
  FunctionDecl *Function;

  /// The function template from which this function template
  /// specialization was generated.
  ///
  /// The two bits contain the top 4 values of TemplateSpecializationKind.
  llvm::PointerIntPair<FunctionTemplateDecl *, 2> Template;

  /// The template arguments used to produce the function template
  /// specialization from the function template.
  const TemplateArgumentList *TemplateArguments;

  /// The template arguments as written in the sources, if provided.
  const ASTTemplateArgumentListInfo *TemplateArgumentsAsWritten;

  /// The point at which this function template specialization was
  /// first instantiated.
  SourceLocation PointOfInstantiation;

  /// Retrieve the template from which this function was specialized.
  FunctionTemplateDecl *getTemplate() const { return Template.getPointer(); }

  /// Determine what kind of template specialization this is.
  TemplateSpecializationKind getTemplateSpecializationKind() const {
    return (TemplateSpecializationKind)(Template.getInt() + 1);
  }

  bool isExplicitSpecialization() const {
    return getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
  }

  /// True if this declaration is an explicit specialization,
  /// explicit instantiation declaration, or explicit instantiation
  /// definition.
  bool isExplicitInstantiationOrSpecialization() const {
    return isTemplateExplicitInstantiationOrSpecialization(
        getTemplateSpecializationKind());
  }

  /// Set the template specialization kind.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK) {
    assert(TSK != TSK_Undeclared &&
         "Cannot encode TSK_Undeclared for a function template specialization");
    Template.setInt(TSK - 1);
  }

  /// Retrieve the first point of instantiation of this function
  /// template specialization.
  ///
  /// The point of instantiation may be an invalid source location if this
  /// function has yet to be instantiated.
  SourceLocation getPointOfInstantiation() const {
    return PointOfInstantiation;
  }

  /// Set the (first) point of instantiation of this function template
  /// specialization.
  void setPointOfInstantiation(SourceLocation POI) {
    PointOfInstantiation = POI;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, TemplateArguments->asArray(),
            Function->getASTContext());
  }

  static void
  Profile(llvm::FoldingSetNodeID &ID, ArrayRef<TemplateArgument> TemplateArgs,
          ASTContext &Context) {
    ID.AddInteger(TemplateArgs.size());
    for (const TemplateArgument &TemplateArg : TemplateArgs)
      TemplateArg.Profile(ID, Context);
  }
};

/// Provides information a specialization of a member of a class
/// template, which may be a member function, static data member,
/// member class or member enumeration.
class MemberSpecializationInfo {
  // The member declaration from which this member was instantiated, and the
  // manner in which the instantiation occurred (in the lower two bits).
  llvm::PointerIntPair<NamedDecl *, 2> MemberAndTSK;

  // The point at which this member was first instantiated.
  SourceLocation PointOfInstantiation;

public:
  explicit
  MemberSpecializationInfo(NamedDecl *IF, TemplateSpecializationKind TSK,
                           SourceLocation POI = SourceLocation())
      : MemberAndTSK(IF, TSK - 1), PointOfInstantiation(POI) {
    assert(TSK != TSK_Undeclared &&
           "Cannot encode undeclared template specializations for members");
  }

  /// Retrieve the member declaration from which this member was
  /// instantiated.
  NamedDecl *getInstantiatedFrom() const { return MemberAndTSK.getPointer(); }

  /// Determine what kind of template specialization this is.
  TemplateSpecializationKind getTemplateSpecializationKind() const {
    return (TemplateSpecializationKind)(MemberAndTSK.getInt() + 1);
  }

  bool isExplicitSpecialization() const {
    return getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
  }

  /// Set the template specialization kind.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK) {
    assert(TSK != TSK_Undeclared &&
           "Cannot encode undeclared template specializations for members");
    MemberAndTSK.setInt(TSK - 1);
  }

  /// Retrieve the first point of instantiation of this member.
  /// If the point of instantiation is an invalid location, then this member
  /// has not yet been instantiated.
  SourceLocation getPointOfInstantiation() const {
    return PointOfInstantiation;
  }

  /// Set the first point of instantiation.
  void setPointOfInstantiation(SourceLocation POI) {
    PointOfInstantiation = POI;
  }
};

/// Provides information about a dependent function-template
/// specialization declaration.
///
/// Since explicit function template specialization and instantiation
/// declarations can only appear in namespace scope, and you can only
/// specialize a member of a fully-specialized class, the only way to
/// get one of these is in a friend declaration like the following:
///
/// \code
///   template \<class T> void foo(T);
///   template \<class T> class A {
///     friend void foo<>(T);
///   };
/// \endcode
class DependentFunctionTemplateSpecializationInfo final
    : private llvm::TrailingObjects<DependentFunctionTemplateSpecializationInfo,
                                    TemplateArgumentLoc,
                                    FunctionTemplateDecl *> {
  /// The number of potential template candidates.
  unsigned NumTemplates;

  /// The number of template arguments.
  unsigned NumArgs;

  /// The locations of the left and right angle brackets.
  SourceRange AngleLocs;

  size_t numTrailingObjects(OverloadToken<TemplateArgumentLoc>) const {
    return NumArgs;
  }
  size_t numTrailingObjects(OverloadToken<FunctionTemplateDecl *>) const {
    return NumTemplates;
  }

  DependentFunctionTemplateSpecializationInfo(
                                 const UnresolvedSetImpl &Templates,
                                 const TemplateArgumentListInfo &TemplateArgs);

public:
  friend TrailingObjects;

  static DependentFunctionTemplateSpecializationInfo *
  Create(ASTContext &Context, const UnresolvedSetImpl &Templates,
         const TemplateArgumentListInfo &TemplateArgs);

  /// Returns the number of function templates that this might
  /// be a specialization of.
  unsigned getNumTemplates() const { return NumTemplates; }

  /// Returns the i'th template candidate.
  FunctionTemplateDecl *getTemplate(unsigned I) const {
    assert(I < getNumTemplates() && "template index out of range");
    return getTrailingObjects<FunctionTemplateDecl *>()[I];
  }

  /// Returns the explicit template arguments that were given.
  const TemplateArgumentLoc *getTemplateArgs() const {
    return getTrailingObjects<TemplateArgumentLoc>();
  }

  /// Returns the number of explicit template arguments that were given.
  unsigned getNumTemplateArgs() const { return NumArgs; }

  /// Returns the nth template argument.
  const TemplateArgumentLoc &getTemplateArg(unsigned I) const {
    assert(I < getNumTemplateArgs() && "template arg index out of range");
    return getTemplateArgs()[I];
  }

  SourceLocation getLAngleLoc() const {
    return AngleLocs.getBegin();
  }

  SourceLocation getRAngleLoc() const {
    return AngleLocs.getEnd();
  }
};

/// Declaration of a redeclarable template.
class RedeclarableTemplateDecl : public TemplateDecl,
                                 public Redeclarable<RedeclarableTemplateDecl>
{
  using redeclarable_base = Redeclarable<RedeclarableTemplateDecl>;

  RedeclarableTemplateDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  RedeclarableTemplateDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  RedeclarableTemplateDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

  void anchor() override;
protected:
  template <typename EntryType> struct SpecEntryTraits {
    using DeclType = EntryType;

    static DeclType *getDecl(EntryType *D) {
      return D;
    }

    static ArrayRef<TemplateArgument> getTemplateArgs(EntryType *D) {
      return D->getTemplateArgs().asArray();
    }
  };

  template <typename EntryType, typename SETraits = SpecEntryTraits<EntryType>,
            typename DeclType = typename SETraits::DeclType>
  struct SpecIterator
      : llvm::iterator_adaptor_base<
            SpecIterator<EntryType, SETraits, DeclType>,
            typename llvm::FoldingSetVector<EntryType>::iterator,
            typename std::iterator_traits<typename llvm::FoldingSetVector<
                EntryType>::iterator>::iterator_category,
            DeclType *, ptrdiff_t, DeclType *, DeclType *> {
    SpecIterator() = default;
    explicit SpecIterator(
        typename llvm::FoldingSetVector<EntryType>::iterator SetIter)
        : SpecIterator::iterator_adaptor_base(std::move(SetIter)) {}

    DeclType *operator*() const {
      return SETraits::getDecl(&*this->I)->getMostRecentDecl();
    }

    DeclType *operator->() const { return **this; }
  };

  template <typename EntryType>
  static SpecIterator<EntryType>
  makeSpecIterator(llvm::FoldingSetVector<EntryType> &Specs, bool isEnd) {
    return SpecIterator<EntryType>(isEnd ? Specs.end() : Specs.begin());
  }

  void loadLazySpecializationsImpl() const;

  template <class EntryType> typename SpecEntryTraits<EntryType>::DeclType*
  findSpecializationImpl(llvm::FoldingSetVector<EntryType> &Specs,
                         ArrayRef<TemplateArgument> Args, void *&InsertPos);

  template <class Derived, class EntryType>
  void addSpecializationImpl(llvm::FoldingSetVector<EntryType> &Specs,
                             EntryType *Entry, void *InsertPos);

  struct CommonBase {
    CommonBase() : InstantiatedFromMember(nullptr, false) {}

    /// The template from which this was most
    /// directly instantiated (or null).
    ///
    /// The boolean value indicates whether this template
    /// was explicitly specialized.
    llvm::PointerIntPair<RedeclarableTemplateDecl*, 1, bool>
      InstantiatedFromMember;

    /// If non-null, points to an array of specializations (including
    /// partial specializations) known only by their external declaration IDs.
    ///
    /// The first value in the array is the number of specializations/partial
    /// specializations that follow.
    uint32_t *LazySpecializations = nullptr;
  };

  /// Pointer to the common data shared by all declarations of this
  /// template.
  mutable CommonBase *Common = nullptr;

  /// Retrieves the "common" pointer shared by all (re-)declarations of
  /// the same template. Calling this routine may implicitly allocate memory
  /// for the common pointer.
  CommonBase *getCommonPtr() const;

  virtual CommonBase *newCommon(ASTContext &C) const = 0;

  // Construct a template decl with name, parameters, and templated element.
  RedeclarableTemplateDecl(ConstrainedTemplateDeclInfo *CTDI, Kind DK,
                           ASTContext &C, DeclContext *DC, SourceLocation L,
                           DeclarationName Name, TemplateParameterList *Params,
                           NamedDecl *Decl)
      : TemplateDecl(CTDI, DK, DC, L, Name, Params, Decl), redeclarable_base(C)
        {}

  RedeclarableTemplateDecl(Kind DK, ASTContext &C, DeclContext *DC,
                           SourceLocation L, DeclarationName Name,
                           TemplateParameterList *Params, NamedDecl *Decl)
      : RedeclarableTemplateDecl(nullptr, DK, C, DC, L, Name, Params, Decl) {}

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class ASTReader;
  template <class decl_type> friend class RedeclarableTemplate;

  /// Retrieves the canonical declaration of this template.
  RedeclarableTemplateDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const RedeclarableTemplateDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  /// Determines whether this template was a specialization of a
  /// member template.
  ///
  /// In the following example, the function template \c X<int>::f and the
  /// member template \c X<int>::Inner are member specializations.
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<typename U> void f(T, U);
  ///   template<typename U> struct Inner;
  /// };
  ///
  /// template<> template<typename T>
  /// void X<int>::f(int, T);
  /// template<> template<typename T>
  /// struct X<int>::Inner { /* ... */ };
  /// \endcode
  bool isMemberSpecialization() const {
    return getCommonPtr()->InstantiatedFromMember.getInt();
  }

  /// Note that this member template is a specialization.
  void setMemberSpecialization() {
    assert(getCommonPtr()->InstantiatedFromMember.getPointer() &&
           "Only member templates can be member template specializations");
    getCommonPtr()->InstantiatedFromMember.setInt(true);
  }

  /// Retrieve the member template from which this template was
  /// instantiated, or nullptr if this template was not instantiated from a
  /// member template.
  ///
  /// A template is instantiated from a member template when the member
  /// template itself is part of a class template (or member thereof). For
  /// example, given
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<typename U> void f(T, U);
  /// };
  ///
  /// void test(X<int> x) {
  ///   x.f(1, 'a');
  /// };
  /// \endcode
  ///
  /// \c X<int>::f is a FunctionTemplateDecl that describes the function
  /// template
  ///
  /// \code
  /// template<typename U> void X<int>::f(int, U);
  /// \endcode
  ///
  /// which was itself created during the instantiation of \c X<int>. Calling
  /// getInstantiatedFromMemberTemplate() on this FunctionTemplateDecl will
  /// retrieve the FunctionTemplateDecl for the original template \c f within
  /// the class template \c X<T>, i.e.,
  ///
  /// \code
  /// template<typename T>
  /// template<typename U>
  /// void X<T>::f(T, U);
  /// \endcode
  RedeclarableTemplateDecl *getInstantiatedFromMemberTemplate() const {
    return getCommonPtr()->InstantiatedFromMember.getPointer();
  }

  void setInstantiatedFromMemberTemplate(RedeclarableTemplateDecl *TD) {
    assert(!getCommonPtr()->InstantiatedFromMember.getPointer());
    getCommonPtr()->InstantiatedFromMember.setPointer(TD);
  }

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstRedeclarableTemplate && K <= lastRedeclarableTemplate;
  }
};

template <> struct RedeclarableTemplateDecl::
SpecEntryTraits<FunctionTemplateSpecializationInfo> {
  using DeclType = FunctionDecl;

  static DeclType *getDecl(FunctionTemplateSpecializationInfo *I) {
    return I->Function;
  }

  static ArrayRef<TemplateArgument>
  getTemplateArgs(FunctionTemplateSpecializationInfo *I) {
    return I->TemplateArguments->asArray();
  }
};

/// Declaration of a template function.
class FunctionTemplateDecl : public RedeclarableTemplateDecl {
protected:
  friend class FunctionDecl;

  /// Data that is common to all of the declarations of a given
  /// function template.
  struct Common : CommonBase {
    /// The function template specializations for this function
    /// template, including explicit specializations and instantiations.
    llvm::FoldingSetVector<FunctionTemplateSpecializationInfo> Specializations;

    /// The set of "injected" template arguments used within this
    /// function template.
    ///
    /// This pointer refers to the template arguments (there are as
    /// many template arguments as template parameaters) for the function
    /// template, and is allocated lazily, since most function templates do not
    /// require the use of this information.
    TemplateArgument *InjectedArgs = nullptr;

    Common() = default;
  };

  FunctionTemplateDecl(ASTContext &C, DeclContext *DC, SourceLocation L,
                       DeclarationName Name, TemplateParameterList *Params,
                       NamedDecl *Decl)
      : RedeclarableTemplateDecl(FunctionTemplate, C, DC, L, Name, Params,
                                 Decl) {}

  CommonBase *newCommon(ASTContext &C) const override;

  Common *getCommonPtr() const {
    return static_cast<Common *>(RedeclarableTemplateDecl::getCommonPtr());
  }

  /// Retrieve the set of function template specializations of this
  /// function template.
  llvm::FoldingSetVector<FunctionTemplateSpecializationInfo> &
  getSpecializations() const;

  /// Add a specialization of this function template.
  ///
  /// \param InsertPos Insert position in the FoldingSetVector, must have been
  ///        retrieved by an earlier call to findSpecialization().
  void addSpecialization(FunctionTemplateSpecializationInfo* Info,
                         void *InsertPos);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Load any lazily-loaded specializations from the external source.
  void LoadLazySpecializations() const;

  /// Get the underlying function declaration of the template.
  FunctionDecl *getTemplatedDecl() const {
    return static_cast<FunctionDecl *>(TemplatedDecl);
  }

  /// Returns whether this template declaration defines the primary
  /// pattern.
  bool isThisDeclarationADefinition() const {
    return getTemplatedDecl()->isThisDeclarationADefinition();
  }

  /// Return the specialization with the provided arguments if it exists,
  /// otherwise return the insertion point.
  FunctionDecl *findSpecialization(ArrayRef<TemplateArgument> Args,
                                   void *&InsertPos);

  FunctionTemplateDecl *getCanonicalDecl() override {
    return cast<FunctionTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }
  const FunctionTemplateDecl *getCanonicalDecl() const {
    return cast<FunctionTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }

  /// Retrieve the previous declaration of this function template, or
  /// nullptr if no such declaration exists.
  FunctionTemplateDecl *getPreviousDecl() {
    return cast_or_null<FunctionTemplateDecl>(
             static_cast<RedeclarableTemplateDecl *>(this)->getPreviousDecl());
  }
  const FunctionTemplateDecl *getPreviousDecl() const {
    return cast_or_null<FunctionTemplateDecl>(
       static_cast<const RedeclarableTemplateDecl *>(this)->getPreviousDecl());
  }

  FunctionTemplateDecl *getMostRecentDecl() {
    return cast<FunctionTemplateDecl>(
        static_cast<RedeclarableTemplateDecl *>(this)
            ->getMostRecentDecl());
  }
  const FunctionTemplateDecl *getMostRecentDecl() const {
    return const_cast<FunctionTemplateDecl*>(this)->getMostRecentDecl();
  }

  FunctionTemplateDecl *getInstantiatedFromMemberTemplate() const {
    return cast_or_null<FunctionTemplateDecl>(
             RedeclarableTemplateDecl::getInstantiatedFromMemberTemplate());
  }

  using spec_iterator = SpecIterator<FunctionTemplateSpecializationInfo>;
  using spec_range = llvm::iterator_range<spec_iterator>;

  spec_range specializations() const {
    return spec_range(spec_begin(), spec_end());
  }

  spec_iterator spec_begin() const {
    return makeSpecIterator(getSpecializations(), false);
  }

  spec_iterator spec_end() const {
    return makeSpecIterator(getSpecializations(), true);
  }

  /// Retrieve the "injected" template arguments that correspond to the
  /// template parameters of this function template.
  ///
  /// Although the C++ standard has no notion of the "injected" template
  /// arguments for a function template, the notion is convenient when
  /// we need to perform substitutions inside the definition of a function
  /// template.
  ArrayRef<TemplateArgument> getInjectedTemplateArgs();

  /// Merge \p Prev with our RedeclarableTemplateDecl::Common.
  void mergePrevDecl(FunctionTemplateDecl *Prev);

  /// Create a function template node.
  static FunctionTemplateDecl *Create(ASTContext &C, DeclContext *DC,
                                      SourceLocation L,
                                      DeclarationName Name,
                                      TemplateParameterList *Params,
                                      NamedDecl *Decl);

  /// Create an empty function template node.
  static FunctionTemplateDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  // Implement isa/cast/dyncast support
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == FunctionTemplate; }
};

//===----------------------------------------------------------------------===//
// Kinds of Template Parameters
//===----------------------------------------------------------------------===//

/// Defines the position of a template parameter within a template
/// parameter list.
///
/// Because template parameter can be listed
/// sequentially for out-of-line template members, each template parameter is
/// given a Depth - the nesting of template parameter scopes - and a Position -
/// the occurrence within the parameter list.
/// This class is inheritedly privately by different kinds of template
/// parameters and is not part of the Decl hierarchy. Just a facility.
class TemplateParmPosition {
protected:
  // FIXME: These probably don't need to be ints. int:5 for depth, int:8 for
  // position? Maybe?
  unsigned Depth;
  unsigned Position;

  TemplateParmPosition(unsigned D, unsigned P) : Depth(D), Position(P) {}

public:
  TemplateParmPosition() = delete;

  /// Get the nesting depth of the template parameter.
  unsigned getDepth() const { return Depth; }
  void setDepth(unsigned D) { Depth = D; }

  /// Get the position of the template parameter within its parameter list.
  unsigned getPosition() const { return Position; }
  void setPosition(unsigned P) { Position = P; }

  /// Get the index of the template parameter within its parameter list.
  unsigned getIndex() const { return Position; }
};

/// Declaration of a template type parameter.
///
/// For example, "T" in
/// \code
/// template<typename T> class vector;
/// \endcode
class TemplateTypeParmDecl : public TypeDecl {
  /// Sema creates these on the stack during auto type deduction.
  friend class Sema;

  /// Whether this template type parameter was declaration with
  /// the 'typename' keyword.
  ///
  /// If false, it was declared with the 'class' keyword.
  bool Typename : 1;

  /// The default template argument, if any.
  using DefArgStorage =
      DefaultArgStorage<TemplateTypeParmDecl, TypeSourceInfo *>;
  DefArgStorage DefaultArgument;

  TemplateTypeParmDecl(DeclContext *DC, SourceLocation KeyLoc,
                       SourceLocation IdLoc, IdentifierInfo *Id,
                       bool Typename)
      : TypeDecl(TemplateTypeParm, DC, IdLoc, Id, KeyLoc), Typename(Typename) {}

public:
  static TemplateTypeParmDecl *Create(const ASTContext &C, DeclContext *DC,
                                      SourceLocation KeyLoc,
                                      SourceLocation NameLoc,
                                      unsigned D, unsigned P,
                                      IdentifierInfo *Id, bool Typename,
                                      bool ParameterPack);
  static TemplateTypeParmDecl *CreateDeserialized(const ASTContext &C,
                                                  unsigned ID);

  /// Whether this template type parameter was declared with
  /// the 'typename' keyword.
  ///
  /// If not, it was declared with the 'class' keyword.
  bool wasDeclaredWithTypename() const { return Typename; }

  const DefArgStorage &getDefaultArgStorage() const { return DefaultArgument; }

  /// Determine whether this template parameter has a default
  /// argument.
  bool hasDefaultArgument() const { return DefaultArgument.isSet(); }

  /// Retrieve the default argument, if any.
  QualType getDefaultArgument() const {
    return DefaultArgument.get()->getType();
  }

  /// Retrieves the default argument's source information, if any.
  TypeSourceInfo *getDefaultArgumentInfo() const {
    return DefaultArgument.get();
  }

  /// Retrieves the location of the default argument declaration.
  SourceLocation getDefaultArgumentLoc() const;

  /// Determines whether the default argument was inherited
  /// from a previous declaration of this template.
  bool defaultArgumentWasInherited() const {
    return DefaultArgument.isInherited();
  }

  /// Set the default argument for this template parameter.
  void setDefaultArgument(TypeSourceInfo *DefArg) {
    DefaultArgument.set(DefArg);
  }

  /// Set that this default argument was inherited from another
  /// parameter.
  void setInheritedDefaultArgument(const ASTContext &C,
                                   TemplateTypeParmDecl *Prev) {
    DefaultArgument.setInherited(C, Prev);
  }

  /// Removes the default argument of this template parameter.
  void removeDefaultArgument() {
    DefaultArgument.clear();
  }

  /// Set whether this template type parameter was declared with
  /// the 'typename' or 'class' keyword.
  void setDeclaredWithTypename(bool withTypename) { Typename = withTypename; }

  /// Retrieve the depth of the template parameter.
  unsigned getDepth() const;

  /// Retrieve the index of the template parameter.
  unsigned getIndex() const;

  /// Returns whether this is a parameter pack.
  bool isParameterPack() const;

  SourceRange getSourceRange() const override LLVM_READONLY;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TemplateTypeParm; }
};

/// NonTypeTemplateParmDecl - Declares a non-type template parameter,
/// e.g., "Size" in
/// @code
/// template<int Size> class array { };
/// @endcode
class NonTypeTemplateParmDecl final
    : public DeclaratorDecl,
      protected TemplateParmPosition,
      private llvm::TrailingObjects<NonTypeTemplateParmDecl,
                                    std::pair<QualType, TypeSourceInfo *>> {
  friend class ASTDeclReader;
  friend TrailingObjects;

  /// The default template argument, if any, and whether or not
  /// it was inherited.
  using DefArgStorage = DefaultArgStorage<NonTypeTemplateParmDecl, Expr *>;
  DefArgStorage DefaultArgument;

  // FIXME: Collapse this into TemplateParamPosition; or, just move depth/index
  // down here to save memory.

  /// Whether this non-type template parameter is a parameter pack.
  bool ParameterPack;

  /// Whether this non-type template parameter is an "expanded"
  /// parameter pack, meaning that its type is a pack expansion and we
  /// already know the set of types that expansion expands to.
  bool ExpandedParameterPack = false;

  /// The number of types in an expanded parameter pack.
  unsigned NumExpandedTypes = 0;

  size_t numTrailingObjects(
      OverloadToken<std::pair<QualType, TypeSourceInfo *>>) const {
    return NumExpandedTypes;
  }

  NonTypeTemplateParmDecl(DeclContext *DC, SourceLocation StartLoc,
                          SourceLocation IdLoc, unsigned D, unsigned P,
                          IdentifierInfo *Id, QualType T,
                          bool ParameterPack, TypeSourceInfo *TInfo)
      : DeclaratorDecl(NonTypeTemplateParm, DC, IdLoc, Id, T, TInfo, StartLoc),
        TemplateParmPosition(D, P), ParameterPack(ParameterPack) {}

  NonTypeTemplateParmDecl(DeclContext *DC, SourceLocation StartLoc,
                          SourceLocation IdLoc, unsigned D, unsigned P,
                          IdentifierInfo *Id, QualType T,
                          TypeSourceInfo *TInfo,
                          ArrayRef<QualType> ExpandedTypes,
                          ArrayRef<TypeSourceInfo *> ExpandedTInfos);

public:
  static NonTypeTemplateParmDecl *
  Create(const ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation IdLoc, unsigned D, unsigned P, IdentifierInfo *Id,
         QualType T, bool ParameterPack, TypeSourceInfo *TInfo);

  static NonTypeTemplateParmDecl *
  Create(const ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation IdLoc, unsigned D, unsigned P, IdentifierInfo *Id,
         QualType T, TypeSourceInfo *TInfo, ArrayRef<QualType> ExpandedTypes,
         ArrayRef<TypeSourceInfo *> ExpandedTInfos);

  static NonTypeTemplateParmDecl *CreateDeserialized(ASTContext &C,
                                                     unsigned ID);
  static NonTypeTemplateParmDecl *CreateDeserialized(ASTContext &C,
                                                     unsigned ID,
                                                     unsigned NumExpandedTypes);

  using TemplateParmPosition::getDepth;
  using TemplateParmPosition::setDepth;
  using TemplateParmPosition::getPosition;
  using TemplateParmPosition::setPosition;
  using TemplateParmPosition::getIndex;

  SourceRange getSourceRange() const override LLVM_READONLY;

  const DefArgStorage &getDefaultArgStorage() const { return DefaultArgument; }

  /// Determine whether this template parameter has a default
  /// argument.
  bool hasDefaultArgument() const { return DefaultArgument.isSet(); }

  /// Retrieve the default argument, if any.
  Expr *getDefaultArgument() const { return DefaultArgument.get(); }

  /// Retrieve the location of the default argument, if any.
  SourceLocation getDefaultArgumentLoc() const;

  /// Determines whether the default argument was inherited
  /// from a previous declaration of this template.
  bool defaultArgumentWasInherited() const {
    return DefaultArgument.isInherited();
  }

  /// Set the default argument for this template parameter, and
  /// whether that default argument was inherited from another
  /// declaration.
  void setDefaultArgument(Expr *DefArg) { DefaultArgument.set(DefArg); }
  void setInheritedDefaultArgument(const ASTContext &C,
                                   NonTypeTemplateParmDecl *Parm) {
    DefaultArgument.setInherited(C, Parm);
  }

  /// Removes the default argument of this template parameter.
  void removeDefaultArgument() { DefaultArgument.clear(); }

  /// Whether this parameter is a non-type template parameter pack.
  ///
  /// If the parameter is a parameter pack, the type may be a
  /// \c PackExpansionType. In the following example, the \c Dims parameter
  /// is a parameter pack (whose type is 'unsigned').
  ///
  /// \code
  /// template<typename T, unsigned ...Dims> struct multi_array;
  /// \endcode
  bool isParameterPack() const { return ParameterPack; }

  /// Whether this parameter pack is a pack expansion.
  ///
  /// A non-type template parameter pack is a pack expansion if its type
  /// contains an unexpanded parameter pack. In this case, we will have
  /// built a PackExpansionType wrapping the type.
  bool isPackExpansion() const {
    return ParameterPack && getType()->getAs<PackExpansionType>();
  }

  /// Whether this parameter is a non-type template parameter pack
  /// that has a known list of different types at different positions.
  ///
  /// A parameter pack is an expanded parameter pack when the original
  /// parameter pack's type was itself a pack expansion, and that expansion
  /// has already been expanded. For example, given:
  ///
  /// \code
  /// template<typename ...Types>
  /// struct X {
  ///   template<Types ...Values>
  ///   struct Y { /* ... */ };
  /// };
  /// \endcode
  ///
  /// The parameter pack \c Values has a \c PackExpansionType as its type,
  /// which expands \c Types. When \c Types is supplied with template arguments
  /// by instantiating \c X, the instantiation of \c Values becomes an
  /// expanded parameter pack. For example, instantiating
  /// \c X<int, unsigned int> results in \c Values being an expanded parameter
  /// pack with expansion types \c int and \c unsigned int.
  ///
  /// The \c getExpansionType() and \c getExpansionTypeSourceInfo() functions
  /// return the expansion types.
  bool isExpandedParameterPack() const { return ExpandedParameterPack; }

  /// Retrieves the number of expansion types in an expanded parameter
  /// pack.
  unsigned getNumExpansionTypes() const {
    assert(ExpandedParameterPack && "Not an expansion parameter pack");
    return NumExpandedTypes;
  }

  /// Retrieve a particular expansion type within an expanded parameter
  /// pack.
  QualType getExpansionType(unsigned I) const {
    assert(I < NumExpandedTypes && "Out-of-range expansion type index");
    auto TypesAndInfos =
        getTrailingObjects<std::pair<QualType, TypeSourceInfo *>>();
    return TypesAndInfos[I].first;
  }

  /// Retrieve a particular expansion type source info within an
  /// expanded parameter pack.
  TypeSourceInfo *getExpansionTypeSourceInfo(unsigned I) const {
    assert(I < NumExpandedTypes && "Out-of-range expansion type index");
    auto TypesAndInfos =
        getTrailingObjects<std::pair<QualType, TypeSourceInfo *>>();
    return TypesAndInfos[I].second;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == NonTypeTemplateParm; }
};

/// TemplateTemplateParmDecl - Declares a template template parameter,
/// e.g., "T" in
/// @code
/// template <template <typename> class T> class container { };
/// @endcode
/// A template template parameter is a TemplateDecl because it defines the
/// name of a template and the template parameters allowable for substitution.
class TemplateTemplateParmDecl final
    : public TemplateDecl,
      protected TemplateParmPosition,
      private llvm::TrailingObjects<TemplateTemplateParmDecl,
                                    TemplateParameterList *> {
  /// The default template argument, if any.
  using DefArgStorage =
      DefaultArgStorage<TemplateTemplateParmDecl, TemplateArgumentLoc *>;
  DefArgStorage DefaultArgument;

  /// Whether this parameter is a parameter pack.
  bool ParameterPack;

  /// Whether this template template parameter is an "expanded"
  /// parameter pack, meaning that it is a pack expansion and we
  /// already know the set of template parameters that expansion expands to.
  bool ExpandedParameterPack = false;

  /// The number of parameters in an expanded parameter pack.
  unsigned NumExpandedParams = 0;

  TemplateTemplateParmDecl(DeclContext *DC, SourceLocation L,
                           unsigned D, unsigned P, bool ParameterPack,
                           IdentifierInfo *Id, TemplateParameterList *Params)
      : TemplateDecl(TemplateTemplateParm, DC, L, Id, Params),
        TemplateParmPosition(D, P), ParameterPack(ParameterPack) {}

  TemplateTemplateParmDecl(DeclContext *DC, SourceLocation L,
                           unsigned D, unsigned P,
                           IdentifierInfo *Id, TemplateParameterList *Params,
                           ArrayRef<TemplateParameterList *> Expansions);

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  static TemplateTemplateParmDecl *Create(const ASTContext &C, DeclContext *DC,
                                          SourceLocation L, unsigned D,
                                          unsigned P, bool ParameterPack,
                                          IdentifierInfo *Id,
                                          TemplateParameterList *Params);
  static TemplateTemplateParmDecl *Create(const ASTContext &C, DeclContext *DC,
                                          SourceLocation L, unsigned D,
                                          unsigned P,
                                          IdentifierInfo *Id,
                                          TemplateParameterList *Params,
                                 ArrayRef<TemplateParameterList *> Expansions);

  static TemplateTemplateParmDecl *CreateDeserialized(ASTContext &C,
                                                      unsigned ID);
  static TemplateTemplateParmDecl *CreateDeserialized(ASTContext &C,
                                                      unsigned ID,
                                                      unsigned NumExpansions);

  using TemplateParmPosition::getDepth;
  using TemplateParmPosition::setDepth;
  using TemplateParmPosition::getPosition;
  using TemplateParmPosition::setPosition;
  using TemplateParmPosition::getIndex;

  /// Whether this template template parameter is a template
  /// parameter pack.
  ///
  /// \code
  /// template<template <class T> ...MetaFunctions> struct Apply;
  /// \endcode
  bool isParameterPack() const { return ParameterPack; }

  /// Whether this parameter pack is a pack expansion.
  ///
  /// A template template parameter pack is a pack expansion if its template
  /// parameter list contains an unexpanded parameter pack.
  bool isPackExpansion() const {
    return ParameterPack &&
           getTemplateParameters()->containsUnexpandedParameterPack();
  }

  /// Whether this parameter is a template template parameter pack that
  /// has a known list of different template parameter lists at different
  /// positions.
  ///
  /// A parameter pack is an expanded parameter pack when the original parameter
  /// pack's template parameter list was itself a pack expansion, and that
  /// expansion has already been expanded. For exampe, given:
  ///
  /// \code
  /// template<typename...Types> struct Outer {
  ///   template<template<Types> class...Templates> struct Inner;
  /// };
  /// \endcode
  ///
  /// The parameter pack \c Templates is a pack expansion, which expands the
  /// pack \c Types. When \c Types is supplied with template arguments by
  /// instantiating \c Outer, the instantiation of \c Templates is an expanded
  /// parameter pack.
  bool isExpandedParameterPack() const { return ExpandedParameterPack; }

  /// Retrieves the number of expansion template parameters in
  /// an expanded parameter pack.
  unsigned getNumExpansionTemplateParameters() const {
    assert(ExpandedParameterPack && "Not an expansion parameter pack");
    return NumExpandedParams;
  }

  /// Retrieve a particular expansion type within an expanded parameter
  /// pack.
  TemplateParameterList *getExpansionTemplateParameters(unsigned I) const {
    assert(I < NumExpandedParams && "Out-of-range expansion type index");
    return getTrailingObjects<TemplateParameterList *>()[I];
  }

  const DefArgStorage &getDefaultArgStorage() const { return DefaultArgument; }

  /// Determine whether this template parameter has a default
  /// argument.
  bool hasDefaultArgument() const { return DefaultArgument.isSet(); }

  /// Retrieve the default argument, if any.
  const TemplateArgumentLoc &getDefaultArgument() const {
    static const TemplateArgumentLoc None;
    return DefaultArgument.isSet() ? *DefaultArgument.get() : None;
  }

  /// Retrieve the location of the default argument, if any.
  SourceLocation getDefaultArgumentLoc() const;

  /// Determines whether the default argument was inherited
  /// from a previous declaration of this template.
  bool defaultArgumentWasInherited() const {
    return DefaultArgument.isInherited();
  }

  /// Set the default argument for this template parameter, and
  /// whether that default argument was inherited from another
  /// declaration.
  void setDefaultArgument(const ASTContext &C,
                          const TemplateArgumentLoc &DefArg);
  void setInheritedDefaultArgument(const ASTContext &C,
                                   TemplateTemplateParmDecl *Prev) {
    DefaultArgument.setInherited(C, Prev);
  }

  /// Removes the default argument of this template parameter.
  void removeDefaultArgument() { DefaultArgument.clear(); }

  SourceRange getSourceRange() const override LLVM_READONLY {
    SourceLocation End = getLocation();
    if (hasDefaultArgument() && !defaultArgumentWasInherited())
      End = getDefaultArgument().getSourceRange().getEnd();
    return SourceRange(getTemplateParameters()->getTemplateLoc(), End);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TemplateTemplateParm; }
};

/// Represents the builtin template declaration which is used to
/// implement __make_integer_seq and other builtin templates.  It serves
/// no real purpose beyond existing as a place to hold template parameters.
class BuiltinTemplateDecl : public TemplateDecl {
  BuiltinTemplateKind BTK;

  BuiltinTemplateDecl(const ASTContext &C, DeclContext *DC,
                      DeclarationName Name, BuiltinTemplateKind BTK);

  void anchor() override;

public:
  // Implement isa/cast/dyncast support
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == BuiltinTemplate; }

  static BuiltinTemplateDecl *Create(const ASTContext &C, DeclContext *DC,
                                     DeclarationName Name,
                                     BuiltinTemplateKind BTK) {
    return new (C, DC) BuiltinTemplateDecl(C, DC, Name, BTK);
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return {};
  }

  BuiltinTemplateKind getBuiltinTemplateKind() const { return BTK; }
};

/// Represents a class template specialization, which refers to
/// a class template with a given set of template arguments.
///
/// Class template specializations represent both explicit
/// specialization of class templates, as in the example below, and
/// implicit instantiations of class templates.
///
/// \code
/// template<typename T> class array;
///
/// template<>
/// class array<bool> { }; // class template specialization array<bool>
/// \endcode
class ClassTemplateSpecializationDecl
  : public CXXRecordDecl, public llvm::FoldingSetNode {
  /// Structure that stores information about a class template
  /// specialization that was instantiated from a class template partial
  /// specialization.
  struct SpecializedPartialSpecialization {
    /// The class template partial specialization from which this
    /// class template specialization was instantiated.
    ClassTemplatePartialSpecializationDecl *PartialSpecialization;

    /// The template argument list deduced for the class template
    /// partial specialization itself.
    const TemplateArgumentList *TemplateArgs;
  };

  /// The template that this specialization specializes
  llvm::PointerUnion<ClassTemplateDecl *, SpecializedPartialSpecialization *>
    SpecializedTemplate;

  /// Further info for explicit template specialization/instantiation.
  struct ExplicitSpecializationInfo {
    /// The type-as-written.
    TypeSourceInfo *TypeAsWritten = nullptr;

    /// The location of the extern keyword.
    SourceLocation ExternLoc;

    /// The location of the template keyword.
    SourceLocation TemplateKeywordLoc;

    ExplicitSpecializationInfo() = default;
  };

  /// Further info for explicit template specialization/instantiation.
  /// Does not apply to implicit specializations.
  ExplicitSpecializationInfo *ExplicitInfo = nullptr;

  /// The template arguments used to describe this specialization.
  const TemplateArgumentList *TemplateArgs;

  /// The point where this template was instantiated (if any)
  SourceLocation PointOfInstantiation;

  /// The kind of specialization this declaration refers to.
  /// Really a value of type TemplateSpecializationKind.
  unsigned SpecializationKind : 3;

protected:
  ClassTemplateSpecializationDecl(ASTContext &Context, Kind DK, TagKind TK,
                                  DeclContext *DC, SourceLocation StartLoc,
                                  SourceLocation IdLoc,
                                  ClassTemplateDecl *SpecializedTemplate,
                                  ArrayRef<TemplateArgument> Args,
                                  ClassTemplateSpecializationDecl *PrevDecl);

  explicit ClassTemplateSpecializationDecl(ASTContext &C, Kind DK);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ClassTemplateSpecializationDecl *
  Create(ASTContext &Context, TagKind TK, DeclContext *DC,
         SourceLocation StartLoc, SourceLocation IdLoc,
         ClassTemplateDecl *SpecializedTemplate,
         ArrayRef<TemplateArgument> Args,
         ClassTemplateSpecializationDecl *PrevDecl);
  static ClassTemplateSpecializationDecl *
  CreateDeserialized(ASTContext &C, unsigned ID);

  void getNameForDiagnostic(raw_ostream &OS, const PrintingPolicy &Policy,
                            bool Qualified) const override;

  // FIXME: This is broken. CXXRecordDecl::getMostRecentDecl() returns a
  // different "most recent" declaration from this function for the same
  // declaration, because we don't override getMostRecentDeclImpl(). But
  // it's not clear that we should override that, because the most recent
  // declaration as a CXXRecordDecl sometimes is the injected-class-name.
  ClassTemplateSpecializationDecl *getMostRecentDecl() {
    return cast<ClassTemplateSpecializationDecl>(
        getMostRecentNonInjectedDecl());
  }

  /// Retrieve the template that this specialization specializes.
  ClassTemplateDecl *getSpecializedTemplate() const;

  /// Retrieve the template arguments of the class template
  /// specialization.
  const TemplateArgumentList &getTemplateArgs() const {
    return *TemplateArgs;
  }

  /// Determine the kind of specialization that this
  /// declaration represents.
  TemplateSpecializationKind getSpecializationKind() const {
    return static_cast<TemplateSpecializationKind>(SpecializationKind);
  }

  bool isExplicitSpecialization() const {
    return getSpecializationKind() == TSK_ExplicitSpecialization;
  }

  /// True if this declaration is an explicit specialization,
  /// explicit instantiation declaration, or explicit instantiation
  /// definition.
  bool isExplicitInstantiationOrSpecialization() const {
    return isTemplateExplicitInstantiationOrSpecialization(
        getTemplateSpecializationKind());
  }

  void setSpecializationKind(TemplateSpecializationKind TSK) {
    SpecializationKind = TSK;
  }

  /// Get the point of instantiation (if any), or null if none.
  SourceLocation getPointOfInstantiation() const {
    return PointOfInstantiation;
  }

  void setPointOfInstantiation(SourceLocation Loc) {
    assert(Loc.isValid() && "point of instantiation must be valid!");
    PointOfInstantiation = Loc;
  }

  /// If this class template specialization is an instantiation of
  /// a template (rather than an explicit specialization), return the
  /// class template or class template partial specialization from which it
  /// was instantiated.
  llvm::PointerUnion<ClassTemplateDecl *,
                     ClassTemplatePartialSpecializationDecl *>
  getInstantiatedFrom() const {
    if (!isTemplateInstantiation(getSpecializationKind()))
      return llvm::PointerUnion<ClassTemplateDecl *,
                                ClassTemplatePartialSpecializationDecl *>();

    return getSpecializedTemplateOrPartial();
  }

  /// Retrieve the class template or class template partial
  /// specialization which was specialized by this.
  llvm::PointerUnion<ClassTemplateDecl *,
                     ClassTemplatePartialSpecializationDecl *>
  getSpecializedTemplateOrPartial() const {
    if (const auto *PartialSpec =
            SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization *>())
      return PartialSpec->PartialSpecialization;

    return SpecializedTemplate.get<ClassTemplateDecl*>();
  }

  /// Retrieve the set of template arguments that should be used
  /// to instantiate members of the class template or class template partial
  /// specialization from which this class template specialization was
  /// instantiated.
  ///
  /// \returns For a class template specialization instantiated from the primary
  /// template, this function will return the same template arguments as
  /// getTemplateArgs(). For a class template specialization instantiated from
  /// a class template partial specialization, this function will return the
  /// deduced template arguments for the class template partial specialization
  /// itself.
  const TemplateArgumentList &getTemplateInstantiationArgs() const {
    if (const auto *PartialSpec =
            SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization *>())
      return *PartialSpec->TemplateArgs;

    return getTemplateArgs();
  }

  /// Note that this class template specialization is actually an
  /// instantiation of the given class template partial specialization whose
  /// template arguments have been deduced.
  void setInstantiationOf(ClassTemplatePartialSpecializationDecl *PartialSpec,
                          const TemplateArgumentList *TemplateArgs) {
    assert(!SpecializedTemplate.is<SpecializedPartialSpecialization*>() &&
           "Already set to a class template partial specialization!");
    auto *PS = new (getASTContext()) SpecializedPartialSpecialization();
    PS->PartialSpecialization = PartialSpec;
    PS->TemplateArgs = TemplateArgs;
    SpecializedTemplate = PS;
  }

  /// Note that this class template specialization is an instantiation
  /// of the given class template.
  void setInstantiationOf(ClassTemplateDecl *TemplDecl) {
    assert(!SpecializedTemplate.is<SpecializedPartialSpecialization*>() &&
           "Previously set to a class template partial specialization!");
    SpecializedTemplate = TemplDecl;
  }

  /// Sets the type of this specialization as it was written by
  /// the user. This will be a class template specialization type.
  void setTypeAsWritten(TypeSourceInfo *T) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->TypeAsWritten = T;
  }

  /// Gets the type of this specialization as it was written by
  /// the user, if it was so written.
  TypeSourceInfo *getTypeAsWritten() const {
    return ExplicitInfo ? ExplicitInfo->TypeAsWritten : nullptr;
  }

  /// Gets the location of the extern keyword, if present.
  SourceLocation getExternLoc() const {
    return ExplicitInfo ? ExplicitInfo->ExternLoc : SourceLocation();
  }

  /// Sets the location of the extern keyword.
  void setExternLoc(SourceLocation Loc) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->ExternLoc = Loc;
  }

  /// Sets the location of the template keyword.
  void setTemplateKeywordLoc(SourceLocation Loc) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->TemplateKeywordLoc = Loc;
  }

  /// Gets the location of the template keyword, if present.
  SourceLocation getTemplateKeywordLoc() const {
    return ExplicitInfo ? ExplicitInfo->TemplateKeywordLoc : SourceLocation();
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, TemplateArgs->asArray(), getASTContext());
  }

  static void
  Profile(llvm::FoldingSetNodeID &ID, ArrayRef<TemplateArgument> TemplateArgs,
          ASTContext &Context) {
    ID.AddInteger(TemplateArgs.size());
    for (const TemplateArgument &TemplateArg : TemplateArgs)
      TemplateArg.Profile(ID, Context);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstClassTemplateSpecialization &&
           K <= lastClassTemplateSpecialization;
  }
};

class ClassTemplatePartialSpecializationDecl
  : public ClassTemplateSpecializationDecl {
  /// The list of template parameters
  TemplateParameterList* TemplateParams = nullptr;

  /// The source info for the template arguments as written.
  /// FIXME: redundant with TypeAsWritten?
  const ASTTemplateArgumentListInfo *ArgsAsWritten = nullptr;

  /// The class template partial specialization from which this
  /// class template partial specialization was instantiated.
  ///
  /// The boolean value will be true to indicate that this class template
  /// partial specialization was specialized at this level.
  llvm::PointerIntPair<ClassTemplatePartialSpecializationDecl *, 1, bool>
      InstantiatedFromMember;

  ClassTemplatePartialSpecializationDecl(ASTContext &Context, TagKind TK,
                                         DeclContext *DC,
                                         SourceLocation StartLoc,
                                         SourceLocation IdLoc,
                                         TemplateParameterList *Params,
                                         ClassTemplateDecl *SpecializedTemplate,
                                         ArrayRef<TemplateArgument> Args,
                               const ASTTemplateArgumentListInfo *ArgsAsWritten,
                               ClassTemplatePartialSpecializationDecl *PrevDecl);

  ClassTemplatePartialSpecializationDecl(ASTContext &C)
    : ClassTemplateSpecializationDecl(C, ClassTemplatePartialSpecialization),
      InstantiatedFromMember(nullptr, false) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ClassTemplatePartialSpecializationDecl *
  Create(ASTContext &Context, TagKind TK, DeclContext *DC,
         SourceLocation StartLoc, SourceLocation IdLoc,
         TemplateParameterList *Params,
         ClassTemplateDecl *SpecializedTemplate,
         ArrayRef<TemplateArgument> Args,
         const TemplateArgumentListInfo &ArgInfos,
         QualType CanonInjectedType,
         ClassTemplatePartialSpecializationDecl *PrevDecl);

  static ClassTemplatePartialSpecializationDecl *
  CreateDeserialized(ASTContext &C, unsigned ID);

  ClassTemplatePartialSpecializationDecl *getMostRecentDecl() {
    return cast<ClassTemplatePartialSpecializationDecl>(
             static_cast<ClassTemplateSpecializationDecl *>(
               this)->getMostRecentDecl());
  }

  /// Get the list of template parameters
  TemplateParameterList *getTemplateParameters() const {
    return TemplateParams;
  }

  /// Get the template arguments as written.
  const ASTTemplateArgumentListInfo *getTemplateArgsAsWritten() const {
    return ArgsAsWritten;
  }

  /// Retrieve the member class template partial specialization from
  /// which this particular class template partial specialization was
  /// instantiated.
  ///
  /// \code
  /// template<typename T>
  /// struct Outer {
  ///   template<typename U> struct Inner;
  ///   template<typename U> struct Inner<U*> { }; // #1
  /// };
  ///
  /// Outer<float>::Inner<int*> ii;
  /// \endcode
  ///
  /// In this example, the instantiation of \c Outer<float>::Inner<int*> will
  /// end up instantiating the partial specialization
  /// \c Outer<float>::Inner<U*>, which itself was instantiated from the class
  /// template partial specialization \c Outer<T>::Inner<U*>. Given
  /// \c Outer<float>::Inner<U*>, this function would return
  /// \c Outer<T>::Inner<U*>.
  ClassTemplatePartialSpecializationDecl *getInstantiatedFromMember() const {
    const auto *First =
        cast<ClassTemplatePartialSpecializationDecl>(getFirstDecl());
    return First->InstantiatedFromMember.getPointer();
  }
  ClassTemplatePartialSpecializationDecl *
  getInstantiatedFromMemberTemplate() const {
    return getInstantiatedFromMember();
  }

  void setInstantiatedFromMember(
                          ClassTemplatePartialSpecializationDecl *PartialSpec) {
    auto *First = cast<ClassTemplatePartialSpecializationDecl>(getFirstDecl());
    First->InstantiatedFromMember.setPointer(PartialSpec);
  }

  /// Determines whether this class template partial specialization
  /// template was a specialization of a member partial specialization.
  ///
  /// In the following example, the member template partial specialization
  /// \c X<int>::Inner<T*> is a member specialization.
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<typename U> struct Inner;
  ///   template<typename U> struct Inner<U*>;
  /// };
  ///
  /// template<> template<typename T>
  /// struct X<int>::Inner<T*> { /* ... */ };
  /// \endcode
  bool isMemberSpecialization() {
    const auto *First =
        cast<ClassTemplatePartialSpecializationDecl>(getFirstDecl());
    return First->InstantiatedFromMember.getInt();
  }

  /// Note that this member template is a specialization.
  void setMemberSpecialization() {
    auto *First = cast<ClassTemplatePartialSpecializationDecl>(getFirstDecl());
    assert(First->InstantiatedFromMember.getPointer() &&
           "Only member templates can be member template specializations");
    return First->InstantiatedFromMember.setInt(true);
  }

  /// Retrieves the injected specialization type for this partial
  /// specialization.  This is not the same as the type-decl-type for
  /// this partial specialization, which is an InjectedClassNameType.
  QualType getInjectedSpecializationType() const {
    assert(getTypeForDecl() && "partial specialization has no type set!");
    return cast<InjectedClassNameType>(getTypeForDecl())
             ->getInjectedSpecializationType();
  }

  // FIXME: Add Profile support!

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K == ClassTemplatePartialSpecialization;
  }
};

/// Declaration of a class template.
class ClassTemplateDecl : public RedeclarableTemplateDecl {
protected:
  /// Data that is common to all of the declarations of a given
  /// class template.
  struct Common : CommonBase {
    /// The class template specializations for this class
    /// template, including explicit specializations and instantiations.
    llvm::FoldingSetVector<ClassTemplateSpecializationDecl> Specializations;

    /// The class template partial specializations for this class
    /// template.
    llvm::FoldingSetVector<ClassTemplatePartialSpecializationDecl>
      PartialSpecializations;

    /// The injected-class-name type for this class template.
    QualType InjectedClassNameType;

    Common() = default;
  };

  /// Retrieve the set of specializations of this class template.
  llvm::FoldingSetVector<ClassTemplateSpecializationDecl> &
  getSpecializations() const;

  /// Retrieve the set of partial specializations of this class
  /// template.
  llvm::FoldingSetVector<ClassTemplatePartialSpecializationDecl> &
  getPartialSpecializations();

  ClassTemplateDecl(ConstrainedTemplateDeclInfo *CTDI, ASTContext &C,
                    DeclContext *DC, SourceLocation L, DeclarationName Name,
                    TemplateParameterList *Params, NamedDecl *Decl)
      : RedeclarableTemplateDecl(CTDI, ClassTemplate, C, DC, L, Name, Params,
                                 Decl) {}

  ClassTemplateDecl(ASTContext &C, DeclContext *DC, SourceLocation L,
                    DeclarationName Name, TemplateParameterList *Params,
                    NamedDecl *Decl)
      : ClassTemplateDecl(nullptr, C, DC, L, Name, Params, Decl) {}

  CommonBase *newCommon(ASTContext &C) const override;

  Common *getCommonPtr() const {
    return static_cast<Common *>(RedeclarableTemplateDecl::getCommonPtr());
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Load any lazily-loaded specializations from the external source.
  void LoadLazySpecializations() const;

  /// Get the underlying class declarations of the template.
  CXXRecordDecl *getTemplatedDecl() const {
    return static_cast<CXXRecordDecl *>(TemplatedDecl);
  }

  /// Returns whether this template declaration defines the primary
  /// class pattern.
  bool isThisDeclarationADefinition() const {
    return getTemplatedDecl()->isThisDeclarationADefinition();
  }

  // FIXME: remove default argument for AssociatedConstraints
  /// Create a class template node.
  static ClassTemplateDecl *Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation L,
                                   DeclarationName Name,
                                   TemplateParameterList *Params,
                                   NamedDecl *Decl,
                                   Expr *AssociatedConstraints = nullptr);

  /// Create an empty class template node.
  static ClassTemplateDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Return the specialization with the provided arguments if it exists,
  /// otherwise return the insertion point.
  ClassTemplateSpecializationDecl *
  findSpecialization(ArrayRef<TemplateArgument> Args, void *&InsertPos);

  /// Insert the specified specialization knowing that it is not already
  /// in. InsertPos must be obtained from findSpecialization.
  void AddSpecialization(ClassTemplateSpecializationDecl *D, void *InsertPos);

  ClassTemplateDecl *getCanonicalDecl() override {
    return cast<ClassTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }
  const ClassTemplateDecl *getCanonicalDecl() const {
    return cast<ClassTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }

  /// Retrieve the previous declaration of this class template, or
  /// nullptr if no such declaration exists.
  ClassTemplateDecl *getPreviousDecl() {
    return cast_or_null<ClassTemplateDecl>(
             static_cast<RedeclarableTemplateDecl *>(this)->getPreviousDecl());
  }
  const ClassTemplateDecl *getPreviousDecl() const {
    return cast_or_null<ClassTemplateDecl>(
             static_cast<const RedeclarableTemplateDecl *>(
               this)->getPreviousDecl());
  }

  ClassTemplateDecl *getMostRecentDecl() {
    return cast<ClassTemplateDecl>(
        static_cast<RedeclarableTemplateDecl *>(this)->getMostRecentDecl());
  }
  const ClassTemplateDecl *getMostRecentDecl() const {
    return const_cast<ClassTemplateDecl*>(this)->getMostRecentDecl();
  }

  ClassTemplateDecl *getInstantiatedFromMemberTemplate() const {
    return cast_or_null<ClassTemplateDecl>(
             RedeclarableTemplateDecl::getInstantiatedFromMemberTemplate());
  }

  /// Return the partial specialization with the provided arguments if it
  /// exists, otherwise return the insertion point.
  ClassTemplatePartialSpecializationDecl *
  findPartialSpecialization(ArrayRef<TemplateArgument> Args, void *&InsertPos);

  /// Insert the specified partial specialization knowing that it is not
  /// already in. InsertPos must be obtained from findPartialSpecialization.
  void AddPartialSpecialization(ClassTemplatePartialSpecializationDecl *D,
                                void *InsertPos);

  /// Retrieve the partial specializations as an ordered list.
  void getPartialSpecializations(
          SmallVectorImpl<ClassTemplatePartialSpecializationDecl *> &PS);

  /// Find a class template partial specialization with the given
  /// type T.
  ///
  /// \param T a dependent type that names a specialization of this class
  /// template.
  ///
  /// \returns the class template partial specialization that exactly matches
  /// the type \p T, or nullptr if no such partial specialization exists.
  ClassTemplatePartialSpecializationDecl *findPartialSpecialization(QualType T);

  /// Find a class template partial specialization which was instantiated
  /// from the given member partial specialization.
  ///
  /// \param D a member class template partial specialization.
  ///
  /// \returns the class template partial specialization which was instantiated
  /// from the given member partial specialization, or nullptr if no such
  /// partial specialization exists.
  ClassTemplatePartialSpecializationDecl *
  findPartialSpecInstantiatedFromMember(
                                     ClassTemplatePartialSpecializationDecl *D);

  /// Retrieve the template specialization type of the
  /// injected-class-name for this class template.
  ///
  /// The injected-class-name for a class template \c X is \c
  /// X<template-args>, where \c template-args is formed from the
  /// template arguments that correspond to the template parameters of
  /// \c X. For example:
  ///
  /// \code
  /// template<typename T, int N>
  /// struct array {
  ///   typedef array this_type; // "array" is equivalent to "array<T, N>"
  /// };
  /// \endcode
  QualType getInjectedClassNameSpecialization();

  using spec_iterator = SpecIterator<ClassTemplateSpecializationDecl>;
  using spec_range = llvm::iterator_range<spec_iterator>;

  spec_range specializations() const {
    return spec_range(spec_begin(), spec_end());
  }

  spec_iterator spec_begin() const {
    return makeSpecIterator(getSpecializations(), false);
  }

  spec_iterator spec_end() const {
    return makeSpecIterator(getSpecializations(), true);
  }

  // Implement isa/cast/dyncast support
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ClassTemplate; }
};

/// Declaration of a friend template.
///
/// For example:
/// \code
/// template \<typename T> class A {
///   friend class MyVector<T>; // not a friend template
///   template \<typename U> friend class B; // not a friend template
///   template \<typename U> friend class Foo<T>::Nested; // friend template
/// };
/// \endcode
///
/// \note This class is not currently in use.  All of the above
/// will yield a FriendDecl, not a FriendTemplateDecl.
class FriendTemplateDecl : public Decl {
  virtual void anchor();

public:
  using FriendUnion = llvm::PointerUnion<NamedDecl *,TypeSourceInfo *>;

private:
  // The number of template parameters;  always non-zero.
  unsigned NumParams = 0;

  // The parameter list.
  TemplateParameterList **Params = nullptr;

  // The declaration that's a friend of this class.
  FriendUnion Friend;

  // Location of the 'friend' specifier.
  SourceLocation FriendLoc;

  FriendTemplateDecl(DeclContext *DC, SourceLocation Loc,
                     MutableArrayRef<TemplateParameterList *> Params,
                     FriendUnion Friend, SourceLocation FriendLoc)
      : Decl(Decl::FriendTemplate, DC, Loc), NumParams(Params.size()),
        Params(Params.data()), Friend(Friend), FriendLoc(FriendLoc) {}

  FriendTemplateDecl(EmptyShell Empty) : Decl(Decl::FriendTemplate, Empty) {}

public:
  friend class ASTDeclReader;

  static FriendTemplateDecl *
  Create(ASTContext &Context, DeclContext *DC, SourceLocation Loc,
         MutableArrayRef<TemplateParameterList *> Params, FriendUnion Friend,
         SourceLocation FriendLoc);

  static FriendTemplateDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// If this friend declaration names a templated type (or
  /// a dependent member type of a templated type), return that
  /// type;  otherwise return null.
  TypeSourceInfo *getFriendType() const {
    return Friend.dyn_cast<TypeSourceInfo*>();
  }

  /// If this friend declaration names a templated function (or
  /// a member function of a templated type), return that type;
  /// otherwise return null.
  NamedDecl *getFriendDecl() const {
    return Friend.dyn_cast<NamedDecl*>();
  }

  /// Retrieves the location of the 'friend' keyword.
  SourceLocation getFriendLoc() const {
    return FriendLoc;
  }

  TemplateParameterList *getTemplateParameterList(unsigned i) const {
    assert(i <= NumParams);
    return Params[i];
  }

  unsigned getNumTemplateParameters() const {
    return NumParams;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::FriendTemplate; }
};

/// Declaration of an alias template.
///
/// For example:
/// \code
/// template \<typename T> using V = std::map<T*, int, MyCompare<T>>;
/// \endcode
class TypeAliasTemplateDecl : public RedeclarableTemplateDecl {
protected:
  using Common = CommonBase;

  TypeAliasTemplateDecl(ASTContext &C, DeclContext *DC, SourceLocation L,
                        DeclarationName Name, TemplateParameterList *Params,
                        NamedDecl *Decl)
      : RedeclarableTemplateDecl(TypeAliasTemplate, C, DC, L, Name, Params,
                                 Decl) {}

  CommonBase *newCommon(ASTContext &C) const override;

  Common *getCommonPtr() {
    return static_cast<Common *>(RedeclarableTemplateDecl::getCommonPtr());
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Get the underlying function declaration of the template.
  TypeAliasDecl *getTemplatedDecl() const {
    return static_cast<TypeAliasDecl *>(TemplatedDecl);
  }


  TypeAliasTemplateDecl *getCanonicalDecl() override {
    return cast<TypeAliasTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }
  const TypeAliasTemplateDecl *getCanonicalDecl() const {
    return cast<TypeAliasTemplateDecl>(
             RedeclarableTemplateDecl::getCanonicalDecl());
  }

  /// Retrieve the previous declaration of this function template, or
  /// nullptr if no such declaration exists.
  TypeAliasTemplateDecl *getPreviousDecl() {
    return cast_or_null<TypeAliasTemplateDecl>(
             static_cast<RedeclarableTemplateDecl *>(this)->getPreviousDecl());
  }
  const TypeAliasTemplateDecl *getPreviousDecl() const {
    return cast_or_null<TypeAliasTemplateDecl>(
             static_cast<const RedeclarableTemplateDecl *>(
               this)->getPreviousDecl());
  }

  TypeAliasTemplateDecl *getInstantiatedFromMemberTemplate() const {
    return cast_or_null<TypeAliasTemplateDecl>(
             RedeclarableTemplateDecl::getInstantiatedFromMemberTemplate());
  }

  /// Create a function template node.
  static TypeAliasTemplateDecl *Create(ASTContext &C, DeclContext *DC,
                                       SourceLocation L,
                                       DeclarationName Name,
                                       TemplateParameterList *Params,
                                       NamedDecl *Decl);

  /// Create an empty alias template node.
  static TypeAliasTemplateDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  // Implement isa/cast/dyncast support
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TypeAliasTemplate; }
};

/// Declaration of a function specialization at template class scope.
///
/// This is a non-standard extension needed to support MSVC.
///
/// For example:
/// \code
/// template <class T>
/// class A {
///    template <class U> void foo(U a) { }
///    template<> void foo(int a) { }
/// }
/// \endcode
///
/// "template<> foo(int a)" will be saved in Specialization as a normal
/// CXXMethodDecl. Then during an instantiation of class A, it will be
/// transformed into an actual function specialization.
class ClassScopeFunctionSpecializationDecl : public Decl {
  CXXMethodDecl *Specialization;
  bool HasExplicitTemplateArgs;
  TemplateArgumentListInfo TemplateArgs;

  ClassScopeFunctionSpecializationDecl(DeclContext *DC, SourceLocation Loc,
                                       CXXMethodDecl *FD, bool Args,
                                       TemplateArgumentListInfo TemplArgs)
      : Decl(Decl::ClassScopeFunctionSpecialization, DC, Loc),
        Specialization(FD), HasExplicitTemplateArgs(Args),
        TemplateArgs(std::move(TemplArgs)) {}

  ClassScopeFunctionSpecializationDecl(EmptyShell Empty)
      : Decl(Decl::ClassScopeFunctionSpecialization, Empty) {}

  virtual void anchor();

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  CXXMethodDecl *getSpecialization() const { return Specialization; }
  bool hasExplicitTemplateArgs() const { return HasExplicitTemplateArgs; }
  const TemplateArgumentListInfo& templateArgs() const { return TemplateArgs; }

  static ClassScopeFunctionSpecializationDecl *Create(ASTContext &C,
                                                      DeclContext *DC,
                                                      SourceLocation Loc,
                                                      CXXMethodDecl *FD,
                                                   bool HasExplicitTemplateArgs,
                                        TemplateArgumentListInfo TemplateArgs) {
    return new (C, DC) ClassScopeFunctionSpecializationDecl(
        DC, Loc, FD, HasExplicitTemplateArgs, std::move(TemplateArgs));
  }

  static ClassScopeFunctionSpecializationDecl *
  CreateDeserialized(ASTContext &Context, unsigned ID);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K == Decl::ClassScopeFunctionSpecialization;
  }
};

/// Implementation of inline functions that require the template declarations
inline AnyFunctionDecl::AnyFunctionDecl(FunctionTemplateDecl *FTD)
    : Function(FTD) {}

/// Represents a variable template specialization, which refers to
/// a variable template with a given set of template arguments.
///
/// Variable template specializations represent both explicit
/// specializations of variable templates, as in the example below, and
/// implicit instantiations of variable templates.
///
/// \code
/// template<typename T> constexpr T pi = T(3.1415926535897932385);
///
/// template<>
/// constexpr float pi<float>; // variable template specialization pi<float>
/// \endcode
class VarTemplateSpecializationDecl : public VarDecl,
                                      public llvm::FoldingSetNode {

  /// Structure that stores information about a variable template
  /// specialization that was instantiated from a variable template partial
  /// specialization.
  struct SpecializedPartialSpecialization {
    /// The variable template partial specialization from which this
    /// variable template specialization was instantiated.
    VarTemplatePartialSpecializationDecl *PartialSpecialization;

    /// The template argument list deduced for the variable template
    /// partial specialization itself.
    const TemplateArgumentList *TemplateArgs;
  };

  /// The template that this specialization specializes.
  llvm::PointerUnion<VarTemplateDecl *, SpecializedPartialSpecialization *>
  SpecializedTemplate;

  /// Further info for explicit template specialization/instantiation.
  struct ExplicitSpecializationInfo {
    /// The type-as-written.
    TypeSourceInfo *TypeAsWritten = nullptr;

    /// The location of the extern keyword.
    SourceLocation ExternLoc;

    /// The location of the template keyword.
    SourceLocation TemplateKeywordLoc;

    ExplicitSpecializationInfo() = default;
  };

  /// Further info for explicit template specialization/instantiation.
  /// Does not apply to implicit specializations.
  ExplicitSpecializationInfo *ExplicitInfo = nullptr;

  /// The template arguments used to describe this specialization.
  const TemplateArgumentList *TemplateArgs;
  TemplateArgumentListInfo TemplateArgsInfo;

  /// The point where this template was instantiated (if any).
  SourceLocation PointOfInstantiation;

  /// The kind of specialization this declaration refers to.
  /// Really a value of type TemplateSpecializationKind.
  unsigned SpecializationKind : 3;

  /// Whether this declaration is a complete definition of the
  /// variable template specialization. We can't otherwise tell apart
  /// an instantiated declaration from an instantiated definition with
  /// no initializer.
  unsigned IsCompleteDefinition : 1;

protected:
  VarTemplateSpecializationDecl(Kind DK, ASTContext &Context, DeclContext *DC,
                                SourceLocation StartLoc, SourceLocation IdLoc,
                                VarTemplateDecl *SpecializedTemplate,
                                QualType T, TypeSourceInfo *TInfo,
                                StorageClass S,
                                ArrayRef<TemplateArgument> Args);

  explicit VarTemplateSpecializationDecl(Kind DK, ASTContext &Context);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class VarDecl;

  static VarTemplateSpecializationDecl *
  Create(ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation IdLoc, VarTemplateDecl *SpecializedTemplate, QualType T,
         TypeSourceInfo *TInfo, StorageClass S,
         ArrayRef<TemplateArgument> Args);
  static VarTemplateSpecializationDecl *CreateDeserialized(ASTContext &C,
                                                           unsigned ID);

  void getNameForDiagnostic(raw_ostream &OS, const PrintingPolicy &Policy,
                            bool Qualified) const override;

  VarTemplateSpecializationDecl *getMostRecentDecl() {
    VarDecl *Recent = static_cast<VarDecl *>(this)->getMostRecentDecl();
    return cast<VarTemplateSpecializationDecl>(Recent);
  }

  /// Retrieve the template that this specialization specializes.
  VarTemplateDecl *getSpecializedTemplate() const;

  /// Retrieve the template arguments of the variable template
  /// specialization.
  const TemplateArgumentList &getTemplateArgs() const { return *TemplateArgs; }

  // TODO: Always set this when creating the new specialization?
  void setTemplateArgsInfo(const TemplateArgumentListInfo &ArgsInfo);

  const TemplateArgumentListInfo &getTemplateArgsInfo() const {
    return TemplateArgsInfo;
  }

  /// Determine the kind of specialization that this
  /// declaration represents.
  TemplateSpecializationKind getSpecializationKind() const {
    return static_cast<TemplateSpecializationKind>(SpecializationKind);
  }

  bool isExplicitSpecialization() const {
    return getSpecializationKind() == TSK_ExplicitSpecialization;
  }

  /// True if this declaration is an explicit specialization,
  /// explicit instantiation declaration, or explicit instantiation
  /// definition.
  bool isExplicitInstantiationOrSpecialization() const {
    return isTemplateExplicitInstantiationOrSpecialization(
        getTemplateSpecializationKind());
  }

  void setSpecializationKind(TemplateSpecializationKind TSK) {
    SpecializationKind = TSK;
  }

  /// Get the point of instantiation (if any), or null if none.
  SourceLocation getPointOfInstantiation() const {
    return PointOfInstantiation;
  }

  void setPointOfInstantiation(SourceLocation Loc) {
    assert(Loc.isValid() && "point of instantiation must be valid!");
    PointOfInstantiation = Loc;
  }

  void setCompleteDefinition() { IsCompleteDefinition = true; }

  /// If this variable template specialization is an instantiation of
  /// a template (rather than an explicit specialization), return the
  /// variable template or variable template partial specialization from which
  /// it was instantiated.
  llvm::PointerUnion<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>
  getInstantiatedFrom() const {
    if (!isTemplateInstantiation(getSpecializationKind()))
      return llvm::PointerUnion<VarTemplateDecl *,
                                VarTemplatePartialSpecializationDecl *>();

    return getSpecializedTemplateOrPartial();
  }

  /// Retrieve the variable template or variable template partial
  /// specialization which was specialized by this.
  llvm::PointerUnion<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>
  getSpecializedTemplateOrPartial() const {
    if (const auto *PartialSpec =
            SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization *>())
      return PartialSpec->PartialSpecialization;

    return SpecializedTemplate.get<VarTemplateDecl *>();
  }

  /// Retrieve the set of template arguments that should be used
  /// to instantiate the initializer of the variable template or variable
  /// template partial specialization from which this variable template
  /// specialization was instantiated.
  ///
  /// \returns For a variable template specialization instantiated from the
  /// primary template, this function will return the same template arguments
  /// as getTemplateArgs(). For a variable template specialization instantiated
  /// from a variable template partial specialization, this function will the
  /// return deduced template arguments for the variable template partial
  /// specialization itself.
  const TemplateArgumentList &getTemplateInstantiationArgs() const {
    if (const auto *PartialSpec =
            SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization *>())
      return *PartialSpec->TemplateArgs;

    return getTemplateArgs();
  }

  /// Note that this variable template specialization is actually an
  /// instantiation of the given variable template partial specialization whose
  /// template arguments have been deduced.
  void setInstantiationOf(VarTemplatePartialSpecializationDecl *PartialSpec,
                          const TemplateArgumentList *TemplateArgs) {
    assert(!SpecializedTemplate.is<SpecializedPartialSpecialization *>() &&
           "Already set to a variable template partial specialization!");
    auto *PS = new (getASTContext()) SpecializedPartialSpecialization();
    PS->PartialSpecialization = PartialSpec;
    PS->TemplateArgs = TemplateArgs;
    SpecializedTemplate = PS;
  }

  /// Note that this variable template specialization is an instantiation
  /// of the given variable template.
  void setInstantiationOf(VarTemplateDecl *TemplDecl) {
    assert(!SpecializedTemplate.is<SpecializedPartialSpecialization *>() &&
           "Previously set to a variable template partial specialization!");
    SpecializedTemplate = TemplDecl;
  }

  /// Sets the type of this specialization as it was written by
  /// the user.
  void setTypeAsWritten(TypeSourceInfo *T) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->TypeAsWritten = T;
  }

  /// Gets the type of this specialization as it was written by
  /// the user, if it was so written.
  TypeSourceInfo *getTypeAsWritten() const {
    return ExplicitInfo ? ExplicitInfo->TypeAsWritten : nullptr;
  }

  /// Gets the location of the extern keyword, if present.
  SourceLocation getExternLoc() const {
    return ExplicitInfo ? ExplicitInfo->ExternLoc : SourceLocation();
  }

  /// Sets the location of the extern keyword.
  void setExternLoc(SourceLocation Loc) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->ExternLoc = Loc;
  }

  /// Sets the location of the template keyword.
  void setTemplateKeywordLoc(SourceLocation Loc) {
    if (!ExplicitInfo)
      ExplicitInfo = new (getASTContext()) ExplicitSpecializationInfo;
    ExplicitInfo->TemplateKeywordLoc = Loc;
  }

  /// Gets the location of the template keyword, if present.
  SourceLocation getTemplateKeywordLoc() const {
    return ExplicitInfo ? ExplicitInfo->TemplateKeywordLoc : SourceLocation();
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, TemplateArgs->asArray(), getASTContext());
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      ArrayRef<TemplateArgument> TemplateArgs,
                      ASTContext &Context) {
    ID.AddInteger(TemplateArgs.size());
    for (const TemplateArgument &TemplateArg : TemplateArgs)
      TemplateArg.Profile(ID, Context);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K >= firstVarTemplateSpecialization &&
           K <= lastVarTemplateSpecialization;
  }
};

class VarTemplatePartialSpecializationDecl
    : public VarTemplateSpecializationDecl {
  /// The list of template parameters
  TemplateParameterList *TemplateParams = nullptr;

  /// The source info for the template arguments as written.
  /// FIXME: redundant with TypeAsWritten?
  const ASTTemplateArgumentListInfo *ArgsAsWritten = nullptr;

  /// The variable template partial specialization from which this
  /// variable template partial specialization was instantiated.
  ///
  /// The boolean value will be true to indicate that this variable template
  /// partial specialization was specialized at this level.
  llvm::PointerIntPair<VarTemplatePartialSpecializationDecl *, 1, bool>
  InstantiatedFromMember;

  VarTemplatePartialSpecializationDecl(
      ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
      SourceLocation IdLoc, TemplateParameterList *Params,
      VarTemplateDecl *SpecializedTemplate, QualType T, TypeSourceInfo *TInfo,
      StorageClass S, ArrayRef<TemplateArgument> Args,
      const ASTTemplateArgumentListInfo *ArgInfos);

  VarTemplatePartialSpecializationDecl(ASTContext &Context)
      : VarTemplateSpecializationDecl(VarTemplatePartialSpecialization,
                                      Context),
        InstantiatedFromMember(nullptr, false) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static VarTemplatePartialSpecializationDecl *
  Create(ASTContext &Context, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation IdLoc, TemplateParameterList *Params,
         VarTemplateDecl *SpecializedTemplate, QualType T,
         TypeSourceInfo *TInfo, StorageClass S, ArrayRef<TemplateArgument> Args,
         const TemplateArgumentListInfo &ArgInfos);

  static VarTemplatePartialSpecializationDecl *CreateDeserialized(ASTContext &C,
                                                                  unsigned ID);

  VarTemplatePartialSpecializationDecl *getMostRecentDecl() {
    return cast<VarTemplatePartialSpecializationDecl>(
             static_cast<VarTemplateSpecializationDecl *>(
               this)->getMostRecentDecl());
  }

  /// Get the list of template parameters
  TemplateParameterList *getTemplateParameters() const {
    return TemplateParams;
  }

  /// Get the template arguments as written.
  const ASTTemplateArgumentListInfo *getTemplateArgsAsWritten() const {
    return ArgsAsWritten;
  }

  /// Retrieve the member variable template partial specialization from
  /// which this particular variable template partial specialization was
  /// instantiated.
  ///
  /// \code
  /// template<typename T>
  /// struct Outer {
  ///   template<typename U> U Inner;
  ///   template<typename U> U* Inner<U*> = (U*)(0); // #1
  /// };
  ///
  /// template int* Outer<float>::Inner<int*>;
  /// \endcode
  ///
  /// In this example, the instantiation of \c Outer<float>::Inner<int*> will
  /// end up instantiating the partial specialization
  /// \c Outer<float>::Inner<U*>, which itself was instantiated from the
  /// variable template partial specialization \c Outer<T>::Inner<U*>. Given
  /// \c Outer<float>::Inner<U*>, this function would return
  /// \c Outer<T>::Inner<U*>.
  VarTemplatePartialSpecializationDecl *getInstantiatedFromMember() const {
    const auto *First =
        cast<VarTemplatePartialSpecializationDecl>(getFirstDecl());
    return First->InstantiatedFromMember.getPointer();
  }

  void
  setInstantiatedFromMember(VarTemplatePartialSpecializationDecl *PartialSpec) {
    auto *First = cast<VarTemplatePartialSpecializationDecl>(getFirstDecl());
    First->InstantiatedFromMember.setPointer(PartialSpec);
  }

  /// Determines whether this variable template partial specialization
  /// was a specialization of a member partial specialization.
  ///
  /// In the following example, the member template partial specialization
  /// \c X<int>::Inner<T*> is a member specialization.
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<typename U> U Inner;
  ///   template<typename U> U* Inner<U*> = (U*)(0);
  /// };
  ///
  /// template<> template<typename T>
  /// U* X<int>::Inner<T*> = (T*)(0) + 1;
  /// \endcode
  bool isMemberSpecialization() {
    const auto *First =
        cast<VarTemplatePartialSpecializationDecl>(getFirstDecl());
    return First->InstantiatedFromMember.getInt();
  }

  /// Note that this member template is a specialization.
  void setMemberSpecialization() {
    auto *First = cast<VarTemplatePartialSpecializationDecl>(getFirstDecl());
    assert(First->InstantiatedFromMember.getPointer() &&
           "Only member templates can be member template specializations");
    return First->InstantiatedFromMember.setInt(true);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }

  static bool classofKind(Kind K) {
    return K == VarTemplatePartialSpecialization;
  }
};

/// Declaration of a variable template.
class VarTemplateDecl : public RedeclarableTemplateDecl {
protected:
  /// Data that is common to all of the declarations of a given
  /// variable template.
  struct Common : CommonBase {
    /// The variable template specializations for this variable
    /// template, including explicit specializations and instantiations.
    llvm::FoldingSetVector<VarTemplateSpecializationDecl> Specializations;

    /// The variable template partial specializations for this variable
    /// template.
    llvm::FoldingSetVector<VarTemplatePartialSpecializationDecl>
    PartialSpecializations;

    Common() = default;
  };

  /// Retrieve the set of specializations of this variable template.
  llvm::FoldingSetVector<VarTemplateSpecializationDecl> &
  getSpecializations() const;

  /// Retrieve the set of partial specializations of this class
  /// template.
  llvm::FoldingSetVector<VarTemplatePartialSpecializationDecl> &
  getPartialSpecializations();

  VarTemplateDecl(ASTContext &C, DeclContext *DC, SourceLocation L,
                  DeclarationName Name, TemplateParameterList *Params,
                  NamedDecl *Decl)
      : RedeclarableTemplateDecl(VarTemplate, C, DC, L, Name, Params, Decl) {}

  CommonBase *newCommon(ASTContext &C) const override;

  Common *getCommonPtr() const {
    return static_cast<Common *>(RedeclarableTemplateDecl::getCommonPtr());
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Load any lazily-loaded specializations from the external source.
  void LoadLazySpecializations() const;

  /// Get the underlying variable declarations of the template.
  VarDecl *getTemplatedDecl() const {
    return static_cast<VarDecl *>(TemplatedDecl);
  }

  /// Returns whether this template declaration defines the primary
  /// variable pattern.
  bool isThisDeclarationADefinition() const {
    return getTemplatedDecl()->isThisDeclarationADefinition();
  }

  VarTemplateDecl *getDefinition();

  /// Create a variable template node.
  static VarTemplateDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, DeclarationName Name,
                                 TemplateParameterList *Params,
                                 VarDecl *Decl);

  /// Create an empty variable template node.
  static VarTemplateDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Return the specialization with the provided arguments if it exists,
  /// otherwise return the insertion point.
  VarTemplateSpecializationDecl *
  findSpecialization(ArrayRef<TemplateArgument> Args, void *&InsertPos);

  /// Insert the specified specialization knowing that it is not already
  /// in. InsertPos must be obtained from findSpecialization.
  void AddSpecialization(VarTemplateSpecializationDecl *D, void *InsertPos);

  VarTemplateDecl *getCanonicalDecl() override {
    return cast<VarTemplateDecl>(RedeclarableTemplateDecl::getCanonicalDecl());
  }
  const VarTemplateDecl *getCanonicalDecl() const {
    return cast<VarTemplateDecl>(RedeclarableTemplateDecl::getCanonicalDecl());
  }

  /// Retrieve the previous declaration of this variable template, or
  /// nullptr if no such declaration exists.
  VarTemplateDecl *getPreviousDecl() {
    return cast_or_null<VarTemplateDecl>(
        static_cast<RedeclarableTemplateDecl *>(this)->getPreviousDecl());
  }
  const VarTemplateDecl *getPreviousDecl() const {
    return cast_or_null<VarTemplateDecl>(
            static_cast<const RedeclarableTemplateDecl *>(
              this)->getPreviousDecl());
  }

  VarTemplateDecl *getMostRecentDecl() {
    return cast<VarTemplateDecl>(
        static_cast<RedeclarableTemplateDecl *>(this)->getMostRecentDecl());
  }
  const VarTemplateDecl *getMostRecentDecl() const {
    return const_cast<VarTemplateDecl *>(this)->getMostRecentDecl();
  }

  VarTemplateDecl *getInstantiatedFromMemberTemplate() const {
    return cast_or_null<VarTemplateDecl>(
        RedeclarableTemplateDecl::getInstantiatedFromMemberTemplate());
  }

  /// Return the partial specialization with the provided arguments if it
  /// exists, otherwise return the insertion point.
  VarTemplatePartialSpecializationDecl *
  findPartialSpecialization(ArrayRef<TemplateArgument> Args, void *&InsertPos);

  /// Insert the specified partial specialization knowing that it is not
  /// already in. InsertPos must be obtained from findPartialSpecialization.
  void AddPartialSpecialization(VarTemplatePartialSpecializationDecl *D,
                                void *InsertPos);

  /// Retrieve the partial specializations as an ordered list.
  void getPartialSpecializations(
      SmallVectorImpl<VarTemplatePartialSpecializationDecl *> &PS);

  /// Find a variable template partial specialization which was
  /// instantiated
  /// from the given member partial specialization.
  ///
  /// \param D a member variable template partial specialization.
  ///
  /// \returns the variable template partial specialization which was
  /// instantiated
  /// from the given member partial specialization, or nullptr if no such
  /// partial specialization exists.
  VarTemplatePartialSpecializationDecl *findPartialSpecInstantiatedFromMember(
      VarTemplatePartialSpecializationDecl *D);

  using spec_iterator = SpecIterator<VarTemplateSpecializationDecl>;
  using spec_range = llvm::iterator_range<spec_iterator>;

  spec_range specializations() const {
    return spec_range(spec_begin(), spec_end());
  }

  spec_iterator spec_begin() const {
    return makeSpecIterator(getSpecializations(), false);
  }

  spec_iterator spec_end() const {
    return makeSpecIterator(getSpecializations(), true);
  }

  // Implement isa/cast/dyncast support
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == VarTemplate; }
};

inline NamedDecl *getAsNamedDecl(TemplateParameter P) {
  if (auto *PD = P.dyn_cast<TemplateTypeParmDecl *>())
    return PD;
  if (auto *PD = P.dyn_cast<NonTypeTemplateParmDecl *>())
    return PD;
  return P.get<TemplateTemplateParmDecl *>();
}

inline TemplateDecl *getAsTypeTemplateDecl(Decl *D) {
  auto *TD = dyn_cast<TemplateDecl>(D);
  return TD && (isa<ClassTemplateDecl>(TD) ||
                isa<ClassTemplatePartialSpecializationDecl>(TD) ||
                isa<TypeAliasTemplateDecl>(TD) ||
                isa<TemplateTemplateParmDecl>(TD))
             ? TD
             : nullptr;
}

} // namespace clang

#endif // LLVM_CLANG_AST_DECLTEMPLATE_H
