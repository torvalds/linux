//===--- CodeGenFunction.cpp - Emit LLVM Code from ASTs for a Function ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-function state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CGBlocks.h"
#include "CGCleanup.h"
#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGDebugInfo.h"
#include "CGOpenMPRuntime.h"
#include "CodeGenModule.h"
#include "CodeGenPGO.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Operator.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
using namespace clang;
using namespace CodeGen;

/// shouldEmitLifetimeMarkers - Decide whether we need emit the life-time
/// markers.
static bool shouldEmitLifetimeMarkers(const CodeGenOptions &CGOpts,
                                      const LangOptions &LangOpts) {
  if (CGOpts.DisableLifetimeMarkers)
    return false;

  // Disable lifetime markers in msan builds.
  // FIXME: Remove this when msan works with lifetime markers.
  if (LangOpts.Sanitize.has(SanitizerKind::Memory))
    return false;

  // Asan uses markers for use-after-scope checks.
  if (CGOpts.SanitizeAddressUseAfterScope)
    return true;

  // For now, only in optimized builds.
  return CGOpts.OptimizationLevel != 0;
}

CodeGenFunction::CodeGenFunction(CodeGenModule &cgm, bool suppressNewContext)
    : CodeGenTypeCache(cgm), CGM(cgm), Target(cgm.getTarget()),
      Builder(cgm, cgm.getModule().getContext(), llvm::ConstantFolder(),
              CGBuilderInserterTy(this)),
      SanOpts(CGM.getLangOpts().Sanitize), DebugInfo(CGM.getModuleDebugInfo()),
      PGO(cgm), ShouldEmitLifetimeMarkers(shouldEmitLifetimeMarkers(
                    CGM.getCodeGenOpts(), CGM.getLangOpts())) {
  if (!suppressNewContext)
    CGM.getCXXABI().getMangleContext().startNewFunction();

  llvm::FastMathFlags FMF;
  if (CGM.getLangOpts().FastMath)
    FMF.setFast();
  if (CGM.getLangOpts().FiniteMathOnly) {
    FMF.setNoNaNs();
    FMF.setNoInfs();
  }
  if (CGM.getCodeGenOpts().NoNaNsFPMath) {
    FMF.setNoNaNs();
  }
  if (CGM.getCodeGenOpts().NoSignedZeros) {
    FMF.setNoSignedZeros();
  }
  if (CGM.getCodeGenOpts().ReciprocalMath) {
    FMF.setAllowReciprocal();
  }
  if (CGM.getCodeGenOpts().Reassociate) {
    FMF.setAllowReassoc();
  }
  Builder.setFastMathFlags(FMF);
}

CodeGenFunction::~CodeGenFunction() {
  assert(LifetimeExtendedCleanupStack.empty() && "failed to emit a cleanup");

  // If there are any unclaimed block infos, go ahead and destroy them
  // now.  This can happen if IR-gen gets clever and skips evaluating
  // something.
  if (FirstBlockInfo)
    destroyBlockInfos(FirstBlockInfo);

  if (getLangOpts().OpenMP && CurFn)
    CGM.getOpenMPRuntime().functionFinished(*this);
}

CharUnits CodeGenFunction::getNaturalPointeeTypeAlignment(QualType T,
                                                    LValueBaseInfo *BaseInfo,
                                                    TBAAAccessInfo *TBAAInfo) {
  return getNaturalTypeAlignment(T->getPointeeType(), BaseInfo, TBAAInfo,
                                 /* forPointeeType= */ true);
}

CharUnits CodeGenFunction::getNaturalTypeAlignment(QualType T,
                                                   LValueBaseInfo *BaseInfo,
                                                   TBAAAccessInfo *TBAAInfo,
                                                   bool forPointeeType) {
  if (TBAAInfo)
    *TBAAInfo = CGM.getTBAAAccessInfo(T);

  // Honor alignment typedef attributes even on incomplete types.
  // We also honor them straight for C++ class types, even as pointees;
  // there's an expressivity gap here.
  if (auto TT = T->getAs<TypedefType>()) {
    if (auto Align = TT->getDecl()->getMaxAlignment()) {
      if (BaseInfo)
        *BaseInfo = LValueBaseInfo(AlignmentSource::AttributedType);
      return getContext().toCharUnitsFromBits(Align);
    }
  }

  if (BaseInfo)
    *BaseInfo = LValueBaseInfo(AlignmentSource::Type);

  CharUnits Alignment;
  if (T->isIncompleteType()) {
    Alignment = CharUnits::One(); // Shouldn't be used, but pessimistic is best.
  } else {
    // For C++ class pointees, we don't know whether we're pointing at a
    // base or a complete object, so we generally need to use the
    // non-virtual alignment.
    const CXXRecordDecl *RD;
    if (forPointeeType && (RD = T->getAsCXXRecordDecl())) {
      Alignment = CGM.getClassPointerAlignment(RD);
    } else {
      Alignment = getContext().getTypeAlignInChars(T);
      if (T.getQualifiers().hasUnaligned())
        Alignment = CharUnits::One();
    }

    // Cap to the global maximum type alignment unless the alignment
    // was somehow explicit on the type.
    if (unsigned MaxAlign = getLangOpts().MaxTypeAlign) {
      if (Alignment.getQuantity() > MaxAlign &&
          !getContext().isAlignmentRequired(T))
        Alignment = CharUnits::fromQuantity(MaxAlign);
    }
  }
  return Alignment;
}

LValue CodeGenFunction::MakeNaturalAlignAddrLValue(llvm::Value *V, QualType T) {
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  CharUnits Alignment = getNaturalTypeAlignment(T, &BaseInfo, &TBAAInfo);
  return LValue::MakeAddr(Address(V, Alignment), T, getContext(), BaseInfo,
                          TBAAInfo);
}

/// Given a value of type T* that may not be to a complete object,
/// construct an l-value with the natural pointee alignment of T.
LValue
CodeGenFunction::MakeNaturalAlignPointeeAddrLValue(llvm::Value *V, QualType T) {
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  CharUnits Align = getNaturalTypeAlignment(T, &BaseInfo, &TBAAInfo,
                                            /* forPointeeType= */ true);
  return MakeAddrLValue(Address(V, Align), T, BaseInfo, TBAAInfo);
}


llvm::Type *CodeGenFunction::ConvertTypeForMem(QualType T) {
  return CGM.getTypes().ConvertTypeForMem(T);
}

llvm::Type *CodeGenFunction::ConvertType(QualType T) {
  return CGM.getTypes().ConvertType(T);
}

TypeEvaluationKind CodeGenFunction::getEvaluationKind(QualType type) {
  type = type.getCanonicalType();
  while (true) {
    switch (type->getTypeClass()) {
#define TYPE(name, parent)
#define ABSTRACT_TYPE(name, parent)
#define NON_CANONICAL_TYPE(name, parent) case Type::name:
#define DEPENDENT_TYPE(name, parent) case Type::name:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(name, parent) case Type::name:
#include "clang/AST/TypeNodes.def"
      llvm_unreachable("non-canonical or dependent type in IR-generation");

    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      llvm_unreachable("undeduced type in IR-generation");

    // Various scalar types.
    case Type::Builtin:
    case Type::Pointer:
    case Type::BlockPointer:
    case Type::LValueReference:
    case Type::RValueReference:
    case Type::MemberPointer:
    case Type::Vector:
    case Type::ExtVector:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
    case Type::Enum:
    case Type::ObjCObjectPointer:
    case Type::Pipe:
      return TEK_Scalar;

    // Complexes.
    case Type::Complex:
      return TEK_Complex;

    // Arrays, records, and Objective-C objects.
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::Record:
    case Type::ObjCObject:
    case Type::ObjCInterface:
      return TEK_Aggregate;

    // We operate on atomic values according to their underlying type.
    case Type::Atomic:
      type = cast<AtomicType>(type)->getValueType();
      continue;
    }
    llvm_unreachable("unknown type kind!");
  }
}

llvm::DebugLoc CodeGenFunction::EmitReturnBlock() {
  // For cleanliness, we try to avoid emitting the return block for
  // simple cases.
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  if (CurBB) {
    assert(!CurBB->getTerminator() && "Unexpected terminated block.");

    // We have a valid insert point, reuse it if it is empty or there are no
    // explicit jumps to the return block.
    if (CurBB->empty() || ReturnBlock.getBlock()->use_empty()) {
      ReturnBlock.getBlock()->replaceAllUsesWith(CurBB);
      delete ReturnBlock.getBlock();
    } else
      EmitBlock(ReturnBlock.getBlock());
    return llvm::DebugLoc();
  }

  // Otherwise, if the return block is the target of a single direct
  // branch then we can just put the code in that block instead. This
  // cleans up functions which started with a unified return block.
  if (ReturnBlock.getBlock()->hasOneUse()) {
    llvm::BranchInst *BI =
      dyn_cast<llvm::BranchInst>(*ReturnBlock.getBlock()->user_begin());
    if (BI && BI->isUnconditional() &&
        BI->getSuccessor(0) == ReturnBlock.getBlock()) {
      // Record/return the DebugLoc of the simple 'return' expression to be used
      // later by the actual 'ret' instruction.
      llvm::DebugLoc Loc = BI->getDebugLoc();
      Builder.SetInsertPoint(BI->getParent());
      BI->eraseFromParent();
      delete ReturnBlock.getBlock();
      return Loc;
    }
  }

  // FIXME: We are at an unreachable point, there is no reason to emit the block
  // unless it has uses. However, we still need a place to put the debug
  // region.end for now.

  EmitBlock(ReturnBlock.getBlock());
  return llvm::DebugLoc();
}

static void EmitIfUsed(CodeGenFunction &CGF, llvm::BasicBlock *BB) {
  if (!BB) return;
  if (!BB->use_empty())
    return CGF.CurFn->getBasicBlockList().push_back(BB);
  delete BB;
}

