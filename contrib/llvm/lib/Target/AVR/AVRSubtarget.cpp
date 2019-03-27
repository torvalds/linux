//===-- AVRSubtarget.cpp - AVR Subtarget Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AVR specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "AVRSubtarget.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/TargetRegistry.h"

#include "AVR.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#define DEBUG_TYPE "avr-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "AVRGenSubtargetInfo.inc"

namespace llvm {

AVRSubtarget::AVRSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS, AVRTargetMachine &TM)
    : AVRGenSubtargetInfo(TT, CPU, FS), InstrInfo(), FrameLowering(),
      TLInfo(TM), TSInfo(),

      // Subtarget features
      m_hasSRAM(false), m_hasJMPCALL(false), m_hasIJMPCALL(false),
      m_hasEIJMPCALL(false), m_hasADDSUBIW(false), m_hasSmallStack(false),
      m_hasMOVW(false), m_hasLPM(false), m_hasLPMX(false),  m_hasELPM(false),
      m_hasELPMX(false), m_hasSPM(false), m_hasSPMX(false), m_hasDES(false),
      m_supportsRMW(false), m_supportsMultiplication(false), m_hasBREAK(false),
      m_hasTinyEncoding(false), ELFArch(false), m_FeatureSetDummy(false) {
  // Parse features string.
  ParseSubtargetFeatures(CPU, FS);
}

} // end of namespace llvm
