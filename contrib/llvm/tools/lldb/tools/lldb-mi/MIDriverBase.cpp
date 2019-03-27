//===-- MIDriverBase.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBEvent.h"

// In-house headers:
#include "MIDriverBase.h"

//++
//------------------------------------------------------------------------------------
// Details: CMIDriverBase constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriverBase::CMIDriverBase()
    : m_pDriverFallThru(nullptr), m_pDriverParent(nullptr), m_bExitApp(false) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIDriverBase destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIDriverBase::~CMIDriverBase() { m_pDriverFallThru = NULL; }

//++
//------------------------------------------------------------------------------------
// Details: This function allows *this driver to call on another driver to
// perform work
//          should this driver not be able to handle the client data input.
// Type:    Overrideable.
//          Check the error message if the function returns a failure.
// Type:    Overridden.
// Args:    vCmd        - (R) Command instruction to interpret.
//          vwErrMsg    - (W) Status description on command failing.
// Return:  MIstatus::success - Command succeeded.
//          MIstatus::failure - Command failed.
// Throws:  None.
//--
bool CMIDriverBase::DoFallThruToAnotherDriver(const CMIUtilString &vCmd,
                                              CMIUtilString &vwErrMsg) {
  // Do nothing - override and implement. Use m_pDriverFallThru.
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: This function allows *this driver to call on another driver to
// perform work
//          should this driver not be able to handle the client data input.
// Type:    Overrideable.
// Args:    vrOtherDriver   - (R) Reference to another driver object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverBase::SetDriverToFallThruTo(const CMIDriverBase &vrOtherDriver) {
  MIunused(vrOtherDriver);

  // Do nothing - override and implement. Set m_pDriverFallThru.

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: This function allows *this driver to call functionality on the
// parent driver
//          ask for information for example.
// Type:    Overrideable.
// Args:    vrOtherDriver     - (R) Reference to another driver object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIDriverBase::SetDriverParent(const CMIDriverBase &vrOtherDriver) {
  MIunused(vrOtherDriver);

  // Do nothing - override and implement. Set m_pDriverParent.

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the parent driver to *this driver if one assigned. If
// assigned *this
//          is the pass through driver that the parent driver passes work to.
// Type:    Method.
// Args:    None.
// Return:  CMIDriverBase * - Pointer to a driver object.
//                          - NULL = there is not parent to *this driver.
// Throws:  None.
//--
CMIDriverBase *CMIDriverBase::GetDriversParent() const {
  return m_pDriverParent;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the pointer to the other fall through driver *this driver
// is using
//          (or not using).
// Type:    Method.
// Args:    None.
// Return:  CMIDriverBase * - Pointer to other driver.
//                          - NULL if no driver set.
// Throws:  None.
//--
CMIDriverBase *CMIDriverBase::GetDriverToFallThruTo() const {
  return m_pDriverFallThru;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a file stream to other drivers on which *this
// driver
//          write's out to and they read as expected input. *this driver is
//          passing
//          through commands to the (child) pass through assigned driver.
// Type:    Overrideable.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriverBase::GetStdin() const {
  // Do nothing - override and implement
  return nullptr;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a file stream to other pass through assigned
// drivers
//          so they know what to write to.
// Type:    Overrideable.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriverBase::GetStdout() const {
  // Do nothing - override and implement
  return nullptr;
}

//++
//------------------------------------------------------------------------------------
// Details: *this driver provides a error file stream to other pass through
// assigned drivers
//          so they know what to write to.
// Type:    Overrideable.
// Args:    None.
// Return:  FILE * - Pointer to stream.
// Throws:  None.
//--
FILE *CMIDriverBase::GetStderr() const {
  // Do nothing - override and implement
  return nullptr;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the MI Driver's exit application flag. The application checks
// this flag
//          after every stdin line is read so the exit may not be instantaneous.
//          If vbForceExit is false the MI Driver queries its state and
//          determines if is
//          should exit or continue operating depending on that running state.
// Type:    Overrideable.
// Args:    vbForceExit - (R) True = Do not query, set state to exit, false =
// query if can/should exit right now.
// Return:  None.
// Throws:  None.
//--
void CMIDriverBase::SetExitApplicationFlag(const bool vbForceExit) {
  MIunused(vbForceExit);

  // Do nothing - override and implement
}