void CodeGenFunction::FinishFunction(SourceLocation EndLoc) {
  assert(BreakContinueStack.empty() &&
         "mismatched push/pop in break/continue stack!");

  bool OnlySimpleReturnStmts = NumSimpleReturnExprs > 0
    && NumSimpleReturnExprs == NumReturnExprs
    && ReturnBlock.getBlock()->use_empty();
  // Usually the return expression is evaluated before the cleanup
  // code.  If the function contains only a simple return statement,
  // such as a constant, the location before the cleanup code becomes
  // the last useful breakpoint in the function, because the simple
  // return expression will be evaluated after the cleanup code. To be
  // safe, set the debug location for cleanup code to the location of
  // the return statement.  Otherwise the cleanup code should be at the
  // end of the function's lexical scope.
  //
  // If there are multiple branches to the return block, the branch
  // instructions will get the location of the return statements and
  // all will be fine.
  if (CGDebugInfo *DI = getDebugInfo()) {
    if (OnlySimpleReturnStmts)
      DI->EmitLocation(Builder, LastStopPoint);
    else
      DI->EmitLocation(Builder, EndLoc);
  }

  // Pop any cleanups that might have been associated with the
  // parameters.  Do this in whatever block we're currently in; it's
  // important to do this before we enter the return block or return
  // edges will be *really* confused.
  bool HasCleanups = EHStack.stable_begin() != PrologueCleanupDepth;
  bool HasOnlyLifetimeMarkers =
      HasCleanups && EHStack.containsOnlyLifetimeMarkers(PrologueCleanupDepth);
  bool EmitRetDbgLoc = !HasCleanups || HasOnlyLifetimeMarkers;
  if (HasCleanups) {
    // Make sure the line table doesn't jump back into the body for
    // the ret after it's been at EndLoc.
    if (CGDebugInfo *DI = getDebugInfo())
      if (OnlySimpleReturnStmts)
        DI->EmitLocation(Builder, EndLoc);

    PopCleanupBlocks(PrologueCleanupDepth);
  }

  // Emit function epilog (to return).
  llvm::DebugLoc Loc = EmitReturnBlock();

  if (ShouldInstrumentFunction()) {
    if (CGM.getCodeGenOpts().InstrumentFunctions)
      CurFn->addFnAttr("instrument-function-exit", "__cyg_profile_func_exit");
    if (CGM.getCodeGenOpts().InstrumentFunctionsAfterInlining)
      CurFn->addFnAttr("instrument-function-exit-inlined",
                       "__cyg_profile_func_exit");
  }

  // Emit debug descriptor for function end.
  if (CGDebugInfo *DI = getDebugInfo())
    DI->EmitFunctionEnd(Builder, CurFn);

  // Reset the debug location to that of the simple 'return' expression, if any
  // rather than that of the end of the function's scope '}'.
  ApplyDebugLocation AL(*this, Loc);
  EmitFunctionEpilog(*CurFnInfo, EmitRetDbgLoc, EndLoc);
  EmitEndEHSpec(CurCodeDecl);

  assert(EHStack.empty() &&
         "did not remove all scopes from cleanup stack!");

  // If someone did an indirect goto, emit the indirect goto block at the end of
  // the function.
  if (IndirectBranch) {
    EmitBlock(IndirectBranch->getParent());
    Builder.ClearInsertionPoint();
  }

  // If some of our locals escaped, insert a call to llvm.localescape in the
  // entry block.
  if (!EscapedLocals.empty()) {
    // Invert the map from local to index into a simple vector. There should be
    // no holes.
    SmallVector<llvm::Value *, 4> EscapeArgs;
    EscapeArgs.resize(EscapedLocals.size());
    for (auto &Pair : EscapedLocals)
      EscapeArgs[Pair.second] = Pair.first;
    llvm::Function *FrameEscapeFn = llvm::Intrinsic::getDeclaration(
        &CGM.getModule(), llvm::Intrinsic::localescape);
    CGBuilderTy(*this, AllocaInsertPt).CreateCall(FrameEscapeFn, EscapeArgs);
  }

  // Remove the AllocaInsertPt instruction, which is just a convenience for us.
  llvm::Instruction *Ptr = AllocaInsertPt;
  AllocaInsertPt = nullptr;
  Ptr->eraseFromParent();

  // If someone took the address of a label but never did an indirect goto, we
  // made a zero entry PHI node, which is illegal, zap it now.
  if (IndirectBranch) {
    llvm::PHINode *PN = cast<llvm::PHINode>(IndirectBranch->getAddress());
    if (PN->getNumIncomingValues() == 0) {
      PN->replaceAllUsesWith(llvm::UndefValue::get(PN->getType()));
      PN->eraseFromParent();
    }
  }

  EmitIfUsed(*this, EHResumeBlock);
  EmitIfUsed(*this, TerminateLandingPad);
  EmitIfUsed(*this, TerminateHandler);
  EmitIfUsed(*this, UnreachableBlock);

  for (const auto &FuncletAndParent : TerminateFunclets)
    EmitIfUsed(*this, FuncletAndParent.second);

  if (CGM.getCodeGenOpts().EmitDeclMetadata)
    EmitDeclMetadata();

  for (SmallVectorImpl<std::pair<llvm::Instruction *, llvm::Value *> >::iterator
           I = DeferredReplacements.begin(),
           E = DeferredReplacements.end();
       I != E; ++I) {
    I->first->replaceAllUsesWith(I->second);
    I->first->eraseFromParent();
  }

  // Eliminate CleanupDestSlot alloca by replacing it with SSA values and
  // PHIs if the current function is a coroutine. We don't do it for all
  // functions as it may result in slight increase in numbers of instructions
  // if compiled with no optimizations. We do it for coroutine as the lifetime
  // of CleanupDestSlot alloca make correct coroutine frame building very
  // difficult.
  if (NormalCleanupDest.isValid() && isCoroutine()) {
    llvm::DominatorTree DT(*CurFn);
    llvm::PromoteMemToReg(
        cast<llvm::AllocaInst>(NormalCleanupDest.getPointer()), DT);
    NormalCleanupDest = Address::invalid();
  }

  // Scan function arguments for vector width.
  for (llvm::Argument &A : CurFn->args())
    if (auto *VT = dyn_cast<llvm::VectorType>(A.getType()))
      LargestVectorWidth = std::max(LargestVectorWidth,
                                    VT->getPrimitiveSizeInBits());

  // Update vector width based on return type.
  if (auto *VT = dyn_cast<llvm::VectorType>(CurFn->getReturnType()))
    LargestVectorWidth = std::max(LargestVectorWidth,
                                  VT->getPrimitiveSizeInBits());

  // Add the required-vector-width attribute. This contains the max width from:
  // 1. min-vector-width attribute used in the source program.
  // 2. Any builtins used that have a vector width specified.
  // 3. Values passed in and out of inline assembly.
  // 4. Width of vector arguments and return types for this function.
  // 5. Width of vector aguments and return types for functions called by this
  //    function.
  CurFn->addFnAttr("min-legal-vector-width", llvm::utostr(LargestVectorWidth));
}

/// ShouldInstrumentFunction - Return true if the current function should be
/// instrumented with __cyg_profile_func_* calls
bool CodeGenFunction::ShouldInstrumentFunction() {
  if (!CGM.getCodeGenOpts().InstrumentFunctions &&
      !CGM.getCodeGenOpts().InstrumentFunctionsAfterInlining &&
      !CGM.getCodeGenOpts().InstrumentFunctionEntryBare)
    return false;
  if (!CurFuncDecl || CurFuncDecl->hasAttr<NoInstrumentFunctionAttr>())
    return false;
  return true;
}

/// ShouldXRayInstrument - Return true if the current function should be
/// instrumented with XRay nop sleds.
bool CodeGenFunction::ShouldXRayInstrumentFunction() const {
  return CGM.getCodeGenOpts().XRayInstrumentFunctions;
}

/// AlwaysEmitXRayCustomEvents - Return true if we should emit IR for calls to
/// the __xray_customevent(...) builtin calls, when doing XRay instrumentation.
bool CodeGenFunction::AlwaysEmitXRayCustomEvents() const {
  return CGM.getCodeGenOpts().XRayInstrumentFunctions &&
         (CGM.getCodeGenOpts().XRayAlwaysEmitCustomEvents ||
          CGM.getCodeGenOpts().XRayInstrumentationBundle.Mask ==
              XRayInstrKind::Custom);
}

bool CodeGenFunction::AlwaysEmitXRayTypedEvents() const {
  return CGM.getCodeGenOpts().XRayInstrumentFunctions &&
         (CGM.getCodeGenOpts().XRayAlwaysEmitTypedEvents ||
          CGM.getCodeGenOpts().XRayInstrumentationBundle.Mask ==
              XRayInstrKind::Typed);
}

llvm::Constant *
CodeGenFunction::EncodeAddrForUseInPrologue(llvm::Function *F,
                                            llvm::Constant *Addr) {
  // Addresses stored in prologue data can't require run-time fixups and must
  // be PC-relative. Run-time fixups are undesirable because they necessitate
  // writable text segments, which are unsafe. And absolute addresses are
  // undesirable because they break PIE mode.

  // Add a layer of indirection through a private global. Taking its address
  // won't result in a run-time fixup, even if Addr has linkonce_odr linkage.
  auto *GV = new llvm::GlobalVariable(CGM.getModule(), Addr->getType(),
                                      /*isConstant=*/true,
                                      llvm::GlobalValue::PrivateLinkage, Addr);

  // Create a PC-relative address.
  auto *GOTAsInt = llvm::ConstantExpr::getPtrToInt(GV, IntPtrTy);
  auto *FuncAsInt = llvm::ConstantExpr::getPtrToInt(F, IntPtrTy);
  auto *PCRelAsInt = llvm::ConstantExpr::getSub(GOTAsInt, FuncAsInt);
  return (IntPtrTy == Int32Ty)
             ? PCRelAsInt
             : llvm::ConstantExpr::getTrunc(PCRelAsInt, Int32Ty);
}

llvm::Value *
CodeGenFunction::DecodeAddrUsedInPrologue(llvm::Value *F,
                                          llvm::Value *EncodedAddr) {
  // Reconstruct the address of the global.
  auto *PCRelAsInt = Builder.CreateSExt(EncodedAddr, IntPtrTy);
  auto *FuncAsInt = Builder.CreatePtrToInt(F, IntPtrTy, "func_addr.int");
  auto *GOTAsInt = Builder.CreateAdd(PCRelAsInt, FuncAsInt, "global_addr.int");
  auto *GOTAddr = Builder.CreateIntToPtr(GOTAsInt, Int8PtrPtrTy, "global_addr");

  // Load the original pointer through the global.
  return Builder.CreateLoad(Address(GOTAddr, getPointerAlign()),
                            "decoded_addr");
}

static void removeImageAccessQualifier(std::string& TyName) {
  std::string ReadOnlyQual("__read_only");
  std::string::size_type ReadOnlyPos = TyName.find(ReadOnlyQual);
  if (ReadOnlyPos != std::string::npos)
    // "+ 1" for the space after access qualifier.
    TyName.erase(ReadOnlyPos, ReadOnlyQual.size() + 1);
  else {
    std::string WriteOnlyQual("__write_only");
    std::string::size_type WriteOnlyPos = TyName.find(WriteOnlyQual);
    if (WriteOnlyPos != std::string::npos)
      TyName.erase(WriteOnlyPos, WriteOnlyQual.size() + 1);
    else {
      std::string ReadWriteQual("__read_write");
      std::string::size_type ReadWritePos = TyName.find(ReadWriteQual);
      if (ReadWritePos != std::string::npos)
        TyName.erase(ReadWritePos, ReadWriteQual.size() + 1);
    }
  }
}

// Returns the address space id that should be produced to the
// kernel_arg_addr_space metadata. This is always fixed to the ids
// as specified in the SPIR 2.0 specification in order to differentiate
// for example in clGetKernelArgInfo() implementation between the address
// spaces with targets without unique mapping to the OpenCL address spaces
// (basically all single AS CPUs).
static unsigned ArgInfoAddressSpace(LangAS AS) {
  switch (AS) {
  case LangAS::opencl_global:   return 1;
  case LangAS::opencl_constant: return 2;
  case LangAS::opencl_local:    return 3;
  case LangAS::opencl_generic:  return 4; // Not in SPIR 2.0 specs.
  default:
    return 0; // Assume private.
  }
}

// OpenCL v1.2 s5.6.4.6 allows the compiler to store kernel argument
// information in the program executable. The argument information stored
// includes the argument name, its type, the address and access qualifiers used.
static void GenOpenCLArgMetadata(const FunctionDecl *FD, llvm::Function *Fn,
                                 CodeGenModule &CGM, llvm::LLVMContext &Context,
                                 CGBuilderTy &Builder, ASTContext &ASTCtx) {
  // Create MDNodes that represent the kernel arg metadata.
  // Each MDNode is a list in the form of "key", N number of values which is
  // the same number of values as their are kernel arguments.

  const PrintingPolicy &Policy = ASTCtx.getPrintingPolicy();

  // MDNode for the kernel argument address space qualifiers.
  SmallVector<llvm::Metadata *, 8> addressQuals;

  // MDNode for the kernel argument access qualifiers (images only).
  SmallVector<llvm::Metadata *, 8> accessQuals;

  // MDNode for the kernel argument type names.
  SmallVector<llvm::Metadata *, 8> argTypeNames;

  // MDNode for the kernel argument base type names.
  SmallVector<llvm::Metadata *, 8> argBaseTypeNames;

  // MDNode for the kernel argument type qualifiers.
  SmallVector<llvm::Metadata *, 8> argTypeQuals;

  // MDNode for the kernel argument names.
  SmallVector<llvm::Metadata *, 8> argNames;

  for (unsigned i = 0, e = FD->getNumParams(); i != e; ++i) {
    const ParmVarDecl *parm = FD->getParamDecl(i);
    QualType ty = parm->getType();
    std::string typeQuals;

    if (ty->isPointerType()) {
      QualType pointeeTy = ty->getPointeeType();

      // Get address qualifier.
      addressQuals.push_back(llvm::ConstantAsMetadata::get(Builder.getInt32(
        ArgInfoAddressSpace(pointeeTy.getAddressSpace()))));

      // Get argument type name.
      std::string typeName =
          pointeeTy.getUnqualifiedType().getAsString(Policy) + "*";

      // Turn "unsigned type" to "utype"
      std::string::size_type pos = typeName.find("unsigned");
      if (pointeeTy.isCanonical() && pos != std::string::npos)
        typeName.erase(pos+1, 8);

      argTypeNames.push_back(llvm::MDString::get(Context, typeName));

      std::string baseTypeName =
          pointeeTy.getUnqualifiedType().getCanonicalType().getAsString(
              Policy) +
          "*";

      // Turn "unsigned type" to "utype"
      pos = baseTypeName.find("unsigned");
      if (pos != std::string::npos)
        baseTypeName.erase(pos+1, 8);

      argBaseTypeNames.push_back(llvm::MDString::get(Context, baseTypeName));

      // Get argument type qualifiers:
      if (ty.isRestrictQualified())
        typeQuals = "restrict";
      if (pointeeTy.isConstQualified() ||
          (pointeeTy.getAddressSpace() == LangAS::opencl_constant))
        typeQuals += typeQuals.empty() ? "const" : " const";
      if (pointeeTy.isVolatileQualified())
        typeQuals += typeQuals.empty() ? "volatile" : " volatile";
    } else {
      uint32_t AddrSpc = 0;
      bool isPipe = ty->isPipeType();
      if (ty->isImageType() || isPipe)
        AddrSpc = ArgInfoAddressSpace(LangAS::opencl_global);

      addressQuals.push_back(
          llvm::ConstantAsMetadata::get(Builder.getInt32(AddrSpc)));

      // Get argument type name.
      std::string typeName;
      if (isPipe)
        typeName = ty.getCanonicalType()->getAs<PipeType>()->getElementType()
                     .getAsString(Policy);
      else
        typeName = ty.getUnqualifiedType().getAsString(Policy);

      // Turn "unsigned type" to "utype"
      std::string::size_type pos = typeName.find("unsigned");
      if (ty.isCanonical() && pos != std::string::npos)
        typeName.erase(pos+1, 8);

      std::string baseTypeName;
      if (isPipe)
        baseTypeName = ty.getCanonicalType()->getAs<PipeType>()
                          ->getElementType().getCanonicalType()
                          .getAsString(Policy);
      else
        baseTypeName =
          ty.getUnqualifiedType().getCanonicalType().getAsString(Policy);

      // Remove access qualifiers on images
      // (as they are inseparable from type in clang implementation,
      // but OpenCL spec provides a special query to get access qualifier
      // via clGetKernelArgInfo with CL_KERNEL_ARG_ACCESS_QUALIFIER):
      if (ty->isImageType()) {
        removeImageAccessQualifier(typeName);
        removeImageAccessQualifier(baseTypeName);
      }

      argTypeNames.push_back(llvm::MDString::get(Context, typeName));

      // Turn "unsigned type" to "utype"
      pos = baseTypeName.find("unsigned");
      if (pos != std::string::npos)
        baseTypeName.erase(pos+1, 8);

      argBaseTypeNames.push_back(llvm::MDString::get(Context, baseTypeName));

      if (isPipe)
        typeQuals = "pipe";
    }

    argTypeQuals.push_back(llvm::MDString::get(Context, typeQuals));

    // Get image and pipe access qualifier:
    if (ty->isImageType()|| ty->isPipeType()) {
      const Decl *PDecl = parm;
      if (auto *TD = dyn_cast<TypedefType>(ty))
        PDecl = TD->getDecl();
      const OpenCLAccessAttr *A = PDecl->getAttr<OpenCLAccessAttr>();
      if (A && A->isWriteOnly())
        accessQuals.push_back(llvm::MDString::get(Context, "write_only"));
      else if (A && A->isReadWrite())
        accessQuals.push_back(llvm::MDString::get(Context, "read_write"));
      else
        accessQuals.push_back(llvm::MDString::get(Context, "read_only"));
    } else
      accessQuals.push_back(llvm::MDString::get(Context, "none"));

    // Get argument name.
    argNames.push_back(llvm::MDString::get(Context, parm->getName()));
  }

  Fn->setMetadata("kernel_arg_addr_space",
                  llvm::MDNode::get(Context, addressQuals));
  Fn->setMetadata("kernel_arg_access_qual",
                  llvm::MDNode::get(Context, accessQuals));
  Fn->setMetadata("kernel_arg_type",
                  llvm::MDNode::get(Context, argTypeNames));
  Fn->setMetadata("kernel_arg_base_type",
                  llvm::MDNode::get(Context, argBaseTypeNames));
  Fn->setMetadata("kernel_arg_type_qual",
                  llvm::MDNode::get(Context, argTypeQuals));
  if (CGM.getCodeGenOpts().EmitOpenCLArgMetadata)
    Fn->setMetadata("kernel_arg_name",
                    llvm::MDNode::get(Context, argNames));
}

