//===-- DynamicLoader.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/DynamicLoader.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private-interfaces.h"

#include "llvm/ADT/StringRef.h"

#include <memory>

#include <assert.h>

using namespace lldb;
using namespace lldb_private;

DynamicLoader *DynamicLoader::FindPlugin(Process *process,
                                         const char *plugin_name) {
  DynamicLoaderCreateInstance create_callback = nullptr;
  if (plugin_name) {
    ConstString const_plugin_name(plugin_name);
    create_callback =
        PluginManager::GetDynamicLoaderCreateCallbackForPluginName(
            const_plugin_name);
    if (create_callback) {
      std::unique_ptr<DynamicLoader> instance_ap(
          create_callback(process, true));
      if (instance_ap)
        return instance_ap.release();
    }
  } else {
    for (uint32_t idx = 0;
         (create_callback =
              PluginManager::GetDynamicLoaderCreateCallbackAtIndex(idx)) !=
         nullptr;
         ++idx) {
      std::unique_ptr<DynamicLoader> instance_ap(
          create_callback(process, false));
      if (instance_ap)
        return instance_ap.release();
    }
  }
  return nullptr;
}

DynamicLoader::DynamicLoader(Process *process) : m_process(process) {}

DynamicLoader::~DynamicLoader() = default;

//----------------------------------------------------------------------
// Accessosors to the global setting as to whether to stop at image (shared
// library) loading/unloading.
//----------------------------------------------------------------------

bool DynamicLoader::GetStopWhenImagesChange() const {
  return m_process->GetStopOnSharedLibraryEvents();
}

void DynamicLoader::SetStopWhenImagesChange(bool stop) {
  m_process->SetStopOnSharedLibraryEvents(stop);
}

ModuleSP DynamicLoader::GetTargetExecutable() {
  Target &target = m_process->GetTarget();
  ModuleSP executable = target.GetExecutableModule();

  if (executable) {
    if (FileSystem::Instance().Exists(executable->GetFileSpec())) {
      ModuleSpec module_spec(executable->GetFileSpec(),
                             executable->GetArchitecture());
      auto module_sp = std::make_shared<Module>(module_spec);

      // Check if the executable has changed and set it to the target
      // executable if they differ.
      if (module_sp && module_sp->GetUUID().IsValid() &&
          executable->GetUUID().IsValid()) {
        if (module_sp->GetUUID() != executable->GetUUID())
          executable.reset();
      } else if (executable->FileHasChanged()) {
        executable.reset();
      }

      if (!executable) {
        executable = target.GetSharedModule(module_spec);
        if (executable.get() != target.GetExecutableModulePointer()) {
          // Don't load dependent images since we are in dyld where we will
          // know and find out about all images that are loaded
          target.SetExecutableModule(executable, eLoadDependentsNo);
        }
      }
    }
  }
  return executable;
}

void DynamicLoader::UpdateLoadedSections(ModuleSP module, addr_t link_map_addr,
                                         addr_t base_addr,
                                         bool base_addr_is_offset) {
  UpdateLoadedSectionsCommon(module, base_addr, base_addr_is_offset);
}

void DynamicLoader::UpdateLoadedSectionsCommon(ModuleSP module,
                                               addr_t base_addr,
                                               bool base_addr_is_offset) {
  bool changed;
  module->SetLoadAddress(m_process->GetTarget(), base_addr, base_addr_is_offset,
                         changed);
}

void DynamicLoader::UnloadSections(const ModuleSP module) {
  UnloadSectionsCommon(module);
}

void DynamicLoader::UnloadSectionsCommon(const ModuleSP module) {
  Target &target = m_process->GetTarget();
  const SectionList *sections = GetSectionListFromModule(module);

  assert(sections && "SectionList missing from unloaded module.");

  const size_t num_sections = sections->GetSize();
  for (size_t i = 0; i < num_sections; ++i) {
    SectionSP section_sp(sections->GetSectionAtIndex(i));
    target.SetSectionUnloaded(section_sp);
  }
}

const SectionList *
DynamicLoader::GetSectionListFromModule(const ModuleSP module) const {
  SectionList *sections = nullptr;
  if (module) {
    ObjectFile *obj_file = module->GetObjectFile();
    if (obj_file != nullptr) {
      sections = obj_file->GetSectionList();
    }
  }
  return sections;
}

ModuleSP DynamicLoader::LoadModuleAtAddress(const FileSpec &file,
                                            addr_t link_map_addr,
                                            addr_t base_addr,
                                            bool base_addr_is_offset) {
  Target &target = m_process->GetTarget();
  ModuleList &modules = target.GetImages();
  ModuleSpec module_spec(file, target.GetArchitecture());
  ModuleSP module_sp;

  if ((module_sp = modules.FindFirstModule(module_spec))) {
    UpdateLoadedSections(module_sp, link_map_addr, base_addr,
                         base_addr_is_offset);
    return module_sp;
  }

  if ((module_sp = target.GetSharedModule(module_spec))) {
    UpdateLoadedSections(module_sp, link_map_addr, base_addr,
                         base_addr_is_offset);
    return module_sp;
  }

  bool check_alternative_file_name = true;
  if (base_addr_is_offset) {
    // Try to fetch the load address of the file from the process as we need
    // absolute load address to read the file out of the memory instead of a
    // load bias.
    bool is_loaded = false;
    lldb::addr_t load_addr;
    Status error = m_process->GetFileLoadAddress(file, is_loaded, load_addr);
    if (error.Success() && is_loaded) {
      check_alternative_file_name = false;
      base_addr = load_addr;
    }
  }

  // We failed to find the module based on its name. Lets try to check if we
  // can find a different name based on the memory region info.
  if (check_alternative_file_name) {
    MemoryRegionInfo memory_info;
    Status error = m_process->GetMemoryRegionInfo(base_addr, memory_info);
    if (error.Success() && memory_info.GetMapped() &&
        memory_info.GetRange().GetRangeBase() == base_addr && 
        !(memory_info.GetName().IsEmpty())) {
      ModuleSpec new_module_spec(FileSpec(memory_info.GetName().AsCString()),
                                 target.GetArchitecture());

      if ((module_sp = modules.FindFirstModule(new_module_spec))) {
        UpdateLoadedSections(module_sp, link_map_addr, base_addr, false);
        return module_sp;
      }

      if ((module_sp = target.GetSharedModule(new_module_spec))) {
        UpdateLoadedSections(module_sp, link_map_addr, base_addr, false);
        return module_sp;
      }
    }
  }

  if ((module_sp = m_process->ReadModuleFromMemory(file, base_addr))) {
    UpdateLoadedSections(module_sp, link_map_addr, base_addr, false);
    target.GetImages().AppendIfNeeded(module_sp);
  }

  return module_sp;
}

int64_t DynamicLoader::ReadUnsignedIntWithSizeInBytes(addr_t addr,
                                                      int size_in_bytes) {
  Status error;
  uint64_t value =
      m_process->ReadUnsignedIntegerFromMemory(addr, size_in_bytes, 0, error);
  if (error.Fail())
    return -1;
  else
    return (int64_t)value;
}

addr_t DynamicLoader::ReadPointer(addr_t addr) {
  Status error;
  addr_t value = m_process->ReadPointerFromMemory(addr, error);
  if (error.Fail())
    return LLDB_INVALID_ADDRESS;
  else
    return value;
}

void DynamicLoader::LoadOperatingSystemPlugin(bool flush)
{
    if (m_process)
        m_process->LoadOperatingSystemPlugin(flush);
}

