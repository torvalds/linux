//===-- AMDGPUCodeEmitter.h - AMDGPU Code Emitter interface -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// CodeEmitter interface for R600 and SI codegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCCODEEMITTER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCCODEEMITTER_H

#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MCInst;
class MCInstrInfo;
class MCOperand;
class MCSubtargetInfo;
class FeatureBitset;

class AMDGPUMCCodeEmitter : public MCCodeEmitter {
  virtual void anchor();

protected:
  const MCInstrInfo &MCII;

  AMDGPUMCCodeEmitter(const MCInstrInfo &mcii) : MCII(mcii) {}

public:

  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  virtual uint64_t getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
    return 0;
  }

  virtual unsigned getSOPPBrEncoding(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
    return 0;
  }

  virtual unsigned getSDWASrcEncoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
    return 0;
  }

  virtual unsigned getSDWAVopcDstEncoding(const MCInst &MI, unsigned OpNo,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
    return 0;
  }

protected:
  uint64_t computeAvailableFeatures(const FeatureBitset &FB) const;
  void verifyInstructionPredicates(const MCInst &MI,
                                   uint64_t AvailableFeatures) const;
};

} // End namespace llvm

#endif
