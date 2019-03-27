//===-- PPCMachObjectWriter.cpp - PPC Mach-O Writer -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCFixupKinds.h"
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"

using namespace llvm;

namespace {
class PPCMachObjectWriter : public MCMachObjectTargetWriter {
  bool recordScatteredRelocation(MachObjectWriter *Writer,
                                 const MCAssembler &Asm,
                                 const MCAsmLayout &Layout,
                                 const MCFragment *Fragment,
                                 const MCFixup &Fixup, MCValue Target,
                                 unsigned Log2Size, uint64_t &FixedValue);

  void RecordPPCRelocation(MachObjectWriter *Writer, const MCAssembler &Asm,
                           const MCAsmLayout &Layout,
                           const MCFragment *Fragment, const MCFixup &Fixup,
                           MCValue Target, uint64_t &FixedValue);

public:
  PPCMachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype)
      : MCMachObjectTargetWriter(Is64Bit, CPUType, CPUSubtype) {}

  void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                        const MCAsmLayout &Layout, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override {
    if (Writer->is64Bit()) {
      report_fatal_error("Relocation emission for MachO/PPC64 unimplemented.");
    } else
      RecordPPCRelocation(Writer, Asm, Layout, Fragment, Fixup, Target,
                          FixedValue);
  }
};
}

/// computes the log2 of the size of the relocation,
/// used for relocation_info::r_length.
static unsigned getFixupKindLog2Size(unsigned Kind) {
  switch (Kind) {
  default:
    report_fatal_error("log2size(FixupKind): Unhandled fixup kind!");
  case FK_PCRel_1:
  case FK_Data_1:
    return 0;
  case FK_PCRel_2:
  case FK_Data_2:
    return 1;
  case FK_PCRel_4:
  case PPC::fixup_ppc_brcond14:
  case PPC::fixup_ppc_half16:
  case PPC::fixup_ppc_br24:
  case FK_Data_4:
    return 2;
  case FK_PCRel_8:
  case FK_Data_8:
    return 3;
  }
  return 0;
}

/// Translates generic PPC fixup kind to Mach-O/PPC relocation type enum.
/// Outline based on PPCELFObjectWriter::getRelocType().
static unsigned getRelocType(const MCValue &Target,
                             const MCFixupKind FixupKind, // from
                                                          // Fixup.getKind()
                             const bool IsPCRel) {
  const MCSymbolRefExpr::VariantKind Modifier =
      Target.isAbsolute() ? MCSymbolRefExpr::VK_None
                          : Target.getSymA()->getKind();
  // determine the type of the relocation
  unsigned Type = MachO::GENERIC_RELOC_VANILLA;
  if (IsPCRel) { // relative to PC
    switch ((unsigned)FixupKind) {
    default:
      report_fatal_error("Unimplemented fixup kind (relative)");
    case PPC::fixup_ppc_br24:
      Type = MachO::PPC_RELOC_BR24; // R_PPC_REL24
      break;
    case PPC::fixup_ppc_brcond14:
      Type = MachO::PPC_RELOC_BR14;
      break;
    case PPC::fixup_ppc_half16:
      switch (Modifier) {
      default:
        llvm_unreachable("Unsupported modifier for half16 fixup");
      case MCSymbolRefExpr::VK_PPC_HA:
        Type = MachO::PPC_RELOC_HA16;
        break;
      case MCSymbolRefExpr::VK_PPC_LO:
        Type = MachO::PPC_RELOC_LO16;
        break;
      case MCSymbolRefExpr::VK_PPC_HI:
        Type = MachO::PPC_RELOC_HI16;
        break;
      }
      break;
    }
  } else {
    switch ((unsigned)FixupKind) {
    default:
      report_fatal_error("Unimplemented fixup kind (absolute)!");
    case PPC::fixup_ppc_half16:
      switch (Modifier) {
      default:
        llvm_unreachable("Unsupported modifier for half16 fixup");
      case MCSymbolRefExpr::VK_PPC_HA:
        Type = MachO::PPC_RELOC_HA16_SECTDIFF;
        break;
      case MCSymbolRefExpr::VK_PPC_LO:
        Type = MachO::PPC_RELOC_LO16_SECTDIFF;
        break;
      case MCSymbolRefExpr::VK_PPC_HI:
        Type = MachO::PPC_RELOC_HI16_SECTDIFF;
        break;
      }
      break;
    case FK_Data_4:
      break;
    case FK_Data_2:
      break;
    }
  }
  return Type;
}

