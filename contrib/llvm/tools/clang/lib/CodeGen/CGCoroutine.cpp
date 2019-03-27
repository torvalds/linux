//===----- CGCoroutine.cpp - Emit LLVM Code for C++ coroutines ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of coroutines.
//
//===----------------------------------------------------------------------===//

#include "CGCleanup.h"
#include "CodeGenFunction.h"
#include "llvm/ADT/ScopeExit.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtVisitor.h"

using namespace clang;
using namespace CodeGen;

using llvm::Value;
using llvm::BasicBlock;

namespace {
enum class AwaitKind { Init, Normal, Yield, Final };
static constexpr llvm::StringLiteral AwaitKindStr[] = {"init", "await", "yield",
                                                       "final"};
}

struct clang::CodeGen::CGCoroData {
  // What is the current await expression kind and how many
  // await/yield expressions were encountered so far.
  // These are used to generate pretty labels for await expressions in LLVM IR.
  AwaitKind CurrentAwaitKind = AwaitKind::Init;
  unsigned AwaitNum = 0;
  unsigned YieldNum = 0;

  // How many co_return statements are in the coroutine. Used to decide whether
  // we need to add co_return; equivalent at the end of the user authored body.
  unsigned CoreturnCount = 0;

  // A branch to this block is emitted when coroutine needs to suspend.
  llvm::BasicBlock *SuspendBB = nullptr;

  // The promise type's 'unhandled_exception' handler, if it defines one.
  Stmt *ExceptionHandler = nullptr;

  // A temporary i1 alloca that stores whether 'await_resume' threw an
  // exception. If it did, 'true' is stored in this variable, and the coroutine
  // body must be skipped. If the promise type does not define an exception
  // handler, this is null.
  llvm::Value *ResumeEHVar = nullptr;

  // Stores the jump destination just before the coroutine memory is freed.
  // This is the destination that every suspend point jumps to for the cleanup
  // branch.
  CodeGenFunction::JumpDest CleanupJD;

  // Stores the jump destination just before the final suspend. The co_return
  // statements jumps to this point after calling return_xxx promise member.
  CodeGenFunction::JumpDest FinalJD;

  // Stores the llvm.coro.id emitted in the function so that we can supply it
  // as the first argument to coro.begin, coro.alloc and coro.free intrinsics.
  // Note: llvm.coro.id returns a token that cannot be directly expressed in a
  // builtin.
  llvm::CallInst *CoroId = nullptr;

  // Stores the llvm.coro.begin emitted in the function so that we can replace
  // all coro.frame intrinsics with direct SSA value of coro.begin that returns
  // the address of the coroutine frame of the current coroutine.
  llvm::CallInst *CoroBegin = nullptr;

  // Stores the last emitted coro.free for the deallocate expressions, we use it
  // to wrap dealloc code with if(auto mem = coro.free) dealloc(mem).
  llvm::CallInst *LastCoroFree = nullptr;

  // If coro.id came from the builtin, remember the expression to give better
  // diagnostic. If CoroIdExpr is nullptr, the coro.id was created by
  // EmitCoroutineBody.
  CallExpr const *CoroIdExpr = nullptr;
};

// Defining these here allows to keep CGCoroData private to this file.
clang::CodeGen::CodeGenFunction::CGCoroInfo::CGCoroInfo() {}
CodeGenFunction::CGCoroInfo::~CGCoroInfo() {}

static void createCoroData(CodeGenFunction &CGF,
                           CodeGenFunction::CGCoroInfo &CurCoro,
                           llvm::CallInst *CoroId,
                           CallExpr const *CoroIdExpr = nullptr) {
  if (CurCoro.Data) {
    if (CurCoro.Data->CoroIdExpr)
      CGF.CGM.Error(CoroIdExpr->getBeginLoc(),
                    "only one __builtin_coro_id can be used in a function");
    else if (CoroIdExpr)
      CGF.CGM.Error(CoroIdExpr->getBeginLoc(),
                    "__builtin_coro_id shall not be used in a C++ coroutine");
    else
      llvm_unreachable("EmitCoroutineBodyStatement called twice?");

    return;
  }

  CurCoro.Data = std::unique_ptr<CGCoroData>(new CGCoroData);
  CurCoro.Data->CoroId = CoroId;
  CurCoro.Data->CoroIdExpr = CoroIdExpr;
}

