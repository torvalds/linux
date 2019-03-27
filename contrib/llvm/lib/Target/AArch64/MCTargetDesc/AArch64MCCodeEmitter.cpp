//=- AArch64/AArch64MCCodeEmitter.cpp - Convert AArch64 code to machine code-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AArch64MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64FixupKinds.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted.");
STATISTIC(MCNumFixups, "Number of MC fixups created.");

namespace {

class AArch64MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

public:
  AArch64MCCodeEmitter(const MCInstrInfo &mcii, MCContext &ctx)
      : Ctx(ctx), MCII(mcii) {}
  AArch64MCCodeEmitter(const AArch64MCCodeEmitter &) = delete;
  void operator=(const AArch64MCCodeEmitter &) = delete;
  ~AArch64MCCodeEmitter() override = default;

  // getBinaryCodeForInstr - TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// getMachineOpValue - Return binary encoding of operand. If the machine
  /// operand requires relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  /// getLdStUImm12OpValue - Return encoding info for 12-bit unsigned immediate
  /// attached to a load, store or prfm instruction. If operand requires a
  /// relocation, record it and return zero in that part of the encoding.
  template <uint32_t FixupKind>
  uint32_t getLdStUImm12OpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  /// getAdrLabelOpValue - Return encoding info for 21-bit immediate ADR label
  /// target.
  uint32_t getAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  /// getAddSubImmOpValue - Return encoding for the 12-bit immediate value and
  /// the 2-bit shift field.
  uint32_t getAddSubImmOpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getCondBranchTargetOpValue - Return the encoded value for a conditional
  /// branch target.
  uint32_t getCondBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const;

