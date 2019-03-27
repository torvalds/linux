//===-- Module.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Module_h_
#define liblldb_Module_h_

#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/PathMappingList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace lldb_private {
class CompilerDeclContext;
}
namespace lldb_private {
class Function;
}
namespace lldb_private {
class Log;
}
namespace lldb_private {
class ObjectFile;
}
namespace lldb_private {
class RegularExpression;
}
namespace lldb_private {
class SectionList;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Symbol;
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
class SymbolVendor;
}
namespace lldb_private {
class Symtab;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
class TypeList;
}
namespace lldb_private {
class TypeMap;
}
namespace lldb_private {
class VariableList;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Module Module.h "lldb/Core/Module.h"
/// A class that describes an executable image and its associated
///        object and symbol files.
///
/// The module is designed to be able to select a single slice of an
/// executable image as it would appear on disk and during program execution.
///
/// Modules control when and if information is parsed according to which
/// accessors are called. For example the object file (ObjectFile)
/// representation will only be parsed if the object file is requested using
/// the Module::GetObjectFile() is called. The debug symbols will only be
/// parsed if the symbol vendor (SymbolVendor) is requested using the
/// Module::GetSymbolVendor() is called.
///
/// The module will parse more detailed information as more queries are made.
//----------------------------------------------------------------------
class Module : public std::enable_shared_from_this<Module>,
               public SymbolContextScope {
public:
  // Static functions that can track the lifetime of module objects. This is
  // handy because we might have Module objects that are in shared pointers
  // that aren't in the global module list (from ModuleList). If this is the
  // case we need to know about it. The modules in the global list maintained
  // by these functions can be viewed using the "target modules list" command
  // using the "--global" (-g for short).
  static size_t GetNumberAllocatedModules();

  static Module *GetAllocatedModuleAtIndex(size_t idx);

  static std::recursive_mutex &GetAllocationModuleCollectionMutex();

  //------------------------------------------------------------------
  /// Construct with file specification and architecture.
  ///
  /// Clients that wish to share modules with other targets should use
  /// ModuleList::GetSharedModule().
  ///
  /// @param[in] file_spec
  ///     The file specification for the on disk representation of
  ///     this executable image.
  ///
  /// @param[in] arch
  ///     The architecture to set as the current architecture in
  ///     this module.
  ///
  /// @param[in] object_name
  ///     The name of an object in a module used to extract a module
  ///     within a module (.a files and modules that contain multiple
  ///     architectures).
  ///
  /// @param[in] object_offset
  ///     The offset within an existing module used to extract a
  ///     module within a module (.a files and modules that contain
  ///     multiple architectures).
  //------------------------------------------------------------------
  Module(
      const FileSpec &file_spec, const ArchSpec &arch,
      const ConstString *object_name = nullptr,
      lldb::offset_t object_offset = 0,
      const llvm::sys::TimePoint<> &object_mod_time = llvm::sys::TimePoint<>());

  Module(const ModuleSpec &module_spec);

  template <typename ObjFilePlugin, typename... Args>
  static lldb::ModuleSP CreateModuleFromObjectFile(Args &&... args) {
    // Must create a module and place it into a shared pointer before we can
    // create an object file since it has a std::weak_ptr back to the module,
    // so we need to control the creation carefully in this static function
    lldb::ModuleSP module_sp(new Module());
    module_sp->m_objfile_sp =
        std::make_shared<ObjFilePlugin>(module_sp, std::forward<Args>(args)...);

    // Once we get the object file, update our module with the object file's
    // architecture since it might differ in vendor/os if some parts were
    // unknown.
    if (ArchSpec arch = module_sp->m_objfile_sp->GetArchitecture()) {
      module_sp->m_arch = arch;
      return module_sp;
    }
    return nullptr;
  }

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~Module() override;

  bool MatchesModuleSpec(const ModuleSpec &module_ref);

  //------------------------------------------------------------------
  /// Set the load address for all sections in a module to be the file address
  /// plus \a slide.
  ///
  /// Many times a module will be loaded in a target with a constant offset
  /// applied to all top level sections. This function can set the load
  /// address for all top level sections to be the section file address +
  /// offset.
  ///
  /// @param[in] target
  ///     The target in which to apply the section load addresses.
  ///
  /// @param[in] value
  ///     if \a value_is_offset is true, then value is the offset to
  ///     apply to all file addresses for all top level sections in
  ///     the object file as each section load address is being set.
  ///     If \a value_is_offset is false, then "value" is the new
  ///     absolute base address for the image.
  ///
  /// @param[in] value_is_offset
  ///     If \b true, then \a value is an offset to apply to each
  ///     file address of each top level section.
  ///     If \b false, then \a value is the image base address that
  ///     will be used to rigidly slide all loadable sections.
  ///
  /// @param[out] changed
  ///     If any section load addresses were changed in \a target,
  ///     then \a changed will be set to \b true. Else \a changed
  ///     will be set to false. This allows this function to be
  ///     called multiple times on the same module for the same
  ///     target. If the module hasn't moved, then \a changed will
  ///     be false and no module updated notification will need to
  ///     be sent out.
  ///
  /// @return
  ///     /b True if any sections were successfully loaded in \a target,
  ///     /b false otherwise.
  //------------------------------------------------------------------
  bool SetLoadAddress(Target &target, lldb::addr_t value, bool value_is_offset,
                      bool &changed);

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  void
  GetDescription(Stream *s,
                 lldb::DescriptionLevel level = lldb::eDescriptionLevelFull);

  //------------------------------------------------------------------
  /// Get the module path and object name.
  ///
  /// Modules can refer to object files. In this case the specification is
  /// simple and would return the path to the file:
  ///
  ///     "/usr/lib/foo.dylib"
  ///
  /// Modules can be .o files inside of a BSD archive (.a file). In this case,
  /// the object specification will look like:
  ///
  ///     "/usr/lib/foo.a(bar.o)"
  ///
  /// There are many places where logging wants to log this fully qualified
  /// specification, so we centralize this functionality here.
  ///
  /// @return
  ///     The object path + object name if there is one.
  //------------------------------------------------------------------
  std::string GetSpecificationDescription() const;

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s. The dumped content will be only what has been loaded or parsed up
  /// to this point at which this function is called, so this is a good way to
  /// see what has been parsed in a module.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //------------------------------------------------------------------
  void Dump(Stream *s);

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void DumpSymbolContext(Stream *s) override;

  //------------------------------------------------------------------
  /// Find a symbol in the object file's symbol table.
  ///
  /// @param[in] name
  ///     The name of the symbol that we are looking for.
  ///
  /// @param[in] symbol_type
  ///     If set to eSymbolTypeAny, find a symbol of any type that
  ///     has a name that matches \a name. If set to any other valid
  ///     SymbolType enumeration value, then search only for
  ///     symbols that match \a symbol_type.
  ///
  /// @return
  ///     Returns a valid symbol pointer if a symbol was found,
  ///     nullptr otherwise.
  //------------------------------------------------------------------
  const Symbol *FindFirstSymbolWithNameAndType(
      const ConstString &name,
      lldb::SymbolType symbol_type = lldb::eSymbolTypeAny);

  size_t FindSymbolsWithNameAndType(const ConstString &name,
                                    lldb::SymbolType symbol_type,
                                    SymbolContextList &sc_list);

  size_t FindSymbolsMatchingRegExAndType(const RegularExpression &regex,
                                         lldb::SymbolType symbol_type,
                                         SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Find a function symbols in the object file's symbol table.
  ///
  /// @param[in] name
  ///     The name of the symbol that we are looking for.
  ///
  /// @param[in] name_type_mask
  ///     A mask that has one or more bitwise OR'ed values from the
  ///     lldb::FunctionNameType enumeration type that indicate what
  ///     kind of names we are looking for.
  ///
  /// @param[out] sc_list
  ///     A list to append any matching symbol contexts to.
  ///
  /// @return
  ///     The number of symbol contexts that were added to \a sc_list
  //------------------------------------------------------------------
  size_t FindFunctionSymbols(const ConstString &name, uint32_t name_type_mask,
                             SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Find compile units by partial or full path.
  ///
  /// Finds all compile units that match \a path in all of the modules and
  /// returns the results in \a sc_list.
  ///
  /// @param[in] path
  ///     The name of the function we are looking for.
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
                          SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Find functions by name.
  ///
  /// If the function is an inlined function, it will have a block,
  /// representing the inlined function, and the function will be the
  /// containing function.  If it is not inlined, then the block will be NULL.
  ///
  /// @param[in] name
  ///     The name of the compile unit we are looking for.
  ///
  /// @param[in] namespace_decl
  ///     If valid, a namespace to search in.
  ///
  /// @param[in] name_type_mask
  ///     A bit mask of bits that indicate what kind of names should
  ///     be used when doing the lookup. Bits include fully qualified
  ///     names, base names, C++ methods, or ObjC selectors.
  ///     See FunctionNameType for more details.
  ///
  /// @param[in] append
  ///     If \b true, any matches will be appended to \a sc_list, else
  ///     matches replace the contents of \a sc_list.
  ///
  /// @param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  ///
  /// @return
  ///     The number of matches added to \a sc_list.
  //------------------------------------------------------------------
  size_t FindFunctions(const ConstString &name,
                       const CompilerDeclContext *parent_decl_ctx,
                       lldb::FunctionNameType name_type_mask, bool symbols_ok,
                       bool inlines_ok, bool append,
                       SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Find functions by name.
  ///
  /// If the function is an inlined function, it will have a block,
  /// representing the inlined function, and the function will be the
  /// containing function.  If it is not inlined, then the block will be NULL.
  ///
  /// @param[in] regex
  ///     A regular expression to use when matching the name.
  ///
  /// @param[in] append
  ///     If \b true, any matches will be appended to \a sc_list, else
  ///     matches replace the contents of \a sc_list.
  ///
  /// @param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  ///
  /// @return
  ///     The number of matches added to \a sc_list.
  //------------------------------------------------------------------
  size_t FindFunctions(const RegularExpression &regex, bool symbols_ok,
                       bool inlines_ok, bool append,
                       SymbolContextList &sc_list);

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
  ///	    Optional filter function. Addresses within this function will be
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

  //------------------------------------------------------------------
  /// Find global and static variables by name.
  ///
  /// @param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// @param[in] parent_decl_ctx
  ///     If valid, a decl context that results must exist within
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
  size_t FindGlobalVariables(const ConstString &name,
                             const CompilerDeclContext *parent_decl_ctx,
                             size_t max_matches, VariableList &variable_list);

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
                             VariableList &variable_list);

  //------------------------------------------------------------------
  /// Find types by name.
  ///
  /// Type lookups in modules go through the SymbolVendor (which will use one
  /// or more SymbolFile subclasses). The SymbolFile needs to be able to
  /// lookup types by basename and not the fully qualified typename. This
  /// allows the type accelerator tables to stay small, even with heavily
  /// templatized C++. The type search will then narrow down the search
  /// results. If "exact_match" is true, then the type search will only match
  /// exact type name matches. If "exact_match" is false, the type will match
  /// as long as the base typename matches and as long as any immediate
  /// containing namespaces/class scopes that are specified match. So to
  /// search for a type "d" in "b::c", the name "b::c::d" can be specified and
  /// it will match any class/namespace "b" which contains a class/namespace
  /// "c" which contains type "d". We do this to allow users to not always
  /// have to specify complete scoping on all expressions, but it also allows
  /// for exact matching when required.
  ///
  /// @param[in] type_name
  ///     The name of the type we are looking for that is a fully
  ///     or partially qualified type name.
  ///
  /// @param[in] exact_match
  ///     If \b true, \a type_name is fully qualified and must match
  ///     exactly. If \b false, \a type_name is a partially qualified
  ///     name where the leading namespaces or classes can be
  ///     omitted to make finding types that a user may type
  ///     easier.
  ///
  /// @param[out] type_list
  ///     A type list gets populated with any matches.
  ///
  /// @return
  ///     The number of matches added to \a type_list.
  //------------------------------------------------------------------
  size_t
  FindTypes(const ConstString &type_name, bool exact_match, size_t max_matches,
            llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
            TypeList &types);

  lldb::TypeSP FindFirstType(const SymbolContext &sc,
                             const ConstString &type_name, bool exact_match);

  //------------------------------------------------------------------
  /// Find types by name that are in a namespace. This function is used by the
  /// expression parser when searches need to happen in an exact namespace
  /// scope.
  ///
  /// @param[in] type_name
  ///     The name of a type within a namespace that should not include
  ///     any qualifying namespaces (just a type basename).
  ///
  /// @param[in] namespace_decl
  ///     The namespace declaration that this type must exist in.
  ///
  /// @param[out] type_list
  ///     A type list gets populated with any matches.
  ///
  /// @return
  ///     The number of matches added to \a type_list.
  //------------------------------------------------------------------
  size_t FindTypesInNamespace(const ConstString &type_name,
                              const CompilerDeclContext *parent_decl_ctx,
                              size_t max_matches, TypeList &type_list);

  //------------------------------------------------------------------
  /// Get const accessor for the module architecture.
  ///
  /// @return
  ///     A const reference to the architecture object.
  //------------------------------------------------------------------
  const ArchSpec &GetArchitecture() const;

  //------------------------------------------------------------------
  /// Get const accessor for the module file specification.
  ///
  /// This function returns the file for the module on the host system that is
  /// running LLDB. This can differ from the path on the platform since we
  /// might be doing remote debugging.
  ///
  /// @return
  ///     A const reference to the file specification object.
  //------------------------------------------------------------------
  const FileSpec &GetFileSpec() const { return m_file; }

  //------------------------------------------------------------------
  /// Get accessor for the module platform file specification.
  ///
  /// Platform file refers to the path of the module as it is known on the
  /// remote system on which it is being debugged. For local debugging this is
  /// always the same as Module::GetFileSpec(). But remote debugging might
  /// mention a file "/usr/lib/liba.dylib" which might be locally downloaded
  /// and cached. In this case the platform file could be something like:
  /// "/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib" The
  /// file could also be cached in a local developer kit directory.
  ///
  /// @return
  ///     A const reference to the file specification object.
  //------------------------------------------------------------------
  const FileSpec &GetPlatformFileSpec() const {
    if (m_platform_file)
      return m_platform_file;
    return m_file;
  }

  void SetPlatformFileSpec(const FileSpec &file) { m_platform_file = file; }

  const FileSpec &GetRemoteInstallFileSpec() const {
    return m_remote_install_file;
  }

  void SetRemoteInstallFileSpec(const FileSpec &file) {
    m_remote_install_file = file;
  }

  const FileSpec &GetSymbolFileFileSpec() const { return m_symfile_spec; }

  void PreloadSymbols();

  void SetSymbolFileFileSpec(const FileSpec &file);

  const llvm::sys::TimePoint<> &GetModificationTime() const {
    return m_mod_time;
  }

  const llvm::sys::TimePoint<> &GetObjectModificationTime() const {
    return m_object_mod_time;
  }

  void SetObjectModificationTime(const llvm::sys::TimePoint<> &mod_time) {
    m_mod_time = mod_time;
  }

  //------------------------------------------------------------------
  /// Tells whether this module is capable of being the main executable for a
  /// process.
  ///
  /// @return
  ///     \b true if it is, \b false otherwise.
  //------------------------------------------------------------------
  bool IsExecutable();

  //------------------------------------------------------------------
  /// Tells whether this module has been loaded in the target passed in. This
  /// call doesn't distinguish between whether the module is loaded by the
  /// dynamic loader, or by a "target module add" type call.
  ///
  /// @param[in] target
  ///    The target to check whether this is loaded in.
  ///
  /// @return
  ///     \b true if it is, \b false otherwise.
  //------------------------------------------------------------------
  bool IsLoadedInTarget(Target *target);

  bool LoadScriptingResourceInTarget(Target *target, Status &error,
                                     Stream *feedback_stream = nullptr);

  //------------------------------------------------------------------
  /// Get the number of compile units for this module.
  ///
  /// @return
  ///     The number of compile units that the symbol vendor plug-in
  ///     finds.
  //------------------------------------------------------------------
  size_t GetNumCompileUnits();

  lldb::CompUnitSP GetCompileUnitAtIndex(size_t idx);

  const ConstString &GetObjectName() const;

  uint64_t GetObjectOffset() const { return m_object_offset; }

  //------------------------------------------------------------------
  /// Get the object file representation for the current architecture.
  ///
  /// If the object file has not been located or parsed yet, this function
  /// will find the best ObjectFile plug-in that can parse Module::m_file.
  ///
  /// @return
  ///     If Module::m_file does not exist, or no plug-in was found
  ///     that can parse the file, or the object file doesn't contain
  ///     the current architecture in Module::m_arch, nullptr will be
  ///     returned, else a valid object file interface will be
  ///     returned. The returned pointer is owned by this object and
  ///     remains valid as long as the object is around.
  //------------------------------------------------------------------
  virtual ObjectFile *GetObjectFile();

  //------------------------------------------------------------------
  /// Get the unified section list for the module. This is the section list
  /// created by the module's object file and any debug info and symbol files
  /// created by the symbol vendor.
  ///
  /// If the symbol vendor has not been loaded yet, this function will return
  /// the section list for the object file.
  ///
  /// @return
  ///     Unified module section list.
  //------------------------------------------------------------------
  virtual SectionList *GetSectionList();

  //------------------------------------------------------------------
  /// Notify the module that the file addresses for the Sections have been
  /// updated.
  ///
  /// If the Section file addresses for a module are updated, this method
  /// should be called.  Any parts of the module, object file, or symbol file
  /// that has cached those file addresses must invalidate or update its
  /// cache.
  //------------------------------------------------------------------
  virtual void SectionFileAddressesChanged();

  llvm::VersionTuple GetVersion();

  //------------------------------------------------------------------
  /// Load an object file from memory.
  ///
  /// If available, the size of the object file in memory may be passed to
  /// avoid additional round trips to process memory. If the size is not
  /// provided, a default value is used. This value should be large enough to
  /// enable the ObjectFile plugins to read the header of the object file
  /// without going back to the process.
  ///
  /// @return
  ///     The object file loaded from memory or nullptr, if the operation
  ///     failed (see the `error` for more information in that case).
  //------------------------------------------------------------------
  ObjectFile *GetMemoryObjectFile(const lldb::ProcessSP &process_sp,
                                  lldb::addr_t header_addr, Status &error,
                                  size_t size_to_read = 512);
  //------------------------------------------------------------------
  /// Get the symbol vendor interface for the current architecture.
  ///
  /// If the symbol vendor file has not been located yet, this function will
  /// find the best SymbolVendor plug-in that can use the current object file.
  ///
  /// @return
  ///     If this module does not have a valid object file, or no
  ///     plug-in can be found that can use the object file, nullptr will
  ///     be returned, else a valid symbol vendor plug-in interface
  ///     will be returned. The returned pointer is owned by this
  ///     object and remains valid as long as the object is around.
  //------------------------------------------------------------------
  virtual SymbolVendor *
  GetSymbolVendor(bool can_create = true,
                  lldb_private::Stream *feedback_strm = nullptr);

  //------------------------------------------------------------------
  /// Get accessor the type list for this module.
  ///
  /// @return
  ///     A valid type list pointer, or nullptr if there is no valid
  ///     symbol vendor for this module.
  //------------------------------------------------------------------
  TypeList *GetTypeList();

  //------------------------------------------------------------------
  /// Get a reference to the UUID value contained in this object.
  ///
  /// If the executable image file doesn't not have a UUID value built into
  /// the file format, an MD5 checksum of the entire file, or slice of the
  /// file for the current architecture should be used.
  ///
  /// @return
  ///     A const pointer to the internal copy of the UUID value in
  ///     this module if this module has a valid UUID value, NULL
  ///     otherwise.
  //------------------------------------------------------------------
  const lldb_private::UUID &GetUUID();

  //------------------------------------------------------------------
  /// A debugging function that will cause everything in a module to
  /// be parsed.
  ///
  /// All compile units will be parsed, along with all globals and static
  /// variables and all functions for those compile units. All types, scopes,
  /// local variables, static variables, global variables, and line tables
  /// will be parsed. This can be used prior to dumping a module to see a
  /// complete list of the resulting debug information that gets parsed, or as
  /// a debug function to ensure that the module can consume all of the debug
  /// data the symbol vendor provides.
  //------------------------------------------------------------------
  void ParseAllDebugSymbols();

  bool ResolveFileAddress(lldb::addr_t vm_addr, Address &so_addr);

  //------------------------------------------------------------------
  /// Resolve the symbol context for the given address.
  ///
  /// Tries to resolve the matching symbol context based on a lookup from the
  /// current symbol vendor.  If the lazy lookup fails, an attempt is made to
  /// parse the eh_frame section to handle stripped symbols.  If this fails,
  /// an attempt is made to resolve the symbol to the previous address to
  /// handle the case of a function with a tail call.
  ///
  /// Use properties of the modified SymbolContext to inspect any resolved
  /// target, module, compilation unit, symbol, function, function block or
  /// line entry.  Use the return value to determine which of these properties
  /// have been modified.
  ///
  /// @param[in] so_addr
  ///     A load address to resolve.
  ///
  /// @param[in] resolve_scope
  ///     The scope that should be resolved (see SymbolContext::Scope).
  ///     A combination of flags from the enumeration SymbolContextItem
  ///     requesting a resolution depth.  Note that the flags that are
  ///     actually resolved may be a superset of the requested flags.
  ///     For instance, eSymbolContextSymbol requires resolution of
  ///     eSymbolContextModule, and eSymbolContextFunction requires
  ///     eSymbolContextSymbol.
  ///
  /// @param[out] sc
  ///     The SymbolContext that is modified based on symbol resolution.
  ///
  /// @param[in] resolve_tail_call_address
  ///     Determines if so_addr should resolve to a symbol in the case
  ///     of a function whose last instruction is a call.  In this case,
  ///     the PC can be one past the address range of the function.
  ///
  /// @return
  ///     The scope that has been resolved (see SymbolContext::Scope).
  ///
  /// @see SymbolContext::Scope
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextForAddress(
      const Address &so_addr, lldb::SymbolContextItem resolve_scope,
      SymbolContext &sc, bool resolve_tail_call_address = false);

  //------------------------------------------------------------------
  /// Resolve items in the symbol context for a given file and line.
  ///
  /// Tries to resolve \a file_path and \a line to a list of matching symbol
  /// contexts.
  ///
  /// The line table entries contains addresses that can be used to further
  /// resolve the values in each match: the function, block, symbol. Care
  /// should be taken to minimize the amount of information that is requested
  /// to only what is needed -- typically the module, compile unit, line table
  /// and line table entry are sufficient.
  ///
  /// @param[in] file_path
  ///     A path to a source file to match. If \a file_path does not
  ///     specify a directory, then this query will match all files
  ///     whose base filename matches. If \a file_path does specify
  ///     a directory, the fullpath to the file must match.
  ///
  /// @param[in] line
  ///     The source line to match, or zero if just the compile unit
  ///     should be resolved.
  ///
  /// @param[in] check_inlines
  ///     Check for inline file and line number matches. This option
  ///     should be used sparingly as it will cause all line tables
  ///     for every compile unit to be parsed and searched for
  ///     matching inline file entries.
  ///
  /// @param[in] resolve_scope
  ///     The scope that should be resolved (see
  ///     SymbolContext::Scope).
  ///
  /// @param[out] sc_list
  ///     A symbol context list that gets matching symbols contexts
  ///     appended to.
  ///
  /// @return
  ///     The number of matches that were added to \a sc_list.
  ///
  /// @see SymbolContext::Scope
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextForFilePath(
      const char *file_path, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Resolve items in the symbol context for a given file and line.
  ///
  /// Tries to resolve \a file_spec and \a line to a list of matching symbol
  /// contexts.
  ///
  /// The line table entries contains addresses that can be used to further
  /// resolve the values in each match: the function, block, symbol. Care
  /// should be taken to minimize the amount of information that is requested
  /// to only what is needed -- typically the module, compile unit, line table
  /// and line table entry are sufficient.
  ///
  /// @param[in] file_spec
  ///     A file spec to a source file to match. If \a file_path does
  ///     not specify a directory, then this query will match all
  ///     files whose base filename matches. If \a file_path does
  ///     specify a directory, the fullpath to the file must match.
  ///
  /// @param[in] line
  ///     The source line to match, or zero if just the compile unit
  ///     should be resolved.
  ///
  /// @param[in] check_inlines
  ///     Check for inline file and line number matches. This option
  ///     should be used sparingly as it will cause all line tables
  ///     for every compile unit to be parsed and searched for
  ///     matching inline file entries.
  ///
  /// @param[in] resolve_scope
  ///     The scope that should be resolved (see
  ///     SymbolContext::Scope).
  ///
  /// @param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  ///
  /// @return
  ///     A integer that contains SymbolContext::Scope bits set for
  ///     each item that was successfully resolved.
  ///
  /// @see SymbolContext::Scope
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextsForFileSpec(
      const FileSpec &file_spec, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list);

  void SetFileSpecAndObjectName(const FileSpec &file,
                                const ConstString &object_name);

  bool GetIsDynamicLinkEditor();

  TypeSystem *GetTypeSystemForLanguage(lldb::LanguageType language);

  // Special error functions that can do printf style formatting that will
  // prepend the message with something appropriate for this module (like the
  // architecture, path and object name (if any)). This centralizes code so
  // that everyone doesn't need to format their error and log messages on their
  // own and keeps the output a bit more consistent.
  void LogMessage(Log *log, const char *format, ...)
      __attribute__((format(printf, 3, 4)));

  void LogMessageVerboseBacktrace(Log *log, const char *format, ...)
      __attribute__((format(printf, 3, 4)));

  void ReportWarning(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  void ReportError(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  // Only report an error once when the module is first detected to be modified
  // so we don't spam the console with many messages.
  void ReportErrorIfModifyDetected(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  //------------------------------------------------------------------
  // Return true if the file backing this module has changed since the module
  // was originally created  since we saved the initial file modification time
  // when the module first gets created.
  //------------------------------------------------------------------
  bool FileHasChanged() const;

  //------------------------------------------------------------------
  // SymbolVendor, SymbolFile and ObjectFile member objects should lock the
  // module mutex to avoid deadlocks.
  //------------------------------------------------------------------
  std::recursive_mutex &GetMutex() const { return m_mutex; }

  PathMappingList &GetSourceMappingList() { return m_source_mappings; }

  const PathMappingList &GetSourceMappingList() const {
    return m_source_mappings;
  }

  //------------------------------------------------------------------
  /// Finds a source file given a file spec using the module source path
  /// remappings (if any).
  ///
  /// Tries to resolve \a orig_spec by checking the module source path
  /// remappings. It makes sure the file exists, so this call can be expensive
  /// if the remappings are on a network file system, so use this function
  /// sparingly (not in a tight debug info parsing loop).
  ///
  /// @param[in] orig_spec
  ///     The original source file path to try and remap.
  ///
  /// @param[out] new_spec
  ///     The newly remapped filespec that is guaranteed to exist.
  ///
  /// @return
  ///     /b true if \a orig_spec was successfully located and
  ///     \a new_spec is filled in with an existing file spec,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool FindSourceFile(const FileSpec &orig_spec, FileSpec &new_spec) const;

  //------------------------------------------------------------------
  /// Remaps a source file given \a path into \a new_path.
  ///
  /// Remaps \a path if any source remappings match. This function does NOT
  /// stat the file system so it can be used in tight loops where debug info
  /// is being parsed.
  ///
  /// @param[in] path
  ///     The original source file path to try and remap.
  ///
  /// @param[out] new_path
  ///     The newly remapped filespec that is may or may not exist.
  ///
  /// @return
  ///     /b true if \a path was successfully located and \a new_path
  ///     is filled in with a new source path, \b false otherwise.
  //------------------------------------------------------------------
  bool RemapSourceFile(llvm::StringRef path, std::string &new_path) const;
  bool RemapSourceFile(const char *, std::string &) const = delete;

  //----------------------------------------------------------------------
  /// @class LookupInfo Module.h "lldb/Core/Module.h"
  /// A class that encapsulates name lookup information.
  ///
  /// Users can type a wide variety of partial names when setting breakpoints
  /// by name or when looking for functions by name. SymbolVendor and
  /// SymbolFile objects are only required to implement name lookup for
  /// function basenames and for fully mangled names. This means if the user
  /// types in a partial name, we must reduce this to a name lookup that will
  /// work with all SymbolFile objects. So we might reduce a name lookup to
  /// look for a basename, and then prune out any results that don't match.
  ///
  /// The "m_name" member variable represents the name as it was typed by the
  /// user. "m_lookup_name" will be the name we actually search for through
  /// the symbol or objects files. Lanaguage is included in case we need to
  /// filter results by language at a later date. The "m_name_type_mask"
  /// member variable tells us what kinds of names we are looking for and can
  /// help us prune out unwanted results.
  ///
  /// Function lookups are done in Module.cpp, ModuleList.cpp and in
  /// BreakpointResolverName.cpp and they all now use this class to do lookups
  /// correctly.
  //----------------------------------------------------------------------
  class LookupInfo {
  public:
    LookupInfo()
        : m_name(), m_lookup_name(), m_language(lldb::eLanguageTypeUnknown),
          m_name_type_mask(lldb::eFunctionNameTypeNone),
          m_match_name_after_lookup(false) {}

    LookupInfo(const ConstString &name, lldb::FunctionNameType name_type_mask,
               lldb::LanguageType language);

    const ConstString &GetName() const { return m_name; }

    void SetName(const ConstString &name) { m_name = name; }

    const ConstString &GetLookupName() const { return m_lookup_name; }

    void SetLookupName(const ConstString &name) { m_lookup_name = name; }

    lldb::FunctionNameType GetNameTypeMask() const { return m_name_type_mask; }

    void SetNameTypeMask(lldb::FunctionNameType mask) {
      m_name_type_mask = mask;
    }

    void Prune(SymbolContextList &sc_list, size_t start_idx) const;

  protected:
    /// What the user originally typed
    ConstString m_name;

    /// The actual name will lookup when calling in the object or symbol file
    ConstString m_lookup_name;

    /// Limit matches to only be for this language
    lldb::LanguageType m_language;

    /// One or more bits from lldb::FunctionNameType that indicate what kind of
    /// names we are looking for
    lldb::FunctionNameType m_name_type_mask;

    ///< If \b true, then demangled names that match will need to contain
    ///< "m_name" in order to be considered a match
    bool m_match_name_after_lookup;
  };

protected:
  //------------------------------------------------------------------
  // Member Variables
  //------------------------------------------------------------------
  mutable std::recursive_mutex m_mutex; ///< A mutex to keep this object happy
                                        ///in multi-threaded environments.

  /// The modification time for this module when it was created.
  llvm::sys::TimePoint<> m_mod_time;

  ArchSpec m_arch;      ///< The architecture for this module.
  UUID m_uuid; ///< Each module is assumed to have a unique identifier to help
               ///match it up to debug symbols.
  FileSpec m_file; ///< The file representation on disk for this module (if
                   ///there is one).
  FileSpec m_platform_file; ///< The path to the module on the platform on which
                            ///it is being debugged
  FileSpec m_remote_install_file; ///< If set when debugging on remote
                                  ///platforms, this module will be installed at
                                  ///this location
  FileSpec m_symfile_spec;   ///< If this path is valid, then this is the file
                             ///that _will_ be used as the symbol file for this
                             ///module
  ConstString m_object_name; ///< The name an object within this module that is
                             ///selected, or empty of the module is represented
                             ///by \a m_file.
  uint64_t m_object_offset;
  llvm::sys::TimePoint<> m_object_mod_time;
  lldb::ObjectFileSP m_objfile_sp; ///< A shared pointer to the object file
                                   ///parser for this module as it may or may
                                   ///not be shared with the SymbolFile
  lldb::SymbolVendorUP
      m_symfile_ap; ///< A pointer to the symbol vendor for this module.
  std::vector<lldb::SymbolVendorUP>
      m_old_symfiles; ///< If anyone calls Module::SetSymbolFileFileSpec() and
                      ///changes the symbol file,
  ///< we need to keep all old symbol files around in case anyone has type
  ///references to them
  TypeSystemMap m_type_system_map;   ///< A map of any type systems associated
                                     ///with this module
  PathMappingList m_source_mappings; ///< Module specific source remappings for
                                     ///when you have debug info for a module
                                     ///that doesn't match where the sources
                                     ///currently are
  lldb::SectionListUP m_sections_ap; ///< Unified section list for module that
                                     ///is used by the ObjectFile and and
                                     ///ObjectFile instances for the debug info

  std::atomic<bool> m_did_load_objfile{false};
  std::atomic<bool> m_did_load_symbol_vendor{false};
  std::atomic<bool> m_did_set_uuid{false};
  mutable bool m_file_has_changed : 1,
      m_first_file_changed_log : 1; /// See if the module was modified after it
                                    /// was initially opened.

  //------------------------------------------------------------------
  /// Resolve a file or load virtual address.
  ///
  /// Tries to resolve \a vm_addr as a file address (if \a
  /// vm_addr_is_file_addr is true) or as a load address if \a
  /// vm_addr_is_file_addr is false) in the symbol vendor. \a resolve_scope
  /// indicates what clients wish to resolve and can be used to limit the
  /// scope of what is parsed.
  ///
  /// @param[in] vm_addr
  ///     The load virtual address to resolve.
  ///
  /// @param[in] vm_addr_is_file_addr
  ///     If \b true, \a vm_addr is a file address, else \a vm_addr
  ///     if a load address.
  ///
  /// @param[in] resolve_scope
  ///     The scope that should be resolved (see
  ///     SymbolContext::Scope).
  ///
  /// @param[out] so_addr
  ///     The section offset based address that got resolved if
  ///     any bits are returned.
  ///
  /// @param[out] sc
  //      The symbol context that has objects filled in. Each bit
  ///     in the \a resolve_scope pertains to a member in the \a sc.
  ///
  /// @return
  ///     A integer that contains SymbolContext::Scope bits set for
  ///     each item that was successfully resolved.
  ///
  /// @see SymbolContext::Scope
  //------------------------------------------------------------------
  uint32_t ResolveSymbolContextForAddress(lldb::addr_t vm_addr,
                                          bool vm_addr_is_file_addr,
                                          lldb::SymbolContextItem resolve_scope,
                                          Address &so_addr, SymbolContext &sc);

  void SymbolIndicesToSymbolContextList(Symtab *symtab,
                                        std::vector<uint32_t> &symbol_indexes,
                                        SymbolContextList &sc_list);

  bool SetArchitecture(const ArchSpec &new_arch);

  void SetUUID(const lldb_private::UUID &uuid);

  SectionList *GetUnifiedSectionList();

  friend class ModuleList;
  friend class ObjectFile;
  friend class SymbolFile;

private:
  Module(); // Only used internally by CreateJITModule ()

  size_t FindTypes_Impl(
      const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
      bool append, size_t max_matches,
      llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
      TypeMap &types);

  DISALLOW_COPY_AND_ASSIGN(Module);
};

} // namespace lldb_private

#endif // liblldb_Module_h_
