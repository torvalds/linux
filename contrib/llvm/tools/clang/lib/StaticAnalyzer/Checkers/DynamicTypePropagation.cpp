//===- DynamicTypePropagation.cpp ------------------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains two checkers. One helps the static analyzer core to track
// types, the other does type inference on Obj-C generics and report type
// errors.
//
// Dynamic Type Propagation:
// This checker defines the rules for dynamic type gathering and propagation.
//
// Generics Checker for Objective-C:
// This checker tries to find type errors that the compiler is not able to catch
// due to the implicit conversions that were introduced for backward
// compatibility.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeMap.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

// ProgramState trait - The type inflation is tracked by DynamicTypeMap. This is
// an auxiliary map that tracks more information about generic types, because in
// some cases the most derived type is not the most informative one about the
// type parameters. This types that are stored for each symbol in this map must
// be specialized.
// TODO: In some case the type stored in this map is exactly the same that is
// stored in DynamicTypeMap. We should no store duplicated information in those
// cases.
REGISTER_MAP_WITH_PROGRAMSTATE(MostSpecializedTypeArgsMap, SymbolRef,
                               const ObjCObjectPointerType *)

namespace {
class DynamicTypePropagation:
    public Checker< check::PreCall,
                    check::PostCall,
                    check::DeadSymbols,
                    check::PostStmt<CastExpr>,
                    check::PostStmt<CXXNewExpr>,
                    check::PreObjCMessage,
                    check::PostObjCMessage > {
  const ObjCObjectType *getObjectTypeForAllocAndNew(const ObjCMessageExpr *MsgE,
                                                    CheckerContext &C) const;

  /// Return a better dynamic type if one can be derived from the cast.
  const ObjCObjectPointerType *getBetterObjCType(const Expr *CastE,
                                                 CheckerContext &C) const;

  ExplodedNode *dynamicTypePropagationOnCasts(const CastExpr *CE,
                                              ProgramStateRef &State,
                                              CheckerContext &C) const;

  mutable std::unique_ptr<BugType> ObjCGenericsBugType;
  void initBugType() const {
    if (!ObjCGenericsBugType)
      ObjCGenericsBugType.reset(
          new BugType(this, "Generics", categories::CoreFoundationObjectiveC));
  }

  class GenericsBugVisitor : public BugReporterVisitor {
  public:
    GenericsBugVisitor(SymbolRef S) : Sym(S) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Sym);
    }

    std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;

  private:
    // The tracked symbol.
    SymbolRef Sym;
  };

  void reportGenericsBug(const ObjCObjectPointerType *From,
                         const ObjCObjectPointerType *To, ExplodedNode *N,
                         SymbolRef Sym, CheckerContext &C,
                         const Stmt *ReportedNode = nullptr) const;

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostStmt(const CastExpr *CastE, CheckerContext &C) const;
  void checkPostStmt(const CXXNewExpr *NewE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
  void checkPreObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;

  /// This value is set to true, when the Generics checker is turned on.
  DefaultBool CheckGenerics;
};
} // end anonymous namespace

void DynamicTypePropagation::checkDeadSymbols(SymbolReaper &SR,
                                              CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  DynamicTypeMapImpl TypeMap = State->get<DynamicTypeMap>();
  for (DynamicTypeMapImpl::iterator I = TypeMap.begin(), E = TypeMap.end();
       I != E; ++I) {
    if (!SR.isLiveRegion(I->first)) {
      State = State->remove<DynamicTypeMap>(I->first);
    }
  }

  MostSpecializedTypeArgsMapTy TyArgMap =
      State->get<MostSpecializedTypeArgsMap>();
  for (MostSpecializedTypeArgsMapTy::iterator I = TyArgMap.begin(),
                                              E = TyArgMap.end();
       I != E; ++I) {
    if (SR.isDead(I->first)) {
      State = State->remove<MostSpecializedTypeArgsMap>(I->first);
    }
  }

  C.addTransition(State);
}

static void recordFixedType(const MemRegion *Region, const CXXMethodDecl *MD,
                            CheckerContext &C) {
  assert(Region);
  assert(MD);

  ASTContext &Ctx = C.getASTContext();
  QualType Ty = Ctx.getPointerType(Ctx.getRecordType(MD->getParent()));

  ProgramStateRef State = C.getState();
  State = setDynamicTypeInfo(State, Region, Ty, /*CanBeSubclass=*/false);
  C.addTransition(State);
}

