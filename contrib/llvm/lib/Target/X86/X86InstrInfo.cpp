//===-- X86InstrInfo.cpp - X86 Instruction Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "X86InstrInfo.h"
#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrFoldTables.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "x86-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "X86GenInstrInfo.inc"

static cl::opt<bool>
    NoFusing("disable-spill-fusing",
             cl::desc("Disable fusing of spill code into instructions"),
             cl::Hidden);
static cl::opt<bool>
PrintFailedFusing("print-failed-fuse-candidates",
                  cl::desc("Print instructions that the allocator wants to"
                           " fuse, but the X86 backend currently can't"),
                  cl::Hidden);
static cl::opt<bool>
ReMatPICStubLoad("remat-pic-stub-load",
                 cl::desc("Re-materialize load from stub in PIC mode"),
                 cl::init(false), cl::Hidden);
static cl::opt<unsigned>
PartialRegUpdateClearance("partial-reg-update-clearance",
                          cl::desc("Clearance between two register writes "
                                   "for inserting XOR to avoid partial "
                                   "register update"),
                          cl::init(64), cl::Hidden);
static cl::opt<unsigned>
UndefRegClearance("undef-reg-clearance",
                  cl::desc("How many idle instructions we would like before "
                           "certain undef register reads"),
                  cl::init(128), cl::Hidden);


// Pin the vtable to this file.
void X86InstrInfo::anchor() {}

X86InstrInfo::X86InstrInfo(X86Subtarget &STI)
    : X86GenInstrInfo((STI.isTarget64BitLP64() ? X86::ADJCALLSTACKDOWN64
                                               : X86::ADJCALLSTACKDOWN32),
                      (STI.isTarget64BitLP64() ? X86::ADJCALLSTACKUP64
                                               : X86::ADJCALLSTACKUP32),
                      X86::CATCHRET,
                      (STI.is64Bit() ? X86::RETQ : X86::RETL)),
      Subtarget(STI), RI(STI.getTargetTriple()) {
}

bool
X86InstrInfo::isCoalescableExtInstr(const MachineInstr &MI,
                                    unsigned &SrcReg, unsigned &DstReg,
                                    unsigned &SubIdx) const {
  switch (MI.getOpcode()) {
  default: break;
  case X86::MOVSX16rr8:
  case X86::MOVZX16rr8:
  case X86::MOVSX32rr8:
  case X86::MOVZX32rr8:
  case X86::MOVSX64rr8:
    if (!Subtarget.is64Bit())
      // It's not always legal to reference the low 8-bit of the larger
      // register in 32-bit mode.
      return false;
    LLVM_FALLTHROUGH;
  case X86::MOVSX32rr16:
  case X86::MOVZX32rr16:
  case X86::MOVSX64rr16:
  case X86::MOVSX64rr32: {
    if (MI.getOperand(0).getSubReg() || MI.getOperand(1).getSubReg())
      // Be conservative.
      return false;
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::MOVSX16rr8:
    case X86::MOVZX16rr8:
    case X86::MOVSX32rr8:
    case X86::MOVZX32rr8:
    case X86::MOVSX64rr8:
      SubIdx = X86::sub_8bit;
      break;
    case X86::MOVSX32rr16:
    case X86::MOVZX32rr16:
    case X86::MOVSX64rr16:
      SubIdx = X86::sub_16bit;
      break;
    case X86::MOVSX64rr32:
      SubIdx = X86::sub_32bit;
      break;
    }
    return true;
  }
  }
  return false;
}

int X86InstrInfo::getSPAdjust(const MachineInstr &MI) const {
  const MachineFunction *MF = MI.getParent()->getParent();
  const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();

  if (isFrameInstr(MI)) {
    unsigned StackAlign = TFI->getStackAlignment();
    int SPAdj = alignTo(getFrameSize(MI), StackAlign);
    SPAdj -= getFrameAdjustment(MI);
    if (!isFrameSetup(MI))
      SPAdj = -SPAdj;
    return SPAdj;
  }

  // To know whether a call adjusts the stack, we need information
  // that is bound to the following ADJCALLSTACKUP pseudo.
  // Look for the next ADJCALLSTACKUP that follows the call.
  if (MI.isCall()) {
    const MachineBasicBlock *MBB = MI.getParent();
    auto I = ++MachineBasicBlock::const_iterator(MI);
    for (auto E = MBB->end(); I != E; ++I) {
      if (I->getOpcode() == getCallFrameDestroyOpcode() ||
          I->isCall())
        break;
    }

    // If we could not find a frame destroy opcode, then it has already
    // been simplified, so we don't care.
    if (I->getOpcode() != getCallFrameDestroyOpcode())
      return 0;

    return -(I->getOperand(1).getImm());
  }

  // Currently handle only PUSHes we can reasonably expect to see
  // in call sequences
  switch (MI.getOpcode()) {
  default:
    return 0;
  case X86::PUSH32i8:
  case X86::PUSH32r:
  case X86::PUSH32rmm:
  case X86::PUSH32rmr:
  case X86::PUSHi32:
    return 4;
  case X86::PUSH64i8:
  case X86::PUSH64r:
  case X86::PUSH64rmm:
  case X86::PUSH64rmr:
  case X86::PUSH64i32:
    return 8;
  }
}

/// Return true and the FrameIndex if the specified
/// operand and follow operands form a reference to the stack frame.
bool X86InstrInfo::isFrameOperand(const MachineInstr &MI, unsigned int Op,
                                  int &FrameIndex) const {
  if (MI.getOperand(Op + X86::AddrBaseReg).isFI() &&
      MI.getOperand(Op + X86::AddrScaleAmt).isImm() &&
      MI.getOperand(Op + X86::AddrIndexReg).isReg() &&
      MI.getOperand(Op + X86::AddrDisp).isImm() &&
      MI.getOperand(Op + X86::AddrScaleAmt).getImm() == 1 &&
      MI.getOperand(Op + X86::AddrIndexReg).getReg() == 0 &&
      MI.getOperand(Op + X86::AddrDisp).getImm() == 0) {
    FrameIndex = MI.getOperand(Op + X86::AddrBaseReg).getIndex();
    return true;
  }
  return false;
}

static bool isFrameLoadOpcode(int Opcode, unsigned &MemBytes) {
  switch (Opcode) {
  default:
    return false;
  case X86::MOV8rm:
  case X86::KMOVBkm:
    MemBytes = 1;
    return true;
  case X86::MOV16rm:
  case X86::KMOVWkm:
    MemBytes = 2;
    return true;
  case X86::MOV32rm:
  case X86::MOVSSrm:
  case X86::VMOVSSZrm:
  case X86::VMOVSSrm:
  case X86::KMOVDkm:
    MemBytes = 4;
    return true;
  case X86::MOV64rm:
  case X86::LD_Fp64m:
  case X86::MOVSDrm:
  case X86::VMOVSDrm:
  case X86::VMOVSDZrm:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  case X86::KMOVQkm:
    MemBytes = 8;
    return true;
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQU64Z128rm:
    MemBytes = 16;
    return true;
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQU64Z256rm:
    MemBytes = 32;
    return true;
  case X86::VMOVAPSZrm:
  case X86::VMOVUPSZrm:
  case X86::VMOVAPDZrm:
  case X86::VMOVUPDZrm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU64Zrm:
    MemBytes = 64;
    return true;
  }
}

static bool isFrameStoreOpcode(int Opcode, unsigned &MemBytes) {
  switch (Opcode) {
  default:
    return false;
  case X86::MOV8mr:
  case X86::KMOVBmk:
    MemBytes = 1;
    return true;
  case X86::MOV16mr:
  case X86::KMOVWmk:
    MemBytes = 2;
    return true;
  case X86::MOV32mr:
  case X86::MOVSSmr:
  case X86::VMOVSSmr:
  case X86::VMOVSSZmr:
  case X86::KMOVDmk:
    MemBytes = 4;
    return true;
  case X86::MOV64mr:
  case X86::ST_FpP64m:
  case X86::MOVSDmr:
  case X86::VMOVSDmr:
  case X86::VMOVSDZmr:
  case X86::MMX_MOVD64mr:
  case X86::MMX_MOVQ64mr:
  case X86::MMX_MOVNTQmr:
  case X86::KMOVQmk:
    MemBytes = 8;
    return true;
  case X86::MOVAPSmr:
  case X86::MOVUPSmr:
  case X86::MOVAPDmr:
  case X86::MOVUPDmr:
  case X86::MOVDQAmr:
  case X86::MOVDQUmr:
  case X86::VMOVAPSmr:
  case X86::VMOVUPSmr:
  case X86::VMOVAPDmr:
  case X86::VMOVUPDmr:
  case X86::VMOVDQAmr:
  case X86::VMOVDQUmr:
  case X86::VMOVUPSZ128mr:
  case X86::VMOVAPSZ128mr:
  case X86::VMOVUPSZ128mr_NOVLX:
  case X86::VMOVAPSZ128mr_NOVLX:
  case X86::VMOVUPDZ128mr:
  case X86::VMOVAPDZ128mr:
  case X86::VMOVDQA32Z128mr:
  case X86::VMOVDQU32Z128mr:
  case X86::VMOVDQA64Z128mr:
  case X86::VMOVDQU64Z128mr:
  case X86::VMOVDQU8Z128mr:
  case X86::VMOVDQU16Z128mr:
    MemBytes = 16;
    return true;
  case X86::VMOVUPSYmr:
  case X86::VMOVAPSYmr:
  case X86::VMOVUPDYmr:
  case X86::VMOVAPDYmr:
  case X86::VMOVDQUYmr:
  case X86::VMOVDQAYmr:
  case X86::VMOVUPSZ256mr:
  case X86::VMOVAPSZ256mr:
  case X86::VMOVUPSZ256mr_NOVLX:
  case X86::VMOVAPSZ256mr_NOVLX:
  case X86::VMOVUPDZ256mr:
  case X86::VMOVAPDZ256mr:
  case X86::VMOVDQU8Z256mr:
  case X86::VMOVDQU16Z256mr:
  case X86::VMOVDQA32Z256mr:
  case X86::VMOVDQU32Z256mr:
  case X86::VMOVDQA64Z256mr:
  case X86::VMOVDQU64Z256mr:
    MemBytes = 32;
    return true;
  case X86::VMOVUPSZmr:
  case X86::VMOVAPSZmr:
  case X86::VMOVUPDZmr:
  case X86::VMOVAPDZmr:
  case X86::VMOVDQU8Zmr:
  case X86::VMOVDQU16Zmr:
  case X86::VMOVDQA32Zmr:
  case X86::VMOVDQU32Zmr:
  case X86::VMOVDQA64Zmr:
  case X86::VMOVDQU64Zmr:
    MemBytes = 64;
    return true;
  }
  return false;
}

unsigned X86InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  unsigned Dummy;
  return X86InstrInfo::isLoadFromStackSlot(MI, FrameIndex, Dummy);
}

unsigned X86InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex,
                                           unsigned &MemBytes) const {
  if (isFrameLoadOpcode(MI.getOpcode(), MemBytes))
    if (MI.getOperand(0).getSubReg() == 0 && isFrameOperand(MI, 1, FrameIndex))
      return MI.getOperand(0).getReg();
  return 0;
}

unsigned X86InstrInfo::isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                                 int &FrameIndex) const {
  unsigned Dummy;
  if (isFrameLoadOpcode(MI.getOpcode(), Dummy)) {
    unsigned Reg;
    if ((Reg = isLoadFromStackSlot(MI, FrameIndex)))
      return Reg;
    // Check for post-frame index elimination operations
    SmallVector<const MachineMemOperand *, 1> Accesses;
    if (hasLoadFromStackSlot(MI, Accesses)) {
      FrameIndex =
          cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
              ->getFrameIndex();
      return 1;
    }
  }
  return 0;
}

unsigned X86InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  unsigned Dummy;
  return X86InstrInfo::isStoreToStackSlot(MI, FrameIndex, Dummy);
}

unsigned X86InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex,
                                          unsigned &MemBytes) const {
  if (isFrameStoreOpcode(MI.getOpcode(), MemBytes))
    if (MI.getOperand(X86::AddrNumOperands).getSubReg() == 0 &&
        isFrameOperand(MI, 0, FrameIndex))
      return MI.getOperand(X86::AddrNumOperands).getReg();
  return 0;
}

unsigned X86InstrInfo::isStoreToStackSlotPostFE(const MachineInstr &MI,
                                                int &FrameIndex) const {
  unsigned Dummy;
  if (isFrameStoreOpcode(MI.getOpcode(), Dummy)) {
    unsigned Reg;
    if ((Reg = isStoreToStackSlot(MI, FrameIndex)))
      return Reg;
    // Check for post-frame index elimination operations
    SmallVector<const MachineMemOperand *, 1> Accesses;
    if (hasStoreToStackSlot(MI, Accesses)) {
      FrameIndex =
          cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
              ->getFrameIndex();
      return 1;
    }
  }
  return 0;
}

/// Return true if register is PIC base; i.e.g defined by X86::MOVPC32r.
static bool regIsPICBase(unsigned BaseReg, const MachineRegisterInfo &MRI) {
  // Don't waste compile time scanning use-def chains of physregs.
  if (!TargetRegisterInfo::isVirtualRegister(BaseReg))
    return false;
  bool isPICBase = false;
  for (MachineRegisterInfo::def_instr_iterator I = MRI.def_instr_begin(BaseReg),
         E = MRI.def_instr_end(); I != E; ++I) {
    MachineInstr *DefMI = &*I;
    if (DefMI->getOpcode() != X86::MOVPC32r)
      return false;
    assert(!isPICBase && "More than one PIC base?");
    isPICBase = true;
  }
  return isPICBase;
}

bool X86InstrInfo::isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                                     AliasAnalysis *AA) const {
  switch (MI.getOpcode()) {
  default: break;
  case X86::MOV8rm:
  case X86::MOV8rm_NOREX:
  case X86::MOV16rm:
  case X86::MOV32rm:
  case X86::MOV64rm:
  case X86::LD_Fp64m:
  case X86::MOVSSrm:
  case X86::MOVSDrm:
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  case X86::VMOVSSrm:
  case X86::VMOVSDrm:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  // AVX-512
  case X86::VMOVSSZrm:
  case X86::VMOVSDZrm:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVAPDZrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVAPSZrm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQU64Z128rm:
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVDQU64Zrm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVUPDZrm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVUPSZrm: {
    // Loads from constant pools are trivially rematerializable.
    if (MI.getOperand(1 + X86::AddrBaseReg).isReg() &&
        MI.getOperand(1 + X86::AddrScaleAmt).isImm() &&
        MI.getOperand(1 + X86::AddrIndexReg).isReg() &&
        MI.getOperand(1 + X86::AddrIndexReg).getReg() == 0 &&
        MI.isDereferenceableInvariantLoad(AA)) {
      unsigned BaseReg = MI.getOperand(1 + X86::AddrBaseReg).getReg();
      if (BaseReg == 0 || BaseReg == X86::RIP)
        return true;
      // Allow re-materialization of PIC load.
      if (!ReMatPICStubLoad && MI.getOperand(1 + X86::AddrDisp).isGlobal())
        return false;
      const MachineFunction &MF = *MI.getParent()->getParent();
      const MachineRegisterInfo &MRI = MF.getRegInfo();
      return regIsPICBase(BaseReg, MRI);
    }
    return false;
  }

  case X86::LEA32r:
  case X86::LEA64r: {
    if (MI.getOperand(1 + X86::AddrScaleAmt).isImm() &&
        MI.getOperand(1 + X86::AddrIndexReg).isReg() &&
        MI.getOperand(1 + X86::AddrIndexReg).getReg() == 0 &&
        !MI.getOperand(1 + X86::AddrDisp).isReg()) {
      // lea fi#, lea GV, etc. are all rematerializable.
      if (!MI.getOperand(1 + X86::AddrBaseReg).isReg())
        return true;
      unsigned BaseReg = MI.getOperand(1 + X86::AddrBaseReg).getReg();
      if (BaseReg == 0)
        return true;
      // Allow re-materialization of lea PICBase + x.
      const MachineFunction &MF = *MI.getParent()->getParent();
      const MachineRegisterInfo &MRI = MF.getRegInfo();
      return regIsPICBase(BaseReg, MRI);
    }
    return false;
  }
  }

  // All other instructions marked M_REMATERIALIZABLE are always trivially
  // rematerializable.
  return true;
}

bool X86InstrInfo::isSafeToClobberEFLAGS(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I) const {
  MachineBasicBlock::iterator E = MBB.end();

  // For compile time consideration, if we are not able to determine the
  // safety after visiting 4 instructions in each direction, we will assume
  // it's not safe.
  MachineBasicBlock::iterator Iter = I;
  for (unsigned i = 0; Iter != E && i < 4; ++i) {
    bool SeenDef = false;
    for (unsigned j = 0, e = Iter->getNumOperands(); j != e; ++j) {
      MachineOperand &MO = Iter->getOperand(j);
      if (MO.isRegMask() && MO.clobbersPhysReg(X86::EFLAGS))
        SeenDef = true;
      if (!MO.isReg())
        continue;
      if (MO.getReg() == X86::EFLAGS) {
        if (MO.isUse())
          return false;
        SeenDef = true;
      }
    }

    if (SeenDef)
      // This instruction defines EFLAGS, no need to look any further.
      return true;
    ++Iter;
    // Skip over debug instructions.
    while (Iter != E && Iter->isDebugInstr())
      ++Iter;
  }

  // It is safe to clobber EFLAGS at the end of a block of no successor has it
  // live in.
  if (Iter == E) {
    for (MachineBasicBlock *S : MBB.successors())
      if (S->isLiveIn(X86::EFLAGS))
        return false;
    return true;
  }

  MachineBasicBlock::iterator B = MBB.begin();
  Iter = I;
  for (unsigned i = 0; i < 4; ++i) {
    // If we make it to the beginning of the block, it's safe to clobber
    // EFLAGS iff EFLAGS is not live-in.
    if (Iter == B)
      return !MBB.isLiveIn(X86::EFLAGS);

    --Iter;
    // Skip over debug instructions.
    while (Iter != B && Iter->isDebugInstr())
      --Iter;

    bool SawKill = false;
    for (unsigned j = 0, e = Iter->getNumOperands(); j != e; ++j) {
      MachineOperand &MO = Iter->getOperand(j);
      // A register mask may clobber EFLAGS, but we should still look for a
      // live EFLAGS def.
      if (MO.isRegMask() && MO.clobbersPhysReg(X86::EFLAGS))
        SawKill = true;
      if (MO.isReg() && MO.getReg() == X86::EFLAGS) {
        if (MO.isDef()) return MO.isDead();
        if (MO.isKill()) SawKill = true;
      }
    }

    if (SawKill)
      // This instruction kills EFLAGS and doesn't redefine it, so
      // there's no need to look further.
      return true;
  }

  // Conservative answer.
  return false;
}

void X86InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 unsigned DestReg, unsigned SubIdx,
                                 const MachineInstr &Orig,
                                 const TargetRegisterInfo &TRI) const {
  bool ClobbersEFLAGS = false;
  for (const MachineOperand &MO : Orig.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == X86::EFLAGS) {
      ClobbersEFLAGS = true;
      break;
    }
  }

  if (ClobbersEFLAGS && !isSafeToClobberEFLAGS(MBB, I)) {
    // The instruction clobbers EFLAGS. Re-materialize as MOV32ri to avoid side
    // effects.
    int Value;
    switch (Orig.getOpcode()) {
    case X86::MOV32r0:  Value = 0; break;
    case X86::MOV32r1:  Value = 1; break;
    case X86::MOV32r_1: Value = -1; break;
    default:
      llvm_unreachable("Unexpected instruction!");
    }

    const DebugLoc &DL = Orig.getDebugLoc();
    BuildMI(MBB, I, DL, get(X86::MOV32ri))
        .add(Orig.getOperand(0))
        .addImm(Value);
  } else {
    MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
    MBB.insert(I, MI);
  }

  MachineInstr &NewMI = *std::prev(I);
  NewMI.substituteRegister(Orig.getOperand(0).getReg(), DestReg, SubIdx, TRI);
}

/// True if MI has a condition code def, e.g. EFLAGS, that is not marked dead.
bool X86InstrInfo::hasLiveCondCodeDef(MachineInstr &MI) const {
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isDef() &&
        MO.getReg() == X86::EFLAGS && !MO.isDead()) {
      return true;
    }
  }
  return false;
}

/// Check whether the shift count for a machine operand is non-zero.
inline static unsigned getTruncatedShiftCount(const MachineInstr &MI,
                                              unsigned ShiftAmtOperandIdx) {
  // The shift count is six bits with the REX.W prefix and five bits without.
  unsigned ShiftCountMask = (MI.getDesc().TSFlags & X86II::REX_W) ? 63 : 31;
  unsigned Imm = MI.getOperand(ShiftAmtOperandIdx).getImm();
  return Imm & ShiftCountMask;
}

/// Check whether the given shift count is appropriate
/// can be represented by a LEA instruction.
inline static bool isTruncatedShiftCountForLEA(unsigned ShAmt) {
  // Left shift instructions can be transformed into load-effective-address
  // instructions if we can encode them appropriately.
  // A LEA instruction utilizes a SIB byte to encode its scale factor.
  // The SIB.scale field is two bits wide which means that we can encode any
  // shift amount less than 4.
  return ShAmt < 4 && ShAmt > 0;
}

bool X86InstrInfo::classifyLEAReg(MachineInstr &MI, const MachineOperand &Src,
                                  unsigned Opc, bool AllowSP, unsigned &NewSrc,
                                  bool &isKill, MachineOperand &ImplicitOp,
                                  LiveVariables *LV) const {
  MachineFunction &MF = *MI.getParent()->getParent();
  const TargetRegisterClass *RC;
  if (AllowSP) {
    RC = Opc != X86::LEA32r ? &X86::GR64RegClass : &X86::GR32RegClass;
  } else {
    RC = Opc != X86::LEA32r ?
      &X86::GR64_NOSPRegClass : &X86::GR32_NOSPRegClass;
  }
  unsigned SrcReg = Src.getReg();

  // For both LEA64 and LEA32 the register already has essentially the right
  // type (32-bit or 64-bit) we may just need to forbid SP.
  if (Opc != X86::LEA64_32r) {
    NewSrc = SrcReg;
    isKill = Src.isKill();
    assert(!Src.isUndef() && "Undef op doesn't need optimization");

    if (TargetRegisterInfo::isVirtualRegister(NewSrc) &&
        !MF.getRegInfo().constrainRegClass(NewSrc, RC))
      return false;

    return true;
  }

  // This is for an LEA64_32r and incoming registers are 32-bit. One way or
  // another we need to add 64-bit registers to the final MI.
  if (TargetRegisterInfo::isPhysicalRegister(SrcReg)) {
    ImplicitOp = Src;
    ImplicitOp.setImplicit();

    NewSrc = getX86SubSuperRegister(Src.getReg(), 64);
    isKill = Src.isKill();
    assert(!Src.isUndef() && "Undef op doesn't need optimization");
  } else {
    // Virtual register of the wrong class, we have to create a temporary 64-bit
    // vreg to feed into the LEA.
    NewSrc = MF.getRegInfo().createVirtualRegister(RC);
    MachineInstr *Copy =
        BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(TargetOpcode::COPY))
            .addReg(NewSrc, RegState::Define | RegState::Undef, X86::sub_32bit)
            .add(Src);

    // Which is obviously going to be dead after we're done with it.
    isKill = true;

    if (LV)
      LV->replaceKillInstruction(SrcReg, MI, *Copy);
  }

  // We've set all the parameters without issue.
  return true;
}

MachineInstr *X86InstrInfo::convertToThreeAddressWithLEA(
    unsigned MIOpc, MachineFunction::iterator &MFI, MachineInstr &MI,
    LiveVariables *LV) const {
  // We handle 8-bit adds and various 16-bit opcodes in the switch below.
  bool Is16BitOp = !(MIOpc == X86::ADD8rr || MIOpc == X86::ADD8ri);
  MachineRegisterInfo &RegInfo = MFI->getParent()->getRegInfo();
  assert((!Is16BitOp || RegInfo.getTargetRegisterInfo()->getRegSizeInBits(
              *RegInfo.getRegClass(MI.getOperand(0).getReg())) == 16) &&
         "Unexpected type for LEA transform");

  // TODO: For a 32-bit target, we need to adjust the LEA variables with
  // something like this:
  //   Opcode = X86::LEA32r;
  //   InRegLEA = RegInfo.createVirtualRegister(&X86::GR32_NOSPRegClass);
  //   OutRegLEA =
  //       Is8BitOp ? RegInfo.createVirtualRegister(&X86::GR32ABCD_RegClass)
  //                : RegInfo.createVirtualRegister(&X86::GR32RegClass);
  if (!Subtarget.is64Bit())
    return nullptr;

  unsigned Opcode = X86::LEA64_32r;
  unsigned InRegLEA = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
  unsigned OutRegLEA = RegInfo.createVirtualRegister(&X86::GR32RegClass);

  // Build and insert into an implicit UNDEF value. This is OK because
  // we will be shifting and then extracting the lower 8/16-bits.
  // This has the potential to cause partial register stall. e.g.
  //   movw    (%rbp,%rcx,2), %dx
  //   leal    -65(%rdx), %esi
  // But testing has shown this *does* help performance in 64-bit mode (at
  // least on modern x86 machines).
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  unsigned Dest = MI.getOperand(0).getReg();
  unsigned Src = MI.getOperand(1).getReg();
  bool IsDead = MI.getOperand(0).isDead();
  bool IsKill = MI.getOperand(1).isKill();
  unsigned SubReg = Is16BitOp ? X86::sub_16bit : X86::sub_8bit;
  assert(!MI.getOperand(1).isUndef() && "Undef op doesn't need optimization");
  BuildMI(*MFI, MBBI, MI.getDebugLoc(), get(X86::IMPLICIT_DEF), InRegLEA);
  MachineInstr *InsMI =
      BuildMI(*MFI, MBBI, MI.getDebugLoc(), get(TargetOpcode::COPY))
          .addReg(InRegLEA, RegState::Define, SubReg)
          .addReg(Src, getKillRegState(IsKill));

  MachineInstrBuilder MIB =
      BuildMI(*MFI, MBBI, MI.getDebugLoc(), get(Opcode), OutRegLEA);
  switch (MIOpc) {
  default: llvm_unreachable("Unreachable!");
  case X86::SHL16ri: {
    unsigned ShAmt = MI.getOperand(2).getImm();
    MIB.addReg(0).addImm(1ULL << ShAmt)
       .addReg(InRegLEA, RegState::Kill).addImm(0).addReg(0);
    break;
  }
  case X86::INC16r:
    addRegOffset(MIB, InRegLEA, true, 1);
    break;
  case X86::DEC16r:
    addRegOffset(MIB, InRegLEA, true, -1);
    break;
  case X86::ADD8ri:
  case X86::ADD16ri:
  case X86::ADD16ri8:
  case X86::ADD16ri_DB:
  case X86::ADD16ri8_DB:
    addRegOffset(MIB, InRegLEA, true, MI.getOperand(2).getImm());
    break;
  case X86::ADD8rr:
  case X86::ADD16rr:
  case X86::ADD16rr_DB: {
    unsigned Src2 = MI.getOperand(2).getReg();
    bool IsKill2 = MI.getOperand(2).isKill();
    assert(!MI.getOperand(2).isUndef() && "Undef op doesn't need optimization");
    unsigned InRegLEA2 = 0;
    MachineInstr *InsMI2 = nullptr;
    if (Src == Src2) {
      // ADD8rr/ADD16rr killed %reg1028, %reg1028
      // just a single insert_subreg.
      addRegReg(MIB, InRegLEA, true, InRegLEA, false);
    } else {
      if (Subtarget.is64Bit())
        InRegLEA2 = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
      else
        InRegLEA2 = RegInfo.createVirtualRegister(&X86::GR32_NOSPRegClass);
      // Build and insert into an implicit UNDEF value. This is OK because
      // we will be shifting and then extracting the lower 8/16-bits.
      BuildMI(*MFI, &*MIB, MI.getDebugLoc(), get(X86::IMPLICIT_DEF), InRegLEA2);
      InsMI2 = BuildMI(*MFI, &*MIB, MI.getDebugLoc(), get(TargetOpcode::COPY))
                   .addReg(InRegLEA2, RegState::Define, SubReg)
                   .addReg(Src2, getKillRegState(IsKill2));
      addRegReg(MIB, InRegLEA, true, InRegLEA2, true);
    }
    if (LV && IsKill2 && InsMI2)
      LV->replaceKillInstruction(Src2, MI, *InsMI2);
    break;
  }
  }

  MachineInstr *NewMI = MIB;
  MachineInstr *ExtMI =
      BuildMI(*MFI, MBBI, MI.getDebugLoc(), get(TargetOpcode::COPY))
          .addReg(Dest, RegState::Define | getDeadRegState(IsDead))
          .addReg(OutRegLEA, RegState::Kill, SubReg);

  if (LV) {
    // Update live variables.
    LV->getVarInfo(InRegLEA).Kills.push_back(NewMI);
    LV->getVarInfo(OutRegLEA).Kills.push_back(ExtMI);
    if (IsKill)
      LV->replaceKillInstruction(Src, MI, *InsMI);
    if (IsDead)
      LV->replaceKillInstruction(Dest, MI, *ExtMI);
  }

  return ExtMI;
}

/// This method must be implemented by targets that
/// set the M_CONVERTIBLE_TO_3_ADDR flag.  When this flag is set, the target
/// may be able to convert a two-address instruction into a true
/// three-address instruction on demand.  This allows the X86 target (for
/// example) to convert ADD and SHL instructions into LEA instructions if they
/// would require register copies due to two-addressness.
///
/// This method returns a null pointer if the transformation cannot be
/// performed, otherwise it returns the new instruction.
///
MachineInstr *
X86InstrInfo::convertToThreeAddress(MachineFunction::iterator &MFI,
                                    MachineInstr &MI, LiveVariables *LV) const {
  // The following opcodes also sets the condition code register(s). Only
  // convert them to equivalent lea if the condition code register def's
  // are dead!
  if (hasLiveCondCodeDef(MI))
    return nullptr;

  MachineFunction &MF = *MI.getParent()->getParent();
  // All instructions input are two-addr instructions.  Get the known operands.
  const MachineOperand &Dest = MI.getOperand(0);
  const MachineOperand &Src = MI.getOperand(1);

  // Ideally, operations with undef should be folded before we get here, but we
  // can't guarantee it. Bail out because optimizing undefs is a waste of time.
  // Without this, we have to forward undef state to new register operands to
  // avoid machine verifier errors.
  if (Src.isUndef())
    return nullptr;
  if (MI.getNumOperands() > 2)
    if (MI.getOperand(2).isReg() && MI.getOperand(2).isUndef())
      return nullptr;

  MachineInstr *NewMI = nullptr;
  bool Is64Bit = Subtarget.is64Bit();

  unsigned MIOpc = MI.getOpcode();
  switch (MIOpc) {
  default: return nullptr;
  case X86::SHL64ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt)) return nullptr;

    // LEA can't handle RSP.
    if (TargetRegisterInfo::isVirtualRegister(Src.getReg()) &&
        !MF.getRegInfo().constrainRegClass(Src.getReg(),
                                           &X86::GR64_NOSPRegClass))
      return nullptr;

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(X86::LEA64r))
                .add(Dest)
                .addReg(0)
                .addImm(1ULL << ShAmt)
                .add(Src)
                .addImm(0)
                .addReg(0);
    break;
  }
  case X86::SHL32ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt)) return nullptr;

    unsigned Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    // LEA can't handle ESP.
    bool isKill;
    unsigned SrcReg;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/ false,
                        SrcReg, isKill, ImplicitOp, LV))
      return nullptr;

    MachineInstrBuilder MIB =
        BuildMI(MF, MI.getDebugLoc(), get(Opc))
            .add(Dest)
            .addReg(0)
            .addImm(1ULL << ShAmt)
            .addReg(SrcReg, getKillRegState(isKill))
            .addImm(0)
            .addReg(0);
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);
    NewMI = MIB;

    break;
  }
  case X86::SHL16ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt))
      return nullptr;
    return convertToThreeAddressWithLEA(MIOpc, MFI, MI, LV);
  }
  case X86::INC64r:
  case X86::INC32r: {
    assert(MI.getNumOperands() >= 2 && "Unknown inc instruction!");
    unsigned Opc = MIOpc == X86::INC64r ? X86::LEA64r :
        (Is64Bit ? X86::LEA64_32r : X86::LEA32r);
    bool isKill;
    unsigned SrcReg;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/ false, SrcReg, isKill,
                        ImplicitOp, LV))
      return nullptr;

    MachineInstrBuilder MIB =
        BuildMI(MF, MI.getDebugLoc(), get(Opc))
            .add(Dest)
            .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, 1);
    break;
  }
  case X86::INC16r:
    return convertToThreeAddressWithLEA(MIOpc, MFI, MI, LV);
  case X86::DEC64r:
  case X86::DEC32r: {
    assert(MI.getNumOperands() >= 2 && "Unknown dec instruction!");
    unsigned Opc = MIOpc == X86::DEC64r ? X86::LEA64r
        : (Is64Bit ? X86::LEA64_32r : X86::LEA32r);

    bool isKill;
    unsigned SrcReg;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/ false, SrcReg, isKill,
                        ImplicitOp, LV))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, -1);

    break;
  }
  case X86::DEC16r:
    return convertToThreeAddressWithLEA(MIOpc, MFI, MI, LV);
  case X86::ADD64rr:
  case X86::ADD64rr_DB:
  case X86::ADD32rr:
  case X86::ADD32rr_DB: {
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    unsigned Opc;
    if (MIOpc == X86::ADD64rr || MIOpc == X86::ADD64rr_DB)
      Opc = X86::LEA64r;
    else
      Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    bool isKill;
    unsigned SrcReg;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/ true,
                        SrcReg, isKill, ImplicitOp, LV))
      return nullptr;

    const MachineOperand &Src2 = MI.getOperand(2);
    bool isKill2;
    unsigned SrcReg2;
    MachineOperand ImplicitOp2 = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src2, Opc, /*AllowSP=*/ false,
                        SrcReg2, isKill2, ImplicitOp2, LV))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc)).add(Dest);
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);
    if (ImplicitOp2.getReg() != 0)
      MIB.add(ImplicitOp2);

    NewMI = addRegReg(MIB, SrcReg, isKill, SrcReg2, isKill2);
    if (LV && Src2.isKill())
      LV->replaceKillInstruction(SrcReg2, MI, *NewMI);
    break;
  }
  case X86::ADD8rr:
  case X86::ADD16rr:
  case X86::ADD16rr_DB:
    return convertToThreeAddressWithLEA(MIOpc, MFI, MI, LV);
  case X86::ADD64ri32:
  case X86::ADD64ri8:
  case X86::ADD64ri32_DB:
  case X86::ADD64ri8_DB:
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    NewMI = addOffset(
        BuildMI(MF, MI.getDebugLoc(), get(X86::LEA64r)).add(Dest).add(Src),
        MI.getOperand(2));
    break;
  case X86::ADD32ri:
  case X86::ADD32ri8:
  case X86::ADD32ri_DB:
  case X86::ADD32ri8_DB: {
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    unsigned Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    bool isKill;
    unsigned SrcReg;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/ true,
                        SrcReg, isKill, ImplicitOp, LV))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, MI.getOperand(2));
    break;
  }
  case X86::ADD8ri:
  case X86::ADD16ri:
  case X86::ADD16ri8:
  case X86::ADD16ri_DB:
  case X86::ADD16ri8_DB:
    return convertToThreeAddressWithLEA(MIOpc, MFI, MI, LV);
  case X86::VMOVDQU8Z128rmk:
  case X86::VMOVDQU8Z256rmk:
  case X86::VMOVDQU8Zrmk:
  case X86::VMOVDQU16Z128rmk:
  case X86::VMOVDQU16Z256rmk:
  case X86::VMOVDQU16Zrmk:
  case X86::VMOVDQU32Z128rmk: case X86::VMOVDQA32Z128rmk:
  case X86::VMOVDQU32Z256rmk: case X86::VMOVDQA32Z256rmk:
  case X86::VMOVDQU32Zrmk:    case X86::VMOVDQA32Zrmk:
  case X86::VMOVDQU64Z128rmk: case X86::VMOVDQA64Z128rmk:
  case X86::VMOVDQU64Z256rmk: case X86::VMOVDQA64Z256rmk:
  case X86::VMOVDQU64Zrmk:    case X86::VMOVDQA64Zrmk:
  case X86::VMOVUPDZ128rmk:   case X86::VMOVAPDZ128rmk:
  case X86::VMOVUPDZ256rmk:   case X86::VMOVAPDZ256rmk:
  case X86::VMOVUPDZrmk:      case X86::VMOVAPDZrmk:
  case X86::VMOVUPSZ128rmk:   case X86::VMOVAPSZ128rmk:
  case X86::VMOVUPSZ256rmk:   case X86::VMOVAPSZ256rmk:
  case X86::VMOVUPSZrmk:      case X86::VMOVAPSZrmk: {
    unsigned Opc;
    switch (MIOpc) {
    default: llvm_unreachable("Unreachable!");
    case X86::VMOVDQU8Z128rmk:  Opc = X86::VPBLENDMBZ128rmk; break;
    case X86::VMOVDQU8Z256rmk:  Opc = X86::VPBLENDMBZ256rmk; break;
    case X86::VMOVDQU8Zrmk:     Opc = X86::VPBLENDMBZrmk;    break;
    case X86::VMOVDQU16Z128rmk: Opc = X86::VPBLENDMWZ128rmk; break;
    case X86::VMOVDQU16Z256rmk: Opc = X86::VPBLENDMWZ256rmk; break;
    case X86::VMOVDQU16Zrmk:    Opc = X86::VPBLENDMWZrmk;    break;
    case X86::VMOVDQU32Z128rmk: Opc = X86::VPBLENDMDZ128rmk; break;
    case X86::VMOVDQU32Z256rmk: Opc = X86::VPBLENDMDZ256rmk; break;
    case X86::VMOVDQU32Zrmk:    Opc = X86::VPBLENDMDZrmk;    break;
    case X86::VMOVDQU64Z128rmk: Opc = X86::VPBLENDMQZ128rmk; break;
    case X86::VMOVDQU64Z256rmk: Opc = X86::VPBLENDMQZ256rmk; break;
    case X86::VMOVDQU64Zrmk:    Opc = X86::VPBLENDMQZrmk;    break;
    case X86::VMOVUPDZ128rmk:   Opc = X86::VBLENDMPDZ128rmk; break;
    case X86::VMOVUPDZ256rmk:   Opc = X86::VBLENDMPDZ256rmk; break;
    case X86::VMOVUPDZrmk:      Opc = X86::VBLENDMPDZrmk;    break;
    case X86::VMOVUPSZ128rmk:   Opc = X86::VBLENDMPSZ128rmk; break;
    case X86::VMOVUPSZ256rmk:   Opc = X86::VBLENDMPSZ256rmk; break;
    case X86::VMOVUPSZrmk:      Opc = X86::VBLENDMPSZrmk;    break;
    case X86::VMOVDQA32Z128rmk: Opc = X86::VPBLENDMDZ128rmk; break;
    case X86::VMOVDQA32Z256rmk: Opc = X86::VPBLENDMDZ256rmk; break;
    case X86::VMOVDQA32Zrmk:    Opc = X86::VPBLENDMDZrmk;    break;
    case X86::VMOVDQA64Z128rmk: Opc = X86::VPBLENDMQZ128rmk; break;
    case X86::VMOVDQA64Z256rmk: Opc = X86::VPBLENDMQZ256rmk; break;
    case X86::VMOVDQA64Zrmk:    Opc = X86::VPBLENDMQZrmk;    break;
    case X86::VMOVAPDZ128rmk:   Opc = X86::VBLENDMPDZ128rmk; break;
    case X86::VMOVAPDZ256rmk:   Opc = X86::VBLENDMPDZ256rmk; break;
    case X86::VMOVAPDZrmk:      Opc = X86::VBLENDMPDZrmk;    break;
    case X86::VMOVAPSZ128rmk:   Opc = X86::VBLENDMPSZ128rmk; break;
    case X86::VMOVAPSZ256rmk:   Opc = X86::VBLENDMPSZ256rmk; break;
    case X86::VMOVAPSZrmk:      Opc = X86::VBLENDMPSZrmk;    break;
    }

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(Opc))
              .add(Dest)
              .add(MI.getOperand(2))
              .add(Src)
              .add(MI.getOperand(3))
              .add(MI.getOperand(4))
              .add(MI.getOperand(5))
              .add(MI.getOperand(6))
              .add(MI.getOperand(7));
    break;
  }
  case X86::VMOVDQU8Z128rrk:
  case X86::VMOVDQU8Z256rrk:
  case X86::VMOVDQU8Zrrk:
  case X86::VMOVDQU16Z128rrk:
  case X86::VMOVDQU16Z256rrk:
  case X86::VMOVDQU16Zrrk:
  case X86::VMOVDQU32Z128rrk: case X86::VMOVDQA32Z128rrk:
  case X86::VMOVDQU32Z256rrk: case X86::VMOVDQA32Z256rrk:
  case X86::VMOVDQU32Zrrk:    case X86::VMOVDQA32Zrrk:
  case X86::VMOVDQU64Z128rrk: case X86::VMOVDQA64Z128rrk:
  case X86::VMOVDQU64Z256rrk: case X86::VMOVDQA64Z256rrk:
  case X86::VMOVDQU64Zrrk:    case X86::VMOVDQA64Zrrk:
  case X86::VMOVUPDZ128rrk:   case X86::VMOVAPDZ128rrk:
  case X86::VMOVUPDZ256rrk:   case X86::VMOVAPDZ256rrk:
  case X86::VMOVUPDZrrk:      case X86::VMOVAPDZrrk:
  case X86::VMOVUPSZ128rrk:   case X86::VMOVAPSZ128rrk:
  case X86::VMOVUPSZ256rrk:   case X86::VMOVAPSZ256rrk:
  case X86::VMOVUPSZrrk:      case X86::VMOVAPSZrrk: {
    unsigned Opc;
    switch (MIOpc) {
    default: llvm_unreachable("Unreachable!");
    case X86::VMOVDQU8Z128rrk:  Opc = X86::VPBLENDMBZ128rrk; break;
    case X86::VMOVDQU8Z256rrk:  Opc = X86::VPBLENDMBZ256rrk; break;
    case X86::VMOVDQU8Zrrk:     Opc = X86::VPBLENDMBZrrk;    break;
    case X86::VMOVDQU16Z128rrk: Opc = X86::VPBLENDMWZ128rrk; break;
    case X86::VMOVDQU16Z256rrk: Opc = X86::VPBLENDMWZ256rrk; break;
    case X86::VMOVDQU16Zrrk:    Opc = X86::VPBLENDMWZrrk;    break;
    case X86::VMOVDQU32Z128rrk: Opc = X86::VPBLENDMDZ128rrk; break;
    case X86::VMOVDQU32Z256rrk: Opc = X86::VPBLENDMDZ256rrk; break;
    case X86::VMOVDQU32Zrrk:    Opc = X86::VPBLENDMDZrrk;    break;
    case X86::VMOVDQU64Z128rrk: Opc = X86::VPBLENDMQZ128rrk; break;
    case X86::VMOVDQU64Z256rrk: Opc = X86::VPBLENDMQZ256rrk; break;
    case X86::VMOVDQU64Zrrk:    Opc = X86::VPBLENDMQZrrk;    break;
    case X86::VMOVUPDZ128rrk:   Opc = X86::VBLENDMPDZ128rrk; break;
    case X86::VMOVUPDZ256rrk:   Opc = X86::VBLENDMPDZ256rrk; break;
    case X86::VMOVUPDZrrk:      Opc = X86::VBLENDMPDZrrk;    break;
    case X86::VMOVUPSZ128rrk:   Opc = X86::VBLENDMPSZ128rrk; break;
    case X86::VMOVUPSZ256rrk:   Opc = X86::VBLENDMPSZ256rrk; break;
    case X86::VMOVUPSZrrk:      Opc = X86::VBLENDMPSZrrk;    break;
    case X86::VMOVDQA32Z128rrk: Opc = X86::VPBLENDMDZ128rrk; break;
    case X86::VMOVDQA32Z256rrk: Opc = X86::VPBLENDMDZ256rrk; break;
    case X86::VMOVDQA32Zrrk:    Opc = X86::VPBLENDMDZrrk;    break;
    case X86::VMOVDQA64Z128rrk: Opc = X86::VPBLENDMQZ128rrk; break;
    case X86::VMOVDQA64Z256rrk: Opc = X86::VPBLENDMQZ256rrk; break;
    case X86::VMOVDQA64Zrrk:    Opc = X86::VPBLENDMQZrrk;    break;
    case X86::VMOVAPDZ128rrk:   Opc = X86::VBLENDMPDZ128rrk; break;
    case X86::VMOVAPDZ256rrk:   Opc = X86::VBLENDMPDZ256rrk; break;
    case X86::VMOVAPDZrrk:      Opc = X86::VBLENDMPDZrrk;    break;
    case X86::VMOVAPSZ128rrk:   Opc = X86::VBLENDMPSZ128rrk; break;
    case X86::VMOVAPSZ256rrk:   Opc = X86::VBLENDMPSZ256rrk; break;
    case X86::VMOVAPSZrrk:      Opc = X86::VBLENDMPSZrrk;    break;
    }

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(Opc))
              .add(Dest)
              .add(MI.getOperand(2))
              .add(Src)
              .add(MI.getOperand(3));
    break;
  }
  }

  if (!NewMI) return nullptr;

  if (LV) {  // Update live variables
    if (Src.isKill())
      LV->replaceKillInstruction(Src.getReg(), MI, *NewMI);
    if (Dest.isDead())
      LV->replaceKillInstruction(Dest.getReg(), MI, *NewMI);
  }

  MFI->insert(MI.getIterator(), NewMI); // Insert the new inst
  return NewMI;
}

/// This determines which of three possible cases of a three source commute
/// the source indexes correspond to taking into account any mask operands.
/// All prevents commuting a passthru operand. Returns -1 if the commute isn't
/// possible.
/// Case 0 - Possible to commute the first and second operands.
/// Case 1 - Possible to commute the first and third operands.
/// Case 2 - Possible to commute the second and third operands.
static unsigned getThreeSrcCommuteCase(uint64_t TSFlags, unsigned SrcOpIdx1,
                                       unsigned SrcOpIdx2) {
  // Put the lowest index to SrcOpIdx1 to simplify the checks below.
  if (SrcOpIdx1 > SrcOpIdx2)
    std::swap(SrcOpIdx1, SrcOpIdx2);

  unsigned Op1 = 1, Op2 = 2, Op3 = 3;
  if (X86II::isKMasked(TSFlags)) {
    Op2++;
    Op3++;
  }

  if (SrcOpIdx1 == Op1 && SrcOpIdx2 == Op2)
    return 0;
  if (SrcOpIdx1 == Op1 && SrcOpIdx2 == Op3)
    return 1;
  if (SrcOpIdx1 == Op2 && SrcOpIdx2 == Op3)
    return 2;
  llvm_unreachable("Unknown three src commute case.");
}

unsigned X86InstrInfo::getFMA3OpcodeToCommuteOperands(
    const MachineInstr &MI, unsigned SrcOpIdx1, unsigned SrcOpIdx2,
    const X86InstrFMA3Group &FMA3Group) const {

  unsigned Opc = MI.getOpcode();

  // TODO: Commuting the 1st operand of FMA*_Int requires some additional
  // analysis. The commute optimization is legal only if all users of FMA*_Int
  // use only the lowest element of the FMA*_Int instruction. Such analysis are
  // not implemented yet. So, just return 0 in that case.
  // When such analysis are available this place will be the right place for
  // calling it.
  assert(!(FMA3Group.isIntrinsic() && (SrcOpIdx1 == 1 || SrcOpIdx2 == 1)) &&
         "Intrinsic instructions can't commute operand 1");

  // Determine which case this commute is or if it can't be done.
  unsigned Case = getThreeSrcCommuteCase(MI.getDesc().TSFlags, SrcOpIdx1,
                                         SrcOpIdx2);
  assert(Case < 3 && "Unexpected case number!");

  // Define the FMA forms mapping array that helps to map input FMA form
  // to output FMA form to preserve the operation semantics after
  // commuting the operands.
  const unsigned Form132Index = 0;
  const unsigned Form213Index = 1;
  const unsigned Form231Index = 2;
  static const unsigned FormMapping[][3] = {
    // 0: SrcOpIdx1 == 1 && SrcOpIdx2 == 2;
    // FMA132 A, C, b; ==> FMA231 C, A, b;
    // FMA213 B, A, c; ==> FMA213 A, B, c;
    // FMA231 C, A, b; ==> FMA132 A, C, b;
    { Form231Index, Form213Index, Form132Index },
    // 1: SrcOpIdx1 == 1 && SrcOpIdx2 == 3;
    // FMA132 A, c, B; ==> FMA132 B, c, A;
    // FMA213 B, a, C; ==> FMA231 C, a, B;
    // FMA231 C, a, B; ==> FMA213 B, a, C;
    { Form132Index, Form231Index, Form213Index },
    // 2: SrcOpIdx1 == 2 && SrcOpIdx2 == 3;
    // FMA132 a, C, B; ==> FMA213 a, B, C;
    // FMA213 b, A, C; ==> FMA132 b, C, A;
    // FMA231 c, A, B; ==> FMA231 c, B, A;
    { Form213Index, Form132Index, Form231Index }
  };

  unsigned FMAForms[3];
  FMAForms[0] = FMA3Group.get132Opcode();
  FMAForms[1] = FMA3Group.get213Opcode();
  FMAForms[2] = FMA3Group.get231Opcode();
  unsigned FormIndex;
  for (FormIndex = 0; FormIndex < 3; FormIndex++)
    if (Opc == FMAForms[FormIndex])
      break;

  // Everything is ready, just adjust the FMA opcode and return it.
  FormIndex = FormMapping[Case][FormIndex];
  return FMAForms[FormIndex];
}

static void commuteVPTERNLOG(MachineInstr &MI, unsigned SrcOpIdx1,
                             unsigned SrcOpIdx2) {
  // Determine which case this commute is or if it can't be done.
  unsigned Case = getThreeSrcCommuteCase(MI.getDesc().TSFlags, SrcOpIdx1,
                                         SrcOpIdx2);
  assert(Case < 3 && "Unexpected case value!");

  // For each case we need to swap two pairs of bits in the final immediate.
  static const uint8_t SwapMasks[3][4] = {
    { 0x04, 0x10, 0x08, 0x20 }, // Swap bits 2/4 and 3/5.
    { 0x02, 0x10, 0x08, 0x40 }, // Swap bits 1/4 and 3/6.
    { 0x02, 0x04, 0x20, 0x40 }, // Swap bits 1/2 and 5/6.
  };

  uint8_t Imm = MI.getOperand(MI.getNumOperands()-1).getImm();
  // Clear out the bits we are swapping.
  uint8_t NewImm = Imm & ~(SwapMasks[Case][0] | SwapMasks[Case][1] |
                           SwapMasks[Case][2] | SwapMasks[Case][3]);
  // If the immediate had a bit of the pair set, then set the opposite bit.
  if (Imm & SwapMasks[Case][0]) NewImm |= SwapMasks[Case][1];
  if (Imm & SwapMasks[Case][1]) NewImm |= SwapMasks[Case][0];
  if (Imm & SwapMasks[Case][2]) NewImm |= SwapMasks[Case][3];
  if (Imm & SwapMasks[Case][3]) NewImm |= SwapMasks[Case][2];
  MI.getOperand(MI.getNumOperands()-1).setImm(NewImm);
}

// Returns true if this is a VPERMI2 or VPERMT2 instruction that can be
// commuted.
static bool isCommutableVPERMV3Instruction(unsigned Opcode) {
#define VPERM_CASES(Suffix) \
  case X86::VPERMI2##Suffix##128rr:    case X86::VPERMT2##Suffix##128rr:    \
  case X86::VPERMI2##Suffix##256rr:    case X86::VPERMT2##Suffix##256rr:    \
  case X86::VPERMI2##Suffix##rr:       case X86::VPERMT2##Suffix##rr:       \
  case X86::VPERMI2##Suffix##128rm:    case X86::VPERMT2##Suffix##128rm:    \
  case X86::VPERMI2##Suffix##256rm:    case X86::VPERMT2##Suffix##256rm:    \
  case X86::VPERMI2##Suffix##rm:       case X86::VPERMT2##Suffix##rm:       \
  case X86::VPERMI2##Suffix##128rrkz:  case X86::VPERMT2##Suffix##128rrkz:  \
  case X86::VPERMI2##Suffix##256rrkz:  case X86::VPERMT2##Suffix##256rrkz:  \
  case X86::VPERMI2##Suffix##rrkz:     case X86::VPERMT2##Suffix##rrkz:     \
  case X86::VPERMI2##Suffix##128rmkz:  case X86::VPERMT2##Suffix##128rmkz:  \
  case X86::VPERMI2##Suffix##256rmkz:  case X86::VPERMT2##Suffix##256rmkz:  \
  case X86::VPERMI2##Suffix##rmkz:     case X86::VPERMT2##Suffix##rmkz:

#define VPERM_CASES_BROADCAST(Suffix) \
  VPERM_CASES(Suffix) \
  case X86::VPERMI2##Suffix##128rmb:   case X86::VPERMT2##Suffix##128rmb:   \
  case X86::VPERMI2##Suffix##256rmb:   case X86::VPERMT2##Suffix##256rmb:   \
  case X86::VPERMI2##Suffix##rmb:      case X86::VPERMT2##Suffix##rmb:      \
  case X86::VPERMI2##Suffix##128rmbkz: case X86::VPERMT2##Suffix##128rmbkz: \
  case X86::VPERMI2##Suffix##256rmbkz: case X86::VPERMT2##Suffix##256rmbkz: \
  case X86::VPERMI2##Suffix##rmbkz:    case X86::VPERMT2##Suffix##rmbkz:

  switch (Opcode) {
  default: return false;
  VPERM_CASES(B)
  VPERM_CASES_BROADCAST(D)
  VPERM_CASES_BROADCAST(PD)
  VPERM_CASES_BROADCAST(PS)
  VPERM_CASES_BROADCAST(Q)
  VPERM_CASES(W)
    return true;
  }
#undef VPERM_CASES_BROADCAST
#undef VPERM_CASES
}

// Returns commuted opcode for VPERMI2 and VPERMT2 instructions by switching
// from the I opcode to the T opcode and vice versa.
static unsigned getCommutedVPERMV3Opcode(unsigned Opcode) {
#define VPERM_CASES(Orig, New) \
  case X86::Orig##128rr:    return X86::New##128rr;   \
  case X86::Orig##128rrkz:  return X86::New##128rrkz; \
  case X86::Orig##128rm:    return X86::New##128rm;   \
  case X86::Orig##128rmkz:  return X86::New##128rmkz; \
  case X86::Orig##256rr:    return X86::New##256rr;   \
  case X86::Orig##256rrkz:  return X86::New##256rrkz; \
  case X86::Orig##256rm:    return X86::New##256rm;   \
  case X86::Orig##256rmkz:  return X86::New##256rmkz; \
  case X86::Orig##rr:       return X86::New##rr;      \
  case X86::Orig##rrkz:     return X86::New##rrkz;    \
  case X86::Orig##rm:       return X86::New##rm;      \
  case X86::Orig##rmkz:     return X86::New##rmkz;

#define VPERM_CASES_BROADCAST(Orig, New) \
  VPERM_CASES(Orig, New) \
  case X86::Orig##128rmb:   return X86::New##128rmb;   \
  case X86::Orig##128rmbkz: return X86::New##128rmbkz; \
  case X86::Orig##256rmb:   return X86::New##256rmb;   \
  case X86::Orig##256rmbkz: return X86::New##256rmbkz; \
  case X86::Orig##rmb:      return X86::New##rmb;      \
  case X86::Orig##rmbkz:    return X86::New##rmbkz;

  switch (Opcode) {
  VPERM_CASES(VPERMI2B, VPERMT2B)
  VPERM_CASES_BROADCAST(VPERMI2D,  VPERMT2D)
  VPERM_CASES_BROADCAST(VPERMI2PD, VPERMT2PD)
  VPERM_CASES_BROADCAST(VPERMI2PS, VPERMT2PS)
  VPERM_CASES_BROADCAST(VPERMI2Q,  VPERMT2Q)
  VPERM_CASES(VPERMI2W, VPERMT2W)
  VPERM_CASES(VPERMT2B, VPERMI2B)
  VPERM_CASES_BROADCAST(VPERMT2D,  VPERMI2D)
  VPERM_CASES_BROADCAST(VPERMT2PD, VPERMI2PD)
  VPERM_CASES_BROADCAST(VPERMT2PS, VPERMI2PS)
  VPERM_CASES_BROADCAST(VPERMT2Q,  VPERMI2Q)
  VPERM_CASES(VPERMT2W, VPERMI2W)
  }

  llvm_unreachable("Unreachable!");
#undef VPERM_CASES_BROADCAST
#undef VPERM_CASES
}

MachineInstr *X86InstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                   unsigned OpIdx1,
                                                   unsigned OpIdx2) const {
  auto cloneIfNew = [NewMI](MachineInstr &MI) -> MachineInstr & {
    if (NewMI)
      return *MI.getParent()->getParent()->CloneMachineInstr(&MI);
    return MI;
  };

  switch (MI.getOpcode()) {
  case X86::SHRD16rri8: // A = SHRD16rri8 B, C, I -> A = SHLD16rri8 C, B, (16-I)
  case X86::SHLD16rri8: // A = SHLD16rri8 B, C, I -> A = SHRD16rri8 C, B, (16-I)
  case X86::SHRD32rri8: // A = SHRD32rri8 B, C, I -> A = SHLD32rri8 C, B, (32-I)
  case X86::SHLD32rri8: // A = SHLD32rri8 B, C, I -> A = SHRD32rri8 C, B, (32-I)
  case X86::SHRD64rri8: // A = SHRD64rri8 B, C, I -> A = SHLD64rri8 C, B, (64-I)
  case X86::SHLD64rri8:{// A = SHLD64rri8 B, C, I -> A = SHRD64rri8 C, B, (64-I)
    unsigned Opc;
    unsigned Size;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::SHRD16rri8: Size = 16; Opc = X86::SHLD16rri8; break;
    case X86::SHLD16rri8: Size = 16; Opc = X86::SHRD16rri8; break;
    case X86::SHRD32rri8: Size = 32; Opc = X86::SHLD32rri8; break;
    case X86::SHLD32rri8: Size = 32; Opc = X86::SHRD32rri8; break;
    case X86::SHRD64rri8: Size = 64; Opc = X86::SHLD64rri8; break;
    case X86::SHLD64rri8: Size = 64; Opc = X86::SHRD64rri8; break;
    }
    unsigned Amt = MI.getOperand(3).getImm();
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.setDesc(get(Opc));
    WorkingMI.getOperand(3).setImm(Size - Amt);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::PFSUBrr:
  case X86::PFSUBRrr: {
    // PFSUB  x, y: x = x - y
    // PFSUBR x, y: x = y - x
    unsigned Opc =
        (X86::PFSUBRrr == MI.getOpcode() ? X86::PFSUBrr : X86::PFSUBRrr);
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.setDesc(get(Opc));
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::BLENDPDrri:
  case X86::BLENDPSrri:
  case X86::VBLENDPDrri:
  case X86::VBLENDPSrri:
    // If we're optimizing for size, try to use MOVSD/MOVSS.
    if (MI.getParent()->getParent()->getFunction().optForSize()) {
      unsigned Mask, Opc;
      switch (MI.getOpcode()) {
      default: llvm_unreachable("Unreachable!");
      case X86::BLENDPDrri:  Opc = X86::MOVSDrr;  Mask = 0x03; break;
      case X86::BLENDPSrri:  Opc = X86::MOVSSrr;  Mask = 0x0F; break;
      case X86::VBLENDPDrri: Opc = X86::VMOVSDrr; Mask = 0x03; break;
      case X86::VBLENDPSrri: Opc = X86::VMOVSSrr; Mask = 0x0F; break;
      }
      if ((MI.getOperand(3).getImm() ^ Mask) == 1) {
        auto &WorkingMI = cloneIfNew(MI);
        WorkingMI.setDesc(get(Opc));
        WorkingMI.RemoveOperand(3);
        return TargetInstrInfo::commuteInstructionImpl(WorkingMI,
                                                       /*NewMI=*/false,
                                                       OpIdx1, OpIdx2);
      }
    }
    LLVM_FALLTHROUGH;
  case X86::PBLENDWrri:
  case X86::VBLENDPDYrri:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDrri:
  case X86::VPBLENDWrri:
  case X86::VPBLENDDYrri:
  case X86::VPBLENDWYrri:{
    unsigned Mask;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::BLENDPDrri:    Mask = 0x03; break;
    case X86::BLENDPSrri:    Mask = 0x0F; break;
    case X86::PBLENDWrri:    Mask = 0xFF; break;
    case X86::VBLENDPDrri:   Mask = 0x03; break;
    case X86::VBLENDPSrri:   Mask = 0x0F; break;
    case X86::VBLENDPDYrri:  Mask = 0x0F; break;
    case X86::VBLENDPSYrri:  Mask = 0xFF; break;
    case X86::VPBLENDDrri:   Mask = 0x0F; break;
    case X86::VPBLENDWrri:   Mask = 0xFF; break;
    case X86::VPBLENDDYrri:  Mask = 0xFF; break;
    case X86::VPBLENDWYrri:  Mask = 0xFF; break;
    }
    // Only the least significant bits of Imm are used.
    unsigned Imm = MI.getOperand(3).getImm() & Mask;
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.getOperand(3).setImm(Mask ^ Imm);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::MOVSDrr:
  case X86::MOVSSrr:
  case X86::VMOVSDrr:
  case X86::VMOVSSrr:{
    // On SSE41 or later we can commute a MOVSS/MOVSD to a BLENDPS/BLENDPD.
    assert(Subtarget.hasSSE41() && "Commuting MOVSD/MOVSS requires SSE41!");

    unsigned Mask, Opc;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::MOVSDrr:  Opc = X86::BLENDPDrri;  Mask = 0x02; break;
    case X86::MOVSSrr:  Opc = X86::BLENDPSrri;  Mask = 0x0E; break;
    case X86::VMOVSDrr: Opc = X86::VBLENDPDrri; Mask = 0x02; break;
    case X86::VMOVSSrr: Opc = X86::VBLENDPSrri; Mask = 0x0E; break;
    }

    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.setDesc(get(Opc));
    WorkingMI.addOperand(MachineOperand::CreateImm(Mask));
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::PCLMULQDQrr:
  case X86::VPCLMULQDQrr:
  case X86::VPCLMULQDQYrr:
  case X86::VPCLMULQDQZrr:
  case X86::VPCLMULQDQZ128rr:
  case X86::VPCLMULQDQZ256rr: {
    // SRC1 64bits = Imm[0] ? SRC1[127:64] : SRC1[63:0]
    // SRC2 64bits = Imm[4] ? SRC2[127:64] : SRC2[63:0]
    unsigned Imm = MI.getOperand(3).getImm();
    unsigned Src1Hi = Imm & 0x01;
    unsigned Src2Hi = Imm & 0x10;
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.getOperand(3).setImm((Src1Hi << 4) | (Src2Hi >> 4));
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::VPCMPBZ128rri:  case X86::VPCMPUBZ128rri:
  case X86::VPCMPBZ256rri:  case X86::VPCMPUBZ256rri:
  case X86::VPCMPBZrri:     case X86::VPCMPUBZrri:
  case X86::VPCMPDZ128rri:  case X86::VPCMPUDZ128rri:
  case X86::VPCMPDZ256rri:  case X86::VPCMPUDZ256rri:
  case X86::VPCMPDZrri:     case X86::VPCMPUDZrri:
  case X86::VPCMPQZ128rri:  case X86::VPCMPUQZ128rri:
  case X86::VPCMPQZ256rri:  case X86::VPCMPUQZ256rri:
  case X86::VPCMPQZrri:     case X86::VPCMPUQZrri:
  case X86::VPCMPWZ128rri:  case X86::VPCMPUWZ128rri:
  case X86::VPCMPWZ256rri:  case X86::VPCMPUWZ256rri:
  case X86::VPCMPWZrri:     case X86::VPCMPUWZrri:
  case X86::VPCMPBZ128rrik: case X86::VPCMPUBZ128rrik:
  case X86::VPCMPBZ256rrik: case X86::VPCMPUBZ256rrik:
  case X86::VPCMPBZrrik:    case X86::VPCMPUBZrrik:
  case X86::VPCMPDZ128rrik: case X86::VPCMPUDZ128rrik:
  case X86::VPCMPDZ256rrik: case X86::VPCMPUDZ256rrik:
  case X86::VPCMPDZrrik:    case X86::VPCMPUDZrrik:
  case X86::VPCMPQZ128rrik: case X86::VPCMPUQZ128rrik:
  case X86::VPCMPQZ256rrik: case X86::VPCMPUQZ256rrik:
  case X86::VPCMPQZrrik:    case X86::VPCMPUQZrrik:
  case X86::VPCMPWZ128rrik: case X86::VPCMPUWZ128rrik:
  case X86::VPCMPWZ256rrik: case X86::VPCMPUWZ256rrik:
  case X86::VPCMPWZrrik:    case X86::VPCMPUWZrrik: {
    // Flip comparison mode immediate (if necessary).
    unsigned Imm = MI.getOperand(MI.getNumOperands() - 1).getImm() & 0x7;
    Imm = X86::getSwappedVPCMPImm(Imm);
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.getOperand(MI.getNumOperands() - 1).setImm(Imm);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::VPCOMBri: case X86::VPCOMUBri:
  case X86::VPCOMDri: case X86::VPCOMUDri:
  case X86::VPCOMQri: case X86::VPCOMUQri:
  case X86::VPCOMWri: case X86::VPCOMUWri: {
    // Flip comparison mode immediate (if necessary).
    unsigned Imm = MI.getOperand(3).getImm() & 0x7;
    Imm = X86::getSwappedVPCOMImm(Imm);
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.getOperand(3).setImm(Imm);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::VPERM2F128rr:
  case X86::VPERM2I128rr: {
    // Flip permute source immediate.
    // Imm & 0x02: lo = if set, select Op1.lo/hi else Op0.lo/hi.
    // Imm & 0x20: hi = if set, select Op1.lo/hi else Op0.lo/hi.
    unsigned Imm = MI.getOperand(3).getImm() & 0xFF;
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.getOperand(3).setImm(Imm ^ 0x22);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::MOVHLPSrr:
  case X86::UNPCKHPDrr:
  case X86::VMOVHLPSrr:
  case X86::VUNPCKHPDrr:
  case X86::VMOVHLPSZrr:
  case X86::VUNPCKHPDZ128rr: {
    assert(Subtarget.hasSSE2() && "Commuting MOVHLP/UNPCKHPD requires SSE2!");

    unsigned Opc = MI.getOpcode();
    switch (Opc) {
    default: llvm_unreachable("Unreachable!");
    case X86::MOVHLPSrr:       Opc = X86::UNPCKHPDrr;      break;
    case X86::UNPCKHPDrr:      Opc = X86::MOVHLPSrr;       break;
    case X86::VMOVHLPSrr:      Opc = X86::VUNPCKHPDrr;     break;
    case X86::VUNPCKHPDrr:     Opc = X86::VMOVHLPSrr;      break;
    case X86::VMOVHLPSZrr:     Opc = X86::VUNPCKHPDZ128rr; break;
    case X86::VUNPCKHPDZ128rr: Opc = X86::VMOVHLPSZrr;     break;
    }
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.setDesc(get(Opc));
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::CMOVB16rr:  case X86::CMOVB32rr:  case X86::CMOVB64rr:
  case X86::CMOVAE16rr: case X86::CMOVAE32rr: case X86::CMOVAE64rr:
  case X86::CMOVE16rr:  case X86::CMOVE32rr:  case X86::CMOVE64rr:
  case X86::CMOVNE16rr: case X86::CMOVNE32rr: case X86::CMOVNE64rr:
  case X86::CMOVBE16rr: case X86::CMOVBE32rr: case X86::CMOVBE64rr:
  case X86::CMOVA16rr:  case X86::CMOVA32rr:  case X86::CMOVA64rr:
  case X86::CMOVL16rr:  case X86::CMOVL32rr:  case X86::CMOVL64rr:
  case X86::CMOVGE16rr: case X86::CMOVGE32rr: case X86::CMOVGE64rr:
  case X86::CMOVLE16rr: case X86::CMOVLE32rr: case X86::CMOVLE64rr:
  case X86::CMOVG16rr:  case X86::CMOVG32rr:  case X86::CMOVG64rr:
  case X86::CMOVS16rr:  case X86::CMOVS32rr:  case X86::CMOVS64rr:
  case X86::CMOVNS16rr: case X86::CMOVNS32rr: case X86::CMOVNS64rr:
  case X86::CMOVP16rr:  case X86::CMOVP32rr:  case X86::CMOVP64rr:
  case X86::CMOVNP16rr: case X86::CMOVNP32rr: case X86::CMOVNP64rr:
  case X86::CMOVO16rr:  case X86::CMOVO32rr:  case X86::CMOVO64rr:
  case X86::CMOVNO16rr: case X86::CMOVNO32rr: case X86::CMOVNO64rr: {
    unsigned Opc;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::CMOVB16rr:  Opc = X86::CMOVAE16rr; break;
    case X86::CMOVB32rr:  Opc = X86::CMOVAE32rr; break;
    case X86::CMOVB64rr:  Opc = X86::CMOVAE64rr; break;
    case X86::CMOVAE16rr: Opc = X86::CMOVB16rr; break;
    case X86::CMOVAE32rr: Opc = X86::CMOVB32rr; break;
    case X86::CMOVAE64rr: Opc = X86::CMOVB64rr; break;
    case X86::CMOVE16rr:  Opc = X86::CMOVNE16rr; break;
    case X86::CMOVE32rr:  Opc = X86::CMOVNE32rr; break;
    case X86::CMOVE64rr:  Opc = X86::CMOVNE64rr; break;
    case X86::CMOVNE16rr: Opc = X86::CMOVE16rr; break;
    case X86::CMOVNE32rr: Opc = X86::CMOVE32rr; break;
    case X86::CMOVNE64rr: Opc = X86::CMOVE64rr; break;
    case X86::CMOVBE16rr: Opc = X86::CMOVA16rr; break;
    case X86::CMOVBE32rr: Opc = X86::CMOVA32rr; break;
    case X86::CMOVBE64rr: Opc = X86::CMOVA64rr; break;
    case X86::CMOVA16rr:  Opc = X86::CMOVBE16rr; break;
    case X86::CMOVA32rr:  Opc = X86::CMOVBE32rr; break;
    case X86::CMOVA64rr:  Opc = X86::CMOVBE64rr; break;
    case X86::CMOVL16rr:  Opc = X86::CMOVGE16rr; break;
    case X86::CMOVL32rr:  Opc = X86::CMOVGE32rr; break;
    case X86::CMOVL64rr:  Opc = X86::CMOVGE64rr; break;
    case X86::CMOVGE16rr: Opc = X86::CMOVL16rr; break;
    case X86::CMOVGE32rr: Opc = X86::CMOVL32rr; break;
    case X86::CMOVGE64rr: Opc = X86::CMOVL64rr; break;
    case X86::CMOVLE16rr: Opc = X86::CMOVG16rr; break;
    case X86::CMOVLE32rr: Opc = X86::CMOVG32rr; break;
    case X86::CMOVLE64rr: Opc = X86::CMOVG64rr; break;
    case X86::CMOVG16rr:  Opc = X86::CMOVLE16rr; break;
    case X86::CMOVG32rr:  Opc = X86::CMOVLE32rr; break;
    case X86::CMOVG64rr:  Opc = X86::CMOVLE64rr; break;
    case X86::CMOVS16rr:  Opc = X86::CMOVNS16rr; break;
    case X86::CMOVS32rr:  Opc = X86::CMOVNS32rr; break;
    case X86::CMOVS64rr:  Opc = X86::CMOVNS64rr; break;
    case X86::CMOVNS16rr: Opc = X86::CMOVS16rr; break;
    case X86::CMOVNS32rr: Opc = X86::CMOVS32rr; break;
    case X86::CMOVNS64rr: Opc = X86::CMOVS64rr; break;
    case X86::CMOVP16rr:  Opc = X86::CMOVNP16rr; break;
    case X86::CMOVP32rr:  Opc = X86::CMOVNP32rr; break;
    case X86::CMOVP64rr:  Opc = X86::CMOVNP64rr; break;
    case X86::CMOVNP16rr: Opc = X86::CMOVP16rr; break;
    case X86::CMOVNP32rr: Opc = X86::CMOVP32rr; break;
    case X86::CMOVNP64rr: Opc = X86::CMOVP64rr; break;
    case X86::CMOVO16rr:  Opc = X86::CMOVNO16rr; break;
    case X86::CMOVO32rr:  Opc = X86::CMOVNO32rr; break;
    case X86::CMOVO64rr:  Opc = X86::CMOVNO64rr; break;
    case X86::CMOVNO16rr: Opc = X86::CMOVO16rr; break;
    case X86::CMOVNO32rr: Opc = X86::CMOVO32rr; break;
    case X86::CMOVNO64rr: Opc = X86::CMOVO64rr; break;
    }
    auto &WorkingMI = cloneIfNew(MI);
    WorkingMI.setDesc(get(Opc));
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  case X86::VPTERNLOGDZrri:      case X86::VPTERNLOGDZrmi:
  case X86::VPTERNLOGDZ128rri:   case X86::VPTERNLOGDZ128rmi:
  case X86::VPTERNLOGDZ256rri:   case X86::VPTERNLOGDZ256rmi:
  case X86::VPTERNLOGQZrri:      case X86::VPTERNLOGQZrmi:
  case X86::VPTERNLOGQZ128rri:   case X86::VPTERNLOGQZ128rmi:
  case X86::VPTERNLOGQZ256rri:   case X86::VPTERNLOGQZ256rmi:
  case X86::VPTERNLOGDZrrik:
  case X86::VPTERNLOGDZ128rrik:
  case X86::VPTERNLOGDZ256rrik:
  case X86::VPTERNLOGQZrrik:
  case X86::VPTERNLOGQZ128rrik:
  case X86::VPTERNLOGQZ256rrik:
  case X86::VPTERNLOGDZrrikz:    case X86::VPTERNLOGDZrmikz:
  case X86::VPTERNLOGDZ128rrikz: case X86::VPTERNLOGDZ128rmikz:
  case X86::VPTERNLOGDZ256rrikz: case X86::VPTERNLOGDZ256rmikz:
  case X86::VPTERNLOGQZrrikz:    case X86::VPTERNLOGQZrmikz:
  case X86::VPTERNLOGQZ128rrikz: case X86::VPTERNLOGQZ128rmikz:
  case X86::VPTERNLOGQZ256rrikz: case X86::VPTERNLOGQZ256rmikz:
  case X86::VPTERNLOGDZ128rmbi:
  case X86::VPTERNLOGDZ256rmbi:
  case X86::VPTERNLOGDZrmbi:
  case X86::VPTERNLOGQZ128rmbi:
  case X86::VPTERNLOGQZ256rmbi:
  case X86::VPTERNLOGQZrmbi:
  case X86::VPTERNLOGDZ128rmbikz:
  case X86::VPTERNLOGDZ256rmbikz:
  case X86::VPTERNLOGDZrmbikz:
  case X86::VPTERNLOGQZ128rmbikz:
  case X86::VPTERNLOGQZ256rmbikz:
  case X86::VPTERNLOGQZrmbikz: {
    auto &WorkingMI = cloneIfNew(MI);
    commuteVPTERNLOG(WorkingMI, OpIdx1, OpIdx2);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  default: {
    if (isCommutableVPERMV3Instruction(MI.getOpcode())) {
      unsigned Opc = getCommutedVPERMV3Opcode(MI.getOpcode());
      auto &WorkingMI = cloneIfNew(MI);
      WorkingMI.setDesc(get(Opc));
      return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                     OpIdx1, OpIdx2);
    }

    const X86InstrFMA3Group *FMA3Group = getFMA3Group(MI.getOpcode(),
                                                      MI.getDesc().TSFlags);
    if (FMA3Group) {
      unsigned Opc =
        getFMA3OpcodeToCommuteOperands(MI, OpIdx1, OpIdx2, *FMA3Group);
      auto &WorkingMI = cloneIfNew(MI);
      WorkingMI.setDesc(get(Opc));
      return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                     OpIdx1, OpIdx2);
    }

    return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  }
  }
}

bool
X86InstrInfo::findThreeSrcCommutedOpIndices(const MachineInstr &MI,
                                            unsigned &SrcOpIdx1,
                                            unsigned &SrcOpIdx2,
                                            bool IsIntrinsic) const {
  uint64_t TSFlags = MI.getDesc().TSFlags;

  unsigned FirstCommutableVecOp = 1;
  unsigned LastCommutableVecOp = 3;
  unsigned KMaskOp = -1U;
  if (X86II::isKMasked(TSFlags)) {
    // For k-zero-masked operations it is Ok to commute the first vector
    // operand.
    // For regular k-masked operations a conservative choice is done as the
    // elements of the first vector operand, for which the corresponding bit
    // in the k-mask operand is set to 0, are copied to the result of the
    // instruction.
    // TODO/FIXME: The commute still may be legal if it is known that the
    // k-mask operand is set to either all ones or all zeroes.
    // It is also Ok to commute the 1st operand if all users of MI use only
    // the elements enabled by the k-mask operand. For example,
    //   v4 = VFMADD213PSZrk v1, k, v2, v3; // v1[i] = k[i] ? v2[i]*v1[i]+v3[i]
    //                                                     : v1[i];
    //   VMOVAPSZmrk <mem_addr>, k, v4; // this is the ONLY user of v4 ->
    //                                  // Ok, to commute v1 in FMADD213PSZrk.

    // The k-mask operand has index = 2 for masked and zero-masked operations.
    KMaskOp = 2;

    // The operand with index = 1 is used as a source for those elements for
    // which the corresponding bit in the k-mask is set to 0.
    if (X86II::isKMergeMasked(TSFlags))
      FirstCommutableVecOp = 3;

    LastCommutableVecOp++;
  } else if (IsIntrinsic) {
    // Commuting the first operand of an intrinsic instruction isn't possible
    // unless we can prove that only the lowest element of the result is used.
    FirstCommutableVecOp = 2;
  }

  if (isMem(MI, LastCommutableVecOp))
    LastCommutableVecOp--;

  // Only the first RegOpsNum operands are commutable.
  // Also, the value 'CommuteAnyOperandIndex' is valid here as it means
  // that the operand is not specified/fixed.
  if (SrcOpIdx1 != CommuteAnyOperandIndex &&
      (SrcOpIdx1 < FirstCommutableVecOp || SrcOpIdx1 > LastCommutableVecOp ||
       SrcOpIdx1 == KMaskOp))
    return false;
  if (SrcOpIdx2 != CommuteAnyOperandIndex &&
      (SrcOpIdx2 < FirstCommutableVecOp || SrcOpIdx2 > LastCommutableVecOp ||
       SrcOpIdx2 == KMaskOp))
    return false;

  // Look for two different register operands assumed to be commutable
  // regardless of the FMA opcode. The FMA opcode is adjusted later.
  if (SrcOpIdx1 == CommuteAnyOperandIndex ||
      SrcOpIdx2 == CommuteAnyOperandIndex) {
    unsigned CommutableOpIdx1 = SrcOpIdx1;
    unsigned CommutableOpIdx2 = SrcOpIdx2;

    // At least one of operands to be commuted is not specified and
    // this method is free to choose appropriate commutable operands.
    if (SrcOpIdx1 == SrcOpIdx2)
      // Both of operands are not fixed. By default set one of commutable
      // operands to the last register operand of the instruction.
      CommutableOpIdx2 = LastCommutableVecOp;
    else if (SrcOpIdx2 == CommuteAnyOperandIndex)
      // Only one of operands is not fixed.
      CommutableOpIdx2 = SrcOpIdx1;

    // CommutableOpIdx2 is well defined now. Let's choose another commutable
    // operand and assign its index to CommutableOpIdx1.
    unsigned Op2Reg = MI.getOperand(CommutableOpIdx2).getReg();
    for (CommutableOpIdx1 = LastCommutableVecOp;
         CommutableOpIdx1 >= FirstCommutableVecOp; CommutableOpIdx1--) {
      // Just ignore and skip the k-mask operand.
      if (CommutableOpIdx1 == KMaskOp)
        continue;

      // The commuted operands must have different registers.
      // Otherwise, the commute transformation does not change anything and
      // is useless then.
      if (Op2Reg != MI.getOperand(CommutableOpIdx1).getReg())
        break;
    }

    // No appropriate commutable operands were found.
    if (CommutableOpIdx1 < FirstCommutableVecOp)
      return false;

    // Assign the found pair of commutable indices to SrcOpIdx1 and SrcOpidx2
    // to return those values.
    if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2,
                              CommutableOpIdx1, CommutableOpIdx2))
      return false;
  }

  return true;
}

bool X86InstrInfo::findCommutedOpIndices(MachineInstr &MI, unsigned &SrcOpIdx1,
                                         unsigned &SrcOpIdx2) const {
  const MCInstrDesc &Desc = MI.getDesc();
  if (!Desc.isCommutable())
    return false;

  switch (MI.getOpcode()) {
  case X86::CMPSDrr:
  case X86::CMPSSrr:
  case X86::CMPPDrri:
  case X86::CMPPSrri:
  case X86::VCMPSDrr:
  case X86::VCMPSSrr:
  case X86::VCMPPDrri:
  case X86::VCMPPSrri:
  case X86::VCMPPDYrri:
  case X86::VCMPPSYrri:
  case X86::VCMPSDZrr:
  case X86::VCMPSSZrr:
  case X86::VCMPPDZrri:
  case X86::VCMPPSZrri:
  case X86::VCMPPDZ128rri:
  case X86::VCMPPSZ128rri:
  case X86::VCMPPDZ256rri:
  case X86::VCMPPSZ256rri: {
    // Float comparison can be safely commuted for
    // Ordered/Unordered/Equal/NotEqual tests
    unsigned Imm = MI.getOperand(3).getImm() & 0x7;
    switch (Imm) {
    case 0x00: // EQUAL
    case 0x03: // UNORDERED
    case 0x04: // NOT EQUAL
    case 0x07: // ORDERED
      // The indices of the commutable operands are 1 and 2.
      // Assign them to the returned operand indices here.
      return fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, 1, 2);
    }
    return false;
  }
  case X86::MOVSDrr:
  case X86::MOVSSrr:
  case X86::VMOVSDrr:
  case X86::VMOVSSrr:
    if (Subtarget.hasSSE41())
      return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
    return false;
  case X86::MOVHLPSrr:
  case X86::UNPCKHPDrr:
  case X86::VMOVHLPSrr:
  case X86::VUNPCKHPDrr:
  case X86::VMOVHLPSZrr:
  case X86::VUNPCKHPDZ128rr:
    if (Subtarget.hasSSE2())
      return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
    return false;
  case X86::VPTERNLOGDZrri:      case X86::VPTERNLOGDZrmi:
  case X86::VPTERNLOGDZ128rri:   case X86::VPTERNLOGDZ128rmi:
  case X86::VPTERNLOGDZ256rri:   case X86::VPTERNLOGDZ256rmi:
  case X86::VPTERNLOGQZrri:      case X86::VPTERNLOGQZrmi:
  case X86::VPTERNLOGQZ128rri:   case X86::VPTERNLOGQZ128rmi:
  case X86::VPTERNLOGQZ256rri:   case X86::VPTERNLOGQZ256rmi:
  case X86::VPTERNLOGDZrrik:
  case X86::VPTERNLOGDZ128rrik:
  case X86::VPTERNLOGDZ256rrik:
  case X86::VPTERNLOGQZrrik:
  case X86::VPTERNLOGQZ128rrik:
  case X86::VPTERNLOGQZ256rrik:
  case X86::VPTERNLOGDZrrikz:    case X86::VPTERNLOGDZrmikz:
  case X86::VPTERNLOGDZ128rrikz: case X86::VPTERNLOGDZ128rmikz:
  case X86::VPTERNLOGDZ256rrikz: case X86::VPTERNLOGDZ256rmikz:
  case X86::VPTERNLOGQZrrikz:    case X86::VPTERNLOGQZrmikz:
  case X86::VPTERNLOGQZ128rrikz: case X86::VPTERNLOGQZ128rmikz:
  case X86::VPTERNLOGQZ256rrikz: case X86::VPTERNLOGQZ256rmikz:
  case X86::VPTERNLOGDZ128rmbi:
  case X86::VPTERNLOGDZ256rmbi:
  case X86::VPTERNLOGDZrmbi:
  case X86::VPTERNLOGQZ128rmbi:
  case X86::VPTERNLOGQZ256rmbi:
  case X86::VPTERNLOGQZrmbi:
  case X86::VPTERNLOGDZ128rmbikz:
  case X86::VPTERNLOGDZ256rmbikz:
  case X86::VPTERNLOGDZrmbikz:
  case X86::VPTERNLOGQZ128rmbikz:
  case X86::VPTERNLOGQZ256rmbikz:
  case X86::VPTERNLOGQZrmbikz:
    return findThreeSrcCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
  case X86::VPMADD52HUQZ128r:
  case X86::VPMADD52HUQZ128rk:
  case X86::VPMADD52HUQZ128rkz:
  case X86::VPMADD52HUQZ256r:
  case X86::VPMADD52HUQZ256rk:
  case X86::VPMADD52HUQZ256rkz:
  case X86::VPMADD52HUQZr:
  case X86::VPMADD52HUQZrk:
  case X86::VPMADD52HUQZrkz:
  case X86::VPMADD52LUQZ128r:
  case X86::VPMADD52LUQZ128rk:
  case X86::VPMADD52LUQZ128rkz:
  case X86::VPMADD52LUQZ256r:
  case X86::VPMADD52LUQZ256rk:
  case X86::VPMADD52LUQZ256rkz:
  case X86::VPMADD52LUQZr:
  case X86::VPMADD52LUQZrk:
  case X86::VPMADD52LUQZrkz: {
    unsigned CommutableOpIdx1 = 2;
    unsigned CommutableOpIdx2 = 3;
    if (X86II::isKMasked(Desc.TSFlags)) {
      // Skip the mask register.
      ++CommutableOpIdx1;
      ++CommutableOpIdx2;
    }
    if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2,
                              CommutableOpIdx1, CommutableOpIdx2))
      return false;
    if (!MI.getOperand(SrcOpIdx1).isReg() ||
        !MI.getOperand(SrcOpIdx2).isReg())
      // No idea.
      return false;
    return true;
  }

  default:
    const X86InstrFMA3Group *FMA3Group = getFMA3Group(MI.getOpcode(),
                                                      MI.getDesc().TSFlags);
    if (FMA3Group)
      return findThreeSrcCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2,
                                           FMA3Group->isIntrinsic());

    // Handled masked instructions since we need to skip over the mask input
    // and the preserved input.
    if (X86II::isKMasked(Desc.TSFlags)) {
      // First assume that the first input is the mask operand and skip past it.
      unsigned CommutableOpIdx1 = Desc.getNumDefs() + 1;
      unsigned CommutableOpIdx2 = Desc.getNumDefs() + 2;
      // Check if the first input is tied. If there isn't one then we only
      // need to skip the mask operand which we did above.
      if ((MI.getDesc().getOperandConstraint(Desc.getNumDefs(),
                                             MCOI::TIED_TO) != -1)) {
        // If this is zero masking instruction with a tied operand, we need to
        // move the first index back to the first input since this must
        // be a 3 input instruction and we want the first two non-mask inputs.
        // Otherwise this is a 2 input instruction with a preserved input and
        // mask, so we need to move the indices to skip one more input.
        if (X86II::isKMergeMasked(Desc.TSFlags)) {
          ++CommutableOpIdx1;
          ++CommutableOpIdx2;
        } else {
          --CommutableOpIdx1;
        }
      }

      if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2,
                                CommutableOpIdx1, CommutableOpIdx2))
        return false;

      if (!MI.getOperand(SrcOpIdx1).isReg() ||
          !MI.getOperand(SrcOpIdx2).isReg())
        // No idea.
        return false;
      return true;
    }

    return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
  }
  return false;
}

X86::CondCode X86::getCondFromBranchOpc(unsigned BrOpc) {
  switch (BrOpc) {
  default: return X86::COND_INVALID;
  case X86::JE_1:  return X86::COND_E;
  case X86::JNE_1: return X86::COND_NE;
  case X86::JL_1:  return X86::COND_L;
  case X86::JLE_1: return X86::COND_LE;
  case X86::JG_1:  return X86::COND_G;
  case X86::JGE_1: return X86::COND_GE;
  case X86::JB_1:  return X86::COND_B;
  case X86::JBE_1: return X86::COND_BE;
  case X86::JA_1:  return X86::COND_A;
  case X86::JAE_1: return X86::COND_AE;
  case X86::JS_1:  return X86::COND_S;
  case X86::JNS_1: return X86::COND_NS;
  case X86::JP_1:  return X86::COND_P;
  case X86::JNP_1: return X86::COND_NP;
  case X86::JO_1:  return X86::COND_O;
  case X86::JNO_1: return X86::COND_NO;
  }
}

/// Return condition code of a SET opcode.
X86::CondCode X86::getCondFromSETOpc(unsigned Opc) {
  switch (Opc) {
  default: return X86::COND_INVALID;
  case X86::SETAr:  case X86::SETAm:  return X86::COND_A;
  case X86::SETAEr: case X86::SETAEm: return X86::COND_AE;
  case X86::SETBr:  case X86::SETBm:  return X86::COND_B;
  case X86::SETBEr: case X86::SETBEm: return X86::COND_BE;
  case X86::SETEr:  case X86::SETEm:  return X86::COND_E;
  case X86::SETGr:  case X86::SETGm:  return X86::COND_G;
  case X86::SETGEr: case X86::SETGEm: return X86::COND_GE;
  case X86::SETLr:  case X86::SETLm:  return X86::COND_L;
  case X86::SETLEr: case X86::SETLEm: return X86::COND_LE;
  case X86::SETNEr: case X86::SETNEm: return X86::COND_NE;
  case X86::SETNOr: case X86::SETNOm: return X86::COND_NO;
  case X86::SETNPr: case X86::SETNPm: return X86::COND_NP;
  case X86::SETNSr: case X86::SETNSm: return X86::COND_NS;
  case X86::SETOr:  case X86::SETOm:  return X86::COND_O;
  case X86::SETPr:  case X86::SETPm:  return X86::COND_P;
  case X86::SETSr:  case X86::SETSm:  return X86::COND_S;
  }
}

/// Return condition code of a CMov opcode.
X86::CondCode X86::getCondFromCMovOpc(unsigned Opc) {
  switch (Opc) {
  default: return X86::COND_INVALID;
  case X86::CMOVA16rm:  case X86::CMOVA16rr:  case X86::CMOVA32rm:
  case X86::CMOVA32rr:  case X86::CMOVA64rm:  case X86::CMOVA64rr:
    return X86::COND_A;
  case X86::CMOVAE16rm: case X86::CMOVAE16rr: case X86::CMOVAE32rm:
  case X86::CMOVAE32rr: case X86::CMOVAE64rm: case X86::CMOVAE64rr:
    return X86::COND_AE;
  case X86::CMOVB16rm:  case X86::CMOVB16rr:  case X86::CMOVB32rm:
  case X86::CMOVB32rr:  case X86::CMOVB64rm:  case X86::CMOVB64rr:
    return X86::COND_B;
  case X86::CMOVBE16rm: case X86::CMOVBE16rr: case X86::CMOVBE32rm:
  case X86::CMOVBE32rr: case X86::CMOVBE64rm: case X86::CMOVBE64rr:
    return X86::COND_BE;
  case X86::CMOVE16rm:  case X86::CMOVE16rr:  case X86::CMOVE32rm:
  case X86::CMOVE32rr:  case X86::CMOVE64rm:  case X86::CMOVE64rr:
    return X86::COND_E;
  case X86::CMOVG16rm:  case X86::CMOVG16rr:  case X86::CMOVG32rm:
  case X86::CMOVG32rr:  case X86::CMOVG64rm:  case X86::CMOVG64rr:
    return X86::COND_G;
  case X86::CMOVGE16rm: case X86::CMOVGE16rr: case X86::CMOVGE32rm:
  case X86::CMOVGE32rr: case X86::CMOVGE64rm: case X86::CMOVGE64rr:
    return X86::COND_GE;
  case X86::CMOVL16rm:  case X86::CMOVL16rr:  case X86::CMOVL32rm:
  case X86::CMOVL32rr:  case X86::CMOVL64rm:  case X86::CMOVL64rr:
    return X86::COND_L;
  case X86::CMOVLE16rm: case X86::CMOVLE16rr: case X86::CMOVLE32rm:
  case X86::CMOVLE32rr: case X86::CMOVLE64rm: case X86::CMOVLE64rr:
    return X86::COND_LE;
  case X86::CMOVNE16rm: case X86::CMOVNE16rr: case X86::CMOVNE32rm:
  case X86::CMOVNE32rr: case X86::CMOVNE64rm: case X86::CMOVNE64rr:
    return X86::COND_NE;
  case X86::CMOVNO16rm: case X86::CMOVNO16rr: case X86::CMOVNO32rm:
  case X86::CMOVNO32rr: case X86::CMOVNO64rm: case X86::CMOVNO64rr:
    return X86::COND_NO;
  case X86::CMOVNP16rm: case X86::CMOVNP16rr: case X86::CMOVNP32rm:
  case X86::CMOVNP32rr: case X86::CMOVNP64rm: case X86::CMOVNP64rr:
    return X86::COND_NP;
  case X86::CMOVNS16rm: case X86::CMOVNS16rr: case X86::CMOVNS32rm:
  case X86::CMOVNS32rr: case X86::CMOVNS64rm: case X86::CMOVNS64rr:
    return X86::COND_NS;
  case X86::CMOVO16rm:  case X86::CMOVO16rr:  case X86::CMOVO32rm:
  case X86::CMOVO32rr:  case X86::CMOVO64rm:  case X86::CMOVO64rr:
    return X86::COND_O;
  case X86::CMOVP16rm:  case X86::CMOVP16rr:  case X86::CMOVP32rm:
  case X86::CMOVP32rr:  case X86::CMOVP64rm:  case X86::CMOVP64rr:
    return X86::COND_P;
  case X86::CMOVS16rm:  case X86::CMOVS16rr:  case X86::CMOVS32rm:
  case X86::CMOVS32rr:  case X86::CMOVS64rm:  case X86::CMOVS64rr:
    return X86::COND_S;
  }
}

unsigned X86::GetCondBranchFromCond(X86::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Illegal condition code!");
  case X86::COND_E:  return X86::JE_1;
  case X86::COND_NE: return X86::JNE_1;
  case X86::COND_L:  return X86::JL_1;
  case X86::COND_LE: return X86::JLE_1;
  case X86::COND_G:  return X86::JG_1;
  case X86::COND_GE: return X86::JGE_1;
  case X86::COND_B:  return X86::JB_1;
  case X86::COND_BE: return X86::JBE_1;
  case X86::COND_A:  return X86::JA_1;
  case X86::COND_AE: return X86::JAE_1;
  case X86::COND_S:  return X86::JS_1;
  case X86::COND_NS: return X86::JNS_1;
  case X86::COND_P:  return X86::JP_1;
  case X86::COND_NP: return X86::JNP_1;
  case X86::COND_O:  return X86::JO_1;
  case X86::COND_NO: return X86::JNO_1;
  }
}

/// Return the inverse of the specified condition,
/// e.g. turning COND_E to COND_NE.
X86::CondCode X86::GetOppositeBranchCondition(X86::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Illegal condition code!");
  case X86::COND_E:  return X86::COND_NE;
  case X86::COND_NE: return X86::COND_E;
  case X86::COND_L:  return X86::COND_GE;
  case X86::COND_LE: return X86::COND_G;
  case X86::COND_G:  return X86::COND_LE;
  case X86::COND_GE: return X86::COND_L;
  case X86::COND_B:  return X86::COND_AE;
  case X86::COND_BE: return X86::COND_A;
  case X86::COND_A:  return X86::COND_BE;
  case X86::COND_AE: return X86::COND_B;
  case X86::COND_S:  return X86::COND_NS;
  case X86::COND_NS: return X86::COND_S;
  case X86::COND_P:  return X86::COND_NP;
  case X86::COND_NP: return X86::COND_P;
  case X86::COND_O:  return X86::COND_NO;
  case X86::COND_NO: return X86::COND_O;
  case X86::COND_NE_OR_P:  return X86::COND_E_AND_NP;
  case X86::COND_E_AND_NP: return X86::COND_NE_OR_P;
  }
}

/// Assuming the flags are set by MI(a,b), return the condition code if we
/// modify the instructions such that flags are set by MI(b,a).
static X86::CondCode getSwappedCondition(X86::CondCode CC) {
  switch (CC) {
  default: return X86::COND_INVALID;
  case X86::COND_E:  return X86::COND_E;
  case X86::COND_NE: return X86::COND_NE;
  case X86::COND_L:  return X86::COND_G;
  case X86::COND_LE: return X86::COND_GE;
  case X86::COND_G:  return X86::COND_L;
  case X86::COND_GE: return X86::COND_LE;
  case X86::COND_B:  return X86::COND_A;
  case X86::COND_BE: return X86::COND_AE;
  case X86::COND_A:  return X86::COND_B;
  case X86::COND_AE: return X86::COND_BE;
  }
}

std::pair<X86::CondCode, bool>
X86::getX86ConditionCode(CmpInst::Predicate Predicate) {
  X86::CondCode CC = X86::COND_INVALID;
  bool NeedSwap = false;
  switch (Predicate) {
  default: break;
  // Floating-point Predicates
  case CmpInst::FCMP_UEQ: CC = X86::COND_E;       break;
  case CmpInst::FCMP_OLT: NeedSwap = true;        LLVM_FALLTHROUGH;
  case CmpInst::FCMP_OGT: CC = X86::COND_A;       break;
  case CmpInst::FCMP_OLE: NeedSwap = true;        LLVM_FALLTHROUGH;
  case CmpInst::FCMP_OGE: CC = X86::COND_AE;      break;
  case CmpInst::FCMP_UGT: NeedSwap = true;        LLVM_FALLTHROUGH;
  case CmpInst::FCMP_ULT: CC = X86::COND_B;       break;
  case CmpInst::FCMP_UGE: NeedSwap = true;        LLVM_FALLTHROUGH;
  case CmpInst::FCMP_ULE: CC = X86::COND_BE;      break;
  case CmpInst::FCMP_ONE: CC = X86::COND_NE;      break;
  case CmpInst::FCMP_UNO: CC = X86::COND_P;       break;
  case CmpInst::FCMP_ORD: CC = X86::COND_NP;      break;
  case CmpInst::FCMP_OEQ:                         LLVM_FALLTHROUGH;
  case CmpInst::FCMP_UNE: CC = X86::COND_INVALID; break;

  // Integer Predicates
  case CmpInst::ICMP_EQ:  CC = X86::COND_E;       break;
  case CmpInst::ICMP_NE:  CC = X86::COND_NE;      break;
  case CmpInst::ICMP_UGT: CC = X86::COND_A;       break;
  case CmpInst::ICMP_UGE: CC = X86::COND_AE;      break;
  case CmpInst::ICMP_ULT: CC = X86::COND_B;       break;
  case CmpInst::ICMP_ULE: CC = X86::COND_BE;      break;
  case CmpInst::ICMP_SGT: CC = X86::COND_G;       break;
  case CmpInst::ICMP_SGE: CC = X86::COND_GE;      break;
  case CmpInst::ICMP_SLT: CC = X86::COND_L;       break;
  case CmpInst::ICMP_SLE: CC = X86::COND_LE;      break;
  }

  return std::make_pair(CC, NeedSwap);
}

/// Return a set opcode for the given condition and
/// whether it has memory operand.
unsigned X86::getSETFromCond(CondCode CC, bool HasMemoryOperand) {
  static const uint16_t Opc[16][2] = {
    { X86::SETAr,  X86::SETAm  },
    { X86::SETAEr, X86::SETAEm },
    { X86::SETBr,  X86::SETBm  },
    { X86::SETBEr, X86::SETBEm },
    { X86::SETEr,  X86::SETEm  },
    { X86::SETGr,  X86::SETGm  },
    { X86::SETGEr, X86::SETGEm },
    { X86::SETLr,  X86::SETLm  },
    { X86::SETLEr, X86::SETLEm },
    { X86::SETNEr, X86::SETNEm },
    { X86::SETNOr, X86::SETNOm },
    { X86::SETNPr, X86::SETNPm },
    { X86::SETNSr, X86::SETNSm },
    { X86::SETOr,  X86::SETOm  },
    { X86::SETPr,  X86::SETPm  },
    { X86::SETSr,  X86::SETSm  }
  };

  assert(CC <= LAST_VALID_COND && "Can only handle standard cond codes");
  return Opc[CC][HasMemoryOperand ? 1 : 0];
}

/// Return a cmov opcode for the given condition,
/// register size in bytes, and operand type.
unsigned X86::getCMovFromCond(CondCode CC, unsigned RegBytes,
                              bool HasMemoryOperand) {
  static const uint16_t Opc[32][3] = {
    { X86::CMOVA16rr,  X86::CMOVA32rr,  X86::CMOVA64rr  },
    { X86::CMOVAE16rr, X86::CMOVAE32rr, X86::CMOVAE64rr },
    { X86::CMOVB16rr,  X86::CMOVB32rr,  X86::CMOVB64rr  },
    { X86::CMOVBE16rr, X86::CMOVBE32rr, X86::CMOVBE64rr },
    { X86::CMOVE16rr,  X86::CMOVE32rr,  X86::CMOVE64rr  },
    { X86::CMOVG16rr,  X86::CMOVG32rr,  X86::CMOVG64rr  },
    { X86::CMOVGE16rr, X86::CMOVGE32rr, X86::CMOVGE64rr },
    { X86::CMOVL16rr,  X86::CMOVL32rr,  X86::CMOVL64rr  },
    { X86::CMOVLE16rr, X86::CMOVLE32rr, X86::CMOVLE64rr },
    { X86::CMOVNE16rr, X86::CMOVNE32rr, X86::CMOVNE64rr },
    { X86::CMOVNO16rr, X86::CMOVNO32rr, X86::CMOVNO64rr },
    { X86::CMOVNP16rr, X86::CMOVNP32rr, X86::CMOVNP64rr },
    { X86::CMOVNS16rr, X86::CMOVNS32rr, X86::CMOVNS64rr },
    { X86::CMOVO16rr,  X86::CMOVO32rr,  X86::CMOVO64rr  },
    { X86::CMOVP16rr,  X86::CMOVP32rr,  X86::CMOVP64rr  },
    { X86::CMOVS16rr,  X86::CMOVS32rr,  X86::CMOVS64rr  },
    { X86::CMOVA16rm,  X86::CMOVA32rm,  X86::CMOVA64rm  },
    { X86::CMOVAE16rm, X86::CMOVAE32rm, X86::CMOVAE64rm },
    { X86::CMOVB16rm,  X86::CMOVB32rm,  X86::CMOVB64rm  },
    { X86::CMOVBE16rm, X86::CMOVBE32rm, X86::CMOVBE64rm },
    { X86::CMOVE16rm,  X86::CMOVE32rm,  X86::CMOVE64rm  },
    { X86::CMOVG16rm,  X86::CMOVG32rm,  X86::CMOVG64rm  },
    { X86::CMOVGE16rm, X86::CMOVGE32rm, X86::CMOVGE64rm },
    { X86::CMOVL16rm,  X86::CMOVL32rm,  X86::CMOVL64rm  },
    { X86::CMOVLE16rm, X86::CMOVLE32rm, X86::CMOVLE64rm },
    { X86::CMOVNE16rm, X86::CMOVNE32rm, X86::CMOVNE64rm },
    { X86::CMOVNO16rm, X86::CMOVNO32rm, X86::CMOVNO64rm },
    { X86::CMOVNP16rm, X86::CMOVNP32rm, X86::CMOVNP64rm },
    { X86::CMOVNS16rm, X86::CMOVNS32rm, X86::CMOVNS64rm },
    { X86::CMOVO16rm,  X86::CMOVO32rm,  X86::CMOVO64rm  },
    { X86::CMOVP16rm,  X86::CMOVP32rm,  X86::CMOVP64rm  },
    { X86::CMOVS16rm,  X86::CMOVS32rm,  X86::CMOVS64rm  }
  };

  assert(CC < 16 && "Can only handle standard cond codes");
  unsigned Idx = HasMemoryOperand ? 16+CC : CC;
  switch(RegBytes) {
  default: llvm_unreachable("Illegal register size!");
  case 2: return Opc[Idx][0];
  case 4: return Opc[Idx][1];
  case 8: return Opc[Idx][2];
  }
}

/// Get the VPCMP immediate for the given condition.
unsigned X86::getVPCMPImmForCond(ISD::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Unexpected SETCC condition");
  case ISD::SETNE:  return 4;
  case ISD::SETEQ:  return 0;
  case ISD::SETULT:
  case ISD::SETLT: return 1;
  case ISD::SETUGT:
  case ISD::SETGT: return 6;
  case ISD::SETUGE:
  case ISD::SETGE: return 5;
  case ISD::SETULE:
  case ISD::SETLE: return 2;
  }
}

/// Get the VPCMP immediate if the opcodes are swapped.
unsigned X86::getSwappedVPCMPImm(unsigned Imm) {
  switch (Imm) {
  default: llvm_unreachable("Unreachable!");
  case 0x01: Imm = 0x06; break; // LT  -> NLE
  case 0x02: Imm = 0x05; break; // LE  -> NLT
  case 0x05: Imm = 0x02; break; // NLT -> LE
  case 0x06: Imm = 0x01; break; // NLE -> LT
  case 0x00: // EQ
  case 0x03: // FALSE
  case 0x04: // NE
  case 0x07: // TRUE
    break;
  }

  return Imm;
}

/// Get the VPCOM immediate if the opcodes are swapped.
unsigned X86::getSwappedVPCOMImm(unsigned Imm) {
  switch (Imm) {
  default: llvm_unreachable("Unreachable!");
  case 0x00: Imm = 0x02; break; // LT -> GT
  case 0x01: Imm = 0x03; break; // LE -> GE
  case 0x02: Imm = 0x00; break; // GT -> LT
  case 0x03: Imm = 0x01; break; // GE -> LE
  case 0x04: // EQ
  case 0x05: // NE
  case 0x06: // FALSE
  case 0x07: // TRUE
    break;
  }

  return Imm;
}

bool X86InstrInfo::isUnpredicatedTerminator(const MachineInstr &MI) const {
  if (!MI.isTerminator()) return false;

  // Conditional branch is a special case.
  if (MI.isBranch() && !MI.isBarrier())
    return true;
  if (!MI.isPredicable())
    return true;
  return !isPredicated(MI);
}

bool X86InstrInfo::isUnconditionalTailCall(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case X86::TCRETURNdi:
  case X86::TCRETURNri:
  case X86::TCRETURNmi:
  case X86::TCRETURNdi64:
  case X86::TCRETURNri64:
  case X86::TCRETURNmi64:
    return true;
  default:
    return false;
  }
}

bool X86InstrInfo::canMakeTailCallConditional(
    SmallVectorImpl<MachineOperand> &BranchCond,
    const MachineInstr &TailCall) const {
  if (TailCall.getOpcode() != X86::TCRETURNdi &&
      TailCall.getOpcode() != X86::TCRETURNdi64) {
    // Only direct calls can be done with a conditional branch.
    return false;
  }

  const MachineFunction *MF = TailCall.getParent()->getParent();
  if (Subtarget.isTargetWin64() && MF->hasWinCFI()) {
    // Conditional tail calls confuse the Win64 unwinder.
    return false;
  }

  assert(BranchCond.size() == 1);
  if (BranchCond[0].getImm() > X86::LAST_VALID_COND) {
    // Can't make a conditional tail call with this condition.
    return false;
  }

  const X86MachineFunctionInfo *X86FI = MF->getInfo<X86MachineFunctionInfo>();
  if (X86FI->getTCReturnAddrDelta() != 0 ||
      TailCall.getOperand(1).getImm() != 0) {
    // A conditional tail call cannot do any stack adjustment.
    return false;
  }

  return true;
}

void X86InstrInfo::replaceBranchWithTailCall(
    MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &BranchCond,
    const MachineInstr &TailCall) const {
  assert(canMakeTailCallConditional(BranchCond, TailCall));

  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!I->isBranch())
      assert(0 && "Can't find the branch to replace!");

    X86::CondCode CC = X86::getCondFromBranchOpc(I->getOpcode());
    assert(BranchCond.size() == 1);
    if (CC != BranchCond[0].getImm())
      continue;

    break;
  }

  unsigned Opc = TailCall.getOpcode() == X86::TCRETURNdi ? X86::TCRETURNdicc
                                                         : X86::TCRETURNdi64cc;

  auto MIB = BuildMI(MBB, I, MBB.findDebugLoc(I), get(Opc));
  MIB->addOperand(TailCall.getOperand(0)); // Destination.
  MIB.addImm(0); // Stack offset (not used).
  MIB->addOperand(BranchCond[0]); // Condition.
  MIB.copyImplicitOps(TailCall); // Regmask and (imp-used) parameters.

  // Add implicit uses and defs of all live regs potentially clobbered by the
  // call. This way they still appear live across the call.
  LivePhysRegs LiveRegs(getRegisterInfo());
  LiveRegs.addLiveOuts(MBB);
  SmallVector<std::pair<MCPhysReg, const MachineOperand *>, 8> Clobbers;
  LiveRegs.stepForward(*MIB, Clobbers);
  for (const auto &C : Clobbers) {
    MIB.addReg(C.first, RegState::Implicit);
    MIB.addReg(C.first, RegState::Implicit | RegState::Define);
  }

  I->eraseFromParent();
}

// Given a MBB and its TBB, find the FBB which was a fallthrough MBB (it may
// not be a fallthrough MBB now due to layout changes). Return nullptr if the
// fallthrough MBB cannot be identified.
static MachineBasicBlock *getFallThroughMBB(MachineBasicBlock *MBB,
                                            MachineBasicBlock *TBB) {
  // Look for non-EHPad successors other than TBB. If we find exactly one, it
  // is the fallthrough MBB. If we find zero, then TBB is both the target MBB
  // and fallthrough MBB. If we find more than one, we cannot identify the
  // fallthrough MBB and should return nullptr.
  MachineBasicBlock *FallthroughBB = nullptr;
  for (auto SI = MBB->succ_begin(), SE = MBB->succ_end(); SI != SE; ++SI) {
    if ((*SI)->isEHPad() || (*SI == TBB && FallthroughBB))
      continue;
    // Return a nullptr if we found more than one fallthrough successor.
    if (FallthroughBB && FallthroughBB != TBB)
      return nullptr;
    FallthroughBB = *SI;
  }
  return FallthroughBB;
}

bool X86InstrInfo::AnalyzeBranchImpl(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    SmallVectorImpl<MachineOperand> &Cond,
    SmallVectorImpl<MachineInstr *> &CondBranches, bool AllowModify) const {

  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  MachineBasicBlock::iterator UnCondBrIter = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!I->isBranch())
      return true;

    // Handle unconditional branches.
    if (I->getOpcode() == X86::JMP_1) {
      UnCondBrIter = I;

      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      while (std::next(I) != MBB.end())
        std::next(I)->eraseFromParent();

      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        UnCondBrIter = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditional destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    X86::CondCode BranchCode = X86::getCondFromBranchOpc(I->getOpcode());
    if (BranchCode == X86::COND_INVALID)
      return true;  // Can't handle indirect branch.

    // In practice we should never have an undef eflags operand, if we do
    // abort here as we are not prepared to preserve the flag.
    if (I->getOperand(1).isUndef())
      return true;

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      MachineBasicBlock *TargetBB = I->getOperand(0).getMBB();
      if (AllowModify && UnCondBrIter != MBB.end() &&
          MBB.isLayoutSuccessor(TargetBB)) {
        // If we can modify the code and it ends in something like:
        //
        //     jCC L1
        //     jmp L2
        //   L1:
        //     ...
        //   L2:
        //
        // Then we can change this to:
        //
        //     jnCC L2
        //   L1:
        //     ...
        //   L2:
        //
        // Which is a bit more efficient.
        // We conditionally jump to the fall-through block.
        BranchCode = GetOppositeBranchCondition(BranchCode);
        unsigned JNCC = GetCondBranchFromCond(BranchCode);
        MachineBasicBlock::iterator OldInst = I;

        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(JNCC))
          .addMBB(UnCondBrIter->getOperand(0).getMBB());
        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(X86::JMP_1))
          .addMBB(TargetBB);

        OldInst->eraseFromParent();
        UnCondBrIter->eraseFromParent();

        // Restart the analysis.
        UnCondBrIter = MBB.end();
        I = MBB.end();
        continue;
      }

      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(BranchCode));
      CondBranches.push_back(&*I);
      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination and their condition
    // opcodes fit one of the special multi-branch idioms.
    assert(Cond.size() == 1);
    assert(TBB);

    // If the conditions are the same, we can leave them alone.
    X86::CondCode OldBranchCode = (X86::CondCode)Cond[0].getImm();
    auto NewTBB = I->getOperand(0).getMBB();
    if (OldBranchCode == BranchCode && TBB == NewTBB)
      continue;

    // If they differ, see if they fit one of the known patterns. Theoretically,
    // we could handle more patterns here, but we shouldn't expect to see them
    // if instruction selection has done a reasonable job.
    if (TBB == NewTBB &&
               ((OldBranchCode == X86::COND_P && BranchCode == X86::COND_NE) ||
                (OldBranchCode == X86::COND_NE && BranchCode == X86::COND_P))) {
      BranchCode = X86::COND_NE_OR_P;
    } else if ((OldBranchCode == X86::COND_NP && BranchCode == X86::COND_NE) ||
               (OldBranchCode == X86::COND_E && BranchCode == X86::COND_P)) {
      if (NewTBB != (FBB ? FBB : getFallThroughMBB(&MBB, TBB)))
        return true;

      // X86::COND_E_AND_NP usually has two different branch destinations.
      //
      // JP B1
      // JE B2
      // JMP B1
      // B1:
      // B2:
      //
      // Here this condition branches to B2 only if NP && E. It has another
      // equivalent form:
      //
      // JNE B1
      // JNP B2
      // JMP B1
      // B1:
      // B2:
      //
      // Similarly it branches to B2 only if E && NP. That is why this condition
      // is named with COND_E_AND_NP.
      BranchCode = X86::COND_E_AND_NP;
    } else
      return true;

    // Update the MachineOperand.
    Cond[0].setImm(BranchCode);
    CondBranches.push_back(&*I);
  }

  return false;
}

bool X86InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  SmallVector<MachineInstr *, 4> CondBranches;
  return AnalyzeBranchImpl(MBB, TBB, FBB, Cond, CondBranches, AllowModify);
}

bool X86InstrInfo::analyzeBranchPredicate(MachineBasicBlock &MBB,
                                          MachineBranchPredicate &MBP,
                                          bool AllowModify) const {
  using namespace std::placeholders;

  SmallVector<MachineOperand, 4> Cond;
  SmallVector<MachineInstr *, 4> CondBranches;
  if (AnalyzeBranchImpl(MBB, MBP.TrueDest, MBP.FalseDest, Cond, CondBranches,
                        AllowModify))
    return true;

  if (Cond.size() != 1)
    return true;

  assert(MBP.TrueDest && "expected!");

  if (!MBP.FalseDest)
    MBP.FalseDest = MBB.getNextNode();

  const TargetRegisterInfo *TRI = &getRegisterInfo();

  MachineInstr *ConditionDef = nullptr;
  bool SingleUseCondition = true;

  for (auto I = std::next(MBB.rbegin()), E = MBB.rend(); I != E; ++I) {
    if (I->modifiesRegister(X86::EFLAGS, TRI)) {
      ConditionDef = &*I;
      break;
    }

    if (I->readsRegister(X86::EFLAGS, TRI))
      SingleUseCondition = false;
  }

  if (!ConditionDef)
    return true;

  if (SingleUseCondition) {
    for (auto *Succ : MBB.successors())
      if (Succ->isLiveIn(X86::EFLAGS))
        SingleUseCondition = false;
  }

  MBP.ConditionDef = ConditionDef;
  MBP.SingleUseCondition = SingleUseCondition;

  // Currently we only recognize the simple pattern:
  //
  //   test %reg, %reg
  //   je %label
  //
  const unsigned TestOpcode =
      Subtarget.is64Bit() ? X86::TEST64rr : X86::TEST32rr;

  if (ConditionDef->getOpcode() == TestOpcode &&
      ConditionDef->getNumOperands() == 3 &&
      ConditionDef->getOperand(0).isIdenticalTo(ConditionDef->getOperand(1)) &&
      (Cond[0].getImm() == X86::COND_NE || Cond[0].getImm() == X86::COND_E)) {
    MBP.LHS = ConditionDef->getOperand(0);
    MBP.RHS = MachineOperand::CreateImm(0);
    MBP.Predicate = Cond[0].getImm() == X86::COND_NE
                        ? MachineBranchPredicate::PRED_NE
                        : MachineBranchPredicate::PRED_EQ;
    return false;
  }

  return true;
}

unsigned X86InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != X86::JMP_1 &&
        X86::getCondFromBranchOpc(I->getOpcode()) == X86::COND_INVALID)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

unsigned X86InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "X86 branch conditions have one component!");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(X86::JMP_1)).addMBB(TBB);
    return 1;
  }

  // If FBB is null, it is implied to be a fall-through block.
  bool FallThru = FBB == nullptr;

  // Conditional branch.
  unsigned Count = 0;
  X86::CondCode CC = (X86::CondCode)Cond[0].getImm();
  switch (CC) {
  case X86::COND_NE_OR_P:
    // Synthesize NE_OR_P with two branches.
    BuildMI(&MBB, DL, get(X86::JNE_1)).addMBB(TBB);
    ++Count;
    BuildMI(&MBB, DL, get(X86::JP_1)).addMBB(TBB);
    ++Count;
    break;
  case X86::COND_E_AND_NP:
    // Use the next block of MBB as FBB if it is null.
    if (FBB == nullptr) {
      FBB = getFallThroughMBB(&MBB, TBB);
      assert(FBB && "MBB cannot be the last block in function when the false "
                    "body is a fall-through.");
    }
    // Synthesize COND_E_AND_NP with two branches.
    BuildMI(&MBB, DL, get(X86::JNE_1)).addMBB(FBB);
    ++Count;
    BuildMI(&MBB, DL, get(X86::JNP_1)).addMBB(TBB);
    ++Count;
    break;
  default: {
    unsigned Opc = GetCondBranchFromCond(CC);
    BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
    ++Count;
  }
  }
  if (!FallThru) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(X86::JMP_1)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

bool X86InstrInfo::
canInsertSelect(const MachineBasicBlock &MBB,
                ArrayRef<MachineOperand> Cond,
                unsigned TrueReg, unsigned FalseReg,
                int &CondCycles, int &TrueCycles, int &FalseCycles) const {
  // Not all subtargets have cmov instructions.
  if (!Subtarget.hasCMov())
    return false;
  if (Cond.size() != 1)
    return false;
  // We cannot do the composite conditions, at least not in SSA form.
  if ((X86::CondCode)Cond[0].getImm() > X86::COND_S)
    return false;

  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // We have cmov instructions for 16, 32, and 64 bit general purpose registers.
  if (X86::GR16RegClass.hasSubClassEq(RC) ||
      X86::GR32RegClass.hasSubClassEq(RC) ||
      X86::GR64RegClass.hasSubClassEq(RC)) {
    // This latency applies to Pentium M, Merom, Wolfdale, Nehalem, and Sandy
    // Bridge. Probably Ivy Bridge as well.
    CondCycles = 2;
    TrueCycles = 2;
    FalseCycles = 2;
    return true;
  }

  // Can't do vectors.
  return false;
}

void X86InstrInfo::insertSelect(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, unsigned DstReg,
                                ArrayRef<MachineOperand> Cond, unsigned TrueReg,
                                unsigned FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  const TargetRegisterClass &RC = *MRI.getRegClass(DstReg);
  assert(Cond.size() == 1 && "Invalid Cond array");
  unsigned Opc = getCMovFromCond((X86::CondCode)Cond[0].getImm(),
                                 TRI.getRegSizeInBits(RC) / 8,
                                 false /*HasMemoryOperand*/);
  BuildMI(MBB, I, DL, get(Opc), DstReg).addReg(FalseReg).addReg(TrueReg);
}

/// Test if the given register is a physical h register.
static bool isHReg(unsigned Reg) {
  return X86::GR8_ABCD_HRegClass.contains(Reg);
}

// Try and copy between VR128/VR64 and GR64 registers.
static unsigned CopyToFromAsymmetricReg(unsigned DestReg, unsigned SrcReg,
                                        const X86Subtarget &Subtarget) {
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();

  // SrcReg(MaskReg) -> DestReg(GR64)
  // SrcReg(MaskReg) -> DestReg(GR32)

  // All KMASK RegClasses hold the same k registers, can be tested against anyone.
  if (X86::VK16RegClass.contains(SrcReg)) {
    if (X86::GR64RegClass.contains(DestReg)) {
      assert(Subtarget.hasBWI());
      return X86::KMOVQrk;
    }
    if (X86::GR32RegClass.contains(DestReg))
      return Subtarget.hasBWI() ? X86::KMOVDrk : X86::KMOVWrk;
  }

  // SrcReg(GR64) -> DestReg(MaskReg)
  // SrcReg(GR32) -> DestReg(MaskReg)

  // All KMASK RegClasses hold the same k registers, can be tested against anyone.
  if (X86::VK16RegClass.contains(DestReg)) {
    if (X86::GR64RegClass.contains(SrcReg)) {
      assert(Subtarget.hasBWI());
      return X86::KMOVQkr;
    }
    if (X86::GR32RegClass.contains(SrcReg))
      return Subtarget.hasBWI() ? X86::KMOVDkr : X86::KMOVWkr;
  }


  // SrcReg(VR128) -> DestReg(GR64)
  // SrcReg(VR64)  -> DestReg(GR64)
  // SrcReg(GR64)  -> DestReg(VR128)
  // SrcReg(GR64)  -> DestReg(VR64)

  if (X86::GR64RegClass.contains(DestReg)) {
    if (X86::VR128XRegClass.contains(SrcReg))
      // Copy from a VR128 register to a GR64 register.
      return HasAVX512 ? X86::VMOVPQIto64Zrr :
             HasAVX    ? X86::VMOVPQIto64rr  :
                         X86::MOVPQIto64rr;
    if (X86::VR64RegClass.contains(SrcReg))
      // Copy from a VR64 register to a GR64 register.
      return X86::MMX_MOVD64from64rr;
  } else if (X86::GR64RegClass.contains(SrcReg)) {
    // Copy from a GR64 register to a VR128 register.
    if (X86::VR128XRegClass.contains(DestReg))
      return HasAVX512 ? X86::VMOV64toPQIZrr :
             HasAVX    ? X86::VMOV64toPQIrr  :
                         X86::MOV64toPQIrr;
    // Copy from a GR64 register to a VR64 register.
    if (X86::VR64RegClass.contains(DestReg))
      return X86::MMX_MOVD64to64rr;
  }

  // SrcReg(FR32) -> DestReg(GR32)
  // SrcReg(GR32) -> DestReg(FR32)

  if (X86::GR32RegClass.contains(DestReg) &&
      X86::FR32XRegClass.contains(SrcReg))
    // Copy from a FR32 register to a GR32 register.
    return HasAVX512 ? X86::VMOVSS2DIZrr :
           HasAVX    ? X86::VMOVSS2DIrr  :
                       X86::MOVSS2DIrr;

  if (X86::FR32XRegClass.contains(DestReg) &&
      X86::GR32RegClass.contains(SrcReg))
    // Copy from a GR32 register to a FR32 register.
    return HasAVX512 ? X86::VMOVDI2SSZrr :
           HasAVX    ? X86::VMOVDI2SSrr  :
                       X86::MOVDI2SSrr;
  return 0;
}

void X86InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, unsigned DestReg,
                               unsigned SrcReg, bool KillSrc) const {
  // First deal with the normal symmetric copies.
  bool HasAVX = Subtarget.hasAVX();
  bool HasVLX = Subtarget.hasVLX();
  unsigned Opc = 0;
  if (X86::GR64RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV64rr;
  else if (X86::GR32RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV32rr;
  else if (X86::GR16RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV16rr;
  else if (X86::GR8RegClass.contains(DestReg, SrcReg)) {
    // Copying to or from a physical H register on x86-64 requires a NOREX
    // move.  Otherwise use a normal move.
    if ((isHReg(DestReg) || isHReg(SrcReg)) &&
        Subtarget.is64Bit()) {
      Opc = X86::MOV8rr_NOREX;
      // Both operands must be encodable without an REX prefix.
      assert(X86::GR8_NOREXRegClass.contains(SrcReg, DestReg) &&
             "8-bit H register can not be copied outside GR8_NOREX");
    } else
      Opc = X86::MOV8rr;
  }
  else if (X86::VR64RegClass.contains(DestReg, SrcReg))
    Opc = X86::MMX_MOVQ64rr;
  else if (X86::VR128XRegClass.contains(DestReg, SrcReg)) {
    if (HasVLX)
      Opc = X86::VMOVAPSZ128rr;
    else if (X86::VR128RegClass.contains(DestReg, SrcReg))
      Opc = HasAVX ? X86::VMOVAPSrr : X86::MOVAPSrr;
    else {
      // If this an extended register and we don't have VLX we need to use a
      // 512-bit move.
      Opc = X86::VMOVAPSZrr;
      const TargetRegisterInfo *TRI = &getRegisterInfo();
      DestReg = TRI->getMatchingSuperReg(DestReg, X86::sub_xmm,
                                         &X86::VR512RegClass);
      SrcReg = TRI->getMatchingSuperReg(SrcReg, X86::sub_xmm,
                                        &X86::VR512RegClass);
    }
  } else if (X86::VR256XRegClass.contains(DestReg, SrcReg)) {
    if (HasVLX)
      Opc = X86::VMOVAPSZ256rr;
    else if (X86::VR256RegClass.contains(DestReg, SrcReg))
      Opc = X86::VMOVAPSYrr;
    else {
      // If this an extended register and we don't have VLX we need to use a
      // 512-bit move.
      Opc = X86::VMOVAPSZrr;
      const TargetRegisterInfo *TRI = &getRegisterInfo();
      DestReg = TRI->getMatchingSuperReg(DestReg, X86::sub_ymm,
                                         &X86::VR512RegClass);
      SrcReg = TRI->getMatchingSuperReg(SrcReg, X86::sub_ymm,
                                        &X86::VR512RegClass);
    }
  } else if (X86::VR512RegClass.contains(DestReg, SrcReg))
    Opc = X86::VMOVAPSZrr;
  // All KMASK RegClasses hold the same k registers, can be tested against anyone.
  else if (X86::VK16RegClass.contains(DestReg, SrcReg))
    Opc = Subtarget.hasBWI() ? X86::KMOVQkk : X86::KMOVWkk;
  if (!Opc)
    Opc = CopyToFromAsymmetricReg(DestReg, SrcReg, Subtarget);

  if (Opc) {
    BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (SrcReg == X86::EFLAGS || DestReg == X86::EFLAGS) {
    // FIXME: We use a fatal error here because historically LLVM has tried
    // lower some of these physreg copies and we want to ensure we get
    // reasonable bug reports if someone encounters a case no other testing
    // found. This path should be removed after the LLVM 7 release.
    report_fatal_error("Unable to copy EFLAGS physical register!");
  }

  LLVM_DEBUG(dbgs() << "Cannot copy " << RI.getName(SrcReg) << " to "
                    << RI.getName(DestReg) << '\n');
  report_fatal_error("Cannot emit physreg copy instruction");
}

bool X86InstrInfo::isCopyInstrImpl(const MachineInstr &MI,
                                   const MachineOperand *&Src,
                                   const MachineOperand *&Dest) const {
  if (MI.isMoveReg()) {
    Dest = &MI.getOperand(0);
    Src = &MI.getOperand(1);
    return true;
  }
  return false;
}

static unsigned getLoadStoreRegOpcode(unsigned Reg,
                                      const TargetRegisterClass *RC,
                                      bool isStackAligned,
                                      const X86Subtarget &STI,
                                      bool load) {
  bool HasAVX = STI.hasAVX();
  bool HasAVX512 = STI.hasAVX512();
  bool HasVLX = STI.hasVLX();

  switch (STI.getRegisterInfo()->getSpillSize(*RC)) {
  default:
    llvm_unreachable("Unknown spill size");
  case 1:
    assert(X86::GR8RegClass.hasSubClassEq(RC) && "Unknown 1-byte regclass");
    if (STI.is64Bit())
      // Copying to or from a physical H register on x86-64 requires a NOREX
      // move.  Otherwise use a normal move.
      if (isHReg(Reg) || X86::GR8_ABCD_HRegClass.hasSubClassEq(RC))
        return load ? X86::MOV8rm_NOREX : X86::MOV8mr_NOREX;
    return load ? X86::MOV8rm : X86::MOV8mr;
  case 2:
    if (X86::VK16RegClass.hasSubClassEq(RC))
      return load ? X86::KMOVWkm : X86::KMOVWmk;
    assert(X86::GR16RegClass.hasSubClassEq(RC) && "Unknown 2-byte regclass");
    return load ? X86::MOV16rm : X86::MOV16mr;
  case 4:
    if (X86::GR32RegClass.hasSubClassEq(RC))
      return load ? X86::MOV32rm : X86::MOV32mr;
    if (X86::FR32XRegClass.hasSubClassEq(RC))
      return load ?
        (HasAVX512 ? X86::VMOVSSZrm : HasAVX ? X86::VMOVSSrm : X86::MOVSSrm) :
        (HasAVX512 ? X86::VMOVSSZmr : HasAVX ? X86::VMOVSSmr : X86::MOVSSmr);
    if (X86::RFP32RegClass.hasSubClassEq(RC))
      return load ? X86::LD_Fp32m : X86::ST_Fp32m;
    if (X86::VK32RegClass.hasSubClassEq(RC)) {
      assert(STI.hasBWI() && "KMOVD requires BWI");
      return load ? X86::KMOVDkm : X86::KMOVDmk;
    }
    llvm_unreachable("Unknown 4-byte regclass");
  case 8:
    if (X86::GR64RegClass.hasSubClassEq(RC))
      return load ? X86::MOV64rm : X86::MOV64mr;
    if (X86::FR64XRegClass.hasSubClassEq(RC))
      return load ?
        (HasAVX512 ? X86::VMOVSDZrm : HasAVX ? X86::VMOVSDrm : X86::MOVSDrm) :
        (HasAVX512 ? X86::VMOVSDZmr : HasAVX ? X86::VMOVSDmr : X86::MOVSDmr);
    if (X86::VR64RegClass.hasSubClassEq(RC))
      return load ? X86::MMX_MOVQ64rm : X86::MMX_MOVQ64mr;
    if (X86::RFP64RegClass.hasSubClassEq(RC))
      return load ? X86::LD_Fp64m : X86::ST_Fp64m;
    if (X86::VK64RegClass.hasSubClassEq(RC)) {
      assert(STI.hasBWI() && "KMOVQ requires BWI");
      return load ? X86::KMOVQkm : X86::KMOVQmk;
    }
    llvm_unreachable("Unknown 8-byte regclass");
  case 10:
    assert(X86::RFP80RegClass.hasSubClassEq(RC) && "Unknown 10-byte regclass");
    return load ? X86::LD_Fp80m : X86::ST_FpP80m;
  case 16: {
    if (X86::VR128XRegClass.hasSubClassEq(RC)) {
      // If stack is realigned we can use aligned stores.
      if (isStackAligned)
        return load ?
          (HasVLX    ? X86::VMOVAPSZ128rm :
           HasAVX512 ? X86::VMOVAPSZ128rm_NOVLX :
           HasAVX    ? X86::VMOVAPSrm :
                       X86::MOVAPSrm):
          (HasVLX    ? X86::VMOVAPSZ128mr :
           HasAVX512 ? X86::VMOVAPSZ128mr_NOVLX :
           HasAVX    ? X86::VMOVAPSmr :
                       X86::MOVAPSmr);
      else
        return load ?
          (HasVLX    ? X86::VMOVUPSZ128rm :
           HasAVX512 ? X86::VMOVUPSZ128rm_NOVLX :
           HasAVX    ? X86::VMOVUPSrm :
                       X86::MOVUPSrm):
          (HasVLX    ? X86::VMOVUPSZ128mr :
           HasAVX512 ? X86::VMOVUPSZ128mr_NOVLX :
           HasAVX    ? X86::VMOVUPSmr :
                       X86::MOVUPSmr);
    }
    if (X86::BNDRRegClass.hasSubClassEq(RC)) {
      if (STI.is64Bit())
        return load ? X86::BNDMOV64rm : X86::BNDMOV64mr;
      else
        return load ? X86::BNDMOV32rm : X86::BNDMOV32mr;
    }
    llvm_unreachable("Unknown 16-byte regclass");
  }
  case 32:
    assert(X86::VR256XRegClass.hasSubClassEq(RC) && "Unknown 32-byte regclass");
    // If stack is realigned we can use aligned stores.
    if (isStackAligned)
      return load ?
        (HasVLX    ? X86::VMOVAPSZ256rm :
         HasAVX512 ? X86::VMOVAPSZ256rm_NOVLX :
                     X86::VMOVAPSYrm) :
        (HasVLX    ? X86::VMOVAPSZ256mr :
         HasAVX512 ? X86::VMOVAPSZ256mr_NOVLX :
                     X86::VMOVAPSYmr);
    else
      return load ?
        (HasVLX    ? X86::VMOVUPSZ256rm :
         HasAVX512 ? X86::VMOVUPSZ256rm_NOVLX :
                     X86::VMOVUPSYrm) :
        (HasVLX    ? X86::VMOVUPSZ256mr :
         HasAVX512 ? X86::VMOVUPSZ256mr_NOVLX :
                     X86::VMOVUPSYmr);
  case 64:
    assert(X86::VR512RegClass.hasSubClassEq(RC) && "Unknown 64-byte regclass");
    assert(STI.hasAVX512() && "Using 512-bit register requires AVX512");
    if (isStackAligned)
      return load ? X86::VMOVAPSZrm : X86::VMOVAPSZmr;
    else
      return load ? X86::VMOVUPSZrm : X86::VMOVUPSZmr;
  }
}

bool X86InstrInfo::getMemOperandWithOffset(
    MachineInstr &MemOp, MachineOperand *&BaseOp, int64_t &Offset,
    const TargetRegisterInfo *TRI) const {
  const MCInstrDesc &Desc = MemOp.getDesc();
  int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
  if (MemRefBegin < 0)
    return false;

  MemRefBegin += X86II::getOperandBias(Desc);

  BaseOp = &MemOp.getOperand(MemRefBegin + X86::AddrBaseReg);
  if (!BaseOp->isReg()) // Can be an MO_FrameIndex
    return false;

  if (MemOp.getOperand(MemRefBegin + X86::AddrScaleAmt).getImm() != 1)
    return false;

  if (MemOp.getOperand(MemRefBegin + X86::AddrIndexReg).getReg() !=
      X86::NoRegister)
    return false;

  const MachineOperand &DispMO = MemOp.getOperand(MemRefBegin + X86::AddrDisp);

  // Displacement can be symbolic
  if (!DispMO.isImm())
    return false;

  Offset = DispMO.getImm();

  assert(BaseOp->isReg() && "getMemOperandWithOffset only supports base "
                            "operands of type register.");
  return true;
}

static unsigned getStoreRegOpcode(unsigned SrcReg,
                                  const TargetRegisterClass *RC,
                                  bool isStackAligned,
                                  const X86Subtarget &STI) {
  return getLoadStoreRegOpcode(SrcReg, RC, isStackAligned, STI, false);
}


static unsigned getLoadRegOpcode(unsigned DestReg,
                                 const TargetRegisterClass *RC,
                                 bool isStackAligned,
                                 const X86Subtarget &STI) {
  return getLoadStoreRegOpcode(DestReg, RC, isStackAligned, STI, true);
}

void X86InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       unsigned SrcReg, bool isKill, int FrameIdx,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  const MachineFunction &MF = *MBB.getParent();
  assert(MF.getFrameInfo().getObjectSize(FrameIdx) >= TRI->getSpillSize(*RC) &&
         "Stack slot too small for store");
  unsigned Alignment = std::max<uint32_t>(TRI->getSpillSize(*RC), 16);
  bool isAligned =
      (Subtarget.getFrameLowering()->getStackAlignment() >= Alignment) ||
      RI.canRealignStack(MF);
  unsigned Opc = getStoreRegOpcode(SrcReg, RC, isAligned, Subtarget);
  addFrameReference(BuildMI(MBB, MI, DebugLoc(), get(Opc)), FrameIdx)
    .addReg(SrcReg, getKillRegState(isKill));
}

void X86InstrInfo::storeRegToAddr(
    MachineFunction &MF, unsigned SrcReg, bool isKill,
    SmallVectorImpl<MachineOperand> &Addr, const TargetRegisterClass *RC,
    ArrayRef<MachineMemOperand *> MMOs,
    SmallVectorImpl<MachineInstr *> &NewMIs) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
  bool isAligned = !MMOs.empty() && MMOs.front()->getAlignment() >= Alignment;
  unsigned Opc = getStoreRegOpcode(SrcReg, RC, isAligned, Subtarget);
  DebugLoc DL;
  MachineInstrBuilder MIB = BuildMI(MF, DL, get(Opc));
  for (unsigned i = 0, e = Addr.size(); i != e; ++i)
    MIB.add(Addr[i]);
  MIB.addReg(SrcReg, getKillRegState(isKill));
  MIB.setMemRefs(MMOs);
  NewMIs.push_back(MIB);
}


void X86InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        unsigned DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI) const {
  const MachineFunction &MF = *MBB.getParent();
  unsigned Alignment = std::max<uint32_t>(TRI->getSpillSize(*RC), 16);
  bool isAligned =
      (Subtarget.getFrameLowering()->getStackAlignment() >= Alignment) ||
      RI.canRealignStack(MF);
  unsigned Opc = getLoadRegOpcode(DestReg, RC, isAligned, Subtarget);
  addFrameReference(BuildMI(MBB, MI, DebugLoc(), get(Opc), DestReg), FrameIdx);
}

void X86InstrInfo::loadRegFromAddr(
    MachineFunction &MF, unsigned DestReg,
    SmallVectorImpl<MachineOperand> &Addr, const TargetRegisterClass *RC,
    ArrayRef<MachineMemOperand *> MMOs,
    SmallVectorImpl<MachineInstr *> &NewMIs) const {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
  bool isAligned = !MMOs.empty() && MMOs.front()->getAlignment() >= Alignment;
  unsigned Opc = getLoadRegOpcode(DestReg, RC, isAligned, Subtarget);
  DebugLoc DL;
  MachineInstrBuilder MIB = BuildMI(MF, DL, get(Opc), DestReg);
  for (unsigned i = 0, e = Addr.size(); i != e; ++i)
    MIB.add(Addr[i]);
  MIB.setMemRefs(MMOs);
  NewMIs.push_back(MIB);
}

bool X86InstrInfo::analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                                  unsigned &SrcReg2, int &CmpMask,
                                  int &CmpValue) const {
  switch (MI.getOpcode()) {
  default: break;
  case X86::CMP64ri32:
  case X86::CMP64ri8:
  case X86::CMP32ri:
  case X86::CMP32ri8:
  case X86::CMP16ri:
  case X86::CMP16ri8:
  case X86::CMP8ri:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    if (MI.getOperand(1).isImm()) {
      CmpMask = ~0;
      CmpValue = MI.getOperand(1).getImm();
    } else {
      CmpMask = CmpValue = 0;
    }
    return true;
  // A SUB can be used to perform comparison.
  case X86::SUB64rm:
  case X86::SUB32rm:
  case X86::SUB16rm:
  case X86::SUB8rm:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    CmpMask = 0;
    CmpValue = 0;
    return true;
  case X86::SUB64rr:
  case X86::SUB32rr:
  case X86::SUB16rr:
  case X86::SUB8rr:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = MI.getOperand(2).getReg();
    CmpMask = 0;
    CmpValue = 0;
    return true;
  case X86::SUB64ri32:
  case X86::SUB64ri8:
  case X86::SUB32ri:
  case X86::SUB32ri8:
  case X86::SUB16ri:
  case X86::SUB16ri8:
  case X86::SUB8ri:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    if (MI.getOperand(2).isImm()) {
      CmpMask = ~0;
      CmpValue = MI.getOperand(2).getImm();
    } else {
      CmpMask = CmpValue = 0;
    }
    return true;
  case X86::CMP64rr:
  case X86::CMP32rr:
  case X86::CMP16rr:
  case X86::CMP8rr:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = MI.getOperand(1).getReg();
    CmpMask = 0;
    CmpValue = 0;
    return true;
  case X86::TEST8rr:
  case X86::TEST16rr:
  case X86::TEST32rr:
  case X86::TEST64rr:
    SrcReg = MI.getOperand(0).getReg();
    if (MI.getOperand(1).getReg() != SrcReg)
      return false;
    // Compare against zero.
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  }
  return false;
}

/// Check whether the first instruction, whose only
/// purpose is to update flags, can be made redundant.
/// CMPrr can be made redundant by SUBrr if the operands are the same.
/// This function can be extended later on.
/// SrcReg, SrcRegs: register operands for FlagI.
/// ImmValue: immediate for FlagI if it takes an immediate.
inline static bool isRedundantFlagInstr(const MachineInstr &FlagI,
                                        unsigned SrcReg, unsigned SrcReg2,
                                        int ImmMask, int ImmValue,
                                        const MachineInstr &OI) {
  if (((FlagI.getOpcode() == X86::CMP64rr && OI.getOpcode() == X86::SUB64rr) ||
       (FlagI.getOpcode() == X86::CMP32rr && OI.getOpcode() == X86::SUB32rr) ||
       (FlagI.getOpcode() == X86::CMP16rr && OI.getOpcode() == X86::SUB16rr) ||
       (FlagI.getOpcode() == X86::CMP8rr && OI.getOpcode() == X86::SUB8rr)) &&
      ((OI.getOperand(1).getReg() == SrcReg &&
        OI.getOperand(2).getReg() == SrcReg2) ||
       (OI.getOperand(1).getReg() == SrcReg2 &&
        OI.getOperand(2).getReg() == SrcReg)))
    return true;

  if (ImmMask != 0 &&
      ((FlagI.getOpcode() == X86::CMP64ri32 &&
        OI.getOpcode() == X86::SUB64ri32) ||
       (FlagI.getOpcode() == X86::CMP64ri8 &&
        OI.getOpcode() == X86::SUB64ri8) ||
       (FlagI.getOpcode() == X86::CMP32ri && OI.getOpcode() == X86::SUB32ri) ||
       (FlagI.getOpcode() == X86::CMP32ri8 &&
        OI.getOpcode() == X86::SUB32ri8) ||
       (FlagI.getOpcode() == X86::CMP16ri && OI.getOpcode() == X86::SUB16ri) ||
       (FlagI.getOpcode() == X86::CMP16ri8 &&
        OI.getOpcode() == X86::SUB16ri8) ||
       (FlagI.getOpcode() == X86::CMP8ri && OI.getOpcode() == X86::SUB8ri)) &&
      OI.getOperand(1).getReg() == SrcReg &&
      OI.getOperand(2).getImm() == ImmValue)
    return true;
  return false;
}

/// Check whether the definition can be converted
/// to remove a comparison against zero.
inline static bool isDefConvertible(const MachineInstr &MI, bool &NoSignFlag) {
  NoSignFlag = false;

  switch (MI.getOpcode()) {
  default: return false;

  // The shift instructions only modify ZF if their shift count is non-zero.
  // N.B.: The processor truncates the shift count depending on the encoding.
  case X86::SAR8ri:    case X86::SAR16ri:  case X86::SAR32ri:case X86::SAR64ri:
  case X86::SHR8ri:    case X86::SHR16ri:  case X86::SHR32ri:case X86::SHR64ri:
     return getTruncatedShiftCount(MI, 2) != 0;

  // Some left shift instructions can be turned into LEA instructions but only
  // if their flags aren't used. Avoid transforming such instructions.
  case X86::SHL8ri:    case X86::SHL16ri:  case X86::SHL32ri:case X86::SHL64ri:{
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (isTruncatedShiftCountForLEA(ShAmt)) return false;
    return ShAmt != 0;
  }

  case X86::SHRD16rri8:case X86::SHRD32rri8:case X86::SHRD64rri8:
  case X86::SHLD16rri8:case X86::SHLD32rri8:case X86::SHLD64rri8:
     return getTruncatedShiftCount(MI, 3) != 0;

  case X86::SUB64ri32: case X86::SUB64ri8: case X86::SUB32ri:
  case X86::SUB32ri8:  case X86::SUB16ri:  case X86::SUB16ri8:
  case X86::SUB8ri:    case X86::SUB64rr:  case X86::SUB32rr:
  case X86::SUB16rr:   case X86::SUB8rr:   case X86::SUB64rm:
  case X86::SUB32rm:   case X86::SUB16rm:  case X86::SUB8rm:
  case X86::DEC64r:    case X86::DEC32r:   case X86::DEC16r: case X86::DEC8r:
  case X86::ADD64ri32: case X86::ADD64ri8: case X86::ADD32ri:
  case X86::ADD32ri8:  case X86::ADD16ri:  case X86::ADD16ri8:
  case X86::ADD8ri:    case X86::ADD64rr:  case X86::ADD32rr:
  case X86::ADD16rr:   case X86::ADD8rr:   case X86::ADD64rm:
  case X86::ADD32rm:   case X86::ADD16rm:  case X86::ADD8rm:
  case X86::INC64r:    case X86::INC32r:   case X86::INC16r: case X86::INC8r:
  case X86::AND64ri32: case X86::AND64ri8: case X86::AND32ri:
  case X86::AND32ri8:  case X86::AND16ri:  case X86::AND16ri8:
  case X86::AND8ri:    case X86::AND64rr:  case X86::AND32rr:
  case X86::AND16rr:   case X86::AND8rr:   case X86::AND64rm:
  case X86::AND32rm:   case X86::AND16rm:  case X86::AND8rm:
  case X86::XOR64ri32: case X86::XOR64ri8: case X86::XOR32ri:
  case X86::XOR32ri8:  case X86::XOR16ri:  case X86::XOR16ri8:
  case X86::XOR8ri:    case X86::XOR64rr:  case X86::XOR32rr:
  case X86::XOR16rr:   case X86::XOR8rr:   case X86::XOR64rm:
  case X86::XOR32rm:   case X86::XOR16rm:  case X86::XOR8rm:
  case X86::OR64ri32:  case X86::OR64ri8:  case X86::OR32ri:
  case X86::OR32ri8:   case X86::OR16ri:   case X86::OR16ri8:
  case X86::OR8ri:     case X86::OR64rr:   case X86::OR32rr:
  case X86::OR16rr:    case X86::OR8rr:    case X86::OR64rm:
  case X86::OR32rm:    case X86::OR16rm:   case X86::OR8rm:
  case X86::ADC64ri32: case X86::ADC64ri8: case X86::ADC32ri:
  case X86::ADC32ri8:  case X86::ADC16ri:  case X86::ADC16ri8:
  case X86::ADC8ri:    case X86::ADC64rr:  case X86::ADC32rr:
  case X86::ADC16rr:   case X86::ADC8rr:   case X86::ADC64rm:
  case X86::ADC32rm:   case X86::ADC16rm:  case X86::ADC8rm:
  case X86::SBB64ri32: case X86::SBB64ri8: case X86::SBB32ri:
  case X86::SBB32ri8:  case X86::SBB16ri:  case X86::SBB16ri8:
  case X86::SBB8ri:    case X86::SBB64rr:  case X86::SBB32rr:
  case X86::SBB16rr:   case X86::SBB8rr:   case X86::SBB64rm:
  case X86::SBB32rm:   case X86::SBB16rm:  case X86::SBB8rm:
  case X86::NEG8r:     case X86::NEG16r:   case X86::NEG32r: case X86::NEG64r:
  case X86::SAR8r1:    case X86::SAR16r1:  case X86::SAR32r1:case X86::SAR64r1:
  case X86::SHR8r1:    case X86::SHR16r1:  case X86::SHR32r1:case X86::SHR64r1:
  case X86::SHL8r1:    case X86::SHL16r1:  case X86::SHL32r1:case X86::SHL64r1:
  case X86::ANDN32rr:  case X86::ANDN32rm:
  case X86::ANDN64rr:  case X86::ANDN64rm:
  case X86::BLSI32rr:  case X86::BLSI32rm:
  case X86::BLSI64rr:  case X86::BLSI64rm:
  case X86::BLSMSK32rr:case X86::BLSMSK32rm:
  case X86::BLSMSK64rr:case X86::BLSMSK64rm:
  case X86::BLSR32rr:  case X86::BLSR32rm:
  case X86::BLSR64rr:  case X86::BLSR64rm:
  case X86::BZHI32rr:  case X86::BZHI32rm:
  case X86::BZHI64rr:  case X86::BZHI64rm:
  case X86::LZCNT16rr: case X86::LZCNT16rm:
  case X86::LZCNT32rr: case X86::LZCNT32rm:
  case X86::LZCNT64rr: case X86::LZCNT64rm:
  case X86::POPCNT16rr:case X86::POPCNT16rm:
  case X86::POPCNT32rr:case X86::POPCNT32rm:
  case X86::POPCNT64rr:case X86::POPCNT64rm:
  case X86::TZCNT16rr: case X86::TZCNT16rm:
  case X86::TZCNT32rr: case X86::TZCNT32rm:
  case X86::TZCNT64rr: case X86::TZCNT64rm:
  case X86::BLCFILL32rr: case X86::BLCFILL32rm:
  case X86::BLCFILL64rr: case X86::BLCFILL64rm:
  case X86::BLCI32rr:    case X86::BLCI32rm:
  case X86::BLCI64rr:    case X86::BLCI64rm:
  case X86::BLCIC32rr:   case X86::BLCIC32rm:
  case X86::BLCIC64rr:   case X86::BLCIC64rm:
  case X86::BLCMSK32rr:  case X86::BLCMSK32rm:
  case X86::BLCMSK64rr:  case X86::BLCMSK64rm:
  case X86::BLCS32rr:    case X86::BLCS32rm:
  case X86::BLCS64rr:    case X86::BLCS64rm:
  case X86::BLSFILL32rr: case X86::BLSFILL32rm:
  case X86::BLSFILL64rr: case X86::BLSFILL64rm:
  case X86::BLSIC32rr:   case X86::BLSIC32rm:
  case X86::BLSIC64rr:   case X86::BLSIC64rm:
  case X86::T1MSKC32rr:  case X86::T1MSKC32rm:
  case X86::T1MSKC64rr:  case X86::T1MSKC64rm:
  case X86::TZMSK32rr:   case X86::TZMSK32rm:
  case X86::TZMSK64rr:   case X86::TZMSK64rm:
    return true;
  case X86::BEXTR32rr:   case X86::BEXTR64rr:
  case X86::BEXTR32rm:   case X86::BEXTR64rm:
  case X86::BEXTRI32ri:  case X86::BEXTRI32mi:
  case X86::BEXTRI64ri:  case X86::BEXTRI64mi:
    // BEXTR doesn't update the sign flag so we can't use it.
    NoSignFlag = true;
    return true;
  }
}

/// Check whether the use can be converted to remove a comparison against zero.
static X86::CondCode isUseDefConvertible(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default: return X86::COND_INVALID;
  case X86::LZCNT16rr: case X86::LZCNT16rm:
  case X86::LZCNT32rr: case X86::LZCNT32rm:
  case X86::LZCNT64rr: case X86::LZCNT64rm:
    return X86::COND_B;
  case X86::POPCNT16rr:case X86::POPCNT16rm:
  case X86::POPCNT32rr:case X86::POPCNT32rm:
  case X86::POPCNT64rr:case X86::POPCNT64rm:
    return X86::COND_E;
  case X86::TZCNT16rr: case X86::TZCNT16rm:
  case X86::TZCNT32rr: case X86::TZCNT32rm:
  case X86::TZCNT64rr: case X86::TZCNT64rm:
    return X86::COND_B;
  case X86::BSF16rr: case X86::BSF16rm:
  case X86::BSF32rr: case X86::BSF32rm:
  case X86::BSF64rr: case X86::BSF64rm:
  case X86::BSR16rr: case X86::BSR16rm:
  case X86::BSR32rr: case X86::BSR32rm:
  case X86::BSR64rr: case X86::BSR64rm:
    return X86::COND_E;
  }
}

/// Check if there exists an earlier instruction that
/// operates on the same source operands and sets flags in the same way as
/// Compare; remove Compare if possible.
bool X86InstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, unsigned SrcReg,
                                        unsigned SrcReg2, int CmpMask,
                                        int CmpValue,
                                        const MachineRegisterInfo *MRI) const {
  // Check whether we can replace SUB with CMP.
  unsigned NewOpcode = 0;
  switch (CmpInstr.getOpcode()) {
  default: break;
  case X86::SUB64ri32:
  case X86::SUB64ri8:
  case X86::SUB32ri:
  case X86::SUB32ri8:
  case X86::SUB16ri:
  case X86::SUB16ri8:
  case X86::SUB8ri:
  case X86::SUB64rm:
  case X86::SUB32rm:
  case X86::SUB16rm:
  case X86::SUB8rm:
  case X86::SUB64rr:
  case X86::SUB32rr:
  case X86::SUB16rr:
  case X86::SUB8rr: {
    if (!MRI->use_nodbg_empty(CmpInstr.getOperand(0).getReg()))
      return false;
    // There is no use of the destination register, we can replace SUB with CMP.
    switch (CmpInstr.getOpcode()) {
    default: llvm_unreachable("Unreachable!");
    case X86::SUB64rm:   NewOpcode = X86::CMP64rm;   break;
    case X86::SUB32rm:   NewOpcode = X86::CMP32rm;   break;
    case X86::SUB16rm:   NewOpcode = X86::CMP16rm;   break;
    case X86::SUB8rm:    NewOpcode = X86::CMP8rm;    break;
    case X86::SUB64rr:   NewOpcode = X86::CMP64rr;   break;
    case X86::SUB32rr:   NewOpcode = X86::CMP32rr;   break;
    case X86::SUB16rr:   NewOpcode = X86::CMP16rr;   break;
    case X86::SUB8rr:    NewOpcode = X86::CMP8rr;    break;
    case X86::SUB64ri32: NewOpcode = X86::CMP64ri32; break;
    case X86::SUB64ri8:  NewOpcode = X86::CMP64ri8;  break;
    case X86::SUB32ri:   NewOpcode = X86::CMP32ri;   break;
    case X86::SUB32ri8:  NewOpcode = X86::CMP32ri8;  break;
    case X86::SUB16ri:   NewOpcode = X86::CMP16ri;   break;
    case X86::SUB16ri8:  NewOpcode = X86::CMP16ri8;  break;
    case X86::SUB8ri:    NewOpcode = X86::CMP8ri;    break;
    }
    CmpInstr.setDesc(get(NewOpcode));
    CmpInstr.RemoveOperand(0);
    // Fall through to optimize Cmp if Cmp is CMPrr or CMPri.
    if (NewOpcode == X86::CMP64rm || NewOpcode == X86::CMP32rm ||
        NewOpcode == X86::CMP16rm || NewOpcode == X86::CMP8rm)
      return false;
  }
  }

  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI->getUniqueVRegDef(SrcReg);
  if (!MI) return false;

  // CmpInstr is the first instruction of the BB.
  MachineBasicBlock::iterator I = CmpInstr, Def = MI;

  // If we are comparing against zero, check whether we can use MI to update
  // EFLAGS. If MI is not in the same BB as CmpInstr, do not optimize.
  bool IsCmpZero = (CmpMask != 0 && CmpValue == 0);
  if (IsCmpZero && MI->getParent() != CmpInstr.getParent())
    return false;

  // If we have a use of the source register between the def and our compare
  // instruction we can eliminate the compare iff the use sets EFLAGS in the
  // right way.
  bool ShouldUpdateCC = false;
  bool NoSignFlag = false;
  X86::CondCode NewCC = X86::COND_INVALID;
  if (IsCmpZero && !isDefConvertible(*MI, NoSignFlag)) {
    // Scan forward from the use until we hit the use we're looking for or the
    // compare instruction.
    for (MachineBasicBlock::iterator J = MI;; ++J) {
      // Do we have a convertible instruction?
      NewCC = isUseDefConvertible(*J);
      if (NewCC != X86::COND_INVALID && J->getOperand(1).isReg() &&
          J->getOperand(1).getReg() == SrcReg) {
        assert(J->definesRegister(X86::EFLAGS) && "Must be an EFLAGS def!");
        ShouldUpdateCC = true; // Update CC later on.
        // This is not a def of SrcReg, but still a def of EFLAGS. Keep going
        // with the new def.
        Def = J;
        MI = &*Def;
        break;
      }

      if (J == I)
        return false;
    }
  }

  // We are searching for an earlier instruction that can make CmpInstr
  // redundant and that instruction will be saved in Sub.
  MachineInstr *Sub = nullptr;
  const TargetRegisterInfo *TRI = &getRegisterInfo();

  // We iterate backward, starting from the instruction before CmpInstr and
  // stop when reaching the definition of a source register or done with the BB.
  // RI points to the instruction before CmpInstr.
  // If the definition is in this basic block, RE points to the definition;
  // otherwise, RE is the rend of the basic block.
  MachineBasicBlock::reverse_iterator
      RI = ++I.getReverse(),
      RE = CmpInstr.getParent() == MI->getParent()
               ? Def.getReverse() /* points to MI */
               : CmpInstr.getParent()->rend();
  MachineInstr *Movr0Inst = nullptr;
  for (; RI != RE; ++RI) {
    MachineInstr &Instr = *RI;
    // Check whether CmpInstr can be made redundant by the current instruction.
    if (!IsCmpZero && isRedundantFlagInstr(CmpInstr, SrcReg, SrcReg2, CmpMask,
                                           CmpValue, Instr)) {
      Sub = &Instr;
      break;
    }

    if (Instr.modifiesRegister(X86::EFLAGS, TRI) ||
        Instr.readsRegister(X86::EFLAGS, TRI)) {
      // This instruction modifies or uses EFLAGS.

      // MOV32r0 etc. are implemented with xor which clobbers condition code.
      // They are safe to move up, if the definition to EFLAGS is dead and
      // earlier instructions do not read or write EFLAGS.
      if (!Movr0Inst && Instr.getOpcode() == X86::MOV32r0 &&
          Instr.registerDefIsDead(X86::EFLAGS, TRI)) {
        Movr0Inst = &Instr;
        continue;
      }

      // We can't remove CmpInstr.
      return false;
    }
  }

  // Return false if no candidates exist.
  if (!IsCmpZero && !Sub)
    return false;

  bool IsSwapped = (SrcReg2 != 0 && Sub->getOperand(1).getReg() == SrcReg2 &&
                    Sub->getOperand(2).getReg() == SrcReg);

  // Scan forward from the instruction after CmpInstr for uses of EFLAGS.
  // It is safe to remove CmpInstr if EFLAGS is redefined or killed.
  // If we are done with the basic block, we need to check whether EFLAGS is
  // live-out.
  bool IsSafe = false;
  SmallVector<std::pair<MachineInstr*, unsigned /*NewOpc*/>, 4> OpsToUpdate;
  MachineBasicBlock::iterator E = CmpInstr.getParent()->end();
  for (++I; I != E; ++I) {
    const MachineInstr &Instr = *I;
    bool ModifyEFLAGS = Instr.modifiesRegister(X86::EFLAGS, TRI);
    bool UseEFLAGS = Instr.readsRegister(X86::EFLAGS, TRI);
    // We should check the usage if this instruction uses and updates EFLAGS.
    if (!UseEFLAGS && ModifyEFLAGS) {
      // It is safe to remove CmpInstr if EFLAGS is updated again.
      IsSafe = true;
      break;
    }
    if (!UseEFLAGS && !ModifyEFLAGS)
      continue;

    // EFLAGS is used by this instruction.
    X86::CondCode OldCC = X86::COND_INVALID;
    bool OpcIsSET = false;
    if (IsCmpZero || IsSwapped) {
      // We decode the condition code from opcode.
      if (Instr.isBranch())
        OldCC = X86::getCondFromBranchOpc(Instr.getOpcode());
      else {
        OldCC = X86::getCondFromSETOpc(Instr.getOpcode());
        if (OldCC != X86::COND_INVALID)
          OpcIsSET = true;
        else
          OldCC = X86::getCondFromCMovOpc(Instr.getOpcode());
      }
      if (OldCC == X86::COND_INVALID) return false;
    }
    X86::CondCode ReplacementCC = X86::COND_INVALID;
    if (IsCmpZero) {
      switch (OldCC) {
      default: break;
      case X86::COND_A: case X86::COND_AE:
      case X86::COND_B: case X86::COND_BE:
      case X86::COND_G: case X86::COND_GE:
      case X86::COND_L: case X86::COND_LE:
      case X86::COND_O: case X86::COND_NO:
        // CF and OF are used, we can't perform this optimization.
        return false;
      case X86::COND_S: case X86::COND_NS:
        // If SF is used, but the instruction doesn't update the SF, then we
        // can't do the optimization.
        if (NoSignFlag)
          return false;
        break;
      }

      // If we're updating the condition code check if we have to reverse the
      // condition.
      if (ShouldUpdateCC)
        switch (OldCC) {
        default:
          return false;
        case X86::COND_E:
          ReplacementCC = NewCC;
          break;
        case X86::COND_NE:
          ReplacementCC = GetOppositeBranchCondition(NewCC);
          break;
        }
    } else if (IsSwapped) {
      // If we have SUB(r1, r2) and CMP(r2, r1), the condition code needs
      // to be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
      // We swap the condition code and synthesize the new opcode.
      ReplacementCC = getSwappedCondition(OldCC);
      if (ReplacementCC == X86::COND_INVALID) return false;
    }

    if ((ShouldUpdateCC || IsSwapped) && ReplacementCC != OldCC) {
      // Synthesize the new opcode.
      bool HasMemoryOperand = Instr.hasOneMemOperand();
      unsigned NewOpc;
      if (Instr.isBranch())
        NewOpc = GetCondBranchFromCond(ReplacementCC);
      else if(OpcIsSET)
        NewOpc = getSETFromCond(ReplacementCC, HasMemoryOperand);
      else {
        unsigned DstReg = Instr.getOperand(0).getReg();
        const TargetRegisterClass *DstRC = MRI->getRegClass(DstReg);
        NewOpc = getCMovFromCond(ReplacementCC, TRI->getRegSizeInBits(*DstRC)/8,
                                 HasMemoryOperand);
      }

      // Push the MachineInstr to OpsToUpdate.
      // If it is safe to remove CmpInstr, the condition code of these
      // instructions will be modified.
      OpsToUpdate.push_back(std::make_pair(&*I, NewOpc));
    }
    if (ModifyEFLAGS || Instr.killsRegister(X86::EFLAGS, TRI)) {
      // It is safe to remove CmpInstr if EFLAGS is updated again or killed.
      IsSafe = true;
      break;
    }
  }

  // If EFLAGS is not killed nor re-defined, we should check whether it is
  // live-out. If it is live-out, do not optimize.
  if ((IsCmpZero || IsSwapped) && !IsSafe) {
    MachineBasicBlock *MBB = CmpInstr.getParent();
    for (MachineBasicBlock *Successor : MBB->successors())
      if (Successor->isLiveIn(X86::EFLAGS))
        return false;
  }

  // The instruction to be updated is either Sub or MI.
  Sub = IsCmpZero ? MI : Sub;
  // Move Movr0Inst to the appropriate place before Sub.
  if (Movr0Inst) {
    // Look backwards until we find a def that doesn't use the current EFLAGS.
    Def = Sub;
    MachineBasicBlock::reverse_iterator InsertI = Def.getReverse(),
                                        InsertE = Sub->getParent()->rend();
    for (; InsertI != InsertE; ++InsertI) {
      MachineInstr *Instr = &*InsertI;
      if (!Instr->readsRegister(X86::EFLAGS, TRI) &&
          Instr->modifiesRegister(X86::EFLAGS, TRI)) {
        Sub->getParent()->remove(Movr0Inst);
        Instr->getParent()->insert(MachineBasicBlock::iterator(Instr),
                                   Movr0Inst);
        break;
      }
    }
    if (InsertI == InsertE)
      return false;
  }

  // Make sure Sub instruction defines EFLAGS and mark the def live.
  unsigned i = 0, e = Sub->getNumOperands();
  for (; i != e; ++i) {
    MachineOperand &MO = Sub->getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == X86::EFLAGS) {
      MO.setIsDead(false);
      break;
    }
  }
  assert(i != e && "Unable to locate a def EFLAGS operand");

  CmpInstr.eraseFromParent();

  // Modify the condition code of instructions in OpsToUpdate.
  for (auto &Op : OpsToUpdate)
    Op.first->setDesc(get(Op.second));
  return true;
}

/// Try to remove the load by folding it to a register
/// operand at the use. We fold the load instructions if load defines a virtual
/// register, the virtual register is used once in the same BB, and the
/// instructions in-between do not load or store, and have no side effects.
MachineInstr *X86InstrInfo::optimizeLoadInstr(MachineInstr &MI,
                                              const MachineRegisterInfo *MRI,
                                              unsigned &FoldAsLoadDefReg,
                                              MachineInstr *&DefMI) const {
  // Check whether we can move DefMI here.
  DefMI = MRI->getVRegDef(FoldAsLoadDefReg);
  assert(DefMI);
  bool SawStore = false;
  if (!DefMI->isSafeToMove(nullptr, SawStore))
    return nullptr;

  // Collect information about virtual register operands of MI.
  SmallVector<unsigned, 1> SrcOperandIds;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (Reg != FoldAsLoadDefReg)
      continue;
    // Do not fold if we have a subreg use or a def.
    if (MO.getSubReg() || MO.isDef())
      return nullptr;
    SrcOperandIds.push_back(i);
  }
  if (SrcOperandIds.empty())
    return nullptr;

  // Check whether we can fold the def into SrcOperandId.
  if (MachineInstr *FoldMI = foldMemoryOperand(MI, SrcOperandIds, *DefMI)) {
    FoldAsLoadDefReg = 0;
    return FoldMI;
  }

  return nullptr;
}

/// Expand a single-def pseudo instruction to a two-addr
/// instruction with two undef reads of the register being defined.
/// This is used for mapping:
///   %xmm4 = V_SET0
/// to:
///   %xmm4 = PXORrr undef %xmm4, undef %xmm4
///
static bool Expand2AddrUndef(MachineInstrBuilder &MIB,
                             const MCInstrDesc &Desc) {
  assert(Desc.getNumOperands() == 3 && "Expected two-addr instruction.");
  unsigned Reg = MIB->getOperand(0).getReg();
  MIB->setDesc(Desc);

  // MachineInstr::addOperand() will insert explicit operands before any
  // implicit operands.
  MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef);
  // But we don't trust that.
  assert(MIB->getOperand(1).getReg() == Reg &&
         MIB->getOperand(2).getReg() == Reg && "Misplaced operand");
  return true;
}

/// Expand a single-def pseudo instruction to a two-addr
/// instruction with two %k0 reads.
/// This is used for mapping:
///   %k4 = K_SET1
/// to:
///   %k4 = KXNORrr %k0, %k0
static bool Expand2AddrKreg(MachineInstrBuilder &MIB,
                            const MCInstrDesc &Desc, unsigned Reg) {
  assert(Desc.getNumOperands() == 3 && "Expected two-addr instruction.");
  MIB->setDesc(Desc);
  MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef);
  return true;
}

static bool expandMOV32r1(MachineInstrBuilder &MIB, const TargetInstrInfo &TII,
                          bool MinusOne) {
  MachineBasicBlock &MBB = *MIB->getParent();
  DebugLoc DL = MIB->getDebugLoc();
  unsigned Reg = MIB->getOperand(0).getReg();

  // Insert the XOR.
  BuildMI(MBB, MIB.getInstr(), DL, TII.get(X86::XOR32rr), Reg)
      .addReg(Reg, RegState::Undef)
      .addReg(Reg, RegState::Undef);

  // Turn the pseudo into an INC or DEC.
  MIB->setDesc(TII.get(MinusOne ? X86::DEC32r : X86::INC32r));
  MIB.addReg(Reg);

  return true;
}

static bool ExpandMOVImmSExti8(MachineInstrBuilder &MIB,
                               const TargetInstrInfo &TII,
                               const X86Subtarget &Subtarget) {
  MachineBasicBlock &MBB = *MIB->getParent();
  DebugLoc DL = MIB->getDebugLoc();
  int64_t Imm = MIB->getOperand(1).getImm();
  assert(Imm != 0 && "Using push/pop for 0 is not efficient.");
  MachineBasicBlock::iterator I = MIB.getInstr();

  int StackAdjustment;

  if (Subtarget.is64Bit()) {
    assert(MIB->getOpcode() == X86::MOV64ImmSExti8 ||
           MIB->getOpcode() == X86::MOV32ImmSExti8);

    // Can't use push/pop lowering if the function might write to the red zone.
    X86MachineFunctionInfo *X86FI =
        MBB.getParent()->getInfo<X86MachineFunctionInfo>();
    if (X86FI->getUsesRedZone()) {
      MIB->setDesc(TII.get(MIB->getOpcode() ==
                           X86::MOV32ImmSExti8 ? X86::MOV32ri : X86::MOV64ri));
      return true;
    }

    // 64-bit mode doesn't have 32-bit push/pop, so use 64-bit operations and
    // widen the register if necessary.
    StackAdjustment = 8;
    BuildMI(MBB, I, DL, TII.get(X86::PUSH64i8)).addImm(Imm);
    MIB->setDesc(TII.get(X86::POP64r));
    MIB->getOperand(0)
        .setReg(getX86SubSuperRegister(MIB->getOperand(0).getReg(), 64));
  } else {
    assert(MIB->getOpcode() == X86::MOV32ImmSExti8);
    StackAdjustment = 4;
    BuildMI(MBB, I, DL, TII.get(X86::PUSH32i8)).addImm(Imm);
    MIB->setDesc(TII.get(X86::POP32r));
  }

  // Build CFI if necessary.
  MachineFunction &MF = *MBB.getParent();
  const X86FrameLowering *TFL = Subtarget.getFrameLowering();
  bool IsWin64Prologue = MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
  bool NeedsDwarfCFI =
      !IsWin64Prologue &&
      (MF.getMMI().hasDebugInfo() || MF.getFunction().needsUnwindTableEntry());
  bool EmitCFI = !TFL->hasFP(MF) && NeedsDwarfCFI;
  if (EmitCFI) {
    TFL->BuildCFI(MBB, I, DL,
        MCCFIInstruction::createAdjustCfaOffset(nullptr, StackAdjustment));
    TFL->BuildCFI(MBB, std::next(I), DL,
        MCCFIInstruction::createAdjustCfaOffset(nullptr, -StackAdjustment));
  }

  return true;
}

// LoadStackGuard has so far only been implemented for 64-bit MachO. Different
// code sequence is needed for other targets.
static void expandLoadStackGuard(MachineInstrBuilder &MIB,
                                 const TargetInstrInfo &TII) {
  MachineBasicBlock &MBB = *MIB->getParent();
  DebugLoc DL = MIB->getDebugLoc();
  unsigned Reg = MIB->getOperand(0).getReg();
  const GlobalValue *GV =
      cast<GlobalValue>((*MIB->memoperands_begin())->getValue());
  auto Flags = MachineMemOperand::MOLoad |
               MachineMemOperand::MODereferenceable |
               MachineMemOperand::MOInvariant;
  MachineMemOperand *MMO = MBB.getParent()->getMachineMemOperand(
      MachinePointerInfo::getGOT(*MBB.getParent()), Flags, 8, 8);
  MachineBasicBlock::iterator I = MIB.getInstr();

  BuildMI(MBB, I, DL, TII.get(X86::MOV64rm), Reg).addReg(X86::RIP).addImm(1)
      .addReg(0).addGlobalAddress(GV, 0, X86II::MO_GOTPCREL).addReg(0)
      .addMemOperand(MMO);
  MIB->setDebugLoc(DL);
  MIB->setDesc(TII.get(X86::MOV64rm));
  MIB.addReg(Reg, RegState::Kill).addImm(1).addReg(0).addImm(0).addReg(0);
}

static bool expandXorFP(MachineInstrBuilder &MIB, const TargetInstrInfo &TII) {
  MachineBasicBlock &MBB = *MIB->getParent();
  MachineFunction &MF = *MBB.getParent();
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  const X86RegisterInfo *TRI = Subtarget.getRegisterInfo();
  unsigned XorOp =
      MIB->getOpcode() == X86::XOR64_FP ? X86::XOR64rr : X86::XOR32rr;
  MIB->setDesc(TII.get(XorOp));
  MIB.addReg(TRI->getFrameRegister(MF), RegState::Undef);
  return true;
}

// This is used to handle spills for 128/256-bit registers when we have AVX512,
// but not VLX. If it uses an extended register we need to use an instruction
// that loads the lower 128/256-bit, but is available with only AVX512F.
static bool expandNOVLXLoad(MachineInstrBuilder &MIB,
                            const TargetRegisterInfo *TRI,
                            const MCInstrDesc &LoadDesc,
                            const MCInstrDesc &BroadcastDesc,
                            unsigned SubIdx) {
  unsigned DestReg = MIB->getOperand(0).getReg();
  // Check if DestReg is XMM16-31 or YMM16-31.
  if (TRI->getEncodingValue(DestReg) < 16) {
    // We can use a normal VEX encoded load.
    MIB->setDesc(LoadDesc);
  } else {
    // Use a 128/256-bit VBROADCAST instruction.
    MIB->setDesc(BroadcastDesc);
    // Change the destination to a 512-bit register.
    DestReg = TRI->getMatchingSuperReg(DestReg, SubIdx, &X86::VR512RegClass);
    MIB->getOperand(0).setReg(DestReg);
  }
  return true;
}

// This is used to handle spills for 128/256-bit registers when we have AVX512,
// but not VLX. If it uses an extended register we need to use an instruction
// that stores the lower 128/256-bit, but is available with only AVX512F.
static bool expandNOVLXStore(MachineInstrBuilder &MIB,
                             const TargetRegisterInfo *TRI,
                             const MCInstrDesc &StoreDesc,
                             const MCInstrDesc &ExtractDesc,
                             unsigned SubIdx) {
  unsigned SrcReg = MIB->getOperand(X86::AddrNumOperands).getReg();
  // Check if DestReg is XMM16-31 or YMM16-31.
  if (TRI->getEncodingValue(SrcReg) < 16) {
    // We can use a normal VEX encoded store.
    MIB->setDesc(StoreDesc);
  } else {
    // Use a VEXTRACTF instruction.
    MIB->setDesc(ExtractDesc);
    // Change the destination to a 512-bit register.
    SrcReg = TRI->getMatchingSuperReg(SrcReg, SubIdx, &X86::VR512RegClass);
    MIB->getOperand(X86::AddrNumOperands).setReg(SrcReg);
    MIB.addImm(0x0); // Append immediate to extract from the lower bits.
  }

  return true;
}
bool X86InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  bool HasAVX = Subtarget.hasAVX();
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  switch (MI.getOpcode()) {
  case X86::MOV32r0:
    return Expand2AddrUndef(MIB, get(X86::XOR32rr));
  case X86::MOV32r1:
    return expandMOV32r1(MIB, *this, /*MinusOne=*/ false);
  case X86::MOV32r_1:
    return expandMOV32r1(MIB, *this, /*MinusOne=*/ true);
  case X86::MOV32ImmSExti8:
  case X86::MOV64ImmSExti8:
    return ExpandMOVImmSExti8(MIB, *this, Subtarget);
  case X86::SETB_C8r:
    return Expand2AddrUndef(MIB, get(X86::SBB8rr));
  case X86::SETB_C16r:
    return Expand2AddrUndef(MIB, get(X86::SBB16rr));
  case X86::SETB_C32r:
    return Expand2AddrUndef(MIB, get(X86::SBB32rr));
  case X86::SETB_C64r:
    return Expand2AddrUndef(MIB, get(X86::SBB64rr));
  case X86::MMX_SET0:
    return Expand2AddrUndef(MIB, get(X86::MMX_PXORirr));
  case X86::V_SET0:
  case X86::FsFLD0SS:
  case X86::FsFLD0SD:
    return Expand2AddrUndef(MIB, get(HasAVX ? X86::VXORPSrr : X86::XORPSrr));
  case X86::AVX_SET0: {
    assert(HasAVX && "AVX not supported");
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    unsigned SrcReg = MIB->getOperand(0).getReg();
    unsigned XReg = TRI->getSubReg(SrcReg, X86::sub_xmm);
    MIB->getOperand(0).setReg(XReg);
    Expand2AddrUndef(MIB, get(X86::VXORPSrr));
    MIB.addReg(SrcReg, RegState::ImplicitDefine);
    return true;
  }
  case X86::AVX512_128_SET0:
  case X86::AVX512_FsFLD0SS:
  case X86::AVX512_FsFLD0SD: {
    bool HasVLX = Subtarget.hasVLX();
    unsigned SrcReg = MIB->getOperand(0).getReg();
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    if (HasVLX || TRI->getEncodingValue(SrcReg) < 16)
      return Expand2AddrUndef(MIB,
                              get(HasVLX ? X86::VPXORDZ128rr : X86::VXORPSrr));
    // Extended register without VLX. Use a larger XOR.
    SrcReg =
        TRI->getMatchingSuperReg(SrcReg, X86::sub_xmm, &X86::VR512RegClass);
    MIB->getOperand(0).setReg(SrcReg);
    return Expand2AddrUndef(MIB, get(X86::VPXORDZrr));
  }
  case X86::AVX512_256_SET0:
  case X86::AVX512_512_SET0: {
    bool HasVLX = Subtarget.hasVLX();
    unsigned SrcReg = MIB->getOperand(0).getReg();
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    if (HasVLX || TRI->getEncodingValue(SrcReg) < 16) {
      unsigned XReg = TRI->getSubReg(SrcReg, X86::sub_xmm);
      MIB->getOperand(0).setReg(XReg);
      Expand2AddrUndef(MIB,
                       get(HasVLX ? X86::VPXORDZ128rr : X86::VXORPSrr));
      MIB.addReg(SrcReg, RegState::ImplicitDefine);
      return true;
    }
    return Expand2AddrUndef(MIB, get(X86::VPXORDZrr));
  }
  case X86::V_SETALLONES:
    return Expand2AddrUndef(MIB, get(HasAVX ? X86::VPCMPEQDrr : X86::PCMPEQDrr));
  case X86::AVX2_SETALLONES:
    return Expand2AddrUndef(MIB, get(X86::VPCMPEQDYrr));
  case X86::AVX1_SETALLONES: {
    unsigned Reg = MIB->getOperand(0).getReg();
    // VCMPPSYrri with an immediate 0xf should produce VCMPTRUEPS.
    MIB->setDesc(get(X86::VCMPPSYrri));
    MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef).addImm(0xf);
    return true;
  }
  case X86::AVX512_512_SETALLONES: {
    unsigned Reg = MIB->getOperand(0).getReg();
    MIB->setDesc(get(X86::VPTERNLOGDZrri));
    // VPTERNLOGD needs 3 register inputs and an immediate.
    // 0xff will return 1s for any input.
    MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef)
       .addReg(Reg, RegState::Undef).addImm(0xff);
    return true;
  }
  case X86::AVX512_512_SEXT_MASK_32:
  case X86::AVX512_512_SEXT_MASK_64: {
    unsigned Reg = MIB->getOperand(0).getReg();
    unsigned MaskReg = MIB->getOperand(1).getReg();
    unsigned MaskState = getRegState(MIB->getOperand(1));
    unsigned Opc = (MI.getOpcode() == X86::AVX512_512_SEXT_MASK_64) ?
                   X86::VPTERNLOGQZrrikz : X86::VPTERNLOGDZrrikz;
    MI.RemoveOperand(1);
    MIB->setDesc(get(Opc));
    // VPTERNLOG needs 3 register inputs and an immediate.
    // 0xff will return 1s for any input.
    MIB.addReg(Reg, RegState::Undef).addReg(MaskReg, MaskState)
       .addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef).addImm(0xff);
    return true;
  }
  case X86::VMOVAPSZ128rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVAPSrm),
                           get(X86::VBROADCASTF32X4rm), X86::sub_xmm);
  case X86::VMOVUPSZ128rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVUPSrm),
                           get(X86::VBROADCASTF32X4rm), X86::sub_xmm);
  case X86::VMOVAPSZ256rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVAPSYrm),
                           get(X86::VBROADCASTF64X4rm), X86::sub_ymm);
  case X86::VMOVUPSZ256rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVUPSYrm),
                           get(X86::VBROADCASTF64X4rm), X86::sub_ymm);
  case X86::VMOVAPSZ128mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVAPSmr),
                            get(X86::VEXTRACTF32x4Zmr), X86::sub_xmm);
  case X86::VMOVUPSZ128mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVUPSmr),
                            get(X86::VEXTRACTF32x4Zmr), X86::sub_xmm);
  case X86::VMOVAPSZ256mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVAPSYmr),
                            get(X86::VEXTRACTF64x4Zmr), X86::sub_ymm);
  case X86::VMOVUPSZ256mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVUPSYmr),
                            get(X86::VEXTRACTF64x4Zmr), X86::sub_ymm);
  case X86::MOV32ri64: {
    unsigned Reg = MIB->getOperand(0).getReg();
    unsigned Reg32 = RI.getSubReg(Reg, X86::sub_32bit);
    MI.setDesc(get(X86::MOV32ri));
    MIB->getOperand(0).setReg(Reg32);
    MIB.addReg(Reg, RegState::ImplicitDefine);
    return true;
  }

  // KNL does not recognize dependency-breaking idioms for mask registers,
  // so kxnor %k1, %k1, %k2 has a RAW dependence on %k1.
  // Using %k0 as the undef input register is a performance heuristic based
  // on the assumption that %k0 is used less frequently than the other mask
  // registers, since it is not usable as a write mask.
  // FIXME: A more advanced approach would be to choose the best input mask
  // register based on context.
  case X86::KSET0W: return Expand2AddrKreg(MIB, get(X86::KXORWrr), X86::K0);
  case X86::KSET0D: return Expand2AddrKreg(MIB, get(X86::KXORDrr), X86::K0);
  case X86::KSET0Q: return Expand2AddrKreg(MIB, get(X86::KXORQrr), X86::K0);
  case X86::KSET1W: return Expand2AddrKreg(MIB, get(X86::KXNORWrr), X86::K0);
  case X86::KSET1D: return Expand2AddrKreg(MIB, get(X86::KXNORDrr), X86::K0);
  case X86::KSET1Q: return Expand2AddrKreg(MIB, get(X86::KXNORQrr), X86::K0);
  case TargetOpcode::LOAD_STACK_GUARD:
    expandLoadStackGuard(MIB, *this);
    return true;
  case X86::XOR64_FP:
  case X86::XOR32_FP:
    return expandXorFP(MIB, *this);
  }
  return false;
}

/// Return true for all instructions that only update
/// the first 32 or 64-bits of the destination register and leave the rest
/// unmodified. This can be used to avoid folding loads if the instructions
/// only update part of the destination register, and the non-updated part is
/// not needed. e.g. cvtss2sd, sqrtss. Unfolding the load from these
/// instructions breaks the partial register dependency and it can improve
/// performance. e.g.:
///
///   movss (%rdi), %xmm0
///   cvtss2sd %xmm0, %xmm0
///
/// Instead of
///   cvtss2sd (%rdi), %xmm0
///
/// FIXME: This should be turned into a TSFlags.
///
static bool hasPartialRegUpdate(unsigned Opcode,
                                const X86Subtarget &Subtarget) {
  switch (Opcode) {
  case X86::CVTSI2SSrr:
  case X86::CVTSI2SSrm:
  case X86::CVTSI642SSrr:
  case X86::CVTSI642SSrm:
  case X86::CVTSI2SDrr:
  case X86::CVTSI2SDrm:
  case X86::CVTSI642SDrr:
  case X86::CVTSI642SDrm:
  case X86::CVTSD2SSrr:
  case X86::CVTSD2SSrm:
  case X86::CVTSS2SDrr:
  case X86::CVTSS2SDrm:
  case X86::MOVHPDrm:
  case X86::MOVHPSrm:
  case X86::MOVLPDrm:
  case X86::MOVLPSrm:
  case X86::RCPSSr:
  case X86::RCPSSm:
  case X86::RCPSSr_Int:
  case X86::RCPSSm_Int:
  case X86::ROUNDSDr:
  case X86::ROUNDSDm:
  case X86::ROUNDSSr:
  case X86::ROUNDSSm:
  case X86::RSQRTSSr:
  case X86::RSQRTSSm:
  case X86::RSQRTSSr_Int:
  case X86::RSQRTSSm_Int:
  case X86::SQRTSSr:
  case X86::SQRTSSm:
  case X86::SQRTSSr_Int:
  case X86::SQRTSSm_Int:
  case X86::SQRTSDr:
  case X86::SQRTSDm:
  case X86::SQRTSDr_Int:
  case X86::SQRTSDm_Int:
    return true;
  // GPR
  case X86::POPCNT32rm:
  case X86::POPCNT32rr:
  case X86::POPCNT64rm:
  case X86::POPCNT64rr:
    return Subtarget.hasPOPCNTFalseDeps();
  case X86::LZCNT32rm:
  case X86::LZCNT32rr:
  case X86::LZCNT64rm:
  case X86::LZCNT64rr:
  case X86::TZCNT32rm:
  case X86::TZCNT32rr:
  case X86::TZCNT64rm:
  case X86::TZCNT64rr:
    return Subtarget.hasLZCNTFalseDeps();
  }

  return false;
}

/// Inform the BreakFalseDeps pass how many idle
/// instructions we would like before a partial register update.
unsigned X86InstrInfo::getPartialRegUpdateClearance(
    const MachineInstr &MI, unsigned OpNum,
    const TargetRegisterInfo *TRI) const {
  if (OpNum != 0 || !hasPartialRegUpdate(MI.getOpcode(), Subtarget))
    return 0;

  // If MI is marked as reading Reg, the partial register update is wanted.
  const MachineOperand &MO = MI.getOperand(0);
  unsigned Reg = MO.getReg();
  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    if (MO.readsReg() || MI.readsVirtualRegister(Reg))
      return 0;
  } else {
    if (MI.readsRegister(Reg, TRI))
      return 0;
  }

  // If any instructions in the clearance range are reading Reg, insert a
  // dependency breaking instruction, which is inexpensive and is likely to
  // be hidden in other instruction's cycles.
  return PartialRegUpdateClearance;
}

// Return true for any instruction the copies the high bits of the first source
// operand into the unused high bits of the destination operand.
static bool hasUndefRegUpdate(unsigned Opcode) {
  switch (Opcode) {
  case X86::VCVTSI2SSrr:
  case X86::VCVTSI2SSrm:
  case X86::VCVTSI2SSrr_Int:
  case X86::VCVTSI2SSrm_Int:
  case X86::VCVTSI642SSrr:
  case X86::VCVTSI642SSrm:
  case X86::VCVTSI642SSrr_Int:
  case X86::VCVTSI642SSrm_Int:
  case X86::VCVTSI2SDrr:
  case X86::VCVTSI2SDrm:
  case X86::VCVTSI2SDrr_Int:
  case X86::VCVTSI2SDrm_Int:
  case X86::VCVTSI642SDrr:
  case X86::VCVTSI642SDrm:
  case X86::VCVTSI642SDrr_Int:
  case X86::VCVTSI642SDrm_Int:
  case X86::VCVTSD2SSrr:
  case X86::VCVTSD2SSrm:
  case X86::VCVTSD2SSrr_Int:
  case X86::VCVTSD2SSrm_Int:
  case X86::VCVTSS2SDrr:
  case X86::VCVTSS2SDrm:
  case X86::VCVTSS2SDrr_Int:
  case X86::VCVTSS2SDrm_Int:
  case X86::VRCPSSr:
  case X86::VRCPSSr_Int:
  case X86::VRCPSSm:
  case X86::VRCPSSm_Int:
  case X86::VROUNDSDr:
  case X86::VROUNDSDm:
  case X86::VROUNDSDr_Int:
  case X86::VROUNDSDm_Int:
  case X86::VROUNDSSr:
  case X86::VROUNDSSm:
  case X86::VROUNDSSr_Int:
  case X86::VROUNDSSm_Int:
  case X86::VRSQRTSSr:
  case X86::VRSQRTSSr_Int:
  case X86::VRSQRTSSm:
  case X86::VRSQRTSSm_Int:
  case X86::VSQRTSSr:
  case X86::VSQRTSSr_Int:
  case X86::VSQRTSSm:
  case X86::VSQRTSSm_Int:
  case X86::VSQRTSDr:
  case X86::VSQRTSDr_Int:
  case X86::VSQRTSDm:
  case X86::VSQRTSDm_Int:
  // AVX-512
  case X86::VCVTSI2SSZrr:
  case X86::VCVTSI2SSZrm:
  case X86::VCVTSI2SSZrr_Int:
  case X86::VCVTSI2SSZrrb_Int:
  case X86::VCVTSI2SSZrm_Int:
  case X86::VCVTSI642SSZrr:
  case X86::VCVTSI642SSZrm:
  case X86::VCVTSI642SSZrr_Int:
  case X86::VCVTSI642SSZrrb_Int:
  case X86::VCVTSI642SSZrm_Int:
  case X86::VCVTSI2SDZrr:
  case X86::VCVTSI2SDZrm:
  case X86::VCVTSI2SDZrr_Int:
  case X86::VCVTSI2SDZrrb_Int:
  case X86::VCVTSI2SDZrm_Int:
  case X86::VCVTSI642SDZrr:
  case X86::VCVTSI642SDZrm:
  case X86::VCVTSI642SDZrr_Int:
  case X86::VCVTSI642SDZrrb_Int:
  case X86::VCVTSI642SDZrm_Int:
  case X86::VCVTUSI2SSZrr:
  case X86::VCVTUSI2SSZrm:
  case X86::VCVTUSI2SSZrr_Int:
  case X86::VCVTUSI2SSZrrb_Int:
  case X86::VCVTUSI2SSZrm_Int:
  case X86::VCVTUSI642SSZrr:
  case X86::VCVTUSI642SSZrm:
  case X86::VCVTUSI642SSZrr_Int:
  case X86::VCVTUSI642SSZrrb_Int:
  case X86::VCVTUSI642SSZrm_Int:
  case X86::VCVTUSI2SDZrr:
  case X86::VCVTUSI2SDZrm:
  case X86::VCVTUSI2SDZrr_Int:
  case X86::VCVTUSI2SDZrm_Int:
  case X86::VCVTUSI642SDZrr:
  case X86::VCVTUSI642SDZrm:
  case X86::VCVTUSI642SDZrr_Int:
  case X86::VCVTUSI642SDZrrb_Int:
  case X86::VCVTUSI642SDZrm_Int:
  case X86::VCVTSD2SSZrr:
  case X86::VCVTSD2SSZrr_Int:
  case X86::VCVTSD2SSZrrb_Int:
  case X86::VCVTSD2SSZrm:
  case X86::VCVTSD2SSZrm_Int:
  case X86::VCVTSS2SDZrr:
  case X86::VCVTSS2SDZrr_Int:
  case X86::VCVTSS2SDZrrb_Int:
  case X86::VCVTSS2SDZrm:
  case X86::VCVTSS2SDZrm_Int:
  case X86::VGETEXPSDZr:
  case X86::VGETEXPSDZrb:
  case X86::VGETEXPSDZm:
  case X86::VGETEXPSSZr:
  case X86::VGETEXPSSZrb:
  case X86::VGETEXPSSZm:
  case X86::VGETMANTSDZrri:
  case X86::VGETMANTSDZrrib:
  case X86::VGETMANTSDZrmi:
  case X86::VGETMANTSSZrri:
  case X86::VGETMANTSSZrrib:
  case X86::VGETMANTSSZrmi:
  case X86::VRNDSCALESDZr:
  case X86::VRNDSCALESDZr_Int:
  case X86::VRNDSCALESDZrb_Int:
  case X86::VRNDSCALESDZm:
  case X86::VRNDSCALESDZm_Int:
  case X86::VRNDSCALESSZr:
  case X86::VRNDSCALESSZr_Int:
  case X86::VRNDSCALESSZrb_Int:
  case X86::VRNDSCALESSZm:
  case X86::VRNDSCALESSZm_Int:
  case X86::VRCP14SDZrr:
  case X86::VRCP14SDZrm:
  case X86::VRCP14SSZrr:
  case X86::VRCP14SSZrm:
  case X86::VRCP28SDZr:
  case X86::VRCP28SDZrb:
  case X86::VRCP28SDZm:
  case X86::VRCP28SSZr:
  case X86::VRCP28SSZrb:
  case X86::VRCP28SSZm:
  case X86::VREDUCESSZrmi:
  case X86::VREDUCESSZrri:
  case X86::VREDUCESSZrrib:
  case X86::VRSQRT14SDZrr:
  case X86::VRSQRT14SDZrm:
  case X86::VRSQRT14SSZrr:
  case X86::VRSQRT14SSZrm:
  case X86::VRSQRT28SDZr:
  case X86::VRSQRT28SDZrb:
  case X86::VRSQRT28SDZm:
  case X86::VRSQRT28SSZr:
  case X86::VRSQRT28SSZrb:
  case X86::VRSQRT28SSZm:
  case X86::VSQRTSSZr:
  case X86::VSQRTSSZr_Int:
  case X86::VSQRTSSZrb_Int:
  case X86::VSQRTSSZm:
  case X86::VSQRTSSZm_Int:
  case X86::VSQRTSDZr:
  case X86::VSQRTSDZr_Int:
  case X86::VSQRTSDZrb_Int:
  case X86::VSQRTSDZm:
  case X86::VSQRTSDZm_Int:
    return true;
  }

  return false;
}

/// Inform the BreakFalseDeps pass how many idle instructions we would like
/// before certain undef register reads.
///
/// This catches the VCVTSI2SD family of instructions:
///
/// vcvtsi2sdq %rax, undef %xmm0, %xmm14
///
/// We should to be careful *not* to catch VXOR idioms which are presumably
/// handled specially in the pipeline:
///
/// vxorps undef %xmm1, undef %xmm1, %xmm1
///
/// Like getPartialRegUpdateClearance, this makes a strong assumption that the
/// high bits that are passed-through are not live.
unsigned
X86InstrInfo::getUndefRegClearance(const MachineInstr &MI, unsigned &OpNum,
                                   const TargetRegisterInfo *TRI) const {
  if (!hasUndefRegUpdate(MI.getOpcode()))
    return 0;

  // Set the OpNum parameter to the first source operand.
  OpNum = 1;

  const MachineOperand &MO = MI.getOperand(OpNum);
  if (MO.isUndef() && TargetRegisterInfo::isPhysicalRegister(MO.getReg())) {
    return UndefRegClearance;
  }
  return 0;
}

void X86InstrInfo::breakPartialRegDependency(
    MachineInstr &MI, unsigned OpNum, const TargetRegisterInfo *TRI) const {
  unsigned Reg = MI.getOperand(OpNum).getReg();
  // If MI kills this register, the false dependence is already broken.
  if (MI.killsRegister(Reg, TRI))
    return;

  if (X86::VR128RegClass.contains(Reg)) {
    // These instructions are all floating point domain, so xorps is the best
    // choice.
    unsigned Opc = Subtarget.hasAVX() ? X86::VXORPSrr : X86::XORPSrr;
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(Opc), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::VR256RegClass.contains(Reg)) {
    // Use vxorps to clear the full ymm register.
    // It wants to read and write the xmm sub-register.
    unsigned XReg = TRI->getSubReg(Reg, X86::sub_xmm);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::VXORPSrr), XReg)
        .addReg(XReg, RegState::Undef)
        .addReg(XReg, RegState::Undef)
        .addReg(Reg, RegState::ImplicitDefine);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::GR64RegClass.contains(Reg)) {
    // Using XOR32rr because it has shorter encoding and zeros up the upper bits
    // as well.
    unsigned XReg = TRI->getSubReg(Reg, X86::sub_32bit);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::XOR32rr), XReg)
        .addReg(XReg, RegState::Undef)
        .addReg(XReg, RegState::Undef)
        .addReg(Reg, RegState::ImplicitDefine);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::GR32RegClass.contains(Reg)) {
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::XOR32rr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
    MI.addRegisterKilled(Reg, TRI, true);
  }
}

static void addOperands(MachineInstrBuilder &MIB, ArrayRef<MachineOperand> MOs,
                        int PtrOffset = 0) {
  unsigned NumAddrOps = MOs.size();

  if (NumAddrOps < 4) {
    // FrameIndex only - add an immediate offset (whether its zero or not).
    for (unsigned i = 0; i != NumAddrOps; ++i)
      MIB.add(MOs[i]);
    addOffset(MIB, PtrOffset);
  } else {
    // General Memory Addressing - we need to add any offset to an existing
    // offset.
    assert(MOs.size() == 5 && "Unexpected memory operand list length");
    for (unsigned i = 0; i != NumAddrOps; ++i) {
      const MachineOperand &MO = MOs[i];
      if (i == 3 && PtrOffset != 0) {
        MIB.addDisp(MO, PtrOffset);
      } else {
        MIB.add(MO);
      }
    }
  }
}

static void updateOperandRegConstraints(MachineFunction &MF,
                                        MachineInstr &NewMI,
                                        const TargetInstrInfo &TII) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();

  for (int Idx : llvm::seq<int>(0, NewMI.getNumOperands())) {
    MachineOperand &MO = NewMI.getOperand(Idx);
    // We only need to update constraints on virtual register operands.
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!TRI.isVirtualRegister(Reg))
      continue;

    auto *NewRC = MRI.constrainRegClass(
        Reg, TII.getRegClass(NewMI.getDesc(), Idx, &TRI, MF));
    if (!NewRC) {
      LLVM_DEBUG(
          dbgs() << "WARNING: Unable to update register constraint for operand "
                 << Idx << " of instruction:\n";
          NewMI.dump(); dbgs() << "\n");
    }
  }
}

static MachineInstr *FuseTwoAddrInst(MachineFunction &MF, unsigned Opcode,
                                     ArrayRef<MachineOperand> MOs,
                                     MachineBasicBlock::iterator InsertPt,
                                     MachineInstr &MI,
                                     const TargetInstrInfo &TII) {
  // Create the base instruction with the memory operand as the first part.
  // Omit the implicit operands, something BuildMI can't do.
  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(Opcode), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);
  addOperands(MIB, MOs);

  // Loop over the rest of the ri operands, converting them over.
  unsigned NumOps = MI.getDesc().getNumOperands() - 2;
  for (unsigned i = 0; i != NumOps; ++i) {
    MachineOperand &MO = MI.getOperand(i + 2);
    MIB.add(MO);
  }
  for (unsigned i = NumOps + 2, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    MIB.add(MO);
  }

  updateOperandRegConstraints(MF, *NewMI, TII);

  MachineBasicBlock *MBB = InsertPt->getParent();
  MBB->insert(InsertPt, NewMI);

  return MIB;
}

static MachineInstr *FuseInst(MachineFunction &MF, unsigned Opcode,
                              unsigned OpNo, ArrayRef<MachineOperand> MOs,
                              MachineBasicBlock::iterator InsertPt,
                              MachineInstr &MI, const TargetInstrInfo &TII,
                              int PtrOffset = 0) {
  // Omit the implicit operands, something BuildMI can't do.
  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(Opcode), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);

  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (i == OpNo) {
      assert(MO.isReg() && "Expected to fold into reg operand!");
      addOperands(MIB, MOs, PtrOffset);
    } else {
      MIB.add(MO);
    }
  }

  updateOperandRegConstraints(MF, *NewMI, TII);

  MachineBasicBlock *MBB = InsertPt->getParent();
  MBB->insert(InsertPt, NewMI);

  return MIB;
}

static MachineInstr *MakeM0Inst(const TargetInstrInfo &TII, unsigned Opcode,
                                ArrayRef<MachineOperand> MOs,
                                MachineBasicBlock::iterator InsertPt,
                                MachineInstr &MI) {
  MachineInstrBuilder MIB = BuildMI(*InsertPt->getParent(), InsertPt,
                                    MI.getDebugLoc(), TII.get(Opcode));
  addOperands(MIB, MOs);
  return MIB.addImm(0);
}

MachineInstr *X86InstrInfo::foldMemoryOperandCustom(
    MachineFunction &MF, MachineInstr &MI, unsigned OpNum,
    ArrayRef<MachineOperand> MOs, MachineBasicBlock::iterator InsertPt,
    unsigned Size, unsigned Align) const {
  switch (MI.getOpcode()) {
  case X86::INSERTPSrr:
  case X86::VINSERTPSrr:
  case X86::VINSERTPSZrr:
    // Attempt to convert the load of inserted vector into a fold load
    // of a single float.
    if (OpNum == 2) {
      unsigned Imm = MI.getOperand(MI.getNumOperands() - 1).getImm();
      unsigned ZMask = Imm & 15;
      unsigned DstIdx = (Imm >> 4) & 3;
      unsigned SrcIdx = (Imm >> 6) & 3;

      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if (Size <= RCSize && 4 <= Align) {
        int PtrOffset = SrcIdx * 4;
        unsigned NewImm = (DstIdx << 4) | ZMask;
        unsigned NewOpCode =
            (MI.getOpcode() == X86::VINSERTPSZrr) ? X86::VINSERTPSZrm :
            (MI.getOpcode() == X86::VINSERTPSrr)  ? X86::VINSERTPSrm  :
                                                    X86::INSERTPSrm;
        MachineInstr *NewMI =
            FuseInst(MF, NewOpCode, OpNum, MOs, InsertPt, MI, *this, PtrOffset);
        NewMI->getOperand(NewMI->getNumOperands() - 1).setImm(NewImm);
        return NewMI;
      }
    }
    break;
  case X86::MOVHLPSrr:
  case X86::VMOVHLPSrr:
  case X86::VMOVHLPSZrr:
    // Move the upper 64-bits of the second operand to the lower 64-bits.
    // To fold the load, adjust the pointer to the upper and use (V)MOVLPS.
    // TODO: In most cases AVX doesn't have a 8-byte alignment requirement.
    if (OpNum == 2) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if (Size <= RCSize && 8 <= Align) {
        unsigned NewOpCode =
            (MI.getOpcode() == X86::VMOVHLPSZrr) ? X86::VMOVLPSZ128rm :
            (MI.getOpcode() == X86::VMOVHLPSrr)  ? X86::VMOVLPSrm     :
                                                   X86::MOVLPSrm;
        MachineInstr *NewMI =
            FuseInst(MF, NewOpCode, OpNum, MOs, InsertPt, MI, *this, 8);
        return NewMI;
      }
    }
    break;
  };

  return nullptr;
}

static bool shouldPreventUndefRegUpdateMemFold(MachineFunction &MF, MachineInstr &MI) {
  if (MF.getFunction().optForSize() || !hasUndefRegUpdate(MI.getOpcode()) ||
      !MI.getOperand(1).isReg())
    return false;

  // The are two cases we need to handle depending on where in the pipeline
  // the folding attempt is being made.
  // -Register has the undef flag set.
  // -Register is produced by the IMPLICIT_DEF instruction.

  if (MI.getOperand(1).isUndef())
    return true;

  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  MachineInstr *VRegDef = RegInfo.getUniqueVRegDef(MI.getOperand(1).getReg());
  return VRegDef && VRegDef->isImplicitDef();
}


MachineInstr *X86InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, unsigned OpNum,
    ArrayRef<MachineOperand> MOs, MachineBasicBlock::iterator InsertPt,
    unsigned Size, unsigned Align, bool AllowCommute) const {
  bool isSlowTwoMemOps = Subtarget.slowTwoMemOps();
  bool isTwoAddrFold = false;

  // For CPUs that favor the register form of a call or push,
  // do not fold loads into calls or pushes, unless optimizing for size
  // aggressively.
  if (isSlowTwoMemOps && !MF.getFunction().optForMinSize() &&
      (MI.getOpcode() == X86::CALL32r || MI.getOpcode() == X86::CALL64r ||
       MI.getOpcode() == X86::PUSH16r || MI.getOpcode() == X86::PUSH32r ||
       MI.getOpcode() == X86::PUSH64r))
    return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().optForSize() &&
      (hasPartialRegUpdate(MI.getOpcode(), Subtarget) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  unsigned NumOps = MI.getDesc().getNumOperands();
  bool isTwoAddr =
      NumOps > 1 && MI.getDesc().getOperandConstraint(1, MCOI::TIED_TO) != -1;

  // FIXME: AsmPrinter doesn't know how to handle
  // X86II::MO_GOT_ABSOLUTE_ADDRESS after folding.
  if (MI.getOpcode() == X86::ADD32ri &&
      MI.getOperand(2).getTargetFlags() == X86II::MO_GOT_ABSOLUTE_ADDRESS)
    return nullptr;

  // GOTTPOFF relocation loads can only be folded into add instructions.
  // FIXME: Need to exclude other relocations that only support specific
  // instructions.
  if (MOs.size() == X86::AddrNumOperands &&
      MOs[X86::AddrDisp].getTargetFlags() == X86II::MO_GOTTPOFF &&
      MI.getOpcode() != X86::ADD64rr)
    return nullptr;

  MachineInstr *NewMI = nullptr;

  // Attempt to fold any custom cases we have.
  if (MachineInstr *CustomMI =
          foldMemoryOperandCustom(MF, MI, OpNum, MOs, InsertPt, Size, Align))
    return CustomMI;

  const X86MemoryFoldTableEntry *I = nullptr;

  // Folding a memory location into the two-address part of a two-address
  // instruction is different than folding it other places.  It requires
  // replacing the *two* registers with the memory location.
  if (isTwoAddr && NumOps >= 2 && OpNum < 2 && MI.getOperand(0).isReg() &&
      MI.getOperand(1).isReg() &&
      MI.getOperand(0).getReg() == MI.getOperand(1).getReg()) {
    I = lookupTwoAddrFoldTable(MI.getOpcode());
    isTwoAddrFold = true;
  } else {
    if (OpNum == 0) {
      if (MI.getOpcode() == X86::MOV32r0) {
        NewMI = MakeM0Inst(*this, X86::MOV32mi, MOs, InsertPt, MI);
        if (NewMI)
          return NewMI;
      }
    }

    I = lookupFoldTable(MI.getOpcode(), OpNum);
  }

  if (I != nullptr) {
    unsigned Opcode = I->DstOp;
    unsigned MinAlign = (I->Flags & TB_ALIGN_MASK) >> TB_ALIGN_SHIFT;
    if (Align < MinAlign)
      return nullptr;
    bool NarrowToMOV32rm = false;
    if (Size) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum,
                                                  &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if (Size < RCSize) {
        // Check if it's safe to fold the load. If the size of the object is
        // narrower than the load width, then it's not.
        if (Opcode != X86::MOV64rm || RCSize != 8 || Size != 4)
          return nullptr;
        // If this is a 64-bit load, but the spill slot is 32, then we can do
        // a 32-bit load which is implicitly zero-extended. This likely is
        // due to live interval analysis remat'ing a load from stack slot.
        if (MI.getOperand(0).getSubReg() || MI.getOperand(1).getSubReg())
          return nullptr;
        Opcode = X86::MOV32rm;
        NarrowToMOV32rm = true;
      }
    }

    if (isTwoAddrFold)
      NewMI = FuseTwoAddrInst(MF, Opcode, MOs, InsertPt, MI, *this);
    else
      NewMI = FuseInst(MF, Opcode, OpNum, MOs, InsertPt, MI, *this);

    if (NarrowToMOV32rm) {
      // If this is the special case where we use a MOV32rm to load a 32-bit
      // value and zero-extend the top bits. Change the destination register
      // to a 32-bit one.
      unsigned DstReg = NewMI->getOperand(0).getReg();
      if (TargetRegisterInfo::isPhysicalRegister(DstReg))
        NewMI->getOperand(0).setReg(RI.getSubReg(DstReg, X86::sub_32bit));
      else
        NewMI->getOperand(0).setSubReg(X86::sub_32bit);
    }
    return NewMI;
  }

  // If the instruction and target operand are commutable, commute the
  // instruction and try again.
  if (AllowCommute) {
    unsigned CommuteOpIdx1 = OpNum, CommuteOpIdx2 = CommuteAnyOperandIndex;
    if (findCommutedOpIndices(MI, CommuteOpIdx1, CommuteOpIdx2)) {
      bool HasDef = MI.getDesc().getNumDefs();
      unsigned Reg0 = HasDef ? MI.getOperand(0).getReg() : 0;
      unsigned Reg1 = MI.getOperand(CommuteOpIdx1).getReg();
      unsigned Reg2 = MI.getOperand(CommuteOpIdx2).getReg();
      bool Tied1 =
          0 == MI.getDesc().getOperandConstraint(CommuteOpIdx1, MCOI::TIED_TO);
      bool Tied2 =
          0 == MI.getDesc().getOperandConstraint(CommuteOpIdx2, MCOI::TIED_TO);

      // If either of the commutable operands are tied to the destination
      // then we can not commute + fold.
      if ((HasDef && Reg0 == Reg1 && Tied1) ||
          (HasDef && Reg0 == Reg2 && Tied2))
        return nullptr;

      MachineInstr *CommutedMI =
          commuteInstruction(MI, false, CommuteOpIdx1, CommuteOpIdx2);
      if (!CommutedMI) {
        // Unable to commute.
        return nullptr;
      }
      if (CommutedMI != &MI) {
        // New instruction. We can't fold from this.
        CommutedMI->eraseFromParent();
        return nullptr;
      }

      // Attempt to fold with the commuted version of the instruction.
      NewMI = foldMemoryOperandImpl(MF, MI, CommuteOpIdx2, MOs, InsertPt,
                                    Size, Align, /*AllowCommute=*/false);
      if (NewMI)
        return NewMI;

      // Folding failed again - undo the commute before returning.
      MachineInstr *UncommutedMI =
          commuteInstruction(MI, false, CommuteOpIdx1, CommuteOpIdx2);
      if (!UncommutedMI) {
        // Unable to commute.
        return nullptr;
      }
      if (UncommutedMI != &MI) {
        // New instruction. It doesn't need to be kept.
        UncommutedMI->eraseFromParent();
        return nullptr;
      }

      // Return here to prevent duplicate fuse failure report.
      return nullptr;
    }
  }

  // No fusion
  if (PrintFailedFusing && !MI.isCopy())
    dbgs() << "We failed to fuse operand " << OpNum << " in " << MI;
  return nullptr;
}

MachineInstr *
X86InstrInfo::foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                    ArrayRef<unsigned> Ops,
                                    MachineBasicBlock::iterator InsertPt,
                                    int FrameIndex, LiveIntervals *LIS) const {
  // Check switch flag
  if (NoFusing)
    return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().optForSize() &&
      (hasPartialRegUpdate(MI.getOpcode(), Subtarget) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  // Don't fold subreg spills, or reloads that use a high subreg.
  for (auto Op : Ops) {
    MachineOperand &MO = MI.getOperand(Op);
    auto SubReg = MO.getSubReg();
    if (SubReg && (MO.isDef() || SubReg == X86::sub_8bit_hi))
      return nullptr;
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Size = MFI.getObjectSize(FrameIndex);
  unsigned Alignment = MFI.getObjectAlignment(FrameIndex);
  // If the function stack isn't realigned we don't want to fold instructions
  // that need increased alignment.
  if (!RI.needsStackRealignment(MF))
    Alignment =
        std::min(Alignment, Subtarget.getFrameLowering()->getStackAlignment());
  if (Ops.size() == 2 && Ops[0] == 0 && Ops[1] == 1) {
    unsigned NewOpc = 0;
    unsigned RCSize = 0;
    switch (MI.getOpcode()) {
    default: return nullptr;
    case X86::TEST8rr:  NewOpc = X86::CMP8ri; RCSize = 1; break;
    case X86::TEST16rr: NewOpc = X86::CMP16ri8; RCSize = 2; break;
    case X86::TEST32rr: NewOpc = X86::CMP32ri8; RCSize = 4; break;
    case X86::TEST64rr: NewOpc = X86::CMP64ri8; RCSize = 8; break;
    }
    // Check if it's safe to fold the load. If the size of the object is
    // narrower than the load width, then it's not.
    if (Size < RCSize)
      return nullptr;
    // Change to CMPXXri r, 0 first.
    MI.setDesc(get(NewOpc));
    MI.getOperand(1).ChangeToImmediate(0);
  } else if (Ops.size() != 1)
    return nullptr;

  return foldMemoryOperandImpl(MF, MI, Ops[0],
                               MachineOperand::CreateFI(FrameIndex), InsertPt,
                               Size, Alignment, /*AllowCommute=*/true);
}

/// Check if \p LoadMI is a partial register load that we can't fold into \p MI
/// because the latter uses contents that wouldn't be defined in the folded
/// version.  For instance, this transformation isn't legal:
///   movss (%rdi), %xmm0
///   addps %xmm0, %xmm0
/// ->
///   addps (%rdi), %xmm0
///
/// But this one is:
///   movss (%rdi), %xmm0
///   addss %xmm0, %xmm0
/// ->
///   addss (%rdi), %xmm0
///
static bool isNonFoldablePartialRegisterLoad(const MachineInstr &LoadMI,
                                             const MachineInstr &UserMI,
                                             const MachineFunction &MF) {
  unsigned Opc = LoadMI.getOpcode();
  unsigned UserOpc = UserMI.getOpcode();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC =
      MF.getRegInfo().getRegClass(LoadMI.getOperand(0).getReg());
  unsigned RegSize = TRI.getRegSizeInBits(*RC);

  if ((Opc == X86::MOVSSrm || Opc == X86::VMOVSSrm || Opc == X86::VMOVSSZrm) &&
      RegSize > 32) {
    // These instructions only load 32 bits, we can't fold them if the
    // destination register is wider than 32 bits (4 bytes), and its user
    // instruction isn't scalar (SS).
    switch (UserOpc) {
    case X86::ADDSSrr_Int: case X86::VADDSSrr_Int: case X86::VADDSSZrr_Int:
    case X86::CMPSSrr_Int: case X86::VCMPSSrr_Int: case X86::VCMPSSZrr_Int:
    case X86::DIVSSrr_Int: case X86::VDIVSSrr_Int: case X86::VDIVSSZrr_Int:
    case X86::MAXSSrr_Int: case X86::VMAXSSrr_Int: case X86::VMAXSSZrr_Int:
    case X86::MINSSrr_Int: case X86::VMINSSrr_Int: case X86::VMINSSZrr_Int:
    case X86::MULSSrr_Int: case X86::VMULSSrr_Int: case X86::VMULSSZrr_Int:
    case X86::SUBSSrr_Int: case X86::VSUBSSrr_Int: case X86::VSUBSSZrr_Int:
    case X86::VADDSSZrr_Intk: case X86::VADDSSZrr_Intkz:
    case X86::VDIVSSZrr_Intk: case X86::VDIVSSZrr_Intkz:
    case X86::VMAXSSZrr_Intk: case X86::VMAXSSZrr_Intkz:
    case X86::VMINSSZrr_Intk: case X86::VMINSSZrr_Intkz:
    case X86::VMULSSZrr_Intk: case X86::VMULSSZrr_Intkz:
    case X86::VSUBSSZrr_Intk: case X86::VSUBSSZrr_Intkz:
    case X86::VFMADDSS4rr_Int:   case X86::VFNMADDSS4rr_Int:
    case X86::VFMSUBSS4rr_Int:   case X86::VFNMSUBSS4rr_Int:
    case X86::VFMADD132SSr_Int:  case X86::VFNMADD132SSr_Int:
    case X86::VFMADD213SSr_Int:  case X86::VFNMADD213SSr_Int:
    case X86::VFMADD231SSr_Int:  case X86::VFNMADD231SSr_Int:
    case X86::VFMSUB132SSr_Int:  case X86::VFNMSUB132SSr_Int:
    case X86::VFMSUB213SSr_Int:  case X86::VFNMSUB213SSr_Int:
    case X86::VFMSUB231SSr_Int:  case X86::VFNMSUB231SSr_Int:
    case X86::VFMADD132SSZr_Int: case X86::VFNMADD132SSZr_Int:
    case X86::VFMADD213SSZr_Int: case X86::VFNMADD213SSZr_Int:
    case X86::VFMADD231SSZr_Int: case X86::VFNMADD231SSZr_Int:
    case X86::VFMSUB132SSZr_Int: case X86::VFNMSUB132SSZr_Int:
    case X86::VFMSUB213SSZr_Int: case X86::VFNMSUB213SSZr_Int:
    case X86::VFMSUB231SSZr_Int: case X86::VFNMSUB231SSZr_Int:
    case X86::VFMADD132SSZr_Intk: case X86::VFNMADD132SSZr_Intk:
    case X86::VFMADD213SSZr_Intk: case X86::VFNMADD213SSZr_Intk:
    case X86::VFMADD231SSZr_Intk: case X86::VFNMADD231SSZr_Intk:
    case X86::VFMSUB132SSZr_Intk: case X86::VFNMSUB132SSZr_Intk:
    case X86::VFMSUB213SSZr_Intk: case X86::VFNMSUB213SSZr_Intk:
    case X86::VFMSUB231SSZr_Intk: case X86::VFNMSUB231SSZr_Intk:
    case X86::VFMADD132SSZr_Intkz: case X86::VFNMADD132SSZr_Intkz:
    case X86::VFMADD213SSZr_Intkz: case X86::VFNMADD213SSZr_Intkz:
    case X86::VFMADD231SSZr_Intkz: case X86::VFNMADD231SSZr_Intkz:
    case X86::VFMSUB132SSZr_Intkz: case X86::VFNMSUB132SSZr_Intkz:
    case X86::VFMSUB213SSZr_Intkz: case X86::VFNMSUB213SSZr_Intkz:
    case X86::VFMSUB231SSZr_Intkz: case X86::VFNMSUB231SSZr_Intkz:
      return false;
    default:
      return true;
    }
  }

  if ((Opc == X86::MOVSDrm || Opc == X86::VMOVSDrm || Opc == X86::VMOVSDZrm) &&
      RegSize > 64) {
    // These instructions only load 64 bits, we can't fold them if the
    // destination register is wider than 64 bits (8 bytes), and its user
    // instruction isn't scalar (SD).
    switch (UserOpc) {
    case X86::ADDSDrr_Int: case X86::VADDSDrr_Int: case X86::VADDSDZrr_Int:
    case X86::CMPSDrr_Int: case X86::VCMPSDrr_Int: case X86::VCMPSDZrr_Int:
    case X86::DIVSDrr_Int: case X86::VDIVSDrr_Int: case X86::VDIVSDZrr_Int:
    case X86::MAXSDrr_Int: case X86::VMAXSDrr_Int: case X86::VMAXSDZrr_Int:
    case X86::MINSDrr_Int: case X86::VMINSDrr_Int: case X86::VMINSDZrr_Int:
    case X86::MULSDrr_Int: case X86::VMULSDrr_Int: case X86::VMULSDZrr_Int:
    case X86::SUBSDrr_Int: case X86::VSUBSDrr_Int: case X86::VSUBSDZrr_Int:
    case X86::VADDSDZrr_Intk: case X86::VADDSDZrr_Intkz:
    case X86::VDIVSDZrr_Intk: case X86::VDIVSDZrr_Intkz:
    case X86::VMAXSDZrr_Intk: case X86::VMAXSDZrr_Intkz:
    case X86::VMINSDZrr_Intk: case X86::VMINSDZrr_Intkz:
    case X86::VMULSDZrr_Intk: case X86::VMULSDZrr_Intkz:
    case X86::VSUBSDZrr_Intk: case X86::VSUBSDZrr_Intkz:
    case X86::VFMADDSD4rr_Int:   case X86::VFNMADDSD4rr_Int:
    case X86::VFMSUBSD4rr_Int:   case X86::VFNMSUBSD4rr_Int:
    case X86::VFMADD132SDr_Int:  case X86::VFNMADD132SDr_Int:
    case X86::VFMADD213SDr_Int:  case X86::VFNMADD213SDr_Int:
    case X86::VFMADD231SDr_Int:  case X86::VFNMADD231SDr_Int:
    case X86::VFMSUB132SDr_Int:  case X86::VFNMSUB132SDr_Int:
    case X86::VFMSUB213SDr_Int:  case X86::VFNMSUB213SDr_Int:
    case X86::VFMSUB231SDr_Int:  case X86::VFNMSUB231SDr_Int:
    case X86::VFMADD132SDZr_Int: case X86::VFNMADD132SDZr_Int:
    case X86::VFMADD213SDZr_Int: case X86::VFNMADD213SDZr_Int:
    case X86::VFMADD231SDZr_Int: case X86::VFNMADD231SDZr_Int:
    case X86::VFMSUB132SDZr_Int: case X86::VFNMSUB132SDZr_Int:
    case X86::VFMSUB213SDZr_Int: case X86::VFNMSUB213SDZr_Int:
    case X86::VFMSUB231SDZr_Int: case X86::VFNMSUB231SDZr_Int:
    case X86::VFMADD132SDZr_Intk: case X86::VFNMADD132SDZr_Intk:
    case X86::VFMADD213SDZr_Intk: case X86::VFNMADD213SDZr_Intk:
    case X86::VFMADD231SDZr_Intk: case X86::VFNMADD231SDZr_Intk:
    case X86::VFMSUB132SDZr_Intk: case X86::VFNMSUB132SDZr_Intk:
    case X86::VFMSUB213SDZr_Intk: case X86::VFNMSUB213SDZr_Intk:
    case X86::VFMSUB231SDZr_Intk: case X86::VFNMSUB231SDZr_Intk:
    case X86::VFMADD132SDZr_Intkz: case X86::VFNMADD132SDZr_Intkz:
    case X86::VFMADD213SDZr_Intkz: case X86::VFNMADD213SDZr_Intkz:
    case X86::VFMADD231SDZr_Intkz: case X86::VFNMADD231SDZr_Intkz:
    case X86::VFMSUB132SDZr_Intkz: case X86::VFNMSUB132SDZr_Intkz:
    case X86::VFMSUB213SDZr_Intkz: case X86::VFNMSUB213SDZr_Intkz:
    case X86::VFMSUB231SDZr_Intkz: case X86::VFNMSUB231SDZr_Intkz:
      return false;
    default:
      return true;
    }
  }

  return false;
}

MachineInstr *X86InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
    LiveIntervals *LIS) const {

  // TODO: Support the case where LoadMI loads a wide register, but MI
  // only uses a subreg.
  for (auto Op : Ops) {
    if (MI.getOperand(Op).getSubReg())
      return nullptr;
  }

  // If loading from a FrameIndex, fold directly from the FrameIndex.
  unsigned NumOps = LoadMI.getDesc().getNumOperands();
  int FrameIndex;
  if (isLoadFromStackSlot(LoadMI, FrameIndex)) {
    if (isNonFoldablePartialRegisterLoad(LoadMI, MI, MF))
      return nullptr;
    return foldMemoryOperandImpl(MF, MI, Ops, InsertPt, FrameIndex, LIS);
  }

  // Check switch flag
  if (NoFusing) return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().optForSize() &&
      (hasPartialRegUpdate(MI.getOpcode(), Subtarget) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  // Determine the alignment of the load.
  unsigned Alignment = 0;
  if (LoadMI.hasOneMemOperand())
    Alignment = (*LoadMI.memoperands_begin())->getAlignment();
  else
    switch (LoadMI.getOpcode()) {
    case X86::AVX512_512_SET0:
    case X86::AVX512_512_SETALLONES:
      Alignment = 64;
      break;
    case X86::AVX2_SETALLONES:
    case X86::AVX1_SETALLONES:
    case X86::AVX_SET0:
    case X86::AVX512_256_SET0:
      Alignment = 32;
      break;
    case X86::V_SET0:
    case X86::V_SETALLONES:
    case X86::AVX512_128_SET0:
      Alignment = 16;
      break;
    case X86::MMX_SET0:
    case X86::FsFLD0SD:
    case X86::AVX512_FsFLD0SD:
      Alignment = 8;
      break;
    case X86::FsFLD0SS:
    case X86::AVX512_FsFLD0SS:
      Alignment = 4;
      break;
    default:
      return nullptr;
    }
  if (Ops.size() == 2 && Ops[0] == 0 && Ops[1] == 1) {
    unsigned NewOpc = 0;
    switch (MI.getOpcode()) {
    default: return nullptr;
    case X86::TEST8rr:  NewOpc = X86::CMP8ri; break;
    case X86::TEST16rr: NewOpc = X86::CMP16ri8; break;
    case X86::TEST32rr: NewOpc = X86::CMP32ri8; break;
    case X86::TEST64rr: NewOpc = X86::CMP64ri8; break;
    }
    // Change to CMPXXri r, 0 first.
    MI.setDesc(get(NewOpc));
    MI.getOperand(1).ChangeToImmediate(0);
  } else if (Ops.size() != 1)
    return nullptr;

  // Make sure the subregisters match.
  // Otherwise we risk changing the size of the load.
  if (LoadMI.getOperand(0).getSubReg() != MI.getOperand(Ops[0]).getSubReg())
    return nullptr;

  SmallVector<MachineOperand,X86::AddrNumOperands> MOs;
  switch (LoadMI.getOpcode()) {
  case X86::MMX_SET0:
  case X86::V_SET0:
  case X86::V_SETALLONES:
  case X86::AVX2_SETALLONES:
  case X86::AVX1_SETALLONES:
  case X86::AVX_SET0:
  case X86::AVX512_128_SET0:
  case X86::AVX512_256_SET0:
  case X86::AVX512_512_SET0:
  case X86::AVX512_512_SETALLONES:
  case X86::FsFLD0SD:
  case X86::AVX512_FsFLD0SD:
  case X86::FsFLD0SS:
  case X86::AVX512_FsFLD0SS: {
    // Folding a V_SET0 or V_SETALLONES as a load, to ease register pressure.
    // Create a constant-pool entry and operands to load from it.

    // Medium and large mode can't fold loads this way.
    if (MF.getTarget().getCodeModel() != CodeModel::Small &&
        MF.getTarget().getCodeModel() != CodeModel::Kernel)
      return nullptr;

    // x86-32 PIC requires a PIC base register for constant pools.
    unsigned PICBase = 0;
    if (MF.getTarget().isPositionIndependent()) {
      if (Subtarget.is64Bit())
        PICBase = X86::RIP;
      else
        // FIXME: PICBase = getGlobalBaseReg(&MF);
        // This doesn't work for several reasons.
        // 1. GlobalBaseReg may have been spilled.
        // 2. It may not be live at MI.
        return nullptr;
    }

    // Create a constant-pool entry.
    MachineConstantPool &MCP = *MF.getConstantPool();
    Type *Ty;
    unsigned Opc = LoadMI.getOpcode();
    if (Opc == X86::FsFLD0SS || Opc == X86::AVX512_FsFLD0SS)
      Ty = Type::getFloatTy(MF.getFunction().getContext());
    else if (Opc == X86::FsFLD0SD || Opc == X86::AVX512_FsFLD0SD)
      Ty = Type::getDoubleTy(MF.getFunction().getContext());
    else if (Opc == X86::AVX512_512_SET0 || Opc == X86::AVX512_512_SETALLONES)
      Ty = VectorType::get(Type::getInt32Ty(MF.getFunction().getContext()),16);
    else if (Opc == X86::AVX2_SETALLONES || Opc == X86::AVX_SET0 ||
             Opc == X86::AVX512_256_SET0 || Opc == X86::AVX1_SETALLONES)
      Ty = VectorType::get(Type::getInt32Ty(MF.getFunction().getContext()), 8);
    else if (Opc == X86::MMX_SET0)
      Ty = VectorType::get(Type::getInt32Ty(MF.getFunction().getContext()), 2);
    else
      Ty = VectorType::get(Type::getInt32Ty(MF.getFunction().getContext()), 4);

    bool IsAllOnes = (Opc == X86::V_SETALLONES || Opc == X86::AVX2_SETALLONES ||
                      Opc == X86::AVX512_512_SETALLONES ||
                      Opc == X86::AVX1_SETALLONES);
    const Constant *C = IsAllOnes ? Constant::getAllOnesValue(Ty) :
                                    Constant::getNullValue(Ty);
    unsigned CPI = MCP.getConstantPoolIndex(C, Alignment);

    // Create operands to load from the constant pool entry.
    MOs.push_back(MachineOperand::CreateReg(PICBase, false));
    MOs.push_back(MachineOperand::CreateImm(1));
    MOs.push_back(MachineOperand::CreateReg(0, false));
    MOs.push_back(MachineOperand::CreateCPI(CPI, 0));
    MOs.push_back(MachineOperand::CreateReg(0, false));
    break;
  }
  default: {
    if (isNonFoldablePartialRegisterLoad(LoadMI, MI, MF))
      return nullptr;

    // Folding a normal load. Just copy the load's address operands.
    MOs.append(LoadMI.operands_begin() + NumOps - X86::AddrNumOperands,
               LoadMI.operands_begin() + NumOps);
    break;
  }
  }
  return foldMemoryOperandImpl(MF, MI, Ops[0], MOs, InsertPt,
                               /*Size=*/0, Alignment, /*AllowCommute=*/true);
}

static SmallVector<MachineMemOperand *, 2>
extractLoadMMOs(ArrayRef<MachineMemOperand *> MMOs, MachineFunction &MF) {
  SmallVector<MachineMemOperand *, 2> LoadMMOs;

  for (MachineMemOperand *MMO : MMOs) {
    if (!MMO->isLoad())
      continue;

    if (!MMO->isStore()) {
      // Reuse the MMO.
      LoadMMOs.push_back(MMO);
    } else {
      // Clone the MMO and unset the store flag.
      LoadMMOs.push_back(MF.getMachineMemOperand(
          MMO->getPointerInfo(), MMO->getFlags() & ~MachineMemOperand::MOStore,
          MMO->getSize(), MMO->getBaseAlignment(), MMO->getAAInfo(), nullptr,
          MMO->getSyncScopeID(), MMO->getOrdering(),
          MMO->getFailureOrdering()));
    }
  }

  return LoadMMOs;
}

static SmallVector<MachineMemOperand *, 2>
extractStoreMMOs(ArrayRef<MachineMemOperand *> MMOs, MachineFunction &MF) {
  SmallVector<MachineMemOperand *, 2> StoreMMOs;

  for (MachineMemOperand *MMO : MMOs) {
    if (!MMO->isStore())
      continue;

    if (!MMO->isLoad()) {
      // Reuse the MMO.
      StoreMMOs.push_back(MMO);
    } else {
      // Clone the MMO and unset the load flag.
      StoreMMOs.push_back(MF.getMachineMemOperand(
          MMO->getPointerInfo(), MMO->getFlags() & ~MachineMemOperand::MOLoad,
          MMO->getSize(), MMO->getBaseAlignment(), MMO->getAAInfo(), nullptr,
          MMO->getSyncScopeID(), MMO->getOrdering(),
          MMO->getFailureOrdering()));
    }
  }

  return StoreMMOs;
}

bool X86InstrInfo::unfoldMemoryOperand(
    MachineFunction &MF, MachineInstr &MI, unsigned Reg, bool UnfoldLoad,
    bool UnfoldStore, SmallVectorImpl<MachineInstr *> &NewMIs) const {
  const X86MemoryFoldTableEntry *I = lookupUnfoldTable(MI.getOpcode());
  if (I == nullptr)
    return false;
  unsigned Opc = I->DstOp;
  unsigned Index = I->Flags & TB_INDEX_MASK;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  if (UnfoldLoad && !FoldedLoad)
    return false;
  UnfoldLoad &= FoldedLoad;
  if (UnfoldStore && !FoldedStore)
    return false;
  UnfoldStore &= FoldedStore;

  const MCInstrDesc &MCID = get(Opc);
  const TargetRegisterClass *RC = getRegClass(MCID, Index, &RI, MF);
  // TODO: Check if 32-byte or greater accesses are slow too?
  if (!MI.hasOneMemOperand() && RC == &X86::VR128RegClass &&
      Subtarget.isUnalignedMem16Slow())
    // Without memoperands, loadRegFromAddr and storeRegToStackSlot will
    // conservatively assume the address is unaligned. That's bad for
    // performance.
    return false;
  SmallVector<MachineOperand, X86::AddrNumOperands> AddrOps;
  SmallVector<MachineOperand,2> BeforeOps;
  SmallVector<MachineOperand,2> AfterOps;
  SmallVector<MachineOperand,4> ImpOps;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &Op = MI.getOperand(i);
    if (i >= Index && i < Index + X86::AddrNumOperands)
      AddrOps.push_back(Op);
    else if (Op.isReg() && Op.isImplicit())
      ImpOps.push_back(Op);
    else if (i < Index)
      BeforeOps.push_back(Op);
    else if (i > Index)
      AfterOps.push_back(Op);
  }

  // Emit the load instruction.
  if (UnfoldLoad) {
    auto MMOs = extractLoadMMOs(MI.memoperands(), MF);
    loadRegFromAddr(MF, Reg, AddrOps, RC, MMOs, NewMIs);
    if (UnfoldStore) {
      // Address operands cannot be marked isKill.
      for (unsigned i = 1; i != 1 + X86::AddrNumOperands; ++i) {
        MachineOperand &MO = NewMIs[0]->getOperand(i);
        if (MO.isReg())
          MO.setIsKill(false);
      }
    }
  }

  // Emit the data processing instruction.
  MachineInstr *DataMI = MF.CreateMachineInstr(MCID, MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, DataMI);

  if (FoldedStore)
    MIB.addReg(Reg, RegState::Define);
  for (MachineOperand &BeforeOp : BeforeOps)
    MIB.add(BeforeOp);
  if (FoldedLoad)
    MIB.addReg(Reg);
  for (MachineOperand &AfterOp : AfterOps)
    MIB.add(AfterOp);
  for (MachineOperand &ImpOp : ImpOps) {
    MIB.addReg(ImpOp.getReg(),
               getDefRegState(ImpOp.isDef()) |
               RegState::Implicit |
               getKillRegState(ImpOp.isKill()) |
               getDeadRegState(ImpOp.isDead()) |
               getUndefRegState(ImpOp.isUndef()));
  }
  // Change CMP32ri r, 0 back to TEST32rr r, r, etc.
  switch (DataMI->getOpcode()) {
  default: break;
  case X86::CMP64ri32:
  case X86::CMP64ri8:
  case X86::CMP32ri:
  case X86::CMP32ri8:
  case X86::CMP16ri:
  case X86::CMP16ri8:
  case X86::CMP8ri: {
    MachineOperand &MO0 = DataMI->getOperand(0);
    MachineOperand &MO1 = DataMI->getOperand(1);
    if (MO1.getImm() == 0) {
      unsigned NewOpc;
      switch (DataMI->getOpcode()) {
      default: llvm_unreachable("Unreachable!");
      case X86::CMP64ri8:
      case X86::CMP64ri32: NewOpc = X86::TEST64rr; break;
      case X86::CMP32ri8:
      case X86::CMP32ri:   NewOpc = X86::TEST32rr; break;
      case X86::CMP16ri8:
      case X86::CMP16ri:   NewOpc = X86::TEST16rr; break;
      case X86::CMP8ri:    NewOpc = X86::TEST8rr; break;
      }
      DataMI->setDesc(get(NewOpc));
      MO1.ChangeToRegister(MO0.getReg(), false);
    }
  }
  }
  NewMIs.push_back(DataMI);

  // Emit the store instruction.
  if (UnfoldStore) {
    const TargetRegisterClass *DstRC = getRegClass(MCID, 0, &RI, MF);
    auto MMOs = extractStoreMMOs(MI.memoperands(), MF);
    storeRegToAddr(MF, Reg, true, AddrOps, DstRC, MMOs, NewMIs);
  }

  return true;
}

bool
X86InstrInfo::unfoldMemoryOperand(SelectionDAG &DAG, SDNode *N,
                                  SmallVectorImpl<SDNode*> &NewNodes) const {
  if (!N->isMachineOpcode())
    return false;

  const X86MemoryFoldTableEntry *I = lookupUnfoldTable(N->getMachineOpcode());
  if (I == nullptr)
    return false;
  unsigned Opc = I->DstOp;
  unsigned Index = I->Flags & TB_INDEX_MASK;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  const MCInstrDesc &MCID = get(Opc);
  MachineFunction &MF = DAG.getMachineFunction();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = getRegClass(MCID, Index, &RI, MF);
  unsigned NumDefs = MCID.NumDefs;
  std::vector<SDValue> AddrOps;
  std::vector<SDValue> BeforeOps;
  std::vector<SDValue> AfterOps;
  SDLoc dl(N);
  unsigned NumOps = N->getNumOperands();
  for (unsigned i = 0; i != NumOps-1; ++i) {
    SDValue Op = N->getOperand(i);
    if (i >= Index-NumDefs && i < Index-NumDefs + X86::AddrNumOperands)
      AddrOps.push_back(Op);
    else if (i < Index-NumDefs)
      BeforeOps.push_back(Op);
    else if (i > Index-NumDefs)
      AfterOps.push_back(Op);
  }
  SDValue Chain = N->getOperand(NumOps-1);
  AddrOps.push_back(Chain);

  // Emit the load instruction.
  SDNode *Load = nullptr;
  if (FoldedLoad) {
    EVT VT = *TRI.legalclasstypes_begin(*RC);
    auto MMOs = extractLoadMMOs(cast<MachineSDNode>(N)->memoperands(), MF);
    if (MMOs.empty() && RC == &X86::VR128RegClass &&
        Subtarget.isUnalignedMem16Slow())
      // Do not introduce a slow unaligned load.
      return false;
    // FIXME: If a VR128 can have size 32, we should be checking if a 32-byte
    // memory access is slow above.
    unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
    bool isAligned = !MMOs.empty() && MMOs.front()->getAlignment() >= Alignment;
    Load = DAG.getMachineNode(getLoadRegOpcode(0, RC, isAligned, Subtarget), dl,
                              VT, MVT::Other, AddrOps);
    NewNodes.push_back(Load);

    // Preserve memory reference information.
    DAG.setNodeMemRefs(cast<MachineSDNode>(Load), MMOs);
  }

  // Emit the data processing instruction.
  std::vector<EVT> VTs;
  const TargetRegisterClass *DstRC = nullptr;
  if (MCID.getNumDefs() > 0) {
    DstRC = getRegClass(MCID, 0, &RI, MF);
    VTs.push_back(*TRI.legalclasstypes_begin(*DstRC));
  }
  for (unsigned i = 0, e = N->getNumValues(); i != e; ++i) {
    EVT VT = N->getValueType(i);
    if (VT != MVT::Other && i >= (unsigned)MCID.getNumDefs())
      VTs.push_back(VT);
  }
  if (Load)
    BeforeOps.push_back(SDValue(Load, 0));
  BeforeOps.insert(BeforeOps.end(), AfterOps.begin(), AfterOps.end());
  // Change CMP32ri r, 0 back to TEST32rr r, r, etc.
  switch (Opc) {
    default: break;
    case X86::CMP64ri32:
    case X86::CMP64ri8:
    case X86::CMP32ri:
    case X86::CMP32ri8:
    case X86::CMP16ri:
    case X86::CMP16ri8:
    case X86::CMP8ri:
      if (isNullConstant(BeforeOps[1])) {
        switch (Opc) {
          default: llvm_unreachable("Unreachable!");
          case X86::CMP64ri8:
          case X86::CMP64ri32: Opc = X86::TEST64rr; break;
          case X86::CMP32ri8:
          case X86::CMP32ri:   Opc = X86::TEST32rr; break;
          case X86::CMP16ri8:
          case X86::CMP16ri:   Opc = X86::TEST16rr; break;
          case X86::CMP8ri:    Opc = X86::TEST8rr; break;
        }
        BeforeOps[1] = BeforeOps[0];
      }
  }
  SDNode *NewNode= DAG.getMachineNode(Opc, dl, VTs, BeforeOps);
  NewNodes.push_back(NewNode);

  // Emit the store instruction.
  if (FoldedStore) {
    AddrOps.pop_back();
    AddrOps.push_back(SDValue(NewNode, 0));
    AddrOps.push_back(Chain);
    auto MMOs = extractStoreMMOs(cast<MachineSDNode>(N)->memoperands(), MF);
    if (MMOs.empty() && RC == &X86::VR128RegClass &&
        Subtarget.isUnalignedMem16Slow())
      // Do not introduce a slow unaligned store.
      return false;
    // FIXME: If a VR128 can have size 32, we should be checking if a 32-byte
    // memory access is slow above.
    unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
    bool isAligned = !MMOs.empty() && MMOs.front()->getAlignment() >= Alignment;
    SDNode *Store =
        DAG.getMachineNode(getStoreRegOpcode(0, DstRC, isAligned, Subtarget),
                           dl, MVT::Other, AddrOps);
    NewNodes.push_back(Store);

    // Preserve memory reference information.
    DAG.setNodeMemRefs(cast<MachineSDNode>(Store), MMOs);
  }

  return true;
}

unsigned X86InstrInfo::getOpcodeAfterMemoryUnfold(unsigned Opc,
                                      bool UnfoldLoad, bool UnfoldStore,
                                      unsigned *LoadRegIndex) const {
  const X86MemoryFoldTableEntry *I = lookupUnfoldTable(Opc);
  if (I == nullptr)
    return 0;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  if (UnfoldLoad && !FoldedLoad)
    return 0;
  if (UnfoldStore && !FoldedStore)
    return 0;
  if (LoadRegIndex)
    *LoadRegIndex = I->Flags & TB_INDEX_MASK;
  return I->DstOp;
}

bool
X86InstrInfo::areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                                     int64_t &Offset1, int64_t &Offset2) const {
  if (!Load1->isMachineOpcode() || !Load2->isMachineOpcode())
    return false;
  unsigned Opc1 = Load1->getMachineOpcode();
  unsigned Opc2 = Load2->getMachineOpcode();
  switch (Opc1) {
  default: return false;
  case X86::MOV8rm:
  case X86::MOV16rm:
  case X86::MOV32rm:
  case X86::MOV64rm:
  case X86::LD_Fp32m:
  case X86::LD_Fp64m:
  case X86::LD_Fp80m:
  case X86::MOVSSrm:
  case X86::MOVSDrm:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  // AVX load instructions
  case X86::VMOVSSrm:
  case X86::VMOVSDrm:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  // AVX512 load instructions
  case X86::VMOVSSZrm:
  case X86::VMOVSDZrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQU64Z128rm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVAPSZrm:
  case X86::VMOVUPSZrm:
  case X86::VMOVAPDZrm:
  case X86::VMOVUPDZrm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU64Zrm:
  case X86::KMOVBkm:
  case X86::KMOVWkm:
  case X86::KMOVDkm:
  case X86::KMOVQkm:
    break;
  }
  switch (Opc2) {
  default: return false;
  case X86::MOV8rm:
  case X86::MOV16rm:
  case X86::MOV32rm:
  case X86::MOV64rm:
  case X86::LD_Fp32m:
  case X86::LD_Fp64m:
  case X86::LD_Fp80m:
  case X86::MOVSSrm:
  case X86::MOVSDrm:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  // AVX load instructions
  case X86::VMOVSSrm:
  case X86::VMOVSDrm:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  // AVX512 load instructions
  case X86::VMOVSSZrm:
  case X86::VMOVSDZrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQU64Z128rm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVAPSZrm:
  case X86::VMOVUPSZrm:
  case X86::VMOVAPDZrm:
  case X86::VMOVUPDZrm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU64Zrm:
  case X86::KMOVBkm:
  case X86::KMOVWkm:
  case X86::KMOVDkm:
  case X86::KMOVQkm:
    break;
  }

  // Lambda to check if both the loads have the same value for an operand index.
  auto HasSameOp = [&](int I) {
    return Load1->getOperand(I) == Load2->getOperand(I);
  };

  // All operands except the displacement should match.
  if (!HasSameOp(X86::AddrBaseReg) || !HasSameOp(X86::AddrScaleAmt) ||
      !HasSameOp(X86::AddrIndexReg) || !HasSameOp(X86::AddrSegmentReg))
    return false;

  // Chain Operand must be the same.
  if (!HasSameOp(5))
    return false;

  // Now let's examine if the displacements are constants.
  auto Disp1 = dyn_cast<ConstantSDNode>(Load1->getOperand(X86::AddrDisp));
  auto Disp2 = dyn_cast<ConstantSDNode>(Load2->getOperand(X86::AddrDisp));
  if (!Disp1 || !Disp2)
    return false;

  Offset1 = Disp1->getSExtValue();
  Offset2 = Disp2->getSExtValue();
  return true;
}

bool X86InstrInfo::shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                                           int64_t Offset1, int64_t Offset2,
                                           unsigned NumLoads) const {
  assert(Offset2 > Offset1);
  if ((Offset2 - Offset1) / 8 > 64)
    return false;

  unsigned Opc1 = Load1->getMachineOpcode();
  unsigned Opc2 = Load2->getMachineOpcode();
  if (Opc1 != Opc2)
    return false;  // FIXME: overly conservative?

  switch (Opc1) {
  default: break;
  case X86::LD_Fp32m:
  case X86::LD_Fp64m:
  case X86::LD_Fp80m:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
    return false;
  }

  EVT VT = Load1->getValueType(0);
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    // XMM registers. In 64-bit mode we can be a bit more aggressive since we
    // have 16 of them to play with.
    if (Subtarget.is64Bit()) {
      if (NumLoads >= 3)
        return false;
    } else if (NumLoads) {
      return false;
    }
    break;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
  case MVT::f32:
  case MVT::f64:
    if (NumLoads)
      return false;
    break;
  }

  return true;
}

bool X86InstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid X86 branch condition!");
  X86::CondCode CC = static_cast<X86::CondCode>(Cond[0].getImm());
  Cond[0].setImm(GetOppositeBranchCondition(CC));
  return false;
}

bool X86InstrInfo::
isSafeToMoveRegClassDefs(const TargetRegisterClass *RC) const {
  // FIXME: Return false for x87 stack register classes for now. We can't
  // allow any loads of these registers before FpGet_ST0_80.
  return !(RC == &X86::CCRRegClass || RC == &X86::DFCCRRegClass ||
           RC == &X86::RFP32RegClass || RC == &X86::RFP64RegClass ||
           RC == &X86::RFP80RegClass);
}

/// Return a virtual register initialized with the
/// the global base register value. Output instructions required to
/// initialize the register in the function entry block, if necessary.
///
/// TODO: Eliminate this and move the code to X86MachineFunctionInfo.
///
unsigned X86InstrInfo::getGlobalBaseReg(MachineFunction *MF) const {
  assert((!Subtarget.is64Bit() ||
          MF->getTarget().getCodeModel() == CodeModel::Medium ||
          MF->getTarget().getCodeModel() == CodeModel::Large) &&
         "X86-64 PIC uses RIP relative addressing");

  X86MachineFunctionInfo *X86FI = MF->getInfo<X86MachineFunctionInfo>();
  unsigned GlobalBaseReg = X86FI->getGlobalBaseReg();
  if (GlobalBaseReg != 0)
    return GlobalBaseReg;

  // Create the register. The code to initialize it is inserted
  // later, by the CGBR pass (below).
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  GlobalBaseReg = RegInfo.createVirtualRegister(
      Subtarget.is64Bit() ? &X86::GR64_NOSPRegClass : &X86::GR32_NOSPRegClass);
  X86FI->setGlobalBaseReg(GlobalBaseReg);
  return GlobalBaseReg;
}

// These are the replaceable SSE instructions. Some of these have Int variants
// that we don't include here. We don't want to replace instructions selected
// by intrinsics.
static const uint16_t ReplaceableInstrs[][3] = {
  //PackedSingle     PackedDouble    PackedInt
  { X86::MOVAPSmr,   X86::MOVAPDmr,  X86::MOVDQAmr  },
  { X86::MOVAPSrm,   X86::MOVAPDrm,  X86::MOVDQArm  },
  { X86::MOVAPSrr,   X86::MOVAPDrr,  X86::MOVDQArr  },
  { X86::MOVUPSmr,   X86::MOVUPDmr,  X86::MOVDQUmr  },
  { X86::MOVUPSrm,   X86::MOVUPDrm,  X86::MOVDQUrm  },
  { X86::MOVLPSmr,   X86::MOVLPDmr,  X86::MOVPQI2QImr },
  { X86::MOVSDmr,    X86::MOVSDmr,   X86::MOVPQI2QImr },
  { X86::MOVSSmr,    X86::MOVSSmr,   X86::MOVPDI2DImr },
  { X86::MOVSDrm,    X86::MOVSDrm,   X86::MOVQI2PQIrm },
  { X86::MOVSSrm,    X86::MOVSSrm,   X86::MOVDI2PDIrm },
  { X86::MOVNTPSmr,  X86::MOVNTPDmr, X86::MOVNTDQmr },
  { X86::ANDNPSrm,   X86::ANDNPDrm,  X86::PANDNrm   },
  { X86::ANDNPSrr,   X86::ANDNPDrr,  X86::PANDNrr   },
  { X86::ANDPSrm,    X86::ANDPDrm,   X86::PANDrm    },
  { X86::ANDPSrr,    X86::ANDPDrr,   X86::PANDrr    },
  { X86::ORPSrm,     X86::ORPDrm,    X86::PORrm     },
  { X86::ORPSrr,     X86::ORPDrr,    X86::PORrr     },
  { X86::XORPSrm,    X86::XORPDrm,   X86::PXORrm    },
  { X86::XORPSrr,    X86::XORPDrr,   X86::PXORrr    },
  { X86::UNPCKLPDrm, X86::UNPCKLPDrm, X86::PUNPCKLQDQrm },
  { X86::MOVLHPSrr,  X86::UNPCKLPDrr, X86::PUNPCKLQDQrr },
  { X86::UNPCKHPDrm, X86::UNPCKHPDrm, X86::PUNPCKHQDQrm },
  { X86::UNPCKHPDrr, X86::UNPCKHPDrr, X86::PUNPCKHQDQrr },
  { X86::UNPCKLPSrm, X86::UNPCKLPSrm, X86::PUNPCKLDQrm },
  { X86::UNPCKLPSrr, X86::UNPCKLPSrr, X86::PUNPCKLDQrr },
  { X86::UNPCKHPSrm, X86::UNPCKHPSrm, X86::PUNPCKHDQrm },
  { X86::UNPCKHPSrr, X86::UNPCKHPSrr, X86::PUNPCKHDQrr },
  { X86::EXTRACTPSmr, X86::EXTRACTPSmr, X86::PEXTRDmr },
  { X86::EXTRACTPSrr, X86::EXTRACTPSrr, X86::PEXTRDrr },
  // AVX 128-bit support
  { X86::VMOVAPSmr,  X86::VMOVAPDmr,  X86::VMOVDQAmr  },
  { X86::VMOVAPSrm,  X86::VMOVAPDrm,  X86::VMOVDQArm  },
  { X86::VMOVAPSrr,  X86::VMOVAPDrr,  X86::VMOVDQArr  },
  { X86::VMOVUPSmr,  X86::VMOVUPDmr,  X86::VMOVDQUmr  },
  { X86::VMOVUPSrm,  X86::VMOVUPDrm,  X86::VMOVDQUrm  },
  { X86::VMOVLPSmr,  X86::VMOVLPDmr,  X86::VMOVPQI2QImr },
  { X86::VMOVSDmr,   X86::VMOVSDmr,   X86::VMOVPQI2QImr },
  { X86::VMOVSSmr,   X86::VMOVSSmr,   X86::VMOVPDI2DImr },
  { X86::VMOVSDrm,   X86::VMOVSDrm,   X86::VMOVQI2PQIrm },
  { X86::VMOVSSrm,   X86::VMOVSSrm,   X86::VMOVDI2PDIrm },
  { X86::VMOVNTPSmr, X86::VMOVNTPDmr, X86::VMOVNTDQmr },
  { X86::VANDNPSrm,  X86::VANDNPDrm,  X86::VPANDNrm   },
  { X86::VANDNPSrr,  X86::VANDNPDrr,  X86::VPANDNrr   },
  { X86::VANDPSrm,   X86::VANDPDrm,   X86::VPANDrm    },
  { X86::VANDPSrr,   X86::VANDPDrr,   X86::VPANDrr    },
  { X86::VORPSrm,    X86::VORPDrm,    X86::VPORrm     },
  { X86::VORPSrr,    X86::VORPDrr,    X86::VPORrr     },
  { X86::VXORPSrm,   X86::VXORPDrm,   X86::VPXORrm    },
  { X86::VXORPSrr,   X86::VXORPDrr,   X86::VPXORrr    },
  { X86::VUNPCKLPDrm, X86::VUNPCKLPDrm, X86::VPUNPCKLQDQrm },
  { X86::VMOVLHPSrr,  X86::VUNPCKLPDrr, X86::VPUNPCKLQDQrr },
  { X86::VUNPCKHPDrm, X86::VUNPCKHPDrm, X86::VPUNPCKHQDQrm },
  { X86::VUNPCKHPDrr, X86::VUNPCKHPDrr, X86::VPUNPCKHQDQrr },
  { X86::VUNPCKLPSrm, X86::VUNPCKLPSrm, X86::VPUNPCKLDQrm },
  { X86::VUNPCKLPSrr, X86::VUNPCKLPSrr, X86::VPUNPCKLDQrr },
  { X86::VUNPCKHPSrm, X86::VUNPCKHPSrm, X86::VPUNPCKHDQrm },
  { X86::VUNPCKHPSrr, X86::VUNPCKHPSrr, X86::VPUNPCKHDQrr },
  { X86::VEXTRACTPSmr, X86::VEXTRACTPSmr, X86::VPEXTRDmr },
  { X86::VEXTRACTPSrr, X86::VEXTRACTPSrr, X86::VPEXTRDrr },
  // AVX 256-bit support
  { X86::VMOVAPSYmr,   X86::VMOVAPDYmr,   X86::VMOVDQAYmr  },
  { X86::VMOVAPSYrm,   X86::VMOVAPDYrm,   X86::VMOVDQAYrm  },
  { X86::VMOVAPSYrr,   X86::VMOVAPDYrr,   X86::VMOVDQAYrr  },
  { X86::VMOVUPSYmr,   X86::VMOVUPDYmr,   X86::VMOVDQUYmr  },
  { X86::VMOVUPSYrm,   X86::VMOVUPDYrm,   X86::VMOVDQUYrm  },
  { X86::VMOVNTPSYmr,  X86::VMOVNTPDYmr,  X86::VMOVNTDQYmr },
  { X86::VPERMPSYrm,   X86::VPERMPSYrm,   X86::VPERMDYrm },
  { X86::VPERMPSYrr,   X86::VPERMPSYrr,   X86::VPERMDYrr },
  { X86::VPERMPDYmi,   X86::VPERMPDYmi,   X86::VPERMQYmi },
  { X86::VPERMPDYri,   X86::VPERMPDYri,   X86::VPERMQYri },
  // AVX512 support
  { X86::VMOVLPSZ128mr,  X86::VMOVLPDZ128mr,  X86::VMOVPQI2QIZmr  },
  { X86::VMOVNTPSZ128mr, X86::VMOVNTPDZ128mr, X86::VMOVNTDQZ128mr },
  { X86::VMOVNTPSZ256mr, X86::VMOVNTPDZ256mr, X86::VMOVNTDQZ256mr },
  { X86::VMOVNTPSZmr,    X86::VMOVNTPDZmr,    X86::VMOVNTDQZmr    },
  { X86::VMOVSDZmr,      X86::VMOVSDZmr,      X86::VMOVPQI2QIZmr  },
  { X86::VMOVSSZmr,      X86::VMOVSSZmr,      X86::VMOVPDI2DIZmr  },
  { X86::VMOVSDZrm,      X86::VMOVSDZrm,      X86::VMOVQI2PQIZrm  },
  { X86::VMOVSSZrm,      X86::VMOVSSZrm,      X86::VMOVDI2PDIZrm  },
  { X86::VBROADCASTSSZ128r, X86::VBROADCASTSSZ128r, X86::VPBROADCASTDZ128r },
  { X86::VBROADCASTSSZ128m, X86::VBROADCASTSSZ128m, X86::VPBROADCASTDZ128m },
  { X86::VBROADCASTSSZ256r, X86::VBROADCASTSSZ256r, X86::VPBROADCASTDZ256r },
  { X86::VBROADCASTSSZ256m, X86::VBROADCASTSSZ256m, X86::VPBROADCASTDZ256m },
  { X86::VBROADCASTSSZr,    X86::VBROADCASTSSZr,    X86::VPBROADCASTDZr },
  { X86::VBROADCASTSSZm,    X86::VBROADCASTSSZm,    X86::VPBROADCASTDZm },
  { X86::VBROADCASTSDZ256r, X86::VBROADCASTSDZ256r, X86::VPBROADCASTQZ256r },
  { X86::VBROADCASTSDZ256m, X86::VBROADCASTSDZ256m, X86::VPBROADCASTQZ256m },
  { X86::VBROADCASTSDZr,    X86::VBROADCASTSDZr,    X86::VPBROADCASTQZr },
  { X86::VBROADCASTSDZm,    X86::VBROADCASTSDZm,    X86::VPBROADCASTQZm },
  { X86::VINSERTF32x4Zrr,   X86::VINSERTF32x4Zrr,   X86::VINSERTI32x4Zrr },
  { X86::VINSERTF32x4Zrm,   X86::VINSERTF32x4Zrm,   X86::VINSERTI32x4Zrm },
  { X86::VINSERTF32x8Zrr,   X86::VINSERTF32x8Zrr,   X86::VINSERTI32x8Zrr },
  { X86::VINSERTF32x8Zrm,   X86::VINSERTF32x8Zrm,   X86::VINSERTI32x8Zrm },
  { X86::VINSERTF64x2Zrr,   X86::VINSERTF64x2Zrr,   X86::VINSERTI64x2Zrr },
  { X86::VINSERTF64x2Zrm,   X86::VINSERTF64x2Zrm,   X86::VINSERTI64x2Zrm },
  { X86::VINSERTF64x4Zrr,   X86::VINSERTF64x4Zrr,   X86::VINSERTI64x4Zrr },
  { X86::VINSERTF64x4Zrm,   X86::VINSERTF64x4Zrm,   X86::VINSERTI64x4Zrm },
  { X86::VINSERTF32x4Z256rr,X86::VINSERTF32x4Z256rr,X86::VINSERTI32x4Z256rr },
  { X86::VINSERTF32x4Z256rm,X86::VINSERTF32x4Z256rm,X86::VINSERTI32x4Z256rm },
  { X86::VINSERTF64x2Z256rr,X86::VINSERTF64x2Z256rr,X86::VINSERTI64x2Z256rr },
  { X86::VINSERTF64x2Z256rm,X86::VINSERTF64x2Z256rm,X86::VINSERTI64x2Z256rm },
  { X86::VEXTRACTF32x4Zrr,   X86::VEXTRACTF32x4Zrr,   X86::VEXTRACTI32x4Zrr },
  { X86::VEXTRACTF32x4Zmr,   X86::VEXTRACTF32x4Zmr,   X86::VEXTRACTI32x4Zmr },
  { X86::VEXTRACTF32x8Zrr,   X86::VEXTRACTF32x8Zrr,   X86::VEXTRACTI32x8Zrr },
  { X86::VEXTRACTF32x8Zmr,   X86::VEXTRACTF32x8Zmr,   X86::VEXTRACTI32x8Zmr },
  { X86::VEXTRACTF64x2Zrr,   X86::VEXTRACTF64x2Zrr,   X86::VEXTRACTI64x2Zrr },
  { X86::VEXTRACTF64x2Zmr,   X86::VEXTRACTF64x2Zmr,   X86::VEXTRACTI64x2Zmr },
  { X86::VEXTRACTF64x4Zrr,   X86::VEXTRACTF64x4Zrr,   X86::VEXTRACTI64x4Zrr },
  { X86::VEXTRACTF64x4Zmr,   X86::VEXTRACTF64x4Zmr,   X86::VEXTRACTI64x4Zmr },
  { X86::VEXTRACTF32x4Z256rr,X86::VEXTRACTF32x4Z256rr,X86::VEXTRACTI32x4Z256rr },
  { X86::VEXTRACTF32x4Z256mr,X86::VEXTRACTF32x4Z256mr,X86::VEXTRACTI32x4Z256mr },
  { X86::VEXTRACTF64x2Z256rr,X86::VEXTRACTF64x2Z256rr,X86::VEXTRACTI64x2Z256rr },
  { X86::VEXTRACTF64x2Z256mr,X86::VEXTRACTF64x2Z256mr,X86::VEXTRACTI64x2Z256mr },
  { X86::VPERMILPSmi,        X86::VPERMILPSmi,        X86::VPSHUFDmi },
  { X86::VPERMILPSri,        X86::VPERMILPSri,        X86::VPSHUFDri },
  { X86::VPERMILPSZ128mi,    X86::VPERMILPSZ128mi,    X86::VPSHUFDZ128mi },
  { X86::VPERMILPSZ128ri,    X86::VPERMILPSZ128ri,    X86::VPSHUFDZ128ri },
  { X86::VPERMILPSZ256mi,    X86::VPERMILPSZ256mi,    X86::VPSHUFDZ256mi },
  { X86::VPERMILPSZ256ri,    X86::VPERMILPSZ256ri,    X86::VPSHUFDZ256ri },
  { X86::VPERMILPSZmi,       X86::VPERMILPSZmi,       X86::VPSHUFDZmi },
  { X86::VPERMILPSZri,       X86::VPERMILPSZri,       X86::VPSHUFDZri },
  { X86::VPERMPSZ256rm,      X86::VPERMPSZ256rm,      X86::VPERMDZ256rm },
  { X86::VPERMPSZ256rr,      X86::VPERMPSZ256rr,      X86::VPERMDZ256rr },
  { X86::VPERMPDZ256mi,      X86::VPERMPDZ256mi,      X86::VPERMQZ256mi },
  { X86::VPERMPDZ256ri,      X86::VPERMPDZ256ri,      X86::VPERMQZ256ri },
  { X86::VPERMPDZ256rm,      X86::VPERMPDZ256rm,      X86::VPERMQZ256rm },
  { X86::VPERMPDZ256rr,      X86::VPERMPDZ256rr,      X86::VPERMQZ256rr },
  { X86::VPERMPSZrm,         X86::VPERMPSZrm,         X86::VPERMDZrm },
  { X86::VPERMPSZrr,         X86::VPERMPSZrr,         X86::VPERMDZrr },
  { X86::VPERMPDZmi,         X86::VPERMPDZmi,         X86::VPERMQZmi },
  { X86::VPERMPDZri,         X86::VPERMPDZri,         X86::VPERMQZri },
  { X86::VPERMPDZrm,         X86::VPERMPDZrm,         X86::VPERMQZrm },
  { X86::VPERMPDZrr,         X86::VPERMPDZrr,         X86::VPERMQZrr },
  { X86::VUNPCKLPDZ256rm,    X86::VUNPCKLPDZ256rm,    X86::VPUNPCKLQDQZ256rm },
  { X86::VUNPCKLPDZ256rr,    X86::VUNPCKLPDZ256rr,    X86::VPUNPCKLQDQZ256rr },
  { X86::VUNPCKHPDZ256rm,    X86::VUNPCKHPDZ256rm,    X86::VPUNPCKHQDQZ256rm },
  { X86::VUNPCKHPDZ256rr,    X86::VUNPCKHPDZ256rr,    X86::VPUNPCKHQDQZ256rr },
  { X86::VUNPCKLPSZ256rm,    X86::VUNPCKLPSZ256rm,    X86::VPUNPCKLDQZ256rm },
  { X86::VUNPCKLPSZ256rr,    X86::VUNPCKLPSZ256rr,    X86::VPUNPCKLDQZ256rr },
  { X86::VUNPCKHPSZ256rm,    X86::VUNPCKHPSZ256rm,    X86::VPUNPCKHDQZ256rm },
  { X86::VUNPCKHPSZ256rr,    X86::VUNPCKHPSZ256rr,    X86::VPUNPCKHDQZ256rr },
  { X86::VUNPCKLPDZ128rm,    X86::VUNPCKLPDZ128rm,    X86::VPUNPCKLQDQZ128rm },
  { X86::VMOVLHPSZrr,        X86::VUNPCKLPDZ128rr,    X86::VPUNPCKLQDQZ128rr },
  { X86::VUNPCKHPDZ128rm,    X86::VUNPCKHPDZ128rm,    X86::VPUNPCKHQDQZ128rm },
  { X86::VUNPCKHPDZ128rr,    X86::VUNPCKHPDZ128rr,    X86::VPUNPCKHQDQZ128rr },
  { X86::VUNPCKLPSZ128rm,    X86::VUNPCKLPSZ128rm,    X86::VPUNPCKLDQZ128rm },
  { X86::VUNPCKLPSZ128rr,    X86::VUNPCKLPSZ128rr,    X86::VPUNPCKLDQZ128rr },
  { X86::VUNPCKHPSZ128rm,    X86::VUNPCKHPSZ128rm,    X86::VPUNPCKHDQZ128rm },
  { X86::VUNPCKHPSZ128rr,    X86::VUNPCKHPSZ128rr,    X86::VPUNPCKHDQZ128rr },
  { X86::VUNPCKLPDZrm,       X86::VUNPCKLPDZrm,       X86::VPUNPCKLQDQZrm },
  { X86::VUNPCKLPDZrr,       X86::VUNPCKLPDZrr,       X86::VPUNPCKLQDQZrr },
  { X86::VUNPCKHPDZrm,       X86::VUNPCKHPDZrm,       X86::VPUNPCKHQDQZrm },
  { X86::VUNPCKHPDZrr,       X86::VUNPCKHPDZrr,       X86::VPUNPCKHQDQZrr },
  { X86::VUNPCKLPSZrm,       X86::VUNPCKLPSZrm,       X86::VPUNPCKLDQZrm },
  { X86::VUNPCKLPSZrr,       X86::VUNPCKLPSZrr,       X86::VPUNPCKLDQZrr },
  { X86::VUNPCKHPSZrm,       X86::VUNPCKHPSZrm,       X86::VPUNPCKHDQZrm },
  { X86::VUNPCKHPSZrr,       X86::VUNPCKHPSZrr,       X86::VPUNPCKHDQZrr },
  { X86::VEXTRACTPSZmr,      X86::VEXTRACTPSZmr,      X86::VPEXTRDZmr },
  { X86::VEXTRACTPSZrr,      X86::VEXTRACTPSZrr,      X86::VPEXTRDZrr },
};

static const uint16_t ReplaceableInstrsAVX2[][3] = {
  //PackedSingle       PackedDouble       PackedInt
  { X86::VANDNPSYrm,   X86::VANDNPDYrm,   X86::VPANDNYrm   },
  { X86::VANDNPSYrr,   X86::VANDNPDYrr,   X86::VPANDNYrr   },
  { X86::VANDPSYrm,    X86::VANDPDYrm,    X86::VPANDYrm    },
  { X86::VANDPSYrr,    X86::VANDPDYrr,    X86::VPANDYrr    },
  { X86::VORPSYrm,     X86::VORPDYrm,     X86::VPORYrm     },
  { X86::VORPSYrr,     X86::VORPDYrr,     X86::VPORYrr     },
  { X86::VXORPSYrm,    X86::VXORPDYrm,    X86::VPXORYrm    },
  { X86::VXORPSYrr,    X86::VXORPDYrr,    X86::VPXORYrr    },
  { X86::VPERM2F128rm,   X86::VPERM2F128rm,   X86::VPERM2I128rm },
  { X86::VPERM2F128rr,   X86::VPERM2F128rr,   X86::VPERM2I128rr },
  { X86::VBROADCASTSSrm, X86::VBROADCASTSSrm, X86::VPBROADCASTDrm},
  { X86::VBROADCASTSSrr, X86::VBROADCASTSSrr, X86::VPBROADCASTDrr},
  { X86::VBROADCASTSSYrr, X86::VBROADCASTSSYrr, X86::VPBROADCASTDYrr},
  { X86::VBROADCASTSSYrm, X86::VBROADCASTSSYrm, X86::VPBROADCASTDYrm},
  { X86::VBROADCASTSDYrr, X86::VBROADCASTSDYrr, X86::VPBROADCASTQYrr},
  { X86::VBROADCASTSDYrm, X86::VBROADCASTSDYrm, X86::VPBROADCASTQYrm},
  { X86::VBROADCASTF128,  X86::VBROADCASTF128,  X86::VBROADCASTI128 },
  { X86::VBLENDPSYrri,    X86::VBLENDPSYrri,    X86::VPBLENDDYrri },
  { X86::VBLENDPSYrmi,    X86::VBLENDPSYrmi,    X86::VPBLENDDYrmi },
  { X86::VPERMILPSYmi,    X86::VPERMILPSYmi,    X86::VPSHUFDYmi },
  { X86::VPERMILPSYri,    X86::VPERMILPSYri,    X86::VPSHUFDYri },
  { X86::VUNPCKLPDYrm,    X86::VUNPCKLPDYrm,    X86::VPUNPCKLQDQYrm },
  { X86::VUNPCKLPDYrr,    X86::VUNPCKLPDYrr,    X86::VPUNPCKLQDQYrr },
  { X86::VUNPCKHPDYrm,    X86::VUNPCKHPDYrm,    X86::VPUNPCKHQDQYrm },
  { X86::VUNPCKHPDYrr,    X86::VUNPCKHPDYrr,    X86::VPUNPCKHQDQYrr },
  { X86::VUNPCKLPSYrm,    X86::VUNPCKLPSYrm,    X86::VPUNPCKLDQYrm },
  { X86::VUNPCKLPSYrr,    X86::VUNPCKLPSYrr,    X86::VPUNPCKLDQYrr },
  { X86::VUNPCKHPSYrm,    X86::VUNPCKHPSYrm,    X86::VPUNPCKHDQYrm },
  { X86::VUNPCKHPSYrr,    X86::VUNPCKHPSYrr,    X86::VPUNPCKHDQYrr },
};

static const uint16_t ReplaceableInstrsAVX2InsertExtract[][3] = {
  //PackedSingle       PackedDouble       PackedInt
  { X86::VEXTRACTF128mr, X86::VEXTRACTF128mr, X86::VEXTRACTI128mr },
  { X86::VEXTRACTF128rr, X86::VEXTRACTF128rr, X86::VEXTRACTI128rr },
  { X86::VINSERTF128rm,  X86::VINSERTF128rm,  X86::VINSERTI128rm },
  { X86::VINSERTF128rr,  X86::VINSERTF128rr,  X86::VINSERTI128rr },
};

static const uint16_t ReplaceableInstrsAVX512[][4] = {
  // Two integer columns for 64-bit and 32-bit elements.
  //PackedSingle        PackedDouble        PackedInt             PackedInt
  { X86::VMOVAPSZ128mr, X86::VMOVAPDZ128mr, X86::VMOVDQA64Z128mr, X86::VMOVDQA32Z128mr  },
  { X86::VMOVAPSZ128rm, X86::VMOVAPDZ128rm, X86::VMOVDQA64Z128rm, X86::VMOVDQA32Z128rm  },
  { X86::VMOVAPSZ128rr, X86::VMOVAPDZ128rr, X86::VMOVDQA64Z128rr, X86::VMOVDQA32Z128rr  },
  { X86::VMOVUPSZ128mr, X86::VMOVUPDZ128mr, X86::VMOVDQU64Z128mr, X86::VMOVDQU32Z128mr  },
  { X86::VMOVUPSZ128rm, X86::VMOVUPDZ128rm, X86::VMOVDQU64Z128rm, X86::VMOVDQU32Z128rm  },
  { X86::VMOVAPSZ256mr, X86::VMOVAPDZ256mr, X86::VMOVDQA64Z256mr, X86::VMOVDQA32Z256mr  },
  { X86::VMOVAPSZ256rm, X86::VMOVAPDZ256rm, X86::VMOVDQA64Z256rm, X86::VMOVDQA32Z256rm  },
  { X86::VMOVAPSZ256rr, X86::VMOVAPDZ256rr, X86::VMOVDQA64Z256rr, X86::VMOVDQA32Z256rr  },
  { X86::VMOVUPSZ256mr, X86::VMOVUPDZ256mr, X86::VMOVDQU64Z256mr, X86::VMOVDQU32Z256mr  },
  { X86::VMOVUPSZ256rm, X86::VMOVUPDZ256rm, X86::VMOVDQU64Z256rm, X86::VMOVDQU32Z256rm  },
  { X86::VMOVAPSZmr,    X86::VMOVAPDZmr,    X86::VMOVDQA64Zmr,    X86::VMOVDQA32Zmr     },
  { X86::VMOVAPSZrm,    X86::VMOVAPDZrm,    X86::VMOVDQA64Zrm,    X86::VMOVDQA32Zrm     },
  { X86::VMOVAPSZrr,    X86::VMOVAPDZrr,    X86::VMOVDQA64Zrr,    X86::VMOVDQA32Zrr     },
  { X86::VMOVUPSZmr,    X86::VMOVUPDZmr,    X86::VMOVDQU64Zmr,    X86::VMOVDQU32Zmr     },
  { X86::VMOVUPSZrm,    X86::VMOVUPDZrm,    X86::VMOVDQU64Zrm,    X86::VMOVDQU32Zrm     },
};

static const uint16_t ReplaceableInstrsAVX512DQ[][4] = {
  // Two integer columns for 64-bit and 32-bit elements.
  //PackedSingle        PackedDouble        PackedInt           PackedInt
  { X86::VANDNPSZ128rm, X86::VANDNPDZ128rm, X86::VPANDNQZ128rm, X86::VPANDNDZ128rm },
  { X86::VANDNPSZ128rr, X86::VANDNPDZ128rr, X86::VPANDNQZ128rr, X86::VPANDNDZ128rr },
  { X86::VANDPSZ128rm,  X86::VANDPDZ128rm,  X86::VPANDQZ128rm,  X86::VPANDDZ128rm  },
  { X86::VANDPSZ128rr,  X86::VANDPDZ128rr,  X86::VPANDQZ128rr,  X86::VPANDDZ128rr  },
  { X86::VORPSZ128rm,   X86::VORPDZ128rm,   X86::VPORQZ128rm,   X86::VPORDZ128rm   },
  { X86::VORPSZ128rr,   X86::VORPDZ128rr,   X86::VPORQZ128rr,   X86::VPORDZ128rr   },
  { X86::VXORPSZ128rm,  X86::VXORPDZ128rm,  X86::VPXORQZ128rm,  X86::VPXORDZ128rm  },
  { X86::VXORPSZ128rr,  X86::VXORPDZ128rr,  X86::VPXORQZ128rr,  X86::VPXORDZ128rr  },
  { X86::VANDNPSZ256rm, X86::VANDNPDZ256rm, X86::VPANDNQZ256rm, X86::VPANDNDZ256rm },
  { X86::VANDNPSZ256rr, X86::VANDNPDZ256rr, X86::VPANDNQZ256rr, X86::VPANDNDZ256rr },
  { X86::VANDPSZ256rm,  X86::VANDPDZ256rm,  X86::VPANDQZ256rm,  X86::VPANDDZ256rm  },
  { X86::VANDPSZ256rr,  X86::VANDPDZ256rr,  X86::VPANDQZ256rr,  X86::VPANDDZ256rr  },
  { X86::VORPSZ256rm,   X86::VORPDZ256rm,   X86::VPORQZ256rm,   X86::VPORDZ256rm   },
  { X86::VORPSZ256rr,   X86::VORPDZ256rr,   X86::VPORQZ256rr,   X86::VPORDZ256rr   },
  { X86::VXORPSZ256rm,  X86::VXORPDZ256rm,  X86::VPXORQZ256rm,  X86::VPXORDZ256rm  },
  { X86::VXORPSZ256rr,  X86::VXORPDZ256rr,  X86::VPXORQZ256rr,  X86::VPXORDZ256rr  },
  { X86::VANDNPSZrm,    X86::VANDNPDZrm,    X86::VPANDNQZrm,    X86::VPANDNDZrm    },
  { X86::VANDNPSZrr,    X86::VANDNPDZrr,    X86::VPANDNQZrr,    X86::VPANDNDZrr    },
  { X86::VANDPSZrm,     X86::VANDPDZrm,     X86::VPANDQZrm,     X86::VPANDDZrm     },
  { X86::VANDPSZrr,     X86::VANDPDZrr,     X86::VPANDQZrr,     X86::VPANDDZrr     },
  { X86::VORPSZrm,      X86::VORPDZrm,      X86::VPORQZrm,      X86::VPORDZrm      },
  { X86::VORPSZrr,      X86::VORPDZrr,      X86::VPORQZrr,      X86::VPORDZrr      },
  { X86::VXORPSZrm,     X86::VXORPDZrm,     X86::VPXORQZrm,     X86::VPXORDZrm     },
  { X86::VXORPSZrr,     X86::VXORPDZrr,     X86::VPXORQZrr,     X86::VPXORDZrr     },
};

static const uint16_t ReplaceableInstrsAVX512DQMasked[][4] = {
  // Two integer columns for 64-bit and 32-bit elements.
  //PackedSingle          PackedDouble
  //PackedInt             PackedInt
  { X86::VANDNPSZ128rmk,  X86::VANDNPDZ128rmk,
    X86::VPANDNQZ128rmk,  X86::VPANDNDZ128rmk  },
  { X86::VANDNPSZ128rmkz, X86::VANDNPDZ128rmkz,
    X86::VPANDNQZ128rmkz, X86::VPANDNDZ128rmkz },
  { X86::VANDNPSZ128rrk,  X86::VANDNPDZ128rrk,
    X86::VPANDNQZ128rrk,  X86::VPANDNDZ128rrk  },
  { X86::VANDNPSZ128rrkz, X86::VANDNPDZ128rrkz,
    X86::VPANDNQZ128rrkz, X86::VPANDNDZ128rrkz },
  { X86::VANDPSZ128rmk,   X86::VANDPDZ128rmk,
    X86::VPANDQZ128rmk,   X86::VPANDDZ128rmk   },
  { X86::VANDPSZ128rmkz,  X86::VANDPDZ128rmkz,
    X86::VPANDQZ128rmkz,  X86::VPANDDZ128rmkz  },
  { X86::VANDPSZ128rrk,   X86::VANDPDZ128rrk,
    X86::VPANDQZ128rrk,   X86::VPANDDZ128rrk   },
  { X86::VANDPSZ128rrkz,  X86::VANDPDZ128rrkz,
    X86::VPANDQZ128rrkz,  X86::VPANDDZ128rrkz  },
  { X86::VORPSZ128rmk,    X86::VORPDZ128rmk,
    X86::VPORQZ128rmk,    X86::VPORDZ128rmk    },
  { X86::VORPSZ128rmkz,   X86::VORPDZ128rmkz,
    X86::VPORQZ128rmkz,   X86::VPORDZ128rmkz   },
  { X86::VORPSZ128rrk,    X86::VORPDZ128rrk,
    X86::VPORQZ128rrk,    X86::VPORDZ128rrk    },
  { X86::VORPSZ128rrkz,   X86::VORPDZ128rrkz,
    X86::VPORQZ128rrkz,   X86::VPORDZ128rrkz   },
  { X86::VXORPSZ128rmk,   X86::VXORPDZ128rmk,
    X86::VPXORQZ128rmk,   X86::VPXORDZ128rmk   },
  { X86::VXORPSZ128rmkz,  X86::VXORPDZ128rmkz,
    X86::VPXORQZ128rmkz,  X86::VPXORDZ128rmkz  },
  { X86::VXORPSZ128rrk,   X86::VXORPDZ128rrk,
    X86::VPXORQZ128rrk,   X86::VPXORDZ128rrk   },
  { X86::VXORPSZ128rrkz,  X86::VXORPDZ128rrkz,
    X86::VPXORQZ128rrkz,  X86::VPXORDZ128rrkz  },
  { X86::VANDNPSZ256rmk,  X86::VANDNPDZ256rmk,
    X86::VPANDNQZ256rmk,  X86::VPANDNDZ256rmk  },
  { X86::VANDNPSZ256rmkz, X86::VANDNPDZ256rmkz,
    X86::VPANDNQZ256rmkz, X86::VPANDNDZ256rmkz },
  { X86::VANDNPSZ256rrk,  X86::VANDNPDZ256rrk,
    X86::VPANDNQZ256rrk,  X86::VPANDNDZ256rrk  },
  { X86::VANDNPSZ256rrkz, X86::VANDNPDZ256rrkz,
    X86::VPANDNQZ256rrkz, X86::VPANDNDZ256rrkz },
  { X86::VANDPSZ256rmk,   X86::VANDPDZ256rmk,
    X86::VPANDQZ256rmk,   X86::VPANDDZ256rmk   },
  { X86::VANDPSZ256rmkz,  X86::VANDPDZ256rmkz,
    X86::VPANDQZ256rmkz,  X86::VPANDDZ256rmkz  },
  { X86::VANDPSZ256rrk,   X86::VANDPDZ256rrk,
    X86::VPANDQZ256rrk,   X86::VPANDDZ256rrk   },
  { X86::VANDPSZ256rrkz,  X86::VANDPDZ256rrkz,
    X86::VPANDQZ256rrkz,  X86::VPANDDZ256rrkz  },
  { X86::VORPSZ256rmk,    X86::VORPDZ256rmk,
    X86::VPORQZ256rmk,    X86::VPORDZ256rmk    },
  { X86::VORPSZ256rmkz,   X86::VORPDZ256rmkz,
    X86::VPORQZ256rmkz,   X86::VPORDZ256rmkz   },
  { X86::VORPSZ256rrk,    X86::VORPDZ256rrk,
    X86::VPORQZ256rrk,    X86::VPORDZ256rrk    },
  { X86::VORPSZ256rrkz,   X86::VORPDZ256rrkz,
    X86::VPORQZ256rrkz,   X86::VPORDZ256rrkz   },
  { X86::VXORPSZ256rmk,   X86::VXORPDZ256rmk,
    X86::VPXORQZ256rmk,   X86::VPXORDZ256rmk   },
  { X86::VXORPSZ256rmkz,  X86::VXORPDZ256rmkz,
    X86::VPXORQZ256rmkz,  X86::VPXORDZ256rmkz  },
  { X86::VXORPSZ256rrk,   X86::VXORPDZ256rrk,
    X86::VPXORQZ256rrk,   X86::VPXORDZ256rrk   },
  { X86::VXORPSZ256rrkz,  X86::VXORPDZ256rrkz,
    X86::VPXORQZ256rrkz,  X86::VPXORDZ256rrkz  },
  { X86::VANDNPSZrmk,     X86::VANDNPDZrmk,
    X86::VPANDNQZrmk,     X86::VPANDNDZrmk     },
  { X86::VANDNPSZrmkz,    X86::VANDNPDZrmkz,
    X86::VPANDNQZrmkz,    X86::VPANDNDZrmkz    },
  { X86::VANDNPSZrrk,     X86::VANDNPDZrrk,
    X86::VPANDNQZrrk,     X86::VPANDNDZrrk     },
  { X86::VANDNPSZrrkz,    X86::VANDNPDZrrkz,
    X86::VPANDNQZrrkz,    X86::VPANDNDZrrkz    },
  { X86::VANDPSZrmk,      X86::VANDPDZrmk,
    X86::VPANDQZrmk,      X86::VPANDDZrmk      },
  { X86::VANDPSZrmkz,     X86::VANDPDZrmkz,
    X86::VPANDQZrmkz,     X86::VPANDDZrmkz     },
  { X86::VANDPSZrrk,      X86::VANDPDZrrk,
    X86::VPANDQZrrk,      X86::VPANDDZrrk      },
  { X86::VANDPSZrrkz,     X86::VANDPDZrrkz,
    X86::VPANDQZrrkz,     X86::VPANDDZrrkz     },
  { X86::VORPSZrmk,       X86::VORPDZrmk,
    X86::VPORQZrmk,       X86::VPORDZrmk       },
  { X86::VORPSZrmkz,      X86::VORPDZrmkz,
    X86::VPORQZrmkz,      X86::VPORDZrmkz      },
  { X86::VORPSZrrk,       X86::VORPDZrrk,
    X86::VPORQZrrk,       X86::VPORDZrrk       },
  { X86::VORPSZrrkz,      X86::VORPDZrrkz,
    X86::VPORQZrrkz,      X86::VPORDZrrkz      },
  { X86::VXORPSZrmk,      X86::VXORPDZrmk,
    X86::VPXORQZrmk,      X86::VPXORDZrmk      },
  { X86::VXORPSZrmkz,     X86::VXORPDZrmkz,
    X86::VPXORQZrmkz,     X86::VPXORDZrmkz     },
  { X86::VXORPSZrrk,      X86::VXORPDZrrk,
    X86::VPXORQZrrk,      X86::VPXORDZrrk      },
  { X86::VXORPSZrrkz,     X86::VXORPDZrrkz,
    X86::VPXORQZrrkz,     X86::VPXORDZrrkz     },
  // Broadcast loads can be handled the same as masked operations to avoid
  // changing element size.
  { X86::VANDNPSZ128rmb,  X86::VANDNPDZ128rmb,
    X86::VPANDNQZ128rmb,  X86::VPANDNDZ128rmb  },
  { X86::VANDPSZ128rmb,   X86::VANDPDZ128rmb,
    X86::VPANDQZ128rmb,   X86::VPANDDZ128rmb   },
  { X86::VORPSZ128rmb,    X86::VORPDZ128rmb,
    X86::VPORQZ128rmb,    X86::VPORDZ128rmb    },
  { X86::VXORPSZ128rmb,   X86::VXORPDZ128rmb,
    X86::VPXORQZ128rmb,   X86::VPXORDZ128rmb   },
  { X86::VANDNPSZ256rmb,  X86::VANDNPDZ256rmb,
    X86::VPANDNQZ256rmb,  X86::VPANDNDZ256rmb  },
  { X86::VANDPSZ256rmb,   X86::VANDPDZ256rmb,
    X86::VPANDQZ256rmb,   X86::VPANDDZ256rmb   },
  { X86::VORPSZ256rmb,    X86::VORPDZ256rmb,
    X86::VPORQZ256rmb,    X86::VPORDZ256rmb    },
  { X86::VXORPSZ256rmb,   X86::VXORPDZ256rmb,
    X86::VPXORQZ256rmb,   X86::VPXORDZ256rmb   },
  { X86::VANDNPSZrmb,     X86::VANDNPDZrmb,
    X86::VPANDNQZrmb,     X86::VPANDNDZrmb     },
  { X86::VANDPSZrmb,      X86::VANDPDZrmb,
    X86::VPANDQZrmb,      X86::VPANDDZrmb      },
  { X86::VANDPSZrmb,      X86::VANDPDZrmb,
    X86::VPANDQZrmb,      X86::VPANDDZrmb      },
  { X86::VORPSZrmb,       X86::VORPDZrmb,
    X86::VPORQZrmb,       X86::VPORDZrmb       },
  { X86::VXORPSZrmb,      X86::VXORPDZrmb,
    X86::VPXORQZrmb,      X86::VPXORDZrmb      },
  { X86::VANDNPSZ128rmbk, X86::VANDNPDZ128rmbk,
    X86::VPANDNQZ128rmbk, X86::VPANDNDZ128rmbk },
  { X86::VANDPSZ128rmbk,  X86::VANDPDZ128rmbk,
    X86::VPANDQZ128rmbk,  X86::VPANDDZ128rmbk  },
  { X86::VORPSZ128rmbk,   X86::VORPDZ128rmbk,
    X86::VPORQZ128rmbk,   X86::VPORDZ128rmbk   },
  { X86::VXORPSZ128rmbk,  X86::VXORPDZ128rmbk,
    X86::VPXORQZ128rmbk,  X86::VPXORDZ128rmbk  },
  { X86::VANDNPSZ256rmbk, X86::VANDNPDZ256rmbk,
    X86::VPANDNQZ256rmbk, X86::VPANDNDZ256rmbk },
  { X86::VANDPSZ256rmbk,  X86::VANDPDZ256rmbk,
    X86::VPANDQZ256rmbk,  X86::VPANDDZ256rmbk  },
  { X86::VORPSZ256rmbk,   X86::VORPDZ256rmbk,
    X86::VPORQZ256rmbk,   X86::VPORDZ256rmbk   },
  { X86::VXORPSZ256rmbk,  X86::VXORPDZ256rmbk,
    X86::VPXORQZ256rmbk,  X86::VPXORDZ256rmbk  },
  { X86::VANDNPSZrmbk,    X86::VANDNPDZrmbk,
    X86::VPANDNQZrmbk,    X86::VPANDNDZrmbk    },
  { X86::VANDPSZrmbk,     X86::VANDPDZrmbk,
    X86::VPANDQZrmbk,     X86::VPANDDZrmbk     },
  { X86::VANDPSZrmbk,     X86::VANDPDZrmbk,
    X86::VPANDQZrmbk,     X86::VPANDDZrmbk     },
  { X86::VORPSZrmbk,      X86::VORPDZrmbk,
    X86::VPORQZrmbk,      X86::VPORDZrmbk      },
  { X86::VXORPSZrmbk,     X86::VXORPDZrmbk,
    X86::VPXORQZrmbk,     X86::VPXORDZrmbk     },
  { X86::VANDNPSZ128rmbkz,X86::VANDNPDZ128rmbkz,
    X86::VPANDNQZ128rmbkz,X86::VPANDNDZ128rmbkz},
  { X86::VANDPSZ128rmbkz, X86::VANDPDZ128rmbkz,
    X86::VPANDQZ128rmbkz, X86::VPANDDZ128rmbkz },
  { X86::VORPSZ128rmbkz,  X86::VORPDZ128rmbkz,
    X86::VPORQZ128rmbkz,  X86::VPORDZ128rmbkz  },
  { X86::VXORPSZ128rmbkz, X86::VXORPDZ128rmbkz,
    X86::VPXORQZ128rmbkz, X86::VPXORDZ128rmbkz },
  { X86::VANDNPSZ256rmbkz,X86::VANDNPDZ256rmbkz,
    X86::VPANDNQZ256rmbkz,X86::VPANDNDZ256rmbkz},
  { X86::VANDPSZ256rmbkz, X86::VANDPDZ256rmbkz,
    X86::VPANDQZ256rmbkz, X86::VPANDDZ256rmbkz },
  { X86::VORPSZ256rmbkz,  X86::VORPDZ256rmbkz,
    X86::VPORQZ256rmbkz,  X86::VPORDZ256rmbkz  },
  { X86::VXORPSZ256rmbkz, X86::VXORPDZ256rmbkz,
    X86::VPXORQZ256rmbkz, X86::VPXORDZ256rmbkz },
  { X86::VANDNPSZrmbkz,   X86::VANDNPDZrmbkz,
    X86::VPANDNQZrmbkz,   X86::VPANDNDZrmbkz   },
  { X86::VANDPSZrmbkz,    X86::VANDPDZrmbkz,
    X86::VPANDQZrmbkz,    X86::VPANDDZrmbkz    },
  { X86::VANDPSZrmbkz,    X86::VANDPDZrmbkz,
    X86::VPANDQZrmbkz,    X86::VPANDDZrmbkz    },
  { X86::VORPSZrmbkz,     X86::VORPDZrmbkz,
    X86::VPORQZrmbkz,     X86::VPORDZrmbkz     },
  { X86::VXORPSZrmbkz,    X86::VXORPDZrmbkz,
    X86::VPXORQZrmbkz,    X86::VPXORDZrmbkz    },
};

// NOTE: These should only be used by the custom domain methods.
static const uint16_t ReplaceableCustomInstrs[][3] = {
  //PackedSingle             PackedDouble             PackedInt
  { X86::BLENDPSrmi,         X86::BLENDPDrmi,         X86::PBLENDWrmi   },
  { X86::BLENDPSrri,         X86::BLENDPDrri,         X86::PBLENDWrri   },
  { X86::VBLENDPSrmi,        X86::VBLENDPDrmi,        X86::VPBLENDWrmi  },
  { X86::VBLENDPSrri,        X86::VBLENDPDrri,        X86::VPBLENDWrri  },
  { X86::VBLENDPSYrmi,       X86::VBLENDPDYrmi,       X86::VPBLENDWYrmi },
  { X86::VBLENDPSYrri,       X86::VBLENDPDYrri,       X86::VPBLENDWYrri },
};
static const uint16_t ReplaceableCustomAVX2Instrs[][3] = {
  //PackedSingle             PackedDouble             PackedInt
  { X86::VBLENDPSrmi,        X86::VBLENDPDrmi,        X86::VPBLENDDrmi  },
  { X86::VBLENDPSrri,        X86::VBLENDPDrri,        X86::VPBLENDDrri  },
  { X86::VBLENDPSYrmi,       X86::VBLENDPDYrmi,       X86::VPBLENDDYrmi },
  { X86::VBLENDPSYrri,       X86::VBLENDPDYrri,       X86::VPBLENDDYrri },
};

// Special table for changing EVEX logic instructions to VEX.
// TODO: Should we run EVEX->VEX earlier?
static const uint16_t ReplaceableCustomAVX512LogicInstrs[][4] = {
  // Two integer columns for 64-bit and 32-bit elements.
  //PackedSingle     PackedDouble     PackedInt           PackedInt
  { X86::VANDNPSrm,  X86::VANDNPDrm,  X86::VPANDNQZ128rm, X86::VPANDNDZ128rm },
  { X86::VANDNPSrr,  X86::VANDNPDrr,  X86::VPANDNQZ128rr, X86::VPANDNDZ128rr },
  { X86::VANDPSrm,   X86::VANDPDrm,   X86::VPANDQZ128rm,  X86::VPANDDZ128rm  },
  { X86::VANDPSrr,   X86::VANDPDrr,   X86::VPANDQZ128rr,  X86::VPANDDZ128rr  },
  { X86::VORPSrm,    X86::VORPDrm,    X86::VPORQZ128rm,   X86::VPORDZ128rm   },
  { X86::VORPSrr,    X86::VORPDrr,    X86::VPORQZ128rr,   X86::VPORDZ128rr   },
  { X86::VXORPSrm,   X86::VXORPDrm,   X86::VPXORQZ128rm,  X86::VPXORDZ128rm  },
  { X86::VXORPSrr,   X86::VXORPDrr,   X86::VPXORQZ128rr,  X86::VPXORDZ128rr  },
  { X86::VANDNPSYrm, X86::VANDNPDYrm, X86::VPANDNQZ256rm, X86::VPANDNDZ256rm },
  { X86::VANDNPSYrr, X86::VANDNPDYrr, X86::VPANDNQZ256rr, X86::VPANDNDZ256rr },
  { X86::VANDPSYrm,  X86::VANDPDYrm,  X86::VPANDQZ256rm,  X86::VPANDDZ256rm  },
  { X86::VANDPSYrr,  X86::VANDPDYrr,  X86::VPANDQZ256rr,  X86::VPANDDZ256rr  },
  { X86::VORPSYrm,   X86::VORPDYrm,   X86::VPORQZ256rm,   X86::VPORDZ256rm   },
  { X86::VORPSYrr,   X86::VORPDYrr,   X86::VPORQZ256rr,   X86::VPORDZ256rr   },
  { X86::VXORPSYrm,  X86::VXORPDYrm,  X86::VPXORQZ256rm,  X86::VPXORDZ256rm  },
  { X86::VXORPSYrr,  X86::VXORPDYrr,  X86::VPXORQZ256rr,  X86::VPXORDZ256rr  },
};

// FIXME: Some shuffle and unpack instructions have equivalents in different
// domains, but they require a bit more work than just switching opcodes.

static const uint16_t *lookup(unsigned opcode, unsigned domain,
                              ArrayRef<uint16_t[3]> Table) {
  for (const uint16_t (&Row)[3] : Table)
    if (Row[domain-1] == opcode)
      return Row;
  return nullptr;
}

static const uint16_t *lookupAVX512(unsigned opcode, unsigned domain,
                                    ArrayRef<uint16_t[4]> Table) {
  // If this is the integer domain make sure to check both integer columns.
  for (const uint16_t (&Row)[4] : Table)
    if (Row[domain-1] == opcode || (domain == 3 && Row[3] == opcode))
      return Row;
  return nullptr;
}

// Helper to attempt to widen/narrow blend masks.
static bool AdjustBlendMask(unsigned OldMask, unsigned OldWidth,
                            unsigned NewWidth, unsigned *pNewMask = nullptr) {
  assert(((OldWidth % NewWidth) == 0 || (NewWidth % OldWidth) == 0) &&
         "Illegal blend mask scale");
  unsigned NewMask = 0;

  if ((OldWidth % NewWidth) == 0) {
    unsigned Scale = OldWidth / NewWidth;
    unsigned SubMask = (1u << Scale) - 1;
    for (unsigned i = 0; i != NewWidth; ++i) {
      unsigned Sub = (OldMask >> (i * Scale)) & SubMask;
      if (Sub == SubMask)
        NewMask |= (1u << i);
      else if (Sub != 0x0)
        return false;
    }
  } else {
    unsigned Scale = NewWidth / OldWidth;
    unsigned SubMask = (1u << Scale) - 1;
    for (unsigned i = 0; i != OldWidth; ++i) {
      if (OldMask & (1 << i)) {
        NewMask |= (SubMask << (i * Scale));
      }
    }
  }

  if (pNewMask)
    *pNewMask = NewMask;
  return true;
}

uint16_t X86InstrInfo::getExecutionDomainCustom(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  unsigned NumOperands = MI.getDesc().getNumOperands();

  auto GetBlendDomains = [&](unsigned ImmWidth, bool Is256) {
    uint16_t validDomains = 0;
    if (MI.getOperand(NumOperands - 1).isImm()) {
      unsigned Imm = MI.getOperand(NumOperands - 1).getImm();
      if (AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4))
        validDomains |= 0x2; // PackedSingle
      if (AdjustBlendMask(Imm, ImmWidth, Is256 ? 4 : 2))
        validDomains |= 0x4; // PackedDouble
      if (!Is256 || Subtarget.hasAVX2())
        validDomains |= 0x8; // PackedInt
    }
    return validDomains;
  };

  switch (Opcode) {
  case X86::BLENDPDrmi:
  case X86::BLENDPDrri:
  case X86::VBLENDPDrmi:
  case X86::VBLENDPDrri:
    return GetBlendDomains(2, false);
  case X86::VBLENDPDYrmi:
  case X86::VBLENDPDYrri:
    return GetBlendDomains(4, true);
  case X86::BLENDPSrmi:
  case X86::BLENDPSrri:
  case X86::VBLENDPSrmi:
  case X86::VBLENDPSrri:
  case X86::VPBLENDDrmi:
  case X86::VPBLENDDrri:
    return GetBlendDomains(4, false);
  case X86::VBLENDPSYrmi:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDYrmi:
  case X86::VPBLENDDYrri:
    return GetBlendDomains(8, true);
  case X86::PBLENDWrmi:
  case X86::PBLENDWrri:
  case X86::VPBLENDWrmi:
  case X86::VPBLENDWrri:
  // Treat VPBLENDWY as a 128-bit vector as it repeats the lo/hi masks.
  case X86::VPBLENDWYrmi:
  case X86::VPBLENDWYrri:
    return GetBlendDomains(8, false);
  case X86::VPANDDZ128rr:  case X86::VPANDDZ128rm:
  case X86::VPANDDZ256rr:  case X86::VPANDDZ256rm:
  case X86::VPANDQZ128rr:  case X86::VPANDQZ128rm:
  case X86::VPANDQZ256rr:  case X86::VPANDQZ256rm:
  case X86::VPANDNDZ128rr: case X86::VPANDNDZ128rm:
  case X86::VPANDNDZ256rr: case X86::VPANDNDZ256rm:
  case X86::VPANDNQZ128rr: case X86::VPANDNQZ128rm:
  case X86::VPANDNQZ256rr: case X86::VPANDNQZ256rm:
  case X86::VPORDZ128rr:   case X86::VPORDZ128rm:
  case X86::VPORDZ256rr:   case X86::VPORDZ256rm:
  case X86::VPORQZ128rr:   case X86::VPORQZ128rm:
  case X86::VPORQZ256rr:   case X86::VPORQZ256rm:
  case X86::VPXORDZ128rr:  case X86::VPXORDZ128rm:
  case X86::VPXORDZ256rr:  case X86::VPXORDZ256rm:
  case X86::VPXORQZ128rr:  case X86::VPXORQZ128rm:
  case X86::VPXORQZ256rr:  case X86::VPXORQZ256rm:
    // If we don't have DQI see if we can still switch from an EVEX integer
    // instruction to a VEX floating point instruction.
    if (Subtarget.hasDQI())
      return 0;

    if (RI.getEncodingValue(MI.getOperand(0).getReg()) >= 16)
      return 0;
    if (RI.getEncodingValue(MI.getOperand(1).getReg()) >= 16)
      return 0;
    // Register forms will have 3 operands. Memory form will have more.
    if (NumOperands == 3 &&
        RI.getEncodingValue(MI.getOperand(2).getReg()) >= 16)
      return 0;

    // All domains are valid.
    return 0xe;
  case X86::MOVHLPSrr:
    // We can swap domains when both inputs are the same register.
    // FIXME: This doesn't catch all the cases we would like. If the input
    // register isn't KILLed by the instruction, the two address instruction
    // pass puts a COPY on one input. The other input uses the original
    // register. This prevents the same physical register from being used by
    // both inputs.
    if (MI.getOperand(1).getReg() == MI.getOperand(2).getReg() &&
        MI.getOperand(0).getSubReg() == 0 &&
        MI.getOperand(1).getSubReg() == 0 &&
        MI.getOperand(2).getSubReg() == 0)
      return 0x6;
    return 0;
  }
  return 0;
}

bool X86InstrInfo::setExecutionDomainCustom(MachineInstr &MI,
                                            unsigned Domain) const {
  assert(Domain > 0 && Domain < 4 && "Invalid execution domain");
  uint16_t dom = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  assert(dom && "Not an SSE instruction");

  unsigned Opcode = MI.getOpcode();
  unsigned NumOperands = MI.getDesc().getNumOperands();

  auto SetBlendDomain = [&](unsigned ImmWidth, bool Is256) {
    if (MI.getOperand(NumOperands - 1).isImm()) {
      unsigned Imm = MI.getOperand(NumOperands - 1).getImm() & 255;
      Imm = (ImmWidth == 16 ? ((Imm << 8) | Imm) : Imm);
      unsigned NewImm = Imm;

      const uint16_t *table = lookup(Opcode, dom, ReplaceableCustomInstrs);
      if (!table)
        table = lookup(Opcode, dom, ReplaceableCustomAVX2Instrs);

      if (Domain == 1) { // PackedSingle
        AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4, &NewImm);
      } else if (Domain == 2) { // PackedDouble
        AdjustBlendMask(Imm, ImmWidth, Is256 ? 4 : 2, &NewImm);
      } else if (Domain == 3) { // PackedInt
        if (Subtarget.hasAVX2()) {
          // If we are already VPBLENDW use that, else use VPBLENDD.
          if ((ImmWidth / (Is256 ? 2 : 1)) != 8) {
            table = lookup(Opcode, dom, ReplaceableCustomAVX2Instrs);
            AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4, &NewImm);
          }
        } else {
          assert(!Is256 && "128-bit vector expected");
          AdjustBlendMask(Imm, ImmWidth, 8, &NewImm);
        }
      }

      assert(table && table[Domain - 1] && "Unknown domain op");
      MI.setDesc(get(table[Domain - 1]));
      MI.getOperand(NumOperands - 1).setImm(NewImm & 255);
    }
    return true;
  };

  switch (Opcode) {
  case X86::BLENDPDrmi:
  case X86::BLENDPDrri:
  case X86::VBLENDPDrmi:
  case X86::VBLENDPDrri:
    return SetBlendDomain(2, false);
  case X86::VBLENDPDYrmi:
  case X86::VBLENDPDYrri:
    return SetBlendDomain(4, true);
  case X86::BLENDPSrmi:
  case X86::BLENDPSrri:
  case X86::VBLENDPSrmi:
  case X86::VBLENDPSrri:
  case X86::VPBLENDDrmi:
  case X86::VPBLENDDrri:
    return SetBlendDomain(4, false);
  case X86::VBLENDPSYrmi:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDYrmi:
  case X86::VPBLENDDYrri:
    return SetBlendDomain(8, true);
  case X86::PBLENDWrmi:
  case X86::PBLENDWrri:
  case X86::VPBLENDWrmi:
  case X86::VPBLENDWrri:
    return SetBlendDomain(8, false);
  case X86::VPBLENDWYrmi:
  case X86::VPBLENDWYrri:
    return SetBlendDomain(16, true);
  case X86::VPANDDZ128rr:  case X86::VPANDDZ128rm:
  case X86::VPANDDZ256rr:  case X86::VPANDDZ256rm:
  case X86::VPANDQZ128rr:  case X86::VPANDQZ128rm:
  case X86::VPANDQZ256rr:  case X86::VPANDQZ256rm:
  case X86::VPANDNDZ128rr: case X86::VPANDNDZ128rm:
  case X86::VPANDNDZ256rr: case X86::VPANDNDZ256rm:
  case X86::VPANDNQZ128rr: case X86::VPANDNQZ128rm:
  case X86::VPANDNQZ256rr: case X86::VPANDNQZ256rm:
  case X86::VPORDZ128rr:   case X86::VPORDZ128rm:
  case X86::VPORDZ256rr:   case X86::VPORDZ256rm:
  case X86::VPORQZ128rr:   case X86::VPORQZ128rm:
  case X86::VPORQZ256rr:   case X86::VPORQZ256rm:
  case X86::VPXORDZ128rr:  case X86::VPXORDZ128rm:
  case X86::VPXORDZ256rr:  case X86::VPXORDZ256rm:
  case X86::VPXORQZ128rr:  case X86::VPXORQZ128rm:
  case X86::VPXORQZ256rr:  case X86::VPXORQZ256rm: {
    // Without DQI, convert EVEX instructions to VEX instructions.
    if (Subtarget.hasDQI())
      return false;

    const uint16_t *table = lookupAVX512(MI.getOpcode(), dom,
                                         ReplaceableCustomAVX512LogicInstrs);
    assert(table && "Instruction not found in table?");
    // Don't change integer Q instructions to D instructions and
    // use D intructions if we started with a PS instruction.
    if (Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
    MI.setDesc(get(table[Domain - 1]));
    return true;
  }
  case X86::UNPCKHPDrr:
  case X86::MOVHLPSrr:
    // We just need to commute the instruction which will switch the domains.
    if (Domain != dom && Domain != 3 &&
        MI.getOperand(1).getReg() == MI.getOperand(2).getReg() &&
        MI.getOperand(0).getSubReg() == 0 &&
        MI.getOperand(1).getSubReg() == 0 &&
        MI.getOperand(2).getSubReg() == 0) {
      commuteInstruction(MI, false);
      return true;
    }
    // We must always return true for MOVHLPSrr.
    if (Opcode == X86::MOVHLPSrr)
      return true;
  }
  return false;
}

std::pair<uint16_t, uint16_t>
X86InstrInfo::getExecutionDomain(const MachineInstr &MI) const {
  uint16_t domain = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  unsigned opcode = MI.getOpcode();
  uint16_t validDomains = 0;
  if (domain) {
    // Attempt to match for custom instructions.
    validDomains = getExecutionDomainCustom(MI);
    if (validDomains)
      return std::make_pair(domain, validDomains);

    if (lookup(opcode, domain, ReplaceableInstrs)) {
      validDomains = 0xe;
    } else if (lookup(opcode, domain, ReplaceableInstrsAVX2)) {
      validDomains = Subtarget.hasAVX2() ? 0xe : 0x6;
    } else if (lookup(opcode, domain, ReplaceableInstrsAVX2InsertExtract)) {
      // Insert/extract instructions should only effect domain if AVX2
      // is enabled.
      if (!Subtarget.hasAVX2())
        return std::make_pair(0, 0);
      validDomains = 0xe;
    } else if (lookupAVX512(opcode, domain, ReplaceableInstrsAVX512)) {
      validDomains = 0xe;
    } else if (Subtarget.hasDQI() && lookupAVX512(opcode, domain,
                                                  ReplaceableInstrsAVX512DQ)) {
      validDomains = 0xe;
    } else if (Subtarget.hasDQI()) {
      if (const uint16_t *table = lookupAVX512(opcode, domain,
                                             ReplaceableInstrsAVX512DQMasked)) {
        if (domain == 1 || (domain == 3 && table[3] == opcode))
          validDomains = 0xa;
        else
          validDomains = 0xc;
      }
    }
  }
  return std::make_pair(domain, validDomains);
}

void X86InstrInfo::setExecutionDomain(MachineInstr &MI, unsigned Domain) const {
  assert(Domain>0 && Domain<4 && "Invalid execution domain");
  uint16_t dom = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  assert(dom && "Not an SSE instruction");

  // Attempt to match for custom instructions.
  if (setExecutionDomainCustom(MI, Domain))
    return;

  const uint16_t *table = lookup(MI.getOpcode(), dom, ReplaceableInstrs);
  if (!table) { // try the other table
    assert((Subtarget.hasAVX2() || Domain < 3) &&
           "256-bit vector operations only available in AVX2");
    table = lookup(MI.getOpcode(), dom, ReplaceableInstrsAVX2);
  }
  if (!table) { // try the other table
    assert(Subtarget.hasAVX2() &&
           "256-bit insert/extract only available in AVX2");
    table = lookup(MI.getOpcode(), dom, ReplaceableInstrsAVX2InsertExtract);
  }
  if (!table) { // try the AVX512 table
    assert(Subtarget.hasAVX512() && "Requires AVX-512");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512);
    // Don't change integer Q instructions to D instructions.
    if (table && Domain == 3 && table[3] == MI.getOpcode())
      Domain = 4;
  }
  if (!table) { // try the AVX512DQ table
    assert((Subtarget.hasDQI() || Domain >= 3) && "Requires AVX-512DQ");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512DQ);
    // Don't change integer Q instructions to D instructions and
    // use D intructions if we started with a PS instruction.
    if (table && Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
  }
  if (!table) { // try the AVX512DQMasked table
    assert((Subtarget.hasDQI() || Domain >= 3) && "Requires AVX-512DQ");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512DQMasked);
    if (table && Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
  }
  assert(table && "Cannot change domain");
  MI.setDesc(get(table[Domain - 1]));
}

/// Return the noop instruction to use for a noop.
void X86InstrInfo::getNoop(MCInst &NopInst) const {
  NopInst.setOpcode(X86::NOOP);
}

bool X86InstrInfo::isHighLatencyDef(int opc) const {
  switch (opc) {
  default: return false;
  case X86::DIVPDrm:
  case X86::DIVPDrr:
  case X86::DIVPSrm:
  case X86::DIVPSrr:
  case X86::DIVSDrm:
  case X86::DIVSDrm_Int:
  case X86::DIVSDrr:
  case X86::DIVSDrr_Int:
  case X86::DIVSSrm:
  case X86::DIVSSrm_Int:
  case X86::DIVSSrr:
  case X86::DIVSSrr_Int:
  case X86::SQRTPDm:
  case X86::SQRTPDr:
  case X86::SQRTPSm:
  case X86::SQRTPSr:
  case X86::SQRTSDm:
  case X86::SQRTSDm_Int:
  case X86::SQRTSDr:
  case X86::SQRTSDr_Int:
  case X86::SQRTSSm:
  case X86::SQRTSSm_Int:
  case X86::SQRTSSr:
  case X86::SQRTSSr_Int:
  // AVX instructions with high latency
  case X86::VDIVPDrm:
  case X86::VDIVPDrr:
  case X86::VDIVPDYrm:
  case X86::VDIVPDYrr:
  case X86::VDIVPSrm:
  case X86::VDIVPSrr:
  case X86::VDIVPSYrm:
  case X86::VDIVPSYrr:
  case X86::VDIVSDrm:
  case X86::VDIVSDrm_Int:
  case X86::VDIVSDrr:
  case X86::VDIVSDrr_Int:
  case X86::VDIVSSrm:
  case X86::VDIVSSrm_Int:
  case X86::VDIVSSrr:
  case X86::VDIVSSrr_Int:
  case X86::VSQRTPDm:
  case X86::VSQRTPDr:
  case X86::VSQRTPDYm:
  case X86::VSQRTPDYr:
  case X86::VSQRTPSm:
  case X86::VSQRTPSr:
  case X86::VSQRTPSYm:
  case X86::VSQRTPSYr:
  case X86::VSQRTSDm:
  case X86::VSQRTSDm_Int:
  case X86::VSQRTSDr:
  case X86::VSQRTSDr_Int:
  case X86::VSQRTSSm:
  case X86::VSQRTSSm_Int:
  case X86::VSQRTSSr:
  case X86::VSQRTSSr_Int:
  // AVX512 instructions with high latency
  case X86::VDIVPDZ128rm:
  case X86::VDIVPDZ128rmb:
  case X86::VDIVPDZ128rmbk:
  case X86::VDIVPDZ128rmbkz:
  case X86::VDIVPDZ128rmk:
  case X86::VDIVPDZ128rmkz:
  case X86::VDIVPDZ128rr:
  case X86::VDIVPDZ128rrk:
  case X86::VDIVPDZ128rrkz:
  case X86::VDIVPDZ256rm:
  case X86::VDIVPDZ256rmb:
  case X86::VDIVPDZ256rmbk:
  case X86::VDIVPDZ256rmbkz:
  case X86::VDIVPDZ256rmk:
  case X86::VDIVPDZ256rmkz:
  case X86::VDIVPDZ256rr:
  case X86::VDIVPDZ256rrk:
  case X86::VDIVPDZ256rrkz:
  case X86::VDIVPDZrrb:
  case X86::VDIVPDZrrbk:
  case X86::VDIVPDZrrbkz:
  case X86::VDIVPDZrm:
  case X86::VDIVPDZrmb:
  case X86::VDIVPDZrmbk:
  case X86::VDIVPDZrmbkz:
  case X86::VDIVPDZrmk:
  case X86::VDIVPDZrmkz:
  case X86::VDIVPDZrr:
  case X86::VDIVPDZrrk:
  case X86::VDIVPDZrrkz:
  case X86::VDIVPSZ128rm:
  case X86::VDIVPSZ128rmb:
  case X86::VDIVPSZ128rmbk:
  case X86::VDIVPSZ128rmbkz:
  case X86::VDIVPSZ128rmk:
  case X86::VDIVPSZ128rmkz:
  case X86::VDIVPSZ128rr:
  case X86::VDIVPSZ128rrk:
  case X86::VDIVPSZ128rrkz:
  case X86::VDIVPSZ256rm:
  case X86::VDIVPSZ256rmb:
  case X86::VDIVPSZ256rmbk:
  case X86::VDIVPSZ256rmbkz:
  case X86::VDIVPSZ256rmk:
  case X86::VDIVPSZ256rmkz:
  case X86::VDIVPSZ256rr:
  case X86::VDIVPSZ256rrk:
  case X86::VDIVPSZ256rrkz:
  case X86::VDIVPSZrrb:
  case X86::VDIVPSZrrbk:
  case X86::VDIVPSZrrbkz:
  case X86::VDIVPSZrm:
  case X86::VDIVPSZrmb:
  case X86::VDIVPSZrmbk:
  case X86::VDIVPSZrmbkz:
  case X86::VDIVPSZrmk:
  case X86::VDIVPSZrmkz:
  case X86::VDIVPSZrr:
  case X86::VDIVPSZrrk:
  case X86::VDIVPSZrrkz:
  case X86::VDIVSDZrm:
  case X86::VDIVSDZrr:
  case X86::VDIVSDZrm_Int:
  case X86::VDIVSDZrm_Intk:
  case X86::VDIVSDZrm_Intkz:
  case X86::VDIVSDZrr_Int:
  case X86::VDIVSDZrr_Intk:
  case X86::VDIVSDZrr_Intkz:
  case X86::VDIVSDZrrb_Int:
  case X86::VDIVSDZrrb_Intk:
  case X86::VDIVSDZrrb_Intkz:
  case X86::VDIVSSZrm:
  case X86::VDIVSSZrr:
  case X86::VDIVSSZrm_Int:
  case X86::VDIVSSZrm_Intk:
  case X86::VDIVSSZrm_Intkz:
  case X86::VDIVSSZrr_Int:
  case X86::VDIVSSZrr_Intk:
  case X86::VDIVSSZrr_Intkz:
  case X86::VDIVSSZrrb_Int:
  case X86::VDIVSSZrrb_Intk:
  case X86::VDIVSSZrrb_Intkz:
  case X86::VSQRTPDZ128m:
  case X86::VSQRTPDZ128mb:
  case X86::VSQRTPDZ128mbk:
  case X86::VSQRTPDZ128mbkz:
  case X86::VSQRTPDZ128mk:
  case X86::VSQRTPDZ128mkz:
  case X86::VSQRTPDZ128r:
  case X86::VSQRTPDZ128rk:
  case X86::VSQRTPDZ128rkz:
  case X86::VSQRTPDZ256m:
  case X86::VSQRTPDZ256mb:
  case X86::VSQRTPDZ256mbk:
  case X86::VSQRTPDZ256mbkz:
  case X86::VSQRTPDZ256mk:
  case X86::VSQRTPDZ256mkz:
  case X86::VSQRTPDZ256r:
  case X86::VSQRTPDZ256rk:
  case X86::VSQRTPDZ256rkz:
  case X86::VSQRTPDZm:
  case X86::VSQRTPDZmb:
  case X86::VSQRTPDZmbk:
  case X86::VSQRTPDZmbkz:
  case X86::VSQRTPDZmk:
  case X86::VSQRTPDZmkz:
  case X86::VSQRTPDZr:
  case X86::VSQRTPDZrb:
  case X86::VSQRTPDZrbk:
  case X86::VSQRTPDZrbkz:
  case X86::VSQRTPDZrk:
  case X86::VSQRTPDZrkz:
  case X86::VSQRTPSZ128m:
  case X86::VSQRTPSZ128mb:
  case X86::VSQRTPSZ128mbk:
  case X86::VSQRTPSZ128mbkz:
  case X86::VSQRTPSZ128mk:
  case X86::VSQRTPSZ128mkz:
  case X86::VSQRTPSZ128r:
  case X86::VSQRTPSZ128rk:
  case X86::VSQRTPSZ128rkz:
  case X86::VSQRTPSZ256m:
  case X86::VSQRTPSZ256mb:
  case X86::VSQRTPSZ256mbk:
  case X86::VSQRTPSZ256mbkz:
  case X86::VSQRTPSZ256mk:
  case X86::VSQRTPSZ256mkz:
  case X86::VSQRTPSZ256r:
  case X86::VSQRTPSZ256rk:
  case X86::VSQRTPSZ256rkz:
  case X86::VSQRTPSZm:
  case X86::VSQRTPSZmb:
  case X86::VSQRTPSZmbk:
  case X86::VSQRTPSZmbkz:
  case X86::VSQRTPSZmk:
  case X86::VSQRTPSZmkz:
  case X86::VSQRTPSZr:
  case X86::VSQRTPSZrb:
  case X86::VSQRTPSZrbk:
  case X86::VSQRTPSZrbkz:
  case X86::VSQRTPSZrk:
  case X86::VSQRTPSZrkz:
  case X86::VSQRTSDZm:
  case X86::VSQRTSDZm_Int:
  case X86::VSQRTSDZm_Intk:
  case X86::VSQRTSDZm_Intkz:
  case X86::VSQRTSDZr:
  case X86::VSQRTSDZr_Int:
  case X86::VSQRTSDZr_Intk:
  case X86::VSQRTSDZr_Intkz:
  case X86::VSQRTSDZrb_Int:
  case X86::VSQRTSDZrb_Intk:
  case X86::VSQRTSDZrb_Intkz:
  case X86::VSQRTSSZm:
  case X86::VSQRTSSZm_Int:
  case X86::VSQRTSSZm_Intk:
  case X86::VSQRTSSZm_Intkz:
  case X86::VSQRTSSZr:
  case X86::VSQRTSSZr_Int:
  case X86::VSQRTSSZr_Intk:
  case X86::VSQRTSSZr_Intkz:
  case X86::VSQRTSSZrb_Int:
  case X86::VSQRTSSZrb_Intk:
  case X86::VSQRTSSZrb_Intkz:

  case X86::VGATHERDPDYrm:
  case X86::VGATHERDPDZ128rm:
  case X86::VGATHERDPDZ256rm:
  case X86::VGATHERDPDZrm:
  case X86::VGATHERDPDrm:
  case X86::VGATHERDPSYrm:
  case X86::VGATHERDPSZ128rm:
  case X86::VGATHERDPSZ256rm:
  case X86::VGATHERDPSZrm:
  case X86::VGATHERDPSrm:
  case X86::VGATHERPF0DPDm:
  case X86::VGATHERPF0DPSm:
  case X86::VGATHERPF0QPDm:
  case X86::VGATHERPF0QPSm:
  case X86::VGATHERPF1DPDm:
  case X86::VGATHERPF1DPSm:
  case X86::VGATHERPF1QPDm:
  case X86::VGATHERPF1QPSm:
  case X86::VGATHERQPDYrm:
  case X86::VGATHERQPDZ128rm:
  case X86::VGATHERQPDZ256rm:
  case X86::VGATHERQPDZrm:
  case X86::VGATHERQPDrm:
  case X86::VGATHERQPSYrm:
  case X86::VGATHERQPSZ128rm:
  case X86::VGATHERQPSZ256rm:
  case X86::VGATHERQPSZrm:
  case X86::VGATHERQPSrm:
  case X86::VPGATHERDDYrm:
  case X86::VPGATHERDDZ128rm:
  case X86::VPGATHERDDZ256rm:
  case X86::VPGATHERDDZrm:
  case X86::VPGATHERDDrm:
  case X86::VPGATHERDQYrm:
  case X86::VPGATHERDQZ128rm:
  case X86::VPGATHERDQZ256rm:
  case X86::VPGATHERDQZrm:
  case X86::VPGATHERDQrm:
  case X86::VPGATHERQDYrm:
  case X86::VPGATHERQDZ128rm:
  case X86::VPGATHERQDZ256rm:
  case X86::VPGATHERQDZrm:
  case X86::VPGATHERQDrm:
  case X86::VPGATHERQQYrm:
  case X86::VPGATHERQQZ128rm:
  case X86::VPGATHERQQZ256rm:
  case X86::VPGATHERQQZrm:
  case X86::VPGATHERQQrm:
  case X86::VSCATTERDPDZ128mr:
  case X86::VSCATTERDPDZ256mr:
  case X86::VSCATTERDPDZmr:
  case X86::VSCATTERDPSZ128mr:
  case X86::VSCATTERDPSZ256mr:
  case X86::VSCATTERDPSZmr:
  case X86::VSCATTERPF0DPDm:
  case X86::VSCATTERPF0DPSm:
  case X86::VSCATTERPF0QPDm:
  case X86::VSCATTERPF0QPSm:
  case X86::VSCATTERPF1DPDm:
  case X86::VSCATTERPF1DPSm:
  case X86::VSCATTERPF1QPDm:
  case X86::VSCATTERPF1QPSm:
  case X86::VSCATTERQPDZ128mr:
  case X86::VSCATTERQPDZ256mr:
  case X86::VSCATTERQPDZmr:
  case X86::VSCATTERQPSZ128mr:
  case X86::VSCATTERQPSZ256mr:
  case X86::VSCATTERQPSZmr:
  case X86::VPSCATTERDDZ128mr:
  case X86::VPSCATTERDDZ256mr:
  case X86::VPSCATTERDDZmr:
  case X86::VPSCATTERDQZ128mr:
  case X86::VPSCATTERDQZ256mr:
  case X86::VPSCATTERDQZmr:
  case X86::VPSCATTERQDZ128mr:
  case X86::VPSCATTERQDZ256mr:
  case X86::VPSCATTERQDZmr:
  case X86::VPSCATTERQQZ128mr:
  case X86::VPSCATTERQQZ256mr:
  case X86::VPSCATTERQQZmr:
    return true;
  }
}

bool X86InstrInfo::hasHighOperandLatency(const TargetSchedModel &SchedModel,
                                         const MachineRegisterInfo *MRI,
                                         const MachineInstr &DefMI,
                                         unsigned DefIdx,
                                         const MachineInstr &UseMI,
                                         unsigned UseIdx) const {
  return isHighLatencyDef(DefMI.getOpcode());
}

bool X86InstrInfo::hasReassociableOperands(const MachineInstr &Inst,
                                           const MachineBasicBlock *MBB) const {
  assert((Inst.getNumOperands() == 3 || Inst.getNumOperands() == 4) &&
         "Reassociation needs binary operators");

  // Integer binary math/logic instructions have a third source operand:
  // the EFLAGS register. That operand must be both defined here and never
  // used; ie, it must be dead. If the EFLAGS operand is live, then we can
  // not change anything because rearranging the operands could affect other
  // instructions that depend on the exact status flags (zero, sign, etc.)
  // that are set by using these particular operands with this operation.
  if (Inst.getNumOperands() == 4) {
    assert(Inst.getOperand(3).isReg() &&
           Inst.getOperand(3).getReg() == X86::EFLAGS &&
           "Unexpected operand in reassociable instruction");
    if (!Inst.getOperand(3).isDead())
      return false;
  }

  return TargetInstrInfo::hasReassociableOperands(Inst, MBB);
}

// TODO: There are many more machine instruction opcodes to match:
//       1. Other data types (integer, vectors)
//       2. Other math / logic operations (xor, or)
//       3. Other forms of the same operation (intrinsics and other variants)
bool X86InstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst) const {
  switch (Inst.getOpcode()) {
  case X86::AND8rr:
  case X86::AND16rr:
  case X86::AND32rr:
  case X86::AND64rr:
  case X86::OR8rr:
  case X86::OR16rr:
  case X86::OR32rr:
  case X86::OR64rr:
  case X86::XOR8rr:
  case X86::XOR16rr:
  case X86::XOR32rr:
  case X86::XOR64rr:
  case X86::IMUL16rr:
  case X86::IMUL32rr:
  case X86::IMUL64rr:
  case X86::PANDrr:
  case X86::PORrr:
  case X86::PXORrr:
  case X86::ANDPDrr:
  case X86::ANDPSrr:
  case X86::ORPDrr:
  case X86::ORPSrr:
  case X86::XORPDrr:
  case X86::XORPSrr:
  case X86::PADDBrr:
  case X86::PADDWrr:
  case X86::PADDDrr:
  case X86::PADDQrr:
  case X86::VPANDrr:
  case X86::VPANDYrr:
  case X86::VPANDDZ128rr:
  case X86::VPANDDZ256rr:
  case X86::VPANDDZrr:
  case X86::VPANDQZ128rr:
  case X86::VPANDQZ256rr:
  case X86::VPANDQZrr:
  case X86::VPORrr:
  case X86::VPORYrr:
  case X86::VPORDZ128rr:
  case X86::VPORDZ256rr:
  case X86::VPORDZrr:
  case X86::VPORQZ128rr:
  case X86::VPORQZ256rr:
  case X86::VPORQZrr:
  case X86::VPXORrr:
  case X86::VPXORYrr:
  case X86::VPXORDZ128rr:
  case X86::VPXORDZ256rr:
  case X86::VPXORDZrr:
  case X86::VPXORQZ128rr:
  case X86::VPXORQZ256rr:
  case X86::VPXORQZrr:
  case X86::VANDPDrr:
  case X86::VANDPSrr:
  case X86::VANDPDYrr:
  case X86::VANDPSYrr:
  case X86::VANDPDZ128rr:
  case X86::VANDPSZ128rr:
  case X86::VANDPDZ256rr:
  case X86::VANDPSZ256rr:
  case X86::VANDPDZrr:
  case X86::VANDPSZrr:
  case X86::VORPDrr:
  case X86::VORPSrr:
  case X86::VORPDYrr:
  case X86::VORPSYrr:
  case X86::VORPDZ128rr:
  case X86::VORPSZ128rr:
  case X86::VORPDZ256rr:
  case X86::VORPSZ256rr:
  case X86::VORPDZrr:
  case X86::VORPSZrr:
  case X86::VXORPDrr:
  case X86::VXORPSrr:
  case X86::VXORPDYrr:
  case X86::VXORPSYrr:
  case X86::VXORPDZ128rr:
  case X86::VXORPSZ128rr:
  case X86::VXORPDZ256rr:
  case X86::VXORPSZ256rr:
  case X86::VXORPDZrr:
  case X86::VXORPSZrr:
  case X86::KADDBrr:
  case X86::KADDWrr:
  case X86::KADDDrr:
  case X86::KADDQrr:
  case X86::KANDBrr:
  case X86::KANDWrr:
  case X86::KANDDrr:
  case X86::KANDQrr:
  case X86::KORBrr:
  case X86::KORWrr:
  case X86::KORDrr:
  case X86::KORQrr:
  case X86::KXORBrr:
  case X86::KXORWrr:
  case X86::KXORDrr:
  case X86::KXORQrr:
  case X86::VPADDBrr:
  case X86::VPADDWrr:
  case X86::VPADDDrr:
  case X86::VPADDQrr:
  case X86::VPADDBYrr:
  case X86::VPADDWYrr:
  case X86::VPADDDYrr:
  case X86::VPADDQYrr:
  case X86::VPADDBZ128rr:
  case X86::VPADDWZ128rr:
  case X86::VPADDDZ128rr:
  case X86::VPADDQZ128rr:
  case X86::VPADDBZ256rr:
  case X86::VPADDWZ256rr:
  case X86::VPADDDZ256rr:
  case X86::VPADDQZ256rr:
  case X86::VPADDBZrr:
  case X86::VPADDWZrr:
  case X86::VPADDDZrr:
  case X86::VPADDQZrr:
  case X86::VPMULLWrr:
  case X86::VPMULLWYrr:
  case X86::VPMULLWZ128rr:
  case X86::VPMULLWZ256rr:
  case X86::VPMULLWZrr:
  case X86::VPMULLDrr:
  case X86::VPMULLDYrr:
  case X86::VPMULLDZ128rr:
  case X86::VPMULLDZ256rr:
  case X86::VPMULLDZrr:
  case X86::VPMULLQZ128rr:
  case X86::VPMULLQZ256rr:
  case X86::VPMULLQZrr:
  // Normal min/max instructions are not commutative because of NaN and signed
  // zero semantics, but these are. Thus, there's no need to check for global
  // relaxed math; the instructions themselves have the properties we need.
  case X86::MAXCPDrr:
  case X86::MAXCPSrr:
  case X86::MAXCSDrr:
  case X86::MAXCSSrr:
  case X86::MINCPDrr:
  case X86::MINCPSrr:
  case X86::MINCSDrr:
  case X86::MINCSSrr:
  case X86::VMAXCPDrr:
  case X86::VMAXCPSrr:
  case X86::VMAXCPDYrr:
  case X86::VMAXCPSYrr:
  case X86::VMAXCPDZ128rr:
  case X86::VMAXCPSZ128rr:
  case X86::VMAXCPDZ256rr:
  case X86::VMAXCPSZ256rr:
  case X86::VMAXCPDZrr:
  case X86::VMAXCPSZrr:
  case X86::VMAXCSDrr:
  case X86::VMAXCSSrr:
  case X86::VMAXCSDZrr:
  case X86::VMAXCSSZrr:
  case X86::VMINCPDrr:
  case X86::VMINCPSrr:
  case X86::VMINCPDYrr:
  case X86::VMINCPSYrr:
  case X86::VMINCPDZ128rr:
  case X86::VMINCPSZ128rr:
  case X86::VMINCPDZ256rr:
  case X86::VMINCPSZ256rr:
  case X86::VMINCPDZrr:
  case X86::VMINCPSZrr:
  case X86::VMINCSDrr:
  case X86::VMINCSSrr:
  case X86::VMINCSDZrr:
  case X86::VMINCSSZrr:
    return true;
  case X86::ADDPDrr:
  case X86::ADDPSrr:
  case X86::ADDSDrr:
  case X86::ADDSSrr:
  case X86::MULPDrr:
  case X86::MULPSrr:
  case X86::MULSDrr:
  case X86::MULSSrr:
  case X86::VADDPDrr:
  case X86::VADDPSrr:
  case X86::VADDPDYrr:
  case X86::VADDPSYrr:
  case X86::VADDPDZ128rr:
  case X86::VADDPSZ128rr:
  case X86::VADDPDZ256rr:
  case X86::VADDPSZ256rr:
  case X86::VADDPDZrr:
  case X86::VADDPSZrr:
  case X86::VADDSDrr:
  case X86::VADDSSrr:
  case X86::VADDSDZrr:
  case X86::VADDSSZrr:
  case X86::VMULPDrr:
  case X86::VMULPSrr:
  case X86::VMULPDYrr:
  case X86::VMULPSYrr:
  case X86::VMULPDZ128rr:
  case X86::VMULPSZ128rr:
  case X86::VMULPDZ256rr:
  case X86::VMULPSZ256rr:
  case X86::VMULPDZrr:
  case X86::VMULPSZrr:
  case X86::VMULSDrr:
  case X86::VMULSSrr:
  case X86::VMULSDZrr:
  case X86::VMULSSZrr:
    return Inst.getParent()->getParent()->getTarget().Options.UnsafeFPMath;
  default:
    return false;
  }
}

/// This is an architecture-specific helper function of reassociateOps.
/// Set special operand attributes for new instructions after reassociation.
void X86InstrInfo::setSpecialOperandAttr(MachineInstr &OldMI1,
                                         MachineInstr &OldMI2,
                                         MachineInstr &NewMI1,
                                         MachineInstr &NewMI2) const {
  // Integer instructions define an implicit EFLAGS source register operand as
  // the third source (fourth total) operand.
  if (OldMI1.getNumOperands() != 4 || OldMI2.getNumOperands() != 4)
    return;

  assert(NewMI1.getNumOperands() == 4 && NewMI2.getNumOperands() == 4 &&
         "Unexpected instruction type for reassociation");

  MachineOperand &OldOp1 = OldMI1.getOperand(3);
  MachineOperand &OldOp2 = OldMI2.getOperand(3);
  MachineOperand &NewOp1 = NewMI1.getOperand(3);
  MachineOperand &NewOp2 = NewMI2.getOperand(3);

  assert(OldOp1.isReg() && OldOp1.getReg() == X86::EFLAGS && OldOp1.isDead() &&
         "Must have dead EFLAGS operand in reassociable instruction");
  assert(OldOp2.isReg() && OldOp2.getReg() == X86::EFLAGS && OldOp2.isDead() &&
         "Must have dead EFLAGS operand in reassociable instruction");

  (void)OldOp1;
  (void)OldOp2;

  assert(NewOp1.isReg() && NewOp1.getReg() == X86::EFLAGS &&
         "Unexpected operand in reassociable instruction");
  assert(NewOp2.isReg() && NewOp2.getReg() == X86::EFLAGS &&
         "Unexpected operand in reassociable instruction");

  // Mark the new EFLAGS operands as dead to be helpful to subsequent iterations
  // of this pass or other passes. The EFLAGS operands must be dead in these new
  // instructions because the EFLAGS operands in the original instructions must
  // be dead in order for reassociation to occur.
  NewOp1.setIsDead();
  NewOp2.setIsDead();
}

std::pair<unsigned, unsigned>
X86InstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::make_pair(TF, 0u);
}

ArrayRef<std::pair<unsigned, const char *>>
X86InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace X86II;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_GOT_ABSOLUTE_ADDRESS, "x86-got-absolute-address"},
      {MO_PIC_BASE_OFFSET, "x86-pic-base-offset"},
      {MO_GOT, "x86-got"},
      {MO_GOTOFF, "x86-gotoff"},
      {MO_GOTPCREL, "x86-gotpcrel"},
      {MO_PLT, "x86-plt"},
      {MO_TLSGD, "x86-tlsgd"},
      {MO_TLSLD, "x86-tlsld"},
      {MO_TLSLDM, "x86-tlsldm"},
      {MO_GOTTPOFF, "x86-gottpoff"},
      {MO_INDNTPOFF, "x86-indntpoff"},
      {MO_TPOFF, "x86-tpoff"},
      {MO_DTPOFF, "x86-dtpoff"},
      {MO_NTPOFF, "x86-ntpoff"},
      {MO_GOTNTPOFF, "x86-gotntpoff"},
      {MO_DLLIMPORT, "x86-dllimport"},
      {MO_DARWIN_NONLAZY, "x86-darwin-nonlazy"},
      {MO_DARWIN_NONLAZY_PIC_BASE, "x86-darwin-nonlazy-pic-base"},
      {MO_TLVP, "x86-tlvp"},
      {MO_TLVP_PIC_BASE, "x86-tlvp-pic-base"},
      {MO_SECREL, "x86-secrel"},
      {MO_COFFSTUB, "x86-coffstub"}};
  return makeArrayRef(TargetFlags);
}

namespace {
  /// Create Global Base Reg pass. This initializes the PIC
  /// global base register for x86-32.
  struct CGBR : public MachineFunctionPass {
    static char ID;
    CGBR() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override {
      const X86TargetMachine *TM =
        static_cast<const X86TargetMachine *>(&MF.getTarget());
      const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();

      // Don't do anything in the 64-bit small and kernel code models. They use
      // RIP-relative addressing for everything.
      if (STI.is64Bit() && (TM->getCodeModel() == CodeModel::Small ||
                            TM->getCodeModel() == CodeModel::Kernel))
        return false;

      // Only emit a global base reg in PIC mode.
      if (!TM->isPositionIndependent())
        return false;

      X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
      unsigned GlobalBaseReg = X86FI->getGlobalBaseReg();

      // If we didn't need a GlobalBaseReg, don't insert code.
      if (GlobalBaseReg == 0)
        return false;

      // Insert the set of GlobalBaseReg into the first MBB of the function
      MachineBasicBlock &FirstMBB = MF.front();
      MachineBasicBlock::iterator MBBI = FirstMBB.begin();
      DebugLoc DL = FirstMBB.findDebugLoc(MBBI);
      MachineRegisterInfo &RegInfo = MF.getRegInfo();
      const X86InstrInfo *TII = STI.getInstrInfo();

      unsigned PC;
      if (STI.isPICStyleGOT())
        PC = RegInfo.createVirtualRegister(&X86::GR32RegClass);
      else
        PC = GlobalBaseReg;

      if (STI.is64Bit()) {
        if (TM->getCodeModel() == CodeModel::Medium) {
          // In the medium code model, use a RIP-relative LEA to materialize the
          // GOT.
          BuildMI(FirstMBB, MBBI, DL, TII->get(X86::LEA64r), PC)
              .addReg(X86::RIP)
              .addImm(0)
              .addReg(0)
              .addExternalSymbol("_GLOBAL_OFFSET_TABLE_")
              .addReg(0);
        } else if (TM->getCodeModel() == CodeModel::Large) {
          // In the large code model, we are aiming for this code, though the
          // register allocation may vary:
          //   leaq .LN$pb(%rip), %rax
          //   movq $_GLOBAL_OFFSET_TABLE_ - .LN$pb, %rcx
          //   addq %rcx, %rax
          // RAX now holds address of _GLOBAL_OFFSET_TABLE_.
          unsigned PBReg = RegInfo.createVirtualRegister(&X86::GR64RegClass);
          unsigned GOTReg =
              RegInfo.createVirtualRegister(&X86::GR64RegClass);
          BuildMI(FirstMBB, MBBI, DL, TII->get(X86::LEA64r), PBReg)
              .addReg(X86::RIP)
              .addImm(0)
              .addReg(0)
              .addSym(MF.getPICBaseSymbol())
              .addReg(0);
          std::prev(MBBI)->setPreInstrSymbol(MF, MF.getPICBaseSymbol());
          BuildMI(FirstMBB, MBBI, DL, TII->get(X86::MOV64ri), GOTReg)
              .addExternalSymbol("_GLOBAL_OFFSET_TABLE_",
                                 X86II::MO_PIC_BASE_OFFSET);
          BuildMI(FirstMBB, MBBI, DL, TII->get(X86::ADD64rr), PC)
              .addReg(PBReg, RegState::Kill)
              .addReg(GOTReg, RegState::Kill);
        } else {
          llvm_unreachable("unexpected code model");
        }
      } else {
        // Operand of MovePCtoStack is completely ignored by asm printer. It's
        // only used in JIT code emission as displacement to pc.
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::MOVPC32r), PC).addImm(0);

        // If we're using vanilla 'GOT' PIC style, we should use relative
        // addressing not to pc, but to _GLOBAL_OFFSET_TABLE_ external.
        if (STI.isPICStyleGOT()) {
          // Generate addl $__GLOBAL_OFFSET_TABLE_ + [.-piclabel],
          // %some_register
          BuildMI(FirstMBB, MBBI, DL, TII->get(X86::ADD32ri), GlobalBaseReg)
              .addReg(PC)
              .addExternalSymbol("_GLOBAL_OFFSET_TABLE_",
                                 X86II::MO_GOT_ABSOLUTE_ADDRESS);
        }
      }

      return true;
    }

    StringRef getPassName() const override {
      return "X86 PIC Global Base Reg Initialization";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

char CGBR::ID = 0;
FunctionPass*
llvm::createX86GlobalBaseRegPass() { return new CGBR(); }

namespace {
  struct LDTLSCleanup : public MachineFunctionPass {
    static char ID;
    LDTLSCleanup() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override {
      if (skipFunction(MF.getFunction()))
        return false;

      X86MachineFunctionInfo *MFI = MF.getInfo<X86MachineFunctionInfo>();
      if (MFI->getNumLocalDynamicTLSAccesses() < 2) {
        // No point folding accesses if there isn't at least two.
        return false;
      }

      MachineDominatorTree *DT = &getAnalysis<MachineDominatorTree>();
      return VisitNode(DT->getRootNode(), 0);
    }

    // Visit the dominator subtree rooted at Node in pre-order.
    // If TLSBaseAddrReg is non-null, then use that to replace any
    // TLS_base_addr instructions. Otherwise, create the register
    // when the first such instruction is seen, and then use it
    // as we encounter more instructions.
    bool VisitNode(MachineDomTreeNode *Node, unsigned TLSBaseAddrReg) {
      MachineBasicBlock *BB = Node->getBlock();
      bool Changed = false;

      // Traverse the current block.
      for (MachineBasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;
           ++I) {
        switch (I->getOpcode()) {
          case X86::TLS_base_addr32:
          case X86::TLS_base_addr64:
            if (TLSBaseAddrReg)
              I = ReplaceTLSBaseAddrCall(*I, TLSBaseAddrReg);
            else
              I = SetRegister(*I, &TLSBaseAddrReg);
            Changed = true;
            break;
          default:
            break;
        }
      }

      // Visit the children of this block in the dominator tree.
      for (MachineDomTreeNode::iterator I = Node->begin(), E = Node->end();
           I != E; ++I) {
        Changed |= VisitNode(*I, TLSBaseAddrReg);
      }

      return Changed;
    }

    // Replace the TLS_base_addr instruction I with a copy from
    // TLSBaseAddrReg, returning the new instruction.
    MachineInstr *ReplaceTLSBaseAddrCall(MachineInstr &I,
                                         unsigned TLSBaseAddrReg) {
      MachineFunction *MF = I.getParent()->getParent();
      const X86Subtarget &STI = MF->getSubtarget<X86Subtarget>();
      const bool is64Bit = STI.is64Bit();
      const X86InstrInfo *TII = STI.getInstrInfo();

      // Insert a Copy from TLSBaseAddrReg to RAX/EAX.
      MachineInstr *Copy =
          BuildMI(*I.getParent(), I, I.getDebugLoc(),
                  TII->get(TargetOpcode::COPY), is64Bit ? X86::RAX : X86::EAX)
              .addReg(TLSBaseAddrReg);

      // Erase the TLS_base_addr instruction.
      I.eraseFromParent();

      return Copy;
    }

    // Create a virtual register in *TLSBaseAddrReg, and populate it by
    // inserting a copy instruction after I. Returns the new instruction.
    MachineInstr *SetRegister(MachineInstr &I, unsigned *TLSBaseAddrReg) {
      MachineFunction *MF = I.getParent()->getParent();
      const X86Subtarget &STI = MF->getSubtarget<X86Subtarget>();
      const bool is64Bit = STI.is64Bit();
      const X86InstrInfo *TII = STI.getInstrInfo();

      // Create a virtual register for the TLS base address.
      MachineRegisterInfo &RegInfo = MF->getRegInfo();
      *TLSBaseAddrReg = RegInfo.createVirtualRegister(is64Bit
                                                      ? &X86::GR64RegClass
                                                      : &X86::GR32RegClass);

      // Insert a copy from RAX/EAX to TLSBaseAddrReg.
      MachineInstr *Next = I.getNextNode();
      MachineInstr *Copy =
          BuildMI(*I.getParent(), Next, I.getDebugLoc(),
                  TII->get(TargetOpcode::COPY), *TLSBaseAddrReg)
              .addReg(is64Bit ? X86::RAX : X86::EAX);

      return Copy;
    }

    StringRef getPassName() const override {
      return "Local Dynamic TLS Access Clean-up";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

char LDTLSCleanup::ID = 0;
FunctionPass*
llvm::createCleanupLocalDynamicTLSPass() { return new LDTLSCleanup(); }

/// Constants defining how certain sequences should be outlined.
///
/// \p MachineOutlinerDefault implies that the function is called with a call
/// instruction, and a return must be emitted for the outlined function frame.
///
/// That is,
///
/// I1                                 OUTLINED_FUNCTION:
/// I2 --> call OUTLINED_FUNCTION       I1
/// I3                                  I2
///                                     I3
///                                     ret
///
/// * Call construction overhead: 1 (call instruction)
/// * Frame construction overhead: 1 (return instruction)
///
/// \p MachineOutlinerTailCall implies that the function is being tail called.
/// A jump is emitted instead of a call, and the return is already present in
/// the outlined sequence. That is,
///
/// I1                                 OUTLINED_FUNCTION:
/// I2 --> jmp OUTLINED_FUNCTION       I1
/// ret                                I2
///                                    ret
///
/// * Call construction overhead: 1 (jump instruction)
/// * Frame construction overhead: 0 (don't need to return)
///
enum MachineOutlinerClass {
  MachineOutlinerDefault,
  MachineOutlinerTailCall
};

outliner::OutlinedFunction X86InstrInfo::getOutliningCandidateInfo(
    std::vector<outliner::Candidate> &RepeatedSequenceLocs) const {
  unsigned SequenceSize =
      std::accumulate(RepeatedSequenceLocs[0].front(),
                      std::next(RepeatedSequenceLocs[0].back()), 0,
                      [](unsigned Sum, const MachineInstr &MI) {
                        // FIXME: x86 doesn't implement getInstSizeInBytes, so
                        // we can't tell the cost.  Just assume each instruction
                        // is one byte.
                        if (MI.isDebugInstr() || MI.isKill())
                          return Sum;
                        return Sum + 1;
                      });

  // FIXME: Use real size in bytes for call and ret instructions.
  if (RepeatedSequenceLocs[0].back()->isTerminator()) {
    for (outliner::Candidate &C : RepeatedSequenceLocs)
      C.setCallInfo(MachineOutlinerTailCall, 1);

    return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize,
                                      0, // Number of bytes to emit frame.
                                      MachineOutlinerTailCall // Type of frame.
    );
  }

  for (outliner::Candidate &C : RepeatedSequenceLocs)
    C.setCallInfo(MachineOutlinerDefault, 1);

  return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize, 1,
                                    MachineOutlinerDefault);
}

bool X86InstrInfo::isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                           bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();

  // Does the function use a red zone? If it does, then we can't risk messing
  // with the stack.
  if (!F.hasFnAttribute(Attribute::NoRedZone)) {
    // It could have a red zone. If it does, then we don't want to touch it.
    const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
    if (!X86FI || X86FI->getUsesRedZone())
      return false;
  }

  // If we *don't* want to outline from things that could potentially be deduped
  // then return false.
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
      return false;

  // This function is viable for outlining, so return true.
  return true;
}

outliner::InstrType
X86InstrInfo::getOutliningType(MachineBasicBlock::iterator &MIT,  unsigned Flags) const {
  MachineInstr &MI = *MIT;
  // Don't allow debug values to impact outlining type.
  if (MI.isDebugInstr() || MI.isIndirectDebugValue())
    return outliner::InstrType::Invisible;

  // At this point, KILL instructions don't really tell us much so we can go
  // ahead and skip over them.
  if (MI.isKill())
    return outliner::InstrType::Invisible;

  // Is this a tail call? If yes, we can outline as a tail call.
  if (isTailCall(MI))
    return outliner::InstrType::Legal;

  // Is this the terminator of a basic block?
  if (MI.isTerminator() || MI.isReturn()) {

    // Does its parent have any successors in its MachineFunction?
    if (MI.getParent()->succ_empty())
      return outliner::InstrType::Legal;

    // It does, so we can't tail call it.
    return outliner::InstrType::Illegal;
  }

  // Don't outline anything that modifies or reads from the stack pointer.
  //
  // FIXME: There are instructions which are being manually built without
  // explicit uses/defs so we also have to check the MCInstrDesc. We should be
  // able to remove the extra checks once those are fixed up. For example,
  // sometimes we might get something like %rax = POP64r 1. This won't be
  // caught by modifiesRegister or readsRegister even though the instruction
  // really ought to be formed so that modifiesRegister/readsRegister would
  // catch it.
  if (MI.modifiesRegister(X86::RSP, &RI) || MI.readsRegister(X86::RSP, &RI) ||
      MI.getDesc().hasImplicitUseOfPhysReg(X86::RSP) ||
      MI.getDesc().hasImplicitDefOfPhysReg(X86::RSP))
    return outliner::InstrType::Illegal;

  // Outlined calls change the instruction pointer, so don't read from it.
  if (MI.readsRegister(X86::RIP, &RI) ||
      MI.getDesc().hasImplicitUseOfPhysReg(X86::RIP) ||
      MI.getDesc().hasImplicitDefOfPhysReg(X86::RIP))
    return outliner::InstrType::Illegal;

  // Positions can't safely be outlined.
  if (MI.isPosition())
    return outliner::InstrType::Illegal;

  // Make sure none of the operands of this instruction do anything tricky.
  for (const MachineOperand &MOP : MI.operands())
    if (MOP.isCPI() || MOP.isJTI() || MOP.isCFIIndex() || MOP.isFI() ||
        MOP.isTargetIndex())
      return outliner::InstrType::Illegal;

  return outliner::InstrType::Legal;
}

void X86InstrInfo::buildOutlinedFrame(MachineBasicBlock &MBB,
                                          MachineFunction &MF,
                                          const outliner::OutlinedFunction &OF)
                                          const {
  // If we're a tail call, we already have a return, so don't do anything.
  if (OF.FrameConstructionID == MachineOutlinerTailCall)
    return;

  // We're a normal call, so our sequence doesn't have a return instruction.
  // Add it in.
  MachineInstr *retq = BuildMI(MF, DebugLoc(), get(X86::RETQ));
  MBB.insert(MBB.end(), retq);
}

MachineBasicBlock::iterator
X86InstrInfo::insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator &It,
                                 MachineFunction &MF,
                                 const outliner::Candidate &C) const {
  // Is it a tail call?
  if (C.CallConstructionID == MachineOutlinerTailCall) {
    // Yes, just insert a JMP.
    It = MBB.insert(It,
                  BuildMI(MF, DebugLoc(), get(X86::TAILJMPd64))
                      .addGlobalAddress(M.getNamedValue(MF.getName())));
  } else {
    // No, insert a call.
    It = MBB.insert(It,
                  BuildMI(MF, DebugLoc(), get(X86::CALL64pcrel32))
                      .addGlobalAddress(M.getNamedValue(MF.getName())));
  }

  return It;
}

#define GET_INSTRINFO_HELPERS
#include "X86GenInstrInfo.inc"
