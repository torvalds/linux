//===-- PluginManager.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/PluginManager.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StringList.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#endif

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <assert.h>

namespace lldb_private {
class CommandInterpreter;
}

using namespace lldb;
using namespace lldb_private;

enum PluginAction {
  ePluginRegisterInstance,
  ePluginUnregisterInstance,
  ePluginGetInstanceAtIndex
};

typedef bool (*PluginInitCallback)();
typedef void (*PluginTermCallback)();

struct PluginInfo {
  PluginInfo() : plugin_init_callback(nullptr), plugin_term_callback(nullptr) {}

  llvm::sys::DynamicLibrary library;
  PluginInitCallback plugin_init_callback;
  PluginTermCallback plugin_term_callback;
};

typedef std::map<FileSpec, PluginInfo> PluginTerminateMap;

static std::recursive_mutex &GetPluginMapMutex() {
  static std::recursive_mutex g_plugin_map_mutex;
  return g_plugin_map_mutex;
}

static PluginTerminateMap &GetPluginMap() {
  static PluginTerminateMap g_plugin_map;
  return g_plugin_map;
}

static bool PluginIsLoaded(const FileSpec &plugin_file_spec) {
  std::lock_guard<std::recursive_mutex> guard(GetPluginMapMutex());
  PluginTerminateMap &plugin_map = GetPluginMap();
  return plugin_map.find(plugin_file_spec) != plugin_map.end();
}

static void SetPluginInfo(const FileSpec &plugin_file_spec,
                          const PluginInfo &plugin_info) {
  std::lock_guard<std::recursive_mutex> guard(GetPluginMapMutex());
  PluginTerminateMap &plugin_map = GetPluginMap();
  assert(plugin_map.find(plugin_file_spec) == plugin_map.end());
  plugin_map[plugin_file_spec] = plugin_info;
}

template <typename FPtrTy> static FPtrTy CastToFPtr(void *VPtr) {
  return reinterpret_cast<FPtrTy>(reinterpret_cast<intptr_t>(VPtr));
}

static FileSystem::EnumerateDirectoryResult
LoadPluginCallback(void *baton, llvm::sys::fs::file_type ft,
                   llvm::StringRef path) {
  //    PluginManager *plugin_manager = (PluginManager *)baton;
  Status error;

  namespace fs = llvm::sys::fs;
  // If we have a regular file, a symbolic link or unknown file type, try and
  // process the file. We must handle unknown as sometimes the directory
  // enumeration might be enumerating a file system that doesn't have correct
  // file type information.
  if (ft == fs::file_type::regular_file || ft == fs::file_type::symlink_file ||
      ft == fs::file_type::type_unknown) {
    FileSpec plugin_file_spec(path);
    FileSystem::Instance().Resolve(plugin_file_spec);

    if (PluginIsLoaded(plugin_file_spec))
      return FileSystem::eEnumerateDirectoryResultNext;
    else {
      PluginInfo plugin_info;

      std::string pluginLoadError;
      plugin_info.library = llvm::sys::DynamicLibrary::getPermanentLibrary(
          plugin_file_spec.GetPath().c_str(), &pluginLoadError);
      if (plugin_info.library.isValid()) {
        bool success = false;
        plugin_info.plugin_init_callback = CastToFPtr<PluginInitCallback>(
            plugin_info.library.getAddressOfSymbol("LLDBPluginInitialize"));
        if (plugin_info.plugin_init_callback) {
          // Call the plug-in "bool LLDBPluginInitialize(void)" function
          success = plugin_info.plugin_init_callback();
        }

        if (success) {
          // It is ok for the "LLDBPluginTerminate" symbol to be nullptr
          plugin_info.plugin_term_callback = CastToFPtr<PluginTermCallback>(
              plugin_info.library.getAddressOfSymbol("LLDBPluginTerminate"));
        } else {
          // The initialize function returned FALSE which means the plug-in
          // might not be compatible, or might be too new or too old, or might
          // not want to run on this machine.  Set it to a default-constructed
          // instance to invalidate it.
          plugin_info = PluginInfo();
        }

        // Regardless of success or failure, cache the plug-in load in our
        // plug-in info so we don't try to load it again and again.
        SetPluginInfo(plugin_file_spec, plugin_info);

        return FileSystem::eEnumerateDirectoryResultNext;
      }
    }
  }

  if (ft == fs::file_type::directory_file ||
      ft == fs::file_type::symlink_file || ft == fs::file_type::type_unknown) {
    // Try and recurse into anything that a directory or symbolic link. We must
    // also do this for unknown as sometimes the directory enumeration might be
    // enumerating a file system that doesn't have correct file type
    // information.
    return FileSystem::eEnumerateDirectoryResultEnter;
  }

  return FileSystem::eEnumerateDirectoryResultNext;
}

void PluginManager::Initialize() {
#if 1
  const bool find_directories = true;
  const bool find_files = true;
  const bool find_other = true;
  char dir_path[PATH_MAX];
  if (FileSpec dir_spec = HostInfo::GetSystemPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, nullptr);
    }
  }

  if (FileSpec dir_spec = HostInfo::GetUserPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, nullptr);
    }
  }
#endif
}

void PluginManager::Terminate() {
  std::lock_guard<std::recursive_mutex> guard(GetPluginMapMutex());
  PluginTerminateMap &plugin_map = GetPluginMap();

  PluginTerminateMap::const_iterator pos, end = plugin_map.end();
  for (pos = plugin_map.begin(); pos != end; ++pos) {
    // Call the plug-in "void LLDBPluginTerminate (void)" function if there is
    // one (if the symbol was not nullptr).
    if (pos->second.library.isValid()) {
      if (pos->second.plugin_term_callback)
        pos->second.plugin_term_callback();
    }
  }
  plugin_map.clear();
}

#pragma mark ABI

struct ABIInstance {
  ABIInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  ABICreateInstance create_callback;
};

typedef std::vector<ABIInstance> ABIInstances;

static std::recursive_mutex &GetABIInstancesMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static ABIInstances &GetABIInstances() {
  static ABIInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(const ConstString &name,
                                   const char *description,
                                   ABICreateInstance create_callback) {
  if (create_callback) {
    ABIInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetABIInstancesMutex());
    GetABIInstances().push_back(instance);
    return true;
  }
  return false;
}

bool PluginManager::UnregisterPlugin(ABICreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetABIInstancesMutex());
    ABIInstances &instances = GetABIInstances();

    ABIInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

ABICreateInstance PluginManager::GetABICreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetABIInstancesMutex());
  ABIInstances &instances = GetABIInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

