//===-- ARMMachObjectWriter.cpp - ARM Mach Object Writer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMFixupKinds.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;

namespace {
class ARMMachObjectWriter : public MCMachObjectTargetWriter {
  void RecordARMScatteredRelocation(MachObjectWriter *Writer,
                                    const MCAssembler &Asm,
                                    const MCAsmLayout &Layout,
                                    const MCFragment *Fragment,
                                    const MCFixup &Fixup,
                                    MCValue Target,
                                    unsigned Type,
                                    unsigned Log2Size,
                                    uint64_t &FixedValue);
  void RecordARMScatteredHalfRelocation(MachObjectWriter *Writer,
                                        const MCAssembler &Asm,
                                        const MCAsmLayout &Layout,
                                        const MCFragment *Fragment,
                                        const MCFixup &Fixup, MCValue Target,
                                        uint64_t &FixedValue);

  bool requiresExternRelocation(MachObjectWriter *Writer,
                                const MCAssembler &Asm,
                                const MCFragment &Fragment, unsigned RelocType,
                                const MCSymbol &S, uint64_t FixedValue);

public:
  ARMMachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype)
      : MCMachObjectTargetWriter(Is64Bit, CPUType, CPUSubtype) {}

  void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                        const MCAsmLayout &Layout, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override;
};
}

static bool getARMFixupKindMachOInfo(unsigned Kind, unsigned &RelocType,
                              unsigned &Log2Size) {
  RelocType = unsigned(MachO::ARM_RELOC_VANILLA);
  Log2Size = ~0U;

  switch (Kind) {
  default:
    return false;

  case FK_Data_1:
    Log2Size = llvm::Log2_32(1);
    return true;
  case FK_Data_2:
    Log2Size = llvm::Log2_32(2);
    return true;
  case FK_Data_4:
    Log2Size = llvm::Log2_32(4);
    return true;
  case FK_Data_8:
    Log2Size = llvm::Log2_32(8);
    return true;

    // These fixups are expected to always be resolvable at assembly time and
    // have no relocations supported.
  case ARM::fixup_arm_ldst_pcrel_12:
  case ARM::fixup_arm_pcrel_10:
  case ARM::fixup_arm_adr_pcrel_12:
  case ARM::fixup_arm_thumb_br:
    return false;

    // Handle 24-bit branch kinds.
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
    RelocType = unsigned(MachO::ARM_RELOC_BR24);
    // Report as 'long', even though that is not quite accurate.
    Log2Size = llvm::Log2_32(4);
    return true;

  case ARM::fixup_t2_uncondbranch:
  case ARM::fixup_arm_thumb_bl:
  case ARM::fixup_arm_thumb_blx:
    RelocType = unsigned(MachO::ARM_THUMB_RELOC_BR22);
    Log2Size = llvm::Log2_32(4);
    return true;

  // For movw/movt r_type relocations they always have a pair following them and
  // the r_length bits are used differently.  The encoding of the r_length is as
  // follows:
  //   low bit of r_length:
  //      0 - :lower16: for movw instructions
  //      1 - :upper16: for movt instructions
  //   high bit of r_length:
  //      0 - arm instructions
  //      1 - thumb instructions
  case ARM::fixup_arm_movt_hi16:
    RelocType = unsigned(MachO::ARM_RELOC_HALF);
    Log2Size = 1;
    return true;
  case ARM::fixup_t2_movt_hi16:
    RelocType = unsigned(MachO::ARM_RELOC_HALF);
    Log2Size = 3;
    return true;

  case ARM::fixup_arm_movw_lo16:
    RelocType = unsigned(MachO::ARM_RELOC_HALF);
    Log2Size = 0;
    return true;
  case ARM::fixup_t2_movw_lo16:
    RelocType = unsigned(MachO::ARM_RELOC_HALF);
    Log2Size = 2;
    return true;
  }
}

