//===-- ModuleList.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ModuleList_h_
#define liblldb_ModuleList_h_

#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseSet.h"

#include <functional>
#include <list>
#include <mutex>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class FileSpecList;
}
namespace lldb_private {
class Function;
}
namespace lldb_private {
class Log;
}
namespace lldb_private {
class Module;
}
namespace lldb_private {
class RegularExpression;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolContext;
}
namespace lldb_private {
class SymbolContextList;
}
namespace lldb_private {
class SymbolFile;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
class TypeList;
}
namespace lldb_private {
class UUID;
}
namespace lldb_private {
class VariableList;
}

namespace lldb_private {

class ModuleListProperties : public Properties {
public:
  ModuleListProperties();

  FileSpec GetClangModulesCachePath() const;
  bool SetClangModulesCachePath(llvm::StringRef path);
  bool GetEnableExternalLookup() const;
}; 

//----------------------------------------------------------------------
/// @class ModuleList ModuleList.h "lldb/Core/ModuleList.h"
/// A collection class for Module objects.
///
/// Modules in the module collection class are stored as reference counted
/// shared pointers to Module objects.
//----------------------------------------------------------------------
class ModuleList {
public:
  class Notifier {
  public:
    virtual ~Notifier() = default;

    virtual void ModuleAdded(const ModuleList &module_list,
                             const lldb::ModuleSP &module_sp) = 0;
    virtual void ModuleRemoved(const ModuleList &module_list,
                               const lldb::ModuleSP &module_sp) = 0;
    virtual void ModuleUpdated(const ModuleList &module_list,
                               const lldb::ModuleSP &old_module_sp,
                               const lldb::ModuleSP &new_module_sp) = 0;
    virtual void WillClearList(const ModuleList &module_list) = 0;
  };

  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Creates an empty list of Module objects.
  //------------------------------------------------------------------
  ModuleList();

  //------------------------------------------------------------------
  /// Copy Constructor.
  ///
  /// Creates a new module list object with a copy of the modules from \a rhs.
  ///
  /// @param[in] rhs
  ///     Another module list object.
  //------------------------------------------------------------------
  ModuleList(const ModuleList &rhs);

  ModuleList(ModuleList::Notifier *notifier);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~ModuleList();

  //------------------------------------------------------------------
  /// Assignment operator.
  ///
  /// Copies the module list from \a rhs into this list.
  ///
  /// @param[in] rhs
  ///     Another module list object.
  ///
  /// @return
  ///     A const reference to this object.
  //------------------------------------------------------------------
  const ModuleList &operator=(const ModuleList &rhs);

  //------------------------------------------------------------------
  /// Append a module to the module list.
  ///
  /// Appends the module to the collection.
  ///
  /// @param[in] module_sp
  ///     A shared pointer to a module to add to this collection.
  //------------------------------------------------------------------
  void Append(const lldb::ModuleSP &module_sp);

  //------------------------------------------------------------------
  /// Append a module to the module list and remove any equivalent modules.
  /// Equivalent modules are ones whose file, platform file and architecture
  /// matches.
  ///
  /// Replaces the module to the collection.
  ///
  /// @param[in] module_sp
  ///     A shared pointer to a module to replace in this collection.
  //------------------------------------------------------------------
  void ReplaceEquivalent(const lldb::ModuleSP &module_sp);

  bool AppendIfNeeded(const lldb::ModuleSP &module_sp);

  void Append(const ModuleList &module_list);

  bool AppendIfNeeded(const ModuleList &module_list);

  bool ReplaceModule(const lldb::ModuleSP &old_module_sp,
                     const lldb::ModuleSP &new_module_sp);

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Clears the list of modules and releases a reference to each module
  /// object and if the reference count goes to zero, the module will be
  /// deleted.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Clears the list of modules and releases a reference to each module
  /// object and if the reference count goes to zero, the module will be
  /// deleted. Also release all memory that might be held by any collection
  /// classes (like std::vector)
  //------------------------------------------------------------------
  void Destroy();

  //------------------------------------------------------------------
  /// Dump the description of each module contained in this list.
  ///
  /// Dump the description of each module contained in this list to the
  /// supplied stream \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @see Module::Dump(Stream *) const
  //------------------------------------------------------------------
  void Dump(Stream *s) const;

