//===-- MICmnThreadMgrStd.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include <vector>

// In-house headers:
#include "MICmnBase.h"
#include "MICmnResources.h"
#include "MIUtilSingletonBase.h"
#include "MIUtilThreadBaseStd.h"

//++
//============================================================================
// Details: MI's worker thread (active thread) manager.
//          The manager creates threads and behalf of clients. Client are
//          responsible for their threads and can delete them when necessary.
//          This manager will stop and delete all threads on *this manager's
//          shutdown.
//          Singleton class.
//--
class CMICmnThreadMgrStd : public CMICmnBase,
                           public MI::ISingleton<CMICmnThreadMgrStd> {
  friend MI::ISingleton<CMICmnThreadMgrStd>;

  // Methods:
public:
  bool Initialize() override;
  bool Shutdown() override;
  bool ThreadAllTerminate(); // Ask all threads to stop (caution)
  template <typename T> // Ask the thread manager to start and stop threads on
                        // our behalf
                        bool ThreadStart(T &vrwObject);

  // Typedef:
private:
  typedef std::vector<CMIUtilThreadActiveObjBase *> ThreadList_t;

  // Methods:
private:
  /* ctor */ CMICmnThreadMgrStd();
  /* ctor */ CMICmnThreadMgrStd(const CMICmnThreadMgrStd &);
  void operator=(const CMICmnThreadMgrStd &);
  //
  bool AddThread(const CMIUtilThreadActiveObjBase &
                     vrObj); // Add a thread for monitoring by the threadmanager

  // Overridden:
private:
  // From CMICmnBase
  /* dtor */ ~CMICmnThreadMgrStd() override;

  // Attributes:
private:
  CMIUtilThreadMutex m_mutex;
  ThreadList_t m_threadList;
};

//++
//------------------------------------------------------------------------------------
// Details: Given a thread object start its (worker) thread to do work. The
// object is
//          added to the *this manager for housekeeping and deletion of all
//          thread objects.
// Type:    Template method.
// Args:    vrwThreadObj      - (RW) A CMIUtilThreadActiveObjBase derived
// object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
template <typename T> bool CMICmnThreadMgrStd::ThreadStart(T &vrwThreadObj) {
  bool bOk = MIstatus::success;

  // Grab a reference to the base object type
  CMIUtilThreadActiveObjBase &rObj =
      static_cast<CMIUtilThreadActiveObjBase &>(vrwThreadObj);

  // Add to the thread managers internal database
  bOk &= AddThread(rObj);
  if (!bOk) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_THREADMGR_ERR_THREAD_FAIL_CREATE),
                              vrwThreadObj.ThreadGetName().c_str()));
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  // Grab a reference on behalf of the caller
  bOk &= vrwThreadObj.Acquire();
  if (!bOk) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_THREADMGR_ERR_THREAD_FAIL_CREATE),
                              vrwThreadObj.ThreadGetName().c_str()));
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  // Thread is already started
  // This call must come after the reference count increment
  if (vrwThreadObj.ThreadIsActive()) {
    // Early exit on thread already running condition
    return MIstatus::success;
  }

  // Start the thread running
  bOk &= vrwThreadObj.ThreadExecute();
  if (!bOk) {
    const CMIUtilString errMsg(
        CMIUtilString::Format(MIRSRC(IDS_THREADMGR_ERR_THREAD_FAIL_CREATE),
                              vrwThreadObj.ThreadGetName().c_str()));
    SetErrorDescription(errMsg);
    return MIstatus::failure;
  }

  return MIstatus::success;
}
