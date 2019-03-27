//===-- MIDriver.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#include "lldb/API/SBError.h"
#include <cassert>
#include <csignal>
#include <fstream>

// In-house headers:
#include "MICmdArgValFile.h"
#include "MICmdArgValString.h"
#include "MICmdMgr.h"
#include "MICmnConfig.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnLog.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnResources.h"
#include "MICmnStreamStderr.h"
#include "MICmnStreamStdout.h"
#include "MICmnThreadMgrStd.h"
#include "MIDriver.h"
#include "MIUtilDebug.h"
#include "MIUtilSingletonHelper.h"

// Instantiations:
#if _DEBUG
const CMIUtilString CMIDriver::ms_constMIVersion =
    MIRSRC(IDS_MI_VERSION_DESCRIPTION_DEBUG);
#else
const CMIUtilString CMIDriver::ms_constMIVersion =
    MIRSRC(IDS_MI_VERSION_DESCRIPTION); // Matches version in resources file
#endif // _DEBUG
const CMIUtilString
    CMIDriver::ms_constAppNameShort(MIRSRC(IDS_MI_APPNAME_SHORT));
const CMIUtilString CMIDriver::ms_constAppNameLong(MIRSRC(IDS_MI_APPNAME_LONG));

//++
//------------------------------------------------------------------------------------
// Details: CMIDriver constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriver::CMIDriver()
    : m_bFallThruToOtherDriverEnabled(false), m_bDriverIsExiting(false),
      m_handleMainThread(0), m_rStdin(CMICmnStreamStdin::Instance()),
      m_rLldbDebugger(CMICmnLLDBDebugger::Instance()),
      m_rStdOut(CMICmnStreamStdout::Instance()),
      m_eCurrentDriverState(eDriverState_NotRunning),
      m_bHaveExecutableFileNamePathOnCmdLine(false),
      m_bDriverDebuggingArgExecutable(false),
      m_bHaveCommandFileNamePathOnCmdLine(false) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIDriver destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriver::~CMIDriver() {}

//++
//------------------------------------------------------------------------------------
// Details: Set whether *this driver (the parent) is enabled to pass a command
// to its
//          fall through (child) driver to interpret the command and do work
//          instead
//          (if *this driver decides it can't handle the command).
// Type:    Method.
// Args:    vbYes   - (R) True = yes fall through, false = do not pass on
// command.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::SetEnableFallThru(const bool vbYes) {
  m_bFallThruToOtherDriverEnabled = vbYes;
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get whether *this driver (the parent) is enabled to pass a command
// to its
//          fall through (child) driver to interpret the command and do work
//          instead
//          (if *this driver decides it can't handle the command).
// Type:    Method.
// Args:    None.
// Return:  bool - True = yes fall through, false = do not pass on command.
// Throws:  None.
//--
bool CMIDriver::GetEnableFallThru() const {
  return m_bFallThruToOtherDriverEnabled;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve MI's application name of itself.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetAppNameShort() const {
  return ms_constAppNameShort;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve MI's application name of itself.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetAppNameLong() const {
  return ms_constAppNameLong;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve MI's version description of itself.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetVersionDescription() const {
  return ms_constMIVersion;
}

//++
//------------------------------------------------------------------------------------
// Details: Initialize setup *this driver ready for use.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::Initialize() {
  m_eCurrentDriverState = eDriverState_Initialising;
  m_clientUsageRefCnt++;

  ClrErrorDescription();

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Initialize all of the modules we depend on
  MI::ModuleInit<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);
  MI::ModuleInit<CMICmnStreamStdout>(IDS_MI_INIT_ERR_STREAMSTDOUT, bOk, errMsg);
  MI::ModuleInit<CMICmnStreamStderr>(IDS_MI_INIT_ERR_STREAMSTDERR, bOk, errMsg);
  MI::ModuleInit<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleInit<CMICmnThreadMgrStd>(IDS_MI_INIT_ERR_THREADMANAGER, bOk,
                                     errMsg);
  MI::ModuleInit<CMICmnStreamStdin>(IDS_MI_INIT_ERR_STREAMSTDIN, bOk, errMsg);
  MI::ModuleInit<CMICmdMgr>(IDS_MI_INIT_ERR_CMDMGR, bOk, errMsg);
  bOk &= m_rLldbDebugger.SetDriver(*this);
  MI::ModuleInit<CMICmnLLDBDebugger>(IDS_MI_INIT_ERR_LLDBDEBUGGER, bOk, errMsg);

  m_bExitApp = false;

  m_bInitialized = bOk;

  if (!bOk) {
    const CMIUtilString msg =
        CMIUtilString::Format(MIRSRC(IDS_MI_INIT_ERR_DRIVER), errMsg.c_str());
    SetErrorDescription(msg);
    return MIstatus::failure;
  }

  m_eCurrentDriverState = eDriverState_RunningNotDebugging;

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Unbind detach or release resources used by *this driver.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  m_eCurrentDriverState = eDriverState_ShuttingDown;

  ClrErrorDescription();

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Shutdown all of the modules we depend on
  MI::ModuleShutdown<CMICmnLLDBDebugger>(IDS_MI_INIT_ERR_LLDBDEBUGGER, bOk,
                                         errMsg);
  MI::ModuleShutdown<CMICmdMgr>(IDS_MI_INIT_ERR_CMDMGR, bOk, errMsg);
  MI::ModuleShutdown<CMICmnStreamStdin>(IDS_MI_INIT_ERR_STREAMSTDIN, bOk,
                                        errMsg);
  MI::ModuleShutdown<CMICmnThreadMgrStd>(IDS_MI_INIT_ERR_THREADMANAGER, bOk,
                                         errMsg);
  MI::ModuleShutdown<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleShutdown<CMICmnStreamStderr>(IDS_MI_INIT_ERR_STREAMSTDERR, bOk,
                                         errMsg);
  MI::ModuleShutdown<CMICmnStreamStdout>(IDS_MI_INIT_ERR_STREAMSTDOUT, bOk,
                                         errMsg);
  MI::ModuleShutdown<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);

  if (!bOk) {
    SetErrorDescriptionn(MIRSRC(IDS_MI_SHUTDOWN_ERR), errMsg.c_str());
  }

  m_eCurrentDriverState = eDriverState_NotRunning;

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Work function. Client (the driver's user) is able to append their
// own message
//          in to the MI's Log trace file.
// Type:    Method.
// Args:    vMessage          - (R) Client's text message.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::WriteMessageToLog(const CMIUtilString &vMessage) {
  CMIUtilString msg;
  msg = CMIUtilString::Format(MIRSRC(IDS_MI_CLIENT_MSG), vMessage.c_str());
  return m_pLog->Write(msg, CMICmnLog::eLogVerbosity_ClientMsg);
}

