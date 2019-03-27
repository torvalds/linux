//===- lib/MC/MCSectionCOFF.cpp - COFF Code Section Representation --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

MCSectionCOFF::~MCSectionCOFF() = default; // anchor.

// ShouldOmitSectionDirective - Decides whether a '.section' directive
// should be printed before the section name
bool MCSectionCOFF::ShouldOmitSectionDirective(StringRef Name,
                                               const MCAsmInfo &MAI) const {
  if (COMDATSymbol)
    return false;

  // FIXME: Does .section .bss/.data/.text work everywhere??
  if (Name == ".text" || Name == ".data" || Name == ".bss")
    return true;

  return false;
}

void MCSectionCOFF::setSelection(int Selection) const {
  assert(Selection != 0 && "invalid COMDAT selection type");
  this->Selection = Selection;
  Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
}

void MCSectionCOFF::PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                         raw_ostream &OS,
                                         const MCExpr *Subsection) const {
  // standard sections don't require the '.section'
  if (ShouldOmitSectionDirective(SectionName, MAI)) {
    OS << '\t' << getSectionName() << '\n';
    return;
  }

  OS << "\t.section\t" << getSectionName() << ",\"";
  if (getCharacteristics() & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA)
    OS << 'd';
  if (getCharacteristics() & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)
    OS << 'b';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_EXECUTE)
    OS << 'x';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_WRITE)
    OS << 'w';
  else if (getCharacteristics() & COFF::IMAGE_SCN_MEM_READ)
    OS << 'r';
  else
    OS << 'y';
  if (getCharacteristics() & COFF::IMAGE_SCN_LNK_REMOVE)
    OS << 'n';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_SHARED)
    OS << 's';
  if ((getCharacteristics() & COFF::IMAGE_SCN_MEM_DISCARDABLE) &&
      !isImplicitlyDiscardable(SectionName))
    OS << 'D';
  OS << '"';

  if (getCharacteristics() & COFF::IMAGE_SCN_LNK_COMDAT) {
    if (COMDATSymbol)
      OS << ",";
    else
      OS << "\n\t.linkonce\t";
    switch (Selection) {
      case COFF::IMAGE_COMDAT_SELECT_NODUPLICATES:
        OS << "one_only";
        break;
      case COFF::IMAGE_COMDAT_SELECT_ANY:
        OS << "discard";
        break;
      case COFF::IMAGE_COMDAT_SELECT_SAME_SIZE:
        OS << "same_size";
        break;
      case COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH:
        OS << "same_contents";
        break;
      case COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE:
        OS << "associative";
        break;
      case COFF::IMAGE_COMDAT_SELECT_LARGEST:
        OS << "largest";
        break;
      case COFF::IMAGE_COMDAT_SELECT_NEWEST:
        OS << "newest";
        break;
      default:
        assert(false && "unsupported COFF selection type");
        break;
    }
    if (COMDATSymbol) {
      OS << ",";
      COMDATSymbol->print(OS, &MAI);
    }
  }
  OS << '\n';
}

bool MCSectionCOFF::UseCodeAlign() const {
  return getKind().isText();
}

bool MCSectionCOFF::isVirtualSection() const {
  return getCharacteristics() & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA;
}
