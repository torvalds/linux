//===- SafeStack.cpp - Safe Stack Insertion -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass splits the stack into the safe stack (kept as-is for LLVM backend)
// and the unsafe stack (explicitly allocated and managed through the runtime
// support library).
//
// http://clang.llvm.org/docs/SafeStack.html
//
//===----------------------------------------------------------------------===//

#include "SafeStackColoring.h"
#include "SafeStackLayout.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

using namespace llvm;
using namespace llvm::safestack;

#define DEBUG_TYPE "safe-stack"

namespace llvm {

STATISTIC(NumFunctions, "Total number of functions");
STATISTIC(NumUnsafeStackFunctions, "Number of functions with unsafe stack");
STATISTIC(NumUnsafeStackRestorePointsFunctions,
          "Number of functions that use setjmp or exceptions");

STATISTIC(NumAllocas, "Total number of allocas");
STATISTIC(NumUnsafeStaticAllocas, "Number of unsafe static allocas");
STATISTIC(NumUnsafeDynamicAllocas, "Number of unsafe dynamic allocas");
STATISTIC(NumUnsafeByValArguments, "Number of unsafe byval arguments");
STATISTIC(NumUnsafeStackRestorePoints, "Number of setjmps and landingpads");

} // namespace llvm

/// Use __safestack_pointer_address even if the platform has a faster way of
/// access safe stack pointer.
static cl::opt<bool>
    SafeStackUsePointerAddress("safestack-use-pointer-address",
                                  cl::init(false), cl::Hidden);


namespace {

/// Rewrite an SCEV expression for a memory access address to an expression that
/// represents offset from the given alloca.
///
/// The implementation simply replaces all mentions of the alloca with zero.
class AllocaOffsetRewriter : public SCEVRewriteVisitor<AllocaOffsetRewriter> {
  const Value *AllocaPtr;

public:
  AllocaOffsetRewriter(ScalarEvolution &SE, const Value *AllocaPtr)
      : SCEVRewriteVisitor(SE), AllocaPtr(AllocaPtr) {}

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (Expr->getValue() == AllocaPtr)
      return SE.getZero(Expr->getType());
    return Expr;
  }
};

/// The SafeStack pass splits the stack of each function into the safe
/// stack, which is only accessed through memory safe dereferences (as
/// determined statically), and the unsafe stack, which contains all
/// local variables that are accessed in ways that we can't prove to
/// be safe.
class SafeStack {
  Function &F;
  const TargetLoweringBase &TL;
  const DataLayout &DL;
  ScalarEvolution &SE;

  Type *StackPtrTy;
  Type *IntPtrTy;
  Type *Int32Ty;
  Type *Int8Ty;

  Value *UnsafeStackPtr = nullptr;

  /// Unsafe stack alignment. Each stack frame must ensure that the stack is
  /// aligned to this value. We need to re-align the unsafe stack if the
  /// alignment of any object on the stack exceeds this value.
  ///
  /// 16 seems like a reasonable upper bound on the alignment of objects that we
  /// might expect to appear on the stack on most common targets.
  enum { StackAlignment = 16 };

  /// Return the value of the stack canary.
  Value *getStackGuard(IRBuilder<> &IRB, Function &F);

  /// Load stack guard from the frame and check if it has changed.
  void checkStackGuard(IRBuilder<> &IRB, Function &F, ReturnInst &RI,
                       AllocaInst *StackGuardSlot, Value *StackGuard);

  /// Find all static allocas, dynamic allocas, return instructions and
  /// stack restore points (exception unwind blocks and setjmp calls) in the
  /// given function and append them to the respective vectors.
  void findInsts(Function &F, SmallVectorImpl<AllocaInst *> &StaticAllocas,
                 SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                 SmallVectorImpl<Argument *> &ByValArguments,
                 SmallVectorImpl<ReturnInst *> &Returns,
                 SmallVectorImpl<Instruction *> &StackRestorePoints);

  /// Calculate the allocation size of a given alloca. Returns 0 if the
  /// size can not be statically determined.
  uint64_t getStaticAllocaAllocationSize(const AllocaInst* AI);

