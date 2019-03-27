//===-- MemorySSAUpdater.cpp - Memory SSA Updater--------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------===//
//
// This file implements the MemorySSAUpdater class.
//
//===----------------------------------------------------------------===//
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include <algorithm>

#define DEBUG_TYPE "memoryssa"
using namespace llvm;

// This is the marker algorithm from "Simple and Efficient Construction of
// Static Single Assignment Form"
// The simple, non-marker algorithm places phi nodes at any join
// Here, we place markers, and only place phi nodes if they end up necessary.
// They are only necessary if they break a cycle (IE we recursively visit
// ourselves again), or we discover, while getting the value of the operands,
// that there are two or more definitions needing to be merged.
// This still will leave non-minimal form in the case of irreducible control
// flow, where phi nodes may be in cycles with themselves, but unnecessary.
MemoryAccess *MemorySSAUpdater::getPreviousDefRecursive(
    BasicBlock *BB,
    DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &CachedPreviousDef) {
  // First, do a cache lookup. Without this cache, certain CFG structures
  // (like a series of if statements) take exponential time to visit.
  auto Cached = CachedPreviousDef.find(BB);
  if (Cached != CachedPreviousDef.end()) {
    return Cached->second;
  }

  if (BasicBlock *Pred = BB->getSinglePredecessor()) {
    // Single predecessor case, just recurse, we can only have one definition.
    MemoryAccess *Result = getPreviousDefFromEnd(Pred, CachedPreviousDef);
    CachedPreviousDef.insert({BB, Result});
    return Result;
  }

  if (VisitedBlocks.count(BB)) {
    // We hit our node again, meaning we had a cycle, we must insert a phi
    // node to break it so we have an operand. The only case this will
    // insert useless phis is if we have irreducible control flow.
    MemoryAccess *Result = MSSA->createMemoryPhi(BB);
    CachedPreviousDef.insert({BB, Result});
    return Result;
  }

  if (VisitedBlocks.insert(BB).second) {
    // Mark us visited so we can detect a cycle
    SmallVector<TrackingVH<MemoryAccess>, 8> PhiOps;

    // Recurse to get the values in our predecessors for placement of a
    // potential phi node. This will insert phi nodes if we cycle in order to
    // break the cycle and have an operand.
    for (auto *Pred : predecessors(BB))
      PhiOps.push_back(getPreviousDefFromEnd(Pred, CachedPreviousDef));

    // Now try to simplify the ops to avoid placing a phi.
    // This may return null if we never created a phi yet, that's okay
    MemoryPhi *Phi = dyn_cast_or_null<MemoryPhi>(MSSA->getMemoryAccess(BB));

    // See if we can avoid the phi by simplifying it.
    auto *Result = tryRemoveTrivialPhi(Phi, PhiOps);
    // If we couldn't simplify, we may have to create a phi
    if (Result == Phi) {
      if (!Phi)
        Phi = MSSA->createMemoryPhi(BB);

      // See if the existing phi operands match what we need.
      // Unlike normal SSA, we only allow one phi node per block, so we can't just
      // create a new one.
      if (Phi->getNumOperands() != 0) {
        // FIXME: Figure out whether this is dead code and if so remove it.
        if (!std::equal(Phi->op_begin(), Phi->op_end(), PhiOps.begin())) {
          // These will have been filled in by the recursive read we did above.
          llvm::copy(PhiOps, Phi->op_begin());
          std::copy(pred_begin(BB), pred_end(BB), Phi->block_begin());
        }
      } else {
        unsigned i = 0;
        for (auto *Pred : predecessors(BB))
          Phi->addIncoming(&*PhiOps[i++], Pred);
        InsertedPHIs.push_back(Phi);
      }
      Result = Phi;
    }

    // Set ourselves up for the next variable by resetting visited state.
    VisitedBlocks.erase(BB);
    CachedPreviousDef.insert({BB, Result});
    return Result;
  }
  llvm_unreachable("Should have hit one of the three cases above");
}

// This starts at the memory access, and goes backwards in the block to find the
// previous definition. If a definition is not found the block of the access,
// it continues globally, creating phi nodes to ensure we have a single
// definition.
MemoryAccess *MemorySSAUpdater::getPreviousDef(MemoryAccess *MA) {
  if (auto *LocalResult = getPreviousDefInBlock(MA))
    return LocalResult;
  DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> CachedPreviousDef;
  return getPreviousDefRecursive(MA->getBlock(), CachedPreviousDef);
}

// This starts at the memory access, and goes backwards in the block to the find
// the previous definition. If the definition is not found in the block of the
// access, it returns nullptr.
MemoryAccess *MemorySSAUpdater::getPreviousDefInBlock(MemoryAccess *MA) {
  auto *Defs = MSSA->getWritableBlockDefs(MA->getBlock());

  // It's possible there are no defs, or we got handed the first def to start.
  if (Defs) {
    // If this is a def, we can just use the def iterators.
    if (!isa<MemoryUse>(MA)) {
      auto Iter = MA->getReverseDefsIterator();
      ++Iter;
      if (Iter != Defs->rend())
        return &*Iter;
    } else {
      // Otherwise, have to walk the all access iterator.
      auto End = MSSA->getWritableBlockAccesses(MA->getBlock())->rend();
      for (auto &U : make_range(++MA->getReverseIterator(), End))
        if (!isa<MemoryUse>(U))
          return cast<MemoryAccess>(&U);
      // Note that if MA comes before Defs->begin(), we won't hit a def.
      return nullptr;
    }
  }
  return nullptr;
}

// This starts at the end of block
MemoryAccess *MemorySSAUpdater::getPreviousDefFromEnd(
    BasicBlock *BB,
    DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &CachedPreviousDef) {
  auto *Defs = MSSA->getWritableBlockDefs(BB);

  if (Defs)
    return &*Defs->rbegin();

  return getPreviousDefRecursive(BB, CachedPreviousDef);
}
// Recurse over a set of phi uses to eliminate the trivial ones
MemoryAccess *MemorySSAUpdater::recursePhi(MemoryAccess *Phi) {
  if (!Phi)
    return nullptr;
  TrackingVH<MemoryAccess> Res(Phi);
  SmallVector<TrackingVH<Value>, 8> Uses;
  std::copy(Phi->user_begin(), Phi->user_end(), std::back_inserter(Uses));
  for (auto &U : Uses) {
    if (MemoryPhi *UsePhi = dyn_cast<MemoryPhi>(&*U)) {
      auto OperRange = UsePhi->operands();
      tryRemoveTrivialPhi(UsePhi, OperRange);
    }
  }
  return Res;
}

