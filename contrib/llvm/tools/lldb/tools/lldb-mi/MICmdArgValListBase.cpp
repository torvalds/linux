//===-- MICmdArgValListBase.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmdArgValListBase.h"
#include "MICmdArgContext.h"
#include "MICmdArgValConsume.h"
#include "MICmdArgValFile.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListBase constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListBase::CMICmdArgValListBase()
    : m_eArgType(eArgValType_invalid) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListBase constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListBase::CMICmdArgValListBase(const CMIUtilString &vrArgName,
                                           const bool vbMandatory,
                                           const bool vbHandleByCmd)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd),
      m_eArgType(eArgValType_invalid) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListBase constructor.
// Type:    Method.
// Args:    vrArgName       - (R) Argument's name to search by.
//          vbMandatory     - (R) True = Yes must be present, false = optional
//          argument.
//          vbHandleByCmd   - (R) True = Command processes *this option, false =
//          not handled.
//          veType          - (R) The type of argument to look for and create
//          argument object of a certain type.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListBase::CMICmdArgValListBase(const CMIUtilString &vrArgName,
                                           const bool vbMandatory,
                                           const bool vbHandleByCmd,
                                           const ArgValType_e veType)
    : CMICmdArgValBaseTemplate(vrArgName, vbMandatory, vbHandleByCmd),
      m_eArgType(veType) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdArgValListBase destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdArgValListBase::~CMICmdArgValListBase() {
  // Tidy up
  Destroy();
}

//++
//------------------------------------------------------------------------------------
// Details: Tear down resources used by *this object.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmdArgValListBase::Destroy() {
  // Tidy up
  VecArgObjPtr_t::const_iterator it = m_argValue.begin();
  while (it != m_argValue.end()) {
    CMICmdArgValBase *pArgObj = *it;
    delete pArgObj;

    // Next
    ++it;
  }
  m_argValue.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Create an CMICmdArgValBase derived object matching the type
// specified
//          and put the option or argument's value inside it.
// Type:    Method.
// Args:    vrTxt   - (R) Text version the option or argument.
//          veType  - (R) The type of argument or option object to create.
// Return:  CMICmdArgValBase * - Option object holding the value.
//                              - NULL = Functional failed.
// Throws:  None.
//--
CMICmdArgValBase *
CMICmdArgValListBase::CreationObj(const CMIUtilString &vrTxt,
                                  const ArgValType_e veType) const {
  CMICmdArgValBase *pOptionObj = nullptr;
  switch (veType) {
  case eArgValType_File:
    pOptionObj = new CMICmdArgValFile();
    break;
  case eArgValType_Consume:
    pOptionObj = new CMICmdArgValConsume();
    break;
  case eArgValType_Number:
    pOptionObj = new CMICmdArgValNumber();
    break;
  case eArgValType_OptionLong:
    pOptionObj = new CMICmdArgValOptionLong();
    break;
  case eArgValType_OptionShort:
    pOptionObj = new CMICmdArgValOptionShort();
    break;
  case eArgValType_String:
    pOptionObj = new CMICmdArgValString();
    break;
  case eArgValType_StringQuoted:
    pOptionObj = new CMICmdArgValString(true, false, false);
    break;
  case eArgValType_StringQuotedNumber:
    pOptionObj = new CMICmdArgValString(true, true, false);
    break;
  case eArgValType_StringQuotedNumberPath:
    pOptionObj = new CMICmdArgValString(true, true, true);
    break;
  case eArgValType_StringAnything:
    pOptionObj = new CMICmdArgValString(true);
    break;
  case eArgValType_ThreadGrp:
    pOptionObj = new CMICmdArgValThreadGrp();
    break;
  default:
    return nullptr;
  }

  CMICmdArgContext argCntxt(vrTxt);
  if (!pOptionObj->Validate(argCntxt))
    return nullptr;

  return pOptionObj;
}

//++
//------------------------------------------------------------------------------------
// Details: Validate the option or argument is the correct type.
// Type:    Method.
// Args:    vrTxt   - (R) Text version the option or argument.
//          veType  - (R) The type of value to expect.
// Return:  bool    - True = Yes expected type present, False = no.
// Throws:  None.
//--
bool CMICmdArgValListBase::IsExpectedCorrectType(
    const CMIUtilString &vrTxt, const ArgValType_e veType) const {
  bool bValid = false;
  switch (veType) {
  case eArgValType_File:
    bValid = CMICmdArgValFile().IsFilePath(vrTxt);
    break;
  case eArgValType_Consume:
    bValid = CMICmdArgValConsume().IsOk();
    break;
  case eArgValType_Number:
    bValid = CMICmdArgValNumber().IsArgNumber(vrTxt);
    break;
  case eArgValType_OptionLong:
    bValid = CMICmdArgValOptionLong().IsArgLongOption(vrTxt);
    break;
  case eArgValType_OptionShort:
    bValid = CMICmdArgValOptionShort().IsArgShortOption(vrTxt);
    break;
  case eArgValType_String:
    bValid = CMICmdArgValString().IsStringArg(vrTxt);
    break;
  case eArgValType_StringQuoted:
    bValid = CMICmdArgValString(true, false, false).IsStringArg(vrTxt);
    break;
  case eArgValType_StringQuotedNumber:
    bValid = CMICmdArgValString(true, true, false).IsStringArg(vrTxt);
    break;
  case eArgValType_StringQuotedNumberPath:
    bValid = CMICmdArgValString(true, true, true).IsStringArg(vrTxt);
    break;
  case eArgValType_StringAnything:
    bValid = CMICmdArgValString(true).IsStringArg(vrTxt);
    break;
  case eArgValType_ThreadGrp:
    bValid = CMICmdArgValThreadGrp().IsArgThreadGrp(vrTxt);
    break;
  default:
    return false;
  }

  return bValid;
}
