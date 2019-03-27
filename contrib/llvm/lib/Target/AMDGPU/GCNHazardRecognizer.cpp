//===-- GCNHazardRecognizers.cpp - GCN Hazard Recognizer Impls ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements hazard recognizers for scheduling on GCN processors.
//
//===----------------------------------------------------------------------===//

#include "GCNHazardRecognizer.h"
#include "AMDGPUSubtarget.h"
#include "SIDefines.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <set>
#include <vector>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Hazard Recoginizer Implementation
//===----------------------------------------------------------------------===//

GCNHazardRecognizer::GCNHazardRecognizer(const MachineFunction &MF) :
  CurrCycleInstr(nullptr),
  MF(MF),
  ST(MF.getSubtarget<GCNSubtarget>()),
  TII(*ST.getInstrInfo()),
  TRI(TII.getRegisterInfo()),
  ClauseUses(TRI.getNumRegUnits()),
  ClauseDefs(TRI.getNumRegUnits()) {
  MaxLookAhead = 5;
}

void GCNHazardRecognizer::EmitInstruction(SUnit *SU) {
  EmitInstruction(SU->getInstr());
}

void GCNHazardRecognizer::EmitInstruction(MachineInstr *MI) {
  CurrCycleInstr = MI;
}

static bool isDivFMas(unsigned Opcode) {
  return Opcode == AMDGPU::V_DIV_FMAS_F32 || Opcode == AMDGPU::V_DIV_FMAS_F64;
}

static bool isSGetReg(unsigned Opcode) {
  return Opcode == AMDGPU::S_GETREG_B32;
}

static bool isSSetReg(unsigned Opcode) {
  return Opcode == AMDGPU::S_SETREG_B32 || Opcode == AMDGPU::S_SETREG_IMM32_B32;
}

static bool isRWLane(unsigned Opcode) {
  return Opcode == AMDGPU::V_READLANE_B32 || Opcode == AMDGPU::V_WRITELANE_B32;
}

static bool isRFE(unsigned Opcode) {
  return Opcode == AMDGPU::S_RFE_B64;
}

static bool isSMovRel(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_MOVRELS_B32:
  case AMDGPU::S_MOVRELS_B64:
  case AMDGPU::S_MOVRELD_B32:
  case AMDGPU::S_MOVRELD_B64:
    return true;
  default:
    return false;
  }
}

static bool isSendMsgTraceDataOrGDS(const SIInstrInfo &TII,
                                    const MachineInstr &MI) {
  if (TII.isAlwaysGDS(MI.getOpcode()))
    return true;

  switch (MI.getOpcode()) {
  case AMDGPU::S_SENDMSG:
  case AMDGPU::S_SENDMSGHALT:
  case AMDGPU::S_TTRACEDATA:
    return true;
  // These DS opcodes don't support GDS.
  case AMDGPU::DS_NOP:
  case AMDGPU::DS_PERMUTE_B32:
  case AMDGPU::DS_BPERMUTE_B32:
    return false;
  default:
    if (TII.isDS(MI.getOpcode())) {
      int GDS = AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                           AMDGPU::OpName::gds);
      if (MI.getOperand(GDS).getImm())
        return true;
    }
    return false;
  }
}

static unsigned getHWReg(const SIInstrInfo *TII, const MachineInstr &RegInstr) {
  const MachineOperand *RegOp = TII->getNamedOperand(RegInstr,
                                                     AMDGPU::OpName::simm16);
  return RegOp->getImm() & AMDGPU::Hwreg::ID_MASK_;
}

