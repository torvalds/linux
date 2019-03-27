//===-- X86FixupLEAs.cpp - use or replace LEA instructions -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the pass that finds instructions that can be
// re-written as LEA instructions in order to reduce pipeline delays.
// When optimizing for size it replaces suitable LEAs with INC or DEC.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define FIXUPLEA_DESC "X86 LEA Fixup"
#define FIXUPLEA_NAME "x86-fixup-LEAs"

#define DEBUG_TYPE FIXUPLEA_NAME

STATISTIC(NumLEAs, "Number of LEA instructions created");

namespace {
class FixupLEAPass : public MachineFunctionPass {
  enum RegUsageState { RU_NotUsed, RU_Write, RU_Read };

  /// Loop over all of the instructions in the basic block
  /// replacing applicable instructions with LEA instructions,
  /// where appropriate.
  bool processBasicBlock(MachineFunction &MF, MachineFunction::iterator MFI,
                         bool IsSlowLEA, bool IsSlow3OpsLEA);

  /// Given a machine register, look for the instruction
  /// which writes it in the current basic block. If found,
  /// try to replace it with an equivalent LEA instruction.
  /// If replacement succeeds, then also process the newly created
  /// instruction.
  void seekLEAFixup(MachineOperand &p, MachineBasicBlock::iterator &I,
                    MachineFunction::iterator MFI);

  /// Given a memory access or LEA instruction
  /// whose address mode uses a base and/or index register, look for
  /// an opportunity to replace the instruction which sets the base or index
  /// register with an equivalent LEA instruction.
  void processInstruction(MachineBasicBlock::iterator &I,
                          MachineFunction::iterator MFI);

  /// Given a LEA instruction which is unprofitable
  /// on SlowLEA targets try to replace it with an equivalent ADD instruction.
  void processInstructionForSlowLEA(MachineBasicBlock::iterator &I,
                                    MachineFunction::iterator MFI);

  /// Given a LEA instruction which is unprofitable
  /// on SNB+ try to replace it with other instructions.
  /// According to Intel's Optimization Reference Manual:
  /// " For LEA instructions with three source operands and some specific
  ///   situations, instruction latency has increased to 3 cycles, and must
  ///   dispatch via port 1:
  /// - LEA that has all three source operands: base, index, and offset
  /// - LEA that uses base and index registers where the base is EBP, RBP,
  ///   or R13
  /// - LEA that uses RIP relative addressing mode
  /// - LEA that uses 16-bit addressing mode "
  /// This function currently handles the first 2 cases only.
  MachineInstr *processInstrForSlow3OpLEA(MachineInstr &MI,
                                          MachineFunction::iterator MFI);

  /// Look for LEAs that add 1 to reg or subtract 1 from reg
  /// and convert them to INC or DEC respectively.
  bool fixupIncDec(MachineBasicBlock::iterator &I,
                   MachineFunction::iterator MFI) const;

  /// Determine if an instruction references a machine register
  /// and, if so, whether it reads or writes the register.
  RegUsageState usesRegister(MachineOperand &p, MachineBasicBlock::iterator I);

  /// Step backwards through a basic block, looking
  /// for an instruction which writes a register within
  /// a maximum of INSTR_DISTANCE_THRESHOLD instruction latency cycles.
  MachineBasicBlock::iterator searchBackwards(MachineOperand &p,
                                              MachineBasicBlock::iterator &I,
                                              MachineFunction::iterator MFI);

  /// if an instruction can be converted to an
  /// equivalent LEA, insert the new instruction into the basic block
  /// and return a pointer to it. Otherwise, return zero.
  MachineInstr *postRAConvertToLEA(MachineFunction::iterator &MFI,
                                   MachineBasicBlock::iterator &MBBI) const;

public:
  static char ID;

  StringRef getPassName() const override { return FIXUPLEA_DESC; }

  FixupLEAPass() : MachineFunctionPass(ID) {
    initializeFixupLEAPassPass(*PassRegistry::getPassRegistry());
  }

  /// Loop over all of the basic blocks,
  /// replacing instructions by equivalent LEA instructions
  /// if needed and when possible.
  bool runOnMachineFunction(MachineFunction &MF) override;

