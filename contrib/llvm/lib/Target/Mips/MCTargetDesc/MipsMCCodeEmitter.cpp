//===-- MipsMCCodeEmitter.cpp - Convert Mips Code to Machine Code ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MipsMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MipsMCCodeEmitter.h"
#include "MCTargetDesc/MipsFixupKinds.h"
#include "MCTargetDesc/MipsMCExpr.h"
#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

#define GET_INSTRMAP_INFO
#include "MipsGenInstrInfo.inc"
#undef GET_INSTRMAP_INFO

namespace llvm {

MCCodeEmitter *createMipsMCCodeEmitterEB(const MCInstrInfo &MCII,
                                         const MCRegisterInfo &MRI,
                                         MCContext &Ctx) {
  return new MipsMCCodeEmitter(MCII, Ctx, false);
}

MCCodeEmitter *createMipsMCCodeEmitterEL(const MCInstrInfo &MCII,
                                         const MCRegisterInfo &MRI,
                                         MCContext &Ctx) {
  return new MipsMCCodeEmitter(MCII, Ctx, true);
}

} // end namespace llvm

// If the D<shift> instruction has a shift amount that is greater
// than 31 (checked in calling routine), lower it to a D<shift>32 instruction
static void LowerLargeShift(MCInst& Inst) {
  assert(Inst.getNumOperands() == 3 && "Invalid no. of operands for shift!");
  assert(Inst.getOperand(2).isImm());

  int64_t Shift = Inst.getOperand(2).getImm();
  if (Shift <= 31)
    return; // Do nothing
  Shift -= 32;

  // saminus32
  Inst.getOperand(2).setImm(Shift);

  switch (Inst.getOpcode()) {
  default:
    // Calling function is not synchronized
    llvm_unreachable("Unexpected shift instruction");
  case Mips::DSLL:
    Inst.setOpcode(Mips::DSLL32);
    return;
  case Mips::DSRL:
    Inst.setOpcode(Mips::DSRL32);
    return;
  case Mips::DSRA:
    Inst.setOpcode(Mips::DSRA32);
    return;
  case Mips::DROTR:
    Inst.setOpcode(Mips::DROTR32);
    return;
  }
}

// Fix a bad compact branch encoding for beqc/bnec.
void MipsMCCodeEmitter::LowerCompactBranch(MCInst& Inst) const {
  // Encoding may be illegal !(rs < rt), but this situation is
  // easily fixed.
  unsigned RegOp0 = Inst.getOperand(0).getReg();
  unsigned RegOp1 = Inst.getOperand(1).getReg();

  unsigned Reg0 =  Ctx.getRegisterInfo()->getEncodingValue(RegOp0);
  unsigned Reg1 =  Ctx.getRegisterInfo()->getEncodingValue(RegOp1);

  if (Inst.getOpcode() == Mips::BNEC || Inst.getOpcode() == Mips::BEQC ||
      Inst.getOpcode() == Mips::BNEC64 || Inst.getOpcode() == Mips::BEQC64) {
    assert(Reg0 != Reg1 && "Instruction has bad operands ($rs == $rt)!");
    if (Reg0 < Reg1)
      return;
  } else if (Inst.getOpcode() == Mips::BNVC || Inst.getOpcode() == Mips::BOVC) {
    if (Reg0 >= Reg1)
      return;
  } else if (Inst.getOpcode() == Mips::BNVC_MMR6 ||
             Inst.getOpcode() == Mips::BOVC_MMR6) {
    if (Reg1 >= Reg0)
      return;
  } else
    llvm_unreachable("Cannot rewrite unknown branch!");

  Inst.getOperand(0).setReg(RegOp1);
  Inst.getOperand(1).setReg(RegOp0);
}

bool MipsMCCodeEmitter::isMicroMips(const MCSubtargetInfo &STI) const {
  return STI.getFeatureBits()[Mips::FeatureMicroMips];
}

bool MipsMCCodeEmitter::isMips32r6(const MCSubtargetInfo &STI) const {
  return STI.getFeatureBits()[Mips::FeatureMips32r6];
}

void MipsMCCodeEmitter::EmitByte(unsigned char C, raw_ostream &OS) const {
  OS << (char)C;
}

