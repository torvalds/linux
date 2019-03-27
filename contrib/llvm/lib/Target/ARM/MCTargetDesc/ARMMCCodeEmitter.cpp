//===-- ARM/ARMMCCodeEmitter.cpp - Convert ARM code to machine code -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARMMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMFixupKinds.h"
#include "MCTargetDesc/ARMMCExpr.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted.");
STATISTIC(MCNumCPRelocations, "Number of constant pool relocations created.");

namespace {

class ARMMCCodeEmitter : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  const MCContext &CTX;
  bool IsLittleEndian;

public:
  ARMMCCodeEmitter(const MCInstrInfo &mcii, MCContext &ctx, bool IsLittle)
    : MCII(mcii), CTX(ctx), IsLittleEndian(IsLittle) {
  }
  ARMMCCodeEmitter(const ARMMCCodeEmitter &) = delete;
  ARMMCCodeEmitter &operator=(const ARMMCCodeEmitter &) = delete;
  ~ARMMCCodeEmitter() override = default;

  bool isThumb(const MCSubtargetInfo &STI) const {
    return STI.getFeatureBits()[ARM::ModeThumb];
  }

  bool isThumb2(const MCSubtargetInfo &STI) const {
    return isThumb(STI) && STI.getFeatureBits()[ARM::FeatureThumb2];
  }

  bool isTargetMachO(const MCSubtargetInfo &STI) const {
    const Triple &TT = STI.getTargetTriple();
    return TT.isOSBinFormatMachO();
  }

  unsigned getMachineSoImmOpValue(unsigned SoImm) const;

  // getBinaryCodeForInstr - TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// getMachineOpValue - Return binary encoding of operand. If the machine
  /// operand requires relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI,const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  /// getHiLo16ImmOpValue - Return the encoding for the hi / low 16-bit of
  /// the specified operand. This is used for operands with :lower16: and
  /// :upper16: prefixes.
  uint32_t getHiLo16ImmOpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  bool EncodeAddrModeOpValues(const MCInst &MI, unsigned OpIdx,
                              unsigned &Reg, unsigned &Imm,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  /// getThumbBLTargetOpValue - Return encoding info for Thumb immediate
  /// BL branch target.
  uint32_t getThumbBLTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getThumbBLXTargetOpValue - Return encoding info for Thumb immediate
  /// BLX branch target.
  uint32_t getThumbBLXTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const;

  /// getThumbBRTargetOpValue - Return encoding info for Thumb branch target.
  uint32_t getThumbBRTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getThumbBCCTargetOpValue - Return encoding info for Thumb branch target.
  uint32_t getThumbBCCTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const;

  /// getThumbCBTargetOpValue - Return encoding info for Thumb branch target.
  uint32_t getThumbCBTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getBranchTargetOpValue - Return encoding info for 24-bit immediate
  /// branch target.
  uint32_t getBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;

  /// getThumbBranchTargetOpValue - Return encoding info for 24-bit
  /// immediate Thumb2 direct branch target.
  uint32_t getThumbBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const;

  /// getARMBranchTargetOpValue - Return encoding info for 24-bit immediate
  /// branch target.
  uint32_t getARMBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;
  uint32_t getARMBLTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getARMBLXTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;

  /// getAdrLabelOpValue - Return encoding info for 12-bit immediate
  /// ADR label target.
  uint32_t getAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  uint32_t getThumbAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  uint32_t getT2AdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;


  /// getAddrModeImm12OpValue - Return encoding info for 'reg +/- imm12'
  /// operand.
  uint32_t getAddrModeImm12OpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getThumbAddrModeRegRegOpValue - Return encoding for 'reg + reg' operand.
  uint32_t getThumbAddrModeRegRegOpValue(const MCInst &MI, unsigned OpIdx,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const;

  /// getT2AddrModeImm8s4OpValue - Return encoding info for 'reg +/- imm8<<2'
  /// operand.
  uint32_t getT2AddrModeImm8s4OpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getT2AddrModeImm0_1020s4OpValue - Return encoding info for 'reg + imm8<<2'
  /// operand.
  uint32_t getT2AddrModeImm0_1020s4OpValue(const MCInst &MI, unsigned OpIdx,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// getT2Imm8s4OpValue - Return encoding info for '+/- imm8<<2'
  /// operand.
  uint32_t getT2Imm8s4OpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;


  /// getLdStSORegOpValue - Return encoding info for 'reg +/- reg shop imm'
  /// operand as needed by load/store instructions.
  uint32_t getLdStSORegOpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getLdStmModeOpValue - Return encoding for load/store multiple mode.
  uint32_t getLdStmModeOpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const {
    ARM_AM::AMSubMode Mode = (ARM_AM::AMSubMode)MI.getOperand(OpIdx).getImm();
    switch (Mode) {
    default: llvm_unreachable("Unknown addressing sub-mode!");
    case ARM_AM::da: return 0;
    case ARM_AM::ia: return 1;
    case ARM_AM::db: return 2;
    case ARM_AM::ib: return 3;
    }
  }

  /// getShiftOp - Return the shift opcode (bit[6:5]) of the immediate value.
  ///
  unsigned getShiftOp(ARM_AM::ShiftOpc ShOpc) const {
    switch (ShOpc) {
    case ARM_AM::no_shift:
    case ARM_AM::lsl: return 0;
    case ARM_AM::lsr: return 1;
    case ARM_AM::asr: return 2;
    case ARM_AM::ror:
    case ARM_AM::rrx: return 3;
    }
    llvm_unreachable("Invalid ShiftOpc!");
  }

  /// getAddrMode2OffsetOpValue - Return encoding for am2offset operands.
  uint32_t getAddrMode2OffsetOpValue(const MCInst &MI, unsigned OpIdx,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  /// getPostIdxRegOpValue - Return encoding for postidx_reg operands.
  uint32_t getPostIdxRegOpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  /// getAddrMode3OffsetOpValue - Return encoding for am3offset operands.
  uint32_t getAddrMode3OffsetOpValue(const MCInst &MI, unsigned OpIdx,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  /// getAddrMode3OpValue - Return encoding for addrmode3 operands.
  uint32_t getAddrMode3OpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getAddrModeThumbSPOpValue - Return encoding info for 'reg +/- imm12'
  /// operand.
  uint32_t getAddrModeThumbSPOpValue(const MCInst &MI, unsigned OpIdx,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  /// getAddrModeISOpValue - Encode the t_addrmode_is# operands.
  uint32_t getAddrModeISOpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  /// getAddrModePCOpValue - Return encoding for t_addrmode_pc operands.
  uint32_t getAddrModePCOpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  /// getAddrMode5OpValue - Return encoding info for 'reg +/- (imm8 << 2)' operand.
  uint32_t getAddrMode5OpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getAddrMode5FP16OpValue - Return encoding info for 'reg +/- (imm8 << 1)' operand.
  uint32_t getAddrMode5FP16OpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getCCOutOpValue - Return encoding of the 's' bit.
  unsigned getCCOutOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
    // The operand is either reg0 or CPSR. The 's' bit is encoded as '0' or
    // '1' respectively.
    return MI.getOperand(Op).getReg() == ARM::CPSR;
  }

  /// getSOImmOpValue - Return an encoded 12-bit shifted-immediate value.
  unsigned getSOImmOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
    const MCOperand &MO = MI.getOperand(Op);

    // We expect MO to be an immediate or an expression,
    // if it is an immediate - that's fine, just encode the value.
    // Otherwise - create a Fixup.
    if (MO.isExpr()) {
      const MCExpr *Expr = MO.getExpr();
      // In instruction code this value always encoded as lowest 12 bits,
      // so we don't have to perform any specific adjustments.
      // Due to requirements of relocatable records we have to use FK_Data_4.
      // See ARMELFObjectWriter::ExplicitRelSym and
      //     ARMELFObjectWriter::GetRelocTypeInner for more details.
      MCFixupKind Kind = MCFixupKind(FK_Data_4);
      Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));
      return 0;
    }

    unsigned SoImm = MO.getImm();
    int SoImmVal = ARM_AM::getSOImmVal(SoImm);
    assert(SoImmVal != -1 && "Not a valid so_imm value!");

    // Encode rotate_imm.
    unsigned Binary = (ARM_AM::getSOImmValRot((unsigned)SoImmVal) >> 1)
      << ARMII::SoRotImmShift;

    // Encode immed_8.
    Binary |= ARM_AM::getSOImmValImm((unsigned)SoImmVal);
    return Binary;
  }

  unsigned getModImmOpValue(const MCInst &MI, unsigned Op,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &ST) const {
    const MCOperand &MO = MI.getOperand(Op);

    // Support for fixups (MCFixup)
    if (MO.isExpr()) {
      const MCExpr *Expr = MO.getExpr();
      // Fixups resolve to plain values that need to be encoded.
      MCFixupKind Kind = MCFixupKind(ARM::fixup_arm_mod_imm);
      Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));
      return 0;
    }

    // Immediate is already in its encoded format
    return MO.getImm();
  }

