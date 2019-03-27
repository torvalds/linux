//===-- MICmnLogMediumFile.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// In-house headers:
#include "MICmnLogMediumFile.h"
#include "MICmnResources.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLogMediumFile constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLogMediumFile::CMICmnLogMediumFile()
    : m_constThisMediumName(MIRSRC(IDS_MEDIUMFILE_NAME)),
      m_constMediumFileNameFormat("lldb-mi-%s.log"),
      m_strMediumFileName(MIRSRC(IDS_MEDIUMFILE_ERR_INVALID_PATH)),
      m_strMediumFileDirectory("."),
      m_fileNamePath(MIRSRC(IDS_MEDIUMFILE_ERR_INVALID_PATH)),
      m_eVerbosityType(CMICmnLog::eLogVerbosity_Log),
      m_strDate(CMIUtilDateTimeStd().GetDate()),
      m_fileHeaderTxt(MIRSRC(IDS_MEDIUMFILE_ERR_FILE_HEADER)) {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLogMediumFile destructor.
// Type:    Overridden.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLogMediumFile::~CMICmnLogMediumFile() {}

//++
//------------------------------------------------------------------------------------
// Details: Get the singleton instance of *this class.
// Type:    Static.
// Args:    None.
// Return:  CMICmnLogMediumFile - Reference to *this object.
// Throws:  None.
//--
CMICmnLogMediumFile &CMICmnLogMediumFile::Instance() {
  static CMICmnLogMediumFile instance;

  return instance;
}

//++
//------------------------------------------------------------------------------------
// Details: Initialize setup *this medium ready for use.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::Initialize() {
  m_bInitialized = true;
  return FileFormFileNamePath();
}

