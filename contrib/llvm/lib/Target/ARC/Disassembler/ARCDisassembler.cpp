//===- ARCDisassembler.cpp - Disassembler for ARC ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the ARC Disassembler.
///
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "ARCRegisterInfo.h"
#include "MCTargetDesc/ARCMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "arc-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

/// A disassembler class for ARC.
class ARCDisassembler : public MCDisassembler {
public:
  std::unique_ptr<MCInstrInfo const> const MCII;

  ARCDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                  MCInstrInfo const *MCII)
      : MCDisassembler(STI, Ctx), MCII(MCII) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static bool readInstruction32(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint32_t &Insn) {
  Size = 4;
  // Read 2 16-bit values, but swap hi/lo parts.
  Insn =
      (Bytes[0] << 16) | (Bytes[1] << 24) | (Bytes[2] << 0) | (Bytes[3] << 8);
  return true;
}

static bool readInstruction64(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint64_t &Insn) {
  Size = 8;
  Insn = ((uint64_t)Bytes[0] << 16) | ((uint64_t)Bytes[1] << 24) |
         ((uint64_t)Bytes[2] << 0) | ((uint64_t)Bytes[3] << 8) |
         ((uint64_t)Bytes[4] << 48) | ((uint64_t)Bytes[5] << 56) |
         ((uint64_t)Bytes[6] << 32) | ((uint64_t)Bytes[7] << 40);
  return true;
}

static bool readInstruction48(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint64_t &Insn) {
  Size = 6;
  Insn = ((uint64_t)Bytes[0] << 0) | ((uint64_t)Bytes[1] << 8) |
         ((uint64_t)Bytes[2] << 32) | ((uint64_t)Bytes[3] << 40) |
         ((uint64_t)Bytes[4] << 16) | ((uint64_t)Bytes[5] << 24);
  return true;
}

static bool readInstruction16(ArrayRef<uint8_t> Bytes, uint64_t Address,
                              uint64_t &Size, uint32_t &Insn) {
  Size = 2;
  Insn = (Bytes[0] << 0) | (Bytes[1] << 8);
  return true;
}

template <unsigned B>
static DecodeStatus DecodeSignedOperand(MCInst &Inst, unsigned InsnS,
                                        uint64_t Address = 0,
                                        const void *Decoder = nullptr);

template <unsigned B>
static DecodeStatus DecodeFromCyclicRange(MCInst &Inst, unsigned InsnS,
                                        uint64_t Address = 0,
                                        const void *Decoder = nullptr);

template <unsigned B>
static DecodeStatus DecodeBranchTargetS(MCInst &Inst, unsigned InsnS,
                                        uint64_t Address, const void *Decoder);

static DecodeStatus DecodeMEMrs9(MCInst &, unsigned, uint64_t, const void *);

static DecodeStatus DecodeLdLImmInstruction(MCInst &, uint64_t, uint64_t,
                                            const void *);

static DecodeStatus DecodeStLImmInstruction(MCInst &, uint64_t, uint64_t,
                                            const void *);

static DecodeStatus DecodeLdRLImmInstruction(MCInst &, uint64_t, uint64_t,
                                             const void *);

static DecodeStatus DecodeMoveHRegInstruction(MCInst &Inst, uint64_t, uint64_t,
                                              const void *);

static const uint16_t GPR32DecoderTable[] = {
    ARC::R0,  ARC::R1,    ARC::R2,  ARC::R3,   ARC::R4,  ARC::R5,  ARC::R6,
    ARC::R7,  ARC::R8,    ARC::R9,  ARC::R10,  ARC::R11, ARC::R12, ARC::R13,
    ARC::R14, ARC::R15,   ARC::R16, ARC::R17,  ARC::R18, ARC::R19, ARC::R20,
    ARC::R21, ARC::R22,   ARC::R23, ARC::R24,  ARC::R25, ARC::GP,  ARC::FP,
    ARC::SP,  ARC::ILINK, ARC::R30, ARC::BLINK};

