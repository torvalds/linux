//===- Store.cpp - Interface for maps from Locations to Values ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the types Store and StoreManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BasicValueFactory.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace clang;
using namespace ento;

StoreManager::StoreManager(ProgramStateManager &stateMgr)
    : svalBuilder(stateMgr.getSValBuilder()), StateMgr(stateMgr),
      MRMgr(svalBuilder.getRegionManager()), Ctx(stateMgr.getContext()) {}

StoreRef StoreManager::enterStackFrame(Store OldStore,
                                       const CallEvent &Call,
                                       const StackFrameContext *LCtx) {
  StoreRef Store = StoreRef(OldStore, *this);

  SmallVector<CallEvent::FrameBindingTy, 16> InitialBindings;
  Call.getInitialStackFrameContents(LCtx, InitialBindings);

  for (const auto &I : InitialBindings)
    Store = Bind(Store.getStore(), I.first, I.second);

  return Store;
}

const ElementRegion *StoreManager::MakeElementRegion(const SubRegion *Base,
                                                     QualType EleTy,
                                                     uint64_t index) {
  NonLoc idx = svalBuilder.makeArrayIndex(index);
  return MRMgr.getElementRegion(EleTy, idx, Base, svalBuilder.getContext());
}

const ElementRegion *StoreManager::GetElementZeroRegion(const SubRegion *R,
                                                        QualType T) {
  NonLoc idx = svalBuilder.makeZeroArrayIndex();
  assert(!T.isNull());
  return MRMgr.getElementRegion(T, idx, R, Ctx);
}

