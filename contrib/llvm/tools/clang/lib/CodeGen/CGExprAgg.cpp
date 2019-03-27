//===--- CGExprAgg.cpp - Emit LLVM Code from Aggregate Expressions --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Aggregate Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CGCXXABI.h"
#include "CGObjCRuntime.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                        Aggregate Expression Emitter
//===----------------------------------------------------------------------===//

namespace  {
class AggExprEmitter : public StmtVisitor<AggExprEmitter> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  AggValueSlot Dest;
  bool IsResultUnused;

  AggValueSlot EnsureSlot(QualType T) {
    if (!Dest.isIgnored()) return Dest;
    return CGF.CreateAggTemp(T, "agg.tmp.ensured");
  }
  void EnsureDest(QualType T) {
    if (!Dest.isIgnored()) return;
    Dest = CGF.CreateAggTemp(T, "agg.tmp.ensured");
  }

  // Calls `Fn` with a valid return value slot, potentially creating a temporary
  // to do so. If a temporary is created, an appropriate copy into `Dest` will
  // be emitted, as will lifetime markers.
  //
  // The given function should take a ReturnValueSlot, and return an RValue that
  // points to said slot.
  void withReturnValueSlot(const Expr *E,
                           llvm::function_ref<RValue(ReturnValueSlot)> Fn);

public:
  AggExprEmitter(CodeGenFunction &cgf, AggValueSlot Dest, bool IsResultUnused)
    : CGF(cgf), Builder(CGF.Builder), Dest(Dest),
    IsResultUnused(IsResultUnused) { }

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  /// EmitAggLoadOfLValue - Given an expression with aggregate type that
  /// represents a value lvalue, this method emits the address of the lvalue,
  /// then loads the result into DestPtr.
  void EmitAggLoadOfLValue(const Expr *E);

  enum ExprValueKind {
    EVK_RValue,
    EVK_NonRValue
  };

  /// EmitFinalDestCopy - Perform the final copy to DestPtr, if desired.
  /// SrcIsRValue is true if source comes from an RValue.
  void EmitFinalDestCopy(QualType type, const LValue &src,
                         ExprValueKind SrcValueKind = EVK_NonRValue);
  void EmitFinalDestCopy(QualType type, RValue src);
  void EmitCopy(QualType type, const AggValueSlot &dest,
                const AggValueSlot &src);

  void EmitMoveFromReturnSlot(const Expr *E, RValue Src);

  void EmitArrayInit(Address DestPtr, llvm::ArrayType *AType,
                     QualType ArrayQTy, InitListExpr *E);

  AggValueSlot::NeedsGCBarriers_t needsGC(QualType T) {
    if (CGF.getLangOpts().getGC() && TypeRequiresGCollection(T))
      return AggValueSlot::NeedsGCBarriers;
    return AggValueSlot::DoesNotNeedGCBarriers;
  }

  bool TypeRequiresGCollection(QualType T);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  void Visit(Expr *E) {
    ApplyDebugLocation DL(CGF, E);
    StmtVisitor<AggExprEmitter>::Visit(E);
  }

  void VisitStmt(Stmt *S) {
    CGF.ErrorUnsupported(S, "aggregate expression");
  }
  void VisitParenExpr(ParenExpr *PE) { Visit(PE->getSubExpr()); }
  void VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    Visit(GE->getResultExpr());
  }
  void VisitCoawaitExpr(CoawaitExpr *E) {
    CGF.EmitCoawaitExpr(*E, Dest, IsResultUnused);
  }
  void VisitCoyieldExpr(CoyieldExpr *E) {
    CGF.EmitCoyieldExpr(*E, Dest, IsResultUnused);
  }
  void VisitUnaryCoawait(UnaryOperator *E) { Visit(E->getSubExpr()); }
  void VisitUnaryExtension(UnaryOperator *E) { Visit(E->getSubExpr()); }
  void VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E) {
    return Visit(E->getReplacement());
  }

  void VisitConstantExpr(ConstantExpr *E) {
    return Visit(E->getSubExpr());
  }

  // l-values.
  void VisitDeclRefExpr(DeclRefExpr *E) { EmitAggLoadOfLValue(E); }
  void VisitMemberExpr(MemberExpr *ME) { EmitAggLoadOfLValue(ME); }
  void VisitUnaryDeref(UnaryOperator *E) { EmitAggLoadOfLValue(E); }
  void VisitStringLiteral(StringLiteral *E) { EmitAggLoadOfLValue(E); }
  void VisitCompoundLiteralExpr(CompoundLiteralExpr *E);
  void VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    EmitAggLoadOfLValue(E);
  }
  void VisitPredefinedExpr(const PredefinedExpr *E) {
    EmitAggLoadOfLValue(E);
  }

  // Operators.
  void VisitCastExpr(CastExpr *E);
  void VisitCallExpr(const CallExpr *E);
  void VisitStmtExpr(const StmtExpr *E);
  void VisitBinaryOperator(const BinaryOperator *BO);
  void VisitPointerToDataMemberBinaryOperator(const BinaryOperator *BO);
  void VisitBinAssign(const BinaryOperator *E);
  void VisitBinComma(const BinaryOperator *E);
  void VisitBinCmp(const BinaryOperator *E);

  void VisitObjCMessageExpr(ObjCMessageExpr *E);
  void VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
    EmitAggLoadOfLValue(E);
  }

  void VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E);
  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO);
  void VisitChooseExpr(const ChooseExpr *CE);
  void VisitInitListExpr(InitListExpr *E);
  void VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E,
                              llvm::Value *outerBegin = nullptr);
  void VisitImplicitValueInitExpr(ImplicitValueInitExpr *E);
  void VisitNoInitExpr(NoInitExpr *E) { } // Do nothing.
  void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE) {
    Visit(DAE->getExpr());
  }
  void VisitCXXDefaultInitExpr(CXXDefaultInitExpr *DIE) {
    CodeGenFunction::CXXDefaultInitExprScope Scope(CGF);
    Visit(DIE->getExpr());
  }
  void VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E);
  void VisitCXXConstructExpr(const CXXConstructExpr *E);
  void VisitCXXInheritedCtorInitExpr(const CXXInheritedCtorInitExpr *E);
  void VisitLambdaExpr(LambdaExpr *E);
  void VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E);
  void VisitExprWithCleanups(ExprWithCleanups *E);
  void VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E);
  void VisitCXXTypeidExpr(CXXTypeidExpr *E) { EmitAggLoadOfLValue(E); }
  void VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E);
  void VisitOpaqueValueExpr(OpaqueValueExpr *E);

  void VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    if (E->isGLValue()) {
      LValue LV = CGF.EmitPseudoObjectLValue(E);
      return EmitFinalDestCopy(E->getType(), LV);
    }

    CGF.EmitPseudoObjectRValue(E, EnsureSlot(E->getType()));
  }

  void VisitVAArgExpr(VAArgExpr *E);

  void EmitInitializationToLValue(Expr *E, LValue Address);
  void EmitNullInitializationToLValue(LValue Address);
  //  case Expr::ChooseExprClass:
  void VisitCXXThrowExpr(const CXXThrowExpr *E) { CGF.EmitCXXThrowExpr(E); }
  void VisitAtomicExpr(AtomicExpr *E) {
    RValue Res = CGF.EmitAtomicExpr(E);
    EmitFinalDestCopy(E->getType(), Res);
  }
};
}  // end anonymous namespace.

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

/// EmitAggLoadOfLValue - Given an expression with aggregate type that
/// represents a value lvalue, this method emits the address of the lvalue,
/// then loads the result into DestPtr.
void AggExprEmitter::EmitAggLoadOfLValue(const Expr *E) {
  LValue LV = CGF.EmitLValue(E);

  // If the type of the l-value is atomic, then do an atomic load.
  if (LV.getType()->isAtomicType() || CGF.LValueIsSuitableForInlineAtomic(LV)) {
    CGF.EmitAtomicLoad(LV, E->getExprLoc(), Dest);
    return;
  }

  EmitFinalDestCopy(E->getType(), LV);
}

/// True if the given aggregate type requires special GC API calls.
bool AggExprEmitter::TypeRequiresGCollection(QualType T) {
  // Only record types have members that might require garbage collection.
  const RecordType *RecordTy = T->getAs<RecordType>();
  if (!RecordTy) return false;

  // Don't mess with non-trivial C++ types.
  RecordDecl *Record = RecordTy->getDecl();
  if (isa<CXXRecordDecl>(Record) &&
      (cast<CXXRecordDecl>(Record)->hasNonTrivialCopyConstructor() ||
       !cast<CXXRecordDecl>(Record)->hasTrivialDestructor()))
    return false;

  // Check whether the type has an object member.
  return Record->hasObjectMember();
}