  /// getT2SOImmOpValue - Return an encoded 12-bit shifted-immediate value.
  unsigned getT2SOImmOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
    const MCOperand &MO = MI.getOperand(Op);

    // Support for fixups (MCFixup)
    if (MO.isExpr()) {
      const MCExpr *Expr = MO.getExpr();
      // Fixups resolve to plain values that need to be encoded.
      MCFixupKind Kind = MCFixupKind(ARM::fixup_t2_so_imm);
      Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));
      return 0;
    }
    unsigned SoImm = MO.getImm();
    unsigned Encoded =  ARM_AM::getT2SOImmVal(SoImm);
    assert(Encoded != ~0U && "Not a Thumb2 so_imm value?");
    return Encoded;
  }

  unsigned getT2AddrModeSORegOpValue(const MCInst &MI, unsigned OpNum,
    SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const;
  unsigned getT2AddrModeImm8OpValue(const MCInst &MI, unsigned OpNum,
    SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const;
  unsigned getT2AddrModeImm8OffsetOpValue(const MCInst &MI, unsigned OpNum,
    SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const;

  /// getSORegOpValue - Return an encoded so_reg shifted register value.
  unsigned getSORegRegOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;
  unsigned getSORegImmOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;
  unsigned getT2SORegOpValue(const MCInst &MI, unsigned Op,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getNEONVcvtImm32OpValue(const MCInst &MI, unsigned Op,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const {
    return 64 - MI.getOperand(Op).getImm();
  }

  unsigned getBitfieldInvertedMaskOpValue(const MCInst &MI, unsigned Op,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const;

  unsigned getRegisterListOpValue(const MCInst &MI, unsigned Op,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;
  unsigned getAddrMode6AddressOpValue(const MCInst &MI, unsigned Op,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const;
  unsigned getAddrMode6OneLane32AddressOpValue(const MCInst &MI, unsigned Op,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const;
  unsigned getAddrMode6DupAddressOpValue(const MCInst &MI, unsigned Op,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const;
  unsigned getAddrMode6OffsetOpValue(const MCInst &MI, unsigned Op,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  unsigned getShiftRight8Imm(const MCInst &MI, unsigned Op,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  unsigned getShiftRight16Imm(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getShiftRight32Imm(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getShiftRight64Imm(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  unsigned getThumbSRImmOpValue(const MCInst &MI, unsigned Op,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned NEONThumb2DataIPostEncoder(const MCInst &MI,
                                      unsigned EncodedValue,
                                      const MCSubtargetInfo &STI) const;
  unsigned NEONThumb2LoadStorePostEncoder(const MCInst &MI,
                                          unsigned EncodedValue,
                                          const MCSubtargetInfo &STI) const;
  unsigned NEONThumb2DupPostEncoder(const MCInst &MI,
                                    unsigned EncodedValue,
                                    const MCSubtargetInfo &STI) const;
  unsigned NEONThumb2V8PostEncoder(const MCInst &MI,
                                   unsigned EncodedValue,
                                   const MCSubtargetInfo &STI) const;

  unsigned VFPThumb2PostEncoder(const MCInst &MI,
                                unsigned EncodedValue,
                                const MCSubtargetInfo &STI) const;

  void EmitByte(unsigned char C, raw_ostream &OS) const {
    OS << (char)C;
  }

  void EmitConstant(uint64_t Val, unsigned Size, raw_ostream &OS) const {
    // Output the constant in little endian byte order.
    for (unsigned i = 0; i != Size; ++i) {
      unsigned Shift = IsLittleEndian ? i * 8 : (Size - 1 - i) * 8;
      EmitByte((Val >> Shift) & 0xff, OS);
    }
  }

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

} // end anonymous namespace

/// NEONThumb2DataIPostEncoder - Post-process encoded NEON data-processing
/// instructions, and rewrite them to their Thumb2 form if we are currently in
/// Thumb2 mode.
unsigned ARMMCCodeEmitter::NEONThumb2DataIPostEncoder(const MCInst &MI,
                                                 unsigned EncodedValue,
                                                 const MCSubtargetInfo &STI) const {
  if (isThumb2(STI)) {
    // NEON Thumb2 data-processsing encodings are very simple: bit 24 is moved
    // to bit 12 of the high half-word (i.e. bit 28), and bits 27-24 are
    // set to 1111.
    unsigned Bit24 = EncodedValue & 0x01000000;
    unsigned Bit28 = Bit24 << 4;
    EncodedValue &= 0xEFFFFFFF;
    EncodedValue |= Bit28;
    EncodedValue |= 0x0F000000;
  }

  return EncodedValue;
}

/// NEONThumb2LoadStorePostEncoder - Post-process encoded NEON load/store
/// instructions, and rewrite them to their Thumb2 form if we are currently in
/// Thumb2 mode.
unsigned ARMMCCodeEmitter::NEONThumb2LoadStorePostEncoder(const MCInst &MI,
                                                 unsigned EncodedValue,
                                                 const MCSubtargetInfo &STI) const {
  if (isThumb2(STI)) {
    EncodedValue &= 0xF0FFFFFF;
    EncodedValue |= 0x09000000;
  }

  return EncodedValue;
}

/// NEONThumb2DupPostEncoder - Post-process encoded NEON vdup
/// instructions, and rewrite them to their Thumb2 form if we are currently in
/// Thumb2 mode.
unsigned ARMMCCodeEmitter::NEONThumb2DupPostEncoder(const MCInst &MI,
                                                 unsigned EncodedValue,
                                                 const MCSubtargetInfo &STI) const {
  if (isThumb2(STI)) {
    EncodedValue &= 0x00FFFFFF;
    EncodedValue |= 0xEE000000;
  }

  return EncodedValue;
}

/// Post-process encoded NEON v8 instructions, and rewrite them to Thumb2 form
/// if we are in Thumb2.
unsigned ARMMCCodeEmitter::NEONThumb2V8PostEncoder(const MCInst &MI,
                                                 unsigned EncodedValue,
                                                 const MCSubtargetInfo &STI) const {
  if (isThumb2(STI)) {
    EncodedValue |= 0xC000000; // Set bits 27-26
  }

  return EncodedValue;
}

/// VFPThumb2PostEncoder - Post-process encoded VFP instructions and rewrite
/// them to their Thumb2 form if we are currently in Thumb2 mode.
unsigned ARMMCCodeEmitter::
VFPThumb2PostEncoder(const MCInst &MI, unsigned EncodedValue,
                     const MCSubtargetInfo &STI) const {
  if (isThumb2(STI)) {
    EncodedValue &= 0x0FFFFFFF;
    EncodedValue |= 0xE0000000;
  }
  return EncodedValue;
}

/// getMachineOpValue - Return binary encoding of operand. If the machine
/// operand requires relocation, record the relocation and return zero.
unsigned ARMMCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    unsigned RegNo = CTX.getRegisterInfo()->getEncodingValue(Reg);

    // Q registers are encoded as 2x their register number.
    switch (Reg) {
    default:
      return RegNo;
    case ARM::Q0:  case ARM::Q1:  case ARM::Q2:  case ARM::Q3:
    case ARM::Q4:  case ARM::Q5:  case ARM::Q6:  case ARM::Q7:
    case ARM::Q8:  case ARM::Q9:  case ARM::Q10: case ARM::Q11:
    case ARM::Q12: case ARM::Q13: case ARM::Q14: case ARM::Q15:
      return 2 * RegNo;
    }
  } else if (MO.isImm()) {
    return static_cast<unsigned>(MO.getImm());
  } else if (MO.isFPImm()) {
    return static_cast<unsigned>(APFloat(MO.getFPImm())
                     .bitcastToAPInt().getHiBits(32).getLimitedValue());
  }

  llvm_unreachable("Unable to encode MCOperand!");
}

/// getAddrModeImmOpValue - Return encoding info for 'reg +/- imm' operand.
bool ARMMCCodeEmitter::
EncodeAddrModeOpValues(const MCInst &MI, unsigned OpIdx, unsigned &Reg,
                       unsigned &Imm, SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO  = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);

  Reg = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());

  int32_t SImm = MO1.getImm();
  bool isAdd = true;

  // Special value for #-0
  if (SImm == INT32_MIN) {
    SImm = 0;
    isAdd = false;
  }

  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (SImm < 0) {
    SImm = -SImm;
    isAdd = false;
  }

  Imm = SImm;
  return isAdd;
}

/// getBranchTargetOpValue - Helper function to get the branch target operand,
/// which is either an immediate or requires a fixup.
static uint32_t getBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                       unsigned FixupKind,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm()) return MO.getImm();
  assert(MO.isExpr() && "Unexpected branch target type!");
  const MCExpr *Expr = MO.getExpr();
  MCFixupKind Kind = MCFixupKind(FixupKind);
  Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

  // All of the information is in the fixup.
  return 0;
}

// Thumb BL and BLX use a strange offset encoding where bits 22 and 21 are
// determined by negating them and XOR'ing them with bit 23.
static int32_t encodeThumbBLOffset(int32_t offset) {
  offset >>= 1;
  uint32_t S  = (offset & 0x800000) >> 23;
  uint32_t J1 = (offset & 0x400000) >> 22;
  uint32_t J2 = (offset & 0x200000) >> 21;
  J1 = (~J1 & 0x1);
  J2 = (~J2 & 0x1);
  J1 ^= S;
  J2 ^= S;

  offset &= ~0x600000;
  offset |= J1 << 22;
  offset |= J2 << 21;

  return offset;
}

/// getThumbBLTargetOpValue - Return encoding info for immediate branch target.
uint32_t ARMMCCodeEmitter::
getThumbBLTargetOpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_bl,
                                    Fixups, STI);
  return encodeThumbBLOffset(MO.getImm());
}

/// getThumbBLXTargetOpValue - Return encoding info for Thumb immediate
/// BLX branch target.
uint32_t ARMMCCodeEmitter::
getThumbBLXTargetOpValue(const MCInst &MI, unsigned OpIdx,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_blx,
                                    Fixups, STI);
  return encodeThumbBLOffset(MO.getImm());
}

/// getThumbBRTargetOpValue - Return encoding info for Thumb branch target.
uint32_t ARMMCCodeEmitter::
getThumbBRTargetOpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_br,
                                    Fixups, STI);
  return (MO.getImm() >> 1);
}

/// getThumbBCCTargetOpValue - Return encoding info for Thumb branch target.
uint32_t ARMMCCodeEmitter::
getThumbBCCTargetOpValue(const MCInst &MI, unsigned OpIdx,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_bcc,
                                    Fixups, STI);
  return (MO.getImm() >> 1);
}

/// getThumbCBTargetOpValue - Return encoding info for Thumb branch target.
uint32_t ARMMCCodeEmitter::
getThumbCBTargetOpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_cb, Fixups, STI);
  return (MO.getImm() >> 1);
}

/// Return true if this branch has a non-always predication
static bool HasConditionalBranch(const MCInst &MI) {
  int NumOp = MI.getNumOperands();
  if (NumOp >= 2) {
    for (int i = 0; i < NumOp-1; ++i) {
      const MCOperand &MCOp1 = MI.getOperand(i);
      const MCOperand &MCOp2 = MI.getOperand(i + 1);
      if (MCOp1.isImm() && MCOp2.isReg() &&
          (MCOp2.getReg() == 0 || MCOp2.getReg() == ARM::CPSR)) {
        if (ARMCC::CondCodes(MCOp1.getImm()) != ARMCC::AL)
          return true;
      }
    }
  }
  return false;
}

/// getBranchTargetOpValue - Return encoding info for 24-bit immediate branch
/// target.
uint32_t ARMMCCodeEmitter::
getBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  // FIXME: This really, really shouldn't use TargetMachine. We don't want
  // coupling between MC and TM anywhere we can help it.
  if (isThumb2(STI))
    return
      ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_t2_condbranch, Fixups, STI);
  return getARMBranchTargetOpValue(MI, OpIdx, Fixups, STI);
}

