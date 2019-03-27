//===---------- AArch64CollectLOH.cpp - AArch64 collect LOH pass --*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that collect the Linker Optimization Hint (LOH).
// This pass should be run at the very end of the compilation flow, just before
// assembly printer.
// To be useful for the linker, the LOH must be printed into the assembly file.
//
// A LOH describes a sequence of instructions that may be optimized by the
// linker.
// This same sequence cannot be optimized by the compiler because some of
// the information will be known at link time.
// For instance, consider the following sequence:
//     L1: adrp xA, sym@PAGE
//     L2: add xB, xA, sym@PAGEOFF
//     L3: ldr xC, [xB, #imm]
// This sequence can be turned into:
// A literal load if sym@PAGE + sym@PAGEOFF + #imm - address(L3) is < 1MB:
//     L3: ldr xC, sym+#imm
// It may also be turned into either the following more efficient
// code sequences:
// - If sym@PAGEOFF + #imm fits the encoding space of L3.
//     L1: adrp xA, sym@PAGE
//     L3: ldr xC, [xB, sym@PAGEOFF + #imm]
// - If sym@PAGE + sym@PAGEOFF - address(L1) < 1MB:
//     L1: adr xA, sym
//     L3: ldr xC, [xB, #imm]
//
// To be valid a LOH must meet all the requirements needed by all the related
// possible linker transformations.
// For instance, using the running example, the constraints to emit
// ".loh AdrpAddLdr" are:
// - L1, L2, and L3 instructions are of the expected type, i.e.,
//   respectively ADRP, ADD (immediate), and LD.
// - The result of L1 is used only by L2.
// - The register argument (xA) used in the ADD instruction is defined
//   only by L1.
// - The result of L2 is used only by L3.
// - The base address (xB) in L3 is defined only L2.
// - The ADRP in L1 and the ADD in L2 must reference the same symbol using
//   @PAGE/@PAGEOFF with no additional constants
//
// Currently supported LOHs are:
// * So called non-ADRP-related:
//   - .loh AdrpAddLdr L1, L2, L3:
//     L1: adrp xA, sym@PAGE
//     L2: add xB, xA, sym@PAGEOFF
//     L3: ldr xC, [xB, #imm]
//   - .loh AdrpLdrGotLdr L1, L2, L3:
//     L1: adrp xA, sym@GOTPAGE
//     L2: ldr xB, [xA, sym@GOTPAGEOFF]
//     L3: ldr xC, [xB, #imm]
//   - .loh AdrpLdr L1, L3:
//     L1: adrp xA, sym@PAGE
//     L3: ldr xC, [xA, sym@PAGEOFF]
//   - .loh AdrpAddStr L1, L2, L3:
//     L1: adrp xA, sym@PAGE
//     L2: add xB, xA, sym@PAGEOFF
//     L3: str xC, [xB, #imm]
//   - .loh AdrpLdrGotStr L1, L2, L3:
//     L1: adrp xA, sym@GOTPAGE
//     L2: ldr xB, [xA, sym@GOTPAGEOFF]
//     L3: str xC, [xB, #imm]
//   - .loh AdrpAdd L1, L2:
//     L1: adrp xA, sym@PAGE
//     L2: add xB, xA, sym@PAGEOFF
//   For all these LOHs, L1, L2, L3 form a simple chain:
//   L1 result is used only by L2 and L2 result by L3.
//   L3 LOH-related argument is defined only by L2 and L2 LOH-related argument
//   by L1.
// All these LOHs aim at using more efficient load/store patterns by folding
// some instructions used to compute the address directly into the load/store.
//
// * So called ADRP-related:
//  - .loh AdrpAdrp L2, L1:
//    L2: ADRP xA, sym1@PAGE
//    L1: ADRP xA, sym2@PAGE
//    L2 dominates L1 and xA is not redifined between L2 and L1
// This LOH aims at getting rid of redundant ADRP instructions.
//
// The overall design for emitting the LOHs is:
// 1. AArch64CollectLOH (this pass) records the LOHs in the AArch64FunctionInfo.
// 2. AArch64AsmPrinter reads the LOHs from AArch64FunctionInfo and it:
//     1. Associates them a label.
//     2. Emits them in a MCStreamer (EmitLOHDirective).
//         - The MCMachOStreamer records them into the MCAssembler.
//         - The MCAsmStreamer prints them.
//         - Other MCStreamers ignore them.
//     3. Closes the MCStreamer:
//         - The MachObjectWriter gets them from the MCAssembler and writes
//           them in the object file.
//         - Other ObjectWriters ignore them.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-collect-loh"

