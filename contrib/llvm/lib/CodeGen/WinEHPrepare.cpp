//===-- WinEHPrepare - Prepare exception handling for code generation ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers LLVM IR exception handling into something closer to what the
// backend wants for functions using a personality function from a runtime
// provided by MSVC. Functions with other personality functions are left alone
// and may be prepared by other passes. In particular, all supported MSVC
// personality functions require cleanup code to be outlined, and the C++
// personality requires catch handler code to be outlined.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

using namespace llvm;

#define DEBUG_TYPE "winehprepare"

static cl::opt<bool> DisableDemotion(
    "disable-demotion", cl::Hidden,
    cl::desc(
        "Clone multicolor basic blocks but do not demote cross scopes"),
    cl::init(false));

static cl::opt<bool> DisableCleanups(
    "disable-cleanups", cl::Hidden,
    cl::desc("Do not remove implausible terminators or other similar cleanups"),
    cl::init(false));

static cl::opt<bool> DemoteCatchSwitchPHIOnlyOpt(
    "demote-catchswitch-only", cl::Hidden,
    cl::desc("Demote catchswitch BBs only (for wasm EH)"), cl::init(false));

namespace {

class WinEHPrepare : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid.
  WinEHPrepare(bool DemoteCatchSwitchPHIOnly = false)
      : FunctionPass(ID), DemoteCatchSwitchPHIOnly(DemoteCatchSwitchPHIOnly) {}

  bool runOnFunction(Function &Fn) override;

  bool doFinalization(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  StringRef getPassName() const override {
    return "Windows exception handling preparation";
  }

private:
  void insertPHIStores(PHINode *OriginalPHI, AllocaInst *SpillSlot);
  void
  insertPHIStore(BasicBlock *PredBlock, Value *PredVal, AllocaInst *SpillSlot,
                 SmallVectorImpl<std::pair<BasicBlock *, Value *>> &Worklist);
  AllocaInst *insertPHILoads(PHINode *PN, Function &F);
  void replaceUseWithLoad(Value *V, Use &U, AllocaInst *&SpillSlot,
                          DenseMap<BasicBlock *, Value *> &Loads, Function &F);
  bool prepareExplicitEH(Function &F);
  void colorFunclets(Function &F);

  void demotePHIsOnFunclets(Function &F, bool DemoteCatchSwitchPHIOnly);
  void cloneCommonBlocks(Function &F);
  void removeImplausibleInstructions(Function &F);
  void cleanupPreparedFunclets(Function &F);
  void verifyPreparedFunclets(Function &F);

  bool DemoteCatchSwitchPHIOnly;

  // All fields are reset by runOnFunction.
  EHPersonality Personality = EHPersonality::Unknown;

  const DataLayout *DL = nullptr;
  DenseMap<BasicBlock *, ColorVector> BlockColors;
  MapVector<BasicBlock *, std::vector<BasicBlock *>> FuncletBlocks;
};

} // end anonymous namespace

char WinEHPrepare::ID = 0;
INITIALIZE_PASS(WinEHPrepare, DEBUG_TYPE, "Prepare Windows exceptions",
                false, false)

FunctionPass *llvm::createWinEHPass(bool DemoteCatchSwitchPHIOnly) {
  return new WinEHPrepare(DemoteCatchSwitchPHIOnly);
}

bool WinEHPrepare::runOnFunction(Function &Fn) {
  if (!Fn.hasPersonalityFn())
    return false;

  // Classify the personality to see what kind of preparation we need.
  Personality = classifyEHPersonality(Fn.getPersonalityFn());

  // Do nothing if this is not a scope-based personality.
  if (!isScopedEHPersonality(Personality))
    return false;

  DL = &Fn.getParent()->getDataLayout();
  return prepareExplicitEH(Fn);
}

bool WinEHPrepare::doFinalization(Module &M) { return false; }

void WinEHPrepare::getAnalysisUsage(AnalysisUsage &AU) const {}

static int addUnwindMapEntry(WinEHFuncInfo &FuncInfo, int ToState,
                             const BasicBlock *BB) {
  CxxUnwindMapEntry UME;
  UME.ToState = ToState;
  UME.Cleanup = BB;
  FuncInfo.CxxUnwindMap.push_back(UME);
  return FuncInfo.getLastStateNumber();
}

static void addTryBlockMapEntry(WinEHFuncInfo &FuncInfo, int TryLow,
                                int TryHigh, int CatchHigh,
                                ArrayRef<const CatchPadInst *> Handlers) {
  WinEHTryBlockMapEntry TBME;
  TBME.TryLow = TryLow;
  TBME.TryHigh = TryHigh;
  TBME.CatchHigh = CatchHigh;
  assert(TBME.TryLow <= TBME.TryHigh);
  for (const CatchPadInst *CPI : Handlers) {
    WinEHHandlerType HT;
    Constant *TypeInfo = cast<Constant>(CPI->getArgOperand(0));
    if (TypeInfo->isNullValue())
      HT.TypeDescriptor = nullptr;
    else
      HT.TypeDescriptor = cast<GlobalVariable>(TypeInfo->stripPointerCasts());
    HT.Adjectives = cast<ConstantInt>(CPI->getArgOperand(1))->getZExtValue();
    HT.Handler = CPI->getParent();
    if (auto *AI =
            dyn_cast<AllocaInst>(CPI->getArgOperand(2)->stripPointerCasts()))
      HT.CatchObj.Alloca = AI;
    else
      HT.CatchObj.Alloca = nullptr;
    TBME.HandlerArray.push_back(HT);
  }
  FuncInfo.TryBlockMap.push_back(TBME);
}

static BasicBlock *getCleanupRetUnwindDest(const CleanupPadInst *CleanupPad) {
  for (const User *U : CleanupPad->users())
    if (const auto *CRI = dyn_cast<CleanupReturnInst>(U))
      return CRI->getUnwindDest();
  return nullptr;
}

