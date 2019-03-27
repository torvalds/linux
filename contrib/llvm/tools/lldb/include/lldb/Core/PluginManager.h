//===-- PluginManager.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PluginManager_h_
#define liblldb_PluginManager_h_

#include "lldb/Core/Architecture.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-interfaces.h"
#include "llvm/ADT/StringRef.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class CommandInterpreter;
}
namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class Debugger;
}
namespace lldb_private {
class StringList;
}
namespace lldb_private {

class PluginManager {
public:
  static void Initialize();

  static void Terminate();

  //------------------------------------------------------------------
  // ABI
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             ABICreateInstance create_callback);

  static bool UnregisterPlugin(ABICreateInstance create_callback);

  static ABICreateInstance GetABICreateCallbackAtIndex(uint32_t idx);

  static ABICreateInstance
  GetABICreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // Architecture
  //------------------------------------------------------------------
  using ArchitectureCreateInstance =
      std::unique_ptr<Architecture> (*)(const ArchSpec &);

  static void RegisterPlugin(const ConstString &name,
                             llvm::StringRef description,
                             ArchitectureCreateInstance create_callback);

  static void UnregisterPlugin(ArchitectureCreateInstance create_callback);

  static std::unique_ptr<Architecture>
  CreateArchitectureInstance(const ArchSpec &arch);

