//===- AArch64SpeculationHardening.cpp - Harden Against Missspeculation  --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass to insert code to mitigate against side channel
// vulnerabilities that may happen under control flow miss-speculation.
//
// The pass implements tracking of control flow miss-speculation into a "taint"
// register. That taint register can then be used to mask off registers with
// sensitive data when executing under miss-speculation, a.k.a. "transient
// execution".
// This pass is aimed at mitigating against SpectreV1-style vulnarabilities.
//
// It also implements speculative load hardening, i.e. using the taint register
// to automatically mask off loaded data.
//
// As a possible follow-on improvement, also an intrinsics-based approach as
// explained at https://lwn.net/Articles/759423/ could be implemented on top of
// the current design.
//
// For AArch64, the following implementation choices are made to implement the
// tracking of control flow miss-speculation into a taint register:
// Some of these are different than the implementation choices made in
// the similar pass implemented in X86SpeculativeLoadHardening.cpp, as
// the instruction set characteristics result in different trade-offs.
// - The speculation hardening is done after register allocation. With a
//   relative abundance of registers, one register is reserved (X16) to be
//   the taint register. X16 is expected to not clash with other register
//   reservation mechanisms with very high probability because:
//   . The AArch64 ABI doesn't guarantee X16 to be retained across any call.
//   . The only way to request X16 to be used as a programmer is through
//     inline assembly. In the rare case a function explicitly demands to
//     use X16/W16, this pass falls back to hardening against speculation
//     by inserting a DSB SYS/ISB barrier pair which will prevent control
//     flow speculation.
// - It is easy to insert mask operations at this late stage as we have
//   mask operations available that don't set flags.
// - The taint variable contains all-ones when no miss-speculation is detected,
//   and contains all-zeros when miss-speculation is detected. Therefore, when
//   masking, an AND instruction (which only changes the register to be masked,
//   no other side effects) can easily be inserted anywhere that's needed.
// - The tracking of miss-speculation is done by using a data-flow conditional
//   select instruction (CSEL) to evaluate the flags that were also used to
//   make conditional branch direction decisions. Speculation of the CSEL
//   instruction can be limited with a CSDB instruction - so the combination of
//   CSEL + a later CSDB gives the guarantee that the flags as used in the CSEL
//   aren't speculated. When conditional branch direction gets miss-speculated,
//   the semantics of the inserted CSEL instruction is such that the taint
//   register will contain all zero bits.
//   One key requirement for this to work is that the conditional branch is
//   followed by an execution of the CSEL instruction, where the CSEL
//   instruction needs to use the same flags status as the conditional branch.
//   This means that the conditional branches must not be implemented as one
//   of the AArch64 conditional branches that do not use the flags as input
//   (CB(N)Z and TB(N)Z). This is implemented by ensuring in the instruction
//   selectors to not produce these instructions when speculation hardening
//   is enabled. This pass will assert if it does encounter such an instruction.
// - On function call boundaries, the miss-speculation state is transferred from
//   the taint register X16 to be encoded in the SP register as value 0.
//
// For the aspect of automatically hardening loads, using the taint register,
// (a.k.a. speculative load hardening, see
//  https://llvm.org/docs/SpeculativeLoadHardening.html), the following
// implementation choices are made for AArch64:
//   - Many of the optimizations described at
//     https://llvm.org/docs/SpeculativeLoadHardening.html to harden fewer
//     loads haven't been implemented yet - but for some of them there are
//     FIXMEs in the code.
//   - loads that load into general purpose (X or W) registers get hardened by
//     masking the loaded data. For loads that load into other registers, the
//     address loaded from gets hardened. It is expected that hardening the
//     loaded data may be more efficient; but masking data in registers other
//     than X or W is not easy and may result in being slower than just
//     hardening the X address register loaded from.
//   - On AArch64, CSDB instructions are inserted between the masking of the
//     register and its first use, to ensure there's no non-control-flow
//     speculation that might undermine the hardening mechanism.
//
// Future extensions/improvements could be:
// - Implement this functionality using full speculation barriers, akin to the
//   x86-slh-lfence option. This may be more useful for the intrinsics-based
//   approach than for the SLH approach to masking.
//   Note that this pass already inserts the full speculation barriers if the
//   function for some niche reason makes use of X16/W16.
// - no indirect branch misprediction gets protected/instrumented; but this
//   could be done for some indirect branches, such as switch jump tables.
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "aarch64-speculation-hardening"

