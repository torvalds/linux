//===-- SymbolFileSymtab.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileSymtab.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

void SymbolFileSymtab::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolFileSymtab::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString SymbolFileSymtab::GetPluginNameStatic() {
  static ConstString g_name("symtab");
  return g_name;
}

const char *SymbolFileSymtab::GetPluginDescriptionStatic() {
  return "Reads debug symbols from an object file's symbol table.";
}

SymbolFile *SymbolFileSymtab::CreateInstance(ObjectFile *obj_file) {
  return new SymbolFileSymtab(obj_file);
}

size_t SymbolFileSymtab::GetTypes(SymbolContextScope *sc_scope,
                                  TypeClass type_mask,
                                  lldb_private::TypeList &type_list) {
  return 0;
}

SymbolFileSymtab::SymbolFileSymtab(ObjectFile *obj_file)
    : SymbolFile(obj_file), m_source_indexes(), m_func_indexes(),
      m_code_indexes(), m_objc_class_name_to_index() {}

SymbolFileSymtab::~SymbolFileSymtab() {}

uint32_t SymbolFileSymtab::CalculateAbilities() {
  uint32_t abilities = 0;
  if (m_obj_file) {
    const Symtab *symtab = m_obj_file->GetSymtab();
    if (symtab) {
      //----------------------------------------------------------------------
      // The snippet of code below will get the indexes the module symbol table
      // entries that are code, data, or function related (debug info), sort
      // them by value (address) and dump the sorted symbols.
      //----------------------------------------------------------------------
      if (symtab->AppendSymbolIndexesWithType(eSymbolTypeSourceFile,
                                              m_source_indexes)) {
        abilities |= CompileUnits;
      }

      if (symtab->AppendSymbolIndexesWithType(
              eSymbolTypeCode, Symtab::eDebugYes, Symtab::eVisibilityAny,
              m_func_indexes)) {
        symtab->SortSymbolIndexesByValue(m_func_indexes, true);
        abilities |= Functions;
      }

      if (symtab->AppendSymbolIndexesWithType(eSymbolTypeCode, Symtab::eDebugNo,
                                              Symtab::eVisibilityAny,
                                              m_code_indexes)) {
        symtab->SortSymbolIndexesByValue(m_code_indexes, true);
        abilities |= Functions;
      }

      if (symtab->AppendSymbolIndexesWithType(eSymbolTypeData,
                                              m_data_indexes)) {
        symtab->SortSymbolIndexesByValue(m_data_indexes, true);
        abilities |= GlobalVariables;
      }

      lldb_private::Symtab::IndexCollection objc_class_indexes;
      if (symtab->AppendSymbolIndexesWithType(eSymbolTypeObjCClass,
                                              objc_class_indexes)) {
        symtab->AppendSymbolNamesToMap(objc_class_indexes, true, true,
                                       m_objc_class_name_to_index);
        m_objc_class_name_to_index.Sort();
      }
    }
  }
  return abilities;
}

uint32_t SymbolFileSymtab::GetNumCompileUnits() {
  // If we don't have any source file symbols we will just have one compile
  // unit for the entire object file
  if (m_source_indexes.empty())
    return 0;

  // If we have any source file symbols we will logically organize the object
  // symbols using these.
  return m_source_indexes.size();
}

CompUnitSP SymbolFileSymtab::ParseCompileUnitAtIndex(uint32_t idx) {
  CompUnitSP cu_sp;

  // If we don't have any source file symbols we will just have one compile
  // unit for the entire object file
  if (idx < m_source_indexes.size()) {
    const Symbol *cu_symbol =
        m_obj_file->GetSymtab()->SymbolAtIndex(m_source_indexes[idx]);
    if (cu_symbol)
      cu_sp.reset(new CompileUnit(m_obj_file->GetModule(), NULL,
                                  cu_symbol->GetName().AsCString(), 0,
                                  eLanguageTypeUnknown, eLazyBoolNo));
  }
  return cu_sp;
}

lldb::LanguageType SymbolFileSymtab::ParseLanguage(CompileUnit &comp_unit) {
  return eLanguageTypeUnknown;
}

