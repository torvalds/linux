//=- WebAssemblyFixIrreducibleControlFlow.cpp - Fix irreducible control flow -//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that transforms irreducible control flow into
/// reducible control flow. Irreducible control flow means multiple-entry
/// loops; they appear as CFG cycles that are not recorded in MachineLoopInfo
/// due to being unnatural.
///
/// Note that LLVM has a generic pass that lowers irreducible control flow, but
/// it linearizes control flow, turning diamonds into two triangles, which is
/// both unnecessary and undesirable for WebAssembly.
///
/// The big picture: Ignoring natural loops (seeing them monolithically), we
/// find all the blocks which can return to themselves ("loopers"). Loopers
/// reachable from the non-loopers are loop entries: if there are 2 or more,
/// then we have irreducible control flow. We fix that as follows: a new block
/// is created that can dispatch to each of the loop entries, based on the
/// value of a label "helper" variable, and we replace direct branches to the
/// entries with assignments to the label variable and a branch to the dispatch
/// block. Then the dispatch block is the single entry in a new natural loop.
///
/// This is similar to what the Relooper [1] does, both identify looping code
/// that requires multiple entries, and resolve it in a similar way. In
/// Relooper terminology, we implement a Multiple shape in a Loop shape. Note
/// also that like the Relooper, we implement a "minimal" intervention: we only
/// use the "label" helper for the blocks we absolutely must and no others. We
/// also prioritize code size and do not perform node splitting (i.e. we don't
/// duplicate code in order to resolve irreducibility).
///
/// The difference between this code and the Relooper is that the Relooper also
/// generates ifs and loops and works in a recursive manner, knowing at each
/// point what the entries are, and recursively breaks down the problem. Here
/// we just want to resolve irreducible control flow, and we also want to use
/// as much LLVM infrastructure as possible. So we use the MachineLoopInfo to
/// identify natural loops, etc., and we start with the whole CFG and must
/// identify both the looping code and its entries.
///
/// [1] Alon Zakai. 2011. Emscripten: an LLVM-to-JavaScript compiler. In
/// Proceedings of the ACM international conference companion on Object oriented
/// programming systems languages and applications companion (SPLASH '11). ACM,
/// New York, NY, USA, 301-312. DOI=10.1145/2048147.2048224
/// http://doi.acm.org/10.1145/2048147.2048224
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-fix-irreducible-control-flow"

namespace {

class LoopFixer {
public:
  LoopFixer(MachineFunction &MF, MachineLoopInfo &MLI, MachineLoop *Loop)
      : MF(MF), MLI(MLI), Loop(Loop) {}

  // Run the fixer on the given inputs. Returns whether changes were made.
  bool run();

private:
  MachineFunction &MF;
  MachineLoopInfo &MLI;
  MachineLoop *Loop;

  MachineBasicBlock *Header;
  SmallPtrSet<MachineBasicBlock *, 4> LoopBlocks;

  using BlockSet = SmallPtrSet<MachineBasicBlock *, 4>;
  DenseMap<MachineBasicBlock *, BlockSet> Reachable;

  // The worklist contains pairs of recent additions, (a, b), where we just
  // added a link a => b.
  using BlockPair = std::pair<MachineBasicBlock *, MachineBasicBlock *>;
  SmallVector<BlockPair, 4> WorkList;

  // Get a canonical block to represent a block or a loop: the block, or if in
  // an inner loop, the loop header, of it in an outer loop scope, we can
  // ignore it. We need to call this on all blocks we work on.
  MachineBasicBlock *canonicalize(MachineBasicBlock *MBB) {
    MachineLoop *InnerLoop = MLI.getLoopFor(MBB);
    if (InnerLoop == Loop) {
      return MBB;
    } else {
      // This is either in an outer or an inner loop, and not in ours.
      if (!LoopBlocks.count(MBB)) {
        // It's in outer code, ignore it.
        return nullptr;
      }
      assert(InnerLoop);
      // It's in an inner loop, canonicalize it to the header of that loop.
      return InnerLoop->getHeader();
    }
  }

  // For a successor we can additionally ignore it if it's a branch back to a
  // natural loop top, as when we are in the scope of a loop, we just care
  // about internal irreducibility, and can ignore the loop we are in. We need
  // to call this on all blocks in a context where they are a successor.
  MachineBasicBlock *canonicalizeSuccessor(MachineBasicBlock *MBB) {
    if (Loop && MBB == Loop->getHeader()) {
      // Ignore branches going to the loop's natural header.
      return nullptr;
    }
    return canonicalize(MBB);
  }

