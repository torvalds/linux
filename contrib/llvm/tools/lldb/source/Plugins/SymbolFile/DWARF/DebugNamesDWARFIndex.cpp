//===-- DebugNamesDWARFIndex.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/DebugNamesDWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/DWARFDebugInfo.h"
#include "Plugins/SymbolFile/DWARF/DWARFDeclContext.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARFDwo.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace lldb;

static llvm::DWARFDataExtractor ToLLVM(const DWARFDataExtractor &data) {
  return llvm::DWARFDataExtractor(
      llvm::StringRef(reinterpret_cast<const char *>(data.GetDataStart()),
                      data.GetByteSize()),
      data.GetByteOrder() == eByteOrderLittle, data.GetAddressByteSize());
}

llvm::Expected<std::unique_ptr<DebugNamesDWARFIndex>>
DebugNamesDWARFIndex::Create(Module &module, DWARFDataExtractor debug_names,
                             DWARFDataExtractor debug_str,
                             DWARFDebugInfo *debug_info) {
  if (!debug_info) {
    return llvm::make_error<llvm::StringError>("debug info null",
                                               llvm::inconvertibleErrorCode());
  }
  auto index_up =
      llvm::make_unique<DebugNames>(ToLLVM(debug_names), ToLLVM(debug_str));
  if (llvm::Error E = index_up->extract())
    return std::move(E);

  return std::unique_ptr<DebugNamesDWARFIndex>(new DebugNamesDWARFIndex(
      module, std::move(index_up), debug_names, debug_str, *debug_info));
}

llvm::DenseSet<dw_offset_t>
DebugNamesDWARFIndex::GetUnits(const DebugNames &debug_names) {
  llvm::DenseSet<dw_offset_t> result;
  for (const DebugNames::NameIndex &ni : debug_names) {
    for (uint32_t cu = 0; cu < ni.getCUCount(); ++cu)
      result.insert(ni.getCUOffset(cu));
  }
  return result;
}

DIERef DebugNamesDWARFIndex::ToDIERef(const DebugNames::Entry &entry) {
  llvm::Optional<uint64_t> cu_offset = entry.getCUOffset();
  if (!cu_offset)
    return DIERef();

  DWARFUnit *cu = m_debug_info.GetCompileUnit(*cu_offset);
  if (!cu)
    return DIERef();

  // This initializes the DWO symbol file. It's not possible for
  // GetDwoSymbolFile to call this automatically because of mutual recursion
  // between this and DWARFDebugInfoEntry::GetAttributeValue.
  cu->ExtractUnitDIEIfNeeded();
  uint64_t die_bias = cu->GetDwoSymbolFile() ? 0 : *cu_offset;

  if (llvm::Optional<uint64_t> die_offset = entry.getDIEUnitOffset())
    return DIERef(*cu_offset, die_bias + *die_offset);

  return DIERef();
}

void DebugNamesDWARFIndex::Append(const DebugNames::Entry &entry,
                                  DIEArray &offsets) {
  if (DIERef ref = ToDIERef(entry))
    offsets.push_back(ref);
}

void DebugNamesDWARFIndex::MaybeLogLookupError(llvm::Error error,
                                               const DebugNames::NameIndex &ni,
                                               llvm::StringRef name) {
  // Ignore SentinelErrors, log everything else.
  LLDB_LOG_ERROR(
      LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS),
      handleErrors(std::move(error), [](const DebugNames::SentinelError &) {}),
      "Failed to parse index entries for index at {1:x}, name {2}: {0}",
      ni.getUnitOffset(), name);
}

void DebugNamesDWARFIndex::GetGlobalVariables(ConstString basename,
                                              DIEArray &offsets) {
  m_fallback.GetGlobalVariables(basename, offsets);

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(basename.GetStringRef())) {
    if (entry.tag() != DW_TAG_variable)
      continue;

    Append(entry, offsets);
  }
}

void DebugNamesDWARFIndex::GetGlobalVariables(const RegularExpression &regex,
                                              DIEArray &offsets) {
  m_fallback.GetGlobalVariables(regex, offsets);

  for (const DebugNames::NameIndex &ni: *m_debug_names_up) {
    for (DebugNames::NameTableEntry nte: ni) {
      if (!regex.Execute(nte.getString()))
        continue;

      uint32_t entry_offset = nte.getEntryOffset();
      llvm::Expected<DebugNames::Entry> entry_or = ni.getEntry(&entry_offset);
      for (; entry_or; entry_or = ni.getEntry(&entry_offset)) {
        if (entry_or->tag() != DW_TAG_variable)
          continue;

        Append(*entry_or, offsets);
      }
      MaybeLogLookupError(entry_or.takeError(), ni, nte.getString());
    }
  }
}

