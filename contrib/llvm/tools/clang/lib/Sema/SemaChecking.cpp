//===- SemaChecking.cpp - Extra Semantic Checking -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements extra semantic analysis beyond what is enforced
//  by the C type system.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/FormatString.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/NonTrivialTypeVisitor.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/SyncScope.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetCXXABI.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/Lexer.h" // TODO: Extract static functions to fix layering.
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;
using namespace sema;

SourceLocation Sema::getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                    unsigned ByteNo) const {
  return SL->getLocationOfByte(ByteNo, getSourceManager(), LangOpts,
                               Context.getTargetInfo());
}

/// Checks that a call expression's argument count is the desired number.
/// This is useful when doing custom type-checking.  Returns true on error.
static bool checkArgCount(Sema &S, CallExpr *call, unsigned desiredArgCount) {
  unsigned argCount = call->getNumArgs();
  if (argCount == desiredArgCount) return false;

  if (argCount < desiredArgCount)
    return S.Diag(call->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << 0 /*function call*/ << desiredArgCount << argCount
           << call->getSourceRange();

  // Highlight all the excess arguments.
  SourceRange range(call->getArg(desiredArgCount)->getBeginLoc(),
                    call->getArg(argCount - 1)->getEndLoc());

  return S.Diag(range.getBegin(), diag::err_typecheck_call_too_many_args)
    << 0 /*function call*/ << desiredArgCount << argCount
    << call->getArg(1)->getSourceRange();
}

/// Check that the first argument to __builtin_annotation is an integer
/// and the second argument is a non-wide string literal.
static bool SemaBuiltinAnnotation(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 2))
    return true;

  // First argument should be an integer.
  Expr *ValArg = TheCall->getArg(0);
  QualType Ty = ValArg->getType();
  if (!Ty->isIntegerType()) {
    S.Diag(ValArg->getBeginLoc(), diag::err_builtin_annotation_first_arg)
        << ValArg->getSourceRange();
    return true;
  }

  // Second argument should be a constant string.
  Expr *StrArg = TheCall->getArg(1)->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(StrArg);
  if (!Literal || !Literal->isAscii()) {
    S.Diag(StrArg->getBeginLoc(), diag::err_builtin_annotation_second_arg)
        << StrArg->getSourceRange();
    return true;
  }

  TheCall->setType(Ty);
  return false;
}

static bool SemaBuiltinMSVCAnnotation(Sema &S, CallExpr *TheCall) {
  // We need at least one argument.
  if (TheCall->getNumArgs() < 1) {
    S.Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 << TheCall->getNumArgs()
        << TheCall->getCallee()->getSourceRange();
    return true;
  }

  // All arguments should be wide string literals.
  for (Expr *Arg : TheCall->arguments()) {
    auto *Literal = dyn_cast<StringLiteral>(Arg->IgnoreParenCasts());
    if (!Literal || !Literal->isWide()) {
      S.Diag(Arg->getBeginLoc(), diag::err_msvc_annotation_wide_str)
          << Arg->getSourceRange();
      return true;
    }
  }

  return false;
}

/// Check that the argument to __builtin_addressof is a glvalue, and set the
/// result type to the corresponding pointer type.
static bool SemaBuiltinAddressof(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return true;

  ExprResult Arg(TheCall->getArg(0));
  QualType ResultType = S.CheckAddressOfOperand(Arg, TheCall->getBeginLoc());
  if (ResultType.isNull())
    return true;

  TheCall->setArg(0, Arg.get());
  TheCall->setType(ResultType);
  return false;
}

static bool SemaBuiltinOverflow(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 3))
    return true;

  // First two arguments should be integers.
  for (unsigned I = 0; I < 2; ++I) {
    ExprResult Arg = TheCall->getArg(I);
    QualType Ty = Arg.get()->getType();
    if (!Ty->isIntegerType()) {
      S.Diag(Arg.get()->getBeginLoc(), diag::err_overflow_builtin_must_be_int)
          << Ty << Arg.get()->getSourceRange();
      return true;
    }
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        S.getASTContext(), Ty, /*consume*/ false);
    Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(I, Arg.get());
  }

  // Third argument should be a pointer to a non-const integer.
  // IRGen correctly handles volatile, restrict, and address spaces, and
  // the other qualifiers aren't possible.
  {
    ExprResult Arg = TheCall->getArg(2);
    QualType Ty = Arg.get()->getType();
    const auto *PtrTy = Ty->getAs<PointerType>();
    if (!(PtrTy && PtrTy->getPointeeType()->isIntegerType() &&
          !PtrTy->getPointeeType().isConstQualified())) {
      S.Diag(Arg.get()->getBeginLoc(),
             diag::err_overflow_builtin_must_be_ptr_int)
          << Ty << Arg.get()->getSourceRange();
      return true;
    }
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        S.getASTContext(), Ty, /*consume*/ false);
    Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(2, Arg.get());
  }
  return false;
}

static void SemaBuiltinMemChkCall(Sema &S, FunctionDecl *FDecl,
                                  CallExpr *TheCall, unsigned SizeIdx,
                                  unsigned DstSizeIdx,
                                  StringRef LikelyMacroName) {
  if (TheCall->getNumArgs() <= SizeIdx ||
      TheCall->getNumArgs() <= DstSizeIdx)
    return;

  const Expr *SizeArg = TheCall->getArg(SizeIdx);
  const Expr *DstSizeArg = TheCall->getArg(DstSizeIdx);

  Expr::EvalResult SizeResult, DstSizeResult;

  // find out if both sizes are known at compile time
  if (!SizeArg->EvaluateAsInt(SizeResult, S.Context) ||
      !DstSizeArg->EvaluateAsInt(DstSizeResult, S.Context))
    return;

  llvm::APSInt Size = SizeResult.Val.getInt();
  llvm::APSInt DstSize = DstSizeResult.Val.getInt();

  if (Size.ule(DstSize))
    return;

  // Confirmed overflow, so generate the diagnostic.
  StringRef FunctionName = FDecl->getName();
  SourceLocation SL = TheCall->getBeginLoc();
  SourceManager &SM = S.getSourceManager();
  // If we're in an expansion of a macro whose name corresponds to this builtin,
  // use the simple macro name and location.
  if (SL.isMacroID() && Lexer::getImmediateMacroName(SL, SM, S.getLangOpts()) ==
                            LikelyMacroName) {
    FunctionName = LikelyMacroName;
    SL = SM.getImmediateMacroCallerLoc(SL);
  }

  S.Diag(SL, diag::warn_memcpy_chk_overflow)
      << FunctionName << DstSize.toString(/*Radix=*/10)
      << Size.toString(/*Radix=*/10);
}

static bool SemaBuiltinCallWithStaticChain(Sema &S, CallExpr *BuiltinCall) {
  if (checkArgCount(S, BuiltinCall, 2))
    return true;

  SourceLocation BuiltinLoc = BuiltinCall->getBeginLoc();
  Expr *Builtin = BuiltinCall->getCallee()->IgnoreImpCasts();
  Expr *Call = BuiltinCall->getArg(0);
  Expr *Chain = BuiltinCall->getArg(1);

  if (Call->getStmtClass() != Stmt::CallExprClass) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_not_call)
        << Call->getSourceRange();
    return true;
  }

  auto CE = cast<CallExpr>(Call);
  if (CE->getCallee()->getType()->isBlockPointerType()) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_block_call)
        << Call->getSourceRange();
    return true;
  }

  const Decl *TargetDecl = CE->getCalleeDecl();
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl))
    if (FD->getBuiltinID()) {
      S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_builtin_call)
          << Call->getSourceRange();
      return true;
    }

  if (isa<CXXPseudoDestructorExpr>(CE->getCallee()->IgnoreParens())) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_pdtor_call)
        << Call->getSourceRange();
    return true;
  }

  ExprResult ChainResult = S.UsualUnaryConversions(Chain);
  if (ChainResult.isInvalid())
    return true;
  if (!ChainResult.get()->getType()->isPointerType()) {
    S.Diag(BuiltinLoc, diag::err_second_argument_to_cwsc_not_pointer)
        << Chain->getSourceRange();
    return true;
  }

  QualType ReturnTy = CE->getCallReturnType(S.Context);
  QualType ArgTys[2] = { ReturnTy, ChainResult.get()->getType() };
  QualType BuiltinTy = S.Context.getFunctionType(
      ReturnTy, ArgTys, FunctionProtoType::ExtProtoInfo());
  QualType BuiltinPtrTy = S.Context.getPointerType(BuiltinTy);

  Builtin =
      S.ImpCastExprToType(Builtin, BuiltinPtrTy, CK_BuiltinFnToFnPtr).get();

  BuiltinCall->setType(CE->getType());
  BuiltinCall->setValueKind(CE->getValueKind());
  BuiltinCall->setObjectKind(CE->getObjectKind());
  BuiltinCall->setCallee(Builtin);
  BuiltinCall->setArg(1, ChainResult.get());

  return false;
}

static bool SemaBuiltinSEHScopeCheck(Sema &SemaRef, CallExpr *TheCall,
                                     Scope::ScopeFlags NeededScopeFlags,
                                     unsigned DiagID) {
  // Scopes aren't available during instantiation. Fortunately, builtin
  // functions cannot be template args so they cannot be formed through template
  // instantiation. Therefore checking once during the parse is sufficient.
  if (SemaRef.inTemplateInstantiation())
    return false;

  Scope *S = SemaRef.getCurScope();
  while (S && !S->isSEHExceptScope())
    S = S->getParent();
  if (!S || !(S->getFlags() & NeededScopeFlags)) {
    auto *DRE = cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
    SemaRef.Diag(TheCall->getExprLoc(), DiagID)
        << DRE->getDecl()->getIdentifier();
    return true;
  }

  return false;
}

static inline bool isBlockPointer(Expr *Arg) {
  return Arg->getType()->isBlockPointerType();
}

/// OpenCL C v2.0, s6.13.17.2 - Checks that the block parameters are all local
/// void*, which is a requirement of device side enqueue.
static bool checkOpenCLBlockArgs(Sema &S, Expr *BlockArg) {
  const BlockPointerType *BPT =
      cast<BlockPointerType>(BlockArg->getType().getCanonicalType());
  ArrayRef<QualType> Params =
      BPT->getPointeeType()->getAs<FunctionProtoType>()->getParamTypes();
  unsigned ArgCounter = 0;
  bool IllegalParams = false;
  // Iterate through the block parameters until either one is found that is not
  // a local void*, or the block is valid.
  for (ArrayRef<QualType>::iterator I = Params.begin(), E = Params.end();
       I != E; ++I, ++ArgCounter) {
    if (!(*I)->isPointerType() || !(*I)->getPointeeType()->isVoidType() ||
        (*I)->getPointeeType().getQualifiers().getAddressSpace() !=
            LangAS::opencl_local) {
      // Get the location of the error. If a block literal has been passed
      // (BlockExpr) then we can point straight to the offending argument,
      // else we just point to the variable reference.
      SourceLocation ErrorLoc;
      if (isa<BlockExpr>(BlockArg)) {
        BlockDecl *BD = cast<BlockExpr>(BlockArg)->getBlockDecl();
        ErrorLoc = BD->getParamDecl(ArgCounter)->getBeginLoc();
      } else if (isa<DeclRefExpr>(BlockArg)) {
        ErrorLoc = cast<DeclRefExpr>(BlockArg)->getBeginLoc();
      }
      S.Diag(ErrorLoc,
             diag::err_opencl_enqueue_kernel_blocks_non_local_void_args);
      IllegalParams = true;
    }
  }

  return IllegalParams;
}

static bool checkOpenCLSubgroupExt(Sema &S, CallExpr *Call) {
  if (!S.getOpenCLOptions().isEnabled("cl_khr_subgroups")) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_requires_extension)
        << 1 << Call->getDirectCallee() << "cl_khr_subgroups";
    return true;
  }
  return false;
}

static bool SemaOpenCLBuiltinNDRangeAndBlock(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 2))
    return true;

  if (checkOpenCLSubgroupExt(S, TheCall))
    return true;

  // First argument is an ndrange_t type.
  Expr *NDRangeArg = TheCall->getArg(0);
  if (NDRangeArg->getType().getUnqualifiedType().getAsString() != "ndrange_t") {
    S.Diag(NDRangeArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'ndrange_t'";
    return true;
  }

  Expr *BlockArg = TheCall->getArg(1);
  if (!isBlockPointer(BlockArg)) {
    S.Diag(BlockArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "block";
    return true;
  }
  return checkOpenCLBlockArgs(S, BlockArg);
}

/// OpenCL C v2.0, s6.13.17.6 - Check the argument to the
/// get_kernel_work_group_size
/// and get_kernel_preferred_work_group_size_multiple builtin functions.
static bool SemaOpenCLBuiltinKernelWorkGroupSize(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return true;

  Expr *BlockArg = TheCall->getArg(0);
  if (!isBlockPointer(BlockArg)) {
    S.Diag(BlockArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "block";
    return true;
  }
  return checkOpenCLBlockArgs(S, BlockArg);
}

/// Diagnose integer type and any valid implicit conversion to it.
static bool checkOpenCLEnqueueIntType(Sema &S, Expr *E,
                                      const QualType &IntType);

static bool checkOpenCLEnqueueLocalSizeArgs(Sema &S, CallExpr *TheCall,
                                            unsigned Start, unsigned End) {
  bool IllegalParams = false;
  for (unsigned I = Start; I <= End; ++I)
    IllegalParams |= checkOpenCLEnqueueIntType(S, TheCall->getArg(I),
                                              S.Context.getSizeType());
  return IllegalParams;
}

/// OpenCL v2.0, s6.13.17.1 - Check that sizes are provided for all
/// 'local void*' parameter of passed block.
static bool checkOpenCLEnqueueVariadicArgs(Sema &S, CallExpr *TheCall,
                                           Expr *BlockArg,
                                           unsigned NumNonVarArgs) {
  const BlockPointerType *BPT =
      cast<BlockPointerType>(BlockArg->getType().getCanonicalType());
  unsigned NumBlockParams =
      BPT->getPointeeType()->getAs<FunctionProtoType>()->getNumParams();
  unsigned TotalNumArgs = TheCall->getNumArgs();

  // For each argument passed to the block, a corresponding uint needs to
  // be passed to describe the size of the local memory.
  if (TotalNumArgs != NumBlockParams + NumNonVarArgs) {
    S.Diag(TheCall->getBeginLoc(),
           diag::err_opencl_enqueue_kernel_local_size_args);
    return true;
  }

  // Check that the sizes of the local memory are specified by integers.
  return checkOpenCLEnqueueLocalSizeArgs(S, TheCall, NumNonVarArgs,
                                         TotalNumArgs - 1);
}

/// OpenCL C v2.0, s6.13.17 - Enqueue kernel function contains four different
/// overload formats specified in Table 6.13.17.1.
/// int enqueue_kernel(queue_t queue,
///                    kernel_enqueue_flags_t flags,
///                    const ndrange_t ndrange,
///                    void (^block)(void))
/// int enqueue_kernel(queue_t queue,
///                    kernel_enqueue_flags_t flags,
///                    const ndrange_t ndrange,
///                    uint num_events_in_wait_list,
///                    clk_event_t *event_wait_list,
///                    clk_event_t *event_ret,
///                    void (^block)(void))
/// int enqueue_kernel(queue_t queue,
///                    kernel_enqueue_flags_t flags,
///                    const ndrange_t ndrange,
///                    void (^block)(local void*, ...),
///                    uint size0, ...)
/// int enqueue_kernel(queue_t queue,
///                    kernel_enqueue_flags_t flags,
///                    const ndrange_t ndrange,
///                    uint num_events_in_wait_list,
///                    clk_event_t *event_wait_list,
///                    clk_event_t *event_ret,
///                    void (^block)(local void*, ...),
///                    uint size0, ...)
static bool SemaOpenCLBuiltinEnqueueKernel(Sema &S, CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs < 4) {
    S.Diag(TheCall->getBeginLoc(), diag::err_typecheck_call_too_few_args);
    return true;
  }

  Expr *Arg0 = TheCall->getArg(0);
  Expr *Arg1 = TheCall->getArg(1);
  Expr *Arg2 = TheCall->getArg(2);
  Expr *Arg3 = TheCall->getArg(3);

  // First argument always needs to be a queue_t type.
  if (!Arg0->getType()->isQueueT()) {
    S.Diag(TheCall->getArg(0)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << S.Context.OCLQueueTy;
    return true;
  }

  // Second argument always needs to be a kernel_enqueue_flags_t enum value.
  if (!Arg1->getType()->isIntegerType()) {
    S.Diag(TheCall->getArg(1)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'kernel_enqueue_flags_t' (i.e. uint)";
    return true;
  }

  // Third argument is always an ndrange_t type.
  if (Arg2->getType().getUnqualifiedType().getAsString() != "ndrange_t") {
    S.Diag(TheCall->getArg(2)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'ndrange_t'";
    return true;
  }

  // With four arguments, there is only one form that the function could be
  // called in: no events and no variable arguments.
  if (NumArgs == 4) {
    // check that the last argument is the right block type.
    if (!isBlockPointer(Arg3)) {
      S.Diag(Arg3->getBeginLoc(), diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "block";
      return true;
    }
    // we have a block type, check the prototype
    const BlockPointerType *BPT =
        cast<BlockPointerType>(Arg3->getType().getCanonicalType());
    if (BPT->getPointeeType()->getAs<FunctionProtoType>()->getNumParams() > 0) {
      S.Diag(Arg3->getBeginLoc(),
             diag::err_opencl_enqueue_kernel_blocks_no_args);
      return true;
    }
    return false;
  }
  // we can have block + varargs.
  if (isBlockPointer(Arg3))
    return (checkOpenCLBlockArgs(S, Arg3) ||
            checkOpenCLEnqueueVariadicArgs(S, TheCall, Arg3, 4));
  // last two cases with either exactly 7 args or 7 args and varargs.
  if (NumArgs >= 7) {
    // check common block argument.
    Expr *Arg6 = TheCall->getArg(6);
    if (!isBlockPointer(Arg6)) {
      S.Diag(Arg6->getBeginLoc(), diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "block";
      return true;
    }
    if (checkOpenCLBlockArgs(S, Arg6))
      return true;

    // Forth argument has to be any integer type.
    if (!Arg3->getType()->isIntegerType()) {
      S.Diag(TheCall->getArg(3)->getBeginLoc(),
             diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "integer";
      return true;
    }
    // check remaining common arguments.
    Expr *Arg4 = TheCall->getArg(4);
    Expr *Arg5 = TheCall->getArg(5);

    // Fifth argument is always passed as a pointer to clk_event_t.
    if (!Arg4->isNullPointerConstant(S.Context,
                                     Expr::NPC_ValueDependentIsNotNull) &&
        !Arg4->getType()->getPointeeOrArrayElementType()->isClkEventT()) {
      S.Diag(TheCall->getArg(4)->getBeginLoc(),
             diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee()
          << S.Context.getPointerType(S.Context.OCLClkEventTy);
      return true;
    }

    // Sixth argument is always passed as a pointer to clk_event_t.
    if (!Arg5->isNullPointerConstant(S.Context,
                                     Expr::NPC_ValueDependentIsNotNull) &&
        !(Arg5->getType()->isPointerType() &&
          Arg5->getType()->getPointeeType()->isClkEventT())) {
      S.Diag(TheCall->getArg(5)->getBeginLoc(),
             diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee()
          << S.Context.getPointerType(S.Context.OCLClkEventTy);
      return true;
    }

    if (NumArgs == 7)
      return false;

    return checkOpenCLEnqueueVariadicArgs(S, TheCall, Arg6, 7);
  }

  // None of the specific case has been detected, give generic error
  S.Diag(TheCall->getBeginLoc(),
         diag::err_opencl_enqueue_kernel_incorrect_args);
  return true;
}

/// Returns OpenCL access qual.
static OpenCLAccessAttr *getOpenCLArgAccess(const Decl *D) {
    return D->getAttr<OpenCLAccessAttr>();
}

/// Returns true if pipe element type is different from the pointer.
static bool checkOpenCLPipeArg(Sema &S, CallExpr *Call) {
  const Expr *Arg0 = Call->getArg(0);
  // First argument type should always be pipe.
  if (!Arg0->getType()->isPipeType()) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_first_arg)
        << Call->getDirectCallee() << Arg0->getSourceRange();
    return true;
  }
  OpenCLAccessAttr *AccessQual =
      getOpenCLArgAccess(cast<DeclRefExpr>(Arg0)->getDecl());
  // Validates the access qualifier is compatible with the call.
  // OpenCL v2.0 s6.13.16 - The access qualifiers for pipe should only be
  // read_only and write_only, and assumed to be read_only if no qualifier is
  // specified.
  switch (Call->getDirectCallee()->getBuiltinID()) {
  case Builtin::BIread_pipe:
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_read_pipe:
    if (!(!AccessQual || AccessQual->isReadOnly())) {
      S.Diag(Arg0->getBeginLoc(),
             diag::err_opencl_builtin_pipe_invalid_access_modifier)
          << "read_only" << Arg0->getSourceRange();
      return true;
    }
    break;
  case Builtin::BIwrite_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
  case Builtin::BIsub_group_reserve_write_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
  case Builtin::BIsub_group_commit_write_pipe:
    if (!(AccessQual && AccessQual->isWriteOnly())) {
      S.Diag(Arg0->getBeginLoc(),
             diag::err_opencl_builtin_pipe_invalid_access_modifier)
          << "write_only" << Arg0->getSourceRange();
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

/// Returns true if pipe element type is different from the pointer.
static bool checkOpenCLPipePacketType(Sema &S, CallExpr *Call, unsigned Idx) {
  const Expr *Arg0 = Call->getArg(0);
  const Expr *ArgIdx = Call->getArg(Idx);
  const PipeType *PipeTy = cast<PipeType>(Arg0->getType());
  const QualType EltTy = PipeTy->getElementType();
  const PointerType *ArgTy = ArgIdx->getType()->getAs<PointerType>();
  // The Idx argument should be a pointer and the type of the pointer and
  // the type of pipe element should also be the same.
  if (!ArgTy ||
      !S.Context.hasSameType(
          EltTy, ArgTy->getPointeeType()->getCanonicalTypeInternal())) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << S.Context.getPointerType(EltTy)
        << ArgIdx->getType() << ArgIdx->getSourceRange();
    return true;
  }
  return false;
}

// Performs semantic analysis for the read/write_pipe call.
// \param S Reference to the semantic analyzer.
// \param Call A pointer to the builtin call.
// \return True if a semantic error has been found, false otherwise.
static bool SemaBuiltinRWPipe(Sema &S, CallExpr *Call) {
  // OpenCL v2.0 s6.13.16.2 - The built-in read/write
  // functions have two forms.
  switch (Call->getNumArgs()) {
  case 2:
    if (checkOpenCLPipeArg(S, Call))
      return true;
    // The call with 2 arguments should be
    // read/write_pipe(pipe T, T*).
    // Check packet type T.
    if (checkOpenCLPipePacketType(S, Call, 1))
      return true;
    break;

  case 4: {
    if (checkOpenCLPipeArg(S, Call))
      return true;
    // The call with 4 arguments should be
    // read/write_pipe(pipe T, reserve_id_t, uint, T*).
    // Check reserve_id_t.
    if (!Call->getArg(1)->getType()->isReserveIDT()) {
      S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
          << Call->getDirectCallee() << S.Context.OCLReserveIDTy
          << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
      return true;
    }

    // Check the index.
    const Expr *Arg2 = Call->getArg(2);
    if (!Arg2->getType()->isIntegerType() &&
        !Arg2->getType()->isUnsignedIntegerType()) {
      S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
          << Call->getDirectCallee() << S.Context.UnsignedIntTy
          << Arg2->getType() << Arg2->getSourceRange();
      return true;
    }

    // Check packet type T.
    if (checkOpenCLPipePacketType(S, Call, 3))
      return true;
  } break;
  default:
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_arg_num)
        << Call->getDirectCallee() << Call->getSourceRange();
    return true;
  }

  return false;
}

// Performs a semantic analysis on the {work_group_/sub_group_
//        /_}reserve_{read/write}_pipe
// \param S Reference to the semantic analyzer.
// \param Call The call to the builtin function to be analyzed.
// \return True if a semantic error was found, false otherwise.
static bool SemaBuiltinReserveRWPipe(Sema &S, CallExpr *Call) {
  if (checkArgCount(S, Call, 2))
    return true;

  if (checkOpenCLPipeArg(S, Call))
    return true;

  // Check the reserve size.
  if (!Call->getArg(1)->getType()->isIntegerType() &&
      !Call->getArg(1)->getType()->isUnsignedIntegerType()) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << S.Context.UnsignedIntTy
        << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
    return true;
  }

  // Since return type of reserve_read/write_pipe built-in function is
  // reserve_id_t, which is not defined in the builtin def file , we used int
  // as return type and need to override the return type of these functions.
  Call->setType(S.Context.OCLReserveIDTy);

  return false;
}

// Performs a semantic analysis on {work_group_/sub_group_
//        /_}commit_{read/write}_pipe
// \param S Reference to the semantic analyzer.
// \param Call The call to the builtin function to be analyzed.
// \return True if a semantic error was found, false otherwise.
static bool SemaBuiltinCommitRWPipe(Sema &S, CallExpr *Call) {
  if (checkArgCount(S, Call, 2))
    return true;

  if (checkOpenCLPipeArg(S, Call))
    return true;

  // Check reserve_id_t.
  if (!Call->getArg(1)->getType()->isReserveIDT()) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << S.Context.OCLReserveIDTy
        << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
    return true;
  }

  return false;
}

// Performs a semantic analysis on the call to built-in Pipe
//        Query Functions.
// \param S Reference to the semantic analyzer.
// \param Call The call to the builtin function to be analyzed.
// \return True if a semantic error was found, false otherwise.
static bool SemaBuiltinPipePackets(Sema &S, CallExpr *Call) {
  if (checkArgCount(S, Call, 1))
    return true;

  if (!Call->getArg(0)->getType()->isPipeType()) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_first_arg)
        << Call->getDirectCallee() << Call->getArg(0)->getSourceRange();
    return true;
  }

  return false;
}

// OpenCL v2.0 s6.13.9 - Address space qualifier functions.
// Performs semantic analysis for the to_global/local/private call.
// \param S Reference to the semantic analyzer.
// \param BuiltinID ID of the builtin function.
// \param Call A pointer to the builtin call.
// \return True if a semantic error has been found, false otherwise.
static bool SemaOpenCLBuiltinToAddr(Sema &S, unsigned BuiltinID,
                                    CallExpr *Call) {
  if (Call->getNumArgs() != 1) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_to_addr_arg_num)
        << Call->getDirectCallee() << Call->getSourceRange();
    return true;
  }

  auto RT = Call->getArg(0)->getType();
  if (!RT->isPointerType() || RT->getPointeeType()
      .getAddressSpace() == LangAS::opencl_constant) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_to_addr_invalid_arg)
        << Call->getArg(0) << Call->getDirectCallee() << Call->getSourceRange();
    return true;
  }

  if (RT->getPointeeType().getAddressSpace() != LangAS::opencl_generic) {
    S.Diag(Call->getArg(0)->getBeginLoc(),
           diag::warn_opencl_generic_address_space_arg)
        << Call->getDirectCallee()->getNameInfo().getAsString()
        << Call->getArg(0)->getSourceRange();
  }

  RT = RT->getPointeeType();
  auto Qual = RT.getQualifiers();
  switch (BuiltinID) {
  case Builtin::BIto_global:
    Qual.setAddressSpace(LangAS::opencl_global);
    break;
  case Builtin::BIto_local:
    Qual.setAddressSpace(LangAS::opencl_local);
    break;
  case Builtin::BIto_private:
    Qual.setAddressSpace(LangAS::opencl_private);
    break;
  default:
    llvm_unreachable("Invalid builtin function");
  }
  Call->setType(S.Context.getPointerType(S.Context.getQualifiedType(
      RT.getUnqualifiedType(), Qual)));

  return false;
}

static ExprResult SemaBuiltinLaunder(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return ExprError();

  // Compute __builtin_launder's parameter type from the argument.
  // The parameter type is:
  //  * The type of the argument if it's not an array or function type,
  //  Otherwise,
  //  * The decayed argument type.
  QualType ParamTy = [&]() {
    QualType ArgTy = TheCall->getArg(0)->getType();
    if (const ArrayType *Ty = ArgTy->getAsArrayTypeUnsafe())
      return S.Context.getPointerType(Ty->getElementType());
    if (ArgTy->isFunctionType()) {
      return S.Context.getPointerType(ArgTy);
    }
    return ArgTy;
  }();

  TheCall->setType(ParamTy);

  auto DiagSelect = [&]() -> llvm::Optional<unsigned> {
    if (!ParamTy->isPointerType())
      return 0;
    if (ParamTy->isFunctionPointerType())
      return 1;
    if (ParamTy->isVoidPointerType())
      return 2;
    return llvm::Optional<unsigned>{};
  }();
  if (DiagSelect.hasValue()) {
    S.Diag(TheCall->getBeginLoc(), diag::err_builtin_launder_invalid_arg)
        << DiagSelect.getValue() << TheCall->getSourceRange();
    return ExprError();
  }

  // We either have an incomplete class type, or we have a class template
  // whose instantiation has not been forced. Example:
  //
  //   template <class T> struct Foo { T value; };
  //   Foo<int> *p = nullptr;
  //   auto *d = __builtin_launder(p);
  if (S.RequireCompleteType(TheCall->getBeginLoc(), ParamTy->getPointeeType(),
                            diag::err_incomplete_type))
    return ExprError();

  assert(ParamTy->getPointeeType()->isObjectType() &&
         "Unhandled non-object pointer case");

  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, ParamTy, false);
  ExprResult Arg =
      S.PerformCopyInitialization(Entity, SourceLocation(), TheCall->getArg(0));
  if (Arg.isInvalid())
    return ExprError();
  TheCall->setArg(0, Arg.get());

  return TheCall;
}

// Emit an error and return true if the current architecture is not in the list
// of supported architectures.
static bool
CheckBuiltinTargetSupport(Sema &S, unsigned BuiltinID, CallExpr *TheCall,
                          ArrayRef<llvm::Triple::ArchType> SupportedArchs) {
  llvm::Triple::ArchType CurArch =
      S.getASTContext().getTargetInfo().getTriple().getArch();
  if (llvm::is_contained(SupportedArchs, CurArch))
    return false;
  S.Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
      << TheCall->getSourceRange();
  return true;
}

ExprResult
Sema::CheckBuiltinFunctionCall(FunctionDecl *FDecl, unsigned BuiltinID,
                               CallExpr *TheCall) {
  ExprResult TheCallResult(TheCall);

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  Context.GetBuiltinType(BuiltinID, Error, &ICEArguments);
  if (Error != ASTContext::GE_None)
    ICEArguments = 0;  // Don't diagnose previously diagnosed errors.

  // If any arguments are required to be ICE's, check and diagnose.
  for (unsigned ArgNo = 0; ICEArguments != 0; ++ArgNo) {
    // Skip arguments not required to be ICE's.
    if ((ICEArguments & (1 << ArgNo)) == 0) continue;

    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, ArgNo, Result))
      return true;
    ICEArguments &= ~(1 << ArgNo);
  }

  switch (BuiltinID) {
  case Builtin::BI__builtin___CFStringMakeConstantString:
    assert(TheCall->getNumArgs() == 1 &&
           "Wrong # arguments to builtin CFStringMakeConstantString");
    if (CheckObjCString(TheCall->getArg(0)))
      return ExprError();
    break;
  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
    if (SemaBuiltinVAStart(BuiltinID, TheCall))
      return ExprError();
    break;
  case Builtin::BI__va_start: {
    switch (Context.getTargetInfo().getTriple().getArch()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      if (SemaBuiltinVAStartARMMicrosoft(TheCall))
        return ExprError();
      break;
    default:
      if (SemaBuiltinVAStart(BuiltinID, TheCall))
        return ExprError();
      break;
    }
    break;
  }

  // The acquire, release, and no fence variants are ARM and AArch64 only.
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    if (CheckBuiltinTargetSupport(
            *this, BuiltinID, TheCall,
            {llvm::Triple::arm, llvm::Triple::thumb, llvm::Triple::aarch64}))
      return ExprError();
    break;

  // The 64-bit bittest variants are x64, ARM, and AArch64 only.
  case Builtin::BI_bittest64:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
    if (CheckBuiltinTargetSupport(*this, BuiltinID, TheCall,
                                  {llvm::Triple::x86_64, llvm::Triple::arm,
                                   llvm::Triple::thumb, llvm::Triple::aarch64}))
      return ExprError();
    break;

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered:
    if (SemaBuiltinUnorderedCompare(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_fpclassify:
    if (SemaBuiltinFPClassification(TheCall, 6))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfinite:
  case Builtin::BI__builtin_isinf:
  case Builtin::BI__builtin_isinf_sign:
  case Builtin::BI__builtin_isnan:
  case Builtin::BI__builtin_isnormal:
  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl:
    if (SemaBuiltinFPClassification(TheCall, 1))
      return ExprError();
    break;
  case Builtin::BI__builtin_shufflevector:
    return SemaBuiltinShuffleVector(TheCall);
    // TheCall will be freed by the smart pointer here, but that's fine, since
    // SemaBuiltinShuffleVector guts it, but then doesn't release it.
  case Builtin::BI__builtin_prefetch:
    if (SemaBuiltinPrefetch(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_alloca_with_align:
    if (SemaBuiltinAllocaWithAlign(TheCall))
      return ExprError();
    break;
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume:
    if (SemaBuiltinAssume(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_assume_aligned:
    if (SemaBuiltinAssumeAligned(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_object_size:
    if (SemaBuiltinConstantArgRange(TheCall, 1, 0, 3))
      return ExprError();
    break;
  case Builtin::BI__builtin_longjmp:
    if (SemaBuiltinLongjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_setjmp:
    if (SemaBuiltinSetjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI_setjmp:
  case Builtin::BI_setjmpex:
    if (checkArgCount(*this, TheCall, 1))
      return true;
    break;
  case Builtin::BI__builtin_classify_type:
    if (checkArgCount(*this, TheCall, 1)) return true;
    TheCall->setType(Context.IntTy);
    break;
  case Builtin::BI__builtin_constant_p:
    if (checkArgCount(*this, TheCall, 1)) return true;
    TheCall->setType(Context.IntTy);
    break;
  case Builtin::BI__builtin_launder:
    return SemaBuiltinLaunder(*this, TheCall);
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return SemaBuiltinAtomicOverloaded(TheCallResult);
  case Builtin::BI__sync_synchronize:
    Diag(TheCall->getBeginLoc(), diag::warn_atomic_implicit_seq_cst)
        << TheCall->getCallee()->getSourceRange();
    break;
  case Builtin::BI__builtin_nontemporal_load:
  case Builtin::BI__builtin_nontemporal_store:
    return SemaBuiltinNontemporalOverloaded(TheCallResult);
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS) \
  case Builtin::BI##ID: \
    return SemaAtomicOpsOverloaded(TheCallResult, AtomicExpr::AO##ID);
#include "clang/Basic/Builtins.def"
  case Builtin::BI__annotation:
    if (SemaBuiltinMSVCAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_annotation:
    if (SemaBuiltinAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_addressof:
    if (SemaBuiltinAddressof(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
    if (SemaBuiltinOverflow(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_operator_new:
  case Builtin::BI__builtin_operator_delete: {
    bool IsDelete = BuiltinID == Builtin::BI__builtin_operator_delete;
    ExprResult Res =
        SemaBuiltinOperatorNewDeleteOverloaded(TheCallResult, IsDelete);
    if (Res.isInvalid())
      CorrectDelayedTyposInExpr(TheCallResult.get());
    return Res;
  }
  case Builtin::BI__builtin_dump_struct: {
    // We first want to ensure we are called with 2 arguments
    if (checkArgCount(*this, TheCall, 2))
      return ExprError();
    // Ensure that the first argument is of type 'struct XX *'
    const Expr *PtrArg = TheCall->getArg(0)->IgnoreParenImpCasts();
    const QualType PtrArgType = PtrArg->getType();
    if (!PtrArgType->isPointerType() ||
        !PtrArgType->getPointeeType()->isRecordType()) {
      Diag(PtrArg->getBeginLoc(), diag::err_typecheck_convert_incompatible)
          << PtrArgType << "structure pointer" << 1 << 0 << 3 << 1 << PtrArgType
          << "structure pointer";
      return ExprError();
    }

    // Ensure that the second argument is of type 'FunctionType'
    const Expr *FnPtrArg = TheCall->getArg(1)->IgnoreImpCasts();
    const QualType FnPtrArgType = FnPtrArg->getType();
    if (!FnPtrArgType->isPointerType()) {
      Diag(FnPtrArg->getBeginLoc(), diag::err_typecheck_convert_incompatible)
          << FnPtrArgType << "'int (*)(const char *, ...)'" << 1 << 0 << 3 << 2
          << FnPtrArgType << "'int (*)(const char *, ...)'";
      return ExprError();
    }

    const auto *FuncType =
        FnPtrArgType->getPointeeType()->getAs<FunctionType>();

    if (!FuncType) {
      Diag(FnPtrArg->getBeginLoc(), diag::err_typecheck_convert_incompatible)
          << FnPtrArgType << "'int (*)(const char *, ...)'" << 1 << 0 << 3 << 2
          << FnPtrArgType << "'int (*)(const char *, ...)'";
      return ExprError();
    }

    if (const auto *FT = dyn_cast<FunctionProtoType>(FuncType)) {
      if (!FT->getNumParams()) {
        Diag(FnPtrArg->getBeginLoc(), diag::err_typecheck_convert_incompatible)
            << FnPtrArgType << "'int (*)(const char *, ...)'" << 1 << 0 << 3
            << 2 << FnPtrArgType << "'int (*)(const char *, ...)'";
        return ExprError();
      }
      QualType PT = FT->getParamType(0);
      if (!FT->isVariadic() || FT->getReturnType() != Context.IntTy ||
          !PT->isPointerType() || !PT->getPointeeType()->isCharType() ||
          !PT->getPointeeType().isConstQualified()) {
        Diag(FnPtrArg->getBeginLoc(), diag::err_typecheck_convert_incompatible)
            << FnPtrArgType << "'int (*)(const char *, ...)'" << 1 << 0 << 3
            << 2 << FnPtrArgType << "'int (*)(const char *, ...)'";
        return ExprError();
      }
    }

    TheCall->setType(Context.IntTy);
    break;
  }

  // check secure string manipulation functions where overflows
  // are detectable at compile time
  case Builtin::BI__builtin___memcpy_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "memcpy");
    break;
  case Builtin::BI__builtin___memmove_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "memmove");
    break;
  case Builtin::BI__builtin___memset_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "memset");
    break;
  case Builtin::BI__builtin___strlcat_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "strlcat");
    break;
  case Builtin::BI__builtin___strlcpy_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "strlcpy");
    break;
  case Builtin::BI__builtin___strncat_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "strncat");
    break;
  case Builtin::BI__builtin___strncpy_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "strncpy");
    break;
  case Builtin::BI__builtin___stpncpy_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 2, 3, "stpncpy");
    break;
  case Builtin::BI__builtin___memccpy_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 3, 4, "memccpy");
    break;
  case Builtin::BI__builtin___snprintf_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 1, 3, "snprintf");
    break;
  case Builtin::BI__builtin___vsnprintf_chk:
    SemaBuiltinMemChkCall(*this, FDecl, TheCall, 1, 3, "vsnprintf");
    break;
  case Builtin::BI__builtin_call_with_static_chain:
    if (SemaBuiltinCallWithStaticChain(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    if (SemaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHExceptScope,
                                 diag::err_seh___except_block))
      return ExprError();
    break;
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    if (SemaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHFilterScope,
                                 diag::err_seh___except_filter))
      return ExprError();
    break;
  case Builtin::BI__GetExceptionInfo:
    if (checkArgCount(*this, TheCall, 1))
      return ExprError();

    if (CheckCXXThrowOperand(
            TheCall->getBeginLoc(),
            Context.getExceptionObjectType(FDecl->getParamDecl(0)->getType()),
            TheCall))
      return ExprError();

    TheCall->setType(Context.VoidPtrTy);
    break;
  // OpenCL v2.0, s6.13.16 - Pipe functions
  case Builtin::BIread_pipe:
  case Builtin::BIwrite_pipe:
    // Since those two functions are declared with var args, we need a semantic
    // check for the argument.
    if (SemaBuiltinRWPipe(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
    if (SemaBuiltinReserveRWPipe(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_write_pipe:
    if (checkOpenCLSubgroupExt(*this, TheCall) ||
        SemaBuiltinReserveRWPipe(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
    if (SemaBuiltinCommitRWPipe(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIsub_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_write_pipe:
    if (checkOpenCLSubgroupExt(*this, TheCall) ||
        SemaBuiltinCommitRWPipe(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIget_pipe_num_packets:
  case Builtin::BIget_pipe_max_packets:
    if (SemaBuiltinPipePackets(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIto_global:
  case Builtin::BIto_local:
  case Builtin::BIto_private:
    if (SemaOpenCLBuiltinToAddr(*this, BuiltinID, TheCall))
      return ExprError();
    break;
  // OpenCL v2.0, s6.13.17 - Enqueue kernel functions.
  case Builtin::BIenqueue_kernel:
    if (SemaOpenCLBuiltinEnqueueKernel(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIget_kernel_work_group_size:
  case Builtin::BIget_kernel_preferred_work_group_size_multiple:
    if (SemaOpenCLBuiltinKernelWorkGroupSize(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BIget_kernel_max_sub_group_size_for_ndrange:
  case Builtin::BIget_kernel_sub_group_count_for_ndrange:
    if (SemaOpenCLBuiltinNDRangeAndBlock(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_os_log_format:
  case Builtin::BI__builtin_os_log_format_buffer_size:
    if (SemaBuiltinOSLogFormat(TheCall))
      return ExprError();
    break;
  }

  // Since the target specific builtins for each arch overlap, only check those
  // of the arch we are compiling for.
  if (Context.BuiltinInfo.isTSBuiltin(BuiltinID)) {
    switch (Context.getTargetInfo().getTriple().getArch()) {
      case llvm::Triple::arm:
      case llvm::Triple::armeb:
      case llvm::Triple::thumb:
      case llvm::Triple::thumbeb:
        if (CheckARMBuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::aarch64:
      case llvm::Triple::aarch64_be:
        if (CheckAArch64BuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::hexagon:
        if (CheckHexagonBuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::mips:
      case llvm::Triple::mipsel:
      case llvm::Triple::mips64:
      case llvm::Triple::mips64el:
        if (CheckMipsBuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::systemz:
        if (CheckSystemZBuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        if (CheckX86BuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
      case llvm::Triple::ppc64le:
        if (CheckPPCBuiltinFunctionCall(BuiltinID, TheCall))
          return ExprError();
        break;
      default:
        break;
    }
  }

  return TheCallResult;
}

// Get the valid immediate range for the specified NEON type code.
static unsigned RFT(unsigned t, bool shift = false, bool ForceQuad = false) {
  NeonTypeFlags Type(t);
  int IsQuad = ForceQuad ? true : Type.isQuad();
  switch (Type.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return shift ? 7 : (8 << IsQuad) - 1;
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return shift ? 15 : (4 << IsQuad) - 1;
  case NeonTypeFlags::Int32:
    return shift ? 31 : (2 << IsQuad) - 1;
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return shift ? 63 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Poly128:
    return shift ? 127 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Float16:
    assert(!shift && "cannot shift float types!");
    return (4 << IsQuad) - 1;
  case NeonTypeFlags::Float32:
    assert(!shift && "cannot shift float types!");
    return (2 << IsQuad) - 1;
  case NeonTypeFlags::Float64:
    assert(!shift && "cannot shift float types!");
    return (1 << IsQuad) - 1;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}

/// getNeonEltType - Return the QualType corresponding to the elements of
/// the vector type specified by the NeonTypeFlags.  This is used to check
/// the pointer arguments for Neon load/store intrinsics.
static QualType getNeonEltType(NeonTypeFlags Flags, ASTContext &Context,
                               bool IsPolyUnsigned, bool IsInt64Long) {
  switch (Flags.getEltType()) {
  case NeonTypeFlags::Int8:
    return Flags.isUnsigned() ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Int16:
    return Flags.isUnsigned() ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Int32:
    return Flags.isUnsigned() ? Context.UnsignedIntTy : Context.IntTy;
  case NeonTypeFlags::Int64:
    if (IsInt64Long)
      return Flags.isUnsigned() ? Context.UnsignedLongTy : Context.LongTy;
    else
      return Flags.isUnsigned() ? Context.UnsignedLongLongTy
                                : Context.LongLongTy;
  case NeonTypeFlags::Poly8:
    return IsPolyUnsigned ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Poly16:
    return IsPolyUnsigned ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Poly64:
    if (IsInt64Long)
      return Context.UnsignedLongTy;
    else
      return Context.UnsignedLongLongTy;
  case NeonTypeFlags::Poly128:
    break;
  case NeonTypeFlags::Float16:
    return Context.HalfTy;
  case NeonTypeFlags::Float32:
    return Context.FloatTy;
  case NeonTypeFlags::Float64:
    return Context.DoubleTy;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}

bool Sema::CheckNeonBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  llvm::APSInt Result;
  uint64_t mask = 0;
  unsigned TV = 0;
  int PtrArgNum = -1;
  bool HasConstPtr = false;
  switch (BuiltinID) {
#define GET_NEON_OVERLOAD_CHECK
#include "clang/Basic/arm_neon.inc"
#include "clang/Basic/arm_fp16.inc"
#undef GET_NEON_OVERLOAD_CHECK
  }

  // For NEON intrinsics which are overloaded on vector element type, validate
  // the immediate which specifies which variant to emit.
  unsigned ImmArg = TheCall->getNumArgs()-1;
  if (mask) {
    if (SemaBuiltinConstantArg(TheCall, ImmArg, Result))
      return true;

    TV = Result.getLimitedValue(64);
    if ((TV > 63) || (mask & (1ULL << TV)) == 0)
      return Diag(TheCall->getBeginLoc(), diag::err_invalid_neon_type_code)
             << TheCall->getArg(ImmArg)->getSourceRange();
  }

  if (PtrArgNum >= 0) {
    // Check that pointer arguments have the specified type.
    Expr *Arg = TheCall->getArg(PtrArgNum);
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg))
      Arg = ICE->getSubExpr();
    ExprResult RHS = DefaultFunctionArrayLvalueConversion(Arg);
    QualType RHSTy = RHS.get()->getType();

    llvm::Triple::ArchType Arch = Context.getTargetInfo().getTriple().getArch();
    bool IsPolyUnsigned = Arch == llvm::Triple::aarch64 ||
                          Arch == llvm::Triple::aarch64_be;
    bool IsInt64Long =
        Context.getTargetInfo().getInt64Type() == TargetInfo::SignedLong;
    QualType EltTy =
        getNeonEltType(NeonTypeFlags(TV), Context, IsPolyUnsigned, IsInt64Long);
    if (HasConstPtr)
      EltTy = EltTy.withConst();
    QualType LHSTy = Context.getPointerType(EltTy);
    AssignConvertType ConvTy;
    ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return true;
    if (DiagnoseAssignmentResult(ConvTy, Arg->getBeginLoc(), LHSTy, RHSTy,
                                 RHS.get(), AA_Assigning))
      return true;
  }

  // For NEON intrinsics which take an immediate value as part of the
  // instruction, range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  #define GET_NEON_IMMEDIATE_CHECK
  #include "clang/Basic/arm_neon.inc"
  #include "clang/Basic/arm_fp16.inc"
  #undef GET_NEON_IMMEDIATE_CHECK
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

bool Sema::CheckARMBuiltinExclusiveCall(unsigned BuiltinID, CallExpr *TheCall,
                                        unsigned MaxWidth) {
  assert((BuiltinID == ARM::BI__builtin_arm_ldrex ||
          BuiltinID == ARM::BI__builtin_arm_ldaex ||
          BuiltinID == ARM::BI__builtin_arm_strex ||
          BuiltinID == ARM::BI__builtin_arm_stlex ||
          BuiltinID == AArch64::BI__builtin_arm_ldrex ||
          BuiltinID == AArch64::BI__builtin_arm_ldaex ||
          BuiltinID == AArch64::BI__builtin_arm_strex ||
          BuiltinID == AArch64::BI__builtin_arm_stlex) &&
         "unexpected ARM builtin");
  bool IsLdrex = BuiltinID == ARM::BI__builtin_arm_ldrex ||
                 BuiltinID == ARM::BI__builtin_arm_ldaex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldrex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldaex;

  DeclRefExpr *DRE =cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(*this, TheCall, IsLdrex ? 1 : 2))
    return true;

  // Inspect the pointer argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(IsLdrex ? 0 : 1);
  ExprResult PointerArgRes = DefaultFunctionArrayLvalueConversion(PointerArg);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // ldrex takes a "const volatile T*" and strex takes a "volatile T*". Our next
  // task is to insert the appropriate casts into the AST. First work out just
  // what the appropriate type is.
  QualType ValType = pointerType->getPointeeType();
  QualType AddrType = ValType.getUnqualifiedType().withVolatile();
  if (IsLdrex)
    AddrType.addConst();

  // Issue a warning if the cast is dodgy.
  CastKind CastNeeded = CK_NoOp;
  if (!AddrType.isAtLeastAsQualifiedAs(ValType)) {
    CastNeeded = CK_BitCast;
    Diag(DRE->getBeginLoc(), diag::ext_typecheck_convert_discards_qualifiers)
        << PointerArg->getType() << Context.getPointerType(AddrType)
        << AA_Passing << PointerArg->getSourceRange();
  }

  // Finally, do the cast and replace the argument with the corrected version.
  AddrType = Context.getPointerType(AddrType);
  PointerArgRes = ImpCastExprToType(PointerArg, AddrType, CastNeeded);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  TheCall->setArg(IsLdrex ? 0 : 1, PointerArg);

  // In general, we allow ints, floats and pointers to be loaded and stored.
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType() && !ValType->isFloatingType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intfltptr)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // But ARM doesn't have instructions to deal with 128-bit versions.
  if (Context.getTypeSize(ValType) > MaxWidth) {
    assert(MaxWidth == 64 && "Diagnostic unexpectedly inaccurate");
    Diag(DRE->getBeginLoc(), diag::err_atomic_exclusive_builtin_pointer_size)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    Diag(DRE->getBeginLoc(), diag::err_arc_atomic_ownership)
        << ValType << PointerArg->getSourceRange();
    return true;
  }

  if (IsLdrex) {
    TheCall->setType(ValType);
    return false;
  }

  // Initialize the argument to be stored.
  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return true;
  TheCall->setArg(0, ValArg.get());

  // __builtin_arm_strex always returns an int. It's marked as such in the .def,
  // but the custom checker bypasses all default analysis.
  TheCall->setType(Context.IntTy);
  return false;
}

bool Sema::CheckARMBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (BuiltinID == ARM::BI__builtin_arm_ldrex ||
      BuiltinID == ARM::BI__builtin_arm_ldaex ||
      BuiltinID == ARM::BI__builtin_arm_strex ||
      BuiltinID == ARM::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 64);
  }

  if (BuiltinID == ARM::BI__builtin_arm_prefetch) {
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1) ||
      SemaBuiltinConstantArgRange(TheCall, 2, 0, 1);
  }

  if (BuiltinID == ARM::BI__builtin_arm_rsr64 ||
      BuiltinID == ARM::BI__builtin_arm_wsr64)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 3, false);

  if (BuiltinID == ARM::BI__builtin_arm_rsr ||
      BuiltinID == ARM::BI__builtin_arm_rsrp ||
      BuiltinID == ARM::BI__builtin_arm_wsr ||
      BuiltinID == ARM::BI__builtin_arm_wsrp)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  if (CheckNeonBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  // FIXME: VFP Intrinsics should error if VFP not present.
  switch (BuiltinID) {
  default: return false;
  case ARM::BI__builtin_arm_ssat:
    return SemaBuiltinConstantArgRange(TheCall, 1, 1, 32);
  case ARM::BI__builtin_arm_usat:
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 31);
  case ARM::BI__builtin_arm_ssat16:
    return SemaBuiltinConstantArgRange(TheCall, 1, 1, 16);
  case ARM::BI__builtin_arm_usat16:
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 15);
  case ARM::BI__builtin_arm_vcvtr_f:
  case ARM::BI__builtin_arm_vcvtr_d:
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1);
  case ARM::BI__builtin_arm_dmb:
  case ARM::BI__builtin_arm_dsb:
  case ARM::BI__builtin_arm_isb:
  case ARM::BI__builtin_arm_dbg:
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 15);
  }
}

bool Sema::CheckAArch64BuiltinFunctionCall(unsigned BuiltinID,
                                         CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_ldrex ||
      BuiltinID == AArch64::BI__builtin_arm_ldaex ||
      BuiltinID == AArch64::BI__builtin_arm_strex ||
      BuiltinID == AArch64::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 128);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_prefetch) {
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1) ||
      SemaBuiltinConstantArgRange(TheCall, 2, 0, 2) ||
      SemaBuiltinConstantArgRange(TheCall, 3, 0, 1) ||
      SemaBuiltinConstantArgRange(TheCall, 4, 0, 1);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr64)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  if (BuiltinID == AArch64::BI__builtin_arm_rsr ||
      BuiltinID == AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == AArch64::BI__builtin_arm_wsr ||
      BuiltinID == AArch64::BI__builtin_arm_wsrp)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Only check the valid encoding range. Any constant in this range would be
  // converted to a register of the form S1_2_C3_C4_5. Let the hardware throw
  // an exception for incorrect registers. This matches MSVC behavior.
  if (BuiltinID == AArch64::BI_ReadStatusReg ||
      BuiltinID == AArch64::BI_WriteStatusReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 0x7fff);

  if (BuiltinID == AArch64::BI__getReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 31);

  if (CheckNeonBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default: return false;
  case AArch64::BI__builtin_arm_dmb:
  case AArch64::BI__builtin_arm_dsb:
  case AArch64::BI__builtin_arm_isb: l = 0; u = 15; break;
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

bool Sema::CheckHexagonBuiltinCpu(unsigned BuiltinID, CallExpr *TheCall) {
  struct BuiltinAndString {
    unsigned BuiltinID;
    const char *Str;
  };

  static BuiltinAndString ValidCPU[] = {
    { Hexagon::BI__builtin_HEXAGON_A6_vcmpbeq_notany, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_A6_vminub_RdP, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_F2_dfadd, "v66" },
    { Hexagon::BI__builtin_HEXAGON_F2_dfsub, "v66" },
    { Hexagon::BI__builtin_HEXAGON_M2_mnaci, "v66" },
    { Hexagon::BI__builtin_HEXAGON_M6_vabsdiffb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_M6_vabsdiffub, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S2_mask, "v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_nac, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_xacc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_nac, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_xacc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_vsplatrbp, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_vtrunehb_ppp, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_S6_vtrunohb_ppp, "v62,v65,v66" },
  };

  static BuiltinAndString ValidHVX[] = {
    { Hexagon::BI__builtin_HEXAGON_V6_hi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_hi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lo, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lo_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_extractw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_extractw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplatb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplatb_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplath, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplath_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplatw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_lvsplatw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_and_n, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_and_n_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_not, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_not_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_or_n, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_or_n_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_scalar2, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_scalar2_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_scalar2v2, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_scalar2v2_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_pred_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_shuffeqh, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_shuffeqh_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_shuffeqw, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_shuffeqw_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsb, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsb_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsb_sat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsb_sat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsdiffw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsh_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsh_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsw_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vabsw_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddb_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddb_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddbsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddbsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddbsat_dv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddbsat_dv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddcarry, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddcarry_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddcarrysat, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddcarrysat_128B, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddclbh, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddclbh_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddclbw, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddclbw_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddh_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddh_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhw_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddhw_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubh_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubh_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddubsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddububb_sat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddububb_sat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhw_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduhw_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduwsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduwsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduwsat_dv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vadduwsat_dv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddw_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddw_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddwsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddwsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddwsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaddwsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_valignb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_valignb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vand, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vand_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandnqrt, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandnqrt_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandnqrt_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandnqrt_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandqrt, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandqrt_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandqrt_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandqrt_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvnqv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvnqv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvqv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvqv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvrt, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvrt_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvrt_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vandvrt_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslh_acc, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslh_acc_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslhv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslhv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslw_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslw_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslwv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vaslwv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrh_acc, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrh_acc_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhbrndsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhbrndsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhbsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhbsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhubrndsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhubrndsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhubsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhubsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrhv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasr_into, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasr_into_128B, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruhubrndsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruhubrndsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruhubsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruhubsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruwuhrndsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruwuhrndsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruwuhsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasruwuhsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrw_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrw_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwhrndsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwhrndsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwuhrndsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwuhrndsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwuhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwuhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vasrwv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vassign, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vassign_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vassignp, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vassignp_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgb, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgb_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgbrnd, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgbrnd_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavghrnd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavghrnd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgubrnd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgubrnd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguhrnd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguhrnd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguw, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguw_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguwrnd, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavguwrnd_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgwrnd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vavgwrnd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcl0h, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcl0h_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcl0w, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcl0w_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcombine, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vcombine_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vd0, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vd0_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdd0, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdd0_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealb4w, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealb4w_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealvdd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdealvdd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdelta, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdelta_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_dv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpybus_dv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_dv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhb_dv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhisat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhisat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhisat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhisat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsuisat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsuisat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsuisat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsuisat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsusat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsusat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsusat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhsusat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhvsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhvsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhvsat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdmpyhvsat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdsaduh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdsaduh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdsaduh_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vdsaduh_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqb_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqh_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_veqw_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtb_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgth_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtub_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuh_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtuw_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_and, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_and_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_or, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_or_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_xor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vgtw_xor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vinsertwr, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vinsertwr_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrb_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrhv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrhv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrwv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlsrwv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlut4, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlut4_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvbi, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvbi_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_nm, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_nm_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracci, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracci_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwhi, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwhi_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_nm, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_nm_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracci, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracci_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxb_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmaxw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminb_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vminw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabusv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabusv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuu, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuu_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuu_acc, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuu_acc_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuuv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpabuuv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahhsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpahhsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhb, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhb_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhb_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhb_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhuhsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpauhuhsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpsuhuhsat, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpsuhuhsat_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybusv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybusv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybusv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybusv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpybv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyewuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyewuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyewuh_64, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyewuh_64_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyh_acc, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyh_acc_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhsat_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhsat_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhsrs, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhsrs_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhss, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhss_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhvsrs, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyhvsrs_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyieoh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyieoh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewh_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewh_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewuh_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiewuh_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyih, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyih_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyih_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyih_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyihb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyihb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyihb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyihb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiowh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiowh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwh_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwh_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwub, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwub_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwub_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyiwub_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_64_acc, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_64_acc_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_rnd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_rnd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_rnd_sacc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_rnd_sacc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_sacc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyowh_sacc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyub_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyub_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyubv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyubv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyubv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyubv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuh_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuh_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhe, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhe_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhe_acc, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhe_acc_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmpyuhv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmux, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vmux_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgb, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgb_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnavgw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnormamth, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnormamth_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnormamtw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnormamtw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnot, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vnot_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackeb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackeb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackeh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackeh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackhb_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackhb_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackhub_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackhub_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackob, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackob_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackoh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackoh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackwh_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackwh_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackwuh_sat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpackwuh_sat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpopcounth, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vpopcounth_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqb, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqb_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqh, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqh_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqw, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vprefixqw_128B, "v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrdelta, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrdelta_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybub_rtt, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybub_rtt_128B, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybub_rtt_acc, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybub_rtt_acc_128B, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_rtt, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_rtt_128B, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_rtt_acc, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyub_rtt_acc_128B, "v65" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubv_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubv_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vror, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vror_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrotr, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrotr_128B, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundhb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundhb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundhub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundhub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrounduhub, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrounduhub_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrounduwuh, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrounduwuh_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundwh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundwh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundwuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vroundwuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatdw, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatdw_128B, "v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsathub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsathub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatuwuh, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatuwuh_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatwh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsatwh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufeh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufeh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffeb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffeb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffob, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffob_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffvdd, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshuffvdd_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoeb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoeb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoeh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoeh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vshufoh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubb_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubb_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubbsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubbsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubbsat_dv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubbsat_dv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubcarry, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubcarry_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubh_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubh_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubhw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsububsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubububb_sat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubububb_sat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuhw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuwsat, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuwsat_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuwsat_dv, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubuwsat_dv_128B, "v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubw, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubw_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubw_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubw_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubwsat, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubwsat_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubwsat_dv, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vsubwsat_dv_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vswap, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vswap_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpybus, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpybus_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpybus_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpybus_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyhb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyhb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyhb_acc, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vtmpyhb_acc_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackob, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackob_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackoh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackoh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackub, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackub_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackuh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vunpackuh_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vxor, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vxor_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vzb, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vzb_128B, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vzh, "v60,v62,v65,v66" },
    { Hexagon::BI__builtin_HEXAGON_V6_vzh_128B, "v60,v62,v65,v66" },
  };

  // Sort the tables on first execution so we can binary search them.
  auto SortCmp = [](const BuiltinAndString &LHS, const BuiltinAndString &RHS) {
    return LHS.BuiltinID < RHS.BuiltinID;
  };
  static const bool SortOnce =
      (llvm::sort(ValidCPU, SortCmp),
       llvm::sort(ValidHVX, SortCmp), true);
  (void)SortOnce;
  auto LowerBoundCmp = [](const BuiltinAndString &BI, unsigned BuiltinID) {
    return BI.BuiltinID < BuiltinID;
  };

  const TargetInfo &TI = Context.getTargetInfo();

  const BuiltinAndString *FC =
      std::lower_bound(std::begin(ValidCPU), std::end(ValidCPU), BuiltinID,
                       LowerBoundCmp);
  if (FC != std::end(ValidCPU) && FC->BuiltinID == BuiltinID) {
    const TargetOptions &Opts = TI.getTargetOpts();
    StringRef CPU = Opts.CPU;
    if (!CPU.empty()) {
      assert(CPU.startswith("hexagon") && "Unexpected CPU name");
      CPU.consume_front("hexagon");
      SmallVector<StringRef, 3> CPUs;
      StringRef(FC->Str).split(CPUs, ',');
      if (llvm::none_of(CPUs, [CPU](StringRef S) { return S == CPU; }))
        return Diag(TheCall->getBeginLoc(),
                    diag::err_hexagon_builtin_unsupported_cpu);
    }
  }

  const BuiltinAndString *FH =
      std::lower_bound(std::begin(ValidHVX), std::end(ValidHVX), BuiltinID,
                       LowerBoundCmp);
  if (FH != std::end(ValidHVX) && FH->BuiltinID == BuiltinID) {
    if (!TI.hasFeature("hvx"))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_hexagon_builtin_requires_hvx);

    SmallVector<StringRef, 3> HVXs;
    StringRef(FH->Str).split(HVXs, ',');
    bool IsValid = llvm::any_of(HVXs,
                                [&TI] (StringRef V) {
                                  std::string F = "hvx" + V.str();
                                  return TI.hasFeature(F);
                                });
    if (!IsValid)
      return Diag(TheCall->getBeginLoc(),
                  diag::err_hexagon_builtin_unsupported_hvx);
  }

  return false;
}

bool Sema::CheckHexagonBuiltinArgument(unsigned BuiltinID, CallExpr *TheCall) {
  struct ArgInfo {
    uint8_t OpNum;
    bool IsSigned;
    uint8_t BitWidth;
    uint8_t Align;
  };
  struct BuiltinInfo {
    unsigned BuiltinID;
    ArgInfo Infos[2];
  };

  static BuiltinInfo Infos[] = {
    { Hexagon::BI__builtin_circ_ldd,                  {{ 3, true,  4,  3 }} },
    { Hexagon::BI__builtin_circ_ldw,                  {{ 3, true,  4,  2 }} },
    { Hexagon::BI__builtin_circ_ldh,                  {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_lduh,                 {{ 3, true,  4,  0 }} },
    { Hexagon::BI__builtin_circ_ldb,                  {{ 3, true,  4,  0 }} },
    { Hexagon::BI__builtin_circ_ldub,                 {{ 3, true,  4,  0 }} },
    { Hexagon::BI__builtin_circ_std,                  {{ 3, true,  4,  3 }} },
    { Hexagon::BI__builtin_circ_stw,                  {{ 3, true,  4,  2 }} },
    { Hexagon::BI__builtin_circ_sth,                  {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_sthhi,                {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_stb,                  {{ 3, true,  4,  0 }} },

    { Hexagon::BI__builtin_HEXAGON_L2_loadrub_pci,    {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrb_pci,     {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadruh_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrh_pci,     {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadri_pci,     {{ 1, true,  4,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrd_pci,     {{ 1, true,  4,  3 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerb_pci,    {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerh_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerf_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storeri_pci,    {{ 1, true,  4,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerd_pci,    {{ 1, true,  4,  3 }} },

    { Hexagon::BI__builtin_HEXAGON_A2_combineii,      {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfrih,          {{ 1, false, 16, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfril,          {{ 1, false, 16, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfrpi,          {{ 0, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_bitspliti,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cmpbeqi,        {{ 1, false, 8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cmpbgti,        {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cround_ri,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_round_ri,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_round_ri_sat,   {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbeqi,       {{ 1, false, 8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpheqi,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmphgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmphgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpweqi,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpwgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpwgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C2_bitsclri,       {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C2_muxii,          {{ 2, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C4_nbitsclri,      {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfclass,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfimm_n,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfimm_p,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfclass,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfimm_n,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfimm_p,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_M4_mpyri_addi,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_M4_mpyri_addr_u2,  {{ 1, false, 6,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_addasl_rrri,    {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_sat,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_rnd_goodsyntax,
                                                      {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_rnd,    {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_rnd_goodsyntax,
                                                      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_rnd,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_svw_trun, {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_clrbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_extractu,       {{ 1, false, 5,  0 },
                                                       { 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_extractup,      {{ 1, false, 6,  0 },
                                                       { 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_insert,         {{ 2, false, 5,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_insertp,        {{ 2, false, 6,  0 },
                                                       { 3, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_setbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxb_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxd_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxh_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxw_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_togglebit_i,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tstbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_valignib,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_vspliceib,      {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_addi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_addi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_andi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_andi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_clbaddi,        {{ 1, true , 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_clbpaddi,       {{ 1, true,  6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_extract,        {{ 1, false, 5,  0 },
                                                       { 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_extractp,       {{ 1, false, 6,  0 },
                                                       { 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_lsli,           {{ 0, true,  6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ntstbit_i,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ori_asl_ri,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ori_lsr_ri,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_subi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_subi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_vrcrotate_acc,  {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_vrcrotate,      {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_asrhub_rnd_sat_goodsyntax,
                                                      {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_asrhub_sat,     {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_vasrhrnd_goodsyntax,
                                                      {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi_128B,  {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi,      {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi_128B, {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi,      {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_128B, {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc,  {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi,       {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_128B,  {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc,   {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi,       {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_128B,  {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc,   {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },
  };

  // Use a dynamically initialized static to sort the table exactly once on
  // first run.
  static const bool SortOnce =
      (llvm::sort(Infos,
                 [](const BuiltinInfo &LHS, const BuiltinInfo &RHS) {
                   return LHS.BuiltinID < RHS.BuiltinID;
                 }),
       true);
  (void)SortOnce;

  const BuiltinInfo *F =
      std::lower_bound(std::begin(Infos), std::end(Infos), BuiltinID,
                       [](const BuiltinInfo &BI, unsigned BuiltinID) {
                         return BI.BuiltinID < BuiltinID;
                       });
  if (F == std::end(Infos) || F->BuiltinID != BuiltinID)
    return false;

  bool Error = false;

  for (const ArgInfo &A : F->Infos) {
    // Ignore empty ArgInfo elements.
    if (A.BitWidth == 0)
      continue;

    int32_t Min = A.IsSigned ? -(1 << (A.BitWidth - 1)) : 0;
    int32_t Max = (1 << (A.IsSigned ? A.BitWidth - 1 : A.BitWidth)) - 1;
    if (!A.Align) {
      Error |= SemaBuiltinConstantArgRange(TheCall, A.OpNum, Min, Max);
    } else {
      unsigned M = 1 << A.Align;
      Min *= M;
      Max *= M;
      Error |= SemaBuiltinConstantArgRange(TheCall, A.OpNum, Min, Max) |
               SemaBuiltinConstantArgMultiple(TheCall, A.OpNum, M);
    }
  }
  return Error;
}

bool Sema::CheckHexagonBuiltinFunctionCall(unsigned BuiltinID,
                                           CallExpr *TheCall) {
  return CheckHexagonBuiltinCpu(BuiltinID, TheCall) ||
         CheckHexagonBuiltinArgument(BuiltinID, TheCall);
}


// CheckMipsBuiltinFunctionCall - Checks the constant value passed to the
// intrinsic is correct. The switch statement is ordered by DSP, MSA. The
// ordering for DSP is unspecified. MSA is ordered by the data format used
// by the underlying instruction i.e., df/m, df/n and then by size.
//
// FIXME: The size tests here should instead be tablegen'd along with the
//        definitions from include/clang/Basic/BuiltinsMips.def.
// FIXME: GCC is strict on signedness for some of these intrinsics, we should
//        be too.
bool Sema::CheckMipsBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  unsigned i = 0, l = 0, u = 0, m = 0;
  switch (BuiltinID) {
  default: return false;
  case Mips::BI__builtin_mips_wrdsp: i = 1; l = 0; u = 63; break;
  case Mips::BI__builtin_mips_rddsp: i = 0; l = 0; u = 63; break;
  case Mips::BI__builtin_mips_append: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_balign: i = 2; l = 0; u = 3; break;
  case Mips::BI__builtin_mips_precr_sra_ph_w: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_precr_sra_r_ph_w: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_prepend: i = 2; l = 0; u = 31; break;
  // MSA intrinsics. Instructions (which the intrinsics maps to) which use the
  // df/m field.
  // These intrinsics take an unsigned 3 bit immediate.
  case Mips::BI__builtin_msa_bclri_b:
  case Mips::BI__builtin_msa_bnegi_b:
  case Mips::BI__builtin_msa_bseti_b:
  case Mips::BI__builtin_msa_sat_s_b:
  case Mips::BI__builtin_msa_sat_u_b:
  case Mips::BI__builtin_msa_slli_b:
  case Mips::BI__builtin_msa_srai_b:
  case Mips::BI__builtin_msa_srari_b:
  case Mips::BI__builtin_msa_srli_b:
  case Mips::BI__builtin_msa_srlri_b: i = 1; l = 0; u = 7; break;
  case Mips::BI__builtin_msa_binsli_b:
  case Mips::BI__builtin_msa_binsri_b: i = 2; l = 0; u = 7; break;
  // These intrinsics take an unsigned 4 bit immediate.
  case Mips::BI__builtin_msa_bclri_h:
  case Mips::BI__builtin_msa_bnegi_h:
  case Mips::BI__builtin_msa_bseti_h:
  case Mips::BI__builtin_msa_sat_s_h:
  case Mips::BI__builtin_msa_sat_u_h:
  case Mips::BI__builtin_msa_slli_h:
  case Mips::BI__builtin_msa_srai_h:
  case Mips::BI__builtin_msa_srari_h:
  case Mips::BI__builtin_msa_srli_h:
  case Mips::BI__builtin_msa_srlri_h: i = 1; l = 0; u = 15; break;
  case Mips::BI__builtin_msa_binsli_h:
  case Mips::BI__builtin_msa_binsri_h: i = 2; l = 0; u = 15; break;
  // These intrinsics take an unsigned 5 bit immediate.
  // The first block of intrinsics actually have an unsigned 5 bit field,
  // not a df/n field.
  case Mips::BI__builtin_msa_clei_u_b:
  case Mips::BI__builtin_msa_clei_u_h:
  case Mips::BI__builtin_msa_clei_u_w:
  case Mips::BI__builtin_msa_clei_u_d:
  case Mips::BI__builtin_msa_clti_u_b:
  case Mips::BI__builtin_msa_clti_u_h:
  case Mips::BI__builtin_msa_clti_u_w:
  case Mips::BI__builtin_msa_clti_u_d:
  case Mips::BI__builtin_msa_maxi_u_b:
  case Mips::BI__builtin_msa_maxi_u_h:
  case Mips::BI__builtin_msa_maxi_u_w:
  case Mips::BI__builtin_msa_maxi_u_d:
  case Mips::BI__builtin_msa_mini_u_b:
  case Mips::BI__builtin_msa_mini_u_h:
  case Mips::BI__builtin_msa_mini_u_w:
  case Mips::BI__builtin_msa_mini_u_d:
  case Mips::BI__builtin_msa_addvi_b:
  case Mips::BI__builtin_msa_addvi_h:
  case Mips::BI__builtin_msa_addvi_w:
  case Mips::BI__builtin_msa_addvi_d:
  case Mips::BI__builtin_msa_bclri_w:
  case Mips::BI__builtin_msa_bnegi_w:
  case Mips::BI__builtin_msa_bseti_w:
  case Mips::BI__builtin_msa_sat_s_w:
  case Mips::BI__builtin_msa_sat_u_w:
  case Mips::BI__builtin_msa_slli_w:
  case Mips::BI__builtin_msa_srai_w:
  case Mips::BI__builtin_msa_srari_w:
  case Mips::BI__builtin_msa_srli_w:
  case Mips::BI__builtin_msa_srlri_w:
  case Mips::BI__builtin_msa_subvi_b:
  case Mips::BI__builtin_msa_subvi_h:
  case Mips::BI__builtin_msa_subvi_w:
  case Mips::BI__builtin_msa_subvi_d: i = 1; l = 0; u = 31; break;
  case Mips::BI__builtin_msa_binsli_w:
  case Mips::BI__builtin_msa_binsri_w: i = 2; l = 0; u = 31; break;
  // These intrinsics take an unsigned 6 bit immediate.
  case Mips::BI__builtin_msa_bclri_d:
  case Mips::BI__builtin_msa_bnegi_d:
  case Mips::BI__builtin_msa_bseti_d:
  case Mips::BI__builtin_msa_sat_s_d:
  case Mips::BI__builtin_msa_sat_u_d:
  case Mips::BI__builtin_msa_slli_d:
  case Mips::BI__builtin_msa_srai_d:
  case Mips::BI__builtin_msa_srari_d:
  case Mips::BI__builtin_msa_srli_d:
  case Mips::BI__builtin_msa_srlri_d: i = 1; l = 0; u = 63; break;
  case Mips::BI__builtin_msa_binsli_d:
  case Mips::BI__builtin_msa_binsri_d: i = 2; l = 0; u = 63; break;
  // These intrinsics take a signed 5 bit immediate.
  case Mips::BI__builtin_msa_ceqi_b:
  case Mips::BI__builtin_msa_ceqi_h:
  case Mips::BI__builtin_msa_ceqi_w:
  case Mips::BI__builtin_msa_ceqi_d:
  case Mips::BI__builtin_msa_clti_s_b:
  case Mips::BI__builtin_msa_clti_s_h:
  case Mips::BI__builtin_msa_clti_s_w:
  case Mips::BI__builtin_msa_clti_s_d:
  case Mips::BI__builtin_msa_clei_s_b:
  case Mips::BI__builtin_msa_clei_s_h:
  case Mips::BI__builtin_msa_clei_s_w:
  case Mips::BI__builtin_msa_clei_s_d:
  case Mips::BI__builtin_msa_maxi_s_b:
  case Mips::BI__builtin_msa_maxi_s_h:
  case Mips::BI__builtin_msa_maxi_s_w:
  case Mips::BI__builtin_msa_maxi_s_d:
  case Mips::BI__builtin_msa_mini_s_b:
  case Mips::BI__builtin_msa_mini_s_h:
  case Mips::BI__builtin_msa_mini_s_w:
  case Mips::BI__builtin_msa_mini_s_d: i = 1; l = -16; u = 15; break;
  // These intrinsics take an unsigned 8 bit immediate.
  case Mips::BI__builtin_msa_andi_b:
  case Mips::BI__builtin_msa_nori_b:
  case Mips::BI__builtin_msa_ori_b:
  case Mips::BI__builtin_msa_shf_b:
  case Mips::BI__builtin_msa_shf_h:
  case Mips::BI__builtin_msa_shf_w:
  case Mips::BI__builtin_msa_xori_b: i = 1; l = 0; u = 255; break;
  case Mips::BI__builtin_msa_bseli_b:
  case Mips::BI__builtin_msa_bmnzi_b:
  case Mips::BI__builtin_msa_bmzi_b: i = 2; l = 0; u = 255; break;
  // df/n format
  // These intrinsics take an unsigned 4 bit immediate.
  case Mips::BI__builtin_msa_copy_s_b:
  case Mips::BI__builtin_msa_copy_u_b:
  case Mips::BI__builtin_msa_insve_b:
  case Mips::BI__builtin_msa_splati_b: i = 1; l = 0; u = 15; break;
  case Mips::BI__builtin_msa_sldi_b: i = 2; l = 0; u = 15; break;
  // These intrinsics take an unsigned 3 bit immediate.
  case Mips::BI__builtin_msa_copy_s_h:
  case Mips::BI__builtin_msa_copy_u_h:
  case Mips::BI__builtin_msa_insve_h:
  case Mips::BI__builtin_msa_splati_h: i = 1; l = 0; u = 7; break;
  case Mips::BI__builtin_msa_sldi_h: i = 2; l = 0; u = 7; break;
  // These intrinsics take an unsigned 2 bit immediate.
  case Mips::BI__builtin_msa_copy_s_w:
  case Mips::BI__builtin_msa_copy_u_w:
  case Mips::BI__builtin_msa_insve_w:
  case Mips::BI__builtin_msa_splati_w: i = 1; l = 0; u = 3; break;
  case Mips::BI__builtin_msa_sldi_w: i = 2; l = 0; u = 3; break;
  // These intrinsics take an unsigned 1 bit immediate.
  case Mips::BI__builtin_msa_copy_s_d:
  case Mips::BI__builtin_msa_copy_u_d:
  case Mips::BI__builtin_msa_insve_d:
  case Mips::BI__builtin_msa_splati_d: i = 1; l = 0; u = 1; break;
  case Mips::BI__builtin_msa_sldi_d: i = 2; l = 0; u = 1; break;
  // Memory offsets and immediate loads.
  // These intrinsics take a signed 10 bit immediate.
  case Mips::BI__builtin_msa_ldi_b: i = 0; l = -128; u = 255; break;
  case Mips::BI__builtin_msa_ldi_h:
  case Mips::BI__builtin_msa_ldi_w:
  case Mips::BI__builtin_msa_ldi_d: i = 0; l = -512; u = 511; break;
  case Mips::BI__builtin_msa_ld_b: i = 1; l = -512; u = 511; m = 1; break;
  case Mips::BI__builtin_msa_ld_h: i = 1; l = -1024; u = 1022; m = 2; break;
  case Mips::BI__builtin_msa_ld_w: i = 1; l = -2048; u = 2044; m = 4; break;
  case Mips::BI__builtin_msa_ld_d: i = 1; l = -4096; u = 4088; m = 8; break;
  case Mips::BI__builtin_msa_st_b: i = 2; l = -512; u = 511; m = 1; break;
  case Mips::BI__builtin_msa_st_h: i = 2; l = -1024; u = 1022; m = 2; break;
  case Mips::BI__builtin_msa_st_w: i = 2; l = -2048; u = 2044; m = 4; break;
  case Mips::BI__builtin_msa_st_d: i = 2; l = -4096; u = 4088; m = 8; break;
  }

  if (!m)
    return SemaBuiltinConstantArgRange(TheCall, i, l, u);

  return SemaBuiltinConstantArgRange(TheCall, i, l, u) ||
         SemaBuiltinConstantArgMultiple(TheCall, i, m);
}

bool Sema::CheckPPCBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  unsigned i = 0, l = 0, u = 0;
  bool Is64BitBltin = BuiltinID == PPC::BI__builtin_divde ||
                      BuiltinID == PPC::BI__builtin_divdeu ||
                      BuiltinID == PPC::BI__builtin_bpermd;
  bool IsTarget64Bit = Context.getTargetInfo()
                              .getTypeWidth(Context
                                            .getTargetInfo()
                                            .getIntPtrType()) == 64;
  bool IsBltinExtDiv = BuiltinID == PPC::BI__builtin_divwe ||
                       BuiltinID == PPC::BI__builtin_divweu ||
                       BuiltinID == PPC::BI__builtin_divde ||
                       BuiltinID == PPC::BI__builtin_divdeu;

  if (Is64BitBltin && !IsTarget64Bit)
    return Diag(TheCall->getBeginLoc(), diag::err_64_bit_builtin_32_bit_tgt)
           << TheCall->getSourceRange();

  if ((IsBltinExtDiv && !Context.getTargetInfo().hasFeature("extdiv")) ||
      (BuiltinID == PPC::BI__builtin_bpermd &&
       !Context.getTargetInfo().hasFeature("bpermd")))
    return Diag(TheCall->getBeginLoc(), diag::err_ppc_builtin_only_on_pwr7)
           << TheCall->getSourceRange();

  auto SemaVSXCheck = [&](CallExpr *TheCall) -> bool {
    if (!Context.getTargetInfo().hasFeature("vsx"))
      return Diag(TheCall->getBeginLoc(), diag::err_ppc_builtin_only_on_pwr7)
             << TheCall->getSourceRange();
    return false;
  };

  switch (BuiltinID) {
  default: return false;
  case PPC::BI__builtin_altivec_crypto_vshasigmaw:
  case PPC::BI__builtin_altivec_crypto_vshasigmad:
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaBuiltinConstantArgRange(TheCall, 2, 0, 15);
  case PPC::BI__builtin_tbegin:
  case PPC::BI__builtin_tend: i = 0; l = 0; u = 1; break;
  case PPC::BI__builtin_tsr: i = 0; l = 0; u = 7; break;
  case PPC::BI__builtin_tabortwc:
  case PPC::BI__builtin_tabortdc: i = 0; l = 0; u = 31; break;
  case PPC::BI__builtin_tabortwci:
  case PPC::BI__builtin_tabortdci:
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 31) ||
           SemaBuiltinConstantArgRange(TheCall, 2, 0, 31);
  case PPC::BI__builtin_vsx_xxpermdi:
  case PPC::BI__builtin_vsx_xxsldwi:
    return SemaBuiltinVSX(TheCall);
  case PPC::BI__builtin_unpack_vector_int128:
    return SemaVSXCheck(TheCall) ||
           SemaBuiltinConstantArgRange(TheCall, 1, 0, 1);
  case PPC::BI__builtin_pack_vector_int128:
    return SemaVSXCheck(TheCall);
  }
  return SemaBuiltinConstantArgRange(TheCall, i, l, u);
}

bool Sema::CheckSystemZBuiltinFunctionCall(unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (BuiltinID == SystemZ::BI__builtin_tabort) {
    Expr *Arg = TheCall->getArg(0);
    llvm::APSInt AbortCode(32);
    if (Arg->isIntegerConstantExpr(AbortCode, Context) &&
        AbortCode.getSExtValue() >= 0 && AbortCode.getSExtValue() < 256)
      return Diag(Arg->getBeginLoc(), diag::err_systemz_invalid_tabort_code)
             << Arg->getSourceRange();
  }

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default: return false;
  case SystemZ::BI__builtin_s390_lcbb: i = 1; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_verimb:
  case SystemZ::BI__builtin_s390_verimh:
  case SystemZ::BI__builtin_s390_verimf:
  case SystemZ::BI__builtin_s390_verimg: i = 3; l = 0; u = 255; break;
  case SystemZ::BI__builtin_s390_vfaeb:
  case SystemZ::BI__builtin_s390_vfaeh:
  case SystemZ::BI__builtin_s390_vfaef:
  case SystemZ::BI__builtin_s390_vfaebs:
  case SystemZ::BI__builtin_s390_vfaehs:
  case SystemZ::BI__builtin_s390_vfaefs:
  case SystemZ::BI__builtin_s390_vfaezb:
  case SystemZ::BI__builtin_s390_vfaezh:
  case SystemZ::BI__builtin_s390_vfaezf:
  case SystemZ::BI__builtin_s390_vfaezbs:
  case SystemZ::BI__builtin_s390_vfaezhs:
  case SystemZ::BI__builtin_s390_vfaezfs: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vfisb:
  case SystemZ::BI__builtin_s390_vfidb:
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 15) ||
           SemaBuiltinConstantArgRange(TheCall, 2, 0, 15);
  case SystemZ::BI__builtin_s390_vftcisb:
  case SystemZ::BI__builtin_s390_vftcidb: i = 1; l = 0; u = 4095; break;
  case SystemZ::BI__builtin_s390_vlbb: i = 1; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vpdi: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vsldb: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vstrcb:
  case SystemZ::BI__builtin_s390_vstrch:
  case SystemZ::BI__builtin_s390_vstrcf:
  case SystemZ::BI__builtin_s390_vstrczb:
  case SystemZ::BI__builtin_s390_vstrczh:
  case SystemZ::BI__builtin_s390_vstrczf:
  case SystemZ::BI__builtin_s390_vstrcbs:
  case SystemZ::BI__builtin_s390_vstrchs:
  case SystemZ::BI__builtin_s390_vstrcfs:
  case SystemZ::BI__builtin_s390_vstrczbs:
  case SystemZ::BI__builtin_s390_vstrczhs:
  case SystemZ::BI__builtin_s390_vstrczfs: i = 3; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vmslg: i = 3; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vfminsb:
  case SystemZ::BI__builtin_s390_vfmaxsb:
  case SystemZ::BI__builtin_s390_vfmindb:
  case SystemZ::BI__builtin_s390_vfmaxdb: i = 2; l = 0; u = 15; break;
  }
  return SemaBuiltinConstantArgRange(TheCall, i, l, u);
}

/// SemaBuiltinCpuSupports - Handle __builtin_cpu_supports(char *).
/// This checks that the target supports __builtin_cpu_supports and
/// that the string argument is constant and valid.
static bool SemaBuiltinCpuSupports(Sema &S, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  // Check if the argument is a string literal.
  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  // Check the contents of the string.
  StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!S.Context.getTargetInfo().validateCpuSupports(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_supports)
           << Arg->getSourceRange();
  return false;
}

/// SemaBuiltinCpuIs - Handle __builtin_cpu_is(char *).
/// This checks that the target supports __builtin_cpu_is and
/// that the string argument is constant and valid.
static bool SemaBuiltinCpuIs(Sema &S, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  // Check if the argument is a string literal.
  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  // Check the contents of the string.
  StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!S.Context.getTargetInfo().validateCpuIs(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_is)
           << Arg->getSourceRange();
  return false;
}

// Check if the rounding mode is legal.
bool Sema::CheckX86BuiltinRoundingOrSAE(unsigned BuiltinID, CallExpr *TheCall) {
  // Indicates if this instruction has rounding control or just SAE.
  bool HasRC = false;

  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vcvttsd2si32:
  case X86::BI__builtin_ia32_vcvttsd2si64:
  case X86::BI__builtin_ia32_vcvttsd2usi32:
  case X86::BI__builtin_ia32_vcvttsd2usi64:
  case X86::BI__builtin_ia32_vcvttss2si32:
  case X86::BI__builtin_ia32_vcvttss2si64:
  case X86::BI__builtin_ia32_vcvttss2usi32:
  case X86::BI__builtin_ia32_vcvttss2usi64:
    ArgNum = 1;
    break;
  case X86::BI__builtin_ia32_maxpd512:
  case X86::BI__builtin_ia32_maxps512:
  case X86::BI__builtin_ia32_minpd512:
  case X86::BI__builtin_ia32_minps512:
    ArgNum = 2;
    break;
  case X86::BI__builtin_ia32_cvtps2pd512_mask:
  case X86::BI__builtin_ia32_cvttpd2dq512_mask:
  case X86::BI__builtin_ia32_cvttpd2qq512_mask:
  case X86::BI__builtin_ia32_cvttpd2udq512_mask:
  case X86::BI__builtin_ia32_cvttpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvttps2dq512_mask:
  case X86::BI__builtin_ia32_cvttps2qq512_mask:
  case X86::BI__builtin_ia32_cvttps2udq512_mask:
  case X86::BI__builtin_ia32_cvttps2uqq512_mask:
  case X86::BI__builtin_ia32_exp2pd_mask:
  case X86::BI__builtin_ia32_exp2ps_mask:
  case X86::BI__builtin_ia32_getexppd512_mask:
  case X86::BI__builtin_ia32_getexpps512_mask:
  case X86::BI__builtin_ia32_rcp28pd_mask:
  case X86::BI__builtin_ia32_rcp28ps_mask:
  case X86::BI__builtin_ia32_rsqrt28pd_mask:
  case X86::BI__builtin_ia32_rsqrt28ps_mask:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_vcvtph2ps512_mask:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_cvtss2sd_round_mask:
  case X86::BI__builtin_ia32_getexpsd128_round_mask:
  case X86::BI__builtin_ia32_getexpss128_round_mask:
  case X86::BI__builtin_ia32_maxsd_round_mask:
  case X86::BI__builtin_ia32_maxss_round_mask:
  case X86::BI__builtin_ia32_minsd_round_mask:
  case X86::BI__builtin_ia32_minss_round_mask:
  case X86::BI__builtin_ia32_rcp28sd_round_mask:
  case X86::BI__builtin_ia32_rcp28ss_round_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rsqrt28sd_round_mask:
  case X86::BI__builtin_ia32_rsqrt28ss_round_mask:
    ArgNum = 4;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_rangesd128_round_mask:
  case X86::BI__builtin_ia32_rangess128_round_mask:
  case X86::BI__builtin_ia32_reducesd_mask:
  case X86::BI__builtin_ia32_reducess_mask:
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
    ArgNum = 5;
    break;
  case X86::BI__builtin_ia32_vcvtsd2si64:
  case X86::BI__builtin_ia32_vcvtsd2si32:
  case X86::BI__builtin_ia32_vcvtsd2usi32:
  case X86::BI__builtin_ia32_vcvtsd2usi64:
  case X86::BI__builtin_ia32_vcvtss2si32:
  case X86::BI__builtin_ia32_vcvtss2si64:
  case X86::BI__builtin_ia32_vcvtss2usi32:
  case X86::BI__builtin_ia32_vcvtss2usi64:
  case X86::BI__builtin_ia32_sqrtpd512:
  case X86::BI__builtin_ia32_sqrtps512:
    ArgNum = 1;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addpd512:
  case X86::BI__builtin_ia32_addps512:
  case X86::BI__builtin_ia32_divpd512:
  case X86::BI__builtin_ia32_divps512:
  case X86::BI__builtin_ia32_mulpd512:
  case X86::BI__builtin_ia32_mulps512:
  case X86::BI__builtin_ia32_subpd512:
  case X86::BI__builtin_ia32_subps512:
  case X86::BI__builtin_ia32_cvtsi2sd64:
  case X86::BI__builtin_ia32_cvtsi2ss32:
  case X86::BI__builtin_ia32_cvtsi2ss64:
  case X86::BI__builtin_ia32_cvtusi2sd64:
  case X86::BI__builtin_ia32_cvtusi2ss32:
  case X86::BI__builtin_ia32_cvtusi2ss64:
    ArgNum = 2;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_cvtdq2ps512_mask:
  case X86::BI__builtin_ia32_cvtudq2ps512_mask:
  case X86::BI__builtin_ia32_cvtpd2ps512_mask:
  case X86::BI__builtin_ia32_cvtpd2qq512_mask:
  case X86::BI__builtin_ia32_cvtpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvtps2qq512_mask:
  case X86::BI__builtin_ia32_cvtps2uqq512_mask:
  case X86::BI__builtin_ia32_cvtqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtuqq2ps512_mask:
    ArgNum = 3;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addss_round_mask:
  case X86::BI__builtin_ia32_addsd_round_mask:
  case X86::BI__builtin_ia32_divss_round_mask:
  case X86::BI__builtin_ia32_divsd_round_mask:
  case X86::BI__builtin_ia32_mulss_round_mask:
  case X86::BI__builtin_ia32_mulsd_round_mask:
  case X86::BI__builtin_ia32_subss_round_mask:
  case X86::BI__builtin_ia32_subsd_round_mask:
  case X86::BI__builtin_ia32_scalefpd512_mask:
  case X86::BI__builtin_ia32_scalefps512_mask:
  case X86::BI__builtin_ia32_scalefsd_round_mask:
  case X86::BI__builtin_ia32_scalefss_round_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_cvtsd2ss_round_mask:
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
    ArgNum = 4;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
    ArgNum = 5;
    HasRC = true;
    break;
  }

  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Make sure rounding mode is either ROUND_CUR_DIRECTION or ROUND_NO_EXC bit
  // is set. If the intrinsic has rounding control(bits 1:0), make sure its only
  // combined with ROUND_NO_EXC.
  if (Result == 4/*ROUND_CUR_DIRECTION*/ ||
      Result == 8/*ROUND_NO_EXC*/ ||
      (HasRC && Result.getZExtValue() >= 8 && Result.getZExtValue() <= 11))
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_rounding)
         << Arg->getSourceRange();
}

// Check if the gather/scatter scale is legal.
bool Sema::CheckX86BuiltinGatherScatterScale(unsigned BuiltinID,
                                             CallExpr *TheCall) {
  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_gatherd_pd:
  case X86::BI__builtin_ia32_gatherd_pd256:
  case X86::BI__builtin_ia32_gatherq_pd:
  case X86::BI__builtin_ia32_gatherq_pd256:
  case X86::BI__builtin_ia32_gatherd_ps:
  case X86::BI__builtin_ia32_gatherd_ps256:
  case X86::BI__builtin_ia32_gatherq_ps:
  case X86::BI__builtin_ia32_gatherq_ps256:
  case X86::BI__builtin_ia32_gatherd_q:
  case X86::BI__builtin_ia32_gatherd_q256:
  case X86::BI__builtin_ia32_gatherq_q:
  case X86::BI__builtin_ia32_gatherq_q256:
  case X86::BI__builtin_ia32_gatherd_d:
  case X86::BI__builtin_ia32_gatherd_d256:
  case X86::BI__builtin_ia32_gatherq_d:
  case X86::BI__builtin_ia32_gatherq_d256:
  case X86::BI__builtin_ia32_gather3div2df:
  case X86::BI__builtin_ia32_gather3div2di:
  case X86::BI__builtin_ia32_gather3div4df:
  case X86::BI__builtin_ia32_gather3div4di:
  case X86::BI__builtin_ia32_gather3div4sf:
  case X86::BI__builtin_ia32_gather3div4si:
  case X86::BI__builtin_ia32_gather3div8sf:
  case X86::BI__builtin_ia32_gather3div8si:
  case X86::BI__builtin_ia32_gather3siv2df:
  case X86::BI__builtin_ia32_gather3siv2di:
  case X86::BI__builtin_ia32_gather3siv4df:
  case X86::BI__builtin_ia32_gather3siv4di:
  case X86::BI__builtin_ia32_gather3siv4sf:
  case X86::BI__builtin_ia32_gather3siv4si:
  case X86::BI__builtin_ia32_gather3siv8sf:
  case X86::BI__builtin_ia32_gather3siv8si:
  case X86::BI__builtin_ia32_gathersiv8df:
  case X86::BI__builtin_ia32_gathersiv16sf:
  case X86::BI__builtin_ia32_gatherdiv8df:
  case X86::BI__builtin_ia32_gatherdiv16sf:
  case X86::BI__builtin_ia32_gathersiv8di:
  case X86::BI__builtin_ia32_gathersiv16si:
  case X86::BI__builtin_ia32_gatherdiv8di:
  case X86::BI__builtin_ia32_gatherdiv16si:
  case X86::BI__builtin_ia32_scatterdiv2df:
  case X86::BI__builtin_ia32_scatterdiv2di:
  case X86::BI__builtin_ia32_scatterdiv4df:
  case X86::BI__builtin_ia32_scatterdiv4di:
  case X86::BI__builtin_ia32_scatterdiv4sf:
  case X86::BI__builtin_ia32_scatterdiv4si:
  case X86::BI__builtin_ia32_scatterdiv8sf:
  case X86::BI__builtin_ia32_scatterdiv8si:
  case X86::BI__builtin_ia32_scattersiv2df:
  case X86::BI__builtin_ia32_scattersiv2di:
  case X86::BI__builtin_ia32_scattersiv4df:
  case X86::BI__builtin_ia32_scattersiv4di:
  case X86::BI__builtin_ia32_scattersiv4sf:
  case X86::BI__builtin_ia32_scattersiv4si:
  case X86::BI__builtin_ia32_scattersiv8sf:
  case X86::BI__builtin_ia32_scattersiv8si:
  case X86::BI__builtin_ia32_scattersiv8df:
  case X86::BI__builtin_ia32_scattersiv16sf:
  case X86::BI__builtin_ia32_scatterdiv8df:
  case X86::BI__builtin_ia32_scatterdiv16sf:
  case X86::BI__builtin_ia32_scattersiv8di:
  case X86::BI__builtin_ia32_scattersiv16si:
  case X86::BI__builtin_ia32_scatterdiv8di:
  case X86::BI__builtin_ia32_scatterdiv16si:
    ArgNum = 4;
    break;
  }

  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result == 1 || Result == 2 || Result == 4 || Result == 8)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_scale)
         << Arg->getSourceRange();
}

static bool isX86_32Builtin(unsigned BuiltinID) {
  // These builtins only work on x86-32 targets.
  switch (BuiltinID) {
  case X86::BI__builtin_ia32_readeflags_u32:
  case X86::BI__builtin_ia32_writeeflags_u32:
    return true;
  }

  return false;
}

bool Sema::CheckX86BuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (BuiltinID == X86::BI__builtin_cpu_supports)
    return SemaBuiltinCpuSupports(*this, TheCall);

  if (BuiltinID == X86::BI__builtin_cpu_is)
    return SemaBuiltinCpuIs(*this, TheCall);

  // Check for 32-bit only builtins on a 64-bit target.
  const llvm::Triple &TT = Context.getTargetInfo().getTriple();
  if (TT.getArch() != llvm::Triple::x86 && isX86_32Builtin(BuiltinID))
    return Diag(TheCall->getCallee()->getBeginLoc(),
                diag::err_32_bit_builtin_64_bit_tgt);

  // If the intrinsic has rounding or SAE make sure its valid.
  if (CheckX86BuiltinRoundingOrSAE(BuiltinID, TheCall))
    return true;

  // If the intrinsic has a gather/scatter scale immediate make sure its valid.
  if (CheckX86BuiltinGatherScatterScale(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  int i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vec_ext_v2si:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
    i = 1; l = 0; u = 1;
    break;
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
    i = 2; l = 0; u = 1;
    break;
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vec_ext_v4hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v4di:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask:
    i = 1; l = 0; u = 3;
    break;
  case X86::BI_mm_prefetch:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
    i = 1; l = 0; u = 7;
    break;
  case X86::BI__builtin_ia32_sha1rnds4:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_vec_set_v4hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v4di:
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti32x4:
    i = 2; l = 0; u = 3;
    break;
  case X86::BI__builtin_ia32_vpermil2pd:
  case X86::BI__builtin_ia32_vpermil2pd256:
  case X86::BI__builtin_ia32_vpermil2ps:
  case X86::BI__builtin_ia32_vpermil2ps256:
    i = 3; l = 0; u = 3;
    break;
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq512_mask:
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask:
  case X86::BI__builtin_ia32_vpcomub:
  case X86::BI__builtin_ia32_vpcomuw:
  case X86::BI__builtin_ia32_vpcomud:
  case X86::BI__builtin_ia32_vpcomuq:
  case X86::BI__builtin_ia32_vpcomb:
  case X86::BI__builtin_ia32_vpcomw:
  case X86::BI__builtin_ia32_vpcomd:
  case X86::BI__builtin_ia32_vpcomq:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
    i = 2; l = 0; u = 7;
    break;
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_roundps:
  case X86::BI__builtin_ia32_roundpd:
  case X86::BI__builtin_ia32_roundps256:
  case X86::BI__builtin_ia32_roundpd256:
  case X86::BI__builtin_ia32_getmantpd128_mask:
  case X86::BI__builtin_ia32_getmantpd256_mask:
  case X86::BI__builtin_ia32_getmantps128_mask:
  case X86::BI__builtin_ia32_getmantps256_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
    i = 1; l = 0; u = 15;
    break;
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_roundss:
  case X86::BI__builtin_ia32_roundsd:
  case X86::BI__builtin_ia32_rangepd128_mask:
  case X86::BI__builtin_ia32_rangepd256_mask:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps128_mask:
  case X86::BI__builtin_ia32_rangeps256_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
    i = 2; l = 0; u = 15;
    break;
  case X86::BI__builtin_ia32_vec_ext_v32qi:
    i = 1; l = 0; u = 31;
    break;
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpss:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmpsd:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd256:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_vec_set_v32qi:
    i = 2; l = 0; u = 31;
    break;
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf512:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512:
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512:
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512:
  case X86::BI__builtin_ia32_vcvtps2ph:
  case X86::BI__builtin_ia32_vcvtps2ph_mask:
  case X86::BI__builtin_ia32_vcvtps2ph256:
  case X86::BI__builtin_ia32_vcvtps2ph256_mask:
  case X86::BI__builtin_ia32_vcvtps2ph512_mask:
  case X86::BI__builtin_ia32_rndscaleps_128_mask:
  case X86::BI__builtin_ia32_rndscalepd_128_mask:
  case X86::BI__builtin_ia32_rndscaleps_256_mask:
  case X86::BI__builtin_ia32_rndscalepd_256_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_reducepd128_mask:
  case X86::BI__builtin_ia32_reducepd256_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps128_mask:
  case X86::BI__builtin_ia32_reduceps256_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq512:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq512:
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask:
  case X86::BI__builtin_ia32_fpclasssd_mask:
  case X86::BI__builtin_ia32_fpclassss_mask:
  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift:
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift:
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi:
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi:
    i = 1; l = 0; u = 255;
    break;
  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256:
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendd256:
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512:
  case X86::BI__builtin_ia32_alignq512:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512:
  case X86::BI__builtin_ia32_dbpsadbw128:
  case X86::BI__builtin_ia32_dbpsadbw256:
  case X86::BI__builtin_ia32_dbpsadbw512:
  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    i = 2; l = 0; u = 255;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_fixupimmpd128_mask:
  case X86::BI__builtin_ia32_fixupimmpd128_maskz:
  case X86::BI__builtin_ia32_fixupimmpd256_mask:
  case X86::BI__builtin_ia32_fixupimmpd256_maskz:
  case X86::BI__builtin_ia32_fixupimmps128_mask:
  case X86::BI__builtin_ia32_fixupimmps128_maskz:
  case X86::BI__builtin_ia32_fixupimmps256_mask:
  case X86::BI__builtin_ia32_fixupimmps256_maskz:
  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_mask:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
    i = 3; l = 0; u = 255;
    break;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    i = 4; l = 2; u = 3;
    break;
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
    i = 4; l = 0; u = 255;
    break;
  }

  // Note that we don't force a hard error on the range check here, allowing
  // template-generated or macro-generated dead code to potentially have out-of-
  // range values. These need to code generate, but don't need to necessarily
  // make any sense. We use a warning that defaults to an error.
  return SemaBuiltinConstantArgRange(TheCall, i, l, u, /*RangeIsError*/ false);
}

/// Given a FunctionDecl's FormatAttr, attempts to populate the FomatStringInfo
/// parameter with the FormatAttr's correct format_idx and firstDataArg.
/// Returns true when the format fits the function and the FormatStringInfo has
/// been populated.
bool Sema::getFormatStringInfo(const FormatAttr *Format, bool IsCXXMember,
                               FormatStringInfo *FSI) {
  FSI->HasVAListArg = Format->getFirstArg() == 0;
  FSI->FormatIdx = Format->getFormatIdx() - 1;
  FSI->FirstDataArg = FSI->HasVAListArg ? 0 : Format->getFirstArg() - 1;

  // The way the format attribute works in GCC, the implicit this argument
  // of member functions is counted. However, it doesn't appear in our own
  // lists, so decrement format_idx in that case.
  if (IsCXXMember) {
    if(FSI->FormatIdx == 0)
      return false;
    --FSI->FormatIdx;
    if (FSI->FirstDataArg != 0)
      --FSI->FirstDataArg;
  }
  return true;
}

/// Checks if a the given expression evaluates to null.
///
/// Returns true if the value evaluates to null.
static bool CheckNonNullExpr(Sema &S, const Expr *Expr) {
  // If the expression has non-null type, it doesn't evaluate to null.
  if (auto nullability
        = Expr->IgnoreImplicit()->getType()->getNullability(S.Context)) {
    if (*nullability == NullabilityKind::NonNull)
      return false;
  }

  // As a special case, transparent unions initialized with zero are
  // considered null for the purposes of the nonnull attribute.
  if (const RecordType *UT = Expr->getType()->getAsUnionType()) {
    if (UT->getDecl()->hasAttr<TransparentUnionAttr>())
      if (const CompoundLiteralExpr *CLE =
          dyn_cast<CompoundLiteralExpr>(Expr))
        if (const InitListExpr *ILE =
            dyn_cast<InitListExpr>(CLE->getInitializer()))
          Expr = ILE->getInit(0);
  }

  bool Result;
  return (!Expr->isValueDependent() &&
          Expr->EvaluateAsBooleanCondition(Result, S.Context) &&
          !Result);
}

static void CheckNonNullArgument(Sema &S,
                                 const Expr *ArgExpr,
                                 SourceLocation CallSiteLoc) {
  if (CheckNonNullExpr(S, ArgExpr))
    S.DiagRuntimeBehavior(CallSiteLoc, ArgExpr,
           S.PDiag(diag::warn_null_arg) << ArgExpr->getSourceRange());
}

bool Sema::GetFormatNSStringIdx(const FormatAttr *Format, unsigned &Idx) {
  FormatStringInfo FSI;
  if ((GetFormatStringType(Format) == FST_NSString) &&
      getFormatStringInfo(Format, false, &FSI)) {
    Idx = FSI.FormatIdx;
    return true;
  }
  return false;
}

/// Diagnose use of %s directive in an NSString which is being passed
/// as formatting string to formatting method.
static void
DiagnoseCStringFormatDirectiveInCFAPI(Sema &S,
                                        const NamedDecl *FDecl,
                                        Expr **Args,
                                        unsigned NumArgs) {
  unsigned Idx = 0;
  bool Format = false;
  ObjCStringFormatFamily SFFamily = FDecl->getObjCFStringFormattingFamily();
  if (SFFamily == ObjCStringFormatFamily::SFF_CFString) {
    Idx = 2;
    Format = true;
  }
  else
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      if (S.GetFormatNSStringIdx(I, Idx)) {
        Format = true;
        break;
      }
    }
  if (!Format || NumArgs <= Idx)
    return;
  const Expr *FormatExpr = Args[Idx];
  if (const CStyleCastExpr *CSCE = dyn_cast<CStyleCastExpr>(FormatExpr))
    FormatExpr = CSCE->getSubExpr();
  const StringLiteral *FormatString;
  if (const ObjCStringLiteral *OSL =
      dyn_cast<ObjCStringLiteral>(FormatExpr->IgnoreParenImpCasts()))
    FormatString = OSL->getString();
  else
    FormatString = dyn_cast<StringLiteral>(FormatExpr->IgnoreParenImpCasts());
  if (!FormatString)
    return;
  if (S.FormatStringHasSArg(FormatString)) {
    S.Diag(FormatExpr->getExprLoc(), diag::warn_objc_cdirective_format_string)
      << "%s" << 1 << 1;
    S.Diag(FDecl->getLocation(), diag::note_entity_declared_at)
      << FDecl->getDeclName();
  }
}

/// Determine whether the given type has a non-null nullability annotation.
static bool isNonNullType(ASTContext &ctx, QualType type) {
  if (auto nullability = type->getNullability(ctx))
    return *nullability == NullabilityKind::NonNull;

  return false;
}

static void CheckNonNullArguments(Sema &S,
                                  const NamedDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  ArrayRef<const Expr *> Args,
                                  SourceLocation CallSiteLoc) {
  assert((FDecl || Proto) && "Need a function declaration or prototype");

  // Check the attributes attached to the method/function itself.
  llvm::SmallBitVector NonNullArgs;
  if (FDecl) {
    // Handle the nonnull attribute on the function/method declaration itself.
    for (const auto *NonNull : FDecl->specific_attrs<NonNullAttr>()) {
      if (!NonNull->args_size()) {
        // Easy case: all pointer arguments are nonnull.
        for (const auto *Arg : Args)
          if (S.isValidPointerAttrType(Arg->getType()))
            CheckNonNullArgument(S, Arg, CallSiteLoc);
        return;
      }

      for (const ParamIdx &Idx : NonNull->args()) {
        unsigned IdxAST = Idx.getASTIndex();
        if (IdxAST >= Args.size())
          continue;
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());
        NonNullArgs.set(IdxAST);
      }
    }
  }

  if (FDecl && (isa<FunctionDecl>(FDecl) || isa<ObjCMethodDecl>(FDecl))) {
    // Handle the nonnull attribute on the parameters of the
    // function/method.
    ArrayRef<ParmVarDecl*> parms;
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(FDecl))
      parms = FD->parameters();
    else
      parms = cast<ObjCMethodDecl>(FDecl)->parameters();

    unsigned ParamIndex = 0;
    for (ArrayRef<ParmVarDecl*>::iterator I = parms.begin(), E = parms.end();
         I != E; ++I, ++ParamIndex) {
      const ParmVarDecl *PVD = *I;
      if (PVD->hasAttr<NonNullAttr>() ||
          isNonNullType(S.Context, PVD->getType())) {
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());

        NonNullArgs.set(ParamIndex);
      }
    }
  } else {
    // If we have a non-function, non-method declaration but no
    // function prototype, try to dig out the function prototype.
    if (!Proto) {
      if (const ValueDecl *VD = dyn_cast<ValueDecl>(FDecl)) {
        QualType type = VD->getType().getNonReferenceType();
        if (auto pointerType = type->getAs<PointerType>())
          type = pointerType->getPointeeType();
        else if (auto blockType = type->getAs<BlockPointerType>())
          type = blockType->getPointeeType();
        // FIXME: data member pointers?

        // Dig out the function prototype, if there is one.
        Proto = type->getAs<FunctionProtoType>();
      }
    }

    // Fill in non-null argument information from the nullability
    // information on the parameter types (if we have them).
    if (Proto) {
      unsigned Index = 0;
      for (auto paramType : Proto->getParamTypes()) {
        if (isNonNullType(S.Context, paramType)) {
          if (NonNullArgs.empty())
            NonNullArgs.resize(Args.size());

          NonNullArgs.set(Index);
        }

        ++Index;
      }
    }
  }

  // Check for non-null arguments.
  for (unsigned ArgIndex = 0, ArgIndexEnd = NonNullArgs.size();
       ArgIndex != ArgIndexEnd; ++ArgIndex) {
    if (NonNullArgs[ArgIndex])
      CheckNonNullArgument(S, Args[ArgIndex], CallSiteLoc);
  }
}

/// Handles the checks for format strings, non-POD arguments to vararg
/// functions, NULL arguments passed to non-NULL parameters, and diagnose_if
/// attributes.
void Sema::checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                     const Expr *ThisArg, ArrayRef<const Expr *> Args,
                     bool IsMemberFunction, SourceLocation Loc,
                     SourceRange Range, VariadicCallType CallType) {
  // FIXME: We should check as much as we can in the template definition.
  if (CurContext->isDependentContext())
    return;

  // Printf and scanf checking.
  llvm::SmallBitVector CheckedVarArgs;
  if (FDecl) {
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      // Only create vector if there are format attributes.
      CheckedVarArgs.resize(Args.size());

      CheckFormatArguments(I, Args, IsMemberFunction, CallType, Loc, Range,
                           CheckedVarArgs);
    }
  }

  // Refuse POD arguments that weren't caught by the format string
  // checks above.
  auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl);
  if (CallType != VariadicDoesNotApply &&
      (!FD || FD->getBuiltinID() != Builtin::BI__noop)) {
    unsigned NumParams = Proto ? Proto->getNumParams()
                       : FDecl && isa<FunctionDecl>(FDecl)
                           ? cast<FunctionDecl>(FDecl)->getNumParams()
                       : FDecl && isa<ObjCMethodDecl>(FDecl)
                           ? cast<ObjCMethodDecl>(FDecl)->param_size()
                       : 0;

    for (unsigned ArgIdx = NumParams; ArgIdx < Args.size(); ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (CheckedVarArgs.empty() || !CheckedVarArgs[ArgIdx])
          checkVariadicArgument(Arg, CallType);
      }
    }
  }

  if (FDecl || Proto) {
    CheckNonNullArguments(*this, FDecl, Proto, Args, Loc);

    // Type safety checking.
    if (FDecl) {
      for (const auto *I : FDecl->specific_attrs<ArgumentWithTypeTagAttr>())
        CheckArgumentWithTypeTag(I, Args, Loc);
    }
  }

  if (FD)
    diagnoseArgDependentDiagnoseIfAttrs(FD, ThisArg, Args, Loc);
}

/// CheckConstructorCall - Check a constructor call for correctness and safety
/// properties not enforced by the C type system.
void Sema::CheckConstructorCall(FunctionDecl *FDecl,
                                ArrayRef<const Expr *> Args,
                                const FunctionProtoType *Proto,
                                SourceLocation Loc) {
  VariadicCallType CallType =
    Proto->isVariadic() ? VariadicConstructor : VariadicDoesNotApply;
  checkCall(FDecl, Proto, /*ThisArg=*/nullptr, Args, /*IsMemberFunction=*/true,
            Loc, SourceRange(), CallType);
}

/// CheckFunctionCall - Check a direct function call for various correctness
/// and safety properties not strictly enforced by the C type system.
bool Sema::CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                             const FunctionProtoType *Proto) {
  bool IsMemberOperatorCall = isa<CXXOperatorCallExpr>(TheCall) &&
                              isa<CXXMethodDecl>(FDecl);
  bool IsMemberFunction = isa<CXXMemberCallExpr>(TheCall) ||
                          IsMemberOperatorCall;
  VariadicCallType CallType = getVariadicCallType(FDecl, Proto,
                                                  TheCall->getCallee());
  Expr** Args = TheCall->getArgs();
  unsigned NumArgs = TheCall->getNumArgs();

  Expr *ImplicitThis = nullptr;
  if (IsMemberOperatorCall) {
    // If this is a call to a member operator, hide the first argument
    // from checkCall.
    // FIXME: Our choice of AST representation here is less than ideal.
    ImplicitThis = Args[0];
    ++Args;
    --NumArgs;
  } else if (IsMemberFunction)
    ImplicitThis =
        cast<CXXMemberCallExpr>(TheCall)->getImplicitObjectArgument();

  checkCall(FDecl, Proto, ImplicitThis, llvm::makeArrayRef(Args, NumArgs),
            IsMemberFunction, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  IdentifierInfo *FnInfo = FDecl->getIdentifier();
  // None of the checks below are needed for functions that don't have
  // simple names (e.g., C++ conversion functions).
  if (!FnInfo)
    return false;

  CheckAbsoluteValueFunction(TheCall, FDecl);
  CheckMaxUnsignedZero(TheCall, FDecl);

  if (getLangOpts().ObjC)
    DiagnoseCStringFormatDirectiveInCFAPI(*this, FDecl, Args, NumArgs);

  unsigned CMId = FDecl->getMemoryFunctionKind();
  if (CMId == 0)
    return false;

  // Handle memory setting and copying functions.
  if (CMId == Builtin::BIstrlcpy || CMId == Builtin::BIstrlcat)
    CheckStrlcpycatArguments(TheCall, FnInfo);
  else if (CMId == Builtin::BIstrncat)
    CheckStrncatArguments(TheCall, FnInfo);
  else
    CheckMemaccessArguments(TheCall, CMId, FnInfo);

  return false;
}

bool Sema::CheckObjCMethodCall(ObjCMethodDecl *Method, SourceLocation lbrac,
                               ArrayRef<const Expr *> Args) {
  VariadicCallType CallType =
      Method->isVariadic() ? VariadicMethod : VariadicDoesNotApply;

  checkCall(Method, nullptr, /*ThisArg=*/nullptr, Args,
            /*IsMemberFunction=*/false, lbrac, Method->getSourceRange(),
            CallType);

  return false;
}

bool Sema::CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                            const FunctionProtoType *Proto) {
  QualType Ty;
  if (const auto *V = dyn_cast<VarDecl>(NDecl))
    Ty = V->getType().getNonReferenceType();
  else if (const auto *F = dyn_cast<FieldDecl>(NDecl))
    Ty = F->getType().getNonReferenceType();
  else
    return false;

  if (!Ty->isBlockPointerType() && !Ty->isFunctionPointerType() &&
      !Ty->isFunctionProtoType())
    return false;

  VariadicCallType CallType;
  if (!Proto || !Proto->isVariadic()) {
    CallType = VariadicDoesNotApply;
  } else if (Ty->isBlockPointerType()) {
    CallType = VariadicBlock;
  } else { // Ty->isFunctionPointerType()
    CallType = VariadicFunction;
  }

  checkCall(NDecl, Proto, /*ThisArg=*/nullptr,
            llvm::makeArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            /*IsMemberFunction=*/false, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  return false;
}

/// Checks function calls when a FunctionDecl or a NamedDecl is not available,
/// such as function pointers returned from functions.
bool Sema::CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto) {
  VariadicCallType CallType = getVariadicCallType(/*FDecl=*/nullptr, Proto,
                                                  TheCall->getCallee());
  checkCall(/*FDecl=*/nullptr, Proto, /*ThisArg=*/nullptr,
            llvm::makeArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            /*IsMemberFunction=*/false, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  return false;
}

static bool isValidOrderingForOp(int64_t Ordering, AtomicExpr::AtomicOp Op) {
  if (!llvm::isValidAtomicOrderingCABI(Ordering))
    return false;

  auto OrderingCABI = (llvm::AtomicOrderingCABI)Ordering;
  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    llvm_unreachable("There is no ordering argument for an init");

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__atomic_load:
    return OrderingCABI != llvm::AtomicOrderingCABI::release &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
    return OrderingCABI != llvm::AtomicOrderingCABI::consume &&
           OrderingCABI != llvm::AtomicOrderingCABI::acquire &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  default:
    return true;
  }
}

ExprResult Sema::SemaAtomicOpsOverloaded(ExprResult TheCallResult,
                                         AtomicExpr::AtomicOp Op) {
  CallExpr *TheCall = cast<CallExpr>(TheCallResult.get());
  DeclRefExpr *DRE =cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());

  // All the non-OpenCL operations take one of the following forms.
  // The OpenCL operations take the __c11 forms with one extra argument for
  // synchronization scope.
  enum {
    // C    __c11_atomic_init(A *, C)
    Init,

    // C    __c11_atomic_load(A *, int)
    Load,

    // void __atomic_load(A *, CP, int)
    LoadCopy,

    // void __atomic_store(A *, CP, int)
    Copy,

    // C    __c11_atomic_add(A *, M, int)
    Arithmetic,

    // C    __atomic_exchange_n(A *, CP, int)
    Xchg,

    // void __atomic_exchange(A *, C *, CP, int)
    GNUXchg,

    // bool __c11_atomic_compare_exchange_strong(A *, C *, CP, int, int)
    C11CmpXchg,

    // bool __atomic_compare_exchange(A *, C *, CP, bool, int, int)
    GNUCmpXchg
  } Form = Init;

  const unsigned NumForm = GNUCmpXchg + 1;
  const unsigned NumArgs[] = { 2, 2, 3, 3, 3, 3, 4, 5, 6 };
  const unsigned NumVals[] = { 1, 0, 1, 1, 1, 1, 2, 2, 3 };
  // where:
  //   C is an appropriate type,
  //   A is volatile _Atomic(C) for __c11 builtins and is C for GNU builtins,
  //   CP is C for __c11 builtins and GNU _n builtins and is C * otherwise,
  //   M is C if C is an integer, and ptrdiff_t if C is a pointer, and
  //   the int parameters are for orderings.

  static_assert(sizeof(NumArgs)/sizeof(NumArgs[0]) == NumForm
      && sizeof(NumVals)/sizeof(NumVals[0]) == NumForm,
      "need to update code for modified forms");
  static_assert(AtomicExpr::AO__c11_atomic_init == 0 &&
                    AtomicExpr::AO__c11_atomic_fetch_xor + 1 ==
                        AtomicExpr::AO__atomic_load,
                "need to update code for modified C11 atomics");
  bool IsOpenCL = Op >= AtomicExpr::AO__opencl_atomic_init &&
                  Op <= AtomicExpr::AO__opencl_atomic_fetch_max;
  bool IsC11 = (Op >= AtomicExpr::AO__c11_atomic_init &&
               Op <= AtomicExpr::AO__c11_atomic_fetch_xor) ||
               IsOpenCL;
  bool IsN = Op == AtomicExpr::AO__atomic_load_n ||
             Op == AtomicExpr::AO__atomic_store_n ||
             Op == AtomicExpr::AO__atomic_exchange_n ||
             Op == AtomicExpr::AO__atomic_compare_exchange_n;
  bool IsAddSub = false;
  bool IsMinMax = false;

  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    Form = Init;
    break;

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
    Form = Load;
    break;

  case AtomicExpr::AO__atomic_load:
    Form = LoadCopy;
    break;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
    Form = Copy;
    break;

  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__c11_atomic_fetch_sub:
  case AtomicExpr::AO__opencl_atomic_fetch_add:
  case AtomicExpr::AO__opencl_atomic_fetch_sub:
  case AtomicExpr::AO__opencl_atomic_fetch_min:
  case AtomicExpr::AO__opencl_atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__atomic_sub_fetch:
    IsAddSub = true;
    LLVM_FALLTHROUGH;
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__opencl_atomic_fetch_and:
  case AtomicExpr::AO__opencl_atomic_fetch_or:
  case AtomicExpr::AO__opencl_atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__atomic_nand_fetch:
    Form = Arithmetic;
    break;

  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__atomic_fetch_max:
    IsMinMax = true;
    Form = Arithmetic;
    break;

  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__opencl_atomic_exchange:
  case AtomicExpr::AO__atomic_exchange_n:
    Form = Xchg;
    break;

  case AtomicExpr::AO__atomic_exchange:
    Form = GNUXchg;
    break;

  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_strong:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_weak:
    Form = C11CmpXchg;
    break;

  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
    Form = GNUCmpXchg;
    break;
  }

  unsigned AdjustedNumArgs = NumArgs[Form];
  if (IsOpenCL && Op != AtomicExpr::AO__opencl_atomic_init)
    ++AdjustedNumArgs;
  // Check we have the right number of arguments.
  if (TheCall->getNumArgs() < AdjustedNumArgs) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
        << 0 << AdjustedNumArgs << TheCall->getNumArgs()
        << TheCall->getCallee()->getSourceRange();
    return ExprError();
  } else if (TheCall->getNumArgs() > AdjustedNumArgs) {
    Diag(TheCall->getArg(AdjustedNumArgs)->getBeginLoc(),
         diag::err_typecheck_call_too_many_args)
        << 0 << AdjustedNumArgs << TheCall->getNumArgs()
        << TheCall->getCallee()->getSourceRange();
    return ExprError();
  }

  // Inspect the first argument of the atomic operation.
  Expr *Ptr = TheCall->getArg(0);
  ExprResult ConvertedPtr = DefaultFunctionArrayLvalueConversion(Ptr);
  if (ConvertedPtr.isInvalid())
    return ExprError();

  Ptr = ConvertedPtr.get();
  const PointerType *pointerType = Ptr->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  // For a __c11 builtin, this should be a pointer to an _Atomic type.
  QualType AtomTy = pointerType->getPointeeType(); // 'A'
  QualType ValType = AtomTy; // 'C'
  if (IsC11) {
    if (!AtomTy->isAtomicType()) {
      Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_atomic)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if ((Form != Load && Form != LoadCopy && AtomTy.isConstQualified()) ||
        AtomTy.getAddressSpace() == LangAS::opencl_constant) {
      Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_non_const_atomic)
          << (AtomTy.isConstQualified() ? 0 : 1) << Ptr->getType()
          << Ptr->getSourceRange();
      return ExprError();
    }
    ValType = AtomTy->getAs<AtomicType>()->getValueType();
  } else if (Form != Load && Form != LoadCopy) {
    if (ValType.isConstQualified()) {
      Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_non_const_pointer)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
  }

  // For an arithmetic operation, the implied arithmetic must be well-formed.
  if (Form == Arithmetic) {
    // gcc does not enforce these rules for GNU atomics, but we do so for sanity.
    if (IsAddSub && !ValType->isIntegerType()
        && !ValType->isPointerType()) {
      Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_atomic_int_or_ptr)
          << IsC11 << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if (IsMinMax) {
      const BuiltinType *BT = ValType->getAs<BuiltinType>();
      if (!BT || (BT->getKind() != BuiltinType::Int &&
                  BT->getKind() != BuiltinType::UInt)) {
        Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_int32_or_ptr);
        return ExprError();
      }
    }
    if (!IsAddSub && !IsMinMax && !ValType->isIntegerType()) {
      Diag(DRE->getBeginLoc(), diag::err_atomic_op_bitwise_needs_atomic_int)
          << IsC11 << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if (IsC11 && ValType->isPointerType() &&
        RequireCompleteType(Ptr->getBeginLoc(), ValType->getPointeeType(),
                            diag::err_incomplete_type)) {
      return ExprError();
    }
  } else if (IsN && !ValType->isIntegerType() && !ValType->isPointerType()) {
    // For __atomic_*_n operations, the value type must be a scalar integral or
    // pointer type which is 1, 2, 4, 8 or 16 bytes in length.
    Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_atomic_int_or_ptr)
        << IsC11 << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  if (!IsC11 && !AtomTy.isTriviallyCopyableType(Context) &&
      !AtomTy->isScalarType()) {
    // For GNU atomics, require a trivially-copyable type. This is not part of
    // the GNU atomics specification, but we enforce it for sanity.
    Diag(DRE->getBeginLoc(), diag::err_atomic_op_needs_trivial_copy)
        << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    // FIXME: Can this happen? By this point, ValType should be known
    // to be trivially copyable.
    Diag(DRE->getBeginLoc(), diag::err_arc_atomic_ownership)
        << ValType << Ptr->getSourceRange();
    return ExprError();
  }

  // All atomic operations have an overload which takes a pointer to a volatile
  // 'A'.  We shouldn't let the volatile-ness of the pointee-type inject itself
  // into the result or the other operands. Similarly atomic_load takes a
  // pointer to a const 'A'.
  ValType.removeLocalVolatile();
  ValType.removeLocalConst();
  QualType ResultType = ValType;
  if (Form == Copy || Form == LoadCopy || Form == GNUXchg ||
      Form == Init)
    ResultType = Context.VoidTy;
  else if (Form == C11CmpXchg || Form == GNUCmpXchg)
    ResultType = Context.BoolTy;

  // The type of a parameter passed 'by value'. In the GNU atomics, such
  // arguments are actually passed as pointers.
  QualType ByValType = ValType; // 'CP'
  bool IsPassedByAddress = false;
  if (!IsC11 && !IsN) {
    ByValType = Ptr->getType();
    IsPassedByAddress = true;
  }

  // The first argument's non-CV pointer type is used to deduce the type of
  // subsequent arguments, except for:
  //  - weak flag (always converted to bool)
  //  - memory order (always converted to int)
  //  - scope  (always converted to int)
  for (unsigned i = 0; i != TheCall->getNumArgs(); ++i) {
    QualType Ty;
    if (i < NumVals[Form] + 1) {
      switch (i) {
      case 0:
        // The first argument is always a pointer. It has a fixed type.
        // It is always dereferenced, a nullptr is undefined.
        CheckNonNullArgument(*this, TheCall->getArg(i), DRE->getBeginLoc());
        // Nothing else to do: we already know all we want about this pointer.
        continue;
      case 1:
        // The second argument is the non-atomic operand. For arithmetic, this
        // is always passed by value, and for a compare_exchange it is always
        // passed by address. For the rest, GNU uses by-address and C11 uses
        // by-value.
        assert(Form != Load);
        if (Form == Init || (Form == Arithmetic && ValType->isIntegerType()))
          Ty = ValType;
        else if (Form == Copy || Form == Xchg) {
          if (IsPassedByAddress)
            // The value pointer is always dereferenced, a nullptr is undefined.
            CheckNonNullArgument(*this, TheCall->getArg(i), DRE->getBeginLoc());
          Ty = ByValType;
        } else if (Form == Arithmetic)
          Ty = Context.getPointerDiffType();
        else {
          Expr *ValArg = TheCall->getArg(i);
          // The value pointer is always dereferenced, a nullptr is undefined.
          CheckNonNullArgument(*this, ValArg, DRE->getBeginLoc());
          LangAS AS = LangAS::Default;
          // Keep address space of non-atomic pointer type.
          if (const PointerType *PtrTy =
                  ValArg->getType()->getAs<PointerType>()) {
            AS = PtrTy->getPointeeType().getAddressSpace();
          }
          Ty = Context.getPointerType(
              Context.getAddrSpaceQualType(ValType.getUnqualifiedType(), AS));
        }
        break;
      case 2:
        // The third argument to compare_exchange / GNU exchange is the desired
        // value, either by-value (for the C11 and *_n variant) or as a pointer.
        if (IsPassedByAddress)
          CheckNonNullArgument(*this, TheCall->getArg(i), DRE->getBeginLoc());
        Ty = ByValType;
        break;
      case 3:
        // The fourth argument to GNU compare_exchange is a 'weak' flag.
        Ty = Context.BoolTy;
        break;
      }
    } else {
      // The order(s) and scope are always converted to int.
      Ty = Context.IntTy;
    }

    InitializedEntity Entity =
        InitializedEntity::InitializeParameter(Context, Ty, false);
    ExprResult Arg = TheCall->getArg(i);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
  }

  // Permute the arguments into a 'consistent' order.
  SmallVector<Expr*, 5> SubExprs;
  SubExprs.push_back(Ptr);
  switch (Form) {
  case Init:
    // Note, AtomicExpr::getVal1() has a special case for this atomic.
    SubExprs.push_back(TheCall->getArg(1)); // Val1
    break;
  case Load:
    SubExprs.push_back(TheCall->getArg(1)); // Order
    break;
  case LoadCopy:
  case Copy:
  case Arithmetic:
  case Xchg:
    SubExprs.push_back(TheCall->getArg(2)); // Order
    SubExprs.push_back(TheCall->getArg(1)); // Val1
    break;
  case GNUXchg:
    // Note, AtomicExpr::getVal2() has a special case for this atomic.
    SubExprs.push_back(TheCall->getArg(3)); // Order
    SubExprs.push_back(TheCall->getArg(1)); // Val1
    SubExprs.push_back(TheCall->getArg(2)); // Val2
    break;
  case C11CmpXchg:
    SubExprs.push_back(TheCall->getArg(3)); // Order
    SubExprs.push_back(TheCall->getArg(1)); // Val1
    SubExprs.push_back(TheCall->getArg(4)); // OrderFail
    SubExprs.push_back(TheCall->getArg(2)); // Val2
    break;
  case GNUCmpXchg:
    SubExprs.push_back(TheCall->getArg(4)); // Order
    SubExprs.push_back(TheCall->getArg(1)); // Val1
    SubExprs.push_back(TheCall->getArg(5)); // OrderFail
    SubExprs.push_back(TheCall->getArg(2)); // Val2
    SubExprs.push_back(TheCall->getArg(3)); // Weak
    break;
  }

  if (SubExprs.size() >= 2 && Form != Init) {
    llvm::APSInt Result(32);
    if (SubExprs[1]->isIntegerConstantExpr(Result, Context) &&
        !isValidOrderingForOp(Result.getSExtValue(), Op))
      Diag(SubExprs[1]->getBeginLoc(),
           diag::warn_atomic_op_has_invalid_memory_order)
          << SubExprs[1]->getSourceRange();
  }

  if (auto ScopeModel = AtomicExpr::getScopeModel(Op)) {
    auto *Scope = TheCall->getArg(TheCall->getNumArgs() - 1);
    llvm::APSInt Result(32);
    if (Scope->isIntegerConstantExpr(Result, Context) &&
        !ScopeModel->isValid(Result.getZExtValue())) {
      Diag(Scope->getBeginLoc(), diag::err_atomic_op_has_invalid_synch_scope)
          << Scope->getSourceRange();
    }
    SubExprs.push_back(Scope);
  }

  AtomicExpr *AE =
      new (Context) AtomicExpr(TheCall->getCallee()->getBeginLoc(), SubExprs,
                               ResultType, Op, TheCall->getRParenLoc());

  if ((Op == AtomicExpr::AO__c11_atomic_load ||
       Op == AtomicExpr::AO__c11_atomic_store ||
       Op == AtomicExpr::AO__opencl_atomic_load ||
       Op == AtomicExpr::AO__opencl_atomic_store ) &&
      Context.AtomicUsesUnsupportedLibcall(AE))
    Diag(AE->getBeginLoc(), diag::err_atomic_load_store_uses_lib)
        << ((Op == AtomicExpr::AO__c11_atomic_load ||
             Op == AtomicExpr::AO__opencl_atomic_load)
                ? 0
                : 1);

  return AE;
}

/// checkBuiltinArgument - Given a call to a builtin function, perform
/// normal type-checking on the given argument, updating the call in
/// place.  This is useful when a builtin function requires custom
/// type-checking for some of its arguments but not necessarily all of
/// them.
///
/// Returns true on error.
static bool checkBuiltinArgument(Sema &S, CallExpr *E, unsigned ArgIndex) {
  FunctionDecl *Fn = E->getDirectCallee();
  assert(Fn && "builtin call without direct callee!");

  ParmVarDecl *Param = Fn->getParamDecl(ArgIndex);
  InitializedEntity Entity =
    InitializedEntity::InitializeParameter(S.Context, Param);

  ExprResult Arg = E->getArg(0);
  Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
  if (Arg.isInvalid())
    return true;

  E->setArg(ArgIndex, Arg.get());
  return false;
}

/// We have a call to a function like __sync_fetch_and_add, which is an
/// overloaded function based on the pointer type of its first argument.
/// The main ActOnCallExpr routines have already promoted the types of
/// arguments because all of these calls are prototyped as void(...).
///
/// This function goes through and does final semantic checking for these
/// builtins, as well as generating any warnings.
ExprResult
Sema::SemaBuiltinAtomicOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = static_cast<CallExpr *>(TheCallResult.get());
  Expr *Callee = TheCall->getCallee();
  DeclRefExpr *DRE = cast<DeclRefExpr>(Callee->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  // Ensure that we have at least one argument to do type inference from.
  if (TheCall->getNumArgs() < 1) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 << TheCall->getNumArgs() << Callee->getSourceRange();
    return ExprError();
  }

  // Inspect the first argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  // FIXME: We don't allow floating point scalars as input.
  Expr *FirstArg = TheCall->getArg(0);
  ExprResult FirstArgResult = DefaultFunctionArrayLvalueConversion(FirstArg);
  if (FirstArgResult.isInvalid())
    return ExprError();
  FirstArg = FirstArgResult.get();
  TheCall->setArg(0, FirstArg);

  const PointerType *pointerType = FirstArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intptr)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  if (ValType.isConstQualified()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_cannot_be_const)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    Diag(DRE->getBeginLoc(), diag::err_arc_atomic_ownership)
        << ValType << FirstArg->getSourceRange();
    return ExprError();
  }

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();

  // The majority of builtins return a value, but a few have special return
  // types, so allow them to override appropriately below.
  QualType ResultType = ValType;

  // We need to figure out which concrete builtin this maps onto.  For example,
  // __sync_fetch_and_add with a 2 byte object turns into
  // __sync_fetch_and_add_2.
#define BUILTIN_ROW(x) \
  { Builtin::BI##x##_1, Builtin::BI##x##_2, Builtin::BI##x##_4, \
    Builtin::BI##x##_8, Builtin::BI##x##_16 }

  static const unsigned BuiltinIndices[][5] = {
    BUILTIN_ROW(__sync_fetch_and_add),
    BUILTIN_ROW(__sync_fetch_and_sub),
    BUILTIN_ROW(__sync_fetch_and_or),
    BUILTIN_ROW(__sync_fetch_and_and),
    BUILTIN_ROW(__sync_fetch_and_xor),
    BUILTIN_ROW(__sync_fetch_and_nand),

    BUILTIN_ROW(__sync_add_and_fetch),
    BUILTIN_ROW(__sync_sub_and_fetch),
    BUILTIN_ROW(__sync_and_and_fetch),
    BUILTIN_ROW(__sync_or_and_fetch),
    BUILTIN_ROW(__sync_xor_and_fetch),
    BUILTIN_ROW(__sync_nand_and_fetch),

    BUILTIN_ROW(__sync_val_compare_and_swap),
    BUILTIN_ROW(__sync_bool_compare_and_swap),
    BUILTIN_ROW(__sync_lock_test_and_set),
    BUILTIN_ROW(__sync_lock_release),
    BUILTIN_ROW(__sync_swap)
  };
#undef BUILTIN_ROW

  // Determine the index of the size.
  unsigned SizeIndex;
  switch (Context.getTypeSizeInChars(ValType).getQuantity()) {
  case 1: SizeIndex = 0; break;
  case 2: SizeIndex = 1; break;
  case 4: SizeIndex = 2; break;
  case 8: SizeIndex = 3; break;
  case 16: SizeIndex = 4; break;
  default:
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_pointer_size)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  // Each of these builtins has one pointer argument, followed by some number of
  // values (0, 1 or 2) followed by a potentially empty varags list of stuff
  // that we ignore.  Find out which row of BuiltinIndices to read from as well
  // as the number of fixed args.
  unsigned BuiltinID = FDecl->getBuiltinID();
  unsigned BuiltinIndex, NumFixed = 1;
  bool WarnAboutSemanticsChange = false;
  switch (BuiltinID) {
  default: llvm_unreachable("Unknown overloaded atomic builtin!");
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    BuiltinIndex = 0;
    break;

  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    BuiltinIndex = 1;
    break;

  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    BuiltinIndex = 2;
    break;

  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    BuiltinIndex = 3;
    break;

  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    BuiltinIndex = 4;
    break;

  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    BuiltinIndex = 5;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    BuiltinIndex = 6;
    break;

  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    BuiltinIndex = 7;
    break;

  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    BuiltinIndex = 8;
    break;

  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    BuiltinIndex = 9;
    break;

  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    BuiltinIndex = 10;
    break;

  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    BuiltinIndex = 11;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    BuiltinIndex = 12;
    NumFixed = 2;
    break;

  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    BuiltinIndex = 13;
    NumFixed = 2;
    ResultType = Context.BoolTy;
    break;

  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    BuiltinIndex = 14;
    break;

  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
    BuiltinIndex = 15;
    NumFixed = 0;
    ResultType = Context.VoidTy;
    break;

  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    BuiltinIndex = 16;
    break;
  }

  // Now that we know how many fixed arguments we expect, first check that we
  // have at least that many.
  if (TheCall->getNumArgs() < 1+NumFixed) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 + NumFixed << TheCall->getNumArgs()
        << Callee->getSourceRange();
    return ExprError();
  }

  Diag(TheCall->getEndLoc(), diag::warn_atomic_implicit_seq_cst)
      << Callee->getSourceRange();

  if (WarnAboutSemanticsChange) {
    Diag(TheCall->getEndLoc(), diag::warn_sync_fetch_and_nand_semantics_change)
        << Callee->getSourceRange();
  }

  // Get the decl for the concrete builtin from this, we can tell what the
  // concrete integer type we should convert to is.
  unsigned NewBuiltinID = BuiltinIndices[BuiltinIndex][SizeIndex];
  const char *NewBuiltinName = Context.BuiltinInfo.getName(NewBuiltinID);
  FunctionDecl *NewBuiltinDecl;
  if (NewBuiltinID == BuiltinID)
    NewBuiltinDecl = FDecl;
  else {
    // Perform builtin lookup to avoid redeclaring it.
    DeclarationName DN(&Context.Idents.get(NewBuiltinName));
    LookupResult Res(*this, DN, DRE->getBeginLoc(), LookupOrdinaryName);
    LookupName(Res, TUScope, /*AllowBuiltinCreation=*/true);
    assert(Res.getFoundDecl());
    NewBuiltinDecl = dyn_cast<FunctionDecl>(Res.getFoundDecl());
    if (!NewBuiltinDecl)
      return ExprError();
  }

  // The first argument --- the pointer --- has a fixed type; we
  // deduce the types of the rest of the arguments accordingly.  Walk
  // the remaining arguments, converting them to the deduced value type.
  for (unsigned i = 0; i != NumFixed; ++i) {
    ExprResult Arg = TheCall->getArg(i+1);

    // GCC does an implicit conversion to the pointer or integer ValType.  This
    // can fail in some cases (1i -> int**), check for this error case now.
    // Initialize the argument.
    InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
                                                   ValType, /*consume*/ false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return ExprError();

    // Okay, we have something that *can* be converted to the right type.  Check
    // to see if there is a potentially weird extension going on here.  This can
    // happen when you do an atomic operation on something like an char* and
    // pass in 42.  The 42 gets converted to char.  This is even more strange
    // for things like 45.123 -> char, etc.
    // FIXME: Do this check.
    TheCall->setArg(i+1, Arg.get());
  }

  // Create a new DeclRefExpr to refer to the new decl.
  DeclRefExpr* NewDRE = DeclRefExpr::Create(
      Context,
      DRE->getQualifierLoc(),
      SourceLocation(),
      NewBuiltinDecl,
      /*enclosing*/ false,
      DRE->getLocation(),
      Context.BuiltinFnTy,
      DRE->getValueKind());

  // Set the callee in the CallExpr.
  // FIXME: This loses syntactic information.
  QualType CalleePtrTy = Context.getPointerType(NewBuiltinDecl->getType());
  ExprResult PromotedCall = ImpCastExprToType(NewDRE, CalleePtrTy,
                                              CK_BuiltinFnToFnPtr);
  TheCall->setCallee(PromotedCall.get());

  // Change the result type of the call to match the original value type. This
  // is arbitrary, but the codegen for these builtins ins design to handle it
  // gracefully.
  TheCall->setType(ResultType);

  return TheCallResult;
}

/// SemaBuiltinNontemporalOverloaded - We have a call to
/// __builtin_nontemporal_store or __builtin_nontemporal_load, which is an
/// overloaded function based on the pointer type of its last argument.
///
/// This function goes through and does final semantic checking for these
/// builtins.
ExprResult Sema::SemaBuiltinNontemporalOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = (CallExpr *)TheCallResult.get();
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());
  unsigned BuiltinID = FDecl->getBuiltinID();
  assert((BuiltinID == Builtin::BI__builtin_nontemporal_store ||
          BuiltinID == Builtin::BI__builtin_nontemporal_load) &&
         "Unexpected nontemporal load/store builtin!");
  bool isStore = BuiltinID == Builtin::BI__builtin_nontemporal_store;
  unsigned numArgs = isStore ? 2 : 1;

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(*this, TheCall, numArgs))
    return ExprError();

  // Inspect the last argument of the nontemporal builtin.  This should always
  // be a pointer type, from which we imply the type of the memory access.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(numArgs - 1);
  ExprResult PointerArgResult =
      DefaultFunctionArrayLvalueConversion(PointerArg);

  if (PointerArgResult.isInvalid())
    return ExprError();
  PointerArg = PointerArgResult.get();
  TheCall->setArg(numArgs - 1, PointerArg);

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_nontemporal_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType() && !ValType->isFloatingType() &&
      !ValType->isVectorType()) {
    Diag(DRE->getBeginLoc(),
         diag::err_nontemporal_builtin_must_be_pointer_intfltptr_or_vector)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  if (!isStore) {
    TheCall->setType(ValType);
    return TheCallResult;
  }

  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return ExprError();

  TheCall->setArg(0, ValArg.get());
  TheCall->setType(Context.VoidTy);
  return TheCallResult;
}

/// CheckObjCString - Checks that the argument to the builtin
/// CFString constructor is correct
/// Note: It might also make sense to do the UTF-16 conversion here (would
/// simplify the backend).
bool Sema::CheckObjCString(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(Arg);

  if (!Literal || !Literal->isAscii()) {
    Diag(Arg->getBeginLoc(), diag::err_cfstring_literal_not_string_constant)
        << Arg->getSourceRange();
    return true;
  }

  if (Literal->containsNonAsciiOrNull()) {
    StringRef String = Literal->getString();
    unsigned NumBytes = String.size();
    SmallVector<llvm::UTF16, 128> ToBuf(NumBytes);
    const llvm::UTF8 *FromPtr = (const llvm::UTF8 *)String.data();
    llvm::UTF16 *ToPtr = &ToBuf[0];

    llvm::ConversionResult Result =
        llvm::ConvertUTF8toUTF16(&FromPtr, FromPtr + NumBytes, &ToPtr,
                                 ToPtr + NumBytes, llvm::strictConversion);
    // Check for conversion failure.
    if (Result != llvm::conversionOK)
      Diag(Arg->getBeginLoc(), diag::warn_cfstring_truncated)
          << Arg->getSourceRange();
  }
  return false;
}

/// CheckObjCString - Checks that the format string argument to the os_log()
/// and os_trace() functions is correct, and converts it to const char *.
ExprResult Sema::CheckOSLogFormatStringArg(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  auto *Literal = dyn_cast<StringLiteral>(Arg);
  if (!Literal) {
    if (auto *ObjcLiteral = dyn_cast<ObjCStringLiteral>(Arg)) {
      Literal = ObjcLiteral->getString();
    }
  }

  if (!Literal || (!Literal->isAscii() && !Literal->isUTF8())) {
    return ExprError(
        Diag(Arg->getBeginLoc(), diag::err_os_log_format_not_string_constant)
        << Arg->getSourceRange());
  }

  ExprResult Result(Literal);
  QualType ResultTy = Context.getPointerType(Context.CharTy.withConst());
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(Context, ResultTy, false);
  Result = PerformCopyInitialization(Entity, SourceLocation(), Result);
  return Result;
}

/// Check that the user is calling the appropriate va_start builtin for the
/// target and calling convention.
static bool checkVAStartABI(Sema &S, unsigned BuiltinID, Expr *Fn) {
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  bool IsX64 = TT.getArch() == llvm::Triple::x86_64;
  bool IsAArch64 = TT.getArch() == llvm::Triple::aarch64;
  bool IsWindows = TT.isOSWindows();
  bool IsMSVAStart = BuiltinID == Builtin::BI__builtin_ms_va_start;
  if (IsX64 || IsAArch64) {
    CallingConv CC = CC_C;
    if (const FunctionDecl *FD = S.getCurFunctionDecl())
      CC = FD->getType()->getAs<FunctionType>()->getCallConv();
    if (IsMSVAStart) {
      // Don't allow this in System V ABI functions.
      if (CC == CC_X86_64SysV || (!IsWindows && CC != CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_ms_va_start_used_in_sysv_function);
    } else {
      // On x86-64/AArch64 Unix, don't allow this in Win64 ABI functions.
      // On x64 Windows, don't allow this in System V ABI functions.
      // (Yes, that means there's no corresponding way to support variadic
      // System V ABI functions on Windows.)
      if ((IsWindows && CC == CC_X86_64SysV) ||
          (!IsWindows && CC == CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_va_start_used_in_wrong_abi_function)
               << !IsWindows;
    }
    return false;
  }

  if (IsMSVAStart)
    return S.Diag(Fn->getBeginLoc(), diag::err_builtin_x64_aarch64_only);
  return false;
}

static bool checkVAStartIsInVariadicFunction(Sema &S, Expr *Fn,
                                             ParmVarDecl **LastParam = nullptr) {
  // Determine whether the current function, block, or obj-c method is variadic
  // and get its parameter list.
  bool IsVariadic = false;
  ArrayRef<ParmVarDecl *> Params;
  DeclContext *Caller = S.CurContext;
  if (auto *Block = dyn_cast<BlockDecl>(Caller)) {
    IsVariadic = Block->isVariadic();
    Params = Block->parameters();
  } else if (auto *FD = dyn_cast<FunctionDecl>(Caller)) {
    IsVariadic = FD->isVariadic();
    Params = FD->parameters();
  } else if (auto *MD = dyn_cast<ObjCMethodDecl>(Caller)) {
    IsVariadic = MD->isVariadic();
    // FIXME: This isn't correct for methods (results in bogus warning).
    Params = MD->parameters();
  } else if (isa<CapturedDecl>(Caller)) {
    // We don't support va_start in a CapturedDecl.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_captured_stmt);
    return true;
  } else {
    // This must be some other declcontext that parses exprs.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_outside_function);
    return true;
  }

  if (!IsVariadic) {
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_fixed_function);
    return true;
  }

  if (LastParam)
    *LastParam = Params.empty() ? nullptr : Params.back();

  return false;
}

/// Check the arguments to '__builtin_va_start' or '__builtin_ms_va_start'
/// for validity.  Emit an error and return true on failure; return false
/// on success.
bool Sema::SemaBuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall) {
  Expr *Fn = TheCall->getCallee();

  if (checkVAStartABI(*this, BuiltinID, Fn))
    return true;

  if (TheCall->getNumArgs() > 2) {
    Diag(TheCall->getArg(2)->getBeginLoc(),
         diag::err_typecheck_call_too_many_args)
        << 0 /*function call*/ << 2 << TheCall->getNumArgs()
        << Fn->getSourceRange()
        << SourceRange(TheCall->getArg(2)->getBeginLoc(),
                       (*(TheCall->arg_end() - 1))->getEndLoc());
    return true;
  }

  if (TheCall->getNumArgs() < 2) {
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 0 /*function call*/ << 2 << TheCall->getNumArgs();
  }

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, TheCall, 0))
    return true;

  // Check that the current function is variadic, and get its last parameter.
  ParmVarDecl *LastParam;
  if (checkVAStartIsInVariadicFunction(*this, Fn, &LastParam))
    return true;

  // Verify that the second argument to the builtin is the last argument of the
  // current function or method.
  bool SecondArgIsLastNamedArgument = false;
  const Expr *Arg = TheCall->getArg(1)->IgnoreParenCasts();

  // These are valid if SecondArgIsLastNamedArgument is false after the next
  // block.
  QualType Type;
  SourceLocation ParamLoc;
  bool IsCRegister = false;

  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Arg)) {
    if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(DR->getDecl())) {
      SecondArgIsLastNamedArgument = PV == LastParam;

      Type = PV->getType();
      ParamLoc = PV->getLocation();
      IsCRegister =
          PV->getStorageClass() == SC_Register && !getLangOpts().CPlusPlus;
    }
  }

  if (!SecondArgIsLastNamedArgument)
    Diag(TheCall->getArg(1)->getBeginLoc(),
         diag::warn_second_arg_of_va_start_not_last_named_param);
  else if (IsCRegister || Type->isReferenceType() ||
           Type->isSpecificBuiltinType(BuiltinType::Float) || [=] {
             // Promotable integers are UB, but enumerations need a bit of
             // extra checking to see what their promotable type actually is.
             if (!Type->isPromotableIntegerType())
               return false;
             if (!Type->isEnumeralType())
               return true;
             const EnumDecl *ED = Type->getAs<EnumType>()->getDecl();
             return !(ED &&
                      Context.typesAreCompatible(ED->getPromotionType(), Type));
           }()) {
    unsigned Reason = 0;
    if (Type->isReferenceType())  Reason = 1;
    else if (IsCRegister)         Reason = 2;
    Diag(Arg->getBeginLoc(), diag::warn_va_start_type_is_undefined) << Reason;
    Diag(ParamLoc, diag::note_parameter_type) << Type;
  }

  TheCall->setType(Context.VoidTy);
  return false;
}

bool Sema::SemaBuiltinVAStartARMMicrosoft(CallExpr *Call) {
  // void __va_start(va_list *ap, const char *named_addr, size_t slot_size,
  //                 const char *named_addr);

  Expr *Func = Call->getCallee();

  if (Call->getNumArgs() < 3)
    return Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 0 /*function call*/ << 3 << Call->getNumArgs();

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, Call, 0))
    return true;

  // Check that the current function is variadic.
  if (checkVAStartIsInVariadicFunction(*this, Func))
    return true;

  // __va_start on Windows does not validate the parameter qualifiers

  const Expr *Arg1 = Call->getArg(1)->IgnoreParens();
  const Type *Arg1Ty = Arg1->getType().getCanonicalType().getTypePtr();

  const Expr *Arg2 = Call->getArg(2)->IgnoreParens();
  const Type *Arg2Ty = Arg2->getType().getCanonicalType().getTypePtr();

  const QualType &ConstCharPtrTy =
      Context.getPointerType(Context.CharTy.withConst());
  if (!Arg1Ty->isPointerType() ||
      Arg1Ty->getPointeeType().withoutLocalFastQualifiers() != Context.CharTy)
    Diag(Arg1->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg1->getType() << ConstCharPtrTy << 1 /* different class */
        << 0                                      /* qualifier difference */
        << 3                                      /* parameter mismatch */
        << 2 << Arg1->getType() << ConstCharPtrTy;

  const QualType SizeTy = Context.getSizeType();
  if (Arg2Ty->getCanonicalTypeInternal().withoutLocalFastQualifiers() != SizeTy)
    Diag(Arg2->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg2->getType() << SizeTy << 1 /* different class */
        << 0                              /* qualifier difference */
        << 3                              /* parameter mismatch */
        << 3 << Arg2->getType() << SizeTy;

  return false;
}

/// SemaBuiltinUnorderedCompare - Handle functions like __builtin_isgreater and
/// friends.  This is declared to take (...), so we have to check everything.
bool Sema::SemaBuiltinUnorderedCompare(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << 0 << 2 << TheCall->getNumArgs() /*function call*/;
  if (TheCall->getNumArgs() > 2)
    return Diag(TheCall->getArg(2)->getBeginLoc(),
                diag::err_typecheck_call_too_many_args)
           << 0 /*function call*/ << 2 << TheCall->getNumArgs()
           << SourceRange(TheCall->getArg(2)->getBeginLoc(),
                          (*(TheCall->arg_end() - 1))->getEndLoc());

  ExprResult OrigArg0 = TheCall->getArg(0);
  ExprResult OrigArg1 = TheCall->getArg(1);

  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res = UsualArithmeticConversions(OrigArg0, OrigArg1, false);
  if (OrigArg0.isInvalid() || OrigArg1.isInvalid())
    return true;

  // Make sure any conversions are pushed back into the call; this is
  // type safe since unordered compare builtins are declared as "_Bool
  // foo(...)".
  TheCall->setArg(0, OrigArg0.get());
  TheCall->setArg(1, OrigArg1.get());

  if (OrigArg0.get()->isTypeDependent() || OrigArg1.get()->isTypeDependent())
    return false;

  // If the common type isn't a real floating type, then the arguments were
  // invalid for this operation.
  if (Res.isNull() || !Res->isRealFloatingType())
    return Diag(OrigArg0.get()->getBeginLoc(),
                diag::err_typecheck_call_invalid_ordered_compare)
           << OrigArg0.get()->getType() << OrigArg1.get()->getType()
           << SourceRange(OrigArg0.get()->getBeginLoc(),
                          OrigArg1.get()->getEndLoc());

  return false;
}

/// SemaBuiltinSemaBuiltinFPClassification - Handle functions like
/// __builtin_isnan and friends.  This is declared to take (...), so we have
/// to check everything. We expect the last argument to be a floating point
/// value.
bool Sema::SemaBuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs) {
  if (TheCall->getNumArgs() < NumArgs)
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << 0 << NumArgs << TheCall->getNumArgs() /*function call*/;
  if (TheCall->getNumArgs() > NumArgs)
    return Diag(TheCall->getArg(NumArgs)->getBeginLoc(),
                diag::err_typecheck_call_too_many_args)
           << 0 /*function call*/ << NumArgs << TheCall->getNumArgs()
           << SourceRange(TheCall->getArg(NumArgs)->getBeginLoc(),
                          (*(TheCall->arg_end() - 1))->getEndLoc());

  Expr *OrigArg = TheCall->getArg(NumArgs-1);

  if (OrigArg->isTypeDependent())
    return false;

  // This operation requires a non-_Complex floating-point number.
  if (!OrigArg->getType()->isRealFloatingType())
    return Diag(OrigArg->getBeginLoc(),
                diag::err_typecheck_call_invalid_unary_fp)
           << OrigArg->getType() << OrigArg->getSourceRange();

  // If this is an implicit conversion from float -> float, double, or
  // long double, remove it.
  if (ImplicitCastExpr *Cast = dyn_cast<ImplicitCastExpr>(OrigArg)) {
    // Only remove standard FloatCasts, leaving other casts inplace
    if (Cast->getCastKind() == CK_FloatingCast) {
      Expr *CastArg = Cast->getSubExpr();
      if (CastArg->getType()->isSpecificBuiltinType(BuiltinType::Float)) {
        assert(
            (Cast->getType()->isSpecificBuiltinType(BuiltinType::Double) ||
             Cast->getType()->isSpecificBuiltinType(BuiltinType::Float) ||
             Cast->getType()->isSpecificBuiltinType(BuiltinType::LongDouble)) &&
            "promotion from float to either float, double, or long double is "
            "the only expected cast here");
        Cast->setSubExpr(nullptr);
        TheCall->setArg(NumArgs-1, CastArg);
      }
    }
  }

  return false;
}

// Customized Sema Checking for VSX builtins that have the following signature:
// vector [...] builtinName(vector [...], vector [...], const int);
// Which takes the same type of vectors (any legal vector type) for the first
// two arguments and takes compile time constant for the third argument.
// Example builtins are :
// vector double vec_xxpermdi(vector double, vector double, int);
// vector short vec_xxsldwi(vector short, vector short, int);
bool Sema::SemaBuiltinVSX(CallExpr *TheCall) {
  unsigned ExpectedNumArgs = 3;
  if (TheCall->getNumArgs() < ExpectedNumArgs)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 0 /*function call*/ << ExpectedNumArgs << TheCall->getNumArgs()
           << TheCall->getSourceRange();

  if (TheCall->getNumArgs() > ExpectedNumArgs)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /*function call*/ << ExpectedNumArgs << TheCall->getNumArgs()
           << TheCall->getSourceRange();

  // Check the third argument is a compile time constant
  llvm::APSInt Value;
  if(!TheCall->getArg(2)->isIntegerConstantExpr(Value, Context))
    return Diag(TheCall->getBeginLoc(),
                diag::err_vsx_builtin_nonconstant_argument)
           << 3 /* argument index */ << TheCall->getDirectCallee()
           << SourceRange(TheCall->getArg(2)->getBeginLoc(),
                          TheCall->getArg(2)->getEndLoc());

  QualType Arg1Ty = TheCall->getArg(0)->getType();
  QualType Arg2Ty = TheCall->getArg(1)->getType();

  // Check the type of argument 1 and argument 2 are vectors.
  SourceLocation BuiltinLoc = TheCall->getBeginLoc();
  if ((!Arg1Ty->isVectorType() && !Arg1Ty->isDependentType()) ||
      (!Arg2Ty->isVectorType() && !Arg2Ty->isDependentType())) {
    return Diag(BuiltinLoc, diag::err_vec_builtin_non_vector)
           << TheCall->getDirectCallee()
           << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                          TheCall->getArg(1)->getEndLoc());
  }

  // Check the first two arguments are the same type.
  if (!Context.hasSameUnqualifiedType(Arg1Ty, Arg2Ty)) {
    return Diag(BuiltinLoc, diag::err_vec_builtin_incompatible_vector)
           << TheCall->getDirectCallee()
           << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                          TheCall->getArg(1)->getEndLoc());
  }

  // When default clang type checking is turned off and the customized type
  // checking is used, the returning type of the function must be explicitly
  // set. Otherwise it is _Bool by default.
  TheCall->setType(Arg1Ty);

  return false;
}

/// SemaBuiltinShuffleVector - Handle __builtin_shufflevector.
// This is declared to take (...), so we have to check everything.
ExprResult Sema::SemaBuiltinShuffleVector(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return ExprError(Diag(TheCall->getEndLoc(),
                          diag::err_typecheck_call_too_few_args_at_least)
                     << 0 /*function call*/ << 2 << TheCall->getNumArgs()
                     << TheCall->getSourceRange());

  // Determine which of the following types of shufflevector we're checking:
  // 1) unary, vector mask: (lhs, mask)
  // 2) binary, scalar mask: (lhs, rhs, index, ..., index)
  QualType resType = TheCall->getArg(0)->getType();
  unsigned numElements = 0;

  if (!TheCall->getArg(0)->isTypeDependent() &&
      !TheCall->getArg(1)->isTypeDependent()) {
    QualType LHSType = TheCall->getArg(0)->getType();
    QualType RHSType = TheCall->getArg(1)->getType();

    if (!LHSType->isVectorType() || !RHSType->isVectorType())
      return ExprError(
          Diag(TheCall->getBeginLoc(), diag::err_vec_builtin_non_vector)
          << TheCall->getDirectCallee()
          << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                         TheCall->getArg(1)->getEndLoc()));

    numElements = LHSType->getAs<VectorType>()->getNumElements();
    unsigned numResElements = TheCall->getNumArgs() - 2;

    // Check to see if we have a call with 2 vector arguments, the unary shuffle
    // with mask.  If so, verify that RHS is an integer vector type with the
    // same number of elts as lhs.
    if (TheCall->getNumArgs() == 2) {
      if (!RHSType->hasIntegerRepresentation() ||
          RHSType->getAs<VectorType>()->getNumElements() != numElements)
        return ExprError(Diag(TheCall->getBeginLoc(),
                              diag::err_vec_builtin_incompatible_vector)
                         << TheCall->getDirectCallee()
                         << SourceRange(TheCall->getArg(1)->getBeginLoc(),
                                        TheCall->getArg(1)->getEndLoc()));
    } else if (!Context.hasSameUnqualifiedType(LHSType, RHSType)) {
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_vec_builtin_incompatible_vector)
                       << TheCall->getDirectCallee()
                       << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                                      TheCall->getArg(1)->getEndLoc()));
    } else if (numElements != numResElements) {
      QualType eltType = LHSType->getAs<VectorType>()->getElementType();
      resType = Context.getVectorType(eltType, numResElements,
                                      VectorType::GenericVector);
    }
  }

  for (unsigned i = 2; i < TheCall->getNumArgs(); i++) {
    if (TheCall->getArg(i)->isTypeDependent() ||
        TheCall->getArg(i)->isValueDependent())
      continue;

    llvm::APSInt Result(32);
    if (!TheCall->getArg(i)->isIntegerConstantExpr(Result, Context))
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_nonconstant_argument)
                       << TheCall->getArg(i)->getSourceRange());

    // Allow -1 which will be translated to undef in the IR.
    if (Result.isSigned() && Result.isAllOnesValue())
      continue;

    if (Result.getActiveBits() > 64 || Result.getZExtValue() >= numElements*2)
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_argument_too_large)
                       << TheCall->getArg(i)->getSourceRange());
  }

  SmallVector<Expr*, 32> exprs;

  for (unsigned i = 0, e = TheCall->getNumArgs(); i != e; i++) {
    exprs.push_back(TheCall->getArg(i));
    TheCall->setArg(i, nullptr);
  }

  return new (Context) ShuffleVectorExpr(Context, exprs, resType,
                                         TheCall->getCallee()->getBeginLoc(),
                                         TheCall->getRParenLoc());
}

/// SemaConvertVectorExpr - Handle __builtin_convertvector
ExprResult Sema::SemaConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                                       SourceLocation BuiltinLoc,
                                       SourceLocation RParenLoc) {
  ExprValueKind VK = VK_RValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType DstTy = TInfo->getType();
  QualType SrcTy = E->getType();

  if (!SrcTy->isVectorType() && !SrcTy->isDependentType())
    return ExprError(Diag(BuiltinLoc,
                          diag::err_convertvector_non_vector)
                     << E->getSourceRange());
  if (!DstTy->isVectorType() && !DstTy->isDependentType())
    return ExprError(Diag(BuiltinLoc,
                          diag::err_convertvector_non_vector_type));

  if (!SrcTy->isDependentType() && !DstTy->isDependentType()) {
    unsigned SrcElts = SrcTy->getAs<VectorType>()->getNumElements();
    unsigned DstElts = DstTy->getAs<VectorType>()->getNumElements();
    if (SrcElts != DstElts)
      return ExprError(Diag(BuiltinLoc,
                            diag::err_convertvector_incompatible_vector)
                       << E->getSourceRange());
  }

  return new (Context)
      ConvertVectorExpr(E, TInfo, DstTy, VK, OK, BuiltinLoc, RParenLoc);
}

/// SemaBuiltinPrefetch - Handle __builtin_prefetch.
// This is declared to take (const void*, ...) and can take two
// optional constant int args.
bool Sema::SemaBuiltinPrefetch(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /*function call*/ << 3 << NumArgs << TheCall->getSourceRange();

  // Argument 0 is checked for us and the remaining arguments must be
  // constant integers.
  for (unsigned i = 1; i != NumArgs; ++i)
    if (SemaBuiltinConstantArgRange(TheCall, i, 0, i == 1 ? 1 : 3))
      return true;

  return false;
}

/// SemaBuiltinAssume - Handle __assume (MS Extension).
// __assume does not evaluate its arguments, and should warn if its argument
// has side effects.
bool Sema::SemaBuiltinAssume(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);
  if (Arg->isInstantiationDependent()) return false;

  if (Arg->HasSideEffects(Context))
    Diag(Arg->getBeginLoc(), diag::warn_assume_side_effects)
        << Arg->getSourceRange()
        << cast<FunctionDecl>(TheCall->getCalleeDecl())->getIdentifier();

  return false;
}

/// Handle __builtin_alloca_with_align. This is declared
/// as (size_t, size_t) where the second size_t must be a power of 2 greater
/// than 8.
bool Sema::SemaBuiltinAllocaWithAlign(CallExpr *TheCall) {
  // The alignment must be a constant integer.
  Expr *Arg = TheCall->getArg(1);

  // We can't check the value of a dependent argument.
  if (!Arg->isTypeDependent() && !Arg->isValueDependent()) {
    if (const auto *UE =
            dyn_cast<UnaryExprOrTypeTraitExpr>(Arg->IgnoreParenImpCasts()))
      if (UE->getKind() == UETT_AlignOf ||
          UE->getKind() == UETT_PreferredAlignOf)
        Diag(TheCall->getBeginLoc(), diag::warn_alloca_align_alignof)
            << Arg->getSourceRange();

    llvm::APSInt Result = Arg->EvaluateKnownConstInt(Context);

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << Arg->getSourceRange();

    if (Result < Context.getCharWidth())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_small)
             << (unsigned)Context.getCharWidth() << Arg->getSourceRange();

    if (Result > std::numeric_limits<int32_t>::max())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_big)
             << std::numeric_limits<int32_t>::max() << Arg->getSourceRange();
  }

  return false;
}

/// Handle __builtin_assume_aligned. This is declared
/// as (const void*, size_t, ...) and can take one optional constant int arg.
bool Sema::SemaBuiltinAssumeAligned(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /*function call*/ << 3 << NumArgs << TheCall->getSourceRange();

  // The alignment must be a constant integer.
  Expr *Arg = TheCall->getArg(1);

  // We can't check the value of a dependent argument.
  if (!Arg->isTypeDependent() && !Arg->isValueDependent()) {
    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, 1, Result))
      return true;

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << Arg->getSourceRange();
  }

  if (NumArgs > 2) {
    ExprResult Arg(TheCall->getArg(2));
    InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
      Context.getSizeType(), false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid()) return true;
    TheCall->setArg(2, Arg.get());
  }

  return false;
}

bool Sema::SemaBuiltinOSLogFormat(CallExpr *TheCall) {
  unsigned BuiltinID =
      cast<FunctionDecl>(TheCall->getCalleeDecl())->getBuiltinID();
  bool IsSizeCall = BuiltinID == Builtin::BI__builtin_os_log_format_buffer_size;

  unsigned NumArgs = TheCall->getNumArgs();
  unsigned NumRequiredArgs = IsSizeCall ? 1 : 2;
  if (NumArgs < NumRequiredArgs) {
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << 0 /* function call */ << NumRequiredArgs << NumArgs
           << TheCall->getSourceRange();
  }
  if (NumArgs >= NumRequiredArgs + 0x100) {
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /* function call */ << (NumRequiredArgs + 0xff) << NumArgs
           << TheCall->getSourceRange();
  }
  unsigned i = 0;

  // For formatting call, check buffer arg.
  if (!IsSizeCall) {
    ExprResult Arg(TheCall->getArg(i));
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, Context.VoidPtrTy, false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check string literal arg.
  unsigned FormatIdx = i;
  {
    ExprResult Arg = CheckOSLogFormatStringArg(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Make sure variadic args are scalar.
  unsigned FirstDataArg = i;
  while (i < NumArgs) {
    ExprResult Arg = DefaultVariadicArgumentPromotion(
        TheCall->getArg(i), VariadicFunction, nullptr);
    if (Arg.isInvalid())
      return true;
    CharUnits ArgSize = Context.getTypeSizeInChars(Arg.get()->getType());
    if (ArgSize.getQuantity() >= 0x100) {
      return Diag(Arg.get()->getEndLoc(), diag::err_os_log_argument_too_big)
             << i << (int)ArgSize.getQuantity() << 0xff
             << TheCall->getSourceRange();
    }
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check formatting specifiers. NOTE: We're only doing this for the non-size
  // call to avoid duplicate diagnostics.
  if (!IsSizeCall) {
    llvm::SmallBitVector CheckedVarArgs(NumArgs, false);
    ArrayRef<const Expr *> Args(TheCall->getArgs(), TheCall->getNumArgs());
    bool Success = CheckFormatArguments(
        Args, /*HasVAListArg*/ false, FormatIdx, FirstDataArg, FST_OSLog,
        VariadicFunction, TheCall->getBeginLoc(), SourceRange(),
        CheckedVarArgs);
    if (!Success)
      return true;
  }

  if (IsSizeCall) {
    TheCall->setType(Context.getSizeType());
  } else {
    TheCall->setType(Context.VoidPtrTy);
  }
  return false;
}

/// SemaBuiltinConstantArg - Handle a check if argument ArgNum of CallExpr
/// TheCall is a constant expression.
bool Sema::SemaBuiltinConstantArg(CallExpr *TheCall, int ArgNum,
                                  llvm::APSInt &Result) {
  Expr *Arg = TheCall->getArg(ArgNum);
  DeclRefExpr *DRE =cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  if (Arg->isTypeDependent() || Arg->isValueDependent()) return false;

  if (!Arg->isIntegerConstantExpr(Result, Context))
    return Diag(TheCall->getBeginLoc(), diag::err_constant_integer_arg_type)
           << FDecl->getDeclName() << Arg->getSourceRange();

  return false;
}

/// SemaBuiltinConstantArgRange - Handle a check if argument ArgNum of CallExpr
/// TheCall is a constant expression in the range [Low, High].
bool Sema::SemaBuiltinConstantArgRange(CallExpr *TheCall, int ArgNum,
                                       int Low, int High, bool RangeIsError) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() < Low || Result.getSExtValue() > High) {
    if (RangeIsError)
      return Diag(TheCall->getBeginLoc(), diag::err_argument_invalid_range)
             << Result.toString(10) << Low << High << Arg->getSourceRange();
    else
      // Defer the warning until we know if the code will be emitted so that
      // dead code can ignore this.
      DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                          PDiag(diag::warn_argument_invalid_range)
                              << Result.toString(10) << Low << High
                              << Arg->getSourceRange());
  }

  return false;
}

/// SemaBuiltinConstantArgMultiple - Handle a check if argument ArgNum of CallExpr
/// TheCall is a constant expression is a multiple of Num..
bool Sema::SemaBuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                          unsigned Num) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() % Num != 0)
    return Diag(TheCall->getBeginLoc(), diag::err_argument_not_multiple)
           << Num << Arg->getSourceRange();

  return false;
}

/// SemaBuiltinARMSpecialReg - Handle a check if argument ArgNum of CallExpr
/// TheCall is an ARM/AArch64 special register string literal.
bool Sema::SemaBuiltinARMSpecialReg(unsigned BuiltinID, CallExpr *TheCall,
                                    int ArgNum, unsigned ExpectedFieldNum,
                                    bool AllowName) {
  bool IsARMBuiltin = BuiltinID == ARM::BI__builtin_arm_rsr64 ||
                      BuiltinID == ARM::BI__builtin_arm_wsr64 ||
                      BuiltinID == ARM::BI__builtin_arm_rsr ||
                      BuiltinID == ARM::BI__builtin_arm_rsrp ||
                      BuiltinID == ARM::BI__builtin_arm_wsr ||
                      BuiltinID == ARM::BI__builtin_arm_wsrp;
  bool IsAArch64Builtin = BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr ||
                          BuiltinID == AArch64::BI__builtin_arm_rsrp ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr ||
                          BuiltinID == AArch64::BI__builtin_arm_wsrp;
  assert((IsARMBuiltin || IsAArch64Builtin) && "Unexpected ARM builtin.");

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check if the argument is a string literal.
  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  // Check the type of special register given.
  StringRef Reg = cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  SmallVector<StringRef, 6> Fields;
  Reg.split(Fields, ":");

  if (Fields.size() != ExpectedFieldNum && !(AllowName && Fields.size() == 1))
    return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
           << Arg->getSourceRange();

  // If the string is the name of a register then we cannot check that it is
  // valid here but if the string is of one the forms described in ACLE then we
  // can check that the supplied fields are integers and within the valid
  // ranges.
  if (Fields.size() > 1) {
    bool FiveFields = Fields.size() == 5;

    bool ValidString = true;
    if (IsARMBuiltin) {
      ValidString &= Fields[0].startswith_lower("cp") ||
                     Fields[0].startswith_lower("p");
      if (ValidString)
        Fields[0] =
          Fields[0].drop_front(Fields[0].startswith_lower("cp") ? 2 : 1);

      ValidString &= Fields[2].startswith_lower("c");
      if (ValidString)
        Fields[2] = Fields[2].drop_front(1);

      if (FiveFields) {
        ValidString &= Fields[3].startswith_lower("c");
        if (ValidString)
          Fields[3] = Fields[3].drop_front(1);
      }
    }

    SmallVector<int, 5> Ranges;
    if (FiveFields)
      Ranges.append({IsAArch64Builtin ? 1 : 15, 7, 15, 15, 7});
    else
      Ranges.append({15, 7, 15});

    for (unsigned i=0; i<Fields.size(); ++i) {
      int IntField;
      ValidString &= !Fields[i].getAsInteger(10, IntField);
      ValidString &= (IntField >= 0 && IntField <= Ranges[i]);
    }

    if (!ValidString)
      return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
             << Arg->getSourceRange();
  } else if (IsAArch64Builtin && Fields.size() == 1) {
    // If the register name is one of those that appear in the condition below
    // and the special register builtin being used is one of the write builtins,
    // then we require that the argument provided for writing to the register
    // is an integer constant expression. This is because it will be lowered to
    // an MSR (immediate) instruction, so we need to know the immediate at
    // compile time.
    if (TheCall->getNumArgs() != 2)
      return false;

    std::string RegLower = Reg.lower();
    if (RegLower != "spsel" && RegLower != "daifset" && RegLower != "daifclr" &&
        RegLower != "pan" && RegLower != "uao")
      return false;

    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 15);
  }

  return false;
}

/// SemaBuiltinLongjmp - Handle __builtin_longjmp(void *env[5], int val).
/// This checks that the target supports __builtin_longjmp and
/// that val is a constant 1.
bool Sema::SemaBuiltinLongjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());

  Expr *Arg = TheCall->getArg(1);
  llvm::APSInt Result;

  // TODO: This is less than ideal. Overload this to take a value.
  if (SemaBuiltinConstantArg(TheCall, 1, Result))
    return true;

  if (Result != 1)
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_invalid_val)
           << SourceRange(Arg->getBeginLoc(), Arg->getEndLoc());

  return false;
}

/// SemaBuiltinSetjmp - Handle __builtin_setjmp(void *env[5]).
/// This checks that the target supports __builtin_setjmp.
bool Sema::SemaBuiltinSetjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_setjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  return false;
}

namespace {

class UncoveredArgHandler {
  enum { Unknown = -1, AllCovered = -2 };

  signed FirstUncoveredArg = Unknown;
  SmallVector<const Expr *, 4> DiagnosticExprs;

public:
  UncoveredArgHandler() = default;

  bool hasUncoveredArg() const {
    return (FirstUncoveredArg >= 0);
  }

  unsigned getUncoveredArg() const {
    assert(hasUncoveredArg() && "no uncovered argument");
    return FirstUncoveredArg;
  }

  void setAllCovered() {
    // A string has been found with all arguments covered, so clear out
    // the diagnostics.
    DiagnosticExprs.clear();
    FirstUncoveredArg = AllCovered;
  }

  void Update(signed NewFirstUncoveredArg, const Expr *StrExpr) {
    assert(NewFirstUncoveredArg >= 0 && "Outside range");

    // Don't update if a previous string covers all arguments.
    if (FirstUncoveredArg == AllCovered)
      return;

    // UncoveredArgHandler tracks the highest uncovered argument index
    // and with it all the strings that match this index.
    if (NewFirstUncoveredArg == FirstUncoveredArg)
      DiagnosticExprs.push_back(StrExpr);
    else if (NewFirstUncoveredArg > FirstUncoveredArg) {
      DiagnosticExprs.clear();
      DiagnosticExprs.push_back(StrExpr);
      FirstUncoveredArg = NewFirstUncoveredArg;
    }
  }

  void Diagnose(Sema &S, bool IsFunctionCall, const Expr *ArgExpr);
};

enum StringLiteralCheckType {
  SLCT_NotALiteral,
  SLCT_UncheckedLiteral,
  SLCT_CheckedLiteral
};

} // namespace

static void sumOffsets(llvm::APSInt &Offset, llvm::APSInt Addend,
                                     BinaryOperatorKind BinOpKind,
                                     bool AddendIsRight) {
  unsigned BitWidth = Offset.getBitWidth();
  unsigned AddendBitWidth = Addend.getBitWidth();
  // There might be negative interim results.
  if (Addend.isUnsigned()) {
    Addend = Addend.zext(++AddendBitWidth);
    Addend.setIsSigned(true);
  }
  // Adjust the bit width of the APSInts.
  if (AddendBitWidth > BitWidth) {
    Offset = Offset.sext(AddendBitWidth);
    BitWidth = AddendBitWidth;
  } else if (BitWidth > AddendBitWidth) {
    Addend = Addend.sext(BitWidth);
  }

  bool Ov = false;
  llvm::APSInt ResOffset = Offset;
  if (BinOpKind == BO_Add)
    ResOffset = Offset.sadd_ov(Addend, Ov);
  else {
    assert(AddendIsRight && BinOpKind == BO_Sub &&
           "operator must be add or sub with addend on the right");
    ResOffset = Offset.ssub_ov(Addend, Ov);
  }

  // We add an offset to a pointer here so we should support an offset as big as
  // possible.
  if (Ov) {
    assert(BitWidth <= std::numeric_limits<unsigned>::max() / 2 &&
           "index (intermediate) result too big");
    Offset = Offset.sext(2 * BitWidth);
    sumOffsets(Offset, Addend, BinOpKind, AddendIsRight);
    return;
  }

  Offset = ResOffset;
}

namespace {

// This is a wrapper class around StringLiteral to support offsetted string
// literals as format strings. It takes the offset into account when returning
// the string and its length or the source locations to display notes correctly.
class FormatStringLiteral {
  const StringLiteral *FExpr;
  int64_t Offset;

 public:
  FormatStringLiteral(const StringLiteral *fexpr, int64_t Offset = 0)
      : FExpr(fexpr), Offset(Offset) {}

  StringRef getString() const {
    return FExpr->getString().drop_front(Offset);
  }

  unsigned getByteLength() const {
    return FExpr->getByteLength() - getCharByteWidth() * Offset;
  }

  unsigned getLength() const { return FExpr->getLength() - Offset; }
  unsigned getCharByteWidth() const { return FExpr->getCharByteWidth(); }

  StringLiteral::StringKind getKind() const { return FExpr->getKind(); }

  QualType getType() const { return FExpr->getType(); }

  bool isAscii() const { return FExpr->isAscii(); }
  bool isWide() const { return FExpr->isWide(); }
  bool isUTF8() const { return FExpr->isUTF8(); }
  bool isUTF16() const { return FExpr->isUTF16(); }
  bool isUTF32() const { return FExpr->isUTF32(); }
  bool isPascal() const { return FExpr->isPascal(); }

  SourceLocation getLocationOfByte(
      unsigned ByteNo, const SourceManager &SM, const LangOptions &Features,
      const TargetInfo &Target, unsigned *StartToken = nullptr,
      unsigned *StartTokenByteOffset = nullptr) const {
    return FExpr->getLocationOfByte(ByteNo + Offset, SM, Features, Target,
                                    StartToken, StartTokenByteOffset);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return FExpr->getBeginLoc().getLocWithOffset(Offset);
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return FExpr->getEndLoc(); }
};

}  // namespace

static void CheckFormatString(Sema &S, const FormatStringLiteral *FExpr,
                              const Expr *OrigFormatExpr,
                              ArrayRef<const Expr *> Args,
                              bool HasVAListArg, unsigned format_idx,
                              unsigned firstDataArg,
                              Sema::FormatStringType Type,
                              bool inFunctionCall,
                              Sema::VariadicCallType CallType,
                              llvm::SmallBitVector &CheckedVarArgs,
                              UncoveredArgHandler &UncoveredArg);

// Determine if an expression is a string literal or constant string.
// If this function returns false on the arguments to a function expecting a
// format string, we will usually need to emit a warning.
// True string literals are then checked by CheckFormatString.
static StringLiteralCheckType
checkFormatStringExpr(Sema &S, const Expr *E, ArrayRef<const Expr *> Args,
                      bool HasVAListArg, unsigned format_idx,
                      unsigned firstDataArg, Sema::FormatStringType Type,
                      Sema::VariadicCallType CallType, bool InFunctionCall,
                      llvm::SmallBitVector &CheckedVarArgs,
                      UncoveredArgHandler &UncoveredArg,
                      llvm::APSInt Offset) {
 tryAgain:
  assert(Offset.isSigned() && "invalid offset");

  if (E->isTypeDependent() || E->isValueDependent())
    return SLCT_NotALiteral;

  E = E->IgnoreParenCasts();

  if (E->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull))
    // Technically -Wformat-nonliteral does not warn about this case.
    // The behavior of printf and friends in this case is implementation
    // dependent.  Ideally if the format string cannot be null then
    // it should have a 'nonnull' attribute in the function prototype.
    return SLCT_UncheckedLiteral;

  switch (E->getStmtClass()) {
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::ConditionalOperatorClass: {
    // The expression is a literal if both sub-expressions were, and it was
    // completely checked only if both sub-expressions were checked.
    const AbstractConditionalOperator *C =
        cast<AbstractConditionalOperator>(E);

    // Determine whether it is necessary to check both sub-expressions, for
    // example, because the condition expression is a constant that can be
    // evaluated at compile time.
    bool CheckLeft = true, CheckRight = true;

    bool Cond;
    if (C->getCond()->EvaluateAsBooleanCondition(Cond, S.getASTContext())) {
      if (Cond)
        CheckRight = false;
      else
        CheckLeft = false;
    }

    // We need to maintain the offsets for the right and the left hand side
    // separately to check if every possible indexed expression is a valid
    // string literal. They might have different offsets for different string
    // literals in the end.
    StringLiteralCheckType Left;
    if (!CheckLeft)
      Left = SLCT_UncheckedLiteral;
    else {
      Left = checkFormatStringExpr(S, C->getTrueExpr(), Args,
                                   HasVAListArg, format_idx, firstDataArg,
                                   Type, CallType, InFunctionCall,
                                   CheckedVarArgs, UncoveredArg, Offset);
      if (Left == SLCT_NotALiteral || !CheckRight) {
        return Left;
      }
    }

    StringLiteralCheckType Right =
        checkFormatStringExpr(S, C->getFalseExpr(), Args,
                              HasVAListArg, format_idx, firstDataArg,
                              Type, CallType, InFunctionCall, CheckedVarArgs,
                              UncoveredArg, Offset);

    return (CheckLeft && Left < Right) ? Left : Right;
  }

  case Stmt::ImplicitCastExprClass:
    E = cast<ImplicitCastExpr>(E)->getSubExpr();
    goto tryAgain;

  case Stmt::OpaqueValueExprClass:
    if (const Expr *src = cast<OpaqueValueExpr>(E)->getSourceExpr()) {
      E = src;
      goto tryAgain;
    }
    return SLCT_NotALiteral;

  case Stmt::PredefinedExprClass:
    // While __func__, etc., are technically not string literals, they
    // cannot contain format specifiers and thus are not a security
    // liability.
    return SLCT_UncheckedLiteral;

  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DR = cast<DeclRefExpr>(E);

    // As an exception, do not flag errors for variables binding to
    // const string literals.
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
      bool isConstant = false;
      QualType T = DR->getType();

      if (const ArrayType *AT = S.Context.getAsArrayType(T)) {
        isConstant = AT->getElementType().isConstant(S.Context);
      } else if (const PointerType *PT = T->getAs<PointerType>()) {
        isConstant = T.isConstant(S.Context) &&
                     PT->getPointeeType().isConstant(S.Context);
      } else if (T->isObjCObjectPointerType()) {
        // In ObjC, there is usually no "const ObjectPointer" type,
        // so don't check if the pointee type is constant.
        isConstant = T.isConstant(S.Context);
      }

      if (isConstant) {
        if (const Expr *Init = VD->getAnyInitializer()) {
          // Look through initializers like const char c[] = { "foo" }
          if (const InitListExpr *InitList = dyn_cast<InitListExpr>(Init)) {
            if (InitList->isStringLiteralInit())
              Init = InitList->getInit(0)->IgnoreParenImpCasts();
          }
          return checkFormatStringExpr(S, Init, Args,
                                       HasVAListArg, format_idx,
                                       firstDataArg, Type, CallType,
                                       /*InFunctionCall*/ false, CheckedVarArgs,
                                       UncoveredArg, Offset);
        }
      }

      // For vprintf* functions (i.e., HasVAListArg==true), we add a
      // special check to see if the format string is a function parameter
      // of the function calling the printf function.  If the function
      // has an attribute indicating it is a printf-like function, then we
      // should suppress warnings concerning non-literals being used in a call
      // to a vprintf function.  For example:
      //
      // void
      // logmessage(char const *fmt __attribute__ (format (printf, 1, 2)), ...){
      //      va_list ap;
      //      va_start(ap, fmt);
      //      vprintf(fmt, ap);  // Do NOT emit a warning about "fmt".
      //      ...
      // }
      if (HasVAListArg) {
        if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(VD)) {
          if (const NamedDecl *ND = dyn_cast<NamedDecl>(PV->getDeclContext())) {
            int PVIndex = PV->getFunctionScopeIndex() + 1;
            for (const auto *PVFormat : ND->specific_attrs<FormatAttr>()) {
              // adjust for implicit parameter
              if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(ND))
                if (MD->isInstance())
                  ++PVIndex;
              // We also check if the formats are compatible.
              // We can't pass a 'scanf' string to a 'printf' function.
              if (PVIndex == PVFormat->getFormatIdx() &&
                  Type == S.GetFormatStringType(PVFormat))
                return SLCT_UncheckedLiteral;
            }
          }
        }
      }
    }

    return SLCT_NotALiteral;
  }

  case Stmt::CallExprClass:
  case Stmt::CXXMemberCallExprClass: {
    const CallExpr *CE = cast<CallExpr>(E);
    if (const NamedDecl *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl())) {
      bool IsFirst = true;
      StringLiteralCheckType CommonResult;
      for (const auto *FA : ND->specific_attrs<FormatArgAttr>()) {
        const Expr *Arg = CE->getArg(FA->getFormatIdx().getASTIndex());
        StringLiteralCheckType Result = checkFormatStringExpr(
            S, Arg, Args, HasVAListArg, format_idx, firstDataArg, Type,
            CallType, InFunctionCall, CheckedVarArgs, UncoveredArg, Offset);
        if (IsFirst) {
          CommonResult = Result;
          IsFirst = false;
        }
      }
      if (!IsFirst)
        return CommonResult;

      if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
        unsigned BuiltinID = FD->getBuiltinID();
        if (BuiltinID == Builtin::BI__builtin___CFStringMakeConstantString ||
            BuiltinID == Builtin::BI__builtin___NSStringMakeConstantString) {
          const Expr *Arg = CE->getArg(0);
          return checkFormatStringExpr(S, Arg, Args,
                                       HasVAListArg, format_idx,
                                       firstDataArg, Type, CallType,
                                       InFunctionCall, CheckedVarArgs,
                                       UncoveredArg, Offset);
        }
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::ObjCMessageExprClass: {
    const auto *ME = cast<ObjCMessageExpr>(E);
    if (const auto *ND = ME->getMethodDecl()) {
      if (const auto *FA = ND->getAttr<FormatArgAttr>()) {
        const Expr *Arg = ME->getArg(FA->getFormatIdx().getASTIndex());
        return checkFormatStringExpr(
            S, Arg, Args, HasVAListArg, format_idx, firstDataArg, Type,
            CallType, InFunctionCall, CheckedVarArgs, UncoveredArg, Offset);
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::ObjCStringLiteralClass:
  case Stmt::StringLiteralClass: {
    const StringLiteral *StrE = nullptr;

    if (const ObjCStringLiteral *ObjCFExpr = dyn_cast<ObjCStringLiteral>(E))
      StrE = ObjCFExpr->getString();
    else
      StrE = cast<StringLiteral>(E);

    if (StrE) {
      if (Offset.isNegative() || Offset > StrE->getLength()) {
        // TODO: It would be better to have an explicit warning for out of
        // bounds literals.
        return SLCT_NotALiteral;
      }
      FormatStringLiteral FStr(StrE, Offset.sextOrTrunc(64).getSExtValue());
      CheckFormatString(S, &FStr, E, Args, HasVAListArg, format_idx,
                        firstDataArg, Type, InFunctionCall, CallType,
                        CheckedVarArgs, UncoveredArg);
      return SLCT_CheckedLiteral;
    }

    return SLCT_NotALiteral;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BinOp = cast<BinaryOperator>(E);

    // A string literal + an int offset is still a string literal.
    if (BinOp->isAdditiveOp()) {
      Expr::EvalResult LResult, RResult;

      bool LIsInt = BinOp->getLHS()->EvaluateAsInt(LResult, S.Context);
      bool RIsInt = BinOp->getRHS()->EvaluateAsInt(RResult, S.Context);

      if (LIsInt != RIsInt) {
        BinaryOperatorKind BinOpKind = BinOp->getOpcode();

        if (LIsInt) {
          if (BinOpKind == BO_Add) {
            sumOffsets(Offset, LResult.Val.getInt(), BinOpKind, RIsInt);
            E = BinOp->getRHS();
            goto tryAgain;
          }
        } else {
          sumOffsets(Offset, RResult.Val.getInt(), BinOpKind, RIsInt);
          E = BinOp->getLHS();
          goto tryAgain;
        }
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UnaOp = cast<UnaryOperator>(E);
    auto ASE = dyn_cast<ArraySubscriptExpr>(UnaOp->getSubExpr());
    if (UnaOp->getOpcode() == UO_AddrOf && ASE) {
      Expr::EvalResult IndexResult;
      if (ASE->getRHS()->EvaluateAsInt(IndexResult, S.Context)) {
        sumOffsets(Offset, IndexResult.Val.getInt(), BO_Add,
                   /*RHS is int*/ true);
        E = ASE->getBase();
        goto tryAgain;
      }
    }

    return SLCT_NotALiteral;
  }

  default:
    return SLCT_NotALiteral;
  }
}

Sema::FormatStringType Sema::GetFormatStringType(const FormatAttr *Format) {
  return llvm::StringSwitch<FormatStringType>(Format->getType()->getName())
      .Case("scanf", FST_Scanf)
      .Cases("printf", "printf0", FST_Printf)
      .Cases("NSString", "CFString", FST_NSString)
      .Case("strftime", FST_Strftime)
      .Case("strfmon", FST_Strfmon)
      .Cases("kprintf", "cmn_err", "vcmn_err", "zcmn_err", FST_Kprintf)
      .Case("freebsd_kprintf", FST_FreeBSDKPrintf)
      .Case("os_trace", FST_OSLog)
      .Case("os_log", FST_OSLog)
      .Default(FST_Unknown);
}

/// CheckFormatArguments - Check calls to printf and scanf (and similar
/// functions) for correct use of format strings.
/// Returns true if a format string has been fully checked.
bool Sema::CheckFormatArguments(const FormatAttr *Format,
                                ArrayRef<const Expr *> Args,
                                bool IsCXXMember,
                                VariadicCallType CallType,
                                SourceLocation Loc, SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  FormatStringInfo FSI;
  if (getFormatStringInfo(Format, IsCXXMember, &FSI))
    return CheckFormatArguments(Args, FSI.HasVAListArg, FSI.FormatIdx,
                                FSI.FirstDataArg, GetFormatStringType(Format),
                                CallType, Loc, Range, CheckedVarArgs);
  return false;
}

bool Sema::CheckFormatArguments(ArrayRef<const Expr *> Args,
                                bool HasVAListArg, unsigned format_idx,
                                unsigned firstDataArg, FormatStringType Type,
                                VariadicCallType CallType,
                                SourceLocation Loc, SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  // CHECK: printf/scanf-like function is called with no format string.
  if (format_idx >= Args.size()) {
    Diag(Loc, diag::warn_missing_format_string) << Range;
    return false;
  }

  const Expr *OrigFormatExpr = Args[format_idx]->IgnoreParenCasts();

  // CHECK: format string is not a string literal.
  //
  // Dynamically generated format strings are difficult to
  // automatically vet at compile time.  Requiring that format strings
  // are string literals: (1) permits the checking of format strings by
  // the compiler and thereby (2) can practically remove the source of
  // many format string exploits.

  // Format string can be either ObjC string (e.g. @"%d") or
  // C string (e.g. "%d")
  // ObjC string uses the same format specifiers as C string, so we can use
  // the same format string checking logic for both ObjC and C strings.
  UncoveredArgHandler UncoveredArg;
  StringLiteralCheckType CT =
      checkFormatStringExpr(*this, OrigFormatExpr, Args, HasVAListArg,
                            format_idx, firstDataArg, Type, CallType,
                            /*IsFunctionCall*/ true, CheckedVarArgs,
                            UncoveredArg,
                            /*no string offset*/ llvm::APSInt(64, false) = 0);

  // Generate a diagnostic where an uncovered argument is detected.
  if (UncoveredArg.hasUncoveredArg()) {
    unsigned ArgIdx = UncoveredArg.getUncoveredArg() + firstDataArg;
    assert(ArgIdx < Args.size() && "ArgIdx outside bounds");
    UncoveredArg.Diagnose(*this, /*IsFunctionCall*/true, Args[ArgIdx]);
  }

  if (CT != SLCT_NotALiteral)
    // Literal format string found, check done!
    return CT == SLCT_CheckedLiteral;

  // Strftime is particular as it always uses a single 'time' argument,
  // so it is safe to pass a non-literal string.
  if (Type == FST_Strftime)
    return false;

  // Do not emit diag when the string param is a macro expansion and the
  // format is either NSString or CFString. This is a hack to prevent
  // diag when using the NSLocalizedString and CFCopyLocalizedString macros
  // which are usually used in place of NS and CF string literals.
  SourceLocation FormatLoc = Args[format_idx]->getBeginLoc();
  if (Type == FST_NSString && SourceMgr.isInSystemMacro(FormatLoc))
    return false;

  // If there are no arguments specified, warn with -Wformat-security, otherwise
  // warn only with -Wformat-nonliteral.
  if (Args.size() == firstDataArg) {
    Diag(FormatLoc, diag::warn_format_nonliteral_noargs)
      << OrigFormatExpr->getSourceRange();
    switch (Type) {
    default:
      break;
    case FST_Kprintf:
    case FST_FreeBSDKPrintf:
    case FST_Printf:
      Diag(FormatLoc, diag::note_format_security_fixit)
        << FixItHint::CreateInsertion(FormatLoc, "\"%s\", ");
      break;
    case FST_NSString:
      Diag(FormatLoc, diag::note_format_security_fixit)
        << FixItHint::CreateInsertion(FormatLoc, "@\"%@\", ");
      break;
    }
  } else {
    Diag(FormatLoc, diag::warn_format_nonliteral)
      << OrigFormatExpr->getSourceRange();
  }
  return false;
}

namespace {

class CheckFormatHandler : public analyze_format_string::FormatStringHandler {
protected:
  Sema &S;
  const FormatStringLiteral *FExpr;
  const Expr *OrigFormatExpr;
  const Sema::FormatStringType FSType;
  const unsigned FirstDataArg;
  const unsigned NumDataArgs;
  const char *Beg; // Start of format string.
  const bool HasVAListArg;
  ArrayRef<const Expr *> Args;
  unsigned FormatIdx;
  llvm::SmallBitVector CoveredArgs;
  bool usesPositionalArgs = false;
  bool atFirstArg = true;
  bool inFunctionCall;
  Sema::VariadicCallType CallType;
  llvm::SmallBitVector &CheckedVarArgs;
  UncoveredArgHandler &UncoveredArg;

public:
  CheckFormatHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, const char *beg, bool hasVAListArg,
                     ArrayRef<const Expr *> Args, unsigned formatIdx,
                     bool inFunctionCall, Sema::VariadicCallType callType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : S(s), FExpr(fexpr), OrigFormatExpr(origFormatExpr), FSType(type),
        FirstDataArg(firstDataArg), NumDataArgs(numDataArgs), Beg(beg),
        HasVAListArg(hasVAListArg), Args(Args), FormatIdx(formatIdx),
        inFunctionCall(inFunctionCall), CallType(callType),
        CheckedVarArgs(CheckedVarArgs), UncoveredArg(UncoveredArg) {
    CoveredArgs.resize(numDataArgs);
    CoveredArgs.reset();
  }

  void DoneProcessing();

  void HandleIncompleteSpecifier(const char *startSpecifier,
                                 unsigned specifierLen) override;

  void HandleInvalidLengthModifier(
                           const analyze_format_string::FormatSpecifier &FS,
                           const analyze_format_string::ConversionSpecifier &CS,
                           const char *startSpecifier, unsigned specifierLen,
                           unsigned DiagID);

  void HandleNonStandardLengthModifier(
                    const analyze_format_string::FormatSpecifier &FS,
                    const char *startSpecifier, unsigned specifierLen);

  void HandleNonStandardConversionSpecifier(
                    const analyze_format_string::ConversionSpecifier &CS,
                    const char *startSpecifier, unsigned specifierLen);

  void HandlePosition(const char *startPos, unsigned posLen) override;

  void HandleInvalidPosition(const char *startSpecifier,
                             unsigned specifierLen,
                             analyze_format_string::PositionContext p) override;

  void HandleZeroPosition(const char *startPos, unsigned posLen) override;

  void HandleNullChar(const char *nullCharacter) override;

  template <typename Range>
  static void
  EmitFormatDiagnostic(Sema &S, bool inFunctionCall, const Expr *ArgumentExpr,
                       const PartialDiagnostic &PDiag, SourceLocation StringLoc,
                       bool IsStringLocation, Range StringRange,
                       ArrayRef<FixItHint> Fixit = None);

protected:
  bool HandleInvalidConversionSpecifier(unsigned argIndex, SourceLocation Loc,
                                        const char *startSpec,
                                        unsigned specifierLen,
                                        const char *csStart, unsigned csLen);

  void HandlePositionalNonpositionalArgs(SourceLocation Loc,
                                         const char *startSpec,
                                         unsigned specifierLen);

  SourceRange getFormatStringRange();
  CharSourceRange getSpecifierRange(const char *startSpecifier,
                                    unsigned specifierLen);
  SourceLocation getLocationOfByte(const char *x);

  const Expr *getDataArg(unsigned i) const;

  bool CheckNumArgs(const analyze_format_string::FormatSpecifier &FS,
                    const analyze_format_string::ConversionSpecifier &CS,
                    const char *startSpecifier, unsigned specifierLen,
                    unsigned argIndex);

  template <typename Range>
  void EmitFormatDiagnostic(PartialDiagnostic PDiag, SourceLocation StringLoc,
                            bool IsStringLocation, Range StringRange,
                            ArrayRef<FixItHint> Fixit = None);
};

} // namespace

SourceRange CheckFormatHandler::getFormatStringRange() {
  return OrigFormatExpr->getSourceRange();
}

CharSourceRange CheckFormatHandler::
getSpecifierRange(const char *startSpecifier, unsigned specifierLen) {
  SourceLocation Start = getLocationOfByte(startSpecifier);
  SourceLocation End   = getLocationOfByte(startSpecifier + specifierLen - 1);

  // Advance the end SourceLocation by one due to half-open ranges.
  End = End.getLocWithOffset(1);

  return CharSourceRange::getCharRange(Start, End);
}

SourceLocation CheckFormatHandler::getLocationOfByte(const char *x) {
  return FExpr->getLocationOfByte(x - Beg, S.getSourceManager(),
                                  S.getLangOpts(), S.Context.getTargetInfo());
}

void CheckFormatHandler::HandleIncompleteSpecifier(const char *startSpecifier,
                                                   unsigned specifierLen){
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_incomplete_specifier),
                       getLocationOfByte(startSpecifier),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen));
}

void CheckFormatHandler::HandleInvalidLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen, unsigned DiagID) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  Optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    EmitFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
      << FixedLM->toString()
      << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    FixItHint Hint;
    if (DiagID == diag::warn_format_nonsensical_length)
      Hint = FixItHint::CreateRemoval(LMRange);

    EmitFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen),
                         Hint);
  }
}

void CheckFormatHandler::HandleNonStandardLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  Optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                           << LM.toString() << 0,
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
      << FixedLM->toString()
      << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                           << LM.toString() << 0,
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandleNonStandardConversionSpecifier(
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  // See if we know how to fix this conversion specifier.
  Optional<ConversionSpecifier> FixedCS = CS.getStandardSpecifier();
  if (FixedCS) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                          << CS.toString() << /*conversion specifier*/1,
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    CharSourceRange CSRange = getSpecifierRange(CS.getStart(), CS.getLength());
    S.Diag(getLocationOfByte(CS.getStart()), diag::note_format_fix_specifier)
      << FixedCS->toString()
      << FixItHint::CreateReplacement(CSRange, FixedCS->toString());
  } else {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                          << CS.toString() << /*conversion specifier*/1,
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandlePosition(const char *startPos,
                                        unsigned posLen) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard_positional_arg),
                               getLocationOfByte(startPos),
                               /*IsStringLocation*/true,
                               getSpecifierRange(startPos, posLen));
}

void
CheckFormatHandler::HandleInvalidPosition(const char *startPos, unsigned posLen,
                                     analyze_format_string::PositionContext p) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_format_invalid_positional_specifier)
                         << (unsigned) p,
                       getLocationOfByte(startPos), /*IsStringLocation*/true,
                       getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleZeroPosition(const char *startPos,
                                            unsigned posLen) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_format_zero_positional_specifier),
                               getLocationOfByte(startPos),
                               /*IsStringLocation*/true,
                               getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleNullChar(const char *nullCharacter) {
  if (!isa<ObjCStringLiteral>(OrigFormatExpr)) {
    // The presence of a null character is likely an error.
    EmitFormatDiagnostic(
      S.PDiag(diag::warn_printf_format_string_contains_null_char),
      getLocationOfByte(nullCharacter), /*IsStringLocation*/true,
      getFormatStringRange());
  }
}

// Note that this may return NULL if there was an error parsing or building
// one of the argument expressions.
const Expr *CheckFormatHandler::getDataArg(unsigned i) const {
  return Args[FirstDataArg + i];
}

void CheckFormatHandler::DoneProcessing() {
  // Does the number of data arguments exceed the number of
  // format conversions in the format string?
  if (!HasVAListArg) {
      // Find any arguments that weren't covered.
    CoveredArgs.flip();
    signed notCoveredArg = CoveredArgs.find_first();
    if (notCoveredArg >= 0) {
      assert((unsigned)notCoveredArg < NumDataArgs);
      UncoveredArg.Update(notCoveredArg, OrigFormatExpr);
    } else {
      UncoveredArg.setAllCovered();
    }
  }
}

void UncoveredArgHandler::Diagnose(Sema &S, bool IsFunctionCall,
                                   const Expr *ArgExpr) {
  assert(hasUncoveredArg() && DiagnosticExprs.size() > 0 &&
         "Invalid state");

  if (!ArgExpr)
    return;

  SourceLocation Loc = ArgExpr->getBeginLoc();

  if (S.getSourceManager().isInSystemMacro(Loc))
    return;

  PartialDiagnostic PDiag = S.PDiag(diag::warn_printf_data_arg_not_used);
  for (auto E : DiagnosticExprs)
    PDiag << E->getSourceRange();

  CheckFormatHandler::EmitFormatDiagnostic(
                                  S, IsFunctionCall, DiagnosticExprs[0],
                                  PDiag, Loc, /*IsStringLocation*/false,
                                  DiagnosticExprs[0]->getSourceRange());
}

bool
CheckFormatHandler::HandleInvalidConversionSpecifier(unsigned argIndex,
                                                     SourceLocation Loc,
                                                     const char *startSpec,
                                                     unsigned specifierLen,
                                                     const char *csStart,
                                                     unsigned csLen) {
  bool keepGoing = true;
  if (argIndex < NumDataArgs) {
    // Consider the argument coverered, even though the specifier doesn't
    // make sense.
    CoveredArgs.set(argIndex);
  }
  else {
    // If argIndex exceeds the number of data arguments we
    // don't issue a warning because that is just a cascade of warnings (and
    // they may have intended '%%' anyway). We don't want to continue processing
    // the format string after this point, however, as we will like just get
    // gibberish when trying to match arguments.
    keepGoing = false;
  }

  StringRef Specifier(csStart, csLen);

  // If the specifier in non-printable, it could be the first byte of a UTF-8
  // sequence. In that case, print the UTF-8 code point. If not, print the byte
  // hex value.
  std::string CodePointStr;
  if (!llvm::sys::locale::isPrint(*csStart)) {
    llvm::UTF32 CodePoint;
    const llvm::UTF8 **B = reinterpret_cast<const llvm::UTF8 **>(&csStart);
    const llvm::UTF8 *E =
        reinterpret_cast<const llvm::UTF8 *>(csStart + csLen);
    llvm::ConversionResult Result =
        llvm::convertUTF8Sequence(B, E, &CodePoint, llvm::strictConversion);

    if (Result != llvm::conversionOK) {
      unsigned char FirstChar = *csStart;
      CodePoint = (llvm::UTF32)FirstChar;
    }

    llvm::raw_string_ostream OS(CodePointStr);
    if (CodePoint < 256)
      OS << "\\x" << llvm::format("%02x", CodePoint);
    else if (CodePoint <= 0xFFFF)
      OS << "\\u" << llvm::format("%04x", CodePoint);
    else
      OS << "\\U" << llvm::format("%08x", CodePoint);
    OS.flush();
    Specifier = CodePointStr;
  }

  EmitFormatDiagnostic(
      S.PDiag(diag::warn_format_invalid_conversion) << Specifier, Loc,
      /*IsStringLocation*/ true, getSpecifierRange(startSpec, specifierLen));

  return keepGoing;
}

void
CheckFormatHandler::HandlePositionalNonpositionalArgs(SourceLocation Loc,
                                                      const char *startSpec,
                                                      unsigned specifierLen) {
  EmitFormatDiagnostic(
    S.PDiag(diag::warn_format_mix_positional_nonpositional_args),
    Loc, /*isStringLoc*/true, getSpecifierRange(startSpec, specifierLen));
}

bool
CheckFormatHandler::CheckNumArgs(
  const analyze_format_string::FormatSpecifier &FS,
  const analyze_format_string::ConversionSpecifier &CS,
  const char *startSpecifier, unsigned specifierLen, unsigned argIndex) {

  if (argIndex >= NumDataArgs) {
    PartialDiagnostic PDiag = FS.usesPositionalArg()
      ? (S.PDiag(diag::warn_printf_positional_arg_exceeds_data_args)
           << (argIndex+1) << NumDataArgs)
      : S.PDiag(diag::warn_printf_insufficient_data_args);
    EmitFormatDiagnostic(
      PDiag, getLocationOfByte(CS.getStart()), /*IsStringLocation*/true,
      getSpecifierRange(startSpecifier, specifierLen));

    // Since more arguments than conversion tokens are given, by extension
    // all arguments are covered, so mark this as so.
    UncoveredArg.setAllCovered();
    return false;
  }
  return true;
}

template<typename Range>
void CheckFormatHandler::EmitFormatDiagnostic(PartialDiagnostic PDiag,
                                              SourceLocation Loc,
                                              bool IsStringLocation,
                                              Range StringRange,
                                              ArrayRef<FixItHint> FixIt) {
  EmitFormatDiagnostic(S, inFunctionCall, Args[FormatIdx], PDiag,
                       Loc, IsStringLocation, StringRange, FixIt);
}

/// If the format string is not within the function call, emit a note
/// so that the function call and string are in diagnostic messages.
///
/// \param InFunctionCall if true, the format string is within the function
/// call and only one diagnostic message will be produced.  Otherwise, an
/// extra note will be emitted pointing to location of the format string.
///
/// \param ArgumentExpr the expression that is passed as the format string
/// argument in the function call.  Used for getting locations when two
/// diagnostics are emitted.
///
/// \param PDiag the callee should already have provided any strings for the
/// diagnostic message.  This function only adds locations and fixits
/// to diagnostics.
///
/// \param Loc primary location for diagnostic.  If two diagnostics are
/// required, one will be at Loc and a new SourceLocation will be created for
/// the other one.
///
/// \param IsStringLocation if true, Loc points to the format string should be
/// used for the note.  Otherwise, Loc points to the argument list and will
/// be used with PDiag.
///
/// \param StringRange some or all of the string to highlight.  This is
/// templated so it can accept either a CharSourceRange or a SourceRange.
///
/// \param FixIt optional fix it hint for the format string.
template <typename Range>
void CheckFormatHandler::EmitFormatDiagnostic(
    Sema &S, bool InFunctionCall, const Expr *ArgumentExpr,
    const PartialDiagnostic &PDiag, SourceLocation Loc, bool IsStringLocation,
    Range StringRange, ArrayRef<FixItHint> FixIt) {
  if (InFunctionCall) {
    const Sema::SemaDiagnosticBuilder &D = S.Diag(Loc, PDiag);
    D << StringRange;
    D << FixIt;
  } else {
    S.Diag(IsStringLocation ? ArgumentExpr->getExprLoc() : Loc, PDiag)
      << ArgumentExpr->getSourceRange();

    const Sema::SemaDiagnosticBuilder &Note =
      S.Diag(IsStringLocation ? Loc : StringRange.getBegin(),
             diag::note_format_string_defined);

    Note << StringRange;
    Note << FixIt;
  }
}

//===--- CHECK: Printf format string checking ------------------------------===//

namespace {

class CheckPrintfHandler : public CheckFormatHandler {
public:
  CheckPrintfHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, bool isObjC, const char *beg,
                     bool hasVAListArg, ArrayRef<const Expr *> Args,
                     unsigned formatIdx, bool inFunctionCall,
                     Sema::VariadicCallType CallType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, hasVAListArg, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool isObjCContext() const { return FSType == Sema::FST_NSString; }

  /// Returns true if '%@' specifiers are allowed in the format string.
  bool allowsObjCArg() const {
    return FSType == Sema::FST_NSString || FSType == Sema::FST_OSLog ||
           FSType == Sema::FST_OSTrace;
  }

  bool HandleInvalidPrintfConversionSpecifier(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const char *startSpecifier,
                                      unsigned specifierLen) override;

  void handleInvalidMaskType(StringRef MaskType) override;

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *startSpecifier,
                             unsigned specifierLen) override;
  bool checkFormatExpr(const analyze_printf::PrintfSpecifier &FS,
                       const char *StartSpecifier,
                       unsigned SpecifierLen,
                       const Expr *E);

  bool HandleAmount(const analyze_format_string::OptionalAmount &Amt, unsigned k,
                    const char *startSpecifier, unsigned specifierLen);
  void HandleInvalidAmount(const analyze_printf::PrintfSpecifier &FS,
                           const analyze_printf::OptionalAmount &Amt,
                           unsigned type,
                           const char *startSpecifier, unsigned specifierLen);
  void HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                  const analyze_printf::OptionalFlag &flag,
                  const char *startSpecifier, unsigned specifierLen);
  void HandleIgnoredFlag(const analyze_printf::PrintfSpecifier &FS,
                         const analyze_printf::OptionalFlag &ignoredFlag,
                         const analyze_printf::OptionalFlag &flag,
                         const char *startSpecifier, unsigned specifierLen);
  bool checkForCStrMembers(const analyze_printf::ArgType &AT,
                           const Expr *E);

  void HandleEmptyObjCModifierFlag(const char *startFlag,
                                   unsigned flagLen) override;

  void HandleInvalidObjCModifierFlag(const char *startFlag,
                                            unsigned flagLen) override;

  void HandleObjCFlagsWithNonObjCConversion(const char *flagsStart,
                                           const char *flagsEnd,
                                           const char *conversionPosition)
                                             override;
};

} // namespace

bool CheckPrintfHandler::HandleInvalidPrintfConversionSpecifier(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const char *startSpecifier,
                                      unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(FS.getArgIndex(),
                                          getLocationOfByte(CS.getStart()),
                                          startSpecifier, specifierLen,
                                          CS.getStart(), CS.getLength());
}

void CheckPrintfHandler::handleInvalidMaskType(StringRef MaskType) {
  S.Diag(getLocationOfByte(MaskType.data()), diag::err_invalid_mask_type_size);
}

bool CheckPrintfHandler::HandleAmount(
                               const analyze_format_string::OptionalAmount &Amt,
                               unsigned k, const char *startSpecifier,
                               unsigned specifierLen) {
  if (Amt.hasDataArgument()) {
    if (!HasVAListArg) {
      unsigned argIndex = Amt.getArgIndex();
      if (argIndex >= NumDataArgs) {
        EmitFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_missing_arg)
                               << k,
                             getLocationOfByte(Amt.getStart()),
                             /*IsStringLocation*/true,
                             getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }

      // Type check the data argument.  It should be an 'int'.
      // Although not in conformance with C99, we also allow the argument to be
      // an 'unsigned int' as that is a reasonably safe case.  GCC also
      // doesn't emit a warning for that case.
      CoveredArgs.set(argIndex);
      const Expr *Arg = getDataArg(argIndex);
      if (!Arg)
        return false;

      QualType T = Arg->getType();

      const analyze_printf::ArgType &AT = Amt.getArgType(S.Context);
      assert(AT.isValid());

      if (!AT.matchesType(S.Context, T)) {
        EmitFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_wrong_type)
                               << k << AT.getRepresentativeTypeName(S.Context)
                               << T << Arg->getSourceRange(),
                             getLocationOfByte(Amt.getStart()),
                             /*IsStringLocation*/true,
                             getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }
    }
  }
  return true;
}

void CheckPrintfHandler::HandleInvalidAmount(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const analyze_printf::OptionalAmount &Amt,
                                      unsigned type,
                                      const char *startSpecifier,
                                      unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  FixItHint fixit =
    Amt.getHowSpecified() == analyze_printf::OptionalAmount::Constant
      ? FixItHint::CreateRemoval(getSpecifierRange(Amt.getStart(),
                                 Amt.getConstantLength()))
      : FixItHint();

  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_nonsensical_optional_amount)
                         << type << CS.toString(),
                       getLocationOfByte(Amt.getStart()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       fixit);
}

void CheckPrintfHandler::HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                                    const analyze_printf::OptionalFlag &flag,
                                    const char *startSpecifier,
                                    unsigned specifierLen) {
  // Warn about pointless flag with a fixit removal.
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_nonsensical_flag)
                         << flag.toString() << CS.toString(),
                       getLocationOfByte(flag.getPosition()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       FixItHint::CreateRemoval(
                         getSpecifierRange(flag.getPosition(), 1)));
}

void CheckPrintfHandler::HandleIgnoredFlag(
                                const analyze_printf::PrintfSpecifier &FS,
                                const analyze_printf::OptionalFlag &ignoredFlag,
                                const analyze_printf::OptionalFlag &flag,
                                const char *startSpecifier,
                                unsigned specifierLen) {
  // Warn about ignored flag with a fixit removal.
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_ignored_flag)
                         << ignoredFlag.toString() << flag.toString(),
                       getLocationOfByte(ignoredFlag.getPosition()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       FixItHint::CreateRemoval(
                         getSpecifierRange(ignoredFlag.getPosition(), 1)));
}

void CheckPrintfHandler::HandleEmptyObjCModifierFlag(const char *startFlag,
                                                     unsigned flagLen) {
  // Warn about an empty flag.
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_empty_objc_flag),
                       getLocationOfByte(startFlag),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startFlag, flagLen));
}

void CheckPrintfHandler::HandleInvalidObjCModifierFlag(const char *startFlag,
                                                       unsigned flagLen) {
  // Warn about an invalid flag.
  auto Range = getSpecifierRange(startFlag, flagLen);
  StringRef flag(startFlag, flagLen);
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_invalid_objc_flag) << flag,
                      getLocationOfByte(startFlag),
                      /*IsStringLocation*/true,
                      Range, FixItHint::CreateRemoval(Range));
}

void CheckPrintfHandler::HandleObjCFlagsWithNonObjCConversion(
    const char *flagsStart, const char *flagsEnd, const char *conversionPosition) {
    // Warn about using '[...]' without a '@' conversion.
    auto Range = getSpecifierRange(flagsStart, flagsEnd - flagsStart + 1);
    auto diag = diag::warn_printf_ObjCflags_without_ObjCConversion;
    EmitFormatDiagnostic(S.PDiag(diag) << StringRef(conversionPosition, 1),
                         getLocationOfByte(conversionPosition),
                         /*IsStringLocation*/true,
                         Range, FixItHint::CreateRemoval(Range));
}

// Determines if the specified is a C++ class or struct containing
// a member with the specified name and kind (e.g. a CXXMethodDecl named
// "c_str()").
template<typename MemberKind>
static llvm::SmallPtrSet<MemberKind*, 1>
CXXRecordMembersNamed(StringRef Name, Sema &S, QualType Ty) {
  const RecordType *RT = Ty->getAs<RecordType>();
  llvm::SmallPtrSet<MemberKind*, 1> Results;

  if (!RT)
    return Results;
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!RD || !RD->getDefinition())
    return Results;

  LookupResult R(S, &S.Context.Idents.get(Name), SourceLocation(),
                 Sema::LookupMemberName);
  R.suppressDiagnostics();

  // We just need to include all members of the right kind turned up by the
  // filter, at this point.
  if (S.LookupQualifiedName(R, RT->getDecl()))
    for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
      NamedDecl *decl = (*I)->getUnderlyingDecl();
      if (MemberKind *FK = dyn_cast<MemberKind>(decl))
        Results.insert(FK);
    }
  return Results;
}

/// Check if we could call '.c_str()' on an object.
///
/// FIXME: This returns the wrong results in some cases (if cv-qualifiers don't
/// allow the call, or if it would be ambiguous).
bool Sema::hasCStrMethod(const Expr *E) {
  using MethodSet = llvm::SmallPtrSet<CXXMethodDecl *, 1>;

  MethodSet Results =
      CXXRecordMembersNamed<CXXMethodDecl>("c_str", *this, E->getType());
  for (MethodSet::iterator MI = Results.begin(), ME = Results.end();
       MI != ME; ++MI)
    if ((*MI)->getMinRequiredArguments() == 0)
      return true;
  return false;
}

// Check if a (w)string was passed when a (w)char* was needed, and offer a
// better diagnostic if so. AT is assumed to be valid.
// Returns true when a c_str() conversion method is found.
bool CheckPrintfHandler::checkForCStrMembers(
    const analyze_printf::ArgType &AT, const Expr *E) {
  using MethodSet = llvm::SmallPtrSet<CXXMethodDecl *, 1>;

  MethodSet Results =
      CXXRecordMembersNamed<CXXMethodDecl>("c_str", S, E->getType());

  for (MethodSet::iterator MI = Results.begin(), ME = Results.end();
       MI != ME; ++MI) {
    const CXXMethodDecl *Method = *MI;
    if (Method->getMinRequiredArguments() == 0 &&
        AT.matchesType(S.Context, Method->getReturnType())) {
      // FIXME: Suggest parens if the expression needs them.
      SourceLocation EndLoc = S.getLocForEndOfToken(E->getEndLoc());
      S.Diag(E->getBeginLoc(), diag::note_printf_c_str)
          << "c_str()" << FixItHint::CreateInsertion(EndLoc, ".c_str()");
      return true;
    }
  }

  return false;
}

bool
CheckPrintfHandler::HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier
                                            &FS,
                                          const char *startSpecifier,
                                          unsigned specifierLen) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  const PrintfConversionSpecifier &CS = FS.getConversionSpecifier();

  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
        atFirstArg = false;
        usesPositionalArgs = FS.usesPositionalArg();
    }
    else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  // First check if the field width, precision, and conversion specifier
  // have matching data arguments.
  if (!HandleAmount(FS.getFieldWidth(), /* field width */ 0,
                    startSpecifier, specifierLen)) {
    return false;
  }

  if (!HandleAmount(FS.getPrecision(), /* precision */ 1,
                    startSpecifier, specifierLen)) {
    return false;
  }

  if (!CS.consumesDataArgument()) {
    // FIXME: Technically specifying a precision or field width here
    // makes no sense.  Worth issuing a warning at some point.
    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
    // The check to see if the argIndex is valid will come later.
    // We set the bit here because we may exit early from this
    // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  // FreeBSD kernel extensions.
  if (CS.getKind() == ConversionSpecifier::FreeBSDbArg ||
      CS.getKind() == ConversionSpecifier::FreeBSDDArg) {
    // We need at least two arguments.
    if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex + 1))
      return false;

    // Claim the second argument.
    CoveredArgs.set(argIndex + 1);

    // Type check the first argument (int for %b, pointer for %D)
    const Expr *Ex = getDataArg(argIndex);
    const analyze_printf::ArgType &AT =
      (CS.getKind() == ConversionSpecifier::FreeBSDbArg) ?
        ArgType(S.Context.IntTy) : ArgType::CPointerTy;
    if (AT.isValid() && !AT.matchesType(S.Context, Ex->getType()))
      EmitFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
              << AT.getRepresentativeTypeName(S.Context) << Ex->getType()
              << false << Ex->getSourceRange(),
          Ex->getBeginLoc(), /*IsStringLocation*/ false,
          getSpecifierRange(startSpecifier, specifierLen));

    // Type check the second argument (char * for both %b and %D)
    Ex = getDataArg(argIndex + 1);
    const analyze_printf::ArgType &AT2 = ArgType::CStrTy;
    if (AT2.isValid() && !AT2.matchesType(S.Context, Ex->getType()))
      EmitFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
              << AT2.getRepresentativeTypeName(S.Context) << Ex->getType()
              << false << Ex->getSourceRange(),
          Ex->getBeginLoc(), /*IsStringLocation*/ false,
          getSpecifierRange(startSpecifier, specifierLen));

     return true;
  }

  // Check for using an Objective-C specific conversion specifier
  // in a non-ObjC literal.
  if (!allowsObjCArg() && CS.isObjCArg()) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // %P can only be used with os_log.
  if (FSType != Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::PArg) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // %n is not allowed with os_log.
  if (FSType == Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::nArg) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_os_log_format_narg),
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));

    return true;
  }

  // Only scalars are allowed for os_trace.
  if (FSType == Sema::FST_OSTrace &&
      (CS.getKind() == ConversionSpecifier::PArg ||
       CS.getKind() == ConversionSpecifier::sArg ||
       CS.getKind() == ConversionSpecifier::ObjCObjArg)) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // Check for use of public/private annotation outside of os_log().
  if (FSType != Sema::FST_OSLog) {
    if (FS.isPublic().isSet()) {
      EmitFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                               << "public",
                           getLocationOfByte(FS.isPublic().getPosition()),
                           /*IsStringLocation*/ false,
                           getSpecifierRange(startSpecifier, specifierLen));
    }
    if (FS.isPrivate().isSet()) {
      EmitFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                               << "private",
                           getLocationOfByte(FS.isPrivate().getPosition()),
                           /*IsStringLocation*/ false,
                           getSpecifierRange(startSpecifier, specifierLen));
    }
  }

  // Check for invalid use of field width
  if (!FS.hasValidFieldWidth()) {
    HandleInvalidAmount(FS, FS.getFieldWidth(), /* field width */ 0,
        startSpecifier, specifierLen);
  }

  // Check for invalid use of precision
  if (!FS.hasValidPrecision()) {
    HandleInvalidAmount(FS, FS.getPrecision(), /* precision */ 1,
        startSpecifier, specifierLen);
  }

  // Precision is mandatory for %P specifier.
  if (CS.getKind() == ConversionSpecifier::PArg &&
      FS.getPrecision().getHowSpecified() == OptionalAmount::NotSpecified) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_P_no_precision),
                         getLocationOfByte(startSpecifier),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
  }

  // Check each flag does not conflict with any other component.
  if (!FS.hasValidThousandsGroupingPrefix())
    HandleFlag(FS, FS.hasThousandsGrouping(), startSpecifier, specifierLen);
  if (!FS.hasValidLeadingZeros())
    HandleFlag(FS, FS.hasLeadingZeros(), startSpecifier, specifierLen);
  if (!FS.hasValidPlusPrefix())
    HandleFlag(FS, FS.hasPlusPrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidSpacePrefix())
    HandleFlag(FS, FS.hasSpacePrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidAlternativeForm())
    HandleFlag(FS, FS.hasAlternativeForm(), startSpecifier, specifierLen);
  if (!FS.hasValidLeftJustified())
    HandleFlag(FS, FS.isLeftJustified(), startSpecifier, specifierLen);

  // Check that flags are not ignored by another flag
  if (FS.hasSpacePrefix() && FS.hasPlusPrefix()) // ' ' ignored by '+'
    HandleIgnoredFlag(FS, FS.hasSpacePrefix(), FS.hasPlusPrefix(),
        startSpecifier, specifierLen);
  if (FS.hasLeadingZeros() && FS.isLeftJustified()) // '0' ignored by '-'
    HandleIgnoredFlag(FS, FS.hasLeadingZeros(), FS.isLeftJustified(),
            startSpecifier, specifierLen);

  // Check the length modifier is valid with the given conversion specifier.
  if (!FS.hasValidLengthModifier(S.getASTContext().getTargetInfo()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  // The remaining checks depend on the data arguments.
  if (HasVAListArg)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  const Expr *Arg = getDataArg(argIndex);
  if (!Arg)
    return true;

  return checkFormatExpr(FS, startSpecifier, specifierLen, Arg);
}

static bool requiresParensToAddCast(const Expr *E) {
  // FIXME: We should have a general way to reason about operator
  // precedence and whether parens are actually needed here.
  // Take care of a few common cases where they aren't.
  const Expr *Inside = E->IgnoreImpCasts();
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(Inside))
    Inside = POE->getSyntacticForm()->IgnoreImpCasts();

  switch (Inside->getStmtClass()) {
  case Stmt::ArraySubscriptExprClass:
  case Stmt::CallExprClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::DeclRefExprClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::MemberExprClass:
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCBoolLiteralExprClass:
  case Stmt::ObjCBoxedExprClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCEncodeExprClass:
  case Stmt::ObjCIvarRefExprClass:
  case Stmt::ObjCMessageExprClass:
  case Stmt::ObjCPropertyRefExprClass:
  case Stmt::ObjCStringLiteralClass:
  case Stmt::ObjCSubscriptRefExprClass:
  case Stmt::ParenExprClass:
  case Stmt::StringLiteralClass:
  case Stmt::UnaryOperatorClass:
    return false;
  default:
    return true;
  }
}

static std::pair<QualType, StringRef>
shouldNotPrintDirectly(const ASTContext &Context,
                       QualType IntendedTy,
                       const Expr *E) {
  // Use a 'while' to peel off layers of typedefs.
  QualType TyTy = IntendedTy;
  while (const TypedefType *UserTy = TyTy->getAs<TypedefType>()) {
    StringRef Name = UserTy->getDecl()->getName();
    QualType CastTy = llvm::StringSwitch<QualType>(Name)
      .Case("CFIndex", Context.getNSIntegerType())
      .Case("NSInteger", Context.getNSIntegerType())
      .Case("NSUInteger", Context.getNSUIntegerType())
      .Case("SInt32", Context.IntTy)
      .Case("UInt32", Context.UnsignedIntTy)
      .Default(QualType());

    if (!CastTy.isNull())
      return std::make_pair(CastTy, Name);

    TyTy = UserTy->desugar();
  }

  // Strip parens if necessary.
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E))
    return shouldNotPrintDirectly(Context,
                                  PE->getSubExpr()->getType(),
                                  PE->getSubExpr());

  // If this is a conditional expression, then its result type is constructed
  // via usual arithmetic conversions and thus there might be no necessary
  // typedef sugar there.  Recurse to operands to check for NSInteger &
  // Co. usage condition.
  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    QualType TrueTy, FalseTy;
    StringRef TrueName, FalseName;

    std::tie(TrueTy, TrueName) =
      shouldNotPrintDirectly(Context,
                             CO->getTrueExpr()->getType(),
                             CO->getTrueExpr());
    std::tie(FalseTy, FalseName) =
      shouldNotPrintDirectly(Context,
                             CO->getFalseExpr()->getType(),
                             CO->getFalseExpr());

    if (TrueTy == FalseTy)
      return std::make_pair(TrueTy, TrueName);
    else if (TrueTy.isNull())
      return std::make_pair(FalseTy, FalseName);
    else if (FalseTy.isNull())
      return std::make_pair(TrueTy, TrueName);
  }

  return std::make_pair(QualType(), StringRef());
}

/// Return true if \p ICE is an implicit argument promotion of an arithmetic
/// type. Bit-field 'promotions' from a higher ranked type to a lower ranked
/// type do not count.
static bool
isArithmeticArgumentPromotion(Sema &S, const ImplicitCastExpr *ICE) {
  QualType From = ICE->getSubExpr()->getType();
  QualType To = ICE->getType();
  // It's an integer promotion if the destination type is the promoted
  // source type.
  if (ICE->getCastKind() == CK_IntegralCast &&
      From->isPromotableIntegerType() &&
      S.Context.getPromotedIntegerType(From) == To)
    return true;
  // Look through vector types, since we do default argument promotion for
  // those in OpenCL.
  if (const auto *VecTy = From->getAs<ExtVectorType>())
    From = VecTy->getElementType();
  if (const auto *VecTy = To->getAs<ExtVectorType>())
    To = VecTy->getElementType();
  // It's a floating promotion if the source type is a lower rank.
  return ICE->getCastKind() == CK_FloatingCast &&
         S.Context.getFloatingTypeOrder(From, To) < 0;
}

bool
CheckPrintfHandler::checkFormatExpr(const analyze_printf::PrintfSpecifier &FS,
                                    const char *StartSpecifier,
                                    unsigned SpecifierLen,
                                    const Expr *E) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  // Now type check the data expression that matches the
  // format specifier.
  const analyze_printf::ArgType &AT = FS.getArgType(S.Context, isObjCContext());
  if (!AT.isValid())
    return true;

  QualType ExprTy = E->getType();
  while (const TypeOfExprType *TET = dyn_cast<TypeOfExprType>(ExprTy)) {
    ExprTy = TET->getUnderlyingExpr()->getType();
  }

  const analyze_printf::ArgType::MatchKind Match =
      AT.matchesType(S.Context, ExprTy);
  bool Pedantic = Match == analyze_printf::ArgType::NoMatchPedantic;
  if (Match == analyze_printf::ArgType::Match)
    return true;

  // Look through argument promotions for our error message's reported type.
  // This includes the integral and floating promotions, but excludes array
  // and function pointer decay (seeing that an argument intended to be a
  // string has type 'char [6]' is probably more confusing than 'char *') and
  // certain bitfield promotions (bitfields can be 'demoted' to a lesser type).
  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (isArithmeticArgumentPromotion(S, ICE)) {
      E = ICE->getSubExpr();
      ExprTy = E->getType();

      // Check if we didn't match because of an implicit cast from a 'char'
      // or 'short' to an 'int'.  This is done because printf is a varargs
      // function.
      if (ICE->getType() == S.Context.IntTy ||
          ICE->getType() == S.Context.UnsignedIntTy) {
        // All further checking is done on the subexpression.
        if (AT.matchesType(S.Context, ExprTy))
          return true;
      }
    }
  } else if (const CharacterLiteral *CL = dyn_cast<CharacterLiteral>(E)) {
    // Special case for 'a', which has type 'int' in C.
    // Note, however, that we do /not/ want to treat multibyte constants like
    // 'MooV' as characters! This form is deprecated but still exists.
    if (ExprTy == S.Context.IntTy)
      if (llvm::isUIntN(S.Context.getCharWidth(), CL->getValue()))
        ExprTy = S.Context.CharTy;
  }

  // Look through enums to their underlying type.
  bool IsEnum = false;
  if (auto EnumTy = ExprTy->getAs<EnumType>()) {
    ExprTy = EnumTy->getDecl()->getIntegerType();
    IsEnum = true;
  }

  // %C in an Objective-C context prints a unichar, not a wchar_t.
  // If the argument is an integer of some kind, believe the %C and suggest
  // a cast instead of changing the conversion specifier.
  QualType IntendedTy = ExprTy;
  if (isObjCContext() &&
      FS.getConversionSpecifier().getKind() == ConversionSpecifier::CArg) {
    if (ExprTy->isIntegralOrUnscopedEnumerationType() &&
        !ExprTy->isCharType()) {
      // 'unichar' is defined as a typedef of unsigned short, but we should
      // prefer using the typedef if it is visible.
      IntendedTy = S.Context.UnsignedShortTy;

      // While we are here, check if the value is an IntegerLiteral that happens
      // to be within the valid range.
      if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
        const llvm::APInt &V = IL->getValue();
        if (V.getActiveBits() <= S.Context.getTypeSize(IntendedTy))
          return true;
      }

      LookupResult Result(S, &S.Context.Idents.get("unichar"), E->getBeginLoc(),
                          Sema::LookupOrdinaryName);
      if (S.LookupName(Result, S.getCurScope())) {
        NamedDecl *ND = Result.getFoundDecl();
        if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(ND))
          if (TD->getUnderlyingType() == IntendedTy)
            IntendedTy = S.Context.getTypedefType(TD);
      }
    }
  }

  // Special-case some of Darwin's platform-independence types by suggesting
  // casts to primitive types that are known to be large enough.
  bool ShouldNotPrintDirectly = false; StringRef CastTyName;
  if (S.Context.getTargetInfo().getTriple().isOSDarwin()) {
    QualType CastTy;
    std::tie(CastTy, CastTyName) = shouldNotPrintDirectly(S.Context, IntendedTy, E);
    if (!CastTy.isNull()) {
      // %zi/%zu and %td/%tu are OK to use for NSInteger/NSUInteger of type int
      // (long in ASTContext). Only complain to pedants.
      if ((CastTyName == "NSInteger" || CastTyName == "NSUInteger") &&
          (AT.isSizeT() || AT.isPtrdiffT()) &&
          AT.matchesType(S.Context, CastTy))
        Pedantic = true;
      IntendedTy = CastTy;
      ShouldNotPrintDirectly = true;
    }
  }

  // We may be able to offer a FixItHint if it is a supported type.
  PrintfSpecifier fixedFS = FS;
  bool Success =
      fixedFS.fixType(IntendedTy, S.getLangOpts(), S.Context, isObjCContext());

  if (Success) {
    // Get the fix string from the fixed format specifier
    SmallString<16> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    CharSourceRange SpecRange = getSpecifierRange(StartSpecifier, SpecifierLen);

    if (IntendedTy == ExprTy && !ShouldNotPrintDirectly) {
      unsigned Diag =
          Pedantic
              ? diag::warn_format_conversion_argument_type_mismatch_pedantic
              : diag::warn_format_conversion_argument_type_mismatch;
      // In this case, the specifier is wrong and should be changed to match
      // the argument.
      EmitFormatDiagnostic(S.PDiag(Diag)
                               << AT.getRepresentativeTypeName(S.Context)
                               << IntendedTy << IsEnum << E->getSourceRange(),
                           E->getBeginLoc(),
                           /*IsStringLocation*/ false, SpecRange,
                           FixItHint::CreateReplacement(SpecRange, os.str()));
    } else {
      // The canonical type for formatting this value is different from the
      // actual type of the expression. (This occurs, for example, with Darwin's
      // NSInteger on 32-bit platforms, where it is typedef'd as 'int', but
      // should be printed as 'long' for 64-bit compatibility.)
      // Rather than emitting a normal format/argument mismatch, we want to
      // add a cast to the recommended type (and correct the format string
      // if necessary).
      SmallString<16> CastBuf;
      llvm::raw_svector_ostream CastFix(CastBuf);
      CastFix << "(";
      IntendedTy.print(CastFix, S.Context.getPrintingPolicy());
      CastFix << ")";

      SmallVector<FixItHint,4> Hints;
      if (!AT.matchesType(S.Context, IntendedTy) || ShouldNotPrintDirectly)
        Hints.push_back(FixItHint::CreateReplacement(SpecRange, os.str()));

      if (const CStyleCastExpr *CCast = dyn_cast<CStyleCastExpr>(E)) {
        // If there's already a cast present, just replace it.
        SourceRange CastRange(CCast->getLParenLoc(), CCast->getRParenLoc());
        Hints.push_back(FixItHint::CreateReplacement(CastRange, CastFix.str()));

      } else if (!requiresParensToAddCast(E)) {
        // If the expression has high enough precedence,
        // just write the C-style cast.
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));
      } else {
        // Otherwise, add parens around the expression as well as the cast.
        CastFix << "(";
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));

        SourceLocation After = S.getLocForEndOfToken(E->getEndLoc());
        Hints.push_back(FixItHint::CreateInsertion(After, ")"));
      }

      if (ShouldNotPrintDirectly) {
        // The expression has a type that should not be printed directly.
        // We extract the name from the typedef because we don't want to show
        // the underlying type in the diagnostic.
        StringRef Name;
        if (const TypedefType *TypedefTy = dyn_cast<TypedefType>(ExprTy))
          Name = TypedefTy->getDecl()->getName();
        else
          Name = CastTyName;
        unsigned Diag = Pedantic
                            ? diag::warn_format_argument_needs_cast_pedantic
                            : diag::warn_format_argument_needs_cast;
        EmitFormatDiagnostic(S.PDiag(Diag) << Name << IntendedTy << IsEnum
                                           << E->getSourceRange(),
                             E->getBeginLoc(), /*IsStringLocation=*/false,
                             SpecRange, Hints);
      } else {
        // In this case, the expression could be printed using a different
        // specifier, but we've decided that the specifier is probably correct
        // and we should cast instead. Just use the normal warning message.
        EmitFormatDiagnostic(
            S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
                << AT.getRepresentativeTypeName(S.Context) << ExprTy << IsEnum
                << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, SpecRange, Hints);
      }
    }
  } else {
    const CharSourceRange &CSR = getSpecifierRange(StartSpecifier,
                                                   SpecifierLen);
    // Since the warning for passing non-POD types to variadic functions
    // was deferred until now, we emit a warning for non-POD
    // arguments here.
    switch (S.isValidVarArgType(ExprTy)) {
    case Sema::VAK_Valid:
    case Sema::VAK_ValidInCXX11: {
      unsigned Diag =
          Pedantic
              ? diag::warn_format_conversion_argument_type_mismatch_pedantic
              : diag::warn_format_conversion_argument_type_mismatch;

      EmitFormatDiagnostic(
          S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context) << ExprTy
                        << IsEnum << CSR << E->getSourceRange(),
          E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      break;
    }
    case Sema::VAK_Undefined:
    case Sema::VAK_MSVCUndefined:
      EmitFormatDiagnostic(S.PDiag(diag::warn_non_pod_vararg_with_format_string)
                               << S.getLangOpts().CPlusPlus11 << ExprTy
                               << CallType
                               << AT.getRepresentativeTypeName(S.Context) << CSR
                               << E->getSourceRange(),
                           E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      checkForCStrMembers(AT, E);
      break;

    case Sema::VAK_Invalid:
      if (ExprTy->isObjCObjectType())
        EmitFormatDiagnostic(
            S.PDiag(diag::err_cannot_pass_objc_interface_to_vararg_format)
                << S.getLangOpts().CPlusPlus11 << ExprTy << CallType
                << AT.getRepresentativeTypeName(S.Context) << CSR
                << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      else
        // FIXME: If this is an initializer list, suggest removing the braces
        // or inserting a cast to the target type.
        S.Diag(E->getBeginLoc(), diag::err_cannot_pass_to_vararg_format)
            << isa<InitListExpr>(E) << ExprTy << CallType
            << AT.getRepresentativeTypeName(S.Context) << E->getSourceRange();
      break;
    }

    assert(FirstDataArg + FS.getArgIndex() < CheckedVarArgs.size() &&
           "format string specifier index out of range");
    CheckedVarArgs[FirstDataArg + FS.getArgIndex()] = true;
  }

  return true;
}

//===--- CHECK: Scanf format string checking ------------------------------===//

namespace {

class CheckScanfHandler : public CheckFormatHandler {
public:
  CheckScanfHandler(Sema &s, const FormatStringLiteral *fexpr,
                    const Expr *origFormatExpr, Sema::FormatStringType type,
                    unsigned firstDataArg, unsigned numDataArgs,
                    const char *beg, bool hasVAListArg,
                    ArrayRef<const Expr *> Args, unsigned formatIdx,
                    bool inFunctionCall, Sema::VariadicCallType CallType,
                    llvm::SmallBitVector &CheckedVarArgs,
                    UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, hasVAListArg, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                            const char *startSpecifier,
                            unsigned specifierLen) override;

  bool HandleInvalidScanfConversionSpecifier(
          const analyze_scanf::ScanfSpecifier &FS,
          const char *startSpecifier,
          unsigned specifierLen) override;

  void HandleIncompleteScanList(const char *start, const char *end) override;
};

} // namespace

void CheckScanfHandler::HandleIncompleteScanList(const char *start,
                                                 const char *end) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_scanf_scanlist_incomplete),
                       getLocationOfByte(end), /*IsStringLocation*/true,
                       getSpecifierRange(start, end - start));
}

bool CheckScanfHandler::HandleInvalidScanfConversionSpecifier(
                                        const analyze_scanf::ScanfSpecifier &FS,
                                        const char *startSpecifier,
                                        unsigned specifierLen) {
  const analyze_scanf::ScanfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(FS.getArgIndex(),
                                          getLocationOfByte(CS.getStart()),
                                          startSpecifier, specifierLen,
                                          CS.getStart(), CS.getLength());
}

bool CheckScanfHandler::HandleScanfSpecifier(
                                       const analyze_scanf::ScanfSpecifier &FS,
                                       const char *startSpecifier,
                                       unsigned specifierLen) {
  using namespace analyze_scanf;
  using namespace analyze_format_string;

  const ScanfConversionSpecifier &CS = FS.getConversionSpecifier();

  // Handle case where '%' and '*' don't consume an argument.  These shouldn't
  // be used to decide if we are using positional arguments consistently.
  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
      atFirstArg = false;
      usesPositionalArgs = FS.usesPositionalArg();
    }
    else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  // Check if the field with is non-zero.
  const OptionalAmount &Amt = FS.getFieldWidth();
  if (Amt.getHowSpecified() == OptionalAmount::Constant) {
    if (Amt.getConstantAmount() == 0) {
      const CharSourceRange &R = getSpecifierRange(Amt.getStart(),
                                                   Amt.getConstantLength());
      EmitFormatDiagnostic(S.PDiag(diag::warn_scanf_nonzero_width),
                           getLocationOfByte(Amt.getStart()),
                           /*IsStringLocation*/true, R,
                           FixItHint::CreateRemoval(R));
    }
  }

  if (!FS.consumesDataArgument()) {
    // FIXME: Technically specifying a precision or field width here
    // makes no sense.  Worth issuing a warning at some point.
    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
      // The check to see if the argIndex is valid will come later.
      // We set the bit here because we may exit early from this
      // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  // Check the length modifier is valid with the given conversion specifier.
  if (!FS.hasValidLengthModifier(S.getASTContext().getTargetInfo()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  // The remaining checks depend on the data arguments.
  if (HasVAListArg)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  // Check that the argument type matches the format specifier.
  const Expr *Ex = getDataArg(argIndex);
  if (!Ex)
    return true;

  const analyze_format_string::ArgType &AT = FS.getArgType(S.Context);

  if (!AT.isValid()) {
    return true;
  }

  analyze_format_string::ArgType::MatchKind Match =
      AT.matchesType(S.Context, Ex->getType());
  bool Pedantic = Match == analyze_format_string::ArgType::NoMatchPedantic;
  if (Match == analyze_format_string::ArgType::Match)
    return true;

  ScanfSpecifier fixedFS = FS;
  bool Success = fixedFS.fixType(Ex->getType(), Ex->IgnoreImpCasts()->getType(),
                                 S.getLangOpts(), S.Context);

  unsigned Diag =
      Pedantic ? diag::warn_format_conversion_argument_type_mismatch_pedantic
               : diag::warn_format_conversion_argument_type_mismatch;

  if (Success) {
    // Get the fix string from the fixed format specifier.
    SmallString<128> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    EmitFormatDiagnostic(
        S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context)
                      << Ex->getType() << false << Ex->getSourceRange(),
        Ex->getBeginLoc(),
        /*IsStringLocation*/ false,
        getSpecifierRange(startSpecifier, specifierLen),
        FixItHint::CreateReplacement(
            getSpecifierRange(startSpecifier, specifierLen), os.str()));
  } else {
    EmitFormatDiagnostic(S.PDiag(Diag)
                             << AT.getRepresentativeTypeName(S.Context)
                             << Ex->getType() << false << Ex->getSourceRange(),
                         Ex->getBeginLoc(),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
  }

  return true;
}

static void CheckFormatString(Sema &S, const FormatStringLiteral *FExpr,
                              const Expr *OrigFormatExpr,
                              ArrayRef<const Expr *> Args,
                              bool HasVAListArg, unsigned format_idx,
                              unsigned firstDataArg,
                              Sema::FormatStringType Type,
                              bool inFunctionCall,
                              Sema::VariadicCallType CallType,
                              llvm::SmallBitVector &CheckedVarArgs,
                              UncoveredArgHandler &UncoveredArg) {
  // CHECK: is the format string a wide literal?
  if (!FExpr->isAscii() && !FExpr->isUTF8()) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_format_string_is_wide_literal), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  // Str - The format string.  NOTE: this is NOT null-terminated!
  StringRef StrRef = FExpr->getString();
  const char *Str = StrRef.data();
  // Account for cases where the string literal is truncated in a declaration.
  const ConstantArrayType *T =
    S.Context.getAsConstantArrayType(FExpr->getType());
  assert(T && "String literal not of constant array type!");
  size_t TypeSize = T->getSize().getZExtValue();
  size_t StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, StrRef.size());
  const unsigned numDataArgs = Args.size() - firstDataArg;

  // Emit a warning if the string literal is truncated and does not contain an
  // embedded null character.
  if (TypeSize <= StrRef.size() &&
      StrRef.substr(0, TypeSize).find('\0') == StringRef::npos) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_printf_format_string_not_null_terminated),
        FExpr->getBeginLoc(),
        /*IsStringLocation=*/true, OrigFormatExpr->getSourceRange());
    return;
  }

  // CHECK: empty format string?
  if (StrLen == 0 && numDataArgs > 0) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_empty_format_string), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  if (Type == Sema::FST_Printf || Type == Sema::FST_NSString ||
      Type == Sema::FST_FreeBSDKPrintf || Type == Sema::FST_OSLog ||
      Type == Sema::FST_OSTrace) {
    CheckPrintfHandler H(
        S, FExpr, OrigFormatExpr, Type, firstDataArg, numDataArgs,
        (Type == Sema::FST_NSString || Type == Sema::FST_OSTrace), Str,
        HasVAListArg, Args, format_idx, inFunctionCall, CallType,
        CheckedVarArgs, UncoveredArg);

    if (!analyze_format_string::ParsePrintfString(H, Str, Str + StrLen,
                                                  S.getLangOpts(),
                                                  S.Context.getTargetInfo(),
                                            Type == Sema::FST_FreeBSDKPrintf))
      H.DoneProcessing();
  } else if (Type == Sema::FST_Scanf) {
    CheckScanfHandler H(S, FExpr, OrigFormatExpr, Type, firstDataArg,
                        numDataArgs, Str, HasVAListArg, Args, format_idx,
                        inFunctionCall, CallType, CheckedVarArgs, UncoveredArg);

    if (!analyze_format_string::ParseScanfString(H, Str, Str + StrLen,
                                                 S.getLangOpts(),
                                                 S.Context.getTargetInfo()))
      H.DoneProcessing();
  } // TODO: handle other formats
}

bool Sema::FormatStringHasSArg(const StringLiteral *FExpr) {
  // Str - The format string.  NOTE: this is NOT null-terminated!
  StringRef StrRef = FExpr->getString();
  const char *Str = StrRef.data();
  // Account for cases where the string literal is truncated in a declaration.
  const ConstantArrayType *T = Context.getAsConstantArrayType(FExpr->getType());
  assert(T && "String literal not of constant array type!");
  size_t TypeSize = T->getSize().getZExtValue();
  size_t StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, StrRef.size());
  return analyze_format_string::ParseFormatStringHasSArg(Str, Str + StrLen,
                                                         getLangOpts(),
                                                         Context.getTargetInfo());
}

//===--- CHECK: Warn on use of wrong absolute value function. -------------===//

// Returns the related absolute value function that is larger, of 0 if one
// does not exist.
static unsigned getLargerAbsoluteValueFunction(unsigned AbsFunction) {
  switch (AbsFunction) {
  default:
    return 0;

  case Builtin::BI__builtin_abs:
    return Builtin::BI__builtin_labs;
  case Builtin::BI__builtin_labs:
    return Builtin::BI__builtin_llabs;
  case Builtin::BI__builtin_llabs:
    return 0;

  case Builtin::BI__builtin_fabsf:
    return Builtin::BI__builtin_fabs;
  case Builtin::BI__builtin_fabs:
    return Builtin::BI__builtin_fabsl;
  case Builtin::BI__builtin_fabsl:
    return 0;

  case Builtin::BI__builtin_cabsf:
    return Builtin::BI__builtin_cabs;
  case Builtin::BI__builtin_cabs:
    return Builtin::BI__builtin_cabsl;
  case Builtin::BI__builtin_cabsl:
    return 0;

  case Builtin::BIabs:
    return Builtin::BIlabs;
  case Builtin::BIlabs:
    return Builtin::BIllabs;
  case Builtin::BIllabs:
    return 0;

  case Builtin::BIfabsf:
    return Builtin::BIfabs;
  case Builtin::BIfabs:
    return Builtin::BIfabsl;
  case Builtin::BIfabsl:
    return 0;

  case Builtin::BIcabsf:
   return Builtin::BIcabs;
  case Builtin::BIcabs:
    return Builtin::BIcabsl;
  case Builtin::BIcabsl:
    return 0;
  }
}

// Returns the argument type of the absolute value function.
static QualType getAbsoluteValueArgumentType(ASTContext &Context,
                                             unsigned AbsType) {
  if (AbsType == 0)
    return QualType();

  ASTContext::GetBuiltinTypeError Error = ASTContext::GE_None;
  QualType BuiltinType = Context.GetBuiltinType(AbsType, Error);
  if (Error != ASTContext::GE_None)
    return QualType();

  const FunctionProtoType *FT = BuiltinType->getAs<FunctionProtoType>();
  if (!FT)
    return QualType();

  if (FT->getNumParams() != 1)
    return QualType();

  return FT->getParamType(0);
}

// Returns the best absolute value function, or zero, based on type and
// current absolute value function.
static unsigned getBestAbsFunction(ASTContext &Context, QualType ArgType,
                                   unsigned AbsFunctionKind) {
  unsigned BestKind = 0;
  uint64_t ArgSize = Context.getTypeSize(ArgType);
  for (unsigned Kind = AbsFunctionKind; Kind != 0;
       Kind = getLargerAbsoluteValueFunction(Kind)) {
    QualType ParamType = getAbsoluteValueArgumentType(Context, Kind);
    if (Context.getTypeSize(ParamType) >= ArgSize) {
      if (BestKind == 0)
        BestKind = Kind;
      else if (Context.hasSameType(ParamType, ArgType)) {
        BestKind = Kind;
        break;
      }
    }
  }
  return BestKind;
}

enum AbsoluteValueKind {
  AVK_Integer,
  AVK_Floating,
  AVK_Complex
};

static AbsoluteValueKind getAbsoluteValueKind(QualType T) {
  if (T->isIntegralOrEnumerationType())
    return AVK_Integer;
  if (T->isRealFloatingType())
    return AVK_Floating;
  if (T->isAnyComplexType())
    return AVK_Complex;

  llvm_unreachable("Type not integer, floating, or complex");
}

// Changes the absolute value function to a different type.  Preserves whether
// the function is a builtin.
static unsigned changeAbsFunction(unsigned AbsKind,
                                  AbsoluteValueKind ValueKind) {
  switch (ValueKind) {
  case AVK_Integer:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_abs;
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIabs;
    }
  case AVK_Floating:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_fabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIfabsf;
    }
  case AVK_Complex:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
      return Builtin::BI__builtin_cabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
      return Builtin::BIcabsf;
    }
  }
  llvm_unreachable("Unable to convert function");
}

static unsigned getAbsoluteValueFunctionKind(const FunctionDecl *FDecl) {
  const IdentifierInfo *FnInfo = FDecl->getIdentifier();
  if (!FnInfo)
    return 0;

  switch (FDecl->getBuiltinID()) {
  default:
    return 0;
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs:
  case Builtin::BI__builtin_cabs:
  case Builtin::BI__builtin_cabsf:
  case Builtin::BI__builtin_cabsl:
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BIfabs:
  case Builtin::BIfabsf:
  case Builtin::BIfabsl:
  case Builtin::BIcabs:
  case Builtin::BIcabsf:
  case Builtin::BIcabsl:
    return FDecl->getBuiltinID();
  }
  llvm_unreachable("Unknown Builtin type");
}

// If the replacement is valid, emit a note with replacement function.
// Additionally, suggest including the proper header if not already included.
static void emitReplacement(Sema &S, SourceLocation Loc, SourceRange Range,
                            unsigned AbsKind, QualType ArgType) {
  bool EmitHeaderHint = true;
  const char *HeaderName = nullptr;
  const char *FunctionName = nullptr;
  if (S.getLangOpts().CPlusPlus && !ArgType->isAnyComplexType()) {
    FunctionName = "std::abs";
    if (ArgType->isIntegralOrEnumerationType()) {
      HeaderName = "cstdlib";
    } else if (ArgType->isRealFloatingType()) {
      HeaderName = "cmath";
    } else {
      llvm_unreachable("Invalid Type");
    }

    // Lookup all std::abs
    if (NamespaceDecl *Std = S.getStdNamespace()) {
      LookupResult R(S, &S.Context.Idents.get("abs"), Loc, Sema::LookupAnyName);
      R.suppressDiagnostics();
      S.LookupQualifiedName(R, Std);

      for (const auto *I : R) {
        const FunctionDecl *FDecl = nullptr;
        if (const UsingShadowDecl *UsingD = dyn_cast<UsingShadowDecl>(I)) {
          FDecl = dyn_cast<FunctionDecl>(UsingD->getTargetDecl());
        } else {
          FDecl = dyn_cast<FunctionDecl>(I);
        }
        if (!FDecl)
          continue;

        // Found std::abs(), check that they are the right ones.
        if (FDecl->getNumParams() != 1)
          continue;

        // Check that the parameter type can handle the argument.
        QualType ParamType = FDecl->getParamDecl(0)->getType();
        if (getAbsoluteValueKind(ArgType) == getAbsoluteValueKind(ParamType) &&
            S.Context.getTypeSize(ArgType) <=
                S.Context.getTypeSize(ParamType)) {
          // Found a function, don't need the header hint.
          EmitHeaderHint = false;
          break;
        }
      }
    }
  } else {
    FunctionName = S.Context.BuiltinInfo.getName(AbsKind);
    HeaderName = S.Context.BuiltinInfo.getHeaderName(AbsKind);

    if (HeaderName) {
      DeclarationName DN(&S.Context.Idents.get(FunctionName));
      LookupResult R(S, DN, Loc, Sema::LookupAnyName);
      R.suppressDiagnostics();
      S.LookupName(R, S.getCurScope());

      if (R.isSingleResult()) {
        FunctionDecl *FD = dyn_cast<FunctionDecl>(R.getFoundDecl());
        if (FD && FD->getBuiltinID() == AbsKind) {
          EmitHeaderHint = false;
        } else {
          return;
        }
      } else if (!R.empty()) {
        return;
      }
    }
  }

  S.Diag(Loc, diag::note_replace_abs_function)
      << FunctionName << FixItHint::CreateReplacement(Range, FunctionName);

  if (!HeaderName)
    return;

  if (!EmitHeaderHint)
    return;

  S.Diag(Loc, diag::note_include_header_or_declare) << HeaderName
                                                    << FunctionName;
}

template <std::size_t StrLen>
static bool IsStdFunction(const FunctionDecl *FDecl,
                          const char (&Str)[StrLen]) {
  if (!FDecl)
    return false;
  if (!FDecl->getIdentifier() || !FDecl->getIdentifier()->isStr(Str))
    return false;
  if (!FDecl->isInStdNamespace())
    return false;

  return true;
}

// Warn when using the wrong abs() function.
void Sema::CheckAbsoluteValueFunction(const CallExpr *Call,
                                      const FunctionDecl *FDecl) {
  if (Call->getNumArgs() != 1)
    return;

  unsigned AbsKind = getAbsoluteValueFunctionKind(FDecl);
  bool IsStdAbs = IsStdFunction(FDecl, "abs");
  if (AbsKind == 0 && !IsStdAbs)
    return;

  QualType ArgType = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  QualType ParamType = Call->getArg(0)->getType();

  // Unsigned types cannot be negative.  Suggest removing the absolute value
  // function call.
  if (ArgType->isUnsignedIntegerType()) {
    const char *FunctionName =
        IsStdAbs ? "std::abs" : Context.BuiltinInfo.getName(AbsKind);
    Diag(Call->getExprLoc(), diag::warn_unsigned_abs) << ArgType << ParamType;
    Diag(Call->getExprLoc(), diag::note_remove_abs)
        << FunctionName
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange());
    return;
  }

  // Taking the absolute value of a pointer is very suspicious, they probably
  // wanted to index into an array, dereference a pointer, call a function, etc.
  if (ArgType->isPointerType() || ArgType->canDecayToPointerType()) {
    unsigned DiagType = 0;
    if (ArgType->isFunctionType())
      DiagType = 1;
    else if (ArgType->isArrayType())
      DiagType = 2;

    Diag(Call->getExprLoc(), diag::warn_pointer_abs) << DiagType << ArgType;
    return;
  }

  // std::abs has overloads which prevent most of the absolute value problems
  // from occurring.
  if (IsStdAbs)
    return;

  AbsoluteValueKind ArgValueKind = getAbsoluteValueKind(ArgType);
  AbsoluteValueKind ParamValueKind = getAbsoluteValueKind(ParamType);

  // The argument and parameter are the same kind.  Check if they are the right
  // size.
  if (ArgValueKind == ParamValueKind) {
    if (Context.getTypeSize(ArgType) <= Context.getTypeSize(ParamType))
      return;

    unsigned NewAbsKind = getBestAbsFunction(Context, ArgType, AbsKind);
    Diag(Call->getExprLoc(), diag::warn_abs_too_small)
        << FDecl << ArgType << ParamType;

    if (NewAbsKind == 0)
      return;

    emitReplacement(*this, Call->getExprLoc(),
                    Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
    return;
  }

  // ArgValueKind != ParamValueKind
  // The wrong type of absolute value function was used.  Attempt to find the
  // proper one.
  unsigned NewAbsKind = changeAbsFunction(AbsKind, ArgValueKind);
  NewAbsKind = getBestAbsFunction(Context, ArgType, NewAbsKind);
  if (NewAbsKind == 0)
    return;

  Diag(Call->getExprLoc(), diag::warn_wrong_absolute_value_type)
      << FDecl << ParamValueKind << ArgValueKind;

  emitReplacement(*this, Call->getExprLoc(),
                  Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
}

//===--- CHECK: Warn on use of std::max and unsigned zero. r---------------===//
void Sema::CheckMaxUnsignedZero(const CallExpr *Call,
                                const FunctionDecl *FDecl) {
  if (!Call || !FDecl) return;

  // Ignore template specializations and macros.
  if (inTemplateInstantiation()) return;
  if (Call->getExprLoc().isMacroID()) return;

  // Only care about the one template argument, two function parameter std::max
  if (Call->getNumArgs() != 2) return;
  if (!IsStdFunction(FDecl, "max")) return;
  const auto * ArgList = FDecl->getTemplateSpecializationArgs();
  if (!ArgList) return;
  if (ArgList->size() != 1) return;

  // Check that template type argument is unsigned integer.
  const auto& TA = ArgList->get(0);
  if (TA.getKind() != TemplateArgument::Type) return;
  QualType ArgType = TA.getAsType();
  if (!ArgType->isUnsignedIntegerType()) return;

  // See if either argument is a literal zero.
  auto IsLiteralZeroArg = [](const Expr* E) -> bool {
    const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E);
    if (!MTE) return false;
    const auto *Num = dyn_cast<IntegerLiteral>(MTE->GetTemporaryExpr());
    if (!Num) return false;
    if (Num->getValue() != 0) return false;
    return true;
  };

  const Expr *FirstArg = Call->getArg(0);
  const Expr *SecondArg = Call->getArg(1);
  const bool IsFirstArgZero = IsLiteralZeroArg(FirstArg);
  const bool IsSecondArgZero = IsLiteralZeroArg(SecondArg);

  // Only warn when exactly one argument is zero.
  if (IsFirstArgZero == IsSecondArgZero) return;

  SourceRange FirstRange = FirstArg->getSourceRange();
  SourceRange SecondRange = SecondArg->getSourceRange();

  SourceRange ZeroRange = IsFirstArgZero ? FirstRange : SecondRange;

  Diag(Call->getExprLoc(), diag::warn_max_unsigned_zero)
      << IsFirstArgZero << Call->getCallee()->getSourceRange() << ZeroRange;

  // Deduce what parts to remove so that "std::max(0u, foo)" becomes "(foo)".
  SourceRange RemovalRange;
  if (IsFirstArgZero) {
    RemovalRange = SourceRange(FirstRange.getBegin(),
                               SecondRange.getBegin().getLocWithOffset(-1));
  } else {
    RemovalRange = SourceRange(getLocForEndOfToken(FirstRange.getEnd()),
                               SecondRange.getEnd());
  }

  Diag(Call->getExprLoc(), diag::note_remove_max_call)
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange())
        << FixItHint::CreateRemoval(RemovalRange);
}

//===--- CHECK: Standard memory functions ---------------------------------===//

/// Takes the expression passed to the size_t parameter of functions
/// such as memcmp, strncat, etc and warns if it's a comparison.
///
/// This is to catch typos like `if (memcmp(&a, &b, sizeof(a) > 0))`.
static bool CheckMemorySizeofForComparison(Sema &S, const Expr *E,
                                           IdentifierInfo *FnName,
                                           SourceLocation FnLoc,
                                           SourceLocation RParenLoc) {
  const BinaryOperator *Size = dyn_cast<BinaryOperator>(E);
  if (!Size)
    return false;

  // if E is binop and op is <=>, >, <, >=, <=, ==, &&, ||:
  if (!Size->isComparisonOp() && !Size->isLogicalOp())
    return false;

  SourceRange SizeRange = Size->getSourceRange();
  S.Diag(Size->getOperatorLoc(), diag::warn_memsize_comparison)
      << SizeRange << FnName;
  S.Diag(FnLoc, diag::note_memsize_comparison_paren)
      << FnName
      << FixItHint::CreateInsertion(
             S.getLocForEndOfToken(Size->getLHS()->getEndLoc()), ")")
      << FixItHint::CreateRemoval(RParenLoc);
  S.Diag(SizeRange.getBegin(), diag::note_memsize_comparison_cast_silence)
      << FixItHint::CreateInsertion(SizeRange.getBegin(), "(size_t)(")
      << FixItHint::CreateInsertion(S.getLocForEndOfToken(SizeRange.getEnd()),
                                    ")");

  return true;
}

/// Determine whether the given type is or contains a dynamic class type
/// (e.g., whether it has a vtable).
static const CXXRecordDecl *getContainedDynamicClass(QualType T,
                                                     bool &IsContained) {
  // Look through array types while ignoring qualifiers.
  const Type *Ty = T->getBaseElementTypeUnsafe();
  IsContained = false;

  const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  RD = RD ? RD->getDefinition() : nullptr;
  if (!RD || RD->isInvalidDecl())
    return nullptr;

  if (RD->isDynamicClass())
    return RD;

  // Check all the fields.  If any bases were dynamic, the class is dynamic.
  // It's impossible for a class to transitively contain itself by value, so
  // infinite recursion is impossible.
  for (auto *FD : RD->fields()) {
    bool SubContained;
    if (const CXXRecordDecl *ContainedRD =
            getContainedDynamicClass(FD->getType(), SubContained)) {
      IsContained = true;
      return ContainedRD;
    }
  }

  return nullptr;
}

static const UnaryExprOrTypeTraitExpr *getAsSizeOfExpr(const Expr *E) {
  if (const auto *Unary = dyn_cast<UnaryExprOrTypeTraitExpr>(E))
    if (Unary->getKind() == UETT_SizeOf)
      return Unary;
  return nullptr;
}

/// If E is a sizeof expression, returns its argument expression,
/// otherwise returns NULL.
static const Expr *getSizeOfExprArg(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    if (!SizeOf->isArgumentType())
      return SizeOf->getArgumentExpr()->IgnoreParenImpCasts();
  return nullptr;
}

/// If E is a sizeof expression, returns its argument type.
static QualType getSizeOfArgType(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    return SizeOf->getTypeOfArgument();
  return QualType();
}

namespace {

struct SearchNonTrivialToInitializeField
    : DefaultInitializedTypeVisitor<SearchNonTrivialToInitializeField> {
  using Super =
      DefaultInitializedTypeVisitor<SearchNonTrivialToInitializeField>;

  SearchNonTrivialToInitializeField(const Expr *E, Sema &S) : E(E), S(S) {}

  void visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK, QualType FT,
                     SourceLocation SL) {
    if (const auto *AT = asDerived().getContext().getAsArrayType(FT)) {
      asDerived().visitArray(PDIK, AT, SL);
      return;
    }

    Super::visitWithKind(PDIK, FT, SL);
  }

  void visitARCStrong(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 1);
  }
  void visitARCWeak(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 1);
  }
  void visitStruct(QualType FT, SourceLocation SL) {
    for (const FieldDecl *FD : FT->castAs<RecordType>()->getDecl()->fields())
      visit(FD->getType(), FD->getLocation());
  }
  void visitArray(QualType::PrimitiveDefaultInitializeKind PDIK,
                  const ArrayType *AT, SourceLocation SL) {
    visit(getContext().getBaseElementType(AT), SL);
  }
  void visitTrivial(QualType FT, SourceLocation SL) {}

  static void diag(QualType RT, const Expr *E, Sema &S) {
    SearchNonTrivialToInitializeField(E, S).visitStruct(RT, SourceLocation());
  }

  ASTContext &getContext() { return S.getASTContext(); }

  const Expr *E;
  Sema &S;
};

struct SearchNonTrivialToCopyField
    : CopiedTypeVisitor<SearchNonTrivialToCopyField, false> {
  using Super = CopiedTypeVisitor<SearchNonTrivialToCopyField, false>;

  SearchNonTrivialToCopyField(const Expr *E, Sema &S) : E(E), S(S) {}

  void visitWithKind(QualType::PrimitiveCopyKind PCK, QualType FT,
                     SourceLocation SL) {
    if (const auto *AT = asDerived().getContext().getAsArrayType(FT)) {
      asDerived().visitArray(PCK, AT, SL);
      return;
    }

    Super::visitWithKind(PCK, FT, SL);
  }

  void visitARCStrong(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 0);
  }
  void visitARCWeak(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 0);
  }
  void visitStruct(QualType FT, SourceLocation SL) {
    for (const FieldDecl *FD : FT->castAs<RecordType>()->getDecl()->fields())
      visit(FD->getType(), FD->getLocation());
  }
  void visitArray(QualType::PrimitiveCopyKind PCK, const ArrayType *AT,
                  SourceLocation SL) {
    visit(getContext().getBaseElementType(AT), SL);
  }
  void preVisit(QualType::PrimitiveCopyKind PCK, QualType FT,
                SourceLocation SL) {}
  void visitTrivial(QualType FT, SourceLocation SL) {}
  void visitVolatileTrivial(QualType FT, SourceLocation SL) {}

  static void diag(QualType RT, const Expr *E, Sema &S) {
    SearchNonTrivialToCopyField(E, S).visitStruct(RT, SourceLocation());
  }

  ASTContext &getContext() { return S.getASTContext(); }

  const Expr *E;
  Sema &S;
};

}

/// Detect if \c SizeofExpr is likely to calculate the sizeof an object.
static bool doesExprLikelyComputeSize(const Expr *SizeofExpr) {
  SizeofExpr = SizeofExpr->IgnoreParenImpCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(SizeofExpr)) {
    if (BO->getOpcode() != BO_Mul && BO->getOpcode() != BO_Add)
      return false;

    return doesExprLikelyComputeSize(BO->getLHS()) ||
           doesExprLikelyComputeSize(BO->getRHS());
  }

  return getAsSizeOfExpr(SizeofExpr) != nullptr;
}

/// Check if the ArgLoc originated from a macro passed to the call at CallLoc.
///
/// \code
///   #define MACRO 0
///   foo(MACRO);
///   foo(0);
/// \endcode
///
/// This should return true for the first call to foo, but not for the second
/// (regardless of whether foo is a macro or function).
static bool isArgumentExpandedFromMacro(SourceManager &SM,
                                        SourceLocation CallLoc,
                                        SourceLocation ArgLoc) {
  if (!CallLoc.isMacroID())
    return SM.getFileID(CallLoc) != SM.getFileID(ArgLoc);

  return SM.getFileID(SM.getImmediateMacroCallerLoc(CallLoc)) !=
         SM.getFileID(SM.getImmediateMacroCallerLoc(ArgLoc));
}

/// Diagnose cases like 'memset(buf, sizeof(buf), 0)', which should have the
/// last two arguments transposed.
static void CheckMemaccessSize(Sema &S, unsigned BId, const CallExpr *Call) {
  if (BId != Builtin::BImemset && BId != Builtin::BIbzero)
    return;

  const Expr *SizeArg =
    Call->getArg(BId == Builtin::BImemset ? 2 : 1)->IgnoreImpCasts();

  auto isLiteralZero = [](const Expr *E) {
    return isa<IntegerLiteral>(E) && cast<IntegerLiteral>(E)->getValue() == 0;
  };

  // If we're memsetting or bzeroing 0 bytes, then this is likely an error.
  SourceLocation CallLoc = Call->getRParenLoc();
  SourceManager &SM = S.getSourceManager();
  if (isLiteralZero(SizeArg) &&
      !isArgumentExpandedFromMacro(SM, CallLoc, SizeArg->getExprLoc())) {

    SourceLocation DiagLoc = SizeArg->getExprLoc();

    // Some platforms #define bzero to __builtin_memset. See if this is the
    // case, and if so, emit a better diagnostic.
    if (BId == Builtin::BIbzero ||
        (CallLoc.isMacroID() && Lexer::getImmediateMacroName(
                                    CallLoc, SM, S.getLangOpts()) == "bzero")) {
      S.Diag(DiagLoc, diag::warn_suspicious_bzero_size);
      S.Diag(DiagLoc, diag::note_suspicious_bzero_size_silence);
    } else if (!isLiteralZero(Call->getArg(1)->IgnoreImpCasts())) {
      S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 0;
      S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 0;
    }
    return;
  }

  // If the second argument to a memset is a sizeof expression and the third
  // isn't, this is also likely an error. This should catch
  // 'memset(buf, sizeof(buf), 0xff)'.
  if (BId == Builtin::BImemset &&
      doesExprLikelyComputeSize(Call->getArg(1)) &&
      !doesExprLikelyComputeSize(Call->getArg(2))) {
    SourceLocation DiagLoc = Call->getArg(1)->getExprLoc();
    S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 1;
    S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 1;
    return;
  }
}

/// Check for dangerous or invalid arguments to memset().
///
/// This issues warnings on known problematic, dangerous or unspecified
/// arguments to the standard 'memset', 'memcpy', 'memmove', and 'memcmp'
/// function calls.
///
/// \param Call The call expression to diagnose.
void Sema::CheckMemaccessArguments(const CallExpr *Call,
                                   unsigned BId,
                                   IdentifierInfo *FnName) {
  assert(BId != 0);

  // It is possible to have a non-standard definition of memset.  Validate
  // we have enough arguments, and if not, abort further checking.
  unsigned ExpectedNumArgs =
      (BId == Builtin::BIstrndup || BId == Builtin::BIbzero ? 2 : 3);
  if (Call->getNumArgs() < ExpectedNumArgs)
    return;

  unsigned LastArg = (BId == Builtin::BImemset || BId == Builtin::BIbzero ||
                      BId == Builtin::BIstrndup ? 1 : 2);
  unsigned LenArg =
      (BId == Builtin::BIbzero || BId == Builtin::BIstrndup ? 1 : 2);
  const Expr *LenExpr = Call->getArg(LenArg)->IgnoreParenImpCasts();

  if (CheckMemorySizeofForComparison(*this, LenExpr, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Catch cases like 'memset(buf, sizeof(buf), 0)'.
  CheckMemaccessSize(*this, BId, Call);

  // We have special checking when the length is a sizeof expression.
  QualType SizeOfArgTy = getSizeOfArgType(LenExpr);
  const Expr *SizeOfArg = getSizeOfExprArg(LenExpr);
  llvm::FoldingSetNodeID SizeOfArgID;

  // Although widely used, 'bzero' is not a standard function. Be more strict
  // with the argument types before allowing diagnostics and only allow the
  // form bzero(ptr, sizeof(...)).
  QualType FirstArgTy = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  if (BId == Builtin::BIbzero && !FirstArgTy->getAs<PointerType>())
    return;

  for (unsigned ArgIdx = 0; ArgIdx != LastArg; ++ArgIdx) {
    const Expr *Dest = Call->getArg(ArgIdx)->IgnoreParenImpCasts();
    SourceRange ArgRange = Call->getArg(ArgIdx)->getSourceRange();

    QualType DestTy = Dest->getType();
    QualType PointeeTy;
    if (const PointerType *DestPtrTy = DestTy->getAs<PointerType>()) {
      PointeeTy = DestPtrTy->getPointeeType();

      // Never warn about void type pointers. This can be used to suppress
      // false positives.
      if (PointeeTy->isVoidType())
        continue;

      // Catch "memset(p, 0, sizeof(p))" -- needs to be sizeof(*p). Do this by
      // actually comparing the expressions for equality. Because computing the
      // expression IDs can be expensive, we only do this if the diagnostic is
      // enabled.
      if (SizeOfArg &&
          !Diags.isIgnored(diag::warn_sizeof_pointer_expr_memaccess,
                           SizeOfArg->getExprLoc())) {
        // We only compute IDs for expressions if the warning is enabled, and
        // cache the sizeof arg's ID.
        if (SizeOfArgID == llvm::FoldingSetNodeID())
          SizeOfArg->Profile(SizeOfArgID, Context, true);
        llvm::FoldingSetNodeID DestID;
        Dest->Profile(DestID, Context, true);
        if (DestID == SizeOfArgID) {
          // TODO: For strncpy() and friends, this could suggest sizeof(dst)
          //       over sizeof(src) as well.
          unsigned ActionIdx = 0; // Default is to suggest dereferencing.
          StringRef ReadableName = FnName->getName();

          if (const UnaryOperator *UnaryOp = dyn_cast<UnaryOperator>(Dest))
            if (UnaryOp->getOpcode() == UO_AddrOf)
              ActionIdx = 1; // If its an address-of operator, just remove it.
          if (!PointeeTy->isIncompleteType() &&
              (Context.getTypeSize(PointeeTy) == Context.getCharWidth()))
            ActionIdx = 2; // If the pointee's size is sizeof(char),
                           // suggest an explicit length.

          // If the function is defined as a builtin macro, do not show macro
          // expansion.
          SourceLocation SL = SizeOfArg->getExprLoc();
          SourceRange DSR = Dest->getSourceRange();
          SourceRange SSR = SizeOfArg->getSourceRange();
          SourceManager &SM = getSourceManager();

          if (SM.isMacroArgExpansion(SL)) {
            ReadableName = Lexer::getImmediateMacroName(SL, SM, LangOpts);
            SL = SM.getSpellingLoc(SL);
            DSR = SourceRange(SM.getSpellingLoc(DSR.getBegin()),
                             SM.getSpellingLoc(DSR.getEnd()));
            SSR = SourceRange(SM.getSpellingLoc(SSR.getBegin()),
                             SM.getSpellingLoc(SSR.getEnd()));
          }

          DiagRuntimeBehavior(SL, SizeOfArg,
                              PDiag(diag::warn_sizeof_pointer_expr_memaccess)
                                << ReadableName
                                << PointeeTy
                                << DestTy
                                << DSR
                                << SSR);
          DiagRuntimeBehavior(SL, SizeOfArg,
                         PDiag(diag::warn_sizeof_pointer_expr_memaccess_note)
                                << ActionIdx
                                << SSR);

          break;
        }
      }

      // Also check for cases where the sizeof argument is the exact same
      // type as the memory argument, and where it points to a user-defined
      // record type.
      if (SizeOfArgTy != QualType()) {
        if (PointeeTy->isRecordType() &&
            Context.typesAreCompatible(SizeOfArgTy, DestTy)) {
          DiagRuntimeBehavior(LenExpr->getExprLoc(), Dest,
                              PDiag(diag::warn_sizeof_pointer_type_memaccess)
                                << FnName << SizeOfArgTy << ArgIdx
                                << PointeeTy << Dest->getSourceRange()
                                << LenExpr->getSourceRange());
          break;
        }
      }
    } else if (DestTy->isArrayType()) {
      PointeeTy = DestTy;
    }

    if (PointeeTy == QualType())
      continue;

    // Always complain about dynamic classes.
    bool IsContained;
    if (const CXXRecordDecl *ContainedRD =
            getContainedDynamicClass(PointeeTy, IsContained)) {

      unsigned OperationType = 0;
      // "overwritten" if we're warning about the destination for any call
      // but memcmp; otherwise a verb appropriate to the call.
      if (ArgIdx != 0 || BId == Builtin::BImemcmp) {
        if (BId == Builtin::BImemcpy)
          OperationType = 1;
        else if(BId == Builtin::BImemmove)
          OperationType = 2;
        else if (BId == Builtin::BImemcmp)
          OperationType = 3;
      }

      DiagRuntimeBehavior(
        Dest->getExprLoc(), Dest,
        PDiag(diag::warn_dyn_class_memaccess)
          << (BId == Builtin::BImemcmp ? ArgIdx + 2 : ArgIdx)
          << FnName << IsContained << ContainedRD << OperationType
          << Call->getCallee()->getSourceRange());
    } else if (PointeeTy.hasNonTrivialObjCLifetime() &&
             BId != Builtin::BImemset)
      DiagRuntimeBehavior(
        Dest->getExprLoc(), Dest,
        PDiag(diag::warn_arc_object_memaccess)
          << ArgIdx << FnName << PointeeTy
          << Call->getCallee()->getSourceRange());
    else if (const auto *RT = PointeeTy->getAs<RecordType>()) {
      if ((BId == Builtin::BImemset || BId == Builtin::BIbzero) &&
          RT->getDecl()->isNonTrivialToPrimitiveDefaultInitialize()) {
        DiagRuntimeBehavior(Dest->getExprLoc(), Dest,
                            PDiag(diag::warn_cstruct_memaccess)
                                << ArgIdx << FnName << PointeeTy << 0);
        SearchNonTrivialToInitializeField::diag(PointeeTy, Dest, *this);
      } else if ((BId == Builtin::BImemcpy || BId == Builtin::BImemmove) &&
                 RT->getDecl()->isNonTrivialToPrimitiveCopy()) {
        DiagRuntimeBehavior(Dest->getExprLoc(), Dest,
                            PDiag(diag::warn_cstruct_memaccess)
                                << ArgIdx << FnName << PointeeTy << 1);
        SearchNonTrivialToCopyField::diag(PointeeTy, Dest, *this);
      } else {
        continue;
      }
    } else
      continue;

    DiagRuntimeBehavior(
      Dest->getExprLoc(), Dest,
      PDiag(diag::note_bad_memaccess_silence)
        << FixItHint::CreateInsertion(ArgRange.getBegin(), "(void*)"));
    break;
  }
}

// A little helper routine: ignore addition and subtraction of integer literals.
// This intentionally does not ignore all integer constant expressions because
// we don't want to remove sizeof().
static const Expr *ignoreLiteralAdditions(const Expr *Ex, ASTContext &Ctx) {
  Ex = Ex->IgnoreParenCasts();

  while (true) {
    const BinaryOperator * BO = dyn_cast<BinaryOperator>(Ex);
    if (!BO || !BO->isAdditiveOp())
      break;

    const Expr *RHS = BO->getRHS()->IgnoreParenCasts();
    const Expr *LHS = BO->getLHS()->IgnoreParenCasts();

    if (isa<IntegerLiteral>(RHS))
      Ex = LHS;
    else if (isa<IntegerLiteral>(LHS))
      Ex = RHS;
    else
      break;
  }

  return Ex;
}

static bool isConstantSizeArrayWithMoreThanOneElement(QualType Ty,
                                                      ASTContext &Context) {
  // Only handle constant-sized or VLAs, but not flexible members.
  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(Ty)) {
    // Only issue the FIXIT for arrays of size > 1.
    if (CAT->getSize().getSExtValue() <= 1)
      return false;
  } else if (!Ty->isVariableArrayType()) {
    return false;
  }
  return true;
}

// Warn if the user has made the 'size' argument to strlcpy or strlcat
// be the size of the source, instead of the destination.
void Sema::CheckStrlcpycatArguments(const CallExpr *Call,
                                    IdentifierInfo *FnName) {

  // Don't crash if the user has the wrong number of arguments
  unsigned NumArgs = Call->getNumArgs();
  if ((NumArgs != 3) && (NumArgs != 4))
    return;

  const Expr *SrcArg = ignoreLiteralAdditions(Call->getArg(1), Context);
  const Expr *SizeArg = ignoreLiteralAdditions(Call->getArg(2), Context);
  const Expr *CompareWithSrc = nullptr;

  if (CheckMemorySizeofForComparison(*this, SizeArg, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Look for 'strlcpy(dst, x, sizeof(x))'
  if (const Expr *Ex = getSizeOfExprArg(SizeArg))
    CompareWithSrc = Ex;
  else {
    // Look for 'strlcpy(dst, x, strlen(x))'
    if (const CallExpr *SizeCall = dyn_cast<CallExpr>(SizeArg)) {
      if (SizeCall->getBuiltinCallee() == Builtin::BIstrlen &&
          SizeCall->getNumArgs() == 1)
        CompareWithSrc = ignoreLiteralAdditions(SizeCall->getArg(0), Context);
    }
  }

  if (!CompareWithSrc)
    return;

  // Determine if the argument to sizeof/strlen is equal to the source
  // argument.  In principle there's all kinds of things you could do
  // here, for instance creating an == expression and evaluating it with
  // EvaluateAsBooleanCondition, but this uses a more direct technique:
  const DeclRefExpr *SrcArgDRE = dyn_cast<DeclRefExpr>(SrcArg);
  if (!SrcArgDRE)
    return;

  const DeclRefExpr *CompareWithSrcDRE = dyn_cast<DeclRefExpr>(CompareWithSrc);
  if (!CompareWithSrcDRE ||
      SrcArgDRE->getDecl() != CompareWithSrcDRE->getDecl())
    return;

  const Expr *OriginalSizeArg = Call->getArg(2);
  Diag(CompareWithSrcDRE->getBeginLoc(), diag::warn_strlcpycat_wrong_size)
      << OriginalSizeArg->getSourceRange() << FnName;

  // Output a FIXIT hint if the destination is an array (rather than a
  // pointer to an array).  This could be enhanced to handle some
  // pointers if we know the actual size, like if DstArg is 'array+2'
  // we could say 'sizeof(array)-2'.
  const Expr *DstArg = Call->getArg(0)->IgnoreParenImpCasts();
  if (!isConstantSizeArrayWithMoreThanOneElement(DstArg->getType(), Context))
    return;

  SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ")";

  Diag(OriginalSizeArg->getBeginLoc(), diag::note_strlcpycat_wrong_size)
      << FixItHint::CreateReplacement(OriginalSizeArg->getSourceRange(),
                                      OS.str());
}

/// Check if two expressions refer to the same declaration.
static bool referToTheSameDecl(const Expr *E1, const Expr *E2) {
  if (const DeclRefExpr *D1 = dyn_cast_or_null<DeclRefExpr>(E1))
    if (const DeclRefExpr *D2 = dyn_cast_or_null<DeclRefExpr>(E2))
      return D1->getDecl() == D2->getDecl();
  return false;
}

static const Expr *getStrlenExprArg(const Expr *E) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD || FD->getMemoryFunctionKind() != Builtin::BIstrlen)
      return nullptr;
    return CE->getArg(0)->IgnoreParenCasts();
  }
  return nullptr;
}

// Warn on anti-patterns as the 'size' argument to strncat.
// The correct size argument should look like following:
//   strncat(dst, src, sizeof(dst) - strlen(dest) - 1);
void Sema::CheckStrncatArguments(const CallExpr *CE,
                                 IdentifierInfo *FnName) {
  // Don't crash if the user has the wrong number of arguments.
  if (CE->getNumArgs() < 3)
    return;
  const Expr *DstArg = CE->getArg(0)->IgnoreParenCasts();
  const Expr *SrcArg = CE->getArg(1)->IgnoreParenCasts();
  const Expr *LenArg = CE->getArg(2)->IgnoreParenCasts();

  if (CheckMemorySizeofForComparison(*this, LenArg, FnName, CE->getBeginLoc(),
                                     CE->getRParenLoc()))
    return;

  // Identify common expressions, which are wrongly used as the size argument
  // to strncat and may lead to buffer overflows.
  unsigned PatternType = 0;
  if (const Expr *SizeOfArg = getSizeOfExprArg(LenArg)) {
    // - sizeof(dst)
    if (referToTheSameDecl(SizeOfArg, DstArg))
      PatternType = 1;
    // - sizeof(src)
    else if (referToTheSameDecl(SizeOfArg, SrcArg))
      PatternType = 2;
  } else if (const BinaryOperator *BE = dyn_cast<BinaryOperator>(LenArg)) {
    if (BE->getOpcode() == BO_Sub) {
      const Expr *L = BE->getLHS()->IgnoreParenCasts();
      const Expr *R = BE->getRHS()->IgnoreParenCasts();
      // - sizeof(dst) - strlen(dst)
      if (referToTheSameDecl(DstArg, getSizeOfExprArg(L)) &&
          referToTheSameDecl(DstArg, getStrlenExprArg(R)))
        PatternType = 1;
      // - sizeof(src) - (anything)
      else if (referToTheSameDecl(SrcArg, getSizeOfExprArg(L)))
        PatternType = 2;
    }
  }

  if (PatternType == 0)
    return;

  // Generate the diagnostic.
  SourceLocation SL = LenArg->getBeginLoc();
  SourceRange SR = LenArg->getSourceRange();
  SourceManager &SM = getSourceManager();

  // If the function is defined as a builtin macro, do not show macro expansion.
  if (SM.isMacroArgExpansion(SL)) {
    SL = SM.getSpellingLoc(SL);
    SR = SourceRange(SM.getSpellingLoc(SR.getBegin()),
                     SM.getSpellingLoc(SR.getEnd()));
  }

  // Check if the destination is an array (rather than a pointer to an array).
  QualType DstTy = DstArg->getType();
  bool isKnownSizeArray = isConstantSizeArrayWithMoreThanOneElement(DstTy,
                                                                    Context);
  if (!isKnownSizeArray) {
    if (PatternType == 1)
      Diag(SL, diag::warn_strncat_wrong_size) << SR;
    else
      Diag(SL, diag::warn_strncat_src_size) << SR;
    return;
  }

  if (PatternType == 1)
    Diag(SL, diag::warn_strncat_large_size) << SR;
  else
    Diag(SL, diag::warn_strncat_src_size) << SR;

  SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - ";
  OS << "strlen(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - 1";

  Diag(SL, diag::note_strncat_wrong_size)
    << FixItHint::CreateReplacement(SR, OS.str());
}

void
Sema::CheckReturnValExpr(Expr *RetValExp, QualType lhsType,
                         SourceLocation ReturnLoc,
                         bool isObjCMethod,
                         const AttrVec *Attrs,
                         const FunctionDecl *FD) {
  // Check if the return value is null but should not be.
  if (((Attrs && hasSpecificAttr<ReturnsNonNullAttr>(*Attrs)) ||
       (!isObjCMethod && isNonNullType(Context, lhsType))) &&
      CheckNonNullExpr(*this, RetValExp))
    Diag(ReturnLoc, diag::warn_null_ret)
      << (isObjCMethod ? 1 : 0) << RetValExp->getSourceRange();

  // C++11 [basic.stc.dynamic.allocation]p4:
  //   If an allocation function declared with a non-throwing
  //   exception-specification fails to allocate storage, it shall return
  //   a null pointer. Any other allocation function that fails to allocate
  //   storage shall indicate failure only by throwing an exception [...]
  if (FD) {
    OverloadedOperatorKind Op = FD->getOverloadedOperator();
    if (Op == OO_New || Op == OO_Array_New) {
      const FunctionProtoType *Proto
        = FD->getType()->castAs<FunctionProtoType>();
      if (!Proto->isNothrow(/*ResultIfDependent*/true) &&
          CheckNonNullExpr(*this, RetValExp))
        Diag(ReturnLoc, diag::warn_operator_new_returns_null)
          << FD << getLangOpts().CPlusPlus11;
    }
  }
}

//===--- CHECK: Floating-Point comparisons (-Wfloat-equal) ---------------===//

/// Check for comparisons of floating point operands using != and ==.
/// Issue a warning if these are no self-comparisons, as they are not likely
/// to do what the programmer intended.
void Sema::CheckFloatComparison(SourceLocation Loc, Expr* LHS, Expr *RHS) {
  Expr* LeftExprSansParen = LHS->IgnoreParenImpCasts();
  Expr* RightExprSansParen = RHS->IgnoreParenImpCasts();

  // Special case: check for x == x (which is OK).
  // Do not emit warnings for such cases.
  if (DeclRefExpr* DRL = dyn_cast<DeclRefExpr>(LeftExprSansParen))
    if (DeclRefExpr* DRR = dyn_cast<DeclRefExpr>(RightExprSansParen))
      if (DRL->getDecl() == DRR->getDecl())
        return;

  // Special case: check for comparisons against literals that can be exactly
  //  represented by APFloat.  In such cases, do not emit a warning.  This
  //  is a heuristic: often comparison against such literals are used to
  //  detect if a value in a variable has not changed.  This clearly can
  //  lead to false negatives.
  if (FloatingLiteral* FLL = dyn_cast<FloatingLiteral>(LeftExprSansParen)) {
    if (FLL->isExact())
      return;
  } else
    if (FloatingLiteral* FLR = dyn_cast<FloatingLiteral>(RightExprSansParen))
      if (FLR->isExact())
        return;

  // Check for comparisons with builtin types.
  if (CallExpr* CL = dyn_cast<CallExpr>(LeftExprSansParen))
    if (CL->getBuiltinCallee())
      return;

  if (CallExpr* CR = dyn_cast<CallExpr>(RightExprSansParen))
    if (CR->getBuiltinCallee())
      return;

  // Emit the diagnostic.
  Diag(Loc, diag::warn_floatingpoint_eq)
    << LHS->getSourceRange() << RHS->getSourceRange();
}

//===--- CHECK: Integer mixed-sign comparisons (-Wsign-compare) --------===//
//===--- CHECK: Lossy implicit conversions (-Wconversion) --------------===//

namespace {

/// Structure recording the 'active' range of an integer-valued
/// expression.
struct IntRange {
  /// The number of bits active in the int.
  unsigned Width;

  /// True if the int is known not to have negative values.
  bool NonNegative;

  IntRange(unsigned Width, bool NonNegative)
      : Width(Width), NonNegative(NonNegative) {}

  /// Returns the range of the bool type.
  static IntRange forBoolType() {
    return IntRange(1, true);
  }

  /// Returns the range of an opaque value of the given integral type.
  static IntRange forValueOfType(ASTContext &C, QualType T) {
    return forValueOfCanonicalType(C,
                          T->getCanonicalTypeInternal().getTypePtr());
  }

  /// Returns the range of an opaque value of a canonical integral type.
  static IntRange forValueOfCanonicalType(ASTContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();

    if (!C.getLangOpts().CPlusPlus) {
      // For enum types in C code, use the underlying datatype.
      if (const EnumType *ET = dyn_cast<EnumType>(T))
        T = ET->getDecl()->getIntegerType().getDesugaredType(C).getTypePtr();
    } else if (const EnumType *ET = dyn_cast<EnumType>(T)) {
      // For enum types in C++, use the known bit width of the enumerators.
      EnumDecl *Enum = ET->getDecl();
      // In C++11, enums can have a fixed underlying type. Use this type to
      // compute the range.
      if (Enum->isFixed()) {
        return IntRange(C.getIntWidth(QualType(T, 0)),
                        !ET->isSignedIntegerOrEnumerationType());
      }

      unsigned NumPositive = Enum->getNumPositiveBits();
      unsigned NumNegative = Enum->getNumNegativeBits();

      if (NumNegative == 0)
        return IntRange(NumPositive, true/*NonNegative*/);
      else
        return IntRange(std::max(NumPositive + 1, NumNegative),
                        false/*NonNegative*/);
    }

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  /// Returns the "target" range of a canonical integral type, i.e.
  /// the range of values expressible in the type.
  ///
  /// This matches forValueOfCanonicalType except that enums have the
  /// full range of their type, not the range of their enumerators.
  static IntRange forTargetOfCanonicalType(ASTContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();
    if (const EnumType *ET = dyn_cast<EnumType>(T))
      T = C.getCanonicalType(ET->getDecl()->getIntegerType()).getTypePtr();

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  /// Returns the supremum of two ranges: i.e. their conservative merge.
  static IntRange join(IntRange L, IntRange R) {
    return IntRange(std::max(L.Width, R.Width),
                    L.NonNegative && R.NonNegative);
  }

  /// Returns the infinum of two ranges: i.e. their aggressive merge.
  static IntRange meet(IntRange L, IntRange R) {
    return IntRange(std::min(L.Width, R.Width),
                    L.NonNegative || R.NonNegative);
  }
};

} // namespace

static IntRange GetValueRange(ASTContext &C, llvm::APSInt &value,
                              unsigned MaxWidth) {
  if (value.isSigned() && value.isNegative())
    return IntRange(value.getMinSignedBits(), false);

  if (value.getBitWidth() > MaxWidth)
    value = value.trunc(MaxWidth);

  // isNonNegative() just checks the sign bit without considering
  // signedness.
  return IntRange(value.getActiveBits(), true);
}

static IntRange GetValueRange(ASTContext &C, APValue &result, QualType Ty,
                              unsigned MaxWidth) {
  if (result.isInt())
    return GetValueRange(C, result.getInt(), MaxWidth);

  if (result.isVector()) {
    IntRange R = GetValueRange(C, result.getVectorElt(0), Ty, MaxWidth);
    for (unsigned i = 1, e = result.getVectorLength(); i != e; ++i) {
      IntRange El = GetValueRange(C, result.getVectorElt(i), Ty, MaxWidth);
      R = IntRange::join(R, El);
    }
    return R;
  }

  if (result.isComplexInt()) {
    IntRange R = GetValueRange(C, result.getComplexIntReal(), MaxWidth);
    IntRange I = GetValueRange(C, result.getComplexIntImag(), MaxWidth);
    return IntRange::join(R, I);
  }

  // This can happen with lossless casts to intptr_t of "based" lvalues.
  // Assume it might use arbitrary bits.
  // FIXME: The only reason we need to pass the type in here is to get
  // the sign right on this one case.  It would be nice if APValue
  // preserved this.
  assert(result.isLValue() || result.isAddrLabelDiff());
  return IntRange(MaxWidth, Ty->isUnsignedIntegerOrEnumerationType());
}

static QualType GetExprType(const Expr *E) {
  QualType Ty = E->getType();
  if (const AtomicType *AtomicRHS = Ty->getAs<AtomicType>())
    Ty = AtomicRHS->getValueType();
  return Ty;
}

/// Pseudo-evaluate the given integer expression, estimating the
/// range of values it might take.
///
/// \param MaxWidth - the width to which the value will be truncated
static IntRange GetExprRange(ASTContext &C, const Expr *E, unsigned MaxWidth) {
  E = E->IgnoreParens();

  // Try a full evaluation first.
  Expr::EvalResult result;
  if (E->EvaluateAsRValue(result, C))
    return GetValueRange(C, result.Val, GetExprType(E), MaxWidth);

  // I think we only want to look through implicit casts here; if the
  // user has an explicit widening cast, we should treat the value as
  // being of the new, wider type.
  if (const auto *CE = dyn_cast<ImplicitCastExpr>(E)) {
    if (CE->getCastKind() == CK_NoOp || CE->getCastKind() == CK_LValueToRValue)
      return GetExprRange(C, CE->getSubExpr(), MaxWidth);

    IntRange OutputTypeRange = IntRange::forValueOfType(C, GetExprType(CE));

    bool isIntegerCast = CE->getCastKind() == CK_IntegralCast ||
                         CE->getCastKind() == CK_BooleanToSignedIntegral;

    // Assume that non-integer casts can span the full range of the type.
    if (!isIntegerCast)
      return OutputTypeRange;

    IntRange SubRange
      = GetExprRange(C, CE->getSubExpr(),
                     std::min(MaxWidth, OutputTypeRange.Width));

    // Bail out if the subexpr's range is as wide as the cast type.
    if (SubRange.Width >= OutputTypeRange.Width)
      return OutputTypeRange;

    // Otherwise, we take the smaller width, and we're non-negative if
    // either the output type or the subexpr is.
    return IntRange(SubRange.Width,
                    SubRange.NonNegative || OutputTypeRange.NonNegative);
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    // If we can fold the condition, just take that operand.
    bool CondResult;
    if (CO->getCond()->EvaluateAsBooleanCondition(CondResult, C))
      return GetExprRange(C, CondResult ? CO->getTrueExpr()
                                        : CO->getFalseExpr(),
                          MaxWidth);

    // Otherwise, conservatively merge.
    IntRange L = GetExprRange(C, CO->getTrueExpr(), MaxWidth);
    IntRange R = GetExprRange(C, CO->getFalseExpr(), MaxWidth);
    return IntRange::join(L, R);
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
    case BO_Cmp:
      llvm_unreachable("builtin <=> should have class type");

    // Boolean-valued operations are single-bit and positive.
    case BO_LAnd:
    case BO_LOr:
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
      return IntRange::forBoolType();

    // The type of the assignments is the type of the LHS, so the RHS
    // is not necessarily the same type.
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_XorAssign:
    case BO_OrAssign:
      // TODO: bitfields?
      return IntRange::forValueOfType(C, GetExprType(E));

    // Simple assignments just pass through the RHS, which will have
    // been coerced to the LHS type.
    case BO_Assign:
      // TODO: bitfields?
      return GetExprRange(C, BO->getRHS(), MaxWidth);

    // Operations with opaque sources are black-listed.
    case BO_PtrMemD:
    case BO_PtrMemI:
      return IntRange::forValueOfType(C, GetExprType(E));

    // Bitwise-and uses the *infinum* of the two source ranges.
    case BO_And:
    case BO_AndAssign:
      return IntRange::meet(GetExprRange(C, BO->getLHS(), MaxWidth),
                            GetExprRange(C, BO->getRHS(), MaxWidth));

    // Left shift gets black-listed based on a judgement call.
    case BO_Shl:
      // ...except that we want to treat '1 << (blah)' as logically
      // positive.  It's an important idiom.
      if (IntegerLiteral *I
            = dyn_cast<IntegerLiteral>(BO->getLHS()->IgnoreParenCasts())) {
        if (I->getValue() == 1) {
          IntRange R = IntRange::forValueOfType(C, GetExprType(E));
          return IntRange(R.Width, /*NonNegative*/ true);
        }
      }
      LLVM_FALLTHROUGH;

    case BO_ShlAssign:
      return IntRange::forValueOfType(C, GetExprType(E));

    // Right shift by a constant can narrow its left argument.
    case BO_Shr:
    case BO_ShrAssign: {
      IntRange L = GetExprRange(C, BO->getLHS(), MaxWidth);

      // If the shift amount is a positive constant, drop the width by
      // that much.
      llvm::APSInt shift;
      if (BO->getRHS()->isIntegerConstantExpr(shift, C) &&
          shift.isNonNegative()) {
        unsigned zext = shift.getZExtValue();
        if (zext >= L.Width)
          L.Width = (L.NonNegative ? 0 : 1);
        else
          L.Width -= zext;
      }

      return L;
    }

    // Comma acts as its right operand.
    case BO_Comma:
      return GetExprRange(C, BO->getRHS(), MaxWidth);

    // Black-list pointer subtractions.
    case BO_Sub:
      if (BO->getLHS()->getType()->isPointerType())
        return IntRange::forValueOfType(C, GetExprType(E));
      break;

    // The width of a division result is mostly determined by the size
    // of the LHS.
    case BO_Div: {
      // Don't 'pre-truncate' the operands.
      unsigned opWidth = C.getIntWidth(GetExprType(E));
      IntRange L = GetExprRange(C, BO->getLHS(), opWidth);

      // If the divisor is constant, use that.
      llvm::APSInt divisor;
      if (BO->getRHS()->isIntegerConstantExpr(divisor, C)) {
        unsigned log2 = divisor.logBase2(); // floor(log_2(divisor))
        if (log2 >= L.Width)
          L.Width = (L.NonNegative ? 0 : 1);
        else
          L.Width = std::min(L.Width - log2, MaxWidth);
        return L;
      }

      // Otherwise, just use the LHS's width.
      IntRange R = GetExprRange(C, BO->getRHS(), opWidth);
      return IntRange(L.Width, L.NonNegative && R.NonNegative);
    }

    // The result of a remainder can't be larger than the result of
    // either side.
    case BO_Rem: {
      // Don't 'pre-truncate' the operands.
      unsigned opWidth = C.getIntWidth(GetExprType(E));
      IntRange L = GetExprRange(C, BO->getLHS(), opWidth);
      IntRange R = GetExprRange(C, BO->getRHS(), opWidth);

      IntRange meet = IntRange::meet(L, R);
      meet.Width = std::min(meet.Width, MaxWidth);
      return meet;
    }

    // The default behavior is okay for these.
    case BO_Mul:
    case BO_Add:
    case BO_Xor:
    case BO_Or:
      break;
    }

    // The default case is to treat the operation as if it were closed
    // on the narrowest type that encompasses both operands.
    IntRange L = GetExprRange(C, BO->getLHS(), MaxWidth);
    IntRange R = GetExprRange(C, BO->getRHS(), MaxWidth);
    return IntRange::join(L, R);
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    // Boolean-valued operations are white-listed.
    case UO_LNot:
      return IntRange::forBoolType();

    // Operations with opaque sources are black-listed.
    case UO_Deref:
    case UO_AddrOf: // should be impossible
      return IntRange::forValueOfType(C, GetExprType(E));

    default:
      return GetExprRange(C, UO->getSubExpr(), MaxWidth);
    }
  }

  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    return GetExprRange(C, OVE->getSourceExpr(), MaxWidth);

  if (const auto *BitField = E->getSourceBitField())
    return IntRange(BitField->getBitWidthValue(C),
                    BitField->getType()->isUnsignedIntegerOrEnumerationType());

  return IntRange::forValueOfType(C, GetExprType(E));
}

static IntRange GetExprRange(ASTContext &C, const Expr *E) {
  return GetExprRange(C, E, C.getIntWidth(GetExprType(E)));
}

/// Checks whether the given value, which currently has the given
/// source semantics, has the same value when coerced through the
/// target semantics.
static bool IsSameFloatAfterCast(const llvm::APFloat &value,
                                 const llvm::fltSemantics &Src,
                                 const llvm::fltSemantics &Tgt) {
  llvm::APFloat truncated = value;

  bool ignored;
  truncated.convert(Src, llvm::APFloat::rmNearestTiesToEven, &ignored);
  truncated.convert(Tgt, llvm::APFloat::rmNearestTiesToEven, &ignored);

  return truncated.bitwiseIsEqual(value);
}

/// Checks whether the given value, which currently has the given
/// source semantics, has the same value when coerced through the
/// target semantics.
///
/// The value might be a vector of floats (or a complex number).
static bool IsSameFloatAfterCast(const APValue &value,
                                 const llvm::fltSemantics &Src,
                                 const llvm::fltSemantics &Tgt) {
  if (value.isFloat())
    return IsSameFloatAfterCast(value.getFloat(), Src, Tgt);

  if (value.isVector()) {
    for (unsigned i = 0, e = value.getVectorLength(); i != e; ++i)
      if (!IsSameFloatAfterCast(value.getVectorElt(i), Src, Tgt))
        return false;
    return true;
  }

  assert(value.isComplexFloat());
  return (IsSameFloatAfterCast(value.getComplexFloatReal(), Src, Tgt) &&
          IsSameFloatAfterCast(value.getComplexFloatImag(), Src, Tgt));
}

static void AnalyzeImplicitConversions(Sema &S, Expr *E, SourceLocation CC);

static bool IsEnumConstOrFromMacro(Sema &S, Expr *E) {
  // Suppress cases where we are comparing against an enum constant.
  if (const DeclRefExpr *DR =
      dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    if (isa<EnumConstantDecl>(DR->getDecl()))
      return true;

  // Suppress cases where the '0' value is expanded from a macro.
  if (E->getBeginLoc().isMacroID())
    return true;

  return false;
}

static bool isKnownToHaveUnsignedValue(Expr *E) {
  return E->getType()->isIntegerType() &&
         (!E->getType()->isSignedIntegerType() ||
          !E->IgnoreParenImpCasts()->getType()->isSignedIntegerType());
}

namespace {
/// The promoted range of values of a type. In general this has the
/// following structure:
///
///     |-----------| . . . |-----------|
///     ^           ^       ^           ^
///    Min       HoleMin  HoleMax      Max
///
/// ... where there is only a hole if a signed type is promoted to unsigned
/// (in which case Min and Max are the smallest and largest representable
/// values).
struct PromotedRange {
  // Min, or HoleMax if there is a hole.
  llvm::APSInt PromotedMin;
  // Max, or HoleMin if there is a hole.
  llvm::APSInt PromotedMax;

  PromotedRange(IntRange R, unsigned BitWidth, bool Unsigned) {
    if (R.Width == 0)
      PromotedMin = PromotedMax = llvm::APSInt(BitWidth, Unsigned);
    else if (R.Width >= BitWidth && !Unsigned) {
      // Promotion made the type *narrower*. This happens when promoting
      // a < 32-bit unsigned / <= 32-bit signed bit-field to 'signed int'.
      // Treat all values of 'signed int' as being in range for now.
      PromotedMin = llvm::APSInt::getMinValue(BitWidth, Unsigned);
      PromotedMax = llvm::APSInt::getMaxValue(BitWidth, Unsigned);
    } else {
      PromotedMin = llvm::APSInt::getMinValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMin.setIsUnsigned(Unsigned);

      PromotedMax = llvm::APSInt::getMaxValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMax.setIsUnsigned(Unsigned);
    }
  }

  // Determine whether this range is contiguous (has no hole).
  bool isContiguous() const { return PromotedMin <= PromotedMax; }

  // Where a constant value is within the range.
  enum ComparisonResult {
    LT = 0x1,
    LE = 0x2,
    GT = 0x4,
    GE = 0x8,
    EQ = 0x10,
    NE = 0x20,
    InRangeFlag = 0x40,

    Less = LE | LT | NE,
    Min = LE | InRangeFlag,
    InRange = InRangeFlag,
    Max = GE | InRangeFlag,
    Greater = GE | GT | NE,

    OnlyValue = LE | GE | EQ | InRangeFlag,
    InHole = NE
  };

  ComparisonResult compare(const llvm::APSInt &Value) const {
    assert(Value.getBitWidth() == PromotedMin.getBitWidth() &&
           Value.isUnsigned() == PromotedMin.isUnsigned());
    if (!isContiguous()) {
      assert(Value.isUnsigned() && "discontiguous range for signed compare");
      if (Value.isMinValue()) return Min;
      if (Value.isMaxValue()) return Max;
      if (Value >= PromotedMin) return InRange;
      if (Value <= PromotedMax) return InRange;
      return InHole;
    }

    switch (llvm::APSInt::compareValues(Value, PromotedMin)) {
    case -1: return Less;
    case 0: return PromotedMin == PromotedMax ? OnlyValue : Min;
    case 1:
      switch (llvm::APSInt::compareValues(Value, PromotedMax)) {
      case -1: return InRange;
      case 0: return Max;
      case 1: return Greater;
      }
    }

    llvm_unreachable("impossible compare result");
  }

  static llvm::Optional<StringRef>
  constantValue(BinaryOperatorKind Op, ComparisonResult R, bool ConstantOnRHS) {
    if (Op == BO_Cmp) {
      ComparisonResult LTFlag = LT, GTFlag = GT;
      if (ConstantOnRHS) std::swap(LTFlag, GTFlag);

      if (R & EQ) return StringRef("'std::strong_ordering::equal'");
      if (R & LTFlag) return StringRef("'std::strong_ordering::less'");
      if (R & GTFlag) return StringRef("'std::strong_ordering::greater'");
      return llvm::None;
    }

    ComparisonResult TrueFlag, FalseFlag;
    if (Op == BO_EQ) {
      TrueFlag = EQ;
      FalseFlag = NE;
    } else if (Op == BO_NE) {
      TrueFlag = NE;
      FalseFlag = EQ;
    } else {
      if ((Op == BO_LT || Op == BO_GE) ^ ConstantOnRHS) {
        TrueFlag = LT;
        FalseFlag = GE;
      } else {
        TrueFlag = GT;
        FalseFlag = LE;
      }
      if (Op == BO_GE || Op == BO_LE)
        std::swap(TrueFlag, FalseFlag);
    }
    if (R & TrueFlag)
      return StringRef("true");
    if (R & FalseFlag)
      return StringRef("false");
    return llvm::None;
  }
};
}

static bool HasEnumType(Expr *E) {
  // Strip off implicit integral promotions.
  while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() != CK_IntegralCast &&
        ICE->getCastKind() != CK_NoOp)
      break;
    E = ICE->getSubExpr();
  }

  return E->getType()->isEnumeralType();
}

static int classifyConstantValue(Expr *Constant) {
  // The values of this enumeration are used in the diagnostics
  // diag::warn_out_of_range_compare and diag::warn_tautological_bool_compare.
  enum ConstantValueKind {
    Miscellaneous = 0,
    LiteralTrue,
    LiteralFalse
  };
  if (auto *BL = dyn_cast<CXXBoolLiteralExpr>(Constant))
    return BL->getValue() ? ConstantValueKind::LiteralTrue
                          : ConstantValueKind::LiteralFalse;
  return ConstantValueKind::Miscellaneous;
}

static bool CheckTautologicalComparison(Sema &S, BinaryOperator *E,
                                        Expr *Constant, Expr *Other,
                                        const llvm::APSInt &Value,
                                        bool RhsConstant) {
  if (S.inTemplateInstantiation())
    return false;

  Expr *OriginalOther = Other;

  Constant = Constant->IgnoreParenImpCasts();
  Other = Other->IgnoreParenImpCasts();

  // Suppress warnings on tautological comparisons between values of the same
  // enumeration type. There are only two ways we could warn on this:
  //  - If the constant is outside the range of representable values of
  //    the enumeration. In such a case, we should warn about the cast
  //    to enumeration type, not about the comparison.
  //  - If the constant is the maximum / minimum in-range value. For an
  //    enumeratin type, such comparisons can be meaningful and useful.
  if (Constant->getType()->isEnumeralType() &&
      S.Context.hasSameUnqualifiedType(Constant->getType(), Other->getType()))
    return false;

  // TODO: Investigate using GetExprRange() to get tighter bounds
  // on the bit ranges.
  QualType OtherT = Other->getType();
  if (const auto *AT = OtherT->getAs<AtomicType>())
    OtherT = AT->getValueType();
  IntRange OtherRange = IntRange::forValueOfType(S.Context, OtherT);

  // Whether we're treating Other as being a bool because of the form of
  // expression despite it having another type (typically 'int' in C).
  bool OtherIsBooleanDespiteType =
      !OtherT->isBooleanType() && Other->isKnownToHaveBooleanValue();
  if (OtherIsBooleanDespiteType)
    OtherRange = IntRange::forBoolType();

  // Determine the promoted range of the other type and see if a comparison of
  // the constant against that range is tautological.
  PromotedRange OtherPromotedRange(OtherRange, Value.getBitWidth(),
                                   Value.isUnsigned());
  auto Cmp = OtherPromotedRange.compare(Value);
  auto Result = PromotedRange::constantValue(E->getOpcode(), Cmp, RhsConstant);
  if (!Result)
    return false;

  // Suppress the diagnostic for an in-range comparison if the constant comes
  // from a macro or enumerator. We don't want to diagnose
  //
  //   some_long_value <= INT_MAX
  //
  // when sizeof(int) == sizeof(long).
  bool InRange = Cmp & PromotedRange::InRangeFlag;
  if (InRange && IsEnumConstOrFromMacro(S, Constant))
    return false;

  // If this is a comparison to an enum constant, include that
  // constant in the diagnostic.
  const EnumConstantDecl *ED = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Constant))
    ED = dyn_cast<EnumConstantDecl>(DR->getDecl());

  // Should be enough for uint128 (39 decimal digits)
  SmallString<64> PrettySourceValue;
  llvm::raw_svector_ostream OS(PrettySourceValue);
  if (ED)
    OS << '\'' << *ED << "' (" << Value << ")";
  else
    OS << Value;

  // FIXME: We use a somewhat different formatting for the in-range cases and
  // cases involving boolean values for historical reasons. We should pick a
  // consistent way of presenting these diagnostics.
  if (!InRange || Other->isKnownToHaveBooleanValue()) {
    S.DiagRuntimeBehavior(
      E->getOperatorLoc(), E,
      S.PDiag(!InRange ? diag::warn_out_of_range_compare
                       : diag::warn_tautological_bool_compare)
          << OS.str() << classifyConstantValue(Constant)
          << OtherT << OtherIsBooleanDespiteType << *Result
          << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange());
  } else {
    unsigned Diag = (isKnownToHaveUnsignedValue(OriginalOther) && Value == 0)
                        ? (HasEnumType(OriginalOther)
                               ? diag::warn_unsigned_enum_always_true_comparison
                               : diag::warn_unsigned_always_true_comparison)
                        : diag::warn_tautological_constant_compare;

    S.Diag(E->getOperatorLoc(), Diag)
        << RhsConstant << OtherT << E->getOpcodeStr() << OS.str() << *Result
        << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange();
  }

  return true;
}

/// Analyze the operands of the given comparison.  Implements the
/// fallback case from AnalyzeComparison.
static void AnalyzeImpConvsInComparison(Sema &S, BinaryOperator *E) {
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());
}

/// Implements -Wsign-compare.
///
/// \param E the binary operator to check for warnings
static void AnalyzeComparison(Sema &S, BinaryOperator *E) {
  // The type the comparison is being performed in.
  QualType T = E->getLHS()->getType();

  // Only analyze comparison operators where both sides have been converted to
  // the same type.
  if (!S.Context.hasSameUnqualifiedType(T, E->getRHS()->getType()))
    return AnalyzeImpConvsInComparison(S, E);

  // Don't analyze value-dependent comparisons directly.
  if (E->isValueDependent())
    return AnalyzeImpConvsInComparison(S, E);

  Expr *LHS = E->getLHS();
  Expr *RHS = E->getRHS();

  if (T->isIntegralType(S.Context)) {
    llvm::APSInt RHSValue;
    llvm::APSInt LHSValue;

    bool IsRHSIntegralLiteral = RHS->isIntegerConstantExpr(RHSValue, S.Context);
    bool IsLHSIntegralLiteral = LHS->isIntegerConstantExpr(LHSValue, S.Context);

    // We don't care about expressions whose result is a constant.
    if (IsRHSIntegralLiteral && IsLHSIntegralLiteral)
      return AnalyzeImpConvsInComparison(S, E);

    // We only care about expressions where just one side is literal
    if (IsRHSIntegralLiteral ^ IsLHSIntegralLiteral) {
      // Is the constant on the RHS or LHS?
      const bool RhsConstant = IsRHSIntegralLiteral;
      Expr *Const = RhsConstant ? RHS : LHS;
      Expr *Other = RhsConstant ? LHS : RHS;
      const llvm::APSInt &Value = RhsConstant ? RHSValue : LHSValue;

      // Check whether an integer constant comparison results in a value
      // of 'true' or 'false'.
      if (CheckTautologicalComparison(S, E, Const, Other, Value, RhsConstant))
        return AnalyzeImpConvsInComparison(S, E);
    }
  }

  if (!T->hasUnsignedIntegerRepresentation()) {
    // We don't do anything special if this isn't an unsigned integral
    // comparison:  we're only interested in integral comparisons, and
    // signed comparisons only happen in cases we don't care to warn about.
    return AnalyzeImpConvsInComparison(S, E);
  }

  LHS = LHS->IgnoreParenImpCasts();
  RHS = RHS->IgnoreParenImpCasts();

  if (!S.getLangOpts().CPlusPlus) {
    // Avoid warning about comparison of integers with different signs when
    // RHS/LHS has a `typeof(E)` type whose sign is different from the sign of
    // the type of `E`.
    if (const auto *TET = dyn_cast<TypeOfExprType>(LHS->getType()))
      LHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();
    if (const auto *TET = dyn_cast<TypeOfExprType>(RHS->getType()))
      RHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();
  }

  // Check to see if one of the (unmodified) operands is of different
  // signedness.
  Expr *signedOperand, *unsignedOperand;
  if (LHS->getType()->hasSignedIntegerRepresentation()) {
    assert(!RHS->getType()->hasSignedIntegerRepresentation() &&
           "unsigned comparison between two signed integer expressions?");
    signedOperand = LHS;
    unsignedOperand = RHS;
  } else if (RHS->getType()->hasSignedIntegerRepresentation()) {
    signedOperand = RHS;
    unsignedOperand = LHS;
  } else {
    return AnalyzeImpConvsInComparison(S, E);
  }

  // Otherwise, calculate the effective range of the signed operand.
  IntRange signedRange = GetExprRange(S.Context, signedOperand);

  // Go ahead and analyze implicit conversions in the operands.  Note
  // that we skip the implicit conversions on both sides.
  AnalyzeImplicitConversions(S, LHS, E->getOperatorLoc());
  AnalyzeImplicitConversions(S, RHS, E->getOperatorLoc());

  // If the signed range is non-negative, -Wsign-compare won't fire.
  if (signedRange.NonNegative)
    return;

  // For (in)equality comparisons, if the unsigned operand is a
  // constant which cannot collide with a overflowed signed operand,
  // then reinterpreting the signed operand as unsigned will not
  // change the result of the comparison.
  if (E->isEqualityOp()) {
    unsigned comparisonWidth = S.Context.getIntWidth(T);
    IntRange unsignedRange = GetExprRange(S.Context, unsignedOperand);

    // We should never be unable to prove that the unsigned operand is
    // non-negative.
    assert(unsignedRange.NonNegative && "unsigned range includes negative?");

    if (unsignedRange.Width < comparisonWidth)
      return;
  }

  S.DiagRuntimeBehavior(E->getOperatorLoc(), E,
    S.PDiag(diag::warn_mixed_sign_comparison)
      << LHS->getType() << RHS->getType()
      << LHS->getSourceRange() << RHS->getSourceRange());
}

/// Analyzes an attempt to assign the given value to a bitfield.
///
/// Returns true if there was something fishy about the attempt.
static bool AnalyzeBitFieldAssignment(Sema &S, FieldDecl *Bitfield, Expr *Init,
                                      SourceLocation InitLoc) {
  assert(Bitfield->isBitField());
  if (Bitfield->isInvalidDecl())
    return false;

  // White-list bool bitfields.
  QualType BitfieldType = Bitfield->getType();
  if (BitfieldType->isBooleanType())
     return false;

  if (BitfieldType->isEnumeralType()) {
    EnumDecl *BitfieldEnumDecl = BitfieldType->getAs<EnumType>()->getDecl();
    // If the underlying enum type was not explicitly specified as an unsigned
    // type and the enum contain only positive values, MSVC++ will cause an
    // inconsistency by storing this as a signed type.
    if (S.getLangOpts().CPlusPlus11 &&
        !BitfieldEnumDecl->getIntegerTypeSourceInfo() &&
        BitfieldEnumDecl->getNumPositiveBits() > 0 &&
        BitfieldEnumDecl->getNumNegativeBits() == 0) {
      S.Diag(InitLoc, diag::warn_no_underlying_type_specified_for_enum_bitfield)
        << BitfieldEnumDecl->getNameAsString();
    }
  }

  if (Bitfield->getType()->isBooleanType())
    return false;

  // Ignore value- or type-dependent expressions.
  if (Bitfield->getBitWidth()->isValueDependent() ||
      Bitfield->getBitWidth()->isTypeDependent() ||
      Init->isValueDependent() ||
      Init->isTypeDependent())
    return false;

  Expr *OriginalInit = Init->IgnoreParenImpCasts();
  unsigned FieldWidth = Bitfield->getBitWidthValue(S.Context);

  Expr::EvalResult Result;
  if (!OriginalInit->EvaluateAsInt(Result, S.Context,
                                   Expr::SE_AllowSideEffects)) {
    // The RHS is not constant.  If the RHS has an enum type, make sure the
    // bitfield is wide enough to hold all the values of the enum without
    // truncation.
    if (const auto *EnumTy = OriginalInit->getType()->getAs<EnumType>()) {
      EnumDecl *ED = EnumTy->getDecl();
      bool SignedBitfield = BitfieldType->isSignedIntegerType();

      // Enum types are implicitly signed on Windows, so check if there are any
      // negative enumerators to see if the enum was intended to be signed or
      // not.
      bool SignedEnum = ED->getNumNegativeBits() > 0;

      // Check for surprising sign changes when assigning enum values to a
      // bitfield of different signedness.  If the bitfield is signed and we
      // have exactly the right number of bits to store this unsigned enum,
      // suggest changing the enum to an unsigned type. This typically happens
      // on Windows where unfixed enums always use an underlying type of 'int'.
      unsigned DiagID = 0;
      if (SignedEnum && !SignedBitfield) {
        DiagID = diag::warn_unsigned_bitfield_assigned_signed_enum;
      } else if (SignedBitfield && !SignedEnum &&
                 ED->getNumPositiveBits() == FieldWidth) {
        DiagID = diag::warn_signed_bitfield_enum_conversion;
      }

      if (DiagID) {
        S.Diag(InitLoc, DiagID) << Bitfield << ED;
        TypeSourceInfo *TSI = Bitfield->getTypeSourceInfo();
        SourceRange TypeRange =
            TSI ? TSI->getTypeLoc().getSourceRange() : SourceRange();
        S.Diag(Bitfield->getTypeSpecStartLoc(), diag::note_change_bitfield_sign)
            << SignedEnum << TypeRange;
      }

      // Compute the required bitwidth. If the enum has negative values, we need
      // one more bit than the normal number of positive bits to represent the
      // sign bit.
      unsigned BitsNeeded = SignedEnum ? std::max(ED->getNumPositiveBits() + 1,
                                                  ED->getNumNegativeBits())
                                       : ED->getNumPositiveBits();

      // Check the bitwidth.
      if (BitsNeeded > FieldWidth) {
        Expr *WidthExpr = Bitfield->getBitWidth();
        S.Diag(InitLoc, diag::warn_bitfield_too_small_for_enum)
            << Bitfield << ED;
        S.Diag(WidthExpr->getExprLoc(), diag::note_widen_bitfield)
            << BitsNeeded << ED << WidthExpr->getSourceRange();
      }
    }

    return false;
  }

  llvm::APSInt Value = Result.Val.getInt();

  unsigned OriginalWidth = Value.getBitWidth();

  if (!Value.isSigned() || Value.isNegative())
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(OriginalInit))
      if (UO->getOpcode() == UO_Minus || UO->getOpcode() == UO_Not)
        OriginalWidth = Value.getMinSignedBits();

  if (OriginalWidth <= FieldWidth)
    return false;

  // Compute the value which the bitfield will contain.
  llvm::APSInt TruncatedValue = Value.trunc(FieldWidth);
  TruncatedValue.setIsSigned(BitfieldType->isSignedIntegerType());

  // Check whether the stored value is equal to the original value.
  TruncatedValue = TruncatedValue.extend(OriginalWidth);
  if (llvm::APSInt::isSameValue(Value, TruncatedValue))
    return false;

  // Special-case bitfields of width 1: booleans are naturally 0/1, and
  // therefore don't strictly fit into a signed bitfield of width 1.
  if (FieldWidth == 1 && Value == 1)
    return false;

  std::string PrettyValue = Value.toString(10);
  std::string PrettyTrunc = TruncatedValue.toString(10);

  S.Diag(InitLoc, diag::warn_impcast_bitfield_precision_constant)
    << PrettyValue << PrettyTrunc << OriginalInit->getType()
    << Init->getSourceRange();

  return true;
}

/// Analyze the given simple or compound assignment for warning-worthy
/// operations.
static void AnalyzeAssignment(Sema &S, BinaryOperator *E) {
  // Just recurse on the LHS.
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());

  // We want to recurse on the RHS as normal unless we're assigning to
  // a bitfield.
  if (FieldDecl *Bitfield = E->getLHS()->getSourceBitField()) {
    if (AnalyzeBitFieldAssignment(S, Bitfield, E->getRHS(),
                                  E->getOperatorLoc())) {
      // Recurse, ignoring any implicit conversions on the RHS.
      return AnalyzeImplicitConversions(S, E->getRHS()->IgnoreParenImpCasts(),
                                        E->getOperatorLoc());
    }
  }

  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  // Diagnose implicitly sequentially-consistent atomic assignment.
  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getRHS()->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
}

/// Diagnose an implicit cast;  purely a helper for CheckImplicitConversion.
static void DiagnoseImpCast(Sema &S, Expr *E, QualType SourceType, QualType T,
                            SourceLocation CContext, unsigned diag,
                            bool pruneControlFlow = false) {
  if (pruneControlFlow) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(diag)
                            << SourceType << T << E->getSourceRange()
                            << SourceRange(CContext));
    return;
  }
  S.Diag(E->getExprLoc(), diag)
    << SourceType << T << E->getSourceRange() << SourceRange(CContext);
}

/// Diagnose an implicit cast;  purely a helper for CheckImplicitConversion.
static void DiagnoseImpCast(Sema &S, Expr *E, QualType T,
                            SourceLocation CContext,
                            unsigned diag, bool pruneControlFlow = false) {
  DiagnoseImpCast(S, E, E->getType(), T, CContext, diag, pruneControlFlow);
}

/// Diagnose an implicit cast from a floating point value to an integer value.
static void DiagnoseFloatingImpCast(Sema &S, Expr *E, QualType T,
                                    SourceLocation CContext) {
  const bool IsBool = T->isSpecificBuiltinType(BuiltinType::Bool);
  const bool PruneWarnings = S.inTemplateInstantiation();

  Expr *InnerE = E->IgnoreParenImpCasts();
  // We also want to warn on, e.g., "int i = -1.234"
  if (UnaryOperator *UOp = dyn_cast<UnaryOperator>(InnerE))
    if (UOp->getOpcode() == UO_Minus || UOp->getOpcode() == UO_Plus)
      InnerE = UOp->getSubExpr()->IgnoreParenImpCasts();

  const bool IsLiteral =
      isa<FloatingLiteral>(E) || isa<FloatingLiteral>(InnerE);

  llvm::APFloat Value(0.0);
  bool IsConstant =
    E->EvaluateAsFloat(Value, S.Context, Expr::SE_AllowSideEffects);
  if (!IsConstant) {
    return DiagnoseImpCast(S, E, T, CContext,
                           diag::warn_impcast_float_integer, PruneWarnings);
  }

  bool isExact = false;

  llvm::APSInt IntegerValue(S.Context.getIntWidth(T),
                            T->hasUnsignedIntegerRepresentation());
  llvm::APFloat::opStatus Result = Value.convertToInteger(
      IntegerValue, llvm::APFloat::rmTowardZero, &isExact);

  if (Result == llvm::APFloat::opOK && isExact) {
    if (IsLiteral) return;
    return DiagnoseImpCast(S, E, T, CContext, diag::warn_impcast_float_integer,
                           PruneWarnings);
  }

  // Conversion of a floating-point value to a non-bool integer where the
  // integral part cannot be represented by the integer type is undefined.
  if (!IsBool && Result == llvm::APFloat::opInvalidOp)
    return DiagnoseImpCast(
        S, E, T, CContext,
        IsLiteral ? diag::warn_impcast_literal_float_to_integer_out_of_range
                  : diag::warn_impcast_float_to_integer_out_of_range,
        PruneWarnings);

  unsigned DiagID = 0;
  if (IsLiteral) {
    // Warn on floating point literal to integer.
    DiagID = diag::warn_impcast_literal_float_to_integer;
  } else if (IntegerValue == 0) {
    if (Value.isZero()) {  // Skip -0.0 to 0 conversion.
      return DiagnoseImpCast(S, E, T, CContext,
                             diag::warn_impcast_float_integer, PruneWarnings);
    }
    // Warn on non-zero to zero conversion.
    DiagID = diag::warn_impcast_float_to_integer_zero;
  } else {
    if (IntegerValue.isUnsigned()) {
      if (!IntegerValue.isMaxValue()) {
        return DiagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    } else {  // IntegerValue.isSigned()
      if (!IntegerValue.isMaxSignedValue() &&
          !IntegerValue.isMinSignedValue()) {
        return DiagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    }
    // Warn on evaluatable floating point expression to integer conversion.
    DiagID = diag::warn_impcast_float_to_integer;
  }

  // FIXME: Force the precision of the source value down so we don't print
  // digits which are usually useless (we don't really care here if we
  // truncate a digit by accident in edge cases).  Ideally, APFloat::toString
  // would automatically print the shortest representation, but it's a bit
  // tricky to implement.
  SmallString<16> PrettySourceValue;
  unsigned precision = llvm::APFloat::semanticsPrecision(Value.getSemantics());
  precision = (precision * 59 + 195) / 196;
  Value.toString(PrettySourceValue, precision);

  SmallString<16> PrettyTargetValue;
  if (IsBool)
    PrettyTargetValue = Value.isZero() ? "false" : "true";
  else
    IntegerValue.toString(PrettyTargetValue);

  if (PruneWarnings) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(DiagID)
                              << E->getType() << T.getUnqualifiedType()
                              << PrettySourceValue << PrettyTargetValue
                              << E->getSourceRange() << SourceRange(CContext));
  } else {
    S.Diag(E->getExprLoc(), DiagID)
        << E->getType() << T.getUnqualifiedType() << PrettySourceValue
        << PrettyTargetValue << E->getSourceRange() << SourceRange(CContext);
  }
}

/// Analyze the given compound assignment for the possible losing of
/// floating-point precision.
static void AnalyzeCompoundAssignment(Sema &S, BinaryOperator *E) {
  assert(isa<CompoundAssignOperator>(E) &&
         "Must be compound assignment operation");
  // Recurse on the LHS and RHS in here
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getOperatorLoc(), diag::warn_atomic_implicit_seq_cst);

  // Now check the outermost expression
  const auto *ResultBT = E->getLHS()->getType()->getAs<BuiltinType>();
  const auto *RBT = cast<CompoundAssignOperator>(E)
                        ->getComputationResultType()
                        ->getAs<BuiltinType>();

  // The below checks assume source is floating point.
  if (!ResultBT || !RBT || !RBT->isFloatingPoint()) return;

  // If source is floating point but target is an integer.
  if (ResultBT->isInteger())
    DiagnoseImpCast(S, E, E->getRHS()->getType(), E->getLHS()->getType(),
                    E->getExprLoc(), diag::warn_impcast_float_integer);
  // If both source and target are floating points. Builtin FP kinds are ordered
  // by increasing FP rank. FIXME: except _Float16, we currently emit a bogus
  // warning.
  else if (ResultBT->isFloatingPoint() && ResultBT->getKind() < RBT->getKind() &&
           // We don't want to warn for system macro.
           !S.SourceMgr.isInSystemMacro(E->getOperatorLoc()))
    // warn about dropping FP rank.
    DiagnoseImpCast(S, E->getRHS(), E->getLHS()->getType(), E->getOperatorLoc(),
                    diag::warn_impcast_float_result_precision);
}

static std::string PrettyPrintInRange(const llvm::APSInt &Value,
                                      IntRange Range) {
  if (!Range.Width) return "0";

  llvm::APSInt ValueInRange = Value;
  ValueInRange.setIsSigned(!Range.NonNegative);
  ValueInRange = ValueInRange.trunc(Range.Width);
  return ValueInRange.toString(10);
}

static bool IsImplicitBoolFloatConversion(Sema &S, Expr *Ex, bool ToBool) {
  if (!isa<ImplicitCastExpr>(Ex))
    return false;

  Expr *InnerE = Ex->IgnoreParenImpCasts();
  const Type *Target = S.Context.getCanonicalType(Ex->getType()).getTypePtr();
  const Type *Source =
    S.Context.getCanonicalType(InnerE->getType()).getTypePtr();
  if (Target->isDependentType())
    return false;

  const BuiltinType *FloatCandidateBT =
    dyn_cast<BuiltinType>(ToBool ? Source : Target);
  const Type *BoolCandidateType = ToBool ? Target : Source;

  return (BoolCandidateType->isSpecificBuiltinType(BuiltinType::Bool) &&
          FloatCandidateBT && (FloatCandidateBT->isFloatingPoint()));
}

static void CheckImplicitArgumentConversions(Sema &S, CallExpr *TheCall,
                                             SourceLocation CC) {
  unsigned NumArgs = TheCall->getNumArgs();
  for (unsigned i = 0; i < NumArgs; ++i) {
    Expr *CurrA = TheCall->getArg(i);
    if (!IsImplicitBoolFloatConversion(S, CurrA, true))
      continue;

    bool IsSwapped = ((i > 0) &&
        IsImplicitBoolFloatConversion(S, TheCall->getArg(i - 1), false));
    IsSwapped |= ((i < (NumArgs - 1)) &&
        IsImplicitBoolFloatConversion(S, TheCall->getArg(i + 1), false));
    if (IsSwapped) {
      // Warn on this floating-point to bool conversion.
      DiagnoseImpCast(S, CurrA->IgnoreParenImpCasts(),
                      CurrA->getType(), CC,
                      diag::warn_impcast_floating_point_to_bool);
    }
  }
}

static void DiagnoseNullConversion(Sema &S, Expr *E, QualType T,
                                   SourceLocation CC) {
  if (S.Diags.isIgnored(diag::warn_impcast_null_pointer_to_integer,
                        E->getExprLoc()))
    return;

  // Don't warn on functions which have return type nullptr_t.
  if (isa<CallExpr>(E))
    return;

  // Check for NULL (GNUNull) or nullptr (CXX11_nullptr).
  const Expr::NullPointerConstantKind NullKind =
      E->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull);
  if (NullKind != Expr::NPCK_GNUNull && NullKind != Expr::NPCK_CXX11_nullptr)
    return;

  // Return if target type is a safe conversion.
  if (T->isAnyPointerType() || T->isBlockPointerType() ||
      T->isMemberPointerType() || !T->isScalarType() || T->isNullPtrType())
    return;

  SourceLocation Loc = E->getSourceRange().getBegin();

  // Venture through the macro stacks to get to the source of macro arguments.
  // The new location is a better location than the complete location that was
  // passed in.
  Loc = S.SourceMgr.getTopMacroCallerLoc(Loc);
  CC = S.SourceMgr.getTopMacroCallerLoc(CC);

  // __null is usually wrapped in a macro.  Go up a macro if that is the case.
  if (NullKind == Expr::NPCK_GNUNull && Loc.isMacroID()) {
    StringRef MacroName = Lexer::getImmediateMacroNameForDiagnostics(
        Loc, S.SourceMgr, S.getLangOpts());
    if (MacroName == "NULL")
      Loc = S.SourceMgr.getImmediateExpansionRange(Loc).getBegin();
  }

  // Only warn if the null and context location are in the same macro expansion.
  if (S.SourceMgr.getFileID(Loc) != S.SourceMgr.getFileID(CC))
    return;

  S.Diag(Loc, diag::warn_impcast_null_pointer_to_integer)
      << (NullKind == Expr::NPCK_CXX11_nullptr) << T << SourceRange(CC)
      << FixItHint::CreateReplacement(Loc,
                                      S.getFixItZeroLiteralForType(T, Loc));
}

static void checkObjCArrayLiteral(Sema &S, QualType TargetType,
                                  ObjCArrayLiteral *ArrayLiteral);

static void
checkObjCDictionaryLiteral(Sema &S, QualType TargetType,
                           ObjCDictionaryLiteral *DictionaryLiteral);

/// Check a single element within a collection literal against the
/// target element type.
static void checkObjCCollectionLiteralElement(Sema &S,
                                              QualType TargetElementType,
                                              Expr *Element,
                                              unsigned ElementKind) {
  // Skip a bitcast to 'id' or qualified 'id'.
  if (auto ICE = dyn_cast<ImplicitCastExpr>(Element)) {
    if (ICE->getCastKind() == CK_BitCast &&
        ICE->getSubExpr()->getType()->getAs<ObjCObjectPointerType>())
      Element = ICE->getSubExpr();
  }

  QualType ElementType = Element->getType();
  ExprResult ElementResult(Element);
  if (ElementType->getAs<ObjCObjectPointerType>() &&
      S.CheckSingleAssignmentConstraints(TargetElementType,
                                         ElementResult,
                                         false, false)
        != Sema::Compatible) {
    S.Diag(Element->getBeginLoc(), diag::warn_objc_collection_literal_element)
        << ElementType << ElementKind << TargetElementType
        << Element->getSourceRange();
  }

  if (auto ArrayLiteral = dyn_cast<ObjCArrayLiteral>(Element))
    checkObjCArrayLiteral(S, TargetElementType, ArrayLiteral);
  else if (auto DictionaryLiteral = dyn_cast<ObjCDictionaryLiteral>(Element))
    checkObjCDictionaryLiteral(S, TargetElementType, DictionaryLiteral);
}

/// Check an Objective-C array literal being converted to the given
/// target type.
static void checkObjCArrayLiteral(Sema &S, QualType TargetType,
                                  ObjCArrayLiteral *ArrayLiteral) {
  if (!S.NSArrayDecl)
    return;

  const auto *TargetObjCPtr = TargetType->getAs<ObjCObjectPointerType>();
  if (!TargetObjCPtr)
    return;

  if (TargetObjCPtr->isUnspecialized() ||
      TargetObjCPtr->getInterfaceDecl()->getCanonicalDecl()
        != S.NSArrayDecl->getCanonicalDecl())
    return;

  auto TypeArgs = TargetObjCPtr->getTypeArgs();
  if (TypeArgs.size() != 1)
    return;

  QualType TargetElementType = TypeArgs[0];
  for (unsigned I = 0, N = ArrayLiteral->getNumElements(); I != N; ++I) {
    checkObjCCollectionLiteralElement(S, TargetElementType,
                                      ArrayLiteral->getElement(I),
                                      0);
  }
}

/// Check an Objective-C dictionary literal being converted to the given
/// target type.
static void
checkObjCDictionaryLiteral(Sema &S, QualType TargetType,
                           ObjCDictionaryLiteral *DictionaryLiteral) {
  if (!S.NSDictionaryDecl)
    return;

  const auto *TargetObjCPtr = TargetType->getAs<ObjCObjectPointerType>();
  if (!TargetObjCPtr)
    return;

  if (TargetObjCPtr->isUnspecialized() ||
      TargetObjCPtr->getInterfaceDecl()->getCanonicalDecl()
        != S.NSDictionaryDecl->getCanonicalDecl())
    return;

  auto TypeArgs = TargetObjCPtr->getTypeArgs();
  if (TypeArgs.size() != 2)
    return;

  QualType TargetKeyType = TypeArgs[0];
  QualType TargetObjectType = TypeArgs[1];
  for (unsigned I = 0, N = DictionaryLiteral->getNumElements(); I != N; ++I) {
    auto Element = DictionaryLiteral->getKeyValueElement(I);
    checkObjCCollectionLiteralElement(S, TargetKeyType, Element.Key, 1);
    checkObjCCollectionLiteralElement(S, TargetObjectType, Element.Value, 2);
  }
}

// Helper function to filter out cases for constant width constant conversion.
// Don't warn on char array initialization or for non-decimal values.
static bool isSameWidthConstantConversion(Sema &S, Expr *E, QualType T,
                                          SourceLocation CC) {
  // If initializing from a constant, and the constant starts with '0',
  // then it is a binary, octal, or hexadecimal.  Allow these constants
  // to fill all the bits, even if there is a sign change.
  if (auto *IntLit = dyn_cast<IntegerLiteral>(E->IgnoreParenImpCasts())) {
    const char FirstLiteralCharacter =
        S.getSourceManager().getCharacterData(IntLit->getBeginLoc())[0];
    if (FirstLiteralCharacter == '0')
      return false;
  }

  // If the CC location points to a '{', and the type is char, then assume
  // assume it is an array initialization.
  if (CC.isValid() && T->isCharType()) {
    const char FirstContextCharacter =
        S.getSourceManager().getCharacterData(CC)[0];
    if (FirstContextCharacter == '{')
      return false;
  }

  return true;
}

static void
CheckImplicitConversion(Sema &S, Expr *E, QualType T, SourceLocation CC,
                        bool *ICContext = nullptr) {
  if (E->isTypeDependent() || E->isValueDependent()) return;

  const Type *Source = S.Context.getCanonicalType(E->getType()).getTypePtr();
  const Type *Target = S.Context.getCanonicalType(T).getTypePtr();
  if (Source == Target) return;
  if (Target->isDependentType()) return;

  // If the conversion context location is invalid don't complain. We also
  // don't want to emit a warning if the issue occurs from the expansion of
  // a system macro. The problem is that 'getSpellingLoc()' is slow, so we
  // delay this check as long as possible. Once we detect we are in that
  // scenario, we just return.
  if (CC.isInvalid())
    return;

  if (Source->isAtomicType())
    S.Diag(E->getExprLoc(), diag::warn_atomic_implicit_seq_cst);

  // Diagnose implicit casts to bool.
  if (Target->isSpecificBuiltinType(BuiltinType::Bool)) {
    if (isa<StringLiteral>(E))
      // Warn on string literal to bool.  Checks for string literals in logical
      // and expressions, for instance, assert(0 && "error here"), are
      // prevented by a check in AnalyzeImplicitConversions().
      return DiagnoseImpCast(S, E, T, CC,
                             diag::warn_impcast_string_literal_to_bool);
    if (isa<ObjCStringLiteral>(E) || isa<ObjCArrayLiteral>(E) ||
        isa<ObjCDictionaryLiteral>(E) || isa<ObjCBoxedExpr>(E)) {
      // This covers the literal expressions that evaluate to Objective-C
      // objects.
      return DiagnoseImpCast(S, E, T, CC,
                             diag::warn_impcast_objective_c_literal_to_bool);
    }
    if (Source->isPointerType() || Source->canDecayToPointerType()) {
      // Warn on pointer to bool conversion that is always true.
      S.DiagnoseAlwaysNonNullPointer(E, Expr::NPCK_NotNull, /*IsEqual*/ false,
                                     SourceRange(CC));
    }
  }

  // Check implicit casts from Objective-C collection literals to specialized
  // collection types, e.g., NSArray<NSString *> *.
  if (auto *ArrayLiteral = dyn_cast<ObjCArrayLiteral>(E))
    checkObjCArrayLiteral(S, QualType(Target, 0), ArrayLiteral);
  else if (auto *DictionaryLiteral = dyn_cast<ObjCDictionaryLiteral>(E))
    checkObjCDictionaryLiteral(S, QualType(Target, 0), DictionaryLiteral);

  // Strip vector types.
  if (isa<VectorType>(Source)) {
    if (!isa<VectorType>(Target)) {
      if (S.SourceMgr.isInSystemMacro(CC))
        return;
      return DiagnoseImpCast(S, E, T, CC, diag::warn_impcast_vector_scalar);
    }

    // If the vector cast is cast between two vectors of the same size, it is
    // a bitcast, not a conversion.
    if (S.Context.getTypeSize(Source) == S.Context.getTypeSize(Target))
      return;

    Source = cast<VectorType>(Source)->getElementType().getTypePtr();
    Target = cast<VectorType>(Target)->getElementType().getTypePtr();
  }
  if (auto VecTy = dyn_cast<VectorType>(Target))
    Target = VecTy->getElementType().getTypePtr();

  // Strip complex types.
  if (isa<ComplexType>(Source)) {
    if (!isa<ComplexType>(Target)) {
      if (S.SourceMgr.isInSystemMacro(CC) || Target->isBooleanType())
        return;

      return DiagnoseImpCast(S, E, T, CC,
                             S.getLangOpts().CPlusPlus
                                 ? diag::err_impcast_complex_scalar
                                 : diag::warn_impcast_complex_scalar);
    }

    Source = cast<ComplexType>(Source)->getElementType().getTypePtr();
    Target = cast<ComplexType>(Target)->getElementType().getTypePtr();
  }

  const BuiltinType *SourceBT = dyn_cast<BuiltinType>(Source);
  const BuiltinType *TargetBT = dyn_cast<BuiltinType>(Target);

  // If the source is floating point...
  if (SourceBT && SourceBT->isFloatingPoint()) {
    // ...and the target is floating point...
    if (TargetBT && TargetBT->isFloatingPoint()) {
      // ...then warn if we're dropping FP rank.

      // Builtin FP kinds are ordered by increasing FP rank.
      if (SourceBT->getKind() > TargetBT->getKind()) {
        // Don't warn about float constants that are precisely
        // representable in the target type.
        Expr::EvalResult result;
        if (E->EvaluateAsRValue(result, S.Context)) {
          // Value might be a float, a float vector, or a float complex.
          if (IsSameFloatAfterCast(result.Val,
                   S.Context.getFloatTypeSemantics(QualType(TargetBT, 0)),
                   S.Context.getFloatTypeSemantics(QualType(SourceBT, 0))))
            return;
        }

        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        DiagnoseImpCast(S, E, T, CC, diag::warn_impcast_float_precision);
      }
      // ... or possibly if we're increasing rank, too
      else if (TargetBT->getKind() > SourceBT->getKind()) {
        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        DiagnoseImpCast(S, E, T, CC, diag::warn_impcast_double_promotion);
      }
      return;
    }

    // If the target is integral, always warn.
    if (TargetBT && TargetBT->isInteger()) {
      if (S.SourceMgr.isInSystemMacro(CC))
        return;

      DiagnoseFloatingImpCast(S, E, T, CC);
    }

    // Detect the case where a call result is converted from floating-point to
    // to bool, and the final argument to the call is converted from bool, to
    // discover this typo:
    //
    //    bool b = fabs(x < 1.0);  // should be "bool b = fabs(x) < 1.0;"
    //
    // FIXME: This is an incredibly special case; is there some more general
    // way to detect this class of misplaced-parentheses bug?
    if (Target->isBooleanType() && isa<CallExpr>(E)) {
      // Check last argument of function call to see if it is an
      // implicit cast from a type matching the type the result
      // is being cast to.
      CallExpr *CEx = cast<CallExpr>(E);
      if (unsigned NumArgs = CEx->getNumArgs()) {
        Expr *LastA = CEx->getArg(NumArgs - 1);
        Expr *InnerE = LastA->IgnoreParenImpCasts();
        if (isa<ImplicitCastExpr>(LastA) &&
            InnerE->getType()->isBooleanType()) {
          // Warn on this floating-point to bool conversion
          DiagnoseImpCast(S, E, T, CC,
                          diag::warn_impcast_floating_point_to_bool);
        }
      }
    }
    return;
  }

  DiagnoseNullConversion(S, E, T, CC);

  S.DiscardMisalignedMemberAddress(Target, E);

  if (!Source->isIntegerType() || !Target->isIntegerType())
    return;

  // TODO: remove this early return once the false positives for constant->bool
  // in templates, macros, etc, are reduced or removed.
  if (Target->isSpecificBuiltinType(BuiltinType::Bool))
    return;

  IntRange SourceRange = GetExprRange(S.Context, E);
  IntRange TargetRange = IntRange::forTargetOfCanonicalType(S.Context, Target);

  if (SourceRange.Width > TargetRange.Width) {
    // If the source is a constant, use a default-on diagnostic.
    // TODO: this should happen for bitfield stores, too.
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects)) {
      llvm::APSInt Value(32);
      Value = Result.Val.getInt();

      if (S.SourceMgr.isInSystemMacro(CC))
        return;

      std::string PrettySourceValue = Value.toString(10);
      std::string PrettyTargetValue = PrettyPrintInRange(Value, TargetRange);

      S.DiagRuntimeBehavior(E->getExprLoc(), E,
        S.PDiag(diag::warn_impcast_integer_precision_constant)
            << PrettySourceValue << PrettyTargetValue
            << E->getType() << T << E->getSourceRange()
            << clang::SourceRange(CC));
      return;
    }

    // People want to build with -Wshorten-64-to-32 and not -Wconversion.
    if (S.SourceMgr.isInSystemMacro(CC))
      return;

    if (TargetRange.Width == 32 && S.Context.getIntWidth(E->getType()) == 64)
      return DiagnoseImpCast(S, E, T, CC, diag::warn_impcast_integer_64_32,
                             /* pruneControlFlow */ true);
    return DiagnoseImpCast(S, E, T, CC, diag::warn_impcast_integer_precision);
  }

  if (TargetRange.Width > SourceRange.Width) {
    if (auto *UO = dyn_cast<UnaryOperator>(E))
      if (UO->getOpcode() == UO_Minus)
        if (Source->isUnsignedIntegerType()) {
          if (Target->isUnsignedIntegerType())
            return DiagnoseImpCast(S, E, T, CC,
                                   diag::warn_impcast_high_order_zero_bits);
          if (Target->isSignedIntegerType())
            return DiagnoseImpCast(S, E, T, CC,
                                   diag::warn_impcast_nonnegative_result);
        }
  }

  if (TargetRange.Width == SourceRange.Width && !TargetRange.NonNegative &&
      SourceRange.NonNegative && Source->isSignedIntegerType()) {
    // Warn when doing a signed to signed conversion, warn if the positive
    // source value is exactly the width of the target type, which will
    // cause a negative value to be stored.

    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects) &&
        !S.SourceMgr.isInSystemMacro(CC)) {
      llvm::APSInt Value = Result.Val.getInt();
      if (isSameWidthConstantConversion(S, E, T, CC)) {
        std::string PrettySourceValue = Value.toString(10);
        std::string PrettyTargetValue = PrettyPrintInRange(Value, TargetRange);

        S.DiagRuntimeBehavior(
            E->getExprLoc(), E,
            S.PDiag(diag::warn_impcast_integer_precision_constant)
                << PrettySourceValue << PrettyTargetValue << E->getType() << T
                << E->getSourceRange() << clang::SourceRange(CC));
        return;
      }
    }

    // Fall through for non-constants to give a sign conversion warning.
  }

  if ((TargetRange.NonNegative && !SourceRange.NonNegative) ||
      (!TargetRange.NonNegative && SourceRange.NonNegative &&
       SourceRange.Width == TargetRange.Width)) {
    if (S.SourceMgr.isInSystemMacro(CC))
      return;

    unsigned DiagID = diag::warn_impcast_integer_sign;

    // Traditionally, gcc has warned about this under -Wsign-compare.
    // We also want to warn about it in -Wconversion.
    // So if -Wconversion is off, use a completely identical diagnostic
    // in the sign-compare group.
    // The conditional-checking code will
    if (ICContext) {
      DiagID = diag::warn_impcast_integer_sign_conditional;
      *ICContext = true;
    }

    return DiagnoseImpCast(S, E, T, CC, DiagID);
  }

  // Diagnose conversions between different enumeration types.
  // In C, we pretend that the type of an EnumConstantDecl is its enumeration
  // type, to give us better diagnostics.
  QualType SourceType = E->getType();
  if (!S.getLangOpts().CPlusPlus) {
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
      if (EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
        EnumDecl *Enum = cast<EnumDecl>(ECD->getDeclContext());
        SourceType = S.Context.getTypeDeclType(Enum);
        Source = S.Context.getCanonicalType(SourceType).getTypePtr();
      }
  }

  if (const EnumType *SourceEnum = Source->getAs<EnumType>())
    if (const EnumType *TargetEnum = Target->getAs<EnumType>())
      if (SourceEnum->getDecl()->hasNameForLinkage() &&
          TargetEnum->getDecl()->hasNameForLinkage() &&
          SourceEnum != TargetEnum) {
        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        return DiagnoseImpCast(S, E, SourceType, T, CC,
                               diag::warn_impcast_different_enum_types);
      }
}

static void CheckConditionalOperator(Sema &S, ConditionalOperator *E,
                                     SourceLocation CC, QualType T);

static void CheckConditionalOperand(Sema &S, Expr *E, QualType T,
                                    SourceLocation CC, bool &ICContext) {
  E = E->IgnoreParenImpCasts();

  if (isa<ConditionalOperator>(E))
    return CheckConditionalOperator(S, cast<ConditionalOperator>(E), CC, T);

  AnalyzeImplicitConversions(S, E, CC);
  if (E->getType() != T)
    return CheckImplicitConversion(S, E, T, CC, &ICContext);
}

static void CheckConditionalOperator(Sema &S, ConditionalOperator *E,
                                     SourceLocation CC, QualType T) {
  AnalyzeImplicitConversions(S, E->getCond(), E->getQuestionLoc());

  bool Suspicious = false;
  CheckConditionalOperand(S, E->getTrueExpr(), T, CC, Suspicious);
  CheckConditionalOperand(S, E->getFalseExpr(), T, CC, Suspicious);

  // If -Wconversion would have warned about either of the candidates
  // for a signedness conversion to the context type...
  if (!Suspicious) return;

  // ...but it's currently ignored...
  if (!S.Diags.isIgnored(diag::warn_impcast_integer_sign_conditional, CC))
    return;

  // ...then check whether it would have warned about either of the
  // candidates for a signedness conversion to the condition type.
  if (E->getType() == T) return;

  Suspicious = false;
  CheckImplicitConversion(S, E->getTrueExpr()->IgnoreParenImpCasts(),
                          E->getType(), CC, &Suspicious);
  if (!Suspicious)
    CheckImplicitConversion(S, E->getFalseExpr()->IgnoreParenImpCasts(),
                            E->getType(), CC, &Suspicious);
}

/// Check conversion of given expression to boolean.
/// Input argument E is a logical expression.
static void CheckBoolLikeConversion(Sema &S, Expr *E, SourceLocation CC) {
  if (S.getLangOpts().Bool)
    return;
  if (E->IgnoreParenImpCasts()->getType()->isAtomicType())
    return;
  CheckImplicitConversion(S, E->IgnoreParenImpCasts(), S.Context.BoolTy, CC);
}

/// AnalyzeImplicitConversions - Find and report any interesting
/// implicit conversions in the given expression.  There are a couple
/// of competing diagnostics here, -Wconversion and -Wsign-compare.
static void AnalyzeImplicitConversions(Sema &S, Expr *OrigE,
                                       SourceLocation CC) {
  QualType T = OrigE->getType();
  Expr *E = OrigE->IgnoreParenImpCasts();

  if (E->isTypeDependent() || E->isValueDependent())
    return;

  // For conditional operators, we analyze the arguments as if they
  // were being fed directly into the output.
  if (isa<ConditionalOperator>(E)) {
    ConditionalOperator *CO = cast<ConditionalOperator>(E);
    CheckConditionalOperator(S, CO, CC, T);
    return;
  }

  // Check implicit argument conversions for function calls.
  if (CallExpr *Call = dyn_cast<CallExpr>(E))
    CheckImplicitArgumentConversions(S, Call, CC);

  // Go ahead and check any implicit conversions we might have skipped.
  // The non-canonical typecheck is just an optimization;
  // CheckImplicitConversion will filter out dead implicit conversions.
  if (E->getType() != T)
    CheckImplicitConversion(S, E, T, CC);

  // Now continue drilling into this expression.

  if (PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    // The bound subexpressions in a PseudoObjectExpr are not reachable
    // as transitive children.
    // FIXME: Use a more uniform representation for this.
    for (auto *SE : POE->semantics())
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(SE))
        AnalyzeImplicitConversions(S, OVE->getSourceExpr(), CC);
  }

  // Skip past explicit casts.
  if (auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    E = CE->getSubExpr()->IgnoreParenImpCasts();
    if (!CE->getType()->isVoidType() && E->getType()->isAtomicType())
      S.Diag(E->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
    return AnalyzeImplicitConversions(S, E, CC);
  }

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    // Do a somewhat different check with comparison operators.
    if (BO->isComparisonOp())
      return AnalyzeComparison(S, BO);

    // And with simple assignments.
    if (BO->getOpcode() == BO_Assign)
      return AnalyzeAssignment(S, BO);
    // And with compound assignments.
    if (BO->isAssignmentOp())
      return AnalyzeCompoundAssignment(S, BO);
  }

  // These break the otherwise-useful invariant below.  Fortunately,
  // we don't really need to recurse into them, because any internal
  // expressions should have been analyzed already when they were
  // built into statements.
  if (isa<StmtExpr>(E)) return;

  // Don't descend into unevaluated contexts.
  if (isa<UnaryExprOrTypeTraitExpr>(E)) return;

  // Now just recurse over the expression's children.
  CC = E->getExprLoc();
  BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  bool IsLogicalAndOperator = BO && BO->getOpcode() == BO_LAnd;
  for (Stmt *SubStmt : E->children()) {
    Expr *ChildExpr = dyn_cast_or_null<Expr>(SubStmt);
    if (!ChildExpr)
      continue;

    if (IsLogicalAndOperator &&
        isa<StringLiteral>(ChildExpr->IgnoreParenImpCasts()))
      // Ignore checking string literals that are in logical and operators.
      // This is a common pattern for asserts.
      continue;
    AnalyzeImplicitConversions(S, ChildExpr, CC);
  }

  if (BO && BO->isLogicalOp()) {
    Expr *SubExpr = BO->getLHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::CheckBoolLikeConversion(S, SubExpr, BO->getExprLoc());

    SubExpr = BO->getRHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::CheckBoolLikeConversion(S, SubExpr, BO->getExprLoc());
  }

  if (const UnaryOperator *U = dyn_cast<UnaryOperator>(E)) {
    if (U->getOpcode() == UO_LNot) {
      ::CheckBoolLikeConversion(S, U->getSubExpr(), CC);
    } else if (U->getOpcode() != UO_AddrOf) {
      if (U->getSubExpr()->getType()->isAtomicType())
        S.Diag(U->getSubExpr()->getBeginLoc(),
               diag::warn_atomic_implicit_seq_cst);
    }
  }
}

/// Diagnose integer type and any valid implicit conversion to it.
static bool checkOpenCLEnqueueIntType(Sema &S, Expr *E, const QualType &IntT) {
  // Taking into account implicit conversions,
  // allow any integer.
  if (!E->getType()->isIntegerType()) {
    S.Diag(E->getBeginLoc(),
           diag::err_opencl_enqueue_kernel_invalid_local_size_type);
    return true;
  }
  // Potentially emit standard warnings for implicit conversions if enabled
  // using -Wconversion.
  CheckImplicitConversion(S, E, IntT, E->getBeginLoc());
  return false;
}

// Helper function for Sema::DiagnoseAlwaysNonNullPointer.
// Returns true when emitting a warning about taking the address of a reference.
static bool CheckForReference(Sema &SemaRef, const Expr *E,
                              const PartialDiagnostic &PD) {
  E = E->IgnoreParenImpCasts();

  const FunctionDecl *FD = nullptr;

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (!DRE->getDecl()->getType()->isReferenceType())
      return false;
  } else if (const MemberExpr *M = dyn_cast<MemberExpr>(E)) {
    if (!M->getMemberDecl()->getType()->isReferenceType())
      return false;
  } else if (const CallExpr *Call = dyn_cast<CallExpr>(E)) {
    if (!Call->getCallReturnType(SemaRef.Context)->isReferenceType())
      return false;
    FD = Call->getDirectCallee();
  } else {
    return false;
  }

  SemaRef.Diag(E->getExprLoc(), PD);

  // If possible, point to location of function.
  if (FD) {
    SemaRef.Diag(FD->getLocation(), diag::note_reference_is_return_value) << FD;
  }

  return true;
}

// Returns true if the SourceLocation is expanded from any macro body.
// Returns false if the SourceLocation is invalid, is from not in a macro
// expansion, or is from expanded from a top-level macro argument.
static bool IsInAnyMacroBody(const SourceManager &SM, SourceLocation Loc) {
  if (Loc.isInvalid())
    return false;

  while (Loc.isMacroID()) {
    if (SM.isMacroBodyExpansion(Loc))
      return true;
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }

  return false;
}

/// Diagnose pointers that are always non-null.
/// \param E the expression containing the pointer
/// \param NullKind NPCK_NotNull if E is a cast to bool, otherwise, E is
/// compared to a null pointer
/// \param IsEqual True when the comparison is equal to a null pointer
/// \param Range Extra SourceRange to highlight in the diagnostic
void Sema::DiagnoseAlwaysNonNullPointer(Expr *E,
                                        Expr::NullPointerConstantKind NullKind,
                                        bool IsEqual, SourceRange Range) {
  if (!E)
    return;

  // Don't warn inside macros.
  if (E->getExprLoc().isMacroID()) {
    const SourceManager &SM = getSourceManager();
    if (IsInAnyMacroBody(SM, E->getExprLoc()) ||
        IsInAnyMacroBody(SM, Range.getBegin()))
      return;
  }
  E = E->IgnoreImpCasts();

  const bool IsCompare = NullKind != Expr::NPCK_NotNull;

  if (isa<CXXThisExpr>(E)) {
    unsigned DiagID = IsCompare ? diag::warn_this_null_compare
                                : diag::warn_this_bool_conversion;
    Diag(E->getExprLoc(), DiagID) << E->getSourceRange() << Range << IsEqual;
    return;
  }

  bool IsAddressOf = false;

  if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() != UO_AddrOf)
      return;
    IsAddressOf = true;
    E = UO->getSubExpr();
  }

  if (IsAddressOf) {
    unsigned DiagID = IsCompare
                          ? diag::warn_address_of_reference_null_compare
                          : diag::warn_address_of_reference_bool_conversion;
    PartialDiagnostic PD = PDiag(DiagID) << E->getSourceRange() << Range
                                         << IsEqual;
    if (CheckForReference(*this, E, PD)) {
      return;
    }
  }

  auto ComplainAboutNonnullParamOrCall = [&](const Attr *NonnullAttr) {
    bool IsParam = isa<NonNullAttr>(NonnullAttr);
    std::string Str;
    llvm::raw_string_ostream S(Str);
    E->printPretty(S, nullptr, getPrintingPolicy());
    unsigned DiagID = IsCompare ? diag::warn_nonnull_expr_compare
                                : diag::warn_cast_nonnull_to_bool;
    Diag(E->getExprLoc(), DiagID) << IsParam << S.str()
      << E->getSourceRange() << Range << IsEqual;
    Diag(NonnullAttr->getLocation(), diag::note_declared_nonnull) << IsParam;
  };

  // If we have a CallExpr that is tagged with returns_nonnull, we can complain.
  if (auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts())) {
    if (auto *Callee = Call->getDirectCallee()) {
      if (const Attr *A = Callee->getAttr<ReturnsNonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }
    }
  }

  // Expect to find a single Decl.  Skip anything more complicated.
  ValueDecl *D = nullptr;
  if (DeclRefExpr *R = dyn_cast<DeclRefExpr>(E)) {
    D = R->getDecl();
  } else if (MemberExpr *M = dyn_cast<MemberExpr>(E)) {
    D = M->getMemberDecl();
  }

  // Weak Decls can be null.
  if (!D || D->isWeak())
    return;

  // Check for parameter decl with nonnull attribute
  if (const auto* PV = dyn_cast<ParmVarDecl>(D)) {
    if (getCurFunction() &&
        !getCurFunction()->ModifiedNonNullParams.count(PV)) {
      if (const Attr *A = PV->getAttr<NonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }

      if (const auto *FD = dyn_cast<FunctionDecl>(PV->getDeclContext())) {
        auto ParamIter = llvm::find(FD->parameters(), PV);
        assert(ParamIter != FD->param_end());
        unsigned ParamNo = std::distance(FD->param_begin(), ParamIter);

        for (const auto *NonNull : FD->specific_attrs<NonNullAttr>()) {
          if (!NonNull->args_size()) {
              ComplainAboutNonnullParamOrCall(NonNull);
              return;
          }

          for (const ParamIdx &ArgNo : NonNull->args()) {
            if (ArgNo.getASTIndex() == ParamNo) {
              ComplainAboutNonnullParamOrCall(NonNull);
              return;
            }
          }
        }
      }
    }
  }

  QualType T = D->getType();
  const bool IsArray = T->isArrayType();
  const bool IsFunction = T->isFunctionType();

  // Address of function is used to silence the function warning.
  if (IsAddressOf && IsFunction) {
    return;
  }

  // Found nothing.
  if (!IsAddressOf && !IsFunction && !IsArray)
    return;

  // Pretty print the expression for the diagnostic.
  std::string Str;
  llvm::raw_string_ostream S(Str);
  E->printPretty(S, nullptr, getPrintingPolicy());

  unsigned DiagID = IsCompare ? diag::warn_null_pointer_compare
                              : diag::warn_impcast_pointer_to_bool;
  enum {
    AddressOf,
    FunctionPointer,
    ArrayPointer
  } DiagType;
  if (IsAddressOf)
    DiagType = AddressOf;
  else if (IsFunction)
    DiagType = FunctionPointer;
  else if (IsArray)
    DiagType = ArrayPointer;
  else
    llvm_unreachable("Could not determine diagnostic.");
  Diag(E->getExprLoc(), DiagID) << DiagType << S.str() << E->getSourceRange()
                                << Range << IsEqual;

  if (!IsFunction)
    return;

  // Suggest '&' to silence the function warning.
  Diag(E->getExprLoc(), diag::note_function_warning_silence)
      << FixItHint::CreateInsertion(E->getBeginLoc(), "&");

  // Check to see if '()' fixit should be emitted.
  QualType ReturnType;
  UnresolvedSet<4> NonTemplateOverloads;
  tryExprAsCall(*E, ReturnType, NonTemplateOverloads);
  if (ReturnType.isNull())
    return;

  if (IsCompare) {
    // There are two cases here.  If there is null constant, the only suggest
    // for a pointer return type.  If the null is 0, then suggest if the return
    // type is a pointer or an integer type.
    if (!ReturnType->isPointerType()) {
      if (NullKind == Expr::NPCK_ZeroExpression ||
          NullKind == Expr::NPCK_ZeroLiteral) {
        if (!ReturnType->isIntegerType())
          return;
      } else {
        return;
      }
    }
  } else { // !IsCompare
    // For function to bool, only suggest if the function pointer has bool
    // return type.
    if (!ReturnType->isSpecificBuiltinType(BuiltinType::Bool))
      return;
  }
  Diag(E->getExprLoc(), diag::note_function_to_function_call)
      << FixItHint::CreateInsertion(getLocForEndOfToken(E->getEndLoc()), "()");
}

/// Diagnoses "dangerous" implicit conversions within the given
/// expression (which is a full expression).  Implements -Wconversion
/// and -Wsign-compare.
///
/// \param CC the "context" location of the implicit conversion, i.e.
///   the most location of the syntactic entity requiring the implicit
///   conversion
void Sema::CheckImplicitConversions(Expr *E, SourceLocation CC) {
  // Don't diagnose in unevaluated contexts.
  if (isUnevaluatedContext())
    return;

  // Don't diagnose for value- or type-dependent expressions.
  if (E->isTypeDependent() || E->isValueDependent())
    return;

  // Check for array bounds violations in cases where the check isn't triggered
  // elsewhere for other Expr types (like BinaryOperators), e.g. when an
  // ArraySubscriptExpr is on the RHS of a variable initialization.
  CheckArrayAccess(E);

  // This is not the right CC for (e.g.) a variable initialization.
  AnalyzeImplicitConversions(*this, E, CC);
}

/// CheckBoolLikeConversion - Check conversion of given expression to boolean.
/// Input argument E is a logical expression.
void Sema::CheckBoolLikeConversion(Expr *E, SourceLocation CC) {
  ::CheckBoolLikeConversion(*this, E, CC);
}

/// Diagnose when expression is an integer constant expression and its evaluation
/// results in integer overflow
void Sema::CheckForIntOverflow (Expr *E) {
  // Use a work list to deal with nested struct initializers.
  SmallVector<Expr *, 2> Exprs(1, E);

  do {
    Expr *OriginalE = Exprs.pop_back_val();
    Expr *E = OriginalE->IgnoreParenCasts();

    if (isa<BinaryOperator>(E)) {
      E->EvaluateForOverflow(Context);
      continue;
    }

    if (auto InitList = dyn_cast<InitListExpr>(OriginalE))
      Exprs.append(InitList->inits().begin(), InitList->inits().end());
    else if (isa<ObjCBoxedExpr>(OriginalE))
      E->EvaluateForOverflow(Context);
    else if (auto Call = dyn_cast<CallExpr>(E))
      Exprs.append(Call->arg_begin(), Call->arg_end());
    else if (auto Message = dyn_cast<ObjCMessageExpr>(E))
      Exprs.append(Message->arg_begin(), Message->arg_end());
  } while (!Exprs.empty());
}

namespace {

/// Visitor for expressions which looks for unsequenced operations on the
/// same object.
class SequenceChecker : public EvaluatedExprVisitor<SequenceChecker> {
  using Base = EvaluatedExprVisitor<SequenceChecker>;

  /// A tree of sequenced regions within an expression. Two regions are
  /// unsequenced if one is an ancestor or a descendent of the other. When we
  /// finish processing an expression with sequencing, such as a comma
  /// expression, we fold its tree nodes into its parent, since they are
  /// unsequenced with respect to nodes we will visit later.
  class SequenceTree {
    struct Value {
      explicit Value(unsigned Parent) : Parent(Parent), Merged(false) {}
      unsigned Parent : 31;
      unsigned Merged : 1;
    };
    SmallVector<Value, 8> Values;

  public:
    /// A region within an expression which may be sequenced with respect
    /// to some other region.
    class Seq {
      friend class SequenceTree;

      unsigned Index = 0;

      explicit Seq(unsigned N) : Index(N) {}

    public:
      Seq() = default;
    };

    SequenceTree() { Values.push_back(Value(0)); }
    Seq root() const { return Seq(0); }

    /// Create a new sequence of operations, which is an unsequenced
    /// subset of \p Parent. This sequence of operations is sequenced with
    /// respect to other children of \p Parent.
    Seq allocate(Seq Parent) {
      Values.push_back(Value(Parent.Index));
      return Seq(Values.size() - 1);
    }

    /// Merge a sequence of operations into its parent.
    void merge(Seq S) {
      Values[S.Index].Merged = true;
    }

    /// Determine whether two operations are unsequenced. This operation
    /// is asymmetric: \p Cur should be the more recent sequence, and \p Old
    /// should have been merged into its parent as appropriate.
    bool isUnsequenced(Seq Cur, Seq Old) {
      unsigned C = representative(Cur.Index);
      unsigned Target = representative(Old.Index);
      while (C >= Target) {
        if (C == Target)
          return true;
        C = Values[C].Parent;
      }
      return false;
    }

  private:
    /// Pick a representative for a sequence.
    unsigned representative(unsigned K) {
      if (Values[K].Merged)
        // Perform path compression as we go.
        return Values[K].Parent = representative(Values[K].Parent);
      return K;
    }
  };

  /// An object for which we can track unsequenced uses.
  using Object = NamedDecl *;

  /// Different flavors of object usage which we track. We only track the
  /// least-sequenced usage of each kind.
  enum UsageKind {
    /// A read of an object. Multiple unsequenced reads are OK.
    UK_Use,

    /// A modification of an object which is sequenced before the value
    /// computation of the expression, such as ++n in C++.
    UK_ModAsValue,

    /// A modification of an object which is not sequenced before the value
    /// computation of the expression, such as n++.
    UK_ModAsSideEffect,

    UK_Count = UK_ModAsSideEffect + 1
  };

  struct Usage {
    Expr *Use = nullptr;
    SequenceTree::Seq Seq;

    Usage() = default;
  };

  struct UsageInfo {
    Usage Uses[UK_Count];

    /// Have we issued a diagnostic for this variable already?
    bool Diagnosed = false;

    UsageInfo() = default;
  };
  using UsageInfoMap = llvm::SmallDenseMap<Object, UsageInfo, 16>;

  Sema &SemaRef;

  /// Sequenced regions within the expression.
  SequenceTree Tree;

  /// Declaration modifications and references which we have seen.
  UsageInfoMap UsageMap;

  /// The region we are currently within.
  SequenceTree::Seq Region;

  /// Filled in with declarations which were modified as a side-effect
  /// (that is, post-increment operations).
  SmallVectorImpl<std::pair<Object, Usage>> *ModAsSideEffect = nullptr;

  /// Expressions to check later. We defer checking these to reduce
  /// stack usage.
  SmallVectorImpl<Expr *> &WorkList;

  /// RAII object wrapping the visitation of a sequenced subexpression of an
  /// expression. At the end of this process, the side-effects of the evaluation
  /// become sequenced with respect to the value computation of the result, so
  /// we downgrade any UK_ModAsSideEffect within the evaluation to
  /// UK_ModAsValue.
  struct SequencedSubexpression {
    SequencedSubexpression(SequenceChecker &Self)
      : Self(Self), OldModAsSideEffect(Self.ModAsSideEffect) {
      Self.ModAsSideEffect = &ModAsSideEffect;
    }

    ~SequencedSubexpression() {
      for (auto &M : llvm::reverse(ModAsSideEffect)) {
        UsageInfo &U = Self.UsageMap[M.first];
        auto &SideEffectUsage = U.Uses[UK_ModAsSideEffect];
        Self.addUsage(U, M.first, SideEffectUsage.Use, UK_ModAsValue);
        SideEffectUsage = M.second;
      }
      Self.ModAsSideEffect = OldModAsSideEffect;
    }

    SequenceChecker &Self;
    SmallVector<std::pair<Object, Usage>, 4> ModAsSideEffect;
    SmallVectorImpl<std::pair<Object, Usage>> *OldModAsSideEffect;
  };

  /// RAII object wrapping the visitation of a subexpression which we might
  /// choose to evaluate as a constant. If any subexpression is evaluated and
  /// found to be non-constant, this allows us to suppress the evaluation of
  /// the outer expression.
  class EvaluationTracker {
  public:
    EvaluationTracker(SequenceChecker &Self)
        : Self(Self), Prev(Self.EvalTracker) {
      Self.EvalTracker = this;
    }

    ~EvaluationTracker() {
      Self.EvalTracker = Prev;
      if (Prev)
        Prev->EvalOK &= EvalOK;
    }

    bool evaluate(const Expr *E, bool &Result) {
      if (!EvalOK || E->isValueDependent())
        return false;
      EvalOK = E->EvaluateAsBooleanCondition(Result, Self.SemaRef.Context);
      return EvalOK;
    }

  private:
    SequenceChecker &Self;
    EvaluationTracker *Prev;
    bool EvalOK = true;
  } *EvalTracker = nullptr;

  /// Find the object which is produced by the specified expression,
  /// if any.
  Object getObject(Expr *E, bool Mod) const {
    E = E->IgnoreParenCasts();
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
      if (Mod && (UO->getOpcode() == UO_PreInc || UO->getOpcode() == UO_PreDec))
        return getObject(UO->getSubExpr(), Mod);
    } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma)
        return getObject(BO->getRHS(), Mod);
      if (Mod && BO->isAssignmentOp())
        return getObject(BO->getLHS(), Mod);
    } else if (MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      // FIXME: Check for more interesting cases, like "x.n = ++x.n".
      if (isa<CXXThisExpr>(ME->getBase()->IgnoreParenCasts()))
        return ME->getMemberDecl();
    } else if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
      // FIXME: If this is a reference, map through to its value.
      return DRE->getDecl();
    return nullptr;
  }

  /// Note that an object was modified or used by an expression.
  void addUsage(UsageInfo &UI, Object O, Expr *Ref, UsageKind UK) {
    Usage &U = UI.Uses[UK];
    if (!U.Use || !Tree.isUnsequenced(Region, U.Seq)) {
      if (UK == UK_ModAsSideEffect && ModAsSideEffect)
        ModAsSideEffect->push_back(std::make_pair(O, U));
      U.Use = Ref;
      U.Seq = Region;
    }
  }

  /// Check whether a modification or use conflicts with a prior usage.
  void checkUsage(Object O, UsageInfo &UI, Expr *Ref, UsageKind OtherKind,
                  bool IsModMod) {
    if (UI.Diagnosed)
      return;

    const Usage &U = UI.Uses[OtherKind];
    if (!U.Use || !Tree.isUnsequenced(Region, U.Seq))
      return;

    Expr *Mod = U.Use;
    Expr *ModOrUse = Ref;
    if (OtherKind == UK_Use)
      std::swap(Mod, ModOrUse);

    SemaRef.Diag(Mod->getExprLoc(),
                 IsModMod ? diag::warn_unsequenced_mod_mod
                          : diag::warn_unsequenced_mod_use)
      << O << SourceRange(ModOrUse->getExprLoc());
    UI.Diagnosed = true;
  }

  void notePreUse(Object O, Expr *Use) {
    UsageInfo &U = UsageMap[O];
    // Uses conflict with other modifications.
    checkUsage(O, U, Use, UK_ModAsValue, false);
  }

  void notePostUse(Object O, Expr *Use) {
    UsageInfo &U = UsageMap[O];
    checkUsage(O, U, Use, UK_ModAsSideEffect, false);
    addUsage(U, O, Use, UK_Use);
  }

  void notePreMod(Object O, Expr *Mod) {
    UsageInfo &U = UsageMap[O];
    // Modifications conflict with other modifications and with uses.
    checkUsage(O, U, Mod, UK_ModAsValue, true);
    checkUsage(O, U, Mod, UK_Use, false);
  }

  void notePostMod(Object O, Expr *Use, UsageKind UK) {
    UsageInfo &U = UsageMap[O];
    checkUsage(O, U, Use, UK_ModAsSideEffect, true);
    addUsage(U, O, Use, UK);
  }

public:
  SequenceChecker(Sema &S, Expr *E, SmallVectorImpl<Expr *> &WorkList)
      : Base(S.Context), SemaRef(S), Region(Tree.root()), WorkList(WorkList) {
    Visit(E);
  }

  void VisitStmt(Stmt *S) {
    // Skip all statements which aren't expressions for now.
  }

  void VisitExpr(Expr *E) {
    // By default, just recurse to evaluated subexpressions.
    Base::VisitStmt(E);
  }

  void VisitCastExpr(CastExpr *E) {
    Object O = Object();
    if (E->getCastKind() == CK_LValueToRValue)
      O = getObject(E->getSubExpr(), false);

    if (O)
      notePreUse(O, E);
    VisitExpr(E);
    if (O)
      notePostUse(O, E);
  }

  void VisitSequencedExpressions(Expr *SequencedBefore, Expr *SequencedAfter) {
    SequenceTree::Seq BeforeRegion = Tree.allocate(Region);
    SequenceTree::Seq AfterRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    {
      SequencedSubexpression SeqBefore(*this);
      Region = BeforeRegion;
      Visit(SequencedBefore);
    }

    Region = AfterRegion;
    Visit(SequencedAfter);

    Region = OldRegion;

    Tree.merge(BeforeRegion);
    Tree.merge(AfterRegion);
  }

  void VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
    // C++17 [expr.sub]p1:
    //   The expression E1[E2] is identical (by definition) to *((E1)+(E2)). The
    //   expression E1 is sequenced before the expression E2.
    if (SemaRef.getLangOpts().CPlusPlus17)
      VisitSequencedExpressions(ASE->getLHS(), ASE->getRHS());
    else
      Base::VisitStmt(ASE);
  }

  void VisitBinComma(BinaryOperator *BO) {
    // C++11 [expr.comma]p1:
    //   Every value computation and side effect associated with the left
    //   expression is sequenced before every value computation and side
    //   effect associated with the right expression.
    VisitSequencedExpressions(BO->getLHS(), BO->getRHS());
  }

  void VisitBinAssign(BinaryOperator *BO) {
    // The modification is sequenced after the value computation of the LHS
    // and RHS, so check it before inspecting the operands and update the
    // map afterwards.
    Object O = getObject(BO->getLHS(), true);
    if (!O)
      return VisitExpr(BO);

    notePreMod(O, BO);

    // C++11 [expr.ass]p7:
    //   E1 op= E2 is equivalent to E1 = E1 op E2, except that E1 is evaluated
    //   only once.
    //
    // Therefore, for a compound assignment operator, O is considered used
    // everywhere except within the evaluation of E1 itself.
    if (isa<CompoundAssignOperator>(BO))
      notePreUse(O, BO);

    Visit(BO->getLHS());

    if (isa<CompoundAssignOperator>(BO))
      notePostUse(O, BO);

    Visit(BO->getRHS());

    // C++11 [expr.ass]p1:
    //   the assignment is sequenced [...] before the value computation of the
    //   assignment expression.
    // C11 6.5.16/3 has no such rule.
    notePostMod(O, BO, SemaRef.getLangOpts().CPlusPlus ? UK_ModAsValue
                                                       : UK_ModAsSideEffect);
  }

  void VisitCompoundAssignOperator(CompoundAssignOperator *CAO) {
    VisitBinAssign(CAO);
  }

  void VisitUnaryPreInc(UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreDec(UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreIncDec(UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    // C++11 [expr.pre.incr]p1:
    //   the expression ++x is equivalent to x+=1
    notePostMod(O, UO, SemaRef.getLangOpts().CPlusPlus ? UK_ModAsValue
                                                       : UK_ModAsSideEffect);
  }

  void VisitUnaryPostInc(UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostDec(UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostIncDec(UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    notePostMod(O, UO, UK_ModAsSideEffect);
  }

  /// Don't visit the RHS of '&&' or '||' if it might not be evaluated.
  void VisitBinLOr(BinaryOperator *BO) {
    // The side-effects of the LHS of an '&&' are sequenced before the
    // value computation of the RHS, and hence before the value computation
    // of the '&&' itself, unless the LHS evaluates to zero. We treat them
    // as if they were unconditionally sequenced.
    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Visit(BO->getLHS());
    }

    bool Result;
    if (Eval.evaluate(BO->getLHS(), Result)) {
      if (!Result)
        Visit(BO->getRHS());
    } else {
      // Check for unsequenced operations in the RHS, treating it as an
      // entirely separate evaluation.
      //
      // FIXME: If there are operations in the RHS which are unsequenced
      // with respect to operations outside the RHS, and those operations
      // are unconditionally evaluated, diagnose them.
      WorkList.push_back(BO->getRHS());
    }
  }
  void VisitBinLAnd(BinaryOperator *BO) {
    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Visit(BO->getLHS());
    }

    bool Result;
    if (Eval.evaluate(BO->getLHS(), Result)) {
      if (Result)
        Visit(BO->getRHS());
    } else {
      WorkList.push_back(BO->getRHS());
    }
  }

  // Only visit the condition, unless we can be sure which subexpression will
  // be chosen.
  void VisitAbstractConditionalOperator(AbstractConditionalOperator *CO) {
    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Visit(CO->getCond());
    }

    bool Result;
    if (Eval.evaluate(CO->getCond(), Result))
      Visit(Result ? CO->getTrueExpr() : CO->getFalseExpr());
    else {
      WorkList.push_back(CO->getTrueExpr());
      WorkList.push_back(CO->getFalseExpr());
    }
  }

  void VisitCallExpr(CallExpr *CE) {
    // C++11 [intro.execution]p15:
    //   When calling a function [...], every value computation and side effect
    //   associated with any argument expression, or with the postfix expression
    //   designating the called function, is sequenced before execution of every
    //   expression or statement in the body of the function [and thus before
    //   the value computation of its result].
    SequencedSubexpression Sequenced(*this);
    Base::VisitCallExpr(CE);

    // FIXME: CXXNewExpr and CXXDeleteExpr implicitly call functions.
  }

  void VisitCXXConstructExpr(CXXConstructExpr *CCE) {
    // This is a call, so all subexpressions are sequenced before the result.
    SequencedSubexpression Sequenced(*this);

    if (!CCE->isListInitialization())
      return VisitExpr(CCE);

    // In C++11, list initializations are sequenced.
    SmallVector<SequenceTree::Seq, 32> Elts;
    SequenceTree::Seq Parent = Region;
    for (CXXConstructExpr::arg_iterator I = CCE->arg_begin(),
                                        E = CCE->arg_end();
         I != E; ++I) {
      Region = Tree.allocate(Parent);
      Elts.push_back(Region);
      Visit(*I);
    }

    // Forget that the initializers are sequenced.
    Region = Parent;
    for (unsigned I = 0; I < Elts.size(); ++I)
      Tree.merge(Elts[I]);
  }

  void VisitInitListExpr(InitListExpr *ILE) {
    if (!SemaRef.getLangOpts().CPlusPlus11)
      return VisitExpr(ILE);

    // In C++11, list initializations are sequenced.
    SmallVector<SequenceTree::Seq, 32> Elts;
    SequenceTree::Seq Parent = Region;
    for (unsigned I = 0; I < ILE->getNumInits(); ++I) {
      Expr *E = ILE->getInit(I);
      if (!E) continue;
      Region = Tree.allocate(Parent);
      Elts.push_back(Region);
      Visit(E);
    }

    // Forget that the initializers are sequenced.
    Region = Parent;
    for (unsigned I = 0; I < Elts.size(); ++I)
      Tree.merge(Elts[I]);
  }
};

} // namespace

void Sema::CheckUnsequencedOperations(Expr *E) {
  SmallVector<Expr *, 8> WorkList;
  WorkList.push_back(E);
  while (!WorkList.empty()) {
    Expr *Item = WorkList.pop_back_val();
    SequenceChecker(*this, Item, WorkList);
  }
}

void Sema::CheckCompletedExpr(Expr *E, SourceLocation CheckLoc,
                              bool IsConstexpr) {
  CheckImplicitConversions(E, CheckLoc);
  if (!E->isInstantiationDependent())
    CheckUnsequencedOperations(E);
  if (!IsConstexpr && !E->isValueDependent())
    CheckForIntOverflow(E);
  DiagnoseMisalignedMembers();
}

void Sema::CheckBitFieldInitialization(SourceLocation InitLoc,
                                       FieldDecl *BitField,
                                       Expr *Init) {
  (void) AnalyzeBitFieldAssignment(*this, BitField, Init, InitLoc);
}

static void diagnoseArrayStarInParamType(Sema &S, QualType PType,
                                         SourceLocation Loc) {
  if (!PType->isVariablyModifiedType())
    return;
  if (const auto *PointerTy = dyn_cast<PointerType>(PType)) {
    diagnoseArrayStarInParamType(S, PointerTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ReferenceTy = dyn_cast<ReferenceType>(PType)) {
    diagnoseArrayStarInParamType(S, ReferenceTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ParenTy = dyn_cast<ParenType>(PType)) {
    diagnoseArrayStarInParamType(S, ParenTy->getInnerType(), Loc);
    return;
  }

  const ArrayType *AT = S.Context.getAsArrayType(PType);
  if (!AT)
    return;

  if (AT->getSizeModifier() != ArrayType::Star) {
    diagnoseArrayStarInParamType(S, AT->getElementType(), Loc);
    return;
  }

  S.Diag(Loc, diag::err_array_star_in_function_definition);
}

/// CheckParmsForFunctionDef - Check that the parameters of the given
/// function are appropriate for the definition of a function. This
/// takes care of any checks that cannot be performed on the
/// declaration itself, e.g., that the types of each of the function
/// parameters are complete.
bool Sema::CheckParmsForFunctionDef(ArrayRef<ParmVarDecl *> Parameters,
                                    bool CheckParameterNames) {
  bool HasInvalidParm = false;
  for (ParmVarDecl *Param : Parameters) {
    // C99 6.7.5.3p4: the parameters in a parameter type list in a
    // function declarator that is part of a function definition of
    // that function shall not have incomplete type.
    //
    // This is also C++ [dcl.fct]p6.
    if (!Param->isInvalidDecl() &&
        RequireCompleteType(Param->getLocation(), Param->getType(),
                            diag::err_typecheck_decl_incomplete_type)) {
      Param->setInvalidDecl();
      HasInvalidParm = true;
    }

    // C99 6.9.1p5: If the declarator includes a parameter type list, the
    // declaration of each parameter shall include an identifier.
    if (CheckParameterNames &&
        Param->getIdentifier() == nullptr &&
        !Param->isImplicit() &&
        !getLangOpts().CPlusPlus)
      Diag(Param->getLocation(), diag::err_parameter_name_omitted);

    // C99 6.7.5.3p12:
    //   If the function declarator is not part of a definition of that
    //   function, parameters may have incomplete type and may use the [*]
    //   notation in their sequences of declarator specifiers to specify
    //   variable length array types.
    QualType PType = Param->getOriginalType();
    // FIXME: This diagnostic should point the '[*]' if source-location
    // information is added for it.
    diagnoseArrayStarInParamType(*this, PType, Param->getLocation());

    // If the parameter is a c++ class type and it has to be destructed in the
    // callee function, declare the destructor so that it can be called by the
    // callee function. Do not perform any direct access check on the dtor here.
    if (!Param->isInvalidDecl()) {
      if (CXXRecordDecl *ClassDecl = Param->getType()->getAsCXXRecordDecl()) {
        if (!ClassDecl->isInvalidDecl() &&
            !ClassDecl->hasIrrelevantDestructor() &&
            !ClassDecl->isDependentContext() &&
            ClassDecl->isParamDestroyedInCallee()) {
          CXXDestructorDecl *Destructor = LookupDestructor(ClassDecl);
          MarkFunctionReferenced(Param->getLocation(), Destructor);
          DiagnoseUseOfDecl(Destructor, Param->getLocation());
        }
      }
    }

    // Parameters with the pass_object_size attribute only need to be marked
    // constant at function definitions. Because we lack information about
    // whether we're on a declaration or definition when we're instantiating the
    // attribute, we need to check for constness here.
    if (const auto *Attr = Param->getAttr<PassObjectSizeAttr>())
      if (!Param->getType().isConstQualified())
        Diag(Param->getLocation(), diag::err_attribute_pointers_only)
            << Attr->getSpelling() << 1;

    // Check for parameter names shadowing fields from the class.
    if (LangOpts.CPlusPlus && !Param->isInvalidDecl()) {
      // The owning context for the parameter should be the function, but we
      // want to see if this function's declaration context is a record.
      DeclContext *DC = Param->getDeclContext();
      if (DC && DC->isFunctionOrMethod()) {
        if (auto *RD = dyn_cast<CXXRecordDecl>(DC->getParent()))
          CheckShadowInheritedFields(Param->getLocation(), Param->getDeclName(),
                                     RD, /*DeclIsField*/ false);
      }
    }
  }

  return HasInvalidParm;
}

/// A helper function to get the alignment of a Decl referred to by DeclRefExpr
/// or MemberExpr.
static CharUnits getDeclAlign(Expr *E, CharUnits TypeAlign,
                              ASTContext &Context) {
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return Context.getDeclAlign(DRE->getDecl());

  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return Context.getDeclAlign(ME->getMemberDecl());

  return TypeAlign;
}

/// CheckCastAlign - Implements -Wcast-align, which warns when a
/// pointer cast increases the alignment requirements.
void Sema::CheckCastAlign(Expr *Op, QualType T, SourceRange TRange) {
  // This is actually a lot of work to potentially be doing on every
  // cast; don't do it if we're ignoring -Wcast_align (as is the default).
  if (getDiagnostics().isIgnored(diag::warn_cast_align, TRange.getBegin()))
    return;

  // Ignore dependent types.
  if (T->isDependentType() || Op->getType()->isDependentType())
    return;

  // Require that the destination be a pointer type.
  const PointerType *DestPtr = T->getAs<PointerType>();
  if (!DestPtr) return;

  // If the destination has alignment 1, we're done.
  QualType DestPointee = DestPtr->getPointeeType();
  if (DestPointee->isIncompleteType()) return;
  CharUnits DestAlign = Context.getTypeAlignInChars(DestPointee);
  if (DestAlign.isOne()) return;

  // Require that the source be a pointer type.
  const PointerType *SrcPtr = Op->getType()->getAs<PointerType>();
  if (!SrcPtr) return;
  QualType SrcPointee = SrcPtr->getPointeeType();

  // Whitelist casts from cv void*.  We already implicitly
  // whitelisted casts to cv void*, since they have alignment 1.
  // Also whitelist casts involving incomplete types, which implicitly
  // includes 'void'.
  if (SrcPointee->isIncompleteType()) return;

  CharUnits SrcAlign = Context.getTypeAlignInChars(SrcPointee);

  if (auto *CE = dyn_cast<CastExpr>(Op)) {
    if (CE->getCastKind() == CK_ArrayToPointerDecay)
      SrcAlign = getDeclAlign(CE->getSubExpr(), SrcAlign, Context);
  } else if (auto *UO = dyn_cast<UnaryOperator>(Op)) {
    if (UO->getOpcode() == UO_AddrOf)
      SrcAlign = getDeclAlign(UO->getSubExpr(), SrcAlign, Context);
  }

  if (SrcAlign >= DestAlign) return;

  Diag(TRange.getBegin(), diag::warn_cast_align)
    << Op->getType() << T
    << static_cast<unsigned>(SrcAlign.getQuantity())
    << static_cast<unsigned>(DestAlign.getQuantity())
    << TRange << Op->getSourceRange();
}

/// Check whether this array fits the idiom of a size-one tail padded
/// array member of a struct.
///
/// We avoid emitting out-of-bounds access warnings for such arrays as they are
/// commonly used to emulate flexible arrays in C89 code.
static bool IsTailPaddedMemberArray(Sema &S, const llvm::APInt &Size,
                                    const NamedDecl *ND) {
  if (Size != 1 || !ND) return false;

  const FieldDecl *FD = dyn_cast<FieldDecl>(ND);
  if (!FD) return false;

  // Don't consider sizes resulting from macro expansions or template argument
  // substitution to form C89 tail-padded arrays.

  TypeSourceInfo *TInfo = FD->getTypeSourceInfo();
  while (TInfo) {
    TypeLoc TL = TInfo->getTypeLoc();
    // Look through typedefs.
    if (TypedefTypeLoc TTL = TL.getAs<TypedefTypeLoc>()) {
      const TypedefNameDecl *TDL = TTL.getTypedefNameDecl();
      TInfo = TDL->getTypeSourceInfo();
      continue;
    }
    if (ConstantArrayTypeLoc CTL = TL.getAs<ConstantArrayTypeLoc>()) {
      const Expr *SizeExpr = dyn_cast<IntegerLiteral>(CTL.getSizeExpr());
      if (!SizeExpr || SizeExpr->getExprLoc().isMacroID())
        return false;
    }
    break;
  }

  const RecordDecl *RD = dyn_cast<RecordDecl>(FD->getDeclContext());
  if (!RD) return false;
  if (RD->isUnion()) return false;
  if (const CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD)) {
    if (!CRD->isStandardLayout()) return false;
  }

  // See if this is the last field decl in the record.
  const Decl *D = FD;
  while ((D = D->getNextDeclInContext()))
    if (isa<FieldDecl>(D))
      return false;
  return true;
}

void Sema::CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                            const ArraySubscriptExpr *ASE,
                            bool AllowOnePastEnd, bool IndexNegated) {
  IndexExpr = IndexExpr->IgnoreParenImpCasts();
  if (IndexExpr->isValueDependent())
    return;

  const Type *EffectiveType =
      BaseExpr->getType()->getPointeeOrArrayElementType();
  BaseExpr = BaseExpr->IgnoreParenCasts();
  const ConstantArrayType *ArrayTy =
      Context.getAsConstantArrayType(BaseExpr->getType());

  if (!ArrayTy)
    return;

  const Type *BaseType = ArrayTy->getElementType().getTypePtr();

  Expr::EvalResult Result;
  if (!IndexExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects))
    return;

  llvm::APSInt index = Result.Val.getInt();
  if (IndexNegated)
    index = -index;

  const NamedDecl *ND = nullptr;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
    ND = DRE->getDecl();
  if (const MemberExpr *ME = dyn_cast<MemberExpr>(BaseExpr))
    ND = ME->getMemberDecl();

  if (index.isUnsigned() || !index.isNegative()) {
    // It is possible that the type of the base expression after
    // IgnoreParenCasts is incomplete, even though the type of the base
    // expression before IgnoreParenCasts is complete (see PR39746 for an
    // example). In this case we have no information about whether the array
    // access exceeds the array bounds. However we can still diagnose an array
    // access which precedes the array bounds.
    if (BaseType->isIncompleteType())
      return;

    llvm::APInt size = ArrayTy->getSize();
    if (!size.isStrictlyPositive())
      return;

    if (BaseType != EffectiveType) {
      // Make sure we're comparing apples to apples when comparing index to size
      uint64_t ptrarith_typesize = Context.getTypeSize(EffectiveType);
      uint64_t array_typesize = Context.getTypeSize(BaseType);
      // Handle ptrarith_typesize being zero, such as when casting to void*
      if (!ptrarith_typesize) ptrarith_typesize = 1;
      if (ptrarith_typesize != array_typesize) {
        // There's a cast to a different size type involved
        uint64_t ratio = array_typesize / ptrarith_typesize;
        // TODO: Be smarter about handling cases where array_typesize is not a
        // multiple of ptrarith_typesize
        if (ptrarith_typesize * ratio == array_typesize)
          size *= llvm::APInt(size.getBitWidth(), ratio);
      }
    }

    if (size.getBitWidth() > index.getBitWidth())
      index = index.zext(size.getBitWidth());
    else if (size.getBitWidth() < index.getBitWidth())
      size = size.zext(index.getBitWidth());

    // For array subscripting the index must be less than size, but for pointer
    // arithmetic also allow the index (offset) to be equal to size since
    // computing the next address after the end of the array is legal and
    // commonly done e.g. in C++ iterators and range-based for loops.
    if (AllowOnePastEnd ? index.ule(size) : index.ult(size))
      return;

    // Also don't warn for arrays of size 1 which are members of some
    // structure. These are often used to approximate flexible arrays in C89
    // code.
    if (IsTailPaddedMemberArray(*this, size, ND))
      return;

    // Suppress the warning if the subscript expression (as identified by the
    // ']' location) and the index expression are both from macro expansions
    // within a system header.
    if (ASE) {
      SourceLocation RBracketLoc = SourceMgr.getSpellingLoc(
          ASE->getRBracketLoc());
      if (SourceMgr.isInSystemHeader(RBracketLoc)) {
        SourceLocation IndexLoc =
            SourceMgr.getSpellingLoc(IndexExpr->getBeginLoc());
        if (SourceMgr.isWrittenInSameFile(RBracketLoc, IndexLoc))
          return;
      }
    }

    unsigned DiagID = diag::warn_ptr_arith_exceeds_bounds;
    if (ASE)
      DiagID = diag::warn_array_index_exceeds_bounds;

    DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                        PDiag(DiagID) << index.toString(10, true)
                                      << size.toString(10, true)
                                      << (unsigned)size.getLimitedValue(~0U)
                                      << IndexExpr->getSourceRange());
  } else {
    unsigned DiagID = diag::warn_array_index_precedes_bounds;
    if (!ASE) {
      DiagID = diag::warn_ptr_arith_precedes_bounds;
      if (index.isNegative()) index = -index;
    }

    DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                        PDiag(DiagID) << index.toString(10, true)
                                      << IndexExpr->getSourceRange());
  }

  if (!ND) {
    // Try harder to find a NamedDecl to point at in the note.
    while (const ArraySubscriptExpr *ASE =
           dyn_cast<ArraySubscriptExpr>(BaseExpr))
      BaseExpr = ASE->getBase()->IgnoreParenCasts();
    if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
      ND = DRE->getDecl();
    if (const MemberExpr *ME = dyn_cast<MemberExpr>(BaseExpr))
      ND = ME->getMemberDecl();
  }

  if (ND)
    DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                        PDiag(diag::note_array_index_out_of_bounds)
                            << ND->getDeclName());
}

void Sema::CheckArrayAccess(const Expr *expr) {
  int AllowOnePastEnd = 0;
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    switch (expr->getStmtClass()) {
      case Stmt::ArraySubscriptExprClass: {
        const ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(expr);
        CheckArrayAccess(ASE->getBase(), ASE->getIdx(), ASE,
                         AllowOnePastEnd > 0);
        expr = ASE->getBase();
        break;
      }
      case Stmt::MemberExprClass: {
        expr = cast<MemberExpr>(expr)->getBase();
        break;
      }
      case Stmt::OMPArraySectionExprClass: {
        const OMPArraySectionExpr *ASE = cast<OMPArraySectionExpr>(expr);
        if (ASE->getLowerBound())
          CheckArrayAccess(ASE->getBase(), ASE->getLowerBound(),
                           /*ASE=*/nullptr, AllowOnePastEnd > 0);
        return;
      }
      case Stmt::UnaryOperatorClass: {
        // Only unwrap the * and & unary operators
        const UnaryOperator *UO = cast<UnaryOperator>(expr);
        expr = UO->getSubExpr();
        switch (UO->getOpcode()) {
          case UO_AddrOf:
            AllowOnePastEnd++;
            break;
          case UO_Deref:
            AllowOnePastEnd--;
            break;
          default:
            return;
        }
        break;
      }
      case Stmt::ConditionalOperatorClass: {
        const ConditionalOperator *cond = cast<ConditionalOperator>(expr);
        if (const Expr *lhs = cond->getLHS())
          CheckArrayAccess(lhs);
        if (const Expr *rhs = cond->getRHS())
          CheckArrayAccess(rhs);
        return;
      }
      case Stmt::CXXOperatorCallExprClass: {
        const auto *OCE = cast<CXXOperatorCallExpr>(expr);
        for (const auto *Arg : OCE->arguments())
          CheckArrayAccess(Arg);
        return;
      }
      default:
        return;
    }
  }
}

//===--- CHECK: Objective-C retain cycles ----------------------------------//

namespace {

struct RetainCycleOwner {
  VarDecl *Variable = nullptr;
  SourceRange Range;
  SourceLocation Loc;
  bool Indirect = false;

  RetainCycleOwner() = default;

  void setLocsFrom(Expr *e) {
    Loc = e->getExprLoc();
    Range = e->getSourceRange();
  }
};

} // namespace

/// Consider whether capturing the given variable can possibly lead to
/// a retain cycle.
static bool considerVariable(VarDecl *var, Expr *ref, RetainCycleOwner &owner) {
  // In ARC, it's captured strongly iff the variable has __strong
  // lifetime.  In MRR, it's captured strongly if the variable is
  // __block and has an appropriate type.
  if (var->getType().getObjCLifetime() != Qualifiers::OCL_Strong)
    return false;

  owner.Variable = var;
  if (ref)
    owner.setLocsFrom(ref);
  return true;
}

static bool findRetainCycleOwner(Sema &S, Expr *e, RetainCycleOwner &owner) {
  while (true) {
    e = e->IgnoreParens();
    if (CastExpr *cast = dyn_cast<CastExpr>(e)) {
      switch (cast->getCastKind()) {
      case CK_BitCast:
      case CK_LValueBitCast:
      case CK_LValueToRValue:
      case CK_ARCReclaimReturnedObject:
        e = cast->getSubExpr();
        continue;

      default:
        return false;
      }
    }

    if (ObjCIvarRefExpr *ref = dyn_cast<ObjCIvarRefExpr>(e)) {
      ObjCIvarDecl *ivar = ref->getDecl();
      if (ivar->getType().getObjCLifetime() != Qualifiers::OCL_Strong)
        return false;

      // Try to find a retain cycle in the base.
      if (!findRetainCycleOwner(S, ref->getBase(), owner))
        return false;

      if (ref->isFreeIvar()) owner.setLocsFrom(ref);
      owner.Indirect = true;
      return true;
    }

    if (DeclRefExpr *ref = dyn_cast<DeclRefExpr>(e)) {
      VarDecl *var = dyn_cast<VarDecl>(ref->getDecl());
      if (!var) return false;
      return considerVariable(var, ref, owner);
    }

    if (MemberExpr *member = dyn_cast<MemberExpr>(e)) {
      if (member->isArrow()) return false;

      // Don't count this as an indirect ownership.
      e = member->getBase();
      continue;
    }

    if (PseudoObjectExpr *pseudo = dyn_cast<PseudoObjectExpr>(e)) {
      // Only pay attention to pseudo-objects on property references.
      ObjCPropertyRefExpr *pre
        = dyn_cast<ObjCPropertyRefExpr>(pseudo->getSyntacticForm()
                                              ->IgnoreParens());
      if (!pre) return false;
      if (pre->isImplicitProperty()) return false;
      ObjCPropertyDecl *property = pre->getExplicitProperty();
      if (!property->isRetaining() &&
          !(property->getPropertyIvarDecl() &&
            property->getPropertyIvarDecl()->getType()
              .getObjCLifetime() == Qualifiers::OCL_Strong))
          return false;

      owner.Indirect = true;
      if (pre->isSuperReceiver()) {
        owner.Variable = S.getCurMethodDecl()->getSelfDecl();
        if (!owner.Variable)
          return false;
        owner.Loc = pre->getLocation();
        owner.Range = pre->getSourceRange();
        return true;
      }
      e = const_cast<Expr*>(cast<OpaqueValueExpr>(pre->getBase())
                              ->getSourceExpr());
      continue;
    }

    // Array ivars?

    return false;
  }
}

namespace {

  struct FindCaptureVisitor : EvaluatedExprVisitor<FindCaptureVisitor> {
    ASTContext &Context;
    VarDecl *Variable;
    Expr *Capturer = nullptr;
    bool VarWillBeReased = false;

    FindCaptureVisitor(ASTContext &Context, VarDecl *variable)
        : EvaluatedExprVisitor<FindCaptureVisitor>(Context),
          Context(Context), Variable(variable) {}

    void VisitDeclRefExpr(DeclRefExpr *ref) {
      if (ref->getDecl() == Variable && !Capturer)
        Capturer = ref;
    }

    void VisitObjCIvarRefExpr(ObjCIvarRefExpr *ref) {
      if (Capturer) return;
      Visit(ref->getBase());
      if (Capturer && ref->isFreeIvar())
        Capturer = ref;
    }

    void VisitBlockExpr(BlockExpr *block) {
      // Look inside nested blocks
      if (block->getBlockDecl()->capturesVariable(Variable))
        Visit(block->getBlockDecl()->getBody());
    }

    void VisitOpaqueValueExpr(OpaqueValueExpr *OVE) {
      if (Capturer) return;
      if (OVE->getSourceExpr())
        Visit(OVE->getSourceExpr());
    }

    void VisitBinaryOperator(BinaryOperator *BinOp) {
      if (!Variable || VarWillBeReased || BinOp->getOpcode() != BO_Assign)
        return;
      Expr *LHS = BinOp->getLHS();
      if (const DeclRefExpr *DRE = dyn_cast_or_null<DeclRefExpr>(LHS)) {
        if (DRE->getDecl() != Variable)
          return;
        if (Expr *RHS = BinOp->getRHS()) {
          RHS = RHS->IgnoreParenCasts();
          llvm::APSInt Value;
          VarWillBeReased =
            (RHS && RHS->isIntegerConstantExpr(Value, Context) && Value == 0);
        }
      }
    }
  };

} // namespace

/// Check whether the given argument is a block which captures a
/// variable.
static Expr *findCapturingExpr(Sema &S, Expr *e, RetainCycleOwner &owner) {
  assert(owner.Variable && owner.Loc.isValid());

  e = e->IgnoreParenCasts();

  // Look through [^{...} copy] and Block_copy(^{...}).
  if (ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(e)) {
    Selector Cmd = ME->getSelector();
    if (Cmd.isUnarySelector() && Cmd.getNameForSlot(0) == "copy") {
      e = ME->getInstanceReceiver();
      if (!e)
        return nullptr;
      e = e->IgnoreParenCasts();
    }
  } else if (CallExpr *CE = dyn_cast<CallExpr>(e)) {
    if (CE->getNumArgs() == 1) {
      FunctionDecl *Fn = dyn_cast_or_null<FunctionDecl>(CE->getCalleeDecl());
      if (Fn) {
        const IdentifierInfo *FnI = Fn->getIdentifier();
        if (FnI && FnI->isStr("_Block_copy")) {
          e = CE->getArg(0)->IgnoreParenCasts();
        }
      }
    }
  }

  BlockExpr *block = dyn_cast<BlockExpr>(e);
  if (!block || !block->getBlockDecl()->capturesVariable(owner.Variable))
    return nullptr;

  FindCaptureVisitor visitor(S.Context, owner.Variable);
  visitor.Visit(block->getBlockDecl()->getBody());
  return visitor.VarWillBeReased ? nullptr : visitor.Capturer;
}

static void diagnoseRetainCycle(Sema &S, Expr *capturer,
                                RetainCycleOwner &owner) {
  assert(capturer);
  assert(owner.Variable && owner.Loc.isValid());

  S.Diag(capturer->getExprLoc(), diag::warn_arc_retain_cycle)
    << owner.Variable << capturer->getSourceRange();
  S.Diag(owner.Loc, diag::note_arc_retain_cycle_owner)
    << owner.Indirect << owner.Range;
}

/// Check for a keyword selector that starts with the word 'add' or
/// 'set'.
static bool isSetterLikeSelector(Selector sel) {
  if (sel.isUnarySelector()) return false;

  StringRef str = sel.getNameForSlot(0);
  while (!str.empty() && str.front() == '_') str = str.substr(1);
  if (str.startswith("set"))
    str = str.substr(3);
  else if (str.startswith("add")) {
    // Specially whitelist 'addOperationWithBlock:'.
    if (sel.getNumArgs() == 1 && str.startswith("addOperationWithBlock"))
      return false;
    str = str.substr(3);
  }
  else
    return false;

  if (str.empty()) return true;
  return !isLowercase(str.front());
}

static Optional<int> GetNSMutableArrayArgumentIndex(Sema &S,
                                                    ObjCMessageExpr *Message) {
  bool IsMutableArray = S.NSAPIObj->isSubclassOfNSClass(
                                                Message->getReceiverInterface(),
                                                NSAPI::ClassId_NSMutableArray);
  if (!IsMutableArray) {
    return None;
  }

  Selector Sel = Message->getSelector();

  Optional<NSAPI::NSArrayMethodKind> MKOpt =
    S.NSAPIObj->getNSArrayMethodKind(Sel);
  if (!MKOpt) {
    return None;
  }

  NSAPI::NSArrayMethodKind MK = *MKOpt;

  switch (MK) {
    case NSAPI::NSMutableArr_addObject:
    case NSAPI::NSMutableArr_insertObjectAtIndex:
    case NSAPI::NSMutableArr_setObjectAtIndexedSubscript:
      return 0;
    case NSAPI::NSMutableArr_replaceObjectAtIndex:
      return 1;

    default:
      return None;
  }

  return None;
}

static
Optional<int> GetNSMutableDictionaryArgumentIndex(Sema &S,
                                                  ObjCMessageExpr *Message) {
  bool IsMutableDictionary = S.NSAPIObj->isSubclassOfNSClass(
                                            Message->getReceiverInterface(),
                                            NSAPI::ClassId_NSMutableDictionary);
  if (!IsMutableDictionary) {
    return None;
  }

  Selector Sel = Message->getSelector();

  Optional<NSAPI::NSDictionaryMethodKind> MKOpt =
    S.NSAPIObj->getNSDictionaryMethodKind(Sel);
  if (!MKOpt) {
    return None;
  }

  NSAPI::NSDictionaryMethodKind MK = *MKOpt;

  switch (MK) {
    case NSAPI::NSMutableDict_setObjectForKey:
    case NSAPI::NSMutableDict_setValueForKey:
    case NSAPI::NSMutableDict_setObjectForKeyedSubscript:
      return 0;

    default:
      return None;
  }

  return None;
}

static Optional<int> GetNSSetArgumentIndex(Sema &S, ObjCMessageExpr *Message) {
  bool IsMutableSet = S.NSAPIObj->isSubclassOfNSClass(
                                                Message->getReceiverInterface(),
                                                NSAPI::ClassId_NSMutableSet);

  bool IsMutableOrderedSet = S.NSAPIObj->isSubclassOfNSClass(
                                            Message->getReceiverInterface(),
                                            NSAPI::ClassId_NSMutableOrderedSet);
  if (!IsMutableSet && !IsMutableOrderedSet) {
    return None;
  }

  Selector Sel = Message->getSelector();

  Optional<NSAPI::NSSetMethodKind> MKOpt = S.NSAPIObj->getNSSetMethodKind(Sel);
  if (!MKOpt) {
    return None;
  }

  NSAPI::NSSetMethodKind MK = *MKOpt;

  switch (MK) {
    case NSAPI::NSMutableSet_addObject:
    case NSAPI::NSOrderedSet_setObjectAtIndex:
    case NSAPI::NSOrderedSet_setObjectAtIndexedSubscript:
    case NSAPI::NSOrderedSet_insertObjectAtIndex:
      return 0;
    case NSAPI::NSOrderedSet_replaceObjectAtIndexWithObject:
      return 1;
  }

  return None;
}

void Sema::CheckObjCCircularContainer(ObjCMessageExpr *Message) {
  if (!Message->isInstanceMessage()) {
    return;
  }

  Optional<int> ArgOpt;

  if (!(ArgOpt = GetNSMutableArrayArgumentIndex(*this, Message)) &&
      !(ArgOpt = GetNSMutableDictionaryArgumentIndex(*this, Message)) &&
      !(ArgOpt = GetNSSetArgumentIndex(*this, Message))) {
    return;
  }

  int ArgIndex = *ArgOpt;

  Expr *Arg = Message->getArg(ArgIndex)->IgnoreImpCasts();
  if (OpaqueValueExpr *OE = dyn_cast<OpaqueValueExpr>(Arg)) {
    Arg = OE->getSourceExpr()->IgnoreImpCasts();
  }

  if (Message->getReceiverKind() == ObjCMessageExpr::SuperInstance) {
    if (DeclRefExpr *ArgRE = dyn_cast<DeclRefExpr>(Arg)) {
      if (ArgRE->isObjCSelfExpr()) {
        Diag(Message->getSourceRange().getBegin(),
             diag::warn_objc_circular_container)
          << ArgRE->getDecl() << StringRef("'super'");
      }
    }
  } else {
    Expr *Receiver = Message->getInstanceReceiver()->IgnoreImpCasts();

    if (OpaqueValueExpr *OE = dyn_cast<OpaqueValueExpr>(Receiver)) {
      Receiver = OE->getSourceExpr()->IgnoreImpCasts();
    }

    if (DeclRefExpr *ReceiverRE = dyn_cast<DeclRefExpr>(Receiver)) {
      if (DeclRefExpr *ArgRE = dyn_cast<DeclRefExpr>(Arg)) {
        if (ReceiverRE->getDecl() == ArgRE->getDecl()) {
          ValueDecl *Decl = ReceiverRE->getDecl();
          Diag(Message->getSourceRange().getBegin(),
               diag::warn_objc_circular_container)
            << Decl << Decl;
          if (!ArgRE->isObjCSelfExpr()) {
            Diag(Decl->getLocation(),
                 diag::note_objc_circular_container_declared_here)
              << Decl;
          }
        }
      }
    } else if (ObjCIvarRefExpr *IvarRE = dyn_cast<ObjCIvarRefExpr>(Receiver)) {
      if (ObjCIvarRefExpr *IvarArgRE = dyn_cast<ObjCIvarRefExpr>(Arg)) {
        if (IvarRE->getDecl() == IvarArgRE->getDecl()) {
          ObjCIvarDecl *Decl = IvarRE->getDecl();
          Diag(Message->getSourceRange().getBegin(),
               diag::warn_objc_circular_container)
            << Decl << Decl;
          Diag(Decl->getLocation(),
               diag::note_objc_circular_container_declared_here)
            << Decl;
        }
      }
    }
  }
}

/// Check a message send to see if it's likely to cause a retain cycle.
void Sema::checkRetainCycles(ObjCMessageExpr *msg) {
  // Only check instance methods whose selector looks like a setter.
  if (!msg->isInstanceMessage() || !isSetterLikeSelector(msg->getSelector()))
    return;

  // Try to find a variable that the receiver is strongly owned by.
  RetainCycleOwner owner;
  if (msg->getReceiverKind() == ObjCMessageExpr::Instance) {
    if (!findRetainCycleOwner(*this, msg->getInstanceReceiver(), owner))
      return;
  } else {
    assert(msg->getReceiverKind() == ObjCMessageExpr::SuperInstance);
    owner.Variable = getCurMethodDecl()->getSelfDecl();
    owner.Loc = msg->getSuperLoc();
    owner.Range = msg->getSuperLoc();
  }

  // Check whether the receiver is captured by any of the arguments.
  const ObjCMethodDecl *MD = msg->getMethodDecl();
  for (unsigned i = 0, e = msg->getNumArgs(); i != e; ++i) {
    if (Expr *capturer = findCapturingExpr(*this, msg->getArg(i), owner)) {
      // noescape blocks should not be retained by the method.
      if (MD && MD->parameters()[i]->hasAttr<NoEscapeAttr>())
        continue;
      return diagnoseRetainCycle(*this, capturer, owner);
    }
  }
}

/// Check a property assign to see if it's likely to cause a retain cycle.
void Sema::checkRetainCycles(Expr *receiver, Expr *argument) {
  RetainCycleOwner owner;
  if (!findRetainCycleOwner(*this, receiver, owner))
    return;

  if (Expr *capturer = findCapturingExpr(*this, argument, owner))
    diagnoseRetainCycle(*this, capturer, owner);
}

void Sema::checkRetainCycles(VarDecl *Var, Expr *Init) {
  RetainCycleOwner Owner;
  if (!considerVariable(Var, /*DeclRefExpr=*/nullptr, Owner))
    return;

  // Because we don't have an expression for the variable, we have to set the
  // location explicitly here.
  Owner.Loc = Var->getLocation();
  Owner.Range = Var->getSourceRange();

  if (Expr *Capturer = findCapturingExpr(*this, Init, Owner))
    diagnoseRetainCycle(*this, Capturer, Owner);
}

static bool checkUnsafeAssignLiteral(Sema &S, SourceLocation Loc,
                                     Expr *RHS, bool isProperty) {
  // Check if RHS is an Objective-C object literal, which also can get
  // immediately zapped in a weak reference.  Note that we explicitly
  // allow ObjCStringLiterals, since those are designed to never really die.
  RHS = RHS->IgnoreParenImpCasts();

  // This enum needs to match with the 'select' in
  // warn_objc_arc_literal_assign (off-by-1).
  Sema::ObjCLiteralKind Kind = S.CheckLiteralKind(RHS);
  if (Kind == Sema::LK_String || Kind == Sema::LK_None)
    return false;

  S.Diag(Loc, diag::warn_arc_literal_assign)
    << (unsigned) Kind
    << (isProperty ? 0 : 1)
    << RHS->getSourceRange();

  return true;
}

static bool checkUnsafeAssignObject(Sema &S, SourceLocation Loc,
                                    Qualifiers::ObjCLifetime LT,
                                    Expr *RHS, bool isProperty) {
  // Strip off any implicit cast added to get to the one ARC-specific.
  while (ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(RHS)) {
    if (cast->getCastKind() == CK_ARCConsumeObject) {
      S.Diag(Loc, diag::warn_arc_retained_assign)
        << (LT == Qualifiers::OCL_ExplicitNone)
        << (isProperty ? 0 : 1)
        << RHS->getSourceRange();
      return true;
    }
    RHS = cast->getSubExpr();
  }

  if (LT == Qualifiers::OCL_Weak &&
      checkUnsafeAssignLiteral(S, Loc, RHS, isProperty))
    return true;

  return false;
}

bool Sema::checkUnsafeAssigns(SourceLocation Loc,
                              QualType LHS, Expr *RHS) {
  Qualifiers::ObjCLifetime LT = LHS.getObjCLifetime();

  if (LT != Qualifiers::OCL_Weak && LT != Qualifiers::OCL_ExplicitNone)
    return false;

  if (checkUnsafeAssignObject(*this, Loc, LT, RHS, false))
    return true;

  return false;
}

void Sema::checkUnsafeExprAssigns(SourceLocation Loc,
                              Expr *LHS, Expr *RHS) {
  QualType LHSType;
  // PropertyRef on LHS type need be directly obtained from
  // its declaration as it has a PseudoType.
  ObjCPropertyRefExpr *PRE
    = dyn_cast<ObjCPropertyRefExpr>(LHS->IgnoreParens());
  if (PRE && !PRE->isImplicitProperty()) {
    const ObjCPropertyDecl *PD = PRE->getExplicitProperty();
    if (PD)
      LHSType = PD->getType();
  }

  if (LHSType.isNull())
    LHSType = LHS->getType();

  Qualifiers::ObjCLifetime LT = LHSType.getObjCLifetime();

  if (LT == Qualifiers::OCL_Weak) {
    if (!Diags.isIgnored(diag::warn_arc_repeated_use_of_weak, Loc))
      getCurFunction()->markSafeWeakUse(LHS);
  }

  if (checkUnsafeAssigns(Loc, LHSType, RHS))
    return;

  // FIXME. Check for other life times.
  if (LT != Qualifiers::OCL_None)
    return;

  if (PRE) {
    if (PRE->isImplicitProperty())
      return;
    const ObjCPropertyDecl *PD = PRE->getExplicitProperty();
    if (!PD)
      return;

    unsigned Attributes = PD->getPropertyAttributes();
    if (Attributes & ObjCPropertyDecl::OBJC_PR_assign) {
      // when 'assign' attribute was not explicitly specified
      // by user, ignore it and rely on property type itself
      // for lifetime info.
      unsigned AsWrittenAttr = PD->getPropertyAttributesAsWritten();
      if (!(AsWrittenAttr & ObjCPropertyDecl::OBJC_PR_assign) &&
          LHSType->isObjCRetainableType())
        return;

      while (ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(RHS)) {
        if (cast->getCastKind() == CK_ARCConsumeObject) {
          Diag(Loc, diag::warn_arc_retained_property_assign)
          << RHS->getSourceRange();
          return;
        }
        RHS = cast->getSubExpr();
      }
    }
    else if (Attributes & ObjCPropertyDecl::OBJC_PR_weak) {
      if (checkUnsafeAssignObject(*this, Loc, Qualifiers::OCL_Weak, RHS, true))
        return;
    }
  }
}

//===--- CHECK: Empty statement body (-Wempty-body) ---------------------===//

static bool ShouldDiagnoseEmptyStmtBody(const SourceManager &SourceMgr,
                                        SourceLocation StmtLoc,
                                        const NullStmt *Body) {
  // Do not warn if the body is a macro that expands to nothing, e.g:
  //
  // #define CALL(x)
  // if (condition)
  //   CALL(0);
  if (Body->hasLeadingEmptyMacro())
    return false;

  // Get line numbers of statement and body.
  bool StmtLineInvalid;
  unsigned StmtLine = SourceMgr.getPresumedLineNumber(StmtLoc,
                                                      &StmtLineInvalid);
  if (StmtLineInvalid)
    return false;

  bool BodyLineInvalid;
  unsigned BodyLine = SourceMgr.getSpellingLineNumber(Body->getSemiLoc(),
                                                      &BodyLineInvalid);
  if (BodyLineInvalid)
    return false;

  // Warn if null statement and body are on the same line.
  if (StmtLine != BodyLine)
    return false;

  return true;
}

void Sema::DiagnoseEmptyStmtBody(SourceLocation StmtLoc,
                                 const Stmt *Body,
                                 unsigned DiagID) {
  // Since this is a syntactic check, don't emit diagnostic for template
  // instantiations, this just adds noise.
  if (CurrentInstantiationScope)
    return;

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Do the usual checks.
  if (!ShouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  Diag(NBody->getSemiLoc(), DiagID);
  Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
}

void Sema::DiagnoseEmptyLoopBody(const Stmt *S,
                                 const Stmt *PossibleBody) {
  assert(!CurrentInstantiationScope); // Ensured by caller

  SourceLocation StmtLoc;
  const Stmt *Body;
  unsigned DiagID;
  if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
    StmtLoc = FS->getRParenLoc();
    Body = FS->getBody();
    DiagID = diag::warn_empty_for_body;
  } else if (const WhileStmt *WS = dyn_cast<WhileStmt>(S)) {
    StmtLoc = WS->getCond()->getSourceRange().getEnd();
    Body = WS->getBody();
    DiagID = diag::warn_empty_while_body;
  } else
    return; // Neither `for' nor `while'.

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Skip expensive checks if diagnostic is disabled.
  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  // Do the usual checks.
  if (!ShouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  // `for(...);' and `while(...);' are popular idioms, so in order to keep
  // noise level low, emit diagnostics only if for/while is followed by a
  // CompoundStmt, e.g.:
  //    for (int i = 0; i < n; i++);
  //    {
  //      a(i);
  //    }
  // or if for/while is followed by a statement with more indentation
  // than for/while itself:
  //    for (int i = 0; i < n; i++);
  //      a(i);
  bool ProbableTypo = isa<CompoundStmt>(PossibleBody);
  if (!ProbableTypo) {
    bool BodyColInvalid;
    unsigned BodyCol = SourceMgr.getPresumedColumnNumber(
        PossibleBody->getBeginLoc(), &BodyColInvalid);
    if (BodyColInvalid)
      return;

    bool StmtColInvalid;
    unsigned StmtCol =
        SourceMgr.getPresumedColumnNumber(S->getBeginLoc(), &StmtColInvalid);
    if (StmtColInvalid)
      return;

    if (BodyCol > StmtCol)
      ProbableTypo = true;
  }

  if (ProbableTypo) {
    Diag(NBody->getSemiLoc(), DiagID);
    Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
  }
}

//===--- CHECK: Warn on self move with std::move. -------------------------===//

/// DiagnoseSelfMove - Emits a warning if a value is moved to itself.
void Sema::DiagnoseSelfMove(const Expr *LHSExpr, const Expr *RHSExpr,
                             SourceLocation OpLoc) {
  if (Diags.isIgnored(diag::warn_sizeof_pointer_expr_memaccess, OpLoc))
    return;

  if (inTemplateInstantiation())
    return;

  // Strip parens and casts away.
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();

  // Check for a call expression
  const CallExpr *CE = dyn_cast<CallExpr>(RHSExpr);
  if (!CE || CE->getNumArgs() != 1)
    return;

  // Check for a call to std::move
  if (!CE->isCallToStdMove())
    return;

  // Get argument from std::move
  RHSExpr = CE->getArg(0);

  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);

  // Two DeclRefExpr's, check that the decls are the same.
  if (LHSDeclRef && RHSDeclRef) {
    if (!LHSDeclRef->getDecl() || !RHSDeclRef->getDecl())
      return;
    if (LHSDeclRef->getDecl()->getCanonicalDecl() !=
        RHSDeclRef->getDecl()->getCanonicalDecl())
      return;

    Diag(OpLoc, diag::warn_self_move) << LHSExpr->getType()
                                        << LHSExpr->getSourceRange()
                                        << RHSExpr->getSourceRange();
    return;
  }

  // Member variables require a different approach to check for self moves.
  // MemberExpr's are the same if every nested MemberExpr refers to the same
  // Decl and that the base Expr's are DeclRefExpr's with the same Decl or
  // the base Expr's are CXXThisExpr's.
  const Expr *LHSBase = LHSExpr;
  const Expr *RHSBase = RHSExpr;
  const MemberExpr *LHSME = dyn_cast<MemberExpr>(LHSExpr);
  const MemberExpr *RHSME = dyn_cast<MemberExpr>(RHSExpr);
  if (!LHSME || !RHSME)
    return;

  while (LHSME && RHSME) {
    if (LHSME->getMemberDecl()->getCanonicalDecl() !=
        RHSME->getMemberDecl()->getCanonicalDecl())
      return;

    LHSBase = LHSME->getBase();
    RHSBase = RHSME->getBase();
    LHSME = dyn_cast<MemberExpr>(LHSBase);
    RHSME = dyn_cast<MemberExpr>(RHSBase);
  }

  LHSDeclRef = dyn_cast<DeclRefExpr>(LHSBase);
  RHSDeclRef = dyn_cast<DeclRefExpr>(RHSBase);
  if (LHSDeclRef && RHSDeclRef) {
    if (!LHSDeclRef->getDecl() || !RHSDeclRef->getDecl())
      return;
    if (LHSDeclRef->getDecl()->getCanonicalDecl() !=
        RHSDeclRef->getDecl()->getCanonicalDecl())
      return;

    Diag(OpLoc, diag::warn_self_move) << LHSExpr->getType()
                                        << LHSExpr->getSourceRange()
                                        << RHSExpr->getSourceRange();
    return;
  }

  if (isa<CXXThisExpr>(LHSBase) && isa<CXXThisExpr>(RHSBase))
    Diag(OpLoc, diag::warn_self_move) << LHSExpr->getType()
                                        << LHSExpr->getSourceRange()
                                        << RHSExpr->getSourceRange();
}

//===--- Layout compatibility ----------------------------------------------//

static bool isLayoutCompatible(ASTContext &C, QualType T1, QualType T2);

/// Check if two enumeration types are layout-compatible.
static bool isLayoutCompatible(ASTContext &C, EnumDecl *ED1, EnumDecl *ED2) {
  // C++11 [dcl.enum] p8:
  // Two enumeration types are layout-compatible if they have the same
  // underlying type.
  return ED1->isComplete() && ED2->isComplete() &&
         C.hasSameType(ED1->getIntegerType(), ED2->getIntegerType());
}

/// Check if two fields are layout-compatible.
static bool isLayoutCompatible(ASTContext &C, FieldDecl *Field1,
                               FieldDecl *Field2) {
  if (!isLayoutCompatible(C, Field1->getType(), Field2->getType()))
    return false;

  if (Field1->isBitField() != Field2->isBitField())
    return false;

  if (Field1->isBitField()) {
    // Make sure that the bit-fields are the same length.
    unsigned Bits1 = Field1->getBitWidthValue(C);
    unsigned Bits2 = Field2->getBitWidthValue(C);

    if (Bits1 != Bits2)
      return false;
  }

  return true;
}

/// Check if two standard-layout structs are layout-compatible.
/// (C++11 [class.mem] p17)
static bool isLayoutCompatibleStruct(ASTContext &C, RecordDecl *RD1,
                                     RecordDecl *RD2) {
  // If both records are C++ classes, check that base classes match.
  if (const CXXRecordDecl *D1CXX = dyn_cast<CXXRecordDecl>(RD1)) {
    // If one of records is a CXXRecordDecl we are in C++ mode,
    // thus the other one is a CXXRecordDecl, too.
    const CXXRecordDecl *D2CXX = cast<CXXRecordDecl>(RD2);
    // Check number of base classes.
    if (D1CXX->getNumBases() != D2CXX->getNumBases())
      return false;

    // Check the base classes.
    for (CXXRecordDecl::base_class_const_iterator
               Base1 = D1CXX->bases_begin(),
           BaseEnd1 = D1CXX->bases_end(),
              Base2 = D2CXX->bases_begin();
         Base1 != BaseEnd1;
         ++Base1, ++Base2) {
      if (!isLayoutCompatible(C, Base1->getType(), Base2->getType()))
        return false;
    }
  } else if (const CXXRecordDecl *D2CXX = dyn_cast<CXXRecordDecl>(RD2)) {
    // If only RD2 is a C++ class, it should have zero base classes.
    if (D2CXX->getNumBases() > 0)
      return false;
  }

  // Check the fields.
  RecordDecl::field_iterator Field2 = RD2->field_begin(),
                             Field2End = RD2->field_end(),
                             Field1 = RD1->field_begin(),
                             Field1End = RD1->field_end();
  for ( ; Field1 != Field1End && Field2 != Field2End; ++Field1, ++Field2) {
    if (!isLayoutCompatible(C, *Field1, *Field2))
      return false;
  }
  if (Field1 != Field1End || Field2 != Field2End)
    return false;

  return true;
}

/// Check if two standard-layout unions are layout-compatible.
/// (C++11 [class.mem] p18)
static bool isLayoutCompatibleUnion(ASTContext &C, RecordDecl *RD1,
                                    RecordDecl *RD2) {
  llvm::SmallPtrSet<FieldDecl *, 8> UnmatchedFields;
  for (auto *Field2 : RD2->fields())
    UnmatchedFields.insert(Field2);

  for (auto *Field1 : RD1->fields()) {
    llvm::SmallPtrSet<FieldDecl *, 8>::iterator
        I = UnmatchedFields.begin(),
        E = UnmatchedFields.end();

    for ( ; I != E; ++I) {
      if (isLayoutCompatible(C, Field1, *I)) {
        bool Result = UnmatchedFields.erase(*I);
        (void) Result;
        assert(Result);
        break;
      }
    }
    if (I == E)
      return false;
  }

  return UnmatchedFields.empty();
}

static bool isLayoutCompatible(ASTContext &C, RecordDecl *RD1,
                               RecordDecl *RD2) {
  if (RD1->isUnion() != RD2->isUnion())
    return false;

  if (RD1->isUnion())
    return isLayoutCompatibleUnion(C, RD1, RD2);
  else
    return isLayoutCompatibleStruct(C, RD1, RD2);
}

/// Check if two types are layout-compatible in C++11 sense.
static bool isLayoutCompatible(ASTContext &C, QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return false;

  // C++11 [basic.types] p11:
  // If two types T1 and T2 are the same type, then T1 and T2 are
  // layout-compatible types.
  if (C.hasSameType(T1, T2))
    return true;

  T1 = T1.getCanonicalType().getUnqualifiedType();
  T2 = T2.getCanonicalType().getUnqualifiedType();

  const Type::TypeClass TC1 = T1->getTypeClass();
  const Type::TypeClass TC2 = T2->getTypeClass();

  if (TC1 != TC2)
    return false;

  if (TC1 == Type::Enum) {
    return isLayoutCompatible(C,
                              cast<EnumType>(T1)->getDecl(),
                              cast<EnumType>(T2)->getDecl());
  } else if (TC1 == Type::Record) {
    if (!T1->isStandardLayoutType() || !T2->isStandardLayoutType())
      return false;

    return isLayoutCompatible(C,
                              cast<RecordType>(T1)->getDecl(),
                              cast<RecordType>(T2)->getDecl());
  }

  return false;
}

//===--- CHECK: pointer_with_type_tag attribute: datatypes should match ----//

/// Given a type tag expression find the type tag itself.
///
/// \param TypeExpr Type tag expression, as it appears in user's code.
///
/// \param VD Declaration of an identifier that appears in a type tag.
///
/// \param MagicValue Type tag magic value.
static bool FindTypeTagExpr(const Expr *TypeExpr, const ASTContext &Ctx,
                            const ValueDecl **VD, uint64_t *MagicValue) {
  while(true) {
    if (!TypeExpr)
      return false;

    TypeExpr = TypeExpr->IgnoreParenImpCasts()->IgnoreParenCasts();

    switch (TypeExpr->getStmtClass()) {
    case Stmt::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(TypeExpr);
      if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref) {
        TypeExpr = UO->getSubExpr();
        continue;
      }
      return false;
    }

    case Stmt::DeclRefExprClass: {
      const DeclRefExpr *DRE = cast<DeclRefExpr>(TypeExpr);
      *VD = DRE->getDecl();
      return true;
    }

    case Stmt::IntegerLiteralClass: {
      const IntegerLiteral *IL = cast<IntegerLiteral>(TypeExpr);
      llvm::APInt MagicValueAPInt = IL->getValue();
      if (MagicValueAPInt.getActiveBits() <= 64) {
        *MagicValue = MagicValueAPInt.getZExtValue();
        return true;
      } else
        return false;
    }

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: {
      const AbstractConditionalOperator *ACO =
          cast<AbstractConditionalOperator>(TypeExpr);
      bool Result;
      if (ACO->getCond()->EvaluateAsBooleanCondition(Result, Ctx)) {
        if (Result)
          TypeExpr = ACO->getTrueExpr();
        else
          TypeExpr = ACO->getFalseExpr();
        continue;
      }
      return false;
    }

    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(TypeExpr);
      if (BO->getOpcode() == BO_Comma) {
        TypeExpr = BO->getRHS();
        continue;
      }
      return false;
    }

    default:
      return false;
    }
  }
}

/// Retrieve the C type corresponding to type tag TypeExpr.
///
/// \param TypeExpr Expression that specifies a type tag.
///
/// \param MagicValues Registered magic values.
///
/// \param FoundWrongKind Set to true if a type tag was found, but of a wrong
///        kind.
///
/// \param TypeInfo Information about the corresponding C type.
///
/// \returns true if the corresponding C type was found.
static bool GetMatchingCType(
        const IdentifierInfo *ArgumentKind,
        const Expr *TypeExpr, const ASTContext &Ctx,
        const llvm::DenseMap<Sema::TypeTagMagicValue,
                             Sema::TypeTagData> *MagicValues,
        bool &FoundWrongKind,
        Sema::TypeTagData &TypeInfo) {
  FoundWrongKind = false;

  // Variable declaration that has type_tag_for_datatype attribute.
  const ValueDecl *VD = nullptr;

  uint64_t MagicValue;

  if (!FindTypeTagExpr(TypeExpr, Ctx, &VD, &MagicValue))
    return false;

  if (VD) {
    if (TypeTagForDatatypeAttr *I = VD->getAttr<TypeTagForDatatypeAttr>()) {
      if (I->getArgumentKind() != ArgumentKind) {
        FoundWrongKind = true;
        return false;
      }
      TypeInfo.Type = I->getMatchingCType();
      TypeInfo.LayoutCompatible = I->getLayoutCompatible();
      TypeInfo.MustBeNull = I->getMustBeNull();
      return true;
    }
    return false;
  }

  if (!MagicValues)
    return false;

  llvm::DenseMap<Sema::TypeTagMagicValue,
                 Sema::TypeTagData>::const_iterator I =
      MagicValues->find(std::make_pair(ArgumentKind, MagicValue));
  if (I == MagicValues->end())
    return false;

  TypeInfo = I->second;
  return true;
}

void Sema::RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                      uint64_t MagicValue, QualType Type,
                                      bool LayoutCompatible,
                                      bool MustBeNull) {
  if (!TypeTagForDatatypeMagicValues)
    TypeTagForDatatypeMagicValues.reset(
        new llvm::DenseMap<TypeTagMagicValue, TypeTagData>);

  TypeTagMagicValue Magic(ArgumentKind, MagicValue);
  (*TypeTagForDatatypeMagicValues)[Magic] =
      TypeTagData(Type, LayoutCompatible, MustBeNull);
}

static bool IsSameCharType(QualType T1, QualType T2) {
  const BuiltinType *BT1 = T1->getAs<BuiltinType>();
  if (!BT1)
    return false;

  const BuiltinType *BT2 = T2->getAs<BuiltinType>();
  if (!BT2)
    return false;

  BuiltinType::Kind T1Kind = BT1->getKind();
  BuiltinType::Kind T2Kind = BT2->getKind();

  return (T1Kind == BuiltinType::SChar  && T2Kind == BuiltinType::Char_S) ||
         (T1Kind == BuiltinType::UChar  && T2Kind == BuiltinType::Char_U) ||
         (T1Kind == BuiltinType::Char_U && T2Kind == BuiltinType::UChar) ||
         (T1Kind == BuiltinType::Char_S && T2Kind == BuiltinType::SChar);
}

void Sema::CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                    const ArrayRef<const Expr *> ExprArgs,
                                    SourceLocation CallSiteLoc) {
  const IdentifierInfo *ArgumentKind = Attr->getArgumentKind();
  bool IsPointerAttr = Attr->getIsPointer();

  // Retrieve the argument representing the 'type_tag'.
  unsigned TypeTagIdxAST = Attr->getTypeTagIdx().getASTIndex();
  if (TypeTagIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 0 << Attr->getTypeTagIdx().getSourceIndex();
    return;
  }
  const Expr *TypeTagExpr = ExprArgs[TypeTagIdxAST];
  bool FoundWrongKind;
  TypeTagData TypeInfo;
  if (!GetMatchingCType(ArgumentKind, TypeTagExpr, Context,
                        TypeTagForDatatypeMagicValues.get(),
                        FoundWrongKind, TypeInfo)) {
    if (FoundWrongKind)
      Diag(TypeTagExpr->getExprLoc(),
           diag::warn_type_tag_for_datatype_wrong_kind)
        << TypeTagExpr->getSourceRange();
    return;
  }

  // Retrieve the argument representing the 'arg_idx'.
  unsigned ArgumentIdxAST = Attr->getArgumentIdx().getASTIndex();
  if (ArgumentIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 1 << Attr->getArgumentIdx().getSourceIndex();
    return;
  }
  const Expr *ArgumentExpr = ExprArgs[ArgumentIdxAST];
  if (IsPointerAttr) {
    // Skip implicit cast of pointer to `void *' (as a function argument).
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgumentExpr))
      if (ICE->getType()->isVoidPointerType() &&
          ICE->getCastKind() == CK_BitCast)
        ArgumentExpr = ICE->getSubExpr();
  }
  QualType ArgumentType = ArgumentExpr->getType();

  // Passing a `void*' pointer shouldn't trigger a warning.
  if (IsPointerAttr && ArgumentType->isVoidPointerType())
    return;

  if (TypeInfo.MustBeNull) {
    // Type tag with matching void type requires a null pointer.
    if (!ArgumentExpr->isNullPointerConstant(Context,
                                             Expr::NPC_ValueDependentIsNotNull)) {
      Diag(ArgumentExpr->getExprLoc(),
           diag::warn_type_safety_null_pointer_required)
          << ArgumentKind->getName()
          << ArgumentExpr->getSourceRange()
          << TypeTagExpr->getSourceRange();
    }
    return;
  }

  QualType RequiredType = TypeInfo.Type;
  if (IsPointerAttr)
    RequiredType = Context.getPointerType(RequiredType);

  bool mismatch = false;
  if (!TypeInfo.LayoutCompatible) {
    mismatch = !Context.hasSameType(ArgumentType, RequiredType);

    // C++11 [basic.fundamental] p1:
    // Plain char, signed char, and unsigned char are three distinct types.
    //
    // But we treat plain `char' as equivalent to `signed char' or `unsigned
    // char' depending on the current char signedness mode.
    if (mismatch)
      if ((IsPointerAttr && IsSameCharType(ArgumentType->getPointeeType(),
                                           RequiredType->getPointeeType())) ||
          (!IsPointerAttr && IsSameCharType(ArgumentType, RequiredType)))
        mismatch = false;
  } else
    if (IsPointerAttr)
      mismatch = !isLayoutCompatible(Context,
                                     ArgumentType->getPointeeType(),
                                     RequiredType->getPointeeType());
    else
      mismatch = !isLayoutCompatible(Context, ArgumentType, RequiredType);

  if (mismatch)
    Diag(ArgumentExpr->getExprLoc(), diag::warn_type_safety_type_mismatch)
        << ArgumentType << ArgumentKind
        << TypeInfo.LayoutCompatible << RequiredType
        << ArgumentExpr->getSourceRange()
        << TypeTagExpr->getSourceRange();
}

void Sema::AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                         CharUnits Alignment) {
  MisalignedMembers.emplace_back(E, RD, MD, Alignment);
}

void Sema::DiagnoseMisalignedMembers() {
  for (MisalignedMember &m : MisalignedMembers) {
    const NamedDecl *ND = m.RD;
    if (ND->getName().empty()) {
      if (const TypedefNameDecl *TD = m.RD->getTypedefNameForAnonDecl())
        ND = TD;
    }
    Diag(m.E->getBeginLoc(), diag::warn_taking_address_of_packed_member)
        << m.MD << ND << m.E->getSourceRange();
  }
  MisalignedMembers.clear();
}

void Sema::DiscardMisalignedMemberAddress(const Type *T, Expr *E) {
  E = E->IgnoreParens();
  if (!T->isPointerType() && !T->isIntegerType())
    return;
  if (isa<UnaryOperator>(E) &&
      cast<UnaryOperator>(E)->getOpcode() == UO_AddrOf) {
    auto *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
    if (isa<MemberExpr>(Op)) {
      auto MA = std::find(MisalignedMembers.begin(), MisalignedMembers.end(),
                          MisalignedMember(Op));
      if (MA != MisalignedMembers.end() &&
          (T->isIntegerType() ||
           (T->isPointerType() && (T->getPointeeType()->isIncompleteType() ||
                                   Context.getTypeAlignInChars(
                                       T->getPointeeType()) <= MA->Alignment))))
        MisalignedMembers.erase(MA);
    }
  }
}

void Sema::RefersToMemberWithReducedAlignment(
    Expr *E,
    llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
        Action) {
  const auto *ME = dyn_cast<MemberExpr>(E);
  if (!ME)
    return;

  // No need to check expressions with an __unaligned-qualified type.
  if (E->getType().getQualifiers().hasUnaligned())
    return;

  // For a chain of MemberExpr like "a.b.c.d" this list
  // will keep FieldDecl's like [d, c, b].
  SmallVector<FieldDecl *, 4> ReverseMemberChain;
  const MemberExpr *TopME = nullptr;
  bool AnyIsPacked = false;
  do {
    QualType BaseType = ME->getBase()->getType();
    if (ME->isArrow())
      BaseType = BaseType->getPointeeType();
    RecordDecl *RD = BaseType->getAs<RecordType>()->getDecl();
    if (RD->isInvalidDecl())
      return;

    ValueDecl *MD = ME->getMemberDecl();
    auto *FD = dyn_cast<FieldDecl>(MD);
    // We do not care about non-data members.
    if (!FD || FD->isInvalidDecl())
      return;

    AnyIsPacked =
        AnyIsPacked || (RD->hasAttr<PackedAttr>() || MD->hasAttr<PackedAttr>());
    ReverseMemberChain.push_back(FD);

    TopME = ME;
    ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParens());
  } while (ME);
  assert(TopME && "We did not compute a topmost MemberExpr!");

  // Not the scope of this diagnostic.
  if (!AnyIsPacked)
    return;

  const Expr *TopBase = TopME->getBase()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(TopBase);
  // TODO: The innermost base of the member expression may be too complicated.
  // For now, just disregard these cases. This is left for future
  // improvement.
  if (!DRE && !isa<CXXThisExpr>(TopBase))
      return;

  // Alignment expected by the whole expression.
  CharUnits ExpectedAlignment = Context.getTypeAlignInChars(E->getType());

  // No need to do anything else with this case.
  if (ExpectedAlignment.isOne())
    return;

  // Synthesize offset of the whole access.
  CharUnits Offset;
  for (auto I = ReverseMemberChain.rbegin(); I != ReverseMemberChain.rend();
       I++) {
    Offset += Context.toCharUnitsFromBits(Context.getFieldOffset(*I));
  }

  // Compute the CompleteObjectAlignment as the alignment of the whole chain.
  CharUnits CompleteObjectAlignment = Context.getTypeAlignInChars(
      ReverseMemberChain.back()->getParent()->getTypeForDecl());

  // The base expression of the innermost MemberExpr may give
  // stronger guarantees than the class containing the member.
  if (DRE && !TopME->isArrow()) {
    const ValueDecl *VD = DRE->getDecl();
    if (!VD->getType()->isReferenceType())
      CompleteObjectAlignment =
          std::max(CompleteObjectAlignment, Context.getDeclAlign(VD));
  }

  // Check if the synthesized offset fulfills the alignment.
  if (Offset % ExpectedAlignment != 0 ||
      // It may fulfill the offset it but the effective alignment may still be
      // lower than the expected expression alignment.
      CompleteObjectAlignment < ExpectedAlignment) {
    // If this happens, we want to determine a sensible culprit of this.
    // Intuitively, watching the chain of member expressions from right to
    // left, we start with the required alignment (as required by the field
    // type) but some packed attribute in that chain has reduced the alignment.
    // It may happen that another packed structure increases it again. But if
    // we are here such increase has not been enough. So pointing the first
    // FieldDecl that either is packed or else its RecordDecl is,
    // seems reasonable.
    FieldDecl *FD = nullptr;
    CharUnits Alignment;
    for (FieldDecl *FDI : ReverseMemberChain) {
      if (FDI->hasAttr<PackedAttr>() ||
          FDI->getParent()->hasAttr<PackedAttr>()) {
        FD = FDI;
        Alignment = std::min(
            Context.getTypeAlignInChars(FD->getType()),
            Context.getTypeAlignInChars(FD->getParent()->getTypeForDecl()));
        break;
      }
    }
    assert(FD && "We did not find a packed FieldDecl!");
    Action(E, FD->getParent(), FD, Alignment);
  }
}

void Sema::CheckAddressOfPackedMember(Expr *rhs) {
  using namespace std::placeholders;

  RefersToMemberWithReducedAlignment(
      rhs, std::bind(&Sema::AddPotentialMisalignedMembers, std::ref(*this), _1,
                     _2, _3, _4));
}