void DynamicTypePropagation::checkPreCall(const CallEvent &Call,
                                          CheckerContext &C) const {
  if (const CXXConstructorCall *Ctor = dyn_cast<CXXConstructorCall>(&Call)) {
    // C++11 [class.cdtor]p4: When a virtual function is called directly or
    //   indirectly from a constructor or from a destructor, including during
    //   the construction or destruction of the class's non-static data members,
    //   and the object to which the call applies is the object under
    //   construction or destruction, the function called is the final overrider
    //   in the constructor's or destructor's class and not one overriding it in
    //   a more-derived class.

    switch (Ctor->getOriginExpr()->getConstructionKind()) {
    case CXXConstructExpr::CK_Complete:
    case CXXConstructExpr::CK_Delegating:
      // No additional type info necessary.
      return;
    case CXXConstructExpr::CK_NonVirtualBase:
    case CXXConstructExpr::CK_VirtualBase:
      if (const MemRegion *Target = Ctor->getCXXThisVal().getAsRegion())
        recordFixedType(Target, Ctor->getDecl(), C);
      return;
    }

    return;
  }

  if (const CXXDestructorCall *Dtor = dyn_cast<CXXDestructorCall>(&Call)) {
    // C++11 [class.cdtor]p4 (see above)
    if (!Dtor->isBaseDestructor())
      return;

    const MemRegion *Target = Dtor->getCXXThisVal().getAsRegion();
    if (!Target)
      return;

    const Decl *D = Dtor->getDecl();
    if (!D)
      return;

    recordFixedType(Target, cast<CXXDestructorDecl>(D), C);
    return;
  }
}

void DynamicTypePropagation::checkPostCall(const CallEvent &Call,
                                           CheckerContext &C) const {
  // We can obtain perfect type info for return values from some calls.
  if (const ObjCMethodCall *Msg = dyn_cast<ObjCMethodCall>(&Call)) {

    // Get the returned value if it's a region.
    const MemRegion *RetReg = Call.getReturnValue().getAsRegion();
    if (!RetReg)
      return;

    ProgramStateRef State = C.getState();
    const ObjCMethodDecl *D = Msg->getDecl();

    if (D && D->hasRelatedResultType()) {
      switch (Msg->getMethodFamily()) {
      default:
        break;

      // We assume that the type of the object returned by alloc and new are the
      // pointer to the object of the class specified in the receiver of the
      // message.
      case OMF_alloc:
      case OMF_new: {
        // Get the type of object that will get created.
        const ObjCMessageExpr *MsgE = Msg->getOriginExpr();
        const ObjCObjectType *ObjTy = getObjectTypeForAllocAndNew(MsgE, C);
        if (!ObjTy)
          return;
        QualType DynResTy =
                 C.getASTContext().getObjCObjectPointerType(QualType(ObjTy, 0));
        C.addTransition(setDynamicTypeInfo(State, RetReg, DynResTy, false));
        break;
      }
      case OMF_init: {
        // Assume, the result of the init method has the same dynamic type as
        // the receiver and propagate the dynamic type info.
        const MemRegion *RecReg = Msg->getReceiverSVal().getAsRegion();
        if (!RecReg)
          return;
        DynamicTypeInfo RecDynType = getDynamicTypeInfo(State, RecReg);
        C.addTransition(setDynamicTypeInfo(State, RetReg, RecDynType));
        break;
      }
      }
    }
    return;
  }

  if (const CXXConstructorCall *Ctor = dyn_cast<CXXConstructorCall>(&Call)) {
    // We may need to undo the effects of our pre-call check.
    switch (Ctor->getOriginExpr()->getConstructionKind()) {
    case CXXConstructExpr::CK_Complete:
    case CXXConstructExpr::CK_Delegating:
      // No additional work necessary.
      // Note: This will leave behind the actual type of the object for
      // complete constructors, but arguably that's a good thing, since it
      // means the dynamic type info will be correct even for objects
      // constructed with operator new.
      return;
    case CXXConstructExpr::CK_NonVirtualBase:
    case CXXConstructExpr::CK_VirtualBase:
      if (const MemRegion *Target = Ctor->getCXXThisVal().getAsRegion()) {
        // We just finished a base constructor. Now we can use the subclass's
        // type when resolving virtual calls.
        const LocationContext *LCtx = C.getLocationContext();

        // FIXME: In C++17 classes with non-virtual bases may be treated as
        // aggregates, and in such case no top-frame constructor will be called.
        // Figure out if we need to do anything in this case.
        // FIXME: Instead of relying on the ParentMap, we should have the
        // trigger-statement (InitListExpr in this case) available in this
        // callback, ideally as part of CallEvent.
        if (dyn_cast_or_null<InitListExpr>(
                LCtx->getParentMap().getParent(Ctor->getOriginExpr())))
          return;

        recordFixedType(Target, cast<CXXConstructorDecl>(LCtx->getDecl()), C);
      }
      return;
    }
  }
}

