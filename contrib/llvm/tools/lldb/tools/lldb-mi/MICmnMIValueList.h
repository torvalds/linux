//===-- MICmnMIValueList.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnMIValue.h"
#include "MICmnMIValueResult.h"

//++
//============================================================================
// Details: MI common code MI Result class. Part of the CMICmnMIValueListRecord
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
class CMICmnMIValueList : public CMICmnMIValue {
  // Methods:
public:
  /* ctor */ CMICmnMIValueList(const bool vbValueTypeList);
  /* ctor */ CMICmnMIValueList(const CMICmnMIValueResult &vResult);
  /* ctor */ CMICmnMIValueList(const CMICmnMIValue &vValue);
  //
  void Add(const CMICmnMIValueResult &vResult);
  void Add(const CMICmnMIValue &vValue);
  CMIUtilString ExtractContentNoBrackets() const;

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmnMIValueList() override;

  // Methods:
private:
  void BuildList();
  void BuildList(const CMICmnMIValueResult &vResult);
  void BuildList(const CMICmnMIValue &vResult);
};
