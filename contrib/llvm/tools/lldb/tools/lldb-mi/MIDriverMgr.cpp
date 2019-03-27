//===-- MIDriverMgr.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third Party Headers:
#include "lldb/API/SBError.h"

// In-house headers:
#include "MICmnLog.h"
#include "MICmnLogMediumFile.h"
#include "MICmnResources.h"
#include "MICmnStreamStdout.h"
#include "MIDriver.h"
#include "MIDriverMgr.h"
#include "MIUtilSingletonHelper.h"

//++
//------------------------------------------------------------------------------------
// Details: CMIDriverMgr constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriverMgr::CMIDriverMgr() : m_pDriverCurrent(nullptr), m_bInMi2Mode(false) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIDriverMgr destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriverMgr::~CMIDriverMgr() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize *this manager.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::Initialize() {
  m_clientUsageRefCnt++;

  ClrErrorDescription();

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Note initialisation order is important here as some resources depend on
  // previous
  MI::ModuleInit<CMICmnLog>(IDS_MI_INIT_ERR_LOG, bOk, errMsg);
  MI::ModuleInit<CMICmnResources>(IDS_MI_INIT_ERR_RESOURCES, bOk, errMsg);

  m_bInitialized = bOk;

  if (!bOk) {
    CMIUtilString strInitError(CMIUtilString::Format(
        MIRSRC(IDS_MI_INIT_ERR_DRIVERMGR), errMsg.c_str()));
    SetErrorDescription(strInitError);
    return MIstatus::failure;
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Unbind detach or release resources used by this server in general
// common
//          functionality shared between versions of any server interfaces
//          implemented.
// Type:    Method.
// Args:    vbAppExitOk - (R) True = No problems, false = App exiting with
// problems (investigate!).
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::Shutdown() {
  // Do not want a ref counter because this function needs to be called how ever
  // this
  // application stops running
  // if( --m_clientUsageRefCnt > 0 )
  //  return MIstatus::success;

  ClrErrorDescription();

  if (!m_bInitialized)
    return MIstatus::success;

  m_bInitialized = false;

  bool bOk = MIstatus::success;
  CMIUtilString errMsg;

  // Tidy up
  UnregisterDriverAll();

  // Note shutdown order is important here
  MI::ModuleShutdown<CMICmnResources>(IDE_MI_SHTDWN_ERR_RESOURCES, bOk, errMsg);
  MI::ModuleShutdown<CMICmnLog>(IDS_MI_SHTDWN_ERR_LOG, bOk, errMsg);

  if (!bOk) {
    SetErrorDescriptionn(MIRSRC(IDS_MI_SHTDWN_ERR_DRIVERMGR), errMsg.c_str());
  }

  return bOk;
}
//++
//------------------------------------------------------------------------------------
// Details: Unregister all the Driver registered with *this manager. The manager
// also
//          deletes
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::UnregisterDriverAll() {
  MapDriverIdToDriver_t::const_iterator it = m_mapDriverIdToDriver.begin();
  while (it != m_mapDriverIdToDriver.end()) {
    IDriver *pDriver = (*it).second;
    pDriver->DoShutdown();

    // Next
    ++it;
  }

  m_mapDriverIdToDriver.clear();
  m_pDriverCurrent = NULL;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Register a driver with *this Driver Manager. Call
// SetUseThisDriverToDoWork()
//          inform the manager which driver is the one to the work. The manager
//          calls
//          the driver's init function which must be successful in order to
//          complete the
//          registration.
// Type:    Method.
// Args:    vrDriver    - (R) The driver to register.
//          vrDriverID  - (R) The driver's ID to lookup by.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::RegisterDriver(const IDriver &vrDriver,
                                  const CMIUtilString &vrDriverID) {
  if (HaveDriverAlready(vrDriver))
    return MIstatus::success;

  IDriver *pDriver = const_cast<IDriver *>(&vrDriver);
  if (!pDriver->SetId(vrDriverID))
    return MIstatus::failure;
  if (!pDriver->DoInitialize()) {
    SetErrorDescriptionn(MIRSRC(IDS_DRIVERMGR_DRIVER_ERR_INIT),
                         pDriver->GetName().c_str(), vrDriverID.c_str(),
                         pDriver->GetError().c_str());
    return MIstatus::failure;
  }

  MapPairDriverIdToDriver_t pr(vrDriverID, pDriver);
  m_mapDriverIdToDriver.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Query the Driver Manager to see if *this manager has the driver
// already
//          registered.
// Type:    Method.
// Args:    vrDriver    - (R) The driver to query.
// Return:  True - registered.
//          False - not registered.
// Throws:  None.
//--
bool CMIDriverMgr::HaveDriverAlready(const IDriver &vrDriver) const {
  MapDriverIdToDriver_t::const_iterator it = m_mapDriverIdToDriver.begin();
  while (it != m_mapDriverIdToDriver.end()) {
    const IDriver *pDrvr = (*it).second;
    if (pDrvr == &vrDriver)
      return true;

    // Next
    ++it;
  }

  return false;
}

//++
//------------------------------------------------------------------------------------
// Details: Unregister a driver from the Driver Manager. Call the
// SetUseThisDriverToDoWork()
//          function to define another driver to do work if the one being
//          unregistered did
//          the work previously.
// Type:    Method.
// Args:    vrDriver    - (R) The driver to unregister.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::UnregisterDriver(const IDriver &vrDriver) {
  const IDriver *pDrvr = nullptr;
  MapDriverIdToDriver_t::const_iterator it = m_mapDriverIdToDriver.begin();
  while (it != m_mapDriverIdToDriver.end()) {
    pDrvr = (*it).second;
    if (pDrvr == &vrDriver)
      break;

    // Next
    ++it;
  }
  m_mapDriverIdToDriver.erase(it);

  if (m_pDriverCurrent == pDrvr)
    m_pDriverCurrent = nullptr;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Specify the driver to do work. The Driver Manager drives this
// driver. Any
//          previous driver doing work is not called anymore (so be sure the
//          previous
//          driver is in a tidy state before stopping it working).
// Type:    Method.
// Args:    vrADriver   - (R) A lldb::SBBroadcaster/IDriver derived object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::SetUseThisDriverToDoWork(const IDriver &vrADriver) {
  m_pDriverCurrent = const_cast<IDriver *>(&vrADriver);

  const CMIUtilString msg(
      CMIUtilString::Format(MIRSRC(IDS_DRIVER_SAY_DRIVER_USING),
                            m_pDriverCurrent->GetName().c_str()));
  m_pLog->Write(msg, CMICmnLog::eLogVerbosity_Log);

  m_bInMi2Mode = m_pDriverCurrent->GetDriverIsGDBMICompatibleDriver();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Ask *this manager which driver is currently doing the work.
// Type:    Method.
// Args:    None.
// Return:  IDriver * - Pointer to a driver, NULL if there is no current working
// driver.
// Throws:  None.
//--
CMIDriverMgr::IDriver *CMIDriverMgr::GetUseThisDriverToDoWork() const {
  return m_pDriverCurrent;
}

//++
//------------------------------------------------------------------------------------
// Details: Call this function puts *this driver to work.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::DriverMainLoop() {
  if (m_pDriverCurrent != nullptr) {
    if (!m_pDriverCurrent->DoMainLoop()) {
      const CMIUtilString errMsg(
          CMIUtilString::Format(MIRSRC(IDS_DRIVER_ERR_MAINLOOP),
                                m_pDriverCurrent->GetError().c_str()));
      CMICmnStreamStdout::Instance().Write(errMsg, true);
      return MIstatus::failure;
    }
  } else {
    const CMIUtilString errMsg(MIRSRC(IDS_DRIVER_ERR_CURRENT_NOT_SET));
    CMICmnStreamStdout::Instance().Write(errMsg, true);
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get the current driver to validate executable command line
// arguments.
// Type:    Method.
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
//          vpStdOut    - (R)   Point to a standard output stream.
//          vwbExiting  - (W)   True = *this want to exit, false = continue to
//          work.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverMgr::DriverParseArgs(const int argc, const char *argv[],
                                   FILE *vpStdOut, bool &vwbExiting) {
  if (m_pDriverCurrent == nullptr) {
    const CMIUtilString errMsg(MIRSRC(IDS_DRIVER_ERR_CURRENT_NOT_SET));
    CMICmnStreamStdout::Instance().Write(errMsg, true);
    return MIstatus::failure;
  }

  const lldb::SBError error(
      m_pDriverCurrent->DoParseArgs(argc, argv, vpStdOut, vwbExiting));
  bool bOk = !error.Fail();
  if (!bOk) {
    CMIUtilString errMsg;
    const char *pErrorCstr = error.GetCString();
    if (pErrorCstr != nullptr)
      errMsg = CMIUtilString::Format(MIRSRC(IDS_DRIVER_ERR_PARSE_ARGS),
                                     m_pDriverCurrent->GetName().c_str(),
                                     pErrorCstr);
    else
      errMsg = CMIUtilString::Format(MIRSRC(IDS_DRIVER_ERR_PARSE_ARGS_UNKNOWN),
                                     m_pDriverCurrent->GetName().c_str());

    bOk = CMICmnStreamStdout::Instance().Write(errMsg, true);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the current driver's last error condition.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text description.
// Throws:  None.
//--
CMIUtilString CMIDriverMgr::DriverGetError() const {
  if (m_pDriverCurrent != nullptr)
    return m_pDriverCurrent->GetError();
  else {
    const CMIUtilString errMsg(MIRSRC(IDS_DRIVER_ERR_CURRENT_NOT_SET));
    CMICmnStreamStdout::Instance().Write(errMsg, true);
  }

  return CMIUtilString();
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the current driver's name.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Driver name.
//                          Empty string = no current working driver specified.
// Throws:  None.
//--
CMIUtilString CMIDriverMgr::DriverGetName() const {
  if (m_pDriverCurrent != nullptr)
    return m_pDriverCurrent->GetName();
  else {
    const CMIUtilString errMsg(MIRSRC(IDS_DRIVER_ERR_CURRENT_NOT_SET));
    CMICmnStreamStdout::Instance().Write(errMsg, true);
  }

  return CMIUtilString();
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the current driver's debugger object.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBDebugger * - Ptr to driver's debugger object.
//                             - NULL = no current working driver specified.
// Throws:  None.
//--
lldb::SBDebugger *CMIDriverMgr::DriverGetTheDebugger() {
  lldb::SBDebugger *pDebugger = nullptr;
  if (m_pDriverCurrent != nullptr)
    pDebugger = &m_pDriverCurrent->GetTheDebugger();
  else {
    const CMIUtilString errMsg(MIRSRC(IDS_DRIVER_ERR_CURRENT_NOT_SET));
    CMICmnStreamStdout::Instance().Write(errMsg, true);
  }

  return pDebugger;
}

//++
//------------------------------------------------------------------------------------
// Details: Check the arguments given on the command line. The main purpose of
// this
//          function is to check for the presence of the --interpreter option.
//          Having
//          this option present tells *this manager to set the CMIDriver to do
//          work. If
//          not use the LLDB driver. The following are options that are only
//          handled by
//          the CMIDriverMgr are:
//              --help or -h
//              --interpreter
//              --version
//              --versionLong
//              --log
//              --executable
//              --log-dir
//          The above arguments are not handled by any driver object except for
//          --executable.
//          The options --interpreter and --executable in code act very similar.
//          The
//          --executable is necessary to differentiate whither the MI Driver is
//          being using
//          by a client i.e. Eclipse or from the command line. Eclipse issues
//          the option
//          --interpreter and also passes additional arguments which can be
//          interpreted as an
//          executable if called from the command line. Using --executable tells
//          the MI
//          Driver is being called the command line and that the executable
//          argument is indeed
//          a specified executable an so actions commands to set up the
//          executable for a
//          debug session. Using --interpreter on the command line does not
//          action additional
//          commands to initialise a debug session and so be able to launch the
//          process. The directory
//          where the log file is created is specified using --log-dir.
// Type:    Method.
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
//          vwbExiting  - (W)   True = *this want to exit, Reasons: help,
//          invalid arg(s),
//                              version information only.
//                              False = Continue to work, start debugger i.e.
//                              Command
//                              interpreter.
// Return:  lldb::SBError - LLDB current error status.
// Throws:  None.
//--
bool CMIDriverMgr::ParseArgs(const int argc, const char *argv[],
                             bool &vwbExiting) {
  bool bOk = MIstatus::success;

  vwbExiting = false;

  // Print MI application path to the Log file
  const CMIUtilString appPath(
      CMIUtilString::Format(MIRSRC(IDS_MI_APP_FILEPATHNAME), argv[0]));
  bOk = m_pLog->Write(appPath, CMICmnLog::eLogVerbosity_Log);

  // Print application arguments to the Log file
  const bool bHaveArgs(argc >= 2);
  CMIUtilString strArgs(MIRSRC(IDS_MI_APP_ARGS));
  if (!bHaveArgs) {
    strArgs += MIRSRC(IDS_WORD_NONE);
    bOk = bOk && m_pLog->Write(strArgs, CMICmnLog::eLogVerbosity_Log);
  } else {
    for (MIint i = 1; i < argc; i++) {
      strArgs += CMIUtilString::Format("%d:'%s' ", i, argv[i]);
    }
    bOk = bOk && m_pLog->Write(strArgs, CMICmnLog::eLogVerbosity_Log);
  }

  // Look for the command line options
  bool bHaveArgInterpret = false;
  bool bHaveArgVersion = false;
  bool bHaveArgVersionLong = false;
  bool bHaveArgLog = false;
  bool bHaveArgLogDir = false;
  bool bHaveArgHelp = false;
  CMIUtilString strLogDir;

  bHaveArgInterpret = true;
  if (bHaveArgs) {
    // CODETAG_MIDRIVE_CMD_LINE_ARG_HANDLING
    for (MIint i = 1; i < argc; i++) {
      // *** Add args to help in GetHelpOnCmdLineArgOptions() ***
      const CMIUtilString strArg(argv[i]);

      // Argument "--executable" is also check for in CMIDriver::ParseArgs()
      if (("--interpreter" == strArg) || // Given by the client such as Eclipse
          ("--executable" == strArg))    // Used to specify that there
                                         // is executable argument also
                                         // on the command line
      {                                  // See fn description.
        bHaveArgInterpret = true;
      }
      if ("--version" == strArg) {
        bHaveArgVersion = true;
      }
      if ("--versionLong" == strArg) {
        bHaveArgVersionLong = true;
      }
      if ("--log" == strArg) {
        bHaveArgLog = true;
      }
      if (0 == strArg.compare(0, 10, "--log-dir=")) {
        strLogDir = strArg.substr(10, CMIUtilString::npos);
        bHaveArgLogDir = true;
      }
      if (("--help" == strArg) || ("-h" == strArg)) {
        bHaveArgHelp = true;
      }
    }
  }

  if (bHaveArgLog) {
    CMICmnLog::Instance().SetEnabled(true);
  }

  if (bHaveArgLogDir) {
    bOk = bOk && CMICmnLogMediumFile::Instance().SetDirectory(strLogDir);
  }

  // Todo: Remove this output when MI is finished. It is temporary to persuade
  // Eclipse plugin to work.
  //       Eclipse reads this literally and will not work unless it gets this
  //       exact version text.
  // Handle --version option (ignore the --interpreter option if present)
  if (bHaveArgVersion) {
    vwbExiting = true;
    bOk = bOk &&
          CMICmnStreamStdout::Instance().WriteMIResponse(
              MIRSRC(IDE_MI_VERSION_GDB));
    return bOk;
  }

  // Todo: Make this the --version when the above --version version is removed
  // Handle --versionlong option (ignore the --interpreter option if present)
  if (bHaveArgVersionLong) {
    vwbExiting = true;
    bOk =
        bOk && CMICmnStreamStdout::Instance().WriteMIResponse(GetAppVersion());
    return bOk;
  }

  // Both '--help' and '--interpreter' means give help for MI only. Without
  // '--interpreter' help the LLDB driver is working and so help is for that.
  if (bHaveArgHelp && bHaveArgInterpret) {
    vwbExiting = true;
    bOk = bOk &&
          CMICmnStreamStdout::Instance().WriteMIResponse(
              GetHelpOnCmdLineArgOptions());
    return bOk;
  }

  // This makes the assumption that there is at least one MI compatible
  // driver registered and one LLDB driver registered and the CMIDriver
  // is the first one found.
  // ToDo: Implement a better solution that handle any order, any number
  // of drivers. Or this 'feature' may be removed if deemed not required.
  IDriver *pLldbDriver = GetFirstNonMIDriver();
  IDriver *pMi2Driver = GetFirstMIDriver();
  if (bHaveArgInterpret && (pMi2Driver != nullptr))
    bOk = bOk && SetUseThisDriverToDoWork(*pMi2Driver);
  else if (pLldbDriver != nullptr)
    bOk = bOk && SetUseThisDriverToDoWork(*pLldbDriver);
  else {
    if (bOk) {
      vwbExiting = true;
      const CMIUtilString msg(MIRSRC(IDS_DRIVER_ERR_NON_REGISTERED));
      bOk = bOk && CMICmnStreamStdout::Instance().WriteMIResponse(msg);
    }
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Return formatted application version and name information.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text data.
// Throws:  None.
//--
CMIUtilString CMIDriverMgr::GetAppVersion() const {
  const CMIUtilString strProj(MIRSRC(IDS_PROJNAME));
  const CMIUtilString strVsn(CMIDriver::Instance().GetVersionDescription());
  const CMIUtilString strGdb(MIRSRC(IDE_MI_VERSION_GDB));
  const CMIUtilString strVrsnInfo(CMIUtilString::Format(
      "%s\n%s\n%s", strProj.c_str(), strVsn.c_str(), strGdb.c_str()));

  return strVrsnInfo;
}

//++
//------------------------------------------------------------------------------------
// Details: Return formatted help information on all the MI command line
// options.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text data.
// Throws:  None.
//--
CMIUtilString CMIDriverMgr::GetHelpOnCmdLineArgOptions() const {
  const CMIUtilString pHelp[] = {
      MIRSRC(IDE_MI_APP_DESCRIPTION), MIRSRC(IDE_MI_APP_INFORMATION),
      MIRSRC(IDE_MI_APP_ARG_USAGE), MIRSRC(IDE_MI_APP_ARG_HELP),
      MIRSRC(IDE_MI_APP_ARG_VERSION), MIRSRC(IDE_MI_APP_ARG_VERSION_LONG),
      MIRSRC(IDE_MI_APP_ARG_INTERPRETER), MIRSRC(IDE_MI_APP_ARG_SOURCE),
      MIRSRC(IDE_MI_APP_ARG_EXECUTEABLE),
      MIRSRC(IDE_MI_APP_ARG_SYNCHRONOUS),
      CMIUtilString::Format(
          MIRSRC(IDE_MI_APP_ARG_APP_LOG),
          CMICmnLogMediumFile::Instance().GetFileName().c_str()),
      MIRSRC(IDE_MI_APP_ARG_APP_LOG_DIR), MIRSRC(IDE_MI_APP_ARG_EXECUTABLE),
      MIRSRC(IDS_CMD_QUIT_HELP), MIRSRC(IDE_MI_APP_ARG_EXAMPLE)};
  const MIuint nHelpItems = sizeof pHelp / sizeof pHelp[0];
  CMIUtilString strHelp;
  for (MIuint i = 0; i < nHelpItems; i++) {
    strHelp += pHelp[i];
    strHelp += "\n\n";
  }

  return strHelp;
}

//++
//------------------------------------------------------------------------------------
// Details: Search the registered drivers and return the first driver which says
// it is
//          GDB/MI compatible i.e. the CMIDriver class.
// Type:    Method.
// Args:    None.
// Return:  IDriver * - Ptr to driver, NULL = no driver found.
// Throws:  None.
//--
CMIDriverMgr::IDriver *CMIDriverMgr::GetFirstMIDriver() const {
  IDriver *pDriver = nullptr;
  MapDriverIdToDriver_t::const_iterator it = m_mapDriverIdToDriver.begin();
  while (it != m_mapDriverIdToDriver.end()) {
    const CMIUtilString &rDrvId = (*it).first;
    MIunused(rDrvId);
    IDriver *pDvr = (*it).second;
    if (pDvr->GetDriverIsGDBMICompatibleDriver()) {
      pDriver = pDvr;
      break;
    }

    // Next
    ++it;
  }

  return pDriver;
}

//++
//------------------------------------------------------------------------------------
// Details: Search the registered drivers and return the first driver which says
// it is
//          not GDB/MI compatible i.e. the LLDB Driver class.
// Type:    Method.
// Args:    None.
// Return:  IDriver * - Ptr to driver, NULL = no driver found.
// Throws:  None.
//--
CMIDriverMgr::IDriver *CMIDriverMgr::GetFirstNonMIDriver() const {
  IDriver *pDriver = nullptr;
  MapDriverIdToDriver_t::const_iterator it = m_mapDriverIdToDriver.begin();
  while (it != m_mapDriverIdToDriver.end()) {
    const CMIUtilString &rDrvId = (*it).first;
    MIunused(rDrvId);
    IDriver *pDvr = (*it).second;
    if (!pDvr->GetDriverIsGDBMICompatibleDriver()) {
      pDriver = pDvr;
      break;
    }

    // Next
    ++it;
  }

  return pDriver;
}

//++
//------------------------------------------------------------------------------------
// Details: Search the registered drivers and return driver with the specified
// ID.
// Type:    Method.
// Args:    vrDriverId  - (R) ID of a driver.
// Return:  IDriver * - Ptr to driver, NULL = no driver found.
// Throws:  None.
//--
CMIDriverMgr::IDriver *
CMIDriverMgr::GetDriver(const CMIUtilString &vrDriverId) const {
  MapDriverIdToDriver_t::const_iterator it =
      m_mapDriverIdToDriver.find(vrDriverId);
  if (it == m_mapDriverIdToDriver.end())
    return nullptr;

  IDriver *pDriver = (*it).second;

  return pDriver;
}

//++
//------------------------------------------------------------------------------------
// Details: Gets called when lldb-mi gets a signal. Passed signal to current
// driver.
//
// Type:    Method.
// Args:    signal that was delivered
// Return:  None.
// Throws:  None.
//--
void CMIDriverMgr::DeliverSignal(int signal) {
  if (m_pDriverCurrent != nullptr)
    m_pDriverCurrent->DeliverSignal(signal);
}