/// TODO: Handle explicit casts.
///       Handle C++ casts.
///
/// Precondition: the cast is between ObjCObjectPointers.
ExplodedNode *DynamicTypePropagation::dynamicTypePropagationOnCasts(
    const CastExpr *CE, ProgramStateRef &State, CheckerContext &C) const {
  // We only track type info for regions.
  const MemRegion *ToR = C.getSVal(CE).getAsRegion();
  if (!ToR)
    return C.getPredecessor();

  if (isa<ExplicitCastExpr>(CE))
    return C.getPredecessor();

  if (const Type *NewTy = getBetterObjCType(CE, C)) {
    State = setDynamicTypeInfo(State, ToR, QualType(NewTy, 0));
    return C.addTransition(State);
  }
  return C.getPredecessor();
}

void DynamicTypePropagation::checkPostStmt(const CXXNewExpr *NewE,
                                           CheckerContext &C) const {
  if (NewE->isArray())
    return;

  // We only track dynamic type info for regions.
  const MemRegion *MR = C.getSVal(NewE).getAsRegion();
  if (!MR)
    return;

  C.addTransition(setDynamicTypeInfo(C.getState(), MR, NewE->getType(),
                                     /*CanBeSubclass=*/false));
}

const ObjCObjectType *
DynamicTypePropagation::getObjectTypeForAllocAndNew(const ObjCMessageExpr *MsgE,
                                                    CheckerContext &C) const {
  if (MsgE->getReceiverKind() == ObjCMessageExpr::Class) {
    if (const ObjCObjectType *ObjTy
          = MsgE->getClassReceiver()->getAs<ObjCObjectType>())
    return ObjTy;
  }

  if (MsgE->getReceiverKind() == ObjCMessageExpr::SuperClass) {
    if (const ObjCObjectType *ObjTy
          = MsgE->getSuperType()->getAs<ObjCObjectType>())
      return ObjTy;
  }

  const Expr *RecE = MsgE->getInstanceReceiver();
  if (!RecE)
    return nullptr;

  RecE= RecE->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(RecE)) {
    const StackFrameContext *SFCtx = C.getStackFrame();
    // Are we calling [self alloc]? If this is self, get the type of the
    // enclosing ObjC class.
    if (DRE->getDecl() == SFCtx->getSelfDecl()) {
      if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(SFCtx->getDecl()))
        if (const ObjCObjectType *ObjTy =
            dyn_cast<ObjCObjectType>(MD->getClassInterface()->getTypeForDecl()))
          return ObjTy;
    }
  }
  return nullptr;
}

// Return a better dynamic type if one can be derived from the cast.
// Compare the current dynamic type of the region and the new type to which we
// are casting. If the new type is lower in the inheritance hierarchy, pick it.
const ObjCObjectPointerType *
DynamicTypePropagation::getBetterObjCType(const Expr *CastE,
                                          CheckerContext &C) const {
  const MemRegion *ToR = C.getSVal(CastE).getAsRegion();
  assert(ToR);

  // Get the old and new types.
  const ObjCObjectPointerType *NewTy =
      CastE->getType()->getAs<ObjCObjectPointerType>();
  if (!NewTy)
    return nullptr;
  QualType OldDTy = getDynamicTypeInfo(C.getState(), ToR).getType();
  if (OldDTy.isNull()) {
    return NewTy;
  }
  const ObjCObjectPointerType *OldTy =
    OldDTy->getAs<ObjCObjectPointerType>();
  if (!OldTy)
    return nullptr;

  // Id the old type is 'id', the new one is more precise.
  if (OldTy->isObjCIdType() && !NewTy->isObjCIdType())
    return NewTy;

  // Return new if it's a subclass of old.
  const ObjCInterfaceDecl *ToI = NewTy->getInterfaceDecl();
  const ObjCInterfaceDecl *FromI = OldTy->getInterfaceDecl();
  if (ToI && FromI && FromI->isSuperClassOf(ToI))
    return NewTy;

  return nullptr;
}

static const ObjCObjectPointerType *getMostInformativeDerivedClassImpl(
    const ObjCObjectPointerType *From, const ObjCObjectPointerType *To,
    const ObjCObjectPointerType *MostInformativeCandidate, ASTContext &C) {
  // Checking if from and to are the same classes modulo specialization.
  if (From->getInterfaceDecl()->getCanonicalDecl() ==
      To->getInterfaceDecl()->getCanonicalDecl()) {
    if (To->isSpecialized()) {
      assert(MostInformativeCandidate->isSpecialized());
      return MostInformativeCandidate;
    }
    return From;
  }

  if (To->getObjectType()->getSuperClassType().isNull()) {
    // If To has no super class and From and To aren't the same then
    // To was not actually a descendent of From. In this case the best we can
    // do is 'From'.
    return From;
  }

  const auto *SuperOfTo =
      To->getObjectType()->getSuperClassType()->getAs<ObjCObjectType>();
  assert(SuperOfTo);
  QualType SuperPtrOfToQual =
      C.getObjCObjectPointerType(QualType(SuperOfTo, 0));
  const auto *SuperPtrOfTo = SuperPtrOfToQual->getAs<ObjCObjectPointerType>();
  if (To->isUnspecialized())
    return getMostInformativeDerivedClassImpl(From, SuperPtrOfTo, SuperPtrOfTo,
                                              C);
  else
    return getMostInformativeDerivedClassImpl(From, SuperPtrOfTo,
                                              MostInformativeCandidate, C);
}

