//===--- CGDeclCXX.cpp - Emit LLVM Code for C++ declarations --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ declarations
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CGCXXABI.h"
#include "CGObjCRuntime.h"
#include "CGOpenMPRuntime.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace CodeGen;

static void EmitDeclInit(CodeGenFunction &CGF, const VarDecl &D,
                         ConstantAddress DeclPtr) {
  assert(
      (D.hasGlobalStorage() ||
       (D.hasLocalStorage() && CGF.getContext().getLangOpts().OpenCLCPlusPlus)) &&
      "VarDecl must have global or local (in the case of OpenCL) storage!");
  assert(!D.getType()->isReferenceType() &&
         "Should not call EmitDeclInit on a reference!");

  QualType type = D.getType();
  LValue lv = CGF.MakeAddrLValue(DeclPtr, type);

  const Expr *Init = D.getInit();
  switch (CGF.getEvaluationKind(type)) {
  case TEK_Scalar: {
    CodeGenModule &CGM = CGF.CGM;
    if (lv.isObjCStrong())
      CGM.getObjCRuntime().EmitObjCGlobalAssign(CGF, CGF.EmitScalarExpr(Init),
                                                DeclPtr, D.getTLSKind());
    else if (lv.isObjCWeak())
      CGM.getObjCRuntime().EmitObjCWeakAssign(CGF, CGF.EmitScalarExpr(Init),
                                              DeclPtr);
    else
      CGF.EmitScalarInit(Init, &D, lv, false);
    return;
  }
  case TEK_Complex:
    CGF.EmitComplexExprIntoLValue(Init, lv, /*isInit*/ true);
    return;
  case TEK_Aggregate:
    CGF.EmitAggExpr(Init, AggValueSlot::forLValue(lv,AggValueSlot::IsDestructed,
                                          AggValueSlot::DoesNotNeedGCBarriers,
                                                  AggValueSlot::IsNotAliased,
                                                  AggValueSlot::DoesNotOverlap));
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

/// Emit code to cause the destruction of the given variable with
/// static storage duration.
static void EmitDeclDestroy(CodeGenFunction &CGF, const VarDecl &D,
                            ConstantAddress Addr) {
  // Honor __attribute__((no_destroy)) and bail instead of attempting
  // to emit a reference to a possibly nonexistent destructor, which
  // in turn can cause a crash. This will result in a global constructor
  // that isn't balanced out by a destructor call as intended by the
  // attribute. This also checks for -fno-c++-static-destructors and
  // bails even if the attribute is not present.
  if (D.isNoDestroy(CGF.getContext()))
    return;
  
  CodeGenModule &CGM = CGF.CGM;

  // FIXME:  __attribute__((cleanup)) ?

  QualType Type = D.getType();
  QualType::DestructionKind DtorKind = Type.isDestructedType();

  switch (DtorKind) {
  case QualType::DK_none:
    return;

  case QualType::DK_cxx_destructor:
    break;

  case QualType::DK_objc_strong_lifetime:
  case QualType::DK_objc_weak_lifetime:
  case QualType::DK_nontrivial_c_struct:
    // We don't care about releasing objects during process teardown.
    assert(!D.getTLSKind() && "should have rejected this");
    return;
  }

  llvm::Constant *Func;
  llvm::Constant *Argument;

  // Special-case non-array C++ destructors, if they have the right signature.
  // Under some ABIs, destructors return this instead of void, and cannot be
  // passed directly to __cxa_atexit if the target does not allow this
  // mismatch.
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  bool CanRegisterDestructor =
      Record && (!CGM.getCXXABI().HasThisReturn(
                     GlobalDecl(Record->getDestructor(), Dtor_Complete)) ||
                 CGM.getCXXABI().canCallMismatchedFunctionType());
  // If __cxa_atexit is disabled via a flag, a different helper function is
  // generated elsewhere which uses atexit instead, and it takes the destructor
  // directly.
  bool UsingExternalHelper = !CGM.getCodeGenOpts().CXAAtExit;
  if (Record && (CanRegisterDestructor || UsingExternalHelper)) {
    assert(!Record->hasTrivialDestructor());
    CXXDestructorDecl *Dtor = Record->getDestructor();

    Func = CGM.getAddrOfCXXStructor(Dtor, StructorType::Complete);
    Argument = llvm::ConstantExpr::getBitCast(
        Addr.getPointer(), CGF.getTypes().ConvertType(Type)->getPointerTo());

  // Otherwise, the standard logic requires a helper function.
  } else {
    Func = CodeGenFunction(CGM)
           .generateDestroyHelper(Addr, Type, CGF.getDestroyer(DtorKind),
                                  CGF.needsEHCleanup(DtorKind), &D);
    Argument = llvm::Constant::getNullValue(CGF.Int8PtrTy);
  }

  CGM.getCXXABI().registerGlobalDtor(CGF, D, Func, Argument);
}

/// Emit code to cause the variable at the given address to be considered as
/// constant from this point onwards.
static void EmitDeclInvariant(CodeGenFunction &CGF, const VarDecl &D,
                              llvm::Constant *Addr) {
  return CGF.EmitInvariantStart(
      Addr, CGF.getContext().getTypeSizeInChars(D.getType()));
}

void CodeGenFunction::EmitInvariantStart(llvm::Constant *Addr, CharUnits Size) {
  // Do not emit the intrinsic if we're not optimizing.
  if (!CGM.getCodeGenOpts().OptimizationLevel)
    return;

  // Grab the llvm.invariant.start intrinsic.
  llvm::Intrinsic::ID InvStartID = llvm::Intrinsic::invariant_start;
  // Overloaded address space type.
  llvm::Type *ObjectPtr[1] = {Int8PtrTy};
  llvm::Constant *InvariantStart = CGM.getIntrinsic(InvStartID, ObjectPtr);

  // Emit a call with the size in bytes of the object.
  uint64_t Width = Size.getQuantity();
  llvm::Value *Args[2] = { llvm::ConstantInt::getSigned(Int64Ty, Width),
                           llvm::ConstantExpr::getBitCast(Addr, Int8PtrTy)};
  Builder.CreateCall(InvariantStart, Args);
}

void CodeGenFunction::EmitCXXGlobalVarDeclInit(const VarDecl &D,
                                               llvm::Constant *DeclPtr,
                                               bool PerformInit) {

  const Expr *Init = D.getInit();
  QualType T = D.getType();

  // The address space of a static local variable (DeclPtr) may be different
  // from the address space of the "this" argument of the constructor. In that
  // case, we need an addrspacecast before calling the constructor.
  //
  // struct StructWithCtor {
  //   __device__ StructWithCtor() {...}
  // };
  // __device__ void foo() {
  //   __shared__ StructWithCtor s;
  //   ...
  // }
  //
  // For example, in the above CUDA code, the static local variable s has a
  // "shared" address space qualifier, but the constructor of StructWithCtor
  // expects "this" in the "generic" address space.
  unsigned ExpectedAddrSpace = getContext().getTargetAddressSpace(T);
  unsigned ActualAddrSpace = DeclPtr->getType()->getPointerAddressSpace();
  if (ActualAddrSpace != ExpectedAddrSpace) {
    llvm::Type *LTy = CGM.getTypes().ConvertTypeForMem(T);
    llvm::PointerType *PTy = llvm::PointerType::get(LTy, ExpectedAddrSpace);
    DeclPtr = llvm::ConstantExpr::getAddrSpaceCast(DeclPtr, PTy);
  }

  ConstantAddress DeclAddr(DeclPtr, getContext().getDeclAlign(&D));

  if (!T->isReferenceType()) {
    if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd &&
        D.hasAttr<OMPThreadPrivateDeclAttr>()) {
      (void)CGM.getOpenMPRuntime().emitThreadPrivateVarDefinition(
          &D, DeclAddr, D.getAttr<OMPThreadPrivateDeclAttr>()->getLocation(),
          PerformInit, this);
    }
    if (PerformInit)
      EmitDeclInit(*this, D, DeclAddr);
    if (CGM.isTypeConstant(D.getType(), true))
      EmitDeclInvariant(*this, D, DeclPtr);
    else
      EmitDeclDestroy(*this, D, DeclAddr);
    return;
  }

  assert(PerformInit && "cannot have constant initializer which needs "
         "destruction for reference");
  RValue RV = EmitReferenceBindingToExpr(Init);
  EmitStoreOfScalar(RV.getScalarVal(), DeclAddr, false, T);
}

/// Create a stub function, suitable for being passed to atexit,
/// which passes the given address to the given destructor function.
llvm::Constant *CodeGenFunction::createAtExitStub(const VarDecl &VD,
                                                  llvm::Constant *dtor,
                                                  llvm::Constant *addr) {
  // Get the destructor function type, void(*)(void).
  llvm::FunctionType *ty = llvm::FunctionType::get(CGM.VoidTy, false);
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    CGM.getCXXABI().getMangleContext().mangleDynamicAtExitDestructor(&VD, Out);
  }

  const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
  llvm::Function *fn = CGM.CreateGlobalInitOrDestructFunction(ty, FnName.str(),
                                                              FI,
                                                              VD.getLocation());

  CodeGenFunction CGF(CGM);

  CGF.StartFunction(&VD, CGM.getContext().VoidTy, fn, FI, FunctionArgList());

  llvm::CallInst *call = CGF.Builder.CreateCall(dtor, addr);

 // Make sure the call and the callee agree on calling convention.
  if (llvm::Function *dtorFn =
        dyn_cast<llvm::Function>(dtor->stripPointerCasts()))
    call->setCallingConv(dtorFn->getCallingConv());

  CGF.FinishFunction();

  return fn;
}

