//===- CoroFrame.cpp - Builds and manipulates coroutine frame -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file contains classes used to discover if for a particular value
// there from sue to definition that crosses a suspend block.
//
// Using the information discovered we form a Coroutine Frame structure to
// contain those values. All uses of those values are replaced with appropriate
// GEP + load from the coroutine frame. At the point of the definition we spill
// the value into the coroutine frame.
//
// TODO: pack values tightly using liveness info.
//===----------------------------------------------------------------------===//

#include "CoroInternal.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/circular_raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

// The "coro-suspend-crossing" flag is very noisy. There is another debug type,
// "coro-frame", which results in leaner debug spew.
#define DEBUG_TYPE "coro-suspend-crossing"

enum { SmallVectorThreshold = 32 };

// Provides two way mapping between the blocks and numbers.
namespace {
class BlockToIndexMapping {
  SmallVector<BasicBlock *, SmallVectorThreshold> V;

public:
  size_t size() const { return V.size(); }

  BlockToIndexMapping(Function &F) {
    for (BasicBlock &BB : F)
      V.push_back(&BB);
    llvm::sort(V);
  }

  size_t blockToIndex(BasicBlock *BB) const {
    auto *I = std::lower_bound(V.begin(), V.end(), BB);
    assert(I != V.end() && *I == BB && "BasicBlockNumberng: Unknown block");
    return I - V.begin();
  }

  BasicBlock *indexToBlock(unsigned Index) const { return V[Index]; }
};
} // end anonymous namespace

// The SuspendCrossingInfo maintains data that allows to answer a question
// whether given two BasicBlocks A and B there is a path from A to B that
// passes through a suspend point.
//
// For every basic block 'i' it maintains a BlockData that consists of:
//   Consumes:  a bit vector which contains a set of indices of blocks that can
//              reach block 'i'
//   Kills: a bit vector which contains a set of indices of blocks that can
//          reach block 'i', but one of the path will cross a suspend point
//   Suspend: a boolean indicating whether block 'i' contains a suspend point.
//   End: a boolean indicating whether block 'i' contains a coro.end intrinsic.
//
namespace {
struct SuspendCrossingInfo {
  BlockToIndexMapping Mapping;

  struct BlockData {
    BitVector Consumes;
    BitVector Kills;
    bool Suspend = false;
    bool End = false;
  };
  SmallVector<BlockData, SmallVectorThreshold> Block;

  iterator_range<succ_iterator> successors(BlockData const &BD) const {
    BasicBlock *BB = Mapping.indexToBlock(&BD - &Block[0]);
    return llvm::successors(BB);
  }

  BlockData &getBlockData(BasicBlock *BB) {
    return Block[Mapping.blockToIndex(BB)];
  }

  void dump() const;
  void dump(StringRef Label, BitVector const &BV) const;

  SuspendCrossingInfo(Function &F, coro::Shape &Shape);

  bool hasPathCrossingSuspendPoint(BasicBlock *DefBB, BasicBlock *UseBB) const {
    size_t const DefIndex = Mapping.blockToIndex(DefBB);
    size_t const UseIndex = Mapping.blockToIndex(UseBB);

    assert(Block[UseIndex].Consumes[DefIndex] && "use must consume def");
    bool const Result = Block[UseIndex].Kills[DefIndex];
    LLVM_DEBUG(dbgs() << UseBB->getName() << " => " << DefBB->getName()
                      << " answer is " << Result << "\n");
    return Result;
  }

  bool isDefinitionAcrossSuspend(BasicBlock *DefBB, User *U) const {
    auto *I = cast<Instruction>(U);

    // We rewrote PHINodes, so that only the ones with exactly one incoming
    // value need to be analyzed.
    if (auto *PN = dyn_cast<PHINode>(I))
      if (PN->getNumIncomingValues() > 1)
        return false;

    BasicBlock *UseBB = I->getParent();
    return hasPathCrossingSuspendPoint(DefBB, UseBB);
  }

  bool isDefinitionAcrossSuspend(Argument &A, User *U) const {
    return isDefinitionAcrossSuspend(&A.getParent()->getEntryBlock(), U);
  }

  bool isDefinitionAcrossSuspend(Instruction &I, User *U) const {
    return isDefinitionAcrossSuspend(I.getParent(), U);
  }
};
} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SuspendCrossingInfo::dump(StringRef Label,
                                                BitVector const &BV) const {
  dbgs() << Label << ":";
  for (size_t I = 0, N = BV.size(); I < N; ++I)
    if (BV[I])
      dbgs() << " " << Mapping.indexToBlock(I)->getName();
  dbgs() << "\n";
}

