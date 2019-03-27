//===-- MICmnLLDBDebugSessionInfo.cpp ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#include "lldb/API/SBThread.h"
#include <inttypes.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif              // _WIN32
#include "lldb/API/SBBreakpointLocation.h"

// In-house headers:
#include "MICmdData.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnLLDBUtilSBValue.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"
#include "MICmnResources.h"
#include "Platform.h"

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfo constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfo::CMICmnLLDBDebugSessionInfo()
    : m_nBrkPointCntMax(INT32_MAX),
      m_currentSelectedThread(LLDB_INVALID_THREAD_ID),
      m_constStrSharedDataKeyWkDir("Working Directory"),
      m_constStrSharedDataSolibPath("Solib Path"),
      m_constStrPrintCharArrayAsString("Print CharArrayAsString"),
      m_constStrPrintExpandAggregates("Print ExpandAggregates"),
      m_constStrPrintAggregateFieldNames("Print AggregateFieldNames") {}

//++
//------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfo destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfo::~CMICmnLLDBDebugSessionInfo() { Shutdown(); }

//++
//------------------------------------------------------------------------------------
// Details: Initialize resources for *this object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::Initialize() {
  m_clientUsageRefCnt++;

  if (m_bInitialized)
    return MIstatus::success;

  m_currentSelectedThread = LLDB_INVALID_THREAD_ID;
  CMICmnLLDBDebugSessionInfoVarObj::VarObjIdResetToZero();

  m_bInitialized = MIstatus::success;

  return m_bInitialized;
}

//++
//------------------------------------------------------------------------------------
// Details: Release resources for *this object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::Shutdown() {
  if (--m_clientUsageRefCnt > 0)
    return MIstatus::success;

  if (!m_bInitialized)
    return MIstatus::success;

  // Tidy up
  SharedDataDestroy();

  m_vecActiveThreadId.clear();
  CMICmnLLDBDebugSessionInfoVarObj::VarObjClear();

  m_bInitialized = false;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Command instances can create and share data between other instances
// of commands.
//          Data can also be assigned by a command and retrieved by LLDB event
//          handler.
//          This function takes down those resources build up over the use of
//          the commands.
//          This function should be called when the creation and running of
//          command has
//          stopped i.e. application shutdown.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfo::SharedDataDestroy() {
  m_mapIdToSessionData.Clear();
  m_vecVarObj.clear();
  m_mapBrkPtIdToBrkPtInfo.clear();
}

