//===-- Lint.cpp - Check for common errors in LLVM IR ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass statically checks for common and easily-identified constructs
// which produce undefined or likely unintended behavior in LLVM IR.
//
// It is not a guarantee of correctness, in two ways. First, it isn't
// comprehensive. There are checks which could be done statically which are
// not yet implemented. Some of these are indicated by TODO comments, but
// those aren't comprehensive either. Second, many conditions cannot be
// checked statically. This pass does no dynamic instrumentation, so it
// can't check for all possible problems.
//
// Another limitation is that it assumes all code will be executed. A store
// through a null pointer in a basic block which is never reached is harmless,
// but this pass will warn about it anyway. This is the main reason why most
// of these checks live here instead of in the Verifier pass.
//
// Optimization passes may make conditions that this pass checks for more or
// less obvious. If an optimization pass appears to be introducing a warning,
// it may be that the optimization pass is merely exposing an existing
// condition in the code.
//
// This code may be run before instcombine. In many cases, instcombine checks
// for the same kinds of things and turns instructions with undefined behavior
// into unreachable (or equivalent). Because of this, this pass makes some
// effort to look through bitcasts and so on.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Lint.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>

using namespace llvm;

namespace {
  namespace MemRef {
    static const unsigned Read     = 1;
    static const unsigned Write    = 2;
    static const unsigned Callee   = 4;
    static const unsigned Branchee = 8;
  } // end namespace MemRef

  class Lint : public FunctionPass, public InstVisitor<Lint> {
    friend class InstVisitor<Lint>;

    void visitFunction(Function &F);

    void visitCallSite(CallSite CS);
    void visitMemoryReference(Instruction &I, Value *Ptr,
                              uint64_t Size, unsigned Align,
                              Type *Ty, unsigned Flags);
    void visitEHBeginCatch(IntrinsicInst *II);
    void visitEHEndCatch(IntrinsicInst *II);

    void visitCallInst(CallInst &I);
    void visitInvokeInst(InvokeInst &I);
    void visitReturnInst(ReturnInst &I);
    void visitLoadInst(LoadInst &I);
    void visitStoreInst(StoreInst &I);
    void visitXor(BinaryOperator &I);
    void visitSub(BinaryOperator &I);
    void visitLShr(BinaryOperator &I);
    void visitAShr(BinaryOperator &I);
    void visitShl(BinaryOperator &I);
    void visitSDiv(BinaryOperator &I);
    void visitUDiv(BinaryOperator &I);
    void visitSRem(BinaryOperator &I);
    void visitURem(BinaryOperator &I);
    void visitAllocaInst(AllocaInst &I);
    void visitVAArgInst(VAArgInst &I);
    void visitIndirectBrInst(IndirectBrInst &I);
    void visitExtractElementInst(ExtractElementInst &I);
    void visitInsertElementInst(InsertElementInst &I);
    void visitUnreachableInst(UnreachableInst &I);

    Value *findValue(Value *V, bool OffsetOk) const;
    Value *findValueImpl(Value *V, bool OffsetOk,
                         SmallPtrSetImpl<Value *> &Visited) const;

  public:
    Module *Mod;
    const DataLayout *DL;
    AliasAnalysis *AA;
    AssumptionCache *AC;
    DominatorTree *DT;
    TargetLibraryInfo *TLI;

    std::string Messages;
    raw_string_ostream MessagesStr;

    static char ID; // Pass identification, replacement for typeid
    Lint() : FunctionPass(ID), MessagesStr(Messages) {
      initializeLintPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<TargetLibraryInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
    }
    void print(raw_ostream &O, const Module *M) const override {}

    void WriteValues(ArrayRef<const Value *> Vs) {
      for (const Value *V : Vs) {
        if (!V)
          continue;
        if (isa<Instruction>(V)) {
          MessagesStr << *V << '\n';
        } else {
          V->printAsOperand(MessagesStr, true, Mod);
          MessagesStr << '\n';
        }
      }
    }

