//===-- MICmnMIResultRecord.h -----------------------------------*- C++ -*-===//
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
#include "MICmnMIValueResult.h"
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code MI Result Record class. A class that encapsulates
//          MI result record data and the forming/format of data added to it.
//          The syntax is as follows:
//          result-record ==>  [ token ] "^" result-class ( "," result )* nl
//          token = any sequence of digits
//          * = 0 to many
//          nl = CR | CR_LF
//          result-class ==> "done" | "running" | "connected" | "error" | "exit"
//          result ==> variable "=" value
//          value ==> const | tuple | list
//          const ==> c-string (7 bit iso c string content) i.e. "all" inc
//          quotes
//          tuple ==>  "{}" | "{" result ( "," result )* "}"
//          list ==>  "[]" | "[" value ( "," value )* "]" | "[" result ( ","
//          result )* "]"
//
//          The result record can be retrieve at any time *this object is
//          instantiated so unless work is done on *this result record then it
//          is
//          possible to return a malformed result record. If nothing has been
//          set
//          or added to *this MI result record object then text "<Invalid>" will
//          be returned.
//          More information see:
//          http://ftp.gnu.org/old-gnu/Manuals/gdb-5.1.1/html_chapter/gdb_22.html
//--
class CMICmnMIResultRecord : public CMICmnBase {
  // Enumerations:
public:
  //++
  // Details: Enumeration of the result class for *this result record
  //--
  enum ResultClass_e {
    eResultClass_Done = 0,
    eResultClass_Running,
    eResultClass_Connected,
    eResultClass_Error,
    eResultClass_Exit
  };

  // Methods:
public:
  /* ctor */ CMICmnMIResultRecord();
  /* ctor */ CMICmnMIResultRecord(const CMIUtilString &vrToken,
                                  ResultClass_e veType);
  /* ctor */ CMICmnMIResultRecord(const CMIUtilString &vrToken,
                                  ResultClass_e veType,
                                  const CMICmnMIValueResult &vValue);
  //
  const CMIUtilString &GetString() const;
  void Add(const CMICmnMIValue &vMIValue);

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmnMIResultRecord() override;

  // Attributes:
private:
  CMIUtilString
      m_strResultRecord; // Holds the text version of the result record to date
};
