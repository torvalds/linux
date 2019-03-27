//===-- MICmnMIOutOfBandRecord.h --------------------------------*- C++ -*-===//
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
#include "MICmnMIValueConst.h"
#include "MICmnMIValueResult.h"
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code MI Out-of-band (Async) Record class. A class that
// encapsulates
//          MI result record data and the forming/format of data added to it.
//          Out-of-band records are used to notify the GDB/MI client of
//          additional
//          changes that have occurred. Those changes can either be a
//          consequence
//          of GDB/MI (e.g., a breakpoint modified) or a result of target
//          activity
//          (e.g., target stopped).
//          The syntax is as follows:
//          "*" type ( "," result )*
//          type ==> running | stopped
//
//          The Out-of-band record can be retrieve at any time *this object is
//          instantiated so unless work is done on *this Out-of-band record then
//          it is
//          possible to return a malformed Out-of-band record. If nothing has
//          been set
//          or added to *this MI Out-of-band record object then text "<Invalid>"
//          will
//          be returned.
//
//          More information see:
//          http://ftp.gnu.org/old-gnu/Manuals/gdb-5.1.1/html_chapter/gdb_22.html//
//--
class CMICmnMIOutOfBandRecord : public CMICmnBase {
  // Enumerations:
public:
  //++
  // Details: Enumeration of the type of Out-of-band for *this Out-of-band
  // record
  //--
  enum OutOfBand_e {
    eOutOfBand_Running = 0,
    eOutOfBand_Stopped,
    eOutOfBand_BreakPointCreated,
    eOutOfBand_BreakPointModified,
    eOutOfBand_Thread,
    eOutOfBand_ThreadGroupAdded,
    eOutOfBand_ThreadGroupExited,
    eOutOfBand_ThreadGroupRemoved,
    eOutOfBand_ThreadGroupStarted,
    eOutOfBand_ThreadCreated,
    eOutOfBand_ThreadExited,
    eOutOfBand_ThreadSelected,
    eOutOfBand_TargetModuleLoaded,
    eOutOfBand_TargetModuleUnloaded,
    eOutOfBand_TargetStreamOutput,
    eOutOfBand_ConsoleStreamOutput,
    eOutOfBand_LogStreamOutput
  };

  // Methods:
public:
  /* ctor */ CMICmnMIOutOfBandRecord();
  /* ctor */ CMICmnMIOutOfBandRecord(OutOfBand_e veType);
  /* ctor */ CMICmnMIOutOfBandRecord(OutOfBand_e veType,
                                     const CMICmnMIValueConst &vConst);
  /* ctor */ CMICmnMIOutOfBandRecord(OutOfBand_e veType,
                                     const CMICmnMIValueResult &vResult);
  //
  const CMIUtilString &GetString() const;
  void Add(const CMICmnMIValueResult &vResult);

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CMICmnMIOutOfBandRecord() override;

  // Attributes:
private:
  CMIUtilString
      m_strAsyncRecord; // Holds the text version of the result record to date
};