/// A downcast may loose specialization information. E. g.:
///   MutableMap<T, U> : Map
/// The downcast to MutableMap looses the information about the types of the
/// Map (due to the type parameters are not being forwarded to Map), and in
/// general there is no way to recover that information from the
/// declaration. In order to have to most information, lets find the most
/// derived type that has all the type parameters forwarded.
///
/// Get the a subclass of \p From (which has a lower bound \p To) that do not
/// loose information about type parameters. \p To has to be a subclass of
/// \p From. From has to be specialized.
static const ObjCObjectPointerType *
getMostInformativeDerivedClass(const ObjCObjectPointerType *From,
                               const ObjCObjectPointerType *To, ASTContext &C) {
  return getMostInformativeDerivedClassImpl(From, To, To, C);
}

/// Inputs:
///   \param StaticLowerBound Static lower bound for a symbol. The dynamic lower
///   bound might be the subclass of this type.
///   \param StaticUpperBound A static upper bound for a symbol.
///   \p StaticLowerBound expected to be the subclass of \p StaticUpperBound.
///   \param Current The type that was inferred for a symbol in a previous
///   context. Might be null when this is the first time that inference happens.
/// Precondition:
///   \p StaticLowerBound or \p StaticUpperBound is specialized. If \p Current
///   is not null, it is specialized.
/// Possible cases:
///   (1) The \p Current is null and \p StaticLowerBound <: \p StaticUpperBound
///   (2) \p StaticLowerBound <: \p Current <: \p StaticUpperBound
///   (3) \p Current <: \p StaticLowerBound <: \p StaticUpperBound
///   (4) \p StaticLowerBound <: \p StaticUpperBound <: \p Current
/// Effect:
///   Use getMostInformativeDerivedClass with the upper and lower bound of the
///   set {\p StaticLowerBound, \p Current, \p StaticUpperBound}. The computed
///   lower bound must be specialized. If the result differs from \p Current or
///   \p Current is null, store the result.
static bool
storeWhenMoreInformative(ProgramStateRef &State, SymbolRef Sym,
                         const ObjCObjectPointerType *const *Current,
                         const ObjCObjectPointerType *StaticLowerBound,
                         const ObjCObjectPointerType *StaticUpperBound,
                         ASTContext &C) {
  // TODO: The above 4 cases are not exhaustive. In particular, it is possible
  // for Current to be incomparable with StaticLowerBound, StaticUpperBound,
  // or both.
  //
  // For example, suppose Foo<T> and Bar<T> are unrelated types.
  //
  //  Foo<T> *f = ...
  //  Bar<T> *b = ...
  //
  //  id t1 = b;
  //  f = t1;
  //  id t2 = f; // StaticLowerBound is Foo<T>, Current is Bar<T>
  //
  // We should either constrain the callers of this function so that the stated
  // preconditions hold (and assert it) or rewrite the function to expicitly
  // handle the additional cases.

  // Precondition
  assert(StaticUpperBound->isSpecialized() ||
         StaticLowerBound->isSpecialized());
  assert(!Current || (*Current)->isSpecialized());

  // Case (1)
  if (!Current) {
    if (StaticUpperBound->isUnspecialized()) {
      State = State->set<MostSpecializedTypeArgsMap>(Sym, StaticLowerBound);
      return true;
    }
    // Upper bound is specialized.
    const ObjCObjectPointerType *WithMostInfo =
        getMostInformativeDerivedClass(StaticUpperBound, StaticLowerBound, C);
    State = State->set<MostSpecializedTypeArgsMap>(Sym, WithMostInfo);
    return true;
  }

  // Case (3)
  if (C.canAssignObjCInterfaces(StaticLowerBound, *Current)) {
    return false;
  }

  // Case (4)
  if (C.canAssignObjCInterfaces(*Current, StaticUpperBound)) {
    // The type arguments might not be forwarded at any point of inheritance.
    const ObjCObjectPointerType *WithMostInfo =
        getMostInformativeDerivedClass(*Current, StaticUpperBound, C);
    WithMostInfo =
        getMostInformativeDerivedClass(WithMostInfo, StaticLowerBound, C);
    if (WithMostInfo == *Current)
      return false;
    State = State->set<MostSpecializedTypeArgsMap>(Sym, WithMostInfo);
    return true;
  }

  // Case (2)
  const ObjCObjectPointerType *WithMostInfo =
      getMostInformativeDerivedClass(*Current, StaticLowerBound, C);
  if (WithMostInfo != *Current) {
    State = State->set<MostSpecializedTypeArgsMap>(Sym, WithMostInfo);
    return true;
  }

  return false;
}

