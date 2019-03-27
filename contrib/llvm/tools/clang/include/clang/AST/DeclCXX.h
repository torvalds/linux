//===- DeclCXX.h - Classes for representing C++ declarations --*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the C++ Decl subclasses, other than those for templates
/// (found in DeclTemplate.h) and friends (in DeclFriend.h).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLCXX_H
#define LLVM_CLANG_AST_DECLCXX_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

namespace clang {

class ClassTemplateDecl;
class ConstructorUsingShadowDecl;
class CXXBasePath;
class CXXBasePaths;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXFinalOverriderMap;
class CXXIndirectPrimaryBaseSet;
class CXXMethodDecl;
class DiagnosticBuilder;
class FriendDecl;
class FunctionTemplateDecl;
class IdentifierInfo;
class MemberSpecializationInfo;
class TemplateDecl;
class TemplateParameterList;
class UsingDecl;

/// Represents any kind of function declaration, whether it is a
/// concrete function or a function template.
class AnyFunctionDecl {
  NamedDecl *Function;

  AnyFunctionDecl(NamedDecl *ND) : Function(ND) {}

public:
  AnyFunctionDecl(FunctionDecl *FD) : Function(FD) {}
  AnyFunctionDecl(FunctionTemplateDecl *FTD);

  /// Implicily converts any function or function template into a
  /// named declaration.
  operator NamedDecl *() const { return Function; }

  /// Retrieve the underlying function or function template.
  NamedDecl *get() const { return Function; }

  static AnyFunctionDecl getFromNamedDecl(NamedDecl *ND) {
    return AnyFunctionDecl(ND);
  }
};

} // namespace clang

namespace llvm {

  // Provide PointerLikeTypeTraits for non-cvr pointers.
  template<>
  struct PointerLikeTypeTraits< ::clang::AnyFunctionDecl> {
    static void *getAsVoidPointer(::clang::AnyFunctionDecl F) {
      return F.get();
    }

    static ::clang::AnyFunctionDecl getFromVoidPointer(void *P) {
      return ::clang::AnyFunctionDecl::getFromNamedDecl(
                                      static_cast< ::clang::NamedDecl*>(P));
    }

    enum { NumLowBitsAvailable = 2 };
  };

} // namespace llvm

namespace clang {

/// Represents an access specifier followed by colon ':'.
///
/// An objects of this class represents sugar for the syntactic occurrence
/// of an access specifier followed by a colon in the list of member
/// specifiers of a C++ class definition.
///
/// Note that they do not represent other uses of access specifiers,
/// such as those occurring in a list of base specifiers.
/// Also note that this class has nothing to do with so-called
/// "access declarations" (C++98 11.3 [class.access.dcl]).
class AccessSpecDecl : public Decl {
  /// The location of the ':'.
  SourceLocation ColonLoc;

  AccessSpecDecl(AccessSpecifier AS, DeclContext *DC,
                 SourceLocation ASLoc, SourceLocation ColonLoc)
    : Decl(AccessSpec, DC, ASLoc), ColonLoc(ColonLoc) {
    setAccess(AS);
  }

  AccessSpecDecl(EmptyShell Empty) : Decl(AccessSpec, Empty) {}

  virtual void anchor();

public:
  /// The location of the access specifier.
  SourceLocation getAccessSpecifierLoc() const { return getLocation(); }

  /// Sets the location of the access specifier.
  void setAccessSpecifierLoc(SourceLocation ASLoc) { setLocation(ASLoc); }

  /// The location of the colon following the access specifier.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Sets the location of the colon.
  void setColonLoc(SourceLocation CLoc) { ColonLoc = CLoc; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getAccessSpecifierLoc(), getColonLoc());
  }

  static AccessSpecDecl *Create(ASTContext &C, AccessSpecifier AS,
                                DeclContext *DC, SourceLocation ASLoc,
                                SourceLocation ColonLoc) {
    return new (C, DC) AccessSpecDecl(AS, DC, ASLoc, ColonLoc);
  }

  static AccessSpecDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == AccessSpec; }
};

/// Represents a base class of a C++ class.
///
/// Each CXXBaseSpecifier represents a single, direct base class (or
/// struct) of a C++ class (or struct). It specifies the type of that
/// base class, whether it is a virtual or non-virtual base, and what
/// level of access (public, protected, private) is used for the
/// derivation. For example:
///
/// \code
///   class A { };
///   class B { };
///   class C : public virtual A, protected B { };
/// \endcode
///
/// In this code, C will have two CXXBaseSpecifiers, one for "public
/// virtual A" and the other for "protected B".
class CXXBaseSpecifier {
  /// The source code range that covers the full base
  /// specifier, including the "virtual" (if present) and access
  /// specifier (if present).
  SourceRange Range;

  /// The source location of the ellipsis, if this is a pack
  /// expansion.
  SourceLocation EllipsisLoc;

  /// Whether this is a virtual base class or not.
  unsigned Virtual : 1;

  /// Whether this is the base of a class (true) or of a struct (false).
  ///
  /// This determines the mapping from the access specifier as written in the
  /// source code to the access specifier used for semantic analysis.
  unsigned BaseOfClass : 1;

  /// Access specifier as written in the source code (may be AS_none).
  ///
  /// The actual type of data stored here is an AccessSpecifier, but we use
  /// "unsigned" here to work around a VC++ bug.
  unsigned Access : 2;

  /// Whether the class contains a using declaration
  /// to inherit the named class's constructors.
  unsigned InheritConstructors : 1;

  /// The type of the base class.
  ///
  /// This will be a class or struct (or a typedef of such). The source code
  /// range does not include the \c virtual or the access specifier.
  TypeSourceInfo *BaseTypeInfo;

public:
  CXXBaseSpecifier() = default;
  CXXBaseSpecifier(SourceRange R, bool V, bool BC, AccessSpecifier A,
                   TypeSourceInfo *TInfo, SourceLocation EllipsisLoc)
    : Range(R), EllipsisLoc(EllipsisLoc), Virtual(V), BaseOfClass(BC),
      Access(A), InheritConstructors(false), BaseTypeInfo(TInfo) {}

  /// Retrieves the source range that contains the entire base specifier.
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  /// Get the location at which the base class type was written.
  SourceLocation getBaseTypeLoc() const LLVM_READONLY {
    return BaseTypeInfo->getTypeLoc().getBeginLoc();
  }

  /// Determines whether the base class is a virtual base class (or not).
  bool isVirtual() const { return Virtual; }

  /// Determine whether this base class is a base of a class declared
  /// with the 'class' keyword (vs. one declared with the 'struct' keyword).
  bool isBaseOfClass() const { return BaseOfClass; }

  /// Determine whether this base specifier is a pack expansion.
  bool isPackExpansion() const { return EllipsisLoc.isValid(); }

  /// Determine whether this base class's constructors get inherited.
  bool getInheritConstructors() const { return InheritConstructors; }

  /// Set that this base class's constructors should be inherited.
  void setInheritConstructors(bool Inherit = true) {
    InheritConstructors = Inherit;
  }

  /// For a pack expansion, determine the location of the ellipsis.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  /// Returns the access specifier for this base specifier.
  ///
  /// This is the actual base specifier as used for semantic analysis, so
  /// the result can never be AS_none. To retrieve the access specifier as
  /// written in the source code, use getAccessSpecifierAsWritten().
  AccessSpecifier getAccessSpecifier() const {
    if ((AccessSpecifier)Access == AS_none)
      return BaseOfClass? AS_private : AS_public;
    else
      return (AccessSpecifier)Access;
  }

  /// Retrieves the access specifier as written in the source code
  /// (which may mean that no access specifier was explicitly written).
  ///
  /// Use getAccessSpecifier() to retrieve the access specifier for use in
  /// semantic analysis.
  AccessSpecifier getAccessSpecifierAsWritten() const {
    return (AccessSpecifier)Access;
  }

  /// Retrieves the type of the base class.
  ///
  /// This type will always be an unqualified class type.
  QualType getType() const {
    return BaseTypeInfo->getType().getUnqualifiedType();
  }

  /// Retrieves the type and source location of the base class.
  TypeSourceInfo *getTypeSourceInfo() const { return BaseTypeInfo; }
};

/// Represents a C++ struct/union/class.
class CXXRecordDecl : public RecordDecl {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class ASTNodeImporter;
  friend class ASTReader;
  friend class ASTRecordWriter;
  friend class ASTWriter;
  friend class DeclContext;
  friend class LambdaExpr;

  friend void FunctionDecl::setPure(bool);
  friend void TagDecl::startDefinition();

  /// Values used in DefinitionData fields to represent special members.
  enum SpecialMemberFlags {
    SMF_DefaultConstructor = 0x1,
    SMF_CopyConstructor = 0x2,
    SMF_MoveConstructor = 0x4,
    SMF_CopyAssignment = 0x8,
    SMF_MoveAssignment = 0x10,
    SMF_Destructor = 0x20,
    SMF_All = 0x3f
  };

  struct DefinitionData {
    /// True if this class has any user-declared constructors.
    unsigned UserDeclaredConstructor : 1;

    /// The user-declared special members which this class has.
    unsigned UserDeclaredSpecialMembers : 6;

    /// True when this class is an aggregate.
    unsigned Aggregate : 1;

    /// True when this class is a POD-type.
    unsigned PlainOldData : 1;

    /// true when this class is empty for traits purposes,
    /// i.e. has no data members other than 0-width bit-fields, has no
    /// virtual function/base, and doesn't inherit from a non-empty
    /// class. Doesn't take union-ness into account.
    unsigned Empty : 1;

    /// True when this class is polymorphic, i.e., has at
    /// least one virtual member or derives from a polymorphic class.
    unsigned Polymorphic : 1;

    /// True when this class is abstract, i.e., has at least
    /// one pure virtual function, (that can come from a base class).
    unsigned Abstract : 1;

    /// True when this class is standard-layout, per the applicable
    /// language rules (including DRs).
    unsigned IsStandardLayout : 1;

    /// True when this class was standard-layout under the C++11
    /// definition.
    ///
    /// C++11 [class]p7.  A standard-layout class is a class that:
    /// * has no non-static data members of type non-standard-layout class (or
    ///   array of such types) or reference,
    /// * has no virtual functions (10.3) and no virtual base classes (10.1),
    /// * has the same access control (Clause 11) for all non-static data
    ///   members
    /// * has no non-standard-layout base classes,
    /// * either has no non-static data members in the most derived class and at
    ///   most one base class with non-static data members, or has no base
    ///   classes with non-static data members, and
    /// * has no base classes of the same type as the first non-static data
    ///   member.
    unsigned IsCXX11StandardLayout : 1;

    /// True when any base class has any declared non-static data
    /// members or bit-fields.
    /// This is a helper bit of state used to implement IsStandardLayout more
    /// efficiently.
    unsigned HasBasesWithFields : 1;

    /// True when any base class has any declared non-static data
    /// members.
    /// This is a helper bit of state used to implement IsCXX11StandardLayout
    /// more efficiently.
    unsigned HasBasesWithNonStaticDataMembers : 1;

    /// True when there are private non-static data members.
    unsigned HasPrivateFields : 1;

    /// True when there are protected non-static data members.
    unsigned HasProtectedFields : 1;

    /// True when there are private non-static data members.
    unsigned HasPublicFields : 1;

    /// True if this class (or any subobject) has mutable fields.
    unsigned HasMutableFields : 1;

    /// True if this class (or any nested anonymous struct or union)
    /// has variant members.
    unsigned HasVariantMembers : 1;

    /// True if there no non-field members declared by the user.
    unsigned HasOnlyCMembers : 1;

    /// True if any field has an in-class initializer, including those
    /// within anonymous unions or structs.
    unsigned HasInClassInitializer : 1;

    /// True if any field is of reference type, and does not have an
    /// in-class initializer.
    ///
    /// In this case, value-initialization of this class is illegal in C++98
    /// even if the class has a trivial default constructor.
    unsigned HasUninitializedReferenceMember : 1;

    /// True if any non-mutable field whose type doesn't have a user-
    /// provided default ctor also doesn't have an in-class initializer.
    unsigned HasUninitializedFields : 1;

    /// True if there are any member using-declarations that inherit
    /// constructors from a base class.
    unsigned HasInheritedConstructor : 1;

    /// True if there are any member using-declarations named
    /// 'operator='.
    unsigned HasInheritedAssignment : 1;

    /// These flags are \c true if a defaulted corresponding special
    /// member can't be fully analyzed without performing overload resolution.
    /// @{
    unsigned NeedOverloadResolutionForCopyConstructor : 1;
    unsigned NeedOverloadResolutionForMoveConstructor : 1;
    unsigned NeedOverloadResolutionForMoveAssignment : 1;
    unsigned NeedOverloadResolutionForDestructor : 1;
    /// @}

    /// These flags are \c true if an implicit defaulted corresponding
    /// special member would be defined as deleted.
    /// @{
    unsigned DefaultedCopyConstructorIsDeleted : 1;
    unsigned DefaultedMoveConstructorIsDeleted : 1;
    unsigned DefaultedMoveAssignmentIsDeleted : 1;
    unsigned DefaultedDestructorIsDeleted : 1;
    /// @}

    /// The trivial special members which this class has, per
    /// C++11 [class.ctor]p5, C++11 [class.copy]p12, C++11 [class.copy]p25,
    /// C++11 [class.dtor]p5, or would have if the member were not suppressed.
    ///
    /// This excludes any user-declared but not user-provided special members
    /// which have been declared but not yet defined.
    unsigned HasTrivialSpecialMembers : 6;

    /// These bits keep track of the triviality of special functions for the
    /// purpose of calls. Only the bits corresponding to SMF_CopyConstructor,
    /// SMF_MoveConstructor, and SMF_Destructor are meaningful here.
    unsigned HasTrivialSpecialMembersForCall : 6;

    /// The declared special members of this class which are known to be
    /// non-trivial.
    ///
    /// This excludes any user-declared but not user-provided special members
    /// which have been declared but not yet defined, and any implicit special
    /// members which have not yet been declared.
    unsigned DeclaredNonTrivialSpecialMembers : 6;

    /// These bits keep track of the declared special members that are
    /// non-trivial for the purpose of calls.
    /// Only the bits corresponding to SMF_CopyConstructor,
    /// SMF_MoveConstructor, and SMF_Destructor are meaningful here.
    unsigned DeclaredNonTrivialSpecialMembersForCall : 6;

    /// True when this class has a destructor with no semantic effect.
    unsigned HasIrrelevantDestructor : 1;

    /// True when this class has at least one user-declared constexpr
    /// constructor which is neither the copy nor move constructor.
    unsigned HasConstexprNonCopyMoveConstructor : 1;

    /// True if this class has a (possibly implicit) defaulted default
    /// constructor.
    unsigned HasDefaultedDefaultConstructor : 1;

    /// True if a defaulted default constructor for this class would
    /// be constexpr.
    unsigned DefaultedDefaultConstructorIsConstexpr : 1;

    /// True if this class has a constexpr default constructor.
    ///
    /// This is true for either a user-declared constexpr default constructor
    /// or an implicitly declared constexpr default constructor.
    unsigned HasConstexprDefaultConstructor : 1;

    /// True when this class contains at least one non-static data
    /// member or base class of non-literal or volatile type.
    unsigned HasNonLiteralTypeFieldsOrBases : 1;

    /// True when visible conversion functions are already computed
    /// and are available.
    unsigned ComputedVisibleConversions : 1;

    /// Whether we have a C++11 user-provided default constructor (not
    /// explicitly deleted or defaulted).
    unsigned UserProvidedDefaultConstructor : 1;

    /// The special members which have been declared for this class,
    /// either by the user or implicitly.
    unsigned DeclaredSpecialMembers : 6;

    /// Whether an implicit copy constructor could have a const-qualified
    /// parameter, for initializing virtual bases and for other subobjects.
    unsigned ImplicitCopyConstructorCanHaveConstParamForVBase : 1;
    unsigned ImplicitCopyConstructorCanHaveConstParamForNonVBase : 1;

    /// Whether an implicit copy assignment operator would have a
    /// const-qualified parameter.
    unsigned ImplicitCopyAssignmentHasConstParam : 1;

    /// Whether any declared copy constructor has a const-qualified
    /// parameter.
    unsigned HasDeclaredCopyConstructorWithConstParam : 1;

    /// Whether any declared copy assignment operator has either a
    /// const-qualified reference parameter or a non-reference parameter.
    unsigned HasDeclaredCopyAssignmentWithConstParam : 1;

