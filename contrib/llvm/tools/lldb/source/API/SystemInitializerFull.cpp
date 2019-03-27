//===-- SystemInitializerFull.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if !defined(LLDB_DISABLE_PYTHON)
#include "Plugins/ScriptInterpreter/Python/lldb-python.h"
#endif

#include "SystemInitializerFull.h"

#include "lldb/API/SBCommandInterpreter.h"

#if !defined(LLDB_DISABLE_PYTHON)
#include "Plugins/ScriptInterpreter/Python/ScriptInterpreterPython.h"
#endif

#include "lldb/Core/Debugger.h"
#include "lldb/Host/Host.h"
#include "lldb/Initialization/SystemInitializerCommon.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Utility/Timer.h"

#ifdef LLDB_ENABLE_ALL
#include "Plugins/ABI/MacOSX-arm/ABIMacOSX_arm.h"
#include "Plugins/ABI/MacOSX-arm64/ABIMacOSX_arm64.h"
#include "Plugins/ABI/MacOSX-i386/ABIMacOSX_i386.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/ABI/SysV-arm/ABISysV_arm.h"
#include "Plugins/ABI/SysV-arm64/ABISysV_arm64.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/ABI/SysV-hexagon/ABISysV_hexagon.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/ABI/SysV-i386/ABISysV_i386.h"
#include "Plugins/ABI/SysV-mips/ABISysV_mips.h"
#include "Plugins/ABI/SysV-mips64/ABISysV_mips64.h"
#include "Plugins/ABI/SysV-ppc/ABISysV_ppc.h"
#include "Plugins/ABI/SysV-ppc64/ABISysV_ppc64.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/ABI/SysV-s390x/ABISysV_s390x.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/ABI/SysV-x86_64/ABISysV_x86_64.h"
#include "Plugins/Architecture/Arm/ArchitectureArm.h"
#include "Plugins/Architecture/Mips/ArchitectureMips.h"
#include "Plugins/Architecture/PPC64/ArchitecturePPC64.h"
#include "Plugins/Disassembler/llvm/DisassemblerLLVMC.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/DynamicLoader/MacOSX-DYLD/DynamicLoaderMacOS.h"
#include "Plugins/DynamicLoader/MacOSX-DYLD/DynamicLoaderMacOSXDYLD.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/DynamicLoader/POSIX-DYLD/DynamicLoaderPOSIXDYLD.h"
#include "Plugins/DynamicLoader/Static/DynamicLoaderStatic.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/DynamicLoader/Windows-DYLD/DynamicLoaderWindowsDYLD.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/Instruction/ARM64/EmulateInstructionARM64.h"
#include "Plugins/Instruction/PPC64/EmulateInstructionPPC64.h"
#include "Plugins/InstrumentationRuntime/ASan/ASanRuntime.h"
#include "Plugins/InstrumentationRuntime/MainThreadChecker/MainThreadCheckerRuntime.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/InstrumentationRuntime/TSan/TSanRuntime.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/InstrumentationRuntime/UBSan/UBSanRuntime.h"
#include "Plugins/JITLoader/GDB/JITLoaderGDB.h"
#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "Plugins/Language/ObjCPlusPlus/ObjCPlusPlusLanguage.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/LanguageRuntime/CPlusPlus/ItaniumABI/ItaniumABILanguageRuntime.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntimeV1.h"
#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntimeV2.h"
#include "Plugins/LanguageRuntime/RenderScript/RenderScriptRuntime/RenderScriptRuntime.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/MemoryHistory/asan/MemoryHistoryASan.h"
#include "Plugins/ObjectFile/Breakpad/ObjectFileBreakpad.h"
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
#include "Plugins/ObjectFile/PECOFF/ObjectFilePECOFF.h"
#include "Plugins/OperatingSystem/Python/OperatingSystemPython.h"
#include "Plugins/Platform/Android/PlatformAndroid.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/Platform/FreeBSD/PlatformFreeBSD.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/Platform/Kalimba/PlatformKalimba.h"
#include "Plugins/Platform/Linux/PlatformLinux.h"
#include "Plugins/Platform/MacOSX/PlatformMacOSX.h"
#include "Plugins/Platform/MacOSX/PlatformRemoteiOS.h"
#include "Plugins/Platform/NetBSD/PlatformNetBSD.h"
#include "Plugins/Platform/OpenBSD/PlatformOpenBSD.h"
#include "Plugins/Platform/Windows/PlatformWindows.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/Platform/gdb-server/PlatformRemoteGDBServer.h"
#include "Plugins/Process/elf-core/ProcessElfCore.h"
#include "Plugins/Process/gdb-remote/ProcessGDBRemote.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/Process/mach-core/ProcessMachCore.h"
#include "Plugins/Process/minidump/ProcessMinidump.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/ScriptInterpreter/None/ScriptInterpreterNone.h"
#include "Plugins/SymbolFile/Breakpad/SymbolFileBreakpad.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARFDebugMap.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/SymbolFile/PDB/SymbolFilePDB.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/SymbolFile/Symtab/SymbolFileSymtab.h"
#include "Plugins/SymbolVendor/ELF/SymbolVendorELF.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/SystemRuntime/MacOSX/SystemRuntimeMacOSX.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/UnwindAssembly/InstEmulation/UnwindAssemblyInstEmulation.h"
#include "Plugins/UnwindAssembly/x86/UnwindAssembly-x86.h"