  void LogUUIDAndPaths(Log *log, const char *prefix_cstr);

  std::recursive_mutex &GetMutex() const { return m_modules_mutex; }

  size_t GetIndexForModule(const Module *module) const;

  //------------------------------------------------------------------
  /// Get the module shared pointer for the module at index \a idx.
  ///
  /// @param[in] idx
  ///     An index into this module collection.
  ///
  /// @return
  ///     A shared pointer to a Module which can contain NULL if
  ///     \a idx is out of range.
  ///
  /// @see ModuleList::GetSize()
  //------------------------------------------------------------------
  lldb::ModuleSP GetModuleAtIndex(size_t idx) const;

  //------------------------------------------------------------------
  /// Get the module shared pointer for the module at index \a idx without
  /// acquiring the ModuleList mutex.  This MUST already have been acquired
  /// with ModuleList::GetMutex and locked for this call to be safe.
  ///
  /// @param[in] idx
  ///     An index into this module collection.
  ///
  /// @return
  ///     A shared pointer to a Module which can contain NULL if
  ///     \a idx is out of range.
  ///
  /// @see ModuleList::GetSize()
  //------------------------------------------------------------------
  lldb::ModuleSP GetModuleAtIndexUnlocked(size_t idx) const;

  //------------------------------------------------------------------
  /// Get the module pointer for the module at index \a idx.
  ///
  /// @param[in] idx
  ///     An index into this module collection.
  ///
  /// @return
  ///     A pointer to a Module which can by nullptr if \a idx is out
  ///     of range.
  ///
  /// @see ModuleList::GetSize()
  //------------------------------------------------------------------
  Module *GetModulePointerAtIndex(size_t idx) const;

  //------------------------------------------------------------------
  /// Get the module pointer for the module at index \a idx without acquiring
  /// the ModuleList mutex.  This MUST already have been acquired with
  /// ModuleList::GetMutex and locked for this call to be safe.
  ///
  /// @param[in] idx
  ///     An index into this module collection.
  ///
  /// @return
  ///     A pointer to a Module which can by nullptr if \a idx is out
  ///     of range.
  ///
  /// @see ModuleList::GetSize()
  //------------------------------------------------------------------
  Module *GetModulePointerAtIndexUnlocked(size_t idx) const;

  //------------------------------------------------------------------
  /// Find compile units by partial or full path.
  ///
  /// Finds all compile units that match \a path in all of the modules and
  /// returns the results in \a sc_list.
  ///
  /// @param[in] path
  ///     The name of the compile unit we are looking for.
  ///
  /// @param[in] append
  ///     If \b true, then append any compile units that were found
  ///     to \a sc_list. If \b false, then the \a sc_list is cleared
  ///     and the contents of \a sc_list are replaced.
  ///
  /// @param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  ///
  /// @return
  ///     The number of matches added to \a sc_list.
  //------------------------------------------------------------------
  size_t FindCompileUnits(const FileSpec &path, bool append,
                          SymbolContextList &sc_list) const;

  //------------------------------------------------------------------
  /// @see Module::FindFunctions ()
  //------------------------------------------------------------------
  size_t FindFunctions(const ConstString &name,
                       lldb::FunctionNameType name_type_mask,
                       bool include_symbols, bool include_inlines, bool append,
                       SymbolContextList &sc_list) const;