    /// Whether this class describes a C++ lambda.
    unsigned IsLambda : 1;

    /// Whether we are currently parsing base specifiers.
    unsigned IsParsingBaseSpecifiers : 1;

    unsigned HasODRHash : 1;

    /// A hash of parts of the class to help in ODR checking.
    unsigned ODRHash = 0;

    /// The number of base class specifiers in Bases.
    unsigned NumBases = 0;

    /// The number of virtual base class specifiers in VBases.
    unsigned NumVBases = 0;

    /// Base classes of this class.
    ///
    /// FIXME: This is wasted space for a union.
    LazyCXXBaseSpecifiersPtr Bases;

    /// direct and indirect virtual base classes of this class.
    LazyCXXBaseSpecifiersPtr VBases;

    /// The conversion functions of this C++ class (but not its
    /// inherited conversion functions).
    ///
    /// Each of the entries in this overload set is a CXXConversionDecl.
    LazyASTUnresolvedSet Conversions;

    /// The conversion functions of this C++ class and all those
    /// inherited conversion functions that are visible in this class.
    ///
    /// Each of the entries in this overload set is a CXXConversionDecl or a
    /// FunctionTemplateDecl.
    LazyASTUnresolvedSet VisibleConversions;

    /// The declaration which defines this record.
    CXXRecordDecl *Definition;

    /// The first friend declaration in this class, or null if there
    /// aren't any.
    ///
    /// This is actually currently stored in reverse order.
    LazyDeclPtr FirstFriend;

    DefinitionData(CXXRecordDecl *D);

    /// Retrieve the set of direct base classes.
    CXXBaseSpecifier *getBases() const {
      if (!Bases.isOffset())
        return Bases.get(nullptr);
      return getBasesSlowCase();
    }

    /// Retrieve the set of virtual base classes.
    CXXBaseSpecifier *getVBases() const {
      if (!VBases.isOffset())
        return VBases.get(nullptr);
      return getVBasesSlowCase();
    }

    ArrayRef<CXXBaseSpecifier> bases() const {
      return llvm::makeArrayRef(getBases(), NumBases);
    }

    ArrayRef<CXXBaseSpecifier> vbases() const {
      return llvm::makeArrayRef(getVBases(), NumVBases);
    }

  private:
    CXXBaseSpecifier *getBasesSlowCase() const;
    CXXBaseSpecifier *getVBasesSlowCase() const;
  };

  struct DefinitionData *DefinitionData;

  /// Describes a C++ closure type (generated by a lambda expression).
  struct LambdaDefinitionData : public DefinitionData {
    using Capture = LambdaCapture;

    /// Whether this lambda is known to be dependent, even if its
    /// context isn't dependent.
    ///
    /// A lambda with a non-dependent context can be dependent if it occurs
    /// within the default argument of a function template, because the
    /// lambda will have been created with the enclosing context as its
    /// declaration context, rather than function. This is an unfortunate
    /// artifact of having to parse the default arguments before.
    unsigned Dependent : 1;

    /// Whether this lambda is a generic lambda.
    unsigned IsGenericLambda : 1;

    /// The Default Capture.
    unsigned CaptureDefault : 2;

    /// The number of captures in this lambda is limited 2^NumCaptures.
    unsigned NumCaptures : 15;

    /// The number of explicit captures in this lambda.
    unsigned NumExplicitCaptures : 13;

    /// The number used to indicate this lambda expression for name
    /// mangling in the Itanium C++ ABI.
    unsigned ManglingNumber = 0;

    /// The declaration that provides context for this lambda, if the
    /// actual DeclContext does not suffice. This is used for lambdas that
    /// occur within default arguments of function parameters within the class
    /// or within a data member initializer.
    LazyDeclPtr ContextDecl;

    /// The list of captures, both explicit and implicit, for this
    /// lambda.
    Capture *Captures = nullptr;

    /// The type of the call method.
    TypeSourceInfo *MethodTyInfo;

    LambdaDefinitionData(CXXRecordDecl *D, TypeSourceInfo *Info,
                         bool Dependent, bool IsGeneric,
                         LambdaCaptureDefault CaptureDefault)
      : DefinitionData(D), Dependent(Dependent), IsGenericLambda(IsGeneric),
        CaptureDefault(CaptureDefault), NumCaptures(0), NumExplicitCaptures(0),
        MethodTyInfo(Info) {
      IsLambda = true;

      // C++1z [expr.prim.lambda]p4:
      //   This class type is not an aggregate type.
      Aggregate = false;
      PlainOldData = false;
    }
  };

  struct DefinitionData *dataPtr() const {
    // Complete the redecl chain (if necessary).
    getMostRecentDecl();
    return DefinitionData;
  }

  struct DefinitionData &data() const {
    auto *DD = dataPtr();
    assert(DD && "queried property of class with no definition");
    return *DD;
  }

  struct LambdaDefinitionData &getLambdaData() const {
    // No update required: a merged definition cannot change any lambda
    // properties.
    auto *DD = DefinitionData;
    assert(DD && DD->IsLambda && "queried lambda property of non-lambda class");
    return static_cast<LambdaDefinitionData&>(*DD);
  }

  /// The template or declaration that this declaration
  /// describes or was instantiated from, respectively.
  ///
  /// For non-templates, this value will be null. For record
  /// declarations that describe a class template, this will be a
  /// pointer to a ClassTemplateDecl. For member
  /// classes of class template specializations, this will be the
  /// MemberSpecializationInfo referring to the member class that was
  /// instantiated or specialized.
  llvm::PointerUnion<ClassTemplateDecl *, MemberSpecializationInfo *>
      TemplateOrInstantiation;

  /// Called from setBases and addedMember to notify the class that a
  /// direct or virtual base class or a member of class type has been added.
  void addedClassSubobject(CXXRecordDecl *Base);

  /// Notify the class that member has been added.
  ///
  /// This routine helps maintain information about the class based on which
  /// members have been added. It will be invoked by DeclContext::addDecl()
  /// whenever a member is added to this record.
  void addedMember(Decl *D);

  void markedVirtualFunctionPure();

  /// Get the head of our list of friend declarations, possibly
  /// deserializing the friends from an external AST source.
  FriendDecl *getFirstFriend() const;

  /// Determine whether this class has an empty base class subobject of type X
  /// or of one of the types that might be at offset 0 within X (per the C++
  /// "standard layout" rules).
  bool hasSubobjectAtOffsetZeroOfEmptyBaseType(ASTContext &Ctx,
                                               const CXXRecordDecl *X);

protected:
  CXXRecordDecl(Kind K, TagKind TK, const ASTContext &C, DeclContext *DC,
                SourceLocation StartLoc, SourceLocation IdLoc,
                IdentifierInfo *Id, CXXRecordDecl *PrevDecl);

public:
  /// Iterator that traverses the base classes of a class.
  using base_class_iterator = CXXBaseSpecifier *;

  /// Iterator that traverses the base classes of a class.
  using base_class_const_iterator = const CXXBaseSpecifier *;

  CXXRecordDecl *getCanonicalDecl() override {
    return cast<CXXRecordDecl>(RecordDecl::getCanonicalDecl());
  }

