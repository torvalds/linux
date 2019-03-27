//===-- PPCRegisterInfo.cpp - PowerPC Register Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "PPCRegisterInfo.h"
#include "PPC.h"
#include "PPCFrameLowering.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

using namespace llvm;

#define DEBUG_TYPE "reginfo"

#define GET_REGINFO_TARGET_DESC
#include "PPCGenRegisterInfo.inc"

STATISTIC(InflateGPRC, "Number of gprc inputs for getLargestLegalClass");
STATISTIC(InflateGP8RC, "Number of g8rc inputs for getLargestLegalClass");

static cl::opt<bool>
EnableBasePointer("ppc-use-base-pointer", cl::Hidden, cl::init(true),
         cl::desc("Enable use of a base pointer for complex stack frames"));

static cl::opt<bool>
AlwaysBasePointer("ppc-always-use-base-pointer", cl::Hidden, cl::init(false),
         cl::desc("Force the use of a base pointer in every function"));

static cl::opt<bool>
EnableGPRToVecSpills("ppc-enable-gpr-to-vsr-spills", cl::Hidden, cl::init(false),
         cl::desc("Enable spills from gpr to vsr rather than stack"));

static cl::opt<bool>
StackPtrConst("ppc-stack-ptr-caller-preserved",
                cl::desc("Consider R1 caller preserved so stack saves of "
                         "caller preserved registers can be LICM candidates"),
                cl::init(true), cl::Hidden);

PPCRegisterInfo::PPCRegisterInfo(const PPCTargetMachine &TM)
  : PPCGenRegisterInfo(TM.isPPC64() ? PPC::LR8 : PPC::LR,
                       TM.isPPC64() ? 0 : 1,
                       TM.isPPC64() ? 0 : 1),
    TM(TM) {
  ImmToIdxMap[PPC::LD]   = PPC::LDX;    ImmToIdxMap[PPC::STD]  = PPC::STDX;
  ImmToIdxMap[PPC::LBZ]  = PPC::LBZX;   ImmToIdxMap[PPC::STB]  = PPC::STBX;
  ImmToIdxMap[PPC::LHZ]  = PPC::LHZX;   ImmToIdxMap[PPC::LHA]  = PPC::LHAX;
  ImmToIdxMap[PPC::LWZ]  = PPC::LWZX;   ImmToIdxMap[PPC::LWA]  = PPC::LWAX;
  ImmToIdxMap[PPC::LFS]  = PPC::LFSX;   ImmToIdxMap[PPC::LFD]  = PPC::LFDX;
  ImmToIdxMap[PPC::STH]  = PPC::STHX;   ImmToIdxMap[PPC::STW]  = PPC::STWX;
  ImmToIdxMap[PPC::STFS] = PPC::STFSX;  ImmToIdxMap[PPC::STFD] = PPC::STFDX;
  ImmToIdxMap[PPC::ADDI] = PPC::ADD4;
  ImmToIdxMap[PPC::LWA_32] = PPC::LWAX_32;

  // 64-bit
  ImmToIdxMap[PPC::LHA8] = PPC::LHAX8; ImmToIdxMap[PPC::LBZ8] = PPC::LBZX8;
  ImmToIdxMap[PPC::LHZ8] = PPC::LHZX8; ImmToIdxMap[PPC::LWZ8] = PPC::LWZX8;
  ImmToIdxMap[PPC::STB8] = PPC::STBX8; ImmToIdxMap[PPC::STH8] = PPC::STHX8;
  ImmToIdxMap[PPC::STW8] = PPC::STWX8; ImmToIdxMap[PPC::STDU] = PPC::STDUX;
  ImmToIdxMap[PPC::ADDI8] = PPC::ADD8;

  // VSX
  ImmToIdxMap[PPC::DFLOADf32] = PPC::LXSSPX;
  ImmToIdxMap[PPC::DFLOADf64] = PPC::LXSDX;
  ImmToIdxMap[PPC::SPILLTOVSR_LD] = PPC::SPILLTOVSR_LDX;
  ImmToIdxMap[PPC::SPILLTOVSR_ST] = PPC::SPILLTOVSR_STX;
  ImmToIdxMap[PPC::DFSTOREf32] = PPC::STXSSPX;
  ImmToIdxMap[PPC::DFSTOREf64] = PPC::STXSDX;
  ImmToIdxMap[PPC::LXV] = PPC::LXVX;
  ImmToIdxMap[PPC::LXSD] = PPC::LXSDX;
  ImmToIdxMap[PPC::LXSSP] = PPC::LXSSPX;
  ImmToIdxMap[PPC::STXV] = PPC::STXVX;
  ImmToIdxMap[PPC::STXSD] = PPC::STXSDX;
  ImmToIdxMap[PPC::STXSSP] = PPC::STXSSPX;

  // SPE
  ImmToIdxMap[PPC::EVLDD] = PPC::EVLDDX;
  ImmToIdxMap[PPC::EVSTDD] = PPC::EVSTDDX;
  ImmToIdxMap[PPC::SPESTW] = PPC::SPESTWX;
  ImmToIdxMap[PPC::SPELWZ] = PPC::SPELWZX;
}

