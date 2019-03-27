//===--- CGVTables.cpp - Emit LLVM Code for C++ vtables -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of virtual tables.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <cstdio>

using namespace clang;
using namespace CodeGen;

CodeGenVTables::CodeGenVTables(CodeGenModule &CGM)
    : CGM(CGM), VTContext(CGM.getContext().getVTableContext()) {}

llvm::Constant *CodeGenModule::GetAddrOfThunk(StringRef Name, llvm::Type *FnTy,
                                              GlobalDecl GD) {
  return GetOrCreateLLVMFunction(Name, FnTy, GD, /*ForVTable=*/true,
                                 /*DontDefer=*/true, /*IsThunk=*/true);
}

static void setThunkProperties(CodeGenModule &CGM, const ThunkInfo &Thunk,
                               llvm::Function *ThunkFn, bool ForVTable,
                               GlobalDecl GD) {
  CGM.setFunctionLinkage(GD, ThunkFn);
  CGM.getCXXABI().setThunkLinkage(ThunkFn, ForVTable, GD,
                                  !Thunk.Return.isEmpty());

  // Set the right visibility.
  CGM.setGVProperties(ThunkFn, GD);

  if (!CGM.getCXXABI().exportThunk()) {
    ThunkFn->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
    ThunkFn->setDSOLocal(true);
  }

  if (CGM.supportsCOMDAT() && ThunkFn->isWeakForLinker())
    ThunkFn->setComdat(CGM.getModule().getOrInsertComdat(ThunkFn->getName()));
}

#ifndef NDEBUG
static bool similar(const ABIArgInfo &infoL, CanQualType typeL,
                    const ABIArgInfo &infoR, CanQualType typeR) {
  return (infoL.getKind() == infoR.getKind() &&
          (typeL == typeR ||
           (isa<PointerType>(typeL) && isa<PointerType>(typeR)) ||
           (isa<ReferenceType>(typeL) && isa<ReferenceType>(typeR))));
}
#endif

static RValue PerformReturnAdjustment(CodeGenFunction &CGF,
                                      QualType ResultType, RValue RV,
                                      const ThunkInfo &Thunk) {
  // Emit the return adjustment.
  bool NullCheckValue = !ResultType->isReferenceType();

  llvm::BasicBlock *AdjustNull = nullptr;
  llvm::BasicBlock *AdjustNotNull = nullptr;
  llvm::BasicBlock *AdjustEnd = nullptr;

  llvm::Value *ReturnValue = RV.getScalarVal();

  if (NullCheckValue) {
    AdjustNull = CGF.createBasicBlock("adjust.null");
    AdjustNotNull = CGF.createBasicBlock("adjust.notnull");
    AdjustEnd = CGF.createBasicBlock("adjust.end");

    llvm::Value *IsNull = CGF.Builder.CreateIsNull(ReturnValue);
    CGF.Builder.CreateCondBr(IsNull, AdjustNull, AdjustNotNull);
    CGF.EmitBlock(AdjustNotNull);
  }

  auto ClassDecl = ResultType->getPointeeType()->getAsCXXRecordDecl();
  auto ClassAlign = CGF.CGM.getClassPointerAlignment(ClassDecl);
  ReturnValue = CGF.CGM.getCXXABI().performReturnAdjustment(CGF,
                                            Address(ReturnValue, ClassAlign),
                                            Thunk.Return);

  if (NullCheckValue) {
    CGF.Builder.CreateBr(AdjustEnd);
    CGF.EmitBlock(AdjustNull);
    CGF.Builder.CreateBr(AdjustEnd);
    CGF.EmitBlock(AdjustEnd);

    llvm::PHINode *PHI = CGF.Builder.CreatePHI(ReturnValue->getType(), 2);
    PHI->addIncoming(ReturnValue, AdjustNotNull);
    PHI->addIncoming(llvm::Constant::getNullValue(ReturnValue->getType()),
                     AdjustNull);
    ReturnValue = PHI;
  }

  return RValue::get(ReturnValue);
}

/// This function clones a function's DISubprogram node and enters it into
/// a value map with the intent that the map can be utilized by the cloner
/// to short-circuit Metadata node mapping.
/// Furthermore, the function resolves any DILocalVariable nodes referenced
/// by dbg.value intrinsics so they can be properly mapped during cloning.
static void resolveTopLevelMetadata(llvm::Function *Fn,
                                    llvm::ValueToValueMapTy &VMap) {
  // Clone the DISubprogram node and put it into the Value map.
  auto *DIS = Fn->getSubprogram();
  if (!DIS)
    return;
  auto *NewDIS = DIS->replaceWithDistinct(DIS->clone());
  VMap.MD()[DIS].reset(NewDIS);

  // Find all llvm.dbg.declare intrinsics and resolve the DILocalVariable nodes
  // they are referencing.
  for (auto &BB : Fn->getBasicBlockList()) {
    for (auto &I : BB) {
      if (auto *DII = dyn_cast<llvm::DbgVariableIntrinsic>(&I)) {
        auto *DILocal = DII->getVariable();
        if (!DILocal->isResolved())
          DILocal->resolve();
      }
    }
  }
}