  // This pass runs after regalloc and doesn't support VReg operands.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  TargetSchedModel TSM;
  MachineFunction *MF;
  const X86InstrInfo *TII; // Machine instruction info.
  bool OptIncDec;
  bool OptLEA;
};
}

char FixupLEAPass::ID = 0;

INITIALIZE_PASS(FixupLEAPass, FIXUPLEA_NAME, FIXUPLEA_DESC, false, false)

MachineInstr *
FixupLEAPass::postRAConvertToLEA(MachineFunction::iterator &MFI,
                                 MachineBasicBlock::iterator &MBBI) const {
  MachineInstr &MI = *MBBI;
  switch (MI.getOpcode()) {
  case X86::MOV32rr:
  case X86::MOV64rr: {
    const MachineOperand &Src = MI.getOperand(1);
    const MachineOperand &Dest = MI.getOperand(0);
    MachineInstr *NewMI =
        BuildMI(*MF, MI.getDebugLoc(),
                TII->get(MI.getOpcode() == X86::MOV32rr ? X86::LEA32r
                                                        : X86::LEA64r))
            .add(Dest)
            .add(Src)
            .addImm(1)
            .addReg(0)
            .addImm(0)
            .addReg(0);
    MFI->insert(MBBI, NewMI); // Insert the new inst
    return NewMI;
  }
  case X86::ADD64ri32:
  case X86::ADD64ri8:
  case X86::ADD64ri32_DB:
  case X86::ADD64ri8_DB:
  case X86::ADD32ri:
  case X86::ADD32ri8:
  case X86::ADD32ri_DB:
  case X86::ADD32ri8_DB:
  case X86::ADD16ri:
  case X86::ADD16ri8:
  case X86::ADD16ri_DB:
  case X86::ADD16ri8_DB:
    if (!MI.getOperand(2).isImm()) {
      // convertToThreeAddress will call getImm()
      // which requires isImm() to be true
      return nullptr;
    }
    break;
  case X86::ADD16rr:
  case X86::ADD16rr_DB:
    if (MI.getOperand(1).getReg() != MI.getOperand(2).getReg()) {
      // if src1 != src2, then convertToThreeAddress will
      // need to create a Virtual register, which we cannot do
      // after register allocation.
      return nullptr;
    }
  }
  return TII->convertToThreeAddress(MFI, MI, nullptr);
}

FunctionPass *llvm::createX86FixupLEAs() { return new FixupLEAPass(); }

bool FixupLEAPass::runOnMachineFunction(MachineFunction &Func) {
  if (skipFunction(Func.getFunction()))
    return false;

  MF = &Func;
  const X86Subtarget &ST = Func.getSubtarget<X86Subtarget>();
  bool IsSlowLEA = ST.slowLEA();
  bool IsSlow3OpsLEA = ST.slow3OpsLEA();

  OptIncDec = !ST.slowIncDec() || Func.getFunction().optForMinSize();
  OptLEA = ST.LEAusesAG() || IsSlowLEA || IsSlow3OpsLEA;

  if (!OptLEA && !OptIncDec)
    return false;

  TSM.init(&Func.getSubtarget());
  TII = ST.getInstrInfo();

  LLVM_DEBUG(dbgs() << "Start X86FixupLEAs\n";);
  // Process all basic blocks.
  for (MachineFunction::iterator I = Func.begin(), E = Func.end(); I != E; ++I)
    processBasicBlock(Func, I, IsSlowLEA, IsSlow3OpsLEA);
  LLVM_DEBUG(dbgs() << "End X86FixupLEAs\n";);

  return true;
}

FixupLEAPass::RegUsageState
FixupLEAPass::usesRegister(MachineOperand &p, MachineBasicBlock::iterator I) {
  RegUsageState RegUsage = RU_NotUsed;
  MachineInstr &MI = *I;

  for (unsigned int i = 0; i < MI.getNumOperands(); ++i) {
    MachineOperand &opnd = MI.getOperand(i);
    if (opnd.isReg() && opnd.getReg() == p.getReg()) {
      if (opnd.isDef())
        return RU_Write;
      RegUsage = RU_Read;
    }
  }
  return RegUsage;
}

