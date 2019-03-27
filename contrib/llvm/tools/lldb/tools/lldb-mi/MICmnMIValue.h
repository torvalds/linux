//===-- MICmnMIValue.h ------------------------------------------*- C++ -*-===//
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

//++
//============================================================================
// Details: MI common code MI Result class. Part of the CMICmnMIValueRecord
//          set of objects.
//          The syntax is as follows:
//          result-record ==>  [ token ] "^" result-class ( "," result )* nl
//          token = any sequence of digits
//          * = 0 to many
//          nl = CR | CR_LF
//          result-class ==> "done" | "running" | "connected" | "error" | "exit"
//          result ==> variable "=" value
//          value ==> const | tuple | list
//          const ==> c-string (7 bit iso c string content)
//          tuple ==>  "{}" | "{" result ( "," result )* "}"
//          list ==>  "[]" | "[" value ( "," value )* "]" | "[" result ( ","
//          result )* "]"
//          More information see:
//          http://ftp.gnu.org/old-gnu/Manuals/gdb-5.1.1/html_chapter/gdb_22.html
//--
class CMICmnMIValue : public CMICmnBase {
  // Methods:
public:
  /* ctor */ CMICmnMIValue();
  //
  const CMIUtilString &GetString() const;

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmnMIValue() override;

  // Attributes:
protected:
  CMIUtilString m_strValue;
  bool m_bJustConstructed; // True = *this just constructed with no value, false
                           // = *this has had value added to it
};