  const CXXRecordDecl *getCanonicalDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getCanonicalDecl();
  }

  CXXRecordDecl *getPreviousDecl() {
    return cast_or_null<CXXRecordDecl>(
            static_cast<RecordDecl *>(this)->getPreviousDecl());
  }

  const CXXRecordDecl *getPreviousDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getPreviousDecl();
  }

  CXXRecordDecl *getMostRecentDecl() {
    return cast<CXXRecordDecl>(
            static_cast<RecordDecl *>(this)->getMostRecentDecl());
  }

  const CXXRecordDecl *getMostRecentDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getMostRecentDecl();
  }

  CXXRecordDecl *getMostRecentNonInjectedDecl() {
    CXXRecordDecl *Recent =
        static_cast<CXXRecordDecl *>(this)->getMostRecentDecl();
    while (Recent->isInjectedClassName()) {
      // FIXME: Does injected class name need to be in the redeclarations chain?
      assert(Recent->getPreviousDecl());
      Recent = Recent->getPreviousDecl();
    }
    return Recent;
  }

  const CXXRecordDecl *getMostRecentNonInjectedDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getMostRecentNonInjectedDecl();
  }

  CXXRecordDecl *getDefinition() const {
    // We only need an update if we don't already know which
    // declaration is the definition.
    auto *DD = DefinitionData ? DefinitionData : dataPtr();
    return DD ? DD->Definition : nullptr;
  }

  bool hasDefinition() const { return DefinitionData || dataPtr(); }

  static CXXRecordDecl *Create(const ASTContext &C, TagKind TK, DeclContext *DC,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               IdentifierInfo *Id,
                               CXXRecordDecl *PrevDecl = nullptr,
                               bool DelayTypeCreation = false);
  static CXXRecordDecl *CreateLambda(const ASTContext &C, DeclContext *DC,
                                     TypeSourceInfo *Info, SourceLocation Loc,
                                     bool DependentLambda, bool IsGeneric,
                                     LambdaCaptureDefault CaptureDefault);
  static CXXRecordDecl *CreateDeserialized(const ASTContext &C, unsigned ID);

  bool isDynamicClass() const {
    return data().Polymorphic || data().NumVBases != 0;
  }

  /// @returns true if class is dynamic or might be dynamic because the
  /// definition is incomplete of dependent.
  bool mayBeDynamicClass() const {
    return !hasDefinition() || isDynamicClass() || hasAnyDependentBases();
  }

  /// @returns true if class is non dynamic or might be non dynamic because the
  /// definition is incomplete of dependent.
  bool mayBeNonDynamicClass() const {
    return !hasDefinition() || !isDynamicClass() || hasAnyDependentBases();
  }

  void setIsParsingBaseSpecifiers() { data().IsParsingBaseSpecifiers = true; }

  bool isParsingBaseSpecifiers() const {
    return data().IsParsingBaseSpecifiers;
  }

  unsigned getODRHash() const;

  /// Sets the base classes of this struct or class.
  void setBases(CXXBaseSpecifier const * const *Bases, unsigned NumBases);

  /// Retrieves the number of base classes of this class.
  unsigned getNumBases() const { return data().NumBases; }

  using base_class_range = llvm::iterator_range<base_class_iterator>;
  using base_class_const_range =
      llvm::iterator_range<base_class_const_iterator>;

  base_class_range bases() {
    return base_class_range(bases_begin(), bases_end());
  }
  base_class_const_range bases() const {
    return base_class_const_range(bases_begin(), bases_end());
  }

  base_class_iterator bases_begin() { return data().getBases(); }
  base_class_const_iterator bases_begin() const { return data().getBases(); }
  base_class_iterator bases_end() { return bases_begin() + data().NumBases; }
  base_class_const_iterator bases_end() const {
    return bases_begin() + data().NumBases;
  }

  /// Retrieves the number of virtual base classes of this class.
  unsigned getNumVBases() const { return data().NumVBases; }

  base_class_range vbases() {
    return base_class_range(vbases_begin(), vbases_end());
  }
  base_class_const_range vbases() const {
    return base_class_const_range(vbases_begin(), vbases_end());
  }

  base_class_iterator vbases_begin() { return data().getVBases(); }
  base_class_const_iterator vbases_begin() const { return data().getVBases(); }
  base_class_iterator vbases_end() { return vbases_begin() + data().NumVBases; }
  base_class_const_iterator vbases_end() const {
    return vbases_begin() + data().NumVBases;
  }

  /// Determine whether this class has any dependent base classes which
  /// are not the current instantiation.
  bool hasAnyDependentBases() const;

  /// Iterator access to method members.  The method iterator visits
  /// all method members of the class, including non-instance methods,
  /// special methods, etc.
  using method_iterator = specific_decl_iterator<CXXMethodDecl>;
  using method_range =
      llvm::iterator_range<specific_decl_iterator<CXXMethodDecl>>;

  method_range methods() const {
    return method_range(method_begin(), method_end());
  }

  /// Method begin iterator.  Iterates in the order the methods
  /// were declared.
  method_iterator method_begin() const {
    return method_iterator(decls_begin());
  }

  /// Method past-the-end iterator.
  method_iterator method_end() const {
    return method_iterator(decls_end());
  }

  /// Iterator access to constructor members.
  using ctor_iterator = specific_decl_iterator<CXXConstructorDecl>;
  using ctor_range =
      llvm::iterator_range<specific_decl_iterator<CXXConstructorDecl>>;

  ctor_range ctors() const { return ctor_range(ctor_begin(), ctor_end()); }

  ctor_iterator ctor_begin() const {
    return ctor_iterator(decls_begin());
  }

  ctor_iterator ctor_end() const {
    return ctor_iterator(decls_end());
  }

  /// An iterator over friend declarations.  All of these are defined
  /// in DeclFriend.h.
  class friend_iterator;
  using friend_range = llvm::iterator_range<friend_iterator>;

  friend_range friends() const;
  friend_iterator friend_begin() const;
  friend_iterator friend_end() const;
  void pushFriendDecl(FriendDecl *FD);

  /// Determines whether this record has any friends.
  bool hasFriends() const {
    return data().FirstFriend.isValid();
  }

  /// \c true if a defaulted copy constructor for this class would be
  /// deleted.
  bool defaultedCopyConstructorIsDeleted() const {
    assert((!needsOverloadResolutionForCopyConstructor() ||
            (data().DeclaredSpecialMembers & SMF_CopyConstructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedCopyConstructorIsDeleted;
  }

  /// \c true if a defaulted move constructor for this class would be
  /// deleted.
  bool defaultedMoveConstructorIsDeleted() const {
    assert((!needsOverloadResolutionForMoveConstructor() ||
            (data().DeclaredSpecialMembers & SMF_MoveConstructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedMoveConstructorIsDeleted;
  }

  /// \c true if a defaulted destructor for this class would be deleted.
  bool defaultedDestructorIsDeleted() const {
    assert((!needsOverloadResolutionForDestructor() ||
            (data().DeclaredSpecialMembers & SMF_Destructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedDestructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous copy constructor that is not deleted.
  bool hasSimpleCopyConstructor() const {
    return !hasUserDeclaredCopyConstructor() &&
           !data().DefaultedCopyConstructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous move constructor that is not deleted.
  bool hasSimpleMoveConstructor() const {
    return !hasUserDeclaredMoveConstructor() && hasMoveConstructor() &&
           !data().DefaultedMoveConstructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous move assignment operator that is not deleted.
  bool hasSimpleMoveAssignment() const {
    return !hasUserDeclaredMoveAssignment() && hasMoveAssignment() &&
           !data().DefaultedMoveAssignmentIsDeleted;
  }

  /// \c true if we know for sure that this class has an accessible
  /// destructor that is not deleted.
  bool hasSimpleDestructor() const {
    return !hasUserDeclaredDestructor() &&
           !data().DefaultedDestructorIsDeleted;
  }

  /// Determine whether this class has any default constructors.
  bool hasDefaultConstructor() const {
    return (data().DeclaredSpecialMembers & SMF_DefaultConstructor) ||
           needsImplicitDefaultConstructor();
  }

  /// Determine if we need to declare a default constructor for
  /// this class.
  ///
  /// This value is used for lazy creation of default constructors.
  bool needsImplicitDefaultConstructor() const {
    return !data().UserDeclaredConstructor &&
           !(data().DeclaredSpecialMembers & SMF_DefaultConstructor) &&
           (!isLambda() || lambdaIsDefaultConstructibleAndAssignable());
  }

  /// Determine whether this class has any user-declared constructors.
  ///
  /// When true, a default constructor will not be implicitly declared.
  bool hasUserDeclaredConstructor() const {
    return data().UserDeclaredConstructor;
  }

  /// Whether this class has a user-provided default constructor
  /// per C++11.
  bool hasUserProvidedDefaultConstructor() const {
    return data().UserProvidedDefaultConstructor;
  }

  /// Determine whether this class has a user-declared copy constructor.
  ///
  /// When false, a copy constructor will be implicitly declared.
  bool hasUserDeclaredCopyConstructor() const {
    return data().UserDeclaredSpecialMembers & SMF_CopyConstructor;
  }

  /// Determine whether this class needs an implicit copy
  /// constructor to be lazily declared.
  bool needsImplicitCopyConstructor() const {
    return !(data().DeclaredSpecialMembers & SMF_CopyConstructor);
  }

  /// Determine whether we need to eagerly declare a defaulted copy
  /// constructor for this class.
  bool needsOverloadResolutionForCopyConstructor() const {
    // C++17 [class.copy.ctor]p6:
    //   If the class definition declares a move constructor or move assignment
    //   operator, the implicitly declared copy constructor is defined as
    //   deleted.
    // In MSVC mode, sometimes a declared move assignment does not delete an
    // implicit copy constructor, so defer this choice to Sema.
    if (data().UserDeclaredSpecialMembers &
        (SMF_MoveConstructor | SMF_MoveAssignment))
      return true;
    return data().NeedOverloadResolutionForCopyConstructor;
  }

  /// Determine whether an implicit copy constructor for this type
  /// would have a parameter with a const-qualified reference type.
  bool implicitCopyConstructorHasConstParam() const {
    return data().ImplicitCopyConstructorCanHaveConstParamForNonVBase &&
           (isAbstract() ||
            data().ImplicitCopyConstructorCanHaveConstParamForVBase);
  }

  /// Determine whether this class has a copy constructor with
  /// a parameter type which is a reference to a const-qualified type.
  bool hasCopyConstructorWithConstParam() const {
    return data().HasDeclaredCopyConstructorWithConstParam ||
           (needsImplicitCopyConstructor() &&
            implicitCopyConstructorHasConstParam());
  }

  /// Whether this class has a user-declared move constructor or
  /// assignment operator.
  ///
  /// When false, a move constructor and assignment operator may be
  /// implicitly declared.
  bool hasUserDeclaredMoveOperation() const {
    return data().UserDeclaredSpecialMembers &
             (SMF_MoveConstructor | SMF_MoveAssignment);
  }

  /// Determine whether this class has had a move constructor
  /// declared by the user.
  bool hasUserDeclaredMoveConstructor() const {
    return data().UserDeclaredSpecialMembers & SMF_MoveConstructor;
  }

  /// Determine whether this class has a move constructor.
  bool hasMoveConstructor() const {
    return (data().DeclaredSpecialMembers & SMF_MoveConstructor) ||
           needsImplicitMoveConstructor();
  }

  /// Set that we attempted to declare an implicit copy
  /// constructor, but overload resolution failed so we deleted it.
  void setImplicitCopyConstructorIsDeleted() {
    assert((data().DefaultedCopyConstructorIsDeleted ||
            needsOverloadResolutionForCopyConstructor()) &&
           "Copy constructor should not be deleted");
    data().DefaultedCopyConstructorIsDeleted = true;
  }

  /// Set that we attempted to declare an implicit move
  /// constructor, but overload resolution failed so we deleted it.
  void setImplicitMoveConstructorIsDeleted() {
    assert((data().DefaultedMoveConstructorIsDeleted ||
            needsOverloadResolutionForMoveConstructor()) &&
           "move constructor should not be deleted");
    data().DefaultedMoveConstructorIsDeleted = true;
  }

  /// Set that we attempted to declare an implicit destructor,
  /// but overload resolution failed so we deleted it.
  void setImplicitDestructorIsDeleted() {
    assert((data().DefaultedDestructorIsDeleted ||
            needsOverloadResolutionForDestructor()) &&
           "destructor should not be deleted");
    data().DefaultedDestructorIsDeleted = true;
  }

  /// Determine whether this class should get an implicit move
  /// constructor or if any existing special member function inhibits this.
  bool needsImplicitMoveConstructor() const {
    return !(data().DeclaredSpecialMembers & SMF_MoveConstructor) &&
           !hasUserDeclaredCopyConstructor() &&
           !hasUserDeclaredCopyAssignment() &&
           !hasUserDeclaredMoveAssignment() &&
           !hasUserDeclaredDestructor();
  }

  /// Determine whether we need to eagerly declare a defaulted move
  /// constructor for this class.
  bool needsOverloadResolutionForMoveConstructor() const {
    return data().NeedOverloadResolutionForMoveConstructor;
  }

  /// Determine whether this class has a user-declared copy assignment
  /// operator.
  ///
  /// When false, a copy assignment operator will be implicitly declared.
  bool hasUserDeclaredCopyAssignment() const {
    return data().UserDeclaredSpecialMembers & SMF_CopyAssignment;
  }

  /// Determine whether this class needs an implicit copy
  /// assignment operator to be lazily declared.
  bool needsImplicitCopyAssignment() const {
    return !(data().DeclaredSpecialMembers & SMF_CopyAssignment);
  }

  /// Determine whether we need to eagerly declare a defaulted copy
  /// assignment operator for this class.
  bool needsOverloadResolutionForCopyAssignment() const {
    return data().HasMutableFields;
  }

  /// Determine whether an implicit copy assignment operator for this
  /// type would have a parameter with a const-qualified reference type.
  bool implicitCopyAssignmentHasConstParam() const {
    return data().ImplicitCopyAssignmentHasConstParam;
  }

  /// Determine whether this class has a copy assignment operator with
  /// a parameter type which is a reference to a const-qualified type or is not
  /// a reference.
  bool hasCopyAssignmentWithConstParam() const {
    return data().HasDeclaredCopyAssignmentWithConstParam ||
           (needsImplicitCopyAssignment() &&
            implicitCopyAssignmentHasConstParam());
  }

  /// Determine whether this class has had a move assignment
  /// declared by the user.
  bool hasUserDeclaredMoveAssignment() const {
    return data().UserDeclaredSpecialMembers & SMF_MoveAssignment;
  }

  /// Determine whether this class has a move assignment operator.
  bool hasMoveAssignment() const {
    return (data().DeclaredSpecialMembers & SMF_MoveAssignment) ||
           needsImplicitMoveAssignment();
  }

  /// Set that we attempted to declare an implicit move assignment
  /// operator, but overload resolution failed so we deleted it.
  void setImplicitMoveAssignmentIsDeleted() {
    assert((data().DefaultedMoveAssignmentIsDeleted ||
            needsOverloadResolutionForMoveAssignment()) &&
           "move assignment should not be deleted");
    data().DefaultedMoveAssignmentIsDeleted = true;
  }

  /// Determine whether this class should get an implicit move
  /// assignment operator or if any existing special member function inhibits
  /// this.
  bool needsImplicitMoveAssignment() const {
    return !(data().DeclaredSpecialMembers & SMF_MoveAssignment) &&
           !hasUserDeclaredCopyConstructor() &&
           !hasUserDeclaredCopyAssignment() &&
           !hasUserDeclaredMoveConstructor() &&
           !hasUserDeclaredDestructor() &&
           (!isLambda() || lambdaIsDefaultConstructibleAndAssignable());
  }

  /// Determine whether we need to eagerly declare a move assignment
  /// operator for this class.
  bool needsOverloadResolutionForMoveAssignment() const {
    return data().NeedOverloadResolutionForMoveAssignment;
  }

  /// Determine whether this class has a user-declared destructor.
  ///
  /// When false, a destructor will be implicitly declared.
  bool hasUserDeclaredDestructor() const {
    return data().UserDeclaredSpecialMembers & SMF_Destructor;
  }

  /// Determine whether this class needs an implicit destructor to
  /// be lazily declared.
  bool needsImplicitDestructor() const {
    return !(data().DeclaredSpecialMembers & SMF_Destructor);
  }

  /// Determine whether we need to eagerly declare a destructor for this
  /// class.
  bool needsOverloadResolutionForDestructor() const {
    return data().NeedOverloadResolutionForDestructor;
  }

  /// Determine whether this class describes a lambda function object.
  bool isLambda() const {
    // An update record can't turn a non-lambda into a lambda.
    auto *DD = DefinitionData;
    return DD && DD->IsLambda;
  }

  /// Determine whether this class describes a generic
  /// lambda function object (i.e. function call operator is
  /// a template).
  bool isGenericLambda() const;

  /// Determine whether this lambda should have an implicit default constructor
  /// and copy and move assignment operators.
  bool lambdaIsDefaultConstructibleAndAssignable() const;

  /// Retrieve the lambda call operator of the closure type
  /// if this is a closure type.
  CXXMethodDecl *getLambdaCallOperator() const;

  /// Retrieve the lambda static invoker, the address of which
  /// is returned by the conversion operator, and the body of which
  /// is forwarded to the lambda call operator.
  CXXMethodDecl *getLambdaStaticInvoker() const;

  /// Retrieve the generic lambda's template parameter list.
  /// Returns null if the class does not represent a lambda or a generic
  /// lambda.
  TemplateParameterList *getGenericLambdaTemplateParameterList() const;

  LambdaCaptureDefault getLambdaCaptureDefault() const {
    assert(isLambda());
    return static_cast<LambdaCaptureDefault>(getLambdaData().CaptureDefault);
  }

  /// For a closure type, retrieve the mapping from captured
  /// variables and \c this to the non-static data members that store the
  /// values or references of the captures.
  ///
  /// \param Captures Will be populated with the mapping from captured
  /// variables to the corresponding fields.
  ///
  /// \param ThisCapture Will be set to the field declaration for the
  /// \c this capture.
  ///
  /// \note No entries will be added for init-captures, as they do not capture
  /// variables.
  void getCaptureFields(llvm::DenseMap<const VarDecl *, FieldDecl *> &Captures,
                        FieldDecl *&ThisCapture) const;

  using capture_const_iterator = const LambdaCapture *;
  using capture_const_range = llvm::iterator_range<capture_const_iterator>;

  capture_const_range captures() const {
    return capture_const_range(captures_begin(), captures_end());
  }

  capture_const_iterator captures_begin() const {
    return isLambda() ? getLambdaData().Captures : nullptr;
  }

  capture_const_iterator captures_end() const {
    return isLambda() ? captures_begin() + getLambdaData().NumCaptures
                      : nullptr;
  }

  using conversion_iterator = UnresolvedSetIterator;

  conversion_iterator conversion_begin() const {
    return data().Conversions.get(getASTContext()).begin();
  }

  conversion_iterator conversion_end() const {
    return data().Conversions.get(getASTContext()).end();
  }

  /// Removes a conversion function from this class.  The conversion
  /// function must currently be a member of this class.  Furthermore,
  /// this class must currently be in the process of being defined.
  void removeConversion(const NamedDecl *Old);

  /// Get all conversion functions visible in current class,
  /// including conversion function templates.
  llvm::iterator_range<conversion_iterator> getVisibleConversionFunctions();

  /// Determine whether this class is an aggregate (C++ [dcl.init.aggr]),
  /// which is a class with no user-declared constructors, no private
  /// or protected non-static data members, no base classes, and no virtual
  /// functions (C++ [dcl.init.aggr]p1).
  bool isAggregate() const { return data().Aggregate; }

  /// Whether this class has any in-class initializers
  /// for non-static data members (including those in anonymous unions or
  /// structs).
  bool hasInClassInitializer() const { return data().HasInClassInitializer; }

  /// Whether this class or any of its subobjects has any members of
  /// reference type which would make value-initialization ill-formed.
  ///
  /// Per C++03 [dcl.init]p5:
  ///  - if T is a non-union class type without a user-declared constructor,
  ///    then every non-static data member and base-class component of T is
  ///    value-initialized [...] A program that calls for [...]
  ///    value-initialization of an entity of reference type is ill-formed.
  bool hasUninitializedReferenceMember() const {
    return !isUnion() && !hasUserDeclaredConstructor() &&
           data().HasUninitializedReferenceMember;
  }

  /// Whether this class is a POD-type (C++ [class]p4)
  ///
  /// For purposes of this function a class is POD if it is an aggregate
  /// that has no non-static non-POD data members, no reference data
  /// members, no user-defined copy assignment operator and no
  /// user-defined destructor.
  ///
  /// Note that this is the C++ TR1 definition of POD.
  bool isPOD() const { return data().PlainOldData; }

  /// True if this class is C-like, without C++-specific features, e.g.
  /// it contains only public fields, no bases, tag kind is not 'class', etc.
  bool isCLike() const;

  /// Determine whether this is an empty class in the sense of
  /// (C++11 [meta.unary.prop]).
  ///
  /// The CXXRecordDecl is a class type, but not a union type,
  /// with no non-static data members other than bit-fields of length 0,
  /// no virtual member functions, no virtual base classes,
  /// and no base class B for which is_empty<B>::value is false.
  ///
  /// \note This does NOT include a check for union-ness.
  bool isEmpty() const { return data().Empty; }

  /// Determine whether this class has direct non-static data members.
  bool hasDirectFields() const {
    auto &D = data();
    return D.HasPublicFields || D.HasProtectedFields || D.HasPrivateFields;
  }

  /// Whether this class is polymorphic (C++ [class.virtual]),
  /// which means that the class contains or inherits a virtual function.
  bool isPolymorphic() const { return data().Polymorphic; }

  /// Determine whether this class has a pure virtual function.
  ///
  /// The class is is abstract per (C++ [class.abstract]p2) if it declares
  /// a pure virtual function or inherits a pure virtual function that is
  /// not overridden.
  bool isAbstract() const { return data().Abstract; }

  /// Determine whether this class is standard-layout per
  /// C++ [class]p7.
  bool isStandardLayout() const { return data().IsStandardLayout; }

  /// Determine whether this class was standard-layout per
  /// C++11 [class]p7, specifically using the C++11 rules without any DRs.
  bool isCXX11StandardLayout() const { return data().IsCXX11StandardLayout; }

  /// Determine whether this class, or any of its class subobjects,
  /// contains a mutable field.
  bool hasMutableFields() const { return data().HasMutableFields; }

  /// Determine whether this class has any variant members.
  bool hasVariantMembers() const { return data().HasVariantMembers; }

  /// Determine whether this class has a trivial default constructor
  /// (C++11 [class.ctor]p5).
  bool hasTrivialDefaultConstructor() const {
    return hasDefaultConstructor() &&
           (data().HasTrivialSpecialMembers & SMF_DefaultConstructor);
  }

  /// Determine whether this class has a non-trivial default constructor
  /// (C++11 [class.ctor]p5).
  bool hasNonTrivialDefaultConstructor() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_DefaultConstructor) ||
           (needsImplicitDefaultConstructor() &&
            !(data().HasTrivialSpecialMembers & SMF_DefaultConstructor));
  }

  /// Determine whether this class has at least one constexpr constructor
  /// other than the copy or move constructors.
  bool hasConstexprNonCopyMoveConstructor() const {
    return data().HasConstexprNonCopyMoveConstructor ||
           (needsImplicitDefaultConstructor() &&
            defaultedDefaultConstructorIsConstexpr());
  }

  /// Determine whether a defaulted default constructor for this class
  /// would be constexpr.
  bool defaultedDefaultConstructorIsConstexpr() const {
    return data().DefaultedDefaultConstructorIsConstexpr &&
           (!isUnion() || hasInClassInitializer() || !hasVariantMembers());
  }

  /// Determine whether this class has a constexpr default constructor.
  bool hasConstexprDefaultConstructor() const {
    return data().HasConstexprDefaultConstructor ||
           (needsImplicitDefaultConstructor() &&
            defaultedDefaultConstructorIsConstexpr());
  }

  /// Determine whether this class has a trivial copy constructor
  /// (C++ [class.copy]p6, C++11 [class.copy]p12)
  bool hasTrivialCopyConstructor() const {
    return data().HasTrivialSpecialMembers & SMF_CopyConstructor;
  }

  bool hasTrivialCopyConstructorForCall() const {
    return data().HasTrivialSpecialMembersForCall & SMF_CopyConstructor;
  }

  /// Determine whether this class has a non-trivial copy constructor
  /// (C++ [class.copy]p6, C++11 [class.copy]p12)
  bool hasNonTrivialCopyConstructor() const {
    return data().DeclaredNonTrivialSpecialMembers & SMF_CopyConstructor ||
           !hasTrivialCopyConstructor();
  }

  bool hasNonTrivialCopyConstructorForCall() const {
    return (data().DeclaredNonTrivialSpecialMembersForCall &
            SMF_CopyConstructor) ||
           !hasTrivialCopyConstructorForCall();
  }

  /// Determine whether this class has a trivial move constructor
  /// (C++11 [class.copy]p12)
  bool hasTrivialMoveConstructor() const {
    return hasMoveConstructor() &&
           (data().HasTrivialSpecialMembers & SMF_MoveConstructor);
  }

  bool hasTrivialMoveConstructorForCall() const {
    return hasMoveConstructor() &&
           (data().HasTrivialSpecialMembersForCall & SMF_MoveConstructor);
  }

  /// Determine whether this class has a non-trivial move constructor
  /// (C++11 [class.copy]p12)
  bool hasNonTrivialMoveConstructor() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_MoveConstructor) ||
           (needsImplicitMoveConstructor() &&
            !(data().HasTrivialSpecialMembers & SMF_MoveConstructor));
  }

  bool hasNonTrivialMoveConstructorForCall() const {
    return (data().DeclaredNonTrivialSpecialMembersForCall &
            SMF_MoveConstructor) ||
           (needsImplicitMoveConstructor() &&
            !(data().HasTrivialSpecialMembersForCall & SMF_MoveConstructor));
  }

  /// Determine whether this class has a trivial copy assignment operator
  /// (C++ [class.copy]p11, C++11 [class.copy]p25)
  bool hasTrivialCopyAssignment() const {
    return data().HasTrivialSpecialMembers & SMF_CopyAssignment;
  }

  /// Determine whether this class has a non-trivial copy assignment
  /// operator (C++ [class.copy]p11, C++11 [class.copy]p25)
  bool hasNonTrivialCopyAssignment() const {
    return data().DeclaredNonTrivialSpecialMembers & SMF_CopyAssignment ||
           !hasTrivialCopyAssignment();
  }

  /// Determine whether this class has a trivial move assignment operator
  /// (C++11 [class.copy]p25)
  bool hasTrivialMoveAssignment() const {
    return hasMoveAssignment() &&
           (data().HasTrivialSpecialMembers & SMF_MoveAssignment);
  }

  /// Determine whether this class has a non-trivial move assignment
  /// operator (C++11 [class.copy]p25)
  bool hasNonTrivialMoveAssignment() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_MoveAssignment) ||
           (needsImplicitMoveAssignment() &&
            !(data().HasTrivialSpecialMembers & SMF_MoveAssignment));
  }

  /// Determine whether this class has a trivial destructor
  /// (C++ [class.dtor]p3)
  bool hasTrivialDestructor() const {
    return data().HasTrivialSpecialMembers & SMF_Destructor;
  }

  bool hasTrivialDestructorForCall() const {
    return data().HasTrivialSpecialMembersForCall & SMF_Destructor;
  }

  /// Determine whether this class has a non-trivial destructor
  /// (C++ [class.dtor]p3)
  bool hasNonTrivialDestructor() const {
    return !(data().HasTrivialSpecialMembers & SMF_Destructor);
  }

  bool hasNonTrivialDestructorForCall() const {
    return !(data().HasTrivialSpecialMembersForCall & SMF_Destructor);
  }

  void setHasTrivialSpecialMemberForCall() {
    data().HasTrivialSpecialMembersForCall =
        (SMF_CopyConstructor | SMF_MoveConstructor | SMF_Destructor);
  }

  /// Determine whether declaring a const variable with this type is ok
  /// per core issue 253.
  bool allowConstDefaultInit() const {
    return !data().HasUninitializedFields ||
           !(data().HasDefaultedDefaultConstructor ||
             needsImplicitDefaultConstructor());
  }

  /// Determine whether this class has a destructor which has no
  /// semantic effect.
  ///
  /// Any such destructor will be trivial, public, defaulted and not deleted,
  /// and will call only irrelevant destructors.
  bool hasIrrelevantDestructor() const {
    return data().HasIrrelevantDestructor;
  }

  /// Determine whether this class has a non-literal or/ volatile type
  /// non-static data member or base class.
  bool hasNonLiteralTypeFieldsOrBases() const {
    return data().HasNonLiteralTypeFieldsOrBases;
  }

  /// Determine whether this class has a using-declaration that names
  /// a user-declared base class constructor.
  bool hasInheritedConstructor() const {
    return data().HasInheritedConstructor;
  }

  /// Determine whether this class has a using-declaration that names
  /// a base class assignment operator.
  bool hasInheritedAssignment() const {
    return data().HasInheritedAssignment;
  }

  /// Determine whether this class is considered trivially copyable per
  /// (C++11 [class]p6).
  bool isTriviallyCopyable() const;

  /// Determine whether this class is considered trivial.
  ///
  /// C++11 [class]p6:
  ///    "A trivial class is a class that has a trivial default constructor and
  ///    is trivially copyable."
  bool isTrivial() const {
    return isTriviallyCopyable() && hasTrivialDefaultConstructor();
  }

  /// Determine whether this class is a literal type.
  ///
  /// C++11 [basic.types]p10:
  ///   A class type that has all the following properties:
  ///     - it has a trivial destructor
  ///     - every constructor call and full-expression in the
  ///       brace-or-equal-intializers for non-static data members (if any) is
  ///       a constant expression.
  ///     - it is an aggregate type or has at least one constexpr constructor
  ///       or constructor template that is not a copy or move constructor, and
  ///     - all of its non-static data members and base classes are of literal
  ///       types
  ///
  /// We resolve DR1361 by ignoring the second bullet. We resolve DR1452 by
  /// treating types with trivial default constructors as literal types.
  ///
  /// Only in C++17 and beyond, are lambdas literal types.
  bool isLiteral() const {
    return hasTrivialDestructor() &&
           (!isLambda() || getASTContext().getLangOpts().CPlusPlus17) &&
           !hasNonLiteralTypeFieldsOrBases() &&
           (isAggregate() || isLambda() ||
            hasConstexprNonCopyMoveConstructor() ||
            hasTrivialDefaultConstructor());
  }

  /// If this record is an instantiation of a member class,
  /// retrieves the member class from which it was instantiated.
  ///
  /// This routine will return non-null for (non-templated) member
  /// classes of class templates. For example, given:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   struct A { };
  /// };
  /// \endcode
  ///
  /// The declaration for X<int>::A is a (non-templated) CXXRecordDecl
  /// whose parent is the class template specialization X<int>. For
  /// this declaration, getInstantiatedFromMemberClass() will return
  /// the CXXRecordDecl X<T>::A. When a complete definition of
  /// X<int>::A is required, it will be instantiated from the
  /// declaration returned by getInstantiatedFromMemberClass().
  CXXRecordDecl *getInstantiatedFromMemberClass() const;

  /// If this class is an instantiation of a member class of a
  /// class template specialization, retrieves the member specialization
  /// information.
  MemberSpecializationInfo *getMemberSpecializationInfo() const;

  /// Specify that this record is an instantiation of the
  /// member class \p RD.
  void setInstantiationOfMemberClass(CXXRecordDecl *RD,
                                     TemplateSpecializationKind TSK);

  /// Retrieves the class template that is described by this
  /// class declaration.
  ///
  /// Every class template is represented as a ClassTemplateDecl and a
  /// CXXRecordDecl. The former contains template properties (such as
  /// the template parameter lists) while the latter contains the
  /// actual description of the template's
  /// contents. ClassTemplateDecl::getTemplatedDecl() retrieves the
  /// CXXRecordDecl that from a ClassTemplateDecl, while
  /// getDescribedClassTemplate() retrieves the ClassTemplateDecl from
  /// a CXXRecordDecl.
  ClassTemplateDecl *getDescribedClassTemplate() const;

  void setDescribedClassTemplate(ClassTemplateDecl *Template);

  /// Determine whether this particular class is a specialization or
  /// instantiation of a class template or member class of a class template,
  /// and how it was instantiated or specialized.
  TemplateSpecializationKind getTemplateSpecializationKind() const;

  /// Set the kind of specialization or template instantiation this is.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK);

  /// Retrieve the record declaration from which this record could be
  /// instantiated. Returns null if this class is not a template instantiation.
  const CXXRecordDecl *getTemplateInstantiationPattern() const;

  CXXRecordDecl *getTemplateInstantiationPattern() {
    return const_cast<CXXRecordDecl *>(const_cast<const CXXRecordDecl *>(this)
                                           ->getTemplateInstantiationPattern());
  }

  /// Returns the destructor decl for this class.
  CXXDestructorDecl *getDestructor() const;

  /// Returns true if the class destructor, or any implicitly invoked
  /// destructors are marked noreturn.
  bool isAnyDestructorNoReturn() const;

  /// If the class is a local class [class.local], returns
  /// the enclosing function declaration.
  const FunctionDecl *isLocalClass() const {
    if (const auto *RD = dyn_cast<CXXRecordDecl>(getDeclContext()))
      return RD->isLocalClass();

    return dyn_cast<FunctionDecl>(getDeclContext());
  }

  FunctionDecl *isLocalClass() {
    return const_cast<FunctionDecl*>(
        const_cast<const CXXRecordDecl*>(this)->isLocalClass());
  }

  /// Determine whether this dependent class is a current instantiation,
  /// when viewed from within the given context.
  bool isCurrentInstantiation(const DeclContext *CurContext) const;

  /// Determine whether this class is derived from the class \p Base.
  ///
  /// This routine only determines whether this class is derived from \p Base,
  /// but does not account for factors that may make a Derived -> Base class
  /// ill-formed, such as private/protected inheritance or multiple, ambiguous
  /// base class subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \returns true if this class is derived from Base, false otherwise.
  bool isDerivedFrom(const CXXRecordDecl *Base) const;

  /// Determine whether this class is derived from the type \p Base.
  ///
  /// This routine only determines whether this class is derived from \p Base,
  /// but does not account for factors that may make a Derived -> Base class
  /// ill-formed, such as private/protected inheritance or multiple, ambiguous
  /// base class subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \param Paths will contain the paths taken from the current class to the
  /// given \p Base class.
  ///
  /// \returns true if this class is derived from \p Base, false otherwise.
  ///
  /// \todo add a separate parameter to configure IsDerivedFrom, rather than
  /// tangling input and output in \p Paths
  bool isDerivedFrom(const CXXRecordDecl *Base, CXXBasePaths &Paths) const;

  /// Determine whether this class is virtually derived from
  /// the class \p Base.
  ///
  /// This routine only determines whether this class is virtually
  /// derived from \p Base, but does not account for factors that may
  /// make a Derived -> Base class ill-formed, such as
  /// private/protected inheritance or multiple, ambiguous base class
  /// subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \returns true if this class is virtually derived from Base,
  /// false otherwise.
  bool isVirtuallyDerivedFrom(const CXXRecordDecl *Base) const;

  /// Determine whether this class is provably not derived from
  /// the type \p Base.
  bool isProvablyNotDerivedFrom(const CXXRecordDecl *Base) const;

  /// Function type used by forallBases() as a callback.
  ///
  /// \param BaseDefinition the definition of the base class
  ///
  /// \returns true if this base matched the search criteria
  using ForallBasesCallback =
      llvm::function_ref<bool(const CXXRecordDecl *BaseDefinition)>;

  /// Determines if the given callback holds for all the direct
  /// or indirect base classes of this type.
  ///
  /// The class itself does not count as a base class.  This routine
  /// returns false if the class has non-computable base classes.
  ///
  /// \param BaseMatches Callback invoked for each (direct or indirect) base
  /// class of this type, or if \p AllowShortCircuit is true then until a call
  /// returns false.
  ///
  /// \param AllowShortCircuit if false, forces the callback to be called
  /// for every base class, even if a dependent or non-matching base was
  /// found.
  bool forallBases(ForallBasesCallback BaseMatches,
                   bool AllowShortCircuit = true) const;

  /// Function type used by lookupInBases() to determine whether a
  /// specific base class subobject matches the lookup criteria.
  ///
  /// \param Specifier the base-class specifier that describes the inheritance
  /// from the base class we are trying to match.
  ///
  /// \param Path the current path, from the most-derived class down to the
  /// base named by the \p Specifier.
  ///
  /// \returns true if this base matched the search criteria, false otherwise.
  using BaseMatchesCallback =
      llvm::function_ref<bool(const CXXBaseSpecifier *Specifier,
                              CXXBasePath &Path)>;

  /// Look for entities within the base classes of this C++ class,
  /// transitively searching all base class subobjects.
  ///
  /// This routine uses the callback function \p BaseMatches to find base
  /// classes meeting some search criteria, walking all base class subobjects
  /// and populating the given \p Paths structure with the paths through the
  /// inheritance hierarchy that resulted in a match. On a successful search,
  /// the \p Paths structure can be queried to retrieve the matching paths and
  /// to determine if there were any ambiguities.
  ///
  /// \param BaseMatches callback function used to determine whether a given
  /// base matches the user-defined search criteria.
  ///
  /// \param Paths used to record the paths from this class to its base class
  /// subobjects that match the search criteria.
  ///
  /// \param LookupInDependent can be set to true to extend the search to
  /// dependent base classes.
  ///
  /// \returns true if there exists any path from this class to a base class
  /// subobject that matches the search criteria.
  bool lookupInBases(BaseMatchesCallback BaseMatches, CXXBasePaths &Paths,
                     bool LookupInDependent = false) const;

  /// Base-class lookup callback that determines whether the given
  /// base class specifier refers to a specific class declaration.
  ///
  /// This callback can be used with \c lookupInBases() to determine whether
  /// a given derived class has is a base class subobject of a particular type.
  /// The base record pointer should refer to the canonical CXXRecordDecl of the
  /// base class that we are searching for.
  static bool FindBaseClass(const CXXBaseSpecifier *Specifier,
                            CXXBasePath &Path, const CXXRecordDecl *BaseRecord);

  /// Base-class lookup callback that determines whether the
  /// given base class specifier refers to a specific class
  /// declaration and describes virtual derivation.
  ///
  /// This callback can be used with \c lookupInBases() to determine
  /// whether a given derived class has is a virtual base class
  /// subobject of a particular type.  The base record pointer should
  /// refer to the canonical CXXRecordDecl of the base class that we
  /// are searching for.
  static bool FindVirtualBaseClass(const CXXBaseSpecifier *Specifier,
                                   CXXBasePath &Path,
                                   const CXXRecordDecl *BaseRecord);

  /// Base-class lookup callback that determines whether there exists
  /// a tag with the given name.
  ///
  /// This callback can be used with \c lookupInBases() to find tag members
  /// of the given name within a C++ class hierarchy.
  static bool FindTagMember(const CXXBaseSpecifier *Specifier,
                            CXXBasePath &Path, DeclarationName Name);

  /// Base-class lookup callback that determines whether there exists
  /// a member with the given name.
  ///
  /// This callback can be used with \c lookupInBases() to find members
  /// of the given name within a C++ class hierarchy.
  static bool FindOrdinaryMember(const CXXBaseSpecifier *Specifier,
                                 CXXBasePath &Path, DeclarationName Name);

  /// Base-class lookup callback that determines whether there exists
  /// a member with the given name.
  ///
  /// This callback can be used with \c lookupInBases() to find members
  /// of the given name within a C++ class hierarchy, including dependent
  /// classes.
  static bool
  FindOrdinaryMemberInDependentClasses(const CXXBaseSpecifier *Specifier,
                                       CXXBasePath &Path, DeclarationName Name);

  /// Base-class lookup callback that determines whether there exists
  /// an OpenMP declare reduction member with the given name.
  ///
  /// This callback can be used with \c lookupInBases() to find members
  /// of the given name within a C++ class hierarchy.
  static bool FindOMPReductionMember(const CXXBaseSpecifier *Specifier,
                                     CXXBasePath &Path, DeclarationName Name);

  /// Base-class lookup callback that determines whether there exists
  /// a member with the given name that can be used in a nested-name-specifier.
  ///
  /// This callback can be used with \c lookupInBases() to find members of
  /// the given name within a C++ class hierarchy that can occur within
  /// nested-name-specifiers.
  static bool FindNestedNameSpecifierMember(const CXXBaseSpecifier *Specifier,
                                            CXXBasePath &Path,
                                            DeclarationName Name);

  /// Retrieve the final overriders for each virtual member
  /// function in the class hierarchy where this class is the
  /// most-derived class in the class hierarchy.
  void getFinalOverriders(CXXFinalOverriderMap &FinaOverriders) const;

  /// Get the indirect primary bases for this class.
  void getIndirectPrimaryBases(CXXIndirectPrimaryBaseSet& Bases) const;

  /// Performs an imprecise lookup of a dependent name in this class.
  ///
  /// This function does not follow strict semantic rules and should be used
  /// only when lookup rules can be relaxed, e.g. indexing.
  std::vector<const NamedDecl *>
  lookupDependentName(const DeclarationName &Name,
                      llvm::function_ref<bool(const NamedDecl *ND)> Filter);

  /// Renders and displays an inheritance diagram
  /// for this C++ class and all of its base classes (transitively) using
  /// GraphViz.
  void viewInheritance(ASTContext& Context) const;

  /// Calculates the access of a decl that is reached
  /// along a path.
  static AccessSpecifier MergeAccess(AccessSpecifier PathAccess,
                                     AccessSpecifier DeclAccess) {
    assert(DeclAccess != AS_none);
    if (DeclAccess == AS_private) return AS_none;
    return (PathAccess > DeclAccess ? PathAccess : DeclAccess);
  }

  /// Indicates that the declaration of a defaulted or deleted special
  /// member function is now complete.
  void finishedDefaultedOrDeletedMember(CXXMethodDecl *MD);

  void setTrivialForCallFlags(CXXMethodDecl *MD);

  /// Indicates that the definition of this class is now complete.
  void completeDefinition() override;

  /// Indicates that the definition of this class is now complete,
  /// and provides a final overrider map to help determine
  ///
  /// \param FinalOverriders The final overrider map for this class, which can
  /// be provided as an optimization for abstract-class checking. If NULL,
  /// final overriders will be computed if they are needed to complete the
  /// definition.
  void completeDefinition(CXXFinalOverriderMap *FinalOverriders);

  /// Determine whether this class may end up being abstract, even though
  /// it is not yet known to be abstract.
  ///
  /// \returns true if this class is not known to be abstract but has any
  /// base classes that are abstract. In this case, \c completeDefinition()
  /// will need to compute final overriders to determine whether the class is
  /// actually abstract.
  bool mayBeAbstract() const;

  /// If this is the closure type of a lambda expression, retrieve the
  /// number to be used for name mangling in the Itanium C++ ABI.
  ///
  /// Zero indicates that this closure type has internal linkage, so the
  /// mangling number does not matter, while a non-zero value indicates which
  /// lambda expression this is in this particular context.
  unsigned getLambdaManglingNumber() const {
    assert(isLambda() && "Not a lambda closure type!");
    return getLambdaData().ManglingNumber;
  }

  /// Retrieve the declaration that provides additional context for a
  /// lambda, when the normal declaration context is not specific enough.
  ///
  /// Certain contexts (default arguments of in-class function parameters and
  /// the initializers of data members) have separate name mangling rules for
  /// lambdas within the Itanium C++ ABI. For these cases, this routine provides
  /// the declaration in which the lambda occurs, e.g., the function parameter
  /// or the non-static data member. Otherwise, it returns NULL to imply that
  /// the declaration context suffices.
  Decl *getLambdaContextDecl() const;

  /// Set the mangling number and context declaration for a lambda
  /// class.
  void setLambdaMangling(unsigned ManglingNumber, Decl *ContextDecl) {
    getLambdaData().ManglingNumber = ManglingNumber;
    getLambdaData().ContextDecl = ContextDecl;
  }

  /// Returns the inheritance model used for this record.
  MSInheritanceAttr::Spelling getMSInheritanceModel() const;

  /// Calculate what the inheritance model would be for this class.
  MSInheritanceAttr::Spelling calculateInheritanceModel() const;

  /// In the Microsoft C++ ABI, use zero for the field offset of a null data
  /// member pointer if we can guarantee that zero is not a valid field offset,
  /// or if the member pointer has multiple fields.  Polymorphic classes have a
  /// vfptr at offset zero, so we can use zero for null.  If there are multiple
  /// fields, we can use zero even if it is a valid field offset because
  /// null-ness testing will check the other fields.
  bool nullFieldOffsetIsZero() const {
    return !MSInheritanceAttr::hasOnlyOneField(/*IsMemberFunction=*/false,
                                               getMSInheritanceModel()) ||
           (hasDefinition() && isPolymorphic());
  }

  /// Controls when vtordisps will be emitted if this record is used as a
  /// virtual base.
  MSVtorDispAttr::Mode getMSVtorDispMode() const;

  /// Determine whether this lambda expression was known to be dependent
  /// at the time it was created, even if its context does not appear to be
  /// dependent.
  ///
  /// This flag is a workaround for an issue with parsing, where default
  /// arguments are parsed before their enclosing function declarations have
  /// been created. This means that any lambda expressions within those
  /// default arguments will have as their DeclContext the context enclosing
  /// the function declaration, which may be non-dependent even when the
  /// function declaration itself is dependent. This flag indicates when we
  /// know that the lambda is dependent despite that.
  bool isDependentLambda() const {
    return isLambda() && getLambdaData().Dependent;
  }

  TypeSourceInfo *getLambdaTypeInfo() const {
    return getLambdaData().MethodTyInfo;
  }

  // Determine whether this type is an Interface Like type for
  // __interface inheritance purposes.
  bool isInterfaceLike() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstCXXRecord && K <= lastCXXRecord;
  }
};

/// Represents a C++ deduction guide declaration.
///
/// \code
/// template<typename T> struct A { A(); A(T); };
/// A() -> A<int>;
/// \endcode
///
/// In this example, there will be an explicit deduction guide from the
/// second line, and implicit deduction guide templates synthesized from
/// the constructors of \c A.
class CXXDeductionGuideDecl : public FunctionDecl {
  void anchor() override;

private:
  CXXDeductionGuideDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                        bool IsExplicit, const DeclarationNameInfo &NameInfo,
                        QualType T, TypeSourceInfo *TInfo,
                        SourceLocation EndLocation)
      : FunctionDecl(CXXDeductionGuide, C, DC, StartLoc, NameInfo, T, TInfo,
                     SC_None, false, false) {
    if (EndLocation.isValid())
      setRangeEnd(EndLocation);
    setExplicitSpecified(IsExplicit);
    setIsCopyDeductionCandidate(false);
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static CXXDeductionGuideDecl *Create(ASTContext &C, DeclContext *DC,
                                       SourceLocation StartLoc, bool IsExplicit,
                                       const DeclarationNameInfo &NameInfo,
                                       QualType T, TypeSourceInfo *TInfo,
                                       SourceLocation EndLocation);

  static CXXDeductionGuideDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Whether this deduction guide is explicit.
  bool isExplicit() const { return isExplicitSpecified(); }

  /// Get the template for which this guide performs deduction.
  TemplateDecl *getDeducedTemplate() const {
    return getDeclName().getCXXDeductionGuideTemplate();
  }

  void setIsCopyDeductionCandidate(bool isCDC = true) {
    FunctionDeclBits.IsCopyDeductionCandidate = isCDC;
  }

  bool isCopyDeductionCandidate() const {
    return FunctionDeclBits.IsCopyDeductionCandidate;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXDeductionGuide; }
};

/// Represents a static or instance method of a struct/union/class.
///
/// In the terminology of the C++ Standard, these are the (static and
/// non-static) member functions, whether virtual or not.
class CXXMethodDecl : public FunctionDecl {
  void anchor() override;

protected:
  CXXMethodDecl(Kind DK, ASTContext &C, CXXRecordDecl *RD,
                SourceLocation StartLoc, const DeclarationNameInfo &NameInfo,
                QualType T, TypeSourceInfo *TInfo,
                StorageClass SC, bool isInline,
                bool isConstexpr, SourceLocation EndLocation)
    : FunctionDecl(DK, C, RD, StartLoc, NameInfo, T, TInfo,
                   SC, isInline, isConstexpr) {
    if (EndLocation.isValid())
      setRangeEnd(EndLocation);
  }

public:
  static CXXMethodDecl *Create(ASTContext &C, CXXRecordDecl *RD,
                               SourceLocation StartLoc,
                               const DeclarationNameInfo &NameInfo,
                               QualType T, TypeSourceInfo *TInfo,
                               StorageClass SC,
                               bool isInline,
                               bool isConstexpr,
                               SourceLocation EndLocation);

  static CXXMethodDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  bool isStatic() const;
  bool isInstance() const { return !isStatic(); }

  /// Returns true if the given operator is implicitly static in a record
  /// context.
  static bool isStaticOverloadedOperator(OverloadedOperatorKind OOK) {
    // [class.free]p1:
    // Any allocation function for a class T is a static member
    // (even if not explicitly declared static).
    // [class.free]p6 Any deallocation function for a class X is a static member
    // (even if not explicitly declared static).
    return OOK == OO_New || OOK == OO_Array_New || OOK == OO_Delete ||
           OOK == OO_Array_Delete;
  }

  bool isConst() const { return getType()->castAs<FunctionType>()->isConst(); }
  bool isVolatile() const { return getType()->castAs<FunctionType>()->isVolatile(); }

  bool isVirtual() const {
    CXXMethodDecl *CD = const_cast<CXXMethodDecl*>(this)->getCanonicalDecl();

    // Member function is virtual if it is marked explicitly so, or if it is
    // declared in __interface -- then it is automatically pure virtual.
    if (CD->isVirtualAsWritten() || CD->isPure())
      return true;

    return CD->size_overridden_methods() != 0;
  }

  /// If it's possible to devirtualize a call to this method, return the called
  /// function. Otherwise, return null.

  /// \param Base The object on which this virtual function is called.
  /// \param IsAppleKext True if we are compiling for Apple kext.
  CXXMethodDecl *getDevirtualizedMethod(const Expr *Base, bool IsAppleKext);

  const CXXMethodDecl *getDevirtualizedMethod(const Expr *Base,
                                              bool IsAppleKext) const {
    return const_cast<CXXMethodDecl *>(this)->getDevirtualizedMethod(
        Base, IsAppleKext);
  }

  /// Determine whether this is a usual deallocation function (C++
  /// [basic.stc.dynamic.deallocation]p2), which is an overloaded delete or
  /// delete[] operator with a particular signature. Populates \p PreventedBy
  /// with the declarations of the functions of the same kind if they were the
  /// reason for this function returning false. This is used by
  /// Sema::isUsualDeallocationFunction to reconsider the answer based on the
  /// context.
  bool isUsualDeallocationFunction(
      SmallVectorImpl<const FunctionDecl *> &PreventedBy) const;

  /// Determine whether this is a copy-assignment operator, regardless
  /// of whether it was declared implicitly or explicitly.
  bool isCopyAssignmentOperator() const;

  /// Determine whether this is a move assignment operator.
  bool isMoveAssignmentOperator() const;

  CXXMethodDecl *getCanonicalDecl() override {
    return cast<CXXMethodDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXMethodDecl *getCanonicalDecl() const {
    return const_cast<CXXMethodDecl*>(this)->getCanonicalDecl();
  }

  CXXMethodDecl *getMostRecentDecl() {
    return cast<CXXMethodDecl>(
            static_cast<FunctionDecl *>(this)->getMostRecentDecl());
  }
  const CXXMethodDecl *getMostRecentDecl() const {
    return const_cast<CXXMethodDecl*>(this)->getMostRecentDecl();
  }

  /// True if this method is user-declared and was not
  /// deleted or defaulted on its first declaration.
  bool isUserProvided() const {
    auto *DeclAsWritten = this;
    if (auto *Pattern = getTemplateInstantiationPattern())
      DeclAsWritten = cast<CXXMethodDecl>(Pattern);
    return !(DeclAsWritten->isDeleted() ||
             DeclAsWritten->getCanonicalDecl()->isDefaulted());
  }

  void addOverriddenMethod(const CXXMethodDecl *MD);

  using method_iterator = const CXXMethodDecl *const *;

  method_iterator begin_overridden_methods() const;
  method_iterator end_overridden_methods() const;
  unsigned size_overridden_methods() const;

  using overridden_method_range= ASTContext::overridden_method_range;

  overridden_method_range overridden_methods() const;

  /// Returns the parent of this method declaration, which
  /// is the class in which this method is defined.
  const CXXRecordDecl *getParent() const {
    return cast<CXXRecordDecl>(FunctionDecl::getParent());
  }

  /// Returns the parent of this method declaration, which
  /// is the class in which this method is defined.
  CXXRecordDecl *getParent() {
    return const_cast<CXXRecordDecl *>(
             cast<CXXRecordDecl>(FunctionDecl::getParent()));
  }

  /// Returns the type of the \c this pointer.
  ///
  /// Should only be called for instance (i.e., non-static) methods. Note
  /// that for the call operator of a lambda closure type, this returns the
  /// desugared 'this' type (a pointer to the closure type), not the captured
  /// 'this' type.
  QualType getThisType() const;

  static QualType getThisType(const FunctionProtoType *FPT,
                              const CXXRecordDecl *Decl);

  Qualifiers getTypeQualifiers() const {
    return getType()->getAs<FunctionProtoType>()->getTypeQuals();
  }

  /// Retrieve the ref-qualifier associated with this method.
  ///
  /// In the following example, \c f() has an lvalue ref-qualifier, \c g()
  /// has an rvalue ref-qualifier, and \c h() has no ref-qualifier.
  /// @code
  /// struct X {
  ///   void f() &;
  ///   void g() &&;
  ///   void h();
  /// };
  /// @endcode
  RefQualifierKind getRefQualifier() const {
    return getType()->getAs<FunctionProtoType>()->getRefQualifier();
  }

  bool hasInlineBody() const;

  /// Determine whether this is a lambda closure type's static member
  /// function that is used for the result of the lambda's conversion to
  /// function pointer (for a lambda with no captures).
  ///
  /// The function itself, if used, will have a placeholder body that will be
  /// supplied by IR generation to either forward to the function call operator
  /// or clone the function call operator.
  bool isLambdaStaticInvoker() const;

  /// Find the method in \p RD that corresponds to this one.
  ///
  /// Find if \p RD or one of the classes it inherits from override this method.
  /// If so, return it. \p RD is assumed to be a subclass of the class defining
  /// this method (or be the class itself), unless \p MayBeBase is set to true.
  CXXMethodDecl *
  getCorrespondingMethodInClass(const CXXRecordDecl *RD,
                                bool MayBeBase = false);

  const CXXMethodDecl *
  getCorrespondingMethodInClass(const CXXRecordDecl *RD,
                                bool MayBeBase = false) const {
    return const_cast<CXXMethodDecl *>(this)
              ->getCorrespondingMethodInClass(RD, MayBeBase);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstCXXMethod && K <= lastCXXMethod;
  }
};

/// Represents a C++ base or member initializer.
///
/// This is part of a constructor initializer that
/// initializes one non-static member variable or one base class. For
/// example, in the following, both 'A(a)' and 'f(3.14159)' are member
/// initializers:
///
/// \code
/// class A { };
/// class B : public A {
///   float f;
/// public:
///   B(A& a) : A(a), f(3.14159) { }
/// };
/// \endcode
class CXXCtorInitializer final {
  /// Either the base class name/delegating constructor type (stored as
  /// a TypeSourceInfo*), an normal field (FieldDecl), or an anonymous field
  /// (IndirectFieldDecl*) being initialized.
  llvm::PointerUnion3<TypeSourceInfo *, FieldDecl *, IndirectFieldDecl *>
    Initializee;

  /// The source location for the field name or, for a base initializer
  /// pack expansion, the location of the ellipsis.
  ///
  /// In the case of a delegating
  /// constructor, it will still include the type's source location as the
  /// Initializee points to the CXXConstructorDecl (to allow loop detection).
  SourceLocation MemberOrEllipsisLocation;

  /// The argument used to initialize the base or member, which may
  /// end up constructing an object (when multiple arguments are involved).
  Stmt *Init;

  /// Location of the left paren of the ctor-initializer.
  SourceLocation LParenLoc;

  /// Location of the right paren of the ctor-initializer.
  SourceLocation RParenLoc;

  /// If the initializee is a type, whether that type makes this
  /// a delegating initialization.
  unsigned IsDelegating : 1;

  /// If the initializer is a base initializer, this keeps track
  /// of whether the base is virtual or not.
  unsigned IsVirtual : 1;

  /// Whether or not the initializer is explicitly written
  /// in the sources.
  unsigned IsWritten : 1;

  /// If IsWritten is true, then this number keeps track of the textual order
  /// of this initializer in the original sources, counting from 0.
  unsigned SourceOrder : 13;

public:
  /// Creates a new base-class initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, TypeSourceInfo *TInfo, bool IsVirtual,
                     SourceLocation L, Expr *Init, SourceLocation R,
                     SourceLocation EllipsisLoc);

  /// Creates a new member initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, FieldDecl *Member,
                     SourceLocation MemberLoc, SourceLocation L, Expr *Init,
                     SourceLocation R);

  /// Creates a new anonymous field initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, IndirectFieldDecl *Member,
                     SourceLocation MemberLoc, SourceLocation L, Expr *Init,
                     SourceLocation R);

  /// Creates a new delegating initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, TypeSourceInfo *TInfo,
                     SourceLocation L, Expr *Init, SourceLocation R);

  /// \return Unique reproducible object identifier.
  int64_t getID(const ASTContext &Context) const;

  /// Determine whether this initializer is initializing a base class.
  bool isBaseInitializer() const {
    return Initializee.is<TypeSourceInfo*>() && !IsDelegating;
  }

  /// Determine whether this initializer is initializing a non-static
  /// data member.
  bool isMemberInitializer() const { return Initializee.is<FieldDecl*>(); }

  bool isAnyMemberInitializer() const {
    return isMemberInitializer() || isIndirectMemberInitializer();
  }

  bool isIndirectMemberInitializer() const {
    return Initializee.is<IndirectFieldDecl*>();
  }

  /// Determine whether this initializer is an implicit initializer
  /// generated for a field with an initializer defined on the member
  /// declaration.
  ///
  /// In-class member initializers (also known as "non-static data member
  /// initializations", NSDMIs) were introduced in C++11.
  bool isInClassMemberInitializer() const {
    return Init->getStmtClass() == Stmt::CXXDefaultInitExprClass;
  }

  /// Determine whether this initializer is creating a delegating
  /// constructor.
  bool isDelegatingInitializer() const {
    return Initializee.is<TypeSourceInfo*>() && IsDelegating;
  }

  /// Determine whether this initializer is a pack expansion.
  bool isPackExpansion() const {
    return isBaseInitializer() && MemberOrEllipsisLocation.isValid();
  }

  // For a pack expansion, returns the location of the ellipsis.
  SourceLocation getEllipsisLoc() const {
    assert(isPackExpansion() && "Initializer is not a pack expansion");
    return MemberOrEllipsisLocation;
  }

  /// If this is a base class initializer, returns the type of the
  /// base class with location information. Otherwise, returns an NULL
  /// type location.
  TypeLoc getBaseClassLoc() const;

  /// If this is a base class initializer, returns the type of the base class.
  /// Otherwise, returns null.
  const Type *getBaseClass() const;

  /// Returns whether the base is virtual or not.
  bool isBaseVirtual() const {
    assert(isBaseInitializer() && "Must call this on base initializer!");

    return IsVirtual;
  }

  /// Returns the declarator information for a base class or delegating
  /// initializer.
  TypeSourceInfo *getTypeSourceInfo() const {
    return Initializee.dyn_cast<TypeSourceInfo *>();
  }

  /// If this is a member initializer, returns the declaration of the
  /// non-static data member being initialized. Otherwise, returns null.
  FieldDecl *getMember() const {
    if (isMemberInitializer())
      return Initializee.get<FieldDecl*>();
    return nullptr;
  }

  FieldDecl *getAnyMember() const {
    if (isMemberInitializer())
      return Initializee.get<FieldDecl*>();
    if (isIndirectMemberInitializer())
      return Initializee.get<IndirectFieldDecl*>()->getAnonField();
    return nullptr;
  }

  IndirectFieldDecl *getIndirectMember() const {
    if (isIndirectMemberInitializer())
      return Initializee.get<IndirectFieldDecl*>();
    return nullptr;
  }

  SourceLocation getMemberLocation() const {
    return MemberOrEllipsisLocation;
  }

  /// Determine the source location of the initializer.
  SourceLocation getSourceLocation() const;

  /// Determine the source range covering the entire initializer.
  SourceRange getSourceRange() const LLVM_READONLY;

  /// Determine whether this initializer is explicitly written
  /// in the source code.
  bool isWritten() const { return IsWritten; }

  /// Return the source position of the initializer, counting from 0.
  /// If the initializer was implicit, -1 is returned.
  int getSourceOrder() const {
    return IsWritten ? static_cast<int>(SourceOrder) : -1;
  }

  /// Set the source order of this initializer.
  ///
  /// This can only be called once for each initializer; it cannot be called
  /// on an initializer having a positive number of (implicit) array indices.
  ///
  /// This assumes that the initializer was written in the source code, and
  /// ensures that isWritten() returns true.
  void setSourceOrder(int Pos) {
    assert(!IsWritten &&
           "setSourceOrder() used on implicit initializer");
    assert(SourceOrder == 0 &&
           "calling twice setSourceOrder() on the same initializer");
    assert(Pos >= 0 &&
           "setSourceOrder() used to make an initializer implicit");
    IsWritten = true;
    SourceOrder = static_cast<unsigned>(Pos);
  }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  /// Get the initializer.
  Expr *getInit() const { return static_cast<Expr *>(Init); }
};

/// Description of a constructor that was inherited from a base class.
class InheritedConstructor {
  ConstructorUsingShadowDecl *Shadow = nullptr;
  CXXConstructorDecl *BaseCtor = nullptr;

public:
  InheritedConstructor() = default;
  InheritedConstructor(ConstructorUsingShadowDecl *Shadow,
                       CXXConstructorDecl *BaseCtor)
      : Shadow(Shadow), BaseCtor(BaseCtor) {}

  explicit operator bool() const { return Shadow; }

  ConstructorUsingShadowDecl *getShadowDecl() const { return Shadow; }
  CXXConstructorDecl *getConstructor() const { return BaseCtor; }
};

/// Represents a C++ constructor within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   explicit X(int); // represented by a CXXConstructorDecl.
/// };
/// \endcode
class CXXConstructorDecl final
    : public CXXMethodDecl,
      private llvm::TrailingObjects<CXXConstructorDecl, InheritedConstructor> {
  // This class stores some data in DeclContext::CXXConstructorDeclBits
  // to save some space. Use the provided accessors to access it.

  /// \name Support for base and member initializers.
  /// \{
  /// The arguments used to initialize the base or member.
  LazyCXXCtorInitializersPtr CtorInitializers;

  CXXConstructorDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                     const DeclarationNameInfo &NameInfo,
                     QualType T, TypeSourceInfo *TInfo,
                     bool isExplicitSpecified, bool isInline,
                     bool isImplicitlyDeclared, bool isConstexpr,
                     InheritedConstructor Inherited);

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  static CXXConstructorDecl *CreateDeserialized(ASTContext &C, unsigned ID,
                                                bool InheritsConstructor);
  static CXXConstructorDecl *
  Create(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         bool isExplicit, bool isInline, bool isImplicitlyDeclared,
         bool isConstexpr,
         InheritedConstructor Inherited = InheritedConstructor());

  /// Iterates through the member/base initializer list.
  using init_iterator = CXXCtorInitializer **;

  /// Iterates through the member/base initializer list.
  using init_const_iterator = CXXCtorInitializer *const *;

  using init_range = llvm::iterator_range<init_iterator>;
  using init_const_range = llvm::iterator_range<init_const_iterator>;

  init_range inits() { return init_range(init_begin(), init_end()); }
  init_const_range inits() const {
    return init_const_range(init_begin(), init_end());
  }

  /// Retrieve an iterator to the first initializer.
  init_iterator init_begin() {
    const auto *ConstThis = this;
    return const_cast<init_iterator>(ConstThis->init_begin());
  }

  /// Retrieve an iterator to the first initializer.
  init_const_iterator init_begin() const;

  /// Retrieve an iterator past the last initializer.
  init_iterator       init_end()       {
    return init_begin() + getNumCtorInitializers();
  }

  /// Retrieve an iterator past the last initializer.
  init_const_iterator init_end() const {
    return init_begin() + getNumCtorInitializers();
  }

  using init_reverse_iterator = std::reverse_iterator<init_iterator>;
  using init_const_reverse_iterator =
      std::reverse_iterator<init_const_iterator>;

  init_reverse_iterator init_rbegin() {
    return init_reverse_iterator(init_end());
  }
  init_const_reverse_iterator init_rbegin() const {
    return init_const_reverse_iterator(init_end());
  }

  init_reverse_iterator init_rend() {
    return init_reverse_iterator(init_begin());
  }
  init_const_reverse_iterator init_rend() const {
    return init_const_reverse_iterator(init_begin());
  }

  /// Determine the number of arguments used to initialize the member
  /// or base.
  unsigned getNumCtorInitializers() const {
      return CXXConstructorDeclBits.NumCtorInitializers;
  }

  void setNumCtorInitializers(unsigned numCtorInitializers) {
    CXXConstructorDeclBits.NumCtorInitializers = numCtorInitializers;
    // This assert added because NumCtorInitializers is stored
    // in CXXConstructorDeclBits as a bitfield and its width has
    // been shrunk from 32 bits to fit into CXXConstructorDeclBitfields.
    assert(CXXConstructorDeclBits.NumCtorInitializers ==
           numCtorInitializers && "NumCtorInitializers overflow!");
  }

  void setCtorInitializers(CXXCtorInitializer **Initializers) {
    CtorInitializers = Initializers;
  }

  /// Whether this function is explicit.
  bool isExplicit() const {
    return getCanonicalDecl()->isExplicitSpecified();
  }

  /// Determine whether this constructor is a delegating constructor.
  bool isDelegatingConstructor() const {
    return (getNumCtorInitializers() == 1) &&
           init_begin()[0]->isDelegatingInitializer();
  }

  /// When this constructor delegates to another, retrieve the target.
  CXXConstructorDecl *getTargetConstructor() const;

  /// Whether this constructor is a default
  /// constructor (C++ [class.ctor]p5), which can be used to
  /// default-initialize a class of this type.
  bool isDefaultConstructor() const;

  /// Whether this constructor is a copy constructor (C++ [class.copy]p2,
  /// which can be used to copy the class.
  ///
  /// \p TypeQuals will be set to the qualifiers on the
  /// argument type. For example, \p TypeQuals would be set to \c
  /// Qualifiers::Const for the following copy constructor:
  ///
  /// \code
  /// class X {
  /// public:
  ///   X(const X&);
  /// };
  /// \endcode
  bool isCopyConstructor(unsigned &TypeQuals) const;

  /// Whether this constructor is a copy
  /// constructor (C++ [class.copy]p2, which can be used to copy the
  /// class.
  bool isCopyConstructor() const {
    unsigned TypeQuals = 0;
    return isCopyConstructor(TypeQuals);
  }

  /// Determine whether this constructor is a move constructor
  /// (C++11 [class.copy]p3), which can be used to move values of the class.
  ///
  /// \param TypeQuals If this constructor is a move constructor, will be set
  /// to the type qualifiers on the referent of the first parameter's type.
  bool isMoveConstructor(unsigned &TypeQuals) const;

  /// Determine whether this constructor is a move constructor
  /// (C++11 [class.copy]p3), which can be used to move values of the class.
  bool isMoveConstructor() const {
    unsigned TypeQuals = 0;
    return isMoveConstructor(TypeQuals);
  }

  /// Determine whether this is a copy or move constructor.
  ///
  /// \param TypeQuals Will be set to the type qualifiers on the reference
  /// parameter, if in fact this is a copy or move constructor.
  bool isCopyOrMoveConstructor(unsigned &TypeQuals) const;

  /// Determine whether this a copy or move constructor.
  bool isCopyOrMoveConstructor() const {
    unsigned Quals;
    return isCopyOrMoveConstructor(Quals);
  }

  /// Whether this constructor is a
  /// converting constructor (C++ [class.conv.ctor]), which can be
  /// used for user-defined conversions.
  bool isConvertingConstructor(bool AllowExplicit) const;

  /// Determine whether this is a member template specialization that
  /// would copy the object to itself. Such constructors are never used to copy
  /// an object.
  bool isSpecializationCopyingObject() const;

  /// Determine whether this is an implicit constructor synthesized to
  /// model a call to a constructor inherited from a base class.
  bool isInheritingConstructor() const {
    return CXXConstructorDeclBits.IsInheritingConstructor;
  }

  /// State that this is an implicit constructor synthesized to
  /// model a call to a constructor inherited from a base class.
  void setInheritingConstructor(bool isIC = true) {
    CXXConstructorDeclBits.IsInheritingConstructor = isIC;
  }

  /// Get the constructor that this inheriting constructor is based on.
  InheritedConstructor getInheritedConstructor() const {
    return isInheritingConstructor() ?
      *getTrailingObjects<InheritedConstructor>() : InheritedConstructor();
  }

  CXXConstructorDecl *getCanonicalDecl() override {
    return cast<CXXConstructorDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXConstructorDecl *getCanonicalDecl() const {
    return const_cast<CXXConstructorDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXConstructor; }
};

/// Represents a C++ destructor within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   ~X(); // represented by a CXXDestructorDecl.
/// };
/// \endcode
class CXXDestructorDecl : public CXXMethodDecl {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  // FIXME: Don't allocate storage for these except in the first declaration
  // of a virtual destructor.
  FunctionDecl *OperatorDelete = nullptr;
  Expr *OperatorDeleteThisArg = nullptr;

  CXXDestructorDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                    const DeclarationNameInfo &NameInfo,
                    QualType T, TypeSourceInfo *TInfo,
                    bool isInline, bool isImplicitlyDeclared)
    : CXXMethodDecl(CXXDestructor, C, RD, StartLoc, NameInfo, T, TInfo,
                    SC_None, isInline, /*isConstexpr=*/false, SourceLocation())
  {
    setImplicit(isImplicitlyDeclared);
  }

  void anchor() override;

public:
  static CXXDestructorDecl *Create(ASTContext &C, CXXRecordDecl *RD,
                                   SourceLocation StartLoc,
                                   const DeclarationNameInfo &NameInfo,
                                   QualType T, TypeSourceInfo* TInfo,
                                   bool isInline,
                                   bool isImplicitlyDeclared);
  static CXXDestructorDecl *CreateDeserialized(ASTContext & C, unsigned ID);

  void setOperatorDelete(FunctionDecl *OD, Expr *ThisArg);

  const FunctionDecl *getOperatorDelete() const {
    return getCanonicalDecl()->OperatorDelete;
  }

  Expr *getOperatorDeleteThisArg() const {
    return getCanonicalDecl()->OperatorDeleteThisArg;
  }

  CXXDestructorDecl *getCanonicalDecl() override {
    return cast<CXXDestructorDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXDestructorDecl *getCanonicalDecl() const {
    return const_cast<CXXDestructorDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXDestructor; }
};

/// Represents a C++ conversion function within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   operator bool();
/// };
/// \endcode
class CXXConversionDecl : public CXXMethodDecl {
  CXXConversionDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                    const DeclarationNameInfo &NameInfo, QualType T,
                    TypeSourceInfo *TInfo, bool isInline,
                    bool isExplicitSpecified, bool isConstexpr,
                    SourceLocation EndLocation)
      : CXXMethodDecl(CXXConversion, C, RD, StartLoc, NameInfo, T, TInfo,
                      SC_None, isInline, isConstexpr, EndLocation) {
    setExplicitSpecified(isExplicitSpecified);
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static CXXConversionDecl *Create(ASTContext &C, CXXRecordDecl *RD,
                                   SourceLocation StartLoc,
                                   const DeclarationNameInfo &NameInfo,
                                   QualType T, TypeSourceInfo *TInfo,
                                   bool isInline, bool isExplicit,
                                   bool isConstexpr,
                                   SourceLocation EndLocation);
  static CXXConversionDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Whether this function is explicit.
  bool isExplicit() const {
    return getCanonicalDecl()->isExplicitSpecified();
  }

  /// Returns the type that this conversion function is converting to.
  QualType getConversionType() const {
    return getType()->getAs<FunctionType>()->getReturnType();
  }

  /// Determine whether this conversion function is a conversion from
  /// a lambda closure type to a block pointer.
  bool isLambdaToBlockPointerConversion() const;

  CXXConversionDecl *getCanonicalDecl() override {
    return cast<CXXConversionDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXConversionDecl *getCanonicalDecl() const {
    return const_cast<CXXConversionDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXConversion; }
};

/// Represents a linkage specification.
///
/// For example:
/// \code
///   extern "C" void foo();
/// \endcode
class LinkageSpecDecl : public Decl, public DeclContext {
  virtual void anchor();
  // This class stores some data in DeclContext::LinkageSpecDeclBits to save
  // some space. Use the provided accessors to access it.
public:
  /// Represents the language in a linkage specification.
  ///
  /// The values are part of the serialization ABI for
  /// ASTs and cannot be changed without altering that ABI.  To help
  /// ensure a stable ABI for this, we choose the DW_LANG_ encodings
  /// from the dwarf standard.
  enum LanguageIDs {
    lang_c = /* DW_LANG_C */ 0x0002,
    lang_cxx = /* DW_LANG_C_plus_plus */ 0x0004
  };

private:
  /// The source location for the extern keyword.
  SourceLocation ExternLoc;

  /// The source location for the right brace (if valid).
  SourceLocation RBraceLoc;

  LinkageSpecDecl(DeclContext *DC, SourceLocation ExternLoc,
                  SourceLocation LangLoc, LanguageIDs lang, bool HasBraces);

public:
  static LinkageSpecDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation ExternLoc,
                                 SourceLocation LangLoc, LanguageIDs Lang,
                                 bool HasBraces);
  static LinkageSpecDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Return the language specified by this linkage specification.
  LanguageIDs getLanguage() const {
    return static_cast<LanguageIDs>(LinkageSpecDeclBits.Language);
  }

  /// Set the language specified by this linkage specification.
  void setLanguage(LanguageIDs L) { LinkageSpecDeclBits.Language = L; }

  /// Determines whether this linkage specification had braces in
  /// its syntactic form.
  bool hasBraces() const {
    assert(!RBraceLoc.isValid() || LinkageSpecDeclBits.HasBraces);
    return LinkageSpecDeclBits.HasBraces;
  }

  SourceLocation getExternLoc() const { return ExternLoc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setExternLoc(SourceLocation L) { ExternLoc = L; }
  void setRBraceLoc(SourceLocation L) {
    RBraceLoc = L;
    LinkageSpecDeclBits.HasBraces = RBraceLoc.isValid();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    if (hasBraces())
      return getRBraceLoc();
    // No braces: get the end location of the (only) declaration in context
    // (if present).
    return decls_empty() ? getLocation() : decls_begin()->getEndLoc();
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(ExternLoc, getEndLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == LinkageSpec; }

  static DeclContext *castToDeclContext(const LinkageSpecDecl *D) {
    return static_cast<DeclContext *>(const_cast<LinkageSpecDecl*>(D));
  }

  static LinkageSpecDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<LinkageSpecDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents C++ using-directive.
///
/// For example:
/// \code
///    using namespace std;
/// \endcode
///
/// \note UsingDirectiveDecl should be Decl not NamedDecl, but we provide
/// artificial names for all using-directives in order to store
/// them in DeclContext effectively.
class UsingDirectiveDecl : public NamedDecl {
  /// The location of the \c using keyword.
  SourceLocation UsingLoc;

  /// The location of the \c namespace keyword.
  SourceLocation NamespaceLoc;

  /// The nested-name-specifier that precedes the namespace.
  NestedNameSpecifierLoc QualifierLoc;

  /// The namespace nominated by this using-directive.
  NamedDecl *NominatedNamespace;

  /// Enclosing context containing both using-directive and nominated
  /// namespace.
  DeclContext *CommonAncestor;

  UsingDirectiveDecl(DeclContext *DC, SourceLocation UsingLoc,
                     SourceLocation NamespcLoc,
                     NestedNameSpecifierLoc QualifierLoc,
                     SourceLocation IdentLoc,
                     NamedDecl *Nominated,
                     DeclContext *CommonAncestor)
      : NamedDecl(UsingDirective, DC, IdentLoc, getName()), UsingLoc(UsingLoc),
        NamespaceLoc(NamespcLoc), QualifierLoc(QualifierLoc),
        NominatedNamespace(Nominated), CommonAncestor(CommonAncestor) {}

  /// Returns special DeclarationName used by using-directives.
  ///
  /// This is only used by DeclContext for storing UsingDirectiveDecls in
  /// its lookup structure.
  static DeclarationName getName() {
    return DeclarationName::getUsingDirectiveName();
  }

  void anchor() override;

public:
  friend class ASTDeclReader;

  // Friend for getUsingDirectiveName.
  friend class DeclContext;

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  NamedDecl *getNominatedNamespaceAsWritten() { return NominatedNamespace; }
  const NamedDecl *getNominatedNamespaceAsWritten() const {
    return NominatedNamespace;
  }

  /// Returns the namespace nominated by this using-directive.
  NamespaceDecl *getNominatedNamespace();

  const NamespaceDecl *getNominatedNamespace() const {
    return const_cast<UsingDirectiveDecl*>(this)->getNominatedNamespace();
  }

  /// Returns the common ancestor context of this using-directive and
  /// its nominated namespace.
  DeclContext *getCommonAncestor() { return CommonAncestor; }
  const DeclContext *getCommonAncestor() const { return CommonAncestor; }

  /// Return the location of the \c using keyword.
  SourceLocation getUsingLoc() const { return UsingLoc; }

  // FIXME: Could omit 'Key' in name.
  /// Returns the location of the \c namespace keyword.
  SourceLocation getNamespaceKeyLocation() const { return NamespaceLoc; }

  /// Returns the location of this using declaration's identifier.
  SourceLocation getIdentLocation() const { return getLocation(); }

  static UsingDirectiveDecl *Create(ASTContext &C, DeclContext *DC,
                                    SourceLocation UsingLoc,
                                    SourceLocation NamespaceLoc,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    SourceLocation IdentLoc,
                                    NamedDecl *Nominated,
                                    DeclContext *CommonAncestor);
  static UsingDirectiveDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(UsingLoc, getLocation());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UsingDirective; }
};

/// Represents a C++ namespace alias.
///
/// For example:
///
/// \code
/// namespace Foo = Bar;
/// \endcode
class NamespaceAliasDecl : public NamedDecl,
                           public Redeclarable<NamespaceAliasDecl> {
  friend class ASTDeclReader;

  /// The location of the \c namespace keyword.
  SourceLocation NamespaceLoc;

  /// The location of the namespace's identifier.
  ///
  /// This is accessed by TargetNameLoc.
  SourceLocation IdentLoc;

  /// The nested-name-specifier that precedes the namespace.
  NestedNameSpecifierLoc QualifierLoc;

  /// The Decl that this alias points to, either a NamespaceDecl or
  /// a NamespaceAliasDecl.
  NamedDecl *Namespace;

  NamespaceAliasDecl(ASTContext &C, DeclContext *DC,
                     SourceLocation NamespaceLoc, SourceLocation AliasLoc,
                     IdentifierInfo *Alias, NestedNameSpecifierLoc QualifierLoc,
                     SourceLocation IdentLoc, NamedDecl *Namespace)
      : NamedDecl(NamespaceAlias, DC, AliasLoc, Alias), redeclarable_base(C),
        NamespaceLoc(NamespaceLoc), IdentLoc(IdentLoc),
        QualifierLoc(QualifierLoc), Namespace(Namespace) {}

  void anchor() override;

  using redeclarable_base = Redeclarable<NamespaceAliasDecl>;

  NamespaceAliasDecl *getNextRedeclarationImpl() override;
  NamespaceAliasDecl *getPreviousDeclImpl() override;
  NamespaceAliasDecl *getMostRecentDeclImpl() override;

public:
  static NamespaceAliasDecl *Create(ASTContext &C, DeclContext *DC,
                                    SourceLocation NamespaceLoc,
                                    SourceLocation AliasLoc,
                                    IdentifierInfo *Alias,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    SourceLocation IdentLoc,
                                    NamedDecl *Namespace);

  static NamespaceAliasDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;

  NamespaceAliasDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const NamespaceAliasDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  /// Retrieve the namespace declaration aliased by this directive.
  NamespaceDecl *getNamespace() {
    if (auto *AD = dyn_cast<NamespaceAliasDecl>(Namespace))
      return AD->getNamespace();

    return cast<NamespaceDecl>(Namespace);
  }

  const NamespaceDecl *getNamespace() const {
    return const_cast<NamespaceAliasDecl *>(this)->getNamespace();
  }

  /// Returns the location of the alias name, i.e. 'foo' in
  /// "namespace foo = ns::bar;".
  SourceLocation getAliasLoc() const { return getLocation(); }

  /// Returns the location of the \c namespace keyword.
  SourceLocation getNamespaceLoc() const { return NamespaceLoc; }

  /// Returns the location of the identifier in the named namespace.
  SourceLocation getTargetNameLoc() const { return IdentLoc; }

  /// Retrieve the namespace that this alias refers to, which
  /// may either be a NamespaceDecl or a NamespaceAliasDecl.
  NamedDecl *getAliasedNamespace() const { return Namespace; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(NamespaceLoc, IdentLoc);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == NamespaceAlias; }
};

/// Represents a shadow declaration introduced into a scope by a
/// (resolved) using declaration.
///
/// For example,
/// \code
/// namespace A {
///   void foo();
/// }
/// namespace B {
///   using A::foo; // <- a UsingDecl
///                 // Also creates a UsingShadowDecl for A::foo() in B
/// }
/// \endcode
class UsingShadowDecl : public NamedDecl, public Redeclarable<UsingShadowDecl> {
  friend class UsingDecl;

  /// The referenced declaration.
  NamedDecl *Underlying = nullptr;

  /// The using declaration which introduced this decl or the next using
  /// shadow declaration contained in the aforementioned using declaration.
  NamedDecl *UsingOrNextShadow = nullptr;

  void anchor() override;

  using redeclarable_base = Redeclarable<UsingShadowDecl>;

  UsingShadowDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  UsingShadowDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  UsingShadowDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

protected:
  UsingShadowDecl(Kind K, ASTContext &C, DeclContext *DC, SourceLocation Loc,
                  UsingDecl *Using, NamedDecl *Target);
  UsingShadowDecl(Kind K, ASTContext &C, EmptyShell);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static UsingShadowDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation Loc, UsingDecl *Using,
                                 NamedDecl *Target) {
    return new (C, DC) UsingShadowDecl(UsingShadow, C, DC, Loc, Using, Target);
  }

  static UsingShadowDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  UsingShadowDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UsingShadowDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  /// Gets the underlying declaration which has been brought into the
  /// local scope.
  NamedDecl *getTargetDecl() const { return Underlying; }

  /// Sets the underlying declaration which has been brought into the
  /// local scope.
  void setTargetDecl(NamedDecl *ND) {
    assert(ND && "Target decl is null!");
    Underlying = ND;
    // A UsingShadowDecl is never a friend or local extern declaration, even
    // if it is a shadow declaration for one.
    IdentifierNamespace =
        ND->getIdentifierNamespace() &
        ~(IDNS_OrdinaryFriend | IDNS_TagFriend | IDNS_LocalExtern);
  }

  /// Gets the using declaration to which this declaration is tied.
  UsingDecl *getUsingDecl() const;

  /// The next using shadow declaration contained in the shadow decl
  /// chain of the using declaration which introduced this decl.
  UsingShadowDecl *getNextUsingShadowDecl() const {
    return dyn_cast_or_null<UsingShadowDecl>(UsingOrNextShadow);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K == Decl::UsingShadow || K == Decl::ConstructorUsingShadow;
  }
};

/// Represents a shadow constructor declaration introduced into a
/// class by a C++11 using-declaration that names a constructor.
///
/// For example:
/// \code
/// struct Base { Base(int); };
/// struct Derived {
///    using Base::Base; // creates a UsingDecl and a ConstructorUsingShadowDecl
/// };
/// \endcode
class ConstructorUsingShadowDecl final : public UsingShadowDecl {
  /// If this constructor using declaration inherted the constructor
  /// from an indirect base class, this is the ConstructorUsingShadowDecl
  /// in the named direct base class from which the declaration was inherited.
  ConstructorUsingShadowDecl *NominatedBaseClassShadowDecl = nullptr;

  /// If this constructor using declaration inherted the constructor
  /// from an indirect base class, this is the ConstructorUsingShadowDecl
  /// that will be used to construct the unique direct or virtual base class
  /// that receives the constructor arguments.
  ConstructorUsingShadowDecl *ConstructedBaseClassShadowDecl = nullptr;

  /// \c true if the constructor ultimately named by this using shadow
  /// declaration is within a virtual base class subobject of the class that
  /// contains this declaration.
  unsigned IsVirtual : 1;

  ConstructorUsingShadowDecl(ASTContext &C, DeclContext *DC, SourceLocation Loc,
                             UsingDecl *Using, NamedDecl *Target,
                             bool TargetInVirtualBase)
      : UsingShadowDecl(ConstructorUsingShadow, C, DC, Loc, Using,
                        Target->getUnderlyingDecl()),
        NominatedBaseClassShadowDecl(
            dyn_cast<ConstructorUsingShadowDecl>(Target)),
        ConstructedBaseClassShadowDecl(NominatedBaseClassShadowDecl),
        IsVirtual(TargetInVirtualBase) {
    // If we found a constructor that chains to a constructor for a virtual
    // base, we should directly call that virtual base constructor instead.
    // FIXME: This logic belongs in Sema.
    if (NominatedBaseClassShadowDecl &&
        NominatedBaseClassShadowDecl->constructsVirtualBase()) {
      ConstructedBaseClassShadowDecl =
          NominatedBaseClassShadowDecl->ConstructedBaseClassShadowDecl;
      IsVirtual = true;
    }
  }

  ConstructorUsingShadowDecl(ASTContext &C, EmptyShell Empty)
      : UsingShadowDecl(ConstructorUsingShadow, C, Empty), IsVirtual(false) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ConstructorUsingShadowDecl *Create(ASTContext &C, DeclContext *DC,
                                            SourceLocation Loc,
                                            UsingDecl *Using, NamedDecl *Target,
                                            bool IsVirtual);
  static ConstructorUsingShadowDecl *CreateDeserialized(ASTContext &C,
                                                        unsigned ID);

  /// Returns the parent of this using shadow declaration, which
  /// is the class in which this is declared.
  //@{
  const CXXRecordDecl *getParent() const {
    return cast<CXXRecordDecl>(getDeclContext());
  }
  CXXRecordDecl *getParent() {
    return cast<CXXRecordDecl>(getDeclContext());
  }
  //@}

  /// Get the inheriting constructor declaration for the direct base
  /// class from which this using shadow declaration was inherited, if there is
  /// one. This can be different for each redeclaration of the same shadow decl.
  ConstructorUsingShadowDecl *getNominatedBaseClassShadowDecl() const {
    return NominatedBaseClassShadowDecl;
  }

  /// Get the inheriting constructor declaration for the base class
  /// for which we don't have an explicit initializer, if there is one.
  ConstructorUsingShadowDecl *getConstructedBaseClassShadowDecl() const {
    return ConstructedBaseClassShadowDecl;
  }

  /// Get the base class that was named in the using declaration. This
  /// can be different for each redeclaration of this same shadow decl.
  CXXRecordDecl *getNominatedBaseClass() const;

  /// Get the base class whose constructor or constructor shadow
  /// declaration is passed the constructor arguments.
  CXXRecordDecl *getConstructedBaseClass() const {
    return cast<CXXRecordDecl>((ConstructedBaseClassShadowDecl
                                    ? ConstructedBaseClassShadowDecl
                                    : getTargetDecl())
                                   ->getDeclContext());
  }

  /// Returns \c true if the constructed base class is a virtual base
  /// class subobject of this declaration's class.
  bool constructsVirtualBase() const {
    return IsVirtual;
  }

  /// Get the constructor or constructor template in the derived class
  /// correspnding to this using shadow declaration, if it has been implicitly
  /// declared already.
  CXXConstructorDecl *getConstructor() const;
  void setConstructor(NamedDecl *Ctor);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ConstructorUsingShadow; }
};

/// Represents a C++ using-declaration.
///
/// For example:
/// \code
///    using someNameSpace::someIdentifier;
/// \endcode
class UsingDecl : public NamedDecl, public Mergeable<UsingDecl> {
  /// The source location of the 'using' keyword itself.
  SourceLocation UsingLocation;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  /// Provides source/type location info for the declaration name
  /// embedded in the ValueDecl base class.
  DeclarationNameLoc DNLoc;

  /// The first shadow declaration of the shadow decl chain associated
  /// with this using declaration.
  ///
  /// The bool member of the pair store whether this decl has the \c typename
  /// keyword.
  llvm::PointerIntPair<UsingShadowDecl *, 1, bool> FirstUsingShadow;

  UsingDecl(DeclContext *DC, SourceLocation UL,
            NestedNameSpecifierLoc QualifierLoc,
            const DeclarationNameInfo &NameInfo, bool HasTypenameKeyword)
    : NamedDecl(Using, DC, NameInfo.getLoc(), NameInfo.getName()),
      UsingLocation(UL), QualifierLoc(QualifierLoc),
      DNLoc(NameInfo.getInfo()), FirstUsingShadow(nullptr, HasTypenameKeyword) {
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Return the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return UsingLocation; }

  /// Set the source location of the 'using' keyword.
  void setUsingLoc(SourceLocation L) { UsingLocation = L; }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation(), DNLoc);
  }

  /// Return true if it is a C++03 access declaration (no 'using').
  bool isAccessDeclaration() const { return UsingLocation.isInvalid(); }

  /// Return true if the using declaration has 'typename'.
  bool hasTypename() const { return FirstUsingShadow.getInt(); }

  /// Sets whether the using declaration has 'typename'.
  void setTypename(bool TN) { FirstUsingShadow.setInt(TN); }

  /// Iterates through the using shadow declarations associated with
  /// this using declaration.
  class shadow_iterator {
    /// The current using shadow declaration.
    UsingShadowDecl *Current = nullptr;

  public:
    using value_type = UsingShadowDecl *;
    using reference = UsingShadowDecl *;
    using pointer = UsingShadowDecl *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    shadow_iterator() = default;
    explicit shadow_iterator(UsingShadowDecl *C) : Current(C) {}

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    shadow_iterator& operator++() {
      Current = Current->getNextUsingShadowDecl();
      return *this;
    }

    shadow_iterator operator++(int) {
      shadow_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(shadow_iterator x, shadow_iterator y) {
      return x.Current == y.Current;
    }
    friend bool operator!=(shadow_iterator x, shadow_iterator y) {
      return x.Current != y.Current;
    }
  };

  using shadow_range = llvm::iterator_range<shadow_iterator>;

  shadow_range shadows() const {
    return shadow_range(shadow_begin(), shadow_end());
  }

  shadow_iterator shadow_begin() const {
    return shadow_iterator(FirstUsingShadow.getPointer());
  }

  shadow_iterator shadow_end() const { return shadow_iterator(); }

  /// Return the number of shadowed declarations associated with this
  /// using declaration.
  unsigned shadow_size() const {
    return std::distance(shadow_begin(), shadow_end());
  }

  void addShadowDecl(UsingShadowDecl *S);
  void removeShadowDecl(UsingShadowDecl *S);

  static UsingDecl *Create(ASTContext &C, DeclContext *DC,
                           SourceLocation UsingL,
                           NestedNameSpecifierLoc QualifierLoc,
                           const DeclarationNameInfo &NameInfo,
                           bool HasTypenameKeyword);

  static UsingDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this declaration.
  UsingDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const UsingDecl *getCanonicalDecl() const { return getFirstDecl(); }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Using; }
};

