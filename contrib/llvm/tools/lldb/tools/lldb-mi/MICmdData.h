//===-- MICmdData.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// In-house headers:
#include "MICmnResources.h"

//++
//============================================================================
// Details: MI command metadata. Holds the command's name, MI number and options
//          as found on stdin. Holds the command's MI output (written to
//          stdout).
//--
struct SMICmdData {
  SMICmdData()
      : id(0), bCmdValid(false), bCmdExecutedSuccessfully(false),
        bMIOldStyle(false), bHasResultRecordExtra(false) {}

  MIuint id;                    // A command's unique ID i.e. GUID
  CMIUtilString strMiCmdToken;  // The command's MI token (a number)
  CMIUtilString strMiCmd;       // The command's name
  CMIUtilString strMiCmdOption; // The command's arguments or options
  CMIUtilString strMiCmdAll;    // The text as received from the client
  CMIUtilString
      strMiCmdResultRecord; // Each command forms 1 response to its input
  CMIUtilString strMiCmdResultRecordExtra; // Hack command produce more response
                                           // text to help the client because of
                                           // using LLDB
  bool bCmdValid; // True = Valid MI format command, false = invalid
  bool bCmdExecutedSuccessfully; // True = Command finished successfully, false
                                 // = Did not start/did not complete
  CMIUtilString strErrorDescription; // Command failed this is why
  bool bMIOldStyle; // True = format "3thread", false = format "3-thread"
  bool bHasResultRecordExtra; // True = Yes command produced additional MI
                              // output to its 1 line response, false = no extra
                              // MI output formed

  void Clear() {
    id = 0;
    strMiCmdToken.clear();
    strMiCmd = MIRSRC(IDS_CMD_ERR_CMD_RUN_BUT_NO_ACTION);
    strMiCmdOption.clear();
    strMiCmdAll.clear();
    strMiCmdResultRecord.clear();
    strMiCmdResultRecordExtra.clear();
    bCmdValid = false;
    bCmdExecutedSuccessfully = false;
    strErrorDescription.clear();
    bMIOldStyle = false;
    bHasResultRecordExtra = false;
  }
};