/// getBranchTargetOpValue - Return encoding info for 24-bit immediate branch
/// target.
uint32_t ARMMCCodeEmitter::
getARMBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr()) {
    if (HasConditionalBranch(MI))
      return ::getBranchTargetOpValue(MI, OpIdx,
                                      ARM::fixup_arm_condbranch, Fixups, STI);
    return ::getBranchTargetOpValue(MI, OpIdx,
                                    ARM::fixup_arm_uncondbranch, Fixups, STI);
  }

  return MO.getImm() >> 2;
}

uint32_t ARMMCCodeEmitter::
getARMBLTargetOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr()) {
    if (HasConditionalBranch(MI))
      return ::getBranchTargetOpValue(MI, OpIdx,
                                      ARM::fixup_arm_condbl, Fixups, STI);
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_uncondbl, Fixups, STI);
  }

  return MO.getImm() >> 2;
}

uint32_t ARMMCCodeEmitter::
getARMBLXTargetOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_blx, Fixups, STI);

  return MO.getImm() >> 1;
}

/// getUnconditionalBranchTargetOpValue - Return encoding info for 24-bit
/// immediate branch target.
uint32_t ARMMCCodeEmitter::getThumbBranchTargetOpValue(
    const MCInst &MI, unsigned OpIdx, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  unsigned Val = 0;
  const MCOperand MO = MI.getOperand(OpIdx);

  if(MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_t2_uncondbranch, Fixups, STI);
  else
    Val = MO.getImm() >> 1;

  bool I  = (Val & 0x800000);
  bool J1 = (Val & 0x400000);
  bool J2 = (Val & 0x200000);
  if (I ^ J1)
    Val &= ~0x400000;
  else
    Val |= 0x400000;

  if (I ^ J2)
    Val &= ~0x200000;
  else
    Val |= 0x200000;

  return Val;
}

