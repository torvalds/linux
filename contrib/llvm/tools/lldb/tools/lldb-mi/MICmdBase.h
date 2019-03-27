//===-- MICmdBase.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include "lldb/API/SBError.h"

#include "MICmdArgSet.h"
#include "MICmdData.h"
#include "MICmdFactory.h"
#include "MICmdInvoker.h"
#include "MICmnBase.h"
#include "MICmnMIResultRecord.h"
#include "MICmnResources.h"
#include "MIUtilString.h"

// Declarations:
class CMICmnLLDBDebugSessionInfo;

//++
//============================================================================
// Details: MI command base class. MI commands derive from this base class.
//          The Command Factory creates command objects and passes them to the
//          Command Invoker. The Invoker takes ownership of any commands created
//          which means it is the only object to delete them when a command is
//          finished working. Commands do not delete themselves.
//          There are two types of command implicitly defined by the state of
//          the m_bWaitForEventFromSBDebugger flag. There is the event type
//          command which registers (command fn) callbacks with the SBListener
//          does some work then wakes up again when called back, does more work
//          perhaps, ends, then the Invoker calls the command's Acknowledge
//          function. The other type of command is one that just does some work,
//          ends, then the Invoker calls the command's Acknowledge function. No
//          events set up.
//          A command's Execute(), Acknowledge() and event callback functions
//          are
//          carried out in the main thread.
//          A command may use the argument derived object classes
//          (CMICmdArgValBase)
//          to factor handling and parsing of different types of arguments
//          presented to a command. A command will produce an error should it
//          be presented with arguments or options it does not understand.
//--
class CMICmdBase : public CMICmnBase,
                   public CMICmdInvoker::ICmd,
                   public CMICmdFactory::ICmd {
  // Methods:
public:
  CMICmdBase();

  // Overridden:
  // From CMICmdInvoker::ICmd
  const SMICmdData &GetCmdData() const override;
  const CMIUtilString &GetErrorDescription() const override;
  void SetCmdData(const SMICmdData &vCmdData) override;
  void CmdFinishedTellInvoker() const override;
  const CMIUtilString &GetMIResultRecord() const override;
  const CMIUtilString &GetMIResultRecordExtra() const override;
  bool HasMIResultRecordExtra() const override;
  bool ParseArgs() override;
  // From CMICmdFactory::ICmd
  const CMIUtilString &GetMiCmd() const override;
  CMICmdFactory::CmdCreatorFnPtr GetCmdCreatorFn() const override;

  virtual MIuint GetGUID();
  void AddCommonArgs();

  // Overrideable:
  ~CMICmdBase() override;
  virtual bool GetExitAppOnCommandFailure() const;

  // Methods:
protected:
  void SetError(const CMIUtilString &rErrMsg);
  bool HandleSBError(const lldb::SBError &error,
                     const std::function<bool()> &successHandler =
                     [] { return MIstatus::success; },
                     const std::function<void()> &errorHandler = [] {});
  bool HandleSBErrorWithSuccess(const lldb::SBError &error,
                                const std::function<bool()> &successHandler);
  bool HandleSBErrorWithFailure(const lldb::SBError &error,
                                const std::function<void()> &errorHandler);
  template <class T> T *GetOption(const CMIUtilString &vStrOptionName);
  bool ParseValidateCmdOptions();

  // Attributes:
  CMICmdFactory::CmdCreatorFnPtr m_pSelfCreatorFn;
  CMIUtilString m_strCurrentErrDescription; // Reason for Execute or Acknowledge
                                            // function failure
  SMICmdData m_cmdData; // Holds information/status of *this command. Used by
                        // other MI code to report or determine state of a
                        // command.
  bool m_bWaitForEventFromSBDebugger; // True = yes event type command wait,
                                      // false = command calls Acknowledge()
                                      // straight after Execute()
                                      // no waiting
  CMIUtilString
      m_strMiCmd; // The MI text identifying *this command i.e. 'break-insert'
  CMICmnMIResultRecord m_miResultRecord; // This is completed in the
                                         // Acknowledge() function and returned
                                         // to the Command Invoker to proceed
  // stdout output. Each command forms 1 response to its input.
  CMIUtilString m_miResultRecordExtra; // This is completed in the Acknowledge()
                                       // function and returned to the Command
                                       // Invoker to proceed
  // stdout output. Hack command produce more response text to help the client
  // because of using LLDB
  CMICmnLLDBDebugSessionInfo &m_rLLDBDebugSessionInfo; // Access to command
                                                       // sharing information or
                                                       // data across any and
                                                       // all command based
                                                       // derived classes.
  bool m_bHasResultRecordExtra; // True = Yes command produced additional MI
                                // output to its 1 line response, false = no
                                // extra MI output
                                // formed.
  CMICmdArgSet m_setCmdArgs;    // The list of arguments *this command needs to
                             // parse from the options string to carry out work.
  const CMIUtilString m_constStrArgThreadGroup;
  const CMIUtilString m_constStrArgThread;
  const CMIUtilString m_constStrArgFrame;
  const CMIUtilString m_constStrArgConsume;

  // These 3 members can be used by the derived classes to make any of
  // "thread", "frame" or "thread-group" mandatory.
  bool m_ThreadGrpArgMandatory;
  bool m_ThreadArgMandatory;
  bool m_FrameArgMandatory;
};

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the command argument or option object pointer so that it
// can be
//          examined. If the option found and valid get the value (number,
//          string or list
//          - see CMICmdArgValBase class) from it to use with the command's
//          decision
//          making. If the argument is not found the command's error description
//          is set
//          describing the error condition.
// Type:    Template method.
// Args:    vStrOptionName  - (R)   The text name of the argument or option to
// search for in
//                                  the list of the command's possible arguments
//                                  or options.
// Return:  T * - CMICmdArgValBase derived object.
//              - nullptr = function has failed, unable to retrieve the
//              option/arg object.
// Throws:  None.
//--
template <class T>
T *CMICmdBase::GetOption(const CMIUtilString &vStrOptionName) {
  CMICmdArgValBase *pPtrBase = nullptr;
  if (!m_setCmdArgs.GetArg(vStrOptionName, pPtrBase)) {
    SetError(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_OPTION_NOT_FOUND),
                                   m_cmdData.strMiCmd.c_str(),
                                   vStrOptionName.c_str()));
    return nullptr;
  }

  return static_cast<T *>(pPtrBase);
}

//++
//------------------------------------------------------------------------------------
// Details: Retrieve the command argument or option object pointer using
// template function
//          CMICmdBase::GetOption(). Should the argument (by name) not be found
//          the
//          command will exit with a failure (set in GetOption()).
// Type:    Preprocessor macro.
// Args:    a   - (R) The actual variable's name.
//          b   - (R) The type of variable (appended to CMICmdArgVal i.e.
//          CMICmdArgValString).
//          c   - (R) The text name of the argument or option to search for in
//          the list of
//                    the command's possible arguments or options.
// Return:  T * - CMICmdArgValBase derived object.
//              - nullptr = function has failed, unable to retrieve the
//              option/arg object.
// Throws:  None.
//--
#define CMICMDBASE_GETOPTION(a, b, c)                                          \
  CMICmdArgVal##b *a = CMICmdBase::GetOption<CMICmdArgVal##b>(c);              \
  if (a == nullptr)                                                            \
    return MIstatus::failure;
// This comment is to stop compile warning for #define