// This function does roughly the same thing as GenerateThunk, but in a
// very different way, so that va_start and va_end work correctly.
// FIXME: This function assumes "this" is the first non-sret LLVM argument of
//        a function, and that there is an alloca built in the entry block
//        for all accesses to "this".
// FIXME: This function assumes there is only one "ret" statement per function.
// FIXME: Cloning isn't correct in the presence of indirect goto!
// FIXME: This implementation of thunks bloats codesize by duplicating the
//        function definition.  There are alternatives:
//        1. Add some sort of stub support to LLVM for cases where we can
//           do a this adjustment, then a sibcall.
//        2. We could transform the definition to take a va_list instead of an
//           actual variable argument list, then have the thunks (including a
//           no-op thunk for the regular definition) call va_start/va_end.
//           There's a bit of per-call overhead for this solution, but it's
//           better for codesize if the definition is long.
llvm::Function *
CodeGenFunction::GenerateVarArgsThunk(llvm::Function *Fn,
                                      const CGFunctionInfo &FnInfo,
                                      GlobalDecl GD, const ThunkInfo &Thunk) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());
  const FunctionProtoType *FPT = MD->getType()->getAs<FunctionProtoType>();
  QualType ResultType = FPT->getReturnType();

  // Get the original function
  assert(FnInfo.isVariadic());
  llvm::Type *Ty = CGM.getTypes().GetFunctionType(FnInfo);
  llvm::Value *Callee = CGM.GetAddrOfFunction(GD, Ty, /*ForVTable=*/true);
  llvm::Function *BaseFn = cast<llvm::Function>(Callee);

  // Clone to thunk.
  llvm::ValueToValueMapTy VMap;

  // We are cloning a function while some Metadata nodes are still unresolved.
  // Ensure that the value mapper does not encounter any of them.
  resolveTopLevelMetadata(BaseFn, VMap);
  llvm::Function *NewFn = llvm::CloneFunction(BaseFn, VMap);
  Fn->replaceAllUsesWith(NewFn);
  NewFn->takeName(Fn);
  Fn->eraseFromParent();
  Fn = NewFn;

  // "Initialize" CGF (minimally).
  CurFn = Fn;

  // Get the "this" value
  llvm::Function::arg_iterator AI = Fn->arg_begin();
  if (CGM.ReturnTypeUsesSRet(FnInfo))
    ++AI;

  // Find the first store of "this", which will be to the alloca associated
  // with "this".
  Address ThisPtr(&*AI, CGM.getClassPointerAlignment(MD->getParent()));
  llvm::BasicBlock *EntryBB = &Fn->front();
  llvm::BasicBlock::iterator ThisStore =
      std::find_if(EntryBB->begin(), EntryBB->end(), [&](llvm::Instruction &I) {
        return isa<llvm::StoreInst>(I) &&
               I.getOperand(0) == ThisPtr.getPointer();
      });
  assert(ThisStore != EntryBB->end() &&
         "Store of this should be in entry block?");
  // Adjust "this", if necessary.
  Builder.SetInsertPoint(&*ThisStore);
  llvm::Value *AdjustedThisPtr =
      CGM.getCXXABI().performThisAdjustment(*this, ThisPtr, Thunk.This);
  ThisStore->setOperand(0, AdjustedThisPtr);

  if (!Thunk.Return.isEmpty()) {
    // Fix up the returned value, if necessary.
    for (llvm::BasicBlock &BB : *Fn) {
      llvm::Instruction *T = BB.getTerminator();
      if (isa<llvm::ReturnInst>(T)) {
        RValue RV = RValue::get(T->getOperand(0));
        T->eraseFromParent();
        Builder.SetInsertPoint(&BB);
        RV = PerformReturnAdjustment(*this, ResultType, RV, Thunk);
        Builder.CreateRet(RV.getScalarVal());
        break;
      }
    }
  }

  return Fn;
}

void CodeGenFunction::StartThunk(llvm::Function *Fn, GlobalDecl GD,
                                 const CGFunctionInfo &FnInfo,
                                 bool IsUnprototyped) {
  assert(!CurGD.getDecl() && "CurGD was already set!");
  CurGD = GD;
  CurFuncIsThunk = true;

  // Build FunctionArgs.
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());
  QualType ThisType = MD->getThisType();
  const FunctionProtoType *FPT = MD->getType()->getAs<FunctionProtoType>();
  QualType ResultType;
  if (IsUnprototyped)
    ResultType = CGM.getContext().VoidTy;
  else if (CGM.getCXXABI().HasThisReturn(GD))
    ResultType = ThisType;
  else if (CGM.getCXXABI().hasMostDerivedReturn(GD))
    ResultType = CGM.getContext().VoidPtrTy;
  else
    ResultType = FPT->getReturnType();
  FunctionArgList FunctionArgs;

  // Create the implicit 'this' parameter declaration.
  CGM.getCXXABI().buildThisParam(*this, FunctionArgs);

  // Add the rest of the parameters, if we have a prototype to work with.
  if (!IsUnprototyped) {
    FunctionArgs.append(MD->param_begin(), MD->param_end());

    if (isa<CXXDestructorDecl>(MD))
      CGM.getCXXABI().addImplicitStructorParams(*this, ResultType,
                                                FunctionArgs);
  }

  // Start defining the function.
  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  StartFunction(GlobalDecl(), ResultType, Fn, FnInfo, FunctionArgs,
                MD->getLocation());
  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  // Since we didn't pass a GlobalDecl to StartFunction, do this ourselves.
  CGM.getCXXABI().EmitInstanceFunctionProlog(*this);
  CXXThisValue = CXXABIThisValue;
  CurCodeDecl = MD;
  CurFuncDecl = MD;
}

void CodeGenFunction::FinishThunk() {
  // Clear these to restore the invariants expected by
  // StartFunction/FinishFunction.
  CurCodeDecl = nullptr;
  CurFuncDecl = nullptr;

  FinishFunction();
}

