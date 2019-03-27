//===-- MIDriverMain.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    Defines the entry point for the console application.
//              The MI application (project name MI) runs in two modes:
//              An LLDB native driver mode where it acts no different from the
//              LLDB driver.
//              The other mode is the MI when it finds on the command line
//              the --interpreter option. Command line argument --help on its
//              own will give
//              help for the LLDB driver. If entered with --interpreter then MI
//              help will
//              provided.
//              To implement new MI commands derive a new command class from the
//              command base
//              class. To enable the new command for interpretation add the new
//              command class
//              to the command factory. The files of relevance are:
//                  MICmdCommands.cpp
//                  MICmdBase.h / .cpp
//                  MICmdCmd.h / .cpp

// Third party headers:
#include "lldb/API/SBHostOS.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include <atomic>
#include <csignal>
#include <stdio.h>

// In house headers:
#include "MICmnConfig.h"
#include "MICmnResources.h"
#include "MICmnStreamStdin.h"
#include "MIDriver.h"
#include "MIDriverMgr.h"
#include "MIUtilDebug.h"
#include "Platform.h"

#if defined(_MSC_VER)
#pragma warning(                                                               \
    once : 4530) // Warning C4530: C++ exception handler used, but unwind
                 // semantics are not enabled. Specify /EHsc
#endif           // _MSC_VER

// CODETAG_IOR_SIGNALS
//++
//------------------------------------------------------------------------------------
// Details: The SIGINT signal is sent to a process by its controlling terminal
// when a
//          user wishes to interrupt the process. This is typically initiated by
//          pressing
//          Control-C, but on some systems, the "delete" character or "break"
//          key can be
//          used.
//          Be aware this function may be called on another thread besides the
//          main thread.
// Type:    Function.
// Args:    vSigno  - (R) Signal number.
// Return:  None.
// Throws:  None.
//--
void sigint_handler(int vSigno) {
#ifdef _WIN32 // Restore handler as it is not persistent on Windows
  signal(SIGINT, sigint_handler);
#endif
  static std::atomic_flag g_interrupt_sent = ATOMIC_FLAG_INIT;
  CMIDriverMgr &rDriverMgr = CMIDriverMgr::Instance();
  lldb::SBDebugger *pDebugger = rDriverMgr.DriverGetTheDebugger();
  if (pDebugger != nullptr) {
    if (!g_interrupt_sent.test_and_set()) {
      pDebugger->DispatchInputInterrupt();
      g_interrupt_sent.clear();
    }
  }

  // Send signal to driver so that it can take suitable action
  rDriverMgr.DeliverSignal(vSigno);
}

//++
//------------------------------------------------------------------------------------
// Details: Init the MI driver system. Initialize the whole driver system which
// includes
//          both the original LLDB driver and the MI driver.
// Type:    Function.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool DriverSystemInit() {
  bool bOk = MIstatus::success;
  CMIDriver &rMIDriver = CMIDriver::Instance();
  CMIDriverMgr &rDriverMgr = CMIDriverMgr::Instance();
  bOk = rDriverMgr.Initialize();

  // Register MIDriver first as it needs to initialize and be ready
  // for the Driver to get information from MIDriver when it initializes
  // (LLDB Driver is registered with the Driver Manager in MI's Initialize())
  bOk = bOk &&
        rDriverMgr.RegisterDriver(rMIDriver, "MIDriver"); // Will be main driver

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Shutdown the debugger system. Release / terminate resources external
// to
//          specifically the MI driver.
// Type:    Function.
// Args:    vbAppExitOk - (R) True = No problems, false = App exiting with
// problems (investigate!).
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool DriverSystemShutdown(const bool vbAppExitOk) {
  bool bOk = MIstatus::success;

  // *** Order is important here ***
  CMIDriverMgr::Instance().Shutdown();
  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: MI's application start point of execution. The application runs in
// two modes.
//          An LLDB native driver mode where it acts no different from the LLDB
//          driver.
//          The other mode is the MI when it finds on the command line
//          the --interpreter option. Command line argument --help on its own
//          will give
//          help for the LLDB driver. If entered with --interpreter then
//          application
//          help will provided.
// Type:    Method.
// Args:    argc    - (R) An integer that contains the count of arguments that
// follow in
//                        argv. The argc parameter is always greater than or
//                        equal to 1.
//          argv    - (R) An array of null-terminated strings representing
//          command-line
//                        arguments entered by the user of the program. By
//                        convention,
//                        argv[0] is the command with which the program is
//                        invoked.
// Return:  int -  0 =   Normal exit, program success.
//                >0    = Program success with status i.e. Control-C signal
//                status
//                <0    = Program failed.
//              -1      = Program failed reason not specified here, see MI log
//              file.
//              -1000   = Program failed did not initialize successfully.
// Throws:  None.
//--
int main(int argc, char const *argv[]) {
#if MICONFIG_DEBUG_SHOW_ATTACH_DBG_DLG
  CMIUtilDebug::WaitForDbgAttachInfinteLoop();
#endif // MICONFIG_DEBUG_SHOW_ATTACH_DBG_DLG

  llvm::StringRef ToolName = argv[0];
  llvm::sys::PrintStackTraceOnErrorSignal(ToolName);
  llvm::PrettyStackTraceProgram X(argc, argv);

  // *** Order is important here ***
  bool bOk = DriverSystemInit();
  if (!bOk) {
    DriverSystemShutdown(bOk);
    return -1000;
  }

  // CODETAG_IOR_SIGNALS
  signal(SIGINT, sigint_handler);

  bool bExiting = false;
  CMIDriverMgr &rDriverMgr = CMIDriverMgr::Instance();
  bOk = bOk && rDriverMgr.ParseArgs(argc, argv, bExiting);
  if (bOk && !bExiting)
    bOk = rDriverMgr.DriverParseArgs(argc, argv, stdout, bExiting);
  if (bOk && !bExiting)
    bOk = rDriverMgr.DriverMainLoop();

  // Logger and other resources shutdown now
  DriverSystemShutdown(bOk);

  const int appResult = bOk ? 0 : -1;

  return appResult;
}