    /// A check failed, so printout out the condition and the message.
    ///
    /// This provides a nice place to put a breakpoint if you want to see why
    /// something is not correct.
    void CheckFailed(const Twine &Message) { MessagesStr << Message << '\n'; }

    /// A check failed (with values to print).
    ///
    /// This calls the Message-only version so that the above is easier to set
    /// a breakpoint on.
    template <typename T1, typename... Ts>
    void CheckFailed(const Twine &Message, const T1 &V1, const Ts &...Vs) {
      CheckFailed(Message);
      WriteValues({V1, Vs...});
    }
  };
} // end anonymous namespace

char Lint::ID = 0;
INITIALIZE_PASS_BEGIN(Lint, "lint", "Statically lint-checks LLVM IR",
                      false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(Lint, "lint", "Statically lint-checks LLVM IR",
                    false, true)

// Assert - We know that cond should be true, if not print an error message.
#define Assert(C, ...) \
    do { if (!(C)) { CheckFailed(__VA_ARGS__); return; } } while (false)

// Lint::run - This is the main Analysis entry point for a
// function.
//
bool Lint::runOnFunction(Function &F) {
  Mod = F.getParent();
  DL = &F.getParent()->getDataLayout();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  visit(F);
  dbgs() << MessagesStr.str();
  Messages.clear();
  return false;
}

void Lint::visitFunction(Function &F) {
  // This isn't undefined behavior, it's just a little unusual, and it's a
  // fairly common mistake to neglect to name a function.
  Assert(F.hasName() || F.hasLocalLinkage(),
         "Unusual: Unnamed function with non-local linkage", &F);

  // TODO: Check for irreducible control flow.
}

void Lint::visitCallSite(CallSite CS) {
  Instruction &I = *CS.getInstruction();
  Value *Callee = CS.getCalledValue();

  visitMemoryReference(I, Callee, MemoryLocation::UnknownSize, 0, nullptr,
                       MemRef::Callee);

  if (Function *F = dyn_cast<Function>(findValue(Callee,
                                                 /*OffsetOk=*/false))) {
    Assert(CS.getCallingConv() == F->getCallingConv(),
           "Undefined behavior: Caller and callee calling convention differ",
           &I);

    FunctionType *FT = F->getFunctionType();
    unsigned NumActualArgs = CS.arg_size();

    Assert(FT->isVarArg() ? FT->getNumParams() <= NumActualArgs
                          : FT->getNumParams() == NumActualArgs,
           "Undefined behavior: Call argument count mismatches callee "
           "argument count",
           &I);

    Assert(FT->getReturnType() == I.getType(),
           "Undefined behavior: Call return type mismatches "
           "callee return type",
           &I);

    // Check argument types (in case the callee was casted) and attributes.
    // TODO: Verify that caller and callee attributes are compatible.
    Function::arg_iterator PI = F->arg_begin(), PE = F->arg_end();
    CallSite::arg_iterator AI = CS.arg_begin(), AE = CS.arg_end();
    for (; AI != AE; ++AI) {
      Value *Actual = *AI;
      if (PI != PE) {
        Argument *Formal = &*PI++;
        Assert(Formal->getType() == Actual->getType(),
               "Undefined behavior: Call argument type mismatches "
               "callee parameter type",
               &I);

        // Check that noalias arguments don't alias other arguments. This is
        // not fully precise because we don't know the sizes of the dereferenced
        // memory regions.
        if (Formal->hasNoAliasAttr() && Actual->getType()->isPointerTy()) {
          AttributeList PAL = CS.getAttributes();
          unsigned ArgNo = 0;
          for (CallSite::arg_iterator BI = CS.arg_begin(); BI != AE; ++BI) {
            // Skip ByVal arguments since they will be memcpy'd to the callee's
            // stack so we're not really passing the pointer anyway.
            if (PAL.hasParamAttribute(ArgNo++, Attribute::ByVal))
              continue;
            if (AI != BI && (*BI)->getType()->isPointerTy()) {
              AliasResult Result = AA->alias(*AI, *BI);
              Assert(Result != MustAlias && Result != PartialAlias,
                     "Unusual: noalias argument aliases another argument", &I);
            }
          }
        }

        // Check that an sret argument points to valid memory.
        if (Formal->hasStructRetAttr() && Actual->getType()->isPointerTy()) {
          Type *Ty =
            cast<PointerType>(Formal->getType())->getElementType();
          visitMemoryReference(I, Actual, DL->getTypeStoreSize(Ty),
                               DL->getABITypeAlignment(Ty), Ty,
                               MemRef::Read | MemRef::Write);
        }
      }
    }
  }

  if (CS.isCall()) {
    const CallInst *CI = cast<CallInst>(CS.getInstruction());
    if (CI->isTailCall()) {
      const AttributeList &PAL = CI->getAttributes();
      unsigned ArgNo = 0;
      for (Value *Arg : CS.args()) {
        // Skip ByVal arguments since they will be memcpy'd to the callee's
        // stack anyway.
        if (PAL.hasParamAttribute(ArgNo++, Attribute::ByVal))
          continue;
        Value *Obj = findValue(Arg, /*OffsetOk=*/true);
        Assert(!isa<AllocaInst>(Obj),
               "Undefined behavior: Call with \"tail\" keyword references "
               "alloca",
               &I);
      }
    }
  }


  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I))
    switch (II->getIntrinsicID()) {
    default: break;

    // TODO: Check more intrinsics

    case Intrinsic::memcpy: {
      MemCpyInst *MCI = cast<MemCpyInst>(&I);
      // TODO: If the size is known, use it.
      visitMemoryReference(I, MCI->getDest(), MemoryLocation::UnknownSize,
                           MCI->getDestAlignment(), nullptr, MemRef::Write);
      visitMemoryReference(I, MCI->getSource(), MemoryLocation::UnknownSize,
                           MCI->getSourceAlignment(), nullptr, MemRef::Read);

      // Check that the memcpy arguments don't overlap. The AliasAnalysis API
      // isn't expressive enough for what we really want to do. Known partial
      // overlap is not distinguished from the case where nothing is known.
      auto Size = LocationSize::unknown();
      if (const ConstantInt *Len =
              dyn_cast<ConstantInt>(findValue(MCI->getLength(),
                                              /*OffsetOk=*/false)))
        if (Len->getValue().isIntN(32))
          Size = LocationSize::precise(Len->getValue().getZExtValue());
      Assert(AA->alias(MCI->getSource(), Size, MCI->getDest(), Size) !=
                 MustAlias,
             "Undefined behavior: memcpy source and destination overlap", &I);
      break;
    }
    case Intrinsic::memmove: {
      MemMoveInst *MMI = cast<MemMoveInst>(&I);
      // TODO: If the size is known, use it.
      visitMemoryReference(I, MMI->getDest(), MemoryLocation::UnknownSize,
                           MMI->getDestAlignment(), nullptr, MemRef::Write);
      visitMemoryReference(I, MMI->getSource(), MemoryLocation::UnknownSize,
                           MMI->getSourceAlignment(), nullptr, MemRef::Read);
      break;
    }
    case Intrinsic::memset: {
      MemSetInst *MSI = cast<MemSetInst>(&I);
      // TODO: If the size is known, use it.
      visitMemoryReference(I, MSI->getDest(), MemoryLocation::UnknownSize,
                           MSI->getDestAlignment(), nullptr, MemRef::Write);
      break;
    }

    case Intrinsic::vastart:
      Assert(I.getParent()->getParent()->isVarArg(),
             "Undefined behavior: va_start called in a non-varargs function",
             &I);

      visitMemoryReference(I, CS.getArgument(0), MemoryLocation::UnknownSize, 0,
                           nullptr, MemRef::Read | MemRef::Write);
      break;
    case Intrinsic::vacopy:
      visitMemoryReference(I, CS.getArgument(0), MemoryLocation::UnknownSize, 0,
                           nullptr, MemRef::Write);
      visitMemoryReference(I, CS.getArgument(1), MemoryLocation::UnknownSize, 0,
                           nullptr, MemRef::Read);
      break;
    case Intrinsic::vaend:
      visitMemoryReference(I, CS.getArgument(0), MemoryLocation::UnknownSize, 0,
                           nullptr, MemRef::Read | MemRef::Write);
      break;

    case Intrinsic::stackrestore:
      // Stackrestore doesn't read or write memory, but it sets the
      // stack pointer, which the compiler may read from or write to
      // at any time, so check it for both readability and writeability.
      visitMemoryReference(I, CS.getArgument(0), MemoryLocation::UnknownSize, 0,
                           nullptr, MemRef::Read | MemRef::Write);
      break;
    }
}