// Eliminate trivial phis
// Phis are trivial if they are defined either by themselves, or all the same
// argument.
// IE phi(a, a) or b = phi(a, b) or c = phi(a, a, c)
// We recursively try to remove them.
template <class RangeType>
MemoryAccess *MemorySSAUpdater::tryRemoveTrivialPhi(MemoryPhi *Phi,
                                                    RangeType &Operands) {
  // Bail out on non-opt Phis.
  if (NonOptPhis.count(Phi))
    return Phi;

  // Detect equal or self arguments
  MemoryAccess *Same = nullptr;
  for (auto &Op : Operands) {
    // If the same or self, good so far
    if (Op == Phi || Op == Same)
      continue;
    // not the same, return the phi since it's not eliminatable by us
    if (Same)
      return Phi;
    Same = cast<MemoryAccess>(&*Op);
  }
  // Never found a non-self reference, the phi is undef
  if (Same == nullptr)
    return MSSA->getLiveOnEntryDef();
  if (Phi) {
    Phi->replaceAllUsesWith(Same);
    removeMemoryAccess(Phi);
  }

  // We should only end up recursing in case we replaced something, in which
  // case, we may have made other Phis trivial.
  return recursePhi(Same);
}

void MemorySSAUpdater::insertUse(MemoryUse *MU) {
  InsertedPHIs.clear();
  MU->setDefiningAccess(getPreviousDef(MU));
  // Unlike for defs, there is no extra work to do.  Because uses do not create
  // new may-defs, there are only two cases:
  //
  // 1. There was a def already below us, and therefore, we should not have
  // created a phi node because it was already needed for the def.
  //
  // 2. There is no def below us, and therefore, there is no extra renaming work
  // to do.
}

// Set every incoming edge {BB, MP->getBlock()} of MemoryPhi MP to NewDef.
static void setMemoryPhiValueForBlock(MemoryPhi *MP, const BasicBlock *BB,
                                      MemoryAccess *NewDef) {
  // Replace any operand with us an incoming block with the new defining
  // access.
  int i = MP->getBasicBlockIndex(BB);
  assert(i != -1 && "Should have found the basic block in the phi");
  // We can't just compare i against getNumOperands since one is signed and the
  // other not. So use it to index into the block iterator.
  for (auto BBIter = MP->block_begin() + i; BBIter != MP->block_end();
       ++BBIter) {
    if (*BBIter != BB)
      break;
    MP->setIncomingValue(i, NewDef);
    ++i;
  }
}

// A brief description of the algorithm:
// First, we compute what should define the new def, using the SSA
// construction algorithm.
// Then, we update the defs below us (and any new phi nodes) in the graph to
// point to the correct new defs, to ensure we only have one variable, and no
// disconnected stores.
void MemorySSAUpdater::insertDef(MemoryDef *MD, bool RenameUses) {
  InsertedPHIs.clear();

  // See if we had a local def, and if not, go hunting.
  MemoryAccess *DefBefore = getPreviousDef(MD);
  bool DefBeforeSameBlock = DefBefore->getBlock() == MD->getBlock();

  // There is a def before us, which means we can replace any store/phi uses
  // of that thing with us, since we are in the way of whatever was there
  // before.
  // We now define that def's memorydefs and memoryphis
  if (DefBeforeSameBlock) {
    for (auto UI = DefBefore->use_begin(), UE = DefBefore->use_end();
         UI != UE;) {
      Use &U = *UI++;
      // Leave the MemoryUses alone.
      // Also make sure we skip ourselves to avoid self references.
      if (isa<MemoryUse>(U.getUser()) || U.getUser() == MD)
        continue;
      U.set(MD);
    }
  }

  // and that def is now our defining access.
  MD->setDefiningAccess(DefBefore);

  SmallVector<WeakVH, 8> FixupList(InsertedPHIs.begin(), InsertedPHIs.end());
  if (!DefBeforeSameBlock) {
    // If there was a local def before us, we must have the same effect it
    // did. Because every may-def is the same, any phis/etc we would create, it
    // would also have created.  If there was no local def before us, we
    // performed a global update, and have to search all successors and make
    // sure we update the first def in each of them (following all paths until
    // we hit the first def along each path). This may also insert phi nodes.
    // TODO: There are other cases we can skip this work, such as when we have a
    // single successor, and only used a straight line of single pred blocks
    // backwards to find the def.  To make that work, we'd have to track whether
    // getDefRecursive only ever used the single predecessor case.  These types
    // of paths also only exist in between CFG simplifications.
    FixupList.push_back(MD);
  }

  while (!FixupList.empty()) {
    unsigned StartingPHISize = InsertedPHIs.size();
    fixupDefs(FixupList);
    FixupList.clear();
    // Put any new phis on the fixup list, and process them
    FixupList.append(InsertedPHIs.begin() + StartingPHISize, InsertedPHIs.end());
  }
  // Now that all fixups are done, rename all uses if we are asked.
  if (RenameUses) {
    SmallPtrSet<BasicBlock *, 16> Visited;
    BasicBlock *StartBlock = MD->getBlock();
    // We are guaranteed there is a def in the block, because we just got it
    // handed to us in this function.
    MemoryAccess *FirstDef = &*MSSA->getWritableBlockDefs(StartBlock)->begin();
    // Convert to incoming value if it's a memorydef. A phi *is* already an
    // incoming value.
    if (auto *MD = dyn_cast<MemoryDef>(FirstDef))
      FirstDef = MD->getDefiningAccess();

    MSSA->renamePass(MD->getBlock(), FirstDef, Visited);
    // We just inserted a phi into this block, so the incoming value will become
    // the phi anyway, so it does not matter what we pass.
    for (auto &MP : InsertedPHIs) {
      MemoryPhi *Phi = dyn_cast_or_null<MemoryPhi>(MP);
      if (Phi)
        MSSA->renamePass(Phi->getBlock(), nullptr, Visited);
    }
  }
}

