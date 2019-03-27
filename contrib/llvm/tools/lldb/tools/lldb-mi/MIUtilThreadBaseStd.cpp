//===-- MIUtilThreadBaseStd.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third Party Headers:
#include <assert.h>

// In-house headers:
#include "MICmnThreadMgrStd.h"
#include "MIUtilThreadBaseStd.h"

//++
//------------------------------------------------------------------------------------
// Details: Constructor.
// Type:    None.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilThreadActiveObjBase::CMIUtilThreadActiveObjBase()
    : m_references(0), m_bHasBeenKilled(false) {}

//++
//------------------------------------------------------------------------------------
// Details: Destructor.
// Type:    None.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilThreadActiveObjBase::~CMIUtilThreadActiveObjBase() {
  // Make sure our thread is not alive before we die
  m_thread.Join();
}

//++
//------------------------------------------------------------------------------------
// Details: Check if an object is already running.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::ThreadIsActive() {
  // Create a new thread to occupy this threads Run() function
  return m_thread.IsActive();
}

//++
//------------------------------------------------------------------------------------
// Details: Set up *this thread.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::ThreadExecute() {
  // Create a new thread to occupy this threads Run() function
  return m_thread.Start(ThreadEntry, this);
}

//++
//------------------------------------------------------------------------------------
// Details: Acquire a reference to CMIUtilThreadActiveObjBase.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::Acquire() {
  // Access to this function is serial
  CMIUtilThreadLock serial(m_mutex);

  // >0 == *this thread is alive
  m_references++;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Release a reference to CMIUtilThreadActiveObjBase.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::Release() {
  // Access to this function is serial
  CMIUtilThreadLock serial(m_mutex);

  // 0 == kill off *this thread
  m_references--;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Force this thread to stop, regardless of references
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::ThreadKill() {
  // Access to this function is serial
  CMIUtilThreadLock serial(m_mutex);

  // Set this thread to killed status
  m_bHasBeenKilled = true;

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Proxy to thread join.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThreadActiveObjBase::ThreadJoin() { return m_thread.Join(); }

//++
//------------------------------------------------------------------------------------
// Details: This function is the entry point of this object thread.
//          It is a trampoline to an instances operation manager.
// Type:    Static method.
// Args:    vpThisClass - (R) From the system (our CMIUtilThreadActiveObjBase
// from the ctor).
// Return:  MIuint - 0 = success.
// Throws:  None.
//--
MIuint CMIUtilThreadActiveObjBase::ThreadEntry(void *vpThisClass) {
  // The argument is a pointer to a CMIUtilThreadActiveObjBase class
  // as passed from the initialize function, so we can safely cast it.
  assert(vpThisClass != nullptr);
  CMIUtilThreadActiveObjBase *pActive =
      reinterpret_cast<CMIUtilThreadActiveObjBase *>(vpThisClass);

  // Start the management routine of this object
  pActive->ThreadManage();

  // Thread death
  return 0;
}

//++
//------------------------------------------------------------------------------------
// Details: This function forms a small management routine, to handle the
// thread's running.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilThreadActiveObjBase::ThreadManage() {
  bool bAlive = true;

  // Infinite loop
  while (bAlive) {
    // Scope the lock while we access m_isDying
    {
      // Lock down access to the interface
      CMIUtilThreadLock serial(m_mutex);

      // Quit the run loop if we are dying
      if (m_references == 0)
        break;
    }
    // Execute the run routine
    if (!ThreadRun(bAlive))
      // Thread's run function failed (MIstatus::failure)
      break;

    // We will die if we have been signaled to die
    bAlive &= !m_bHasBeenKilled;
  }

  // Execute the finish routine just before we die
  // to give the object a chance to clean up
  ThreadFinish();

  m_thread.Finish();
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//
CMIUtilThread::CMIUtilThread() : m_pThread(nullptr), m_bIsActive(false) {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilThread destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilThread::~CMIUtilThread() { Join(); }

//++
//------------------------------------------------------------------------------------
// Details: Wait for thread to stop.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThread::Join() {
  if (m_pThread != nullptr) {
    // Wait for this thread to die
    m_pThread->join();

    // Scope the thread lock while we modify the pointer
    {
      CMIUtilThreadLock _lock(m_mutex);
      delete m_pThread;
      m_pThread = nullptr;
    }
  }

  return MIstatus::success;
}

//++
//------------------------------------------------------------------------------------
// Details: Is the thread doing work.
// Type:    Method.
// Args:    None.
// Return:  bool - True = Yes active, false = not active.
// Throws:  None.
//--
bool CMIUtilThread::IsActive() {
  // Lock while we access the thread status
  CMIUtilThreadLock _lock(m_mutex);
  return m_bIsActive;
}

//++
//------------------------------------------------------------------------------------
// Details: Finish this thread
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilThread::Finish() {
  // Lock while we access the thread status
  CMIUtilThreadLock _lock(m_mutex);
  m_bIsActive = false;
}

//++
//------------------------------------------------------------------------------------
// Details: Set up *this thread.
// Type:    Method.
// Args:    vpFn    (R) - Function pointer to thread's main function.
//          vpArg   (R) - Pointer arguments to pass to the thread.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool CMIUtilThread::Start(FnThreadProc vpFn, void *vpArg) {
  // Lock while we access the thread pointer and status
  CMIUtilThreadLock _lock(m_mutex);

  // Create the std thread, which starts immediately and update its status
  m_pThread = new std::thread(vpFn, vpArg);
  m_bIsActive = true;

  // We expect to always be able to create one
  assert(m_pThread != nullptr);

  return MIstatus::success;
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

//++
//------------------------------------------------------------------------------------
// Details: Take resource.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilThreadMutex::Lock() { m_mutex.lock(); }

//++
//------------------------------------------------------------------------------------
// Details: Release resource.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilThreadMutex::Unlock() { m_mutex.unlock(); }

//++
//------------------------------------------------------------------------------------
// Details: Take resource if available. Immediately return in either case.
// Type:    Method.
// Args:    None.
// Return:  True    - mutex has been locked.
//          False   - mutex could not be locked.
// Throws:  None.
//--
bool CMIUtilThreadMutex::TryLock() { return m_mutex.try_lock(); }
