//===- AtomicExpandPass.cpp - Expand atomic instructions ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass (at IR level) to replace atomic instructions with
// __atomic_* library calls, or target specific instruction which implement the
// same semantics in a way which better fits the target backend.  This can
// include the use of (intrinsic-based) load-linked/store-conditional loops,
// AtomicCmpXchg, or type coercions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AtomicExpandUtils.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "atomic-expand"

namespace {

  class AtomicExpand: public FunctionPass {
    const TargetLowering *TLI = nullptr;

  public:
    static char ID; // Pass identification, replacement for typeid

    AtomicExpand() : FunctionPass(ID) {
      initializeAtomicExpandPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;

  private:
    bool bracketInstWithFences(Instruction *I, AtomicOrdering Order);
    IntegerType *getCorrespondingIntegerType(Type *T, const DataLayout &DL);
    LoadInst *convertAtomicLoadToIntegerType(LoadInst *LI);
    bool tryExpandAtomicLoad(LoadInst *LI);
    bool expandAtomicLoadToLL(LoadInst *LI);
    bool expandAtomicLoadToCmpXchg(LoadInst *LI);
    StoreInst *convertAtomicStoreToIntegerType(StoreInst *SI);
    bool expandAtomicStore(StoreInst *SI);
    bool tryExpandAtomicRMW(AtomicRMWInst *AI);
    Value *
    insertRMWLLSCLoop(IRBuilder<> &Builder, Type *ResultTy, Value *Addr,
                      AtomicOrdering MemOpOrder,
                      function_ref<Value *(IRBuilder<> &, Value *)> PerformOp);
    void expandAtomicOpToLLSC(
        Instruction *I, Type *ResultTy, Value *Addr, AtomicOrdering MemOpOrder,
        function_ref<Value *(IRBuilder<> &, Value *)> PerformOp);
    void expandPartwordAtomicRMW(
        AtomicRMWInst *I,
        TargetLoweringBase::AtomicExpansionKind ExpansionKind);
    AtomicRMWInst *widenPartwordAtomicRMW(AtomicRMWInst *AI);
    void expandPartwordCmpXchg(AtomicCmpXchgInst *I);
    void expandAtomicRMWToMaskedIntrinsic(AtomicRMWInst *AI);
    void expandAtomicCmpXchgToMaskedIntrinsic(AtomicCmpXchgInst *CI);

    AtomicCmpXchgInst *convertCmpXchgToIntegerType(AtomicCmpXchgInst *CI);
    static Value *insertRMWCmpXchgLoop(
        IRBuilder<> &Builder, Type *ResultType, Value *Addr,
        AtomicOrdering MemOpOrder,
        function_ref<Value *(IRBuilder<> &, Value *)> PerformOp,
        CreateCmpXchgInstFun CreateCmpXchg);
    bool tryExpandAtomicCmpXchg(AtomicCmpXchgInst *CI);

    bool expandAtomicCmpXchg(AtomicCmpXchgInst *CI);
    bool isIdempotentRMW(AtomicRMWInst *RMWI);
    bool simplifyIdempotentRMW(AtomicRMWInst *RMWI);

    bool expandAtomicOpToLibcall(Instruction *I, unsigned Size, unsigned Align,
                                 Value *PointerOperand, Value *ValueOperand,
                                 Value *CASExpected, AtomicOrdering Ordering,
                                 AtomicOrdering Ordering2,
                                 ArrayRef<RTLIB::Libcall> Libcalls);
    void expandAtomicLoadToLibcall(LoadInst *LI);
    void expandAtomicStoreToLibcall(StoreInst *LI);
    void expandAtomicRMWToLibcall(AtomicRMWInst *I);
    void expandAtomicCASToLibcall(AtomicCmpXchgInst *I);

    friend bool
    llvm::expandAtomicRMWToCmpXchg(AtomicRMWInst *AI,
                                   CreateCmpXchgInstFun CreateCmpXchg);
  };

} // end anonymous namespace

char AtomicExpand::ID = 0;

char &llvm::AtomicExpandID = AtomicExpand::ID;

INITIALIZE_PASS(AtomicExpand, DEBUG_TYPE, "Expand Atomic instructions",
                false, false)

FunctionPass *llvm::createAtomicExpandPass() { return new AtomicExpand(); }

// Helper functions to retrieve the size of atomic instructions.
static unsigned getAtomicOpSize(LoadInst *LI) {
  const DataLayout &DL = LI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(LI->getType());
}

static unsigned getAtomicOpSize(StoreInst *SI) {
  const DataLayout &DL = SI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(SI->getValueOperand()->getType());
}

static unsigned getAtomicOpSize(AtomicRMWInst *RMWI) {
  const DataLayout &DL = RMWI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(RMWI->getValOperand()->getType());
}

static unsigned getAtomicOpSize(AtomicCmpXchgInst *CASI) {
  const DataLayout &DL = CASI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(CASI->getCompareOperand()->getType());
}

// Helper functions to retrieve the alignment of atomic instructions.
static unsigned getAtomicOpAlign(LoadInst *LI) {
  unsigned Align = LI->getAlignment();
  // In the future, if this IR restriction is relaxed, we should
  // return DataLayout::getABITypeAlignment when there's no align
  // value.
  assert(Align != 0 && "An atomic LoadInst always has an explicit alignment");
  return Align;
}

static unsigned getAtomicOpAlign(StoreInst *SI) {
  unsigned Align = SI->getAlignment();
  // In the future, if this IR restriction is relaxed, we should
  // return DataLayout::getABITypeAlignment when there's no align
  // value.
  assert(Align != 0 && "An atomic StoreInst always has an explicit alignment");
  return Align;
}

static unsigned getAtomicOpAlign(AtomicRMWInst *RMWI) {
  // TODO(PR27168): This instruction has no alignment attribute, but unlike the
  // default alignment for load/store, the default here is to assume
  // it has NATURAL alignment, not DataLayout-specified alignment.
  const DataLayout &DL = RMWI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(RMWI->getValOperand()->getType());
}

static unsigned getAtomicOpAlign(AtomicCmpXchgInst *CASI) {
  // TODO(PR27168): same comment as above.
  const DataLayout &DL = CASI->getModule()->getDataLayout();
  return DL.getTypeStoreSize(CASI->getCompareOperand()->getType());
}

// Determine if a particular atomic operation has a supported size,
// and is of appropriate alignment, to be passed through for target
// lowering. (Versus turning into a __atomic libcall)
template <typename Inst>
static bool atomicSizeSupported(const TargetLowering *TLI, Inst *I) {
  unsigned Size = getAtomicOpSize(I);
  unsigned Align = getAtomicOpAlign(I);
  return Align >= Size && Size <= TLI->getMaxAtomicSizeInBitsSupported() / 8;
}

bool AtomicExpand::runOnFunction(Function &F) {
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  auto &TM = TPC->getTM<TargetMachine>();
  if (!TM.getSubtargetImpl(F)->enableAtomicExpand())
    return false;
  TLI = TM.getSubtargetImpl(F)->getTargetLowering();

  SmallVector<Instruction *, 1> AtomicInsts;

  // Changing control-flow while iterating through it is a bad idea, so gather a
  // list of all atomic instructions before we start.
  for (inst_iterator II = inst_begin(F), E = inst_end(F); II != E; ++II) {
    Instruction *I = &*II;
    if (I->isAtomic() && !isa<FenceInst>(I))
      AtomicInsts.push_back(I);
  }

  bool MadeChange = false;
  for (auto I : AtomicInsts) {
    auto LI = dyn_cast<LoadInst>(I);
    auto SI = dyn_cast<StoreInst>(I);
    auto RMWI = dyn_cast<AtomicRMWInst>(I);
    auto CASI = dyn_cast<AtomicCmpXchgInst>(I);
    assert((LI || SI || RMWI || CASI) && "Unknown atomic instruction");

    // If the Size/Alignment is not supported, replace with a libcall.
    if (LI) {
      if (!atomicSizeSupported(TLI, LI)) {
        expandAtomicLoadToLibcall(LI);
        MadeChange = true;
        continue;
      }
    } else if (SI) {
      if (!atomicSizeSupported(TLI, SI)) {
        expandAtomicStoreToLibcall(SI);
        MadeChange = true;
        continue;
      }
    } else if (RMWI) {
      if (!atomicSizeSupported(TLI, RMWI)) {
        expandAtomicRMWToLibcall(RMWI);
        MadeChange = true;
        continue;
      }
    } else if (CASI) {
      if (!atomicSizeSupported(TLI, CASI)) {
        expandAtomicCASToLibcall(CASI);
        MadeChange = true;
        continue;
      }
    }

    if (TLI->shouldInsertFencesForAtomic(I)) {
      auto FenceOrdering = AtomicOrdering::Monotonic;
      if (LI && isAcquireOrStronger(LI->getOrdering())) {
        FenceOrdering = LI->getOrdering();
        LI->setOrdering(AtomicOrdering::Monotonic);
      } else if (SI && isReleaseOrStronger(SI->getOrdering())) {
        FenceOrdering = SI->getOrdering();
        SI->setOrdering(AtomicOrdering::Monotonic);
      } else if (RMWI && (isReleaseOrStronger(RMWI->getOrdering()) ||
                          isAcquireOrStronger(RMWI->getOrdering()))) {
        FenceOrdering = RMWI->getOrdering();
        RMWI->setOrdering(AtomicOrdering::Monotonic);
      } else if (CASI &&
                 TLI->shouldExpandAtomicCmpXchgInIR(CASI) ==
                     TargetLoweringBase::AtomicExpansionKind::None &&
                 (isReleaseOrStronger(CASI->getSuccessOrdering()) ||
                  isAcquireOrStronger(CASI->getSuccessOrdering()))) {
        // If a compare and swap is lowered to LL/SC, we can do smarter fence
        // insertion, with a stronger one on the success path than on the
        // failure path. As a result, fence insertion is directly done by
        // expandAtomicCmpXchg in that case.
        FenceOrdering = CASI->getSuccessOrdering();
        CASI->setSuccessOrdering(AtomicOrdering::Monotonic);
        CASI->setFailureOrdering(AtomicOrdering::Monotonic);
      }

      if (FenceOrdering != AtomicOrdering::Monotonic) {
        MadeChange |= bracketInstWithFences(I, FenceOrdering);
      }
    }

    if (LI) {
      if (LI->getType()->isFloatingPointTy()) {
        // TODO: add a TLI hook to control this so that each target can
        // convert to lowering the original type one at a time.
        LI = convertAtomicLoadToIntegerType(LI);
        assert(LI->getType()->isIntegerTy() && "invariant broken");
        MadeChange = true;
      }

      MadeChange |= tryExpandAtomicLoad(LI);
    } else if (SI) {
      if (SI->getValueOperand()->getType()->isFloatingPointTy()) {
        // TODO: add a TLI hook to control this so that each target can
        // convert to lowering the original type one at a time.
        SI = convertAtomicStoreToIntegerType(SI);
        assert(SI->getValueOperand()->getType()->isIntegerTy() &&
               "invariant broken");
        MadeChange = true;
      }

      if (TLI->shouldExpandAtomicStoreInIR(SI))
        MadeChange |= expandAtomicStore(SI);
    } else if (RMWI) {
      // There are two different ways of expanding RMW instructions:
      // - into a load if it is idempotent
      // - into a Cmpxchg/LL-SC loop otherwise
      // we try them in that order.

      if (isIdempotentRMW(RMWI) && simplifyIdempotentRMW(RMWI)) {
        MadeChange = true;
      } else {
        unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
        unsigned ValueSize = getAtomicOpSize(RMWI);
        AtomicRMWInst::BinOp Op = RMWI->getOperation();
        if (ValueSize < MinCASSize &&
            (Op == AtomicRMWInst::Or || Op == AtomicRMWInst::Xor ||
             Op == AtomicRMWInst::And)) {
          RMWI = widenPartwordAtomicRMW(RMWI);
          MadeChange = true;
        }

        MadeChange |= tryExpandAtomicRMW(RMWI);
      }
    } else if (CASI) {
      // TODO: when we're ready to make the change at the IR level, we can
      // extend convertCmpXchgToInteger for floating point too.
      assert(!CASI->getCompareOperand()->getType()->isFloatingPointTy() &&
             "unimplemented - floating point not legal at IR level");
      if (CASI->getCompareOperand()->getType()->isPointerTy() ) {
        // TODO: add a TLI hook to control this so that each target can
        // convert to lowering the original type one at a time.
        CASI = convertCmpXchgToIntegerType(CASI);
        assert(CASI->getCompareOperand()->getType()->isIntegerTy() &&
               "invariant broken");
        MadeChange = true;
      }

      MadeChange |= tryExpandAtomicCmpXchg(CASI);
    }
  }
  return MadeChange;
}

bool AtomicExpand::bracketInstWithFences(Instruction *I, AtomicOrdering Order) {
  IRBuilder<> Builder(I);

  auto LeadingFence = TLI->emitLeadingFence(Builder, I, Order);

  auto TrailingFence = TLI->emitTrailingFence(Builder, I, Order);
  // We have a guard here because not every atomic operation generates a
  // trailing fence.
  if (TrailingFence)
    TrailingFence->moveAfter(I);

  return (LeadingFence || TrailingFence);
}

/// Get the iX type with the same bitwidth as T.
IntegerType *AtomicExpand::getCorrespondingIntegerType(Type *T,
                                                       const DataLayout &DL) {
  EVT VT = TLI->getValueType(DL, T);
  unsigned BitWidth = VT.getStoreSizeInBits();
  assert(BitWidth == VT.getSizeInBits() && "must be a power of two");
  return IntegerType::get(T->getContext(), BitWidth);
}

/// Convert an atomic load of a non-integral type to an integer load of the
/// equivalent bitwidth.  See the function comment on
/// convertAtomicStoreToIntegerType for background.
LoadInst *AtomicExpand::convertAtomicLoadToIntegerType(LoadInst *LI) {
  auto *M = LI->getModule();
  Type *NewTy = getCorrespondingIntegerType(LI->getType(),
                                            M->getDataLayout());

  IRBuilder<> Builder(LI);

  Value *Addr = LI->getPointerOperand();
  Type *PT = PointerType::get(NewTy,
                              Addr->getType()->getPointerAddressSpace());
  Value *NewAddr = Builder.CreateBitCast(Addr, PT);

  auto *NewLI = Builder.CreateLoad(NewAddr);
  NewLI->setAlignment(LI->getAlignment());
  NewLI->setVolatile(LI->isVolatile());
  NewLI->setAtomic(LI->getOrdering(), LI->getSyncScopeID());
  LLVM_DEBUG(dbgs() << "Replaced " << *LI << " with " << *NewLI << "\n");

  Value *NewVal = Builder.CreateBitCast(NewLI, LI->getType());
  LI->replaceAllUsesWith(NewVal);
  LI->eraseFromParent();
  return NewLI;
}

bool AtomicExpand::tryExpandAtomicLoad(LoadInst *LI) {
  switch (TLI->shouldExpandAtomicLoadInIR(LI)) {
  case TargetLoweringBase::AtomicExpansionKind::None:
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC:
    expandAtomicOpToLLSC(
        LI, LI->getType(), LI->getPointerOperand(), LI->getOrdering(),
        [](IRBuilder<> &Builder, Value *Loaded) { return Loaded; });
    return true;
  case TargetLoweringBase::AtomicExpansionKind::LLOnly:
    return expandAtomicLoadToLL(LI);
  case TargetLoweringBase::AtomicExpansionKind::CmpXChg:
    return expandAtomicLoadToCmpXchg(LI);
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicLoad");
  }
}

bool AtomicExpand::expandAtomicLoadToLL(LoadInst *LI) {
  IRBuilder<> Builder(LI);

  // On some architectures, load-linked instructions are atomic for larger
  // sizes than normal loads. For example, the only 64-bit load guaranteed
  // to be single-copy atomic by ARM is an ldrexd (A3.5.3).
  Value *Val =
      TLI->emitLoadLinked(Builder, LI->getPointerOperand(), LI->getOrdering());
  TLI->emitAtomicCmpXchgNoStoreLLBalance(Builder);

  LI->replaceAllUsesWith(Val);
  LI->eraseFromParent();

  return true;
}

bool AtomicExpand::expandAtomicLoadToCmpXchg(LoadInst *LI) {
  IRBuilder<> Builder(LI);
  AtomicOrdering Order = LI->getOrdering();
  Value *Addr = LI->getPointerOperand();
  Type *Ty = cast<PointerType>(Addr->getType())->getElementType();
  Constant *DummyVal = Constant::getNullValue(Ty);

  Value *Pair = Builder.CreateAtomicCmpXchg(
      Addr, DummyVal, DummyVal, Order,
      AtomicCmpXchgInst::getStrongestFailureOrdering(Order));
  Value *Loaded = Builder.CreateExtractValue(Pair, 0, "loaded");

  LI->replaceAllUsesWith(Loaded);
  LI->eraseFromParent();

  return true;
}

/// Convert an atomic store of a non-integral type to an integer store of the
/// equivalent bitwidth.  We used to not support floating point or vector
/// atomics in the IR at all.  The backends learned to deal with the bitcast
/// idiom because that was the only way of expressing the notion of a atomic
/// float or vector store.  The long term plan is to teach each backend to
/// instruction select from the original atomic store, but as a migration
/// mechanism, we convert back to the old format which the backends understand.
/// Each backend will need individual work to recognize the new format.
StoreInst *AtomicExpand::convertAtomicStoreToIntegerType(StoreInst *SI) {
  IRBuilder<> Builder(SI);
  auto *M = SI->getModule();
  Type *NewTy = getCorrespondingIntegerType(SI->getValueOperand()->getType(),
                                            M->getDataLayout());
  Value *NewVal = Builder.CreateBitCast(SI->getValueOperand(), NewTy);

  Value *Addr = SI->getPointerOperand();
  Type *PT = PointerType::get(NewTy,
                              Addr->getType()->getPointerAddressSpace());
  Value *NewAddr = Builder.CreateBitCast(Addr, PT);

  StoreInst *NewSI = Builder.CreateStore(NewVal, NewAddr);
  NewSI->setAlignment(SI->getAlignment());
  NewSI->setVolatile(SI->isVolatile());
  NewSI->setAtomic(SI->getOrdering(), SI->getSyncScopeID());
  LLVM_DEBUG(dbgs() << "Replaced " << *SI << " with " << *NewSI << "\n");
  SI->eraseFromParent();
  return NewSI;
}

bool AtomicExpand::expandAtomicStore(StoreInst *SI) {
  // This function is only called on atomic stores that are too large to be
  // atomic if implemented as a native store. So we replace them by an
  // atomic swap, that can be implemented for example as a ldrex/strex on ARM
  // or lock cmpxchg8/16b on X86, as these are atomic for larger sizes.
  // It is the responsibility of the target to only signal expansion via
  // shouldExpandAtomicRMW in cases where this is required and possible.
  IRBuilder<> Builder(SI);
  AtomicRMWInst *AI =
      Builder.CreateAtomicRMW(AtomicRMWInst::Xchg, SI->getPointerOperand(),
                              SI->getValueOperand(), SI->getOrdering());
  SI->eraseFromParent();

  // Now we have an appropriate swap instruction, lower it as usual.
  return tryExpandAtomicRMW(AI);
}

static void createCmpXchgInstFun(IRBuilder<> &Builder, Value *Addr,
                                 Value *Loaded, Value *NewVal,
                                 AtomicOrdering MemOpOrder,
                                 Value *&Success, Value *&NewLoaded) {
  Value* Pair = Builder.CreateAtomicCmpXchg(
      Addr, Loaded, NewVal, MemOpOrder,
      AtomicCmpXchgInst::getStrongestFailureOrdering(MemOpOrder));
  Success = Builder.CreateExtractValue(Pair, 1, "success");
  NewLoaded = Builder.CreateExtractValue(Pair, 0, "newloaded");
}

/// Emit IR to implement the given atomicrmw operation on values in registers,
/// returning the new value.
static Value *performAtomicOp(AtomicRMWInst::BinOp Op, IRBuilder<> &Builder,
                              Value *Loaded, Value *Inc) {
  Value *NewVal;
  switch (Op) {
  case AtomicRMWInst::Xchg:
    return Inc;
  case AtomicRMWInst::Add:
    return Builder.CreateAdd(Loaded, Inc, "new");
  case AtomicRMWInst::Sub:
    return Builder.CreateSub(Loaded, Inc, "new");
  case AtomicRMWInst::And:
    return Builder.CreateAnd(Loaded, Inc, "new");
  case AtomicRMWInst::Nand:
    return Builder.CreateNot(Builder.CreateAnd(Loaded, Inc), "new");
  case AtomicRMWInst::Or:
    return Builder.CreateOr(Loaded, Inc, "new");
  case AtomicRMWInst::Xor:
    return Builder.CreateXor(Loaded, Inc, "new");
  case AtomicRMWInst::Max:
    NewVal = Builder.CreateICmpSGT(Loaded, Inc);
    return Builder.CreateSelect(NewVal, Loaded, Inc, "new");
  case AtomicRMWInst::Min:
    NewVal = Builder.CreateICmpSLE(Loaded, Inc);
    return Builder.CreateSelect(NewVal, Loaded, Inc, "new");
  case AtomicRMWInst::UMax:
    NewVal = Builder.CreateICmpUGT(Loaded, Inc);
    return Builder.CreateSelect(NewVal, Loaded, Inc, "new");
  case AtomicRMWInst::UMin:
    NewVal = Builder.CreateICmpULE(Loaded, Inc);
    return Builder.CreateSelect(NewVal, Loaded, Inc, "new");
  default:
    llvm_unreachable("Unknown atomic op");
  }
}

bool AtomicExpand::tryExpandAtomicRMW(AtomicRMWInst *AI) {
  switch (TLI->shouldExpandAtomicRMWInIR(AI)) {
  case TargetLoweringBase::AtomicExpansionKind::None:
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC: {
    unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
    unsigned ValueSize = getAtomicOpSize(AI);
    if (ValueSize < MinCASSize) {
      llvm_unreachable(
          "MinCmpXchgSizeInBits not yet supported for LL/SC architectures.");
    } else {
      auto PerformOp = [&](IRBuilder<> &Builder, Value *Loaded) {
        return performAtomicOp(AI->getOperation(), Builder, Loaded,
                               AI->getValOperand());
      };
      expandAtomicOpToLLSC(AI, AI->getType(), AI->getPointerOperand(),
                           AI->getOrdering(), PerformOp);
    }
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::CmpXChg: {
    unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
    unsigned ValueSize = getAtomicOpSize(AI);
    if (ValueSize < MinCASSize) {
      expandPartwordAtomicRMW(AI,
                              TargetLoweringBase::AtomicExpansionKind::CmpXChg);
    } else {
      expandAtomicRMWToCmpXchg(AI, createCmpXchgInstFun);
    }
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::MaskedIntrinsic: {
    expandAtomicRMWToMaskedIntrinsic(AI);
    return true;
  }
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicRMW");
  }
}

namespace {

/// Result values from createMaskInstrs helper.
struct PartwordMaskValues {
  Type *WordType;
  Type *ValueType;
  Value *AlignedAddr;
  Value *ShiftAmt;
  Value *Mask;
  Value *Inv_Mask;
};

} // end anonymous namespace

/// This is a helper function which builds instructions to provide
/// values necessary for partword atomic operations. It takes an
/// incoming address, Addr, and ValueType, and constructs the address,
/// shift-amounts and masks needed to work with a larger value of size
/// WordSize.
///
/// AlignedAddr: Addr rounded down to a multiple of WordSize
///
/// ShiftAmt: Number of bits to right-shift a WordSize value loaded
///           from AlignAddr for it to have the same value as if
///           ValueType was loaded from Addr.
///
/// Mask: Value to mask with the value loaded from AlignAddr to
///       include only the part that would've been loaded from Addr.
///
/// Inv_Mask: The inverse of Mask.
static PartwordMaskValues createMaskInstrs(IRBuilder<> &Builder, Instruction *I,
                                           Type *ValueType, Value *Addr,
                                           unsigned WordSize) {
  PartwordMaskValues Ret;

  BasicBlock *BB = I->getParent();
  Function *F = BB->getParent();
  Module *M = I->getModule();

  LLVMContext &Ctx = F->getContext();
  const DataLayout &DL = M->getDataLayout();

  unsigned ValueSize = DL.getTypeStoreSize(ValueType);

  assert(ValueSize < WordSize);

  Ret.ValueType = ValueType;
  Ret.WordType = Type::getIntNTy(Ctx, WordSize * 8);

  Type *WordPtrType =
      Ret.WordType->getPointerTo(Addr->getType()->getPointerAddressSpace());

  Value *AddrInt = Builder.CreatePtrToInt(Addr, DL.getIntPtrType(Ctx));
  Ret.AlignedAddr = Builder.CreateIntToPtr(
      Builder.CreateAnd(AddrInt, ~(uint64_t)(WordSize - 1)), WordPtrType,
      "AlignedAddr");

  Value *PtrLSB = Builder.CreateAnd(AddrInt, WordSize - 1, "PtrLSB");
  if (DL.isLittleEndian()) {
    // turn bytes into bits
    Ret.ShiftAmt = Builder.CreateShl(PtrLSB, 3);
  } else {
    // turn bytes into bits, and count from the other side.
    Ret.ShiftAmt =
        Builder.CreateShl(Builder.CreateXor(PtrLSB, WordSize - ValueSize), 3);
  }

  Ret.ShiftAmt = Builder.CreateTrunc(Ret.ShiftAmt, Ret.WordType, "ShiftAmt");
  Ret.Mask = Builder.CreateShl(
      ConstantInt::get(Ret.WordType, (1 << ValueSize * 8) - 1), Ret.ShiftAmt,
      "Mask");
  Ret.Inv_Mask = Builder.CreateNot(Ret.Mask, "Inv_Mask");

  return Ret;
}

/// Emit IR to implement a masked version of a given atomicrmw
/// operation. (That is, only the bits under the Mask should be
/// affected by the operation)
static Value *performMaskedAtomicOp(AtomicRMWInst::BinOp Op,
                                    IRBuilder<> &Builder, Value *Loaded,
                                    Value *Shifted_Inc, Value *Inc,
                                    const PartwordMaskValues &PMV) {
  // TODO: update to use
  // https://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge in order
  // to merge bits from two values without requiring PMV.Inv_Mask.
  switch (Op) {
  case AtomicRMWInst::Xchg: {
    Value *Loaded_MaskOut = Builder.CreateAnd(Loaded, PMV.Inv_Mask);
    Value *FinalVal = Builder.CreateOr(Loaded_MaskOut, Shifted_Inc);
    return FinalVal;
  }
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::And:
    llvm_unreachable("Or/Xor/And handled by widenPartwordAtomicRMW");
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Nand: {
    // The other arithmetic ops need to be masked into place.
    Value *NewVal = performAtomicOp(Op, Builder, Loaded, Shifted_Inc);
    Value *NewVal_Masked = Builder.CreateAnd(NewVal, PMV.Mask);
    Value *Loaded_MaskOut = Builder.CreateAnd(Loaded, PMV.Inv_Mask);
    Value *FinalVal = Builder.CreateOr(Loaded_MaskOut, NewVal_Masked);
    return FinalVal;
  }
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin: {
    // Finally, comparison ops will operate on the full value, so
    // truncate down to the original size, and expand out again after
    // doing the operation.
    Value *Loaded_Shiftdown = Builder.CreateTrunc(
        Builder.CreateLShr(Loaded, PMV.ShiftAmt), PMV.ValueType);
    Value *NewVal = performAtomicOp(Op, Builder, Loaded_Shiftdown, Inc);
    Value *NewVal_Shiftup = Builder.CreateShl(
        Builder.CreateZExt(NewVal, PMV.WordType), PMV.ShiftAmt);
    Value *Loaded_MaskOut = Builder.CreateAnd(Loaded, PMV.Inv_Mask);
    Value *FinalVal = Builder.CreateOr(Loaded_MaskOut, NewVal_Shiftup);
    return FinalVal;
  }
  default:
    llvm_unreachable("Unknown atomic op");
  }
}

/// Expand a sub-word atomicrmw operation into an appropriate
/// word-sized operation.
///
/// It will create an LL/SC or cmpxchg loop, as appropriate, the same
/// way as a typical atomicrmw expansion. The only difference here is
/// that the operation inside of the loop must operate only upon a
/// part of the value.
void AtomicExpand::expandPartwordAtomicRMW(
    AtomicRMWInst *AI, TargetLoweringBase::AtomicExpansionKind ExpansionKind) {
  assert(ExpansionKind == TargetLoweringBase::AtomicExpansionKind::CmpXChg);

  AtomicOrdering MemOpOrder = AI->getOrdering();

  IRBuilder<> Builder(AI);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       TLI->getMinCmpXchgSizeInBits() / 8);

  Value *ValOperand_Shifted =
      Builder.CreateShl(Builder.CreateZExt(AI->getValOperand(), PMV.WordType),
                        PMV.ShiftAmt, "ValOperand_Shifted");

  auto PerformPartwordOp = [&](IRBuilder<> &Builder, Value *Loaded) {
    return performMaskedAtomicOp(AI->getOperation(), Builder, Loaded,
                                 ValOperand_Shifted, AI->getValOperand(), PMV);
  };

  // TODO: When we're ready to support LLSC conversions too, use
  // insertRMWLLSCLoop here for ExpansionKind==LLSC.
  Value *OldResult =
      insertRMWCmpXchgLoop(Builder, PMV.WordType, PMV.AlignedAddr, MemOpOrder,
                           PerformPartwordOp, createCmpXchgInstFun);
  Value *FinalOldResult = Builder.CreateTrunc(
      Builder.CreateLShr(OldResult, PMV.ShiftAmt), PMV.ValueType);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
}

// Widen the bitwise atomicrmw (or/xor/and) to the minimum supported width.
AtomicRMWInst *AtomicExpand::widenPartwordAtomicRMW(AtomicRMWInst *AI) {
  IRBuilder<> Builder(AI);
  AtomicRMWInst::BinOp Op = AI->getOperation();

  assert((Op == AtomicRMWInst::Or || Op == AtomicRMWInst::Xor ||
          Op == AtomicRMWInst::And) &&
         "Unable to widen operation");

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       TLI->getMinCmpXchgSizeInBits() / 8);

  Value *ValOperand_Shifted =
      Builder.CreateShl(Builder.CreateZExt(AI->getValOperand(), PMV.WordType),
                        PMV.ShiftAmt, "ValOperand_Shifted");

  Value *NewOperand;

  if (Op == AtomicRMWInst::And)
    NewOperand =
        Builder.CreateOr(PMV.Inv_Mask, ValOperand_Shifted, "AndOperand");
  else
    NewOperand = ValOperand_Shifted;

  AtomicRMWInst *NewAI = Builder.CreateAtomicRMW(Op, PMV.AlignedAddr,
                                                 NewOperand, AI->getOrdering());

  Value *FinalOldResult = Builder.CreateTrunc(
      Builder.CreateLShr(NewAI, PMV.ShiftAmt), PMV.ValueType);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
  return NewAI;
}

void AtomicExpand::expandPartwordCmpXchg(AtomicCmpXchgInst *CI) {
  // The basic idea here is that we're expanding a cmpxchg of a
  // smaller memory size up to a word-sized cmpxchg. To do this, we
  // need to add a retry-loop for strong cmpxchg, so that
  // modifications to other parts of the word don't cause a spurious
  // failure.

  // This generates code like the following:
  //     [[Setup mask values PMV.*]]
  //     %NewVal_Shifted = shl i32 %NewVal, %PMV.ShiftAmt
  //     %Cmp_Shifted = shl i32 %Cmp, %PMV.ShiftAmt
  //     %InitLoaded = load i32* %addr
  //     %InitLoaded_MaskOut = and i32 %InitLoaded, %PMV.Inv_Mask
  //     br partword.cmpxchg.loop
  // partword.cmpxchg.loop:
  //     %Loaded_MaskOut = phi i32 [ %InitLoaded_MaskOut, %entry ],
  //        [ %OldVal_MaskOut, %partword.cmpxchg.failure ]
  //     %FullWord_NewVal = or i32 %Loaded_MaskOut, %NewVal_Shifted
  //     %FullWord_Cmp = or i32 %Loaded_MaskOut, %Cmp_Shifted
  //     %NewCI = cmpxchg i32* %PMV.AlignedAddr, i32 %FullWord_Cmp,
  //        i32 %FullWord_NewVal success_ordering failure_ordering
  //     %OldVal = extractvalue { i32, i1 } %NewCI, 0
  //     %Success = extractvalue { i32, i1 } %NewCI, 1
  //     br i1 %Success, label %partword.cmpxchg.end,
  //        label %partword.cmpxchg.failure
  // partword.cmpxchg.failure:
  //     %OldVal_MaskOut = and i32 %OldVal, %PMV.Inv_Mask
  //     %ShouldContinue = icmp ne i32 %Loaded_MaskOut, %OldVal_MaskOut
  //     br i1 %ShouldContinue, label %partword.cmpxchg.loop,
  //         label %partword.cmpxchg.end
  // partword.cmpxchg.end:
  //    %tmp1 = lshr i32 %OldVal, %PMV.ShiftAmt
  //    %FinalOldVal = trunc i32 %tmp1 to i8
  //    %tmp2 = insertvalue { i8, i1 } undef, i8 %FinalOldVal, 0
  //    %Res = insertvalue { i8, i1 } %25, i1 %Success, 1

  Value *Addr = CI->getPointerOperand();
  Value *Cmp = CI->getCompareOperand();
  Value *NewVal = CI->getNewValOperand();

  BasicBlock *BB = CI->getParent();
  Function *F = BB->getParent();
  IRBuilder<> Builder(CI);
  LLVMContext &Ctx = Builder.getContext();

  const int WordSize = TLI->getMinCmpXchgSizeInBits() / 8;

  BasicBlock *EndBB =
      BB->splitBasicBlock(CI->getIterator(), "partword.cmpxchg.end");
  auto FailureBB =
      BasicBlock::Create(Ctx, "partword.cmpxchg.failure", F, EndBB);
  auto LoopBB = BasicBlock::Create(Ctx, "partword.cmpxchg.loop", F, FailureBB);

  // The split call above "helpfully" added a branch at the end of BB
  // (to the wrong place).
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);