ABICreateInstance
PluginManager::GetABICreateCallbackForPluginName(const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetABIInstancesMutex());
    ABIInstances &instances = GetABIInstances();

    ABIInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark Architecture

struct ArchitectureInstance {
  ConstString name;
  std::string description;
  PluginManager::ArchitectureCreateInstance create_callback;
};

typedef std::vector<ArchitectureInstance> ArchitectureInstances;

static std::mutex &GetArchitectureMutex() {
    static std::mutex g_architecture_mutex;
    return g_architecture_mutex;
}

static ArchitectureInstances &GetArchitectureInstances() {
  static ArchitectureInstances g_instances;
  return g_instances;
}

void PluginManager::RegisterPlugin(const ConstString &name,
                                   llvm::StringRef description,
                                   ArchitectureCreateInstance create_callback) {
  std::lock_guard<std::mutex> guard(GetArchitectureMutex());
  GetArchitectureInstances().push_back({name, description, create_callback});
}

void PluginManager::UnregisterPlugin(
    ArchitectureCreateInstance create_callback) {
  std::lock_guard<std::mutex> guard(GetArchitectureMutex());
  auto &instances = GetArchitectureInstances();

  for (auto pos = instances.begin(), end = instances.end(); pos != end; ++pos) {
    if (pos->create_callback == create_callback) {
      instances.erase(pos);
      return;
    }
  }
  llvm_unreachable("Plugin not found");
}

std::unique_ptr<Architecture>
PluginManager::CreateArchitectureInstance(const ArchSpec &arch) {
  std::lock_guard<std::mutex> guard(GetArchitectureMutex());
  for (const auto &instances : GetArchitectureInstances()) {
    if (auto plugin_up = instances.create_callback(arch))
      return plugin_up;
  }
  return nullptr;
}

#pragma mark Disassembler

struct DisassemblerInstance {
  DisassemblerInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  DisassemblerCreateInstance create_callback;
};

typedef std::vector<DisassemblerInstance> DisassemblerInstances;

static std::recursive_mutex &GetDisassemblerMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static DisassemblerInstances &GetDisassemblerInstances() {
  static DisassemblerInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(const ConstString &name,
                                   const char *description,
                                   DisassemblerCreateInstance create_callback) {
  if (create_callback) {
    DisassemblerInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetDisassemblerMutex());
    GetDisassemblerInstances().push_back(instance);
    return true;
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    DisassemblerCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetDisassemblerMutex());
    DisassemblerInstances &instances = GetDisassemblerInstances();

    DisassemblerInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

DisassemblerCreateInstance
PluginManager::GetDisassemblerCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetDisassemblerMutex());
  DisassemblerInstances &instances = GetDisassemblerInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

DisassemblerCreateInstance
PluginManager::GetDisassemblerCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetDisassemblerMutex());
    DisassemblerInstances &instances = GetDisassemblerInstances();

    DisassemblerInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark DynamicLoader

struct DynamicLoaderInstance {
  DynamicLoaderInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  DynamicLoaderCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<DynamicLoaderInstance> DynamicLoaderInstances;

static std::recursive_mutex &GetDynamicLoaderMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static DynamicLoaderInstances &GetDynamicLoaderInstances() {
  static DynamicLoaderInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    DynamicLoaderCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    DynamicLoaderInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    std::lock_guard<std::recursive_mutex> guard(GetDynamicLoaderMutex());
    GetDynamicLoaderInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    DynamicLoaderCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetDynamicLoaderMutex());
    DynamicLoaderInstances &instances = GetDynamicLoaderInstances();

    DynamicLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

DynamicLoaderCreateInstance
PluginManager::GetDynamicLoaderCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetDynamicLoaderMutex());
  DynamicLoaderInstances &instances = GetDynamicLoaderInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

DynamicLoaderCreateInstance
PluginManager::GetDynamicLoaderCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetDynamicLoaderMutex());
    DynamicLoaderInstances &instances = GetDynamicLoaderInstances();

    DynamicLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark JITLoader

struct JITLoaderInstance {
  JITLoaderInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  JITLoaderCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<JITLoaderInstance> JITLoaderInstances;

static std::recursive_mutex &GetJITLoaderMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static JITLoaderInstances &GetJITLoaderInstances() {
  static JITLoaderInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    JITLoaderCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    JITLoaderInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    std::lock_guard<std::recursive_mutex> guard(GetJITLoaderMutex());
    GetJITLoaderInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(JITLoaderCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetJITLoaderMutex());
    JITLoaderInstances &instances = GetJITLoaderInstances();

    JITLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

JITLoaderCreateInstance
PluginManager::GetJITLoaderCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetJITLoaderMutex());
  JITLoaderInstances &instances = GetJITLoaderInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

JITLoaderCreateInstance PluginManager::GetJITLoaderCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetJITLoaderMutex());
    JITLoaderInstances &instances = GetJITLoaderInstances();

    JITLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark EmulateInstruction

struct EmulateInstructionInstance {
  EmulateInstructionInstance()
      : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  EmulateInstructionCreateInstance create_callback;
};

typedef std::vector<EmulateInstructionInstance> EmulateInstructionInstances;

static std::recursive_mutex &GetEmulateInstructionMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static EmulateInstructionInstances &GetEmulateInstructionInstances() {
  static EmulateInstructionInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    EmulateInstructionCreateInstance create_callback) {
  if (create_callback) {
    EmulateInstructionInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetEmulateInstructionMutex());
    GetEmulateInstructionInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    EmulateInstructionCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetEmulateInstructionMutex());
    EmulateInstructionInstances &instances = GetEmulateInstructionInstances();

    EmulateInstructionInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

EmulateInstructionCreateInstance
PluginManager::GetEmulateInstructionCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetEmulateInstructionMutex());
  EmulateInstructionInstances &instances = GetEmulateInstructionInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

EmulateInstructionCreateInstance
PluginManager::GetEmulateInstructionCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetEmulateInstructionMutex());
    EmulateInstructionInstances &instances = GetEmulateInstructionInstances();

    EmulateInstructionInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark OperatingSystem

struct OperatingSystemInstance {
  OperatingSystemInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  OperatingSystemCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<OperatingSystemInstance> OperatingSystemInstances;

static std::recursive_mutex &GetOperatingSystemMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static OperatingSystemInstances &GetOperatingSystemInstances() {
  static OperatingSystemInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    OperatingSystemCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    OperatingSystemInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    std::lock_guard<std::recursive_mutex> guard(GetOperatingSystemMutex());
    GetOperatingSystemInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    OperatingSystemCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetOperatingSystemMutex());
    OperatingSystemInstances &instances = GetOperatingSystemInstances();

    OperatingSystemInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

OperatingSystemCreateInstance
PluginManager::GetOperatingSystemCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetOperatingSystemMutex());
  OperatingSystemInstances &instances = GetOperatingSystemInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

OperatingSystemCreateInstance
PluginManager::GetOperatingSystemCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetOperatingSystemMutex());
    OperatingSystemInstances &instances = GetOperatingSystemInstances();

    OperatingSystemInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark Language

struct LanguageInstance {
  LanguageInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  LanguageCreateInstance create_callback;
};

typedef std::vector<LanguageInstance> LanguageInstances;

static std::recursive_mutex &GetLanguageMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static LanguageInstances &GetLanguageInstances() {
  static LanguageInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(const ConstString &name,
                                   const char *description,
                                   LanguageCreateInstance create_callback) {
  if (create_callback) {
    LanguageInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetLanguageMutex());
    GetLanguageInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(LanguageCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetLanguageMutex());
    LanguageInstances &instances = GetLanguageInstances();

    LanguageInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

LanguageCreateInstance
PluginManager::GetLanguageCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetLanguageMutex());
  LanguageInstances &instances = GetLanguageInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

LanguageCreateInstance
PluginManager::GetLanguageCreateCallbackForPluginName(const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetLanguageMutex());
    LanguageInstances &instances = GetLanguageInstances();

    LanguageInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark LanguageRuntime

struct LanguageRuntimeInstance {
  LanguageRuntimeInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  LanguageRuntimeCreateInstance create_callback;
  LanguageRuntimeGetCommandObject command_callback;
};

typedef std::vector<LanguageRuntimeInstance> LanguageRuntimeInstances;

static std::recursive_mutex &GetLanguageRuntimeMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static LanguageRuntimeInstances &GetLanguageRuntimeInstances() {
  static LanguageRuntimeInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    LanguageRuntimeCreateInstance create_callback,
    LanguageRuntimeGetCommandObject command_callback) {
  if (create_callback) {
    LanguageRuntimeInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.command_callback = command_callback;
    std::lock_guard<std::recursive_mutex> guard(GetLanguageRuntimeMutex());
    GetLanguageRuntimeInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    LanguageRuntimeCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetLanguageRuntimeMutex());
    LanguageRuntimeInstances &instances = GetLanguageRuntimeInstances();

    LanguageRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

LanguageRuntimeCreateInstance
PluginManager::GetLanguageRuntimeCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetLanguageRuntimeMutex());
  LanguageRuntimeInstances &instances = GetLanguageRuntimeInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

LanguageRuntimeGetCommandObject
PluginManager::GetLanguageRuntimeGetCommandObjectAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetLanguageRuntimeMutex());
  LanguageRuntimeInstances &instances = GetLanguageRuntimeInstances();
  if (idx < instances.size())
    return instances[idx].command_callback;
  return nullptr;
}

LanguageRuntimeCreateInstance
PluginManager::GetLanguageRuntimeCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetLanguageRuntimeMutex());
    LanguageRuntimeInstances &instances = GetLanguageRuntimeInstances();

    LanguageRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark SystemRuntime

struct SystemRuntimeInstance {
  SystemRuntimeInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  SystemRuntimeCreateInstance create_callback;
};

typedef std::vector<SystemRuntimeInstance> SystemRuntimeInstances;

static std::recursive_mutex &GetSystemRuntimeMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static SystemRuntimeInstances &GetSystemRuntimeInstances() {
  static SystemRuntimeInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    SystemRuntimeCreateInstance create_callback) {
  if (create_callback) {
    SystemRuntimeInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetSystemRuntimeMutex());
    GetSystemRuntimeInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    SystemRuntimeCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetSystemRuntimeMutex());
    SystemRuntimeInstances &instances = GetSystemRuntimeInstances();

    SystemRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

SystemRuntimeCreateInstance
PluginManager::GetSystemRuntimeCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetSystemRuntimeMutex());
  SystemRuntimeInstances &instances = GetSystemRuntimeInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

SystemRuntimeCreateInstance
PluginManager::GetSystemRuntimeCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetSystemRuntimeMutex());
    SystemRuntimeInstances &instances = GetSystemRuntimeInstances();

    SystemRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark ObjectFile

struct ObjectFileInstance {
  ObjectFileInstance()
      : name(), description(), create_callback(nullptr),
        create_memory_callback(nullptr), get_module_specifications(nullptr),
        save_core(nullptr) {}

  ConstString name;
  std::string description;
  ObjectFileCreateInstance create_callback;
  ObjectFileCreateMemoryInstance create_memory_callback;
  ObjectFileGetModuleSpecifications get_module_specifications;
  ObjectFileSaveCore save_core;
};

typedef std::vector<ObjectFileInstance> ObjectFileInstances;

static std::recursive_mutex &GetObjectFileMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static ObjectFileInstances &GetObjectFileInstances() {
  static ObjectFileInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    ObjectFileCreateInstance create_callback,
    ObjectFileCreateMemoryInstance create_memory_callback,
    ObjectFileGetModuleSpecifications get_module_specifications,
    ObjectFileSaveCore save_core) {
  if (create_callback) {
    ObjectFileInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.create_memory_callback = create_memory_callback;
    instance.save_core = save_core;
    instance.get_module_specifications = get_module_specifications;
    std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
    GetObjectFileInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(ObjectFileCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
    ObjectFileInstances &instances = GetObjectFileInstances();

    ObjectFileInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

ObjectFileCreateInstance
PluginManager::GetObjectFileCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
  ObjectFileInstances &instances = GetObjectFileInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

ObjectFileCreateMemoryInstance
PluginManager::GetObjectFileCreateMemoryCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
  ObjectFileInstances &instances = GetObjectFileInstances();
  if (idx < instances.size())
    return instances[idx].create_memory_callback;
  return nullptr;
}

ObjectFileGetModuleSpecifications
PluginManager::GetObjectFileGetModuleSpecificationsCallbackAtIndex(
    uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
  ObjectFileInstances &instances = GetObjectFileInstances();
  if (idx < instances.size())
    return instances[idx].get_module_specifications;
  return nullptr;
}

ObjectFileCreateInstance
PluginManager::GetObjectFileCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
    ObjectFileInstances &instances = GetObjectFileInstances();

    ObjectFileInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

ObjectFileCreateMemoryInstance
PluginManager::GetObjectFileCreateMemoryCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
    ObjectFileInstances &instances = GetObjectFileInstances();

    ObjectFileInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_memory_callback;
    }
  }
  return nullptr;
}