STATISTIC(NumADRPSimpleCandidate,
          "Number of simplifiable ADRP dominate by another");
STATISTIC(NumADDToSTR, "Number of simplifiable STR reachable by ADD");
STATISTIC(NumLDRToSTR, "Number of simplifiable STR reachable by LDR");
STATISTIC(NumADDToLDR, "Number of simplifiable LDR reachable by ADD");
STATISTIC(NumLDRToLDR, "Number of simplifiable LDR reachable by LDR");
STATISTIC(NumADRPToLDR, "Number of simplifiable LDR reachable by ADRP");
STATISTIC(NumADRSimpleCandidate, "Number of simplifiable ADRP + ADD");

#define AARCH64_COLLECT_LOH_NAME "AArch64 Collect Linker Optimization Hint (LOH)"

namespace {

struct AArch64CollectLOH : public MachineFunctionPass {
  static char ID;
  AArch64CollectLOH() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return AARCH64_COLLECT_LOH_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.setPreservesAll();
  }
};

char AArch64CollectLOH::ID = 0;

} // end anonymous namespace.

INITIALIZE_PASS(AArch64CollectLOH, "aarch64-collect-loh",
                AARCH64_COLLECT_LOH_NAME, false, false)

static bool canAddBePartOfLOH(const MachineInstr &MI) {
  // Check immediate to see if the immediate is an address.
  switch (MI.getOperand(2).getType()) {
  default:
    return false;
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_BlockAddress:
    return true;
  }
}

/// Answer the following question: Can Def be one of the definition
/// involved in a part of a LOH?
static bool canDefBePartOfLOH(const MachineInstr &MI) {
  // Accept ADRP, ADDLow and LOADGot.
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::ADRP:
    return true;
  case AArch64::ADDXri:
    return canAddBePartOfLOH(MI);
  case AArch64::LDRXui:
    // Check immediate to see if the immediate is an address.
    switch (MI.getOperand(2).getType()) {
    default:
      return false;
    case MachineOperand::MO_GlobalAddress:
      return MI.getOperand(2).getTargetFlags() & AArch64II::MO_GOT;
    }
  }
}

/// Check whether the given instruction can the end of a LOH chain involving a
/// store.
static bool isCandidateStore(const MachineInstr &MI, const MachineOperand &MO) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::STRBBui:
  case AArch64::STRHHui:
  case AArch64::STRBui:
  case AArch64::STRHui:
  case AArch64::STRWui:
  case AArch64::STRXui:
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STRQui:
    // We can only optimize the index operand.
    // In case we have str xA, [xA, #imm], this is two different uses
    // of xA and we cannot fold, otherwise the xA stored may be wrong,
    // even if #imm == 0.
    return MI.getOperandNo(&MO) == 1 &&
           MI.getOperand(0).getReg() != MI.getOperand(1).getReg();
  }
}

/// Check whether the given instruction can be the end of a LOH chain
/// involving a load.
static bool isCandidateLoad(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::LDRSBWui:
  case AArch64::LDRSBXui:
  case AArch64::LDRSHWui:
  case AArch64::LDRSHXui:
  case AArch64::LDRSWui:
  case AArch64::LDRBui:
  case AArch64::LDRHui:
  case AArch64::LDRWui:
  case AArch64::LDRXui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:
    return !(MI.getOperand(2).getTargetFlags() & AArch64II::MO_GOT);
  }
}

/// Check whether the given instruction can load a litteral.
static bool supportLoadFromLiteral(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::LDRSWui:
  case AArch64::LDRWui:
  case AArch64::LDRXui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:
    return true;
  }
}