  PartwordMaskValues PMV = createMaskInstrs(
      Builder, CI, CI->getCompareOperand()->getType(), Addr, WordSize);

  // Shift the incoming values over, into the right location in the word.
  Value *NewVal_Shifted =
      Builder.CreateShl(Builder.CreateZExt(NewVal, PMV.WordType), PMV.ShiftAmt);
  Value *Cmp_Shifted =
      Builder.CreateShl(Builder.CreateZExt(Cmp, PMV.WordType), PMV.ShiftAmt);

  // Load the entire current word, and mask into place the expected and new
  // values
  LoadInst *InitLoaded = Builder.CreateLoad(PMV.WordType, PMV.AlignedAddr);
  InitLoaded->setVolatile(CI->isVolatile());
  Value *InitLoaded_MaskOut = Builder.CreateAnd(InitLoaded, PMV.Inv_Mask);
  Builder.CreateBr(LoopBB);

  // partword.cmpxchg.loop:
  Builder.SetInsertPoint(LoopBB);
  PHINode *Loaded_MaskOut = Builder.CreatePHI(PMV.WordType, 2);
  Loaded_MaskOut->addIncoming(InitLoaded_MaskOut, BB);

  // Mask/Or the expected and new values into place in the loaded word.
  Value *FullWord_NewVal = Builder.CreateOr(Loaded_MaskOut, NewVal_Shifted);
  Value *FullWord_Cmp = Builder.CreateOr(Loaded_MaskOut, Cmp_Shifted);
  AtomicCmpXchgInst *NewCI = Builder.CreateAtomicCmpXchg(
      PMV.AlignedAddr, FullWord_Cmp, FullWord_NewVal, CI->getSuccessOrdering(),
      CI->getFailureOrdering(), CI->getSyncScopeID());
  NewCI->setVolatile(CI->isVolatile());
  // When we're building a strong cmpxchg, we need a loop, so you
  // might think we could use a weak cmpxchg inside. But, using strong
  // allows the below comparison for ShouldContinue, and we're
  // expecting the underlying cmpxchg to be a machine instruction,
  // which is strong anyways.
  NewCI->setWeak(CI->isWeak());