void DebugNamesDWARFIndex::GetGlobalVariables(const DWARFUnit &cu,
                                              DIEArray &offsets) {
  m_fallback.GetGlobalVariables(cu, offsets);

  uint64_t cu_offset = cu.GetOffset();
  for (const DebugNames::NameIndex &ni: *m_debug_names_up) {
    for (DebugNames::NameTableEntry nte: ni) {
      uint32_t entry_offset = nte.getEntryOffset();
      llvm::Expected<DebugNames::Entry> entry_or = ni.getEntry(&entry_offset);
      for (; entry_or; entry_or = ni.getEntry(&entry_offset)) {
        if (entry_or->tag() != DW_TAG_variable)
          continue;
        if (entry_or->getCUOffset() != cu_offset)
          continue;

        Append(*entry_or, offsets);
      }
      MaybeLogLookupError(entry_or.takeError(), ni, nte.getString());
    }
  }
}

void DebugNamesDWARFIndex::GetCompleteObjCClass(ConstString class_name,
                                                bool must_be_implementation,
                                                DIEArray &offsets) {
  m_fallback.GetCompleteObjCClass(class_name, must_be_implementation, offsets);

  // Keep a list of incomplete types as fallback for when we don't find the
  // complete type.
  DIEArray incomplete_types;

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(class_name.GetStringRef())) {
    if (entry.tag() != DW_TAG_structure_type &&
        entry.tag() != DW_TAG_class_type)
      continue;

    DIERef ref = ToDIERef(entry);
    if (!ref)
      continue;

    DWARFUnit *cu = m_debug_info.GetCompileUnit(ref.cu_offset);
    if (!cu || !cu->Supports_DW_AT_APPLE_objc_complete_type()) {
      incomplete_types.push_back(ref);
      continue;
    }

    // FIXME: We should return DWARFDIEs so we don't have to resolve it twice.
    DWARFDIE die = m_debug_info.GetDIE(ref);
    if (!die)
      continue;

    if (die.GetAttributeValueAsUnsigned(DW_AT_APPLE_objc_complete_type, 0)) {
      // If we find the complete version we're done.
      offsets.push_back(ref);
      return;
    } else {
      incomplete_types.push_back(ref);
    }
  }

  offsets.insert(offsets.end(), incomplete_types.begin(),
                 incomplete_types.end());
}

void DebugNamesDWARFIndex::GetTypes(ConstString name, DIEArray &offsets) {
  m_fallback.GetTypes(name, offsets);

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(name.GetStringRef())) {
    if (isType(entry.tag()))
      Append(entry, offsets);
  }
}

void DebugNamesDWARFIndex::GetTypes(const DWARFDeclContext &context,
                                    DIEArray &offsets) {
  m_fallback.GetTypes(context, offsets);

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(context[0].name)) {
    if (entry.tag() == context[0].tag)
      Append(entry, offsets);
  }
}

void DebugNamesDWARFIndex::GetNamespaces(ConstString name, DIEArray &offsets) {
  m_fallback.GetNamespaces(name, offsets);

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(name.GetStringRef())) {
    if (entry.tag() == DW_TAG_namespace)
      Append(entry, offsets);
  }
}

void DebugNamesDWARFIndex::GetFunctions(
    ConstString name, DWARFDebugInfo &info,
    const CompilerDeclContext &parent_decl_ctx, uint32_t name_type_mask,
    std::vector<DWARFDIE> &dies) {

  std::vector<DWARFDIE> v;
  m_fallback.GetFunctions(name, info, parent_decl_ctx, name_type_mask, v);

  for (const DebugNames::Entry &entry :
       m_debug_names_up->equal_range(name.GetStringRef())) {
    Tag tag = entry.tag();
    if (tag != DW_TAG_subprogram && tag != DW_TAG_inlined_subroutine)
      continue;

    if (DIERef ref = ToDIERef(entry))
      ProcessFunctionDIE(name.GetStringRef(), ref, info, parent_decl_ctx,
                         name_type_mask, v);
  }

  std::set<DWARFDebugInfoEntry *> seen;
  for (DWARFDIE die : v)
    if (seen.insert(die.GetDIE()).second)
      dies.push_back(die);
}

void DebugNamesDWARFIndex::GetFunctions(const RegularExpression &regex,
                                        DIEArray &offsets) {
  m_fallback.GetFunctions(regex, offsets);

  for (const DebugNames::NameIndex &ni: *m_debug_names_up) {
    for (DebugNames::NameTableEntry nte: ni) {
      if (!regex.Execute(nte.getString()))
        continue;

      uint32_t entry_offset = nte.getEntryOffset();
      llvm::Expected<DebugNames::Entry> entry_or = ni.getEntry(&entry_offset);
      for (; entry_or; entry_or = ni.getEntry(&entry_offset)) {
        Tag tag = entry_or->tag();
        if (tag != DW_TAG_subprogram && tag != DW_TAG_inlined_subroutine)
          continue;

        Append(*entry_or, offsets);
      }
      MaybeLogLookupError(entry_or.takeError(), ni, nte.getString());
    }
  }
}

void DebugNamesDWARFIndex::Dump(Stream &s) {
  m_fallback.Dump(s);

  std::string data;
  llvm::raw_string_ostream os(data);
  m_debug_names_up->dump(os);
  s.PutCString(os.str());
}
