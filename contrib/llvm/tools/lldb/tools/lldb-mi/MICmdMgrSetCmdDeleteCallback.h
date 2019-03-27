//===-- MICmdMgrSetCmdDeleteCallback.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//--

#pragma once

// Third party headers:
#include <set>

// In-house headers:
#include "MICmnBase.h"

// Declarations:
struct SMICmdData;

namespace CMICmdMgrSetCmdDeleteCallback {

//++
//============================================================================
// Details: MI Command Manager interface for client call back.
//          Objects that want to be notified of a command being deleted
//          inherit this interface and register interest in command object
//          deletion. An object deleting a command must not do it itself but
//          call
//          the Command Manager CmdDelete() function to delete a command object.
//--
class ICallback {
public:
  virtual void Delete(SMICmdData &vCmd) = 0;

  /* dtor */ virtual ~ICallback() {}
};

//++
//============================================================================
// Details: MI Command Manager container for clients registered interest in
// command
//          objects being deleted. Objects register an interest so when a
//          command
//          is to be deleted that object wanting the delete calls the Command
//          Manager to delete the command object. In so do all other registered
//          objects get called to about the deletion including the object
//          wanting
//          to do the delete in the first place.
//--
class CSetClients : public std::set<class ICallback *>, public CMICmnBase {
  // Methods:
public:
  /* ctor */ CSetClients();

  bool Register(class ICallback &vObject);
  bool Unregister(class ICallback &vObject);
  void Delete(SMICmdData &vCmdData);

  // Overridden:
public:
  // From CMICmnBase
  /* dtor */ ~CSetClients() override;

  // Attributes:
private:
  bool m_bClientUnregistered; // True = yes while deleting a client
                              // unregistered, false = no client unregistered
                              // during deletion
};

} // namespace CMICmdMgrSetCmdDeleteCallback