  /// getLoadLiteralOpValue - Return the encoded value for a load-literal
  /// pc-relative address.
  uint32_t getLoadLiteralOpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// getMemExtendOpValue - Return the encoded value for a reg-extend load/store
  /// instruction: bit 0 is whether a shift is present, bit 1 is whether the
  /// operation is a sign extend (as opposed to a zero extend).
  uint32_t getMemExtendOpValue(const MCInst &MI, unsigned OpIdx,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getTestBranchTargetOpValue - Return the encoded value for a test-bit-and-
  /// branch target.
  uint32_t getTestBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const;

  /// getBranchTargetOpValue - Return the encoded value for an unconditional
  /// branch target.
  uint32_t getBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;

  /// getMoveWideImmOpValue - Return the encoded value for the immediate operand
  /// of a MOVZ or MOVK instruction.
  uint32_t getMoveWideImmOpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// getVecShifterOpValue - Return the encoded value for the vector shifter.
  uint32_t getVecShifterOpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  /// getMoveVecShifterOpValue - Return the encoded value for the vector move
  /// shifter (MSL).
  uint32_t getMoveVecShifterOpValue(const MCInst &MI, unsigned OpIdx,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const;

  /// getFixedPointScaleOpValue - Return the encoded value for the
  // FP-to-fixed-point scale factor.
  uint32_t getFixedPointScaleOpValue(const MCInst &MI, unsigned OpIdx,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  uint32_t getVecShiftR64OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftR32OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftR16OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftR8OpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftL64OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftL32OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftL16OpValue(const MCInst &MI, unsigned OpIdx,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  uint32_t getVecShiftL8OpValue(const MCInst &MI, unsigned OpIdx,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  uint32_t getImm8OptLsl(const MCInst &MI, unsigned OpIdx,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;
  uint32_t getSVEIncDecImm(const MCInst &MI, unsigned OpIdx,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned fixMOVZ(const MCInst &MI, unsigned EncodedValue,
                   const MCSubtargetInfo &STI) const;

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  unsigned fixMulHigh(const MCInst &MI, unsigned EncodedValue,
                      const MCSubtargetInfo &STI) const;

  template<int hasRs, int hasRt2> unsigned
  fixLoadStoreExclusive(const MCInst &MI, unsigned EncodedValue,
                        const MCSubtargetInfo &STI) const;

  unsigned fixOneOperandFPComparison(const MCInst &MI, unsigned EncodedValue,
                                     const MCSubtargetInfo &STI) const;

private:
  uint64_t computeAvailableFeatures(const FeatureBitset &FB) const;
  void verifyInstructionPredicates(const MCInst &MI,
                                   uint64_t AvailableFeatures) const;
};

} // end anonymous namespace

/// getMachineOpValue - Return binary encoding of operand. If the machine
/// operand requires relocation, record the relocation and return zero.
unsigned
AArch64MCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  assert(MO.isImm() && "did not expect relocated expression");
  return static_cast<unsigned>(MO.getImm());
}

template<unsigned FixupKind> uint32_t
AArch64MCCodeEmitter::getLdStUImm12OpValue(const MCInst &MI, unsigned OpIdx,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  uint32_t ImmVal = 0;

  if (MO.isImm())
    ImmVal = static_cast<uint32_t>(MO.getImm());
  else {
    assert(MO.isExpr() && "unable to encode load/store imm operand");
    MCFixupKind Kind = MCFixupKind(FixupKind);
    Fixups.push_back(MCFixup::create(0, MO.getExpr(), Kind, MI.getLoc()));
    ++MCNumFixups;
  }

  return ImmVal;
}

/// getAdrLabelOpValue - Return encoding info for 21-bit immediate ADR label
/// target.
uint32_t
AArch64MCCodeEmitter::getAdrLabelOpValue(const MCInst &MI, unsigned OpIdx,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected target type!");
  const MCExpr *Expr = MO.getExpr();

  MCFixupKind Kind = MI.getOpcode() == AArch64::ADR
                         ? MCFixupKind(AArch64::fixup_aarch64_pcrel_adr_imm21)
                         : MCFixupKind(AArch64::fixup_aarch64_pcrel_adrp_imm21);
  Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

  MCNumFixups += 1;

  // All of the information is in the fixup.
  return 0;
}

/// getAddSubImmOpValue - Return encoding for the 12-bit immediate value and
/// the 2-bit shift field.  The shift field is stored in bits 13-14 of the
/// return value.
uint32_t
AArch64MCCodeEmitter::getAddSubImmOpValue(const MCInst &MI, unsigned OpIdx,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  // Suboperands are [imm, shifter].
  const MCOperand &MO = MI.getOperand(OpIdx);
  const MCOperand &MO1 = MI.getOperand(OpIdx + 1);
  assert(AArch64_AM::getShiftType(MO1.getImm()) == AArch64_AM::LSL &&
         "unexpected shift type for add/sub immediate");
  unsigned ShiftVal = AArch64_AM::getShiftValue(MO1.getImm());
  assert((ShiftVal == 0 || ShiftVal == 12) &&
         "unexpected shift value for add/sub immediate");
  if (MO.isImm())
    return MO.getImm() | (ShiftVal == 0 ? 0 : (1 << ShiftVal));
  assert(MO.isExpr() && "Unable to encode MCOperand!");
  const MCExpr *Expr = MO.getExpr();

  // Encode the 12 bits of the fixup.
  MCFixupKind Kind = MCFixupKind(AArch64::fixup_aarch64_add_imm12);
  Fixups.push_back(MCFixup::create(0, Expr, Kind, MI.getLoc()));

  ++MCNumFixups;

  // Set the shift bit of the add instruction for relocation types
  // R_AARCH64_TLSLE_ADD_TPREL_HI12 and R_AARCH64_TLSLD_ADD_DTPREL_HI12.
  if (const AArch64MCExpr *A64E = dyn_cast<AArch64MCExpr>(Expr)) {
    AArch64MCExpr::VariantKind RefKind = A64E->getKind();
    if (RefKind == AArch64MCExpr::VK_TPREL_HI12 ||
        RefKind == AArch64MCExpr::VK_DTPREL_HI12 ||
        RefKind == AArch64MCExpr::VK_SECREL_HI12)
      ShiftVal = 12;
  }
  return ShiftVal == 0 ? 0 : (1 << ShiftVal);
}

/// getCondBranchTargetOpValue - Return the encoded value for a conditional
/// branch target.
uint32_t AArch64MCCodeEmitter::getCondBranchTargetOpValue(
    const MCInst &MI, unsigned OpIdx, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected target type!");

  MCFixupKind Kind = MCFixupKind(AArch64::fixup_aarch64_pcrel_branch19);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), Kind, MI.getLoc()));

  ++MCNumFixups;

  // All of the information is in the fixup.
  return 0;
}

/// getLoadLiteralOpValue - Return the encoded value for a load-literal
/// pc-relative address.
uint32_t
AArch64MCCodeEmitter::getLoadLiteralOpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected target type!");

  MCFixupKind Kind = MCFixupKind(AArch64::fixup_aarch64_ldr_pcrel_imm19);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), Kind, MI.getLoc()));

  ++MCNumFixups;

  // All of the information is in the fixup.
  return 0;
}

uint32_t
AArch64MCCodeEmitter::getMemExtendOpValue(const MCInst &MI, unsigned OpIdx,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  unsigned SignExtend = MI.getOperand(OpIdx).getImm();
  unsigned DoShift = MI.getOperand(OpIdx + 1).getImm();
  return (SignExtend << 1) | DoShift;
}