  Value *OldVal = Builder.CreateExtractValue(NewCI, 0);
  Value *Success = Builder.CreateExtractValue(NewCI, 1);

  if (CI->isWeak())
    Builder.CreateBr(EndBB);
  else
    Builder.CreateCondBr(Success, EndBB, FailureBB);

  // partword.cmpxchg.failure:
  Builder.SetInsertPoint(FailureBB);
  // Upon failure, verify that the masked-out part of the loaded value
  // has been modified.  If it didn't, abort the cmpxchg, since the
  // masked-in part must've.
  Value *OldVal_MaskOut = Builder.CreateAnd(OldVal, PMV.Inv_Mask);
  Value *ShouldContinue = Builder.CreateICmpNE(Loaded_MaskOut, OldVal_MaskOut);
  Builder.CreateCondBr(ShouldContinue, LoopBB, EndBB);

  // Add the second value to the phi from above
  Loaded_MaskOut->addIncoming(OldVal_MaskOut, FailureBB);

  // partword.cmpxchg.end:
  Builder.SetInsertPoint(CI);

  Value *FinalOldVal = Builder.CreateTrunc(
      Builder.CreateLShr(OldVal, PMV.ShiftAmt), PMV.ValueType);
  Value *Res = UndefValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, FinalOldVal, 0);
  Res = Builder.CreateInsertValue(Res, Success, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
}