const MemRegion *StoreManager::castRegion(const MemRegion *R, QualType CastToTy) {
  ASTContext &Ctx = StateMgr.getContext();

  // Handle casts to Objective-C objects.
  if (CastToTy->isObjCObjectPointerType())
    return R->StripCasts();

  if (CastToTy->isBlockPointerType()) {
    // FIXME: We may need different solutions, depending on the symbol
    // involved.  Blocks can be casted to/from 'id', as they can be treated
    // as Objective-C objects.  This could possibly be handled by enhancing
    // our reasoning of downcasts of symbolic objects.
    if (isa<CodeTextRegion>(R) || isa<SymbolicRegion>(R))
      return R;

    // We don't know what to make of it.  Return a NULL region, which
    // will be interpreted as UnknownVal.
    return nullptr;
  }

  // Now assume we are casting from pointer to pointer. Other cases should
  // already be handled.
  QualType PointeeTy = CastToTy->getPointeeType();
  QualType CanonPointeeTy = Ctx.getCanonicalType(PointeeTy);

  // Handle casts to void*.  We just pass the region through.
  if (CanonPointeeTy.getLocalUnqualifiedType() == Ctx.VoidTy)
    return R;

  // Handle casts from compatible types.
  if (R->isBoundable())
    if (const auto *TR = dyn_cast<TypedValueRegion>(R)) {
      QualType ObjTy = Ctx.getCanonicalType(TR->getValueType());
      if (CanonPointeeTy == ObjTy)
        return R;
    }

  // Process region cast according to the kind of the region being cast.
  switch (R->getKind()) {
    case MemRegion::CXXThisRegionKind:
    case MemRegion::CodeSpaceRegionKind:
    case MemRegion::StackLocalsSpaceRegionKind:
    case MemRegion::StackArgumentsSpaceRegionKind:
    case MemRegion::HeapSpaceRegionKind:
    case MemRegion::UnknownSpaceRegionKind:
    case MemRegion::StaticGlobalSpaceRegionKind:
    case MemRegion::GlobalInternalSpaceRegionKind:
    case MemRegion::GlobalSystemSpaceRegionKind:
    case MemRegion::GlobalImmutableSpaceRegionKind: {
      llvm_unreachable("Invalid region cast");
    }

    case MemRegion::FunctionCodeRegionKind:
    case MemRegion::BlockCodeRegionKind:
    case MemRegion::BlockDataRegionKind:
    case MemRegion::StringRegionKind:
      // FIXME: Need to handle arbitrary downcasts.
    case MemRegion::SymbolicRegionKind:
    case MemRegion::AllocaRegionKind:
    case MemRegion::CompoundLiteralRegionKind:
    case MemRegion::FieldRegionKind:
    case MemRegion::ObjCIvarRegionKind:
    case MemRegion::ObjCStringRegionKind:
    case MemRegion::VarRegionKind:
    case MemRegion::CXXTempObjectRegionKind:
    case MemRegion::CXXBaseObjectRegionKind:
    case MemRegion::CXXDerivedObjectRegionKind:
      return MakeElementRegion(cast<SubRegion>(R), PointeeTy);

    case MemRegion::ElementRegionKind: {
      // If we are casting from an ElementRegion to another type, the
      // algorithm is as follows:
      //
      // (1) Compute the "raw offset" of the ElementRegion from the
      //     base region.  This is done by calling 'getAsRawOffset()'.
      //
      // (2a) If we get a 'RegionRawOffset' after calling
      //      'getAsRawOffset()', determine if the absolute offset
      //      can be exactly divided into chunks of the size of the
      //      casted-pointee type.  If so, create a new ElementRegion with
      //      the pointee-cast type as the new ElementType and the index
      //      being the offset divded by the chunk size.  If not, create
      //      a new ElementRegion at offset 0 off the raw offset region.
      //
      // (2b) If we don't a get a 'RegionRawOffset' after calling
      //      'getAsRawOffset()', it means that we are at offset 0.
      //
      // FIXME: Handle symbolic raw offsets.

      const ElementRegion *elementR = cast<ElementRegion>(R);
      const RegionRawOffset &rawOff = elementR->getAsArrayOffset();
      const MemRegion *baseR = rawOff.getRegion();

      // If we cannot compute a raw offset, throw up our hands and return
      // a NULL MemRegion*.
      if (!baseR)
        return nullptr;

      CharUnits off = rawOff.getOffset();

      if (off.isZero()) {
        // Edge case: we are at 0 bytes off the beginning of baseR.  We
        // check to see if type we are casting to is the same as the base
        // region.  If so, just return the base region.
        if (const auto *TR = dyn_cast<TypedValueRegion>(baseR)) {
          QualType ObjTy = Ctx.getCanonicalType(TR->getValueType());
          QualType CanonPointeeTy = Ctx.getCanonicalType(PointeeTy);
          if (CanonPointeeTy == ObjTy)
            return baseR;
        }

        // Otherwise, create a new ElementRegion at offset 0.
        return MakeElementRegion(cast<SubRegion>(baseR), PointeeTy);
      }

      // We have a non-zero offset from the base region.  We want to determine
      // if the offset can be evenly divided by sizeof(PointeeTy).  If so,
      // we create an ElementRegion whose index is that value.  Otherwise, we
      // create two ElementRegions, one that reflects a raw offset and the other
      // that reflects the cast.

      // Compute the index for the new ElementRegion.
      int64_t newIndex = 0;
      const MemRegion *newSuperR = nullptr;

      // We can only compute sizeof(PointeeTy) if it is a complete type.
      if (!PointeeTy->isIncompleteType()) {
        // Compute the size in **bytes**.
        CharUnits pointeeTySize = Ctx.getTypeSizeInChars(PointeeTy);
        if (!pointeeTySize.isZero()) {
          // Is the offset a multiple of the size?  If so, we can layer the
          // ElementRegion (with elementType == PointeeTy) directly on top of
          // the base region.
          if (off % pointeeTySize == 0) {
            newIndex = off / pointeeTySize;
            newSuperR = baseR;
          }
        }
      }

      if (!newSuperR) {
        // Create an intermediate ElementRegion to represent the raw byte.
        // This will be the super region of the final ElementRegion.
        newSuperR = MakeElementRegion(cast<SubRegion>(baseR), Ctx.CharTy,
                                      off.getQuantity());
      }

      return MakeElementRegion(cast<SubRegion>(newSuperR), PointeeTy, newIndex);
    }
  }

  llvm_unreachable("unreachable");
}

static bool regionMatchesCXXRecordType(SVal V, QualType Ty) {
  const MemRegion *MR = V.getAsRegion();
  if (!MR)
    return true;

  const auto *TVR = dyn_cast<TypedValueRegion>(MR);
  if (!TVR)
    return true;

  const CXXRecordDecl *RD = TVR->getValueType()->getAsCXXRecordDecl();
  if (!RD)
    return true;

  const CXXRecordDecl *Expected = Ty->getPointeeCXXRecordDecl();
  if (!Expected)
    Expected = Ty->getAsCXXRecordDecl();

  return Expected->getCanonicalDecl() == RD->getCanonicalDecl();
}

