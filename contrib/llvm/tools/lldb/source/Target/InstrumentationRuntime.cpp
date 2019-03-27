//===-- InstrumentationRuntime.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

void InstrumentationRuntime::ModulesDidLoad(
    lldb_private::ModuleList &module_list, lldb_private::Process *process,
    InstrumentationRuntimeCollection &runtimes) {
  InstrumentationRuntimeCreateInstance create_callback = nullptr;
  InstrumentationRuntimeGetType get_type_callback;
  for (uint32_t idx = 0;; ++idx) {
    create_callback =
        PluginManager::GetInstrumentationRuntimeCreateCallbackAtIndex(idx);
    if (create_callback == nullptr)
      break;
    get_type_callback =
        PluginManager::GetInstrumentationRuntimeGetTypeCallbackAtIndex(idx);
    InstrumentationRuntimeType type = get_type_callback();

    InstrumentationRuntimeCollection::iterator pos;
    pos = runtimes.find(type);
    if (pos == runtimes.end()) {
      runtimes[type] = create_callback(process->shared_from_this());
    }
  }
}

void InstrumentationRuntime::ModulesDidLoad(
    lldb_private::ModuleList &module_list) {
  if (IsActive())
    return;

  if (GetRuntimeModuleSP()) {
    Activate();
    return;
  }

  module_list.ForEach([this](const lldb::ModuleSP module_sp) -> bool {
    const FileSpec &file_spec = module_sp->GetFileSpec();
    if (!file_spec)
      return true; // Keep iterating.

    const RegularExpression &runtime_regex = GetPatternForRuntimeLibrary();
    if (runtime_regex.Execute(file_spec.GetFilename().GetCString()) ||
        module_sp->IsExecutable()) {
      if (CheckIfRuntimeIsValid(module_sp)) {
        SetRuntimeModuleSP(module_sp);
        Activate();
        return false; // Stop iterating, we're done.
      }
    }

    return true;
  });
}

lldb::ThreadCollectionSP
InstrumentationRuntime::GetBacktracesFromExtendedStopInfo(
    StructuredData::ObjectSP info) {
  return ThreadCollectionSP(new ThreadCollection());
}
