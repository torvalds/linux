//===- MCSectionMachO.h - MachO Machine Code Sections -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionMachO class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONMACHO_H
#define LLVM_MC_MCSECTIONMACHO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCSection.h"

namespace llvm {

/// This represents a section on a Mach-O system (used by Mac OS X).  On a Mac
/// system, these are also described in /usr/include/mach-o/loader.h.
class MCSectionMachO final : public MCSection {
  char SegmentName[16];  // Not necessarily null terminated!
  char SectionName[16];  // Not necessarily null terminated!

  /// This is the SECTION_TYPE and SECTION_ATTRIBUTES field of a section, drawn
  /// from the enums below.
  unsigned TypeAndAttributes;

  /// The 'reserved2' field of a section, used to represent the size of stubs,
  /// for example.
  unsigned Reserved2;

  MCSectionMachO(StringRef Segment, StringRef Section, unsigned TAA,
                 unsigned reserved2, SectionKind K, MCSymbol *Begin);
  friend class MCContext;
public:

  StringRef getSegmentName() const {
    // SegmentName is not necessarily null terminated!
    if (SegmentName[15])
      return StringRef(SegmentName, 16);
    return StringRef(SegmentName);
  }
  StringRef getSectionName() const {
    // SectionName is not necessarily null terminated!
    if (SectionName[15])
      return StringRef(SectionName, 16);
    return StringRef(SectionName);
  }

  unsigned getTypeAndAttributes() const { return TypeAndAttributes; }
  unsigned getStubSize() const { return Reserved2; }

  MachO::SectionType getType() const {
    return static_cast<MachO::SectionType>(TypeAndAttributes &
                                           MachO::SECTION_TYPE);
  }
  bool hasAttribute(unsigned Value) const {
    return (TypeAndAttributes & Value) != 0;
  }

  /// Parse the section specifier indicated by "Spec". This is a string that can
  /// appear after a .section directive in a mach-o flavored .s file.  If
  /// successful, this fills in the specified Out parameters and returns an
  /// empty string.  When an invalid section specifier is present, this returns
  /// a string indicating the problem. If no TAA was parsed, TAA is not altered,
  /// and TAAWasSet becomes false.
  static std::string ParseSectionSpecifier(StringRef Spec,       // In.
                                           StringRef &Segment,   // Out.
                                           StringRef &Section,   // Out.
                                           unsigned  &TAA,       // Out.
                                           bool      &TAAParsed, // Out.
                                           unsigned  &StubSize); // Out.

  void PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            const MCExpr *Subsection) const override;
  bool UseCodeAlign() const override;
  bool isVirtualSection() const override;

  static bool classof(const MCSection *S) {
    return S->getVariant() == SV_MachO;
  }
};

} // end namespace llvm

#endif
