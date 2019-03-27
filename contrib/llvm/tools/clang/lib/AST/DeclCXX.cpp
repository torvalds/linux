//===- DeclCXX.cpp - C++ Declaration AST Node Implementation --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the C++ related Decl classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/ODRHash.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace clang;

//===----------------------------------------------------------------------===//
// Decl Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//

void AccessSpecDecl::anchor() {}

AccessSpecDecl *AccessSpecDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) AccessSpecDecl(EmptyShell());
}

void LazyASTUnresolvedSet::getFromExternalSource(ASTContext &C) const {
  ExternalASTSource *Source = C.getExternalSource();
  assert(Impl.Decls.isLazy() && "getFromExternalSource for non-lazy set");
  assert(Source && "getFromExternalSource with no external source");

  for (ASTUnresolvedSet::iterator I = Impl.begin(); I != Impl.end(); ++I)
    I.setDecl(cast<NamedDecl>(Source->GetExternalDecl(
        reinterpret_cast<uintptr_t>(I.getDecl()) >> 2)));
  Impl.Decls.setLazy(false);
}

CXXRecordDecl::DefinitionData::DefinitionData(CXXRecordDecl *D)
    : UserDeclaredConstructor(false), UserDeclaredSpecialMembers(0),
      Aggregate(true), PlainOldData(true), Empty(true), Polymorphic(false),
      Abstract(false), IsStandardLayout(true), IsCXX11StandardLayout(true),
      HasBasesWithFields(false), HasBasesWithNonStaticDataMembers(false),
      HasPrivateFields(false), HasProtectedFields(false),
      HasPublicFields(false), HasMutableFields(false), HasVariantMembers(false),
      HasOnlyCMembers(true), HasInClassInitializer(false),
      HasUninitializedReferenceMember(false), HasUninitializedFields(false),
      HasInheritedConstructor(false), HasInheritedAssignment(false),
      NeedOverloadResolutionForCopyConstructor(false),
      NeedOverloadResolutionForMoveConstructor(false),
      NeedOverloadResolutionForMoveAssignment(false),
      NeedOverloadResolutionForDestructor(false),
      DefaultedCopyConstructorIsDeleted(false),
      DefaultedMoveConstructorIsDeleted(false),
      DefaultedMoveAssignmentIsDeleted(false),
      DefaultedDestructorIsDeleted(false), HasTrivialSpecialMembers(SMF_All),
      HasTrivialSpecialMembersForCall(SMF_All),
      DeclaredNonTrivialSpecialMembers(0),
      DeclaredNonTrivialSpecialMembersForCall(0), HasIrrelevantDestructor(true),
      HasConstexprNonCopyMoveConstructor(false),
      HasDefaultedDefaultConstructor(false),
      DefaultedDefaultConstructorIsConstexpr(true),
      HasConstexprDefaultConstructor(false),
      HasNonLiteralTypeFieldsOrBases(false), ComputedVisibleConversions(false),
      UserProvidedDefaultConstructor(false), DeclaredSpecialMembers(0),
      ImplicitCopyConstructorCanHaveConstParamForVBase(true),
      ImplicitCopyConstructorCanHaveConstParamForNonVBase(true),
      ImplicitCopyAssignmentHasConstParam(true),
      HasDeclaredCopyConstructorWithConstParam(false),
      HasDeclaredCopyAssignmentWithConstParam(false), IsLambda(false),
      IsParsingBaseSpecifiers(false), HasODRHash(false), Definition(D) {}

CXXBaseSpecifier *CXXRecordDecl::DefinitionData::getBasesSlowCase() const {
  return Bases.get(Definition->getASTContext().getExternalSource());
}

CXXBaseSpecifier *CXXRecordDecl::DefinitionData::getVBasesSlowCase() const {
  return VBases.get(Definition->getASTContext().getExternalSource());
}

CXXRecordDecl::CXXRecordDecl(Kind K, TagKind TK, const ASTContext &C,
                             DeclContext *DC, SourceLocation StartLoc,
                             SourceLocation IdLoc, IdentifierInfo *Id,
                             CXXRecordDecl *PrevDecl)
    : RecordDecl(K, TK, C, DC, StartLoc, IdLoc, Id, PrevDecl),
      DefinitionData(PrevDecl ? PrevDecl->DefinitionData
                              : nullptr) {}

CXXRecordDecl *CXXRecordDecl::Create(const ASTContext &C, TagKind TK,
                                     DeclContext *DC, SourceLocation StartLoc,
                                     SourceLocation IdLoc, IdentifierInfo *Id,
                                     CXXRecordDecl *PrevDecl,
                                     bool DelayTypeCreation) {
  auto *R = new (C, DC) CXXRecordDecl(CXXRecord, TK, C, DC, StartLoc, IdLoc, Id,
                                      PrevDecl);
  R->setMayHaveOutOfDateDef(C.getLangOpts().Modules);

  // FIXME: DelayTypeCreation seems like such a hack
  if (!DelayTypeCreation)
    C.getTypeDeclType(R, PrevDecl);
  return R;
}

CXXRecordDecl *
CXXRecordDecl::CreateLambda(const ASTContext &C, DeclContext *DC,
                            TypeSourceInfo *Info, SourceLocation Loc,
                            bool Dependent, bool IsGeneric,
                            LambdaCaptureDefault CaptureDefault) {
  auto *R = new (C, DC) CXXRecordDecl(CXXRecord, TTK_Class, C, DC, Loc, Loc,
                                      nullptr, nullptr);
  R->setBeingDefined(true);
  R->DefinitionData =
      new (C) struct LambdaDefinitionData(R, Info, Dependent, IsGeneric,
                                          CaptureDefault);
  R->setMayHaveOutOfDateDef(false);
  R->setImplicit(true);
  C.getTypeDeclType(R, /*PrevDecl=*/nullptr);
  return R;
}

CXXRecordDecl *
CXXRecordDecl::CreateDeserialized(const ASTContext &C, unsigned ID) {
  auto *R = new (C, ID) CXXRecordDecl(
      CXXRecord, TTK_Struct, C, nullptr, SourceLocation(), SourceLocation(),
      nullptr, nullptr);
  R->setMayHaveOutOfDateDef(false);
  return R;
}

/// Determine whether a class has a repeated base class. This is intended for
/// use when determining if a class is standard-layout, so makes no attempt to
/// handle virtual bases.
static bool hasRepeatedBaseClass(const CXXRecordDecl *StartRD) {
  llvm::SmallPtrSet<const CXXRecordDecl*, 8> SeenBaseTypes;
  SmallVector<const CXXRecordDecl*, 8> WorkList = {StartRD};
  while (!WorkList.empty()) {
    const CXXRecordDecl *RD = WorkList.pop_back_val();
    for (const CXXBaseSpecifier &BaseSpec : RD->bases()) {
      if (const CXXRecordDecl *B = BaseSpec.getType()->getAsCXXRecordDecl()) {
        if (!SeenBaseTypes.insert(B).second)
          return true;
        WorkList.push_back(B);
      }
    }
  }
  return false;
}

void
CXXRecordDecl::setBases(CXXBaseSpecifier const * const *Bases,
                        unsigned NumBases) {
  ASTContext &C = getASTContext();

  if (!data().Bases.isOffset() && data().NumBases > 0)
    C.Deallocate(data().getBases());

  if (NumBases) {
    if (!C.getLangOpts().CPlusPlus17) {
      // C++ [dcl.init.aggr]p1:
      //   An aggregate is [...] a class with [...] no base classes [...].
      data().Aggregate = false;
    }

    // C++ [class]p4:
    //   A POD-struct is an aggregate class...
    data().PlainOldData = false;
  }

  // The set of seen virtual base types.
  llvm::SmallPtrSet<CanQualType, 8> SeenVBaseTypes;

  // The virtual bases of this class.
  SmallVector<const CXXBaseSpecifier *, 8> VBases;

  data().Bases = new(C) CXXBaseSpecifier [NumBases];
  data().NumBases = NumBases;
  for (unsigned i = 0; i < NumBases; ++i) {
    data().getBases()[i] = *Bases[i];
    // Keep track of inherited vbases for this base class.
    const CXXBaseSpecifier *Base = Bases[i];
    QualType BaseType = Base->getType();
    // Skip dependent types; we can't do any checking on them now.
    if (BaseType->isDependentType())
      continue;
    auto *BaseClassDecl =
        cast<CXXRecordDecl>(BaseType->getAs<RecordType>()->getDecl());

    // C++2a [class]p7:
    //   A standard-layout class is a class that:
    //    [...]
    //    -- has all non-static data members and bit-fields in the class and
    //       its base classes first declared in the same class
    if (BaseClassDecl->data().HasBasesWithFields ||
        !BaseClassDecl->field_empty()) {
      if (data().HasBasesWithFields)
        // Two bases have members or bit-fields: not standard-layout.
        data().IsStandardLayout = false;
      data().HasBasesWithFields = true;
    }

    // C++11 [class]p7:
    //   A standard-layout class is a class that:
    //     -- [...] has [...] at most one base class with non-static data
    //        members
    if (BaseClassDecl->data().HasBasesWithNonStaticDataMembers ||
        BaseClassDecl->hasDirectFields()) {
      if (data().HasBasesWithNonStaticDataMembers)
        data().IsCXX11StandardLayout = false;
      data().HasBasesWithNonStaticDataMembers = true;
    }

    if (!BaseClassDecl->isEmpty()) {
      // C++14 [meta.unary.prop]p4:
      //   T is a class type [...] with [...] no base class B for which
      //   is_empty<B>::value is false.
      data().Empty = false;
    }

    // C++1z [dcl.init.agg]p1:
    //   An aggregate is a class with [...] no private or protected base classes
    if (Base->getAccessSpecifier() != AS_public)
      data().Aggregate = false;

    // C++ [class.virtual]p1:
    //   A class that declares or inherits a virtual function is called a
    //   polymorphic class.
    if (BaseClassDecl->isPolymorphic()) {
      data().Polymorphic = true;

      //   An aggregate is a class with [...] no virtual functions.
      data().Aggregate = false;
    }

    // C++0x [class]p7:
    //   A standard-layout class is a class that: [...]
    //    -- has no non-standard-layout base classes
    if (!BaseClassDecl->isStandardLayout())
      data().IsStandardLayout = false;
    if (!BaseClassDecl->isCXX11StandardLayout())
      data().IsCXX11StandardLayout = false;

    // Record if this base is the first non-literal field or base.
    if (!hasNonLiteralTypeFieldsOrBases() && !BaseType->isLiteralType(C))
      data().HasNonLiteralTypeFieldsOrBases = true;

    // Now go through all virtual bases of this base and add them.
    for (const auto &VBase : BaseClassDecl->vbases()) {
      // Add this base if it's not already in the list.
      if (SeenVBaseTypes.insert(C.getCanonicalType(VBase.getType())).second) {
        VBases.push_back(&VBase);

        // C++11 [class.copy]p8:
        //   The implicitly-declared copy constructor for a class X will have
        //   the form 'X::X(const X&)' if each [...] virtual base class B of X
        //   has a copy constructor whose first parameter is of type
        //   'const B&' or 'const volatile B&' [...]
        if (CXXRecordDecl *VBaseDecl = VBase.getType()->getAsCXXRecordDecl())
          if (!VBaseDecl->hasCopyConstructorWithConstParam())
            data().ImplicitCopyConstructorCanHaveConstParamForVBase = false;

        // C++1z [dcl.init.agg]p1:
        //   An aggregate is a class with [...] no virtual base classes
        data().Aggregate = false;
      }
    }

    if (Base->isVirtual()) {
      // Add this base if it's not already in the list.
      if (SeenVBaseTypes.insert(C.getCanonicalType(BaseType)).second)
        VBases.push_back(Base);

      // C++14 [meta.unary.prop] is_empty:
      //   T is a class type, but not a union type, with ... no virtual base
      //   classes
      data().Empty = false;

      // C++1z [dcl.init.agg]p1:
      //   An aggregate is a class with [...] no virtual base classes
      data().Aggregate = false;

      // C++11 [class.ctor]p5, C++11 [class.copy]p12, C++11 [class.copy]p25:
      //   A [default constructor, copy/move constructor, or copy/move assignment
      //   operator for a class X] is trivial [...] if:
      //    -- class X has [...] no virtual base classes
      data().HasTrivialSpecialMembers &= SMF_Destructor;
      data().HasTrivialSpecialMembersForCall &= SMF_Destructor;

      // C++0x [class]p7:
      //   A standard-layout class is a class that: [...]
      //    -- has [...] no virtual base classes
      data().IsStandardLayout = false;
      data().IsCXX11StandardLayout = false;

      // C++11 [dcl.constexpr]p4:
      //   In the definition of a constexpr constructor [...]
      //    -- the class shall not have any virtual base classes
      data().DefaultedDefaultConstructorIsConstexpr = false;

      // C++1z [class.copy]p8:
      //   The implicitly-declared copy constructor for a class X will have
      //   the form 'X::X(const X&)' if each potentially constructed subobject
      //   has a copy constructor whose first parameter is of type
      //   'const B&' or 'const volatile B&' [...]
      if (!BaseClassDecl->hasCopyConstructorWithConstParam())
        data().ImplicitCopyConstructorCanHaveConstParamForVBase = false;
    } else {
      // C++ [class.ctor]p5:
      //   A default constructor is trivial [...] if:
      //    -- all the direct base classes of its class have trivial default
      //       constructors.
      if (!BaseClassDecl->hasTrivialDefaultConstructor())
        data().HasTrivialSpecialMembers &= ~SMF_DefaultConstructor;

      // C++0x [class.copy]p13:
      //   A copy/move constructor for class X is trivial if [...]
      //    [...]
      //    -- the constructor selected to copy/move each direct base class
      //       subobject is trivial, and
      if (!BaseClassDecl->hasTrivialCopyConstructor())
        data().HasTrivialSpecialMembers &= ~SMF_CopyConstructor;

      if (!BaseClassDecl->hasTrivialCopyConstructorForCall())
        data().HasTrivialSpecialMembersForCall &= ~SMF_CopyConstructor;

      // If the base class doesn't have a simple move constructor, we'll eagerly
      // declare it and perform overload resolution to determine which function
      // it actually calls. If it does have a simple move constructor, this
      // check is correct.
      if (!BaseClassDecl->hasTrivialMoveConstructor())
        data().HasTrivialSpecialMembers &= ~SMF_MoveConstructor;

      if (!BaseClassDecl->hasTrivialMoveConstructorForCall())
        data().HasTrivialSpecialMembersForCall &= ~SMF_MoveConstructor;

      // C++0x [class.copy]p27:
      //   A copy/move assignment operator for class X is trivial if [...]
      //    [...]
      //    -- the assignment operator selected to copy/move each direct base
      //       class subobject is trivial, and
      if (!BaseClassDecl->hasTrivialCopyAssignment())
        data().HasTrivialSpecialMembers &= ~SMF_CopyAssignment;
      // If the base class doesn't have a simple move assignment, we'll eagerly
      // declare it and perform overload resolution to determine which function
      // it actually calls. If it does have a simple move assignment, this
      // check is correct.
      if (!BaseClassDecl->hasTrivialMoveAssignment())
        data().HasTrivialSpecialMembers &= ~SMF_MoveAssignment;

      // C++11 [class.ctor]p6:
      //   If that user-written default constructor would satisfy the
      //   requirements of a constexpr constructor, the implicitly-defined
      //   default constructor is constexpr.
      if (!BaseClassDecl->hasConstexprDefaultConstructor())
        data().DefaultedDefaultConstructorIsConstexpr = false;

      // C++1z [class.copy]p8:
      //   The implicitly-declared copy constructor for a class X will have
      //   the form 'X::X(const X&)' if each potentially constructed subobject
      //   has a copy constructor whose first parameter is of type
      //   'const B&' or 'const volatile B&' [...]
      if (!BaseClassDecl->hasCopyConstructorWithConstParam())
        data().ImplicitCopyConstructorCanHaveConstParamForNonVBase = false;
    }

    // C++ [class.ctor]p3:
    //   A destructor is trivial if all the direct base classes of its class
    //   have trivial destructors.
    if (!BaseClassDecl->hasTrivialDestructor())
      data().HasTrivialSpecialMembers &= ~SMF_Destructor;

    if (!BaseClassDecl->hasTrivialDestructorForCall())
      data().HasTrivialSpecialMembersForCall &= ~SMF_Destructor;

    if (!BaseClassDecl->hasIrrelevantDestructor())
      data().HasIrrelevantDestructor = false;

    // C++11 [class.copy]p18:
    //   The implicitly-declared copy assignment oeprator for a class X will
    //   have the form 'X& X::operator=(const X&)' if each direct base class B
    //   of X has a copy assignment operator whose parameter is of type 'const
    //   B&', 'const volatile B&', or 'B' [...]
    if (!BaseClassDecl->hasCopyAssignmentWithConstParam())
      data().ImplicitCopyAssignmentHasConstParam = false;

    // A class has an Objective-C object member if... or any of its bases
    // has an Objective-C object member.
    if (BaseClassDecl->hasObjectMember())
      setHasObjectMember(true);

    if (BaseClassDecl->hasVolatileMember())
      setHasVolatileMember(true);

    if (BaseClassDecl->getArgPassingRestrictions() ==
        RecordDecl::APK_CanNeverPassInRegs)
      setArgPassingRestrictions(RecordDecl::APK_CanNeverPassInRegs);

    // Keep track of the presence of mutable fields.
    if (BaseClassDecl->hasMutableFields()) {
      data().HasMutableFields = true;
      data().NeedOverloadResolutionForCopyConstructor = true;
    }

    if (BaseClassDecl->hasUninitializedReferenceMember())
      data().HasUninitializedReferenceMember = true;

    if (!BaseClassDecl->allowConstDefaultInit())
      data().HasUninitializedFields = true;

    addedClassSubobject(BaseClassDecl);
  }

  // C++2a [class]p7:
  //   A class S is a standard-layout class if it:
  //     -- has at most one base class subobject of any given type
  //
  // Note that we only need to check this for classes with more than one base
  // class. If there's only one base class, and it's standard layout, then
  // we know there are no repeated base classes.
  if (data().IsStandardLayout && NumBases > 1 && hasRepeatedBaseClass(this))
    data().IsStandardLayout = false;

  if (VBases.empty()) {
    data().IsParsingBaseSpecifiers = false;
    return;
  }

  // Create base specifier for any direct or indirect virtual bases.
  data().VBases = new (C) CXXBaseSpecifier[VBases.size()];
  data().NumVBases = VBases.size();
  for (int I = 0, E = VBases.size(); I != E; ++I) {
    QualType Type = VBases[I]->getType();
    if (!Type->isDependentType())
      addedClassSubobject(Type->getAsCXXRecordDecl());
    data().getVBases()[I] = *VBases[I];
  }

  data().IsParsingBaseSpecifiers = false;
}

