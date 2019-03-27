//===-- DWARFIndex.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DWARFINDEX_H
#define LLDB_DWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DIERef.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "Plugins/SymbolFile/DWARF/DWARFFormValue.h"

class DWARFDebugInfo;
class DWARFDeclContext;
class DWARFDIE;

namespace lldb_private {
class DWARFIndex {
public:
  DWARFIndex(Module &module) : m_module(module) {}
  virtual ~DWARFIndex();

  virtual void Preload() = 0;

  /// Finds global variables with the given base name. Any additional filtering
  /// (e.g., to only retrieve variables from a given context) should be done by
  /// the consumer.
  virtual void GetGlobalVariables(ConstString basename, DIEArray &offsets) = 0;

  virtual void GetGlobalVariables(const RegularExpression &regex,
                                  DIEArray &offsets) = 0;
  virtual void GetGlobalVariables(const DWARFUnit &cu, DIEArray &offsets) = 0;
  virtual void GetObjCMethods(ConstString class_name, DIEArray &offsets) = 0;
  virtual void GetCompleteObjCClass(ConstString class_name,
                                    bool must_be_implementation,
                                    DIEArray &offsets) = 0;
  virtual void GetTypes(ConstString name, DIEArray &offsets) = 0;
  virtual void GetTypes(const DWARFDeclContext &context, DIEArray &offsets) = 0;
  virtual void GetNamespaces(ConstString name, DIEArray &offsets) = 0;
  virtual void GetFunctions(ConstString name, DWARFDebugInfo &info,
                            const CompilerDeclContext &parent_decl_ctx,
                            uint32_t name_type_mask,
                            std::vector<DWARFDIE> &dies) = 0;
  virtual void GetFunctions(const RegularExpression &regex,
                            DIEArray &offsets) = 0;

  virtual void ReportInvalidDIEOffset(dw_offset_t offset,
                                      llvm::StringRef name) = 0;
  virtual void Dump(Stream &s) = 0;

protected:
  Module &m_module;

  /// Helper function implementing common logic for processing function dies. If
  /// the function given by "ref" matches search criteria given by
  /// "parent_decl_ctx" and "name_type_mask", it is inserted into the "dies"
  /// vector.
  void ProcessFunctionDIE(llvm::StringRef name, DIERef ref,
                          DWARFDebugInfo &info,
                          const CompilerDeclContext &parent_decl_ctx,
                          uint32_t name_type_mask, std::vector<DWARFDIE> &dies);
};
} // namespace lldb_private

#endif // LLDB_DWARFINDEX_H