/// getPointerRegClass - Return the register class to use to hold pointers.
/// This is used for addressing modes.
const TargetRegisterClass *
PPCRegisterInfo::getPointerRegClass(const MachineFunction &MF, unsigned Kind)
                                                                       const {
  // Note that PPCInstrInfo::FoldImmediate also directly uses this Kind value
  // when it checks for ZERO folding.
  if (Kind == 1) {
    if (TM.isPPC64())
      return &PPC::G8RC_NOX0RegClass;
    return &PPC::GPRC_NOR0RegClass;
  }

  if (TM.isPPC64())
    return &PPC::G8RCRegClass;
  return &PPC::GPRCRegClass;
}

const MCPhysReg*
PPCRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const PPCSubtarget &Subtarget = MF->getSubtarget<PPCSubtarget>();
  if (MF->getFunction().getCallingConv() == CallingConv::AnyReg) {
    if (Subtarget.hasVSX())
      return CSR_64_AllRegs_VSX_SaveList;
    if (Subtarget.hasAltivec())
      return CSR_64_AllRegs_Altivec_SaveList;
    return CSR_64_AllRegs_SaveList;
  }

  if (Subtarget.isDarwinABI())
    return TM.isPPC64()
               ? (Subtarget.hasAltivec() ? CSR_Darwin64_Altivec_SaveList
                                         : CSR_Darwin64_SaveList)
               : (Subtarget.hasAltivec() ? CSR_Darwin32_Altivec_SaveList
                                         : CSR_Darwin32_SaveList);

  if (TM.isPPC64() && MF->getInfo<PPCFunctionInfo>()->isSplitCSR())
    return CSR_SRV464_TLS_PE_SaveList;

  if (Subtarget.hasSPE())
    return CSR_SVR432_SPE_SaveList;

  // On PPC64, we might need to save r2 (but only if it is not reserved).
  bool SaveR2 = MF->getRegInfo().isAllocatable(PPC::X2);

  if (MF->getFunction().getCallingConv() == CallingConv::Cold) {
    return TM.isPPC64()
               ? (Subtarget.hasAltivec()
                      ? (SaveR2 ? CSR_SVR64_ColdCC_R2_Altivec_SaveList
                                : CSR_SVR64_ColdCC_Altivec_SaveList)
                      : (SaveR2 ? CSR_SVR64_ColdCC_R2_SaveList
                                : CSR_SVR64_ColdCC_SaveList))
               : (Subtarget.hasAltivec() ? CSR_SVR32_ColdCC_Altivec_SaveList
                                         : CSR_SVR32_ColdCC_SaveList);
  }

  return TM.isPPC64()
             ? (Subtarget.hasAltivec()
                    ? (SaveR2 ? CSR_SVR464_R2_Altivec_SaveList
                              : CSR_SVR464_Altivec_SaveList)
                    : (SaveR2 ? CSR_SVR464_R2_SaveList : CSR_SVR464_SaveList))
             : (Subtarget.hasAltivec() ? CSR_SVR432_Altivec_SaveList
                                       : CSR_SVR432_SaveList);
}

const MCPhysReg *
PPCRegisterInfo::getCalleeSavedRegsViaCopy(const MachineFunction *MF) const {
  assert(MF && "Invalid MachineFunction pointer.");
  const PPCSubtarget &Subtarget = MF->getSubtarget<PPCSubtarget>();
  if (Subtarget.isDarwinABI())
    return nullptr;
  if (!TM.isPPC64())
    return nullptr;
  if (MF->getFunction().getCallingConv() != CallingConv::CXX_FAST_TLS)
    return nullptr;
  if (!MF->getInfo<PPCFunctionInfo>()->isSplitCSR())
    return nullptr;

  // On PPC64, we might need to save r2 (but only if it is not reserved).
  bool SaveR2 = !getReservedRegs(*MF).test(PPC::X2);
  if (Subtarget.hasAltivec())
    return SaveR2
      ? CSR_SVR464_R2_Altivec_ViaCopy_SaveList
      : CSR_SVR464_Altivec_ViaCopy_SaveList;
  else
    return SaveR2
      ? CSR_SVR464_R2_ViaCopy_SaveList
      : CSR_SVR464_ViaCopy_SaveList;
}