/// Number of GPR registers traked by mapRegToGPRIndex()
static const unsigned N_GPR_REGS = 31;
/// Map register number to index from 0-30.
static int mapRegToGPRIndex(MCPhysReg Reg) {
  static_assert(AArch64::X28 - AArch64::X0 + 3 == N_GPR_REGS, "Number of GPRs");
  static_assert(AArch64::W30 - AArch64::W0 + 1 == N_GPR_REGS, "Number of GPRs");
  if (AArch64::X0 <= Reg && Reg <= AArch64::X28)
    return Reg - AArch64::X0;
  if (AArch64::W0 <= Reg && Reg <= AArch64::W30)
    return Reg - AArch64::W0;
  // TableGen gives "FP" and "LR" an index not adjacent to X28 so we have to
  // handle them as special cases.
  if (Reg == AArch64::FP)
    return 29;
  if (Reg == AArch64::LR)
    return 30;
  return -1;
}

/// State tracked per register.
/// The main algorithm walks backwards over a basic block maintaining this
/// datastructure for each tracked general purpose register.
struct LOHInfo {
  MCLOHType Type : 8;           ///< "Best" type of LOH possible.
  bool IsCandidate : 1;         ///< Possible LOH candidate.
  bool OneUser : 1;             ///< Found exactly one user (yet).
  bool MultiUsers : 1;          ///< Found multiple users.
  const MachineInstr *MI0;      ///< First instruction involved in the LOH.
  const MachineInstr *MI1;      ///< Second instruction involved in the LOH
                                ///  (if any).
  const MachineInstr *LastADRP; ///< Last ADRP in same register.
};

/// Update state \p Info given \p MI uses the tracked register.
static void handleUse(const MachineInstr &MI, const MachineOperand &MO,
                      LOHInfo &Info) {
  // We have multiple uses if we already found one before.
  if (Info.MultiUsers || Info.OneUser) {
    Info.IsCandidate = false;
    Info.MultiUsers = true;
    return;
  }
  Info.OneUser = true;

  // Start new LOHInfo if applicable.
  if (isCandidateLoad(MI)) {
    Info.Type = MCLOH_AdrpLdr;
    Info.IsCandidate = true;
    Info.MI0 = &MI;
    // Note that even this is AdrpLdr now, we can switch to a Ldr variant
    // later.
  } else if (isCandidateStore(MI, MO)) {
    Info.Type = MCLOH_AdrpAddStr;
    Info.IsCandidate = true;
    Info.MI0 = &MI;
    Info.MI1 = nullptr;
  } else if (MI.getOpcode() == AArch64::ADDXri) {
    Info.Type = MCLOH_AdrpAdd;
    Info.IsCandidate = true;
    Info.MI0 = &MI;
  } else if (MI.getOpcode() == AArch64::LDRXui &&
             MI.getOperand(2).getTargetFlags() & AArch64II::MO_GOT) {
    Info.Type = MCLOH_AdrpLdrGot;
    Info.IsCandidate = true;
    Info.MI0 = &MI;
  }
}

/// Update state \p Info given the tracked register is clobbered.
static void handleClobber(LOHInfo &Info) {
  Info.IsCandidate = false;
  Info.OneUser = false;
  Info.MultiUsers = false;
  Info.LastADRP = nullptr;
}

