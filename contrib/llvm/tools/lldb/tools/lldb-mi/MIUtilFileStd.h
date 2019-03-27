//===-- MIUtilFileStd.h -----------------------------------------*- C++ -*-===//
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
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code utility class. File handling.
//--
class CMIUtilFileStd : public CMICmnBase {
  // Static:
public:
  static char GetSlash();

  // Methods:
public:
  /* ctor */ CMIUtilFileStd();
  //
  bool CreateWrite(const CMIUtilString &vFileNamePath, bool &vwrbNewCreated);
  bool Write(const CMIUtilString &vData);
  bool Write(const char *vpData, const MIuint vCharCnt);
  void Close();
  bool IsOk() const;
  bool IsFileExist(const CMIUtilString &vFileNamePath) const;
  const CMIUtilString &GetLineReturn() const;
  static CMIUtilString StripOffFileName(const CMIUtilString &vDirectoryPath);

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMIUtilFileStd() override;

  // Attributes:
private:
  CMIUtilString m_fileNamePath;
  FILE *m_pFileHandle;
  CMIUtilString m_constCharNewLine;
  bool m_bFileError; // True = have a file error ATM, false = all ok
};
