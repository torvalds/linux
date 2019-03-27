//===-- DynamicLoaderHexagonDYLD.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DynamicLoaderHexagonDYLD_h_
#define liblldb_DynamicLoaderHexagonDYLD_h_

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Target/DynamicLoader.h"

#include "HexagonDYLDRendezvous.h"

class DynamicLoaderHexagonDYLD : public lldb_private::DynamicLoader {
public:
  DynamicLoaderHexagonDYLD(lldb_private::Process *process);

  ~DynamicLoaderHexagonDYLD() override;

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
  HexagonDYLDRendezvous m_rendezvous;

  /// Virtual load address of the inferior process.
  lldb::addr_t m_load_offset;

  /// Virtual entry address of the inferior process.
  lldb::addr_t m_entry_point;

  /// Rendezvous breakpoint.
  lldb::break_id_t m_dyld_bid;

  /// Loaded module list. (link map for each module)
  std::map<lldb::ModuleWP, lldb::addr_t, std::owner_less<lldb::ModuleWP>>
      m_loaded_modules;

  /// Enables a breakpoint on a function called by the runtime
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
  void LoadAllCurrentModules();

  /// Computes a value for m_load_offset returning the computed address on
  /// success and LLDB_INVALID_ADDRESS on failure.
  lldb::addr_t ComputeLoadOffset();

  /// Computes a value for m_entry_point returning the computed address on
  /// success and LLDB_INVALID_ADDRESS on failure.
  lldb::addr_t GetEntryPoint();

  /// Checks to see if the target module has changed, updates the target
  /// accordingly and returns the target executable module.
  lldb::ModuleSP GetTargetExecutable();

  /// return the address of the Rendezvous breakpoint
  lldb::addr_t FindRendezvousBreakpointAddress();

private:
  const lldb_private::SectionList *
  GetSectionListFromModule(const lldb::ModuleSP module) const;

  DISALLOW_COPY_AND_ASSIGN(DynamicLoaderHexagonDYLD);
};

#endif // liblldb_DynamicLoaderHexagonDYLD_h_
