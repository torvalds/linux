//===-- MIDriverBase.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDebugger.h"

// In-house headers:
#include "MIUtilString.h"

// Declarations:
namespace lldb {
class SBBroadcaster;
}

//++
//============================================================================
// Details: MI driver base implementation class. This class has been created so
//          not have to edit the lldb::SBBroadcaster class code. Functionality
//          and attributes need to be common to the LLDB Driver class and the
//          MI Driver class (derived from lldb::SBBroadcaster) so they can call
//          upon each other for functionality fall through and allow the
//          CDriverMgr to manage either (any) driver to be operated on.
//          Each driver instance (the CMIDriver, LLDB::Driver) has its own
//          LLDB::SBDebugger object.
//--
class CMIDriverBase {
  // Methods:
public:
  /* ctor */ CMIDriverBase();

  CMIDriverBase *GetDriverToFallThruTo() const;
  CMIDriverBase *GetDriversParent() const;

  // Overrideable:
public:
  /* dtor */ virtual ~CMIDriverBase();

  virtual bool DoFallThruToAnotherDriver(const CMIUtilString &vCmd,
                                         CMIUtilString &vwErrMsg);
  virtual bool SetDriverToFallThruTo(const CMIDriverBase &vrOtherDriver);
  virtual bool SetDriverParent(const CMIDriverBase &vrOtherDriver);
  virtual const CMIUtilString &GetDriverName() const = 0;
  virtual const CMIUtilString &GetDriverId() const = 0;
  virtual void SetExitApplicationFlag(const bool vbForceExit);

  // MI provide information for the pass through (child) assigned driver
  virtual FILE *GetStdin() const;
  virtual FILE *GetStdout() const;
  virtual FILE *GetStderr() const;

  // Attributes:
protected:
  CMIDriverBase *m_pDriverFallThru; // Child driver to use should *this driver
                                    // not be able to handle client input
  CMIDriverBase *m_pDriverParent; // The parent driver who passes work to *this
                                  // driver to do work
  CMIUtilString m_strDriverId;
  bool m_bExitApp; // True = Yes, exit application, false = continue execution
};
