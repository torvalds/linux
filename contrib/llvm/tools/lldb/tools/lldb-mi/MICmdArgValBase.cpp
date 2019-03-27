//===-- MICmdArgValBase.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MICmdArgValBase.h"
#include "MICmdArgContext.h"
#include "MIUtilString.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValBase constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValBase::CMICmdArgValBase()
    : m_bFound(false), m_bValid(false), m_bMandatory(false), m_bHandled(false),
      m_bIsMissingOptions(false) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValBase constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValBase::CMICmdArgValBase(const CMIUtilString &vrArgName,
                                   const bool vbMandatory,
                                   const bool vbHandleByCmd)
    : m_bFound(false), m_bValid(false), m_bMandatory(vbMandatory),
      m_strArgName(vrArgName), m_bHandled(vbHandleByCmd),
      m_bIsMissingOptions(false) {}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag of whether the argument is handled by the
// command or
//          not.
// Type:    Method.
// Args:    None.
// Return:  True - Command needs more information.
//          False - All information is present as expected.
// Throws:  None.
//--
bool CMICmdArgValBase::GetIsMissingOptions() const {
  return m_bIsMissingOptions;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag of whether the argument is handled by the
// command or
//          not.
// Type:    Method.
// Args:    None.
// Return:  True - Command handles *this argument or option.
//          False - Not handled (argument specified but ignored).
// Throws:  None.
//--
bool CMICmdArgValBase::GetIsHandledByCmd() const { return m_bHandled; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the name of *this argument.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Return the text name.
// Throws:  None.
//--
const CMIUtilString &CMICmdArgValBase::GetName() const { return m_strArgName; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag of whether the argument was found in the
// command's
//          argument / options string.
// Type:    Method.
// Args:    None.
// Return:  True - Argument found.
//          False - Argument not found.
// Throws:  None.
//--
bool CMICmdArgValBase::GetFound() const { return m_bFound; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag indicating whether the value was obtained
// from the
//          text arguments string and is valid.
// Type:    Method.
// Args:    None.
// Return:  True - Argument valid.
//          False - Argument not valid.
// Throws:  None.
//--
bool CMICmdArgValBase::GetValid() const { return m_bValid; }

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the state flag indicating whether *this argument is a
// mandatory
//          argument for the command or is optional to be present.
// Type:    Method.
// Args:    None.
// Return:  True - Mandatory.
//          False - Optional.
// Throws:  None.
//--
bool CMICmdArgValBase::GetIsMandatory() const { return m_bMandatory; }

//++
//------------------------------------------------------------------------------------
// Details: Parse the command's argument options string and try to extract the
// value *this
//          argument is looking for.
// Type:    Overrideable.
// Args:    vArgContext - (RW) The command's argument options string.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdArgValBase::Validate(CMICmdArgContext &vwArgContext) {
  MIunused(vwArgContext);

  // Override to implement

  return MIstatus::failure;
}