void CodeGenFunction::EmitOpenCLKernelMetadata(const FunctionDecl *FD,
                                               llvm::Function *Fn)
{
  if (!FD->hasAttr<OpenCLKernelAttr>())
    return;

  llvm::LLVMContext &Context = getLLVMContext();

  GenOpenCLArgMetadata(FD, Fn, CGM, Context, Builder, getContext());

  if (const VecTypeHintAttr *A = FD->getAttr<VecTypeHintAttr>()) {
    QualType HintQTy = A->getTypeHint();
    const ExtVectorType *HintEltQTy = HintQTy->getAs<ExtVectorType>();
    bool IsSignedInteger =
        HintQTy->isSignedIntegerType() ||
        (HintEltQTy && HintEltQTy->getElementType()->isSignedIntegerType());
    llvm::Metadata *AttrMDArgs[] = {
        llvm::ConstantAsMetadata::get(llvm::UndefValue::get(
            CGM.getTypes().ConvertType(A->getTypeHint()))),
        llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
            llvm::IntegerType::get(Context, 32),
            llvm::APInt(32, (uint64_t)(IsSignedInteger ? 1 : 0))))};
    Fn->setMetadata("vec_type_hint", llvm::MDNode::get(Context, AttrMDArgs));
  }

  if (const WorkGroupSizeHintAttr *A = FD->getAttr<WorkGroupSizeHintAttr>()) {
    llvm::Metadata *AttrMDArgs[] = {
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getXDim())),
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getYDim())),
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getZDim()))};
    Fn->setMetadata("work_group_size_hint", llvm::MDNode::get(Context, AttrMDArgs));
  }

  if (const ReqdWorkGroupSizeAttr *A = FD->getAttr<ReqdWorkGroupSizeAttr>()) {
    llvm::Metadata *AttrMDArgs[] = {
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getXDim())),
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getYDim())),
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getZDim()))};
    Fn->setMetadata("reqd_work_group_size", llvm::MDNode::get(Context, AttrMDArgs));
  }

  if (const OpenCLIntelReqdSubGroupSizeAttr *A =
          FD->getAttr<OpenCLIntelReqdSubGroupSizeAttr>()) {
    llvm::Metadata *AttrMDArgs[] = {
        llvm::ConstantAsMetadata::get(Builder.getInt32(A->getSubGroupSize()))};
    Fn->setMetadata("intel_reqd_sub_group_size",
                    llvm::MDNode::get(Context, AttrMDArgs));
  }
}

/// Determine whether the function F ends with a return stmt.
static bool endsWithReturn(const Decl* F) {
  const Stmt *Body = nullptr;
  if (auto *FD = dyn_cast_or_null<FunctionDecl>(F))
    Body = FD->getBody();
  else if (auto *OMD = dyn_cast_or_null<ObjCMethodDecl>(F))
    Body = OMD->getBody();

  if (auto *CS = dyn_cast_or_null<CompoundStmt>(Body)) {
    auto LastStmt = CS->body_rbegin();
    if (LastStmt != CS->body_rend())
      return isa<ReturnStmt>(*LastStmt);
  }
  return false;
}

void CodeGenFunction::markAsIgnoreThreadCheckingAtRuntime(llvm::Function *Fn) {
  if (SanOpts.has(SanitizerKind::Thread)) {
    Fn->addFnAttr("sanitize_thread_no_checking_at_run_time");
    Fn->removeFnAttr(llvm::Attribute::SanitizeThread);
  }
}

static bool matchesStlAllocatorFn(const Decl *D, const ASTContext &Ctx) {
  auto *MD = dyn_cast_or_null<CXXMethodDecl>(D);
  if (!MD || !MD->getDeclName().getAsIdentifierInfo() ||
      !MD->getDeclName().getAsIdentifierInfo()->isStr("allocate") ||
      (MD->getNumParams() != 1 && MD->getNumParams() != 2))
    return false;

  if (MD->parameters()[0]->getType().getCanonicalType() != Ctx.getSizeType())
    return false;

  if (MD->getNumParams() == 2) {
    auto *PT = MD->parameters()[1]->getType()->getAs<PointerType>();
    if (!PT || !PT->isVoidPointerType() ||
        !PT->getPointeeType().isConstQualified())
      return false;
  }

  return true;
}

/// Return the UBSan prologue signature for \p FD if one is available.
static llvm::Constant *getPrologueSignature(CodeGenModule &CGM,
                                            const FunctionDecl *FD) {
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (!MD->isStatic())
      return nullptr;
  return CGM.getTargetCodeGenInfo().getUBSanFunctionSignature(CGM);
}