//++
//------------------------------------------------------------------------------------
// Details: CDriverMgr calls *this driver initialize setup ready for use.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::DoInitialize() { return CMIDriver::Instance().Initialize(); }

//++
//------------------------------------------------------------------------------------
// Details: CDriverMgr calls *this driver to unbind detach or release resources
// used by
//          *this driver.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::DoShutdown() { return CMIDriver::Instance().Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the name for *this driver.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString & - Driver name.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetName() const {
  const CMIUtilString &rName = GetAppNameLong();
  const CMIUtilString &rVsn = GetVersionDescription();
  static CMIUtilString strName =
      CMIUtilString::Format("%s %s", rName.c_str(), rVsn.c_str());

  return strName;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve *this driver's last error condition.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString - Text description.
// Throws:  None.
//--
CMIUtilString CMIDriver::GetError() const { return GetErrorDescription(); }

//++
//------------------------------------------------------------------------------------
// Details: Call *this driver to return it's debugger.
// Type:    Overridden.
// Args:    None.
// Return:  lldb::SBDebugger & - LLDB debugger object reference.
// Throws:  None.
//--
lldb::SBDebugger &CMIDriver::GetTheDebugger() {
  return m_rLldbDebugger.GetTheDebugger();
}

//++
//------------------------------------------------------------------------------------
// Details: Specify another driver *this driver can call should this driver not
// be able
//          to handle the client data input. DoFallThruToAnotherDriver() makes
//          the call.
// Type:    Overridden.
// Args:    vrOtherDriver     - (R) Reference to another driver object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::SetDriverToFallThruTo(const CMIDriverBase &vrOtherDriver) {
  m_pDriverFallThru = const_cast<CMIDriverBase *>(&vrOtherDriver);

  return m_pDriverFallThru->SetDriverParent(*this);
}

//++
//------------------------------------------------------------------------------------
// Details: Proxy function CMIDriverMgr IDriver interface implementation. *this
// driver's
//          implementation called from here to match the existing function name
//          of the
//          original LLDB driver class (the extra indirection is not necessarily
//          required).
//          Check the arguments that were passed to this program to make sure
//          they are
//          valid and to get their argument values (if any).
// Type:    Overridden.
// Args:    argc        - (R)   An integer that contains the count of arguments
// that follow in
//                              argv. The argc parameter is always greater than
//                              or equal to 1.
//          argv        - (R)   An array of null-terminated strings representing
//          command-line
//                              arguments entered by the user of the program. By
//                              convention,
//                              argv[0] is the command with which the program is
//                              invoked.
//          vpStdOut    - (R)   Pointer to a standard output stream.
//          vwbExiting  - (W)   True = *this want to exit, Reasons: help,
//          invalid arg(s),
//                              version information only.
//                              False = Continue to work, start debugger i.e.
//                              Command
//                              interpreter.
// Return:  lldb::SBError - LLDB current error status.
// Throws:  None.
//--
lldb::SBError CMIDriver::DoParseArgs(const int argc, const char *argv[],
                                     FILE *vpStdOut, bool &vwbExiting) {
  return ParseArgs(argc, argv, vpStdOut, vwbExiting);
}

