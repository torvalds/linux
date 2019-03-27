//===-- BPFMCTargetDesc.cpp - BPF Target Descriptions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides BPF specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "BPF.h"
#include "InstPrinter/BPFInstPrinter.h"
#include "MCTargetDesc/BPFMCAsmInfo.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "BPFGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "BPFGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "BPFGenRegisterInfo.inc"

using namespace llvm;

static MCInstrInfo *createBPFMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitBPFMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createBPFMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitBPFMCRegisterInfo(X, BPF::R11 /* RAReg doesn't exist */);
  return X;
}

static MCSubtargetInfo *createBPFMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createBPFMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCStreamer *createBPFMCStreamer(const Triple &T, MCContext &Ctx,
                                       std::unique_ptr<MCAsmBackend> &&MAB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&Emitter,
                                       bool RelaxAll) {
  return createELFStreamer(Ctx, std::move(MAB), std::move(OW), std::move(Emitter),
                           RelaxAll);
}

static MCInstPrinter *createBPFMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new BPFInstPrinter(MAI, MII, MRI);
  return nullptr;
}

namespace {

class BPFMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit BPFMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    // The target is the 3rd operand of cond inst and the 1st of uncond inst.
    int16_t Imm;
    if (isConditionalBranch(Inst)) {
      Imm = Inst.getOperand(2).getImm();
    } else if (isUnconditionalBranch(Inst))
      Imm = Inst.getOperand(0).getImm();
    else
      return false;

    Target = Addr + Size + Imm * Size;
    return true;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createBPFInstrAnalysis(const MCInstrInfo *Info) {
  return new BPFMCInstrAnalysis(Info);
}

extern "C" void LLVMInitializeBPFTargetMC() {
  for (Target *T :
       {&getTheBPFleTarget(), &getTheBPFbeTarget(), &getTheBPFTarget()}) {
    // Register the MC asm info.
    RegisterMCAsmInfo<BPFMCAsmInfo> X(*T);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createBPFMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createBPFMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T,
                                            createBPFMCSubtargetInfo);

    // Register the object streamer
    TargetRegistry::RegisterELFStreamer(*T, createBPFMCStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createBPFMCInstPrinter);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createBPFInstrAnalysis);
  }

  // Register the MC code emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheBPFleTarget(),
                                        createBPFMCCodeEmitter);
  TargetRegistry::RegisterMCCodeEmitter(getTheBPFbeTarget(),
                                        createBPFbeMCCodeEmitter);

  // Register the ASM Backend
  TargetRegistry::RegisterMCAsmBackend(getTheBPFleTarget(),
                                       createBPFAsmBackend);
  TargetRegistry::RegisterMCAsmBackend(getTheBPFbeTarget(),
                                       createBPFbeAsmBackend);

  if (sys::IsLittleEndianHost) {
    TargetRegistry::RegisterMCCodeEmitter(getTheBPFTarget(),
                                          createBPFMCCodeEmitter);
    TargetRegistry::RegisterMCAsmBackend(getTheBPFTarget(),
                                         createBPFAsmBackend);
  } else {
    TargetRegistry::RegisterMCCodeEmitter(getTheBPFTarget(),
                                          createBPFbeMCCodeEmitter);
    TargetRegistry::RegisterMCAsmBackend(getTheBPFTarget(),
                                         createBPFbeAsmBackend);
  }

}