ScheduleHazardRecognizer::HazardType
GCNHazardRecognizer::getHazardType(SUnit *SU, int Stalls) {
  MachineInstr *MI = SU->getInstr();

  if (SIInstrInfo::isSMRD(*MI) && checkSMRDHazards(MI) > 0)
    return NoopHazard;

  // FIXME: Should flat be considered vmem?
  if ((SIInstrInfo::isVMEM(*MI) ||
       SIInstrInfo::isFLAT(*MI))
      && checkVMEMHazards(MI) > 0)
    return NoopHazard;

  if (SIInstrInfo::isVALU(*MI) && checkVALUHazards(MI) > 0)
    return NoopHazard;

  if (SIInstrInfo::isDPP(*MI) && checkDPPHazards(MI) > 0)
    return NoopHazard;

  if (isDivFMas(MI->getOpcode()) && checkDivFMasHazards(MI) > 0)
    return NoopHazard;

  if (isRWLane(MI->getOpcode()) && checkRWLaneHazards(MI) > 0)
    return NoopHazard;

  if (isSGetReg(MI->getOpcode()) && checkGetRegHazards(MI) > 0)
    return NoopHazard;

  if (isSSetReg(MI->getOpcode()) && checkSetRegHazards(MI) > 0)
    return NoopHazard;

  if (isRFE(MI->getOpcode()) && checkRFEHazards(MI) > 0)
    return NoopHazard;

  if (ST.hasReadM0MovRelInterpHazard() &&
      (TII.isVINTRP(*MI) || isSMovRel(MI->getOpcode())) &&
      checkReadM0Hazards(MI) > 0)
    return NoopHazard;

  if (ST.hasReadM0SendMsgHazard() && isSendMsgTraceDataOrGDS(TII, *MI) &&
      checkReadM0Hazards(MI) > 0)
    return NoopHazard;

  if (MI->isInlineAsm() && checkInlineAsmHazards(MI) > 0)
    return NoopHazard;

  if (checkAnyInstHazards(MI) > 0)
    return NoopHazard;

  return NoHazard;
}

unsigned GCNHazardRecognizer::PreEmitNoops(SUnit *SU) {
  return PreEmitNoops(SU->getInstr());
}

unsigned GCNHazardRecognizer::PreEmitNoops(MachineInstr *MI) {
  int WaitStates = std::max(0, checkAnyInstHazards(MI));

  if (SIInstrInfo::isSMRD(*MI))
    return std::max(WaitStates, checkSMRDHazards(MI));

  if (SIInstrInfo::isVALU(*MI))
    WaitStates = std::max(WaitStates, checkVALUHazards(MI));

  if (SIInstrInfo::isVMEM(*MI) || SIInstrInfo::isFLAT(*MI))
    WaitStates = std::max(WaitStates, checkVMEMHazards(MI));

  if (SIInstrInfo::isDPP(*MI))
    WaitStates = std::max(WaitStates, checkDPPHazards(MI));

  if (isDivFMas(MI->getOpcode()))
    WaitStates = std::max(WaitStates, checkDivFMasHazards(MI));

  if (isRWLane(MI->getOpcode()))
    WaitStates = std::max(WaitStates, checkRWLaneHazards(MI));

  if (MI->isInlineAsm())
    return std::max(WaitStates, checkInlineAsmHazards(MI));

  if (isSGetReg(MI->getOpcode()))
    return std::max(WaitStates, checkGetRegHazards(MI));

  if (isSSetReg(MI->getOpcode()))
    return std::max(WaitStates, checkSetRegHazards(MI));

  if (isRFE(MI->getOpcode()))
    return std::max(WaitStates, checkRFEHazards(MI));

  if (ST.hasReadM0MovRelInterpHazard() && (TII.isVINTRP(*MI) ||
                                           isSMovRel(MI->getOpcode())))
    return std::max(WaitStates, checkReadM0Hazards(MI));

  if (ST.hasReadM0SendMsgHazard() && isSendMsgTraceDataOrGDS(TII, *MI))
    return std::max(WaitStates, checkReadM0Hazards(MI));

  return WaitStates;
}

void GCNHazardRecognizer::EmitNoop() {
  EmittedInstrs.push_front(nullptr);
}

