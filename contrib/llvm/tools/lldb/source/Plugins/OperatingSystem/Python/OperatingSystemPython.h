//===-- OperatingSystemPython.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OperatingSystemPython_h_
#define liblldb_OperatingSystemPython_h_

#ifndef LLDB_DISABLE_PYTHON

#include "lldb/Target/OperatingSystem.h"
#include "lldb/Utility/StructuredData.h"

class DynamicRegisterInfo;

namespace lldb_private {
class ScriptInterpreter;
}

class OperatingSystemPython : public lldb_private::OperatingSystem {
public:
  OperatingSystemPython(lldb_private::Process *process,
                        const lldb_private::FileSpec &python_module_path);

  ~OperatingSystemPython() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static lldb_private::OperatingSystem *
  CreateInstance(lldb_private::Process *process, bool force);

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  //------------------------------------------------------------------
  // lldb_private::PluginInterface Methods
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  //------------------------------------------------------------------
  // lldb_private::OperatingSystem Methods
  //------------------------------------------------------------------
  bool UpdateThreadList(lldb_private::ThreadList &old_thread_list,
                        lldb_private::ThreadList &real_thread_list,
                        lldb_private::ThreadList &new_thread_list) override;

  void ThreadWasSelected(lldb_private::Thread *thread) override;

  lldb::RegisterContextSP
  CreateRegisterContextForThread(lldb_private::Thread *thread,
                                 lldb::addr_t reg_data_addr) override;

  lldb::StopInfoSP
  CreateThreadStopReason(lldb_private::Thread *thread) override;

  //------------------------------------------------------------------
  // Method for lazy creation of threads on demand
  //------------------------------------------------------------------
  lldb::ThreadSP CreateThread(lldb::tid_t tid, lldb::addr_t context) override;

protected:
  bool IsValid() const {
    return m_python_object_sp && m_python_object_sp->IsValid();
  }

  lldb::ThreadSP CreateThreadFromThreadInfo(
      lldb_private::StructuredData::Dictionary &thread_dict,
      lldb_private::ThreadList &core_thread_list,
      lldb_private::ThreadList &old_thread_list,
      std::vector<bool> &core_used_map, bool *did_create_ptr);

  DynamicRegisterInfo *GetDynamicRegisterInfo();

  lldb::ValueObjectSP m_thread_list_valobj_sp;
  std::unique_ptr<DynamicRegisterInfo> m_register_info_ap;
  lldb_private::ScriptInterpreter *m_interpreter;
  lldb_private::StructuredData::ObjectSP m_python_object_sp;
};

#endif // LLDB_DISABLE_PYTHON

#endif // liblldb_OperatingSystemPython_h_