const uint32_t *
PPCRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  if (CC == CallingConv::AnyReg) {
    if (Subtarget.hasVSX())
      return CSR_64_AllRegs_VSX_RegMask;
    if (Subtarget.hasAltivec())
      return CSR_64_AllRegs_Altivec_RegMask;
    return CSR_64_AllRegs_RegMask;
  }

  if (Subtarget.isDarwinABI())
    return TM.isPPC64() ? (Subtarget.hasAltivec() ? CSR_Darwin64_Altivec_RegMask
                                                  : CSR_Darwin64_RegMask)
                        : (Subtarget.hasAltivec() ? CSR_Darwin32_Altivec_RegMask
                                                  : CSR_Darwin32_RegMask);

  if (CC == CallingConv::Cold) {
    return TM.isPPC64() ? (Subtarget.hasAltivec() ? CSR_SVR64_ColdCC_Altivec_RegMask
                                                  : CSR_SVR64_ColdCC_RegMask)
                        : (Subtarget.hasAltivec() ? CSR_SVR32_ColdCC_Altivec_RegMask
                                                  : CSR_SVR32_ColdCC_RegMask);
  }

  return TM.isPPC64() ? (Subtarget.hasAltivec() ? CSR_SVR464_Altivec_RegMask
                                                : CSR_SVR464_RegMask)
                      : (Subtarget.hasAltivec() ? CSR_SVR432_Altivec_RegMask
                                                : CSR_SVR432_RegMask);
}

const uint32_t*
PPCRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

void PPCRegisterInfo::adjustStackMapLiveOutMask(uint32_t *Mask) const {
  for (unsigned PseudoReg : {PPC::ZERO, PPC::ZERO8, PPC::RM})
    Mask[PseudoReg / 32] &= ~(1u << (PseudoReg % 32));
}

BitVector PPCRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const PPCFrameLowering *TFI = getFrameLowering(MF);

  // The ZERO register is not really a register, but the representation of r0
  // when used in instructions that treat r0 as the constant 0.
  markSuperRegs(Reserved, PPC::ZERO);

  // The FP register is also not really a register, but is the representation
  // of the frame pointer register used by ISD::FRAMEADDR.
  markSuperRegs(Reserved, PPC::FP);

  // The BP register is also not really a register, but is the representation
  // of the base pointer register used by setjmp.
  markSuperRegs(Reserved, PPC::BP);

  // The counter registers must be reserved so that counter-based loops can
  // be correctly formed (and the mtctr instructions are not DCE'd).
  markSuperRegs(Reserved, PPC::CTR);
  markSuperRegs(Reserved, PPC::CTR8);

  markSuperRegs(Reserved, PPC::R1);
  markSuperRegs(Reserved, PPC::LR);
  markSuperRegs(Reserved, PPC::LR8);
  markSuperRegs(Reserved, PPC::RM);

  if (!Subtarget.isDarwinABI() || !Subtarget.hasAltivec())
    markSuperRegs(Reserved, PPC::VRSAVE);

  // The SVR4 ABI reserves r2 and r13
  if (Subtarget.isSVR4ABI()) {
    // We only reserve r2 if we need to use the TOC pointer. If we have no
    // explicit uses of the TOC pointer (meaning we're a leaf function with
    // no constant-pool loads, etc.) and we have no potential uses inside an
    // inline asm block, then we can treat r2 has an ordinary callee-saved
    // register.
    const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
    if (!TM.isPPC64() || FuncInfo->usesTOCBasePtr() || MF.hasInlineAsm())
      markSuperRegs(Reserved, PPC::R2);  // System-reserved register
    markSuperRegs(Reserved, PPC::R13); // Small Data Area pointer register
  }

  // On PPC64, r13 is the thread pointer. Never allocate this register.
  if (TM.isPPC64())
    markSuperRegs(Reserved, PPC::R13);

  if (TFI->needsFP(MF))
    markSuperRegs(Reserved, PPC::R31);

  bool IsPositionIndependent = TM.isPositionIndependent();
  if (hasBasePointer(MF)) {
    if (Subtarget.isSVR4ABI() && !TM.isPPC64() && IsPositionIndependent)
      markSuperRegs(Reserved, PPC::R29);
    else
      markSuperRegs(Reserved, PPC::R30);
  }

  if (Subtarget.isSVR4ABI() && !TM.isPPC64() && IsPositionIndependent)
    markSuperRegs(Reserved, PPC::R30);

  // Reserve Altivec registers when Altivec is unavailable.
  if (!Subtarget.hasAltivec())
    for (TargetRegisterClass::iterator I = PPC::VRRCRegClass.begin(),
         IE = PPC::VRRCRegClass.end(); I != IE; ++I)
      markSuperRegs(Reserved, *I);

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

bool PPCRegisterInfo::isCallerPreservedPhysReg(unsigned PhysReg,
                                               const MachineFunction &MF) const {
  assert(TargetRegisterInfo::isPhysicalRegister(PhysReg));
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!TM.isPPC64())
    return false;

  if (!Subtarget.isSVR4ABI())
    return false;
  if (PhysReg == PPC::X2)
    // X2 is guaranteed to be preserved within a function if it is reserved.
    // The reason it's reserved is that it's the TOC pointer (and the function
    // uses the TOC). In functions where it isn't reserved (i.e. leaf functions
    // with no TOC access), we can't claim that it is preserved.
    return (getReservedRegs(MF).test(PPC::X2));
  if (StackPtrConst && (PhysReg == PPC::X1) && !MFI.hasVarSizedObjects()
      && !MFI.hasOpaqueSPAdjustment())
    // The value of the stack pointer does not change within a function after
    // the prologue and before the epilogue if there are no dynamic allocations
    // and no inline asm which clobbers X1.
    return true;
  return false;
}

