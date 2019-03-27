//===-- AppleDWARFIndex.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_APPLEDWARFINDEX_H
#define LLDB_APPLEDWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/HashedNameToDIE.h"

namespace lldb_private {
class AppleDWARFIndex : public DWARFIndex {
public:
  static std::unique_ptr<AppleDWARFIndex>
  Create(Module &module, DWARFDataExtractor apple_names,
         DWARFDataExtractor apple_namespaces, DWARFDataExtractor apple_types,
         DWARFDataExtractor apple_objc, DWARFDataExtractor debug_str);

  AppleDWARFIndex(
      Module &module, std::unique_ptr<DWARFMappedHash::MemoryTable> apple_names,
      std::unique_ptr<DWARFMappedHash::MemoryTable> apple_namespaces,
      std::unique_ptr<DWARFMappedHash::MemoryTable> apple_types,
      std::unique_ptr<DWARFMappedHash::MemoryTable> apple_objc)
      : DWARFIndex(module), m_apple_names_up(std::move(apple_names)),
        m_apple_namespaces_up(std::move(apple_namespaces)),
        m_apple_types_up(std::move(apple_types)),
        m_apple_objc_up(std::move(apple_objc)) {}

  void Preload() override {}

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
                              llvm::StringRef name) override;
  void Dump(Stream &s) override;

private:
  std::unique_ptr<DWARFMappedHash::MemoryTable> m_apple_names_up;
  std::unique_ptr<DWARFMappedHash::MemoryTable> m_apple_namespaces_up;
  std::unique_ptr<DWARFMappedHash::MemoryTable> m_apple_types_up;
  std::unique_ptr<DWARFMappedHash::MemoryTable> m_apple_objc_up;
};
} // namespace lldb_private

#endif // LLDB_APPLEDWARFINDEX_H