  /// Allocate space for all static allocas in \p StaticAllocas,
  /// replace allocas with pointers into the unsafe stack and generate code to
  /// restore the stack pointer before all return instructions in \p Returns.
  ///
  /// \returns A pointer to the top of the unsafe stack after all unsafe static
  /// allocas are allocated.
  Value *moveStaticAllocasToUnsafeStack(IRBuilder<> &IRB, Function &F,
                                        ArrayRef<AllocaInst *> StaticAllocas,
                                        ArrayRef<Argument *> ByValArguments,
                                        ArrayRef<ReturnInst *> Returns,
                                        Instruction *BasePointer,
                                        AllocaInst *StackGuardSlot);

  /// Generate code to restore the stack after all stack restore points
  /// in \p StackRestorePoints.
  ///
  /// \returns A local variable in which to maintain the dynamic top of the
  /// unsafe stack if needed.
  AllocaInst *
  createStackRestorePoints(IRBuilder<> &IRB, Function &F,
                           ArrayRef<Instruction *> StackRestorePoints,
                           Value *StaticTop, bool NeedDynamicTop);

  /// Replace all allocas in \p DynamicAllocas with code to allocate
  /// space dynamically on the unsafe stack and store the dynamic unsafe stack
  /// top to \p DynamicTop if non-null.
  void moveDynamicAllocasToUnsafeStack(Function &F, Value *UnsafeStackPtr,
                                       AllocaInst *DynamicTop,
                                       ArrayRef<AllocaInst *> DynamicAllocas);

  bool IsSafeStackAlloca(const Value *AllocaPtr, uint64_t AllocaSize);

  bool IsMemIntrinsicSafe(const MemIntrinsic *MI, const Use &U,
                          const Value *AllocaPtr, uint64_t AllocaSize);
  bool IsAccessSafe(Value *Addr, uint64_t Size, const Value *AllocaPtr,
                    uint64_t AllocaSize);

  bool ShouldInlinePointerAddress(CallSite &CS);
  void TryInlinePointerAddress();

public:
  SafeStack(Function &F, const TargetLoweringBase &TL, const DataLayout &DL,
            ScalarEvolution &SE)
      : F(F), TL(TL), DL(DL), SE(SE),
        StackPtrTy(Type::getInt8PtrTy(F.getContext())),
        IntPtrTy(DL.getIntPtrType(F.getContext())),
        Int32Ty(Type::getInt32Ty(F.getContext())),
        Int8Ty(Type::getInt8Ty(F.getContext())) {}

  // Run the transformation on the associated function.
  // Returns whether the function was changed.
  bool run();
};

uint64_t SafeStack::getStaticAllocaAllocationSize(const AllocaInst* AI) {
  uint64_t Size = DL.getTypeAllocSize(AI->getAllocatedType());
  if (AI->isArrayAllocation()) {
    auto C = dyn_cast<ConstantInt>(AI->getArraySize());
    if (!C)
      return 0;
    Size *= C->getZExtValue();
  }
  return Size;
}

bool SafeStack::IsAccessSafe(Value *Addr, uint64_t AccessSize,
                             const Value *AllocaPtr, uint64_t AllocaSize) {
  AllocaOffsetRewriter Rewriter(SE, AllocaPtr);
  const SCEV *Expr = Rewriter.visit(SE.getSCEV(Addr));

  uint64_t BitWidth = SE.getTypeSizeInBits(Expr->getType());
  ConstantRange AccessStartRange = SE.getUnsignedRange(Expr);
  ConstantRange SizeRange =
      ConstantRange(APInt(BitWidth, 0), APInt(BitWidth, AccessSize));
  ConstantRange AccessRange = AccessStartRange.add(SizeRange);
  ConstantRange AllocaRange =
      ConstantRange(APInt(BitWidth, 0), APInt(BitWidth, AllocaSize));
  bool Safe = AllocaRange.contains(AccessRange);

  LLVM_DEBUG(
      dbgs() << "[SafeStack] "
             << (isa<AllocaInst>(AllocaPtr) ? "Alloca " : "ByValArgument ")
             << *AllocaPtr << "\n"
             << "            Access " << *Addr << "\n"
             << "            SCEV " << *Expr
             << " U: " << SE.getUnsignedRange(Expr)
             << ", S: " << SE.getSignedRange(Expr) << "\n"
             << "            Range " << AccessRange << "\n"
             << "            AllocaRange " << AllocaRange << "\n"
             << "            " << (Safe ? "safe" : "unsafe") << "\n");

  return Safe;
}

