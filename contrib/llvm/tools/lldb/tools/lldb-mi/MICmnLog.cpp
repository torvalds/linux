//===-- MICmnLog.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnLog.h"
#include "MICmnLogMediumFile.h"
#include "MICmnResources.h"
#include "MIDriverMgr.h"
#include "MIUtilDateTimeStd.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLog constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLog::CMICmnLog() : m_bEnabled(false), m_bInitializingATM(false) {
  // Do not use this constructor, use Initialize()
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLog destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLog::~CMICmnLog() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Logger.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  ClrErrorDescription();

  // Mediums set inside because explicitly initing in MIDriverMain.cpp causes
  // compile errors with CAtlFile
  CMICmnLogMediumFile &rFileLog(CMICmnLogMediumFile::Instance());
  bool bOk = RegisterMedium(rFileLog);
  if (bOk) {
    // Set the Log trace file's header
    const CMIUtilString &rCR(rFileLog.GetLineReturn());
    CMIUtilDateTimeStd date;
    CMIUtilString msg;
    msg = CMIUtilString::Format(
        "%s\n", CMIDriverMgr::Instance().GetAppVersion().c_str());
    CMIUtilString logHdr(msg);
    msg = CMIUtilString::Format(MIRSRC(IDS_LOG_MSG_CREATION_DATE),
                                date.GetDate().c_str(), date.GetTime().c_str(),
                                rCR.c_str());
    logHdr += msg;
    msg =
        CMIUtilString::Format(MIRSRC(IDS_LOG_MSG_FILE_LOGGER_PATH),
                              rFileLog.GetFileNamePath().c_str(), rCR.c_str());
    logHdr += msg;

    bOk = rFileLog.SetHeaderTxt(logHdr);

    // Note log file medium's status is not available until we write at least
    // once to the file (so just write the title 1st line)
    m_bInitializingATM = true;
    CMICmnLog::WriteLog(".");
    if (!rFileLog.IsOk()) {
      const CMIUtilString msg(
          CMIUtilString::Format(MIRSRC(IDS_LOG_ERR_FILE_LOGGER_DISABLED),
                                rFileLog.GetErrorDescription().c_str()));
      CMICmnLog::WriteLog(msg);
    }
    m_bInitializingATM = false;
  }

  m_bInitialized = bOk;

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Logger.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  ClrErrorDescription();

  const bool bOk = UnregisterMediumAll();

  m_bInitialized = bOk;

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Enabled or disable *this Logger from writing any data to registered
// clients.
// Type:    Method.
// Args:    vbYes   - (R) True = Logger enabled, false = disabled.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::SetEnabled(const bool vbYes) {
  m_bEnabled = vbYes;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve state whether *this Logger is enabled writing data to
// registered clients.
// Type:    Method.
// Args:    None.
// Return:  True = Logger enable.
//          False = disabled.
// Throws:  None.
//--
bool CMICmnLog::GetEnabled() const { return m_bEnabled; }

//++
//------------------------------------------------------------------------------------
// Details: Unregister all the Mediums registered with *this Logger.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::UnregisterMediumAll() {
  MapMediumToName_t::const_iterator it = m_mapMediumToName.begin();
  for (; it != m_mapMediumToName.end(); it++) {
    IMedium *pMedium = (*it).first;
    pMedium->Shutdown();
  }

  m_mapMediumToName.clear();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Register a Medium with *this Logger.
// Type:    Method.
// Args:    vrMedium    - (R) The medium to register.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::RegisterMedium(const IMedium &vrMedium) {
  if (HaveMediumAlready(vrMedium))
    return MIstatus::success;

  IMedium *pMedium = const_cast<IMedium *>(&vrMedium);
  if (!pMedium->Initialize()) {
    const CMIUtilString &rStrMedName(pMedium->GetName());
    const CMIUtilString &rStrMedErr(pMedium->GetError());
    SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LOG_MEDIUM_ERR_INIT),
                                              rStrMedName.c_str(),
                                              rStrMedErr.c_str()));
    return MIstatus::failure;
  }

  MapPairMediumToName_t pr(pMedium, pMedium->GetName());
  m_mapMediumToName.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Query the Logger to see if a medium is already registered.