  //------------------------------------------------------------------
  /// @see Module::FindFunctionSymbols ()
  //------------------------------------------------------------------
  size_t FindFunctionSymbols(const ConstString &name,
                             lldb::FunctionNameType name_type_mask,
                             SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// @see Module::FindFunctions ()
  //------------------------------------------------------------------
  size_t FindFunctions(const RegularExpression &name, bool include_symbols,
                       bool include_inlines, bool append,
                       SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Find global and static variables by name.
  ///
  /// @param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// @param[in] variable_list
  ///     A list of variables that gets the matches appended to.
  ///
  /// @return
  ///     The number of matches added to \a variable_list.
  //------------------------------------------------------------------
  size_t FindGlobalVariables(const ConstString &name, size_t max_matches,
                             VariableList &variable_list) const;

  //------------------------------------------------------------------
  /// Find global and static variables by regular expression.
  ///
  /// @param[in] regex
  ///     A regular expression to use when matching the name.
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// @param[in] variable_list
  ///     A list of variables that gets the matches appended to.
  ///
  /// @return
  ///     The number of matches added to \a variable_list.
  //------------------------------------------------------------------
  size_t FindGlobalVariables(const RegularExpression &regex, size_t max_matches,
                             VariableList &variable_list) const;

  //------------------------------------------------------------------
  /// Finds the first module whose file specification matches \a file_spec.
  ///
  /// @param[in] file_spec_ptr
  ///     A file specification object to match against the Module's
  ///     file specifications. If \a file_spec does not have
  ///     directory information, matches will occur by matching only
  ///     the basename of any modules in this list. If this value is
  ///     NULL, then file specifications won't be compared when
  ///     searching for matching modules.
  ///
  /// @param[in] arch_ptr
  ///     The architecture to search for if non-NULL. If this value
  ///     is NULL no architecture matching will be performed.
  ///
  /// @param[in] uuid_ptr
  ///     The uuid to search for if non-NULL. If this value is NULL
  ///     no uuid matching will be performed.
  ///
  /// @param[in] object_name
  ///     An optional object name that must match as well. This value
  ///     can be NULL.
  ///
  /// @param[out] matching_module_list
  ///     A module list that gets filled in with any modules that
  ///     match the search criteria.
  ///
  /// @return
  ///     The number of matching modules found by the search.
  //------------------------------------------------------------------
  size_t FindModules(const ModuleSpec &module_spec,
                     ModuleList &matching_module_list) const;

  lldb::ModuleSP FindModule(const Module *module_ptr) const;

  //------------------------------------------------------------------
  // Find a module by UUID
  //
  // The UUID value for a module is extracted from the ObjectFile and is the
  // MD5 checksum, or a smarter object file equivalent, so finding modules by
  // UUID values is very efficient and accurate.
  //------------------------------------------------------------------
  lldb::ModuleSP FindModule(const UUID &uuid) const;

  lldb::ModuleSP FindFirstModule(const ModuleSpec &module_spec) const;

  size_t FindSymbolsWithNameAndType(const ConstString &name,
                                    lldb::SymbolType symbol_type,
                                    SymbolContextList &sc_list,
                                    bool append = false) const;

  size_t FindSymbolsMatchingRegExAndType(const RegularExpression &regex,
                                         lldb::SymbolType symbol_type,
                                         SymbolContextList &sc_list,
                                         bool append = false) const;

  //------------------------------------------------------------------
  /// Find types by name.
  ///
  /// @param[in] search_first
  ///     If non-null, this module will be searched before any other
  ///     modules.
  ///
  /// @param[in] name
  ///     The name of the type we are looking for.
  ///
  /// @param[in] append
  ///     If \b true, any matches will be appended to \a
  ///     variable_list, else matches replace the contents of
  ///     \a variable_list.
  ///
  /// @param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// @param[in] encoding
  ///     Limit the search to specific types, or get all types if
  ///     set to Type::invalid.
  ///
  /// @param[in] udt_name
  ///     If the encoding is a user defined type, specify the name
  ///     of the user defined type ("struct", "union", "class", etc).
  ///
  /// @param[out] type_list
  ///     A type list gets populated with any matches.
  ///
  /// @return
  ///     The number of matches added to \a type_list.
  //------------------------------------------------------------------
  size_t FindTypes(Module *search_first, const ConstString &name,
                   bool name_is_fully_qualified, size_t max_matches,
                   llvm::DenseSet<SymbolFile *> &searched_symbol_files,
                   TypeList &types) const;

  bool FindSourceFile(const FileSpec &orig_spec, FileSpec &new_spec) const;

  //------------------------------------------------------------------
  /// Find addresses by file/line
  ///
  /// @param[in] target_sp
  ///     The target the addresses are desired for.
  ///
  /// @param[in] file
  ///     Source file to locate.
  ///
  /// @param[in] line
  ///     Source line to locate.
  ///
  /// @param[in] function
  ///     Optional filter function. Addresses within this function will be
  ///     added to the 'local' list. All others will be added to the 'extern'
  ///     list.
  ///
  /// @param[out] output_local
  ///     All matching addresses within 'function'
  ///
  /// @param[out] output_extern
  ///     All matching addresses not within 'function'
  void FindAddressesForLine(const lldb::TargetSP target_sp,
                            const FileSpec &file, uint32_t line,
                            Function *function,
                            std::vector<Address> &output_local,
                            std::vector<Address> &output_extern);

  bool Remove(const lldb::ModuleSP &module_sp);

  size_t Remove(ModuleList &module_list);

  bool RemoveIfOrphaned(const Module *module_ptr);

  size_t RemoveOrphans(bool mandatory);

  bool ResolveFileAddress(lldb::addr_t vm_addr, Address &so_addr) const;

  //------------------------------------------------------------------
  /// @copydoc Module::ResolveSymbolContextForAddress (const Address
  /// &,uint32_t,SymbolContext&)
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextForAddress(const Address &so_addr,
                                          lldb::SymbolContextItem resolve_scope,
                                          SymbolContext &sc) const;

  //------------------------------------------------------------------
  /// @copydoc Module::ResolveSymbolContextForFilePath (const char
  /// *,uint32_t,bool,uint32_t,SymbolContextList&)
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextForFilePath(
      const char *file_path, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) const;

  //------------------------------------------------------------------
  /// @copydoc Module::ResolveSymbolContextsForFileSpec (const FileSpec
  /// &,uint32_t,bool,uint32_t,SymbolContextList&)
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextsForFileSpec(
      const FileSpec &file_spec, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) const;

  //------------------------------------------------------------------
  /// Gets the size of the module list.
  ///
  /// @return
  ///     The number of modules in the module list.
  //------------------------------------------------------------------
  size_t GetSize() const;

  bool LoadScriptingResourcesInTarget(Target *target, std::list<Status> &errors,
                                      Stream *feedback_stream = nullptr,
                                      bool continue_on_error = true);

  static ModuleListProperties &GetGlobalModuleListProperties();

  static bool ModuleIsInCache(const Module *module_ptr);

  static Status GetSharedModule(const ModuleSpec &module_spec,
                                lldb::ModuleSP &module_sp,
                                const FileSpecList *module_search_paths_ptr,
                                lldb::ModuleSP *old_module_sp_ptr,
                                bool *did_create_ptr,
                                bool always_create = false);

  static bool RemoveSharedModule(lldb::ModuleSP &module_sp);

  static size_t FindSharedModules(const ModuleSpec &module_spec,
                                  ModuleList &matching_module_list);

  static size_t RemoveOrphanSharedModules(bool mandatory);

  static bool RemoveSharedModuleIfOrphaned(const Module *module_ptr);
  
  void ForEach(std::function<bool(const lldb::ModuleSP &module_sp)> const
                   &callback) const;

protected:
  //------------------------------------------------------------------
  // Class typedefs.
  //------------------------------------------------------------------
  typedef std::vector<lldb::ModuleSP>
      collection; ///< The module collection type.

  void AppendImpl(const lldb::ModuleSP &module_sp, bool use_notifier = true);

  bool RemoveImpl(const lldb::ModuleSP &module_sp, bool use_notifier = true);

  collection::iterator RemoveImpl(collection::iterator pos,
                                  bool use_notifier = true);

  void ClearImpl(bool use_notifier = true);

  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  collection m_modules; ///< The collection of modules.
  mutable std::recursive_mutex m_modules_mutex;

  Notifier *m_notifier;

public:
  typedef LockingAdaptedIterable<collection, lldb::ModuleSP, vector_adapter,
                                 std::recursive_mutex>
      ModuleIterable;
  ModuleIterable Modules() { return ModuleIterable(m_modules, GetMutex()); }

  typedef AdaptedIterable<collection, lldb::ModuleSP, vector_adapter>
      ModuleIterableNoLocking;
  ModuleIterableNoLocking ModulesNoLocking() {
    return ModuleIterableNoLocking(m_modules);
  }
};

} // namespace lldb_private

#endif // liblldb_ModuleList_h_