LLVM_DUMP_METHOD void SuspendCrossingInfo::dump() const {
  for (size_t I = 0, N = Block.size(); I < N; ++I) {
    BasicBlock *const B = Mapping.indexToBlock(I);
    dbgs() << B->getName() << ":\n";
    dump("   Consumes", Block[I].Consumes);
    dump("      Kills", Block[I].Kills);
  }
  dbgs() << "\n";
}
#endif

SuspendCrossingInfo::SuspendCrossingInfo(Function &F, coro::Shape &Shape)
    : Mapping(F) {
  const size_t N = Mapping.size();
  Block.resize(N);

  // Initialize every block so that it consumes itself
  for (size_t I = 0; I < N; ++I) {
    auto &B = Block[I];
    B.Consumes.resize(N);
    B.Kills.resize(N);
    B.Consumes.set(I);
  }

  // Mark all CoroEnd Blocks. We do not propagate Kills beyond coro.ends as
  // the code beyond coro.end is reachable during initial invocation of the
  // coroutine.
  for (auto *CE : Shape.CoroEnds)
    getBlockData(CE->getParent()).End = true;

  // Mark all suspend blocks and indicate that they kill everything they
  // consume. Note, that crossing coro.save also requires a spill, as any code
  // between coro.save and coro.suspend may resume the coroutine and all of the
  // state needs to be saved by that time.
  auto markSuspendBlock = [&](IntrinsicInst *BarrierInst) {
    BasicBlock *SuspendBlock = BarrierInst->getParent();
    auto &B = getBlockData(SuspendBlock);
    B.Suspend = true;
    B.Kills |= B.Consumes;
  };
  for (CoroSuspendInst *CSI : Shape.CoroSuspends) {
    markSuspendBlock(CSI);
    markSuspendBlock(CSI->getCoroSave());
  }

  // Iterate propagating consumes and kills until they stop changing.
  int Iteration = 0;
  (void)Iteration;

  bool Changed;
  do {
    LLVM_DEBUG(dbgs() << "iteration " << ++Iteration);
    LLVM_DEBUG(dbgs() << "==============\n");

    Changed = false;
    for (size_t I = 0; I < N; ++I) {
      auto &B = Block[I];
      for (BasicBlock *SI : successors(B)) {

        auto SuccNo = Mapping.blockToIndex(SI);

        // Saved Consumes and Kills bitsets so that it is easy to see
        // if anything changed after propagation.
        auto &S = Block[SuccNo];
        auto SavedConsumes = S.Consumes;
        auto SavedKills = S.Kills;

        // Propagate Kills and Consumes from block B into its successor S.
        S.Consumes |= B.Consumes;
        S.Kills |= B.Kills;

        // If block B is a suspend block, it should propagate kills into the
        // its successor for every block B consumes.
        if (B.Suspend) {
          S.Kills |= B.Consumes;
        }
        if (S.Suspend) {
          // If block S is a suspend block, it should kill all of the blocks it
          // consumes.
          S.Kills |= S.Consumes;
        } else if (S.End) {
          // If block S is an end block, it should not propagate kills as the
          // blocks following coro.end() are reached during initial invocation
          // of the coroutine while all the data are still available on the
          // stack or in the registers.
          S.Kills.reset();
        } else {
          // This is reached when S block it not Suspend nor coro.end and it
          // need to make sure that it is not in the kill set.
          S.Kills.reset(SuccNo);
        }

        // See if anything changed.
        Changed |= (S.Kills != SavedKills) || (S.Consumes != SavedConsumes);

        if (S.Kills != SavedKills) {
          LLVM_DEBUG(dbgs() << "\nblock " << I << " follower " << SI->getName()
                            << "\n");
          LLVM_DEBUG(dump("S.Kills", S.Kills));
          LLVM_DEBUG(dump("SavedKills", SavedKills));
        }
        if (S.Consumes != SavedConsumes) {
          LLVM_DEBUG(dbgs() << "\nblock " << I << " follower " << SI << "\n");
          LLVM_DEBUG(dump("S.Consume", S.Consumes));
          LLVM_DEBUG(dump("SavedCons", SavedConsumes));
        }
      }
    }
  } while (Changed);
  LLVM_DEBUG(dump());
}

#undef DEBUG_TYPE // "coro-suspend-crossing"
#define DEBUG_TYPE "coro-frame"

// We build up the list of spills for every case where a use is separated
// from the definition by a suspend point.