  //------------------------------------------------------------------
  // Disassembler
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             DisassemblerCreateInstance create_callback);

  static bool UnregisterPlugin(DisassemblerCreateInstance create_callback);

  static DisassemblerCreateInstance
  GetDisassemblerCreateCallbackAtIndex(uint32_t idx);

  static DisassemblerCreateInstance
  GetDisassemblerCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // DynamicLoader
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 DynamicLoaderCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr);

  static bool UnregisterPlugin(DynamicLoaderCreateInstance create_callback);

  static DynamicLoaderCreateInstance
  GetDynamicLoaderCreateCallbackAtIndex(uint32_t idx);

  static DynamicLoaderCreateInstance
  GetDynamicLoaderCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // JITLoader
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 JITLoaderCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr);

  static bool UnregisterPlugin(JITLoaderCreateInstance create_callback);

  static JITLoaderCreateInstance
  GetJITLoaderCreateCallbackAtIndex(uint32_t idx);

  static JITLoaderCreateInstance
  GetJITLoaderCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // EmulateInstruction
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             EmulateInstructionCreateInstance create_callback);

  static bool
  UnregisterPlugin(EmulateInstructionCreateInstance create_callback);

  static EmulateInstructionCreateInstance
  GetEmulateInstructionCreateCallbackAtIndex(uint32_t idx);

  static EmulateInstructionCreateInstance
  GetEmulateInstructionCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // OperatingSystem
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             OperatingSystemCreateInstance create_callback,
                             DebuggerInitializeCallback debugger_init_callback);

  static bool UnregisterPlugin(OperatingSystemCreateInstance create_callback);

  static OperatingSystemCreateInstance
  GetOperatingSystemCreateCallbackAtIndex(uint32_t idx);

  static OperatingSystemCreateInstance
  GetOperatingSystemCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // Language
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             LanguageCreateInstance create_callback);

  static bool UnregisterPlugin(LanguageCreateInstance create_callback);

  static LanguageCreateInstance GetLanguageCreateCallbackAtIndex(uint32_t idx);

  static LanguageCreateInstance
  GetLanguageCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // LanguageRuntime
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 LanguageRuntimeCreateInstance create_callback,
                 LanguageRuntimeGetCommandObject command_callback = nullptr);

  static bool UnregisterPlugin(LanguageRuntimeCreateInstance create_callback);

  static LanguageRuntimeCreateInstance
  GetLanguageRuntimeCreateCallbackAtIndex(uint32_t idx);

  static LanguageRuntimeGetCommandObject
  GetLanguageRuntimeGetCommandObjectAtIndex(uint32_t idx);

  static LanguageRuntimeCreateInstance
  GetLanguageRuntimeCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // SystemRuntime
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             SystemRuntimeCreateInstance create_callback);

  static bool UnregisterPlugin(SystemRuntimeCreateInstance create_callback);

  static SystemRuntimeCreateInstance
  GetSystemRuntimeCreateCallbackAtIndex(uint32_t idx);

  static SystemRuntimeCreateInstance
  GetSystemRuntimeCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // ObjectFile
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 ObjectFileCreateInstance create_callback,
                 ObjectFileCreateMemoryInstance create_memory_callback,
                 ObjectFileGetModuleSpecifications get_module_specifications,
                 ObjectFileSaveCore save_core = nullptr);

  static bool UnregisterPlugin(ObjectFileCreateInstance create_callback);

  static ObjectFileCreateInstance
  GetObjectFileCreateCallbackAtIndex(uint32_t idx);

  static ObjectFileCreateMemoryInstance
  GetObjectFileCreateMemoryCallbackAtIndex(uint32_t idx);

  static ObjectFileGetModuleSpecifications
  GetObjectFileGetModuleSpecificationsCallbackAtIndex(uint32_t idx);

  static ObjectFileCreateInstance
  GetObjectFileCreateCallbackForPluginName(const ConstString &name);

  static ObjectFileCreateMemoryInstance
  GetObjectFileCreateMemoryCallbackForPluginName(const ConstString &name);

  static Status SaveCore(const lldb::ProcessSP &process_sp,
                         const FileSpec &outfile);

  //------------------------------------------------------------------
  // ObjectContainer
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 ObjectContainerCreateInstance create_callback,
                 ObjectFileGetModuleSpecifications get_module_specifications);

  static bool UnregisterPlugin(ObjectContainerCreateInstance create_callback);

  static ObjectContainerCreateInstance
  GetObjectContainerCreateCallbackAtIndex(uint32_t idx);

  static ObjectContainerCreateInstance
  GetObjectContainerCreateCallbackForPluginName(const ConstString &name);

  static ObjectFileGetModuleSpecifications
  GetObjectContainerGetModuleSpecificationsCallbackAtIndex(uint32_t idx);

  //------------------------------------------------------------------
  // Platform
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 PlatformCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr);

  static bool UnregisterPlugin(PlatformCreateInstance create_callback);

  static PlatformCreateInstance GetPlatformCreateCallbackAtIndex(uint32_t idx);

  static PlatformCreateInstance
  GetPlatformCreateCallbackForPluginName(const ConstString &name);

  static const char *GetPlatformPluginNameAtIndex(uint32_t idx);

  static const char *GetPlatformPluginDescriptionAtIndex(uint32_t idx);

  static size_t AutoCompletePlatformName(llvm::StringRef partial_name,
                                         StringList &matches);
  //------------------------------------------------------------------
  // Process
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 ProcessCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr);

  static bool UnregisterPlugin(ProcessCreateInstance create_callback);

  static ProcessCreateInstance GetProcessCreateCallbackAtIndex(uint32_t idx);

  static ProcessCreateInstance
  GetProcessCreateCallbackForPluginName(const ConstString &name);

  static const char *GetProcessPluginNameAtIndex(uint32_t idx);

  static const char *GetProcessPluginDescriptionAtIndex(uint32_t idx);

  //------------------------------------------------------------------
  // ScriptInterpreter
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             lldb::ScriptLanguage script_lang,
                             ScriptInterpreterCreateInstance create_callback);

  static bool UnregisterPlugin(ScriptInterpreterCreateInstance create_callback);

  static ScriptInterpreterCreateInstance
  GetScriptInterpreterCreateCallbackAtIndex(uint32_t idx);

  static lldb::ScriptInterpreterSP
  GetScriptInterpreterForLanguage(lldb::ScriptLanguage script_lang,
                                  CommandInterpreter &interpreter);

  //------------------------------------------------------------------
  // StructuredDataPlugin
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Register a StructuredDataPlugin class along with optional
  /// callbacks for debugger initialization and Process launch info
  /// filtering and manipulation.
  ///
  /// @param[in] name
  ///    The name of the plugin.
  ///
  /// @param[in] description
  ///    A description string for the plugin.
  ///
  /// @param[in] create_callback
  ///    The callback that will be invoked to create an instance of
  ///    the callback.  This may not be nullptr.
  ///
  /// @param[in] debugger_init_callback
  ///    An optional callback that will be made when a Debugger
  ///    instance is initialized.
  ///
  /// @param[in] filter_callback
  ///    An optional callback that will be invoked before LLDB
  ///    launches a process for debugging.  The callback must
  ///    do the following:
  ///    1. Only do something if the plugin's behavior is enabled.
  ///    2. Only make changes for processes that are relevant to the
  ///       plugin.  The callback gets a pointer to the Target, which
  ///       can be inspected as needed.  The ProcessLaunchInfo is
  ///       provided in read-write mode, and may be modified by the
  ///       plugin if, for instance, additional environment variables
  ///       are needed to support the feature when enabled.
  ///
  /// @return
  ///    Returns true upon success; otherwise, false.
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 StructuredDataPluginCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr,
                 StructuredDataFilterLaunchInfo filter_callback = nullptr);

  static bool
  UnregisterPlugin(StructuredDataPluginCreateInstance create_callback);

  static StructuredDataPluginCreateInstance
  GetStructuredDataPluginCreateCallbackAtIndex(uint32_t idx);

  static StructuredDataPluginCreateInstance
  GetStructuredDataPluginCreateCallbackForPluginName(const ConstString &name);

  static StructuredDataFilterLaunchInfo
  GetStructuredDataFilterCallbackAtIndex(uint32_t idx,
                                         bool &iteration_complete);

  //------------------------------------------------------------------
  // SymbolFile
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 SymbolFileCreateInstance create_callback,
                 DebuggerInitializeCallback debugger_init_callback = nullptr);

  static bool UnregisterPlugin(SymbolFileCreateInstance create_callback);

  static SymbolFileCreateInstance
  GetSymbolFileCreateCallbackAtIndex(uint32_t idx);

  static SymbolFileCreateInstance
  GetSymbolFileCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // SymbolVendor
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             SymbolVendorCreateInstance create_callback);

  static bool UnregisterPlugin(SymbolVendorCreateInstance create_callback);

  static SymbolVendorCreateInstance
  GetSymbolVendorCreateCallbackAtIndex(uint32_t idx);

  static SymbolVendorCreateInstance
  GetSymbolVendorCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // UnwindAssembly
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             UnwindAssemblyCreateInstance create_callback);

  static bool UnregisterPlugin(UnwindAssemblyCreateInstance create_callback);

  static UnwindAssemblyCreateInstance
  GetUnwindAssemblyCreateCallbackAtIndex(uint32_t idx);

  static UnwindAssemblyCreateInstance
  GetUnwindAssemblyCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // MemoryHistory
  //------------------------------------------------------------------
  static bool RegisterPlugin(const ConstString &name, const char *description,
                             MemoryHistoryCreateInstance create_callback);

  static bool UnregisterPlugin(MemoryHistoryCreateInstance create_callback);

  static MemoryHistoryCreateInstance
  GetMemoryHistoryCreateCallbackAtIndex(uint32_t idx);

  static MemoryHistoryCreateInstance
  GetMemoryHistoryCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // InstrumentationRuntime
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 InstrumentationRuntimeCreateInstance create_callback,
                 InstrumentationRuntimeGetType get_type_callback);

  static bool
  UnregisterPlugin(InstrumentationRuntimeCreateInstance create_callback);

  static InstrumentationRuntimeGetType
  GetInstrumentationRuntimeGetTypeCallbackAtIndex(uint32_t idx);

  static InstrumentationRuntimeCreateInstance
  GetInstrumentationRuntimeCreateCallbackAtIndex(uint32_t idx);

  static InstrumentationRuntimeCreateInstance
  GetInstrumentationRuntimeCreateCallbackForPluginName(const ConstString &name);

  //------------------------------------------------------------------
  // TypeSystem
  //------------------------------------------------------------------
  static bool RegisterPlugin(
      const ConstString &name, const char *description,
      TypeSystemCreateInstance create_callback,
      TypeSystemEnumerateSupportedLanguages enumerate_languages_callback);

  static bool UnregisterPlugin(TypeSystemCreateInstance create_callback);

  static TypeSystemCreateInstance
  GetTypeSystemCreateCallbackAtIndex(uint32_t idx);

  static TypeSystemCreateInstance
  GetTypeSystemCreateCallbackForPluginName(const ConstString &name);

  static TypeSystemEnumerateSupportedLanguages
  GetTypeSystemEnumerateSupportedLanguagesCallbackAtIndex(uint32_t idx);

  static TypeSystemEnumerateSupportedLanguages
  GetTypeSystemEnumerateSupportedLanguagesCallbackForPluginName(
      const ConstString &name);

  //------------------------------------------------------------------
  // REPL
  //------------------------------------------------------------------
  static bool
  RegisterPlugin(const ConstString &name, const char *description,
                 REPLCreateInstance create_callback,
                 REPLEnumerateSupportedLanguages enumerate_languages_callback);

  static bool UnregisterPlugin(REPLCreateInstance create_callback);

  static REPLCreateInstance GetREPLCreateCallbackAtIndex(uint32_t idx);

  static REPLCreateInstance
  GetREPLCreateCallbackForPluginName(const ConstString &name);

  static REPLEnumerateSupportedLanguages
  GetREPLEnumerateSupportedLanguagesCallbackAtIndex(uint32_t idx);

  static REPLEnumerateSupportedLanguages
  GetREPLSystemEnumerateSupportedLanguagesCallbackForPluginName(
      const ConstString &name);

  //------------------------------------------------------------------
  // Some plug-ins might register a DebuggerInitializeCallback callback when
  // registering the plug-in. After a new Debugger instance is created, this
  // DebuggerInitialize function will get called. This allows plug-ins to
  // install Properties and do any other initialization that requires a
  // debugger instance.
  //------------------------------------------------------------------
  static void DebuggerInitialize(Debugger &debugger);

  static lldb::OptionValuePropertiesSP
  GetSettingForDynamicLoaderPlugin(Debugger &debugger,
                                   const ConstString &setting_name);

  static bool CreateSettingForDynamicLoaderPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForPlatformPlugin(Debugger &debugger,
                              const ConstString &setting_name);

  static bool CreateSettingForPlatformPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForProcessPlugin(Debugger &debugger,
                             const ConstString &setting_name);

  static bool CreateSettingForProcessPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForSymbolFilePlugin(Debugger &debugger,
                                const ConstString &setting_name);

  static bool CreateSettingForSymbolFilePlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForJITLoaderPlugin(Debugger &debugger,
                               const ConstString &setting_name);

  static bool CreateSettingForJITLoaderPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForOperatingSystemPlugin(Debugger &debugger,
                                     const ConstString &setting_name);

  static bool CreateSettingForOperatingSystemPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);

  static lldb::OptionValuePropertiesSP
  GetSettingForStructuredDataPlugin(Debugger &debugger,
                                    const ConstString &setting_name);

  static bool CreateSettingForStructuredDataPlugin(
      Debugger &debugger, const lldb::OptionValuePropertiesSP &properties_sp,
      const ConstString &description, bool is_global_property);
};

} // namespace lldb_private

#endif // liblldb_PluginManager_h_