// Synthesize a pretty name for a suspend point.
static SmallString<32> buildSuspendPrefixStr(CGCoroData &Coro, AwaitKind Kind) {
  unsigned No = 0;
  switch (Kind) {
  case AwaitKind::Init:
  case AwaitKind::Final:
    break;
  case AwaitKind::Normal:
    No = ++Coro.AwaitNum;
    break;
  case AwaitKind::Yield:
    No = ++Coro.YieldNum;
    break;
  }
  SmallString<32> Prefix(AwaitKindStr[static_cast<unsigned>(Kind)]);
  if (No > 1) {
    Twine(No).toVector(Prefix);
  }
  return Prefix;
}

static bool memberCallExpressionCanThrow(const Expr *E) {
  if (const auto *CE = dyn_cast<CXXMemberCallExpr>(E))
    if (const auto *Proto =
            CE->getMethodDecl()->getType()->getAs<FunctionProtoType>())
      if (isNoexceptExceptionSpec(Proto->getExceptionSpecType()) &&
          Proto->canThrow() == CT_Cannot)
        return false;
  return true;
}

// Emit suspend expression which roughly looks like:
//
//   auto && x = CommonExpr();
//   if (!x.await_ready()) {
//      llvm_coro_save();
//      x.await_suspend(...);     (*)
//      llvm_coro_suspend(); (**)
//   }
//   x.await_resume();
//
// where the result of the entire expression is the result of x.await_resume()
//
//   (*) If x.await_suspend return type is bool, it allows to veto a suspend:
//      if (x.await_suspend(...))
//        llvm_coro_suspend();
//
//  (**) llvm_coro_suspend() encodes three possible continuations as
//       a switch instruction:
//
//  %where-to = call i8 @llvm.coro.suspend(...)
//  switch i8 %where-to, label %coro.ret [ ; jump to epilogue to suspend
//    i8 0, label %yield.ready   ; go here when resumed
//    i8 1, label %yield.cleanup ; go here when destroyed
//  ]
//
//  See llvm's docs/Coroutines.rst for more details.
//
namespace {
  struct LValueOrRValue {
    LValue LV;
    RValue RV;
  };
}
static LValueOrRValue emitSuspendExpression(CodeGenFunction &CGF, CGCoroData &Coro,
                                    CoroutineSuspendExpr const &S,
                                    AwaitKind Kind, AggValueSlot aggSlot,
                                    bool ignoreResult, bool forLValue) {
  auto *E = S.getCommonExpr();

  auto Binder =
      CodeGenFunction::OpaqueValueMappingData::bind(CGF, S.getOpaqueValue(), E);
  auto UnbindOnExit = llvm::make_scope_exit([&] { Binder.unbind(CGF); });

  auto Prefix = buildSuspendPrefixStr(Coro, Kind);
  BasicBlock *ReadyBlock = CGF.createBasicBlock(Prefix + Twine(".ready"));
  BasicBlock *SuspendBlock = CGF.createBasicBlock(Prefix + Twine(".suspend"));
  BasicBlock *CleanupBlock = CGF.createBasicBlock(Prefix + Twine(".cleanup"));

  // If expression is ready, no need to suspend.
  CGF.EmitBranchOnBoolExpr(S.getReadyExpr(), ReadyBlock, SuspendBlock, 0);

  // Otherwise, emit suspend logic.
  CGF.EmitBlock(SuspendBlock);

  auto &Builder = CGF.Builder;
  llvm::Function *CoroSave = CGF.CGM.getIntrinsic(llvm::Intrinsic::coro_save);
  auto *NullPtr = llvm::ConstantPointerNull::get(CGF.CGM.Int8PtrTy);
  auto *SaveCall = Builder.CreateCall(CoroSave, {NullPtr});

  auto *SuspendRet = CGF.EmitScalarExpr(S.getSuspendExpr());
  if (SuspendRet != nullptr && SuspendRet->getType()->isIntegerTy(1)) {
    // Veto suspension if requested by bool returning await_suspend.
    BasicBlock *RealSuspendBlock =
        CGF.createBasicBlock(Prefix + Twine(".suspend.bool"));
    CGF.Builder.CreateCondBr(SuspendRet, RealSuspendBlock, ReadyBlock);
    SuspendBlock = RealSuspendBlock;
    CGF.EmitBlock(RealSuspendBlock);
  }

  // Emit the suspend point.
  const bool IsFinalSuspend = (Kind == AwaitKind::Final);
  llvm::Function *CoroSuspend =
      CGF.CGM.getIntrinsic(llvm::Intrinsic::coro_suspend);
  auto *SuspendResult = Builder.CreateCall(
      CoroSuspend, {SaveCall, Builder.getInt1(IsFinalSuspend)});

  // Create a switch capturing three possible continuations.
  auto *Switch = Builder.CreateSwitch(SuspendResult, Coro.SuspendBB, 2);
  Switch->addCase(Builder.getInt8(0), ReadyBlock);
  Switch->addCase(Builder.getInt8(1), CleanupBlock);

  // Emit cleanup for this suspend point.
  CGF.EmitBlock(CleanupBlock);
  CGF.EmitBranchThroughCleanup(Coro.CleanupJD);

  // Emit await_resume expression.
  CGF.EmitBlock(ReadyBlock);

  // Exception handling requires additional IR. If the 'await_resume' function
  // is marked as 'noexcept', we avoid generating this additional IR.
  CXXTryStmt *TryStmt = nullptr;
  if (Coro.ExceptionHandler && Kind == AwaitKind::Init &&
      memberCallExpressionCanThrow(S.getResumeExpr())) {
    Coro.ResumeEHVar =
        CGF.CreateTempAlloca(Builder.getInt1Ty(), Prefix + Twine("resume.eh"));
    Builder.CreateFlagStore(true, Coro.ResumeEHVar);

    auto Loc = S.getResumeExpr()->getExprLoc();
    auto *Catch = new (CGF.getContext())
        CXXCatchStmt(Loc, /*exDecl=*/nullptr, Coro.ExceptionHandler);
    auto *TryBody =
        CompoundStmt::Create(CGF.getContext(), S.getResumeExpr(), Loc, Loc);
    TryStmt = CXXTryStmt::Create(CGF.getContext(), Loc, TryBody, Catch);
    CGF.EnterCXXTryStmt(*TryStmt);
  }

  LValueOrRValue Res;
  if (forLValue)
    Res.LV = CGF.EmitLValue(S.getResumeExpr());
  else
    Res.RV = CGF.EmitAnyExpr(S.getResumeExpr(), aggSlot, ignoreResult);

  if (TryStmt) {
    Builder.CreateFlagStore(false, Coro.ResumeEHVar);
    CGF.ExitCXXTryStmt(*TryStmt);
  }

  return Res;
}

