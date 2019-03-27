//===-- HexagonMCTargetDesc.h - Hexagon Target Descriptions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Hexagon specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H

#include "llvm/Support/CommandLine.h"
#include <cstdint>
#include <string>

#define Hexagon_POINTER_SIZE 4

#define Hexagon_PointerSize (Hexagon_POINTER_SIZE)
#define Hexagon_PointerSize_Bits (Hexagon_POINTER_SIZE * 8)
#define Hexagon_WordSize Hexagon_PointerSize
#define Hexagon_WordSize_Bits Hexagon_PointerSize_Bits

// allocframe saves LR and FP on stack before allocating
// a new stack frame. This takes 8 bytes.
#define HEXAGON_LRFP_SIZE 8

// Normal instruction size (in bytes).
#define HEXAGON_INSTR_SIZE 4

// Maximum number of words and instructions in a packet.
#define HEXAGON_PACKET_SIZE 4
#define HEXAGON_MAX_PACKET_SIZE (HEXAGON_PACKET_SIZE * HEXAGON_INSTR_SIZE)
// Minimum number of instructions in an end-loop packet.
#define HEXAGON_PACKET_INNER_SIZE 2
#define HEXAGON_PACKET_OUTER_SIZE 3
// Maximum number of instructions in a packet before shuffling,
// including a compound one or a duplex or an extender.
#define HEXAGON_PRESHUFFLE_PACKET_SIZE (HEXAGON_PACKET_SIZE + 3)

// Name of the global offset table as defined by the Hexagon ABI
#define HEXAGON_GOT_SYM_NAME "_GLOBAL_OFFSET_TABLE_"

namespace llvm {

struct InstrItinerary;
struct InstrStage;
class FeatureBitset;
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
class raw_ostream;
class raw_pwrite_stream;

Target &getTheHexagonTarget();
extern cl::opt<bool> HexagonDisableCompound;
extern cl::opt<bool> HexagonDisableDuplex;
extern const InstrStage HexagonStages[];

MCInstrInfo *createHexagonMCInstrInfo();
MCRegisterInfo *createHexagonMCRegisterInfo(StringRef TT);

namespace Hexagon_MC {
  StringRef selectHexagonCPU(StringRef CPU);

  FeatureBitset completeHVXFeatures(const FeatureBitset &FB);
  /// Create a Hexagon MCSubtargetInfo instance. This is exposed so Asm parser,
  /// etc. do not need to go through TargetRegistry.
  MCSubtargetInfo *createHexagonMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                                StringRef FS);
  unsigned GetELFFlags(const MCSubtargetInfo &STI);
}

MCCodeEmitter *createHexagonMCCodeEmitter(const MCInstrInfo &MCII,
                                          const MCRegisterInfo &MRI,
                                          MCContext &MCT);

MCAsmBackend *createHexagonAsmBackend(const Target &T,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createHexagonELFObjectWriter(uint8_t OSABI, StringRef CPU);

unsigned HexagonGetLastSlot();

} // End llvm namespace

// Define symbolic names for Hexagon registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "HexagonGenRegisterInfo.inc"

// Defines symbolic names for the Hexagon instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#include "HexagonGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "HexagonGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H
