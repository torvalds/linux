//===-- MICmdCmdData.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    CMICmdCmdDataEvaluateExpression     implementation.
//              CMICmdCmdDataDisassemble            implementation.
//              CMICmdCmdDataReadMemoryBytes        implementation.
//              CMICmdCmdDataReadMemory             implementation.
//              CMICmdCmdDataListRegisterNames      implementation.
//              CMICmdCmdDataListRegisterValues     implementation.
//              CMICmdCmdDataListRegisterChanged    implementation.
//              CMICmdCmdDataWriteMemoryBytes       implementation.
//              CMICmdCmdDataWriteMemory            implementation.
//              CMICmdCmdDataInfoLine               implementation.

// Third Party Headers:
#include "lldb/API/SBInstruction.h"
#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBThread.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <inttypes.h>
#include <string>

// In-house headers:
#include "MICmdArgValConsume.h"
#include "MICmdArgValListOfN.h"
#include "MICmdArgValNumber.h"
#include "MICmdArgValOptionLong.h"
#include "MICmdArgValOptionShort.h"
#include "MICmdArgValString.h"
#include "MICmdArgValThreadGrp.h"
#include "MICmdCmdData.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugSessionInfoVarObj.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnLLDBProxySBValue.h"
#include "MICmnLLDBUtilSBValue.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "Platform.h"

