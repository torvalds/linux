//===- DWARFTypeUnit.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>

using namespace llvm;

void DWARFTypeUnit::dump(raw_ostream &OS, DIDumpOptions DumpOpts) {
  DWARFDie TD = getDIEForOffset(getTypeOffset() + getOffset());
  const char *Name = TD.getName(DINameKind::ShortName);

  if (DumpOpts.SummarizeTypes) {
    OS << "name = '" << Name << "'"
       << " type_signature = " << format("0x%016" PRIx64, getTypeHash())
       << " length = " << format("0x%08x", getLength()) << '\n';
    return;
  }

  OS << format("0x%08x", getOffset()) << ": Type Unit:"
     << " length = " << format("0x%08x", getLength())
     << " version = " << format("0x%04x", getVersion());
  if (getVersion() >= 5)
    OS << " unit_type = " << dwarf::UnitTypeString(getUnitType());
  OS << " abbr_offset = " << format("0x%04x", getAbbreviations()->getOffset())
     << " addr_size = " << format("0x%02x", getAddressByteSize())
     << " name = '" << Name << "'"
     << " type_signature = " << format("0x%016" PRIx64, getTypeHash())
     << " type_offset = " << format("0x%04x", getTypeOffset())
     << " (next unit at " << format("0x%08x", getNextUnitOffset()) << ")\n";

  if (DWARFDie TU = getUnitDIE(false))
    TU.dump(OS, 0, DumpOpts);
  else
    OS << "<type unit can't be parsed!>\n\n";
}