#define AARCH64_SPECULATION_HARDENING_NAME "AArch64 speculation hardening pass"

cl::opt<bool> HardenLoads("aarch64-slh-loads", cl::Hidden,
                          cl::desc("Sanitize loads from memory."),
                          cl::init(true));

namespace {

class AArch64SpeculationHardening : public MachineFunctionPass {
public:
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;

  static char ID;

  AArch64SpeculationHardening() : MachineFunctionPass(ID) {
    initializeAArch64SpeculationHardeningPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override {
    return AARCH64_SPECULATION_HARDENING_NAME;
  }

private:
  unsigned MisspeculatingTaintReg;
  unsigned MisspeculatingTaintReg32Bit;
  bool UseControlFlowSpeculationBarrier;
  BitVector RegsNeedingCSDBBeforeUse;
  BitVector RegsAlreadyMasked;

  bool functionUsesHardeningRegister(MachineFunction &MF) const;
  bool instrumentControlFlow(MachineBasicBlock &MBB,
                             bool &UsesFullSpeculationBarrier);
  bool endsWithCondControlFlow(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                               MachineBasicBlock *&FBB,
                               AArch64CC::CondCode &CondCode) const;
  void insertTrackingCode(MachineBasicBlock &SplitEdgeBB,
                          AArch64CC::CondCode &CondCode, DebugLoc DL) const;
  void insertSPToRegTaintPropagation(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI) const;
  void insertRegToSPTaintPropagation(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     unsigned TmpReg) const;
  void insertFullSpeculationBarrier(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    DebugLoc DL) const;

  bool slhLoads(MachineBasicBlock &MBB);
  bool makeGPRSpeculationSafe(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineInstr &MI, unsigned Reg);
  bool lowerSpeculationSafeValuePseudos(MachineBasicBlock &MBB,
                                        bool UsesFullSpeculationBarrier);
  bool expandSpeculationSafeValue(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  bool UsesFullSpeculationBarrier);
  bool insertCSDB(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                  DebugLoc DL);
};

} // end anonymous namespace

char AArch64SpeculationHardening::ID = 0;

INITIALIZE_PASS(AArch64SpeculationHardening, "aarch64-speculation-hardening",
                AARCH64_SPECULATION_HARDENING_NAME, false, false)

bool AArch64SpeculationHardening::endsWithCondControlFlow(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    AArch64CC::CondCode &CondCode) const {
  SmallVector<MachineOperand, 1> analyzeBranchCondCode;
  if (TII->analyzeBranch(MBB, TBB, FBB, analyzeBranchCondCode, false))
    return false;

  // Ignore if the BB ends in an unconditional branch/fall-through.
  if (analyzeBranchCondCode.empty())
    return false;

  // If the BB ends with a single conditional branch, FBB will be set to
  // nullptr (see API docs for TII->analyzeBranch). For the rest of the
  // analysis we want the FBB block to be set always.
  assert(TBB != nullptr);
  if (FBB == nullptr)
    FBB = MBB.getFallThrough();

  // If both the true and the false condition jump to the same basic block,
  // there isn't need for any protection - whether the branch is speculated
  // correctly or not, we end up executing the architecturally correct code.
  if (TBB == FBB)
    return false;

  assert(MBB.succ_size() == 2);
  // translate analyzeBranchCondCode to CondCode.
  assert(analyzeBranchCondCode.size() == 1 && "unknown Cond array format");
  CondCode = AArch64CC::CondCode(analyzeBranchCondCode[0].getImm());
  return true;
}

void AArch64SpeculationHardening::insertFullSpeculationBarrier(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    DebugLoc DL) const {
  // A full control flow speculation barrier consists of (DSB SYS + ISB)
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::DSB)).addImm(0xf);
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::ISB)).addImm(0xf);
}

