//===-- SymbolFileDWARFDwo.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARFDwo_SymbolFileDWARFDwo_h_
#define SymbolFileDWARFDwo_SymbolFileDWARFDwo_h_

#include "SymbolFileDWARF.h"

class SymbolFileDWARFDwo : public SymbolFileDWARF {
public:
  SymbolFileDWARFDwo(lldb::ObjectFileSP objfile, DWARFUnit *dwarf_cu);

  ~SymbolFileDWARFDwo() override = default;

  lldb::CompUnitSP ParseCompileUnit(DWARFUnit *dwarf_cu,
                                    uint32_t cu_idx) override;

  DWARFUnit *GetCompileUnit();

  DWARFUnit *
  GetDWARFCompileUnit(lldb_private::CompileUnit *comp_unit) override;

  lldb_private::DWARFExpression::LocationListFormat
  GetLocationListFormat() const override;

  size_t GetObjCMethodDIEOffsets(lldb_private::ConstString class_name,
                                 DIEArray &method_die_offsets) override;

  lldb_private::TypeSystem *
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  DWARFDIE
  GetDIE(const DIERef &die_ref) override;

  std::unique_ptr<SymbolFileDWARFDwo>
  GetDwoSymbolFileForCompileUnit(DWARFUnit &dwarf_cu,
                                 const DWARFDebugInfoEntry &cu_die) override {
    return nullptr;
  }

  DWARFUnit *GetBaseCompileUnit() override;

  const lldb_private::DWARFDataExtractor &get_debug_abbrev_data() override;
  const lldb_private::DWARFDataExtractor &get_debug_addr_data() override;
  const lldb_private::DWARFDataExtractor &get_debug_info_data() override;
  const lldb_private::DWARFDataExtractor &get_debug_str_data() override;
  const lldb_private::DWARFDataExtractor &get_debug_str_offsets_data() override;

protected:
  void LoadSectionData(lldb::SectionType sect_type,
                       lldb_private::DWARFDataExtractor &data) override;

  DIEToTypePtr &GetDIEToType() override;

  DIEToVariableSP &GetDIEToVariable() override;

  DIEToClangType &GetForwardDeclDieToClangType() override;

  ClangTypeToDIE &GetForwardDeclClangTypeToDie() override;

  UniqueDWARFASTTypeMap &GetUniqueDWARFASTTypeMap() override;

  lldb::TypeSP FindDefinitionTypeForDWARFDeclContext(
      const DWARFDeclContext &die_decl_ctx) override;

  lldb::TypeSP FindCompleteObjCDefinitionTypeForDIE(
      const DWARFDIE &die, const lldb_private::ConstString &type_name,
      bool must_be_implementation) override;

  SymbolFileDWARF *GetBaseSymbolFile();

  lldb::ObjectFileSP m_obj_file_sp;
  DWARFUnit *m_base_dwarf_cu;
};

#endif // SymbolFileDWARFDwo_SymbolFileDWARFDwo_h_
