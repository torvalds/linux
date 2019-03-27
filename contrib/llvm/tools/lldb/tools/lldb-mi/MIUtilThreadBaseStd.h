//===-- MIUtilThreadBaseStd.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

// Third party headers:
#include <mutex>
#include <thread>

// In-house headers:
#include "MIDataTypes.h"
#include "MIUtilString.h"

//++
//============================================================================
// Details: MI common code utility class. Handle thread mutual exclusion.
//          Embed Mutexes in your Active Object and then use them through Locks.
//--
class CMIUtilThreadMutex {
  // Methods:
public:
  /* ctor */ CMIUtilThreadMutex() {}
  //
  void Lock();    // Wait until mutex can be obtained
  void Unlock();  // Release the mutex
  bool TryLock(); // Gain the lock if available

  // Overrideable:
public:
  // From CMICmnBase
  /* dtor */ virtual ~CMIUtilThreadMutex() {}

  // Attributes:
private:
  std::recursive_mutex m_mutex;
};

//++
//============================================================================
// Details: MI common code utility class. Thread object.
//--
class CMIUtilThread {
  // Typedef:
public:
  typedef MIuint (*FnThreadProc)(void *vpThisClass);

  // Methods:
public:
  /* ctor */ CMIUtilThread();
  //
  bool Start(FnThreadProc vpFn, void *vpArg); // Start execution of this thread
  bool Join();                                // Wait for this thread to stop
  bool IsActive(); // Returns true if this thread is running
  void Finish();   // Finish this thread

  // Overrideable:
public:
  /* dtor */ virtual ~CMIUtilThread();

  // Methods:
private:
  CMIUtilThreadMutex m_mutex;
  std::thread *m_pThread;
  bool m_bIsActive;
};

//++
//============================================================================
// Details: MI common code utility class. Base class for a worker thread active
//          object. Runs an 'captive thread'.
//--
class CMIUtilThreadActiveObjBase {
  // Methods:
public:
  /* ctor */ CMIUtilThreadActiveObjBase();
  //
  bool Acquire();        // Obtain a reference to this object
  bool Release();        // Release a reference to this object
  bool ThreadIsActive(); // Return true if this object is running
  bool ThreadJoin();     // Wait for this thread to stop running
  bool ThreadKill();     // Force this thread to stop, regardless of references
  bool ThreadExecute();  // Start this objects execution in another thread
  void ThreadManage();

  // Overrideable:
public:
  /* dtor */ virtual ~CMIUtilThreadActiveObjBase();
  //
  // Each thread object must supple a unique name that can be used to locate it
  virtual const CMIUtilString &ThreadGetName() const = 0;

  // Statics:
protected:
  static MIuint ThreadEntry(void *vpThisClass); // Thread entry point

  // Overrideable:
protected:
  virtual bool ThreadRun(bool &vrIsAlive) = 0; // Call the main worker method
  virtual bool ThreadFinish() = 0;             // Finish of what you were doing

  // Attributes:
protected:
  volatile MIuint m_references; // Stores the current lifetime state of this
                                // thread, 0 = running, > 0 = shutting down
  volatile bool
      m_bHasBeenKilled;       // Set to true when this thread has been killed
  CMIUtilThread m_thread;     // The execution thread
  CMIUtilThreadMutex m_mutex; // This mutex allows us to safely communicate with
                              // this thread object across the interface from
                              // multiple threads
};

//++
//============================================================================
// Details: MI common code utility class. Handle thread resource locking.
//          Put Locks inside all the methods of your Active Object that access
//          data shared with the captive thread.
//--
class CMIUtilThreadLock {
  // Methods:
public:
  /* ctor */
  CMIUtilThreadLock(CMIUtilThreadMutex &vMutex) : m_rMutex(vMutex) {
    m_rMutex.Lock();
  }

  // Overrideable:
public:
  /* dtor */
  virtual ~CMIUtilThreadLock() { m_rMutex.Unlock(); }

  // Attributes:
private:
  CMIUtilThreadMutex &m_rMutex;
};