void AArch64SpeculationHardening::insertTrackingCode(
    MachineBasicBlock &SplitEdgeBB, AArch64CC::CondCode &CondCode,
    DebugLoc DL) const {
  if (UseControlFlowSpeculationBarrier) {
    insertFullSpeculationBarrier(SplitEdgeBB, SplitEdgeBB.begin(), DL);
  } else {
    BuildMI(SplitEdgeBB, SplitEdgeBB.begin(), DL, TII->get(AArch64::CSELXr))
        .addDef(MisspeculatingTaintReg)
        .addUse(MisspeculatingTaintReg)
        .addUse(AArch64::XZR)
        .addImm(CondCode);
    SplitEdgeBB.addLiveIn(AArch64::NZCV);
  }
}

bool AArch64SpeculationHardening::instrumentControlFlow(
    MachineBasicBlock &MBB, bool &UsesFullSpeculationBarrier) {
  LLVM_DEBUG(dbgs() << "Instrument control flow tracking on MBB: " << MBB);

  bool Modified = false;
  MachineBasicBlock *TBB = nullptr;
  MachineBasicBlock *FBB = nullptr;
  AArch64CC::CondCode CondCode;

  if (!endsWithCondControlFlow(MBB, TBB, FBB, CondCode)) {
    LLVM_DEBUG(dbgs() << "... doesn't end with CondControlFlow\n");
  } else {
    // Now insert:
    // "CSEL MisSpeculatingR, MisSpeculatingR, XZR, cond" on the True edge and
    // "CSEL MisSpeculatingR, MisSpeculatingR, XZR, Invertcond" on the False
    // edge.
    AArch64CC::CondCode InvCondCode = AArch64CC::getInvertedCondCode(CondCode);

    MachineBasicBlock *SplitEdgeTBB = MBB.SplitCriticalEdge(TBB, *this);
    MachineBasicBlock *SplitEdgeFBB = MBB.SplitCriticalEdge(FBB, *this);

    assert(SplitEdgeTBB != nullptr);
    assert(SplitEdgeFBB != nullptr);

    DebugLoc DL;
    if (MBB.instr_end() != MBB.instr_begin())
      DL = (--MBB.instr_end())->getDebugLoc();

    insertTrackingCode(*SplitEdgeTBB, CondCode, DL);
    insertTrackingCode(*SplitEdgeFBB, InvCondCode, DL);

    LLVM_DEBUG(dbgs() << "SplitEdgeTBB: " << *SplitEdgeTBB << "\n");
    LLVM_DEBUG(dbgs() << "SplitEdgeFBB: " << *SplitEdgeFBB << "\n");
    Modified = true;
  }

  // Perform correct code generation around function calls and before returns.
  // The below variables record the return/terminator instructions and the call
  // instructions respectively; including which register is available as a
  // temporary register just before the recorded instructions.
  SmallVector<std::pair<MachineInstr *, unsigned>, 4> ReturnInstructions;
  SmallVector<std::pair<MachineInstr *, unsigned>, 4> CallInstructions;
  // if a temporary register is not available for at least one of the
  // instructions for which we need to transfer taint to the stack pointer, we
  // need to insert a full speculation barrier.
  // TmpRegisterNotAvailableEverywhere tracks that condition.
  bool TmpRegisterNotAvailableEverywhere = false;

  RegScavenger RS;
  RS.enterBasicBlock(MBB);

  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); I++) {
    MachineInstr &MI = *I;
    if (!MI.isReturn() && !MI.isCall())
      continue;

    // The RegScavenger represents registers available *after* the MI
    // instruction pointed to by RS.getCurrentPosition().
    // We need to have a register that is available *before* the MI is executed.
    if (I != MBB.begin())
      RS.forward(std::prev(I));
    // FIXME: The below just finds *a* unused register. Maybe code could be
    // optimized more if this looks for the register that isn't used for the
    // longest time around this place, to enable more scheduling freedom. Not
    // sure if that would actually result in a big performance difference
    // though. Maybe RegisterScavenger::findSurvivorBackwards has some logic
    // already to do this - but it's unclear if that could easily be used here.
    unsigned TmpReg = RS.FindUnusedReg(&AArch64::GPR64commonRegClass);
    LLVM_DEBUG(dbgs() << "RS finds "
                      << ((TmpReg == 0) ? "no register " : "register ");
               if (TmpReg != 0) dbgs() << printReg(TmpReg, TRI) << " ";
               dbgs() << "to be available at MI " << MI);
    if (TmpReg == 0)
      TmpRegisterNotAvailableEverywhere = true;
    if (MI.isReturn())
      ReturnInstructions.push_back({&MI, TmpReg});
    else if (MI.isCall())
      CallInstructions.push_back({&MI, TmpReg});
  }

  if (TmpRegisterNotAvailableEverywhere) {
    // When a temporary register is not available everywhere in this basic
    // basic block where a propagate-taint-to-sp operation is needed, just
    // emit a full speculation barrier at the start of this basic block, which
    // renders the taint/speculation tracking in this basic block unnecessary.
    insertFullSpeculationBarrier(MBB, MBB.begin(),
                                 (MBB.begin())->getDebugLoc());
    UsesFullSpeculationBarrier = true;
    Modified = true;
  } else {
    for (auto MI_Reg : ReturnInstructions) {
      assert(MI_Reg.second != 0);
      LLVM_DEBUG(
          dbgs()
          << " About to insert Reg to SP taint propagation with temp register "
          << printReg(MI_Reg.second, TRI)
          << " on instruction: " << *MI_Reg.first);
      insertRegToSPTaintPropagation(MBB, MI_Reg.first, MI_Reg.second);
      Modified = true;
    }

    for (auto MI_Reg : CallInstructions) {
      assert(MI_Reg.second != 0);
      LLVM_DEBUG(dbgs() << " About to insert Reg to SP and back taint "
                           "propagation with temp register "
                        << printReg(MI_Reg.second, TRI)
                        << " around instruction: " << *MI_Reg.first);
      // Just after the call:
      insertSPToRegTaintPropagation(
          MBB, std::next((MachineBasicBlock::iterator)MI_Reg.first));
      // Just before the call:
      insertRegToSPTaintPropagation(MBB, MI_Reg.first, MI_Reg.second);
      Modified = true;
    }
  }
  return Modified;
}