#if defined(__APPLE__)
#include "Plugins/DynamicLoader/Darwin-Kernel/DynamicLoaderDarwinKernel.h"
#include "Plugins/Platform/MacOSX/PlatformAppleTVSimulator.h"
#include "Plugins/Platform/MacOSX/PlatformAppleWatchSimulator.h"
#include "Plugins/Platform/MacOSX/PlatformDarwinKernel.h"
#include "Plugins/Platform/MacOSX/PlatformRemoteAppleTV.h"
#include "Plugins/Platform/MacOSX/PlatformRemoteAppleWatch.h"
#include "Plugins/Platform/MacOSX/PlatformRemoteAppleBridge.h"
#include "Plugins/Platform/MacOSX/PlatformiOSSimulator.h"
#include "Plugins/Process/MacOSX-Kernel/ProcessKDP.h"
#include "Plugins/SymbolVendor/MacOSX/SymbolVendorMacOSX.h"
#endif
#ifdef LLDB_ENABLE_ALL
#include "Plugins/StructuredData/DarwinLog/StructuredDataDarwinLog.h"
#endif // LLDB_ENABLE_ALL

#if defined(__FreeBSD__)
#include "Plugins/Process/FreeBSD/ProcessFreeBSD.h"
#endif

#if defined(_WIN32)
#include "Plugins/Process/Windows/Common/ProcessWindows.h"
#include "lldb/Host/windows/windows.h"
#endif

#include "llvm/Support/TargetSelect.h"

#include <string>

using namespace lldb_private;

#ifndef LLDB_DISABLE_PYTHON

// Defined in the SWIG source file
#if PY_MAJOR_VERSION >= 3
extern "C" PyObject *PyInit__lldb(void);

#define LLDBSwigPyInit PyInit__lldb

#else
extern "C" void init_lldb(void);

#define LLDBSwigPyInit init_lldb
#endif

// these are the Pythonic implementations of the required callbacks these are
// scripting-language specific, which is why they belong here we still need to
// use function pointers to them instead of relying on linkage-time resolution
// because the SWIG stuff and this file get built at different times
extern "C" bool LLDBSwigPythonBreakpointCallbackFunction(
    const char *python_function_name, const char *session_dictionary_name,
    const lldb::StackFrameSP &sb_frame,
    const lldb::BreakpointLocationSP &sb_bp_loc);

extern "C" bool LLDBSwigPythonWatchpointCallbackFunction(
    const char *python_function_name, const char *session_dictionary_name,
    const lldb::StackFrameSP &sb_frame, const lldb::WatchpointSP &sb_wp);

extern "C" bool LLDBSwigPythonCallTypeScript(
    const char *python_function_name, void *session_dictionary,
    const lldb::ValueObjectSP &valobj_sp, void **pyfunct_wrapper,
    const lldb::TypeSummaryOptionsSP &options_sp, std::string &retval);

extern "C" void *
LLDBSwigPythonCreateSyntheticProvider(const char *python_class_name,
                                      const char *session_dictionary_name,
                                      const lldb::ValueObjectSP &valobj_sp);

extern "C" void *
LLDBSwigPythonCreateCommandObject(const char *python_class_name,
                                  const char *session_dictionary_name,
                                  const lldb::DebuggerSP debugger_sp);

extern "C" void *LLDBSwigPythonCreateScriptedThreadPlan(
    const char *python_class_name, const char *session_dictionary_name,
    const lldb::ThreadPlanSP &thread_plan_sp);

extern "C" bool LLDBSWIGPythonCallThreadPlan(void *implementor,
                                             const char *method_name,
                                             Event *event_sp, bool &got_error);