/// Update state \p Info given that \p MI is possibly the middle instruction
/// of an LOH involving 3 instructions.
static bool handleMiddleInst(const MachineInstr &MI, LOHInfo &DefInfo,
                             LOHInfo &OpInfo) {
  if (!DefInfo.IsCandidate || (&DefInfo != &OpInfo && OpInfo.OneUser))
    return false;
  // Copy LOHInfo for dest register to LOHInfo for source register.
  if (&DefInfo != &OpInfo) {
    OpInfo = DefInfo;
    // Invalidate \p DefInfo because we track it in \p OpInfo now.
    handleClobber(DefInfo);
  } else
    DefInfo.LastADRP = nullptr;

  // Advance state machine.
  assert(OpInfo.IsCandidate && "Expect valid state");
  if (MI.getOpcode() == AArch64::ADDXri && canAddBePartOfLOH(MI)) {
    if (OpInfo.Type == MCLOH_AdrpLdr) {
      OpInfo.Type = MCLOH_AdrpAddLdr;
      OpInfo.IsCandidate = true;
      OpInfo.MI1 = &MI;
      return true;
    } else if (OpInfo.Type == MCLOH_AdrpAddStr && OpInfo.MI1 == nullptr) {
      OpInfo.Type = MCLOH_AdrpAddStr;
      OpInfo.IsCandidate = true;
      OpInfo.MI1 = &MI;
      return true;
    }
  } else {
    assert(MI.getOpcode() == AArch64::LDRXui && "Expect LDRXui");
    assert((MI.getOperand(2).getTargetFlags() & AArch64II::MO_GOT) &&
           "Expected GOT relocation");
    if (OpInfo.Type == MCLOH_AdrpAddStr && OpInfo.MI1 == nullptr) {
      OpInfo.Type = MCLOH_AdrpLdrGotStr;
      OpInfo.IsCandidate = true;
      OpInfo.MI1 = &MI;
      return true;
    } else if (OpInfo.Type == MCLOH_AdrpLdr) {
      OpInfo.Type = MCLOH_AdrpLdrGotLdr;
      OpInfo.IsCandidate = true;
      OpInfo.MI1 = &MI;
      return true;
    }
  }
  return false;
}

/// Update state when seeing and ADRP instruction.
static void handleADRP(const MachineInstr &MI, AArch64FunctionInfo &AFI,
                       LOHInfo &Info) {
  if (Info.LastADRP != nullptr) {
    LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpAdrp:\n"
                      << '\t' << MI << '\t' << *Info.LastADRP);
    AFI.addLOHDirective(MCLOH_AdrpAdrp, {&MI, Info.LastADRP});
    ++NumADRPSimpleCandidate;
  }

  // Produce LOH directive if possible.
  if (Info.IsCandidate) {
    switch (Info.Type) {
    case MCLOH_AdrpAdd:
      LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpAdd:\n"
                        << '\t' << MI << '\t' << *Info.MI0);
      AFI.addLOHDirective(MCLOH_AdrpAdd, {&MI, Info.MI0});
      ++NumADRSimpleCandidate;
      break;
    case MCLOH_AdrpLdr:
      if (supportLoadFromLiteral(*Info.MI0)) {
        LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpLdr:\n"
                          << '\t' << MI << '\t' << *Info.MI0);
        AFI.addLOHDirective(MCLOH_AdrpLdr, {&MI, Info.MI0});
        ++NumADRPToLDR;
      }
      break;
    case MCLOH_AdrpAddLdr:
      LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpAddLdr:\n"
                        << '\t' << MI << '\t' << *Info.MI1 << '\t'
                        << *Info.MI0);
      AFI.addLOHDirective(MCLOH_AdrpAddLdr, {&MI, Info.MI1, Info.MI0});
      ++NumADDToLDR;
      break;
    case MCLOH_AdrpAddStr:
      if (Info.MI1 != nullptr) {
        LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpAddStr:\n"
                          << '\t' << MI << '\t' << *Info.MI1 << '\t'
                          << *Info.MI0);
        AFI.addLOHDirective(MCLOH_AdrpAddStr, {&MI, Info.MI1, Info.MI0});
        ++NumADDToSTR;
      }
      break;
    case MCLOH_AdrpLdrGotLdr:
      LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpLdrGotLdr:\n"
                        << '\t' << MI << '\t' << *Info.MI1 << '\t'
                        << *Info.MI0);
      AFI.addLOHDirective(MCLOH_AdrpLdrGotLdr, {&MI, Info.MI1, Info.MI0});
      ++NumLDRToLDR;
      break;
    case MCLOH_AdrpLdrGotStr:
      LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpLdrGotStr:\n"
                        << '\t' << MI << '\t' << *Info.MI1 << '\t'
                        << *Info.MI0);
      AFI.addLOHDirective(MCLOH_AdrpLdrGotStr, {&MI, Info.MI1, Info.MI0});
      ++NumLDRToSTR;
      break;
    case MCLOH_AdrpLdrGot:
      LLVM_DEBUG(dbgs() << "Adding MCLOH_AdrpLdrGot:\n"
                        << '\t' << MI << '\t' << *Info.MI0);
      AFI.addLOHDirective(MCLOH_AdrpLdrGot, {&MI, Info.MI0});
      break;
    case MCLOH_AdrpAdrp:
      llvm_unreachable("MCLOH_AdrpAdrp not used in state machine");
    }
  }

  handleClobber(Info);
  Info.LastADRP = &MI;
}