RValue CodeGenFunction::EmitCoawaitExpr(const CoawaitExpr &E,
                                        AggValueSlot aggSlot,
                                        bool ignoreResult) {
  return emitSuspendExpression(*this, *CurCoro.Data, E,
                               CurCoro.Data->CurrentAwaitKind, aggSlot,
                               ignoreResult, /*forLValue*/false).RV;
}
RValue CodeGenFunction::EmitCoyieldExpr(const CoyieldExpr &E,
                                        AggValueSlot aggSlot,
                                        bool ignoreResult) {
  return emitSuspendExpression(*this, *CurCoro.Data, E, AwaitKind::Yield,
                               aggSlot, ignoreResult, /*forLValue*/false).RV;
}

void CodeGenFunction::EmitCoreturnStmt(CoreturnStmt const &S) {
  ++CurCoro.Data->CoreturnCount;
  const Expr *RV = S.getOperand();
  if (RV && RV->getType()->isVoidType()) {
    // Make sure to evaluate the expression of a co_return with a void
    // expression for side effects.
    RunCleanupsScope cleanupScope(*this);
    EmitIgnoredExpr(RV);
  }
  EmitStmt(S.getPromiseCall());
  EmitBranchThroughCleanup(CurCoro.Data->FinalJD);
}


#ifndef NDEBUG
static QualType getCoroutineSuspendExprReturnType(const ASTContext &Ctx,
  const CoroutineSuspendExpr *E) {
  const auto *RE = E->getResumeExpr();
  // Is it possible for RE to be a CXXBindTemporaryExpr wrapping
  // a MemberCallExpr?
  assert(isa<CallExpr>(RE) && "unexpected suspend expression type");
  return cast<CallExpr>(RE)->getCallReturnType(Ctx);
}
#endif