/// Type inference based on static type information that is available for the
/// cast and the tracked type information for the given symbol. When the tracked
/// symbol and the destination type of the cast are unrelated, report an error.
void DynamicTypePropagation::checkPostStmt(const CastExpr *CE,
                                           CheckerContext &C) const {
  if (CE->getCastKind() != CK_BitCast)
    return;

  QualType OriginType = CE->getSubExpr()->getType();
  QualType DestType = CE->getType();

  const auto *OrigObjectPtrType = OriginType->getAs<ObjCObjectPointerType>();
  const auto *DestObjectPtrType = DestType->getAs<ObjCObjectPointerType>();

  if (!OrigObjectPtrType || !DestObjectPtrType)
    return;

  ProgramStateRef State = C.getState();
  ExplodedNode *AfterTypeProp = dynamicTypePropagationOnCasts(CE, State, C);

  ASTContext &ASTCtxt = C.getASTContext();

  // This checker detects the subtyping relationships using the assignment
  // rules. In order to be able to do this the kindofness must be stripped
  // first. The checker treats every type as kindof type anyways: when the
  // tracked type is the subtype of the static type it tries to look up the
  // methods in the tracked type first.
  OrigObjectPtrType = OrigObjectPtrType->stripObjCKindOfTypeAndQuals(ASTCtxt);
  DestObjectPtrType = DestObjectPtrType->stripObjCKindOfTypeAndQuals(ASTCtxt);

  if (OrigObjectPtrType->isUnspecialized() &&
      DestObjectPtrType->isUnspecialized())
    return;

  SymbolRef Sym = C.getSVal(CE).getAsSymbol();
  if (!Sym)
    return;

  const ObjCObjectPointerType *const *TrackedType =
      State->get<MostSpecializedTypeArgsMap>(Sym);

  if (isa<ExplicitCastExpr>(CE)) {
    // Treat explicit casts as an indication from the programmer that the
    // Objective-C type system is not rich enough to express the needed
    // invariant. In such cases, forget any existing information inferred
    // about the type arguments. We don't assume the casted-to specialized
    // type here because the invariant the programmer specifies in the cast
    // may only hold at this particular program point and not later ones.
    // We don't want a suppressing cast to require a cascade of casts down the
    // line.
    if (TrackedType) {
      State = State->remove<MostSpecializedTypeArgsMap>(Sym);
      C.addTransition(State, AfterTypeProp);
    }
    return;
  }

  // Check which assignments are legal.
  bool OrigToDest =
      ASTCtxt.canAssignObjCInterfaces(DestObjectPtrType, OrigObjectPtrType);
  bool DestToOrig =
      ASTCtxt.canAssignObjCInterfaces(OrigObjectPtrType, DestObjectPtrType);

  // The tracked type should be the sub or super class of the static destination
  // type. When an (implicit) upcast or a downcast happens according to static
  // types, and there is no subtyping relationship between the tracked and the
  // static destination types, it indicates an error.
  if (TrackedType &&
      !ASTCtxt.canAssignObjCInterfaces(DestObjectPtrType, *TrackedType) &&
      !ASTCtxt.canAssignObjCInterfaces(*TrackedType, DestObjectPtrType)) {
    static CheckerProgramPointTag IllegalConv(this, "IllegalConversion");
    ExplodedNode *N = C.addTransition(State, AfterTypeProp, &IllegalConv);
    reportGenericsBug(*TrackedType, DestObjectPtrType, N, Sym, C);
    return;
  }

  // Handle downcasts and upcasts.

  const ObjCObjectPointerType *LowerBound = DestObjectPtrType;
  const ObjCObjectPointerType *UpperBound = OrigObjectPtrType;
  if (OrigToDest && !DestToOrig)
    std::swap(LowerBound, UpperBound);

  // The id type is not a real bound. Eliminate it.
  LowerBound = LowerBound->isObjCIdType() ? UpperBound : LowerBound;
  UpperBound = UpperBound->isObjCIdType() ? LowerBound : UpperBound;

  if (storeWhenMoreInformative(State, Sym, TrackedType, LowerBound, UpperBound,
                               ASTCtxt)) {
    C.addTransition(State, AfterTypeProp);
  }
}

static const Expr *stripCastsAndSugar(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E))
    E = POE->getSyntacticForm()->IgnoreParenImpCasts();
  if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E))
    E = OVE->getSourceExpr()->IgnoreParenImpCasts();
  return E;
}

static bool isObjCTypeParamDependent(QualType Type) {
  // It is illegal to typedef parameterized types inside an interface. Therefore
  // an Objective-C type can only be dependent on a type parameter when the type
  // parameter structurally present in the type itself.
  class IsObjCTypeParamDependentTypeVisitor
      : public RecursiveASTVisitor<IsObjCTypeParamDependentTypeVisitor> {
  public:
    IsObjCTypeParamDependentTypeVisitor() : Result(false) {}
    bool VisitObjCTypeParamType(const ObjCTypeParamType *Type) {
      if (isa<ObjCTypeParamDecl>(Type->getDecl())) {
        Result = true;
        return false;
      }
      return true;
    }

    bool Result;
  };

  IsObjCTypeParamDependentTypeVisitor Visitor;
  Visitor.TraverseType(Type);
  return Visitor.Result;
}

