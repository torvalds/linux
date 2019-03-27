//===-- SymbolFileDWARFDwp.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARFDwp_SymbolFileDWARFDwp_h_
#define SymbolFileDWARFDwp_SymbolFileDWARFDwp_h_

#include <memory>

#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"

#include "lldb/Core/Module.h"

#include "DWARFDataExtractor.h"
#include "SymbolFileDWARFDwo.h"

class SymbolFileDWARFDwp {
public:
  static std::unique_ptr<SymbolFileDWARFDwp>
  Create(lldb::ModuleSP module_sp, const lldb_private::FileSpec &file_spec);

  std::unique_ptr<SymbolFileDWARFDwo>
  GetSymbolFileForDwoId(DWARFUnit *dwarf_cu, uint64_t dwo_id);

  bool LoadSectionData(uint64_t dwo_id, lldb::SectionType sect_type,
                       lldb_private::DWARFDataExtractor &data);

private:
  explicit SymbolFileDWARFDwp(lldb::ModuleSP module_sp,
                              lldb::ObjectFileSP obj_file);

  bool LoadRawSectionData(lldb::SectionType sect_type,
                          lldb_private::DWARFDataExtractor &data);
  
  void InitDebugCUIndexMap();

  lldb::ObjectFileSP m_obj_file;

  std::mutex m_sections_mutex;
  std::map<lldb::SectionType, lldb_private::DWARFDataExtractor> m_sections;

  llvm::DWARFUnitIndex m_debug_cu_index;
  std::map<uint64_t, const llvm::DWARFUnitIndex::Entry *> m_debug_cu_index_map;
};

#endif // SymbolFileDWARFDwp_SymbolFileDWARFDwp_h_
