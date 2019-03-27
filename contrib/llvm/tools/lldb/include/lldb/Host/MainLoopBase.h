//===-- MainLoopBase.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_posix_MainLoopBase_h_
#define lldb_Host_posix_MainLoopBase_h_

#include "lldb/Utility/IOObject.h"
#include "lldb/Utility/Status.h"
#include "llvm/Support/ErrorHandling.h"
#include <functional>

namespace lldb_private {

// The purpose of this class is to enable multiplexed processing of data from
// different sources without resorting to multi-threading. Clients can register
// IOObjects, which will be monitored for readability, and when they become
// ready, the specified callback will be invoked. Monitoring for writability is
// not supported, but can be easily added if needed.
//
// The RegisterReadObject function return a handle, which controls the duration
// of the monitoring. When this handle is destroyed, the callback is
// deregistered.
//
// This class simply defines the interface common for all platforms, actual
// implementations are platform-specific.
class MainLoopBase {
private:
  class ReadHandle;

public:
  MainLoopBase() {}
  virtual ~MainLoopBase() {}

  typedef std::unique_ptr<ReadHandle> ReadHandleUP;

  typedef std::function<void(MainLoopBase &)> Callback;

  virtual ReadHandleUP RegisterReadObject(const lldb::IOObjectSP &object_sp,
                                          const Callback &callback,
                                          Status &error) {
    llvm_unreachable("Not implemented");
  }

  // Waits for registered events and invoke the proper callbacks. Returns when
  // all callbacks deregister themselves or when someone requests termination.
  virtual Status Run() { llvm_unreachable("Not implemented"); }

  // Requests the exit of the Run() function.
  virtual void RequestTermination() { llvm_unreachable("Not implemented"); }

protected:
  ReadHandleUP CreateReadHandle(const lldb::IOObjectSP &object_sp) {
    return ReadHandleUP(new ReadHandle(*this, object_sp->GetWaitableHandle()));
  }

  virtual void UnregisterReadObject(IOObject::WaitableHandle handle) {
    llvm_unreachable("Not implemented");
  }

private:
  class ReadHandle {
  public:
    ~ReadHandle() { m_mainloop.UnregisterReadObject(m_handle); }

  private:
    ReadHandle(MainLoopBase &mainloop, IOObject::WaitableHandle handle)
        : m_mainloop(mainloop), m_handle(handle) {}

    MainLoopBase &m_mainloop;
    IOObject::WaitableHandle m_handle;

    friend class MainLoopBase;
    DISALLOW_COPY_AND_ASSIGN(ReadHandle);
  };

private:
  DISALLOW_COPY_AND_ASSIGN(MainLoopBase);
};

} // namespace lldb_private

#endif // lldb_Host_posix_MainLoopBase_h_