/// Represents a pack of using declarations that a single
/// using-declarator pack-expanded into.
///
/// \code
/// template<typename ...T> struct X : T... {
///   using T::operator()...;
///   using T::operator T...;
/// };
/// \endcode
///
/// In the second case above, the UsingPackDecl will have the name
/// 'operator T' (which contains an unexpanded pack), but the individual
/// UsingDecls and UsingShadowDecls will have more reasonable names.
class UsingPackDecl final
    : public NamedDecl, public Mergeable<UsingPackDecl>,
      private llvm::TrailingObjects<UsingPackDecl, NamedDecl *> {
  /// The UnresolvedUsingValueDecl or UnresolvedUsingTypenameDecl from
  /// which this waas instantiated.
  NamedDecl *InstantiatedFrom;

  /// The number of using-declarations created by this pack expansion.
  unsigned NumExpansions;

  UsingPackDecl(DeclContext *DC, NamedDecl *InstantiatedFrom,
                ArrayRef<NamedDecl *> UsingDecls)
      : NamedDecl(UsingPack, DC,
                  InstantiatedFrom ? InstantiatedFrom->getLocation()
                                   : SourceLocation(),
                  InstantiatedFrom ? InstantiatedFrom->getDeclName()
                                   : DeclarationName()),
        InstantiatedFrom(InstantiatedFrom), NumExpansions(UsingDecls.size()) {
    std::uninitialized_copy(UsingDecls.begin(), UsingDecls.end(),
                            getTrailingObjects<NamedDecl *>());
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  /// Get the using declaration from which this was instantiated. This will
  /// always be an UnresolvedUsingValueDecl or an UnresolvedUsingTypenameDecl
  /// that is a pack expansion.
  NamedDecl *getInstantiatedFromUsingDecl() const { return InstantiatedFrom; }

  /// Get the set of using declarations that this pack expanded into. Note that
  /// some of these may still be unresolved.
  ArrayRef<NamedDecl *> expansions() const {
    return llvm::makeArrayRef(getTrailingObjects<NamedDecl *>(), NumExpansions);
  }

  static UsingPackDecl *Create(ASTContext &C, DeclContext *DC,
                               NamedDecl *InstantiatedFrom,
                               ArrayRef<NamedDecl *> UsingDecls);

  static UsingPackDecl *CreateDeserialized(ASTContext &C, unsigned ID,
                                           unsigned NumExpansions);

  SourceRange getSourceRange() const override LLVM_READONLY {
    return InstantiatedFrom->getSourceRange();
  }

  UsingPackDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const UsingPackDecl *getCanonicalDecl() const { return getFirstDecl(); }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UsingPack; }
};