extern "C" void *LLDBSwigPythonCreateScriptedBreakpointResolver(
    const char *python_class_name,
    const char *session_dictionary_name,
    lldb_private::StructuredDataImpl *args,
    lldb::BreakpointSP &bkpt_sp);

extern "C" unsigned int LLDBSwigPythonCallBreakpointResolver(
    void *implementor,
    const char *method_name,
    lldb_private::SymbolContext *sym_ctx
);

extern "C" size_t LLDBSwigPython_CalculateNumChildren(void *implementor,
                                                      uint32_t max);

extern "C" void *LLDBSwigPython_GetChildAtIndex(void *implementor,
                                                uint32_t idx);

extern "C" int LLDBSwigPython_GetIndexOfChildWithName(void *implementor,
                                                      const char *child_name);

extern "C" void *LLDBSWIGPython_CastPyObjectToSBValue(void *data);

extern lldb::ValueObjectSP
LLDBSWIGPython_GetValueObjectSPFromSBValue(void *data);

extern "C" bool LLDBSwigPython_UpdateSynthProviderInstance(void *implementor);

extern "C" bool
LLDBSwigPython_MightHaveChildrenSynthProviderInstance(void *implementor);

extern "C" void *
LLDBSwigPython_GetValueSynthProviderInstance(void *implementor);

extern "C" bool
LLDBSwigPythonCallCommand(const char *python_function_name,
                          const char *session_dictionary_name,
                          lldb::DebuggerSP &debugger, const char *args,
                          lldb_private::CommandReturnObject &cmd_retobj,
                          lldb::ExecutionContextRefSP exe_ctx_ref_sp);

extern "C" bool
LLDBSwigPythonCallCommandObject(void *implementor, lldb::DebuggerSP &debugger,
                                const char *args,
                                lldb_private::CommandReturnObject &cmd_retobj,
                                lldb::ExecutionContextRefSP exe_ctx_ref_sp);

extern "C" bool
LLDBSwigPythonCallModuleInit(const char *python_module_name,
                             const char *session_dictionary_name,
                             lldb::DebuggerSP &debugger);

extern "C" void *
LLDBSWIGPythonCreateOSPlugin(const char *python_class_name,
                             const char *session_dictionary_name,
                             const lldb::ProcessSP &process_sp);

extern "C" void *LLDBSWIGPython_CreateFrameRecognizer(
    const char *python_class_name,
    const char *session_dictionary_name);

extern "C" void *LLDBSwigPython_GetRecognizedArguments(void *implementor,
    const lldb::StackFrameSP& frame_sp);

extern "C" bool LLDBSWIGPythonRunScriptKeywordProcess(
    const char *python_function_name, const char *session_dictionary_name,
    lldb::ProcessSP &process, std::string &output);

extern "C" bool LLDBSWIGPythonRunScriptKeywordThread(
    const char *python_function_name, const char *session_dictionary_name,
    lldb::ThreadSP &thread, std::string &output);

extern "C" bool LLDBSWIGPythonRunScriptKeywordTarget(
    const char *python_function_name, const char *session_dictionary_name,
    lldb::TargetSP &target, std::string &output);

extern "C" bool LLDBSWIGPythonRunScriptKeywordFrame(
    const char *python_function_name, const char *session_dictionary_name,
    lldb::StackFrameSP &frame, std::string &output);

extern "C" bool LLDBSWIGPythonRunScriptKeywordValue(
    const char *python_function_name, const char *session_dictionary_name,
    lldb::ValueObjectSP &value, std::string &output);

extern "C" void *
LLDBSWIGPython_GetDynamicSetting(void *module, const char *setting,
                                 const lldb::TargetSP &target_sp);

#endif

SystemInitializerFull::SystemInitializerFull() {}

SystemInitializerFull::~SystemInitializerFull() {}

