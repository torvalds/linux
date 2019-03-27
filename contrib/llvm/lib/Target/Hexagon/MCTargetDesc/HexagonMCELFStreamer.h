//===- HexagonMCELFStreamer.h - Hexagon subclass of MCElfStreamer ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCELFSTREAMER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCELFSTREAMER_H

#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include <cstdint>
#include <memory>

namespace llvm {

class HexagonMCELFStreamer : public MCELFStreamer {
  std::unique_ptr<MCInstrInfo> MCII;

public:
  HexagonMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                       std::unique_ptr<MCObjectWriter> OW,
                       std::unique_ptr<MCCodeEmitter> Emitter);

  HexagonMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                       std::unique_ptr<MCObjectWriter> OW,
                       std::unique_ptr<MCCodeEmitter> Emitter,
                       MCAssembler *Assembler);

  void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                       bool) override;
  void EmitSymbol(const MCInst &Inst);
  void HexagonMCEmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                      unsigned ByteAlignment,
                                      unsigned AccessSize);
  void HexagonMCEmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                 unsigned ByteAlignment, unsigned AccessSize);
};

MCStreamer *createHexagonELFStreamer(Triple const &TT, MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> MAB,
                                     std::unique_ptr<MCObjectWriter> OW,
                                     std::unique_ptr<MCCodeEmitter> CE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCELFSTREAMER_H