static void calculateStateNumbersForInvokes(const Function *Fn,
                                            WinEHFuncInfo &FuncInfo) {
  auto *F = const_cast<Function *>(Fn);
  DenseMap<BasicBlock *, ColorVector> BlockColors = colorEHFunclets(*F);
  for (BasicBlock &BB : *F) {
    auto *II = dyn_cast<InvokeInst>(BB.getTerminator());
    if (!II)
      continue;

    auto &BBColors = BlockColors[&BB];
    assert(BBColors.size() == 1 && "multi-color BB not removed by preparation");
    BasicBlock *FuncletEntryBB = BBColors.front();

    BasicBlock *FuncletUnwindDest;
    auto *FuncletPad =
        dyn_cast<FuncletPadInst>(FuncletEntryBB->getFirstNonPHI());
    assert(FuncletPad || FuncletEntryBB == &Fn->getEntryBlock());
    if (!FuncletPad)
      FuncletUnwindDest = nullptr;
    else if (auto *CatchPad = dyn_cast<CatchPadInst>(FuncletPad))
      FuncletUnwindDest = CatchPad->getCatchSwitch()->getUnwindDest();
    else if (auto *CleanupPad = dyn_cast<CleanupPadInst>(FuncletPad))
      FuncletUnwindDest = getCleanupRetUnwindDest(CleanupPad);
    else
      llvm_unreachable("unexpected funclet pad!");

    BasicBlock *InvokeUnwindDest = II->getUnwindDest();
    int BaseState = -1;
    if (FuncletUnwindDest == InvokeUnwindDest) {
      auto BaseStateI = FuncInfo.FuncletBaseStateMap.find(FuncletPad);
      if (BaseStateI != FuncInfo.FuncletBaseStateMap.end())
        BaseState = BaseStateI->second;
    }

    if (BaseState != -1) {
      FuncInfo.InvokeStateMap[II] = BaseState;
    } else {
      Instruction *PadInst = InvokeUnwindDest->getFirstNonPHI();
      assert(FuncInfo.EHPadStateMap.count(PadInst) && "EH Pad has no state!");
      FuncInfo.InvokeStateMap[II] = FuncInfo.EHPadStateMap[PadInst];
    }
  }
}

// Given BB which ends in an unwind edge, return the EHPad that this BB belongs
// to. If the unwind edge came from an invoke, return null.
static const BasicBlock *getEHPadFromPredecessor(const BasicBlock *BB,
                                                 Value *ParentPad) {
  const Instruction *TI = BB->getTerminator();
  if (isa<InvokeInst>(TI))
    return nullptr;
  if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(TI)) {
    if (CatchSwitch->getParentPad() != ParentPad)
      return nullptr;
    return BB;
  }
  assert(!TI->isEHPad() && "unexpected EHPad!");
  auto *CleanupPad = cast<CleanupReturnInst>(TI)->getCleanupPad();
  if (CleanupPad->getParentPad() != ParentPad)
    return nullptr;
  return CleanupPad->getParent();
}

static void calculateCXXStateNumbers(WinEHFuncInfo &FuncInfo,
                                     const Instruction *FirstNonPHI,
                                     int ParentState) {
  const BasicBlock *BB = FirstNonPHI->getParent();
  assert(BB->isEHPad() && "not a funclet!");

  if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(FirstNonPHI)) {
    assert(FuncInfo.EHPadStateMap.count(CatchSwitch) == 0 &&
           "shouldn't revist catch funclets!");

    SmallVector<const CatchPadInst *, 2> Handlers;
    for (const BasicBlock *CatchPadBB : CatchSwitch->handlers()) {
      auto *CatchPad = cast<CatchPadInst>(CatchPadBB->getFirstNonPHI());
      Handlers.push_back(CatchPad);
    }
    int TryLow = addUnwindMapEntry(FuncInfo, ParentState, nullptr);
    FuncInfo.EHPadStateMap[CatchSwitch] = TryLow;
    for (const BasicBlock *PredBlock : predecessors(BB))
      if ((PredBlock = getEHPadFromPredecessor(PredBlock,
                                               CatchSwitch->getParentPad())))
        calculateCXXStateNumbers(FuncInfo, PredBlock->getFirstNonPHI(),
                                 TryLow);
    int CatchLow = addUnwindMapEntry(FuncInfo, ParentState, nullptr);

    // catchpads are separate funclets in C++ EH due to the way rethrow works.
    int TryHigh = CatchLow - 1;
    for (const auto *CatchPad : Handlers) {
      FuncInfo.FuncletBaseStateMap[CatchPad] = CatchLow;
      for (const User *U : CatchPad->users()) {
        const auto *UserI = cast<Instruction>(U);
        if (auto *InnerCatchSwitch = dyn_cast<CatchSwitchInst>(UserI)) {
          BasicBlock *UnwindDest = InnerCatchSwitch->getUnwindDest();
          if (!UnwindDest || UnwindDest == CatchSwitch->getUnwindDest())
            calculateCXXStateNumbers(FuncInfo, UserI, CatchLow);
        }
        if (auto *InnerCleanupPad = dyn_cast<CleanupPadInst>(UserI)) {
          BasicBlock *UnwindDest = getCleanupRetUnwindDest(InnerCleanupPad);
          // If a nested cleanup pad reports a null unwind destination and the
          // enclosing catch pad doesn't it must be post-dominated by an
          // unreachable instruction.
          if (!UnwindDest || UnwindDest == CatchSwitch->getUnwindDest())
            calculateCXXStateNumbers(FuncInfo, UserI, CatchLow);
        }
      }
    }
    int CatchHigh = FuncInfo.getLastStateNumber();
    addTryBlockMapEntry(FuncInfo, TryLow, TryHigh, CatchHigh, Handlers);
    LLVM_DEBUG(dbgs() << "TryLow[" << BB->getName() << "]: " << TryLow << '\n');
    LLVM_DEBUG(dbgs() << "TryHigh[" << BB->getName() << "]: " << TryHigh
                      << '\n');
    LLVM_DEBUG(dbgs() << "CatchHigh[" << BB->getName() << "]: " << CatchHigh
                      << '\n');
  } else {
    auto *CleanupPad = cast<CleanupPadInst>(FirstNonPHI);

    // It's possible for a cleanup to be visited twice: it might have multiple
    // cleanupret instructions.
    if (FuncInfo.EHPadStateMap.count(CleanupPad))
      return;

    int CleanupState = addUnwindMapEntry(FuncInfo, ParentState, BB);
    FuncInfo.EHPadStateMap[CleanupPad] = CleanupState;
    LLVM_DEBUG(dbgs() << "Assigning state #" << CleanupState << " to BB "
                      << BB->getName() << '\n');
    for (const BasicBlock *PredBlock : predecessors(BB)) {
      if ((PredBlock = getEHPadFromPredecessor(PredBlock,
                                               CleanupPad->getParentPad()))) {
        calculateCXXStateNumbers(FuncInfo, PredBlock->getFirstNonPHI(),
                                 CleanupState);
      }
    }
    for (const User *U : CleanupPad->users()) {
      const auto *UserI = cast<Instruction>(U);
      if (UserI->isEHPad())
        report_fatal_error("Cleanup funclets for the MSVC++ personality cannot "
                           "contain exceptional actions");
    }
  }
}

static int addSEHExcept(WinEHFuncInfo &FuncInfo, int ParentState,
                        const Function *Filter, const BasicBlock *Handler) {
  SEHUnwindMapEntry Entry;
  Entry.ToState = ParentState;
  Entry.IsFinally = false;
  Entry.Filter = Filter;
  Entry.Handler = Handler;
  FuncInfo.SEHUnwindMap.push_back(Entry);
  return FuncInfo.SEHUnwindMap.size() - 1;
}