llvm::Error
SystemInitializerFull::Initialize(const InitializerOptions &options) {
  if (auto e = SystemInitializerCommon::Initialize(options))
    return e;

  breakpad::ObjectFileBreakpad::Initialize();
  ObjectFileELF::Initialize();
#ifdef LLDB_ENABLE_ALL
  ObjectFileMachO::Initialize();
  ObjectFilePECOFF::Initialize();
#endif // LLDB_ENABLE_ALL

  ScriptInterpreterNone::Initialize();

#ifndef LLDB_DISABLE_PYTHON
  OperatingSystemPython::Initialize();
#endif

#if !defined(LLDB_DISABLE_PYTHON)
  InitializeSWIG();

  // ScriptInterpreterPython::Initialize() depends on things like HostInfo
  // being initialized so it can compute the python directory etc, so we need
  // to do this after SystemInitializerCommon::Initialize().
  ScriptInterpreterPython::Initialize();
#endif

  platform_freebsd::PlatformFreeBSD::Initialize();
#ifdef LLDB_ENABLE_ALL
  platform_linux::PlatformLinux::Initialize();
  platform_netbsd::PlatformNetBSD::Initialize();
  platform_openbsd::PlatformOpenBSD::Initialize();
  PlatformWindows::Initialize();
  PlatformKalimba::Initialize();
  platform_android::PlatformAndroid::Initialize();
  PlatformRemoteiOS::Initialize();
  PlatformMacOSX::Initialize();
#endif // LLDB_ENABLE_ALL
#if defined(__APPLE__)
  PlatformiOSSimulator::Initialize();
  PlatformDarwinKernel::Initialize();
#endif

  // Initialize LLVM and Clang
  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();

  ClangASTContext::Initialize();

#ifdef LLDB_ENABLE_ALL
  ABIMacOSX_i386::Initialize();
  ABIMacOSX_arm::Initialize();
  ABIMacOSX_arm64::Initialize();
#endif // LLDB_ENABLE_ALL
  ABISysV_arm::Initialize();
  ABISysV_arm64::Initialize();
#ifdef LLDB_ENABLE_ALL
  ABISysV_hexagon::Initialize();
#endif // LLDB_ENABLE_ALL
  ABISysV_i386::Initialize();
  ABISysV_x86_64::Initialize();
  ABISysV_ppc::Initialize();
  ABISysV_ppc64::Initialize();
  ABISysV_mips::Initialize();
  ABISysV_mips64::Initialize();
#ifdef LLDB_ENABLE_ALL
  ABISysV_s390x::Initialize();
#endif // LLDB_ENABLE_ALL

  ArchitectureArm::Initialize();
  ArchitectureMips::Initialize();
  ArchitecturePPC64::Initialize();

  DisassemblerLLVMC::Initialize();

  JITLoaderGDB::Initialize();
  ProcessElfCore::Initialize();
#ifdef LLDB_ENABLE_ALL
  ProcessMachCore::Initialize();
  minidump::ProcessMinidump::Initialize();
#endif // LLDB_ENABLE_ALL
  MemoryHistoryASan::Initialize();
  AddressSanitizerRuntime::Initialize();
#ifdef LLDB_ENABLE_ALL
  ThreadSanitizerRuntime::Initialize();
#endif // LLDB_ENABLE_ALL
  UndefinedBehaviorSanitizerRuntime::Initialize();
  MainThreadCheckerRuntime::Initialize();

  SymbolVendorELF::Initialize();
  breakpad::SymbolFileBreakpad::Initialize();
  SymbolFileDWARF::Initialize();
#ifdef LLDB_ENABLE_ALL
  SymbolFilePDB::Initialize();
#endif // LLDB_ENABLE_ALL
  SymbolFileSymtab::Initialize();
  UnwindAssemblyInstEmulation::Initialize();
  UnwindAssembly_x86::Initialize();
  EmulateInstructionARM64::Initialize();
  EmulateInstructionPPC64::Initialize();
  SymbolFileDWARFDebugMap::Initialize();
  ItaniumABILanguageRuntime::Initialize();
#ifdef LLDB_ENABLE_ALL
  AppleObjCRuntimeV2::Initialize();
  AppleObjCRuntimeV1::Initialize();
  SystemRuntimeMacOSX::Initialize();
  RenderScriptRuntime::Initialize();
#endif // LLDB_ENABLE_ALL

  CPlusPlusLanguage::Initialize();
#ifdef LLDB_ENABLE_ALL
  ObjCLanguage::Initialize();
  ObjCPlusPlusLanguage::Initialize();
#endif // LLDB_ENABLE_ALL

#if defined(_WIN32)
  ProcessWindows::Initialize();
#endif
#if defined(__FreeBSD__)
  ProcessFreeBSD::Initialize();
#endif
#if defined(__APPLE__)
  SymbolVendorMacOSX::Initialize();
  ProcessKDP::Initialize();
  PlatformAppleTVSimulator::Initialize();
  PlatformAppleWatchSimulator::Initialize();
  PlatformRemoteAppleTV::Initialize();
  PlatformRemoteAppleWatch::Initialize();
  PlatformRemoteAppleBridge::Initialize();
  DynamicLoaderDarwinKernel::Initialize();
#endif

  // This plugin is valid on any host that talks to a Darwin remote. It
  // shouldn't be limited to __APPLE__.
#ifdef LLDB_ENABLE_ALL
  StructuredDataDarwinLog::Initialize();
#endif // LLDB_ENABLE_ALL

  //----------------------------------------------------------------------
  // Platform agnostic plugins
  //----------------------------------------------------------------------
  platform_gdb_server::PlatformRemoteGDBServer::Initialize();

  process_gdb_remote::ProcessGDBRemote::Initialize();
#ifdef LLDB_ENABLE_ALL
  DynamicLoaderMacOSXDYLD::Initialize();
  DynamicLoaderMacOS::Initialize();
#endif // LLDB_ENABLE_ALL
  DynamicLoaderPOSIXDYLD::Initialize();
  DynamicLoaderStatic::Initialize();
#ifdef LLDB_ENABLE_ALL
  DynamicLoaderWindowsDYLD::Initialize();
#endif // LLDB_ENABLE_ALL

  // Scan for any system or user LLDB plug-ins
  PluginManager::Initialize();

  // The process settings need to know about installed plug-ins, so the
  // Settings must be initialized
  // AFTER PluginManager::Initialize is called.

  Debugger::SettingsInitialize();

  return llvm::Error::success();
}

