//===-- XCoreTargetStreamer.h - XCore Target Streamer ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORETARGETSTREAMER_H
#define LLVM_LIB_TARGET_XCORE_XCORETARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class XCoreTargetStreamer : public MCTargetStreamer {
public:
  XCoreTargetStreamer(MCStreamer &S);
  ~XCoreTargetStreamer() override;
  virtual void emitCCTopData(StringRef Name) = 0;
  virtual void emitCCTopFunction(StringRef Name) = 0;
  virtual void emitCCBottomData(StringRef Name) = 0;
  virtual void emitCCBottomFunction(StringRef Name) = 0;
};
}

#endif