namespace {
class Spill {
  Value *Def = nullptr;
  Instruction *User = nullptr;
  unsigned FieldNo = 0;

public:
  Spill(Value *Def, llvm::User *U) : Def(Def), User(cast<Instruction>(U)) {}

  Value *def() const { return Def; }
  Instruction *user() const { return User; }
  BasicBlock *userBlock() const { return User->getParent(); }

  // Note that field index is stored in the first SpillEntry for a particular
  // definition. Subsequent mentions of a defintion do not have fieldNo
  // assigned. This works out fine as the users of Spills capture the info about
  // the definition the first time they encounter it. Consider refactoring
  // SpillInfo into two arrays to normalize the spill representation.
  unsigned fieldIndex() const {
    assert(FieldNo && "Accessing unassigned field");
    return FieldNo;
  }
  void setFieldIndex(unsigned FieldNumber) {
    assert(!FieldNo && "Reassigning field number");
    FieldNo = FieldNumber;
  }
};
} // namespace

// Note that there may be more than one record with the same value of Def in
// the SpillInfo vector.
using SpillInfo = SmallVector<Spill, 8>;

#ifndef NDEBUG
static void dump(StringRef Title, SpillInfo const &Spills) {
  dbgs() << "------------- " << Title << "--------------\n";
  Value *CurrentValue = nullptr;
  for (auto const &E : Spills) {
    if (CurrentValue != E.def()) {
      CurrentValue = E.def();
      CurrentValue->dump();
    }
    dbgs() << "   user: ";
    E.user()->dump();
  }
}
#endif

namespace {
// We cannot rely solely on natural alignment of a type when building a
// coroutine frame and if the alignment specified on the Alloca instruction
// differs from the natural alignment of the alloca type we will need to insert
// padding.
struct PaddingCalculator {
  const DataLayout &DL;
  LLVMContext &Context;
  unsigned StructSize = 0;

  PaddingCalculator(LLVMContext &Context, DataLayout const &DL)
      : DL(DL), Context(Context) {}

  // Replicate the logic from IR/DataLayout.cpp to match field offset
  // computation for LLVM structs.
  void addType(Type *Ty) {
    unsigned TyAlign = DL.getABITypeAlignment(Ty);
    if ((StructSize & (TyAlign - 1)) != 0)
      StructSize = alignTo(StructSize, TyAlign);

    StructSize += DL.getTypeAllocSize(Ty); // Consume space for this data item.
  }

  void addTypes(SmallVectorImpl<Type *> const &Types) {
    for (auto *Ty : Types)
      addType(Ty);
  }

  unsigned computePadding(Type *Ty, unsigned ForcedAlignment) {
    unsigned TyAlign = DL.getABITypeAlignment(Ty);
    auto Natural = alignTo(StructSize, TyAlign);
    auto Forced = alignTo(StructSize, ForcedAlignment);

    // Return how many bytes of padding we need to insert.
    if (Natural != Forced)
      return std::max(Natural, Forced) - StructSize;

    // Rely on natural alignment.
    return 0;
  }

  // If padding required, return the padding field type to insert.
  ArrayType *getPaddingType(Type *Ty, unsigned ForcedAlignment) {
    if (auto Padding = computePadding(Ty, ForcedAlignment))
      return ArrayType::get(Type::getInt8Ty(Context), Padding);

    return nullptr;
  }
};
} // namespace

