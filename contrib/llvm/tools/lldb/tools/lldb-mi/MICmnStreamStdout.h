//===-- MICmnStreamStdout.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnBase.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilString.h"
#include "MIUtilThreadBaseStd.h"

//++
//============================================================================
// Details: MI common code class. The MI driver requires this object.
//          CMICmnStreamStdout sets up and tears downs stdout for the driver.
//
//          Singleton class.
//--
class CMICmnStreamStdout : public CMICmnBase,
                           public MI::ISingleton<CMICmnStreamStdout> {
  friend class MI::ISingleton<CMICmnStreamStdout>;

  // Statics:
public:
  static bool TextToStdout(const CMIUtilString &vrTxt);
  static bool WritePrompt();

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  //
  bool Lock();
  bool Unlock();
  bool Write(const CMIUtilString &vText, const bool vbSendToLog = true);
  bool WriteMIResponse(const CMIUtilString &vText,
                       const bool vbSendToLog = true);

  // Methods:
private:
  /* ctor */ CMICmnStreamStdout();
  /* ctor */ CMICmnStreamStdout(const CMICmnStreamStdout &);
  void operator=(const CMICmnStreamStdout &);
  //
  bool WritePriv(const CMIUtilString &vText,
                 const CMIUtilString &vTxtForLogFile,
                 const bool vbSendToLog = true);

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnStreamStdout() override;

  // Attributes:
private:
  CMIUtilThreadMutex m_mutex; // Mutex object for sync during writing to stream
};