void AtomicExpand::expandAtomicOpToLLSC(
    Instruction *I, Type *ResultType, Value *Addr, AtomicOrdering MemOpOrder,
    function_ref<Value *(IRBuilder<> &, Value *)> PerformOp) {
  IRBuilder<> Builder(I);
  Value *Loaded =
      insertRMWLLSCLoop(Builder, ResultType, Addr, MemOpOrder, PerformOp);

  I->replaceAllUsesWith(Loaded);
  I->eraseFromParent();
}

void AtomicExpand::expandAtomicRMWToMaskedIntrinsic(AtomicRMWInst *AI) {
  IRBuilder<> Builder(AI);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       TLI->getMinCmpXchgSizeInBits() / 8);

  // The value operand must be sign-extended for signed min/max so that the
  // target's signed comparison instructions can be used. Otherwise, just
  // zero-ext.
  Instruction::CastOps CastOp = Instruction::ZExt;
  AtomicRMWInst::BinOp RMWOp = AI->getOperation();
  if (RMWOp == AtomicRMWInst::Max || RMWOp == AtomicRMWInst::Min)
    CastOp = Instruction::SExt;

  Value *ValOperand_Shifted = Builder.CreateShl(
      Builder.CreateCast(CastOp, AI->getValOperand(), PMV.WordType),
      PMV.ShiftAmt, "ValOperand_Shifted");
  Value *OldResult = TLI->emitMaskedAtomicRMWIntrinsic(
      Builder, AI, PMV.AlignedAddr, ValOperand_Shifted, PMV.Mask, PMV.ShiftAmt,
      AI->getOrdering());
  Value *FinalOldResult = Builder.CreateTrunc(
      Builder.CreateLShr(OldResult, PMV.ShiftAmt), PMV.ValueType);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
}