void MemorySSAUpdater::fixupDefs(const SmallVectorImpl<WeakVH> &Vars) {
  SmallPtrSet<const BasicBlock *, 8> Seen;
  SmallVector<const BasicBlock *, 16> Worklist;
  for (auto &Var : Vars) {
    MemoryAccess *NewDef = dyn_cast_or_null<MemoryAccess>(Var);
    if (!NewDef)
      continue;
    // First, see if there is a local def after the operand.
    auto *Defs = MSSA->getWritableBlockDefs(NewDef->getBlock());
    auto DefIter = NewDef->getDefsIterator();

    // The temporary Phi is being fixed, unmark it for not to optimize.
    if (MemoryPhi *Phi = dyn_cast<MemoryPhi>(NewDef))
      NonOptPhis.erase(Phi);

    // If there is a local def after us, we only have to rename that.
    if (++DefIter != Defs->end()) {
      cast<MemoryDef>(DefIter)->setDefiningAccess(NewDef);
      continue;
    }

    // Otherwise, we need to search down through the CFG.
    // For each of our successors, handle it directly if their is a phi, or
    // place on the fixup worklist.
    for (const auto *S : successors(NewDef->getBlock())) {
      if (auto *MP = MSSA->getMemoryAccess(S))
        setMemoryPhiValueForBlock(MP, NewDef->getBlock(), NewDef);
      else
        Worklist.push_back(S);
    }

    while (!Worklist.empty()) {
      const BasicBlock *FixupBlock = Worklist.back();
      Worklist.pop_back();

      // Get the first def in the block that isn't a phi node.
      if (auto *Defs = MSSA->getWritableBlockDefs(FixupBlock)) {
        auto *FirstDef = &*Defs->begin();
        // The loop above and below should have taken care of phi nodes
        assert(!isa<MemoryPhi>(FirstDef) &&
               "Should have already handled phi nodes!");
        // We are now this def's defining access, make sure we actually dominate
        // it
        assert(MSSA->dominates(NewDef, FirstDef) &&
               "Should have dominated the new access");

        // This may insert new phi nodes, because we are not guaranteed the
        // block we are processing has a single pred, and depending where the
        // store was inserted, it may require phi nodes below it.
        cast<MemoryDef>(FirstDef)->setDefiningAccess(getPreviousDef(FirstDef));
        return;
      }
      // We didn't find a def, so we must continue.
      for (const auto *S : successors(FixupBlock)) {
        // If there is a phi node, handle it.
        // Otherwise, put the block on the worklist
        if (auto *MP = MSSA->getMemoryAccess(S))
          setMemoryPhiValueForBlock(MP, FixupBlock, NewDef);
        else {
          // If we cycle, we should have ended up at a phi node that we already
          // processed.  FIXME: Double check this
          if (!Seen.insert(S).second)
            continue;
          Worklist.push_back(S);
        }
      }
    }
  }
}

void MemorySSAUpdater::removeEdge(BasicBlock *From, BasicBlock *To) {
  if (MemoryPhi *MPhi = MSSA->getMemoryAccess(To)) {
    MPhi->unorderedDeleteIncomingBlock(From);
    if (MPhi->getNumIncomingValues() == 1)
      removeMemoryAccess(MPhi);
  }
}

void MemorySSAUpdater::removeDuplicatePhiEdgesBetween(BasicBlock *From,
                                                      BasicBlock *To) {
  if (MemoryPhi *MPhi = MSSA->getMemoryAccess(To)) {
    bool Found = false;
    MPhi->unorderedDeleteIncomingIf([&](const MemoryAccess *, BasicBlock *B) {
      if (From != B)
        return false;
      if (Found)
        return true;
      Found = true;
      return false;
    });
    if (MPhi->getNumIncomingValues() == 1)
      removeMemoryAccess(MPhi);
  }
}

void MemorySSAUpdater::cloneUsesAndDefs(BasicBlock *BB, BasicBlock *NewBB,
                                        const ValueToValueMapTy &VMap,
                                        PhiToDefMap &MPhiMap) {
  auto GetNewDefiningAccess = [&](MemoryAccess *MA) -> MemoryAccess * {
    MemoryAccess *InsnDefining = MA;
    if (MemoryUseOrDef *DefMUD = dyn_cast<MemoryUseOrDef>(InsnDefining)) {
      if (!MSSA->isLiveOnEntryDef(DefMUD)) {
        Instruction *DefMUDI = DefMUD->getMemoryInst();
        assert(DefMUDI && "Found MemoryUseOrDef with no Instruction.");
        if (Instruction *NewDefMUDI =
                cast_or_null<Instruction>(VMap.lookup(DefMUDI)))
          InsnDefining = MSSA->getMemoryAccess(NewDefMUDI);
      }
    } else {
      MemoryPhi *DefPhi = cast<MemoryPhi>(InsnDefining);
      if (MemoryAccess *NewDefPhi = MPhiMap.lookup(DefPhi))
        InsnDefining = NewDefPhi;
    }
    assert(InsnDefining && "Defining instruction cannot be nullptr.");
    return InsnDefining;
  };

  const MemorySSA::AccessList *Acc = MSSA->getBlockAccesses(BB);
  if (!Acc)
    return;
  for (const MemoryAccess &MA : *Acc) {
    if (const MemoryUseOrDef *MUD = dyn_cast<MemoryUseOrDef>(&MA)) {
      Instruction *Insn = MUD->getMemoryInst();
      // Entry does not exist if the clone of the block did not clone all
      // instructions. This occurs in LoopRotate when cloning instructions
      // from the old header to the old preheader. The cloned instruction may
      // also be a simplified Value, not an Instruction (see LoopRotate).
      if (Instruction *NewInsn =
              dyn_cast_or_null<Instruction>(VMap.lookup(Insn))) {
        MemoryAccess *NewUseOrDef = MSSA->createDefinedAccess(
            NewInsn, GetNewDefiningAccess(MUD->getDefiningAccess()), MUD);
        MSSA->insertIntoListsForBlock(NewUseOrDef, NewBB, MemorySSA::End);
      }
    }
  }
}