bool SafeStack::IsMemIntrinsicSafe(const MemIntrinsic *MI, const Use &U,
                                   const Value *AllocaPtr,
                                   uint64_t AllocaSize) {
  if (auto MTI = dyn_cast<MemTransferInst>(MI)) {
    if (MTI->getRawSource() != U && MTI->getRawDest() != U)
      return true;
  } else {
    if (MI->getRawDest() != U)
      return true;
  }

  const auto *Len = dyn_cast<ConstantInt>(MI->getLength());
  // Non-constant size => unsafe. FIXME: try SCEV getRange.
  if (!Len) return false;
  return IsAccessSafe(U, Len->getZExtValue(), AllocaPtr, AllocaSize);
}

/// Check whether a given allocation must be put on the safe
/// stack or not. The function analyzes all uses of AI and checks whether it is
/// only accessed in a memory safe way (as decided statically).
bool SafeStack::IsSafeStackAlloca(const Value *AllocaPtr, uint64_t AllocaSize) {
  // Go through all uses of this alloca and check whether all accesses to the
  // allocated object are statically known to be memory safe and, hence, the
  // object can be placed on the safe stack.
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 8> WorkList;
  WorkList.push_back(AllocaPtr);

  // A DFS search through all uses of the alloca in bitcasts/PHI/GEPs/etc.
  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();
    for (const Use &UI : V->uses()) {
      auto I = cast<const Instruction>(UI.getUser());
      assert(V == UI.get());

      switch (I->getOpcode()) {
      case Instruction::Load:
        if (!IsAccessSafe(UI, DL.getTypeStoreSize(I->getType()), AllocaPtr,
                          AllocaSize))
          return false;
        break;

      case Instruction::VAArg:
        // "va-arg" from a pointer is safe.
        break;
      case Instruction::Store:
        if (V == I->getOperand(0)) {
          // Stored the pointer - conservatively assume it may be unsafe.
          LLVM_DEBUG(dbgs()
                     << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                     << "\n            store of address: " << *I << "\n");
          return false;
        }

        if (!IsAccessSafe(UI, DL.getTypeStoreSize(I->getOperand(0)->getType()),
                          AllocaPtr, AllocaSize))
          return false;
        break;

      case Instruction::Ret:
        // Information leak.
        return false;

      case Instruction::Call:
      case Instruction::Invoke: {
        ImmutableCallSite CS(I);

        if (I->isLifetimeStartOrEnd())
          continue;

        if (const MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I)) {
          if (!IsMemIntrinsicSafe(MI, UI, AllocaPtr, AllocaSize)) {
            LLVM_DEBUG(dbgs()
                       << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                       << "\n            unsafe memintrinsic: " << *I << "\n");
            return false;
          }
          continue;
        }

        // LLVM 'nocapture' attribute is only set for arguments whose address
        // is not stored, passed around, or used in any other non-trivial way.
        // We assume that passing a pointer to an object as a 'nocapture
        // readnone' argument is safe.
        // FIXME: a more precise solution would require an interprocedural
        // analysis here, which would look at all uses of an argument inside
        // the function being called.
        ImmutableCallSite::arg_iterator B = CS.arg_begin(), E = CS.arg_end();
        for (ImmutableCallSite::arg_iterator A = B; A != E; ++A)
          if (A->get() == V)
            if (!(CS.doesNotCapture(A - B) && (CS.doesNotAccessMemory(A - B) ||
                                               CS.doesNotAccessMemory()))) {
              LLVM_DEBUG(dbgs() << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                                << "\n            unsafe call: " << *I << "\n");
              return false;
            }
        continue;
      }

      default:
        if (Visited.insert(I).second)
          WorkList.push_back(cast<const Instruction>(I));
      }
    }
  }

  // All uses of the alloca are safe, we can place it on the safe stack.
  return true;
}

Value *SafeStack::getStackGuard(IRBuilder<> &IRB, Function &F) {
  Value *StackGuardVar = TL.getIRStackGuard(IRB);
  if (!StackGuardVar)
    StackGuardVar =
        F.getParent()->getOrInsertGlobal("__stack_chk_guard", StackPtrTy);
  return IRB.CreateLoad(StackGuardVar, "StackGuard");
}

