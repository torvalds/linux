//===-- MIDriverMgr.h -------------------------------------------*- C++ -*-===//
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
#include <map>

// In-house headers:
#include "MICmnBase.h"
#include "MICmnLog.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI Driver Manager. Register lldb::SBBroadcaster derived Driver type
//          objects with *this manager. The manager does not own driver objects
//          registered with it and so will not delete when this manager is
//          shutdown. The Driver flagged as "use this one" will be set as
//          current
//          driver and will be the one that is used. Other drivers are not
//          operated. A Driver can call another Driver should it not handle a
//          command.
//          It also initializes other resources as part it's setup such as the
//          Logger and Resources objects (explicit indicate *this object
//          requires
//          those objects (modules/components) to support it's own
//          functionality).
//          The Driver manager is the first object instantiated as part of the
//          MI code base. It is also the first thing to interpret the command
//          line arguments passed to the executable. Bases on options it
//          understands the manage will set up the appropriate driver or give
//          help information. Other options are passed on to the driver chosen
//          to do work.
//          Each driver instance (the CMIDriver, LLDB::Driver) has its own
//          LLDB::SBDebugger.
//          Singleton class.
//--
class CMIDriverMgr : public CMICmnBase, public MI::ISingleton<CMIDriverMgr> {
  friend MI::ISingleton<CMIDriverMgr>;

  // Class:
public:
  //++
  // Description: Driver deriver objects need this interface to work with
  //              *this manager.
  //--
  class IDriver {
  public:
    virtual bool DoInitialize() = 0;
    virtual bool DoShutdown() = 0;
    virtual bool DoMainLoop() = 0;
    virtual lldb::SBError DoParseArgs(const int argc, const char *argv[],
                                      FILE *vpStdOut, bool &vwbExiting) = 0;
    virtual CMIUtilString GetError() const = 0;
    virtual const CMIUtilString &GetName() const = 0;
    virtual lldb::SBDebugger &GetTheDebugger() = 0;
    virtual bool GetDriverIsGDBMICompatibleDriver() const = 0;
    virtual bool SetId(const CMIUtilString &vId) = 0;
    virtual const CMIUtilString &GetId() const = 0;
    virtual void DeliverSignal(int signal) = 0;

    // Not part of the interface, ignore
    /* dtor */ virtual ~IDriver() {}
  };

  // Methods:
public:
  // MI system
  bool Initialize() override;
  bool Shutdown() override;
  //
  CMIUtilString GetAppVersion() const;
  bool RegisterDriver(const IDriver &vrADriver,
                      const CMIUtilString &vrDriverID);
  bool UnregisterDriver(const IDriver &vrADriver);
  bool SetUseThisDriverToDoWork(
      const IDriver &vrADriver); // Specify working main driver
  IDriver *GetUseThisDriverToDoWork() const;
  bool ParseArgs(const int argc, const char *argv[], bool &vwbExiting);
  IDriver *GetDriver(const CMIUtilString &vrDriverId) const;
  //
  // MI Proxy fn to current specified working driver
  bool DriverMainLoop();
  bool DriverParseArgs(const int argc, const char *argv[], FILE *vpStdOut,
                       bool &vwbExiting);
  CMIUtilString DriverGetError() const;
  CMIUtilString DriverGetName() const;
  lldb::SBDebugger *DriverGetTheDebugger();
  void DeliverSignal(int signal);

  // Typedef:
private:
  typedef std::map<CMIUtilString, IDriver *> MapDriverIdToDriver_t;
  typedef std::pair<CMIUtilString, IDriver *> MapPairDriverIdToDriver_t;

  // Methods:
private:
  /* ctor */ CMIDriverMgr();
  /* ctor */ CMIDriverMgr(const CMIDriverMgr &);
  void operator=(const CMIDriverMgr &);
  //
  bool HaveDriverAlready(const IDriver &vrMedium) const;
  bool UnregisterDriverAll();
  IDriver *GetFirstMIDriver() const;
  IDriver *GetFirstNonMIDriver() const;
  CMIUtilString GetHelpOnCmdLineArgOptions() const;

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMIDriverMgr() override;

  // Attributes:
private:
  MapDriverIdToDriver_t m_mapDriverIdToDriver;
  IDriver *m_pDriverCurrent; // This driver is used by this manager to do work.
                             // It is the main driver.
  bool m_bInMi2Mode; // True = --interpreter entered on the cmd line, false =
                     // operate LLDB driver (non GDB)
};