static int addSEHFinally(WinEHFuncInfo &FuncInfo, int ParentState,
                         const BasicBlock *Handler) {
  SEHUnwindMapEntry Entry;
  Entry.ToState = ParentState;
  Entry.IsFinally = true;
  Entry.Filter = nullptr;
  Entry.Handler = Handler;
  FuncInfo.SEHUnwindMap.push_back(Entry);
  return FuncInfo.SEHUnwindMap.size() - 1;
}

static void calculateSEHStateNumbers(WinEHFuncInfo &FuncInfo,
                                     const Instruction *FirstNonPHI,
                                     int ParentState) {
  const BasicBlock *BB = FirstNonPHI->getParent();
  assert(BB->isEHPad() && "no a funclet!");

  if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(FirstNonPHI)) {
    assert(FuncInfo.EHPadStateMap.count(CatchSwitch) == 0 &&
           "shouldn't revist catch funclets!");

    // Extract the filter function and the __except basic block and create a
    // state for them.
    assert(CatchSwitch->getNumHandlers() == 1 &&
           "SEH doesn't have multiple handlers per __try");
    const auto *CatchPad =
        cast<CatchPadInst>((*CatchSwitch->handler_begin())->getFirstNonPHI());
    const BasicBlock *CatchPadBB = CatchPad->getParent();
    const Constant *FilterOrNull =
        cast<Constant>(CatchPad->getArgOperand(0)->stripPointerCasts());
    const Function *Filter = dyn_cast<Function>(FilterOrNull);
    assert((Filter || FilterOrNull->isNullValue()) &&
           "unexpected filter value");
    int TryState = addSEHExcept(FuncInfo, ParentState, Filter, CatchPadBB);

    // Everything in the __try block uses TryState as its parent state.
    FuncInfo.EHPadStateMap[CatchSwitch] = TryState;
    LLVM_DEBUG(dbgs() << "Assigning state #" << TryState << " to BB "
                      << CatchPadBB->getName() << '\n');
    for (const BasicBlock *PredBlock : predecessors(BB))
      if ((PredBlock = getEHPadFromPredecessor(PredBlock,
                                               CatchSwitch->getParentPad())))
        calculateSEHStateNumbers(FuncInfo, PredBlock->getFirstNonPHI(),
                                 TryState);

    // Everything in the __except block unwinds to ParentState, just like code
    // outside the __try.
    for (const User *U : CatchPad->users()) {
      const auto *UserI = cast<Instruction>(U);
      if (auto *InnerCatchSwitch = dyn_cast<CatchSwitchInst>(UserI)) {
        BasicBlock *UnwindDest = InnerCatchSwitch->getUnwindDest();
        if (!UnwindDest || UnwindDest == CatchSwitch->getUnwindDest())
          calculateSEHStateNumbers(FuncInfo, UserI, ParentState);
      }
      if (auto *InnerCleanupPad = dyn_cast<CleanupPadInst>(UserI)) {
        BasicBlock *UnwindDest = getCleanupRetUnwindDest(InnerCleanupPad);
        // If a nested cleanup pad reports a null unwind destination and the
        // enclosing catch pad doesn't it must be post-dominated by an
        // unreachable instruction.
        if (!UnwindDest || UnwindDest == CatchSwitch->getUnwindDest())
          calculateSEHStateNumbers(FuncInfo, UserI, ParentState);
      }
    }
  } else {
    auto *CleanupPad = cast<CleanupPadInst>(FirstNonPHI);

    // It's possible for a cleanup to be visited twice: it might have multiple
    // cleanupret instructions.
    if (FuncInfo.EHPadStateMap.count(CleanupPad))
      return;

    int CleanupState = addSEHFinally(FuncInfo, ParentState, BB);
    FuncInfo.EHPadStateMap[CleanupPad] = CleanupState;
    LLVM_DEBUG(dbgs() << "Assigning state #" << CleanupState << " to BB "
                      << BB->getName() << '\n');
    for (const BasicBlock *PredBlock : predecessors(BB))
      if ((PredBlock =
               getEHPadFromPredecessor(PredBlock, CleanupPad->getParentPad())))
        calculateSEHStateNumbers(FuncInfo, PredBlock->getFirstNonPHI(),
                                 CleanupState);
    for (const User *U : CleanupPad->users()) {
      const auto *UserI = cast<Instruction>(U);
      if (UserI->isEHPad())
        report_fatal_error("Cleanup funclets for the SEH personality cannot "
                           "contain exceptional actions");
    }
  }
}

static bool isTopLevelPadForMSVC(const Instruction *EHPad) {
  if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(EHPad))
    return isa<ConstantTokenNone>(CatchSwitch->getParentPad()) &&
           CatchSwitch->unwindsToCaller();
  if (auto *CleanupPad = dyn_cast<CleanupPadInst>(EHPad))
    return isa<ConstantTokenNone>(CleanupPad->getParentPad()) &&
           getCleanupRetUnwindDest(CleanupPad) == nullptr;
  if (isa<CatchPadInst>(EHPad))
    return false;
  llvm_unreachable("unexpected EHPad!");
}

void llvm::calculateSEHStateNumbers(const Function *Fn,
                                    WinEHFuncInfo &FuncInfo) {
  // Don't compute state numbers twice.
  if (!FuncInfo.SEHUnwindMap.empty())
    return;

  for (const BasicBlock &BB : *Fn) {
    if (!BB.isEHPad())
      continue;
    const Instruction *FirstNonPHI = BB.getFirstNonPHI();
    if (!isTopLevelPadForMSVC(FirstNonPHI))
      continue;
    ::calculateSEHStateNumbers(FuncInfo, FirstNonPHI, -1);
  }

  calculateStateNumbersForInvokes(Fn, FuncInfo);
}

void llvm::calculateWinCXXEHStateNumbers(const Function *Fn,
                                         WinEHFuncInfo &FuncInfo) {
  // Return if it's already been done.
  if (!FuncInfo.EHPadStateMap.empty())
    return;

  for (const BasicBlock &BB : *Fn) {
    if (!BB.isEHPad())
      continue;
    const Instruction *FirstNonPHI = BB.getFirstNonPHI();
    if (!isTopLevelPadForMSVC(FirstNonPHI))
      continue;
    calculateCXXStateNumbers(FuncInfo, FirstNonPHI, -1);
  }

  calculateStateNumbersForInvokes(Fn, FuncInfo);
}

static int addClrEHHandler(WinEHFuncInfo &FuncInfo, int HandlerParentState,
                           int TryParentState, ClrHandlerType HandlerType,
                           uint32_t TypeToken, const BasicBlock *Handler) {
  ClrEHUnwindMapEntry Entry;
  Entry.HandlerParentState = HandlerParentState;
  Entry.TryParentState = TryParentState;
  Entry.Handler = Handler;
  Entry.HandlerType = HandlerType;
  Entry.TypeToken = TypeToken;
  FuncInfo.ClrEHUnwindMap.push_back(Entry);
  return FuncInfo.ClrEHUnwindMap.size() - 1;
}

