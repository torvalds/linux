//===-------- AMDGPUELFStreamer.cpp - ELF Object Output -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUELFStreamer.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"

using namespace llvm;

namespace {

class AMDGPUELFStreamer : public MCELFStreamer {
public:
  AMDGPUELFStreamer(const Triple &T, MCContext &Context,
                    std::unique_ptr<MCAsmBackend> MAB,
                    std::unique_ptr<MCObjectWriter> OW,
                    std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(MAB), std::move(OW),
                      std::move(Emitter)) {}
};

}

MCELFStreamer *llvm::createAMDGPUELFStreamer(
    const Triple &T, MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    bool RelaxAll) {
  return new AMDGPUELFStreamer(T, Context, std::move(MAB), std::move(OW),
                               std::move(Emitter));
}