unsigned CXXRecordDecl::getODRHash() const {
  assert(hasDefinition() && "ODRHash only for records with definitions");

  // Previously calculated hash is stored in DefinitionData.
  if (DefinitionData->HasODRHash)
    return DefinitionData->ODRHash;

  // Only calculate hash on first call of getODRHash per record.
  ODRHash Hash;
  Hash.AddCXXRecordDecl(getDefinition());
  DefinitionData->HasODRHash = true;
  DefinitionData->ODRHash = Hash.CalculateHash();

  return DefinitionData->ODRHash;
}

void CXXRecordDecl::addedClassSubobject(CXXRecordDecl *Subobj) {
  // C++11 [class.copy]p11:
  //   A defaulted copy/move constructor for a class X is defined as
  //   deleted if X has:
  //    -- a direct or virtual base class B that cannot be copied/moved [...]
  //    -- a non-static data member of class type M (or array thereof)
  //       that cannot be copied or moved [...]
  if (!Subobj->hasSimpleCopyConstructor())
    data().NeedOverloadResolutionForCopyConstructor = true;
  if (!Subobj->hasSimpleMoveConstructor())
    data().NeedOverloadResolutionForMoveConstructor = true;

  // C++11 [class.copy]p23:
  //   A defaulted copy/move assignment operator for a class X is defined as
  //   deleted if X has:
  //    -- a direct or virtual base class B that cannot be copied/moved [...]
  //    -- a non-static data member of class type M (or array thereof)
  //        that cannot be copied or moved [...]
  if (!Subobj->hasSimpleMoveAssignment())
    data().NeedOverloadResolutionForMoveAssignment = true;

  // C++11 [class.ctor]p5, C++11 [class.copy]p11, C++11 [class.dtor]p5:
  //   A defaulted [ctor or dtor] for a class X is defined as
  //   deleted if X has:
  //    -- any direct or virtual base class [...] has a type with a destructor
  //       that is deleted or inaccessible from the defaulted [ctor or dtor].
  //    -- any non-static data member has a type with a destructor
  //       that is deleted or inaccessible from the defaulted [ctor or dtor].
  if (!Subobj->hasSimpleDestructor()) {
    data().NeedOverloadResolutionForCopyConstructor = true;
    data().NeedOverloadResolutionForMoveConstructor = true;
    data().NeedOverloadResolutionForDestructor = true;
  }
}

bool CXXRecordDecl::hasAnyDependentBases() const {
  if (!isDependentContext())
    return false;

  return !forallBases([](const CXXRecordDecl *) { return true; });
}

bool CXXRecordDecl::isTriviallyCopyable() const {
  // C++0x [class]p5:
  //   A trivially copyable class is a class that:
  //   -- has no non-trivial copy constructors,
  if (hasNonTrivialCopyConstructor()) return false;
  //   -- has no non-trivial move constructors,
  if (hasNonTrivialMoveConstructor()) return false;
  //   -- has no non-trivial copy assignment operators,
  if (hasNonTrivialCopyAssignment()) return false;
  //   -- has no non-trivial move assignment operators, and
  if (hasNonTrivialMoveAssignment()) return false;
  //   -- has a trivial destructor.
  if (!hasTrivialDestructor()) return false;

  return true;
}

void CXXRecordDecl::markedVirtualFunctionPure() {
  // C++ [class.abstract]p2:
  //   A class is abstract if it has at least one pure virtual function.
  data().Abstract = true;
}

bool CXXRecordDecl::hasSubobjectAtOffsetZeroOfEmptyBaseType(
    ASTContext &Ctx, const CXXRecordDecl *XFirst) {
  if (!getNumBases())
    return false;

  llvm::SmallPtrSet<const CXXRecordDecl*, 8> Bases;
  llvm::SmallPtrSet<const CXXRecordDecl*, 8> M;
  SmallVector<const CXXRecordDecl*, 8> WorkList;

  // Visit a type that we have determined is an element of M(S).
  auto Visit = [&](const CXXRecordDecl *RD) -> bool {
    RD = RD->getCanonicalDecl();

    // C++2a [class]p8:
    //   A class S is a standard-layout class if it [...] has no element of the
    //   set M(S) of types as a base class.
    //
    // If we find a subobject of an empty type, it might also be a base class,
    // so we'll need to walk the base classes to check.
    if (!RD->data().HasBasesWithFields) {
      // Walk the bases the first time, stopping if we find the type. Build a
      // set of them so we don't need to walk them again.
      if (Bases.empty()) {
        bool RDIsBase = !forallBases([&](const CXXRecordDecl *Base) -> bool {
          Base = Base->getCanonicalDecl();
          if (RD == Base)
            return false;
          Bases.insert(Base);
          return true;
        });
        if (RDIsBase)
          return true;
      } else {
        if (Bases.count(RD))
          return true;
      }
    }

    if (M.insert(RD).second)
      WorkList.push_back(RD);
    return false;
  };

  if (Visit(XFirst))
    return true;

  while (!WorkList.empty()) {
    const CXXRecordDecl *X = WorkList.pop_back_val();

    // FIXME: We don't check the bases of X. That matches the standard, but
    // that sure looks like a wording bug.

    //   -- If X is a non-union class type with a non-static data member
    //      [recurse to] the first non-static data member of X
    //   -- If X is a union type, [recurse to union members]
    for (auto *FD : X->fields()) {
      // FIXME: Should we really care about the type of the first non-static
      // data member of a non-union if there are preceding unnamed bit-fields?
      if (FD->isUnnamedBitfield())
        continue;

      //   -- If X is n array type, [visit the element type]
      QualType T = Ctx.getBaseElementType(FD->getType());
      if (auto *RD = T->getAsCXXRecordDecl())
        if (Visit(RD))
          return true;

      if (!X->isUnion())
        break;
    }
  }

  return false;
}

bool CXXRecordDecl::lambdaIsDefaultConstructibleAndAssignable() const {
  assert(isLambda() && "not a lambda");

  // C++2a [expr.prim.lambda.capture]p11:
  //   The closure type associated with a lambda-expression has no default
  //   constructor if the lambda-expression has a lambda-capture and a
  //   defaulted default constructor otherwise. It has a deleted copy
  //   assignment operator if the lambda-expression has a lambda-capture and
  //   defaulted copy and move assignment operators otherwise.
  //
  // C++17 [expr.prim.lambda]p21:
  //   The closure type associated with a lambda-expression has no default
  //   constructor and a deleted copy assignment operator.
  if (getLambdaCaptureDefault() != LCD_None)
    return false;
  return getASTContext().getLangOpts().CPlusPlus2a;
}

