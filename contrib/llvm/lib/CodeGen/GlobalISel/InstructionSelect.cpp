//===- llvm/CodeGen/GlobalISel/InstructionSelect.cpp - InstructionSelect ---==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the InstructionSelect class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TargetRegistry.h"

#define DEBUG_TYPE "instruction-select"

using namespace llvm;

#ifdef LLVM_GISEL_COV_PREFIX
static cl::opt<std::string>
    CoveragePrefix("gisel-coverage-prefix", cl::init(LLVM_GISEL_COV_PREFIX),
                   cl::desc("Record GlobalISel rule coverage files of this "
                            "prefix if instrumentation was generated"));
#else
static const std::string CoveragePrefix = "";
#endif

char InstructionSelect::ID = 0;
INITIALIZE_PASS_BEGIN(InstructionSelect, DEBUG_TYPE,
                      "Select target instructions out of generic instructions",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(InstructionSelect, DEBUG_TYPE,
                    "Select target instructions out of generic instructions",
                    false, false)

InstructionSelect::InstructionSelect() : MachineFunctionPass(ID) {
  initializeInstructionSelectPass(*PassRegistry::getPassRegistry());
}

void InstructionSelect::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool InstructionSelect::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Selecting function: " << MF.getName() << '\n');

  const TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  const InstructionSelector *ISel = MF.getSubtarget().getInstructionSelector();
  CodeGenCoverage CoverageInfo;
  assert(ISel && "Cannot work without InstructionSelector");

  // An optimization remark emitter. Used to report failures.
  MachineOptimizationRemarkEmitter MORE(MF, /*MBFI=*/nullptr);

  // FIXME: There are many other MF/MFI fields we need to initialize.

  MachineRegisterInfo &MRI = MF.getRegInfo();
#ifndef NDEBUG
  // Check that our input is fully legal: we require the function to have the
  // Legalized property, so it should be.
  // FIXME: This should be in the MachineVerifier, as the RegBankSelected
  // property check already is.
  if (!DisableGISelLegalityCheck)
    if (const MachineInstr *MI = machineFunctionIsIllegal(MF)) {
      reportGISelFailure(MF, TPC, MORE, "gisel-select",
                         "instruction is not legal", *MI);
      return false;
    }
#endif
  // FIXME: We could introduce new blocks and will need to fix the outer loop.
  // Until then, keep track of the number of blocks to assert that we don't.
  const size_t NumBlocks = MF.size();

  for (MachineBasicBlock *MBB : post_order(&MF)) {
    if (MBB->empty())
      continue;

    // Select instructions in reverse block order. We permit erasing so have
    // to resort to manually iterating and recognizing the begin (rend) case.
    bool ReachedBegin = false;
    for (auto MII = std::prev(MBB->end()), Begin = MBB->begin();
         !ReachedBegin;) {
#ifndef NDEBUG
      // Keep track of the insertion range for debug printing.
      const auto AfterIt = std::next(MII);
#endif
      // Select this instruction.
      MachineInstr &MI = *MII;

      // And have our iterator point to the next instruction, if there is one.
      if (MII == Begin)
        ReachedBegin = true;
      else
        --MII;

      LLVM_DEBUG(dbgs() << "Selecting: \n  " << MI);

      // We could have folded this instruction away already, making it dead.
      // If so, erase it.
      if (isTriviallyDead(MI, MRI)) {
        LLVM_DEBUG(dbgs() << "Is dead; erasing.\n");
        MI.eraseFromParentAndMarkDBGValuesForRemoval();
        continue;
      }

      if (!ISel->select(MI, CoverageInfo)) {
        // FIXME: It would be nice to dump all inserted instructions.  It's
        // not obvious how, esp. considering select() can insert after MI.
        reportGISelFailure(MF, TPC, MORE, "gisel-select", "cannot select", MI);
        return false;
      }

      // Dump the range of instructions that MI expanded into.
      LLVM_DEBUG({
        auto InsertedBegin = ReachedBegin ? MBB->begin() : std::next(MII);
        dbgs() << "Into:\n";
        for (auto &InsertedMI : make_range(InsertedBegin, AfterIt))
          dbgs() << "  " << InsertedMI;
        dbgs() << '\n';
      });
    }
  }

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    // Try to find redundant copies b/w vregs of the same register class.
    bool ReachedBegin = false;
    for (auto MII = std::prev(MBB.end()), Begin = MBB.begin(); !ReachedBegin;) {
      // Select this instruction.
      MachineInstr &MI = *MII;

      // And have our iterator point to the next instruction, if there is one.
      if (MII == Begin)
        ReachedBegin = true;
      else
        --MII;
      if (MI.getOpcode() != TargetOpcode::COPY)
        continue;
      unsigned SrcReg = MI.getOperand(1).getReg();
      unsigned DstReg = MI.getOperand(0).getReg();
      if (TargetRegisterInfo::isVirtualRegister(SrcReg) &&
          TargetRegisterInfo::isVirtualRegister(DstReg)) {
        auto SrcRC = MRI.getRegClass(SrcReg);
        auto DstRC = MRI.getRegClass(DstReg);
        if (SrcRC == DstRC) {
          MRI.replaceRegWith(DstReg, SrcReg);
          MI.eraseFromParentAndMarkDBGValuesForRemoval();
        }
      }
    }
  }

  // Now that selection is complete, there are no more generic vregs.  Verify
  // that the size of the now-constrained vreg is unchanged and that it has a
  // register class.
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    unsigned VReg = TargetRegisterInfo::index2VirtReg(I);

    MachineInstr *MI = nullptr;
    if (!MRI.def_empty(VReg))
      MI = &*MRI.def_instr_begin(VReg);
    else if (!MRI.use_empty(VReg))
      MI = &*MRI.use_instr_begin(VReg);
    if (!MI)
      continue;

    const TargetRegisterClass *RC = MRI.getRegClassOrNull(VReg);
    if (!RC) {
      reportGISelFailure(MF, TPC, MORE, "gisel-select",
                         "VReg has no regclass after selection", *MI);
      return false;
    }

    const LLT Ty = MRI.getType(VReg);
    if (Ty.isValid() && Ty.getSizeInBits() > TRI.getRegSizeInBits(*RC)) {
      reportGISelFailure(
          MF, TPC, MORE, "gisel-select",
          "VReg's low-level type and register class have different sizes", *MI);
      return false;
    }
  }

  if (MF.size() != NumBlocks) {
    MachineOptimizationRemarkMissed R("gisel-select", "GISelFailure",
                                      MF.getFunction().getSubprogram(),
                                      /*MBB=*/nullptr);
    R << "inserting blocks is not supported yet";
    reportGISelFailure(MF, TPC, MORE, R);
    return false;
  }

  auto &TLI = *MF.getSubtarget().getTargetLowering();
  TLI.finalizeLowering(MF);

  LLVM_DEBUG({
    dbgs() << "Rules covered by selecting function: " << MF.getName() << ":";
    for (auto RuleID : CoverageInfo.covered())
      dbgs() << " id" << RuleID;
    dbgs() << "\n\n";
  });
  CoverageInfo.emit(CoveragePrefix,
                    MF.getSubtarget()
                        .getTargetLowering()
                        ->getTargetMachine()
                        .getTarget()
                        .getBackendName());

  // If we successfully selected the function nothing is going to use the vreg
  // types after us (otherwise MIRPrinter would need them). Make sure the types
  // disappear.
  MRI.clearVirtRegTypes();

  // FIXME: Should we accurately track changes?
  return true;
}