//++
//------------------------------------------------------------------------------------
// Details: Check the arguments that were passed to this program to make sure
// they are
//          valid and to get their argument values (if any). The following are
//          options
//          that are only handled by *this driver:
//              --executable <file>
//              --source <file> or -s <file>
//              --synchronous
//          The application's options --interpreter and --executable in code act
//          very similar.
//          The --executable is necessary to differentiate whether the MI Driver
//          is being
//          used by a client (e.g. Eclipse) or from the command line. Eclipse
//          issues the option
//          --interpreter and also passes additional arguments which can be
//          interpreted as an
//          executable if called from the command line. Using --executable tells
//          the MI Driver
//          it is being called from the command line and to prepare to launch
//          the executable
//          argument for a debug session. Using --interpreter on the command
//          line does not
//          issue additional commands to initialise a debug session.
//          Option --synchronous disables an asynchronous mode in the lldb-mi driver.
// Type:    Overridden.
// Args:    argc        - (R)   An integer that contains the count of arguments
// that follow in
//                              argv. The argc parameter is always greater than
//                              or equal to 1.
//          argv        - (R)   An array of null-terminated strings representing
//          command-line
//                              arguments entered by the user of the program. By
//                              convention,
//                              argv[0] is the command with which the program is
//                              invoked.
//          vpStdOut    - (R)   Pointer to a standard output stream.
//          vwbExiting  - (W)   True = *this want to exit, Reasons: help,
//          invalid arg(s),
//                              version information only.
//                              False = Continue to work, start debugger i.e.
//                              Command
//                              interpreter.
// Return:  lldb::SBError - LLDB current error status.
// Throws:  None.
//--
lldb::SBError CMIDriver::ParseArgs(const int argc, const char *argv[],
                                   FILE *vpStdOut, bool &vwbExiting) {
  lldb::SBError errStatus;
  const bool bHaveArgs(argc >= 2);

  // *** Add any args handled here to GetHelpOnCmdLineArgOptions() ***

  // CODETAG_MIDRIVE_CMD_LINE_ARG_HANDLING
  // Look for the command line options
  bool bHaveExecutableFileNamePath = false;
  bool bHaveExecutableLongOption = false;

  if (bHaveArgs) {
    // Search right to left to look for filenames
    for (MIint i = argc - 1; i > 0; i--) {
      const CMIUtilString strArg(argv[i]);
      const CMICmdArgValFile argFile;

      // Check for a filename
      if (argFile.IsFilePath(strArg) ||
          CMICmdArgValString(true, false, true).IsStringArg(strArg)) {
        // Is this the command file for the '-s' or '--source' options?
        const CMIUtilString strPrevArg(argv[i - 1]);
        if (strPrevArg == "-s" || strPrevArg == "--source") {
          m_strCmdLineArgCommandFileNamePath = strArg;
          m_bHaveCommandFileNamePathOnCmdLine = true;
          i--; // skip '-s' on the next loop
          continue;
        }
        // Else, must be the executable
        bHaveExecutableFileNamePath = true;
        m_strCmdLineArgExecuteableFileNamePath = strArg;
        m_bHaveExecutableFileNamePathOnCmdLine = true;
      }
      // Report error if no command file was specified for the '-s' or
      // '--source' options
      else if (strArg == "-s" || strArg == "--source") {
        vwbExiting = true;
        const CMIUtilString errMsg = CMIUtilString::Format(
            MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_MISSING_INF), strArg.c_str());
        errStatus.SetErrorString(errMsg.c_str());
        break;
      }
      // This argument is also checked for in CMIDriverMgr::ParseArgs()
      else if (strArg == "--executable") // Used to specify that
                                         // there is executable
                                         // argument also on the
                                         // command line
      {                                  // See fn description.
        bHaveExecutableLongOption = true;
      } else if (strArg == "--synchronous") {
        CMICmnLLDBDebugSessionInfo::Instance().GetDebugger().SetAsync(false);
      }
    }
  }

  if (bHaveExecutableFileNamePath && bHaveExecutableLongOption) {
    SetDriverDebuggingArgExecutable();
  }

  return errStatus;
}

//++
//------------------------------------------------------------------------------------
// Details: A client can ask if *this driver is GDB/MI compatible.
// Type:    Overridden.
// Args:    None.
// Return:  True - GBD/MI compatible LLDB front end.
//          False - Not GBD/MI compatible LLDB front end.
// Throws:  None.
//--
bool CMIDriver::GetDriverIsGDBMICompatibleDriver() const { return true; }