void Lint::visitCallInst(CallInst &I) {
  return visitCallSite(&I);
}

void Lint::visitInvokeInst(InvokeInst &I) {
  return visitCallSite(&I);
}

void Lint::visitReturnInst(ReturnInst &I) {
  Function *F = I.getParent()->getParent();
  Assert(!F->doesNotReturn(),
         "Unusual: Return statement in function with noreturn attribute", &I);

  if (Value *V = I.getReturnValue()) {
    Value *Obj = findValue(V, /*OffsetOk=*/true);
    Assert(!isa<AllocaInst>(Obj), "Unusual: Returning alloca value", &I);
  }
}

// TODO: Check that the reference is in bounds.
// TODO: Check readnone/readonly function attributes.
void Lint::visitMemoryReference(Instruction &I,
                                Value *Ptr, uint64_t Size, unsigned Align,
                                Type *Ty, unsigned Flags) {
  // If no memory is being referenced, it doesn't matter if the pointer
  // is valid.
  if (Size == 0)
    return;

  Value *UnderlyingObject = findValue(Ptr, /*OffsetOk=*/true);
  Assert(!isa<ConstantPointerNull>(UnderlyingObject),
         "Undefined behavior: Null pointer dereference", &I);
  Assert(!isa<UndefValue>(UnderlyingObject),
         "Undefined behavior: Undef pointer dereference", &I);
  Assert(!isa<ConstantInt>(UnderlyingObject) ||
             !cast<ConstantInt>(UnderlyingObject)->isMinusOne(),
         "Unusual: All-ones pointer dereference", &I);
  Assert(!isa<ConstantInt>(UnderlyingObject) ||
             !cast<ConstantInt>(UnderlyingObject)->isOne(),
         "Unusual: Address one pointer dereference", &I);

  if (Flags & MemRef::Write) {
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(UnderlyingObject))
      Assert(!GV->isConstant(), "Undefined behavior: Write to read-only memory",
             &I);
    Assert(!isa<Function>(UnderlyingObject) &&
               !isa<BlockAddress>(UnderlyingObject),
           "Undefined behavior: Write to text section", &I);
  }
  if (Flags & MemRef::Read) {
    Assert(!isa<Function>(UnderlyingObject), "Unusual: Load from function body",
           &I);
    Assert(!isa<BlockAddress>(UnderlyingObject),
           "Undefined behavior: Load from block address", &I);
  }
  if (Flags & MemRef::Callee) {
    Assert(!isa<BlockAddress>(UnderlyingObject),
           "Undefined behavior: Call to block address", &I);
  }
  if (Flags & MemRef::Branchee) {
    Assert(!isa<Constant>(UnderlyingObject) ||
               isa<BlockAddress>(UnderlyingObject),
           "Undefined behavior: Branch to non-blockaddress", &I);
  }

  // Check for buffer overflows and misalignment.
  // Only handles memory references that read/write something simple like an
  // alloca instruction or a global variable.
  int64_t Offset = 0;
  if (Value *Base = GetPointerBaseWithConstantOffset(Ptr, Offset, *DL)) {
    // OK, so the access is to a constant offset from Ptr.  Check that Ptr is
    // something we can handle and if so extract the size of this base object
    // along with its alignment.
    uint64_t BaseSize = MemoryLocation::UnknownSize;
    unsigned BaseAlign = 0;

    if (AllocaInst *AI = dyn_cast<AllocaInst>(Base)) {
      Type *ATy = AI->getAllocatedType();
      if (!AI->isArrayAllocation() && ATy->isSized())
        BaseSize = DL->getTypeAllocSize(ATy);
      BaseAlign = AI->getAlignment();
      if (BaseAlign == 0 && ATy->isSized())
        BaseAlign = DL->getABITypeAlignment(ATy);
    } else if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Base)) {
      // If the global may be defined differently in another compilation unit
      // then don't warn about funky memory accesses.
      if (GV->hasDefinitiveInitializer()) {
        Type *GTy = GV->getValueType();
        if (GTy->isSized())
          BaseSize = DL->getTypeAllocSize(GTy);
        BaseAlign = GV->getAlignment();
        if (BaseAlign == 0 && GTy->isSized())
          BaseAlign = DL->getABITypeAlignment(GTy);
      }
    }

    // Accesses from before the start or after the end of the object are not
    // defined.
    Assert(Size == MemoryLocation::UnknownSize ||
               BaseSize == MemoryLocation::UnknownSize ||
               (Offset >= 0 && Offset + Size <= BaseSize),
           "Undefined behavior: Buffer overflow", &I);

    // Accesses that say that the memory is more aligned than it is are not
    // defined.
    if (Align == 0 && Ty && Ty->isSized())
      Align = DL->getABITypeAlignment(Ty);
    Assert(!BaseAlign || Align <= MinAlign(BaseAlign, Offset),
           "Undefined behavior: Memory reference address is misaligned", &I);
  }
}