void AArch64SpeculationHardening::insertSPToRegTaintPropagation(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) const {
  // If full control flow speculation barriers are used, emit a control flow
  // barrier to block potential miss-speculation in flight coming in to this
  // function.
  if (UseControlFlowSpeculationBarrier) {
    insertFullSpeculationBarrier(MBB, MBBI, DebugLoc());
    return;
  }

  // CMP   SP, #0   === SUBS   xzr, SP, #0
  BuildMI(MBB, MBBI, DebugLoc(), TII->get(AArch64::SUBSXri))
      .addDef(AArch64::XZR)
      .addUse(AArch64::SP)
      .addImm(0)
      .addImm(0); // no shift
  // CSETM x16, NE  === CSINV  x16, xzr, xzr, EQ
  BuildMI(MBB, MBBI, DebugLoc(), TII->get(AArch64::CSINVXr))
      .addDef(MisspeculatingTaintReg)
      .addUse(AArch64::XZR)
      .addUse(AArch64::XZR)
      .addImm(AArch64CC::EQ);
}

void AArch64SpeculationHardening::insertRegToSPTaintPropagation(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    unsigned TmpReg) const {
  // If full control flow speculation barriers are used, there will not be
  // miss-speculation when returning from this function, and therefore, also
  // no need to encode potential miss-speculation into the stack pointer.
  if (UseControlFlowSpeculationBarrier)
    return;

  // mov   Xtmp, SP  === ADD  Xtmp, SP, #0
  BuildMI(MBB, MBBI, DebugLoc(), TII->get(AArch64::ADDXri))
      .addDef(TmpReg)
      .addUse(AArch64::SP)
      .addImm(0)
      .addImm(0); // no shift
  // and   Xtmp, Xtmp, TaintReg === AND Xtmp, Xtmp, TaintReg, #0
  BuildMI(MBB, MBBI, DebugLoc(), TII->get(AArch64::ANDXrs))
      .addDef(TmpReg, RegState::Renamable)
      .addUse(TmpReg, RegState::Kill | RegState::Renamable)
      .addUse(MisspeculatingTaintReg, RegState::Kill)
      .addImm(0);
  // mov   SP, Xtmp === ADD SP, Xtmp, #0
  BuildMI(MBB, MBBI, DebugLoc(), TII->get(AArch64::ADDXri))
      .addDef(AArch64::SP)
      .addUse(TmpReg, RegState::Kill)
      .addImm(0)
      .addImm(0); // no shift
}