/// A method might not be available in the interface indicated by the static
/// type. However it might be available in the tracked type. In order to
/// properly substitute the type parameters we need the declaration context of
/// the method. The more specialized the enclosing class of the method is, the
/// more likely that the parameter substitution will be successful.
static const ObjCMethodDecl *
findMethodDecl(const ObjCMessageExpr *MessageExpr,
               const ObjCObjectPointerType *TrackedType, ASTContext &ASTCtxt) {
  const ObjCMethodDecl *Method = nullptr;

  QualType ReceiverType = MessageExpr->getReceiverType();
  const auto *ReceiverObjectPtrType =
      ReceiverType->getAs<ObjCObjectPointerType>();

  // Do this "devirtualization" on instance and class methods only. Trust the
  // static type on super and super class calls.
  if (MessageExpr->getReceiverKind() == ObjCMessageExpr::Instance ||
      MessageExpr->getReceiverKind() == ObjCMessageExpr::Class) {
    // When the receiver type is id, Class, or some super class of the tracked
    // type, look up the method in the tracked type, not in the receiver type.
    // This way we preserve more information.
    if (ReceiverType->isObjCIdType() || ReceiverType->isObjCClassType() ||
        ASTCtxt.canAssignObjCInterfaces(ReceiverObjectPtrType, TrackedType)) {
      const ObjCInterfaceDecl *InterfaceDecl = TrackedType->getInterfaceDecl();
      // The method might not be found.
      Selector Sel = MessageExpr->getSelector();
      Method = InterfaceDecl->lookupInstanceMethod(Sel);
      if (!Method)
        Method = InterfaceDecl->lookupClassMethod(Sel);
    }
  }

  // Fallback to statick method lookup when the one based on the tracked type
  // failed.
  return Method ? Method : MessageExpr->getMethodDecl();
}

/// Get the returned ObjCObjectPointerType by a method based on the tracked type
/// information, or null pointer when the returned type is not an
/// ObjCObjectPointerType.
static QualType getReturnTypeForMethod(
    const ObjCMethodDecl *Method, ArrayRef<QualType> TypeArgs,
    const ObjCObjectPointerType *SelfType, ASTContext &C) {
  QualType StaticResultType = Method->getReturnType();

  // Is the return type declared as instance type?
  if (StaticResultType == C.getObjCInstanceType())
    return QualType(SelfType, 0);

  // Check whether the result type depends on a type parameter.
  if (!isObjCTypeParamDependent(StaticResultType))
    return QualType();

  QualType ResultType = StaticResultType.substObjCTypeArgs(
      C, TypeArgs, ObjCSubstitutionContext::Result);

  return ResultType;
}

