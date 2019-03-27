//===-- OperatingSystemPython.cpp --------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DISABLE_PYTHON

#include "OperatingSystemPython.h"
#include "Plugins/Process/Utility/DynamicRegisterInfo.h"
#include "Plugins/Process/Utility/RegisterContextDummy.h"
#include "Plugins/Process/Utility/RegisterContextMemory.h"
#include "Plugins/Process/Utility/ThreadMemory.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;

void OperatingSystemPython::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                nullptr);
}

void OperatingSystemPython::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

OperatingSystem *OperatingSystemPython::CreateInstance(Process *process,
                                                       bool force) {
  // Python OperatingSystem plug-ins must be requested by name, so force must
  // be true
  FileSpec python_os_plugin_spec(process->GetPythonOSPluginPath());
  if (python_os_plugin_spec &&
      FileSystem::Instance().Exists(python_os_plugin_spec)) {
    std::unique_ptr<OperatingSystemPython> os_ap(
        new OperatingSystemPython(process, python_os_plugin_spec));
    if (os_ap.get() && os_ap->IsValid())
      return os_ap.release();
  }
  return NULL;
}

ConstString OperatingSystemPython::GetPluginNameStatic() {
  static ConstString g_name("python");
  return g_name;
}

const char *OperatingSystemPython::GetPluginDescriptionStatic() {
  return "Operating system plug-in that gathers OS information from a python "
         "class that implements the necessary OperatingSystem functionality.";
}

OperatingSystemPython::OperatingSystemPython(lldb_private::Process *process,
                                             const FileSpec &python_module_path)
    : OperatingSystem(process), m_thread_list_valobj_sp(), m_register_info_ap(),
      m_interpreter(NULL), m_python_object_sp() {
  if (!process)
    return;
  TargetSP target_sp = process->CalculateTarget();
  if (!target_sp)
    return;
  m_interpreter =
      target_sp->GetDebugger().GetCommandInterpreter().GetScriptInterpreter();
  if (m_interpreter) {

    std::string os_plugin_class_name(
        python_module_path.GetFilename().AsCString(""));
    if (!os_plugin_class_name.empty()) {
      const bool init_session = false;
      const bool allow_reload = true;
      char python_module_path_cstr[PATH_MAX];
      python_module_path.GetPath(python_module_path_cstr,
                                 sizeof(python_module_path_cstr));
      Status error;
      if (m_interpreter->LoadScriptingModule(
              python_module_path_cstr, allow_reload, init_session, error)) {
        // Strip the ".py" extension if there is one
        size_t py_extension_pos = os_plugin_class_name.rfind(".py");
        if (py_extension_pos != std::string::npos)
          os_plugin_class_name.erase(py_extension_pos);
        // Add ".OperatingSystemPlugIn" to the module name to get a string like
        // "modulename.OperatingSystemPlugIn"
        os_plugin_class_name += ".OperatingSystemPlugIn";
        StructuredData::ObjectSP object_sp =
            m_interpreter->OSPlugin_CreatePluginObject(
                os_plugin_class_name.c_str(), process->CalculateProcess());
        if (object_sp && object_sp->IsValid())
          m_python_object_sp = object_sp;
      }
    }
  }
}

OperatingSystemPython::~OperatingSystemPython() {}

DynamicRegisterInfo *OperatingSystemPython::GetDynamicRegisterInfo() {
  if (m_register_info_ap.get() == NULL) {
    if (!m_interpreter || !m_python_object_sp)
      return NULL;
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OS));

    if (log)
      log->Printf("OperatingSystemPython::GetDynamicRegisterInfo() fetching "
                  "thread register definitions from python for pid %" PRIu64,
                  m_process->GetID());

    StructuredData::DictionarySP dictionary =
        m_interpreter->OSPlugin_RegisterInfo(m_python_object_sp);
    if (!dictionary)
      return NULL;

    m_register_info_ap.reset(new DynamicRegisterInfo(
        *dictionary, m_process->GetTarget().GetArchitecture()));
    assert(m_register_info_ap->GetNumRegisters() > 0);
    assert(m_register_info_ap->GetNumRegisterSets() > 0);
  }
  return m_register_info_ap.get();
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
ConstString OperatingSystemPython::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t OperatingSystemPython::GetPluginVersion() { return 1; }