bool AArch64SpeculationHardening::functionUsesHardeningRegister(
    MachineFunction &MF) const {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // treat function calls specially, as the hardening register does not
      // need to remain live across function calls.
      if (MI.isCall())
        continue;
      if (MI.readsRegister(MisspeculatingTaintReg, TRI) ||
          MI.modifiesRegister(MisspeculatingTaintReg, TRI))
        return true;
    }
  }
  return false;
}

// Make GPR register Reg speculation-safe by putting it through the
// SpeculationSafeValue pseudo instruction, if we can't prove that
// the value in the register has already been hardened.
bool AArch64SpeculationHardening::makeGPRSpeculationSafe(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, MachineInstr &MI,
    unsigned Reg) {
  assert(AArch64::GPR32allRegClass.contains(Reg) ||
         AArch64::GPR64allRegClass.contains(Reg));

  // Loads cannot directly load a value into the SP (nor WSP).
  // Therefore, if Reg is SP or WSP, it is because the instruction loads from
  // the stack through the stack pointer.
  //
  // Since the stack pointer is never dynamically controllable, don't harden it.
  if (Reg == AArch64::SP || Reg == AArch64::WSP)
    return false;

  // Do not harden the register again if already hardened before.
  if (RegsAlreadyMasked[Reg])
    return false;

  const bool Is64Bit = AArch64::GPR64allRegClass.contains(Reg);
  LLVM_DEBUG(dbgs() << "About to harden register : " << Reg << "\n");
  BuildMI(MBB, MBBI, MI.getDebugLoc(),
          TII->get(Is64Bit ? AArch64::SpeculationSafeValueX
                           : AArch64::SpeculationSafeValueW))
      .addDef(Reg)
      .addUse(Reg);
  RegsAlreadyMasked.set(Reg);
  return true;
}