void AtomicExpand::expandAtomicCmpXchgToMaskedIntrinsic(AtomicCmpXchgInst *CI) {
  IRBuilder<> Builder(CI);

  PartwordMaskValues PMV = createMaskInstrs(
      Builder, CI, CI->getCompareOperand()->getType(), CI->getPointerOperand(),
      TLI->getMinCmpXchgSizeInBits() / 8);

  Value *CmpVal_Shifted = Builder.CreateShl(
      Builder.CreateZExt(CI->getCompareOperand(), PMV.WordType), PMV.ShiftAmt,
      "CmpVal_Shifted");
  Value *NewVal_Shifted = Builder.CreateShl(
      Builder.CreateZExt(CI->getNewValOperand(), PMV.WordType), PMV.ShiftAmt,
      "NewVal_Shifted");
  Value *OldVal = TLI->emitMaskedAtomicCmpXchgIntrinsic(
      Builder, CI, PMV.AlignedAddr, CmpVal_Shifted, NewVal_Shifted, PMV.Mask,
      CI->getSuccessOrdering());
  Value *FinalOldVal = Builder.CreateTrunc(
      Builder.CreateLShr(OldVal, PMV.ShiftAmt), PMV.ValueType);

  Value *Res = UndefValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, FinalOldVal, 0);
  Value *Success = Builder.CreateICmpEQ(
      CmpVal_Shifted, Builder.CreateAnd(OldVal, PMV.Mask), "Success");
  Res = Builder.CreateInsertValue(Res, Success, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
}

Value *AtomicExpand::insertRMWLLSCLoop(
    IRBuilder<> &Builder, Type *ResultTy, Value *Addr,
    AtomicOrdering MemOpOrder,
    function_ref<Value *(IRBuilder<> &, Value *)> PerformOp) {
  LLVMContext &Ctx = Builder.getContext();
  BasicBlock *BB = Builder.GetInsertBlock();
  Function *F = BB->getParent();

  // Given: atomicrmw some_op iN* %addr, iN %incr ordering
  //
  // The standard expansion we produce is:
  //     [...]
  // atomicrmw.start:
  //     %loaded = @load.linked(%addr)
  //     %new = some_op iN %loaded, %incr
  //     %stored = @store_conditional(%new, %addr)
  //     %try_again = icmp i32 ne %stored, 0
  //     br i1 %try_again, label %loop, label %atomicrmw.end
  // atomicrmw.end:
  //     [...]
  BasicBlock *ExitBB =
      BB->splitBasicBlock(Builder.GetInsertPoint(), "atomicrmw.end");
  BasicBlock *LoopBB =  BasicBlock::Create(Ctx, "atomicrmw.start", F, ExitBB);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place).
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  Builder.CreateBr(LoopBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(LoopBB);
  Value *Loaded = TLI->emitLoadLinked(Builder, Addr, MemOpOrder);

  Value *NewVal = PerformOp(Builder, Loaded);

  Value *StoreSuccess =
      TLI->emitStoreConditional(Builder, NewVal, Addr, MemOpOrder);
  Value *TryAgain = Builder.CreateICmpNE(
      StoreSuccess, ConstantInt::get(IntegerType::get(Ctx, 32), 0), "tryagain");
  Builder.CreateCondBr(TryAgain, LoopBB, ExitBB);

  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  return Loaded;
}

/// Convert an atomic cmpxchg of a non-integral type to an integer cmpxchg of
/// the equivalent bitwidth.  We used to not support pointer cmpxchg in the
/// IR.  As a migration step, we convert back to what use to be the standard
/// way to represent a pointer cmpxchg so that we can update backends one by
/// one.
AtomicCmpXchgInst *AtomicExpand::convertCmpXchgToIntegerType(AtomicCmpXchgInst *CI) {
  auto *M = CI->getModule();
  Type *NewTy = getCorrespondingIntegerType(CI->getCompareOperand()->getType(),
                                            M->getDataLayout());

  IRBuilder<> Builder(CI);

  Value *Addr = CI->getPointerOperand();
  Type *PT = PointerType::get(NewTy,
                              Addr->getType()->getPointerAddressSpace());
  Value *NewAddr = Builder.CreateBitCast(Addr, PT);

  Value *NewCmp = Builder.CreatePtrToInt(CI->getCompareOperand(), NewTy);
  Value *NewNewVal = Builder.CreatePtrToInt(CI->getNewValOperand(), NewTy);


  auto *NewCI = Builder.CreateAtomicCmpXchg(NewAddr, NewCmp, NewNewVal,
                                            CI->getSuccessOrdering(),
                                            CI->getFailureOrdering(),
                                            CI->getSyncScopeID());
  NewCI->setVolatile(CI->isVolatile());
  NewCI->setWeak(CI->isWeak());
  LLVM_DEBUG(dbgs() << "Replaced " << *CI << " with " << *NewCI << "\n");

  Value *OldVal = Builder.CreateExtractValue(NewCI, 0);
  Value *Succ = Builder.CreateExtractValue(NewCI, 1);

  OldVal = Builder.CreateIntToPtr(OldVal, CI->getCompareOperand()->getType());

  Value *Res = UndefValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, OldVal, 0);
  Res = Builder.CreateInsertValue(Res, Succ, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
  return NewCI;
}