void CXXRecordDecl::addedMember(Decl *D) {
  if (!D->isImplicit() &&
      !isa<FieldDecl>(D) &&
      !isa<IndirectFieldDecl>(D) &&
      (!isa<TagDecl>(D) || cast<TagDecl>(D)->getTagKind() == TTK_Class ||
        cast<TagDecl>(D)->getTagKind() == TTK_Interface))
    data().HasOnlyCMembers = false;

  // Ignore friends and invalid declarations.
  if (D->getFriendObjectKind() || D->isInvalidDecl())
    return;

  auto *FunTmpl = dyn_cast<FunctionTemplateDecl>(D);
  if (FunTmpl)
    D = FunTmpl->getTemplatedDecl();

  // FIXME: Pass NamedDecl* to addedMember?
  Decl *DUnderlying = D;
  if (auto *ND = dyn_cast<NamedDecl>(DUnderlying)) {
    DUnderlying = ND->getUnderlyingDecl();
    if (auto *UnderlyingFunTmpl = dyn_cast<FunctionTemplateDecl>(DUnderlying))
      DUnderlying = UnderlyingFunTmpl->getTemplatedDecl();
  }

  if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
    if (Method->isVirtual()) {
      // C++ [dcl.init.aggr]p1:
      //   An aggregate is an array or a class with [...] no virtual functions.
      data().Aggregate = false;

      // C++ [class]p4:
      //   A POD-struct is an aggregate class...
      data().PlainOldData = false;

      // C++14 [meta.unary.prop]p4:
      //   T is a class type [...] with [...] no virtual member functions...
      data().Empty = false;

      // C++ [class.virtual]p1:
      //   A class that declares or inherits a virtual function is called a
      //   polymorphic class.
      data().Polymorphic = true;

      // C++11 [class.ctor]p5, C++11 [class.copy]p12, C++11 [class.copy]p25:
      //   A [default constructor, copy/move constructor, or copy/move
      //   assignment operator for a class X] is trivial [...] if:
      //    -- class X has no virtual functions [...]
      data().HasTrivialSpecialMembers &= SMF_Destructor;
      data().HasTrivialSpecialMembersForCall &= SMF_Destructor;

      // C++0x [class]p7:
      //   A standard-layout class is a class that: [...]
      //    -- has no virtual functions
      data().IsStandardLayout = false;
      data().IsCXX11StandardLayout = false;
    }
  }

  // Notify the listener if an implicit member was added after the definition
  // was completed.
  if (!isBeingDefined() && D->isImplicit())
    if (ASTMutationListener *L = getASTMutationListener())
      L->AddedCXXImplicitMember(data().Definition, D);

  // The kind of special member this declaration is, if any.
  unsigned SMKind = 0;

  // Handle constructors.
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    if (!Constructor->isImplicit()) {
      // Note that we have a user-declared constructor.
      data().UserDeclaredConstructor = true;

      // C++ [class]p4:
      //   A POD-struct is an aggregate class [...]
      // Since the POD bit is meant to be C++03 POD-ness, clear it even if the
      // type is technically an aggregate in C++0x since it wouldn't be in 03.
      data().PlainOldData = false;
    }

    if (Constructor->isDefaultConstructor()) {
      SMKind |= SMF_DefaultConstructor;

      if (Constructor->isUserProvided())
        data().UserProvidedDefaultConstructor = true;
      if (Constructor->isConstexpr())
        data().HasConstexprDefaultConstructor = true;
      if (Constructor->isDefaulted())
        data().HasDefaultedDefaultConstructor = true;
    }

    if (!FunTmpl) {
      unsigned Quals;
      if (Constructor->isCopyConstructor(Quals)) {
        SMKind |= SMF_CopyConstructor;

        if (Quals & Qualifiers::Const)
          data().HasDeclaredCopyConstructorWithConstParam = true;
      } else if (Constructor->isMoveConstructor())
        SMKind |= SMF_MoveConstructor;
    }

    // C++11 [dcl.init.aggr]p1: DR1518
    //   An aggregate is an array or a class with no user-provided [or]
    //   explicit [...] constructors
    // C++20 [dcl.init.aggr]p1:
    //   An aggregate is an array or a class with no user-declared [...]
    //   constructors
    if (getASTContext().getLangOpts().CPlusPlus2a
            ? !Constructor->isImplicit()
            : (Constructor->isUserProvided() || Constructor->isExplicit()))
      data().Aggregate = false;
  }

  // Handle constructors, including those inherited from base classes.
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(DUnderlying)) {
    // Record if we see any constexpr constructors which are neither copy
    // nor move constructors.
    // C++1z [basic.types]p10:
    //   [...] has at least one constexpr constructor or constructor template
    //   (possibly inherited from a base class) that is not a copy or move
    //   constructor [...]
    if (Constructor->isConstexpr() && !Constructor->isCopyOrMoveConstructor())
      data().HasConstexprNonCopyMoveConstructor = true;
  }

  // Handle destructors.
  if (const auto *DD = dyn_cast<CXXDestructorDecl>(D)) {
    SMKind |= SMF_Destructor;

    if (DD->isUserProvided())
      data().HasIrrelevantDestructor = false;
    // If the destructor is explicitly defaulted and not trivial or not public
    // or if the destructor is deleted, we clear HasIrrelevantDestructor in
    // finishedDefaultedOrDeletedMember.

    // C++11 [class.dtor]p5:
    //   A destructor is trivial if [...] the destructor is not virtual.
    if (DD->isVirtual()) {
      data().HasTrivialSpecialMembers &= ~SMF_Destructor;
      data().HasTrivialSpecialMembersForCall &= ~SMF_Destructor;
    }
  }

  // Handle member functions.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
    if (Method->isCopyAssignmentOperator()) {
      SMKind |= SMF_CopyAssignment;

      const auto *ParamTy =
          Method->getParamDecl(0)->getType()->getAs<ReferenceType>();
      if (!ParamTy || ParamTy->getPointeeType().isConstQualified())
        data().HasDeclaredCopyAssignmentWithConstParam = true;
    }

    if (Method->isMoveAssignmentOperator())
      SMKind |= SMF_MoveAssignment;

    // Keep the list of conversion functions up-to-date.
    if (auto *Conversion = dyn_cast<CXXConversionDecl>(D)) {
      // FIXME: We use the 'unsafe' accessor for the access specifier here,
      // because Sema may not have set it yet. That's really just a misdesign
      // in Sema. However, LLDB *will* have set the access specifier correctly,
      // and adds declarations after the class is technically completed,
      // so completeDefinition()'s overriding of the access specifiers doesn't
      // work.
      AccessSpecifier AS = Conversion->getAccessUnsafe();

      if (Conversion->getPrimaryTemplate()) {
        // We don't record specializations.
      } else {
        ASTContext &Ctx = getASTContext();
        ASTUnresolvedSet &Conversions = data().Conversions.get(Ctx);
        NamedDecl *Primary =
            FunTmpl ? cast<NamedDecl>(FunTmpl) : cast<NamedDecl>(Conversion);
        if (Primary->getPreviousDecl())
          Conversions.replace(cast<NamedDecl>(Primary->getPreviousDecl()),
                              Primary, AS);
        else
          Conversions.addDecl(Ctx, Primary, AS);
      }
    }

    if (SMKind) {
      // If this is the first declaration of a special member, we no longer have
      // an implicit trivial special member.
      data().HasTrivialSpecialMembers &=
          data().DeclaredSpecialMembers | ~SMKind;
      data().HasTrivialSpecialMembersForCall &=
          data().DeclaredSpecialMembers | ~SMKind;

      if (!Method->isImplicit() && !Method->isUserProvided()) {
        // This method is user-declared but not user-provided. We can't work out
        // whether it's trivial yet (not until we get to the end of the class).
        // We'll handle this method in finishedDefaultedOrDeletedMember.
      } else if (Method->isTrivial()) {
        data().HasTrivialSpecialMembers |= SMKind;
        data().HasTrivialSpecialMembersForCall |= SMKind;
      } else if (Method->isTrivialForCall()) {
        data().HasTrivialSpecialMembersForCall |= SMKind;
        data().DeclaredNonTrivialSpecialMembers |= SMKind;
      } else {
        data().DeclaredNonTrivialSpecialMembers |= SMKind;
        // If this is a user-provided function, do not set
        // DeclaredNonTrivialSpecialMembersForCall here since we don't know
        // yet whether the method would be considered non-trivial for the
        // purpose of calls (attribute "trivial_abi" can be dropped from the
        // class later, which can change the special method's triviality).
        if (!Method->isUserProvided())
          data().DeclaredNonTrivialSpecialMembersForCall |= SMKind;
      }

      // Note when we have declared a declared special member, and suppress the
      // implicit declaration of this special member.
      data().DeclaredSpecialMembers |= SMKind;

      if (!Method->isImplicit()) {
        data().UserDeclaredSpecialMembers |= SMKind;

        // C++03 [class]p4:
        //   A POD-struct is an aggregate class that has [...] no user-defined
        //   copy assignment operator and no user-defined destructor.
        //
        // Since the POD bit is meant to be C++03 POD-ness, and in C++03,
        // aggregates could not have any constructors, clear it even for an
        // explicitly defaulted or deleted constructor.
        // type is technically an aggregate in C++0x since it wouldn't be in 03.
        //
        // Also, a user-declared move assignment operator makes a class non-POD.
        // This is an extension in C++03.
        data().PlainOldData = false;
      }
    }

    return;
  }

  // Handle non-static data members.
  if (const auto *Field = dyn_cast<FieldDecl>(D)) {
    ASTContext &Context = getASTContext();

    // C++2a [class]p7:
    //   A standard-layout class is a class that:
    //    [...]
    //    -- has all non-static data members and bit-fields in the class and
    //       its base classes first declared in the same class
    if (data().HasBasesWithFields)
      data().IsStandardLayout = false;

    // C++ [class.bit]p2:
    //   A declaration for a bit-field that omits the identifier declares an
    //   unnamed bit-field. Unnamed bit-fields are not members and cannot be
    //   initialized.
    if (Field->isUnnamedBitfield()) {
      // C++ [meta.unary.prop]p4: [LWG2358]
      //   T is a class type [...] with [...] no unnamed bit-fields of non-zero
      //   length
      if (data().Empty && !Field->isZeroLengthBitField(Context) &&
          Context.getLangOpts().getClangABICompat() >
              LangOptions::ClangABI::Ver6)
        data().Empty = false;
      return;
    }

    // C++11 [class]p7:
    //   A standard-layout class is a class that:
    //    -- either has no non-static data members in the most derived class
    //       [...] or has no base classes with non-static data members
    if (data().HasBasesWithNonStaticDataMembers)
      data().IsCXX11StandardLayout = false;

    // C++ [dcl.init.aggr]p1:
    //   An aggregate is an array or a class (clause 9) with [...] no
    //   private or protected non-static data members (clause 11).
    //
    // A POD must be an aggregate.
    if (D->getAccess() == AS_private || D->getAccess() == AS_protected) {
      data().Aggregate = false;
      data().PlainOldData = false;
    }

    // Track whether this is the first field. We use this when checking
    // whether the class is standard-layout below.
    bool IsFirstField = !data().HasPrivateFields &&
                        !data().HasProtectedFields && !data().HasPublicFields;

    // C++0x [class]p7:
    //   A standard-layout class is a class that:
    //    [...]
    //    -- has the same access control for all non-static data members,
    switch (D->getAccess()) {
    case AS_private:    data().HasPrivateFields = true;   break;
    case AS_protected:  data().HasProtectedFields = true; break;
    case AS_public:     data().HasPublicFields = true;    break;
    case AS_none:       llvm_unreachable("Invalid access specifier");
    };
    if ((data().HasPrivateFields + data().HasProtectedFields +
         data().HasPublicFields) > 1) {
      data().IsStandardLayout = false;
      data().IsCXX11StandardLayout = false;
    }

    // Keep track of the presence of mutable fields.
    if (Field->isMutable()) {
      data().HasMutableFields = true;
      data().NeedOverloadResolutionForCopyConstructor = true;
    }

    // C++11 [class.union]p8, DR1460:
    //   If X is a union, a non-static data member of X that is not an anonymous
    //   union is a variant member of X.
    if (isUnion() && !Field->isAnonymousStructOrUnion())
      data().HasVariantMembers = true;

    // C++0x [class]p9:
    //   A POD struct is a class that is both a trivial class and a
    //   standard-layout class, and has no non-static data members of type
    //   non-POD struct, non-POD union (or array of such types).
    //
    // Automatic Reference Counting: the presence of a member of Objective-C pointer type
    // that does not explicitly have no lifetime makes the class a non-POD.
    QualType T = Context.getBaseElementType(Field->getType());
    if (T->isObjCRetainableType() || T.isObjCGCStrong()) {
      if (T.hasNonTrivialObjCLifetime()) {
        // Objective-C Automatic Reference Counting:
        //   If a class has a non-static data member of Objective-C pointer
        //   type (or array thereof), it is a non-POD type and its
        //   default constructor (if any), copy constructor, move constructor,
        //   copy assignment operator, move assignment operator, and destructor are
        //   non-trivial.
        setHasObjectMember(true);
        struct DefinitionData &Data = data();
        Data.PlainOldData = false;
        Data.HasTrivialSpecialMembers = 0;

        // __strong or __weak fields do not make special functions non-trivial
        // for the purpose of calls.
        Qualifiers::ObjCLifetime LT = T.getQualifiers().getObjCLifetime();
        if (LT != Qualifiers::OCL_Strong && LT != Qualifiers::OCL_Weak)
          data().HasTrivialSpecialMembersForCall = 0;

        // Structs with __weak fields should never be passed directly.
        if (LT == Qualifiers::OCL_Weak)
          setArgPassingRestrictions(RecordDecl::APK_CanNeverPassInRegs);

        Data.HasIrrelevantDestructor = false;
      } else if (!Context.getLangOpts().ObjCAutoRefCount) {
        setHasObjectMember(true);
      }
    } else if (!T.isCXX98PODType(Context))
      data().PlainOldData = false;

    if (T->isReferenceType()) {
      if (!Field->hasInClassInitializer())
        data().HasUninitializedReferenceMember = true;

      // C++0x [class]p7:
      //   A standard-layout class is a class that:
      //    -- has no non-static data members of type [...] reference,
      data().IsStandardLayout = false;
      data().IsCXX11StandardLayout = false;

      // C++1z [class.copy.ctor]p10:
      //   A defaulted copy constructor for a class X is defined as deleted if X has:
      //    -- a non-static data member of rvalue reference type
      if (T->isRValueReferenceType())
        data().DefaultedCopyConstructorIsDeleted = true;
    }

    if (!Field->hasInClassInitializer() && !Field->isMutable()) {
      if (CXXRecordDecl *FieldType = T->getAsCXXRecordDecl()) {
        if (FieldType->hasDefinition() && !FieldType->allowConstDefaultInit())
          data().HasUninitializedFields = true;
      } else {
        data().HasUninitializedFields = true;
      }
    }

    // Record if this field is the first non-literal or volatile field or base.
    if (!T->isLiteralType(Context) || T.isVolatileQualified())
      data().HasNonLiteralTypeFieldsOrBases = true;

    if (Field->hasInClassInitializer() ||
        (Field->isAnonymousStructOrUnion() &&
         Field->getType()->getAsCXXRecordDecl()->hasInClassInitializer())) {
      data().HasInClassInitializer = true;

      // C++11 [class]p5:
      //   A default constructor is trivial if [...] no non-static data member
      //   of its class has a brace-or-equal-initializer.
      data().HasTrivialSpecialMembers &= ~SMF_DefaultConstructor;

      // C++11 [dcl.init.aggr]p1:
      //   An aggregate is a [...] class with [...] no
      //   brace-or-equal-initializers for non-static data members.
      //
      // This rule was removed in C++14.
      if (!getASTContext().getLangOpts().CPlusPlus14)
        data().Aggregate = false;

      // C++11 [class]p10:
      //   A POD struct is [...] a trivial class.
      data().PlainOldData = false;
    }

    // C++11 [class.copy]p23:
    //   A defaulted copy/move assignment operator for a class X is defined
    //   as deleted if X has:
    //    -- a non-static data member of reference type
    if (T->isReferenceType())
      data().DefaultedMoveAssignmentIsDeleted = true;

    if (const auto *RecordTy = T->getAs<RecordType>()) {
      auto *FieldRec = cast<CXXRecordDecl>(RecordTy->getDecl());
      if (FieldRec->getDefinition()) {
        addedClassSubobject(FieldRec);

        // We may need to perform overload resolution to determine whether a
        // field can be moved if it's const or volatile qualified.
        if (T.getCVRQualifiers() & (Qualifiers::Const | Qualifiers::Volatile)) {
          // We need to care about 'const' for the copy constructor because an
          // implicit copy constructor might be declared with a non-const
          // parameter.
          data().NeedOverloadResolutionForCopyConstructor = true;
          data().NeedOverloadResolutionForMoveConstructor = true;
          data().NeedOverloadResolutionForMoveAssignment = true;
        }

        // C++11 [class.ctor]p5, C++11 [class.copy]p11:
        //   A defaulted [special member] for a class X is defined as
        //   deleted if:
        //    -- X is a union-like class that has a variant member with a
        //       non-trivial [corresponding special member]
        if (isUnion()) {
          if (FieldRec->hasNonTrivialCopyConstructor())
            data().DefaultedCopyConstructorIsDeleted = true;
          if (FieldRec->hasNonTrivialMoveConstructor())
            data().DefaultedMoveConstructorIsDeleted = true;
          if (FieldRec->hasNonTrivialMoveAssignment())
            data().DefaultedMoveAssignmentIsDeleted = true;
          if (FieldRec->hasNonTrivialDestructor())
            data().DefaultedDestructorIsDeleted = true;
        }

        // For an anonymous union member, our overload resolution will perform
        // overload resolution for its members.
        if (Field->isAnonymousStructOrUnion()) {
          data().NeedOverloadResolutionForCopyConstructor |=
              FieldRec->data().NeedOverloadResolutionForCopyConstructor;
          data().NeedOverloadResolutionForMoveConstructor |=
              FieldRec->data().NeedOverloadResolutionForMoveConstructor;
          data().NeedOverloadResolutionForMoveAssignment |=
              FieldRec->data().NeedOverloadResolutionForMoveAssignment;
          data().NeedOverloadResolutionForDestructor |=
              FieldRec->data().NeedOverloadResolutionForDestructor;
        }

        // C++0x [class.ctor]p5:
        //   A default constructor is trivial [...] if:
        //    -- for all the non-static data members of its class that are of
        //       class type (or array thereof), each such class has a trivial
        //       default constructor.
        if (!FieldRec->hasTrivialDefaultConstructor())
          data().HasTrivialSpecialMembers &= ~SMF_DefaultConstructor;

        // C++0x [class.copy]p13:
        //   A copy/move constructor for class X is trivial if [...]
        //    [...]
        //    -- for each non-static data member of X that is of class type (or
        //       an array thereof), the constructor selected to copy/move that
        //       member is trivial;
        if (!FieldRec->hasTrivialCopyConstructor())
          data().HasTrivialSpecialMembers &= ~SMF_CopyConstructor;

        if (!FieldRec->hasTrivialCopyConstructorForCall())
          data().HasTrivialSpecialMembersForCall &= ~SMF_CopyConstructor;

        // If the field doesn't have a simple move constructor, we'll eagerly
        // declare the move constructor for this class and we'll decide whether
        // it's trivial then.
        if (!FieldRec->hasTrivialMoveConstructor())
          data().HasTrivialSpecialMembers &= ~SMF_MoveConstructor;

        if (!FieldRec->hasTrivialMoveConstructorForCall())
          data().HasTrivialSpecialMembersForCall &= ~SMF_MoveConstructor;

        // C++0x [class.copy]p27:
        //   A copy/move assignment operator for class X is trivial if [...]
        //    [...]
        //    -- for each non-static data member of X that is of class type (or
        //       an array thereof), the assignment operator selected to
        //       copy/move that member is trivial;
        if (!FieldRec->hasTrivialCopyAssignment())
          data().HasTrivialSpecialMembers &= ~SMF_CopyAssignment;
        // If the field doesn't have a simple move assignment, we'll eagerly
        // declare the move assignment for this class and we'll decide whether
        // it's trivial then.
        if (!FieldRec->hasTrivialMoveAssignment())
          data().HasTrivialSpecialMembers &= ~SMF_MoveAssignment;

        if (!FieldRec->hasTrivialDestructor())
          data().HasTrivialSpecialMembers &= ~SMF_Destructor;
        if (!FieldRec->hasTrivialDestructorForCall())
          data().HasTrivialSpecialMembersForCall &= ~SMF_Destructor;
        if (!FieldRec->hasIrrelevantDestructor())
          data().HasIrrelevantDestructor = false;
        if (FieldRec->hasObjectMember())
          setHasObjectMember(true);
        if (FieldRec->hasVolatileMember())
          setHasVolatileMember(true);
        if (FieldRec->getArgPassingRestrictions() ==
            RecordDecl::APK_CanNeverPassInRegs)
          setArgPassingRestrictions(RecordDecl::APK_CanNeverPassInRegs);

        // C++0x [class]p7:
        //   A standard-layout class is a class that:
        //    -- has no non-static data members of type non-standard-layout
        //       class (or array of such types) [...]
        if (!FieldRec->isStandardLayout())
          data().IsStandardLayout = false;
        if (!FieldRec->isCXX11StandardLayout())
          data().IsCXX11StandardLayout = false;

        // C++2a [class]p7:
        //   A standard-layout class is a class that:
        //    [...]
        //    -- has no element of the set M(S) of types as a base class.
        if (data().IsStandardLayout && (isUnion() || IsFirstField) &&
            hasSubobjectAtOffsetZeroOfEmptyBaseType(Context, FieldRec))
          data().IsStandardLayout = false;

        // C++11 [class]p7:
        //   A standard-layout class is a class that:
        //    -- has no base classes of the same type as the first non-static
        //       data member
        if (data().IsCXX11StandardLayout && IsFirstField) {
          // FIXME: We should check all base classes here, not just direct
          // base classes.
          for (const auto &BI : bases()) {
            if (Context.hasSameUnqualifiedType(BI.getType(), T)) {
              data().IsCXX11StandardLayout = false;
              break;
            }
          }
        }

        // Keep track of the presence of mutable fields.
        if (FieldRec->hasMutableFields()) {
          data().HasMutableFields = true;
          data().NeedOverloadResolutionForCopyConstructor = true;
        }

        // C++11 [class.copy]p13:
        //   If the implicitly-defined constructor would satisfy the
        //   requirements of a constexpr constructor, the implicitly-defined
        //   constructor is constexpr.
        // C++11 [dcl.constexpr]p4:
        //    -- every constructor involved in initializing non-static data
        //       members [...] shall be a constexpr constructor
        if (!Field->hasInClassInitializer() &&
            !FieldRec->hasConstexprDefaultConstructor() && !isUnion())
          // The standard requires any in-class initializer to be a constant
          // expression. We consider this to be a defect.
          data().DefaultedDefaultConstructorIsConstexpr = false;

        // C++11 [class.copy]p8:
        //   The implicitly-declared copy constructor for a class X will have
        //   the form 'X::X(const X&)' if each potentially constructed subobject
        //   of a class type M (or array thereof) has a copy constructor whose
        //   first parameter is of type 'const M&' or 'const volatile M&'.
        if (!FieldRec->hasCopyConstructorWithConstParam())
          data().ImplicitCopyConstructorCanHaveConstParamForNonVBase = false;

        // C++11 [class.copy]p18:
        //   The implicitly-declared copy assignment oeprator for a class X will
        //   have the form 'X& X::operator=(const X&)' if [...] for all the
        //   non-static data members of X that are of a class type M (or array
        //   thereof), each such class type has a copy assignment operator whose
        //   parameter is of type 'const M&', 'const volatile M&' or 'M'.
        if (!FieldRec->hasCopyAssignmentWithConstParam())
          data().ImplicitCopyAssignmentHasConstParam = false;

        if (FieldRec->hasUninitializedReferenceMember() &&
            !Field->hasInClassInitializer())
          data().HasUninitializedReferenceMember = true;

        // C++11 [class.union]p8, DR1460:
        //   a non-static data member of an anonymous union that is a member of
        //   X is also a variant member of X.
        if (FieldRec->hasVariantMembers() &&
            Field->isAnonymousStructOrUnion())
          data().HasVariantMembers = true;
      }
    } else {
      // Base element type of field is a non-class type.
      if (!T->isLiteralType(Context) ||
          (!Field->hasInClassInitializer() && !isUnion()))
        data().DefaultedDefaultConstructorIsConstexpr = false;

      // C++11 [class.copy]p23:
      //   A defaulted copy/move assignment operator for a class X is defined
      //   as deleted if X has:
      //    -- a non-static data member of const non-class type (or array
      //       thereof)
      if (T.isConstQualified())
        data().DefaultedMoveAssignmentIsDeleted = true;
    }

    // C++14 [meta.unary.prop]p4:
    //   T is a class type [...] with [...] no non-static data members
    data().Empty = false;
  }

  // Handle using declarations of conversion functions.
  if (auto *Shadow = dyn_cast<UsingShadowDecl>(D)) {
    if (Shadow->getDeclName().getNameKind()
          == DeclarationName::CXXConversionFunctionName) {
      ASTContext &Ctx = getASTContext();
      data().Conversions.get(Ctx).addDecl(Ctx, Shadow, Shadow->getAccess());
    }
  }

  if (const auto *Using = dyn_cast<UsingDecl>(D)) {
    if (Using->getDeclName().getNameKind() ==
        DeclarationName::CXXConstructorName) {
      data().HasInheritedConstructor = true;
      // C++1z [dcl.init.aggr]p1:
      //  An aggregate is [...] a class [...] with no inherited constructors
      data().Aggregate = false;
    }

    if (Using->getDeclName().getCXXOverloadedOperator() == OO_Equal)
      data().HasInheritedAssignment = true;
  }
}

