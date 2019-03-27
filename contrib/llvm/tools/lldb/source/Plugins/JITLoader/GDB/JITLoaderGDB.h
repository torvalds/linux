//===-- JITLoaderGDB.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_JITLoaderGDB_h_
#define liblldb_JITLoaderGDB_h_

#include <map>

#include "lldb/Target/JITLoader.h"
#include "lldb/Target/Process.h"

class JITLoaderGDB : public lldb_private::JITLoader {
public:
  JITLoaderGDB(lldb_private::Process *process);

  ~JITLoaderGDB() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb::JITLoaderSP CreateInstance(lldb_private::Process *process,
                                          bool force);

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  //------------------------------------------------------------------
  // JITLoader interface
  //------------------------------------------------------------------
  void DidAttach() override;

  void DidLaunch() override;

  void ModulesDidLoad(lldb_private::ModuleList &module_list) override;

private:
  lldb::addr_t GetSymbolAddress(lldb_private::ModuleList &module_list,
                                const lldb_private::ConstString &name,
                                lldb::SymbolType symbol_type) const;

  void SetJITBreakpoint(lldb_private::ModuleList &module_list);

  bool DidSetJITBreakpoint() const;

  bool ReadJITDescriptor(bool all_entries);

  template <typename ptr_t> bool ReadJITDescriptorImpl(bool all_entries);

  static bool
  JITDebugBreakpointHit(void *baton,
                        lldb_private::StoppointCallbackContext *context,
                        lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  static void ProcessStateChangedCallback(void *baton,
                                          lldb_private::Process *process,
                                          lldb::StateType state);

  // A collection of in-memory jitted object addresses and their corresponding
  // modules
  typedef std::map<lldb::addr_t, const lldb::ModuleSP> JITObjectMap;
  JITObjectMap m_jit_objects;

  lldb::user_id_t m_jit_break_id;
  lldb::addr_t m_jit_descriptor_addr;
};

#endif // liblldb_JITLoaderGDB_h_