bool AtomicExpand::expandAtomicCmpXchg(AtomicCmpXchgInst *CI) {
  AtomicOrdering SuccessOrder = CI->getSuccessOrdering();
  AtomicOrdering FailureOrder = CI->getFailureOrdering();
  Value *Addr = CI->getPointerOperand();
  BasicBlock *BB = CI->getParent();
  Function *F = BB->getParent();
  LLVMContext &Ctx = F->getContext();
  // If shouldInsertFencesForAtomic() returns true, then the target does not
  // want to deal with memory orders, and emitLeading/TrailingFence should take
  // care of everything. Otherwise, emitLeading/TrailingFence are no-op and we
  // should preserve the ordering.
  bool ShouldInsertFencesForAtomic = TLI->shouldInsertFencesForAtomic(CI);
  AtomicOrdering MemOpOrder =
      ShouldInsertFencesForAtomic ? AtomicOrdering::Monotonic : SuccessOrder;

  // In implementations which use a barrier to achieve release semantics, we can
  // delay emitting this barrier until we know a store is actually going to be
  // attempted. The cost of this delay is that we need 2 copies of the block
  // emitting the load-linked, affecting code size.
  //
  // Ideally, this logic would be unconditional except for the minsize check
  // since in other cases the extra blocks naturally collapse down to the
  // minimal loop. Unfortunately, this puts too much stress on later
  // optimisations so we avoid emitting the extra logic in those cases too.
  bool HasReleasedLoadBB = !CI->isWeak() && ShouldInsertFencesForAtomic &&
                           SuccessOrder != AtomicOrdering::Monotonic &&
                           SuccessOrder != AtomicOrdering::Acquire &&
                           !F->optForMinSize();

  // There's no overhead for sinking the release barrier in a weak cmpxchg, so
  // do it even on minsize.
  bool UseUnconditionalReleaseBarrier = F->optForMinSize() && !CI->isWeak();

  // Given: cmpxchg some_op iN* %addr, iN %desired, iN %new success_ord fail_ord
  //
  // The full expansion we produce is:
  //     [...]
  // cmpxchg.start:
  //     %unreleasedload = @load.linked(%addr)
  //     %should_store = icmp eq %unreleasedload, %desired
  //     br i1 %should_store, label %cmpxchg.fencedstore,
  //                          label %cmpxchg.nostore
  // cmpxchg.releasingstore:
  //     fence?
  //     br label cmpxchg.trystore
  // cmpxchg.trystore:
  //     %loaded.trystore = phi [%unreleasedload, %releasingstore],
  //                            [%releasedload, %cmpxchg.releasedload]
  //     %stored = @store_conditional(%new, %addr)
  //     %success = icmp eq i32 %stored, 0
  //     br i1 %success, label %cmpxchg.success,
  //                     label %cmpxchg.releasedload/%cmpxchg.failure
  // cmpxchg.releasedload:
  //     %releasedload = @load.linked(%addr)
  //     %should_store = icmp eq %releasedload, %desired
  //     br i1 %should_store, label %cmpxchg.trystore,
  //                          label %cmpxchg.failure
  // cmpxchg.success:
  //     fence?
  //     br label %cmpxchg.end
  // cmpxchg.nostore:
  //     %loaded.nostore = phi [%unreleasedload, %cmpxchg.start],
  //                           [%releasedload,
  //                               %cmpxchg.releasedload/%cmpxchg.trystore]
  //     @load_linked_fail_balance()?
  //     br label %cmpxchg.failure
  // cmpxchg.failure:
  //     fence?
  //     br label %cmpxchg.end
  // cmpxchg.end:
  //     %loaded = phi [%loaded.nostore, %cmpxchg.failure],
  //                   [%loaded.trystore, %cmpxchg.trystore]
  //     %success = phi i1 [true, %cmpxchg.success], [false, %cmpxchg.failure]
  //     %restmp = insertvalue { iN, i1 } undef, iN %loaded, 0
  //     %res = insertvalue { iN, i1 } %restmp, i1 %success, 1
  //     [...]
  BasicBlock *ExitBB = BB->splitBasicBlock(CI->getIterator(), "cmpxchg.end");
  auto FailureBB = BasicBlock::Create(Ctx, "cmpxchg.failure", F, ExitBB);
  auto NoStoreBB = BasicBlock::Create(Ctx, "cmpxchg.nostore", F, FailureBB);
  auto SuccessBB = BasicBlock::Create(Ctx, "cmpxchg.success", F, NoStoreBB);
  auto ReleasedLoadBB =
      BasicBlock::Create(Ctx, "cmpxchg.releasedload", F, SuccessBB);
  auto TryStoreBB =
      BasicBlock::Create(Ctx, "cmpxchg.trystore", F, ReleasedLoadBB);
  auto ReleasingStoreBB =
      BasicBlock::Create(Ctx, "cmpxchg.fencedstore", F, TryStoreBB);
  auto StartBB = BasicBlock::Create(Ctx, "cmpxchg.start", F, ReleasingStoreBB);

  // This grabs the DebugLoc from CI
  IRBuilder<> Builder(CI);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place), but we might want a fence too. It's easiest to just remove
  // the branch entirely.
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  if (ShouldInsertFencesForAtomic && UseUnconditionalReleaseBarrier)
    TLI->emitLeadingFence(Builder, CI, SuccessOrder);
  Builder.CreateBr(StartBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(StartBB);
  Value *UnreleasedLoad = TLI->emitLoadLinked(Builder, Addr, MemOpOrder);
  Value *ShouldStore = Builder.CreateICmpEQ(
      UnreleasedLoad, CI->getCompareOperand(), "should_store");

  // If the cmpxchg doesn't actually need any ordering when it fails, we can
  // jump straight past that fence instruction (if it exists).
  Builder.CreateCondBr(ShouldStore, ReleasingStoreBB, NoStoreBB);

  Builder.SetInsertPoint(ReleasingStoreBB);
  if (ShouldInsertFencesForAtomic && !UseUnconditionalReleaseBarrier)
    TLI->emitLeadingFence(Builder, CI, SuccessOrder);
  Builder.CreateBr(TryStoreBB);

  Builder.SetInsertPoint(TryStoreBB);
  Value *StoreSuccess = TLI->emitStoreConditional(
      Builder, CI->getNewValOperand(), Addr, MemOpOrder);
  StoreSuccess = Builder.CreateICmpEQ(
      StoreSuccess, ConstantInt::get(Type::getInt32Ty(Ctx), 0), "success");
  BasicBlock *RetryBB = HasReleasedLoadBB ? ReleasedLoadBB : StartBB;
  Builder.CreateCondBr(StoreSuccess, SuccessBB,
                       CI->isWeak() ? FailureBB : RetryBB);

  Builder.SetInsertPoint(ReleasedLoadBB);
  Value *SecondLoad;
  if (HasReleasedLoadBB) {
    SecondLoad = TLI->emitLoadLinked(Builder, Addr, MemOpOrder);
    ShouldStore = Builder.CreateICmpEQ(SecondLoad, CI->getCompareOperand(),
                                       "should_store");

    // If the cmpxchg doesn't actually need any ordering when it fails, we can
    // jump straight past that fence instruction (if it exists).
    Builder.CreateCondBr(ShouldStore, TryStoreBB, NoStoreBB);
  } else
    Builder.CreateUnreachable();

  // Make sure later instructions don't get reordered with a fence if
  // necessary.
  Builder.SetInsertPoint(SuccessBB);
  if (ShouldInsertFencesForAtomic)
    TLI->emitTrailingFence(Builder, CI, SuccessOrder);
  Builder.CreateBr(ExitBB);

  Builder.SetInsertPoint(NoStoreBB);
  // In the failing case, where we don't execute the store-conditional, the
  // target might want to balance out the load-linked with a dedicated
  // instruction (e.g., on ARM, clearing the exclusive monitor).
  TLI->emitAtomicCmpXchgNoStoreLLBalance(Builder);
  Builder.CreateBr(FailureBB);

  Builder.SetInsertPoint(FailureBB);
  if (ShouldInsertFencesForAtomic)
    TLI->emitTrailingFence(Builder, CI, FailureOrder);
  Builder.CreateBr(ExitBB);

  // Finally, we have control-flow based knowledge of whether the cmpxchg
  // succeeded or not. We expose this to later passes by converting any
  // subsequent "icmp eq/ne %loaded, %oldval" into a use of an appropriate
  // PHI.
  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  PHINode *Success = Builder.CreatePHI(Type::getInt1Ty(Ctx), 2);
  Success->addIncoming(ConstantInt::getTrue(Ctx), SuccessBB);
  Success->addIncoming(ConstantInt::getFalse(Ctx), FailureBB);

  // Setup the builder so we can create any PHIs we need.
  Value *Loaded;
  if (!HasReleasedLoadBB)
    Loaded = UnreleasedLoad;
  else {
    Builder.SetInsertPoint(TryStoreBB, TryStoreBB->begin());
    PHINode *TryStoreLoaded = Builder.CreatePHI(UnreleasedLoad->getType(), 2);
    TryStoreLoaded->addIncoming(UnreleasedLoad, ReleasingStoreBB);
    TryStoreLoaded->addIncoming(SecondLoad, ReleasedLoadBB);

    Builder.SetInsertPoint(NoStoreBB, NoStoreBB->begin());
    PHINode *NoStoreLoaded = Builder.CreatePHI(UnreleasedLoad->getType(), 2);
    NoStoreLoaded->addIncoming(UnreleasedLoad, StartBB);
    NoStoreLoaded->addIncoming(SecondLoad, ReleasedLoadBB);

    Builder.SetInsertPoint(ExitBB, ++ExitBB->begin());
    PHINode *ExitLoaded = Builder.CreatePHI(UnreleasedLoad->getType(), 2);
    ExitLoaded->addIncoming(TryStoreLoaded, SuccessBB);
    ExitLoaded->addIncoming(NoStoreLoaded, FailureBB);

    Loaded = ExitLoaded;
  }

  // Look for any users of the cmpxchg that are just comparing the loaded value
  // against the desired one, and replace them with the CFG-derived version.
  SmallVector<ExtractValueInst *, 2> PrunedInsts;
  for (auto User : CI->users()) {
    ExtractValueInst *EV = dyn_cast<ExtractValueInst>(User);
    if (!EV)
      continue;

    assert(EV->getNumIndices() == 1 && EV->getIndices()[0] <= 1 &&
           "weird extraction from { iN, i1 }");

    if (EV->getIndices()[0] == 0)
      EV->replaceAllUsesWith(Loaded);
    else
      EV->replaceAllUsesWith(Success);

    PrunedInsts.push_back(EV);
  }

  // We can remove the instructions now we're no longer iterating through them.
  for (auto EV : PrunedInsts)
    EV->eraseFromParent();

  if (!CI->use_empty()) {
    // Some use of the full struct return that we don't understand has happened,
    // so we've got to reconstruct it properly.
    Value *Res;
    Res = Builder.CreateInsertValue(UndefValue::get(CI->getType()), Loaded, 0);
    Res = Builder.CreateInsertValue(Res, Success, 1);

    CI->replaceAllUsesWith(Res);
  }

  CI->eraseFromParent();
  return true;
}

bool AtomicExpand::isIdempotentRMW(AtomicRMWInst* RMWI) {
  auto C = dyn_cast<ConstantInt>(RMWI->getValOperand());
  if(!C)
    return false;

  AtomicRMWInst::BinOp Op = RMWI->getOperation();
  switch(Op) {
    case AtomicRMWInst::Add:
    case AtomicRMWInst::Sub:
    case AtomicRMWInst::Or:
    case AtomicRMWInst::Xor:
      return C->isZero();
    case AtomicRMWInst::And:
      return C->isMinusOne();
    // FIXME: we could also treat Min/Max/UMin/UMax by the INT_MIN/INT_MAX/...
    default:
      return false;
  }
}

bool AtomicExpand::simplifyIdempotentRMW(AtomicRMWInst* RMWI) {
  if (auto ResultingLoad = TLI->lowerIdempotentRMWIntoFencedLoad(RMWI)) {
    tryExpandAtomicLoad(ResultingLoad);
    return true;
  }
  return false;
}

Value *AtomicExpand::insertRMWCmpXchgLoop(
    IRBuilder<> &Builder, Type *ResultTy, Value *Addr,
    AtomicOrdering MemOpOrder,
    function_ref<Value *(IRBuilder<> &, Value *)> PerformOp,
    CreateCmpXchgInstFun CreateCmpXchg) {
  LLVMContext &Ctx = Builder.getContext();
  BasicBlock *BB = Builder.GetInsertBlock();
  Function *F = BB->getParent();

  // Given: atomicrmw some_op iN* %addr, iN %incr ordering
  //
  // The standard expansion we produce is:
  //     [...]
  //     %init_loaded = load atomic iN* %addr
  //     br label %loop
  // loop:
  //     %loaded = phi iN [ %init_loaded, %entry ], [ %new_loaded, %loop ]
  //     %new = some_op iN %loaded, %incr
  //     %pair = cmpxchg iN* %addr, iN %loaded, iN %new
  //     %new_loaded = extractvalue { iN, i1 } %pair, 0
  //     %success = extractvalue { iN, i1 } %pair, 1
  //     br i1 %success, label %atomicrmw.end, label %loop
  // atomicrmw.end:
  //     [...]
  BasicBlock *ExitBB =
      BB->splitBasicBlock(Builder.GetInsertPoint(), "atomicrmw.end");
  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "atomicrmw.start", F, ExitBB);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place), but we want a load. It's easiest to just remove
  // the branch entirely.
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  LoadInst *InitLoaded = Builder.CreateLoad(ResultTy, Addr);
  // Atomics require at least natural alignment.
  InitLoaded->setAlignment(ResultTy->getPrimitiveSizeInBits() / 8);
  Builder.CreateBr(LoopBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(LoopBB);
  PHINode *Loaded = Builder.CreatePHI(ResultTy, 2, "loaded");
  Loaded->addIncoming(InitLoaded, BB);

  Value *NewVal = PerformOp(Builder, Loaded);

  Value *NewLoaded = nullptr;
  Value *Success = nullptr;

  CreateCmpXchg(Builder, Addr, Loaded, NewVal,
                MemOpOrder == AtomicOrdering::Unordered
                    ? AtomicOrdering::Monotonic
                    : MemOpOrder,
                Success, NewLoaded);
  assert(Success && NewLoaded);

  Loaded->addIncoming(NewLoaded, LoopBB);

  Builder.CreateCondBr(Success, ExitBB, LoopBB);

  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  return NewLoaded;
}