void MipsMCCodeEmitter::EmitInstruction(uint64_t Val, unsigned Size,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &OS) const {
  // Output the instruction encoding in little endian byte order.
  // Little-endian byte ordering:
  //   mips32r2:   4 | 3 | 2 | 1
  //   microMIPS:  2 | 1 | 4 | 3
  if (IsLittleEndian && Size == 4 && isMicroMips(STI)) {
    EmitInstruction(Val >> 16, 2, STI, OS);
    EmitInstruction(Val, 2, STI, OS);
  } else {
    for (unsigned i = 0; i < Size; ++i) {
      unsigned Shift = IsLittleEndian ? i * 8 : (Size - 1 - i) * 8;
      EmitByte((Val >> Shift) & 0xff, OS);
    }
  }
}

/// encodeInstruction - Emit the instruction.
/// Size the instruction with Desc.getSize().
void MipsMCCodeEmitter::
encodeInstruction(const MCInst &MI, raw_ostream &OS,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const
{
  // Non-pseudo instructions that get changed for direct object
  // only based on operand values.
  // If this list of instructions get much longer we will move
  // the check to a function call. Until then, this is more efficient.
  MCInst TmpInst = MI;
  switch (MI.getOpcode()) {
  // If shift amount is >= 32 it the inst needs to be lowered further
  case Mips::DSLL:
  case Mips::DSRL:
  case Mips::DSRA:
  case Mips::DROTR:
    LowerLargeShift(TmpInst);
    break;
  // Compact branches, enforce encoding restrictions.
  case Mips::BEQC:
  case Mips::BNEC:
  case Mips::BEQC64:
  case Mips::BNEC64:
  case Mips::BOVC:
  case Mips::BOVC_MMR6:
  case Mips::BNVC:
  case Mips::BNVC_MMR6:
    LowerCompactBranch(TmpInst);
  }

  unsigned long N = Fixups.size();
  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);

  // Check for unimplemented opcodes.
  // Unfortunately in MIPS both NOP and SLL will come in with Binary == 0
  // so we have to special check for them.
  unsigned Opcode = TmpInst.getOpcode();
  if ((Opcode != Mips::NOP) && (Opcode != Mips::SLL) &&
      (Opcode != Mips::SLL_MM) && (Opcode != Mips::SLL_MMR6) && !Binary)
    llvm_unreachable("unimplemented opcode in encodeInstruction()");

  int NewOpcode = -1;
  if (isMicroMips(STI)) {
    if (isMips32r6(STI)) {
      NewOpcode = Mips::MipsR62MicroMipsR6(Opcode, Mips::Arch_micromipsr6);
      if (NewOpcode == -1)
        NewOpcode = Mips::Std2MicroMipsR6(Opcode, Mips::Arch_micromipsr6);
    }
    else
      NewOpcode = Mips::Std2MicroMips(Opcode, Mips::Arch_micromips);

    // Check whether it is Dsp instruction.
    if (NewOpcode == -1)
      NewOpcode = Mips::Dsp2MicroMips(Opcode, Mips::Arch_mmdsp);

    if (NewOpcode != -1) {
      if (Fixups.size() > N)
        Fixups.pop_back();

      Opcode = NewOpcode;
      TmpInst.setOpcode (NewOpcode);
      Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    }

    if (((MI.getOpcode() == Mips::MOVEP_MM) ||
         (MI.getOpcode() == Mips::MOVEP_MMR6))) {
      unsigned RegPair = getMovePRegPairOpValue(MI, 0, Fixups, STI);
      Binary = (Binary & 0xFFFFFC7F) | (RegPair << 7);
    }
  }

  const MCInstrDesc &Desc = MCII.get(TmpInst.getOpcode());

  // Get byte count of instruction
  unsigned Size = Desc.getSize();
  if (!Size)
    llvm_unreachable("Desc.getSize() returns 0");

  EmitInstruction(Binary, Size, STI, OS);
}

/// getBranchTargetOpValue - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 4.
  if (MO.isImm()) return MO.getImm() >> 2;

  assert(MO.isExpr() &&
         "getBranchTargetOpValue expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_Mips_PC16)));
  return 0;
}

/// getBranchTargetOpValue1SImm16 - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValue1SImm16(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm()) return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTargetOpValue expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_Mips_PC16)));
  return 0;
}

/// getBranchTargetOpValueMMR6 - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValueMMR6(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm())
    return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTargetOpValueMMR6 expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-2, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_Mips_PC16)));
  return 0;
}