Status PluginManager::SaveCore(const lldb::ProcessSP &process_sp,
                               const FileSpec &outfile) {
  Status error;
  std::lock_guard<std::recursive_mutex> guard(GetObjectFileMutex());
  ObjectFileInstances &instances = GetObjectFileInstances();

  ObjectFileInstances::iterator pos, end = instances.end();
  for (pos = instances.begin(); pos != end; ++pos) {
    if (pos->save_core && pos->save_core(process_sp, outfile, error))
      return error;
  }
  error.SetErrorString(
      "no ObjectFile plugins were able to save a core for this process");
  return error;
}

#pragma mark ObjectContainer

struct ObjectContainerInstance {
  ObjectContainerInstance()
      : name(), description(), create_callback(nullptr),
        get_module_specifications(nullptr) {}

  ConstString name;
  std::string description;
  ObjectContainerCreateInstance create_callback;
  ObjectFileGetModuleSpecifications get_module_specifications;
};

typedef std::vector<ObjectContainerInstance> ObjectContainerInstances;

static std::recursive_mutex &GetObjectContainerMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static ObjectContainerInstances &GetObjectContainerInstances() {
  static ObjectContainerInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    ObjectContainerCreateInstance create_callback,
    ObjectFileGetModuleSpecifications get_module_specifications) {
  if (create_callback) {
    ObjectContainerInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.get_module_specifications = get_module_specifications;
    std::lock_guard<std::recursive_mutex> guard(GetObjectContainerMutex());
    GetObjectContainerInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    ObjectContainerCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetObjectContainerMutex());
    ObjectContainerInstances &instances = GetObjectContainerInstances();

    ObjectContainerInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

ObjectContainerCreateInstance
PluginManager::GetObjectContainerCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetObjectContainerMutex());
  ObjectContainerInstances &instances = GetObjectContainerInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

ObjectContainerCreateInstance
PluginManager::GetObjectContainerCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetObjectContainerMutex());
    ObjectContainerInstances &instances = GetObjectContainerInstances();

    ObjectContainerInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

ObjectFileGetModuleSpecifications
PluginManager::GetObjectContainerGetModuleSpecificationsCallbackAtIndex(
    uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetObjectContainerMutex());
  ObjectContainerInstances &instances = GetObjectContainerInstances();
  if (idx < instances.size())
    return instances[idx].get_module_specifications;
  return nullptr;
}

#pragma mark Platform

struct PlatformInstance {
  PlatformInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  PlatformCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<PlatformInstance> PlatformInstances;

static std::recursive_mutex &GetPlatformInstancesMutex() {
  static std::recursive_mutex g_platform_instances_mutex;
  return g_platform_instances_mutex;
}

static PlatformInstances &GetPlatformInstances() {
  static PlatformInstances g_platform_instances;
  return g_platform_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    PlatformCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());

    PlatformInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    GetPlatformInstances().push_back(instance);
    return true;
  }
  return false;
}

const char *PluginManager::GetPlatformPluginNameAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
  PlatformInstances &instances = GetPlatformInstances();
  if (idx < instances.size())
    return instances[idx].name.GetCString();
  return nullptr;
}

const char *PluginManager::GetPlatformPluginDescriptionAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
  PlatformInstances &instances = GetPlatformInstances();
  if (idx < instances.size())
    return instances[idx].description.c_str();
  return nullptr;
}

bool PluginManager::UnregisterPlugin(PlatformCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
    PlatformInstances &instances = GetPlatformInstances();

    PlatformInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

PlatformCreateInstance
PluginManager::GetPlatformCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
  PlatformInstances &instances = GetPlatformInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

PlatformCreateInstance
PluginManager::GetPlatformCreateCallbackForPluginName(const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
    PlatformInstances &instances = GetPlatformInstances();

    PlatformInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

size_t PluginManager::AutoCompletePlatformName(llvm::StringRef name,
                                               StringList &matches) {
  if (name.empty())
    return matches.GetSize();

  std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
  PlatformInstances &instances = GetPlatformInstances();
  llvm::StringRef name_sref(name);

  PlatformInstances::iterator pos, end = instances.end();
  for (pos = instances.begin(); pos != end; ++pos) {
    llvm::StringRef plugin_name(pos->name.GetCString());
    if (plugin_name.startswith(name_sref))
      matches.AppendString(plugin_name.data());
  }
  return matches.GetSize();
}

#pragma mark Process

struct ProcessInstance {
  ProcessInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  ProcessCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<ProcessInstance> ProcessInstances;

static std::recursive_mutex &GetProcessMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static ProcessInstances &GetProcessInstances() {
  static ProcessInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    ProcessCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    ProcessInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
    GetProcessInstances().push_back(instance);
  }
  return false;
}

const char *PluginManager::GetProcessPluginNameAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
  ProcessInstances &instances = GetProcessInstances();
  if (idx < instances.size())
    return instances[idx].name.GetCString();
  return nullptr;
}

const char *PluginManager::GetProcessPluginDescriptionAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
  ProcessInstances &instances = GetProcessInstances();
  if (idx < instances.size())
    return instances[idx].description.c_str();
  return nullptr;
}