LValue
CodeGenFunction::EmitCoawaitLValue(const CoawaitExpr *E) {
  assert(getCoroutineSuspendExprReturnType(getContext(), E)->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");
  return emitSuspendExpression(*this, *CurCoro.Data, *E,
                               CurCoro.Data->CurrentAwaitKind, AggValueSlot::ignored(),
                               /*ignoreResult*/false, /*forLValue*/true).LV;
}

LValue
CodeGenFunction::EmitCoyieldLValue(const CoyieldExpr *E) {
  assert(getCoroutineSuspendExprReturnType(getContext(), E)->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");
  return emitSuspendExpression(*this, *CurCoro.Data, *E,
                               AwaitKind::Yield, AggValueSlot::ignored(),
                               /*ignoreResult*/false, /*forLValue*/true).LV;
}

// Hunts for the parameter reference in the parameter copy/move declaration.
namespace {
struct GetParamRef : public StmtVisitor<GetParamRef> {
public:
  DeclRefExpr *Expr = nullptr;
  GetParamRef() {}
  void VisitDeclRefExpr(DeclRefExpr *E) {
    assert(Expr == nullptr && "multilple declref in param move");
    Expr = E;
  }
  void VisitStmt(Stmt *S) {
    for (auto *C : S->children()) {
      if (C)
        Visit(C);
    }
  }
};
}

// This class replaces references to parameters to their copies by changing
// the addresses in CGF.LocalDeclMap and restoring back the original values in
// its destructor.

namespace {
  struct ParamReferenceReplacerRAII {
    CodeGenFunction::DeclMapTy SavedLocals;
    CodeGenFunction::DeclMapTy& LocalDeclMap;

    ParamReferenceReplacerRAII(CodeGenFunction::DeclMapTy &LocalDeclMap)
        : LocalDeclMap(LocalDeclMap) {}

    void addCopy(DeclStmt const *PM) {
      // Figure out what param it refers to.

      assert(PM->isSingleDecl());
      VarDecl const*VD = static_cast<VarDecl const*>(PM->getSingleDecl());
      Expr const *InitExpr = VD->getInit();
      GetParamRef Visitor;
      Visitor.Visit(const_cast<Expr*>(InitExpr));
      assert(Visitor.Expr);
      DeclRefExpr *DREOrig = Visitor.Expr;
      auto *PD = DREOrig->getDecl();

      auto it = LocalDeclMap.find(PD);
      assert(it != LocalDeclMap.end() && "parameter is not found");
      SavedLocals.insert({ PD, it->second });

      auto copyIt = LocalDeclMap.find(VD);
      assert(copyIt != LocalDeclMap.end() && "parameter copy is not found");
      it->second = copyIt->getSecond();
    }

    ~ParamReferenceReplacerRAII() {
      for (auto&& SavedLocal : SavedLocals) {
        LocalDeclMap.insert({SavedLocal.first, SavedLocal.second});
      }
    }
  };
}

// For WinEH exception representation backend needs to know what funclet coro.end
// belongs to. That information is passed in a funclet bundle.
static SmallVector<llvm::OperandBundleDef, 1>
getBundlesForCoroEnd(CodeGenFunction &CGF) {
  SmallVector<llvm::OperandBundleDef, 1> BundleList;

  if (llvm::Instruction *EHPad = CGF.CurrentFuncletPad)
    BundleList.emplace_back("funclet", EHPad);

  return BundleList;
}

namespace {
// We will insert coro.end to cut any of the destructors for objects that
// do not need to be destroyed once the coroutine is resumed.
// See llvm/docs/Coroutines.rst for more details about coro.end.
struct CallCoroEnd final : public EHScopeStack::Cleanup {
  void Emit(CodeGenFunction &CGF, Flags flags) override {
    auto &CGM = CGF.CGM;
    auto *NullPtr = llvm::ConstantPointerNull::get(CGF.Int8PtrTy);
    llvm::Function *CoroEndFn = CGM.getIntrinsic(llvm::Intrinsic::coro_end);
    // See if we have a funclet bundle to associate coro.end with. (WinEH)
    auto Bundles = getBundlesForCoroEnd(CGF);
    auto *CoroEnd = CGF.Builder.CreateCall(
        CoroEndFn, {NullPtr, CGF.Builder.getTrue()}, Bundles);
    if (Bundles.empty()) {
      // Otherwise, (landingpad model), create a conditional branch that leads
      // either to a cleanup block or a block with EH resume instruction.
      auto *ResumeBB = CGF.getEHResumeBlock(/*cleanup=*/true);
      auto *CleanupContBB = CGF.createBasicBlock("cleanup.cont");
      CGF.Builder.CreateCondBr(CoroEnd, ResumeBB, CleanupContBB);
      CGF.EmitBlock(CleanupContBB);
    }
  }
};
}

namespace {
// Make sure to call coro.delete on scope exit.
struct CallCoroDelete final : public EHScopeStack::Cleanup {
  Stmt *Deallocate;

  // Emit "if (coro.free(CoroId, CoroBegin)) Deallocate;"

  // Note: That deallocation will be emitted twice: once for a normal exit and
  // once for exceptional exit. This usage is safe because Deallocate does not
  // contain any declarations. The SubStmtBuilder::makeNewAndDeleteExpr()
  // builds a single call to a deallocation function which is safe to emit
  // multiple times.
  void Emit(CodeGenFunction &CGF, Flags) override {
    // Remember the current point, as we are going to emit deallocation code
    // first to get to coro.free instruction that is an argument to a delete
    // call.
    BasicBlock *SaveInsertBlock = CGF.Builder.GetInsertBlock();

    auto *FreeBB = CGF.createBasicBlock("coro.free");
    CGF.EmitBlock(FreeBB);
    CGF.EmitStmt(Deallocate);

    auto *AfterFreeBB = CGF.createBasicBlock("after.coro.free");
    CGF.EmitBlock(AfterFreeBB);

    // We should have captured coro.free from the emission of deallocate.
    auto *CoroFree = CGF.CurCoro.Data->LastCoroFree;
    if (!CoroFree) {
      CGF.CGM.Error(Deallocate->getBeginLoc(),
                    "Deallocation expressoin does not refer to coro.free");
      return;
    }

    // Get back to the block we were originally and move coro.free there.
    auto *InsertPt = SaveInsertBlock->getTerminator();
    CoroFree->moveBefore(InsertPt);
    CGF.Builder.SetInsertPoint(InsertPt);

    // Add if (auto *mem = coro.free) Deallocate;
    auto *NullPtr = llvm::ConstantPointerNull::get(CGF.Int8PtrTy);
    auto *Cond = CGF.Builder.CreateICmpNE(CoroFree, NullPtr);
    CGF.Builder.CreateCondBr(Cond, FreeBB, AfterFreeBB);

    // No longer need old terminator.
    InsertPt->eraseFromParent();
    CGF.Builder.SetInsertPoint(AfterFreeBB);
  }
  explicit CallCoroDelete(Stmt *DeallocStmt) : Deallocate(DeallocStmt) {}
};
}

namespace {
struct GetReturnObjectManager {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  const CoroutineBodyStmt &S;

  Address GroActiveFlag;
  CodeGenFunction::AutoVarEmission GroEmission;

  GetReturnObjectManager(CodeGenFunction &CGF, const CoroutineBodyStmt &S)
      : CGF(CGF), Builder(CGF.Builder), S(S), GroActiveFlag(Address::invalid()),
        GroEmission(CodeGenFunction::AutoVarEmission::invalid()) {}

  // The gro variable has to outlive coroutine frame and coroutine promise, but,
  // it can only be initialized after coroutine promise was created, thus, we
  // split its emission in two parts. EmitGroAlloca emits an alloca and sets up
  // cleanups. Later when coroutine promise is available we initialize the gro
  // and sets the flag that the cleanup is now active.

  void EmitGroAlloca() {
    auto *GroDeclStmt = dyn_cast<DeclStmt>(S.getResultDecl());
    if (!GroDeclStmt) {
      // If get_return_object returns void, no need to do an alloca.
      return;
    }

    auto *GroVarDecl = cast<VarDecl>(GroDeclStmt->getSingleDecl());

    // Set GRO flag that it is not initialized yet
    GroActiveFlag =
      CGF.CreateTempAlloca(Builder.getInt1Ty(), CharUnits::One(), "gro.active");
    Builder.CreateStore(Builder.getFalse(), GroActiveFlag);

    GroEmission = CGF.EmitAutoVarAlloca(*GroVarDecl);

    // Remember the top of EHStack before emitting the cleanup.
    auto old_top = CGF.EHStack.stable_begin();
    CGF.EmitAutoVarCleanups(GroEmission);
    auto top = CGF.EHStack.stable_begin();

    // Make the cleanup conditional on gro.active
    for (auto b = CGF.EHStack.find(top), e = CGF.EHStack.find(old_top);
      b != e; b++) {
      if (auto *Cleanup = dyn_cast<EHCleanupScope>(&*b)) {
        assert(!Cleanup->hasActiveFlag() && "cleanup already has active flag?");
        Cleanup->setActiveFlag(GroActiveFlag);
        Cleanup->setTestFlagInEHCleanup();
        Cleanup->setTestFlagInNormalCleanup();
      }
    }
  }

  void EmitGroInit() {
    if (!GroActiveFlag.isValid()) {
      // No Gro variable was allocated. Simply emit the call to
      // get_return_object.
      CGF.EmitStmt(S.getResultDecl());
      return;
    }

    CGF.EmitAutoVarInit(GroEmission);
    Builder.CreateStore(Builder.getTrue(), GroActiveFlag);
  }
};
}

static void emitBodyAndFallthrough(CodeGenFunction &CGF,
                                   const CoroutineBodyStmt &S, Stmt *Body) {
  CGF.EmitStmt(Body);
  const bool CanFallthrough = CGF.Builder.GetInsertBlock();
  if (CanFallthrough)
    if (Stmt *OnFallthrough = S.getFallthroughHandler())
      CGF.EmitStmt(OnFallthrough);
}

void CodeGenFunction::EmitCoroutineBody(const CoroutineBodyStmt &S) {
  auto *NullPtr = llvm::ConstantPointerNull::get(Builder.getInt8PtrTy());
  auto &TI = CGM.getContext().getTargetInfo();
  unsigned NewAlign = TI.getNewAlign() / TI.getCharWidth();

  auto *EntryBB = Builder.GetInsertBlock();
  auto *AllocBB = createBasicBlock("coro.alloc");
  auto *InitBB = createBasicBlock("coro.init");
  auto *FinalBB = createBasicBlock("coro.final");
  auto *RetBB = createBasicBlock("coro.ret");

  auto *CoroId = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::coro_id),
      {Builder.getInt32(NewAlign), NullPtr, NullPtr, NullPtr});
  createCoroData(*this, CurCoro, CoroId);
  CurCoro.Data->SuspendBB = RetBB;

  // Backend is allowed to elide memory allocations, to help it, emit
  // auto mem = coro.alloc() ? 0 : ... allocation code ...;
  auto *CoroAlloc = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::coro_alloc), {CoroId});

  Builder.CreateCondBr(CoroAlloc, AllocBB, InitBB);

  EmitBlock(AllocBB);
  auto *AllocateCall = EmitScalarExpr(S.getAllocate());
  auto *AllocOrInvokeContBB = Builder.GetInsertBlock();

  // Handle allocation failure if 'ReturnStmtOnAllocFailure' was provided.
  if (auto *RetOnAllocFailure = S.getReturnStmtOnAllocFailure()) {
    auto *RetOnFailureBB = createBasicBlock("coro.ret.on.failure");

    // See if allocation was successful.
    auto *NullPtr = llvm::ConstantPointerNull::get(Int8PtrTy);
    auto *Cond = Builder.CreateICmpNE(AllocateCall, NullPtr);
    Builder.CreateCondBr(Cond, InitBB, RetOnFailureBB);

    // If not, return OnAllocFailure object.
    EmitBlock(RetOnFailureBB);
    EmitStmt(RetOnAllocFailure);
  }
  else {
    Builder.CreateBr(InitBB);
  }

  EmitBlock(InitBB);

  // Pass the result of the allocation to coro.begin.
  auto *Phi = Builder.CreatePHI(VoidPtrTy, 2);
  Phi->addIncoming(NullPtr, EntryBB);
  Phi->addIncoming(AllocateCall, AllocOrInvokeContBB);
  auto *CoroBegin = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::coro_begin), {CoroId, Phi});
  CurCoro.Data->CoroBegin = CoroBegin;

  GetReturnObjectManager GroManager(*this, S);
  GroManager.EmitGroAlloca();

  CurCoro.Data->CleanupJD = getJumpDestInCurrentScope(RetBB);
  {
    ParamReferenceReplacerRAII ParamReplacer(LocalDeclMap);
    CodeGenFunction::RunCleanupsScope ResumeScope(*this);
    EHStack.pushCleanup<CallCoroDelete>(NormalAndEHCleanup, S.getDeallocate());

    // Create parameter copies. We do it before creating a promise, since an
    // evolution of coroutine TS may allow promise constructor to observe
    // parameter copies.
    for (auto *PM : S.getParamMoves()) {
      EmitStmt(PM);
      ParamReplacer.addCopy(cast<DeclStmt>(PM));
      // TODO: if(CoroParam(...)) need to surround ctor and dtor
      // for the copy, so that llvm can elide it if the copy is
      // not needed.
    }

    EmitStmt(S.getPromiseDeclStmt());

    Address PromiseAddr = GetAddrOfLocalVar(S.getPromiseDecl());
    auto *PromiseAddrVoidPtr =
        new llvm::BitCastInst(PromiseAddr.getPointer(), VoidPtrTy, "", CoroId);
    // Update CoroId to refer to the promise. We could not do it earlier because
    // promise local variable was not emitted yet.
    CoroId->setArgOperand(1, PromiseAddrVoidPtr);

    // Now we have the promise, initialize the GRO
    GroManager.EmitGroInit();

    EHStack.pushCleanup<CallCoroEnd>(EHCleanup);

    CurCoro.Data->CurrentAwaitKind = AwaitKind::Init;
    CurCoro.Data->ExceptionHandler = S.getExceptionHandler();
    EmitStmt(S.getInitSuspendStmt());
    CurCoro.Data->FinalJD = getJumpDestInCurrentScope(FinalBB);

    CurCoro.Data->CurrentAwaitKind = AwaitKind::Normal;

    if (CurCoro.Data->ExceptionHandler) {
      // If we generated IR to record whether an exception was thrown from
      // 'await_resume', then use that IR to determine whether the coroutine
      // body should be skipped.
      // If we didn't generate the IR (perhaps because 'await_resume' was marked
      // as 'noexcept'), then we skip this check.
      BasicBlock *ContBB = nullptr;
      if (CurCoro.Data->ResumeEHVar) {
        BasicBlock *BodyBB = createBasicBlock("coro.resumed.body");
        ContBB = createBasicBlock("coro.resumed.cont");
        Value *SkipBody = Builder.CreateFlagLoad(CurCoro.Data->ResumeEHVar,
                                                 "coro.resumed.eh");
        Builder.CreateCondBr(SkipBody, ContBB, BodyBB);
        EmitBlock(BodyBB);
      }

      auto Loc = S.getBeginLoc();
      CXXCatchStmt Catch(Loc, /*exDecl=*/nullptr,
                         CurCoro.Data->ExceptionHandler);
      auto *TryStmt =
          CXXTryStmt::Create(getContext(), Loc, S.getBody(), &Catch);

      EnterCXXTryStmt(*TryStmt);
      emitBodyAndFallthrough(*this, S, TryStmt->getTryBlock());
      ExitCXXTryStmt(*TryStmt);

      if (ContBB)
        EmitBlock(ContBB);
    }
    else {
      emitBodyAndFallthrough(*this, S, S.getBody());
    }

    // See if we need to generate final suspend.
    const bool CanFallthrough = Builder.GetInsertBlock();
    const bool HasCoreturns = CurCoro.Data->CoreturnCount > 0;
    if (CanFallthrough || HasCoreturns) {
      EmitBlock(FinalBB);
      CurCoro.Data->CurrentAwaitKind = AwaitKind::Final;
      EmitStmt(S.getFinalSuspendStmt());
    } else {
      // We don't need FinalBB. Emit it to make sure the block is deleted.
      EmitBlock(FinalBB, /*IsFinished=*/true);
    }
  }

  EmitBlock(RetBB);
  // Emit coro.end before getReturnStmt (and parameter destructors), since
  // resume and destroy parts of the coroutine should not include them.
  llvm::Function *CoroEnd = CGM.getIntrinsic(llvm::Intrinsic::coro_end);
  Builder.CreateCall(CoroEnd, {NullPtr, Builder.getFalse()});

  if (Stmt *Ret = S.getReturnStmt())
    EmitStmt(Ret);
}

