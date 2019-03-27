//=== HexagonMCELFStreamer.cpp - Hexagon subclass of MCELFStreamer -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a stub that parses a MCInst bundle and passes the
// instructions on to the real streamer.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "hexagonmcelfstreamer"

#include "MCTargetDesc/HexagonMCELFStreamer.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

static cl::opt<unsigned> GPSize
  ("gpsize", cl::NotHidden,
   cl::desc("Global Pointer Addressing Size.  The default size is 8."),
   cl::Prefix,
   cl::init(8));

HexagonMCELFStreamer::HexagonMCELFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW), std::move(Emitter)),
      MCII(createHexagonMCInstrInfo()) {}

HexagonMCELFStreamer::HexagonMCELFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    MCAssembler *Assembler)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW), std::move(Emitter)),
      MCII(createHexagonMCInstrInfo()) {}

void HexagonMCELFStreamer::EmitInstruction(const MCInst &MCB,
                                           const MCSubtargetInfo &STI, bool) {
  assert(MCB.getOpcode() == Hexagon::BUNDLE);
  assert(HexagonMCInstrInfo::bundleSize(MCB) <= HEXAGON_PACKET_SIZE);
  assert(HexagonMCInstrInfo::bundleSize(MCB) > 0);

  // At this point, MCB is a bundle
  // Iterate through the bundle and assign addends for the instructions
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
    MCInst *MCI = const_cast<MCInst *>(I.getInst());
    EmitSymbol(*MCI);
  }

  MCObjectStreamer::EmitInstruction(MCB, STI);
}

void HexagonMCELFStreamer::EmitSymbol(const MCInst &Inst) {
  // Scan for values.
  for (unsigned i = Inst.getNumOperands(); i--;)
    if (Inst.getOperand(i).isExpr())
      visitUsedExpr(*Inst.getOperand(i).getExpr());
}

// EmitCommonSymbol and EmitLocalCommonSymbol are extended versions of the
// functions found in MCELFStreamer.cpp taking AccessSize as an additional
// parameter.
void HexagonMCELFStreamer::HexagonMCEmitCommonSymbol(MCSymbol *Symbol,
                                                     uint64_t Size,
                                                     unsigned ByteAlignment,
                                                     unsigned AccessSize) {
  getAssembler().registerSymbol(*Symbol);
  StringRef sbss[4] = {".sbss.1", ".sbss.2", ".sbss.4", ".sbss.8"};

  auto ELFSymbol = cast<MCSymbolELF>(Symbol);
  if (!ELFSymbol->isBindingSet()) {
    ELFSymbol->setBinding(ELF::STB_GLOBAL);
    ELFSymbol->setExternal(true);
  }

  ELFSymbol->setType(ELF::STT_OBJECT);

  if (ELFSymbol->getBinding() == ELF::STB_LOCAL) {
    StringRef SectionName =
        ((AccessSize == 0) || (Size == 0) || (Size > GPSize))
            ? ".bss"
            : sbss[(Log2_64(AccessSize))];
    MCSection &Section = *getAssembler().getContext().getELFSection(
        SectionName, ELF::SHT_NOBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
    MCSectionSubPair P = getCurrentSection();
    SwitchSection(&Section);

    if (ELFSymbol->isUndefined()) {
      EmitValueToAlignment(ByteAlignment, 0, 1, 0);
      EmitLabel(Symbol);
      EmitZeros(Size);
    }

    // Update the maximum alignment of the section if necessary.
    if (ByteAlignment > Section.getAlignment())
      Section.setAlignment(ByteAlignment);

    SwitchSection(P.first, P.second);
  } else {
    if (ELFSymbol->declareCommon(Size, ByteAlignment))
      report_fatal_error("Symbol: " + Symbol->getName() +
                         " redeclared as different type");
    if ((AccessSize) && (Size <= GPSize)) {
      uint64_t SectionIndex =
          (AccessSize <= GPSize)
              ? ELF::SHN_HEXAGON_SCOMMON + (Log2_64(AccessSize) + 1)
              : (unsigned)ELF::SHN_HEXAGON_SCOMMON;
      ELFSymbol->setIndex(SectionIndex);
    }
  }

  ELFSymbol->setSize(MCConstantExpr::create(Size, getContext()));
}

void HexagonMCELFStreamer::HexagonMCEmitLocalCommonSymbol(MCSymbol *Symbol,
                                                         uint64_t Size,
                                                         unsigned ByteAlignment,
                                                         unsigned AccessSize) {
  getAssembler().registerSymbol(*Symbol);
  auto ELFSymbol = cast<MCSymbolELF>(Symbol);
  ELFSymbol->setBinding(ELF::STB_LOCAL);
  ELFSymbol->setExternal(false);
  HexagonMCEmitCommonSymbol(Symbol, Size, ByteAlignment, AccessSize);
}


namespace llvm {
MCStreamer *createHexagonELFStreamer(Triple const &TT, MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> MAB,
                                     std::unique_ptr<MCObjectWriter> OW,
                                     std::unique_ptr<MCCodeEmitter> CE) {
  return new HexagonMCELFStreamer(Context, std::move(MAB), std::move(OW),
                                  std::move(CE));
  }

} // end namespace llvm