void AggExprEmitter::withReturnValueSlot(
    const Expr *E, llvm::function_ref<RValue(ReturnValueSlot)> EmitCall) {
  QualType RetTy = E->getType();
  bool RequiresDestruction =
      Dest.isIgnored() &&
      RetTy.isDestructedType() == QualType::DK_nontrivial_c_struct;

  // If it makes no observable difference, save a memcpy + temporary.
  //
  // We need to always provide our own temporary if destruction is required.
  // Otherwise, EmitCall will emit its own, notice that it's "unused", and end
  // its lifetime before we have the chance to emit a proper destructor call.
  bool UseTemp = Dest.isPotentiallyAliased() || Dest.requiresGCollection() ||
                 (RequiresDestruction && !Dest.getAddress().isValid());

  Address RetAddr = Address::invalid();
  Address RetAllocaAddr = Address::invalid();

  EHScopeStack::stable_iterator LifetimeEndBlock;
  llvm::Value *LifetimeSizePtr = nullptr;
  llvm::IntrinsicInst *LifetimeStartInst = nullptr;
  if (!UseTemp) {
    RetAddr = Dest.getAddress();
  } else {
    RetAddr = CGF.CreateMemTemp(RetTy, "tmp", &RetAllocaAddr);
    uint64_t Size =
        CGF.CGM.getDataLayout().getTypeAllocSize(CGF.ConvertTypeForMem(RetTy));
    LifetimeSizePtr = CGF.EmitLifetimeStart(Size, RetAllocaAddr.getPointer());
    if (LifetimeSizePtr) {
      LifetimeStartInst =
          cast<llvm::IntrinsicInst>(std::prev(Builder.GetInsertPoint()));
      assert(LifetimeStartInst->getIntrinsicID() ==
                 llvm::Intrinsic::lifetime_start &&
             "Last insertion wasn't a lifetime.start?");

      CGF.pushFullExprCleanup<CodeGenFunction::CallLifetimeEnd>(
          NormalEHLifetimeMarker, RetAllocaAddr, LifetimeSizePtr);
      LifetimeEndBlock = CGF.EHStack.stable_begin();
    }
  }

  RValue Src =
      EmitCall(ReturnValueSlot(RetAddr, Dest.isVolatile(), IsResultUnused));

  if (RequiresDestruction)
    CGF.pushDestroy(RetTy.isDestructedType(), Src.getAggregateAddress(), RetTy);

  if (!UseTemp)
    return;

  assert(Dest.getPointer() != Src.getAggregatePointer());
  EmitFinalDestCopy(E->getType(), Src);

  if (!RequiresDestruction && LifetimeStartInst) {
    // If there's no dtor to run, the copy was the last use of our temporary.
    // Since we're not guaranteed to be in an ExprWithCleanups, clean up
    // eagerly.
    CGF.DeactivateCleanupBlock(LifetimeEndBlock, LifetimeStartInst);
    CGF.EmitLifetimeEnd(LifetimeSizePtr, RetAllocaAddr.getPointer());
  }
}

/// EmitFinalDestCopy - Perform the final copy to DestPtr, if desired.
void AggExprEmitter::EmitFinalDestCopy(QualType type, RValue src) {
  assert(src.isAggregate() && "value must be aggregate value!");
  LValue srcLV = CGF.MakeAddrLValue(src.getAggregateAddress(), type);
  EmitFinalDestCopy(type, srcLV, EVK_RValue);
}

/// EmitFinalDestCopy - Perform the final copy to DestPtr, if desired.
void AggExprEmitter::EmitFinalDestCopy(QualType type, const LValue &src,
                                       ExprValueKind SrcValueKind) {
  // If Dest is ignored, then we're evaluating an aggregate expression
  // in a context that doesn't care about the result.  Note that loads
  // from volatile l-values force the existence of a non-ignored
  // destination.
  if (Dest.isIgnored())
    return;

  // Copy non-trivial C structs here.
  LValue DstLV = CGF.MakeAddrLValue(
      Dest.getAddress(), Dest.isVolatile() ? type.withVolatile() : type);

  if (SrcValueKind == EVK_RValue) {
    if (type.isNonTrivialToPrimitiveDestructiveMove() == QualType::PCK_Struct) {
      if (Dest.isPotentiallyAliased())
        CGF.callCStructMoveAssignmentOperator(DstLV, src);
      else
        CGF.callCStructMoveConstructor(DstLV, src);
      return;
    }
  } else {
    if (type.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct) {
      if (Dest.isPotentiallyAliased())
        CGF.callCStructCopyAssignmentOperator(DstLV, src);
      else
        CGF.callCStructCopyConstructor(DstLV, src);
      return;
    }
  }

  AggValueSlot srcAgg =
    AggValueSlot::forLValue(src, AggValueSlot::IsDestructed,
                            needsGC(type), AggValueSlot::IsAliased,
                            AggValueSlot::MayOverlap);
  EmitCopy(type, Dest, srcAgg);
}

/// Perform a copy from the source into the destination.
///
/// \param type - the type of the aggregate being copied; qualifiers are
///   ignored
void AggExprEmitter::EmitCopy(QualType type, const AggValueSlot &dest,
                              const AggValueSlot &src) {
  if (dest.requiresGCollection()) {
    CharUnits sz = dest.getPreferredSize(CGF.getContext(), type);
    llvm::Value *size = llvm::ConstantInt::get(CGF.SizeTy, sz.getQuantity());
    CGF.CGM.getObjCRuntime().EmitGCMemmoveCollectable(CGF,
                                                      dest.getAddress(),
                                                      src.getAddress(),
                                                      size);
    return;
  }

  // If the result of the assignment is used, copy the LHS there also.
  // It's volatile if either side is.  Use the minimum alignment of
  // the two sides.
  LValue DestLV = CGF.MakeAddrLValue(dest.getAddress(), type);
  LValue SrcLV = CGF.MakeAddrLValue(src.getAddress(), type);
  CGF.EmitAggregateCopy(DestLV, SrcLV, type, dest.mayOverlap(),
                        dest.isVolatile() || src.isVolatile());
}

/// Emit the initializer for a std::initializer_list initialized with a
/// real initializer list.
void
AggExprEmitter::VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E) {
  // Emit an array containing the elements.  The array is externally destructed
  // if the std::initializer_list object is.
  ASTContext &Ctx = CGF.getContext();
  LValue Array = CGF.EmitLValue(E->getSubExpr());
  assert(Array.isSimple() && "initializer_list array not a simple lvalue");
  Address ArrayPtr = Array.getAddress();

  const ConstantArrayType *ArrayType =
      Ctx.getAsConstantArrayType(E->getSubExpr()->getType());
  assert(ArrayType && "std::initializer_list constructed from non-array");

  // FIXME: Perform the checks on the field types in SemaInit.
  RecordDecl *Record = E->getType()->castAs<RecordType>()->getDecl();
  RecordDecl::field_iterator Field = Record->field_begin();
  if (Field == Record->field_end()) {
    CGF.ErrorUnsupported(E, "weird std::initializer_list");
    return;
  }

  // Start pointer.
  if (!Field->getType()->isPointerType() ||
      !Ctx.hasSameType(Field->getType()->getPointeeType(),
                       ArrayType->getElementType())) {
    CGF.ErrorUnsupported(E, "weird std::initializer_list");
    return;
  }

  AggValueSlot Dest = EnsureSlot(E->getType());
  LValue DestLV = CGF.MakeAddrLValue(Dest.getAddress(), E->getType());
  LValue Start = CGF.EmitLValueForFieldInitialization(DestLV, *Field);
  llvm::Value *Zero = llvm::ConstantInt::get(CGF.PtrDiffTy, 0);
  llvm::Value *IdxStart[] = { Zero, Zero };
  llvm::Value *ArrayStart =
      Builder.CreateInBoundsGEP(ArrayPtr.getPointer(), IdxStart, "arraystart");
  CGF.EmitStoreThroughLValue(RValue::get(ArrayStart), Start);
  ++Field;

  if (Field == Record->field_end()) {
    CGF.ErrorUnsupported(E, "weird std::initializer_list");
    return;
  }

  llvm::Value *Size = Builder.getInt(ArrayType->getSize());
  LValue EndOrLength = CGF.EmitLValueForFieldInitialization(DestLV, *Field);
  if (Field->getType()->isPointerType() &&
      Ctx.hasSameType(Field->getType()->getPointeeType(),
                      ArrayType->getElementType())) {
    // End pointer.
    llvm::Value *IdxEnd[] = { Zero, Size };
    llvm::Value *ArrayEnd =
        Builder.CreateInBoundsGEP(ArrayPtr.getPointer(), IdxEnd, "arrayend");
    CGF.EmitStoreThroughLValue(RValue::get(ArrayEnd), EndOrLength);
  } else if (Ctx.hasSameType(Field->getType(), Ctx.getSizeType())) {
    // Length.
    CGF.EmitStoreThroughLValue(RValue::get(Size), EndOrLength);
  } else {
    CGF.ErrorUnsupported(E, "weird std::initializer_list");
    return;
  }
}

/// Determine if E is a trivial array filler, that is, one that is
/// equivalent to zero-initialization.
static bool isTrivialFiller(Expr *E) {
  if (!E)
    return true;

  if (isa<ImplicitValueInitExpr>(E))
    return true;

  if (auto *ILE = dyn_cast<InitListExpr>(E)) {
    if (ILE->getNumInits())
      return false;
    return isTrivialFiller(ILE->getArrayFiller());
  }

  if (auto *Cons = dyn_cast_or_null<CXXConstructExpr>(E))
    return Cons->getConstructor()->isDefaultConstructor() &&
           Cons->getConstructor()->isTrivial();

  // FIXME: Are there other cases where we can avoid emitting an initializer?
  return false;
}