unsigned PPCRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                              MachineFunction &MF) const {
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  const unsigned DefaultSafety = 1;

  switch (RC->getID()) {
  default:
    return 0;
  case PPC::G8RC_NOX0RegClassID:
  case PPC::GPRC_NOR0RegClassID:
  case PPC::SPERCRegClassID:
  case PPC::SPE4RCRegClassID:
  case PPC::G8RCRegClassID:
  case PPC::GPRCRegClassID: {
    unsigned FP = TFI->hasFP(MF) ? 1 : 0;
    return 32 - FP - DefaultSafety;
  }
  case PPC::F8RCRegClassID:
  case PPC::F4RCRegClassID:
  case PPC::QFRCRegClassID:
  case PPC::QSRCRegClassID:
  case PPC::QBRCRegClassID:
  case PPC::VRRCRegClassID:
  case PPC::VFRCRegClassID:
  case PPC::VSLRCRegClassID:
    return 32 - DefaultSafety;
  case PPC::VSRCRegClassID:
  case PPC::VSFRCRegClassID:
  case PPC::VSSRCRegClassID:
    return 64 - DefaultSafety;
  case PPC::CRRCRegClassID:
    return 8 - DefaultSafety;
  }
}

const TargetRegisterClass *
PPCRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  if (Subtarget.hasVSX()) {
    // With VSX, we can inflate various sub-register classes to the full VSX
    // register set.

    // For Power9 we allow the user to enable GPR to vector spills.
    // FIXME: Currently limited to spilling GP8RC. A follow on patch will add
    // support to spill GPRC.
    if (TM.isELFv2ABI()) {
      if (Subtarget.hasP9Vector() && EnableGPRToVecSpills &&
          RC == &PPC::G8RCRegClass) {
        InflateGP8RC++;
        return &PPC::SPILLTOVSRRCRegClass;
      }
      if (RC == &PPC::GPRCRegClass && EnableGPRToVecSpills)
        InflateGPRC++;
    }
    if (RC == &PPC::F8RCRegClass)
      return &PPC::VSFRCRegClass;
    else if (RC == &PPC::VRRCRegClass)
      return &PPC::VSRCRegClass;
    else if (RC == &PPC::F4RCRegClass && Subtarget.hasP8Vector())
      return &PPC::VSSRCRegClass;
  }

  return TargetRegisterInfo::getLargestLegalSuperClass(RC, MF);
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