void GCNHazardRecognizer::AdvanceCycle() {
  // When the scheduler detects a stall, it will call AdvanceCycle() without
  // emitting any instructions.
  if (!CurrCycleInstr)
    return;

  // Do not track non-instructions which do not affect the wait states.
  // If included, these instructions can lead to buffer overflow such that
  // detectable hazards are missed.
  if (CurrCycleInstr->getOpcode() == AMDGPU::IMPLICIT_DEF)
    return;
  else if (CurrCycleInstr->isDebugInstr())
    return;

  unsigned NumWaitStates = TII.getNumWaitStates(*CurrCycleInstr);

  // Keep track of emitted instructions
  EmittedInstrs.push_front(CurrCycleInstr);

  // Add a nullptr for each additional wait state after the first.  Make sure
  // not to add more than getMaxLookAhead() items to the list, since we
  // truncate the list to that size right after this loop.
  for (unsigned i = 1, e = std::min(NumWaitStates, getMaxLookAhead());
       i < e; ++i) {
    EmittedInstrs.push_front(nullptr);
  }

  // getMaxLookahead() is the largest number of wait states we will ever need
  // to insert, so there is no point in keeping track of more than that many
  // wait states.
  EmittedInstrs.resize(getMaxLookAhead());

  CurrCycleInstr = nullptr;
}

void GCNHazardRecognizer::RecedeCycle() {
  llvm_unreachable("hazard recognizer does not support bottom-up scheduling.");
}

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

int GCNHazardRecognizer::getWaitStatesSince(
    function_ref<bool(MachineInstr *)> IsHazard) {
  int WaitStates = 0;
  for (MachineInstr *MI : EmittedInstrs) {
    if (MI) {
      if (IsHazard(MI))
        return WaitStates;

      unsigned Opcode = MI->getOpcode();
      if (Opcode == AMDGPU::INLINEASM)
        continue;
    }
    ++WaitStates;
  }
  return std::numeric_limits<int>::max();
}

int GCNHazardRecognizer::getWaitStatesSinceDef(
    unsigned Reg, function_ref<bool(MachineInstr *)> IsHazardDef) {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  auto IsHazardFn = [IsHazardDef, TRI, Reg] (MachineInstr *MI) {
    return IsHazardDef(MI) && MI->modifiesRegister(Reg, TRI);
  };

  return getWaitStatesSince(IsHazardFn);
}

int GCNHazardRecognizer::getWaitStatesSinceSetReg(
    function_ref<bool(MachineInstr *)> IsHazard) {
  auto IsHazardFn = [IsHazard] (MachineInstr *MI) {
    return isSSetReg(MI->getOpcode()) && IsHazard(MI);
  };

  return getWaitStatesSince(IsHazardFn);
}

//===----------------------------------------------------------------------===//
// No-op Hazard Detection
//===----------------------------------------------------------------------===//

static void addRegUnits(const SIRegisterInfo &TRI,
                        BitVector &BV, unsigned Reg) {
  for (MCRegUnitIterator RUI(Reg, &TRI); RUI.isValid(); ++RUI)
    BV.set(*RUI);
}

static void addRegsToSet(const SIRegisterInfo &TRI,
                         iterator_range<MachineInstr::const_mop_iterator> Ops,
                         BitVector &Set) {
  for (const MachineOperand &Op : Ops) {
    if (Op.isReg())
      addRegUnits(TRI, Set, Op.getReg());
  }
}

void GCNHazardRecognizer::addClauseInst(const MachineInstr &MI) {
  // XXX: Do we need to worry about implicit operands
  addRegsToSet(TRI, MI.defs(), ClauseDefs);
  addRegsToSet(TRI, MI.uses(), ClauseUses);
}

int GCNHazardRecognizer::checkSoftClauseHazards(MachineInstr *MEM) {
  // SMEM soft clause are only present on VI+, and only matter if xnack is
  // enabled.
  if (!ST.isXNACKEnabled())
    return 0;

  bool IsSMRD = TII.isSMRD(*MEM);

  resetClause();

  // A soft-clause is any group of consecutive SMEM instructions.  The
  // instructions in this group may return out of order and/or may be
  // replayed (i.e. the same instruction issued more than once).
  //
  // In order to handle these situations correctly we need to make sure
  // that when a clause has more than one instruction, no instruction in the
  // clause writes to a register that is read another instruction in the clause
  // (including itself). If we encounter this situaion, we need to break the
  // clause by inserting a non SMEM instruction.

  for (MachineInstr *MI : EmittedInstrs) {
    // When we hit a non-SMEM instruction then we have passed the start of the
    // clause and we can stop.
    if (!MI)
      break;

    if (IsSMRD != SIInstrInfo::isSMRD(*MI))
      break;

    addClauseInst(*MI);
  }

  if (ClauseDefs.none())
    return 0;

  // We need to make sure not to put loads and stores in the same clause if they
  // use the same address. For now, just start a new clause whenever we see a
  // store.
  if (MEM->mayStore())
    return 1;

  addClauseInst(*MEM);

  // If the set of defs and uses intersect then we cannot add this instruction
  // to the clause, so we have a hazard.
  return ClauseDefs.anyCommon(ClauseUses) ? 1 : 0;
}

