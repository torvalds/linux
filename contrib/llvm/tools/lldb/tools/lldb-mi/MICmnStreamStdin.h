//===-- MICmnStreamStdin.h --------------------------------------*- C++ -*-===//
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
// Details: MI common code class. Used to handle stream data from Stdin.
//          Singleton class using the Visitor pattern. A driver using the
//          interface
//          provide can receive callbacks when a new line of data is received.
//          Each line is determined by a carriage return.
//          A singleton class.
//--
class CMICmnStreamStdin : public CMICmnBase,
                          public MI::ISingleton<CMICmnStreamStdin> {
  // Give singleton access to private constructors
  friend MI::ISingleton<CMICmnStreamStdin>;

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  //
  const CMIUtilString &GetPrompt() const;
  bool SetPrompt(const CMIUtilString &vNewPrompt);
  void SetEnablePrompt(const bool vbYes);
  bool GetEnablePrompt() const;
  const char *ReadLine(CMIUtilString &vwErrMsg);

  // Methods:
private:
  /* ctor */ CMICmnStreamStdin();
  /* ctor */ CMICmnStreamStdin(const CMICmnStreamStdin &);
  void operator=(const CMICmnStreamStdin &);

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnStreamStdin() override;

  // Attributes:
private:
  CMIUtilString m_strPromptCurrent; // Command line prompt as shown to the user
  bool m_bShowPrompt; // True = Yes prompt is shown/output to the user (stdout),
                      // false = no prompt
  static const int m_constBufferSize = 2048;
  char *m_pCmdBuffer;
};