void CXXRecordDecl::finishedDefaultedOrDeletedMember(CXXMethodDecl *D) {
  assert(!D->isImplicit() && !D->isUserProvided());

  // The kind of special member this declaration is, if any.
  unsigned SMKind = 0;

  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    if (Constructor->isDefaultConstructor()) {
      SMKind |= SMF_DefaultConstructor;
      if (Constructor->isConstexpr())
        data().HasConstexprDefaultConstructor = true;
    }
    if (Constructor->isCopyConstructor())
      SMKind |= SMF_CopyConstructor;
    else if (Constructor->isMoveConstructor())
      SMKind |= SMF_MoveConstructor;
    else if (Constructor->isConstexpr())
      // We may now know that the constructor is constexpr.
      data().HasConstexprNonCopyMoveConstructor = true;
  } else if (isa<CXXDestructorDecl>(D)) {
    SMKind |= SMF_Destructor;
    if (!D->isTrivial() || D->getAccess() != AS_public || D->isDeleted())
      data().HasIrrelevantDestructor = false;
  } else if (D->isCopyAssignmentOperator())
    SMKind |= SMF_CopyAssignment;
  else if (D->isMoveAssignmentOperator())
    SMKind |= SMF_MoveAssignment;

  // Update which trivial / non-trivial special members we have.
  // addedMember will have skipped this step for this member.
  if (D->isTrivial())
    data().HasTrivialSpecialMembers |= SMKind;
  else
    data().DeclaredNonTrivialSpecialMembers |= SMKind;
}

void CXXRecordDecl::setTrivialForCallFlags(CXXMethodDecl *D) {
  unsigned SMKind = 0;

  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    if (Constructor->isCopyConstructor())
      SMKind = SMF_CopyConstructor;
    else if (Constructor->isMoveConstructor())
      SMKind = SMF_MoveConstructor;
  } else if (isa<CXXDestructorDecl>(D))
    SMKind = SMF_Destructor;

  if (D->isTrivialForCall())
    data().HasTrivialSpecialMembersForCall |= SMKind;
  else
    data().DeclaredNonTrivialSpecialMembersForCall |= SMKind;
}

bool CXXRecordDecl::isCLike() const {
  if (getTagKind() == TTK_Class || getTagKind() == TTK_Interface ||
      !TemplateOrInstantiation.isNull())
    return false;
  if (!hasDefinition())
    return true;

  return isPOD() && data().HasOnlyCMembers;
}

bool CXXRecordDecl::isGenericLambda() const {
  if (!isLambda()) return false;
  return getLambdaData().IsGenericLambda;
}

#ifndef NDEBUG
static bool allLookupResultsAreTheSame(const DeclContext::lookup_result &R) {
  for (auto *D : R)
    if (!declaresSameEntity(D, R.front()))
      return false;
  return true;
}
#endif

CXXMethodDecl* CXXRecordDecl::getLambdaCallOperator() const {
  if (!isLambda()) return nullptr;
  DeclarationName Name =
    getASTContext().DeclarationNames.getCXXOperatorName(OO_Call);
  DeclContext::lookup_result Calls = lookup(Name);

  assert(!Calls.empty() && "Missing lambda call operator!");
  assert(allLookupResultsAreTheSame(Calls) &&
         "More than one lambda call operator!");

  NamedDecl *CallOp = Calls.front();
  if (const auto *CallOpTmpl = dyn_cast<FunctionTemplateDecl>(CallOp))
    return cast<CXXMethodDecl>(CallOpTmpl->getTemplatedDecl());

  return cast<CXXMethodDecl>(CallOp);
}

CXXMethodDecl* CXXRecordDecl::getLambdaStaticInvoker() const {
  if (!isLambda()) return nullptr;
  DeclarationName Name =
    &getASTContext().Idents.get(getLambdaStaticInvokerName());
  DeclContext::lookup_result Invoker = lookup(Name);
  if (Invoker.empty()) return nullptr;
  assert(allLookupResultsAreTheSame(Invoker) &&
         "More than one static invoker operator!");
  NamedDecl *InvokerFun = Invoker.front();
  if (const auto *InvokerTemplate = dyn_cast<FunctionTemplateDecl>(InvokerFun))
    return cast<CXXMethodDecl>(InvokerTemplate->getTemplatedDecl());

  return cast<CXXMethodDecl>(InvokerFun);
}

void CXXRecordDecl::getCaptureFields(
       llvm::DenseMap<const VarDecl *, FieldDecl *> &Captures,
       FieldDecl *&ThisCapture) const {
  Captures.clear();
  ThisCapture = nullptr;

  LambdaDefinitionData &Lambda = getLambdaData();
  RecordDecl::field_iterator Field = field_begin();
  for (const LambdaCapture *C = Lambda.Captures, *CEnd = C + Lambda.NumCaptures;
       C != CEnd; ++C, ++Field) {
    if (C->capturesThis())
      ThisCapture = *Field;
    else if (C->capturesVariable())
      Captures[C->getCapturedVar()] = *Field;
  }
  assert(Field == field_end());
}

TemplateParameterList *
CXXRecordDecl::getGenericLambdaTemplateParameterList() const {
  if (!isLambda()) return nullptr;
  CXXMethodDecl *CallOp = getLambdaCallOperator();
  if (FunctionTemplateDecl *Tmpl = CallOp->getDescribedFunctionTemplate())
    return Tmpl->getTemplateParameters();
  return nullptr;
}

Decl *CXXRecordDecl::getLambdaContextDecl() const {
  assert(isLambda() && "Not a lambda closure type!");
  ExternalASTSource *Source = getParentASTContext().getExternalSource();
  return getLambdaData().ContextDecl.get(Source);
}

static CanQualType GetConversionType(ASTContext &Context, NamedDecl *Conv) {
  QualType T =
      cast<CXXConversionDecl>(Conv->getUnderlyingDecl()->getAsFunction())
          ->getConversionType();
  return Context.getCanonicalType(T);
}