/// getPreviousInstr - Given a reference to an instruction in a basic
/// block, return a reference to the previous instruction in the block,
/// wrapping around to the last instruction of the block if the block
/// branches to itself.
static inline bool getPreviousInstr(MachineBasicBlock::iterator &I,
                                    MachineFunction::iterator MFI) {
  if (I == MFI->begin()) {
    if (MFI->isPredecessor(&*MFI)) {
      I = --MFI->end();
      return true;
    } else
      return false;
  }
  --I;
  return true;
}

MachineBasicBlock::iterator
FixupLEAPass::searchBackwards(MachineOperand &p, MachineBasicBlock::iterator &I,
                              MachineFunction::iterator MFI) {
  int InstrDistance = 1;
  MachineBasicBlock::iterator CurInst;
  static const int INSTR_DISTANCE_THRESHOLD = 5;

  CurInst = I;
  bool Found;
  Found = getPreviousInstr(CurInst, MFI);
  while (Found && I != CurInst) {
    if (CurInst->isCall() || CurInst->isInlineAsm())
      break;
    if (InstrDistance > INSTR_DISTANCE_THRESHOLD)
      break; // too far back to make a difference
    if (usesRegister(p, CurInst) == RU_Write) {
      return CurInst;
    }
    InstrDistance += TSM.computeInstrLatency(&*CurInst);
    Found = getPreviousInstr(CurInst, MFI);
  }
  return MachineBasicBlock::iterator();
}

static inline bool isLEA(const int Opcode) {
  return Opcode == X86::LEA16r || Opcode == X86::LEA32r ||
         Opcode == X86::LEA64r || Opcode == X86::LEA64_32r;
}

static inline bool isInefficientLEAReg(unsigned int Reg) {
  return Reg == X86::EBP || Reg == X86::RBP ||
         Reg == X86::R13D || Reg == X86::R13;
}

static inline bool isRegOperand(const MachineOperand &Op) {
  return Op.isReg() && Op.getReg() != X86::NoRegister;
}

/// Returns true if this LEA uses base an index registers, and the base register
/// is known to be inefficient for the subtarget.
// TODO: use a variant scheduling class to model the latency profile
// of LEA instructions, and implement this logic as a scheduling predicate.
static inline bool hasInefficientLEABaseReg(const MachineOperand &Base,
                                            const MachineOperand &Index) {
  return Base.isReg() && isInefficientLEAReg(Base.getReg()) &&
         isRegOperand(Index);
}

static inline bool hasLEAOffset(const MachineOperand &Offset) {
  return (Offset.isImm() && Offset.getImm() != 0) || Offset.isGlobal();
}

static inline int getADDrrFromLEA(int LEAOpcode) {
  switch (LEAOpcode) {
  default:
    llvm_unreachable("Unexpected LEA instruction");
  case X86::LEA16r:
    return X86::ADD16rr;
  case X86::LEA32r:
    return X86::ADD32rr;
  case X86::LEA64_32r:
  case X86::LEA64r:
    return X86::ADD64rr;
  }
}

static inline int getADDriFromLEA(int LEAOpcode, const MachineOperand &Offset) {
  bool IsInt8 = Offset.isImm() && isInt<8>(Offset.getImm());
  switch (LEAOpcode) {
  default:
    llvm_unreachable("Unexpected LEA instruction");
  case X86::LEA16r:
    return IsInt8 ? X86::ADD16ri8 : X86::ADD16ri;
  case X86::LEA32r:
  case X86::LEA64_32r:
    return IsInt8 ? X86::ADD32ri8 : X86::ADD32ri;
  case X86::LEA64r:
    return IsInt8 ? X86::ADD64ri8 : X86::ADD64ri32;
  }
}

/// isLEASimpleIncOrDec - Does this LEA have one these forms:
/// lea  %reg, 1(%reg)
/// lea  %reg, -1(%reg)
static inline bool isLEASimpleIncOrDec(MachineInstr &LEA) {
  unsigned SrcReg = LEA.getOperand(1 + X86::AddrBaseReg).getReg();
  unsigned DstReg = LEA.getOperand(0).getReg();
  const MachineOperand &AddrDisp = LEA.getOperand(1 + X86::AddrDisp);
  return SrcReg == DstReg &&
         LEA.getOperand(1 + X86::AddrIndexReg).getReg() == 0 &&
         LEA.getOperand(1 + X86::AddrSegmentReg).getReg() == 0 &&
         AddrDisp.isImm() &&
         (AddrDisp.getImm() == 1 || AddrDisp.getImm() == -1);
}