void CodeGenFunction::StartFunction(GlobalDecl GD,
                                    QualType RetTy,
                                    llvm::Function *Fn,
                                    const CGFunctionInfo &FnInfo,
                                    const FunctionArgList &Args,
                                    SourceLocation Loc,
                                    SourceLocation StartLoc) {
  assert(!CurFn &&
         "Do not use a CodeGenFunction object for more than one function");

  const Decl *D = GD.getDecl();

  DidCallStackSave = false;
  CurCodeDecl = D;
  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(D))
    if (FD->usesSEHTry())
      CurSEHParent = FD;
  CurFuncDecl = (D ? D->getNonClosureContext() : nullptr);
  FnRetTy = RetTy;
  CurFn = Fn;
  CurFnInfo = &FnInfo;
  assert(CurFn->isDeclaration() && "Function already has body?");

  // If this function has been blacklisted for any of the enabled sanitizers,
  // disable the sanitizer for the function.
  do {
#define SANITIZER(NAME, ID)                                                    \
  if (SanOpts.empty())                                                         \
    break;                                                                     \
  if (SanOpts.has(SanitizerKind::ID))                                          \
    if (CGM.isInSanitizerBlacklist(SanitizerKind::ID, Fn, Loc))                \
      SanOpts.set(SanitizerKind::ID, false);

#include "clang/Basic/Sanitizers.def"
#undef SANITIZER
  } while (0);

  if (D) {
    // Apply the no_sanitize* attributes to SanOpts.
    for (auto Attr : D->specific_attrs<NoSanitizeAttr>()) {
      SanitizerMask mask = Attr->getMask();
      SanOpts.Mask &= ~mask;
      if (mask & SanitizerKind::Address)
        SanOpts.set(SanitizerKind::KernelAddress, false);
      if (mask & SanitizerKind::KernelAddress)
        SanOpts.set(SanitizerKind::Address, false);
      if (mask & SanitizerKind::HWAddress)
        SanOpts.set(SanitizerKind::KernelHWAddress, false);
      if (mask & SanitizerKind::KernelHWAddress)
        SanOpts.set(SanitizerKind::HWAddress, false);
    }
  }

  // Apply sanitizer attributes to the function.
  if (SanOpts.hasOneOf(SanitizerKind::Address | SanitizerKind::KernelAddress))
    Fn->addFnAttr(llvm::Attribute::SanitizeAddress);
  if (SanOpts.hasOneOf(SanitizerKind::HWAddress | SanitizerKind::KernelHWAddress))
    Fn->addFnAttr(llvm::Attribute::SanitizeHWAddress);
  if (SanOpts.has(SanitizerKind::Thread))
    Fn->addFnAttr(llvm::Attribute::SanitizeThread);
  if (SanOpts.hasOneOf(SanitizerKind::Memory | SanitizerKind::KernelMemory))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemory);
  if (SanOpts.has(SanitizerKind::SafeStack))
    Fn->addFnAttr(llvm::Attribute::SafeStack);
  if (SanOpts.has(SanitizerKind::ShadowCallStack))
    Fn->addFnAttr(llvm::Attribute::ShadowCallStack);

  // Apply fuzzing attribute to the function.
  if (SanOpts.hasOneOf(SanitizerKind::Fuzzer | SanitizerKind::FuzzerNoLink))
    Fn->addFnAttr(llvm::Attribute::OptForFuzzing);

  // Ignore TSan memory acesses from within ObjC/ObjC++ dealloc, initialize,
  // .cxx_destruct, __destroy_helper_block_ and all of their calees at run time.
  if (SanOpts.has(SanitizerKind::Thread)) {
    if (const auto *OMD = dyn_cast_or_null<ObjCMethodDecl>(D)) {
      IdentifierInfo *II = OMD->getSelector().getIdentifierInfoForSlot(0);
      if (OMD->getMethodFamily() == OMF_dealloc ||
          OMD->getMethodFamily() == OMF_initialize ||
          (OMD->getSelector().isUnarySelector() && II->isStr(".cxx_destruct"))) {
        markAsIgnoreThreadCheckingAtRuntime(Fn);
      }
    }
  }

  // Ignore unrelated casts in STL allocate() since the allocator must cast
  // from void* to T* before object initialization completes. Don't match on the
  // namespace because not all allocators are in std::
  if (D && SanOpts.has(SanitizerKind::CFIUnrelatedCast)) {
    if (matchesStlAllocatorFn(D, getContext()))
      SanOpts.Mask &= ~SanitizerKind::CFIUnrelatedCast;
  }

  // Apply xray attributes to the function (as a string, for now)
  if (D) {
    if (const auto *XRayAttr = D->getAttr<XRayInstrumentAttr>()) {
      if (CGM.getCodeGenOpts().XRayInstrumentationBundle.has(
              XRayInstrKind::Function)) {
        if (XRayAttr->alwaysXRayInstrument() && ShouldXRayInstrumentFunction())
          Fn->addFnAttr("function-instrument", "xray-always");
        if (XRayAttr->neverXRayInstrument())
          Fn->addFnAttr("function-instrument", "xray-never");
        if (const auto *LogArgs = D->getAttr<XRayLogArgsAttr>())
          if (ShouldXRayInstrumentFunction())
            Fn->addFnAttr("xray-log-args",
                          llvm::utostr(LogArgs->getArgumentCount()));
      }
    } else {
      if (ShouldXRayInstrumentFunction() && !CGM.imbueXRayAttrs(Fn, Loc))
        Fn->addFnAttr(
            "xray-instruction-threshold",
            llvm::itostr(CGM.getCodeGenOpts().XRayInstructionThreshold));
    }
  }

  // Add no-jump-tables value.
  Fn->addFnAttr("no-jump-tables",
                llvm::toStringRef(CGM.getCodeGenOpts().NoUseJumpTables));

  // Add profile-sample-accurate value.
  if (CGM.getCodeGenOpts().ProfileSampleAccurate)
    Fn->addFnAttr("profile-sample-accurate");

  if (getLangOpts().OpenCL) {
    // Add metadata for a kernel function.
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
      EmitOpenCLKernelMetadata(FD, Fn);
  }

  // If we are checking function types, emit a function type signature as
  // prologue data.
  if (getLangOpts().CPlusPlus && SanOpts.has(SanitizerKind::Function)) {
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (llvm::Constant *PrologueSig = getPrologueSignature(CGM, FD)) {
        // Remove any (C++17) exception specifications, to allow calling e.g. a
        // noexcept function through a non-noexcept pointer.
        auto ProtoTy =
          getContext().getFunctionTypeWithExceptionSpec(FD->getType(),
                                                        EST_None);
        llvm::Constant *FTRTTIConst =
            CGM.GetAddrOfRTTIDescriptor(ProtoTy, /*ForEH=*/true);
        llvm::Constant *FTRTTIConstEncoded =
            EncodeAddrForUseInPrologue(Fn, FTRTTIConst);
        llvm::Constant *PrologueStructElems[] = {PrologueSig,
                                                 FTRTTIConstEncoded};
        llvm::Constant *PrologueStructConst =
            llvm::ConstantStruct::getAnon(PrologueStructElems, /*Packed=*/true);
        Fn->setPrologueData(PrologueStructConst);
      }
    }
  }

  // If we're checking nullability, we need to know whether we can check the
  // return value. Initialize the flag to 'true' and refine it in EmitParmDecl.
  if (SanOpts.has(SanitizerKind::NullabilityReturn)) {
    auto Nullability = FnRetTy->getNullability(getContext());
    if (Nullability && *Nullability == NullabilityKind::NonNull) {
      if (!(SanOpts.has(SanitizerKind::ReturnsNonnullAttribute) &&
            CurCodeDecl && CurCodeDecl->getAttr<ReturnsNonNullAttr>()))
        RetValNullabilityPrecondition =
            llvm::ConstantInt::getTrue(getLLVMContext());
    }
  }

  // If we're in C++ mode and the function name is "main", it is guaranteed
  // to be norecurse by the standard (3.6.1.3 "The function main shall not be
  // used within a program").
  if (getLangOpts().CPlusPlus)
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
      if (FD->isMain())
        Fn->addFnAttr(llvm::Attribute::NoRecurse);

  // If a custom alignment is used, force realigning to this alignment on
  // any main function which certainly will need it.
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
    if ((FD->isMain() || FD->isMSVCRTEntryPoint()) &&
        CGM.getCodeGenOpts().StackAlignment)
      Fn->addFnAttr("stackrealign");

  llvm::BasicBlock *EntryBB = createBasicBlock("entry", CurFn);

  // Create a marker to make it easy to insert allocas into the entryblock
  // later.  Don't create this with the builder, because we don't want it
  // folded.
  llvm::Value *Undef = llvm::UndefValue::get(Int32Ty);
  AllocaInsertPt = new llvm::BitCastInst(Undef, Int32Ty, "allocapt", EntryBB);

  ReturnBlock = getJumpDestInCurrentScope("return");

  Builder.SetInsertPoint(EntryBB);

  // If we're checking the return value, allocate space for a pointer to a
  // precise source location of the checked return statement.
  if (requiresReturnValueCheck()) {
    ReturnLocation = CreateDefaultAlignTempAlloca(Int8PtrTy, "return.sloc.ptr");
    InitTempAlloca(ReturnLocation, llvm::ConstantPointerNull::get(Int8PtrTy));
  }

  // Emit subprogram debug descriptor.
  if (CGDebugInfo *DI = getDebugInfo()) {
    // Reconstruct the type from the argument list so that implicit parameters,
    // such as 'this' and 'vtt', show up in the debug info. Preserve the calling
    // convention.
    CallingConv CC = CallingConv::CC_C;
    if (auto *FD = dyn_cast_or_null<FunctionDecl>(D))
      if (const auto *SrcFnTy = FD->getType()->getAs<FunctionType>())
        CC = SrcFnTy->getCallConv();
    SmallVector<QualType, 16> ArgTypes;
    for (const VarDecl *VD : Args)
      ArgTypes.push_back(VD->getType());
    QualType FnType = getContext().getFunctionType(
        RetTy, ArgTypes, FunctionProtoType::ExtProtoInfo(CC));
    DI->EmitFunctionStart(GD, Loc, StartLoc, FnType, CurFn, CurFuncIsThunk,
                          Builder);
  }

  if (ShouldInstrumentFunction()) {
    if (CGM.getCodeGenOpts().InstrumentFunctions)
      CurFn->addFnAttr("instrument-function-entry", "__cyg_profile_func_enter");
    if (CGM.getCodeGenOpts().InstrumentFunctionsAfterInlining)
      CurFn->addFnAttr("instrument-function-entry-inlined",
                       "__cyg_profile_func_enter");
    if (CGM.getCodeGenOpts().InstrumentFunctionEntryBare)
      CurFn->addFnAttr("instrument-function-entry-inlined",
                       "__cyg_profile_func_enter_bare");
  }

  // Since emitting the mcount call here impacts optimizations such as function
  // inlining, we just add an attribute to insert a mcount call in backend.
  // The attribute "counting-function" is set to mcount function name which is
  // architecture dependent.
  if (CGM.getCodeGenOpts().InstrumentForProfiling) {
    // Calls to fentry/mcount should not be generated if function has
    // the no_instrument_function attribute.
    if (!CurFuncDecl || !CurFuncDecl->hasAttr<NoInstrumentFunctionAttr>()) {
      if (CGM.getCodeGenOpts().CallFEntry)
        Fn->addFnAttr("fentry-call", "true");
      else {
        Fn->addFnAttr("instrument-function-entry-inlined",
                      getTarget().getMCountName());
      }
    }
  }

  if (RetTy->isVoidType()) {
    // Void type; nothing to return.
    ReturnValue = Address::invalid();

    // Count the implicit return.
    if (!endsWithReturn(D))
      ++NumReturnExprs;
  } else if (CurFnInfo->getReturnInfo().getKind() == ABIArgInfo::Indirect) {
    // Indirect return; emit returned value directly into sret slot.
    // This reduces code size, and affects correctness in C++.
    auto AI = CurFn->arg_begin();
    if (CurFnInfo->getReturnInfo().isSRetAfterThis())
      ++AI;
    ReturnValue = Address(&*AI, CurFnInfo->getReturnInfo().getIndirectAlign());
  } else if (CurFnInfo->getReturnInfo().getKind() == ABIArgInfo::InAlloca &&
             !hasScalarEvaluationKind(CurFnInfo->getReturnType())) {
    // Load the sret pointer from the argument struct and return into that.
    unsigned Idx = CurFnInfo->getReturnInfo().getInAllocaFieldIndex();
    llvm::Function::arg_iterator EI = CurFn->arg_end();
    --EI;
    llvm::Value *Addr = Builder.CreateStructGEP(nullptr, &*EI, Idx);
    Addr = Builder.CreateAlignedLoad(Addr, getPointerAlign(), "agg.result");
    ReturnValue = Address(Addr, getNaturalTypeAlignment(RetTy));
  } else {
    ReturnValue = CreateIRTemp(RetTy, "retval");

    // Tell the epilog emitter to autorelease the result.  We do this
    // now so that various specialized functions can suppress it
    // during their IR-generation.
    if (getLangOpts().ObjCAutoRefCount &&
        !CurFnInfo->isReturnsRetained() &&
        RetTy->isObjCRetainableType())
      AutoreleaseResult = true;
  }

  EmitStartEHSpec(CurCodeDecl);

  PrologueCleanupDepth = EHStack.stable_begin();

  // Emit OpenMP specific initialization of the device functions.
  if (getLangOpts().OpenMP && CurCodeDecl)
    CGM.getOpenMPRuntime().emitFunctionProlog(*this, CurCodeDecl);

  EmitFunctionProlog(*CurFnInfo, CurFn, Args);

  if (D && isa<CXXMethodDecl>(D) && cast<CXXMethodDecl>(D)->isInstance()) {
    CGM.getCXXABI().EmitInstanceFunctionProlog(*this);
    const CXXMethodDecl *MD = cast<CXXMethodDecl>(D);
    if (MD->getParent()->isLambda() &&
        MD->getOverloadedOperator() == OO_Call) {
      // We're in a lambda; figure out the captures.
      MD->getParent()->getCaptureFields(LambdaCaptureFields,
                                        LambdaThisCaptureField);
      if (LambdaThisCaptureField) {
        // If the lambda captures the object referred to by '*this' - either by
        // value or by reference, make sure CXXThisValue points to the correct
        // object.

        // Get the lvalue for the field (which is a copy of the enclosing object
        // or contains the address of the enclosing object).
        LValue ThisFieldLValue = EmitLValueForLambdaField(LambdaThisCaptureField);
        if (!LambdaThisCaptureField->getType()->isPointerType()) {
          // If the enclosing object was captured by value, just use its address.
          CXXThisValue = ThisFieldLValue.getAddress().getPointer();
        } else {
          // Load the lvalue pointed to by the field, since '*this' was captured
          // by reference.
          CXXThisValue =
              EmitLoadOfLValue(ThisFieldLValue, SourceLocation()).getScalarVal();
        }
      }
      for (auto *FD : MD->getParent()->fields()) {
        if (FD->hasCapturedVLAType()) {
          auto *ExprArg = EmitLoadOfLValue(EmitLValueForLambdaField(FD),
                                           SourceLocation()).getScalarVal();
          auto VAT = FD->getCapturedVLAType();
          VLASizeMap[VAT->getSizeExpr()] = ExprArg;
        }
      }
    } else {
      // Not in a lambda; just use 'this' from the method.
      // FIXME: Should we generate a new load for each use of 'this'?  The
      // fast register allocator would be happier...
      CXXThisValue = CXXABIThisValue;
    }

    // Check the 'this' pointer once per function, if it's available.
    if (CXXABIThisValue) {
      SanitizerSet SkippedChecks;
      SkippedChecks.set(SanitizerKind::ObjectSize, true);
      QualType ThisTy = MD->getThisType();

      // If this is the call operator of a lambda with no capture-default, it
      // may have a static invoker function, which may call this operator with
      // a null 'this' pointer.
      if (isLambdaCallOperator(MD) &&
          MD->getParent()->getLambdaCaptureDefault() == LCD_None)
        SkippedChecks.set(SanitizerKind::Null, true);

      EmitTypeCheck(isa<CXXConstructorDecl>(MD) ? TCK_ConstructorCall
                                                : TCK_MemberCall,
                    Loc, CXXABIThisValue, ThisTy,
                    getContext().getTypeAlignInChars(ThisTy->getPointeeType()),
                    SkippedChecks);
    }
  }

  // If any of the arguments have a variably modified type, make sure to
  // emit the type size.
  for (FunctionArgList::const_iterator i = Args.begin(), e = Args.end();
       i != e; ++i) {
    const VarDecl *VD = *i;

    // Dig out the type as written from ParmVarDecls; it's unclear whether
    // the standard (C99 6.9.1p10) requires this, but we're following the
    // precedent set by gcc.
    QualType Ty;
    if (const ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(VD))
      Ty = PVD->getOriginalType();
    else
      Ty = VD->getType();

    if (Ty->isVariablyModifiedType())
      EmitVariablyModifiedType(Ty);
  }
  // Emit a location at the end of the prologue.
  if (CGDebugInfo *DI = getDebugInfo())
    DI->EmitLocation(Builder, StartLoc);

  // TODO: Do we need to handle this in two places like we do with
  // target-features/target-cpu?
  if (CurFuncDecl)
    if (const auto *VecWidth = CurFuncDecl->getAttr<MinVectorWidthAttr>())
      LargestVectorWidth = VecWidth->getVectorWidth();
}

void CodeGenFunction::EmitFunctionBody(const Stmt *Body) {
  incrementProfileCounter(Body);
  if (const CompoundStmt *S = dyn_cast<CompoundStmt>(Body))
    EmitCompoundStmtWithoutScope(*S);
  else
    EmitStmt(Body);
}

/// When instrumenting to collect profile data, the counts for some blocks
/// such as switch cases need to not include the fall-through counts, so
/// emit a branch around the instrumentation code. When not instrumenting,
/// this just calls EmitBlock().
void CodeGenFunction::EmitBlockWithFallThrough(llvm::BasicBlock *BB,
                                               const Stmt *S) {
  llvm::BasicBlock *SkipCountBB = nullptr;
  if (HaveInsertPoint() && CGM.getCodeGenOpts().hasProfileClangInstr()) {
    // When instrumenting for profiling, the fallthrough to certain
    // statements needs to skip over the instrumentation code so that we
    // get an accurate count.
    SkipCountBB = createBasicBlock("skipcount");
    EmitBranch(SkipCountBB);
  }
  EmitBlock(BB);
  uint64_t CurrentCount = getCurrentProfileCount();
  incrementProfileCounter(S);
  setCurrentProfileCount(getCurrentProfileCount() + CurrentCount);
  if (SkipCountBB)
    EmitBlock(SkipCountBB);
}

