//===- DWARFYAML.cpp - DWARF YAMLIO implementation ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of DWARF Debug
// Info.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/DWARFYAML.h"

namespace llvm {

bool DWARFYAML::Data::isEmpty() const {
  return 0 == DebugStrings.size() + AbbrevDecls.size();
}

namespace yaml {

void MappingTraits<DWARFYAML::Data>::mapping(IO &IO, DWARFYAML::Data &DWARF) {
  auto oldContext = IO.getContext();
  IO.setContext(&DWARF);
  IO.mapOptional("debug_str", DWARF.DebugStrings);
  IO.mapOptional("debug_abbrev", DWARF.AbbrevDecls);
  if (!DWARF.ARanges.empty() || !IO.outputting())
    IO.mapOptional("debug_aranges", DWARF.ARanges);
  if (!DWARF.PubNames.Entries.empty() || !IO.outputting())
    IO.mapOptional("debug_pubnames", DWARF.PubNames);
  if (!DWARF.PubTypes.Entries.empty() || !IO.outputting())
    IO.mapOptional("debug_pubtypes", DWARF.PubTypes);
  if (!DWARF.GNUPubNames.Entries.empty() || !IO.outputting())
    IO.mapOptional("debug_gnu_pubnames", DWARF.GNUPubNames);
  if (!DWARF.GNUPubTypes.Entries.empty() || !IO.outputting())
    IO.mapOptional("debug_gnu_pubtypes", DWARF.GNUPubTypes);
  IO.mapOptional("debug_info", DWARF.CompileUnits);
  IO.mapOptional("debug_line", DWARF.DebugLines);
  IO.setContext(&oldContext);
}

void MappingTraits<DWARFYAML::Abbrev>::mapping(IO &IO,
                                               DWARFYAML::Abbrev &Abbrev) {
  IO.mapRequired("Code", Abbrev.Code);
  IO.mapRequired("Tag", Abbrev.Tag);
  IO.mapRequired("Children", Abbrev.Children);
  IO.mapRequired("Attributes", Abbrev.Attributes);
}

void MappingTraits<DWARFYAML::AttributeAbbrev>::mapping(
    IO &IO, DWARFYAML::AttributeAbbrev &AttAbbrev) {
  IO.mapRequired("Attribute", AttAbbrev.Attribute);
  IO.mapRequired("Form", AttAbbrev.Form);
  if(AttAbbrev.Form == dwarf::DW_FORM_implicit_const)
    IO.mapRequired("Value", AttAbbrev.Value);
}

void MappingTraits<DWARFYAML::ARangeDescriptor>::mapping(
    IO &IO, DWARFYAML::ARangeDescriptor &Descriptor) {
  IO.mapRequired("Address", Descriptor.Address);
  IO.mapRequired("Length", Descriptor.Length);
}

void MappingTraits<DWARFYAML::ARange>::mapping(IO &IO,
                                               DWARFYAML::ARange &Range) {
  IO.mapRequired("Length", Range.Length);
  IO.mapRequired("Version", Range.Version);
  IO.mapRequired("CuOffset", Range.CuOffset);
  IO.mapRequired("AddrSize", Range.AddrSize);
  IO.mapRequired("SegSize", Range.SegSize);
  IO.mapRequired("Descriptors", Range.Descriptors);
}

void MappingTraits<DWARFYAML::PubEntry>::mapping(IO &IO,
                                                 DWARFYAML::PubEntry &Entry) {
  IO.mapRequired("DieOffset", Entry.DieOffset);
  if (reinterpret_cast<DWARFYAML::PubSection *>(IO.getContext())->IsGNUStyle)
    IO.mapRequired("Descriptor", Entry.Descriptor);
  IO.mapRequired("Name", Entry.Name);
}

void MappingTraits<DWARFYAML::PubSection>::mapping(
    IO &IO, DWARFYAML::PubSection &Section) {
  auto OldContext = IO.getContext();
  IO.setContext(&Section);

  IO.mapRequired("Length", Section.Length);
  IO.mapRequired("Version", Section.Version);
  IO.mapRequired("UnitOffset", Section.UnitOffset);
  IO.mapRequired("UnitSize", Section.UnitSize);
  IO.mapRequired("Entries", Section.Entries);

  IO.setContext(OldContext);
}

void MappingTraits<DWARFYAML::Unit>::mapping(IO &IO, DWARFYAML::Unit &Unit) {
  IO.mapRequired("Length", Unit.Length);
  IO.mapRequired("Version", Unit.Version);
  if (Unit.Version >= 5)
    IO.mapRequired("UnitType", Unit.Type);
  IO.mapRequired("AbbrOffset", Unit.AbbrOffset);
  IO.mapRequired("AddrSize", Unit.AddrSize);
  IO.mapOptional("Entries", Unit.Entries);
}

void MappingTraits<DWARFYAML::Entry>::mapping(IO &IO, DWARFYAML::Entry &Entry) {
  IO.mapRequired("AbbrCode", Entry.AbbrCode);
  IO.mapRequired("Values", Entry.Values);
}

void MappingTraits<DWARFYAML::FormValue>::mapping(
    IO &IO, DWARFYAML::FormValue &FormValue) {
  IO.mapOptional("Value", FormValue.Value);
  if (!FormValue.CStr.empty() || !IO.outputting())
    IO.mapOptional("CStr", FormValue.CStr);
  if (!FormValue.BlockData.empty() || !IO.outputting())
    IO.mapOptional("BlockData", FormValue.BlockData);
}

void MappingTraits<DWARFYAML::File>::mapping(IO &IO, DWARFYAML::File &File) {
  IO.mapRequired("Name", File.Name);
  IO.mapRequired("DirIdx", File.DirIdx);
  IO.mapRequired("ModTime", File.ModTime);
  IO.mapRequired("Length", File.Length);
}

void MappingTraits<DWARFYAML::LineTableOpcode>::mapping(
    IO &IO, DWARFYAML::LineTableOpcode &LineTableOpcode) {
  IO.mapRequired("Opcode", LineTableOpcode.Opcode);
  if (LineTableOpcode.Opcode == dwarf::DW_LNS_extended_op) {
    IO.mapRequired("ExtLen", LineTableOpcode.ExtLen);
    IO.mapRequired("SubOpcode", LineTableOpcode.SubOpcode);
  }

  if (!LineTableOpcode.UnknownOpcodeData.empty() || !IO.outputting())
    IO.mapOptional("UnknownOpcodeData", LineTableOpcode.UnknownOpcodeData);
  if (!LineTableOpcode.UnknownOpcodeData.empty() || !IO.outputting())
    IO.mapOptional("StandardOpcodeData", LineTableOpcode.StandardOpcodeData);
  if (!LineTableOpcode.FileEntry.Name.empty() || !IO.outputting())
    IO.mapOptional("FileEntry", LineTableOpcode.FileEntry);
  if (LineTableOpcode.Opcode == dwarf::DW_LNS_advance_line || !IO.outputting())
    IO.mapOptional("SData", LineTableOpcode.SData);
  IO.mapOptional("Data", LineTableOpcode.Data);
}

void MappingTraits<DWARFYAML::LineTable>::mapping(
    IO &IO, DWARFYAML::LineTable &LineTable) {
  IO.mapRequired("Length", LineTable.Length);
  IO.mapRequired("Version", LineTable.Version);
  IO.mapRequired("PrologueLength", LineTable.PrologueLength);
  IO.mapRequired("MinInstLength", LineTable.MinInstLength);
  if(LineTable.Version >= 4)
    IO.mapRequired("MaxOpsPerInst", LineTable.MaxOpsPerInst);
  IO.mapRequired("DefaultIsStmt", LineTable.DefaultIsStmt);
  IO.mapRequired("LineBase", LineTable.LineBase);
  IO.mapRequired("LineRange", LineTable.LineRange);
  IO.mapRequired("OpcodeBase", LineTable.OpcodeBase);
  IO.mapRequired("StandardOpcodeLengths", LineTable.StandardOpcodeLengths);
  IO.mapRequired("IncludeDirs", LineTable.IncludeDirs);
  IO.mapRequired("Files", LineTable.Files);
  IO.mapRequired("Opcodes", LineTable.Opcodes);
}

void MappingTraits<DWARFYAML::InitialLength>::mapping(
    IO &IO, DWARFYAML::InitialLength &InitialLength) {
  IO.mapRequired("TotalLength", InitialLength.TotalLength);
  if (InitialLength.isDWARF64())
    IO.mapRequired("TotalLength64", InitialLength.TotalLength64);
}

} // end namespace yaml

} // end namespace llvm