//++
//------------------------------------------------------------------------------------
// Details: Record information about a LLDB break point so that is can be
// recalled in other
//          commands or LLDB event handling functions.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
//          vrBrkPtInfo     - (R) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::RecordBrkPtInfo(
    const MIuint vnBrkPtId, const SBrkPtInfo &vrBrkPtInfo) {
  MapPairBrkPtIdToBrkPtInfo_t pr(vnBrkPtId, vrBrkPtInfo);
  m_mapBrkPtIdToBrkPtInfo.insert(pr);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve information about a LLDB break point previous recorded
// either by
//          commands or LLDB event handling functions.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
//          vrwBrkPtInfo    - (W) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::RecordBrkPtInfoGet(
    const MIuint vnBrkPtId, SBrkPtInfo &vrwBrkPtInfo) const {
  const MapBrkPtIdToBrkPtInfo_t::const_iterator it =
      m_mapBrkPtIdToBrkPtInfo.find(vnBrkPtId);
  if (it != m_mapBrkPtIdToBrkPtInfo.end()) {
    vrwBrkPtInfo = (*it).second;
    return MIstatus::success;
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Delete information about a specific LLDB break point object. This
// function
//          should be called when a LLDB break point is deleted.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::RecordBrkPtInfoDelete(const MIuint vnBrkPtId) {
  const MapBrkPtIdToBrkPtInfo_t::const_iterator it =
      m_mapBrkPtIdToBrkPtInfo.find(vnBrkPtId);
  if (it != m_mapBrkPtIdToBrkPtInfo.end()) {
    m_mapBrkPtIdToBrkPtInfo.erase(it);
    return MIstatus::success;
  }

  return MIstatus::failure;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the specified thread's frame information.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vThreadIdx      - (R) Thread index.
//          vwrThreadFrames - (W) Frame data.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::GetThreadFrames(
    const SMICmdData &vCmdData, const MIuint vThreadIdx,
    const FrameInfoFormat_e veFrameInfoFormat, CMIUtilString &vwrThreadFrames) {
  lldb::SBThread thread = GetProcess().GetThreadByIndexID(vThreadIdx);
  const uint32_t nFrames = thread.GetNumFrames();
  if (nFrames == 0) {
    // MI print "frame={}"
    CMICmnMIValueTuple miValueTuple;
    CMICmnMIValueResult miValueResult("frame", miValueTuple);
    vwrThreadFrames = miValueResult.GetString();
    return MIstatus::success;
  }

  // MI print
  // "frame={level=\"%d\",addr=\"0x%016" PRIx64
  // "\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},frame={level=\"%d\",addr=\"0x%016"
  // PRIx64 "\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},
  // ..."
  CMIUtilString strListCommaSeparated;
  for (MIuint nLevel = 0; nLevel < nFrames; nLevel++) {
    CMICmnMIValueTuple miValueTuple;
    if (!MIResponseFormFrameInfo(thread, nLevel, veFrameInfoFormat,
                                 miValueTuple))
      return MIstatus::failure;

    const CMICmnMIValueResult miValueResult2("frame", miValueTuple);
    if (nLevel != 0)
      strListCommaSeparated += ",";
    strListCommaSeparated += miValueResult2.GetString();
  }

  vwrThreadFrames = strListCommaSeparated;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Return the resolved file's path for the given file.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vPath           - (R) Original path.
//          vwrResolvedPath - (W) Resolved path.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::ResolvePath(const SMICmdData &vCmdData,
                                             const CMIUtilString &vPath,
                                             CMIUtilString &vwrResolvedPath) {
  // ToDo: Verify this code as it does not work as vPath is always empty

  CMIUtilString strResolvedPath;
  if (!SharedDataRetrieve<CMIUtilString>(m_constStrSharedDataKeyWkDir,
                                         strResolvedPath)) {
    vwrResolvedPath = "";
    SetErrorDescription(CMIUtilString::Format(
        MIRSRC(IDS_CMD_ERR_SHARED_DATA_NOT_FOUND), vCmdData.strMiCmd.c_str(),
        m_constStrSharedDataKeyWkDir.c_str()));
    return MIstatus::failure;
  }

  vwrResolvedPath = vPath;

  return ResolvePath(strResolvedPath, vwrResolvedPath);
}

//++
//------------------------------------------------------------------------------------
// Details: Return the resolved file's path for the given file.
// Type:    Method.
// Args:    vstrUnknown     - (R)   String assigned to path when resolved path
// is empty.
//          vwrResolvedPath - (RW)  The original path overwritten with resolved
//          path.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::ResolvePath(const CMIUtilString &vstrUnknown,
                                             CMIUtilString &vwrResolvedPath) {
  if (vwrResolvedPath.size() < 1) {
    vwrResolvedPath = vstrUnknown;
    return MIstatus::success;
  }

  bool bOk = MIstatus::success;

  CMIUtilString::VecString_t vecPathFolders;
  const MIuint nSplits = vwrResolvedPath.Split("/", vecPathFolders);
  MIunused(nSplits);
  MIuint nFoldersBack = 1; // 1 is just the file (last element of vector)
  while (bOk && (vecPathFolders.size() >= nFoldersBack)) {
    CMIUtilString strTestPath;
    MIuint nFoldersToAdd = nFoldersBack;
    while (nFoldersToAdd > 0) {
      strTestPath += "/";
      strTestPath += vecPathFolders[vecPathFolders.size() - nFoldersToAdd];
      nFoldersToAdd--;
    }
    bool bYesAccessible = false;
    bOk = AccessPath(strTestPath, bYesAccessible);
    if (bYesAccessible) {
      vwrResolvedPath = strTestPath;
      return MIstatus::success;
    } else
      nFoldersBack++;
  }

  // No files exist in the union of working directory and debuginfo path
  // Simply use the debuginfo path and let the IDE handle it.

  return bOk;
}