/// Emit initialization of an array from an initializer list.
void AggExprEmitter::EmitArrayInit(Address DestPtr, llvm::ArrayType *AType,
                                   QualType ArrayQTy, InitListExpr *E) {
  uint64_t NumInitElements = E->getNumInits();

  uint64_t NumArrayElements = AType->getNumElements();
  assert(NumInitElements <= NumArrayElements);

  QualType elementType =
      CGF.getContext().getAsArrayType(ArrayQTy)->getElementType();

  // DestPtr is an array*.  Construct an elementType* by drilling
  // down a level.
  llvm::Value *zero = llvm::ConstantInt::get(CGF.SizeTy, 0);
  llvm::Value *indices[] = { zero, zero };
  llvm::Value *begin =
    Builder.CreateInBoundsGEP(DestPtr.getPointer(), indices, "arrayinit.begin");

  CharUnits elementSize = CGF.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
    DestPtr.getAlignment().alignmentOfArrayElement(elementSize);

  // Consider initializing the array by copying from a global. For this to be
  // more efficient than per-element initialization, the size of the elements
  // with explicit initializers should be large enough.
  if (NumInitElements * elementSize.getQuantity() > 16 &&
      elementType.isTriviallyCopyableType(CGF.getContext())) {
    CodeGen::CodeGenModule &CGM = CGF.CGM;
    ConstantEmitter Emitter(CGM);
    LangAS AS = ArrayQTy.getAddressSpace();
    if (llvm::Constant *C = Emitter.tryEmitForInitializer(E, AS, ArrayQTy)) {
      auto GV = new llvm::GlobalVariable(
          CGM.getModule(), C->getType(),
          CGM.isTypeConstant(ArrayQTy, /* ExcludeCtorDtor= */ true),
          llvm::GlobalValue::PrivateLinkage, C, "constinit",
          /* InsertBefore= */ nullptr, llvm::GlobalVariable::NotThreadLocal,
          CGM.getContext().getTargetAddressSpace(AS));
      Emitter.finalize(GV);
      CharUnits Align = CGM.getContext().getTypeAlignInChars(ArrayQTy);
      GV->setAlignment(Align.getQuantity());
      EmitFinalDestCopy(ArrayQTy, CGF.MakeAddrLValue(GV, ArrayQTy, Align));
      return;
    }
  }

  // Exception safety requires us to destroy all the
  // already-constructed members if an initializer throws.
  // For that, we'll need an EH cleanup.
  QualType::DestructionKind dtorKind = elementType.isDestructedType();
  Address endOfInit = Address::invalid();
  EHScopeStack::stable_iterator cleanup;
  llvm::Instruction *cleanupDominator = nullptr;
  if (CGF.needsEHCleanup(dtorKind)) {
    // In principle we could tell the cleanup where we are more
    // directly, but the control flow can get so varied here that it
    // would actually be quite complex.  Therefore we go through an
    // alloca.
    endOfInit = CGF.CreateTempAlloca(begin->getType(), CGF.getPointerAlign(),
                                     "arrayinit.endOfInit");
    cleanupDominator = Builder.CreateStore(begin, endOfInit);
    CGF.pushIrregularPartialArrayCleanup(begin, endOfInit, elementType,
                                         elementAlign,
                                         CGF.getDestroyer(dtorKind));
    cleanup = CGF.EHStack.stable_begin();

  // Otherwise, remember that we didn't need a cleanup.
  } else {
    dtorKind = QualType::DK_none;
  }

  llvm::Value *one = llvm::ConstantInt::get(CGF.SizeTy, 1);

  // The 'current element to initialize'.  The invariants on this
  // variable are complicated.  Essentially, after each iteration of
  // the loop, it points to the last initialized element, except
  // that it points to the beginning of the array before any
  // elements have been initialized.
  llvm::Value *element = begin;

  // Emit the explicit initializers.
  for (uint64_t i = 0; i != NumInitElements; ++i) {
    // Advance to the next element.
    if (i > 0) {
      element = Builder.CreateInBoundsGEP(element, one, "arrayinit.element");

      // Tell the cleanup that it needs to destroy up to this
      // element.  TODO: some of these stores can be trivially
      // observed to be unnecessary.
      if (endOfInit.isValid()) Builder.CreateStore(element, endOfInit);
    }

    LValue elementLV =
      CGF.MakeAddrLValue(Address(element, elementAlign), elementType);
    EmitInitializationToLValue(E->getInit(i), elementLV);
  }

  // Check whether there's a non-trivial array-fill expression.
  Expr *filler = E->getArrayFiller();
  bool hasTrivialFiller = isTrivialFiller(filler);

  // Any remaining elements need to be zero-initialized, possibly
  // using the filler expression.  We can skip this if the we're
  // emitting to zeroed memory.
  if (NumInitElements != NumArrayElements &&
      !(Dest.isZeroed() && hasTrivialFiller &&
        CGF.getTypes().isZeroInitializable(elementType))) {

    // Use an actual loop.  This is basically
    //   do { *array++ = filler; } while (array != end);

    // Advance to the start of the rest of the array.
    if (NumInitElements) {
      element = Builder.CreateInBoundsGEP(element, one, "arrayinit.start");
      if (endOfInit.isValid()) Builder.CreateStore(element, endOfInit);
    }

    // Compute the end of the array.
    llvm::Value *end = Builder.CreateInBoundsGEP(begin,
                      llvm::ConstantInt::get(CGF.SizeTy, NumArrayElements),
                                                 "arrayinit.end");

    llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
    llvm::BasicBlock *bodyBB = CGF.createBasicBlock("arrayinit.body");

    // Jump into the body.
    CGF.EmitBlock(bodyBB);
    llvm::PHINode *currentElement =
      Builder.CreatePHI(element->getType(), 2, "arrayinit.cur");
    currentElement->addIncoming(element, entryBB);

    // Emit the actual filler expression.
    {
      // C++1z [class.temporary]p5:
      //   when a default constructor is called to initialize an element of
      //   an array with no corresponding initializer [...] the destruction of
      //   every temporary created in a default argument is sequenced before
      //   the construction of the next array element, if any
      CodeGenFunction::RunCleanupsScope CleanupsScope(CGF);
      LValue elementLV =
        CGF.MakeAddrLValue(Address(currentElement, elementAlign), elementType);
      if (filler)
        EmitInitializationToLValue(filler, elementLV);
      else
        EmitNullInitializationToLValue(elementLV);
    }

    // Move on to the next element.
    llvm::Value *nextElement =
      Builder.CreateInBoundsGEP(currentElement, one, "arrayinit.next");

    // Tell the EH cleanup that we finished with the last element.
    if (endOfInit.isValid()) Builder.CreateStore(nextElement, endOfInit);

    // Leave the loop if we're done.
    llvm::Value *done = Builder.CreateICmpEQ(nextElement, end,
                                             "arrayinit.done");
    llvm::BasicBlock *endBB = CGF.createBasicBlock("arrayinit.end");
    Builder.CreateCondBr(done, endBB, bodyBB);
    currentElement->addIncoming(nextElement, Builder.GetInsertBlock());

    CGF.EmitBlock(endBB);
  }

  // Leave the partial-array cleanup if we entered one.
  if (dtorKind) CGF.DeactivateCleanupBlock(cleanup, cleanupDominator);
}

//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

void AggExprEmitter::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E){
  Visit(E->GetTemporaryExpr());
}

void AggExprEmitter::VisitOpaqueValueExpr(OpaqueValueExpr *e) {
  // If this is a unique OVE, just visit its source expression.
  if (e->isUnique())
    Visit(e->getSourceExpr());
  else
    EmitFinalDestCopy(e->getType(), CGF.getOrCreateOpaqueLValueMapping(e));
}

void
AggExprEmitter::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  if (Dest.isPotentiallyAliased() &&
      E->getType().isPODType(CGF.getContext())) {
    // For a POD type, just emit a load of the lvalue + a copy, because our
    // compound literal might alias the destination.
    EmitAggLoadOfLValue(E);
    return;
  }

  AggValueSlot Slot = EnsureSlot(E->getType());
  CGF.EmitAggExpr(E->getInitializer(), Slot);
}

/// Attempt to look through various unimportant expressions to find a
/// cast of the given kind.
static Expr *findPeephole(Expr *op, CastKind kind) {
  while (true) {
    op = op->IgnoreParens();
    if (CastExpr *castE = dyn_cast<CastExpr>(op)) {
      if (castE->getCastKind() == kind)
        return castE->getSubExpr();
      if (castE->getCastKind() == CK_NoOp)
        continue;
    }
    return nullptr;
  }
}