/// Represents a dependent using declaration which was not marked with
/// \c typename.
///
/// Unlike non-dependent using declarations, these *only* bring through
/// non-types; otherwise they would break two-phase lookup.
///
/// \code
/// template \<class T> class A : public Base<T> {
///   using Base<T>::foo;
/// };
/// \endcode
class UnresolvedUsingValueDecl : public ValueDecl,
                                 public Mergeable<UnresolvedUsingValueDecl> {
  /// The source location of the 'using' keyword
  SourceLocation UsingLocation;

  /// If this is a pack expansion, the location of the '...'.
  SourceLocation EllipsisLoc;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  /// Provides source/type location info for the declaration name
  /// embedded in the ValueDecl base class.
  DeclarationNameLoc DNLoc;

  UnresolvedUsingValueDecl(DeclContext *DC, QualType Ty,
                           SourceLocation UsingLoc,
                           NestedNameSpecifierLoc QualifierLoc,
                           const DeclarationNameInfo &NameInfo,
                           SourceLocation EllipsisLoc)
      : ValueDecl(UnresolvedUsingValue, DC,
                  NameInfo.getLoc(), NameInfo.getName(), Ty),
        UsingLocation(UsingLoc), EllipsisLoc(EllipsisLoc),
        QualifierLoc(QualifierLoc), DNLoc(NameInfo.getInfo()) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Returns the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return UsingLocation; }

  /// Set the source location of the 'using' keyword.
  void setUsingLoc(SourceLocation L) { UsingLocation = L; }

  /// Return true if it is a C++03 access declaration (no 'using').
  bool isAccessDeclaration() const { return UsingLocation.isInvalid(); }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation(), DNLoc);
  }

  /// Determine whether this is a pack expansion.
  bool isPackExpansion() const {
    return EllipsisLoc.isValid();
  }

  /// Get the location of the ellipsis if this is a pack expansion.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  static UnresolvedUsingValueDecl *
    Create(ASTContext &C, DeclContext *DC, SourceLocation UsingLoc,
           NestedNameSpecifierLoc QualifierLoc,
           const DeclarationNameInfo &NameInfo, SourceLocation EllipsisLoc);

  static UnresolvedUsingValueDecl *
  CreateDeserialized(ASTContext &C, unsigned ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this declaration.
  UnresolvedUsingValueDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UnresolvedUsingValueDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UnresolvedUsingValue; }
};

