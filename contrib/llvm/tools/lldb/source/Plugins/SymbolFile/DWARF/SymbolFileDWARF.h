//===-- SymbolFileDWARF.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_SymbolFileDWARF_h_
#define SymbolFileDWARF_SymbolFileDWARF_h_

#include <list>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Threading.h"

#include "lldb/Utility/Flags.h"

#include "lldb/Core/RangeMap.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

#include "DWARFDataExtractor.h"
#include "DWARFDefines.h"
#include "DWARFIndex.h"
#include "UniqueDWARFASTType.h"

//----------------------------------------------------------------------
// Forward Declarations for this DWARF plugin
//----------------------------------------------------------------------
class DebugMapModule;
class DWARFAbbreviationDeclaration;
class DWARFAbbreviationDeclarationSet;
class DWARFileUnit;
class DWARFDebugAbbrev;
class DWARFDebugAranges;
class DWARFDebugInfo;
class DWARFDebugInfoEntry;
class DWARFDebugLine;
class DWARFDebugRangesBase;
class DWARFDeclContext;
class DWARFDIECollection;
class DWARFFormValue;
class SymbolFileDWARFDebugMap;
class SymbolFileDWARFDwo;
class SymbolFileDWARFDwp;

#define DIE_IS_BEING_PARSED ((lldb_private::Type *)1)

