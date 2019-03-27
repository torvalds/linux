//===-- AArch64WinCOFFStreamer.h - WinCOFF Streamer for AArch64 -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements WinCOFF streamer information for the AArch64 backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64WINCOFFSTREAMER_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64WINCOFFSTREAMER_H

#include "AArch64TargetStreamer.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

namespace llvm {

MCWinCOFFStreamer *createAArch64WinCOFFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    bool RelaxAll, bool IncrementalLinkerCompatible);
} // end llvm namespace

#endif