  // Potentially insert a new reachable edge, and if so, note it as further
  // work.
  void maybeInsert(MachineBasicBlock *MBB, MachineBasicBlock *Succ) {
    assert(MBB == canonicalize(MBB));
    assert(Succ);
    // Succ may not be interesting as a sucessor.
    Succ = canonicalizeSuccessor(Succ);
    if (!Succ)
      return;
    if (Reachable[MBB].insert(Succ).second) {
      // For there to be further work, it means that we have
      //   X => MBB => Succ
      // for some other X, and in that case X => Succ would be a new edge for
      // us to discover later. However, if we don't care about MBB as a
      // successor, then we don't care about that anyhow.
      if (canonicalizeSuccessor(MBB)) {
        WorkList.emplace_back(MBB, Succ);
      }
    }
  }
};

bool LoopFixer::run() {
  Header = Loop ? Loop->getHeader() : &*MF.begin();

  // Identify all the blocks in this loop scope.
  if (Loop) {
    for (auto *MBB : Loop->getBlocks()) {
      LoopBlocks.insert(MBB);
    }
  } else {
    for (auto &MBB : MF) {
      LoopBlocks.insert(&MBB);
    }
  }

  // Compute which (canonicalized) blocks each block can reach.

  // Add all the initial work.
  for (auto *MBB : LoopBlocks) {
    MachineLoop *InnerLoop = MLI.getLoopFor(MBB);

    if (InnerLoop == Loop) {
      for (auto *Succ : MBB->successors()) {
        maybeInsert(MBB, Succ);
      }
    } else {
      // It can't be in an outer loop - we loop on LoopBlocks - and so it must
      // be an inner loop.
      assert(InnerLoop);
      // Check if we are the canonical block for this loop.
      if (canonicalize(MBB) != MBB) {
        continue;
      }
      // The successors are those of the loop.
      SmallVector<MachineBasicBlock *, 2> ExitBlocks;
      InnerLoop->getExitBlocks(ExitBlocks);
      for (auto *Succ : ExitBlocks) {
        maybeInsert(MBB, Succ);
      }
    }
  }

  // Do work until we are all done.
  while (!WorkList.empty()) {
    MachineBasicBlock *MBB;
    MachineBasicBlock *Succ;
    std::tie(MBB, Succ) = WorkList.pop_back_val();
    // The worklist item is an edge we just added, so it must have valid blocks
    // (and not something canonicalized to nullptr).
    assert(MBB);
    assert(Succ);
    // The successor in that pair must also be a valid successor.
    assert(MBB == canonicalizeSuccessor(MBB));
    // We recently added MBB => Succ, and that means we may have enabled
    // Pred => MBB => Succ. Check all the predecessors. Note that our loop here
    // is correct for both a block and a block representing a loop, as the loop
    // is natural and so the predecessors are all predecessors of the loop
    // header, which is the block we have here.
    for (auto *Pred : MBB->predecessors()) {
      // Canonicalize, make sure it's relevant, and check it's not the same
      // block (an update to the block itself doesn't help compute that same
      // block).
      Pred = canonicalize(Pred);
      if (Pred && Pred != MBB) {
        maybeInsert(Pred, Succ);
      }
    }
  }

  // It's now trivial to identify the loopers.
  SmallPtrSet<MachineBasicBlock *, 4> Loopers;
  for (auto MBB : LoopBlocks) {
    if (Reachable[MBB].count(MBB)) {
      Loopers.insert(MBB);
    }
  }
  // The header cannot be a looper. At the toplevel, LLVM does not allow the
  // entry to be in a loop, and in a natural loop we should ignore the header.
  assert(Loopers.count(Header) == 0);

  // Find the entries, loopers reachable from non-loopers.
  SmallPtrSet<MachineBasicBlock *, 4> Entries;
  SmallVector<MachineBasicBlock *, 4> SortedEntries;
  for (auto *Looper : Loopers) {
    for (auto *Pred : Looper->predecessors()) {
      Pred = canonicalize(Pred);
      if (Pred && !Loopers.count(Pred)) {
        Entries.insert(Looper);
        SortedEntries.push_back(Looper);
        break;
      }
    }
  }

  // Check if we found irreducible control flow.
  if (LLVM_LIKELY(Entries.size() <= 1))
    return false;

  // Sort the entries to ensure a deterministic build.
  llvm::sort(SortedEntries,
             [&](const MachineBasicBlock *A, const MachineBasicBlock *B) {
               auto ANum = A->getNumber();
               auto BNum = B->getNumber();
               return ANum < BNum;
             });

#ifndef NDEBUG
  for (auto Block : SortedEntries)
    assert(Block->getNumber() != -1);
  if (SortedEntries.size() > 1) {
    for (auto I = SortedEntries.begin(), E = SortedEntries.end() - 1;
         I != E; ++I) {
      auto ANum = (*I)->getNumber();
      auto BNum = (*(std::next(I)))->getNumber();
      assert(ANum != BNum);
    }
  }
#endif

  // Create a dispatch block which will contain a jump table to the entries.
  MachineBasicBlock *Dispatch = MF.CreateMachineBasicBlock();
  MF.insert(MF.end(), Dispatch);
  MLI.changeLoopFor(Dispatch, Loop);

  // Add the jump table.
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  MachineInstrBuilder MIB = BuildMI(*Dispatch, Dispatch->end(), DebugLoc(),
                                    TII.get(WebAssembly::BR_TABLE_I32));

  // Add the register which will be used to tell the jump table which block to
  // jump to.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned Reg = MRI.createVirtualRegister(&WebAssembly::I32RegClass);
  MIB.addReg(Reg);

  // Compute the indices in the superheader, one for each bad block, and
  // add them as successors.
  DenseMap<MachineBasicBlock *, unsigned> Indices;
  for (auto *MBB : SortedEntries) {
    auto Pair = Indices.insert(std::make_pair(MBB, 0));
    if (!Pair.second) {
      continue;
    }

    unsigned Index = MIB.getInstr()->getNumExplicitOperands() - 1;
    Pair.first->second = Index;

    MIB.addMBB(MBB);
    Dispatch->addSuccessor(MBB);
  }

  // Rewrite the problematic successors for every block that wants to reach the
  // bad blocks. For simplicity, we just introduce a new block for every edge
  // we need to rewrite. (Fancier things are possible.)

  SmallVector<MachineBasicBlock *, 4> AllPreds;
  for (auto *MBB : SortedEntries) {
    for (auto *Pred : MBB->predecessors()) {
      if (Pred != Dispatch) {
        AllPreds.push_back(Pred);
      }
    }
  }

  for (MachineBasicBlock *MBB : AllPreds) {
    DenseMap<MachineBasicBlock *, MachineBasicBlock *> Map;
    for (auto *Succ : MBB->successors()) {
      if (!Entries.count(Succ)) {
        continue;
      }

      // This is a successor we need to rewrite.
      MachineBasicBlock *Split = MF.CreateMachineBasicBlock();
      MF.insert(MBB->isLayoutSuccessor(Succ) ? MachineFunction::iterator(Succ)
                                             : MF.end(),
                Split);
      MLI.changeLoopFor(Split, Loop);

      // Set the jump table's register of the index of the block we wish to
      // jump to, and jump to the jump table.
      BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::CONST_I32),
              Reg)
          .addImm(Indices[Succ]);
      BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::BR))
          .addMBB(Dispatch);
      Split->addSuccessor(Dispatch);
      Map[Succ] = Split;
    }
    // Remap the terminator operands and the successor list.
    for (MachineInstr &Term : MBB->terminators())
      for (auto &Op : Term.explicit_uses())
        if (Op.isMBB() && Indices.count(Op.getMBB()))
          Op.setMBB(Map[Op.getMBB()]);
    for (auto Rewrite : Map)
      MBB->replaceSuccessor(Rewrite.first, Rewrite.second);
  }

  // Create a fake default label, because br_table requires one.
  MIB.addMBB(MIB.getInstr()
                 ->getOperand(MIB.getInstr()->getNumExplicitOperands() - 1)
                 .getMBB());

  return true;
}

