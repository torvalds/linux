//===-- MICmnMIValue.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnMIValue.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValue constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValue::CMICmnMIValue()
    : m_strValue(MIRSRC(IDS_WORD_INVALIDBRKTS)), m_bJustConstructed(true) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnMIValue destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnMIValue::~CMICmnMIValue() {}

//++
//------------------------------------------------------------------------------------
// Details: Return the MI value as a string. The string is a direct result of
//          work done on *this value so if not enough data is added then it is
//          possible to return a malformed value. If nothing has been set or
//          added to *this MI value object then text "<Invalid>" will be
//          returned.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - MI output text.
// Throws:  None.
//--
const CMIUtilString &CMICmnMIValue::GetString() const { return m_strValue; }