bool OperatingSystemPython::UpdateThreadList(ThreadList &old_thread_list,
                                             ThreadList &core_thread_list,
                                             ThreadList &new_thread_list) {
  if (!m_interpreter || !m_python_object_sp)
    return false;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OS));

  // First thing we have to do is to try to get the API lock, and the
  // interpreter lock. We're going to change the thread content of the process,
  // and we're going to use python, which requires the API lock to do it. We
  // need the interpreter lock to make sure thread_info_dict stays alive.
  //
  // If someone already has the API lock, that is ok, we just want to avoid
  // external code from making new API calls while this call is happening.
  //
  // This is a recursive lock so we can grant it to any Python code called on
  // the stack below us.
  Target &target = m_process->GetTarget();
  std::unique_lock<std::recursive_mutex> api_lock(target.GetAPIMutex(),
                                                  std::defer_lock);
  api_lock.try_lock();
  auto interpreter_lock = m_interpreter->AcquireInterpreterLock();

  if (log)
    log->Printf("OperatingSystemPython::UpdateThreadList() fetching thread "
                "data from python for pid %" PRIu64,
                m_process->GetID());

  // The threads that are in "new_thread_list" upon entry are the threads from
  // the lldb_private::Process subclass, no memory threads will be in this
  // list.
  StructuredData::ArraySP threads_list =
      m_interpreter->OSPlugin_ThreadsInfo(m_python_object_sp);

  const uint32_t num_cores = core_thread_list.GetSize(false);

  // Make a map so we can keep track of which cores were used from the
  // core_thread list. Any real threads/cores that weren't used should later be
  // put back into the "new_thread_list".
  std::vector<bool> core_used_map(num_cores, false);
  if (threads_list) {
    if (log) {
      StreamString strm;
      threads_list->Dump(strm);
      log->Printf("threads_list = %s", strm.GetData());
    }

    const uint32_t num_threads = threads_list->GetSize();
    for (uint32_t i = 0; i < num_threads; ++i) {
      StructuredData::ObjectSP thread_dict_obj =
          threads_list->GetItemAtIndex(i);
      if (auto thread_dict = thread_dict_obj->GetAsDictionary()) {
        ThreadSP thread_sp(
            CreateThreadFromThreadInfo(*thread_dict, core_thread_list,
                                       old_thread_list, core_used_map, NULL));
        if (thread_sp)
          new_thread_list.AddThread(thread_sp);
      }
    }
  }

  // Any real core threads that didn't end up backing a memory thread should
  // still be in the main thread list, and they should be inserted at the
  // beginning of the list
  uint32_t insert_idx = 0;
  for (uint32_t core_idx = 0; core_idx < num_cores; ++core_idx) {
    if (!core_used_map[core_idx]) {
      new_thread_list.InsertThread(
          core_thread_list.GetThreadAtIndex(core_idx, false), insert_idx);
      ++insert_idx;
    }
  }

  return new_thread_list.GetSize(false) > 0;
}

ThreadSP OperatingSystemPython::CreateThreadFromThreadInfo(
    StructuredData::Dictionary &thread_dict, ThreadList &core_thread_list,
    ThreadList &old_thread_list, std::vector<bool> &core_used_map,
    bool *did_create_ptr) {
  ThreadSP thread_sp;
  tid_t tid = LLDB_INVALID_THREAD_ID;
  if (!thread_dict.GetValueForKeyAsInteger("tid", tid))
    return ThreadSP();

  uint32_t core_number;
  addr_t reg_data_addr;
  llvm::StringRef name;
  llvm::StringRef queue;

  thread_dict.GetValueForKeyAsInteger("core", core_number, UINT32_MAX);
  thread_dict.GetValueForKeyAsInteger("register_data_addr", reg_data_addr,
                                      LLDB_INVALID_ADDRESS);
  thread_dict.GetValueForKeyAsString("name", name);
  thread_dict.GetValueForKeyAsString("queue", queue);

  // See if a thread already exists for "tid"
  thread_sp = old_thread_list.FindThreadByID(tid, false);
  if (thread_sp) {
    // A thread already does exist for "tid", make sure it was an operating
    // system
    // plug-in generated thread.
    if (!IsOperatingSystemPluginThread(thread_sp)) {
      // We have thread ID overlap between the protocol threads and the
      // operating system threads, clear the thread so we create an operating
      // system thread for this.
      thread_sp.reset();
    }
  }

  if (!thread_sp) {
    if (did_create_ptr)
      *did_create_ptr = true;
    thread_sp.reset(
        new ThreadMemory(*m_process, tid, name, queue, reg_data_addr));
  }

  if (core_number < core_thread_list.GetSize(false)) {
    ThreadSP core_thread_sp(
        core_thread_list.GetThreadAtIndex(core_number, false));
    if (core_thread_sp) {
      // Keep track of which cores were set as the backing thread for memory
      // threads...
      if (core_number < core_used_map.size())
        core_used_map[core_number] = true;

      ThreadSP backing_core_thread_sp(core_thread_sp->GetBackingThread());
      if (backing_core_thread_sp) {
        thread_sp->SetBackingThread(backing_core_thread_sp);
      } else {
        thread_sp->SetBackingThread(core_thread_sp);
      }
    }
  }
  return thread_sp;
}

void OperatingSystemPython::ThreadWasSelected(Thread *thread) {}