void AggExprEmitter::VisitCastExpr(CastExpr *E) {
  if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
    CGF.CGM.EmitExplicitCastExprType(ECE, &CGF);
  switch (E->getCastKind()) {
  case CK_Dynamic: {
    // FIXME: Can this actually happen? We have no test coverage for it.
    assert(isa<CXXDynamicCastExpr>(E) && "CK_Dynamic without a dynamic_cast?");
    LValue LV = CGF.EmitCheckedLValue(E->getSubExpr(),
                                      CodeGenFunction::TCK_Load);
    // FIXME: Do we also need to handle property references here?
    if (LV.isSimple())
      CGF.EmitDynamicCast(LV.getAddress(), cast<CXXDynamicCastExpr>(E));
    else
      CGF.CGM.ErrorUnsupported(E, "non-simple lvalue dynamic_cast");

    if (!Dest.isIgnored())
      CGF.CGM.ErrorUnsupported(E, "lvalue dynamic_cast with a destination");
    break;
  }

  case CK_ToUnion: {
    // Evaluate even if the destination is ignored.
    if (Dest.isIgnored()) {
      CGF.EmitAnyExpr(E->getSubExpr(), AggValueSlot::ignored(),
                      /*ignoreResult=*/true);
      break;
    }

    // GCC union extension
    QualType Ty = E->getSubExpr()->getType();
    Address CastPtr =
      Builder.CreateElementBitCast(Dest.getAddress(), CGF.ConvertType(Ty));
    EmitInitializationToLValue(E->getSubExpr(),
                               CGF.MakeAddrLValue(CastPtr, Ty));
    break;
  }

  case CK_DerivedToBase:
  case CK_BaseToDerived:
  case CK_UncheckedDerivedToBase: {
    llvm_unreachable("cannot perform hierarchy conversion in EmitAggExpr: "
                "should have been unpacked before we got here");
  }

  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic: {
    bool isToAtomic = (E->getCastKind() == CK_NonAtomicToAtomic);

    // Determine the atomic and value types.
    QualType atomicType = E->getSubExpr()->getType();
    QualType valueType = E->getType();
    if (isToAtomic) std::swap(atomicType, valueType);

    assert(atomicType->isAtomicType());
    assert(CGF.getContext().hasSameUnqualifiedType(valueType,
                          atomicType->castAs<AtomicType>()->getValueType()));

    // Just recurse normally if we're ignoring the result or the
    // atomic type doesn't change representation.
    if (Dest.isIgnored() || !CGF.CGM.isPaddedAtomicType(atomicType)) {
      return Visit(E->getSubExpr());
    }

    CastKind peepholeTarget =
      (isToAtomic ? CK_AtomicToNonAtomic : CK_NonAtomicToAtomic);

    // These two cases are reverses of each other; try to peephole them.
    if (Expr *op = findPeephole(E->getSubExpr(), peepholeTarget)) {
      assert(CGF.getContext().hasSameUnqualifiedType(op->getType(),
                                                     E->getType()) &&
           "peephole significantly changed types?");
      return Visit(op);
    }

    // If we're converting an r-value of non-atomic type to an r-value
    // of atomic type, just emit directly into the relevant sub-object.
    if (isToAtomic) {
      AggValueSlot valueDest = Dest;
      if (!valueDest.isIgnored() && CGF.CGM.isPaddedAtomicType(atomicType)) {
        // Zero-initialize.  (Strictly speaking, we only need to initialize
        // the padding at the end, but this is simpler.)
        if (!Dest.isZeroed())
          CGF.EmitNullInitialization(Dest.getAddress(), atomicType);

        // Build a GEP to refer to the subobject.
        Address valueAddr =
            CGF.Builder.CreateStructGEP(valueDest.getAddress(), 0,
                                        CharUnits());
        valueDest = AggValueSlot::forAddr(valueAddr,
                                          valueDest.getQualifiers(),
                                          valueDest.isExternallyDestructed(),
                                          valueDest.requiresGCollection(),
                                          valueDest.isPotentiallyAliased(),
                                          AggValueSlot::DoesNotOverlap,
                                          AggValueSlot::IsZeroed);
      }

      CGF.EmitAggExpr(E->getSubExpr(), valueDest);
      return;
    }

    // Otherwise, we're converting an atomic type to a non-atomic type.
    // Make an atomic temporary, emit into that, and then copy the value out.
    AggValueSlot atomicSlot =
      CGF.CreateAggTemp(atomicType, "atomic-to-nonatomic.temp");
    CGF.EmitAggExpr(E->getSubExpr(), atomicSlot);

    Address valueAddr =
      Builder.CreateStructGEP(atomicSlot.getAddress(), 0, CharUnits());
    RValue rvalue = RValue::getAggregate(valueAddr, atomicSlot.isVolatile());
    return EmitFinalDestCopy(valueType, rvalue);
  }

  case CK_LValueToRValue:
    // If we're loading from a volatile type, force the destination
    // into existence.
    if (E->getSubExpr()->getType().isVolatileQualified()) {
      EnsureDest(E->getType());
      return Visit(E->getSubExpr());
    }

    LLVM_FALLTHROUGH;

  case CK_NoOp:
  case CK_UserDefinedConversion:
  case CK_ConstructorConversion:
    assert(CGF.getContext().hasSameUnqualifiedType(E->getSubExpr()->getType(),
                                                   E->getType()) &&
           "Implicit cast types must be compatible");
    Visit(E->getSubExpr());
    break;

  case CK_LValueBitCast:
    llvm_unreachable("should not be emitting lvalue bitcast as rvalue");

  case CK_Dependent:
  case CK_BitCast:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_AddressSpaceConversion:
  case CK_IntToOCLSampler:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
    llvm_unreachable("cast kind invalid for aggregate types");
  }
}

void AggExprEmitter::VisitCallExpr(const CallExpr *E) {
  if (E->getCallReturnType(CGF.getContext())->isReferenceType()) {
    EmitAggLoadOfLValue(E);
    return;
  }

  withReturnValueSlot(E, [&](ReturnValueSlot Slot) {
    return CGF.EmitCallExpr(E, Slot);
  });
}

void AggExprEmitter::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  withReturnValueSlot(E, [&](ReturnValueSlot Slot) {
    return CGF.EmitObjCMessageExpr(E, Slot);
  });
}

void AggExprEmitter::VisitBinComma(const BinaryOperator *E) {
  CGF.EmitIgnoredExpr(E->getLHS());
  Visit(E->getRHS());
}

void AggExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  CodeGenFunction::StmtExprEvaluation eval(CGF);
  CGF.EmitCompoundStmt(*E->getSubStmt(), true, Dest);
}

enum CompareKind {
  CK_Less,
  CK_Greater,
  CK_Equal,
};

static llvm::Value *EmitCompare(CGBuilderTy &Builder, CodeGenFunction &CGF,
                                const BinaryOperator *E, llvm::Value *LHS,
                                llvm::Value *RHS, CompareKind Kind,
                                const char *NameSuffix = "") {
  QualType ArgTy = E->getLHS()->getType();
  if (const ComplexType *CT = ArgTy->getAs<ComplexType>())
    ArgTy = CT->getElementType();

  if (const auto *MPT = ArgTy->getAs<MemberPointerType>()) {
    assert(Kind == CK_Equal &&
           "member pointers may only be compared for equality");
    return CGF.CGM.getCXXABI().EmitMemberPointerComparison(
        CGF, LHS, RHS, MPT, /*IsInequality*/ false);
  }

  // Compute the comparison instructions for the specified comparison kind.
  struct CmpInstInfo {
    const char *Name;
    llvm::CmpInst::Predicate FCmp;
    llvm::CmpInst::Predicate SCmp;
    llvm::CmpInst::Predicate UCmp;
  };
  CmpInstInfo InstInfo = [&]() -> CmpInstInfo {
    using FI = llvm::FCmpInst;
    using II = llvm::ICmpInst;
    switch (Kind) {
    case CK_Less:
      return {"cmp.lt", FI::FCMP_OLT, II::ICMP_SLT, II::ICMP_ULT};
    case CK_Greater:
      return {"cmp.gt", FI::FCMP_OGT, II::ICMP_SGT, II::ICMP_UGT};
    case CK_Equal:
      return {"cmp.eq", FI::FCMP_OEQ, II::ICMP_EQ, II::ICMP_EQ};
    }
    llvm_unreachable("Unrecognised CompareKind enum");
  }();

  if (ArgTy->hasFloatingRepresentation())
    return Builder.CreateFCmp(InstInfo.FCmp, LHS, RHS,
                              llvm::Twine(InstInfo.Name) + NameSuffix);
  if (ArgTy->isIntegralOrEnumerationType() || ArgTy->isPointerType()) {
    auto Inst =
        ArgTy->hasSignedIntegerRepresentation() ? InstInfo.SCmp : InstInfo.UCmp;
    return Builder.CreateICmp(Inst, LHS, RHS,
                              llvm::Twine(InstInfo.Name) + NameSuffix);
  }

  llvm_unreachable("unsupported aggregate binary expression should have "
                   "already been handled");
}

void AggExprEmitter::VisitBinCmp(const BinaryOperator *E) {
  using llvm::BasicBlock;
  using llvm::PHINode;
  using llvm::Value;
  assert(CGF.getContext().hasSameType(E->getLHS()->getType(),
                                      E->getRHS()->getType()));
  const ComparisonCategoryInfo &CmpInfo =
      CGF.getContext().CompCategories.getInfoForType(E->getType());
  assert(CmpInfo.Record->isTriviallyCopyable() &&
         "cannot copy non-trivially copyable aggregate");

  QualType ArgTy = E->getLHS()->getType();

  // TODO: Handle comparing these types.
  if (ArgTy->isVectorType())
    return CGF.ErrorUnsupported(
        E, "aggregate three-way comparison with vector arguments");
  if (!ArgTy->isIntegralOrEnumerationType() && !ArgTy->isRealFloatingType() &&
      !ArgTy->isNullPtrType() && !ArgTy->isPointerType() &&
      !ArgTy->isMemberPointerType() && !ArgTy->isAnyComplexType()) {
    return CGF.ErrorUnsupported(E, "aggregate three-way comparison");
  }
  bool IsComplex = ArgTy->isAnyComplexType();

  // Evaluate the operands to the expression and extract their values.
  auto EmitOperand = [&](Expr *E) -> std::pair<Value *, Value *> {
    RValue RV = CGF.EmitAnyExpr(E);
    if (RV.isScalar())
      return {RV.getScalarVal(), nullptr};
    if (RV.isAggregate())
      return {RV.getAggregatePointer(), nullptr};
    assert(RV.isComplex());
    return RV.getComplexVal();
  };
  auto LHSValues = EmitOperand(E->getLHS()),
       RHSValues = EmitOperand(E->getRHS());

  auto EmitCmp = [&](CompareKind K) {
    Value *Cmp = EmitCompare(Builder, CGF, E, LHSValues.first, RHSValues.first,
                             K, IsComplex ? ".r" : "");
    if (!IsComplex)
      return Cmp;
    assert(K == CompareKind::CK_Equal);
    Value *CmpImag = EmitCompare(Builder, CGF, E, LHSValues.second,
                                 RHSValues.second, K, ".i");
    return Builder.CreateAnd(Cmp, CmpImag, "and.eq");
  };
  auto EmitCmpRes = [&](const ComparisonCategoryInfo::ValueInfo *VInfo) {
    return Builder.getInt(VInfo->getIntValue());
  };

  Value *Select;
  if (ArgTy->isNullPtrType()) {
    Select = EmitCmpRes(CmpInfo.getEqualOrEquiv());
  } else if (CmpInfo.isEquality()) {
    Select = Builder.CreateSelect(
        EmitCmp(CK_Equal), EmitCmpRes(CmpInfo.getEqualOrEquiv()),
        EmitCmpRes(CmpInfo.getNonequalOrNonequiv()), "sel.eq");
  } else if (!CmpInfo.isPartial()) {
    Value *SelectOne =
        Builder.CreateSelect(EmitCmp(CK_Less), EmitCmpRes(CmpInfo.getLess()),
                             EmitCmpRes(CmpInfo.getGreater()), "sel.lt");
    Select = Builder.CreateSelect(EmitCmp(CK_Equal),
                                  EmitCmpRes(CmpInfo.getEqualOrEquiv()),
                                  SelectOne, "sel.eq");
  } else {
    Value *SelectEq = Builder.CreateSelect(
        EmitCmp(CK_Equal), EmitCmpRes(CmpInfo.getEqualOrEquiv()),
        EmitCmpRes(CmpInfo.getUnordered()), "sel.eq");
    Value *SelectGT = Builder.CreateSelect(EmitCmp(CK_Greater),
                                           EmitCmpRes(CmpInfo.getGreater()),
                                           SelectEq, "sel.gt");
    Select = Builder.CreateSelect(
        EmitCmp(CK_Less), EmitCmpRes(CmpInfo.getLess()), SelectGT, "sel.lt");
  }
  // Create the return value in the destination slot.
  EnsureDest(E->getType());
  LValue DestLV = CGF.MakeAddrLValue(Dest.getAddress(), E->getType());

  // Emit the address of the first (and only) field in the comparison category
  // type, and initialize it from the constant integer value selected above.
  LValue FieldLV = CGF.EmitLValueForFieldInitialization(
      DestLV, *CmpInfo.Record->field_begin());
  CGF.EmitStoreThroughLValue(RValue::get(Select), FieldLV, /*IsInit*/ true);

  // All done! The result is in the Dest slot.
}