void SystemInitializerFull::InitializeSWIG() {
#if !defined(LLDB_DISABLE_PYTHON)
  ScriptInterpreterPython::InitializeInterpreter(
      LLDBSwigPyInit, LLDBSwigPythonBreakpointCallbackFunction,
      LLDBSwigPythonWatchpointCallbackFunction, LLDBSwigPythonCallTypeScript,
      LLDBSwigPythonCreateSyntheticProvider, LLDBSwigPythonCreateCommandObject,
      LLDBSwigPython_CalculateNumChildren, LLDBSwigPython_GetChildAtIndex,
      LLDBSwigPython_GetIndexOfChildWithName,
      LLDBSWIGPython_CastPyObjectToSBValue,
      LLDBSWIGPython_GetValueObjectSPFromSBValue,
      LLDBSwigPython_UpdateSynthProviderInstance,
      LLDBSwigPython_MightHaveChildrenSynthProviderInstance,
      LLDBSwigPython_GetValueSynthProviderInstance, LLDBSwigPythonCallCommand,
      LLDBSwigPythonCallCommandObject, LLDBSwigPythonCallModuleInit,
      LLDBSWIGPythonCreateOSPlugin, LLDBSWIGPython_CreateFrameRecognizer,
      LLDBSwigPython_GetRecognizedArguments,
      LLDBSWIGPythonRunScriptKeywordProcess,
      LLDBSWIGPythonRunScriptKeywordThread,
      LLDBSWIGPythonRunScriptKeywordTarget, LLDBSWIGPythonRunScriptKeywordFrame,
      LLDBSWIGPythonRunScriptKeywordValue, LLDBSWIGPython_GetDynamicSetting,
      LLDBSwigPythonCreateScriptedThreadPlan, LLDBSWIGPythonCallThreadPlan,
      LLDBSwigPythonCreateScriptedBreakpointResolver, LLDBSwigPythonCallBreakpointResolver);
#endif
}

void SystemInitializerFull::Terminate() {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, LLVM_PRETTY_FUNCTION);

  Debugger::SettingsTerminate();

  // Terminate and unload and loaded system or user LLDB plug-ins
  PluginManager::Terminate();

  ClangASTContext::Terminate();

#ifdef LLDB_ENABLE_ALL
  ArchitectureArm::Terminate();
  ArchitectureMips::Terminate();
  ArchitecturePPC64::Terminate();

  ABIMacOSX_i386::Terminate();
  ABIMacOSX_arm::Terminate();
  ABIMacOSX_arm64::Terminate();
#endif // LLDB_ENABLE_ALL
  ABISysV_arm::Terminate();
  ABISysV_arm64::Terminate();
#ifdef LLDB_ENABLE_ALL
  ABISysV_hexagon::Terminate();
#endif // LLDB_ENABLE_ALL
  ABISysV_i386::Terminate();
  ABISysV_x86_64::Terminate();
  ABISysV_ppc::Terminate();
  ABISysV_ppc64::Terminate();
  ABISysV_mips::Terminate();
  ABISysV_mips64::Terminate();
