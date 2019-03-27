//===-- X86WinCOFFStreamer.cpp - X86 Target WinCOFF Streamer ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "X86MCTargetDesc.h"
#include "X86TargetStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class X86WinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::UnwindEmitter EHStreamer;
public:
  X86WinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                     std::unique_ptr<MCCodeEmitter> CE,
                     std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void EmitWinEHHandlerData(SMLoc Loc) override;
  void EmitWindowsUnwindTables() override;
  void EmitCVFPOData(const MCSymbol *ProcSym, SMLoc Loc) override;
  void FinishImpl() override;
};

void X86WinCOFFStreamer::EmitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::EmitWinEHHandlerData(Loc);

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  EHStreamer.EmitUnwindInfo(*this, getCurrentWinFrameInfo());
}

void X86WinCOFFStreamer::EmitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  EHStreamer.Emit(*this);
}

void X86WinCOFFStreamer::EmitCVFPOData(const MCSymbol *ProcSym, SMLoc Loc) {
  X86TargetStreamer *XTS =
      static_cast<X86TargetStreamer *>(getTargetStreamer());
  XTS->emitFPOData(ProcSym, Loc);
}

void X86WinCOFFStreamer::FinishImpl() {
  EmitFrames(nullptr);
  EmitWindowsUnwindTables();

  MCWinCOFFStreamer::FinishImpl();
}
}

MCStreamer *llvm::createX86WinCOFFStreamer(MCContext &C,
                                           std::unique_ptr<MCAsmBackend> &&AB,
                                           std::unique_ptr<MCObjectWriter> &&OW,
                                           std::unique_ptr<MCCodeEmitter> &&CE,
                                           bool RelaxAll,
                                           bool IncrementalLinkerCompatible) {
  X86WinCOFFStreamer *S =
      new X86WinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW));
  S->getAssembler().setRelaxAll(RelaxAll);
  S->getAssembler().setIncrementalLinkerCompatible(IncrementalLinkerCompatible);
  return S;
}