void SafeStack::findInsts(Function &F,
                          SmallVectorImpl<AllocaInst *> &StaticAllocas,
                          SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                          SmallVectorImpl<Argument *> &ByValArguments,
                          SmallVectorImpl<ReturnInst *> &Returns,
                          SmallVectorImpl<Instruction *> &StackRestorePoints) {
  for (Instruction &I : instructions(&F)) {
    if (auto AI = dyn_cast<AllocaInst>(&I)) {
      ++NumAllocas;

      uint64_t Size = getStaticAllocaAllocationSize(AI);
      if (IsSafeStackAlloca(AI, Size))
        continue;

      if (AI->isStaticAlloca()) {
        ++NumUnsafeStaticAllocas;
        StaticAllocas.push_back(AI);
      } else {
        ++NumUnsafeDynamicAllocas;
        DynamicAllocas.push_back(AI);
      }
    } else if (auto RI = dyn_cast<ReturnInst>(&I)) {
      Returns.push_back(RI);
    } else if (auto CI = dyn_cast<CallInst>(&I)) {
      // setjmps require stack restore.
      if (CI->getCalledFunction() && CI->canReturnTwice())
        StackRestorePoints.push_back(CI);
    } else if (auto LP = dyn_cast<LandingPadInst>(&I)) {
      // Exception landing pads require stack restore.
      StackRestorePoints.push_back(LP);
    } else if (auto II = dyn_cast<IntrinsicInst>(&I)) {
      if (II->getIntrinsicID() == Intrinsic::gcroot)
        report_fatal_error(
            "gcroot intrinsic not compatible with safestack attribute");
    }
  }
  for (Argument &Arg : F.args()) {
    if (!Arg.hasByValAttr())
      continue;
    uint64_t Size =
        DL.getTypeStoreSize(Arg.getType()->getPointerElementType());
    if (IsSafeStackAlloca(&Arg, Size))
      continue;

    ++NumUnsafeByValArguments;
    ByValArguments.push_back(&Arg);
  }
}

AllocaInst *
SafeStack::createStackRestorePoints(IRBuilder<> &IRB, Function &F,
                                    ArrayRef<Instruction *> StackRestorePoints,
                                    Value *StaticTop, bool NeedDynamicTop) {
  assert(StaticTop && "The stack top isn't set.");

  if (StackRestorePoints.empty())
    return nullptr;

  // We need the current value of the shadow stack pointer to restore
  // after longjmp or exception catching.

  // FIXME: On some platforms this could be handled by the longjmp/exception
  // runtime itself.

  AllocaInst *DynamicTop = nullptr;
  if (NeedDynamicTop) {
    // If we also have dynamic alloca's, the stack pointer value changes
    // throughout the function. For now we store it in an alloca.
    DynamicTop = IRB.CreateAlloca(StackPtrTy, /*ArraySize=*/nullptr,
                                  "unsafe_stack_dynamic_ptr");
    IRB.CreateStore(StaticTop, DynamicTop);
  }

  // Restore current stack pointer after longjmp/exception catch.
  for (Instruction *I : StackRestorePoints) {
    ++NumUnsafeStackRestorePoints;

    IRB.SetInsertPoint(I->getNextNode());
    Value *CurrentTop = DynamicTop ? IRB.CreateLoad(DynamicTop) : StaticTop;
    IRB.CreateStore(CurrentTop, UnsafeStackPtr);
  }

  return DynamicTop;
}

void SafeStack::checkStackGuard(IRBuilder<> &IRB, Function &F, ReturnInst &RI,
                                AllocaInst *StackGuardSlot, Value *StackGuard) {
  Value *V = IRB.CreateLoad(StackGuardSlot);
  Value *Cmp = IRB.CreateICmpNE(StackGuard, V);

  auto SuccessProb = BranchProbabilityInfo::getBranchProbStackProtector(true);
  auto FailureProb = BranchProbabilityInfo::getBranchProbStackProtector(false);
  MDNode *Weights = MDBuilder(F.getContext())
                        .createBranchWeights(SuccessProb.getNumerator(),
                                             FailureProb.getNumerator());
  Instruction *CheckTerm =
      SplitBlockAndInsertIfThen(Cmp, &RI,
                                /* Unreachable */ true, Weights);
  IRBuilder<> IRBFail(CheckTerm);
  // FIXME: respect -fsanitize-trap / -ftrap-function here?
  Constant *StackChkFail = F.getParent()->getOrInsertFunction(
      "__stack_chk_fail", IRB.getVoidTy());
  IRBFail.CreateCall(StackChkFail, {});
}