#ifdef LLDB_ENABLE_ALL
  ABISysV_s390x::Terminate();
#endif // LLDB_ENABLE_ALL
  DisassemblerLLVMC::Terminate();

  JITLoaderGDB::Terminate();
  ProcessElfCore::Terminate();
#ifdef LLDB_ENABLE_ALL
  ProcessMachCore::Terminate();
  minidump::ProcessMinidump::Terminate();
#endif // LLDB_ENABLE_ALL
  MemoryHistoryASan::Terminate();
  AddressSanitizerRuntime::Terminate();
#ifdef LLDB_ENABLE_ALL
  ThreadSanitizerRuntime::Terminate();
#endif // LLDB_ENABLE_ALL
  UndefinedBehaviorSanitizerRuntime::Terminate();
  MainThreadCheckerRuntime::Terminate();
  SymbolVendorELF::Terminate();
  breakpad::SymbolFileBreakpad::Terminate();
  SymbolFileDWARF::Terminate();
#ifdef LLDB_ENABLE_ALL
  SymbolFilePDB::Terminate();
#endif // LLDB_ENABLE_ALL
  SymbolFileSymtab::Terminate();
  UnwindAssembly_x86::Terminate();
  UnwindAssemblyInstEmulation::Terminate();
  EmulateInstructionARM64::Terminate();
  EmulateInstructionPPC64::Terminate();
  SymbolFileDWARFDebugMap::Terminate();
  ItaniumABILanguageRuntime::Terminate();
#ifdef LLDB_ENABLE_ALL
  AppleObjCRuntimeV2::Terminate();
  AppleObjCRuntimeV1::Terminate();
  SystemRuntimeMacOSX::Terminate();
  RenderScriptRuntime::Terminate();
#endif // LLDB_ENABLE_ALL

  CPlusPlusLanguage::Terminate();
#ifdef LLDB_ENABLE_ALL
  ObjCLanguage::Terminate();
  ObjCPlusPlusLanguage::Terminate();
#endif // LLDB_ENABLE_ALL

#if defined(__APPLE__)
  DynamicLoaderDarwinKernel::Terminate();
  ProcessKDP::Terminate();
  SymbolVendorMacOSX::Terminate();
  PlatformAppleTVSimulator::Terminate();
  PlatformAppleWatchSimulator::Terminate();
  PlatformRemoteAppleTV::Terminate();
  PlatformRemoteAppleWatch::Terminate();
  PlatformRemoteAppleBridge::Terminate();
#endif

#if defined(__FreeBSD__)
  ProcessFreeBSD::Terminate();
#endif
  Debugger::SettingsTerminate();

  platform_gdb_server::PlatformRemoteGDBServer::Terminate();
  process_gdb_remote::ProcessGDBRemote::Terminate();
#ifdef LLDB_ENABLE_ALL
  StructuredDataDarwinLog::Terminate();

  DynamicLoaderMacOSXDYLD::Terminate();
  DynamicLoaderMacOS::Terminate();
#endif // LLDB_ENABLE_ALL
  DynamicLoaderPOSIXDYLD::Terminate();
  DynamicLoaderStatic::Terminate();
#ifdef LLDB_ENABLE_ALL
  DynamicLoaderWindowsDYLD::Terminate();
#endif // LLDB_ENABLE_ALL

#ifndef LLDB_DISABLE_PYTHON
  OperatingSystemPython::Terminate();
#endif

  platform_freebsd::PlatformFreeBSD::Terminate();
#ifdef LLDB_ENABLE_ALL
  platform_linux::PlatformLinux::Terminate();
  platform_netbsd::PlatformNetBSD::Terminate();
  platform_openbsd::PlatformOpenBSD::Terminate();
  PlatformWindows::Terminate();
  PlatformKalimba::Terminate();
  platform_android::PlatformAndroid::Terminate();
  PlatformMacOSX::Terminate();
  PlatformRemoteiOS::Terminate();
#endif // LLDB_ENABLE_ALL
#if defined(__APPLE__)
  PlatformiOSSimulator::Terminate();
  PlatformDarwinKernel::Terminate();
#endif

  breakpad::ObjectFileBreakpad::Terminate();
  ObjectFileELF::Terminate();
#ifdef LLDB_ENABLE_ALL
  ObjectFileMachO::Terminate();
  ObjectFilePECOFF::Terminate();
#endif // LLDB_ENABLE_ALL

  // Now shutdown the common parts, in reverse order.
  SystemInitializerCommon::Terminate();
}