void ARMMachObjectWriter::
RecordARMScatteredHalfRelocation(MachObjectWriter *Writer,
                                 const MCAssembler &Asm,
                                 const MCAsmLayout &Layout,
                                 const MCFragment *Fragment,
                                 const MCFixup &Fixup,
                                 MCValue Target,
                                 uint64_t &FixedValue) {
  uint32_t FixupOffset = Layout.getFragmentOffset(Fragment)+Fixup.getOffset();

  if (FixupOffset & 0xff000000) {
    Asm.getContext().reportError(Fixup.getLoc(),
                                 "can not encode offset '0x" +
                                     to_hexString(FixupOffset) +
                                     "' in resulting scattered relocation.");
    return;
  }

  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());
  unsigned Type = MachO::ARM_RELOC_HALF;

  // See <reloc.h>.
  const MCSymbol *A = &Target.getSymA()->getSymbol();

  if (!A->getFragment()) {
    Asm.getContext().reportError(Fixup.getLoc(),
                       "symbol '" + A->getName() +
                       "' can not be undefined in a subtraction expression");
    return;
  }

  uint32_t Value = Writer->getSymbolAddress(*A, Layout);
  uint32_t Value2 = 0;
  uint64_t SecAddr = Writer->getSectionAddress(A->getFragment()->getParent());
  FixedValue += SecAddr;

  if (const MCSymbolRefExpr *B = Target.getSymB()) {
    const MCSymbol *SB = &B->getSymbol();

    if (!SB->getFragment()) {
      Asm.getContext().reportError(Fixup.getLoc(),
                         "symbol '" + B->getSymbol().getName() +
                         "' can not be undefined in a subtraction expression");
      return;
    }

    // Select the appropriate difference relocation type.
    Type = MachO::ARM_RELOC_HALF_SECTDIFF;
    Value2 = Writer->getSymbolAddress(B->getSymbol(), Layout);
    FixedValue -= Writer->getSectionAddress(SB->getFragment()->getParent());
  }

  // Relocations are written out in reverse order, so the PAIR comes first.
  // ARM_RELOC_HALF and ARM_RELOC_HALF_SECTDIFF abuse the r_length field:
  //
  // For these two r_type relocations they always have a pair following them and
  // the r_length bits are used differently.  The encoding of the r_length is as
  // follows:
  //   low bit of r_length:
  //      0 - :lower16: for movw instructions
  //      1 - :upper16: for movt instructions
  //   high bit of r_length:
  //      0 - arm instructions
  //      1 - thumb instructions
  // the other half of the relocated expression is in the following pair
  // relocation entry in the low 16 bits of r_address field.
  unsigned ThumbBit = 0;
  unsigned MovtBit = 0;
  switch ((unsigned)Fixup.getKind()) {
  default: break;
  case ARM::fixup_arm_movt_hi16:
    MovtBit = 1;
    // The thumb bit shouldn't be set in the 'other-half' bit of the
    // relocation, but it will be set in FixedValue if the base symbol
    // is a thumb function. Clear it out here.
    if (Asm.isThumbFunc(A))
      FixedValue &= 0xfffffffe;
    break;
  case ARM::fixup_t2_movt_hi16:
    if (Asm.isThumbFunc(A))
      FixedValue &= 0xfffffffe;
    MovtBit = 1;
    LLVM_FALLTHROUGH;
  case ARM::fixup_t2_movw_lo16:
    ThumbBit = 1;
    break;
  }

  if (Type == MachO::ARM_RELOC_HALF_SECTDIFF) {
    uint32_t OtherHalf = MovtBit
      ? (FixedValue & 0xffff) : ((FixedValue & 0xffff0000) >> 16);

    MachO::any_relocation_info MRE;
    MRE.r_word0 = ((OtherHalf             <<  0) |
                   (MachO::ARM_RELOC_PAIR << 24) |
                   (MovtBit               << 28) |
                   (ThumbBit              << 29) |
                   (IsPCRel               << 30) |
                   MachO::R_SCATTERED);
    MRE.r_word1 = Value2;
    Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  }

  MachO::any_relocation_info MRE;
  MRE.r_word0 = ((FixupOffset <<  0) |
                 (Type        << 24) |
                 (MovtBit     << 28) |
                 (ThumbBit    << 29) |
                 (IsPCRel     << 30) |
                 MachO::R_SCATTERED);
  MRE.r_word1 = Value;
  Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
}

