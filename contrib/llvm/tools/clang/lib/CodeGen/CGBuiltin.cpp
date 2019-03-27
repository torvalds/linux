//===---- CGBuiltin.cpp - Emit LLVM Code for builtins ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Builtin calls as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGObjCRuntime.h"
#include "CGOpenCLRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/OSLog.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetParser.h"
#include <sstream>

using namespace clang;
using namespace CodeGen;
using namespace llvm;

static
int64_t clamp(int64_t Value, int64_t Low, int64_t High) {
  return std::min(High, std::max(Low, Value));
}

/// getBuiltinLibFunction - Given a builtin id for a function like
/// "__builtin_fabsf", return a Function* for "fabsf".
llvm::Constant *CodeGenModule::getBuiltinLibFunction(const FunctionDecl *FD,
                                                     unsigned BuiltinID) {
  assert(Context.BuiltinInfo.isLibFunction(BuiltinID));

  // Get the name, skip over the __builtin_ prefix (if necessary).
  StringRef Name;
  GlobalDecl D(FD);

  // If the builtin has been declared explicitly with an assembler label,
  // use the mangled name. This differs from the plain label on platforms
  // that prefix labels.
  if (FD->hasAttr<AsmLabelAttr>())
    Name = getMangledName(D);
  else
    Name = Context.BuiltinInfo.getName(BuiltinID) + 10;

  llvm::FunctionType *Ty =
    cast<llvm::FunctionType>(getTypes().ConvertType(FD->getType()));

  return GetOrCreateLLVMFunction(Name, Ty, D, /*ForVTable=*/false);
}

/// Emit the conversions required to turn the given value into an
/// integer of the given size.
static Value *EmitToInt(CodeGenFunction &CGF, llvm::Value *V,
                        QualType T, llvm::IntegerType *IntType) {
  V = CGF.EmitToMemory(V, T);

  if (V->getType()->isPointerTy())
    return CGF.Builder.CreatePtrToInt(V, IntType);

  assert(V->getType() == IntType);
  return V;
}

static Value *EmitFromInt(CodeGenFunction &CGF, llvm::Value *V,
                          QualType T, llvm::Type *ResultType) {
  V = CGF.EmitFromMemory(V, T);

  if (ResultType->isPointerTy())
    return CGF.Builder.CreateIntToPtr(V, ResultType);

  assert(V->getType() == ResultType);
  return V;
}

/// Utility to insert an atomic instruction based on Intrinsic::ID
/// and the expression node.
static Value *MakeBinaryAtomicValue(
    CodeGenFunction &CGF, llvm::AtomicRMWInst::BinOp Kind, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(T,
                                  E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  llvm::Value *DestPtr = CGF.EmitScalarExpr(E->getArg(0));
  unsigned AddrSpace = DestPtr->getType()->getPointerAddressSpace();

  llvm::IntegerType *IntType =
    llvm::IntegerType::get(CGF.getLLVMContext(),
                           CGF.getContext().getTypeSize(T));
  llvm::Type *IntPtrType = IntType->getPointerTo(AddrSpace);

  llvm::Value *Args[2];
  Args[0] = CGF.Builder.CreateBitCast(DestPtr, IntPtrType);
  Args[1] = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Args[1]->getType();
  Args[1] = EmitToInt(CGF, Args[1], T, IntType);

  llvm::Value *Result = CGF.Builder.CreateAtomicRMW(
      Kind, Args[0], Args[1], Ordering);
  return EmitFromInt(CGF, Result, T, ValueType);
}

static Value *EmitNontemporalStore(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Val = CGF.EmitScalarExpr(E->getArg(0));
  Value *Address = CGF.EmitScalarExpr(E->getArg(1));

  // Convert the type of the pointer to a pointer to the stored type.
  Val = CGF.EmitToMemory(Val, E->getArg(0)->getType());
  Value *BC = CGF.Builder.CreateBitCast(
      Address, llvm::PointerType::getUnqual(Val->getType()), "cast");
  LValue LV = CGF.MakeNaturalAlignAddrLValue(BC, E->getArg(0)->getType());
  LV.setNontemporal(true);
  CGF.EmitStoreOfScalar(Val, LV, false);
  return nullptr;
}

static Value *EmitNontemporalLoad(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Address = CGF.EmitScalarExpr(E->getArg(0));

  LValue LV = CGF.MakeNaturalAlignAddrLValue(Address, E->getType());
  LV.setNontemporal(true);
  return CGF.EmitLoadOfScalar(LV, E->getExprLoc());
}

static RValue EmitBinaryAtomic(CodeGenFunction &CGF,
                               llvm::AtomicRMWInst::BinOp Kind,
                               const CallExpr *E) {
  return RValue::get(MakeBinaryAtomicValue(CGF, Kind, E));
}

/// Utility to insert an atomic instruction based Intrinsic::ID and
/// the expression node, where the return value is the result of the
/// operation.
static RValue EmitBinaryAtomicPost(CodeGenFunction &CGF,
                                   llvm::AtomicRMWInst::BinOp Kind,
                                   const CallExpr *E,
                                   Instruction::BinaryOps Op,
                                   bool Invert = false) {
  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(T,
                                  E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  llvm::Value *DestPtr = CGF.EmitScalarExpr(E->getArg(0));
  unsigned AddrSpace = DestPtr->getType()->getPointerAddressSpace();

  llvm::IntegerType *IntType =
    llvm::IntegerType::get(CGF.getLLVMContext(),
                           CGF.getContext().getTypeSize(T));
  llvm::Type *IntPtrType = IntType->getPointerTo(AddrSpace);

  llvm::Value *Args[2];
  Args[1] = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Args[1]->getType();
  Args[1] = EmitToInt(CGF, Args[1], T, IntType);
  Args[0] = CGF.Builder.CreateBitCast(DestPtr, IntPtrType);

  llvm::Value *Result = CGF.Builder.CreateAtomicRMW(
      Kind, Args[0], Args[1], llvm::AtomicOrdering::SequentiallyConsistent);
  Result = CGF.Builder.CreateBinOp(Op, Result, Args[1]);
  if (Invert)
    Result = CGF.Builder.CreateBinOp(llvm::Instruction::Xor, Result,
                                     llvm::ConstantInt::get(IntType, -1));
  Result = EmitFromInt(CGF, Result, T, ValueType);
  return RValue::get(Result);
}

/// Utility to insert an atomic cmpxchg instruction.
///
/// @param CGF The current codegen function.
/// @param E   Builtin call expression to convert to cmpxchg.
///            arg0 - address to operate on
///            arg1 - value to compare with
///            arg2 - new value
/// @param ReturnBool Specifies whether to return success flag of
///                   cmpxchg result or the old value.
///
/// @returns result of cmpxchg, according to ReturnBool
///
/// Note: In order to lower Microsoft's _InterlockedCompareExchange* intrinsics
/// invoke the function EmitAtomicCmpXchgForMSIntrin.
static Value *MakeAtomicCmpXchgValue(CodeGenFunction &CGF, const CallExpr *E,
                                     bool ReturnBool) {
  QualType T = ReturnBool ? E->getArg(1)->getType() : E->getType();
  llvm::Value *DestPtr = CGF.EmitScalarExpr(E->getArg(0));
  unsigned AddrSpace = DestPtr->getType()->getPointerAddressSpace();

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(), CGF.getContext().getTypeSize(T));
  llvm::Type *IntPtrType = IntType->getPointerTo(AddrSpace);

  Value *Args[3];
  Args[0] = CGF.Builder.CreateBitCast(DestPtr, IntPtrType);
  Args[1] = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Args[1]->getType();
  Args[1] = EmitToInt(CGF, Args[1], T, IntType);
  Args[2] = EmitToInt(CGF, CGF.EmitScalarExpr(E->getArg(2)), T, IntType);

  Value *Pair = CGF.Builder.CreateAtomicCmpXchg(
      Args[0], Args[1], Args[2], llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering::SequentiallyConsistent);
  if (ReturnBool)
    // Extract boolean success flag and zext it to int.
    return CGF.Builder.CreateZExt(CGF.Builder.CreateExtractValue(Pair, 1),
                                  CGF.ConvertType(E->getType()));
  else
    // Extract old value and emit it using the same type as compare value.
    return EmitFromInt(CGF, CGF.Builder.CreateExtractValue(Pair, 0), T,
                       ValueType);
}

/// This function should be invoked to emit atomic cmpxchg for Microsoft's
/// _InterlockedCompareExchange* intrinsics which have the following signature:
/// T _InterlockedCompareExchange(T volatile *Destination,
///                               T Exchange,
///                               T Comparand);
///
/// Whereas the llvm 'cmpxchg' instruction has the following syntax:
/// cmpxchg *Destination, Comparand, Exchange.
/// So we need to swap Comparand and Exchange when invoking
/// CreateAtomicCmpXchg. That is the reason we could not use the above utility
/// function MakeAtomicCmpXchgValue since it expects the arguments to be
/// already swapped.

static
Value *EmitAtomicCmpXchgForMSIntrin(CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering SuccessOrdering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(
      E->getType(), E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(E->getType(),
                                                 E->getArg(1)->getType()));
  assert(CGF.getContext().hasSameUnqualifiedType(E->getType(),
                                                 E->getArg(2)->getType()));

  auto *Destination = CGF.EmitScalarExpr(E->getArg(0));
  auto *Comparand = CGF.EmitScalarExpr(E->getArg(2));
  auto *Exchange = CGF.EmitScalarExpr(E->getArg(1));

  // For Release ordering, the failure ordering should be Monotonic.
  auto FailureOrdering = SuccessOrdering == AtomicOrdering::Release ?
                         AtomicOrdering::Monotonic :
                         SuccessOrdering;

  auto *Result = CGF.Builder.CreateAtomicCmpXchg(
                   Destination, Comparand, Exchange,
                   SuccessOrdering, FailureOrdering);
  Result->setVolatile(true);
  return CGF.Builder.CreateExtractValue(Result, 0);
}

static Value *EmitAtomicIncrementValue(CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = CGF.ConvertType(E->getType());
  auto *Result = CGF.Builder.CreateAtomicRMW(
                   AtomicRMWInst::Add,
                   CGF.EmitScalarExpr(E->getArg(0)),
                   ConstantInt::get(IntTy, 1),
                   Ordering);
  return CGF.Builder.CreateAdd(Result, ConstantInt::get(IntTy, 1));
}

static Value *EmitAtomicDecrementValue(CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = CGF.ConvertType(E->getType());
  auto *Result = CGF.Builder.CreateAtomicRMW(
                   AtomicRMWInst::Sub,
                   CGF.EmitScalarExpr(E->getArg(0)),
                   ConstantInt::get(IntTy, 1),
                   Ordering);
  return CGF.Builder.CreateSub(Result, ConstantInt::get(IntTy, 1));
}

// Emit a simple mangled intrinsic that has 1 argument and a return type
// matching the argument type.
static Value *emitUnaryBuiltin(CodeGenFunction &CGF,
                               const CallExpr *E,
                               unsigned IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));

  Value *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
  return CGF.Builder.CreateCall(F, Src0);
}

// Emit an intrinsic that has 2 operands of the same type as its result.
static Value *emitBinaryBuiltin(CodeGenFunction &CGF,
                                const CallExpr *E,
                                unsigned IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  Value *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
  return CGF.Builder.CreateCall(F, { Src0, Src1 });
}

// Emit an intrinsic that has 3 operands of the same type as its result.
static Value *emitTernaryBuiltin(CodeGenFunction &CGF,
                                 const CallExpr *E,
                                 unsigned IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Value *Src2 = CGF.EmitScalarExpr(E->getArg(2));

  Value *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
  return CGF.Builder.CreateCall(F, { Src0, Src1, Src2 });
}

// Emit an intrinsic that has 1 float or double operand, and 1 integer.
static Value *emitFPIntBuiltin(CodeGenFunction &CGF,
                               const CallExpr *E,
                               unsigned IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  Value *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
  return CGF.Builder.CreateCall(F, {Src0, Src1});
}

/// EmitFAbs - Emit a call to @llvm.fabs().
static Value *EmitFAbs(CodeGenFunction &CGF, Value *V) {
  Value *F = CGF.CGM.getIntrinsic(Intrinsic::fabs, V->getType());
  llvm::CallInst *Call = CGF.Builder.CreateCall(F, V);
  Call->setDoesNotAccessMemory();
  return Call;
}

/// Emit the computation of the sign bit for a floating point value. Returns
/// the i1 sign bit value.
static Value *EmitSignBit(CodeGenFunction &CGF, Value *V) {
  LLVMContext &C = CGF.CGM.getLLVMContext();

  llvm::Type *Ty = V->getType();
  int Width = Ty->getPrimitiveSizeInBits();
  llvm::Type *IntTy = llvm::IntegerType::get(C, Width);
  V = CGF.Builder.CreateBitCast(V, IntTy);
  if (Ty->isPPC_FP128Ty()) {
    // We want the sign bit of the higher-order double. The bitcast we just
    // did works as if the double-double was stored to memory and then
    // read as an i128. The "store" will put the higher-order double in the
    // lower address in both little- and big-Endian modes, but the "load"
    // will treat those bits as a different part of the i128: the low bits in
    // little-Endian, the high bits in big-Endian. Therefore, on big-Endian
    // we need to shift the high bits down to the low before truncating.
    Width >>= 1;
    if (CGF.getTarget().isBigEndian()) {
      Value *ShiftCst = llvm::ConstantInt::get(IntTy, Width);
      V = CGF.Builder.CreateLShr(V, ShiftCst);
    }
    // We are truncating value in order to extract the higher-order
    // double, which we will be using to extract the sign from.
    IntTy = llvm::IntegerType::get(C, Width);
    V = CGF.Builder.CreateTrunc(V, IntTy);
  }
  Value *Zero = llvm::Constant::getNullValue(IntTy);
  return CGF.Builder.CreateICmpSLT(V, Zero);
}

static RValue emitLibraryCall(CodeGenFunction &CGF, const FunctionDecl *FD,
                              const CallExpr *E, llvm::Constant *calleeValue) {
  CGCallee callee = CGCallee::forDirect(calleeValue, GlobalDecl(FD));
  return CGF.EmitCall(E->getCallee()->getType(), callee, E, ReturnValueSlot());
}

/// Emit a call to llvm.{sadd,uadd,ssub,usub,smul,umul}.with.overflow.*
/// depending on IntrinsicID.
///
/// \arg CGF The current codegen function.
/// \arg IntrinsicID The ID for the Intrinsic we wish to generate.
/// \arg X The first argument to the llvm.*.with.overflow.*.
/// \arg Y The second argument to the llvm.*.with.overflow.*.
/// \arg Carry The carry returned by the llvm.*.with.overflow.*.
/// \returns The result (i.e. sum/product) returned by the intrinsic.
static llvm::Value *EmitOverflowIntrinsic(CodeGenFunction &CGF,
                                          const llvm::Intrinsic::ID IntrinsicID,
                                          llvm::Value *X, llvm::Value *Y,
                                          llvm::Value *&Carry) {
  // Make sure we have integers of the same width.
  assert(X->getType() == Y->getType() &&
         "Arguments must be the same type. (Did you forget to make sure both "
         "arguments have the same integer width?)");

  llvm::Value *Callee = CGF.CGM.getIntrinsic(IntrinsicID, X->getType());
  llvm::Value *Tmp = CGF.Builder.CreateCall(Callee, {X, Y});
  Carry = CGF.Builder.CreateExtractValue(Tmp, 1);
  return CGF.Builder.CreateExtractValue(Tmp, 0);
}

static Value *emitRangedBuiltin(CodeGenFunction &CGF,
                                unsigned IntrinsicID,
                                int low, int high) {
    llvm::MDBuilder MDHelper(CGF.getLLVMContext());
    llvm::MDNode *RNode = MDHelper.createRange(APInt(32, low), APInt(32, high));
    Value *F = CGF.CGM.getIntrinsic(IntrinsicID, {});
    llvm::Instruction *Call = CGF.Builder.CreateCall(F);
    Call->setMetadata(llvm::LLVMContext::MD_range, RNode);
    return Call;
}

namespace {
  struct WidthAndSignedness {
    unsigned Width;
    bool Signed;
  };
}

static WidthAndSignedness
getIntegerWidthAndSignedness(const clang::ASTContext &context,
                             const clang::QualType Type) {
  assert(Type->isIntegerType() && "Given type is not an integer.");
  unsigned Width = Type->isBooleanType() ? 1 : context.getTypeInfo(Type).Width;
  bool Signed = Type->isSignedIntegerType();
  return {Width, Signed};
}

// Given one or more integer types, this function produces an integer type that
// encompasses them: any value in one of the given types could be expressed in
// the encompassing type.
static struct WidthAndSignedness
EncompassingIntegerType(ArrayRef<struct WidthAndSignedness> Types) {
  assert(Types.size() > 0 && "Empty list of types.");

  // If any of the given types is signed, we must return a signed type.
  bool Signed = false;
  for (const auto &Type : Types) {
    Signed |= Type.Signed;
  }

  // The encompassing type must have a width greater than or equal to the width
  // of the specified types.  Additionally, if the encompassing type is signed,
  // its width must be strictly greater than the width of any unsigned types
  // given.
  unsigned Width = 0;
  for (const auto &Type : Types) {
    unsigned MinWidth = Type.Width + (Signed && !Type.Signed);
    if (Width < MinWidth) {
      Width = MinWidth;
    }
  }

  return {Width, Signed};
}

Value *CodeGenFunction::EmitVAStartEnd(Value *ArgValue, bool IsStart) {
  llvm::Type *DestType = Int8PtrTy;
  if (ArgValue->getType() != DestType)
    ArgValue =
        Builder.CreateBitCast(ArgValue, DestType, ArgValue->getName().data());

  Intrinsic::ID inst = IsStart ? Intrinsic::vastart : Intrinsic::vaend;
  return Builder.CreateCall(CGM.getIntrinsic(inst), ArgValue);
}

/// Checks if using the result of __builtin_object_size(p, @p From) in place of
/// __builtin_object_size(p, @p To) is correct
static bool areBOSTypesCompatible(int From, int To) {
  // Note: Our __builtin_object_size implementation currently treats Type=0 and
  // Type=2 identically. Encoding this implementation detail here may make
  // improving __builtin_object_size difficult in the future, so it's omitted.
  return From == To || (From == 0 && To == 1) || (From == 3 && To == 2);
}

static llvm::Value *
getDefaultBuiltinObjectSizeResult(unsigned Type, llvm::IntegerType *ResType) {
  return ConstantInt::get(ResType, (Type & 2) ? 0 : -1, /*isSigned=*/true);
}

llvm::Value *
CodeGenFunction::evaluateOrEmitBuiltinObjectSize(const Expr *E, unsigned Type,
                                                 llvm::IntegerType *ResType,
                                                 llvm::Value *EmittedE) {
  uint64_t ObjectSize;
  if (!E->tryEvaluateObjectSize(ObjectSize, getContext(), Type))
    return emitBuiltinObjectSize(E, Type, ResType, EmittedE);
  return ConstantInt::get(ResType, ObjectSize, /*isSigned=*/true);
}

/// Returns a Value corresponding to the size of the given expression.
/// This Value may be either of the following:
///   - A llvm::Argument (if E is a param with the pass_object_size attribute on
///     it)
///   - A call to the @llvm.objectsize intrinsic
///
/// EmittedE is the result of emitting `E` as a scalar expr. If it's non-null
/// and we wouldn't otherwise try to reference a pass_object_size parameter,
/// we'll call @llvm.objectsize on EmittedE, rather than emitting E.
llvm::Value *
CodeGenFunction::emitBuiltinObjectSize(const Expr *E, unsigned Type,
                                       llvm::IntegerType *ResType,
                                       llvm::Value *EmittedE) {
  // We need to reference an argument if the pointer is a parameter with the
  // pass_object_size attribute.
  if (auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) {
    auto *Param = dyn_cast<ParmVarDecl>(D->getDecl());
    auto *PS = D->getDecl()->getAttr<PassObjectSizeAttr>();
    if (Param != nullptr && PS != nullptr &&
        areBOSTypesCompatible(PS->getType(), Type)) {
      auto Iter = SizeArguments.find(Param);
      assert(Iter != SizeArguments.end());

      const ImplicitParamDecl *D = Iter->second;
      auto DIter = LocalDeclMap.find(D);
      assert(DIter != LocalDeclMap.end());

      return EmitLoadOfScalar(DIter->second, /*volatile=*/false,
                              getContext().getSizeType(), E->getBeginLoc());
    }
  }

  // LLVM can't handle Type=3 appropriately, and __builtin_object_size shouldn't
  // evaluate E for side-effects. In either case, we shouldn't lower to
  // @llvm.objectsize.
  if (Type == 3 || (!EmittedE && E->HasSideEffects(getContext())))
    return getDefaultBuiltinObjectSizeResult(Type, ResType);

  Value *Ptr = EmittedE ? EmittedE : EmitScalarExpr(E);
  assert(Ptr->getType()->isPointerTy() &&
         "Non-pointer passed to __builtin_object_size?");

  Value *F = CGM.getIntrinsic(Intrinsic::objectsize, {ResType, Ptr->getType()});

  // LLVM only supports 0 and 2, make sure that we pass along that as a boolean.
  Value *Min = Builder.getInt1((Type & 2) != 0);
  // For GCC compatibility, __builtin_object_size treat NULL as unknown size.
  Value *NullIsUnknown = Builder.getTrue();
  return Builder.CreateCall(F, {Ptr, Min, NullIsUnknown});
}

namespace {
/// A struct to generically describe a bit test intrinsic.
struct BitTest {
  enum ActionKind : uint8_t { TestOnly, Complement, Reset, Set };
  enum InterlockingKind : uint8_t {
    Unlocked,
    Sequential,
    Acquire,
    Release,
    NoFence
  };

  ActionKind Action;
  InterlockingKind Interlocking;
  bool Is64Bit;

  static BitTest decodeBitTestBuiltin(unsigned BuiltinID);
};
} // namespace

BitTest BitTest::decodeBitTestBuiltin(unsigned BuiltinID) {
  switch (BuiltinID) {
    // Main portable variants.
  case Builtin::BI_bittest:
    return {TestOnly, Unlocked, false};
  case Builtin::BI_bittestandcomplement:
    return {Complement, Unlocked, false};
  case Builtin::BI_bittestandreset:
    return {Reset, Unlocked, false};
  case Builtin::BI_bittestandset:
    return {Set, Unlocked, false};
  case Builtin::BI_interlockedbittestandreset:
    return {Reset, Sequential, false};
  case Builtin::BI_interlockedbittestandset:
    return {Set, Sequential, false};

    // X86-specific 64-bit variants.
  case Builtin::BI_bittest64:
    return {TestOnly, Unlocked, true};
  case Builtin::BI_bittestandcomplement64:
    return {Complement, Unlocked, true};
  case Builtin::BI_bittestandreset64:
    return {Reset, Unlocked, true};
  case Builtin::BI_bittestandset64:
    return {Set, Unlocked, true};
  case Builtin::BI_interlockedbittestandreset64:
    return {Reset, Sequential, true};
  case Builtin::BI_interlockedbittestandset64:
    return {Set, Sequential, true};

    // ARM/AArch64-specific ordering variants.
  case Builtin::BI_interlockedbittestandset_acq:
    return {Set, Acquire, false};
  case Builtin::BI_interlockedbittestandset_rel:
    return {Set, Release, false};
  case Builtin::BI_interlockedbittestandset_nf:
    return {Set, NoFence, false};
  case Builtin::BI_interlockedbittestandreset_acq:
    return {Reset, Acquire, false};
  case Builtin::BI_interlockedbittestandreset_rel:
    return {Reset, Release, false};
  case Builtin::BI_interlockedbittestandreset_nf:
    return {Reset, NoFence, false};
  }
  llvm_unreachable("expected only bittest intrinsics");
}

static char bitActionToX86BTCode(BitTest::ActionKind A) {
  switch (A) {
  case BitTest::TestOnly:   return '\0';
  case BitTest::Complement: return 'c';
  case BitTest::Reset:      return 'r';
  case BitTest::Set:        return 's';
  }
  llvm_unreachable("invalid action");
}

static llvm::Value *EmitX86BitTestIntrinsic(CodeGenFunction &CGF,
                                            BitTest BT,
                                            const CallExpr *E, Value *BitBase,
                                            Value *BitPos) {
  char Action = bitActionToX86BTCode(BT.Action);
  char SizeSuffix = BT.Is64Bit ? 'q' : 'l';

  // Build the assembly.
  SmallString<64> Asm;
  raw_svector_ostream AsmOS(Asm);
  if (BT.Interlocking != BitTest::Unlocked)
    AsmOS << "lock ";
  AsmOS << "bt";
  if (Action)
    AsmOS << Action;
  AsmOS << SizeSuffix << " $2, ($1)\n\tsetc ${0:b}";

  // Build the constraints. FIXME: We should support immediates when possible.
  std::string Constraints = "=r,r,r,~{cc},~{flags},~{fpsr}";
  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(),
      CGF.getContext().getTypeSize(E->getArg(1)->getType()));
  llvm::Type *IntPtrType = IntType->getPointerTo();
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGF.Int8Ty, {IntPtrType, IntType}, false);

  llvm::InlineAsm *IA =
      llvm::InlineAsm::get(FTy, Asm, Constraints, /*SideEffects=*/true);
  return CGF.Builder.CreateCall(IA, {BitBase, BitPos});
}

static llvm::AtomicOrdering
getBitTestAtomicOrdering(BitTest::InterlockingKind I) {
  switch (I) {
  case BitTest::Unlocked:   return llvm::AtomicOrdering::NotAtomic;
  case BitTest::Sequential: return llvm::AtomicOrdering::SequentiallyConsistent;
  case BitTest::Acquire:    return llvm::AtomicOrdering::Acquire;
  case BitTest::Release:    return llvm::AtomicOrdering::Release;
  case BitTest::NoFence:    return llvm::AtomicOrdering::Monotonic;
  }
  llvm_unreachable("invalid interlocking");
}

/// Emit a _bittest* intrinsic. These intrinsics take a pointer to an array of
/// bits and a bit position and read and optionally modify the bit at that
/// position. The position index can be arbitrarily large, i.e. it can be larger
/// than 31 or 63, so we need an indexed load in the general case.
static llvm::Value *EmitBitTestIntrinsic(CodeGenFunction &CGF,
                                         unsigned BuiltinID,
                                         const CallExpr *E) {
  Value *BitBase = CGF.EmitScalarExpr(E->getArg(0));
  Value *BitPos = CGF.EmitScalarExpr(E->getArg(1));

  BitTest BT = BitTest::decodeBitTestBuiltin(BuiltinID);

  // X86 has special BT, BTC, BTR, and BTS instructions that handle the array
  // indexing operation internally. Use them if possible.
  llvm::Triple::ArchType Arch = CGF.getTarget().getTriple().getArch();
  if (Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64)
    return EmitX86BitTestIntrinsic(CGF, BT, E, BitBase, BitPos);

  // Otherwise, use generic code to load one byte and test the bit. Use all but
  // the bottom three bits as the array index, and the bottom three bits to form
  // a mask.
  // Bit = BitBaseI8[BitPos >> 3] & (1 << (BitPos & 0x7)) != 0;
  Value *ByteIndex = CGF.Builder.CreateAShr(
      BitPos, llvm::ConstantInt::get(BitPos->getType(), 3), "bittest.byteidx");
  Value *BitBaseI8 = CGF.Builder.CreatePointerCast(BitBase, CGF.Int8PtrTy);
  Address ByteAddr(CGF.Builder.CreateInBoundsGEP(CGF.Int8Ty, BitBaseI8,
                                                 ByteIndex, "bittest.byteaddr"),
                   CharUnits::One());
  Value *PosLow =
      CGF.Builder.CreateAnd(CGF.Builder.CreateTrunc(BitPos, CGF.Int8Ty),
                            llvm::ConstantInt::get(CGF.Int8Ty, 0x7));

  // The updating instructions will need a mask.
  Value *Mask = nullptr;
  if (BT.Action != BitTest::TestOnly) {
    Mask = CGF.Builder.CreateShl(llvm::ConstantInt::get(CGF.Int8Ty, 1), PosLow,
                                 "bittest.mask");
  }

  // Check the action and ordering of the interlocked intrinsics.
  llvm::AtomicOrdering Ordering = getBitTestAtomicOrdering(BT.Interlocking);

  Value *OldByte = nullptr;
  if (Ordering != llvm::AtomicOrdering::NotAtomic) {
    // Emit a combined atomicrmw load/store operation for the interlocked
    // intrinsics.
    llvm::AtomicRMWInst::BinOp RMWOp = llvm::AtomicRMWInst::Or;
    if (BT.Action == BitTest::Reset) {
      Mask = CGF.Builder.CreateNot(Mask);
      RMWOp = llvm::AtomicRMWInst::And;
    }
    OldByte = CGF.Builder.CreateAtomicRMW(RMWOp, ByteAddr.getPointer(), Mask,
                                          Ordering);
  } else {
    // Emit a plain load for the non-interlocked intrinsics.
    OldByte = CGF.Builder.CreateLoad(ByteAddr, "bittest.byte");
    Value *NewByte = nullptr;
    switch (BT.Action) {
    case BitTest::TestOnly:
      // Don't store anything.
      break;
    case BitTest::Complement:
      NewByte = CGF.Builder.CreateXor(OldByte, Mask);
      break;
    case BitTest::Reset:
      NewByte = CGF.Builder.CreateAnd(OldByte, CGF.Builder.CreateNot(Mask));
      break;
    case BitTest::Set:
      NewByte = CGF.Builder.CreateOr(OldByte, Mask);
      break;
    }
    if (NewByte)
      CGF.Builder.CreateStore(NewByte, ByteAddr);
  }

  // However we loaded the old byte, either by plain load or atomicrmw, shift
  // the bit into the low position and mask it to 0 or 1.
  Value *ShiftedByte = CGF.Builder.CreateLShr(OldByte, PosLow, "bittest.shr");
  return CGF.Builder.CreateAnd(
      ShiftedByte, llvm::ConstantInt::get(CGF.Int8Ty, 1), "bittest.res");
}

namespace {
enum class MSVCSetJmpKind {
  _setjmpex,
  _setjmp3,
  _setjmp
};
}

/// MSVC handles setjmp a bit differently on different platforms. On every
/// architecture except 32-bit x86, the frame address is passed. On x86, extra
/// parameters can be passed as variadic arguments, but we always pass none.
static RValue EmitMSVCRTSetJmp(CodeGenFunction &CGF, MSVCSetJmpKind SJKind,
                               const CallExpr *E) {
  llvm::Value *Arg1 = nullptr;
  llvm::Type *Arg1Ty = nullptr;
  StringRef Name;
  bool IsVarArg = false;
  if (SJKind == MSVCSetJmpKind::_setjmp3) {
    Name = "_setjmp3";
    Arg1Ty = CGF.Int32Ty;
    Arg1 = llvm::ConstantInt::get(CGF.IntTy, 0);
    IsVarArg = true;
  } else {
    Name = SJKind == MSVCSetJmpKind::_setjmp ? "_setjmp" : "_setjmpex";
    Arg1Ty = CGF.Int8PtrTy;
    if (CGF.getTarget().getTriple().getArch() == llvm::Triple::aarch64) {
      Arg1 = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(Intrinsic::sponentry));
    } else
      Arg1 = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(Intrinsic::frameaddress),
                                    llvm::ConstantInt::get(CGF.Int32Ty, 0));
  }

  // Mark the call site and declaration with ReturnsTwice.
  llvm::Type *ArgTypes[2] = {CGF.Int8PtrTy, Arg1Ty};
  llvm::AttributeList ReturnsTwiceAttr = llvm::AttributeList::get(
      CGF.getLLVMContext(), llvm::AttributeList::FunctionIndex,
      llvm::Attribute::ReturnsTwice);
  llvm::Constant *SetJmpFn = CGF.CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGF.IntTy, ArgTypes, IsVarArg), Name,
      ReturnsTwiceAttr, /*Local=*/true);

  llvm::Value *Buf = CGF.Builder.CreateBitOrPointerCast(
      CGF.EmitScalarExpr(E->getArg(0)), CGF.Int8PtrTy);
  llvm::Value *Args[] = {Buf, Arg1};
  llvm::CallSite CS = CGF.EmitRuntimeCallOrInvoke(SetJmpFn, Args);
  CS.setAttributes(ReturnsTwiceAttr);
  return RValue::get(CS.getInstruction());
}

// Many of MSVC builtins are on x64, ARM and AArch64; to avoid repeating code,
// we handle them here.
enum class CodeGenFunction::MSVCIntrin {
  _BitScanForward,
  _BitScanReverse,
  _InterlockedAnd,
  _InterlockedDecrement,
  _InterlockedExchange,
  _InterlockedExchangeAdd,
  _InterlockedExchangeSub,
  _InterlockedIncrement,
  _InterlockedOr,
  _InterlockedXor,
  _InterlockedExchangeAdd_acq,
  _InterlockedExchangeAdd_rel,
  _InterlockedExchangeAdd_nf,
  _InterlockedExchange_acq,
  _InterlockedExchange_rel,
  _InterlockedExchange_nf,
  _InterlockedCompareExchange_acq,
  _InterlockedCompareExchange_rel,
  _InterlockedCompareExchange_nf,
  _InterlockedOr_acq,
  _InterlockedOr_rel,
  _InterlockedOr_nf,
  _InterlockedXor_acq,
  _InterlockedXor_rel,
  _InterlockedXor_nf,
  _InterlockedAnd_acq,
  _InterlockedAnd_rel,
  _InterlockedAnd_nf,
  _InterlockedIncrement_acq,
  _InterlockedIncrement_rel,
  _InterlockedIncrement_nf,
  _InterlockedDecrement_acq,
  _InterlockedDecrement_rel,
  _InterlockedDecrement_nf,
  __fastfail,
};

Value *CodeGenFunction::EmitMSVCBuiltinExpr(MSVCIntrin BuiltinID,
                                            const CallExpr *E) {
  switch (BuiltinID) {
  case MSVCIntrin::_BitScanForward:
  case MSVCIntrin::_BitScanReverse: {
    Value *ArgValue = EmitScalarExpr(E->getArg(1));

    llvm::Type *ArgType = ArgValue->getType();
    llvm::Type *IndexType =
      EmitScalarExpr(E->getArg(0))->getType()->getPointerElementType();
    llvm::Type *ResultType = ConvertType(E->getType());

    Value *ArgZero = llvm::Constant::getNullValue(ArgType);
    Value *ResZero = llvm::Constant::getNullValue(ResultType);
    Value *ResOne = llvm::ConstantInt::get(ResultType, 1);

    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("bitscan_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result = Builder.CreatePHI(ResultType, 2, "bitscan_result");

    Builder.SetInsertPoint(Begin);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, ArgZero);
    BasicBlock *NotZero = createBasicBlock("bitscan_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ResZero, Begin);

    Builder.SetInsertPoint(NotZero);
    Address IndexAddress = EmitPointerWithAlignment(E->getArg(0));

    if (BuiltinID == MSVCIntrin::_BitScanForward) {
      Value *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Builder.CreateStore(ZeroCount, IndexAddress, false);
    } else {
      unsigned ArgWidth = cast<llvm::IntegerType>(ArgType)->getBitWidth();
      Value *ArgTypeLastIndex = llvm::ConstantInt::get(IndexType, ArgWidth - 1);

      Value *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Value *Index = Builder.CreateNSWSub(ArgTypeLastIndex, ZeroCount);
      Builder.CreateStore(Index, IndexAddress, false);
    }
    Builder.CreateBr(End);
    Result->addIncoming(ResOne, NotZero);

    Builder.SetInsertPoint(End);
    return Result;
  }
  case MSVCIntrin::_InterlockedAnd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E);
  case MSVCIntrin::_InterlockedExchange:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E);
  case MSVCIntrin::_InterlockedExchangeAdd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E);
  case MSVCIntrin::_InterlockedExchangeSub:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Sub, E);
  case MSVCIntrin::_InterlockedOr:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E);
  case MSVCIntrin::_InterlockedXor:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E);
  case MSVCIntrin::_InterlockedExchangeAdd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchangeAdd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchangeAdd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedExchange_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchange_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchange_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedCompareExchange_acq:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange_rel:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedCompareExchange_nf:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedOr_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedOr_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedOr_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedXor_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedXor_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedXor_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedAnd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedAnd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedAnd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedIncrement_acq:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedIncrement_rel:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedIncrement_nf:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedDecrement_acq:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedDecrement_rel:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedDecrement_nf:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Monotonic);

  case MSVCIntrin::_InterlockedDecrement:
    return EmitAtomicDecrementValue(*this, E);
  case MSVCIntrin::_InterlockedIncrement:
    return EmitAtomicIncrementValue(*this, E);

  case MSVCIntrin::__fastfail: {
    // Request immediate process termination from the kernel. The instruction
    // sequences to do this are documented on MSDN:
    // https://msdn.microsoft.com/en-us/library/dn774154.aspx
    llvm::Triple::ArchType ISA = getTarget().getTriple().getArch();
    StringRef Asm, Constraints;
    switch (ISA) {
    default:
      ErrorUnsupported(E, "__fastfail call for this architecture");
      break;
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      Asm = "int $$0x29";
      Constraints = "{cx}";
      break;
    case llvm::Triple::thumb:
      Asm = "udf #251";
      Constraints = "{r0}";
      break;
    }
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, {Int32Ty}, false);
    llvm::InlineAsm *IA =
        llvm::InlineAsm::get(FTy, Asm, Constraints, /*SideEffects=*/true);
    llvm::AttributeList NoReturnAttr = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex,
        llvm::Attribute::NoReturn);
    CallSite CS = Builder.CreateCall(IA, EmitScalarExpr(E->getArg(0)));
    CS.setAttributes(NoReturnAttr);
    return CS.getInstruction();
  }
  }
  llvm_unreachable("Incorrect MSVC intrinsic!");
}

namespace {
// ARC cleanup for __builtin_os_log_format
struct CallObjCArcUse final : EHScopeStack::Cleanup {
  CallObjCArcUse(llvm::Value *object) : object(object) {}
  llvm::Value *object;

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    CGF.EmitARCIntrinsicUse(object);
  }
};
}

Value *CodeGenFunction::EmitCheckedArgForBuiltin(const Expr *E,
                                                 BuiltinCheckKind Kind) {
  assert((Kind == BCK_CLZPassedZero || Kind == BCK_CTZPassedZero)
          && "Unsupported builtin check kind");

  Value *ArgValue = EmitScalarExpr(E);
  if (!SanOpts.has(SanitizerKind::Builtin) || !getTarget().isCLZForZeroUndef())
    return ArgValue;

  SanitizerScope SanScope(this);
  Value *Cond = Builder.CreateICmpNE(
      ArgValue, llvm::Constant::getNullValue(ArgValue->getType()));
  EmitCheck(std::make_pair(Cond, SanitizerKind::Builtin),
            SanitizerHandler::InvalidBuiltin,
            {EmitCheckSourceLocation(E->getExprLoc()),
             llvm::ConstantInt::get(Builder.getInt8Ty(), Kind)},
            None);
  return ArgValue;
}

/// Get the argument type for arguments to os_log_helper.
static CanQualType getOSLogArgType(ASTContext &C, int Size) {
  QualType UnsignedTy = C.getIntTypeForBitwidth(Size * 8, /*Signed=*/false);
  return C.getCanonicalType(UnsignedTy);
}

llvm::Function *CodeGenFunction::generateBuiltinOSLogHelperFunction(
    const analyze_os_log::OSLogBufferLayout &Layout,
    CharUnits BufferAlignment) {
  ASTContext &Ctx = getContext();

  llvm::SmallString<64> Name;
  {
    raw_svector_ostream OS(Name);
    OS << "__os_log_helper";
    OS << "_" << BufferAlignment.getQuantity();
    OS << "_" << int(Layout.getSummaryByte());
    OS << "_" << int(Layout.getNumArgsByte());
    for (const auto &Item : Layout.Items)
      OS << "_" << int(Item.getSizeByte()) << "_"
         << int(Item.getDescriptorByte());
  }

  if (llvm::Function *F = CGM.getModule().getFunction(Name))
    return F;

  llvm::SmallVector<QualType, 4> ArgTys;
  llvm::SmallVector<ImplicitParamDecl, 4> Params;
  Params.emplace_back(Ctx, nullptr, SourceLocation(), &Ctx.Idents.get("buffer"),
                      Ctx.VoidPtrTy, ImplicitParamDecl::Other);
  ArgTys.emplace_back(Ctx.VoidPtrTy);

  for (unsigned int I = 0, E = Layout.Items.size(); I < E; ++I) {
    char Size = Layout.Items[I].getSizeByte();
    if (!Size)
      continue;

    QualType ArgTy = getOSLogArgType(Ctx, Size);
    Params.emplace_back(
        Ctx, nullptr, SourceLocation(),
        &Ctx.Idents.get(std::string("arg") + llvm::to_string(I)), ArgTy,
        ImplicitParamDecl::Other);
    ArgTys.emplace_back(ArgTy);
  }

  FunctionArgList Args;
  for (auto &P : Params)
    Args.push_back(&P);

  QualType ReturnTy = Ctx.VoidTy;
  QualType FuncionTy = Ctx.getFunctionType(ReturnTy, ArgTys, {});

  // The helper function has linkonce_odr linkage to enable the linker to merge
  // identical functions. To ensure the merging always happens, 'noinline' is
  // attached to the function when compiling with -Oz.
  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, Args);
  llvm::FunctionType *FuncTy = CGM.getTypes().GetFunctionType(FI);
  llvm::Function *Fn = llvm::Function::Create(
      FuncTy, llvm::GlobalValue::LinkOnceODRLinkage, Name, &CGM.getModule());
  Fn->setVisibility(llvm::GlobalValue::HiddenVisibility);
  CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, Fn);
  CGM.SetLLVMFunctionAttributesForDefinition(nullptr, Fn);

  // Attach 'noinline' at -Oz.
  if (CGM.getCodeGenOpts().OptimizeSize == 2)
    Fn->addFnAttr(llvm::Attribute::NoInline);

  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  IdentifierInfo *II = &Ctx.Idents.get(Name);
  FunctionDecl *FD = FunctionDecl::Create(
      Ctx, Ctx.getTranslationUnitDecl(), SourceLocation(), SourceLocation(), II,
      FuncionTy, nullptr, SC_PrivateExtern, false, false);

  StartFunction(FD, ReturnTy, Fn, FI, Args);

  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  CharUnits Offset;
  Address BufAddr(Builder.CreateLoad(GetAddrOfLocalVar(&Params[0]), "buf"),
                  BufferAlignment);
  Builder.CreateStore(Builder.getInt8(Layout.getSummaryByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "summary"));
  Builder.CreateStore(Builder.getInt8(Layout.getNumArgsByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "numArgs"));

  unsigned I = 1;
  for (const auto &Item : Layout.Items) {
    Builder.CreateStore(
        Builder.getInt8(Item.getDescriptorByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argDescriptor"));
    Builder.CreateStore(
        Builder.getInt8(Item.getSizeByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argSize"));

    CharUnits Size = Item.size();
    if (!Size.getQuantity())
      continue;

    Address Arg = GetAddrOfLocalVar(&Params[I]);
    Address Addr = Builder.CreateConstByteGEP(BufAddr, Offset, "argData");
    Addr = Builder.CreateBitCast(Addr, Arg.getPointer()->getType(),
                                 "argDataCast");
    Builder.CreateStore(Builder.CreateLoad(Arg), Addr);
    Offset += Size;
    ++I;
  }

  FinishFunction();

  return Fn;
}

RValue CodeGenFunction::emitBuiltinOSLogFormat(const CallExpr &E) {
  assert(E.getNumArgs() >= 2 &&
         "__builtin_os_log_format takes at least 2 arguments");
  ASTContext &Ctx = getContext();
  analyze_os_log::OSLogBufferLayout Layout;
  analyze_os_log::computeOSLogBufferLayout(Ctx, &E, Layout);
  Address BufAddr = EmitPointerWithAlignment(E.getArg(0));
  llvm::SmallVector<llvm::Value *, 4> RetainableOperands;

  // Ignore argument 1, the format string. It is not currently used.
  CallArgList Args;
  Args.add(RValue::get(BufAddr.getPointer()), Ctx.VoidPtrTy);

  for (const auto &Item : Layout.Items) {
    int Size = Item.getSizeByte();
    if (!Size)
      continue;

    llvm::Value *ArgVal;

    if (Item.getKind() == analyze_os_log::OSLogBufferItem::MaskKind) {
      uint64_t Val = 0;
      for (unsigned I = 0, E = Item.getMaskType().size(); I < E; ++I)
        Val |= ((uint64_t)Item.getMaskType()[I]) << I * 8;
      ArgVal = llvm::Constant::getIntegerValue(Int64Ty, llvm::APInt(64, Val));
    } else if (const Expr *TheExpr = Item.getExpr()) {
      ArgVal = EmitScalarExpr(TheExpr, /*Ignore*/ false);

      // Check if this is a retainable type.
      if (TheExpr->getType()->isObjCRetainableType()) {
        assert(getEvaluationKind(TheExpr->getType()) == TEK_Scalar &&
               "Only scalar can be a ObjC retainable type");
        // Check if the object is constant, if not, save it in
        // RetainableOperands.
        if (!isa<Constant>(ArgVal))
          RetainableOperands.push_back(ArgVal);
      }
    } else {
      ArgVal = Builder.getInt32(Item.getConstValue().getQuantity());
    }

    unsigned ArgValSize =
        CGM.getDataLayout().getTypeSizeInBits(ArgVal->getType());
    llvm::IntegerType *IntTy = llvm::Type::getIntNTy(getLLVMContext(),
                                                     ArgValSize);
    ArgVal = Builder.CreateBitOrPointerCast(ArgVal, IntTy);
    CanQualType ArgTy = getOSLogArgType(Ctx, Size);
    // If ArgVal has type x86_fp80, zero-extend ArgVal.
    ArgVal = Builder.CreateZExtOrBitCast(ArgVal, ConvertType(ArgTy));
    Args.add(RValue::get(ArgVal), ArgTy);
  }

  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionCall(Ctx.VoidTy, Args);
  llvm::Function *F = CodeGenFunction(CGM).generateBuiltinOSLogHelperFunction(
      Layout, BufAddr.getAlignment());
  EmitCall(FI, CGCallee::forDirect(F), ReturnValueSlot(), Args);

  // Push a clang.arc.use cleanup for each object in RetainableOperands. The
  // cleanup will cause the use to appear after the final log call, keeping
  // the object valid while it’s held in the log buffer.  Note that if there’s
  // a release cleanup on the object, it will already be active; since
  // cleanups are emitted in reverse order, the use will occur before the
  // object is released.
  if (!RetainableOperands.empty() && getLangOpts().ObjCAutoRefCount &&
      CGM.getCodeGenOpts().OptimizationLevel != 0)
    for (llvm::Value *Object : RetainableOperands)
      pushFullExprCleanup<CallObjCArcUse>(getARCCleanupKind(), Object);

  return RValue::get(BufAddr.getPointer());
}

/// Determine if a binop is a checked mixed-sign multiply we can specialize.
static bool isSpecialMixedSignMultiply(unsigned BuiltinID,
                                       WidthAndSignedness Op1Info,
                                       WidthAndSignedness Op2Info,
                                       WidthAndSignedness ResultInfo) {
  return BuiltinID == Builtin::BI__builtin_mul_overflow &&
         std::max(Op1Info.Width, Op2Info.Width) >= ResultInfo.Width &&
         Op1Info.Signed != Op2Info.Signed;
}

/// Emit a checked mixed-sign multiply. This is a cheaper specialization of
/// the generic checked-binop irgen.
static RValue
EmitCheckedMixedSignMultiply(CodeGenFunction &CGF, const clang::Expr *Op1,
                             WidthAndSignedness Op1Info, const clang::Expr *Op2,
                             WidthAndSignedness Op2Info,
                             const clang::Expr *ResultArg, QualType ResultQTy,
                             WidthAndSignedness ResultInfo) {
  assert(isSpecialMixedSignMultiply(Builtin::BI__builtin_mul_overflow, Op1Info,
                                    Op2Info, ResultInfo) &&
         "Not a mixed-sign multipliction we can specialize");

  // Emit the signed and unsigned operands.
  const clang::Expr *SignedOp = Op1Info.Signed ? Op1 : Op2;
  const clang::Expr *UnsignedOp = Op1Info.Signed ? Op2 : Op1;
  llvm::Value *Signed = CGF.EmitScalarExpr(SignedOp);
  llvm::Value *Unsigned = CGF.EmitScalarExpr(UnsignedOp);
  unsigned SignedOpWidth = Op1Info.Signed ? Op1Info.Width : Op2Info.Width;
  unsigned UnsignedOpWidth = Op1Info.Signed ? Op2Info.Width : Op1Info.Width;

  // One of the operands may be smaller than the other. If so, [s|z]ext it.
  if (SignedOpWidth < UnsignedOpWidth)
    Signed = CGF.Builder.CreateSExt(Signed, Unsigned->getType(), "op.sext");
  if (UnsignedOpWidth < SignedOpWidth)
    Unsigned = CGF.Builder.CreateZExt(Unsigned, Signed->getType(), "op.zext");

  llvm::Type *OpTy = Signed->getType();
  llvm::Value *Zero = llvm::Constant::getNullValue(OpTy);
  Address ResultPtr = CGF.EmitPointerWithAlignment(ResultArg);
  llvm::Type *ResTy = ResultPtr.getElementType();
  unsigned OpWidth = std::max(Op1Info.Width, Op2Info.Width);

  // Take the absolute value of the signed operand.
  llvm::Value *IsNegative = CGF.Builder.CreateICmpSLT(Signed, Zero);
  llvm::Value *AbsOfNegative = CGF.Builder.CreateSub(Zero, Signed);
  llvm::Value *AbsSigned =
      CGF.Builder.CreateSelect(IsNegative, AbsOfNegative, Signed);

  // Perform a checked unsigned multiplication.
  llvm::Value *UnsignedOverflow;
  llvm::Value *UnsignedResult =
      EmitOverflowIntrinsic(CGF, llvm::Intrinsic::umul_with_overflow, AbsSigned,
                            Unsigned, UnsignedOverflow);

  llvm::Value *Overflow, *Result;
  if (ResultInfo.Signed) {
    // Signed overflow occurs if the result is greater than INT_MAX or lesser
    // than INT_MIN, i.e when |Result| > (INT_MAX + IsNegative).
    auto IntMax =
        llvm::APInt::getSignedMaxValue(ResultInfo.Width).zextOrSelf(OpWidth);
    llvm::Value *MaxResult =
        CGF.Builder.CreateAdd(llvm::ConstantInt::get(OpTy, IntMax),
                              CGF.Builder.CreateZExt(IsNegative, OpTy));
    llvm::Value *SignedOverflow =
        CGF.Builder.CreateICmpUGT(UnsignedResult, MaxResult);
    Overflow = CGF.Builder.CreateOr(UnsignedOverflow, SignedOverflow);

    // Prepare the signed result (possibly by negating it).
    llvm::Value *NegativeResult = CGF.Builder.CreateNeg(UnsignedResult);
    llvm::Value *SignedResult =
        CGF.Builder.CreateSelect(IsNegative, NegativeResult, UnsignedResult);
    Result = CGF.Builder.CreateTrunc(SignedResult, ResTy);
  } else {
    // Unsigned overflow occurs if the result is < 0 or greater than UINT_MAX.
    llvm::Value *Underflow = CGF.Builder.CreateAnd(
        IsNegative, CGF.Builder.CreateIsNotNull(UnsignedResult));
    Overflow = CGF.Builder.CreateOr(UnsignedOverflow, Underflow);
    if (ResultInfo.Width < OpWidth) {
      auto IntMax =
          llvm::APInt::getMaxValue(ResultInfo.Width).zext(OpWidth);
      llvm::Value *TruncOverflow = CGF.Builder.CreateICmpUGT(
          UnsignedResult, llvm::ConstantInt::get(OpTy, IntMax));
      Overflow = CGF.Builder.CreateOr(Overflow, TruncOverflow);
    }

    // Negate the product if it would be negative in infinite precision.
    Result = CGF.Builder.CreateSelect(
        IsNegative, CGF.Builder.CreateNeg(UnsignedResult), UnsignedResult);

    Result = CGF.Builder.CreateTrunc(Result, ResTy);
  }
  assert(Overflow && Result && "Missing overflow or result");

  bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
  CGF.Builder.CreateStore(CGF.EmitToMemory(Result, ResultQTy), ResultPtr,
                          isVolatile);
  return RValue::get(Overflow);
}

static llvm::Value *dumpRecord(CodeGenFunction &CGF, QualType RType,
                               Value *&RecordPtr, CharUnits Align, Value *Func,
                               int Lvl) {
  const auto *RT = RType->getAs<RecordType>();
  ASTContext &Context = CGF.getContext();
  RecordDecl *RD = RT->getDecl()->getDefinition();
  ASTContext &Ctx = RD->getASTContext();
  const ASTRecordLayout &RL = Ctx.getASTRecordLayout(RD);
  std::string Pad = std::string(Lvl * 4, ' ');

  Value *GString =
      CGF.Builder.CreateGlobalStringPtr(RType.getAsString() + " {\n");
  Value *Res = CGF.Builder.CreateCall(Func, {GString});

  static llvm::DenseMap<QualType, const char *> Types;
  if (Types.empty()) {
    Types[Context.CharTy] = "%c";
    Types[Context.BoolTy] = "%d";
    Types[Context.SignedCharTy] = "%hhd";
    Types[Context.UnsignedCharTy] = "%hhu";
    Types[Context.IntTy] = "%d";
    Types[Context.UnsignedIntTy] = "%u";
    Types[Context.LongTy] = "%ld";
    Types[Context.UnsignedLongTy] = "%lu";
    Types[Context.LongLongTy] = "%lld";
    Types[Context.UnsignedLongLongTy] = "%llu";
    Types[Context.ShortTy] = "%hd";
    Types[Context.UnsignedShortTy] = "%hu";
    Types[Context.VoidPtrTy] = "%p";
    Types[Context.FloatTy] = "%f";
    Types[Context.DoubleTy] = "%f";
    Types[Context.LongDoubleTy] = "%Lf";
    Types[Context.getPointerType(Context.CharTy)] = "%s";
    Types[Context.getPointerType(Context.getConstType(Context.CharTy))] = "%s";
  }

  for (const auto *FD : RD->fields()) {
    uint64_t Off = RL.getFieldOffset(FD->getFieldIndex());
    Off = Ctx.toCharUnitsFromBits(Off).getQuantity();

    Value *FieldPtr = RecordPtr;
    if (RD->isUnion())
      FieldPtr = CGF.Builder.CreatePointerCast(
          FieldPtr, CGF.ConvertType(Context.getPointerType(FD->getType())));
    else
      FieldPtr = CGF.Builder.CreateStructGEP(CGF.ConvertType(RType), FieldPtr,
                                             FD->getFieldIndex());

    GString = CGF.Builder.CreateGlobalStringPtr(
        llvm::Twine(Pad)
            .concat(FD->getType().getAsString())
            .concat(llvm::Twine(' '))
            .concat(FD->getNameAsString())
            .concat(" : ")
            .str());
    Value *TmpRes = CGF.Builder.CreateCall(Func, {GString});
    Res = CGF.Builder.CreateAdd(Res, TmpRes);

    QualType CanonicalType =
        FD->getType().getUnqualifiedType().getCanonicalType();

    // We check whether we are in a recursive type
    if (CanonicalType->isRecordType()) {
      Value *TmpRes =
          dumpRecord(CGF, CanonicalType, FieldPtr, Align, Func, Lvl + 1);
      Res = CGF.Builder.CreateAdd(TmpRes, Res);
      continue;
    }

    // We try to determine the best format to print the current field
    llvm::Twine Format = Types.find(CanonicalType) == Types.end()
                             ? Types[Context.VoidPtrTy]
                             : Types[CanonicalType];

    Address FieldAddress = Address(FieldPtr, Align);
    FieldPtr = CGF.Builder.CreateLoad(FieldAddress);

    // FIXME Need to handle bitfield here
    GString = CGF.Builder.CreateGlobalStringPtr(
        Format.concat(llvm::Twine('\n')).str());
    TmpRes = CGF.Builder.CreateCall(Func, {GString, FieldPtr});
    Res = CGF.Builder.CreateAdd(Res, TmpRes);
  }

  GString = CGF.Builder.CreateGlobalStringPtr(Pad + "}\n");
  Value *TmpRes = CGF.Builder.CreateCall(Func, {GString});
  Res = CGF.Builder.CreateAdd(Res, TmpRes);
  return Res;
}

static bool
TypeRequiresBuiltinLaunderImp(const ASTContext &Ctx, QualType Ty,
                              llvm::SmallPtrSetImpl<const Decl *> &Seen) {
  if (const auto *Arr = Ctx.getAsArrayType(Ty))
    Ty = Ctx.getBaseElementType(Arr);

  const auto *Record = Ty->getAsCXXRecordDecl();
  if (!Record)
    return false;

  // We've already checked this type, or are in the process of checking it.
  if (!Seen.insert(Record).second)
    return false;

  assert(Record->hasDefinition() &&
         "Incomplete types should already be diagnosed");

  if (Record->isDynamicClass())
    return true;

  for (FieldDecl *F : Record->fields()) {
    if (TypeRequiresBuiltinLaunderImp(Ctx, F->getType(), Seen))
      return true;
  }
  return false;
}

/// Determine if the specified type requires laundering by checking if it is a
/// dynamic class type or contains a subobject which is a dynamic class type.
static bool TypeRequiresBuiltinLaunder(CodeGenModule &CGM, QualType Ty) {
  if (!CGM.getCodeGenOpts().StrictVTablePointers)
    return false;
  llvm::SmallPtrSet<const Decl *, 16> Seen;
  return TypeRequiresBuiltinLaunderImp(CGM.getContext(), Ty, Seen);
}

RValue CodeGenFunction::emitRotate(const CallExpr *E, bool IsRotateRight) {
  llvm::Value *Src = EmitScalarExpr(E->getArg(0));
  llvm::Value *ShiftAmt = EmitScalarExpr(E->getArg(1));

  // The builtin's shift arg may have a different type than the source arg and
  // result, but the LLVM intrinsic uses the same type for all values.
  llvm::Type *Ty = Src->getType();
  ShiftAmt = Builder.CreateIntCast(ShiftAmt, Ty, false);

  // Rotate is a special case of LLVM funnel shift - 1st 2 args are the same.
  unsigned IID = IsRotateRight ? Intrinsic::fshr : Intrinsic::fshl;
  Value *F = CGM.getIntrinsic(IID, Ty);
  return RValue::get(Builder.CreateCall(F, { Src, Src, ShiftAmt }));
}

RValue CodeGenFunction::EmitBuiltinExpr(const GlobalDecl GD, unsigned BuiltinID,
                                        const CallExpr *E,
                                        ReturnValueSlot ReturnValue) {
  const FunctionDecl *FD = GD.getDecl()->getAsFunction();
  // See if we can constant fold this builtin.  If so, don't emit it at all.
  Expr::EvalResult Result;
  if (E->EvaluateAsRValue(Result, CGM.getContext()) &&
      !Result.hasSideEffects()) {
    if (Result.Val.isInt())
      return RValue::get(llvm::ConstantInt::get(getLLVMContext(),
                                                Result.Val.getInt()));
    if (Result.Val.isFloat())
      return RValue::get(llvm::ConstantFP::get(getLLVMContext(),
                                               Result.Val.getFloat()));
  }

  // There are LLVM math intrinsics/instructions corresponding to math library
  // functions except the LLVM op will never set errno while the math library
  // might. Also, math builtins have the same semantics as their math library
  // twins. Thus, we can transform math library and builtin calls to their
  // LLVM counterparts if the call is marked 'const' (known to never set errno).
  if (FD->hasAttr<ConstAttr>()) {
    switch (BuiltinID) {
    case Builtin::BIceil:
    case Builtin::BIceilf:
    case Builtin::BIceill:
    case Builtin::BI__builtin_ceil:
    case Builtin::BI__builtin_ceilf:
    case Builtin::BI__builtin_ceill:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::ceil));

    case Builtin::BIcopysign:
    case Builtin::BIcopysignf:
    case Builtin::BIcopysignl:
    case Builtin::BI__builtin_copysign:
    case Builtin::BI__builtin_copysignf:
    case Builtin::BI__builtin_copysignl:
    case Builtin::BI__builtin_copysignf128:
      return RValue::get(emitBinaryBuiltin(*this, E, Intrinsic::copysign));

    case Builtin::BIcos:
    case Builtin::BIcosf:
    case Builtin::BIcosl:
    case Builtin::BI__builtin_cos:
    case Builtin::BI__builtin_cosf:
    case Builtin::BI__builtin_cosl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::cos));

    case Builtin::BIexp:
    case Builtin::BIexpf:
    case Builtin::BIexpl:
    case Builtin::BI__builtin_exp:
    case Builtin::BI__builtin_expf:
    case Builtin::BI__builtin_expl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::exp));

    case Builtin::BIexp2:
    case Builtin::BIexp2f:
    case Builtin::BIexp2l:
    case Builtin::BI__builtin_exp2:
    case Builtin::BI__builtin_exp2f:
    case Builtin::BI__builtin_exp2l:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::exp2));

    case Builtin::BIfabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabsl:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_fabsf128:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::fabs));

    case Builtin::BIfloor:
    case Builtin::BIfloorf:
    case Builtin::BIfloorl:
    case Builtin::BI__builtin_floor:
    case Builtin::BI__builtin_floorf:
    case Builtin::BI__builtin_floorl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::floor));

    case Builtin::BIfma:
    case Builtin::BIfmaf:
    case Builtin::BIfmal:
    case Builtin::BI__builtin_fma:
    case Builtin::BI__builtin_fmaf:
    case Builtin::BI__builtin_fmal:
      return RValue::get(emitTernaryBuiltin(*this, E, Intrinsic::fma));

    case Builtin::BIfmax:
    case Builtin::BIfmaxf:
    case Builtin::BIfmaxl:
    case Builtin::BI__builtin_fmax:
    case Builtin::BI__builtin_fmaxf:
    case Builtin::BI__builtin_fmaxl:
      return RValue::get(emitBinaryBuiltin(*this, E, Intrinsic::maxnum));

    case Builtin::BIfmin:
    case Builtin::BIfminf:
    case Builtin::BIfminl:
    case Builtin::BI__builtin_fmin:
    case Builtin::BI__builtin_fminf:
    case Builtin::BI__builtin_fminl:
      return RValue::get(emitBinaryBuiltin(*this, E, Intrinsic::minnum));

    // fmod() is a special-case. It maps to the frem instruction rather than an
    // LLVM intrinsic.
    case Builtin::BIfmod:
    case Builtin::BIfmodf:
    case Builtin::BIfmodl:
    case Builtin::BI__builtin_fmod:
    case Builtin::BI__builtin_fmodf:
    case Builtin::BI__builtin_fmodl: {
      Value *Arg1 = EmitScalarExpr(E->getArg(0));
      Value *Arg2 = EmitScalarExpr(E->getArg(1));
      return RValue::get(Builder.CreateFRem(Arg1, Arg2, "fmod"));
    }

    case Builtin::BIlog:
    case Builtin::BIlogf:
    case Builtin::BIlogl:
    case Builtin::BI__builtin_log:
    case Builtin::BI__builtin_logf:
    case Builtin::BI__builtin_logl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::log));

    case Builtin::BIlog10:
    case Builtin::BIlog10f:
    case Builtin::BIlog10l:
    case Builtin::BI__builtin_log10:
    case Builtin::BI__builtin_log10f:
    case Builtin::BI__builtin_log10l:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::log10));

    case Builtin::BIlog2:
    case Builtin::BIlog2f:
    case Builtin::BIlog2l:
    case Builtin::BI__builtin_log2:
    case Builtin::BI__builtin_log2f:
    case Builtin::BI__builtin_log2l:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::log2));

    case Builtin::BInearbyint:
    case Builtin::BInearbyintf:
    case Builtin::BInearbyintl:
    case Builtin::BI__builtin_nearbyint:
    case Builtin::BI__builtin_nearbyintf:
    case Builtin::BI__builtin_nearbyintl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::nearbyint));

    case Builtin::BIpow:
    case Builtin::BIpowf:
    case Builtin::BIpowl:
    case Builtin::BI__builtin_pow:
    case Builtin::BI__builtin_powf:
    case Builtin::BI__builtin_powl:
      return RValue::get(emitBinaryBuiltin(*this, E, Intrinsic::pow));

    case Builtin::BIrint:
    case Builtin::BIrintf:
    case Builtin::BIrintl:
    case Builtin::BI__builtin_rint:
    case Builtin::BI__builtin_rintf:
    case Builtin::BI__builtin_rintl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::rint));

    case Builtin::BIround:
    case Builtin::BIroundf:
    case Builtin::BIroundl:
    case Builtin::BI__builtin_round:
    case Builtin::BI__builtin_roundf:
    case Builtin::BI__builtin_roundl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::round));

    case Builtin::BIsin:
    case Builtin::BIsinf:
    case Builtin::BIsinl:
    case Builtin::BI__builtin_sin:
    case Builtin::BI__builtin_sinf:
    case Builtin::BI__builtin_sinl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::sin));

    case Builtin::BIsqrt:
    case Builtin::BIsqrtf:
    case Builtin::BIsqrtl:
    case Builtin::BI__builtin_sqrt:
    case Builtin::BI__builtin_sqrtf:
    case Builtin::BI__builtin_sqrtl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::sqrt));

    case Builtin::BItrunc:
    case Builtin::BItruncf:
    case Builtin::BItruncl:
    case Builtin::BI__builtin_trunc:
    case Builtin::BI__builtin_truncf:
    case Builtin::BI__builtin_truncl:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::trunc));

    default:
      break;
    }
  }

  switch (BuiltinID) {
  default: break;
  case Builtin::BI__builtin___CFStringMakeConstantString:
  case Builtin::BI__builtin___NSStringMakeConstantString:
    return RValue::get(ConstantEmitter(*this).emitAbstract(E, E->getType()));
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__va_start:
  case Builtin::BI__builtin_va_end:
    return RValue::get(
        EmitVAStartEnd(BuiltinID == Builtin::BI__va_start
                           ? EmitScalarExpr(E->getArg(0))
                           : EmitVAListRef(E->getArg(0)).getPointer(),
                       BuiltinID != Builtin::BI__builtin_va_end));
  case Builtin::BI__builtin_va_copy: {
    Value *DstPtr = EmitVAListRef(E->getArg(0)).getPointer();
    Value *SrcPtr = EmitVAListRef(E->getArg(1)).getPointer();

    llvm::Type *Type = Int8PtrTy;

    DstPtr = Builder.CreateBitCast(DstPtr, Type);
    SrcPtr = Builder.CreateBitCast(SrcPtr, Type);
    return RValue::get(Builder.CreateCall(CGM.getIntrinsic(Intrinsic::vacopy),
                                          {DstPtr, SrcPtr}));
  }
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs: {
    // X < 0 ? -X : X
    // The negation has 'nsw' because abs of INT_MIN is undefined.
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    Value *NegOp = Builder.CreateNSWNeg(ArgValue, "neg");
    Constant *Zero = llvm::Constant::getNullValue(ArgValue->getType());
    Value *CmpResult = Builder.CreateICmpSLT(ArgValue, Zero, "abscond");
    Value *Result = Builder.CreateSelect(CmpResult, NegOp, ArgValue, "abs");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_conj:
  case Builtin::BI__builtin_conjf:
  case Builtin::BI__builtin_conjl: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    Value *Real = ComplexVal.first;
    Value *Imag = ComplexVal.second;
    Value *Zero =
      Imag->getType()->isFPOrFPVectorTy()
        ? llvm::ConstantFP::getZeroValueForNegation(Imag->getType())
        : llvm::Constant::getNullValue(Imag->getType());

    Imag = Builder.CreateFSub(Zero, Imag, "sub");
    return RValue::getComplex(std::make_pair(Real, Imag));
  }
  case Builtin::BI__builtin_creal:
  case Builtin::BI__builtin_crealf:
  case Builtin::BI__builtin_creall:
  case Builtin::BIcreal:
  case Builtin::BIcrealf:
  case Builtin::BIcreall: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.first);
  }

  case Builtin::BI__builtin_dump_struct: {
    Value *Func = EmitScalarExpr(E->getArg(1)->IgnoreImpCasts());
    CharUnits Arg0Align = EmitPointerWithAlignment(E->getArg(0)).getAlignment();

    const Expr *Arg0 = E->getArg(0)->IgnoreImpCasts();
    QualType Arg0Type = Arg0->getType()->getPointeeType();

    Value *RecordPtr = EmitScalarExpr(Arg0);
    Value *Res = dumpRecord(*this, Arg0Type, RecordPtr, Arg0Align, Func, 0);
    return RValue::get(Res);
  }

  case Builtin::BI__builtin_cimag:
  case Builtin::BI__builtin_cimagf:
  case Builtin::BI__builtin_cimagl:
  case Builtin::BIcimag:
  case Builtin::BIcimagf:
  case Builtin::BIcimagl: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.second);
  }

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll: {
    // clrsb(x) -> clz(x < 0 ? ~x : x) - 1 or
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsNeg = Builder.CreateICmpSLT(ArgValue, Zero, "isneg");
    Value *Inverse = Builder.CreateNot(ArgValue, "not");
    Value *Tmp = Builder.CreateSelect(IsNeg, Inverse, ArgValue);
    Value *Ctlz = Builder.CreateCall(F, {Tmp, Builder.getFalse()});
    Value *Result = Builder.CreateSub(Ctlz, llvm::ConstantInt::get(ArgType, 1));
    Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                   "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ctzs:
  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll: {
    Value *ArgValue = EmitCheckedArgForBuiltin(E->getArg(0), BCK_CTZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *ZeroUndef = Builder.getInt1(getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_clzs:
  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll: {
    Value *ArgValue = EmitCheckedArgForBuiltin(E->getArg(0), BCK_CLZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *ZeroUndef = Builder.getInt1(getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    // ffs(x) -> x ? cttz(x) + 1 : 0
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp =
        Builder.CreateAdd(Builder.CreateCall(F, {ArgValue, Builder.getTrue()}),
                          llvm::ConstantInt::get(ArgType, 1));
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *Result = Builder.CreateSelect(IsZero, Zero, Tmp, "ffs");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    // parity(x) -> ctpop(x) & 1
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp = Builder.CreateCall(F, ArgValue);
    Value *Result = Builder.CreateAnd(Tmp, llvm::ConstantInt::get(ArgType, 1));
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__lzcnt16:
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, {ArgValue, Builder.getFalse()});
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__popcnt16:
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64:
  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_unpredictable: {
    // Always return the argument of __builtin_unpredictable. LLVM does not
    // handle this builtin. Metadata for this builtin should be added directly
    // to instructions such as branches or switches that use it.
    return RValue::get(EmitScalarExpr(E->getArg(0)));
  }
  case Builtin::BI__builtin_expect: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();

    Value *ExpectedValue = EmitScalarExpr(E->getArg(1));
    // Don't generate llvm.expect on -O0 as the backend won't use it for
    // anything.
    // Note, we still IRGen ExpectedValue because it could have side-effects.
    if (CGM.getCodeGenOpts().OptimizationLevel == 0)
      return RValue::get(ArgValue);

    Value *FnExpect = CGM.getIntrinsic(Intrinsic::expect, ArgType);
    Value *Result =
        Builder.CreateCall(FnExpect, {ArgValue, ExpectedValue}, "expval");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_assume_aligned: {
    const Expr *Ptr = E->getArg(0);
    Value *PtrValue = EmitScalarExpr(Ptr);
    Value *OffsetValue =
      (E->getNumArgs() > 2) ? EmitScalarExpr(E->getArg(2)) : nullptr;

    Value *AlignmentValue = EmitScalarExpr(E->getArg(1));
    ConstantInt *AlignmentCI = cast<ConstantInt>(AlignmentValue);
    unsigned Alignment = (unsigned)AlignmentCI->getZExtValue();

    EmitAlignmentAssumption(PtrValue, Ptr, /*The expr loc is sufficient.*/ SourceLocation(),
                            Alignment, OffsetValue);
    return RValue::get(PtrValue);
  }
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume: {
    if (E->getArg(0)->HasSideEffects(getContext()))
      return RValue::get(nullptr);

    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    Value *FnAssume = CGM.getIntrinsic(Intrinsic::assume);
    return RValue::get(Builder.CreateCall(FnAssume, ArgValue));
  }
  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64: {
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::bswap));
  }
  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64: {
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::bitreverse));
  }
  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
  case Builtin::BI_rotl8: // Microsoft variants of rotate left
  case Builtin::BI_rotl16:
  case Builtin::BI_rotl:
  case Builtin::BI_lrotl:
  case Builtin::BI_rotl64:
    return emitRotate(E, false);

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
  case Builtin::BI_rotr8: // Microsoft variants of rotate right
  case Builtin::BI_rotr16:
  case Builtin::BI_rotr:
  case Builtin::BI_lrotr:
  case Builtin::BI_rotr64:
    return emitRotate(E, true);

  case Builtin::BI__builtin_constant_p: {
    llvm::Type *ResultType = ConvertType(E->getType());
    if (CGM.getCodeGenOpts().OptimizationLevel == 0)
      // At -O0, we don't perform inlining, so we don't need to delay the
      // processing.
      return RValue::get(ConstantInt::get(ResultType, 0));

    const Expr *Arg = E->getArg(0);
    QualType ArgType = Arg->getType();
    if (!hasScalarEvaluationKind(ArgType) || ArgType->isFunctionType())
      // We can only reason about scalar types.
      return RValue::get(ConstantInt::get(ResultType, 0));

    Value *ArgValue = EmitScalarExpr(Arg);
    Value *F = CGM.getIntrinsic(Intrinsic::is_constant, ConvertType(ArgType));
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/false);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_object_size: {
    unsigned Type =
        E->getArg(1)->EvaluateKnownConstInt(getContext()).getZExtValue();
    auto *ResType = cast<llvm::IntegerType>(ConvertType(E->getType()));

    // We pass this builtin onto the optimizer so that it can figure out the
    // object size in more complex cases.
    return RValue::get(emitBuiltinObjectSize(E->getArg(0), Type, ResType,
                                             /*EmittedE=*/nullptr));
  }
  case Builtin::BI__builtin_prefetch: {
    Value *Locality, *RW, *Address = EmitScalarExpr(E->getArg(0));
    // FIXME: Technically these constants should of type 'int', yes?
    RW = (E->getNumArgs() > 1) ? EmitScalarExpr(E->getArg(1)) :
      llvm::ConstantInt::get(Int32Ty, 0);
    Locality = (E->getNumArgs() > 2) ? EmitScalarExpr(E->getArg(2)) :
      llvm::ConstantInt::get(Int32Ty, 3);
    Value *Data = llvm::ConstantInt::get(Int32Ty, 1);
    Value *F = CGM.getIntrinsic(Intrinsic::prefetch);
    return RValue::get(Builder.CreateCall(F, {Address, RW, Locality, Data}));
  }
  case Builtin::BI__builtin_readcyclecounter: {
    Value *F = CGM.getIntrinsic(Intrinsic::readcyclecounter);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin___clear_cache: {
    Value *Begin = EmitScalarExpr(E->getArg(0));
    Value *End = EmitScalarExpr(E->getArg(1));
    Value *F = CGM.getIntrinsic(Intrinsic::clear_cache);
    return RValue::get(Builder.CreateCall(F, {Begin, End}));
  }
  case Builtin::BI__builtin_trap:
    return RValue::get(EmitTrapCall(Intrinsic::trap));
  case Builtin::BI__debugbreak:
    return RValue::get(EmitTrapCall(Intrinsic::debugtrap));
  case Builtin::BI__builtin_unreachable: {
    EmitUnreachable(E->getExprLoc());

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("unreachable.cont"));

    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_powi:
  case Builtin::BI__builtin_powif:
  case Builtin::BI__builtin_powil: {
    Value *Base = EmitScalarExpr(E->getArg(0));
    Value *Exponent = EmitScalarExpr(E->getArg(1));
    llvm::Type *ArgType = Base->getType();
    Value *F = CGM.getIntrinsic(Intrinsic::powi, ArgType);
    return RValue::get(Builder.CreateCall(F, {Base, Exponent}));
  }

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered: {
    // Ordered comparisons: we know the arguments to these are matching scalar
    // floating point values.
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));

    switch (BuiltinID) {
    default: llvm_unreachable("Unknown ordered comparison");
    case Builtin::BI__builtin_isgreater:
      LHS = Builder.CreateFCmpOGT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isgreaterequal:
      LHS = Builder.CreateFCmpOGE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isless:
      LHS = Builder.CreateFCmpOLT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessequal:
      LHS = Builder.CreateFCmpOLE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessgreater:
      LHS = Builder.CreateFCmpONE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isunordered:
      LHS = Builder.CreateFCmpUNO(LHS, RHS, "cmp");
      break;
    }
    // ZExt bool to int type.
    return RValue::get(Builder.CreateZExt(LHS, ConvertType(E->getType())));
  }
  case Builtin::BI__builtin_isnan: {
    Value *V = EmitScalarExpr(E->getArg(0));
    V = Builder.CreateFCmpUNO(V, V, "cmp");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType())));
  }

  case Builtin::BIfinite:
  case Builtin::BI__finite:
  case Builtin::BIfinitef:
  case Builtin::BI__finitef:
  case Builtin::BIfinitel:
  case Builtin::BI__finitel:
  case Builtin::BI__builtin_isinf:
  case Builtin::BI__builtin_isfinite: {
    // isinf(x)    --> fabs(x) == infinity
    // isfinite(x) --> fabs(x) != infinity
    // x != NaN via the ordered compare in either case.
    Value *V = EmitScalarExpr(E->getArg(0));
    Value *Fabs = EmitFAbs(*this, V);
    Constant *Infinity = ConstantFP::getInfinity(V->getType());
    CmpInst::Predicate Pred = (BuiltinID == Builtin::BI__builtin_isinf)
                                  ? CmpInst::FCMP_OEQ
                                  : CmpInst::FCMP_ONE;
    Value *FCmp = Builder.CreateFCmp(Pred, Fabs, Infinity, "cmpinf");
    return RValue::get(Builder.CreateZExt(FCmp, ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isinf_sign: {
    // isinf_sign(x) -> fabs(x) == infinity ? (signbit(x) ? -1 : 1) : 0
    Value *Arg = EmitScalarExpr(E->getArg(0));
    Value *AbsArg = EmitFAbs(*this, Arg);
    Value *IsInf = Builder.CreateFCmpOEQ(
        AbsArg, ConstantFP::getInfinity(Arg->getType()), "isinf");
    Value *IsNeg = EmitSignBit(*this, Arg);

    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Zero = Constant::getNullValue(IntTy);
    Value *One = ConstantInt::get(IntTy, 1);
    Value *NegativeOne = ConstantInt::get(IntTy, -1);
    Value *SignResult = Builder.CreateSelect(IsNeg, NegativeOne, One);
    Value *Result = Builder.CreateSelect(IsInf, SignResult, Zero);
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_isnormal: {
    // isnormal(x) --> x == x && fabsf(x) < infinity && fabsf(x) >= float_min
    Value *V = EmitScalarExpr(E->getArg(0));
    Value *Eq = Builder.CreateFCmpOEQ(V, V, "iseq");

    Value *Abs = EmitFAbs(*this, V);
    Value *IsLessThanInf =
      Builder.CreateFCmpULT(Abs, ConstantFP::getInfinity(V->getType()),"isinf");
    APFloat Smallest = APFloat::getSmallestNormalized(
                   getContext().getFloatTypeSemantics(E->getArg(0)->getType()));
    Value *IsNormal =
      Builder.CreateFCmpUGE(Abs, ConstantFP::get(V->getContext(), Smallest),
                            "isnormal");
    V = Builder.CreateAnd(Eq, IsLessThanInf, "and");
    V = Builder.CreateAnd(V, IsNormal, "and");
    return RValue::get(Builder.CreateZExt(V, ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_fpclassify: {
    Value *V = EmitScalarExpr(E->getArg(5));
    llvm::Type *Ty = ConvertType(E->getArg(5)->getType());

    // Create Result
    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("fpclassify_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result =
      Builder.CreatePHI(ConvertType(E->getArg(0)->getType()), 4,
                        "fpclassify_result");

    // if (V==0) return FP_ZERO
    Builder.SetInsertPoint(Begin);
    Value *IsZero = Builder.CreateFCmpOEQ(V, Constant::getNullValue(Ty),
                                          "iszero");
    Value *ZeroLiteral = EmitScalarExpr(E->getArg(4));
    BasicBlock *NotZero = createBasicBlock("fpclassify_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ZeroLiteral, Begin);

    // if (V != V) return FP_NAN
    Builder.SetInsertPoint(NotZero);
    Value *IsNan = Builder.CreateFCmpUNO(V, V, "cmp");
    Value *NanLiteral = EmitScalarExpr(E->getArg(0));
    BasicBlock *NotNan = createBasicBlock("fpclassify_not_nan", this->CurFn);
    Builder.CreateCondBr(IsNan, End, NotNan);
    Result->addIncoming(NanLiteral, NotZero);

    // if (fabs(V) == infinity) return FP_INFINITY
    Builder.SetInsertPoint(NotNan);
    Value *VAbs = EmitFAbs(*this, V);
    Value *IsInf =
      Builder.CreateFCmpOEQ(VAbs, ConstantFP::getInfinity(V->getType()),
                            "isinf");
    Value *InfLiteral = EmitScalarExpr(E->getArg(1));
    BasicBlock *NotInf = createBasicBlock("fpclassify_not_inf", this->CurFn);
    Builder.CreateCondBr(IsInf, End, NotInf);
    Result->addIncoming(InfLiteral, NotNan);

    // if (fabs(V) >= MIN_NORMAL) return FP_NORMAL else FP_SUBNORMAL
    Builder.SetInsertPoint(NotInf);
    APFloat Smallest = APFloat::getSmallestNormalized(
        getContext().getFloatTypeSemantics(E->getArg(5)->getType()));
    Value *IsNormal =
      Builder.CreateFCmpUGE(VAbs, ConstantFP::get(V->getContext(), Smallest),
                            "isnormal");
    Value *NormalResult =
      Builder.CreateSelect(IsNormal, EmitScalarExpr(E->getArg(2)),
                           EmitScalarExpr(E->getArg(3)));
    Builder.CreateBr(End);
    Result->addIncoming(NormalResult, NotInf);

    // return Result
    Builder.SetInsertPoint(End);
    return RValue::get(Result);
  }

  case Builtin::BIalloca:
  case Builtin::BI_alloca:
  case Builtin::BI__builtin_alloca: {
    Value *Size = EmitScalarExpr(E->getArg(0));
    const TargetInfo &TI = getContext().getTargetInfo();
    // The alignment of the alloca should correspond to __BIGGEST_ALIGNMENT__.
    unsigned SuitableAlignmentInBytes =
        CGM.getContext()
            .toCharUnitsFromBits(TI.getSuitableAlign())
            .getQuantity();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(SuitableAlignmentInBytes);
    return RValue::get(AI);
  }

  case Builtin::BI__builtin_alloca_with_align: {
    Value *Size = EmitScalarExpr(E->getArg(0));
    Value *AlignmentInBitsValue = EmitScalarExpr(E->getArg(1));
    auto *AlignmentInBitsCI = cast<ConstantInt>(AlignmentInBitsValue);
    unsigned AlignmentInBits = AlignmentInBitsCI->getZExtValue();
    unsigned AlignmentInBytes =
        CGM.getContext().toCharUnitsFromBits(AlignmentInBits).getQuantity();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(AlignmentInBytes);
    return RValue::get(AI);
  }

  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *SizeVal = EmitScalarExpr(E->getArg(1));
    EmitNonNullArgCheck(RValue::get(Dest.getPointer()), E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    Builder.CreateMemSet(Dest, Builder.getInt8(0), SizeVal, false);
    return RValue::get(nullptr);
  }
  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitNonNullArgCheck(RValue::get(Dest.getPointer()), E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    EmitNonNullArgCheck(RValue::get(Src.getPointer()), E->getArg(1)->getType(),
                        E->getArg(1)->getExprLoc(), FD, 1);
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BI__builtin_char_memchr:
    BuiltinID = Builtin::BI__builtin_memchr;
    break;

  case Builtin::BI__builtin___memcpy_chk: {
    // fold __builtin_memcpy_chk(x, y, cst1, cst2) to memcpy iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BI__builtin_objc_memmove_collectable: {
    Address DestAddr = EmitPointerWithAlignment(E->getArg(0));
    Address SrcAddr = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    CGM.getObjCRuntime().EmitGCMemmoveCollectable(*this,
                                                  DestAddr, SrcAddr, SizeVal);
    return RValue::get(DestAddr.getPointer());
  }

  case Builtin::BI__builtin___memmove_chk: {
    // fold __builtin_memmove_chk(x, y, cst1, cst2) to memmove iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitNonNullArgCheck(RValue::get(Dest.getPointer()), E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    EmitNonNullArgCheck(RValue::get(Src.getPointer()), E->getArg(1)->getType(),
                        E->getArg(1)->getExprLoc(), FD, 1);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *ByteVal = Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)),
                                         Builder.getInt8Ty());
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitNonNullArgCheck(RValue::get(Dest.getPointer()), E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BI__builtin___memset_chk: {
    // fold __builtin_memset_chk(x, y, cst1, cst2) to memset iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *ByteVal = Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)),
                                         Builder.getInt8Ty());
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BI__builtin_wmemcmp: {
    // The MSVC runtime library does not provide a definition of wmemcmp, so we
    // need an inline implementation.
    if (!getTarget().getTriple().isOSMSVCRT())
      break;

    llvm::Type *WCharTy = ConvertType(getContext().WCharTy);

    Value *Dst = EmitScalarExpr(E->getArg(0));
    Value *Src = EmitScalarExpr(E->getArg(1));
    Value *Size = EmitScalarExpr(E->getArg(2));

    BasicBlock *Entry = Builder.GetInsertBlock();
    BasicBlock *CmpGT = createBasicBlock("wmemcmp.gt");
    BasicBlock *CmpLT = createBasicBlock("wmemcmp.lt");
    BasicBlock *Next = createBasicBlock("wmemcmp.next");
    BasicBlock *Exit = createBasicBlock("wmemcmp.exit");
    Value *SizeEq0 = Builder.CreateICmpEQ(Size, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(SizeEq0, Exit, CmpGT);

    EmitBlock(CmpGT);
    PHINode *DstPhi = Builder.CreatePHI(Dst->getType(), 2);
    DstPhi->addIncoming(Dst, Entry);
    PHINode *SrcPhi = Builder.CreatePHI(Src->getType(), 2);
    SrcPhi->addIncoming(Src, Entry);
    PHINode *SizePhi = Builder.CreatePHI(SizeTy, 2);
    SizePhi->addIncoming(Size, Entry);
    CharUnits WCharAlign =
        getContext().getTypeAlignInChars(getContext().WCharTy);
    Value *DstCh = Builder.CreateAlignedLoad(WCharTy, DstPhi, WCharAlign);
    Value *SrcCh = Builder.CreateAlignedLoad(WCharTy, SrcPhi, WCharAlign);
    Value *DstGtSrc = Builder.CreateICmpUGT(DstCh, SrcCh);
    Builder.CreateCondBr(DstGtSrc, Exit, CmpLT);

    EmitBlock(CmpLT);
    Value *DstLtSrc = Builder.CreateICmpULT(DstCh, SrcCh);
    Builder.CreateCondBr(DstLtSrc, Exit, Next);

    EmitBlock(Next);
    Value *NextDst = Builder.CreateConstInBoundsGEP1_32(WCharTy, DstPhi, 1);
    Value *NextSrc = Builder.CreateConstInBoundsGEP1_32(WCharTy, SrcPhi, 1);
    Value *NextSize = Builder.CreateSub(SizePhi, ConstantInt::get(SizeTy, 1));
    Value *NextSizeEq0 =
        Builder.CreateICmpEQ(NextSize, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(NextSizeEq0, Exit, CmpGT);
    DstPhi->addIncoming(NextDst, Next);
    SrcPhi->addIncoming(NextSrc, Next);
    SizePhi->addIncoming(NextSize, Next);

    EmitBlock(Exit);
    PHINode *Ret = Builder.CreatePHI(IntTy, 4);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Entry);
    Ret->addIncoming(ConstantInt::get(IntTy, 1), CmpGT);
    Ret->addIncoming(ConstantInt::get(IntTy, -1), CmpLT);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Next);
    return RValue::get(Ret);
  }
  case Builtin::BI__builtin_dwarf_cfa: {
    // The offset in bytes from the first argument to the CFA.
    //
    // Why on earth is this in the frontend?  Is there any reason at
    // all that the backend can't reasonably determine this while
    // lowering llvm.eh.dwarf.cfa()?
    //
    // TODO: If there's a satisfactory reason, add a target hook for
    // this instead of hard-coding 0, which is correct for most targets.
    int32_t Offset = 0;

    Value *F = CGM.getIntrinsic(Intrinsic::eh_dwarf_cfa);
    return RValue::get(Builder.CreateCall(F,
                                      llvm::ConstantInt::get(Int32Ty, Offset)));
  }
  case Builtin::BI__builtin_return_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(E->getArg(0),
                                                   getContext().UnsignedIntTy);
    Value *F = CGM.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI_ReturnAddress: {
    Value *F = CGM.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Builder.getInt32(0)));
  }
  case Builtin::BI__builtin_frame_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(E->getArg(0),
                                                   getContext().UnsignedIntTy);
    Value *F = CGM.getIntrinsic(Intrinsic::frameaddress);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI__builtin_extract_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().decodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_frob_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().encodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dwarf_sp_column: {
    llvm::IntegerType *Ty
      = cast<llvm::IntegerType>(ConvertType(E->getType()));
    int Column = getTargetHooks().getDwarfEHStackPointer(CGM);
    if (Column == -1) {
      CGM.ErrorUnsupported(E, "__builtin_dwarf_sp_column");
      return RValue::get(llvm::UndefValue::get(Ty));
    }
    return RValue::get(llvm::ConstantInt::get(Ty, Column, true));
  }
  case Builtin::BI__builtin_init_dwarf_reg_size_table: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    if (getTargetHooks().initDwarfEHRegSizeTable(*this, Address))
      CGM.ErrorUnsupported(E, "__builtin_init_dwarf_reg_size_table");
    return RValue::get(llvm::UndefValue::get(ConvertType(E->getType())));
  }
  case Builtin::BI__builtin_eh_return: {
    Value *Int = EmitScalarExpr(E->getArg(0));
    Value *Ptr = EmitScalarExpr(E->getArg(1));

    llvm::IntegerType *IntTy = cast<llvm::IntegerType>(Int->getType());
    assert((IntTy->getBitWidth() == 32 || IntTy->getBitWidth() == 64) &&
           "LLVM's __builtin_eh_return only supports 32- and 64-bit variants");
    Value *F = CGM.getIntrinsic(IntTy->getBitWidth() == 32
                                  ? Intrinsic::eh_return_i32
                                  : Intrinsic::eh_return_i64);
    Builder.CreateCall(F, {Int, Ptr});
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("builtin_eh_return.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_unwind_init: {
    Value *F = CGM.getIntrinsic(Intrinsic::eh_unwind_init);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin_extend_pointer: {
    // Extends a pointer to the size of an _Unwind_Word, which is
    // uint64_t on all platforms.  Generally this gets poked into a
    // register and eventually used as an address, so if the
    // addressing registers are wider than pointers and the platform
    // doesn't implicitly ignore high-order bits when doing
    // addressing, we need to make sure we zext / sext based on
    // the platform's expectations.
    //
    // See: http://gcc.gnu.org/ml/gcc-bugs/2002-02/msg00237.html

    // Cast the pointer to intptr_t.
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Result = Builder.CreatePtrToInt(Ptr, IntPtrTy, "extend.cast");

    // If that's 64 bits, we're done.
    if (IntPtrTy->getBitWidth() == 64)
      return RValue::get(Result);

    // Otherwise, ask the codegen data what to do.
    if (getTargetHooks().extendPointerWithSExt())
      return RValue::get(Builder.CreateSExt(Result, Int64Ty, "extend.sext"));
    else
      return RValue::get(Builder.CreateZExt(Result, Int64Ty, "extend.zext"));
  }
  case Builtin::BI__builtin_setjmp: {
    // Buffer is a void**.
    Address Buf = EmitPointerWithAlignment(E->getArg(0));

    // Store the frame pointer to the setjmp buffer.
    Value *FrameAddr =
      Builder.CreateCall(CGM.getIntrinsic(Intrinsic::frameaddress),
                         ConstantInt::get(Int32Ty, 0));
    Builder.CreateStore(FrameAddr, Buf);

    // Store the stack pointer to the setjmp buffer.
    Value *StackAddr =
        Builder.CreateCall(CGM.getIntrinsic(Intrinsic::stacksave));
    Address StackSaveSlot =
      Builder.CreateConstInBoundsGEP(Buf, 2, getPointerSize());
    Builder.CreateStore(StackAddr, StackSaveSlot);

    // Call LLVM's EH setjmp, which is lightweight.
    Value *F = CGM.getIntrinsic(Intrinsic::eh_sjlj_setjmp);
    Buf = Builder.CreateBitCast(Buf, Int8PtrTy);
    return RValue::get(Builder.CreateCall(F, Buf.getPointer()));
  }
  case Builtin::BI__builtin_longjmp: {
    Value *Buf = EmitScalarExpr(E->getArg(0));
    Buf = Builder.CreateBitCast(Buf, Int8PtrTy);

    // Call LLVM's EH longjmp, which is lightweight.
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::eh_sjlj_longjmp), Buf);

    // longjmp doesn't return; mark this as unreachable.
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("longjmp.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_launder: {
    const Expr *Arg = E->getArg(0);
    QualType ArgTy = Arg->getType()->getPointeeType();
    Value *Ptr = EmitScalarExpr(Arg);
    if (TypeRequiresBuiltinLaunder(CGM, ArgTy))
      Ptr = Builder.CreateLaunderInvariantGroup(Ptr);

    return RValue::get(Ptr);
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_swap:
    llvm_unreachable("Shouldn't make it through sema");
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Add, E);
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Sub, E);
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Or, E);
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::And, E);
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xor, E);
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Nand, E);

  // Clang extensions: not overloaded yet.
  case Builtin::BI__sync_fetch_and_min:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Min, E);
  case Builtin::BI__sync_fetch_and_max:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Max, E);
  case Builtin::BI__sync_fetch_and_umin:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::UMin, E);
  case Builtin::BI__sync_fetch_and_umax:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::UMax, E);

  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Add, E,
                                llvm::Instruction::Add);
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Sub, E,
                                llvm::Instruction::Sub);
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::And, E,
                                llvm::Instruction::And);
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Or, E,
                                llvm::Instruction::Or);
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Xor, E,
                                llvm::Instruction::Xor);
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Nand, E,
                                llvm::Instruction::And, true);

  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, false));

  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, true));

  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    QualType ElTy = E->getArg(0)->getType()->getPointeeType();
    CharUnits StoreSize = getContext().getTypeSizeInChars(ElTy);
    llvm::Type *ITy = llvm::IntegerType::get(getLLVMContext(),
                                             StoreSize.getQuantity() * 8);
    Ptr = Builder.CreateBitCast(Ptr, ITy->getPointerTo());
    llvm::StoreInst *Store =
      Builder.CreateAlignedStore(llvm::Constant::getNullValue(ITy), Ptr,
                                 StoreSize);
    Store->setAtomic(llvm::AtomicOrdering::Release);
    return RValue::get(nullptr);
  }

  case Builtin::BI__sync_synchronize: {
    // We assume this is supposed to correspond to a C++0x-style
    // sequentially-consistent fence (i.e. this is only usable for
    // synchronization, not device I/O or anything like that). This intrinsic
    // is really badly designed in the sense that in theory, there isn't
    // any way to safely use it... but in practice, it mostly works
    // to use it with non-atomic loads and stores to get acquire/release
    // semantics.
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_nontemporal_load:
    return RValue::get(EmitNontemporalLoad(*this, E));
  case Builtin::BI__builtin_nontemporal_store:
    return RValue::get(EmitNontemporalStore(*this, E));
  case Builtin::BI__c11_atomic_is_lock_free:
  case Builtin::BI__atomic_is_lock_free: {
    // Call "bool __atomic_is_lock_free(size_t size, void *ptr)". For the
    // __c11 builtin, ptr is 0 (indicating a properly-aligned object), since
    // _Atomic(T) is always properly-aligned.
    const char *LibCallName = "__atomic_is_lock_free";
    CallArgList Args;
    Args.add(RValue::get(EmitScalarExpr(E->getArg(0))),
             getContext().getSizeType());
    if (BuiltinID == Builtin::BI__atomic_is_lock_free)
      Args.add(RValue::get(EmitScalarExpr(E->getArg(1))),
               getContext().VoidPtrTy);
    else
      Args.add(RValue::get(llvm::Constant::getNullValue(VoidPtrTy)),
               getContext().VoidPtrTy);
    const CGFunctionInfo &FuncInfo =
        CGM.getTypes().arrangeBuiltinFunctionCall(E->getType(), Args);
    llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FuncInfo);
    llvm::Constant *Func = CGM.CreateRuntimeFunction(FTy, LibCallName);
    return EmitCall(FuncInfo, CGCallee::forDirect(Func),
                    ReturnValueSlot(), Args);
  }

  case Builtin::BI__atomic_test_and_set: {
    // Look at the argument type to determine whether this is a volatile
    // operation. The parameter type is always volatile.
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Value *Ptr = EmitScalarExpr(E->getArg(0));
    unsigned AddrSpace = Ptr->getType()->getPointerAddressSpace();
    Ptr = Builder.CreateBitCast(Ptr, Int8Ty->getPointerTo(AddrSpace));
    Value *NewVal = Builder.getInt8(1);
    Value *Order = EmitScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      AtomicRMWInst *Result = nullptr;
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Monotonic);
        break;
      case 1: // memory_order_consume
      case 2: // memory_order_acquire
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Acquire);
        break;
      case 3: // memory_order_release
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Release);
        break;
      case 4: // memory_order_acq_rel

        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::AcquireRelease);
        break;
      case 5: // memory_order_seq_cst
        Result = Builder.CreateAtomicRMW(
            llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
            llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      Result->setVolatile(Volatile);
      return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[5] = {
      createBasicBlock("monotonic", CurFn),
      createBasicBlock("acquire", CurFn),
      createBasicBlock("release", CurFn),
      createBasicBlock("acqrel", CurFn),
      createBasicBlock("seqcst", CurFn)
    };
    llvm::AtomicOrdering Orders[5] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Acquire,
        llvm::AtomicOrdering::Release, llvm::AtomicOrdering::AcquireRelease,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    Builder.SetInsertPoint(ContBB);
    PHINode *Result = Builder.CreatePHI(Int8Ty, 5, "was_set");

    for (unsigned i = 0; i < 5; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      AtomicRMWInst *RMW = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg,
                                                   Ptr, NewVal, Orders[i]);
      RMW->setVolatile(Volatile);
      Result->addIncoming(RMW, BBs[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(1), BBs[1]);
    SI->addCase(Builder.getInt32(2), BBs[1]);
    SI->addCase(Builder.getInt32(3), BBs[2]);
    SI->addCase(Builder.getInt32(4), BBs[3]);
    SI->addCase(Builder.getInt32(5), BBs[4]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
  }

  case Builtin::BI__atomic_clear: {
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Address Ptr = EmitPointerWithAlignment(E->getArg(0));
    unsigned AddrSpace = Ptr.getPointer()->getType()->getPointerAddressSpace();
    Ptr = Builder.CreateBitCast(Ptr, Int8Ty->getPointerTo(AddrSpace));
    Value *NewVal = Builder.getInt8(0);
    Value *Order = EmitScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Store->setOrdering(llvm::AtomicOrdering::Monotonic);
        break;
      case 3:  // memory_order_release
        Store->setOrdering(llvm::AtomicOrdering::Release);
        break;
      case 5:  // memory_order_seq_cst
        Store->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[3] = {
      createBasicBlock("monotonic", CurFn),
      createBasicBlock("release", CurFn),
      createBasicBlock("seqcst", CurFn)
    };
    llvm::AtomicOrdering Orders[3] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Release,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    for (unsigned i = 0; i < 3; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      Store->setOrdering(Orders[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(3), BBs[1]);
    SI->addCase(Builder.getInt32(5), BBs[2]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__c11_atomic_thread_fence:
  case Builtin::BI__c11_atomic_signal_fence: {
    llvm::SyncScope::ID SSID;
    if (BuiltinID == Builtin::BI__atomic_signal_fence ||
        BuiltinID == Builtin::BI__c11_atomic_signal_fence)
      SSID = llvm::SyncScope::SingleThread;
    else
      SSID = llvm::SyncScope::System;
    Value *Order = EmitScalarExpr(E->getArg(0));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        break;
      case 1:  // memory_order_consume
      case 2:  // memory_order_acquire
        Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
        break;
      case 3:  // memory_order_release
        Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
        break;
      case 4:  // memory_order_acq_rel
        Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
        break;
      case 5:  // memory_order_seq_cst
        Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *AcquireBB, *ReleaseBB, *AcqRelBB, *SeqCstBB;
    AcquireBB = createBasicBlock("acquire", CurFn);
    ReleaseBB = createBasicBlock("release", CurFn);
    AcqRelBB = createBasicBlock("acqrel", CurFn);
    SeqCstBB = createBasicBlock("seqcst", CurFn);
    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, ContBB);

    Builder.SetInsertPoint(AcquireBB);
    Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(1), AcquireBB);
    SI->addCase(Builder.getInt32(2), AcquireBB);

    Builder.SetInsertPoint(ReleaseBB);
    Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(3), ReleaseBB);

    Builder.SetInsertPoint(AcqRelBB);
    Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(4), AcqRelBB);

    Builder.SetInsertPoint(SeqCstBB);
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(5), SeqCstBB);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl: {
    return RValue::get(
        Builder.CreateZExt(EmitSignBit(*this, EmitScalarExpr(E->getArg(0))),
                           ConvertType(E->getType())));
  }
  case Builtin::BI__annotation: {
    // Re-encode each wide string to UTF8 and make an MDString.
    SmallVector<Metadata *, 1> Strings;
    for (const Expr *Arg : E->arguments()) {
      const auto *Str = cast<StringLiteral>(Arg->IgnoreParenCasts());
      assert(Str->getCharByteWidth() == 2);
      StringRef WideBytes = Str->getBytes();
      std::string StrUtf8;
      if (!convertUTF16ToUTF8String(
              makeArrayRef(WideBytes.data(), WideBytes.size()), StrUtf8)) {
        CGM.ErrorUnsupported(E, "non-UTF16 __annotation argument");
        continue;
      }
      Strings.push_back(llvm::MDString::get(getLLVMContext(), StrUtf8));
    }

    // Build and MDTuple of MDStrings and emit the intrinsic call.
    llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::codeview_annotation, {});
    MDTuple *StrTuple = MDTuple::get(getLLVMContext(), Strings);
    Builder.CreateCall(F, MetadataAsValue::get(getLLVMContext(), StrTuple));
    return RValue::getIgnored();
  }
  case Builtin::BI__builtin_annotation: {
    llvm::Value *AnnVal = EmitScalarExpr(E->getArg(0));
    llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::annotation,
                                      AnnVal->getType());

    // Get the annotation string, go through casts. Sema requires this to be a
    // non-wide string literal, potentially casted, so the cast<> is safe.
    const Expr *AnnotationStrExpr = E->getArg(1)->IgnoreParenCasts();
    StringRef Str = cast<StringLiteral>(AnnotationStrExpr)->getString();
    return RValue::get(EmitAnnotationCall(F, AnnVal, Str, E->getExprLoc()));
  }
  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll: {

    // We translate all of these builtins from expressions of the form:
    //   int x = ..., y = ..., carryin = ..., carryout, result;
    //   result = __builtin_addc(x, y, carryin, &carryout);
    //
    // to LLVM IR of the form:
    //
    //   %tmp1 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %x, i32 %y)
    //   %tmpsum1 = extractvalue {i32, i1} %tmp1, 0
    //   %carry1 = extractvalue {i32, i1} %tmp1, 1
    //   %tmp2 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %tmpsum1,
    //                                                       i32 %carryin)
    //   %result = extractvalue {i32, i1} %tmp2, 0
    //   %carry2 = extractvalue {i32, i1} %tmp2, 1
    //   %tmp3 = or i1 %carry1, %carry2
    //   %tmp4 = zext i1 %tmp3 to i32
    //   store i32 %tmp4, i32* %carryout

    // Scalarize our inputs.
    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    llvm::Value *Carryin = EmitScalarExpr(E->getArg(2));
    Address CarryOutPtr = EmitPointerWithAlignment(E->getArg(3));

    // Decide if we are lowering to a uadd.with.overflow or usub.with.overflow.
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default: llvm_unreachable("Unknown multiprecision builtin id.");
    case Builtin::BI__builtin_addcb:
    case Builtin::BI__builtin_addcs:
    case Builtin::BI__builtin_addc:
    case Builtin::BI__builtin_addcl:
    case Builtin::BI__builtin_addcll:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_subcb:
    case Builtin::BI__builtin_subcs:
    case Builtin::BI__builtin_subc:
    case Builtin::BI__builtin_subcl:
    case Builtin::BI__builtin_subcll:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    }

    // Construct our resulting LLVM IR expression.
    llvm::Value *Carry1;
    llvm::Value *Sum1 = EmitOverflowIntrinsic(*this, IntrinsicId,
                                              X, Y, Carry1);
    llvm::Value *Carry2;
    llvm::Value *Sum2 = EmitOverflowIntrinsic(*this, IntrinsicId,
                                              Sum1, Carryin, Carry2);
    llvm::Value *CarryOut = Builder.CreateZExt(Builder.CreateOr(Carry1, Carry2),
                                               X->getType());
    Builder.CreateStore(CarryOut, CarryOutPtr);
    return RValue::get(Sum2);
  }

  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow: {
    const clang::Expr *LeftArg = E->getArg(0);
    const clang::Expr *RightArg = E->getArg(1);
    const clang::Expr *ResultArg = E->getArg(2);

    clang::QualType ResultQTy =
        ResultArg->getType()->castAs<PointerType>()->getPointeeType();

    WidthAndSignedness LeftInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), LeftArg->getType());
    WidthAndSignedness RightInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), RightArg->getType());
    WidthAndSignedness ResultInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), ResultQTy);

    // Handle mixed-sign multiplication as a special case, because adding
    // runtime or backend support for our generic irgen would be too expensive.
    if (isSpecialMixedSignMultiply(BuiltinID, LeftInfo, RightInfo, ResultInfo))
      return EmitCheckedMixedSignMultiply(*this, LeftArg, LeftInfo, RightArg,
                                          RightInfo, ResultArg, ResultQTy,
                                          ResultInfo);

    WidthAndSignedness EncompassingInfo =
        EncompassingIntegerType({LeftInfo, RightInfo, ResultInfo});

    llvm::Type *EncompassingLLVMTy =
        llvm::IntegerType::get(CGM.getLLVMContext(), EncompassingInfo.Width);

    llvm::Type *ResultLLVMTy = CGM.getTypes().ConvertType(ResultQTy);

    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_add_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::sadd_with_overflow
                        : llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_sub_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::ssub_with_overflow
                        : llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_mul_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::smul_with_overflow
                        : llvm::Intrinsic::umul_with_overflow;
      break;
    }

    llvm::Value *Left = EmitScalarExpr(LeftArg);
    llvm::Value *Right = EmitScalarExpr(RightArg);
    Address ResultPtr = EmitPointerWithAlignment(ResultArg);

    // Extend each operand to the encompassing type.
    Left = Builder.CreateIntCast(Left, EncompassingLLVMTy, LeftInfo.Signed);
    Right = Builder.CreateIntCast(Right, EncompassingLLVMTy, RightInfo.Signed);

    // Perform the operation on the extended values.
    llvm::Value *Overflow, *Result;
    Result = EmitOverflowIntrinsic(*this, IntrinsicId, Left, Right, Overflow);

    if (EncompassingInfo.Width > ResultInfo.Width) {
      // The encompassing type is wider than the result type, so we need to
      // truncate it.
      llvm::Value *ResultTrunc = Builder.CreateTrunc(Result, ResultLLVMTy);

      // To see if the truncation caused an overflow, we will extend
      // the result and then compare it to the original result.
      llvm::Value *ResultTruncExt = Builder.CreateIntCast(
          ResultTrunc, EncompassingLLVMTy, ResultInfo.Signed);
      llvm::Value *TruncationOverflow =
          Builder.CreateICmpNE(Result, ResultTruncExt);

      Overflow = Builder.CreateOr(Overflow, TruncationOverflow);
      Result = ResultTrunc;
    }

    // Finally, store the result using the pointer.
    bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
    Builder.CreateStore(EmitToMemory(Result, ResultQTy), ResultPtr, isVolatile);

    return RValue::get(Overflow);
  }

  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {

    // We translate all of these builtins directly to the relevant llvm IR node.

    // Scalarize our inputs.
    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    Address SumOutPtr = EmitPointerWithAlignment(E->getArg(2));

    // Decide which of the overflow intrinsics we are lowering to:
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default: llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
      IntrinsicId = llvm::Intrinsic::umul_with_overflow;
      break;
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
      IntrinsicId = llvm::Intrinsic::sadd_with_overflow;
      break;
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
      IntrinsicId = llvm::Intrinsic::ssub_with_overflow;
      break;
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
      IntrinsicId = llvm::Intrinsic::smul_with_overflow;
      break;
    }


    llvm::Value *Carry;
    llvm::Value *Sum = EmitOverflowIntrinsic(*this, IntrinsicId, X, Y, Carry);
    Builder.CreateStore(Sum, SumOutPtr);

    return RValue::get(Carry);
  }
  case Builtin::BI__builtin_addressof:
    return RValue::get(EmitLValue(E->getArg(0)).getPointer());
  case Builtin::BI__builtin_operator_new:
    return EmitBuiltinNewDeleteCall(
        E->getCallee()->getType()->castAs<FunctionProtoType>(), E, false);
  case Builtin::BI__builtin_operator_delete:
    return EmitBuiltinNewDeleteCall(
        E->getCallee()->getType()->castAs<FunctionProtoType>(), E, true);

  case Builtin::BI__noop:
    // __noop always evaluates to an integer literal zero.
    return RValue::get(ConstantInt::get(IntTy, 0));
  case Builtin::BI__builtin_call_with_static_chain: {
    const CallExpr *Call = cast<CallExpr>(E->getArg(0));
    const Expr *Chain = E->getArg(1);
    return EmitCall(Call->getCallee()->getType(),
                    EmitCallee(Call->getCallee()), Call, ReturnValue,
                    EmitScalarExpr(Chain));
  }
  case Builtin::BI_InterlockedExchange8:
  case Builtin::BI_InterlockedExchange16:
  case Builtin::BI_InterlockedExchange:
  case Builtin::BI_InterlockedExchangePointer:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E));
  case Builtin::BI_InterlockedCompareExchangePointer:
  case Builtin::BI_InterlockedCompareExchangePointer_nf: {
    llvm::Type *RTy;
    llvm::IntegerType *IntType =
      IntegerType::get(getLLVMContext(),
                       getContext().getTypeSize(E->getType()));
    llvm::Type *IntPtrType = IntType->getPointerTo();

    llvm::Value *Destination =
      Builder.CreateBitCast(EmitScalarExpr(E->getArg(0)), IntPtrType);

    llvm::Value *Exchange = EmitScalarExpr(E->getArg(1));
    RTy = Exchange->getType();
    Exchange = Builder.CreatePtrToInt(Exchange, IntType);

    llvm::Value *Comparand =
      Builder.CreatePtrToInt(EmitScalarExpr(E->getArg(2)), IntType);

    auto Ordering =
      BuiltinID == Builtin::BI_InterlockedCompareExchangePointer_nf ?
      AtomicOrdering::Monotonic : AtomicOrdering::SequentiallyConsistent;

    auto Result = Builder.CreateAtomicCmpXchg(Destination, Comparand, Exchange,
                                              Ordering, Ordering);
    Result->setVolatile(true);

    return RValue::get(Builder.CreateIntToPtr(Builder.CreateExtractValue(Result,
                                                                         0),
                                              RTy));
  }
  case Builtin::BI_InterlockedCompareExchange8:
  case Builtin::BI_InterlockedCompareExchange16:
  case Builtin::BI_InterlockedCompareExchange:
  case Builtin::BI_InterlockedCompareExchange64:
    return RValue::get(EmitAtomicCmpXchgForMSIntrin(*this, E));
  case Builtin::BI_InterlockedIncrement16:
  case Builtin::BI_InterlockedIncrement:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E));
  case Builtin::BI_InterlockedDecrement16:
  case Builtin::BI_InterlockedDecrement:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E));
  case Builtin::BI_InterlockedAnd8:
  case Builtin::BI_InterlockedAnd16:
  case Builtin::BI_InterlockedAnd:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E));
  case Builtin::BI_InterlockedExchangeAdd8:
  case Builtin::BI_InterlockedExchangeAdd16:
  case Builtin::BI_InterlockedExchangeAdd:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E));
  case Builtin::BI_InterlockedExchangeSub8:
  case Builtin::BI_InterlockedExchangeSub16:
  case Builtin::BI_InterlockedExchangeSub:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E));
  case Builtin::BI_InterlockedOr8:
  case Builtin::BI_InterlockedOr16:
  case Builtin::BI_InterlockedOr:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E));
  case Builtin::BI_InterlockedXor8:
  case Builtin::BI_InterlockedXor16:
  case Builtin::BI_InterlockedXor:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E));

  case Builtin::BI_bittest64:
  case Builtin::BI_bittest:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandcomplement:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandreset:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_bittestandset:
  case Builtin::BI_interlockedbittestandreset:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
  case Builtin::BI_interlockedbittestandset:
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    return RValue::get(EmitBitTestIntrinsic(*this, BuiltinID, E));

  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    return RValue::get(EmitSEHExceptionCode());
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    return RValue::get(EmitSEHExceptionInfo());
  case Builtin::BI__abnormal_termination:
  case Builtin::BI_abnormal_termination:
    return RValue::get(EmitSEHAbnormalTermination());
  case Builtin::BI_setjmpex:
    if (getTarget().getTriple().isOSMSVCRT())
      return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
    break;
  case Builtin::BI_setjmp:
    if (getTarget().getTriple().isOSMSVCRT()) {
      if (getTarget().getTriple().getArch() == llvm::Triple::x86)
        return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmp3, E);
      else if (getTarget().getTriple().getArch() == llvm::Triple::aarch64)
        return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
      return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmp, E);
    }
    break;

  case Builtin::BI__GetExceptionInfo: {
    if (llvm::GlobalVariable *GV =
            CGM.getCXXABI().getThrowInfo(FD->getParamDecl(0)->getType()))
      return RValue::get(llvm::ConstantExpr::getBitCast(GV, CGM.Int8PtrTy));
    break;
  }

  case Builtin::BI__fastfail:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::__fastfail, E));

  case Builtin::BI__builtin_coro_size: {
    auto & Context = getContext();
    auto SizeTy = Context.getSizeType();
    auto T = Builder.getIntNTy(Context.getTypeSize(SizeTy));
    Value *F = CGM.getIntrinsic(Intrinsic::coro_size, T);
    return RValue::get(Builder.CreateCall(F));
  }

  case Builtin::BI__builtin_coro_id:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_id);
  case Builtin::BI__builtin_coro_promise:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_promise);
  case Builtin::BI__builtin_coro_resume:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_resume);
  case Builtin::BI__builtin_coro_frame:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_frame);
  case Builtin::BI__builtin_coro_noop:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_noop);
  case Builtin::BI__builtin_coro_free:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_free);
  case Builtin::BI__builtin_coro_destroy:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_destroy);
  case Builtin::BI__builtin_coro_done:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_done);
  case Builtin::BI__builtin_coro_alloc:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_alloc);
  case Builtin::BI__builtin_coro_begin:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_begin);
  case Builtin::BI__builtin_coro_end:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_end);
  case Builtin::BI__builtin_coro_suspend:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_suspend);
  case Builtin::BI__builtin_coro_param:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_param);

  // OpenCL v2.0 s6.13.16.2, Built-in pipe read and write functions
  case Builtin::BIread_pipe:
  case Builtin::BIwrite_pipe: {
    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Type of the generic packet parameter.
    unsigned GenericAS =
        getContext().getTargetAddressSpace(LangAS::opencl_generic);
    llvm::Type *I8PTy = llvm::PointerType::get(
        llvm::Type::getInt8Ty(getLLVMContext()), GenericAS);

    // Testing which overloaded version we should generate the call for.
    if (2U == E->getNumArgs()) {
      const char *Name = (BuiltinID == Builtin::BIread_pipe) ? "__read_pipe_2"
                                                             : "__write_pipe_2";
      // Creating a generic function type to be able to call with any builtin or
      // user defined type.
      llvm::Type *ArgTys[] = {Arg0->getType(), I8PTy, Int32Ty, Int32Ty};
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      Value *BCast = Builder.CreatePointerCast(Arg1, I8PTy);
      return RValue::get(
          Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                             {Arg0, BCast, PacketSize, PacketAlign}));
    } else {
      assert(4 == E->getNumArgs() &&
             "Illegal number of parameters to pipe function");
      const char *Name = (BuiltinID == Builtin::BIread_pipe) ? "__read_pipe_4"
                                                             : "__write_pipe_4";

      llvm::Type *ArgTys[] = {Arg0->getType(), Arg1->getType(), Int32Ty, I8PTy,
                              Int32Ty, Int32Ty};
      Value *Arg2 = EmitScalarExpr(E->getArg(2)),
            *Arg3 = EmitScalarExpr(E->getArg(3));
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      Value *BCast = Builder.CreatePointerCast(Arg3, I8PTy);
      // We know the third argument is an integer type, but we may need to cast
      // it to i32.
      if (Arg2->getType() != Int32Ty)
        Arg2 = Builder.CreateZExtOrTrunc(Arg2, Int32Ty);
      return RValue::get(Builder.CreateCall(
          CGM.CreateRuntimeFunction(FTy, Name),
          {Arg0, Arg1, Arg2, BCast, PacketSize, PacketAlign}));
    }
  }
  // OpenCL v2.0 s6.13.16 ,s9.17.3.5 - Built-in pipe reserve read and write
  // functions
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_write_pipe: {
    // Composing the mangled name for the function.
    const char *Name;
    if (BuiltinID == Builtin::BIreserve_read_pipe)
      Name = "__reserve_read_pipe";
    else if (BuiltinID == Builtin::BIreserve_write_pipe)
      Name = "__reserve_write_pipe";
    else if (BuiltinID == Builtin::BIwork_group_reserve_read_pipe)
      Name = "__work_group_reserve_read_pipe";
    else if (BuiltinID == Builtin::BIwork_group_reserve_write_pipe)
      Name = "__work_group_reserve_write_pipe";
    else if (BuiltinID == Builtin::BIsub_group_reserve_read_pipe)
      Name = "__sub_group_reserve_read_pipe";
    else
      Name = "__sub_group_reserve_write_pipe";

    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *ReservedIDTy = ConvertType(getContext().OCLReserveIDTy);
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Building the generic function prototype.
    llvm::Type *ArgTys[] = {Arg0->getType(), Int32Ty, Int32Ty, Int32Ty};
    llvm::FunctionType *FTy = llvm::FunctionType::get(
        ReservedIDTy, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
    // We know the second argument is an integer type, but we may need to cast
    // it to i32.
    if (Arg1->getType() != Int32Ty)
      Arg1 = Builder.CreateZExtOrTrunc(Arg1, Int32Ty);
    return RValue::get(
        Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                           {Arg0, Arg1, PacketSize, PacketAlign}));
  }
  // OpenCL v2.0 s6.13.16, s9.17.3.5 - Built-in pipe commit read and write
  // functions
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
  case Builtin::BIsub_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_write_pipe: {
    const char *Name;
    if (BuiltinID == Builtin::BIcommit_read_pipe)
      Name = "__commit_read_pipe";
    else if (BuiltinID == Builtin::BIcommit_write_pipe)
      Name = "__commit_write_pipe";
    else if (BuiltinID == Builtin::BIwork_group_commit_read_pipe)
      Name = "__work_group_commit_read_pipe";
    else if (BuiltinID == Builtin::BIwork_group_commit_write_pipe)
      Name = "__work_group_commit_write_pipe";
    else if (BuiltinID == Builtin::BIsub_group_commit_read_pipe)
      Name = "__sub_group_commit_read_pipe";
    else
      Name = "__sub_group_commit_write_pipe";

    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Building the generic function prototype.
    llvm::Type *ArgTys[] = {Arg0->getType(), Arg1->getType(), Int32Ty, Int32Ty};
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(llvm::Type::getVoidTy(getLLVMContext()),
                                llvm::ArrayRef<llvm::Type *>(ArgTys), false);

    return RValue::get(
        Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                           {Arg0, Arg1, PacketSize, PacketAlign}));
  }
  // OpenCL v2.0 s6.13.16.4 Built-in pipe query functions
  case Builtin::BIget_pipe_num_packets:
  case Builtin::BIget_pipe_max_packets: {
    const char *BaseName;
    const PipeType *PipeTy = E->getArg(0)->getType()->getAs<PipeType>();
    if (BuiltinID == Builtin::BIget_pipe_num_packets)
      BaseName = "__get_pipe_num_packets";
    else
      BaseName = "__get_pipe_max_packets";
    auto Name = std::string(BaseName) +
                std::string(PipeTy->isReadOnly() ? "_ro" : "_wo");

    // Building the generic function prototype.
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));
    llvm::Type *ArgTys[] = {Arg0->getType(), Int32Ty, Int32Ty};
    llvm::FunctionType *FTy = llvm::FunctionType::get(
        Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);

    return RValue::get(Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                                          {Arg0, PacketSize, PacketAlign}));
  }

  // OpenCL v2.0 s6.13.9 - Address space qualifier functions.
  case Builtin::BIto_global:
  case Builtin::BIto_local:
  case Builtin::BIto_private: {
    auto Arg0 = EmitScalarExpr(E->getArg(0));
    auto NewArgT = llvm::PointerType::get(Int8Ty,
      CGM.getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto NewRetT = llvm::PointerType::get(Int8Ty,
      CGM.getContext().getTargetAddressSpace(
        E->getType()->getPointeeType().getAddressSpace()));
    auto FTy = llvm::FunctionType::get(NewRetT, {NewArgT}, false);
    llvm::Value *NewArg;
    if (Arg0->getType()->getPointerAddressSpace() !=
        NewArgT->getPointerAddressSpace())
      NewArg = Builder.CreateAddrSpaceCast(Arg0, NewArgT);
    else
      NewArg = Builder.CreateBitOrPointerCast(Arg0, NewArgT);
    auto NewName = std::string("__") + E->getDirectCallee()->getName().str();
    auto NewCall =
        Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, NewName), {NewArg});
    return RValue::get(Builder.CreateBitOrPointerCast(NewCall,
      ConvertType(E->getType())));
  }

  // OpenCL v2.0, s6.13.17 - Enqueue kernel function.
  // It contains four different overload formats specified in Table 6.13.17.1.
  case Builtin::BIenqueue_kernel: {
    StringRef Name; // Generated function call name
    unsigned NumArgs = E->getNumArgs();

    llvm::Type *QueueTy = ConvertType(getContext().OCLQueueTy);
    llvm::Type *GenericVoidPtrTy = Builder.getInt8PtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));

    llvm::Value *Queue = EmitScalarExpr(E->getArg(0));
    llvm::Value *Flags = EmitScalarExpr(E->getArg(1));
    LValue NDRangeL = EmitAggExprToLValue(E->getArg(2));
    llvm::Value *Range = NDRangeL.getAddress().getPointer();
    llvm::Type *RangeTy = NDRangeL.getAddress().getType();

    if (NumArgs == 4) {
      // The most basic form of the call with parameters:
      // queue_t, kernel_enqueue_flags_t, ndrange_t, block(void)
      Name = "__enqueue_kernel_basic";
      llvm::Type *ArgTys[] = {QueueTy, Int32Ty, RangeTy, GenericVoidPtrTy,
                              GenericVoidPtrTy};
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);

      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(3));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
      llvm::Value *Block =
          Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);

      AttrBuilder B;
      B.addAttribute(Attribute::ByVal);
      llvm::AttributeList ByValAttrSet =
          llvm::AttributeList::get(CGM.getModule().getContext(), 3U, B);

      auto RTCall =
          Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name, ByValAttrSet),
                             {Queue, Flags, Range, Kernel, Block});
      RTCall->setAttributes(ByValAttrSet);
      return RValue::get(RTCall);
    }
    assert(NumArgs >= 5 && "Invalid enqueue_kernel signature");

    // Create a temporary array to hold the sizes of local pointer arguments
    // for the block. \p First is the position of the first size argument.
    auto CreateArrayForSizeVar = [=](unsigned First)
        -> std::tuple<llvm::Value *, llvm::Value *, llvm::Value *> {
      llvm::APInt ArraySize(32, NumArgs - First);
      QualType SizeArrayTy = getContext().getConstantArrayType(
          getContext().getSizeType(), ArraySize, ArrayType::Normal,
          /*IndexTypeQuals=*/0);
      auto Tmp = CreateMemTemp(SizeArrayTy, "block_sizes");
      llvm::Value *TmpPtr = Tmp.getPointer();
      llvm::Value *TmpSize = EmitLifetimeStart(
          CGM.getDataLayout().getTypeAllocSize(Tmp.getElementType()), TmpPtr);
      llvm::Value *ElemPtr;
      // Each of the following arguments specifies the size of the corresponding
      // argument passed to the enqueued block.
      auto *Zero = llvm::ConstantInt::get(IntTy, 0);
      for (unsigned I = First; I < NumArgs; ++I) {
        auto *Index = llvm::ConstantInt::get(IntTy, I - First);
        auto *GEP = Builder.CreateGEP(TmpPtr, {Zero, Index});
        if (I == First)
          ElemPtr = GEP;
        auto *V =
            Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(I)), SizeTy);
        Builder.CreateAlignedStore(
            V, GEP, CGM.getDataLayout().getPrefTypeAlignment(SizeTy));
      }
      return std::tie(ElemPtr, TmpSize, TmpPtr);
    };

    // Could have events and/or varargs.
    if (E->getArg(3)->getType()->isBlockPointerType()) {
      // No events passed, but has variadic arguments.
      Name = "__enqueue_kernel_varargs";
      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(3));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
      auto *Block = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
      llvm::Value *ElemPtr, *TmpSize, *TmpPtr;
      std::tie(ElemPtr, TmpSize, TmpPtr) = CreateArrayForSizeVar(4);

      // Create a vector of the arguments, as well as a constant value to
      // express to the runtime the number of variadic arguments.
      std::vector<llvm::Value *> Args = {
          Queue,  Flags, Range,
          Kernel, Block, ConstantInt::get(IntTy, NumArgs - 4),
          ElemPtr};
      std::vector<llvm::Type *> ArgTys = {
          QueueTy,          IntTy, RangeTy,           GenericVoidPtrTy,
          GenericVoidPtrTy, IntTy, ElemPtr->getType()};

      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      auto Call =
          RValue::get(Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                                         llvm::ArrayRef<llvm::Value *>(Args)));
      if (TmpSize)
        EmitLifetimeEnd(TmpSize, TmpPtr);
      return Call;
    }
    // Any calls now have event arguments passed.
    if (NumArgs >= 7) {
      llvm::Type *EventTy = ConvertType(getContext().OCLClkEventTy);
      llvm::Type *EventPtrTy = EventTy->getPointerTo(
          CGM.getContext().getTargetAddressSpace(LangAS::opencl_generic));

      llvm::Value *NumEvents =
          Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(3)), Int32Ty);
      llvm::Value *EventList =
          E->getArg(4)->getType()->isArrayType()
              ? EmitArrayToPointerDecay(E->getArg(4)).getPointer()
              : EmitScalarExpr(E->getArg(4));
      llvm::Value *ClkEvent = EmitScalarExpr(E->getArg(5));
      // Convert to generic address space.
      EventList = Builder.CreatePointerCast(EventList, EventPtrTy);
      ClkEvent = ClkEvent->getType()->isIntegerTy()
                   ? Builder.CreateBitOrPointerCast(ClkEvent, EventPtrTy)
                   : Builder.CreatePointerCast(ClkEvent, EventPtrTy);
      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(6));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
      llvm::Value *Block =
          Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);

      std::vector<llvm::Type *> ArgTys = {
          QueueTy,    Int32Ty,    RangeTy,          Int32Ty,
          EventPtrTy, EventPtrTy, GenericVoidPtrTy, GenericVoidPtrTy};

      std::vector<llvm::Value *> Args = {Queue,     Flags,    Range,  NumEvents,
                                         EventList, ClkEvent, Kernel, Block};

      if (NumArgs == 7) {
        // Has events but no variadics.
        Name = "__enqueue_kernel_basic_events";
        llvm::FunctionType *FTy = llvm::FunctionType::get(
            Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
        return RValue::get(
            Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                               llvm::ArrayRef<llvm::Value *>(Args)));
      }
      // Has event info and variadics
      // Pass the number of variadics to the runtime function too.
      Args.push_back(ConstantInt::get(Int32Ty, NumArgs - 7));
      ArgTys.push_back(Int32Ty);
      Name = "__enqueue_kernel_events_varargs";

      llvm::Value *ElemPtr, *TmpSize, *TmpPtr;
      std::tie(ElemPtr, TmpSize, TmpPtr) = CreateArrayForSizeVar(7);
      Args.push_back(ElemPtr);
      ArgTys.push_back(ElemPtr->getType());

      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      auto Call =
          RValue::get(Builder.CreateCall(CGM.CreateRuntimeFunction(FTy, Name),
                                         llvm::ArrayRef<llvm::Value *>(Args)));
      if (TmpSize)
        EmitLifetimeEnd(TmpSize, TmpPtr);
      return Call;
    }
    LLVM_FALLTHROUGH;
  }
  // OpenCL v2.0 s6.13.17.6 - Kernel query functions need bitcast of block
  // parameter.
  case Builtin::BIget_kernel_work_group_size: {
    llvm::Type *GenericVoidPtrTy = Builder.getInt8PtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(0));
    Value *Kernel = Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
    Value *Arg = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    return RValue::get(Builder.CreateCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(IntTy, {GenericVoidPtrTy, GenericVoidPtrTy},
                                    false),
            "__get_kernel_work_group_size_impl"),
        {Kernel, Arg}));
  }
  case Builtin::BIget_kernel_preferred_work_group_size_multiple: {
    llvm::Type *GenericVoidPtrTy = Builder.getInt8PtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(0));
    Value *Kernel = Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
    Value *Arg = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    return RValue::get(Builder.CreateCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(IntTy, {GenericVoidPtrTy, GenericVoidPtrTy},
                                    false),
            "__get_kernel_preferred_work_group_size_multiple_impl"),
        {Kernel, Arg}));
  }
  case Builtin::BIget_kernel_max_sub_group_size_for_ndrange:
  case Builtin::BIget_kernel_sub_group_count_for_ndrange: {
    llvm::Type *GenericVoidPtrTy = Builder.getInt8PtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    LValue NDRangeL = EmitAggExprToLValue(E->getArg(0));
    llvm::Value *NDRange = NDRangeL.getAddress().getPointer();
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(1));
    Value *Kernel = Builder.CreatePointerCast(Info.Kernel, GenericVoidPtrTy);
    Value *Block = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    const char *Name =
        BuiltinID == Builtin::BIget_kernel_max_sub_group_size_for_ndrange
            ? "__get_kernel_max_sub_group_size_for_ndrange_impl"
            : "__get_kernel_sub_group_count_for_ndrange_impl";
    return RValue::get(Builder.CreateCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(
                IntTy, {NDRange->getType(), GenericVoidPtrTy, GenericVoidPtrTy},
                false),
            Name),
        {NDRange, Kernel, Block}));
  }

  case Builtin::BI__builtin_store_half:
  case Builtin::BI__builtin_store_halff: {
    Value *Val = EmitScalarExpr(E->getArg(0));
    Address Address = EmitPointerWithAlignment(E->getArg(1));
    Value *HalfVal = Builder.CreateFPTrunc(Val, Builder.getHalfTy());
    return RValue::get(Builder.CreateStore(HalfVal, Address));
  }
  case Builtin::BI__builtin_load_half: {
    Address Address = EmitPointerWithAlignment(E->getArg(0));
    Value *HalfVal = Builder.CreateLoad(Address);
    return RValue::get(Builder.CreateFPExt(HalfVal, Builder.getDoubleTy()));
  }
  case Builtin::BI__builtin_load_halff: {
    Address Address = EmitPointerWithAlignment(E->getArg(0));
    Value *HalfVal = Builder.CreateLoad(Address);
    return RValue::get(Builder.CreateFPExt(HalfVal, Builder.getFloatTy()));
  }
  case Builtin::BIprintf:
    if (getTarget().getTriple().isNVPTX())
      return EmitNVPTXDevicePrintfCallExpr(E, ReturnValue);
    break;
  case Builtin::BI__builtin_canonicalize:
  case Builtin::BI__builtin_canonicalizef:
  case Builtin::BI__builtin_canonicalizel:
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::canonicalize));

  case Builtin::BI__builtin_thread_pointer: {
    if (!getContext().getTargetInfo().isTLSSupported())
      CGM.ErrorUnsupported(E, "__builtin_thread_pointer");
    // Fall through - it's already mapped to the intrinsic by GCCBuiltin.
    break;
  }
  case Builtin::BI__builtin_os_log_format:
    return emitBuiltinOSLogFormat(*E);

  case Builtin::BI__xray_customevent: {
    if (!ShouldXRayInstrumentFunction())
      return RValue::getIgnored();

    if (!CGM.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Custom))
      return RValue::getIgnored();

    if (const auto *XRayAttr = CurFuncDecl->getAttr<XRayInstrumentAttr>())
      if (XRayAttr->neverXRayInstrument() && !AlwaysEmitXRayCustomEvents())
        return RValue::getIgnored();

    Function *F = CGM.getIntrinsic(Intrinsic::xray_customevent);
    auto FTy = F->getFunctionType();
    auto Arg0 = E->getArg(0);
    auto Arg0Val = EmitScalarExpr(Arg0);
    auto Arg0Ty = Arg0->getType();
    auto PTy0 = FTy->getParamType(0);
    if (PTy0 != Arg0Val->getType()) {
      if (Arg0Ty->isArrayType())
        Arg0Val = EmitArrayToPointerDecay(Arg0).getPointer();
      else
        Arg0Val = Builder.CreatePointerCast(Arg0Val, PTy0);
    }
    auto Arg1 = EmitScalarExpr(E->getArg(1));
    auto PTy1 = FTy->getParamType(1);
    if (PTy1 != Arg1->getType())
      Arg1 = Builder.CreateTruncOrBitCast(Arg1, PTy1);
    return RValue::get(Builder.CreateCall(F, {Arg0Val, Arg1}));
  }

  case Builtin::BI__xray_typedevent: {
    // TODO: There should be a way to always emit events even if the current
    // function is not instrumented. Losing events in a stream can cripple
    // a trace.
    if (!ShouldXRayInstrumentFunction())
      return RValue::getIgnored();

    if (!CGM.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Typed))
      return RValue::getIgnored();

    if (const auto *XRayAttr = CurFuncDecl->getAttr<XRayInstrumentAttr>())
      if (XRayAttr->neverXRayInstrument() && !AlwaysEmitXRayTypedEvents())
        return RValue::getIgnored();

    Function *F = CGM.getIntrinsic(Intrinsic::xray_typedevent);
    auto FTy = F->getFunctionType();
    auto Arg0 = EmitScalarExpr(E->getArg(0));
    auto PTy0 = FTy->getParamType(0);
    if (PTy0 != Arg0->getType())
      Arg0 = Builder.CreateTruncOrBitCast(Arg0, PTy0);
    auto Arg1 = E->getArg(1);
    auto Arg1Val = EmitScalarExpr(Arg1);
    auto Arg1Ty = Arg1->getType();
    auto PTy1 = FTy->getParamType(1);
    if (PTy1 != Arg1Val->getType()) {
      if (Arg1Ty->isArrayType())
        Arg1Val = EmitArrayToPointerDecay(Arg1).getPointer();
      else
        Arg1Val = Builder.CreatePointerCast(Arg1Val, PTy1);
    }
    auto Arg2 = EmitScalarExpr(E->getArg(2));
    auto PTy2 = FTy->getParamType(2);
    if (PTy2 != Arg2->getType())
      Arg2 = Builder.CreateTruncOrBitCast(Arg2, PTy2);
    return RValue::get(Builder.CreateCall(F, {Arg0, Arg1Val, Arg2}));
  }

  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_ms_va_end:
    return RValue::get(
        EmitVAStartEnd(EmitMSVAListRef(E->getArg(0)).getPointer(),
                       BuiltinID == Builtin::BI__builtin_ms_va_start));

  case Builtin::BI__builtin_ms_va_copy: {
    // Lower this manually. We can't reliably determine whether or not any
    // given va_copy() is for a Win64 va_list from the calling convention
    // alone, because it's legal to do this from a System V ABI function.
    // With opaque pointer types, we won't have enough information in LLVM
    // IR to determine this from the argument types, either. Best to do it
    // now, while we have enough information.
    Address DestAddr = EmitMSVAListRef(E->getArg(0));
    Address SrcAddr = EmitMSVAListRef(E->getArg(1));

    llvm::Type *BPP = Int8PtrPtrTy;

    DestAddr = Address(Builder.CreateBitCast(DestAddr.getPointer(), BPP, "cp"),
                       DestAddr.getAlignment());
    SrcAddr = Address(Builder.CreateBitCast(SrcAddr.getPointer(), BPP, "ap"),
                      SrcAddr.getAlignment());

    Value *ArgPtr = Builder.CreateLoad(SrcAddr, "ap.val");
    return RValue::get(Builder.CreateStore(ArgPtr, DestAddr));
  }
  }

  // If this is an alias for a lib function (e.g. __builtin_sin), emit
  // the call using the normal call path, but using the unmangled
  // version of the function name.
  if (getContext().BuiltinInfo.isLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E,
                           CGM.getBuiltinLibFunction(FD, BuiltinID));

  // If this is a predefined lib function (e.g. malloc), emit the call
  // using exactly the normal call path.
  if (getContext().BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E,
                      cast<llvm::Constant>(EmitScalarExpr(E->getCallee())));

  // Check that a call to a target specific builtin has the correct target
  // features.
  // This is down here to avoid non-target specific builtins, however, if
  // generic builtins start to require generic target features then we
  // can move this up to the beginning of the function.
  checkTargetFeatures(E, FD);

  if (unsigned VectorWidth = getContext().BuiltinInfo.getRequiredVectorWidth(BuiltinID))
    LargestVectorWidth = std::max(LargestVectorWidth, VectorWidth);

  // See if we have a target specific intrinsic.
  const char *Name = getContext().BuiltinInfo.getName(BuiltinID);
  Intrinsic::ID IntrinsicID = Intrinsic::not_intrinsic;
  StringRef Prefix =
      llvm::Triple::getArchTypePrefix(getTarget().getTriple().getArch());
  if (!Prefix.empty()) {
    IntrinsicID = Intrinsic::getIntrinsicForGCCBuiltin(Prefix.data(), Name);
    // NOTE we don't need to perform a compatibility flag check here since the
    // intrinsics are declared in Builtins*.def via LANGBUILTIN which filter the
    // MS builtins via ALL_MS_LANGUAGES and are filtered earlier.
    if (IntrinsicID == Intrinsic::not_intrinsic)
      IntrinsicID = Intrinsic::getIntrinsicForMSBuiltin(Prefix.data(), Name);
  }

  if (IntrinsicID != Intrinsic::not_intrinsic) {
    SmallVector<Value*, 16> Args;

    // Find out if any arguments are required to be integer constant
    // expressions.
    unsigned ICEArguments = 0;
    ASTContext::GetBuiltinTypeError Error;
    getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
    assert(Error == ASTContext::GE_None && "Should not codegen an error");

    Function *F = CGM.getIntrinsic(IntrinsicID);
    llvm::FunctionType *FTy = F->getFunctionType();

    for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
      Value *ArgValue;
      // If this is a normal argument, just emit it as a scalar.
      if ((ICEArguments & (1 << i)) == 0) {
        ArgValue = EmitScalarExpr(E->getArg(i));
      } else {
        // If this is required to be a constant, constant fold it so that we
        // know that the generated intrinsic gets a ConstantInt.
        llvm::APSInt Result;
        bool IsConst = E->getArg(i)->isIntegerConstantExpr(Result,getContext());
        assert(IsConst && "Constant arg isn't actually constant?");
        (void)IsConst;
        ArgValue = llvm::ConstantInt::get(getLLVMContext(), Result);
      }

      // If the intrinsic arg type is different from the builtin arg type
      // we need to do a bit cast.
      llvm::Type *PTy = FTy->getParamType(i);
      if (PTy != ArgValue->getType()) {
        // XXX - vector of pointers?
        if (auto *PtrTy = dyn_cast<llvm::PointerType>(PTy)) {
          if (PtrTy->getAddressSpace() !=
              ArgValue->getType()->getPointerAddressSpace()) {
            ArgValue = Builder.CreateAddrSpaceCast(
              ArgValue,
              ArgValue->getType()->getPointerTo(PtrTy->getAddressSpace()));
          }
        }

        assert(PTy->canLosslesslyBitCastTo(FTy->getParamType(i)) &&
               "Must be able to losslessly bit cast to param");
        ArgValue = Builder.CreateBitCast(ArgValue, PTy);
      }

      Args.push_back(ArgValue);
    }

    Value *V = Builder.CreateCall(F, Args);
    QualType BuiltinRetType = E->getType();

    llvm::Type *RetTy = VoidTy;
    if (!BuiltinRetType->isVoidType())
      RetTy = ConvertType(BuiltinRetType);

    if (RetTy != V->getType()) {
      // XXX - vector of pointers?
      if (auto *PtrTy = dyn_cast<llvm::PointerType>(RetTy)) {
        if (PtrTy->getAddressSpace() != V->getType()->getPointerAddressSpace()) {
          V = Builder.CreateAddrSpaceCast(
            V, V->getType()->getPointerTo(PtrTy->getAddressSpace()));
        }
      }

      assert(V->getType()->canLosslesslyBitCastTo(RetTy) &&
             "Must be able to losslessly bit cast result type");
      V = Builder.CreateBitCast(V, RetTy);
    }

    return RValue::get(V);
  }

  // See if we have a target specific builtin that needs to be lowered.
  if (Value *V = EmitTargetBuiltinExpr(BuiltinID, E))
    return RValue::get(V);

  ErrorUnsupported(E, "builtin function");

  // Unknown builtin, for now just dump it out and return undef.
  return GetUndefRValue(E->getType());
}

static Value *EmitTargetArchBuiltinExpr(CodeGenFunction *CGF,
                                        unsigned BuiltinID, const CallExpr *E,
                                        llvm::Triple::ArchType Arch) {
  switch (Arch) {
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return CGF->EmitARMBuiltinExpr(BuiltinID, E, Arch);
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
    return CGF->EmitAArch64BuiltinExpr(BuiltinID, E, Arch);
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return CGF->EmitX86BuiltinExpr(BuiltinID, E);
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    return CGF->EmitPPCBuiltinExpr(BuiltinID, E);
  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    return CGF->EmitAMDGPUBuiltinExpr(BuiltinID, E);
  case llvm::Triple::systemz:
    return CGF->EmitSystemZBuiltinExpr(BuiltinID, E);
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return CGF->EmitNVPTXBuiltinExpr(BuiltinID, E);
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return CGF->EmitWebAssemblyBuiltinExpr(BuiltinID, E);
  case llvm::Triple::hexagon:
    return CGF->EmitHexagonBuiltinExpr(BuiltinID, E);
  default:
    return nullptr;
  }
}

Value *CodeGenFunction::EmitTargetBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  if (getContext().BuiltinInfo.isAuxBuiltinID(BuiltinID)) {
    assert(getContext().getAuxTargetInfo() && "Missing aux target info");
    return EmitTargetArchBuiltinExpr(
        this, getContext().BuiltinInfo.getAuxBuiltinID(BuiltinID), E,
        getContext().getAuxTargetInfo()->getTriple().getArch());
  }

  return EmitTargetArchBuiltinExpr(this, BuiltinID, E,
                                   getTarget().getTriple().getArch());
}

static llvm::VectorType *GetNeonType(CodeGenFunction *CGF,
                                     NeonTypeFlags TypeFlags,
                                     bool HasLegalHalfType=true,
                                     bool V1Ty=false) {
  int IsQuad = TypeFlags.isQuad();
  switch (TypeFlags.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return llvm::VectorType::get(CGF->Int8Ty, V1Ty ? 1 : (8 << IsQuad));
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return llvm::VectorType::get(CGF->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Float16:
    if (HasLegalHalfType)
      return llvm::VectorType::get(CGF->HalfTy, V1Ty ? 1 : (4 << IsQuad));
    else
      return llvm::VectorType::get(CGF->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::VectorType::get(CGF->Int32Ty, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return llvm::VectorType::get(CGF->Int64Ty, V1Ty ? 1 : (1 << IsQuad));
  case NeonTypeFlags::Poly128:
    // FIXME: i128 and f128 doesn't get fully support in Clang and llvm.
    // There is a lot of i128 and f128 API missing.
    // so we use v16i8 to represent poly128 and get pattern matched.
    return llvm::VectorType::get(CGF->Int8Ty, 16);
  case NeonTypeFlags::Float32:
    return llvm::VectorType::get(CGF->FloatTy, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Float64:
    return llvm::VectorType::get(CGF->DoubleTy, V1Ty ? 1 : (1 << IsQuad));
  }
  llvm_unreachable("Unknown vector element type!");
}

static llvm::VectorType *GetFloatNeonType(CodeGenFunction *CGF,
                                          NeonTypeFlags IntTypeFlags) {
  int IsQuad = IntTypeFlags.isQuad();
  switch (IntTypeFlags.getEltType()) {
  case NeonTypeFlags::Int16:
    return llvm::VectorType::get(CGF->HalfTy, (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::VectorType::get(CGF->FloatTy, (2 << IsQuad));
  case NeonTypeFlags::Int64:
    return llvm::VectorType::get(CGF->DoubleTy, (1 << IsQuad));
  default:
    llvm_unreachable("Type can't be converted to floating-point!");
  }
}

Value *CodeGenFunction::EmitNeonSplat(Value *V, Constant *C) {
  unsigned nElts = V->getType()->getVectorNumElements();
  Value* SV = llvm::ConstantVector::getSplat(nElts, C);
  return Builder.CreateShuffleVector(V, V, SV, "lane");
}

Value *CodeGenFunction::EmitNeonCall(Function *F, SmallVectorImpl<Value*> &Ops,
                                     const char *name,
                                     unsigned shift, bool rightshift) {
  unsigned j = 0;
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j)
    if (shift > 0 && shift == j)
      Ops[j] = EmitNeonShiftVector(Ops[j], ai->getType(), rightshift);
    else
      Ops[j] = Builder.CreateBitCast(Ops[j], ai->getType(), name);

  return Builder.CreateCall(F, Ops, name);
}

Value *CodeGenFunction::EmitNeonShiftVector(Value *V, llvm::Type *Ty,
                                            bool neg) {
  int SV = cast<ConstantInt>(V)->getSExtValue();
  return ConstantInt::get(Ty, neg ? -SV : SV);
}

// Right-shift a vector by a constant.
Value *CodeGenFunction::EmitNeonRShiftImm(Value *Vec, Value *Shift,
                                          llvm::Type *Ty, bool usgn,
                                          const char *name) {
  llvm::VectorType *VTy = cast<llvm::VectorType>(Ty);

  int ShiftAmt = cast<ConstantInt>(Shift)->getSExtValue();
  int EltSize = VTy->getScalarSizeInBits();

  Vec = Builder.CreateBitCast(Vec, Ty);

  // lshr/ashr are undefined when the shift amount is equal to the vector
  // element size.
  if (ShiftAmt == EltSize) {
    if (usgn) {
      // Right-shifting an unsigned value by its size yields 0.
      return llvm::ConstantAggregateZero::get(VTy);
    } else {
      // Right-shifting a signed value by its size is equivalent
      // to a shift of size-1.
      --ShiftAmt;
      Shift = ConstantInt::get(VTy->getElementType(), ShiftAmt);
    }
  }

  Shift = EmitNeonShiftVector(Shift, Ty, false);
  if (usgn)
    return Builder.CreateLShr(Vec, Shift, name);
  else
    return Builder.CreateAShr(Vec, Shift, name);
}

enum {
  AddRetType = (1 << 0),
  Add1ArgType = (1 << 1),
  Add2ArgTypes = (1 << 2),

  VectorizeRetType = (1 << 3),
  VectorizeArgTypes = (1 << 4),

  InventFloatType = (1 << 5),
  UnsignedAlts = (1 << 6),

  Use64BitVectors = (1 << 7),
  Use128BitVectors = (1 << 8),

  Vectorize1ArgType = Add1ArgType | VectorizeArgTypes,
  VectorRet = AddRetType | VectorizeRetType,
  VectorRetGetArgs01 =
      AddRetType | Add2ArgTypes | VectorizeRetType | VectorizeArgTypes,
  FpCmpzModifiers =
      AddRetType | VectorizeRetType | Add1ArgType | InventFloatType
};

namespace {
struct NeonIntrinsicInfo {
  const char *NameHint;
  unsigned BuiltinID;
  unsigned LLVMIntrinsic;
  unsigned AltLLVMIntrinsic;
  unsigned TypeModifier;

  bool operator<(unsigned RHSBuiltinID) const {
    return BuiltinID < RHSBuiltinID;
  }
  bool operator<(const NeonIntrinsicInfo &TE) const {
    return BuiltinID < TE.BuiltinID;
  }
};
} // end anonymous namespace

#define NEONMAP0(NameBase) \
  { #NameBase, NEON::BI__builtin_neon_ ## NameBase, 0, 0, 0 }

#define NEONMAP1(NameBase, LLVMIntrinsic, TypeModifier) \
  { #NameBase, NEON:: BI__builtin_neon_ ## NameBase, \
      Intrinsic::LLVMIntrinsic, 0, TypeModifier }

#define NEONMAP2(NameBase, LLVMIntrinsic, AltLLVMIntrinsic, TypeModifier) \
  { #NameBase, NEON:: BI__builtin_neon_ ## NameBase, \
      Intrinsic::LLVMIntrinsic, Intrinsic::AltLLVMIntrinsic, \
      TypeModifier }

static const NeonIntrinsicInfo ARMSIMDIntrinsicMap [] = {
  NEONMAP2(vabd_v, arm_neon_vabdu, arm_neon_vabds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vabdq_v, arm_neon_vabdu, arm_neon_vabds, Add1ArgType | UnsignedAlts),
  NEONMAP1(vabs_v, arm_neon_vabs, 0),
  NEONMAP1(vabsq_v, arm_neon_vabs, 0),
  NEONMAP0(vaddhn_v),
  NEONMAP1(vaesdq_v, arm_neon_aesd, 0),
  NEONMAP1(vaeseq_v, arm_neon_aese, 0),
  NEONMAP1(vaesimcq_v, arm_neon_aesimc, 0),
  NEONMAP1(vaesmcq_v, arm_neon_aesmc, 0),
  NEONMAP1(vbsl_v, arm_neon_vbsl, AddRetType),
  NEONMAP1(vbslq_v, arm_neon_vbsl, AddRetType),
  NEONMAP1(vcage_v, arm_neon_vacge, 0),
  NEONMAP1(vcageq_v, arm_neon_vacge, 0),
  NEONMAP1(vcagt_v, arm_neon_vacgt, 0),
  NEONMAP1(vcagtq_v, arm_neon_vacgt, 0),
  NEONMAP1(vcale_v, arm_neon_vacge, 0),
  NEONMAP1(vcaleq_v, arm_neon_vacge, 0),
  NEONMAP1(vcalt_v, arm_neon_vacgt, 0),
  NEONMAP1(vcaltq_v, arm_neon_vacgt, 0),
  NEONMAP0(vceqz_v),
  NEONMAP0(vceqzq_v),
  NEONMAP0(vcgez_v),
  NEONMAP0(vcgezq_v),
  NEONMAP0(vcgtz_v),
  NEONMAP0(vcgtzq_v),
  NEONMAP0(vclez_v),
  NEONMAP0(vclezq_v),
  NEONMAP1(vcls_v, arm_neon_vcls, Add1ArgType),
  NEONMAP1(vclsq_v, arm_neon_vcls, Add1ArgType),
  NEONMAP0(vcltz_v),
  NEONMAP0(vcltzq_v),
  NEONMAP1(vclz_v, ctlz, Add1ArgType),
  NEONMAP1(vclzq_v, ctlz, Add1ArgType),
  NEONMAP1(vcnt_v, ctpop, Add1ArgType),
  NEONMAP1(vcntq_v, ctpop, Add1ArgType),
  NEONMAP1(vcvt_f16_f32, arm_neon_vcvtfp2hf, 0),
  NEONMAP0(vcvt_f16_v),
  NEONMAP1(vcvt_f32_f16, arm_neon_vcvthf2fp, 0),
  NEONMAP0(vcvt_f32_v),
  NEONMAP2(vcvt_n_f16_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvt_n_f32_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_s16_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s32_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s64_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_u16_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u32_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u64_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvt_s16_v),
  NEONMAP0(vcvt_s32_v),
  NEONMAP0(vcvt_s64_v),
  NEONMAP0(vcvt_u16_v),
  NEONMAP0(vcvt_u32_v),
  NEONMAP0(vcvt_u64_v),
  NEONMAP1(vcvta_s16_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_s32_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_s64_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_u16_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvta_u32_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvta_u64_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_s16_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_s32_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_s64_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_u16_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_u32_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_u64_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtm_s16_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_s32_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_s64_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_u16_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtm_u32_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtm_u64_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_s16_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_s32_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_s64_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_u16_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_u32_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_u64_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtn_s16_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_s32_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_s64_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_u16_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtn_u32_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtn_u64_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_s16_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_s32_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_s64_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_u16_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_u32_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_u64_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtp_s16_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_s32_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_s64_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_u16_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtp_u32_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtp_u64_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_s16_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_s32_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_s64_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_u16_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_u32_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_u64_v, arm_neon_vcvtpu, 0),
  NEONMAP0(vcvtq_f16_v),
  NEONMAP0(vcvtq_f32_v),
  NEONMAP2(vcvtq_n_f16_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvtq_n_f32_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_s16_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s32_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s64_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_u16_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u32_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u64_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvtq_s16_v),
  NEONMAP0(vcvtq_s32_v),
  NEONMAP0(vcvtq_s64_v),
  NEONMAP0(vcvtq_u16_v),
  NEONMAP0(vcvtq_u32_v),
  NEONMAP0(vcvtq_u64_v),
  NEONMAP2(vdot_v, arm_neon_udot, arm_neon_sdot, 0),
  NEONMAP2(vdotq_v, arm_neon_udot, arm_neon_sdot, 0),
  NEONMAP0(vext_v),
  NEONMAP0(vextq_v),
  NEONMAP0(vfma_v),
  NEONMAP0(vfmaq_v),
  NEONMAP2(vhadd_v, arm_neon_vhaddu, arm_neon_vhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhaddq_v, arm_neon_vhaddu, arm_neon_vhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsub_v, arm_neon_vhsubu, arm_neon_vhsubs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsubq_v, arm_neon_vhsubu, arm_neon_vhsubs, Add1ArgType | UnsignedAlts),
  NEONMAP0(vld1_dup_v),
  NEONMAP1(vld1_v, arm_neon_vld1, 0),
  NEONMAP1(vld1_x2_v, arm_neon_vld1x2, 0),
  NEONMAP1(vld1_x3_v, arm_neon_vld1x3, 0),
  NEONMAP1(vld1_x4_v, arm_neon_vld1x4, 0),
  NEONMAP0(vld1q_dup_v),
  NEONMAP1(vld1q_v, arm_neon_vld1, 0),
  NEONMAP1(vld1q_x2_v, arm_neon_vld1x2, 0),
  NEONMAP1(vld1q_x3_v, arm_neon_vld1x3, 0),
  NEONMAP1(vld1q_x4_v, arm_neon_vld1x4, 0),
  NEONMAP1(vld2_dup_v, arm_neon_vld2dup, 0),
  NEONMAP1(vld2_lane_v, arm_neon_vld2lane, 0),
  NEONMAP1(vld2_v, arm_neon_vld2, 0),
  NEONMAP1(vld2q_dup_v, arm_neon_vld2dup, 0),
  NEONMAP1(vld2q_lane_v, arm_neon_vld2lane, 0),
  NEONMAP1(vld2q_v, arm_neon_vld2, 0),
  NEONMAP1(vld3_dup_v, arm_neon_vld3dup, 0),
  NEONMAP1(vld3_lane_v, arm_neon_vld3lane, 0),
  NEONMAP1(vld3_v, arm_neon_vld3, 0),
  NEONMAP1(vld3q_dup_v, arm_neon_vld3dup, 0),
  NEONMAP1(vld3q_lane_v, arm_neon_vld3lane, 0),
  NEONMAP1(vld3q_v, arm_neon_vld3, 0),
  NEONMAP1(vld4_dup_v, arm_neon_vld4dup, 0),
  NEONMAP1(vld4_lane_v, arm_neon_vld4lane, 0),
  NEONMAP1(vld4_v, arm_neon_vld4, 0),
  NEONMAP1(vld4q_dup_v, arm_neon_vld4dup, 0),
  NEONMAP1(vld4q_lane_v, arm_neon_vld4lane, 0),
  NEONMAP1(vld4q_v, arm_neon_vld4, 0),
  NEONMAP2(vmax_v, arm_neon_vmaxu, arm_neon_vmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP1(vmaxnm_v, arm_neon_vmaxnm, Add1ArgType),
  NEONMAP1(vmaxnmq_v, arm_neon_vmaxnm, Add1ArgType),
  NEONMAP2(vmaxq_v, arm_neon_vmaxu, arm_neon_vmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vmin_v, arm_neon_vminu, arm_neon_vmins, Add1ArgType | UnsignedAlts),
  NEONMAP1(vminnm_v, arm_neon_vminnm, Add1ArgType),
  NEONMAP1(vminnmq_v, arm_neon_vminnm, Add1ArgType),
  NEONMAP2(vminq_v, arm_neon_vminu, arm_neon_vmins, Add1ArgType | UnsignedAlts),
  NEONMAP0(vmovl_v),
  NEONMAP0(vmovn_v),
  NEONMAP1(vmul_v, arm_neon_vmulp, Add1ArgType),
  NEONMAP0(vmull_v),
  NEONMAP1(vmulq_v, arm_neon_vmulp, Add1ArgType),
  NEONMAP2(vpadal_v, arm_neon_vpadalu, arm_neon_vpadals, UnsignedAlts),
  NEONMAP2(vpadalq_v, arm_neon_vpadalu, arm_neon_vpadals, UnsignedAlts),
  NEONMAP1(vpadd_v, arm_neon_vpadd, Add1ArgType),
  NEONMAP2(vpaddl_v, arm_neon_vpaddlu, arm_neon_vpaddls, UnsignedAlts),
  NEONMAP2(vpaddlq_v, arm_neon_vpaddlu, arm_neon_vpaddls, UnsignedAlts),
  NEONMAP1(vpaddq_v, arm_neon_vpadd, Add1ArgType),
  NEONMAP2(vpmax_v, arm_neon_vpmaxu, arm_neon_vpmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vpmin_v, arm_neon_vpminu, arm_neon_vpmins, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqabs_v, arm_neon_vqabs, Add1ArgType),
  NEONMAP1(vqabsq_v, arm_neon_vqabs, Add1ArgType),
  NEONMAP2(vqadd_v, arm_neon_vqaddu, arm_neon_vqadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqaddq_v, arm_neon_vqaddu, arm_neon_vqadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqdmlal_v, arm_neon_vqdmull, arm_neon_vqadds, 0),
  NEONMAP2(vqdmlsl_v, arm_neon_vqdmull, arm_neon_vqsubs, 0),
  NEONMAP1(vqdmulh_v, arm_neon_vqdmulh, Add1ArgType),
  NEONMAP1(vqdmulhq_v, arm_neon_vqdmulh, Add1ArgType),
  NEONMAP1(vqdmull_v, arm_neon_vqdmull, Add1ArgType),
  NEONMAP2(vqmovn_v, arm_neon_vqmovnu, arm_neon_vqmovns, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqmovun_v, arm_neon_vqmovnsu, Add1ArgType),
  NEONMAP1(vqneg_v, arm_neon_vqneg, Add1ArgType),
  NEONMAP1(vqnegq_v, arm_neon_vqneg, Add1ArgType),
  NEONMAP1(vqrdmulh_v, arm_neon_vqrdmulh, Add1ArgType),
  NEONMAP1(vqrdmulhq_v, arm_neon_vqrdmulh, Add1ArgType),
  NEONMAP2(vqrshl_v, arm_neon_vqrshiftu, arm_neon_vqrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqrshlq_v, arm_neon_vqrshiftu, arm_neon_vqrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshl_n_v, arm_neon_vqshiftu, arm_neon_vqshifts, UnsignedAlts),
  NEONMAP2(vqshl_v, arm_neon_vqshiftu, arm_neon_vqshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshlq_n_v, arm_neon_vqshiftu, arm_neon_vqshifts, UnsignedAlts),
  NEONMAP2(vqshlq_v, arm_neon_vqshiftu, arm_neon_vqshifts, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqshlu_n_v, arm_neon_vqshiftsu, 0),
  NEONMAP1(vqshluq_n_v, arm_neon_vqshiftsu, 0),
  NEONMAP2(vqsub_v, arm_neon_vqsubu, arm_neon_vqsubs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqsubq_v, arm_neon_vqsubu, arm_neon_vqsubs, Add1ArgType | UnsignedAlts),
  NEONMAP1(vraddhn_v, arm_neon_vraddhn, Add1ArgType),
  NEONMAP2(vrecpe_v, arm_neon_vrecpe, arm_neon_vrecpe, 0),
  NEONMAP2(vrecpeq_v, arm_neon_vrecpe, arm_neon_vrecpe, 0),
  NEONMAP1(vrecps_v, arm_neon_vrecps, Add1ArgType),
  NEONMAP1(vrecpsq_v, arm_neon_vrecps, Add1ArgType),
  NEONMAP2(vrhadd_v, arm_neon_vrhaddu, arm_neon_vrhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrhaddq_v, arm_neon_vrhaddu, arm_neon_vrhadds, Add1ArgType | UnsignedAlts),
  NEONMAP1(vrnd_v, arm_neon_vrintz, Add1ArgType),
  NEONMAP1(vrnda_v, arm_neon_vrinta, Add1ArgType),
  NEONMAP1(vrndaq_v, arm_neon_vrinta, Add1ArgType),
  NEONMAP0(vrndi_v),
  NEONMAP0(vrndiq_v),
  NEONMAP1(vrndm_v, arm_neon_vrintm, Add1ArgType),
  NEONMAP1(vrndmq_v, arm_neon_vrintm, Add1ArgType),
  NEONMAP1(vrndn_v, arm_neon_vrintn, Add1ArgType),
  NEONMAP1(vrndnq_v, arm_neon_vrintn, Add1ArgType),
  NEONMAP1(vrndp_v, arm_neon_vrintp, Add1ArgType),
  NEONMAP1(vrndpq_v, arm_neon_vrintp, Add1ArgType),
  NEONMAP1(vrndq_v, arm_neon_vrintz, Add1ArgType),
  NEONMAP1(vrndx_v, arm_neon_vrintx, Add1ArgType),
  NEONMAP1(vrndxq_v, arm_neon_vrintx, Add1ArgType),
  NEONMAP2(vrshl_v, arm_neon_vrshiftu, arm_neon_vrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshlq_v, arm_neon_vrshiftu, arm_neon_vrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshr_n_v, arm_neon_vrshiftu, arm_neon_vrshifts, UnsignedAlts),
  NEONMAP2(vrshrq_n_v, arm_neon_vrshiftu, arm_neon_vrshifts, UnsignedAlts),
  NEONMAP2(vrsqrte_v, arm_neon_vrsqrte, arm_neon_vrsqrte, 0),
  NEONMAP2(vrsqrteq_v, arm_neon_vrsqrte, arm_neon_vrsqrte, 0),
  NEONMAP1(vrsqrts_v, arm_neon_vrsqrts, Add1ArgType),
  NEONMAP1(vrsqrtsq_v, arm_neon_vrsqrts, Add1ArgType),
  NEONMAP1(vrsubhn_v, arm_neon_vrsubhn, Add1ArgType),
  NEONMAP1(vsha1su0q_v, arm_neon_sha1su0, 0),
  NEONMAP1(vsha1su1q_v, arm_neon_sha1su1, 0),
  NEONMAP1(vsha256h2q_v, arm_neon_sha256h2, 0),
  NEONMAP1(vsha256hq_v, arm_neon_sha256h, 0),
  NEONMAP1(vsha256su0q_v, arm_neon_sha256su0, 0),
  NEONMAP1(vsha256su1q_v, arm_neon_sha256su1, 0),
  NEONMAP0(vshl_n_v),
  NEONMAP2(vshl_v, arm_neon_vshiftu, arm_neon_vshifts, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshll_n_v),
  NEONMAP0(vshlq_n_v),
  NEONMAP2(vshlq_v, arm_neon_vshiftu, arm_neon_vshifts, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshr_n_v),
  NEONMAP0(vshrn_n_v),
  NEONMAP0(vshrq_n_v),
  NEONMAP1(vst1_v, arm_neon_vst1, 0),
  NEONMAP1(vst1_x2_v, arm_neon_vst1x2, 0),
  NEONMAP1(vst1_x3_v, arm_neon_vst1x3, 0),
  NEONMAP1(vst1_x4_v, arm_neon_vst1x4, 0),
  NEONMAP1(vst1q_v, arm_neon_vst1, 0),
  NEONMAP1(vst1q_x2_v, arm_neon_vst1x2, 0),
  NEONMAP1(vst1q_x3_v, arm_neon_vst1x3, 0),
  NEONMAP1(vst1q_x4_v, arm_neon_vst1x4, 0),
  NEONMAP1(vst2_lane_v, arm_neon_vst2lane, 0),
  NEONMAP1(vst2_v, arm_neon_vst2, 0),
  NEONMAP1(vst2q_lane_v, arm_neon_vst2lane, 0),
  NEONMAP1(vst2q_v, arm_neon_vst2, 0),
  NEONMAP1(vst3_lane_v, arm_neon_vst3lane, 0),
  NEONMAP1(vst3_v, arm_neon_vst3, 0),
  NEONMAP1(vst3q_lane_v, arm_neon_vst3lane, 0),
  NEONMAP1(vst3q_v, arm_neon_vst3, 0),
  NEONMAP1(vst4_lane_v, arm_neon_vst4lane, 0),
  NEONMAP1(vst4_v, arm_neon_vst4, 0),
  NEONMAP1(vst4q_lane_v, arm_neon_vst4lane, 0),
  NEONMAP1(vst4q_v, arm_neon_vst4, 0),
  NEONMAP0(vsubhn_v),
  NEONMAP0(vtrn_v),
  NEONMAP0(vtrnq_v),
  NEONMAP0(vtst_v),
  NEONMAP0(vtstq_v),
  NEONMAP0(vuzp_v),
  NEONMAP0(vuzpq_v),
  NEONMAP0(vzip_v),
  NEONMAP0(vzipq_v)
};

static const NeonIntrinsicInfo AArch64SIMDIntrinsicMap[] = {
  NEONMAP1(vabs_v, aarch64_neon_abs, 0),
  NEONMAP1(vabsq_v, aarch64_neon_abs, 0),
  NEONMAP0(vaddhn_v),
  NEONMAP1(vaesdq_v, aarch64_crypto_aesd, 0),
  NEONMAP1(vaeseq_v, aarch64_crypto_aese, 0),
  NEONMAP1(vaesimcq_v, aarch64_crypto_aesimc, 0),
  NEONMAP1(vaesmcq_v, aarch64_crypto_aesmc, 0),
  NEONMAP1(vcage_v, aarch64_neon_facge, 0),
  NEONMAP1(vcageq_v, aarch64_neon_facge, 0),
  NEONMAP1(vcagt_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcagtq_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcale_v, aarch64_neon_facge, 0),
  NEONMAP1(vcaleq_v, aarch64_neon_facge, 0),
  NEONMAP1(vcalt_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcaltq_v, aarch64_neon_facgt, 0),
  NEONMAP0(vceqz_v),
  NEONMAP0(vceqzq_v),
  NEONMAP0(vcgez_v),
  NEONMAP0(vcgezq_v),
  NEONMAP0(vcgtz_v),
  NEONMAP0(vcgtzq_v),
  NEONMAP0(vclez_v),
  NEONMAP0(vclezq_v),
  NEONMAP1(vcls_v, aarch64_neon_cls, Add1ArgType),
  NEONMAP1(vclsq_v, aarch64_neon_cls, Add1ArgType),
  NEONMAP0(vcltz_v),
  NEONMAP0(vcltzq_v),
  NEONMAP1(vclz_v, ctlz, Add1ArgType),
  NEONMAP1(vclzq_v, ctlz, Add1ArgType),
  NEONMAP1(vcnt_v, ctpop, Add1ArgType),
  NEONMAP1(vcntq_v, ctpop, Add1ArgType),
  NEONMAP1(vcvt_f16_f32, aarch64_neon_vcvtfp2hf, 0),
  NEONMAP0(vcvt_f16_v),
  NEONMAP1(vcvt_f32_f16, aarch64_neon_vcvthf2fp, 0),
  NEONMAP0(vcvt_f32_v),
  NEONMAP2(vcvt_n_f16_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvt_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvt_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_s16_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_u16_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvtq_f16_v),
  NEONMAP0(vcvtq_f32_v),
  NEONMAP2(vcvtq_n_f16_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvtq_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvtq_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_s16_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_u16_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtx_f32_v, aarch64_neon_fcvtxn, AddRetType | Add1ArgType),
  NEONMAP2(vdot_v, aarch64_neon_udot, aarch64_neon_sdot, 0),
  NEONMAP2(vdotq_v, aarch64_neon_udot, aarch64_neon_sdot, 0),
  NEONMAP0(vext_v),
  NEONMAP0(vextq_v),
  NEONMAP0(vfma_v),
  NEONMAP0(vfmaq_v),
  NEONMAP1(vfmlal_high_v, aarch64_neon_fmlal2, 0),
  NEONMAP1(vfmlal_low_v, aarch64_neon_fmlal, 0),
  NEONMAP1(vfmlalq_high_v, aarch64_neon_fmlal2, 0),
  NEONMAP1(vfmlalq_low_v, aarch64_neon_fmlal, 0),
  NEONMAP1(vfmlsl_high_v, aarch64_neon_fmlsl2, 0),
  NEONMAP1(vfmlsl_low_v, aarch64_neon_fmlsl, 0),
  NEONMAP1(vfmlslq_high_v, aarch64_neon_fmlsl2, 0),
  NEONMAP1(vfmlslq_low_v, aarch64_neon_fmlsl, 0),
  NEONMAP2(vhadd_v, aarch64_neon_uhadd, aarch64_neon_shadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhaddq_v, aarch64_neon_uhadd, aarch64_neon_shadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsub_v, aarch64_neon_uhsub, aarch64_neon_shsub, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsubq_v, aarch64_neon_uhsub, aarch64_neon_shsub, Add1ArgType | UnsignedAlts),
  NEONMAP1(vld1_x2_v, aarch64_neon_ld1x2, 0),
  NEONMAP1(vld1_x3_v, aarch64_neon_ld1x3, 0),
  NEONMAP1(vld1_x4_v, aarch64_neon_ld1x4, 0),
  NEONMAP1(vld1q_x2_v, aarch64_neon_ld1x2, 0),
  NEONMAP1(vld1q_x3_v, aarch64_neon_ld1x3, 0),
  NEONMAP1(vld1q_x4_v, aarch64_neon_ld1x4, 0),
  NEONMAP0(vmovl_v),
  NEONMAP0(vmovn_v),
  NEONMAP1(vmul_v, aarch64_neon_pmul, Add1ArgType),
  NEONMAP1(vmulq_v, aarch64_neon_pmul, Add1ArgType),
  NEONMAP1(vpadd_v, aarch64_neon_addp, Add1ArgType),
  NEONMAP2(vpaddl_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
  NEONMAP2(vpaddlq_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
  NEONMAP1(vpaddq_v, aarch64_neon_addp, Add1ArgType),
  NEONMAP1(vqabs_v, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqabsq_v, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP2(vqadd_v, aarch64_neon_uqadd, aarch64_neon_sqadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqaddq_v, aarch64_neon_uqadd, aarch64_neon_sqadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqdmlal_v, aarch64_neon_sqdmull, aarch64_neon_sqadd, 0),
  NEONMAP2(vqdmlsl_v, aarch64_neon_sqdmull, aarch64_neon_sqsub, 0),
  NEONMAP1(vqdmulh_v, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmulhq_v, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmull_v, aarch64_neon_sqdmull, Add1ArgType),
  NEONMAP2(vqmovn_v, aarch64_neon_uqxtn, aarch64_neon_sqxtn, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqmovun_v, aarch64_neon_sqxtun, Add1ArgType),
  NEONMAP1(vqneg_v, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqnegq_v, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqrdmulh_v, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP1(vqrdmulhq_v, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP2(vqrshl_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqrshlq_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshl_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
  NEONMAP2(vqshl_v, aarch64_neon_uqshl, aarch64_neon_sqshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshlq_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl,UnsignedAlts),
  NEONMAP2(vqshlq_v, aarch64_neon_uqshl, aarch64_neon_sqshl, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqshlu_n_v, aarch64_neon_sqshlu, 0),
  NEONMAP1(vqshluq_n_v, aarch64_neon_sqshlu, 0),
  NEONMAP2(vqsub_v, aarch64_neon_uqsub, aarch64_neon_sqsub, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqsubq_v, aarch64_neon_uqsub, aarch64_neon_sqsub, Add1ArgType | UnsignedAlts),
  NEONMAP1(vraddhn_v, aarch64_neon_raddhn, Add1ArgType),
  NEONMAP2(vrecpe_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
  NEONMAP2(vrecpeq_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
  NEONMAP1(vrecps_v, aarch64_neon_frecps, Add1ArgType),
  NEONMAP1(vrecpsq_v, aarch64_neon_frecps, Add1ArgType),
  NEONMAP2(vrhadd_v, aarch64_neon_urhadd, aarch64_neon_srhadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrhaddq_v, aarch64_neon_urhadd, aarch64_neon_srhadd, Add1ArgType | UnsignedAlts),
  NEONMAP0(vrndi_v),
  NEONMAP0(vrndiq_v),
  NEONMAP2(vrshl_v, aarch64_neon_urshl, aarch64_neon_srshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshlq_v, aarch64_neon_urshl, aarch64_neon_srshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshr_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
  NEONMAP2(vrshrq_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
  NEONMAP2(vrsqrte_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
  NEONMAP2(vrsqrteq_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
  NEONMAP1(vrsqrts_v, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsqrtsq_v, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsubhn_v, aarch64_neon_rsubhn, Add1ArgType),
  NEONMAP1(vsha1su0q_v, aarch64_crypto_sha1su0, 0),
  NEONMAP1(vsha1su1q_v, aarch64_crypto_sha1su1, 0),
  NEONMAP1(vsha256h2q_v, aarch64_crypto_sha256h2, 0),
  NEONMAP1(vsha256hq_v, aarch64_crypto_sha256h, 0),
  NEONMAP1(vsha256su0q_v, aarch64_crypto_sha256su0, 0),
  NEONMAP1(vsha256su1q_v, aarch64_crypto_sha256su1, 0),
  NEONMAP0(vshl_n_v),
  NEONMAP2(vshl_v, aarch64_neon_ushl, aarch64_neon_sshl, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshll_n_v),
  NEONMAP0(vshlq_n_v),
  NEONMAP2(vshlq_v, aarch64_neon_ushl, aarch64_neon_sshl, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshr_n_v),
  NEONMAP0(vshrn_n_v),
  NEONMAP0(vshrq_n_v),
  NEONMAP1(vst1_x2_v, aarch64_neon_st1x2, 0),
  NEONMAP1(vst1_x3_v, aarch64_neon_st1x3, 0),
  NEONMAP1(vst1_x4_v, aarch64_neon_st1x4, 0),
  NEONMAP1(vst1q_x2_v, aarch64_neon_st1x2, 0),
  NEONMAP1(vst1q_x3_v, aarch64_neon_st1x3, 0),
  NEONMAP1(vst1q_x4_v, aarch64_neon_st1x4, 0),
  NEONMAP0(vsubhn_v),
  NEONMAP0(vtst_v),
  NEONMAP0(vtstq_v),
};

static const NeonIntrinsicInfo AArch64SISDIntrinsicMap[] = {
  NEONMAP1(vabdd_f64, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vabds_f32, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vabsd_s64, aarch64_neon_abs, Add1ArgType),
  NEONMAP1(vaddlv_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlv_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlvq_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlvq_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_f64, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_s64, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vcaged_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcages_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcagtd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcagts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcaled_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcales_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcaltd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcalts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcvtad_s64_f64, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtad_u64_f64, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtas_s32_f32, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtas_u32_f32, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_f64_s64, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_f64_u64, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_s64_f64, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_u64_f64, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmd_s64_f64, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmd_u64_f64, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtms_s32_f32, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtms_u32_f32, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnd_s64_f64, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnd_u64_f64, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtns_s32_f32, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtns_u32_f32, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtpd_s64_f64, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtpd_u64_f64, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtps_s32_f32, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtps_u32_f32, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_f32_s32, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_f32_u32, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_s32_f32, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_u32_f32, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtxd_f32_f64, aarch64_sisd_fcvtxn, 0),
  NEONMAP1(vmaxnmv_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxnmvq_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxnmvq_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmv_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmvq_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmvq_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
  NEONMAP1(vmull_p64, aarch64_neon_pmull64, 0),
  NEONMAP1(vmulxd_f64, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vmulxs_f32, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vpaddd_s64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vpaddd_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxnmqd_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxnms_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxqd_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxs_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vpminnmqd_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpminnms_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpminqd_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vpmins_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vqabsb_s8, aarch64_neon_sqabs, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqabsd_s64, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqabsh_s16, aarch64_neon_sqabs, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqabss_s32, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqaddb_s8, aarch64_neon_sqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddb_u8, aarch64_neon_uqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddd_s64, aarch64_neon_sqadd, Add1ArgType),
  NEONMAP1(vqaddd_u64, aarch64_neon_uqadd, Add1ArgType),
  NEONMAP1(vqaddh_s16, aarch64_neon_sqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddh_u16, aarch64_neon_uqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqadds_s32, aarch64_neon_sqadd, Add1ArgType),
  NEONMAP1(vqadds_u32, aarch64_neon_uqadd, Add1ArgType),
  NEONMAP1(vqdmulhh_s16, aarch64_neon_sqdmulh, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqdmulhs_s32, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmullh_s16, aarch64_neon_sqdmull, VectorRet | Use128BitVectors),
  NEONMAP1(vqdmulls_s32, aarch64_neon_sqdmulls_scalar, 0),
  NEONMAP1(vqmovnd_s64, aarch64_neon_scalar_sqxtn, AddRetType | Add1ArgType),
  NEONMAP1(vqmovnd_u64, aarch64_neon_scalar_uqxtn, AddRetType | Add1ArgType),
  NEONMAP1(vqmovnh_s16, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovnh_u16, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovns_s32, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovns_u32, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovund_s64, aarch64_neon_scalar_sqxtun, AddRetType | Add1ArgType),
  NEONMAP1(vqmovunh_s16, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovuns_s32, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
  NEONMAP1(vqnegb_s8, aarch64_neon_sqneg, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqnegd_s64, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqnegh_s16, aarch64_neon_sqneg, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqnegs_s32, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqrdmulhh_s16, aarch64_neon_sqrdmulh, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrdmulhs_s32, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP1(vqrshlb_s8, aarch64_neon_sqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshlb_u8, aarch64_neon_uqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshld_s64, aarch64_neon_sqrshl, Add1ArgType),
  NEONMAP1(vqrshld_u64, aarch64_neon_uqrshl, Add1ArgType),
  NEONMAP1(vqrshlh_s16, aarch64_neon_sqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshlh_u16, aarch64_neon_uqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshls_s32, aarch64_neon_sqrshl, Add1ArgType),
  NEONMAP1(vqrshls_u32, aarch64_neon_uqrshl, Add1ArgType),
  NEONMAP1(vqrshrnd_n_s64, aarch64_neon_sqrshrn, AddRetType),
  NEONMAP1(vqrshrnd_n_u64, aarch64_neon_uqrshrn, AddRetType),
  NEONMAP1(vqrshrnh_n_s16, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrnh_n_u16, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrns_n_s32, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrns_n_u32, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrund_n_s64, aarch64_neon_sqrshrun, AddRetType),
  NEONMAP1(vqrshrunh_n_s16, aarch64_neon_sqrshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshruns_n_s32, aarch64_neon_sqrshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqshlb_n_s8, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_n_u8, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_s8, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_u8, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshld_s64, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshld_u64, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshlh_n_s16, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_n_u16, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_s16, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_u16, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshls_n_s32, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshls_n_u32, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshls_s32, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshls_u32, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshlub_n_s8, aarch64_neon_sqshlu, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshluh_n_s16, aarch64_neon_sqshlu, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlus_n_s32, aarch64_neon_sqshlu, Add1ArgType),
  NEONMAP1(vqshrnd_n_s64, aarch64_neon_sqshrn, AddRetType),
  NEONMAP1(vqshrnd_n_u64, aarch64_neon_uqshrn, AddRetType),
  NEONMAP1(vqshrnh_n_s16, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrnh_n_u16, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrns_n_s32, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrns_n_u32, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrund_n_s64, aarch64_neon_sqshrun, AddRetType),
  NEONMAP1(vqshrunh_n_s16, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqshruns_n_s32, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqsubb_s8, aarch64_neon_sqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubb_u8, aarch64_neon_uqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubd_s64, aarch64_neon_sqsub, Add1ArgType),
  NEONMAP1(vqsubd_u64, aarch64_neon_uqsub, Add1ArgType),
  NEONMAP1(vqsubh_s16, aarch64_neon_sqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubh_u16, aarch64_neon_uqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubs_s32, aarch64_neon_sqsub, Add1ArgType),
  NEONMAP1(vqsubs_u32, aarch64_neon_uqsub, Add1ArgType),
  NEONMAP1(vrecped_f64, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpes_f32, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpxd_f64, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrecpxs_f32, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrshld_s64, aarch64_neon_srshl, Add1ArgType),
  NEONMAP1(vrshld_u64, aarch64_neon_urshl, Add1ArgType),
  NEONMAP1(vrsqrted_f64, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtes_f32, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtsd_f64, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsqrtss_f32, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vsha1cq_u32, aarch64_crypto_sha1c, 0),
  NEONMAP1(vsha1h_u32, aarch64_crypto_sha1h, 0),
  NEONMAP1(vsha1mq_u32, aarch64_crypto_sha1m, 0),
  NEONMAP1(vsha1pq_u32, aarch64_crypto_sha1p, 0),
  NEONMAP1(vshld_s64, aarch64_neon_sshl, Add1ArgType),
  NEONMAP1(vshld_u64, aarch64_neon_ushl, Add1ArgType),
  NEONMAP1(vslid_n_s64, aarch64_neon_vsli, Vectorize1ArgType),
  NEONMAP1(vslid_n_u64, aarch64_neon_vsli, Vectorize1ArgType),
  NEONMAP1(vsqaddb_u8, aarch64_neon_usqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vsqaddd_u64, aarch64_neon_usqadd, Add1ArgType),
  NEONMAP1(vsqaddh_u16, aarch64_neon_usqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vsqadds_u32, aarch64_neon_usqadd, Add1ArgType),
  NEONMAP1(vsrid_n_s64, aarch64_neon_vsri, Vectorize1ArgType),
  NEONMAP1(vsrid_n_u64, aarch64_neon_vsri, Vectorize1ArgType),
  NEONMAP1(vuqaddb_s8, aarch64_neon_suqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vuqaddd_s64, aarch64_neon_suqadd, Add1ArgType),
  NEONMAP1(vuqaddh_s16, aarch64_neon_suqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vuqadds_s32, aarch64_neon_suqadd, Add1ArgType),
  // FP16 scalar intrinisics go here.
  NEONMAP1(vabdh_f16, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vcvtah_s32_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_s64_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_u32_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_u64_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_s32, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_s64, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_u32, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_u64, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_s32_f16, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_s64_f16, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_u32_f16, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_u64_f16, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_s32_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_s64_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_u32_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_u64_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_s32_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_s64_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_u32_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_u64_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_s32_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_s64_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_u32_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_u64_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vmulxh_f16, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vrecpeh_f16, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpxh_f16, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrsqrteh_f16, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtsh_f16, aarch64_neon_frsqrts, Add1ArgType),
};

#undef NEONMAP0
#undef NEONMAP1
#undef NEONMAP2

static bool NEONSIMDIntrinsicsProvenSorted = false;

static bool AArch64SIMDIntrinsicsProvenSorted = false;
static bool AArch64SISDIntrinsicsProvenSorted = false;


static const NeonIntrinsicInfo *
findNeonIntrinsicInMap(ArrayRef<NeonIntrinsicInfo> IntrinsicMap,
                       unsigned BuiltinID, bool &MapProvenSorted) {

#ifndef NDEBUG
  if (!MapProvenSorted) {
    assert(std::is_sorted(std::begin(IntrinsicMap), std::end(IntrinsicMap)));
    MapProvenSorted = true;
  }
#endif

  const NeonIntrinsicInfo *Builtin =
      std::lower_bound(IntrinsicMap.begin(), IntrinsicMap.end(), BuiltinID);

  if (Builtin != IntrinsicMap.end() && Builtin->BuiltinID == BuiltinID)
    return Builtin;

  return nullptr;
}

Function *CodeGenFunction::LookupNeonLLVMIntrinsic(unsigned IntrinsicID,
                                                   unsigned Modifier,
                                                   llvm::Type *ArgType,
                                                   const CallExpr *E) {
  int VectorSize = 0;
  if (Modifier & Use64BitVectors)
    VectorSize = 64;
  else if (Modifier & Use128BitVectors)
    VectorSize = 128;

  // Return type.
  SmallVector<llvm::Type *, 3> Tys;
  if (Modifier & AddRetType) {
    llvm::Type *Ty = ConvertType(E->getCallReturnType(getContext()));
    if (Modifier & VectorizeRetType)
      Ty = llvm::VectorType::get(
          Ty, VectorSize ? VectorSize / Ty->getPrimitiveSizeInBits() : 1);

    Tys.push_back(Ty);
  }

  // Arguments.
  if (Modifier & VectorizeArgTypes) {
    int Elts = VectorSize ? VectorSize / ArgType->getPrimitiveSizeInBits() : 1;
    ArgType = llvm::VectorType::get(ArgType, Elts);
  }

  if (Modifier & (Add1ArgType | Add2ArgTypes))
    Tys.push_back(ArgType);

  if (Modifier & Add2ArgTypes)
    Tys.push_back(ArgType);

  if (Modifier & InventFloatType)
    Tys.push_back(FloatTy);

  return CGM.getIntrinsic(IntrinsicID, Tys);
}

static Value *EmitCommonNeonSISDBuiltinExpr(CodeGenFunction &CGF,
                                            const NeonIntrinsicInfo &SISDInfo,
                                            SmallVectorImpl<Value *> &Ops,
                                            const CallExpr *E) {
  unsigned BuiltinID = SISDInfo.BuiltinID;
  unsigned int Int = SISDInfo.LLVMIntrinsic;
  unsigned Modifier = SISDInfo.TypeModifier;
  const char *s = SISDInfo.NameHint;

  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vcled_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcales_f32:
  case NEON::BI__builtin_neon_vcaled_f64:
  case NEON::BI__builtin_neon_vcalts_f32:
  case NEON::BI__builtin_neon_vcaltd_f64:
    // Only one direction of comparisons actually exist, cmle is actually a cmge
    // with swapped operands. The table gives us the right intrinsic but we
    // still need to do the swap.
    std::swap(Ops[0], Ops[1]);
    break;
  }

  assert(Int && "Generic code assumes a valid intrinsic");

  // Determine the type(s) of this overloaded AArch64 intrinsic.
  const Expr *Arg = E->getArg(0);
  llvm::Type *ArgTy = CGF.ConvertType(Arg->getType());
  Function *F = CGF.LookupNeonLLVMIntrinsic(Int, Modifier, ArgTy, E);

  int j = 0;
  ConstantInt *C0 = ConstantInt::get(CGF.SizeTy, 0);
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j) {
    llvm::Type *ArgTy = ai->getType();
    if (Ops[j]->getType()->getPrimitiveSizeInBits() ==
             ArgTy->getPrimitiveSizeInBits())
      continue;

    assert(ArgTy->isVectorTy() && !Ops[j]->getType()->isVectorTy());
    // The constant argument to an _n_ intrinsic always has Int32Ty, so truncate
    // it before inserting.
    Ops[j] =
        CGF.Builder.CreateTruncOrBitCast(Ops[j], ArgTy->getVectorElementType());
    Ops[j] =
        CGF.Builder.CreateInsertElement(UndefValue::get(ArgTy), Ops[j], C0);
  }

  Value *Result = CGF.EmitNeonCall(F, Ops, s);
  llvm::Type *ResultType = CGF.ConvertType(E->getType());
  if (ResultType->getPrimitiveSizeInBits() <
      Result->getType()->getPrimitiveSizeInBits())
    return CGF.Builder.CreateExtractElement(Result, C0);

  return CGF.Builder.CreateBitCast(Result, ResultType, s);
}

Value *CodeGenFunction::EmitCommonNeonBuiltinExpr(
    unsigned BuiltinID, unsigned LLVMIntrinsic, unsigned AltLLVMIntrinsic,
    const char *NameHint, unsigned Modifier, const CallExpr *E,
    SmallVectorImpl<llvm::Value *> &Ops, Address PtrOp0, Address PtrOp1,
    llvm::Triple::ArchType Arch) {
  // Get the last argument, which specifies the vector type.
  llvm::APSInt NeonTypeConst;
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  if (!Arg->isIntegerConstantExpr(NeonTypeConst, getContext()))
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type(NeonTypeConst.getZExtValue());
  bool Usgn = Type.isUnsigned();
  bool Quad = Type.isQuad();
  const bool HasLegalHalfType = getTarget().hasLegalHalfType();

  llvm::VectorType *VTy = GetNeonType(this, Type, HasLegalHalfType);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  auto getAlignmentValue32 = [&](Address addr) -> Value* {
    return Builder.getInt32(addr.getAlignment().getQuantity());
  };

  unsigned Int = LLVMIntrinsic;
  if ((Modifier & UnsignedAlts) && !Usgn)
    Int = AltLLVMIntrinsic;

  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v:
    if (VTy->getElementType()->isFloatingPointTy())
      return EmitNeonCall(CGM.getIntrinsic(Intrinsic::fabs, Ty), Ops, "vabs");
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Ty), Ops, "vabs");
  case NEON::BI__builtin_neon_vaddhn_v: {
    llvm::VectorType *SrcTy =
        llvm::VectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateAdd(Ops[0], Ops[1], "vaddhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vaddhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vaddhn");
  }
  case NEON::BI__builtin_neon_vcale_v:
  case NEON::BI__builtin_neon_vcaleq_v:
  case NEON::BI__builtin_neon_vcalt_v:
  case NEON::BI__builtin_neon_vcaltq_v:
    std::swap(Ops[0], Ops[1]);
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcage_v:
  case NEON::BI__builtin_neon_vcageq_v:
  case NEON::BI__builtin_neon_vcagt_v:
  case NEON::BI__builtin_neon_vcagtq_v: {
    llvm::Type *Ty;
    switch (VTy->getScalarSizeInBits()) {
    default: llvm_unreachable("unexpected type");
    case 32:
      Ty = FloatTy;
      break;
    case 64:
      Ty = DoubleTy;
      break;
    case 16:
      Ty = HalfTy;
      break;
    }
    llvm::Type *VecFlt = llvm::VectorType::get(Ty, VTy->getNumElements());
    llvm::Type *Tys[] = { VTy, VecFlt };
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    return EmitNeonCall(F, Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vceqz_v:
  case NEON::BI__builtin_neon_vceqzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OEQ,
                                         ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgez_v:
  case NEON::BI__builtin_neon_vcgezq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGE,
                                         ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclez_v:
  case NEON::BI__builtin_neon_vclezq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLE,
                                         ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtz_v:
  case NEON::BI__builtin_neon_vcgtzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGT,
                                         ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltz_v:
  case NEON::BI__builtin_neon_vcltzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLT,
                                         ICmpInst::ICMP_SLT, "vcltz");
  case NEON::BI__builtin_neon_vclz_v:
  case NEON::BI__builtin_neon_vclzq_v:
    // We generate target-independent intrinsic, which needs a second argument
    // for whether or not clz of zero is undefined; on ARM it isn't.
    Ops.push_back(Builder.getInt1(getTarget().isCLZForZeroUndef()));
    break;
  case NEON::BI__builtin_neon_vcvt_f32_v:
  case NEON::BI__builtin_neon_vcvtq_f32_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float32, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f16_v:
  case NEON::BI__builtin_neon_vcvtq_f16_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float16, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_n_f16_v:
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
  case NEON::BI__builtin_neon_vcvt_n_f64_v:
  case NEON::BI__builtin_neon_vcvtq_n_f16_v:
  case NEON::BI__builtin_neon_vcvtq_n_f32_v:
  case NEON::BI__builtin_neon_vcvtq_n_f64_v: {
    llvm::Type *Tys[2] = { GetFloatNeonType(this, Type), Ty };
    Int = Usgn ? LLVMIntrinsic : AltLLVMIntrinsic;
    Function *F = CGM.getIntrinsic(Int, Tys);
    return EmitNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_n_s16_v:
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
  case NEON::BI__builtin_neon_vcvt_n_u16_v:
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
  case NEON::BI__builtin_neon_vcvt_n_s64_v:
  case NEON::BI__builtin_neon_vcvt_n_u64_v:
  case NEON::BI__builtin_neon_vcvtq_n_s16_v:
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
  case NEON::BI__builtin_neon_vcvtq_n_u16_v:
  case NEON::BI__builtin_neon_vcvtq_n_u32_v:
  case NEON::BI__builtin_neon_vcvtq_n_s64_v:
  case NEON::BI__builtin_neon_vcvtq_n_u64_v: {
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    return EmitNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_v:
  case NEON::BI__builtin_neon_vcvt_u16_v:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_v:
  case NEON::BI__builtin_neon_vcvtq_u16_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], GetFloatNeonType(this, Type));
    return Usgn ? Builder.CreateFPToUI(Ops[0], Ty, "vcvt")
                : Builder.CreateFPToSI(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvta_s16_v:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvta_u16_v:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_v:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_v:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v:
  case NEON::BI__builtin_neon_vcvtn_s16_v:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u16_v:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_v:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_v:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v:
  case NEON::BI__builtin_neon_vcvtp_s16_v:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u16_v:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_v:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_v:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v:
  case NEON::BI__builtin_neon_vcvtm_s16_v:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u16_v:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_v:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_v:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vext_v:
  case NEON::BI__builtin_neon_vextq_v: {
    int CV = cast<ConstantInt>(Ops[2])->getSExtValue();
    SmallVector<uint32_t, 16> Indices;
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
      Indices.push_back(i+CV);

    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    return Builder.CreateShuffleVector(Ops[0], Ops[1], Indices, "vext");
  }
  case NEON::BI__builtin_neon_vfma_v:
  case NEON::BI__builtin_neon_vfmaq_v: {
    Value *F = CGM.getIntrinsic(Intrinsic::fma, Ty);
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);

    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return Builder.CreateCall(F, {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "vld1");
  }
  case NEON::BI__builtin_neon_vld1_x2_v:
  case NEON::BI__builtin_neon_vld1q_x2_v:
  case NEON::BI__builtin_neon_vld1_x3_v:
  case NEON::BI__builtin_neon_vld1q_x3_v:
  case NEON::BI__builtin_neon_vld1_x4_v:
  case NEON::BI__builtin_neon_vld1q_x4_v: {
    llvm::Type *PTy = llvm::PointerType::getUnqual(VTy->getVectorElementType());
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld1xN");
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v:
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v:
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v:
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v:
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v:
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    Value *Align = getAlignmentValue32(PtrOp1);
    Ops[1] = Builder.CreateCall(F, {Ops[1], Align}, NameHint);
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = UndefValue::get(Ty);
    Ty = llvm::PointerType::getUnqual(VTy->getElementType());
    PtrOp0 = Builder.CreateBitCast(PtrOp0, Ty);
    LoadInst *Ld = Builder.CreateLoad(PtrOp0);
    llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ld, CI);
    return EmitNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v:
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v:
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    for (unsigned I = 2; I < Ops.size() - 1; ++I)
      Ops[I] = Builder.CreateBitCast(Ops[I], Ty);
    Ops.push_back(getAlignmentValue32(PtrOp1));
    Ops[1] = Builder.CreateCall(F, makeArrayRef(Ops).slice(1), NameHint);
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vmovl_v: {
    llvm::Type *DTy =llvm::VectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], DTy);
    if (Usgn)
      return Builder.CreateZExt(Ops[0], Ty, "vmovl");
    return Builder.CreateSExt(Ops[0], Ty, "vmovl");
  }
  case NEON::BI__builtin_neon_vmovn_v: {
    llvm::Type *QTy = llvm::VectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], QTy);
    return Builder.CreateTrunc(Ops[0], Ty, "vmovn");
  }
  case NEON::BI__builtin_neon_vmull_v:
    // FIXME: the integer vmull operations could be emitted in terms of pure
    // LLVM IR (2 exts followed by a mul). Unfortunately LLVM has a habit of
    // hoisting the exts outside loops. Until global ISel comes along that can
    // see through such movement this leads to bad CodeGen. So we need an
    // intrinsic for now.
    Int = Usgn ? Intrinsic::arm_neon_vmullu : Intrinsic::arm_neon_vmulls;
    Int = Type.isPoly() ? (unsigned)Intrinsic::arm_neon_vmullp : Int;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy =
      llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    llvm::Type *NarrowTy =
      llvm::VectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = { Ty, NarrowTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vpaddl_v:
  case NEON::BI__builtin_neon_vpaddlq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy = llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    llvm::Type *NarrowTy =
      llvm::VectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = { Ty, NarrowTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vpaddl");
  }
  case NEON::BI__builtin_neon_vqdmlal_v:
  case NEON::BI__builtin_neon_vqdmlsl_v: {
    SmallVector<Value *, 2> MulOps(Ops.begin() + 1, Ops.end());
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Ty), MulOps, "vqdmlal");
    Ops.resize(2);
    return EmitNeonCall(CGM.getIntrinsic(AltLLVMIntrinsic, Ty), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqshl_n_v:
  case NEON::BI__builtin_neon_vqshlq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshl_n",
                        1, false);
  case NEON::BI__builtin_neon_vqshlu_n_v:
  case NEON::BI__builtin_neon_vqshluq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshlu_n",
                        1, false);
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
  case NEON::BI__builtin_neon_vrsqrte_v:
  case NEON::BI__builtin_neon_vrsqrteq_v:
    Int = Ty->isFPOrFPVectorTy() ? LLVMIntrinsic : AltLLVMIntrinsic;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrndi_v:
  case NEON::BI__builtin_neon_vrndiq_v:
    Int = Intrinsic::nearbyint;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrshr_n_v:
  case NEON::BI__builtin_neon_vrshrq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrshr_n",
                        1, true);
  case NEON::BI__builtin_neon_vshl_n_v:
  case NEON::BI__builtin_neon_vshlq_n_v:
    Ops[1] = EmitNeonShiftVector(Ops[1], Ty, false);
    return Builder.CreateShl(Builder.CreateBitCast(Ops[0],Ty), Ops[1],
                             "vshl_n");
  case NEON::BI__builtin_neon_vshll_n_v: {
    llvm::Type *SrcTy = llvm::VectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    if (Usgn)
      Ops[0] = Builder.CreateZExt(Ops[0], VTy);
    else
      Ops[0] = Builder.CreateSExt(Ops[0], VTy);
    Ops[1] = EmitNeonShiftVector(Ops[1], VTy, false);
    return Builder.CreateShl(Ops[0], Ops[1], "vshll_n");
  }
  case NEON::BI__builtin_neon_vshrn_n_v: {
    llvm::Type *SrcTy = llvm::VectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = EmitNeonShiftVector(Ops[1], SrcTy, false);
    if (Usgn)
      Ops[0] = Builder.CreateLShr(Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateAShr(Ops[0], Ops[1]);
    return Builder.CreateTrunc(Ops[0], Ty, "vshrn_n");
  }
  case NEON::BI__builtin_neon_vshr_n_v:
  case NEON::BI__builtin_neon_vshrq_n_v:
    return EmitNeonRShiftImm(Ops[0], Ops[1], Ty, Usgn, "vshr_n");
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v:
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v:
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v:
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v:
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v:
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    llvm::Type *Tys[] = {Int8PtrTy, Ty};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vst1_x2_v:
  case NEON::BI__builtin_neon_vst1q_x2_v:
  case NEON::BI__builtin_neon_vst1_x3_v:
  case NEON::BI__builtin_neon_vst1q_x3_v:
  case NEON::BI__builtin_neon_vst1_x4_v:
  case NEON::BI__builtin_neon_vst1q_x4_v: {
    llvm::Type *PTy = llvm::PointerType::getUnqual(VTy->getVectorElementType());
    // TODO: Currently in AArch32 mode the pointer operand comes first, whereas
    // in AArch64 it comes last. We may want to stick to one or another.
    if (Arch == llvm::Triple::aarch64 || Arch == llvm::Triple::aarch64_be) {
      llvm::Type *Tys[2] = { VTy, PTy };
      std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
      return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "");
    }
    llvm::Type *Tys[2] = { PTy, VTy };
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vsubhn_v: {
    llvm::VectorType *SrcTy =
        llvm::VectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateSub(Ops[0], Ops[1], "vsubhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vsubhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vsubhn");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i+vi);
        Indices.push_back(i+e+vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vtst_v:
  case NEON::BI__builtin_neon_vtstq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                ConstantAggregateZero::get(Ty));
    return Builder.CreateSExt(Ops[0], Ty, "vtst");
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2*i+vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi*e) >> 1);
        Indices.push_back(((i + vi*e) >> 1)+e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vdot_v:
  case NEON::BI__builtin_neon_vdotq_v: {
    llvm::Type *InputTy =
        llvm::VectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = { Ty, InputTy };
    Int = Usgn ? LLVMIntrinsic : AltLLVMIntrinsic;
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vdot");
  }
  case NEON::BI__builtin_neon_vfmlal_low_v:
  case NEON::BI__builtin_neon_vfmlalq_low_v: {
    llvm::Type *InputTy =
        llvm::VectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlal_low");
  }
  case NEON::BI__builtin_neon_vfmlsl_low_v:
  case NEON::BI__builtin_neon_vfmlslq_low_v: {
    llvm::Type *InputTy =
        llvm::VectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlsl_low");
  }
  case NEON::BI__builtin_neon_vfmlal_high_v:
  case NEON::BI__builtin_neon_vfmlalq_high_v: {
    llvm::Type *InputTy =
           llvm::VectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlal_high");
  }
  case NEON::BI__builtin_neon_vfmlsl_high_v:
  case NEON::BI__builtin_neon_vfmlslq_high_v: {
    llvm::Type *InputTy =
           llvm::VectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlsl_high");
  }
  }

  assert(Int && "Expected valid intrinsic number");

  // Determine the type(s) of this overloaded AArch64 intrinsic.
  Function *F = LookupNeonLLVMIntrinsic(Int, Modifier, Ty, E);

  Value *Result = EmitNeonCall(F, Ops, NameHint);
  llvm::Type *ResultType = ConvertType(E->getType());
  // AArch64 intrinsic one-element vector type cast to
  // scalar type expected by the builtin
  return Builder.CreateBitCast(Result, ResultType, NameHint);
}

Value *CodeGenFunction::EmitAArch64CompareBuiltinExpr(
    Value *Op, llvm::Type *Ty, const CmpInst::Predicate Fp,
    const CmpInst::Predicate Ip, const Twine &Name) {
  llvm::Type *OTy = Op->getType();

  // FIXME: this is utterly horrific. We should not be looking at previous
  // codegen context to find out what needs doing. Unfortunately TableGen
  // currently gives us exactly the same calls for vceqz_f32 and vceqz_s32
  // (etc).
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Op))
    OTy = BI->getOperand(0)->getType();

  Op = Builder.CreateBitCast(Op, OTy);
  if (OTy->getScalarType()->isFloatingPointTy()) {
    Op = Builder.CreateFCmp(Fp, Op, Constant::getNullValue(OTy));
  } else {
    Op = Builder.CreateICmp(Ip, Op, Constant::getNullValue(OTy));
  }
  return Builder.CreateSExt(Op, Ty, Name);
}

static Value *packTBLDVectorList(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                                 Value *ExtOp, Value *IndexOp,
                                 llvm::Type *ResTy, unsigned IntID,
                                 const char *Name) {
  SmallVector<Value *, 2> TblOps;
  if (ExtOp)
    TblOps.push_back(ExtOp);

  // Build a vector containing sequential number like (0, 1, 2, ..., 15)
  SmallVector<uint32_t, 16> Indices;
  llvm::VectorType *TblTy = cast<llvm::VectorType>(Ops[0]->getType());
  for (unsigned i = 0, e = TblTy->getNumElements(); i != e; ++i) {
    Indices.push_back(2*i);
    Indices.push_back(2*i+1);
  }

  int PairPos = 0, End = Ops.size() - 1;
  while (PairPos < End) {
    TblOps.push_back(CGF.Builder.CreateShuffleVector(Ops[PairPos],
                                                     Ops[PairPos+1], Indices,
                                                     Name));
    PairPos += 2;
  }

  // If there's an odd number of 64-bit lookup table, fill the high 64-bit
  // of the 128-bit lookup table with zero.
  if (PairPos == End) {
    Value *ZeroTbl = ConstantAggregateZero::get(TblTy);
    TblOps.push_back(CGF.Builder.CreateShuffleVector(Ops[PairPos],
                                                     ZeroTbl, Indices, Name));
  }

  Function *TblF;
  TblOps.push_back(IndexOp);
  TblF = CGF.CGM.getIntrinsic(IntID, ResTy);

  return CGF.EmitNeonCall(TblF, TblOps, Name);
}

Value *CodeGenFunction::GetValueForARMHint(unsigned BuiltinID) {
  unsigned Value;
  switch (BuiltinID) {
  default:
    return nullptr;
  case ARM::BI__builtin_arm_nop:
    Value = 0;
    break;
  case ARM::BI__builtin_arm_yield:
  case ARM::BI__yield:
    Value = 1;
    break;
  case ARM::BI__builtin_arm_wfe:
  case ARM::BI__wfe:
    Value = 2;
    break;
  case ARM::BI__builtin_arm_wfi:
  case ARM::BI__wfi:
    Value = 3;
    break;
  case ARM::BI__builtin_arm_sev:
  case ARM::BI__sev:
    Value = 4;
    break;
  case ARM::BI__builtin_arm_sevl:
  case ARM::BI__sevl:
    Value = 5;
    break;
  }

  return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_hint),
                            llvm::ConstantInt::get(Int32Ty, Value));
}

// Generates the IR for the read/write special register builtin,
// ValueType is the type of the value that is to be written or read,
// RegisterType is the type of the register being written to or read from.
static Value *EmitSpecialRegisterBuiltin(CodeGenFunction &CGF,
                                         const CallExpr *E,
                                         llvm::Type *RegisterType,
                                         llvm::Type *ValueType,
                                         bool IsRead,
                                         StringRef SysReg = "") {
  // write and register intrinsics only support 32 and 64 bit operations.
  assert((RegisterType->isIntegerTy(32) || RegisterType->isIntegerTy(64))
          && "Unsupported size for register.");

  CodeGen::CGBuilderTy &Builder = CGF.Builder;
  CodeGen::CodeGenModule &CGM = CGF.CGM;
  LLVMContext &Context = CGM.getLLVMContext();

  if (SysReg.empty()) {
    const Expr *SysRegStrExpr = E->getArg(0)->IgnoreParenCasts();
    SysReg = cast<clang::StringLiteral>(SysRegStrExpr)->getString();
  }

  llvm::Metadata *Ops[] = { llvm::MDString::get(Context, SysReg) };
  llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
  llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

  llvm::Type *Types[] = { RegisterType };

  bool MixedTypes = RegisterType->isIntegerTy(64) && ValueType->isIntegerTy(32);
  assert(!(RegisterType->isIntegerTy(32) && ValueType->isIntegerTy(64))
            && "Can't fit 64-bit value in 32-bit register");

  if (IsRead) {
    llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::read_register, Types);
    llvm::Value *Call = Builder.CreateCall(F, Metadata);

    if (MixedTypes)
      // Read into 64 bit register and then truncate result to 32 bit.
      return Builder.CreateTrunc(Call, ValueType);

    if (ValueType->isPointerTy())
      // Have i32/i64 result (Call) but want to return a VoidPtrTy (i8*).
      return Builder.CreateIntToPtr(Call, ValueType);

    return Call;
  }

  llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::write_register, Types);
  llvm::Value *ArgValue = CGF.EmitScalarExpr(E->getArg(1));
  if (MixedTypes) {
    // Extend 32 bit write value to 64 bit to pass to write.
    ArgValue = Builder.CreateZExt(ArgValue, RegisterType);
    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  if (ValueType->isPointerTy()) {
    // Have VoidPtrTy ArgValue but want to return an i32/i64.
    ArgValue = Builder.CreatePtrToInt(ArgValue, RegisterType);
    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  return Builder.CreateCall(F, { Metadata, ArgValue });
}

/// Return true if BuiltinID is an overloaded Neon intrinsic with an extra
/// argument that specifies the vector type.
static bool HasExtraNeonArgument(unsigned BuiltinID) {
  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vget_lane_f32:
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
  case NEON::BI__builtin_neon_vsha1h_u32:
  case NEON::BI__builtin_neon_vsha1cq_u32:
  case NEON::BI__builtin_neon_vsha1pq_u32:
  case NEON::BI__builtin_neon_vsha1mq_u32:
  case clang::ARM::BI_MoveToCoprocessor:
  case clang::ARM::BI_MoveToCoprocessor2:
    return false;
  }
  return true;
}

Value *CodeGenFunction::EmitISOVolatileLoad(const CallExpr *E) {
  Value *Ptr = EmitScalarExpr(E->getArg(0));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits LoadSize = getContext().getTypeSizeInChars(ElTy);
  llvm::Type *ITy = llvm::IntegerType::get(getLLVMContext(),
                                           LoadSize.getQuantity() * 8);
  Ptr = Builder.CreateBitCast(Ptr, ITy->getPointerTo());
  llvm::LoadInst *Load =
    Builder.CreateAlignedLoad(Ptr, LoadSize);
  Load->setVolatile(true);
  return Load;
}

Value *CodeGenFunction::EmitISOVolatileStore(const CallExpr *E) {
  Value *Ptr = EmitScalarExpr(E->getArg(0));
  Value *Value = EmitScalarExpr(E->getArg(1));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits StoreSize = getContext().getTypeSizeInChars(ElTy);
  llvm::Type *ITy = llvm::IntegerType::get(getLLVMContext(),
                                           StoreSize.getQuantity() * 8);
  Ptr = Builder.CreateBitCast(Ptr, ITy->getPointerTo());
  llvm::StoreInst *Store =
    Builder.CreateAlignedStore(Value, Ptr,
                               StoreSize);
  Store->setVolatile(true);
  return Store;
}

Value *CodeGenFunction::EmitARMBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E,
                                           llvm::Triple::ArchType Arch) {
  if (auto Hint = GetValueForARMHint(BuiltinID))
    return Hint;

  if (BuiltinID == ARM::BI__emit) {
    bool IsThumb = getTarget().getTriple().getArch() == llvm::Triple::thumb;
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(VoidTy, /*Variadic=*/false);

    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, CGM.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt Value = Result.Val.getInt();
    uint64_t ZExtValue = Value.zextOrTrunc(IsThumb ? 16 : 32).getZExtValue();

    llvm::InlineAsm *Emit =
        IsThumb ? InlineAsm::get(FTy, ".inst.n 0x" + utohexstr(ZExtValue), "",
                                 /*SideEffects=*/true)
                : InlineAsm::get(FTy, ".inst 0x" + utohexstr(ZExtValue), "",
                                 /*SideEffects=*/true);

    return Builder.CreateCall(Emit);
  }

  if (BuiltinID == ARM::BI__builtin_arm_dbg) {
    Value *Option = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_dbg), Option);
  }

  if (BuiltinID == ARM::BI__builtin_arm_prefetch) {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *RW      = EmitScalarExpr(E->getArg(1));
    Value *IsData  = EmitScalarExpr(E->getArg(2));

    // Locality is not supported on ARM target
    Value *Locality = llvm::ConstantInt::get(Int32Ty, 3);

    Value *F = CGM.getIntrinsic(Intrinsic::prefetch);
    return Builder.CreateCall(F, {Address, RW, Locality, IsData});
  }

  if (BuiltinID == ARM::BI__builtin_arm_rbit) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }

  if (BuiltinID == ARM::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    const FunctionDecl *FD = E->getDirectCallee();
    Value *Ops[2];
    for (unsigned i = 0; i < 2; i++)
      Ops[i] = EmitScalarExpr(E->getArg(i));
    llvm::Type *Ty = CGM.getTypes().ConvertType(FD->getType());
    llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    StringRef Name = FD->getName();
    return EmitNounwindRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name), Ops);
  }

  if (BuiltinID == ARM::BI__builtin_arm_mcrr ||
      BuiltinID == ARM::BI__builtin_arm_mcrr2) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case ARM::BI__builtin_arm_mcrr:
      F = CGM.getIntrinsic(Intrinsic::arm_mcrr);
      break;
    case ARM::BI__builtin_arm_mcrr2:
      F = CGM.getIntrinsic(Intrinsic::arm_mcrr2);
      break;
    }

    // MCRR{2} instruction has 5 operands but
    // the intrinsic has 4 because Rt and Rt2
    // are represented as a single unsigned 64
    // bit integer in the intrinsic definition
    // but internally it's represented as 2 32
    // bit integers.

    Value *Coproc = EmitScalarExpr(E->getArg(0));
    Value *Opc1 = EmitScalarExpr(E->getArg(1));
    Value *RtAndRt2 = EmitScalarExpr(E->getArg(2));
    Value *CRm = EmitScalarExpr(E->getArg(3));

    Value *C1 = llvm::ConstantInt::get(Int64Ty, 32);
    Value *Rt = Builder.CreateTruncOrBitCast(RtAndRt2, Int32Ty);
    Value *Rt2 = Builder.CreateLShr(RtAndRt2, C1);
    Rt2 = Builder.CreateTruncOrBitCast(Rt2, Int32Ty);

    return Builder.CreateCall(F, {Coproc, Opc1, Rt, Rt2, CRm});
  }

  if (BuiltinID == ARM::BI__builtin_arm_mrrc ||
      BuiltinID == ARM::BI__builtin_arm_mrrc2) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case ARM::BI__builtin_arm_mrrc:
      F = CGM.getIntrinsic(Intrinsic::arm_mrrc);
      break;
    case ARM::BI__builtin_arm_mrrc2:
      F = CGM.getIntrinsic(Intrinsic::arm_mrrc2);
      break;
    }

    Value *Coproc = EmitScalarExpr(E->getArg(0));
    Value *Opc1 = EmitScalarExpr(E->getArg(1));
    Value *CRm  = EmitScalarExpr(E->getArg(2));
    Value *RtAndRt2 = Builder.CreateCall(F, {Coproc, Opc1, CRm});

    // Returns an unsigned 64 bit integer, represented
    // as two 32 bit integers.

    Value *Rt = Builder.CreateExtractValue(RtAndRt2, 1);
    Value *Rt1 = Builder.CreateExtractValue(RtAndRt2, 0);
    Rt = Builder.CreateZExt(Rt, Int64Ty);
    Rt1 = Builder.CreateZExt(Rt1, Int64Ty);

    Value *ShiftCast = llvm::ConstantInt::get(Int64Ty, 32);
    RtAndRt2 = Builder.CreateShl(Rt, ShiftCast, "shl", true);
    RtAndRt2 = Builder.CreateOr(RtAndRt2, Rt1);

    return Builder.CreateBitCast(RtAndRt2, ConvertType(E->getType()));
  }

  if (BuiltinID == ARM::BI__builtin_arm_ldrexd ||
      ((BuiltinID == ARM::BI__builtin_arm_ldrex ||
        BuiltinID == ARM::BI__builtin_arm_ldaex) &&
       getContext().getTypeSize(E->getType()) == 64) ||
      BuiltinID == ARM::BI__ldrexd) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case ARM::BI__builtin_arm_ldaex:
      F = CGM.getIntrinsic(Intrinsic::arm_ldaexd);
      break;
    case ARM::BI__builtin_arm_ldrexd:
    case ARM::BI__builtin_arm_ldrex:
    case ARM::BI__ldrexd:
      F = CGM.getIntrinsic(Intrinsic::arm_ldrexd);
      break;
    }

    Value *LdPtr = EmitScalarExpr(E->getArg(0));
    Value *Val = Builder.CreateCall(F, Builder.CreateBitCast(LdPtr, Int8PtrTy),
                                    "ldrexd");

    Value *Val0 = Builder.CreateExtractValue(Val, 1);
    Value *Val1 = Builder.CreateExtractValue(Val, 0);
    Val0 = Builder.CreateZExt(Val0, Int64Ty);
    Val1 = Builder.CreateZExt(Val1, Int64Ty);

    Value *ShiftCst = llvm::ConstantInt::get(Int64Ty, 32);
    Val = Builder.CreateShl(Val0, ShiftCst, "shl", true /* nuw */);
    Val = Builder.CreateOr(Val, Val1);
    return Builder.CreateBitCast(Val, ConvertType(E->getType()));
  }

  if (BuiltinID == ARM::BI__builtin_arm_ldrex ||
      BuiltinID == ARM::BI__builtin_arm_ldaex) {
    Value *LoadAddr = EmitScalarExpr(E->getArg(0));

    QualType Ty = E->getType();
    llvm::Type *RealResTy = ConvertType(Ty);
    llvm::Type *PtrTy = llvm::IntegerType::get(
        getLLVMContext(), getContext().getTypeSize(Ty))->getPointerTo();
    LoadAddr = Builder.CreateBitCast(LoadAddr, PtrTy);

    Function *F = CGM.getIntrinsic(BuiltinID == ARM::BI__builtin_arm_ldaex
                                       ? Intrinsic::arm_ldaex
                                       : Intrinsic::arm_ldrex,
                                   PtrTy);
    Value *Val = Builder.CreateCall(F, LoadAddr, "ldrex");

    if (RealResTy->isPointerTy())
      return Builder.CreateIntToPtr(Val, RealResTy);
    else {
      llvm::Type *IntResTy = llvm::IntegerType::get(
          getLLVMContext(), CGM.getDataLayout().getTypeSizeInBits(RealResTy));
      Val = Builder.CreateTruncOrBitCast(Val, IntResTy);
      return Builder.CreateBitCast(Val, RealResTy);
    }
  }

  if (BuiltinID == ARM::BI__builtin_arm_strexd ||
      ((BuiltinID == ARM::BI__builtin_arm_stlex ||
        BuiltinID == ARM::BI__builtin_arm_strex) &&
       getContext().getTypeSize(E->getArg(0)->getType()) == 64)) {
    Function *F = CGM.getIntrinsic(BuiltinID == ARM::BI__builtin_arm_stlex
                                       ? Intrinsic::arm_stlexd
                                       : Intrinsic::arm_strexd);
    llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty);

    Address Tmp = CreateMemTemp(E->getArg(0)->getType());
    Value *Val = EmitScalarExpr(E->getArg(0));
    Builder.CreateStore(Val, Tmp);

    Address LdPtr = Builder.CreateBitCast(Tmp,llvm::PointerType::getUnqual(STy));
    Val = Builder.CreateLoad(LdPtr);

    Value *Arg0 = Builder.CreateExtractValue(Val, 0);
    Value *Arg1 = Builder.CreateExtractValue(Val, 1);
    Value *StPtr = Builder.CreateBitCast(EmitScalarExpr(E->getArg(1)), Int8PtrTy);
    return Builder.CreateCall(F, {Arg0, Arg1, StPtr}, "strexd");
  }

  if (BuiltinID == ARM::BI__builtin_arm_strex ||
      BuiltinID == ARM::BI__builtin_arm_stlex) {
    Value *StoreVal = EmitScalarExpr(E->getArg(0));
    Value *StoreAddr = EmitScalarExpr(E->getArg(1));

    QualType Ty = E->getArg(0)->getType();
    llvm::Type *StoreTy = llvm::IntegerType::get(getLLVMContext(),
                                                 getContext().getTypeSize(Ty));
    StoreAddr = Builder.CreateBitCast(StoreAddr, StoreTy->getPointerTo());

    if (StoreVal->getType()->isPointerTy())
      StoreVal = Builder.CreatePtrToInt(StoreVal, Int32Ty);
    else {
      llvm::Type *IntTy = llvm::IntegerType::get(
          getLLVMContext(),
          CGM.getDataLayout().getTypeSizeInBits(StoreVal->getType()));
      StoreVal = Builder.CreateBitCast(StoreVal, IntTy);
      StoreVal = Builder.CreateZExtOrBitCast(StoreVal, Int32Ty);
    }

    Function *F = CGM.getIntrinsic(BuiltinID == ARM::BI__builtin_arm_stlex
                                       ? Intrinsic::arm_stlex
                                       : Intrinsic::arm_strex,
                                   StoreAddr->getType());
    return Builder.CreateCall(F, {StoreVal, StoreAddr}, "strex");
  }

  switch (BuiltinID) {
  case ARM::BI__iso_volatile_load8:
  case ARM::BI__iso_volatile_load16:
  case ARM::BI__iso_volatile_load32:
  case ARM::BI__iso_volatile_load64:
    return EmitISOVolatileLoad(E);
  case ARM::BI__iso_volatile_store8:
  case ARM::BI__iso_volatile_store16:
  case ARM::BI__iso_volatile_store32:
  case ARM::BI__iso_volatile_store64:
    return EmitISOVolatileStore(E);
  }

  if (BuiltinID == ARM::BI__builtin_arm_clrex) {
    Function *F = CGM.getIntrinsic(Intrinsic::arm_clrex);
    return Builder.CreateCall(F);
  }

  // CRC32
  Intrinsic::ID CRCIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case ARM::BI__builtin_arm_crc32b:
    CRCIntrinsicID = Intrinsic::arm_crc32b; break;
  case ARM::BI__builtin_arm_crc32cb:
    CRCIntrinsicID = Intrinsic::arm_crc32cb; break;
  case ARM::BI__builtin_arm_crc32h:
    CRCIntrinsicID = Intrinsic::arm_crc32h; break;
  case ARM::BI__builtin_arm_crc32ch:
    CRCIntrinsicID = Intrinsic::arm_crc32ch; break;
  case ARM::BI__builtin_arm_crc32w:
  case ARM::BI__builtin_arm_crc32d:
    CRCIntrinsicID = Intrinsic::arm_crc32w; break;
  case ARM::BI__builtin_arm_crc32cw:
  case ARM::BI__builtin_arm_crc32cd:
    CRCIntrinsicID = Intrinsic::arm_crc32cw; break;
  }

  if (CRCIntrinsicID != Intrinsic::not_intrinsic) {
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    Value *Arg1 = EmitScalarExpr(E->getArg(1));

    // crc32{c,}d intrinsics are implemnted as two calls to crc32{c,}w
    // intrinsics, hence we need different codegen for these cases.
    if (BuiltinID == ARM::BI__builtin_arm_crc32d ||
        BuiltinID == ARM::BI__builtin_arm_crc32cd) {
      Value *C1 = llvm::ConstantInt::get(Int64Ty, 32);
      Value *Arg1a = Builder.CreateTruncOrBitCast(Arg1, Int32Ty);
      Value *Arg1b = Builder.CreateLShr(Arg1, C1);
      Arg1b = Builder.CreateTruncOrBitCast(Arg1b, Int32Ty);

      Function *F = CGM.getIntrinsic(CRCIntrinsicID);
      Value *Res = Builder.CreateCall(F, {Arg0, Arg1a});
      return Builder.CreateCall(F, {Res, Arg1b});
    } else {
      Arg1 = Builder.CreateZExtOrBitCast(Arg1, Int32Ty);

      Function *F = CGM.getIntrinsic(CRCIntrinsicID);
      return Builder.CreateCall(F, {Arg0, Arg1});
    }
  }

  if (BuiltinID == ARM::BI__builtin_arm_rsr ||
      BuiltinID == ARM::BI__builtin_arm_rsr64 ||
      BuiltinID == ARM::BI__builtin_arm_rsrp ||
      BuiltinID == ARM::BI__builtin_arm_wsr ||
      BuiltinID == ARM::BI__builtin_arm_wsr64 ||
      BuiltinID == ARM::BI__builtin_arm_wsrp) {

    bool IsRead = BuiltinID == ARM::BI__builtin_arm_rsr ||
                  BuiltinID == ARM::BI__builtin_arm_rsr64 ||
                  BuiltinID == ARM::BI__builtin_arm_rsrp;

    bool IsPointerBuiltin = BuiltinID == ARM::BI__builtin_arm_rsrp ||
                            BuiltinID == ARM::BI__builtin_arm_wsrp;

    bool Is64Bit = BuiltinID == ARM::BI__builtin_arm_rsr64 ||
                   BuiltinID == ARM::BI__builtin_arm_wsr64;

    llvm::Type *ValueType;
    llvm::Type *RegisterType;
    if (IsPointerBuiltin) {
      ValueType = VoidPtrTy;
      RegisterType = Int32Ty;
    } else if (Is64Bit) {
      ValueType = RegisterType = Int64Ty;
    } else {
      ValueType = RegisterType = Int32Ty;
    }

    return EmitSpecialRegisterBuiltin(*this, E, RegisterType, ValueType, IsRead);
  }

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  auto getAlignmentValue32 = [&](Address addr) -> Value* {
    return Builder.getInt32(addr.getAlignment().getQuantity());
  };

  Address PtrOp0 = Address::invalid();
  Address PtrOp1 = Address::invalid();
  SmallVector<Value*, 4> Ops;
  bool HasExtraArg = HasExtraNeonArgument(BuiltinID);
  unsigned NumArgs = E->getNumArgs() - (HasExtraArg ? 1 : 0);
  for (unsigned i = 0, e = NumArgs; i != e; i++) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst2_v:
      case NEON::BI__builtin_neon_vst2q_v:
      case NEON::BI__builtin_neon_vst2_lane_v:
      case NEON::BI__builtin_neon_vst2q_lane_v:
      case NEON::BI__builtin_neon_vst3_v:
      case NEON::BI__builtin_neon_vst3q_v:
      case NEON::BI__builtin_neon_vst3_lane_v:
      case NEON::BI__builtin_neon_vst3q_lane_v:
      case NEON::BI__builtin_neon_vst4_v:
      case NEON::BI__builtin_neon_vst4q_v:
      case NEON::BI__builtin_neon_vst4_lane_v:
      case NEON::BI__builtin_neon_vst4q_lane_v:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp0 = EmitPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.getPointer());
        continue;
      }
    }
    if (i == 1) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld2_v:
      case NEON::BI__builtin_neon_vld2q_v:
      case NEON::BI__builtin_neon_vld3_v:
      case NEON::BI__builtin_neon_vld3q_v:
      case NEON::BI__builtin_neon_vld4_v:
      case NEON::BI__builtin_neon_vld4q_v:
      case NEON::BI__builtin_neon_vld2_lane_v:
      case NEON::BI__builtin_neon_vld2q_lane_v:
      case NEON::BI__builtin_neon_vld3_lane_v:
      case NEON::BI__builtin_neon_vld3q_lane_v:
      case NEON::BI__builtin_neon_vld4_lane_v:
      case NEON::BI__builtin_neon_vld4q_lane_v:
      case NEON::BI__builtin_neon_vld2_dup_v:
      case NEON::BI__builtin_neon_vld2q_dup_v:
      case NEON::BI__builtin_neon_vld3_dup_v:
      case NEON::BI__builtin_neon_vld3q_dup_v:
      case NEON::BI__builtin_neon_vld4_dup_v:
      case NEON::BI__builtin_neon_vld4q_dup_v:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp1 = EmitPointerWithAlignment(E->getArg(1));
        Ops.push_back(PtrOp1.getPointer());
        continue;
      }
    }

    if ((ICEArguments & (1 << i)) == 0) {
      Ops.push_back(EmitScalarExpr(E->getArg(i)));
    } else {
      // If this is required to be a constant, constant fold it so that we know
      // that the generated intrinsic gets a ConstantInt.
      llvm::APSInt Result;
      bool IsConst = E->getArg(i)->isIntegerConstantExpr(Result, getContext());
      assert(IsConst && "Constant arg isn't actually constant?"); (void)IsConst;
      Ops.push_back(llvm::ConstantInt::get(getLLVMContext(), Result));
    }
  }

  switch (BuiltinID) {
  default: break;

  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vget_lane_f32:
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vgetq_lane_f32:
    return Builder.CreateExtractElement(Ops[0], Ops[1], "vget_lane");

  case NEON::BI__builtin_neon_vrndns_f32: {
    Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Tys[] = {Arg->getType()};
    Function *F = CGM.getIntrinsic(Intrinsic::arm_neon_vrintn, Tys);
    return Builder.CreateCall(F, {Arg}, "vrndn"); }

  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");

  case NEON::BI__builtin_neon_vsha1h_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1h), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1cq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1c), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1pq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1p), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1mq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1m), Ops,
                        "vsha1h");

  // The ARM _MoveToCoprocessor builtins put the input register value as
  // the first argument, but the LLVM intrinsic expects it as the third one.
  case ARM::BI_MoveToCoprocessor:
  case ARM::BI_MoveToCoprocessor2: {
    Function *F = CGM.getIntrinsic(BuiltinID == ARM::BI_MoveToCoprocessor ?
                                   Intrinsic::arm_mcr : Intrinsic::arm_mcr2);
    return Builder.CreateCall(F, {Ops[1], Ops[2], Ops[0],
                                  Ops[3], Ops[4], Ops[5]});
  }
  case ARM::BI_BitScanForward:
  case ARM::BI_BitScanForward64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanForward, E);
  case ARM::BI_BitScanReverse:
  case ARM::BI_BitScanReverse64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanReverse, E);

  case ARM::BI_InterlockedAnd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E);
  case ARM::BI_InterlockedExchange64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E);
  case ARM::BI_InterlockedExchangeAdd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E);
  case ARM::BI_InterlockedExchangeSub64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E);
  case ARM::BI_InterlockedOr64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E);
  case ARM::BI_InterlockedXor64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E);
  case ARM::BI_InterlockedDecrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E);
  case ARM::BI_InterlockedIncrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E);
  case ARM::BI_InterlockedExchangeAdd8_acq:
  case ARM::BI_InterlockedExchangeAdd16_acq:
  case ARM::BI_InterlockedExchangeAdd_acq:
  case ARM::BI_InterlockedExchangeAdd64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_acq, E);
  case ARM::BI_InterlockedExchangeAdd8_rel:
  case ARM::BI_InterlockedExchangeAdd16_rel:
  case ARM::BI_InterlockedExchangeAdd_rel:
  case ARM::BI_InterlockedExchangeAdd64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_rel, E);
  case ARM::BI_InterlockedExchangeAdd8_nf:
  case ARM::BI_InterlockedExchangeAdd16_nf:
  case ARM::BI_InterlockedExchangeAdd_nf:
  case ARM::BI_InterlockedExchangeAdd64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_nf, E);
  case ARM::BI_InterlockedExchange8_acq:
  case ARM::BI_InterlockedExchange16_acq:
  case ARM::BI_InterlockedExchange_acq:
  case ARM::BI_InterlockedExchange64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_acq, E);
  case ARM::BI_InterlockedExchange8_rel:
  case ARM::BI_InterlockedExchange16_rel:
  case ARM::BI_InterlockedExchange_rel:
  case ARM::BI_InterlockedExchange64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_rel, E);
  case ARM::BI_InterlockedExchange8_nf:
  case ARM::BI_InterlockedExchange16_nf:
  case ARM::BI_InterlockedExchange_nf:
  case ARM::BI_InterlockedExchange64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_nf, E);
  case ARM::BI_InterlockedCompareExchange8_acq:
  case ARM::BI_InterlockedCompareExchange16_acq:
  case ARM::BI_InterlockedCompareExchange_acq:
  case ARM::BI_InterlockedCompareExchange64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_acq, E);
  case ARM::BI_InterlockedCompareExchange8_rel:
  case ARM::BI_InterlockedCompareExchange16_rel:
  case ARM::BI_InterlockedCompareExchange_rel:
  case ARM::BI_InterlockedCompareExchange64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_rel, E);
  case ARM::BI_InterlockedCompareExchange8_nf:
  case ARM::BI_InterlockedCompareExchange16_nf:
  case ARM::BI_InterlockedCompareExchange_nf:
  case ARM::BI_InterlockedCompareExchange64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_nf, E);
  case ARM::BI_InterlockedOr8_acq:
  case ARM::BI_InterlockedOr16_acq:
  case ARM::BI_InterlockedOr_acq:
  case ARM::BI_InterlockedOr64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_acq, E);
  case ARM::BI_InterlockedOr8_rel:
  case ARM::BI_InterlockedOr16_rel:
  case ARM::BI_InterlockedOr_rel:
  case ARM::BI_InterlockedOr64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_rel, E);
  case ARM::BI_InterlockedOr8_nf:
  case ARM::BI_InterlockedOr16_nf:
  case ARM::BI_InterlockedOr_nf:
  case ARM::BI_InterlockedOr64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_nf, E);
  case ARM::BI_InterlockedXor8_acq:
  case ARM::BI_InterlockedXor16_acq:
  case ARM::BI_InterlockedXor_acq:
  case ARM::BI_InterlockedXor64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_acq, E);
  case ARM::BI_InterlockedXor8_rel:
  case ARM::BI_InterlockedXor16_rel:
  case ARM::BI_InterlockedXor_rel:
  case ARM::BI_InterlockedXor64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_rel, E);
  case ARM::BI_InterlockedXor8_nf:
  case ARM::BI_InterlockedXor16_nf:
  case ARM::BI_InterlockedXor_nf:
  case ARM::BI_InterlockedXor64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_nf, E);
  case ARM::BI_InterlockedAnd8_acq:
  case ARM::BI_InterlockedAnd16_acq:
  case ARM::BI_InterlockedAnd_acq:
  case ARM::BI_InterlockedAnd64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_acq, E);
  case ARM::BI_InterlockedAnd8_rel:
  case ARM::BI_InterlockedAnd16_rel:
  case ARM::BI_InterlockedAnd_rel:
  case ARM::BI_InterlockedAnd64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_rel, E);
  case ARM::BI_InterlockedAnd8_nf:
  case ARM::BI_InterlockedAnd16_nf:
  case ARM::BI_InterlockedAnd_nf:
  case ARM::BI_InterlockedAnd64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_nf, E);
  case ARM::BI_InterlockedIncrement16_acq:
  case ARM::BI_InterlockedIncrement_acq:
  case ARM::BI_InterlockedIncrement64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_acq, E);
  case ARM::BI_InterlockedIncrement16_rel:
  case ARM::BI_InterlockedIncrement_rel:
  case ARM::BI_InterlockedIncrement64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_rel, E);
  case ARM::BI_InterlockedIncrement16_nf:
  case ARM::BI_InterlockedIncrement_nf:
  case ARM::BI_InterlockedIncrement64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_nf, E);
  case ARM::BI_InterlockedDecrement16_acq:
  case ARM::BI_InterlockedDecrement_acq:
  case ARM::BI_InterlockedDecrement64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_acq, E);
  case ARM::BI_InterlockedDecrement16_rel:
  case ARM::BI_InterlockedDecrement_rel:
  case ARM::BI_InterlockedDecrement64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_rel, E);
  case ARM::BI_InterlockedDecrement16_nf:
  case ARM::BI_InterlockedDecrement_nf:
  case ARM::BI_InterlockedDecrement64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_nf, E);
  }

  // Get the last argument, which specifies the vector type.
  assert(HasExtraArg);
  llvm::APSInt Result;
  const Expr *Arg = E->getArg(E->getNumArgs()-1);
  if (!Arg->isIntegerConstantExpr(Result, getContext()))
    return nullptr;

  if (BuiltinID == ARM::BI__builtin_arm_vcvtr_f ||
      BuiltinID == ARM::BI__builtin_arm_vcvtr_d) {
    // Determine the overloaded type of this builtin.
    llvm::Type *Ty;
    if (BuiltinID == ARM::BI__builtin_arm_vcvtr_f)
      Ty = FloatTy;
    else
      Ty = DoubleTy;

    // Determine whether this is an unsigned conversion or not.
    bool usgn = Result.getZExtValue() == 1;
    unsigned Int = usgn ? Intrinsic::arm_vcvtru : Intrinsic::arm_vcvtr;

    // Call the appropriate intrinsic.
    Function *F = CGM.getIntrinsic(Int, Ty);
    return Builder.CreateCall(F, Ops, "vcvtr");
  }

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type(Result.getZExtValue());
  bool usgn = Type.isUnsigned();
  bool rightShift = false;

  llvm::VectorType *VTy = GetNeonType(this, Type,
                                      getTarget().hasLegalHalfType());
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  // Many NEON builtins have identical semantics and uses in ARM and
  // AArch64. Emit these in a single function.
  auto IntrinsicMap = makeArrayRef(ARMSIMDIntrinsicMap);
  const NeonIntrinsicInfo *Builtin = findNeonIntrinsicInMap(
      IntrinsicMap, BuiltinID, NEONSIMDIntrinsicsProvenSorted);
  if (Builtin)
    return EmitCommonNeonBuiltinExpr(
        Builtin->BuiltinID, Builtin->LLVMIntrinsic, Builtin->AltLLVMIntrinsic,
        Builtin->NameHint, Builtin->TypeModifier, E, Ops, PtrOp0, PtrOp1, Arch);

  unsigned Int;
  switch (BuiltinID) {
  default: return nullptr;
  case NEON::BI__builtin_neon_vld1q_lane_v:
    // Handle 64-bit integer elements as a special case.  Use shuffles of
    // one-element vectors to avoid poor code for i64 in the backend.
    if (VTy->getElementType()->isIntegerTy(64)) {
      // Extract the other lane.
      Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
      uint32_t Lane = cast<ConstantInt>(Ops[2])->getZExtValue();
      Value *SV = llvm::ConstantVector::get(ConstantInt::get(Int32Ty, 1-Lane));
      Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV);
      // Load the value as a one-element vector.
      Ty = llvm::VectorType::get(VTy->getElementType(), 1);
      llvm::Type *Tys[] = {Ty, Int8PtrTy};
      Function *F = CGM.getIntrinsic(Intrinsic::arm_neon_vld1, Tys);
      Value *Align = getAlignmentValue32(PtrOp0);
      Value *Ld = Builder.CreateCall(F, {Ops[0], Align});
      // Combine them.
      uint32_t Indices[] = {1 - Lane, Lane};
      SV = llvm::ConstantDataVector::get(getLLVMContext(), Indices);
      return Builder.CreateShuffleVector(Ops[1], Ld, SV, "vld1q_lane");
    }
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vld1_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    PtrOp0 = Builder.CreateElementBitCast(PtrOp0, VTy->getElementType());
    Value *Ld = Builder.CreateLoad(PtrOp0);
    return Builder.CreateInsertElement(Ops[1], Ld, Ops[2], "vld1_lane");
  }
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    Int =
      usgn ? Intrinsic::arm_neon_vqrshiftnu : Intrinsic::arm_neon_vqrshiftns;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrn_n",
                        1, true);
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vqrshiftnsu, Ty),
                        Ops, "vqrshrun_n", 1, true);
  case NEON::BI__builtin_neon_vqshrn_n_v:
    Int = usgn ? Intrinsic::arm_neon_vqshiftnu : Intrinsic::arm_neon_vqshiftns;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrn_n",
                        1, true);
  case NEON::BI__builtin_neon_vqshrun_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vqshiftnsu, Ty),
                        Ops, "vqshrun_n", 1, true);
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vrecpe, Ty),
                        Ops, "vrecpe");
  case NEON::BI__builtin_neon_vrshrn_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vrshiftn, Ty),
                        Ops, "vrshrn_n", 1, true);
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = EmitNeonShiftVector(Ops[2], Ty, true);
    Int = usgn ? Intrinsic::arm_neon_vrshiftu : Intrinsic::arm_neon_vrshifts;
    Ops[1] = Builder.CreateCall(CGM.getIntrinsic(Int, Ty), {Ops[1], Ops[2]});
    return Builder.CreateAdd(Ops[0], Ops[1], "vrsra_n");
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v:
    rightShift = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v:
    Ops[2] = EmitNeonShiftVector(Ops[2], Ty, rightShift);
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vshiftins, Ty),
                        Ops, "vsli_n");
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = EmitNeonRShiftImm(Ops[1], Ops[2], Ty, usgn, "vsra_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  case NEON::BI__builtin_neon_vst1q_lane_v:
    // Handle 64-bit integer elements as a special case.  Use a shuffle to get
    // a one-element vector and avoid poor code for i64 in the backend.
    if (VTy->getElementType()->isIntegerTy(64)) {
      Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
      Value *SV = llvm::ConstantVector::get(cast<llvm::Constant>(Ops[2]));
      Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV);
      Ops[2] = getAlignmentValue32(PtrOp0);
      llvm::Type *Tys[] = {Int8PtrTy, Ops[1]->getType()};
      return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_neon_vst1,
                                                 Tys), Ops);
    }
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vst1_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    auto St = Builder.CreateStore(Ops[1], Builder.CreateBitCast(PtrOp0, Ty));
    return St;
  }
  case NEON::BI__builtin_neon_vtbl1_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl1),
                        Ops, "vtbl1");
  case NEON::BI__builtin_neon_vtbl2_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl2),
                        Ops, "vtbl2");
  case NEON::BI__builtin_neon_vtbl3_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl3),
                        Ops, "vtbl3");
  case NEON::BI__builtin_neon_vtbl4_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl4),
                        Ops, "vtbl4");
  case NEON::BI__builtin_neon_vtbx1_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx1),
                        Ops, "vtbx1");
  case NEON::BI__builtin_neon_vtbx2_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx2),
                        Ops, "vtbx2");
  case NEON::BI__builtin_neon_vtbx3_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx3),
                        Ops, "vtbx3");
  case NEON::BI__builtin_neon_vtbx4_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx4),
                        Ops, "vtbx4");
  }
}

static Value *EmitAArch64TblBuiltinExpr(CodeGenFunction &CGF, unsigned BuiltinID,
                                      const CallExpr *E,
                                      SmallVectorImpl<Value *> &Ops,
                                      llvm::Triple::ArchType Arch) {
  unsigned int Int = 0;
  const char *s = nullptr;

  switch (BuiltinID) {
  default:
    return nullptr;
  case NEON::BI__builtin_neon_vtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
  case NEON::BI__builtin_neon_vtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v:
  case NEON::BI__builtin_neon_vtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
  case NEON::BI__builtin_neon_vtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    break;
  case NEON::BI__builtin_neon_vtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
  case NEON::BI__builtin_neon_vtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
  case NEON::BI__builtin_neon_vtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
  case NEON::BI__builtin_neon_vtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    break;
  }

  assert(E->getNumArgs() >= 3);

  // Get the last argument, which specifies the vector type.
  llvm::APSInt Result;
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  if (!Arg->isIntegerConstantExpr(Result, CGF.getContext()))
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type(Result.getZExtValue());
  llvm::VectorType *Ty = GetNeonType(&CGF, Type);
  if (!Ty)
    return nullptr;

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  // AArch64 scalar builtins are not overloaded, they do not have an extra
  // argument that specifies the vector type, need to handle each case.
  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vtbl1_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(0, 1), nullptr,
                              Ops[1], Ty, Intrinsic::aarch64_neon_tbl1,
                              "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl2_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(0, 2), nullptr,
                              Ops[2], Ty, Intrinsic::aarch64_neon_tbl1,
                              "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl3_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(0, 3), nullptr,
                              Ops[3], Ty, Intrinsic::aarch64_neon_tbl2,
                              "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbl4_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(0, 4), nullptr,
                              Ops[4], Ty, Intrinsic::aarch64_neon_tbl2,
                              "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbx1_v: {
    Value *TblRes =
        packTBLDVectorList(CGF, makeArrayRef(Ops).slice(1, 1), nullptr, Ops[2],
                           Ty, Intrinsic::aarch64_neon_tbl1, "vtbl1");

    llvm::Constant *EightV = ConstantInt::get(Ty, 8);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[2], EightV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx2_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(1, 2), Ops[0],
                              Ops[3], Ty, Intrinsic::aarch64_neon_tbx1,
                              "vtbx1");
  }
  case NEON::BI__builtin_neon_vtbx3_v: {
    Value *TblRes =
        packTBLDVectorList(CGF, makeArrayRef(Ops).slice(1, 3), nullptr, Ops[4],
                           Ty, Intrinsic::aarch64_neon_tbl2, "vtbl2");

    llvm::Constant *TwentyFourV = ConstantInt::get(Ty, 24);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[4],
                                           TwentyFourV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx4_v: {
    return packTBLDVectorList(CGF, makeArrayRef(Ops).slice(1, 4), Ops[0],
                              Ops[5], Ty, Intrinsic::aarch64_neon_tbx2,
                              "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
    Int = Intrinsic::aarch64_neon_tbl1; s = "vtbl1"; break;
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    Int = Intrinsic::aarch64_neon_tbl2; s = "vtbl2"; break;
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
    Int = Intrinsic::aarch64_neon_tbl3; s = "vtbl3"; break;
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    Int = Intrinsic::aarch64_neon_tbl4; s = "vtbl4"; break;
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
    Int = Intrinsic::aarch64_neon_tbx1; s = "vtbx1"; break;
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
    Int = Intrinsic::aarch64_neon_tbx2; s = "vtbx2"; break;
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
    Int = Intrinsic::aarch64_neon_tbx3; s = "vtbx3"; break;
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    Int = Intrinsic::aarch64_neon_tbx4; s = "vtbx4"; break;
  }
  }

  if (!Int)
    return nullptr;

  Function *F = CGF.CGM.getIntrinsic(Int, Ty);
  return CGF.EmitNeonCall(F, Ops, s);
}

Value *CodeGenFunction::vectorWrapScalar16(Value *Op) {
  llvm::Type *VTy = llvm::VectorType::get(Int16Ty, 4);
  Op = Builder.CreateBitCast(Op, Int16Ty);
  Value *V = UndefValue::get(VTy);
  llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
  Op = Builder.CreateInsertElement(V, Op, CI);
  return Op;
}

Value *CodeGenFunction::EmitAArch64BuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E,
                                               llvm::Triple::ArchType Arch) {
  unsigned HintID = static_cast<unsigned>(-1);
  switch (BuiltinID) {
  default: break;
  case AArch64::BI__builtin_arm_nop:
    HintID = 0;
    break;
  case AArch64::BI__builtin_arm_yield:
  case AArch64::BI__yield:
    HintID = 1;
    break;
  case AArch64::BI__builtin_arm_wfe:
  case AArch64::BI__wfe:
    HintID = 2;
    break;
  case AArch64::BI__builtin_arm_wfi:
  case AArch64::BI__wfi:
    HintID = 3;
    break;
  case AArch64::BI__builtin_arm_sev:
  case AArch64::BI__sev:
    HintID = 4;
    break;
  case AArch64::BI__builtin_arm_sevl:
  case AArch64::BI__sevl:
    HintID = 5;
    break;
  }

  if (HintID != static_cast<unsigned>(-1)) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_hint);
    return Builder.CreateCall(F, llvm::ConstantInt::get(Int32Ty, HintID));
  }

  if (BuiltinID == AArch64::BI__builtin_arm_prefetch) {
    Value *Address         = EmitScalarExpr(E->getArg(0));
    Value *RW              = EmitScalarExpr(E->getArg(1));
    Value *CacheLevel      = EmitScalarExpr(E->getArg(2));
    Value *RetentionPolicy = EmitScalarExpr(E->getArg(3));
    Value *IsData          = EmitScalarExpr(E->getArg(4));

    Value *Locality = nullptr;
    if (cast<llvm::ConstantInt>(RetentionPolicy)->isZero()) {
      // Temporal fetch, needs to convert cache level to locality.
      Locality = llvm::ConstantInt::get(Int32Ty,
        -cast<llvm::ConstantInt>(CacheLevel)->getValue() + 3);
    } else {
      // Streaming fetch.
      Locality = llvm::ConstantInt::get(Int32Ty, 0);
    }

    // FIXME: We need AArch64 specific LLVM intrinsic if we want to specify
    // PLDL3STRM or PLDL2STRM.
    Value *F = CGM.getIntrinsic(Intrinsic::prefetch);
    return Builder.CreateCall(F, {Address, RW, Locality, IsData});
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rbit) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "rbit of unusual size!");
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }
  if (BuiltinID == AArch64::BI__builtin_arm_rbit64) {
    assert((getContext().getTypeSize(E->getType()) == 64) &&
           "rbit of unusual size!");
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }

  if (BuiltinID == AArch64::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    const FunctionDecl *FD = E->getDirectCallee();
    Value *Ops[2];
    for (unsigned i = 0; i < 2; i++)
      Ops[i] = EmitScalarExpr(E->getArg(i));
    llvm::Type *Ty = CGM.getTypes().ConvertType(FD->getType());
    llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    StringRef Name = FD->getName();
    return EmitNounwindRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name), Ops);
  }

  if ((BuiltinID == AArch64::BI__builtin_arm_ldrex ||
      BuiltinID == AArch64::BI__builtin_arm_ldaex) &&
      getContext().getTypeSize(E->getType()) == 128) {
    Function *F = CGM.getIntrinsic(BuiltinID == AArch64::BI__builtin_arm_ldaex
                                       ? Intrinsic::aarch64_ldaxp
                                       : Intrinsic::aarch64_ldxp);

    Value *LdPtr = EmitScalarExpr(E->getArg(0));
    Value *Val = Builder.CreateCall(F, Builder.CreateBitCast(LdPtr, Int8PtrTy),
                                    "ldxp");

    Value *Val0 = Builder.CreateExtractValue(Val, 1);
    Value *Val1 = Builder.CreateExtractValue(Val, 0);
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);
    Val0 = Builder.CreateZExt(Val0, Int128Ty);
    Val1 = Builder.CreateZExt(Val1, Int128Ty);

    Value *ShiftCst = llvm::ConstantInt::get(Int128Ty, 64);
    Val = Builder.CreateShl(Val0, ShiftCst, "shl", true /* nuw */);
    Val = Builder.CreateOr(Val, Val1);
    return Builder.CreateBitCast(Val, ConvertType(E->getType()));
  } else if (BuiltinID == AArch64::BI__builtin_arm_ldrex ||
             BuiltinID == AArch64::BI__builtin_arm_ldaex) {
    Value *LoadAddr = EmitScalarExpr(E->getArg(0));

    QualType Ty = E->getType();
    llvm::Type *RealResTy = ConvertType(Ty);
    llvm::Type *PtrTy = llvm::IntegerType::get(
        getLLVMContext(), getContext().getTypeSize(Ty))->getPointerTo();
    LoadAddr = Builder.CreateBitCast(LoadAddr, PtrTy);

    Function *F = CGM.getIntrinsic(BuiltinID == AArch64::BI__builtin_arm_ldaex
                                       ? Intrinsic::aarch64_ldaxr
                                       : Intrinsic::aarch64_ldxr,
                                   PtrTy);
    Value *Val = Builder.CreateCall(F, LoadAddr, "ldxr");

    if (RealResTy->isPointerTy())
      return Builder.CreateIntToPtr(Val, RealResTy);

    llvm::Type *IntResTy = llvm::IntegerType::get(
        getLLVMContext(), CGM.getDataLayout().getTypeSizeInBits(RealResTy));
    Val = Builder.CreateTruncOrBitCast(Val, IntResTy);
    return Builder.CreateBitCast(Val, RealResTy);
  }

  if ((BuiltinID == AArch64::BI__builtin_arm_strex ||
       BuiltinID == AArch64::BI__builtin_arm_stlex) &&
      getContext().getTypeSize(E->getArg(0)->getType()) == 128) {
    Function *F = CGM.getIntrinsic(BuiltinID == AArch64::BI__builtin_arm_stlex
                                       ? Intrinsic::aarch64_stlxp
                                       : Intrinsic::aarch64_stxp);
    llvm::Type *STy = llvm::StructType::get(Int64Ty, Int64Ty);

    Address Tmp = CreateMemTemp(E->getArg(0)->getType());
    EmitAnyExprToMem(E->getArg(0), Tmp, Qualifiers(), /*init*/ true);

    Tmp = Builder.CreateBitCast(Tmp, llvm::PointerType::getUnqual(STy));
    llvm::Value *Val = Builder.CreateLoad(Tmp);

    Value *Arg0 = Builder.CreateExtractValue(Val, 0);
    Value *Arg1 = Builder.CreateExtractValue(Val, 1);
    Value *StPtr = Builder.CreateBitCast(EmitScalarExpr(E->getArg(1)),
                                         Int8PtrTy);
    return Builder.CreateCall(F, {Arg0, Arg1, StPtr}, "stxp");
  }

  if (BuiltinID == AArch64::BI__builtin_arm_strex ||
      BuiltinID == AArch64::BI__builtin_arm_stlex) {
    Value *StoreVal = EmitScalarExpr(E->getArg(0));
    Value *StoreAddr = EmitScalarExpr(E->getArg(1));

    QualType Ty = E->getArg(0)->getType();
    llvm::Type *StoreTy = llvm::IntegerType::get(getLLVMContext(),
                                                 getContext().getTypeSize(Ty));
    StoreAddr = Builder.CreateBitCast(StoreAddr, StoreTy->getPointerTo());

    if (StoreVal->getType()->isPointerTy())
      StoreVal = Builder.CreatePtrToInt(StoreVal, Int64Ty);
    else {
      llvm::Type *IntTy = llvm::IntegerType::get(
          getLLVMContext(),
          CGM.getDataLayout().getTypeSizeInBits(StoreVal->getType()));
      StoreVal = Builder.CreateBitCast(StoreVal, IntTy);
      StoreVal = Builder.CreateZExtOrBitCast(StoreVal, Int64Ty);
    }

    Function *F = CGM.getIntrinsic(BuiltinID == AArch64::BI__builtin_arm_stlex
                                       ? Intrinsic::aarch64_stlxr
                                       : Intrinsic::aarch64_stxr,
                                   StoreAddr->getType());
    return Builder.CreateCall(F, {StoreVal, StoreAddr}, "stxr");
  }

  if (BuiltinID == AArch64::BI__getReg) {
    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, CGM.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt Value = Result.Val.getInt();
    LLVMContext &Context = CGM.getLLVMContext();
    std::string Reg = Value == 31 ? "sp" : "x" + Value.toString(10);

    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, Reg)};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Value *F =
        CGM.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    return Builder.CreateCall(F, Metadata);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_clrex) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_clrex);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == AArch64::BI_ReadWriteBarrier)
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::SingleThread);

  // CRC32
  Intrinsic::ID CRCIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case AArch64::BI__builtin_arm_crc32b:
    CRCIntrinsicID = Intrinsic::aarch64_crc32b; break;
  case AArch64::BI__builtin_arm_crc32cb:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cb; break;
  case AArch64::BI__builtin_arm_crc32h:
    CRCIntrinsicID = Intrinsic::aarch64_crc32h; break;
  case AArch64::BI__builtin_arm_crc32ch:
    CRCIntrinsicID = Intrinsic::aarch64_crc32ch; break;
  case AArch64::BI__builtin_arm_crc32w:
    CRCIntrinsicID = Intrinsic::aarch64_crc32w; break;
  case AArch64::BI__builtin_arm_crc32cw:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cw; break;
  case AArch64::BI__builtin_arm_crc32d:
    CRCIntrinsicID = Intrinsic::aarch64_crc32x; break;
  case AArch64::BI__builtin_arm_crc32cd:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cx; break;
  }

  if (CRCIntrinsicID != Intrinsic::not_intrinsic) {
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    Value *Arg1 = EmitScalarExpr(E->getArg(1));
    Function *F = CGM.getIntrinsic(CRCIntrinsicID);

    llvm::Type *DataTy = F->getFunctionType()->getParamType(1);
    Arg1 = Builder.CreateZExtOrBitCast(Arg1, DataTy);

    return Builder.CreateCall(F, {Arg0, Arg1});
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr ||
      BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == AArch64::BI__builtin_arm_wsr ||
      BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_wsrp) {

    bool IsRead = BuiltinID == AArch64::BI__builtin_arm_rsr ||
                  BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
                  BuiltinID == AArch64::BI__builtin_arm_rsrp;

    bool IsPointerBuiltin = BuiltinID == AArch64::BI__builtin_arm_rsrp ||
                            BuiltinID == AArch64::BI__builtin_arm_wsrp;

    bool Is64Bit = BuiltinID != AArch64::BI__builtin_arm_rsr &&
                   BuiltinID != AArch64::BI__builtin_arm_wsr;

    llvm::Type *ValueType;
    llvm::Type *RegisterType = Int64Ty;
    if (IsPointerBuiltin) {
      ValueType = VoidPtrTy;
    } else if (Is64Bit) {
      ValueType = Int64Ty;
    } else {
      ValueType = Int32Ty;
    }

    return EmitSpecialRegisterBuiltin(*this, E, RegisterType, ValueType, IsRead);
  }

  if (BuiltinID == AArch64::BI_ReadStatusReg ||
      BuiltinID == AArch64::BI_WriteStatusReg) {
    LLVMContext &Context = CGM.getLLVMContext();

    unsigned SysReg =
      E->getArg(0)->EvaluateKnownConstInt(getContext()).getZExtValue();

    std::string SysRegStr;
    llvm::raw_string_ostream(SysRegStr) <<
                       ((1 << 1) | ((SysReg >> 14) & 1))  << ":" <<
                       ((SysReg >> 11) & 7)               << ":" <<
                       ((SysReg >> 7)  & 15)              << ":" <<
                       ((SysReg >> 3)  & 15)              << ":" <<
                       ( SysReg        & 7);

    llvm::Metadata *Ops[] = { llvm::MDString::get(Context, SysRegStr) };
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Type *RegisterType = Int64Ty;
    llvm::Type *Types[] = { RegisterType };

    if (BuiltinID == AArch64::BI_ReadStatusReg) {
      llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::read_register, Types);

      return Builder.CreateCall(F, Metadata);
    }

    llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::write_register, Types);
    llvm::Value *ArgValue = EmitScalarExpr(E->getArg(1));

    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  if (BuiltinID == AArch64::BI_AddressOfReturnAddress) {
    llvm::Value *F = CGM.getIntrinsic(Intrinsic::addressofreturnaddress);
    return Builder.CreateCall(F);
  }

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  llvm::SmallVector<Value*, 4> Ops;
  for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; i++) {
    if ((ICEArguments & (1 << i)) == 0) {
      Ops.push_back(EmitScalarExpr(E->getArg(i)));
    } else {
      // If this is required to be a constant, constant fold it so that we know
      // that the generated intrinsic gets a ConstantInt.
      llvm::APSInt Result;
      bool IsConst = E->getArg(i)->isIntegerConstantExpr(Result, getContext());
      assert(IsConst && "Constant arg isn't actually constant?");
      (void)IsConst;
      Ops.push_back(llvm::ConstantInt::get(getLLVMContext(), Result));
    }
  }

  auto SISDMap = makeArrayRef(AArch64SISDIntrinsicMap);
  const NeonIntrinsicInfo *Builtin = findNeonIntrinsicInMap(
      SISDMap, BuiltinID, AArch64SISDIntrinsicsProvenSorted);

  if (Builtin) {
    Ops.push_back(EmitScalarExpr(E->getArg(E->getNumArgs() - 1)));
    Value *Result = EmitCommonNeonSISDBuiltinExpr(*this, *Builtin, Ops, E);
    assert(Result && "SISD intrinsic should have been handled");
    return Result;
  }

  llvm::APSInt Result;
  const Expr *Arg = E->getArg(E->getNumArgs()-1);
  NeonTypeFlags Type(0);
  if (Arg->isIntegerConstantExpr(Result, getContext()))
    // Determine the type of this overloaded NEON intrinsic.
    Type = NeonTypeFlags(Result.getZExtValue());

  bool usgn = Type.isUnsigned();
  bool quad = Type.isQuad();

  // Handle non-overloaded intrinsics first.
  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_vabsh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::fabs, HalfTy), Ops, "vabs");
  case NEON::BI__builtin_neon_vldrq_p128: {
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(getLLVMContext(), 128);
    llvm::Type *Int128PTy = llvm::PointerType::get(Int128Ty, 0);
    Value *Ptr = Builder.CreateBitCast(EmitScalarExpr(E->getArg(0)), Int128PTy);
    return Builder.CreateAlignedLoad(Int128Ty, Ptr,
                                     CharUnits::fromQuantity(16));
  }
  case NEON::BI__builtin_neon_vstrq_p128: {
    llvm::Type *Int128PTy = llvm::Type::getIntNPtrTy(getLLVMContext(), 128);
    Value *Ptr = Builder.CreateBitCast(Ops[0], Int128PTy);
    return Builder.CreateDefaultAlignedStore(EmitScalarExpr(E->getArg(1)), Ptr);
  }
  case NEON::BI__builtin_neon_vcvts_u32_f32:
  case NEON::BI__builtin_neon_vcvtd_u64_f64:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvts_s32_f32:
  case NEON::BI__builtin_neon_vcvtd_s64_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    bool Is64 = Ops[0]->getType()->getPrimitiveSizeInBits() == 64;
    llvm::Type *InTy = Is64 ? Int64Ty : Int32Ty;
    llvm::Type *FTy = Is64 ? DoubleTy : FloatTy;
    Ops[0] = Builder.CreateBitCast(Ops[0], FTy);
    if (usgn)
      return Builder.CreateFPToUI(Ops[0], InTy);
    return Builder.CreateFPToSI(Ops[0], InTy);
  }
  case NEON::BI__builtin_neon_vcvts_f32_u32:
  case NEON::BI__builtin_neon_vcvtd_f64_u64:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvts_f32_s32:
  case NEON::BI__builtin_neon_vcvtd_f64_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    bool Is64 = Ops[0]->getType()->getPrimitiveSizeInBits() == 64;
    llvm::Type *InTy = Is64 ? Int64Ty : Int32Ty;
    llvm::Type *FTy = Is64 ? DoubleTy : FloatTy;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvth_f16_u16:
  case NEON::BI__builtin_neon_vcvth_f16_u32:
  case NEON::BI__builtin_neon_vcvth_f16_u64:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvth_f16_s16:
  case NEON::BI__builtin_neon_vcvth_f16_s32:
  case NEON::BI__builtin_neon_vcvth_f16_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    llvm::Type *FTy = HalfTy;
    llvm::Type *InTy;
    if (Ops[0]->getType()->getPrimitiveSizeInBits() == 64)
      InTy = Int64Ty;
    else if (Ops[0]->getType()->getPrimitiveSizeInBits() == 32)
      InTy = Int32Ty;
    else
      InTy = Int16Ty;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvth_u16_f16:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvth_s16_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    if (usgn)
      return Builder.CreateFPToUI(Ops[0], Int16Ty);
    return Builder.CreateFPToSI(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_u32_f16:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvth_s32_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    if (usgn)
      return Builder.CreateFPToUI(Ops[0], Int32Ty);
    return Builder.CreateFPToSI(Ops[0], Int32Ty);
  }
  case NEON::BI__builtin_neon_vcvth_u64_f16:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vcvth_s64_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    if (usgn)
      return Builder.CreateFPToUI(Ops[0], Int64Ty);
    return Builder.CreateFPToSI(Ops[0], Int64Ty);
  }
  case NEON::BI__builtin_neon_vcvtah_u16_f16:
  case NEON::BI__builtin_neon_vcvtmh_u16_f16:
  case NEON::BI__builtin_neon_vcvtnh_u16_f16:
  case NEON::BI__builtin_neon_vcvtph_u16_f16:
  case NEON::BI__builtin_neon_vcvtah_s16_f16:
  case NEON::BI__builtin_neon_vcvtmh_s16_f16:
  case NEON::BI__builtin_neon_vcvtnh_s16_f16:
  case NEON::BI__builtin_neon_vcvtph_s16_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvtah_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtau; break;
    case NEON::BI__builtin_neon_vcvtmh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtmu; break;
    case NEON::BI__builtin_neon_vcvtnh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtnu; break;
    case NEON::BI__builtin_neon_vcvtph_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtpu; break;
    case NEON::BI__builtin_neon_vcvtah_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtas; break;
    case NEON::BI__builtin_neon_vcvtmh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtms; break;
    case NEON::BI__builtin_neon_vcvtnh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtns; break;
    case NEON::BI__builtin_neon_vcvtph_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtps; break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvt");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcaleh_f16:
  case NEON::BI__builtin_neon_vcalth_f16:
  case NEON::BI__builtin_neon_vcageh_f16:
  case NEON::BI__builtin_neon_vcagth_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcageh_f16:
      Int = Intrinsic::aarch64_neon_facge; break;
    case NEON::BI__builtin_neon_vcagth_f16:
      Int = Intrinsic::aarch64_neon_facgt; break;
    case NEON::BI__builtin_neon_vcaleh_f16:
      Int = Intrinsic::aarch64_neon_facge; std::swap(Ops[0], Ops[1]); break;
    case NEON::BI__builtin_neon_vcalth_f16:
      Int = Intrinsic::aarch64_neon_facgt; std::swap(Ops[0], Ops[1]); break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "facg");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_s16_f16:
  case NEON::BI__builtin_neon_vcvth_n_u16_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_s16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxs; break;
    case NEON::BI__builtin_neon_vcvth_n_u16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxu; break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvth_n");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_f16_s16:
  case NEON::BI__builtin_neon_vcvth_n_f16_u16: {
    unsigned Int;
    llvm::Type* FTy  = HalfTy;
    llvm::Type* InTy = Int32Ty;
    llvm::Type *Tys[2] = {FTy, InTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_f16_s16:
      Int = Intrinsic::aarch64_neon_vcvtfxs2fp;
      Ops[0] = Builder.CreateSExt(Ops[0], InTy, "sext");
      break;
    case NEON::BI__builtin_neon_vcvth_n_f16_u16:
      Int = Intrinsic::aarch64_neon_vcvtfxu2fp;
      Ops[0] = Builder.CreateZExt(Ops[0], InTy);
      break;
    }
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvth_n");
  }
  case NEON::BI__builtin_neon_vpaddd_s64: {
    llvm::Type *Ty = llvm::VectorType::get(Int64Ty, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2i64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpaddd_f64: {
    llvm::Type *Ty =
      llvm::VectorType::get(DoubleTy, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpadds_f32: {
    llvm::Type *Ty =
      llvm::VectorType::get(FloatTy, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f32, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f32");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f32 into a scalar f32.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vceqzd_s64:
  case NEON::BI__builtin_neon_vceqzd_f64:
  case NEON::BI__builtin_neon_vceqzs_f32:
  case NEON::BI__builtin_neon_vceqzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OEQ, ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgezd_s64:
  case NEON::BI__builtin_neon_vcgezd_f64:
  case NEON::BI__builtin_neon_vcgezs_f32:
  case NEON::BI__builtin_neon_vcgezh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGE, ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclezd_s64:
  case NEON::BI__builtin_neon_vclezd_f64:
  case NEON::BI__builtin_neon_vclezs_f32:
  case NEON::BI__builtin_neon_vclezh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLE, ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtzd_s64:
  case NEON::BI__builtin_neon_vcgtzd_f64:
  case NEON::BI__builtin_neon_vcgtzs_f32:
  case NEON::BI__builtin_neon_vcgtzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGT, ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltzd_s64:
  case NEON::BI__builtin_neon_vcltzd_f64:
  case NEON::BI__builtin_neon_vcltzs_f32:
  case NEON::BI__builtin_neon_vcltzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLT, ICmpInst::ICMP_SLT, "vcltz");

  case NEON::BI__builtin_neon_vceqzd_u64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[0] =
        Builder.CreateICmpEQ(Ops[0], llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqzd");
  }
  case NEON::BI__builtin_neon_vceqd_f64:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcged_f64:
  case NEON::BI__builtin_neon_vcgtd_f64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_f64: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcled_f64: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vcltd_f64: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcged_f64: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgtd_f64: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
    Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqs_f32:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcges_f32:
  case NEON::BI__builtin_neon_vcgts_f32: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqs_f32: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcles_f32: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vclts_f32: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcges_f32: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgts_f32: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], FloatTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], FloatTy);
    Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int32Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqh_f16:
  case NEON::BI__builtin_neon_vcleh_f16:
  case NEON::BI__builtin_neon_vclth_f16:
  case NEON::BI__builtin_neon_vcgeh_f16:
  case NEON::BI__builtin_neon_vcgth_f16: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqh_f16: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcleh_f16: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vclth_f16: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcgeh_f16: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgth_f16: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], HalfTy);
    Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int16Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqd_s64:
  case NEON::BI__builtin_neon_vceqd_u64:
  case NEON::BI__builtin_neon_vcgtd_s64:
  case NEON::BI__builtin_neon_vcgtd_u64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vcged_u64:
  case NEON::BI__builtin_neon_vcged_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcled_s64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_s64:
    case NEON::BI__builtin_neon_vceqd_u64:P = llvm::ICmpInst::ICMP_EQ;break;
    case NEON::BI__builtin_neon_vcgtd_s64:P = llvm::ICmpInst::ICMP_SGT;break;
    case NEON::BI__builtin_neon_vcgtd_u64:P = llvm::ICmpInst::ICMP_UGT;break;
    case NEON::BI__builtin_neon_vcltd_s64:P = llvm::ICmpInst::ICMP_SLT;break;
    case NEON::BI__builtin_neon_vcltd_u64:P = llvm::ICmpInst::ICMP_ULT;break;
    case NEON::BI__builtin_neon_vcged_u64:P = llvm::ICmpInst::ICMP_UGE;break;
    case NEON::BI__builtin_neon_vcged_s64:P = llvm::ICmpInst::ICMP_SGE;break;
    case NEON::BI__builtin_neon_vcled_u64:P = llvm::ICmpInst::ICMP_ULE;break;
    case NEON::BI__builtin_neon_vcled_s64:P = llvm::ICmpInst::ICMP_SLE;break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateICmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqd");
  }
  case NEON::BI__builtin_neon_vtstd_s64:
  case NEON::BI__builtin_neon_vtstd_u64: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vtstd");
  }
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vset_lane_f64:
    // The vector type needs a cast for the v1f64 variant.
    Ops[1] = Builder.CreateBitCast(Ops[1],
                                   llvm::VectorType::get(DoubleTy, 1));
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vsetq_lane_f64:
    // The vector type needs a cast for the v2f64 variant.
    Ops[1] = Builder.CreateBitCast(Ops[1],
        llvm::VectorType::get(DoubleTy, 2));
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");

  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vdupb_lane_i8:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int8Ty, 8));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vdupb_laneq_i8:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int8Ty, 16));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vduph_lane_i16:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int16Ty, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vduph_laneq_i16:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int16Ty, 8));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vdups_lane_i32:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int32Ty, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdups_lane_f32:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vdups_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vdups_laneq_i32:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int32Ty, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vdupd_lane_i64:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int64Ty, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdupd_lane_f64:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vdupd_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vdupd_laneq_i64:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int64Ty, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_f32:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vget_lane_f64:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vdups_laneq_f32:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(FloatTy, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f64:
  case NEON::BI__builtin_neon_vdupd_laneq_f64:
    Ops[0] = Builder.CreateBitCast(Ops[0],
        llvm::VectorType::get(DoubleTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vaddh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFAdd(Ops[0], Ops[1], "vaddh");
  case NEON::BI__builtin_neon_vsubh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFSub(Ops[0], Ops[1], "vsubh");
  case NEON::BI__builtin_neon_vmulh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFMul(Ops[0], Ops[1], "vmulh");
  case NEON::BI__builtin_neon_vdivh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFDiv(Ops[0], Ops[1], "vdivh");
  case NEON::BI__builtin_neon_vfmah_f16: {
    Value *F = CGM.getIntrinsic(Intrinsic::fma, HalfTy);
    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return Builder.CreateCall(F,
      {EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2)), Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmsh_f16: {
    Value *F = CGM.getIntrinsic(Intrinsic::fma, HalfTy);
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(HalfTy);
    Value* Sub = Builder.CreateFSub(Zero, EmitScalarExpr(E->getArg(1)), "vsubh");
    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return Builder.CreateCall(F, {Sub, EmitScalarExpr(E->getArg(2)), Ops[0]});
  }
  case NEON::BI__builtin_neon_vaddd_s64:
  case NEON::BI__builtin_neon_vaddd_u64:
    return Builder.CreateAdd(Ops[0], EmitScalarExpr(E->getArg(1)), "vaddd");
  case NEON::BI__builtin_neon_vsubd_s64:
  case NEON::BI__builtin_neon_vsubd_u64:
    return Builder.CreateSub(Ops[0], EmitScalarExpr(E->getArg(1)), "vsubd");
  case NEON::BI__builtin_neon_vqdmlalh_s16:
  case NEON::BI__builtin_neon_vqdmlslh_s16: {
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(EmitScalarExpr(E->getArg(2))));
    llvm::Type *VTy = llvm::VectorType::get(Int32Ty, 4);
    Ops[1] = EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                          ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlalh_s16
                                        ? Intrinsic::aarch64_neon_sqadd
                                        : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccumInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqshlud_n_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqshlu, Int64Ty),
                        Ops, "vqshlu_n");
  }
  case NEON::BI__builtin_neon_vqshld_n_u64:
  case NEON::BI__builtin_neon_vqshld_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vqshld_n_u64
                                   ? Intrinsic::aarch64_neon_uqshl
                                   : Intrinsic::aarch64_neon_sqshl;
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return EmitNeonCall(CGM.getIntrinsic(Int, Int64Ty), Ops, "vqshl_n");
  }
  case NEON::BI__builtin_neon_vrshrd_n_u64:
  case NEON::BI__builtin_neon_vrshrd_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrshrd_n_u64
                                   ? Intrinsic::aarch64_neon_urshl
                                   : Intrinsic::aarch64_neon_srshl;
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    int SV = cast<ConstantInt>(Ops[1])->getSExtValue();
    Ops[1] = ConstantInt::get(Int64Ty, -SV);
    return EmitNeonCall(CGM.getIntrinsic(Int, Int64Ty), Ops, "vrshr_n");
  }
  case NEON::BI__builtin_neon_vrsrad_n_u64:
  case NEON::BI__builtin_neon_vrsrad_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrsrad_n_u64
                                   ? Intrinsic::aarch64_neon_urshl
                                   : Intrinsic::aarch64_neon_srshl;
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops.push_back(Builder.CreateNeg(EmitScalarExpr(E->getArg(2))));
    Ops[1] = Builder.CreateCall(CGM.getIntrinsic(Int, Int64Ty),
                                {Ops[1], Builder.CreateSExt(Ops[2], Int64Ty)});
    return Builder.CreateAdd(Ops[0], Builder.CreateBitCast(Ops[1], Int64Ty));
  }
  case NEON::BI__builtin_neon_vshld_n_s64:
  case NEON::BI__builtin_neon_vshld_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateShl(
        Ops[0], ConstantInt::get(Int64Ty, Amt->getZExtValue()), "shld_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateAShr(
        Ops[0], ConstantInt::get(Int64Ty, std::min(static_cast<uint64_t>(63),
                                                   Amt->getZExtValue())),
        "shrd_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    if (ShiftAmt == 64)
      return ConstantInt::get(Int64Ty, 0);
    return Builder.CreateLShr(Ops[0], ConstantInt::get(Int64Ty, ShiftAmt),
                              "shrd_n");
  }
  case NEON::BI__builtin_neon_vsrad_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateAShr(
        Ops[1], ConstantInt::get(Int64Ty, std::min(static_cast<uint64_t>(63),
                                                   Amt->getZExtValue())),
        "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vsrad_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(2)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    // As Op + 0 = Op, return Ops[0] directly.
    if (ShiftAmt == 64)
      return Ops[0];
    Ops[1] = Builder.CreateLShr(Ops[1], ConstantInt::get(Int64Ty, ShiftAmt),
                                "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vqdmlalh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlalh_laneq_s16:
  case NEON::BI__builtin_neon_vqdmlslh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlslh_laneq_s16: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], EmitScalarExpr(E->getArg(3)),
                                          "lane");
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(Ops[2]));
    llvm::Type *VTy = llvm::VectorType::get(Int32Ty, 4);
    Ops[1] = EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                          ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlalh_lane_s16 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlalh_laneq_s16)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_s32:
  case NEON::BI__builtin_neon_vqdmlsls_s32: {
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(EmitScalarExpr(E->getArg(2)));
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                     ProductOps, "vqdmlXl");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlals_s32
                                        ? Intrinsic::aarch64_neon_sqadd
                                        : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccumInt, Int64Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_lane_s32:
  case NEON::BI__builtin_neon_vqdmlals_laneq_s32:
  case NEON::BI__builtin_neon_vqdmlsls_lane_s32:
  case NEON::BI__builtin_neon_vqdmlsls_laneq_s32: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], EmitScalarExpr(E->getArg(3)),
                                          "lane");
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(Ops[2]);
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                     ProductOps, "vqdmlXl");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlals_lane_s32 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlals_laneq_s32)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccInt, Int64Ty), Ops, "vqdmlXl");
  }
  }

  llvm::VectorType *VTy = GetNeonType(this, Type);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  // Not all intrinsics handled by the common case work for AArch64 yet, so only
  // defer to common code if it's been added to our special map.
  Builtin = findNeonIntrinsicInMap(AArch64SIMDIntrinsicMap, BuiltinID,
                                   AArch64SIMDIntrinsicsProvenSorted);

  if (Builtin)
    return EmitCommonNeonBuiltinExpr(
        Builtin->BuiltinID, Builtin->LLVMIntrinsic, Builtin->AltLLVMIntrinsic,
        Builtin->NameHint, Builtin->TypeModifier, E, Ops,
        /*never use addresses*/ Address::invalid(), Address::invalid(), Arch);

  if (Value *V = EmitAArch64TblBuiltinExpr(*this, BuiltinID, E, Ops, Arch))
    return V;

  unsigned Int;
  switch (BuiltinID) {
  default: return nullptr;
  case NEON::BI__builtin_neon_vbsl_v:
  case NEON::BI__builtin_neon_vbslq_v: {
    llvm::Type *BitTy = llvm::VectorType::getInteger(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], BitTy, "vbsl");
    Ops[1] = Builder.CreateBitCast(Ops[1], BitTy, "vbsl");
    Ops[2] = Builder.CreateBitCast(Ops[2], BitTy, "vbsl");

    Ops[1] = Builder.CreateAnd(Ops[0], Ops[1], "vbsl");
    Ops[2] = Builder.CreateAnd(Builder.CreateNot(Ops[0]), Ops[2], "vbsl");
    Ops[0] = Builder.CreateOr(Ops[1], Ops[2], "vbsl");
    return Builder.CreateBitCast(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vfma_lane_v:
  case NEON::BI__builtin_neon_vfmaq_lane_v: { // Only used for FP types
    // The ARM builtins (and instructions) have the addend as the first
    // operand, but the 'fma' intrinsics have it last. Swap it around here.
    Value *Addend = Ops[0];
    Value *Multiplicand = Ops[1];
    Value *LaneSource = Ops[2];
    Ops[0] = Multiplicand;
    Ops[1] = LaneSource;
    Ops[2] = Addend;

    // Now adjust things to handle the lane access.
    llvm::Type *SourceTy = BuiltinID == NEON::BI__builtin_neon_vfmaq_lane_v ?
      llvm::VectorType::get(VTy->getElementType(), VTy->getNumElements() / 2) :
      VTy;
    llvm::Constant *cst = cast<Constant>(Ops[3]);
    Value *SV = llvm::ConstantVector::getSplat(VTy->getNumElements(), cst);
    Ops[1] = Builder.CreateBitCast(Ops[1], SourceTy);
    Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV, "lane");

    Ops.pop_back();
    Int = Intrinsic::fma;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "fmla");
  }
  case NEON::BI__builtin_neon_vfma_laneq_v: {
    llvm::VectorType *VTy = cast<llvm::VectorType>(Ty);
    // v1f64 fma should be mapped to Neon scalar f64 fma
    if (VTy && VTy->getElementType() == DoubleTy) {
      Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
      Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
      llvm::Type *VTy = GetNeonType(this,
        NeonTypeFlags(NeonTypeFlags::Float64, false, true));
      Ops[2] = Builder.CreateBitCast(Ops[2], VTy);
      Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
      Value *F = CGM.getIntrinsic(Intrinsic::fma, DoubleTy);
      Value *Result = Builder.CreateCall(F, {Ops[1], Ops[2], Ops[0]});
      return Builder.CreateBitCast(Result, Ty);
    }
    Value *F = CGM.getIntrinsic(Intrinsic::fma, Ty);
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    llvm::Type *STy = llvm::VectorType::get(VTy->getElementType(),
                                            VTy->getNumElements() * 2);
    Ops[2] = Builder.CreateBitCast(Ops[2], STy);
    Value* SV = llvm::ConstantVector::getSplat(VTy->getNumElements(),
                                               cast<ConstantInt>(Ops[3]));
    Ops[2] = Builder.CreateShuffleVector(Ops[2], Ops[2], SV, "lane");

    return Builder.CreateCall(F, {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmaq_laneq_v: {
    Value *F = CGM.getIntrinsic(Intrinsic::fma, Ty);
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[2] = EmitNeonSplat(Ops[2], cast<ConstantInt>(Ops[3]));
    return Builder.CreateCall(F, {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmah_lane_f16:
  case NEON::BI__builtin_neon_vfmas_lane_f32:
  case NEON::BI__builtin_neon_vfmah_laneq_f16:
  case NEON::BI__builtin_neon_vfmas_laneq_f32:
  case NEON::BI__builtin_neon_vfmad_lane_f64:
  case NEON::BI__builtin_neon_vfmad_laneq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(3)));
    llvm::Type *Ty = ConvertType(E->getCallReturnType(getContext()));
    Value *F = CGM.getIntrinsic(Intrinsic::fma, Ty);
    Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
    return Builder.CreateCall(F, {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vmull_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umull : Intrinsic::aarch64_neon_smull;
    if (Type.isPoly()) Int = Intrinsic::aarch64_neon_pmull;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vmax_v:
  case NEON::BI__builtin_neon_vmaxq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umax : Intrinsic::aarch64_neon_smax;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmax;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmax");
  case NEON::BI__builtin_neon_vmaxh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmax;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmax");
  }
  case NEON::BI__builtin_neon_vmin_v:
  case NEON::BI__builtin_neon_vminq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umin : Intrinsic::aarch64_neon_smin;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmin;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmin");
  case NEON::BI__builtin_neon_vminh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmin;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmin");
  }
  case NEON::BI__builtin_neon_vabd_v:
  case NEON::BI__builtin_neon_vabdq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_uabd : Intrinsic::aarch64_neon_sabd;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fabd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vabd");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    unsigned ArgElts = VTy->getNumElements();
    llvm::IntegerType *EltTy = cast<IntegerType>(VTy->getElementType());
    unsigned BitWidth = EltTy->getBitWidth();
    llvm::Type *ArgTy = llvm::VectorType::get(
        llvm::IntegerType::get(getLLVMContext(), BitWidth/2), 2*ArgElts);
    llvm::Type* Tys[2] = { VTy, ArgTy };
    Int = usgn ? Intrinsic::aarch64_neon_uaddlp : Intrinsic::aarch64_neon_saddlp;
    SmallVector<llvm::Value*, 1> TmpOps;
    TmpOps.push_back(Ops[1]);
    Function *F = CGM.getIntrinsic(Int, Tys);
    llvm::Value *tmp = EmitNeonCall(F, TmpOps, "vpadal");
    llvm::Value *addend = Builder.CreateBitCast(Ops[0], tmp->getType());
    return Builder.CreateAdd(tmp, addend);
  }
  case NEON::BI__builtin_neon_vpmin_v:
  case NEON::BI__builtin_neon_vpminq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_uminp : Intrinsic::aarch64_neon_sminp;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fminp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmin");
  case NEON::BI__builtin_neon_vpmax_v:
  case NEON::BI__builtin_neon_vpmaxq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umaxp : Intrinsic::aarch64_neon_smaxp;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmaxp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmax");
  case NEON::BI__builtin_neon_vminnm_v:
  case NEON::BI__builtin_neon_vminnmq_v:
    Int = Intrinsic::aarch64_neon_fminnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vminnm");
  case NEON::BI__builtin_neon_vminnmh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fminnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vminnm");
  case NEON::BI__builtin_neon_vmaxnm_v:
  case NEON::BI__builtin_neon_vmaxnmq_v:
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vmaxnmh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vrecpss_f32: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, FloatTy),
                        Ops, "vrecps");
  }
  case NEON::BI__builtin_neon_vrecpsd_f64:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, DoubleTy),
                        Ops, "vrecps");
  case NEON::BI__builtin_neon_vrecpsh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, HalfTy),
                        Ops, "vrecps");
  case NEON::BI__builtin_neon_vqshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqshrun;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrun_n");
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqrshrun;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrun_n");
  case NEON::BI__builtin_neon_vqshrn_n_v:
    Int = usgn ? Intrinsic::aarch64_neon_uqshrn : Intrinsic::aarch64_neon_sqshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrn_n");
  case NEON::BI__builtin_neon_vrshrn_n_v:
    Int = Intrinsic::aarch64_neon_rshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrshrn_n");
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    Int = usgn ? Intrinsic::aarch64_neon_uqrshrn : Intrinsic::aarch64_neon_sqrshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrn_n");
  case NEON::BI__builtin_neon_vrndah_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::round;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrnda_v:
  case NEON::BI__builtin_neon_vrndaq_v: {
    Int = Intrinsic::round;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrndih_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::nearbyint;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndi");
  }
  case NEON::BI__builtin_neon_vrndmh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::floor;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndm_v:
  case NEON::BI__builtin_neon_vrndmq_v: {
    Int = Intrinsic::floor;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndnh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frintn;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndn_v:
  case NEON::BI__builtin_neon_vrndnq_v: {
    Int = Intrinsic::aarch64_neon_frintn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndns_f32: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frintn;
    return EmitNeonCall(CGM.getIntrinsic(Int, FloatTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndph_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::ceil;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndp_v:
  case NEON::BI__builtin_neon_vrndpq_v: {
    Int = Intrinsic::ceil;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndxh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::rint;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndx_v:
  case NEON::BI__builtin_neon_vrndxq_v: {
    Int = Intrinsic::rint;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::trunc;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vrnd_v:
  case NEON::BI__builtin_neon_vrndq_v: {
    Int = Intrinsic::trunc;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vcvt_f64_v:
  case NEON::BI__builtin_neon_vcvtq_f64_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, quad));
    return usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f64_f32: {
    assert(Type.getEltType() == NeonTypeFlags::Float64 && quad &&
           "unexpected vcvt_f64_f32 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float32, false, false);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPExt(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_f32_f64: {
    assert(Type.getEltType() == NeonTypeFlags::Float32 &&
           "unexpected vcvt_f32_f64 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float64, false, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPTrunc(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_v:
  case NEON::BI__builtin_neon_vcvt_u16_v:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_v:
  case NEON::BI__builtin_neon_vcvtq_u16_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], GetFloatNeonType(this, Type));
    if (usgn)
      return Builder.CreateFPToUI(Ops[0], Ty);
    return Builder.CreateFPToSI(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vcvta_s16_v:
  case NEON::BI__builtin_neon_vcvta_u16_v:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_v:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_v:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtau : Intrinsic::aarch64_neon_fcvtas;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvta");
  }
  case NEON::BI__builtin_neon_vcvtm_s16_v:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_v:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtm_u16_v:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_v:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtmu : Intrinsic::aarch64_neon_fcvtms;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtm");
  }
  case NEON::BI__builtin_neon_vcvtn_s16_v:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_v:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtn_u16_v:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_v:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtnu : Intrinsic::aarch64_neon_fcvtns;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtn");
  }
  case NEON::BI__builtin_neon_vcvtp_s16_v:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_v:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtp_u16_v:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_v:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtpu : Intrinsic::aarch64_neon_fcvtps;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtp");
  }
  case NEON::BI__builtin_neon_vmulx_v:
  case NEON::BI__builtin_neon_vmulxq_v: {
    Int = Intrinsic::aarch64_neon_fmulx;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmulxh_lane_f16:
  case NEON::BI__builtin_neon_vmulxh_laneq_f16: {
    // vmulx_lane should be mapped to Neon scalar mulx after
    // extracting the scalar element
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Ops.pop_back();
    Int = Intrinsic::aarch64_neon_fmulx;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmul_lane_v:
  case NEON::BI__builtin_neon_vmul_laneq_v: {
    // v1f64 vmul_lane should be mapped to Neon scalar mul lane
    bool Quad = false;
    if (BuiltinID == NEON::BI__builtin_neon_vmul_laneq_v)
      Quad = true;
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    llvm::Type *VTy = GetNeonType(this,
      NeonTypeFlags(NeonTypeFlags::Float64, false, Quad));
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Value *Result = Builder.CreateFMul(Ops[0], Ops[1]);
    return Builder.CreateBitCast(Result, Ty);
  }
  case NEON::BI__builtin_neon_vnegd_s64:
    return Builder.CreateNeg(EmitScalarExpr(E->getArg(0)), "vnegd");
  case NEON::BI__builtin_neon_vnegh_f16:
    return Builder.CreateFNeg(EmitScalarExpr(E->getArg(0)), "vnegh");
  case NEON::BI__builtin_neon_vpmaxnm_v:
  case NEON::BI__builtin_neon_vpmaxnmq_v: {
    Int = Intrinsic::aarch64_neon_fmaxnmp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmaxnm");
  }
  case NEON::BI__builtin_neon_vpminnm_v:
  case NEON::BI__builtin_neon_vpminnmq_v: {
    Int = Intrinsic::aarch64_neon_fminnmp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpminnm");
  }
  case NEON::BI__builtin_neon_vsqrth_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::sqrt;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vsqrt_v:
  case NEON::BI__builtin_neon_vsqrtq_v: {
    Int = Intrinsic::sqrt;
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vrbit_v:
  case NEON::BI__builtin_neon_vrbitq_v: {
    Int = Intrinsic::aarch64_neon_rbit;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrbit");
  }
  case NEON::BI__builtin_neon_vaddv_u8:
    // FIXME: These are handled by the AArch64 scalar code.
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vaddv_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddv_u16:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vaddv_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u8:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vaddvq_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u16:
    usgn = true;
    LLVM_FALLTHROUGH;
  case NEON::BI__builtin_neon_vaddvq_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminv_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminvq_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmv_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::VectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmul_n_f64: {
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Value *RHS = Builder.CreateBitCast(EmitScalarExpr(E->getArg(1)), DoubleTy);
    return Builder.CreateFMul(Ops[0], RHS);
  }
  case NEON::BI__builtin_neon_vaddlv_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlv_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::VectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v: {
    Int = Intrinsic::aarch64_neon_vsri;
    llvm::Function *Intrin = CGM.getIntrinsic(Int, Ty);
    return EmitNeonCall(Intrin, Ops, "vsri_n");
  }
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v: {
    Int = Intrinsic::aarch64_neon_vsli;
    llvm::Function *Intrin = CGM.getIntrinsic(Int, Ty);
    return EmitNeonCall(Intrin, Ops, "vsli_n");
  }
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = EmitNeonRShiftImm(Ops[1], Ops[2], Ty, usgn, "vsra_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v: {
    Int = usgn ? Intrinsic::aarch64_neon_urshl : Intrinsic::aarch64_neon_srshl;
    SmallVector<llvm::Value*,2> TmpOps;
    TmpOps.push_back(Ops[1]);
    TmpOps.push_back(Ops[2]);
    Function* F = CGM.getIntrinsic(Int, Ty);
    llvm::Value *tmp = EmitNeonCall(F, TmpOps, "vrshr_n", 1, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    return Builder.CreateAdd(Ops[0], tmp);
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(VTy));
    auto Alignment = CharUnits::fromQuantity(
        BuiltinID == NEON::BI__builtin_neon_vld1_v ? 8 : 16);
    return Builder.CreateAlignedLoad(VTy, Ops[0], Alignment);
  }
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(VTy));
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  case NEON::BI__builtin_neon_vld1_lane_v:
  case NEON::BI__builtin_neon_vld1q_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ty = llvm::PointerType::getUnqual(VTy->getElementType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    auto Alignment = CharUnits::fromQuantity(
        BuiltinID == NEON::BI__builtin_neon_vld1_lane_v ? 8 : 16);
    Ops[0] =
        Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0], Alignment);
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vld1_lane");
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = UndefValue::get(Ty);
    Ty = llvm::PointerType::getUnqual(VTy->getElementType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    auto Alignment = CharUnits::fromQuantity(
        BuiltinID == NEON::BI__builtin_neon_vld1_dup_v ? 8 : 16);
    Ops[0] =
        Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0], Alignment);
    llvm::Constant *CI = ConstantInt::get(Int32Ty, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ops[0], CI);
    return EmitNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vst1_lane_v:
  case NEON::BI__builtin_neon_vst1q_lane_v:
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    return Builder.CreateDefaultAlignedStore(Ops[1],
                                             Builder.CreateBitCast(Ops[0], Ty));
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v: {
    llvm::Type *PTy = llvm::PointerType::getUnqual(VTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v: {
    llvm::Type *PTy = llvm::PointerType::getUnqual(VTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v: {
    llvm::Type *PTy = llvm::PointerType::getUnqual(VTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v: {
    llvm::Type *PTy =
      llvm::PointerType::getUnqual(VTy->getElementType());
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v: {
    llvm::Type *PTy =
      llvm::PointerType::getUnqual(VTy->getElementType());
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *PTy =
      llvm::PointerType::getUnqual(VTy->getElementType());
    Ops[1] = Builder.CreateBitCast(Ops[1], PTy);
    llvm::Type *Tys[2] = { VTy, PTy };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    Ops[0] = Builder.CreateBitCast(Ops[0],
                llvm::PointerType::getUnqual(Ops[1]->getType()));
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2lane, Tys);
    Ops.push_back(Ops[1]);
    Ops.erase(Ops.begin()+1);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    Ops[1] = Builder.CreateCall(F, makeArrayRef(Ops).slice(1), "vld2_lane");
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3lane, Tys);
    Ops.push_back(Ops[1]);
    Ops.erase(Ops.begin()+1);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    Ops[1] = Builder.CreateCall(F, makeArrayRef(Ops).slice(1), "vld3_lane");
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4lane, Tys);
    Ops.push_back(Ops[1]);
    Ops.erase(Ops.begin()+1);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateBitCast(Ops[4], Ty);
    Ops[5] = Builder.CreateZExt(Ops[5], Int64Ty);
    Ops[1] = Builder.CreateCall(F, makeArrayRef(Ops).slice(1), "vld4_lane");
    Ty = llvm::PointerType::getUnqual(Ops[1]->getType());
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    llvm::Type *Tys[2] = { VTy, Ops[2]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st2, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[3]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st2lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    llvm::Type *Tys[2] = { VTy, Ops[3]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st3, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[4]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st3lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    llvm::Type *Tys[2] = { VTy, Ops[4]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st4, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    Ops.push_back(Ops[0]);
    Ops.erase(Ops.begin());
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[5]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st4lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i+vi);
        Indices.push_back(i+e+vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2*i+vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::PointerType::getUnqual(Ty));
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<uint32_t, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi*e) >> 1);
        Indices.push_back(((i + vi*e) >> 1)+e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vqtbl1q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl1, Ty),
                        Ops, "vtbl1");
  }
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl2, Ty),
                        Ops, "vtbl2");
  }
  case NEON::BI__builtin_neon_vqtbl3q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl3, Ty),
                        Ops, "vtbl3");
  }
  case NEON::BI__builtin_neon_vqtbl4q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl4, Ty),
                        Ops, "vtbl4");
  }
  case NEON::BI__builtin_neon_vqtbx1q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx1, Ty),
                        Ops, "vtbx1");
  }
  case NEON::BI__builtin_neon_vqtbx2q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx2, Ty),
                        Ops, "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbx3q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx3, Ty),
                        Ops, "vtbx3");
  }
  case NEON::BI__builtin_neon_vqtbx4q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx4, Ty),
                        Ops, "vtbx4");
  }
  case NEON::BI__builtin_neon_vsqadd_v:
  case NEON::BI__builtin_neon_vsqaddq_v: {
    Int = Intrinsic::aarch64_neon_usqadd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vsqadd");
  }
  case NEON::BI__builtin_neon_vuqadd_v:
  case NEON::BI__builtin_neon_vuqaddq_v: {
    Int = Intrinsic::aarch64_neon_suqadd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vuqadd");
  }
  case AArch64::BI__iso_volatile_load8:
  case AArch64::BI__iso_volatile_load16:
  case AArch64::BI__iso_volatile_load32:
  case AArch64::BI__iso_volatile_load64:
    return EmitISOVolatileLoad(E);
  case AArch64::BI__iso_volatile_store8:
  case AArch64::BI__iso_volatile_store16:
  case AArch64::BI__iso_volatile_store32:
  case AArch64::BI__iso_volatile_store64:
    return EmitISOVolatileStore(E);
  case AArch64::BI_BitScanForward:
  case AArch64::BI_BitScanForward64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanForward, E);
  case AArch64::BI_BitScanReverse:
  case AArch64::BI_BitScanReverse64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanReverse, E);
  case AArch64::BI_InterlockedAnd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E);
  case AArch64::BI_InterlockedExchange64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E);
  case AArch64::BI_InterlockedExchangeAdd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E);
  case AArch64::BI_InterlockedExchangeSub64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E);
  case AArch64::BI_InterlockedOr64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E);
  case AArch64::BI_InterlockedXor64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E);
  case AArch64::BI_InterlockedDecrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E);
  case AArch64::BI_InterlockedIncrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E);
  case AArch64::BI_InterlockedExchangeAdd8_acq:
  case AArch64::BI_InterlockedExchangeAdd16_acq:
  case AArch64::BI_InterlockedExchangeAdd_acq:
  case AArch64::BI_InterlockedExchangeAdd64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_acq, E);
  case AArch64::BI_InterlockedExchangeAdd8_rel:
  case AArch64::BI_InterlockedExchangeAdd16_rel:
  case AArch64::BI_InterlockedExchangeAdd_rel:
  case AArch64::BI_InterlockedExchangeAdd64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_rel, E);
  case AArch64::BI_InterlockedExchangeAdd8_nf:
  case AArch64::BI_InterlockedExchangeAdd16_nf:
  case AArch64::BI_InterlockedExchangeAdd_nf:
  case AArch64::BI_InterlockedExchangeAdd64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd_nf, E);
  case AArch64::BI_InterlockedExchange8_acq:
  case AArch64::BI_InterlockedExchange16_acq:
  case AArch64::BI_InterlockedExchange_acq:
  case AArch64::BI_InterlockedExchange64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_acq, E);
  case AArch64::BI_InterlockedExchange8_rel:
  case AArch64::BI_InterlockedExchange16_rel:
  case AArch64::BI_InterlockedExchange_rel:
  case AArch64::BI_InterlockedExchange64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_rel, E);
  case AArch64::BI_InterlockedExchange8_nf:
  case AArch64::BI_InterlockedExchange16_nf:
  case AArch64::BI_InterlockedExchange_nf:
  case AArch64::BI_InterlockedExchange64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange_nf, E);
  case AArch64::BI_InterlockedCompareExchange8_acq:
  case AArch64::BI_InterlockedCompareExchange16_acq:
  case AArch64::BI_InterlockedCompareExchange_acq:
  case AArch64::BI_InterlockedCompareExchange64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_acq, E);
  case AArch64::BI_InterlockedCompareExchange8_rel:
  case AArch64::BI_InterlockedCompareExchange16_rel:
  case AArch64::BI_InterlockedCompareExchange_rel:
  case AArch64::BI_InterlockedCompareExchange64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_rel, E);
  case AArch64::BI_InterlockedCompareExchange8_nf:
  case AArch64::BI_InterlockedCompareExchange16_nf:
  case AArch64::BI_InterlockedCompareExchange_nf:
  case AArch64::BI_InterlockedCompareExchange64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_nf, E);
  case AArch64::BI_InterlockedOr8_acq:
  case AArch64::BI_InterlockedOr16_acq:
  case AArch64::BI_InterlockedOr_acq:
  case AArch64::BI_InterlockedOr64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_acq, E);
  case AArch64::BI_InterlockedOr8_rel:
  case AArch64::BI_InterlockedOr16_rel:
  case AArch64::BI_InterlockedOr_rel:
  case AArch64::BI_InterlockedOr64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_rel, E);
  case AArch64::BI_InterlockedOr8_nf:
  case AArch64::BI_InterlockedOr16_nf:
  case AArch64::BI_InterlockedOr_nf:
  case AArch64::BI_InterlockedOr64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr_nf, E);
  case AArch64::BI_InterlockedXor8_acq:
  case AArch64::BI_InterlockedXor16_acq:
  case AArch64::BI_InterlockedXor_acq:
  case AArch64::BI_InterlockedXor64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_acq, E);
  case AArch64::BI_InterlockedXor8_rel:
  case AArch64::BI_InterlockedXor16_rel:
  case AArch64::BI_InterlockedXor_rel:
  case AArch64::BI_InterlockedXor64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_rel, E);
  case AArch64::BI_InterlockedXor8_nf:
  case AArch64::BI_InterlockedXor16_nf:
  case AArch64::BI_InterlockedXor_nf:
  case AArch64::BI_InterlockedXor64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor_nf, E);
  case AArch64::BI_InterlockedAnd8_acq:
  case AArch64::BI_InterlockedAnd16_acq:
  case AArch64::BI_InterlockedAnd_acq:
  case AArch64::BI_InterlockedAnd64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_acq, E);
  case AArch64::BI_InterlockedAnd8_rel:
  case AArch64::BI_InterlockedAnd16_rel:
  case AArch64::BI_InterlockedAnd_rel:
  case AArch64::BI_InterlockedAnd64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_rel, E);
  case AArch64::BI_InterlockedAnd8_nf:
  case AArch64::BI_InterlockedAnd16_nf:
  case AArch64::BI_InterlockedAnd_nf:
  case AArch64::BI_InterlockedAnd64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd_nf, E);
  case AArch64::BI_InterlockedIncrement16_acq:
  case AArch64::BI_InterlockedIncrement_acq:
  case AArch64::BI_InterlockedIncrement64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_acq, E);
  case AArch64::BI_InterlockedIncrement16_rel:
  case AArch64::BI_InterlockedIncrement_rel:
  case AArch64::BI_InterlockedIncrement64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_rel, E);
  case AArch64::BI_InterlockedIncrement16_nf:
  case AArch64::BI_InterlockedIncrement_nf:
  case AArch64::BI_InterlockedIncrement64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement_nf, E);
  case AArch64::BI_InterlockedDecrement16_acq:
  case AArch64::BI_InterlockedDecrement_acq:
  case AArch64::BI_InterlockedDecrement64_acq:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_acq, E);
  case AArch64::BI_InterlockedDecrement16_rel:
  case AArch64::BI_InterlockedDecrement_rel:
  case AArch64::BI_InterlockedDecrement64_rel:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_rel, E);
  case AArch64::BI_InterlockedDecrement16_nf:
  case AArch64::BI_InterlockedDecrement_nf:
  case AArch64::BI_InterlockedDecrement64_nf:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement_nf, E);

  case AArch64::BI_InterlockedAdd: {
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    Value *Arg1 = EmitScalarExpr(E->getArg(1));
    AtomicRMWInst *RMWI = Builder.CreateAtomicRMW(
      AtomicRMWInst::Add, Arg0, Arg1,
      llvm::AtomicOrdering::SequentiallyConsistent);
    return Builder.CreateAdd(RMWI, Arg1);
  }
  }
}

llvm::Value *CodeGenFunction::
BuildVector(ArrayRef<llvm::Value*> Ops) {
  assert((Ops.size() & (Ops.size() - 1)) == 0 &&
         "Not a power-of-two sized vector!");
  bool AllConstants = true;
  for (unsigned i = 0, e = Ops.size(); i != e && AllConstants; ++i)
    AllConstants &= isa<Constant>(Ops[i]);

  // If this is a constant vector, create a ConstantVector.
  if (AllConstants) {
    SmallVector<llvm::Constant*, 16> CstOps;
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      CstOps.push_back(cast<Constant>(Ops[i]));
    return llvm::ConstantVector::get(CstOps);
  }

  // Otherwise, insertelement the values to build the vector.
  Value *Result =
    llvm::UndefValue::get(llvm::VectorType::get(Ops[0]->getType(), Ops.size()));

  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    Result = Builder.CreateInsertElement(Result, Ops[i], Builder.getInt32(i));

  return Result;
}

// Convert the mask from an integer type to a vector of i1.
static Value *getMaskVecValue(CodeGenFunction &CGF, Value *Mask,
                              unsigned NumElts) {

  llvm::VectorType *MaskTy = llvm::VectorType::get(CGF.Builder.getInt1Ty(),
                         cast<IntegerType>(Mask->getType())->getBitWidth());
  Value *MaskVec = CGF.Builder.CreateBitCast(Mask, MaskTy);

  // If we have less than 8 elements, then the starting mask was an i8 and
  // we need to extract down to the right number of elements.
  if (NumElts < 8) {
    uint32_t Indices[4];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;
    MaskVec = CGF.Builder.CreateShuffleVector(MaskVec, MaskVec,
                                             makeArrayRef(Indices, NumElts),
                                             "extract");
  }
  return MaskVec;
}

static Value *EmitX86MaskedStore(CodeGenFunction &CGF,
                                 ArrayRef<Value *> Ops,
                                 unsigned Align) {
  // Cast the pointer to right type.
  Value *Ptr = CGF.Builder.CreateBitCast(Ops[0],
                               llvm::PointerType::getUnqual(Ops[1]->getType()));

  Value *MaskVec = getMaskVecValue(CGF, Ops[2],
                                   Ops[1]->getType()->getVectorNumElements());

  return CGF.Builder.CreateMaskedStore(Ops[1], Ptr, Align, MaskVec);
}

static Value *EmitX86MaskedLoad(CodeGenFunction &CGF,
                                ArrayRef<Value *> Ops, unsigned Align) {
  // Cast the pointer to right type.
  Value *Ptr = CGF.Builder.CreateBitCast(Ops[0],
                               llvm::PointerType::getUnqual(Ops[1]->getType()));

  Value *MaskVec = getMaskVecValue(CGF, Ops[2],
                                   Ops[1]->getType()->getVectorNumElements());

  return CGF.Builder.CreateMaskedLoad(Ptr, Align, MaskVec, Ops[1]);
}

static Value *EmitX86ExpandLoad(CodeGenFunction &CGF,
                                ArrayRef<Value *> Ops) {
  llvm::Type *ResultTy = Ops[1]->getType();
  llvm::Type *PtrTy = ResultTy->getVectorElementType();

  // Cast the pointer to element type.
  Value *Ptr = CGF.Builder.CreateBitCast(Ops[0],
                                         llvm::PointerType::getUnqual(PtrTy));

  Value *MaskVec = getMaskVecValue(CGF, Ops[2],
                                   ResultTy->getVectorNumElements());

  llvm::Function *F = CGF.CGM.getIntrinsic(Intrinsic::masked_expandload,
                                           ResultTy);
  return CGF.Builder.CreateCall(F, { Ptr, MaskVec, Ops[1] });
}

static Value *EmitX86CompressStore(CodeGenFunction &CGF,
                                   ArrayRef<Value *> Ops) {
  llvm::Type *ResultTy = Ops[1]->getType();
  llvm::Type *PtrTy = ResultTy->getVectorElementType();

  // Cast the pointer to element type.
  Value *Ptr = CGF.Builder.CreateBitCast(Ops[0],
                                         llvm::PointerType::getUnqual(PtrTy));

  Value *MaskVec = getMaskVecValue(CGF, Ops[2],
                                   ResultTy->getVectorNumElements());

  llvm::Function *F = CGF.CGM.getIntrinsic(Intrinsic::masked_compressstore,
                                           ResultTy);
  return CGF.Builder.CreateCall(F, { Ops[1], Ptr, MaskVec });
}

static Value *EmitX86MaskLogic(CodeGenFunction &CGF, Instruction::BinaryOps Opc,
                              ArrayRef<Value *> Ops,
                              bool InvertLHS = false) {
  unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
  Value *LHS = getMaskVecValue(CGF, Ops[0], NumElts);
  Value *RHS = getMaskVecValue(CGF, Ops[1], NumElts);

  if (InvertLHS)
    LHS = CGF.Builder.CreateNot(LHS);

  return CGF.Builder.CreateBitCast(CGF.Builder.CreateBinOp(Opc, LHS, RHS),
                                   Ops[0]->getType());
}

static Value *EmitX86FunnelShift(CodeGenFunction &CGF, Value *Op0, Value *Op1,
                                 Value *Amt, bool IsRight) {
  llvm::Type *Ty = Op0->getType();

  // Amount may be scalar immediate, in which case create a splat vector.
  // Funnel shifts amounts are treated as modulo and types are all power-of-2 so
  // we only care about the lowest log2 bits anyway.
  if (Amt->getType() != Ty) {
    unsigned NumElts = Ty->getVectorNumElements();
    Amt = CGF.Builder.CreateIntCast(Amt, Ty->getScalarType(), false);
    Amt = CGF.Builder.CreateVectorSplat(NumElts, Amt);
  }

  unsigned IID = IsRight ? Intrinsic::fshr : Intrinsic::fshl;
  Value *F = CGF.CGM.getIntrinsic(IID, Ty);
  return CGF.Builder.CreateCall(F, {Op0, Op1, Amt});
}

static Value *EmitX86Select(CodeGenFunction &CGF,
                            Value *Mask, Value *Op0, Value *Op1) {

  // If the mask is all ones just return first argument.
  if (const auto *C = dyn_cast<Constant>(Mask))
    if (C->isAllOnesValue())
      return Op0;

  Mask = getMaskVecValue(CGF, Mask, Op0->getType()->getVectorNumElements());

  return CGF.Builder.CreateSelect(Mask, Op0, Op1);
}

static Value *EmitX86ScalarSelect(CodeGenFunction &CGF,
                                  Value *Mask, Value *Op0, Value *Op1) {
  // If the mask is all ones just return first argument.
  if (const auto *C = dyn_cast<Constant>(Mask))
    if (C->isAllOnesValue())
      return Op0;

  llvm::VectorType *MaskTy =
    llvm::VectorType::get(CGF.Builder.getInt1Ty(),
                          Mask->getType()->getIntegerBitWidth());
  Mask = CGF.Builder.CreateBitCast(Mask, MaskTy);
  Mask = CGF.Builder.CreateExtractElement(Mask, (uint64_t)0);
  return CGF.Builder.CreateSelect(Mask, Op0, Op1);
}

static Value *EmitX86MaskedCompareResult(CodeGenFunction &CGF, Value *Cmp,
                                         unsigned NumElts, Value *MaskIn) {
  if (MaskIn) {
    const auto *C = dyn_cast<Constant>(MaskIn);
    if (!C || !C->isAllOnesValue())
      Cmp = CGF.Builder.CreateAnd(Cmp, getMaskVecValue(CGF, MaskIn, NumElts));
  }

  if (NumElts < 8) {
    uint32_t Indices[8];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;
    for (unsigned i = NumElts; i != 8; ++i)
      Indices[i] = i % NumElts + NumElts;
    Cmp = CGF.Builder.CreateShuffleVector(
        Cmp, llvm::Constant::getNullValue(Cmp->getType()), Indices);
  }

  return CGF.Builder.CreateBitCast(Cmp,
                                   IntegerType::get(CGF.getLLVMContext(),
                                                    std::max(NumElts, 8U)));
}

static Value *EmitX86MaskedCompare(CodeGenFunction &CGF, unsigned CC,
                                   bool Signed, ArrayRef<Value *> Ops) {
  assert((Ops.size() == 2 || Ops.size() == 4) &&
         "Unexpected number of arguments");
  unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
  Value *Cmp;

  if (CC == 3) {
    Cmp = Constant::getNullValue(
                       llvm::VectorType::get(CGF.Builder.getInt1Ty(), NumElts));
  } else if (CC == 7) {
    Cmp = Constant::getAllOnesValue(
                       llvm::VectorType::get(CGF.Builder.getInt1Ty(), NumElts));
  } else {
    ICmpInst::Predicate Pred;
    switch (CC) {
    default: llvm_unreachable("Unknown condition code");
    case 0: Pred = ICmpInst::ICMP_EQ;  break;
    case 1: Pred = Signed ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT; break;
    case 2: Pred = Signed ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE; break;
    case 4: Pred = ICmpInst::ICMP_NE;  break;
    case 5: Pred = Signed ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE; break;
    case 6: Pred = Signed ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT; break;
    }
    Cmp = CGF.Builder.CreateICmp(Pred, Ops[0], Ops[1]);
  }

  Value *MaskIn = nullptr;
  if (Ops.size() == 4)
    MaskIn = Ops[3];

  return EmitX86MaskedCompareResult(CGF, Cmp, NumElts, MaskIn);
}

static Value *EmitX86ConvertToMask(CodeGenFunction &CGF, Value *In) {
  Value *Zero = Constant::getNullValue(In->getType());
  return EmitX86MaskedCompare(CGF, 1, true, { In, Zero });
}

static Value *EmitX86Abs(CodeGenFunction &CGF, ArrayRef<Value *> Ops) {

  llvm::Type *Ty = Ops[0]->getType();
  Value *Zero = llvm::Constant::getNullValue(Ty);
  Value *Sub = CGF.Builder.CreateSub(Zero, Ops[0]);
  Value *Cmp = CGF.Builder.CreateICmp(ICmpInst::ICMP_SGT, Ops[0], Zero);
  Value *Res = CGF.Builder.CreateSelect(Cmp, Ops[0], Sub);
  return Res;
}

static Value *EmitX86MinMax(CodeGenFunction &CGF, ICmpInst::Predicate Pred,
                            ArrayRef<Value *> Ops) {
  Value *Cmp = CGF.Builder.CreateICmp(Pred, Ops[0], Ops[1]);
  Value *Res = CGF.Builder.CreateSelect(Cmp, Ops[0], Ops[1]);

  assert(Ops.size() == 2);
  return Res;
}

// Lowers X86 FMA intrinsics to IR.
static Value *EmitX86FMAExpr(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                             unsigned BuiltinID, bool IsAddSub) {

  bool Subtract = false;
  Intrinsic::ID IID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  default: break;
  case clang::X86::BI__builtin_ia32_vfmsubps512_mask3:
    Subtract = true;
    LLVM_FALLTHROUGH;
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmadd_ps_512; break;
  case clang::X86::BI__builtin_ia32_vfmsubpd512_mask3:
    Subtract = true;
    LLVM_FALLTHROUGH;
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmadd_pd_512; break;
  case clang::X86::BI__builtin_ia32_vfmsubaddps512_mask3:
    Subtract = true;
    LLVM_FALLTHROUGH;
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmaddsub_ps_512;
    break;
  case clang::X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    Subtract = true;
    LLVM_FALLTHROUGH;
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmaddsub_pd_512;
    break;
  }

  Value *A = Ops[0];
  Value *B = Ops[1];
  Value *C = Ops[2];

  if (Subtract)
    C = CGF.Builder.CreateFNeg(C);

  Value *Res;

  // Only handle in case of _MM_FROUND_CUR_DIRECTION/4 (no rounding).
  if (IID != Intrinsic::not_intrinsic &&
      cast<llvm::ConstantInt>(Ops.back())->getZExtValue() != (uint64_t)4) {
    Function *Intr = CGF.CGM.getIntrinsic(IID);
    Res = CGF.Builder.CreateCall(Intr, {A, B, C, Ops.back() });
  } else {
    llvm::Type *Ty = A->getType();
    Function *FMA = CGF.CGM.getIntrinsic(Intrinsic::fma, Ty);
    Res = CGF.Builder.CreateCall(FMA, {A, B, C} );

    if (IsAddSub) {
      // Negate even elts in C using a mask.
      unsigned NumElts = Ty->getVectorNumElements();
      SmallVector<uint32_t, 16> Indices(NumElts);
      for (unsigned i = 0; i != NumElts; ++i)
        Indices[i] = i + (i % 2) * NumElts;

      Value *NegC = CGF.Builder.CreateFNeg(C);
      Value *FMSub = CGF.Builder.CreateCall(FMA, {A, B, NegC} );
      Res = CGF.Builder.CreateShuffleVector(FMSub, Res, Indices);
    }
  }

  // Handle any required masking.
  Value *MaskFalseVal = nullptr;
  switch (BuiltinID) {
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask:
    MaskFalseVal = Ops[0];
    break;
  case clang::X86::BI__builtin_ia32_vfmaddps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
    MaskFalseVal = Constant::getNullValue(Ops[0]->getType());
    break;
  case clang::X86::BI__builtin_ia32_vfmsubps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    MaskFalseVal = Ops[2];
    break;
  }

  if (MaskFalseVal)
    return EmitX86Select(CGF, Ops[3], Res, MaskFalseVal);

  return Res;
}

static Value *
EmitScalarFMAExpr(CodeGenFunction &CGF, MutableArrayRef<Value *> Ops,
                  Value *Upper, bool ZeroMask = false, unsigned PTIdx = 0,
                  bool NegAcc = false) {
  unsigned Rnd = 4;
  if (Ops.size() > 4)
    Rnd = cast<llvm::ConstantInt>(Ops[4])->getZExtValue();

  if (NegAcc)
    Ops[2] = CGF.Builder.CreateFNeg(Ops[2]);

  Ops[0] = CGF.Builder.CreateExtractElement(Ops[0], (uint64_t)0);
  Ops[1] = CGF.Builder.CreateExtractElement(Ops[1], (uint64_t)0);
  Ops[2] = CGF.Builder.CreateExtractElement(Ops[2], (uint64_t)0);
  Value *Res;
  if (Rnd != 4) {
    Intrinsic::ID IID = Ops[0]->getType()->getPrimitiveSizeInBits() == 32 ?
                        Intrinsic::x86_avx512_vfmadd_f32 :
                        Intrinsic::x86_avx512_vfmadd_f64;
    Res = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IID),
                                 {Ops[0], Ops[1], Ops[2], Ops[4]});
  } else {
    Function *FMA = CGF.CGM.getIntrinsic(Intrinsic::fma, Ops[0]->getType());
    Res = CGF.Builder.CreateCall(FMA, Ops.slice(0, 3));
  }
  // If we have more than 3 arguments, we need to do masking.
  if (Ops.size() > 3) {
    Value *PassThru = ZeroMask ? Constant::getNullValue(Res->getType())
                               : Ops[PTIdx];

    // If we negated the accumulator and the its the PassThru value we need to
    // bypass the negate. Conveniently Upper should be the same thing in this
    // case.
    if (NegAcc && PTIdx == 2)
      PassThru = CGF.Builder.CreateExtractElement(Upper, (uint64_t)0);

    Res = EmitX86ScalarSelect(CGF, Ops[3], Res, PassThru);
  }
  return CGF.Builder.CreateInsertElement(Upper, Res, (uint64_t)0);
}

static Value *EmitX86Muldq(CodeGenFunction &CGF, bool IsSigned,
                           ArrayRef<Value *> Ops) {
  llvm::Type *Ty = Ops[0]->getType();
  // Arguments have a vXi32 type so cast to vXi64.
  Ty = llvm::VectorType::get(CGF.Int64Ty,
                             Ty->getPrimitiveSizeInBits() / 64);
  Value *LHS = CGF.Builder.CreateBitCast(Ops[0], Ty);
  Value *RHS = CGF.Builder.CreateBitCast(Ops[1], Ty);

  if (IsSigned) {
    // Shift left then arithmetic shift right.
    Constant *ShiftAmt = ConstantInt::get(Ty, 32);
    LHS = CGF.Builder.CreateShl(LHS, ShiftAmt);
    LHS = CGF.Builder.CreateAShr(LHS, ShiftAmt);
    RHS = CGF.Builder.CreateShl(RHS, ShiftAmt);
    RHS = CGF.Builder.CreateAShr(RHS, ShiftAmt);
  } else {
    // Clear the upper bits.
    Constant *Mask = ConstantInt::get(Ty, 0xffffffff);
    LHS = CGF.Builder.CreateAnd(LHS, Mask);
    RHS = CGF.Builder.CreateAnd(RHS, Mask);
  }

  return CGF.Builder.CreateMul(LHS, RHS);
}

// Emit a masked pternlog intrinsic. This only exists because the header has to
// use a macro and we aren't able to pass the input argument to a pternlog
// builtin and a select builtin without evaluating it twice.
static Value *EmitX86Ternlog(CodeGenFunction &CGF, bool ZeroMask,
                             ArrayRef<Value *> Ops) {
  llvm::Type *Ty = Ops[0]->getType();

  unsigned VecWidth = Ty->getPrimitiveSizeInBits();
  unsigned EltWidth = Ty->getScalarSizeInBits();
  Intrinsic::ID IID;
  if (VecWidth == 128 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_128;
  else if (VecWidth == 256 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_256;
  else if (VecWidth == 512 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_512;
  else if (VecWidth == 128 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_128;
  else if (VecWidth == 256 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_256;
  else if (VecWidth == 512 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_512;
  else
    llvm_unreachable("Unexpected intrinsic");

  Value *Ternlog = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IID),
                                          Ops.drop_back());
  Value *PassThru = ZeroMask ? ConstantAggregateZero::get(Ty) : Ops[0];
  return EmitX86Select(CGF, Ops[4], Ternlog, PassThru);
}

static Value *EmitX86SExtMask(CodeGenFunction &CGF, Value *Op,
                              llvm::Type *DstTy) {
  unsigned NumberOfElements = DstTy->getVectorNumElements();
  Value *Mask = getMaskVecValue(CGF, Op, NumberOfElements);
  return CGF.Builder.CreateSExt(Mask, DstTy, "vpmovm2");
}

// Emit addition or subtraction with signed/unsigned saturation.
static Value *EmitX86AddSubSatExpr(CodeGenFunction &CGF,
                                   ArrayRef<Value *> Ops, bool IsSigned,
                                   bool IsAddition) {
  Intrinsic::ID IID =
      IsSigned ? (IsAddition ? Intrinsic::sadd_sat : Intrinsic::ssub_sat)
               : (IsAddition ? Intrinsic::uadd_sat : Intrinsic::usub_sat);
  llvm::Function *F = CGF.CGM.getIntrinsic(IID, Ops[0]->getType());
  return CGF.Builder.CreateCall(F, {Ops[0], Ops[1]});
}

Value *CodeGenFunction::EmitX86CpuIs(const CallExpr *E) {
  const Expr *CPUExpr = E->getArg(0)->IgnoreParenCasts();
  StringRef CPUStr = cast<clang::StringLiteral>(CPUExpr)->getString();
  return EmitX86CpuIs(CPUStr);
}

Value *CodeGenFunction::EmitX86CpuIs(StringRef CPUStr) {

  llvm::Type *Int32Ty = Builder.getInt32Ty();

  // Matching the struct layout from the compiler-rt/libgcc structure that is
  // filled in:
  // unsigned int __cpu_vendor;
  // unsigned int __cpu_type;
  // unsigned int __cpu_subtype;
  // unsigned int __cpu_features[1];
  llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty, Int32Ty,
                                          llvm::ArrayType::get(Int32Ty, 1));

  // Grab the global __cpu_model.
  llvm::Constant *CpuModel = CGM.CreateRuntimeVariable(STy, "__cpu_model");
  cast<llvm::GlobalValue>(CpuModel)->setDSOLocal(true);

  // Calculate the index needed to access the correct field based on the
  // range. Also adjust the expected value.
  unsigned Index;
  unsigned Value;
  std::tie(Index, Value) = StringSwitch<std::pair<unsigned, unsigned>>(CPUStr)
#define X86_VENDOR(ENUM, STRING)                                               \
  .Case(STRING, {0u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE_COMPAT_WITH_ALIAS(ARCHNAME, ENUM, STR, ALIAS)             \
  .Cases(STR, ALIAS, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE_COMPAT(ARCHNAME, ENUM, STR)                               \
  .Case(STR, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE_COMPAT(ARCHNAME, ENUM, STR)                            \
  .Case(STR, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#include "llvm/Support/X86TargetParser.def"
                               .Default({0, 0});
  assert(Value != 0 && "Invalid CPUStr passed to CpuIs");

  // Grab the appropriate field from __cpu_model.
  llvm::Value *Idxs[] = {ConstantInt::get(Int32Ty, 0),
                         ConstantInt::get(Int32Ty, Index)};
  llvm::Value *CpuValue = Builder.CreateGEP(STy, CpuModel, Idxs);
  CpuValue = Builder.CreateAlignedLoad(CpuValue, CharUnits::fromQuantity(4));

  // Check the value of the field against the requested value.
  return Builder.CreateICmpEQ(CpuValue,
                                  llvm::ConstantInt::get(Int32Ty, Value));
}

Value *CodeGenFunction::EmitX86CpuSupports(const CallExpr *E) {
  const Expr *FeatureExpr = E->getArg(0)->IgnoreParenCasts();
  StringRef FeatureStr = cast<StringLiteral>(FeatureExpr)->getString();
  return EmitX86CpuSupports(FeatureStr);
}

uint64_t
CodeGenFunction::GetX86CpuSupportsMask(ArrayRef<StringRef> FeatureStrs) {
  // Processor features and mapping to processor feature value.
  uint64_t FeaturesMask = 0;
  for (const StringRef &FeatureStr : FeatureStrs) {
    unsigned Feature =
        StringSwitch<unsigned>(FeatureStr)
#define X86_FEATURE_COMPAT(VAL, ENUM, STR) .Case(STR, VAL)
#include "llvm/Support/X86TargetParser.def"
        ;
    FeaturesMask |= (1ULL << Feature);
  }
  return FeaturesMask;
}

Value *CodeGenFunction::EmitX86CpuSupports(ArrayRef<StringRef> FeatureStrs) {
  return EmitX86CpuSupports(GetX86CpuSupportsMask(FeatureStrs));
}

llvm::Value *CodeGenFunction::EmitX86CpuSupports(uint64_t FeaturesMask) {
  uint32_t Features1 = Lo_32(FeaturesMask);
  uint32_t Features2 = Hi_32(FeaturesMask);

  Value *Result = Builder.getTrue();

  if (Features1 != 0) {
    // Matching the struct layout from the compiler-rt/libgcc structure that is
    // filled in:
    // unsigned int __cpu_vendor;
    // unsigned int __cpu_type;
    // unsigned int __cpu_subtype;
    // unsigned int __cpu_features[1];
    llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty, Int32Ty,
                                            llvm::ArrayType::get(Int32Ty, 1));

    // Grab the global __cpu_model.
    llvm::Constant *CpuModel = CGM.CreateRuntimeVariable(STy, "__cpu_model");
    cast<llvm::GlobalValue>(CpuModel)->setDSOLocal(true);

    // Grab the first (0th) element from the field __cpu_features off of the
    // global in the struct STy.
    Value *Idxs[] = {Builder.getInt32(0), Builder.getInt32(3),
                     Builder.getInt32(0)};
    Value *CpuFeatures = Builder.CreateGEP(STy, CpuModel, Idxs);
    Value *Features =
        Builder.CreateAlignedLoad(CpuFeatures, CharUnits::fromQuantity(4));

    // Check the value of the bit corresponding to the feature requested.
    Value *Mask = Builder.getInt32(Features1);
    Value *Bitset = Builder.CreateAnd(Features, Mask);
    Value *Cmp = Builder.CreateICmpEQ(Bitset, Mask);
    Result = Builder.CreateAnd(Result, Cmp);
  }

  if (Features2 != 0) {
    llvm::Constant *CpuFeatures2 = CGM.CreateRuntimeVariable(Int32Ty,
                                                             "__cpu_features2");
    cast<llvm::GlobalValue>(CpuFeatures2)->setDSOLocal(true);

    Value *Features =
        Builder.CreateAlignedLoad(CpuFeatures2, CharUnits::fromQuantity(4));

    // Check the value of the bit corresponding to the feature requested.
    Value *Mask = Builder.getInt32(Features2);
    Value *Bitset = Builder.CreateAnd(Features, Mask);
    Value *Cmp = Builder.CreateICmpEQ(Bitset, Mask);
    Result = Builder.CreateAnd(Result, Cmp);
  }

  return Result;
}

Value *CodeGenFunction::EmitX86CpuInit() {
  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy,
                                                    /*Variadic*/ false);
  llvm::Constant *Func = CGM.CreateRuntimeFunction(FTy, "__cpu_indicator_init");
  cast<llvm::GlobalValue>(Func)->setDSOLocal(true);
  cast<llvm::GlobalValue>(Func)->setDLLStorageClass(
      llvm::GlobalValue::DefaultStorageClass);
  return Builder.CreateCall(Func);
}

Value *CodeGenFunction::EmitX86BuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  if (BuiltinID == X86::BI__builtin_cpu_is)
    return EmitX86CpuIs(E);
  if (BuiltinID == X86::BI__builtin_cpu_supports)
    return EmitX86CpuSupports(E);
  if (BuiltinID == X86::BI__builtin_cpu_init)
    return EmitX86CpuInit();

  SmallVector<Value*, 4> Ops;

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++) {
    // If this is a normal argument, just emit it as a scalar.
    if ((ICEArguments & (1 << i)) == 0) {
      Ops.push_back(EmitScalarExpr(E->getArg(i)));
      continue;
    }

    // If this is required to be a constant, constant fold it so that we know
    // that the generated intrinsic gets a ConstantInt.
    llvm::APSInt Result;
    bool IsConst = E->getArg(i)->isIntegerConstantExpr(Result, getContext());
    assert(IsConst && "Constant arg isn't actually constant?"); (void)IsConst;
    Ops.push_back(llvm::ConstantInt::get(getLLVMContext(), Result));
  }

  // These exist so that the builtin that takes an immediate can be bounds
  // checked by clang to avoid passing bad immediates to the backend. Since
  // AVX has a larger immediate than SSE we would need separate builtins to
  // do the different bounds checking. Rather than create a clang specific
  // SSE only builtin, this implements eight separate builtins to match gcc
  // implementation.
  auto getCmpIntrinsicCall = [this, &Ops](Intrinsic::ID ID, unsigned Imm) {
    Ops.push_back(llvm::ConstantInt::get(Int8Ty, Imm));
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops);
  };

  // For the vector forms of FP comparisons, translate the builtins directly to
  // IR.
  // TODO: The builtins could be removed if the SSE header files used vector
  // extension comparisons directly (vector ordered/unordered may need
  // additional support via __builtin_isnan()).
  auto getVectorFCmpIR = [this, &Ops](CmpInst::Predicate Pred) {
    Value *Cmp = Builder.CreateFCmp(Pred, Ops[0], Ops[1]);
    llvm::VectorType *FPVecTy = cast<llvm::VectorType>(Ops[0]->getType());
    llvm::VectorType *IntVecTy = llvm::VectorType::getInteger(FPVecTy);
    Value *Sext = Builder.CreateSExt(Cmp, IntVecTy);
    return Builder.CreateBitCast(Sext, FPVecTy);
  };

  switch (BuiltinID) {
  default: return nullptr;
  case X86::BI_mm_prefetch: {
    Value *Address = Ops[0];
    ConstantInt *C = cast<ConstantInt>(Ops[1]);
    Value *RW = ConstantInt::get(Int32Ty, (C->getZExtValue() >> 2) & 0x1);
    Value *Locality = ConstantInt::get(Int32Ty, C->getZExtValue() & 0x3);
    Value *Data = ConstantInt::get(Int32Ty, 1);
    Value *F = CGM.getIntrinsic(Intrinsic::prefetch);
    return Builder.CreateCall(F, {Address, RW, Locality, Data});
  }
  case X86::BI_mm_clflush: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_clflush),
                              Ops[0]);
  }
  case X86::BI_mm_lfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_lfence));
  }
  case X86::BI_mm_mfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_mfence));
  }
  case X86::BI_mm_sfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_sfence));
  }
  case X86::BI_mm_pause: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_pause));
  }
  case X86::BI__rdtsc: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_rdtsc));
  }
  case X86::BI__builtin_ia32_rdtscp: {
    Value *Call = Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_rdtscp));
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 1),
                                      Ops[0]);
    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_lzcnt_u16:
  case X86::BI__builtin_ia32_lzcnt_u32:
  case X86::BI__builtin_ia32_lzcnt_u64: {
    Value *F = CGM.getIntrinsic(Intrinsic::ctlz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_tzcnt_u16:
  case X86::BI__builtin_ia32_tzcnt_u32:
  case X86::BI__builtin_ia32_tzcnt_u64: {
    Value *F = CGM.getIntrinsic(Intrinsic::cttz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_undef128:
  case X86::BI__builtin_ia32_undef256:
  case X86::BI__builtin_ia32_undef512:
    // The x86 definition of "undef" is not the same as the LLVM definition
    // (PR32176). We leave optimizing away an unnecessary zero constant to the
    // IR optimizer and backend.
    // TODO: If we had a "freeze" IR instruction to generate a fixed undef
    // value, we should use that here instead of a zero.
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  case X86::BI__builtin_ia32_vec_init_v8qi:
  case X86::BI__builtin_ia32_vec_init_v4hi:
  case X86::BI__builtin_ia32_vec_init_v2si:
    return Builder.CreateBitCast(BuildVector(Ops),
                                 llvm::Type::getX86_MMXTy(getLLVMContext()));
  case X86::BI__builtin_ia32_vec_ext_v2si:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vec_ext_v32qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
  case X86::BI__builtin_ia32_vec_ext_v4di: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    uint64_t Index = cast<ConstantInt>(Ops[1])->getZExtValue();
    Index &= NumElts - 1;
    // These builtins exist so we can ensure the index is an ICE and in range.
    // Otherwise we could just do this in the header file.
    return Builder.CreateExtractElement(Ops[0], Index);
  }
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vec_set_v32qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
  case X86::BI__builtin_ia32_vec_set_v4di: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    unsigned Index = cast<ConstantInt>(Ops[2])->getZExtValue();
    Index &= NumElts - 1;
    // These builtins exist so we can ensure the index is an ICE and in range.
    // Otherwise we could just do this in the header file.
    return Builder.CreateInsertElement(Ops[0], Ops[1], Index);
  }
  case X86::BI_mm_setcsr:
  case X86::BI__builtin_ia32_ldmxcsr: {
    Address Tmp = CreateMemTemp(E->getArg(0)->getType());
    Builder.CreateStore(Ops[0], Tmp);
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_ldmxcsr),
                          Builder.CreateBitCast(Tmp.getPointer(), Int8PtrTy));
  }
  case X86::BI_mm_getcsr:
  case X86::BI__builtin_ia32_stmxcsr: {
    Address Tmp = CreateMemTemp(E->getType());
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_stmxcsr),
                       Builder.CreateBitCast(Tmp.getPointer(), Int8PtrTy));
    return Builder.CreateLoad(Tmp, "stmxcsr");
  }
  case X86::BI__builtin_ia32_xsave:
  case X86::BI__builtin_ia32_xsave64:
  case X86::BI__builtin_ia32_xrstor:
  case X86::BI__builtin_ia32_xrstor64:
  case X86::BI__builtin_ia32_xsaveopt:
  case X86::BI__builtin_ia32_xsaveopt64:
  case X86::BI__builtin_ia32_xrstors:
  case X86::BI__builtin_ia32_xrstors64:
  case X86::BI__builtin_ia32_xsavec:
  case X86::BI__builtin_ia32_xsavec64:
  case X86::BI__builtin_ia32_xsaves:
  case X86::BI__builtin_ia32_xsaves64: {
    Intrinsic::ID ID;
#define INTRINSIC_X86_XSAVE_ID(NAME) \
    case X86::BI__builtin_ia32_##NAME: \
      ID = Intrinsic::x86_##NAME; \
      break
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    INTRINSIC_X86_XSAVE_ID(xsave);
    INTRINSIC_X86_XSAVE_ID(xsave64);
    INTRINSIC_X86_XSAVE_ID(xrstor);
    INTRINSIC_X86_XSAVE_ID(xrstor64);
    INTRINSIC_X86_XSAVE_ID(xsaveopt);
    INTRINSIC_X86_XSAVE_ID(xsaveopt64);
    INTRINSIC_X86_XSAVE_ID(xrstors);
    INTRINSIC_X86_XSAVE_ID(xrstors64);
    INTRINSIC_X86_XSAVE_ID(xsavec);
    INTRINSIC_X86_XSAVE_ID(xsavec64);
    INTRINSIC_X86_XSAVE_ID(xsaves);
    INTRINSIC_X86_XSAVE_ID(xsaves64);
    }
#undef INTRINSIC_X86_XSAVE_ID
    Value *Mhi = Builder.CreateTrunc(
      Builder.CreateLShr(Ops[1], ConstantInt::get(Int64Ty, 32)), Int32Ty);
    Value *Mlo = Builder.CreateTrunc(Ops[1], Int32Ty);
    Ops[1] = Mhi;
    Ops.push_back(Mlo);
    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
  }
  case X86::BI__builtin_ia32_storedqudi128_mask:
  case X86::BI__builtin_ia32_storedqusi128_mask:
  case X86::BI__builtin_ia32_storedquhi128_mask:
  case X86::BI__builtin_ia32_storedquqi128_mask:
  case X86::BI__builtin_ia32_storeupd128_mask:
  case X86::BI__builtin_ia32_storeups128_mask:
  case X86::BI__builtin_ia32_storedqudi256_mask:
  case X86::BI__builtin_ia32_storedqusi256_mask:
  case X86::BI__builtin_ia32_storedquhi256_mask:
  case X86::BI__builtin_ia32_storedquqi256_mask:
  case X86::BI__builtin_ia32_storeupd256_mask:
  case X86::BI__builtin_ia32_storeups256_mask:
  case X86::BI__builtin_ia32_storedqudi512_mask:
  case X86::BI__builtin_ia32_storedqusi512_mask:
  case X86::BI__builtin_ia32_storedquhi512_mask:
  case X86::BI__builtin_ia32_storedquqi512_mask:
  case X86::BI__builtin_ia32_storeupd512_mask:
  case X86::BI__builtin_ia32_storeups512_mask:
    return EmitX86MaskedStore(*this, Ops, 1);

  case X86::BI__builtin_ia32_storess128_mask:
  case X86::BI__builtin_ia32_storesd128_mask: {
    return EmitX86MaskedStore(*this, Ops, 1);
  }
  case X86::BI__builtin_ia32_vpopcntb_128:
  case X86::BI__builtin_ia32_vpopcntd_128:
  case X86::BI__builtin_ia32_vpopcntq_128:
  case X86::BI__builtin_ia32_vpopcntw_128:
  case X86::BI__builtin_ia32_vpopcntb_256:
  case X86::BI__builtin_ia32_vpopcntd_256:
  case X86::BI__builtin_ia32_vpopcntq_256:
  case X86::BI__builtin_ia32_vpopcntw_256:
  case X86::BI__builtin_ia32_vpopcntb_512:
  case X86::BI__builtin_ia32_vpopcntd_512:
  case X86::BI__builtin_ia32_vpopcntq_512:
  case X86::BI__builtin_ia32_vpopcntw_512: {
    llvm::Type *ResultType = ConvertType(E->getType());
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, Ops);
  }
  case X86::BI__builtin_ia32_cvtmask2b128:
  case X86::BI__builtin_ia32_cvtmask2b256:
  case X86::BI__builtin_ia32_cvtmask2b512:
  case X86::BI__builtin_ia32_cvtmask2w128:
  case X86::BI__builtin_ia32_cvtmask2w256:
  case X86::BI__builtin_ia32_cvtmask2w512:
  case X86::BI__builtin_ia32_cvtmask2d128:
  case X86::BI__builtin_ia32_cvtmask2d256:
  case X86::BI__builtin_ia32_cvtmask2d512:
  case X86::BI__builtin_ia32_cvtmask2q128:
  case X86::BI__builtin_ia32_cvtmask2q256:
  case X86::BI__builtin_ia32_cvtmask2q512:
    return EmitX86SExtMask(*this, Ops[0], ConvertType(E->getType()));

  case X86::BI__builtin_ia32_cvtb2mask128:
  case X86::BI__builtin_ia32_cvtb2mask256:
  case X86::BI__builtin_ia32_cvtb2mask512:
  case X86::BI__builtin_ia32_cvtw2mask128:
  case X86::BI__builtin_ia32_cvtw2mask256:
  case X86::BI__builtin_ia32_cvtw2mask512:
  case X86::BI__builtin_ia32_cvtd2mask128:
  case X86::BI__builtin_ia32_cvtd2mask256:
  case X86::BI__builtin_ia32_cvtd2mask512:
  case X86::BI__builtin_ia32_cvtq2mask128:
  case X86::BI__builtin_ia32_cvtq2mask256:
  case X86::BI__builtin_ia32_cvtq2mask512:
    return EmitX86ConvertToMask(*this, Ops[0]);

  case X86::BI__builtin_ia32_vfmaddss3:
  case X86::BI__builtin_ia32_vfmaddsd3:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
    return EmitScalarFMAExpr(*this, Ops, Ops[0]);
  case X86::BI__builtin_ia32_vfmaddss:
  case X86::BI__builtin_ia32_vfmaddsd:
    return EmitScalarFMAExpr(*this, Ops,
                             Constant::getNullValue(Ops[0]->getType()));
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
    return EmitScalarFMAExpr(*this, Ops, Ops[0], /*ZeroMask*/true);
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
    return EmitScalarFMAExpr(*this, Ops, Ops[2], /*ZeroMask*/false, 2);
  case X86::BI__builtin_ia32_vfmsubss3_mask3:
  case X86::BI__builtin_ia32_vfmsubsd3_mask3:
    return EmitScalarFMAExpr(*this, Ops, Ops[2], /*ZeroMask*/false, 2,
                             /*NegAcc*/true);
  case X86::BI__builtin_ia32_vfmaddps:
  case X86::BI__builtin_ia32_vfmaddpd:
  case X86::BI__builtin_ia32_vfmaddps256:
  case X86::BI__builtin_ia32_vfmaddpd256:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
    return EmitX86FMAExpr(*this, Ops, BuiltinID, /*IsAddSub*/false);
  case X86::BI__builtin_ia32_vfmaddsubps:
  case X86::BI__builtin_ia32_vfmaddsubpd:
  case X86::BI__builtin_ia32_vfmaddsubps256:
  case X86::BI__builtin_ia32_vfmaddsubpd256:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    return EmitX86FMAExpr(*this, Ops, BuiltinID, /*IsAddSub*/true);

  case X86::BI__builtin_ia32_movdqa32store128_mask:
  case X86::BI__builtin_ia32_movdqa64store128_mask:
  case X86::BI__builtin_ia32_storeaps128_mask:
  case X86::BI__builtin_ia32_storeapd128_mask:
  case X86::BI__builtin_ia32_movdqa32store256_mask:
  case X86::BI__builtin_ia32_movdqa64store256_mask:
  case X86::BI__builtin_ia32_storeaps256_mask:
  case X86::BI__builtin_ia32_storeapd256_mask:
  case X86::BI__builtin_ia32_movdqa32store512_mask:
  case X86::BI__builtin_ia32_movdqa64store512_mask:
  case X86::BI__builtin_ia32_storeaps512_mask:
  case X86::BI__builtin_ia32_storeapd512_mask: {
    unsigned Align =
      getContext().getTypeAlignInChars(E->getArg(1)->getType()).getQuantity();
    return EmitX86MaskedStore(*this, Ops, Align);
  }
  case X86::BI__builtin_ia32_loadups128_mask:
  case X86::BI__builtin_ia32_loadups256_mask:
  case X86::BI__builtin_ia32_loadups512_mask:
  case X86::BI__builtin_ia32_loadupd128_mask:
  case X86::BI__builtin_ia32_loadupd256_mask:
  case X86::BI__builtin_ia32_loadupd512_mask:
  case X86::BI__builtin_ia32_loaddquqi128_mask:
  case X86::BI__builtin_ia32_loaddquqi256_mask:
  case X86::BI__builtin_ia32_loaddquqi512_mask:
  case X86::BI__builtin_ia32_loaddquhi128_mask:
  case X86::BI__builtin_ia32_loaddquhi256_mask:
  case X86::BI__builtin_ia32_loaddquhi512_mask:
  case X86::BI__builtin_ia32_loaddqusi128_mask:
  case X86::BI__builtin_ia32_loaddqusi256_mask:
  case X86::BI__builtin_ia32_loaddqusi512_mask:
  case X86::BI__builtin_ia32_loaddqudi128_mask:
  case X86::BI__builtin_ia32_loaddqudi256_mask:
  case X86::BI__builtin_ia32_loaddqudi512_mask:
    return EmitX86MaskedLoad(*this, Ops, 1);

  case X86::BI__builtin_ia32_loadss128_mask:
  case X86::BI__builtin_ia32_loadsd128_mask:
    return EmitX86MaskedLoad(*this, Ops, 1);

  case X86::BI__builtin_ia32_loadaps128_mask:
  case X86::BI__builtin_ia32_loadaps256_mask:
  case X86::BI__builtin_ia32_loadaps512_mask:
  case X86::BI__builtin_ia32_loadapd128_mask:
  case X86::BI__builtin_ia32_loadapd256_mask:
  case X86::BI__builtin_ia32_loadapd512_mask:
  case X86::BI__builtin_ia32_movdqa32load128_mask:
  case X86::BI__builtin_ia32_movdqa32load256_mask:
  case X86::BI__builtin_ia32_movdqa32load512_mask:
  case X86::BI__builtin_ia32_movdqa64load128_mask:
  case X86::BI__builtin_ia32_movdqa64load256_mask:
  case X86::BI__builtin_ia32_movdqa64load512_mask: {
    unsigned Align =
      getContext().getTypeAlignInChars(E->getArg(1)->getType()).getQuantity();
    return EmitX86MaskedLoad(*this, Ops, Align);
  }

  case X86::BI__builtin_ia32_expandloaddf128_mask:
  case X86::BI__builtin_ia32_expandloaddf256_mask:
  case X86::BI__builtin_ia32_expandloaddf512_mask:
  case X86::BI__builtin_ia32_expandloadsf128_mask:
  case X86::BI__builtin_ia32_expandloadsf256_mask:
  case X86::BI__builtin_ia32_expandloadsf512_mask:
  case X86::BI__builtin_ia32_expandloaddi128_mask:
  case X86::BI__builtin_ia32_expandloaddi256_mask:
  case X86::BI__builtin_ia32_expandloaddi512_mask:
  case X86::BI__builtin_ia32_expandloadsi128_mask:
  case X86::BI__builtin_ia32_expandloadsi256_mask:
  case X86::BI__builtin_ia32_expandloadsi512_mask:
  case X86::BI__builtin_ia32_expandloadhi128_mask:
  case X86::BI__builtin_ia32_expandloadhi256_mask:
  case X86::BI__builtin_ia32_expandloadhi512_mask:
  case X86::BI__builtin_ia32_expandloadqi128_mask:
  case X86::BI__builtin_ia32_expandloadqi256_mask:
  case X86::BI__builtin_ia32_expandloadqi512_mask:
    return EmitX86ExpandLoad(*this, Ops);

  case X86::BI__builtin_ia32_compressstoredf128_mask:
  case X86::BI__builtin_ia32_compressstoredf256_mask:
  case X86::BI__builtin_ia32_compressstoredf512_mask:
  case X86::BI__builtin_ia32_compressstoresf128_mask:
  case X86::BI__builtin_ia32_compressstoresf256_mask:
  case X86::BI__builtin_ia32_compressstoresf512_mask:
  case X86::BI__builtin_ia32_compressstoredi128_mask:
  case X86::BI__builtin_ia32_compressstoredi256_mask:
  case X86::BI__builtin_ia32_compressstoredi512_mask:
  case X86::BI__builtin_ia32_compressstoresi128_mask:
  case X86::BI__builtin_ia32_compressstoresi256_mask:
  case X86::BI__builtin_ia32_compressstoresi512_mask:
  case X86::BI__builtin_ia32_compressstorehi128_mask:
  case X86::BI__builtin_ia32_compressstorehi256_mask:
  case X86::BI__builtin_ia32_compressstorehi512_mask:
  case X86::BI__builtin_ia32_compressstoreqi128_mask:
  case X86::BI__builtin_ia32_compressstoreqi256_mask:
  case X86::BI__builtin_ia32_compressstoreqi512_mask:
    return EmitX86CompressStore(*this, Ops);

  case X86::BI__builtin_ia32_storehps:
  case X86::BI__builtin_ia32_storelps: {
    llvm::Type *PtrTy = llvm::PointerType::getUnqual(Int64Ty);
    llvm::Type *VecTy = llvm::VectorType::get(Int64Ty, 2);

    // cast val v2i64
    Ops[1] = Builder.CreateBitCast(Ops[1], VecTy, "cast");

    // extract (0, 1)
    unsigned Index = BuiltinID == X86::BI__builtin_ia32_storelps ? 0 : 1;
    Ops[1] = Builder.CreateExtractElement(Ops[1], Index, "extract");

    // cast pointer to i64 & store
    Ops[0] = Builder.CreateBitCast(Ops[0], PtrTy);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask: {
    llvm::Type *DstTy = ConvertType(E->getType());
    unsigned NumElts = DstTy->getVectorNumElements();
    unsigned SrcNumElts = Ops[0]->getType()->getVectorNumElements();
    unsigned SubVectors = SrcNumElts / NumElts;
    unsigned Index = cast<ConstantInt>(Ops[1])->getZExtValue();
    assert(llvm::isPowerOf2_32(SubVectors) && "Expected power of 2 subvectors");
    Index &= SubVectors - 1; // Remove any extra bits.
    Index *= NumElts;

    uint32_t Indices[16];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + Index;

    Value *Res = Builder.CreateShuffleVector(Ops[0],
                                             UndefValue::get(Ops[0]->getType()),
                                             makeArrayRef(Indices, NumElts),
                                             "extract");

    if (Ops.size() == 4)
      Res = EmitX86Select(*this, Ops[3], Res, Ops[2]);

    return Res;
  }
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_inserti32x4:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512: {
    unsigned DstNumElts = Ops[0]->getType()->getVectorNumElements();
    unsigned SrcNumElts = Ops[1]->getType()->getVectorNumElements();
    unsigned SubVectors = DstNumElts / SrcNumElts;
    unsigned Index = cast<ConstantInt>(Ops[2])->getZExtValue();
    assert(llvm::isPowerOf2_32(SubVectors) && "Expected power of 2 subvectors");
    Index &= SubVectors - 1; // Remove any extra bits.
    Index *= SrcNumElts;

    uint32_t Indices[16];
    for (unsigned i = 0; i != DstNumElts; ++i)
      Indices[i] = (i >= SrcNumElts) ? SrcNumElts + (i % SrcNumElts) : i;

    Value *Op1 = Builder.CreateShuffleVector(Ops[1],
                                             UndefValue::get(Ops[1]->getType()),
                                             makeArrayRef(Indices, DstNumElts),
                                             "widen");

    for (unsigned i = 0; i != DstNumElts; ++i) {
      if (i >= Index && i < (Index + SrcNumElts))
        Indices[i] = (i - Index) + DstNumElts;
      else
        Indices[i] = i;
    }

    return Builder.CreateShuffleVector(Ops[0], Op1,
                                       makeArrayRef(Indices, DstNumElts),
                                       "insert");
  }
  case X86::BI__builtin_ia32_pmovqd512_mask:
  case X86::BI__builtin_ia32_pmovwb512_mask: {
    Value *Res = Builder.CreateTrunc(Ops[0], Ops[1]->getType());
    return EmitX86Select(*this, Ops[2], Res, Ops[1]);
  }
  case X86::BI__builtin_ia32_pmovdb512_mask:
  case X86::BI__builtin_ia32_pmovdw512_mask:
  case X86::BI__builtin_ia32_pmovqw512_mask: {
    if (const auto *C = dyn_cast<Constant>(Ops[2]))
      if (C->isAllOnesValue())
        return Builder.CreateTrunc(Ops[0], Ops[1]->getType());

    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_pmovdb512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_db_512;
      break;
    case X86::BI__builtin_ia32_pmovdw512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_dw_512;
      break;
    case X86::BI__builtin_ia32_pmovqw512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_qw_512;
      break;
    }

    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, Ops);
  }
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_pblendd256: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();

    uint32_t Indices[16];
    // If there are more than 8 elements, the immediate is used twice so make
    // sure we handle that.
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = ((Imm >> (i % 8)) & 0x1) ? NumElts + i : i;

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       makeArrayRef(Indices, NumElts),
                                       "blend");
  }
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    uint32_t Indices[32];
    for (unsigned l = 0; l != NumElts; l += 8) {
      for (unsigned i = 0; i != 4; ++i) {
        Indices[l + i] = l + (Imm & 3);
        Imm >>= 2;
      }
      for (unsigned i = 4; i != 8; ++i)
        Indices[l + i] = l + i;
    }

    return Builder.CreateShuffleVector(Ops[0], UndefValue::get(Ty),
                                       makeArrayRef(Indices, NumElts),
                                       "pshuflw");
  }
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    uint32_t Indices[32];
    for (unsigned l = 0; l != NumElts; l += 8) {
      for (unsigned i = 0; i != 4; ++i)
        Indices[l + i] = l + i;
      for (unsigned i = 4; i != 8; ++i) {
        Indices[l + i] = l + 4 + (Imm & 3);
        Imm >>= 2;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], UndefValue::get(Ty),
                                       makeArrayRef(Indices, NumElts),
                                       "pshufhw");
  }
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() / 128;
    unsigned NumLaneElts = NumElts / NumLanes;

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    uint32_t Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        Indices[i + l] = (Imm % NumLaneElts) + l;
        Imm /= NumLaneElts;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], UndefValue::get(Ty),
                                       makeArrayRef(Indices, NumElts),
                                       "permil");
  }
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() / 128;
    unsigned NumLaneElts = NumElts / NumLanes;

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    uint32_t Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        unsigned Index = Imm % NumLaneElts;
        Imm /= NumLaneElts;
        if (i >= (NumLaneElts / 2))
          Index += NumElts;
        Indices[l + i] = l + Index;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       makeArrayRef(Indices, NumElts),
                                       "shufp");
  }
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_permdf512: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();

    // These intrinsics operate on 256-bit lanes of four 64-bit elements.
    uint32_t Indices[8];
    for (unsigned l = 0; l != NumElts; l += 4)
      for (unsigned i = 0; i != 4; ++i)
        Indices[l + i] = l + ((Imm >> (2 * i)) & 0x3);

    return Builder.CreateShuffleVector(Ops[0], UndefValue::get(Ty),
                                       makeArrayRef(Indices, NumElts),
                                       "perm");
  }
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0xff;

    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    assert(NumElts % 16 == 0);

    // If palignr is shifting the pair of vectors more than the size of two
    // lanes, emit zero.
    if (ShiftVal >= 32)
      return llvm::Constant::getNullValue(ConvertType(E->getType()));

    // If palignr is shifting the pair of input vectors more than one lane,
    // but less than two lanes, convert to shifting in zeroes.
    if (ShiftVal > 16) {
      ShiftVal -= 16;
      Ops[1] = Ops[0];
      Ops[0] = llvm::Constant::getNullValue(Ops[0]->getType());
    }

    uint32_t Indices[64];
    // 256-bit palignr operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = ShiftVal + i;
        if (Idx >= 16)
          Idx += NumElts - 16; // End of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    return Builder.CreateShuffleVector(Ops[1], Ops[0],
                                       makeArrayRef(Indices, NumElts),
                                       "palignr");
  }
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_alignq512: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0xff;

    // Mask the shift amount to width of two vectors.
    ShiftVal &= (2 * NumElts) - 1;

    uint32_t Indices[16];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + ShiftVal;

    return Builder.CreateShuffleVector(Ops[1], Ops[0],
                                       makeArrayRef(Indices, NumElts),
                                       "valign");
  }
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    llvm::Type *Ty = Ops[0]->getType();
    unsigned NumElts = Ty->getVectorNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() == 512 ? 4 : 2;
    unsigned NumLaneElts = NumElts / NumLanes;

    uint32_t Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      unsigned Index = (Imm % NumLanes) * NumLaneElts;
      Imm /= NumLanes; // Discard the bits we just used.
      if (l >= (NumElts / 2))
        Index += NumElts; // Switch to other source.
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        Indices[l + i] = Index + i;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       makeArrayRef(Indices, NumElts),
                                       "shuf");
  }

  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();

    // This takes a very simple approach since there are two lanes and a
    // shuffle can have 2 inputs. So we reserve the first input for the first
    // lane and the second input for the second lane. This may result in
    // duplicate sources, but this can be dealt with in the backend.

    Value *OutOps[2];
    uint32_t Indices[8];
    for (unsigned l = 0; l != 2; ++l) {
      // Determine the source for this lane.
      if (Imm & (1 << ((l * 4) + 3)))
        OutOps[l] = llvm::ConstantAggregateZero::get(Ops[0]->getType());
      else if (Imm & (1 << ((l * 4) + 1)))
        OutOps[l] = Ops[1];
      else
        OutOps[l] = Ops[0];

      for (unsigned i = 0; i != NumElts/2; ++i) {
        // Start with ith element of the source for this lane.
        unsigned Idx = (l * NumElts) + i;
        // If bit 0 of the immediate half is set, switch to the high half of
        // the source.
        if (Imm & (1 << (l * 4)))
          Idx += NumElts/2;
        Indices[(l * (NumElts/2)) + i] = Idx;
      }
    }

    return Builder.CreateShuffleVector(OutOps[0], OutOps[1],
                                       makeArrayRef(Indices, NumElts),
                                       "vperm");
  }

  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    llvm::Type *ResultType = Ops[0]->getType();
    // Builtin type is vXi64 so multiply by 8 to get bytes.
    unsigned NumElts = ResultType->getVectorNumElements() * 8;

    // If pslldq is shifting the vector more than 15 bytes, emit zero.
    if (ShiftVal >= 16)
      return llvm::Constant::getNullValue(ResultType);

    uint32_t Indices[64];
    // 256/512-bit pslldq operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = NumElts + i - ShiftVal;
        if (Idx < NumElts) Idx -= NumElts - 16; // end of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    llvm::Type *VecTy = llvm::VectorType::get(Int8Ty, NumElts);
    Value *Cast = Builder.CreateBitCast(Ops[0], VecTy, "cast");
    Value *Zero = llvm::Constant::getNullValue(VecTy);
    Value *SV = Builder.CreateShuffleVector(Zero, Cast,
                                            makeArrayRef(Indices, NumElts),
                                            "pslldq");
    return Builder.CreateBitCast(SV, Ops[0]->getType(), "cast");
  }
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    llvm::Type *ResultType = Ops[0]->getType();
    // Builtin type is vXi64 so multiply by 8 to get bytes.
    unsigned NumElts = ResultType->getVectorNumElements() * 8;

    // If psrldq is shifting the vector more than 15 bytes, emit zero.
    if (ShiftVal >= 16)
      return llvm::Constant::getNullValue(ResultType);

    uint32_t Indices[64];
    // 256/512-bit psrldq operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = i + ShiftVal;
        if (Idx >= 16) Idx += NumElts - 16; // end of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    llvm::Type *VecTy = llvm::VectorType::get(Int8Ty, NumElts);
    Value *Cast = Builder.CreateBitCast(Ops[0], VecTy, "cast");
    Value *Zero = llvm::Constant::getNullValue(VecTy);
    Value *SV = Builder.CreateShuffleVector(Cast, Zero,
                                            makeArrayRef(Indices, NumElts),
                                            "psrldq");
    return Builder.CreateBitCast(SV, ResultType, "cast");
  }
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();

    if (ShiftVal >= NumElts)
      return llvm::Constant::getNullValue(Ops[0]->getType());

    Value *In = getMaskVecValue(*this, Ops[0], NumElts);

    uint32_t Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = NumElts + i - ShiftVal;

    Value *Zero = llvm::Constant::getNullValue(In->getType());
    Value *SV = Builder.CreateShuffleVector(Zero, In,
                                            makeArrayRef(Indices, NumElts),
                                            "kshiftl");
    return Builder.CreateBitCast(SV, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();

    if (ShiftVal >= NumElts)
      return llvm::Constant::getNullValue(Ops[0]->getType());

    Value *In = getMaskVecValue(*this, Ops[0], NumElts);

    uint32_t Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + ShiftVal;

    Value *Zero = llvm::Constant::getNullValue(In->getType());
    Value *SV = Builder.CreateShuffleVector(In, Zero,
                                            makeArrayRef(Indices, NumElts),
                                            "kshiftr");
    return Builder.CreateBitCast(SV, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_movnti:
  case X86::BI__builtin_ia32_movnti64:
  case X86::BI__builtin_ia32_movntsd:
  case X86::BI__builtin_ia32_movntss: {
    llvm::MDNode *Node = llvm::MDNode::get(
        getLLVMContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));

    Value *Ptr = Ops[0];
    Value *Src = Ops[1];

    // Extract the 0'th element of the source vector.
    if (BuiltinID == X86::BI__builtin_ia32_movntsd ||
        BuiltinID == X86::BI__builtin_ia32_movntss)
      Src = Builder.CreateExtractElement(Src, (uint64_t)0, "extract");

    // Convert the type of the pointer to a pointer to the stored type.
    Value *BC = Builder.CreateBitCast(
        Ptr, llvm::PointerType::getUnqual(Src->getType()), "cast");

    // Unaligned nontemporal store of the scalar value.
    StoreInst *SI = Builder.CreateDefaultAlignedStore(Src, BC);
    SI->setMetadata(CGM.getModule().getMDKindID("nontemporal"), Node);
    SI->setAlignment(1);
    return SI;
  }
  // Rotate is a special case of funnel shift - 1st 2 args are the same.
  case X86::BI__builtin_ia32_vprotb:
  case X86::BI__builtin_ia32_vprotw:
  case X86::BI__builtin_ia32_vprotd:
  case X86::BI__builtin_ia32_vprotq:
  case X86::BI__builtin_ia32_vprotbi:
  case X86::BI__builtin_ia32_vprotwi:
  case X86::BI__builtin_ia32_vprotdi:
  case X86::BI__builtin_ia32_vprotqi:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prolq512:
  case X86::BI__builtin_ia32_prolvd128:
  case X86::BI__builtin_ia32_prolvd256:
  case X86::BI__builtin_ia32_prolvd512:
  case X86::BI__builtin_ia32_prolvq128:
  case X86::BI__builtin_ia32_prolvq256:
  case X86::BI__builtin_ia32_prolvq512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[0], Ops[1], false);
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_prorq512:
  case X86::BI__builtin_ia32_prorvd128:
  case X86::BI__builtin_ia32_prorvd256:
  case X86::BI__builtin_ia32_prorvd512:
  case X86::BI__builtin_ia32_prorvq128:
  case X86::BI__builtin_ia32_prorvq256:
  case X86::BI__builtin_ia32_prorvq512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[0], Ops[1], true);
  case X86::BI__builtin_ia32_selectb_128:
  case X86::BI__builtin_ia32_selectb_256:
  case X86::BI__builtin_ia32_selectb_512:
  case X86::BI__builtin_ia32_selectw_128:
  case X86::BI__builtin_ia32_selectw_256:
  case X86::BI__builtin_ia32_selectw_512:
  case X86::BI__builtin_ia32_selectd_128:
  case X86::BI__builtin_ia32_selectd_256:
  case X86::BI__builtin_ia32_selectd_512:
  case X86::BI__builtin_ia32_selectq_128:
  case X86::BI__builtin_ia32_selectq_256:
  case X86::BI__builtin_ia32_selectq_512:
  case X86::BI__builtin_ia32_selectps_128:
  case X86::BI__builtin_ia32_selectps_256:
  case X86::BI__builtin_ia32_selectps_512:
  case X86::BI__builtin_ia32_selectpd_128:
  case X86::BI__builtin_ia32_selectpd_256:
  case X86::BI__builtin_ia32_selectpd_512:
    return EmitX86Select(*this, Ops[0], Ops[1], Ops[2]);
  case X86::BI__builtin_ia32_selectss_128:
  case X86::BI__builtin_ia32_selectsd_128: {
    Value *A = Builder.CreateExtractElement(Ops[1], (uint64_t)0);
    Value *B = Builder.CreateExtractElement(Ops[2], (uint64_t)0);
    A = EmitX86ScalarSelect(*this, Ops[0], A, B);
    return Builder.CreateInsertElement(Ops[1], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpq512_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x7;
    return EmitX86MaskedCompare(*this, CC, true, Ops);
  }
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x7;
    return EmitX86MaskedCompare(*this, CC, false, Ops);
  }

  case X86::BI__builtin_ia32_kortestcqi:
  case X86::BI__builtin_ia32_kortestchi:
  case X86::BI__builtin_ia32_kortestcsi:
  case X86::BI__builtin_ia32_kortestcdi: {
    Value *Or = EmitX86MaskLogic(*this, Instruction::Or, Ops);
    Value *C = llvm::Constant::getAllOnesValue(Ops[0]->getType());
    Value *Cmp = Builder.CreateICmpEQ(Or, C);
    return Builder.CreateZExt(Cmp, ConvertType(E->getType()));
  }
  case X86::BI__builtin_ia32_kortestzqi:
  case X86::BI__builtin_ia32_kortestzhi:
  case X86::BI__builtin_ia32_kortestzsi:
  case X86::BI__builtin_ia32_kortestzdi: {
    Value *Or = EmitX86MaskLogic(*this, Instruction::Or, Ops);
    Value *C = llvm::Constant::getNullValue(Ops[0]->getType());
    Value *Cmp = Builder.CreateICmpEQ(Or, C);
    return Builder.CreateZExt(Cmp, ConvertType(E->getType()));
  }

  case X86::BI__builtin_ia32_ktestcqi:
  case X86::BI__builtin_ia32_ktestzqi:
  case X86::BI__builtin_ia32_ktestchi:
  case X86::BI__builtin_ia32_ktestzhi:
  case X86::BI__builtin_ia32_ktestcsi:
  case X86::BI__builtin_ia32_ktestzsi:
  case X86::BI__builtin_ia32_ktestcdi:
  case X86::BI__builtin_ia32_ktestzdi: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_ktestcqi:
      IID = Intrinsic::x86_avx512_ktestc_b;
      break;
    case X86::BI__builtin_ia32_ktestzqi:
      IID = Intrinsic::x86_avx512_ktestz_b;
      break;
    case X86::BI__builtin_ia32_ktestchi:
      IID = Intrinsic::x86_avx512_ktestc_w;
      break;
    case X86::BI__builtin_ia32_ktestzhi:
      IID = Intrinsic::x86_avx512_ktestz_w;
      break;
    case X86::BI__builtin_ia32_ktestcsi:
      IID = Intrinsic::x86_avx512_ktestc_d;
      break;
    case X86::BI__builtin_ia32_ktestzsi:
      IID = Intrinsic::x86_avx512_ktestz_d;
      break;
    case X86::BI__builtin_ia32_ktestcdi:
      IID = Intrinsic::x86_avx512_ktestc_q;
      break;
    case X86::BI__builtin_ia32_ktestzdi:
      IID = Intrinsic::x86_avx512_ktestz_q;
      break;
    }

    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, {LHS, RHS});
  }

  case X86::BI__builtin_ia32_kaddqi:
  case X86::BI__builtin_ia32_kaddhi:
  case X86::BI__builtin_ia32_kaddsi:
  case X86::BI__builtin_ia32_kadddi: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_kaddqi:
      IID = Intrinsic::x86_avx512_kadd_b;
      break;
    case X86::BI__builtin_ia32_kaddhi:
      IID = Intrinsic::x86_avx512_kadd_w;
      break;
    case X86::BI__builtin_ia32_kaddsi:
      IID = Intrinsic::x86_avx512_kadd_d;
      break;
    case X86::BI__builtin_ia32_kadddi:
      IID = Intrinsic::x86_avx512_kadd_q;
      break;
    }

    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    Function *Intr = CGM.getIntrinsic(IID);
    Value *Res = Builder.CreateCall(Intr, {LHS, RHS});
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kandqi:
  case X86::BI__builtin_ia32_kandhi:
  case X86::BI__builtin_ia32_kandsi:
  case X86::BI__builtin_ia32_kanddi:
    return EmitX86MaskLogic(*this, Instruction::And, Ops);
  case X86::BI__builtin_ia32_kandnqi:
  case X86::BI__builtin_ia32_kandnhi:
  case X86::BI__builtin_ia32_kandnsi:
  case X86::BI__builtin_ia32_kandndi:
    return EmitX86MaskLogic(*this, Instruction::And, Ops, true);
  case X86::BI__builtin_ia32_korqi:
  case X86::BI__builtin_ia32_korhi:
  case X86::BI__builtin_ia32_korsi:
  case X86::BI__builtin_ia32_kordi:
    return EmitX86MaskLogic(*this, Instruction::Or, Ops);
  case X86::BI__builtin_ia32_kxnorqi:
  case X86::BI__builtin_ia32_kxnorhi:
  case X86::BI__builtin_ia32_kxnorsi:
  case X86::BI__builtin_ia32_kxnordi:
    return EmitX86MaskLogic(*this, Instruction::Xor, Ops, true);
  case X86::BI__builtin_ia32_kxorqi:
  case X86::BI__builtin_ia32_kxorhi:
  case X86::BI__builtin_ia32_kxorsi:
  case X86::BI__builtin_ia32_kxordi:
    return EmitX86MaskLogic(*this, Instruction::Xor,  Ops);
  case X86::BI__builtin_ia32_knotqi:
  case X86::BI__builtin_ia32_knothi:
  case X86::BI__builtin_ia32_knotsi:
  case X86::BI__builtin_ia32_knotdi: {
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *Res = getMaskVecValue(*this, Ops[0], NumElts);
    return Builder.CreateBitCast(Builder.CreateNot(Res),
                                 Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kmovb:
  case X86::BI__builtin_ia32_kmovw:
  case X86::BI__builtin_ia32_kmovd:
  case X86::BI__builtin_ia32_kmovq: {
    // Bitcast to vXi1 type and then back to integer. This gets the mask
    // register type into the IR, but might be optimized out depending on
    // what's around it.
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *Res = getMaskVecValue(*this, Ops[0], NumElts);
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }

  case X86::BI__builtin_ia32_kunpckdi:
  case X86::BI__builtin_ia32_kunpcksi:
  case X86::BI__builtin_ia32_kunpckhi: {
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    uint32_t Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;

    // First extract half of each vector. This gives better codegen than
    // doing it in a single shuffle.
    LHS = Builder.CreateShuffleVector(LHS, LHS,
                                      makeArrayRef(Indices, NumElts / 2));
    RHS = Builder.CreateShuffleVector(RHS, RHS,
                                      makeArrayRef(Indices, NumElts / 2));
    // Concat the vectors.
    // NOTE: Operands are swapped to match the intrinsic definition.
    Value *Res = Builder.CreateShuffleVector(RHS, LHS,
                                             makeArrayRef(Indices, NumElts));
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }

  case X86::BI__builtin_ia32_vplzcntd_128:
  case X86::BI__builtin_ia32_vplzcntd_256:
  case X86::BI__builtin_ia32_vplzcntd_512:
  case X86::BI__builtin_ia32_vplzcntq_128:
  case X86::BI__builtin_ia32_vplzcntq_256:
  case X86::BI__builtin_ia32_vplzcntq_512: {
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0],Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_sqrtss:
  case X86::BI__builtin_ia32_sqrtsd: {
    Value *A = Builder.CreateExtractElement(Ops[0], (uint64_t)0);
    Function *F = CGM.getIntrinsic(Intrinsic::sqrt, A->getType());
    A = Builder.CreateCall(F, {A});
    return Builder.CreateInsertElement(Ops[0], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[4])->getZExtValue();
    // Support only if the rounding mode is 4 (AKA CUR_DIRECTION),
    // otherwise keep the intrinsic.
    if (CC != 4) {
      Intrinsic::ID IID = BuiltinID == X86::BI__builtin_ia32_sqrtsd_round_mask ?
                          Intrinsic::x86_avx512_mask_sqrt_sd :
                          Intrinsic::x86_avx512_mask_sqrt_ss;
      return Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
    }
    Value *A = Builder.CreateExtractElement(Ops[1], (uint64_t)0);
    Function *F = CGM.getIntrinsic(Intrinsic::sqrt, A->getType());
    A = Builder.CreateCall(F, A);
    Value *Src = Builder.CreateExtractElement(Ops[2], (uint64_t)0);
    A = EmitX86ScalarSelect(*this, Ops[3], A, Src);
    return Builder.CreateInsertElement(Ops[0], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_sqrtpd256:
  case X86::BI__builtin_ia32_sqrtpd:
  case X86::BI__builtin_ia32_sqrtps256:
  case X86::BI__builtin_ia32_sqrtps:
  case X86::BI__builtin_ia32_sqrtps512:
  case X86::BI__builtin_ia32_sqrtpd512: {
    if (Ops.size() == 2) {
      unsigned CC = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
      // Support only if the rounding mode is 4 (AKA CUR_DIRECTION),
      // otherwise keep the intrinsic.
      if (CC != 4) {
        Intrinsic::ID IID = BuiltinID == X86::BI__builtin_ia32_sqrtps512 ?
                            Intrinsic::x86_avx512_sqrt_ps_512 :
                            Intrinsic::x86_avx512_sqrt_pd_512;
        return Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
      }
    }
    Function *F = CGM.getIntrinsic(Intrinsic::sqrt, Ops[0]->getType());
    return Builder.CreateCall(F, Ops[0]);
  }
  case X86::BI__builtin_ia32_pabsb128:
  case X86::BI__builtin_ia32_pabsw128:
  case X86::BI__builtin_ia32_pabsd128:
  case X86::BI__builtin_ia32_pabsb256:
  case X86::BI__builtin_ia32_pabsw256:
  case X86::BI__builtin_ia32_pabsd256:
  case X86::BI__builtin_ia32_pabsq128:
  case X86::BI__builtin_ia32_pabsq256:
  case X86::BI__builtin_ia32_pabsb512:
  case X86::BI__builtin_ia32_pabsw512:
  case X86::BI__builtin_ia32_pabsd512:
  case X86::BI__builtin_ia32_pabsq512:
    return EmitX86Abs(*this, Ops);

  case X86::BI__builtin_ia32_pmaxsb128:
  case X86::BI__builtin_ia32_pmaxsw128:
  case X86::BI__builtin_ia32_pmaxsd128:
  case X86::BI__builtin_ia32_pmaxsq128:
  case X86::BI__builtin_ia32_pmaxsb256:
  case X86::BI__builtin_ia32_pmaxsw256:
  case X86::BI__builtin_ia32_pmaxsd256:
  case X86::BI__builtin_ia32_pmaxsq256:
  case X86::BI__builtin_ia32_pmaxsb512:
  case X86::BI__builtin_ia32_pmaxsw512:
  case X86::BI__builtin_ia32_pmaxsd512:
  case X86::BI__builtin_ia32_pmaxsq512:
    return EmitX86MinMax(*this, ICmpInst::ICMP_SGT, Ops);
  case X86::BI__builtin_ia32_pmaxub128:
  case X86::BI__builtin_ia32_pmaxuw128:
  case X86::BI__builtin_ia32_pmaxud128:
  case X86::BI__builtin_ia32_pmaxuq128:
  case X86::BI__builtin_ia32_pmaxub256:
  case X86::BI__builtin_ia32_pmaxuw256:
  case X86::BI__builtin_ia32_pmaxud256:
  case X86::BI__builtin_ia32_pmaxuq256:
  case X86::BI__builtin_ia32_pmaxub512:
  case X86::BI__builtin_ia32_pmaxuw512:
  case X86::BI__builtin_ia32_pmaxud512:
  case X86::BI__builtin_ia32_pmaxuq512:
    return EmitX86MinMax(*this, ICmpInst::ICMP_UGT, Ops);
  case X86::BI__builtin_ia32_pminsb128:
  case X86::BI__builtin_ia32_pminsw128:
  case X86::BI__builtin_ia32_pminsd128:
  case X86::BI__builtin_ia32_pminsq128:
  case X86::BI__builtin_ia32_pminsb256:
  case X86::BI__builtin_ia32_pminsw256:
  case X86::BI__builtin_ia32_pminsd256:
  case X86::BI__builtin_ia32_pminsq256:
  case X86::BI__builtin_ia32_pminsb512:
  case X86::BI__builtin_ia32_pminsw512:
  case X86::BI__builtin_ia32_pminsd512:
  case X86::BI__builtin_ia32_pminsq512:
    return EmitX86MinMax(*this, ICmpInst::ICMP_SLT, Ops);
  case X86::BI__builtin_ia32_pminub128:
  case X86::BI__builtin_ia32_pminuw128:
  case X86::BI__builtin_ia32_pminud128:
  case X86::BI__builtin_ia32_pminuq128:
  case X86::BI__builtin_ia32_pminub256:
  case X86::BI__builtin_ia32_pminuw256:
  case X86::BI__builtin_ia32_pminud256:
  case X86::BI__builtin_ia32_pminuq256:
  case X86::BI__builtin_ia32_pminub512:
  case X86::BI__builtin_ia32_pminuw512:
  case X86::BI__builtin_ia32_pminud512:
  case X86::BI__builtin_ia32_pminuq512:
    return EmitX86MinMax(*this, ICmpInst::ICMP_ULT, Ops);

  case X86::BI__builtin_ia32_pmuludq128:
  case X86::BI__builtin_ia32_pmuludq256:
  case X86::BI__builtin_ia32_pmuludq512:
    return EmitX86Muldq(*this, /*IsSigned*/false, Ops);

  case X86::BI__builtin_ia32_pmuldq128:
  case X86::BI__builtin_ia32_pmuldq256:
  case X86::BI__builtin_ia32_pmuldq512:
    return EmitX86Muldq(*this, /*IsSigned*/true, Ops);

  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq256_mask:
    return EmitX86Ternlog(*this, /*ZeroMask*/false, Ops);

  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
    return EmitX86Ternlog(*this, /*ZeroMask*/true, Ops);

  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[1], Ops[2], false);

  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    // Ops 0 and 1 are swapped.
    return EmitX86FunnelShift(*this, Ops[1], Ops[0], Ops[2], true);

  case X86::BI__builtin_ia32_vpshldvd128:
  case X86::BI__builtin_ia32_vpshldvd256:
  case X86::BI__builtin_ia32_vpshldvd512:
  case X86::BI__builtin_ia32_vpshldvq128:
  case X86::BI__builtin_ia32_vpshldvq256:
  case X86::BI__builtin_ia32_vpshldvq512:
  case X86::BI__builtin_ia32_vpshldvw128:
  case X86::BI__builtin_ia32_vpshldvw256:
  case X86::BI__builtin_ia32_vpshldvw512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[1], Ops[2], false);

  case X86::BI__builtin_ia32_vpshrdvd128:
  case X86::BI__builtin_ia32_vpshrdvd256:
  case X86::BI__builtin_ia32_vpshrdvd512:
  case X86::BI__builtin_ia32_vpshrdvq128:
  case X86::BI__builtin_ia32_vpshrdvq256:
  case X86::BI__builtin_ia32_vpshrdvq512:
  case X86::BI__builtin_ia32_vpshrdvw128:
  case X86::BI__builtin_ia32_vpshrdvw256:
  case X86::BI__builtin_ia32_vpshrdvw512:
    // Ops 0 and 1 are swapped.
    return EmitX86FunnelShift(*this, Ops[1], Ops[0], Ops[2], true);

  // 3DNow!
  case X86::BI__builtin_ia32_pswapdsf:
  case X86::BI__builtin_ia32_pswapdsi: {
    llvm::Type *MMXTy = llvm::Type::getX86_MMXTy(getLLVMContext());
    Ops[0] = Builder.CreateBitCast(Ops[0], MMXTy, "cast");
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::x86_3dnowa_pswapd);
    return Builder.CreateCall(F, Ops, "pswapd");
  }
  case X86::BI__builtin_ia32_rdrand16_step:
  case X86::BI__builtin_ia32_rdrand32_step:
  case X86::BI__builtin_ia32_rdrand64_step:
  case X86::BI__builtin_ia32_rdseed16_step:
  case X86::BI__builtin_ia32_rdseed32_step:
  case X86::BI__builtin_ia32_rdseed64_step: {
    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_rdrand16_step:
      ID = Intrinsic::x86_rdrand_16;
      break;
    case X86::BI__builtin_ia32_rdrand32_step:
      ID = Intrinsic::x86_rdrand_32;
      break;
    case X86::BI__builtin_ia32_rdrand64_step:
      ID = Intrinsic::x86_rdrand_64;
      break;
    case X86::BI__builtin_ia32_rdseed16_step:
      ID = Intrinsic::x86_rdseed_16;
      break;
    case X86::BI__builtin_ia32_rdseed32_step:
      ID = Intrinsic::x86_rdseed_32;
      break;
    case X86::BI__builtin_ia32_rdseed64_step:
      ID = Intrinsic::x86_rdseed_64;
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(ID));
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 0),
                                      Ops[0]);
    return Builder.CreateExtractValue(Call, 1);
  }
  case X86::BI__builtin_ia32_addcarryx_u32:
  case X86::BI__builtin_ia32_addcarryx_u64:
  case X86::BI__builtin_ia32_subborrow_u32:
  case X86::BI__builtin_ia32_subborrow_u64: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_addcarryx_u32:
      IID = Intrinsic::x86_addcarry_32;
      break;
    case X86::BI__builtin_ia32_addcarryx_u64:
      IID = Intrinsic::x86_addcarry_64;
      break;
    case X86::BI__builtin_ia32_subborrow_u32:
      IID = Intrinsic::x86_subborrow_32;
      break;
    case X86::BI__builtin_ia32_subborrow_u64:
      IID = Intrinsic::x86_subborrow_64;
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID),
                                     { Ops[0], Ops[1], Ops[2] });
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 1),
                                      Ops[3]);
    return Builder.CreateExtractValue(Call, 0);
  }

  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    Value *MaskIn = Ops[2];
    Ops.erase(&Ops[2]);

    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_fpclassps128_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_128;
      break;
    case X86::BI__builtin_ia32_fpclassps256_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_256;
      break;
    case X86::BI__builtin_ia32_fpclassps512_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_512;
      break;
    case X86::BI__builtin_ia32_fpclasspd128_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_128;
      break;
    case X86::BI__builtin_ia32_fpclasspd256_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_256;
      break;
    case X86::BI__builtin_ia32_fpclasspd512_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_512;
      break;
    }

    Value *Fpclass = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    return EmitX86MaskedCompareResult(*this, Fpclass, NumElts, MaskIn);
  }

  case X86::BI__builtin_ia32_vpmultishiftqb128:
  case X86::BI__builtin_ia32_vpmultishiftqb256:
  case X86::BI__builtin_ia32_vpmultishiftqb512: {
    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_vpmultishiftqb128:
      ID = Intrinsic::x86_avx512_pmultishift_qb_128;
      break;
    case X86::BI__builtin_ia32_vpmultishiftqb256:
      ID = Intrinsic::x86_avx512_pmultishift_qb_256;
      break;
    case X86::BI__builtin_ia32_vpmultishiftqb512:
      ID = Intrinsic::x86_avx512_pmultishift_qb_512;
      break;
    }

    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
  }

  case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb512_mask: {
    unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
    Value *MaskIn = Ops[2];
    Ops.erase(&Ops[2]);

    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_128;
      break;
    case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_256;
      break;
    case X86::BI__builtin_ia32_vpshufbitqmb512_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_512;
      break;
    }

    Value *Shufbit = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    return EmitX86MaskedCompareResult(*this, Shufbit, NumElts, MaskIn);
  }

  // packed comparison intrinsics
  case X86::BI__builtin_ia32_cmpeqps:
  case X86::BI__builtin_ia32_cmpeqpd:
    return getVectorFCmpIR(CmpInst::FCMP_OEQ);
  case X86::BI__builtin_ia32_cmpltps:
  case X86::BI__builtin_ia32_cmpltpd:
    return getVectorFCmpIR(CmpInst::FCMP_OLT);
  case X86::BI__builtin_ia32_cmpleps:
  case X86::BI__builtin_ia32_cmplepd:
    return getVectorFCmpIR(CmpInst::FCMP_OLE);
  case X86::BI__builtin_ia32_cmpunordps:
  case X86::BI__builtin_ia32_cmpunordpd:
    return getVectorFCmpIR(CmpInst::FCMP_UNO);
  case X86::BI__builtin_ia32_cmpneqps:
  case X86::BI__builtin_ia32_cmpneqpd:
    return getVectorFCmpIR(CmpInst::FCMP_UNE);
  case X86::BI__builtin_ia32_cmpnltps:
  case X86::BI__builtin_ia32_cmpnltpd:
    return getVectorFCmpIR(CmpInst::FCMP_UGE);
  case X86::BI__builtin_ia32_cmpnleps:
  case X86::BI__builtin_ia32_cmpnlepd:
    return getVectorFCmpIR(CmpInst::FCMP_UGT);
  case X86::BI__builtin_ia32_cmpordps:
  case X86::BI__builtin_ia32_cmpordpd:
    return getVectorFCmpIR(CmpInst::FCMP_ORD);
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmppd256:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmppd512_mask: {
    // Lowering vector comparisons to fcmp instructions, while
    // ignoring signalling behaviour requested
    // ignoring rounding mode requested
    // This is is only possible as long as FENV_ACCESS is not implemented.
    // See also: https://reviews.llvm.org/D45616

    // The third argument is the comparison condition, and integer in the
    // range [0, 31]
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x1f;

    // Lowering to IR fcmp instruction.
    // Ignoring requested signaling behaviour,
    // e.g. both _CMP_GT_OS & _CMP_GT_OQ are translated to FCMP_OGT.
    FCmpInst::Predicate Pred;
    switch (CC) {
    case 0x00: Pred = FCmpInst::FCMP_OEQ;   break;
    case 0x01: Pred = FCmpInst::FCMP_OLT;   break;
    case 0x02: Pred = FCmpInst::FCMP_OLE;   break;
    case 0x03: Pred = FCmpInst::FCMP_UNO;   break;
    case 0x04: Pred = FCmpInst::FCMP_UNE;   break;
    case 0x05: Pred = FCmpInst::FCMP_UGE;   break;
    case 0x06: Pred = FCmpInst::FCMP_UGT;   break;
    case 0x07: Pred = FCmpInst::FCMP_ORD;   break;
    case 0x08: Pred = FCmpInst::FCMP_UEQ;   break;
    case 0x09: Pred = FCmpInst::FCMP_ULT;   break;
    case 0x0a: Pred = FCmpInst::FCMP_ULE;   break;
    case 0x0b: Pred = FCmpInst::FCMP_FALSE; break;
    case 0x0c: Pred = FCmpInst::FCMP_ONE;   break;
    case 0x0d: Pred = FCmpInst::FCMP_OGE;   break;
    case 0x0e: Pred = FCmpInst::FCMP_OGT;   break;
    case 0x0f: Pred = FCmpInst::FCMP_TRUE;  break;
    case 0x10: Pred = FCmpInst::FCMP_OEQ;   break;
    case 0x11: Pred = FCmpInst::FCMP_OLT;   break;
    case 0x12: Pred = FCmpInst::FCMP_OLE;   break;
    case 0x13: Pred = FCmpInst::FCMP_UNO;   break;
    case 0x14: Pred = FCmpInst::FCMP_UNE;   break;
    case 0x15: Pred = FCmpInst::FCMP_UGE;   break;
    case 0x16: Pred = FCmpInst::FCMP_UGT;   break;
    case 0x17: Pred = FCmpInst::FCMP_ORD;   break;
    case 0x18: Pred = FCmpInst::FCMP_UEQ;   break;
    case 0x19: Pred = FCmpInst::FCMP_ULT;   break;
    case 0x1a: Pred = FCmpInst::FCMP_ULE;   break;
    case 0x1b: Pred = FCmpInst::FCMP_FALSE; break;
    case 0x1c: Pred = FCmpInst::FCMP_ONE;   break;
    case 0x1d: Pred = FCmpInst::FCMP_OGE;   break;
    case 0x1e: Pred = FCmpInst::FCMP_OGT;   break;
    case 0x1f: Pred = FCmpInst::FCMP_TRUE;  break;
    default: llvm_unreachable("Unhandled CC");
    }

    // Builtins without the _mask suffix return a vector of integers
    // of the same width as the input vectors
    switch (BuiltinID) {
    case X86::BI__builtin_ia32_cmpps512_mask:
    case X86::BI__builtin_ia32_cmppd512_mask:
    case X86::BI__builtin_ia32_cmpps128_mask:
    case X86::BI__builtin_ia32_cmpps256_mask:
    case X86::BI__builtin_ia32_cmppd128_mask:
    case X86::BI__builtin_ia32_cmppd256_mask: {
      unsigned NumElts = Ops[0]->getType()->getVectorNumElements();
      Value *Cmp = Builder.CreateFCmp(Pred, Ops[0], Ops[1]);
      return EmitX86MaskedCompareResult(*this, Cmp, NumElts, Ops[3]);
    }
    default:
      return getVectorFCmpIR(Pred);
    }
  }

  // SSE scalar comparison intrinsics
  case X86::BI__builtin_ia32_cmpeqss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 0);
  case X86::BI__builtin_ia32_cmpltss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 1);
  case X86::BI__builtin_ia32_cmpless:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 2);
  case X86::BI__builtin_ia32_cmpunordss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 3);
  case X86::BI__builtin_ia32_cmpneqss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 4);
  case X86::BI__builtin_ia32_cmpnltss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 5);
  case X86::BI__builtin_ia32_cmpnless:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 6);
  case X86::BI__builtin_ia32_cmpordss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 7);
  case X86::BI__builtin_ia32_cmpeqsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 0);
  case X86::BI__builtin_ia32_cmpltsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 1);
  case X86::BI__builtin_ia32_cmplesd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 2);
  case X86::BI__builtin_ia32_cmpunordsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 3);
  case X86::BI__builtin_ia32_cmpneqsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 4);
  case X86::BI__builtin_ia32_cmpnltsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 5);
  case X86::BI__builtin_ia32_cmpnlesd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 6);
  case X86::BI__builtin_ia32_cmpordsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 7);

  case X86::BI__emul:
  case X86::BI__emulu: {
    llvm::Type *Int64Ty = llvm::IntegerType::get(getLLVMContext(), 64);
    bool isSigned = (BuiltinID == X86::BI__emul);
    Value *LHS = Builder.CreateIntCast(Ops[0], Int64Ty, isSigned);
    Value *RHS = Builder.CreateIntCast(Ops[1], Int64Ty, isSigned);
    return Builder.CreateMul(LHS, RHS, "", !isSigned, isSigned);
  }
  case X86::BI__mulh:
  case X86::BI__umulh:
  case X86::BI_mul128:
  case X86::BI_umul128: {
    llvm::Type *ResType = ConvertType(E->getType());
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);

    bool IsSigned = (BuiltinID == X86::BI__mulh || BuiltinID == X86::BI_mul128);
    Value *LHS = Builder.CreateIntCast(Ops[0], Int128Ty, IsSigned);
    Value *RHS = Builder.CreateIntCast(Ops[1], Int128Ty, IsSigned);

    Value *MulResult, *HigherBits;
    if (IsSigned) {
      MulResult = Builder.CreateNSWMul(LHS, RHS);
      HigherBits = Builder.CreateAShr(MulResult, 64);
    } else {
      MulResult = Builder.CreateNUWMul(LHS, RHS);
      HigherBits = Builder.CreateLShr(MulResult, 64);
    }
    HigherBits = Builder.CreateIntCast(HigherBits, ResType, IsSigned);

    if (BuiltinID == X86::BI__mulh || BuiltinID == X86::BI__umulh)
      return HigherBits;

    Address HighBitsAddress = EmitPointerWithAlignment(E->getArg(2));
    Builder.CreateStore(HigherBits, HighBitsAddress);
    return Builder.CreateIntCast(MulResult, ResType, IsSigned);
  }

  case X86::BI__faststorefence: {
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::System);
  }
  case X86::BI__shiftleft128:
  case X86::BI__shiftright128: {
    // FIXME: Once fshl/fshr no longer add an unneeded and and cmov, do this:
    // llvm::Function *F = CGM.getIntrinsic(
    //   BuiltinID == X86::BI__shiftleft128 ? Intrinsic::fshl : Intrinsic::fshr,
    //   Int64Ty);
    // Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    // return Builder.CreateCall(F, Ops);
    llvm::Type *Int128Ty = Builder.getInt128Ty();
    Value *Val = Builder.CreateOr(
        Builder.CreateShl(Builder.CreateZExt(Ops[1], Int128Ty), 64),
        Builder.CreateZExt(Ops[0], Int128Ty));
    Value *Amt = Builder.CreateAnd(Builder.CreateZExt(Ops[2], Int128Ty),
                                   llvm::ConstantInt::get(Int128Ty, 0x3f));
    Value *Res;
    if (BuiltinID == X86::BI__shiftleft128)
      Res = Builder.CreateLShr(Builder.CreateShl(Val, Amt), 64);
    else
      Res = Builder.CreateLShr(Val, Amt);
    return Builder.CreateTrunc(Res, Int64Ty);
  }
  case X86::BI_ReadWriteBarrier:
  case X86::BI_ReadBarrier:
  case X86::BI_WriteBarrier: {
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::SingleThread);
  }
  case X86::BI_BitScanForward:
  case X86::BI_BitScanForward64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanForward, E);
  case X86::BI_BitScanReverse:
  case X86::BI_BitScanReverse64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_BitScanReverse, E);

  case X86::BI_InterlockedAnd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E);
  case X86::BI_InterlockedExchange64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E);
  case X86::BI_InterlockedExchangeAdd64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E);
  case X86::BI_InterlockedExchangeSub64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E);
  case X86::BI_InterlockedOr64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E);
  case X86::BI_InterlockedXor64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E);
  case X86::BI_InterlockedDecrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E);
  case X86::BI_InterlockedIncrement64:
    return EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E);
  case X86::BI_InterlockedCompareExchange128: {
    // InterlockedCompareExchange128 doesn't directly refer to 128bit ints,
    // instead it takes pointers to 64bit ints for Destination and
    // ComparandResult, and exchange is taken as two 64bit ints (high & low).
    // The previous value is written to ComparandResult, and success is
    // returned.

    llvm::Type *Int128Ty = Builder.getInt128Ty();
    llvm::Type *Int128PtrTy = Int128Ty->getPointerTo();

    Value *Destination =
        Builder.CreateBitCast(Ops[0], Int128PtrTy);
    Value *ExchangeHigh128 = Builder.CreateZExt(Ops[1], Int128Ty);
    Value *ExchangeLow128 = Builder.CreateZExt(Ops[2], Int128Ty);
    Address ComparandResult(Builder.CreateBitCast(Ops[3], Int128PtrTy),
                            getContext().toCharUnitsFromBits(128));

    Value *Exchange = Builder.CreateOr(
        Builder.CreateShl(ExchangeHigh128, 64, "", false, false),
        ExchangeLow128);

    Value *Comparand = Builder.CreateLoad(ComparandResult);

    AtomicCmpXchgInst *CXI =
        Builder.CreateAtomicCmpXchg(Destination, Comparand, Exchange,
                                    AtomicOrdering::SequentiallyConsistent,
                                    AtomicOrdering::SequentiallyConsistent);
    CXI->setVolatile(true);

    // Write the result back to the inout pointer.
    Builder.CreateStore(Builder.CreateExtractValue(CXI, 0), ComparandResult);

    // Get the success boolean and zero extend it to i8.
    Value *Success = Builder.CreateExtractValue(CXI, 1);
    return Builder.CreateZExt(Success, ConvertType(E->getType()));
  }

  case X86::BI_AddressOfReturnAddress: {
    Value *F = CGM.getIntrinsic(Intrinsic::addressofreturnaddress);
    return Builder.CreateCall(F);
  }
  case X86::BI__stosb: {
    // We treat __stosb as a volatile memset - it may not generate "rep stosb"
    // instruction, but it will create a memset that won't be optimized away.
    return Builder.CreateMemSet(Ops[0], Ops[1], Ops[2], 1, true);
  }
  case X86::BI__ud2:
    // llvm.trap makes a ud2a instruction on x86.
    return EmitTrapCall(Intrinsic::trap);
  case X86::BI__int2c: {
    // This syscall signals a driver assertion failure in x86 NT kernels.
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
    llvm::InlineAsm *IA =
        llvm::InlineAsm::get(FTy, "int $$0x2c", "", /*SideEffects=*/true);
    llvm::AttributeList NoReturnAttr = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex,
        llvm::Attribute::NoReturn);
    CallSite CS = Builder.CreateCall(IA);
    CS.setAttributes(NoReturnAttr);
    return CS.getInstruction();
  }
  case X86::BI__readfsbyte:
  case X86::BI__readfsword:
  case X86::BI__readfsdword:
  case X86::BI__readfsqword: {
    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Ptr =
        Builder.CreateIntToPtr(Ops[0], llvm::PointerType::get(IntTy, 257));
    LoadInst *Load = Builder.CreateAlignedLoad(
        IntTy, Ptr, getContext().getTypeAlignInChars(E->getType()));
    Load->setVolatile(true);
    return Load;
  }
  case X86::BI__readgsbyte:
  case X86::BI__readgsword:
  case X86::BI__readgsdword:
  case X86::BI__readgsqword: {
    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Ptr =
        Builder.CreateIntToPtr(Ops[0], llvm::PointerType::get(IntTy, 256));
    LoadInst *Load = Builder.CreateAlignedLoad(
        IntTy, Ptr, getContext().getTypeAlignInChars(E->getType()));
    Load->setVolatile(true);
    return Load;
  }
  case X86::BI__builtin_ia32_paddsb512:
  case X86::BI__builtin_ia32_paddsw512:
  case X86::BI__builtin_ia32_paddsb256:
  case X86::BI__builtin_ia32_paddsw256:
  case X86::BI__builtin_ia32_paddsb128:
  case X86::BI__builtin_ia32_paddsw128:
    return EmitX86AddSubSatExpr(*this, Ops, true, true);
  case X86::BI__builtin_ia32_paddusb512:
  case X86::BI__builtin_ia32_paddusw512:
  case X86::BI__builtin_ia32_paddusb256:
  case X86::BI__builtin_ia32_paddusw256:
  case X86::BI__builtin_ia32_paddusb128:
  case X86::BI__builtin_ia32_paddusw128:
    return EmitX86AddSubSatExpr(*this, Ops, false, true);
  case X86::BI__builtin_ia32_psubsb512:
  case X86::BI__builtin_ia32_psubsw512:
  case X86::BI__builtin_ia32_psubsb256:
  case X86::BI__builtin_ia32_psubsw256:
  case X86::BI__builtin_ia32_psubsb128:
  case X86::BI__builtin_ia32_psubsw128:
    return EmitX86AddSubSatExpr(*this, Ops, true, false);
  case X86::BI__builtin_ia32_psubusb512:
  case X86::BI__builtin_ia32_psubusw512:
  case X86::BI__builtin_ia32_psubusb256:
  case X86::BI__builtin_ia32_psubusw256:
  case X86::BI__builtin_ia32_psubusb128:
  case X86::BI__builtin_ia32_psubusw128:
    return EmitX86AddSubSatExpr(*this, Ops, false, false);
  }
}

Value *CodeGenFunction::EmitPPCBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  SmallVector<Value*, 4> Ops;

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++)
    Ops.push_back(EmitScalarExpr(E->getArg(i)));

  Intrinsic::ID ID = Intrinsic::not_intrinsic;

  switch (BuiltinID) {
  default: return nullptr;

  // __builtin_ppc_get_timebase is GCC 4.8+'s PowerPC-specific name for what we
  // call __builtin_readcyclecounter.
  case PPC::BI__builtin_ppc_get_timebase:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::readcyclecounter));

  // vec_ld, vec_xl_be, vec_lvsl, vec_lvsr
  case PPC::BI__builtin_altivec_lvx:
  case PPC::BI__builtin_altivec_lvxl:
  case PPC::BI__builtin_altivec_lvebx:
  case PPC::BI__builtin_altivec_lvehx:
  case PPC::BI__builtin_altivec_lvewx:
  case PPC::BI__builtin_altivec_lvsl:
  case PPC::BI__builtin_altivec_lvsr:
  case PPC::BI__builtin_vsx_lxvd2x:
  case PPC::BI__builtin_vsx_lxvw4x:
  case PPC::BI__builtin_vsx_lxvd2x_be:
  case PPC::BI__builtin_vsx_lxvw4x_be:
  case PPC::BI__builtin_vsx_lxvl:
  case PPC::BI__builtin_vsx_lxvll:
  {
    if(BuiltinID == PPC::BI__builtin_vsx_lxvl ||
       BuiltinID == PPC::BI__builtin_vsx_lxvll){
      Ops[0] = Builder.CreateBitCast(Ops[0], Int8PtrTy);
    }else {
      Ops[1] = Builder.CreateBitCast(Ops[1], Int8PtrTy);
      Ops[0] = Builder.CreateGEP(Ops[1], Ops[0]);
      Ops.pop_back();
    }

    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported ld/lvsl/lvsr intrinsic!");
    case PPC::BI__builtin_altivec_lvx:
      ID = Intrinsic::ppc_altivec_lvx;
      break;
    case PPC::BI__builtin_altivec_lvxl:
      ID = Intrinsic::ppc_altivec_lvxl;
      break;
    case PPC::BI__builtin_altivec_lvebx:
      ID = Intrinsic::ppc_altivec_lvebx;
      break;
    case PPC::BI__builtin_altivec_lvehx:
      ID = Intrinsic::ppc_altivec_lvehx;
      break;
    case PPC::BI__builtin_altivec_lvewx:
      ID = Intrinsic::ppc_altivec_lvewx;
      break;
    case PPC::BI__builtin_altivec_lvsl:
      ID = Intrinsic::ppc_altivec_lvsl;
      break;
    case PPC::BI__builtin_altivec_lvsr:
      ID = Intrinsic::ppc_altivec_lvsr;
      break;
    case PPC::BI__builtin_vsx_lxvd2x:
      ID = Intrinsic::ppc_vsx_lxvd2x;
      break;
    case PPC::BI__builtin_vsx_lxvw4x:
      ID = Intrinsic::ppc_vsx_lxvw4x;
      break;
    case PPC::BI__builtin_vsx_lxvd2x_be:
      ID = Intrinsic::ppc_vsx_lxvd2x_be;
      break;
    case PPC::BI__builtin_vsx_lxvw4x_be:
      ID = Intrinsic::ppc_vsx_lxvw4x_be;
      break;
    case PPC::BI__builtin_vsx_lxvl:
      ID = Intrinsic::ppc_vsx_lxvl;
      break;
    case PPC::BI__builtin_vsx_lxvll:
      ID = Intrinsic::ppc_vsx_lxvll;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops, "");
  }

  // vec_st, vec_xst_be
  case PPC::BI__builtin_altivec_stvx:
  case PPC::BI__builtin_altivec_stvxl:
  case PPC::BI__builtin_altivec_stvebx:
  case PPC::BI__builtin_altivec_stvehx:
  case PPC::BI__builtin_altivec_stvewx:
  case PPC::BI__builtin_vsx_stxvd2x:
  case PPC::BI__builtin_vsx_stxvw4x:
  case PPC::BI__builtin_vsx_stxvd2x_be:
  case PPC::BI__builtin_vsx_stxvw4x_be:
  case PPC::BI__builtin_vsx_stxvl:
  case PPC::BI__builtin_vsx_stxvll:
  {
    if(BuiltinID == PPC::BI__builtin_vsx_stxvl ||
      BuiltinID == PPC::BI__builtin_vsx_stxvll ){
      Ops[1] = Builder.CreateBitCast(Ops[1], Int8PtrTy);
    }else {
      Ops[2] = Builder.CreateBitCast(Ops[2], Int8PtrTy);
      Ops[1] = Builder.CreateGEP(Ops[2], Ops[1]);
      Ops.pop_back();
    }

    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported st intrinsic!");
    case PPC::BI__builtin_altivec_stvx:
      ID = Intrinsic::ppc_altivec_stvx;
      break;
    case PPC::BI__builtin_altivec_stvxl:
      ID = Intrinsic::ppc_altivec_stvxl;
      break;
    case PPC::BI__builtin_altivec_stvebx:
      ID = Intrinsic::ppc_altivec_stvebx;
      break;
    case PPC::BI__builtin_altivec_stvehx:
      ID = Intrinsic::ppc_altivec_stvehx;
      break;
    case PPC::BI__builtin_altivec_stvewx:
      ID = Intrinsic::ppc_altivec_stvewx;
      break;
    case PPC::BI__builtin_vsx_stxvd2x:
      ID = Intrinsic::ppc_vsx_stxvd2x;
      break;
    case PPC::BI__builtin_vsx_stxvw4x:
      ID = Intrinsic::ppc_vsx_stxvw4x;
      break;
    case PPC::BI__builtin_vsx_stxvd2x_be:
      ID = Intrinsic::ppc_vsx_stxvd2x_be;
      break;
    case PPC::BI__builtin_vsx_stxvw4x_be:
      ID = Intrinsic::ppc_vsx_stxvw4x_be;
      break;
    case PPC::BI__builtin_vsx_stxvl:
      ID = Intrinsic::ppc_vsx_stxvl;
      break;
    case PPC::BI__builtin_vsx_stxvll:
      ID = Intrinsic::ppc_vsx_stxvll;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops, "");
  }
  // Square root
  case PPC::BI__builtin_vsx_xvsqrtsp:
  case PPC::BI__builtin_vsx_xvsqrtdp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    ID = Intrinsic::sqrt;
    llvm::Function *F = CGM.getIntrinsic(ID, ResultType);
    return Builder.CreateCall(F, X);
  }
  // Count leading zeros
  case PPC::BI__builtin_altivec_vclzb:
  case PPC::BI__builtin_altivec_vclzh:
  case PPC::BI__builtin_altivec_vclzw:
  case PPC::BI__builtin_altivec_vclzd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }
  case PPC::BI__builtin_altivec_vctzb:
  case PPC::BI__builtin_altivec_vctzh:
  case PPC::BI__builtin_altivec_vctzw:
  case PPC::BI__builtin_altivec_vctzd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }
  case PPC::BI__builtin_altivec_vpopcntb:
  case PPC::BI__builtin_altivec_vpopcnth:
  case PPC::BI__builtin_altivec_vpopcntw:
  case PPC::BI__builtin_altivec_vpopcntd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, X);
  }
  // Copy sign
  case PPC::BI__builtin_vsx_xvcpsgnsp:
  case PPC::BI__builtin_vsx_xvcpsgndp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    ID = Intrinsic::copysign;
    llvm::Function *F = CGM.getIntrinsic(ID, ResultType);
    return Builder.CreateCall(F, {X, Y});
  }
  // Rounding/truncation
  case PPC::BI__builtin_vsx_xvrspip:
  case PPC::BI__builtin_vsx_xvrdpip:
  case PPC::BI__builtin_vsx_xvrdpim:
  case PPC::BI__builtin_vsx_xvrspim:
  case PPC::BI__builtin_vsx_xvrdpi:
  case PPC::BI__builtin_vsx_xvrspi:
  case PPC::BI__builtin_vsx_xvrdpic:
  case PPC::BI__builtin_vsx_xvrspic:
  case PPC::BI__builtin_vsx_xvrdpiz:
  case PPC::BI__builtin_vsx_xvrspiz: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    if (BuiltinID == PPC::BI__builtin_vsx_xvrdpim ||
        BuiltinID == PPC::BI__builtin_vsx_xvrspim)
      ID = Intrinsic::floor;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpi ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspi)
      ID = Intrinsic::round;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpic ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspic)
      ID = Intrinsic::nearbyint;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpip ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspip)
      ID = Intrinsic::ceil;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpiz ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspiz)
      ID = Intrinsic::trunc;
    llvm::Function *F = CGM.getIntrinsic(ID, ResultType);
    return Builder.CreateCall(F, X);
  }

  // Absolute value
  case PPC::BI__builtin_vsx_xvabsdp:
  case PPC::BI__builtin_vsx_xvabssp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateCall(F, X);
  }

  // FMA variations
  case PPC::BI__builtin_vsx_xvmaddadp:
  case PPC::BI__builtin_vsx_xvmaddasp:
  case PPC::BI__builtin_vsx_xvnmaddadp:
  case PPC::BI__builtin_vsx_xvnmaddasp:
  case PPC::BI__builtin_vsx_xvmsubadp:
  case PPC::BI__builtin_vsx_xvmsubasp:
  case PPC::BI__builtin_vsx_xvnmsubadp:
  case PPC::BI__builtin_vsx_xvnmsubasp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(ResultType);
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    switch (BuiltinID) {
      case PPC::BI__builtin_vsx_xvmaddadp:
      case PPC::BI__builtin_vsx_xvmaddasp:
        return Builder.CreateCall(F, {X, Y, Z});
      case PPC::BI__builtin_vsx_xvnmaddadp:
      case PPC::BI__builtin_vsx_xvnmaddasp:
        return Builder.CreateFSub(Zero,
                                  Builder.CreateCall(F, {X, Y, Z}), "sub");
      case PPC::BI__builtin_vsx_xvmsubadp:
      case PPC::BI__builtin_vsx_xvmsubasp:
        return Builder.CreateCall(F,
                                  {X, Y, Builder.CreateFSub(Zero, Z, "sub")});
      case PPC::BI__builtin_vsx_xvnmsubadp:
      case PPC::BI__builtin_vsx_xvnmsubasp:
        Value *FsubRes =
          Builder.CreateCall(F, {X, Y, Builder.CreateFSub(Zero, Z, "sub")});
        return Builder.CreateFSub(Zero, FsubRes, "sub");
    }
    llvm_unreachable("Unknown FMA operation");
    return nullptr; // Suppress no-return warning
  }

  case PPC::BI__builtin_vsx_insertword: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_vsx_xxinsertw);

    // Third argument is a compile time constant int. It must be clamped to
    // to the range [0, 12].
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Ops[2]);
    assert(ArgCI &&
           "Third arg to xxinsertw intrinsic must be constant integer");
    const int64_t MaxIndex = 12;
    int64_t Index = clamp(ArgCI->getSExtValue(), 0, MaxIndex);

    // The builtin semantics don't exactly match the xxinsertw instructions
    // semantics (which ppc_vsx_xxinsertw follows). The builtin extracts the
    // word from the first argument, and inserts it in the second argument. The
    // instruction extracts the word from its second input register and inserts
    // it into its first input register, so swap the first and second arguments.
    std::swap(Ops[0], Ops[1]);

    // Need to cast the second argument from a vector of unsigned int to a
    // vector of long long.
    Ops[1] = Builder.CreateBitCast(Ops[1], llvm::VectorType::get(Int64Ty, 2));

    if (getTarget().isLittleEndian()) {
      // Create a shuffle mask of (1, 0)
      Constant *ShuffleElts[2] = { ConstantInt::get(Int32Ty, 1),
                                   ConstantInt::get(Int32Ty, 0)
                                 };
      Constant *ShuffleMask = llvm::ConstantVector::get(ShuffleElts);

      // Reverse the double words in the vector we will extract from.
      Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int64Ty, 2));
      Ops[0] = Builder.CreateShuffleVector(Ops[0], Ops[0], ShuffleMask);

      // Reverse the index.
      Index = MaxIndex - Index;
    }

    // Intrinsic expects the first arg to be a vector of int.
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int32Ty, 4));
    Ops[2] = ConstantInt::getSigned(Int32Ty, Index);
    return Builder.CreateCall(F, Ops);
  }

  case PPC::BI__builtin_vsx_extractuword: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_vsx_xxextractuw);

    // Intrinsic expects the first argument to be a vector of doublewords.
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int64Ty, 2));

    // The second argument is a compile time constant int that needs to
    // be clamped to the range [0, 12].
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Ops[1]);
    assert(ArgCI &&
           "Second Arg to xxextractuw intrinsic must be a constant integer!");
    const int64_t MaxIndex = 12;
    int64_t Index = clamp(ArgCI->getSExtValue(), 0, MaxIndex);

    if (getTarget().isLittleEndian()) {
      // Reverse the index.
      Index = MaxIndex - Index;
      Ops[1] = ConstantInt::getSigned(Int32Ty, Index);

      // Emit the call, then reverse the double words of the results vector.
      Value *Call = Builder.CreateCall(F, Ops);

      // Create a shuffle mask of (1, 0)
      Constant *ShuffleElts[2] = { ConstantInt::get(Int32Ty, 1),
                                   ConstantInt::get(Int32Ty, 0)
                                 };
      Constant *ShuffleMask = llvm::ConstantVector::get(ShuffleElts);

      Value *ShuffleCall = Builder.CreateShuffleVector(Call, Call, ShuffleMask);
      return ShuffleCall;
    } else {
      Ops[1] = ConstantInt::getSigned(Int32Ty, Index);
      return Builder.CreateCall(F, Ops);
    }
  }

  case PPC::BI__builtin_vsx_xxpermdi: {
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Ops[2]);
    assert(ArgCI && "Third arg must be constant integer!");

    unsigned Index = ArgCI->getZExtValue();
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int64Ty, 2));
    Ops[1] = Builder.CreateBitCast(Ops[1], llvm::VectorType::get(Int64Ty, 2));

    // Account for endianness by treating this as just a shuffle. So we use the
    // same indices for both LE and BE in order to produce expected results in
    // both cases.
    unsigned ElemIdx0 = (Index & 2) >> 1;
    unsigned ElemIdx1 = 2 + (Index & 1);

    Constant *ShuffleElts[2] = {ConstantInt::get(Int32Ty, ElemIdx0),
                                ConstantInt::get(Int32Ty, ElemIdx1)};
    Constant *ShuffleMask = llvm::ConstantVector::get(ShuffleElts);

    Value *ShuffleCall =
        Builder.CreateShuffleVector(Ops[0], Ops[1], ShuffleMask);
    QualType BIRetType = E->getType();
    auto RetTy = ConvertType(BIRetType);
    return Builder.CreateBitCast(ShuffleCall, RetTy);
  }

  case PPC::BI__builtin_vsx_xxsldwi: {
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Ops[2]);
    assert(ArgCI && "Third argument must be a compile time constant");
    unsigned Index = ArgCI->getZExtValue() & 0x3;
    Ops[0] = Builder.CreateBitCast(Ops[0], llvm::VectorType::get(Int32Ty, 4));
    Ops[1] = Builder.CreateBitCast(Ops[1], llvm::VectorType::get(Int32Ty, 4));

    // Create a shuffle mask
    unsigned ElemIdx0;
    unsigned ElemIdx1;
    unsigned ElemIdx2;
    unsigned ElemIdx3;
    if (getTarget().isLittleEndian()) {
      // Little endian element N comes from element 8+N-Index of the
      // concatenated wide vector (of course, using modulo arithmetic on
      // the total number of elements).
      ElemIdx0 = (8 - Index) % 8;
      ElemIdx1 = (9 - Index) % 8;
      ElemIdx2 = (10 - Index) % 8;
      ElemIdx3 = (11 - Index) % 8;
    } else {
      // Big endian ElemIdx<N> = Index + N
      ElemIdx0 = Index;
      ElemIdx1 = Index + 1;
      ElemIdx2 = Index + 2;
      ElemIdx3 = Index + 3;
    }

    Constant *ShuffleElts[4] = {ConstantInt::get(Int32Ty, ElemIdx0),
                                ConstantInt::get(Int32Ty, ElemIdx1),
                                ConstantInt::get(Int32Ty, ElemIdx2),
                                ConstantInt::get(Int32Ty, ElemIdx3)};

    Constant *ShuffleMask = llvm::ConstantVector::get(ShuffleElts);
    Value *ShuffleCall =
        Builder.CreateShuffleVector(Ops[0], Ops[1], ShuffleMask);
    QualType BIRetType = E->getType();
    auto RetTy = ConvertType(BIRetType);
    return Builder.CreateBitCast(ShuffleCall, RetTy);
  }

  case PPC::BI__builtin_pack_vector_int128: {
    bool isLittleEndian = getTarget().isLittleEndian();
    Value *UndefValue =
        llvm::UndefValue::get(llvm::VectorType::get(Ops[0]->getType(), 2));
    Value *Res = Builder.CreateInsertElement(
        UndefValue, Ops[0], (uint64_t)(isLittleEndian ? 1 : 0));
    Res = Builder.CreateInsertElement(Res, Ops[1],
                                      (uint64_t)(isLittleEndian ? 0 : 1));
    return Builder.CreateBitCast(Res, ConvertType(E->getType()));
  }

  case PPC::BI__builtin_unpack_vector_int128: {
    ConstantInt *Index = cast<ConstantInt>(Ops[1]);
    Value *Unpacked = Builder.CreateBitCast(
        Ops[0], llvm::VectorType::get(ConvertType(E->getType()), 2));

    if (getTarget().isLittleEndian())
      Index = ConstantInt::get(Index->getType(), 1 - Index->getZExtValue());

    return Builder.CreateExtractElement(Unpacked, Index);
  }
  }
}

Value *CodeGenFunction::EmitAMDGPUBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  switch (BuiltinID) {
  case AMDGPU::BI__builtin_amdgcn_div_scale:
  case AMDGPU::BI__builtin_amdgcn_div_scalef: {
    // Translate from the intrinsics's struct return to the builtin's out
    // argument.

    Address FlagOutPtr = EmitPointerWithAlignment(E->getArg(3));

    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    llvm::Value *Z = EmitScalarExpr(E->getArg(2));

    llvm::Value *Callee = CGM.getIntrinsic(Intrinsic::amdgcn_div_scale,
                                           X->getType());

    llvm::Value *Tmp = Builder.CreateCall(Callee, {X, Y, Z});

    llvm::Value *Result = Builder.CreateExtractValue(Tmp, 0);
    llvm::Value *Flag = Builder.CreateExtractValue(Tmp, 1);

    llvm::Type *RealFlagType
      = FlagOutPtr.getPointer()->getType()->getPointerElementType();

    llvm::Value *FlagExt = Builder.CreateZExt(Flag, RealFlagType);
    Builder.CreateStore(FlagExt, FlagOutPtr);
    return Result;
  }
  case AMDGPU::BI__builtin_amdgcn_div_fmas:
  case AMDGPU::BI__builtin_amdgcn_div_fmasf: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Value *Src2 = EmitScalarExpr(E->getArg(2));
    llvm::Value *Src3 = EmitScalarExpr(E->getArg(3));

    llvm::Value *F = CGM.getIntrinsic(Intrinsic::amdgcn_div_fmas,
                                      Src0->getType());
    llvm::Value *Src3ToBool = Builder.CreateIsNotNull(Src3);
    return Builder.CreateCall(F, {Src0, Src1, Src2, Src3ToBool});
  }

  case AMDGPU::BI__builtin_amdgcn_ds_swizzle:
    return emitBinaryBuiltin(*this, E, Intrinsic::amdgcn_ds_swizzle);
  case AMDGPU::BI__builtin_amdgcn_mov_dpp:
  case AMDGPU::BI__builtin_amdgcn_update_dpp: {
    llvm::SmallVector<llvm::Value *, 6> Args;
    for (unsigned I = 0; I != E->getNumArgs(); ++I)
      Args.push_back(EmitScalarExpr(E->getArg(I)));
    assert(Args.size() == 5 || Args.size() == 6);
    if (Args.size() == 5)
      Args.insert(Args.begin(), llvm::UndefValue::get(Args[0]->getType()));
    Value *F =
        CGM.getIntrinsic(Intrinsic::amdgcn_update_dpp, Args[0]->getType());
    return Builder.CreateCall(F, Args);
  }
  case AMDGPU::BI__builtin_amdgcn_div_fixup:
  case AMDGPU::BI__builtin_amdgcn_div_fixupf:
  case AMDGPU::BI__builtin_amdgcn_div_fixuph:
    return emitTernaryBuiltin(*this, E, Intrinsic::amdgcn_div_fixup);
  case AMDGPU::BI__builtin_amdgcn_trig_preop:
  case AMDGPU::BI__builtin_amdgcn_trig_preopf:
    return emitFPIntBuiltin(*this, E, Intrinsic::amdgcn_trig_preop);
  case AMDGPU::BI__builtin_amdgcn_rcp:
  case AMDGPU::BI__builtin_amdgcn_rcpf:
  case AMDGPU::BI__builtin_amdgcn_rcph:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_rcp);
  case AMDGPU::BI__builtin_amdgcn_rsq:
  case AMDGPU::BI__builtin_amdgcn_rsqf:
  case AMDGPU::BI__builtin_amdgcn_rsqh:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_rsq);
  case AMDGPU::BI__builtin_amdgcn_rsq_clamp:
  case AMDGPU::BI__builtin_amdgcn_rsq_clampf:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_rsq_clamp);
  case AMDGPU::BI__builtin_amdgcn_sinf:
  case AMDGPU::BI__builtin_amdgcn_sinh:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_sin);
  case AMDGPU::BI__builtin_amdgcn_cosf:
  case AMDGPU::BI__builtin_amdgcn_cosh:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_cos);
  case AMDGPU::BI__builtin_amdgcn_log_clampf:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_log_clamp);
  case AMDGPU::BI__builtin_amdgcn_ldexp:
  case AMDGPU::BI__builtin_amdgcn_ldexpf:
  case AMDGPU::BI__builtin_amdgcn_ldexph:
    return emitFPIntBuiltin(*this, E, Intrinsic::amdgcn_ldexp);
  case AMDGPU::BI__builtin_amdgcn_frexp_mant:
  case AMDGPU::BI__builtin_amdgcn_frexp_mantf:
  case AMDGPU::BI__builtin_amdgcn_frexp_manth:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_frexp_mant);
  case AMDGPU::BI__builtin_amdgcn_frexp_exp:
  case AMDGPU::BI__builtin_amdgcn_frexp_expf: {
    Value *Src0 = EmitScalarExpr(E->getArg(0));
    Value *F = CGM.getIntrinsic(Intrinsic::amdgcn_frexp_exp,
                                { Builder.getInt32Ty(), Src0->getType() });
    return Builder.CreateCall(F, Src0);
  }
  case AMDGPU::BI__builtin_amdgcn_frexp_exph: {
    Value *Src0 = EmitScalarExpr(E->getArg(0));
    Value *F = CGM.getIntrinsic(Intrinsic::amdgcn_frexp_exp,
                                { Builder.getInt16Ty(), Src0->getType() });
    return Builder.CreateCall(F, Src0);
  }
  case AMDGPU::BI__builtin_amdgcn_fract:
  case AMDGPU::BI__builtin_amdgcn_fractf:
  case AMDGPU::BI__builtin_amdgcn_fracth:
    return emitUnaryBuiltin(*this, E, Intrinsic::amdgcn_fract);
  case AMDGPU::BI__builtin_amdgcn_lerp:
    return emitTernaryBuiltin(*this, E, Intrinsic::amdgcn_lerp);
  case AMDGPU::BI__builtin_amdgcn_uicmp:
  case AMDGPU::BI__builtin_amdgcn_uicmpl:
  case AMDGPU::BI__builtin_amdgcn_sicmp:
  case AMDGPU::BI__builtin_amdgcn_sicmpl:
    return emitTernaryBuiltin(*this, E, Intrinsic::amdgcn_icmp);
  case AMDGPU::BI__builtin_amdgcn_fcmp:
  case AMDGPU::BI__builtin_amdgcn_fcmpf:
    return emitTernaryBuiltin(*this, E, Intrinsic::amdgcn_fcmp);
  case AMDGPU::BI__builtin_amdgcn_class:
  case AMDGPU::BI__builtin_amdgcn_classf:
  case AMDGPU::BI__builtin_amdgcn_classh:
    return emitFPIntBuiltin(*this, E, Intrinsic::amdgcn_class);
  case AMDGPU::BI__builtin_amdgcn_fmed3f:
  case AMDGPU::BI__builtin_amdgcn_fmed3h:
    return emitTernaryBuiltin(*this, E, Intrinsic::amdgcn_fmed3);
  case AMDGPU::BI__builtin_amdgcn_read_exec: {
    CallInst *CI = cast<CallInst>(
      EmitSpecialRegisterBuiltin(*this, E, Int64Ty, Int64Ty, true, "exec"));
    CI->setConvergent();
    return CI;
  }
  case AMDGPU::BI__builtin_amdgcn_read_exec_lo:
  case AMDGPU::BI__builtin_amdgcn_read_exec_hi: {
    StringRef RegName = BuiltinID == AMDGPU::BI__builtin_amdgcn_read_exec_lo ?
      "exec_lo" : "exec_hi";
    CallInst *CI = cast<CallInst>(
      EmitSpecialRegisterBuiltin(*this, E, Int32Ty, Int32Ty, true, RegName));
    CI->setConvergent();
    return CI;
  }
  // amdgcn workitem
  case AMDGPU::BI__builtin_amdgcn_workitem_id_x:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_x, 0, 1024);
  case AMDGPU::BI__builtin_amdgcn_workitem_id_y:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_y, 0, 1024);
  case AMDGPU::BI__builtin_amdgcn_workitem_id_z:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_z, 0, 1024);

  // r600 intrinsics
  case AMDGPU::BI__builtin_r600_recipsqrt_ieee:
  case AMDGPU::BI__builtin_r600_recipsqrt_ieeef:
    return emitUnaryBuiltin(*this, E, Intrinsic::r600_recipsqrt_ieee);
  case AMDGPU::BI__builtin_r600_read_tidig_x:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_x, 0, 1024);
  case AMDGPU::BI__builtin_r600_read_tidig_y:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_y, 0, 1024);
  case AMDGPU::BI__builtin_r600_read_tidig_z:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_z, 0, 1024);
  default:
    return nullptr;
  }
}

/// Handle a SystemZ function in which the final argument is a pointer
/// to an int that receives the post-instruction CC value.  At the LLVM level
/// this is represented as a function that returns a {result, cc} pair.
static Value *EmitSystemZIntrinsicWithCC(CodeGenFunction &CGF,
                                         unsigned IntrinsicID,
                                         const CallExpr *E) {
  unsigned NumArgs = E->getNumArgs() - 1;
  SmallVector<Value *, 8> Args(NumArgs);
  for (unsigned I = 0; I < NumArgs; ++I)
    Args[I] = CGF.EmitScalarExpr(E->getArg(I));
  Address CCPtr = CGF.EmitPointerWithAlignment(E->getArg(NumArgs));
  Value *F = CGF.CGM.getIntrinsic(IntrinsicID);
  Value *Call = CGF.Builder.CreateCall(F, Args);
  Value *CC = CGF.Builder.CreateExtractValue(Call, 1);
  CGF.Builder.CreateStore(CC, CCPtr);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

Value *CodeGenFunction::EmitSystemZBuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E) {
  switch (BuiltinID) {
  case SystemZ::BI__builtin_tbegin: {
    Value *TDB = EmitScalarExpr(E->getArg(0));
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff0c);
    Value *F = CGM.getIntrinsic(Intrinsic::s390_tbegin);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tbegin_nofloat: {
    Value *TDB = EmitScalarExpr(E->getArg(0));
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff0c);
    Value *F = CGM.getIntrinsic(Intrinsic::s390_tbegin_nofloat);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tbeginc: {
    Value *TDB = llvm::ConstantPointerNull::get(Int8PtrTy);
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff08);
    Value *F = CGM.getIntrinsic(Intrinsic::s390_tbeginc);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tabort: {
    Value *Data = EmitScalarExpr(E->getArg(0));
    Value *F = CGM.getIntrinsic(Intrinsic::s390_tabort);
    return Builder.CreateCall(F, Builder.CreateSExt(Data, Int64Ty, "tabort"));
  }
  case SystemZ::BI__builtin_non_tx_store: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Data = EmitScalarExpr(E->getArg(1));
    Value *F = CGM.getIntrinsic(Intrinsic::s390_ntstg);
    return Builder.CreateCall(F, {Data, Address});
  }

  // Vector builtins.  Note that most vector builtins are mapped automatically
  // to target-specific LLVM intrinsics.  The ones handled specially here can
  // be represented via standard LLVM IR, which is preferable to enable common
  // LLVM optimizations.

  case SystemZ::BI__builtin_s390_vpopctb:
  case SystemZ::BI__builtin_s390_vpopcth:
  case SystemZ::BI__builtin_s390_vpopctf:
  case SystemZ::BI__builtin_s390_vpopctg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, X);
  }

  case SystemZ::BI__builtin_s390_vclzb:
  case SystemZ::BI__builtin_s390_vclzh:
  case SystemZ::BI__builtin_s390_vclzf:
  case SystemZ::BI__builtin_s390_vclzg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }

  case SystemZ::BI__builtin_s390_vctzb:
  case SystemZ::BI__builtin_s390_vctzh:
  case SystemZ::BI__builtin_s390_vctzf:
  case SystemZ::BI__builtin_s390_vctzg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }

  case SystemZ::BI__builtin_s390_vfsqsb:
  case SystemZ::BI__builtin_s390_vfsqdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::sqrt, ResultType);
    return Builder.CreateCall(F, X);
  }
  case SystemZ::BI__builtin_s390_vfmasb:
  case SystemZ::BI__builtin_s390_vfmadb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    return Builder.CreateCall(F, {X, Y, Z});
  }
  case SystemZ::BI__builtin_s390_vfmssb:
  case SystemZ::BI__builtin_s390_vfmsdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(ResultType);
    Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    return Builder.CreateCall(F, {X, Y, Builder.CreateFSub(Zero, Z, "sub")});
  }
  case SystemZ::BI__builtin_s390_vfnmasb:
  case SystemZ::BI__builtin_s390_vfnmadb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(ResultType);
    Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    return Builder.CreateFSub(Zero, Builder.CreateCall(F, {X, Y, Z}), "sub");
  }
  case SystemZ::BI__builtin_s390_vfnmssb:
  case SystemZ::BI__builtin_s390_vfnmsdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(ResultType);
    Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    Value *NegZ = Builder.CreateFSub(Zero, Z, "sub");
    return Builder.CreateFSub(Zero, Builder.CreateCall(F, {X, Y, NegZ}));
  }
  case SystemZ::BI__builtin_s390_vflpsb:
  case SystemZ::BI__builtin_s390_vflpdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateCall(F, X);
  }
  case SystemZ::BI__builtin_s390_vflnsb:
  case SystemZ::BI__builtin_s390_vflndb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Zero = llvm::ConstantFP::getZeroValueForNegation(ResultType);
    Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateFSub(Zero, Builder.CreateCall(F, X), "sub");
  }
  case SystemZ::BI__builtin_s390_vfisb:
  case SystemZ::BI__builtin_s390_vfidb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    // Constant-fold the M4 and M5 mask arguments.
    llvm::APSInt M4, M5;
    bool IsConstM4 = E->getArg(1)->isIntegerConstantExpr(M4, getContext());
    bool IsConstM5 = E->getArg(2)->isIntegerConstantExpr(M5, getContext());
    assert(IsConstM4 && IsConstM5 && "Constant arg isn't actually constant?");
    (void)IsConstM4; (void)IsConstM5;
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some combinations of M4 and M5.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    switch (M4.getZExtValue()) {
    default: break;
    case 0:  // IEEE-inexact exception allowed
      switch (M5.getZExtValue()) {
      default: break;
      case 0: ID = Intrinsic::rint; break;
      }
      break;
    case 4:  // IEEE-inexact exception suppressed
      switch (M5.getZExtValue()) {
      default: break;
      case 0: ID = Intrinsic::nearbyint; break;
      case 1: ID = Intrinsic::round; break;
      case 5: ID = Intrinsic::trunc; break;
      case 6: ID = Intrinsic::ceil; break;
      case 7: ID = Intrinsic::floor; break;
      }
      break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      Function *F = CGM.getIntrinsic(ID, ResultType);
      return Builder.CreateCall(F, X);
    }
    switch (BuiltinID) {
      case SystemZ::BI__builtin_s390_vfisb: ID = Intrinsic::s390_vfisb; break;
      case SystemZ::BI__builtin_s390_vfidb: ID = Intrinsic::s390_vfidb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    Value *M5Value = llvm::ConstantInt::get(getLLVMContext(), M5);
    return Builder.CreateCall(F, {X, M4Value, M5Value});
  }
  case SystemZ::BI__builtin_s390_vfmaxsb:
  case SystemZ::BI__builtin_s390_vfmaxdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    // Constant-fold the M4 mask argument.
    llvm::APSInt M4;
    bool IsConstM4 = E->getArg(2)->isIntegerConstantExpr(M4, getContext());
    assert(IsConstM4 && "Constant arg isn't actually constant?");
    (void)IsConstM4;
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some values of M4.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    switch (M4.getZExtValue()) {
    default: break;
    case 4: ID = Intrinsic::maxnum; break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      Function *F = CGM.getIntrinsic(ID, ResultType);
      return Builder.CreateCall(F, {X, Y});
    }
    switch (BuiltinID) {
      case SystemZ::BI__builtin_s390_vfmaxsb: ID = Intrinsic::s390_vfmaxsb; break;
      case SystemZ::BI__builtin_s390_vfmaxdb: ID = Intrinsic::s390_vfmaxdb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    return Builder.CreateCall(F, {X, Y, M4Value});
  }
  case SystemZ::BI__builtin_s390_vfminsb:
  case SystemZ::BI__builtin_s390_vfmindb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    // Constant-fold the M4 mask argument.
    llvm::APSInt M4;
    bool IsConstM4 = E->getArg(2)->isIntegerConstantExpr(M4, getContext());
    assert(IsConstM4 && "Constant arg isn't actually constant?");
    (void)IsConstM4;
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some values of M4.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    switch (M4.getZExtValue()) {
    default: break;
    case 4: ID = Intrinsic::minnum; break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      Function *F = CGM.getIntrinsic(ID, ResultType);
      return Builder.CreateCall(F, {X, Y});
    }
    switch (BuiltinID) {
      case SystemZ::BI__builtin_s390_vfminsb: ID = Intrinsic::s390_vfminsb; break;
      case SystemZ::BI__builtin_s390_vfmindb: ID = Intrinsic::s390_vfmindb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    return Builder.CreateCall(F, {X, Y, M4Value});
  }

  // Vector intrinsics that output the post-instruction CC value.

#define INTRINSIC_WITH_CC(NAME) \
    case SystemZ::BI__builtin_##NAME: \
      return EmitSystemZIntrinsicWithCC(*this, Intrinsic::NAME, E)

  INTRINSIC_WITH_CC(s390_vpkshs);
  INTRINSIC_WITH_CC(s390_vpksfs);
  INTRINSIC_WITH_CC(s390_vpksgs);

  INTRINSIC_WITH_CC(s390_vpklshs);
  INTRINSIC_WITH_CC(s390_vpklsfs);
  INTRINSIC_WITH_CC(s390_vpklsgs);

  INTRINSIC_WITH_CC(s390_vceqbs);
  INTRINSIC_WITH_CC(s390_vceqhs);
  INTRINSIC_WITH_CC(s390_vceqfs);
  INTRINSIC_WITH_CC(s390_vceqgs);

  INTRINSIC_WITH_CC(s390_vchbs);
  INTRINSIC_WITH_CC(s390_vchhs);
  INTRINSIC_WITH_CC(s390_vchfs);
  INTRINSIC_WITH_CC(s390_vchgs);

  INTRINSIC_WITH_CC(s390_vchlbs);
  INTRINSIC_WITH_CC(s390_vchlhs);
  INTRINSIC_WITH_CC(s390_vchlfs);
  INTRINSIC_WITH_CC(s390_vchlgs);

  INTRINSIC_WITH_CC(s390_vfaebs);
  INTRINSIC_WITH_CC(s390_vfaehs);
  INTRINSIC_WITH_CC(s390_vfaefs);

  INTRINSIC_WITH_CC(s390_vfaezbs);
  INTRINSIC_WITH_CC(s390_vfaezhs);
  INTRINSIC_WITH_CC(s390_vfaezfs);

  INTRINSIC_WITH_CC(s390_vfeebs);
  INTRINSIC_WITH_CC(s390_vfeehs);
  INTRINSIC_WITH_CC(s390_vfeefs);

  INTRINSIC_WITH_CC(s390_vfeezbs);
  INTRINSIC_WITH_CC(s390_vfeezhs);
  INTRINSIC_WITH_CC(s390_vfeezfs);

  INTRINSIC_WITH_CC(s390_vfenebs);
  INTRINSIC_WITH_CC(s390_vfenehs);
  INTRINSIC_WITH_CC(s390_vfenefs);

  INTRINSIC_WITH_CC(s390_vfenezbs);
  INTRINSIC_WITH_CC(s390_vfenezhs);
  INTRINSIC_WITH_CC(s390_vfenezfs);

  INTRINSIC_WITH_CC(s390_vistrbs);
  INTRINSIC_WITH_CC(s390_vistrhs);
  INTRINSIC_WITH_CC(s390_vistrfs);

  INTRINSIC_WITH_CC(s390_vstrcbs);
  INTRINSIC_WITH_CC(s390_vstrchs);
  INTRINSIC_WITH_CC(s390_vstrcfs);

  INTRINSIC_WITH_CC(s390_vstrczbs);
  INTRINSIC_WITH_CC(s390_vstrczhs);
  INTRINSIC_WITH_CC(s390_vstrczfs);

  INTRINSIC_WITH_CC(s390_vfcesbs);
  INTRINSIC_WITH_CC(s390_vfcedbs);
  INTRINSIC_WITH_CC(s390_vfchsbs);
  INTRINSIC_WITH_CC(s390_vfchdbs);
  INTRINSIC_WITH_CC(s390_vfchesbs);
  INTRINSIC_WITH_CC(s390_vfchedbs);

  INTRINSIC_WITH_CC(s390_vftcisb);
  INTRINSIC_WITH_CC(s390_vftcidb);

#undef INTRINSIC_WITH_CC

  default:
    return nullptr;
  }
}

Value *CodeGenFunction::EmitNVPTXBuiltinExpr(unsigned BuiltinID,
                                             const CallExpr *E) {
  auto MakeLdg = [&](unsigned IntrinsicID) {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    clang::CharUnits Align =
        getNaturalPointeeTypeAlignment(E->getArg(0)->getType());
    return Builder.CreateCall(
        CGM.getIntrinsic(IntrinsicID, {Ptr->getType()->getPointerElementType(),
                                       Ptr->getType()}),
        {Ptr, ConstantInt::get(Builder.getInt32Ty(), Align.getQuantity())});
  };
  auto MakeScopedAtomic = [&](unsigned IntrinsicID) {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(IntrinsicID, {Ptr->getType()->getPointerElementType(),
                                       Ptr->getType()}),
        {Ptr, EmitScalarExpr(E->getArg(1))});
  };
  switch (BuiltinID) {
  case NVPTX::BI__nvvm_atom_add_gen_i:
  case NVPTX::BI__nvvm_atom_add_gen_l:
  case NVPTX::BI__nvvm_atom_add_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Add, E);

  case NVPTX::BI__nvvm_atom_sub_gen_i:
  case NVPTX::BI__nvvm_atom_sub_gen_l:
  case NVPTX::BI__nvvm_atom_sub_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Sub, E);

  case NVPTX::BI__nvvm_atom_and_gen_i:
  case NVPTX::BI__nvvm_atom_and_gen_l:
  case NVPTX::BI__nvvm_atom_and_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::And, E);

  case NVPTX::BI__nvvm_atom_or_gen_i:
  case NVPTX::BI__nvvm_atom_or_gen_l:
  case NVPTX::BI__nvvm_atom_or_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Or, E);

  case NVPTX::BI__nvvm_atom_xor_gen_i:
  case NVPTX::BI__nvvm_atom_xor_gen_l:
  case NVPTX::BI__nvvm_atom_xor_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Xor, E);

  case NVPTX::BI__nvvm_atom_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_xchg_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Xchg, E);

  case NVPTX::BI__nvvm_atom_max_gen_i:
  case NVPTX::BI__nvvm_atom_max_gen_l:
  case NVPTX::BI__nvvm_atom_max_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Max, E);

  case NVPTX::BI__nvvm_atom_max_gen_ui:
  case NVPTX::BI__nvvm_atom_max_gen_ul:
  case NVPTX::BI__nvvm_atom_max_gen_ull:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::UMax, E);

  case NVPTX::BI__nvvm_atom_min_gen_i:
  case NVPTX::BI__nvvm_atom_min_gen_l:
  case NVPTX::BI__nvvm_atom_min_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Min, E);

  case NVPTX::BI__nvvm_atom_min_gen_ui:
  case NVPTX::BI__nvvm_atom_min_gen_ul:
  case NVPTX::BI__nvvm_atom_min_gen_ull:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::UMin, E);

  case NVPTX::BI__nvvm_atom_cas_gen_i:
  case NVPTX::BI__nvvm_atom_cas_gen_l:
  case NVPTX::BI__nvvm_atom_cas_gen_ll:
    // __nvvm_atom_cas_gen_* should return the old value rather than the
    // success flag.
    return MakeAtomicCmpXchgValue(*this, E, /*ReturnBool=*/false);

  case NVPTX::BI__nvvm_atom_add_gen_f: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    // atomicrmw only deals with integer arguments so we need to use
    // LLVM's nvvm_atomic_load_add_f32 intrinsic for that.
    Value *FnALAF32 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_add_f32, Ptr->getType());
    return Builder.CreateCall(FnALAF32, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_atom_add_gen_d: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    // atomicrmw only deals with integer arguments, so we need to use
    // LLVM's nvvm_atomic_load_add_f64 intrinsic.
    Value *FnALAF64 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_add_f64, Ptr->getType());
    return Builder.CreateCall(FnALAF64, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_atom_inc_gen_ui: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Value *FnALI32 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_inc_32, Ptr->getType());
    return Builder.CreateCall(FnALI32, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_atom_dec_gen_ui: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Value *FnALD32 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_dec_32, Ptr->getType());
    return Builder.CreateCall(FnALD32, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_ldg_c:
  case NVPTX::BI__nvvm_ldg_c2:
  case NVPTX::BI__nvvm_ldg_c4:
  case NVPTX::BI__nvvm_ldg_s:
  case NVPTX::BI__nvvm_ldg_s2:
  case NVPTX::BI__nvvm_ldg_s4:
  case NVPTX::BI__nvvm_ldg_i:
  case NVPTX::BI__nvvm_ldg_i2:
  case NVPTX::BI__nvvm_ldg_i4:
  case NVPTX::BI__nvvm_ldg_l:
  case NVPTX::BI__nvvm_ldg_ll:
  case NVPTX::BI__nvvm_ldg_ll2:
  case NVPTX::BI__nvvm_ldg_uc:
  case NVPTX::BI__nvvm_ldg_uc2:
  case NVPTX::BI__nvvm_ldg_uc4:
  case NVPTX::BI__nvvm_ldg_us:
  case NVPTX::BI__nvvm_ldg_us2:
  case NVPTX::BI__nvvm_ldg_us4:
  case NVPTX::BI__nvvm_ldg_ui:
  case NVPTX::BI__nvvm_ldg_ui2:
  case NVPTX::BI__nvvm_ldg_ui4:
  case NVPTX::BI__nvvm_ldg_ul:
  case NVPTX::BI__nvvm_ldg_ull:
  case NVPTX::BI__nvvm_ldg_ull2:
    // PTX Interoperability section 2.2: "For a vector with an even number of
    // elements, its alignment is set to number of elements times the alignment
    // of its member: n*alignof(t)."
    return MakeLdg(Intrinsic::nvvm_ldg_global_i);
  case NVPTX::BI__nvvm_ldg_f:
  case NVPTX::BI__nvvm_ldg_f2:
  case NVPTX::BI__nvvm_ldg_f4:
  case NVPTX::BI__nvvm_ldg_d:
  case NVPTX::BI__nvvm_ldg_d2:
    return MakeLdg(Intrinsic::nvvm_ldg_global_f);

  case NVPTX::BI__nvvm_atom_cta_add_gen_i:
  case NVPTX::BI__nvvm_atom_cta_add_gen_l:
  case NVPTX::BI__nvvm_atom_cta_add_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_add_gen_i:
  case NVPTX::BI__nvvm_atom_sys_add_gen_l:
  case NVPTX::BI__nvvm_atom_sys_add_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_add_gen_f:
  case NVPTX::BI__nvvm_atom_cta_add_gen_d:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_f_cta);
  case NVPTX::BI__nvvm_atom_sys_add_gen_f:
  case NVPTX::BI__nvvm_atom_sys_add_gen_d:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_f_sys);
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_exch_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_exch_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_max_gen_i:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ui:
  case NVPTX::BI__nvvm_atom_cta_max_gen_l:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ul:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ll:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_max_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_max_gen_i:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ui:
  case NVPTX::BI__nvvm_atom_sys_max_gen_l:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ul:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ll:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_max_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_min_gen_i:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ui:
  case NVPTX::BI__nvvm_atom_cta_min_gen_l:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ul:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ll:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_min_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_min_gen_i:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ui:
  case NVPTX::BI__nvvm_atom_sys_min_gen_l:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ul:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ll:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_min_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_inc_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_inc_gen_i_cta);
  case NVPTX::BI__nvvm_atom_cta_dec_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_dec_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_inc_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_inc_gen_i_sys);
  case NVPTX::BI__nvvm_atom_sys_dec_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_dec_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_and_gen_i:
  case NVPTX::BI__nvvm_atom_cta_and_gen_l:
  case NVPTX::BI__nvvm_atom_cta_and_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_and_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_and_gen_i:
  case NVPTX::BI__nvvm_atom_sys_and_gen_l:
  case NVPTX::BI__nvvm_atom_sys_and_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_and_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_or_gen_i:
  case NVPTX::BI__nvvm_atom_cta_or_gen_l:
  case NVPTX::BI__nvvm_atom_cta_or_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_or_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_or_gen_i:
  case NVPTX::BI__nvvm_atom_sys_or_gen_l:
  case NVPTX::BI__nvvm_atom_sys_or_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_or_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_xor_gen_i:
  case NVPTX::BI__nvvm_atom_cta_xor_gen_l:
  case NVPTX::BI__nvvm_atom_cta_xor_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_xor_gen_i_cta);
  case NVPTX::BI__nvvm_atom_sys_xor_gen_i:
  case NVPTX::BI__nvvm_atom_sys_xor_gen_l:
  case NVPTX::BI__nvvm_atom_sys_xor_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_xor_gen_i_sys);
  case NVPTX::BI__nvvm_atom_cta_cas_gen_i:
  case NVPTX::BI__nvvm_atom_cta_cas_gen_l:
  case NVPTX::BI__nvvm_atom_cta_cas_gen_ll: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(
            Intrinsic::nvvm_atomic_cas_gen_i_cta,
            {Ptr->getType()->getPointerElementType(), Ptr->getType()}),
        {Ptr, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2))});
  }
  case NVPTX::BI__nvvm_atom_sys_cas_gen_i:
  case NVPTX::BI__nvvm_atom_sys_cas_gen_l:
  case NVPTX::BI__nvvm_atom_sys_cas_gen_ll: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(
            Intrinsic::nvvm_atomic_cas_gen_i_sys,
            {Ptr->getType()->getPointerElementType(), Ptr->getType()}),
        {Ptr, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2))});
  }
  case NVPTX::BI__nvvm_match_all_sync_i32p:
  case NVPTX::BI__nvvm_match_all_sync_i64p: {
    Value *Mask = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Address PredOutPtr = EmitPointerWithAlignment(E->getArg(2));
    Value *ResultPair = Builder.CreateCall(
        CGM.getIntrinsic(BuiltinID == NVPTX::BI__nvvm_match_all_sync_i32p
                             ? Intrinsic::nvvm_match_all_sync_i32p
                             : Intrinsic::nvvm_match_all_sync_i64p),
        {Mask, Val});
    Value *Pred = Builder.CreateZExt(Builder.CreateExtractValue(ResultPair, 1),
                                     PredOutPtr.getElementType());
    Builder.CreateStore(Pred, PredOutPtr);
    return Builder.CreateExtractValue(ResultPair, 0);
  }
  case NVPTX::BI__hmma_m16n16k16_ld_a:
  case NVPTX::BI__hmma_m16n16k16_ld_b:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
  case NVPTX::BI__hmma_m32n8k16_ld_a:
  case NVPTX::BI__hmma_m32n8k16_ld_b:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
  case NVPTX::BI__hmma_m8n32k16_ld_a:
  case NVPTX::BI__hmma_m8n32k16_ld_b:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f32: {
    Address Dst = EmitPointerWithAlignment(E->getArg(0));
    Value *Src = EmitScalarExpr(E->getArg(1));
    Value *Ldm = EmitScalarExpr(E->getArg(2));
    llvm::APSInt isColMajorArg;
    if (!E->getArg(3)->isIntegerConstantExpr(isColMajorArg, getContext()))
      return nullptr;
    bool isColMajor = isColMajorArg.getSExtValue();
    unsigned IID;
    unsigned NumResults;
    switch (BuiltinID) {
    case NVPTX::BI__hmma_m16n16k16_ld_a:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m16n16k16_ld_b:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m32n8k16_ld_a:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m32n8k16_ld_b:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m8n32k16_ld_a:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m8n32k16_ld_b:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_row_stride;
      NumResults = 8;
      break;
    case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m8n32k16_ld_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_row_stride;
      NumResults = 8;
      break;
    default:
      llvm_unreachable("Unexpected builtin ID.");
    }
    Value *Result =
        Builder.CreateCall(CGM.getIntrinsic(IID, Src->getType()), {Src, Ldm});

    // Save returned values.
    for (unsigned i = 0; i < NumResults; ++i) {
      Builder.CreateAlignedStore(
          Builder.CreateBitCast(Builder.CreateExtractValue(Result, i),
                                Dst.getElementType()),
          Builder.CreateGEP(Dst.getPointer(), llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
    }
    return Result;
  }

  case NVPTX::BI__hmma_m16n16k16_st_c_f16:
  case NVPTX::BI__hmma_m16n16k16_st_c_f32:
  case NVPTX::BI__hmma_m32n8k16_st_c_f16:
  case NVPTX::BI__hmma_m32n8k16_st_c_f32:
  case NVPTX::BI__hmma_m8n32k16_st_c_f16:
  case NVPTX::BI__hmma_m8n32k16_st_c_f32: {
    Value *Dst = EmitScalarExpr(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *Ldm = EmitScalarExpr(E->getArg(2));
    llvm::APSInt isColMajorArg;
    if (!E->getArg(3)->isIntegerConstantExpr(isColMajorArg, getContext()))
      return nullptr;
    bool isColMajor = isColMajorArg.getSExtValue();
    unsigned IID;
    unsigned NumResults = 8;
    // PTX Instructions (and LLVM intrinsics) are defined for slice _d_, yet
    // for some reason nvcc builtins use _c_.
    switch (BuiltinID) {
    case NVPTX::BI__hmma_m16n16k16_st_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m16n16k16_st_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_col_stride
                       : Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_row_stride;
      break;
    case NVPTX::BI__hmma_m32n8k16_st_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m32n8k16_st_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_col_stride
                       : Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_row_stride;
      break;
    case NVPTX::BI__hmma_m8n32k16_st_c_f16:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_row_stride;
      NumResults = 4;
      break;
    case NVPTX::BI__hmma_m8n32k16_st_c_f32:
      IID = isColMajor ? Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_col_stride
                       : Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_row_stride;
      break;
    default:
      llvm_unreachable("Unexpected builtin ID.");
    }
    Function *Intrinsic = CGM.getIntrinsic(IID, Dst->getType());
    llvm::Type *ParamType = Intrinsic->getFunctionType()->getParamType(1);
    SmallVector<Value *, 10> Values = {Dst};
    for (unsigned i = 0; i < NumResults; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          Builder.CreateGEP(Src.getPointer(), llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, ParamType));
    }
    Values.push_back(Ldm);
    Value *Result = Builder.CreateCall(Intrinsic, Values);
    return Result;
  }

  // BI__hmma_m16n16k16_mma_<Dtype><CType>(d, a, b, c, layout, satf) -->
  // Intrinsic::nvvm_wmma_m16n16k16_mma_sync<layout A,B><DType><CType><Satf>
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32: {
    Address Dst = EmitPointerWithAlignment(E->getArg(0));
    Address SrcA = EmitPointerWithAlignment(E->getArg(1));
    Address SrcB = EmitPointerWithAlignment(E->getArg(2));
    Address SrcC = EmitPointerWithAlignment(E->getArg(3));
    llvm::APSInt LayoutArg;
    if (!E->getArg(4)->isIntegerConstantExpr(LayoutArg, getContext()))
      return nullptr;
    int Layout = LayoutArg.getSExtValue();
    if (Layout < 0 || Layout > 3)
      return nullptr;
    llvm::APSInt SatfArg;
    if (!E->getArg(5)->isIntegerConstantExpr(SatfArg, getContext()))
      return nullptr;
    bool Satf = SatfArg.getSExtValue();

    // clang-format off
#define MMA_VARIANTS(geom, type) {{                                 \
      Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type##_satfinite  \
    }}
    // clang-format on

    auto getMMAIntrinsic = [Layout, Satf](std::array<unsigned, 8> Variants) {
      unsigned Index = Layout * 2 + Satf;
      assert(Index < 8);
      return Variants[Index];
    };
    unsigned IID;
    unsigned NumEltsC;
    unsigned NumEltsD;
    switch (BuiltinID) {
    case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m16n16k16, f16_f16));
      NumEltsC = 4;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m16n16k16, f32_f16));
      NumEltsC = 4;
      NumEltsD = 8;
      break;
    case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m16n16k16, f16_f32));
      NumEltsC = 8;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m16n16k16, f32_f32));
      NumEltsC = 8;
      NumEltsD = 8;
      break;
    case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m32n8k16, f16_f16));
      NumEltsC = 4;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m32n8k16, f32_f16));
      NumEltsC = 4;
      NumEltsD = 8;
      break;
    case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m32n8k16, f16_f32));
      NumEltsC = 8;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m32n8k16, f32_f32));
      NumEltsC = 8;
      NumEltsD = 8;
      break;
    case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m8n32k16, f16_f16));
      NumEltsC = 4;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
      IID = getMMAIntrinsic(MMA_VARIANTS(m8n32k16, f32_f16));
      NumEltsC = 4;
      NumEltsD = 8;
      break;
    case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m8n32k16, f16_f32));
      NumEltsC = 8;
      NumEltsD = 4;
      break;
    case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
      IID = getMMAIntrinsic(MMA_VARIANTS(m8n32k16, f32_f32));
      NumEltsC = 8;
      NumEltsD = 8;
      break;
    default:
      llvm_unreachable("Unexpected builtin ID.");
    }
#undef MMA_VARIANTS

    SmallVector<Value *, 24> Values;
    Function *Intrinsic = CGM.getIntrinsic(IID);
    llvm::Type *ABType = Intrinsic->getFunctionType()->getParamType(0);
    // Load A
    for (unsigned i = 0; i < 8; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          Builder.CreateGEP(SrcA.getPointer(),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, ABType));
    }
    // Load B
    for (unsigned i = 0; i < 8; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          Builder.CreateGEP(SrcB.getPointer(),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, ABType));
    }
    // Load C
    llvm::Type *CType = Intrinsic->getFunctionType()->getParamType(16);
    for (unsigned i = 0; i < NumEltsC; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          Builder.CreateGEP(SrcC.getPointer(),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, CType));
    }
    Value *Result = Builder.CreateCall(Intrinsic, Values);
    llvm::Type *DType = Dst.getElementType();
    for (unsigned i = 0; i < NumEltsD; ++i)
      Builder.CreateAlignedStore(
          Builder.CreateBitCast(Builder.CreateExtractValue(Result, i), DType),
          Builder.CreateGEP(Dst.getPointer(), llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
    return Result;
  }
  default:
    return nullptr;
  }
}

Value *CodeGenFunction::EmitWebAssemblyBuiltinExpr(unsigned BuiltinID,
                                                   const CallExpr *E) {
  switch (BuiltinID) {
  case WebAssembly::BI__builtin_wasm_memory_size: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *I = EmitScalarExpr(E->getArg(0));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_memory_size, ResultType);
    return Builder.CreateCall(Callee, I);
  }
  case WebAssembly::BI__builtin_wasm_memory_grow: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Args[] = {
      EmitScalarExpr(E->getArg(0)),
      EmitScalarExpr(E->getArg(1))
    };
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_memory_grow, ResultType);
    return Builder.CreateCall(Callee, Args);
  }
  case WebAssembly::BI__builtin_wasm_throw: {
    Value *Tag = EmitScalarExpr(E->getArg(0));
    Value *Obj = EmitScalarExpr(E->getArg(1));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_throw);
    return Builder.CreateCall(Callee, {Tag, Obj});
  }
  case WebAssembly::BI__builtin_wasm_rethrow: {
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_rethrow);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_atomic_wait_i32: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Expected = EmitScalarExpr(E->getArg(1));
    Value *Timeout = EmitScalarExpr(E->getArg(2));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_atomic_wait_i32);
    return Builder.CreateCall(Callee, {Addr, Expected, Timeout});
  }
  case WebAssembly::BI__builtin_wasm_atomic_wait_i64: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Expected = EmitScalarExpr(E->getArg(1));
    Value *Timeout = EmitScalarExpr(E->getArg(2));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_atomic_wait_i64);
    return Builder.CreateCall(Callee, {Addr, Expected, Timeout});
  }
  case WebAssembly::BI__builtin_wasm_atomic_notify: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Count = EmitScalarExpr(E->getArg(1));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_atomic_notify);
    return Builder.CreateCall(Callee, {Addr, Count});
  }
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i64_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32x4_f32x4:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i64x2_f64x2: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_trunc_saturate_signed,
                                     {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i64_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32x4_f32x4:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i64x2_f64x2: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_trunc_saturate_unsigned,
                                     {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_min_f32:
  case WebAssembly::BI__builtin_wasm_min_f64:
  case WebAssembly::BI__builtin_wasm_min_f32x4:
  case WebAssembly::BI__builtin_wasm_min_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *Callee = CGM.getIntrinsic(Intrinsic::minimum,
                                     ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_max_f32:
  case WebAssembly::BI__builtin_wasm_max_f64:
  case WebAssembly::BI__builtin_wasm_max_f32x4:
  case WebAssembly::BI__builtin_wasm_max_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *Callee = CGM.getIntrinsic(Intrinsic::maximum,
                                     ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_extract_lane_s_i8x16:
  case WebAssembly::BI__builtin_wasm_extract_lane_u_i8x16:
  case WebAssembly::BI__builtin_wasm_extract_lane_s_i16x8:
  case WebAssembly::BI__builtin_wasm_extract_lane_u_i16x8:
  case WebAssembly::BI__builtin_wasm_extract_lane_i32x4:
  case WebAssembly::BI__builtin_wasm_extract_lane_i64x2:
  case WebAssembly::BI__builtin_wasm_extract_lane_f32x4:
  case WebAssembly::BI__builtin_wasm_extract_lane_f64x2: {
    llvm::APSInt LaneConst;
    if (!E->getArg(1)->isIntegerConstantExpr(LaneConst, getContext()))
      llvm_unreachable("Constant arg isn't actually constant?");
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Lane = llvm::ConstantInt::get(getLLVMContext(), LaneConst);
    Value *Extract = Builder.CreateExtractElement(Vec, Lane);
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_extract_lane_s_i8x16:
    case WebAssembly::BI__builtin_wasm_extract_lane_s_i16x8:
      return Builder.CreateSExt(Extract, ConvertType(E->getType()));
    case WebAssembly::BI__builtin_wasm_extract_lane_u_i8x16:
    case WebAssembly::BI__builtin_wasm_extract_lane_u_i16x8:
      return Builder.CreateZExt(Extract, ConvertType(E->getType()));
    case WebAssembly::BI__builtin_wasm_extract_lane_i32x4:
    case WebAssembly::BI__builtin_wasm_extract_lane_i64x2:
    case WebAssembly::BI__builtin_wasm_extract_lane_f32x4:
    case WebAssembly::BI__builtin_wasm_extract_lane_f64x2:
      return Extract;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
  }
  case WebAssembly::BI__builtin_wasm_replace_lane_i8x16:
  case WebAssembly::BI__builtin_wasm_replace_lane_i16x8:
  case WebAssembly::BI__builtin_wasm_replace_lane_i32x4:
  case WebAssembly::BI__builtin_wasm_replace_lane_i64x2:
  case WebAssembly::BI__builtin_wasm_replace_lane_f32x4:
  case WebAssembly::BI__builtin_wasm_replace_lane_f64x2: {
    llvm::APSInt LaneConst;
    if (!E->getArg(1)->isIntegerConstantExpr(LaneConst, getContext()))
      llvm_unreachable("Constant arg isn't actually constant?");
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Lane = llvm::ConstantInt::get(getLLVMContext(), LaneConst);
    Value *Val = EmitScalarExpr(E->getArg(2));
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_replace_lane_i8x16:
    case WebAssembly::BI__builtin_wasm_replace_lane_i16x8: {
      llvm::Type *ElemType = ConvertType(E->getType())->getVectorElementType();
      Value *Trunc = Builder.CreateTrunc(Val, ElemType);
      return Builder.CreateInsertElement(Vec, Trunc, Lane);
    }
    case WebAssembly::BI__builtin_wasm_replace_lane_i32x4:
    case WebAssembly::BI__builtin_wasm_replace_lane_i64x2:
    case WebAssembly::BI__builtin_wasm_replace_lane_f32x4:
    case WebAssembly::BI__builtin_wasm_replace_lane_f64x2:
      return Builder.CreateInsertElement(Vec, Val, Lane);
    default:
      llvm_unreachable("unexpected builtin ID");
    }
  }
  case WebAssembly::BI__builtin_wasm_add_saturate_s_i8x16:
  case WebAssembly::BI__builtin_wasm_add_saturate_u_i8x16:
  case WebAssembly::BI__builtin_wasm_add_saturate_s_i16x8:
  case WebAssembly::BI__builtin_wasm_add_saturate_u_i16x8:
  case WebAssembly::BI__builtin_wasm_sub_saturate_s_i8x16:
  case WebAssembly::BI__builtin_wasm_sub_saturate_u_i8x16:
  case WebAssembly::BI__builtin_wasm_sub_saturate_s_i16x8:
  case WebAssembly::BI__builtin_wasm_sub_saturate_u_i16x8: {
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_add_saturate_s_i8x16:
    case WebAssembly::BI__builtin_wasm_add_saturate_s_i16x8:
      IntNo = Intrinsic::sadd_sat;
      break;
    case WebAssembly::BI__builtin_wasm_add_saturate_u_i8x16:
    case WebAssembly::BI__builtin_wasm_add_saturate_u_i16x8:
      IntNo = Intrinsic::uadd_sat;
      break;
    case WebAssembly::BI__builtin_wasm_sub_saturate_s_i8x16:
    case WebAssembly::BI__builtin_wasm_sub_saturate_s_i16x8:
      IntNo = Intrinsic::wasm_sub_saturate_signed;
      break;
    case WebAssembly::BI__builtin_wasm_sub_saturate_u_i8x16:
    case WebAssembly::BI__builtin_wasm_sub_saturate_u_i16x8:
      IntNo = Intrinsic::wasm_sub_saturate_unsigned;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *Callee = CGM.getIntrinsic(IntNo, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_bitselect: {
    Value *V1 = EmitScalarExpr(E->getArg(0));
    Value *V2 = EmitScalarExpr(E->getArg(1));
    Value *C = EmitScalarExpr(E->getArg(2));
    Value *Callee = CGM.getIntrinsic(Intrinsic::wasm_bitselect,
                                     ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {V1, V2, C});
  }
  case WebAssembly::BI__builtin_wasm_any_true_i8x16:
  case WebAssembly::BI__builtin_wasm_any_true_i16x8:
  case WebAssembly::BI__builtin_wasm_any_true_i32x4:
  case WebAssembly::BI__builtin_wasm_any_true_i64x2:
  case WebAssembly::BI__builtin_wasm_all_true_i8x16:
  case WebAssembly::BI__builtin_wasm_all_true_i16x8:
  case WebAssembly::BI__builtin_wasm_all_true_i32x4:
  case WebAssembly::BI__builtin_wasm_all_true_i64x2: {
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_any_true_i8x16:
    case WebAssembly::BI__builtin_wasm_any_true_i16x8:
    case WebAssembly::BI__builtin_wasm_any_true_i32x4:
    case WebAssembly::BI__builtin_wasm_any_true_i64x2:
      IntNo = Intrinsic::wasm_anytrue;
      break;
    case WebAssembly::BI__builtin_wasm_all_true_i8x16:
    case WebAssembly::BI__builtin_wasm_all_true_i16x8:
    case WebAssembly::BI__builtin_wasm_all_true_i32x4:
    case WebAssembly::BI__builtin_wasm_all_true_i64x2:
      IntNo = Intrinsic::wasm_alltrue;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Callee = CGM.getIntrinsic(IntNo, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_abs_f32x4:
  case WebAssembly::BI__builtin_wasm_abs_f64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Callee = CGM.getIntrinsic(Intrinsic::fabs, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_sqrt_f32x4:
  case WebAssembly::BI__builtin_wasm_sqrt_f64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Callee = CGM.getIntrinsic(Intrinsic::sqrt, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }

  default:
    return nullptr;
  }
}

Value *CodeGenFunction::EmitHexagonBuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E) {
  SmallVector<llvm::Value *, 4> Ops;
  Intrinsic::ID ID = Intrinsic::not_intrinsic;

  auto MakeCircLd = [&](unsigned IntID, bool HasImm) {
    // The base pointer is passed by address, so it needs to be loaded.
    Address BP = EmitPointerWithAlignment(E->getArg(0));
    BP = Address(Builder.CreateBitCast(BP.getPointer(), Int8PtrPtrTy),
                 BP.getAlignment());
    llvm::Value *Base = Builder.CreateLoad(BP);
    // Operands are Base, Increment, Modifier, Start.
    if (HasImm)
      Ops = { Base, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2)),
              EmitScalarExpr(E->getArg(3)) };
    else
      Ops = { Base, EmitScalarExpr(E->getArg(1)),
              EmitScalarExpr(E->getArg(2)) };

    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(IntID), Ops);
    llvm::Value *NewBase = Builder.CreateExtractValue(Result, 1);
    llvm::Value *LV = Builder.CreateBitCast(EmitScalarExpr(E->getArg(0)),
                                            NewBase->getType()->getPointerTo());
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    // The intrinsic generates two results. The new value for the base pointer
    // needs to be stored.
    Builder.CreateAlignedStore(NewBase, LV, Dest.getAlignment());
    return Builder.CreateExtractValue(Result, 0);
  };

  auto MakeCircSt = [&](unsigned IntID, bool HasImm) {
    // The base pointer is passed by address, so it needs to be loaded.
    Address BP = EmitPointerWithAlignment(E->getArg(0));
    BP = Address(Builder.CreateBitCast(BP.getPointer(), Int8PtrPtrTy),
                 BP.getAlignment());
    llvm::Value *Base = Builder.CreateLoad(BP);
    // Operands are Base, Increment, Modifier, Value, Start.
    if (HasImm)
      Ops = { Base, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2)),
              EmitScalarExpr(E->getArg(3)), EmitScalarExpr(E->getArg(4)) };
    else
      Ops = { Base, EmitScalarExpr(E->getArg(1)),
              EmitScalarExpr(E->getArg(2)), EmitScalarExpr(E->getArg(3)) };

    llvm::Value *NewBase = Builder.CreateCall(CGM.getIntrinsic(IntID), Ops);
    llvm::Value *LV = Builder.CreateBitCast(EmitScalarExpr(E->getArg(0)),
                                            NewBase->getType()->getPointerTo());
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    // The intrinsic generates one result, which is the new value for the base
    // pointer. It needs to be stored.
    return Builder.CreateAlignedStore(NewBase, LV, Dest.getAlignment());
  };

  // Handle the conversion of bit-reverse load intrinsics to bit code.
  // The intrinsic call after this function only reads from memory and the
  // write to memory is dealt by the store instruction.
  auto MakeBrevLd = [&](unsigned IntID, llvm::Type *DestTy) {
    // The intrinsic generates one result, which is the new value for the base
    // pointer. It needs to be returned. The result of the load instruction is
    // passed to intrinsic by address, so the value needs to be stored.
    llvm::Value *BaseAddress =
        Builder.CreateBitCast(EmitScalarExpr(E->getArg(0)), Int8PtrTy);

    // Expressions like &(*pt++) will be incremented per evaluation.
    // EmitPointerWithAlignment and EmitScalarExpr evaluates the expression
    // per call.
    Address DestAddr = EmitPointerWithAlignment(E->getArg(1));
    DestAddr = Address(Builder.CreateBitCast(DestAddr.getPointer(), Int8PtrTy),
                       DestAddr.getAlignment());
    llvm::Value *DestAddress = DestAddr.getPointer();

    // Operands are Base, Dest, Modifier.
    // The intrinsic format in LLVM IR is defined as
    // { ValueType, i8* } (i8*, i32).
    Ops = {BaseAddress, EmitScalarExpr(E->getArg(2))};

    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(IntID), Ops);
    // The value needs to be stored as the variable is passed by reference.
    llvm::Value *DestVal = Builder.CreateExtractValue(Result, 0);

    // The store needs to be truncated to fit the destination type.
    // While i32 and i64 are natively supported on Hexagon, i8 and i16 needs
    // to be handled with stores of respective destination type.
    DestVal = Builder.CreateTrunc(DestVal, DestTy);

    llvm::Value *DestForStore =
        Builder.CreateBitCast(DestAddress, DestVal->getType()->getPointerTo());
    Builder.CreateAlignedStore(DestVal, DestForStore, DestAddr.getAlignment());
    // The updated value of the base pointer is returned.
    return Builder.CreateExtractValue(Result, 1);
  };

  switch (BuiltinID) {
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarry:
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarry_128B: {
    Address Dest = EmitPointerWithAlignment(E->getArg(2));
    unsigned Size;
    if (BuiltinID == Hexagon::BI__builtin_HEXAGON_V6_vaddcarry) {
      Size = 512;
      ID = Intrinsic::hexagon_V6_vaddcarry;
    } else {
      Size = 1024;
      ID = Intrinsic::hexagon_V6_vaddcarry_128B;
    }
    Dest = Builder.CreateBitCast(Dest,
        llvm::VectorType::get(Builder.getInt1Ty(), Size)->getPointerTo(0));
    LoadInst *QLd = Builder.CreateLoad(Dest);
    Ops = { EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1)), QLd };
    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    llvm::Value *Vprd = Builder.CreateExtractValue(Result, 1);
    llvm::Value *Base = Builder.CreateBitCast(EmitScalarExpr(E->getArg(2)),
                                              Vprd->getType()->getPointerTo(0));
    Builder.CreateAlignedStore(Vprd, Base, Dest.getAlignment());
    return Builder.CreateExtractValue(Result, 0);
  }
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarry:
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarry_128B: {
    Address Dest = EmitPointerWithAlignment(E->getArg(2));
    unsigned Size;
    if (BuiltinID == Hexagon::BI__builtin_HEXAGON_V6_vsubcarry) {
      Size = 512;
      ID = Intrinsic::hexagon_V6_vsubcarry;
    } else {
      Size = 1024;
      ID = Intrinsic::hexagon_V6_vsubcarry_128B;
    }
    Dest = Builder.CreateBitCast(Dest,
        llvm::VectorType::get(Builder.getInt1Ty(), Size)->getPointerTo(0));
    LoadInst *QLd = Builder.CreateLoad(Dest);
    Ops = { EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1)), QLd };
    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    llvm::Value *Vprd = Builder.CreateExtractValue(Result, 1);
    llvm::Value *Base = Builder.CreateBitCast(EmitScalarExpr(E->getArg(2)),
                                              Vprd->getType()->getPointerTo(0));
    Builder.CreateAlignedStore(Vprd, Base, Dest.getAlignment());
    return Builder.CreateExtractValue(Result, 0);
  }
  case Hexagon::BI__builtin_HEXAGON_L2_loadrub_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrub_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrb_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrb_pci,  /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadruh_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadruh_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrh_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrh_pci,  /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadri_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadri_pci,  /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrd_pci:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrd_pci,  /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrub_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrub_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrb_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrb_pcr,  /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_L2_loadruh_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadruh_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrh_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrh_pcr,  /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_L2_loadri_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadri_pcr,  /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_L2_loadrd_pcr:
    return MakeCircLd(Intrinsic::hexagon_L2_loadrd_pcr,  /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_S2_storerb_pci:
    return MakeCircSt(Intrinsic::hexagon_S2_storerb_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storerh_pci:
    return MakeCircSt(Intrinsic::hexagon_S2_storerh_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storerf_pci:
    return MakeCircSt(Intrinsic::hexagon_S2_storerf_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storeri_pci:
    return MakeCircSt(Intrinsic::hexagon_S2_storeri_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storerd_pci:
    return MakeCircSt(Intrinsic::hexagon_S2_storerd_pci, /*HasImm*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storerb_pcr:
    return MakeCircSt(Intrinsic::hexagon_S2_storerb_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_S2_storerh_pcr:
    return MakeCircSt(Intrinsic::hexagon_S2_storerh_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_S2_storerf_pcr:
    return MakeCircSt(Intrinsic::hexagon_S2_storerf_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_S2_storeri_pcr:
    return MakeCircSt(Intrinsic::hexagon_S2_storeri_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_HEXAGON_S2_storerd_pcr:
    return MakeCircSt(Intrinsic::hexagon_S2_storerd_pcr, /*HasImm*/false);
  case Hexagon::BI__builtin_brev_ldub:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrub_pbr, Int8Ty);
  case Hexagon::BI__builtin_brev_ldb:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrb_pbr, Int8Ty);
  case Hexagon::BI__builtin_brev_lduh:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadruh_pbr, Int16Ty);
  case Hexagon::BI__builtin_brev_ldh:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrh_pbr, Int16Ty);
  case Hexagon::BI__builtin_brev_ldw:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadri_pbr, Int32Ty);
  case Hexagon::BI__builtin_brev_ldd:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrd_pbr, Int64Ty);
  default:
    break;
  } // switch

  return nullptr;
}
