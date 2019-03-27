//===-- X86RegisterInfo.cpp - X86 Register Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetRegisterInfo class.
// This file is responsible for the frame pointer elimination optimization
// on X86.
//
//===----------------------------------------------------------------------===//

#include "X86RegisterInfo.h"
#include "X86FrameLowering.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "X86GenRegisterInfo.inc"

static cl::opt<bool>
EnableBasePointer("x86-use-base-pointer", cl::Hidden, cl::init(true),
          cl::desc("Enable use of a base pointer for complex stack frames"));

X86RegisterInfo::X86RegisterInfo(const Triple &TT)
    : X86GenRegisterInfo((TT.isArch64Bit() ? X86::RIP : X86::EIP),
                         X86_MC::getDwarfRegFlavour(TT, false),
                         X86_MC::getDwarfRegFlavour(TT, true),
                         (TT.isArch64Bit() ? X86::RIP : X86::EIP)) {
  X86_MC::initLLVMToSEHAndCVRegMapping(this);

  // Cache some information.
  Is64Bit = TT.isArch64Bit();
  IsWin64 = Is64Bit && TT.isOSWindows();

  // Use a callee-saved register as the base pointer.  These registers must
  // not conflict with any ABI requirements.  For example, in 32-bit mode PIC
  // requires GOT in the EBX register before function calls via PLT GOT pointer.
  if (Is64Bit) {
    SlotSize = 8;
    // This matches the simplified 32-bit pointer code in the data layout
    // computation.
    // FIXME: Should use the data layout?
    bool Use64BitReg = TT.getEnvironment() != Triple::GNUX32;
    StackPtr = Use64BitReg ? X86::RSP : X86::ESP;
    FramePtr = Use64BitReg ? X86::RBP : X86::EBP;
    BasePtr = Use64BitReg ? X86::RBX : X86::EBX;
  } else {
    SlotSize = 4;
    StackPtr = X86::ESP;
    FramePtr = X86::EBP;
    BasePtr = X86::ESI;
  }
}

bool
X86RegisterInfo::trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  // ExecutionDomainFix, BreakFalseDeps and PostRAScheduler require liveness.
  return true;
}

int
X86RegisterInfo::getSEHRegNum(unsigned i) const {
  return getEncodingValue(i);
}

const TargetRegisterClass *
X86RegisterInfo::getSubClassWithSubReg(const TargetRegisterClass *RC,
                                       unsigned Idx) const {
  // The sub_8bit sub-register index is more constrained in 32-bit mode.
  // It behaves just like the sub_8bit_hi index.
  if (!Is64Bit && Idx == X86::sub_8bit)
    Idx = X86::sub_8bit_hi;

  // Forward to TableGen's default version.
  return X86GenRegisterInfo::getSubClassWithSubReg(RC, Idx);
}

const TargetRegisterClass *
X86RegisterInfo::getMatchingSuperRegClass(const TargetRegisterClass *A,
                                          const TargetRegisterClass *B,
                                          unsigned SubIdx) const {
  // The sub_8bit sub-register index is more constrained in 32-bit mode.
  if (!Is64Bit && SubIdx == X86::sub_8bit) {
    A = X86GenRegisterInfo::getSubClassWithSubReg(A, X86::sub_8bit_hi);
    if (!A)
      return nullptr;
  }
  return X86GenRegisterInfo::getMatchingSuperRegClass(A, B, SubIdx);
}