/// getAdrLabelOpValue - Return encoding info for 12-bit shifted-immediate
/// ADR label target.
uint32_t ARMMCCodeEmitter::
getAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_adr_pcrel_12,
                                    Fixups, STI);
  int64_t offset = MO.getImm();
  uint32_t Val = 0x2000;

  int SoImmVal;
  if (offset == INT32_MIN) {
    Val = 0x1000;
    SoImmVal = 0;
  } else if (offset < 0) {
    Val = 0x1000;
    offset *= -1;
    SoImmVal = ARM_AM::getSOImmVal(offset);
    if(SoImmVal == -1) {
      Val = 0x2000;
      offset *= -1;
      SoImmVal = ARM_AM::getSOImmVal(offset);
    }
  } else {
    SoImmVal = ARM_AM::getSOImmVal(offset);
    if(SoImmVal == -1) {
      Val = 0x1000;
      offset *= -1;
      SoImmVal = ARM_AM::getSOImmVal(offset);
    }
  }

  assert(SoImmVal != -1 && "Not a valid so_imm value!");

  Val |= SoImmVal;
  return Val;
}

/// getT2AdrLabelOpValue - Return encoding info for 12-bit immediate ADR label
/// target.
uint32_t ARMMCCodeEmitter::
getT2AdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_t2_adr_pcrel_12,
                                    Fixups, STI);
  int32_t Val = MO.getImm();
  if (Val == INT32_MIN)
    Val = 0x1000;
  else if (Val < 0) {
    Val *= -1;
    Val |= 0x1000;
  }
  return Val;
}

/// getThumbAdrLabelOpValue - Return encoding info for 8-bit immediate ADR label
/// target.
uint32_t ARMMCCodeEmitter::
getThumbAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_thumb_adr_pcrel_10,
                                    Fixups, STI);
  return MO.getImm();
}

/// getThumbAddrModeRegRegOpValue - Return encoding info for 'reg + reg'
/// operand.
uint32_t ARMMCCodeEmitter::
getThumbAddrModeRegRegOpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &,
                              const MCSubtargetInfo &STI) const {
  // [Rn, Rm]
  //   {5-3} = Rm
  //   {2-0} = Rn
  const MCOperand &MO1 = MI.getOperand(OpIdx);
  const MCOperand &MO2 = MI.getOperand(OpIdx + 1);
  unsigned Rn = CTX.getRegisterInfo()->getEncodingValue(MO1.getReg());
  unsigned Rm = CTX.getRegisterInfo()->getEncodingValue(MO2.getReg());
  return (Rm << 3) | Rn;
}

/// getAddrModeImm12OpValue - Return encoding info for 'reg +/- imm12' operand.
uint32_t ARMMCCodeEmitter::
getAddrModeImm12OpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  // {17-13} = reg
  // {12}    = (U)nsigned (add == '1', sub == '0')
  // {11-0}  = imm12
  unsigned Reg, Imm12;
  bool isAdd = true;
  // If The first operand isn't a register, we have a label reference.
  const MCOperand &MO = MI.getOperand(OpIdx);
  if (!MO.isReg()) {
    Reg = CTX.getRegisterInfo()->getEncodingValue(ARM::PC);   // Rn is PC.
    Imm12 = 0;

    if (MO.isExpr()) {
      const MCExpr *Expr = MO.getExpr();
      isAdd = false ; // 'U' bit is set as part of the fixup.

      MCFixupKind Kind;
      if (isThumb2(STI))
        Kind = MCFixupKind(ARM::fixup_t2_ldst_pcrel_12);
      else
        Kind = MCFixupKind(ARM::fixup_arm_ldst_pcrel_12);
      Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

      ++MCNumCPRelocations;
    } else {
      Reg = ARM::PC;
      int32_t Offset = MO.getImm();
      if (Offset == INT32_MIN) {
        Offset = 0;
        isAdd = false;
      } else if (Offset < 0) {
        Offset *= -1;
        isAdd = false;
      }
      Imm12 = Offset;
    }
  } else
    isAdd = EncodeAddrModeOpValues(MI, OpIdx, Reg, Imm12, Fixups, STI);

  uint32_t Binary = Imm12 & 0xfff;
  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (isAdd)
    Binary |= (1 << 12);
  Binary |= (Reg << 13);
  return Binary;
}

/// getT2Imm8s4OpValue - Return encoding info for
/// '+/- imm8<<2' operand.
uint32_t ARMMCCodeEmitter::
getT2Imm8s4OpValue(const MCInst &MI, unsigned OpIdx,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  // FIXME: The immediate operand should have already been encoded like this
  // before ever getting here. The encoder method should just need to combine
  // the MI operands for the register and the offset into a single
  // representation for the complex operand in the .td file. This isn't just
  // style, unfortunately. As-is, we can't represent the distinct encoding
  // for #-0.

  // {8}    = (U)nsigned (add == '1', sub == '0')
  // {7-0}  = imm8
  int32_t Imm8 = MI.getOperand(OpIdx).getImm();
  bool isAdd = Imm8 >= 0;

  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (Imm8 < 0)
    Imm8 = -(uint32_t)Imm8;

  // Scaled by 4.
  Imm8 /= 4;

  uint32_t Binary = Imm8 & 0xff;
  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (isAdd)
    Binary |= (1 << 8);
  return Binary;
}

