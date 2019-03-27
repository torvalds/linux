//===- COFFYAML.h - COFF YAMLIO implementation ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares classes for handling the YAML representation of COFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_COFFYAML_H
#define LLVM_OBJECTYAML_COFFYAML_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypeHashing.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/ObjectYAML/YAML.h"
#include <cstdint>
#include <vector>

namespace llvm {

namespace COFF {

inline Characteristics operator|(Characteristics a, Characteristics b) {
  uint32_t Ret = static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
  return static_cast<Characteristics>(Ret);
}

inline SectionCharacteristics operator|(SectionCharacteristics a,
                                        SectionCharacteristics b) {
  uint32_t Ret = static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
  return static_cast<SectionCharacteristics>(Ret);
}

inline DLLCharacteristics operator|(DLLCharacteristics a,
                                    DLLCharacteristics b) {
  uint16_t Ret = static_cast<uint16_t>(a) | static_cast<uint16_t>(b);
  return static_cast<DLLCharacteristics>(Ret);
}

} // end namespace COFF

// The structure of the yaml files is not an exact 1:1 match to COFF. In order
// to use yaml::IO, we use these structures which are closer to the source.
namespace COFFYAML {

LLVM_YAML_STRONG_TYPEDEF(uint8_t, COMDATType)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, WeakExternalCharacteristics)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, AuxSymbolType)

struct Relocation {
  uint32_t VirtualAddress;
  uint16_t Type;

  // Normally a Relocation can refer to the symbol via its name.
  // It can also use a direct symbol table index instead (with no name
  // specified), allowing disambiguating between multiple symbols with the
  // same name or crafting intentionally broken files for testing.
  StringRef SymbolName;
  Optional<uint32_t> SymbolTableIndex;
};

struct Section {
  COFF::section Header;
  unsigned Alignment = 0;
  yaml::BinaryRef SectionData;
  std::vector<CodeViewYAML::YAMLDebugSubsection> DebugS;
  std::vector<CodeViewYAML::LeafRecord> DebugT;
  std::vector<CodeViewYAML::LeafRecord> DebugP;
  Optional<CodeViewYAML::DebugHSection> DebugH;
  std::vector<Relocation> Relocations;
  StringRef Name;

  Section();
};

struct Symbol {
  COFF::symbol Header;
  COFF::SymbolBaseType SimpleType = COFF::IMAGE_SYM_TYPE_NULL;
  COFF::SymbolComplexType ComplexType = COFF::IMAGE_SYM_DTYPE_NULL;
  Optional<COFF::AuxiliaryFunctionDefinition> FunctionDefinition;
  Optional<COFF::AuxiliarybfAndefSymbol> bfAndefSymbol;
  Optional<COFF::AuxiliaryWeakExternal> WeakExternal;
  StringRef File;
  Optional<COFF::AuxiliarySectionDefinition> SectionDefinition;
  Optional<COFF::AuxiliaryCLRToken> CLRToken;
  StringRef Name;

  Symbol();
};

struct PEHeader {
  COFF::PE32Header Header;
  Optional<COFF::DataDirectory> DataDirectories[COFF::NUM_DATA_DIRECTORIES];
};

struct Object {
  Optional<PEHeader> OptionalHeader;
  COFF::header Header;
  std::vector<Section> Sections;
  std::vector<Symbol> Symbols;

  Object();
};

} // end namespace COFFYAML

} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(COFFYAML::Section)
LLVM_YAML_IS_SEQUENCE_VECTOR(COFFYAML::Symbol)
LLVM_YAML_IS_SEQUENCE_VECTOR(COFFYAML::Relocation)

namespace llvm {
namespace yaml {

template <>
struct ScalarEnumerationTraits<COFFYAML::WeakExternalCharacteristics> {
  static void enumeration(IO &IO, COFFYAML::WeakExternalCharacteristics &Value);
};

template <>
struct ScalarEnumerationTraits<COFFYAML::AuxSymbolType> {
  static void enumeration(IO &IO, COFFYAML::AuxSymbolType &Value);
};

template <>
struct ScalarEnumerationTraits<COFFYAML::COMDATType> {
  static void enumeration(IO &IO, COFFYAML::COMDATType &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::MachineTypes> {
  static void enumeration(IO &IO, COFF::MachineTypes &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::SymbolBaseType> {
  static void enumeration(IO &IO, COFF::SymbolBaseType &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::SymbolStorageClass> {
  static void enumeration(IO &IO, COFF::SymbolStorageClass &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::SymbolComplexType> {
  static void enumeration(IO &IO, COFF::SymbolComplexType &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::RelocationTypeI386> {
  static void enumeration(IO &IO, COFF::RelocationTypeI386 &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::RelocationTypeAMD64> {
  static void enumeration(IO &IO, COFF::RelocationTypeAMD64 &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::RelocationTypesARM> {
  static void enumeration(IO &IO, COFF::RelocationTypesARM &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::RelocationTypesARM64> {
  static void enumeration(IO &IO, COFF::RelocationTypesARM64 &Value);
};

template <>
struct ScalarEnumerationTraits<COFF::WindowsSubsystem> {
  static void enumeration(IO &IO, COFF::WindowsSubsystem &Value);
};

template <>
struct ScalarBitSetTraits<COFF::Characteristics> {
  static void bitset(IO &IO, COFF::Characteristics &Value);
};

template <>
struct ScalarBitSetTraits<COFF::SectionCharacteristics> {
  static void bitset(IO &IO, COFF::SectionCharacteristics &Value);
};

template <>
struct ScalarBitSetTraits<COFF::DLLCharacteristics> {
  static void bitset(IO &IO, COFF::DLLCharacteristics &Value);
};

template <>
struct MappingTraits<COFFYAML::Relocation> {
  static void mapping(IO &IO, COFFYAML::Relocation &Rel);
};

template <>
struct MappingTraits<COFFYAML::PEHeader> {
  static void mapping(IO &IO, COFFYAML::PEHeader &PH);
};

template <>
struct MappingTraits<COFF::DataDirectory> {
  static void mapping(IO &IO, COFF::DataDirectory &DD);
};

template <>
struct MappingTraits<COFF::header> {
  static void mapping(IO &IO, COFF::header &H);
};

template <> struct MappingTraits<COFF::AuxiliaryFunctionDefinition> {
  static void mapping(IO &IO, COFF::AuxiliaryFunctionDefinition &AFD);
};

template <> struct MappingTraits<COFF::AuxiliarybfAndefSymbol> {
  static void mapping(IO &IO, COFF::AuxiliarybfAndefSymbol &AAS);
};

template <> struct MappingTraits<COFF::AuxiliaryWeakExternal> {
  static void mapping(IO &IO, COFF::AuxiliaryWeakExternal &AWE);
};

template <> struct MappingTraits<COFF::AuxiliarySectionDefinition> {
  static void mapping(IO &IO, COFF::AuxiliarySectionDefinition &ASD);
};

template <> struct MappingTraits<COFF::AuxiliaryCLRToken> {
  static void mapping(IO &IO, COFF::AuxiliaryCLRToken &ACT);
};

template <>
struct MappingTraits<COFFYAML::Symbol> {
  static void mapping(IO &IO, COFFYAML::Symbol &S);
};

template <>
struct MappingTraits<COFFYAML::Section> {
  static void mapping(IO &IO, COFFYAML::Section &Sec);
};

template <>
struct MappingTraits<COFFYAML::Object> {
  static void mapping(IO &IO, COFFYAML::Object &Obj);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_COFFYAML_H
