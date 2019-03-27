//===-- MICmdFactory.h ------------------------------------------*- C++ -*-===//
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
#include "MICmnBase.h"
#include "MIUtilSingletonBase.h"

// Declarations:
class CMICmdBase;
struct SMICmdData;

//++
//============================================================================
// Details: MI Command Factory. Holds a list of registered MI commands that
//          MI application understands to interpret. Creates commands objects.
//          The Command Factory is carried out in the main thread.
//          A singleton class.
//--
class CMICmdFactory : public CMICmnBase, public MI::ISingleton<CMICmdFactory> {
  friend class MI::ISingleton<CMICmdFactory>;

  // Typedefs:
public:
  typedef CMICmdBase *(*CmdCreatorFnPtr)();

  // Class:
public:
  //++
  // Description: Command's factory's interface for commands to implement.
  //--
  class ICmd {
  public:
    virtual const CMIUtilString &GetMiCmd() const = 0;
    virtual CmdCreatorFnPtr GetCmdCreatorFn() const = 0;
    // virtual CMICmdBase *         CreateSelf( void ) = 0;             // Not
    // possible as require a static creator
    // function in the command class, here for awareness

    /* dtor */ virtual ~ICmd() {}
  };

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  bool CmdRegister(const CMIUtilString &vMiCmd, CmdCreatorFnPtr vCmdCreateFn);
  bool CmdCreate(const CMIUtilString &vMiCmd, const SMICmdData &vCmdData,
                 CMICmdBase *&vpNewCmd);
  bool CmdExist(const CMIUtilString &vMiCmd) const;

  // Methods:
private:
  /* ctor */ CMICmdFactory();
  /* ctor */ CMICmdFactory(const CMICmdFactory &);
  void operator=(const CMICmdFactory &);

  bool HaveAlready(const CMIUtilString &vMiCmd) const;
  bool IsValid(const CMIUtilString &vMiCmd) const;

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmdFactory() override;

  // Typedefs:
private:
  typedef std::map<CMIUtilString, CmdCreatorFnPtr> MapMiCmdToCmdCreatorFn_t;
  typedef std::pair<CMIUtilString, CmdCreatorFnPtr>
      MapPairMiCmdToCmdCreatorFn_t;

  // Attributes:
private:
  MapMiCmdToCmdCreatorFn_t m_mapMiCmdToCmdCreatorFn;
};