//++
//------------------------------------------------------------------------------------
// Details: Determine the given file path exists or not.
// Type:    Method.
// Args:    vPath               - (R) File name path.
//          vwbYesAccessible    - (W) True - file exists, false = does not
//          exist.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::AccessPath(const CMIUtilString &vPath,
                                            bool &vwbYesAccessible) {
#ifdef _WIN32
  vwbYesAccessible = (::_access(vPath.c_str(), 0) == 0);
#else
  vwbYesAccessible = (::access(vPath.c_str(), 0) == 0);
#endif // _WIN32

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to
// the
//          tuple type object past in.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::MIResponseFormThreadInfo(
    const SMICmdData &vCmdData, const lldb::SBThread &vrThread,
    const ThreadInfoFormat_e veThreadInfoFormat,
    CMICmnMIValueTuple &vwrMIValueTuple) {
  lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

  const bool bSuspended = rThread.IsSuspended();
  const lldb::StopReason eReason = rThread.GetStopReason();
  const bool bValidReason = !((eReason == lldb::eStopReasonNone) ||
                              (eReason == lldb::eStopReasonInvalid));
  const CMIUtilString strState((bSuspended || bValidReason) ? "stopped"
                                                            : "running");

  // Add "id"
  const CMIUtilString strId(CMIUtilString::Format("%d", rThread.GetIndexID()));
  const CMICmnMIValueConst miValueConst1(strId);
  const CMICmnMIValueResult miValueResult1("id", miValueConst1);
  vwrMIValueTuple.Add(miValueResult1);

  // Add "target-id"
  const char *pThreadName = rThread.GetName();
  const MIuint len =
      (pThreadName != nullptr) ? CMIUtilString(pThreadName).length() : 0;
  const bool bHaveName = ((pThreadName != nullptr) && (len > 0) && (len < 32) &&
                          CMIUtilString::IsAllValidAlphaAndNumeric(
                              pThreadName)); // 32 is arbitrary number
  const char *pThrdFmt = bHaveName ? "%s" : "Thread %d";
  CMIUtilString strThread;
  if (bHaveName)
    strThread = CMIUtilString::Format(pThrdFmt, pThreadName);
  else
    strThread = CMIUtilString::Format(pThrdFmt, rThread.GetIndexID());
  const CMICmnMIValueConst miValueConst2(strThread);
  const CMICmnMIValueResult miValueResult2("target-id", miValueConst2);
  vwrMIValueTuple.Add(miValueResult2);

  // Add "frame"
  if (veThreadInfoFormat != eThreadInfoFormat_NoFrames) {
    CMIUtilString strFrames;
    if (!GetThreadFrames(vCmdData, rThread.GetIndexID(),
                         eFrameInfoFormat_AllArgumentsInSimpleForm, strFrames))
      return MIstatus::failure;

    const CMICmnMIValueConst miValueConst3(strFrames, true);
    vwrMIValueTuple.Add(miValueConst3, false);
  }

  // Add "state"
  const CMICmnMIValueConst miValueConst4(strState);
  const CMICmnMIValueResult miValueResult4("state", miValueConst4);
  vwrMIValueTuple.Add(miValueResult4);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to
// the
//          tuple type object past in.
// Type:    Method.
// Args:    vrFrame         - (R)   LLDB thread object.
//          vMaskVarTypes   - (R)   Construed according to VariableType_e.
//          veVarInfoFormat - (R)   The type of variable info that should be
//          shown.
//          vwrMIValueList  - (W)   MI value list object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::MIResponseFormVariableInfo(
    const lldb::SBFrame &vrFrame, const MIuint vMaskVarTypes,
    const VariableInfoFormat_e veVarInfoFormat,
    CMICmnMIValueList &vwrMiValueList, const MIuint vnMaxDepth, /* = 10 */
    const bool vbMarkArgs /* = false*/) {
  bool bOk = MIstatus::success;
  lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

  const bool bArg = (vMaskVarTypes & eVariableType_Arguments);
  const bool bLocals = (vMaskVarTypes & eVariableType_Locals);
  const bool bStatics = (vMaskVarTypes & eVariableType_Statics);
  const bool bInScopeOnly = (vMaskVarTypes & eVariableType_InScope);

  // Handle arguments first
  lldb::SBValueList listArg = rFrame.GetVariables(bArg, false, false, false);
  bOk = bOk && MIResponseForVariableInfoInternal(veVarInfoFormat,
                                                 vwrMiValueList, listArg,
                                                 vnMaxDepth, true, vbMarkArgs);

  // Handle remaining variables
  lldb::SBValueList listVars =
      rFrame.GetVariables(false, bLocals, bStatics, bInScopeOnly);
  bOk = bOk && MIResponseForVariableInfoInternal(veVarInfoFormat,
                                                 vwrMiValueList, listVars,
                                                 vnMaxDepth, false, vbMarkArgs);

  return bOk;
}