/// Tries to mark the given function nounwind based on the
/// non-existence of any throwing calls within it.  We believe this is
/// lightweight enough to do at -O0.
static void TryMarkNoThrow(llvm::Function *F) {
  // LLVM treats 'nounwind' on a function as part of the type, so we
  // can't do this on functions that can be overwritten.
  if (F->isInterposable()) return;

  for (llvm::BasicBlock &BB : *F)
    for (llvm::Instruction &I : BB)
      if (I.mayThrow())
        return;

  F->setDoesNotThrow();
}

QualType CodeGenFunction::BuildFunctionArgList(GlobalDecl GD,
                                               FunctionArgList &Args) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  QualType ResTy = FD->getReturnType();

  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD);
  if (MD && MD->isInstance()) {
    if (CGM.getCXXABI().HasThisReturn(GD))
      ResTy = MD->getThisType();
    else if (CGM.getCXXABI().hasMostDerivedReturn(GD))
      ResTy = CGM.getContext().VoidPtrTy;
    CGM.getCXXABI().buildThisParam(*this, Args);
  }

  // The base version of an inheriting constructor whose constructed base is a
  // virtual base is not passed any arguments (because it doesn't actually call
  // the inherited constructor).
  bool PassedParams = true;
  if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(FD))
    if (auto Inherited = CD->getInheritedConstructor())
      PassedParams =
          getTypes().inheritingCtorHasParams(Inherited, GD.getCtorType());

  if (PassedParams) {
    for (auto *Param : FD->parameters()) {
      Args.push_back(Param);
      if (!Param->hasAttr<PassObjectSizeAttr>())
        continue;

      auto *Implicit = ImplicitParamDecl::Create(
          getContext(), Param->getDeclContext(), Param->getLocation(),
          /*Id=*/nullptr, getContext().getSizeType(), ImplicitParamDecl::Other);
      SizeArguments[Param] = Implicit;
      Args.push_back(Implicit);
    }
  }

  if (MD && (isa<CXXConstructorDecl>(MD) || isa<CXXDestructorDecl>(MD)))
    CGM.getCXXABI().addImplicitStructorParams(*this, ResTy, Args);

  return ResTy;
}

static bool
shouldUseUndefinedBehaviorReturnOptimization(const FunctionDecl *FD,
                                             const ASTContext &Context) {
  QualType T = FD->getReturnType();
  // Avoid the optimization for functions that return a record type with a
  // trivial destructor or another trivially copyable type.
  if (const RecordType *RT = T.getCanonicalType()->getAs<RecordType>()) {
    if (const auto *ClassDecl = dyn_cast<CXXRecordDecl>(RT->getDecl()))
      return !ClassDecl->hasTrivialDestructor();
  }
  return !T.isTriviallyCopyableType(Context);
}

void CodeGenFunction::GenerateCode(GlobalDecl GD, llvm::Function *Fn,
                                   const CGFunctionInfo &FnInfo) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  CurGD = GD;

  FunctionArgList Args;
  QualType ResTy = BuildFunctionArgList(GD, Args);

  // Check if we should generate debug info for this function.
  if (FD->hasAttr<NoDebugAttr>())
    DebugInfo = nullptr; // disable debug info indefinitely for this function

  // The function might not have a body if we're generating thunks for a
  // function declaration.
  SourceRange BodyRange;
  if (Stmt *Body = FD->getBody())
    BodyRange = Body->getSourceRange();
  else
    BodyRange = FD->getLocation();
  CurEHLocation = BodyRange.getEnd();

  // Use the location of the start of the function to determine where
  // the function definition is located. By default use the location
  // of the declaration as the location for the subprogram. A function
  // may lack a declaration in the source code if it is created by code
  // gen. (examples: _GLOBAL__I_a, __cxx_global_array_dtor, thunk).
  SourceLocation Loc = FD->getLocation();

  // If this is a function specialization then use the pattern body
  // as the location for the function.
  if (const FunctionDecl *SpecDecl = FD->getTemplateInstantiationPattern())
    if (SpecDecl->hasBody(SpecDecl))
      Loc = SpecDecl->getLocation();

  Stmt *Body = FD->getBody();

  // Initialize helper which will detect jumps which can cause invalid lifetime
  // markers.
  if (Body && ShouldEmitLifetimeMarkers)
    Bypasses.Init(Body);

  // Emit the standard function prologue.
  StartFunction(GD, ResTy, Fn, FnInfo, Args, Loc, BodyRange.getBegin());

  // Generate the body of the function.
  PGO.assignRegionCounters(GD, CurFn);
  if (isa<CXXDestructorDecl>(FD))
    EmitDestructorBody(Args);
  else if (isa<CXXConstructorDecl>(FD))
    EmitConstructorBody(Args);
  else if (getLangOpts().CUDA &&
           !getLangOpts().CUDAIsDevice &&
           FD->hasAttr<CUDAGlobalAttr>())
    CGM.getCUDARuntime().emitDeviceStub(*this, Args);
  else if (isa<CXXMethodDecl>(FD) &&
           cast<CXXMethodDecl>(FD)->isLambdaStaticInvoker()) {
    // The lambda static invoker function is special, because it forwards or
    // clones the body of the function call operator (but is actually static).
    EmitLambdaStaticInvokeBody(cast<CXXMethodDecl>(FD));
  } else if (FD->isDefaulted() && isa<CXXMethodDecl>(FD) &&
             (cast<CXXMethodDecl>(FD)->isCopyAssignmentOperator() ||
              cast<CXXMethodDecl>(FD)->isMoveAssignmentOperator())) {
    // Implicit copy-assignment gets the same special treatment as implicit
    // copy-constructors.
    emitImplicitAssignmentOperatorBody(Args);
  } else if (Body) {
    EmitFunctionBody(Body);
  } else
    llvm_unreachable("no definition for emitted function");

  // C++11 [stmt.return]p2:
  //   Flowing off the end of a function [...] results in undefined behavior in
  //   a value-returning function.
  // C11 6.9.1p12:
  //   If the '}' that terminates a function is reached, and the value of the
  //   function call is used by the caller, the behavior is undefined.
  if (getLangOpts().CPlusPlus && !FD->hasImplicitReturnZero() && !SawAsmBlock &&
      !FD->getReturnType()->isVoidType() && Builder.GetInsertBlock()) {
    bool ShouldEmitUnreachable =
        CGM.getCodeGenOpts().StrictReturn ||
        shouldUseUndefinedBehaviorReturnOptimization(FD, getContext());
    if (SanOpts.has(SanitizerKind::Return)) {
      SanitizerScope SanScope(this);
      llvm::Value *IsFalse = Builder.getFalse();
      EmitCheck(std::make_pair(IsFalse, SanitizerKind::Return),
                SanitizerHandler::MissingReturn,
                EmitCheckSourceLocation(FD->getLocation()), None);
    } else if (ShouldEmitUnreachable) {
      if (CGM.getCodeGenOpts().OptimizationLevel == 0)
        EmitTrapCall(llvm::Intrinsic::trap);
    }
    if (SanOpts.has(SanitizerKind::Return) || ShouldEmitUnreachable) {
      Builder.CreateUnreachable();
      Builder.ClearInsertionPoint();
    }
  }

  // Emit the standard function epilogue.
  FinishFunction(BodyRange.getEnd());

  // If we haven't marked the function nothrow through other means, do
  // a quick pass now to see if we can.
  if (!CurFn->doesNotThrow())
    TryMarkNoThrow(CurFn);
}

/// ContainsLabel - Return true if the statement contains a label in it.  If
/// this statement is not executed normally, it not containing a label means
/// that we can just remove the code.
bool CodeGenFunction::ContainsLabel(const Stmt *S, bool IgnoreCaseStmts) {
  // Null statement, not a label!
  if (!S) return false;

  // If this is a label, we have to emit the code, consider something like:
  // if (0) {  ...  foo:  bar(); }  goto foo;
  //
  // TODO: If anyone cared, we could track __label__'s, since we know that you
  // can't jump to one from outside their declared region.
  if (isa<LabelStmt>(S))
    return true;

  // If this is a case/default statement, and we haven't seen a switch, we have
  // to emit the code.
  if (isa<SwitchCase>(S) && !IgnoreCaseStmts)
    return true;

  // If this is a switch statement, we want to ignore cases below it.
  if (isa<SwitchStmt>(S))
    IgnoreCaseStmts = true;

  // Scan subexpressions for verboten labels.
  for (const Stmt *SubStmt : S->children())
    if (ContainsLabel(SubStmt, IgnoreCaseStmts))
      return true;

  return false;
}

/// containsBreak - Return true if the statement contains a break out of it.
/// If the statement (recursively) contains a switch or loop with a break
/// inside of it, this is fine.
bool CodeGenFunction::containsBreak(const Stmt *S) {
  // Null statement, not a label!
  if (!S) return false;

  // If this is a switch or loop that defines its own break scope, then we can
  // include it and anything inside of it.
  if (isa<SwitchStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S) ||
      isa<ForStmt>(S))
    return false;

  if (isa<BreakStmt>(S))
    return true;

  // Scan subexpressions for verboten breaks.
  for (const Stmt *SubStmt : S->children())
    if (containsBreak(SubStmt))
      return true;

  return false;
}

bool CodeGenFunction::mightAddDeclToScope(const Stmt *S) {
  if (!S) return false;

  // Some statement kinds add a scope and thus never add a decl to the current
  // scope. Note, this list is longer than the list of statements that might
  // have an unscoped decl nested within them, but this way is conservatively
  // correct even if more statement kinds are added.
  if (isa<IfStmt>(S) || isa<SwitchStmt>(S) || isa<WhileStmt>(S) ||
      isa<DoStmt>(S) || isa<ForStmt>(S) || isa<CompoundStmt>(S) ||
      isa<CXXForRangeStmt>(S) || isa<CXXTryStmt>(S) ||
      isa<ObjCForCollectionStmt>(S) || isa<ObjCAtTryStmt>(S))
    return false;

  if (isa<DeclStmt>(S))
    return true;

  for (const Stmt *SubStmt : S->children())
    if (mightAddDeclToScope(SubStmt))
      return true;

  return false;
}

/// ConstantFoldsToSimpleInteger - If the specified expression does not fold
/// to a constant, or if it does but contains a label, return false.  If it
/// constant folds return true and set the boolean result in Result.
bool CodeGenFunction::ConstantFoldsToSimpleInteger(const Expr *Cond,
                                                   bool &ResultBool,
                                                   bool AllowLabels) {
  llvm::APSInt ResultInt;
  if (!ConstantFoldsToSimpleInteger(Cond, ResultInt, AllowLabels))
    return false;

  ResultBool = ResultInt.getBoolValue();
  return true;
}

/// ConstantFoldsToSimpleInteger - If the specified expression does not fold
/// to a constant, or if it does but contains a label, return false.  If it
/// constant folds return true and set the folded value.
bool CodeGenFunction::ConstantFoldsToSimpleInteger(const Expr *Cond,
                                                   llvm::APSInt &ResultInt,
                                                   bool AllowLabels) {
  // FIXME: Rename and handle conversion of other evaluatable things
  // to bool.
  Expr::EvalResult Result;
  if (!Cond->EvaluateAsInt(Result, getContext()))
    return false;  // Not foldable, not integer or not fully evaluatable.

  llvm::APSInt Int = Result.Val.getInt();
  if (!AllowLabels && CodeGenFunction::ContainsLabel(Cond))
    return false;  // Contains a label.

  ResultInt = Int;
  return true;
}