SVal StoreManager::evalDerivedToBase(SVal Derived, const CastExpr *Cast) {
  // Sanity check to avoid doing the wrong thing in the face of
  // reinterpret_cast.
  if (!regionMatchesCXXRecordType(Derived, Cast->getSubExpr()->getType()))
    return UnknownVal();

  // Walk through the cast path to create nested CXXBaseRegions.
  SVal Result = Derived;
  for (CastExpr::path_const_iterator I = Cast->path_begin(),
                                     E = Cast->path_end();
       I != E; ++I) {
    Result = evalDerivedToBase(Result, (*I)->getType(), (*I)->isVirtual());
  }
  return Result;
}

SVal StoreManager::evalDerivedToBase(SVal Derived, const CXXBasePath &Path) {
  // Walk through the path to create nested CXXBaseRegions.
  SVal Result = Derived;
  for (const auto &I : Path)
    Result = evalDerivedToBase(Result, I.Base->getType(),
                               I.Base->isVirtual());
  return Result;
}

SVal StoreManager::evalDerivedToBase(SVal Derived, QualType BaseType,
                                     bool IsVirtual) {
  const MemRegion *DerivedReg = Derived.getAsRegion();
  if (!DerivedReg)
    return Derived;

  const CXXRecordDecl *BaseDecl = BaseType->getPointeeCXXRecordDecl();
  if (!BaseDecl)
    BaseDecl = BaseType->getAsCXXRecordDecl();
  assert(BaseDecl && "not a C++ object?");

  if (const auto *AlreadyDerivedReg =
          dyn_cast<CXXDerivedObjectRegion>(DerivedReg)) {
    if (const auto *SR =
            dyn_cast<SymbolicRegion>(AlreadyDerivedReg->getSuperRegion()))
      if (SR->getSymbol()->getType()->getPointeeCXXRecordDecl() == BaseDecl)
        return loc::MemRegionVal(SR);

    DerivedReg = AlreadyDerivedReg->getSuperRegion();
  }

  const MemRegion *BaseReg = MRMgr.getCXXBaseObjectRegion(
      BaseDecl, cast<SubRegion>(DerivedReg), IsVirtual);

  return loc::MemRegionVal(BaseReg);
}

/// Returns the static type of the given region, if it represents a C++ class
/// object.
///
/// This handles both fully-typed regions, where the dynamic type is known, and
/// symbolic regions, where the dynamic type is merely bounded (and even then,
/// only ostensibly!), but does not take advantage of any dynamic type info.
static const CXXRecordDecl *getCXXRecordType(const MemRegion *MR) {
  if (const auto *TVR = dyn_cast<TypedValueRegion>(MR))
    return TVR->getValueType()->getAsCXXRecordDecl();
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    return SR->getSymbol()->getType()->getPointeeCXXRecordDecl();
  return nullptr;
}