/// We explicitly compute and set the unsafe stack layout for all unsafe
/// static alloca instructions. We save the unsafe "base pointer" in the
/// prologue into a local variable and restore it in the epilogue.
Value *SafeStack::moveStaticAllocasToUnsafeStack(
    IRBuilder<> &IRB, Function &F, ArrayRef<AllocaInst *> StaticAllocas,
    ArrayRef<Argument *> ByValArguments, ArrayRef<ReturnInst *> Returns,
    Instruction *BasePointer, AllocaInst *StackGuardSlot) {
  if (StaticAllocas.empty() && ByValArguments.empty())
    return BasePointer;

  DIBuilder DIB(*F.getParent());

  StackColoring SSC(F, StaticAllocas);
  SSC.run();
  SSC.removeAllMarkers();

  // Unsafe stack always grows down.
  StackLayout SSL(StackAlignment);
  if (StackGuardSlot) {
    Type *Ty = StackGuardSlot->getAllocatedType();
    unsigned Align =
        std::max(DL.getPrefTypeAlignment(Ty), StackGuardSlot->getAlignment());
    SSL.addObject(StackGuardSlot, getStaticAllocaAllocationSize(StackGuardSlot),
                  Align, SSC.getFullLiveRange());
  }

  for (Argument *Arg : ByValArguments) {
    Type *Ty = Arg->getType()->getPointerElementType();
    uint64_t Size = DL.getTypeStoreSize(Ty);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    // Ensure the object is properly aligned.
    unsigned Align = std::max((unsigned)DL.getPrefTypeAlignment(Ty),
                              Arg->getParamAlignment());
    SSL.addObject(Arg, Size, Align, SSC.getFullLiveRange());
  }

  for (AllocaInst *AI : StaticAllocas) {
    Type *Ty = AI->getAllocatedType();
    uint64_t Size = getStaticAllocaAllocationSize(AI);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    // Ensure the object is properly aligned.
    unsigned Align =
        std::max((unsigned)DL.getPrefTypeAlignment(Ty), AI->getAlignment());

    SSL.addObject(AI, Size, Align, SSC.getLiveRange(AI));
  }

  SSL.computeLayout();
  unsigned FrameAlignment = SSL.getFrameAlignment();

  // FIXME: tell SSL that we start at a less-then-MaxAlignment aligned location
  // (AlignmentSkew).
  if (FrameAlignment > StackAlignment) {
    // Re-align the base pointer according to the max requested alignment.
    assert(isPowerOf2_32(FrameAlignment));
    IRB.SetInsertPoint(BasePointer->getNextNode());
    BasePointer = cast<Instruction>(IRB.CreateIntToPtr(
        IRB.CreateAnd(IRB.CreatePtrToInt(BasePointer, IntPtrTy),
                      ConstantInt::get(IntPtrTy, ~uint64_t(FrameAlignment - 1))),
        StackPtrTy));
  }

  IRB.SetInsertPoint(BasePointer->getNextNode());

  if (StackGuardSlot) {
    unsigned Offset = SSL.getObjectOffset(StackGuardSlot);
    Value *Off = IRB.CreateGEP(BasePointer, // BasePointer is i8*
                               ConstantInt::get(Int32Ty, -Offset));
    Value *NewAI =
        IRB.CreateBitCast(Off, StackGuardSlot->getType(), "StackGuardSlot");

    // Replace alloc with the new location.
    StackGuardSlot->replaceAllUsesWith(NewAI);
    StackGuardSlot->eraseFromParent();
  }

  for (Argument *Arg : ByValArguments) {
    unsigned Offset = SSL.getObjectOffset(Arg);
    unsigned Align = SSL.getObjectAlignment(Arg);
    Type *Ty = Arg->getType()->getPointerElementType();

    uint64_t Size = DL.getTypeStoreSize(Ty);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    Value *Off = IRB.CreateGEP(BasePointer, // BasePointer is i8*
                               ConstantInt::get(Int32Ty, -Offset));
    Value *NewArg = IRB.CreateBitCast(Off, Arg->getType(),
                                     Arg->getName() + ".unsafe-byval");

    // Replace alloc with the new location.
    replaceDbgDeclare(Arg, BasePointer, BasePointer->getNextNode(), DIB,
                      DIExpression::NoDeref, -Offset, DIExpression::NoDeref);
    Arg->replaceAllUsesWith(NewArg);
    IRB.SetInsertPoint(cast<Instruction>(NewArg)->getNextNode());
    IRB.CreateMemCpy(Off, Align, Arg, Arg->getParamAlignment(), Size);
  }

  // Allocate space for every unsafe static AllocaInst on the unsafe stack.
  for (AllocaInst *AI : StaticAllocas) {
    IRB.SetInsertPoint(AI);
    unsigned Offset = SSL.getObjectOffset(AI);

    uint64_t Size = getStaticAllocaAllocationSize(AI);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    replaceDbgDeclareForAlloca(AI, BasePointer, DIB, DIExpression::NoDeref,
                               -Offset, DIExpression::NoDeref);
    replaceDbgValueForAlloca(AI, BasePointer, DIB, -Offset);

    // Replace uses of the alloca with the new location.
    // Insert address calculation close to each use to work around PR27844.
    std::string Name = std::string(AI->getName()) + ".unsafe";
    while (!AI->use_empty()) {
      Use &U = *AI->use_begin();
      Instruction *User = cast<Instruction>(U.getUser());

      Instruction *InsertBefore;
      if (auto *PHI = dyn_cast<PHINode>(User))
        InsertBefore = PHI->getIncomingBlock(U)->getTerminator();
      else
        InsertBefore = User;

      IRBuilder<> IRBUser(InsertBefore);
      Value *Off = IRBUser.CreateGEP(BasePointer, // BasePointer is i8*
                                     ConstantInt::get(Int32Ty, -Offset));
      Value *Replacement = IRBUser.CreateBitCast(Off, AI->getType(), Name);

      if (auto *PHI = dyn_cast<PHINode>(User)) {
        // PHI nodes may have multiple incoming edges from the same BB (why??),
        // all must be updated at once with the same incoming value.
        auto *BB = PHI->getIncomingBlock(U);
        for (unsigned I = 0; I < PHI->getNumIncomingValues(); ++I)
          if (PHI->getIncomingBlock(I) == BB)
            PHI->setIncomingValue(I, Replacement);
      } else {
        U.set(Replacement);
      }
    }

    AI->eraseFromParent();
  }

  // Re-align BasePointer so that our callees would see it aligned as
  // expected.
  // FIXME: no need to update BasePointer in leaf functions.
  unsigned FrameSize = alignTo(SSL.getFrameSize(), StackAlignment);

  // Update shadow stack pointer in the function epilogue.
  IRB.SetInsertPoint(BasePointer->getNextNode());

  Value *StaticTop =
      IRB.CreateGEP(BasePointer, ConstantInt::get(Int32Ty, -FrameSize),
                    "unsafe_stack_static_top");
  IRB.CreateStore(StaticTop, UnsafeStackPtr);
  return StaticTop;
}