void CodeGenFunction::EmitCallAndReturnForThunk(llvm::Constant *CalleePtr,
                                                const ThunkInfo *Thunk,
                                                bool IsUnprototyped) {
  assert(isa<CXXMethodDecl>(CurGD.getDecl()) &&
         "Please use a new CGF for this thunk");
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CurGD.getDecl());

  // Adjust the 'this' pointer if necessary
  llvm::Value *AdjustedThisPtr =
    Thunk ? CGM.getCXXABI().performThisAdjustment(
                          *this, LoadCXXThisAddress(), Thunk->This)
          : LoadCXXThis();

  if (CurFnInfo->usesInAlloca() || IsUnprototyped) {
    // We don't handle return adjusting thunks, because they require us to call
    // the copy constructor.  For now, fall through and pretend the return
    // adjustment was empty so we don't crash.
    if (Thunk && !Thunk->Return.isEmpty()) {
      if (IsUnprototyped)
        CGM.ErrorUnsupported(
            MD, "return-adjusting thunk with incomplete parameter type");
      else
        CGM.ErrorUnsupported(
            MD, "non-trivial argument copy for return-adjusting thunk");
    }
    EmitMustTailThunk(CurGD, AdjustedThisPtr, CalleePtr);
    return;
  }

  // Start building CallArgs.
  CallArgList CallArgs;
  QualType ThisType = MD->getThisType();
  CallArgs.add(RValue::get(AdjustedThisPtr), ThisType);

  if (isa<CXXDestructorDecl>(MD))
    CGM.getCXXABI().adjustCallArgsForDestructorThunk(*this, CurGD, CallArgs);

#ifndef NDEBUG
  unsigned PrefixArgs = CallArgs.size() - 1;
#endif
  // Add the rest of the arguments.
  for (const ParmVarDecl *PD : MD->parameters())
    EmitDelegateCallArg(CallArgs, PD, SourceLocation());

  const FunctionProtoType *FPT = MD->getType()->getAs<FunctionProtoType>();

#ifndef NDEBUG
  const CGFunctionInfo &CallFnInfo = CGM.getTypes().arrangeCXXMethodCall(
      CallArgs, FPT, RequiredArgs::forPrototypePlus(FPT, 1, MD), PrefixArgs);
  assert(CallFnInfo.getRegParm() == CurFnInfo->getRegParm() &&
         CallFnInfo.isNoReturn() == CurFnInfo->isNoReturn() &&
         CallFnInfo.getCallingConvention() == CurFnInfo->getCallingConvention());
  assert(isa<CXXDestructorDecl>(MD) || // ignore dtor return types
         similar(CallFnInfo.getReturnInfo(), CallFnInfo.getReturnType(),
                 CurFnInfo->getReturnInfo(), CurFnInfo->getReturnType()));
  assert(CallFnInfo.arg_size() == CurFnInfo->arg_size());
  for (unsigned i = 0, e = CurFnInfo->arg_size(); i != e; ++i)
    assert(similar(CallFnInfo.arg_begin()[i].info,
                   CallFnInfo.arg_begin()[i].type,
                   CurFnInfo->arg_begin()[i].info,
                   CurFnInfo->arg_begin()[i].type));
#endif

  // Determine whether we have a return value slot to use.
  QualType ResultType = CGM.getCXXABI().HasThisReturn(CurGD)
                            ? ThisType
                            : CGM.getCXXABI().hasMostDerivedReturn(CurGD)
                                  ? CGM.getContext().VoidPtrTy
                                  : FPT->getReturnType();
  ReturnValueSlot Slot;
  if (!ResultType->isVoidType() &&
      CurFnInfo->getReturnInfo().getKind() == ABIArgInfo::Indirect)
    Slot = ReturnValueSlot(ReturnValue, ResultType.isVolatileQualified());

  // Now emit our call.
  llvm::Instruction *CallOrInvoke;
  CGCallee Callee = CGCallee::forDirect(CalleePtr, CurGD);
  RValue RV = EmitCall(*CurFnInfo, Callee, Slot, CallArgs, &CallOrInvoke);

  // Consider return adjustment if we have ThunkInfo.
  if (Thunk && !Thunk->Return.isEmpty())
    RV = PerformReturnAdjustment(*this, ResultType, RV, *Thunk);
  else if (llvm::CallInst* Call = dyn_cast<llvm::CallInst>(CallOrInvoke))
    Call->setTailCallKind(llvm::CallInst::TCK_Tail);

  // Emit return.
  if (!ResultType->isVoidType() && Slot.isNull())
    CGM.getCXXABI().EmitReturnFromThunk(*this, RV, ResultType);

  // Disable the final ARC autorelease.
  AutoreleaseResult = false;

  FinishThunk();
}

