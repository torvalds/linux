//===-- MICmnLLDBDebugSessionInfo.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include <map>
#include <vector>

// In-house headers:
#include "MICmnBase.h"
#include "MICmnLLDBDebugSessionInfoVarObj.h"
#include "MICmnMIValueTuple.h"
#include "MIUtilMapIdToVariant.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilThreadBaseStd.h"

// Declarations:
class CMICmnLLDBDebugger;
struct SMICmdData;
class CMICmnMIValueTuple;
class CMICmnMIValueList;

//++
//============================================================================
// Details: MI debug session object that holds debugging information between
//          instances of MI commands executing their work and producing MI
//          result records. Information/data is set by one or many commands then
//          retrieved by the same or other subsequent commands.
//          It primarily holds LLDB type objects.
//          A singleton class.
//--
class CMICmnLLDBDebugSessionInfo
    : public CMICmnBase,
      public MI::ISingleton<CMICmnLLDBDebugSessionInfo> {
  friend class MI::ISingleton<CMICmnLLDBDebugSessionInfo>;

  // Structs:
public:
  //++
  //============================================================================
  // Details: Break point information object. Used to easily pass information
  // about
  //          a break around and record break point information to be recalled
  //          by
  //          other commands or LLDB event handling functions.
  //--
  struct SBrkPtInfo {
    SBrkPtInfo()
        : m_id(0), m_bDisp(false), m_bEnabled(false), m_pc(0), m_nLine(0),
          m_bHaveArgOptionThreadGrp(false), m_nTimes(0), m_bPending(false),
          m_nIgnore(0), m_bCondition(false), m_bBrkPtThreadId(false),
          m_nBrkPtThreadId(0) {}

    MIuint m_id;              // LLDB break point ID.
    CMIUtilString m_strType;  // Break point type.
    bool m_bDisp;             // True = "del", false = "keep".
    bool m_bEnabled;          // True = enabled, false = disabled break point.
    lldb::addr_t m_pc;        // Address number.
    CMIUtilString m_fnName;   // Function name.
    CMIUtilString m_fileName; // File name text.
    CMIUtilString m_path;     // Full file name and path text.
    MIuint m_nLine;           // File line number.
    bool m_bHaveArgOptionThreadGrp; // True = include MI field, false = do not
                                    // include "thread-groups".
    CMIUtilString m_strOptThrdGrp;  // Thread group number.
    MIuint m_nTimes;                // The count of the breakpoint existence.
    CMIUtilString m_strOrigLoc;     // The name of the break point.
    bool m_bPending;  // True = the breakpoint has not been established yet,
                      // false = location found
    MIuint m_nIgnore; // The number of time the breakpoint is run over before it
                      // is stopped on a hit
    bool m_bCondition; // True = break point is conditional, use condition
                       // expression, false = no condition
    CMIUtilString m_strCondition; // Break point condition expression
    bool m_bBrkPtThreadId; // True = break point is specified to work with a
                           // specific thread, false = no specified thread given
    MIuint
        m_nBrkPtThreadId; // Restrict the breakpoint to the specified thread-id
  };

  // Enumerations:
public:
  //++ ===================================================================
  // Details: The type of variable used by MIResponseFormVariableInfo family
  // functions.
  //--
  enum VariableType_e {
    eVariableType_InScope = (1u << 0),  // In scope only.
    eVariableType_Statics = (1u << 1),  // Statics.
    eVariableType_Locals = (1u << 2),   // Locals.
    eVariableType_Arguments = (1u << 3) // Arguments.
  };

  //++ ===================================================================
  // Details: Determine the information that should be shown by using
  // MIResponseFormVariableInfo family functions.
  //--
  enum VariableInfoFormat_e {
    eVariableInfoFormat_NoValues = 0,
    eVariableInfoFormat_AllValues = 1,
    eVariableInfoFormat_SimpleValues = 2
  };

  //++ ===================================================================
  // Details: Determine the information that should be shown by using
  // MIResponseFormThreadInfo family functions.
  //--
  enum ThreadInfoFormat_e {
    eThreadInfoFormat_NoFrames,
    eThreadInfoFormat_AllFrames
  };

  //++ ===================================================================
  // Details: Determine the information that should be shown by using
  // MIResponseFormFrameInfo family functions.
  //--
  enum FrameInfoFormat_e {
    eFrameInfoFormat_NoArguments,
    eFrameInfoFormat_AllArguments,
    eFrameInfoFormat_AllArgumentsInSimpleForm
  };

  // Typedefs:
public:
  typedef std::vector<uint32_t> VecActiveThreadId_t;

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;

  // Variant type data which can be assigned and retrieved across all command
  // instances
  template <typename T>
  bool SharedDataAdd(const CMIUtilString &vKey, const T &vData);
  template <typename T>
  bool SharedDataRetrieve(const CMIUtilString &vKey, T &vwData);
  void SharedDataDestroy();

  //  Common command required functionality
  bool AccessPath(const CMIUtilString &vPath, bool &vwbYesAccessible);
  bool ResolvePath(const SMICmdData &vCmdData, const CMIUtilString &vPath,
                   CMIUtilString &vwrResolvedPath);
  bool ResolvePath(const CMIUtilString &vstrUnknown,
                   CMIUtilString &vwrResolvedPath);
  bool MIResponseFormFrameInfo(const lldb::SBThread &vrThread,
                               const MIuint vnLevel,
                               const FrameInfoFormat_e veFrameInfoFormat,
                               CMICmnMIValueTuple &vwrMiValueTuple);
  bool MIResponseFormThreadInfo(const SMICmdData &vCmdData,
                                const lldb::SBThread &vrThread,
                                const ThreadInfoFormat_e veThreadInfoFormat,
                                CMICmnMIValueTuple &vwrMIValueTuple);
  bool MIResponseFormVariableInfo(const lldb::SBFrame &vrFrame,
                                  const MIuint vMaskVarTypes,
                                  const VariableInfoFormat_e veVarInfoFormat,
                                  CMICmnMIValueList &vwrMiValueList,
                                  const MIuint vnMaxDepth = 10,
                                  const bool vbMarkArgs = false);
  void MIResponseFormBrkPtFrameInfo(const SBrkPtInfo &vrBrkPtInfo,
                                    CMICmnMIValueTuple &vwrMiValueTuple);
  bool MIResponseFormBrkPtInfo(const SBrkPtInfo &vrBrkPtInfo,
                               CMICmnMIValueTuple &vwrMiValueTuple);
  bool GetBrkPtInfo(const lldb::SBBreakpoint &vBrkPt,
                    SBrkPtInfo &vrwBrkPtInfo) const;
  bool RecordBrkPtInfo(const MIuint vnBrkPtId, const SBrkPtInfo &vrBrkPtInfo);
  bool RecordBrkPtInfoGet(const MIuint vnBrkPtId,
                          SBrkPtInfo &vrwBrkPtInfo) const;
  bool RecordBrkPtInfoDelete(const MIuint vnBrkPtId);
  CMIUtilThreadMutex &GetSessionMutex() { return m_sessionMutex; }
  lldb::SBDebugger &GetDebugger() const;
  lldb::SBListener &GetListener() const;
  lldb::SBTarget GetTarget() const;
  lldb::SBProcess GetProcess() const;

  // Attributes:
public:
  // The following are available to all command instances
  const MIuint m_nBrkPointCntMax;
  VecActiveThreadId_t m_vecActiveThreadId;
  lldb::tid_t m_currentSelectedThread;

  // These are keys that can be used to access the shared data map
  // Note: This list is expected to grow and will be moved and abstracted in the
  // future.
  const CMIUtilString m_constStrSharedDataKeyWkDir;
  const CMIUtilString m_constStrSharedDataSolibPath;
  const CMIUtilString m_constStrPrintCharArrayAsString;
  const CMIUtilString m_constStrPrintExpandAggregates;
  const CMIUtilString m_constStrPrintAggregateFieldNames;

  // Typedefs:
private:
  typedef std::vector<CMICmnLLDBDebugSessionInfoVarObj> VecVarObj_t;
  typedef std::map<MIuint, SBrkPtInfo> MapBrkPtIdToBrkPtInfo_t;
  typedef std::pair<MIuint, SBrkPtInfo> MapPairBrkPtIdToBrkPtInfo_t;

  // Methods:
private:
  /* ctor */ CMICmnLLDBDebugSessionInfo();
  /* ctor */ CMICmnLLDBDebugSessionInfo(const CMICmnLLDBDebugSessionInfo &);
  void operator=(const CMICmnLLDBDebugSessionInfo &);
  //
  bool GetVariableInfo(const lldb::SBValue &vrValue, const bool vbInSimpleForm,
                       CMIUtilString &vwrStrValue);
  bool GetFrameInfo(const lldb::SBFrame &vrFrame, lldb::addr_t &vwPc,
                    CMIUtilString &vwFnName, CMIUtilString &vwFileName,
                    CMIUtilString &vwPath, MIuint &vwnLine);
  bool GetThreadFrames(const SMICmdData &vCmdData, const MIuint vThreadIdx,
                       const FrameInfoFormat_e veFrameInfoFormat,
                       CMIUtilString &vwrThreadFrames);
  bool
  MIResponseForVariableInfoInternal(const VariableInfoFormat_e veVarInfoFormat,
                                    CMICmnMIValueList &vwrMiValueList,
                                    const lldb::SBValueList &vwrSBValueList,
                                    const MIuint vnMaxDepth,
                                    const bool vbIsArgs, const bool vbMarkArgs);

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnLLDBDebugSessionInfo() override;

  // Attributes:
private:
  CMIUtilMapIdToVariant m_mapIdToSessionData; // Hold and retrieve key to value
                                              // data available across all
                                              // commands
  VecVarObj_t m_vecVarObj; // Vector of session variable objects
  MapBrkPtIdToBrkPtInfo_t m_mapBrkPtIdToBrkPtInfo;
  CMIUtilThreadMutex m_sessionMutex;
};