/// getBranchTargetOpValueLsl2MMR6 - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValueLsl2MMR6(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 4.
  if (MO.isImm())
    return MO.getImm() >> 2;

  assert(MO.isExpr() &&
         "getBranchTargetOpValueLsl2MMR6 expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_Mips_PC16)));
  return 0;
}

/// getBranchTarget7OpValueMM - Return binary encoding of the microMIPS branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTarget7OpValueMM(const MCInst &MI, unsigned OpNo,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm()) return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTargetOpValueMM expects only expressions or immediates");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                                   MCFixupKind(Mips::fixup_MICROMIPS_PC7_S1)));
  return 0;
}

/// getBranchTargetOpValueMMPC10 - Return binary encoding of the microMIPS
/// 10-bit branch target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValueMMPC10(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm()) return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTargetOpValuePC10 expects only expressions or immediates");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                   MCFixupKind(Mips::fixup_MICROMIPS_PC10_S1)));
  return 0;
}

/// getBranchTargetOpValue - Return binary encoding of the microMIPS branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTargetOpValueMM(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm()) return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTargetOpValueMM expects only expressions or immediates");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                   MCFixupKind(Mips::
                               fixup_MICROMIPS_PC16_S1)));
  return 0;
}

/// getBranchTarget21OpValue - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTarget21OpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 4.
  if (MO.isImm()) return MO.getImm() >> 2;

  assert(MO.isExpr() &&
         "getBranchTarget21OpValue expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_MIPS_PC21_S2)));
  return 0;
}

/// getBranchTarget21OpValueMM - Return binary encoding of the branch
/// target operand for microMIPS. If the machine operand requires
/// relocation, record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTarget21OpValueMM(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 4.
  if (MO.isImm()) return MO.getImm() >> 2;

  assert(MO.isExpr() &&
    "getBranchTarget21OpValueMM expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_MICROMIPS_PC21_S1)));
  return 0;
}

/// getBranchTarget26OpValue - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getBranchTarget26OpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 4.
  if (MO.isImm()) return MO.getImm() >> 2;

  assert(MO.isExpr() &&
         "getBranchTarget26OpValue expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_MIPS_PC26_S2)));
  return 0;
}

/// getBranchTarget26OpValueMM - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::getBranchTarget26OpValueMM(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, divide by 2.
  if (MO.isImm())
    return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getBranchTarget26OpValueMM expects only expressions or immediates");

  const MCExpr *FixupExpression = MCBinaryExpr::createAdd(
      MO.getExpr(), MCConstantExpr::create(-4, Ctx), Ctx);
  Fixups.push_back(MCFixup::create(0, FixupExpression,
                                   MCFixupKind(Mips::fixup_MICROMIPS_PC26_S1)));
  return 0;
}

/// getJumpOffset16OpValue - Return binary encoding of the jump
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getJumpOffset16OpValue(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) return MO.getImm();

  assert(MO.isExpr() &&
         "getJumpOffset16OpValue expects only expressions or an immediate");

   // TODO: Push fixup.
   return 0;
}

/// getJumpTargetOpValue - Return binary encoding of the jump
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getJumpTargetOpValue(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  // If the destination is an immediate, divide by 4.
  if (MO.isImm()) return MO.getImm()>>2;

  assert(MO.isExpr() &&
         "getJumpTargetOpValue expects only expressions or an immediate");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                                   MCFixupKind(Mips::fixup_Mips_26)));
  return 0;
}

unsigned MipsMCCodeEmitter::
getJumpTargetOpValueMM(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  // If the destination is an immediate, divide by 2.
  if (MO.isImm()) return MO.getImm() >> 1;

  assert(MO.isExpr() &&
         "getJumpTargetOpValueMM expects only expressions or an immediate");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                                   MCFixupKind(Mips::fixup_MICROMIPS_26_S1)));
  return 0;
}

unsigned MipsMCCodeEmitter::
getUImm5Lsl2Encoding(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    // The immediate is encoded as 'immediate << 2'.
    unsigned Res = getMachineOpValue(MI, MO, Fixups, STI);
    assert((Res & 3) == 0);
    return Res >> 2;
  }

  assert(MO.isExpr() &&
         "getUImm5Lsl2Encoding expects only expressions or an immediate");

  return 0;
}

unsigned MipsMCCodeEmitter::
getSImm3Lsa2Value(const MCInst &MI, unsigned OpNo,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    int Value = MO.getImm();
    return Value >> 2;
  }

  return 0;
}

