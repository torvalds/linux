//===-- DynamicLoaderPOSIXDYLD.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DynamicLoaderPOSIXDYLD_h_
#define liblldb_DynamicLoaderPOSIXDYLD_h_

#include <map>
#include <memory>

#include "DYLDRendezvous.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Target/DynamicLoader.h"

class AuxVector;

class DynamicLoaderPOSIXDYLD : public lldb_private::DynamicLoader {
public:
  DynamicLoaderPOSIXDYLD(lldb_private::Process *process);

  ~DynamicLoaderPOSIXDYLD() override;

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::DynamicLoader *
  CreateInstance(lldb_private::Process *process, bool force);

  //------------------------------------------------------------------
  // DynamicLoader protocol
  //------------------------------------------------------------------

  void DidAttach() override;

  void DidLaunch() override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                                  bool stop_others) override;

  lldb_private::Status CanLoadImage() override;

  lldb::addr_t GetThreadLocalData(const lldb::ModuleSP module,
                                  const lldb::ThreadSP thread,
                                  lldb::addr_t tls_file_addr) override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

protected:
  /// Runtime linker rendezvous structure.
  DYLDRendezvous m_rendezvous;

  /// Virtual load address of the inferior process.
  lldb::addr_t m_load_offset;

  /// Virtual entry address of the inferior process.
  lldb::addr_t m_entry_point;

  /// Auxiliary vector of the inferior process.
  std::unique_ptr<AuxVector> m_auxv;

  /// Rendezvous breakpoint.
  lldb::break_id_t m_dyld_bid;

  /// Contains AT_SYSINFO_EHDR, which means a vDSO has been
  /// mapped to the address space
  lldb::addr_t m_vdso_base;

  /// Contains AT_BASE, which means a dynamic loader has been
  /// mapped to the address space
  lldb::addr_t m_interpreter_base;

  /// Loaded module list. (link map for each module)
  std::map<lldb::ModuleWP, lldb::addr_t, std::owner_less<lldb::ModuleWP>>
      m_loaded_modules;

  /// If possible sets a breakpoint on a function called by the runtime
  /// linker each time a module is loaded or unloaded.
  bool SetRendezvousBreakpoint();

  /// Callback routine which updates the current list of loaded modules based
  /// on the information supplied by the runtime linker.
  static bool RendezvousBreakpointHit(
      void *baton, lldb_private::StoppointCallbackContext *context,
      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  /// Helper method for RendezvousBreakpointHit.  Updates LLDB's current set
  /// of loaded modules.
  void RefreshModules();

  /// Updates the load address of every allocatable section in @p module.
  ///
  /// @param module The module to traverse.
  ///
  /// @param link_map_addr The virtual address of the link map for the @p
  /// module.
  ///
  /// @param base_addr The virtual base address @p module is loaded at.
  void UpdateLoadedSections(lldb::ModuleSP module, lldb::addr_t link_map_addr,
                            lldb::addr_t base_addr,
                            bool base_addr_is_offset) override;

  /// Removes the loaded sections from the target in @p module.
  ///
  /// @param module The module to traverse.
  void UnloadSections(const lldb::ModuleSP module) override;

  /// Resolves the entry point for the current inferior process and sets a
  /// breakpoint at that address.
  void ProbeEntry();

  /// Callback routine invoked when we hit the breakpoint on process entry.
  ///
  /// This routine is responsible for resolving the load addresses of all
  /// dependent modules required by the inferior and setting up the rendezvous
  /// breakpoint.
  static bool
  EntryBreakpointHit(void *baton,
                     lldb_private::StoppointCallbackContext *context,
                     lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  /// Helper for the entry breakpoint callback.  Resolves the load addresses
  /// of all dependent modules.
  virtual void LoadAllCurrentModules();

  void LoadVDSO();

  // Loading an interpreter module (if present) assumming m_interpreter_base
  // already points to its base address.
  lldb::ModuleSP LoadInterpreterModule();

  /// Computes a value for m_load_offset returning the computed address on
  /// success and LLDB_INVALID_ADDRESS on failure.
  lldb::addr_t ComputeLoadOffset();

  /// Computes a value for m_entry_point returning the computed address on
  /// success and LLDB_INVALID_ADDRESS on failure.
  lldb::addr_t GetEntryPoint();

  /// Evaluate if Aux vectors contain vDSO and LD information
  /// in case they do, read and assign the address to m_vdso_base
  /// and m_interpreter_base.
  void EvalSpecialModulesStatus();

  /// Loads Module from inferior process.
  void ResolveExecutableModule(lldb::ModuleSP &module_sp);

  bool AlwaysRelyOnEHUnwindInfo(lldb_private::SymbolContext &sym_ctx) override;

private:
  DISALLOW_COPY_AND_ASSIGN(DynamicLoaderPOSIXDYLD);
};

#endif // liblldb_DynamicLoaderPOSIXDYLD_h_