void MemorySSAUpdater::updateForClonedLoop(const LoopBlocksRPO &LoopBlocks,
                                           ArrayRef<BasicBlock *> ExitBlocks,
                                           const ValueToValueMapTy &VMap,
                                           bool IgnoreIncomingWithNoClones) {
  PhiToDefMap MPhiMap;

  auto FixPhiIncomingValues = [&](MemoryPhi *Phi, MemoryPhi *NewPhi) {
    assert(Phi && NewPhi && "Invalid Phi nodes.");
    BasicBlock *NewPhiBB = NewPhi->getBlock();
    SmallPtrSet<BasicBlock *, 4> NewPhiBBPreds(pred_begin(NewPhiBB),
                                               pred_end(NewPhiBB));
    for (unsigned It = 0, E = Phi->getNumIncomingValues(); It < E; ++It) {
      MemoryAccess *IncomingAccess = Phi->getIncomingValue(It);
      BasicBlock *IncBB = Phi->getIncomingBlock(It);

      if (BasicBlock *NewIncBB = cast_or_null<BasicBlock>(VMap.lookup(IncBB)))
        IncBB = NewIncBB;
      else if (IgnoreIncomingWithNoClones)
        continue;

      // Now we have IncBB, and will need to add incoming from it to NewPhi.

      // If IncBB is not a predecessor of NewPhiBB, then do not add it.
      // NewPhiBB was cloned without that edge.
      if (!NewPhiBBPreds.count(IncBB))
        continue;

      // Determine incoming value and add it as incoming from IncBB.
      if (MemoryUseOrDef *IncMUD = dyn_cast<MemoryUseOrDef>(IncomingAccess)) {
        if (!MSSA->isLiveOnEntryDef(IncMUD)) {
          Instruction *IncI = IncMUD->getMemoryInst();
          assert(IncI && "Found MemoryUseOrDef with no Instruction.");
          if (Instruction *NewIncI =
                  cast_or_null<Instruction>(VMap.lookup(IncI))) {
            IncMUD = MSSA->getMemoryAccess(NewIncI);
            assert(IncMUD &&
                   "MemoryUseOrDef cannot be null, all preds processed.");
          }
        }
        NewPhi->addIncoming(IncMUD, IncBB);
      } else {
        MemoryPhi *IncPhi = cast<MemoryPhi>(IncomingAccess);
        if (MemoryAccess *NewDefPhi = MPhiMap.lookup(IncPhi))
          NewPhi->addIncoming(NewDefPhi, IncBB);
        else
          NewPhi->addIncoming(IncPhi, IncBB);
      }
    }
  };

  auto ProcessBlock = [&](BasicBlock *BB) {
    BasicBlock *NewBlock = cast_or_null<BasicBlock>(VMap.lookup(BB));
    if (!NewBlock)
      return;

    assert(!MSSA->getWritableBlockAccesses(NewBlock) &&
           "Cloned block should have no accesses");

    // Add MemoryPhi.
    if (MemoryPhi *MPhi = MSSA->getMemoryAccess(BB)) {
      MemoryPhi *NewPhi = MSSA->createMemoryPhi(NewBlock);
      MPhiMap[MPhi] = NewPhi;
    }
    // Update Uses and Defs.
    cloneUsesAndDefs(BB, NewBlock, VMap, MPhiMap);
  };

  for (auto BB : llvm::concat<BasicBlock *const>(LoopBlocks, ExitBlocks))
    ProcessBlock(BB);

  for (auto BB : llvm::concat<BasicBlock *const>(LoopBlocks, ExitBlocks))
    if (MemoryPhi *MPhi = MSSA->getMemoryAccess(BB))
      if (MemoryAccess *NewPhi = MPhiMap.lookup(MPhi))
        FixPhiIncomingValues(MPhi, cast<MemoryPhi>(NewPhi));
}

void MemorySSAUpdater::updateForClonedBlockIntoPred(
    BasicBlock *BB, BasicBlock *P1, const ValueToValueMapTy &VM) {
  // All defs/phis from outside BB that are used in BB, are valid uses in P1.
  // Since those defs/phis must have dominated BB, and also dominate P1.
  // Defs from BB being used in BB will be replaced with the cloned defs from
  // VM. The uses of BB's Phi (if it exists) in BB will be replaced by the
  // incoming def into the Phi from P1.
  PhiToDefMap MPhiMap;
  if (MemoryPhi *MPhi = MSSA->getMemoryAccess(BB))
    MPhiMap[MPhi] = MPhi->getIncomingValueForBlock(P1);
  cloneUsesAndDefs(BB, P1, VM, MPhiMap);
}

template <typename Iter>
void MemorySSAUpdater::privateUpdateExitBlocksForClonedLoop(
    ArrayRef<BasicBlock *> ExitBlocks, Iter ValuesBegin, Iter ValuesEnd,
    DominatorTree &DT) {
  SmallVector<CFGUpdate, 4> Updates;
  // Update/insert phis in all successors of exit blocks.
  for (auto *Exit : ExitBlocks)
    for (const ValueToValueMapTy *VMap : make_range(ValuesBegin, ValuesEnd))
      if (BasicBlock *NewExit = cast_or_null<BasicBlock>(VMap->lookup(Exit))) {
        BasicBlock *ExitSucc = NewExit->getTerminator()->getSuccessor(0);
        Updates.push_back({DT.Insert, NewExit, ExitSucc});
      }
  applyInsertUpdates(Updates, DT);
}

void MemorySSAUpdater::updateExitBlocksForClonedLoop(
    ArrayRef<BasicBlock *> ExitBlocks, const ValueToValueMapTy &VMap,
    DominatorTree &DT) {
  const ValueToValueMapTy *const Arr[] = {&VMap};
  privateUpdateExitBlocksForClonedLoop(ExitBlocks, std::begin(Arr),
                                       std::end(Arr), DT);
}

void MemorySSAUpdater::updateExitBlocksForClonedLoop(
    ArrayRef<BasicBlock *> ExitBlocks,
    ArrayRef<std::unique_ptr<ValueToValueMapTy>> VMaps, DominatorTree &DT) {
  auto GetPtr = [&](const std::unique_ptr<ValueToValueMapTy> &I) {
    return I.get();
  };
  using MappedIteratorType =
      mapped_iterator<const std::unique_ptr<ValueToValueMapTy> *,
                      decltype(GetPtr)>;
  auto MapBegin = MappedIteratorType(VMaps.begin(), GetPtr);
  auto MapEnd = MappedIteratorType(VMaps.end(), GetPtr);
  privateUpdateExitBlocksForClonedLoop(ExitBlocks, MapBegin, MapEnd, DT);
}