void llvm::calculateClrEHStateNumbers(const Function *Fn,
                                      WinEHFuncInfo &FuncInfo) {
  // Return if it's already been done.
  if (!FuncInfo.EHPadStateMap.empty())
    return;

  // This numbering assigns one state number to each catchpad and cleanuppad.
  // It also computes two tree-like relations over states:
  // 1) Each state has a "HandlerParentState", which is the state of the next
  //    outer handler enclosing this state's handler (same as nearest ancestor
  //    per the ParentPad linkage on EH pads, but skipping over catchswitches).
  // 2) Each state has a "TryParentState", which:
  //    a) for a catchpad that's not the last handler on its catchswitch, is
  //       the state of the next catchpad on that catchswitch
  //    b) for all other pads, is the state of the pad whose try region is the
  //       next outer try region enclosing this state's try region.  The "try
  //       regions are not present as such in the IR, but will be inferred
  //       based on the placement of invokes and pads which reach each other
  //       by exceptional exits
  // Catchswitches do not get their own states, but each gets mapped to the
  // state of its first catchpad.

  // Step one: walk down from outermost to innermost funclets, assigning each
  // catchpad and cleanuppad a state number.  Add an entry to the
  // ClrEHUnwindMap for each state, recording its HandlerParentState and
  // handler attributes.  Record the TryParentState as well for each catchpad
  // that's not the last on its catchswitch, but initialize all other entries'
  // TryParentStates to a sentinel -1 value that the next pass will update.

  // Seed a worklist with pads that have no parent.
  SmallVector<std::pair<const Instruction *, int>, 8> Worklist;
  for (const BasicBlock &BB : *Fn) {
    const Instruction *FirstNonPHI = BB.getFirstNonPHI();
    const Value *ParentPad;
    if (const auto *CPI = dyn_cast<CleanupPadInst>(FirstNonPHI))
      ParentPad = CPI->getParentPad();
    else if (const auto *CSI = dyn_cast<CatchSwitchInst>(FirstNonPHI))
      ParentPad = CSI->getParentPad();
    else
      continue;
    if (isa<ConstantTokenNone>(ParentPad))
      Worklist.emplace_back(FirstNonPHI, -1);
  }

  // Use the worklist to visit all pads, from outer to inner.  Record
  // HandlerParentState for all pads.  Record TryParentState only for catchpads
  // that aren't the last on their catchswitch (setting all other entries'
  // TryParentStates to an initial value of -1).  This loop is also responsible
  // for setting the EHPadStateMap entry for all catchpads, cleanuppads, and
  // catchswitches.
  while (!Worklist.empty()) {
    const Instruction *Pad;
    int HandlerParentState;
    std::tie(Pad, HandlerParentState) = Worklist.pop_back_val();

    if (const auto *Cleanup = dyn_cast<CleanupPadInst>(Pad)) {
      // Create the entry for this cleanup with the appropriate handler
      // properties.  Finally and fault handlers are distinguished by arity.
      ClrHandlerType HandlerType =
          (Cleanup->getNumArgOperands() ? ClrHandlerType::Fault
                                        : ClrHandlerType::Finally);
      int CleanupState = addClrEHHandler(FuncInfo, HandlerParentState, -1,
                                         HandlerType, 0, Pad->getParent());
      // Queue any child EH pads on the worklist.
      for (const User *U : Cleanup->users())
        if (const auto *I = dyn_cast<Instruction>(U))
          if (I->isEHPad())
            Worklist.emplace_back(I, CleanupState);
      // Remember this pad's state.
      FuncInfo.EHPadStateMap[Cleanup] = CleanupState;
    } else {
      // Walk the handlers of this catchswitch in reverse order since all but
      // the last need to set the following one as its TryParentState.
      const auto *CatchSwitch = cast<CatchSwitchInst>(Pad);
      int CatchState = -1, FollowerState = -1;
      SmallVector<const BasicBlock *, 4> CatchBlocks(CatchSwitch->handlers());
      for (auto CBI = CatchBlocks.rbegin(), CBE = CatchBlocks.rend();
           CBI != CBE; ++CBI, FollowerState = CatchState) {
        const BasicBlock *CatchBlock = *CBI;
        // Create the entry for this catch with the appropriate handler
        // properties.
        const auto *Catch = cast<CatchPadInst>(CatchBlock->getFirstNonPHI());
        uint32_t TypeToken = static_cast<uint32_t>(
            cast<ConstantInt>(Catch->getArgOperand(0))->getZExtValue());
        CatchState =
            addClrEHHandler(FuncInfo, HandlerParentState, FollowerState,
                            ClrHandlerType::Catch, TypeToken, CatchBlock);
        // Queue any child EH pads on the worklist.
        for (const User *U : Catch->users())
          if (const auto *I = dyn_cast<Instruction>(U))
            if (I->isEHPad())
              Worklist.emplace_back(I, CatchState);
        // Remember this catch's state.
        FuncInfo.EHPadStateMap[Catch] = CatchState;
      }
      // Associate the catchswitch with the state of its first catch.
      assert(CatchSwitch->getNumHandlers());
      FuncInfo.EHPadStateMap[CatchSwitch] = CatchState;
    }
  }

  // Step two: record the TryParentState of each state.  For cleanuppads that
  // don't have cleanuprets, we may need to infer this from their child pads,
  // so visit pads in descendant-most to ancestor-most order.
  for (auto Entry = FuncInfo.ClrEHUnwindMap.rbegin(),
            End = FuncInfo.ClrEHUnwindMap.rend();
       Entry != End; ++Entry) {
    const Instruction *Pad =
        Entry->Handler.get<const BasicBlock *>()->getFirstNonPHI();
    // For most pads, the TryParentState is the state associated with the
    // unwind dest of exceptional exits from it.
    const BasicBlock *UnwindDest;
    if (const auto *Catch = dyn_cast<CatchPadInst>(Pad)) {
      // If a catch is not the last in its catchswitch, its TryParentState is
      // the state associated with the next catch in the switch, even though
      // that's not the unwind dest of exceptions escaping the catch.  Those
      // cases were already assigned a TryParentState in the first pass, so
      // skip them.
      if (Entry->TryParentState != -1)
        continue;
      // Otherwise, get the unwind dest from the catchswitch.
      UnwindDest = Catch->getCatchSwitch()->getUnwindDest();
    } else {
      const auto *Cleanup = cast<CleanupPadInst>(Pad);
      UnwindDest = nullptr;
      for (const User *U : Cleanup->users()) {
        if (auto *CleanupRet = dyn_cast<CleanupReturnInst>(U)) {
          // Common and unambiguous case -- cleanupret indicates cleanup's
          // unwind dest.
          UnwindDest = CleanupRet->getUnwindDest();
          break;
        }

        // Get an unwind dest for the user
        const BasicBlock *UserUnwindDest = nullptr;
        if (auto *Invoke = dyn_cast<InvokeInst>(U)) {
          UserUnwindDest = Invoke->getUnwindDest();
        } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(U)) {
          UserUnwindDest = CatchSwitch->getUnwindDest();
        } else if (auto *ChildCleanup = dyn_cast<CleanupPadInst>(U)) {
          int UserState = FuncInfo.EHPadStateMap[ChildCleanup];
          int UserUnwindState =
              FuncInfo.ClrEHUnwindMap[UserState].TryParentState;
          if (UserUnwindState != -1)
            UserUnwindDest = FuncInfo.ClrEHUnwindMap[UserUnwindState]
                                 .Handler.get<const BasicBlock *>();
        }

        // Not having an unwind dest for this user might indicate that it
        // doesn't unwind, so can't be taken as proof that the cleanup itself
        // may unwind to caller (see e.g. SimplifyUnreachable and
        // RemoveUnwindEdge).
        if (!UserUnwindDest)
          continue;

        // Now we have an unwind dest for the user, but we need to see if it
        // unwinds all the way out of the cleanup or if it stays within it.
        const Instruction *UserUnwindPad = UserUnwindDest->getFirstNonPHI();
        const Value *UserUnwindParent;
        if (auto *CSI = dyn_cast<CatchSwitchInst>(UserUnwindPad))
          UserUnwindParent = CSI->getParentPad();
        else
          UserUnwindParent =
              cast<CleanupPadInst>(UserUnwindPad)->getParentPad();

        // The unwind stays within the cleanup iff it targets a child of the
        // cleanup.
        if (UserUnwindParent == Cleanup)
          continue;

        // This unwind exits the cleanup, so its dest is the cleanup's dest.
        UnwindDest = UserUnwindDest;
        break;
      }
    }

    // Record the state of the unwind dest as the TryParentState.
    int UnwindDestState;

    // If UnwindDest is null at this point, either the pad in question can
    // be exited by unwind to caller, or it cannot be exited by unwind.  In
    // either case, reporting such cases as unwinding to caller is correct.
    // This can lead to EH tables that "look strange" -- if this pad's is in
    // a parent funclet which has other children that do unwind to an enclosing
    // pad, the try region for this pad will be missing the "duplicate" EH
    // clause entries that you'd expect to see covering the whole parent.  That
    // should be benign, since the unwind never actually happens.  If it were
    // an issue, we could add a subsequent pass that pushes unwind dests down
    // from parents that have them to children that appear to unwind to caller.
    if (!UnwindDest) {
      UnwindDestState = -1;
    } else {
      UnwindDestState = FuncInfo.EHPadStateMap[UnwindDest->getFirstNonPHI()];
    }

    Entry->TryParentState = UnwindDestState;
  }

  // Step three: transfer information from pads to invokes.
  calculateStateNumbersForInvokes(Fn, FuncInfo);
}