bool AArch64SpeculationHardening::slhLoads(MachineBasicBlock &MBB) {
  bool Modified = false;

  LLVM_DEBUG(dbgs() << "slhLoads running on MBB: " << MBB);

  RegsAlreadyMasked.reset();

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  MachineBasicBlock::iterator NextMBBI;
  for (; MBBI != E; MBBI = NextMBBI) {
    MachineInstr &MI = *MBBI;
    NextMBBI = std::next(MBBI);
    // Only harden loaded values or addresses used in loads.
    if (!MI.mayLoad())
      continue;

    LLVM_DEBUG(dbgs() << "About to harden: " << MI);

    // For general purpose register loads, harden the registers loaded into.
    // For other loads, harden the address loaded from.
    // Masking the loaded value is expected to result in less performance
    // overhead, as the load can still execute speculatively in comparison to
    // when the address loaded from gets masked. However, masking is only
    // easy to do efficiently on GPR registers, so for loads into non-GPR
    // registers (e.g. floating point loads), mask the address loaded from.
    bool AllDefsAreGPR = llvm::all_of(MI.defs(), [&](MachineOperand &Op) {
      return Op.isReg() && (AArch64::GPR32allRegClass.contains(Op.getReg()) ||
                            AArch64::GPR64allRegClass.contains(Op.getReg()));
    });
    // FIXME: it might be a worthwhile optimization to not mask loaded
    // values if all the registers involved in address calculation are already
    // hardened, leading to this load not able to execute on a miss-speculated
    // path.
    bool HardenLoadedData = AllDefsAreGPR;
    bool HardenAddressLoadedFrom = !HardenLoadedData;

    // First remove registers from AlreadyMaskedRegisters if their value is
    // updated by this instruction - it makes them contain a new value that is
    // not guaranteed to already have been masked.
    for (MachineOperand Op : MI.defs())
      for (MCRegAliasIterator AI(Op.getReg(), TRI, true); AI.isValid(); ++AI)
        RegsAlreadyMasked.reset(*AI);

    // FIXME: loads from the stack with an immediate offset from the stack
    // pointer probably shouldn't be hardened, which could result in a
    // significant optimization. See section "Don’t check loads from
    // compile-time constant stack offsets", in
    // https://llvm.org/docs/SpeculativeLoadHardening.html

    if (HardenLoadedData)
      for (auto Def : MI.defs()) {
        if (Def.isDead())
          // Do not mask a register that is not used further.
          continue;
        // FIXME: For pre/post-increment addressing modes, the base register
        // used in address calculation is also defined by this instruction.
        // It might be a worthwhile optimization to not harden that
        // base register increment/decrement when the increment/decrement is
        // an immediate.
        Modified |= makeGPRSpeculationSafe(MBB, NextMBBI, MI, Def.getReg());
      }

    if (HardenAddressLoadedFrom)
      for (auto Use : MI.uses()) {
        if (!Use.isReg())
          continue;
        unsigned Reg = Use.getReg();
        // Some loads of floating point data have implicit defs/uses on a
        // super register of that floating point data. Some examples:
        // $s0 = LDRSui $sp, 22, implicit-def $q0
        // $q0 = LD1i64 $q0, 1, renamable $x0
        // We need to filter out these uses for non-GPR register which occur
        // because the load partially fills a non-GPR register with the loaded
        // data. Just skipping all non-GPR registers is safe (for now) as all
        // AArch64 load instructions only use GPR registers to perform the
        // address calculation. FIXME: However that might change once we can
        // produce SVE gather instructions.
        if (!(AArch64::GPR32allRegClass.contains(Reg) ||
              AArch64::GPR64allRegClass.contains(Reg)))
          continue;
        Modified |= makeGPRSpeculationSafe(MBB, MBBI, MI, Reg);
      }
  }
  return Modified;
}

/// \brief If MBBI references a pseudo instruction that should be expanded
/// here, do the expansion and return true. Otherwise return false.
bool AArch64SpeculationHardening::expandSpeculationSafeValue(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    bool UsesFullSpeculationBarrier) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  bool Is64Bit = true;

  switch (Opcode) {
  default:
    break;
  case AArch64::SpeculationSafeValueW:
    Is64Bit = false;
    LLVM_FALLTHROUGH;
  case AArch64::SpeculationSafeValueX:
    // Just remove the SpeculationSafe pseudo's if control flow
    // miss-speculation isn't happening because we're already inserting barriers
    // to guarantee that.
    if (!UseControlFlowSpeculationBarrier && !UsesFullSpeculationBarrier) {
      unsigned DstReg = MI.getOperand(0).getReg();
      unsigned SrcReg = MI.getOperand(1).getReg();
      // Mark this register and all its aliasing registers as needing to be
      // value speculation hardened before its next use, by using a CSDB
      // barrier instruction.
      for (MachineOperand Op : MI.defs())
        for (MCRegAliasIterator AI(Op.getReg(), TRI, true); AI.isValid(); ++AI)
          RegsNeedingCSDBBeforeUse.set(*AI);

      // Mask off with taint state.
      BuildMI(MBB, MBBI, MI.getDebugLoc(),
              Is64Bit ? TII->get(AArch64::ANDXrs) : TII->get(AArch64::ANDWrs))
          .addDef(DstReg)
          .addUse(SrcReg, RegState::Kill)
          .addUse(Is64Bit ? MisspeculatingTaintReg
                          : MisspeculatingTaintReg32Bit)
          .addImm(0);
    }
    MI.eraseFromParent();
    return true;
  }
  return false;
}

