//===-- AVRTargetStreamer.h - AVR Target Streamer --------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_TARGET_STREAMER_H
#define LLVM_AVR_TARGET_STREAMER_H

#include "llvm/MC/MCELFStreamer.h"

namespace llvm {
class MCStreamer;

/// A generic AVR target output stream.
class AVRTargetStreamer : public MCTargetStreamer {
public:
  explicit AVRTargetStreamer(MCStreamer &S);

  void finish() override;
};

/// A target streamer for textual AVR assembly code.
class AVRTargetAsmStreamer : public AVRTargetStreamer {
public:
  explicit AVRTargetAsmStreamer(MCStreamer &S);
};

} // end namespace llvm

#endif // LLVM_AVR_TARGET_STREAMER_H