void MemorySSAUpdater::applyUpdates(ArrayRef<CFGUpdate> Updates,
                                    DominatorTree &DT) {
  SmallVector<CFGUpdate, 4> RevDeleteUpdates;
  SmallVector<CFGUpdate, 4> InsertUpdates;
  for (auto &Update : Updates) {
    if (Update.getKind() == DT.Insert)
      InsertUpdates.push_back({DT.Insert, Update.getFrom(), Update.getTo()});
    else
      RevDeleteUpdates.push_back({DT.Insert, Update.getFrom(), Update.getTo()});
  }

  if (!RevDeleteUpdates.empty()) {
    // Update for inserted edges: use newDT and snapshot CFG as if deletes had
    // not occured.
    // FIXME: This creates a new DT, so it's more expensive to do mix
    // delete/inserts vs just inserts. We can do an incremental update on the DT
    // to revert deletes, than re-delete the edges. Teaching DT to do this, is
    // part of a pending cleanup.
    DominatorTree NewDT(DT, RevDeleteUpdates);
    GraphDiff<BasicBlock *> GD(RevDeleteUpdates);
    applyInsertUpdates(InsertUpdates, NewDT, &GD);
  } else {
    GraphDiff<BasicBlock *> GD;
    applyInsertUpdates(InsertUpdates, DT, &GD);
  }

  // Update for deleted edges
  for (auto &Update : RevDeleteUpdates)
    removeEdge(Update.getFrom(), Update.getTo());
}

void MemorySSAUpdater::applyInsertUpdates(ArrayRef<CFGUpdate> Updates,
                                          DominatorTree &DT) {
  GraphDiff<BasicBlock *> GD;
  applyInsertUpdates(Updates, DT, &GD);
}

