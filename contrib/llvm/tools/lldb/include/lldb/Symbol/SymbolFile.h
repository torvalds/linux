//===-- SymbolFile.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SymbolFile_h_
#define liblldb_SymbolFile_h_

#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/DenseSet.h"

#include <mutex>

#if defined(LLDB_CONFIGURATION_DEBUG)
#define ASSERT_MODULE_LOCK(expr) (expr->AssertModuleLock())
#else
#define ASSERT_MODULE_LOCK(expr) ((void)0)
#endif

namespace lldb_private {

class SymbolFile : public PluginInterface {
public:
  //------------------------------------------------------------------
  // Symbol file ability bits.
  //
  // Each symbol file can claim to support one or more symbol file abilities.
  // These get returned from SymbolFile::GetAbilities(). These help us to
  // determine which plug-in will be best to load the debug information found
  // in files.
  //------------------------------------------------------------------
  enum Abilities {
    CompileUnits = (1u << 0),
    LineTables = (1u << 1),
    Functions = (1u << 2),
    Blocks = (1u << 3),
    GlobalVariables = (1u << 4),
    LocalVariables = (1u << 5),
    VariableTypes = (1u << 6),
    kAllAbilities = ((1u << 7) - 1u)
  };

  static SymbolFile *FindPlugin(ObjectFile *obj_file);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  SymbolFile(ObjectFile *obj_file)
      : m_obj_file(obj_file), m_abilities(0), m_calculated_abilities(false) {}

  ~SymbolFile() override {}

  //------------------------------------------------------------------
  /// Get a mask of what this symbol file supports for the object file
  /// that it was constructed with.
  ///
  /// Each symbol file gets to respond with a mask of abilities that
  /// it supports for each object file. This happens when we are
  /// trying to figure out which symbol file plug-in will get used
  /// for a given object file. The plug-in that responds with the
  /// best mix of "SymbolFile::Abilities" bits set, will get chosen to
  /// be the symbol file parser. This allows each plug-in to check for
  /// sections that contain data a symbol file plug-in would need. For
  /// example the DWARF plug-in requires DWARF sections in a file that
  /// contain debug information. If the DWARF plug-in doesn't find
  /// these sections, it won't respond with many ability bits set, and
  /// we will probably fall back to the symbol table SymbolFile plug-in
  /// which uses any information in the symbol table. Also, plug-ins
  /// might check for some specific symbols in a symbol table in the
  /// case where the symbol table contains debug information (STABS
  /// and COFF). Not a lot of work should happen in these functions
  /// as the plug-in might not get selected due to another plug-in
  /// having more abilities. Any initialization work should be saved
  /// for "void SymbolFile::InitializeObject()" which will get called
  /// on the SymbolFile object with the best set of abilities.
  ///
  /// @return
  ///     A uint32_t mask containing bits from the SymbolFile::Abilities
  ///     enumeration. Any bits that are set represent an ability that
  ///     this symbol plug-in can parse from the object file.
  ///------------------------------------------------------------------
  uint32_t GetAbilities() {
    if (!m_calculated_abilities) {
      m_abilities = CalculateAbilities();
      m_calculated_abilities = true;
    }

    return m_abilities;
  }

  virtual uint32_t CalculateAbilities() = 0;

  //------------------------------------------------------------------
  /// Symbols file subclasses should override this to return the Module that
  /// owns the TypeSystem that this symbol file modifies type information in.
  //------------------------------------------------------------------
  virtual std::recursive_mutex &GetModuleMutex() const;

  //------------------------------------------------------------------
  /// Initialize the SymbolFile object.
  ///
  /// The SymbolFile object with the best set of abilities (detected
  /// in "uint32_t SymbolFile::GetAbilities()) will have this function
  /// called if it is chosen to parse an object file. More complete
  /// initialization can happen in this function which will get called
  /// prior to any other functions in the SymbolFile protocol.
  //------------------------------------------------------------------
  virtual void InitializeObject() {}

  //------------------------------------------------------------------
  // Compile Unit function calls
  //------------------------------------------------------------------
  // Approach 1 - iterator
  virtual uint32_t GetNumCompileUnits() = 0;
  virtual lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) = 0;

