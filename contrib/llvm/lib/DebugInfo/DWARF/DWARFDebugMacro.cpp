//===- DWARFDebugMacro.cpp ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;
using namespace dwarf;

void DWARFDebugMacro::dump(raw_ostream &OS) const {
  unsigned IndLevel = 0;
  for (const Entry &E : Macros) {
    // There should not be DW_MACINFO_end_file when IndLevel is Zero. However,
    // this check handles the case of corrupted ".debug_macinfo" section.
    if (IndLevel > 0)
      IndLevel -= (E.Type == DW_MACINFO_end_file);
    // Print indentation.
    for (unsigned I = 0; I < IndLevel; I++)
      OS << "  ";
    IndLevel += (E.Type == DW_MACINFO_start_file);

    WithColor(OS, HighlightColor::Macro).get() << MacinfoString(E.Type);
    switch (E.Type) {
    default:
      // Got a corrupted ".debug_macinfo" section (invalid macinfo type).
      break;
    case DW_MACINFO_define:
    case DW_MACINFO_undef:
      OS << " - lineno: " << E.Line;
      OS << " macro: " << E.MacroStr;
      break;
    case DW_MACINFO_start_file:
      OS << " - lineno: " << E.Line;
      OS << " filenum: " << E.File;
      break;
    case DW_MACINFO_end_file:
      break;
    case DW_MACINFO_vendor_ext:
      OS << " - constant: " << E.ExtConstant;
      OS << " string: " << E.ExtStr;
      break;
    }
    OS << "\n";
  }
}

void DWARFDebugMacro::parse(DataExtractor data) {
  uint32_t Offset = 0;
  while (data.isValidOffset(Offset)) {
    // A macro list entry consists of:
    Entry E;
    // 1. Macinfo type
    E.Type = data.getULEB128(&Offset);

    if (E.Type == 0) {
      // Reached end of ".debug_macinfo" section.
      return;
    }

    switch (E.Type) {
    default:
      // Got a corrupted ".debug_macinfo" section (invalid macinfo type).
      // Push the corrupted entry to the list and halt parsing.
      E.Type = DW_MACINFO_invalid;
      Macros.push_back(E);
      return;
    case DW_MACINFO_define:
    case DW_MACINFO_undef:
      // 2. Source line
      E.Line = data.getULEB128(&Offset);
      // 3. Macro string
      E.MacroStr = data.getCStr(&Offset);
      break;
    case DW_MACINFO_start_file:
      // 2. Source line
      E.Line = data.getULEB128(&Offset);
      // 3. Source file id
      E.File = data.getULEB128(&Offset);
      break;
    case DW_MACINFO_end_file:
      break;
    case DW_MACINFO_vendor_ext:
      // 2. Vendor extension constant
      E.ExtConstant = data.getULEB128(&Offset);
      // 3. Vendor extension string
      E.ExtStr = data.getCStr(&Offset);
      break;
    }

    Macros.push_back(E);
  }
}