unsigned MipsMCCodeEmitter::
getUImm6Lsl2Encoding(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    unsigned Value = MO.getImm();
    return Value >> 2;
  }

  return 0;
}

unsigned MipsMCCodeEmitter::
getSImm9AddiuspValue(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    unsigned Binary = (MO.getImm() >> 2) & 0x0000ffff;
    return (((Binary & 0x8000) >> 7) | (Binary & 0x00ff));
  }

  return 0;
}

unsigned MipsMCCodeEmitter::
getExprOpValue(const MCExpr *Expr, SmallVectorImpl<MCFixup> &Fixups,
               const MCSubtargetInfo &STI) const {
  int64_t Res;

  if (Expr->evaluateAsAbsolute(Res))
    return Res;

  MCExpr::ExprKind Kind = Expr->getKind();
  if (Kind == MCExpr::Constant) {
    return cast<MCConstantExpr>(Expr)->getValue();
  }

  if (Kind == MCExpr::Binary) {
    unsigned Res = getExprOpValue(cast<MCBinaryExpr>(Expr)->getLHS(), Fixups, STI);
    Res += getExprOpValue(cast<MCBinaryExpr>(Expr)->getRHS(), Fixups, STI);
    return Res;
  }

  if (Kind == MCExpr::Target) {
    const MipsMCExpr *MipsExpr = cast<MipsMCExpr>(Expr);

    Mips::Fixups FixupKind = Mips::Fixups(0);
    switch (MipsExpr->getKind()) {
    case MipsMCExpr::MEK_None:
    case MipsMCExpr::MEK_Special:
      llvm_unreachable("Unhandled fixup kind!");
      break;
    case MipsMCExpr::MEK_DTPREL:
      // MEK_DTPREL is used for marking TLS DIEExpr only
      // and contains a regular sub-expression.
      return getExprOpValue(MipsExpr->getSubExpr(), Fixups, STI);
    case MipsMCExpr::MEK_CALL_HI16:
      FixupKind = Mips::fixup_Mips_CALL_HI16;
      break;
    case MipsMCExpr::MEK_CALL_LO16:
      FixupKind = Mips::fixup_Mips_CALL_LO16;
      break;
    case MipsMCExpr::MEK_DTPREL_HI:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_DTPREL_HI16
                                   : Mips::fixup_Mips_DTPREL_HI;
      break;
    case MipsMCExpr::MEK_DTPREL_LO:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_DTPREL_LO16
                                   : Mips::fixup_Mips_DTPREL_LO;
      break;
    case MipsMCExpr::MEK_GOTTPREL:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GOTTPREL
                                   : Mips::fixup_Mips_GOTTPREL;
      break;
    case MipsMCExpr::MEK_GOT:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GOT16
                                   : Mips::fixup_Mips_GOT;
      break;
    case MipsMCExpr::MEK_GOT_CALL:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_CALL16
                                   : Mips::fixup_Mips_CALL16;
      break;
    case MipsMCExpr::MEK_GOT_DISP:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GOT_DISP
                                   : Mips::fixup_Mips_GOT_DISP;
      break;
    case MipsMCExpr::MEK_GOT_HI16:
      FixupKind = Mips::fixup_Mips_GOT_HI16;
      break;
    case MipsMCExpr::MEK_GOT_LO16:
      FixupKind = Mips::fixup_Mips_GOT_LO16;
      break;
    case MipsMCExpr::MEK_GOT_PAGE:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GOT_PAGE
                                   : Mips::fixup_Mips_GOT_PAGE;
      break;
    case MipsMCExpr::MEK_GOT_OFST:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GOT_OFST
                                   : Mips::fixup_Mips_GOT_OFST;
      break;
    case MipsMCExpr::MEK_GPREL:
      FixupKind = Mips::fixup_Mips_GPREL16;
      break;
    case MipsMCExpr::MEK_LO:
      // Check for %lo(%neg(%gp_rel(X)))
      if (MipsExpr->isGpOff())
        FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GPOFF_LO
                                     : Mips::fixup_Mips_GPOFF_LO;
      else
        FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_LO16
                                     : Mips::fixup_Mips_LO16;
      break;
    case MipsMCExpr::MEK_HIGHEST:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_HIGHEST
                                   : Mips::fixup_Mips_HIGHEST;
      break;
    case MipsMCExpr::MEK_HIGHER:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_HIGHER
                                   : Mips::fixup_Mips_HIGHER;
      break;
    case MipsMCExpr::MEK_HI:
      // Check for %hi(%neg(%gp_rel(X)))
      if (MipsExpr->isGpOff())
        FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_GPOFF_HI
                                     : Mips::fixup_Mips_GPOFF_HI;
      else
        FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_HI16
                                     : Mips::fixup_Mips_HI16;
      break;
    case MipsMCExpr::MEK_PCREL_HI16:
      FixupKind = Mips::fixup_MIPS_PCHI16;
      break;
    case MipsMCExpr::MEK_PCREL_LO16:
      FixupKind = Mips::fixup_MIPS_PCLO16;
      break;
    case MipsMCExpr::MEK_TLSGD:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_GD
                                   : Mips::fixup_Mips_TLSGD;
      break;
    case MipsMCExpr::MEK_TLSLDM:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_LDM
                                   : Mips::fixup_Mips_TLSLDM;
      break;
    case MipsMCExpr::MEK_TPREL_HI:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_TPREL_HI16
                                   : Mips::fixup_Mips_TPREL_HI;
      break;
    case MipsMCExpr::MEK_TPREL_LO:
      FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_TLS_TPREL_LO16
                                   : Mips::fixup_Mips_TPREL_LO;
      break;
    case MipsMCExpr::MEK_NEG:
      FixupKind =
          isMicroMips(STI) ? Mips::fixup_MICROMIPS_SUB : Mips::fixup_Mips_SUB;
      break;
    }
    Fixups.push_back(MCFixup::create(0, MipsExpr, MCFixupKind(FixupKind)));
    return 0;
  }

  if (Kind == MCExpr::SymbolRef) {
    Mips::Fixups FixupKind = Mips::Fixups(0);

    switch(cast<MCSymbolRefExpr>(Expr)->getKind()) {
    default: llvm_unreachable("Unknown fixup kind!");
      break;
    case MCSymbolRefExpr::VK_None:
      FixupKind = Mips::fixup_Mips_32; // FIXME: This is ok for O32/N32 but not N64.
      break;
    } // switch

    Fixups.push_back(MCFixup::create(0, Expr, MCFixupKind(FixupKind)));
    return 0;
  }
  return 0;
}

