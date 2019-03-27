//===-- SystemInitializerCommon.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Initialization/SystemInitializerCommon.h"

#include "Plugins/Instruction/ARM/EmulateInstructionARM.h"
#include "Plugins/Instruction/MIPS/EmulateInstructionMIPS.h"
#include "Plugins/Instruction/MIPS64/EmulateInstructionMIPS64.h"
#include "Plugins/ObjectContainer/BSD-Archive/ObjectContainerBSDArchive.h"
#ifdef LLDB_ENABLE_ALL
#include "Plugins/ObjectContainer/Universal-Mach-O/ObjectContainerUniversalMachO.h"
#endif // LLDB_ENABLE_ALL
#include "Plugins/Process/gdb-remote/ProcessGDBRemoteLog.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Reproducer.h"
#include "lldb/Utility/Timer.h"

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#endif

#if defined(_MSC_VER)
#include "Plugins/Process/Windows/Common/ProcessWindowsLog.h"
#include "lldb/Host/windows/windows.h"
#endif

#include "llvm/Support/TargetSelect.h"

#include <string>

using namespace lldb_private;
using namespace lldb_private::repro;

SystemInitializerCommon::SystemInitializerCommon() {}

SystemInitializerCommon::~SystemInitializerCommon() {}

llvm::Error
SystemInitializerCommon::Initialize(const InitializerOptions &options) {
#if defined(_MSC_VER)
  const char *disable_crash_dialog_var = getenv("LLDB_DISABLE_CRASH_DIALOG");
  if (disable_crash_dialog_var &&
      llvm::StringRef(disable_crash_dialog_var).equals_lower("true")) {
    // This will prevent Windows from displaying a dialog box requiring user
    // interaction when
    // LLDB crashes.  This is mostly useful when automating LLDB, for example
    // via the test
    // suite, so that a crash in LLDB does not prevent completion of the test
    // suite.
    ::SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
                   SEM_NOGPFAULTERRORBOX);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  }
#endif

  ReproducerMode mode = ReproducerMode::Off;
  if (options.reproducer_capture)
    mode = ReproducerMode::Capture;
  if (options.reproducer_replay)
    mode = ReproducerMode::Replay;

  if (auto e = Reproducer::Initialize(mode, FileSpec(options.reproducer_path)))
    return e;

  FileSystem::Initialize();
  Log::Initialize();
  HostInfo::Initialize();
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, LLVM_PRETTY_FUNCTION);

  process_gdb_remote::ProcessGDBRemoteLog::Initialize();

  // Initialize plug-ins
  ObjectContainerBSDArchive::Initialize();

  EmulateInstructionARM::Initialize();
  EmulateInstructionMIPS::Initialize();
  EmulateInstructionMIPS64::Initialize();

  //----------------------------------------------------------------------
  // Apple/Darwin hosted plugins
  //----------------------------------------------------------------------
#ifdef LLDB_ENABLE_ALL
  ObjectContainerUniversalMachO::Initialize();
#endif // LLDB_ENABLE_ALL

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
  ProcessPOSIXLog::Initialize();
#endif
#if defined(_MSC_VER)
  ProcessWindowsLog::Initialize();
#endif

  return llvm::Error::success();
}

void SystemInitializerCommon::Terminate() {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, LLVM_PRETTY_FUNCTION);
  ObjectContainerBSDArchive::Terminate();

  EmulateInstructionARM::Terminate();
  EmulateInstructionMIPS::Terminate();
  EmulateInstructionMIPS64::Terminate();

#ifdef LLDB_ENABLE_ALL
  ObjectContainerUniversalMachO::Terminate();
#endif // LLDB_ENABLE_ALL

#if defined(_MSC_VER)
  ProcessWindowsLog::Terminate();
#endif

  HostInfo::Terminate();
  Log::DisableAllLogChannels();
  FileSystem::Terminate();
  Reproducer::Terminate();
}
