//===- HexagonVectorLoopCarriedReuse.cpp ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass removes the computation of provably redundant expressions that have
// been computed earlier in a previous iteration. It relies on the use of PHIs
// to identify loop carried dependences. This is scalar replacement for vector
// types.
//
//-----------------------------------------------------------------------------
// Motivation: Consider the case where we have the following loop structure.
//
// Loop:
//  t0 = a[i];
//  t1 = f(t0);
//  t2 = g(t1);
//  ...
//  t3 = a[i+1];
//  t4 = f(t3);
//  t5 = g(t4);
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// This can be converted to
//  t00 = a[0];
//  t10 = f(t00);
//  t20 = g(t10);
// Loop:
//  t2 = t20;
//  t3 = a[i+1];
//  t4 = f(t3);
//  t5 = g(t4);
//  t6 = op(t2, t5)
//  t20 = t5
//  cond_branch <Loop>
//
// SROA does a good job of reusing a[i+1] as a[i] in the next iteration.
// Such a loop comes to this pass in the following form.
//
// LoopPreheader:
//  X0 = a[0];
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  t1 = f(X2)   <-- I1
//  t2 = g(t1)
//  ...
//  X1 = a[i+1]
//  t4 = f(X1)   <-- I2
//  t5 = g(t4)
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// In this pass, we look for PHIs such as X2 whose incoming values come only
// from the Loop Preheader and over the backedge and additionaly, both these
// values are the results of the same operation in terms of opcode. We call such
// a PHI node a dependence chain or DepChain. In this case, the dependence of X2
// over X1 is carried over only one iteration and so the DepChain is only one
// PHI node long.
//
// Then, we traverse the uses of the PHI (X2) and the uses of the value of the
// PHI coming  over the backedge (X1). We stop at the first pair of such users
// I1 (of X2) and I2 (of X1) that meet the following conditions.
// 1. I1 and I2 are the same operation, but with different operands.
// 2. X2 and X1 are used at the same operand number in the two instructions.
// 3. All other operands Op1 of I1 and Op2 of I2 are also such that there is a
//    a DepChain from Op1 to Op2 of the same length as that between X2 and X1.
//
// We then make the following transformation
// LoopPreheader:
//  X0 = a[0];
//  Y0 = f(X0);
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  Y2 = PHI<(Y0, LoopPreheader), (t4, Loop)>
//  t1 = f(X2)   <-- Will be removed by DCE.
//  t2 = g(Y2)
//  ...
//  X1 = a[i+1]
//  t4 = f(X1)
//  t5 = g(t4)
//  t6 = op(t2, t5)
//  cond_branch <Loop>
//
// We proceed until we cannot find any more such instructions I1 and I2.
//
// --- DepChains & Loop carried dependences ---
// Consider a single basic block loop such as
//
// LoopPreheader:
//  X0 = ...
//  Y0 = ...
// Loop:
//  X2 = PHI<(X0, LoopPreheader), (X1, Loop)>
//  Y2 = PHI<(Y0, LoopPreheader), (X2, Loop)>
//  ...
//  X1 = ...
//  ...
//  cond_branch <Loop>
//
// Then there is a dependence between X2 and X1 that goes back one iteration,
// i.e. X1 is used as X2 in the very next iteration. We represent this as a
// DepChain from X2 to X1 (X2->X1).
// Similarly, there is a dependence between Y2 and X1 that goes back two
// iterations. X1 is used as Y2 two iterations after it is computed. This is
// represented by a DepChain as (Y2->X2->X1).
//
// A DepChain has the following properties.
// 1. Num of edges in DepChain = Number of Instructions in DepChain = Number of
//    iterations of carried dependence + 1.
// 2. All instructions in the DepChain except the last are PHIs.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <set>

using namespace llvm;

#define DEBUG_TYPE "hexagon-vlcr"

STATISTIC(HexagonNumVectorLoopCarriedReuse,
          "Number of values that were reused from a previous iteration.");

static cl::opt<int> HexagonVLCRIterationLim("hexagon-vlcr-iteration-lim",
    cl::Hidden,
    cl::desc("Maximum distance of loop carried dependences that are handled"),
    cl::init(2), cl::ZeroOrMore);