/// Collect the visible conversions of a base class.
///
/// \param Record a base class of the class we're considering
/// \param InVirtual whether this base class is a virtual base (or a base
///   of a virtual base)
/// \param Access the access along the inheritance path to this base
/// \param ParentHiddenTypes the conversions provided by the inheritors
///   of this base
/// \param Output the set to which to add conversions from non-virtual bases
/// \param VOutput the set to which to add conversions from virtual bases
/// \param HiddenVBaseCs the set of conversions which were hidden in a
///   virtual base along some inheritance path
static void CollectVisibleConversions(ASTContext &Context,
                                      CXXRecordDecl *Record,
                                      bool InVirtual,
                                      AccessSpecifier Access,
                  const llvm::SmallPtrSet<CanQualType, 8> &ParentHiddenTypes,
                                      ASTUnresolvedSet &Output,
                                      UnresolvedSetImpl &VOutput,
                           llvm::SmallPtrSet<NamedDecl*, 8> &HiddenVBaseCs) {
  // The set of types which have conversions in this class or its
  // subclasses.  As an optimization, we don't copy the derived set
  // unless it might change.
  const llvm::SmallPtrSet<CanQualType, 8> *HiddenTypes = &ParentHiddenTypes;
  llvm::SmallPtrSet<CanQualType, 8> HiddenTypesBuffer;

  // Collect the direct conversions and figure out which conversions
  // will be hidden in the subclasses.
  CXXRecordDecl::conversion_iterator ConvI = Record->conversion_begin();
  CXXRecordDecl::conversion_iterator ConvE = Record->conversion_end();
  if (ConvI != ConvE) {
    HiddenTypesBuffer = ParentHiddenTypes;
    HiddenTypes = &HiddenTypesBuffer;

    for (CXXRecordDecl::conversion_iterator I = ConvI; I != ConvE; ++I) {
      CanQualType ConvType(GetConversionType(Context, I.getDecl()));
      bool Hidden = ParentHiddenTypes.count(ConvType);
      if (!Hidden)
        HiddenTypesBuffer.insert(ConvType);

      // If this conversion is hidden and we're in a virtual base,
      // remember that it's hidden along some inheritance path.
      if (Hidden && InVirtual)
        HiddenVBaseCs.insert(cast<NamedDecl>(I.getDecl()->getCanonicalDecl()));

      // If this conversion isn't hidden, add it to the appropriate output.
      else if (!Hidden) {
        AccessSpecifier IAccess
          = CXXRecordDecl::MergeAccess(Access, I.getAccess());

        if (InVirtual)
          VOutput.addDecl(I.getDecl(), IAccess);
        else
          Output.addDecl(Context, I.getDecl(), IAccess);
      }
    }
  }

  // Collect information recursively from any base classes.
  for (const auto &I : Record->bases()) {
    const RecordType *RT = I.getType()->getAs<RecordType>();
    if (!RT) continue;

    AccessSpecifier BaseAccess
      = CXXRecordDecl::MergeAccess(Access, I.getAccessSpecifier());
    bool BaseInVirtual = InVirtual || I.isVirtual();

    auto *Base = cast<CXXRecordDecl>(RT->getDecl());
    CollectVisibleConversions(Context, Base, BaseInVirtual, BaseAccess,
                              *HiddenTypes, Output, VOutput, HiddenVBaseCs);
  }
}

/// Collect the visible conversions of a class.
///
/// This would be extremely straightforward if it weren't for virtual
/// bases.  It might be worth special-casing that, really.
static void CollectVisibleConversions(ASTContext &Context,
                                      CXXRecordDecl *Record,
                                      ASTUnresolvedSet &Output) {
  // The collection of all conversions in virtual bases that we've
  // found.  These will be added to the output as long as they don't
  // appear in the hidden-conversions set.
  UnresolvedSet<8> VBaseCs;

  // The set of conversions in virtual bases that we've determined to
  // be hidden.
  llvm::SmallPtrSet<NamedDecl*, 8> HiddenVBaseCs;

  // The set of types hidden by classes derived from this one.
  llvm::SmallPtrSet<CanQualType, 8> HiddenTypes;

  // Go ahead and collect the direct conversions and add them to the
  // hidden-types set.
  CXXRecordDecl::conversion_iterator ConvI = Record->conversion_begin();
  CXXRecordDecl::conversion_iterator ConvE = Record->conversion_end();
  Output.append(Context, ConvI, ConvE);
  for (; ConvI != ConvE; ++ConvI)
    HiddenTypes.insert(GetConversionType(Context, ConvI.getDecl()));

  // Recursively collect conversions from base classes.
  for (const auto &I : Record->bases()) {
    const RecordType *RT = I.getType()->getAs<RecordType>();
    if (!RT) continue;

    CollectVisibleConversions(Context, cast<CXXRecordDecl>(RT->getDecl()),
                              I.isVirtual(), I.getAccessSpecifier(),
                              HiddenTypes, Output, VBaseCs, HiddenVBaseCs);
  }

  // Add any unhidden conversions provided by virtual bases.
  for (UnresolvedSetIterator I = VBaseCs.begin(), E = VBaseCs.end();
         I != E; ++I) {
    if (!HiddenVBaseCs.count(cast<NamedDecl>(I.getDecl()->getCanonicalDecl())))
      Output.addDecl(Context, I.getDecl(), I.getAccess());
  }
}

/// getVisibleConversionFunctions - get all conversion functions visible
/// in current class; including conversion function templates.
llvm::iterator_range<CXXRecordDecl::conversion_iterator>
CXXRecordDecl::getVisibleConversionFunctions() {
  ASTContext &Ctx = getASTContext();

  ASTUnresolvedSet *Set;
  if (bases_begin() == bases_end()) {
    // If root class, all conversions are visible.
    Set = &data().Conversions.get(Ctx);
  } else {
    Set = &data().VisibleConversions.get(Ctx);
    // If visible conversion list is not evaluated, evaluate it.
    if (!data().ComputedVisibleConversions) {
      CollectVisibleConversions(Ctx, this, *Set);
      data().ComputedVisibleConversions = true;
    }
  }
  return llvm::make_range(Set->begin(), Set->end());
}

void CXXRecordDecl::removeConversion(const NamedDecl *ConvDecl) {
  // This operation is O(N) but extremely rare.  Sema only uses it to
  // remove UsingShadowDecls in a class that were followed by a direct
  // declaration, e.g.:
  //   class A : B {
  //     using B::operator int;
  //     operator int();
  //   };
  // This is uncommon by itself and even more uncommon in conjunction
  // with sufficiently large numbers of directly-declared conversions
  // that asymptotic behavior matters.

  ASTUnresolvedSet &Convs = data().Conversions.get(getASTContext());
  for (unsigned I = 0, E = Convs.size(); I != E; ++I) {
    if (Convs[I].getDecl() == ConvDecl) {
      Convs.erase(I);
      assert(std::find(Convs.begin(), Convs.end(), ConvDecl) == Convs.end()
             && "conversion was found multiple times in unresolved set");
      return;
    }
  }

  llvm_unreachable("conversion not found in set!");
}

CXXRecordDecl *CXXRecordDecl::getInstantiatedFromMemberClass() const {
  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo())
    return cast<CXXRecordDecl>(MSInfo->getInstantiatedFrom());

  return nullptr;
}

MemberSpecializationInfo *CXXRecordDecl::getMemberSpecializationInfo() const {
  return TemplateOrInstantiation.dyn_cast<MemberSpecializationInfo *>();
}

void
CXXRecordDecl::setInstantiationOfMemberClass(CXXRecordDecl *RD,
                                             TemplateSpecializationKind TSK) {
  assert(TemplateOrInstantiation.isNull() &&
         "Previous template or instantiation?");
  assert(!isa<ClassTemplatePartialSpecializationDecl>(this));
  TemplateOrInstantiation
    = new (getASTContext()) MemberSpecializationInfo(RD, TSK);
}

ClassTemplateDecl *CXXRecordDecl::getDescribedClassTemplate() const {
  return TemplateOrInstantiation.dyn_cast<ClassTemplateDecl *>();
}

void CXXRecordDecl::setDescribedClassTemplate(ClassTemplateDecl *Template) {
  TemplateOrInstantiation = Template;
}

TemplateSpecializationKind CXXRecordDecl::getTemplateSpecializationKind() const{
  if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(this))
    return Spec->getSpecializationKind();

  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo())
    return MSInfo->getTemplateSpecializationKind();

  return TSK_Undeclared;
}

void
CXXRecordDecl::setTemplateSpecializationKind(TemplateSpecializationKind TSK) {
  if (auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(this)) {
    Spec->setSpecializationKind(TSK);
    return;
  }

  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo()) {
    MSInfo->setTemplateSpecializationKind(TSK);
    return;
  }

  llvm_unreachable("Not a class template or member class specialization");
}

const CXXRecordDecl *CXXRecordDecl::getTemplateInstantiationPattern() const {
  auto GetDefinitionOrSelf =
      [](const CXXRecordDecl *D) -> const CXXRecordDecl * {
    if (auto *Def = D->getDefinition())
      return Def;
    return D;
  };

  // If it's a class template specialization, find the template or partial
  // specialization from which it was instantiated.
  if (auto *TD = dyn_cast<ClassTemplateSpecializationDecl>(this)) {
    auto From = TD->getInstantiatedFrom();
    if (auto *CTD = From.dyn_cast<ClassTemplateDecl *>()) {
      while (auto *NewCTD = CTD->getInstantiatedFromMemberTemplate()) {
        if (NewCTD->isMemberSpecialization())
          break;
        CTD = NewCTD;
      }
      return GetDefinitionOrSelf(CTD->getTemplatedDecl());
    }
    if (auto *CTPSD =
            From.dyn_cast<ClassTemplatePartialSpecializationDecl *>()) {
      while (auto *NewCTPSD = CTPSD->getInstantiatedFromMember()) {
        if (NewCTPSD->isMemberSpecialization())
          break;
        CTPSD = NewCTPSD;
      }
      return GetDefinitionOrSelf(CTPSD);
    }
  }

  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo()) {
    if (isTemplateInstantiation(MSInfo->getTemplateSpecializationKind())) {
      const CXXRecordDecl *RD = this;
      while (auto *NewRD = RD->getInstantiatedFromMemberClass())
        RD = NewRD;
      return GetDefinitionOrSelf(RD);
    }
  }

  assert(!isTemplateInstantiation(this->getTemplateSpecializationKind()) &&
         "couldn't find pattern for class template instantiation");
  return nullptr;
}

CXXDestructorDecl *CXXRecordDecl::getDestructor() const {
  ASTContext &Context = getASTContext();
  QualType ClassType = Context.getTypeDeclType(this);

  DeclarationName Name
    = Context.DeclarationNames.getCXXDestructorName(
                                          Context.getCanonicalType(ClassType));

  DeclContext::lookup_result R = lookup(Name);

  return R.empty() ? nullptr : dyn_cast<CXXDestructorDecl>(R.front());
}

bool CXXRecordDecl::isAnyDestructorNoReturn() const {
  // Destructor is noreturn.
  if (const CXXDestructorDecl *Destructor = getDestructor())
    if (Destructor->isNoReturn())
      return true;

  // Check base classes destructor for noreturn.
  for (const auto &Base : bases())
    if (const CXXRecordDecl *RD = Base.getType()->getAsCXXRecordDecl())
      if (RD->isAnyDestructorNoReturn())
        return true;

  // Check fields for noreturn.
  for (const auto *Field : fields())
    if (const CXXRecordDecl *RD =
            Field->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl())
      if (RD->isAnyDestructorNoReturn())
        return true;

  // All destructors are not noreturn.
  return false;
}

static bool isDeclContextInNamespace(const DeclContext *DC) {
  while (!DC->isTranslationUnit()) {
    if (DC->isNamespace())
      return true;
    DC = DC->getParent();
  }
  return false;
}

bool CXXRecordDecl::isInterfaceLike() const {
  assert(hasDefinition() && "checking for interface-like without a definition");
  // All __interfaces are inheritently interface-like.
  if (isInterface())
    return true;

  // Interface-like types cannot have a user declared constructor, destructor,
  // friends, VBases, conversion functions, or fields.  Additionally, lambdas
  // cannot be interface types.
  if (isLambda() || hasUserDeclaredConstructor() ||
      hasUserDeclaredDestructor() || !field_empty() || hasFriends() ||
      getNumVBases() > 0 || conversion_end() - conversion_begin() > 0)
    return false;

  // No interface-like type can have a method with a definition.
  for (const auto *const Method : methods())
    if (Method->isDefined() && !Method->isImplicit())
      return false;

  // Check "Special" types.
  const auto *Uuid = getAttr<UuidAttr>();
  // MS SDK declares IUnknown/IDispatch both in the root of a TU, or in an
  // extern C++ block directly in the TU.  These are only valid if in one
  // of these two situations.
  if (Uuid && isStruct() && !getDeclContext()->isExternCContext() &&
      !isDeclContextInNamespace(getDeclContext()) &&
      ((getName() == "IUnknown" &&
        Uuid->getGuid() == "00000000-0000-0000-C000-000000000046") ||
       (getName() == "IDispatch" &&
        Uuid->getGuid() == "00020400-0000-0000-C000-000000000046"))) {
    if (getNumBases() > 0)
      return false;
    return true;
  }

  // FIXME: Any access specifiers is supposed to make this no longer interface
  // like.

  // If this isn't a 'special' type, it must have a single interface-like base.
  if (getNumBases() != 1)
    return false;

  const auto BaseSpec = *bases_begin();
  if (BaseSpec.isVirtual() || BaseSpec.getAccessSpecifier() != AS_public)
    return false;
  const auto *Base = BaseSpec.getType()->getAsCXXRecordDecl();
  if (Base->isInterface() || !Base->isInterfaceLike())
    return false;
  return true;
}

void CXXRecordDecl::completeDefinition() {
  completeDefinition(nullptr);
}

void CXXRecordDecl::completeDefinition(CXXFinalOverriderMap *FinalOverriders) {
  RecordDecl::completeDefinition();

  // If the class may be abstract (but hasn't been marked as such), check for
  // any pure final overriders.
  if (mayBeAbstract()) {
    CXXFinalOverriderMap MyFinalOverriders;
    if (!FinalOverriders) {
      getFinalOverriders(MyFinalOverriders);
      FinalOverriders = &MyFinalOverriders;
    }

    bool Done = false;
    for (CXXFinalOverriderMap::iterator M = FinalOverriders->begin(),
                                     MEnd = FinalOverriders->end();
         M != MEnd && !Done; ++M) {
      for (OverridingMethods::iterator SO = M->second.begin(),
                                    SOEnd = M->second.end();
           SO != SOEnd && !Done; ++SO) {
        assert(SO->second.size() > 0 &&
               "All virtual functions have overriding virtual functions");

        // C++ [class.abstract]p4:
        //   A class is abstract if it contains or inherits at least one
        //   pure virtual function for which the final overrider is pure
        //   virtual.
        if (SO->second.front().Method->isPure()) {
          data().Abstract = true;
          Done = true;
          break;
        }
      }
    }
  }

  // Set access bits correctly on the directly-declared conversions.
  for (conversion_iterator I = conversion_begin(), E = conversion_end();
       I != E; ++I)
    I.setAccess((*I)->getAccess());
}