/// getT2AddrModeImm8s4OpValue - Return encoding info for
/// 'reg +/- imm8<<2' operand.
uint32_t ARMMCCodeEmitter::
getT2AddrModeImm8s4OpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  // {12-9} = reg
  // {8}    = (U)nsigned (add == '1', sub == '0')
  // {7-0}  = imm8
  unsigned Reg, Imm8;
  bool isAdd = true;
  // If The first operand isn't a register, we have a label reference.
  const MCOperand &MO = MI.getOperand(OpIdx);
  if (!MO.isReg()) {
    Reg = CTX.getRegisterInfo()->getEncodingValue(ARM::PC);   // Rn is PC.
    Imm8 = 0;
    isAdd = false ; // 'U' bit is set as part of the fixup.

    assert(MO.isExpr() && "Unexpected machine operand type!");
    const MCExpr *Expr = MO.getExpr();
    MCFixupKind Kind = MCFixupKind(ARM::fixup_t2_pcrel_10);
    Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

    ++MCNumCPRelocations;
  } else
    isAdd = EncodeAddrModeOpValues(MI, OpIdx, Reg, Imm8, Fixups, STI);

  // FIXME: The immediate operand should have already been encoded like this
  // before ever getting here. The encoder method should just need to combine
  // the MI operands for the register and the offset into a single
  // representation for the complex operand in the .td file. This isn't just
  // style, unfortunately. As-is, we can't represent the distinct encoding
  // for #-0.
  uint32_t Binary = (Imm8 >> 2) & 0xff;
  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (isAdd)
    Binary |= (1 << 8);
  Binary |= (Reg << 9);
  return Binary;
}

/// getT2AddrModeImm0_1020s4OpValue - Return encoding info for
/// 'reg + imm8<<2' operand.
uint32_t ARMMCCodeEmitter::
getT2AddrModeImm0_1020s4OpValue(const MCInst &MI, unsigned OpIdx,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const {
  // {11-8} = reg
  // {7-0}  = imm8
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  unsigned Reg = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Imm8 = MO1.getImm();
  return (Reg << 8) | Imm8;
}

uint32_t
ARMMCCodeEmitter::getHiLo16ImmOpValue(const MCInst &MI, unsigned OpIdx,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // {20-16} = imm{15-12}
  // {11-0}  = imm{11-0}
  const MCOperand &MO = MI.getOperand(OpIdx);
  if (MO.isImm())
    // Hi / lo 16 bits already extracted during earlier passes.
    return static_cast<unsigned>(MO.getImm());

  // Handle :upper16: and :lower16: assembly prefixes.
  const MCExpr *E = MO.getExpr();
  MCFixupKind Kind;
  if (E->getKind() == MCExpr::Target) {
    const ARMMCExpr *ARM16Expr = cast<ARMMCExpr>(E);
    E = ARM16Expr->getSubExpr();

    if (const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(E)) {
      const int64_t Value = MCE->getValue();
      if (Value > UINT32_MAX)
        report_fatal_error("constant value truncated (limited to 32-bit)");

      switch (ARM16Expr->getKind()) {
      case ARMMCExpr::VK_ARM_HI16:
        return (int32_t(Value) & 0xffff0000) >> 16;
      case ARMMCExpr::VK_ARM_LO16:
        return (int32_t(Value) & 0x0000ffff);
      default: llvm_unreachable("Unsupported ARMFixup");
      }
    }

    switch (ARM16Expr->getKind()) {
    default: llvm_unreachable("Unsupported ARMFixup");
    case ARMMCExpr::VK_ARM_HI16:
      Kind = MCFixupKind(isThumb(STI) ? ARM::fixup_t2_movt_hi16
                                      : ARM::fixup_arm_movt_hi16);
      break;
    case ARMMCExpr::VK_ARM_LO16:
      Kind = MCFixupKind(isThumb(STI) ? ARM::fixup_t2_movw_lo16
                                      : ARM::fixup_arm_movw_lo16);
      break;
    }

    Fixups.push_back(MCFixup::create(0, E, Kind, MI.getLoc()));
    return 0;
  }
  // If the expression doesn't have :upper16: or :lower16: on it,
  // it's just a plain immediate expression, previously those evaluated to
  // the lower 16 bits of the expression regardless of whether
  // we have a movt or a movw, but that led to misleadingly results.
  // This is disallowed in the AsmParser in validateInstruction()
  // so this should never happen.
  llvm_unreachable("expression without :upper16: or :lower16:");
}

uint32_t ARMMCCodeEmitter::
getLdStSORegOpValue(const MCInst &MI, unsigned OpIdx,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx+1);
  const MCOperand &MO2 = MI.getOperand(OpIdx+2);
  unsigned Rn = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Rm = CTX.getRegisterInfo()->getEncodingValue(MO1.getReg());
  unsigned ShImm = ARM_AM::getAM2Offset(MO2.getImm());
  bool isAdd = ARM_AM::getAM2Op(MO2.getImm()) == ARM_AM::add;
  ARM_AM::ShiftOpc ShOp = ARM_AM::getAM2ShiftOpc(MO2.getImm());
  unsigned SBits = getShiftOp(ShOp);

  // While "lsr #32" and "asr #32" exist, they are encoded with a 0 in the shift
  // amount. However, it would be an easy mistake to make so check here.
  assert((ShImm & ~0x1f) == 0 && "Out of range shift amount");

  // {16-13} = Rn
  // {12}    = isAdd
  // {11-0}  = shifter
  //  {3-0}  = Rm
  //  {4}    = 0
  //  {6-5}  = type
  //  {11-7} = imm
  uint32_t Binary = Rm;
  Binary |= Rn << 13;
  Binary |= SBits << 5;
  Binary |= ShImm << 7;
  if (isAdd)
    Binary |= 1 << 12;
  return Binary;
}

uint32_t ARMMCCodeEmitter::
getAddrMode2OffsetOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  // {13}     1 == imm12, 0 == Rm
  // {12}     isAdd
  // {11-0}   imm12/Rm
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx+1);
  unsigned Imm = MO1.getImm();
  bool isAdd = ARM_AM::getAM2Op(Imm) == ARM_AM::add;
  bool isReg = MO.getReg() != 0;
  uint32_t Binary = ARM_AM::getAM2Offset(Imm);
  // if reg +/- reg, Rm will be non-zero. Otherwise, we have reg +/- imm12
  if (isReg) {
    ARM_AM::ShiftOpc ShOp = ARM_AM::getAM2ShiftOpc(Imm);
    Binary <<= 7;                    // Shift amount is bits [11:7]
    Binary |= getShiftOp(ShOp) << 5; // Shift type is bits [6:5]
    Binary |= CTX.getRegisterInfo()->getEncodingValue(MO.getReg()); // Rm is bits [3:0]
  }
  return Binary | (isAdd << 12) | (isReg << 13);
}

uint32_t ARMMCCodeEmitter::
getPostIdxRegOpValue(const MCInst &MI, unsigned OpIdx,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  // {4}      isAdd
  // {3-0}    Rm
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx+1);
  bool isAdd = MO1.getImm() != 0;
  return CTX.getRegisterInfo()->getEncodingValue(MO.getReg()) | (isAdd << 4);
}