void MemorySSAUpdater::applyInsertUpdates(ArrayRef<CFGUpdate> Updates,
                                          DominatorTree &DT,
                                          const GraphDiff<BasicBlock *> *GD) {
  // Get recursive last Def, assuming well formed MSSA and updated DT.
  auto GetLastDef = [&](BasicBlock *BB) -> MemoryAccess * {
    while (true) {
      MemorySSA::DefsList *Defs = MSSA->getWritableBlockDefs(BB);
      // Return last Def or Phi in BB, if it exists.
      if (Defs)
        return &*(--Defs->end());

      // Check number of predecessors, we only care if there's more than one.
      unsigned Count = 0;
      BasicBlock *Pred = nullptr;
      for (auto &Pair : children<GraphDiffInvBBPair>({GD, BB})) {
        Pred = Pair.second;
        Count++;
        if (Count == 2)
          break;
      }

      // If BB has multiple predecessors, get last definition from IDom.
      if (Count != 1) {
        // [SimpleLoopUnswitch] If BB is a dead block, about to be deleted, its
        // DT is invalidated. Return LoE as its last def. This will be added to
        // MemoryPhi node, and later deleted when the block is deleted.
        if (!DT.getNode(BB))
          return MSSA->getLiveOnEntryDef();
        if (auto *IDom = DT.getNode(BB)->getIDom())
          if (IDom->getBlock() != BB) {
            BB = IDom->getBlock();
            continue;
          }
        return MSSA->getLiveOnEntryDef();
      } else {
        // Single predecessor, BB cannot be dead. GetLastDef of Pred.
        assert(Count == 1 && Pred && "Single predecessor expected.");
        BB = Pred;
      }
    };
    llvm_unreachable("Unable to get last definition.");
  };

  // Get nearest IDom given a set of blocks.
  // TODO: this can be optimized by starting the search at the node with the
  // lowest level (highest in the tree).
  auto FindNearestCommonDominator =
      [&](const SmallSetVector<BasicBlock *, 2> &BBSet) -> BasicBlock * {
    BasicBlock *PrevIDom = *BBSet.begin();
    for (auto *BB : BBSet)
      PrevIDom = DT.findNearestCommonDominator(PrevIDom, BB);
    return PrevIDom;
  };

  // Get all blocks that dominate PrevIDom, stop when reaching CurrIDom. Do not
  // include CurrIDom.
  auto GetNoLongerDomBlocks =
      [&](BasicBlock *PrevIDom, BasicBlock *CurrIDom,
          SmallVectorImpl<BasicBlock *> &BlocksPrevDom) {
        if (PrevIDom == CurrIDom)
          return;
        BlocksPrevDom.push_back(PrevIDom);
        BasicBlock *NextIDom = PrevIDom;
        while (BasicBlock *UpIDom =
                   DT.getNode(NextIDom)->getIDom()->getBlock()) {
          if (UpIDom == CurrIDom)
            break;
          BlocksPrevDom.push_back(UpIDom);
          NextIDom = UpIDom;
        }
      };

  // Map a BB to its predecessors: added + previously existing. To get a
  // deterministic order, store predecessors as SetVectors. The order in each
  // will be defined by teh order in Updates (fixed) and the order given by
  // children<> (also fixed). Since we further iterate over these ordered sets,
  // we lose the information of multiple edges possibly existing between two
  // blocks, so we'll keep and EdgeCount map for that.
  // An alternate implementation could keep unordered set for the predecessors,
  // traverse either Updates or children<> each time to get  the deterministic
  // order, and drop the usage of EdgeCount. This alternate approach would still
  // require querying the maps for each predecessor, and children<> call has
  // additional computation inside for creating the snapshot-graph predecessors.
  // As such, we favor using a little additional storage and less compute time.
  // This decision can be revisited if we find the alternative more favorable.

  struct PredInfo {
    SmallSetVector<BasicBlock *, 2> Added;
    SmallSetVector<BasicBlock *, 2> Prev;
  };
  SmallDenseMap<BasicBlock *, PredInfo> PredMap;

  for (auto &Edge : Updates) {
    BasicBlock *BB = Edge.getTo();
    auto &AddedBlockSet = PredMap[BB].Added;
    AddedBlockSet.insert(Edge.getFrom());
  }

  // Store all existing predecessor for each BB, at least one must exist.
  SmallDenseMap<std::pair<BasicBlock *, BasicBlock *>, int> EdgeCountMap;
  SmallPtrSet<BasicBlock *, 2> NewBlocks;
  for (auto &BBPredPair : PredMap) {
    auto *BB = BBPredPair.first;
    const auto &AddedBlockSet = BBPredPair.second.Added;
    auto &PrevBlockSet = BBPredPair.second.Prev;
    for (auto &Pair : children<GraphDiffInvBBPair>({GD, BB})) {
      BasicBlock *Pi = Pair.second;
      if (!AddedBlockSet.count(Pi))
        PrevBlockSet.insert(Pi);
      EdgeCountMap[{Pi, BB}]++;
    }

    if (PrevBlockSet.empty()) {
      assert(pred_size(BB) == AddedBlockSet.size() && "Duplicate edges added.");
      LLVM_DEBUG(
          dbgs()
          << "Adding a predecessor to a block with no predecessors. "
             "This must be an edge added to a new, likely cloned, block. "
             "Its memory accesses must be already correct, assuming completed "
             "via the updateExitBlocksForClonedLoop API. "
             "Assert a single such edge is added so no phi addition or "
             "additional processing is required.\n");
      assert(AddedBlockSet.size() == 1 &&
             "Can only handle adding one predecessor to a new block.");
      // Need to remove new blocks from PredMap. Remove below to not invalidate
      // iterator here.
      NewBlocks.insert(BB);
    }
  }
  // Nothing to process for new/cloned blocks.
  for (auto *BB : NewBlocks)
    PredMap.erase(BB);

  SmallVector<BasicBlock *, 8> BlocksToProcess;
  SmallVector<BasicBlock *, 16> BlocksWithDefsToReplace;

  // First create MemoryPhis in all blocks that don't have one. Create in the
  // order found in Updates, not in PredMap, to get deterministic numbering.
  for (auto &Edge : Updates) {
    BasicBlock *BB = Edge.getTo();
    if (PredMap.count(BB) && !MSSA->getMemoryAccess(BB))
      MSSA->createMemoryPhi(BB);
  }

  // Now we'll fill in the MemoryPhis with the right incoming values.
  for (auto &BBPredPair : PredMap) {
    auto *BB = BBPredPair.first;
    const auto &PrevBlockSet = BBPredPair.second.Prev;
    const auto &AddedBlockSet = BBPredPair.second.Added;
    assert(!PrevBlockSet.empty() &&
           "At least one previous predecessor must exist.");

    // TODO: if this becomes a bottleneck, we can save on GetLastDef calls by
    // keeping this map before the loop. We can reuse already populated entries
    // if an edge is added from the same predecessor to two different blocks,
    // and this does happen in rotate. Note that the map needs to be updated
    // when deleting non-necessary phis below, if the phi is in the map by
    // replacing the value with DefP1.
    SmallDenseMap<BasicBlock *, MemoryAccess *> LastDefAddedPred;
    for (auto *AddedPred : AddedBlockSet) {
      auto *DefPn = GetLastDef(AddedPred);
      assert(DefPn != nullptr && "Unable to find last definition.");
      LastDefAddedPred[AddedPred] = DefPn;
    }

    MemoryPhi *NewPhi = MSSA->getMemoryAccess(BB);
    // If Phi is not empty, add an incoming edge from each added pred. Must
    // still compute blocks with defs to replace for this block below.
    if (NewPhi->getNumOperands()) {
      for (auto *Pred : AddedBlockSet) {
        auto *LastDefForPred = LastDefAddedPred[Pred];
        for (int I = 0, E = EdgeCountMap[{Pred, BB}]; I < E; ++I)
          NewPhi->addIncoming(LastDefForPred, Pred);
      }
    } else {
      // Pick any existing predecessor and get its definition. All other
      // existing predecessors should have the same one, since no phi existed.
      auto *P1 = *PrevBlockSet.begin();
      MemoryAccess *DefP1 = GetLastDef(P1);

      // Check DefP1 against all Defs in LastDefPredPair. If all the same,
      // nothing to add.
      bool InsertPhi = false;
      for (auto LastDefPredPair : LastDefAddedPred)
        if (DefP1 != LastDefPredPair.second) {
          InsertPhi = true;
          break;
        }
      if (!InsertPhi) {
        // Since NewPhi may be used in other newly added Phis, replace all uses
        // of NewPhi with the definition coming from all predecessors (DefP1),
        // before deleting it.
        NewPhi->replaceAllUsesWith(DefP1);
        removeMemoryAccess(NewPhi);
        continue;
      }

      // Update Phi with new values for new predecessors and old value for all
      // other predecessors. Since AddedBlockSet and PrevBlockSet are ordered
      // sets, the order of entries in NewPhi is deterministic.
      for (auto *Pred : AddedBlockSet) {
        auto *LastDefForPred = LastDefAddedPred[Pred];
        for (int I = 0, E = EdgeCountMap[{Pred, BB}]; I < E; ++I)
          NewPhi->addIncoming(LastDefForPred, Pred);
      }
      for (auto *Pred : PrevBlockSet)
        for (int I = 0, E = EdgeCountMap[{Pred, BB}]; I < E; ++I)
          NewPhi->addIncoming(DefP1, Pred);

      // Insert BB in the set of blocks that now have definition. We'll use this
      // to compute IDF and add Phis there next.
      BlocksToProcess.push_back(BB);
    }

    // Get all blocks that used to dominate BB and no longer do after adding
    // AddedBlockSet, where PrevBlockSet are the previously known predecessors.
    assert(DT.getNode(BB)->getIDom() && "BB does not have valid idom");
    BasicBlock *PrevIDom = FindNearestCommonDominator(PrevBlockSet);
    assert(PrevIDom && "Previous IDom should exists");
    BasicBlock *NewIDom = DT.getNode(BB)->getIDom()->getBlock();
    assert(NewIDom && "BB should have a new valid idom");
    assert(DT.dominates(NewIDom, PrevIDom) &&
           "New idom should dominate old idom");
    GetNoLongerDomBlocks(PrevIDom, NewIDom, BlocksWithDefsToReplace);
  }

  // Compute IDF and add Phis in all IDF blocks that do not have one.
  SmallVector<BasicBlock *, 32> IDFBlocks;
  if (!BlocksToProcess.empty()) {
    ForwardIDFCalculator IDFs(DT);
    SmallPtrSet<BasicBlock *, 16> DefiningBlocks(BlocksToProcess.begin(),
                                                 BlocksToProcess.end());
    IDFs.setDefiningBlocks(DefiningBlocks);
    IDFs.calculate(IDFBlocks);
    for (auto *BBIDF : IDFBlocks) {
      if (auto *IDFPhi = MSSA->getMemoryAccess(BBIDF)) {
        // Update existing Phi.
        // FIXME: some updates may be redundant, try to optimize and skip some.
        for (unsigned I = 0, E = IDFPhi->getNumIncomingValues(); I < E; ++I)
          IDFPhi->setIncomingValue(I, GetLastDef(IDFPhi->getIncomingBlock(I)));
      } else {
        IDFPhi = MSSA->createMemoryPhi(BBIDF);
        for (auto &Pair : children<GraphDiffInvBBPair>({GD, BBIDF})) {
          BasicBlock *Pi = Pair.second;
          IDFPhi->addIncoming(GetLastDef(Pi), Pi);
        }
      }
    }
  }

  // Now for all defs in BlocksWithDefsToReplace, if there are uses they no
  // longer dominate, replace those with the closest dominating def.
  // This will also update optimized accesses, as they're also uses.
  for (auto *BlockWithDefsToReplace : BlocksWithDefsToReplace) {
    if (auto DefsList = MSSA->getWritableBlockDefs(BlockWithDefsToReplace)) {
      for (auto &DefToReplaceUses : *DefsList) {
        BasicBlock *DominatingBlock = DefToReplaceUses.getBlock();
        Value::use_iterator UI = DefToReplaceUses.use_begin(),
                            E = DefToReplaceUses.use_end();
        for (; UI != E;) {
          Use &U = *UI;
          ++UI;
          MemoryAccess *Usr = dyn_cast<MemoryAccess>(U.getUser());
          if (MemoryPhi *UsrPhi = dyn_cast<MemoryPhi>(Usr)) {
            BasicBlock *DominatedBlock = UsrPhi->getIncomingBlock(U);
            if (!DT.dominates(DominatingBlock, DominatedBlock))
              U.set(GetLastDef(DominatedBlock));
          } else {
            BasicBlock *DominatedBlock = Usr->getBlock();
            if (!DT.dominates(DominatingBlock, DominatedBlock)) {
              if (auto *DomBlPhi = MSSA->getMemoryAccess(DominatedBlock))
                U.set(DomBlPhi);
              else {
                auto *IDom = DT.getNode(DominatedBlock)->getIDom();
                assert(IDom && "Block must have a valid IDom.");
                U.set(GetLastDef(IDom->getBlock()));
              }
              cast<MemoryUseOrDef>(Usr)->resetOptimized();
            }
          }
        }
      }
    }
  }
}

