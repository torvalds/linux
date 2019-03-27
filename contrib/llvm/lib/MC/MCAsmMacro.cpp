//===- MCAsmMacro.h - Assembly Macros ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmMacro.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void MCAsmMacroParameter::dump(raw_ostream &OS) const {
  OS << "\"" << Name << "\"";
  if (Required)
    OS << ":req";
  if (Vararg)
    OS << ":vararg";
  if (!Value.empty()) {
    OS << " = ";
    bool first = true;
    for (const AsmToken &T : Value) {
      if (!first)
        OS << ", ";
      first = false;
      OS << T.getString();
    }
  }
  OS << "\n";
}

void MCAsmMacro::dump(raw_ostream &OS) const {
  OS << "Macro " << Name << ":\n";
  OS << "  Parameters:\n";
  for (const MCAsmMacroParameter &P : Parameters) {
    OS << "    ";
    P.dump();
  }
  OS << "  (BEGIN BODY)" << Body << "(END BODY)\n";
}
