//===- SelectionDAGISel.cpp - Implement the SelectionDAGISel class --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the SelectionDAGISel class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SelectionDAGISel.h"
#include "ScheduleDAGSDNodes.h"
#include "SelectionDAGBuilder.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "isel"

STATISTIC(NumFastIselFailures, "Number of instructions fast isel failed on");
STATISTIC(NumFastIselSuccess, "Number of instructions fast isel selected");
STATISTIC(NumFastIselBlocks, "Number of blocks selected entirely by fast isel");
STATISTIC(NumDAGBlocks, "Number of blocks selected using DAG");
STATISTIC(NumDAGIselRetries,"Number of times dag isel has to try another path");
STATISTIC(NumEntryBlocks, "Number of entry blocks encountered");
STATISTIC(NumFastIselFailLowerArguments,
          "Number of entry blocks where fast isel failed to lower arguments");

static cl::opt<int> EnableFastISelAbort(
    "fast-isel-abort", cl::Hidden,
    cl::desc("Enable abort calls when \"fast\" instruction selection "
             "fails to lower an instruction: 0 disable the abort, 1 will "
             "abort but for args, calls and terminators, 2 will also "
             "abort for argument lowering, and 3 will never fallback "
             "to SelectionDAG."));

static cl::opt<bool> EnableFastISelFallbackReport(
    "fast-isel-report-on-fallback", cl::Hidden,
    cl::desc("Emit a diagnostic when \"fast\" instruction selection "
             "falls back to SelectionDAG."));

static cl::opt<bool>
UseMBPI("use-mbpi",
        cl::desc("use Machine Branch Probability Info"),
        cl::init(true), cl::Hidden);

#ifndef NDEBUG
static cl::opt<std::string>
FilterDAGBasicBlockName("filter-view-dags", cl::Hidden,
                        cl::desc("Only display the basic block whose name "
                                 "matches this for all view-*-dags options"));
static cl::opt<bool>
ViewDAGCombine1("view-dag-combine1-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before the first "
                   "dag combine pass"));
static cl::opt<bool>
ViewLegalizeTypesDAGs("view-legalize-types-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before legalize types"));
static cl::opt<bool>
ViewLegalizeDAGs("view-legalize-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before legalize"));
static cl::opt<bool>
ViewDAGCombine2("view-dag-combine2-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before the second "
                   "dag combine pass"));
static cl::opt<bool>
ViewDAGCombineLT("view-dag-combine-lt-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before the post legalize types"
                   " dag combine pass"));
static cl::opt<bool>
ViewISelDAGs("view-isel-dags", cl::Hidden,
          cl::desc("Pop up a window to show isel dags as they are selected"));
static cl::opt<bool>
ViewSchedDAGs("view-sched-dags", cl::Hidden,
          cl::desc("Pop up a window to show sched dags as they are processed"));
static cl::opt<bool>
ViewSUnitDAGs("view-sunit-dags", cl::Hidden,
      cl::desc("Pop up a window to show SUnit dags after they are processed"));
#else
static const bool ViewDAGCombine1 = false,
                  ViewLegalizeTypesDAGs = false, ViewLegalizeDAGs = false,
                  ViewDAGCombine2 = false,
                  ViewDAGCombineLT = false,
                  ViewISelDAGs = false, ViewSchedDAGs = false,
                  ViewSUnitDAGs = false;
#endif

//===---------------------------------------------------------------------===//
///
/// RegisterScheduler class - Track the registration of instruction schedulers.
///
//===---------------------------------------------------------------------===//
MachinePassRegistry<RegisterScheduler::FunctionPassCtor>
    RegisterScheduler::Registry;

//===---------------------------------------------------------------------===//
///
/// ISHeuristic command line option for instruction schedulers.
///
//===---------------------------------------------------------------------===//
static cl::opt<RegisterScheduler::FunctionPassCtor, false,
               RegisterPassParser<RegisterScheduler>>
ISHeuristic("pre-RA-sched",
            cl::init(&createDefaultScheduler), cl::Hidden,
            cl::desc("Instruction schedulers available (before register"
                     " allocation):"));

static RegisterScheduler
defaultListDAGScheduler("default", "Best scheduler for the target",
                        createDefaultScheduler);

namespace llvm {

  //===--------------------------------------------------------------------===//
  /// This class is used by SelectionDAGISel to temporarily override
  /// the optimization level on a per-function basis.
  class OptLevelChanger {
    SelectionDAGISel &IS;
    CodeGenOpt::Level SavedOptLevel;
    bool SavedFastISel;

  public:
    OptLevelChanger(SelectionDAGISel &ISel,
                    CodeGenOpt::Level NewOptLevel) : IS(ISel) {
      SavedOptLevel = IS.OptLevel;
      if (NewOptLevel == SavedOptLevel)
        return;
      IS.OptLevel = NewOptLevel;
      IS.TM.setOptLevel(NewOptLevel);
      LLVM_DEBUG(dbgs() << "\nChanging optimization level for Function "
                        << IS.MF->getFunction().getName() << "\n");
      LLVM_DEBUG(dbgs() << "\tBefore: -O" << SavedOptLevel << " ; After: -O"
                        << NewOptLevel << "\n");
      SavedFastISel = IS.TM.Options.EnableFastISel;
      if (NewOptLevel == CodeGenOpt::None) {
        IS.TM.setFastISel(IS.TM.getO0WantsFastISel());
        LLVM_DEBUG(
            dbgs() << "\tFastISel is "
                   << (IS.TM.Options.EnableFastISel ? "enabled" : "disabled")
                   << "\n");
      }
    }

    ~OptLevelChanger() {
      if (IS.OptLevel == SavedOptLevel)
        return;
      LLVM_DEBUG(dbgs() << "\nRestoring optimization level for Function "
                        << IS.MF->getFunction().getName() << "\n");
      LLVM_DEBUG(dbgs() << "\tBefore: -O" << IS.OptLevel << " ; After: -O"
                        << SavedOptLevel << "\n");
      IS.OptLevel = SavedOptLevel;
      IS.TM.setOptLevel(SavedOptLevel);
      IS.TM.setFastISel(SavedFastISel);
    }
  };

  //===--------------------------------------------------------------------===//
  /// createDefaultScheduler - This creates an instruction scheduler appropriate
  /// for the target.
  ScheduleDAGSDNodes* createDefaultScheduler(SelectionDAGISel *IS,
                                             CodeGenOpt::Level OptLevel) {
    const TargetLowering *TLI = IS->TLI;
    const TargetSubtargetInfo &ST = IS->MF->getSubtarget();

    // Try first to see if the Target has its own way of selecting a scheduler
    if (auto *SchedulerCtor = ST.getDAGScheduler(OptLevel)) {
      return SchedulerCtor(IS, OptLevel);
    }

    if (OptLevel == CodeGenOpt::None ||
        (ST.enableMachineScheduler() && ST.enableMachineSchedDefaultSched()) ||
        TLI->getSchedulingPreference() == Sched::Source)
      return createSourceListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::RegPressure)
      return createBURRListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::Hybrid)
      return createHybridListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::VLIW)
      return createVLIWDAGScheduler(IS, OptLevel);
    assert(TLI->getSchedulingPreference() == Sched::ILP &&
           "Unknown sched type!");
    return createILPListDAGScheduler(IS, OptLevel);
  }

} // end namespace llvm

// EmitInstrWithCustomInserter - This method should be implemented by targets
// that mark instructions with the 'usesCustomInserter' flag.  These
// instructions are special in various ways, which require special support to
// insert.  The specified MachineInstr is created but not inserted into any
// basic blocks, and this method is called to expand it into a sequence of
// instructions, potentially also creating new basic blocks and control flow.
// When new basic blocks are inserted and the edges from MBB to its successors
// are modified, the method should insert pairs of <OldSucc, NewSucc> into the
// DenseMap.
MachineBasicBlock *
TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                            MachineBasicBlock *MBB) const {
#ifndef NDEBUG
  dbgs() << "If a target marks an instruction with "
          "'usesCustomInserter', it must implement "
          "TargetLowering::EmitInstrWithCustomInserter!";
#endif
  llvm_unreachable(nullptr);
}

void TargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                   SDNode *Node) const {
  assert(!MI.hasPostISelHook() &&
         "If a target marks an instruction with 'hasPostISelHook', "
         "it must implement TargetLowering::AdjustInstrPostInstrSelection!");
}

//===----------------------------------------------------------------------===//
// SelectionDAGISel code
//===----------------------------------------------------------------------===//

SelectionDAGISel::SelectionDAGISel(TargetMachine &tm,
                                   CodeGenOpt::Level OL) :
  MachineFunctionPass(ID), TM(tm),
  FuncInfo(new FunctionLoweringInfo()),
  CurDAG(new SelectionDAG(tm, OL)),
  SDB(new SelectionDAGBuilder(*CurDAG, *FuncInfo, OL)),
  AA(), GFI(),
  OptLevel(OL),
  DAGSize(0) {
    initializeGCModuleInfoPass(*PassRegistry::getPassRegistry());
    initializeBranchProbabilityInfoWrapperPassPass(
        *PassRegistry::getPassRegistry());
    initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeTargetLibraryInfoWrapperPassPass(
        *PassRegistry::getPassRegistry());
  }

SelectionDAGISel::~SelectionDAGISel() {
  delete SDB;
  delete CurDAG;
  delete FuncInfo;
}

void SelectionDAGISel::getAnalysisUsage(AnalysisUsage &AU) const {
  if (OptLevel != CodeGenOpt::None)
    AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<GCModuleInfo>();
  AU.addRequired<StackProtector>();
  AU.addPreserved<GCModuleInfo>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<TargetTransformInfoWrapperPass>();
  if (UseMBPI && OptLevel != CodeGenOpt::None)
    AU.addRequired<BranchProbabilityInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// SplitCriticalSideEffectEdges - Look for critical edges with a PHI value that
/// may trap on it.  In this case we have to split the edge so that the path
/// through the predecessor block that doesn't go to the phi block doesn't
/// execute the possibly trapping instruction. If available, we pass domtree
/// and loop info to be updated when we split critical edges. This is because
/// SelectionDAGISel preserves these analyses.
/// This is required for correctness, so it must be done at -O0.
///
static void SplitCriticalSideEffectEdges(Function &Fn, DominatorTree *DT,
                                         LoopInfo *LI) {
  // Loop for blocks with phi nodes.
  for (BasicBlock &BB : Fn) {
    PHINode *PN = dyn_cast<PHINode>(BB.begin());
    if (!PN) continue;

  ReprocessBlock:
    // For each block with a PHI node, check to see if any of the input values
    // are potentially trapping constant expressions.  Constant expressions are
    // the only potentially trapping value that can occur as the argument to a
    // PHI.
    for (BasicBlock::iterator I = BB.begin(); (PN = dyn_cast<PHINode>(I)); ++I)
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        ConstantExpr *CE = dyn_cast<ConstantExpr>(PN->getIncomingValue(i));
        if (!CE || !CE->canTrap()) continue;

        // The only case we have to worry about is when the edge is critical.
        // Since this block has a PHI Node, we assume it has multiple input
        // edges: check to see if the pred has multiple successors.
        BasicBlock *Pred = PN->getIncomingBlock(i);
        if (Pred->getTerminator()->getNumSuccessors() == 1)
          continue;

        // Okay, we have to split this edge.
        SplitCriticalEdge(
            Pred->getTerminator(), GetSuccessorNumber(Pred, &BB),
            CriticalEdgeSplittingOptions(DT, LI).setMergeIdenticalEdges());
        goto ReprocessBlock;
      }
  }
}

bool SelectionDAGISel::runOnMachineFunction(MachineFunction &mf) {
  // If we already selected that function, we do not need to run SDISel.
  if (mf.getProperties().hasProperty(
          MachineFunctionProperties::Property::Selected))
    return false;
  // Do some sanity-checking on the command-line options.
  assert((!EnableFastISelAbort || TM.Options.EnableFastISel) &&
         "-fast-isel-abort > 0 requires -fast-isel");

  const Function &Fn = mf.getFunction();
  MF = &mf;

  // Reset the target options before resetting the optimization
  // level below.
  // FIXME: This is a horrible hack and should be processed via
  // codegen looking at the optimization level explicitly when
  // it wants to look at it.
  TM.resetTargetOptions(Fn);
  // Reset OptLevel to None for optnone functions.
  CodeGenOpt::Level NewOptLevel = OptLevel;
  if (OptLevel != CodeGenOpt::None && skipFunction(Fn))
    NewOptLevel = CodeGenOpt::None;
  OptLevelChanger OLC(*this, NewOptLevel);

  TII = MF->getSubtarget().getInstrInfo();
  TLI = MF->getSubtarget().getTargetLowering();
  RegInfo = &MF->getRegInfo();
  LibInfo = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  GFI = Fn.hasGC() ? &getAnalysis<GCModuleInfo>().getFunctionInfo(Fn) : nullptr;
  ORE = make_unique<OptimizationRemarkEmitter>(&Fn);
  auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DominatorTree *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
  LoopInfo *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;

  LLVM_DEBUG(dbgs() << "\n\n\n=== " << Fn.getName() << "\n");

  SplitCriticalSideEffectEdges(const_cast<Function &>(Fn), DT, LI);

  CurDAG->init(*MF, *ORE, this, LibInfo,
   getAnalysisIfAvailable<LegacyDivergenceAnalysis>());
  FuncInfo->set(Fn, *MF, CurDAG);

  // Now get the optional analyzes if we want to.
  // This is based on the possibly changed OptLevel (after optnone is taken
  // into account).  That's unfortunate but OK because it just means we won't
  // ask for passes that have been required anyway.

  if (UseMBPI && OptLevel != CodeGenOpt::None)
    FuncInfo->BPI = &getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
  else
    FuncInfo->BPI = nullptr;

  if (OptLevel != CodeGenOpt::None)
    AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  else
    AA = nullptr;

  SDB->init(GFI, AA, LibInfo);

  MF->setHasInlineAsm(false);

  FuncInfo->SplitCSR = false;

  // We split CSR if the target supports it for the given function
  // and the function has only return exits.
  if (OptLevel != CodeGenOpt::None && TLI->supportSplitCSR(MF)) {
    FuncInfo->SplitCSR = true;

    // Collect all the return blocks.
    for (const BasicBlock &BB : Fn) {
      if (!succ_empty(&BB))
        continue;

      const Instruction *Term = BB.getTerminator();
      if (isa<UnreachableInst>(Term) || isa<ReturnInst>(Term))
        continue;

      // Bail out if the exit block is not Return nor Unreachable.
      FuncInfo->SplitCSR = false;
      break;
    }
  }

  MachineBasicBlock *EntryMBB = &MF->front();
  if (FuncInfo->SplitCSR)
    // This performs initialization so lowering for SplitCSR will be correct.
    TLI->initializeSplitCSR(EntryMBB);

  SelectAllBasicBlocks(Fn);
  if (FastISelFailed && EnableFastISelFallbackReport) {
    DiagnosticInfoISelFallback DiagFallback(Fn);
    Fn.getContext().diagnose(DiagFallback);
  }

  // If the first basic block in the function has live ins that need to be
  // copied into vregs, emit the copies into the top of the block before
  // emitting the code for the block.
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  RegInfo->EmitLiveInCopies(EntryMBB, TRI, *TII);

  // Insert copies in the entry block and the return blocks.
  if (FuncInfo->SplitCSR) {
    SmallVector<MachineBasicBlock*, 4> Returns;
    // Collect all the return blocks.
    for (MachineBasicBlock &MBB : mf) {
      if (!MBB.succ_empty())
        continue;

      MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
      if (Term != MBB.end() && Term->isReturn()) {
        Returns.push_back(&MBB);
        continue;
      }
    }
    TLI->insertCopiesSplitCSR(EntryMBB, Returns);
  }

  DenseMap<unsigned, unsigned> LiveInMap;
  if (!FuncInfo->ArgDbgValues.empty())
    for (std::pair<unsigned, unsigned> LI : RegInfo->liveins())
      if (LI.second)
        LiveInMap.insert(LI);

  // Insert DBG_VALUE instructions for function arguments to the entry block.
  for (unsigned i = 0, e = FuncInfo->ArgDbgValues.size(); i != e; ++i) {
    MachineInstr *MI = FuncInfo->ArgDbgValues[e-i-1];
    bool hasFI = MI->getOperand(0).isFI();
    unsigned Reg =
        hasFI ? TRI.getFrameRegister(*MF) : MI->getOperand(0).getReg();
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      EntryMBB->insert(EntryMBB->begin(), MI);
    else {
      MachineInstr *Def = RegInfo->getVRegDef(Reg);
      if (Def) {
        MachineBasicBlock::iterator InsertPos = Def;
        // FIXME: VR def may not be in entry block.
        Def->getParent()->insert(std::next(InsertPos), MI);
      } else
        LLVM_DEBUG(dbgs() << "Dropping debug info for dead vreg"
                          << TargetRegisterInfo::virtReg2Index(Reg) << "\n");
    }

    // If Reg is live-in then update debug info to track its copy in a vreg.
    DenseMap<unsigned, unsigned>::iterator LDI = LiveInMap.find(Reg);
    if (LDI != LiveInMap.end()) {
      assert(!hasFI && "There's no handling of frame pointer updating here yet "
                       "- add if needed");
      MachineInstr *Def = RegInfo->getVRegDef(LDI->second);
      MachineBasicBlock::iterator InsertPos = Def;
      const MDNode *Variable = MI->getDebugVariable();
      const MDNode *Expr = MI->getDebugExpression();
      DebugLoc DL = MI->getDebugLoc();
      bool IsIndirect = MI->isIndirectDebugValue();
      if (IsIndirect)
        assert(MI->getOperand(1).getImm() == 0 &&
               "DBG_VALUE with nonzero offset");
      assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
             "Expected inlined-at fields to agree");
      // Def is never a terminator here, so it is ok to increment InsertPos.
      BuildMI(*EntryMBB, ++InsertPos, DL, TII->get(TargetOpcode::DBG_VALUE),
              IsIndirect, LDI->second, Variable, Expr);

      // If this vreg is directly copied into an exported register then
      // that COPY instructions also need DBG_VALUE, if it is the only
      // user of LDI->second.
      MachineInstr *CopyUseMI = nullptr;
      for (MachineRegisterInfo::use_instr_iterator
           UI = RegInfo->use_instr_begin(LDI->second),
           E = RegInfo->use_instr_end(); UI != E; ) {
        MachineInstr *UseMI = &*(UI++);
        if (UseMI->isDebugValue()) continue;
        if (UseMI->isCopy() && !CopyUseMI && UseMI->getParent() == EntryMBB) {
          CopyUseMI = UseMI; continue;
        }
        // Otherwise this is another use or second copy use.
        CopyUseMI = nullptr; break;
      }
      if (CopyUseMI) {
        // Use MI's debug location, which describes where Variable was
        // declared, rather than whatever is attached to CopyUseMI.
        MachineInstr *NewMI =
            BuildMI(*MF, DL, TII->get(TargetOpcode::DBG_VALUE), IsIndirect,
                    CopyUseMI->getOperand(0).getReg(), Variable, Expr);
        MachineBasicBlock::iterator Pos = CopyUseMI;
        EntryMBB->insertAfter(Pos, NewMI);
      }
    }
  }

  // Determine if there are any calls in this machine function.
  MachineFrameInfo &MFI = MF->getFrameInfo();
  for (const auto &MBB : *MF) {
    if (MFI.hasCalls() && MF->hasInlineAsm())
      break;

    for (const auto &MI : MBB) {
      const MCInstrDesc &MCID = TII->get(MI.getOpcode());
      if ((MCID.isCall() && !MCID.isReturn()) ||
          MI.isStackAligningInlineAsm()) {
        MFI.setHasCalls(true);
      }
      if (MI.isInlineAsm()) {
        MF->setHasInlineAsm(true);
      }
    }
  }

  // Determine if there is a call to setjmp in the machine function.
  MF->setExposesReturnsTwice(Fn.callsFunctionThatReturnsTwice());

  // Replace forward-declared registers with the registers containing
  // the desired value.
  MachineRegisterInfo &MRI = MF->getRegInfo();
  for (DenseMap<unsigned, unsigned>::iterator
       I = FuncInfo->RegFixups.begin(), E = FuncInfo->RegFixups.end();
       I != E; ++I) {
    unsigned From = I->first;
    unsigned To = I->second;
    // If To is also scheduled to be replaced, find what its ultimate
    // replacement is.
    while (true) {
      DenseMap<unsigned, unsigned>::iterator J = FuncInfo->RegFixups.find(To);
      if (J == E) break;
      To = J->second;
    }
    // Make sure the new register has a sufficiently constrained register class.
    if (TargetRegisterInfo::isVirtualRegister(From) &&
        TargetRegisterInfo::isVirtualRegister(To))
      MRI.constrainRegClass(To, MRI.getRegClass(From));
    // Replace it.


    // Replacing one register with another won't touch the kill flags.
    // We need to conservatively clear the kill flags as a kill on the old
    // register might dominate existing uses of the new register.
    if (!MRI.use_empty(To))
      MRI.clearKillFlags(From);
    MRI.replaceRegWith(From, To);
  }

  TLI->finalizeLowering(*MF);

  // Release function-specific state. SDB and CurDAG are already cleared
  // at this point.
  FuncInfo->clear();

  LLVM_DEBUG(dbgs() << "*** MachineFunction at end of ISel ***\n");
  LLVM_DEBUG(MF->print(dbgs()));

  return true;
}