void AggExprEmitter::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->getOpcode() == BO_PtrMemD || E->getOpcode() == BO_PtrMemI)
    VisitPointerToDataMemberBinaryOperator(E);
  else
    CGF.ErrorUnsupported(E, "aggregate binary expression");
}

void AggExprEmitter::VisitPointerToDataMemberBinaryOperator(
                                                    const BinaryOperator *E) {
  LValue LV = CGF.EmitPointerToDataMemberBinaryExpr(E);
  EmitFinalDestCopy(E->getType(), LV);
}

/// Is the value of the given expression possibly a reference to or
/// into a __block variable?
static bool isBlockVarRef(const Expr *E) {
  // Make sure we look through parens.
  E = E->IgnoreParens();

  // Check for a direct reference to a __block variable.
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    const VarDecl *var = dyn_cast<VarDecl>(DRE->getDecl());
    return (var && var->hasAttr<BlocksAttr>());
  }

  // More complicated stuff.

  // Binary operators.
  if (const BinaryOperator *op = dyn_cast<BinaryOperator>(E)) {
    // For an assignment or pointer-to-member operation, just care
    // about the LHS.
    if (op->isAssignmentOp() || op->isPtrMemOp())
      return isBlockVarRef(op->getLHS());

    // For a comma, just care about the RHS.
    if (op->getOpcode() == BO_Comma)
      return isBlockVarRef(op->getRHS());

    // FIXME: pointer arithmetic?
    return false;

  // Check both sides of a conditional operator.
  } else if (const AbstractConditionalOperator *op
               = dyn_cast<AbstractConditionalOperator>(E)) {
    return isBlockVarRef(op->getTrueExpr())
        || isBlockVarRef(op->getFalseExpr());

  // OVEs are required to support BinaryConditionalOperators.
  } else if (const OpaqueValueExpr *op
               = dyn_cast<OpaqueValueExpr>(E)) {
    if (const Expr *src = op->getSourceExpr())
      return isBlockVarRef(src);

  // Casts are necessary to get things like (*(int*)&var) = foo().
  // We don't really care about the kind of cast here, except
  // we don't want to look through l2r casts, because it's okay
  // to get the *value* in a __block variable.
  } else if (const CastExpr *cast = dyn_cast<CastExpr>(E)) {
    if (cast->getCastKind() == CK_LValueToRValue)
      return false;
    return isBlockVarRef(cast->getSubExpr());

  // Handle unary operators.  Again, just aggressively look through
  // it, ignoring the operation.
  } else if (const UnaryOperator *uop = dyn_cast<UnaryOperator>(E)) {
    return isBlockVarRef(uop->getSubExpr());

  // Look into the base of a field access.
  } else if (const MemberExpr *mem = dyn_cast<MemberExpr>(E)) {
    return isBlockVarRef(mem->getBase());

  // Look into the base of a subscript.
  } else if (const ArraySubscriptExpr *sub = dyn_cast<ArraySubscriptExpr>(E)) {
    return isBlockVarRef(sub->getBase());
  }

  return false;
}

void AggExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  // For an assignment to work, the value on the right has
  // to be compatible with the value on the left.
  assert(CGF.getContext().hasSameUnqualifiedType(E->getLHS()->getType(),
                                                 E->getRHS()->getType())
         && "Invalid assignment");

  // If the LHS might be a __block variable, and the RHS can
  // potentially cause a block copy, we need to evaluate the RHS first
  // so that the assignment goes the right place.
  // This is pretty semantically fragile.
  if (isBlockVarRef(E->getLHS()) &&
      E->getRHS()->HasSideEffects(CGF.getContext())) {
    // Ensure that we have a destination, and evaluate the RHS into that.
    EnsureDest(E->getRHS()->getType());
    Visit(E->getRHS());

    // Now emit the LHS and copy into it.
    LValue LHS = CGF.EmitCheckedLValue(E->getLHS(), CodeGenFunction::TCK_Store);

    // That copy is an atomic copy if the LHS is atomic.
    if (LHS.getType()->isAtomicType() ||
        CGF.LValueIsSuitableForInlineAtomic(LHS)) {
      CGF.EmitAtomicStore(Dest.asRValue(), LHS, /*isInit*/ false);
      return;
    }

    EmitCopy(E->getLHS()->getType(),
             AggValueSlot::forLValue(LHS, AggValueSlot::IsDestructed,
                                     needsGC(E->getLHS()->getType()),
                                     AggValueSlot::IsAliased,
                                     AggValueSlot::MayOverlap),
             Dest);
    return;
  }

  LValue LHS = CGF.EmitLValue(E->getLHS());

  // If we have an atomic type, evaluate into the destination and then
  // do an atomic copy.
  if (LHS.getType()->isAtomicType() ||
      CGF.LValueIsSuitableForInlineAtomic(LHS)) {
    EnsureDest(E->getRHS()->getType());
    Visit(E->getRHS());
    CGF.EmitAtomicStore(Dest.asRValue(), LHS, /*isInit*/ false);
    return;
  }

  // Codegen the RHS so that it stores directly into the LHS.
  AggValueSlot LHSSlot =
    AggValueSlot::forLValue(LHS, AggValueSlot::IsDestructed,
                            needsGC(E->getLHS()->getType()),
                            AggValueSlot::IsAliased,
                            AggValueSlot::MayOverlap);
  // A non-volatile aggregate destination might have volatile member.
  if (!LHSSlot.isVolatile() &&
      CGF.hasVolatileMember(E->getLHS()->getType()))
    LHSSlot.setVolatile(true);

  CGF.EmitAggExpr(E->getRHS(), LHSSlot);

  // Copy into the destination if the assignment isn't ignored.
  EmitFinalDestCopy(E->getType(), LHS);
}

void AggExprEmitter::
VisitAbstractConditionalOperator(const AbstractConditionalOperator *E) {
  llvm::BasicBlock *LHSBlock = CGF.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("cond.end");

  // Bind the common expression if necessary.
  CodeGenFunction::OpaqueValueMapping binding(CGF, E);

  CodeGenFunction::ConditionalEvaluation eval(CGF);
  CGF.EmitBranchOnBoolExpr(E->getCond(), LHSBlock, RHSBlock,
                           CGF.getProfileCount(E));

  // Save whether the destination's lifetime is externally managed.
  bool isExternallyDestructed = Dest.isExternallyDestructed();

  eval.begin(CGF);
  CGF.EmitBlock(LHSBlock);
  CGF.incrementProfileCounter(E);
  Visit(E->getTrueExpr());
  eval.end(CGF);

  assert(CGF.HaveInsertPoint() && "expression evaluation ended with no IP!");
  CGF.Builder.CreateBr(ContBlock);

  // If the result of an agg expression is unused, then the emission
  // of the LHS might need to create a destination slot.  That's fine
  // with us, and we can safely emit the RHS into the same slot, but
  // we shouldn't claim that it's already being destructed.
  Dest.setExternallyDestructed(isExternallyDestructed);

  eval.begin(CGF);
  CGF.EmitBlock(RHSBlock);
  Visit(E->getFalseExpr());
  eval.end(CGF);

  CGF.EmitBlock(ContBlock);
}

void AggExprEmitter::VisitChooseExpr(const ChooseExpr *CE) {
  Visit(CE->getChosenSubExpr());
}

void AggExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  Address ArgValue = Address::invalid();
  Address ArgPtr = CGF.EmitVAArg(VE, ArgValue);

  // If EmitVAArg fails, emit an error.
  if (!ArgPtr.isValid()) {
    CGF.ErrorUnsupported(VE, "aggregate va_arg expression");
    return;
  }

  EmitFinalDestCopy(VE->getType(), CGF.MakeAddrLValue(ArgPtr, VE->getType()));
}

