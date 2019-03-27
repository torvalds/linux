//===-- MICmnStreamStderr.cpp ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnStreamStderr.h"
#include "MICmnLog.h"
#include "MICmnResources.h"
#include "MIDriver.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnStreamStderr constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnStreamStderr::CMICmnStreamStderr() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnStreamStderr destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnStreamStderr::~CMICmnStreamStderr() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this stderr stream.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;

#ifdef _MSC_VER
// Debugging / I/O issues with client.
// This is only required on Windows if you do not use ::flush(stderr). MI uses
// ::flush(stderr)
// It trys to ensure the process attached to the stderr steam gets ALL the data.
//::setbuf( stderr, NULL );
#endif // _MSC_VER

  m_bInitialized = bOk;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this stderr stream.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  ClrErrorDescription();

  m_bInitialized = false;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Write text data to stderr. Prefix the message with "MI:". The text
// data does
//          not need to include a carriage line return as this is added to the
//          text. The
//          function also then passes the text data into the CMICmnLog logger.
// Type:    Method.
// Args:    vText       - (R) Text data.
//          vbSendToLog - (R) True = Yes send to the Log file too, false = do
//          not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::Write(const CMIUtilString &vText,
                               const bool vbSendToLog /* = true */) {
  if (vText.length() == 0)
    return MIstatus::failure;

  const CMIUtilString strPrefixed(CMIUtilString::Format(
      "%s: %s", CMIDriver::Instance().GetAppNameShort().c_str(),
      vText.c_str()));

  return WritePriv(strPrefixed, vText, vbSendToLog);
}

//++
//------------------------------------------------------------------------------------
// Details: Write an LLDB text message to stderr.
//          The text data does not need to include a carriage line return as
//          this is added
//          to the text. The function also then passes the text data into the
//          CMICmnLog
//          logger.
// Type:    Method.
// Args:    vText       - (R) Text data.
//          vbSendToLog - (R) True = Yes send to the Log file too, false = do
//          not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::WriteLLDBMsg(const CMIUtilString &vText,
                                      const bool vbSendToLog /* = true */) {
  if (vText.length() == 0)
    return MIstatus::failure;

  const CMIUtilString strPrefixed(
      CMIUtilString::Format("LLDB: %s", vText.c_str()));

  return WritePriv(vText, strPrefixed, vbSendToLog);
}

//++
//------------------------------------------------------------------------------------
// Details: Write text data to stderr. The text data does not need to
//          include a carriage line return as this is added to the text. The
//          function also
//          then passes the text data into the CMICmnLog logger.
// Type:    Method.
// Args:    vText           - (R) Text data. May be prefixed with MI app's short
// name.
//          vTxtForLogFile  - (R) Text data.
//          vbSendToLog     - (R) True = Yes send to the Log file too, false =
//          do not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::WritePriv(const CMIUtilString &vText,
                                   const CMIUtilString &vTxtForLogFile,
                                   const bool vbSendToLog /* = true */) {
  if (vText.length() == 0)
    return MIstatus::failure;

  bool bOk = MIstatus::success;
  {
    // Grab the stderr thread lock while we print
    CMIUtilThreadLock _lock(m_mutex);

    // Send this text to stderr
    const MIint status = ::fputs(vText.c_str(), stderr);
    if (status == EOF) {
      const CMIUtilString errMsg(CMIUtilString::Format(
          MIRSRC(IDS_STDERR_ERR_NOT_ALL_DATA_WRITTEN), vText.c_str()));
      SetErrorDescription(errMsg);
      bOk = MIstatus::failure;
    } else {
      ::fprintf(stderr, "\n");
      ::fflush(stderr);
    }

    // Send this text to the log
    if (bOk && vbSendToLog)
      bOk &= m_pLog->WriteLog(vTxtForLogFile);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Lock the availability of the stream stderr. Other users of *this
// stream will
//          be stalled until it is available (Unlock()).
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::Lock() {
  m_mutex.Lock();
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release a previously locked stderr.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::Unlock() {
  m_mutex.Unlock();
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Take MI Driver text message and send to the stderr stream. Also
// output to the
//           MI Log file.
// Type:    Static method.
// Args:    vrTxt   - (R) Text.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::TextToStderr(const CMIUtilString &vrTxt) {
  const bool bLock = CMICmnStreamStderr::Instance().Lock();
  const bool bOk = bLock && CMICmnStreamStderr::Instance().Write(vrTxt);
  bLock &&CMICmnStreamStderr::Instance().Unlock();

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Take an LLDB message and send to the stderr stream. The message is
// not always
//          an error message. The user has typed a command in to the Eclipse
//          console (by-
//          passing Eclipse) and this is the result message from LLDB back to
//          the user.
//          Also output to the MI Log file.
// Type:    Static method.
// Args:    vrTxt   - (R) Text.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnStreamStderr::LLDBMsgToConsole(const CMIUtilString &vrTxt) {
  const bool bLock = CMICmnStreamStderr::Instance().Lock();
  const bool bOk = bLock && CMICmnStreamStderr::Instance().WriteLLDBMsg(vrTxt);
  bLock &&CMICmnStreamStderr::Instance().Unlock();

  return bOk;
}
