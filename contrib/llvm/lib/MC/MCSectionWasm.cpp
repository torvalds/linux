//===- lib/MC/MCSectionWasm.cpp - Wasm Code Section Representation --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCSectionWasm::~MCSectionWasm() {} // anchor.

// Decides whether a '.section' directive
// should be printed before the section name.
bool MCSectionWasm::ShouldOmitSectionDirective(StringRef Name,
                                               const MCAsmInfo &MAI) const {
  return MAI.shouldOmitSectionDirective(Name);
}

static void printName(raw_ostream &OS, StringRef Name) {
  if (Name.find_first_not_of("0123456789_."
                             "abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == Name.npos) {
    OS << Name;
    return;
  }
  OS << '"';
  for (const char *B = Name.begin(), *E = Name.end(); B < E; ++B) {
    if (*B == '"') // Unquoted "
      OS << "\\\"";
    else if (*B != '\\') // Neither " or backslash
      OS << *B;
    else if (B + 1 == E) // Trailing backslash
      OS << "\\\\";
    else {
      OS << B[0] << B[1]; // Quoted character
      ++B;
    }
  }
  OS << '"';
}

void MCSectionWasm::PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                         raw_ostream &OS,
                                         const MCExpr *Subsection) const {

  if (ShouldOmitSectionDirective(SectionName, MAI)) {
    OS << '\t' << getSectionName();
    if (Subsection) {
      OS << '\t';
      Subsection->print(OS, &MAI);
    }
    OS << '\n';
    return;
  }

  OS << "\t.section\t";
  printName(OS, getSectionName());
  OS << ",\"";

  // TODO: Print section flags.

  OS << '"';

  OS << ',';

  // If comment string is '@', e.g. as on ARM - use '%' instead
  if (MAI.getCommentString()[0] == '@')
    OS << '%';
  else
    OS << '@';

  // TODO: Print section type.

  if (isUnique())
    OS << ",unique," << UniqueID;

  OS << '\n';

  if (Subsection) {
    OS << "\t.subsection\t";
    Subsection->print(OS, &MAI);
    OS << '\n';
  }
}

bool MCSectionWasm::UseCodeAlign() const { return false; }

bool MCSectionWasm::isVirtualSection() const { return false; }