/// EmitBranchOnBoolExpr - Emit a branch on a boolean condition (e.g. for an if
/// statement) to the specified blocks.  Based on the condition, this might try
/// to simplify the codegen of the conditional based on the branch.
///
void CodeGenFunction::EmitBranchOnBoolExpr(const Expr *Cond,
                                           llvm::BasicBlock *TrueBlock,
                                           llvm::BasicBlock *FalseBlock,
                                           uint64_t TrueCount) {
  Cond = Cond->IgnoreParens();

  if (const BinaryOperator *CondBOp = dyn_cast<BinaryOperator>(Cond)) {

    // Handle X && Y in a condition.
    if (CondBOp->getOpcode() == BO_LAnd) {
      // If we have "1 && X", simplify the code.  "0 && X" would have constant
      // folded if the case was simple enough.
      bool ConstantBool = false;
      if (ConstantFoldsToSimpleInteger(CondBOp->getLHS(), ConstantBool) &&
          ConstantBool) {
        // br(1 && X) -> br(X).
        incrementProfileCounter(CondBOp);
        return EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock,
                                    TrueCount);
      }

      // If we have "X && 1", simplify the code to use an uncond branch.
      // "X && 0" would have been constant folded to 0.
      if (ConstantFoldsToSimpleInteger(CondBOp->getRHS(), ConstantBool) &&
          ConstantBool) {
        // br(X && 1) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock,
                                    TrueCount);
      }

      // Emit the LHS as a conditional.  If the LHS conditional is false, we
      // want to jump to the FalseBlock.
      llvm::BasicBlock *LHSTrue = createBasicBlock("land.lhs.true");
      // The counter tells us how often we evaluate RHS, and all of TrueCount
      // can be propagated to that branch.
      uint64_t RHSCount = getProfileCount(CondBOp->getRHS());

      ConditionalEvaluation eval(*this);
      {
        ApplyDebugLocation DL(*this, Cond);
        EmitBranchOnBoolExpr(CondBOp->getLHS(), LHSTrue, FalseBlock, RHSCount);
        EmitBlock(LHSTrue);
      }

      incrementProfileCounter(CondBOp);
      setCurrentProfileCount(getProfileCount(CondBOp->getRHS()));

      // Any temporaries created here are conditional.
      eval.begin(*this);
      EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock, TrueCount);
      eval.end(*this);

      return;
    }

    if (CondBOp->getOpcode() == BO_LOr) {
      // If we have "0 || X", simplify the code.  "1 || X" would have constant
      // folded if the case was simple enough.
      bool ConstantBool = false;
      if (ConstantFoldsToSimpleInteger(CondBOp->getLHS(), ConstantBool) &&
          !ConstantBool) {
        // br(0 || X) -> br(X).
        incrementProfileCounter(CondBOp);
        return EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock,
                                    TrueCount);
      }

      // If we have "X || 0", simplify the code to use an uncond branch.
      // "X || 1" would have been constant folded to 1.
      if (ConstantFoldsToSimpleInteger(CondBOp->getRHS(), ConstantBool) &&
          !ConstantBool) {
        // br(X || 0) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock,
                                    TrueCount);
      }

      // Emit the LHS as a conditional.  If the LHS conditional is true, we
      // want to jump to the TrueBlock.
      llvm::BasicBlock *LHSFalse = createBasicBlock("lor.lhs.false");
      // We have the count for entry to the RHS and for the whole expression
      // being true, so we can divy up True count between the short circuit and
      // the RHS.
      uint64_t LHSCount =
          getCurrentProfileCount() - getProfileCount(CondBOp->getRHS());
      uint64_t RHSCount = TrueCount - LHSCount;

      ConditionalEvaluation eval(*this);
      {
        ApplyDebugLocation DL(*this, Cond);
        EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, LHSFalse, LHSCount);
        EmitBlock(LHSFalse);
      }

      incrementProfileCounter(CondBOp);
      setCurrentProfileCount(getProfileCount(CondBOp->getRHS()));

      // Any temporaries created here are conditional.
      eval.begin(*this);
      EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock, RHSCount);

      eval.end(*this);

      return;
    }
  }

  if (const UnaryOperator *CondUOp = dyn_cast<UnaryOperator>(Cond)) {
    // br(!x, t, f) -> br(x, f, t)
    if (CondUOp->getOpcode() == UO_LNot) {
      // Negate the count.
      uint64_t FalseCount = getCurrentProfileCount() - TrueCount;
      // Negate the condition and swap the destination blocks.
      return EmitBranchOnBoolExpr(CondUOp->getSubExpr(), FalseBlock, TrueBlock,
                                  FalseCount);
    }
  }

  if (const ConditionalOperator *CondOp = dyn_cast<ConditionalOperator>(Cond)) {
    // br(c ? x : y, t, f) -> br(c, br(x, t, f), br(y, t, f))
    llvm::BasicBlock *LHSBlock = createBasicBlock("cond.true");
    llvm::BasicBlock *RHSBlock = createBasicBlock("cond.false");

    ConditionalEvaluation cond(*this);
    EmitBranchOnBoolExpr(CondOp->getCond(), LHSBlock, RHSBlock,
                         getProfileCount(CondOp));

    // When computing PGO branch weights, we only know the overall count for
    // the true block. This code is essentially doing tail duplication of the
    // naive code-gen, introducing new edges for which counts are not
    // available. Divide the counts proportionally between the LHS and RHS of
    // the conditional operator.
    uint64_t LHSScaledTrueCount = 0;
    if (TrueCount) {
      double LHSRatio =
          getProfileCount(CondOp) / (double)getCurrentProfileCount();
      LHSScaledTrueCount = TrueCount * LHSRatio;
    }

    cond.begin(*this);
    EmitBlock(LHSBlock);
    incrementProfileCounter(CondOp);
    {
      ApplyDebugLocation DL(*this, Cond);
      EmitBranchOnBoolExpr(CondOp->getLHS(), TrueBlock, FalseBlock,
                           LHSScaledTrueCount);
    }
    cond.end(*this);

    cond.begin(*this);
    EmitBlock(RHSBlock);
    EmitBranchOnBoolExpr(CondOp->getRHS(), TrueBlock, FalseBlock,
                         TrueCount - LHSScaledTrueCount);
    cond.end(*this);

    return;
  }

  if (const CXXThrowExpr *Throw = dyn_cast<CXXThrowExpr>(Cond)) {
    // Conditional operator handling can give us a throw expression as a
    // condition for a case like:
    //   br(c ? throw x : y, t, f) -> br(c, br(throw x, t, f), br(y, t, f)
    // Fold this to:
    //   br(c, throw x, br(y, t, f))
    EmitCXXThrowExpr(Throw, /*KeepInsertionPoint*/false);
    return;
  }

  // If the branch has a condition wrapped by __builtin_unpredictable,
  // create metadata that specifies that the branch is unpredictable.
  // Don't bother if not optimizing because that metadata would not be used.
  llvm::MDNode *Unpredictable = nullptr;
  auto *Call = dyn_cast<CallExpr>(Cond->IgnoreImpCasts());
  if (Call && CGM.getCodeGenOpts().OptimizationLevel != 0) {
    auto *FD = dyn_cast_or_null<FunctionDecl>(Call->getCalleeDecl());
    if (FD && FD->getBuiltinID() == Builtin::BI__builtin_unpredictable) {
      llvm::MDBuilder MDHelper(getLLVMContext());
      Unpredictable = MDHelper.createUnpredictable();
    }
  }

  // Create branch weights based on the number of times we get here and the
  // number of times the condition should be true.
  uint64_t CurrentCount = std::max(getCurrentProfileCount(), TrueCount);
  llvm::MDNode *Weights =
      createProfileWeights(TrueCount, CurrentCount - TrueCount);

  // Emit the code with the fully general case.
  llvm::Value *CondV;
  {
    ApplyDebugLocation DL(*this, Cond);
    CondV = EvaluateExprAsBool(Cond);
  }
  Builder.CreateCondBr(CondV, TrueBlock, FalseBlock, Weights, Unpredictable);
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified stmt yet.
void CodeGenFunction::ErrorUnsupported(const Stmt *S, const char *Type) {
  CGM.ErrorUnsupported(S, Type);
}

/// emitNonZeroVLAInit - Emit the "zero" initialization of a
/// variable-length array whose elements have a non-zero bit-pattern.
///
/// \param baseType the inner-most element type of the array
/// \param src - a char* pointing to the bit-pattern for a single
/// base element of the array
/// \param sizeInChars - the total size of the VLA, in chars
static void emitNonZeroVLAInit(CodeGenFunction &CGF, QualType baseType,
                               Address dest, Address src,
                               llvm::Value *sizeInChars) {
  CGBuilderTy &Builder = CGF.Builder;

  CharUnits baseSize = CGF.getContext().getTypeSizeInChars(baseType);
  llvm::Value *baseSizeInChars
    = llvm::ConstantInt::get(CGF.IntPtrTy, baseSize.getQuantity());

  Address begin =
    Builder.CreateElementBitCast(dest, CGF.Int8Ty, "vla.begin");
  llvm::Value *end =
    Builder.CreateInBoundsGEP(begin.getPointer(), sizeInChars, "vla.end");

  llvm::BasicBlock *originBB = CGF.Builder.GetInsertBlock();
  llvm::BasicBlock *loopBB = CGF.createBasicBlock("vla-init.loop");
  llvm::BasicBlock *contBB = CGF.createBasicBlock("vla-init.cont");

  // Make a loop over the VLA.  C99 guarantees that the VLA element
  // count must be nonzero.
  CGF.EmitBlock(loopBB);

  llvm::PHINode *cur = Builder.CreatePHI(begin.getType(), 2, "vla.cur");
  cur->addIncoming(begin.getPointer(), originBB);

  CharUnits curAlign =
    dest.getAlignment().alignmentOfArrayElement(baseSize);

  // memcpy the individual element bit-pattern.
  Builder.CreateMemCpy(Address(cur, curAlign), src, baseSizeInChars,
                       /*volatile*/ false);

  // Go to the next element.
  llvm::Value *next =
    Builder.CreateInBoundsGEP(CGF.Int8Ty, cur, baseSizeInChars, "vla.next");

  // Leave if that's the end of the VLA.
  llvm::Value *done = Builder.CreateICmpEQ(next, end, "vla-init.isdone");
  Builder.CreateCondBr(done, contBB, loopBB);
  cur->addIncoming(next, loopBB);

  CGF.EmitBlock(contBB);
}

void
CodeGenFunction::EmitNullInitialization(Address DestPtr, QualType Ty) {
  // Ignore empty classes in C++.
  if (getLangOpts().CPlusPlus) {
    if (const RecordType *RT = Ty->getAs<RecordType>()) {
      if (cast<CXXRecordDecl>(RT->getDecl())->isEmpty())
        return;
    }
  }

  // Cast the dest ptr to the appropriate i8 pointer type.
  if (DestPtr.getElementType() != Int8Ty)
    DestPtr = Builder.CreateElementBitCast(DestPtr, Int8Ty);

  // Get size and alignment info for this aggregate.
  CharUnits size = getContext().getTypeSizeInChars(Ty);

  llvm::Value *SizeVal;
  const VariableArrayType *vla;

  // Don't bother emitting a zero-byte memset.
  if (size.isZero()) {
    // But note that getTypeInfo returns 0 for a VLA.
    if (const VariableArrayType *vlaType =
          dyn_cast_or_null<VariableArrayType>(
                                          getContext().getAsArrayType(Ty))) {
      auto VlaSize = getVLASize(vlaType);
      SizeVal = VlaSize.NumElts;
      CharUnits eltSize = getContext().getTypeSizeInChars(VlaSize.Type);
      if (!eltSize.isOne())
        SizeVal = Builder.CreateNUWMul(SizeVal, CGM.getSize(eltSize));
      vla = vlaType;
    } else {
      return;
    }
  } else {
    SizeVal = CGM.getSize(size);
    vla = nullptr;
  }

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  // TODO: there are other patterns besides zero that we can usefully memset,
  // like -1, which happens to be the pattern used by member-pointers.
  if (!CGM.getTypes().isZeroInitializable(Ty)) {
    // For a VLA, emit a single element, then splat that over the VLA.
    if (vla) Ty = getContext().getBaseElementType(vla);

    llvm::Constant *NullConstant = CGM.EmitNullConstant(Ty);

    llvm::GlobalVariable *NullVariable =
      new llvm::GlobalVariable(CGM.getModule(), NullConstant->getType(),
                               /*isConstant=*/true,
                               llvm::GlobalVariable::PrivateLinkage,
                               NullConstant, Twine());
    CharUnits NullAlign = DestPtr.getAlignment();
    NullVariable->setAlignment(NullAlign.getQuantity());
    Address SrcPtr(Builder.CreateBitCast(NullVariable, Builder.getInt8PtrTy()),
                   NullAlign);

    if (vla) return emitNonZeroVLAInit(*this, Ty, DestPtr, SrcPtr, SizeVal);

    // Get and call the appropriate llvm.memcpy overload.
    Builder.CreateMemCpy(DestPtr, SrcPtr, SizeVal, false);
    return;
  }

  // Otherwise, just memset the whole thing to zero.  This is legal
  // because in LLVM, all default initializers (other than the ones we just
  // handled above) are guaranteed to have a bit pattern of all zeros.
  Builder.CreateMemSet(DestPtr, Builder.getInt8(0), SizeVal, false);
}

llvm::BlockAddress *CodeGenFunction::GetAddrOfLabel(const LabelDecl *L) {
  // Make sure that there is a block for the indirect goto.
  if (!IndirectBranch)
    GetIndirectGotoBlock();

  llvm::BasicBlock *BB = getJumpDestForLabel(L).getBlock();

  // Make sure the indirect branch includes all of the address-taken blocks.
  IndirectBranch->addDestination(BB);
  return llvm::BlockAddress::get(CurFn, BB);
}

llvm::BasicBlock *CodeGenFunction::GetIndirectGotoBlock() {
  // If we already made the indirect branch for indirect goto, return its block.
  if (IndirectBranch) return IndirectBranch->getParent();

  CGBuilderTy TmpBuilder(*this, createBasicBlock("indirectgoto"));

  // Create the PHI node that indirect gotos will add entries to.
  llvm::Value *DestVal = TmpBuilder.CreatePHI(Int8PtrTy, 0,
                                              "indirect.goto.dest");

  // Create the indirect branch instruction.
  IndirectBranch = TmpBuilder.CreateIndirectBr(DestVal);
  return IndirectBranch->getParent();
}