void CodeGenFunction::EmitMustTailThunk(GlobalDecl GD,
                                        llvm::Value *AdjustedThisPtr,
                                        llvm::Value *CalleePtr) {
  // Emitting a musttail call thunk doesn't use any of the CGCall.cpp machinery
  // to translate AST arguments into LLVM IR arguments.  For thunks, we know
  // that the caller prototype more or less matches the callee prototype with
  // the exception of 'this'.
  SmallVector<llvm::Value *, 8> Args;
  for (llvm::Argument &A : CurFn->args())
    Args.push_back(&A);

  // Set the adjusted 'this' pointer.
  const ABIArgInfo &ThisAI = CurFnInfo->arg_begin()->info;
  if (ThisAI.isDirect()) {
    const ABIArgInfo &RetAI = CurFnInfo->getReturnInfo();
    int ThisArgNo = RetAI.isIndirect() && !RetAI.isSRetAfterThis() ? 1 : 0;
    llvm::Type *ThisType = Args[ThisArgNo]->getType();
    if (ThisType != AdjustedThisPtr->getType())
      AdjustedThisPtr = Builder.CreateBitCast(AdjustedThisPtr, ThisType);
    Args[ThisArgNo] = AdjustedThisPtr;
  } else {
    assert(ThisAI.isInAlloca() && "this is passed directly or inalloca");
    Address ThisAddr = GetAddrOfLocalVar(CXXABIThisDecl);
    llvm::Type *ThisType = ThisAddr.getElementType();
    if (ThisType != AdjustedThisPtr->getType())
      AdjustedThisPtr = Builder.CreateBitCast(AdjustedThisPtr, ThisType);
    Builder.CreateStore(AdjustedThisPtr, ThisAddr);
  }

  // Emit the musttail call manually.  Even if the prologue pushed cleanups, we
  // don't actually want to run them.
  llvm::CallInst *Call = Builder.CreateCall(CalleePtr, Args);
  Call->setTailCallKind(llvm::CallInst::TCK_MustTail);

  // Apply the standard set of call attributes.
  unsigned CallingConv;
  llvm::AttributeList Attrs;
  CGM.ConstructAttributeList(CalleePtr->getName(), *CurFnInfo, GD, Attrs,
                             CallingConv, /*AttrOnCallSite=*/true);
  Call->setAttributes(Attrs);
  Call->setCallingConv(static_cast<llvm::CallingConv::ID>(CallingConv));

  if (Call->getType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(Call);

  // Finish the function to maintain CodeGenFunction invariants.
  // FIXME: Don't emit unreachable code.
  EmitBlock(createBasicBlock());
  FinishFunction();
}

void CodeGenFunction::generateThunk(llvm::Function *Fn,
                                    const CGFunctionInfo &FnInfo, GlobalDecl GD,
                                    const ThunkInfo &Thunk,
                                    bool IsUnprototyped) {
  StartThunk(Fn, GD, FnInfo, IsUnprototyped);
  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  // Get our callee. Use a placeholder type if this method is unprototyped so
  // that CodeGenModule doesn't try to set attributes.
  llvm::Type *Ty;
  if (IsUnprototyped)
    Ty = llvm::StructType::get(getLLVMContext());
  else
    Ty = CGM.getTypes().GetFunctionType(FnInfo);

  llvm::Constant *Callee = CGM.GetAddrOfFunction(GD, Ty, /*ForVTable=*/true);

  // Fix up the function type for an unprototyped musttail call.
  if (IsUnprototyped)
    Callee = llvm::ConstantExpr::getBitCast(Callee, Fn->getType());

  // Make the call and return the result.
  EmitCallAndReturnForThunk(Callee, &Thunk, IsUnprototyped);
}

static bool shouldEmitVTableThunk(CodeGenModule &CGM, const CXXMethodDecl *MD,
                                  bool IsUnprototyped, bool ForVTable) {
  // Always emit thunks in the MS C++ ABI. We cannot rely on other TUs to
  // provide thunks for us.
  if (CGM.getTarget().getCXXABI().isMicrosoft())
    return true;

  // In the Itanium C++ ABI, vtable thunks are provided by TUs that provide
  // definitions of the main method. Therefore, emitting thunks with the vtable
  // is purely an optimization. Emit the thunk if optimizations are enabled and
  // all of the parameter types are complete.
  if (ForVTable)
    return CGM.getCodeGenOpts().OptimizationLevel && !IsUnprototyped;

  // Always emit thunks along with the method definition.
  return true;
}

llvm::Constant *CodeGenVTables::maybeEmitThunk(GlobalDecl GD,
                                               const ThunkInfo &TI,
                                               bool ForVTable) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());

  // First, get a declaration. Compute the mangled name. Don't worry about
  // getting the function prototype right, since we may only need this
  // declaration to fill in a vtable slot.
  SmallString<256> Name;
  MangleContext &MCtx = CGM.getCXXABI().getMangleContext();
  llvm::raw_svector_ostream Out(Name);
  if (const CXXDestructorDecl *DD = dyn_cast<CXXDestructorDecl>(MD))
    MCtx.mangleCXXDtorThunk(DD, GD.getDtorType(), TI.This, Out);
  else
    MCtx.mangleThunk(MD, TI, Out);
  llvm::Type *ThunkVTableTy = CGM.getTypes().GetFunctionTypeForVTable(GD);
  llvm::Constant *Thunk = CGM.GetAddrOfThunk(Name, ThunkVTableTy, GD);

  // If we don't need to emit a definition, return this declaration as is.
  bool IsUnprototyped = !CGM.getTypes().isFuncTypeConvertible(
      MD->getType()->castAs<FunctionType>());
  if (!shouldEmitVTableThunk(CGM, MD, IsUnprototyped, ForVTable))
    return Thunk;

  // Arrange a function prototype appropriate for a function definition. In some
  // cases in the MS ABI, we may need to build an unprototyped musttail thunk.
  const CGFunctionInfo &FnInfo =
      IsUnprototyped ? CGM.getTypes().arrangeUnprototypedMustTailThunk(MD)
                     : CGM.getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *ThunkFnTy = CGM.getTypes().GetFunctionType(FnInfo);

  // If the type of the underlying GlobalValue is wrong, we'll have to replace
  // it. It should be a declaration.
  llvm::Function *ThunkFn = cast<llvm::Function>(Thunk->stripPointerCasts());
  if (ThunkFn->getFunctionType() != ThunkFnTy) {
    llvm::GlobalValue *OldThunkFn = ThunkFn;

    assert(OldThunkFn->isDeclaration() && "Shouldn't replace non-declaration");

    // Remove the name from the old thunk function and get a new thunk.
    OldThunkFn->setName(StringRef());
    ThunkFn = llvm::Function::Create(ThunkFnTy, llvm::Function::ExternalLinkage,
                                     Name.str(), &CGM.getModule());
    CGM.SetLLVMFunctionAttributes(MD, FnInfo, ThunkFn);

    // If needed, replace the old thunk with a bitcast.
    if (!OldThunkFn->use_empty()) {
      llvm::Constant *NewPtrForOldDecl =
          llvm::ConstantExpr::getBitCast(ThunkFn, OldThunkFn->getType());
      OldThunkFn->replaceAllUsesWith(NewPtrForOldDecl);
    }

    // Remove the old thunk.
    OldThunkFn->eraseFromParent();
  }

  bool ABIHasKeyFunctions = CGM.getTarget().getCXXABI().hasKeyFunctions();
  bool UseAvailableExternallyLinkage = ForVTable && ABIHasKeyFunctions;

  if (!ThunkFn->isDeclaration()) {
    if (!ABIHasKeyFunctions || UseAvailableExternallyLinkage) {
      // There is already a thunk emitted for this function, do nothing.
      return ThunkFn;
    }

    setThunkProperties(CGM, TI, ThunkFn, ForVTable, GD);
    return ThunkFn;
  }

  // If this will be unprototyped, add the "thunk" attribute so that LLVM knows
  // that the return type is meaningless. These thunks can be used to call
  // functions with differing return types, and the caller is required to cast
  // the prototype appropriately to extract the correct value.
  if (IsUnprototyped)
    ThunkFn->addFnAttr("thunk");

  CGM.SetLLVMFunctionAttributesForDefinition(GD.getDecl(), ThunkFn);

  if (!IsUnprototyped && ThunkFn->isVarArg()) {
    // Varargs thunks are special; we can't just generate a call because
    // we can't copy the varargs.  Our implementation is rather
    // expensive/sucky at the moment, so don't generate the thunk unless
    // we have to.
    // FIXME: Do something better here; GenerateVarArgsThunk is extremely ugly.
    if (UseAvailableExternallyLinkage)
      return ThunkFn;
    ThunkFn = CodeGenFunction(CGM).GenerateVarArgsThunk(ThunkFn, FnInfo, GD,
                                                        TI);
  } else {
    // Normal thunk body generation.
    CodeGenFunction(CGM).generateThunk(ThunkFn, FnInfo, GD, TI, IsUnprototyped);
  }

  setThunkProperties(CGM, TI, ThunkFn, ForVTable, GD);
  return ThunkFn;
}