bool CXXRecordDecl::mayBeAbstract() const {
  if (data().Abstract || isInvalidDecl() || !data().Polymorphic ||
      isDependentContext())
    return false;

  for (const auto &B : bases()) {
    const auto *BaseDecl =
        cast<CXXRecordDecl>(B.getType()->getAs<RecordType>()->getDecl());
    if (BaseDecl->isAbstract())
      return true;
  }

  return false;
}

void CXXDeductionGuideDecl::anchor() {}

CXXDeductionGuideDecl *CXXDeductionGuideDecl::Create(
    ASTContext &C, DeclContext *DC, SourceLocation StartLoc, bool IsExplicit,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    SourceLocation EndLocation) {
  return new (C, DC) CXXDeductionGuideDecl(C, DC, StartLoc, IsExplicit,
                                           NameInfo, T, TInfo, EndLocation);
}

CXXDeductionGuideDecl *CXXDeductionGuideDecl::CreateDeserialized(ASTContext &C,
                                                                 unsigned ID) {
  return new (C, ID) CXXDeductionGuideDecl(C, nullptr, SourceLocation(), false,
                                           DeclarationNameInfo(), QualType(),
                                           nullptr, SourceLocation());
}

void CXXMethodDecl::anchor() {}

bool CXXMethodDecl::isStatic() const {
  const CXXMethodDecl *MD = getCanonicalDecl();

  if (MD->getStorageClass() == SC_Static)
    return true;

  OverloadedOperatorKind OOK = getDeclName().getCXXOverloadedOperator();
  return isStaticOverloadedOperator(OOK);
}

static bool recursivelyOverrides(const CXXMethodDecl *DerivedMD,
                                 const CXXMethodDecl *BaseMD) {
  for (const CXXMethodDecl *MD : DerivedMD->overridden_methods()) {
    if (MD->getCanonicalDecl() == BaseMD->getCanonicalDecl())
      return true;
    if (recursivelyOverrides(MD, BaseMD))
      return true;
  }
  return false;
}

CXXMethodDecl *
CXXMethodDecl::getCorrespondingMethodInClass(const CXXRecordDecl *RD,
                                             bool MayBeBase) {
  if (this->getParent()->getCanonicalDecl() == RD->getCanonicalDecl())
    return this;

  // Lookup doesn't work for destructors, so handle them separately.
  if (isa<CXXDestructorDecl>(this)) {
    CXXMethodDecl *MD = RD->getDestructor();
    if (MD) {
      if (recursivelyOverrides(MD, this))
        return MD;
      if (MayBeBase && recursivelyOverrides(this, MD))
        return MD;
    }
    return nullptr;
  }

  for (auto *ND : RD->lookup(getDeclName())) {
    auto *MD = dyn_cast<CXXMethodDecl>(ND);
    if (!MD)
      continue;
    if (recursivelyOverrides(MD, this))
      return MD;
    if (MayBeBase && recursivelyOverrides(this, MD))
      return MD;
  }

  for (const auto &I : RD->bases()) {
    const RecordType *RT = I.getType()->getAs<RecordType>();
    if (!RT)
      continue;
    const auto *Base = cast<CXXRecordDecl>(RT->getDecl());
    CXXMethodDecl *T = this->getCorrespondingMethodInClass(Base);
    if (T)
      return T;
  }

  return nullptr;
}

CXXMethodDecl *
CXXMethodDecl::Create(ASTContext &C, CXXRecordDecl *RD,
                      SourceLocation StartLoc,
                      const DeclarationNameInfo &NameInfo,
                      QualType T, TypeSourceInfo *TInfo,
                      StorageClass SC, bool isInline,
                      bool isConstexpr, SourceLocation EndLocation) {
  return new (C, RD) CXXMethodDecl(CXXMethod, C, RD, StartLoc, NameInfo,
                                   T, TInfo, SC, isInline, isConstexpr,
                                   EndLocation);
}

CXXMethodDecl *CXXMethodDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) CXXMethodDecl(CXXMethod, C, nullptr, SourceLocation(),
                                   DeclarationNameInfo(), QualType(), nullptr,
                                   SC_None, false, false, SourceLocation());
}

CXXMethodDecl *CXXMethodDecl::getDevirtualizedMethod(const Expr *Base,
                                                     bool IsAppleKext) {
  assert(isVirtual() && "this method is expected to be virtual");

  // When building with -fapple-kext, all calls must go through the vtable since
  // the kernel linker can do runtime patching of vtables.
  if (IsAppleKext)
    return nullptr;

  // If the member function is marked 'final', we know that it can't be
  // overridden and can therefore devirtualize it unless it's pure virtual.
  if (hasAttr<FinalAttr>())
    return isPure() ? nullptr : this;

  // If Base is unknown, we cannot devirtualize.
  if (!Base)
    return nullptr;

  // If the base expression (after skipping derived-to-base conversions) is a
  // class prvalue, then we can devirtualize.
  Base = Base->getBestDynamicClassTypeExpr();
  if (Base->isRValue() && Base->getType()->isRecordType())
    return this;

  // If we don't even know what we would call, we can't devirtualize.
  const CXXRecordDecl *BestDynamicDecl = Base->getBestDynamicClassType();
  if (!BestDynamicDecl)
    return nullptr;

  // There may be a method corresponding to MD in a derived class.
  CXXMethodDecl *DevirtualizedMethod =
      getCorrespondingMethodInClass(BestDynamicDecl);

  // If that method is pure virtual, we can't devirtualize. If this code is
  // reached, the result would be UB, not a direct call to the derived class
  // function, and we can't assume the derived class function is defined.
  if (DevirtualizedMethod->isPure())
    return nullptr;

  // If that method is marked final, we can devirtualize it.
  if (DevirtualizedMethod->hasAttr<FinalAttr>())
    return DevirtualizedMethod;

  // Similarly, if the class itself is marked 'final' it can't be overridden
  // and we can therefore devirtualize the member function call.
  if (BestDynamicDecl->hasAttr<FinalAttr>())
    return DevirtualizedMethod;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (VD->getType()->isRecordType())
        // This is a record decl. We know the type and can devirtualize it.
        return DevirtualizedMethod;

    return nullptr;
  }

  // We can devirtualize calls on an object accessed by a class member access
  // expression, since by C++11 [basic.life]p6 we know that it can't refer to
  // a derived class object constructed in the same location.
  if (const auto *ME = dyn_cast<MemberExpr>(Base)) {
    const ValueDecl *VD = ME->getMemberDecl();
    return VD->getType()->isRecordType() ? DevirtualizedMethod : nullptr;
  }

  // Likewise for calls on an object accessed by a (non-reference) pointer to
  // member access.
  if (auto *BO = dyn_cast<BinaryOperator>(Base)) {
    if (BO->isPtrMemOp()) {
      auto *MPT = BO->getRHS()->getType()->castAs<MemberPointerType>();
      if (MPT->getPointeeType()->isRecordType())
        return DevirtualizedMethod;
    }
  }

  // We can't devirtualize the call.
  return nullptr;
}

bool CXXMethodDecl::isUsualDeallocationFunction(
    SmallVectorImpl<const FunctionDecl *> &PreventedBy) const {
  assert(PreventedBy.empty() && "PreventedBy is expected to be empty");
  if (getOverloadedOperator() != OO_Delete &&
      getOverloadedOperator() != OO_Array_Delete)
    return false;

  // C++ [basic.stc.dynamic.deallocation]p2:
  //   A template instance is never a usual deallocation function,
  //   regardless of its signature.
  if (getPrimaryTemplate())
    return false;

  // C++ [basic.stc.dynamic.deallocation]p2:
  //   If a class T has a member deallocation function named operator delete
  //   with exactly one parameter, then that function is a usual (non-placement)
  //   deallocation function. [...]
  if (getNumParams() == 1)
    return true;
  unsigned UsualParams = 1;

  // C++ P0722:
  //   A destroying operator delete is a usual deallocation function if
  //   removing the std::destroying_delete_t parameter and changing the
  //   first parameter type from T* to void* results in the signature of
  //   a usual deallocation function.
  if (isDestroyingOperatorDelete())
    ++UsualParams;

  // C++ <=14 [basic.stc.dynamic.deallocation]p2:
  //   [...] If class T does not declare such an operator delete but does
  //   declare a member deallocation function named operator delete with
  //   exactly two parameters, the second of which has type std::size_t (18.1),
  //   then this function is a usual deallocation function.
  //
  // C++17 says a usual deallocation function is one with the signature
  //   (void* [, size_t] [, std::align_val_t] [, ...])
  // and all such functions are usual deallocation functions. It's not clear
  // that allowing varargs functions was intentional.
  ASTContext &Context = getASTContext();
  if (UsualParams < getNumParams() &&
      Context.hasSameUnqualifiedType(getParamDecl(UsualParams)->getType(),
                                     Context.getSizeType()))
    ++UsualParams;

  if (UsualParams < getNumParams() &&
      getParamDecl(UsualParams)->getType()->isAlignValT())
    ++UsualParams;

  if (UsualParams != getNumParams())
    return false;

  // In C++17 onwards, all potential usual deallocation functions are actual
  // usual deallocation functions.
  if (Context.getLangOpts().AlignedAllocation)
    return true;

  // This function is a usual deallocation function if there are no
  // single-parameter deallocation functions of the same kind.
  DeclContext::lookup_result R = getDeclContext()->lookup(getDeclName());
  bool Result = true;
  for (const auto *D : R) {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getNumParams() == 1) {
        PreventedBy.push_back(FD);
        Result = false;
      }
    }
  }
  return Result;
}

bool CXXMethodDecl::isCopyAssignmentOperator() const {
  // C++0x [class.copy]p17:
  //  A user-declared copy assignment operator X::operator= is a non-static
  //  non-template member function of class X with exactly one parameter of
  //  type X, X&, const X&, volatile X& or const volatile X&.
  if (/*operator=*/getOverloadedOperator() != OO_Equal ||
      /*non-static*/ isStatic() ||
      /*non-template*/getPrimaryTemplate() || getDescribedFunctionTemplate() ||
      getNumParams() != 1)
    return false;

  QualType ParamType = getParamDecl(0)->getType();
  if (const auto *Ref = ParamType->getAs<LValueReferenceType>())
    ParamType = Ref->getPointeeType();

  ASTContext &Context = getASTContext();
  QualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(getParent()));
  return Context.hasSameUnqualifiedType(ClassType, ParamType);
}

bool CXXMethodDecl::isMoveAssignmentOperator() const {
  // C++0x [class.copy]p19:
  //  A user-declared move assignment operator X::operator= is a non-static
  //  non-template member function of class X with exactly one parameter of type
  //  X&&, const X&&, volatile X&&, or const volatile X&&.
  if (getOverloadedOperator() != OO_Equal || isStatic() ||
      getPrimaryTemplate() || getDescribedFunctionTemplate() ||
      getNumParams() != 1)
    return false;

  QualType ParamType = getParamDecl(0)->getType();
  if (!isa<RValueReferenceType>(ParamType))
    return false;
  ParamType = ParamType->getPointeeType();

  ASTContext &Context = getASTContext();
  QualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(getParent()));
  return Context.hasSameUnqualifiedType(ClassType, ParamType);
}

void CXXMethodDecl::addOverriddenMethod(const CXXMethodDecl *MD) {
  assert(MD->isCanonicalDecl() && "Method is not canonical!");
  assert(!MD->getParent()->isDependentContext() &&
         "Can't add an overridden method to a class template!");
  assert(MD->isVirtual() && "Method is not virtual!");

  getASTContext().addOverriddenMethod(this, MD);
}

CXXMethodDecl::method_iterator CXXMethodDecl::begin_overridden_methods() const {
  if (isa<CXXConstructorDecl>(this)) return nullptr;
  return getASTContext().overridden_methods_begin(this);
}

CXXMethodDecl::method_iterator CXXMethodDecl::end_overridden_methods() const {
  if (isa<CXXConstructorDecl>(this)) return nullptr;
  return getASTContext().overridden_methods_end(this);
}

unsigned CXXMethodDecl::size_overridden_methods() const {
  if (isa<CXXConstructorDecl>(this)) return 0;
  return getASTContext().overridden_methods_size(this);
}

CXXMethodDecl::overridden_method_range
CXXMethodDecl::overridden_methods() const {
  if (isa<CXXConstructorDecl>(this))
    return overridden_method_range(nullptr, nullptr);
  return getASTContext().overridden_methods(this);
}

QualType CXXMethodDecl::getThisType(const FunctionProtoType *FPT,
                                    const CXXRecordDecl *Decl) {
  ASTContext &C = Decl->getASTContext();
  QualType ClassTy = C.getTypeDeclType(Decl);
  ClassTy = C.getQualifiedType(ClassTy, FPT->getTypeQuals());
  return C.getPointerType(ClassTy);
}

QualType CXXMethodDecl::getThisType() const {
  // C++ 9.3.2p1: The type of this in a member function of a class X is X*.
  // If the member function is declared const, the type of this is const X*,
  // if the member function is declared volatile, the type of this is
  // volatile X*, and if the member function is declared const volatile,
  // the type of this is const volatile X*.
  assert(isInstance() && "No 'this' for static methods!");

  return CXXMethodDecl::getThisType(getType()->getAs<FunctionProtoType>(),
                                    getParent());
}

bool CXXMethodDecl::hasInlineBody() const {
  // If this function is a template instantiation, look at the template from
  // which it was instantiated.
  const FunctionDecl *CheckFn = getTemplateInstantiationPattern();
  if (!CheckFn)
    CheckFn = this;

  const FunctionDecl *fn;
  return CheckFn->isDefined(fn) && !fn->isOutOfLine() &&
         (fn->doesThisDeclarationHaveABody() || fn->willHaveBody());
}

bool CXXMethodDecl::isLambdaStaticInvoker() const {
  const CXXRecordDecl *P = getParent();
  if (P->isLambda()) {
    if (const CXXMethodDecl *StaticInvoker = P->getLambdaStaticInvoker()) {
      if (StaticInvoker == this) return true;
      if (P->isGenericLambda() && this->isFunctionTemplateSpecialization())
        return StaticInvoker == this->getPrimaryTemplate()->getTemplatedDecl();
    }
  }
  return false;
}

CXXCtorInitializer::CXXCtorInitializer(ASTContext &Context,
                                       TypeSourceInfo *TInfo, bool IsVirtual,
                                       SourceLocation L, Expr *Init,
                                       SourceLocation R,
                                       SourceLocation EllipsisLoc)
    : Initializee(TInfo), MemberOrEllipsisLocation(EllipsisLoc), Init(Init),
      LParenLoc(L), RParenLoc(R), IsDelegating(false), IsVirtual(IsVirtual),
      IsWritten(false), SourceOrder(0) {}

