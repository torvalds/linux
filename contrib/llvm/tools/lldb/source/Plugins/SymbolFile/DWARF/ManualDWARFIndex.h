//===-- ManulaDWARFIndex.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_MANUALDWARFINDEX_H
#define LLDB_MANUALDWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/NameToDIE.h"
#include "llvm/ADT/DenseSet.h"

namespace lldb_private {
class ManualDWARFIndex : public DWARFIndex {
public:
  ManualDWARFIndex(Module &module, DWARFDebugInfo *debug_info,
                   llvm::DenseSet<dw_offset_t> units_to_avoid = {})
      : DWARFIndex(module), m_debug_info(debug_info),
        m_units_to_avoid(std::move(units_to_avoid)) {}

  void Preload() override { Index(); }

  void GetGlobalVariables(ConstString basename, DIEArray &offsets) override;
  void GetGlobalVariables(const RegularExpression &regex,
                          DIEArray &offsets) override;
  void GetGlobalVariables(const DWARFUnit &cu, DIEArray &offsets) override;
  void GetObjCMethods(ConstString class_name, DIEArray &offsets) override;
  void GetCompleteObjCClass(ConstString class_name, bool must_be_implementation,
                            DIEArray &offsets) override;
  void GetTypes(ConstString name, DIEArray &offsets) override;
  void GetTypes(const DWARFDeclContext &context, DIEArray &offsets) override;
  void GetNamespaces(ConstString name, DIEArray &offsets) override;
  void GetFunctions(ConstString name, DWARFDebugInfo &info,
                    const CompilerDeclContext &parent_decl_ctx,
                    uint32_t name_type_mask,
                    std::vector<DWARFDIE> &dies) override;
  void GetFunctions(const RegularExpression &regex, DIEArray &offsets) override;

  void ReportInvalidDIEOffset(dw_offset_t offset,
                              llvm::StringRef name) override {}
  void Dump(Stream &s) override;

private:
  struct IndexSet {
    NameToDIE function_basenames;
    NameToDIE function_fullnames;
    NameToDIE function_methods;
    NameToDIE function_selectors;
    NameToDIE objc_class_selectors;
    NameToDIE globals;
    NameToDIE types;
    NameToDIE namespaces;
  };
  void Index();
  void IndexUnit(DWARFUnit &unit, IndexSet &set);

  static void
  IndexUnitImpl(DWARFUnit &unit, const lldb::LanguageType cu_language,
                const DWARFFormValue::FixedFormSizes &fixed_form_sizes,
                const dw_offset_t cu_offset, IndexSet &set);

  /// Non-null value means we haven't built the index yet.
  DWARFDebugInfo *m_debug_info;
  /// Which dwarf units should we skip while building the index.
  llvm::DenseSet<dw_offset_t> m_units_to_avoid;

  IndexSet m_set;
};
} // namespace lldb_private

#endif // LLDB_MANUALDWARFINDEX_H
