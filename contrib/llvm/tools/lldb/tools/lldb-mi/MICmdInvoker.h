//===-- MICmdInvoker.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers
#include <map>

// In-house headers:
#include "MICmdData.h"
#include "MICmdMgrSetCmdDeleteCallback.h"
#include "MICmnBase.h"
#include "MIUtilSingletonBase.h"

// Declarations:
class CMICmdBase;
class CMICmnStreamStdout;

//++
//============================================================================
// Details: MI Command Invoker. The Invoker works on the command pattern design.
//          There two main jobs; action command Execute() function, followed by
//          the command's Acknowledge() function. When a command has finished
//          its
//          execute function it returns to the invoker. The invoker then calls
//          the
//          command's Acknowledge() function to do more work, form and give
//          back a MI result. In the meantime the Command Monitor is monitoring
//          the each command doing their Execute() function work so they do not
//          exceed a time limit which if it exceeds informs the command(s) to
//          stop work.
//          The work by the Invoker is carried out in the main thread.
//          The Invoker takes ownership of any commands created which means it
//          is the only object to delete them when a command is finished
//          working.
//          A singleton class.
//--
class CMICmdInvoker : public CMICmnBase,
                      public CMICmdMgrSetCmdDeleteCallback::ICallback,
                      public MI::ISingleton<CMICmdInvoker> {
  friend class MI::ISingleton<CMICmdInvoker>;

  // Class:
public:
  //++
  // Description: Invoker's interface for commands to implement.
  //--
  class ICmd {
  public:
    virtual bool Acknowledge() = 0;
    virtual bool Execute() = 0;
    virtual bool ParseArgs() = 0;
    virtual void SetCmdData(const SMICmdData &vCmdData) = 0;
    virtual const SMICmdData &GetCmdData() const = 0;
    virtual const CMIUtilString &GetErrorDescription() const = 0;
    virtual void CmdFinishedTellInvoker() const = 0;
    virtual const CMIUtilString &GetMIResultRecord() const = 0;
    virtual const CMIUtilString &GetMIResultRecordExtra() const = 0;
    virtual bool HasMIResultRecordExtra() const = 0;

    /* dtor */ virtual ~ICmd() {}
  };

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  bool CmdExecute(CMICmdBase &vCmd);
  bool CmdExecuteFinished(CMICmdBase &vCmd);

  // Typedefs:
private:
  typedef std::map<MIuint, CMICmdBase *> MapCmdIdToCmd_t;
  typedef std::pair<MIuint, CMICmdBase *> MapPairCmdIdToCmd_t;

  // Methods:
private:
  /* ctor */ CMICmdInvoker();
  /* ctor */ CMICmdInvoker(const CMICmdInvoker &);
  void operator=(const CMICmdInvoker &);
  void CmdDeleteAll();
  bool CmdDelete(const MIuint vCmdId, const bool vbYesDeleteCmd = false);
  bool CmdAdd(const CMICmdBase &vCmd);
  bool CmdStdout(const SMICmdData &vCmdData) const;
  void CmdCauseAppExit(const CMICmdBase &vCmd) const;

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmdInvoker() override;
  // From CMICmdMgrSetCmdDeleteCallback::ICallback
  void Delete(SMICmdData &vCmd) override;

  // Attributes:
private:
  MapCmdIdToCmd_t m_mapCmdIdToCmd;
  CMICmnStreamStdout &m_rStreamOut;
};
