//===-- SymbolFileDWARFDwp.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARFDwp.h"

#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"

#include "SymbolFileDWARFDwoDwp.h"

static llvm::DWARFSectionKind
lldbSectTypeToLlvmSectionKind(lldb::SectionType type) {
  switch (type) {
  case lldb::eSectionTypeDWARFDebugInfo:
    return llvm::DW_SECT_INFO;
  // case lldb::eSectionTypeDWARFDebugTypes:
  //   return llvm::DW_SECT_TYPES;
  case lldb::eSectionTypeDWARFDebugAbbrev:
    return llvm::DW_SECT_ABBREV;
  case lldb::eSectionTypeDWARFDebugLine:
    return llvm::DW_SECT_LINE;
  case lldb::eSectionTypeDWARFDebugLoc:
    return llvm::DW_SECT_LOC; 
  case lldb::eSectionTypeDWARFDebugStrOffsets:
    return llvm::DW_SECT_STR_OFFSETS;
  // case lldb::eSectionTypeDWARFDebugMacinfo:
  //   return llvm::DW_SECT_MACINFO;
  case lldb::eSectionTypeDWARFDebugMacro:
    return llvm::DW_SECT_MACRO;
  default:
    // Note: 0 is an invalid dwarf section kind.
    return llvm::DWARFSectionKind(0);
  }
}

std::unique_ptr<SymbolFileDWARFDwp>
SymbolFileDWARFDwp::Create(lldb::ModuleSP module_sp,
                           const lldb_private::FileSpec &file_spec) {
  const lldb::offset_t file_offset = 0;
  lldb::DataBufferSP file_data_sp;
  lldb::offset_t file_data_offset = 0;
  lldb::ObjectFileSP obj_file = lldb_private::ObjectFile::FindPlugin(
      module_sp, &file_spec, file_offset,
      lldb_private::FileSystem::Instance().GetByteSize(file_spec), file_data_sp,
      file_data_offset);
  if (obj_file == nullptr)
    return nullptr;

  std::unique_ptr<SymbolFileDWARFDwp> dwp_symfile(
      new SymbolFileDWARFDwp(module_sp, obj_file));

  lldb_private::DWARFDataExtractor debug_cu_index;
  if (!dwp_symfile->LoadRawSectionData(lldb::eSectionTypeDWARFDebugCuIndex,
                                       debug_cu_index))
    return nullptr;

  llvm::DataExtractor llvm_debug_cu_index(
      llvm::StringRef(debug_cu_index.PeekCStr(0), debug_cu_index.GetByteSize()),
      debug_cu_index.GetByteOrder() == lldb::eByteOrderLittle,
      debug_cu_index.GetAddressByteSize());
  if (!dwp_symfile->m_debug_cu_index.parse(llvm_debug_cu_index))
    return nullptr;
  dwp_symfile->InitDebugCUIndexMap();
  return dwp_symfile;
}

void SymbolFileDWARFDwp::InitDebugCUIndexMap() {
  m_debug_cu_index_map.clear();
  for (const auto &entry : m_debug_cu_index.getRows())
    m_debug_cu_index_map.emplace(entry.getSignature(), &entry);
}

SymbolFileDWARFDwp::SymbolFileDWARFDwp(lldb::ModuleSP module_sp,
                                       lldb::ObjectFileSP obj_file)
    : m_obj_file(std::move(obj_file)), m_debug_cu_index(llvm::DW_SECT_INFO) 
{}

std::unique_ptr<SymbolFileDWARFDwo>
SymbolFileDWARFDwp::GetSymbolFileForDwoId(DWARFUnit *dwarf_cu,
                                          uint64_t dwo_id) {
  return std::unique_ptr<SymbolFileDWARFDwo>(
      new SymbolFileDWARFDwoDwp(this, m_obj_file, dwarf_cu, dwo_id));
}

bool SymbolFileDWARFDwp::LoadSectionData(
    uint64_t dwo_id, lldb::SectionType sect_type,
    lldb_private::DWARFDataExtractor &data) {
  lldb_private::DWARFDataExtractor section_data;
  if (!LoadRawSectionData(sect_type, section_data))
    return false;

  auto it = m_debug_cu_index_map.find(dwo_id);
  if (it == m_debug_cu_index_map.end())
    return false;

  auto *offsets =
      it->second->getOffset(lldbSectTypeToLlvmSectionKind(sect_type));
  if (offsets) {
    data.SetData(section_data, offsets->Offset, offsets->Length);
  } else {
    data.SetData(section_data, 0, section_data.GetByteSize());
  }
  return true;
}

bool SymbolFileDWARFDwp::LoadRawSectionData(
    lldb::SectionType sect_type, lldb_private::DWARFDataExtractor &data) {
  std::lock_guard<std::mutex> lock(m_sections_mutex);

  auto it = m_sections.find(sect_type);
  if (it != m_sections.end()) {
    if (it->second.GetByteSize() == 0)
      return false;

    data = it->second;
    return true;
  }

  const lldb_private::SectionList *section_list =
      m_obj_file->GetSectionList(false /* update_module_section_list */);
  if (section_list) {
    lldb::SectionSP section_sp(
        section_list->FindSectionByType(sect_type, true));
    if (section_sp) {
      if (m_obj_file->ReadSectionData(section_sp.get(), data) != 0) {
        m_sections[sect_type] = data;
        return true;
      }
    }
  }
  m_sections[sect_type].Clear();
  return false;
}
