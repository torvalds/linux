//===-- CompileUnit.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CompUnit_h_
#define liblldb_CompUnit_h_

#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/ModuleChild.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/DenseMap.h"

namespace lldb_private {
//----------------------------------------------------------------------
/// @class CompileUnit CompileUnit.h "lldb/Symbol/CompileUnit.h"
/// A class that describes a compilation unit.
///
/// A representation of a compilation unit, or compiled source file.
/// The UserID of the compile unit is specified by the SymbolFile plug-in and
/// can have any value as long as the value is unique within the Module that
/// owns this compile units.
///
/// Each compile unit has a list of functions, global and static variables,
/// support file list (include files and inlined source files), and a line
/// table.
//----------------------------------------------------------------------
class CompileUnit : public std::enable_shared_from_this<CompileUnit>,
                    public ModuleChild,
                    public FileSpec,
                    public UserID,
                    public SymbolContextScope {
public:
  //------------------------------------------------------------------
  /// Construct with a module, path, UID and language.
  ///
  /// Initialize the compile unit given the owning \a module, a path to
  /// convert into a FileSpec, the SymbolFile plug-in supplied \a uid, and the
  /// source language type.
  ///
  /// @param[in] module
  ///     The parent module that owns this compile unit. This value
  ///     must be a valid pointer value.
  ///
  /// @param[in] user_data
  ///     User data where the SymbolFile parser can store data.
  ///
  /// @param[in] pathname
  ///     The path to the source file for this compile unit.
  ///
  /// @param[in] uid
  ///     The user ID of the compile unit. This value is supplied by
  ///     the SymbolFile plug-in and should be a value that allows
  ///     the SymbolFile plug-in to easily locate and parse additional
  ///     information for the compile unit.
  ///
  /// @param[in] language
  ///     A language enumeration type that describes the main language
  ///     of this compile unit.
  ///
  /// @param[in] is_optimized
  ///     A value that can initialized with eLazyBoolYes, eLazyBoolNo
  ///     or eLazyBoolCalculate. If set to eLazyBoolCalculate, then
  ///     an extra call into SymbolVendor will be made to calculate if
  ///     the compile unit is optimized will be made when
  ///     CompileUnit::GetIsOptimized() is called.
  ///
  /// @see lldb::LanguageType
  //------------------------------------------------------------------
  CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
              const char *pathname, lldb::user_id_t uid,
              lldb::LanguageType language, lldb_private::LazyBool is_optimized);