void AggExprEmitter::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  // Ensure that we have a slot, but if we already do, remember
  // whether it was externally destructed.
  bool wasExternallyDestructed = Dest.isExternallyDestructed();
  EnsureDest(E->getType());

  // We're going to push a destructor if there isn't already one.
  Dest.setExternallyDestructed();

  Visit(E->getSubExpr());

  // Push that destructor we promised.
  if (!wasExternallyDestructed)
    CGF.EmitCXXTemporary(E->getTemporary(), E->getType(), Dest.getAddress());
}

void
AggExprEmitter::VisitCXXConstructExpr(const CXXConstructExpr *E) {
  AggValueSlot Slot = EnsureSlot(E->getType());
  CGF.EmitCXXConstructExpr(E, Slot);
}

void AggExprEmitter::VisitCXXInheritedCtorInitExpr(
    const CXXInheritedCtorInitExpr *E) {
  AggValueSlot Slot = EnsureSlot(E->getType());
  CGF.EmitInheritedCXXConstructorCall(
      E->getConstructor(), E->constructsVBase(), Slot.getAddress(),
      E->inheritedFromVBase(), E);
}

void
AggExprEmitter::VisitLambdaExpr(LambdaExpr *E) {
  AggValueSlot Slot = EnsureSlot(E->getType());
  CGF.EmitLambdaExpr(E, Slot);
}

void AggExprEmitter::VisitExprWithCleanups(ExprWithCleanups *E) {
  CGF.enterFullExpression(E);
  CodeGenFunction::RunCleanupsScope cleanups(CGF);
  Visit(E->getSubExpr());
}

void AggExprEmitter::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
  QualType T = E->getType();
  AggValueSlot Slot = EnsureSlot(T);
  EmitNullInitializationToLValue(CGF.MakeAddrLValue(Slot.getAddress(), T));
}

void AggExprEmitter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  QualType T = E->getType();
  AggValueSlot Slot = EnsureSlot(T);
  EmitNullInitializationToLValue(CGF.MakeAddrLValue(Slot.getAddress(), T));
}

/// isSimpleZero - If emitting this value will obviously just cause a store of
/// zero to memory, return true.  This can return false if uncertain, so it just
/// handles simple cases.
static bool isSimpleZero(const Expr *E, CodeGenFunction &CGF) {
  E = E->IgnoreParens();

  // 0
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue() == 0;
  // +0.0
  if (const FloatingLiteral *FL = dyn_cast<FloatingLiteral>(E))
    return FL->getValue().isPosZero();
  // int()
  if ((isa<ImplicitValueInitExpr>(E) || isa<CXXScalarValueInitExpr>(E)) &&
      CGF.getTypes().isZeroInitializable(E->getType()))
    return true;
  // (int*)0 - Null pointer expressions.
  if (const CastExpr *ICE = dyn_cast<CastExpr>(E))
    return ICE->getCastKind() == CK_NullToPointer &&
        CGF.getTypes().isPointerZeroInitializable(E->getType());
  // '\0'
  if (const CharacterLiteral *CL = dyn_cast<CharacterLiteral>(E))
    return CL->getValue() == 0;

  // Otherwise, hard case: conservatively return false.
  return false;
}