uint32_t ARMMCCodeEmitter::
getAddrMode3OffsetOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  // {9}      1 == imm8, 0 == Rm
  // {8}      isAdd
  // {7-4}    imm7_4/zero
  // {3-0}    imm3_0/Rm
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx+1);
  unsigned Imm = MO1.getImm();
  bool isAdd = ARM_AM::getAM3Op(Imm) == ARM_AM::add;
  bool isImm = MO.getReg() == 0;
  uint32_t Imm8 = ARM_AM::getAM3Offset(Imm);
  // if reg +/- reg, Rm will be non-zero. Otherwise, we have reg +/- imm8
  if (!isImm)
    Imm8 = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  return Imm8 | (isAdd << 8) | (isImm << 9);
}

uint32_t ARMMCCodeEmitter::
getAddrMode3OpValue(const MCInst &MI, unsigned OpIdx,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  // {13}     1 == imm8, 0 == Rm
  // {12-9}   Rn
  // {8}      isAdd
  // {7-4}    imm7_4/zero
  // {3-0}    imm3_0/Rm
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx+1);
  const MCOperand &MO2 = MI.getOperand(OpIdx+2);

  // If The first operand isn't a register, we have a label reference.
  if (!MO.isReg()) {
    unsigned Rn = CTX.getRegisterInfo()->getEncodingValue(ARM::PC);   // Rn is PC.

    assert(MO.isExpr() && "Unexpected machine operand type!");
    const MCExpr *Expr = MO.getExpr();
    MCFixupKind Kind = MCFixupKind(ARM::fixup_arm_pcrel_10_unscaled);
    Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

    ++MCNumCPRelocations;
    return (Rn << 9) | (1 << 13);
  }
  unsigned Rn = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Imm = MO2.getImm();
  bool isAdd = ARM_AM::getAM3Op(Imm) == ARM_AM::add;
  bool isImm = MO1.getReg() == 0;
  uint32_t Imm8 = ARM_AM::getAM3Offset(Imm);
  // if reg +/- reg, Rm will be non-zero. Otherwise, we have reg +/- imm8
  if (!isImm)
    Imm8 = CTX.getRegisterInfo()->getEncodingValue(MO1.getReg());
  return (Rn << 9) | Imm8 | (isAdd << 8) | (isImm << 13);
}

/// getAddrModeThumbSPOpValue - Encode the t_addrmode_sp operands.
uint32_t ARMMCCodeEmitter::
getAddrModeThumbSPOpValue(const MCInst &MI, unsigned OpIdx,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  // [SP, #imm]
  //   {7-0} = imm8
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  assert(MI.getOperand(OpIdx).getReg() == ARM::SP &&
         "Unexpected base register!");

  // The immediate is already shifted for the implicit zeroes, so no change
  // here.
  return MO1.getImm() & 0xff;
}

/// getAddrModeISOpValue - Encode the t_addrmode_is# operands.
uint32_t ARMMCCodeEmitter::
getAddrModeISOpValue(const MCInst &MI, unsigned OpIdx,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  // [Rn, #imm]
  //   {7-3} = imm5
  //   {2-0} = Rn
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  unsigned Rn = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Imm5 = MO1.getImm();
  return ((Imm5 & 0x1f) << 3) | Rn;
}

/// getAddrModePCOpValue - Return encoding for t_addrmode_pc operands.
uint32_t ARMMCCodeEmitter::
getAddrModePCOpValue(const MCInst &MI, unsigned OpIdx,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand MO = MI.getOperand(OpIdx);
  if (MO.isExpr())
    return ::getBranchTargetOpValue(MI, OpIdx, ARM::fixup_arm_thumb_cp, Fixups, STI);
  return (MO.getImm() >> 2);
}

/// getAddrMode5OpValue - Return encoding info for 'reg +/- (imm8 << 2)' operand.
uint32_t ARMMCCodeEmitter::
getAddrMode5OpValue(const MCInst &MI, unsigned OpIdx,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  // {12-9} = reg
  // {8}    = (U)nsigned (add == '1', sub == '0')
  // {7-0}  = imm8
  unsigned Reg, Imm8;
  bool isAdd;
  // If The first operand isn't a register, we have a label reference.
  const MCOperand &MO = MI.getOperand(OpIdx);
  if (!MO.isReg()) {
    Reg = CTX.getRegisterInfo()->getEncodingValue(ARM::PC);   // Rn is PC.
    Imm8 = 0;
    isAdd = false; // 'U' bit is handled as part of the fixup.

    assert(MO.isExpr() && "Unexpected machine operand type!");
    const MCExpr *Expr = MO.getExpr();
    MCFixupKind Kind;
    if (isThumb2(STI))
      Kind = MCFixupKind(ARM::fixup_t2_pcrel_10);
    else
      Kind = MCFixupKind(ARM::fixup_arm_pcrel_10);
    Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

    ++MCNumCPRelocations;
  } else {
    EncodeAddrModeOpValues(MI, OpIdx, Reg, Imm8, Fixups, STI);
    isAdd = ARM_AM::getAM5Op(Imm8) == ARM_AM::add;
  }

  uint32_t Binary = ARM_AM::getAM5Offset(Imm8);
  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (isAdd)
    Binary |= (1 << 8);
  Binary |= (Reg << 9);
  return Binary;
}

/// getAddrMode5FP16OpValue - Return encoding info for 'reg +/- (imm8 << 1)' operand.
uint32_t ARMMCCodeEmitter::
getAddrMode5FP16OpValue(const MCInst &MI, unsigned OpIdx,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  // {12-9} = reg
  // {8}    = (U)nsigned (add == '1', sub == '0')
  // {7-0}  = imm8
  unsigned Reg, Imm8;
  bool isAdd;
  // If The first operand isn't a register, we have a label reference.
  const MCOperand &MO = MI.getOperand(OpIdx);
  if (!MO.isReg()) {
    Reg = CTX.getRegisterInfo()->getEncodingValue(ARM::PC);   // Rn is PC.
    Imm8 = 0;
    isAdd = false; // 'U' bit is handled as part of the fixup.

    assert(MO.isExpr() && "Unexpected machine operand type!");
    const MCExpr *Expr = MO.getExpr();
    MCFixupKind Kind;
    if (isThumb2(STI))
      Kind = MCFixupKind(ARM::fixup_t2_pcrel_9);
    else
      Kind = MCFixupKind(ARM::fixup_arm_pcrel_9);
    Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

    ++MCNumCPRelocations;
  } else {
    EncodeAddrModeOpValues(MI, OpIdx, Reg, Imm8, Fixups, STI);
    isAdd = ARM_AM::getAM5Op(Imm8) == ARM_AM::add;
  }

  uint32_t Binary = ARM_AM::getAM5Offset(Imm8);
  // Immediate is always encoded as positive. The 'U' bit controls add vs sub.
  if (isAdd)
    Binary |= (1 << 8);
  Binary |= (Reg << 9);
  return Binary;
}