void CodeGenVTables::EmitThunks(GlobalDecl GD) {
  const CXXMethodDecl *MD =
    cast<CXXMethodDecl>(GD.getDecl())->getCanonicalDecl();

  // We don't need to generate thunks for the base destructor.
  if (isa<CXXDestructorDecl>(MD) && GD.getDtorType() == Dtor_Base)
    return;

  const VTableContextBase::ThunkInfoVectorTy *ThunkInfoVector =
      VTContext->getThunkInfo(GD);

  if (!ThunkInfoVector)
    return;

  for (const ThunkInfo& Thunk : *ThunkInfoVector)
    maybeEmitThunk(GD, Thunk, /*ForVTable=*/false);
}

void CodeGenVTables::addVTableComponent(
    ConstantArrayBuilder &builder, const VTableLayout &layout,
    unsigned idx, llvm::Constant *rtti, unsigned &nextVTableThunkIndex) {
  auto &component = layout.vtable_components()[idx];

  auto addOffsetConstant = [&](CharUnits offset) {
    builder.add(llvm::ConstantExpr::getIntToPtr(
        llvm::ConstantInt::get(CGM.PtrDiffTy, offset.getQuantity()),
        CGM.Int8PtrTy));
  };

  switch (component.getKind()) {
  case VTableComponent::CK_VCallOffset:
    return addOffsetConstant(component.getVCallOffset());

  case VTableComponent::CK_VBaseOffset:
    return addOffsetConstant(component.getVBaseOffset());

  case VTableComponent::CK_OffsetToTop:
    return addOffsetConstant(component.getOffsetToTop());

  case VTableComponent::CK_RTTI:
    return builder.add(llvm::ConstantExpr::getBitCast(rtti, CGM.Int8PtrTy));

  case VTableComponent::CK_FunctionPointer:
  case VTableComponent::CK_CompleteDtorPointer:
  case VTableComponent::CK_DeletingDtorPointer: {
    GlobalDecl GD;

    // Get the right global decl.
    switch (component.getKind()) {
    default:
      llvm_unreachable("Unexpected vtable component kind");
    case VTableComponent::CK_FunctionPointer:
      GD = component.getFunctionDecl();
      break;
    case VTableComponent::CK_CompleteDtorPointer:
      GD = GlobalDecl(component.getDestructorDecl(), Dtor_Complete);
      break;
    case VTableComponent::CK_DeletingDtorPointer:
      GD = GlobalDecl(component.getDestructorDecl(), Dtor_Deleting);
      break;
    }

    if (CGM.getLangOpts().CUDA) {
      // Emit NULL for methods we can't codegen on this
      // side. Otherwise we'd end up with vtable with unresolved
      // references.
      const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());
      // OK on device side: functions w/ __device__ attribute
      // OK on host side: anything except __device__-only functions.
      bool CanEmitMethod =
          CGM.getLangOpts().CUDAIsDevice
              ? MD->hasAttr<CUDADeviceAttr>()
              : (MD->hasAttr<CUDAHostAttr>() || !MD->hasAttr<CUDADeviceAttr>());
      if (!CanEmitMethod)
        return builder.addNullPointer(CGM.Int8PtrTy);
      // Method is acceptable, continue processing as usual.
    }

    auto getSpecialVirtualFn = [&](StringRef name) {
      llvm::FunctionType *fnTy =
          llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
      llvm::Constant *fn = CGM.CreateRuntimeFunction(fnTy, name);
      if (auto f = dyn_cast<llvm::Function>(fn))
        f->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      return llvm::ConstantExpr::getBitCast(fn, CGM.Int8PtrTy);
    };

    llvm::Constant *fnPtr;

    // Pure virtual member functions.
    if (cast<CXXMethodDecl>(GD.getDecl())->isPure()) {
      if (!PureVirtualFn)
        PureVirtualFn =
          getSpecialVirtualFn(CGM.getCXXABI().GetPureVirtualCallName());
      fnPtr = PureVirtualFn;

    // Deleted virtual member functions.
    } else if (cast<CXXMethodDecl>(GD.getDecl())->isDeleted()) {
      if (!DeletedVirtualFn)
        DeletedVirtualFn =
          getSpecialVirtualFn(CGM.getCXXABI().GetDeletedVirtualCallName());
      fnPtr = DeletedVirtualFn;

    // Thunks.
    } else if (nextVTableThunkIndex < layout.vtable_thunks().size() &&
               layout.vtable_thunks()[nextVTableThunkIndex].first == idx) {
      auto &thunkInfo = layout.vtable_thunks()[nextVTableThunkIndex].second;

      nextVTableThunkIndex++;
      fnPtr = maybeEmitThunk(GD, thunkInfo, /*ForVTable=*/true);

    // Otherwise we can use the method definition directly.
    } else {
      llvm::Type *fnTy = CGM.getTypes().GetFunctionTypeForVTable(GD);
      fnPtr = CGM.GetAddrOfFunction(GD, fnTy, /*ForVTable=*/true);
    }

    fnPtr = llvm::ConstantExpr::getBitCast(fnPtr, CGM.Int8PtrTy);
    builder.add(fnPtr);
    return;
  }

  case VTableComponent::CK_UnusedFunctionPointer:
    return builder.addNullPointer(CGM.Int8PtrTy);
  }

  llvm_unreachable("Unexpected vtable component kind");
}