static void makeRelocationInfo(MachO::any_relocation_info &MRE,
                               const uint32_t FixupOffset, const uint32_t Index,
                               const unsigned IsPCRel, const unsigned Log2Size,
                               const unsigned IsExtern, const unsigned Type) {
  MRE.r_word0 = FixupOffset;
  // The bitfield offsets that work (as determined by trial-and-error)
  // are different than what is documented in the mach-o manuals.
  // This appears to be an endianness issue; reversing the order of the
  // documented bitfields in <llvm/BinaryFormat/MachO.h> fixes this (but
  // breaks x86/ARM assembly).
  MRE.r_word1 = ((Index << 8) |    // was << 0
                 (IsPCRel << 7) |  // was << 24
                 (Log2Size << 5) | // was << 25
                 (IsExtern << 4) | // was << 27
                 (Type << 0));     // was << 28
}

static void
makeScatteredRelocationInfo(MachO::any_relocation_info &MRE,
                            const uint32_t Addr, const unsigned Type,
                            const unsigned Log2Size, const unsigned IsPCRel,
                            const uint32_t Value2) {
  // For notes on bitfield positions and endianness, see:
  // https://developer.apple.com/library/mac/documentation/developertools/conceptual/MachORuntime/Reference/reference.html#//apple_ref/doc/uid/20001298-scattered_relocation_entry
  MRE.r_word0 = ((Addr << 0) | (Type << 24) | (Log2Size << 28) |
                 (IsPCRel << 30) | MachO::R_SCATTERED);
  MRE.r_word1 = Value2;
}

/// Compute fixup offset (address).
static uint32_t getFixupOffset(const MCAsmLayout &Layout,
                               const MCFragment *Fragment,
                               const MCFixup &Fixup) {
  uint32_t FixupOffset = Layout.getFragmentOffset(Fragment) + Fixup.getOffset();
  // On Mach-O, ppc_fixup_half16 relocations must refer to the
  // start of the instruction, not the second halfword, as ELF does
  if (unsigned(Fixup.getKind()) == PPC::fixup_ppc_half16)
    FixupOffset &= ~uint32_t(3);
  return FixupOffset;
}

/// \return false if falling back to using non-scattered relocation,
/// otherwise true for normal scattered relocation.
/// based on X86MachObjectWriter::recordScatteredRelocation
/// and ARMMachObjectWriter::recordScatteredRelocation
bool PPCMachObjectWriter::recordScatteredRelocation(
    MachObjectWriter *Writer, const MCAssembler &Asm, const MCAsmLayout &Layout,
    const MCFragment *Fragment, const MCFixup &Fixup, MCValue Target,
    unsigned Log2Size, uint64_t &FixedValue) {
  // caller already computes these, can we just pass and reuse?
  const uint32_t FixupOffset = getFixupOffset(Layout, Fragment, Fixup);
  const MCFixupKind FK = Fixup.getKind();
  const unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, FK);
  const unsigned Type = getRelocType(Target, FK, IsPCRel);

  // Is this a local or SECTDIFF relocation entry?
  // SECTDIFF relocation entries have symbol subtractions,
  // and require two entries, the first for the add-symbol value,
  // the second for the subtract-symbol value.

  // See <reloc.h>.
  const MCSymbol *A = &Target.getSymA()->getSymbol();

  if (!A->getFragment())
    report_fatal_error("symbol '" + A->getName() +
                       "' can not be undefined in a subtraction expression");

  uint32_t Value = Writer->getSymbolAddress(*A, Layout);
  uint64_t SecAddr = Writer->getSectionAddress(A->getFragment()->getParent());
  FixedValue += SecAddr;
  uint32_t Value2 = 0;

  if (const MCSymbolRefExpr *B = Target.getSymB()) {
    const MCSymbol *SB = &B->getSymbol();

    if (!SB->getFragment())
      report_fatal_error("symbol '" + SB->getName() +
                         "' can not be undefined in a subtraction expression");

    // FIXME: is Type correct? see include/llvm/BinaryFormat/MachO.h
    Value2 = Writer->getSymbolAddress(*SB, Layout);
    FixedValue -= Writer->getSectionAddress(SB->getFragment()->getParent());
  }
  // FIXME: does FixedValue get used??

  // Relocations are written out in reverse order, so the PAIR comes first.
  if (Type == MachO::PPC_RELOC_SECTDIFF ||
      Type == MachO::PPC_RELOC_HI16_SECTDIFF ||
      Type == MachO::PPC_RELOC_LO16_SECTDIFF ||
      Type == MachO::PPC_RELOC_HA16_SECTDIFF ||
      Type == MachO::PPC_RELOC_LO14_SECTDIFF ||
      Type == MachO::PPC_RELOC_LOCAL_SECTDIFF) {
    // X86 had this piece, but ARM does not
    // If the offset is too large to fit in a scattered relocation,
    // we're hosed. It's an unfortunate limitation of the MachO format.
    if (FixupOffset > 0xffffff) {
      char Buffer[32];
      format("0x%x", FixupOffset).print(Buffer, sizeof(Buffer));
      Asm.getContext().reportError(Fixup.getLoc(),
                                  Twine("Section too large, can't encode "
                                        "r_address (") +
                                      Buffer + ") into 24 bits of scattered "
                                               "relocation entry.");
      return false;
    }

    // Is this supposed to follow MCTarget/PPCAsmBackend.cpp:adjustFixupValue()?
    // see PPCMCExpr::evaluateAsRelocatableImpl()
    uint32_t other_half = 0;
    switch (Type) {
    case MachO::PPC_RELOC_LO16_SECTDIFF:
      other_half = (FixedValue >> 16) & 0xffff;
      // applyFixupOffset longer extracts the high part because it now assumes
      // this was already done.
      // It looks like this is not true for the FixedValue needed with Mach-O
      // relocs.
      // So we need to adjust FixedValue again here.
      FixedValue &= 0xffff;
      break;
    case MachO::PPC_RELOC_HA16_SECTDIFF:
      other_half = FixedValue & 0xffff;
      FixedValue =
          ((FixedValue >> 16) + ((FixedValue & 0x8000) ? 1 : 0)) & 0xffff;
      break;
    case MachO::PPC_RELOC_HI16_SECTDIFF:
      other_half = FixedValue & 0xffff;
      FixedValue = (FixedValue >> 16) & 0xffff;
      break;
    default:
      llvm_unreachable("Invalid PPC scattered relocation type.");
      break;
    }

    MachO::any_relocation_info MRE;
    makeScatteredRelocationInfo(MRE, other_half, MachO::GENERIC_RELOC_PAIR,
                                Log2Size, IsPCRel, Value2);
    Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  } else {
    // If the offset is more than 24-bits, it won't fit in a scattered
    // relocation offset field, so we fall back to using a non-scattered
    // relocation. This is a bit risky, as if the offset reaches out of
    // the block and the linker is doing scattered loading on this
    // symbol, things can go badly.
    //
    // Required for 'as' compatibility.
    if (FixupOffset > 0xffffff)
      return false;
  }
  MachO::any_relocation_info MRE;
  makeScatteredRelocationInfo(MRE, FixupOffset, Type, Log2Size, IsPCRel, Value);
  Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  return true;
}