bool AArch64SpeculationHardening::insertCSDB(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MBBI,
                                             DebugLoc DL) {
  assert(!UseControlFlowSpeculationBarrier && "No need to insert CSDBs when "
                                              "control flow miss-speculation "
                                              "is already blocked");
  // insert data value speculation barrier (CSDB)
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::HINT)).addImm(0x14);
  RegsNeedingCSDBBeforeUse.reset();
  return true;
}

bool AArch64SpeculationHardening::lowerSpeculationSafeValuePseudos(
    MachineBasicBlock &MBB, bool UsesFullSpeculationBarrier) {
  bool Modified = false;

  RegsNeedingCSDBBeforeUse.reset();

  // The following loop iterates over all instructions in the basic block,
  // and performs 2 operations:
  // 1. Insert a CSDB at this location if needed.
  // 2. Expand the SpeculationSafeValuePseudo if the current instruction is
  // one.
  //
  // The insertion of the CSDB is done as late as possible (i.e. just before
  // the use of a masked register), in the hope that that will reduce the
  // total number of CSDBs in a block when there are multiple masked registers
  // in the block.
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  DebugLoc DL;
  while (MBBI != E) {
    MachineInstr &MI = *MBBI;
    DL = MI.getDebugLoc();
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);

    // First check if a CSDB needs to be inserted due to earlier registers
    // that were masked and that are used by the next instruction.
    // Also emit the barrier on any potential control flow changes.
    bool NeedToEmitBarrier = false;
    if (RegsNeedingCSDBBeforeUse.any() && (MI.isCall() || MI.isTerminator()))
      NeedToEmitBarrier = true;
    if (!NeedToEmitBarrier)
      for (MachineOperand Op : MI.uses())
        if (Op.isReg() && RegsNeedingCSDBBeforeUse[Op.getReg()]) {
          NeedToEmitBarrier = true;
          break;
        }

    if (NeedToEmitBarrier && !UsesFullSpeculationBarrier)
      Modified |= insertCSDB(MBB, MBBI, DL);

    Modified |=
        expandSpeculationSafeValue(MBB, MBBI, UsesFullSpeculationBarrier);

    MBBI = NMBBI;
  }

  if (RegsNeedingCSDBBeforeUse.any() && !UsesFullSpeculationBarrier)
    Modified |= insertCSDB(MBB, MBBI, DL);

  return Modified;
}

bool AArch64SpeculationHardening::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening))
    return false;

  MisspeculatingTaintReg = AArch64::X16;
  MisspeculatingTaintReg32Bit = AArch64::W16;
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  RegsNeedingCSDBBeforeUse.resize(TRI->getNumRegs());
  RegsAlreadyMasked.resize(TRI->getNumRegs());
  UseControlFlowSpeculationBarrier = functionUsesHardeningRegister(MF);

  bool Modified = false;

  // Step 1: Enable automatic insertion of SpeculationSafeValue.
  if (HardenLoads) {
    LLVM_DEBUG(
        dbgs() << "***** AArch64SpeculationHardening - automatic insertion of "
                  "SpeculationSafeValue intrinsics *****\n");
    for (auto &MBB : MF)
      Modified |= slhLoads(MBB);
  }

  // 2. Add instrumentation code to function entry and exits.
  LLVM_DEBUG(
      dbgs()
      << "***** AArch64SpeculationHardening - track control flow *****\n");

  SmallVector<MachineBasicBlock *, 2> EntryBlocks;
  EntryBlocks.push_back(&MF.front());
  for (const LandingPadInfo &LPI : MF.getLandingPads())
    EntryBlocks.push_back(LPI.LandingPadBlock);
  for (auto Entry : EntryBlocks)
    insertSPToRegTaintPropagation(
        *Entry, Entry->SkipPHIsLabelsAndDebug(Entry->begin()));

  // 3. Add instrumentation code to every basic block.
  for (auto &MBB : MF) {
    bool UsesFullSpeculationBarrier = false;
    Modified |= instrumentControlFlow(MBB, UsesFullSpeculationBarrier);
    Modified |=
        lowerSpeculationSafeValuePseudos(MBB, UsesFullSpeculationBarrier);
  }

  return Modified;
}

/// \brief Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createAArch64SpeculationHardeningPass() {
  return new AArch64SpeculationHardening();
}