void WinEHPrepare::colorFunclets(Function &F) {
  BlockColors = colorEHFunclets(F);

  // Invert the map from BB to colors to color to BBs.
  for (BasicBlock &BB : F) {
    ColorVector &Colors = BlockColors[&BB];
    for (BasicBlock *Color : Colors)
      FuncletBlocks[Color].push_back(&BB);
  }
}

void WinEHPrepare::demotePHIsOnFunclets(Function &F,
                                        bool DemoteCatchSwitchPHIOnly) {
  // Strip PHI nodes off of EH pads.
  SmallVector<PHINode *, 16> PHINodes;
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE;) {
    BasicBlock *BB = &*FI++;
    if (!BB->isEHPad())
      continue;
    if (DemoteCatchSwitchPHIOnly && !isa<CatchSwitchInst>(BB->getFirstNonPHI()))
      continue;

    for (BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE;) {
      Instruction *I = &*BI++;
      auto *PN = dyn_cast<PHINode>(I);
      // Stop at the first non-PHI.
      if (!PN)
        break;

      AllocaInst *SpillSlot = insertPHILoads(PN, F);
      if (SpillSlot)
        insertPHIStores(PN, SpillSlot);

      PHINodes.push_back(PN);
    }
  }

  for (auto *PN : PHINodes) {
    // There may be lingering uses on other EH PHIs being removed
    PN->replaceAllUsesWith(UndefValue::get(PN->getType()));
    PN->eraseFromParent();
  }
}