void Lint::visitLoadInst(LoadInst &I) {
  visitMemoryReference(I, I.getPointerOperand(),
                       DL->getTypeStoreSize(I.getType()), I.getAlignment(),
                       I.getType(), MemRef::Read);
}

void Lint::visitStoreInst(StoreInst &I) {
  visitMemoryReference(I, I.getPointerOperand(),
                       DL->getTypeStoreSize(I.getOperand(0)->getType()),
                       I.getAlignment(),
                       I.getOperand(0)->getType(), MemRef::Write);
}

void Lint::visitXor(BinaryOperator &I) {
  Assert(!isa<UndefValue>(I.getOperand(0)) || !isa<UndefValue>(I.getOperand(1)),
         "Undefined result: xor(undef, undef)", &I);
}

void Lint::visitSub(BinaryOperator &I) {
  Assert(!isa<UndefValue>(I.getOperand(0)) || !isa<UndefValue>(I.getOperand(1)),
         "Undefined result: sub(undef, undef)", &I);
}

void Lint::visitLShr(BinaryOperator &I) {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(findValue(I.getOperand(1),
                                                        /*OffsetOk=*/false)))
    Assert(CI->getValue().ult(cast<IntegerType>(I.getType())->getBitWidth()),
           "Undefined result: Shift count out of range", &I);
}

