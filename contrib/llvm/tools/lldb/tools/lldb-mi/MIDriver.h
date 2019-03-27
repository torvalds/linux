//===-- MIDriver.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers
#include <queue>

// In-house headers:
#include "MICmdData.h"
#include "MICmnBase.h"
#include "MICmnConfig.h"
#include "MICmnStreamStdin.h"
#include "MIDriverBase.h"
#include "MIDriverMgr.h"
#include "MIUtilSingletonBase.h"

// Declarations:
class CMICmnLLDBDebugger;
class CMICmnStreamStdout;

//++
//============================================================================
// Details: MI driver implementation class. A singleton class derived from
//          LLDB SBBroadcaster class. Register the instance of *this class with
//          the CMIDriverMgr. The CMIDriverMgr sets the driver(s) of to start
//          work depending on the one selected to work. A driver can if not able
//          to handle an instruction or 'command' can pass that command onto
//          another driver object registered with the Driver Manager.
//--
class CMIDriver : public CMICmnBase,
                  public CMIDriverMgr::IDriver,
                  public CMIDriverBase,
                  public MI::ISingleton<CMIDriver> {
  friend class MI::ISingleton<CMIDriver>;

  // Enumerations:
public:
  //++ ----------------------------------------------------------------------
  // Details: The MI Driver has a running state which is used to help determine
  //          which specific action(s) it should take or not allow.
  //          The driver when operational and not shutting down alternates
  //          between eDriverState_RunningNotDebugging and
  //          eDriverState_RunningDebugging. eDriverState_RunningNotDebugging
  //          is normally set when a breakpoint is hit or halted.
  //          eDriverState_RunningDebugging is normally set when "exec-continue"
  //          or "exec-run" is issued.
  //--
  enum DriverState_e {
    eDriverState_NotRunning = 0,      // The MI Driver is not operating
    eDriverState_Initialising,        // The MI Driver is setting itself up
    eDriverState_RunningNotDebugging, // The MI Driver is operational acting on
                                      // any MI commands sent to it
    eDriverState_RunningDebugging, // The MI Driver is currently overseeing an
                                   // inferior program that is running
    eDriverState_ShuttingDown, // The MI Driver is tearing down resources and
                               // about exit
    eDriverState_count         // Always last
  };

  // Methods:
public:
  // MI system
  bool Initialize() override;
  bool Shutdown() override;

  // MI state
  bool GetExitApplicationFlag() const;
  DriverState_e GetCurrentDriverState() const;
  bool SetDriverStateRunningNotDebugging();
  bool SetDriverStateRunningDebugging();
  void SetDriverDebuggingArgExecutable();
  bool IsDriverDebuggingArgExecutable() const;

  // MI information about itself
  const CMIUtilString &GetAppNameShort() const;
  const CMIUtilString &GetAppNameLong() const;
  const CMIUtilString &GetVersionDescription() const;

  // MI do work
  bool WriteMessageToLog(const CMIUtilString &vMessage);
  bool SetEnableFallThru(const bool vbYes);
  bool GetEnableFallThru() const;
  bool HaveExecutableFileNamePathOnCmdLine() const;
  const CMIUtilString &GetExecutableFileNamePathOnCmdLine() const;

  // Overridden:
public:
  // From CMIDriverMgr::IDriver
  bool DoInitialize() override;
  bool DoShutdown() override;
  bool DoMainLoop() override;
  lldb::SBError DoParseArgs(const int argc, const char *argv[], FILE *vpStdOut,
                            bool &vwbExiting) override;
  CMIUtilString GetError() const override;
  const CMIUtilString &GetName() const override;
  lldb::SBDebugger &GetTheDebugger() override;
  bool GetDriverIsGDBMICompatibleDriver() const override;
  bool SetId(const CMIUtilString &vId) override;
  const CMIUtilString &GetId() const override;
  // From CMIDriverBase
  void SetExitApplicationFlag(const bool vbForceExit) override;
  bool DoFallThruToAnotherDriver(const CMIUtilString &vCmd,
                                 CMIUtilString &vwErrMsg) override;
  bool SetDriverToFallThruTo(const CMIDriverBase &vrOtherDriver) override;
  FILE *GetStdin() const override;
  FILE *GetStdout() const override;
  FILE *GetStderr() const override;
  const CMIUtilString &GetDriverName() const override;
  const CMIUtilString &GetDriverId() const override;
  void DeliverSignal(int signal) override;

  // Typedefs:
private:
  typedef std::queue<CMIUtilString> QueueStdinLine_t;

  // Methods:
private:
  /* ctor */ CMIDriver();
  /* ctor */ CMIDriver(const CMIDriver &);
  void operator=(const CMIDriver &);

  lldb::SBError ParseArgs(const int argc, const char *argv[], FILE *vpStdOut,
                          bool &vwbExiting);
  bool DoAppQuit();
  bool InterpretCommand(const CMIUtilString &vTextLine);
  bool InterpretCommandThisDriver(const CMIUtilString &vTextLine,
                                  bool &vwbCmdYesValid);
  CMIUtilString
  WrapCLICommandIntoMICommand(const CMIUtilString &vTextLine) const;
  bool InterpretCommandFallThruDriver(const CMIUtilString &vTextLine,
                                      bool &vwbCmdYesValid);
  bool ExecuteCommand(const SMICmdData &vCmdData);
  bool StartWorkerThreads();
  bool StopWorkerThreads();
  bool InitClientIDEToMIDriver() const;
  bool InitClientIDEEclipse() const;
  bool LocalDebugSessionStartupExecuteCommands();
  bool ExecuteCommandFile(const bool vbAsyncMode);

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMIDriver() override;

  // Attributes:
private:
  static const CMIUtilString ms_constAppNameShort;
  static const CMIUtilString ms_constAppNameLong;
  static const CMIUtilString ms_constMIVersion;
  //
  bool m_bFallThruToOtherDriverEnabled; // True = yes fall through, false = do
                                        // not pass on command
  CMIUtilThreadMutex m_threadMutex;
  bool m_bDriverIsExiting;  // True = yes, driver told to quit, false = continue
                            // working
  void *m_handleMainThread; // *this driver is run by the main thread
  CMICmnStreamStdin &m_rStdin;
  CMICmnLLDBDebugger &m_rLldbDebugger;
  CMICmnStreamStdout &m_rStdOut;
  DriverState_e m_eCurrentDriverState;
  bool m_bHaveExecutableFileNamePathOnCmdLine; // True = yes, executable given
                                               // as one of the parameters to
                                               // the MI Driver, false = not
                                               // found
  CMIUtilString m_strCmdLineArgExecuteableFileNamePath;
  bool m_bDriverDebuggingArgExecutable; // True = the MI Driver (MI mode) is
                                        // debugging executable passed as
                                        // argument,
  // false = running via a client (e.g. Eclipse)
  bool m_bHaveCommandFileNamePathOnCmdLine; // True = file with initial commands
                                            // given as one of the parameters to
                                            // the MI Driver, false = not found
  CMIUtilString m_strCmdLineArgCommandFileNamePath;
};