void ARMMachObjectWriter::RecordARMScatteredRelocation(MachObjectWriter *Writer,
                                                    const MCAssembler &Asm,
                                                    const MCAsmLayout &Layout,
                                                    const MCFragment *Fragment,
                                                    const MCFixup &Fixup,
                                                    MCValue Target,
                                                    unsigned Type,
                                                    unsigned Log2Size,
                                                    uint64_t &FixedValue) {
  uint32_t FixupOffset = Layout.getFragmentOffset(Fragment)+Fixup.getOffset();

  if (FixupOffset & 0xff000000) {
    Asm.getContext().reportError(Fixup.getLoc(),
                                 "can not encode offset '0x" +
                                     to_hexString(FixupOffset) +
                                     "' in resulting scattered relocation.");
    return;
  }

  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());

  // See <reloc.h>.
  const MCSymbol *A = &Target.getSymA()->getSymbol();

  if (!A->getFragment()) {
    Asm.getContext().reportError(Fixup.getLoc(),
                       "symbol '" + A->getName() +
                       "' can not be undefined in a subtraction expression");
    return;
  }

  uint32_t Value = Writer->getSymbolAddress(*A, Layout);
  uint64_t SecAddr = Writer->getSectionAddress(A->getFragment()->getParent());
  FixedValue += SecAddr;
  uint32_t Value2 = 0;

  if (const MCSymbolRefExpr *B = Target.getSymB()) {
    assert(Type == MachO::ARM_RELOC_VANILLA && "invalid reloc for 2 symbols");
    const MCSymbol *SB = &B->getSymbol();

    if (!SB->getFragment()) {
      Asm.getContext().reportError(Fixup.getLoc(),
                         "symbol '" + B->getSymbol().getName() +
                         "' can not be undefined in a subtraction expression");
      return;
    }

    // Select the appropriate difference relocation type.
    Type = MachO::ARM_RELOC_SECTDIFF;
    Value2 = Writer->getSymbolAddress(B->getSymbol(), Layout);
    FixedValue -= Writer->getSectionAddress(SB->getFragment()->getParent());
  }

  // Relocations are written out in reverse order, so the PAIR comes first.
  if (Type == MachO::ARM_RELOC_SECTDIFF ||
      Type == MachO::ARM_RELOC_LOCAL_SECTDIFF) {
    MachO::any_relocation_info MRE;
    MRE.r_word0 = ((0                     <<  0) |
                   (MachO::ARM_RELOC_PAIR << 24) |
                   (Log2Size              << 28) |
                   (IsPCRel               << 30) |
                   MachO::R_SCATTERED);
    MRE.r_word1 = Value2;
    Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  }

  MachO::any_relocation_info MRE;
  MRE.r_word0 = ((FixupOffset <<  0) |
                 (Type        << 24) |
                 (Log2Size    << 28) |
                 (IsPCRel     << 30) |
                 MachO::R_SCATTERED);
  MRE.r_word1 = Value;
  Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
}

bool ARMMachObjectWriter::requiresExternRelocation(MachObjectWriter *Writer,
                                                   const MCAssembler &Asm,
                                                   const MCFragment &Fragment,
                                                   unsigned RelocType,
                                                   const MCSymbol &S,
                                                   uint64_t FixedValue) {
  // Most cases can be identified purely from the symbol.
  if (Writer->doesSymbolRequireExternRelocation(S))
    return true;
  int64_t Value = (int64_t)FixedValue;  // The displacement is signed.
  int64_t Range;
  switch (RelocType) {
  default:
    return false;
  case MachO::ARM_RELOC_BR24:
    // An ARM call might be to a Thumb function, in which case the offset may
    // not be encodable in the instruction and we must use an external
    // relocation that explicitly mentions the function. Not a problem if it's
    // to a temporary "Lwhatever" symbol though, and in fact trying to use an
    // external relocation there causes more issues.
    if (!S.isTemporary())
       return true;

    // PC pre-adjustment of 8 for these instructions.
    Value -= 8;
    // ARM BL/BLX has a 25-bit offset.
    Range = 0x1ffffff;
    break;
  case MachO::ARM_THUMB_RELOC_BR22:
    // PC pre-adjustment of 4 for these instructions.
    Value -= 4;
    // Thumb BL/BLX has a 24-bit offset.
    Range = 0xffffff;
  }
  // BL/BLX also use external relocations when an internal relocation
  // would result in the target being out of range. This gives the linker
  // enough information to generate a branch island.
  Value += Writer->getSectionAddress(&S.getSection());
  Value -= Writer->getSectionAddress(Fragment.getParent());
  // If the resultant value would be out of range for an internal relocation,
  // use an external instead.
  if (Value > Range || Value < -(Range + 1))
    return true;
  return false;
}