bool PluginManager::UnregisterPlugin(ProcessCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
    ProcessInstances &instances = GetProcessInstances();

    ProcessInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

ProcessCreateInstance
PluginManager::GetProcessCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
  ProcessInstances &instances = GetProcessInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

ProcessCreateInstance
PluginManager::GetProcessCreateCallbackForPluginName(const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
    ProcessInstances &instances = GetProcessInstances();

    ProcessInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark ScriptInterpreter

struct ScriptInterpreterInstance {
  ScriptInterpreterInstance()
      : name(), language(lldb::eScriptLanguageNone), description(),
        create_callback(nullptr) {}

  ConstString name;
  lldb::ScriptLanguage language;
  std::string description;
  ScriptInterpreterCreateInstance create_callback;
};

typedef std::vector<ScriptInterpreterInstance> ScriptInterpreterInstances;

static std::recursive_mutex &GetScriptInterpreterMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static ScriptInterpreterInstances &GetScriptInterpreterInstances() {
  static ScriptInterpreterInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    lldb::ScriptLanguage script_language,
    ScriptInterpreterCreateInstance create_callback) {
  if (!create_callback)
    return false;
  ScriptInterpreterInstance instance;
  assert((bool)name);
  instance.name = name;
  if (description && description[0])
    instance.description = description;
  instance.create_callback = create_callback;
  instance.language = script_language;
  std::lock_guard<std::recursive_mutex> guard(GetScriptInterpreterMutex());
  GetScriptInterpreterInstances().push_back(instance);
  return false;
}

bool PluginManager::UnregisterPlugin(
    ScriptInterpreterCreateInstance create_callback) {
  if (!create_callback)
    return false;
  std::lock_guard<std::recursive_mutex> guard(GetScriptInterpreterMutex());
  ScriptInterpreterInstances &instances = GetScriptInterpreterInstances();

  ScriptInterpreterInstances::iterator pos, end = instances.end();
  for (pos = instances.begin(); pos != end; ++pos) {
    if (pos->create_callback != create_callback)
      continue;

    instances.erase(pos);
    return true;
  }
  return false;
}

ScriptInterpreterCreateInstance
PluginManager::GetScriptInterpreterCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetScriptInterpreterMutex());
  ScriptInterpreterInstances &instances = GetScriptInterpreterInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

lldb::ScriptInterpreterSP PluginManager::GetScriptInterpreterForLanguage(
    lldb::ScriptLanguage script_lang, CommandInterpreter &interpreter) {
  std::lock_guard<std::recursive_mutex> guard(GetScriptInterpreterMutex());
  ScriptInterpreterInstances &instances = GetScriptInterpreterInstances();

  ScriptInterpreterInstances::iterator pos, end = instances.end();
  ScriptInterpreterCreateInstance none_instance = nullptr;
  for (pos = instances.begin(); pos != end; ++pos) {
    if (pos->language == lldb::eScriptLanguageNone)
      none_instance = pos->create_callback;

    if (script_lang == pos->language)
      return pos->create_callback(interpreter);
  }

  // If we didn't find one, return the ScriptInterpreter for the null language.
  assert(none_instance != nullptr);
  return none_instance(interpreter);
}

#pragma mark -
#pragma mark StructuredDataPlugin

// -----------------------------------------------------------------------------
// StructuredDataPlugin
// -----------------------------------------------------------------------------

struct StructuredDataPluginInstance {
  StructuredDataPluginInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr), filter_callback(nullptr) {}

  ConstString name;
  std::string description;
  StructuredDataPluginCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
  StructuredDataFilterLaunchInfo filter_callback;
};

typedef std::vector<StructuredDataPluginInstance> StructuredDataPluginInstances;

static std::recursive_mutex &GetStructuredDataPluginMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static StructuredDataPluginInstances &GetStructuredDataPluginInstances() {
  static StructuredDataPluginInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    StructuredDataPluginCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback,
    StructuredDataFilterLaunchInfo filter_callback) {
  if (create_callback) {
    StructuredDataPluginInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    instance.filter_callback = filter_callback;
    std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
    GetStructuredDataPluginInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    StructuredDataPluginCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
    StructuredDataPluginInstances &instances =
        GetStructuredDataPluginInstances();

    StructuredDataPluginInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

StructuredDataPluginCreateInstance
PluginManager::GetStructuredDataPluginCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
  StructuredDataPluginInstances &instances = GetStructuredDataPluginInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

StructuredDataPluginCreateInstance
PluginManager::GetStructuredDataPluginCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
    StructuredDataPluginInstances &instances =
        GetStructuredDataPluginInstances();

    StructuredDataPluginInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

StructuredDataFilterLaunchInfo
PluginManager::GetStructuredDataFilterCallbackAtIndex(
    uint32_t idx, bool &iteration_complete) {
  std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
  StructuredDataPluginInstances &instances = GetStructuredDataPluginInstances();
  if (idx < instances.size()) {
    iteration_complete = false;
    return instances[idx].filter_callback;
  } else {
    iteration_complete = true;
  }
  return nullptr;
}

#pragma mark SymbolFile

struct SymbolFileInstance {
  SymbolFileInstance()
      : name(), description(), create_callback(nullptr),
        debugger_init_callback(nullptr) {}

  ConstString name;
  std::string description;
  SymbolFileCreateInstance create_callback;
  DebuggerInitializeCallback debugger_init_callback;
};

typedef std::vector<SymbolFileInstance> SymbolFileInstances;

static std::recursive_mutex &GetSymbolFileMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static SymbolFileInstances &GetSymbolFileInstances() {
  static SymbolFileInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    SymbolFileCreateInstance create_callback,
    DebuggerInitializeCallback debugger_init_callback) {
  if (create_callback) {
    SymbolFileInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.debugger_init_callback = debugger_init_callback;
    std::lock_guard<std::recursive_mutex> guard(GetSymbolFileMutex());
    GetSymbolFileInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(SymbolFileCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetSymbolFileMutex());
    SymbolFileInstances &instances = GetSymbolFileInstances();

    SymbolFileInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

SymbolFileCreateInstance
PluginManager::GetSymbolFileCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetSymbolFileMutex());
  SymbolFileInstances &instances = GetSymbolFileInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

SymbolFileCreateInstance
PluginManager::GetSymbolFileCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetSymbolFileMutex());
    SymbolFileInstances &instances = GetSymbolFileInstances();

    SymbolFileInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark SymbolVendor

struct SymbolVendorInstance {
  SymbolVendorInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  SymbolVendorCreateInstance create_callback;
};

typedef std::vector<SymbolVendorInstance> SymbolVendorInstances;

static std::recursive_mutex &GetSymbolVendorMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static SymbolVendorInstances &GetSymbolVendorInstances() {
  static SymbolVendorInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(const ConstString &name,
                                   const char *description,
                                   SymbolVendorCreateInstance create_callback) {
  if (create_callback) {
    SymbolVendorInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetSymbolVendorMutex());
    GetSymbolVendorInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    SymbolVendorCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetSymbolVendorMutex());
    SymbolVendorInstances &instances = GetSymbolVendorInstances();

    SymbolVendorInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

SymbolVendorCreateInstance
PluginManager::GetSymbolVendorCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetSymbolVendorMutex());
  SymbolVendorInstances &instances = GetSymbolVendorInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

SymbolVendorCreateInstance
PluginManager::GetSymbolVendorCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetSymbolVendorMutex());
    SymbolVendorInstances &instances = GetSymbolVendorInstances();

    SymbolVendorInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark UnwindAssembly

struct UnwindAssemblyInstance {
  UnwindAssemblyInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  UnwindAssemblyCreateInstance create_callback;
};

typedef std::vector<UnwindAssemblyInstance> UnwindAssemblyInstances;

static std::recursive_mutex &GetUnwindAssemblyMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static UnwindAssemblyInstances &GetUnwindAssemblyInstances() {
  static UnwindAssemblyInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    UnwindAssemblyCreateInstance create_callback) {
  if (create_callback) {
    UnwindAssemblyInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetUnwindAssemblyMutex());
    GetUnwindAssemblyInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    UnwindAssemblyCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetUnwindAssemblyMutex());
    UnwindAssemblyInstances &instances = GetUnwindAssemblyInstances();

    UnwindAssemblyInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

UnwindAssemblyCreateInstance
PluginManager::GetUnwindAssemblyCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetUnwindAssemblyMutex());
  UnwindAssemblyInstances &instances = GetUnwindAssemblyInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