unsigned ARMMCCodeEmitter::
getSORegRegOpValue(const MCInst &MI, unsigned OpIdx,
                SmallVectorImpl<MCFixup> &Fixups,
                const MCSubtargetInfo &STI) const {
  // Sub-operands are [reg, reg, imm]. The first register is Rm, the reg to be
  // shifted. The second is Rs, the amount to shift by, and the third specifies
  // the type of the shift.
  //
  // {3-0} = Rm.
  // {4}   = 1
  // {6-5} = type
  // {11-8} = Rs
  // {7}    = 0

  const MCOperand &MO  = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  const MCOperand &MO2 = MI.getOperand(OpIdx + 2);
  ARM_AM::ShiftOpc SOpc = ARM_AM::getSORegShOp(MO2.getImm());

  // Encode Rm.
  unsigned Binary = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());

  // Encode the shift opcode.
  unsigned SBits = 0;
  unsigned Rs = MO1.getReg();
  if (Rs) {
    // Set shift operand (bit[7:4]).
    // LSL - 0001
    // LSR - 0011
    // ASR - 0101
    // ROR - 0111
    switch (SOpc) {
    default: llvm_unreachable("Unknown shift opc!");
    case ARM_AM::lsl: SBits = 0x1; break;
    case ARM_AM::lsr: SBits = 0x3; break;
    case ARM_AM::asr: SBits = 0x5; break;
    case ARM_AM::ror: SBits = 0x7; break;
    }
  }

  Binary |= SBits << 4;

  // Encode the shift operation Rs.
  // Encode Rs bit[11:8].
  assert(ARM_AM::getSORegOffset(MO2.getImm()) == 0);
  return Binary | (CTX.getRegisterInfo()->getEncodingValue(Rs) << ARMII::RegRsShift);
}

unsigned ARMMCCodeEmitter::
getSORegImmOpValue(const MCInst &MI, unsigned OpIdx,
                SmallVectorImpl<MCFixup> &Fixups,
                const MCSubtargetInfo &STI) const {
  // Sub-operands are [reg, imm]. The first register is Rm, the reg to be
  // shifted. The second is the amount to shift by.
  //
  // {3-0} = Rm.
  // {4}   = 0
  // {6-5} = type
  // {11-7} = imm

  const MCOperand &MO  = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  ARM_AM::ShiftOpc SOpc = ARM_AM::getSORegShOp(MO1.getImm());

  // Encode Rm.
  unsigned Binary = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());

  // Encode the shift opcode.
  unsigned SBits = 0;

  // Set shift operand (bit[6:4]).
  // LSL - 000
  // LSR - 010
  // ASR - 100
  // ROR - 110
  // RRX - 110 and bit[11:8] clear.
  switch (SOpc) {
  default: llvm_unreachable("Unknown shift opc!");
  case ARM_AM::lsl: SBits = 0x0; break;
  case ARM_AM::lsr: SBits = 0x2; break;
  case ARM_AM::asr: SBits = 0x4; break;
  case ARM_AM::ror: SBits = 0x6; break;
  case ARM_AM::rrx:
    Binary |= 0x60;
    return Binary;
  }

  // Encode shift_imm bit[11:7].
  Binary |= SBits << 4;
  unsigned Offset = ARM_AM::getSORegOffset(MO1.getImm());
  assert(Offset < 32 && "Offset must be in range 0-31!");
  return Binary | (Offset << 7);
}


unsigned ARMMCCodeEmitter::
getT2AddrModeSORegOpValue(const MCInst &MI, unsigned OpNum,
                SmallVectorImpl<MCFixup> &Fixups,
                const MCSubtargetInfo &STI) const {
  const MCOperand &MO1 = MI.getOperand(OpNum);
  const MCOperand &MO2 = MI.getOperand(OpNum+1);
  const MCOperand &MO3 = MI.getOperand(OpNum+2);

  // Encoded as [Rn, Rm, imm].
  // FIXME: Needs fixup support.
  unsigned Value = CTX.getRegisterInfo()->getEncodingValue(MO1.getReg());
  Value <<= 4;
  Value |= CTX.getRegisterInfo()->getEncodingValue(MO2.getReg());
  Value <<= 2;
  Value |= MO3.getImm();

  return Value;
}

unsigned ARMMCCodeEmitter::
getT2AddrModeImm8OpValue(const MCInst &MI, unsigned OpNum,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO1 = MI.getOperand(OpNum);
  const MCOperand &MO2 = MI.getOperand(OpNum+1);

  // FIXME: Needs fixup support.
  unsigned Value = CTX.getRegisterInfo()->getEncodingValue(MO1.getReg());

  // Even though the immediate is 8 bits long, we need 9 bits in order
  // to represent the (inverse of the) sign bit.
  Value <<= 9;
  int32_t tmp = (int32_t)MO2.getImm();
  if (tmp < 0)
    tmp = abs(tmp);
  else
    Value |= 256; // Set the ADD bit
  Value |= tmp & 255;
  return Value;
}

unsigned ARMMCCodeEmitter::
getT2AddrModeImm8OffsetOpValue(const MCInst &MI, unsigned OpNum,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO1 = MI.getOperand(OpNum);

  // FIXME: Needs fixup support.
  unsigned Value = 0;
  int32_t tmp = (int32_t)MO1.getImm();
  if (tmp < 0)
    tmp = abs(tmp);
  else
    Value |= 256; // Set the ADD bit
  Value |= tmp & 255;
  return Value;
}

unsigned ARMMCCodeEmitter::
getT2SORegOpValue(const MCInst &MI, unsigned OpIdx,
                SmallVectorImpl<MCFixup> &Fixups,
                const MCSubtargetInfo &STI) const {
  // Sub-operands are [reg, imm]. The first register is Rm, the reg to be
  // shifted. The second is the amount to shift by.
  //
  // {3-0} = Rm.
  // {4}   = 0
  // {6-5} = type
  // {11-7} = imm

  const MCOperand &MO  = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  ARM_AM::ShiftOpc SOpc = ARM_AM::getSORegShOp(MO1.getImm());

  // Encode Rm.
  unsigned Binary = CTX.getRegisterInfo()->getEncodingValue(MO.getReg());

  // Encode the shift opcode.
  unsigned SBits = 0;
  // Set shift operand (bit[6:4]).
  // LSL - 000
  // LSR - 010
  // ASR - 100
  // ROR - 110
  switch (SOpc) {
  default: llvm_unreachable("Unknown shift opc!");
  case ARM_AM::lsl: SBits = 0x0; break;
  case ARM_AM::lsr: SBits = 0x2; break;
  case ARM_AM::asr: SBits = 0x4; break;
  case ARM_AM::rrx: LLVM_FALLTHROUGH;
  case ARM_AM::ror: SBits = 0x6; break;
  }

  Binary |= SBits << 4;
  if (SOpc == ARM_AM::rrx)
    return Binary;

  // Encode shift_imm bit[11:7].
  return Binary | ARM_AM::getSORegOffset(MO1.getImm()) << 7;
}