namespace {
CMIUtilString IntToHexAddrStr(uint32_t number) {
  return CMIUtilString("0x" + llvm::Twine::utohexstr(number).str());
}
} // namespace

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataEvaluateExpression constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataEvaluateExpression::CMICmdCmdDataEvaluateExpression()
    : m_bExpressionValid(true), m_bEvaluatedExpression(true), m_strValue("??"),
      m_bFoundInvalidChar(false), m_cExpressionInvalidChar(0x00),
      m_constStrArgExpr("expr") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-evaluate-expression";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataEvaluateExpression::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataEvaluateExpression destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataEvaluateExpression::~CMICmdCmdDataEvaluateExpression() {}

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
bool CMICmdCmdDataEvaluateExpression::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgExpr, true, true, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataEvaluateExpression::Execute() {
  CMICMDBASE_GETOPTION(pArgExpr, String, m_constStrArgExpr);

  const CMIUtilString &rExpression(pArgExpr->GetValue());
  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBThread thread = sbProcess.GetSelectedThread();
  m_bExpressionValid = (thread.GetNumFrames() > 0);
  if (!m_bExpressionValid)
    return MIstatus::success;

  lldb::SBFrame frame = thread.GetSelectedFrame();
  lldb::SBValue value = frame.EvaluateExpression(rExpression.c_str());
  m_Error = value.GetError();
  if (!value.IsValid() || m_Error.Fail())
    value = frame.FindVariable(rExpression.c_str());
  const CMICmnLLDBUtilSBValue utilValue(value, true);
  if (!utilValue.IsValid() || utilValue.IsValueUnknown()) {
    m_bEvaluatedExpression = false;
    return MIstatus::success;
  }
  if (!utilValue.HasName()) {
    if (HaveInvalidCharacterInExpression(rExpression,
                                         m_cExpressionInvalidChar)) {
      m_bFoundInvalidChar = true;
      return MIstatus::success;
    }

    m_strValue = rExpression;
    return MIstatus::success;
  }
  if (rExpression.IsQuoted()) {
    m_strValue = rExpression.Trim('\"');
    return MIstatus::success;
  }
  m_strValue = utilValue.GetValue(true).Escape().AddSlashes();
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
bool CMICmdCmdDataEvaluateExpression::Acknowledge() {
  if (m_bExpressionValid) {
    if (m_bEvaluatedExpression) {
      if (m_bFoundInvalidChar) {
        const CMICmnMIValueConst miValueConst(CMIUtilString::Format(
            "Invalid character '%c' in expression", m_cExpressionInvalidChar));
        const CMICmnMIValueResult miValueResult("msg", miValueConst);
        const CMICmnMIResultRecord miRecordResult(
            m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
            miValueResult);
        m_miResultRecord = miRecordResult;
        return MIstatus::success;
      }

      const CMICmnMIValueConst miValueConst(m_strValue);
      const CMICmnMIValueResult miValueResult("value", miValueConst);
      const CMICmnMIResultRecord miRecordResult(
          m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
          miValueResult);
      m_miResultRecord = miRecordResult;
      return MIstatus::success;
    }
    CMIUtilString mi_error_msg = "Could not evaluate expression";
    if (const char *err_msg = m_Error.GetCString())
      mi_error_msg = err_msg;
    const CMICmnMIValueConst miValueConst(mi_error_msg.Escape(true));
    const CMICmnMIValueResult miValueResult("msg", miValueConst);
    const CMICmnMIResultRecord miRecordResult(
        m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
        miValueResult);
    m_miResultRecord = miRecordResult;
    return MIstatus::success;
  }

  const CMICmnMIValueConst miValueConst("Invalid expression");
  const CMICmnMIValueResult miValueResult("msg", miValueConst);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataEvaluateExpression::CreateSelf() {
  return new CMICmdCmdDataEvaluateExpression();
}

//++
//------------------------------------------------------------------------------------
// Details: Examine the expression string to see if it contains invalid
// characters.
// Type:    Method.
// Args:    vrExpr          - (R) Expression string given to *this command.
//          vrwInvalidChar  - (W) True = Invalid character found, false =
//          nothing found.
// Return:  bool - True = Invalid character found, false = nothing found.
// Throws:  None.
//--
bool CMICmdCmdDataEvaluateExpression::HaveInvalidCharacterInExpression(
    const CMIUtilString &vrExpr, char &vrwInvalidChar) {
  static const std::string strInvalidCharacters(";#\\");
  const size_t nInvalidCharacterOffset =
      vrExpr.find_first_of(strInvalidCharacters);
  const bool bFoundInvalidCharInExpression =
      (nInvalidCharacterOffset != CMIUtilString::npos);
  vrwInvalidChar =
      bFoundInvalidCharInExpression ? vrExpr[nInvalidCharacterOffset] : 0x00;
  return bFoundInvalidCharInExpression;
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataDisassemble constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataDisassemble::CMICmdCmdDataDisassemble()
    : m_constStrArgAddrStart("s"), m_constStrArgAddrEnd("e"),
      m_constStrArgMode("mode"), m_miValueList(true) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-disassemble";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataDisassemble::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataDisassemble destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataDisassemble::~CMICmdCmdDataDisassemble() {}

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
bool CMICmdCmdDataDisassemble::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValOptionShort(
      m_constStrArgAddrStart, true, true,
      CMICmdArgValListBase::eArgValType_StringQuotedNumber, 1));
  m_setCmdArgs.Add(new CMICmdArgValOptionShort(
      m_constStrArgAddrEnd, true, true,
      CMICmdArgValListBase::eArgValType_StringQuotedNumber, 1));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgMode, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataDisassemble::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgAddrStart, OptionShort, m_constStrArgAddrStart);
  CMICMDBASE_GETOPTION(pArgAddrEnd, OptionShort, m_constStrArgAddrEnd);
  CMICMDBASE_GETOPTION(pArgMode, Number, m_constStrArgMode);

  // Retrieve the --thread option's thread ID (only 1)
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound() &&
      !pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nThreadId)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgThread.c_str()));
    return MIstatus::failure;
  }
  CMIUtilString strAddrStart;
  if (!pArgAddrStart->GetExpectedOption<CMICmdArgValString, CMIUtilString>(
          strAddrStart)) {
    SetError(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_DISASM_ADDR_START_INVALID),
        m_cmdData.strMiCmd.c_str(), m_constStrArgAddrStart.c_str()));
    return MIstatus::failure;
  }
  MIint64 nAddrStart = 0;
  if (!strAddrStart.ExtractNumber(nAddrStart)) {
    SetError(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_DISASM_ADDR_START_INVALID),
        m_cmdData.strMiCmd.c_str(), m_constStrArgAddrStart.c_str()));
    return MIstatus::failure;
  }

  CMIUtilString strAddrEnd;
  if (!pArgAddrEnd->GetExpectedOption<CMICmdArgValString, CMIUtilString>(
          strAddrEnd)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_DISASM_ADDR_END_INVALID),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgAddrEnd.c_str()));
    return MIstatus::failure;
  }
  MIint64 nAddrEnd = 0;
  if (!strAddrEnd.ExtractNumber(nAddrEnd)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_DISASM_ADDR_END_INVALID),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgAddrEnd.c_str()));
    return MIstatus::failure;
  }
  const MIuint nDisasmMode = pArgMode->GetValue();

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBTarget sbTarget = rSessionInfo.GetTarget();
  lldb::addr_t lldbStartAddr = static_cast<lldb::addr_t>(nAddrStart);
  lldb::SBInstructionList instructions = sbTarget.ReadInstructions(
      lldb::SBAddress(lldbStartAddr, sbTarget), nAddrEnd - nAddrStart);
  const MIuint nInstructions = instructions.GetSize();
  // Calculate the offset of first instruction so that we can generate offset
  // starting at 0
  lldb::addr_t start_offset = 0;
  if (nInstructions > 0)
    start_offset =
        instructions.GetInstructionAtIndex(0).GetAddress().GetOffset();

  for (size_t i = 0; i < nInstructions; i++) {
    const char *pUnknown = "??";
    lldb::SBInstruction instrt = instructions.GetInstructionAtIndex(i);
    const char *pStrMnemonic = instrt.GetMnemonic(sbTarget);
    pStrMnemonic = (pStrMnemonic != nullptr) ? pStrMnemonic : pUnknown;
    const char *pStrComment = instrt.GetComment(sbTarget);
    CMIUtilString strComment;
    if (pStrComment != nullptr && *pStrComment != '\0')
      strComment = CMIUtilString::Format("; %s", pStrComment);
    lldb::SBAddress address = instrt.GetAddress();
    lldb::addr_t addr = address.GetLoadAddress(sbTarget);
    const char *pFnName = address.GetFunction().GetName();
    pFnName = (pFnName != nullptr) ? pFnName : pUnknown;
    lldb::addr_t addrOffSet = address.GetOffset() - start_offset;
    const char *pStrOperands = instrt.GetOperands(sbTarget);
    pStrOperands = (pStrOperands != nullptr) ? pStrOperands : pUnknown;
    const size_t instrtSize = instrt.GetByteSize();

    // MI "{address=\"0x%016" PRIx64
    // "\",func-name=\"%s\",offset=\"%lld\",inst=\"%s %s\"}"
    const CMICmnMIValueConst miValueConst(
        CMIUtilString::Format("0x%016" PRIx64, addr));
    const CMICmnMIValueResult miValueResult("address", miValueConst);
    CMICmnMIValueTuple miValueTuple(miValueResult);
    const CMICmnMIValueConst miValueConst2(pFnName);
    const CMICmnMIValueResult miValueResult2("func-name", miValueConst2);
    miValueTuple.Add(miValueResult2);
    const CMICmnMIValueConst miValueConst3(
        CMIUtilString::Format("%lld", addrOffSet));
    const CMICmnMIValueResult miValueResult3("offset", miValueConst3);
    miValueTuple.Add(miValueResult3);
    const CMICmnMIValueConst miValueConst4(
        CMIUtilString::Format("%d", instrtSize));
    const CMICmnMIValueResult miValueResult4("size", miValueConst4);
    miValueTuple.Add(miValueResult4);
    const CMICmnMIValueConst miValueConst5(
        CMIUtilString::Format("%s %s%s", pStrMnemonic, pStrOperands,
                              strComment.Escape(true).c_str()));
    const CMICmnMIValueResult miValueResult5("inst", miValueConst5);
    miValueTuple.Add(miValueResult5);

    if (nDisasmMode == 1) {
      lldb::SBLineEntry lineEntry = address.GetLineEntry();
      const MIuint nLine = lineEntry.GetLine();
      const char *pFileName = lineEntry.GetFileSpec().GetFilename();
      pFileName = (pFileName != nullptr) ? pFileName : pUnknown;

      // MI "src_and_asm_line={line=\"%u\",file=\"%s\",line_asm_insn=[ ]}"
      const CMICmnMIValueConst miValueConst(
          CMIUtilString::Format("0x%u", nLine));
      const CMICmnMIValueResult miValueResult("line", miValueConst);
      CMICmnMIValueTuple miValueTuple2(miValueResult);
      const CMICmnMIValueConst miValueConst2(pFileName);
      const CMICmnMIValueResult miValueResult2("file", miValueConst2);
      miValueTuple2.Add(miValueResult2);
      const CMICmnMIValueList miValueList(miValueTuple);
      const CMICmnMIValueResult miValueResult3("line_asm_insn", miValueList);
      miValueTuple2.Add(miValueResult3);
      const CMICmnMIValueResult miValueResult4("src_and_asm_line",
                                               miValueTuple2);
      m_miValueList.Add(miValueResult4);
    } else {
      m_miValueList.Add(miValueTuple);
    }
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
bool CMICmdCmdDataDisassemble::Acknowledge() {
  const CMICmnMIValueResult miValueResult("asm_insns", m_miValueList);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataDisassemble::CreateSelf() {
  return new CMICmdCmdDataDisassemble();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataReadMemoryBytes constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataReadMemoryBytes::CMICmdCmdDataReadMemoryBytes()
    : m_constStrArgByteOffset("o"), m_constStrArgAddrExpr("address"),
      m_constStrArgNumBytes("count"), m_pBufferMemory(nullptr), m_nAddrStart(0),
      m_nAddrNumBytesToRead(0) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-read-memory-bytes";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataReadMemoryBytes::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataReadMemoryBytes destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataReadMemoryBytes::~CMICmdCmdDataReadMemoryBytes() {
  if (m_pBufferMemory != nullptr) {
    delete[] m_pBufferMemory;
    m_pBufferMemory = nullptr;
  }
}

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
bool CMICmdCmdDataReadMemoryBytes::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValOptionShort(m_constStrArgByteOffset, false, true,
                                  CMICmdArgValListBase::eArgValType_Number, 1));
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgAddrExpr, true, true, true, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumBytes, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Function succeeded.
//          MIstatus::failure - Function failed.
// Throws:  None.
//--
bool CMICmdCmdDataReadMemoryBytes::Execute() {
  CMICMDBASE_GETOPTION(pArgThread, OptionLong, m_constStrArgThread);
  CMICMDBASE_GETOPTION(pArgFrame, OptionLong, m_constStrArgFrame);
  CMICMDBASE_GETOPTION(pArgAddrOffset, OptionShort, m_constStrArgByteOffset);
  CMICMDBASE_GETOPTION(pArgAddrExpr, String, m_constStrArgAddrExpr);
  CMICMDBASE_GETOPTION(pArgNumBytes, Number, m_constStrArgNumBytes);

  // get the --thread option value
  MIuint64 nThreadId = UINT64_MAX;
  if (pArgThread->GetFound() &&
      !pArgThread->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nThreadId)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgThread.c_str()));
    return MIstatus::failure;
  }

  // get the --frame option value
  MIuint64 nFrame = UINT64_MAX;
  if (pArgFrame->GetFound() &&
      !pArgFrame->GetExpectedOption<CMICmdArgValNumber, MIuint64>(nFrame)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgFrame.c_str()));
    return MIstatus::failure;
  }

  // get the -o option value
  MIuint64 nAddrOffset = 0;
  if (pArgAddrOffset->GetFound() &&
      !pArgAddrOffset->GetExpectedOption<CMICmdArgValNumber, MIuint64>(
          nAddrOffset)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgByteOffset.c_str()));
    return MIstatus::failure;
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  if (!sbProcess.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBThread thread = (nThreadId != UINT64_MAX)
                              ? sbProcess.GetThreadByIndexID(nThreadId)
                              : sbProcess.GetSelectedThread();
  if (!thread.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_THREAD_INVALID),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  lldb::SBFrame frame = (nFrame != UINT64_MAX) ? thread.GetFrameAtIndex(nFrame)
                                               : thread.GetSelectedFrame();
  if (!frame.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_FRAME_INVALID),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  const CMIUtilString &rAddrExpr = pArgAddrExpr->GetValue();
  lldb::SBValue addrExprValue = frame.EvaluateExpression(rAddrExpr.c_str());
  lldb::SBError error = addrExprValue.GetError();
  if (error.Fail()) {
    SetError(error.GetCString());
    return MIstatus::failure;
  } else if (!addrExprValue.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_EXPR_INVALID),
                                   rAddrExpr.c_str()));
    return MIstatus::failure;
  }

  MIuint64 nAddrStart = 0;
  if (!CMICmnLLDBProxySBValue::GetValueAsUnsigned(addrExprValue, nAddrStart)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_EXPR_INVALID),
                                   rAddrExpr.c_str()));
    return MIstatus::failure;
  }

  nAddrStart += nAddrOffset;
  const MIuint64 nAddrNumBytes = pArgNumBytes->GetValue();

  m_pBufferMemory = new unsigned char[nAddrNumBytes];
  if (m_pBufferMemory == nullptr) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_MEMORY_ALLOC_FAILURE),
                                   m_cmdData.strMiCmd.c_str(), nAddrNumBytes));
    return MIstatus::failure;
  }

  const MIuint64 nReadBytes =
      sbProcess.ReadMemory(static_cast<lldb::addr_t>(nAddrStart),
                           (void *)m_pBufferMemory, nAddrNumBytes, error);
  if (nReadBytes != nAddrNumBytes) {
    SetError(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_LLDB_ERR_NOT_READ_WHOLE_BLK),
        m_cmdData.strMiCmd.c_str(), nAddrNumBytes, nAddrStart));
    return MIstatus::failure;
  }
  if (error.Fail()) {
    lldb::SBStream err;
    const bool bOk = error.GetDescription(err);
    MIunused(bOk);
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_LLDB_ERR_READ_MEM_BYTES),
                                   m_cmdData.strMiCmd.c_str(), nAddrNumBytes,
                                   nAddrStart, err.GetData()));
    return MIstatus::failure;
  }

  m_nAddrStart = nAddrStart;
  m_nAddrNumBytesToRead = nAddrNumBytes;

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
bool CMICmdCmdDataReadMemoryBytes::Acknowledge() {
  // MI: memory=[{begin=\"0x%016" PRIx64 "\",offset=\"0x%016" PRIx64"
  // \",end=\"0x%016" PRIx64 "\",contents=\" \" }]"
  const CMICmnMIValueConst miValueConst(
      CMIUtilString::Format("0x%016" PRIx64, m_nAddrStart));
  const CMICmnMIValueResult miValueResult("begin", miValueConst);
  CMICmnMIValueTuple miValueTuple(miValueResult);
  const MIuint64 nAddrOffset = 0;
  const CMICmnMIValueConst miValueConst2(
      CMIUtilString::Format("0x%016" PRIx64, nAddrOffset));
  const CMICmnMIValueResult miValueResult2("offset", miValueConst2);
  miValueTuple.Add(miValueResult2);
  const CMICmnMIValueConst miValueConst3(CMIUtilString::Format(
      "0x%016" PRIx64, m_nAddrStart + m_nAddrNumBytesToRead));
  const CMICmnMIValueResult miValueResult3("end", miValueConst3);
  miValueTuple.Add(miValueResult3);

  // MI: contents=\" \"
  CMIUtilString strContent;
  strContent.reserve((m_nAddrNumBytesToRead << 1) + 1);
  for (MIuint64 i = 0; i < m_nAddrNumBytesToRead; i++) {
    strContent += CMIUtilString::Format("%02hhx", m_pBufferMemory[i]);
  }
  const CMICmnMIValueConst miValueConst4(strContent);
  const CMICmnMIValueResult miValueResult4("contents", miValueConst4);
  miValueTuple.Add(miValueResult4);
  const CMICmnMIValueList miValueList(miValueTuple);
  const CMICmnMIValueResult miValueResult5("memory", miValueList);

  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult5);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataReadMemoryBytes::CreateSelf() {
  return new CMICmdCmdDataReadMemoryBytes();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataReadMemory constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataReadMemory::CMICmdCmdDataReadMemory() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-read-memory";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataReadMemory::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataReadMemory destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataReadMemory::~CMICmdCmdDataReadMemory() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataReadMemory::Execute() {
  // Do nothing - command deprecated use "data-read-memory-bytes" command
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
bool CMICmdCmdDataReadMemory::Acknowledge() {
  // Command CMICmdCmdSupportListFeatures sends "data-read-memory-bytes" which
  // causes this command not to be called
  const CMICmnMIValueConst miValueConst(
      MIRSRC(IDS_CMD_ERR_NOT_IMPLEMENTED_DEPRECATED));
  const CMICmnMIValueResult miValueResult("msg", miValueConst);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataReadMemory::CreateSelf() {
  return new CMICmdCmdDataReadMemory();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataListRegisterNames constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterNames::CMICmdCmdDataListRegisterNames()
    : m_constStrArgRegNo("regno"), m_miValueList(true) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-list-register-names";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataListRegisterNames::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataReadMemoryBytes destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterNames::~CMICmdCmdDataListRegisterNames() {}

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
bool CMICmdCmdDataListRegisterNames::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValListOfN(m_constStrArgRegNo, false, false,
                              CMICmdArgValListBase::eArgValType_Number));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataListRegisterNames::Execute() {
  CMICMDBASE_GETOPTION(pArgRegNo, ListOfN, m_constStrArgRegNo);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  if (!sbProcess.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  const CMICmdArgValListBase::VecArgObjPtr_t &rVecRegNo(
      pArgRegNo->GetExpectedOptions());
  if (!rVecRegNo.empty()) {
    // List of required registers
    CMICmdArgValListBase::VecArgObjPtr_t::const_iterator it = rVecRegNo.begin();
    while (it != rVecRegNo.end()) {
      const CMICmdArgValNumber *pRegNo = static_cast<CMICmdArgValNumber *>(*it);
      const MIuint nRegIndex = pRegNo->GetValue();
      lldb::SBValue regValue = GetRegister(nRegIndex);
      if (regValue.IsValid()) {
        const CMICmnMIValueConst miValueConst(
            CMICmnLLDBUtilSBValue(regValue).GetName());
        m_miValueList.Add(miValueConst);
      }

      // Next
      ++it;
    }
  } else {
    // List of all registers
    lldb::SBThread thread = sbProcess.GetSelectedThread();
    lldb::SBFrame frame = thread.GetSelectedFrame();
    lldb::SBValueList registers = frame.GetRegisters();
    const MIuint nRegisters = registers.GetSize();
    for (MIuint i = 0; i < nRegisters; i++) {
      lldb::SBValue value = registers.GetValueAtIndex(i);
      const MIuint nRegChildren = value.GetNumChildren();
      for (MIuint j = 0; j < nRegChildren; j++) {
        lldb::SBValue regValue = value.GetChildAtIndex(j);
        if (regValue.IsValid()) {
          const CMICmnMIValueConst miValueConst(
              CMICmnLLDBUtilSBValue(regValue).GetName());
          m_miValueList.Add(miValueConst);
        }
      }
    }
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
bool CMICmdCmdDataListRegisterNames::Acknowledge() {
  const CMICmnMIValueResult miValueResult("register-names", m_miValueList);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataListRegisterNames::CreateSelf() {
  return new CMICmdCmdDataListRegisterNames();
}

//++
//------------------------------------------------------------------------------------
// Details: Required by the CMICmdFactory when registering *this command. The
// factory
//          calls this function to create an instance of *this command.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBValue - LLDB SBValue object.
// Throws:  None.
//--
lldb::SBValue
CMICmdCmdDataListRegisterNames::GetRegister(const MIuint vRegisterIndex) const {
  lldb::SBThread thread =
      CMICmnLLDBDebugSessionInfo::Instance().GetProcess().GetSelectedThread();
  lldb::SBFrame frame = thread.GetSelectedFrame();
  lldb::SBValueList registers = frame.GetRegisters();
  const MIuint nRegisters = registers.GetSize();
  MIuint nRegisterIndex(vRegisterIndex);
  for (MIuint i = 0; i < nRegisters; i++) {
    lldb::SBValue value = registers.GetValueAtIndex(i);
    const MIuint nRegChildren = value.GetNumChildren();
    if (nRegisterIndex >= nRegChildren) {
      nRegisterIndex -= nRegChildren;
      continue;
    }

    lldb::SBValue value2 = value.GetChildAtIndex(nRegisterIndex);
    if (value2.IsValid()) {
      return value2;
    }
  }

  return lldb::SBValue();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataListRegisterValues constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterValues::CMICmdCmdDataListRegisterValues()
    : m_constStrArgSkip("skip-unavailable"), m_constStrArgFormat("fmt"),
      m_constStrArgRegNo("regno"), m_miValueList(true) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-list-register-values";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataListRegisterValues::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataListRegisterValues destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterValues::~CMICmdCmdDataListRegisterValues() {}

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
bool CMICmdCmdDataListRegisterValues::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValOptionLong(m_constStrArgThread, false, false,
                                 CMICmdArgValListBase::eArgValType_Number, 1));
  m_setCmdArgs.Add(new CMICmdArgValOptionLong(m_constStrArgSkip, false, false));
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgFormat, true, true));
  m_setCmdArgs.Add(
      new CMICmdArgValListOfN(m_constStrArgRegNo, false, true,
                              CMICmdArgValListBase::eArgValType_Number));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataListRegisterValues::Execute() {
  CMICMDBASE_GETOPTION(pArgFormat, String, m_constStrArgFormat);
  CMICMDBASE_GETOPTION(pArgRegNo, ListOfN, m_constStrArgRegNo);

  const CMIUtilString &rStrFormat(pArgFormat->GetValue());
  if (rStrFormat.length() != 1) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_FORMAT_TYPE),
                                   m_cmdData.strMiCmd.c_str(),
                                   rStrFormat.c_str()));
    return MIstatus::failure;
  }
  const CMICmnLLDBDebugSessionInfoVarObj::varFormat_e eFormat =
      CMICmnLLDBDebugSessionInfoVarObj::GetVarFormatForChar(rStrFormat[0]);
  if (eFormat == CMICmnLLDBDebugSessionInfoVarObj::eVarFormat_Invalid) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_FORMAT_TYPE),
                                   m_cmdData.strMiCmd.c_str(),
                                   rStrFormat.c_str()));
    return MIstatus::failure;
  }

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  if (!sbProcess.IsValid()) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_INVALID_PROCESS),
                                   m_cmdData.strMiCmd.c_str()));
    return MIstatus::failure;
  }

  const CMICmdArgValListBase::VecArgObjPtr_t &rVecRegNo(
      pArgRegNo->GetExpectedOptions());
  if (!rVecRegNo.empty()) {
    // List of required registers
    CMICmdArgValListBase::VecArgObjPtr_t::const_iterator it = rVecRegNo.begin();
    while (it != rVecRegNo.end()) {
      const CMICmdArgValNumber *pRegNo = static_cast<CMICmdArgValNumber *>(*it);
      const MIuint nRegIndex = pRegNo->GetValue();
      lldb::SBValue regValue = GetRegister(nRegIndex);
      if (regValue.IsValid()) {
        AddToOutput(nRegIndex, regValue, eFormat);
      }

      // Next
      ++it;
    }
  } else {
    // No register numbers are provided. Output all registers.
    lldb::SBThread thread = sbProcess.GetSelectedThread();
    lldb::SBFrame frame = thread.GetSelectedFrame();
    lldb::SBValueList registers = frame.GetRegisters();
    const MIuint nRegisters = registers.GetSize();
    MIuint nRegIndex = 0;
    for (MIuint i = 0; i < nRegisters; i++) {
      lldb::SBValue value = registers.GetValueAtIndex(i);
      const MIuint nRegChildren = value.GetNumChildren();
      for (MIuint j = 0; j < nRegChildren; j++) {
        lldb::SBValue regValue = value.GetChildAtIndex(j);
        if (regValue.IsValid()) {
          AddToOutput(nRegIndex, regValue, eFormat);
        }

        // Next
        ++nRegIndex;
      }
    }
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
bool CMICmdCmdDataListRegisterValues::Acknowledge() {
  const CMICmnMIValueResult miValueResult("register-values", m_miValueList);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataListRegisterValues::CreateSelf() {
  return new CMICmdCmdDataListRegisterValues();
}

//++
//------------------------------------------------------------------------------------
// Details: Required by the CMICmdFactory when registering *this command. The
// factory
//          calls this function to create an instance of *this command.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBValue - LLDB SBValue object.
// Throws:  None.
//--
lldb::SBValue CMICmdCmdDataListRegisterValues::GetRegister(
    const MIuint vRegisterIndex) const {
  lldb::SBThread thread =
      CMICmnLLDBDebugSessionInfo::Instance().GetProcess().GetSelectedThread();
  lldb::SBFrame frame = thread.GetSelectedFrame();
  lldb::SBValueList registers = frame.GetRegisters();
  const MIuint nRegisters = registers.GetSize();
  MIuint nRegisterIndex(vRegisterIndex);
  for (MIuint i = 0; i < nRegisters; i++) {
    lldb::SBValue value = registers.GetValueAtIndex(i);
    const MIuint nRegChildren = value.GetNumChildren();
    if (nRegisterIndex >= nRegChildren) {
      nRegisterIndex -= nRegChildren;
      continue;
    }

    lldb::SBValue value2 = value.GetChildAtIndex(nRegisterIndex);
    if (value2.IsValid()) {
      return value2;
    }
  }

  return lldb::SBValue();
}

//++
//------------------------------------------------------------------------------------
// Details: Adds the register value to the output list.
// Type:    Method.
// Args:    Value of the register, its index and output format.
// Return:  None
// Throws:  None.
//--
void CMICmdCmdDataListRegisterValues::AddToOutput(
    const MIuint vnIndex, const lldb::SBValue &vrValue,
    CMICmnLLDBDebugSessionInfoVarObj::varFormat_e veVarFormat) {
  const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%u", vnIndex));
  const CMICmnMIValueResult miValueResult("number", miValueConst);
  CMICmnMIValueTuple miValueTuple(miValueResult);
  const CMIUtilString strRegValue(
      CMICmnLLDBDebugSessionInfoVarObj::GetValueStringFormatted(vrValue,
                                                                veVarFormat));
  const CMICmnMIValueConst miValueConst2(strRegValue);
  const CMICmnMIValueResult miValueResult2("value", miValueConst2);
  miValueTuple.Add(miValueResult2);
  m_miValueList.Add(miValueTuple);
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataListRegisterChanged constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterChanged::CMICmdCmdDataListRegisterChanged() {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-list-changed-registers";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataListRegisterChanged::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataListRegisterChanged destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataListRegisterChanged::~CMICmdCmdDataListRegisterChanged() {}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataListRegisterChanged::Execute() {
  // Do nothing

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
bool CMICmdCmdDataListRegisterChanged::Acknowledge() {
  const CMICmnMIValueConst miValueConst(MIRSRC(IDS_WORD_NOT_IMPLEMENTED));
  const CMICmnMIValueResult miValueResult("msg", miValueConst);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataListRegisterChanged::CreateSelf() {
  return new CMICmdCmdDataListRegisterChanged();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataWriteMemoryBytes constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataWriteMemoryBytes::CMICmdCmdDataWriteMemoryBytes()
    : m_constStrArgAddr("address"), m_constStrArgContents("contents"),
      m_constStrArgCount("count") {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-write-memory-bytes";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataWriteMemoryBytes::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataWriteMemoryBytes destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataWriteMemoryBytes::~CMICmdCmdDataWriteMemoryBytes() {}

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
bool CMICmdCmdDataWriteMemoryBytes::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgAddr, true, true, false, true));
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgContents, true, true, true, true));
  m_setCmdArgs.Add(
      new CMICmdArgValString(m_constStrArgCount, false, true, false, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataWriteMemoryBytes::Execute() {
  // Do nothing - not reproduceable (yet) in Eclipse
  // CMICMDBASE_GETOPTION( pArgOffset, OptionShort, m_constStrArgOffset );
  // CMICMDBASE_GETOPTION( pArgAddr, String, m_constStrArgAddr );
  // CMICMDBASE_GETOPTION( pArgNumber, String, m_constStrArgNumber );
  // CMICMDBASE_GETOPTION( pArgContents, String, m_constStrArgContents );
  //
  // Numbers extracts as string types as they could be hex numbers
  // '&' is not recognised and so has to be removed

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
bool CMICmdCmdDataWriteMemoryBytes::Acknowledge() {
  const CMICmnMIValueConst miValueConst(MIRSRC(IDS_WORD_NOT_IMPLEMENTED));
  const CMICmnMIValueResult miValueResult("msg", miValueConst);
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Error,
      miValueResult);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataWriteMemoryBytes::CreateSelf() {
  return new CMICmdCmdDataWriteMemoryBytes();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataWriteMemory constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataWriteMemory::CMICmdCmdDataWriteMemory()
    : m_constStrArgOffset("o"), m_constStrArgAddr("address"),
      m_constStrArgD("d"), m_constStrArgNumber("a number"),
      m_constStrArgContents("contents"), m_nAddr(0), m_nCount(0),
      m_pBufferMemory(nullptr) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-write-memory";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataWriteMemory::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataWriteMemory destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataWriteMemory::~CMICmdCmdDataWriteMemory() {
  if (m_pBufferMemory != nullptr) {
    delete[] m_pBufferMemory;
    m_pBufferMemory = nullptr;
  }
}

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
bool CMICmdCmdDataWriteMemory::ParseArgs() {
  m_setCmdArgs.Add(
      new CMICmdArgValOptionShort(m_constStrArgOffset, false, true,
                                  CMICmdArgValListBase::eArgValType_Number, 1));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgAddr, true, true));
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgD, true, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgNumber, true, true));
  m_setCmdArgs.Add(new CMICmdArgValNumber(m_constStrArgContents, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataWriteMemory::Execute() {
  CMICMDBASE_GETOPTION(pArgOffset, OptionShort, m_constStrArgOffset);
  CMICMDBASE_GETOPTION(pArgAddr, Number, m_constStrArgAddr);
  CMICMDBASE_GETOPTION(pArgNumber, Number, m_constStrArgNumber);
  CMICMDBASE_GETOPTION(pArgContents, Number, m_constStrArgContents);

  MIuint nAddrOffset = 0;
  if (pArgOffset->GetFound() &&
      !pArgOffset->GetExpectedOption<CMICmdArgValNumber, MIuint>(nAddrOffset)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ARGS_ERR_VALIDATION_INVALID),
                                   m_cmdData.strMiCmd.c_str(),
                                   m_constStrArgAddr.c_str()));
    return MIstatus::failure;
  }
  m_nAddr = pArgAddr->GetValue();
  m_nCount = pArgNumber->GetValue();
  const MIuint64 nValue = pArgContents->GetValue();

  m_pBufferMemory = new unsigned char[m_nCount];
  if (m_pBufferMemory == nullptr) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_MEMORY_ALLOC_FAILURE),
                                   m_cmdData.strMiCmd.c_str(), m_nCount));
    return MIstatus::failure;
  }
  *m_pBufferMemory = static_cast<char>(nValue);

  CMICmnLLDBDebugSessionInfo &rSessionInfo(
      CMICmnLLDBDebugSessionInfo::Instance());
  lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
  lldb::SBError error;
  lldb::addr_t addr = static_cast<lldb::addr_t>(m_nAddr + nAddrOffset);
  const size_t nBytesWritten = sbProcess.WriteMemory(
      addr, (const void *)m_pBufferMemory, (size_t)m_nCount, error);
  if (nBytesWritten != static_cast<size_t>(m_nCount)) {
    SetError(
        CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_LLDB_ERR_NOT_WRITE_WHOLEBLK),
                              m_cmdData.strMiCmd.c_str(), m_nCount, addr));
    return MIstatus::failure;
  }
  if (error.Fail()) {
    lldb::SBStream err;
    const bool bOk = error.GetDescription(err);
    MIunused(bOk);
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_LLDB_ERR_WRITE_MEM_BYTES),
                                   m_cmdData.strMiCmd.c_str(), m_nCount, addr,
                                   err.GetData()));
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
bool CMICmdCmdDataWriteMemory::Acknowledge() {
  const CMICmnMIResultRecord miRecordResult(
      m_cmdData.strMiCmdToken, CMICmnMIResultRecord::eResultClass_Done);
  m_miResultRecord = miRecordResult;

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
CMICmdBase *CMICmdCmdDataWriteMemory::CreateSelf() {
  return new CMICmdCmdDataWriteMemory();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataInfoLine constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataInfoLine::CMICmdCmdDataInfoLine()
    : m_constStrArgLocation("location"),
      m_resultRecord(m_cmdData.strMiCmdToken,
                     CMICmnMIResultRecord::eResultClass_Done) {
  // Command factory matches this name with that received from the stdin stream
  m_strMiCmd = "data-info-line";

  // Required by the CMICmdFactory when registering *this command
  m_pSelfCreatorFn = &CMICmdCmdDataInfoLine::CreateSelf;
}

//++
//------------------------------------------------------------------------------------
// Details: CMICmdCmdDataInfoLine destructor.
// Type:    Overrideable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmdCmdDataInfoLine::~CMICmdCmdDataInfoLine() = default;

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
bool CMICmdCmdDataInfoLine::ParseArgs() {
  m_setCmdArgs.Add(new CMICmdArgValString(m_constStrArgLocation, true, true));
  return ParseValidateCmdOptions();
}

//++
//------------------------------------------------------------------------------------
// Details: The invoker requires this function. The command does work in this
// function.
//          The command is likely to communicate with the LLDB SBDebugger in
//          here.
// Type:    Overridden.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmdCmdDataInfoLine::Execute() {
  CMICMDBASE_GETOPTION(pArgLocation, String, m_constStrArgLocation);

  lldb::SBLineEntry line;
  bool found_line = false;
  const CMIUtilString &strLocation(pArgLocation->GetValue());
  lldb::SBTarget target = CMICmnLLDBDebugSessionInfo::Instance().GetTarget();

  if (strLocation.at(0) == '*') {
    // Parse argument:
    // *0x12345
    // ^^^^^^^^^ -- address
    lldb::addr_t address = 0x0;
    if (llvm::StringRef(strLocation.substr(1)).getAsInteger(0, address)) {
      SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_SOME_ERROR),
                                     m_cmdData.strMiCmd.c_str(),
                                     "Failed to parse address."));
      return MIstatus::failure;
    }
    line = target.ResolveFileAddress(address).GetLineEntry();
    // Check that found line is valid.
    if (line.GetLine())
      found_line = true;
  } else {
    const size_t nLineStartPos = strLocation.rfind(':');
    if ((nLineStartPos == std::string::npos) || (nLineStartPos == 0) ||
        (nLineStartPos == strLocation.length() - 1)) {
      SetError(CMIUtilString::Format(
          MIRSRC(IDS_CMD_ERR_INVALID_LOCATION_FORMAT),
          m_cmdData.strMiCmd.c_str(), strLocation.c_str()));
      return MIstatus::failure;
    }
    // Parse argument:
    // hello.cpp:5
    // ^^^^^^^^^ -- file
    //           ^ -- line
    const CMIUtilString &strFile(strLocation.substr(0, nLineStartPos));
    uint32_t numLine = 0;
    llvm::StringRef(strLocation.substr(nLineStartPos + 1))
        .getAsInteger(0, numLine);
    lldb::SBSymbolContextList sc_cu_list =
        target.FindCompileUnits(lldb::SBFileSpec(strFile.c_str(), false));
    for (uint32_t i = 0, e = sc_cu_list.GetSize(); i < e; ++i) {
      const lldb::SBCompileUnit &cu =
        sc_cu_list.GetContextAtIndex(i).GetCompileUnit();
      // Break if we have already found requested line.
      if (found_line)
        break;
      for (uint32_t j = 0, e = cu.GetNumLineEntries(); j < e; ++j) {
        const lldb::SBLineEntry &curLine = cu.GetLineEntryAtIndex(j);
        if (curLine.GetLine() == numLine) {
          line = curLine;
          found_line = true;
          break;
        }
      }
    }
  }
  if (!found_line) {
    SetError(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_SOME_ERROR), m_cmdData.strMiCmd.c_str(),
        "The LineEntry is absent or has an unknown format."));
    return MIstatus::failure;
  }
  // Start address.
  m_resultRecord.Add(CMICmnMIValueResult(
      "start", CMICmnMIValueConst(IntToHexAddrStr(
               line.GetStartAddress().GetFileAddress()))));
  // End address.
  m_resultRecord.Add(CMICmnMIValueResult(
      "end", CMICmnMIValueConst(IntToHexAddrStr(
             line.GetEndAddress().GetFileAddress()))));
  // File.
  std::unique_ptr<char[]> upPath(new char[PATH_MAX]);
  line.GetFileSpec().GetPath(upPath.get(), PATH_MAX);
  m_resultRecord.Add(CMICmnMIValueResult(
      "file", CMICmnMIValueConst(CMIUtilString(upPath.get()))));
  // Line.
  m_resultRecord.Add(CMICmnMIValueResult(
      "line", CMICmnMIValueConst(std::to_string(line.GetLine()))));
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
bool CMICmdCmdDataInfoLine::Acknowledge() {
  m_miResultRecord = m_resultRecord;
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
CMICmdBase *CMICmdCmdDataInfoLine::CreateSelf() {
  return new CMICmdCmdDataInfoLine();
}
