//===-- DynamicLoader.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DynamicLoader_h_
#define liblldb_DynamicLoader_h_

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>
namespace lldb_private {
class ModuleList;
}
namespace lldb_private {
class Process;
}
namespace lldb_private {
class SectionList;
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
class Thread;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DynamicLoader DynamicLoader.h "lldb/Target/DynamicLoader.h"
/// A plug-in interface definition class for dynamic loaders.
///
/// Dynamic loader plug-ins track image (shared library) loading and
/// unloading. The class is initialized given a live process that is halted at
/// its entry point or just after attaching.
///
/// Dynamic loader plug-ins can track the process by registering callbacks
/// using the: Process::RegisterNotificationCallbacks (const Notifications&)
/// function.
///
/// Breakpoints can also be set in the process which can register functions
/// that get called using: Process::BreakpointSetCallback (lldb::user_id_t,
/// BreakpointHitCallback, void *). These breakpoint callbacks return a
/// boolean value that indicates if the process should continue or halt and
/// should return the global setting for this using:
/// DynamicLoader::StopWhenImagesChange() const.
//----------------------------------------------------------------------
class DynamicLoader : public PluginInterface {
public:
  //------------------------------------------------------------------
  /// Find a dynamic loader plugin for a given process.
  ///
  /// Scans the installed DynamicLoader plug-ins and tries to find an instance
  /// that can be used to track image changes in \a process.
  ///
  /// @param[in] process
  ///     The process for which to try and locate a dynamic loader
  ///     plug-in instance.
  ///
  /// @param[in] plugin_name
  ///     An optional name of a specific dynamic loader plug-in that
  ///     should be used. If NULL, pick the best plug-in.
  //------------------------------------------------------------------
  static DynamicLoader *FindPlugin(Process *process, const char *plugin_name);

  //------------------------------------------------------------------
  /// Construct with a process.
  //------------------------------------------------------------------
  DynamicLoader(Process *process);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class is designed to be inherited
  /// from by the plug-in instance.
  //------------------------------------------------------------------
  virtual ~DynamicLoader() override;

  //------------------------------------------------------------------
  /// Called after attaching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after attaching to a
  /// process.
  //------------------------------------------------------------------
  virtual void DidAttach() = 0;

  //------------------------------------------------------------------
  /// Called after launching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after the process has
  /// stopped for the first time on launch.
  //------------------------------------------------------------------
  virtual void DidLaunch() = 0;

  //------------------------------------------------------------------
  /// Helper function that can be used to detect when a process has called
  /// exec and is now a new and different process. This can be called when
  /// necessary to try and detect the exec. The process might be able to
  /// answer this question, but sometimes it might not be able and the dynamic
  /// loader often knows what the program entry point is. So the process and
  /// the dynamic loader can work together to detect this.
  //------------------------------------------------------------------
  virtual bool ProcessDidExec() { return false; }
  //------------------------------------------------------------------
  /// Get whether the process should stop when images change.
  ///
  /// When images (executables and shared libraries) get loaded or unloaded,
  /// often debug sessions will want to try and resolve or unresolve
  /// breakpoints that are set in these images. Any breakpoints set by
  /// DynamicLoader plug-in instances should return this value to ensure
  /// consistent debug session behaviour.
  ///
  /// @return
  ///     Returns \b true if the process should stop when images
  ///     change, \b false if the process should resume.
  //------------------------------------------------------------------
  bool GetStopWhenImagesChange() const;

  //------------------------------------------------------------------
  /// Set whether the process should stop when images change.
  ///
  /// When images (executables and shared libraries) get loaded or unloaded,
  /// often debug sessions will want to try and resolve or unresolve
  /// breakpoints that are set in these images. The default is set so that the
  /// process stops when images change, but this can be overridden using this
  /// function callback.
  ///
  /// @param[in] stop
  ///     Boolean value that indicates whether the process should stop
  ///     when images change.
  //------------------------------------------------------------------
  void SetStopWhenImagesChange(bool stop);

