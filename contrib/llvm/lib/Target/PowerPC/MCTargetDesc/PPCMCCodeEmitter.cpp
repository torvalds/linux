//===-- PPCMCCodeEmitter.cpp - Convert PPC code to machine code -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPCMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCFixupKinds.h"
#include "PPCInstrInfo.h"
#include "PPCMCCodeEmitter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");

MCCodeEmitter *llvm::createPPCMCCodeEmitter(const MCInstrInfo &MCII,
                                            const MCRegisterInfo &MRI,
                                            MCContext &Ctx) {
  return new PPCMCCodeEmitter(MCII, Ctx);
}

unsigned PPCMCCodeEmitter::
getDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_br24));
  return 0;
}

unsigned PPCMCCodeEmitter::getCondBrEncoding(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_br24abs));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsCondBrEncoding(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14abs));
  return 0;
}

unsigned PPCMCCodeEmitter::getImm16Encoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the immediate field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return 0;
}

unsigned PPCMCCodeEmitter::getMemRIEncoding(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  // Encode (imm, reg) as a memri, which has the low 16-bits as the
  // displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 16;

  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return (getMachineOpValue(MI, MO, Fixups, STI) & 0xFFFF) | RegBits;

  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return RegBits;
}

unsigned PPCMCCodeEmitter::getMemRIXEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // Encode (imm, reg) as a memrix, which has the low 14-bits as the
  // displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 14;

  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return ((getMachineOpValue(MI, MO, Fixups, STI) >> 2) & 0x3FFF) | RegBits;

  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16ds));
  return RegBits;
}

unsigned PPCMCCodeEmitter::getMemRIX16Encoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // Encode (imm, reg) as a memrix16, which has the low 12-bits as the
  // displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 12;

  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm()) {
    assert(!(MO.getImm() % 16) &&
           "Expecting an immediate that is a multiple of 16");
    return ((getMachineOpValue(MI, MO, Fixups, STI) >> 4) & 0xFFF) | RegBits;
  }

  // Otherwise add a fixup for the displacement field.
  Fixups.push_back(MCFixup::create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16ds));
  return RegBits;
}

unsigned PPCMCCodeEmitter::getSPE8DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe8dis, which has the low 5-bits of (imm / 8)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 3;
  return reverseBits(Imm | RegBits) >> 22;
}

unsigned PPCMCCodeEmitter::getSPE4DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe4dis, which has the low 5-bits of (imm / 4)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 2;
  return reverseBits(Imm | RegBits) >> 22;
}

unsigned PPCMCCodeEmitter::getSPE2DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe2dis, which has the low 5-bits of (imm / 2)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 1;
  return reverseBits(Imm | RegBits) >> 22;
}

unsigned PPCMCCodeEmitter::getTLSRegEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the TLS register, which simply provides a relocation
  // hint to the linker that this statement is part of a relocation sequence.
  // Return the thread-pointer register's encoding.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  const Triple &TT = STI.getTargetTriple();
  bool isPPC64 = TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le;
  return CTX.getRegisterInfo()->getEncodingValue(isPPC64 ? PPC::X13 : PPC::R2);
}

unsigned PPCMCCodeEmitter::getTLSCallEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // For special TLS calls, we need two fixups; one for the branch target
  // (__tls_get_addr), which we create via getDirectBrEncoding as usual,
  // and one for the TLSGD or TLSLD symbol, which is emitted here.
  const MCOperand &MO = MI.getOperand(OpNo+1);
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  return getDirectBrEncoding(MI, OpNo, Fixups, STI);
}

unsigned PPCMCCodeEmitter::
get_crbitm_encoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert((MI.getOpcode() == PPC::MTOCRF || MI.getOpcode() == PPC::MTOCRF8 ||
          MI.getOpcode() == PPC::MFOCRF || MI.getOpcode() == PPC::MFOCRF8) &&
         (MO.getReg() >= PPC::CR0 && MO.getReg() <= PPC::CR7));
  return 0x80 >> CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
}

// Get the index for this operand in this instruction. This is needed for
// computing the register number in PPCInstrInfo::getRegNumForOperand() for
// any instructions that use a different numbering scheme for registers in
// different operands.
static unsigned getOpIdxForMO(const MCInst &MI, const MCOperand &MO) {
  for (unsigned i = 0; i < MI.getNumOperands(); i++) {
    const MCOperand &Op = MI.getOperand(i);
    if (&Op == &MO)
      return i;
  }
  llvm_unreachable("This operand is not part of this instruction");
  return ~0U; // Silence any warnings about no return.
}

unsigned PPCMCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    // MTOCRF/MFOCRF should go through get_crbitm_encoding for the CR operand.
    // The GPR operand should come through here though.
    assert((MI.getOpcode() != PPC::MTOCRF && MI.getOpcode() != PPC::MTOCRF8 &&
            MI.getOpcode() != PPC::MFOCRF && MI.getOpcode() != PPC::MFOCRF8) ||
           MO.getReg() < PPC::CR0 || MO.getReg() > PPC::CR7);
    unsigned OpNo = getOpIdxForMO(MI, MO);
    unsigned Reg =
      PPCInstrInfo::getRegNumForOperand(MCII.get(MI.getOpcode()),
                                        MO.getReg(), OpNo);
    return CTX.getRegisterInfo()->getEncodingValue(Reg);
  }

  assert(MO.isImm() &&
         "Relocation required in an instruction that we cannot encode!");
  return MO.getImm();
}

void PPCMCCodeEmitter::encodeInstruction(
    const MCInst &MI, raw_ostream &OS, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  verifyInstructionPredicates(MI,
                              computeAvailableFeatures(STI.getFeatureBits()));

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);

  // Output the constant in big/little endian byte order.
  unsigned Size = getInstSizeInBytes(MI);
  support::endianness E = IsLittleEndian ? support::little : support::big;
  switch (Size) {
  case 0:
    break;
  case 4:
    support::endian::write<uint32_t>(OS, Bits, E);
    break;
  case 8:
    // If we emit a pair of instructions, the first one is
    // always in the top 32 bits, even on little-endian.
    support::endian::write<uint32_t>(OS, Bits >> 32, E);
    support::endian::write<uint32_t>(OS, Bits, E);
    break;
  default:
    llvm_unreachable("Invalid instruction size");
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

// Get the number of bytes used to encode the given MCInst.
unsigned PPCMCCodeEmitter::getInstSizeInBytes(const MCInst &MI) const {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);
  return Desc.getSize();
}

#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "PPCGenMCCodeEmitter.inc"