/// Register a global destructor using the C atexit runtime function.
void CodeGenFunction::registerGlobalDtorWithAtExit(const VarDecl &VD,
                                                   llvm::Constant *dtor,
                                                   llvm::Constant *addr) {
  // Create a function which calls the destructor.
  llvm::Constant *dtorStub = createAtExitStub(VD, dtor, addr);
  registerGlobalDtorWithAtExit(dtorStub);
}

void CodeGenFunction::registerGlobalDtorWithAtExit(llvm::Constant *dtorStub) {
  // extern "C" int atexit(void (*f)(void));
  llvm::FunctionType *atexitTy =
    llvm::FunctionType::get(IntTy, dtorStub->getType(), false);

  llvm::Constant *atexit =
      CGM.CreateRuntimeFunction(atexitTy, "atexit", llvm::AttributeList(),
                                /*Local=*/true);
  if (llvm::Function *atexitFn = dyn_cast<llvm::Function>(atexit))
    atexitFn->setDoesNotThrow();

  EmitNounwindRuntimeCall(atexit, dtorStub);
}

void CodeGenFunction::EmitCXXGuardedInit(const VarDecl &D,
                                         llvm::GlobalVariable *DeclPtr,
                                         bool PerformInit) {
  // If we've been asked to forbid guard variables, emit an error now.
  // This diagnostic is hard-coded for Darwin's use case;  we can find
  // better phrasing if someone else needs it.
  if (CGM.getCodeGenOpts().ForbidGuardVariables)
    CGM.Error(D.getLocation(),
              "this initialization requires a guard variable, which "
              "the kernel does not support");

  CGM.getCXXABI().EmitGuardedInit(*this, D, DeclPtr, PerformInit);
}

