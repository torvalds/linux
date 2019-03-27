//===----- AVRELFStreamer.h - AVR Target Streamer --------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_ELF_STREAMER_H
#define LLVM_AVR_ELF_STREAMER_H

#include "AVRTargetStreamer.h"

namespace llvm {

/// A target streamer for an AVR ELF object file.
class AVRELFStreamer : public AVRTargetStreamer {
public:
  AVRELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }
};

} // end namespace llvm

#endif