// Build a struct that will keep state for an active coroutine.
//   struct f.frame {
//     ResumeFnTy ResumeFnAddr;
//     ResumeFnTy DestroyFnAddr;
//     int ResumeIndex;
//     ... promise (if present) ...
//     ... spills ...
//   };
static StructType *buildFrameType(Function &F, coro::Shape &Shape,
                                  SpillInfo &Spills) {
  LLVMContext &C = F.getContext();
  const DataLayout &DL = F.getParent()->getDataLayout();
  PaddingCalculator Padder(C, DL);
  SmallString<32> Name(F.getName());
  Name.append(".Frame");
  StructType *FrameTy = StructType::create(C, Name);
  auto *FramePtrTy = FrameTy->getPointerTo();
  auto *FnTy = FunctionType::get(Type::getVoidTy(C), FramePtrTy,
                                 /*IsVarArgs=*/false);
  auto *FnPtrTy = FnTy->getPointerTo();

  // Figure out how wide should be an integer type storing the suspend index.
  unsigned IndexBits = std::max(1U, Log2_64_Ceil(Shape.CoroSuspends.size()));
  Type *PromiseType = Shape.PromiseAlloca
                          ? Shape.PromiseAlloca->getType()->getElementType()
                          : Type::getInt1Ty(C);
  SmallVector<Type *, 8> Types{FnPtrTy, FnPtrTy, PromiseType,
                               Type::getIntNTy(C, IndexBits)};
  Value *CurrentDef = nullptr;

  Padder.addTypes(Types);

  // Create an entry for every spilled value.
  for (auto &S : Spills) {
    if (CurrentDef == S.def())
      continue;

    CurrentDef = S.def();
    // PromiseAlloca was already added to Types array earlier.
    if (CurrentDef == Shape.PromiseAlloca)
      continue;

    Type *Ty = nullptr;
    if (auto *AI = dyn_cast<AllocaInst>(CurrentDef)) {
      Ty = AI->getAllocatedType();
      if (unsigned AllocaAlignment = AI->getAlignment()) {
        // If alignment is specified in alloca, see if we need to insert extra
        // padding.
        if (auto PaddingTy = Padder.getPaddingType(Ty, AllocaAlignment)) {
          Types.push_back(PaddingTy);
          Padder.addType(PaddingTy);
        }
      }
    } else {
      Ty = CurrentDef->getType();
    }
    S.setFieldIndex(Types.size());
    Types.push_back(Ty);
    Padder.addType(Ty);
  }
  FrameTy->setBody(Types);

  return FrameTy;
}

// We need to make room to insert a spill after initial PHIs, but before
// catchswitch instruction. Placing it before violates the requirement that
// catchswitch, like all other EHPads must be the first nonPHI in a block.
//
// Split away catchswitch into a separate block and insert in its place:
//
//   cleanuppad <InsertPt> cleanupret.
//
// cleanupret instruction will act as an insert point for the spill.
static Instruction *splitBeforeCatchSwitch(CatchSwitchInst *CatchSwitch) {
  BasicBlock *CurrentBlock = CatchSwitch->getParent();
  BasicBlock *NewBlock = CurrentBlock->splitBasicBlock(CatchSwitch);
  CurrentBlock->getTerminator()->eraseFromParent();

  auto *CleanupPad =
      CleanupPadInst::Create(CatchSwitch->getParentPad(), {}, "", CurrentBlock);
  auto *CleanupRet =
      CleanupReturnInst::Create(CleanupPad, NewBlock, CurrentBlock);
  return CleanupRet;
}

