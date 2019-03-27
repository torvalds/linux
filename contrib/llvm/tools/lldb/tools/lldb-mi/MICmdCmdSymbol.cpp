//===-- MICmdCmdSymbol.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdSymbolListLines     implementation.

// Third Party Headers:
#include "llvm/ADT/Twine.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBSymbolContextList.h"

// In-house headers:
#include "MICmdArgValFile.h"
#include "MICmdCmdSymbol.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueTuple.h"
#include "MICmnMIValueResult.h"

namespace {
const CMICmnMIValueTuple
CreateMITuplePCLine(const uint32_t addr, const uint32_t line_number) {
  const CMICmnMIValueConst miValueConstAddr("0x" + llvm::Twine::utohexstr(addr).str());
  const CMICmnMIValueConst miValueConstLine(llvm::Twine(line_number).str());
  const CMICmnMIValueResult miValueResultAddr("pc", miValueConstAddr);
  const CMICmnMIValueResult miValueResultLine("line", miValueConstLine);
  CMICmnMIValueTuple miValueTuple(miValueResultAddr);
  miValueTuple.Add(miValueResultLine);
  return miValueTuple;
}
} // namespace

using namespace lldb; // For operator==(const SBAddress &, const SBAddress &).

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSymbolListLines constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSymbolListLines::CMICmdCmdSymbolListLines()
    : m_resultList(false), m_constStrArgNameFile("file") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "symbol-list-lines";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdSymbolListLines::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdSymbolListLines destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdSymbolListLines::~CMICmdCmdSymbolListLines() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The parses the command line
// options
//          arguments to extract values for each of those arguments.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdSymbolListLines::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValFile(m_constStrArgNameFile, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
//          Synopsis: -symbol-list-lines file
//          Ref:
//          http://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Symbol-Query.html#GDB_002fMI-Symbol-Query
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdSymbolListLines::Execute() {
  CMICMDBASE_GETOPTION(pArgFile, File, m_constStrArgNameFile);

  const auto &rSessionInfo(CMICmnLLDBDebugSessionInfo::Instance());
  if (rSessionInfo.GetTarget() == rSessionInfo.GetDebugger().GetDummyTarget()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_TARGET_CURRENT),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  const lldb::SBFileSpec source_file_spec(pArgFile->GetValue().c_str(), true);
  const char *source_file_name = source_file_spec.GetFilename();
  const char *source_file_directory = source_file_spec.GetDirectory();
  const bool has_path = source_file_directory;

  lldb::SBSymbolContextList sc_cu_list =
      CMICmnLLDBDebugSessionInfo::Instance().GetTarget().FindCompileUnits(
          source_file_spec);

  bool found_something = false;
  for (uint32_t i = 0, e = sc_cu_list.GetSize(); i < e; ++i) {
    const lldb::SBCompileUnit cu =
        sc_cu_list.GetContextAtIndex(i).GetCompileUnit();
    for (uint32_t j = 0, e = cu.GetNumLineEntries(); j < e; ++j) {
      const lldb::SBLineEntry line = cu.GetLineEntryAtIndex(j);
      const lldb::SBFileSpec line_spec = line.GetFileSpec();
      if (line_spec.GetFilename() == source_file_name) {
        if (has_path && (line_spec.GetDirectory() != source_file_directory))
          continue;
        // We don't need a line with start address equals to end one,
        // so just skip it.
        const lldb::SBAddress line_start_address = line.GetStartAddress();
        const lldb::SBAddress line_end_address = line.GetEndAddress();
        if (line_start_address == line_end_address)
          continue;
        // We have a matching line.
        found_something = true;
        m_resultList.Add(CreateMITuplePCLine(
            line_start_address.GetFileAddress(),
            line.GetLine()));
      }
    }
  }
  if (!found_something) {
    SetError(MIRSRC(IDS_UTIL_FILE_ERR_INVALID_PATHNAME));
    return MIstatus::failure;
  }
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command prepares a MI Record
// Result
//          for the work carried out in the Execute().
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdSymbolListLines::Acknowledge() {
  // MI print "%s^done,lines=[{pc=\"%d\",line=\"%d\"}...]"
  const CMICmnMIValueResult miValueResult("lines", m_resultList);
  m_miResultRecord = CMICmnMIResultRecord(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult);
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Required by the CMICmdFactory when registering *this command. The
// factory
//          calls this function to create an instance of *this command.
// Type:    Static method.
// Args:    None.
// Return:  CMICmdBase * - Pointer to a new command.
// Throws:  None.
//--
CMICmdBase *CMICmdCmdSymbolListLines::CreateSelf() {
  return new CMICmdCmdSymbolListLines();
}