bool FixupLEAPass::fixupIncDec(MachineBasicBlock::iterator &I,
                               MachineFunction::iterator MFI) const {
  MachineInstr &MI = *I;
  int Opcode = MI.getOpcode();
  if (!isLEA(Opcode))
    return false;

  if (isLEASimpleIncOrDec(MI) && TII->isSafeToClobberEFLAGS(*MFI, I)) {
    int NewOpcode;
    bool isINC = MI.getOperand(1 + X86::AddrDisp).getImm() == 1;
    switch (Opcode) {
    case X86::LEA16r:
      NewOpcode = isINC ? X86::INC16r : X86::DEC16r;
      break;
    case X86::LEA32r:
    case X86::LEA64_32r:
      NewOpcode = isINC ? X86::INC32r : X86::DEC32r;
      break;
    case X86::LEA64r:
      NewOpcode = isINC ? X86::INC64r : X86::DEC64r;
      break;
    }

    MachineInstr *NewMI =
        BuildMI(*MFI, I, MI.getDebugLoc(), TII->get(NewOpcode))
            .add(MI.getOperand(0))
            .add(MI.getOperand(1 + X86::AddrBaseReg));
    MFI->erase(I);
    I = static_cast<MachineBasicBlock::iterator>(NewMI);
    return true;
  }
  return false;
}

void FixupLEAPass::processInstruction(MachineBasicBlock::iterator &I,
                                      MachineFunction::iterator MFI) {
  // Process a load, store, or LEA instruction.
  MachineInstr &MI = *I;
  const MCInstrDesc &Desc = MI.getDesc();
  int AddrOffset = X86II::getMemoryOperandNo(Desc.TSFlags);
  if (AddrOffset >= 0) {
    AddrOffset += X86II::getOperandBias(Desc);
    MachineOperand &p = MI.getOperand(AddrOffset + X86::AddrBaseReg);
    if (p.isReg() && p.getReg() != X86::ESP) {
      seekLEAFixup(p, I, MFI);
    }
    MachineOperand &q = MI.getOperand(AddrOffset + X86::AddrIndexReg);
    if (q.isReg() && q.getReg() != X86::ESP) {
      seekLEAFixup(q, I, MFI);
    }
  }
}

void FixupLEAPass::seekLEAFixup(MachineOperand &p,
                                MachineBasicBlock::iterator &I,
                                MachineFunction::iterator MFI) {
  MachineBasicBlock::iterator MBI = searchBackwards(p, I, MFI);
  if (MBI != MachineBasicBlock::iterator()) {
    MachineInstr *NewMI = postRAConvertToLEA(MFI, MBI);
    if (NewMI) {
      ++NumLEAs;
      LLVM_DEBUG(dbgs() << "FixLEA: Candidate to replace:"; MBI->dump(););
      // now to replace with an equivalent LEA...
      LLVM_DEBUG(dbgs() << "FixLEA: Replaced by: "; NewMI->dump(););
      MFI->erase(MBI);
      MachineBasicBlock::iterator J =
          static_cast<MachineBasicBlock::iterator>(NewMI);
      processInstruction(J, MFI);
    }
  }
}