/// lowerDynamicAlloc - Generate the code for allocating an object in the
/// current frame.  The sequence of code will be in the general form
///
///   addi   R0, SP, \#frameSize ; get the address of the previous frame
///   stwxu  R0, SP, Rnegsize   ; add and update the SP with the negated size
///   addi   Rnew, SP, \#maxCalFrameSize ; get the top of the allocation
///
void PPCRegisterInfo::lowerDynamicAlloc(MachineBasicBlock::iterator II) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  // Determine whether 64-bit pointers are used.
  bool LP64 = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();

  // Get the maximum call stack size.
  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();
  // Get the total frame size.
  unsigned FrameSize = MFI.getStackSize();

  // Get stack alignments.
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  unsigned TargetAlign = TFI->getStackAlignment();
  unsigned MaxAlign = MFI.getMaxAlignment();
  assert((maxCallFrameSize & (MaxAlign-1)) == 0 &&
         "Maximum call-frame size not sufficiently aligned");

  // Determine the previous frame's address.  If FrameSize can't be
  // represented as 16 bits or we need special alignment, then we load the
  // previous frame's address from 0(SP).  Why not do an addis of the hi?
  // Because R0 is our only safe tmp register and addi/addis treat R0 as zero.
  // Constructing the constant and adding would take 3 instructions.
  // Fortunately, a frame greater than 32K is rare.
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

  if (MaxAlign < TargetAlign && isInt<16>(FrameSize)) {
    if (LP64)
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), Reg)
        .addReg(PPC::X31)
        .addImm(FrameSize);
    else
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI), Reg)
        .addReg(PPC::R31)
        .addImm(FrameSize);
  } else if (LP64) {
    BuildMI(MBB, II, dl, TII.get(PPC::LD), Reg)
      .addImm(0)
      .addReg(PPC::X1);
  } else {
    BuildMI(MBB, II, dl, TII.get(PPC::LWZ), Reg)
      .addImm(0)
      .addReg(PPC::R1);
  }

  bool KillNegSizeReg = MI.getOperand(1).isKill();
  unsigned NegSizeReg = MI.getOperand(1).getReg();

  // Grow the stack and update the stack pointer link, then determine the
  // address of new allocated space.
  if (LP64) {
    if (MaxAlign > TargetAlign) {
      unsigned UnalNegSizeReg = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(G8RC);

      // Unfortunately, there is no andi, only andi., and we can't insert that
      // here because we might clobber cr0 while it is live.
      BuildMI(MBB, II, dl, TII.get(PPC::LI8), NegSizeReg)
        .addImm(~(MaxAlign-1));

      unsigned NegSizeReg1 = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(G8RC);
      BuildMI(MBB, II, dl, TII.get(PPC::AND8), NegSizeReg)
        .addReg(UnalNegSizeReg, getKillRegState(KillNegSizeReg))
        .addReg(NegSizeReg1, RegState::Kill);
      KillNegSizeReg = true;
    }

    BuildMI(MBB, II, dl, TII.get(PPC::STDUX), PPC::X1)
      .addReg(Reg, RegState::Kill)
      .addReg(PPC::X1)
      .addReg(NegSizeReg, getKillRegState(KillNegSizeReg));
    BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), MI.getOperand(0).getReg())
      .addReg(PPC::X1)
      .addImm(maxCallFrameSize);
  } else {
    if (MaxAlign > TargetAlign) {
      unsigned UnalNegSizeReg = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(GPRC);

      // Unfortunately, there is no andi, only andi., and we can't insert that
      // here because we might clobber cr0 while it is live.
      BuildMI(MBB, II, dl, TII.get(PPC::LI), NegSizeReg)
        .addImm(~(MaxAlign-1));

      unsigned NegSizeReg1 = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(GPRC);
      BuildMI(MBB, II, dl, TII.get(PPC::AND), NegSizeReg)
        .addReg(UnalNegSizeReg, getKillRegState(KillNegSizeReg))
        .addReg(NegSizeReg1, RegState::Kill);
      KillNegSizeReg = true;
    }

    BuildMI(MBB, II, dl, TII.get(PPC::STWUX), PPC::R1)
      .addReg(Reg, RegState::Kill)
      .addReg(PPC::R1)
      .addReg(NegSizeReg, getKillRegState(KillNegSizeReg));
    BuildMI(MBB, II, dl, TII.get(PPC::ADDI), MI.getOperand(0).getReg())
      .addReg(PPC::R1)
      .addImm(maxCallFrameSize);
  }

  // Discard the DYNALLOC instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerDynamicAreaOffset(
    MachineBasicBlock::iterator II) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();

  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();
  bool is64Bit = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();
  BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LI8 : PPC::LI),
          MI.getOperand(0).getReg())
      .addImm(maxCallFrameSize);
  MBB.erase(II);
}