const TargetRegisterClass *
X86RegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &MF) const {
  // Don't allow super-classes of GR8_NOREX.  This class is only used after
  // extracting sub_8bit_hi sub-registers.  The H sub-registers cannot be copied
  // to the full GR8 register class in 64-bit mode, so we cannot allow the
  // reigster class inflation.
  //
  // The GR8_NOREX class is always used in a way that won't be constrained to a
  // sub-class, so sub-classes like GR8_ABCD_L are allowed to expand to the
  // full GR8 class.
  if (RC == &X86::GR8_NOREXRegClass)
    return RC;

  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();

  const TargetRegisterClass *Super = RC;
  TargetRegisterClass::sc_iterator I = RC->getSuperClasses();
  do {
    switch (Super->getID()) {
    case X86::FR32RegClassID:
    case X86::FR64RegClassID:
      // If AVX-512 isn't supported we should only inflate to these classes.
      if (!Subtarget.hasAVX512() &&
          getRegSizeInBits(*Super) == getRegSizeInBits(*RC))
        return Super;
      break;
    case X86::VR128RegClassID:
    case X86::VR256RegClassID:
      // If VLX isn't supported we should only inflate to these classes.
      if (!Subtarget.hasVLX() &&
          getRegSizeInBits(*Super) == getRegSizeInBits(*RC))
        return Super;
      break;
    case X86::VR128XRegClassID:
    case X86::VR256XRegClassID:
      // If VLX isn't support we shouldn't inflate to these classes.
      if (Subtarget.hasVLX() &&
          getRegSizeInBits(*Super) == getRegSizeInBits(*RC))
        return Super;
      break;
    case X86::FR32XRegClassID:
    case X86::FR64XRegClassID:
      // If AVX-512 isn't support we shouldn't inflate to these classes.
      if (Subtarget.hasAVX512() &&
          getRegSizeInBits(*Super) == getRegSizeInBits(*RC))
        return Super;
      break;
    case X86::GR8RegClassID:
    case X86::GR16RegClassID:
    case X86::GR32RegClassID:
    case X86::GR64RegClassID:
    case X86::RFP32RegClassID:
    case X86::RFP64RegClassID:
    case X86::RFP80RegClassID:
    case X86::VR512RegClassID:
      // Don't return a super-class that would shrink the spill size.
      // That can happen with the vector and float classes.
      if (getRegSizeInBits(*Super) == getRegSizeInBits(*RC))
        return Super;
    }
    Super = *I++;
  } while (Super);
  return RC;
}

const TargetRegisterClass *
X86RegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                    unsigned Kind) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  switch (Kind) {
  default: llvm_unreachable("Unexpected Kind in getPointerRegClass!");
  case 0: // Normal GPRs.
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64RegClass;
    // If the target is 64bit but we have been told to use 32bit addresses,
    // we can still use 64-bit register as long as we know the high bits
    // are zeros.
    // Reflect that in the returned register class.
    if (Is64Bit) {
      // When the target also allows 64-bit frame pointer and we do have a
      // frame, this is fine to use it for the address accesses as well.
      const X86FrameLowering *TFI = getFrameLowering(MF);
      return TFI->hasFP(MF) && TFI->Uses64BitFramePtr
                 ? &X86::LOW32_ADDR_ACCESS_RBPRegClass
                 : &X86::LOW32_ADDR_ACCESSRegClass;
    }
    return &X86::GR32RegClass;
  case 1: // Normal GPRs except the stack pointer (for encoding reasons).
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOSPRegClass;
    // NOSP does not contain RIP, so no special case here.
    return &X86::GR32_NOSPRegClass;
  case 2: // NOREX GPRs.
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOREXRegClass;
    return &X86::GR32_NOREXRegClass;
  case 3: // NOREX GPRs except the stack pointer (for encoding reasons).
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOREX_NOSPRegClass;
    // NOSP does not contain RIP, so no special case here.
    return &X86::GR32_NOREX_NOSPRegClass;
  case 4: // Available for tailcall (not callee-saved GPRs).
    return getGPRsForTailCall(MF);
  }
}

const TargetRegisterClass *
X86RegisterInfo::getGPRsForTailCall(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  if (IsWin64 || (F.getCallingConv() == CallingConv::Win64))
    return &X86::GR64_TCW64RegClass;
  else if (Is64Bit)
    return &X86::GR64_TCRegClass;

  bool hasHipeCC = (F.getCallingConv() == CallingConv::HiPE);
  if (hasHipeCC)
    return &X86::GR32RegClass;
  return &X86::GR32_TCRegClass;
}

const TargetRegisterClass *
X86RegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (RC == &X86::CCRRegClass) {
    if (Is64Bit)
      return &X86::GR64RegClass;
    else
      return &X86::GR32RegClass;
  }
  return RC;
}