static DecodeStatus DecodeGPR32RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const void *Decoder) {
  if (RegNo >= 32) {
    LLVM_DEBUG(dbgs() << "Not a GPR32 register.");
    return MCDisassembler::Fail;
  }

  unsigned Reg = GPR32DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGBR32ShortRegister(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const void *Decoder) {
  // Enumerates registers from ranges [r0-r3],[r12-r15].
  if (RegNo > 3)
    RegNo += 8; // 4 for r12, etc...

  return DecodeGPR32RegisterClass(Inst, RegNo, Address, Decoder);
}

#include "ARCGenDisassemblerTables.inc"

static unsigned decodeCField(unsigned Insn) {
  return fieldFromInstruction(Insn, 6, 6);
}

static unsigned decodeBField(unsigned Insn) {
  return (fieldFromInstruction(Insn, 12, 3) << 3) |
         fieldFromInstruction(Insn, 24, 3);
}

static unsigned decodeAField(unsigned Insn) {
  return fieldFromInstruction(Insn, 0, 6);
}

static DecodeStatus DecodeMEMrs9(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const void *Dec) {
  // We have the 9-bit immediate in the low bits, 6-bit register in high bits.
  unsigned S9 = Insn & 0x1ff;
  unsigned R = (Insn & (0x7fff & ~0x1ff)) >> 9;
  DecodeGPR32RegisterClass(Inst, R, Address, Dec);
  Inst.addOperand(MCOperand::createImm(SignExtend32<9>(S9)));
  return MCDisassembler::Success;
}

static bool DecodeSymbolicOperand(MCInst &Inst, uint64_t Address,
                                  uint64_t Value, const void *Decoder) {
  static const uint64_t atLeast = 2;
  // TODO: Try to force emitter to use MCDisassembler* instead of void*.
  auto Disassembler = static_cast<const MCDisassembler *>(Decoder);
  return (nullptr != Disassembler &&
          Disassembler->tryAddingSymbolicOperand(Inst, Value, Address, true, 0,
                                                 atLeast));
}

static void DecodeSymbolicOperandOff(MCInst &Inst, uint64_t Address,
                                     uint64_t Offset, const void *Decoder) {
  uint64_t nextAddress = Address + Offset;

  if (!DecodeSymbolicOperand(Inst, Address, nextAddress, Decoder))
    Inst.addOperand(MCOperand::createImm(Offset));
}

template <unsigned B>
static DecodeStatus DecodeBranchTargetS(MCInst &Inst, unsigned InsnS,
                                        uint64_t Address, const void *Decoder) {

  static_assert(B > 0, "field is empty");
  DecodeSymbolicOperandOff(Inst, Address, SignExtend32<B>(InsnS), Decoder);
  return MCDisassembler::Success;
}

template <unsigned B>
static DecodeStatus DecodeSignedOperand(MCInst &Inst, unsigned InsnS,
                                        uint64_t /*Address*/,
                                        const void * /*Decoder*/) {

  static_assert(B > 0, "field is empty");
  Inst.addOperand(MCOperand::createImm(
      SignExtend32<B>(maskTrailingOnes<decltype(InsnS)>(B) & InsnS)));
  return MCDisassembler::Success;
}

template <unsigned B>
static DecodeStatus DecodeFromCyclicRange(MCInst &Inst, unsigned InsnS,
                                          uint64_t /*Address*/,
                                          const void * /*Decoder*/) {

  static_assert(B > 0, "field is empty");
  const unsigned max = (1u << B) - 1;
  Inst.addOperand(
      MCOperand::createImm(InsnS < max ? static_cast<int>(InsnS) : -1));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeStLImmInstruction(MCInst &Inst, uint64_t Insn,
                                            uint64_t Address,
                                            const void *Decoder) {
  unsigned SrcC, DstB, LImm;
  DstB = decodeBField(Insn);
  if (DstB != 62) {
    LLVM_DEBUG(dbgs() << "Decoding StLImm found non-limm register.");
    return MCDisassembler::Fail;
  }
  SrcC = decodeCField(Insn);
  DecodeGPR32RegisterClass(Inst, SrcC, Address, Decoder);
  LImm = (Insn >> 32);
  Inst.addOperand(MCOperand::createImm(LImm));
  Inst.addOperand(MCOperand::createImm(0));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeLdLImmInstruction(MCInst &Inst, uint64_t Insn,
                                            uint64_t Address,
                                            const void *Decoder) {
  unsigned DstA, SrcB, LImm;
  LLVM_DEBUG(dbgs() << "Decoding LdLImm:\n");
  SrcB = decodeBField(Insn);
  if (SrcB != 62) {
    LLVM_DEBUG(dbgs() << "Decoding LdLImm found non-limm register.");
    return MCDisassembler::Fail;
  }
  DstA = decodeAField(Insn);
  DecodeGPR32RegisterClass(Inst, DstA, Address, Decoder);
  LImm = (Insn >> 32);
  Inst.addOperand(MCOperand::createImm(LImm));
  Inst.addOperand(MCOperand::createImm(0));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeLdRLImmInstruction(MCInst &Inst, uint64_t Insn,
                                             uint64_t Address,
                                             const void *Decoder) {
  unsigned DstA, SrcB;
  LLVM_DEBUG(dbgs() << "Decoding LdRLimm\n");
  DstA = decodeAField(Insn);
  DecodeGPR32RegisterClass(Inst, DstA, Address, Decoder);
  SrcB = decodeBField(Insn);
  DecodeGPR32RegisterClass(Inst, SrcB, Address, Decoder);
  if (decodeCField(Insn) != 62) {
    LLVM_DEBUG(dbgs() << "Decoding LdRLimm found non-limm register.");
    return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createImm((uint32_t)(Insn >> 32)));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeMoveHRegInstruction(MCInst &Inst, uint64_t Insn,
                                              uint64_t Address,
                                              const void *Decoder) {
  LLVM_DEBUG(dbgs() << "Decoding MOV_S h-register\n");
  using Field = decltype(Insn);
  Field h = fieldFromInstruction(Insn, 5, 3) |
            (fieldFromInstruction(Insn, 0, 2) << 3);
  Field g = fieldFromInstruction(Insn, 8, 3) |
            (fieldFromInstruction(Insn, 3, 2) << 3);

  auto DecodeRegisterOrImm = [&Inst, Address, Decoder](Field RegNum,
                                                       Field Value) {
    if (30 == RegNum) {
      Inst.addOperand(MCOperand::createImm(Value));
      return MCDisassembler::Success;
    }

    return DecodeGPR32RegisterClass(Inst, RegNum, Address, Decoder);
  };

  if (MCDisassembler::Success != DecodeRegisterOrImm(g, 0))
    return MCDisassembler::Fail;

  return DecodeRegisterOrImm(h, Insn >> 16u);
}

DecodeStatus ARCDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &vStream,
                                             raw_ostream &cStream) const {
  MCDisassembler::DecodeStatus Result;
  if (Bytes.size() < 2) {
    Size = 0;
    return Fail;
  }
  uint8_t DecodeByte = (Bytes[1] & 0xF7) >> 3;
  // 0x00 -> 0x07 are 32-bit instructions.
  // 0x08 -> 0x1F are 16-bit instructions.
  if (DecodeByte < 0x08) {
    // 32-bit instruction.
    if (Bytes.size() < 4) {
      // Did we decode garbage?
      Size = 0;
      return Fail;
    }
    if (Bytes.size() >= 8) {
      // Attempt to decode 64-bit instruction.
      uint64_t Insn64;
      if (!readInstruction64(Bytes, Address, Size, Insn64))
        return Fail;
      Result =
          decodeInstruction(DecoderTable64, Instr, Insn64, Address, this, STI);
      if (Success == Result) {
        LLVM_DEBUG(dbgs() << "Successfully decoded 64-bit instruction.");
        return Result;
      }
      LLVM_DEBUG(dbgs() << "Not a 64-bit instruction, falling back to 32-bit.");
    }
    uint32_t Insn32;
    if (!readInstruction32(Bytes, Address, Size, Insn32)) {
      return Fail;
    }
    // Calling the auto-generated decoder function.
    return decodeInstruction(DecoderTable32, Instr, Insn32, Address, this, STI);
  } else {
    if (Bytes.size() >= 6) {
      // Attempt to treat as instr. with limm data.
      uint64_t Insn48;
      if (!readInstruction48(Bytes, Address, Size, Insn48))
        return Fail;
      Result =
          decodeInstruction(DecoderTable48, Instr, Insn48, Address, this, STI);
      if (Success == Result) {
        LLVM_DEBUG(
            dbgs() << "Successfully decoded 16-bit instruction with limm.");
        return Result;
      }
      LLVM_DEBUG(
          dbgs() << "Not a 16-bit instruction with limm, try without it.");
    }

    uint32_t Insn16;
    if (!readInstruction16(Bytes, Address, Size, Insn16))
      return Fail;

    // Calling the auto-generated decoder function.
    return decodeInstruction(DecoderTable16, Instr, Insn16, Address, this, STI);
  }
}

static MCDisassembler *createARCDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new ARCDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" void LLVMInitializeARCDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheARCTarget(),
                                         createARCDisassembler);
}
