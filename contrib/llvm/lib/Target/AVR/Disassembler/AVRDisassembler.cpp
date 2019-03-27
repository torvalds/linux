//===- AVRDisassembler.cpp - Disassembler for AVR ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the AVR Disassembler.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "AVRRegisterInfo.h"
#include "AVRSubtarget.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "avr-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for AVR.
class AVRDisassembler : public MCDisassembler {
public:
  AVRDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  virtual ~AVRDisassembler() {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};
}

static MCDisassembler *createAVRDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new AVRDisassembler(STI, Ctx);
}


extern "C" void LLVMInitializeAVRDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheAVRTarget(),
                                         createAVRDisassembler);
}

static DecodeStatus DecodeGPR8RegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address, const void *Decoder) {
  return MCDisassembler::Success;
}

static DecodeStatus DecodeLD8RegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address, const void *Decoder) {
  return MCDisassembler::Success;
}

static DecodeStatus DecodePTRREGSRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address, const void *Decoder) {
  return MCDisassembler::Success;
}

#include "AVRGenDisassemblerTables.inc"

static DecodeStatus readInstruction16(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      uint64_t &Size, uint32_t &Insn) {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  Size = 2;
  Insn = (Bytes[0] << 0) | (Bytes[1] << 8);

  return MCDisassembler::Success;
}

static DecodeStatus readInstruction32(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      uint64_t &Size, uint32_t &Insn) {

  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  Size = 4;
  Insn = (Bytes[0] << 0) | (Bytes[1] << 8) | (Bytes[2] << 16) | (Bytes[3] << 24);

  return MCDisassembler::Success;
}

static const uint8_t *getDecoderTable(uint64_t Size) {

  switch (Size) {
    case 2: return DecoderTable16;
    case 4: return DecoderTable32;
    default: llvm_unreachable("instructions must be 16 or 32-bits");
  }
}

DecodeStatus AVRDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &VStream,
                                             raw_ostream &CStream) const {
  uint32_t Insn;

  DecodeStatus Result;

  // Try decode a 16-bit instruction.
  {
    Result = readInstruction16(Bytes, Address, Size, Insn);

    if (Result == MCDisassembler::Fail) return MCDisassembler::Fail;

    // Try to auto-decode a 16-bit instruction.
    Result = decodeInstruction(getDecoderTable(Size), Instr,
                               Insn, Address, this, STI);

    if (Result != MCDisassembler::Fail)
      return Result;
  }

  // Try decode a 32-bit instruction.
  {
    Result = readInstruction32(Bytes, Address, Size, Insn);

    if (Result == MCDisassembler::Fail) return MCDisassembler::Fail;

    Result = decodeInstruction(getDecoderTable(Size), Instr, Insn,
                               Address, this, STI);

    if (Result != MCDisassembler::Fail) {
      return Result;
    }

    return MCDisassembler::Fail;
  }
}

typedef DecodeStatus (*DecodeFunc)(MCInst &MI, unsigned insn, uint64_t Address,
                                   const void *Decoder);