// Replace all alloca and SSA values that are accessed across suspend points
// with GetElementPointer from coroutine frame + loads and stores. Create an
// AllocaSpillBB that will become the new entry block for the resume parts of
// the coroutine:
//
//    %hdl = coro.begin(...)
//    whatever
//
// becomes:
//
//    %hdl = coro.begin(...)
//    %FramePtr = bitcast i8* hdl to %f.frame*
//    br label %AllocaSpillBB
//
//  AllocaSpillBB:
//    ; geps corresponding to allocas that were moved to coroutine frame
//    br label PostSpill
//
//  PostSpill:
//    whatever
//
//
static Instruction *insertSpills(SpillInfo &Spills, coro::Shape &Shape) {
  auto *CB = Shape.CoroBegin;
  IRBuilder<> Builder(CB->getNextNode());
  PointerType *FramePtrTy = Shape.FrameTy->getPointerTo();
  auto *FramePtr =
      cast<Instruction>(Builder.CreateBitCast(CB, FramePtrTy, "FramePtr"));
  Type *FrameTy = FramePtrTy->getElementType();

  Value *CurrentValue = nullptr;
  BasicBlock *CurrentBlock = nullptr;
  Value *CurrentReload = nullptr;
  unsigned Index = 0; // Proper field number will be read from field definition.

  // We need to keep track of any allocas that need "spilling"
  // since they will live in the coroutine frame now, all access to them
  // need to be changed, not just the access across suspend points
  // we remember allocas and their indices to be handled once we processed
  // all the spills.
  SmallVector<std::pair<AllocaInst *, unsigned>, 4> Allocas;
  // Promise alloca (if present) has a fixed field number (Shape::PromiseField)
  if (Shape.PromiseAlloca)
    Allocas.emplace_back(Shape.PromiseAlloca, coro::Shape::PromiseField);

  // Create a load instruction to reload the spilled value from the coroutine
  // frame.
  auto CreateReload = [&](Instruction *InsertBefore) {
    assert(Index && "accessing unassigned field number");
    Builder.SetInsertPoint(InsertBefore);
    auto *G = Builder.CreateConstInBoundsGEP2_32(FrameTy, FramePtr, 0, Index,
                                                 CurrentValue->getName() +
                                                     Twine(".reload.addr"));
    return isa<AllocaInst>(CurrentValue)
               ? G
               : Builder.CreateLoad(G,
                                    CurrentValue->getName() + Twine(".reload"));
  };

  for (auto const &E : Spills) {
    // If we have not seen the value, generate a spill.
    if (CurrentValue != E.def()) {
      CurrentValue = E.def();
      CurrentBlock = nullptr;
      CurrentReload = nullptr;

      Index = E.fieldIndex();

      if (auto *AI = dyn_cast<AllocaInst>(CurrentValue)) {
        // Spilled AllocaInst will be replaced with GEP from the coroutine frame
        // there is no spill required.
        Allocas.emplace_back(AI, Index);
        if (!AI->isStaticAlloca())
          report_fatal_error("Coroutines cannot handle non static allocas yet");
      } else {
        // Otherwise, create a store instruction storing the value into the
        // coroutine frame.

        Instruction *InsertPt = nullptr;
        if (isa<Argument>(CurrentValue)) {
          // For arguments, we will place the store instruction right after
          // the coroutine frame pointer instruction, i.e. bitcast of
          // coro.begin from i8* to %f.frame*.
          InsertPt = FramePtr->getNextNode();
        } else if (auto *II = dyn_cast<InvokeInst>(CurrentValue)) {
          // If we are spilling the result of the invoke instruction, split the
          // normal edge and insert the spill in the new block.
          auto NewBB = SplitEdge(II->getParent(), II->getNormalDest());
          InsertPt = NewBB->getTerminator();
        } else if (dyn_cast<PHINode>(CurrentValue)) {
          // Skip the PHINodes and EH pads instructions.
          BasicBlock *DefBlock = cast<Instruction>(E.def())->getParent();
          if (auto *CSI = dyn_cast<CatchSwitchInst>(DefBlock->getTerminator()))
            InsertPt = splitBeforeCatchSwitch(CSI);
          else
            InsertPt = &*DefBlock->getFirstInsertionPt();
        } else {
          // For all other values, the spill is placed immediately after
          // the definition.
          assert(!cast<Instruction>(E.def())->isTerminator() &&
                 "unexpected terminator");
          InsertPt = cast<Instruction>(E.def())->getNextNode();
        }

        Builder.SetInsertPoint(InsertPt);
        auto *G = Builder.CreateConstInBoundsGEP2_32(
            FrameTy, FramePtr, 0, Index,
            CurrentValue->getName() + Twine(".spill.addr"));
        Builder.CreateStore(CurrentValue, G);
      }
    }

    // If we have not seen the use block, generate a reload in it.
    if (CurrentBlock != E.userBlock()) {
      CurrentBlock = E.userBlock();
      CurrentReload = CreateReload(&*CurrentBlock->getFirstInsertionPt());
    }

    // If we have a single edge PHINode, remove it and replace it with a reload
    // from the coroutine frame. (We already took care of multi edge PHINodes
    // by rewriting them in the rewritePHIs function).
    if (auto *PN = dyn_cast<PHINode>(E.user())) {
      assert(PN->getNumIncomingValues() == 1 && "unexpected number of incoming "
                                                "values in the PHINode");
      PN->replaceAllUsesWith(CurrentReload);
      PN->eraseFromParent();
      continue;
    }

    // Replace all uses of CurrentValue in the current instruction with reload.
    E.user()->replaceUsesOfWith(CurrentValue, CurrentReload);
  }

  BasicBlock *FramePtrBB = FramePtr->getParent();
  Shape.AllocaSpillBlock =
      FramePtrBB->splitBasicBlock(FramePtr->getNextNode(), "AllocaSpillBB");
  Shape.AllocaSpillBlock->splitBasicBlock(&Shape.AllocaSpillBlock->front(),
                                          "PostSpill");

  Builder.SetInsertPoint(&Shape.AllocaSpillBlock->front());
  // If we found any allocas, replace all of their remaining uses with Geps.
  for (auto &P : Allocas) {
    auto *G =
        Builder.CreateConstInBoundsGEP2_32(FrameTy, FramePtr, 0, P.second);
    // We are not using ReplaceInstWithInst(P.first, cast<Instruction>(G)) here,
    // as we are changing location of the instruction.
    G->takeName(P.first);
    P.first->replaceAllUsesWith(G);
    P.first->eraseFromParent();
  }
  return FramePtr;
}