  //------------------------------------------------------------------
  /// Provides a plan to step through the dynamic loader trampoline for the
  /// current state of \a thread.
  ///
  ///
  /// @param[in] stop_others
  ///     Whether the plan should be set to stop other threads.
  ///
  /// @return
  ///    A pointer to the plan (caller owned) or NULL if we are not at such
  ///    a trampoline.
  //------------------------------------------------------------------
  virtual lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                          bool stop_others) = 0;

  //------------------------------------------------------------------
  /// Some dynamic loaders provide features where there are a group of symbols
  /// "equivalent to" a given symbol one of which will be chosen when the
  /// symbol is bound.  If you want to set a breakpoint on one of these
  /// symbols, you really need to set it on all the equivalent symbols.
  ///
  ///
  /// @param[in] original_symbol
  ///     The symbol for which we are finding equivalences.
  ///
  /// @param[in] module_list
  ///     The set of modules in which to search.
  ///
  /// @param[out] equivalent_symbols
  ///     The equivalent symbol list - any equivalent symbols found are appended
  ///     to this list.
  ///
  /// @return
  ///    Number of equivalent symbols found.
  //------------------------------------------------------------------
  virtual size_t FindEquivalentSymbols(Symbol *original_symbol,
                                       ModuleList &module_list,
                                       SymbolContextList &equivalent_symbols) {
    return 0;
  }

  //------------------------------------------------------------------
  /// Ask if it is ok to try and load or unload an shared library (image).
  ///
  /// The dynamic loader often knows when it would be ok to try and load or
  /// unload a shared library. This function call allows the dynamic loader
  /// plug-ins to check any current dyld state to make sure it is an ok time
  /// to load a shared library.
  ///
  /// @return
  ///     \b true if it is currently ok to try and load a shared
  ///     library into the process, \b false otherwise.
  //------------------------------------------------------------------
  virtual Status CanLoadImage() = 0;

  //------------------------------------------------------------------
  /// Ask if the eh_frame information for the given SymbolContext should be
  /// relied on even when it's the first frame in a stack unwind.
  ///
  /// The CFI instructions from the eh_frame section are normally only valid
  /// at call sites -- places where a program could throw an exception and
  /// need to unwind out.  But some Modules may be known to the system as
  /// having reliable eh_frame information at all call sites.  This would be
  /// the case if the Module's contents are largely hand-written assembly with
  /// hand-written eh_frame information. Normally when unwinding from a
  /// function at the beginning of a stack unwind lldb will examine the
  /// assembly instructions to understand how the stack frame is set up and
  /// where saved registers are stored. But with hand-written assembly this is
  /// not reliable enough -- we need to consult those function's hand-written
  /// eh_frame information.
  ///
  /// @return
  ///     \b True if the symbol context should use eh_frame instructions
  ///     unconditionally when unwinding from this frame.  Else \b false,
  ///     the normal lldb unwind behavior of only using eh_frame when the
  ///     function appears in the middle of the stack.
  //------------------------------------------------------------------
  virtual bool AlwaysRelyOnEHUnwindInfo(SymbolContext &sym_ctx) {
    return false;
  }

  //------------------------------------------------------------------
  /// Retrieves the per-module TLS block for a given thread.
  ///
  /// @param[in] module
  ///     The module to query TLS data for.
  ///
  /// @param[in] thread
  ///     The specific thread to query TLS data for.
  ///
  /// @return
  ///     If the given thread has TLS data allocated for the
  ///     module, the address of the TLS block. Otherwise
  ///     LLDB_INVALID_ADDRESS is returned.
  //------------------------------------------------------------------
  virtual lldb::addr_t GetThreadLocalData(const lldb::ModuleSP module,
                                          const lldb::ThreadSP thread,
                                          lldb::addr_t tls_file_addr) {
    return LLDB_INVALID_ADDRESS;
  }

  /// Locates or creates a module given by @p file and updates/loads the
  /// resulting module at the virtual base address @p base_addr.
  virtual lldb::ModuleSP LoadModuleAtAddress(const lldb_private::FileSpec &file,
                                             lldb::addr_t link_map_addr,
                                             lldb::addr_t base_addr,
                                             bool base_addr_is_offset);