CXXCtorInitializer::CXXCtorInitializer(ASTContext &Context,
                                       FieldDecl *Member,
                                       SourceLocation MemberLoc,
                                       SourceLocation L, Expr *Init,
                                       SourceLocation R)
    : Initializee(Member), MemberOrEllipsisLocation(MemberLoc), Init(Init),
      LParenLoc(L), RParenLoc(R), IsDelegating(false), IsVirtual(false),
      IsWritten(false), SourceOrder(0) {}

CXXCtorInitializer::CXXCtorInitializer(ASTContext &Context,
                                       IndirectFieldDecl *Member,
                                       SourceLocation MemberLoc,
                                       SourceLocation L, Expr *Init,
                                       SourceLocation R)
    : Initializee(Member), MemberOrEllipsisLocation(MemberLoc), Init(Init),
      LParenLoc(L), RParenLoc(R), IsDelegating(false), IsVirtual(false),
      IsWritten(false), SourceOrder(0) {}

CXXCtorInitializer::CXXCtorInitializer(ASTContext &Context,
                                       TypeSourceInfo *TInfo,
                                       SourceLocation L, Expr *Init,
                                       SourceLocation R)
    : Initializee(TInfo), Init(Init), LParenLoc(L), RParenLoc(R),
      IsDelegating(true), IsVirtual(false), IsWritten(false), SourceOrder(0) {}

int64_t CXXCtorInitializer::getID(const ASTContext &Context) const {
  return Context.getAllocator()
                .identifyKnownAlignedObject<CXXCtorInitializer>(this);
}

TypeLoc CXXCtorInitializer::getBaseClassLoc() const {
  if (isBaseInitializer())
    return Initializee.get<TypeSourceInfo*>()->getTypeLoc();
  else
    return {};
}

const Type *CXXCtorInitializer::getBaseClass() const {
  if (isBaseInitializer())
    return Initializee.get<TypeSourceInfo*>()->getType().getTypePtr();
  else
    return nullptr;
}

SourceLocation CXXCtorInitializer::getSourceLocation() const {
  if (isInClassMemberInitializer())
    return getAnyMember()->getLocation();

  if (isAnyMemberInitializer())
    return getMemberLocation();

  if (const auto *TSInfo = Initializee.get<TypeSourceInfo *>())
    return TSInfo->getTypeLoc().getLocalSourceRange().getBegin();

  return {};
}

SourceRange CXXCtorInitializer::getSourceRange() const {
  if (isInClassMemberInitializer()) {
    FieldDecl *D = getAnyMember();
    if (Expr *I = D->getInClassInitializer())
      return I->getSourceRange();
    return {};
  }

  return SourceRange(getSourceLocation(), getRParenLoc());
}

CXXConstructorDecl::CXXConstructorDecl(
    ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    bool isExplicitSpecified, bool isInline, bool isImplicitlyDeclared,
    bool isConstexpr, InheritedConstructor Inherited)
    : CXXMethodDecl(CXXConstructor, C, RD, StartLoc, NameInfo, T, TInfo,
                    SC_None, isInline, isConstexpr, SourceLocation()) {
  setNumCtorInitializers(0);
  setInheritingConstructor(static_cast<bool>(Inherited));
  setImplicit(isImplicitlyDeclared);
  if (Inherited)
    *getTrailingObjects<InheritedConstructor>() = Inherited;
  setExplicitSpecified(isExplicitSpecified);
}

void CXXConstructorDecl::anchor() {}

CXXConstructorDecl *CXXConstructorDecl::CreateDeserialized(ASTContext &C,
                                                           unsigned ID,
                                                           bool Inherited) {
  unsigned Extra = additionalSizeToAlloc<InheritedConstructor>(Inherited);
  auto *Result = new (C, ID, Extra) CXXConstructorDecl(
      C, nullptr, SourceLocation(), DeclarationNameInfo(), QualType(), nullptr,
      false, false, false, false, InheritedConstructor());
  Result->setInheritingConstructor(Inherited);
  return Result;
}

CXXConstructorDecl *
CXXConstructorDecl::Create(ASTContext &C, CXXRecordDecl *RD,
                           SourceLocation StartLoc,
                           const DeclarationNameInfo &NameInfo,
                           QualType T, TypeSourceInfo *TInfo,
                           bool isExplicit, bool isInline,
                           bool isImplicitlyDeclared, bool isConstexpr,
                           InheritedConstructor Inherited) {
  assert(NameInfo.getName().getNameKind()
         == DeclarationName::CXXConstructorName &&
         "Name must refer to a constructor");
  unsigned Extra =
      additionalSizeToAlloc<InheritedConstructor>(Inherited ? 1 : 0);
  return new (C, RD, Extra) CXXConstructorDecl(
      C, RD, StartLoc, NameInfo, T, TInfo, isExplicit, isInline,
      isImplicitlyDeclared, isConstexpr, Inherited);
}

CXXConstructorDecl::init_const_iterator CXXConstructorDecl::init_begin() const {
  return CtorInitializers.get(getASTContext().getExternalSource());
}

CXXConstructorDecl *CXXConstructorDecl::getTargetConstructor() const {
  assert(isDelegatingConstructor() && "Not a delegating constructor!");
  Expr *E = (*init_begin())->getInit()->IgnoreImplicit();
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E))
    return Construct->getConstructor();

  return nullptr;
}

bool CXXConstructorDecl::isDefaultConstructor() const {
  // C++ [class.ctor]p5:
  //   A default constructor for a class X is a constructor of class
  //   X that can be called without an argument.
  return (getNumParams() == 0) ||
         (getNumParams() > 0 && getParamDecl(0)->hasDefaultArg());
}

bool
CXXConstructorDecl::isCopyConstructor(unsigned &TypeQuals) const {
  return isCopyOrMoveConstructor(TypeQuals) &&
         getParamDecl(0)->getType()->isLValueReferenceType();
}

bool CXXConstructorDecl::isMoveConstructor(unsigned &TypeQuals) const {
  return isCopyOrMoveConstructor(TypeQuals) &&
    getParamDecl(0)->getType()->isRValueReferenceType();
}

/// Determine whether this is a copy or move constructor.
bool CXXConstructorDecl::isCopyOrMoveConstructor(unsigned &TypeQuals) const {
  // C++ [class.copy]p2:
  //   A non-template constructor for class X is a copy constructor
  //   if its first parameter is of type X&, const X&, volatile X& or
  //   const volatile X&, and either there are no other parameters
  //   or else all other parameters have default arguments (8.3.6).
  // C++0x [class.copy]p3:
  //   A non-template constructor for class X is a move constructor if its
  //   first parameter is of type X&&, const X&&, volatile X&&, or
  //   const volatile X&&, and either there are no other parameters or else
  //   all other parameters have default arguments.
  if ((getNumParams() < 1) ||
      (getNumParams() > 1 && !getParamDecl(1)->hasDefaultArg()) ||
      (getPrimaryTemplate() != nullptr) ||
      (getDescribedFunctionTemplate() != nullptr))
    return false;

  const ParmVarDecl *Param = getParamDecl(0);

  // Do we have a reference type?
  const auto *ParamRefType = Param->getType()->getAs<ReferenceType>();
  if (!ParamRefType)
    return false;

  // Is it a reference to our class type?
  ASTContext &Context = getASTContext();

  CanQualType PointeeType
    = Context.getCanonicalType(ParamRefType->getPointeeType());
  CanQualType ClassTy
    = Context.getCanonicalType(Context.getTagDeclType(getParent()));
  if (PointeeType.getUnqualifiedType() != ClassTy)
    return false;

  // FIXME: other qualifiers?

  // We have a copy or move constructor.
  TypeQuals = PointeeType.getCVRQualifiers();
  return true;
}

bool CXXConstructorDecl::isConvertingConstructor(bool AllowExplicit) const {
  // C++ [class.conv.ctor]p1:
  //   A constructor declared without the function-specifier explicit
  //   that can be called with a single parameter specifies a
  //   conversion from the type of its first parameter to the type of
  //   its class. Such a constructor is called a converting
  //   constructor.
  if (isExplicit() && !AllowExplicit)
    return false;

  return (getNumParams() == 0 &&
          getType()->getAs<FunctionProtoType>()->isVariadic()) ||
         (getNumParams() == 1) ||
         (getNumParams() > 1 &&
          (getParamDecl(1)->hasDefaultArg() ||
           getParamDecl(1)->isParameterPack()));
}

bool CXXConstructorDecl::isSpecializationCopyingObject() const {
  if ((getNumParams() < 1) ||
      (getNumParams() > 1 && !getParamDecl(1)->hasDefaultArg()) ||
      (getDescribedFunctionTemplate() != nullptr))
    return false;

  const ParmVarDecl *Param = getParamDecl(0);

  ASTContext &Context = getASTContext();
  CanQualType ParamType = Context.getCanonicalType(Param->getType());

  // Is it the same as our class type?
  CanQualType ClassTy
    = Context.getCanonicalType(Context.getTagDeclType(getParent()));
  if (ParamType.getUnqualifiedType() != ClassTy)
    return false;

  return true;
}

void CXXDestructorDecl::anchor() {}

CXXDestructorDecl *
CXXDestructorDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID)
      CXXDestructorDecl(C, nullptr, SourceLocation(), DeclarationNameInfo(),
                        QualType(), nullptr, false, false);
}

CXXDestructorDecl *
CXXDestructorDecl::Create(ASTContext &C, CXXRecordDecl *RD,
                          SourceLocation StartLoc,
                          const DeclarationNameInfo &NameInfo,
                          QualType T, TypeSourceInfo *TInfo,
                          bool isInline, bool isImplicitlyDeclared) {
  assert(NameInfo.getName().getNameKind()
         == DeclarationName::CXXDestructorName &&
         "Name must refer to a destructor");
  return new (C, RD) CXXDestructorDecl(C, RD, StartLoc, NameInfo, T, TInfo,
                                       isInline, isImplicitlyDeclared);
}

void CXXDestructorDecl::setOperatorDelete(FunctionDecl *OD, Expr *ThisArg) {
  auto *First = cast<CXXDestructorDecl>(getFirstDecl());
  if (OD && !First->OperatorDelete) {
    First->OperatorDelete = OD;
    First->OperatorDeleteThisArg = ThisArg;
    if (auto *L = getASTMutationListener())
      L->ResolvedOperatorDelete(First, OD, ThisArg);
  }
}

void CXXConversionDecl::anchor() {}

CXXConversionDecl *
CXXConversionDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) CXXConversionDecl(C, nullptr, SourceLocation(),
                                       DeclarationNameInfo(), QualType(),
                                       nullptr, false, false, false,
                                       SourceLocation());
}

CXXConversionDecl *
CXXConversionDecl::Create(ASTContext &C, CXXRecordDecl *RD,
                          SourceLocation StartLoc,
                          const DeclarationNameInfo &NameInfo,
                          QualType T, TypeSourceInfo *TInfo,
                          bool isInline, bool isExplicit,
                          bool isConstexpr, SourceLocation EndLocation) {
  assert(NameInfo.getName().getNameKind()
         == DeclarationName::CXXConversionFunctionName &&
         "Name must refer to a conversion function");
  return new (C, RD) CXXConversionDecl(C, RD, StartLoc, NameInfo, T, TInfo,
                                       isInline, isExplicit, isConstexpr,
                                       EndLocation);
}

bool CXXConversionDecl::isLambdaToBlockPointerConversion() const {
  return isImplicit() && getParent()->isLambda() &&
         getConversionType()->isBlockPointerType();
}

LinkageSpecDecl::LinkageSpecDecl(DeclContext *DC, SourceLocation ExternLoc,
                                 SourceLocation LangLoc, LanguageIDs lang,
                                 bool HasBraces)
    : Decl(LinkageSpec, DC, LangLoc), DeclContext(LinkageSpec),
      ExternLoc(ExternLoc), RBraceLoc(SourceLocation()) {
  setLanguage(lang);
  LinkageSpecDeclBits.HasBraces = HasBraces;
}

void LinkageSpecDecl::anchor() {}

LinkageSpecDecl *LinkageSpecDecl::Create(ASTContext &C,
                                         DeclContext *DC,
                                         SourceLocation ExternLoc,
                                         SourceLocation LangLoc,
                                         LanguageIDs Lang,
                                         bool HasBraces) {
  return new (C, DC) LinkageSpecDecl(DC, ExternLoc, LangLoc, Lang, HasBraces);
}

LinkageSpecDecl *LinkageSpecDecl::CreateDeserialized(ASTContext &C,
                                                     unsigned ID) {
  return new (C, ID) LinkageSpecDecl(nullptr, SourceLocation(),
                                     SourceLocation(), lang_c, false);
}

void UsingDirectiveDecl::anchor() {}

UsingDirectiveDecl *UsingDirectiveDecl::Create(ASTContext &C, DeclContext *DC,
                                               SourceLocation L,
                                               SourceLocation NamespaceLoc,
                                           NestedNameSpecifierLoc QualifierLoc,
                                               SourceLocation IdentLoc,
                                               NamedDecl *Used,
                                               DeclContext *CommonAncestor) {
  if (auto *NS = dyn_cast_or_null<NamespaceDecl>(Used))
    Used = NS->getOriginalNamespace();
  return new (C, DC) UsingDirectiveDecl(DC, L, NamespaceLoc, QualifierLoc,
                                        IdentLoc, Used, CommonAncestor);
}

UsingDirectiveDecl *UsingDirectiveDecl::CreateDeserialized(ASTContext &C,
                                                           unsigned ID) {
  return new (C, ID) UsingDirectiveDecl(nullptr, SourceLocation(),
                                        SourceLocation(),
                                        NestedNameSpecifierLoc(),
                                        SourceLocation(), nullptr, nullptr);
}

NamespaceDecl *UsingDirectiveDecl::getNominatedNamespace() {
  if (auto *NA = dyn_cast_or_null<NamespaceAliasDecl>(NominatedNamespace))
    return NA->getNamespace();
  return cast_or_null<NamespaceDecl>(NominatedNamespace);
}

