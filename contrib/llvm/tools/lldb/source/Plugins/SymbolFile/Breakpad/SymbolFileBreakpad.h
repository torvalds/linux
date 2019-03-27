//===-- SymbolFileBreakpad.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILE_BREAKPAD_SYMBOLFILEBREAKPAD_H
#define LLDB_PLUGINS_SYMBOLFILE_BREAKPAD_SYMBOLFILEBREAKPAD_H

#include "lldb/Symbol/SymbolFile.h"

namespace lldb_private {

namespace breakpad {

class SymbolFileBreakpad : public SymbolFile {
public:
  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();
  static void Terminate();
  static void DebuggerInitialize(Debugger &debugger) {}
  static ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic() {
    return "Breakpad debug symbol file reader.";
  }

  static SymbolFile *CreateInstance(ObjectFile *obj_file) {
    return new SymbolFileBreakpad(obj_file);
  }

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  SymbolFileBreakpad(ObjectFile *object_file) : SymbolFile(object_file) {}

  ~SymbolFileBreakpad() override {}

  uint32_t CalculateAbilities() override;

  void InitializeObject() override {}

  //------------------------------------------------------------------
  // Compile Unit function calls
  //------------------------------------------------------------------

  uint32_t GetNumCompileUnits() override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override {
    return lldb::eLanguageTypeUnknown;
  }

  size_t ParseFunctions(CompileUnit &comp_unit) override;

  bool ParseLineTable(CompileUnit &comp_unit) override;

  bool ParseDebugMacros(CompileUnit &comp_unit) override { return false; }

  bool ParseSupportFiles(CompileUnit &comp_unit,
                         FileSpecList &support_files) override {
    return false;
  }
  size_t ParseTypes(CompileUnit &cu) override { return 0; }

  bool
  ParseImportedModules(const SymbolContext &sc,
                       std::vector<ConstString> &imported_modules) override {
    return false;
  }

  size_t ParseBlocksRecursive(Function &func) override { return 0; }

  uint32_t FindGlobalVariables(const ConstString &name,
                               const CompilerDeclContext *parent_decl_ctx,
                               uint32_t max_matches,
                               VariableList &variables) override {
    return 0;
  }

  size_t ParseVariablesForContext(const SymbolContext &sc) override {
    return 0;
  }
  Type *ResolveTypeUID(lldb::user_id_t type_uid) override { return nullptr; }
  llvm::Optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override {
    return llvm::None;
  }

  bool CompleteType(CompilerType &compiler_type) override { return false; }
  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;

  size_t GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                  TypeList &type_list) override {
    return 0;
  }

  uint32_t FindFunctions(const ConstString &name,
                         const CompilerDeclContext *parent_decl_ctx,
                         lldb::FunctionNameType name_type_mask,
                         bool include_inlines, bool append,
                         SymbolContextList &sc_list) override;

  uint32_t FindFunctions(const RegularExpression &regex, bool include_inlines,
                         bool append, SymbolContextList &sc_list) override;

  uint32_t FindTypes(const ConstString &name,
                     const CompilerDeclContext *parent_decl_ctx, bool append,
                     uint32_t max_matches,
                     llvm::DenseSet<SymbolFile *> &searched_symbol_files,
                     TypeMap &types) override;

  size_t FindTypes(const std::vector<CompilerContext> &context, bool append,
                   TypeMap &types) override;

  TypeSystem *GetTypeSystemForLanguage(lldb::LanguageType language) override {
    return nullptr;
  }

  CompilerDeclContext
  FindNamespace(const ConstString &name,
                const CompilerDeclContext *parent_decl_ctx) override {
    return CompilerDeclContext();
  }

  void AddSymbols(Symtab &symtab) override;

  ConstString GetPluginName() override { return GetPluginNameStatic(); }
  uint32_t GetPluginVersion() override { return 1; }

private:
};

} // namespace breakpad
} // namespace lldb_private

#endif
