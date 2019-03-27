//===- MCSectionCOFF.h - COFF Machine Code Sections -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionCOFF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONCOFF_H
#define LLVM_MC_MCSECTIONCOFF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"
#include <cassert>

namespace llvm {

class MCSymbol;

/// This represents a section on Windows
class MCSectionCOFF final : public MCSection {
  // The memory for this string is stored in the same MCContext as *this.
  StringRef SectionName;

  // FIXME: The following fields should not be mutable, but are for now so the
  // asm parser can honor the .linkonce directive.

  /// This is the Characteristics field of a section, drawn from the enums
  /// below.
  mutable unsigned Characteristics;

  /// The unique IDs used with the .pdata and .xdata sections created internally
  /// by the assembler. This ID is used to ensure that for every .text section,
  /// there is exactly one .pdata and one .xdata section, which is required by
  /// the Microsoft incremental linker. This data is mutable because this ID is
  /// not notionally part of the section.
  mutable unsigned WinCFISectionID = ~0U;

  /// The COMDAT symbol of this section. Only valid if this is a COMDAT section.
  /// Two COMDAT sections are merged if they have the same COMDAT symbol.
  MCSymbol *COMDATSymbol;

  /// This is the Selection field for the section symbol, if it is a COMDAT
  /// section (Characteristics & IMAGE_SCN_LNK_COMDAT) != 0
  mutable int Selection;

private:
  friend class MCContext;
  MCSectionCOFF(StringRef Section, unsigned Characteristics,
                MCSymbol *COMDATSymbol, int Selection, SectionKind K,
                MCSymbol *Begin)
      : MCSection(SV_COFF, K, Begin), SectionName(Section),
        Characteristics(Characteristics), COMDATSymbol(COMDATSymbol),
        Selection(Selection) {
    assert((Characteristics & 0x00F00000) == 0 &&
           "alignment must not be set upon section creation");
  }

public:
  ~MCSectionCOFF();

  /// Decides whether a '.section' directive should be printed before the
  /// section name
  bool ShouldOmitSectionDirective(StringRef Name, const MCAsmInfo &MAI) const;

  StringRef getSectionName() const { return SectionName; }
  unsigned getCharacteristics() const { return Characteristics; }
  MCSymbol *getCOMDATSymbol() const { return COMDATSymbol; }
  int getSelection() const { return Selection; }

  void setSelection(int Selection) const;

  void PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            const MCExpr *Subsection) const override;
  bool UseCodeAlign() const override;
  bool isVirtualSection() const override;

  unsigned getOrAssignWinCFISectionID(unsigned *NextID) const {
    if (WinCFISectionID == ~0U)
      WinCFISectionID = (*NextID)++;
    return WinCFISectionID;
  }

  static bool isImplicitlyDiscardable(StringRef Name) {
    return Name.startswith(".debug");
  }

  static bool classof(const MCSection *S) { return S->getVariant() == SV_COFF; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONCOFF_H