// Type:    Method.
// Args:    vrMedium    - (R) The medium to query.
// Return:  True - registered.
//          False - not registered.
// Throws:  None.
//--
bool CMICmnLog::HaveMediumAlready(const IMedium &vrMedium) const {
  IMedium *pMedium = const_cast<IMedium *>(&vrMedium);
  const MapMediumToName_t::const_iterator it = m_mapMediumToName.find(pMedium);
  return it != m_mapMediumToName.end();
}

//++
//------------------------------------------------------------------------------------
// Details: Unregister a medium from the Logger.
// Type:    Method.
// Args:    vrMedium    - (R) The medium to unregister.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::UnregisterMedium(const IMedium &vrMedium) {
  IMedium *pMedium = const_cast<IMedium *>(&vrMedium);
  m_mapMediumToName.erase(pMedium);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The callee client uses this function to write to the Logger. The
// data to be
//          written is given out to all the mediums registered. The verbosity
//          type parameter
//          indicates to the medium(s) the type of data or message given to it.
//          The medium has
//          modes of verbosity and depending on the verbosity set determines
//          which writes
//          go in to the logger.
//          The logger must be initialized successfully before a write to any
//          registered
//          can be carried out.
// Type:    Method.
// Args:    vData       - (R) The data to write to the logger.
//          veType      - (R) Verbosity type.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::Write(const CMIUtilString &vData, const ELogVerbosity veType) {
  if (!m_bInitialized && !m_bInitializingATM)
    return MIstatus::success;
  if (m_bRecursiveDive)
    return MIstatus::success;
  if (!m_bEnabled)
    return MIstatus::success;

  m_bRecursiveDive = true;

  MIuint cnt = 0;
  MIuint cntErr = 0;
  {
    MapMediumToName_t::const_iterator it = m_mapMediumToName.begin();
    while (it != m_mapMediumToName.end()) {
      IMedium *pMedium = (*it).first;
      const CMIUtilString &rNameMedium = (*it).second;
      MIunused(rNameMedium);
      if (pMedium->Write(vData, veType))
        cnt++;
      else
        cntErr++;

      // Next
      ++it;
    }
  }

  bool bOk = MIstatus::success;
  const MIuint mediumCnt = m_mapMediumToName.size();
  if ((cnt == 0) && (mediumCnt > 0)) {
    SetErrorDescription(MIRSRC(IDS_LOG_MEDIUM_ERR_WRITE_ANY));
    bOk = MIstatus::failure;
  }
  if (bOk && (cntErr != 0)) {
    SetErrorDescription(MIRSRC(IDS_LOG_MEDIUM_ERR_WRITE_MEDIUMFAIL));
    bOk = MIstatus::failure;
  }

  m_bRecursiveDive = false;

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Short cut function call to write only to the Log file.
//          The logger must be initialized successfully before a write to any
//          registered
//          can be carried out.
// Type:    Static.
// Args:    vData   - (R) The data to write to the logger.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLog::WriteLog(const CMIUtilString &vData) {
  return CMICmnLog::Instance().Write(vData, CMICmnLog::eLogVerbosity_Log);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve a string detailing the last error.
// Type:    Method.
// Args:    None,
// Return:  CMIUtilString.
// Throws:  None.
//--
const CMIUtilString &CMICmnLog::GetErrorDescription() const {
  return m_strMILastErrorDescription;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the internal description of the last error.
// Type:    Method.
// Args:    (R) String containing a description of the last error.
// Return:  None.
// Throws:  None.
//--
void CMICmnLog::SetErrorDescription(const CMIUtilString &vrTxt) const {
  m_strMILastErrorDescription = vrTxt;
}

//++
//------------------------------------------------------------------------------------
// Details: Clear the last error.
// Type:    None.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnLog::ClrErrorDescription() const {
  m_strMILastErrorDescription = CMIUtilString("");
}