/// When the receiver has a tracked type, use that type to validate the
/// argumments of the message expression and the return value.
void DynamicTypePropagation::checkPreObjCMessage(const ObjCMethodCall &M,
                                                 CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SymbolRef Sym = M.getReceiverSVal().getAsSymbol();
  if (!Sym)
    return;

  const ObjCObjectPointerType *const *TrackedType =
      State->get<MostSpecializedTypeArgsMap>(Sym);
  if (!TrackedType)
    return;

  // Get the type arguments from tracked type and substitute type arguments
  // before do the semantic check.

  ASTContext &ASTCtxt = C.getASTContext();
  const ObjCMessageExpr *MessageExpr = M.getOriginExpr();
  const ObjCMethodDecl *Method =
      findMethodDecl(MessageExpr, *TrackedType, ASTCtxt);

  // It is possible to call non-existent methods in Obj-C.
  if (!Method)
    return;

  // If the method is declared on a class that has a non-invariant
  // type parameter, don't warn about parameter mismatches after performing
  // substitution. This prevents warning when the programmer has purposely
  // casted the receiver to a super type or unspecialized type but the analyzer
  // has a more precise tracked type than the programmer intends at the call
  // site.
  //
  // For example, consider NSArray (which has a covariant type parameter)
  // and NSMutableArray (a subclass of NSArray where the type parameter is
  // invariant):
  // NSMutableArray *a = [[NSMutableArray<NSString *> alloc] init;
  //
  // [a containsObject:number]; // Safe: -containsObject is defined on NSArray.
  // NSArray<NSObject *> *other = [a arrayByAddingObject:number]  // Safe
  //
  // [a addObject:number] // Unsafe: -addObject: is defined on NSMutableArray
  //

  const ObjCInterfaceDecl *Interface = Method->getClassInterface();
  if (!Interface)
    return;

  ObjCTypeParamList *TypeParams = Interface->getTypeParamList();
  if (!TypeParams)
    return;

  for (ObjCTypeParamDecl *TypeParam : *TypeParams) {
    if (TypeParam->getVariance() != ObjCTypeParamVariance::Invariant)
      return;
  }

  Optional<ArrayRef<QualType>> TypeArgs =
      (*TrackedType)->getObjCSubstitutions(Method->getDeclContext());
  // This case might happen when there is an unspecialized override of a
  // specialized method.
  if (!TypeArgs)
    return;

  for (unsigned i = 0; i < Method->param_size(); i++) {
    const Expr *Arg = MessageExpr->getArg(i);
    const ParmVarDecl *Param = Method->parameters()[i];

    QualType OrigParamType = Param->getType();
    if (!isObjCTypeParamDependent(OrigParamType))
      continue;

    QualType ParamType = OrigParamType.substObjCTypeArgs(
        ASTCtxt, *TypeArgs, ObjCSubstitutionContext::Parameter);
    // Check if it can be assigned
    const auto *ParamObjectPtrType = ParamType->getAs<ObjCObjectPointerType>();
    const auto *ArgObjectPtrType =
        stripCastsAndSugar(Arg)->getType()->getAs<ObjCObjectPointerType>();
    if (!ParamObjectPtrType || !ArgObjectPtrType)
      continue;

    // Check if we have more concrete tracked type that is not a super type of
    // the static argument type.
    SVal ArgSVal = M.getArgSVal(i);
    SymbolRef ArgSym = ArgSVal.getAsSymbol();
    if (ArgSym) {
      const ObjCObjectPointerType *const *TrackedArgType =
          State->get<MostSpecializedTypeArgsMap>(ArgSym);
      if (TrackedArgType &&
          ASTCtxt.canAssignObjCInterfaces(ArgObjectPtrType, *TrackedArgType)) {
        ArgObjectPtrType = *TrackedArgType;
      }
    }

    // Warn when argument is incompatible with the parameter.
    if (!ASTCtxt.canAssignObjCInterfaces(ParamObjectPtrType,
                                         ArgObjectPtrType)) {
      static CheckerProgramPointTag Tag(this, "ArgTypeMismatch");
      ExplodedNode *N = C.addTransition(State, &Tag);
      reportGenericsBug(ArgObjectPtrType, ParamObjectPtrType, N, Sym, C, Arg);
      return;
    }
  }
}

/// This callback is used to infer the types for Class variables. This info is
/// used later to validate messages that sent to classes. Class variables are
/// initialized with by invoking the 'class' method on a class.
/// This method is also used to infer the type information for the return
/// types.
// TODO: right now it only tracks generic types. Extend this to track every
// type in the DynamicTypeMap and diagnose type errors!
void DynamicTypePropagation::checkPostObjCMessage(const ObjCMethodCall &M,
                                                  CheckerContext &C) const {
  const ObjCMessageExpr *MessageExpr = M.getOriginExpr();

  SymbolRef RetSym = M.getReturnValue().getAsSymbol();
  if (!RetSym)
    return;

  Selector Sel = MessageExpr->getSelector();
  ProgramStateRef State = C.getState();
  // Inference for class variables.
  // We are only interested in cases where the class method is invoked on a
  // class. This method is provided by the runtime and available on all classes.
  if (MessageExpr->getReceiverKind() == ObjCMessageExpr::Class &&
      Sel.getAsString() == "class") {
    QualType ReceiverType = MessageExpr->getClassReceiver();
    const auto *ReceiverClassType = ReceiverType->getAs<ObjCObjectType>();
    QualType ReceiverClassPointerType =
        C.getASTContext().getObjCObjectPointerType(
            QualType(ReceiverClassType, 0));

    if (!ReceiverClassType->isSpecialized())
      return;
    const auto *InferredType =
        ReceiverClassPointerType->getAs<ObjCObjectPointerType>();
    assert(InferredType);

    State = State->set<MostSpecializedTypeArgsMap>(RetSym, InferredType);
    C.addTransition(State);
    return;
  }

  // Tracking for return types.
  SymbolRef RecSym = M.getReceiverSVal().getAsSymbol();
  if (!RecSym)
    return;

  const ObjCObjectPointerType *const *TrackedType =
      State->get<MostSpecializedTypeArgsMap>(RecSym);
  if (!TrackedType)
    return;

  ASTContext &ASTCtxt = C.getASTContext();
  const ObjCMethodDecl *Method =
      findMethodDecl(MessageExpr, *TrackedType, ASTCtxt);
  if (!Method)
    return;

  Optional<ArrayRef<QualType>> TypeArgs =
      (*TrackedType)->getObjCSubstitutions(Method->getDeclContext());
  if (!TypeArgs)
    return;

  QualType ResultType =
      getReturnTypeForMethod(Method, *TypeArgs, *TrackedType, ASTCtxt);
  // The static type is the same as the deduced type.
  if (ResultType.isNull())
    return;

  const MemRegion *RetRegion = M.getReturnValue().getAsRegion();
  ExplodedNode *Pred = C.getPredecessor();
  // When there is an entry available for the return symbol in DynamicTypeMap,
  // the call was inlined, and the information in the DynamicTypeMap is should
  // be precise.
  if (RetRegion && !State->get<DynamicTypeMap>(RetRegion)) {
    // TODO: we have duplicated information in DynamicTypeMap and
    // MostSpecializedTypeArgsMap. We should only store anything in the later if
    // the stored data differs from the one stored in the former.
    State = setDynamicTypeInfo(State, RetRegion, ResultType,
                               /*CanBeSubclass=*/true);
    Pred = C.addTransition(State);
  }

  const auto *ResultPtrType = ResultType->getAs<ObjCObjectPointerType>();

  if (!ResultPtrType || ResultPtrType->isUnspecialized())
    return;

  // When the result is a specialized type and it is not tracked yet, track it
  // for the result symbol.
  if (!State->get<MostSpecializedTypeArgsMap>(RetSym)) {
    State = State->set<MostSpecializedTypeArgsMap>(RetSym, ResultPtrType);
    C.addTransition(State, Pred);
  }
}