void CodeGenFunction::EmitCXXGuardedInitBranch(llvm::Value *NeedsInit,
                                               llvm::BasicBlock *InitBlock,
                                               llvm::BasicBlock *NoInitBlock,
                                               GuardKind Kind,
                                               const VarDecl *D) {
  assert((Kind == GuardKind::TlsGuard || D) && "no guarded variable");

  // A guess at how many times we will enter the initialization of a
  // variable, depending on the kind of variable.
  static const uint64_t InitsPerTLSVar = 1024;
  static const uint64_t InitsPerLocalVar = 1024 * 1024;

  llvm::MDNode *Weights;
  if (Kind == GuardKind::VariableGuard && !D->isLocalVarDecl()) {
    // For non-local variables, don't apply any weighting for now. Due to our
    // use of COMDATs, we expect there to be at most one initialization of the
    // variable per DSO, but we have no way to know how many DSOs will try to
    // initialize the variable.
    Weights = nullptr;
  } else {
    uint64_t NumInits;
    // FIXME: For the TLS case, collect and use profiling information to
    // determine a more accurate brach weight.
    if (Kind == GuardKind::TlsGuard || D->getTLSKind())
      NumInits = InitsPerTLSVar;
    else
      NumInits = InitsPerLocalVar;

    // The probability of us entering the initializer is
    //   1 / (total number of times we attempt to initialize the variable).
    llvm::MDBuilder MDHelper(CGM.getLLVMContext());
    Weights = MDHelper.createBranchWeights(1, NumInits - 1);
  }

  Builder.CreateCondBr(NeedsInit, InitBlock, NoInitBlock, Weights);
}