/// Represents a dependent using declaration which was marked with
/// \c typename.
///
/// \code
/// template \<class T> class A : public Base<T> {
///   using typename Base<T>::foo;
/// };
/// \endcode
///
/// The type associated with an unresolved using typename decl is
/// currently always a typename type.
class UnresolvedUsingTypenameDecl
    : public TypeDecl,
      public Mergeable<UnresolvedUsingTypenameDecl> {
  friend class ASTDeclReader;

  /// The source location of the 'typename' keyword
  SourceLocation TypenameLocation;

  /// If this is a pack expansion, the location of the '...'.
  SourceLocation EllipsisLoc;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  UnresolvedUsingTypenameDecl(DeclContext *DC, SourceLocation UsingLoc,
                              SourceLocation TypenameLoc,
                              NestedNameSpecifierLoc QualifierLoc,
                              SourceLocation TargetNameLoc,
                              IdentifierInfo *TargetName,
                              SourceLocation EllipsisLoc)
    : TypeDecl(UnresolvedUsingTypename, DC, TargetNameLoc, TargetName,
               UsingLoc),
      TypenameLocation(TypenameLoc), EllipsisLoc(EllipsisLoc),
      QualifierLoc(QualifierLoc) {}

  void anchor() override;

public:
  /// Returns the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return getBeginLoc(); }

  /// Returns the source location of the 'typename' keyword.
  SourceLocation getTypenameLoc() const { return TypenameLocation; }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation());
  }

  /// Determine whether this is a pack expansion.
  bool isPackExpansion() const {
    return EllipsisLoc.isValid();
  }

  /// Get the location of the ellipsis if this is a pack expansion.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  static UnresolvedUsingTypenameDecl *
    Create(ASTContext &C, DeclContext *DC, SourceLocation UsingLoc,
           SourceLocation TypenameLoc, NestedNameSpecifierLoc QualifierLoc,
           SourceLocation TargetNameLoc, DeclarationName TargetName,
           SourceLocation EllipsisLoc);

  static UnresolvedUsingTypenameDecl *
  CreateDeserialized(ASTContext &C, unsigned ID);

  /// Retrieves the canonical declaration of this declaration.
  UnresolvedUsingTypenameDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UnresolvedUsingTypenameDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UnresolvedUsingTypename; }
};

