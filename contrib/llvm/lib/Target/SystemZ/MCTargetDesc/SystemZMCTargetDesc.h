//===-- SystemZMCTargetDesc.h - SystemZ target descriptions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCTARGETDESC_H
#define LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

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
class StringRef;
class Target;
class Triple;
class raw_pwrite_stream;
class raw_ostream;

Target &getTheSystemZTarget();

namespace SystemZMC {
// How many bytes are in the ABI-defined, caller-allocated part of
// a stack frame.
const int64_t CallFrameSize = 160;

// The offset of the DWARF CFA from the incoming stack pointer.
const int64_t CFAOffsetFromInitialSP = CallFrameSize;

// Maps of asm register numbers to LLVM register numbers, with 0 indicating
// an invalid register.  In principle we could use 32-bit and 64-bit register
// classes directly, provided that we relegated the GPR allocation order
// in SystemZRegisterInfo.td to an AltOrder and left the default order
// as %r0-%r15.  It seems better to provide the same interface for
// all classes though.
extern const unsigned GR32Regs[16];
extern const unsigned GRH32Regs[16];
extern const unsigned GR64Regs[16];
extern const unsigned GR128Regs[16];
extern const unsigned FP32Regs[16];
extern const unsigned FP64Regs[16];
extern const unsigned FP128Regs[16];
extern const unsigned VR32Regs[32];
extern const unsigned VR64Regs[32];
extern const unsigned VR128Regs[32];
extern const unsigned AR32Regs[16];
extern const unsigned CR64Regs[16];

// Return the 0-based number of the first architectural register that
// contains the given LLVM register.   E.g. R1D -> 1.
unsigned getFirstReg(unsigned Reg);

// Return the given register as a GR64.
inline unsigned getRegAsGR64(unsigned Reg) {
  return GR64Regs[getFirstReg(Reg)];
}

// Return the given register as a low GR32.
inline unsigned getRegAsGR32(unsigned Reg) {
  return GR32Regs[getFirstReg(Reg)];
}

// Return the given register as a high GR32.
inline unsigned getRegAsGRH32(unsigned Reg) {
  return GRH32Regs[getFirstReg(Reg)];
}

// Return the given register as a VR128.
inline unsigned getRegAsVR128(unsigned Reg) {
  return VR128Regs[getFirstReg(Reg)];
}
} // end namespace SystemZMC

MCCodeEmitter *createSystemZMCCodeEmitter(const MCInstrInfo &MCII,
                                          const MCRegisterInfo &MRI,
                                          MCContext &Ctx);

MCAsmBackend *createSystemZMCAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createSystemZObjectWriter(uint8_t OSABI);
} // end namespace llvm

// Defines symbolic names for SystemZ registers.
// This defines a mapping from register name to register number.
#define GET_REGINFO_ENUM
#include "SystemZGenRegisterInfo.inc"

// Defines symbolic names for the SystemZ instructions.
#define GET_INSTRINFO_ENUM
#include "SystemZGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "SystemZGenSubtargetInfo.inc"

#endif