llvm::Type *CodeGenVTables::getVTableType(const VTableLayout &layout) {
  SmallVector<llvm::Type *, 4> tys;
  for (unsigned i = 0, e = layout.getNumVTables(); i != e; ++i) {
    tys.push_back(llvm::ArrayType::get(CGM.Int8PtrTy, layout.getVTableSize(i)));
  }

  return llvm::StructType::get(CGM.getLLVMContext(), tys);
}

void CodeGenVTables::createVTableInitializer(ConstantStructBuilder &builder,
                                             const VTableLayout &layout,
                                             llvm::Constant *rtti) {
  unsigned nextVTableThunkIndex = 0;
  for (unsigned i = 0, e = layout.getNumVTables(); i != e; ++i) {
    auto vtableElem = builder.beginArray(CGM.Int8PtrTy);
    size_t thisIndex = layout.getVTableOffset(i);
    size_t nextIndex = thisIndex + layout.getVTableSize(i);
    for (unsigned i = thisIndex; i != nextIndex; ++i) {
      addVTableComponent(vtableElem, layout, i, rtti, nextVTableThunkIndex);
    }
    vtableElem.finishAndAddTo(builder);
  }
}

llvm::GlobalVariable *
CodeGenVTables::GenerateConstructionVTable(const CXXRecordDecl *RD,
                                      const BaseSubobject &Base,
                                      bool BaseIsVirtual,
                                   llvm::GlobalVariable::LinkageTypes Linkage,
                                      VTableAddressPointsMapTy& AddressPoints) {
  if (CGDebugInfo *DI = CGM.getModuleDebugInfo())
    DI->completeClassData(Base.getBase());

  std::unique_ptr<VTableLayout> VTLayout(
      getItaniumVTableContext().createConstructionVTableLayout(
          Base.getBase(), Base.getBaseOffset(), BaseIsVirtual, RD));

  // Add the address points.
  AddressPoints = VTLayout->getAddressPoints();

  // Get the mangled construction vtable name.
  SmallString<256> OutName;
  llvm::raw_svector_ostream Out(OutName);
  cast<ItaniumMangleContext>(CGM.getCXXABI().getMangleContext())
      .mangleCXXCtorVTable(RD, Base.getBaseOffset().getQuantity(),
                           Base.getBase(), Out);
  StringRef Name = OutName.str();

  llvm::Type *VTType = getVTableType(*VTLayout);

  // Construction vtable symbols are not part of the Itanium ABI, so we cannot
  // guarantee that they actually will be available externally. Instead, when
  // emitting an available_externally VTT, we provide references to an internal
  // linkage construction vtable. The ABI only requires complete-object vtables
  // to be the same for all instances of a type, not construction vtables.
  if (Linkage == llvm::GlobalVariable::AvailableExternallyLinkage)
    Linkage = llvm::GlobalVariable::InternalLinkage;

  unsigned Align = CGM.getDataLayout().getABITypeAlignment(VTType);

  // Create the variable that will hold the construction vtable.
  llvm::GlobalVariable *VTable =
      CGM.CreateOrReplaceCXXRuntimeVariable(Name, VTType, Linkage, Align);
  CGM.setGVProperties(VTable, RD);

  // V-tables are always unnamed_addr.
  VTable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  llvm::Constant *RTTI = CGM.GetAddrOfRTTIDescriptor(
      CGM.getContext().getTagDeclType(Base.getBase()));

  // Create and set the initializer.
  ConstantInitBuilder builder(CGM);
  auto components = builder.beginStruct();
  createVTableInitializer(components, *VTLayout, RTTI);
  components.finishAndSetAsInitializer(VTable);

  CGM.EmitVTableTypeMetadata(VTable, *VTLayout.get());

  return VTable;
}

static bool shouldEmitAvailableExternallyVTable(const CodeGenModule &CGM,
                                                const CXXRecordDecl *RD) {
  return CGM.getCodeGenOpts().OptimizationLevel > 0 &&
         CGM.getCXXABI().canSpeculativelyEmitVTable(RD);
}

