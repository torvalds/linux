//===-- AMDGPUMCTargetDesc.h - AMDGPU Target Descriptions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Provides AMDGPU specific target descriptions.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCTARGETDESC_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCTARGETDESC_H

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

Target &getTheAMDGPUTarget();
Target &getTheGCNTarget();

MCCodeEmitter *createR600MCCodeEmitter(const MCInstrInfo &MCII,
                                       const MCRegisterInfo &MRI,
                                       MCContext &Ctx);
MCInstrInfo *createR600MCInstrInfo();

MCCodeEmitter *createSIMCCodeEmitter(const MCInstrInfo &MCII,
                                     const MCRegisterInfo &MRI,
                                     MCContext &Ctx);

MCAsmBackend *createAMDGPUAsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createAMDGPUELFObjectWriter(bool Is64Bit, uint8_t OSABI,
                            bool HasRelocationAddend);
} // End llvm namespace

#define GET_REGINFO_ENUM
#include "AMDGPUGenRegisterInfo.inc"
#undef GET_REGINFO_ENUM

#define GET_REGINFO_ENUM
#include "R600GenRegisterInfo.inc"
#undef GET_REGINFO_ENUM

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_OPERAND_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#include "AMDGPUGenInstrInfo.inc"
#undef GET_INSTRINFO_SCHED_ENUM
#undef GET_INSTRINFO_OPERAND_ENUM
#undef GET_INSTRINFO_ENUM

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_OPERAND_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#include "R600GenInstrInfo.inc"
#undef GET_INSTRINFO_SCHED_ENUM
#undef GET_INSTRINFO_OPERAND_ENUM
#undef GET_INSTRINFO_ENUM

#define GET_SUBTARGETINFO_ENUM
#include "AMDGPUGenSubtargetInfo.inc"
#undef GET_SUBTARGETINFO_ENUM

#define GET_SUBTARGETINFO_ENUM
#include "R600GenSubtargetInfo.inc"
#undef GET_SUBTARGETINFO_ENUM

#endif