void Lint::visitAShr(BinaryOperator &I) {
  if (ConstantInt *CI =
          dyn_cast<ConstantInt>(findValue(I.getOperand(1), /*OffsetOk=*/false)))
    Assert(CI->getValue().ult(cast<IntegerType>(I.getType())->getBitWidth()),
           "Undefined result: Shift count out of range", &I);
}

void Lint::visitShl(BinaryOperator &I) {
  if (ConstantInt *CI =
          dyn_cast<ConstantInt>(findValue(I.getOperand(1), /*OffsetOk=*/false)))
    Assert(CI->getValue().ult(cast<IntegerType>(I.getType())->getBitWidth()),
           "Undefined result: Shift count out of range", &I);
}

static bool isZero(Value *V, const DataLayout &DL, DominatorTree *DT,
                   AssumptionCache *AC) {
  // Assume undef could be zero.
  if (isa<UndefValue>(V))
    return true;

  VectorType *VecTy = dyn_cast<VectorType>(V->getType());
  if (!VecTy) {
    KnownBits Known = computeKnownBits(V, DL, 0, AC, dyn_cast<Instruction>(V), DT);
    return Known.isZero();
  }

  // Per-component check doesn't work with zeroinitializer
  Constant *C = dyn_cast<Constant>(V);
  if (!C)
    return false;

  if (C->isZeroValue())
    return true;

  // For a vector, KnownZero will only be true if all values are zero, so check
  // this per component
  for (unsigned I = 0, N = VecTy->getNumElements(); I != N; ++I) {
    Constant *Elem = C->getAggregateElement(I);
    if (isa<UndefValue>(Elem))
      return true;

    KnownBits Known = computeKnownBits(Elem, DL);
    if (Known.isZero())
      return true;
  }

  return false;
}

