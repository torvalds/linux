//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that performs optimization on SIMD instructions
// with high latency by splitting them into more efficient series of
// instructions.
//
// 1. Rewrite certain SIMD instructions with vector element due to their
// inefficiency on some targets.
//
// For example:
//    fmla v0.4s, v1.4s, v2.s[1]
//
// Is rewritten into:
//    dup v3.4s, v2.s[1]
//    fmla v0.4s, v1.4s, v3.4s
//
// 2. Rewrite interleaved memory access instructions due to their
// inefficiency on some targets.
//
// For example:
//    st2 {v0.4s, v1.4s}, addr
//
// Is rewritten into:
//    zip1 v2.4s, v0.4s, v1.4s
//    zip2 v3.4s, v0.4s, v1.4s
//    stp  q2, q3,  addr
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Pass.h"
#include <unordered_map>

using namespace llvm;

#define DEBUG_TYPE "aarch64-simdinstr-opt"

STATISTIC(NumModifiedInstr,
          "Number of SIMD instructions modified");

#define AARCH64_VECTOR_BY_ELEMENT_OPT_NAME                                     \
  "AArch64 SIMD instructions optimization pass"

namespace {

struct AArch64SIMDInstrOpt : public MachineFunctionPass {
  static char ID;

  const TargetInstrInfo *TII;
  MachineRegisterInfo *MRI;
  TargetSchedModel SchedModel;

  // The two maps below are used to cache decisions instead of recomputing:
  // This is used to cache instruction replacement decisions within function
  // units and across function units.
  std::map<std::pair<unsigned, std::string>, bool> SIMDInstrTable;
  // This is used to cache the decision of whether to leave the interleaved
  // store instructions replacement pass early or not for a particular target.
  std::unordered_map<std::string, bool> InterlEarlyExit;

  typedef enum {
    VectorElem,
    Interleave
  } Subpass;

  // Instruction represented by OrigOpc is replaced by instructions in ReplOpc.
  struct InstReplInfo {
    unsigned OrigOpc;
		std::vector<unsigned> ReplOpc;
    const TargetRegisterClass RC;
  };

#define RuleST2(OpcOrg, OpcR0, OpcR1, OpcR2, RC) \
  {OpcOrg, {OpcR0, OpcR1, OpcR2}, RC}
#define RuleST4(OpcOrg, OpcR0, OpcR1, OpcR2, OpcR3, OpcR4, OpcR5, OpcR6, \
                OpcR7, OpcR8, OpcR9, RC) \
  {OpcOrg, \
   {OpcR0, OpcR1, OpcR2, OpcR3, OpcR4, OpcR5, OpcR6, OpcR7, OpcR8, OpcR9}, RC}