void
AggExprEmitter::EmitInitializationToLValue(Expr *E, LValue LV) {
  QualType type = LV.getType();
  // FIXME: Ignore result?
  // FIXME: Are initializers affected by volatile?
  if (Dest.isZeroed() && isSimpleZero(E, CGF)) {
    // Storing "i32 0" to a zero'd memory location is a noop.
    return;
  } else if (isa<ImplicitValueInitExpr>(E) || isa<CXXScalarValueInitExpr>(E)) {
    return EmitNullInitializationToLValue(LV);
  } else if (isa<NoInitExpr>(E)) {
    // Do nothing.
    return;
  } else if (type->isReferenceType()) {
    RValue RV = CGF.EmitReferenceBindingToExpr(E);
    return CGF.EmitStoreThroughLValue(RV, LV);
  }

  switch (CGF.getEvaluationKind(type)) {
  case TEK_Complex:
    CGF.EmitComplexExprIntoLValue(E, LV, /*isInit*/ true);
    return;
  case TEK_Aggregate:
    CGF.EmitAggExpr(E, AggValueSlot::forLValue(LV,
                                               AggValueSlot::IsDestructed,
                                      AggValueSlot::DoesNotNeedGCBarriers,
                                               AggValueSlot::IsNotAliased,
                                               AggValueSlot::MayOverlap,
                                               Dest.isZeroed()));
    return;
  case TEK_Scalar:
    if (LV.isSimple()) {
      CGF.EmitScalarInit(E, /*D=*/nullptr, LV, /*Captured=*/false);
    } else {
      CGF.EmitStoreThroughLValue(RValue::get(CGF.EmitScalarExpr(E)), LV);
    }
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

void AggExprEmitter::EmitNullInitializationToLValue(LValue lv) {
  QualType type = lv.getType();

  // If the destination slot is already zeroed out before the aggregate is
  // copied into it, we don't have to emit any zeros here.
  if (Dest.isZeroed() && CGF.getTypes().isZeroInitializable(type))
    return;

  if (CGF.hasScalarEvaluationKind(type)) {
    // For non-aggregates, we can store the appropriate null constant.
    llvm::Value *null = CGF.CGM.EmitNullConstant(type);
    // Note that the following is not equivalent to
    // EmitStoreThroughBitfieldLValue for ARC types.
    if (lv.isBitField()) {
      CGF.EmitStoreThroughBitfieldLValue(RValue::get(null), lv);
    } else {
      assert(lv.isSimple());
      CGF.EmitStoreOfScalar(null, lv, /* isInitialization */ true);
    }
  } else {
    // There's a potential optimization opportunity in combining
    // memsets; that would be easy for arrays, but relatively
    // difficult for structures with the current code.
    CGF.EmitNullInitialization(lv.getAddress(), lv.getType());
  }
}

void AggExprEmitter::VisitInitListExpr(InitListExpr *E) {
#if 0
  // FIXME: Assess perf here?  Figure out what cases are worth optimizing here
  // (Length of globals? Chunks of zeroed-out space?).
  //
  // If we can, prefer a copy from a global; this is a lot less code for long
  // globals, and it's easier for the current optimizers to analyze.
  if (llvm::Constant* C = CGF.CGM.EmitConstantExpr(E, E->getType(), &CGF)) {
    llvm::GlobalVariable* GV =
    new llvm::GlobalVariable(CGF.CGM.getModule(), C->getType(), true,
                             llvm::GlobalValue::InternalLinkage, C, "");
    EmitFinalDestCopy(E->getType(), CGF.MakeAddrLValue(GV, E->getType()));
    return;
  }
#endif
  if (E->hadArrayRangeDesignator())
    CGF.ErrorUnsupported(E, "GNU array range designator extension");

  if (E->isTransparent())
    return Visit(E->getInit(0));

  AggValueSlot Dest = EnsureSlot(E->getType());

  LValue DestLV = CGF.MakeAddrLValue(Dest.getAddress(), E->getType());

  // Handle initialization of an array.
  if (E->getType()->isArrayType()) {
    auto AType = cast<llvm::ArrayType>(Dest.getAddress().getElementType());
    EmitArrayInit(Dest.getAddress(), AType, E->getType(), E);
    return;
  }

  assert(E->getType()->isRecordType() && "Only support structs/unions here!");

  // Do struct initialization; this code just sets each individual member
  // to the approprate value.  This makes bitfield support automatic;
  // the disadvantage is that the generated code is more difficult for
  // the optimizer, especially with bitfields.
  unsigned NumInitElements = E->getNumInits();
  RecordDecl *record = E->getType()->castAs<RecordType>()->getDecl();

  // We'll need to enter cleanup scopes in case any of the element
  // initializers throws an exception.
  SmallVector<EHScopeStack::stable_iterator, 16> cleanups;
  llvm::Instruction *cleanupDominator = nullptr;

  unsigned curInitIndex = 0;

  // Emit initialization of base classes.
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(record)) {
    assert(E->getNumInits() >= CXXRD->getNumBases() &&
           "missing initializer for base class");
    for (auto &Base : CXXRD->bases()) {
      assert(!Base.isVirtual() && "should not see vbases here");
      auto *BaseRD = Base.getType()->getAsCXXRecordDecl();
      Address V = CGF.GetAddressOfDirectBaseInCompleteClass(
          Dest.getAddress(), CXXRD, BaseRD,
          /*isBaseVirtual*/ false);
      AggValueSlot AggSlot = AggValueSlot::forAddr(
          V, Qualifiers(),
          AggValueSlot::IsDestructed,
          AggValueSlot::DoesNotNeedGCBarriers,
          AggValueSlot::IsNotAliased,
          CGF.overlapForBaseInit(CXXRD, BaseRD, Base.isVirtual()));
      CGF.EmitAggExpr(E->getInit(curInitIndex++), AggSlot);

      if (QualType::DestructionKind dtorKind =
              Base.getType().isDestructedType()) {
        CGF.pushDestroy(dtorKind, V, Base.getType());
        cleanups.push_back(CGF.EHStack.stable_begin());
      }
    }
  }

  // Prepare a 'this' for CXXDefaultInitExprs.
  CodeGenFunction::FieldConstructionScope FCS(CGF, Dest.getAddress());

  if (record->isUnion()) {
    // Only initialize one field of a union. The field itself is
    // specified by the initializer list.
    if (!E->getInitializedFieldInUnion()) {
      // Empty union; we have nothing to do.

#ifndef NDEBUG
      // Make sure that it's really an empty and not a failure of
      // semantic analysis.
      for (const auto *Field : record->fields())
        assert(Field->isUnnamedBitfield() && "Only unnamed bitfields allowed");
#endif
      return;
    }

    // FIXME: volatility
    FieldDecl *Field = E->getInitializedFieldInUnion();

    LValue FieldLoc = CGF.EmitLValueForFieldInitialization(DestLV, Field);
    if (NumInitElements) {
      // Store the initializer into the field
      EmitInitializationToLValue(E->getInit(0), FieldLoc);
    } else {
      // Default-initialize to null.
      EmitNullInitializationToLValue(FieldLoc);
    }

    return;
  }

  // Here we iterate over the fields; this makes it simpler to both
  // default-initialize fields and skip over unnamed fields.
  for (const auto *field : record->fields()) {
    // We're done once we hit the flexible array member.
    if (field->getType()->isIncompleteArrayType())
      break;

    // Always skip anonymous bitfields.
    if (field->isUnnamedBitfield())
      continue;

    // We're done if we reach the end of the explicit initializers, we
    // have a zeroed object, and the rest of the fields are
    // zero-initializable.
    if (curInitIndex == NumInitElements && Dest.isZeroed() &&
        CGF.getTypes().isZeroInitializable(E->getType()))
      break;


    LValue LV = CGF.EmitLValueForFieldInitialization(DestLV, field);
    // We never generate write-barries for initialized fields.
    LV.setNonGC(true);

    if (curInitIndex < NumInitElements) {
      // Store the initializer into the field.
      EmitInitializationToLValue(E->getInit(curInitIndex++), LV);
    } else {
      // We're out of initializers; default-initialize to null
      EmitNullInitializationToLValue(LV);
    }

    // Push a destructor if necessary.
    // FIXME: if we have an array of structures, all explicitly
    // initialized, we can end up pushing a linear number of cleanups.
    bool pushedCleanup = false;
    if (QualType::DestructionKind dtorKind
          = field->getType().isDestructedType()) {
      assert(LV.isSimple());
      if (CGF.needsEHCleanup(dtorKind)) {
        if (!cleanupDominator)
          cleanupDominator = CGF.Builder.CreateAlignedLoad(
              CGF.Int8Ty,
              llvm::Constant::getNullValue(CGF.Int8PtrTy),
              CharUnits::One()); // placeholder

        CGF.pushDestroy(EHCleanup, LV.getAddress(), field->getType(),
                        CGF.getDestroyer(dtorKind), false);
        cleanups.push_back(CGF.EHStack.stable_begin());
        pushedCleanup = true;
      }
    }

    // If the GEP didn't get used because of a dead zero init or something
    // else, clean it up for -O0 builds and general tidiness.
    if (!pushedCleanup && LV.isSimple())
      if (llvm::GetElementPtrInst *GEP =
            dyn_cast<llvm::GetElementPtrInst>(LV.getPointer()))
        if (GEP->use_empty())
          GEP->eraseFromParent();
  }

  // Deactivate all the partial cleanups in reverse order, which
  // generally means popping them.
  for (unsigned i = cleanups.size(); i != 0; --i)
    CGF.DeactivateCleanupBlock(cleanups[i-1], cleanupDominator);

  // Destroy the placeholder if we made one.
  if (cleanupDominator)
    cleanupDominator->eraseFromParent();
}

void AggExprEmitter::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E,
                                            llvm::Value *outerBegin) {
  // Emit the common subexpression.
  CodeGenFunction::OpaqueValueMapping binding(CGF, E->getCommonExpr());

  Address destPtr = EnsureSlot(E->getType()).getAddress();
  uint64_t numElements = E->getArraySize().getZExtValue();

  if (!numElements)
    return;

  // destPtr is an array*. Construct an elementType* by drilling down a level.
  llvm::Value *zero = llvm::ConstantInt::get(CGF.SizeTy, 0);
  llvm::Value *indices[] = {zero, zero};
  llvm::Value *begin = Builder.CreateInBoundsGEP(destPtr.getPointer(), indices,
                                                 "arrayinit.begin");

  // Prepare to special-case multidimensional array initialization: we avoid
  // emitting multiple destructor loops in that case.
  if (!outerBegin)
    outerBegin = begin;
  ArrayInitLoopExpr *InnerLoop = dyn_cast<ArrayInitLoopExpr>(E->getSubExpr());

  QualType elementType =
      CGF.getContext().getAsArrayType(E->getType())->getElementType();
  CharUnits elementSize = CGF.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
      destPtr.getAlignment().alignmentOfArrayElement(elementSize);

  llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
  llvm::BasicBlock *bodyBB = CGF.createBasicBlock("arrayinit.body");

  // Jump into the body.
  CGF.EmitBlock(bodyBB);
  llvm::PHINode *index =
      Builder.CreatePHI(zero->getType(), 2, "arrayinit.index");
  index->addIncoming(zero, entryBB);
  llvm::Value *element = Builder.CreateInBoundsGEP(begin, index);

  // Prepare for a cleanup.
  QualType::DestructionKind dtorKind = elementType.isDestructedType();
  EHScopeStack::stable_iterator cleanup;
  if (CGF.needsEHCleanup(dtorKind) && !InnerLoop) {
    if (outerBegin->getType() != element->getType())
      outerBegin = Builder.CreateBitCast(outerBegin, element->getType());
    CGF.pushRegularPartialArrayCleanup(outerBegin, element, elementType,
                                       elementAlign,
                                       CGF.getDestroyer(dtorKind));
    cleanup = CGF.EHStack.stable_begin();
  } else {
    dtorKind = QualType::DK_none;
  }

  // Emit the actual filler expression.
  {
    // Temporaries created in an array initialization loop are destroyed
    // at the end of each iteration.
    CodeGenFunction::RunCleanupsScope CleanupsScope(CGF);
    CodeGenFunction::ArrayInitLoopExprScope Scope(CGF, index);
    LValue elementLV =
        CGF.MakeAddrLValue(Address(element, elementAlign), elementType);

    if (InnerLoop) {
      // If the subexpression is an ArrayInitLoopExpr, share its cleanup.
      auto elementSlot = AggValueSlot::forLValue(
          elementLV, AggValueSlot::IsDestructed,
          AggValueSlot::DoesNotNeedGCBarriers,
          AggValueSlot::IsNotAliased,
          AggValueSlot::DoesNotOverlap);
      AggExprEmitter(CGF, elementSlot, false)
          .VisitArrayInitLoopExpr(InnerLoop, outerBegin);
    } else
      EmitInitializationToLValue(E->getSubExpr(), elementLV);
  }

  // Move on to the next element.
  llvm::Value *nextIndex = Builder.CreateNUWAdd(
      index, llvm::ConstantInt::get(CGF.SizeTy, 1), "arrayinit.next");
  index->addIncoming(nextIndex, Builder.GetInsertBlock());

  // Leave the loop if we're done.
  llvm::Value *done = Builder.CreateICmpEQ(
      nextIndex, llvm::ConstantInt::get(CGF.SizeTy, numElements),
      "arrayinit.done");
  llvm::BasicBlock *endBB = CGF.createBasicBlock("arrayinit.end");
  Builder.CreateCondBr(done, endBB, bodyBB);

  CGF.EmitBlock(endBB);

  // Leave the partial-array cleanup if we entered one.
  if (dtorKind)
    CGF.DeactivateCleanupBlock(cleanup, index);
}

void AggExprEmitter::VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E) {
  AggValueSlot Dest = EnsureSlot(E->getType());

  LValue DestLV = CGF.MakeAddrLValue(Dest.getAddress(), E->getType());
  EmitInitializationToLValue(E->getBase(), DestLV);
  VisitInitListExpr(E->getUpdater());
}

//===----------------------------------------------------------------------===//
//                        Entry Points into this File
//===----------------------------------------------------------------------===//

/// GetNumNonZeroBytesInInit - Get an approximate count of the number of
/// non-zero bytes that will be stored when outputting the initializer for the
/// specified initializer expression.
static CharUnits GetNumNonZeroBytesInInit(const Expr *E, CodeGenFunction &CGF) {
  E = E->IgnoreParens();

  // 0 and 0.0 won't require any non-zero stores!
  if (isSimpleZero(E, CGF)) return CharUnits::Zero();

  // If this is an initlist expr, sum up the size of sizes of the (present)
  // elements.  If this is something weird, assume the whole thing is non-zero.
  const InitListExpr *ILE = dyn_cast<InitListExpr>(E);
  while (ILE && ILE->isTransparent())
    ILE = dyn_cast<InitListExpr>(ILE->getInit(0));
  if (!ILE || !CGF.getTypes().isZeroInitializable(ILE->getType()))
    return CGF.getContext().getTypeSizeInChars(E->getType());

  // InitListExprs for structs have to be handled carefully.  If there are
  // reference members, we need to consider the size of the reference, not the
  // referencee.  InitListExprs for unions and arrays can't have references.
  if (const RecordType *RT = E->getType()->getAs<RecordType>()) {
    if (!RT->isUnionType()) {
      RecordDecl *SD = E->getType()->getAs<RecordType>()->getDecl();
      CharUnits NumNonZeroBytes = CharUnits::Zero();

      unsigned ILEElement = 0;
      if (auto *CXXRD = dyn_cast<CXXRecordDecl>(SD))
        while (ILEElement != CXXRD->getNumBases())
          NumNonZeroBytes +=
              GetNumNonZeroBytesInInit(ILE->getInit(ILEElement++), CGF);
      for (const auto *Field : SD->fields()) {
        // We're done once we hit the flexible array member or run out of
        // InitListExpr elements.
        if (Field->getType()->isIncompleteArrayType() ||
            ILEElement == ILE->getNumInits())
          break;
        if (Field->isUnnamedBitfield())
          continue;

        const Expr *E = ILE->getInit(ILEElement++);

        // Reference values are always non-null and have the width of a pointer.
        if (Field->getType()->isReferenceType())
          NumNonZeroBytes += CGF.getContext().toCharUnitsFromBits(
              CGF.getTarget().getPointerWidth(0));
        else
          NumNonZeroBytes += GetNumNonZeroBytesInInit(E, CGF);
      }

      return NumNonZeroBytes;
    }
  }


  CharUnits NumNonZeroBytes = CharUnits::Zero();
  for (unsigned i = 0, e = ILE->getNumInits(); i != e; ++i)
    NumNonZeroBytes += GetNumNonZeroBytesInInit(ILE->getInit(i), CGF);
  return NumNonZeroBytes;
}

