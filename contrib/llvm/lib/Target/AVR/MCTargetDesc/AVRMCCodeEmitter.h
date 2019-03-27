//===-- AVRMCCodeEmitter.h - Convert AVR Code to Machine Code -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the AVRMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_AVR_CODE_EMITTER_H
#define LLVM_AVR_CODE_EMITTER_H

#include "AVRFixupKinds.h"

#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/Support/DataTypes.h"

#define GET_INSTRINFO_OPERAND_TYPES_ENUM
#include "AVRGenInstrInfo.inc"

namespace llvm {

class MCContext;
class MCExpr;
class MCFixup;
class MCInst;
class MCInstrInfo;
class MCOperand;
class MCSubtargetInfo;
class raw_ostream;

/// Writes AVR machine code to a stream.
class AVRMCCodeEmitter : public MCCodeEmitter {
public:
  AVRMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}

private:
  /// Finishes up encoding an LD/ST instruction.
  /// The purpose of this function is to set an bit in the instruction
  /// which follows no logical pattern. See the implementation for details.
  unsigned loadStorePostEncoder(const MCInst &MI, unsigned EncodedValue,
                                const MCSubtargetInfo &STI) const;

  /// Gets the encoding for a conditional branch target.
  template <AVR::Fixups Fixup>
  unsigned encodeRelCondBrTarget(const MCInst &MI, unsigned OpNo,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Encodes the `PTRREGS` operand to a load or store instruction.
  unsigned encodeLDSTPtrReg(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// Encodes a `register+immediate` operand for `LDD`/`STD`.
  unsigned encodeMemri(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const;

  /// Takes the complement of a number (~0 - val).
  unsigned encodeComplement(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// Encodes an immediate value with a given fixup.
  /// \tparam Offset The offset into the instruction for the fixup.
  template <AVR::Fixups Fixup, unsigned Offset>
  unsigned encodeImm(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const;

  /// Gets the encoding of the target for the `CALL k` instruction.
  unsigned encodeCallTarget(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// TableGen'ed function to get the binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned getExprOpValue(const MCExpr *Expr, SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  /// Returns the binary encoding of operand.
  ///
  /// If the machine operand requires relocation, the relocation is recorded
  /// and zero is returned.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  void emitInstruction(uint64_t Val, unsigned Size, const MCSubtargetInfo &STI,
                       raw_ostream &OS) const;

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  AVRMCCodeEmitter(const AVRMCCodeEmitter &) = delete;
  void operator=(const AVRMCCodeEmitter &) = delete;

  const MCInstrInfo &MCII;
  MCContext &Ctx;
};

} // end namespace of llvm.

#endif // LLVM_AVR_CODE_EMITTER_H