NamespaceDecl::NamespaceDecl(ASTContext &C, DeclContext *DC, bool Inline,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, NamespaceDecl *PrevDecl)
    : NamedDecl(Namespace, DC, IdLoc, Id), DeclContext(Namespace),
      redeclarable_base(C), LocStart(StartLoc),
      AnonOrFirstNamespaceAndInline(nullptr, Inline) {
  setPreviousDecl(PrevDecl);

  if (PrevDecl)
    AnonOrFirstNamespaceAndInline.setPointer(PrevDecl->getOriginalNamespace());
}

NamespaceDecl *NamespaceDecl::Create(ASTContext &C, DeclContext *DC,
                                     bool Inline, SourceLocation StartLoc,
                                     SourceLocation IdLoc, IdentifierInfo *Id,
                                     NamespaceDecl *PrevDecl) {
  return new (C, DC) NamespaceDecl(C, DC, Inline, StartLoc, IdLoc, Id,
                                   PrevDecl);
}

NamespaceDecl *NamespaceDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) NamespaceDecl(C, nullptr, false, SourceLocation(),
                                   SourceLocation(), nullptr, nullptr);
}

NamespaceDecl *NamespaceDecl::getOriginalNamespace() {
  if (isFirstDecl())
    return this;

  return AnonOrFirstNamespaceAndInline.getPointer();
}

const NamespaceDecl *NamespaceDecl::getOriginalNamespace() const {
  if (isFirstDecl())
    return this;

  return AnonOrFirstNamespaceAndInline.getPointer();
}

bool NamespaceDecl::isOriginalNamespace() const { return isFirstDecl(); }

NamespaceDecl *NamespaceDecl::getNextRedeclarationImpl() {
  return getNextRedeclaration();
}

NamespaceDecl *NamespaceDecl::getPreviousDeclImpl() {
  return getPreviousDecl();
}

NamespaceDecl *NamespaceDecl::getMostRecentDeclImpl() {
  return getMostRecentDecl();
}

void NamespaceAliasDecl::anchor() {}

NamespaceAliasDecl *NamespaceAliasDecl::getNextRedeclarationImpl() {
  return getNextRedeclaration();
}

NamespaceAliasDecl *NamespaceAliasDecl::getPreviousDeclImpl() {
  return getPreviousDecl();
}

NamespaceAliasDecl *NamespaceAliasDecl::getMostRecentDeclImpl() {
  return getMostRecentDecl();
}

NamespaceAliasDecl *NamespaceAliasDecl::Create(ASTContext &C, DeclContext *DC,
                                               SourceLocation UsingLoc,
                                               SourceLocation AliasLoc,
                                               IdentifierInfo *Alias,
                                           NestedNameSpecifierLoc QualifierLoc,
                                               SourceLocation IdentLoc,
                                               NamedDecl *Namespace) {
  // FIXME: Preserve the aliased namespace as written.
  if (auto *NS = dyn_cast_or_null<NamespaceDecl>(Namespace))
    Namespace = NS->getOriginalNamespace();
  return new (C, DC) NamespaceAliasDecl(C, DC, UsingLoc, AliasLoc, Alias,
                                        QualifierLoc, IdentLoc, Namespace);
}

NamespaceAliasDecl *
NamespaceAliasDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) NamespaceAliasDecl(C, nullptr, SourceLocation(),
                                        SourceLocation(), nullptr,
                                        NestedNameSpecifierLoc(),
                                        SourceLocation(), nullptr);
}

void UsingShadowDecl::anchor() {}

UsingShadowDecl::UsingShadowDecl(Kind K, ASTContext &C, DeclContext *DC,
                                 SourceLocation Loc, UsingDecl *Using,
                                 NamedDecl *Target)
    : NamedDecl(K, DC, Loc, Using ? Using->getDeclName() : DeclarationName()),
      redeclarable_base(C), UsingOrNextShadow(cast<NamedDecl>(Using)) {
  if (Target)
    setTargetDecl(Target);
  setImplicit();
}

UsingShadowDecl::UsingShadowDecl(Kind K, ASTContext &C, EmptyShell Empty)
    : NamedDecl(K, nullptr, SourceLocation(), DeclarationName()),
      redeclarable_base(C) {}

UsingShadowDecl *
UsingShadowDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) UsingShadowDecl(UsingShadow, C, EmptyShell());
}

UsingDecl *UsingShadowDecl::getUsingDecl() const {
  const UsingShadowDecl *Shadow = this;
  while (const auto *NextShadow =
             dyn_cast<UsingShadowDecl>(Shadow->UsingOrNextShadow))
    Shadow = NextShadow;
  return cast<UsingDecl>(Shadow->UsingOrNextShadow);
}

void ConstructorUsingShadowDecl::anchor() {}

ConstructorUsingShadowDecl *
ConstructorUsingShadowDecl::Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation Loc, UsingDecl *Using,
                                   NamedDecl *Target, bool IsVirtual) {
  return new (C, DC) ConstructorUsingShadowDecl(C, DC, Loc, Using, Target,
                                                IsVirtual);
}

ConstructorUsingShadowDecl *
ConstructorUsingShadowDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) ConstructorUsingShadowDecl(C, EmptyShell());
}

CXXRecordDecl *ConstructorUsingShadowDecl::getNominatedBaseClass() const {
  return getUsingDecl()->getQualifier()->getAsRecordDecl();
}

void UsingDecl::anchor() {}

void UsingDecl::addShadowDecl(UsingShadowDecl *S) {
  assert(std::find(shadow_begin(), shadow_end(), S) == shadow_end() &&
         "declaration already in set");
  assert(S->getUsingDecl() == this);

  if (FirstUsingShadow.getPointer())
    S->UsingOrNextShadow = FirstUsingShadow.getPointer();
  FirstUsingShadow.setPointer(S);
}

void UsingDecl::removeShadowDecl(UsingShadowDecl *S) {
  assert(std::find(shadow_begin(), shadow_end(), S) != shadow_end() &&
         "declaration not in set");
  assert(S->getUsingDecl() == this);

  // Remove S from the shadow decl chain. This is O(n) but hopefully rare.

  if (FirstUsingShadow.getPointer() == S) {
    FirstUsingShadow.setPointer(
      dyn_cast<UsingShadowDecl>(S->UsingOrNextShadow));
    S->UsingOrNextShadow = this;
    return;
  }

  UsingShadowDecl *Prev = FirstUsingShadow.getPointer();
  while (Prev->UsingOrNextShadow != S)
    Prev = cast<UsingShadowDecl>(Prev->UsingOrNextShadow);
  Prev->UsingOrNextShadow = S->UsingOrNextShadow;
  S->UsingOrNextShadow = this;
}

UsingDecl *UsingDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation UL,
                             NestedNameSpecifierLoc QualifierLoc,
                             const DeclarationNameInfo &NameInfo,
                             bool HasTypename) {
  return new (C, DC) UsingDecl(DC, UL, QualifierLoc, NameInfo, HasTypename);
}

UsingDecl *UsingDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) UsingDecl(nullptr, SourceLocation(),
                               NestedNameSpecifierLoc(), DeclarationNameInfo(),
                               false);
}

SourceRange UsingDecl::getSourceRange() const {
  SourceLocation Begin = isAccessDeclaration()
    ? getQualifierLoc().getBeginLoc() : UsingLocation;
  return SourceRange(Begin, getNameInfo().getEndLoc());
}

void UsingPackDecl::anchor() {}

UsingPackDecl *UsingPackDecl::Create(ASTContext &C, DeclContext *DC,
                                     NamedDecl *InstantiatedFrom,
                                     ArrayRef<NamedDecl *> UsingDecls) {
  size_t Extra = additionalSizeToAlloc<NamedDecl *>(UsingDecls.size());
  return new (C, DC, Extra) UsingPackDecl(DC, InstantiatedFrom, UsingDecls);
}

UsingPackDecl *UsingPackDecl::CreateDeserialized(ASTContext &C, unsigned ID,
                                                 unsigned NumExpansions) {
  size_t Extra = additionalSizeToAlloc<NamedDecl *>(NumExpansions);
  auto *Result = new (C, ID, Extra) UsingPackDecl(nullptr, nullptr, None);
  Result->NumExpansions = NumExpansions;
  auto *Trail = Result->getTrailingObjects<NamedDecl *>();
  for (unsigned I = 0; I != NumExpansions; ++I)
    new (Trail + I) NamedDecl*(nullptr);
  return Result;
}

void UnresolvedUsingValueDecl::anchor() {}

UnresolvedUsingValueDecl *
UnresolvedUsingValueDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation UsingLoc,
                                 NestedNameSpecifierLoc QualifierLoc,
                                 const DeclarationNameInfo &NameInfo,
                                 SourceLocation EllipsisLoc) {
  return new (C, DC) UnresolvedUsingValueDecl(DC, C.DependentTy, UsingLoc,
                                              QualifierLoc, NameInfo,
                                              EllipsisLoc);
}

UnresolvedUsingValueDecl *
UnresolvedUsingValueDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) UnresolvedUsingValueDecl(nullptr, QualType(),
                                              SourceLocation(),
                                              NestedNameSpecifierLoc(),
                                              DeclarationNameInfo(),
                                              SourceLocation());
}

SourceRange UnresolvedUsingValueDecl::getSourceRange() const {
  SourceLocation Begin = isAccessDeclaration()
    ? getQualifierLoc().getBeginLoc() : UsingLocation;
  return SourceRange(Begin, getNameInfo().getEndLoc());
}

void UnresolvedUsingTypenameDecl::anchor() {}

UnresolvedUsingTypenameDecl *
UnresolvedUsingTypenameDecl::Create(ASTContext &C, DeclContext *DC,
                                    SourceLocation UsingLoc,
                                    SourceLocation TypenameLoc,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    SourceLocation TargetNameLoc,
                                    DeclarationName TargetName,
                                    SourceLocation EllipsisLoc) {
  return new (C, DC) UnresolvedUsingTypenameDecl(
      DC, UsingLoc, TypenameLoc, QualifierLoc, TargetNameLoc,
      TargetName.getAsIdentifierInfo(), EllipsisLoc);
}

UnresolvedUsingTypenameDecl *
UnresolvedUsingTypenameDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) UnresolvedUsingTypenameDecl(
      nullptr, SourceLocation(), SourceLocation(), NestedNameSpecifierLoc(),
      SourceLocation(), nullptr, SourceLocation());
}

void StaticAssertDecl::anchor() {}

StaticAssertDecl *StaticAssertDecl::Create(ASTContext &C, DeclContext *DC,
                                           SourceLocation StaticAssertLoc,
                                           Expr *AssertExpr,
                                           StringLiteral *Message,
                                           SourceLocation RParenLoc,
                                           bool Failed) {
  return new (C, DC) StaticAssertDecl(DC, StaticAssertLoc, AssertExpr, Message,
                                      RParenLoc, Failed);
}

StaticAssertDecl *StaticAssertDecl::CreateDeserialized(ASTContext &C,
                                                       unsigned ID) {
  return new (C, ID) StaticAssertDecl(nullptr, SourceLocation(), nullptr,
                                      nullptr, SourceLocation(), false);
}

void BindingDecl::anchor() {}

BindingDecl *BindingDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation IdLoc, IdentifierInfo *Id) {
  return new (C, DC) BindingDecl(DC, IdLoc, Id);
}

BindingDecl *BindingDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) BindingDecl(nullptr, SourceLocation(), nullptr);
}

VarDecl *BindingDecl::getHoldingVar() const {
  Expr *B = getBinding();
  if (!B)
    return nullptr;
  auto *DRE = dyn_cast<DeclRefExpr>(B->IgnoreImplicit());
  if (!DRE)
    return nullptr;

  auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  assert(VD->isImplicit() && "holding var for binding decl not implicit");
  return VD;
}

void DecompositionDecl::anchor() {}

DecompositionDecl *DecompositionDecl::Create(ASTContext &C, DeclContext *DC,
                                             SourceLocation StartLoc,
                                             SourceLocation LSquareLoc,
                                             QualType T, TypeSourceInfo *TInfo,
                                             StorageClass SC,
                                             ArrayRef<BindingDecl *> Bindings) {
  size_t Extra = additionalSizeToAlloc<BindingDecl *>(Bindings.size());
  return new (C, DC, Extra)
      DecompositionDecl(C, DC, StartLoc, LSquareLoc, T, TInfo, SC, Bindings);
}

DecompositionDecl *DecompositionDecl::CreateDeserialized(ASTContext &C,
                                                         unsigned ID,
                                                         unsigned NumBindings) {
  size_t Extra = additionalSizeToAlloc<BindingDecl *>(NumBindings);
  auto *Result = new (C, ID, Extra)
      DecompositionDecl(C, nullptr, SourceLocation(), SourceLocation(),
                        QualType(), nullptr, StorageClass(), None);
  // Set up and clean out the bindings array.
  Result->NumBindings = NumBindings;
  auto *Trail = Result->getTrailingObjects<BindingDecl *>();
  for (unsigned I = 0; I != NumBindings; ++I)
    new (Trail + I) BindingDecl*(nullptr);
  return Result;
}

void DecompositionDecl::printName(llvm::raw_ostream &os) const {
  os << '[';
  bool Comma = false;
  for (const auto *B : bindings()) {
    if (Comma)
      os << ", ";
    B->printName(os);
    Comma = true;
  }
  os << ']';
}

void MSPropertyDecl::anchor() {}

MSPropertyDecl *MSPropertyDecl::Create(ASTContext &C, DeclContext *DC,
                                       SourceLocation L, DeclarationName N,
                                       QualType T, TypeSourceInfo *TInfo,
                                       SourceLocation StartL,
                                       IdentifierInfo *Getter,
                                       IdentifierInfo *Setter) {
  return new (C, DC) MSPropertyDecl(DC, L, N, T, TInfo, StartL, Getter, Setter);
}

MSPropertyDecl *MSPropertyDecl::CreateDeserialized(ASTContext &C,
                                                   unsigned ID) {
  return new (C, ID) MSPropertyDecl(nullptr, SourceLocation(),
                                    DeclarationName(), QualType(), nullptr,
                                    SourceLocation(), nullptr, nullptr);
}

static const char *getAccessName(AccessSpecifier AS) {
  switch (AS) {
    case AS_none:
      llvm_unreachable("Invalid access specifier!");
    case AS_public:
      return "public";
    case AS_private:
      return "private";
    case AS_protected:
      return "protected";
  }
  llvm_unreachable("Invalid access specifier!");
}

const DiagnosticBuilder &clang::operator<<(const DiagnosticBuilder &DB,
                                           AccessSpecifier AS) {
  return DB << getAccessName(AS);
}

const PartialDiagnostic &clang::operator<<(const PartialDiagnostic &DB,
                                           AccessSpecifier AS) {
  return DB << getAccessName(AS);
}