void WinEHPrepare::cloneCommonBlocks(Function &F) {
  // We need to clone all blocks which belong to multiple funclets.  Values are
  // remapped throughout the funclet to propagate both the new instructions
  // *and* the new basic blocks themselves.
  for (auto &Funclets : FuncletBlocks) {
    BasicBlock *FuncletPadBB = Funclets.first;
    std::vector<BasicBlock *> &BlocksInFunclet = Funclets.second;
    Value *FuncletToken;
    if (FuncletPadBB == &F.getEntryBlock())
      FuncletToken = ConstantTokenNone::get(F.getContext());
    else
      FuncletToken = FuncletPadBB->getFirstNonPHI();

    std::vector<std::pair<BasicBlock *, BasicBlock *>> Orig2Clone;
    ValueToValueMapTy VMap;
    for (BasicBlock *BB : BlocksInFunclet) {
      ColorVector &ColorsForBB = BlockColors[BB];
      // We don't need to do anything if the block is monochromatic.
      size_t NumColorsForBB = ColorsForBB.size();
      if (NumColorsForBB == 1)
        continue;

      DEBUG_WITH_TYPE("winehprepare-coloring",
                      dbgs() << "  Cloning block \'" << BB->getName()
                              << "\' for funclet \'" << FuncletPadBB->getName()
                              << "\'.\n");

      // Create a new basic block and copy instructions into it!
      BasicBlock *CBB =
          CloneBasicBlock(BB, VMap, Twine(".for.", FuncletPadBB->getName()));
      // Insert the clone immediately after the original to ensure determinism
      // and to keep the same relative ordering of any funclet's blocks.
      CBB->insertInto(&F, BB->getNextNode());

      // Add basic block mapping.
      VMap[BB] = CBB;

      // Record delta operations that we need to perform to our color mappings.
      Orig2Clone.emplace_back(BB, CBB);
    }

    // If nothing was cloned, we're done cloning in this funclet.
    if (Orig2Clone.empty())
      continue;

    // Update our color mappings to reflect that one block has lost a color and
    // another has gained a color.
    for (auto &BBMapping : Orig2Clone) {
      BasicBlock *OldBlock = BBMapping.first;
      BasicBlock *NewBlock = BBMapping.second;

      BlocksInFunclet.push_back(NewBlock);
      ColorVector &NewColors = BlockColors[NewBlock];
      assert(NewColors.empty() && "A new block should only have one color!");
      NewColors.push_back(FuncletPadBB);

      DEBUG_WITH_TYPE("winehprepare-coloring",
                      dbgs() << "  Assigned color \'" << FuncletPadBB->getName()
                              << "\' to block \'" << NewBlock->getName()
                              << "\'.\n");

      BlocksInFunclet.erase(
          std::remove(BlocksInFunclet.begin(), BlocksInFunclet.end(), OldBlock),
          BlocksInFunclet.end());
      ColorVector &OldColors = BlockColors[OldBlock];
      OldColors.erase(
          std::remove(OldColors.begin(), OldColors.end(), FuncletPadBB),
          OldColors.end());

      DEBUG_WITH_TYPE("winehprepare-coloring",
                      dbgs() << "  Removed color \'" << FuncletPadBB->getName()
                              << "\' from block \'" << OldBlock->getName()
                              << "\'.\n");
    }

    // Loop over all of the instructions in this funclet, fixing up operand
    // references as we go.  This uses VMap to do all the hard work.
    for (BasicBlock *BB : BlocksInFunclet)
      // Loop over all instructions, fixing each one as we find it...
      for (Instruction &I : *BB)
        RemapInstruction(&I, VMap,
                         RF_IgnoreMissingLocals | RF_NoModuleLevelChanges);

    // Catchrets targeting cloned blocks need to be updated separately from
    // the loop above because they are not in the current funclet.
    SmallVector<CatchReturnInst *, 2> FixupCatchrets;
    for (auto &BBMapping : Orig2Clone) {
      BasicBlock *OldBlock = BBMapping.first;
      BasicBlock *NewBlock = BBMapping.second;

      FixupCatchrets.clear();
      for (BasicBlock *Pred : predecessors(OldBlock))
        if (auto *CatchRet = dyn_cast<CatchReturnInst>(Pred->getTerminator()))
          if (CatchRet->getCatchSwitchParentPad() == FuncletToken)
            FixupCatchrets.push_back(CatchRet);

      for (CatchReturnInst *CatchRet : FixupCatchrets)
        CatchRet->setSuccessor(NewBlock);
    }

    auto UpdatePHIOnClonedBlock = [&](PHINode *PN, bool IsForOldBlock) {
      unsigned NumPreds = PN->getNumIncomingValues();
      for (unsigned PredIdx = 0, PredEnd = NumPreds; PredIdx != PredEnd;
           ++PredIdx) {
        BasicBlock *IncomingBlock = PN->getIncomingBlock(PredIdx);
        bool EdgeTargetsFunclet;
        if (auto *CRI =
                dyn_cast<CatchReturnInst>(IncomingBlock->getTerminator())) {
          EdgeTargetsFunclet = (CRI->getCatchSwitchParentPad() == FuncletToken);
        } else {
          ColorVector &IncomingColors = BlockColors[IncomingBlock];
          assert(!IncomingColors.empty() && "Block not colored!");
          assert((IncomingColors.size() == 1 ||
                  llvm::all_of(IncomingColors,
                               [&](BasicBlock *Color) {
                                 return Color != FuncletPadBB;
                               })) &&
                 "Cloning should leave this funclet's blocks monochromatic");
          EdgeTargetsFunclet = (IncomingColors.front() == FuncletPadBB);
        }
        if (IsForOldBlock != EdgeTargetsFunclet)
          continue;
        PN->removeIncomingValue(IncomingBlock, /*DeletePHIIfEmpty=*/false);
        // Revisit the next entry.
        --PredIdx;
        --PredEnd;
      }
    };

    for (auto &BBMapping : Orig2Clone) {
      BasicBlock *OldBlock = BBMapping.first;
      BasicBlock *NewBlock = BBMapping.second;
      for (PHINode &OldPN : OldBlock->phis()) {
        UpdatePHIOnClonedBlock(&OldPN, /*IsForOldBlock=*/true);
      }
      for (PHINode &NewPN : NewBlock->phis()) {
        UpdatePHIOnClonedBlock(&NewPN, /*IsForOldBlock=*/false);
      }
    }

    // Check to see if SuccBB has PHI nodes. If so, we need to add entries to
    // the PHI nodes for NewBB now.
    for (auto &BBMapping : Orig2Clone) {
      BasicBlock *OldBlock = BBMapping.first;
      BasicBlock *NewBlock = BBMapping.second;
      for (BasicBlock *SuccBB : successors(NewBlock)) {
        for (PHINode &SuccPN : SuccBB->phis()) {
          // Ok, we have a PHI node.  Figure out what the incoming value was for
          // the OldBlock.
          int OldBlockIdx = SuccPN.getBasicBlockIndex(OldBlock);
          if (OldBlockIdx == -1)
            break;
          Value *IV = SuccPN.getIncomingValue(OldBlockIdx);

          // Remap the value if necessary.
          if (auto *Inst = dyn_cast<Instruction>(IV)) {
            ValueToValueMapTy::iterator I = VMap.find(Inst);
            if (I != VMap.end())
              IV = I->second;
          }

          SuccPN.addIncoming(IV, NewBlock);
        }
      }
    }

    for (ValueToValueMapTy::value_type VT : VMap) {
      // If there were values defined in BB that are used outside the funclet,
      // then we now have to update all uses of the value to use either the
      // original value, the cloned value, or some PHI derived value.  This can
      // require arbitrary PHI insertion, of which we are prepared to do, clean
      // these up now.
      SmallVector<Use *, 16> UsesToRename;

      auto *OldI = dyn_cast<Instruction>(const_cast<Value *>(VT.first));
      if (!OldI)
        continue;
      auto *NewI = cast<Instruction>(VT.second);
      // Scan all uses of this instruction to see if it is used outside of its
      // funclet, and if so, record them in UsesToRename.
      for (Use &U : OldI->uses()) {
        Instruction *UserI = cast<Instruction>(U.getUser());
        BasicBlock *UserBB = UserI->getParent();
        ColorVector &ColorsForUserBB = BlockColors[UserBB];
        assert(!ColorsForUserBB.empty());
        if (ColorsForUserBB.size() > 1 ||
            *ColorsForUserBB.begin() != FuncletPadBB)
          UsesToRename.push_back(&U);
      }

      // If there are no uses outside the block, we're done with this
      // instruction.
      if (UsesToRename.empty())
        continue;

      // We found a use of OldI outside of the funclet.  Rename all uses of OldI
      // that are outside its funclet to be uses of the appropriate PHI node
      // etc.
      SSAUpdater SSAUpdate;
      SSAUpdate.Initialize(OldI->getType(), OldI->getName());
      SSAUpdate.AddAvailableValue(OldI->getParent(), OldI);
      SSAUpdate.AddAvailableValue(NewI->getParent(), NewI);

      while (!UsesToRename.empty())
        SSAUpdate.RewriteUseAfterInsertions(*UsesToRename.pop_back_val());
    }
  }
}