//++
//------------------------------------------------------------------------------------
// Details: Command instances can create and share data between other instances
// of commands.
//          This function adds new data to the shared data. Using the same ID
//          more than
//          once replaces any previous matching data keys.
// Type:    Template method.
// Args:    T       - The type of the object to be stored.
//          vKey    - (R) A non empty unique data key to retrieve the data by.
//          vData   - (R) Data to be added to the share.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
template <typename T>
bool CMICmnLLDBDebugSessionInfo::SharedDataAdd(const CMIUtilString &vKey,
                                               const T &vData) {
  if (!m_mapIdToSessionData.Add<T>(vKey, vData)) {
    SetErrorDescription(m_mapIdToSessionData.GetErrorDescription());
    return MIstatus::failure;
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Command instances can create and share data between other instances
// of commands.
//          This function retrieves data from the shared data container.
// Type:    Method.
// Args:    T     - The type of the object being retrieved.
//          vKey  - (R) A non empty unique data key to retrieve the data by.
//          vData - (W) The data.
// Return:  bool  - True = data found, false = data not found or an error
// occurred trying to fetch.
// Throws:  None.
//--
template <typename T>
bool CMICmnLLDBDebugSessionInfo::SharedDataRetrieve(const CMIUtilString &vKey,
                                                    T &vwData) {
  bool bDataFound = false;

  if (!m_mapIdToSessionData.Get<T>(vKey, vwData, bDataFound)) {
    SetErrorDescription(m_mapIdToSessionData.GetErrorDescription());
    return MIstatus::failure;
  }

  return bDataFound;
}
