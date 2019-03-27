//===-- ClangModulesDeclVendor.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangModulesDeclVendor_h
#define liblldb_ClangModulesDeclVendor_h

#include "lldb/Core/ClangForward.h"
#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Target/Platform.h"

#include <set>
#include <vector>

namespace lldb_private {

class ClangModulesDeclVendor : public DeclVendor {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ClangModulesDeclVendor();

  ~ClangModulesDeclVendor() override;

  static ClangModulesDeclVendor *Create(Target &target);

  typedef std::vector<ConstString> ModulePath;
  typedef uintptr_t ModuleID;
  typedef std::vector<ModuleID> ModuleVector;

  //------------------------------------------------------------------
  /// Add a module to the list of modules to search.
  ///
  /// @param[in] path
  ///     The path to the exact module to be loaded.  E.g., if the desired
  ///     module is std.io, then this should be { "std", "io" }.
  ///
  /// @param[in] exported_modules
  ///     If non-NULL, a pointer to a vector to populate with the ID of every
  ///     module that is re-exported by the specified module.
  ///
  /// @param[in] error_stream
  ///     A stream to populate with the output of the Clang parser when
  ///     it tries to load the module.
  ///
  /// @return
  ///     True if the module could be loaded; false if not.  If the
  ///     compiler encountered a fatal error during a previous module
  ///     load, then this will always return false for this ModuleImporter.
  //------------------------------------------------------------------
  virtual bool AddModule(ModulePath &path, ModuleVector *exported_modules,
                         Stream &error_stream) = 0;

  //------------------------------------------------------------------
  /// Add all modules referred to in a given compilation unit to the list
  /// of modules to search.
  ///
  /// @param[in] cu
  ///     The compilation unit to scan for imported modules.
  ///
  /// @param[in] exported_modules
  ///     A vector to populate with the ID of each module loaded (directly
  ///     and via re-exports) in this way.
  ///
  /// @param[in] error_stream
  ///     A stream to populate with the output of the Clang parser when
  ///     it tries to load the modules.
  ///
  /// @return
  ///     True if all modules referred to by the compilation unit could be
  ///     loaded; false if one could not be loaded.  If the compiler
  ///     encountered a fatal error during a previous module
  ///     load, then this will always return false for this ModuleImporter.
  //------------------------------------------------------------------
  virtual bool AddModulesForCompileUnit(CompileUnit &cu,
                                        ModuleVector &exported_modules,
                                        Stream &error_stream) = 0;

  //------------------------------------------------------------------
  /// Enumerate all the macros that are defined by a given set of modules
  /// that are already imported.
  ///
  /// @param[in] modules
  ///     The unique IDs for all modules to query.  Later modules have higher
  ///     priority, just as if you @imported them in that order.  This matters
  ///     if module A #defines a macro and module B #undefs it.
  ///
  /// @param[in] handler
  ///     A function to call with the text of each #define (including the
  ///     #define directive).  #undef directives are not included; we simply
  ///     elide any corresponding #define.  If this function returns true,
  ///     we stop the iteration immediately.
  //------------------------------------------------------------------
  virtual void
  ForEachMacro(const ModuleVector &modules,
               std::function<bool(const std::string &)> handler) = 0;

  //------------------------------------------------------------------
  /// Query whether Clang supports modules for a particular language.
  /// LLDB uses this to decide whether to try to find the modules loaded
  /// by a gaiven compile unit.
  ///
  /// @param[in] language
  ///     The language to query for.
  ///
  /// @return
  ///     True if Clang has modules for the given language.
  //------------------------------------------------------------------
  static bool LanguageSupportsClangModules(lldb::LanguageType language);
};

} // namespace lldb_private

#endif // liblldb_ClangModulesDeclVendor_h