namespace llvm {

void initializeHexagonVectorLoopCarriedReusePass(PassRegistry&);
Pass *createHexagonVectorLoopCarriedReusePass();

} // end namespace llvm

namespace {

  // See info about DepChain in the comments at the top of this file.
  using ChainOfDependences = SmallVector<Instruction *, 4>;

  class DepChain {
    ChainOfDependences Chain;

  public:
    bool isIdentical(DepChain &Other) const {
      if (Other.size() != size())
        return false;
      ChainOfDependences &OtherChain = Other.getChain();
      for (int i = 0; i < size(); ++i) {
        if (Chain[i] != OtherChain[i])
          return false;
      }
      return true;
    }

    ChainOfDependences &getChain() {
      return Chain;
    }

    int size() const {
      return Chain.size();
    }

    void clear() {
      Chain.clear();
    }

    void push_back(Instruction *I) {
      Chain.push_back(I);
    }

    int iterations() const {
      return size() - 1;
    }

    Instruction *front() const {
      return Chain.front();
    }

    Instruction *back() const {
      return Chain.back();
    }

    Instruction *&operator[](const int index) {
      return Chain[index];
    }

   friend raw_ostream &operator<< (raw_ostream &OS, const DepChain &D);
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<<(raw_ostream &OS, const DepChain &D) {
    const ChainOfDependences &CD = D.Chain;
    int ChainSize = CD.size();
    OS << "**DepChain Start::**\n";
    for (int i = 0; i < ChainSize -1; ++i) {
      OS << *(CD[i]) << " -->\n";
    }
    OS << *CD[ChainSize-1] << "\n";
    return OS;
  }

  struct ReuseValue {
    Instruction *Inst2Replace = nullptr;

    // In the new PHI node that we'll construct this is the value that'll be
    // used over the backedge. This is teh value that gets reused from a
    // previous iteration.
    Instruction *BackedgeInst = nullptr;

    ReuseValue() = default;

    void reset() { Inst2Replace = nullptr; BackedgeInst = nullptr; }
    bool isDefined() { return Inst2Replace != nullptr; }
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<<(raw_ostream &OS, const ReuseValue &RU) {
    OS << "** ReuseValue ***\n";
    OS << "Instruction to Replace: " << *(RU.Inst2Replace) << "\n";
    OS << "Backedge Instruction: " << *(RU.BackedgeInst) << "\n";
    return OS;
  }

  class HexagonVectorLoopCarriedReuse : public LoopPass {
  public:
    static char ID;

    explicit HexagonVectorLoopCarriedReuse() : LoopPass(ID) {
      PassRegistry *PR = PassRegistry::getPassRegistry();
      initializeHexagonVectorLoopCarriedReusePass(*PR);
    }

    StringRef getPassName() const override {
      return "Hexagon-specific loop carried reuse for HVX vectors";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequiredID(LoopSimplifyID);
      AU.addRequiredID(LCSSAID);
      AU.addPreservedID(LCSSAID);
      AU.setPreservesCFG();
    }

    bool runOnLoop(Loop *L, LPPassManager &LPM) override;

  private:
    SetVector<DepChain *> Dependences;
    std::set<Instruction *> ReplacedInsts;
    Loop *CurLoop;
    ReuseValue ReuseCandidate;