/// lowerCRSpilling - Generate the code for spilling a CR register. Instead of
/// reserving a whole register (R0), we scrounge for one here. This generates
/// code like this:
///
///   mfcr rA                  ; Move the conditional register into GPR rA.
///   rlwinm rA, rA, SB, 0, 31 ; Shift the bits left so they are in CR0's slot.
///   stw rA, FI               ; Store rA to the frame.
///
void PPCRegisterInfo::lowerCRSpilling(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_CR <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned SrcReg = MI.getOperand(0).getReg();

  // We need to store the CR in the low 4-bits of the saved value. First, issue
  // an MFOCRF to save all of the CRBits and, if needed, kill the SrcReg.
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), Reg)
      .addReg(SrcReg, getKillRegState(MI.getOperand(0).isKill()));

  // If the saved register wasn't CR0, shift the bits left so that they are in
  // CR0's slot.
  if (SrcReg != PPC::CR0) {
    unsigned Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    // rlwinm rA, rA, ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
      .addReg(Reg1, RegState::Kill)
      .addImm(getEncodingValue(SrcReg) * 4)
      .addImm(0)
      .addImm(31);
  }

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::STW8 : PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_CR <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg) &&
    "RESTORE_CR does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LWZ8 : PPC::LWZ),
                              Reg), FrameIndex);

  // If the reloaded register isn't CR0, shift the bits right so that they are
  // in the right CR's slot.
  if (DestReg != PPC::CR0) {
    unsigned Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    unsigned ShiftBits = getEncodingValue(DestReg)*4;
    // rlwinm r11, r11, 32-ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
             .addReg(Reg1, RegState::Kill).addImm(32-ShiftBits).addImm(0)
             .addImm(31);
  }

  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MTOCRF8 : PPC::MTOCRF), DestReg)
             .addReg(Reg, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRBitSpilling(MachineBasicBlock::iterator II,
                                         unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_CRBIT <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned SrcReg = MI.getOperand(0).getReg();

  // We need to move the CR field that contains the CR bit we are spilling.
  // The super register may not be explicitly defined (i.e. it can be defined
  // by a CR-logical that only defines the subreg) so we state that the CR
  // field is undef. Also, in order to preserve the kill flag on the CR bit,
  // we add it as an implicit use.
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), Reg)
      .addReg(getCRFromCRBit(SrcReg), RegState::Undef)
      .addReg(SrcReg,
              RegState::Implicit | getKillRegState(MI.getOperand(0).isKill()));

  // If the saved register wasn't CR0LT, shift the bits left so that the bit to
  // store is the first one. Mask all but that bit.
  unsigned Reg1 = Reg;
  Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

  // rlwinm rA, rA, ShiftBits, 0, 0.
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
    .addReg(Reg1, RegState::Kill)
    .addImm(getEncodingValue(SrcReg))
    .addImm(0).addImm(0);

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::STW8 : PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRBitRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_CRBIT <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg) &&
    "RESTORE_CRBIT does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LWZ8 : PPC::LWZ),
                              Reg), FrameIndex);

  BuildMI(MBB, II, dl, TII.get(TargetOpcode::IMPLICIT_DEF), DestReg);

  unsigned RegO = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), RegO)
          .addReg(getCRFromCRBit(DestReg));

  unsigned ShiftBits = getEncodingValue(DestReg);
  // rlwimi r11, r10, 32-ShiftBits, ..., ...
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWIMI8 : PPC::RLWIMI), RegO)
      .addReg(RegO, RegState::Kill)
      .addReg(Reg, RegState::Kill)
      .addImm(ShiftBits ? 32 - ShiftBits : 0)
      .addImm(ShiftBits)
      .addImm(ShiftBits);

  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MTOCRF8 : PPC::MTOCRF),
          getCRFromCRBit(DestReg))
      .addReg(RegO, RegState::Kill)
      // Make sure we have a use dependency all the way through this
      // sequence of instructions. We can't have the other bits in the CR
      // modified in between the mfocrf and the mtocrf.
      .addReg(getCRFromCRBit(DestReg), RegState::Implicit);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerVRSAVESpilling(MachineBasicBlock::iterator II,
                                          unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_VRSAVE <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(GPRC);
  unsigned SrcReg = MI.getOperand(0).getReg();

  BuildMI(MBB, II, dl, TII.get(PPC::MFVRSAVEv), Reg)
      .addReg(SrcReg, getKillRegState(MI.getOperand(0).isKill()));

  addFrameReference(
      BuildMI(MBB, II, dl, TII.get(PPC::STW)).addReg(Reg, RegState::Kill),
      FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerVRSAVERestore(MachineBasicBlock::iterator II,
                                         unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_VRSAVE <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(GPRC);
  unsigned DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg) &&
    "RESTORE_VRSAVE does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(PPC::LWZ),
                              Reg), FrameIndex);

  BuildMI(MBB, II, dl, TII.get(PPC::MTVRSAVEv), DestReg)
             .addReg(Reg, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

bool PPCRegisterInfo::hasReservedSpillSlot(const MachineFunction &MF,
                                           unsigned Reg, int &FrameIdx) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // For the nonvolatile condition registers (CR2, CR3, CR4) in an SVR4
  // ABI, return true to prevent allocating an additional frame slot.
  // For 64-bit, the CR save area is at SP+8; the value of FrameIdx = 0
  // is arbitrary and will be subsequently ignored.  For 32-bit, we have
  // previously created the stack slot if needed, so return its FrameIdx.
  if (Subtarget.isSVR4ABI() && PPC::CR2 <= Reg && Reg <= PPC::CR4) {
    if (TM.isPPC64())
      FrameIdx = 0;
    else {
      const PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
      FrameIdx = FI->getCRSpillFrameIndex();
    }
    return true;
  }
  return false;
}

// If the offset must be a multiple of some value, return what that value is.
static unsigned offsetMinAlign(const MachineInstr &MI) {
  unsigned OpC = MI.getOpcode();

  switch (OpC) {
  default:
    return 1;
  case PPC::LWA:
  case PPC::LWA_32:
  case PPC::LD:
  case PPC::LDU:
  case PPC::STD:
  case PPC::STDU:
  case PPC::DFLOADf32:
  case PPC::DFLOADf64:
  case PPC::DFSTOREf32:
  case PPC::DFSTOREf64:
  case PPC::LXSD:
  case PPC::LXSSP:
  case PPC::STXSD:
  case PPC::STXSSP:
    return 4;
  case PPC::LXV:
  case PPC::STXV:
    return 16;
  }
}

// Return the OffsetOperandNo given the FIOperandNum (and the instruction).
static unsigned getOffsetONFromFION(const MachineInstr &MI,
                                    unsigned FIOperandNum) {
  // Take into account whether it's an add or mem instruction
  unsigned OffsetOperandNo = (FIOperandNum == 2) ? 1 : 2;
  if (MI.isInlineAsm())
    OffsetOperandNo = FIOperandNum - 1;
  else if (MI.getOpcode() == TargetOpcode::STACKMAP ||
           MI.getOpcode() == TargetOpcode::PATCHPOINT)
    OffsetOperandNo = FIOperandNum + 1;

  return OffsetOperandNo;
}

