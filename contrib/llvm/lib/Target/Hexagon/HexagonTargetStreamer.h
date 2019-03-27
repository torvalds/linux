//===-- HexagonTargetStreamer.h - Hexagon Target Streamer ------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef HEXAGONTARGETSTREAMER_H
#define HEXAGONTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class HexagonTargetStreamer : public MCTargetStreamer {
public:
  HexagonTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}
  virtual void EmitCodeAlignment(unsigned ByteAlignment,
                                 unsigned MaxBytesToEmit = 0){};
  virtual void emitFAlign(unsigned Size, unsigned MaxBytesToEmit){};
  virtual void EmitCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                      unsigned ByteAlignment,
                                      unsigned AccessGranularity){};
  virtual void EmitLocalCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                           unsigned ByteAlign,
                                           unsigned AccessGranularity){};
};
}

#endif
