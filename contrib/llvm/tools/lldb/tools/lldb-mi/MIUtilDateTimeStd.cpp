//===-- MIUtilDateTimeStd.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MIUtilDateTimeStd.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDateTimeStd constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilDateTimeStd::CMIUtilDateTimeStd() {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDateTimeStd destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilDateTimeStd::~CMIUtilDateTimeStd() {}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve system local current date. Format is MM/DD/YYYY.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text description.
// Throws:  None.
//--
CMIUtilString CMIUtilDateTimeStd::GetDate() {
  CMIUtilString strDate(MIRSRC(IDS_WORD_INVALIDBRKTS));

  std::time(&m_rawTime);
  const std::tm *pTi = std::localtime(&m_rawTime);
  if (std::strftime(&m_pScratch[0], sizeof(m_pScratch), "%d/%m/%y", pTi) > 0)
    strDate = m_pScratch;

  return strDate;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve system local current time. Format is HH:MM:SS 24 hour
// clock.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text description.
// Throws:  None.
//--
CMIUtilString CMIUtilDateTimeStd::GetTime() {
  std::time(&m_rawTime);
  const std::tm *pTi = std::localtime(&m_rawTime);
  const CMIUtilString seconds(CMIUtilString::Format("%d", pTi->tm_sec));
  const CMIUtilString zero((seconds.length() == 1) ? "0" : "");
  const CMIUtilString strTime(CMIUtilString::Format(
      "%d:%d:%s%s", pTi->tm_hour, pTi->tm_min, zero.c_str(), seconds.c_str()));

  return strTime;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve system local current date and time in yyyy-MM-dd--HH-mm-ss
// format for log file names.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString - Text description.
// Throws:  None.
//--
CMIUtilString CMIUtilDateTimeStd::GetDateTimeLogFilename() {
  std::time(&m_rawTime);
  const std::tm *pTi = std::localtime(&m_rawTime);
  const CMIUtilString strTime(CMIUtilString::Format(
      "%d%02d%02d%02d%02d%02d", pTi->tm_year + 1900, pTi->tm_mon, pTi->tm_mday,
      pTi->tm_hour, pTi->tm_min, pTi->tm_sec));

  return strTime;
}