//++
//------------------------------------------------------------------------------------
// Details: Start worker threads for the driver.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::StartWorkerThreads() {
  bool bOk = MIstatus::success;

  // Grab the thread manager
  CMICmnThreadMgrStd &rThreadMgr = CMICmnThreadMgrStd::Instance();

  // Start the event polling thread
  if (bOk && !rThreadMgr.ThreadStart<CMICmnLLDBDebugger>(m_rLldbDebugger)) {
    const CMIUtilString errMsg = CMIUtilString::Format(
        MIRSRC(IDS_THREADMGR_ERR_THREAD_FAIL_CREATE),
        CMICmnThreadMgrStd::Instance().GetErrorDescription().c_str());
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Stop worker threads for the driver.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::StopWorkerThreads() {
  CMICmnThreadMgrStd &rThreadMgr = CMICmnThreadMgrStd::Instance();
  return rThreadMgr.ThreadAllTerminate();
}

//++
//------------------------------------------------------------------------------------
// Details: Call this function puts *this driver to work.
//          This function is used by the application's main thread.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::DoMainLoop() {
  if (!InitClientIDEToMIDriver()) // Init Eclipse IDE
  {
    SetErrorDescriptionn(MIRSRC(IDS_MI_INIT_ERR_CLIENT_USING_DRIVER));
    return MIstatus::failure;
  }

  if (!StartWorkerThreads())
    return MIstatus::failure;

  bool bOk = MIstatus::success;

  if (HaveExecutableFileNamePathOnCmdLine()) {
    if (!LocalDebugSessionStartupExecuteCommands()) {
      SetErrorDescription(MIRSRC(IDS_MI_INIT_ERR_LOCAL_DEBUG_SESSION));
      bOk = MIstatus::failure;
    }
  }

  // App is not quitting currently
  m_bExitApp = false;

  // Handle source file
  if (m_bHaveCommandFileNamePathOnCmdLine) {
    const bool bAsyncMode = false;
    ExecuteCommandFile(bAsyncMode);
  }

  // While the app is active
  while (bOk && !m_bExitApp) {
    CMIUtilString errorText;
    const char *pCmd = m_rStdin.ReadLine(errorText);
    if (pCmd != nullptr) {
      CMIUtilString lineText(pCmd);
      if (!lineText.empty()) {
        // Check that the handler thread is alive (otherwise we stuck here)
        assert(CMICmnLLDBDebugger::Instance().ThreadIsActive());

        {
          // Lock Mutex before processing commands so that we don't disturb an
          // event
          // being processed
          CMIUtilThreadLock lock(
              CMICmnLLDBDebugSessionInfo::Instance().GetSessionMutex());
          bOk = InterpretCommand(lineText);
        }

        // Draw prompt if desired
        bOk = bOk && CMICmnStreamStdout::WritePrompt();

        // Wait while the handler thread handles incoming events
        CMICmnLLDBDebugger::Instance().WaitForHandleEvent();
      }
    }
  }

  // Signal that the application is shutting down
  DoAppQuit();

  // Close and wait for the workers to stop
  StopWorkerThreads();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Set things in motion, set state etc that brings *this driver (and
// the
//          application) to a tidy shutdown.
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::DoAppQuit() {
  bool bYesQuit = true;

  // Shutdown stuff, ready app for exit
  {
    CMIUtilThreadLock lock(m_threadMutex);
    m_bDriverIsExiting = true;
  }

  return bYesQuit;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver passes text commands to a fall through driver is it
// does not
//          understand them (the LLDB driver).
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    vTextLine           - (R) Text data representing a possible command.
//          vwbCmdYesValid      - (W) True = Command valid, false = command not
//          handled.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::InterpretCommandFallThruDriver(const CMIUtilString &vTextLine,
                                               bool &vwbCmdYesValid) {
  MIunused(vTextLine);
  MIunused(vwbCmdYesValid);

  // ToDo: Implement when less urgent work to be done or decide remove as not
  // required
  // bool bOk = MIstatus::success;
  // bool bCmdNotUnderstood = true;
  // if( bCmdNotUnderstood && GetEnableFallThru() )
  //{
  //  CMIUtilString errMsg;
  //  bOk = DoFallThruToAnotherDriver( vStdInBuffer, errMsg );
  //  if( !bOk )
  //  {
  //      errMsg = errMsg.StripCREndOfLine();
  //      errMsg = errMsg.StripCRAll();
  //      const CMIDriverBase * pOtherDriver = GetDriverToFallThruTo();
  //      const char * pName = pOtherDriver->GetDriverName().c_str();
  //      const char * pId = pOtherDriver->GetDriverId().c_str();
  //      const CMIUtilString msg( CMIUtilString::Format( MIRSRC(
  //      IDS_DRIVER_ERR_FALLTHRU_DRIVER_ERR ), pName, pId, errMsg.c_str() )
  //);
  //      m_pLog->WriteMsg( msg );
  //  }
  //}
  //
  // vwbCmdYesValid = bOk;
  // CMIUtilString strNot;
  // if( vwbCmdYesValid)
  //  strNot = CMIUtilString::Format( "%s ", MIRSRC( IDS_WORD_NOT ) );
  // const CMIUtilString msg( CMIUtilString::Format( MIRSRC(
  // IDS_FALLTHRU_DRIVER_CMD_RECEIVED ), vTextLine.c_str(), strNot.c_str() ) );
  // m_pLog->WriteLog( msg );

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the name for *this driver.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString & - Driver name.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetDriverName() const { return GetName(); }

//++
//------------------------------------------------------------------------------------
// Details: Get the unique ID for *this driver.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetDriverId() const { return GetId(); }

//++
//------------------------------------------------------------------------------------
// Details: This function allows *this driver to call on another driver to
// perform work
//          should this driver not be able to handle the client data input.
//          SetDriverToFallThruTo() specifies the fall through to driver.
//          Check the error message if the function returns a failure.
// Type:    Overridden.
// Args:    vCmd        - (R) Command instruction to interpret.
//          vwErrMsg    - (W) Status description on command failing.
// Return:  MIstatus::success - Command succeeded.
//          MIstatus::failure - Command failed.
// Throws:  None.
//--
bool CMIDriver::DoFallThruToAnotherDriver(const CMIUtilString &vCmd,
                                          CMIUtilString &vwErrMsg) {
  bool bOk = MIstatus::success;

  CMIDriverBase *pOtherDriver = GetDriverToFallThruTo();
  if (pOtherDriver == nullptr)
    return bOk;

  return pOtherDriver->DoFallThruToAnotherDriver(vCmd, vwErrMsg);
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a file stream to other drivers on which *this
// driver
//          write's out to and they read as expected input. *this driver is
//          passing
//          through commands to the (child) pass through assigned driver.
// Type:    Overrdidden.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriver::GetStdin() const {
  // Note this fn is called on CMIDriverMgr register driver so stream has to be
  // available before *this driver has been initialized! Flaw?

  // This very likely to change later to a stream that the pass thru driver
  // will read and we write to give it 'input'
  return stdin;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a file stream to other pass through assigned
// drivers
//          so they know what to write to.
// Type:    Overidden.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriver::GetStdout() const {
  // Note this fn is called on CMIDriverMgr register driver so stream has to be
  // available before *this driver has been initialized! Flaw?

  // Do not want to pass through driver to write to stdout
  return NULL;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a error file stream to other pass through
// assigned drivers
//          so they know what to write to.
// Type:    Overidden.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriver::GetStderr() const {
  // Note this fn is called on CMIDriverMgr register driver so stream has to be
  // available before *this driver has been initialized! Flaw?

  // This very likely to change later to a stream that the pass thru driver
  // will write to and *this driver reads from to pass on the CMICmnLog object
  return stderr;
}

//++
//------------------------------------------------------------------------------------
// Details: Set a unique ID for *this driver. It cannot be empty.
// Type:    Overridden.
// Args:    vId - (R) Text description.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::SetId(const CMIUtilString &vId) {
  if (vId.empty()) {
    SetErrorDescriptionn(MIRSRC(IDS_DRIVER_ERR_ID_INVALID), GetName().c_str(),
                         vId.c_str());
    return MIstatus::failure;
  }

  m_strDriverId = vId;
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get the unique ID for *this driver.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString & - Text description.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetId() const { return m_strDriverId; }

//++
//------------------------------------------------------------------------------------
// Details: Interpret the text data and match against current commands to see if
// there
//          is a match. If a match then the command is issued and actioned on.
//          The
//          text data if not understood by *this driver is past on to the Fall
//          Thru
//          driver.
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    vTextLine   - (R) Text data representing a possible command.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::InterpretCommand(const CMIUtilString &vTextLine) {
  const bool bNeedToRebroadcastStopEvent =
      m_rLldbDebugger.CheckIfNeedToRebroadcastStopEvent();
  bool bCmdYesValid = false;
  bool bOk = InterpretCommandThisDriver(vTextLine, bCmdYesValid);
  if (bOk && !bCmdYesValid)
    bOk = InterpretCommandFallThruDriver(vTextLine, bCmdYesValid);

  if (bNeedToRebroadcastStopEvent)
    m_rLldbDebugger.RebroadcastStopEvent();

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Helper function for CMIDriver::InterpretCommandThisDriver.
//          Convert a CLI command to MI command (just wrap any CLI command
//          into "<tokens>-interpreter-exec command \"<CLI command>\"").
// Type:    Method.
// Args:    vTextLine   - (R) Text data representing a possible command.
// Return:  CMIUtilString   - The original MI command or converted CLI command.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
CMIUtilString
CMIDriver::WrapCLICommandIntoMICommand(const CMIUtilString &vTextLine) const {
  // Tokens contain following digits
  static const CMIUtilString digits("0123456789");

  // Consider an algorithm on the following example:
  // 001-file-exec-and-symbols "/path/to/file"
  //
  // 1. Skip a command token
  // For example:
  // 001-file-exec-and-symbols "/path/to/file"
  // 001target create "/path/to/file"
  //    ^ -- command starts here (in both cases)
  // Also possible case when command not found:
  // 001
  //    ^ -- i.e. only tokens are present (or empty string at all)
  const size_t nCommandOffset = vTextLine.find_first_not_of(digits);

  // 2. Check if command is empty
  // For example:
  // 001-file-exec-and-symbols "/path/to/file"
  // 001target create "/path/to/file"
  //    ^ -- command not empty (in both cases)
  // or:
  // 001
  //    ^ -- command wasn't found
  const bool bIsEmptyCommand = (nCommandOffset == CMIUtilString::npos);

  // 3. Check and exit if it isn't a CLI command
  // For example:
  // 001-file-exec-and-symbols "/path/to/file"
  // 001
  //    ^ -- it isn't CLI command (in both cases)
  // or:
  // 001target create "/path/to/file"
  //    ^ -- it's CLI command
  const bool bIsCliCommand =
      !bIsEmptyCommand && (vTextLine.at(nCommandOffset) != '-');
  if (!bIsCliCommand)
    return vTextLine;

  // 4. Wrap CLI command to make it MI-compatible
  //
  // 001target create "/path/to/file"
  // ^^^ -- token
  const std::string vToken(vTextLine.begin(),
                           vTextLine.begin() + nCommandOffset);
  // 001target create "/path/to/file"
  //    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ -- CLI command
  const CMIUtilString vCliCommand(std::string(vTextLine, nCommandOffset));

  // 5. Escape special characters and embed the command in a string
  // Result: it looks like -- target create \"/path/to/file\".
  const std::string vShieldedCliCommand(vCliCommand.AddSlashes());

  // 6. Turn the CLI command into an MI command, as in:
  // 001-interpreter-exec command "target create \"/path/to/file\""
  // ^^^ -- token
  //    ^^^^^^^^^^^^^^^^^^^^^^^^^^^                               ^ -- wrapper
  //                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ -- shielded
  //                               CLI command
  return CMIUtilString::Format("%s-interpreter-exec command \"%s\"",
                               vToken.c_str(), vShieldedCliCommand.c_str());
}

//++
//------------------------------------------------------------------------------------
// Details: Interpret the text data and match against current commands to see if
// there
//          is a match. If a match then the command is issued and actioned on.
//          If a
//          command cannot be found to match then vwbCmdYesValid is set to false
//          and
//          nothing else is done here.
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    vTextLine           - (R) Text data representing a possible command.
//          vwbCmdYesValid      - (W) True = Command valid, false = command not
//          handled.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::InterpretCommandThisDriver(const CMIUtilString &vTextLine,
                                           bool &vwbCmdYesValid) {
  // Convert any CLI commands into MI commands
  const CMIUtilString vMITextLine(WrapCLICommandIntoMICommand(vTextLine));

  vwbCmdYesValid = false;
  bool bCmdNotInCmdFactor = false;
  SMICmdData cmdData;
  CMICmdMgr &rCmdMgr = CMICmdMgr::Instance();
  if (!rCmdMgr.CmdInterpret(vMITextLine, vwbCmdYesValid, bCmdNotInCmdFactor,
                            cmdData))
    return MIstatus::failure;

  if (vwbCmdYesValid) {
    // For debugging only
    // m_pLog->WriteLog( cmdData.strMiCmdAll.c_str() );

    return ExecuteCommand(cmdData);
  }

  // Check for escape character, may be cursor control characters
  // This code is not necessary for application operation, just want to keep
  // tabs on what
  // has been given to the driver to try and interpret.
  if (vMITextLine.at(0) == 27) {
    CMIUtilString logInput(MIRSRC(IDS_STDIN_INPUT_CTRL_CHARS));
    for (MIuint i = 0; i < vMITextLine.length(); i++) {
      logInput += CMIUtilString::Format("%d ", vMITextLine.at(i));
    }
    m_pLog->WriteLog(logInput);
    return MIstatus::success;
  }

  // Write to the Log that a 'command' was not valid.
  // Report back to the MI client via MI result record.
  CMIUtilString strNotInCmdFactory;
  if (bCmdNotInCmdFactor)
    strNotInCmdFactory = CMIUtilString::Format(
        MIRSRC(IDS_DRIVER_CMD_NOT_IN_FACTORY), cmdData.strMiCmd.c_str());
  const CMIUtilString strNot(
      CMIUtilString::Format("%s ", MIRSRC(IDS_WORD_NOT)));
  const CMIUtilString msg(CMIUtilString::Format(
      MIRSRC(IDS_DRIVER_CMD_RECEIVED), vMITextLine.c_str(), strNot.c_str(),
      strNotInCmdFactory.c_str()));
  const CMICmnMIValueConst vconst = CMICmnMIValueConst(msg);
  const CMICmnMIValueResult valueResult("msg", vconst);
  const CMICmnMIResultRecord miResultRecord(
      cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
      valueResult);
  const bool bOk = m_rStdOut.WriteMIResponse(miResultRecord.GetString());

  // Proceed to wait for or execute next command
  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Having previously had the potential command validated and found
// valid now
//          get the command executed.
//          This function is used by the application's main thread.
// Type:    Method.
// Args:    vCmdData    - (RW) Command meta data.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriver::ExecuteCommand(const SMICmdData &vCmdData) {
  CMICmdMgr &rCmdMgr = CMICmdMgr::Instance();
  return rCmdMgr.CmdExecute(vCmdData);
}

//++
//------------------------------------------------------------------------------------
// Details: Set the MI Driver's exit application flag. The application checks
// this flag
//          after every stdin line is read so the exit may not be instantaneous.
//          If vbForceExit is false the MI Driver queries its state and
//          determines if is
//          should exit or continue operating depending on that running state.
//          This is related to the running state of the MI driver.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIDriver::SetExitApplicationFlag(const bool vbForceExit) {
  if (vbForceExit) {
    CMIUtilThreadLock lock(m_threadMutex);
    m_bExitApp = true;
    return;
  }

  // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM
  // Did we receive a SIGINT from the client during a running debug program, if
  // so then SIGINT is not to be taken as meaning kill the MI driver application
  // but halt the inferior program being debugged instead
  if (m_eCurrentDriverState == eDriverState_RunningDebugging) {
    InterpretCommand("-exec-interrupt");
    return;
  }

  m_bExitApp = true;
}

//++
//------------------------------------------------------------------------------------
// Details: Get the  MI Driver's exit exit application flag.
//          This is related to the running state of the MI driver.
// Type:    Method.
// Args:    None.
// Return:  bool    - True = MI Driver is shutting down, false = MI driver is
// running.
// Throws:  None.
//--
bool CMIDriver::GetExitApplicationFlag() const { return m_bExitApp; }

//++
//------------------------------------------------------------------------------------
// Details: Get the current running state of the MI Driver.
// Type:    Method.
// Args:    None.
// Return:  DriverState_e   - The current running state of the application.
// Throws:  None.
//--
CMIDriver::DriverState_e CMIDriver::GetCurrentDriverState() const {
  return m_eCurrentDriverState;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the current running state of the MI Driver to running and
// currently not in
//          a debug session.
// Type:    Method.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Return:  DriverState_e   - The current running state of the application.
// Throws:  None.
//--
bool CMIDriver::SetDriverStateRunningNotDebugging() {
  // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM

  if (m_eCurrentDriverState == eDriverState_RunningNotDebugging)
    return MIstatus::success;

  // Driver cannot be in the following states to set
  // eDriverState_RunningNotDebugging
  switch (m_eCurrentDriverState) {
  case eDriverState_NotRunning:
  case eDriverState_Initialising:
  case eDriverState_ShuttingDown: {
    SetErrorDescription(MIRSRC(IDS_DRIVER_ERR_DRIVER_STATE_ERROR));
    return MIstatus::failure;
  }
  case eDriverState_RunningDebugging:
  case eDriverState_RunningNotDebugging:
    break;
  case eDriverState_count:
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_CODE_ERR_INVALID_ENUMERATION_VALUE),
                              "SetDriverStateRunningNotDebugging()"));
    return MIstatus::failure;
  }

  // Driver must be in this state to set eDriverState_RunningNotDebugging
  if (m_eCurrentDriverState != eDriverState_RunningDebugging) {
    SetErrorDescription(MIRSRC(IDS_DRIVER_ERR_DRIVER_STATE_ERROR));
    return MIstatus::failure;
  }

  m_eCurrentDriverState = eDriverState_RunningNotDebugging;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the current running state of the MI Driver to running and
// currently not in
//          a debug session. The driver's state must in the state running and in
//          a
//          debug session to set this new state.
// Type:    Method.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Return:  DriverState_e   - The current running state of the application.
// Throws:  None.
//--
bool CMIDriver::SetDriverStateRunningDebugging() {
  // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM

  if (m_eCurrentDriverState == eDriverState_RunningDebugging)
    return MIstatus::success;

  // Driver cannot be in the following states to set
  // eDriverState_RunningDebugging
  switch (m_eCurrentDriverState) {
  case eDriverState_NotRunning:
  case eDriverState_Initialising:
  case eDriverState_ShuttingDown: {
    SetErrorDescription(MIRSRC(IDS_DRIVER_ERR_DRIVER_STATE_ERROR));
    return MIstatus::failure;
  }
  case eDriverState_RunningDebugging:
  case eDriverState_RunningNotDebugging:
    break;
  case eDriverState_count:
    SetErrorDescription(
        CMIUtilString::Format(MIRSRC(IDS_CODE_ERR_INVALID_ENUMERATION_VALUE),
                              "SetDriverStateRunningDebugging()"));
    return MIstatus::failure;
  }

  // Driver must be in this state to set eDriverState_RunningDebugging
  if (m_eCurrentDriverState != eDriverState_RunningNotDebugging) {
    SetErrorDescription(MIRSRC(IDS_DRIVER_ERR_DRIVER_STATE_ERROR));
    return MIstatus::failure;
  }

  m_eCurrentDriverState = eDriverState_RunningDebugging;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Prepare the client IDE so it will start working/communicating with
// *this MI
//          driver.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMIDriver::InitClientIDEToMIDriver() const {
  // Put other IDE init functions here
  return InitClientIDEEclipse();
}

//++
//------------------------------------------------------------------------------------
// Details: The IDE Eclipse when debugging locally expects "(gdb)\n" character
//          sequence otherwise it refuses to communicate and times out. This
//          should be
//          sent to Eclipse before anything else.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMIDriver::InitClientIDEEclipse() const {
  return CMICmnStreamStdout::WritePrompt();
}