void WinEHPrepare::removeImplausibleInstructions(Function &F) {
  // Remove implausible terminators and replace them with UnreachableInst.
  for (auto &Funclet : FuncletBlocks) {
    BasicBlock *FuncletPadBB = Funclet.first;
    std::vector<BasicBlock *> &BlocksInFunclet = Funclet.second;
    Instruction *FirstNonPHI = FuncletPadBB->getFirstNonPHI();
    auto *FuncletPad = dyn_cast<FuncletPadInst>(FirstNonPHI);
    auto *CatchPad = dyn_cast_or_null<CatchPadInst>(FuncletPad);
    auto *CleanupPad = dyn_cast_or_null<CleanupPadInst>(FuncletPad);

    for (BasicBlock *BB : BlocksInFunclet) {
      for (Instruction &I : *BB) {
        CallSite CS(&I);
        if (!CS)
          continue;

        Value *FuncletBundleOperand = nullptr;
        if (auto BU = CS.getOperandBundle(LLVMContext::OB_funclet))
          FuncletBundleOperand = BU->Inputs.front();

        if (FuncletBundleOperand == FuncletPad)
          continue;

        // Skip call sites which are nounwind intrinsics or inline asm.
        auto *CalledFn =
            dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
        if (CalledFn && ((CalledFn->isIntrinsic() && CS.doesNotThrow()) ||
                         CS.isInlineAsm()))
          continue;

        // This call site was not part of this funclet, remove it.
        if (CS.isInvoke()) {
          // Remove the unwind edge if it was an invoke.
          removeUnwindEdge(BB);
          // Get a pointer to the new call.
          BasicBlock::iterator CallI =
              std::prev(BB->getTerminator()->getIterator());
          auto *CI = cast<CallInst>(&*CallI);
          changeToUnreachable(CI, /*UseLLVMTrap=*/false);
        } else {
          changeToUnreachable(&I, /*UseLLVMTrap=*/false);
        }

        // There are no more instructions in the block (except for unreachable),
        // we are done.
        break;
      }

      Instruction *TI = BB->getTerminator();
      // CatchPadInst and CleanupPadInst can't transfer control to a ReturnInst.
      bool IsUnreachableRet = isa<ReturnInst>(TI) && FuncletPad;
      // The token consumed by a CatchReturnInst must match the funclet token.
      bool IsUnreachableCatchret = false;
      if (auto *CRI = dyn_cast<CatchReturnInst>(TI))
        IsUnreachableCatchret = CRI->getCatchPad() != CatchPad;
      // The token consumed by a CleanupReturnInst must match the funclet token.
      bool IsUnreachableCleanupret = false;
      if (auto *CRI = dyn_cast<CleanupReturnInst>(TI))
        IsUnreachableCleanupret = CRI->getCleanupPad() != CleanupPad;
      if (IsUnreachableRet || IsUnreachableCatchret ||
          IsUnreachableCleanupret) {
        changeToUnreachable(TI, /*UseLLVMTrap=*/false);
      } else if (isa<InvokeInst>(TI)) {
        if (Personality == EHPersonality::MSVC_CXX && CleanupPad) {
          // Invokes within a cleanuppad for the MSVC++ personality never
          // transfer control to their unwind edge: the personality will
          // terminate the program.
          removeUnwindEdge(BB);
        }
      }
    }
  }
}

void WinEHPrepare::cleanupPreparedFunclets(Function &F) {
  // Clean-up some of the mess we made by removing useles PHI nodes, trivial
  // branches, etc.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE;) {
    BasicBlock *BB = &*FI++;
    SimplifyInstructionsInBlock(BB);
    ConstantFoldTerminator(BB, /*DeleteDeadConditions=*/true);
    MergeBlockIntoPredecessor(BB);
  }

  // We might have some unreachable blocks after cleaning up some impossible
  // control flow.
  removeUnreachableBlocks(F);
}

#ifndef NDEBUG
void WinEHPrepare::verifyPreparedFunclets(Function &F) {
  for (BasicBlock &BB : F) {
    size_t NumColors = BlockColors[&BB].size();
    assert(NumColors == 1 && "Expected monochromatic BB!");
    if (NumColors == 0)
      report_fatal_error("Uncolored BB!");
    if (NumColors > 1)
      report_fatal_error("Multicolor BB!");
    assert((DisableDemotion || !(BB.isEHPad() && isa<PHINode>(BB.begin()))) &&
           "EH Pad still has a PHI!");
  }
}
#endif

bool WinEHPrepare::prepareExplicitEH(Function &F) {
  // Remove unreachable blocks.  It is not valuable to assign them a color and
  // their existence can trick us into thinking values are alive when they are
  // not.
  removeUnreachableBlocks(F);

  // Determine which blocks are reachable from which funclet entries.
  colorFunclets(F);

  cloneCommonBlocks(F);

  if (!DisableDemotion)
    demotePHIsOnFunclets(F, DemoteCatchSwitchPHIOnly ||
                                DemoteCatchSwitchPHIOnlyOpt);

  if (!DisableCleanups) {
    LLVM_DEBUG(verifyFunction(F));
    removeImplausibleInstructions(F);

    LLVM_DEBUG(verifyFunction(F));
    cleanupPreparedFunclets(F);
  }

  LLVM_DEBUG(verifyPreparedFunclets(F));
  // Recolor the CFG to verify that all is well.
  LLVM_DEBUG(colorFunclets(F));
  LLVM_DEBUG(verifyPreparedFunclets(F));

  BlockColors.clear();
  FuncletBlocks.clear();

  return true;
}

// TODO: Share loads when one use dominates another, or when a catchpad exit
// dominates uses (needs dominators).
AllocaInst *WinEHPrepare::insertPHILoads(PHINode *PN, Function &F) {
  BasicBlock *PHIBlock = PN->getParent();
  AllocaInst *SpillSlot = nullptr;
  Instruction *EHPad = PHIBlock->getFirstNonPHI();

  if (!EHPad->isTerminator()) {
    // If the EHPad isn't a terminator, then we can insert a load in this block
    // that will dominate all uses.
    SpillSlot = new AllocaInst(PN->getType(), DL->getAllocaAddrSpace(), nullptr,
                               Twine(PN->getName(), ".wineh.spillslot"),
                               &F.getEntryBlock().front());
    Value *V = new LoadInst(SpillSlot, Twine(PN->getName(), ".wineh.reload"),
                            &*PHIBlock->getFirstInsertionPt());
    PN->replaceAllUsesWith(V);
    return SpillSlot;
  }

  // Otherwise, we have a PHI on a terminator EHPad, and we give up and insert
  // loads of the slot before every use.
  DenseMap<BasicBlock *, Value *> Loads;
  for (Value::use_iterator UI = PN->use_begin(), UE = PN->use_end();
       UI != UE;) {
    Use &U = *UI++;
    auto *UsingInst = cast<Instruction>(U.getUser());
    if (isa<PHINode>(UsingInst) && UsingInst->getParent()->isEHPad()) {
      // Use is on an EH pad phi.  Leave it alone; we'll insert loads and
      // stores for it separately.
      continue;
    }
    replaceUseWithLoad(PN, U, SpillSlot, Loads, F);
  }
  return SpillSlot;
}

