//===-- SystemZMCObjectWriter.cpp - SystemZ ELF writer --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZMCFixups.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

class SystemZObjectWriter : public MCELFObjectTargetWriter {
public:
  SystemZObjectWriter(uint8_t OSABI);
  ~SystemZObjectWriter() override = default;

protected:
  // Override MCELFObjectTargetWriter.
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

} // end anonymous namespace

SystemZObjectWriter::SystemZObjectWriter(uint8_t OSABI)
  : MCELFObjectTargetWriter(/*Is64Bit=*/true, OSABI, ELF::EM_S390,
                            /*HasRelocationAddend=*/ true) {}

// Return the relocation type for an absolute value of MCFixupKind Kind.
static unsigned getAbsoluteReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_1: return ELF::R_390_8;
  case FK_Data_2: return ELF::R_390_16;
  case FK_Data_4: return ELF::R_390_32;
  case FK_Data_8: return ELF::R_390_64;
  }
  llvm_unreachable("Unsupported absolute address");
}

// Return the relocation type for a PC-relative value of MCFixupKind Kind.
static unsigned getPCRelReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_2:                return ELF::R_390_PC16;
  case FK_Data_4:                return ELF::R_390_PC32;
  case FK_Data_8:                return ELF::R_390_PC64;
  case SystemZ::FK_390_PC12DBL:  return ELF::R_390_PC12DBL;
  case SystemZ::FK_390_PC16DBL:  return ELF::R_390_PC16DBL;
  case SystemZ::FK_390_PC24DBL:  return ELF::R_390_PC24DBL;
  case SystemZ::FK_390_PC32DBL:  return ELF::R_390_PC32DBL;
  }
  llvm_unreachable("Unsupported PC-relative address");
}

// Return the R_390_TLS_LE* relocation type for MCFixupKind Kind.
static unsigned getTLSLEReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LE32;
  case FK_Data_8: return ELF::R_390_TLS_LE64;
  }
  llvm_unreachable("Unsupported absolute address");
}

// Return the R_390_TLS_LDO* relocation type for MCFixupKind Kind.
static unsigned getTLSLDOReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LDO32;
  case FK_Data_8: return ELF::R_390_TLS_LDO64;
  }
  llvm_unreachable("Unsupported absolute address");
}

// Return the R_390_TLS_LDM* relocation type for MCFixupKind Kind.
static unsigned getTLSLDMReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_LDM32;
  case FK_Data_8: return ELF::R_390_TLS_LDM64;
  case SystemZ::FK_390_TLS_CALL: return ELF::R_390_TLS_LDCALL;
  }
  llvm_unreachable("Unsupported absolute address");
}

// Return the R_390_TLS_GD* relocation type for MCFixupKind Kind.
static unsigned getTLSGDReloc(unsigned Kind) {
  switch (Kind) {
  case FK_Data_4: return ELF::R_390_TLS_GD32;
  case FK_Data_8: return ELF::R_390_TLS_GD64;
  case SystemZ::FK_390_TLS_CALL: return ELF::R_390_TLS_GDCALL;
  }
  llvm_unreachable("Unsupported absolute address");
}

// Return the PLT relocation counterpart of MCFixupKind Kind.
static unsigned getPLTReloc(unsigned Kind) {
  switch (Kind) {
  case SystemZ::FK_390_PC12DBL: return ELF::R_390_PLT12DBL;
  case SystemZ::FK_390_PC16DBL: return ELF::R_390_PLT16DBL;
  case SystemZ::FK_390_PC24DBL: return ELF::R_390_PLT24DBL;
  case SystemZ::FK_390_PC32DBL: return ELF::R_390_PLT32DBL;
  }
  llvm_unreachable("Unsupported absolute address");
}

unsigned SystemZObjectWriter::getRelocType(MCContext &Ctx,
                                           const MCValue &Target,
                                           const MCFixup &Fixup,
                                           bool IsPCRel) const {
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();
  unsigned Kind = Fixup.getKind();
  switch (Modifier) {
  case MCSymbolRefExpr::VK_None:
    if (IsPCRel)
      return getPCRelReloc(Kind);
    return getAbsoluteReloc(Kind);

  case MCSymbolRefExpr::VK_NTPOFF:
    assert(!IsPCRel && "NTPOFF shouldn't be PC-relative");
    return getTLSLEReloc(Kind);

  case MCSymbolRefExpr::VK_INDNTPOFF:
    if (IsPCRel && Kind == SystemZ::FK_390_PC32DBL)
      return ELF::R_390_TLS_IEENT;
    llvm_unreachable("Only PC-relative INDNTPOFF accesses are supported for now");

  case MCSymbolRefExpr::VK_DTPOFF:
    assert(!IsPCRel && "DTPOFF shouldn't be PC-relative");
    return getTLSLDOReloc(Kind);

  case MCSymbolRefExpr::VK_TLSLDM:
    assert(!IsPCRel && "TLSLDM shouldn't be PC-relative");
    return getTLSLDMReloc(Kind);

  case MCSymbolRefExpr::VK_TLSGD:
    assert(!IsPCRel && "TLSGD shouldn't be PC-relative");
    return getTLSGDReloc(Kind);

  case MCSymbolRefExpr::VK_GOT:
    if (IsPCRel && Kind == SystemZ::FK_390_PC32DBL)
      return ELF::R_390_GOTENT;
    llvm_unreachable("Only PC-relative GOT accesses are supported for now");

  case MCSymbolRefExpr::VK_PLT:
    assert(IsPCRel && "@PLT shouldt be PC-relative");
    return getPLTReloc(Kind);

  default:
    llvm_unreachable("Modifier not supported");
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createSystemZObjectWriter(uint8_t OSABI) {
  return llvm::make_unique<SystemZObjectWriter>(OSABI);
}