class WebAssemblyFixIrreducibleControlFlow final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Irreducible Control Flow";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool runIteration(MachineFunction &MF, MachineLoopInfo &MLI) {
    // Visit the function body, which is identified as a null loop.
    if (LoopFixer(MF, MLI, nullptr).run()) {
      return true;
    }

    // Visit all the loops.
    SmallVector<MachineLoop *, 8> Worklist(MLI.begin(), MLI.end());
    while (!Worklist.empty()) {
      MachineLoop *Loop = Worklist.pop_back_val();
      Worklist.append(Loop->begin(), Loop->end());
      if (LoopFixer(MF, MLI, Loop).run()) {
        return true;
      }
    }

    return false;
  }

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyFixIrreducibleControlFlow() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyFixIrreducibleControlFlow::ID = 0;
INITIALIZE_PASS(WebAssemblyFixIrreducibleControlFlow, DEBUG_TYPE,
                "Removes irreducible control flow", false, false)

FunctionPass *llvm::createWebAssemblyFixIrreducibleControlFlow() {
  return new WebAssemblyFixIrreducibleControlFlow();
}

bool WebAssemblyFixIrreducibleControlFlow::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing Irreducible Control Flow **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  auto &MLI = getAnalysis<MachineLoopInfo>();

  // When we modify something, bail out and recompute MLI, then start again, as
  // we create a new natural loop when we resolve irreducible control flow, and
  // other loops may become nested in it, etc. In practice this is not an issue
  // because irreducible control flow is rare, only very few cycles are needed
  // here.
  while (LLVM_UNLIKELY(runIteration(MF, MLI))) {
    // We rewrote part of the function; recompute MLI and start again.
    LLVM_DEBUG(dbgs() << "Recomputing loops.\n");
    MF.getRegInfo().invalidateLiveness();
    MF.RenumberBlocks();
    getAnalysis<MachineDominatorTree>().runOnMachineFunction(MF);
    MLI.runOnMachineFunction(MF);
    Changed = true;
  }

  return Changed;
}