/// Compute the required linkage of the vtable for the given class.
///
/// Note that we only call this at the end of the translation unit.
llvm::GlobalVariable::LinkageTypes
CodeGenModule::getVTableLinkage(const CXXRecordDecl *RD) {
  if (!RD->isExternallyVisible())
    return llvm::GlobalVariable::InternalLinkage;

  // We're at the end of the translation unit, so the current key
  // function is fully correct.
  const CXXMethodDecl *keyFunction = Context.getCurrentKeyFunction(RD);
  if (keyFunction && !RD->hasAttr<DLLImportAttr>()) {
    // If this class has a key function, use that to determine the
    // linkage of the vtable.
    const FunctionDecl *def = nullptr;
    if (keyFunction->hasBody(def))
      keyFunction = cast<CXXMethodDecl>(def);

    switch (keyFunction->getTemplateSpecializationKind()) {
      case TSK_Undeclared:
      case TSK_ExplicitSpecialization:
        assert((def || CodeGenOpts.OptimizationLevel > 0 ||
                CodeGenOpts.getDebugInfo() != codegenoptions::NoDebugInfo) &&
               "Shouldn't query vtable linkage without key function, "
               "optimizations, or debug info");
        if (!def && CodeGenOpts.OptimizationLevel > 0)
          return llvm::GlobalVariable::AvailableExternallyLinkage;

        if (keyFunction->isInlined())
          return !Context.getLangOpts().AppleKext ?
                   llvm::GlobalVariable::LinkOnceODRLinkage :
                   llvm::Function::InternalLinkage;

        return llvm::GlobalVariable::ExternalLinkage;

      case TSK_ImplicitInstantiation:
        return !Context.getLangOpts().AppleKext ?
                 llvm::GlobalVariable::LinkOnceODRLinkage :
                 llvm::Function::InternalLinkage;

      case TSK_ExplicitInstantiationDefinition:
        return !Context.getLangOpts().AppleKext ?
                 llvm::GlobalVariable::WeakODRLinkage :
                 llvm::Function::InternalLinkage;

      case TSK_ExplicitInstantiationDeclaration:
        llvm_unreachable("Should not have been asked to emit this");
    }
  }

  // -fapple-kext mode does not support weak linkage, so we must use
  // internal linkage.
  if (Context.getLangOpts().AppleKext)
    return llvm::Function::InternalLinkage;

  llvm::GlobalVariable::LinkageTypes DiscardableODRLinkage =
      llvm::GlobalValue::LinkOnceODRLinkage;
  llvm::GlobalVariable::LinkageTypes NonDiscardableODRLinkage =
      llvm::GlobalValue::WeakODRLinkage;
  if (RD->hasAttr<DLLExportAttr>()) {
    // Cannot discard exported vtables.
    DiscardableODRLinkage = NonDiscardableODRLinkage;
  } else if (RD->hasAttr<DLLImportAttr>()) {
    // Imported vtables are available externally.
    DiscardableODRLinkage = llvm::GlobalVariable::AvailableExternallyLinkage;
    NonDiscardableODRLinkage = llvm::GlobalVariable::AvailableExternallyLinkage;
  }

  switch (RD->getTemplateSpecializationKind()) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
    case TSK_ImplicitInstantiation:
      return DiscardableODRLinkage;

    case TSK_ExplicitInstantiationDeclaration:
      // Explicit instantiations in MSVC do not provide vtables, so we must emit
      // our own.
      if (getTarget().getCXXABI().isMicrosoft())
        return DiscardableODRLinkage;
      return shouldEmitAvailableExternallyVTable(*this, RD)
                 ? llvm::GlobalVariable::AvailableExternallyLinkage
                 : llvm::GlobalVariable::ExternalLinkage;

    case TSK_ExplicitInstantiationDefinition:
      return NonDiscardableODRLinkage;
  }

  llvm_unreachable("Invalid TemplateSpecializationKind!");
}

/// This is a callback from Sema to tell us that a particular vtable is
/// required to be emitted in this translation unit.
///
/// This is only called for vtables that _must_ be emitted (mainly due to key
/// functions).  For weak vtables, CodeGen tracks when they are needed and
/// emits them as-needed.
void CodeGenModule::EmitVTable(CXXRecordDecl *theClass) {
  VTables.GenerateClassData(theClass);
}

void
CodeGenVTables::GenerateClassData(const CXXRecordDecl *RD) {
  if (CGDebugInfo *DI = CGM.getModuleDebugInfo())
    DI->completeClassData(RD);

  if (RD->getNumVBases())
    CGM.getCXXABI().emitVirtualInheritanceTables(RD);

  CGM.getCXXABI().emitVTableDefinitions(*this, RD);
}

/// At this point in the translation unit, does it appear that can we
/// rely on the vtable being defined elsewhere in the program?
///
/// The response is really only definitive when called at the end of
/// the translation unit.
///
/// The only semantic restriction here is that the object file should
/// not contain a vtable definition when that vtable is defined
/// strongly elsewhere.  Otherwise, we'd just like to avoid emitting
/// vtables when unnecessary.
bool CodeGenVTables::isVTableExternal(const CXXRecordDecl *RD) {
  assert(RD->isDynamicClass() && "Non-dynamic classes have no VTable.");

  // We always synthesize vtables if they are needed in the MS ABI. MSVC doesn't
  // emit them even if there is an explicit template instantiation.
  if (CGM.getTarget().getCXXABI().isMicrosoft())
    return false;

  // If we have an explicit instantiation declaration (and not a
  // definition), the vtable is defined elsewhere.
  TemplateSpecializationKind TSK = RD->getTemplateSpecializationKind();
  if (TSK == TSK_ExplicitInstantiationDeclaration)
    return true;

  // Otherwise, if the class is an instantiated template, the
  // vtable must be defined here.
  if (TSK == TSK_ImplicitInstantiation ||
      TSK == TSK_ExplicitInstantiationDefinition)
    return false;

  // Otherwise, if the class doesn't have a key function (possibly
  // anymore), the vtable must be defined here.
  const CXXMethodDecl *keyFunction = CGM.getContext().getCurrentKeyFunction(RD);
  if (!keyFunction)
    return false;

  // Otherwise, if we don't have a definition of the key function, the
  // vtable must be defined somewhere else.
  return !keyFunction->hasBody();
}

