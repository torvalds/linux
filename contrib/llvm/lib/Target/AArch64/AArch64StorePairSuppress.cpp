//===--- AArch64StorePairSuppress.cpp --- Suppress store pair formation ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass identifies floating point stores that should not be combined into
// store pairs. Later we may do the same for floating point loads.
// ===---------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-stp-suppress"

#define STPSUPPRESS_PASS_NAME "AArch64 Store Pair Suppression"

namespace {
class AArch64StorePairSuppress : public MachineFunctionPass {
  const AArch64InstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  TargetSchedModel SchedModel;
  MachineTraceMetrics *Traces;
  MachineTraceMetrics::Ensemble *MinInstr;

public:
  static char ID;
  AArch64StorePairSuppress() : MachineFunctionPass(ID) {
    initializeAArch64StorePairSuppressPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return STPSUPPRESS_PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &F) override;

private:
  bool shouldAddSTPToBlock(const MachineBasicBlock *BB);

  bool isNarrowFPStore(const MachineInstr &MI);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineTraceMetrics>();
    AU.addPreserved<MachineTraceMetrics>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char AArch64StorePairSuppress::ID = 0;
} // anonymous

INITIALIZE_PASS(AArch64StorePairSuppress, "aarch64-stp-suppress",
                STPSUPPRESS_PASS_NAME, false, false)

FunctionPass *llvm::createAArch64StorePairSuppressPass() {
  return new AArch64StorePairSuppress();
}

/// Return true if an STP can be added to this block without increasing the
/// critical resource height. STP is good to form in Ld/St limited blocks and
/// bad to form in float-point limited blocks. This is true independent of the
/// critical path. If the critical path is longer than the resource height, the
/// extra vector ops can limit physreg renaming. Otherwise, it could simply
/// oversaturate the vector units.
bool AArch64StorePairSuppress::shouldAddSTPToBlock(const MachineBasicBlock *BB) {
  if (!MinInstr)
    MinInstr = Traces->getEnsemble(MachineTraceMetrics::TS_MinInstrCount);

  MachineTraceMetrics::Trace BBTrace = MinInstr->getTrace(BB);
  unsigned ResLength = BBTrace.getResourceLength();

  // Get the machine model's scheduling class for STPQi.
  // Bypass TargetSchedule's SchedClass resolution since we only have an opcode.
  unsigned SCIdx = TII->get(AArch64::STPDi).getSchedClass();
  const MCSchedClassDesc *SCDesc =
      SchedModel.getMCSchedModel()->getSchedClassDesc(SCIdx);

  // If a subtarget does not define resources for STPQi, bail here.
  if (SCDesc->isValid() && !SCDesc->isVariant()) {
    unsigned ResLenWithSTP = BBTrace.getResourceLength(None, SCDesc);
    if (ResLenWithSTP > ResLength) {
      LLVM_DEBUG(dbgs() << "  Suppress STP in BB: " << BB->getNumber()
                        << " resources " << ResLength << " -> " << ResLenWithSTP
                        << "\n");
      return false;
    }
  }
  return true;
}

/// Return true if this is a floating-point store smaller than the V reg. On
/// cyclone, these require a vector shuffle before storing a pair.
/// Ideally we would call getMatchingPairOpcode() and have the machine model
/// tell us if it's profitable with no cpu knowledge here.
///
/// FIXME: We plan to develop a decent Target abstraction for simple loads and
/// stores. Until then use a nasty switch similar to AArch64LoadStoreOptimizer.
bool AArch64StorePairSuppress::isNarrowFPStore(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STURSi:
  case AArch64::STURDi:
    return true;
  }
}

bool AArch64StorePairSuppress::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const TargetSubtargetInfo &ST = MF.getSubtarget();
  TII = static_cast<const AArch64InstrInfo *>(ST.getInstrInfo());
  TRI = ST.getRegisterInfo();
  MRI = &MF.getRegInfo();
  SchedModel.init(&ST);
  Traces = &getAnalysis<MachineTraceMetrics>();
  MinInstr = nullptr;

  LLVM_DEBUG(dbgs() << "*** " << getPassName() << ": " << MF.getName() << '\n');

  if (!SchedModel.hasInstrSchedModel()) {
    LLVM_DEBUG(dbgs() << "  Skipping pass: no machine model present.\n");
    return false;
  }

  // Check for a sequence of stores to the same base address. We don't need to
  // precisely determine whether a store pair can be formed. But we do want to
  // filter out most situations where we can't form store pairs to avoid
  // computing trace metrics in those cases.
  for (auto &MBB : MF) {
    bool SuppressSTP = false;
    unsigned PrevBaseReg = 0;
    for (auto &MI : MBB) {
      if (!isNarrowFPStore(MI))
        continue;
      MachineOperand *BaseOp;
      int64_t Offset;
      if (TII->getMemOperandWithOffset(MI, BaseOp, Offset, TRI) &&
          BaseOp->isReg()) {
        unsigned BaseReg = BaseOp->getReg();
        if (PrevBaseReg == BaseReg) {
          // If this block can take STPs, skip ahead to the next block.
          if (!SuppressSTP && shouldAddSTPToBlock(MI.getParent()))
            break;
          // Otherwise, continue unpairing the stores in this block.
          LLVM_DEBUG(dbgs() << "Unpairing store " << MI << "\n");
          SuppressSTP = true;
          TII->suppressLdStPair(MI);
        }
        PrevBaseReg = BaseReg;
      } else
        PrevBaseReg = 0;
    }
  }
  // This pass just sets some internal MachineMemOperand flags. It can't really
  // invalidate anything.
  return false;
}