static void handleRegMaskClobber(const uint32_t *RegMask, MCPhysReg Reg,
                                 LOHInfo *LOHInfos) {
  if (!MachineOperand::clobbersPhysReg(RegMask, Reg))
    return;
  int Idx = mapRegToGPRIndex(Reg);
  if (Idx >= 0)
    handleClobber(LOHInfos[Idx]);
}

static void handleNormalInst(const MachineInstr &MI, LOHInfo *LOHInfos) {
  // Handle defs and regmasks.
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isRegMask()) {
      const uint32_t *RegMask = MO.getRegMask();
      for (MCPhysReg Reg : AArch64::GPR32RegClass)
        handleRegMaskClobber(RegMask, Reg, LOHInfos);
      for (MCPhysReg Reg : AArch64::GPR64RegClass)
        handleRegMaskClobber(RegMask, Reg, LOHInfos);
      continue;
    }
    if (!MO.isReg() || !MO.isDef())
      continue;
    int Idx = mapRegToGPRIndex(MO.getReg());
    if (Idx < 0)
      continue;
    handleClobber(LOHInfos[Idx]);
  }
  // Handle uses.
  for (const MachineOperand &MO : MI.uses()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    int Idx = mapRegToGPRIndex(MO.getReg());
    if (Idx < 0)
      continue;
    handleUse(MI, MO, LOHInfos[Idx]);
  }
}

bool AArch64CollectLOH::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "********** AArch64 Collect LOH **********\n"
                    << "Looking in function " << MF.getName() << '\n');

  LOHInfo LOHInfos[N_GPR_REGS];
  AArch64FunctionInfo &AFI = *MF.getInfo<AArch64FunctionInfo>();
  for (const MachineBasicBlock &MBB : MF) {
    // Reset register tracking state.
    memset(LOHInfos, 0, sizeof(LOHInfos));
    // Live-out registers are used.
    for (const MachineBasicBlock *Succ : MBB.successors()) {
      for (const auto &LI : Succ->liveins()) {
        int RegIdx = mapRegToGPRIndex(LI.PhysReg);
        if (RegIdx >= 0)
          LOHInfos[RegIdx].OneUser = true;
      }
    }

    // Walk the basic block backwards and update the per register state machine
    // in the process.
    for (const MachineInstr &MI : make_range(MBB.rbegin(), MBB.rend())) {
      unsigned Opcode = MI.getOpcode();
      switch (Opcode) {
      case AArch64::ADDXri:
      case AArch64::LDRXui:
        if (canDefBePartOfLOH(MI)) {
          const MachineOperand &Def = MI.getOperand(0);
          const MachineOperand &Op = MI.getOperand(1);
          assert(Def.isReg() && Def.isDef() && "Expected reg def");
          assert(Op.isReg() && Op.isUse() && "Expected reg use");
          int DefIdx = mapRegToGPRIndex(Def.getReg());
          int OpIdx = mapRegToGPRIndex(Op.getReg());
          if (DefIdx >= 0 && OpIdx >= 0 &&
              handleMiddleInst(MI, LOHInfos[DefIdx], LOHInfos[OpIdx]))
            continue;
        }
        break;
      case AArch64::ADRP:
        const MachineOperand &Op0 = MI.getOperand(0);
        int Idx = mapRegToGPRIndex(Op0.getReg());
        if (Idx >= 0) {
          handleADRP(MI, AFI, LOHInfos[Idx]);
          continue;
        }
        break;
      }
      handleNormalInst(MI, LOHInfos);
    }
  }

  // Return "no change": The pass only collects information.
  return false;
}

FunctionPass *llvm::createAArch64CollectLOHPass() {
  return new AArch64CollectLOH();
}
