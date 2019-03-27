//===- ARCTargetStreamer.h - ARC Target Streamer ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H
#define LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {

class ARCTargetStreamer : public MCTargetStreamer {
public:
  ARCTargetStreamer(MCStreamer &S);
  ~ARCTargetStreamer() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H