static void reportFastISelFailure(MachineFunction &MF,
                                  OptimizationRemarkEmitter &ORE,
                                  OptimizationRemarkMissed &R,
                                  bool ShouldAbort) {
  // Print the function name explicitly if we don't have a debug location (which
  // makes the diagnostic less useful) or if we're going to emit a raw error.
  if (!R.getLocation().isValid() || ShouldAbort)
    R << (" (in function: " + MF.getName() + ")").str();

  if (ShouldAbort)
    report_fatal_error(R.getMsg());

  ORE.emit(R);
}

void SelectionDAGISel::SelectBasicBlock(BasicBlock::const_iterator Begin,
                                        BasicBlock::const_iterator End,
                                        bool &HadTailCall) {
  // Allow creating illegal types during DAG building for the basic block.
  CurDAG->NewNodesMustHaveLegalTypes = false;

  // Lower the instructions. If a call is emitted as a tail call, cease emitting
  // nodes for this block.
  for (BasicBlock::const_iterator I = Begin; I != End && !SDB->HasTailCall; ++I) {
    if (!ElidedArgCopyInstrs.count(&*I))
      SDB->visit(*I);
  }

  // Make sure the root of the DAG is up-to-date.
  CurDAG->setRoot(SDB->getControlRoot());
  HadTailCall = SDB->HasTailCall;
  SDB->clear();

  // Final step, emit the lowered DAG as machine code.
  CodeGenAndEmitDAG();
}

void SelectionDAGISel::ComputeLiveOutVRegInfo() {
  SmallPtrSet<SDNode*, 16> VisitedNodes;
  SmallVector<SDNode*, 128> Worklist;

  Worklist.push_back(CurDAG->getRoot().getNode());

  KnownBits Known;

  do {
    SDNode *N = Worklist.pop_back_val();

    // If we've already seen this node, ignore it.
    if (!VisitedNodes.insert(N).second)
      continue;

    // Otherwise, add all chain operands to the worklist.
    for (const SDValue &Op : N->op_values())
      if (Op.getValueType() == MVT::Other)
        Worklist.push_back(Op.getNode());

    // If this is a CopyToReg with a vreg dest, process it.
    if (N->getOpcode() != ISD::CopyToReg)
      continue;

    unsigned DestReg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
    if (!TargetRegisterInfo::isVirtualRegister(DestReg))
      continue;

    // Ignore non-integer values.
    SDValue Src = N->getOperand(2);
    EVT SrcVT = Src.getValueType();
    if (!SrcVT.isInteger())
      continue;

    unsigned NumSignBits = CurDAG->ComputeNumSignBits(Src);
    Known = CurDAG->computeKnownBits(Src);
    FuncInfo->AddLiveOutRegInfo(DestReg, NumSignBits, Known);
  } while (!Worklist.empty());
}

