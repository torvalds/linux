//===-- SymbolFileNativePDB.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILE_NATIVEPDB_SYMBOLFILENATIVEPDB_H
#define LLDB_PLUGINS_SYMBOLFILE_NATIVEPDB_SYMBOLFILENATIVEPDB_H

#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/SymbolFile.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "CompileUnitIndex.h"
#include "PdbIndex.h"

namespace clang {
class TagDecl;
}

namespace llvm {
namespace codeview {
class ClassRecord;
class EnumRecord;
class ModifierRecord;
class PointerRecord;
struct UnionRecord;
} // namespace codeview
} // namespace llvm

namespace lldb_private {
class ClangASTImporter;

namespace npdb {
class PdbAstBuilder;

class SymbolFileNativePDB : public SymbolFile {
  friend class UdtRecordCompleter;

public:
  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static void DebuggerInitialize(Debugger &debugger);

  static ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static SymbolFile *CreateInstance(ObjectFile *obj_file);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  SymbolFileNativePDB(ObjectFile *ofile);

  ~SymbolFileNativePDB() override;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  //------------------------------------------------------------------
  // Compile Unit function calls
  //------------------------------------------------------------------

  uint32_t GetNumCompileUnits() override;

  void
  ParseDeclsForContext(lldb_private::CompilerDeclContext decl_ctx) override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  lldb::LanguageType
  ParseLanguage(lldb_private::CompileUnit &comp_unit) override;

  size_t ParseFunctions(lldb_private::CompileUnit &comp_unit) override;

  bool ParseLineTable(lldb_private::CompileUnit &comp_unit) override;

  bool ParseDebugMacros(lldb_private::CompileUnit &comp_unit) override;

  bool ParseSupportFiles(lldb_private::CompileUnit &comp_unit,
                         FileSpecList &support_files) override;
  size_t ParseTypes(lldb_private::CompileUnit &comp_unit) override;

  bool
  ParseImportedModules(const SymbolContext &sc,
                       std::vector<ConstString> &imported_modules) override;

  size_t ParseBlocksRecursive(Function &func) override;

  uint32_t FindGlobalVariables(const ConstString &name,
                               const CompilerDeclContext *parent_decl_ctx,
                               uint32_t max_matches,
                               VariableList &variables) override;

  size_t ParseVariablesForContext(const SymbolContext &sc) override;

  void AddSymbols(Symtab &symtab) override;

  CompilerDecl GetDeclForUID(lldb::user_id_t uid) override;
  CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) override;
  CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) override;
  Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  llvm::Optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override;

  bool CompleteType(CompilerType &compiler_type) override;
  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;
  uint32_t ResolveSymbolContext(const FileSpec &file_spec, uint32_t line,
                                bool check_inlines,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list) override;

  size_t GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                  TypeList &type_list) override;

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

  TypeSystem *GetTypeSystemForLanguage(lldb::LanguageType language) override;

  CompilerDeclContext
  FindNamespace(const ConstString &name,
                const CompilerDeclContext *parent_decl_ctx) override;

  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  llvm::pdb::PDBFile &GetPDBFile() { return m_index->pdb(); }
  const llvm::pdb::PDBFile &GetPDBFile() const { return m_index->pdb(); }

  void DumpClangAST(Stream &s) override;

private:

  size_t FindTypesByName(llvm::StringRef name, uint32_t max_matches,
                         TypeMap &types);

  lldb::TypeSP CreateModifierType(PdbTypeSymId type_id,
                                  const llvm::codeview::ModifierRecord &mr,
                                  CompilerType ct);
  lldb::TypeSP CreatePointerType(PdbTypeSymId type_id,
                                 const llvm::codeview::PointerRecord &pr,
                                 CompilerType ct);
  lldb::TypeSP CreateSimpleType(llvm::codeview::TypeIndex ti, CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::ClassRecord &cr,
                             CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::EnumRecord &er,
                             CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::UnionRecord &ur,
                             CompilerType ct);
  lldb::TypeSP CreateArrayType(PdbTypeSymId type_id,
                               const llvm::codeview::ArrayRecord &ar,
                               CompilerType ct);
  lldb::TypeSP CreateProcedureType(PdbTypeSymId type_id,
                                   const llvm::codeview::ProcedureRecord &pr,
                                   CompilerType ct);
  lldb::TypeSP CreateClassStructUnion(PdbTypeSymId type_id,
                                      const llvm::codeview::TagRecord &record,
                                      size_t size, CompilerType ct);

  lldb::FunctionSP GetOrCreateFunction(PdbCompilandSymId func_id,
                                       CompileUnit &comp_unit);
  lldb::CompUnitSP GetOrCreateCompileUnit(const CompilandIndexItem &cci);
  lldb::TypeSP GetOrCreateType(PdbTypeSymId type_id);
  lldb::TypeSP GetOrCreateType(llvm::codeview::TypeIndex ti);
  lldb::VariableSP GetOrCreateGlobalVariable(PdbGlobalSymId var_id);
  Block &GetOrCreateBlock(PdbCompilandSymId block_id);
  lldb::VariableSP GetOrCreateLocalVariable(PdbCompilandSymId scope_id,
                                            PdbCompilandSymId var_id,
                                            bool is_param);
  lldb::TypeSP GetOrCreateTypedef(PdbGlobalSymId id);

  lldb::FunctionSP CreateFunction(PdbCompilandSymId func_id,
                                  CompileUnit &comp_unit);
  Block &CreateBlock(PdbCompilandSymId block_id);
  lldb::VariableSP CreateLocalVariable(PdbCompilandSymId scope_id,
                                       PdbCompilandSymId var_id, bool is_param);
  lldb::TypeSP CreateTypedef(PdbGlobalSymId id);
  lldb::CompUnitSP CreateCompileUnit(const CompilandIndexItem &cci);
  lldb::TypeSP CreateType(PdbTypeSymId type_id, CompilerType ct);
  lldb::TypeSP CreateAndCacheType(PdbTypeSymId type_id);
  lldb::VariableSP CreateGlobalVariable(PdbGlobalSymId var_id);
  lldb::VariableSP CreateConstantSymbol(PdbGlobalSymId var_id,
                                        const llvm::codeview::CVSymbol &cvs);
  size_t ParseVariablesForCompileUnit(CompileUnit &comp_unit,
                                      VariableList &variables);
  size_t ParseVariablesForBlock(PdbCompilandSymId block_id);

  llvm::BumpPtrAllocator m_allocator;

  lldb::addr_t m_obj_load_address = 0;
  bool m_done_full_type_scan = false;

  std::unique_ptr<PdbIndex> m_index;

  std::unique_ptr<PdbAstBuilder> m_ast;

  llvm::DenseMap<lldb::user_id_t, lldb::VariableSP> m_global_vars;
  llvm::DenseMap<lldb::user_id_t, lldb::VariableSP> m_local_variables;
  llvm::DenseMap<lldb::user_id_t, lldb::BlockSP> m_blocks;
  llvm::DenseMap<lldb::user_id_t, lldb::FunctionSP> m_functions;
  llvm::DenseMap<lldb::user_id_t, lldb::CompUnitSP> m_compilands;
  llvm::DenseMap<lldb::user_id_t, lldb::TypeSP> m_types;
};

} // namespace npdb
} // namespace lldb_private

#endif // lldb_Plugins_SymbolFile_PDB_SymbolFilePDB_h_