void SafeStack::moveDynamicAllocasToUnsafeStack(
    Function &F, Value *UnsafeStackPtr, AllocaInst *DynamicTop,
    ArrayRef<AllocaInst *> DynamicAllocas) {
  DIBuilder DIB(*F.getParent());

  for (AllocaInst *AI : DynamicAllocas) {
    IRBuilder<> IRB(AI);

    // Compute the new SP value (after AI).
    Value *ArraySize = AI->getArraySize();
    if (ArraySize->getType() != IntPtrTy)
      ArraySize = IRB.CreateIntCast(ArraySize, IntPtrTy, false);

    Type *Ty = AI->getAllocatedType();
    uint64_t TySize = DL.getTypeAllocSize(Ty);
    Value *Size = IRB.CreateMul(ArraySize, ConstantInt::get(IntPtrTy, TySize));

    Value *SP = IRB.CreatePtrToInt(IRB.CreateLoad(UnsafeStackPtr), IntPtrTy);
    SP = IRB.CreateSub(SP, Size);

    // Align the SP value to satisfy the AllocaInst, type and stack alignments.
    unsigned Align = std::max(
        std::max((unsigned)DL.getPrefTypeAlignment(Ty), AI->getAlignment()),
        (unsigned)StackAlignment);

    assert(isPowerOf2_32(Align));
    Value *NewTop = IRB.CreateIntToPtr(
        IRB.CreateAnd(SP, ConstantInt::get(IntPtrTy, ~uint64_t(Align - 1))),
        StackPtrTy);

    // Save the stack pointer.
    IRB.CreateStore(NewTop, UnsafeStackPtr);
    if (DynamicTop)
      IRB.CreateStore(NewTop, DynamicTop);

    Value *NewAI = IRB.CreatePointerCast(NewTop, AI->getType());
    if (AI->hasName() && isa<Instruction>(NewAI))
      NewAI->takeName(AI);

    replaceDbgDeclareForAlloca(AI, NewAI, DIB, DIExpression::NoDeref, 0,
                               DIExpression::NoDeref);
    AI->replaceAllUsesWith(NewAI);
    AI->eraseFromParent();
  }

  if (!DynamicAllocas.empty()) {
    // Now go through the instructions again, replacing stacksave/stackrestore.
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      auto II = dyn_cast<IntrinsicInst>(I);
      if (!II)
        continue;

      if (II->getIntrinsicID() == Intrinsic::stacksave) {
        IRBuilder<> IRB(II);
        Instruction *LI = IRB.CreateLoad(UnsafeStackPtr);
        LI->takeName(II);
        II->replaceAllUsesWith(LI);
        II->eraseFromParent();
      } else if (II->getIntrinsicID() == Intrinsic::stackrestore) {
        IRBuilder<> IRB(II);
        Instruction *SI = IRB.CreateStore(II->getArgOperand(0), UnsafeStackPtr);
        SI->takeName(II);
        assert(II->use_empty());
        II->eraseFromParent();
      }
    }
  }
}

