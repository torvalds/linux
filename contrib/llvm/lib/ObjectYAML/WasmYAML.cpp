//===- WasmYAML.cpp - Wasm YAMLIO implementation --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of wasm.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/WasmYAML.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {

namespace WasmYAML {

// Declared here rather than in the header to comply with:
// http://llvm.org/docs/CodingStandards.html#provide-a-virtual-method-anchor-for-classes-in-headers
Section::~Section() = default;

} // end namespace WasmYAML

namespace yaml {

void MappingTraits<WasmYAML::FileHeader>::mapping(
    IO &IO, WasmYAML::FileHeader &FileHdr) {
  IO.mapRequired("Version", FileHdr.Version);
}

void MappingTraits<WasmYAML::Object>::mapping(IO &IO,
                                              WasmYAML::Object &Object) {
  IO.setContext(&Object);
  IO.mapTag("!WASM", true);
  IO.mapRequired("FileHeader", Object.Header);
  IO.mapOptional("Sections", Object.Sections);
  IO.setContext(nullptr);
}

static void commonSectionMapping(IO &IO, WasmYAML::Section &Section) {
  IO.mapRequired("Type", Section.Type);
  IO.mapOptional("Relocations", Section.Relocations);
}

static void sectionMapping(IO &IO, WasmYAML::DylinkSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Name", Section.Name);
  IO.mapRequired("MemorySize", Section.MemorySize);
  IO.mapRequired("MemoryAlignment", Section.MemoryAlignment);
  IO.mapRequired("TableSize", Section.TableSize);
  IO.mapRequired("TableAlignment", Section.TableAlignment);
  IO.mapRequired("Needed", Section.Needed);
}

static void sectionMapping(IO &IO, WasmYAML::NameSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Name", Section.Name);
  IO.mapOptional("FunctionNames", Section.FunctionNames);
}

static void sectionMapping(IO &IO, WasmYAML::LinkingSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Name", Section.Name);
  IO.mapRequired("Version", Section.Version);
  IO.mapOptional("SymbolTable", Section.SymbolTable);
  IO.mapOptional("SegmentInfo", Section.SegmentInfos);
  IO.mapOptional("InitFunctions", Section.InitFunctions);
  IO.mapOptional("Comdats", Section.Comdats);
}

static void sectionMapping(IO &IO, WasmYAML::CustomSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Name", Section.Name);
  IO.mapRequired("Payload", Section.Payload);
}

static void sectionMapping(IO &IO, WasmYAML::TypeSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Signatures", Section.Signatures);
}

static void sectionMapping(IO &IO, WasmYAML::ImportSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Imports", Section.Imports);
}

static void sectionMapping(IO &IO, WasmYAML::FunctionSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("FunctionTypes", Section.FunctionTypes);
}

static void sectionMapping(IO &IO, WasmYAML::TableSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Tables", Section.Tables);
}

static void sectionMapping(IO &IO, WasmYAML::MemorySection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Memories", Section.Memories);
}

static void sectionMapping(IO &IO, WasmYAML::GlobalSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Globals", Section.Globals);
}

static void sectionMapping(IO &IO, WasmYAML::EventSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Events", Section.Events);
}

static void sectionMapping(IO &IO, WasmYAML::ExportSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Exports", Section.Exports);
}

static void sectionMapping(IO &IO, WasmYAML::StartSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("StartFunction", Section.StartFunction);
}

static void sectionMapping(IO &IO, WasmYAML::ElemSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Segments", Section.Segments);
}

static void sectionMapping(IO &IO, WasmYAML::CodeSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Functions", Section.Functions);
}

static void sectionMapping(IO &IO, WasmYAML::DataSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapRequired("Segments", Section.Segments);
}

void MappingTraits<std::unique_ptr<WasmYAML::Section>>::mapping(
    IO &IO, std::unique_ptr<WasmYAML::Section> &Section) {
  WasmYAML::SectionType SectionType;
  if (IO.outputting())
    SectionType = Section->Type;
  else
    IO.mapRequired("Type", SectionType);

  switch (SectionType) {
  case wasm::WASM_SEC_CUSTOM: {
    StringRef SectionName;
    if (IO.outputting()) {
      auto CustomSection = cast<WasmYAML::CustomSection>(Section.get());
      SectionName = CustomSection->Name;
    } else {
      IO.mapRequired("Name", SectionName);
    }
    if (SectionName == "dylink") {
      if (!IO.outputting())
        Section.reset(new WasmYAML::DylinkSection());
      sectionMapping(IO, *cast<WasmYAML::DylinkSection>(Section.get()));
    } else if (SectionName == "linking") {
      if (!IO.outputting())
        Section.reset(new WasmYAML::LinkingSection());
      sectionMapping(IO, *cast<WasmYAML::LinkingSection>(Section.get()));
    } else if (SectionName == "name") {
      if (!IO.outputting())
        Section.reset(new WasmYAML::NameSection());
      sectionMapping(IO, *cast<WasmYAML::NameSection>(Section.get()));
    } else {
      if (!IO.outputting())
        Section.reset(new WasmYAML::CustomSection(SectionName));
      sectionMapping(IO, *cast<WasmYAML::CustomSection>(Section.get()));
    }
    break;
  }
  case wasm::WASM_SEC_TYPE:
    if (!IO.outputting())
      Section.reset(new WasmYAML::TypeSection());
    sectionMapping(IO, *cast<WasmYAML::TypeSection>(Section.get()));
    break;
  case wasm::WASM_SEC_IMPORT:
    if (!IO.outputting())
      Section.reset(new WasmYAML::ImportSection());
    sectionMapping(IO, *cast<WasmYAML::ImportSection>(Section.get()));
    break;
  case wasm::WASM_SEC_FUNCTION:
    if (!IO.outputting())
      Section.reset(new WasmYAML::FunctionSection());
    sectionMapping(IO, *cast<WasmYAML::FunctionSection>(Section.get()));
    break;
  case wasm::WASM_SEC_TABLE:
    if (!IO.outputting())
      Section.reset(new WasmYAML::TableSection());
    sectionMapping(IO, *cast<WasmYAML::TableSection>(Section.get()));
    break;
  case wasm::WASM_SEC_MEMORY:
    if (!IO.outputting())
      Section.reset(new WasmYAML::MemorySection());
    sectionMapping(IO, *cast<WasmYAML::MemorySection>(Section.get()));
    break;
  case wasm::WASM_SEC_GLOBAL:
    if (!IO.outputting())
      Section.reset(new WasmYAML::GlobalSection());
    sectionMapping(IO, *cast<WasmYAML::GlobalSection>(Section.get()));
    break;
  case wasm::WASM_SEC_EVENT:
    if (!IO.outputting())
      Section.reset(new WasmYAML::EventSection());
    sectionMapping(IO, *cast<WasmYAML::EventSection>(Section.get()));
    break;
  case wasm::WASM_SEC_EXPORT:
    if (!IO.outputting())
      Section.reset(new WasmYAML::ExportSection());
    sectionMapping(IO, *cast<WasmYAML::ExportSection>(Section.get()));
    break;
  case wasm::WASM_SEC_START:
    if (!IO.outputting())
      Section.reset(new WasmYAML::StartSection());
    sectionMapping(IO, *cast<WasmYAML::StartSection>(Section.get()));
    break;
  case wasm::WASM_SEC_ELEM:
    if (!IO.outputting())
      Section.reset(new WasmYAML::ElemSection());
    sectionMapping(IO, *cast<WasmYAML::ElemSection>(Section.get()));
    break;
  case wasm::WASM_SEC_CODE:
    if (!IO.outputting())
      Section.reset(new WasmYAML::CodeSection());
    sectionMapping(IO, *cast<WasmYAML::CodeSection>(Section.get()));
    break;
  case wasm::WASM_SEC_DATA:
    if (!IO.outputting())
      Section.reset(new WasmYAML::DataSection());
    sectionMapping(IO, *cast<WasmYAML::DataSection>(Section.get()));
    break;
  default:
    llvm_unreachable("Unknown section type");
  }
}

void ScalarEnumerationTraits<WasmYAML::SectionType>::enumeration(
    IO &IO, WasmYAML::SectionType &Type) {
#define ECase(X) IO.enumCase(Type, #X, wasm::WASM_SEC_##X);
  ECase(CUSTOM);
  ECase(TYPE);
  ECase(IMPORT);
  ECase(FUNCTION);
  ECase(TABLE);
  ECase(MEMORY);
  ECase(GLOBAL);
  ECase(EVENT);
  ECase(EXPORT);
  ECase(START);
  ECase(ELEM);
  ECase(CODE);
  ECase(DATA);
#undef ECase
}

void MappingTraits<WasmYAML::Signature>::mapping(
    IO &IO, WasmYAML::Signature &Signature) {
  IO.mapRequired("Index", Signature.Index);
  IO.mapRequired("ReturnType", Signature.ReturnType);
  IO.mapRequired("ParamTypes", Signature.ParamTypes);
}

void MappingTraits<WasmYAML::Table>::mapping(IO &IO, WasmYAML::Table &Table) {
  IO.mapRequired("ElemType", Table.ElemType);
  IO.mapRequired("Limits", Table.TableLimits);
}

void MappingTraits<WasmYAML::Function>::mapping(IO &IO,
                                                WasmYAML::Function &Function) {
  IO.mapRequired("Index", Function.Index);
  IO.mapRequired("Locals", Function.Locals);
  IO.mapRequired("Body", Function.Body);
}

void MappingTraits<WasmYAML::Relocation>::mapping(
    IO &IO, WasmYAML::Relocation &Relocation) {
  IO.mapRequired("Type", Relocation.Type);
  IO.mapRequired("Index", Relocation.Index);
  IO.mapRequired("Offset", Relocation.Offset);
  IO.mapOptional("Addend", Relocation.Addend, 0);
}

void MappingTraits<WasmYAML::NameEntry>::mapping(
    IO &IO, WasmYAML::NameEntry &NameEntry) {
  IO.mapRequired("Index", NameEntry.Index);
  IO.mapRequired("Name", NameEntry.Name);
}

void MappingTraits<WasmYAML::SegmentInfo>::mapping(
    IO &IO, WasmYAML::SegmentInfo &SegmentInfo) {
  IO.mapRequired("Index", SegmentInfo.Index);
  IO.mapRequired("Name", SegmentInfo.Name);
  IO.mapRequired("Alignment", SegmentInfo.Alignment);
  IO.mapRequired("Flags", SegmentInfo.Flags);
}

void MappingTraits<WasmYAML::LocalDecl>::mapping(
    IO &IO, WasmYAML::LocalDecl &LocalDecl) {
  IO.mapRequired("Type", LocalDecl.Type);
  IO.mapRequired("Count", LocalDecl.Count);
}

void MappingTraits<WasmYAML::Limits>::mapping(IO &IO,
                                              WasmYAML::Limits &Limits) {
  if (!IO.outputting() || Limits.Flags)
    IO.mapOptional("Flags", Limits.Flags);
  IO.mapRequired("Initial", Limits.Initial);
  if (!IO.outputting() || Limits.Flags & wasm::WASM_LIMITS_FLAG_HAS_MAX)
    IO.mapOptional("Maximum", Limits.Maximum);
}

void MappingTraits<WasmYAML::ElemSegment>::mapping(
    IO &IO, WasmYAML::ElemSegment &Segment) {
  IO.mapRequired("Offset", Segment.Offset);
  IO.mapRequired("Functions", Segment.Functions);
}

void MappingTraits<WasmYAML::Import>::mapping(IO &IO,
                                              WasmYAML::Import &Import) {
  IO.mapRequired("Module", Import.Module);
  IO.mapRequired("Field", Import.Field);
  IO.mapRequired("Kind", Import.Kind);
  if (Import.Kind == wasm::WASM_EXTERNAL_FUNCTION) {
    IO.mapRequired("SigIndex", Import.SigIndex);
  } else if (Import.Kind == wasm::WASM_EXTERNAL_GLOBAL) {
    IO.mapRequired("GlobalType", Import.GlobalImport.Type);
    IO.mapRequired("GlobalMutable", Import.GlobalImport.Mutable);
  } else if (Import.Kind == wasm::WASM_EXTERNAL_EVENT) {
    IO.mapRequired("EventAttribute", Import.EventImport.Attribute);
    IO.mapRequired("EventSigIndex", Import.EventImport.SigIndex);
  } else if (Import.Kind == wasm::WASM_EXTERNAL_TABLE) {
    IO.mapRequired("Table", Import.TableImport);
  } else if (Import.Kind == wasm::WASM_EXTERNAL_MEMORY) {
    IO.mapRequired("Memory", Import.Memory);
  } else {
    llvm_unreachable("unhandled import type");
  }
}

void MappingTraits<WasmYAML::Export>::mapping(IO &IO,
                                              WasmYAML::Export &Export) {
  IO.mapRequired("Name", Export.Name);
  IO.mapRequired("Kind", Export.Kind);
  IO.mapRequired("Index", Export.Index);
}

void MappingTraits<WasmYAML::Global>::mapping(IO &IO,
                                              WasmYAML::Global &Global) {
  IO.mapRequired("Index", Global.Index);
  IO.mapRequired("Type", Global.Type);
  IO.mapRequired("Mutable", Global.Mutable);
  IO.mapRequired("InitExpr", Global.InitExpr);
}

void MappingTraits<wasm::WasmInitExpr>::mapping(IO &IO,
                                                wasm::WasmInitExpr &Expr) {
  WasmYAML::Opcode Op = Expr.Opcode;
  IO.mapRequired("Opcode", Op);
  Expr.Opcode = Op;
  switch (Expr.Opcode) {
  case wasm::WASM_OPCODE_I32_CONST:
    IO.mapRequired("Value", Expr.Value.Int32);
    break;
  case wasm::WASM_OPCODE_I64_CONST:
    IO.mapRequired("Value", Expr.Value.Int64);
    break;
  case wasm::WASM_OPCODE_F32_CONST:
    IO.mapRequired("Value", Expr.Value.Float32);
    break;
  case wasm::WASM_OPCODE_F64_CONST:
    IO.mapRequired("Value", Expr.Value.Float64);
    break;
  case wasm::WASM_OPCODE_GLOBAL_GET:
    IO.mapRequired("Index", Expr.Value.Global);
    break;
  }
}

void MappingTraits<WasmYAML::DataSegment>::mapping(
    IO &IO, WasmYAML::DataSegment &Segment) {
  IO.mapOptional("SectionOffset", Segment.SectionOffset);
  IO.mapRequired("MemoryIndex", Segment.MemoryIndex);
  IO.mapRequired("Offset", Segment.Offset);
  IO.mapRequired("Content", Segment.Content);
}

void MappingTraits<WasmYAML::InitFunction>::mapping(
    IO &IO, WasmYAML::InitFunction &Init) {
  IO.mapRequired("Priority", Init.Priority);
  IO.mapRequired("Symbol", Init.Symbol);
}

void ScalarEnumerationTraits<WasmYAML::ComdatKind>::enumeration(
    IO &IO, WasmYAML::ComdatKind &Kind) {
#define ECase(X) IO.enumCase(Kind, #X, wasm::WASM_COMDAT_##X);
  ECase(FUNCTION);
  ECase(DATA);
#undef ECase
}

void MappingTraits<WasmYAML::ComdatEntry>::mapping(
    IO &IO, WasmYAML::ComdatEntry &ComdatEntry) {
  IO.mapRequired("Kind", ComdatEntry.Kind);
  IO.mapRequired("Index", ComdatEntry.Index);
}

void MappingTraits<WasmYAML::Comdat>::mapping(IO &IO,
                                              WasmYAML::Comdat &Comdat) {
  IO.mapRequired("Name", Comdat.Name);
  IO.mapRequired("Entries", Comdat.Entries);
}

void MappingTraits<WasmYAML::SymbolInfo>::mapping(IO &IO,
                                                  WasmYAML::SymbolInfo &Info) {
  IO.mapRequired("Index", Info.Index);
  IO.mapRequired("Kind", Info.Kind);
  IO.mapRequired("Name", Info.Name);
  IO.mapRequired("Flags", Info.Flags);
  if (Info.Kind == wasm::WASM_SYMBOL_TYPE_FUNCTION) {
    IO.mapRequired("Function", Info.ElementIndex);
  } else if (Info.Kind == wasm::WASM_SYMBOL_TYPE_GLOBAL) {
    IO.mapRequired("Global", Info.ElementIndex);
  } else if (Info.Kind == wasm::WASM_SYMBOL_TYPE_EVENT) {
    IO.mapRequired("Event", Info.ElementIndex);
  } else if (Info.Kind == wasm::WASM_SYMBOL_TYPE_DATA) {
    if ((Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) == 0) {
      IO.mapRequired("Segment", Info.DataRef.Segment);
      IO.mapOptional("Offset", Info.DataRef.Offset, 0u);
      IO.mapRequired("Size", Info.DataRef.Size);
    }
  } else if (Info.Kind == wasm::WASM_SYMBOL_TYPE_SECTION) {
    IO.mapRequired("Section", Info.ElementIndex);
  } else {
    llvm_unreachable("unsupported symbol kind");
  }
}

void MappingTraits<WasmYAML::Event>::mapping(IO &IO, WasmYAML::Event &Event) {
  IO.mapRequired("Index", Event.Index);
  IO.mapRequired("Attribute", Event.Attribute);
  IO.mapRequired("SigIndex", Event.SigIndex);
}

void ScalarBitSetTraits<WasmYAML::LimitFlags>::bitset(
    IO &IO, WasmYAML::LimitFlags &Value) {
#define BCase(X) IO.bitSetCase(Value, #X, wasm::WASM_LIMITS_FLAG_##X)
  BCase(HAS_MAX);
  BCase(IS_SHARED);
#undef BCase
}

void ScalarBitSetTraits<WasmYAML::SegmentFlags>::bitset(
    IO &IO, WasmYAML::SegmentFlags &Value) {}

void ScalarBitSetTraits<WasmYAML::SymbolFlags>::bitset(
    IO &IO, WasmYAML::SymbolFlags &Value) {
#define BCaseMask(M, X)                                                        \
  IO.maskedBitSetCase(Value, #X, wasm::WASM_SYMBOL_##X, wasm::WASM_SYMBOL_##M)
  // BCaseMask(BINDING_MASK, BINDING_GLOBAL);
  BCaseMask(BINDING_MASK, BINDING_WEAK);
  BCaseMask(BINDING_MASK, BINDING_LOCAL);
  // BCaseMask(VISIBILITY_MASK, VISIBILITY_DEFAULT);
  BCaseMask(VISIBILITY_MASK, VISIBILITY_HIDDEN);
  BCaseMask(UNDEFINED, UNDEFINED);
#undef BCaseMask
}

void ScalarEnumerationTraits<WasmYAML::SymbolKind>::enumeration(
    IO &IO, WasmYAML::SymbolKind &Kind) {
#define ECase(X) IO.enumCase(Kind, #X, wasm::WASM_SYMBOL_TYPE_##X);
  ECase(FUNCTION);
  ECase(DATA);
  ECase(GLOBAL);
  ECase(SECTION);
  ECase(EVENT);
#undef ECase
}

void ScalarEnumerationTraits<WasmYAML::ValueType>::enumeration(
    IO &IO, WasmYAML::ValueType &Type) {
#define ECase(X) IO.enumCase(Type, #X, wasm::WASM_TYPE_##X);
  ECase(I32);
  ECase(I64);
  ECase(F32);
  ECase(F64);
  ECase(V128);
  ECase(FUNCREF);
  ECase(FUNC);
  ECase(NORESULT);
#undef ECase
}

void ScalarEnumerationTraits<WasmYAML::ExportKind>::enumeration(
    IO &IO, WasmYAML::ExportKind &Kind) {
#define ECase(X) IO.enumCase(Kind, #X, wasm::WASM_EXTERNAL_##X);
  ECase(FUNCTION);
  ECase(TABLE);
  ECase(MEMORY);
  ECase(GLOBAL);
  ECase(EVENT);
#undef ECase
}

void ScalarEnumerationTraits<WasmYAML::Opcode>::enumeration(
    IO &IO, WasmYAML::Opcode &Code) {
#define ECase(X) IO.enumCase(Code, #X, wasm::WASM_OPCODE_##X);
  ECase(END);
  ECase(I32_CONST);
  ECase(I64_CONST);
  ECase(F64_CONST);
  ECase(F32_CONST);
  ECase(GLOBAL_GET);
#undef ECase
}

void ScalarEnumerationTraits<WasmYAML::TableType>::enumeration(
    IO &IO, WasmYAML::TableType &Type) {
#define ECase(X) IO.enumCase(Type, #X, wasm::WASM_TYPE_##X);
  ECase(FUNCREF);
#undef ECase
}

void ScalarEnumerationTraits<WasmYAML::RelocType>::enumeration(
    IO &IO, WasmYAML::RelocType &Type) {
#define WASM_RELOC(name, value) IO.enumCase(Type, #name, wasm::name);
#include "llvm/BinaryFormat/WasmRelocs.def"
#undef WASM_RELOC
}

} // end namespace yaml

} // end namespace llvm