// Sets the unwind edge of an instruction to a particular successor.
static void setUnwindEdgeTo(Instruction *TI, BasicBlock *Succ) {
  if (auto *II = dyn_cast<InvokeInst>(TI))
    II->setUnwindDest(Succ);
  else if (auto *CS = dyn_cast<CatchSwitchInst>(TI))
    CS->setUnwindDest(Succ);
  else if (auto *CR = dyn_cast<CleanupReturnInst>(TI))
    CR->setUnwindDest(Succ);
  else
    llvm_unreachable("unexpected terminator instruction");
}

// Replaces all uses of OldPred with the NewPred block in all PHINodes in a
// block.
static void updatePhiNodes(BasicBlock *DestBB, BasicBlock *OldPred,
                           BasicBlock *NewPred,
                           PHINode *LandingPadReplacement) {
  unsigned BBIdx = 0;
  for (BasicBlock::iterator I = DestBB->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);

    // We manually update the LandingPadReplacement PHINode and it is the last
    // PHI Node. So, if we find it, we are done.
    if (LandingPadReplacement == PN)
      break;

    // Reuse the previous value of BBIdx if it lines up.  In cases where we
    // have multiple phi nodes with *lots* of predecessors, this is a speed
    // win because we don't have to scan the PHI looking for TIBB.  This
    // happens because the BB list of PHI nodes are usually in the same
    // order.
    if (PN->getIncomingBlock(BBIdx) != OldPred)
      BBIdx = PN->getBasicBlockIndex(OldPred);

    assert(BBIdx != (unsigned)-1 && "Invalid PHI Index!");
    PN->setIncomingBlock(BBIdx, NewPred);
  }
}

// Uses SplitEdge unless the successor block is an EHPad, in which case do EH
// specific handling.
static BasicBlock *ehAwareSplitEdge(BasicBlock *BB, BasicBlock *Succ,
                                    LandingPadInst *OriginalPad,
                                    PHINode *LandingPadReplacement) {
  auto *PadInst = Succ->getFirstNonPHI();
  if (!LandingPadReplacement && !PadInst->isEHPad())
    return SplitEdge(BB, Succ);

  auto *NewBB = BasicBlock::Create(BB->getContext(), "", BB->getParent(), Succ);
  setUnwindEdgeTo(BB->getTerminator(), NewBB);
  updatePhiNodes(Succ, BB, NewBB, LandingPadReplacement);

  if (LandingPadReplacement) {
    auto *NewLP = OriginalPad->clone();
    auto *Terminator = BranchInst::Create(Succ, NewBB);
    NewLP->insertBefore(Terminator);
    LandingPadReplacement->addIncoming(NewLP, NewBB);
    return NewBB;
  }
  Value *ParentPad = nullptr;
  if (auto *FuncletPad = dyn_cast<FuncletPadInst>(PadInst))
    ParentPad = FuncletPad->getParentPad();
  else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(PadInst))
    ParentPad = CatchSwitch->getParentPad();
  else
    llvm_unreachable("handling for other EHPads not implemented yet");

  auto *NewCleanupPad = CleanupPadInst::Create(ParentPad, {}, "", NewBB);
  CleanupReturnInst::Create(NewCleanupPad, Succ, NewBB);
  return NewBB;
}

static void rewritePHIs(BasicBlock &BB) {
  // For every incoming edge we will create a block holding all
  // incoming values in a single PHI nodes.
  //
  // loop:
  //    %n.val = phi i32[%n, %entry], [%inc, %loop]
  //
  // It will create:
  //
  // loop.from.entry:
  //    %n.loop.pre = phi i32 [%n, %entry]
  //    br %label loop
  // loop.from.loop:
  //    %inc.loop.pre = phi i32 [%inc, %loop]
  //    br %label loop
  //
  // After this rewrite, further analysis will ignore any phi nodes with more
  // than one incoming edge.

  // TODO: Simplify PHINodes in the basic block to remove duplicate
  // predecessors.

  LandingPadInst *LandingPad = nullptr;
  PHINode *ReplPHI = nullptr;
  if ((LandingPad = dyn_cast_or_null<LandingPadInst>(BB.getFirstNonPHI()))) {
    // ehAwareSplitEdge will clone the LandingPad in all the edge blocks.
    // We replace the original landing pad with a PHINode that will collect the
    // results from all of them.
    ReplPHI = PHINode::Create(LandingPad->getType(), 1, "", LandingPad);
    ReplPHI->takeName(LandingPad);
    LandingPad->replaceAllUsesWith(ReplPHI);
    // We will erase the original landing pad at the end of this function after
    // ehAwareSplitEdge cloned it in the transition blocks.
  }

  SmallVector<BasicBlock *, 8> Preds(pred_begin(&BB), pred_end(&BB));
  for (BasicBlock *Pred : Preds) {
    auto *IncomingBB = ehAwareSplitEdge(Pred, &BB, LandingPad, ReplPHI);
    IncomingBB->setName(BB.getName() + Twine(".from.") + Pred->getName());
    auto *PN = cast<PHINode>(&BB.front());
    do {
      int Index = PN->getBasicBlockIndex(IncomingBB);
      Value *V = PN->getIncomingValue(Index);
      PHINode *InputV = PHINode::Create(
          V->getType(), 1, V->getName() + Twine(".") + BB.getName(),
          &IncomingBB->front());
      InputV->addIncoming(V, Pred);
      PN->setIncomingValue(Index, InputV);
      PN = dyn_cast<PHINode>(PN->getNextNode());
    } while (PN != ReplPHI); // ReplPHI is either null or the PHI that replaced
                             // the landing pad.
  }

  if (LandingPad) {
    // Calls to ehAwareSplitEdge function cloned the original lading pad.
    // No longer need it.
    LandingPad->eraseFromParent();
  }
}