    bool doVLCR();
    void findLoopCarriedDeps();
    void findValueToReuse();
    void findDepChainFromPHI(Instruction *I, DepChain &D);
    void reuseValue();
    Value *findValueInBlock(Value *Op, BasicBlock *BB);
    bool isDepChainBtwn(Instruction *I1, Instruction *I2, int Iters);
    DepChain *getDepChainBtwn(Instruction *I1, Instruction *I2);
    bool isEquivalentOperation(Instruction *I1, Instruction *I2);
    bool canReplace(Instruction *I);
  };

} // end anonymous namespace

char HexagonVectorLoopCarriedReuse::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonVectorLoopCarriedReuse, "hexagon-vlcr",
    "Hexagon-specific predictive commoning for HVX vectors", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(LCSSAWrapperPass)
INITIALIZE_PASS_END(HexagonVectorLoopCarriedReuse, "hexagon-vlcr",
    "Hexagon-specific predictive commoning for HVX vectors", false, false)

bool HexagonVectorLoopCarriedReuse::runOnLoop(Loop *L, LPPassManager &LPM) {
  if (skipLoop(L))
    return false;

  if (!L->getLoopPreheader())
    return false;

  // Work only on innermost loops.
  if (!L->getSubLoops().empty())
    return false;

  // Work only on single basic blocks loops.
  if (L->getNumBlocks() != 1)
    return false;

  CurLoop = L;

  return doVLCR();
}

bool HexagonVectorLoopCarriedReuse::isEquivalentOperation(Instruction *I1,
                                                          Instruction *I2) {
  if (!I1->isSameOperationAs(I2))
    return false;
  // This check is in place specifically for intrinsics. isSameOperationAs will
  // return two for any two hexagon intrinsics because they are essentially the
  // same instruciton (CallInst). We need to scratch the surface to see if they
  // are calls to the same function.
  if (CallInst *C1 = dyn_cast<CallInst>(I1)) {
    if (CallInst *C2 = dyn_cast<CallInst>(I2)) {
      if (C1->getCalledFunction() != C2->getCalledFunction())
        return false;
    }
  }

  // If both the Instructions are of Vector Type and any of the element
  // is integer constant, check their values too for equivalence.
  if (I1->getType()->isVectorTy() && I2->getType()->isVectorTy()) {
    unsigned NumOperands = I1->getNumOperands();
    for (unsigned i = 0; i < NumOperands; ++i) {
      ConstantInt *C1 = dyn_cast<ConstantInt>(I1->getOperand(i));
      ConstantInt *C2 = dyn_cast<ConstantInt>(I2->getOperand(i));
      if(!C1) continue;
      assert(C2);
      if (C1->getSExtValue() != C2->getSExtValue())
        return false;
    }
  }

  return true;
}

bool HexagonVectorLoopCarriedReuse::canReplace(Instruction *I) {
  const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I);
  if (II &&
      (II->getIntrinsicID() == Intrinsic::hexagon_V6_hi ||
       II->getIntrinsicID() == Intrinsic::hexagon_V6_lo)) {
    LLVM_DEBUG(dbgs() << "Not considering for reuse: " << *II << "\n");
    return false;
  }
  return true;
}
void HexagonVectorLoopCarriedReuse::findValueToReuse() {
  for (auto *D : Dependences) {
    LLVM_DEBUG(dbgs() << "Processing dependence " << *(D->front()) << "\n");
    if (D->iterations() > HexagonVLCRIterationLim) {
      LLVM_DEBUG(
          dbgs()
          << ".. Skipping because number of iterations > than the limit\n");
      continue;
    }

    PHINode *PN = cast<PHINode>(D->front());
    Instruction *BEInst = D->back();
    int Iters = D->iterations();
    BasicBlock *BB = PN->getParent();
    LLVM_DEBUG(dbgs() << "Checking if any uses of " << *PN
                      << " can be reused\n");

    SmallVector<Instruction *, 4> PNUsers;
    for (auto UI = PN->use_begin(), E = PN->use_end(); UI != E; ++UI) {
      Use &U = *UI;
      Instruction *User = cast<Instruction>(U.getUser());

      if (User->getParent() != BB)
        continue;
      if (ReplacedInsts.count(User)) {
        LLVM_DEBUG(dbgs() << *User
                          << " has already been replaced. Skipping...\n");
        continue;
      }
      if (isa<PHINode>(User))
        continue;
      if (User->mayHaveSideEffects())
        continue;
      if (!canReplace(User))
        continue;

      PNUsers.push_back(User);
    }
    LLVM_DEBUG(dbgs() << PNUsers.size() << " use(s) of the PHI in the block\n");

    // For each interesting use I of PN, find an Instruction BEUser that
    // performs the same operation as I on BEInst and whose other operands,
    // if any, can also be rematerialized in OtherBB. We stop when we find the
    // first such Instruction BEUser. This is because once BEUser is
    // rematerialized in OtherBB, we may find more such "fixup" opportunities
    // in this block. So, we'll start over again.
    for (Instruction *I : PNUsers) {
      for (auto UI = BEInst->use_begin(), E = BEInst->use_end(); UI != E;
           ++UI) {
        Use &U = *UI;
        Instruction *BEUser = cast<Instruction>(U.getUser());

        if (BEUser->getParent() != BB)
          continue;
        if (!isEquivalentOperation(I, BEUser))
          continue;

        int NumOperands = I->getNumOperands();

        for (int OpNo = 0; OpNo < NumOperands; ++OpNo) {
          Value *Op = I->getOperand(OpNo);
          Instruction *OpInst = dyn_cast<Instruction>(Op);
          if (!OpInst)
            continue;

          Value *BEOp = BEUser->getOperand(OpNo);
          Instruction *BEOpInst = dyn_cast<Instruction>(BEOp);

          if (!isDepChainBtwn(OpInst, BEOpInst, Iters)) {
            BEUser = nullptr;
            break;
          }
        }
        if (BEUser) {
          LLVM_DEBUG(dbgs() << "Found Value for reuse.\n");
          ReuseCandidate.Inst2Replace = I;
          ReuseCandidate.BackedgeInst = BEUser;
          return;
        } else
          ReuseCandidate.reset();
      }
    }
  }
  ReuseCandidate.reset();
}

Value *HexagonVectorLoopCarriedReuse::findValueInBlock(Value *Op,
                                                       BasicBlock *BB) {
  PHINode *PN = dyn_cast<PHINode>(Op);
  assert(PN);
  Value *ValueInBlock = PN->getIncomingValueForBlock(BB);
  return ValueInBlock;
}

void HexagonVectorLoopCarriedReuse::reuseValue() {
  LLVM_DEBUG(dbgs() << ReuseCandidate);
  Instruction *Inst2Replace = ReuseCandidate.Inst2Replace;
  Instruction *BEInst = ReuseCandidate.BackedgeInst;
  int NumOperands = Inst2Replace->getNumOperands();
  std::map<Instruction *, DepChain *> DepChains;
  int Iterations = -1;
  BasicBlock *LoopPH = CurLoop->getLoopPreheader();

  for (int i = 0; i < NumOperands; ++i) {
    Instruction *I = dyn_cast<Instruction>(Inst2Replace->getOperand(i));
    if(!I)
      continue;
    else {
      Instruction *J = cast<Instruction>(BEInst->getOperand(i));
      DepChain *D = getDepChainBtwn(I, J);

      assert(D &&
             "No DepChain between corresponding operands in ReuseCandidate\n");
      if (Iterations == -1)
        Iterations = D->iterations();
      assert(Iterations == D->iterations() && "Iterations mismatch");
      DepChains[I] = D;
    }
  }

  LLVM_DEBUG(dbgs() << "reuseValue is making the following changes\n");

  SmallVector<Instruction *, 4> InstsInPreheader;
  for (int i = 0; i < Iterations; ++i) {
    Instruction *InstInPreheader = Inst2Replace->clone();
    SmallVector<Value *, 4> Ops;
    for (int j = 0; j < NumOperands; ++j) {
      Instruction *I = dyn_cast<Instruction>(Inst2Replace->getOperand(j));
      if (!I)
        continue;
      // Get the DepChain corresponding to this operand.
      DepChain &D = *DepChains[I];
      // Get the PHI for the iteration number and find
      // the incoming value from the Loop Preheader for
      // that PHI.
      Value *ValInPreheader = findValueInBlock(D[i], LoopPH);
      InstInPreheader->setOperand(j, ValInPreheader);
    }
    InstsInPreheader.push_back(InstInPreheader);
    InstInPreheader->setName(Inst2Replace->getName() + ".hexagon.vlcr");
    InstInPreheader->insertBefore(LoopPH->getTerminator());
    LLVM_DEBUG(dbgs() << "Added " << *InstInPreheader << " to "
                      << LoopPH->getName() << "\n");
  }
  BasicBlock *BB = BEInst->getParent();
  IRBuilder<> IRB(BB);
  IRB.SetInsertPoint(BB->getFirstNonPHI());
  Value *BEVal = BEInst;
  PHINode *NewPhi;
  for (int i = Iterations-1; i >=0 ; --i) {
    Instruction *InstInPreheader = InstsInPreheader[i];
    NewPhi = IRB.CreatePHI(InstInPreheader->getType(), 2);
    NewPhi->addIncoming(InstInPreheader, LoopPH);
    NewPhi->addIncoming(BEVal, BB);
    LLVM_DEBUG(dbgs() << "Adding " << *NewPhi << " to " << BB->getName()
                      << "\n");
    BEVal = NewPhi;
  }
  // We are in LCSSA form. So, a value defined inside the Loop is used only
  // inside the loop. So, the following is safe.
  Inst2Replace->replaceAllUsesWith(NewPhi);
  ReplacedInsts.insert(Inst2Replace);
  ++HexagonNumVectorLoopCarriedReuse;
}

bool HexagonVectorLoopCarriedReuse::doVLCR() {
  assert(CurLoop->getSubLoops().empty() &&
         "Can do VLCR on the innermost loop only");
  assert((CurLoop->getNumBlocks() == 1) &&
         "Can do VLCR only on single block loops");

  bool Changed = false;
  bool Continue;

  LLVM_DEBUG(dbgs() << "Working on Loop: " << *CurLoop->getHeader() << "\n");
  do {
    // Reset datastructures.
    Dependences.clear();
    Continue = false;

    findLoopCarriedDeps();
    findValueToReuse();
    if (ReuseCandidate.isDefined()) {
      reuseValue();
      Changed = true;
      Continue = true;
    }
    llvm::for_each(Dependences, std::default_delete<DepChain>());
  } while (Continue);
  return Changed;
}

void HexagonVectorLoopCarriedReuse::findDepChainFromPHI(Instruction *I,
                                                        DepChain &D) {
  PHINode *PN = dyn_cast<PHINode>(I);
  if (!PN) {
    D.push_back(I);
    return;
  } else {
    auto NumIncomingValues = PN->getNumIncomingValues();
    if (NumIncomingValues != 2) {
      D.clear();
      return;
    }

    BasicBlock *BB = PN->getParent();
    if (BB != CurLoop->getHeader()) {
      D.clear();
      return;
    }

    Value *BEVal = PN->getIncomingValueForBlock(BB);
    Instruction *BEInst = dyn_cast<Instruction>(BEVal);
    // This is a single block loop with a preheader, so at least
    // one value should come over the backedge.
    assert(BEInst && "There should be a value over the backedge");

    Value *PreHdrVal =
      PN->getIncomingValueForBlock(CurLoop->getLoopPreheader());
    if(!PreHdrVal || !isa<Instruction>(PreHdrVal)) {
      D.clear();
      return;
    }
    D.push_back(PN);
    findDepChainFromPHI(BEInst, D);
  }
}

bool HexagonVectorLoopCarriedReuse::isDepChainBtwn(Instruction *I1,
                                                      Instruction *I2,
                                                      int Iters) {
  for (auto *D : Dependences) {
    if (D->front() == I1 && D->back() == I2 && D->iterations() == Iters)
      return true;
  }
  return false;
}

DepChain *HexagonVectorLoopCarriedReuse::getDepChainBtwn(Instruction *I1,
                                                            Instruction *I2) {
  for (auto *D : Dependences) {
    if (D->front() == I1 && D->back() == I2)
      return D;
  }
  return nullptr;
}

void HexagonVectorLoopCarriedReuse::findLoopCarriedDeps() {
  BasicBlock *BB = CurLoop->getHeader();
  for (auto I = BB->begin(), E = BB->end(); I != E && isa<PHINode>(I); ++I) {
    auto *PN = cast<PHINode>(I);
    if (!isa<VectorType>(PN->getType()))
      continue;

    DepChain *D = new DepChain();
    findDepChainFromPHI(PN, *D);
    if (D->size() != 0)
      Dependences.insert(D);
    else
      delete D;
  }
  LLVM_DEBUG(dbgs() << "Found " << Dependences.size() << " dependences\n");
  LLVM_DEBUG(for (size_t i = 0; i < Dependences.size();
                  ++i) { dbgs() << *Dependences[i] << "\n"; });
}

Pass *llvm::createHexagonVectorLoopCarriedReusePass() {
  return new HexagonVectorLoopCarriedReuse();
}