UnwindAssemblyCreateInstance
PluginManager::GetUnwindAssemblyCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetUnwindAssemblyMutex());
    UnwindAssemblyInstances &instances = GetUnwindAssemblyInstances();

    UnwindAssemblyInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark MemoryHistory

struct MemoryHistoryInstance {
  MemoryHistoryInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  MemoryHistoryCreateInstance create_callback;
};

typedef std::vector<MemoryHistoryInstance> MemoryHistoryInstances;

static std::recursive_mutex &GetMemoryHistoryMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static MemoryHistoryInstances &GetMemoryHistoryInstances() {
  static MemoryHistoryInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    MemoryHistoryCreateInstance create_callback) {
  if (create_callback) {
    MemoryHistoryInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    std::lock_guard<std::recursive_mutex> guard(GetMemoryHistoryMutex());
    GetMemoryHistoryInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    MemoryHistoryCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetMemoryHistoryMutex());
    MemoryHistoryInstances &instances = GetMemoryHistoryInstances();

    MemoryHistoryInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

MemoryHistoryCreateInstance
PluginManager::GetMemoryHistoryCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetMemoryHistoryMutex());
  MemoryHistoryInstances &instances = GetMemoryHistoryInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

MemoryHistoryCreateInstance
PluginManager::GetMemoryHistoryCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetMemoryHistoryMutex());
    MemoryHistoryInstances &instances = GetMemoryHistoryInstances();

    MemoryHistoryInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark InstrumentationRuntime

struct InstrumentationRuntimeInstance {
  InstrumentationRuntimeInstance()
      : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  InstrumentationRuntimeCreateInstance create_callback;
  InstrumentationRuntimeGetType get_type_callback;
};

typedef std::vector<InstrumentationRuntimeInstance>
    InstrumentationRuntimeInstances;

static std::recursive_mutex &GetInstrumentationRuntimeMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static InstrumentationRuntimeInstances &GetInstrumentationRuntimeInstances() {
  static InstrumentationRuntimeInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    InstrumentationRuntimeCreateInstance create_callback,
    InstrumentationRuntimeGetType get_type_callback) {
  if (create_callback) {
    InstrumentationRuntimeInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.get_type_callback = get_type_callback;
    std::lock_guard<std::recursive_mutex> guard(
        GetInstrumentationRuntimeMutex());
    GetInstrumentationRuntimeInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(
    InstrumentationRuntimeCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(
        GetInstrumentationRuntimeMutex());
    InstrumentationRuntimeInstances &instances =
        GetInstrumentationRuntimeInstances();

    InstrumentationRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

InstrumentationRuntimeGetType
PluginManager::GetInstrumentationRuntimeGetTypeCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetInstrumentationRuntimeMutex());
  InstrumentationRuntimeInstances &instances =
      GetInstrumentationRuntimeInstances();
  if (idx < instances.size())
    return instances[idx].get_type_callback;
  return nullptr;
}

InstrumentationRuntimeCreateInstance
PluginManager::GetInstrumentationRuntimeCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetInstrumentationRuntimeMutex());
  InstrumentationRuntimeInstances &instances =
      GetInstrumentationRuntimeInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

InstrumentationRuntimeCreateInstance
PluginManager::GetInstrumentationRuntimeCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(
        GetInstrumentationRuntimeMutex());
    InstrumentationRuntimeInstances &instances =
        GetInstrumentationRuntimeInstances();

    InstrumentationRuntimeInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

#pragma mark TypeSystem

struct TypeSystemInstance {
  TypeSystemInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  TypeSystemCreateInstance create_callback;
  TypeSystemEnumerateSupportedLanguages enumerate_callback;
};

typedef std::vector<TypeSystemInstance> TypeSystemInstances;

static std::recursive_mutex &GetTypeSystemMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static TypeSystemInstances &GetTypeSystemInstances() {
  static TypeSystemInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(const ConstString &name,
                                   const char *description,
                                   TypeSystemCreateInstance create_callback,
                                   TypeSystemEnumerateSupportedLanguages
                                       enumerate_supported_languages_callback) {
  if (create_callback) {
    TypeSystemInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.enumerate_callback = enumerate_supported_languages_callback;
    std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
    GetTypeSystemInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(TypeSystemCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
    TypeSystemInstances &instances = GetTypeSystemInstances();

    TypeSystemInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

TypeSystemCreateInstance
PluginManager::GetTypeSystemCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
  TypeSystemInstances &instances = GetTypeSystemInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

TypeSystemCreateInstance
PluginManager::GetTypeSystemCreateCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
    TypeSystemInstances &instances = GetTypeSystemInstances();

    TypeSystemInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

TypeSystemEnumerateSupportedLanguages
PluginManager::GetTypeSystemEnumerateSupportedLanguagesCallbackAtIndex(
    uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
  TypeSystemInstances &instances = GetTypeSystemInstances();
  if (idx < instances.size())
    return instances[idx].enumerate_callback;
  return nullptr;
}

TypeSystemEnumerateSupportedLanguages
PluginManager::GetTypeSystemEnumerateSupportedLanguagesCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetTypeSystemMutex());
    TypeSystemInstances &instances = GetTypeSystemInstances();

    TypeSystemInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->enumerate_callback;
    }
  }
  return nullptr;
}

#pragma mark REPL

struct REPLInstance {
  REPLInstance() : name(), description(), create_callback(nullptr) {}

  ConstString name;
  std::string description;
  REPLCreateInstance create_callback;
  REPLEnumerateSupportedLanguages enumerate_languages_callback;
};

typedef std::vector<REPLInstance> REPLInstances;

static std::recursive_mutex &GetREPLMutex() {
  static std::recursive_mutex g_instances_mutex;
  return g_instances_mutex;
}