// Move What before Where in the MemorySSA IR.
template <class WhereType>
void MemorySSAUpdater::moveTo(MemoryUseOrDef *What, BasicBlock *BB,
                              WhereType Where) {
  // Mark MemoryPhi users of What not to be optimized.
  for (auto *U : What->users())
    if (MemoryPhi *PhiUser = dyn_cast<MemoryPhi>(U))
      NonOptPhis.insert(PhiUser);

  // Replace all our users with our defining access.
  What->replaceAllUsesWith(What->getDefiningAccess());

  // Let MemorySSA take care of moving it around in the lists.
  MSSA->moveTo(What, BB, Where);

  // Now reinsert it into the IR and do whatever fixups needed.
  if (auto *MD = dyn_cast<MemoryDef>(What))
    insertDef(MD);
  else
    insertUse(cast<MemoryUse>(What));

  // Clear dangling pointers. We added all MemoryPhi users, but not all
  // of them are removed by fixupDefs().
  NonOptPhis.clear();
}

// Move What before Where in the MemorySSA IR.
void MemorySSAUpdater::moveBefore(MemoryUseOrDef *What, MemoryUseOrDef *Where) {
  moveTo(What, Where->getBlock(), Where->getIterator());
}

// Move What after Where in the MemorySSA IR.
void MemorySSAUpdater::moveAfter(MemoryUseOrDef *What, MemoryUseOrDef *Where) {
  moveTo(What, Where->getBlock(), ++Where->getIterator());
}

void MemorySSAUpdater::moveToPlace(MemoryUseOrDef *What, BasicBlock *BB,
                                   MemorySSA::InsertionPlace Where) {
  return moveTo(What, BB, Where);
}

// All accesses in To used to be in From. Move to end and update access lists.
void MemorySSAUpdater::moveAllAccesses(BasicBlock *From, BasicBlock *To,
                                       Instruction *Start) {

  MemorySSA::AccessList *Accs = MSSA->getWritableBlockAccesses(From);
  if (!Accs)
    return;

  MemoryAccess *FirstInNew = nullptr;
  for (Instruction &I : make_range(Start->getIterator(), To->end()))
    if ((FirstInNew = MSSA->getMemoryAccess(&I)))
      break;
  if (!FirstInNew)
    return;

  auto *MUD = cast<MemoryUseOrDef>(FirstInNew);
  do {
    auto NextIt = ++MUD->getIterator();
    MemoryUseOrDef *NextMUD = (!Accs || NextIt == Accs->end())
                                  ? nullptr
                                  : cast<MemoryUseOrDef>(&*NextIt);
    MSSA->moveTo(MUD, To, MemorySSA::End);
    // Moving MUD from Accs in the moveTo above, may delete Accs, so we need to
    // retrieve it again.
    Accs = MSSA->getWritableBlockAccesses(From);
    MUD = NextMUD;
  } while (MUD);
}

void MemorySSAUpdater::moveAllAfterSpliceBlocks(BasicBlock *From,
                                                BasicBlock *To,
                                                Instruction *Start) {
  assert(MSSA->getBlockAccesses(To) == nullptr &&
         "To block is expected to be free of MemoryAccesses.");
  moveAllAccesses(From, To, Start);
  for (BasicBlock *Succ : successors(To))
    if (MemoryPhi *MPhi = MSSA->getMemoryAccess(Succ))
      MPhi->setIncomingBlock(MPhi->getBasicBlockIndex(From), To);
}

void MemorySSAUpdater::moveAllAfterMergeBlocks(BasicBlock *From, BasicBlock *To,
                                               Instruction *Start) {
  assert(From->getSinglePredecessor() == To &&
         "From block is expected to have a single predecessor (To).");
  moveAllAccesses(From, To, Start);
  for (BasicBlock *Succ : successors(From))
    if (MemoryPhi *MPhi = MSSA->getMemoryAccess(Succ))
      MPhi->setIncomingBlock(MPhi->getBasicBlockIndex(From), To);
}

/// If all arguments of a MemoryPHI are defined by the same incoming
/// argument, return that argument.
static MemoryAccess *onlySingleValue(MemoryPhi *MP) {
  MemoryAccess *MA = nullptr;

  for (auto &Arg : MP->operands()) {
    if (!MA)
      MA = cast<MemoryAccess>(Arg);
    else if (MA != Arg)
      return nullptr;
  }
  return MA;
}