llvm::Function *CodeGenModule::CreateGlobalInitOrDestructFunction(
    llvm::FunctionType *FTy, const Twine &Name, const CGFunctionInfo &FI,
    SourceLocation Loc, bool TLS) {
  llvm::Function *Fn =
    llvm::Function::Create(FTy, llvm::GlobalValue::InternalLinkage,
                           Name, &getModule());
  if (!getLangOpts().AppleKext && !TLS) {
    // Set the section if needed.
    if (const char *Section = getTarget().getStaticInitSectionSpecifier())
      Fn->setSection(Section);
  }

  SetInternalFunctionAttributes(GlobalDecl(), Fn, FI);

  Fn->setCallingConv(getRuntimeCC());

  if (!getLangOpts().Exceptions)
    Fn->setDoesNotThrow();

  if (getLangOpts().Sanitize.has(SanitizerKind::Address) &&
      !isInSanitizerBlacklist(SanitizerKind::Address, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelAddress) &&
      !isInSanitizerBlacklist(SanitizerKind::KernelAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::HWAddress) &&
      !isInSanitizerBlacklist(SanitizerKind::HWAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeHWAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelHWAddress) &&
      !isInSanitizerBlacklist(SanitizerKind::KernelHWAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeHWAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::Thread) &&
      !isInSanitizerBlacklist(SanitizerKind::Thread, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeThread);

  if (getLangOpts().Sanitize.has(SanitizerKind::Memory) &&
      !isInSanitizerBlacklist(SanitizerKind::Memory, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemory);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelMemory) &&
      !isInSanitizerBlacklist(SanitizerKind::KernelMemory, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemory);

  if (getLangOpts().Sanitize.has(SanitizerKind::SafeStack) &&
      !isInSanitizerBlacklist(SanitizerKind::SafeStack, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SafeStack);

  if (getLangOpts().Sanitize.has(SanitizerKind::ShadowCallStack) &&
      !isInSanitizerBlacklist(SanitizerKind::ShadowCallStack, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::ShadowCallStack);

  auto RASignKind = getCodeGenOpts().getSignReturnAddress();
  if (RASignKind != CodeGenOptions::SignReturnAddressScope::None) {
    Fn->addFnAttr("sign-return-address",
                  RASignKind == CodeGenOptions::SignReturnAddressScope::All
                      ? "all"
                      : "non-leaf");
    auto RASignKey = getCodeGenOpts().getSignReturnAddressKey();
    Fn->addFnAttr("sign-return-address-key",
                  RASignKey == CodeGenOptions::SignReturnAddressKeyValue::AKey
                      ? "a_key"
                      : "b_key");
  }

  if (getCodeGenOpts().BranchTargetEnforcement)
    Fn->addFnAttr("branch-target-enforcement");

  return Fn;
}

/// Create a global pointer to a function that will initialize a global
/// variable.  The user has requested that this pointer be emitted in a specific
/// section.
void CodeGenModule::EmitPointerToInitFunc(const VarDecl *D,
                                          llvm::GlobalVariable *GV,
                                          llvm::Function *InitFunc,
                                          InitSegAttr *ISA) {
  llvm::GlobalVariable *PtrArray = new llvm::GlobalVariable(
      TheModule, InitFunc->getType(), /*isConstant=*/true,
      llvm::GlobalValue::PrivateLinkage, InitFunc, "__cxx_init_fn_ptr");
  PtrArray->setSection(ISA->getSection());
  addUsedGlobal(PtrArray);

  // If the GV is already in a comdat group, then we have to join it.
  if (llvm::Comdat *C = GV->getComdat())
    PtrArray->setComdat(C);
}

void
CodeGenModule::EmitCXXGlobalVarDeclInitFunc(const VarDecl *D,
                                            llvm::GlobalVariable *Addr,
                                            bool PerformInit) {

  // According to E.2.3.1 in CUDA-7.5 Programming guide: __device__,
  // __constant__ and __shared__ variables defined in namespace scope,
  // that are of class type, cannot have a non-empty constructor. All
  // the checks have been done in Sema by now. Whatever initializers
  // are allowed are empty and we just need to ignore them here.
  if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
      (D->hasAttr<CUDADeviceAttr>() || D->hasAttr<CUDAConstantAttr>() ||
       D->hasAttr<CUDASharedAttr>()))
    return;

  if (getLangOpts().OpenMP &&
      getOpenMPRuntime().emitDeclareTargetVarDefinition(D, Addr, PerformInit))
    return;

  // Check if we've already initialized this decl.
  auto I = DelayedCXXInitPosition.find(D);
  if (I != DelayedCXXInitPosition.end() && I->second == ~0U)
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    getCXXABI().getMangleContext().mangleDynamicInitializer(D, Out);
  }

  // Create a variable initialization function.
  llvm::Function *Fn =
      CreateGlobalInitOrDestructFunction(FTy, FnName.str(),
                                         getTypes().arrangeNullaryFunction(),
                                         D->getLocation());

  auto *ISA = D->getAttr<InitSegAttr>();
  CodeGenFunction(*this).GenerateCXXGlobalVarDeclInitFunc(Fn, D, Addr,
                                                          PerformInit);

  llvm::GlobalVariable *COMDATKey =
      supportsCOMDAT() && D->isExternallyVisible() ? Addr : nullptr;

  if (D->getTLSKind()) {
    // FIXME: Should we support init_priority for thread_local?
    // FIXME: We only need to register one __cxa_thread_atexit function for the
    // entire TU.
    CXXThreadLocalInits.push_back(Fn);
    CXXThreadLocalInitVars.push_back(D);
  } else if (PerformInit && ISA) {
    EmitPointerToInitFunc(D, Addr, Fn, ISA);
  } else if (auto *IPA = D->getAttr<InitPriorityAttr>()) {
    OrderGlobalInits Key(IPA->getPriority(), PrioritizedCXXGlobalInits.size());
    PrioritizedCXXGlobalInits.push_back(std::make_pair(Key, Fn));
  } else if (isTemplateInstantiation(D->getTemplateSpecializationKind())) {
    // C++ [basic.start.init]p2:
    //   Definitions of explicitly specialized class template static data
    //   members have ordered initialization. Other class template static data
    //   members (i.e., implicitly or explicitly instantiated specializations)
    //   have unordered initialization.
    //
    // As a consequence, we can put them into their own llvm.global_ctors entry.
    //
    // If the global is externally visible, put the initializer into a COMDAT
    // group with the global being initialized.  On most platforms, this is a
    // minor startup time optimization.  In the MS C++ ABI, there are no guard
    // variables, so this COMDAT key is required for correctness.
    AddGlobalCtor(Fn, 65535, COMDATKey);
  } else if (D->hasAttr<SelectAnyAttr>()) {
    // SelectAny globals will be comdat-folded. Put the initializer into a
    // COMDAT group associated with the global, so the initializers get folded
    // too.
    AddGlobalCtor(Fn, 65535, COMDATKey);
  } else {
    I = DelayedCXXInitPosition.find(D); // Re-do lookup in case of re-hash.
    if (I == DelayedCXXInitPosition.end()) {
      CXXGlobalInits.push_back(Fn);
    } else if (I->second != ~0U) {
      assert(I->second < CXXGlobalInits.size() &&
             CXXGlobalInits[I->second] == nullptr);
      CXXGlobalInits[I->second] = Fn;
    }
  }

  // Remember that we already emitted the initializer for this global.
  DelayedCXXInitPosition[D] = ~0U;
}

void CodeGenModule::EmitCXXThreadLocalInitFunc() {
  getCXXABI().EmitThreadLocalInitFuncs(
      *this, CXXThreadLocals, CXXThreadLocalInits, CXXThreadLocalInitVars);

  CXXThreadLocalInits.clear();
  CXXThreadLocalInitVars.clear();
  CXXThreadLocals.clear();
}

void
CodeGenModule::EmitCXXGlobalInitFunc() {
  while (!CXXGlobalInits.empty() && !CXXGlobalInits.back())
    CXXGlobalInits.pop_back();

  if (CXXGlobalInits.empty() && PrioritizedCXXGlobalInits.empty())
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  const CGFunctionInfo &FI = getTypes().arrangeNullaryFunction();

  // Create our global initialization function.
  if (!PrioritizedCXXGlobalInits.empty()) {
    SmallVector<llvm::Function *, 8> LocalCXXGlobalInits;
    llvm::array_pod_sort(PrioritizedCXXGlobalInits.begin(),
                         PrioritizedCXXGlobalInits.end());
    // Iterate over "chunks" of ctors with same priority and emit each chunk
    // into separate function. Note - everything is sorted first by priority,
    // second - by lex order, so we emit ctor functions in proper order.
    for (SmallVectorImpl<GlobalInitData >::iterator
           I = PrioritizedCXXGlobalInits.begin(),
           E = PrioritizedCXXGlobalInits.end(); I != E; ) {
      SmallVectorImpl<GlobalInitData >::iterator
        PrioE = std::upper_bound(I + 1, E, *I, GlobalInitPriorityCmp());

      LocalCXXGlobalInits.clear();
      unsigned Priority = I->first.priority;
      // Compute the function suffix from priority. Prepend with zeroes to make
      // sure the function names are also ordered as priorities.
      std::string PrioritySuffix = llvm::utostr(Priority);
      // Priority is always <= 65535 (enforced by sema).
      PrioritySuffix = std::string(6-PrioritySuffix.size(), '0')+PrioritySuffix;
      llvm::Function *Fn = CreateGlobalInitOrDestructFunction(
          FTy, "_GLOBAL__I_" + PrioritySuffix, FI);

      for (; I < PrioE; ++I)
        LocalCXXGlobalInits.push_back(I->second);

      CodeGenFunction(*this).GenerateCXXGlobalInitFunc(Fn, LocalCXXGlobalInits);
      AddGlobalCtor(Fn, Priority);
    }
    PrioritizedCXXGlobalInits.clear();
  }

  // Include the filename in the symbol name. Including "sub_" matches gcc and
  // makes sure these symbols appear lexicographically behind the symbols with
  // priority emitted above.
  SmallString<128> FileName = llvm::sys::path::filename(getModule().getName());
  if (FileName.empty())
    FileName = "<null>";

  for (size_t i = 0; i < FileName.size(); ++i) {
    // Replace everything that's not [a-zA-Z0-9._] with a _. This set happens
    // to be the set of C preprocessing numbers.
    if (!isPreprocessingNumberBody(FileName[i]))
      FileName[i] = '_';
  }

  llvm::Function *Fn = CreateGlobalInitOrDestructFunction(
      FTy, llvm::Twine("_GLOBAL__sub_I_", FileName), FI);

  CodeGenFunction(*this).GenerateCXXGlobalInitFunc(Fn, CXXGlobalInits);
  AddGlobalCtor(Fn);

  CXXGlobalInits.clear();
}

void CodeGenModule::EmitCXXGlobalDtorFunc() {
  if (CXXGlobalDtors.empty())
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);

  // Create our global destructor function.
  const CGFunctionInfo &FI = getTypes().arrangeNullaryFunction();
  llvm::Function *Fn =
      CreateGlobalInitOrDestructFunction(FTy, "_GLOBAL__D_a", FI);

  CodeGenFunction(*this).GenerateCXXGlobalDtorsFunc(Fn, CXXGlobalDtors);
  AddGlobalDtor(Fn);
}