bool AtomicExpand::tryExpandAtomicCmpXchg(AtomicCmpXchgInst *CI) {
  unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
  unsigned ValueSize = getAtomicOpSize(CI);

  switch (TLI->shouldExpandAtomicCmpXchgInIR(CI)) {
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicCmpXchg");
  case TargetLoweringBase::AtomicExpansionKind::None:
    if (ValueSize < MinCASSize)
      expandPartwordCmpXchg(CI);
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC: {
    assert(ValueSize >= MinCASSize &&
           "MinCmpXchgSizeInBits not yet supported for LL/SC expansions.");
    return expandAtomicCmpXchg(CI);
  }
  case TargetLoweringBase::AtomicExpansionKind::MaskedIntrinsic:
    expandAtomicCmpXchgToMaskedIntrinsic(CI);
    return true;
  }
}

// Note: This function is exposed externally by AtomicExpandUtils.h
bool llvm::expandAtomicRMWToCmpXchg(AtomicRMWInst *AI,
                                    CreateCmpXchgInstFun CreateCmpXchg) {
  IRBuilder<> Builder(AI);
  Value *Loaded = AtomicExpand::insertRMWCmpXchgLoop(
      Builder, AI->getType(), AI->getPointerOperand(), AI->getOrdering(),
      [&](IRBuilder<> &Builder, Value *Loaded) {
        return performAtomicOp(AI->getOperation(), Builder, Loaded,
                               AI->getValOperand());
      },
      CreateCmpXchg);

  AI->replaceAllUsesWith(Loaded);
  AI->eraseFromParent();
  return true;
}

// In order to use one of the sized library calls such as
// __atomic_fetch_add_4, the alignment must be sufficient, the size
// must be one of the potentially-specialized sizes, and the value
// type must actually exist in C on the target (otherwise, the
// function wouldn't actually be defined.)
static bool canUseSizedAtomicCall(unsigned Size, unsigned Align,
                                  const DataLayout &DL) {
  // TODO: "LargestSize" is an approximation for "largest type that
  // you can express in C". It seems to be the case that int128 is
  // supported on all 64-bit platforms, otherwise only up to 64-bit
  // integers are supported. If we get this wrong, then we'll try to
  // call a sized libcall that doesn't actually exist. There should
  // really be some more reliable way in LLVM of determining integer
  // sizes which are valid in the target's C ABI...
  unsigned LargestSize = DL.getLargestLegalIntTypeSizeInBits() >= 64 ? 16 : 8;
  return Align >= Size &&
         (Size == 1 || Size == 2 || Size == 4 || Size == 8 || Size == 16) &&
         Size <= LargestSize;
}

void AtomicExpand::expandAtomicLoadToLibcall(LoadInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_LOAD,   RTLIB::ATOMIC_LOAD_1, RTLIB::ATOMIC_LOAD_2,
      RTLIB::ATOMIC_LOAD_4, RTLIB::ATOMIC_LOAD_8, RTLIB::ATOMIC_LOAD_16};
  unsigned Size = getAtomicOpSize(I);
  unsigned Align = getAtomicOpAlign(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, Align, I->getPointerOperand(), nullptr, nullptr,
      I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);
  (void)expanded;
  assert(expanded && "expandAtomicOpToLibcall shouldn't fail tor Load");
}

void AtomicExpand::expandAtomicStoreToLibcall(StoreInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_STORE,   RTLIB::ATOMIC_STORE_1, RTLIB::ATOMIC_STORE_2,
      RTLIB::ATOMIC_STORE_4, RTLIB::ATOMIC_STORE_8, RTLIB::ATOMIC_STORE_16};
  unsigned Size = getAtomicOpSize(I);
  unsigned Align = getAtomicOpAlign(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, Align, I->getPointerOperand(), I->getValueOperand(), nullptr,
      I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);
  (void)expanded;
  assert(expanded && "expandAtomicOpToLibcall shouldn't fail tor Store");
}

void AtomicExpand::expandAtomicCASToLibcall(AtomicCmpXchgInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_COMPARE_EXCHANGE,   RTLIB::ATOMIC_COMPARE_EXCHANGE_1,
      RTLIB::ATOMIC_COMPARE_EXCHANGE_2, RTLIB::ATOMIC_COMPARE_EXCHANGE_4,
      RTLIB::ATOMIC_COMPARE_EXCHANGE_8, RTLIB::ATOMIC_COMPARE_EXCHANGE_16};
  unsigned Size = getAtomicOpSize(I);
  unsigned Align = getAtomicOpAlign(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, Align, I->getPointerOperand(), I->getNewValOperand(),
      I->getCompareOperand(), I->getSuccessOrdering(), I->getFailureOrdering(),
      Libcalls);
  (void)expanded;
  assert(expanded && "expandAtomicOpToLibcall shouldn't fail tor CAS");
}

static ArrayRef<RTLIB::Libcall> GetRMWLibcall(AtomicRMWInst::BinOp Op) {
  static const RTLIB::Libcall LibcallsXchg[6] = {
      RTLIB::ATOMIC_EXCHANGE,   RTLIB::ATOMIC_EXCHANGE_1,
      RTLIB::ATOMIC_EXCHANGE_2, RTLIB::ATOMIC_EXCHANGE_4,
      RTLIB::ATOMIC_EXCHANGE_8, RTLIB::ATOMIC_EXCHANGE_16};
  static const RTLIB::Libcall LibcallsAdd[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_ADD_1,
      RTLIB::ATOMIC_FETCH_ADD_2, RTLIB::ATOMIC_FETCH_ADD_4,
      RTLIB::ATOMIC_FETCH_ADD_8, RTLIB::ATOMIC_FETCH_ADD_16};
  static const RTLIB::Libcall LibcallsSub[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_SUB_1,
      RTLIB::ATOMIC_FETCH_SUB_2, RTLIB::ATOMIC_FETCH_SUB_4,
      RTLIB::ATOMIC_FETCH_SUB_8, RTLIB::ATOMIC_FETCH_SUB_16};
  static const RTLIB::Libcall LibcallsAnd[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_AND_1,
      RTLIB::ATOMIC_FETCH_AND_2, RTLIB::ATOMIC_FETCH_AND_4,
      RTLIB::ATOMIC_FETCH_AND_8, RTLIB::ATOMIC_FETCH_AND_16};
  static const RTLIB::Libcall LibcallsOr[6] = {
      RTLIB::UNKNOWN_LIBCALL,   RTLIB::ATOMIC_FETCH_OR_1,
      RTLIB::ATOMIC_FETCH_OR_2, RTLIB::ATOMIC_FETCH_OR_4,
      RTLIB::ATOMIC_FETCH_OR_8, RTLIB::ATOMIC_FETCH_OR_16};
  static const RTLIB::Libcall LibcallsXor[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_XOR_1,
      RTLIB::ATOMIC_FETCH_XOR_2, RTLIB::ATOMIC_FETCH_XOR_4,
      RTLIB::ATOMIC_FETCH_XOR_8, RTLIB::ATOMIC_FETCH_XOR_16};
  static const RTLIB::Libcall LibcallsNand[6] = {
      RTLIB::UNKNOWN_LIBCALL,     RTLIB::ATOMIC_FETCH_NAND_1,
      RTLIB::ATOMIC_FETCH_NAND_2, RTLIB::ATOMIC_FETCH_NAND_4,
      RTLIB::ATOMIC_FETCH_NAND_8, RTLIB::ATOMIC_FETCH_NAND_16};

  switch (Op) {
  case AtomicRMWInst::BAD_BINOP:
    llvm_unreachable("Should not have BAD_BINOP.");
  case AtomicRMWInst::Xchg:
    return makeArrayRef(LibcallsXchg);
  case AtomicRMWInst::Add:
    return makeArrayRef(LibcallsAdd);
  case AtomicRMWInst::Sub:
    return makeArrayRef(LibcallsSub);
  case AtomicRMWInst::And:
    return makeArrayRef(LibcallsAnd);
  case AtomicRMWInst::Or:
    return makeArrayRef(LibcallsOr);
  case AtomicRMWInst::Xor:
    return makeArrayRef(LibcallsXor);
  case AtomicRMWInst::Nand:
    return makeArrayRef(LibcallsNand);
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
    // No atomic libcalls are available for max/min/umax/umin.
    return {};
  }
  llvm_unreachable("Unexpected AtomicRMW operation.");
}