bool CMICmnLLDBDebugSessionInfo::MIResponseForVariableInfoInternal(
    const VariableInfoFormat_e veVarInfoFormat,
    CMICmnMIValueList &vwrMiValueList, const lldb::SBValueList &vwrSBValueList,
    const MIuint vnMaxDepth, const bool vbIsArgs, const bool vbMarkArgs) {
  const MIuint nArgs = vwrSBValueList.GetSize();
  for (MIuint i = 0; i < nArgs; i++) {
    CMICmnMIValueTuple miValueTuple;
    lldb::SBValue value = vwrSBValueList.GetValueAtIndex(i);
    // If one stops inside try block with, which catch clause type is unnamed
    // (e.g std::exception&) then value name will be nullptr as well as value
    // pointer
    const char *name = value.GetName();
    if (name == nullptr)
      continue;
    const CMICmnMIValueConst miValueConst(name);
    const CMICmnMIValueResult miValueResultName("name", miValueConst);
    if (vbMarkArgs && vbIsArgs) {
      const CMICmnMIValueConst miValueConstArg("1");
      const CMICmnMIValueResult miValueResultArg("arg", miValueConstArg);
      miValueTuple.Add(miValueResultArg);
    }
    if (veVarInfoFormat != eVariableInfoFormat_NoValues) {
      miValueTuple.Add(miValueResultName); // name
      if (veVarInfoFormat == eVariableInfoFormat_SimpleValues) {
        const CMICmnMIValueConst miValueConst3(value.GetTypeName());
        const CMICmnMIValueResult miValueResult3("type", miValueConst3);
        miValueTuple.Add(miValueResult3);
      }
      const MIuint nChildren = value.GetNumChildren();
      const bool bIsPointerType = value.GetType().IsPointerType();
      if (nChildren == 0 ||                                 // no children
          (bIsPointerType && nChildren == 1) ||             // pointers
          veVarInfoFormat == eVariableInfoFormat_AllValues) // show all values
      {
        CMIUtilString strValue;
        if (GetVariableInfo(value, vnMaxDepth == 0, strValue)) {
          const CMICmnMIValueConst miValueConst2(
              strValue.Escape().AddSlashes());
          const CMICmnMIValueResult miValueResult2("value", miValueConst2);
          miValueTuple.Add(miValueResult2);
        }
      }
      vwrMiValueList.Add(miValueTuple);
      continue;
    }

    if (vbMarkArgs) {
      // If we are printing names only with vbMarkArgs, we still need to add the
      // name to the value tuple
      miValueTuple.Add(miValueResultName); // name
      vwrMiValueList.Add(miValueTuple);
    } else {
      // If we are printing name only then no need to put it in the tuple.
      vwrMiValueList.Add(miValueResultName);
    }
  }
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Extract the value's name and value or recurse into child value
// object.
// Type:    Method.
// Args:    vrValue         - (R)  LLDB value object.
//          vbInSimpleForm  - (R)  True = Get variable info in simple form (i.e.
//          don't expand aggregates).
//                          -      False = Get variable info (and expand
//                          aggregates if any).
//          vwrStrValue  t  - (W)  The string representation of this value.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::GetVariableInfo(const lldb::SBValue &vrValue,
                                                 const bool vbInSimpleForm,
                                                 CMIUtilString &vwrStrValue) {
  const CMICmnLLDBUtilSBValue utilValue(vrValue, true, false);
  const bool bExpandAggregates = !vbInSimpleForm;
  vwrStrValue = utilValue.GetValue(bExpandAggregates);
  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to
// the
//          tuple type object past in.
// Type:    Method.
// Args:    vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
//          vArgInfo        - (R) Args information in MI response form.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::MIResponseFormFrameInfo(
    const lldb::SBThread &vrThread, const MIuint vnLevel,
    const FrameInfoFormat_e veFrameInfoFormat,
    CMICmnMIValueTuple &vwrMiValueTuple) {
  lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

  lldb::SBFrame frame = rThread.GetFrameAtIndex(vnLevel);
  lldb::addr_t pc = 0;
  CMIUtilString fnName;
  CMIUtilString fileName;
  CMIUtilString path;
  MIuint nLine = 0;
  if (!GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
    return MIstatus::failure;

  // MI print "{level=\"0\",addr=\"0x%016" PRIx64
  // "\",func=\"%s\",file=\"%s\",fullname=\"%s\",line=\"%d\"}"
  const CMIUtilString strLevel(CMIUtilString::Format("%d", vnLevel));
  const CMICmnMIValueConst miValueConst(strLevel);
  const CMICmnMIValueResult miValueResult("level", miValueConst);
  vwrMiValueTuple.Add(miValueResult);
  const CMIUtilString strAddr(CMIUtilString::Format("0x%016" PRIx64, pc));
  const CMICmnMIValueConst miValueConst2(strAddr);
  const CMICmnMIValueResult miValueResult2("addr", miValueConst2);
  vwrMiValueTuple.Add(miValueResult2);
  const CMICmnMIValueConst miValueConst3(fnName);
  const CMICmnMIValueResult miValueResult3("func", miValueConst3);
  vwrMiValueTuple.Add(miValueResult3);
  if (veFrameInfoFormat != eFrameInfoFormat_NoArguments) {
    CMICmnMIValueList miValueList(true);
    const MIuint maskVarTypes = eVariableType_Arguments;
    if (veFrameInfoFormat == eFrameInfoFormat_AllArgumentsInSimpleForm) {
      if (!MIResponseFormVariableInfo(frame, maskVarTypes,
                                      eVariableInfoFormat_AllValues,
                                      miValueList, 0))
        return MIstatus::failure;
    } else if (!MIResponseFormVariableInfo(frame, maskVarTypes,
                                           eVariableInfoFormat_AllValues,
                                           miValueList))
      return MIstatus::failure;

    const CMICmnMIValueResult miValueResult4("args", miValueList);
    vwrMiValueTuple.Add(miValueResult4);
  }
  const CMICmnMIValueConst miValueConst5(fileName);
  const CMICmnMIValueResult miValueResult5("file", miValueConst5);
  vwrMiValueTuple.Add(miValueResult5);
  const CMICmnMIValueConst miValueConst6(path);
  const CMICmnMIValueResult miValueResult6("fullname", miValueConst6);
  vwrMiValueTuple.Add(miValueResult6);
  const CMIUtilString strLine(CMIUtilString::Format("%d", nLine));
  const CMICmnMIValueConst miValueConst7(strLine);
  const CMICmnMIValueResult miValueResult7("line", miValueConst7);
  vwrMiValueTuple.Add(miValueResult7);

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the frame information from LLDB frame object.
// Type:    Method.
// Args:    vrFrame         - (R) LLDB thread object.
//          vPc             - (W) Address number.
//          vFnName         - (W) Function name.
//          vFileName       - (W) File name text.
//          vPath           - (W) Full file name and path text.
//          vnLine          - (W) File line number.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::GetFrameInfo(
    const lldb::SBFrame &vrFrame, lldb::addr_t &vwPc, CMIUtilString &vwFnName,
    CMIUtilString &vwFileName, CMIUtilString &vwPath, MIuint &vwnLine) {
  lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

  static char pBuffer[PATH_MAX];
  const MIuint nBytes =
      rFrame.GetLineEntry().GetFileSpec().GetPath(&pBuffer[0], sizeof(pBuffer));
  MIunused(nBytes);
  CMIUtilString strResolvedPath(&pBuffer[0]);
  const char *pUnkwn = "??";
  if (!ResolvePath(pUnkwn, strResolvedPath))
    return MIstatus::failure;
  vwPath = strResolvedPath;

  vwPc = rFrame.GetPC();

  const char *pFnName = rFrame.GetFunctionName();
  vwFnName = (pFnName != nullptr) ? pFnName : pUnkwn;

  const char *pFileName = rFrame.GetLineEntry().GetFileSpec().GetFilename();
  vwFileName = (pFileName != nullptr) ? pFileName : pUnkwn;

  vwnLine = rFrame.GetLineEntry().GetLine();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to
// the
//          tuple type object past in.
// Type:    Method.
// Args:    vrBrkPtInfo     - (R) Break point information object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  None.
// Throws:  None.
//--
void CMICmnLLDBDebugSessionInfo::MIResponseFormBrkPtFrameInfo(
    const SBrkPtInfo &vrBrkPtInfo, CMICmnMIValueTuple &vwrMiValueTuple) {
  const CMIUtilString strAddr(
      CMIUtilString::Format("0x%016" PRIx64, vrBrkPtInfo.m_pc));
  const CMICmnMIValueConst miValueConst2(strAddr);
  const CMICmnMIValueResult miValueResult2("addr", miValueConst2);
  vwrMiValueTuple.Add(miValueResult2);
  const CMICmnMIValueConst miValueConst3(vrBrkPtInfo.m_fnName);
  const CMICmnMIValueResult miValueResult3("func", miValueConst3);
  vwrMiValueTuple.Add(miValueResult3);
  const CMICmnMIValueConst miValueConst5(vrBrkPtInfo.m_fileName);
  const CMICmnMIValueResult miValueResult5("file", miValueConst5);
  vwrMiValueTuple.Add(miValueResult5);
  const CMIUtilString strN5 = CMIUtilString::Format(
      "%s/%s", vrBrkPtInfo.m_path.c_str(), vrBrkPtInfo.m_fileName.c_str());
  const CMICmnMIValueConst miValueConst6(strN5);
  const CMICmnMIValueResult miValueResult6("fullname", miValueConst6);
  vwrMiValueTuple.Add(miValueResult6);
  const CMIUtilString strLine(CMIUtilString::Format("%d", vrBrkPtInfo.m_nLine));
  const CMICmnMIValueConst miValueConst7(strLine);
  const CMICmnMIValueResult miValueResult7("line", miValueConst7);
  vwrMiValueTuple.Add(miValueResult7);
}

//++
//------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to
// the
//          tuple type object past in.
// Type:    Method.
// Args:    vrBrkPtInfo     - (R) Break point information object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::MIResponseFormBrkPtInfo(
    const SBrkPtInfo &vrBrkPtInfo, CMICmnMIValueTuple &vwrMiValueTuple) {
  // MI print
  // "=breakpoint-modified,bkpt={number=\"%d\",type=\"breakpoint\",disp=\"%s\",enabled=\"%c\",addr=\"0x%016"
  // PRIx64 "\",
  // func=\"%s\",file=\"%s\",fullname=\"%s/%s\",line=\"%d\",times=\"%d\",original-location=\"%s\"}"

  // "number="
  const CMICmnMIValueConst miValueConst(
      CMIUtilString::Format("%d", vrBrkPtInfo.m_id));
  const CMICmnMIValueResult miValueResult("number", miValueConst);
  CMICmnMIValueTuple miValueTuple(miValueResult);
  // "type="
  const CMICmnMIValueConst miValueConst2(vrBrkPtInfo.m_strType);
  const CMICmnMIValueResult miValueResult2("type", miValueConst2);
  miValueTuple.Add(miValueResult2);
  // "disp="
  const CMICmnMIValueConst miValueConst3(vrBrkPtInfo.m_bDisp ? "del" : "keep");
  const CMICmnMIValueResult miValueResult3("disp", miValueConst3);
  miValueTuple.Add(miValueResult3);
  // "enabled="
  const CMICmnMIValueConst miValueConst4(vrBrkPtInfo.m_bEnabled ? "y" : "n");
  const CMICmnMIValueResult miValueResult4("enabled", miValueConst4);
  miValueTuple.Add(miValueResult4);
  // "addr="
  // "func="
  // "file="
  // "fullname="
  // "line="
  MIResponseFormBrkPtFrameInfo(vrBrkPtInfo, miValueTuple);
  // "pending="
  if (vrBrkPtInfo.m_bPending) {
    const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strOrigLoc);
    const CMICmnMIValueList miValueList(miValueConst);
    const CMICmnMIValueResult miValueResult("pending", miValueList);
    miValueTuple.Add(miValueResult);
  }
  if (vrBrkPtInfo.m_bHaveArgOptionThreadGrp) {
    const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strOptThrdGrp);
    const CMICmnMIValueList miValueList(miValueConst);
    const CMICmnMIValueResult miValueResult("thread-groups", miValueList);
    miValueTuple.Add(miValueResult);
  }
  // "times="
  const CMICmnMIValueConst miValueConstB(
      CMIUtilString::Format("%d", vrBrkPtInfo.m_nTimes));
  const CMICmnMIValueResult miValueResultB("times", miValueConstB);
  miValueTuple.Add(miValueResultB);
  // "thread="
  if (vrBrkPtInfo.m_bBrkPtThreadId) {
    const CMICmnMIValueConst miValueConst(
        CMIUtilString::Format("%d", vrBrkPtInfo.m_nBrkPtThreadId));
    const CMICmnMIValueResult miValueResult("thread", miValueConst);
    miValueTuple.Add(miValueResult);
  }
  // "cond="
  if (vrBrkPtInfo.m_bCondition) {
    const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strCondition);
    const CMICmnMIValueResult miValueResult("cond", miValueConst);
    miValueTuple.Add(miValueResult);
  }
  // "ignore="
  if (vrBrkPtInfo.m_nIgnore != 0) {
    const CMICmnMIValueConst miValueConst(
        CMIUtilString::Format("%d", vrBrkPtInfo.m_nIgnore));
    const CMICmnMIValueResult miValueResult("ignore", miValueConst);
    miValueTuple.Add(miValueResult);
  }
  // "original-location="
  const CMICmnMIValueConst miValueConstC(vrBrkPtInfo.m_strOrigLoc);
  const CMICmnMIValueResult miValueResultC("original-location", miValueConstC);
  miValueTuple.Add(miValueResultC);

  vwrMiValueTuple = miValueTuple;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve breakpoint information and write into the given breakpoint