size_t SymbolFileSymtab::ParseFunctions(CompileUnit &comp_unit) {
  size_t num_added = 0;
  // We must at least have a valid compile unit
  const Symtab *symtab = m_obj_file->GetSymtab();
  const Symbol *curr_symbol = NULL;
  const Symbol *next_symbol = NULL;
  //  const char *prefix = m_obj_file->SymbolPrefix();
  //  if (prefix == NULL)
  //      prefix == "";
  //
  //  const uint32_t prefix_len = strlen(prefix);

  // If we don't have any source file symbols we will just have one compile
  // unit for the entire object file
  if (m_source_indexes.empty()) {
    // The only time we will have a user ID of zero is when we don't have and
    // source file symbols and we declare one compile unit for the entire
    // object file
    if (!m_func_indexes.empty()) {
    }

    if (!m_code_indexes.empty()) {
      //          StreamFile s(stdout);
      //          symtab->Dump(&s, m_code_indexes);

      uint32_t idx = 0; // Index into the indexes
      const uint32_t num_indexes = m_code_indexes.size();
      for (idx = 0; idx < num_indexes; ++idx) {
        uint32_t symbol_idx = m_code_indexes[idx];
        curr_symbol = symtab->SymbolAtIndex(symbol_idx);
        if (curr_symbol) {
          // Union of all ranges in the function DIE (if the function is
          // discontiguous)
          AddressRange func_range(curr_symbol->GetAddress(), 0);
          if (func_range.GetBaseAddress().IsSectionOffset()) {
            uint32_t symbol_size = curr_symbol->GetByteSize();
            if (symbol_size != 0 && !curr_symbol->GetSizeIsSibling())
              func_range.SetByteSize(symbol_size);
            else if (idx + 1 < num_indexes) {
              next_symbol = symtab->SymbolAtIndex(m_code_indexes[idx + 1]);
              if (next_symbol) {
                func_range.SetByteSize(
                    next_symbol->GetAddressRef().GetOffset() -
                    curr_symbol->GetAddressRef().GetOffset());
              }
            }

            FunctionSP func_sp(
                new Function(&comp_unit,
                             symbol_idx,       // UserID is the DIE offset
                             LLDB_INVALID_UID, // We don't have any type info
                                               // for this function
                             curr_symbol->GetMangled(), // Linker/mangled name
                             NULL, // no return type for a code symbol...
                             func_range)); // first address range

            if (func_sp.get() != NULL) {
              comp_unit.AddFunction(func_sp);
              ++num_added;
            }
          }
        }
      }
    }
  } else {
    // We assume we
  }
  return num_added;
}

size_t SymbolFileSymtab::ParseTypes(CompileUnit &comp_unit) { return 0; }

bool SymbolFileSymtab::ParseLineTable(CompileUnit &comp_unit) { return false; }

bool SymbolFileSymtab::ParseDebugMacros(CompileUnit &comp_unit) {
  return false;
}

bool SymbolFileSymtab::ParseSupportFiles(CompileUnit &comp_unit,
                                         FileSpecList &support_files) {
  return false;
}

bool SymbolFileSymtab::ParseImportedModules(
    const SymbolContext &sc, std::vector<ConstString> &imported_modules) {
  return false;
}

size_t SymbolFileSymtab::ParseBlocksRecursive(Function &func) { return 0; }

size_t SymbolFileSymtab::ParseVariablesForContext(const SymbolContext &sc) {
  return 0;
}

Type *SymbolFileSymtab::ResolveTypeUID(lldb::user_id_t type_uid) {
  return NULL;
}

llvm::Optional<SymbolFile::ArrayInfo>
SymbolFileSymtab::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  return llvm::None;
}

bool SymbolFileSymtab::CompleteType(lldb_private::CompilerType &compiler_type) {
  return false;
}

uint32_t SymbolFileSymtab::ResolveSymbolContext(const Address &so_addr,
                                                SymbolContextItem resolve_scope,
                                                SymbolContext &sc) {
  if (m_obj_file->GetSymtab() == NULL)
    return 0;

  uint32_t resolved_flags = 0;
  if (resolve_scope & eSymbolContextSymbol) {
    sc.symbol = m_obj_file->GetSymtab()->FindSymbolContainingFileAddress(
        so_addr.GetFileAddress());
    if (sc.symbol)
      resolved_flags |= eSymbolContextSymbol;
  }
  return resolved_flags;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString SymbolFileSymtab::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t SymbolFileSymtab::GetPluginVersion() { return 1; }
