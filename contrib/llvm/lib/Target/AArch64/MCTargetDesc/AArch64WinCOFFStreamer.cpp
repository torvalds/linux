//===-- AArch64WinCOFFStreamer.cpp - ARM Target WinCOFF Streamer ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AArch64WinCOFFStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {

class AArch64WinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::ARM64UnwindEmitter EHStreamer;

public:
  AArch64WinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                         std::unique_ptr<MCCodeEmitter> CE,
                         std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void EmitWinEHHandlerData(SMLoc Loc) override;
  void EmitWindowsUnwindTables() override;
  void FinishImpl() override;
};

void AArch64WinCOFFStreamer::EmitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::EmitWinEHHandlerData(Loc);

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  EHStreamer.EmitUnwindInfo(*this, getCurrentWinFrameInfo());
}

void AArch64WinCOFFStreamer::EmitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  EHStreamer.Emit(*this);
}

void AArch64WinCOFFStreamer::FinishImpl() {
  EmitFrames(nullptr);
  EmitWindowsUnwindTables();

  MCWinCOFFStreamer::FinishImpl();
}
} // end anonymous namespace

namespace llvm {

// Helper function to common out unwind code setup for those codes that can
// belong to both prolog and epilog.
// There are three types of Windows ARM64 SEH codes.  They can
// 1) take no operands: SEH_Nop, SEH_PrologEnd, SEH_EpilogStart, SEH_EpilogEnd
// 2) take an offset: SEH_StackAlloc, SEH_SaveFPLR, SEH_SaveFPLR_X
// 3) take a register and an offset/size: all others
void AArch64TargetWinCOFFStreamer::EmitARM64WinUnwindCode(unsigned UnwindCode,
                                                          int Reg,
                                                          int Offset) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;
  MCSymbol *Label = S.EmitCFILabel();
  auto Inst = WinEH::Instruction(UnwindCode, Label, Reg, Offset);
  if (InEpilogCFI)
    CurFrame->EpilogMap[CurrentEpilog].push_back(Inst);
  else
    CurFrame->Instructions.push_back(Inst);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFIAllocStack(unsigned Size) {
  unsigned Op = Win64EH::UOP_AllocSmall;
  if (Size >= 16384)
    Op = Win64EH::UOP_AllocLarge;
  else if (Size >= 512)
    Op = Win64EH::UOP_AllocMedium;
  EmitARM64WinUnwindCode(Op, -1, Size);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFPLR(int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFPLR, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFPLRX(int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFPLRX, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveReg(unsigned Reg,
                                                          int Offset) {
  assert(Offset >= 0 && Offset <= 504 &&
        "Offset for save reg should be >= 0 && <= 504");
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveReg, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveRegX(unsigned Reg,
                                                           int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveRegX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveRegP(unsigned Reg,
                                                           int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveRegP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveRegPX(unsigned Reg,
                                                            int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveRegPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFReg(unsigned Reg,
                                                           int Offset) {
  assert(Offset >= 0 && Offset <= 504 &&
        "Offset for save reg should be >= 0 && <= 504");
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFReg, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFRegX(unsigned Reg,
                                                            int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFRegX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFRegP(unsigned Reg,
                                                            int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFRegP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISaveFRegPX(unsigned Reg,
                                                             int Offset) {
  EmitARM64WinUnwindCode(Win64EH::UOP_SaveFRegPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFISetFP() {
  EmitARM64WinUnwindCode(Win64EH::UOP_SetFP, -1, 0);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFIAddFP(unsigned Offset) {
  assert(Offset <= 2040 && "UOP_AddFP must have offset <= 2040");
  EmitARM64WinUnwindCode(Win64EH::UOP_AddFP, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFINop() {
  EmitARM64WinUnwindCode(Win64EH::UOP_Nop, -1, 0);
}

// The functions below handle opcodes that can end up in either a prolog or
// an epilog, but not both.
void AArch64TargetWinCOFFStreamer::EmitARM64WinCFIPrologEnd() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  MCSymbol *Label = S.EmitCFILabel();
  CurFrame->PrologEnd = Label;
  WinEH::Instruction Inst = WinEH::Instruction(Win64EH::UOP_End, Label, -1, 0);
  auto it = CurFrame->Instructions.begin();
  CurFrame->Instructions.insert(it, Inst);
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFIEpilogStart() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  InEpilogCFI = true;
  CurrentEpilog = S.EmitCFILabel();
}

void AArch64TargetWinCOFFStreamer::EmitARM64WinCFIEpilogEnd() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  InEpilogCFI = false;
  MCSymbol *Label = S.EmitCFILabel();
  WinEH::Instruction Inst = WinEH::Instruction(Win64EH::UOP_End, Label, -1, 0);
  CurFrame->EpilogMap[CurrentEpilog].push_back(Inst);
  CurrentEpilog = nullptr;
}

MCWinCOFFStreamer *createAArch64WinCOFFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    bool RelaxAll, bool IncrementalLinkerCompatible) {
  auto *S = new AArch64WinCOFFStreamer(Context, std::move(MAB),
                                       std::move(Emitter), std::move(OW));
  S->getAssembler().setIncrementalLinkerCompatible(IncrementalLinkerCompatible);
  return S;
}

} // end llvm namespace