SVal StoreManager::attemptDownCast(SVal Base, QualType TargetType,
                                   bool &Failed) {
  Failed = false;

  const MemRegion *MR = Base.getAsRegion();
  if (!MR)
    return UnknownVal();

  // Assume the derived class is a pointer or a reference to a CXX record.
  TargetType = TargetType->getPointeeType();
  assert(!TargetType.isNull());
  const CXXRecordDecl *TargetClass = TargetType->getAsCXXRecordDecl();
  if (!TargetClass && !TargetType->isVoidType())
    return UnknownVal();

  // Drill down the CXXBaseObject chains, which represent upcasts (casts from
  // derived to base).
  while (const CXXRecordDecl *MRClass = getCXXRecordType(MR)) {
    // If found the derived class, the cast succeeds.
    if (MRClass == TargetClass)
      return loc::MemRegionVal(MR);

    // We skip over incomplete types. They must be the result of an earlier
    // reinterpret_cast, as one can only dynamic_cast between types in the same
    // class hierarchy.
    if (!TargetType->isVoidType() && MRClass->hasDefinition()) {
      // Static upcasts are marked as DerivedToBase casts by Sema, so this will
      // only happen when multiple or virtual inheritance is involved.
      CXXBasePaths Paths(/*FindAmbiguities=*/false, /*RecordPaths=*/true,
                         /*DetectVirtual=*/false);
      if (MRClass->isDerivedFrom(TargetClass, Paths))
        return evalDerivedToBase(loc::MemRegionVal(MR), Paths.front());
    }

    if (const auto *BaseR = dyn_cast<CXXBaseObjectRegion>(MR)) {
      // Drill down the chain to get the derived classes.
      MR = BaseR->getSuperRegion();
      continue;
    }

    // If this is a cast to void*, return the region.
    if (TargetType->isVoidType())
      return loc::MemRegionVal(MR);

    // Strange use of reinterpret_cast can give us paths we don't reason
    // about well, by putting in ElementRegions where we'd expect
    // CXXBaseObjectRegions. If it's a valid reinterpret_cast (i.e. if the
    // derived class has a zero offset from the base class), then it's safe
    // to strip the cast; if it's invalid, -Wreinterpret-base-class should
    // catch it. In the interest of performance, the analyzer will silently
    // do the wrong thing in the invalid case (because offsets for subregions
    // will be wrong).
    const MemRegion *Uncasted = MR->StripCasts(/*IncludeBaseCasts=*/false);
    if (Uncasted == MR) {
      // We reached the bottom of the hierarchy and did not find the derived
      // class. We must be casting the base to derived, so the cast should
      // fail.
      break;
    }

    MR = Uncasted;
  }

  // If we're casting a symbolic base pointer to a derived class, use
  // CXXDerivedObjectRegion to represent the cast. If it's a pointer to an
  // unrelated type, it must be a weird reinterpret_cast and we have to
  // be fine with ElementRegion. TODO: Should we instead make
  // Derived{TargetClass, Element{SourceClass, SR}}?
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR)) {
    QualType T = SR->getSymbol()->getType();
    const CXXRecordDecl *SourceClass = T->getPointeeCXXRecordDecl();
    if (TargetClass && SourceClass && TargetClass->isDerivedFrom(SourceClass))
      return loc::MemRegionVal(
          MRMgr.getCXXDerivedObjectRegion(TargetClass, SR));
    return loc::MemRegionVal(GetElementZeroRegion(SR, TargetType));
  }

  // We failed if the region we ended up with has perfect type info.
  Failed = isa<TypedValueRegion>(MR);
  return UnknownVal();
}

/// CastRetrievedVal - Used by subclasses of StoreManager to implement
///  implicit casts that arise from loads from regions that are reinterpreted
///  as another region.
SVal StoreManager::CastRetrievedVal(SVal V, const TypedValueRegion *R,
                                    QualType castTy) {
  if (castTy.isNull() || V.isUnknownOrUndef())
    return V;

  // The dispatchCast() call below would convert the int into a float.
  // What we want, however, is a bit-by-bit reinterpretation of the int
  // as a float, which usually yields nothing garbage. For now skip casts
  // from ints to floats.
  // TODO: What other combinations of types are affected?
  if (castTy->isFloatingType()) {
    SymbolRef Sym = V.getAsSymbol();
    if (Sym && !Sym->getType()->isFloatingType())
      return UnknownVal();
  }

  // When retrieving symbolic pointer and expecting a non-void pointer,
  // wrap them into element regions of the expected type if necessary.
  // SValBuilder::dispatchCast() doesn't do that, but it is necessary to
  // make sure that the retrieved value makes sense, because there's no other
  // cast in the AST that would tell us to cast it to the correct pointer type.
  // We might need to do that for non-void pointers as well.
  // FIXME: We really need a single good function to perform casts for us
  // correctly every time we need it.
  if (castTy->isPointerType() && !castTy->isVoidPointerType())
    if (const auto *SR = dyn_cast_or_null<SymbolicRegion>(V.getAsRegion()))
      if (SR->getSymbol()->getType().getCanonicalType() !=
          castTy.getCanonicalType())
        return loc::MemRegionVal(castRegion(SR, castTy));

  return svalBuilder.dispatchCast(V, castTy);
}