bool SafeStack::ShouldInlinePointerAddress(CallSite &CS) {
  Function *Callee = CS.getCalledFunction();
  if (CS.hasFnAttr(Attribute::AlwaysInline) && isInlineViable(*Callee))
    return true;
  if (Callee->isInterposable() || Callee->hasFnAttribute(Attribute::NoInline) ||
      CS.isNoInline())
    return false;
  return true;
}

void SafeStack::TryInlinePointerAddress() {
  if (!isa<CallInst>(UnsafeStackPtr))
    return;

  if(F.hasFnAttribute(Attribute::OptimizeNone))
    return;

  CallSite CS(UnsafeStackPtr);
  Function *Callee = CS.getCalledFunction();
  if (!Callee || Callee->isDeclaration())
    return;

  if (!ShouldInlinePointerAddress(CS))
    return;

  InlineFunctionInfo IFI;
  InlineFunction(CS, IFI);
}

bool SafeStack::run() {
  assert(F.hasFnAttribute(Attribute::SafeStack) &&
         "Can't run SafeStack on a function without the attribute");
  assert(!F.isDeclaration() && "Can't run SafeStack on a function declaration");

  ++NumFunctions;

  SmallVector<AllocaInst *, 16> StaticAllocas;
  SmallVector<AllocaInst *, 4> DynamicAllocas;
  SmallVector<Argument *, 4> ByValArguments;
  SmallVector<ReturnInst *, 4> Returns;

  // Collect all points where stack gets unwound and needs to be restored
  // This is only necessary because the runtime (setjmp and unwind code) is
  // not aware of the unsafe stack and won't unwind/restore it properly.
  // To work around this problem without changing the runtime, we insert
  // instrumentation to restore the unsafe stack pointer when necessary.
  SmallVector<Instruction *, 4> StackRestorePoints;

  // Find all static and dynamic alloca instructions that must be moved to the
  // unsafe stack, all return instructions and stack restore points.
  findInsts(F, StaticAllocas, DynamicAllocas, ByValArguments, Returns,
            StackRestorePoints);

  if (StaticAllocas.empty() && DynamicAllocas.empty() &&
      ByValArguments.empty() && StackRestorePoints.empty())
    return false; // Nothing to do in this function.

  if (!StaticAllocas.empty() || !DynamicAllocas.empty() ||
      !ByValArguments.empty())
    ++NumUnsafeStackFunctions; // This function has the unsafe stack.

  if (!StackRestorePoints.empty())
    ++NumUnsafeStackRestorePointsFunctions;

  IRBuilder<> IRB(&F.front(), F.begin()->getFirstInsertionPt());
  // Calls must always have a debug location, or else inlining breaks. So
  // we explicitly set a artificial debug location here.
  if (DISubprogram *SP = F.getSubprogram())
    IRB.SetCurrentDebugLocation(DebugLoc::get(SP->getScopeLine(), 0, SP));
  if (SafeStackUsePointerAddress) {
    Value *Fn = F.getParent()->getOrInsertFunction(
        "__safestack_pointer_address", StackPtrTy->getPointerTo(0));
    UnsafeStackPtr = IRB.CreateCall(Fn);
  } else {
    UnsafeStackPtr = TL.getSafeStackPointerLocation(IRB);
  }

  // Load the current stack pointer (we'll also use it as a base pointer).
  // FIXME: use a dedicated register for it ?
  Instruction *BasePointer =
      IRB.CreateLoad(UnsafeStackPtr, false, "unsafe_stack_ptr");
  assert(BasePointer->getType() == StackPtrTy);

  AllocaInst *StackGuardSlot = nullptr;
  // FIXME: implement weaker forms of stack protector.
  if (F.hasFnAttribute(Attribute::StackProtect) ||
      F.hasFnAttribute(Attribute::StackProtectStrong) ||
      F.hasFnAttribute(Attribute::StackProtectReq)) {
    Value *StackGuard = getStackGuard(IRB, F);
    StackGuardSlot = IRB.CreateAlloca(StackPtrTy, nullptr);
    IRB.CreateStore(StackGuard, StackGuardSlot);

    for (ReturnInst *RI : Returns) {
      IRBuilder<> IRBRet(RI);
      checkStackGuard(IRBRet, F, *RI, StackGuardSlot, StackGuard);
    }
  }

  // The top of the unsafe stack after all unsafe static allocas are
  // allocated.
  Value *StaticTop =
      moveStaticAllocasToUnsafeStack(IRB, F, StaticAllocas, ByValArguments,
                                     Returns, BasePointer, StackGuardSlot);

  // Safe stack object that stores the current unsafe stack top. It is updated
  // as unsafe dynamic (non-constant-sized) allocas are allocated and freed.
  // This is only needed if we need to restore stack pointer after longjmp
  // or exceptions, and we have dynamic allocations.
  // FIXME: a better alternative might be to store the unsafe stack pointer
  // before setjmp / invoke instructions.
  AllocaInst *DynamicTop = createStackRestorePoints(
      IRB, F, StackRestorePoints, StaticTop, !DynamicAllocas.empty());

  // Handle dynamic allocas.
  moveDynamicAllocasToUnsafeStack(F, UnsafeStackPtr, DynamicTop,
                                  DynamicAllocas);

  // Restore the unsafe stack pointer before each return.
  for (ReturnInst *RI : Returns) {
    IRB.SetInsertPoint(RI);
    IRB.CreateStore(BasePointer, UnsafeStackPtr);
  }

  TryInlinePointerAddress();

  LLVM_DEBUG(dbgs() << "[SafeStack]     safestack applied\n");
  return true;
}