// information
//          object. Note not all possible information is retrieved and so the
//          information
//          object may need to be filled in with more information after calling
//          this
//          function. Mainly breakpoint location information of information that
//          is
//          unlikely to change.
// Type:    Method.
// Args:    vBrkPt      - (R) LLDB break point object.
//          vrBrkPtInfo - (W) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMICmnLLDBDebugSessionInfo::GetBrkPtInfo(const lldb::SBBreakpoint &vBrkPt,
                                              SBrkPtInfo &vrwBrkPtInfo) const {
  lldb::SBBreakpoint &rBrkPt = const_cast<lldb::SBBreakpoint &>(vBrkPt);
  lldb::SBBreakpointLocation brkPtLoc = rBrkPt.GetLocationAtIndex(0);
  lldb::SBAddress brkPtAddr = brkPtLoc.GetAddress();
  lldb::SBSymbolContext symbolCntxt =
      brkPtAddr.GetSymbolContext(lldb::eSymbolContextEverything);
  const char *pUnkwn = "??";
  lldb::SBModule rModule = symbolCntxt.GetModule();
  const char *pModule =
      rModule.IsValid() ? rModule.GetFileSpec().GetFilename() : pUnkwn;
  MIunused(pModule);
  const char *pFile = pUnkwn;
  const char *pFn = pUnkwn;
  const char *pFilePath = pUnkwn;
  size_t nLine = 0;
  lldb::addr_t nAddr = brkPtAddr.GetLoadAddress(GetTarget());
  if (nAddr == LLDB_INVALID_ADDRESS)
    nAddr = brkPtAddr.GetFileAddress();

  lldb::SBCompileUnit rCmplUnit = symbolCntxt.GetCompileUnit();
  if (rCmplUnit.IsValid()) {
    lldb::SBFileSpec rFileSpec = rCmplUnit.GetFileSpec();
    pFile = rFileSpec.GetFilename();
    pFilePath = rFileSpec.GetDirectory();
    lldb::SBFunction rFn = symbolCntxt.GetFunction();
    if (rFn.IsValid())
      pFn = rFn.GetName();
    lldb::SBLineEntry rLnEntry = symbolCntxt.GetLineEntry();
    if (rLnEntry.GetLine() > 0)
      nLine = rLnEntry.GetLine();
  }

  vrwBrkPtInfo.m_id = vBrkPt.GetID();
  vrwBrkPtInfo.m_strType = "breakpoint";
  vrwBrkPtInfo.m_pc = nAddr;
  vrwBrkPtInfo.m_fnName = pFn;
  vrwBrkPtInfo.m_fileName = pFile;
  vrwBrkPtInfo.m_path = pFilePath;
  vrwBrkPtInfo.m_nLine = nLine;
  vrwBrkPtInfo.m_nTimes = vBrkPt.GetHitCount();

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Get current debugger.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBDebugger   - current debugger.
// Throws:  None.
//--
lldb::SBDebugger &CMICmnLLDBDebugSessionInfo::GetDebugger() const {
  return CMICmnLLDBDebugger::Instance().GetTheDebugger();
}

//++
//------------------------------------------------------------------------------------
// Details: Get current listener.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBListener   - current listener.
// Throws:  None.
//--
lldb::SBListener &CMICmnLLDBDebugSessionInfo::GetListener() const {
  return CMICmnLLDBDebugger::Instance().GetTheListener();
}

//++
//------------------------------------------------------------------------------------
// Details: Get current target.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBTarget   - current target.
// Throws:  None.
//--
lldb::SBTarget CMICmnLLDBDebugSessionInfo::GetTarget() const {
  auto target = GetDebugger().GetSelectedTarget();
  if (target.IsValid())
    return target;
  return GetDebugger().GetDummyTarget();
}

//++
//------------------------------------------------------------------------------------
// Details: Get current process.
// Type:    Method.
// Args:    None.
// Return:  lldb::SBProcess   - current process.
// Throws:  None.
//--
lldb::SBProcess CMICmnLLDBDebugSessionInfo::GetProcess() const {
  return GetTarget().GetProcess();
}