class SymbolFileDWARF : public lldb_private::SymbolFile,
                        public lldb_private::UserID {
public:
  friend class SymbolFileDWARFDebugMap;
  friend class SymbolFileDWARFDwo;
  friend class DebugMapModule;
  friend struct DIERef;
  friend class DWARFUnit;
  friend class DWARFDIE;
  friend class DWARFASTParserClang;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::SymbolFile *
  CreateInstance(lldb_private::ObjectFile *obj_file);

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------

  SymbolFileDWARF(lldb_private::ObjectFile *ofile);

  ~SymbolFileDWARF() override;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  //------------------------------------------------------------------
  // Compile Unit function calls
  //------------------------------------------------------------------

  uint32_t GetNumCompileUnits() override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  lldb::LanguageType
  ParseLanguage(lldb_private::CompileUnit &comp_unit) override;

  size_t ParseFunctions(lldb_private::CompileUnit &comp_unit) override;

  bool ParseLineTable(lldb_private::CompileUnit &comp_unit) override;

  bool ParseDebugMacros(lldb_private::CompileUnit &comp_unit) override;

  bool ParseSupportFiles(lldb_private::CompileUnit &comp_unit,
                         lldb_private::FileSpecList &support_files) override;

  bool ParseIsOptimized(lldb_private::CompileUnit &comp_unit) override;

  size_t ParseTypes(lldb_private::CompileUnit &comp_unit) override;

  bool ParseImportedModules(
      const lldb_private::SymbolContext &sc,
      std::vector<lldb_private::ConstString> &imported_modules) override;

  size_t ParseBlocksRecursive(lldb_private::Function &func) override;

  size_t
  ParseVariablesForContext(const lldb_private::SymbolContext &sc) override;

  lldb_private::Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  llvm::Optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override;

  bool CompleteType(lldb_private::CompilerType &compiler_type) override;

  lldb_private::Type *ResolveType(const DWARFDIE &die,
                                  bool assert_not_being_parsed = true,
                                  bool resolve_function_context = false);

  SymbolFileDWARF *GetDWARFForUID(lldb::user_id_t uid);

  DWARFDIE
  GetDIEFromUID(lldb::user_id_t uid);

  lldb_private::CompilerDecl GetDeclForUID(lldb::user_id_t uid) override;

  lldb_private::CompilerDeclContext
  GetDeclContextForUID(lldb::user_id_t uid) override;

  lldb_private::CompilerDeclContext
  GetDeclContextContainingUID(lldb::user_id_t uid) override;

  void
  ParseDeclsForContext(lldb_private::CompilerDeclContext decl_ctx) override;

  uint32_t ResolveSymbolContext(const lldb_private::Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                lldb_private::SymbolContext &sc) override;

  uint32_t
  ResolveSymbolContext(const lldb_private::FileSpec &file_spec, uint32_t line,
                       bool check_inlines,
                       lldb::SymbolContextItem resolve_scope,
                       lldb_private::SymbolContextList &sc_list) override;

  uint32_t
  FindGlobalVariables(const lldb_private::ConstString &name,
                      const lldb_private::CompilerDeclContext *parent_decl_ctx,
                      uint32_t max_matches,
                      lldb_private::VariableList &variables) override;

  uint32_t FindGlobalVariables(const lldb_private::RegularExpression &regex,
                               uint32_t max_matches,
                               lldb_private::VariableList &variables) override;

  uint32_t
  FindFunctions(const lldb_private::ConstString &name,
                const lldb_private::CompilerDeclContext *parent_decl_ctx,
                lldb::FunctionNameType name_type_mask, bool include_inlines,
                bool append, lldb_private::SymbolContextList &sc_list) override;

  uint32_t FindFunctions(const lldb_private::RegularExpression &regex,
                         bool include_inlines, bool append,
                         lldb_private::SymbolContextList &sc_list) override;

  void GetMangledNamesForFunction(
      const std::string &scope_qualified_name,
      std::vector<lldb_private::ConstString> &mangled_names) override;

  uint32_t
  FindTypes(const lldb_private::ConstString &name,
            const lldb_private::CompilerDeclContext *parent_decl_ctx,
            bool append, uint32_t max_matches,
            llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
            lldb_private::TypeMap &types) override;

  size_t FindTypes(const std::vector<lldb_private::CompilerContext> &context,
                   bool append, lldb_private::TypeMap &types) override;

  lldb_private::TypeList *GetTypeList() override;

  size_t GetTypes(lldb_private::SymbolContextScope *sc_scope,
                  lldb::TypeClass type_mask,
                  lldb_private::TypeList &type_list) override;

  lldb_private::TypeSystem *
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  lldb_private::CompilerDeclContext FindNamespace(
      const lldb_private::ConstString &name,
      const lldb_private::CompilerDeclContext *parent_decl_ctx) override;

  void PreloadSymbols() override;

  std::recursive_mutex &GetModuleMutex() const override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  virtual const lldb_private::DWARFDataExtractor &get_debug_abbrev_data();
  virtual const lldb_private::DWARFDataExtractor &get_debug_addr_data();
  const lldb_private::DWARFDataExtractor &get_debug_aranges_data();
  const lldb_private::DWARFDataExtractor &get_debug_frame_data();
  virtual const lldb_private::DWARFDataExtractor &get_debug_info_data();
  const lldb_private::DWARFDataExtractor &get_debug_line_data();
  const lldb_private::DWARFDataExtractor &get_debug_line_str_data();
  const lldb_private::DWARFDataExtractor &get_debug_macro_data();
  const lldb_private::DWARFDataExtractor &get_debug_loc_data();
  const lldb_private::DWARFDataExtractor &get_debug_loclists_data();
  const lldb_private::DWARFDataExtractor &get_debug_ranges_data();
  const lldb_private::DWARFDataExtractor &get_debug_rnglists_data();
  virtual const lldb_private::DWARFDataExtractor &get_debug_str_data();
  virtual const lldb_private::DWARFDataExtractor &get_debug_str_offsets_data();
  const lldb_private::DWARFDataExtractor &get_debug_types_data();
  const lldb_private::DWARFDataExtractor &get_apple_names_data();
  const lldb_private::DWARFDataExtractor &get_apple_types_data();
  const lldb_private::DWARFDataExtractor &get_apple_namespaces_data();
  const lldb_private::DWARFDataExtractor &get_apple_objc_data();
  const lldb_private::DWARFDataExtractor &get_gnu_debugaltlink();

  DWARFDebugAbbrev *DebugAbbrev();

  const DWARFDebugAbbrev *DebugAbbrev() const;

  DWARFDebugInfo *DebugInfo();

  const DWARFDebugInfo *DebugInfo() const;

  DWARFDebugRangesBase *DebugRanges();

  const DWARFDebugRangesBase *DebugRanges() const;

  const lldb_private::DWARFDataExtractor &DebugLocData();

  static bool SupportedVersion(uint16_t version);

  DWARFDIE
  GetDeclContextDIEContainingDIE(const DWARFDIE &die);

  bool
  HasForwardDeclForClangType(const lldb_private::CompilerType &compiler_type);

  lldb_private::CompileUnit *
  GetCompUnitForDWARFCompUnit(DWARFUnit *dwarf_cu,
                              uint32_t cu_idx = UINT32_MAX);

  virtual size_t GetObjCMethodDIEOffsets(lldb_private::ConstString class_name,
                                         DIEArray &method_die_offsets);

  bool Supports_DW_AT_APPLE_objc_complete_type(DWARFUnit *cu);

  lldb_private::DebugMacrosSP ParseDebugMacros(lldb::offset_t *offset);

  static DWARFDIE GetParentSymbolContextDIE(const DWARFDIE &die);

  virtual lldb::CompUnitSP ParseCompileUnit(DWARFUnit *dwarf_cu,
                                            uint32_t cu_idx);

  virtual lldb_private::DWARFExpression::LocationListFormat
  GetLocationListFormat() const;

  lldb::ModuleSP GetDWOModule(lldb_private::ConstString name);

  typedef std::map<lldb_private::ConstString, lldb::ModuleSP>
      ExternalTypeModuleMap;

  /// Return the list of Clang modules imported by this SymbolFile.
  const ExternalTypeModuleMap& getExternalTypeModules() const {
      return m_external_type_modules;
  }

  virtual DWARFDIE GetDIE(const DIERef &die_ref);

  virtual std::unique_ptr<SymbolFileDWARFDwo>
  GetDwoSymbolFileForCompileUnit(DWARFUnit &dwarf_cu,
                                 const DWARFDebugInfoEntry &cu_die);

  // For regular SymbolFileDWARF instances the method returns nullptr,
  // for the instances of the subclass SymbolFileDWARFDwo
  // the method returns a pointer to the base compile unit.
  virtual DWARFUnit *GetBaseCompileUnit();

  static bool
  DIEInDeclContext(const lldb_private::CompilerDeclContext *parent_decl_ctx,
                   const DWARFDIE &die);

  std::vector<lldb_private::CallEdge>
  ParseCallEdgesInFunction(UserID func_id) override;

  void Dump(lldb_private::Stream &s) override;

  void DumpClangAST(lldb_private::Stream &s) override;

protected:
  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, lldb_private::Type *>
      DIEToTypePtr;
  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, lldb::VariableSP>
      DIEToVariableSP;
  typedef llvm::DenseMap<const DWARFDebugInfoEntry *,
                         lldb::opaque_compiler_type_t>
      DIEToClangType;
  typedef llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef> ClangTypeToDIE;

  struct DWARFDataSegment {
    llvm::once_flag m_flag;
    lldb_private::DWARFDataExtractor m_data;
  };

  DISALLOW_COPY_AND_ASSIGN(SymbolFileDWARF);

  const lldb_private::DWARFDataExtractor &
  GetCachedSectionData(lldb::SectionType sect_type,
                       DWARFDataSegment &data_segment);

  virtual void LoadSectionData(lldb::SectionType sect_type,
                               lldb_private::DWARFDataExtractor &data);

  bool DeclContextMatchesThisSymbolFile(
      const lldb_private::CompilerDeclContext *decl_ctx);

  virtual DWARFUnit *
  GetDWARFCompileUnit(lldb_private::CompileUnit *comp_unit);

  DWARFUnit *GetNextUnparsedDWARFCompileUnit(DWARFUnit *prev_cu);

  bool GetFunction(const DWARFDIE &die, lldb_private::SymbolContext &sc);

  lldb_private::Function *ParseFunction(lldb_private::CompileUnit &comp_unit,
                                        const DWARFDIE &die);

  size_t ParseBlocksRecursive(lldb_private::CompileUnit &comp_unit,
                              lldb_private::Block *parent_block,
                              const DWARFDIE &die,
                              lldb::addr_t subprogram_low_pc, uint32_t depth);

  size_t ParseTypes(const lldb_private::SymbolContext &sc, const DWARFDIE &die,
                    bool parse_siblings, bool parse_children);

  lldb::TypeSP ParseType(const lldb_private::SymbolContext &sc,
                         const DWARFDIE &die, bool *type_is_new);

  lldb_private::Type *ResolveTypeUID(const DWARFDIE &die,
                                     bool assert_not_being_parsed);

  lldb_private::Type *ResolveTypeUID(const DIERef &die_ref);

  lldb::VariableSP ParseVariableDIE(const lldb_private::SymbolContext &sc,
                                    const DWARFDIE &die,
                                    const lldb::addr_t func_low_pc);

  size_t ParseVariables(const lldb_private::SymbolContext &sc,
                        const DWARFDIE &orig_die,
                        const lldb::addr_t func_low_pc, bool parse_siblings,
                        bool parse_children,
                        lldb_private::VariableList *cc_variable_list = NULL);

  bool ClassOrStructIsVirtual(const DWARFDIE &die);

  // Given a die_offset, figure out the symbol context representing that die.
  bool ResolveFunction(const DWARFDIE &die, bool include_inlines,
                       lldb_private::SymbolContextList &sc_list);

  virtual lldb::TypeSP
  FindDefinitionTypeForDWARFDeclContext(const DWARFDeclContext &die_decl_ctx);

  virtual lldb::TypeSP FindCompleteObjCDefinitionTypeForDIE(
      const DWARFDIE &die, const lldb_private::ConstString &type_name,
      bool must_be_implementation);

  lldb_private::Symbol *
  GetObjCClassSymbol(const lldb_private::ConstString &objc_class_name);

  lldb::TypeSP GetTypeForDIE(const DWARFDIE &die,
                             bool resolve_function_context = false);

  void SetDebugMapModule(const lldb::ModuleSP &module_sp) {
    m_debug_map_module_wp = module_sp;
  }

  SymbolFileDWARFDebugMap *GetDebugMapSymfile();

  DWARFDIE
  FindBlockContainingSpecification(const DIERef &func_die_ref,
                                   dw_offset_t spec_block_die_offset);

  DWARFDIE
  FindBlockContainingSpecification(const DWARFDIE &die,
                                   dw_offset_t spec_block_die_offset);

  virtual UniqueDWARFASTTypeMap &GetUniqueDWARFASTTypeMap();

  bool DIEDeclContextsMatch(const DWARFDIE &die1, const DWARFDIE &die2);

  bool ClassContainsSelector(const DWARFDIE &class_die,
                             const lldb_private::ConstString &selector);

  bool FixupAddress(lldb_private::Address &addr);

  typedef std::set<lldb_private::Type *> TypeSet;

  void GetTypes(const DWARFDIE &die, dw_offset_t min_die_offset,
                dw_offset_t max_die_offset, uint32_t type_mask,
                TypeSet &type_set);

  typedef lldb_private::RangeDataVector<lldb::addr_t, lldb::addr_t,
                                        lldb_private::Variable *>
      GlobalVariableMap;

  GlobalVariableMap &GetGlobalAranges();

  void UpdateExternalModuleListIfNeeded();

  virtual DIEToTypePtr &GetDIEToType() { return m_die_to_type; }

  virtual DIEToVariableSP &GetDIEToVariable() { return m_die_to_variable_sp; }

  virtual DIEToClangType &GetForwardDeclDieToClangType() {
    return m_forward_decl_die_to_clang_type;
  }

  virtual ClangTypeToDIE &GetForwardDeclClangTypeToDie() {
    return m_forward_decl_clang_type_to_die;
  }

  SymbolFileDWARFDwp *GetDwpSymbolFile();

  lldb::ModuleWP m_debug_map_module_wp;
  SymbolFileDWARFDebugMap *m_debug_map_symfile;

  llvm::once_flag m_dwp_symfile_once_flag;
  std::unique_ptr<SymbolFileDWARFDwp> m_dwp_symfile;

  lldb_private::DWARFDataExtractor m_dwarf_data;

  DWARFDataSegment m_data_debug_abbrev;
  DWARFDataSegment m_data_debug_addr;
  DWARFDataSegment m_data_debug_aranges;
  DWARFDataSegment m_data_debug_frame;
  DWARFDataSegment m_data_debug_info;
  DWARFDataSegment m_data_debug_line;
  DWARFDataSegment m_data_debug_line_str;
  DWARFDataSegment m_data_debug_macro;
  DWARFDataSegment m_data_debug_loc;
  DWARFDataSegment m_data_debug_loclists;
  DWARFDataSegment m_data_debug_ranges;
  DWARFDataSegment m_data_debug_rnglists;
  DWARFDataSegment m_data_debug_str;
  DWARFDataSegment m_data_debug_str_offsets;
  DWARFDataSegment m_data_debug_types;
  DWARFDataSegment m_data_apple_names;
  DWARFDataSegment m_data_apple_types;
  DWARFDataSegment m_data_apple_namespaces;
  DWARFDataSegment m_data_apple_objc;
  DWARFDataSegment m_data_gnu_debugaltlink;

  // The unique pointer items below are generated on demand if and when someone
  // accesses
  // them through a non const version of this class.
  std::unique_ptr<DWARFDebugAbbrev> m_abbr;
  std::unique_ptr<DWARFDebugInfo> m_info;
  std::unique_ptr<DWARFDebugLine> m_line;
  std::unique_ptr<GlobalVariableMap> m_global_aranges_ap;

  typedef std::unordered_map<lldb::offset_t, lldb_private::DebugMacrosSP>
      DebugMacrosMap;
  DebugMacrosMap m_debug_macros_map;

  ExternalTypeModuleMap m_external_type_modules;
  std::unique_ptr<lldb_private::DWARFIndex> m_index;
  bool m_fetched_external_modules : 1;
  lldb_private::LazyBool m_supports_DW_AT_APPLE_objc_complete_type;

  typedef std::shared_ptr<std::set<DIERef>> DIERefSetSP;
  typedef std::unordered_map<std::string, DIERefSetSP> NameToOffsetMap;
  NameToOffsetMap m_function_scope_qualified_name_map;
  std::unique_ptr<DWARFDebugRangesBase> m_ranges;
  UniqueDWARFASTTypeMap m_unique_ast_type_map;
  DIEToTypePtr m_die_to_type;
  DIEToVariableSP m_die_to_variable_sp;
  DIEToClangType m_forward_decl_die_to_clang_type;
  ClangTypeToDIE m_forward_decl_clang_type_to_die;
};

#endif // SymbolFileDWARF_SymbolFileDWARF_h_