  //------------------------------------------------------------------
  /// Construct with a module, file spec, UID and language.
  ///
  /// Initialize the compile unit given the owning \a module, a path to
  /// convert into a FileSpec, the SymbolFile plug-in supplied \a uid, and the
  /// source language type.
  ///
  /// @param[in] module
  ///     The parent module that owns this compile unit. This value
  ///     must be a valid pointer value.
  ///
  /// @param[in] user_data
  ///     User data where the SymbolFile parser can store data.
  ///
  /// @param[in] file_spec
  ///     The file specification for the source file of this compile
  ///     unit.
  ///
  /// @param[in] uid
  ///     The user ID of the compile unit. This value is supplied by
  ///     the SymbolFile plug-in and should be a value that allows
  ///     the plug-in to easily locate and parse
  ///     additional information for the compile unit.
  ///
  /// @param[in] language
  ///     A language enumeration type that describes the main language
  ///     of this compile unit.
  ///
  /// @param[in] is_optimized
  ///     A value that can initialized with eLazyBoolYes, eLazyBoolNo
  ///     or eLazyBoolCalculate. If set to eLazyBoolCalculate, then
  ///     an extra call into SymbolVendor will be made to calculate if
  ///     the compile unit is optimized will be made when
  ///     CompileUnit::GetIsOptimized() is called.
  ///
  /// @see lldb::LanguageType
  //------------------------------------------------------------------
  CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
              const FileSpec &file_spec, lldb::user_id_t uid,
              lldb::LanguageType language, lldb_private::LazyBool is_optimized);

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~CompileUnit() override;

  //------------------------------------------------------------------
  /// Add a function to this compile unit.
  ///
  /// Typically called by the SymbolFile plug-ins as they partially parse the
  /// debug information.
  ///
  /// @param[in] function_sp
  ///     A shared pointer to the Function object.
  //------------------------------------------------------------------
  void AddFunction(lldb::FunctionSP &function_sp);

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  CompileUnit *CalculateSymbolContextCompileUnit() override;

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void DumpSymbolContext(Stream *s) override;

  lldb::LanguageType GetLanguage();

  void SetLanguage(lldb::LanguageType language) {
    m_flags.Set(flagsParsedLanguage);
    m_language = language;
  }

  void GetDescription(Stream *s, lldb::DescriptionLevel level) const;

  //------------------------------------------------------------------
  /// Apply a lambda to each function in this compile unit.
  ///
  /// This provides raw access to the function shared pointer list and will not
  /// cause the SymbolFile plug-in to parse any unparsed functions.
  ///
  /// @note Prefer using FindFunctionByUID over this if possible.
  ///
  /// @param[in] lambda
  ///     The lambda that should be applied to every function. The lambda can
  ///     return true if the iteration should be aborted earlier.
  //------------------------------------------------------------------
  void ForeachFunction(
      llvm::function_ref<bool(const lldb::FunctionSP &)> lambda) const;

  //------------------------------------------------------------------
  /// Dump the compile unit contents to the stream \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] show_context
  ///     If \b true, variables will dump their symbol context
  ///     information.
  //------------------------------------------------------------------
  void Dump(Stream *s, bool show_context) const;

  //------------------------------------------------------------------
  /// Find the line entry by line and optional inlined file spec.
  ///
  /// Finds the first line entry that has an index greater than \a start_idx
  /// that matches \a line. If \a file_spec_ptr is NULL, then the search
  /// matches line entries whose file matches the file for the compile unit.
  /// If \a file_spec_ptr is not NULL, line entries must match the specified
  /// file spec (for inlined line table entries).
  ///
  /// Multiple calls to this function can find all entries that match a given
  /// file and line by starting with \a start_idx equal to zero, and calling
  /// this function back with the return value + 1.
  ///
  /// @param[in] start_idx
  ///     The zero based index at which to start looking for matches.
  ///
  /// @param[in] line
  ///     The line number to search for.
  ///
  /// @param[in] file_spec_ptr
  ///     If non-NULL search for entries that match this file spec,
  ///     else if NULL, search for line entries that match the compile
  ///     unit file.
  ///
  /// @param[in] exact
  ///     If \btrue match only if there is a line table entry for this line
  ///     number.
  ///     If \bfalse, find the line table entry equal to or after this line
  ///     number.
  ///
  /// @param[out] line_entry
  ///     If non-NULL, a copy of the line entry that was found.
  ///
  /// @return
  ///     The zero based index of a matching line entry, or UINT32_MAX
  ///     if no matching line entry is found.
  //------------------------------------------------------------------
  uint32_t FindLineEntry(uint32_t start_idx, uint32_t line,
                         const FileSpec *file_spec_ptr, bool exact,
                         LineEntry *line_entry);

  //------------------------------------------------------------------
  /// Get the line table for the compile unit.
  ///
  /// Called by clients and the SymbolFile plug-in. The SymbolFile plug-ins
  /// use this function to determine if the line table has be parsed yet.
  /// Clients use this function to get the line table from a compile unit.
  ///
  /// @return
  ///     The line table object pointer, or NULL if this line table
  ///     hasn't been parsed yet.
  //------------------------------------------------------------------
  LineTable *GetLineTable();

  DebugMacros *GetDebugMacros();

  //------------------------------------------------------------------
  /// Get the compile unit's support file list.
  ///
  /// The support file list is used by the line table, and any objects that
  /// have valid Declaration objects.
  ///
  /// @return
  ///     A support file list object.
  //------------------------------------------------------------------
  FileSpecList &GetSupportFiles();

  //------------------------------------------------------------------
  /// Get the compile unit's imported module list.
  ///
  /// This reports all the imports that the compile unit made, including the
  /// current module.
  ///
  /// @return
  ///     A list of imported module names.
  //------------------------------------------------------------------
  const std::vector<ConstString> &GetImportedModules();

  //------------------------------------------------------------------
  /// Get the SymbolFile plug-in user data.
  ///
  /// SymbolFile plug-ins can store user data to internal state or objects to
  /// quickly allow them to parse more information for a given object.
  ///
  /// @return
  ///     The user data stored with the CompileUnit when it was
  ///     constructed.
  //------------------------------------------------------------------
  void *GetUserData() const;

  //------------------------------------------------------------------
  /// Get the variable list for a compile unit.
  ///
  /// Called by clients to get the variable list for a compile unit. The
  /// variable list will contain all global and static variables that were
  /// defined at the compile unit level.
  ///
  /// @param[in] can_create
  ///     If \b true, the variable list will be parsed on demand. If
  ///     \b false, the current variable list will be returned even
  ///     if it contains a NULL VariableList object (typically
  ///     called by dumping routines that want to display only what
  ///     has currently been parsed).
  ///
  /// @return
  ///     A shared pointer to a variable list, that can contain NULL
  ///     VariableList pointer if there are no global or static
  ///     variables.
  //------------------------------------------------------------------
  lldb::VariableListSP GetVariableList(bool can_create);

  //------------------------------------------------------------------
  /// Finds a function by user ID.
  ///
  /// Typically used by SymbolFile plug-ins when partially parsing the debug
  /// information to see if the function has been parsed yet.
  ///
  /// @param[in] uid
  ///     The user ID of the function to find. This value is supplied
  ///     by the SymbolFile plug-in and should be a value that
  ///     allows the plug-in to easily locate and parse additional
  ///     information in the function.
  ///
  /// @return
  ///     A shared pointer to the function object that might contain
  ///     a NULL Function pointer.
  //------------------------------------------------------------------
  lldb::FunctionSP FindFunctionByUID(lldb::user_id_t uid);

  //------------------------------------------------------------------
  /// Set the line table for the compile unit.
  ///
  /// Called by the SymbolFile plug-in when if first parses the line table and
  /// hands ownership of the line table to this object. The compile unit owns
  /// the line table object and will delete the object when it is deleted.
  ///
  /// @param[in] line_table
  ///     A line table object pointer that this object now owns.
  //------------------------------------------------------------------
  void SetLineTable(LineTable *line_table);

  void SetDebugMacros(const DebugMacrosSP &debug_macros);

  //------------------------------------------------------------------
  /// Set accessor for the variable list.
  ///
  /// Called by the SymbolFile plug-ins after they have parsed the variable
  /// lists and are ready to hand ownership of the list over to this object.
  ///
  /// @param[in] variable_list_sp
  ///     A shared pointer to a VariableList.
  //------------------------------------------------------------------
  void SetVariableList(lldb::VariableListSP &variable_list_sp);

  //------------------------------------------------------------------
  /// Resolve symbol contexts by file and line.
  ///
  /// Given a file in \a file_spec, and a line number, find all instances and
  /// append them to the supplied symbol context list \a sc_list.
  ///
  /// @param[in] file_spec
  ///     A file specification. If \a file_spec contains no directory
  ///     information, only the basename will be used when matching
  ///     contexts. If the directory in \a file_spec is valid, a
  ///     complete file specification match will be performed.
  ///
  /// @param[in] line
  ///     The line number to match against the compile unit's line
  ///     tables.
  ///
  /// @param[in] check_inlines
  ///     If \b true this function will also match any inline
  ///     file and line matches. If \b false, the compile unit's
  ///     file specification must match \a file_spec for any matches
  ///     to be returned.
  ///
  /// @param[in] exact
  ///     If true, only resolve the context if \a line exists in the line table.
  ///     If false, resolve the context to the closest line greater than \a line
  ///     in the line table.
  ///
  /// @param[in] resolve_scope
  ///     For each matching line entry, this bitfield indicates what
  ///     values within each SymbolContext that gets added to \a
  ///     sc_list will be resolved. See the SymbolContext::Scope
  ///     enumeration for a list of all available bits that can be
  ///     resolved. Only SymbolContext entries that can be resolved
  ///     using a LineEntry base address will be able to be resolved.
  ///
  /// @param[out] sc_list
  ///     A SymbolContext list class that will get any matching
  ///     entries appended to.
  ///
  /// @return
  ///     The number of new matches that were added to \a sc_list.
  ///
  /// @see enum SymbolContext::Scope
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContext(const FileSpec &file_spec, uint32_t line,
                                bool check_inlines, bool exact,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Get whether compiler optimizations were enabled for this compile unit
  ///
  /// "optimized" means that the debug experience may be difficult for the
  /// user to understand.  Variables may not be available when the developer
  /// would expect them, stepping through the source lines in the function may
  /// appear strange, etc.
  ///
  /// @return
  ///     Returns 'true' if this compile unit was compiled with
  ///     optimization.  'false' indicates that either the optimization
  ///     is unknown, or this compile unit was built without optimization.
  //------------------------------------------------------------------
  bool GetIsOptimized();

  //------------------------------------------------------------------
  /// Returns the number of functions in this compile unit
  //------------------------------------------------------------------
  size_t GetNumFunctions() const { return m_functions_by_uid.size(); }

protected:
  void *m_user_data; ///< User data for the SymbolFile parser to store
                     ///information into.
  lldb::LanguageType
      m_language; ///< The programming language enumeration value.
  Flags m_flags;  ///< Compile unit flags that help with partial parsing.

  /// Maps UIDs to functions.
  llvm::DenseMap<lldb::user_id_t, lldb::FunctionSP> m_functions_by_uid;
  std::vector<ConstString> m_imported_modules; ///< All modules, including the
                                               ///current module, imported by
                                               ///this
                                               ///< compile unit.
  FileSpecList m_support_files; ///< Files associated with this compile unit's
                                ///line table and declarations.
  std::unique_ptr<LineTable>
      m_line_table_ap; ///< Line table that will get parsed on demand.
  DebugMacrosSP
      m_debug_macros_sp; ///< Debug macros that will get parsed on demand.
  lldb::VariableListSP m_variables; ///< Global and static variable list that
                                    ///will get parsed on demand.
  lldb_private::LazyBool m_is_optimized; /// eLazyBoolYes if this compile unit
                                         /// was compiled with optimization.

private:
  enum {
    flagsParsedAllFunctions =
        (1u << 0), ///< Have we already parsed all our functions
    flagsParsedVariables =
        (1u << 1), ///< Have we already parsed globals and statics?
    flagsParsedSupportFiles = (1u << 2), ///< Have we already parsed the support
                                         ///files for this compile unit?
    flagsParsedLineTable =
        (1u << 3),                   ///< Have we parsed the line table already?
    flagsParsedLanguage = (1u << 4), ///< Have we parsed the language already?
    flagsParsedImportedModules =
        (1u << 5), ///< Have we parsed the imported modules already?
    flagsParsedDebugMacros =
        (1u << 6) ///< Have we parsed the debug macros already?
  };

  DISALLOW_COPY_AND_ASSIGN(CompileUnit);
};

} // namespace lldb_private

#endif // liblldb_CompUnit_h_