void SelectionDAGISel::CodeGenAndEmitDAG() {
  StringRef GroupName = "sdag";
  StringRef GroupDescription = "Instruction Selection and Scheduling";
  std::string BlockName;
  int BlockNumber = -1;
  (void)BlockNumber;
  bool MatchFilterBB = false; (void)MatchFilterBB;
#ifndef NDEBUG
  TargetTransformInfo &TTI =
      getAnalysis<TargetTransformInfoWrapperPass>().getTTI(*FuncInfo->Fn);
#endif

  // Pre-type legalization allow creation of any node types.
  CurDAG->NewNodesMustHaveLegalTypes = false;

#ifndef NDEBUG
  MatchFilterBB = (FilterDAGBasicBlockName.empty() ||
                   FilterDAGBasicBlockName ==
                       FuncInfo->MBB->getBasicBlock()->getName());
#endif
#ifdef NDEBUG
  if (ViewDAGCombine1 || ViewLegalizeTypesDAGs || ViewLegalizeDAGs ||
      ViewDAGCombine2 || ViewDAGCombineLT || ViewISelDAGs || ViewSchedDAGs ||
      ViewSUnitDAGs)
#endif
  {
    BlockNumber = FuncInfo->MBB->getNumber();
    BlockName =
        (MF->getName() + ":" + FuncInfo->MBB->getBasicBlock()->getName()).str();
  }
  LLVM_DEBUG(dbgs() << "Initial selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  if (ViewDAGCombine1 && MatchFilterBB)
    CurDAG->viewGraph("dag-combine1 input for " + BlockName);

  // Run the DAG combiner in pre-legalize mode.
  {
    NamedRegionTimer T("combine1", "DAG Combining 1", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Combine(BeforeLegalizeTypes, AA, OptLevel);
  }

#ifndef NDEBUG
  if (TTI.hasBranchDivergence())
    CurDAG->VerifyDAGDiverence();
#endif

  LLVM_DEBUG(dbgs() << "Optimized lowered selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  // Second step, hack on the DAG until it only uses operations and types that
  // the target supports.
  if (ViewLegalizeTypesDAGs && MatchFilterBB)
    CurDAG->viewGraph("legalize-types input for " + BlockName);

  bool Changed;
  {
    NamedRegionTimer T("legalize_types", "Type Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Changed = CurDAG->LegalizeTypes();
  }

#ifndef NDEBUG
  if (TTI.hasBranchDivergence())
    CurDAG->VerifyDAGDiverence();
#endif

  LLVM_DEBUG(dbgs() << "Type-legalized selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  // Only allow creation of legal node types.
  CurDAG->NewNodesMustHaveLegalTypes = true;

  if (Changed) {
    if (ViewDAGCombineLT && MatchFilterBB)
      CurDAG->viewGraph("dag-combine-lt input for " + BlockName);

    // Run the DAG combiner in post-type-legalize mode.
    {
      NamedRegionTimer T("combine_lt", "DAG Combining after legalize types",
                         GroupName, GroupDescription, TimePassesIsEnabled);
      CurDAG->Combine(AfterLegalizeTypes, AA, OptLevel);
    }

#ifndef NDEBUG
    if (TTI.hasBranchDivergence())
      CurDAG->VerifyDAGDiverence();
#endif

    LLVM_DEBUG(dbgs() << "Optimized type-legalized selection DAG: "
                      << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                      << "'\n";
               CurDAG->dump());
  }

  {
    NamedRegionTimer T("legalize_vec", "Vector Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Changed = CurDAG->LegalizeVectors();
  }

  if (Changed) {
    LLVM_DEBUG(dbgs() << "Vector-legalized selection DAG: "
                      << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                      << "'\n";
               CurDAG->dump());

    {
      NamedRegionTimer T("legalize_types2", "Type Legalization 2", GroupName,
                         GroupDescription, TimePassesIsEnabled);
      CurDAG->LegalizeTypes();
    }

    LLVM_DEBUG(dbgs() << "Vector/type-legalized selection DAG: "
                      << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                      << "'\n";
               CurDAG->dump());

    if (ViewDAGCombineLT && MatchFilterBB)
      CurDAG->viewGraph("dag-combine-lv input for " + BlockName);

    // Run the DAG combiner in post-type-legalize mode.
    {
      NamedRegionTimer T("combine_lv", "DAG Combining after legalize vectors",
                         GroupName, GroupDescription, TimePassesIsEnabled);
      CurDAG->Combine(AfterLegalizeVectorOps, AA, OptLevel);
    }

    LLVM_DEBUG(dbgs() << "Optimized vector-legalized selection DAG: "
                      << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                      << "'\n";
               CurDAG->dump());

#ifndef NDEBUG
    if (TTI.hasBranchDivergence())
      CurDAG->VerifyDAGDiverence();
#endif
  }

  if (ViewLegalizeDAGs && MatchFilterBB)
    CurDAG->viewGraph("legalize input for " + BlockName);

  {
    NamedRegionTimer T("legalize", "DAG Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Legalize();
  }

#ifndef NDEBUG
  if (TTI.hasBranchDivergence())
    CurDAG->VerifyDAGDiverence();
#endif

  LLVM_DEBUG(dbgs() << "Legalized selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  if (ViewDAGCombine2 && MatchFilterBB)
    CurDAG->viewGraph("dag-combine2 input for " + BlockName);

  // Run the DAG combiner in post-legalize mode.
  {
    NamedRegionTimer T("combine2", "DAG Combining 2", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Combine(AfterLegalizeDAG, AA, OptLevel);
  }

#ifndef NDEBUG
  if (TTI.hasBranchDivergence())
    CurDAG->VerifyDAGDiverence();
#endif

  LLVM_DEBUG(dbgs() << "Optimized legalized selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  if (OptLevel != CodeGenOpt::None)
    ComputeLiveOutVRegInfo();

  if (ViewISelDAGs && MatchFilterBB)
    CurDAG->viewGraph("isel input for " + BlockName);

  // Third, instruction select all of the operations to machine code, adding the
  // code to the MachineBasicBlock.
  {
    NamedRegionTimer T("isel", "Instruction Selection", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    DoInstructionSelection();
  }

  LLVM_DEBUG(dbgs() << "Selected selection DAG: "
                    << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                    << "'\n";
             CurDAG->dump());

  if (ViewSchedDAGs && MatchFilterBB)
    CurDAG->viewGraph("scheduler input for " + BlockName);

  // Schedule machine code.
  ScheduleDAGSDNodes *Scheduler = CreateScheduler();
  {
    NamedRegionTimer T("sched", "Instruction Scheduling", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Scheduler->Run(CurDAG, FuncInfo->MBB);
  }

  if (ViewSUnitDAGs && MatchFilterBB)
    Scheduler->viewGraph();

  // Emit machine code to BB.  This can change 'BB' to the last block being
  // inserted into.
  MachineBasicBlock *FirstMBB = FuncInfo->MBB, *LastMBB;
  {
    NamedRegionTimer T("emit", "Instruction Creation", GroupName,
                       GroupDescription, TimePassesIsEnabled);

    // FuncInfo->InsertPt is passed by reference and set to the end of the
    // scheduled instructions.
    LastMBB = FuncInfo->MBB = Scheduler->EmitSchedule(FuncInfo->InsertPt);
  }

  // If the block was split, make sure we update any references that are used to
  // update PHI nodes later on.
  if (FirstMBB != LastMBB)
    SDB->UpdateSplitBlock(FirstMBB, LastMBB);

  // Free the scheduler state.
  {
    NamedRegionTimer T("cleanup", "Instruction Scheduling Cleanup", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    delete Scheduler;
  }

  // Free the SelectionDAG state, now that we're finished with it.
  CurDAG->clear();
}

namespace {

/// ISelUpdater - helper class to handle updates of the instruction selection
/// graph.
class ISelUpdater : public SelectionDAG::DAGUpdateListener {
  SelectionDAG::allnodes_iterator &ISelPosition;

public:
  ISelUpdater(SelectionDAG &DAG, SelectionDAG::allnodes_iterator &isp)
    : SelectionDAG::DAGUpdateListener(DAG), ISelPosition(isp) {}

  /// NodeDeleted - Handle nodes deleted from the graph. If the node being
  /// deleted is the current ISelPosition node, update ISelPosition.
  ///
  void NodeDeleted(SDNode *N, SDNode *E) override {
    if (ISelPosition == SelectionDAG::allnodes_iterator(N))
      ++ISelPosition;
  }
};

} // end anonymous namespace

// This function is used to enforce the topological node id property
// property leveraged during Instruction selection. Before selection all
// nodes are given a non-negative id such that all nodes have a larger id than
// their operands. As this holds transitively we can prune checks that a node N
// is a predecessor of M another by not recursively checking through M's
// operands if N's ID is larger than M's ID. This is significantly improves
// performance of for various legality checks (e.g. IsLegalToFold /
// UpdateChains).

// However, when we fuse multiple nodes into a single node
// during selection we may induce a predecessor relationship between inputs and
// outputs of distinct nodes being merged violating the topological property.
// Should a fused node have a successor which has yet to be selected, our
// legality checks would be incorrect. To avoid this we mark all unselected
// sucessor nodes, i.e. id != -1 as invalid for pruning by bit-negating (x =>
// (-(x+1))) the ids and modify our pruning check to ignore negative Ids of M.
// We use bit-negation to more clearly enforce that node id -1 can only be
// achieved by selected nodes). As the conversion is reversable the original Id,
// topological pruning can still be leveraged when looking for unselected nodes.
// This method is call internally in all ISel replacement calls.
void SelectionDAGISel::EnforceNodeIdInvariant(SDNode *Node) {
  SmallVector<SDNode *, 4> Nodes;
  Nodes.push_back(Node);

  while (!Nodes.empty()) {
    SDNode *N = Nodes.pop_back_val();
    for (auto *U : N->uses()) {
      auto UId = U->getNodeId();
      if (UId > 0) {
        InvalidateNodeId(U);
        Nodes.push_back(U);
      }
    }
  }
}

// InvalidateNodeId - As discusses in EnforceNodeIdInvariant, mark a
// NodeId with the equivalent node id which is invalid for topological
// pruning.
void SelectionDAGISel::InvalidateNodeId(SDNode *N) {
  int InvalidId = -(N->getNodeId() + 1);
  N->setNodeId(InvalidId);
}

// getUninvalidatedNodeId - get original uninvalidated node id.
int SelectionDAGISel::getUninvalidatedNodeId(SDNode *N) {
  int Id = N->getNodeId();
  if (Id < -1)
    return -(Id + 1);
  return Id;
}

void SelectionDAGISel::DoInstructionSelection() {
  LLVM_DEBUG(dbgs() << "===== Instruction selection begins: "
                    << printMBBReference(*FuncInfo->MBB) << " '"
                    << FuncInfo->MBB->getName() << "'\n");

  PreprocessISelDAG();

  // Select target instructions for the DAG.
  {
    // Number all nodes with a topological order and set DAGSize.
    DAGSize = CurDAG->AssignTopologicalOrder();

    // Create a dummy node (which is not added to allnodes), that adds
    // a reference to the root node, preventing it from being deleted,
    // and tracking any changes of the root.
    HandleSDNode Dummy(CurDAG->getRoot());
    SelectionDAG::allnodes_iterator ISelPosition (CurDAG->getRoot().getNode());
    ++ISelPosition;

    // Make sure that ISelPosition gets properly updated when nodes are deleted
    // in calls made from this function.
    ISelUpdater ISU(*CurDAG, ISelPosition);

    // The AllNodes list is now topological-sorted. Visit the
    // nodes by starting at the end of the list (the root of the
    // graph) and preceding back toward the beginning (the entry
    // node).
    while (ISelPosition != CurDAG->allnodes_begin()) {
      SDNode *Node = &*--ISelPosition;
      // Skip dead nodes. DAGCombiner is expected to eliminate all dead nodes,
      // but there are currently some corner cases that it misses. Also, this
      // makes it theoretically possible to disable the DAGCombiner.
      if (Node->use_empty())
        continue;

#ifndef NDEBUG
      SmallVector<SDNode *, 4> Nodes;
      Nodes.push_back(Node);

      while (!Nodes.empty()) {
        auto N = Nodes.pop_back_val();
        if (N->getOpcode() == ISD::TokenFactor || N->getNodeId() < 0)
          continue;
        for (const SDValue &Op : N->op_values()) {
          if (Op->getOpcode() == ISD::TokenFactor)
            Nodes.push_back(Op.getNode());
          else {
            // We rely on topological ordering of node ids for checking for
            // cycles when fusing nodes during selection. All unselected nodes
            // successors of an already selected node should have a negative id.
            // This assertion will catch such cases. If this assertion triggers
            // it is likely you using DAG-level Value/Node replacement functions
            // (versus equivalent ISEL replacement) in backend-specific
            // selections. See comment in EnforceNodeIdInvariant for more
            // details.
            assert(Op->getNodeId() != -1 &&
                   "Node has already selected predecessor node");
          }
        }
      }
#endif

      // When we are using non-default rounding modes or FP exception behavior
      // FP operations are represented by StrictFP pseudo-operations.  They
      // need to be simplified here so that the target-specific instruction
      // selectors know how to handle them.
      //
      // If the current node is a strict FP pseudo-op, the isStrictFPOp()
      // function will provide the corresponding normal FP opcode to which the
      // node should be mutated.
      //
      // FIXME: The backends need a way to handle FP constraints.
      if (Node->isStrictFPOpcode())
        Node = CurDAG->mutateStrictFPToFP(Node);

      LLVM_DEBUG(dbgs() << "\nISEL: Starting selection on root node: ";
                 Node->dump(CurDAG));

      Select(Node);
    }

    CurDAG->setRoot(Dummy.getValue());
  }

  LLVM_DEBUG(dbgs() << "\n===== Instruction selection ends:\n");

  PostprocessISelDAG();
}

static bool hasExceptionPointerOrCodeUser(const CatchPadInst *CPI) {
  for (const User *U : CPI->users()) {
    if (const IntrinsicInst *EHPtrCall = dyn_cast<IntrinsicInst>(U)) {
      Intrinsic::ID IID = EHPtrCall->getIntrinsicID();
      if (IID == Intrinsic::eh_exceptionpointer ||
          IID == Intrinsic::eh_exceptioncode)
        return true;
    }
  }
  return false;
}

// wasm.landingpad.index intrinsic is for associating a landing pad index number
// with a catchpad instruction. Retrieve the landing pad index in the intrinsic
// and store the mapping in the function.
static void mapWasmLandingPadIndex(MachineBasicBlock *MBB,
                                   const CatchPadInst *CPI) {
  MachineFunction *MF = MBB->getParent();
  // In case of single catch (...), we don't emit LSDA, so we don't need
  // this information.
  bool IsSingleCatchAllClause =
      CPI->getNumArgOperands() == 1 &&
      cast<Constant>(CPI->getArgOperand(0))->isNullValue();
  if (!IsSingleCatchAllClause) {
    // Create a mapping from landing pad label to landing pad index.
    bool IntrFound = false;
    for (const User *U : CPI->users()) {
      if (const auto *Call = dyn_cast<IntrinsicInst>(U)) {
        Intrinsic::ID IID = Call->getIntrinsicID();
        if (IID == Intrinsic::wasm_landingpad_index) {
          Value *IndexArg = Call->getArgOperand(1);
          int Index = cast<ConstantInt>(IndexArg)->getZExtValue();
          MF->setWasmLandingPadIndex(MBB, Index);
          IntrFound = true;
          break;
        }
      }
    }
    assert(IntrFound && "wasm.landingpad.index intrinsic not found!");
    (void)IntrFound;
  }
}

/// PrepareEHLandingPad - Emit an EH_LABEL, set up live-in registers, and
/// do other setup for EH landing-pad blocks.
bool SelectionDAGISel::PrepareEHLandingPad() {
  MachineBasicBlock *MBB = FuncInfo->MBB;
  const Constant *PersonalityFn = FuncInfo->Fn->getPersonalityFn();
  const BasicBlock *LLVMBB = MBB->getBasicBlock();
  const TargetRegisterClass *PtrRC =
      TLI->getRegClassFor(TLI->getPointerTy(CurDAG->getDataLayout()));

  auto Pers = classifyEHPersonality(PersonalityFn);

  // Catchpads have one live-in register, which typically holds the exception
  // pointer or code.
  if (isFuncletEHPersonality(Pers)) {
    if (const auto *CPI = dyn_cast<CatchPadInst>(LLVMBB->getFirstNonPHI())) {
      if (hasExceptionPointerOrCodeUser(CPI)) {
        // Get or create the virtual register to hold the pointer or code.  Mark
        // the live in physreg and copy into the vreg.
        MCPhysReg EHPhysReg = TLI->getExceptionPointerRegister(PersonalityFn);
        assert(EHPhysReg && "target lacks exception pointer register");
        MBB->addLiveIn(EHPhysReg);
        unsigned VReg = FuncInfo->getCatchPadExceptionPointerVReg(CPI, PtrRC);
        BuildMI(*MBB, FuncInfo->InsertPt, SDB->getCurDebugLoc(),
                TII->get(TargetOpcode::COPY), VReg)
            .addReg(EHPhysReg, RegState::Kill);
      }
    }
    return true;
  }

  // Add a label to mark the beginning of the landing pad.  Deletion of the
  // landing pad can thus be detected via the MachineModuleInfo.
  MCSymbol *Label = MF->addLandingPad(MBB);

  const MCInstrDesc &II = TII->get(TargetOpcode::EH_LABEL);
  BuildMI(*MBB, FuncInfo->InsertPt, SDB->getCurDebugLoc(), II)
    .addSym(Label);

  if (Pers == EHPersonality::Wasm_CXX) {
    if (const auto *CPI = dyn_cast<CatchPadInst>(LLVMBB->getFirstNonPHI()))
      mapWasmLandingPadIndex(MBB, CPI);
  } else {
    // Assign the call site to the landing pad's begin label.
    MF->setCallSiteLandingPad(Label, SDB->LPadToCallSiteMap[MBB]);
    // Mark exception register as live in.
    if (unsigned Reg = TLI->getExceptionPointerRegister(PersonalityFn))
      FuncInfo->ExceptionPointerVirtReg = MBB->addLiveIn(Reg, PtrRC);
    // Mark exception selector register as live in.
    if (unsigned Reg = TLI->getExceptionSelectorRegister(PersonalityFn))
      FuncInfo->ExceptionSelectorVirtReg = MBB->addLiveIn(Reg, PtrRC);
  }

  return true;
}

/// isFoldedOrDeadInstruction - Return true if the specified instruction is
/// side-effect free and is either dead or folded into a generated instruction.
/// Return false if it needs to be emitted.
static bool isFoldedOrDeadInstruction(const Instruction *I,
                                      FunctionLoweringInfo *FuncInfo) {
  return !I->mayWriteToMemory() && // Side-effecting instructions aren't folded.
         !I->isTerminator() &&     // Terminators aren't folded.
         !isa<DbgInfoIntrinsic>(I) &&  // Debug instructions aren't folded.
         !I->isEHPad() &&              // EH pad instructions aren't folded.
         !FuncInfo->isExportedInst(I); // Exported instrs must be computed.
}

/// Set up SwiftErrorVals by going through the function. If the function has
/// swifterror argument, it will be the first entry.
static void setupSwiftErrorVals(const Function &Fn, const TargetLowering *TLI,
                                FunctionLoweringInfo *FuncInfo) {
  if (!TLI->supportSwiftError())
    return;

  FuncInfo->SwiftErrorVals.clear();
  FuncInfo->SwiftErrorVRegDefMap.clear();
  FuncInfo->SwiftErrorVRegUpwardsUse.clear();
  FuncInfo->SwiftErrorVRegDefUses.clear();
  FuncInfo->SwiftErrorArg = nullptr;

  // Check if function has a swifterror argument.
  bool HaveSeenSwiftErrorArg = false;
  for (Function::const_arg_iterator AI = Fn.arg_begin(), AE = Fn.arg_end();
       AI != AE; ++AI)
    if (AI->hasSwiftErrorAttr()) {
      assert(!HaveSeenSwiftErrorArg &&
             "Must have only one swifterror parameter");
      (void)HaveSeenSwiftErrorArg; // silence warning.
      HaveSeenSwiftErrorArg = true;
      FuncInfo->SwiftErrorArg = &*AI;
      FuncInfo->SwiftErrorVals.push_back(&*AI);
    }

  for (const auto &LLVMBB : Fn)
    for (const auto &Inst : LLVMBB) {
      if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst))
        if (Alloca->isSwiftError())
          FuncInfo->SwiftErrorVals.push_back(Alloca);
    }
}

static void createSwiftErrorEntriesInEntryBlock(FunctionLoweringInfo *FuncInfo,
                                                FastISel *FastIS,
                                                const TargetLowering *TLI,
                                                const TargetInstrInfo *TII,
                                                SelectionDAGBuilder *SDB) {
  if (!TLI->supportSwiftError())
    return;

  // We only need to do this when we have swifterror parameter or swifterror
  // alloc.
  if (FuncInfo->SwiftErrorVals.empty())
    return;

  assert(FuncInfo->MBB == &*FuncInfo->MF->begin() &&
         "expected to insert into entry block");
  auto &DL = FuncInfo->MF->getDataLayout();
  auto const *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
  for (const auto *SwiftErrorVal : FuncInfo->SwiftErrorVals) {
    // We will always generate a copy from the argument. It is always used at
    // least by the 'return' of the swifterror.
    if (FuncInfo->SwiftErrorArg && FuncInfo->SwiftErrorArg == SwiftErrorVal)
      continue;
    unsigned VReg = FuncInfo->MF->getRegInfo().createVirtualRegister(RC);
    // Assign Undef to Vreg. We construct MI directly to make sure it works
    // with FastISel.
    BuildMI(*FuncInfo->MBB, FuncInfo->MBB->getFirstNonPHI(),
            SDB->getCurDebugLoc(), TII->get(TargetOpcode::IMPLICIT_DEF),
            VReg);

    // Keep FastIS informed about the value we just inserted.
    if (FastIS)
      FastIS->setLastLocalValue(&*std::prev(FuncInfo->InsertPt));

    FuncInfo->setCurrentSwiftErrorVReg(FuncInfo->MBB, SwiftErrorVal, VReg);
  }
}

/// Collect llvm.dbg.declare information. This is done after argument lowering
/// in case the declarations refer to arguments.
static void processDbgDeclares(FunctionLoweringInfo *FuncInfo) {
  MachineFunction *MF = FuncInfo->MF;
  const DataLayout &DL = MF->getDataLayout();
  for (const BasicBlock &BB : *FuncInfo->Fn) {
    for (const Instruction &I : BB) {
      const DbgDeclareInst *DI = dyn_cast<DbgDeclareInst>(&I);
      if (!DI)
        continue;

      assert(DI->getVariable() && "Missing variable");
      assert(DI->getDebugLoc() && "Missing location");
      const Value *Address = DI->getAddress();
      if (!Address)
        continue;

      // Look through casts and constant offset GEPs. These mostly come from
      // inalloca.
      APInt Offset(DL.getTypeSizeInBits(Address->getType()), 0);
      Address = Address->stripAndAccumulateInBoundsConstantOffsets(DL, Offset);

      // Check if the variable is a static alloca or a byval or inalloca
      // argument passed in memory. If it is not, then we will ignore this
      // intrinsic and handle this during isel like dbg.value.
      int FI = std::numeric_limits<int>::max();
      if (const auto *AI = dyn_cast<AllocaInst>(Address)) {
        auto SI = FuncInfo->StaticAllocaMap.find(AI);
        if (SI != FuncInfo->StaticAllocaMap.end())
          FI = SI->second;
      } else if (const auto *Arg = dyn_cast<Argument>(Address))
        FI = FuncInfo->getArgumentFrameIndex(Arg);

      if (FI == std::numeric_limits<int>::max())
        continue;

      DIExpression *Expr = DI->getExpression();
      if (Offset.getBoolValue())
        Expr = DIExpression::prepend(Expr, DIExpression::NoDeref,
                                     Offset.getZExtValue());
      MF->setVariableDbgInfo(DI->getVariable(), Expr, FI, DI->getDebugLoc());
    }
  }
}

/// Propagate swifterror values through the machine function CFG.
static void propagateSwiftErrorVRegs(FunctionLoweringInfo *FuncInfo) {
  auto *TLI = FuncInfo->TLI;
  if (!TLI->supportSwiftError())
    return;

  // We only need to do this when we have swifterror parameter or swifterror
  // alloc.
  if (FuncInfo->SwiftErrorVals.empty())
    return;

  // For each machine basic block in reverse post order.
  ReversePostOrderTraversal<MachineFunction *> RPOT(FuncInfo->MF);
  for (MachineBasicBlock *MBB : RPOT) {
    // For each swifterror value in the function.
    for(const auto *SwiftErrorVal : FuncInfo->SwiftErrorVals) {
      auto Key = std::make_pair(MBB, SwiftErrorVal);
      auto UUseIt = FuncInfo->SwiftErrorVRegUpwardsUse.find(Key);
      auto VRegDefIt = FuncInfo->SwiftErrorVRegDefMap.find(Key);
      bool UpwardsUse = UUseIt != FuncInfo->SwiftErrorVRegUpwardsUse.end();
      unsigned UUseVReg = UpwardsUse ? UUseIt->second : 0;
      bool DownwardDef = VRegDefIt != FuncInfo->SwiftErrorVRegDefMap.end();
      assert(!(UpwardsUse && !DownwardDef) &&
             "We can't have an upwards use but no downwards def");

      // If there is no upwards exposed use and an entry for the swifterror in
      // the def map for this value we don't need to do anything: We already
      // have a downward def for this basic block.
      if (!UpwardsUse && DownwardDef)
        continue;

      // Otherwise we either have an upwards exposed use vreg that we need to
      // materialize or need to forward the downward def from predecessors.

      // Check whether we have a single vreg def from all predecessors.
      // Otherwise we need a phi.
      SmallVector<std::pair<MachineBasicBlock *, unsigned>, 4> VRegs;
      SmallSet<const MachineBasicBlock*, 8> Visited;
      for (auto *Pred : MBB->predecessors()) {
        if (!Visited.insert(Pred).second)
          continue;
        VRegs.push_back(std::make_pair(
            Pred, FuncInfo->getOrCreateSwiftErrorVReg(Pred, SwiftErrorVal)));
        if (Pred != MBB)
          continue;
        // We have a self-edge.
        // If there was no upwards use in this basic block there is now one: the
        // phi needs to use it self.
        if (!UpwardsUse) {
          UpwardsUse = true;
          UUseIt = FuncInfo->SwiftErrorVRegUpwardsUse.find(Key);
          assert(UUseIt != FuncInfo->SwiftErrorVRegUpwardsUse.end());
          UUseVReg = UUseIt->second;
        }
      }

      // We need a phi node if we have more than one predecessor with different
      // downward defs.
      bool needPHI =
          VRegs.size() >= 1 &&
          std::find_if(
              VRegs.begin(), VRegs.end(),
              [&](const std::pair<const MachineBasicBlock *, unsigned> &V)
                  -> bool { return V.second != VRegs[0].second; }) !=
              VRegs.end();

      // If there is no upwards exposed used and we don't need a phi just
      // forward the swifterror vreg from the predecessor(s).
      if (!UpwardsUse && !needPHI) {
        assert(!VRegs.empty() &&
               "No predecessors? The entry block should bail out earlier");
        // Just forward the swifterror vreg from the predecessor(s).
        FuncInfo->setCurrentSwiftErrorVReg(MBB, SwiftErrorVal, VRegs[0].second);
        continue;
      }

      auto DLoc = isa<Instruction>(SwiftErrorVal)
                      ? cast<Instruction>(SwiftErrorVal)->getDebugLoc()
                      : DebugLoc();
      const auto *TII = FuncInfo->MF->getSubtarget().getInstrInfo();

      // If we don't need a phi create a copy to the upward exposed vreg.
      if (!needPHI) {
        assert(UpwardsUse);
        assert(!VRegs.empty() &&
               "No predecessors?  Is the Calling Convention correct?");
        unsigned DestReg = UUseVReg;
        BuildMI(*MBB, MBB->getFirstNonPHI(), DLoc, TII->get(TargetOpcode::COPY),
                DestReg)
            .addReg(VRegs[0].second);
        continue;
      }

      // We need a phi: if there is an upwards exposed use we already have a
      // destination virtual register number otherwise we generate a new one.
      auto &DL = FuncInfo->MF->getDataLayout();
      auto const *RC = TLI->getRegClassFor(TLI->getPointerTy(DL));
      unsigned PHIVReg =
          UpwardsUse ? UUseVReg
                     : FuncInfo->MF->getRegInfo().createVirtualRegister(RC);
      MachineInstrBuilder SwiftErrorPHI =
          BuildMI(*MBB, MBB->getFirstNonPHI(), DLoc,
                  TII->get(TargetOpcode::PHI), PHIVReg);
      for (auto BBRegPair : VRegs) {
        SwiftErrorPHI.addReg(BBRegPair.second).addMBB(BBRegPair.first);
      }

      // We did not have a definition in this block before: store the phi's vreg
      // as this block downward exposed def.
      if (!UpwardsUse)
        FuncInfo->setCurrentSwiftErrorVReg(MBB, SwiftErrorVal, PHIVReg);
    }
  }
}

static void preassignSwiftErrorRegs(const TargetLowering *TLI,
                                    FunctionLoweringInfo *FuncInfo,
                                    BasicBlock::const_iterator Begin,
                                    BasicBlock::const_iterator End) {
  if (!TLI->supportSwiftError() || FuncInfo->SwiftErrorVals.empty())
    return;

  // Iterator over instructions and assign vregs to swifterror defs and uses.
  for (auto It = Begin; It != End; ++It) {
    ImmutableCallSite CS(&*It);
    if (CS) {
      // A call-site with a swifterror argument is both use and def.
      const Value *SwiftErrorAddr = nullptr;
      for (auto &Arg : CS.args()) {
        if (!Arg->isSwiftError())
          continue;
        // Use of swifterror.
        assert(!SwiftErrorAddr && "Cannot have multiple swifterror arguments");
        SwiftErrorAddr = &*Arg;
        assert(SwiftErrorAddr->isSwiftError() &&
               "Must have a swifterror value argument");
        unsigned VReg; bool CreatedReg;
        std::tie(VReg, CreatedReg) = FuncInfo->getOrCreateSwiftErrorVRegUseAt(
          &*It, FuncInfo->MBB, SwiftErrorAddr);
        assert(CreatedReg);
      }
      if (!SwiftErrorAddr)
        continue;

      // Def of swifterror.
      unsigned VReg; bool CreatedReg;
      std::tie(VReg, CreatedReg) =
          FuncInfo->getOrCreateSwiftErrorVRegDefAt(&*It);
      assert(CreatedReg);
      FuncInfo->setCurrentSwiftErrorVReg(FuncInfo->MBB, SwiftErrorAddr, VReg);

    // A load is a use.
    } else if (const LoadInst *LI = dyn_cast<const LoadInst>(&*It)) {
      const Value *V = LI->getOperand(0);
      if (!V->isSwiftError())
        continue;

      unsigned VReg; bool CreatedReg;
      std::tie(VReg, CreatedReg) =
          FuncInfo->getOrCreateSwiftErrorVRegUseAt(LI, FuncInfo->MBB, V);
      assert(CreatedReg);

    // A store is a def.
    } else if (const StoreInst *SI = dyn_cast<const StoreInst>(&*It)) {
      const Value *SwiftErrorAddr = SI->getOperand(1);
      if (!SwiftErrorAddr->isSwiftError())
        continue;

      // Def of swifterror.
      unsigned VReg; bool CreatedReg;
      std::tie(VReg, CreatedReg) =
          FuncInfo->getOrCreateSwiftErrorVRegDefAt(&*It);
      assert(CreatedReg);
      FuncInfo->setCurrentSwiftErrorVReg(FuncInfo->MBB, SwiftErrorAddr, VReg);

    // A return in a swiferror returning function is a use.
    } else if (const ReturnInst *R = dyn_cast<const ReturnInst>(&*It)) {
      const Function *F = R->getParent()->getParent();
      if(!F->getAttributes().hasAttrSomewhere(Attribute::SwiftError))
        continue;

      unsigned VReg; bool CreatedReg;
      std::tie(VReg, CreatedReg) = FuncInfo->getOrCreateSwiftErrorVRegUseAt(
          R, FuncInfo->MBB, FuncInfo->SwiftErrorArg);
      assert(CreatedReg);
    }
  }
}

void SelectionDAGISel::SelectAllBasicBlocks(const Function &Fn) {
  FastISelFailed = false;
  // Initialize the Fast-ISel state, if needed.
  FastISel *FastIS = nullptr;
  if (TM.Options.EnableFastISel) {
    LLVM_DEBUG(dbgs() << "Enabling fast-isel\n");
    FastIS = TLI->createFastISel(*FuncInfo, LibInfo);
  }

  setupSwiftErrorVals(Fn, TLI, FuncInfo);

  ReversePostOrderTraversal<const Function*> RPOT(&Fn);

  // Lower arguments up front. An RPO iteration always visits the entry block
  // first.
  assert(*RPOT.begin() == &Fn.getEntryBlock());
  ++NumEntryBlocks;

  // Set up FuncInfo for ISel. Entry blocks never have PHIs.
  FuncInfo->MBB = FuncInfo->MBBMap[&Fn.getEntryBlock()];
  FuncInfo->InsertPt = FuncInfo->MBB->begin();

  CurDAG->setFunctionLoweringInfo(FuncInfo);

  if (!FastIS) {
    LowerArguments(Fn);
  } else {
    // See if fast isel can lower the arguments.
    FastIS->startNewBlock();
    if (!FastIS->lowerArguments()) {
      FastISelFailed = true;
      // Fast isel failed to lower these arguments
      ++NumFastIselFailLowerArguments;

      OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                 Fn.getSubprogram(),
                                 &Fn.getEntryBlock());
      R << "FastISel didn't lower all arguments: "
        << ore::NV("Prototype", Fn.getType());
      reportFastISelFailure(*MF, *ORE, R, EnableFastISelAbort > 1);

      // Use SelectionDAG argument lowering
      LowerArguments(Fn);
      CurDAG->setRoot(SDB->getControlRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // If we inserted any instructions at the beginning, make a note of
    // where they are, so we can be sure to emit subsequent instructions
    // after them.
    if (FuncInfo->InsertPt != FuncInfo->MBB->begin())
      FastIS->setLastLocalValue(&*std::prev(FuncInfo->InsertPt));
    else
      FastIS->setLastLocalValue(nullptr);
  }
  createSwiftErrorEntriesInEntryBlock(FuncInfo, FastIS, TLI, TII, SDB);

  processDbgDeclares(FuncInfo);

  // Iterate over all basic blocks in the function.
  StackProtector &SP = getAnalysis<StackProtector>();
  for (const BasicBlock *LLVMBB : RPOT) {
    if (OptLevel != CodeGenOpt::None) {
      bool AllPredsVisited = true;
      for (const_pred_iterator PI = pred_begin(LLVMBB), PE = pred_end(LLVMBB);
           PI != PE; ++PI) {
        if (!FuncInfo->VisitedBBs.count(*PI)) {
          AllPredsVisited = false;
          break;
        }
      }

      if (AllPredsVisited) {
        for (const PHINode &PN : LLVMBB->phis())
          FuncInfo->ComputePHILiveOutRegInfo(&PN);
      } else {
        for (const PHINode &PN : LLVMBB->phis())
          FuncInfo->InvalidatePHILiveOutRegInfo(&PN);
      }

      FuncInfo->VisitedBBs.insert(LLVMBB);
    }

    BasicBlock::const_iterator const Begin =
        LLVMBB->getFirstNonPHI()->getIterator();
    BasicBlock::const_iterator const End = LLVMBB->end();
    BasicBlock::const_iterator BI = End;

    FuncInfo->MBB = FuncInfo->MBBMap[LLVMBB];
    if (!FuncInfo->MBB)
      continue; // Some blocks like catchpads have no code or MBB.

    // Insert new instructions after any phi or argument setup code.
    FuncInfo->InsertPt = FuncInfo->MBB->end();

    // Setup an EH landing-pad block.
    FuncInfo->ExceptionPointerVirtReg = 0;
    FuncInfo->ExceptionSelectorVirtReg = 0;
    if (LLVMBB->isEHPad())
      if (!PrepareEHLandingPad())
        continue;

    // Before doing SelectionDAG ISel, see if FastISel has been requested.
    if (FastIS) {
      if (LLVMBB != &Fn.getEntryBlock())
        FastIS->startNewBlock();

      unsigned NumFastIselRemaining = std::distance(Begin, End);

      // Pre-assign swifterror vregs.
      preassignSwiftErrorRegs(TLI, FuncInfo, Begin, End);

      // Do FastISel on as many instructions as possible.
      for (; BI != Begin; --BI) {
        const Instruction *Inst = &*std::prev(BI);

        // If we no longer require this instruction, skip it.
        if (isFoldedOrDeadInstruction(Inst, FuncInfo) ||
            ElidedArgCopyInstrs.count(Inst)) {
          --NumFastIselRemaining;
          continue;
        }

        // Bottom-up: reset the insert pos at the top, after any local-value
        // instructions.
        FastIS->recomputeInsertPt();

        // Try to select the instruction with FastISel.
        if (FastIS->selectInstruction(Inst)) {
          --NumFastIselRemaining;
          ++NumFastIselSuccess;
          // If fast isel succeeded, skip over all the folded instructions, and
          // then see if there is a load right before the selected instructions.
          // Try to fold the load if so.
          const Instruction *BeforeInst = Inst;
          while (BeforeInst != &*Begin) {
            BeforeInst = &*std::prev(BasicBlock::const_iterator(BeforeInst));
            if (!isFoldedOrDeadInstruction(BeforeInst, FuncInfo))
              break;
          }
          if (BeforeInst != Inst && isa<LoadInst>(BeforeInst) &&
              BeforeInst->hasOneUse() &&
              FastIS->tryToFoldLoad(cast<LoadInst>(BeforeInst), Inst)) {
            // If we succeeded, don't re-select the load.
            BI = std::next(BasicBlock::const_iterator(BeforeInst));
            --NumFastIselRemaining;
            ++NumFastIselSuccess;
          }
          continue;
        }

        FastISelFailed = true;

        // Then handle certain instructions as single-LLVM-Instruction blocks.
        // We cannot separate out GCrelocates to their own blocks since we need
        // to keep track of gc-relocates for a particular gc-statepoint. This is
        // done by SelectionDAGBuilder::LowerAsSTATEPOINT, called before
        // visitGCRelocate.
        if (isa<CallInst>(Inst) && !isStatepoint(Inst) && !isGCRelocate(Inst)) {
          OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                     Inst->getDebugLoc(), LLVMBB);

          R << "FastISel missed call";

          if (R.isEnabled() || EnableFastISelAbort) {
            std::string InstStrStorage;
            raw_string_ostream InstStr(InstStrStorage);
            InstStr << *Inst;

            R << ": " << InstStr.str();
          }

          reportFastISelFailure(*MF, *ORE, R, EnableFastISelAbort > 2);

          if (!Inst->getType()->isVoidTy() && !Inst->getType()->isTokenTy() &&
              !Inst->use_empty()) {
            unsigned &R = FuncInfo->ValueMap[Inst];
            if (!R)
              R = FuncInfo->CreateRegs(Inst->getType());
          }

          bool HadTailCall = false;
          MachineBasicBlock::iterator SavedInsertPt = FuncInfo->InsertPt;
          SelectBasicBlock(Inst->getIterator(), BI, HadTailCall);

          // If the call was emitted as a tail call, we're done with the block.
          // We also need to delete any previously emitted instructions.
          if (HadTailCall) {
            FastIS->removeDeadCode(SavedInsertPt, FuncInfo->MBB->end());
            --BI;
            break;
          }

          // Recompute NumFastIselRemaining as Selection DAG instruction
          // selection may have handled the call, input args, etc.
          unsigned RemainingNow = std::distance(Begin, BI);
          NumFastIselFailures += NumFastIselRemaining - RemainingNow;
          NumFastIselRemaining = RemainingNow;
          continue;
        }

        OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                   Inst->getDebugLoc(), LLVMBB);

        bool ShouldAbort = EnableFastISelAbort;
        if (Inst->isTerminator()) {
          // Use a different message for terminator misses.
          R << "FastISel missed terminator";
          // Don't abort for terminator unless the level is really high
          ShouldAbort = (EnableFastISelAbort > 2);
        } else {
          R << "FastISel missed";
        }

        if (R.isEnabled() || EnableFastISelAbort) {
          std::string InstStrStorage;
          raw_string_ostream InstStr(InstStrStorage);
          InstStr << *Inst;
          R << ": " << InstStr.str();
        }

        reportFastISelFailure(*MF, *ORE, R, ShouldAbort);

        NumFastIselFailures += NumFastIselRemaining;
        break;
      }

      FastIS->recomputeInsertPt();
    }

    if (SP.shouldEmitSDCheck(*LLVMBB)) {
      bool FunctionBasedInstrumentation =
          TLI->getSSPStackGuardCheck(*Fn.getParent());
      SDB->SPDescriptor.initialize(LLVMBB, FuncInfo->MBBMap[LLVMBB],
                                   FunctionBasedInstrumentation);
    }

    if (Begin != BI)
      ++NumDAGBlocks;
    else
      ++NumFastIselBlocks;

    if (Begin != BI) {
      // Run SelectionDAG instruction selection on the remainder of the block
      // not handled by FastISel. If FastISel is not run, this is the entire
      // block.
      bool HadTailCall;
      SelectBasicBlock(Begin, BI, HadTailCall);

      // But if FastISel was run, we already selected some of the block.
      // If we emitted a tail-call, we need to delete any previously emitted
      // instruction that follows it.
      if (HadTailCall && FuncInfo->InsertPt != FuncInfo->MBB->end())
        FastIS->removeDeadCode(FuncInfo->InsertPt, FuncInfo->MBB->end());
    }

    if (FastIS)
      FastIS->finishBasicBlock();
    FinishBasicBlock();
    FuncInfo->PHINodesToUpdate.clear();
    ElidedArgCopyInstrs.clear();
  }

  SP.copyToMachineFrameInfo(MF->getFrameInfo());

  propagateSwiftErrorVRegs(FuncInfo);

  delete FastIS;
  SDB->clearDanglingDebugInfo();
  SDB->SPDescriptor.resetPerFunctionState();
}

/// Given that the input MI is before a partial terminator sequence TSeq, return
/// true if M + TSeq also a partial terminator sequence.
///
/// A Terminator sequence is a sequence of MachineInstrs which at this point in
/// lowering copy vregs into physical registers, which are then passed into
/// terminator instructors so we can satisfy ABI constraints. A partial
/// terminator sequence is an improper subset of a terminator sequence (i.e. it
/// may be the whole terminator sequence).
static bool MIIsInTerminatorSequence(const MachineInstr &MI) {
  // If we do not have a copy or an implicit def, we return true if and only if
  // MI is a debug value.
  if (!MI.isCopy() && !MI.isImplicitDef())
    // Sometimes DBG_VALUE MI sneak in between the copies from the vregs to the
    // physical registers if there is debug info associated with the terminator
    // of our mbb. We want to include said debug info in our terminator
    // sequence, so we return true in that case.
    return MI.isDebugValue();

  // We have left the terminator sequence if we are not doing one of the
  // following:
  //
  // 1. Copying a vreg into a physical register.
  // 2. Copying a vreg into a vreg.
  // 3. Defining a register via an implicit def.

  // OPI should always be a register definition...
  MachineInstr::const_mop_iterator OPI = MI.operands_begin();
  if (!OPI->isReg() || !OPI->isDef())
    return false;

  // Defining any register via an implicit def is always ok.
  if (MI.isImplicitDef())
    return true;

  // Grab the copy source...
  MachineInstr::const_mop_iterator OPI2 = OPI;
  ++OPI2;
  assert(OPI2 != MI.operands_end()
         && "Should have a copy implying we should have 2 arguments.");

  // Make sure that the copy dest is not a vreg when the copy source is a
  // physical register.
  if (!OPI2->isReg() ||
      (!TargetRegisterInfo::isPhysicalRegister(OPI->getReg()) &&
       TargetRegisterInfo::isPhysicalRegister(OPI2->getReg())))
    return false;

  return true;
}

/// Find the split point at which to splice the end of BB into its success stack
/// protector check machine basic block.
///
/// On many platforms, due to ABI constraints, terminators, even before register
/// allocation, use physical registers. This creates an issue for us since
/// physical registers at this point can not travel across basic
/// blocks. Luckily, selectiondag always moves physical registers into vregs
/// when they enter functions and moves them through a sequence of copies back
/// into the physical registers right before the terminator creating a
/// ``Terminator Sequence''. This function is searching for the beginning of the
/// terminator sequence so that we can ensure that we splice off not just the
/// terminator, but additionally the copies that move the vregs into the
/// physical registers.
static MachineBasicBlock::iterator
FindSplitPointForStackProtector(MachineBasicBlock *BB) {
  MachineBasicBlock::iterator SplitPoint = BB->getFirstTerminator();
  //
  if (SplitPoint == BB->begin())
    return SplitPoint;

  MachineBasicBlock::iterator Start = BB->begin();
  MachineBasicBlock::iterator Previous = SplitPoint;
  --Previous;

  while (MIIsInTerminatorSequence(*Previous)) {
    SplitPoint = Previous;
    if (Previous == Start)
      break;
    --Previous;
  }

  return SplitPoint;
}

void
SelectionDAGISel::FinishBasicBlock() {
  LLVM_DEBUG(dbgs() << "Total amount of phi nodes to update: "
                    << FuncInfo->PHINodesToUpdate.size() << "\n";
             for (unsigned i = 0, e = FuncInfo->PHINodesToUpdate.size(); i != e;
                  ++i) dbgs()
             << "Node " << i << " : (" << FuncInfo->PHINodesToUpdate[i].first
             << ", " << FuncInfo->PHINodesToUpdate[i].second << ")\n");

  // Next, now that we know what the last MBB the LLVM BB expanded is, update
  // PHI nodes in successors.
  for (unsigned i = 0, e = FuncInfo->PHINodesToUpdate.size(); i != e; ++i) {
    MachineInstrBuilder PHI(*MF, FuncInfo->PHINodesToUpdate[i].first);
    assert(PHI->isPHI() &&
           "This is not a machine PHI node that we are updating!");
    if (!FuncInfo->MBB->isSuccessor(PHI->getParent()))
      continue;
    PHI.addReg(FuncInfo->PHINodesToUpdate[i].second).addMBB(FuncInfo->MBB);
  }

  // Handle stack protector.
  if (SDB->SPDescriptor.shouldEmitFunctionBasedCheckStackProtector()) {
    // The target provides a guard check function. There is no need to
    // generate error handling code or to split current basic block.
    MachineBasicBlock *ParentMBB = SDB->SPDescriptor.getParentMBB();

    // Add load and check to the basicblock.
    FuncInfo->MBB = ParentMBB;
    FuncInfo->InsertPt =
        FindSplitPointForStackProtector(ParentMBB);
    SDB->visitSPDescriptorParent(SDB->SPDescriptor, ParentMBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Clear the Per-BB State.
    SDB->SPDescriptor.resetPerBBState();
  } else if (SDB->SPDescriptor.shouldEmitStackProtector()) {
    MachineBasicBlock *ParentMBB = SDB->SPDescriptor.getParentMBB();
    MachineBasicBlock *SuccessMBB = SDB->SPDescriptor.getSuccessMBB();

    // Find the split point to split the parent mbb. At the same time copy all
    // physical registers used in the tail of parent mbb into virtual registers
    // before the split point and back into physical registers after the split
    // point. This prevents us needing to deal with Live-ins and many other
    // register allocation issues caused by us splitting the parent mbb. The
    // register allocator will clean up said virtual copies later on.
    MachineBasicBlock::iterator SplitPoint =
        FindSplitPointForStackProtector(ParentMBB);

    // Splice the terminator of ParentMBB into SuccessMBB.
    SuccessMBB->splice(SuccessMBB->end(), ParentMBB,
                       SplitPoint,
                       ParentMBB->end());

    // Add compare/jump on neq/jump to the parent BB.
    FuncInfo->MBB = ParentMBB;
    FuncInfo->InsertPt = ParentMBB->end();
    SDB->visitSPDescriptorParent(SDB->SPDescriptor, ParentMBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // CodeGen Failure MBB if we have not codegened it yet.
    MachineBasicBlock *FailureMBB = SDB->SPDescriptor.getFailureMBB();
    if (FailureMBB->empty()) {
      FuncInfo->MBB = FailureMBB;
      FuncInfo->InsertPt = FailureMBB->end();
      SDB->visitSPDescriptorFailure(SDB->SPDescriptor);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // Clear the Per-BB State.
    SDB->SPDescriptor.resetPerBBState();
  }

  // Lower each BitTestBlock.
  for (auto &BTB : SDB->BitTestCases) {
    // Lower header first, if it wasn't already lowered
    if (!BTB.Emitted) {
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = BTB.Parent;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code
      SDB->visitBitTestHeader(BTB, FuncInfo->MBB);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    BranchProbability UnhandledProb = BTB.Prob;
    for (unsigned j = 0, ej = BTB.Cases.size(); j != ej; ++j) {
      UnhandledProb -= BTB.Cases[j].ExtraProb;
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = BTB.Cases[j].ThisBB;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code

      // If all cases cover a contiguous range, it is not necessary to jump to
      // the default block after the last bit test fails. This is because the
      // range check during bit test header creation has guaranteed that every
      // case here doesn't go outside the range. In this case, there is no need
      // to perform the last bit test, as it will always be true. Instead, make
      // the second-to-last bit-test fall through to the target of the last bit
      // test, and delete the last bit test.

      MachineBasicBlock *NextMBB;
      if (BTB.ContiguousRange && j + 2 == ej) {
        // Second-to-last bit-test with contiguous range: fall through to the
        // target of the final bit test.
        NextMBB = BTB.Cases[j + 1].TargetBB;
      } else if (j + 1 == ej) {
        // For the last bit test, fall through to Default.
        NextMBB = BTB.Default;
      } else {
        // Otherwise, fall through to the next bit test.
        NextMBB = BTB.Cases[j + 1].ThisBB;
      }

      SDB->visitBitTestCase(BTB, NextMBB, UnhandledProb, BTB.Reg, BTB.Cases[j],
                            FuncInfo->MBB);

      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();

      if (BTB.ContiguousRange && j + 2 == ej) {
        // Since we're not going to use the final bit test, remove it.
        BTB.Cases.pop_back();
        break;
      }
    }

    // Update PHI Nodes
    for (unsigned pi = 0, pe = FuncInfo->PHINodesToUpdate.size();
         pi != pe; ++pi) {
      MachineInstrBuilder PHI(*MF, FuncInfo->PHINodesToUpdate[pi].first);
      MachineBasicBlock *PHIBB = PHI->getParent();
      assert(PHI->isPHI() &&
             "This is not a machine PHI node that we are updating!");
      // This is "default" BB. We have two jumps to it. From "header" BB and
      // from last "case" BB, unless the latter was skipped.
      if (PHIBB == BTB.Default) {
        PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second).addMBB(BTB.Parent);
        if (!BTB.ContiguousRange) {
          PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second)
              .addMBB(BTB.Cases.back().ThisBB);
         }
      }
      // One of "cases" BB.
      for (unsigned j = 0, ej = BTB.Cases.size();
           j != ej; ++j) {
        MachineBasicBlock* cBB = BTB.Cases[j].ThisBB;
        if (cBB->isSuccessor(PHIBB))
          PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second).addMBB(cBB);
      }
    }
  }
  SDB->BitTestCases.clear();

  // If the JumpTable record is filled in, then we need to emit a jump table.
  // Updating the PHI nodes is tricky in this case, since we need to determine
  // whether the PHI is a successor of the range check MBB or the jump table MBB
  for (unsigned i = 0, e = SDB->JTCases.size(); i != e; ++i) {
    // Lower header first, if it wasn't already lowered
    if (!SDB->JTCases[i].first.Emitted) {
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = SDB->JTCases[i].first.HeaderBB;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code
      SDB->visitJumpTableHeader(SDB->JTCases[i].second, SDB->JTCases[i].first,
                                FuncInfo->MBB);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // Set the current basic block to the mbb we wish to insert the code into
    FuncInfo->MBB = SDB->JTCases[i].second.MBB;
    FuncInfo->InsertPt = FuncInfo->MBB->end();
    // Emit the code
    SDB->visitJumpTable(SDB->JTCases[i].second);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Update PHI Nodes
    for (unsigned pi = 0, pe = FuncInfo->PHINodesToUpdate.size();
         pi != pe; ++pi) {
      MachineInstrBuilder PHI(*MF, FuncInfo->PHINodesToUpdate[pi].first);
      MachineBasicBlock *PHIBB = PHI->getParent();
      assert(PHI->isPHI() &&
             "This is not a machine PHI node that we are updating!");
      // "default" BB. We can go there only from header BB.
      if (PHIBB == SDB->JTCases[i].second.Default)
        PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second)
           .addMBB(SDB->JTCases[i].first.HeaderBB);
      // JT BB. Just iterate over successors here
      if (FuncInfo->MBB->isSuccessor(PHIBB))
        PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second).addMBB(FuncInfo->MBB);
    }
  }
  SDB->JTCases.clear();

  // If we generated any switch lowering information, build and codegen any
  // additional DAGs necessary.
  for (unsigned i = 0, e = SDB->SwitchCases.size(); i != e; ++i) {
    // Set the current basic block to the mbb we wish to insert the code into
    FuncInfo->MBB = SDB->SwitchCases[i].ThisBB;
    FuncInfo->InsertPt = FuncInfo->MBB->end();

    // Determine the unique successors.
    SmallVector<MachineBasicBlock *, 2> Succs;
    Succs.push_back(SDB->SwitchCases[i].TrueBB);
    if (SDB->SwitchCases[i].TrueBB != SDB->SwitchCases[i].FalseBB)
      Succs.push_back(SDB->SwitchCases[i].FalseBB);

    // Emit the code. Note that this could result in FuncInfo->MBB being split.
    SDB->visitSwitchCase(SDB->SwitchCases[i], FuncInfo->MBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Remember the last block, now that any splitting is done, for use in
    // populating PHI nodes in successors.
    MachineBasicBlock *ThisBB = FuncInfo->MBB;

    // Handle any PHI nodes in successors of this chunk, as if we were coming
    // from the original BB before switch expansion.  Note that PHI nodes can
    // occur multiple times in PHINodesToUpdate.  We have to be very careful to
    // handle them the right number of times.
    for (unsigned i = 0, e = Succs.size(); i != e; ++i) {
      FuncInfo->MBB = Succs[i];
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // FuncInfo->MBB may have been removed from the CFG if a branch was
      // constant folded.
      if (ThisBB->isSuccessor(FuncInfo->MBB)) {
        for (MachineBasicBlock::iterator
             MBBI = FuncInfo->MBB->begin(), MBBE = FuncInfo->MBB->end();
             MBBI != MBBE && MBBI->isPHI(); ++MBBI) {
          MachineInstrBuilder PHI(*MF, MBBI);
          // This value for this PHI node is recorded in PHINodesToUpdate.
          for (unsigned pn = 0; ; ++pn) {
            assert(pn != FuncInfo->PHINodesToUpdate.size() &&
                   "Didn't find PHI entry!");
            if (FuncInfo->PHINodesToUpdate[pn].first == PHI) {
              PHI.addReg(FuncInfo->PHINodesToUpdate[pn].second).addMBB(ThisBB);
              break;
            }
          }
        }
      }
    }
  }
  SDB->SwitchCases.clear();
}

/// Create the scheduler. If a specific scheduler was specified
/// via the SchedulerRegistry, use it, otherwise select the
/// one preferred by the target.
///
ScheduleDAGSDNodes *SelectionDAGISel::CreateScheduler() {
  return ISHeuristic(this, OptLevel);
}

//===----------------------------------------------------------------------===//
// Helper functions used by the generated instruction selector.
//===----------------------------------------------------------------------===//
// Calls to these methods are generated by tblgen.

/// CheckAndMask - The isel is trying to match something like (and X, 255).  If
/// the dag combiner simplified the 255, we still want to match.  RHS is the
/// actual value in the DAG on the RHS of an AND, and DesiredMaskS is the value
/// specified in the .td file (e.g. 255).
bool SelectionDAGISel::CheckAndMask(SDValue LHS, ConstantSDNode *RHS,
                                    int64_t DesiredMaskS) const {
  const APInt &ActualMask = RHS->getAPIntValue();
  const APInt &DesiredMask = APInt(LHS.getValueSizeInBits(), DesiredMaskS);

  // If the actual mask exactly matches, success!
  if (ActualMask == DesiredMask)
    return true;

  // If the actual AND mask is allowing unallowed bits, this doesn't match.
  if (!ActualMask.isSubsetOf(DesiredMask))
    return false;

  // Otherwise, the DAG Combiner may have proven that the value coming in is
  // either already zero or is not demanded.  Check for known zero input bits.
  APInt NeededMask = DesiredMask & ~ActualMask;
  if (CurDAG->MaskedValueIsZero(LHS, NeededMask))
    return true;

  // TODO: check to see if missing bits are just not demanded.

  // Otherwise, this pattern doesn't match.
  return false;
}

/// CheckOrMask - The isel is trying to match something like (or X, 255).  If
/// the dag combiner simplified the 255, we still want to match.  RHS is the
/// actual value in the DAG on the RHS of an OR, and DesiredMaskS is the value
/// specified in the .td file (e.g. 255).
bool SelectionDAGISel::CheckOrMask(SDValue LHS, ConstantSDNode *RHS,
                                   int64_t DesiredMaskS) const {
  const APInt &ActualMask = RHS->getAPIntValue();
  const APInt &DesiredMask = APInt(LHS.getValueSizeInBits(), DesiredMaskS);

  // If the actual mask exactly matches, success!
  if (ActualMask == DesiredMask)
    return true;

  // If the actual AND mask is allowing unallowed bits, this doesn't match.
  if (!ActualMask.isSubsetOf(DesiredMask))
    return false;

  // Otherwise, the DAG Combiner may have proven that the value coming in is
  // either already zero or is not demanded.  Check for known zero input bits.
  APInt NeededMask = DesiredMask & ~ActualMask;
  KnownBits Known = CurDAG->computeKnownBits(LHS);

  // If all the missing bits in the or are already known to be set, match!
  if (NeededMask.isSubsetOf(Known.One))
    return true;

  // TODO: check to see if missing bits are just not demanded.

  // Otherwise, this pattern doesn't match.
  return false;
}

/// SelectInlineAsmMemoryOperands - Calls to this are automatically generated
/// by tblgen.  Others should not call it.
void SelectionDAGISel::SelectInlineAsmMemoryOperands(std::vector<SDValue> &Ops,
                                                     const SDLoc &DL) {
  std::vector<SDValue> InOps;
  std::swap(InOps, Ops);

  Ops.push_back(InOps[InlineAsm::Op_InputChain]); // 0
  Ops.push_back(InOps[InlineAsm::Op_AsmString]);  // 1
  Ops.push_back(InOps[InlineAsm::Op_MDNode]);     // 2, !srcloc
  Ops.push_back(InOps[InlineAsm::Op_ExtraInfo]);  // 3 (SideEffect, AlignStack)

  unsigned i = InlineAsm::Op_FirstOperand, e = InOps.size();
  if (InOps[e-1].getValueType() == MVT::Glue)
    --e;  // Don't process a glue operand if it is here.

  while (i != e) {
    unsigned Flags = cast<ConstantSDNode>(InOps[i])->getZExtValue();
    if (!InlineAsm::isMemKind(Flags)) {
      // Just skip over this operand, copying the operands verbatim.
      Ops.insert(Ops.end(), InOps.begin()+i,
                 InOps.begin()+i+InlineAsm::getNumOperandRegisters(Flags) + 1);
      i += InlineAsm::getNumOperandRegisters(Flags) + 1;
    } else {
      assert(InlineAsm::getNumOperandRegisters(Flags) == 1 &&
             "Memory operand with multiple values?");

      unsigned TiedToOperand;
      if (InlineAsm::isUseOperandTiedToDef(Flags, TiedToOperand)) {
        // We need the constraint ID from the operand this is tied to.
        unsigned CurOp = InlineAsm::Op_FirstOperand;
        Flags = cast<ConstantSDNode>(InOps[CurOp])->getZExtValue();
        for (; TiedToOperand; --TiedToOperand) {
          CurOp += InlineAsm::getNumOperandRegisters(Flags)+1;
          Flags = cast<ConstantSDNode>(InOps[CurOp])->getZExtValue();
        }
      }

      // Otherwise, this is a memory operand.  Ask the target to select it.
      std::vector<SDValue> SelOps;
      unsigned ConstraintID = InlineAsm::getMemoryConstraintID(Flags);
      if (SelectInlineAsmMemoryOperand(InOps[i+1], ConstraintID, SelOps))
        report_fatal_error("Could not match memory address.  Inline asm"
                           " failure!");

      // Add this to the output node.
      unsigned NewFlags =
        InlineAsm::getFlagWord(InlineAsm::Kind_Mem, SelOps.size());
      NewFlags = InlineAsm::getFlagWordForMem(NewFlags, ConstraintID);
      Ops.push_back(CurDAG->getTargetConstant(NewFlags, DL, MVT::i32));
      Ops.insert(Ops.end(), SelOps.begin(), SelOps.end());
      i += 2;
    }
  }

  // Add the glue input back if present.
  if (e != InOps.size())
    Ops.push_back(InOps.back());
}

/// findGlueUse - Return use of MVT::Glue value produced by the specified
/// SDNode.
///
static SDNode *findGlueUse(SDNode *N) {
  unsigned FlagResNo = N->getNumValues()-1;
  for (SDNode::use_iterator I = N->use_begin(), E = N->use_end(); I != E; ++I) {
    SDUse &Use = I.getUse();
    if (Use.getResNo() == FlagResNo)
      return Use.getUser();
  }
  return nullptr;
}

/// findNonImmUse - Return true if "Def" is a predecessor of "Root" via a path
/// beyond "ImmedUse".  We may ignore chains as they are checked separately.
static bool findNonImmUse(SDNode *Root, SDNode *Def, SDNode *ImmedUse,
                          bool IgnoreChains) {
  SmallPtrSet<const SDNode *, 16> Visited;
  SmallVector<const SDNode *, 16> WorkList;
  // Only check if we have non-immediate uses of Def.
  if (ImmedUse->isOnlyUserOf(Def))
    return false;

  // We don't care about paths to Def that go through ImmedUse so mark it
  // visited and mark non-def operands as used.
  Visited.insert(ImmedUse);
  for (const SDValue &Op : ImmedUse->op_values()) {
    SDNode *N = Op.getNode();
    // Ignore chain deps (they are validated by
    // HandleMergeInputChains) and immediate uses
    if ((Op.getValueType() == MVT::Other && IgnoreChains) || N == Def)
      continue;
    if (!Visited.insert(N).second)
      continue;
    WorkList.push_back(N);
  }

  // Initialize worklist to operands of Root.
  if (Root != ImmedUse) {
    for (const SDValue &Op : Root->op_values()) {
      SDNode *N = Op.getNode();
      // Ignore chains (they are validated by HandleMergeInputChains)
      if ((Op.getValueType() == MVT::Other && IgnoreChains) || N == Def)
        continue;
      if (!Visited.insert(N).second)
        continue;
      WorkList.push_back(N);
    }
  }

  return SDNode::hasPredecessorHelper(Def, Visited, WorkList, 0, true);
}

/// IsProfitableToFold - Returns true if it's profitable to fold the specific
/// operand node N of U during instruction selection that starts at Root.
bool SelectionDAGISel::IsProfitableToFold(SDValue N, SDNode *U,
                                          SDNode *Root) const {
  if (OptLevel == CodeGenOpt::None) return false;
  return N.hasOneUse();
}

/// IsLegalToFold - Returns true if the specific operand node N of
/// U can be folded during instruction selection that starts at Root.
bool SelectionDAGISel::IsLegalToFold(SDValue N, SDNode *U, SDNode *Root,
                                     CodeGenOpt::Level OptLevel,
                                     bool IgnoreChains) {
  if (OptLevel == CodeGenOpt::None) return false;

  // If Root use can somehow reach N through a path that that doesn't contain
  // U then folding N would create a cycle. e.g. In the following
  // diagram, Root can reach N through X. If N is folded into Root, then
  // X is both a predecessor and a successor of U.
  //
  //          [N*]           //
  //         ^   ^           //
  //        /     \          //
  //      [U*]    [X]?       //
  //        ^     ^          //
  //         \   /           //
  //          \ /            //
  //         [Root*]         //
  //
  // * indicates nodes to be folded together.
  //
  // If Root produces glue, then it gets (even more) interesting. Since it
  // will be "glued" together with its glue use in the scheduler, we need to
  // check if it might reach N.
  //
  //          [N*]           //
  //         ^   ^           //
  //        /     \          //
  //      [U*]    [X]?       //
  //        ^       ^        //
  //         \       \       //
  //          \      |       //
  //         [Root*] |       //
  //          ^      |       //
  //          f      |       //
  //          |      /       //
  //         [Y]    /        //
  //           ^   /         //
  //           f  /          //
  //           | /           //
  //          [GU]           //
  //
  // If GU (glue use) indirectly reaches N (the load), and Root folds N
  // (call it Fold), then X is a predecessor of GU and a successor of
  // Fold. But since Fold and GU are glued together, this will create
  // a cycle in the scheduling graph.

  // If the node has glue, walk down the graph to the "lowest" node in the
  // glueged set.
  EVT VT = Root->getValueType(Root->getNumValues()-1);
  while (VT == MVT::Glue) {
    SDNode *GU = findGlueUse(Root);
    if (!GU)
      break;
    Root = GU;
    VT = Root->getValueType(Root->getNumValues()-1);

    // If our query node has a glue result with a use, we've walked up it.  If
    // the user (which has already been selected) has a chain or indirectly uses
    // the chain, HandleMergeInputChains will not consider it.  Because of
    // this, we cannot ignore chains in this predicate.
    IgnoreChains = false;
  }

  return !findNonImmUse(Root, N.getNode(), U, IgnoreChains);
}

void SelectionDAGISel::Select_INLINEASM(SDNode *N) {
  SDLoc DL(N);

  std::vector<SDValue> Ops(N->op_begin(), N->op_end());
  SelectInlineAsmMemoryOperands(Ops, DL);

  const EVT VTs[] = {MVT::Other, MVT::Glue};
  SDValue New = CurDAG->getNode(ISD::INLINEASM, DL, VTs, Ops);
  New->setNodeId(-1);
  ReplaceUses(N, New.getNode());
  CurDAG->RemoveDeadNode(N);
}

void SelectionDAGISel::Select_READ_REGISTER(SDNode *Op) {
  SDLoc dl(Op);
  MDNodeSDNode *MD = dyn_cast<MDNodeSDNode>(Op->getOperand(1));
  const MDString *RegStr = dyn_cast<MDString>(MD->getMD()->getOperand(0));
  unsigned Reg =
      TLI->getRegisterByName(RegStr->getString().data(), Op->getValueType(0),
                             *CurDAG);
  SDValue New = CurDAG->getCopyFromReg(
                        Op->getOperand(0), dl, Reg, Op->getValueType(0));
  New->setNodeId(-1);
  ReplaceUses(Op, New.getNode());
  CurDAG->RemoveDeadNode(Op);
}

void SelectionDAGISel::Select_WRITE_REGISTER(SDNode *Op) {
  SDLoc dl(Op);
  MDNodeSDNode *MD = dyn_cast<MDNodeSDNode>(Op->getOperand(1));
  const MDString *RegStr = dyn_cast<MDString>(MD->getMD()->getOperand(0));
  unsigned Reg = TLI->getRegisterByName(RegStr->getString().data(),
                                        Op->getOperand(2).getValueType(),
                                        *CurDAG);
  SDValue New = CurDAG->getCopyToReg(
                        Op->getOperand(0), dl, Reg, Op->getOperand(2));
  New->setNodeId(-1);
  ReplaceUses(Op, New.getNode());
  CurDAG->RemoveDeadNode(Op);
}

void SelectionDAGISel::Select_UNDEF(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::IMPLICIT_DEF, N->getValueType(0));
}

/// GetVBR - decode a vbr encoding whose top bit is set.
LLVM_ATTRIBUTE_ALWAYS_INLINE static inline uint64_t
GetVBR(uint64_t Val, const unsigned char *MatcherTable, unsigned &Idx) {
  assert(Val >= 128 && "Not a VBR");
  Val &= 127;  // Remove first vbr bit.

  unsigned Shift = 7;
  uint64_t NextBits;
  do {
    NextBits = MatcherTable[Idx++];
    Val |= (NextBits&127) << Shift;
    Shift += 7;
  } while (NextBits & 128);

  return Val;
}

/// When a match is complete, this method updates uses of interior chain results
/// to use the new results.
void SelectionDAGISel::UpdateChains(
    SDNode *NodeToMatch, SDValue InputChain,
    SmallVectorImpl<SDNode *> &ChainNodesMatched, bool isMorphNodeTo) {
  SmallVector<SDNode*, 4> NowDeadNodes;

  // Now that all the normal results are replaced, we replace the chain and
  // glue results if present.
  if (!ChainNodesMatched.empty()) {
    assert(InputChain.getNode() &&
           "Matched input chains but didn't produce a chain");
    // Loop over all of the nodes we matched that produced a chain result.
    // Replace all the chain results with the final chain we ended up with.
    for (unsigned i = 0, e = ChainNodesMatched.size(); i != e; ++i) {
      SDNode *ChainNode = ChainNodesMatched[i];
      // If ChainNode is null, it's because we replaced it on a previous
      // iteration and we cleared it out of the map. Just skip it.
      if (!ChainNode)
        continue;

      assert(ChainNode->getOpcode() != ISD::DELETED_NODE &&
             "Deleted node left in chain");

      // Don't replace the results of the root node if we're doing a
      // MorphNodeTo.
      if (ChainNode == NodeToMatch && isMorphNodeTo)
        continue;

      SDValue ChainVal = SDValue(ChainNode, ChainNode->getNumValues()-1);
      if (ChainVal.getValueType() == MVT::Glue)
        ChainVal = ChainVal.getValue(ChainVal->getNumValues()-2);
      assert(ChainVal.getValueType() == MVT::Other && "Not a chain?");
      SelectionDAG::DAGNodeDeletedListener NDL(
          *CurDAG, [&](SDNode *N, SDNode *E) {
            std::replace(ChainNodesMatched.begin(), ChainNodesMatched.end(), N,
                         static_cast<SDNode *>(nullptr));
          });
      if (ChainNode->getOpcode() != ISD::TokenFactor)
        ReplaceUses(ChainVal, InputChain);

      // If the node became dead and we haven't already seen it, delete it.
      if (ChainNode != NodeToMatch && ChainNode->use_empty() &&
          !std::count(NowDeadNodes.begin(), NowDeadNodes.end(), ChainNode))
        NowDeadNodes.push_back(ChainNode);
    }
  }

  if (!NowDeadNodes.empty())
    CurDAG->RemoveDeadNodes(NowDeadNodes);

  LLVM_DEBUG(dbgs() << "ISEL: Match complete!\n");
}

/// HandleMergeInputChains - This implements the OPC_EmitMergeInputChains
/// operation for when the pattern matched at least one node with a chains.  The
/// input vector contains a list of all of the chained nodes that we match.  We
/// must determine if this is a valid thing to cover (i.e. matching it won't
/// induce cycles in the DAG) and if so, creating a TokenFactor node. that will
/// be used as the input node chain for the generated nodes.
static SDValue
HandleMergeInputChains(SmallVectorImpl<SDNode*> &ChainNodesMatched,
                       SelectionDAG *CurDAG) {

  SmallPtrSet<const SDNode *, 16> Visited;
  SmallVector<const SDNode *, 8> Worklist;
  SmallVector<SDValue, 3> InputChains;
  unsigned int Max = 8192;

  // Quick exit on trivial merge.
  if (ChainNodesMatched.size() == 1)
    return ChainNodesMatched[0]->getOperand(0);

  // Add chains that aren't already added (internal). Peek through
  // token factors.
  std::function<void(const SDValue)> AddChains = [&](const SDValue V) {
    if (V.getValueType() != MVT::Other)
      return;
    if (V->getOpcode() == ISD::EntryToken)
      return;
    if (!Visited.insert(V.getNode()).second)
      return;
    if (V->getOpcode() == ISD::TokenFactor) {
      for (const SDValue &Op : V->op_values())
        AddChains(Op);
    } else
      InputChains.push_back(V);
  };

  for (auto *N : ChainNodesMatched) {
    Worklist.push_back(N);
    Visited.insert(N);
  }

  while (!Worklist.empty())
    AddChains(Worklist.pop_back_val()->getOperand(0));

  // Skip the search if there are no chain dependencies.
  if (InputChains.size() == 0)
    return CurDAG->getEntryNode();

  // If one of these chains is a successor of input, we must have a
  // node that is both the predecessor and successor of the
  // to-be-merged nodes. Fail.
  Visited.clear();
  for (SDValue V : InputChains)
    Worklist.push_back(V.getNode());

  for (auto *N : ChainNodesMatched)
    if (SDNode::hasPredecessorHelper(N, Visited, Worklist, Max, true))
      return SDValue();

  // Return merged chain.
  if (InputChains.size() == 1)
    return InputChains[0];
  return CurDAG->getNode(ISD::TokenFactor, SDLoc(ChainNodesMatched[0]),
                         MVT::Other, InputChains);
}

/// MorphNode - Handle morphing a node in place for the selector.
SDNode *SelectionDAGISel::
MorphNode(SDNode *Node, unsigned TargetOpc, SDVTList VTList,
          ArrayRef<SDValue> Ops, unsigned EmitNodeInfo) {
  // It is possible we're using MorphNodeTo to replace a node with no
  // normal results with one that has a normal result (or we could be
  // adding a chain) and the input could have glue and chains as well.
  // In this case we need to shift the operands down.
  // FIXME: This is a horrible hack and broken in obscure cases, no worse
  // than the old isel though.
  int OldGlueResultNo = -1, OldChainResultNo = -1;

  unsigned NTMNumResults = Node->getNumValues();
  if (Node->getValueType(NTMNumResults-1) == MVT::Glue) {
    OldGlueResultNo = NTMNumResults-1;
    if (NTMNumResults != 1 &&
        Node->getValueType(NTMNumResults-2) == MVT::Other)
      OldChainResultNo = NTMNumResults-2;
  } else if (Node->getValueType(NTMNumResults-1) == MVT::Other)
    OldChainResultNo = NTMNumResults-1;

  // Call the underlying SelectionDAG routine to do the transmogrification. Note
  // that this deletes operands of the old node that become dead.
  SDNode *Res = CurDAG->MorphNodeTo(Node, ~TargetOpc, VTList, Ops);

  // MorphNodeTo can operate in two ways: if an existing node with the
  // specified operands exists, it can just return it.  Otherwise, it
  // updates the node in place to have the requested operands.
  if (Res == Node) {
    // If we updated the node in place, reset the node ID.  To the isel,
    // this should be just like a newly allocated machine node.
    Res->setNodeId(-1);
  }

  unsigned ResNumResults = Res->getNumValues();
  // Move the glue if needed.
  if ((EmitNodeInfo & OPFL_GlueOutput) && OldGlueResultNo != -1 &&
      (unsigned)OldGlueResultNo != ResNumResults-1)
    ReplaceUses(SDValue(Node, OldGlueResultNo),
                SDValue(Res, ResNumResults - 1));

  if ((EmitNodeInfo & OPFL_GlueOutput) != 0)
    --ResNumResults;

  // Move the chain reference if needed.
  if ((EmitNodeInfo & OPFL_Chain) && OldChainResultNo != -1 &&
      (unsigned)OldChainResultNo != ResNumResults-1)
    ReplaceUses(SDValue(Node, OldChainResultNo),
                SDValue(Res, ResNumResults - 1));

  // Otherwise, no replacement happened because the node already exists. Replace
  // Uses of the old node with the new one.
  if (Res != Node) {
    ReplaceNode(Node, Res);
  } else {
    EnforceNodeIdInvariant(Res);
  }

  return Res;
}

/// CheckSame - Implements OP_CheckSame.
LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckSame(const unsigned char *MatcherTable, unsigned &MatcherIndex,
          SDValue N,
          const SmallVectorImpl<std::pair<SDValue, SDNode*>> &RecordedNodes) {
  // Accept if it is exactly the same as a previously recorded node.
  unsigned RecNo = MatcherTable[MatcherIndex++];
  assert(RecNo < RecordedNodes.size() && "Invalid CheckSame");
  return N == RecordedNodes[RecNo].first;
}

/// CheckChildSame - Implements OP_CheckChildXSame.
LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckChildSame(const unsigned char *MatcherTable, unsigned &MatcherIndex,
              SDValue N,
              const SmallVectorImpl<std::pair<SDValue, SDNode*>> &RecordedNodes,
              unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false;  // Match fails if out of range child #.
  return ::CheckSame(MatcherTable, MatcherIndex, N.getOperand(ChildNo),
                     RecordedNodes);
}

/// CheckPatternPredicate - Implements OP_CheckPatternPredicate.
LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckPatternPredicate(const unsigned char *MatcherTable, unsigned &MatcherIndex,
                      const SelectionDAGISel &SDISel) {
  return SDISel.CheckPatternPredicate(MatcherTable[MatcherIndex++]);
}

/// CheckNodePredicate - Implements OP_CheckNodePredicate.
LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckNodePredicate(const unsigned char *MatcherTable, unsigned &MatcherIndex,
                   const SelectionDAGISel &SDISel, SDNode *N) {
  return SDISel.CheckNodePredicate(N, MatcherTable[MatcherIndex++]);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckOpcode(const unsigned char *MatcherTable, unsigned &MatcherIndex,
            SDNode *N) {
  uint16_t Opc = MatcherTable[MatcherIndex++];
  Opc |= (unsigned short)MatcherTable[MatcherIndex++] << 8;
  return N->getOpcode() == Opc;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckType(const unsigned char *MatcherTable, unsigned &MatcherIndex, SDValue N,
          const TargetLowering *TLI, const DataLayout &DL) {
  MVT::SimpleValueType VT = (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
  if (N.getValueType() == VT) return true;

  // Handle the case when VT is iPTR.
  return VT == MVT::iPTR && N.getValueType() == TLI->getPointerTy(DL);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckChildType(const unsigned char *MatcherTable, unsigned &MatcherIndex,
               SDValue N, const TargetLowering *TLI, const DataLayout &DL,
               unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false;  // Match fails if out of range child #.
  return ::CheckType(MatcherTable, MatcherIndex, N.getOperand(ChildNo), TLI,
                     DL);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckCondCode(const unsigned char *MatcherTable, unsigned &MatcherIndex,
              SDValue N) {
  return cast<CondCodeSDNode>(N)->get() ==
      (ISD::CondCode)MatcherTable[MatcherIndex++];
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckValueType(const unsigned char *MatcherTable, unsigned &MatcherIndex,
               SDValue N, const TargetLowering *TLI, const DataLayout &DL) {
  MVT::SimpleValueType VT = (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
  if (cast<VTSDNode>(N)->getVT() == VT)
    return true;

  // Handle the case when VT is iPTR.
  return VT == MVT::iPTR && cast<VTSDNode>(N)->getVT() == TLI->getPointerTy(DL);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckInteger(const unsigned char *MatcherTable, unsigned &MatcherIndex,
             SDValue N) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N);
  return C && C->getSExtValue() == Val;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckChildInteger(const unsigned char *MatcherTable, unsigned &MatcherIndex,
                  SDValue N, unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false;  // Match fails if out of range child #.
  return ::CheckInteger(MatcherTable, MatcherIndex, N.getOperand(ChildNo));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckAndImm(const unsigned char *MatcherTable, unsigned &MatcherIndex,
            SDValue N, const SelectionDAGISel &SDISel) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  if (N->getOpcode() != ISD::AND) return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  return C && SDISel.CheckAndMask(N.getOperand(0), C, Val);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static inline bool
CheckOrImm(const unsigned char *MatcherTable, unsigned &MatcherIndex,
           SDValue N, const SelectionDAGISel &SDISel) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  if (N->getOpcode() != ISD::OR) return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  return C && SDISel.CheckOrMask(N.getOperand(0), C, Val);
}

/// IsPredicateKnownToFail - If we know how and can do so without pushing a
/// scope, evaluate the current node.  If the current predicate is known to
/// fail, set Result=true and return anything.  If the current predicate is
/// known to pass, set Result=false and return the MatcherIndex to continue
/// with.  If the current predicate is unknown, set Result=false and return the
/// MatcherIndex to continue with.
static unsigned IsPredicateKnownToFail(const unsigned char *Table,
                                       unsigned Index, SDValue N,
                                       bool &Result,
                                       const SelectionDAGISel &SDISel,
                  SmallVectorImpl<std::pair<SDValue, SDNode*>> &RecordedNodes) {
  switch (Table[Index++]) {
  default:
    Result = false;
    return Index-1;  // Could not evaluate this predicate.
  case SelectionDAGISel::OPC_CheckSame:
    Result = !::CheckSame(Table, Index, N, RecordedNodes);
    return Index;
  case SelectionDAGISel::OPC_CheckChild0Same:
  case SelectionDAGISel::OPC_CheckChild1Same:
  case SelectionDAGISel::OPC_CheckChild2Same:
  case SelectionDAGISel::OPC_CheckChild3Same:
    Result = !::CheckChildSame(Table, Index, N, RecordedNodes,
                        Table[Index-1] - SelectionDAGISel::OPC_CheckChild0Same);
    return Index;
  case SelectionDAGISel::OPC_CheckPatternPredicate:
    Result = !::CheckPatternPredicate(Table, Index, SDISel);
    return Index;
  case SelectionDAGISel::OPC_CheckPredicate:
    Result = !::CheckNodePredicate(Table, Index, SDISel, N.getNode());
    return Index;
  case SelectionDAGISel::OPC_CheckOpcode:
    Result = !::CheckOpcode(Table, Index, N.getNode());
    return Index;
  case SelectionDAGISel::OPC_CheckType:
    Result = !::CheckType(Table, Index, N, SDISel.TLI,
                          SDISel.CurDAG->getDataLayout());
    return Index;
  case SelectionDAGISel::OPC_CheckTypeRes: {
    unsigned Res = Table[Index++];
    Result = !::CheckType(Table, Index, N.getValue(Res), SDISel.TLI,
                          SDISel.CurDAG->getDataLayout());
    return Index;
  }
  case SelectionDAGISel::OPC_CheckChild0Type:
  case SelectionDAGISel::OPC_CheckChild1Type:
  case SelectionDAGISel::OPC_CheckChild2Type:
  case SelectionDAGISel::OPC_CheckChild3Type:
  case SelectionDAGISel::OPC_CheckChild4Type:
  case SelectionDAGISel::OPC_CheckChild5Type:
  case SelectionDAGISel::OPC_CheckChild6Type:
  case SelectionDAGISel::OPC_CheckChild7Type:
    Result = !::CheckChildType(
                 Table, Index, N, SDISel.TLI, SDISel.CurDAG->getDataLayout(),
                 Table[Index - 1] - SelectionDAGISel::OPC_CheckChild0Type);
    return Index;
  case SelectionDAGISel::OPC_CheckCondCode:
    Result = !::CheckCondCode(Table, Index, N);
    return Index;
  case SelectionDAGISel::OPC_CheckValueType:
    Result = !::CheckValueType(Table, Index, N, SDISel.TLI,
                               SDISel.CurDAG->getDataLayout());
    return Index;
  case SelectionDAGISel::OPC_CheckInteger:
    Result = !::CheckInteger(Table, Index, N);
    return Index;
  case SelectionDAGISel::OPC_CheckChild0Integer:
  case SelectionDAGISel::OPC_CheckChild1Integer:
  case SelectionDAGISel::OPC_CheckChild2Integer:
  case SelectionDAGISel::OPC_CheckChild3Integer:
  case SelectionDAGISel::OPC_CheckChild4Integer:
    Result = !::CheckChildInteger(Table, Index, N,
                     Table[Index-1] - SelectionDAGISel::OPC_CheckChild0Integer);
    return Index;
  case SelectionDAGISel::OPC_CheckAndImm:
    Result = !::CheckAndImm(Table, Index, N, SDISel);
    return Index;
  case SelectionDAGISel::OPC_CheckOrImm:
    Result = !::CheckOrImm(Table, Index, N, SDISel);
    return Index;
  }
}

namespace {

struct MatchScope {
  /// FailIndex - If this match fails, this is the index to continue with.
  unsigned FailIndex;

  /// NodeStack - The node stack when the scope was formed.
  SmallVector<SDValue, 4> NodeStack;

  /// NumRecordedNodes - The number of recorded nodes when the scope was formed.
  unsigned NumRecordedNodes;

  /// NumMatchedMemRefs - The number of matched memref entries.
  unsigned NumMatchedMemRefs;

  /// InputChain/InputGlue - The current chain/glue
  SDValue InputChain, InputGlue;

  /// HasChainNodesMatched - True if the ChainNodesMatched list is non-empty.
  bool HasChainNodesMatched;
};

/// \A DAG update listener to keep the matching state
/// (i.e. RecordedNodes and MatchScope) uptodate if the target is allowed to
/// change the DAG while matching.  X86 addressing mode matcher is an example
/// for this.
class MatchStateUpdater : public SelectionDAG::DAGUpdateListener
{
  SDNode **NodeToMatch;
  SmallVectorImpl<std::pair<SDValue, SDNode *>> &RecordedNodes;
  SmallVectorImpl<MatchScope> &MatchScopes;

public:
  MatchStateUpdater(SelectionDAG &DAG, SDNode **NodeToMatch,
                    SmallVectorImpl<std::pair<SDValue, SDNode *>> &RN,
                    SmallVectorImpl<MatchScope> &MS)
      : SelectionDAG::DAGUpdateListener(DAG), NodeToMatch(NodeToMatch),
        RecordedNodes(RN), MatchScopes(MS) {}

  void NodeDeleted(SDNode *N, SDNode *E) override {
    // Some early-returns here to avoid the search if we deleted the node or
    // if the update comes from MorphNodeTo (MorphNodeTo is the last thing we
    // do, so it's unnecessary to update matching state at that point).
    // Neither of these can occur currently because we only install this
    // update listener during matching a complex patterns.
    if (!E || E->isMachineOpcode())
      return;
    // Check if NodeToMatch was updated.
    if (N == *NodeToMatch)
      *NodeToMatch = E;
    // Performing linear search here does not matter because we almost never
    // run this code.  You'd have to have a CSE during complex pattern
    // matching.
    for (auto &I : RecordedNodes)
      if (I.first.getNode() == N)
        I.first.setNode(E);

    for (auto &I : MatchScopes)
      for (auto &J : I.NodeStack)
        if (J.getNode() == N)
          J.setNode(E);
  }
};

} // end anonymous namespace

void SelectionDAGISel::SelectCodeCommon(SDNode *NodeToMatch,
                                        const unsigned char *MatcherTable,
                                        unsigned TableSize) {
  // FIXME: Should these even be selected?  Handle these cases in the caller?
  switch (NodeToMatch->getOpcode()) {
  default:
    break;
  case ISD::EntryToken:       // These nodes remain the same.
  case ISD::BasicBlock:
  case ISD::Register:
  case ISD::RegisterMask:
  case ISD::HANDLENODE:
  case ISD::MDNODE_SDNODE:
  case ISD::TargetConstant:
  case ISD::TargetConstantFP:
  case ISD::TargetConstantPool:
  case ISD::TargetFrameIndex:
  case ISD::TargetExternalSymbol:
  case ISD::MCSymbol:
  case ISD::TargetBlockAddress:
  case ISD::TargetJumpTable:
  case ISD::TargetGlobalTLSAddress:
  case ISD::TargetGlobalAddress:
  case ISD::TokenFactor:
  case ISD::CopyFromReg:
  case ISD::CopyToReg:
  case ISD::EH_LABEL:
  case ISD::ANNOTATION_LABEL:
  case ISD::LIFETIME_START:
  case ISD::LIFETIME_END:
    NodeToMatch->setNodeId(-1); // Mark selected.
    return;
  case ISD::AssertSext:
  case ISD::AssertZext:
    ReplaceUses(SDValue(NodeToMatch, 0), NodeToMatch->getOperand(0));
    CurDAG->RemoveDeadNode(NodeToMatch);
    return;
  case ISD::INLINEASM:
    Select_INLINEASM(NodeToMatch);
    return;
  case ISD::READ_REGISTER:
    Select_READ_REGISTER(NodeToMatch);
    return;
  case ISD::WRITE_REGISTER:
    Select_WRITE_REGISTER(NodeToMatch);
    return;
  case ISD::UNDEF:
    Select_UNDEF(NodeToMatch);
    return;
  }

  assert(!NodeToMatch->isMachineOpcode() && "Node already selected!");

  // Set up the node stack with NodeToMatch as the only node on the stack.
  SmallVector<SDValue, 8> NodeStack;
  SDValue N = SDValue(NodeToMatch, 0);
  NodeStack.push_back(N);

  // MatchScopes - Scopes used when matching, if a match failure happens, this
  // indicates where to continue checking.
  SmallVector<MatchScope, 8> MatchScopes;

  // RecordedNodes - This is the set of nodes that have been recorded by the
  // state machine.  The second value is the parent of the node, or null if the
  // root is recorded.
  SmallVector<std::pair<SDValue, SDNode*>, 8> RecordedNodes;

  // MatchedMemRefs - This is the set of MemRef's we've seen in the input
  // pattern.
  SmallVector<MachineMemOperand*, 2> MatchedMemRefs;

  // These are the current input chain and glue for use when generating nodes.
  // Various Emit operations change these.  For example, emitting a copytoreg
  // uses and updates these.
  SDValue InputChain, InputGlue;

  // ChainNodesMatched - If a pattern matches nodes that have input/output
  // chains, the OPC_EmitMergeInputChains operation is emitted which indicates
  // which ones they are.  The result is captured into this list so that we can
  // update the chain results when the pattern is complete.
  SmallVector<SDNode*, 3> ChainNodesMatched;

  LLVM_DEBUG(dbgs() << "ISEL: Starting pattern match\n");

  // Determine where to start the interpreter.  Normally we start at opcode #0,
  // but if the state machine starts with an OPC_SwitchOpcode, then we
  // accelerate the first lookup (which is guaranteed to be hot) with the
  // OpcodeOffset table.
  unsigned MatcherIndex = 0;

  if (!OpcodeOffset.empty()) {
    // Already computed the OpcodeOffset table, just index into it.
    if (N.getOpcode() < OpcodeOffset.size())
      MatcherIndex = OpcodeOffset[N.getOpcode()];
    LLVM_DEBUG(dbgs() << "  Initial Opcode index to " << MatcherIndex << "\n");

  } else if (MatcherTable[0] == OPC_SwitchOpcode) {
    // Otherwise, the table isn't computed, but the state machine does start
    // with an OPC_SwitchOpcode instruction.  Populate the table now, since this
    // is the first time we're selecting an instruction.
    unsigned Idx = 1;
    while (true) {
      // Get the size of this case.
      unsigned CaseSize = MatcherTable[Idx++];
      if (CaseSize & 128)
        CaseSize = GetVBR(CaseSize, MatcherTable, Idx);
      if (CaseSize == 0) break;

      // Get the opcode, add the index to the table.
      uint16_t Opc = MatcherTable[Idx++];
      Opc |= (unsigned short)MatcherTable[Idx++] << 8;
      if (Opc >= OpcodeOffset.size())
        OpcodeOffset.resize((Opc+1)*2);
      OpcodeOffset[Opc] = Idx;
      Idx += CaseSize;
    }

    // Okay, do the lookup for the first opcode.
    if (N.getOpcode() < OpcodeOffset.size())
      MatcherIndex = OpcodeOffset[N.getOpcode()];
  }

  while (true) {
    assert(MatcherIndex < TableSize && "Invalid index");
#ifndef NDEBUG
    unsigned CurrentOpcodeIndex = MatcherIndex;
#endif
    BuiltinOpcodes Opcode = (BuiltinOpcodes)MatcherTable[MatcherIndex++];
    switch (Opcode) {
    case OPC_Scope: {
      // Okay, the semantics of this operation are that we should push a scope
      // then evaluate the first child.  However, pushing a scope only to have
      // the first check fail (which then pops it) is inefficient.  If we can
      // determine immediately that the first check (or first several) will
      // immediately fail, don't even bother pushing a scope for them.
      unsigned FailIndex;

      while (true) {
        unsigned NumToSkip = MatcherTable[MatcherIndex++];
        if (NumToSkip & 128)
          NumToSkip = GetVBR(NumToSkip, MatcherTable, MatcherIndex);
        // Found the end of the scope with no match.
        if (NumToSkip == 0) {
          FailIndex = 0;
          break;
        }

        FailIndex = MatcherIndex+NumToSkip;

        unsigned MatcherIndexOfPredicate = MatcherIndex;
        (void)MatcherIndexOfPredicate; // silence warning.

        // If we can't evaluate this predicate without pushing a scope (e.g. if
        // it is a 'MoveParent') or if the predicate succeeds on this node, we
        // push the scope and evaluate the full predicate chain.
        bool Result;
        MatcherIndex = IsPredicateKnownToFail(MatcherTable, MatcherIndex, N,
                                              Result, *this, RecordedNodes);
        if (!Result)
          break;

        LLVM_DEBUG(
            dbgs() << "  Skipped scope entry (due to false predicate) at "
                   << "index " << MatcherIndexOfPredicate << ", continuing at "
                   << FailIndex << "\n");
        ++NumDAGIselRetries;

        // Otherwise, we know that this case of the Scope is guaranteed to fail,
        // move to the next case.
        MatcherIndex = FailIndex;
      }

      // If the whole scope failed to match, bail.
      if (FailIndex == 0) break;

      // Push a MatchScope which indicates where to go if the first child fails
      // to match.
      MatchScope NewEntry;
      NewEntry.FailIndex = FailIndex;
      NewEntry.NodeStack.append(NodeStack.begin(), NodeStack.end());
      NewEntry.NumRecordedNodes = RecordedNodes.size();
      NewEntry.NumMatchedMemRefs = MatchedMemRefs.size();
      NewEntry.InputChain = InputChain;
      NewEntry.InputGlue = InputGlue;
      NewEntry.HasChainNodesMatched = !ChainNodesMatched.empty();
      MatchScopes.push_back(NewEntry);
      continue;
    }
    case OPC_RecordNode: {
      // Remember this node, it may end up being an operand in the pattern.
      SDNode *Parent = nullptr;
      if (NodeStack.size() > 1)
        Parent = NodeStack[NodeStack.size()-2].getNode();
      RecordedNodes.push_back(std::make_pair(N, Parent));
      continue;
    }

    case OPC_RecordChild0: case OPC_RecordChild1:
    case OPC_RecordChild2: case OPC_RecordChild3:
    case OPC_RecordChild4: case OPC_RecordChild5:
    case OPC_RecordChild6: case OPC_RecordChild7: {
      unsigned ChildNo = Opcode-OPC_RecordChild0;
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.

      RecordedNodes.push_back(std::make_pair(N->getOperand(ChildNo),
                                             N.getNode()));
      continue;
    }
    case OPC_RecordMemRef:
      if (auto *MN = dyn_cast<MemSDNode>(N))
        MatchedMemRefs.push_back(MN->getMemOperand());
      else {
        LLVM_DEBUG(dbgs() << "Expected MemSDNode "; N->dump(CurDAG);
                   dbgs() << '\n');
      }

      continue;

    case OPC_CaptureGlueInput:
      // If the current node has an input glue, capture it in InputGlue.
      if (N->getNumOperands() != 0 &&
          N->getOperand(N->getNumOperands()-1).getValueType() == MVT::Glue)
        InputGlue = N->getOperand(N->getNumOperands()-1);
      continue;

    case OPC_MoveChild: {
      unsigned ChildNo = MatcherTable[MatcherIndex++];
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.
      N = N.getOperand(ChildNo);
      NodeStack.push_back(N);
      continue;
    }

    case OPC_MoveChild0: case OPC_MoveChild1:
    case OPC_MoveChild2: case OPC_MoveChild3:
    case OPC_MoveChild4: case OPC_MoveChild5:
    case OPC_MoveChild6: case OPC_MoveChild7: {
      unsigned ChildNo = Opcode-OPC_MoveChild0;
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.
      N = N.getOperand(ChildNo);
      NodeStack.push_back(N);
      continue;
    }

    case OPC_MoveParent:
      // Pop the current node off the NodeStack.
      NodeStack.pop_back();
      assert(!NodeStack.empty() && "Node stack imbalance!");
      N = NodeStack.back();
      continue;

    case OPC_CheckSame:
      if (!::CheckSame(MatcherTable, MatcherIndex, N, RecordedNodes)) break;
      continue;

    case OPC_CheckChild0Same: case OPC_CheckChild1Same:
    case OPC_CheckChild2Same: case OPC_CheckChild3Same:
      if (!::CheckChildSame(MatcherTable, MatcherIndex, N, RecordedNodes,
                            Opcode-OPC_CheckChild0Same))
        break;
      continue;

    case OPC_CheckPatternPredicate:
      if (!::CheckPatternPredicate(MatcherTable, MatcherIndex, *this)) break;
      continue;
    case OPC_CheckPredicate:
      if (!::CheckNodePredicate(MatcherTable, MatcherIndex, *this,
                                N.getNode()))
        break;
      continue;
    case OPC_CheckPredicateWithOperands: {
      unsigned OpNum = MatcherTable[MatcherIndex++];
      SmallVector<SDValue, 8> Operands;

      for (unsigned i = 0; i < OpNum; ++i)
        Operands.push_back(RecordedNodes[MatcherTable[MatcherIndex++]].first);

      unsigned PredNo = MatcherTable[MatcherIndex++];
      if (!CheckNodePredicateWithOperands(N.getNode(), PredNo, Operands))
        break;
      continue;
    }
    case OPC_CheckComplexPat: {
      unsigned CPNum = MatcherTable[MatcherIndex++];
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid CheckComplexPat");

      // If target can modify DAG during matching, keep the matching state
      // consistent.
      std::unique_ptr<MatchStateUpdater> MSU;
      if (ComplexPatternFuncMutatesDAG())
        MSU.reset(new MatchStateUpdater(*CurDAG, &NodeToMatch, RecordedNodes,
                                        MatchScopes));

      if (!CheckComplexPattern(NodeToMatch, RecordedNodes[RecNo].second,
                               RecordedNodes[RecNo].first, CPNum,
                               RecordedNodes))
        break;
      continue;
    }
    case OPC_CheckOpcode:
      if (!::CheckOpcode(MatcherTable, MatcherIndex, N.getNode())) break;
      continue;

    case OPC_CheckType:
      if (!::CheckType(MatcherTable, MatcherIndex, N, TLI,
                       CurDAG->getDataLayout()))
        break;
      continue;

    case OPC_CheckTypeRes: {
      unsigned Res = MatcherTable[MatcherIndex++];
      if (!::CheckType(MatcherTable, MatcherIndex, N.getValue(Res), TLI,
                       CurDAG->getDataLayout()))
        break;
      continue;
    }

    case OPC_SwitchOpcode: {
      unsigned CurNodeOpcode = N.getOpcode();
      unsigned SwitchStart = MatcherIndex-1; (void)SwitchStart;
      unsigned CaseSize;
      while (true) {
        // Get the size of this case.
        CaseSize = MatcherTable[MatcherIndex++];
        if (CaseSize & 128)
          CaseSize = GetVBR(CaseSize, MatcherTable, MatcherIndex);
        if (CaseSize == 0) break;

        uint16_t Opc = MatcherTable[MatcherIndex++];
        Opc |= (unsigned short)MatcherTable[MatcherIndex++] << 8;

        // If the opcode matches, then we will execute this case.
        if (CurNodeOpcode == Opc)
          break;

        // Otherwise, skip over this case.
        MatcherIndex += CaseSize;
      }

      // If no cases matched, bail out.
      if (CaseSize == 0) break;

      // Otherwise, execute the case we found.
      LLVM_DEBUG(dbgs() << "  OpcodeSwitch from " << SwitchStart << " to "
                        << MatcherIndex << "\n");
      continue;
    }

    case OPC_SwitchType: {
      MVT CurNodeVT = N.getSimpleValueType();
      unsigned SwitchStart = MatcherIndex-1; (void)SwitchStart;
      unsigned CaseSize;
      while (true) {
        // Get the size of this case.
        CaseSize = MatcherTable[MatcherIndex++];
        if (CaseSize & 128)
          CaseSize = GetVBR(CaseSize, MatcherTable, MatcherIndex);
        if (CaseSize == 0) break;

        MVT CaseVT = (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
        if (CaseVT == MVT::iPTR)
          CaseVT = TLI->getPointerTy(CurDAG->getDataLayout());

        // If the VT matches, then we will execute this case.
        if (CurNodeVT == CaseVT)
          break;

        // Otherwise, skip over this case.
        MatcherIndex += CaseSize;
      }

      // If no cases matched, bail out.
      if (CaseSize == 0) break;

      // Otherwise, execute the case we found.
      LLVM_DEBUG(dbgs() << "  TypeSwitch[" << EVT(CurNodeVT).getEVTString()
                        << "] from " << SwitchStart << " to " << MatcherIndex
                        << '\n');
      continue;
    }
    case OPC_CheckChild0Type: case OPC_CheckChild1Type:
    case OPC_CheckChild2Type: case OPC_CheckChild3Type:
    case OPC_CheckChild4Type: case OPC_CheckChild5Type:
    case OPC_CheckChild6Type: case OPC_CheckChild7Type:
      if (!::CheckChildType(MatcherTable, MatcherIndex, N, TLI,
                            CurDAG->getDataLayout(),
                            Opcode - OPC_CheckChild0Type))
        break;
      continue;
    case OPC_CheckCondCode:
      if (!::CheckCondCode(MatcherTable, MatcherIndex, N)) break;
      continue;
    case OPC_CheckValueType:
      if (!::CheckValueType(MatcherTable, MatcherIndex, N, TLI,
                            CurDAG->getDataLayout()))
        break;
      continue;
    case OPC_CheckInteger:
      if (!::CheckInteger(MatcherTable, MatcherIndex, N)) break;
      continue;
    case OPC_CheckChild0Integer: case OPC_CheckChild1Integer:
    case OPC_CheckChild2Integer: case OPC_CheckChild3Integer:
    case OPC_CheckChild4Integer:
      if (!::CheckChildInteger(MatcherTable, MatcherIndex, N,
                               Opcode-OPC_CheckChild0Integer)) break;
      continue;
    case OPC_CheckAndImm:
      if (!::CheckAndImm(MatcherTable, MatcherIndex, N, *this)) break;
      continue;
    case OPC_CheckOrImm:
      if (!::CheckOrImm(MatcherTable, MatcherIndex, N, *this)) break;
      continue;

    case OPC_CheckFoldableChainNode: {
      assert(NodeStack.size() != 1 && "No parent node");
      // Verify that all intermediate nodes between the root and this one have
      // a single use.
      bool HasMultipleUses = false;
      for (unsigned i = 1, e = NodeStack.size()-1; i != e; ++i)
        if (!NodeStack[i].getNode()->hasOneUse()) {
          HasMultipleUses = true;
          break;
        }
      if (HasMultipleUses) break;

      // Check to see that the target thinks this is profitable to fold and that
      // we can fold it without inducing cycles in the graph.
      if (!IsProfitableToFold(N, NodeStack[NodeStack.size()-2].getNode(),
                              NodeToMatch) ||
          !IsLegalToFold(N, NodeStack[NodeStack.size()-2].getNode(),
                         NodeToMatch, OptLevel,
                         true/*We validate our own chains*/))
        break;

      continue;
    }
    case OPC_EmitInteger: {
      MVT::SimpleValueType VT =
        (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
      int64_t Val = MatcherTable[MatcherIndex++];
      if (Val & 128)
        Val = GetVBR(Val, MatcherTable, MatcherIndex);
      RecordedNodes.push_back(std::pair<SDValue, SDNode*>(
                              CurDAG->getTargetConstant(Val, SDLoc(NodeToMatch),
                                                        VT), nullptr));
      continue;
    }
    case OPC_EmitRegister: {
      MVT::SimpleValueType VT =
        (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
      unsigned RegNo = MatcherTable[MatcherIndex++];
      RecordedNodes.push_back(std::pair<SDValue, SDNode*>(
                              CurDAG->getRegister(RegNo, VT), nullptr));
      continue;
    }
    case OPC_EmitRegister2: {
      // For targets w/ more than 256 register names, the register enum
      // values are stored in two bytes in the matcher table (just like
      // opcodes).
      MVT::SimpleValueType VT =
        (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
      unsigned RegNo = MatcherTable[MatcherIndex++];
      RegNo |= MatcherTable[MatcherIndex++] << 8;
      RecordedNodes.push_back(std::pair<SDValue, SDNode*>(
                              CurDAG->getRegister(RegNo, VT), nullptr));
      continue;
    }

    case OPC_EmitConvertToTarget:  {
      // Convert from IMM/FPIMM to target version.
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid EmitConvertToTarget");
      SDValue Imm = RecordedNodes[RecNo].first;

      if (Imm->getOpcode() == ISD::Constant) {
        const ConstantInt *Val=cast<ConstantSDNode>(Imm)->getConstantIntValue();
        Imm = CurDAG->getTargetConstant(*Val, SDLoc(NodeToMatch),
                                        Imm.getValueType());
      } else if (Imm->getOpcode() == ISD::ConstantFP) {
        const ConstantFP *Val=cast<ConstantFPSDNode>(Imm)->getConstantFPValue();
        Imm = CurDAG->getTargetConstantFP(*Val, SDLoc(NodeToMatch),
                                          Imm.getValueType());
      }

      RecordedNodes.push_back(std::make_pair(Imm, RecordedNodes[RecNo].second));
      continue;
    }

    case OPC_EmitMergeInputChains1_0:    // OPC_EmitMergeInputChains, 1, 0
    case OPC_EmitMergeInputChains1_1:    // OPC_EmitMergeInputChains, 1, 1
    case OPC_EmitMergeInputChains1_2: {  // OPC_EmitMergeInputChains, 1, 2
      // These are space-optimized forms of OPC_EmitMergeInputChains.
      assert(!InputChain.getNode() &&
             "EmitMergeInputChains should be the first chain producing node");
      assert(ChainNodesMatched.empty() &&
             "Should only have one EmitMergeInputChains per match");

      // Read all of the chained nodes.
      unsigned RecNo = Opcode - OPC_EmitMergeInputChains1_0;
      assert(RecNo < RecordedNodes.size() && "Invalid EmitMergeInputChains");
      ChainNodesMatched.push_back(RecordedNodes[RecNo].first.getNode());

      // FIXME: What if other value results of the node have uses not matched
      // by this pattern?
      if (ChainNodesMatched.back() != NodeToMatch &&
          !RecordedNodes[RecNo].first.hasOneUse()) {
        ChainNodesMatched.clear();
        break;
      }

      // Merge the input chains if they are not intra-pattern references.
      InputChain = HandleMergeInputChains(ChainNodesMatched, CurDAG);

      if (!InputChain.getNode())
        break;  // Failed to merge.
      continue;
    }

    case OPC_EmitMergeInputChains: {
      assert(!InputChain.getNode() &&
             "EmitMergeInputChains should be the first chain producing node");
      // This node gets a list of nodes we matched in the input that have
      // chains.  We want to token factor all of the input chains to these nodes
      // together.  However, if any of the input chains is actually one of the
      // nodes matched in this pattern, then we have an intra-match reference.
      // Ignore these because the newly token factored chain should not refer to
      // the old nodes.
      unsigned NumChains = MatcherTable[MatcherIndex++];
      assert(NumChains != 0 && "Can't TF zero chains");

      assert(ChainNodesMatched.empty() &&
             "Should only have one EmitMergeInputChains per match");

      // Read all of the chained nodes.
      for (unsigned i = 0; i != NumChains; ++i) {
        unsigned RecNo = MatcherTable[MatcherIndex++];
        assert(RecNo < RecordedNodes.size() && "Invalid EmitMergeInputChains");
        ChainNodesMatched.push_back(RecordedNodes[RecNo].first.getNode());

        // FIXME: What if other value results of the node have uses not matched
        // by this pattern?
        if (ChainNodesMatched.back() != NodeToMatch &&
            !RecordedNodes[RecNo].first.hasOneUse()) {
          ChainNodesMatched.clear();
          break;
        }
      }

      // If the inner loop broke out, the match fails.
      if (ChainNodesMatched.empty())
        break;

      // Merge the input chains if they are not intra-pattern references.
      InputChain = HandleMergeInputChains(ChainNodesMatched, CurDAG);

      if (!InputChain.getNode())
        break;  // Failed to merge.

      continue;
    }

    case OPC_EmitCopyToReg: {
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid EmitCopyToReg");
      unsigned DestPhysReg = MatcherTable[MatcherIndex++];

      if (!InputChain.getNode())
        InputChain = CurDAG->getEntryNode();

      InputChain = CurDAG->getCopyToReg(InputChain, SDLoc(NodeToMatch),
                                        DestPhysReg, RecordedNodes[RecNo].first,
                                        InputGlue);

      InputGlue = InputChain.getValue(1);
      continue;
    }

    case OPC_EmitNodeXForm: {
      unsigned XFormNo = MatcherTable[MatcherIndex++];
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid EmitNodeXForm");
      SDValue Res = RunSDNodeXForm(RecordedNodes[RecNo].first, XFormNo);
      RecordedNodes.push_back(std::pair<SDValue,SDNode*>(Res, nullptr));
      continue;
    }
    case OPC_Coverage: {
      // This is emitted right before MorphNode/EmitNode.
      // So it should be safe to assume that this node has been selected
      unsigned index = MatcherTable[MatcherIndex++];
      index |= (MatcherTable[MatcherIndex++] << 8);
      dbgs() << "COVERED: " << getPatternForIndex(index) << "\n";
      dbgs() << "INCLUDED: " << getIncludePathForIndex(index) << "\n";
      continue;
    }

    case OPC_EmitNode:     case OPC_MorphNodeTo:
    case OPC_EmitNode0:    case OPC_EmitNode1:    case OPC_EmitNode2:
    case OPC_MorphNodeTo0: case OPC_MorphNodeTo1: case OPC_MorphNodeTo2: {
      uint16_t TargetOpc = MatcherTable[MatcherIndex++];
      TargetOpc |= (unsigned short)MatcherTable[MatcherIndex++] << 8;
      unsigned EmitNodeInfo = MatcherTable[MatcherIndex++];
      // Get the result VT list.
      unsigned NumVTs;
      // If this is one of the compressed forms, get the number of VTs based
      // on the Opcode. Otherwise read the next byte from the table.
      if (Opcode >= OPC_MorphNodeTo0 && Opcode <= OPC_MorphNodeTo2)
        NumVTs = Opcode - OPC_MorphNodeTo0;
      else if (Opcode >= OPC_EmitNode0 && Opcode <= OPC_EmitNode2)
        NumVTs = Opcode - OPC_EmitNode0;
      else
        NumVTs = MatcherTable[MatcherIndex++];
      SmallVector<EVT, 4> VTs;
      for (unsigned i = 0; i != NumVTs; ++i) {
        MVT::SimpleValueType VT =
          (MVT::SimpleValueType)MatcherTable[MatcherIndex++];
        if (VT == MVT::iPTR)
          VT = TLI->getPointerTy(CurDAG->getDataLayout()).SimpleTy;
        VTs.push_back(VT);
      }

      if (EmitNodeInfo & OPFL_Chain)
        VTs.push_back(MVT::Other);
      if (EmitNodeInfo & OPFL_GlueOutput)
        VTs.push_back(MVT::Glue);

      // This is hot code, so optimize the two most common cases of 1 and 2
      // results.
      SDVTList VTList;
      if (VTs.size() == 1)
        VTList = CurDAG->getVTList(VTs[0]);
      else if (VTs.size() == 2)
        VTList = CurDAG->getVTList(VTs[0], VTs[1]);
      else
        VTList = CurDAG->getVTList(VTs);

      // Get the operand list.
      unsigned NumOps = MatcherTable[MatcherIndex++];
      SmallVector<SDValue, 8> Ops;
      for (unsigned i = 0; i != NumOps; ++i) {
        unsigned RecNo = MatcherTable[MatcherIndex++];
        if (RecNo & 128)
          RecNo = GetVBR(RecNo, MatcherTable, MatcherIndex);

        assert(RecNo < RecordedNodes.size() && "Invalid EmitNode");
        Ops.push_back(RecordedNodes[RecNo].first);
      }

      // If there are variadic operands to add, handle them now.
      if (EmitNodeInfo & OPFL_VariadicInfo) {
        // Determine the start index to copy from.
        unsigned FirstOpToCopy = getNumFixedFromVariadicInfo(EmitNodeInfo);
        FirstOpToCopy += (EmitNodeInfo & OPFL_Chain) ? 1 : 0;
        assert(NodeToMatch->getNumOperands() >= FirstOpToCopy &&
               "Invalid variadic node");
        // Copy all of the variadic operands, not including a potential glue
        // input.
        for (unsigned i = FirstOpToCopy, e = NodeToMatch->getNumOperands();
             i != e; ++i) {
          SDValue V = NodeToMatch->getOperand(i);
          if (V.getValueType() == MVT::Glue) break;
          Ops.push_back(V);
        }
      }

      // If this has chain/glue inputs, add them.
      if (EmitNodeInfo & OPFL_Chain)
        Ops.push_back(InputChain);
      if ((EmitNodeInfo & OPFL_GlueInput) && InputGlue.getNode() != nullptr)
        Ops.push_back(InputGlue);

      // Create the node.
      MachineSDNode *Res = nullptr;
      bool IsMorphNodeTo = Opcode == OPC_MorphNodeTo ||
                     (Opcode >= OPC_MorphNodeTo0 && Opcode <= OPC_MorphNodeTo2);
      if (!IsMorphNodeTo) {
        // If this is a normal EmitNode command, just create the new node and
        // add the results to the RecordedNodes list.
        Res = CurDAG->getMachineNode(TargetOpc, SDLoc(NodeToMatch),
                                     VTList, Ops);

        // Add all the non-glue/non-chain results to the RecordedNodes list.
        for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
          if (VTs[i] == MVT::Other || VTs[i] == MVT::Glue) break;
          RecordedNodes.push_back(std::pair<SDValue,SDNode*>(SDValue(Res, i),
                                                             nullptr));
        }
      } else {
        assert(NodeToMatch->getOpcode() != ISD::DELETED_NODE &&
               "NodeToMatch was removed partway through selection");
        SelectionDAG::DAGNodeDeletedListener NDL(*CurDAG, [&](SDNode *N,
                                                              SDNode *E) {
          CurDAG->salvageDebugInfo(*N);
          auto &Chain = ChainNodesMatched;
          assert((!E || !is_contained(Chain, N)) &&
                 "Chain node replaced during MorphNode");
          Chain.erase(std::remove(Chain.begin(), Chain.end(), N), Chain.end());
        });
        Res = cast<MachineSDNode>(MorphNode(NodeToMatch, TargetOpc, VTList,
                                            Ops, EmitNodeInfo));
      }

      // If the node had chain/glue results, update our notion of the current
      // chain and glue.
      if (EmitNodeInfo & OPFL_GlueOutput) {
        InputGlue = SDValue(Res, VTs.size()-1);
        if (EmitNodeInfo & OPFL_Chain)
          InputChain = SDValue(Res, VTs.size()-2);
      } else if (EmitNodeInfo & OPFL_Chain)
        InputChain = SDValue(Res, VTs.size()-1);

      // If the OPFL_MemRefs glue is set on this node, slap all of the
      // accumulated memrefs onto it.
      //
      // FIXME: This is vastly incorrect for patterns with multiple outputs
      // instructions that access memory and for ComplexPatterns that match
      // loads.
      if (EmitNodeInfo & OPFL_MemRefs) {
        // Only attach load or store memory operands if the generated
        // instruction may load or store.
        const MCInstrDesc &MCID = TII->get(TargetOpc);
        bool mayLoad = MCID.mayLoad();
        bool mayStore = MCID.mayStore();

        // We expect to have relatively few of these so just filter them into a
        // temporary buffer so that we can easily add them to the instruction.
        SmallVector<MachineMemOperand *, 4> FilteredMemRefs;
        for (MachineMemOperand *MMO : MatchedMemRefs) {
          if (MMO->isLoad()) {
            if (mayLoad)
              FilteredMemRefs.push_back(MMO);
          } else if (MMO->isStore()) {
            if (mayStore)
              FilteredMemRefs.push_back(MMO);
          } else {
            FilteredMemRefs.push_back(MMO);
          }
        }

        CurDAG->setNodeMemRefs(Res, FilteredMemRefs);
      }

      LLVM_DEBUG(if (!MatchedMemRefs.empty() && Res->memoperands_empty()) dbgs()
                     << "  Dropping mem operands\n";
                 dbgs() << "  " << (IsMorphNodeTo ? "Morphed" : "Created")
                        << " node: ";
                 Res->dump(CurDAG););

      // If this was a MorphNodeTo then we're completely done!
      if (IsMorphNodeTo) {
        // Update chain uses.
        UpdateChains(Res, InputChain, ChainNodesMatched, true);
        return;
      }
      continue;
    }

    case OPC_CompleteMatch: {
      // The match has been completed, and any new nodes (if any) have been
      // created.  Patch up references to the matched dag to use the newly
      // created nodes.
      unsigned NumResults = MatcherTable[MatcherIndex++];

      for (unsigned i = 0; i != NumResults; ++i) {
        unsigned ResSlot = MatcherTable[MatcherIndex++];
        if (ResSlot & 128)
          ResSlot = GetVBR(ResSlot, MatcherTable, MatcherIndex);

        assert(ResSlot < RecordedNodes.size() && "Invalid CompleteMatch");
        SDValue Res = RecordedNodes[ResSlot].first;

        assert(i < NodeToMatch->getNumValues() &&
               NodeToMatch->getValueType(i) != MVT::Other &&
               NodeToMatch->getValueType(i) != MVT::Glue &&
               "Invalid number of results to complete!");
        assert((NodeToMatch->getValueType(i) == Res.getValueType() ||
                NodeToMatch->getValueType(i) == MVT::iPTR ||
                Res.getValueType() == MVT::iPTR ||
                NodeToMatch->getValueType(i).getSizeInBits() ==
                    Res.getValueSizeInBits()) &&
               "invalid replacement");
        ReplaceUses(SDValue(NodeToMatch, i), Res);
      }

      // Update chain uses.
      UpdateChains(NodeToMatch, InputChain, ChainNodesMatched, false);

      // If the root node defines glue, we need to update it to the glue result.
      // TODO: This never happens in our tests and I think it can be removed /
      // replaced with an assert, but if we do it this the way the change is
      // NFC.
      if (NodeToMatch->getValueType(NodeToMatch->getNumValues() - 1) ==
              MVT::Glue &&
          InputGlue.getNode())
        ReplaceUses(SDValue(NodeToMatch, NodeToMatch->getNumValues() - 1),
                    InputGlue);

      assert(NodeToMatch->use_empty() &&
             "Didn't replace all uses of the node?");
      CurDAG->RemoveDeadNode(NodeToMatch);

      return;
    }
    }

    // If the code reached this point, then the match failed.  See if there is
    // another child to try in the current 'Scope', otherwise pop it until we
    // find a case to check.
    LLVM_DEBUG(dbgs() << "  Match failed at index " << CurrentOpcodeIndex
                      << "\n");
    ++NumDAGIselRetries;
    while (true) {
      if (MatchScopes.empty()) {
        CannotYetSelect(NodeToMatch);
        return;
      }

      // Restore the interpreter state back to the point where the scope was
      // formed.
      MatchScope &LastScope = MatchScopes.back();
      RecordedNodes.resize(LastScope.NumRecordedNodes);
      NodeStack.clear();
      NodeStack.append(LastScope.NodeStack.begin(), LastScope.NodeStack.end());
      N = NodeStack.back();

      if (LastScope.NumMatchedMemRefs != MatchedMemRefs.size())
        MatchedMemRefs.resize(LastScope.NumMatchedMemRefs);
      MatcherIndex = LastScope.FailIndex;

      LLVM_DEBUG(dbgs() << "  Continuing at " << MatcherIndex << "\n");

      InputChain = LastScope.InputChain;
      InputGlue = LastScope.InputGlue;
      if (!LastScope.HasChainNodesMatched)
        ChainNodesMatched.clear();

      // Check to see what the offset is at the new MatcherIndex.  If it is zero
      // we have reached the end of this scope, otherwise we have another child
      // in the current scope to try.
      unsigned NumToSkip = MatcherTable[MatcherIndex++];
      if (NumToSkip & 128)
        NumToSkip = GetVBR(NumToSkip, MatcherTable, MatcherIndex);

      // If we have another child in this scope to match, update FailIndex and
      // try it.
      if (NumToSkip != 0) {
        LastScope.FailIndex = MatcherIndex+NumToSkip;
        break;
      }

      // End of this scope, pop it and try the next child in the containing
      // scope.
      MatchScopes.pop_back();
    }
  }
}

bool SelectionDAGISel::isOrEquivalentToAdd(const SDNode *N) const {
  assert(N->getOpcode() == ISD::OR && "Unexpected opcode");
  auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C)
    return false;

  // Detect when "or" is used to add an offset to a stack object.
  if (auto *FN = dyn_cast<FrameIndexSDNode>(N->getOperand(0))) {
    MachineFrameInfo &MFI = MF->getFrameInfo();
    unsigned A = MFI.getObjectAlignment(FN->getIndex());
    assert(isPowerOf2_32(A) && "Unexpected alignment");
    int32_t Off = C->getSExtValue();
    // If the alleged offset fits in the zero bits guaranteed by
    // the alignment, then this or is really an add.
    return (Off >= 0) && (((A - 1) & Off) == unsigned(Off));
  }
  return false;
}

void SelectionDAGISel::CannotYetSelect(SDNode *N) {
  std::string msg;
  raw_string_ostream Msg(msg);
  Msg << "Cannot select: ";

  if (N->getOpcode() != ISD::INTRINSIC_W_CHAIN &&
      N->getOpcode() != ISD::INTRINSIC_WO_CHAIN &&
      N->getOpcode() != ISD::INTRINSIC_VOID) {
    N->printrFull(Msg, CurDAG);
    Msg << "\nIn function: " << MF->getName();
  } else {
    bool HasInputChain = N->getOperand(0).getValueType() == MVT::Other;
    unsigned iid =
      cast<ConstantSDNode>(N->getOperand(HasInputChain))->getZExtValue();
    if (iid < Intrinsic::num_intrinsics)
      Msg << "intrinsic %" << Intrinsic::getName((Intrinsic::ID)iid, None);
    else if (const TargetIntrinsicInfo *TII = TM.getIntrinsicInfo())
      Msg << "target intrinsic %" << TII->getName(iid);
    else
      Msg << "unknown intrinsic #" << iid;
  }
  report_fatal_error(Msg.str());
}

char SelectionDAGISel::ID = 0;