  //------------------------------------------------------------------
  /// Get information about the shared cache for a process, if possible.
  ///
  /// On some systems (e.g. Darwin based systems), a set of libraries that are
  /// common to most processes may be put in a single region of memory and
  /// mapped into every process, this is called the shared cache, as a
  /// performance optimization.
  ///
  /// Many targets will not have the concept of a shared cache.
  ///
  /// Depending on how the DynamicLoader gathers information about the shared
  /// cache, it may be able to only return basic information - like the UUID
  /// of the cache - or it may be able to return additional information about
  /// the cache.
  ///
  /// @param[out] base_address
  ///     The base address (load address) of the shared cache.
  ///     LLDB_INVALID_ADDRESS if it cannot be determined.
  ///
  /// @param[out] uuid
  ///     The UUID of the shared cache, if it can be determined.
  ///     If the UUID cannot be fetched, IsValid() will be false.
  ///
  /// @param[out] using_shared_cache
  ///     If this process is using a shared cache.
  ///     If unknown, eLazyBoolCalculate is returned.
  ///
  /// @param[out] private_shared_cache
  ///     A LazyBool indicating whether this process is using a
  ///     private shared cache.
  ///     If this information cannot be fetched, eLazyBoolCalculate.
  ///
  /// @return
  ///     Returns false if this DynamicLoader cannot gather information
  ///     about the shared cache / has no concept of a shared cache.
  //------------------------------------------------------------------
  virtual bool GetSharedCacheInformation(lldb::addr_t &base_address, UUID &uuid,
                                         LazyBool &using_shared_cache,
                                         LazyBool &private_shared_cache) {
    base_address = LLDB_INVALID_ADDRESS;
    uuid.Clear();
    using_shared_cache = eLazyBoolCalculate;
    private_shared_cache = eLazyBoolCalculate;
    return false;
  }

protected:
  //------------------------------------------------------------------
  // Utility methods for derived classes
  //------------------------------------------------------------------

  /// Checks to see if the target module has changed, updates the target
  /// accordingly and returns the target executable module.
  lldb::ModuleSP GetTargetExecutable();

  /// Updates the load address of every allocatable section in @p module.
  ///
  /// @param module The module to traverse.
  ///
  /// @param link_map_addr The virtual address of the link map for the @p
  /// module.
  ///
  /// @param base_addr The virtual base address @p module is loaded at.
  virtual void UpdateLoadedSections(lldb::ModuleSP module,
                                    lldb::addr_t link_map_addr,
                                    lldb::addr_t base_addr,
                                    bool base_addr_is_offset);

  // Utility method so base classes can share implementation of
  // UpdateLoadedSections
  void UpdateLoadedSectionsCommon(lldb::ModuleSP module, lldb::addr_t base_addr,
                                  bool base_addr_is_offset);

  /// Removes the loaded sections from the target in @p module.
  ///
  /// @param module The module to traverse.
  virtual void UnloadSections(const lldb::ModuleSP module);

  // Utility method so base classes can share implementation of UnloadSections
  void UnloadSectionsCommon(const lldb::ModuleSP module);

  const lldb_private::SectionList *
  GetSectionListFromModule(const lldb::ModuleSP module) const;

  // Read an unsigned int of the given size from memory at the given addr.
  // Return -1 if the read fails, otherwise return the result as an int64_t.
  int64_t ReadUnsignedIntWithSizeInBytes(lldb::addr_t addr, int size_in_bytes);

  // Read a pointer from memory at the given addr. Return LLDB_INVALID_ADDRESS
  // if the read fails.
  lldb::addr_t ReadPointer(lldb::addr_t addr);
  
  // Calls into the Process protected method LoadOperatingSystemPlugin:
  void LoadOperatingSystemPlugin(bool flush);


  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  Process
      *m_process; ///< The process that this dynamic loader plug-in is tracking.

private:
  DISALLOW_COPY_AND_ASSIGN(DynamicLoader);
};

} // namespace lldb_private

#endif // liblldb_DynamicLoader_h_