/// Emit the code necessary to initialize the given global variable.
void CodeGenFunction::GenerateCXXGlobalVarDeclInitFunc(llvm::Function *Fn,
                                                       const VarDecl *D,
                                                 llvm::GlobalVariable *Addr,
                                                       bool PerformInit) {
  // Check if we need to emit debug info for variable initializer.
  if (D->hasAttr<NoDebugAttr>())
    DebugInfo = nullptr; // disable debug info indefinitely for this function

  CurEHLocation = D->getBeginLoc();

  StartFunction(GlobalDecl(D), getContext().VoidTy, Fn,
                getTypes().arrangeNullaryFunction(),
                FunctionArgList(), D->getLocation(),
                D->getInit()->getExprLoc());

  // Use guarded initialization if the global variable is weak. This
  // occurs for, e.g., instantiated static data members and
  // definitions explicitly marked weak.
  if (Addr->hasWeakLinkage() || Addr->hasLinkOnceLinkage()) {
    EmitCXXGuardedInit(*D, Addr, PerformInit);
  } else {
    EmitCXXGlobalVarDeclInit(*D, Addr, PerformInit);
  }

  FinishFunction();
}

void
CodeGenFunction::GenerateCXXGlobalInitFunc(llvm::Function *Fn,
                                           ArrayRef<llvm::Function *> Decls,
                                           ConstantAddress Guard) {
  {
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    StartFunction(GlobalDecl(), getContext().VoidTy, Fn,
                  getTypes().arrangeNullaryFunction(), FunctionArgList());
    // Emit an artificial location for this function.
    auto AL = ApplyDebugLocation::CreateArtificial(*this);

    llvm::BasicBlock *ExitBlock = nullptr;
    if (Guard.isValid()) {
      // If we have a guard variable, check whether we've already performed
      // these initializations. This happens for TLS initialization functions.
      llvm::Value *GuardVal = Builder.CreateLoad(Guard);
      llvm::Value *Uninit = Builder.CreateIsNull(GuardVal,
                                                 "guard.uninitialized");
      llvm::BasicBlock *InitBlock = createBasicBlock("init");
      ExitBlock = createBasicBlock("exit");
      EmitCXXGuardedInitBranch(Uninit, InitBlock, ExitBlock,
                               GuardKind::TlsGuard, nullptr);
      EmitBlock(InitBlock);
      // Mark as initialized before initializing anything else. If the
      // initializers use previously-initialized thread_local vars, that's
      // probably supposed to be OK, but the standard doesn't say.
      Builder.CreateStore(llvm::ConstantInt::get(GuardVal->getType(),1), Guard);

      // The guard variable can't ever change again.
      EmitInvariantStart(
          Guard.getPointer(),
          CharUnits::fromQuantity(
              CGM.getDataLayout().getTypeAllocSize(GuardVal->getType())));
    }

    RunCleanupsScope Scope(*this);

    // When building in Objective-C++ ARC mode, create an autorelease pool
    // around the global initializers.
    if (getLangOpts().ObjCAutoRefCount && getLangOpts().CPlusPlus) {
      llvm::Value *token = EmitObjCAutoreleasePoolPush();
      EmitObjCAutoreleasePoolCleanup(token);
    }

    for (unsigned i = 0, e = Decls.size(); i != e; ++i)
      if (Decls[i])
        EmitRuntimeCall(Decls[i]);

    Scope.ForceCleanup();

    if (ExitBlock) {
      Builder.CreateBr(ExitBlock);
      EmitBlock(ExitBlock);
    }
  }

  FinishFunction();
}