  virtual lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) = 0;
  virtual size_t ParseFunctions(CompileUnit &comp_unit) = 0;
  virtual bool ParseLineTable(CompileUnit &comp_unit) = 0;
  virtual bool ParseDebugMacros(CompileUnit &comp_unit) = 0;
  virtual bool ParseSupportFiles(CompileUnit &comp_unit,
                                 FileSpecList &support_files) = 0;
  virtual size_t ParseTypes(CompileUnit &comp_unit) = 0;
  virtual bool ParseIsOptimized(CompileUnit &comp_unit) { return false; }

  virtual bool
  ParseImportedModules(const SymbolContext &sc,
                       std::vector<ConstString> &imported_modules) = 0;
  virtual size_t ParseBlocksRecursive(Function &func) = 0;
  virtual size_t ParseVariablesForContext(const SymbolContext &sc) = 0;
  virtual Type *ResolveTypeUID(lldb::user_id_t type_uid) = 0;


  /// The characteristics of an array type.
  struct ArrayInfo {
    int64_t first_index;
    llvm::SmallVector<uint64_t, 1> element_orders;
    uint32_t byte_stride;
    uint32_t bit_stride;
  };
  /// If \c type_uid points to an array type, return its characteristics.
  /// To support variable-length array types, this function takes an
  /// optional \p ExtecutionContext. If \c exe_ctx is non-null, the
  /// dynamic characteristics for that context are returned.
  virtual llvm::Optional<ArrayInfo>
  GetDynamicArrayInfoForUID(lldb::user_id_t type_uid,
                            const lldb_private::ExecutionContext *exe_ctx) = 0;

  virtual bool CompleteType(CompilerType &compiler_type) = 0;
  virtual void ParseDeclsForContext(CompilerDeclContext decl_ctx) {}
  virtual CompilerDecl GetDeclForUID(lldb::user_id_t uid) {
    return CompilerDecl();
  }
  virtual CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) {
    return CompilerDeclContext();
  }
  virtual CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) {
    return CompilerDeclContext();
  }
  virtual uint32_t ResolveSymbolContext(const Address &so_addr,
                                        lldb::SymbolContextItem resolve_scope,
                                        SymbolContext &sc) = 0;
  virtual uint32_t ResolveSymbolContext(const FileSpec &file_spec,
                                        uint32_t line, bool check_inlines,
                                        lldb::SymbolContextItem resolve_scope,
                                        SymbolContextList &sc_list);

  virtual void DumpClangAST(Stream &s) {}
  virtual uint32_t
  FindGlobalVariables(const ConstString &name,
                      const CompilerDeclContext *parent_decl_ctx,
                      uint32_t max_matches, VariableList &variables);
  virtual uint32_t FindGlobalVariables(const RegularExpression &regex,
                                       uint32_t max_matches,
                                       VariableList &variables);
  virtual uint32_t FindFunctions(const ConstString &name,
                                 const CompilerDeclContext *parent_decl_ctx,
                                 lldb::FunctionNameType name_type_mask,
                                 bool include_inlines, bool append,
                                 SymbolContextList &sc_list);
  virtual uint32_t FindFunctions(const RegularExpression &regex,
                                 bool include_inlines, bool append,
                                 SymbolContextList &sc_list);
  virtual uint32_t
  FindTypes(const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
            bool append, uint32_t max_matches,
            llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
            TypeMap &types);
  virtual size_t FindTypes(const std::vector<CompilerContext> &context,
                           bool append, TypeMap &types);

  virtual void
  GetMangledNamesForFunction(const std::string &scope_qualified_name,
                             std::vector<ConstString> &mangled_names);
  //  virtual uint32_t        FindTypes (const SymbolContext& sc, const
  //  RegularExpression& regex, bool append, uint32_t max_matches, TypeList&
  //  types) = 0;
  virtual TypeList *GetTypeList();
  virtual size_t GetTypes(lldb_private::SymbolContextScope *sc_scope,
                          lldb::TypeClass type_mask,
                          lldb_private::TypeList &type_list) = 0;

  virtual void PreloadSymbols();

  virtual lldb_private::TypeSystem *
  GetTypeSystemForLanguage(lldb::LanguageType language);

  virtual CompilerDeclContext
  FindNamespace(const ConstString &name,
                const CompilerDeclContext *parent_decl_ctx) {
    return CompilerDeclContext();
  }

  ObjectFile *GetObjectFile() { return m_obj_file; }
  const ObjectFile *GetObjectFile() const { return m_obj_file; }

  virtual std::vector<CallEdge> ParseCallEdgesInFunction(UserID func_id) {
    return {};
  }

  virtual void AddSymbols(Symtab &symtab) {}

  //------------------------------------------------------------------
  /// Notify the SymbolFile that the file addresses in the Sections
  /// for this module have been changed.
  //------------------------------------------------------------------
  virtual void SectionFileAddressesChanged() {}

  virtual void Dump(Stream &s) {}

protected:
  void AssertModuleLock();

  ObjectFile *m_obj_file; // The object file that symbols can be extracted from.
  uint32_t m_abilities;
  bool m_calculated_abilities;

private:
  DISALLOW_COPY_AND_ASSIGN(SymbolFile);
};

} // namespace lldb_private

#endif // liblldb_SymbolFile_h_