unsigned
X86RegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                     MachineFunction &MF) const {
  const X86FrameLowering *TFI = getFrameLowering(MF);

  unsigned FPDiff = TFI->hasFP(MF) ? 1 : 0;
  switch (RC->getID()) {
  default:
    return 0;
  case X86::GR32RegClassID:
    return 4 - FPDiff;
  case X86::GR64RegClassID:
    return 12 - FPDiff;
  case X86::VR128RegClassID:
    return Is64Bit ? 10 : 4;
  case X86::VR64RegClassID:
    return 4;
  }
}

const MCPhysReg *
X86RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  assert(MF && "MachineFunction required");

  const X86Subtarget &Subtarget = MF->getSubtarget<X86Subtarget>();
  const Function &F = MF->getFunction();
  bool HasSSE = Subtarget.hasSSE1();
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();
  bool CallsEHReturn = MF->callsEHReturn();

  CallingConv::ID CC = F.getCallingConv();

  // If attribute NoCallerSavedRegisters exists then we set X86_INTR calling
  // convention because it has the CSR list.
  if (MF->getFunction().hasFnAttribute("no_caller_saved_registers"))
    CC = CallingConv::X86_INTR;

  switch (CC) {
  case CallingConv::GHC:
  case CallingConv::HiPE:
    return CSR_NoRegs_SaveList;
  case CallingConv::AnyReg:
    if (HasAVX)
      return CSR_64_AllRegs_AVX_SaveList;
    return CSR_64_AllRegs_SaveList;
  case CallingConv::PreserveMost:
    return CSR_64_RT_MostRegs_SaveList;
  case CallingConv::PreserveAll:
    if (HasAVX)
      return CSR_64_RT_AllRegs_AVX_SaveList;
    return CSR_64_RT_AllRegs_SaveList;
  case CallingConv::CXX_FAST_TLS:
    if (Is64Bit)
      return MF->getInfo<X86MachineFunctionInfo>()->isSplitCSR() ?
             CSR_64_CXX_TLS_Darwin_PE_SaveList : CSR_64_TLS_Darwin_SaveList;
    break;
  case CallingConv::Intel_OCL_BI: {
    if (HasAVX512 && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX512_SaveList;
    if (HasAVX512 && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX512_SaveList;
    if (HasAVX && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX_SaveList;
    if (HasAVX && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX_SaveList;
    if (!HasAVX && !IsWin64 && Is64Bit)
      return CSR_64_Intel_OCL_BI_SaveList;
    break;
  }
  case CallingConv::HHVM:
    return CSR_64_HHVM_SaveList;
  case CallingConv::X86_RegCall:
    if (Is64Bit) {
      if (IsWin64) {
        return (HasSSE ? CSR_Win64_RegCall_SaveList :
                         CSR_Win64_RegCall_NoSSE_SaveList);
      } else {
        return (HasSSE ? CSR_SysV64_RegCall_SaveList :
                         CSR_SysV64_RegCall_NoSSE_SaveList);
      }
    } else {
      return (HasSSE ? CSR_32_RegCall_SaveList :
                       CSR_32_RegCall_NoSSE_SaveList);
    }
  case CallingConv::Cold:
    if (Is64Bit)
      return CSR_64_MostRegs_SaveList;
    break;
  case CallingConv::Win64:
    if (!HasSSE)
      return CSR_Win64_NoSSE_SaveList;
    return CSR_Win64_SaveList;
  case CallingConv::X86_64_SysV:
    if (CallsEHReturn)
      return CSR_64EHRet_SaveList;
    return CSR_64_SaveList;
  case CallingConv::X86_INTR:
    if (Is64Bit) {
      if (HasAVX512)
        return CSR_64_AllRegs_AVX512_SaveList;
      if (HasAVX)
        return CSR_64_AllRegs_AVX_SaveList;
      if (HasSSE)
        return CSR_64_AllRegs_SaveList;
      return CSR_64_AllRegs_NoSSE_SaveList;
    } else {
      if (HasAVX512)
        return CSR_32_AllRegs_AVX512_SaveList;
      if (HasAVX)
        return CSR_32_AllRegs_AVX_SaveList;
      if (HasSSE)
        return CSR_32_AllRegs_SSE_SaveList;
      return CSR_32_AllRegs_SaveList;
    }
  default:
    break;
  }

  if (Is64Bit) {
    bool IsSwiftCC = Subtarget.getTargetLowering()->supportSwiftError() &&
                     F.getAttributes().hasAttrSomewhere(Attribute::SwiftError);
    if (IsSwiftCC)
      return IsWin64 ? CSR_Win64_SwiftError_SaveList
                     : CSR_64_SwiftError_SaveList;

    if (IsWin64)
      return HasSSE ? CSR_Win64_SaveList : CSR_Win64_NoSSE_SaveList;
    if (CallsEHReturn)
      return CSR_64EHRet_SaveList;
    return CSR_64_SaveList;
  }

  return CallsEHReturn ? CSR_32EHRet_SaveList : CSR_32_SaveList;
}

const MCPhysReg *X86RegisterInfo::getCalleeSavedRegsViaCopy(
    const MachineFunction *MF) const {
  assert(MF && "Invalid MachineFunction pointer.");
  if (MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
      MF->getInfo<X86MachineFunctionInfo>()->isSplitCSR())
    return CSR_64_CXX_TLS_Darwin_ViaCopy_SaveList;
  return nullptr;
}

const uint32_t *
X86RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  bool HasSSE = Subtarget.hasSSE1();
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();

  switch (CC) {
  case CallingConv::GHC:
  case CallingConv::HiPE:
    return CSR_NoRegs_RegMask;
  case CallingConv::AnyReg:
    if (HasAVX)
      return CSR_64_AllRegs_AVX_RegMask;
    return CSR_64_AllRegs_RegMask;
  case CallingConv::PreserveMost:
    return CSR_64_RT_MostRegs_RegMask;
  case CallingConv::PreserveAll:
    if (HasAVX)
      return CSR_64_RT_AllRegs_AVX_RegMask;
    return CSR_64_RT_AllRegs_RegMask;
  case CallingConv::CXX_FAST_TLS:
    if (Is64Bit)
      return CSR_64_TLS_Darwin_RegMask;
    break;
  case CallingConv::Intel_OCL_BI: {
    if (HasAVX512 && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX512_RegMask;
    if (HasAVX512 && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX512_RegMask;
    if (HasAVX && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX_RegMask;
    if (HasAVX && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX_RegMask;
    if (!HasAVX && !IsWin64 && Is64Bit)
      return CSR_64_Intel_OCL_BI_RegMask;
    break;
  }
  case CallingConv::HHVM:
    return CSR_64_HHVM_RegMask;
  case CallingConv::X86_RegCall:
    if (Is64Bit) {
      if (IsWin64) {
        return (HasSSE ? CSR_Win64_RegCall_RegMask :
                         CSR_Win64_RegCall_NoSSE_RegMask);
      } else {
        return (HasSSE ? CSR_SysV64_RegCall_RegMask :
                         CSR_SysV64_RegCall_NoSSE_RegMask);
      }
    } else {
      return (HasSSE ? CSR_32_RegCall_RegMask :
                       CSR_32_RegCall_NoSSE_RegMask);
    }
  case CallingConv::Cold:
    if (Is64Bit)
      return CSR_64_MostRegs_RegMask;
    break;
  case CallingConv::Win64:
    return CSR_Win64_RegMask;
  case CallingConv::X86_64_SysV:
    return CSR_64_RegMask;
  case CallingConv::X86_INTR:
    if (Is64Bit) {
      if (HasAVX512)
        return CSR_64_AllRegs_AVX512_RegMask;
      if (HasAVX)
        return CSR_64_AllRegs_AVX_RegMask;
      if (HasSSE)
        return CSR_64_AllRegs_RegMask;
      return CSR_64_AllRegs_NoSSE_RegMask;
    } else {
      if (HasAVX512)
        return CSR_32_AllRegs_AVX512_RegMask;
      if (HasAVX)
        return CSR_32_AllRegs_AVX_RegMask;
      if (HasSSE)
        return CSR_32_AllRegs_SSE_RegMask;
      return CSR_32_AllRegs_RegMask;
    }
  default:
    break;
  }

  // Unlike getCalleeSavedRegs(), we don't have MMI so we can't check
  // callsEHReturn().
  if (Is64Bit) {
    const Function &F = MF.getFunction();
    bool IsSwiftCC = Subtarget.getTargetLowering()->supportSwiftError() &&
                     F.getAttributes().hasAttrSomewhere(Attribute::SwiftError);
    if (IsSwiftCC)
      return IsWin64 ? CSR_Win64_SwiftError_RegMask : CSR_64_SwiftError_RegMask;
    return IsWin64 ? CSR_Win64_RegMask : CSR_64_RegMask;
  }

  return CSR_32_RegMask;
}

const uint32_t*
X86RegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

const uint32_t *X86RegisterInfo::getDarwinTLSCallPreservedMask() const {
  return CSR_64_TLS_Darwin_RegMask;
}

BitVector X86RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const X86FrameLowering *TFI = getFrameLowering(MF);

  // Set the floating point control register as reserved.
  Reserved.set(X86::FPCW);

  // Set the stack-pointer register and its aliases as reserved.
  for (MCSubRegIterator I(X86::RSP, this, /*IncludeSelf=*/true); I.isValid();
       ++I)
    Reserved.set(*I);

  // Set the Shadow Stack Pointer as reserved.
  Reserved.set(X86::SSP);

  // Set the instruction pointer register and its aliases as reserved.
  for (MCSubRegIterator I(X86::RIP, this, /*IncludeSelf=*/true); I.isValid();
       ++I)
    Reserved.set(*I);

  // Set the frame-pointer register and its aliases as reserved if needed.
  if (TFI->hasFP(MF)) {
    for (MCSubRegIterator I(X86::RBP, this, /*IncludeSelf=*/true); I.isValid();
         ++I)
      Reserved.set(*I);
  }

  // Set the base-pointer register and its aliases as reserved if needed.
  if (hasBasePointer(MF)) {
    CallingConv::ID CC = MF.getFunction().getCallingConv();
    const uint32_t *RegMask = getCallPreservedMask(MF, CC);
    if (MachineOperand::clobbersPhysReg(RegMask, getBaseRegister()))
      report_fatal_error(
        "Stack realignment in presence of dynamic allocas is not supported with"
        "this calling convention.");

    unsigned BasePtr = getX86SubSuperRegister(getBaseRegister(), 64);
    for (MCSubRegIterator I(BasePtr, this, /*IncludeSelf=*/true);
         I.isValid(); ++I)
      Reserved.set(*I);
  }

  // Mark the segment registers as reserved.
  Reserved.set(X86::CS);
  Reserved.set(X86::SS);
  Reserved.set(X86::DS);
  Reserved.set(X86::ES);
  Reserved.set(X86::FS);
  Reserved.set(X86::GS);

  // Mark the floating point stack registers as reserved.
  for (unsigned n = 0; n != 8; ++n)
    Reserved.set(X86::ST0 + n);

  // Reserve the registers that only exist in 64-bit mode.
  if (!Is64Bit) {
    // These 8-bit registers are part of the x86-64 extension even though their
    // super-registers are old 32-bits.
    Reserved.set(X86::SIL);
    Reserved.set(X86::DIL);
    Reserved.set(X86::BPL);
    Reserved.set(X86::SPL);
    Reserved.set(X86::SIH);
    Reserved.set(X86::DIH);
    Reserved.set(X86::BPH);
    Reserved.set(X86::SPH);

    for (unsigned n = 0; n != 8; ++n) {
      // R8, R9, ...
      for (MCRegAliasIterator AI(X86::R8 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);

      // XMM8, XMM9, ...
      for (MCRegAliasIterator AI(X86::XMM8 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);
    }
  }
  if (!Is64Bit || !MF.getSubtarget<X86Subtarget>().hasAVX512()) {
    for (unsigned n = 16; n != 32; ++n) {
      for (MCRegAliasIterator AI(X86::XMM0 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);
    }
  }

  assert(checkAllSuperRegsMarked(Reserved,
                                 {X86::SIL, X86::DIL, X86::BPL, X86::SPL,
                                  X86::SIH, X86::DIH, X86::BPH, X86::SPH}));
  return Reserved;
}

void X86RegisterInfo::adjustStackMapLiveOutMask(uint32_t *Mask) const {
  // Check if the EFLAGS register is marked as live-out. This shouldn't happen,
  // because the calling convention defines the EFLAGS register as NOT
  // preserved.
  //
  // Unfortunatelly the EFLAGS show up as live-out after branch folding. Adding
  // an assert to track this and clear the register afterwards to avoid
  // unnecessary crashes during release builds.
  assert(!(Mask[X86::EFLAGS / 32] & (1U << (X86::EFLAGS % 32))) &&
         "EFLAGS are not live-out from a patchpoint.");

  // Also clean other registers that don't need preserving (IP).
  for (auto Reg : {X86::EFLAGS, X86::RIP, X86::EIP, X86::IP})
    Mask[Reg / 32] &= ~(1U << (Reg % 32));
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

static bool CantUseSP(const MachineFrameInfo &MFI) {
  return MFI.hasVarSizedObjects() || MFI.hasOpaqueSPAdjustment();
}

bool X86RegisterInfo::hasBasePointer(const MachineFunction &MF) const {
   const MachineFrameInfo &MFI = MF.getFrameInfo();

   if (!EnableBasePointer)
     return false;

   // When we need stack realignment, we can't address the stack from the frame
   // pointer.  When we have dynamic allocas or stack-adjusting inline asm, we
   // can't address variables from the stack pointer.  MS inline asm can
   // reference locals while also adjusting the stack pointer.  When we can't
   // use both the SP and the FP, we need a separate base pointer register.
   bool CantUseFP = needsStackRealignment(MF);
   return CantUseFP && CantUseSP(MFI);
}

bool X86RegisterInfo::canRealignStack(const MachineFunction &MF) const {
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const MachineRegisterInfo *MRI = &MF.getRegInfo();

  // Stack realignment requires a frame pointer.  If we already started
  // register allocation with frame pointer elimination, it is too late now.
  if (!MRI->canReserveReg(FramePtr))
    return false;

  // If a base pointer is necessary.  Check that it isn't too late to reserve
  // it.
  if (CantUseSP(MFI))
    return MRI->canReserveReg(BasePtr);
  return true;
}

bool X86RegisterInfo::hasReservedSpillSlot(const MachineFunction &MF,
                                           unsigned Reg, int &FrameIdx) const {
  // Since X86 defines assignCalleeSavedSpillSlots which always return true
  // this function neither used nor tested.
  llvm_unreachable("Unused function on X86. Otherwise need a test case.");
}

// tryOptimizeLEAtoMOV - helper function that tries to replace a LEA instruction
// of the form 'lea (%esp), %ebx' --> 'mov %esp, %ebx'.
// TODO: In this case we should be really trying first to entirely eliminate
// this instruction which is a plain copy.
static bool tryOptimizeLEAtoMOV(MachineBasicBlock::iterator II) {
  MachineInstr &MI = *II;
  unsigned Opc = II->getOpcode();
  // Check if this is a LEA of the form 'lea (%esp), %ebx'
  if ((Opc != X86::LEA32r && Opc != X86::LEA64r && Opc != X86::LEA64_32r) ||
      MI.getOperand(2).getImm() != 1 ||
      MI.getOperand(3).getReg() != X86::NoRegister ||
      MI.getOperand(4).getImm() != 0 ||
      MI.getOperand(5).getReg() != X86::NoRegister)
    return false;
  unsigned BasePtr = MI.getOperand(1).getReg();
  // In X32 mode, ensure the base-pointer is a 32-bit operand, so the LEA will
  // be replaced with a 32-bit operand MOV which will zero extend the upper
  // 32-bits of the super register.
  if (Opc == X86::LEA64_32r)
    BasePtr = getX86SubSuperRegister(BasePtr, 32);
  unsigned NewDestReg = MI.getOperand(0).getReg();
  const X86InstrInfo *TII =
      MI.getParent()->getParent()->getSubtarget<X86Subtarget>().getInstrInfo();
  TII->copyPhysReg(*MI.getParent(), II, MI.getDebugLoc(), NewDestReg, BasePtr,
                   MI.getOperand(1).isKill());
  MI.eraseFromParent();
  return true;
}

void
X86RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                     int SPAdj, unsigned FIOperandNum,
                                     RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const X86FrameLowering *TFI = getFrameLowering(MF);
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Determine base register and offset.
  int FIOffset;
  unsigned BasePtr;
  if (MI.isReturn()) {
    assert((!needsStackRealignment(MF) ||
           MF.getFrameInfo().isFixedObjectIndex(FrameIndex)) &&
           "Return instruction can only reference SP relative frame objects");
    FIOffset = TFI->getFrameIndexReferenceSP(MF, FrameIndex, BasePtr, 0);
  } else {
    FIOffset = TFI->getFrameIndexReference(MF, FrameIndex, BasePtr);
  }

  // LOCAL_ESCAPE uses a single offset, with no register. It only works in the
  // simple FP case, and doesn't work with stack realignment. On 32-bit, the
  // offset is from the traditional base pointer location.  On 64-bit, the
  // offset is from the SP at the end of the prologue, not the FP location. This
  // matches the behavior of llvm.frameaddress.
  unsigned Opc = MI.getOpcode();
  if (Opc == TargetOpcode::LOCAL_ESCAPE) {
    MachineOperand &FI = MI.getOperand(FIOperandNum);
    FI.ChangeToImmediate(FIOffset);
    return;
  }

  // For LEA64_32r when BasePtr is 32-bits (X32) we can use full-size 64-bit
  // register as source operand, semantic is the same and destination is
  // 32-bits. It saves one byte per lea in code since 0x67 prefix is avoided.
  // Don't change BasePtr since it is used later for stack adjustment.
  unsigned MachineBasePtr = BasePtr;
  if (Opc == X86::LEA64_32r && X86::GR32RegClass.contains(BasePtr))
    MachineBasePtr = getX86SubSuperRegister(BasePtr, 64);

  // This must be part of a four operand memory reference.  Replace the
  // FrameIndex with base register.  Add an offset to the offset.
  MI.getOperand(FIOperandNum).ChangeToRegister(MachineBasePtr, false);

  if (BasePtr == StackPtr)
    FIOffset += SPAdj;

  // The frame index format for stackmaps and patchpoints is different from the
  // X86 format. It only has a FI and an offset.
  if (Opc == TargetOpcode::STACKMAP || Opc == TargetOpcode::PATCHPOINT) {
    assert(BasePtr == FramePtr && "Expected the FP as base register");
    int64_t Offset = MI.getOperand(FIOperandNum + 1).getImm() + FIOffset;
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return;
  }

  if (MI.getOperand(FIOperandNum+3).isImm()) {
    // Offset is a 32-bit integer.
    int Imm = (int)(MI.getOperand(FIOperandNum + 3).getImm());
    int Offset = FIOffset + Imm;
    assert((!Is64Bit || isInt<32>((long long)FIOffset + Imm)) &&
           "Requesting 64-bit offset in 32-bit immediate!");
    if (Offset != 0 || !tryOptimizeLEAtoMOV(II))
      MI.getOperand(FIOperandNum + 3).ChangeToImmediate(Offset);
  } else {
    // Offset is symbolic. This is extremely rare.
    uint64_t Offset = FIOffset +
      (uint64_t)MI.getOperand(FIOperandNum+3).getOffset();
    MI.getOperand(FIOperandNum + 3).setOffset(Offset);
  }
}

unsigned X86RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const X86FrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? FramePtr : StackPtr;
}

unsigned
X86RegisterInfo::getPtrSizedFrameRegister(const MachineFunction &MF) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  unsigned FrameReg = getFrameRegister(MF);
  if (Subtarget.isTarget64BitILP32())
    FrameReg = getX86SubSuperRegister(FrameReg, 32);
  return FrameReg;
}