static REPLInstances &GetREPLInstances() {
  static REPLInstances g_instances;
  return g_instances;
}

bool PluginManager::RegisterPlugin(
    const ConstString &name, const char *description,
    REPLCreateInstance create_callback,
    REPLEnumerateSupportedLanguages enumerate_languages_callback) {
  if (create_callback) {
    REPLInstance instance;
    assert((bool)name);
    instance.name = name;
    if (description && description[0])
      instance.description = description;
    instance.create_callback = create_callback;
    instance.enumerate_languages_callback = enumerate_languages_callback;
    std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
    GetREPLInstances().push_back(instance);
  }
  return false;
}

bool PluginManager::UnregisterPlugin(REPLCreateInstance create_callback) {
  if (create_callback) {
    std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
    REPLInstances &instances = GetREPLInstances();

    REPLInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->create_callback == create_callback) {
        instances.erase(pos);
        return true;
      }
    }
  }
  return false;
}

REPLCreateInstance PluginManager::GetREPLCreateCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
  REPLInstances &instances = GetREPLInstances();
  if (idx < instances.size())
    return instances[idx].create_callback;
  return nullptr;
}

REPLCreateInstance
PluginManager::GetREPLCreateCallbackForPluginName(const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
    REPLInstances &instances = GetREPLInstances();

    REPLInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->create_callback;
    }
  }
  return nullptr;
}

REPLEnumerateSupportedLanguages
PluginManager::GetREPLEnumerateSupportedLanguagesCallbackAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
  REPLInstances &instances = GetREPLInstances();
  if (idx < instances.size())
    return instances[idx].enumerate_languages_callback;
  return nullptr;
}

REPLEnumerateSupportedLanguages
PluginManager::GetREPLSystemEnumerateSupportedLanguagesCallbackForPluginName(
    const ConstString &name) {
  if (name) {
    std::lock_guard<std::recursive_mutex> guard(GetREPLMutex());
    REPLInstances &instances = GetREPLInstances();

    REPLInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (name == pos->name)
        return pos->enumerate_languages_callback;
    }
  }
  return nullptr;
}

#pragma mark PluginManager

void PluginManager::DebuggerInitialize(Debugger &debugger) {
  // Initialize the DynamicLoader plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetDynamicLoaderMutex());
    DynamicLoaderInstances &instances = GetDynamicLoaderInstances();

    DynamicLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->debugger_init_callback)
        pos->debugger_init_callback(debugger);
    }
  }

  // Initialize the JITLoader plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetJITLoaderMutex());
    JITLoaderInstances &instances = GetJITLoaderInstances();

    JITLoaderInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->debugger_init_callback)
        pos->debugger_init_callback(debugger);
    }
  }

  // Initialize the Platform plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetPlatformInstancesMutex());
    PlatformInstances &instances = GetPlatformInstances();

    PlatformInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->debugger_init_callback)
        pos->debugger_init_callback(debugger);
    }
  }

  // Initialize the Process plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetProcessMutex());
    ProcessInstances &instances = GetProcessInstances();

    ProcessInstances::iterator pos, end = instances.end();
    for (pos = instances.begin(); pos != end; ++pos) {
      if (pos->debugger_init_callback)
        pos->debugger_init_callback(debugger);
    }
  }

  // Initialize the SymbolFile plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetSymbolFileMutex());
    for (auto &sym_file : GetSymbolFileInstances()) {
      if (sym_file.debugger_init_callback)
        sym_file.debugger_init_callback(debugger);
    }
  }

  // Initialize the OperatingSystem plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetOperatingSystemMutex());
    for (auto &os : GetOperatingSystemInstances()) {
      if (os.debugger_init_callback)
        os.debugger_init_callback(debugger);
    }
  }

  // Initialize the StructuredDataPlugin plugins
  {
    std::lock_guard<std::recursive_mutex> guard(GetStructuredDataPluginMutex());
    for (auto &plugin : GetStructuredDataPluginInstances()) {
      if (plugin.debugger_init_callback)
        plugin.debugger_init_callback(debugger);
    }
  }
}

// This is the preferred new way to register plugin specific settings.  e.g.
// This will put a plugin's settings under e.g.
// "plugin.<plugin_type_name>.<plugin_type_desc>.SETTINGNAME".
static lldb::OptionValuePropertiesSP GetDebuggerPropertyForPlugins(
    Debugger &debugger, const ConstString &plugin_type_name,
    const ConstString &plugin_type_desc, bool can_create) {
  lldb::OptionValuePropertiesSP parent_properties_sp(
      debugger.GetValueProperties());
  if (parent_properties_sp) {
    static ConstString g_property_name("plugin");

    OptionValuePropertiesSP plugin_properties_sp =
        parent_properties_sp->GetSubProperty(nullptr, g_property_name);
    if (!plugin_properties_sp && can_create) {
      plugin_properties_sp =
          std::make_shared<OptionValueProperties>(g_property_name);
      parent_properties_sp->AppendProperty(
          g_property_name, ConstString("Settings specify to plugins."), true,
          plugin_properties_sp);
    }

    if (plugin_properties_sp) {
      lldb::OptionValuePropertiesSP plugin_type_properties_sp =
          plugin_properties_sp->GetSubProperty(nullptr, plugin_type_name);
      if (!plugin_type_properties_sp && can_create) {
        plugin_type_properties_sp =
            std::make_shared<OptionValueProperties>(plugin_type_name);
        plugin_properties_sp->AppendProperty(plugin_type_name, plugin_type_desc,
                                             true, plugin_type_properties_sp);
      }
      return plugin_type_properties_sp;
    }
  }
  return lldb::OptionValuePropertiesSP();
}

// This is deprecated way to register plugin specific settings.  e.g.
// "<plugin_type_name>.plugin.<plugin_type_desc>.SETTINGNAME" and Platform
// generic settings would be under "platform.SETTINGNAME".
static lldb::OptionValuePropertiesSP GetDebuggerPropertyForPluginsOldStyle(
    Debugger &debugger, const ConstString &plugin_type_name,
    const ConstString &plugin_type_desc, bool can_create) {
  static ConstString g_property_name("plugin");
  lldb::OptionValuePropertiesSP parent_properties_sp(
      debugger.GetValueProperties());
  if (parent_properties_sp) {
    OptionValuePropertiesSP plugin_properties_sp =
        parent_properties_sp->GetSubProperty(nullptr, plugin_type_name);
    if (!plugin_properties_sp && can_create) {
      plugin_properties_sp =
          std::make_shared<OptionValueProperties>(plugin_type_name);
      parent_properties_sp->AppendProperty(plugin_type_name, plugin_type_desc,
                                           true, plugin_properties_sp);
    }

    if (plugin_properties_sp) {
      lldb::OptionValuePropertiesSP plugin_type_properties_sp =
          plugin_properties_sp->GetSubProperty(nullptr, g_property_name);
      if (!plugin_type_properties_sp && can_create) {
        plugin_type_properties_sp =
            std::make_shared<OptionValueProperties>(g_property_name);
        plugin_properties_sp->AppendProperty(
            g_property_name, ConstString("Settings specific to plugins"), true,
            plugin_type_properties_sp);
      }
      return plugin_type_properties_sp;
    }
  }
  return lldb::OptionValuePropertiesSP();
}

