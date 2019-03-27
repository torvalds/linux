//===- ObjectYAML.cpp - YAML utilities for object files -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper class for handling tagged YAML input
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"
#include <string>

using namespace llvm;
using namespace yaml;

void MappingTraits<YamlObjectFile>::mapping(IO &IO,
                                            YamlObjectFile &ObjectFile) {
  if (IO.outputting()) {
    if (ObjectFile.Elf)
      MappingTraits<ELFYAML::Object>::mapping(IO, *ObjectFile.Elf);
    if (ObjectFile.Coff)
      MappingTraits<COFFYAML::Object>::mapping(IO, *ObjectFile.Coff);
    if (ObjectFile.MachO)
      MappingTraits<MachOYAML::Object>::mapping(IO, *ObjectFile.MachO);
    if (ObjectFile.FatMachO)
      MappingTraits<MachOYAML::UniversalBinary>::mapping(IO,
                                                         *ObjectFile.FatMachO);
  } else {
    if (IO.mapTag("!ELF")) {
      ObjectFile.Elf.reset(new ELFYAML::Object());
      MappingTraits<ELFYAML::Object>::mapping(IO, *ObjectFile.Elf);
    } else if (IO.mapTag("!COFF")) {
      ObjectFile.Coff.reset(new COFFYAML::Object());
      MappingTraits<COFFYAML::Object>::mapping(IO, *ObjectFile.Coff);
    } else if (IO.mapTag("!mach-o")) {
      ObjectFile.MachO.reset(new MachOYAML::Object());
      MappingTraits<MachOYAML::Object>::mapping(IO, *ObjectFile.MachO);
    } else if (IO.mapTag("!fat-mach-o")) {
      ObjectFile.FatMachO.reset(new MachOYAML::UniversalBinary());
      MappingTraits<MachOYAML::UniversalBinary>::mapping(IO,
                                                         *ObjectFile.FatMachO);
    } else if (IO.mapTag("!WASM")) {
      ObjectFile.Wasm.reset(new WasmYAML::Object());
      MappingTraits<WasmYAML::Object>::mapping(IO, *ObjectFile.Wasm);
    } else {
      Input &In = (Input &)IO;
      std::string Tag = In.getCurrentNode()->getRawTag();
      if (Tag.empty())
        IO.setError("YAML Object File missing document type tag!");
      else
        IO.setError(
            Twine("YAML Object File unsupported document type tag '") +
            Twine(Tag) + Twine("'!"));
    }
  }
}