/// getMachineOpValue - Return binary encoding of operand. If the machine
/// operand requires relocation, record the relocation and return zero.
unsigned MipsMCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    unsigned RegNo = Ctx.getRegisterInfo()->getEncodingValue(Reg);
    return RegNo;
  } else if (MO.isImm()) {
    return static_cast<unsigned>(MO.getImm());
  } else if (MO.isFPImm()) {
    return static_cast<unsigned>(APFloat(MO.getFPImm())
        .bitcastToAPInt().getHiBits(32).getLimitedValue());
  }
  // MO must be an Expr.
  assert(MO.isExpr());
  return getExprOpValue(MO.getExpr(),Fixups, STI);
}

/// Return binary encoding of memory related operand.
/// If the offset operand requires relocation, record the relocation.
template <unsigned ShiftAmount>
unsigned MipsMCCodeEmitter::getMemEncoding(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 20-16, offset is encoded in bits 15-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo),Fixups, STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  // Apply the scale factor if there is one.
  OffBits >>= ShiftAmount;

  return (OffBits & 0xFFFF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm4(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 6-4, offset is encoded in bits 3-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo),
                                       Fixups, STI) << 4;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1),
                                       Fixups, STI);

  return (OffBits & 0xF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm4Lsl1(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 6-4, offset is encoded in bits 3-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo),
                                       Fixups, STI) << 4;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1),
                                       Fixups, STI) >> 1;

  return (OffBits & 0xF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm4Lsl2(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 6-4, offset is encoded in bits 3-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo),
                                       Fixups, STI) << 4;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1),
                                       Fixups, STI) >> 2;

  return (OffBits & 0xF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMSPImm5Lsl2(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
  // Register is encoded in bits 9-5, offset is encoded in bits 4-0.
  assert(MI.getOperand(OpNo).isReg() &&
         (MI.getOperand(OpNo).getReg() == Mips::SP ||
         MI.getOperand(OpNo).getReg() == Mips::SP_64) &&
         "Unexpected base register!");
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1),
                                       Fixups, STI) >> 2;

  return OffBits & 0x1F;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMGPImm7Lsl2(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const {
  // Register is encoded in bits 9-7, offset is encoded in bits 6-0.
  assert(MI.getOperand(OpNo).isReg() &&
         MI.getOperand(OpNo).getReg() == Mips::GP &&
         "Unexpected base register!");

  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1),
                                       Fixups, STI) >> 2;

  return OffBits & 0x7F;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm9(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 20-16, offset is encoded in bits 8-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups,
                                       STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo + 1), Fixups, STI);

  return (OffBits & 0x1FF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm11(const MCInst &MI, unsigned OpNo,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 20-16, offset is encoded in bits 10-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups,
                                       STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  return (OffBits & 0x07FF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm12(const MCInst &MI, unsigned OpNo,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const {
  // opNum can be invalid if instruction had reglist as operand.
  // MemOperand is always last operand of instruction (base + offset).
  switch (MI.getOpcode()) {
  default:
    break;
  case Mips::SWM32_MM:
  case Mips::LWM32_MM:
    OpNo = MI.getNumOperands() - 2;
    break;
  }

  // Base register is encoded in bits 20-16, offset is encoded in bits 11-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  return (OffBits & 0x0FFF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm16(const MCInst &MI, unsigned OpNo,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 20-16, offset is encoded in bits 15-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups,
                                       STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  return (OffBits & 0xFFFF) | RegBits;
}

unsigned MipsMCCodeEmitter::
getMemEncodingMMImm4sp(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  // opNum can be invalid if instruction had reglist as operand
  // MemOperand is always last operand of instruction (base + offset)
  switch (MI.getOpcode()) {
  default:
    break;
  case Mips::SWM16_MM:
  case Mips::SWM16_MMR6:
  case Mips::LWM16_MM:
  case Mips::LWM16_MMR6:
    OpNo = MI.getNumOperands() - 2;
    break;
  }

  // Offset is encoded in bits 4-0.
  assert(MI.getOperand(OpNo).isReg());
  // Base register is always SP - thus it is not encoded.
  assert(MI.getOperand(OpNo+1).isImm());
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  return ((OffBits >> 2) & 0x0F);
}

// FIXME: should be called getMSBEncoding
//
unsigned
MipsMCCodeEmitter::getSizeInsEncoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo-1).isImm());
  assert(MI.getOperand(OpNo).isImm());
  unsigned Position = getMachineOpValue(MI, MI.getOperand(OpNo-1), Fixups, STI);
  unsigned Size = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI);

  return Position + Size - 1;
}