void FixupLEAPass::processInstructionForSlowLEA(MachineBasicBlock::iterator &I,
                                                MachineFunction::iterator MFI) {
  MachineInstr &MI = *I;
  const int Opcode = MI.getOpcode();
  if (!isLEA(Opcode))
    return;

  const MachineOperand &Dst =     MI.getOperand(0);
  const MachineOperand &Base =    MI.getOperand(1 + X86::AddrBaseReg);
  const MachineOperand &Scale =   MI.getOperand(1 + X86::AddrScaleAmt);
  const MachineOperand &Index =   MI.getOperand(1 + X86::AddrIndexReg);
  const MachineOperand &Offset =  MI.getOperand(1 + X86::AddrDisp);
  const MachineOperand &Segment = MI.getOperand(1 + X86::AddrSegmentReg);

  if (Segment.getReg() != 0 || !Offset.isImm() ||
      !TII->isSafeToClobberEFLAGS(*MFI, I))
    return;
  const unsigned DstR = Dst.getReg();
  const unsigned SrcR1 = Base.getReg();
  const unsigned SrcR2 = Index.getReg();
  if ((SrcR1 == 0 || SrcR1 != DstR) && (SrcR2 == 0 || SrcR2 != DstR))
    return;
  if (Scale.getImm() > 1)
    return;
  LLVM_DEBUG(dbgs() << "FixLEA: Candidate to replace:"; I->dump(););
  LLVM_DEBUG(dbgs() << "FixLEA: Replaced by: ";);
  MachineInstr *NewMI = nullptr;
  // Make ADD instruction for two registers writing to LEA's destination
  if (SrcR1 != 0 && SrcR2 != 0) {
    const MCInstrDesc &ADDrr = TII->get(getADDrrFromLEA(Opcode));
    const MachineOperand &Src = SrcR1 == DstR ? Index : Base;
    NewMI =
        BuildMI(*MFI, I, MI.getDebugLoc(), ADDrr, DstR).addReg(DstR).add(Src);
    LLVM_DEBUG(NewMI->dump(););
  }
  // Make ADD instruction for immediate
  if (Offset.getImm() != 0) {
    const MCInstrDesc &ADDri =
        TII->get(getADDriFromLEA(Opcode, Offset));
    const MachineOperand &SrcR = SrcR1 == DstR ? Base : Index;
    NewMI = BuildMI(*MFI, I, MI.getDebugLoc(), ADDri, DstR)
                .add(SrcR)
                .addImm(Offset.getImm());
    LLVM_DEBUG(NewMI->dump(););
  }
  if (NewMI) {
    MFI->erase(I);
    I = NewMI;
  }
}