uint32_t
AArch64MCCodeEmitter::getMoveWideImmOpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected movz/movk immediate");

  Fixups.push_back(MCFixup::create(
      0, MO.getExpr(), MCFixupKind(AArch64::fixup_aarch64_movw), MI.getLoc()));

  ++MCNumFixups;

  return 0;
}

/// getTestBranchTargetOpValue - Return the encoded value for a test-bit-and-
/// branch target.
uint32_t AArch64MCCodeEmitter::getTestBranchTargetOpValue(
    const MCInst &MI, unsigned OpIdx, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected ADR target type!");

  MCFixupKind Kind = MCFixupKind(AArch64::fixup_aarch64_pcrel_branch14);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), Kind, MI.getLoc()));

  ++MCNumFixups;

  // All of the information is in the fixup.
  return 0;
}

/// getBranchTargetOpValue - Return the encoded value for an unconditional
/// branch target.
uint32_t
AArch64MCCodeEmitter::getBranchTargetOpValue(const MCInst &MI, unsigned OpIdx,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm())
    return MO.getImm();
  assert(MO.isExpr() && "Unexpected ADR target type!");

  MCFixupKind Kind = MI.getOpcode() == AArch64::BL
                         ? MCFixupKind(AArch64::fixup_aarch64_pcrel_call26)
                         : MCFixupKind(AArch64::fixup_aarch64_pcrel_branch26);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), Kind, MI.getLoc()));

  ++MCNumFixups;

  // All of the information is in the fixup.
  return 0;
}

/// getVecShifterOpValue - Return the encoded value for the vector shifter:
///
///   00 -> 0
///   01 -> 8
///   10 -> 16
///   11 -> 24
uint32_t
AArch64MCCodeEmitter::getVecShifterOpValue(const MCInst &MI, unsigned OpIdx,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the shift amount!");

  switch (MO.getImm()) {
  default:
    break;
  case 0:
    return 0;
  case 8:
    return 1;
  case 16:
    return 2;
  case 24:
    return 3;
  }

  llvm_unreachable("Invalid value for vector shift amount!");
}

/// getFixedPointScaleOpValue - Return the encoded value for the
// FP-to-fixed-point scale factor.
uint32_t AArch64MCCodeEmitter::getFixedPointScaleOpValue(
    const MCInst &MI, unsigned OpIdx, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return 64 - MO.getImm();
}

uint32_t
AArch64MCCodeEmitter::getVecShiftR64OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return 64 - MO.getImm();
}

uint32_t
AArch64MCCodeEmitter::getVecShiftR32OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return 32 - MO.getImm();
}

uint32_t
AArch64MCCodeEmitter::getVecShiftR16OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return 16 - MO.getImm();
}

uint32_t
AArch64MCCodeEmitter::getVecShiftR8OpValue(const MCInst &MI, unsigned OpIdx,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return 8 - MO.getImm();
}

uint32_t
AArch64MCCodeEmitter::getVecShiftL64OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return MO.getImm() - 64;
}

uint32_t
AArch64MCCodeEmitter::getVecShiftL32OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return MO.getImm() - 32;
}

uint32_t
AArch64MCCodeEmitter::getVecShiftL16OpValue(const MCInst &MI, unsigned OpIdx,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return MO.getImm() - 16;
}

uint32_t
AArch64MCCodeEmitter::getVecShiftL8OpValue(const MCInst &MI, unsigned OpIdx,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value for the scale amount!");
  return MO.getImm() - 8;
}

uint32_t
AArch64MCCodeEmitter::getImm8OptLsl(const MCInst &MI, unsigned OpIdx,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const {
  // Test shift
  auto ShiftOpnd = MI.getOperand(OpIdx + 1).getImm();
  assert(AArch64_AM::getShiftType(ShiftOpnd) == AArch64_AM::LSL &&
         "Unexpected shift type for imm8_opt_lsl immediate.");

  unsigned ShiftVal = AArch64_AM::getShiftValue(ShiftOpnd);
  assert((ShiftVal == 0 || ShiftVal == 8) &&
         "Unexpected shift value for imm8_opt_lsl immediate.");

  // Test immediate
  auto Immediate = MI.getOperand(OpIdx).getImm();
  return (Immediate & 0xff) | (ShiftVal == 0 ? 0 : (1 << ShiftVal));
}

uint32_t
AArch64MCCodeEmitter::getSVEIncDecImm(const MCInst &MI, unsigned OpIdx,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() && "Expected an immediate value!");
  // Normalize 1-16 range to 0-15.
  return MO.getImm() - 1;
}

