//===-- SparcTargetStreamer.h - Sparc Target Streamer ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCTARGETSTREAMER_H
#define LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCTARGETSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {
class SparcTargetStreamer : public MCTargetStreamer {
  virtual void anchor();

public:
  SparcTargetStreamer(MCStreamer &S);
  /// Emit ".register <reg>, #ignore".
  virtual void emitSparcRegisterIgnore(unsigned reg) = 0;
  /// Emit ".register <reg>, #scratch".
  virtual void emitSparcRegisterScratch(unsigned reg) = 0;
};

// This part is for ascii assembly output
class SparcTargetAsmStreamer : public SparcTargetStreamer {
  formatted_raw_ostream &OS;

public:
  SparcTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
  void emitSparcRegisterIgnore(unsigned reg) override;
  void emitSparcRegisterScratch(unsigned reg) override;
};

// This part is for ELF object output
class SparcTargetELFStreamer : public SparcTargetStreamer {
public:
  SparcTargetELFStreamer(MCStreamer &S);
  MCELFStreamer &getStreamer();
  void emitSparcRegisterIgnore(unsigned reg) override {}
  void emitSparcRegisterScratch(unsigned reg) override {}
};
} // end namespace llvm

#endif