void MemorySSAUpdater::wireOldPredecessorsToNewImmediatePredecessor(
    BasicBlock *Old, BasicBlock *New, ArrayRef<BasicBlock *> Preds,
    bool IdenticalEdgesWereMerged) {
  assert(!MSSA->getWritableBlockAccesses(New) &&
         "Access list should be null for a new block.");
  MemoryPhi *Phi = MSSA->getMemoryAccess(Old);
  if (!Phi)
    return;
  if (Old->hasNPredecessors(1)) {
    assert(pred_size(New) == Preds.size() &&
           "Should have moved all predecessors.");
    MSSA->moveTo(Phi, New, MemorySSA::Beginning);
  } else {
    assert(!Preds.empty() && "Must be moving at least one predecessor to the "
                             "new immediate predecessor.");
    MemoryPhi *NewPhi = MSSA->createMemoryPhi(New);
    SmallPtrSet<BasicBlock *, 16> PredsSet(Preds.begin(), Preds.end());
    // Currently only support the case of removing a single incoming edge when
    // identical edges were not merged.
    if (!IdenticalEdgesWereMerged)
      assert(PredsSet.size() == Preds.size() &&
             "If identical edges were not merged, we cannot have duplicate "
             "blocks in the predecessors");
    Phi->unorderedDeleteIncomingIf([&](MemoryAccess *MA, BasicBlock *B) {
      if (PredsSet.count(B)) {
        NewPhi->addIncoming(MA, B);
        if (!IdenticalEdgesWereMerged)
          PredsSet.erase(B);
        return true;
      }
      return false;
    });
    Phi->addIncoming(NewPhi, New);
    if (onlySingleValue(NewPhi))
      removeMemoryAccess(NewPhi);
  }
}

void MemorySSAUpdater::removeMemoryAccess(MemoryAccess *MA) {
  assert(!MSSA->isLiveOnEntryDef(MA) &&
         "Trying to remove the live on entry def");
  // We can only delete phi nodes if they have no uses, or we can replace all
  // uses with a single definition.
  MemoryAccess *NewDefTarget = nullptr;
  if (MemoryPhi *MP = dyn_cast<MemoryPhi>(MA)) {
    // Note that it is sufficient to know that all edges of the phi node have
    // the same argument.  If they do, by the definition of dominance frontiers
    // (which we used to place this phi), that argument must dominate this phi,
    // and thus, must dominate the phi's uses, and so we will not hit the assert
    // below.
    NewDefTarget = onlySingleValue(MP);
    assert((NewDefTarget || MP->use_empty()) &&
           "We can't delete this memory phi");
  } else {
    NewDefTarget = cast<MemoryUseOrDef>(MA)->getDefiningAccess();
  }

  // Re-point the uses at our defining access
  if (!isa<MemoryUse>(MA) && !MA->use_empty()) {
    // Reset optimized on users of this store, and reset the uses.
    // A few notes:
    // 1. This is a slightly modified version of RAUW to avoid walking the
    // uses twice here.
    // 2. If we wanted to be complete, we would have to reset the optimized
    // flags on users of phi nodes if doing the below makes a phi node have all
    // the same arguments. Instead, we prefer users to removeMemoryAccess those
    // phi nodes, because doing it here would be N^3.
    if (MA->hasValueHandle())
      ValueHandleBase::ValueIsRAUWd(MA, NewDefTarget);
    // Note: We assume MemorySSA is not used in metadata since it's not really
    // part of the IR.

    while (!MA->use_empty()) {
      Use &U = *MA->use_begin();
      if (auto *MUD = dyn_cast<MemoryUseOrDef>(U.getUser()))
        MUD->resetOptimized();
      U.set(NewDefTarget);
    }
  }

  // The call below to erase will destroy MA, so we can't change the order we
  // are doing things here
  MSSA->removeFromLookups(MA);
  MSSA->removeFromLists(MA);
}

void MemorySSAUpdater::removeBlocks(
    const SmallPtrSetImpl<BasicBlock *> &DeadBlocks) {
  // First delete all uses of BB in MemoryPhis.
  for (BasicBlock *BB : DeadBlocks) {
    Instruction *TI = BB->getTerminator();
    assert(TI && "Basic block expected to have a terminator instruction");
    for (BasicBlock *Succ : successors(TI))
      if (!DeadBlocks.count(Succ))
        if (MemoryPhi *MP = MSSA->getMemoryAccess(Succ)) {
          MP->unorderedDeleteIncomingBlock(BB);
          if (MP->getNumIncomingValues() == 1)
            removeMemoryAccess(MP);
        }
    // Drop all references of all accesses in BB
    if (MemorySSA::AccessList *Acc = MSSA->getWritableBlockAccesses(BB))
      for (MemoryAccess &MA : *Acc)
        MA.dropAllReferences();
  }

  // Next, delete all memory accesses in each block
  for (BasicBlock *BB : DeadBlocks) {
    MemorySSA::AccessList *Acc = MSSA->getWritableBlockAccesses(BB);
    if (!Acc)
      continue;
    for (auto AB = Acc->begin(), AE = Acc->end(); AB != AE;) {
      MemoryAccess *MA = &*AB;
      ++AB;
      MSSA->removeFromLookups(MA);
      MSSA->removeFromLists(MA);
    }
  }
}

MemoryAccess *MemorySSAUpdater::createMemoryAccessInBB(
    Instruction *I, MemoryAccess *Definition, const BasicBlock *BB,
    MemorySSA::InsertionPlace Point) {
  MemoryUseOrDef *NewAccess = MSSA->createDefinedAccess(I, Definition);
  MSSA->insertIntoListsForBlock(NewAccess, BB, Point);
  return NewAccess;
}

MemoryUseOrDef *MemorySSAUpdater::createMemoryAccessBefore(
    Instruction *I, MemoryAccess *Definition, MemoryUseOrDef *InsertPt) {
  assert(I->getParent() == InsertPt->getBlock() &&
         "New and old access must be in the same block");
  MemoryUseOrDef *NewAccess = MSSA->createDefinedAccess(I, Definition);
  MSSA->insertIntoListsBefore(NewAccess, InsertPt->getBlock(),
                              InsertPt->getIterator());
  return NewAccess;
}

MemoryUseOrDef *MemorySSAUpdater::createMemoryAccessAfter(
    Instruction *I, MemoryAccess *Definition, MemoryAccess *InsertPt) {
  assert(I->getParent() == InsertPt->getBlock() &&
         "New and old access must be in the same block");
  MemoryUseOrDef *NewAccess = MSSA->createDefinedAccess(I, Definition);
  MSSA->insertIntoListsBefore(NewAccess, InsertPt->getBlock(),
                              ++InsertPt->getIterator());
  return NewAccess;
}
