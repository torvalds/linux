//===-- AppleDWARFIndex.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/AppleDWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/DWARFDebugInfo.h"
#include "Plugins/SymbolFile/DWARF/DWARFDeclContext.h"
#include "Plugins/SymbolFile/DWARF/DWARFUnit.h"
#include "Plugins/SymbolFile/DWARF/LogChannelDWARF.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"

using namespace lldb_private;
using namespace lldb;

std::unique_ptr<AppleDWARFIndex> AppleDWARFIndex::Create(
    Module &module, DWARFDataExtractor apple_names,
    DWARFDataExtractor apple_namespaces, DWARFDataExtractor apple_types,
    DWARFDataExtractor apple_objc, DWARFDataExtractor debug_str) {
  auto apple_names_table_up = llvm::make_unique<DWARFMappedHash::MemoryTable>(
      apple_names, debug_str, ".apple_names");
  if (!apple_names_table_up->IsValid())
    apple_names_table_up.reset();

  auto apple_namespaces_table_up =
      llvm::make_unique<DWARFMappedHash::MemoryTable>(
          apple_namespaces, debug_str, ".apple_namespaces");
  if (!apple_namespaces_table_up->IsValid())
    apple_namespaces_table_up.reset();

  auto apple_types_table_up = llvm::make_unique<DWARFMappedHash::MemoryTable>(
      apple_types, debug_str, ".apple_types");
  if (!apple_types_table_up->IsValid())
    apple_types_table_up.reset();

  auto apple_objc_table_up = llvm::make_unique<DWARFMappedHash::MemoryTable>(
      apple_objc, debug_str, ".apple_objc");
  if (!apple_objc_table_up->IsValid())
    apple_objc_table_up.reset();

  if (apple_names_table_up || apple_names_table_up || apple_types_table_up ||
      apple_objc_table_up)
    return llvm::make_unique<AppleDWARFIndex>(
        module, std::move(apple_names_table_up),
        std::move(apple_namespaces_table_up), std::move(apple_types_table_up),
        std::move(apple_objc_table_up));

  return nullptr;
}

void AppleDWARFIndex::GetGlobalVariables(ConstString basename, DIEArray &offsets) {
  if (m_apple_names_up)
    m_apple_names_up->FindByName(basename.GetStringRef(), offsets);
}

void AppleDWARFIndex::GetGlobalVariables(const RegularExpression &regex,
                                         DIEArray &offsets) {
  if (!m_apple_names_up)
    return;

  DWARFMappedHash::DIEInfoArray hash_data;
  if (m_apple_names_up->AppendAllDIEsThatMatchingRegex(regex, hash_data))
    DWARFMappedHash::ExtractDIEArray(hash_data, offsets);
}

void AppleDWARFIndex::GetGlobalVariables(const DWARFUnit &cu,
                                         DIEArray &offsets) {
  if (!m_apple_names_up)
    return;

  DWARFMappedHash::DIEInfoArray hash_data;
  if (m_apple_names_up->AppendAllDIEsInRange(
          cu.GetOffset(), cu.GetNextCompileUnitOffset(), hash_data))
    DWARFMappedHash::ExtractDIEArray(hash_data, offsets);
}

void AppleDWARFIndex::GetObjCMethods(ConstString class_name,
                                     DIEArray &offsets) {
  if (m_apple_objc_up)
    m_apple_objc_up->FindByName(class_name.GetStringRef(), offsets);
}

void AppleDWARFIndex::GetCompleteObjCClass(ConstString class_name,
                                           bool must_be_implementation,
                                           DIEArray &offsets) {
  if (m_apple_types_up) {
    m_apple_types_up->FindCompleteObjCClassByName(
        class_name.GetStringRef(), offsets, must_be_implementation);
  }
}

void AppleDWARFIndex::GetTypes(ConstString name, DIEArray &offsets) {
  if (m_apple_types_up)
    m_apple_types_up->FindByName(name.GetStringRef(), offsets);
}

void AppleDWARFIndex::GetTypes(const DWARFDeclContext &context,
                               DIEArray &offsets) {
  if (!m_apple_types_up)
    return;

  Log *log = LogChannelDWARF::GetLogIfAny(DWARF_LOG_TYPE_COMPLETION |
                                          DWARF_LOG_LOOKUPS);
  const bool has_tag = m_apple_types_up->GetHeader().header_data.ContainsAtom(
      DWARFMappedHash::eAtomTypeTag);
  const bool has_qualified_name_hash =
      m_apple_types_up->GetHeader().header_data.ContainsAtom(
          DWARFMappedHash::eAtomTypeQualNameHash);
  const ConstString type_name(context[0].name);
  const dw_tag_t tag = context[0].tag;
  if (has_tag && has_qualified_name_hash) {
    const char *qualified_name = context.GetQualifiedName();
    const uint32_t qualified_name_hash = llvm::djbHash(qualified_name);
    if (log)
      m_module.LogMessage(log, "FindByNameAndTagAndQualifiedNameHash()");
    m_apple_types_up->FindByNameAndTagAndQualifiedNameHash(
        type_name.GetStringRef(), tag, qualified_name_hash, offsets);
  } else if (has_tag) {
    if (log)
      m_module.LogMessage(log, "FindByNameAndTag()");
    m_apple_types_up->FindByNameAndTag(type_name.GetStringRef(), tag, offsets);
  } else
    m_apple_types_up->FindByName(type_name.GetStringRef(), offsets);
}

void AppleDWARFIndex::GetNamespaces(ConstString name, DIEArray &offsets) {
  if (m_apple_namespaces_up)
    m_apple_namespaces_up->FindByName(name.GetStringRef(), offsets);
}

void AppleDWARFIndex::GetFunctions(ConstString name, DWARFDebugInfo &info,
                                   const CompilerDeclContext &parent_decl_ctx,
                                   uint32_t name_type_mask,
                                   std::vector<DWARFDIE> &dies) {
  DIEArray offsets;
  m_apple_names_up->FindByName(name.GetStringRef(), offsets);
  for (const DIERef &die_ref : offsets) {
    ProcessFunctionDIE(name.GetStringRef(), die_ref, info, parent_decl_ctx,
                       name_type_mask, dies);
  }
}

void AppleDWARFIndex::GetFunctions(const RegularExpression &regex,
                                   DIEArray &offsets) {
  if (!m_apple_names_up)
    return;

  DWARFMappedHash::DIEInfoArray hash_data;
  if (m_apple_names_up->AppendAllDIEsThatMatchingRegex(regex, hash_data))
    DWARFMappedHash::ExtractDIEArray(hash_data, offsets);
}

void AppleDWARFIndex::ReportInvalidDIEOffset(dw_offset_t offset,
                                             llvm::StringRef name) {
  m_module.ReportErrorIfModifyDetected(
      "the DWARF debug information has been modified (accelerator table had "
      "bad die 0x%8.8x for '%s')\n",
      offset, name.str().c_str());
}

void AppleDWARFIndex::Dump(Stream &s) {
  if (m_apple_names_up)
    s.PutCString(".apple_names index present\n");
  if (m_apple_namespaces_up)
    s.PutCString(".apple_namespaces index present\n");
  if (m_apple_types_up)
    s.PutCString(".apple_types index present\n");
  if (m_apple_objc_up)
    s.PutCString(".apple_objc index present\n");
  // TODO: Dump index contents
}