void Lint::visitSDiv(BinaryOperator &I) {
  Assert(!isZero(I.getOperand(1), I.getModule()->getDataLayout(), DT, AC),
         "Undefined behavior: Division by zero", &I);
}

void Lint::visitUDiv(BinaryOperator &I) {
  Assert(!isZero(I.getOperand(1), I.getModule()->getDataLayout(), DT, AC),
         "Undefined behavior: Division by zero", &I);
}

void Lint::visitSRem(BinaryOperator &I) {
  Assert(!isZero(I.getOperand(1), I.getModule()->getDataLayout(), DT, AC),
         "Undefined behavior: Division by zero", &I);
}

void Lint::visitURem(BinaryOperator &I) {
  Assert(!isZero(I.getOperand(1), I.getModule()->getDataLayout(), DT, AC),
         "Undefined behavior: Division by zero", &I);
}

void Lint::visitAllocaInst(AllocaInst &I) {
  if (isa<ConstantInt>(I.getArraySize()))
    // This isn't undefined behavior, it's just an obvious pessimization.
    Assert(&I.getParent()->getParent()->getEntryBlock() == I.getParent(),
           "Pessimization: Static alloca outside of entry block", &I);

  // TODO: Check for an unusual size (MSB set?)
}

void Lint::visitVAArgInst(VAArgInst &I) {
  visitMemoryReference(I, I.getOperand(0), MemoryLocation::UnknownSize, 0,
                       nullptr, MemRef::Read | MemRef::Write);
}

void Lint::visitIndirectBrInst(IndirectBrInst &I) {
  visitMemoryReference(I, I.getAddress(), MemoryLocation::UnknownSize, 0,
                       nullptr, MemRef::Branchee);

  Assert(I.getNumDestinations() != 0,
         "Undefined behavior: indirectbr with no destinations", &I);
}

void Lint::visitExtractElementInst(ExtractElementInst &I) {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(findValue(I.getIndexOperand(),
                                                        /*OffsetOk=*/false)))
    Assert(CI->getValue().ult(I.getVectorOperandType()->getNumElements()),
           "Undefined result: extractelement index out of range", &I);
}

void Lint::visitInsertElementInst(InsertElementInst &I) {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(findValue(I.getOperand(2),
                                                        /*OffsetOk=*/false)))
    Assert(CI->getValue().ult(I.getType()->getNumElements()),
           "Undefined result: insertelement index out of range", &I);
}

void Lint::visitUnreachableInst(UnreachableInst &I) {
  // This isn't undefined behavior, it's merely suspicious.
  Assert(&I == &I.getParent()->front() ||
             std::prev(I.getIterator())->mayHaveSideEffects(),
         "Unusual: unreachable immediately preceded by instruction without "
         "side effects",
         &I);
}

/// findValue - Look through bitcasts and simple memory reference patterns
/// to identify an equivalent, but more informative, value.  If OffsetOk
/// is true, look through getelementptrs with non-zero offsets too.
///
/// Most analysis passes don't require this logic, because instcombine
/// will simplify most of these kinds of things away. But it's a goal of
/// this Lint pass to be useful even on non-optimized IR.
Value *Lint::findValue(Value *V, bool OffsetOk) const {
  SmallPtrSet<Value *, 4> Visited;
  return findValueImpl(V, OffsetOk, Visited);
}