// TODO: improve store placement.  Inserting at def is probably good, but need
// to be careful not to introduce interfering stores (needs liveness analysis).
// TODO: identify related phi nodes that can share spill slots, and share them
// (also needs liveness).
void WinEHPrepare::insertPHIStores(PHINode *OriginalPHI,
                                   AllocaInst *SpillSlot) {
  // Use a worklist of (Block, Value) pairs -- the given Value needs to be
  // stored to the spill slot by the end of the given Block.
  SmallVector<std::pair<BasicBlock *, Value *>, 4> Worklist;

  Worklist.push_back({OriginalPHI->getParent(), OriginalPHI});

  while (!Worklist.empty()) {
    BasicBlock *EHBlock;
    Value *InVal;
    std::tie(EHBlock, InVal) = Worklist.pop_back_val();

    PHINode *PN = dyn_cast<PHINode>(InVal);
    if (PN && PN->getParent() == EHBlock) {
      // The value is defined by another PHI we need to remove, with no room to
      // insert a store after the PHI, so each predecessor needs to store its
      // incoming value.
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
        Value *PredVal = PN->getIncomingValue(i);

        // Undef can safely be skipped.
        if (isa<UndefValue>(PredVal))
          continue;

        insertPHIStore(PN->getIncomingBlock(i), PredVal, SpillSlot, Worklist);
      }
    } else {
      // We need to store InVal, which dominates EHBlock, but can't put a store
      // in EHBlock, so need to put stores in each predecessor.
      for (BasicBlock *PredBlock : predecessors(EHBlock)) {
        insertPHIStore(PredBlock, InVal, SpillSlot, Worklist);
      }
    }
  }
}

void WinEHPrepare::insertPHIStore(
    BasicBlock *PredBlock, Value *PredVal, AllocaInst *SpillSlot,
    SmallVectorImpl<std::pair<BasicBlock *, Value *>> &Worklist) {

  if (PredBlock->isEHPad() && PredBlock->getFirstNonPHI()->isTerminator()) {
    // Pred is unsplittable, so we need to queue it on the worklist.
    Worklist.push_back({PredBlock, PredVal});
    return;
  }

  // Otherwise, insert the store at the end of the basic block.
  new StoreInst(PredVal, SpillSlot, PredBlock->getTerminator());
}

void WinEHPrepare::replaceUseWithLoad(Value *V, Use &U, AllocaInst *&SpillSlot,
                                      DenseMap<BasicBlock *, Value *> &Loads,
                                      Function &F) {
  // Lazilly create the spill slot.
  if (!SpillSlot)
    SpillSlot = new AllocaInst(V->getType(), DL->getAllocaAddrSpace(), nullptr,
                               Twine(V->getName(), ".wineh.spillslot"),
                               &F.getEntryBlock().front());

  auto *UsingInst = cast<Instruction>(U.getUser());
  if (auto *UsingPHI = dyn_cast<PHINode>(UsingInst)) {
    // If this is a PHI node, we can't insert a load of the value before
    // the use.  Instead insert the load in the predecessor block
    // corresponding to the incoming value.
    //
    // Note that if there are multiple edges from a basic block to this
    // PHI node that we cannot have multiple loads.  The problem is that
    // the resulting PHI node will have multiple values (from each load)
    // coming in from the same block, which is illegal SSA form.
    // For this reason, we keep track of and reuse loads we insert.
    BasicBlock *IncomingBlock = UsingPHI->getIncomingBlock(U);
    if (auto *CatchRet =
            dyn_cast<CatchReturnInst>(IncomingBlock->getTerminator())) {
      // Putting a load above a catchret and use on the phi would still leave
      // a cross-funclet def/use.  We need to split the edge, change the
      // catchret to target the new block, and put the load there.
      BasicBlock *PHIBlock = UsingInst->getParent();
      BasicBlock *NewBlock = SplitEdge(IncomingBlock, PHIBlock);
      // SplitEdge gives us:
      //   IncomingBlock:
      //     ...
      //     br label %NewBlock
      //   NewBlock:
      //     catchret label %PHIBlock
      // But we need:
      //   IncomingBlock:
      //     ...
      //     catchret label %NewBlock
      //   NewBlock:
      //     br label %PHIBlock
      // So move the terminators to each others' blocks and swap their
      // successors.
      BranchInst *Goto = cast<BranchInst>(IncomingBlock->getTerminator());
      Goto->removeFromParent();
      CatchRet->removeFromParent();
      IncomingBlock->getInstList().push_back(CatchRet);
      NewBlock->getInstList().push_back(Goto);
      Goto->setSuccessor(0, PHIBlock);
      CatchRet->setSuccessor(NewBlock);
      // Update the color mapping for the newly split edge.
      // Grab a reference to the ColorVector to be inserted before getting the
      // reference to the vector we are copying because inserting the new
      // element in BlockColors might cause the map to be reallocated.
      ColorVector &ColorsForNewBlock = BlockColors[NewBlock];
      ColorVector &ColorsForPHIBlock = BlockColors[PHIBlock];
      ColorsForNewBlock = ColorsForPHIBlock;
      for (BasicBlock *FuncletPad : ColorsForPHIBlock)
        FuncletBlocks[FuncletPad].push_back(NewBlock);
      // Treat the new block as incoming for load insertion.
      IncomingBlock = NewBlock;
    }
    Value *&Load = Loads[IncomingBlock];
    // Insert the load into the predecessor block
    if (!Load)
      Load = new LoadInst(SpillSlot, Twine(V->getName(), ".wineh.reload"),
                          /*Volatile=*/false, IncomingBlock->getTerminator());

    U.set(Load);
  } else {
    // Reload right before the old use.
    auto *Load = new LoadInst(SpillSlot, Twine(V->getName(), ".wineh.reload"),
                              /*Volatile=*/false, UsingInst);
    U.set(Load);
  }
}

void WinEHFuncInfo::addIPToStateRange(const InvokeInst *II,
                                      MCSymbol *InvokeBegin,
                                      MCSymbol *InvokeEnd) {
  assert(InvokeStateMap.count(II) &&
         "should get invoke with precomputed state");
  LabelToStateMap[InvokeBegin] = std::make_pair(InvokeStateMap[II], InvokeEnd);
}

WinEHFuncInfo::WinEHFuncInfo() {}