int GCNHazardRecognizer::checkSMRDHazards(MachineInstr *SMRD) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  int WaitStatesNeeded = 0;

  WaitStatesNeeded = checkSoftClauseHazards(SMRD);

  // This SMRD hazard only affects SI.
  if (ST.getGeneration() != AMDGPUSubtarget::SOUTHERN_ISLANDS)
    return WaitStatesNeeded;

  // A read of an SGPR by SMRD instruction requires 4 wait states when the
  // SGPR was written by a VALU instruction.
  int SmrdSgprWaitStates = 4;
  auto IsHazardDefFn = [this] (MachineInstr *MI) { return TII.isVALU(*MI); };
  auto IsBufferHazardDefFn = [this] (MachineInstr *MI) { return TII.isSALU(*MI); };

  bool IsBufferSMRD = TII.isBufferSMRD(*SMRD);

  for (const MachineOperand &Use : SMRD->uses()) {
    if (!Use.isReg())
      continue;
    int WaitStatesNeededForUse =
        SmrdSgprWaitStates - getWaitStatesSinceDef(Use.getReg(), IsHazardDefFn);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);

    // This fixes what appears to be undocumented hardware behavior in SI where
    // s_mov writing a descriptor and s_buffer_load_dword reading the descriptor
    // needs some number of nops in between. We don't know how many we need, but
    // let's use 4. This wasn't discovered before probably because the only
    // case when this happens is when we expand a 64-bit pointer into a full
    // descriptor and use s_buffer_load_dword instead of s_load_dword, which was
    // probably never encountered in the closed-source land.
    if (IsBufferSMRD) {
      int WaitStatesNeededForUse =
        SmrdSgprWaitStates - getWaitStatesSinceDef(Use.getReg(),
                                                   IsBufferHazardDefFn);
      WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
    }
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkVMEMHazards(MachineInstr* VMEM) {
  if (ST.getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS)
    return 0;

  int WaitStatesNeeded = checkSoftClauseHazards(VMEM);

  // A read of an SGPR by a VMEM instruction requires 5 wait states when the
  // SGPR was written by a VALU Instruction.
  const int VmemSgprWaitStates = 5;
  auto IsHazardDefFn = [this] (MachineInstr *MI) { return TII.isVALU(*MI); };

  for (const MachineOperand &Use : VMEM->uses()) {
    if (!Use.isReg() || TRI.isVGPR(MF.getRegInfo(), Use.getReg()))
      continue;

    int WaitStatesNeededForUse =
        VmemSgprWaitStates - getWaitStatesSinceDef(Use.getReg(), IsHazardDefFn);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }
  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkDPPHazards(MachineInstr *DPP) {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Check for DPP VGPR read after VALU VGPR write and EXEC write.
  int DppVgprWaitStates = 2;
  int DppExecWaitStates = 5;
  int WaitStatesNeeded = 0;
  auto IsHazardDefFn = [TII] (MachineInstr *MI) { return TII->isVALU(*MI); };

  for (const MachineOperand &Use : DPP->uses()) {
    if (!Use.isReg() || !TRI->isVGPR(MF.getRegInfo(), Use.getReg()))
      continue;
    int WaitStatesNeededForUse =
        DppVgprWaitStates - getWaitStatesSinceDef(Use.getReg());
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  WaitStatesNeeded = std::max(
      WaitStatesNeeded,
      DppExecWaitStates - getWaitStatesSinceDef(AMDGPU::EXEC, IsHazardDefFn));

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkDivFMasHazards(MachineInstr *DivFMas) {
  const SIInstrInfo *TII = ST.getInstrInfo();

  // v_div_fmas requires 4 wait states after a write to vcc from a VALU
  // instruction.
  const int DivFMasWaitStates = 4;
  auto IsHazardDefFn = [TII] (MachineInstr *MI) { return TII->isVALU(*MI); };
  int WaitStatesNeeded = getWaitStatesSinceDef(AMDGPU::VCC, IsHazardDefFn);

  return DivFMasWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkGetRegHazards(MachineInstr *GetRegInstr) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned GetRegHWReg = getHWReg(TII, *GetRegInstr);

  const int GetRegWaitStates = 2;
  auto IsHazardFn = [TII, GetRegHWReg] (MachineInstr *MI) {
    return GetRegHWReg == getHWReg(TII, *MI);
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn);

  return GetRegWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkSetRegHazards(MachineInstr *SetRegInstr) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned HWReg = getHWReg(TII, *SetRegInstr);

  const int SetRegWaitStates =
      ST.getGeneration() <= AMDGPUSubtarget::SEA_ISLANDS ? 1 : 2;
  auto IsHazardFn = [TII, HWReg] (MachineInstr *MI) {
    return HWReg == getHWReg(TII, *MI);
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn);
  return SetRegWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::createsVALUHazard(const MachineInstr &MI) {
  if (!MI.mayStore())
    return -1;

  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();

  int VDataIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdata);
  int VDataRCID = -1;
  if (VDataIdx != -1)
    VDataRCID = Desc.OpInfo[VDataIdx].RegClass;

  if (TII->isMUBUF(MI) || TII->isMTBUF(MI)) {
    // There is no hazard if the instruction does not use vector regs
    // (like wbinvl1)
    if (VDataIdx == -1)
      return -1;
    // For MUBUF/MTBUF instructions this hazard only exists if the
    // instruction is not using a register in the soffset field.
    const MachineOperand *SOffset =
        TII->getNamedOperand(MI, AMDGPU::OpName::soffset);
    // If we have no soffset operand, then assume this field has been
    // hardcoded to zero.
    if (AMDGPU::getRegBitWidth(VDataRCID) > 64 &&
        (!SOffset || !SOffset->isReg()))
      return VDataIdx;
  }

  // MIMG instructions create a hazard if they don't use a 256-bit T# and
  // the store size is greater than 8 bytes and they have more than two bits
  // of their dmask set.
  // All our MIMG definitions use a 256-bit T#, so we can skip checking for them.
  if (TII->isMIMG(MI)) {
    int SRsrcIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::srsrc);
    assert(SRsrcIdx != -1 &&
           AMDGPU::getRegBitWidth(Desc.OpInfo[SRsrcIdx].RegClass) == 256);
    (void)SRsrcIdx;
  }

  if (TII->isFLAT(MI)) {
    int DataIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::vdata);
    if (AMDGPU::getRegBitWidth(Desc.OpInfo[DataIdx].RegClass) > 64)
      return DataIdx;
  }

  return -1;
}

int GCNHazardRecognizer::checkVALUHazardsHelper(const MachineOperand &Def,
						const MachineRegisterInfo &MRI) {
  // Helper to check for the hazard where VMEM instructions that store more than
  // 8 bytes can have there store data over written by the next instruction.
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  const int VALUWaitStates = 1;
  int WaitStatesNeeded = 0;

  if (!TRI->isVGPR(MRI, Def.getReg()))
    return WaitStatesNeeded;
  unsigned Reg = Def.getReg();
  auto IsHazardFn = [this, Reg, TRI] (MachineInstr *MI) {
    int DataIdx = createsVALUHazard(*MI);
    return DataIdx >= 0 &&
    TRI->regsOverlap(MI->getOperand(DataIdx).getReg(), Reg);
  };
  int WaitStatesNeededForDef =
    VALUWaitStates - getWaitStatesSince(IsHazardFn);
  WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForDef);

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkVALUHazards(MachineInstr *VALU) {
  // This checks for the hazard where VMEM instructions that store more than
  // 8 bytes can have there store data over written by the next instruction.
  if (!ST.has12DWordStoreHazard())
    return 0;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  int WaitStatesNeeded = 0;

  for (const MachineOperand &Def : VALU->defs()) {
    WaitStatesNeeded = std::max(WaitStatesNeeded, checkVALUHazardsHelper(Def, MRI));
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkInlineAsmHazards(MachineInstr *IA) {
  // This checks for hazards associated with inline asm statements.
  // Since inline asms can contain just about anything, we use this
  // to call/leverage other check*Hazard routines. Note that
  // this function doesn't attempt to address all possible inline asm
  // hazards (good luck), but is a collection of what has been
  // problematic thus far.

  // see checkVALUHazards()
  if (!ST.has12DWordStoreHazard())
    return 0;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  int WaitStatesNeeded = 0;

  for (unsigned I = InlineAsm::MIOp_FirstOperand, E = IA->getNumOperands();
       I != E; ++I) {
    const MachineOperand &Op = IA->getOperand(I);
    if (Op.isReg() && Op.isDef()) {
      WaitStatesNeeded = std::max(WaitStatesNeeded, checkVALUHazardsHelper(Op, MRI));
    }
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkRWLaneHazards(MachineInstr *RWLane) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  const MachineOperand *LaneSelectOp =
      TII->getNamedOperand(*RWLane, AMDGPU::OpName::src1);

  if (!LaneSelectOp->isReg() || !TRI->isSGPRReg(MRI, LaneSelectOp->getReg()))
    return 0;

  unsigned LaneSelectReg = LaneSelectOp->getReg();
  auto IsHazardFn = [TII] (MachineInstr *MI) {
    return TII->isVALU(*MI);
  };

  const int RWLaneWaitStates = 4;
  int WaitStatesSince = getWaitStatesSinceDef(LaneSelectReg, IsHazardFn);
  return RWLaneWaitStates - WaitStatesSince;
}

int GCNHazardRecognizer::checkRFEHazards(MachineInstr *RFE) {
  if (ST.getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS)
    return 0;

  const SIInstrInfo *TII = ST.getInstrInfo();

  const int RFEWaitStates = 1;

  auto IsHazardFn = [TII] (MachineInstr *MI) {
    return getHWReg(TII, *MI) == AMDGPU::Hwreg::ID_TRAPSTS;
  };
  int WaitStatesNeeded = getWaitStatesSinceSetReg(IsHazardFn);
  return RFEWaitStates - WaitStatesNeeded;
}

int GCNHazardRecognizer::checkAnyInstHazards(MachineInstr *MI) {
  if (MI->isDebugInstr())
    return 0;

  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  if (!ST.hasSMovFedHazard())
    return 0;

  // Check for any instruction reading an SGPR after a write from
  // s_mov_fed_b32.
  int MovFedWaitStates = 1;
  int WaitStatesNeeded = 0;

  for (const MachineOperand &Use : MI->uses()) {
    if (!Use.isReg() || TRI->isVGPR(MF.getRegInfo(), Use.getReg()))
      continue;
    auto IsHazardFn = [] (MachineInstr *MI) {
      return MI->getOpcode() == AMDGPU::S_MOV_FED_B32;
    };
    int WaitStatesNeededForUse =
        MovFedWaitStates - getWaitStatesSinceDef(Use.getReg(), IsHazardFn);
    WaitStatesNeeded = std::max(WaitStatesNeeded, WaitStatesNeededForUse);
  }

  return WaitStatesNeeded;
}

int GCNHazardRecognizer::checkReadM0Hazards(MachineInstr *MI) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const int SMovRelWaitStates = 1;
  auto IsHazardFn = [TII] (MachineInstr *MI) {
    return TII->isSALU(*MI);
  };
  return SMovRelWaitStates - getWaitStatesSinceDef(AMDGPU::M0, IsHazardFn);
}