SVal StoreManager::getLValueFieldOrIvar(const Decl *D, SVal Base) {
  if (Base.isUnknownOrUndef())
    return Base;

  Loc BaseL = Base.castAs<Loc>();
  const SubRegion* BaseR = nullptr;

  switch (BaseL.getSubKind()) {
  case loc::MemRegionValKind:
    BaseR = cast<SubRegion>(BaseL.castAs<loc::MemRegionVal>().getRegion());
    break;

  case loc::GotoLabelKind:
    // These are anormal cases. Flag an undefined value.
    return UndefinedVal();

  case loc::ConcreteIntKind:
    // While these seem funny, this can happen through casts.
    // FIXME: What we should return is the field offset, not base. For example,
    //  add the field offset to the integer value.  That way things
    //  like this work properly:  &(((struct foo *) 0xa)->f)
    //  However, that's not easy to fix without reducing our abilities
    //  to catch null pointer dereference. Eg., ((struct foo *)0x0)->f = 7
    //  is a null dereference even though we're dereferencing offset of f
    //  rather than null. Coming up with an approach that computes offsets
    //  over null pointers properly while still being able to catch null
    //  dereferences might be worth it.
    return Base;

  default:
    llvm_unreachable("Unhandled Base.");
  }

  // NOTE: We must have this check first because ObjCIvarDecl is a subclass
  // of FieldDecl.
  if (const auto *ID = dyn_cast<ObjCIvarDecl>(D))
    return loc::MemRegionVal(MRMgr.getObjCIvarRegion(ID, BaseR));

  return loc::MemRegionVal(MRMgr.getFieldRegion(cast<FieldDecl>(D), BaseR));
}

SVal StoreManager::getLValueIvar(const ObjCIvarDecl *decl, SVal base) {
  return getLValueFieldOrIvar(decl, base);
}

SVal StoreManager::getLValueElement(QualType elementType, NonLoc Offset,
                                    SVal Base) {
  // If the base is an unknown or undefined value, just return it back.
  // FIXME: For absolute pointer addresses, we just return that value back as
  //  well, although in reality we should return the offset added to that
  //  value. See also the similar FIXME in getLValueFieldOrIvar().
  if (Base.isUnknownOrUndef() || Base.getAs<loc::ConcreteInt>())
    return Base;

  if (Base.getAs<loc::GotoLabel>())
    return UnknownVal();

  const SubRegion *BaseRegion =
      Base.castAs<loc::MemRegionVal>().getRegionAs<SubRegion>();

  // Pointer of any type can be cast and used as array base.
  const auto *ElemR = dyn_cast<ElementRegion>(BaseRegion);

  // Convert the offset to the appropriate size and signedness.
  Offset = svalBuilder.convertToArrayIndex(Offset).castAs<NonLoc>();

  if (!ElemR) {
    // If the base region is not an ElementRegion, create one.
    // This can happen in the following example:
    //
    //   char *p = __builtin_alloc(10);
    //   p[1] = 8;
    //
    //  Observe that 'p' binds to an AllocaRegion.
    return loc::MemRegionVal(MRMgr.getElementRegion(elementType, Offset,
                                                    BaseRegion, Ctx));
  }

  SVal BaseIdx = ElemR->getIndex();

  if (!BaseIdx.getAs<nonloc::ConcreteInt>())
    return UnknownVal();

  const llvm::APSInt &BaseIdxI =
      BaseIdx.castAs<nonloc::ConcreteInt>().getValue();

  // Only allow non-integer offsets if the base region has no offset itself.
  // FIXME: This is a somewhat arbitrary restriction. We should be using
  // SValBuilder here to add the two offsets without checking their types.
  if (!Offset.getAs<nonloc::ConcreteInt>()) {
    if (isa<ElementRegion>(BaseRegion->StripCasts()))
      return UnknownVal();

    return loc::MemRegionVal(MRMgr.getElementRegion(
        elementType, Offset, cast<SubRegion>(ElemR->getSuperRegion()), Ctx));
  }

  const llvm::APSInt& OffI = Offset.castAs<nonloc::ConcreteInt>().getValue();
  assert(BaseIdxI.isSigned());

  // Compute the new index.
  nonloc::ConcreteInt NewIdx(svalBuilder.getBasicValueFactory().getValue(BaseIdxI +
                                                                    OffI));

  // Construct the new ElementRegion.
  const SubRegion *ArrayR = cast<SubRegion>(ElemR->getSuperRegion());
  return loc::MemRegionVal(MRMgr.getElementRegion(elementType, NewIdx, ArrayR,
                                                  Ctx));
}

StoreManager::BindingsHandler::~BindingsHandler() = default;

bool StoreManager::FindUniqueBinding::HandleBinding(StoreManager& SMgr,
                                                    Store store,
                                                    const MemRegion* R,
                                                    SVal val) {
  SymbolRef SymV = val.getAsLocSymbol();
  if (!SymV || SymV != Sym)
    return true;

  if (Binding) {
    First = false;
    return false;
  }
  else
    Binding = R;

  return true;
}