// Emit coroutine intrinsic and patch up arguments of the token type.
RValue CodeGenFunction::EmitCoroutineIntrinsic(const CallExpr *E,
                                               unsigned int IID) {
  SmallVector<llvm::Value *, 8> Args;
  switch (IID) {
  default:
    break;
  // The coro.frame builtin is replaced with an SSA value of the coro.begin
  // intrinsic.
  case llvm::Intrinsic::coro_frame: {
    if (CurCoro.Data && CurCoro.Data->CoroBegin) {
      return RValue::get(CurCoro.Data->CoroBegin);
    }
    CGM.Error(E->getBeginLoc(), "this builtin expect that __builtin_coro_begin "
                                "has been used earlier in this function");
    auto NullPtr = llvm::ConstantPointerNull::get(Builder.getInt8PtrTy());
    return RValue::get(NullPtr);
  }
  // The following three intrinsics take a token parameter referring to a token
  // returned by earlier call to @llvm.coro.id. Since we cannot represent it in
  // builtins, we patch it up here.
  case llvm::Intrinsic::coro_alloc:
  case llvm::Intrinsic::coro_begin:
  case llvm::Intrinsic::coro_free: {
    if (CurCoro.Data && CurCoro.Data->CoroId) {
      Args.push_back(CurCoro.Data->CoroId);
      break;
    }
    CGM.Error(E->getBeginLoc(), "this builtin expect that __builtin_coro_id has"
                                " been used earlier in this function");
    // Fallthrough to the next case to add TokenNone as the first argument.
    LLVM_FALLTHROUGH;
  }
  // @llvm.coro.suspend takes a token parameter. Add token 'none' as the first
  // argument.
  case llvm::Intrinsic::coro_suspend:
    Args.push_back(llvm::ConstantTokenNone::get(getLLVMContext()));
    break;
  }
  for (auto &Arg : E->arguments())
    Args.push_back(EmitScalarExpr(Arg));

  llvm::Value *F = CGM.getIntrinsic(IID);
  llvm::CallInst *Call = Builder.CreateCall(F, Args);

  // Note: The following code is to enable to emit coro.id and coro.begin by
  // hand to experiment with coroutines in C.
  // If we see @llvm.coro.id remember it in the CoroData. We will update
  // coro.alloc, coro.begin and coro.free intrinsics to refer to it.
  if (IID == llvm::Intrinsic::coro_id) {
    createCoroData(*this, CurCoro, Call, E);
  }
  else if (IID == llvm::Intrinsic::coro_begin) {
    if (CurCoro.Data)
      CurCoro.Data->CoroBegin = Call;
  }
  else if (IID == llvm::Intrinsic::coro_free) {
    // Remember the last coro_free as we need it to build the conditional
    // deletion of the coroutine frame.
    if (CurCoro.Data)
      CurCoro.Data->LastCoroFree = Call;
  }
  return RValue::get(Call);
}