/// Computes the length of an array in elements, as well as the base
/// element type and a properly-typed first element pointer.
llvm::Value *CodeGenFunction::emitArrayLength(const ArrayType *origArrayType,
                                              QualType &baseType,
                                              Address &addr) {
  const ArrayType *arrayType = origArrayType;

  // If it's a VLA, we have to load the stored size.  Note that
  // this is the size of the VLA in bytes, not its size in elements.
  llvm::Value *numVLAElements = nullptr;
  if (isa<VariableArrayType>(arrayType)) {
    numVLAElements = getVLASize(cast<VariableArrayType>(arrayType)).NumElts;

    // Walk into all VLAs.  This doesn't require changes to addr,
    // which has type T* where T is the first non-VLA element type.
    do {
      QualType elementType = arrayType->getElementType();
      arrayType = getContext().getAsArrayType(elementType);

      // If we only have VLA components, 'addr' requires no adjustment.
      if (!arrayType) {
        baseType = elementType;
        return numVLAElements;
      }
    } while (isa<VariableArrayType>(arrayType));

    // We get out here only if we find a constant array type
    // inside the VLA.
  }

  // We have some number of constant-length arrays, so addr should
  // have LLVM type [M x [N x [...]]]*.  Build a GEP that walks
  // down to the first element of addr.
  SmallVector<llvm::Value*, 8> gepIndices;

  // GEP down to the array type.
  llvm::ConstantInt *zero = Builder.getInt32(0);
  gepIndices.push_back(zero);

  uint64_t countFromCLAs = 1;
  QualType eltType;

  llvm::ArrayType *llvmArrayType =
    dyn_cast<llvm::ArrayType>(addr.getElementType());
  while (llvmArrayType) {
    assert(isa<ConstantArrayType>(arrayType));
    assert(cast<ConstantArrayType>(arrayType)->getSize().getZExtValue()
             == llvmArrayType->getNumElements());

    gepIndices.push_back(zero);
    countFromCLAs *= llvmArrayType->getNumElements();
    eltType = arrayType->getElementType();

    llvmArrayType =
      dyn_cast<llvm::ArrayType>(llvmArrayType->getElementType());
    arrayType = getContext().getAsArrayType(arrayType->getElementType());
    assert((!llvmArrayType || arrayType) &&
           "LLVM and Clang types are out-of-synch");
  }

  if (arrayType) {
    // From this point onwards, the Clang array type has been emitted
    // as some other type (probably a packed struct). Compute the array
    // size, and just emit the 'begin' expression as a bitcast.
    while (arrayType) {
      countFromCLAs *=
          cast<ConstantArrayType>(arrayType)->getSize().getZExtValue();
      eltType = arrayType->getElementType();
      arrayType = getContext().getAsArrayType(eltType);
    }

    llvm::Type *baseType = ConvertType(eltType);
    addr = Builder.CreateElementBitCast(addr, baseType, "array.begin");
  } else {
    // Create the actual GEP.
    addr = Address(Builder.CreateInBoundsGEP(addr.getPointer(),
                                             gepIndices, "array.begin"),
                   addr.getAlignment());
  }

  baseType = eltType;

  llvm::Value *numElements
    = llvm::ConstantInt::get(SizeTy, countFromCLAs);

  // If we had any VLA dimensions, factor them in.
  if (numVLAElements)
    numElements = Builder.CreateNUWMul(numVLAElements, numElements);

  return numElements;
}

CodeGenFunction::VlaSizePair CodeGenFunction::getVLASize(QualType type) {
  const VariableArrayType *vla = getContext().getAsVariableArrayType(type);
  assert(vla && "type was not a variable array type!");
  return getVLASize(vla);
}

CodeGenFunction::VlaSizePair
CodeGenFunction::getVLASize(const VariableArrayType *type) {
  // The number of elements so far; always size_t.
  llvm::Value *numElements = nullptr;

  QualType elementType;
  do {
    elementType = type->getElementType();
    llvm::Value *vlaSize = VLASizeMap[type->getSizeExpr()];
    assert(vlaSize && "no size for VLA!");
    assert(vlaSize->getType() == SizeTy);

    if (!numElements) {
      numElements = vlaSize;
    } else {
      // It's undefined behavior if this wraps around, so mark it that way.
      // FIXME: Teach -fsanitize=undefined to trap this.
      numElements = Builder.CreateNUWMul(numElements, vlaSize);
    }
  } while ((type = getContext().getAsVariableArrayType(elementType)));

  return { numElements, elementType };
}

CodeGenFunction::VlaSizePair
CodeGenFunction::getVLAElements1D(QualType type) {
  const VariableArrayType *vla = getContext().getAsVariableArrayType(type);
  assert(vla && "type was not a variable array type!");
  return getVLAElements1D(vla);
}

CodeGenFunction::VlaSizePair
CodeGenFunction::getVLAElements1D(const VariableArrayType *Vla) {
  llvm::Value *VlaSize = VLASizeMap[Vla->getSizeExpr()];
  assert(VlaSize && "no size for VLA!");
  assert(VlaSize->getType() == SizeTy);
  return { VlaSize, Vla->getElementType() };
}

void CodeGenFunction::EmitVariablyModifiedType(QualType type) {
  assert(type->isVariablyModifiedType() &&
         "Must pass variably modified type to EmitVLASizes!");

  EnsureInsertPoint();

  // We're going to walk down into the type and look for VLA
  // expressions.
  do {
    assert(type->isVariablyModifiedType());

    const Type *ty = type.getTypePtr();
    switch (ty->getTypeClass()) {

#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
      llvm_unreachable("unexpected dependent type!");

    // These types are never variably-modified.
    case Type::Builtin:
    case Type::Complex:
    case Type::Vector:
    case Type::ExtVector:
    case Type::Record:
    case Type::Enum:
    case Type::Elaborated:
    case Type::TemplateSpecialization:
    case Type::ObjCTypeParam:
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
      llvm_unreachable("type class is never variably-modified!");

    case Type::Adjusted:
      type = cast<AdjustedType>(ty)->getAdjustedType();
      break;

    case Type::Decayed:
      type = cast<DecayedType>(ty)->getPointeeType();
      break;

    case Type::Pointer:
      type = cast<PointerType>(ty)->getPointeeType();
      break;

    case Type::BlockPointer:
      type = cast<BlockPointerType>(ty)->getPointeeType();
      break;

    case Type::LValueReference:
    case Type::RValueReference:
      type = cast<ReferenceType>(ty)->getPointeeType();
      break;

    case Type::MemberPointer:
      type = cast<MemberPointerType>(ty)->getPointeeType();
      break;

    case Type::ConstantArray:
    case Type::IncompleteArray:
      // Losing element qualification here is fine.
      type = cast<ArrayType>(ty)->getElementType();
      break;

    case Type::VariableArray: {
      // Losing element qualification here is fine.
      const VariableArrayType *vat = cast<VariableArrayType>(ty);

      // Unknown size indication requires no size computation.
      // Otherwise, evaluate and record it.
      if (const Expr *size = vat->getSizeExpr()) {
        // It's possible that we might have emitted this already,
        // e.g. with a typedef and a pointer to it.
        llvm::Value *&entry = VLASizeMap[size];
        if (!entry) {
          llvm::Value *Size = EmitScalarExpr(size);

          // C11 6.7.6.2p5:
          //   If the size is an expression that is not an integer constant
          //   expression [...] each time it is evaluated it shall have a value
          //   greater than zero.
          if (SanOpts.has(SanitizerKind::VLABound) &&
              size->getType()->isSignedIntegerType()) {
            SanitizerScope SanScope(this);
            llvm::Value *Zero = llvm::Constant::getNullValue(Size->getType());
            llvm::Constant *StaticArgs[] = {
                EmitCheckSourceLocation(size->getBeginLoc()),
                EmitCheckTypeDescriptor(size->getType())};
            EmitCheck(std::make_pair(Builder.CreateICmpSGT(Size, Zero),
                                     SanitizerKind::VLABound),
                      SanitizerHandler::VLABoundNotPositive, StaticArgs, Size);
          }

          // Always zexting here would be wrong if it weren't
          // undefined behavior to have a negative bound.
          entry = Builder.CreateIntCast(Size, SizeTy, /*signed*/ false);
        }
      }
      type = vat->getElementType();
      break;
    }

    case Type::FunctionProto:
    case Type::FunctionNoProto:
      type = cast<FunctionType>(ty)->getReturnType();
      break;

    case Type::Paren:
    case Type::TypeOf:
    case Type::UnaryTransform:
    case Type::Attributed:
    case Type::SubstTemplateTypeParm:
    case Type::PackExpansion:
      // Keep walking after single level desugaring.
      type = type.getSingleStepDesugaredType(getContext());
      break;

    case Type::Typedef:
    case Type::Decltype:
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      // Stop walking: nothing to do.
      return;

    case Type::TypeOfExpr:
      // Stop walking: emit typeof expression.
      EmitIgnoredExpr(cast<TypeOfExprType>(ty)->getUnderlyingExpr());
      return;

    case Type::Atomic:
      type = cast<AtomicType>(ty)->getValueType();
      break;

    case Type::Pipe:
      type = cast<PipeType>(ty)->getElementType();
      break;
    }
  } while (type->isVariablyModifiedType());
}

Address CodeGenFunction::EmitVAListRef(const Expr* E) {
  if (getContext().getBuiltinVaListType()->isArrayType())
    return EmitPointerWithAlignment(E);
  return EmitLValue(E).getAddress();
}

Address CodeGenFunction::EmitMSVAListRef(const Expr *E) {
  return EmitLValue(E).getAddress();
}

void CodeGenFunction::EmitDeclRefExprDbgValue(const DeclRefExpr *E,
                                              const APValue &Init) {
  assert(!Init.isUninit() && "Invalid DeclRefExpr initializer!");
  if (CGDebugInfo *Dbg = getDebugInfo())
    if (CGM.getCodeGenOpts().getDebugInfo() >= codegenoptions::LimitedDebugInfo)
      Dbg->EmitGlobalVariable(E->getDecl(), Init);
}

CodeGenFunction::PeepholeProtection
CodeGenFunction::protectFromPeepholes(RValue rvalue) {
  // At the moment, the only aggressive peephole we do in IR gen
  // is trunc(zext) folding, but if we add more, we can easily
  // extend this protection.

  if (!rvalue.isScalar()) return PeepholeProtection();
  llvm::Value *value = rvalue.getScalarVal();
  if (!isa<llvm::ZExtInst>(value)) return PeepholeProtection();

  // Just make an extra bitcast.
  assert(HaveInsertPoint());
  llvm::Instruction *inst = new llvm::BitCastInst(value, value->getType(), "",
                                                  Builder.GetInsertBlock());

  PeepholeProtection protection;
  protection.Inst = inst;
  return protection;
}

void CodeGenFunction::unprotectFromPeepholes(PeepholeProtection protection) {
  if (!protection.Inst) return;

  // In theory, we could try to duplicate the peepholes now, but whatever.
  protection.Inst->eraseFromParent();
}

void CodeGenFunction::EmitAlignmentAssumption(llvm::Value *PtrValue,
                                              QualType Ty, SourceLocation Loc,
                                              SourceLocation AssumptionLoc,
                                              llvm::Value *Alignment,
                                              llvm::Value *OffsetValue) {
  llvm::Value *TheCheck;
  llvm::Instruction *Assumption = Builder.CreateAlignmentAssumption(
      CGM.getDataLayout(), PtrValue, Alignment, OffsetValue, &TheCheck);
  if (SanOpts.has(SanitizerKind::Alignment)) {
    EmitAlignmentAssumptionCheck(PtrValue, Ty, Loc, AssumptionLoc, Alignment,
                                 OffsetValue, TheCheck, Assumption);
  }
}

void CodeGenFunction::EmitAlignmentAssumption(llvm::Value *PtrValue,
                                              QualType Ty, SourceLocation Loc,
                                              SourceLocation AssumptionLoc,
                                              unsigned Alignment,
                                              llvm::Value *OffsetValue) {
  llvm::Value *TheCheck;
  llvm::Instruction *Assumption = Builder.CreateAlignmentAssumption(
      CGM.getDataLayout(), PtrValue, Alignment, OffsetValue, &TheCheck);
  if (SanOpts.has(SanitizerKind::Alignment)) {
    llvm::Value *AlignmentVal = llvm::ConstantInt::get(IntPtrTy, Alignment);
    EmitAlignmentAssumptionCheck(PtrValue, Ty, Loc, AssumptionLoc, AlignmentVal,
                                 OffsetValue, TheCheck, Assumption);
  }
}

void CodeGenFunction::EmitAlignmentAssumption(llvm::Value *PtrValue,
                                              const Expr *E,
                                              SourceLocation AssumptionLoc,
                                              unsigned Alignment,
                                              llvm::Value *OffsetValue) {
  if (auto *CE = dyn_cast<CastExpr>(E))
    E = CE->getSubExprAsWritten();
  QualType Ty = E->getType();
  SourceLocation Loc = E->getExprLoc();

  EmitAlignmentAssumption(PtrValue, Ty, Loc, AssumptionLoc, Alignment,
                          OffsetValue);
}