//++
//------------------------------------------------------------------------------------
// Details: Unbind detach or release resources used by *this medium.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
bool CMICmnLogMediumFile::Shutdown() {
  if (m_bInitialized) {
    m_bInitialized = false;
    m_file.Close();
  }
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the name of *this medium.
// Type:    Overridden.
// Args:    None.
// Return:  CMIUtilString - Text data.
// Throws:  None.
//--
const CMIUtilString &CMICmnLogMediumFile::GetName() const {
  return m_constThisMediumName;
}

//++
//------------------------------------------------------------------------------------
// Details: The callee client calls the write function on the Logger. The data
// to be
//          written is given out to all the mediums registered. The verbosity
//          type parameter
//          indicates to the medium the type of data or message given to it. The
//          medium has
//          modes of verbosity and depending on the verbosity set determines
//          which data is
//          sent to the medium's output.
// Type:    Method.
// Args:    vData       - (R) The data to write to the logger.
//          veType      - (R) Verbosity type.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::Write(const CMIUtilString &vData,
                                const CMICmnLog::ELogVerbosity veType) {
  if (m_bInitialized && m_file.IsOk()) {
    const bool bDoWrite = (m_eVerbosityType & veType);
    if (bDoWrite) {
      bool bNewCreated = false;
      bool bOk = m_file.CreateWrite(m_fileNamePath, bNewCreated);
      if (bOk) {
        if (bNewCreated)
          bOk = FileWriteHeader();
        bOk = bOk && FileWriteEnglish(MassagedData(vData, veType));
      }
      return bOk;
    }
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve *this medium's last error condition.
// Type:    Method.
// Args:    None.
// Return:  CString & -  Text description.
// Throws:  None.
//--
const CMIUtilString &CMICmnLogMediumFile::GetError() const {
  return m_strMILastErrorDescription;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the verbosity mode for this medium.
// Type:    Method.
// Args:    veType  - (R) Mask value.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::SetVerbosity(const MIuint veType) {
  m_eVerbosityType = veType;
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get the verbosity mode for this medium.
// Type:    Method.
// Args:    veType  - (R) Mask value.
// Return:  CMICmnLog::ELogVerbosity - Mask value.
// Throws:  None.
//--
MIuint CMICmnLogMediumFile::GetVerbosity() const { return m_eVerbosityType; }

//++
//------------------------------------------------------------------------------------
// Details: Write data to a file English font.
// Type:    Method.
// Args:    vData   - (R) The data to write to the logger.
// Return:  None.
// Throws:  None.
//--
bool CMICmnLogMediumFile::FileWriteEnglish(const CMIUtilString &vData) {
  return m_file.Write(vData);
}

//++
//------------------------------------------------------------------------------------
// Details: Determine and form the medium file's directory path and name.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::FileFormFileNamePath() {
  ClrErrorDescription();

  m_fileNamePath = MIRSRC(IDS_MEDIUMFILE_ERR_INVALID_PATH);

  CMIUtilDateTimeStd date;
  m_strMediumFileName =
      CMIUtilString::Format(m_constMediumFileNameFormat.c_str(),
                            date.GetDateTimeLogFilename().c_str());

#if defined(_MSC_VER)
  m_fileNamePath = CMIUtilString::Format(
      "%s\\%s", m_strMediumFileDirectory.c_str(), m_strMediumFileName.c_str());
#else
  m_fileNamePath = CMIUtilString::Format(
      "%s/%s", m_strMediumFileDirectory.c_str(), m_strMediumFileName.c_str());
#endif // defined ( _MSC_VER )

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the medium file's directory path and name.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - File path.
// Throws:  None.
//--
const CMIUtilString &CMICmnLogMediumFile::GetFileNamePath() const {
  return m_fileNamePath;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the medium file's name.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - File name.
// Throws:  None.
//--
const CMIUtilString &CMICmnLogMediumFile::GetFileName() const {
  return m_strMediumFileName;
}

//++
//------------------------------------------------------------------------------------
// Details: Massage the data to behave correct when submitted to file. Insert
// extra log
//          specific text. The veType is there to allow in the future to parse
//          the log and
//          filter in out specific types of message to make viewing the log more
//          manageable.
// Type:    Method.
// Args:    vData   - (R) Raw data.
//          veType  - (R) Message type.
// Return:  CMIUtilString - Massaged data.
// Throws:  None.
//--
CMIUtilString
CMICmnLogMediumFile::MassagedData(const CMIUtilString &vData,
                                  const CMICmnLog::ELogVerbosity veType) {
  const CMIUtilString strCr("\n");
  CMIUtilString data;
  const char verbosityCode(ConvertLogVerbosityTypeToId(veType));
  const CMIUtilString dt(CMIUtilString::Format("%s %s", m_strDate.c_str(),
                                               m_dateTime.GetTime().c_str()));

  data = CMIUtilString::Format("%c,%s,%s", verbosityCode, dt.c_str(),
                               vData.c_str());
  data = ConvertCr(data);

  // Look for EOL...
  const size_t pos = vData.rfind(strCr);
  if (pos == vData.size())
    return data;

  // ... did not have an EOL so add one
  data += GetLineReturn();

  return data;
}

//++
//------------------------------------------------------------------------------------
// Details: Convert the Log's verbosity type number into a single char
// character.
// Type:    Method.
// Args:    veType  - (R) Message type.
// Return:  wchar_t - A letter.
// Throws:  None.
//--
char CMICmnLogMediumFile::ConvertLogVerbosityTypeToId(
    const CMICmnLog::ELogVerbosity veType) const {
  char c = 0;
  if (veType != 0) {
    MIuint cnt = 0;
    MIuint number(veType);
    while (1 != number) {
      number = number >> 1;
      ++cnt;
    }
    c = 'A' + cnt;
  } else {
    c = '*';
  }

  return c;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve state of whether the file medium is ok.
// Type:    Method.
// Args:    None.
// Return:  True - file ok.
//          False - file has a problem.
// Throws:  None.
//--
bool CMICmnLogMediumFile::IsOk() const { return m_file.IsOk(); }

//++
//------------------------------------------------------------------------------------
// Details: Status on the file log medium existing already.
// Type:    Method.
// Args:    None.
// Return:  True - Exists.
//          False - Not found.
// Throws:  None.
//--
bool CMICmnLogMediumFile::IsFileExist() const {
  return m_file.IsFileExist(GetFileNamePath());
}

//++
//------------------------------------------------------------------------------------
// Details: Write the header text the logger file.
// Type:    Method.
// Args:    vText   - (R) Text.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::FileWriteHeader() {
  return FileWriteEnglish(ConvertCr(m_fileHeaderTxt));
}

//++
//------------------------------------------------------------------------------------
// Details: Convert any carriage line returns to be compatible with the platform
// the
//          Log file is being written to.
// Type:    Method.
// Args:    vData   - (R) Text data.
// Return:  CMIUtilString - Converted string data.
// Throws:  None.
//--
CMIUtilString CMICmnLogMediumFile::ConvertCr(const CMIUtilString &vData) const {
  const CMIUtilString strCr("\n");
  const CMIUtilString &rCrCmpat(GetLineReturn());

  if (strCr == rCrCmpat)
    return vData;

  const size_t nSizeCmpat(rCrCmpat.size());
  const size_t nSize(strCr.size());
  CMIUtilString strConv(vData);
  size_t pos = strConv.find(strCr);
  while (pos != CMIUtilString::npos) {
    strConv.replace(pos, nSize, rCrCmpat);
    pos = strConv.find(strCr, pos + nSizeCmpat);
  }

  return strConv;
}

//++
//------------------------------------------------------------------------------------
// Details: Set the header text that is written to the logger file at the
// beginning.
// Type:    Method.
// Args:    vText   - (R) Text.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::SetHeaderTxt(const CMIUtilString &vText) {
  m_fileHeaderTxt = vText;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the file current carriage line return characters used.
// Type:    Method.
// Args:    None.
// Return:  CMIUtilString & - Text.
// Throws:  None.
//--
const CMIUtilString &CMICmnLogMediumFile::GetLineReturn() const {
  return m_file.GetLineReturn();
}

//++
//------------------------------------------------------------------------------------
// Details: Set the directory to place the log file.
// Type:    Method.
// Args:    vPath   - (R) Path to log.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLogMediumFile::SetDirectory(const CMIUtilString &vPath) {
  m_strMediumFileDirectory = vPath;

  return FileFormFileNamePath();
}