//++
//------------------------------------------------------------------------------------
// Details: Ask *this driver whether it found an executable in the MI Driver's
// list of
//          arguments which to open and debug. If so instigate commands to set
//          up a debug
//          session for that executable.
// Type:    Method.
// Args:    None.
// Return:  bool - True = True = Yes executable given as one of the parameters
// to the MI
//                 Driver.
//                 False = not found.
// Throws:  None.
//--
bool CMIDriver::HaveExecutableFileNamePathOnCmdLine() const {
  return m_bHaveExecutableFileNamePathOnCmdLine;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve from *this driver executable file name path to start a
// debug session
//          with (if present see HaveExecutableFileNamePathOnCmdLine()).
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Executeable file name path or empty string.
// Throws:  None.
//--
const CMIUtilString &CMIDriver::GetExecutableFileNamePathOnCmdLine() const {
  return m_strCmdLineArgExecuteableFileNamePath;
}

//++
//------------------------------------------------------------------------------------
// Details: Execute commands (by injecting them into the stdin line queue
// container) and
//          other code to set up the MI Driver such that is can take the
//          executable
//          argument passed on the command and create a debug session for it.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMIDriver::LocalDebugSessionStartupExecuteCommands() {
  const CMIUtilString strCmd(CMIUtilString::Format(
      "-file-exec-and-symbols \"%s\"",
      m_strCmdLineArgExecuteableFileNamePath.AddSlashes().c_str()));
  bool bOk = CMICmnStreamStdout::TextToStdout(strCmd);
  bOk = bOk && InterpretCommand(strCmd);
  bOk = bOk && CMICmnStreamStdout::WritePrompt();
  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the MI Driver into "its debugging an executable passed as an
// argument"
//          mode as against running via a client like Eclipse.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIDriver::SetDriverDebuggingArgExecutable() {
  m_bDriverDebuggingArgExecutable = true;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the MI Driver state indicating if it is operating in "its
// debugging
//          an executable passed as an argument" mode as against running via a
//          client
//          like Eclipse.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
bool CMIDriver::IsDriverDebuggingArgExecutable() const {
  return m_bDriverDebuggingArgExecutable;
}

//++
//------------------------------------------------------------------------------------
// Details: Execute commands from command source file in specified mode, and
//          set exit-flag if needed.
// Type:    Method.
// Args:    vbAsyncMode       - (R) True = execute commands in asynchronous
// mode, false = otherwise.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMIDriver::ExecuteCommandFile(const bool vbAsyncMode) {
  std::ifstream ifsStartScript(m_strCmdLineArgCommandFileNamePath.c_str());
  if (!ifsStartScript.is_open()) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_UTIL_FILE_ERR_OPENING_FILE_UNKNOWN),
                              m_strCmdLineArgCommandFileNamePath.c_str()));
    SetErrorDescription(errMsg.c_str());
    const bool bForceExit = true;
    SetExitApplicationFlag(bForceExit);
    return MIstatus::failure;
  }

  // Switch lldb to synchronous mode
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  const bool bAsyncSetting = rSessionInfo.GetDebugger().GetAsync();
  rSessionInfo.GetDebugger().SetAsync(vbAsyncMode);

  // Execute commands from file
  bool bOk = MIstatus::success;
  CMIUtilString strCommand;
  while (!m_bExitApp && std::getline(ifsStartScript, strCommand)) {
    // Print command
    bOk = CMICmnStreamStdout::TextToStdout(strCommand);

    // Skip if it's a comment or empty line
    if (strCommand.empty() || strCommand[0] == '#')
      continue;

    // Execute if no error
    if (bOk) {
      CMIUtilThreadLock lock(rSessionInfo.GetSessionMutex());
      bOk = InterpretCommand(strCommand);
    }

    // Draw the prompt after command will be executed (if enabled)
    bOk = bOk && CMICmnStreamStdout::WritePrompt();

    // Exit if there is an error
    if (!bOk) {
      const bool bForceExit = true;
      SetExitApplicationFlag(bForceExit);
      break;
    }

    // Wait while the handler thread handles incoming events
    CMICmnLLDBDebugger::Instance().WaitForHandleEvent();
  }

  // Switch lldb back to initial mode
  rSessionInfo.GetDebugger().SetAsync(bAsyncSetting);

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Gets called when lldb-mi gets a signal. Stops the process if it was
// SIGINT.
//
// Type:    Method.
// Args:    signal that was delivered
// Return:  None.
// Throws:  None.
//--
void CMIDriver::DeliverSignal(int signal) {
  if (signal == SIGINT &&
      (m_eCurrentDriverState == eDriverState_RunningDebugging))
    InterpretCommand("-exec-interrupt");
}