llvm::Value *CodeGenFunction::EmitAnnotationCall(llvm::Value *AnnotationFn,
                                                 llvm::Value *AnnotatedVal,
                                                 StringRef AnnotationStr,
                                                 SourceLocation Location) {
  llvm::Value *Args[4] = {
    AnnotatedVal,
    Builder.CreateBitCast(CGM.EmitAnnotationString(AnnotationStr), Int8PtrTy),
    Builder.CreateBitCast(CGM.EmitAnnotationUnit(Location), Int8PtrTy),
    CGM.EmitAnnotationLineNo(Location)
  };
  return Builder.CreateCall(AnnotationFn, Args);
}

void CodeGenFunction::EmitVarAnnotations(const VarDecl *D, llvm::Value *V) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  // FIXME We create a new bitcast for every annotation because that's what
  // llvm-gcc was doing.
  for (const auto *I : D->specific_attrs<AnnotateAttr>())
    EmitAnnotationCall(CGM.getIntrinsic(llvm::Intrinsic::var_annotation),
                       Builder.CreateBitCast(V, CGM.Int8PtrTy, V->getName()),
                       I->getAnnotation(), D->getLocation());
}

Address CodeGenFunction::EmitFieldAnnotations(const FieldDecl *D,
                                              Address Addr) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  llvm::Value *V = Addr.getPointer();
  llvm::Type *VTy = V->getType();
  llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::ptr_annotation,
                                    CGM.Int8PtrTy);

  for (const auto *I : D->specific_attrs<AnnotateAttr>()) {
    // FIXME Always emit the cast inst so we can differentiate between
    // annotation on the first field of a struct and annotation on the struct
    // itself.
    if (VTy != CGM.Int8PtrTy)
      V = Builder.CreateBitCast(V, CGM.Int8PtrTy);
    V = EmitAnnotationCall(F, V, I->getAnnotation(), D->getLocation());
    V = Builder.CreateBitCast(V, VTy);
  }

  return Address(V, Addr.getAlignment());
}

CodeGenFunction::CGCapturedStmtInfo::~CGCapturedStmtInfo() { }

CodeGenFunction::SanitizerScope::SanitizerScope(CodeGenFunction *CGF)
    : CGF(CGF) {
  assert(!CGF->IsSanitizerScope);
  CGF->IsSanitizerScope = true;
}

CodeGenFunction::SanitizerScope::~SanitizerScope() {
  CGF->IsSanitizerScope = false;
}

void CodeGenFunction::InsertHelper(llvm::Instruction *I,
                                   const llvm::Twine &Name,
                                   llvm::BasicBlock *BB,
                                   llvm::BasicBlock::iterator InsertPt) const {
  LoopStack.InsertHelper(I);
  if (IsSanitizerScope)
    CGM.getSanitizerMetadata()->disableSanitizerForInstruction(I);
}

void CGBuilderInserter::InsertHelper(
    llvm::Instruction *I, const llvm::Twine &Name, llvm::BasicBlock *BB,
    llvm::BasicBlock::iterator InsertPt) const {
  llvm::IRBuilderDefaultInserter::InsertHelper(I, Name, BB, InsertPt);
  if (CGF)
    CGF->InsertHelper(I, Name, BB, InsertPt);
}

static bool hasRequiredFeatures(const SmallVectorImpl<StringRef> &ReqFeatures,
                                CodeGenModule &CGM, const FunctionDecl *FD,
                                std::string &FirstMissing) {
  // If there aren't any required features listed then go ahead and return.
  if (ReqFeatures.empty())
    return false;

  // Now build up the set of caller features and verify that all the required
  // features are there.
  llvm::StringMap<bool> CallerFeatureMap;
  CGM.getFunctionFeatureMap(CallerFeatureMap, GlobalDecl().getWithDecl(FD));

  // If we have at least one of the features in the feature list return
  // true, otherwise return false.
  return std::all_of(
      ReqFeatures.begin(), ReqFeatures.end(), [&](StringRef Feature) {
        SmallVector<StringRef, 1> OrFeatures;
        Feature.split(OrFeatures, '|');
        return llvm::any_of(OrFeatures, [&](StringRef Feature) {
          if (!CallerFeatureMap.lookup(Feature)) {
            FirstMissing = Feature.str();
            return false;
          }
          return true;
        });
      });
}

// Emits an error if we don't have a valid set of target features for the
// called function.
void CodeGenFunction::checkTargetFeatures(const CallExpr *E,
                                          const FunctionDecl *TargetDecl) {
  // Early exit if this is an indirect call.
  if (!TargetDecl)
    return;

  // Get the current enclosing function if it exists. If it doesn't
  // we can't check the target features anyhow.
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CurFuncDecl);
  if (!FD)
    return;

  // Grab the required features for the call. For a builtin this is listed in
  // the td file with the default cpu, for an always_inline function this is any
  // listed cpu and any listed features.
  unsigned BuiltinID = TargetDecl->getBuiltinID();
  std::string MissingFeature;
  if (BuiltinID) {
    SmallVector<StringRef, 1> ReqFeatures;
    const char *FeatureList =
        CGM.getContext().BuiltinInfo.getRequiredFeatures(BuiltinID);
    // Return if the builtin doesn't have any required features.
    if (!FeatureList || StringRef(FeatureList) == "")
      return;
    StringRef(FeatureList).split(ReqFeatures, ',');
    if (!hasRequiredFeatures(ReqFeatures, CGM, FD, MissingFeature))
      CGM.getDiags().Report(E->getBeginLoc(), diag::err_builtin_needs_feature)
          << TargetDecl->getDeclName()
          << CGM.getContext().BuiltinInfo.getRequiredFeatures(BuiltinID);

  } else if (TargetDecl->hasAttr<TargetAttr>() ||
             TargetDecl->hasAttr<CPUSpecificAttr>()) {
    // Get the required features for the callee.

    const TargetAttr *TD = TargetDecl->getAttr<TargetAttr>();
    TargetAttr::ParsedTargetAttr ParsedAttr = CGM.filterFunctionTargetAttrs(TD);

    SmallVector<StringRef, 1> ReqFeatures;
    llvm::StringMap<bool> CalleeFeatureMap;
    CGM.getFunctionFeatureMap(CalleeFeatureMap, TargetDecl);

    for (const auto &F : ParsedAttr.Features) {
      if (F[0] == '+' && CalleeFeatureMap.lookup(F.substr(1)))
        ReqFeatures.push_back(StringRef(F).substr(1));
    }

    for (const auto &F : CalleeFeatureMap) {
      // Only positive features are "required".
      if (F.getValue())
        ReqFeatures.push_back(F.getKey());
    }
    if (!hasRequiredFeatures(ReqFeatures, CGM, FD, MissingFeature))
      CGM.getDiags().Report(E->getBeginLoc(), diag::err_function_needs_feature)
          << FD->getDeclName() << TargetDecl->getDeclName() << MissingFeature;
  }
}

void CodeGenFunction::EmitSanitizerStatReport(llvm::SanitizerStatKind SSK) {
  if (!CGM.getCodeGenOpts().SanitizeStats)
    return;

  llvm::IRBuilder<> IRB(Builder.GetInsertBlock(), Builder.GetInsertPoint());
  IRB.SetCurrentDebugLocation(Builder.getCurrentDebugLocation());
  CGM.getSanStats().create(IRB, SSK);
}

llvm::Value *
CodeGenFunction::FormResolverCondition(const MultiVersionResolverOption &RO) {
  llvm::Value *Condition = nullptr;

  if (!RO.Conditions.Architecture.empty())
    Condition = EmitX86CpuIs(RO.Conditions.Architecture);

  if (!RO.Conditions.Features.empty()) {
    llvm::Value *FeatureCond = EmitX86CpuSupports(RO.Conditions.Features);
    Condition =
        Condition ? Builder.CreateAnd(Condition, FeatureCond) : FeatureCond;
  }
  return Condition;
}

static void CreateMultiVersionResolverReturn(CodeGenModule &CGM,
                                             llvm::Function *Resolver,
                                             CGBuilderTy &Builder,
                                             llvm::Function *FuncToReturn,
                                             bool SupportsIFunc) {
  if (SupportsIFunc) {
    Builder.CreateRet(FuncToReturn);
    return;
  }

  llvm::SmallVector<llvm::Value *, 10> Args;
  llvm::for_each(Resolver->args(),
                 [&](llvm::Argument &Arg) { Args.push_back(&Arg); });

  llvm::CallInst *Result = Builder.CreateCall(FuncToReturn, Args);
  Result->setTailCallKind(llvm::CallInst::TCK_MustTail);

  if (Resolver->getReturnType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(Result);
}

void CodeGenFunction::EmitMultiVersionResolver(
    llvm::Function *Resolver, ArrayRef<MultiVersionResolverOption> Options) {
  assert((getContext().getTargetInfo().getTriple().getArch() ==
              llvm::Triple::x86 ||
          getContext().getTargetInfo().getTriple().getArch() ==
              llvm::Triple::x86_64) &&
         "Only implemented for x86 targets");

  bool SupportsIFunc = getContext().getTargetInfo().supportsIFunc();

  // Main function's basic block.
  llvm::BasicBlock *CurBlock = createBasicBlock("resolver_entry", Resolver);
  Builder.SetInsertPoint(CurBlock);
  EmitX86CpuInit();

  for (const MultiVersionResolverOption &RO : Options) {
    Builder.SetInsertPoint(CurBlock);
    llvm::Value *Condition = FormResolverCondition(RO);

    // The 'default' or 'generic' case.
    if (!Condition) {
      assert(&RO == Options.end() - 1 &&
             "Default or Generic case must be last");
      CreateMultiVersionResolverReturn(CGM, Resolver, Builder, RO.Function,
                                       SupportsIFunc);
      return;
    }

    llvm::BasicBlock *RetBlock = createBasicBlock("resolver_return", Resolver);
    CGBuilderTy RetBuilder(*this, RetBlock);
    CreateMultiVersionResolverReturn(CGM, Resolver, RetBuilder, RO.Function,
                                     SupportsIFunc);
    CurBlock = createBasicBlock("resolver_else", Resolver);
    Builder.CreateCondBr(Condition, RetBlock, CurBlock);
  }

  // If no generic/default, emit an unreachable.
  Builder.SetInsertPoint(CurBlock);
  llvm::CallInst *TrapCall = EmitTrapCall(llvm::Intrinsic::trap);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  Builder.CreateUnreachable();
  Builder.ClearInsertionPoint();
}

// Loc - where the diagnostic will point, where in the source code this
//  alignment has failed.
// SecondaryLoc - if present (will be present if sufficiently different from
//  Loc), the diagnostic will additionally point a "Note:" to this location.
//  It should be the location where the __attribute__((assume_aligned))
//  was written e.g.
void CodeGenFunction::EmitAlignmentAssumptionCheck(
    llvm::Value *Ptr, QualType Ty, SourceLocation Loc,
    SourceLocation SecondaryLoc, llvm::Value *Alignment,
    llvm::Value *OffsetValue, llvm::Value *TheCheck,
    llvm::Instruction *Assumption) {
  assert(Assumption && isa<llvm::CallInst>(Assumption) &&
         cast<llvm::CallInst>(Assumption)->getCalledValue() ==
             llvm::Intrinsic::getDeclaration(
                 Builder.GetInsertBlock()->getParent()->getParent(),
                 llvm::Intrinsic::assume) &&
         "Assumption should be a call to llvm.assume().");
  assert(&(Builder.GetInsertBlock()->back()) == Assumption &&
         "Assumption should be the last instruction of the basic block, "
         "since the basic block is still being generated.");

  if (!SanOpts.has(SanitizerKind::Alignment))
    return;

  // Don't check pointers to volatile data. The behavior here is implementation-
  // defined.
  if (Ty->getPointeeType().isVolatileQualified())
    return;

  // We need to temorairly remove the assumption so we can insert the
  // sanitizer check before it, else the check will be dropped by optimizations.
  Assumption->removeFromParent();

  {
    SanitizerScope SanScope(this);

    if (!OffsetValue)
      OffsetValue = Builder.getInt1(0); // no offset.

    llvm::Constant *StaticData[] = {EmitCheckSourceLocation(Loc),
                                    EmitCheckSourceLocation(SecondaryLoc),
                                    EmitCheckTypeDescriptor(Ty)};
    llvm::Value *DynamicData[] = {EmitCheckValue(Ptr),
                                  EmitCheckValue(Alignment),
                                  EmitCheckValue(OffsetValue)};
    EmitCheck({std::make_pair(TheCheck, SanitizerKind::Alignment)},
              SanitizerHandler::AlignmentAssumption, StaticData, DynamicData);
  }

  // We are now in the (new, empty) "cont" basic block.
  // Reintroduce the assumption.
  Builder.Insert(Assumption);
  // FIXME: Assumption still has it's original basic block as it's Parent.
}

llvm::DebugLoc CodeGenFunction::SourceLocToDebugLoc(SourceLocation Location) {
  if (CGDebugInfo *DI = getDebugInfo())
    return DI->SourceLocToDebugLoc(Location);

  return llvm::DebugLoc();
}