/// Represents a C++11 static_assert declaration.
class StaticAssertDecl : public Decl {
  llvm::PointerIntPair<Expr *, 1, bool> AssertExprAndFailed;
  StringLiteral *Message;
  SourceLocation RParenLoc;

  StaticAssertDecl(DeclContext *DC, SourceLocation StaticAssertLoc,
                   Expr *AssertExpr, StringLiteral *Message,
                   SourceLocation RParenLoc, bool Failed)
      : Decl(StaticAssert, DC, StaticAssertLoc),
        AssertExprAndFailed(AssertExpr, Failed), Message(Message),
        RParenLoc(RParenLoc) {}

  virtual void anchor();

public:
  friend class ASTDeclReader;

  static StaticAssertDecl *Create(ASTContext &C, DeclContext *DC,
                                  SourceLocation StaticAssertLoc,
                                  Expr *AssertExpr, StringLiteral *Message,
                                  SourceLocation RParenLoc, bool Failed);
  static StaticAssertDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  Expr *getAssertExpr() { return AssertExprAndFailed.getPointer(); }
  const Expr *getAssertExpr() const { return AssertExprAndFailed.getPointer(); }

  StringLiteral *getMessage() { return Message; }
  const StringLiteral *getMessage() const { return Message; }

  bool isFailed() const { return AssertExprAndFailed.getInt(); }

  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocation(), getRParenLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == StaticAssert; }
};