void AtomicExpand::expandAtomicRMWToLibcall(AtomicRMWInst *I) {
  ArrayRef<RTLIB::Libcall> Libcalls = GetRMWLibcall(I->getOperation());

  unsigned Size = getAtomicOpSize(I);
  unsigned Align = getAtomicOpAlign(I);

  bool Success = false;
  if (!Libcalls.empty())
    Success = expandAtomicOpToLibcall(
        I, Size, Align, I->getPointerOperand(), I->getValOperand(), nullptr,
        I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);

  // The expansion failed: either there were no libcalls at all for
  // the operation (min/max), or there were only size-specialized
  // libcalls (add/sub/etc) and we needed a generic. So, expand to a
  // CAS libcall, via a CAS loop, instead.
  if (!Success) {
    expandAtomicRMWToCmpXchg(I, [this](IRBuilder<> &Builder, Value *Addr,
                                       Value *Loaded, Value *NewVal,
                                       AtomicOrdering MemOpOrder,
                                       Value *&Success, Value *&NewLoaded) {
      // Create the CAS instruction normally...
      AtomicCmpXchgInst *Pair = Builder.CreateAtomicCmpXchg(
          Addr, Loaded, NewVal, MemOpOrder,
          AtomicCmpXchgInst::getStrongestFailureOrdering(MemOpOrder));
      Success = Builder.CreateExtractValue(Pair, 1, "success");
      NewLoaded = Builder.CreateExtractValue(Pair, 0, "newloaded");

      // ...and then expand the CAS into a libcall.
      expandAtomicCASToLibcall(Pair);
    });
  }
}

// A helper routine for the above expandAtomic*ToLibcall functions.
//
// 'Libcalls' contains an array of enum values for the particular
// ATOMIC libcalls to be emitted. All of the other arguments besides
// 'I' are extracted from the Instruction subclass by the
// caller. Depending on the particular call, some will be null.
bool AtomicExpand::expandAtomicOpToLibcall(
    Instruction *I, unsigned Size, unsigned Align, Value *PointerOperand,
    Value *ValueOperand, Value *CASExpected, AtomicOrdering Ordering,
    AtomicOrdering Ordering2, ArrayRef<RTLIB::Libcall> Libcalls) {
  assert(Libcalls.size() == 6);

  LLVMContext &Ctx = I->getContext();
  Module *M = I->getModule();
  const DataLayout &DL = M->getDataLayout();
  IRBuilder<> Builder(I);
  IRBuilder<> AllocaBuilder(&I->getFunction()->getEntryBlock().front());

  bool UseSizedLibcall = canUseSizedAtomicCall(Size, Align, DL);
  Type *SizedIntTy = Type::getIntNTy(Ctx, Size * 8);

  unsigned AllocaAlignment = DL.getPrefTypeAlignment(SizedIntTy);

  // TODO: the "order" argument type is "int", not int32. So
  // getInt32Ty may be wrong if the arch uses e.g. 16-bit ints.
  ConstantInt *SizeVal64 = ConstantInt::get(Type::getInt64Ty(Ctx), Size);
  assert(Ordering != AtomicOrdering::NotAtomic && "expect atomic MO");
  Constant *OrderingVal =
      ConstantInt::get(Type::getInt32Ty(Ctx), (int)toCABI(Ordering));
  Constant *Ordering2Val = nullptr;
  if (CASExpected) {
    assert(Ordering2 != AtomicOrdering::NotAtomic && "expect atomic MO");
    Ordering2Val =
        ConstantInt::get(Type::getInt32Ty(Ctx), (int)toCABI(Ordering2));
  }
  bool HasResult = I->getType() != Type::getVoidTy(Ctx);

  RTLIB::Libcall RTLibType;
  if (UseSizedLibcall) {
    switch (Size) {
    case 1: RTLibType = Libcalls[1]; break;
    case 2: RTLibType = Libcalls[2]; break;
    case 4: RTLibType = Libcalls[3]; break;
    case 8: RTLibType = Libcalls[4]; break;
    case 16: RTLibType = Libcalls[5]; break;
    }
  } else if (Libcalls[0] != RTLIB::UNKNOWN_LIBCALL) {
    RTLibType = Libcalls[0];
  } else {
    // Can't use sized function, and there's no generic for this
    // operation, so give up.
    return false;
  }

  // Build up the function call. There's two kinds. First, the sized
  // variants.  These calls are going to be one of the following (with
  // N=1,2,4,8,16):
  //  iN    __atomic_load_N(iN *ptr, int ordering)
  //  void  __atomic_store_N(iN *ptr, iN val, int ordering)
  //  iN    __atomic_{exchange|fetch_*}_N(iN *ptr, iN val, int ordering)
  //  bool  __atomic_compare_exchange_N(iN *ptr, iN *expected, iN desired,
  //                                    int success_order, int failure_order)
  //
  // Note that these functions can be used for non-integer atomic
  // operations, the values just need to be bitcast to integers on the
  // way in and out.
  //
  // And, then, the generic variants. They look like the following:
  //  void  __atomic_load(size_t size, void *ptr, void *ret, int ordering)
  //  void  __atomic_store(size_t size, void *ptr, void *val, int ordering)
  //  void  __atomic_exchange(size_t size, void *ptr, void *val, void *ret,
  //                          int ordering)
  //  bool  __atomic_compare_exchange(size_t size, void *ptr, void *expected,
  //                                  void *desired, int success_order,
  //                                  int failure_order)
  //
  // The different signatures are built up depending on the
  // 'UseSizedLibcall', 'CASExpected', 'ValueOperand', and 'HasResult'
  // variables.

  AllocaInst *AllocaCASExpected = nullptr;
  Value *AllocaCASExpected_i8 = nullptr;
  AllocaInst *AllocaValue = nullptr;
  Value *AllocaValue_i8 = nullptr;
  AllocaInst *AllocaResult = nullptr;
  Value *AllocaResult_i8 = nullptr;

  Type *ResultTy;
  SmallVector<Value *, 6> Args;
  AttributeList Attr;

  // 'size' argument.
  if (!UseSizedLibcall) {
    // Note, getIntPtrType is assumed equivalent to size_t.
    Args.push_back(ConstantInt::get(DL.getIntPtrType(Ctx), Size));
  }

  // 'ptr' argument.
  Value *PtrVal =
      Builder.CreateBitCast(PointerOperand, Type::getInt8PtrTy(Ctx));
  Args.push_back(PtrVal);

  // 'expected' argument, if present.
  if (CASExpected) {
    AllocaCASExpected = AllocaBuilder.CreateAlloca(CASExpected->getType());
    AllocaCASExpected->setAlignment(AllocaAlignment);
    AllocaCASExpected_i8 =
        Builder.CreateBitCast(AllocaCASExpected, Type::getInt8PtrTy(Ctx));
    Builder.CreateLifetimeStart(AllocaCASExpected_i8, SizeVal64);
    Builder.CreateAlignedStore(CASExpected, AllocaCASExpected, AllocaAlignment);
    Args.push_back(AllocaCASExpected_i8);
  }

  // 'val' argument ('desired' for cas), if present.
  if (ValueOperand) {
    if (UseSizedLibcall) {
      Value *IntValue =
          Builder.CreateBitOrPointerCast(ValueOperand, SizedIntTy);
      Args.push_back(IntValue);
    } else {
      AllocaValue = AllocaBuilder.CreateAlloca(ValueOperand->getType());
      AllocaValue->setAlignment(AllocaAlignment);
      AllocaValue_i8 =
          Builder.CreateBitCast(AllocaValue, Type::getInt8PtrTy(Ctx));
      Builder.CreateLifetimeStart(AllocaValue_i8, SizeVal64);
      Builder.CreateAlignedStore(ValueOperand, AllocaValue, AllocaAlignment);
      Args.push_back(AllocaValue_i8);
    }
  }

  // 'ret' argument.
  if (!CASExpected && HasResult && !UseSizedLibcall) {
    AllocaResult = AllocaBuilder.CreateAlloca(I->getType());
    AllocaResult->setAlignment(AllocaAlignment);
    AllocaResult_i8 =
        Builder.CreateBitCast(AllocaResult, Type::getInt8PtrTy(Ctx));
    Builder.CreateLifetimeStart(AllocaResult_i8, SizeVal64);
    Args.push_back(AllocaResult_i8);
  }

  // 'ordering' ('success_order' for cas) argument.
  Args.push_back(OrderingVal);

  // 'failure_order' argument, if present.
  if (Ordering2Val)
    Args.push_back(Ordering2Val);

  // Now, the return type.
  if (CASExpected) {
    ResultTy = Type::getInt1Ty(Ctx);
    Attr = Attr.addAttribute(Ctx, AttributeList::ReturnIndex, Attribute::ZExt);
  } else if (HasResult && UseSizedLibcall)
    ResultTy = SizedIntTy;
  else
    ResultTy = Type::getVoidTy(Ctx);

  // Done with setting up arguments and return types, create the call:
  SmallVector<Type *, 6> ArgTys;
  for (Value *Arg : Args)
    ArgTys.push_back(Arg->getType());
  FunctionType *FnType = FunctionType::get(ResultTy, ArgTys, false);
  Constant *LibcallFn =
      M->getOrInsertFunction(TLI->getLibcallName(RTLibType), FnType, Attr);
  CallInst *Call = Builder.CreateCall(LibcallFn, Args);
  Call->setAttributes(Attr);
  Value *Result = Call;

  // And then, extract the results...
  if (ValueOperand && !UseSizedLibcall)
    Builder.CreateLifetimeEnd(AllocaValue_i8, SizeVal64);

  if (CASExpected) {
    // The final result from the CAS is {load of 'expected' alloca, bool result
    // from call}
    Type *FinalResultTy = I->getType();
    Value *V = UndefValue::get(FinalResultTy);
    Value *ExpectedOut =
        Builder.CreateAlignedLoad(AllocaCASExpected, AllocaAlignment);
    Builder.CreateLifetimeEnd(AllocaCASExpected_i8, SizeVal64);
    V = Builder.CreateInsertValue(V, ExpectedOut, 0);
    V = Builder.CreateInsertValue(V, Result, 1);
    I->replaceAllUsesWith(V);
  } else if (HasResult) {
    Value *V;
    if (UseSizedLibcall)
      V = Builder.CreateBitOrPointerCast(Result, I->getType());
    else {
      V = Builder.CreateAlignedLoad(AllocaResult, AllocaAlignment);
      Builder.CreateLifetimeEnd(AllocaResult_i8, SizeVal64);
    }
    I->replaceAllUsesWith(V);
  }
  I->eraseFromParent();
  return true;
}