static void rewritePHIs(Function &F) {
  SmallVector<BasicBlock *, 8> WorkList;

  for (BasicBlock &BB : F)
    if (auto *PN = dyn_cast<PHINode>(&BB.front()))
      if (PN->getNumIncomingValues() > 1)
        WorkList.push_back(&BB);

  for (BasicBlock *BB : WorkList)
    rewritePHIs(*BB);
}

// Check for instructions that we can recreate on resume as opposed to spill
// the result into a coroutine frame.
static bool materializable(Instruction &V) {
  return isa<CastInst>(&V) || isa<GetElementPtrInst>(&V) ||
         isa<BinaryOperator>(&V) || isa<CmpInst>(&V) || isa<SelectInst>(&V);
}

// Check for structural coroutine intrinsics that should not be spilled into
// the coroutine frame.
static bool isCoroutineStructureIntrinsic(Instruction &I) {
  return isa<CoroIdInst>(&I) || isa<CoroSaveInst>(&I) ||
         isa<CoroSuspendInst>(&I);
}

// For every use of the value that is across suspend point, recreate that value
// after a suspend point.
static void rewriteMaterializableInstructions(IRBuilder<> &IRB,
                                              SpillInfo const &Spills) {
  BasicBlock *CurrentBlock = nullptr;
  Instruction *CurrentMaterialization = nullptr;
  Instruction *CurrentDef = nullptr;

  for (auto const &E : Spills) {
    // If it is a new definition, update CurrentXXX variables.
    if (CurrentDef != E.def()) {
      CurrentDef = cast<Instruction>(E.def());
      CurrentBlock = nullptr;
      CurrentMaterialization = nullptr;
    }

    // If we have not seen this block, materialize the value.
    if (CurrentBlock != E.userBlock()) {
      CurrentBlock = E.userBlock();
      CurrentMaterialization = cast<Instruction>(CurrentDef)->clone();
      CurrentMaterialization->setName(CurrentDef->getName());
      CurrentMaterialization->insertBefore(
          &*CurrentBlock->getFirstInsertionPt());
    }

    if (auto *PN = dyn_cast<PHINode>(E.user())) {
      assert(PN->getNumIncomingValues() == 1 && "unexpected number of incoming "
                                                "values in the PHINode");
      PN->replaceAllUsesWith(CurrentMaterialization);
      PN->eraseFromParent();
      continue;
    }

    // Replace all uses of CurrentDef in the current instruction with the
    // CurrentMaterialization for the block.
    E.user()->replaceUsesOfWith(CurrentDef, CurrentMaterialization);
  }
}

// Move early uses of spilled variable after CoroBegin.
// For example, if a parameter had address taken, we may end up with the code
// like:
//        define @f(i32 %n) {
//          %n.addr = alloca i32
//          store %n, %n.addr
//          ...
//          call @coro.begin
//    we need to move the store after coro.begin
static void moveSpillUsesAfterCoroBegin(Function &F, SpillInfo const &Spills,
                                        CoroBeginInst *CoroBegin) {
  DominatorTree DT(F);
  SmallVector<Instruction *, 8> NeedsMoving;

  Value *CurrentValue = nullptr;

  for (auto const &E : Spills) {
    if (CurrentValue == E.def())
      continue;

    CurrentValue = E.def();

    for (User *U : CurrentValue->users()) {
      Instruction *I = cast<Instruction>(U);
      if (!DT.dominates(CoroBegin, I)) {
        LLVM_DEBUG(dbgs() << "will move: " << *I << "\n");

        // TODO: Make this more robust. Currently if we run into a situation
        // where simple instruction move won't work we panic and
        // report_fatal_error.
        for (User *UI : I->users()) {
          if (!DT.dominates(CoroBegin, cast<Instruction>(UI)))
            report_fatal_error("cannot move instruction since its users are not"
                               " dominated by CoroBegin");
        }

        NeedsMoving.push_back(I);
      }
    }
  }

  Instruction *InsertPt = CoroBegin->getNextNode();
  for (Instruction *I : NeedsMoving)
    I->moveBefore(InsertPt);
}