void ARMMachObjectWriter::recordRelocation(MachObjectWriter *Writer,
                                           MCAssembler &Asm,
                                           const MCAsmLayout &Layout,
                                           const MCFragment *Fragment,
                                           const MCFixup &Fixup, MCValue Target,
                                           uint64_t &FixedValue) {
  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());
  unsigned Log2Size;
  unsigned RelocType = MachO::ARM_RELOC_VANILLA;
  if (!getARMFixupKindMachOInfo(Fixup.getKind(), RelocType, Log2Size)) {
    // If we failed to get fixup kind info, it's because there's no legal
    // relocation type for the fixup kind. This happens when it's a fixup that's
    // expected to always be resolvable at assembly time and not have any
    // relocations needed.
    Asm.getContext().reportError(Fixup.getLoc(),
                                 "unsupported relocation on symbol");
    return;
  }

  // If this is a difference or a defined symbol plus an offset, then we need a
  // scattered relocation entry.  Differences always require scattered
  // relocations.
  if (Target.getSymB()) {
    if (RelocType == MachO::ARM_RELOC_HALF)
      return RecordARMScatteredHalfRelocation(Writer, Asm, Layout, Fragment,
                                              Fixup, Target, FixedValue);
    return RecordARMScatteredRelocation(Writer, Asm, Layout, Fragment, Fixup,
                                        Target, RelocType, Log2Size,
                                        FixedValue);
  }

  // Get the symbol data, if any.
  const MCSymbol *A = nullptr;
  if (Target.getSymA())
    A = &Target.getSymA()->getSymbol();

  // FIXME: For other platforms, we need to use scattered relocations for
  // internal relocations with offsets.  If this is an internal relocation with
  // an offset, it also needs a scattered relocation entry.
  //
  // Is this right for ARM?
  uint32_t Offset = Target.getConstant();
  if (IsPCRel && RelocType == MachO::ARM_RELOC_VANILLA)
    Offset += 1 << Log2Size;
  if (Offset && A && !Writer->doesSymbolRequireExternRelocation(*A) &&
      RelocType != MachO::ARM_RELOC_HALF)
    return RecordARMScatteredRelocation(Writer, Asm, Layout, Fragment, Fixup,
                                        Target, RelocType, Log2Size,
                                        FixedValue);

  // See <reloc.h>.
  uint32_t FixupOffset = Layout.getFragmentOffset(Fragment)+Fixup.getOffset();
  unsigned Index = 0;
  unsigned Type = 0;
  const MCSymbol *RelSymbol = nullptr;

  if (Target.isAbsolute()) { // constant
    // FIXME!
    report_fatal_error("FIXME: relocations to absolute targets "
                       "not yet implemented");
  } else {
    // Resolve constant variables.
    if (A->isVariable()) {
      int64_t Res;
      if (A->getVariableValue()->evaluateAsAbsolute(
              Res, Layout, Writer->getSectionAddressMap())) {
        FixedValue = Res;
        return;
      }
    }

    // Check whether we need an external or internal relocation.
    if (requiresExternRelocation(Writer, Asm, *Fragment, RelocType, *A,
                                 FixedValue)) {
      RelSymbol = A;

      // For external relocations, make sure to offset the fixup value to
      // compensate for the addend of the symbol address, if it was
      // undefined. This occurs with weak definitions, for example.
      if (!A->isUndefined())
        FixedValue -= Layout.getSymbolOffset(*A);
    } else {
      // The index is the section ordinal (1-based).
      const MCSection &Sec = A->getSection();
      Index = Sec.getOrdinal() + 1;
      FixedValue += Writer->getSectionAddress(&Sec);
    }
    if (IsPCRel)
      FixedValue -= Writer->getSectionAddress(Fragment->getParent());

    // The type is determined by the fixup kind.
    Type = RelocType;
  }

  // struct relocation_info (8 bytes)
  MachO::any_relocation_info MRE;
  MRE.r_word0 = FixupOffset;
  MRE.r_word1 =
      (Index << 0) | (IsPCRel << 24) | (Log2Size << 25) | (Type << 28);

  // Even when it's not a scattered relocation, movw/movt always uses
  // a PAIR relocation.
  if (Type == MachO::ARM_RELOC_HALF) {
    // The entire addend is needed to correctly apply a relocation. One half is
    // extracted from the instruction itself, the other comes from this
    // PAIR. I.e. it's correct that we insert the high bits of the addend in the
    // MOVW case here.  relocation entries.
    uint32_t Value = 0;
    switch ((unsigned)Fixup.getKind()) {
    default: break;
    case ARM::fixup_arm_movw_lo16:
    case ARM::fixup_t2_movw_lo16:
      Value = (FixedValue >> 16) & 0xffff;
      break;
    case ARM::fixup_arm_movt_hi16:
    case ARM::fixup_t2_movt_hi16:
      Value = FixedValue & 0xffff;
      break;
    }
    MachO::any_relocation_info MREPair;
    MREPair.r_word0 = Value;
    MREPair.r_word1 = ((0xffffff              <<  0) |
                       (Log2Size              << 25) |
                       (MachO::ARM_RELOC_PAIR << 28));

    Writer->addRelocation(nullptr, Fragment->getParent(), MREPair);
  }

  Writer->addRelocation(RelSymbol, Fragment->getParent(), MRE);
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createARMMachObjectWriter(bool Is64Bit, uint32_t CPUType,
                                uint32_t CPUSubtype) {
  return llvm::make_unique<ARMMachObjectWriter>(Is64Bit, CPUType, CPUSubtype);
}