/// A binding in a decomposition declaration. For instance, given:
///
///   int n[3];
///   auto &[a, b, c] = n;
///
/// a, b, and c are BindingDecls, whose bindings are the expressions
/// x[0], x[1], and x[2] respectively, where x is the implicit
/// DecompositionDecl of type 'int (&)[3]'.
class BindingDecl : public ValueDecl {
  /// The binding represented by this declaration. References to this
  /// declaration are effectively equivalent to this expression (except
  /// that it is only evaluated once at the point of declaration of the
  /// binding).
  Expr *Binding = nullptr;

  BindingDecl(DeclContext *DC, SourceLocation IdLoc, IdentifierInfo *Id)
      : ValueDecl(Decl::Binding, DC, IdLoc, Id, QualType()) {}

  void anchor() override;

public:
  friend class ASTDeclReader;

  static BindingDecl *Create(ASTContext &C, DeclContext *DC,
                             SourceLocation IdLoc, IdentifierInfo *Id);
  static BindingDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  /// Get the expression to which this declaration is bound. This may be null
  /// in two different cases: while parsing the initializer for the
  /// decomposition declaration, and when the initializer is type-dependent.
  Expr *getBinding() const { return Binding; }

  /// Get the variable (if any) that holds the value of evaluating the binding.
  /// Only present for user-defined bindings for tuple-like types.
  VarDecl *getHoldingVar() const;

  /// Set the binding for this BindingDecl, along with its declared type (which
  /// should be a possibly-cv-qualified form of the type of the binding, or a
  /// reference to such a type).
  void setBinding(QualType DeclaredType, Expr *Binding) {
    setType(DeclaredType);
    this->Binding = Binding;
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::Binding; }
};

/// A decomposition declaration. For instance, given:
///
///   int n[3];
///   auto &[a, b, c] = n;
///
/// the second line declares a DecompositionDecl of type 'int (&)[3]', and
/// three BindingDecls (named a, b, and c). An instance of this class is always
/// unnamed, but behaves in almost all other respects like a VarDecl.
class DecompositionDecl final
    : public VarDecl,
      private llvm::TrailingObjects<DecompositionDecl, BindingDecl *> {
  /// The number of BindingDecl*s following this object.
  unsigned NumBindings;

  DecompositionDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                    SourceLocation LSquareLoc, QualType T,
                    TypeSourceInfo *TInfo, StorageClass SC,
                    ArrayRef<BindingDecl *> Bindings)
      : VarDecl(Decomposition, C, DC, StartLoc, LSquareLoc, nullptr, T, TInfo,
                SC),
        NumBindings(Bindings.size()) {
    std::uninitialized_copy(Bindings.begin(), Bindings.end(),
                            getTrailingObjects<BindingDecl *>());
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend TrailingObjects;

  static DecompositionDecl *Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation StartLoc,
                                   SourceLocation LSquareLoc,
                                   QualType T, TypeSourceInfo *TInfo,
                                   StorageClass S,
                                   ArrayRef<BindingDecl *> Bindings);
  static DecompositionDecl *CreateDeserialized(ASTContext &C, unsigned ID,
                                               unsigned NumBindings);

  ArrayRef<BindingDecl *> bindings() const {
    return llvm::makeArrayRef(getTrailingObjects<BindingDecl *>(), NumBindings);
  }

  void printName(raw_ostream &os) const override;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decomposition; }
};

/// An instance of this class represents the declaration of a property
/// member.  This is a Microsoft extension to C++, first introduced in
/// Visual Studio .NET 2003 as a parallel to similar features in C#
/// and Managed C++.
///
/// A property must always be a non-static class member.
///
/// A property member superficially resembles a non-static data
/// member, except preceded by a property attribute:
///   __declspec(property(get=GetX, put=PutX)) int x;
/// Either (but not both) of the 'get' and 'put' names may be omitted.
///
/// A reference to a property is always an lvalue.  If the lvalue
/// undergoes lvalue-to-rvalue conversion, then a getter name is
/// required, and that member is called with no arguments.
/// If the lvalue is assigned into, then a setter name is required,
/// and that member is called with one argument, the value assigned.
/// Both operations are potentially overloaded.  Compound assignments
/// are permitted, as are the increment and decrement operators.
///
/// The getter and putter methods are permitted to be overloaded,
/// although their return and parameter types are subject to certain
/// restrictions according to the type of the property.
///
/// A property declared using an incomplete array type may
/// additionally be subscripted, adding extra parameters to the getter
/// and putter methods.
class MSPropertyDecl : public DeclaratorDecl {
  IdentifierInfo *GetterId, *SetterId;

  MSPropertyDecl(DeclContext *DC, SourceLocation L, DeclarationName N,
                 QualType T, TypeSourceInfo *TInfo, SourceLocation StartL,
                 IdentifierInfo *Getter, IdentifierInfo *Setter)
      : DeclaratorDecl(MSProperty, DC, L, N, T, TInfo, StartL),
        GetterId(Getter), SetterId(Setter) {}

  void anchor() override;
public:
  friend class ASTDeclReader;

  static MSPropertyDecl *Create(ASTContext &C, DeclContext *DC,
                                SourceLocation L, DeclarationName N, QualType T,
                                TypeSourceInfo *TInfo, SourceLocation StartL,
                                IdentifierInfo *Getter, IdentifierInfo *Setter);
  static MSPropertyDecl *CreateDeserialized(ASTContext &C, unsigned ID);

  static bool classof(const Decl *D) { return D->getKind() == MSProperty; }

  bool hasGetter() const { return GetterId != nullptr; }
  IdentifierInfo* getGetterId() const { return GetterId; }
  bool hasSetter() const { return SetterId != nullptr; }
  IdentifierInfo* getSetterId() const { return SetterId; }
};

/// Insertion operator for diagnostics.  This allows sending an AccessSpecifier
/// into a diagnostic with <<.
const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    AccessSpecifier AS);

const PartialDiagnostic &operator<<(const PartialDiagnostic &DB,
                                    AccessSpecifier AS);

} // namespace clang

#endif // LLVM_CLANG_AST_DECLCXX_H