void CodeGenFunction::GenerateCXXGlobalDtorsFunc(
    llvm::Function *Fn,
    const std::vector<std::pair<llvm::WeakTrackingVH, llvm::Constant *>>
        &DtorsAndObjects) {
  {
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    StartFunction(GlobalDecl(), getContext().VoidTy, Fn,
                  getTypes().arrangeNullaryFunction(), FunctionArgList());
    // Emit an artificial location for this function.
    auto AL = ApplyDebugLocation::CreateArtificial(*this);

    // Emit the dtors, in reverse order from construction.
    for (unsigned i = 0, e = DtorsAndObjects.size(); i != e; ++i) {
      llvm::Value *Callee = DtorsAndObjects[e - i - 1].first;
      llvm::CallInst *CI = Builder.CreateCall(Callee,
                                          DtorsAndObjects[e - i - 1].second);
      // Make sure the call and the callee agree on calling convention.
      if (llvm::Function *F = dyn_cast<llvm::Function>(Callee))
        CI->setCallingConv(F->getCallingConv());
    }
  }

  FinishFunction();
}

/// generateDestroyHelper - Generates a helper function which, when
/// invoked, destroys the given object.  The address of the object
/// should be in global memory.
llvm::Function *CodeGenFunction::generateDestroyHelper(
    Address addr, QualType type, Destroyer *destroyer,
    bool useEHCleanupForArray, const VarDecl *VD) {
  FunctionArgList args;
  ImplicitParamDecl Dst(getContext(), getContext().VoidPtrTy,
                        ImplicitParamDecl::Other);
  args.push_back(&Dst);

  const CGFunctionInfo &FI =
    CGM.getTypes().arrangeBuiltinFunctionDeclaration(getContext().VoidTy, args);
  llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
  llvm::Function *fn = CGM.CreateGlobalInitOrDestructFunction(
      FTy, "__cxx_global_array_dtor", FI, VD->getLocation());

  CurEHLocation = VD->getBeginLoc();

  StartFunction(VD, getContext().VoidTy, fn, FI, args);

  emitDestroy(addr, type, destroyer, useEHCleanupForArray);

  FinishFunction();

  return fn;
}