/// CheckAggExprForMemSetUse - If the initializer is large and has a lot of
/// zeros in it, emit a memset and avoid storing the individual zeros.
///
static void CheckAggExprForMemSetUse(AggValueSlot &Slot, const Expr *E,
                                     CodeGenFunction &CGF) {
  // If the slot is already known to be zeroed, nothing to do.  Don't mess with
  // volatile stores.
  if (Slot.isZeroed() || Slot.isVolatile() || !Slot.getAddress().isValid())
    return;

  // C++ objects with a user-declared constructor don't need zero'ing.
  if (CGF.getLangOpts().CPlusPlus)
    if (const RecordType *RT = CGF.getContext()
                       .getBaseElementType(E->getType())->getAs<RecordType>()) {
      const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
      if (RD->hasUserDeclaredConstructor())
        return;
    }

  // If the type is 16-bytes or smaller, prefer individual stores over memset.
  CharUnits Size = Slot.getPreferredSize(CGF.getContext(), E->getType());
  if (Size <= CharUnits::fromQuantity(16))
    return;

  // Check to see if over 3/4 of the initializer are known to be zero.  If so,
  // we prefer to emit memset + individual stores for the rest.
  CharUnits NumNonZeroBytes = GetNumNonZeroBytesInInit(E, CGF);
  if (NumNonZeroBytes*4 > Size)
    return;

  // Okay, it seems like a good idea to use an initial memset, emit the call.
  llvm::Constant *SizeVal = CGF.Builder.getInt64(Size.getQuantity());

  Address Loc = Slot.getAddress();
  Loc = CGF.Builder.CreateElementBitCast(Loc, CGF.Int8Ty);
  CGF.Builder.CreateMemSet(Loc, CGF.Builder.getInt8(0), SizeVal, false);

  // Tell the AggExprEmitter that the slot is known zero.
  Slot.setZeroed();
}




/// EmitAggExpr - Emit the computation of the specified expression of aggregate
/// type.  The result is computed into DestPtr.  Note that if DestPtr is null,
/// the value of the aggregate expression is not needed.  If VolatileDest is
/// true, DestPtr cannot be 0.
void CodeGenFunction::EmitAggExpr(const Expr *E, AggValueSlot Slot) {
  assert(E && hasAggregateEvaluationKind(E->getType()) &&
         "Invalid aggregate expression to emit");
  assert((Slot.getAddress().isValid() || Slot.isIgnored()) &&
         "slot has bits but no address");

  // Optimize the slot if possible.
  CheckAggExprForMemSetUse(Slot, E, *this);

  AggExprEmitter(*this, Slot, Slot.isIgnored()).Visit(const_cast<Expr*>(E));
}

LValue CodeGenFunction::EmitAggExprToLValue(const Expr *E) {
  assert(hasAggregateEvaluationKind(E->getType()) && "Invalid argument!");
  Address Temp = CreateMemTemp(E->getType());
  LValue LV = MakeAddrLValue(Temp, E->getType());
  EmitAggExpr(E, AggValueSlot::forLValue(LV, AggValueSlot::IsNotDestructed,
                                         AggValueSlot::DoesNotNeedGCBarriers,
                                         AggValueSlot::IsNotAliased,
                                         AggValueSlot::DoesNotOverlap));
  return LV;
}

AggValueSlot::Overlap_t CodeGenFunction::overlapForBaseInit(
    const CXXRecordDecl *RD, const CXXRecordDecl *BaseRD, bool IsVirtual) {
  // Virtual bases are initialized first, in address order, so there's never
  // any overlap during their initialization.
  //
  // FIXME: Under P0840, this is no longer true: the tail padding of a vbase
  // of a field could be reused by a vbase of a containing class.
  if (IsVirtual)
    return AggValueSlot::DoesNotOverlap;

  // If the base class is laid out entirely within the nvsize of the derived
  // class, its tail padding cannot yet be initialized, so we can issue
  // stores at the full width of the base class.
  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
  if (Layout.getBaseClassOffset(BaseRD) +
          getContext().getASTRecordLayout(BaseRD).getSize() <=
      Layout.getNonVirtualSize())
    return AggValueSlot::DoesNotOverlap;

  // The tail padding may contain values we need to preserve.
  return AggValueSlot::MayOverlap;
}

void CodeGenFunction::EmitAggregateCopy(LValue Dest, LValue Src, QualType Ty,
                                        AggValueSlot::Overlap_t MayOverlap,
                                        bool isVolatile) {
  assert(!Ty->isAnyComplexType() && "Shouldn't happen for complex");

  Address DestPtr = Dest.getAddress();
  Address SrcPtr = Src.getAddress();

  if (getLangOpts().CPlusPlus) {
    if (const RecordType *RT = Ty->getAs<RecordType>()) {
      CXXRecordDecl *Record = cast<CXXRecordDecl>(RT->getDecl());
      assert((Record->hasTrivialCopyConstructor() ||
              Record->hasTrivialCopyAssignment() ||
              Record->hasTrivialMoveConstructor() ||
              Record->hasTrivialMoveAssignment() ||
              Record->isUnion()) &&
             "Trying to aggregate-copy a type without a trivial copy/move "
             "constructor or assignment operator");
      // Ignore empty classes in C++.
      if (Record->isEmpty())
        return;
    }
  }

  // Aggregate assignment turns into llvm.memcpy.  This is almost valid per
  // C99 6.5.16.1p3, which states "If the value being stored in an object is
  // read from another object that overlaps in anyway the storage of the first
  // object, then the overlap shall be exact and the two objects shall have
  // qualified or unqualified versions of a compatible type."
  //
  // memcpy is not defined if the source and destination pointers are exactly
  // equal, but other compilers do this optimization, and almost every memcpy
  // implementation handles this case safely.  If there is a libc that does not
  // safely handle this, we can add a target hook.

  // Get data size info for this aggregate. Don't copy the tail padding if this
  // might be a potentially-overlapping subobject, since the tail padding might
  // be occupied by a different object. Otherwise, copying it is fine.
  std::pair<CharUnits, CharUnits> TypeInfo;
  if (MayOverlap)
    TypeInfo = getContext().getTypeInfoDataSizeInChars(Ty);
  else
    TypeInfo = getContext().getTypeInfoInChars(Ty);

  llvm::Value *SizeVal = nullptr;
  if (TypeInfo.first.isZero()) {
    // But note that getTypeInfo returns 0 for a VLA.
    if (auto *VAT = dyn_cast_or_null<VariableArrayType>(
            getContext().getAsArrayType(Ty))) {
      QualType BaseEltTy;
      SizeVal = emitArrayLength(VAT, BaseEltTy, DestPtr);
      TypeInfo = getContext().getTypeInfoInChars(BaseEltTy);
      assert(!TypeInfo.first.isZero());
      SizeVal = Builder.CreateNUWMul(
          SizeVal,
          llvm::ConstantInt::get(SizeTy, TypeInfo.first.getQuantity()));
    }
  }
  if (!SizeVal) {
    SizeVal = llvm::ConstantInt::get(SizeTy, TypeInfo.first.getQuantity());
  }

  // FIXME: If we have a volatile struct, the optimizer can remove what might
  // appear to be `extra' memory ops:
  //
  // volatile struct { int i; } a, b;
  //
  // int main() {
  //   a = b;
  //   a = b;
  // }
  //
  // we need to use a different call here.  We use isVolatile to indicate when
  // either the source or the destination is volatile.

  DestPtr = Builder.CreateElementBitCast(DestPtr, Int8Ty);
  SrcPtr = Builder.CreateElementBitCast(SrcPtr, Int8Ty);

  // Don't do any of the memmove_collectable tests if GC isn't set.
  if (CGM.getLangOpts().getGC() == LangOptions::NonGC) {
    // fall through
  } else if (const RecordType *RecordTy = Ty->getAs<RecordType>()) {
    RecordDecl *Record = RecordTy->getDecl();
    if (Record->hasObjectMember()) {
      CGM.getObjCRuntime().EmitGCMemmoveCollectable(*this, DestPtr, SrcPtr,
                                                    SizeVal);
      return;
    }
  } else if (Ty->isArrayType()) {
    QualType BaseType = getContext().getBaseElementType(Ty);
    if (const RecordType *RecordTy = BaseType->getAs<RecordType>()) {
      if (RecordTy->getDecl()->hasObjectMember()) {
        CGM.getObjCRuntime().EmitGCMemmoveCollectable(*this, DestPtr, SrcPtr,
                                                      SizeVal);
        return;
      }
    }
  }

  auto Inst = Builder.CreateMemCpy(DestPtr, SrcPtr, SizeVal, isVolatile);

  // Determine the metadata to describe the position of any padding in this
  // memcpy, as well as the TBAA tags for the members of the struct, in case
  // the optimizer wishes to expand it in to scalar memory operations.
  if (llvm::MDNode *TBAAStructTag = CGM.getTBAAStructInfo(Ty))
    Inst->setMetadata(llvm::LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (CGM.getCodeGenOpts().NewStructPathTBAA) {
    TBAAAccessInfo TBAAInfo = CGM.mergeTBAAInfoForMemoryTransfer(
        Dest.getTBAAInfo(), Src.getTBAAInfo());
    CGM.DecorateInstructionWithTBAA(Inst, TBAAInfo);
  }
}