  // The Instruction Replacement Table:
  std::vector<InstReplInfo> IRT = {
    // ST2 instructions
    RuleST2(AArch64::ST2Twov2d, AArch64::ZIP1v2i64, AArch64::ZIP2v2i64,
          AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST2(AArch64::ST2Twov4s, AArch64::ZIP1v4i32, AArch64::ZIP2v4i32,
          AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST2(AArch64::ST2Twov2s, AArch64::ZIP1v2i32, AArch64::ZIP2v2i32,
          AArch64::STPDi, AArch64::FPR64RegClass),
    RuleST2(AArch64::ST2Twov8h, AArch64::ZIP1v8i16, AArch64::ZIP2v8i16,
          AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST2(AArch64::ST2Twov4h, AArch64::ZIP1v4i16, AArch64::ZIP2v4i16,
          AArch64::STPDi, AArch64::FPR64RegClass),
    RuleST2(AArch64::ST2Twov16b, AArch64::ZIP1v16i8, AArch64::ZIP2v16i8,
          AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST2(AArch64::ST2Twov8b, AArch64::ZIP1v8i8, AArch64::ZIP2v8i8,
          AArch64::STPDi, AArch64::FPR64RegClass),
    // ST4 instructions
    RuleST4(AArch64::ST4Fourv2d, AArch64::ZIP1v2i64, AArch64::ZIP2v2i64,
          AArch64::ZIP1v2i64, AArch64::ZIP2v2i64, AArch64::ZIP1v2i64,
          AArch64::ZIP2v2i64, AArch64::ZIP1v2i64, AArch64::ZIP2v2i64,
          AArch64::STPQi, AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST4(AArch64::ST4Fourv4s, AArch64::ZIP1v4i32, AArch64::ZIP2v4i32,
          AArch64::ZIP1v4i32, AArch64::ZIP2v4i32, AArch64::ZIP1v4i32,
          AArch64::ZIP2v4i32, AArch64::ZIP1v4i32, AArch64::ZIP2v4i32,
          AArch64::STPQi, AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST4(AArch64::ST4Fourv2s, AArch64::ZIP1v2i32, AArch64::ZIP2v2i32,
          AArch64::ZIP1v2i32, AArch64::ZIP2v2i32, AArch64::ZIP1v2i32,
          AArch64::ZIP2v2i32, AArch64::ZIP1v2i32, AArch64::ZIP2v2i32,
          AArch64::STPDi, AArch64::STPDi, AArch64::FPR64RegClass),
    RuleST4(AArch64::ST4Fourv8h, AArch64::ZIP1v8i16, AArch64::ZIP2v8i16,
          AArch64::ZIP1v8i16, AArch64::ZIP2v8i16, AArch64::ZIP1v8i16,
          AArch64::ZIP2v8i16, AArch64::ZIP1v8i16, AArch64::ZIP2v8i16,
          AArch64::STPQi, AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST4(AArch64::ST4Fourv4h, AArch64::ZIP1v4i16, AArch64::ZIP2v4i16,
          AArch64::ZIP1v4i16, AArch64::ZIP2v4i16, AArch64::ZIP1v4i16,
          AArch64::ZIP2v4i16, AArch64::ZIP1v4i16, AArch64::ZIP2v4i16,
          AArch64::STPDi, AArch64::STPDi, AArch64::FPR64RegClass),
    RuleST4(AArch64::ST4Fourv16b, AArch64::ZIP1v16i8, AArch64::ZIP2v16i8,
          AArch64::ZIP1v16i8, AArch64::ZIP2v16i8, AArch64::ZIP1v16i8,
          AArch64::ZIP2v16i8, AArch64::ZIP1v16i8, AArch64::ZIP2v16i8,
          AArch64::STPQi, AArch64::STPQi, AArch64::FPR128RegClass),
    RuleST4(AArch64::ST4Fourv8b, AArch64::ZIP1v8i8, AArch64::ZIP2v8i8,
          AArch64::ZIP1v8i8, AArch64::ZIP2v8i8, AArch64::ZIP1v8i8,
          AArch64::ZIP2v8i8, AArch64::ZIP1v8i8, AArch64::ZIP2v8i8,
          AArch64::STPDi, AArch64::STPDi, AArch64::FPR64RegClass)
  };

  // A costly instruction is replaced in this work by N efficient instructions
  // The maximum of N is curently 10 and it is for ST4 case.
  static const unsigned MaxNumRepl = 10;

  AArch64SIMDInstrOpt() : MachineFunctionPass(ID) {
    initializeAArch64SIMDInstrOptPass(*PassRegistry::getPassRegistry());
  }

  /// Based only on latency of instructions, determine if it is cost efficient
  /// to replace the instruction InstDesc by the instructions stored in the
  /// array InstDescRepl.
  /// Return true if replacement is expected to be faster.
  bool shouldReplaceInst(MachineFunction *MF, const MCInstrDesc *InstDesc,
                         SmallVectorImpl<const MCInstrDesc*> &ReplInstrMCID);

  /// Determine if we need to exit the instruction replacement optimization
  /// passes early. This makes sure that no compile time is spent in this pass
  /// for targets with no need for any of these optimizations.
  /// Return true if early exit of the pass is recommended.
  bool shouldExitEarly(MachineFunction *MF, Subpass SP);

  /// Check whether an equivalent DUP instruction has already been
  /// created or not.
  /// Return true when the DUP instruction already exists. In this case,
  /// DestReg will point to the destination of the already created DUP.
  bool reuseDUP(MachineInstr &MI, unsigned DupOpcode, unsigned SrcReg,
                unsigned LaneNumber, unsigned *DestReg) const;

  /// Certain SIMD instructions with vector element operand are not efficient.
  /// Rewrite them into SIMD instructions with vector operands. This rewrite
  /// is driven by the latency of the instructions.
  /// Return true if the SIMD instruction is modified.
  bool optimizeVectElement(MachineInstr &MI);

  /// Process The REG_SEQUENCE instruction, and extract the source
  /// operands of the ST2/4 instruction from it.
  /// Example of such instructions.
  ///    %dest = REG_SEQUENCE %st2_src1, dsub0, %st2_src2, dsub1;
  /// Return true when the instruction is processed successfully.
  bool processSeqRegInst(MachineInstr *DefiningMI, unsigned* StReg,
                         unsigned* StRegKill, unsigned NumArg) const;

  /// Load/Store Interleaving instructions are not always beneficial.
  /// Replace them by ZIP instructionand classical load/store.
  /// Return true if the SIMD instruction is modified.
  bool optimizeLdStInterleave(MachineInstr &MI);

  /// Return the number of useful source registers for this
  /// instruction (2 for ST2 and 4 for ST4).
  unsigned determineSrcReg(MachineInstr &MI) const;

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override {
    return AARCH64_VECTOR_BY_ELEMENT_OPT_NAME;
  }
};

char AArch64SIMDInstrOpt::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(AArch64SIMDInstrOpt, "aarch64-simdinstr-opt",
                AARCH64_VECTOR_BY_ELEMENT_OPT_NAME, false, false)

/// Based only on latency of instructions, determine if it is cost efficient
/// to replace the instruction InstDesc by the instructions stored in the
/// array InstDescRepl.
/// Return true if replacement is expected to be faster.
bool AArch64SIMDInstrOpt::
shouldReplaceInst(MachineFunction *MF, const MCInstrDesc *InstDesc,
                  SmallVectorImpl<const MCInstrDesc*> &InstDescRepl) {
  // Check if replacement decision is already available in the cached table.
  // if so, return it.
  std::string Subtarget = SchedModel.getSubtargetInfo()->getCPU();
  auto InstID = std::make_pair(InstDesc->getOpcode(), Subtarget);
  if (SIMDInstrTable.find(InstID) != SIMDInstrTable.end())
    return SIMDInstrTable[InstID];

  unsigned SCIdx = InstDesc->getSchedClass();
  const MCSchedClassDesc *SCDesc =
    SchedModel.getMCSchedModel()->getSchedClassDesc(SCIdx);

  // If a target does not define resources for the instructions
  // of interest, then return false for no replacement.
  const MCSchedClassDesc *SCDescRepl;
  if (!SCDesc->isValid() || SCDesc->isVariant())
  {
    SIMDInstrTable[InstID] = false;
    return false;
  }
  for (auto IDesc : InstDescRepl)
  {
    SCDescRepl = SchedModel.getMCSchedModel()->getSchedClassDesc(
      IDesc->getSchedClass());
    if (!SCDescRepl->isValid() || SCDescRepl->isVariant())
    {
      SIMDInstrTable[InstID] = false;
      return false;
    }
  }

  // Replacement cost.
  unsigned ReplCost = 0;
  for (auto IDesc :InstDescRepl)
    ReplCost += SchedModel.computeInstrLatency(IDesc->getOpcode());

  if (SchedModel.computeInstrLatency(InstDesc->getOpcode()) > ReplCost)
  {
    SIMDInstrTable[InstID] = true;
    return true;
  }
  else
  {
    SIMDInstrTable[InstID] = false;
    return false;
  }
}

/// Determine if we need to exit this pass for a kind of instruction replacement
/// early. This makes sure that no compile time is spent in this pass for
/// targets with no need for any of these optimizations beyond performing this
/// check.
/// Return true if early exit of this pass for a kind of instruction
/// replacement is recommended for a target.
bool AArch64SIMDInstrOpt::shouldExitEarly(MachineFunction *MF, Subpass SP) {
  const MCInstrDesc* OriginalMCID;
  SmallVector<const MCInstrDesc*, MaxNumRepl> ReplInstrMCID;

  switch (SP) {
  // For this optimization, check by comparing the latency of a representative
  // instruction to that of the replacement instructions.
  // TODO: check for all concerned instructions.
  case VectorElem:
    OriginalMCID = &TII->get(AArch64::FMLAv4i32_indexed);
    ReplInstrMCID.push_back(&TII->get(AArch64::DUPv4i32lane));
    ReplInstrMCID.push_back(&TII->get(AArch64::FMLAv4f32));
    if (shouldReplaceInst(MF, OriginalMCID, ReplInstrMCID))
      return false;
    break;

  // For this optimization, check for all concerned instructions.
  case Interleave:
    std::string Subtarget = SchedModel.getSubtargetInfo()->getCPU();
    if (InterlEarlyExit.find(Subtarget) != InterlEarlyExit.end())
      return InterlEarlyExit[Subtarget];

    for (auto &I : IRT) {
      OriginalMCID = &TII->get(I.OrigOpc);
      for (auto &Repl : I.ReplOpc)
        ReplInstrMCID.push_back(&TII->get(Repl));
      if (shouldReplaceInst(MF, OriginalMCID, ReplInstrMCID)) {
        InterlEarlyExit[Subtarget] = false;
        return false;
      }
      ReplInstrMCID.clear();
    }
    InterlEarlyExit[Subtarget] = true;
    break;
  }

  return true;
}

/// Check whether an equivalent DUP instruction has already been
/// created or not.
/// Return true when the DUP instruction already exists. In this case,
/// DestReg will point to the destination of the already created DUP.
bool AArch64SIMDInstrOpt::reuseDUP(MachineInstr &MI, unsigned DupOpcode,
                                         unsigned SrcReg, unsigned LaneNumber,
                                         unsigned *DestReg) const {
  for (MachineBasicBlock::iterator MII = MI, MIE = MI.getParent()->begin();
       MII != MIE;) {
    MII--;
    MachineInstr *CurrentMI = &*MII;

    if (CurrentMI->getOpcode() == DupOpcode &&
        CurrentMI->getNumOperands() == 3 &&
        CurrentMI->getOperand(1).getReg() == SrcReg &&
        CurrentMI->getOperand(2).getImm() == LaneNumber) {
      *DestReg = CurrentMI->getOperand(0).getReg();
      return true;
    }
  }

  return false;
}

/// Certain SIMD instructions with vector element operand are not efficient.
/// Rewrite them into SIMD instructions with vector operands. This rewrite
/// is driven by the latency of the instructions.
/// The instruction of concerns are for the time being FMLA, FMLS, FMUL,
/// and FMULX and hence they are hardcoded.
///
/// For example:
///    fmla v0.4s, v1.4s, v2.s[1]
///
/// Is rewritten into
///    dup  v3.4s, v2.s[1]      // DUP not necessary if redundant
///    fmla v0.4s, v1.4s, v3.4s
///
/// Return true if the SIMD instruction is modified.
bool AArch64SIMDInstrOpt::optimizeVectElement(MachineInstr &MI) {
  const MCInstrDesc *MulMCID, *DupMCID;
  const TargetRegisterClass *RC = &AArch64::FPR128RegClass;

  switch (MI.getOpcode()) {
  default:
    return false;

  // 4X32 instructions
  case AArch64::FMLAv4i32_indexed:
    DupMCID = &TII->get(AArch64::DUPv4i32lane);
    MulMCID = &TII->get(AArch64::FMLAv4f32);
    break;
  case AArch64::FMLSv4i32_indexed:
    DupMCID = &TII->get(AArch64::DUPv4i32lane);
    MulMCID = &TII->get(AArch64::FMLSv4f32);
    break;
  case AArch64::FMULXv4i32_indexed:
    DupMCID = &TII->get(AArch64::DUPv4i32lane);
    MulMCID = &TII->get(AArch64::FMULXv4f32);
    break;
  case AArch64::FMULv4i32_indexed:
    DupMCID = &TII->get(AArch64::DUPv4i32lane);
    MulMCID = &TII->get(AArch64::FMULv4f32);
    break;

  // 2X64 instructions
  case AArch64::FMLAv2i64_indexed:
    DupMCID = &TII->get(AArch64::DUPv2i64lane);
    MulMCID = &TII->get(AArch64::FMLAv2f64);
    break;
  case AArch64::FMLSv2i64_indexed:
    DupMCID = &TII->get(AArch64::DUPv2i64lane);
    MulMCID = &TII->get(AArch64::FMLSv2f64);
    break;
  case AArch64::FMULXv2i64_indexed:
    DupMCID = &TII->get(AArch64::DUPv2i64lane);
    MulMCID = &TII->get(AArch64::FMULXv2f64);
    break;
  case AArch64::FMULv2i64_indexed:
    DupMCID = &TII->get(AArch64::DUPv2i64lane);
    MulMCID = &TII->get(AArch64::FMULv2f64);
    break;

  // 2X32 instructions
  case AArch64::FMLAv2i32_indexed:
    RC = &AArch64::FPR64RegClass;
    DupMCID = &TII->get(AArch64::DUPv2i32lane);
    MulMCID = &TII->get(AArch64::FMLAv2f32);
    break;
  case AArch64::FMLSv2i32_indexed:
    RC = &AArch64::FPR64RegClass;
    DupMCID = &TII->get(AArch64::DUPv2i32lane);
    MulMCID = &TII->get(AArch64::FMLSv2f32);
    break;
  case AArch64::FMULXv2i32_indexed:
    RC = &AArch64::FPR64RegClass;
    DupMCID = &TII->get(AArch64::DUPv2i32lane);
    MulMCID = &TII->get(AArch64::FMULXv2f32);
    break;
  case AArch64::FMULv2i32_indexed:
    RC = &AArch64::FPR64RegClass;
    DupMCID = &TII->get(AArch64::DUPv2i32lane);
    MulMCID = &TII->get(AArch64::FMULv2f32);
    break;
  }

  SmallVector<const MCInstrDesc*, 2> ReplInstrMCID;
  ReplInstrMCID.push_back(DupMCID);
  ReplInstrMCID.push_back(MulMCID);
  if (!shouldReplaceInst(MI.getParent()->getParent(), &TII->get(MI.getOpcode()),
                         ReplInstrMCID))
    return false;

  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  // Get the operands of the current SIMD arithmetic instruction.
  unsigned MulDest = MI.getOperand(0).getReg();
  unsigned SrcReg0 = MI.getOperand(1).getReg();
  unsigned Src0IsKill = getKillRegState(MI.getOperand(1).isKill());
  unsigned SrcReg1 = MI.getOperand(2).getReg();
  unsigned Src1IsKill = getKillRegState(MI.getOperand(2).isKill());
  unsigned DupDest;

  // Instructions of interest have either 4 or 5 operands.
  if (MI.getNumOperands() == 5) {
    unsigned SrcReg2 = MI.getOperand(3).getReg();
    unsigned Src2IsKill = getKillRegState(MI.getOperand(3).isKill());
    unsigned LaneNumber = MI.getOperand(4).getImm();
    // Create a new DUP instruction. Note that if an equivalent DUP instruction
    // has already been created before, then use that one instead of creating
    // a new one.
    if (!reuseDUP(MI, DupMCID->getOpcode(), SrcReg2, LaneNumber, &DupDest)) {
      DupDest = MRI.createVirtualRegister(RC);
      BuildMI(MBB, MI, DL, *DupMCID, DupDest)
          .addReg(SrcReg2, Src2IsKill)
          .addImm(LaneNumber);
    }
    BuildMI(MBB, MI, DL, *MulMCID, MulDest)
        .addReg(SrcReg0, Src0IsKill)
        .addReg(SrcReg1, Src1IsKill)
        .addReg(DupDest, Src2IsKill);
  } else if (MI.getNumOperands() == 4) {
    unsigned LaneNumber = MI.getOperand(3).getImm();
    if (!reuseDUP(MI, DupMCID->getOpcode(), SrcReg1, LaneNumber, &DupDest)) {
      DupDest = MRI.createVirtualRegister(RC);
      BuildMI(MBB, MI, DL, *DupMCID, DupDest)
          .addReg(SrcReg1, Src1IsKill)
          .addImm(LaneNumber);
    }
    BuildMI(MBB, MI, DL, *MulMCID, MulDest)
        .addReg(SrcReg0, Src0IsKill)
        .addReg(DupDest, Src1IsKill);
  } else {
    return false;
  }

  ++NumModifiedInstr;
  return true;
}

/// Load/Store Interleaving instructions are not always beneficial.
/// Replace them by ZIP instructions and classical load/store.
///
/// For example:
///    st2 {v0.4s, v1.4s}, addr
///
/// Is rewritten into:
///    zip1 v2.4s, v0.4s, v1.4s
///    zip2 v3.4s, v0.4s, v1.4s
///    stp  q2, q3, addr
//
/// For example:
///    st4 {v0.4s, v1.4s, v2.4s, v3.4s}, addr
///
/// Is rewritten into:
///    zip1 v4.4s, v0.4s, v2.4s
///    zip2 v5.4s, v0.4s, v2.4s
///    zip1 v6.4s, v1.4s, v3.4s
///    zip2 v7.4s, v1.4s, v3.4s
///    zip1 v8.4s, v4.4s, v6.4s
///    zip2 v9.4s, v4.4s, v6.4s
///    zip1 v10.4s, v5.4s, v7.4s
///    zip2 v11.4s, v5.4s, v7.4s
///    stp  q8, q9, addr
///    stp  q10, q11, addr+32
///
/// Currently only instructions related to ST2 and ST4 are considered.
/// Other may be added later.
/// Return true if the SIMD instruction is modified.
bool AArch64SIMDInstrOpt::optimizeLdStInterleave(MachineInstr &MI) {

  unsigned SeqReg, AddrReg;
  unsigned StReg[4], StRegKill[4];
  MachineInstr *DefiningMI;
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  SmallVector<unsigned, MaxNumRepl> ZipDest;
  SmallVector<const MCInstrDesc*, MaxNumRepl> ReplInstrMCID;

  // If current instruction matches any of the rewriting rules, then
  // gather information about parameters of the new instructions.
  bool Match = false;
  for (auto &I : IRT) {
    if (MI.getOpcode() == I.OrigOpc) {
      SeqReg  = MI.getOperand(0).getReg();
      AddrReg = MI.getOperand(1).getReg();
      DefiningMI = MRI->getUniqueVRegDef(SeqReg);
      unsigned NumReg = determineSrcReg(MI);
      if (!processSeqRegInst(DefiningMI, StReg, StRegKill, NumReg))
        return false;

      for (auto &Repl : I.ReplOpc) {
        ReplInstrMCID.push_back(&TII->get(Repl));
        // Generate destination registers but only for non-store instruction.
        if (Repl != AArch64::STPQi && Repl != AArch64::STPDi)
          ZipDest.push_back(MRI->createVirtualRegister(&I.RC));
      }
      Match = true;
      break;
    }
  }

  if (!Match)
    return false;

  // Determine if it is profitable to replace MI by the series of instructions
  // represented in ReplInstrMCID.
  if (!shouldReplaceInst(MI.getParent()->getParent(), &TII->get(MI.getOpcode()),
                         ReplInstrMCID))
    return false;

  // Generate the replacement instructions composed of ZIP1, ZIP2, and STP (at
  // this point, the code generation is hardcoded and does not rely on the IRT
  // table used above given that code generation for ST2 replacement is somewhat
  // different than for ST4 replacement. We could have added more info into the
  // table related to how we build new instructions but we may be adding more
  // complexity with that).
  switch (MI.getOpcode()) {
  default:
    return false;

  case AArch64::ST2Twov16b:
  case AArch64::ST2Twov8b:
  case AArch64::ST2Twov8h:
  case AArch64::ST2Twov4h:
  case AArch64::ST2Twov4s:
  case AArch64::ST2Twov2s:
  case AArch64::ST2Twov2d:
    // ZIP instructions
    BuildMI(MBB, MI, DL, *ReplInstrMCID[0], ZipDest[0])
        .addReg(StReg[0])
        .addReg(StReg[1]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[1], ZipDest[1])
        .addReg(StReg[0], StRegKill[0])
        .addReg(StReg[1], StRegKill[1]);
    // STP instructions
    BuildMI(MBB, MI, DL, *ReplInstrMCID[2])
        .addReg(ZipDest[0])
        .addReg(ZipDest[1])
        .addReg(AddrReg)
        .addImm(0);
    break;

  case AArch64::ST4Fourv16b:
  case AArch64::ST4Fourv8b:
  case AArch64::ST4Fourv8h:
  case AArch64::ST4Fourv4h:
  case AArch64::ST4Fourv4s:
  case AArch64::ST4Fourv2s:
  case AArch64::ST4Fourv2d:
    // ZIP instructions
    BuildMI(MBB, MI, DL, *ReplInstrMCID[0], ZipDest[0])
        .addReg(StReg[0])
        .addReg(StReg[2]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[1], ZipDest[1])
        .addReg(StReg[0], StRegKill[0])
        .addReg(StReg[2], StRegKill[2]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[2], ZipDest[2])
        .addReg(StReg[1])
        .addReg(StReg[3]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[3], ZipDest[3])
        .addReg(StReg[1], StRegKill[1])
        .addReg(StReg[3], StRegKill[3]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[4], ZipDest[4])
        .addReg(ZipDest[0])
        .addReg(ZipDest[2]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[5], ZipDest[5])
        .addReg(ZipDest[0])
        .addReg(ZipDest[2]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[6], ZipDest[6])
        .addReg(ZipDest[1])
        .addReg(ZipDest[3]);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[7], ZipDest[7])
        .addReg(ZipDest[1])
        .addReg(ZipDest[3]);
    // stp instructions
    BuildMI(MBB, MI, DL, *ReplInstrMCID[8])
        .addReg(ZipDest[4])
        .addReg(ZipDest[5])
        .addReg(AddrReg)
        .addImm(0);
    BuildMI(MBB, MI, DL, *ReplInstrMCID[9])
        .addReg(ZipDest[6])
        .addReg(ZipDest[7])
        .addReg(AddrReg)
        .addImm(2);
    break;
  }

  ++NumModifiedInstr;
  return true;
}

/// Process The REG_SEQUENCE instruction, and extract the source
/// operands of the ST2/4 instruction from it.
/// Example of such instruction.
///    %dest = REG_SEQUENCE %st2_src1, dsub0, %st2_src2, dsub1;
/// Return true when the instruction is processed successfully.
bool AArch64SIMDInstrOpt::processSeqRegInst(MachineInstr *DefiningMI,
     unsigned* StReg, unsigned* StRegKill, unsigned NumArg) const {
  assert (DefiningMI != NULL);
  if (DefiningMI->getOpcode() != AArch64::REG_SEQUENCE)
    return false;

  for (unsigned i=0; i<NumArg; i++) {
    StReg[i]     = DefiningMI->getOperand(2*i+1).getReg();
    StRegKill[i] = getKillRegState(DefiningMI->getOperand(2*i+1).isKill());

    // Sanity check for the other arguments.
    if (DefiningMI->getOperand(2*i+2).isImm()) {
      switch (DefiningMI->getOperand(2*i+2).getImm()) {
      default:
        return false;

      case AArch64::dsub0:
      case AArch64::dsub1:
      case AArch64::dsub2:
      case AArch64::dsub3:
      case AArch64::qsub0:
      case AArch64::qsub1:
      case AArch64::qsub2:
      case AArch64::qsub3:
        break;
      }
    }
    else
      return false;
  }
  return true;
}

/// Return the number of useful source registers for this instruction
/// (2 for ST2 and 4 for ST4).
unsigned AArch64SIMDInstrOpt::determineSrcReg(MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unsupported instruction for this pass");

  case AArch64::ST2Twov16b:
  case AArch64::ST2Twov8b:
  case AArch64::ST2Twov8h:
  case AArch64::ST2Twov4h:
  case AArch64::ST2Twov4s:
  case AArch64::ST2Twov2s:
  case AArch64::ST2Twov2d:
    return 2;

  case AArch64::ST4Fourv16b:
  case AArch64::ST4Fourv8b:
  case AArch64::ST4Fourv8h:
  case AArch64::ST4Fourv4h:
  case AArch64::ST4Fourv4s:
  case AArch64::ST4Fourv2s:
  case AArch64::ST4Fourv2d:
    return 4;
  }
}

bool AArch64SIMDInstrOpt::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();
  const TargetSubtargetInfo &ST = MF.getSubtarget();
  const AArch64InstrInfo *AAII =
      static_cast<const AArch64InstrInfo *>(ST.getInstrInfo());
  if (!AAII)
    return false;
  SchedModel.init(&ST);
  if (!SchedModel.hasInstrSchedModel())
    return false;

  bool Changed = false;
  for (auto OptimizationKind : {VectorElem, Interleave}) {
    if (!shouldExitEarly(&MF, OptimizationKind)) {
      SmallVector<MachineInstr *, 8> RemoveMIs;
      for (MachineBasicBlock &MBB : MF) {
        for (MachineBasicBlock::iterator MII = MBB.begin(), MIE = MBB.end();
             MII != MIE;) {
          MachineInstr &MI = *MII;
          bool InstRewrite;
          if (OptimizationKind == VectorElem)
            InstRewrite = optimizeVectElement(MI) ;
          else
            InstRewrite = optimizeLdStInterleave(MI);
          if (InstRewrite) {
            // Add MI to the list of instructions to be removed given that it
            // has been replaced.
            RemoveMIs.push_back(&MI);
            Changed = true;
          }
          ++MII;
        }
      }
      for (MachineInstr *MI : RemoveMIs)
        MI->eraseFromParent();
    }
  }

  return Changed;
}

/// Returns an instance of the high cost ASIMD instruction replacement
/// optimization pass.
FunctionPass *llvm::createAArch64SIMDInstrOptPass() {
  return new AArch64SIMDInstrOpt();
}
