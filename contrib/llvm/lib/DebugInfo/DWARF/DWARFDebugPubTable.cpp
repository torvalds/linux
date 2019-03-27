//===- DWARFDebugPubTable.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;
using namespace dwarf;

DWARFDebugPubTable::DWARFDebugPubTable(const DWARFObject &Obj,
                                       const DWARFSection &Sec,
                                       bool LittleEndian, bool GnuStyle)
    : GnuStyle(GnuStyle) {
  DWARFDataExtractor PubNames(Obj, Sec, LittleEndian, 0);
  uint32_t Offset = 0;
  while (PubNames.isValidOffset(Offset)) {
    Sets.push_back({});
    Set &SetData = Sets.back();

    SetData.Length = PubNames.getU32(&Offset);
    SetData.Version = PubNames.getU16(&Offset);
    SetData.Offset = PubNames.getRelocatedValue(4, &Offset);
    SetData.Size = PubNames.getU32(&Offset);

    while (Offset < Sec.Data.size()) {
      uint32_t DieRef = PubNames.getU32(&Offset);
      if (DieRef == 0)
        break;
      uint8_t IndexEntryValue = GnuStyle ? PubNames.getU8(&Offset) : 0;
      StringRef Name = PubNames.getCStrRef(&Offset);
      SetData.Entries.push_back(
          {DieRef, PubIndexEntryDescriptor(IndexEntryValue), Name});
    }
  }
}

void DWARFDebugPubTable::dump(raw_ostream &OS) const {
  for (const Set &S : Sets) {
    OS << "length = " << format("0x%08x", S.Length);
    OS << " version = " << format("0x%04x", S.Version);
    OS << " unit_offset = " << format("0x%08x", S.Offset);
    OS << " unit_size = " << format("0x%08x", S.Size) << '\n';
    OS << (GnuStyle ? "Offset     Linkage  Kind     Name\n"
                    : "Offset     Name\n");

    for (const Entry &E : S.Entries) {
      OS << format("0x%8.8x ", E.SecOffset);
      if (GnuStyle) {
        StringRef EntryLinkage =
            GDBIndexEntryLinkageString(E.Descriptor.Linkage);
        StringRef EntryKind = dwarf::GDBIndexEntryKindString(E.Descriptor.Kind);
        OS << format("%-8s", EntryLinkage.data()) << ' '
           << format("%-8s", EntryKind.data()) << ' ';
      }
      OS << '\"' << E.Name << "\"\n";
    }
  }
}