void
PPCRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                     int SPAdj, unsigned FIOperandNum,
                                     RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc dl = MI.getDebugLoc();

  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);

  // Get the frame index.
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Get the frame pointer save index.  Users of this index are primarily
  // DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int FPSI = FI->getFramePointerSaveIndex();
  // Get the instruction opcode.
  unsigned OpC = MI.getOpcode();

  if ((OpC == PPC::DYNAREAOFFSET || OpC == PPC::DYNAREAOFFSET8)) {
    lowerDynamicAreaOffset(II);
    return;
  }

  // Special case for dynamic alloca.
  if (FPSI && FrameIndex == FPSI &&
      (OpC == PPC::DYNALLOC || OpC == PPC::DYNALLOC8)) {
    lowerDynamicAlloc(II);
    return;
  }

  // Special case for pseudo-ops SPILL_CR and RESTORE_CR, etc.
  if (OpC == PPC::SPILL_CR) {
    lowerCRSpilling(II, FrameIndex);
    return;
  } else if (OpC == PPC::RESTORE_CR) {
    lowerCRRestore(II, FrameIndex);
    return;
  } else if (OpC == PPC::SPILL_CRBIT) {
    lowerCRBitSpilling(II, FrameIndex);
    return;
  } else if (OpC == PPC::RESTORE_CRBIT) {
    lowerCRBitRestore(II, FrameIndex);
    return;
  } else if (OpC == PPC::SPILL_VRSAVE) {
    lowerVRSAVESpilling(II, FrameIndex);
    return;
  } else if (OpC == PPC::RESTORE_VRSAVE) {
    lowerVRSAVERestore(II, FrameIndex);
    return;
  }

  // Replace the FrameIndex with base register with GPR1 (SP) or GPR31 (FP).
  MI.getOperand(FIOperandNum).ChangeToRegister(
    FrameIndex < 0 ? getBaseRegister(MF) : getFrameRegister(MF), false);

  // If the instruction is not present in ImmToIdxMap, then it has no immediate
  // form (and must be r+r).
  bool noImmForm = !MI.isInlineAsm() && OpC != TargetOpcode::STACKMAP &&
                   OpC != TargetOpcode::PATCHPOINT && !ImmToIdxMap.count(OpC);

  // Now add the frame object offset to the offset from r1.
  int Offset = MFI.getObjectOffset(FrameIndex);
  Offset += MI.getOperand(OffsetOperandNo).getImm();

  // If we're not using a Frame Pointer that has been set to the value of the
  // SP before having the stack size subtracted from it, then add the stack size
  // to Offset to get the correct offset.
  // Naked functions have stack size 0, although getStackSize may not reflect
  // that because we didn't call all the pieces that compute it for naked
  // functions.
  if (!MF.getFunction().hasFnAttribute(Attribute::Naked)) {
    if (!(hasBasePointer(MF) && FrameIndex < 0))
      Offset += MFI.getStackSize();
  }

  // If we can, encode the offset directly into the instruction.  If this is a
  // normal PPC "ri" instruction, any 16-bit value can be safely encoded.  If
  // this is a PPC64 "ix" instruction, only a 16-bit value with the low two bits
  // clear can be encoded.  This is extremely uncommon, because normally you
  // only "std" to a stack slot that is at least 4-byte aligned, but it can
  // happen in invalid code.
  assert(OpC != PPC::DBG_VALUE &&
         "This should be handled in a target-independent way");
  if (!noImmForm && ((isInt<16>(Offset) &&
                      ((Offset % offsetMinAlign(MI)) == 0)) ||
                     OpC == TargetOpcode::STACKMAP ||
                     OpC == TargetOpcode::PATCHPOINT)) {
    MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);
    return;
  }

  // The offset doesn't fit into a single register, scavenge one to build the
  // offset in.

  bool is64Bit = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  const TargetRegisterClass *RC = is64Bit ? G8RC : GPRC;
  unsigned SRegHi = MF.getRegInfo().createVirtualRegister(RC),
           SReg = MF.getRegInfo().createVirtualRegister(RC);

  // Insert a set of rA with the full offset value before the ld, st, or add
  if (isInt<16>(Offset))
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LI8 : PPC::LI), SReg)
      .addImm(Offset);
  else {
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LIS8 : PPC::LIS), SRegHi)
      .addImm(Offset >> 16);
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::ORI8 : PPC::ORI), SReg)
      .addReg(SRegHi, RegState::Kill)
      .addImm(Offset);
  }

  // Convert into indexed form of the instruction:
  //
  //   sth 0:rA, 1:imm 2:(rB) ==> sthx 0:rA, 2:rB, 1:r0
  //   addi 0:rA 1:rB, 2, imm ==> add 0:rA, 1:rB, 2:r0
  unsigned OperandBase;

  if (noImmForm)
    OperandBase = 1;
  else if (OpC != TargetOpcode::INLINEASM) {
    assert(ImmToIdxMap.count(OpC) &&
           "No indexed form of load or store available!");
    unsigned NewOpcode = ImmToIdxMap.find(OpC)->second;
    MI.setDesc(TII.get(NewOpcode));
    OperandBase = 1;
  } else {
    OperandBase = OffsetOperandNo;
  }

  unsigned StackReg = MI.getOperand(FIOperandNum).getReg();
  MI.getOperand(OperandBase).ChangeToRegister(StackReg, false);
  MI.getOperand(OperandBase + 1).ChangeToRegister(SReg, false, false, true);
}