template <unsigned Bits, int Offset>
unsigned
MipsMCCodeEmitter::getUImmWithOffsetEncoding(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo).isImm());
  unsigned Value = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI);
  Value -= Offset;
  return Value;
}

unsigned
MipsMCCodeEmitter::getSimm19Lsl2Encoding(const MCInst &MI, unsigned OpNo,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    // The immediate is encoded as 'immediate << 2'.
    unsigned Res = getMachineOpValue(MI, MO, Fixups, STI);
    assert((Res & 3) == 0);
    return Res >> 2;
  }

  assert(MO.isExpr() &&
         "getSimm19Lsl2Encoding expects only expressions or an immediate");

  const MCExpr *Expr = MO.getExpr();
  Mips::Fixups FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_PC19_S2
                                            : Mips::fixup_MIPS_PC19_S2;
  Fixups.push_back(MCFixup::create(0, Expr, MCFixupKind(FixupKind)));
  return 0;
}

unsigned
MipsMCCodeEmitter::getSimm18Lsl3Encoding(const MCInst &MI, unsigned OpNo,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    // The immediate is encoded as 'immediate << 3'.
    unsigned Res = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI);
    assert((Res & 7) == 0);
    return Res >> 3;
  }

  assert(MO.isExpr() &&
         "getSimm18Lsl2Encoding expects only expressions or an immediate");

  const MCExpr *Expr = MO.getExpr();
  Mips::Fixups FixupKind = isMicroMips(STI) ? Mips::fixup_MICROMIPS_PC18_S3
                                            : Mips::fixup_MIPS_PC18_S3;
  Fixups.push_back(MCFixup::create(0, Expr, MCFixupKind(FixupKind)));
  return 0;
}

