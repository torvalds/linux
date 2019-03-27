//===- LanaiDisassembler.cpp - Disassembler for Lanai -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the Lanai Disassembler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_DISASSEMBLER_LANAIDISASSEMBLER_H
#define LLVM_LIB_TARGET_LANAI_DISASSEMBLER_LANAIDISASSEMBLER_H

#define DEBUG_TYPE "lanai-disassembler"

#include "llvm/MC/MCDisassembler/MCDisassembler.h"

namespace llvm {

class LanaiDisassembler : public MCDisassembler {
public:
  LanaiDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx);

  ~LanaiDisassembler() override = default;

  // getInstruction - See MCDisassembler.
  MCDisassembler::DecodeStatus
  getInstruction(MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes,
                 uint64_t Address, raw_ostream &VStream,
                 raw_ostream &CStream) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_DISASSEMBLER_LANAIDISASSEMBLER_H
