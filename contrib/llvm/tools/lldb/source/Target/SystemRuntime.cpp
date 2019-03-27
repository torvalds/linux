//===-- SystemRuntime.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/SystemRuntime.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Process.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

SystemRuntime *SystemRuntime::FindPlugin(Process *process) {
  SystemRuntimeCreateInstance create_callback = nullptr;
  for (uint32_t idx = 0;
       (create_callback = PluginManager::GetSystemRuntimeCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    std::unique_ptr<SystemRuntime> instance_ap(create_callback(process));
    if (instance_ap)
      return instance_ap.release();
  }
  return nullptr;
}

//----------------------------------------------------------------------
// SystemRuntime constructor
//----------------------------------------------------------------------
SystemRuntime::SystemRuntime(Process *process)
    : m_process(process), m_types() {}

SystemRuntime::~SystemRuntime() = default;

void SystemRuntime::DidAttach() {}

void SystemRuntime::DidLaunch() {}

void SystemRuntime::Detach() {}

void SystemRuntime::ModulesDidLoad(ModuleList &module_list) {}

const std::vector<ConstString> &SystemRuntime::GetExtendedBacktraceTypes() {
  return m_types;
}

ThreadSP SystemRuntime::GetExtendedBacktraceThread(ThreadSP thread,
                                                   ConstString type) {
  return ThreadSP();
}