unsigned
MipsMCCodeEmitter::getUImm3Mod8Encoding(const MCInst &MI, unsigned OpNo,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo).isImm());
  const MCOperand &MO = MI.getOperand(OpNo);
  return MO.getImm() % 8;
}

unsigned
MipsMCCodeEmitter::getUImm4AndValue(const MCInst &MI, unsigned OpNo,
                                    SmallVectorImpl<MCFixup> &Fixups,
                                    const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo).isImm());
  const MCOperand &MO = MI.getOperand(OpNo);
  unsigned Value = MO.getImm();
  switch (Value) {
    case 128:   return 0x0;
    case 1:     return 0x1;
    case 2:     return 0x2;
    case 3:     return 0x3;
    case 4:     return 0x4;
    case 7:     return 0x5;
    case 8:     return 0x6;
    case 15:    return 0x7;
    case 16:    return 0x8;
    case 31:    return 0x9;
    case 32:    return 0xa;
    case 63:    return 0xb;
    case 64:    return 0xc;
    case 255:   return 0xd;
    case 32768: return 0xe;
    case 65535: return 0xf;
  }
  llvm_unreachable("Unexpected value");
}

unsigned
MipsMCCodeEmitter::getRegisterListOpValue(const MCInst &MI, unsigned OpNo,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  unsigned res = 0;

  // Register list operand is always first operand of instruction and it is
  // placed before memory operand (register + imm).

  for (unsigned I = OpNo, E = MI.getNumOperands() - 2; I < E; ++I) {
    unsigned Reg = MI.getOperand(I).getReg();
    unsigned RegNo = Ctx.getRegisterInfo()->getEncodingValue(Reg);
    if (RegNo != 31)
      res++;
    else
      res |= 0x10;
  }
  return res;
}

unsigned
MipsMCCodeEmitter::getRegisterListOpValue16(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  return (MI.getNumOperands() - 4);
}

unsigned
MipsMCCodeEmitter::getMovePRegPairOpValue(const MCInst &MI, unsigned OpNo,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  unsigned res = 0;

  if (MI.getOperand(0).getReg() == Mips::A1 &&
      MI.getOperand(1).getReg() == Mips::A2)
    res = 0;
  else if (MI.getOperand(0).getReg() == Mips::A1 &&
           MI.getOperand(1).getReg() == Mips::A3)
    res = 1;
  else if (MI.getOperand(0).getReg() == Mips::A2 &&
           MI.getOperand(1).getReg() == Mips::A3)
    res = 2;
  else if (MI.getOperand(0).getReg() == Mips::A0 &&
           MI.getOperand(1).getReg() == Mips::S5)
    res = 3;
  else if (MI.getOperand(0).getReg() == Mips::A0 &&
           MI.getOperand(1).getReg() == Mips::S6)
    res = 4;
  else if (MI.getOperand(0).getReg() == Mips::A0 &&
           MI.getOperand(1).getReg() == Mips::A1)
    res = 5;
  else if (MI.getOperand(0).getReg() == Mips::A0 &&
           MI.getOperand(1).getReg() == Mips::A2)
    res = 6;
  else if (MI.getOperand(0).getReg() == Mips::A0 &&
           MI.getOperand(1).getReg() == Mips::A3)
    res = 7;

  return res;
}

unsigned
MipsMCCodeEmitter::getMovePRegSingleOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  assert(((OpNo == 2) || (OpNo == 3)) &&
         "Unexpected OpNo for movep operand encoding!");

  MCOperand Op = MI.getOperand(OpNo);
  assert(Op.isReg() && "Operand of movep is not a register!");
  switch (Op.getReg()) {
  default:
    llvm_unreachable("Unknown register for movep!");
  case Mips::ZERO:  return 0;
  case Mips::S1:    return 1;
  case Mips::V0:    return 2;
  case Mips::V1:    return 3;
  case Mips::S0:    return 4;
  case Mips::S2:    return 5;
  case Mips::S3:    return 6;
  case Mips::S4:    return 7;
  }
}

unsigned
MipsMCCodeEmitter::getSimm23Lsl2Encoding(const MCInst &MI, unsigned OpNo,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && "getSimm23Lsl2Encoding expects only an immediate");
  // The immediate is encoded as 'immediate >> 2'.
  unsigned Res = static_cast<unsigned>(MO.getImm());
  assert((Res & 3) == 0);
  return Res >> 2;
}

#include "MipsGenMCCodeEmitter.inc"