unsigned ARMMCCodeEmitter::
getBitfieldInvertedMaskOpValue(const MCInst &MI, unsigned Op,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const {
  // 10 bits. lower 5 bits are the lsb of the mask, high five bits are the
  // msb of the mask.
  const MCOperand &MO = MI.getOperand(Op);
  uint32_t v = ~MO.getImm();
  uint32_t lsb = countTrailingZeros(v);
  uint32_t msb = (32 - countLeadingZeros (v)) - 1;
  assert(v != 0 && lsb < 32 && msb < 32 && "Illegal bitfield mask!");
  return lsb | (msb << 5);
}

unsigned ARMMCCodeEmitter::
getRegisterListOpValue(const MCInst &MI, unsigned Op,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  // VLDM/VSTM:
  //   {12-8} = Vd
  //   {7-0}  = Number of registers
  //
  // LDM/STM:
  //   {15-0}  = Bitfield of GPRs.
  unsigned Reg = MI.getOperand(Op).getReg();
  bool SPRRegs = ARMMCRegisterClasses[ARM::SPRRegClassID].contains(Reg);
  bool DPRRegs = ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Reg);

  unsigned Binary = 0;

  if (SPRRegs || DPRRegs) {
    // VLDM/VSTM
    unsigned RegNo = CTX.getRegisterInfo()->getEncodingValue(Reg);
    unsigned NumRegs = (MI.getNumOperands() - Op) & 0xff;
    Binary |= (RegNo & 0x1f) << 8;
    if (SPRRegs)
      Binary |= NumRegs;
    else
      Binary |= NumRegs * 2;
  } else {
    const MCRegisterInfo &MRI = *CTX.getRegisterInfo();
    assert(std::is_sorted(MI.begin() + Op, MI.end(),
                          [&](const MCOperand &LHS, const MCOperand &RHS) {
                            return MRI.getEncodingValue(LHS.getReg()) <
                                   MRI.getEncodingValue(RHS.getReg());
                          }));

    for (unsigned I = Op, E = MI.getNumOperands(); I < E; ++I) {
      unsigned RegNo = MRI.getEncodingValue(MI.getOperand(I).getReg());
      Binary |= 1 << RegNo;
    }
  }

  return Binary;
}

/// getAddrMode6AddressOpValue - Encode an addrmode6 register number along
/// with the alignment operand.
unsigned ARMMCCodeEmitter::
getAddrMode6AddressOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
  const MCOperand &Reg = MI.getOperand(Op);
  const MCOperand &Imm = MI.getOperand(Op + 1);

  unsigned RegNo = CTX.getRegisterInfo()->getEncodingValue(Reg.getReg());
  unsigned Align = 0;

  switch (Imm.getImm()) {
  default: break;
  case 2:
  case 4:
  case 8:  Align = 0x01; break;
  case 16: Align = 0x02; break;
  case 32: Align = 0x03; break;
  }

  return RegNo | (Align << 4);
}

/// getAddrMode6OneLane32AddressOpValue - Encode an addrmode6 register number
/// along  with the alignment operand for use in VST1 and VLD1 with size 32.
unsigned ARMMCCodeEmitter::
getAddrMode6OneLane32AddressOpValue(const MCInst &MI, unsigned Op,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const {
  const MCOperand &Reg = MI.getOperand(Op);
  const MCOperand &Imm = MI.getOperand(Op + 1);

  unsigned RegNo = CTX.getRegisterInfo()->getEncodingValue(Reg.getReg());
  unsigned Align = 0;

  switch (Imm.getImm()) {
  default: break;
  case 8:
  case 16:
  case 32: // Default '0' value for invalid alignments of 8, 16, 32 bytes.
  case 2: Align = 0x00; break;
  case 4: Align = 0x03; break;
  }

  return RegNo | (Align << 4);
}


/// getAddrMode6DupAddressOpValue - Encode an addrmode6 register number and
/// alignment operand for use in VLD-dup instructions.  This is the same as
/// getAddrMode6AddressOpValue except for the alignment encoding, which is
/// different for VLD4-dup.
unsigned ARMMCCodeEmitter::
getAddrMode6DupAddressOpValue(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const {
  const MCOperand &Reg = MI.getOperand(Op);
  const MCOperand &Imm = MI.getOperand(Op + 1);

  unsigned RegNo = CTX.getRegisterInfo()->getEncodingValue(Reg.getReg());
  unsigned Align = 0;

  switch (Imm.getImm()) {
  default: break;
  case 2:
  case 4:
  case 8:  Align = 0x01; break;
  case 16: Align = 0x03; break;
  }

  return RegNo | (Align << 4);
}

unsigned ARMMCCodeEmitter::
getAddrMode6OffsetOpValue(const MCInst &MI, unsigned Op,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.getReg() == 0) return 0x0D;
  return CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
}

unsigned ARMMCCodeEmitter::
getShiftRight8Imm(const MCInst &MI, unsigned Op,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  return 8 - MI.getOperand(Op).getImm();
}

unsigned ARMMCCodeEmitter::
getShiftRight16Imm(const MCInst &MI, unsigned Op,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  return 16 - MI.getOperand(Op).getImm();
}

unsigned ARMMCCodeEmitter::
getShiftRight32Imm(const MCInst &MI, unsigned Op,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  return 32 - MI.getOperand(Op).getImm();
}

unsigned ARMMCCodeEmitter::
getShiftRight64Imm(const MCInst &MI, unsigned Op,
                   SmallVectorImpl<MCFixup> &Fixups,
                   const MCSubtargetInfo &STI) const {
  return 64 - MI.getOperand(Op).getImm();
}

void ARMMCCodeEmitter::
encodeInstruction(const MCInst &MI, raw_ostream &OS,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  // Pseudo instructions don't get encoded.
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  uint64_t TSFlags = Desc.TSFlags;
  if ((TSFlags & ARMII::FormMask) == ARMII::Pseudo)
    return;

  int Size;
  if (Desc.getSize() == 2 || Desc.getSize() == 4)
    Size = Desc.getSize();
  else
    llvm_unreachable("Unexpected instruction size!");

  uint32_t Binary = getBinaryCodeForInstr(MI, Fixups, STI);
  // Thumb 32-bit wide instructions need to emit the high order halfword
  // first.
  if (isThumb(STI) && Size == 4) {
    EmitConstant(Binary >> 16, 2, OS);
    EmitConstant(Binary & 0xffff, 2, OS);
  } else
    EmitConstant(Binary, Size, OS);
  ++MCNumEmitted;  // Keep track of the # of mi's emitted.
}

#include "ARMGenMCCodeEmitter.inc"

MCCodeEmitter *llvm::createARMLEMCCodeEmitter(const MCInstrInfo &MCII,
                                              const MCRegisterInfo &MRI,
                                              MCContext &Ctx) {
  return new ARMMCCodeEmitter(MCII, Ctx, true);
}

MCCodeEmitter *llvm::createARMBEMCCodeEmitter(const MCInstrInfo &MCII,
                                              const MCRegisterInfo &MRI,
                                              MCContext &Ctx) {
  return new ARMMCCodeEmitter(MCII, Ctx, false);
}
