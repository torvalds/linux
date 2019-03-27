//===-- SymbolVendor.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SymbolVendor_h_
#define liblldb_SymbolVendor_h_

#include <vector>

#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/lldb-private.h"
#include "llvm/ADT/DenseSet.h"

namespace lldb_private {

//----------------------------------------------------------------------
// The symbol vendor class is designed to abstract the process of searching for
// debug information for a given module. Platforms can subclass this class and
// provide extra ways to find debug information. Examples would be a subclass
// that would allow for locating a stand alone debug file, parsing debug maps,
// or runtime data in the object files. A symbol vendor can use multiple
// sources (SymbolFile objects) to provide the information and only parse as
// deep as needed in order to provide the information that is requested.
//----------------------------------------------------------------------
class SymbolVendor : public ModuleChild, public PluginInterface {
public:
  static SymbolVendor *FindPlugin(const lldb::ModuleSP &module_sp,
                                  Stream *feedback_strm);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  SymbolVendor(const lldb::ModuleSP &module_sp);

  ~SymbolVendor() override;

  void AddSymbolFileRepresentation(const lldb::ObjectFileSP &objfile_sp);

  virtual void Dump(Stream *s);

  virtual lldb::LanguageType ParseLanguage(CompileUnit &comp_unit);

  virtual size_t ParseFunctions(CompileUnit &comp_unit);

  virtual bool ParseLineTable(CompileUnit &comp_unit);

  virtual bool ParseDebugMacros(CompileUnit &comp_unit);

  virtual bool ParseSupportFiles(CompileUnit &comp_unit,
                                 FileSpecList &support_files);

  virtual bool ParseIsOptimized(CompileUnit &comp_unit);

  virtual size_t ParseTypes(CompileUnit &comp_unit);

  virtual bool ParseImportedModules(const SymbolContext &sc,
                                    std::vector<ConstString> &imported_modules);

  virtual size_t ParseBlocksRecursive(Function &func);

  virtual size_t ParseVariablesForContext(const SymbolContext &sc);

  virtual Type *ResolveTypeUID(lldb::user_id_t type_uid);

  virtual uint32_t ResolveSymbolContext(const Address &so_addr,
                                        lldb::SymbolContextItem resolve_scope,
                                        SymbolContext &sc);

  virtual uint32_t ResolveSymbolContext(const FileSpec &file_spec,
                                        uint32_t line, bool check_inlines,
                                        lldb::SymbolContextItem resolve_scope,
                                        SymbolContextList &sc_list);

  virtual size_t FindGlobalVariables(const ConstString &name,
                                     const CompilerDeclContext *parent_decl_ctx,
                                     size_t max_matches,
                                     VariableList &variables);

  virtual size_t FindGlobalVariables(const RegularExpression &regex,
                                     size_t max_matches,
                                     VariableList &variables);

  virtual size_t FindFunctions(const ConstString &name,
                               const CompilerDeclContext *parent_decl_ctx,
                               lldb::FunctionNameType name_type_mask,
                               bool include_inlines, bool append,
                               SymbolContextList &sc_list);

  virtual size_t FindFunctions(const RegularExpression &regex,
                               bool include_inlines, bool append,
                               SymbolContextList &sc_list);

  virtual size_t
  FindTypes(const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
            bool append, size_t max_matches,
            llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
            TypeMap &types);

  virtual size_t FindTypes(const std::vector<CompilerContext> &context,
                           bool append, TypeMap &types);

  virtual CompilerDeclContext
  FindNamespace(const ConstString &name,
                const CompilerDeclContext *parent_decl_ctx);

  virtual size_t GetNumCompileUnits();

  virtual bool SetCompileUnitAtIndex(size_t cu_idx,
                                     const lldb::CompUnitSP &cu_sp);

  virtual lldb::CompUnitSP GetCompileUnitAtIndex(size_t idx);

  TypeList &GetTypeList() { return m_type_list; }

  const TypeList &GetTypeList() const { return m_type_list; }

  virtual size_t GetTypes(SymbolContextScope *sc_scope,
                          lldb::TypeClass type_mask, TypeList &type_list);

  SymbolFile *GetSymbolFile() { return m_sym_file_ap.get(); }

  FileSpec GetMainFileSpec() const;

  // Get module unified section list symbol table.
  virtual Symtab *GetSymtab();

  // Clear module unified section list symbol table.
  virtual void ClearSymtab();

  //------------------------------------------------------------------
  /// Notify the SymbolVendor that the file addresses in the Sections
  /// for this module have been changed.
  //------------------------------------------------------------------
  virtual void SectionFileAddressesChanged();

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

protected:
  //------------------------------------------------------------------
  // Classes that inherit from SymbolVendor can see and modify these
  //------------------------------------------------------------------
  typedef std::vector<lldb::CompUnitSP> CompileUnits;
  typedef CompileUnits::iterator CompileUnitIter;
  typedef CompileUnits::const_iterator CompileUnitConstIter;

  TypeList m_type_list; // Uniqued types for all parsers owned by this module
  CompileUnits m_compile_units;    // The current compile units
  lldb::ObjectFileSP m_objfile_sp; // Keep a reference to the object file in
                                   // case it isn't the same as the module
                                   // object file (debug symbols in a separate
                                   // file)
  std::unique_ptr<SymbolFile> m_sym_file_ap; // A single symbol file. Subclasses
                                             // can add more of these if needed.
  Symtab *m_symtab; // Save a symtab once to not pass it through `AddSymbols` of
                    // the symbol file each time when it is needed

private:
  //------------------------------------------------------------------
  // For SymbolVendor only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(SymbolVendor);
};

} // namespace lldb_private

#endif // liblldb_SymbolVendor_h_