MachineInstr *
FixupLEAPass::processInstrForSlow3OpLEA(MachineInstr &MI,
                                        MachineFunction::iterator MFI) {

  const int LEAOpcode = MI.getOpcode();
  if (!isLEA(LEAOpcode))
    return nullptr;

  const MachineOperand &Dst =     MI.getOperand(0);
  const MachineOperand &Base =    MI.getOperand(1 + X86::AddrBaseReg);
  const MachineOperand &Scale =   MI.getOperand(1 + X86::AddrScaleAmt);
  const MachineOperand &Index =   MI.getOperand(1 + X86::AddrIndexReg);
  const MachineOperand &Offset =  MI.getOperand(1 + X86::AddrDisp);
  const MachineOperand &Segment = MI.getOperand(1 + X86::AddrSegmentReg);

  if (!(TII->isThreeOperandsLEA(MI) ||
        hasInefficientLEABaseReg(Base, Index)) ||
      !TII->isSafeToClobberEFLAGS(*MFI, MI) ||
      Segment.getReg() != X86::NoRegister)
    return nullptr;

  unsigned int DstR = Dst.getReg();
  unsigned int BaseR = Base.getReg();
  unsigned int IndexR = Index.getReg();
  unsigned SSDstR =
      (LEAOpcode == X86::LEA64_32r) ? getX86SubSuperRegister(DstR, 64) : DstR;
  bool IsScale1 = Scale.getImm() == 1;
  bool IsInefficientBase = isInefficientLEAReg(BaseR);
  bool IsInefficientIndex = isInefficientLEAReg(IndexR);

  // Skip these cases since it takes more than 2 instructions
  // to replace the LEA instruction.
  if (IsInefficientBase && SSDstR == BaseR && !IsScale1)
    return nullptr;
  if (LEAOpcode == X86::LEA64_32r && IsInefficientBase &&
      (IsInefficientIndex || !IsScale1))
    return nullptr;

  const DebugLoc DL = MI.getDebugLoc();
  const MCInstrDesc &ADDrr = TII->get(getADDrrFromLEA(LEAOpcode));
  const MCInstrDesc &ADDri = TII->get(getADDriFromLEA(LEAOpcode, Offset));

  LLVM_DEBUG(dbgs() << "FixLEA: Candidate to replace:"; MI.dump(););
  LLVM_DEBUG(dbgs() << "FixLEA: Replaced by: ";);

  // First try to replace LEA with one or two (for the 3-op LEA case)
  // add instructions:
  // 1.lea (%base,%index,1), %base => add %index,%base
  // 2.lea (%base,%index,1), %index => add %base,%index
  if (IsScale1 && (DstR == BaseR || DstR == IndexR)) {
    const MachineOperand &Src = DstR == BaseR ? Index : Base;
    MachineInstr *NewMI =
        BuildMI(*MFI, MI, DL, ADDrr, DstR).addReg(DstR).add(Src);
    LLVM_DEBUG(NewMI->dump(););
    // Create ADD instruction for the Offset in case of 3-Ops LEA.
    if (hasLEAOffset(Offset)) {
      NewMI = BuildMI(*MFI, MI, DL, ADDri, DstR).addReg(DstR).add(Offset);
      LLVM_DEBUG(NewMI->dump(););
    }
    return NewMI;
  }
  // If the base is inefficient try switching the index and base operands,
  // otherwise just break the 3-Ops LEA inst into 2-Ops LEA + ADD instruction:
  // lea offset(%base,%index,scale),%dst =>
  // lea (%base,%index,scale); add offset,%dst
  if (!IsInefficientBase || (!IsInefficientIndex && IsScale1)) {
    MachineInstr *NewMI = BuildMI(*MFI, MI, DL, TII->get(LEAOpcode))
                              .add(Dst)
                              .add(IsInefficientBase ? Index : Base)
                              .add(Scale)
                              .add(IsInefficientBase ? Base : Index)
                              .addImm(0)
                              .add(Segment);
    LLVM_DEBUG(NewMI->dump(););
    // Create ADD instruction for the Offset in case of 3-Ops LEA.
    if (hasLEAOffset(Offset)) {
      NewMI = BuildMI(*MFI, MI, DL, ADDri, DstR).addReg(DstR).add(Offset);
      LLVM_DEBUG(NewMI->dump(););
    }
    return NewMI;
  }
  // Handle the rest of the cases with inefficient base register:
  assert(SSDstR != BaseR && "SSDstR == BaseR should be handled already!");
  assert(IsInefficientBase && "efficient base should be handled already!");

  // lea (%base,%index,1), %dst => mov %base,%dst; add %index,%dst
  if (IsScale1 && !hasLEAOffset(Offset)) {
    bool BIK = Base.isKill() && BaseR != IndexR;
    TII->copyPhysReg(*MFI, MI, DL, DstR, BaseR, BIK);
    LLVM_DEBUG(MI.getPrevNode()->dump(););

    MachineInstr *NewMI =
        BuildMI(*MFI, MI, DL, ADDrr, DstR).addReg(DstR).add(Index);
    LLVM_DEBUG(NewMI->dump(););
    return NewMI;
  }
  // lea offset(%base,%index,scale), %dst =>
  // lea offset( ,%index,scale), %dst; add %base,%dst
  MachineInstr *NewMI = BuildMI(*MFI, MI, DL, TII->get(LEAOpcode))
                            .add(Dst)
                            .addReg(0)
                            .add(Scale)
                            .add(Index)
                            .add(Offset)
                            .add(Segment);
  LLVM_DEBUG(NewMI->dump(););

  NewMI = BuildMI(*MFI, MI, DL, ADDrr, DstR).addReg(DstR).add(Base);
  LLVM_DEBUG(NewMI->dump(););
  return NewMI;
}

bool FixupLEAPass::processBasicBlock(MachineFunction &MF,
                                     MachineFunction::iterator MFI,
                                     bool IsSlowLEA, bool IsSlow3OpsLEA) {
  for (MachineBasicBlock::iterator I = MFI->begin(); I != MFI->end(); ++I) {
    if (OptIncDec)
      if (fixupIncDec(I, MFI))
        continue;

    if (OptLEA) {
      if (IsSlowLEA) {
        processInstructionForSlowLEA(I, MFI);
        continue;
      }
      
      if (IsSlow3OpsLEA) {
        if (auto *NewMI = processInstrForSlow3OpLEA(*I, MFI)) {
          MFI->erase(I);
          I = NewMI;
        }
        continue;
      }

      processInstruction(I, MFI);
    }
  }
  return false;
}