/// findValueImpl - Implementation helper for findValue.
Value *Lint::findValueImpl(Value *V, bool OffsetOk,
                           SmallPtrSetImpl<Value *> &Visited) const {
  // Detect self-referential values.
  if (!Visited.insert(V).second)
    return UndefValue::get(V->getType());

  // TODO: Look through sext or zext cast, when the result is known to
  // be interpreted as signed or unsigned, respectively.
  // TODO: Look through eliminable cast pairs.
  // TODO: Look through calls with unique return values.
  // TODO: Look through vector insert/extract/shuffle.
  V = OffsetOk ? GetUnderlyingObject(V, *DL) : V->stripPointerCasts();
  if (LoadInst *L = dyn_cast<LoadInst>(V)) {
    BasicBlock::iterator BBI = L->getIterator();
    BasicBlock *BB = L->getParent();
    SmallPtrSet<BasicBlock *, 4> VisitedBlocks;
    for (;;) {
      if (!VisitedBlocks.insert(BB).second)
        break;
      if (Value *U =
          FindAvailableLoadedValue(L, BB, BBI, DefMaxInstsToScan, AA))
        return findValueImpl(U, OffsetOk, Visited);
      if (BBI != BB->begin()) break;
      BB = BB->getUniquePredecessor();
      if (!BB) break;
      BBI = BB->end();
    }
  } else if (PHINode *PN = dyn_cast<PHINode>(V)) {
    if (Value *W = PN->hasConstantValue())
      if (W != V)
        return findValueImpl(W, OffsetOk, Visited);
  } else if (CastInst *CI = dyn_cast<CastInst>(V)) {
    if (CI->isNoopCast(*DL))
      return findValueImpl(CI->getOperand(0), OffsetOk, Visited);
  } else if (ExtractValueInst *Ex = dyn_cast<ExtractValueInst>(V)) {
    if (Value *W = FindInsertedValue(Ex->getAggregateOperand(),
                                     Ex->getIndices()))
      if (W != V)
        return findValueImpl(W, OffsetOk, Visited);
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    // Same as above, but for ConstantExpr instead of Instruction.
    if (Instruction::isCast(CE->getOpcode())) {
      if (CastInst::isNoopCast(Instruction::CastOps(CE->getOpcode()),
                               CE->getOperand(0)->getType(), CE->getType(),
                               *DL))
        return findValueImpl(CE->getOperand(0), OffsetOk, Visited);
    } else if (CE->getOpcode() == Instruction::ExtractValue) {
      ArrayRef<unsigned> Indices = CE->getIndices();
      if (Value *W = FindInsertedValue(CE->getOperand(0), Indices))
        if (W != V)
          return findValueImpl(W, OffsetOk, Visited);
    }
  }

  // As a last resort, try SimplifyInstruction or constant folding.
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {
    if (Value *W = SimplifyInstruction(Inst, {*DL, TLI, DT, AC}))
      return findValueImpl(W, OffsetOk, Visited);
  } else if (auto *C = dyn_cast<Constant>(V)) {
    if (Value *W = ConstantFoldConstant(C, *DL, TLI))
      if (W && W != V)
        return findValueImpl(W, OffsetOk, Visited);
  }

  return V;
}

//===----------------------------------------------------------------------===//
//  Implement the public interfaces to this file...
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createLintPass() {
  return new Lint();
}

/// lintFunction - Check a function for errors, printing messages on stderr.
///
void llvm::lintFunction(const Function &f) {
  Function &F = const_cast<Function&>(f);
  assert(!F.isDeclaration() && "Cannot lint external functions");

  legacy::FunctionPassManager FPM(F.getParent());
  Lint *V = new Lint();
  FPM.add(V);
  FPM.run(F);
}

/// lintModule - Check a module for errors, printing messages on stderr.
///
void llvm::lintModule(const Module &M) {
  legacy::PassManager PM;
  Lint *V = new Lint();
  PM.add(V);
  PM.run(const_cast<Module&>(M));
}