/// Given that we're currently at the end of the translation unit, and
/// we've emitted a reference to the vtable for this class, should
/// we define that vtable?
static bool shouldEmitVTableAtEndOfTranslationUnit(CodeGenModule &CGM,
                                                   const CXXRecordDecl *RD) {
  // If vtable is internal then it has to be done.
  if (!CGM.getVTables().isVTableExternal(RD))
    return true;

  // If it's external then maybe we will need it as available_externally.
  return shouldEmitAvailableExternallyVTable(CGM, RD);
}

/// Given that at some point we emitted a reference to one or more
/// vtables, and that we are now at the end of the translation unit,
/// decide whether we should emit them.
void CodeGenModule::EmitDeferredVTables() {
#ifndef NDEBUG
  // Remember the size of DeferredVTables, because we're going to assume
  // that this entire operation doesn't modify it.
  size_t savedSize = DeferredVTables.size();
#endif

  for (const CXXRecordDecl *RD : DeferredVTables)
    if (shouldEmitVTableAtEndOfTranslationUnit(*this, RD))
      VTables.GenerateClassData(RD);
    else if (shouldOpportunisticallyEmitVTables())
      OpportunisticVTables.push_back(RD);

  assert(savedSize == DeferredVTables.size() &&
         "deferred extra vtables during vtable emission?");
  DeferredVTables.clear();
}

bool CodeGenModule::HasHiddenLTOVisibility(const CXXRecordDecl *RD) {
  LinkageInfo LV = RD->getLinkageAndVisibility();
  if (!isExternallyVisible(LV.getLinkage()))
    return true;

  if (RD->hasAttr<LTOVisibilityPublicAttr>() || RD->hasAttr<UuidAttr>())
    return false;

  if (getTriple().isOSBinFormatCOFF()) {
    if (RD->hasAttr<DLLExportAttr>() || RD->hasAttr<DLLImportAttr>())
      return false;
  } else {
    if (LV.getVisibility() != HiddenVisibility)
      return false;
  }

  if (getCodeGenOpts().LTOVisibilityPublicStd) {
    const DeclContext *DC = RD;
    while (1) {
      auto *D = cast<Decl>(DC);
      DC = DC->getParent();
      if (isa<TranslationUnitDecl>(DC->getRedeclContext())) {
        if (auto *ND = dyn_cast<NamespaceDecl>(D))
          if (const IdentifierInfo *II = ND->getIdentifier())
            if (II->isStr("std") || II->isStr("stdext"))
              return false;
        break;
      }
    }
  }

  return true;
}

void CodeGenModule::EmitVTableTypeMetadata(llvm::GlobalVariable *VTable,
                                           const VTableLayout &VTLayout) {
  if (!getCodeGenOpts().LTOUnit)
    return;

  CharUnits PointerWidth =
      Context.toCharUnitsFromBits(Context.getTargetInfo().getPointerWidth(0));

  typedef std::pair<const CXXRecordDecl *, unsigned> AddressPoint;
  std::vector<AddressPoint> AddressPoints;
  for (auto &&AP : VTLayout.getAddressPoints())
    AddressPoints.push_back(std::make_pair(
        AP.first.getBase(), VTLayout.getVTableOffset(AP.second.VTableIndex) +
                                AP.second.AddressPointIndex));

  // Sort the address points for determinism.
  llvm::sort(AddressPoints, [this](const AddressPoint &AP1,
                                   const AddressPoint &AP2) {
    if (&AP1 == &AP2)
      return false;

    std::string S1;
    llvm::raw_string_ostream O1(S1);
    getCXXABI().getMangleContext().mangleTypeName(
        QualType(AP1.first->getTypeForDecl(), 0), O1);
    O1.flush();

    std::string S2;
    llvm::raw_string_ostream O2(S2);
    getCXXABI().getMangleContext().mangleTypeName(
        QualType(AP2.first->getTypeForDecl(), 0), O2);
    O2.flush();

    if (S1 < S2)
      return true;
    if (S1 != S2)
      return false;

    return AP1.second < AP2.second;
  });

  ArrayRef<VTableComponent> Comps = VTLayout.vtable_components();
  for (auto AP : AddressPoints) {
    // Create type metadata for the address point.
    AddVTableTypeMetadata(VTable, PointerWidth * AP.second, AP.first);

    // The class associated with each address point could also potentially be
    // used for indirect calls via a member function pointer, so we need to
    // annotate the address of each function pointer with the appropriate member
    // function pointer type.
    for (unsigned I = 0; I != Comps.size(); ++I) {
      if (Comps[I].getKind() != VTableComponent::CK_FunctionPointer)
        continue;
      llvm::Metadata *MD = CreateMetadataIdentifierForVirtualMemPtrType(
          Context.getMemberPointerType(
              Comps[I].getFunctionDecl()->getType(),
              Context.getRecordType(AP.first).getTypePtr()));
      VTable->addTypeMetadata((PointerWidth * I).getQuantity(), MD);
    }
  }
}