/// getMoveVecShifterOpValue - Return the encoded value for the vector move
/// shifter (MSL).
uint32_t AArch64MCCodeEmitter::getMoveVecShifterOpValue(
    const MCInst &MI, unsigned OpIdx, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isImm() &&
         "Expected an immediate value for the move shift amount!");
  unsigned ShiftVal = AArch64_AM::getShiftValue(MO.getImm());
  assert((ShiftVal == 8 || ShiftVal == 16) && "Invalid shift amount!");
  return ShiftVal == 8 ? 0 : 1;
}

unsigned AArch64MCCodeEmitter::fixMOVZ(const MCInst &MI, unsigned EncodedValue,
                                       const MCSubtargetInfo &STI) const {
  // If one of the signed fixup kinds is applied to a MOVZ instruction, the
  // eventual result could be either a MOVZ or a MOVN. It's the MCCodeEmitter's
  // job to ensure that any bits possibly affected by this are 0. This means we
  // must zero out bit 30 (essentially emitting a MOVN).
  MCOperand UImm16MO = MI.getOperand(1);

  // Nothing to do if there's no fixup.
  if (UImm16MO.isImm())
    return EncodedValue;

  const AArch64MCExpr *A64E = cast<AArch64MCExpr>(UImm16MO.getExpr());
  switch (A64E->getKind()) {
  case AArch64MCExpr::VK_DTPREL_G2:
  case AArch64MCExpr::VK_DTPREL_G1:
  case AArch64MCExpr::VK_DTPREL_G0:
  case AArch64MCExpr::VK_GOTTPREL_G1:
  case AArch64MCExpr::VK_TPREL_G2:
  case AArch64MCExpr::VK_TPREL_G1:
  case AArch64MCExpr::VK_TPREL_G0:
    return EncodedValue & ~(1u << 30);
  default:
    // Nothing to do for an unsigned fixup.
    return EncodedValue;
  }


  return EncodedValue & ~(1u << 30);
}

void AArch64MCCodeEmitter::encodeInstruction(const MCInst &MI, raw_ostream &OS,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  verifyInstructionPredicates(MI,
                              computeAvailableFeatures(STI.getFeatureBits()));

  if (MI.getOpcode() == AArch64::TLSDESCCALL) {
    // This is a directive which applies an R_AARCH64_TLSDESC_CALL to the
    // following (BLR) instruction. It doesn't emit any code itself so it
    // doesn't go through the normal TableGenerated channels.
    MCFixupKind Fixup = MCFixupKind(AArch64::fixup_aarch64_tlsdesc_call);
    Fixups.push_back(MCFixup::create(0, MI.getOperand(0).getExpr(), Fixup));
    return;
  } else if (MI.getOpcode() == AArch64::CompilerBarrier) {
    // This just prevents the compiler from reordering accesses, no actual code.
    return;
  }

  uint64_t Binary = getBinaryCodeForInstr(MI, Fixups, STI);
  support::endian::write<uint32_t>(OS, Binary, support::little);
  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

unsigned
AArch64MCCodeEmitter::fixMulHigh(const MCInst &MI,
                                 unsigned EncodedValue,
                                 const MCSubtargetInfo &STI) const {
  // The Ra field of SMULH and UMULH is unused: it should be assembled as 31
  // (i.e. all bits 1) but is ignored by the processor.
  EncodedValue |= 0x1f << 10;
  return EncodedValue;
}

template<int hasRs, int hasRt2> unsigned
AArch64MCCodeEmitter::fixLoadStoreExclusive(const MCInst &MI,
                                            unsigned EncodedValue,
                                            const MCSubtargetInfo &STI) const {
  if (!hasRs) EncodedValue |= 0x001F0000;
  if (!hasRt2) EncodedValue |= 0x00007C00;

  return EncodedValue;
}

unsigned AArch64MCCodeEmitter::fixOneOperandFPComparison(
    const MCInst &MI, unsigned EncodedValue, const MCSubtargetInfo &STI) const {
  // The Rm field of FCMP and friends is unused - it should be assembled
  // as 0, but is ignored by the processor.
  EncodedValue &= ~(0x1f << 16);
  return EncodedValue;
}

#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "AArch64GenMCCodeEmitter.inc"

MCCodeEmitter *llvm::createAArch64MCCodeEmitter(const MCInstrInfo &MCII,
                                                const MCRegisterInfo &MRI,
                                                MCContext &Ctx) {
  return new AArch64MCCodeEmitter(MCII, Ctx);
}