class SafeStackLegacyPass : public FunctionPass {
  const TargetMachine *TM = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid..

  SafeStackLegacyPass() : FunctionPass(ID) {
    initializeSafeStackLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
  }

  bool runOnFunction(Function &F) override {
    LLVM_DEBUG(dbgs() << "[SafeStack] Function: " << F.getName() << "\n");

    if (!F.hasFnAttribute(Attribute::SafeStack)) {
      LLVM_DEBUG(dbgs() << "[SafeStack]     safestack is not requested"
                           " for this function\n");
      return false;
    }

    if (F.isDeclaration()) {
      LLVM_DEBUG(dbgs() << "[SafeStack]     function definition"
                           " is not available\n");
      return false;
    }

    TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    auto *TL = TM->getSubtargetImpl(F)->getTargetLowering();
    if (!TL)
      report_fatal_error("TargetLowering instance is required");

    auto *DL = &F.getParent()->getDataLayout();
    auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    auto &ACT = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

    // Compute DT and LI only for functions that have the attribute.
    // This is only useful because the legacy pass manager doesn't let us
    // compute analyzes lazily.
    // In the backend pipeline, nothing preserves DT before SafeStack, so we
    // would otherwise always compute it wastefully, even if there is no
    // function with the safestack attribute.
    DominatorTree DT(F);
    LoopInfo LI(DT);

    ScalarEvolution SE(F, TLI, ACT, DT, LI);

    return SafeStack(F, *TL, *DL, SE).run();
  }
};

} // end anonymous namespace

char SafeStackLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(SafeStackLegacyPass, DEBUG_TYPE,
                      "Safe Stack instrumentation pass", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(SafeStackLegacyPass, DEBUG_TYPE,
                    "Safe Stack instrumentation pass", false, false)

FunctionPass *llvm::createSafeStackPass() { return new SafeStackLegacyPass(); }