// Splits the block at a particular instruction unless it is the first
// instruction in the block with a single predecessor.
static BasicBlock *splitBlockIfNotFirst(Instruction *I, const Twine &Name) {
  auto *BB = I->getParent();
  if (&BB->front() == I) {
    if (BB->getSinglePredecessor()) {
      BB->setName(Name);
      return BB;
    }
  }
  return BB->splitBasicBlock(I, Name);
}

// Split above and below a particular instruction so that it
// will be all alone by itself in a block.
static void splitAround(Instruction *I, const Twine &Name) {
  splitBlockIfNotFirst(I, Name);
  splitBlockIfNotFirst(I->getNextNode(), "After" + Name);
}

void coro::buildCoroutineFrame(Function &F, Shape &Shape) {
  // Lower coro.dbg.declare to coro.dbg.value, since we are going to rewrite
  // access to local variables.
  LowerDbgDeclare(F);

  Shape.PromiseAlloca = Shape.CoroBegin->getId()->getPromise();
  if (Shape.PromiseAlloca) {
    Shape.CoroBegin->getId()->clearPromise();
  }

  // Make sure that all coro.save, coro.suspend and the fallthrough coro.end
  // intrinsics are in their own blocks to simplify the logic of building up
  // SuspendCrossing data.
  for (CoroSuspendInst *CSI : Shape.CoroSuspends) {
    splitAround(CSI->getCoroSave(), "CoroSave");
    splitAround(CSI, "CoroSuspend");
  }

  // Put CoroEnds into their own blocks.
  for (CoroEndInst *CE : Shape.CoroEnds)
    splitAround(CE, "CoroEnd");

  // Transforms multi-edge PHI Nodes, so that any value feeding into a PHI will
  // never has its definition separated from the PHI by the suspend point.
  rewritePHIs(F);

  // Build suspend crossing info.
  SuspendCrossingInfo Checker(F, Shape);

  IRBuilder<> Builder(F.getContext());
  SpillInfo Spills;

  for (int Repeat = 0; Repeat < 4; ++Repeat) {
    // See if there are materializable instructions across suspend points.
    for (Instruction &I : instructions(F))
      if (materializable(I))
        for (User *U : I.users())
          if (Checker.isDefinitionAcrossSuspend(I, U))
            Spills.emplace_back(&I, U);

    if (Spills.empty())
      break;

    // Rewrite materializable instructions to be materialized at the use point.
    LLVM_DEBUG(dump("Materializations", Spills));
    rewriteMaterializableInstructions(Builder, Spills);
    Spills.clear();
  }

  // Collect the spills for arguments and other not-materializable values.
  for (Argument &A : F.args())
    for (User *U : A.users())
      if (Checker.isDefinitionAcrossSuspend(A, U))
        Spills.emplace_back(&A, U);

  for (Instruction &I : instructions(F)) {
    // Values returned from coroutine structure intrinsics should not be part
    // of the Coroutine Frame.
    if (isCoroutineStructureIntrinsic(I) || &I == Shape.CoroBegin)
      continue;
    // The Coroutine Promise always included into coroutine frame, no need to
    // check for suspend crossing.
    if (Shape.PromiseAlloca == &I)
      continue;

    for (User *U : I.users())
      if (Checker.isDefinitionAcrossSuspend(I, U)) {
        // We cannot spill a token.
        if (I.getType()->isTokenTy())
          report_fatal_error(
              "token definition is separated from the use by a suspend point");
        Spills.emplace_back(&I, U);
      }
  }
  LLVM_DEBUG(dump("Spills", Spills));
  moveSpillUsesAfterCoroBegin(F, Spills, Shape.CoroBegin);
  Shape.FrameTy = buildFrameType(F, Shape, Spills);
  Shape.FramePtr = insertSpills(Spills, Shape);
}
