//===-- PPCMCTargetDesc.h - PowerPC Target Descriptions ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides PowerPC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H

// GCC #defines PPC on Linux but we use it as our namespace name
#undef PPC

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;
class Triple;
class StringRef;
class raw_pwrite_stream;

Target &getThePPC32Target();
Target &getThePPC64Target();
Target &getThePPC64LETarget();

MCCodeEmitter *createPPCMCCodeEmitter(const MCInstrInfo &MCII,
                                      const MCRegisterInfo &MRI,
                                      MCContext &Ctx);

MCAsmBackend *createPPCAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                  const MCRegisterInfo &MRI,
                                  const MCTargetOptions &Options);

/// Construct an PPC ELF object writer.
std::unique_ptr<MCObjectTargetWriter> createPPCELFObjectWriter(bool Is64Bit,
                                                               uint8_t OSABI);
/// Construct a PPC Mach-O object writer.
std::unique_ptr<MCObjectTargetWriter>
createPPCMachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype);

/// Returns true iff Val consists of one contiguous run of 1s with any number of
/// 0s on either side.  The 1s are allowed to wrap from LSB to MSB, so
/// 0x000FFF0, 0x0000FFFF, and 0xFF0000FF are all runs.  0x0F0F0000 is not,
/// since all 1s are not contiguous.
static inline bool isRunOfOnes(unsigned Val, unsigned &MB, unsigned &ME) {
  if (!Val)
    return false;

  if (isShiftedMask_32(Val)) {
    // look for the first non-zero bit
    MB = countLeadingZeros(Val);
    // look for the first zero bit after the run of ones
    ME = countLeadingZeros((Val - 1) ^ Val);
    return true;
  } else {
    Val = ~Val; // invert mask
    if (isShiftedMask_32(Val)) {
      // effectively look for the first zero bit
      ME = countLeadingZeros(Val) - 1;
      // effectively look for the first one bit after the run of zeros
      MB = countLeadingZeros((Val - 1) ^ Val) + 1;
      return true;
    }
  }
  // no run present
  return false;
}

} // end namespace llvm

// Generated files will use "namespace PPC". To avoid symbol clash,
// undefine PPC here. PPC may be predefined on some hosts.
#undef PPC

// Defines symbolic names for PowerPC registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "PPCGenRegisterInfo.inc"

// Defines symbolic names for the PowerPC instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#include "PPCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "PPCGenSubtargetInfo.inc"

#define PPC_REGS0_31(X)                                                        \
  {                                                                            \
    X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11,  \
        X##12, X##13, X##14, X##15, X##16, X##17, X##18, X##19, X##20, X##21,  \
        X##22, X##23, X##24, X##25, X##26, X##27, X##28, X##29, X##30, X##31   \
  }

#define PPC_REGS_NO0_31(Z, X)                                                  \
  {                                                                            \
    Z, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11,     \
        X##12, X##13, X##14, X##15, X##16, X##17, X##18, X##19, X##20, X##21,  \
        X##22, X##23, X##24, X##25, X##26, X##27, X##28, X##29, X##30, X##31   \
  }

#define PPC_REGS_LO_HI(LO, HI)                                                 \
  {                                                                            \
    LO##0, LO##1, LO##2, LO##3, LO##4, LO##5, LO##6, LO##7, LO##8, LO##9,      \
        LO##10, LO##11, LO##12, LO##13, LO##14, LO##15, LO##16, LO##17,        \
        LO##18, LO##19, LO##20, LO##21, LO##22, LO##23, LO##24, LO##25,        \
        LO##26, LO##27, LO##28, LO##29, LO##30, LO##31, HI##0, HI##1, HI##2,   \
        HI##3, HI##4, HI##5, HI##6, HI##7, HI##8, HI##9, HI##10, HI##11,       \
        HI##12, HI##13, HI##14, HI##15, HI##16, HI##17, HI##18, HI##19,        \
        HI##20, HI##21, HI##22, HI##23, HI##24, HI##25, HI##26, HI##27,        \
        HI##28, HI##29, HI##30, HI##31                                         \
  }

using llvm::MCPhysReg;

#define DEFINE_PPC_REGCLASSES \
  static const MCPhysReg RRegs[32] = PPC_REGS0_31(PPC::R); \
  static const MCPhysReg XRegs[32] = PPC_REGS0_31(PPC::X); \
  static const MCPhysReg FRegs[32] = PPC_REGS0_31(PPC::F); \
  static const MCPhysReg SPERegs[32] = PPC_REGS0_31(PPC::S); \
  static const MCPhysReg VFRegs[32] = PPC_REGS0_31(PPC::VF); \
  static const MCPhysReg VRegs[32] = PPC_REGS0_31(PPC::V); \
  static const MCPhysReg QFRegs[32] = PPC_REGS0_31(PPC::QF); \
  static const MCPhysReg RRegsNoR0[32] = \
    PPC_REGS_NO0_31(PPC::ZERO, PPC::R); \
  static const MCPhysReg XRegsNoX0[32] = \
    PPC_REGS_NO0_31(PPC::ZERO8, PPC::X); \
  static const MCPhysReg VSRegs[64] = \
    PPC_REGS_LO_HI(PPC::VSL, PPC::V); \
  static const MCPhysReg VSFRegs[64] = \
    PPC_REGS_LO_HI(PPC::F, PPC::VF); \
  static const MCPhysReg VSSRegs[64] = \
    PPC_REGS_LO_HI(PPC::F, PPC::VF); \
  static const MCPhysReg CRBITRegs[32] = { \
    PPC::CR0LT, PPC::CR0GT, PPC::CR0EQ, PPC::CR0UN, \
    PPC::CR1LT, PPC::CR1GT, PPC::CR1EQ, PPC::CR1UN, \
    PPC::CR2LT, PPC::CR2GT, PPC::CR2EQ, PPC::CR2UN, \
    PPC::CR3LT, PPC::CR3GT, PPC::CR3EQ, PPC::CR3UN, \
    PPC::CR4LT, PPC::CR4GT, PPC::CR4EQ, PPC::CR4UN, \
    PPC::CR5LT, PPC::CR5GT, PPC::CR5EQ, PPC::CR5UN, \
    PPC::CR6LT, PPC::CR6GT, PPC::CR6EQ, PPC::CR6UN, \
    PPC::CR7LT, PPC::CR7GT, PPC::CR7EQ, PPC::CR7UN}; \
  static const MCPhysReg CRRegs[8] = { \
    PPC::CR0, PPC::CR1, PPC::CR2, PPC::CR3, \
    PPC::CR4, PPC::CR5, PPC::CR6, PPC::CR7}

#endif // LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H
