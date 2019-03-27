//===-- ARMAsmBackendWinCOFF.h - ARM Asm Backend WinCOFF --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMASMBACKENDWINCOFF_H
#define LLVM_LIB_TARGET_ARM_ARMASMBACKENDWINCOFF_H

#include "ARMAsmBackend.h"
#include "llvm/MC/MCObjectWriter.h"
using namespace llvm;

namespace {
class ARMAsmBackendWinCOFF : public ARMAsmBackend {
public:
  ARMAsmBackendWinCOFF(const Target &T, const MCSubtargetInfo &STI)
      : ARMAsmBackend(T, STI, support::little) {}
  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createARMWinCOFFObjectWriter(/*Is64Bit=*/false);
  }
};
}

#endif
