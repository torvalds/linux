//===- MipsOptionRecord.cpp - Abstraction for storing information ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MipsOptionRecord.h"
#include "MipsABIInfo.h"
#include "MipsELFStreamer.h"
#include "MipsTargetStreamer.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include <cassert>

using namespace llvm;

void MipsRegInfoRecord::EmitMipsOptionRecord() {
  MCAssembler &MCA = Streamer->getAssembler();
  MipsTargetStreamer *MTS =
      static_cast<MipsTargetStreamer *>(Streamer->getTargetStreamer());

  Streamer->PushSection();

  // We need to distinguish between N64 and the rest because at the moment
  // we don't emit .Mips.options for other ELFs other than N64.
  // Since .reginfo has the same information as .Mips.options (ODK_REGINFO),
  // we can use the same abstraction (MipsRegInfoRecord class) to handle both.
  if (MTS->getABI().IsN64()) {
    // The EntrySize value of 1 seems strange since the records are neither
    // 1-byte long nor fixed length but it matches the value GAS emits.
    MCSectionELF *Sec =
        Context.getELFSection(".MIPS.options", ELF::SHT_MIPS_OPTIONS,
                              ELF::SHF_ALLOC | ELF::SHF_MIPS_NOSTRIP, 1, "");
    MCA.registerSection(*Sec);
    Sec->setAlignment(8);
    Streamer->SwitchSection(Sec);

    Streamer->EmitIntValue(ELF::ODK_REGINFO, 1);  // kind
    Streamer->EmitIntValue(40, 1); // size
    Streamer->EmitIntValue(0, 2);  // section
    Streamer->EmitIntValue(0, 4);  // info
    Streamer->EmitIntValue(ri_gprmask, 4);
    Streamer->EmitIntValue(0, 4); // pad
    Streamer->EmitIntValue(ri_cprmask[0], 4);
    Streamer->EmitIntValue(ri_cprmask[1], 4);
    Streamer->EmitIntValue(ri_cprmask[2], 4);
    Streamer->EmitIntValue(ri_cprmask[3], 4);
    Streamer->EmitIntValue(ri_gp_value, 8);
  } else {
    MCSectionELF *Sec = Context.getELFSection(".reginfo", ELF::SHT_MIPS_REGINFO,
                                              ELF::SHF_ALLOC, 24, "");
    MCA.registerSection(*Sec);
    Sec->setAlignment(MTS->getABI().IsN32() ? 8 : 4);
    Streamer->SwitchSection(Sec);

    Streamer->EmitIntValue(ri_gprmask, 4);
    Streamer->EmitIntValue(ri_cprmask[0], 4);
    Streamer->EmitIntValue(ri_cprmask[1], 4);
    Streamer->EmitIntValue(ri_cprmask[2], 4);
    Streamer->EmitIntValue(ri_cprmask[3], 4);
    assert((ri_gp_value & 0xffffffff) == ri_gp_value);
    Streamer->EmitIntValue(ri_gp_value, 4);
  }

  Streamer->PopSection();
}

void MipsRegInfoRecord::SetPhysRegUsed(unsigned Reg,
                                       const MCRegisterInfo *MCRegInfo) {
  unsigned Value = 0;

  for (MCSubRegIterator SubRegIt(Reg, MCRegInfo, true); SubRegIt.isValid();
       ++SubRegIt) {
    unsigned CurrentSubReg = *SubRegIt;

    unsigned EncVal = MCRegInfo->getEncodingValue(CurrentSubReg);
    Value |= 1 << EncVal;

    if (GPR32RegClass->contains(CurrentSubReg) ||
        GPR64RegClass->contains(CurrentSubReg))
      ri_gprmask |= Value;
    else if (COP0RegClass->contains(CurrentSubReg))
      ri_cprmask[0] |= Value;
    // MIPS COP1 is the FPU.
    else if (FGR32RegClass->contains(CurrentSubReg) ||
             FGR64RegClass->contains(CurrentSubReg) ||
             AFGR64RegClass->contains(CurrentSubReg) ||
             MSA128BRegClass->contains(CurrentSubReg))
      ri_cprmask[1] |= Value;
    else if (COP2RegClass->contains(CurrentSubReg))
      ri_cprmask[2] |= Value;
    else if (COP3RegClass->contains(CurrentSubReg))
      ri_cprmask[3] |= Value;
  }
}