RegisterContextSP
OperatingSystemPython::CreateRegisterContextForThread(Thread *thread,
                                                      addr_t reg_data_addr) {
  RegisterContextSP reg_ctx_sp;
  if (!m_interpreter || !m_python_object_sp || !thread)
    return reg_ctx_sp;

  if (!IsOperatingSystemPluginThread(thread->shared_from_this()))
    return reg_ctx_sp;

  // First thing we have to do is to try to get the API lock, and the
  // interpreter lock. We're going to change the thread content of the process,
  // and we're going to use python, which requires the API lock to do it. We
  // need the interpreter lock to make sure thread_info_dict stays alive.
  //
  // If someone already has the API lock, that is ok, we just want to avoid
  // external code from making new API calls while this call is happening.
  //
  // This is a recursive lock so we can grant it to any Python code called on
  // the stack below us.
  Target &target = m_process->GetTarget();
  std::unique_lock<std::recursive_mutex> api_lock(target.GetAPIMutex(),
                                                  std::defer_lock);
  api_lock.try_lock();
  auto interpreter_lock = m_interpreter->AcquireInterpreterLock();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));

  if (reg_data_addr != LLDB_INVALID_ADDRESS) {
    // The registers data is in contiguous memory, just create the register
    // context using the address provided
    if (log)
      log->Printf("OperatingSystemPython::CreateRegisterContextForThread (tid "
                  "= 0x%" PRIx64 ", 0x%" PRIx64 ", reg_data_addr = 0x%" PRIx64
                  ") creating memory register context",
                  thread->GetID(), thread->GetProtocolID(), reg_data_addr);
    reg_ctx_sp.reset(new RegisterContextMemory(
        *thread, 0, *GetDynamicRegisterInfo(), reg_data_addr));
  } else {
    // No register data address is provided, query the python plug-in to let it
    // make up the data as it sees fit
    if (log)
      log->Printf("OperatingSystemPython::CreateRegisterContextForThread (tid "
                  "= 0x%" PRIx64 ", 0x%" PRIx64
                  ") fetching register data from python",
                  thread->GetID(), thread->GetProtocolID());

    StructuredData::StringSP reg_context_data =
        m_interpreter->OSPlugin_RegisterContextData(m_python_object_sp,
                                                    thread->GetID());
    if (reg_context_data) {
      std::string value = reg_context_data->GetValue();
      DataBufferSP data_sp(new DataBufferHeap(value.c_str(), value.length()));
      if (data_sp->GetByteSize()) {
        RegisterContextMemory *reg_ctx_memory = new RegisterContextMemory(
            *thread, 0, *GetDynamicRegisterInfo(), LLDB_INVALID_ADDRESS);
        if (reg_ctx_memory) {
          reg_ctx_sp.reset(reg_ctx_memory);
          reg_ctx_memory->SetAllRegisterData(data_sp);
        }
      }
    }
  }
  // if we still have no register data, fallback on a dummy context to avoid
  // crashing
  if (!reg_ctx_sp) {
    if (log)
      log->Printf("OperatingSystemPython::CreateRegisterContextForThread (tid "
                  "= 0x%" PRIx64 ") forcing a dummy register context",
                  thread->GetID());
    reg_ctx_sp.reset(new RegisterContextDummy(
        *thread, 0, target.GetArchitecture().GetAddressByteSize()));
  }
  return reg_ctx_sp;
}

StopInfoSP
OperatingSystemPython::CreateThreadStopReason(lldb_private::Thread *thread) {
  // We should have gotten the thread stop info from the dictionary of data for
  // the thread in the initial call to get_thread_info(), this should have been
  // cached so we can return it here
  StopInfoSP
      stop_info_sp; //(StopInfo::CreateStopReasonWithSignal (*thread, SIGSTOP));
  return stop_info_sp;
}

lldb::ThreadSP OperatingSystemPython::CreateThread(lldb::tid_t tid,
                                                   addr_t context) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));

  if (log)
    log->Printf("OperatingSystemPython::CreateThread (tid = 0x%" PRIx64
                ", context = 0x%" PRIx64 ") fetching register data from python",
                tid, context);

  if (m_interpreter && m_python_object_sp) {
    // First thing we have to do is to try to get the API lock, and the
    // interpreter lock. We're going to change the thread content of the
    // process, and we're going to use python, which requires the API lock to
    // do it. We need the interpreter lock to make sure thread_info_dict stays
    // alive.
    //
    // If someone already has the API lock, that is ok, we just want to avoid
    // external code from making new API calls while this call is happening.
    //
    // This is a recursive lock so we can grant it to any Python code called on
    // the stack below us.
    Target &target = m_process->GetTarget();
    std::unique_lock<std::recursive_mutex> api_lock(target.GetAPIMutex(),
                                                    std::defer_lock);
    api_lock.try_lock();
    auto interpreter_lock = m_interpreter->AcquireInterpreterLock();

    StructuredData::DictionarySP thread_info_dict =
        m_interpreter->OSPlugin_CreateThread(m_python_object_sp, tid, context);
    std::vector<bool> core_used_map;
    if (thread_info_dict) {
      ThreadList core_threads(m_process);
      ThreadList &thread_list = m_process->GetThreadList();
      bool did_create = false;
      ThreadSP thread_sp(
          CreateThreadFromThreadInfo(*thread_info_dict, core_threads,
                                     thread_list, core_used_map, &did_create));
      if (did_create)
        thread_list.AddThread(thread_sp);
      return thread_sp;
    }
  }
  return ThreadSP();
}

#endif // #ifndef LLDB_DISABLE_PYTHON