// see PPCELFObjectWriter for a general outline of cases
void PPCMachObjectWriter::RecordPPCRelocation(
    MachObjectWriter *Writer, const MCAssembler &Asm, const MCAsmLayout &Layout,
    const MCFragment *Fragment, const MCFixup &Fixup, MCValue Target,
    uint64_t &FixedValue) {
  const MCFixupKind FK = Fixup.getKind(); // unsigned
  const unsigned Log2Size = getFixupKindLog2Size(FK);
  const bool IsPCRel = Writer->isFixupKindPCRel(Asm, FK);
  const unsigned RelocType = getRelocType(Target, FK, IsPCRel);

  // If this is a difference or a defined symbol plus an offset, then we need a
  // scattered relocation entry. Differences always require scattered
  // relocations.
  if (Target.getSymB() &&
      // Q: are branch targets ever scattered?
      RelocType != MachO::PPC_RELOC_BR24 &&
      RelocType != MachO::PPC_RELOC_BR14) {
    recordScatteredRelocation(Writer, Asm, Layout, Fragment, Fixup, Target,
                              Log2Size, FixedValue);
    return;
  }

  // this doesn't seem right for RIT_PPC_BR24
  // Get the symbol data, if any.
  const MCSymbol *A = nullptr;
  if (Target.getSymA())
    A = &Target.getSymA()->getSymbol();

  // See <reloc.h>.
  const uint32_t FixupOffset = getFixupOffset(Layout, Fragment, Fixup);
  unsigned Index = 0;
  unsigned Type = RelocType;

  const MCSymbol *RelSymbol = nullptr;
  if (Target.isAbsolute()) { // constant
                             // SymbolNum of 0 indicates the absolute section.
                             //
    // FIXME: Currently, these are never generated (see code below). I cannot
    // find a case where they are actually emitted.
    report_fatal_error("FIXME: relocations to absolute targets "
                       "not yet implemented");
    // the above line stolen from ARM, not sure
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
    if (Writer->doesSymbolRequireExternRelocation(*A)) {
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
  }

  // struct relocation_info (8 bytes)
  MachO::any_relocation_info MRE;
  makeRelocationInfo(MRE, FixupOffset, Index, IsPCRel, Log2Size, false, Type);
  Writer->addRelocation(RelSymbol, Fragment->getParent(), MRE);
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createPPCMachObjectWriter(bool Is64Bit, uint32_t CPUType,
                                uint32_t CPUSubtype) {
  return llvm::make_unique<PPCMachObjectWriter>(Is64Bit, CPUType, CPUSubtype);
}