namespace {

typedef lldb::OptionValuePropertiesSP
GetDebuggerPropertyForPluginsPtr(Debugger &, const ConstString &,
                                 const ConstString &, bool can_create);

lldb::OptionValuePropertiesSP
GetSettingForPlugin(Debugger &debugger, const ConstString &setting_name,
                    const ConstString &plugin_type_name,
                    GetDebuggerPropertyForPluginsPtr get_debugger_property =
                        GetDebuggerPropertyForPlugins) {
  lldb::OptionValuePropertiesSP properties_sp;
  lldb::OptionValuePropertiesSP plugin_type_properties_sp(get_debugger_property(
      debugger, plugin_type_name,
      ConstString(), // not creating to so we don't need the description
      false));
  if (plugin_type_properties_sp)
    properties_sp =
        plugin_type_properties_sp->GetSubProperty(nullptr, setting_name);
  return properties_sp;
}

bool CreateSettingForPlugin(
    Debugger &debugger, const ConstString &plugin_type_name,
    const ConstString &plugin_type_desc,
    const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property,
    GetDebuggerPropertyForPluginsPtr get_debugger_property =
        GetDebuggerPropertyForPlugins) {
  if (properties_sp) {
    lldb::OptionValuePropertiesSP plugin_type_properties_sp(
        get_debugger_property(debugger, plugin_type_name, plugin_type_desc,
                              true));
    if (plugin_type_properties_sp) {
      plugin_type_properties_sp->AppendProperty(properties_sp->GetName(),
                                                description, is_global_property,
                                                properties_sp);
      return true;
    }
  }
  return false;
}

const char *kDynamicLoaderPluginName("dynamic-loader");
const char *kPlatformPluginName("platform");
const char *kProcessPluginName("process");
const char *kSymbolFilePluginName("symbol-file");
const char *kJITLoaderPluginName("jit-loader");
const char *kStructuredDataPluginName("structured-data");

} // anonymous namespace

lldb::OptionValuePropertiesSP PluginManager::GetSettingForDynamicLoaderPlugin(
    Debugger &debugger, const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kDynamicLoaderPluginName));
}

bool PluginManager::CreateSettingForDynamicLoaderPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(
      debugger, ConstString(kDynamicLoaderPluginName),
      ConstString("Settings for dynamic loader plug-ins"), properties_sp,
      description, is_global_property);
}

lldb::OptionValuePropertiesSP
PluginManager::GetSettingForPlatformPlugin(Debugger &debugger,
                                           const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kPlatformPluginName),
                             GetDebuggerPropertyForPluginsOldStyle);
}

bool PluginManager::CreateSettingForPlatformPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(debugger, ConstString(kPlatformPluginName),
                                ConstString("Settings for platform plug-ins"),
                                properties_sp, description, is_global_property,
                                GetDebuggerPropertyForPluginsOldStyle);
}

lldb::OptionValuePropertiesSP
PluginManager::GetSettingForProcessPlugin(Debugger &debugger,
                                          const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kProcessPluginName));
}

bool PluginManager::CreateSettingForProcessPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(debugger, ConstString(kProcessPluginName),
                                ConstString("Settings for process plug-ins"),
                                properties_sp, description, is_global_property);
}

lldb::OptionValuePropertiesSP
PluginManager::GetSettingForSymbolFilePlugin(Debugger &debugger,
                                             const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kSymbolFilePluginName));
}

bool PluginManager::CreateSettingForSymbolFilePlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(
      debugger, ConstString(kSymbolFilePluginName),
      ConstString("Settings for symbol file plug-ins"), properties_sp,
      description, is_global_property);
}

lldb::OptionValuePropertiesSP
PluginManager::GetSettingForJITLoaderPlugin(Debugger &debugger,
                                            const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kJITLoaderPluginName));
}

bool PluginManager::CreateSettingForJITLoaderPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(debugger, ConstString(kJITLoaderPluginName),
                                ConstString("Settings for JIT loader plug-ins"),
                                properties_sp, description, is_global_property);
}

static const char *kOperatingSystemPluginName("os");

lldb::OptionValuePropertiesSP PluginManager::GetSettingForOperatingSystemPlugin(
    Debugger &debugger, const ConstString &setting_name) {
  lldb::OptionValuePropertiesSP properties_sp;
  lldb::OptionValuePropertiesSP plugin_type_properties_sp(
      GetDebuggerPropertyForPlugins(
          debugger, ConstString(kOperatingSystemPluginName),
          ConstString(), // not creating to so we don't need the description
          false));
  if (plugin_type_properties_sp)
    properties_sp =
        plugin_type_properties_sp->GetSubProperty(nullptr, setting_name);
  return properties_sp;
}

bool PluginManager::CreateSettingForOperatingSystemPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  if (properties_sp) {
    lldb::OptionValuePropertiesSP plugin_type_properties_sp(
        GetDebuggerPropertyForPlugins(
            debugger, ConstString(kOperatingSystemPluginName),
            ConstString("Settings for operating system plug-ins"), true));
    if (plugin_type_properties_sp) {
      plugin_type_properties_sp->AppendProperty(properties_sp->GetName(),
                                                description, is_global_property,
                                                properties_sp);
      return true;
    }
  }
  return false;
}

lldb::OptionValuePropertiesSP PluginManager::GetSettingForStructuredDataPlugin(
    Debugger &debugger, const ConstString &setting_name) {
  return GetSettingForPlugin(debugger, setting_name,
                             ConstString(kStructuredDataPluginName));
}

bool PluginManager::CreateSettingForStructuredDataPlugin(
    Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
    const ConstString &description, bool is_global_property) {
  return CreateSettingForPlugin(
      debugger, ConstString(kStructuredDataPluginName),
      ConstString("Settings for structured data plug-ins"), properties_sp,
      description, is_global_property);
}
