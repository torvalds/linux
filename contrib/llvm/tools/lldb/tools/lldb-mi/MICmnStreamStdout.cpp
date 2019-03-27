//===-- MICmnStreamStdout.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnStreamStdout.h"
#include "MICmnLog.h"
#include "MICmnResources.h"
#include "MIDriver.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnStreamStdout constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnStreamStdout::CMICmnStreamStdout() {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnStreamStdout destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnStreamStdout::~CMICmnStreamStdout() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this Stdout stream.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  bool bOk = MIstatus::success;

#ifdef _MSC_VER
// Debugging / I/O issues with client.
// This is only required on Windows if you do not use ::flush(stdout). MI uses
// ::flush(stdout)
// It trys to ensure the process attached to the stdout steam gets ALL the data.
//::setbuf( stdout, NULL );
#endif // _MSC_VER

  m_bInitialized = bOk;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this Stdout stream.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::Shutdown() {
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
// Details: Write an MI format type response to stdout. The text data does not
// need to
//          include a carriage line return as this is added to the text. The
//          function also
//          then passes the text data into the CMICmnLog logger.
// Type:    Method.
// Args:    vText       - (R) MI formatted text.
//          vbSendToLog - (R) True = Yes send to the Log file too, false = do
//          not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::WriteMIResponse(const CMIUtilString &vText,
                                         const bool vbSendToLog /* = true */) {
  return WritePriv(vText, vText, vbSendToLog);
}

//++
//------------------------------------------------------------------------------------
// Details: Write text data to stdout. The text data does not need to
//          include a carriage line return as this is added to the text. The
//          function also
//          then passes the text data into the CMICmnLog logger.
// Type:    Method.
// Args:    vText       - (R) Text data.
//          vbSendToLog - (R) True = Yes send to the Log file too, false = do
//          not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::Write(const CMIUtilString &vText,
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
// Details: Write text data to stdout. The text data does not need to
//          include a carriage line return as this is added to the text. The
//          function also
//          then passes the text data into the CMICmnLog logger.
// Type:    Method.
// Args:    vText           - (R) Text data prefixed with MI app's short name.
//          vTxtForLogFile  - (R) Text data.
//          vbSendToLog     - (R) True = Yes send to the Log file too, false =
//          do not. (Dflt = true)
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::WritePriv(const CMIUtilString &vText,
                                   const CMIUtilString &vTxtForLogFile,
                                   const bool vbSendToLog /* = true */) {
  if (vText.length() == 0)
    return MIstatus::failure;

  bool bOk = MIstatus::success;
  {
    // Grab the stdout thread lock while we print
    CMIUtilThreadLock _lock(m_mutex);

    // Send this text to stdout
    const MIint status = ::fputs(vText.c_str(), stdout);
    if (status == EOF)
      // Don't call the CMICmnBase::SetErrorDescription() because it will cause
      // a stack overflow:
      // CMICmnBase::SetErrorDescription -> CMICmnStreamStdout::Write ->
      // CMICmnStreamStdout::WritePriv -> CMICmnBase::SetErrorDescription
      bOk = MIstatus::failure;
    else {
      ::fprintf(stdout, "\n");
      ::fflush(stdout);
    }

    // Send this text to the log
    if (bOk && vbSendToLog)
      bOk &= m_pLog->WriteLog(vTxtForLogFile);
  }

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Lock the availability of the stream stdout. Other users of *this
// stream will
//          be stalled until it is available (Unlock()).
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::Lock() {
  m_mutex.Lock();
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release a previously locked stdout.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::Unlock() {
  m_mutex.Unlock();
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Take a text data and send to the stdout stream. Also output to the
// MI Log
//          file.
// Type:    Static method.
// Args:    vrTxt   - (R) Text.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::TextToStdout(const CMIUtilString &vrTxt) {
  const bool bSendToLog = true;
  return CMICmnStreamStdout::Instance().WriteMIResponse(vrTxt, bSendToLog);
}

//++
//------------------------------------------------------------------------------------
// Details: Write prompt to stdout if it's enabled.
// Type:    Static method.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmnStreamStdout::WritePrompt() {
  const CMICmnStreamStdin &rStdinMan = CMICmnStreamStdin::Instance();
  if (rStdinMan.GetEnablePrompt())
    return TextToStdout(rStdinMan.GetPrompt());
  return MIstatus::success;
}