unsigned PPCRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const PPCFrameLowering *TFI = getFrameLowering(MF);

  if (!TM.isPPC64())
    return TFI->hasFP(MF) ? PPC::R31 : PPC::R1;
  else
    return TFI->hasFP(MF) ? PPC::X31 : PPC::X1;
}

unsigned PPCRegisterInfo::getBaseRegister(const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  if (!hasBasePointer(MF))
    return getFrameRegister(MF);

  if (TM.isPPC64())
    return PPC::X30;

  if (Subtarget.isSVR4ABI() && TM.isPositionIndependent())
    return PPC::R29;

  return PPC::R30;
}

bool PPCRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  if (!EnableBasePointer)
    return false;
  if (AlwaysBasePointer)
    return true;

  // If we need to realign the stack, then the stack pointer can no longer
  // serve as an offset into the caller's stack space. As a result, we need a
  // base pointer.
  return needsStackRealignment(MF);
}

/// Returns true if the instruction's frame index
/// reference would be better served by a base register other than FP
/// or SP. Used by LocalStackFrameAllocation to determine which frame index
/// references it should create new base registers for.
bool PPCRegisterInfo::
needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  assert(Offset < 0 && "Local offset must be negative");

  // It's the load/store FI references that cause issues, as it can be difficult
  // to materialize the offset if it won't fit in the literal field. Estimate
  // based on the size of the local frame and some conservative assumptions
  // about the rest of the stack frame (note, this is pre-regalloc, so
  // we don't know everything for certain yet) whether this offset is likely
  // to be out of range of the immediate. Return true if so.

  // We only generate virtual base registers for loads and stores that have
  // an r+i form. Return false for everything else.
  unsigned OpC = MI->getOpcode();
  if (!ImmToIdxMap.count(OpC))
    return false;

  // Don't generate a new virtual base register just to add zero to it.
  if ((OpC == PPC::ADDI || OpC == PPC::ADDI8) &&
      MI->getOperand(2).getImm() == 0)
    return false;

  MachineBasicBlock &MBB = *MI->getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  unsigned StackEst = TFI->determineFrameLayout(MF, false, true);

  // If we likely don't need a stack frame, then we probably don't need a
  // virtual base register either.
  if (!StackEst)
    return false;

  // Estimate an offset from the stack pointer.
  // The incoming offset is relating to the SP at the start of the function,
  // but when we access the local it'll be relative to the SP after local
  // allocation, so adjust our SP-relative offset by that allocation size.
  Offset += StackEst;

  // The frame pointer will point to the end of the stack, so estimate the
  // offset as the difference between the object offset and the FP location.
  return !isFrameOffsetLegal(MI, getBaseRegister(MF), Offset);
}

/// Insert defining instruction(s) for BaseReg to
/// be a pointer to FrameIdx at the beginning of the basic block.
void PPCRegisterInfo::
materializeFrameBaseRegister(MachineBasicBlock *MBB,
                             unsigned BaseReg, int FrameIdx,
                             int64_t Offset) const {
  unsigned ADDriOpc = TM.isPPC64() ? PPC::ADDI8 : PPC::ADDI;

  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL;                  // Defaults to "unknown"
  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  const MachineFunction &MF = *MBB->getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const MCInstrDesc &MCID = TII.get(ADDriOpc);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  MRI.constrainRegClass(BaseReg, TII.getRegClass(MCID, 0, this, MF));

  BuildMI(*MBB, Ins, DL, MCID, BaseReg)
    .addFrameIndex(FrameIdx).addImm(Offset);
}

void PPCRegisterInfo::resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                                        int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI.getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI.getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);
  Offset += MI.getOperand(OffsetOperandNo).getImm();
  MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const MCInstrDesc &MCID = MI.getDesc();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MRI.constrainRegClass(BaseReg,
                        TII.getRegClass(MCID, FIOperandNum, this, MF));
}

bool PPCRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                         unsigned BaseReg,
                                         int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI->getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  unsigned OffsetOperandNo = getOffsetONFromFION(*MI, FIOperandNum);
  Offset += MI->getOperand(OffsetOperandNo).getImm();

  return MI->getOpcode() == PPC::DBG_VALUE || // DBG_VALUE is always Reg+Imm
         MI->getOpcode() == TargetOpcode::STACKMAP ||
         MI->getOpcode() == TargetOpcode::PATCHPOINT ||
         (isInt<16>(Offset) && (Offset % offsetMinAlign(*MI)) == 0);
}