void DynamicTypePropagation::reportGenericsBug(
    const ObjCObjectPointerType *From, const ObjCObjectPointerType *To,
    ExplodedNode *N, SymbolRef Sym, CheckerContext &C,
    const Stmt *ReportedNode) const {
  if (!CheckGenerics)
    return;

  initBugType();
  SmallString<192> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << "Conversion from value of type '";
  QualType::print(From, Qualifiers(), OS, C.getLangOpts(), llvm::Twine());
  OS << "' to incompatible type '";
  QualType::print(To, Qualifiers(), OS, C.getLangOpts(), llvm::Twine());
  OS << "'";
  std::unique_ptr<BugReport> R(
      new BugReport(*ObjCGenericsBugType, OS.str(), N));
  R->markInteresting(Sym);
  R->addVisitor(llvm::make_unique<GenericsBugVisitor>(Sym));
  if (ReportedNode)
    R->addRange(ReportedNode->getSourceRange());
  C.emitReport(std::move(R));
}

std::shared_ptr<PathDiagnosticPiece>
DynamicTypePropagation::GenericsBugVisitor::VisitNode(const ExplodedNode *N,
                                                      BugReporterContext &BRC,
                                                      BugReport &BR) {
  ProgramStateRef state = N->getState();
  ProgramStateRef statePrev = N->getFirstPred()->getState();

  const ObjCObjectPointerType *const *TrackedType =
      state->get<MostSpecializedTypeArgsMap>(Sym);
  const ObjCObjectPointerType *const *TrackedTypePrev =
      statePrev->get<MostSpecializedTypeArgsMap>(Sym);
  if (!TrackedType)
    return nullptr;

  if (TrackedTypePrev && *TrackedTypePrev == *TrackedType)
    return nullptr;

  // Retrieve the associated statement.
  const Stmt *S = PathDiagnosticLocation::getStmt(N);
  if (!S)
    return nullptr;

  const LangOptions &LangOpts = BRC.getASTContext().getLangOpts();

  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << "Type '";
  QualType::print(*TrackedType, Qualifiers(), OS, LangOpts, llvm::Twine());
  OS << "' is inferred from ";

  if (const auto *ExplicitCast = dyn_cast<ExplicitCastExpr>(S)) {
    OS << "explicit cast (from '";
    QualType::print(ExplicitCast->getSubExpr()->getType().getTypePtr(),
                    Qualifiers(), OS, LangOpts, llvm::Twine());
    OS << "' to '";
    QualType::print(ExplicitCast->getType().getTypePtr(), Qualifiers(), OS,
                    LangOpts, llvm::Twine());
    OS << "')";
  } else if (const auto *ImplicitCast = dyn_cast<ImplicitCastExpr>(S)) {
    OS << "implicit cast (from '";
    QualType::print(ImplicitCast->getSubExpr()->getType().getTypePtr(),
                    Qualifiers(), OS, LangOpts, llvm::Twine());
    OS << "' to '";
    QualType::print(ImplicitCast->getType().getTypePtr(), Qualifiers(), OS,
                    LangOpts, llvm::Twine());
    OS << "')";
  } else {
    OS << "this context";
  }

  // Generate the extra diagnostic.
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, OS.str(), true,
                                                    nullptr);
}

/// Register checkers.
void ento::registerObjCGenericsChecker(CheckerManager &mgr) {
  DynamicTypePropagation *checker =
      mgr.registerChecker<DynamicTypePropagation>();
  checker->CheckGenerics = true;
}

void ento::registerDynamicTypePropagation(CheckerManager &mgr) {
  mgr.registerChecker<DynamicTypePropagation>();
}
