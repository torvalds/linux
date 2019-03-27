//===-- MainLoop.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_MainLoop_h_
#define lldb_Host_MainLoop_h_

#include "lldb/Host/Config.h"
#include "lldb/Host/MainLoopBase.h"
#include "llvm/ADT/DenseMap.h"
#include <csignal>

#if !HAVE_PPOLL && !HAVE_SYS_EVENT_H && !defined(__ANDROID__)
#define SIGNAL_POLLING_UNSUPPORTED 1
#endif

namespace lldb_private {

// Implementation of the MainLoopBase class. It can monitor file descriptors
// for readability using ppoll, kqueue, poll or WSAPoll. On Windows it only
// supports polling sockets, and will not work on generic file handles or
// pipes. On systems without kqueue or ppoll handling singnals is not
// supported. In addition to the common base, this class provides the ability
// to invoke a given handler when a signal is received.
//
// Since this class is primarily intended to be used for single-threaded
// processing, it does not attempt to perform any internal synchronisation and
// any concurrent accesses must be protected  externally. However, it is
// perfectly legitimate to have more than one instance of this class running on
// separate threads, or even a single thread (with some limitations on signal
// monitoring).
// TODO: Add locking if this class is to be used in a multi-threaded context.
class MainLoop : public MainLoopBase {
private:
  class SignalHandle;

public:
  typedef std::unique_ptr<SignalHandle> SignalHandleUP;

  MainLoop();
  ~MainLoop() override;

  ReadHandleUP RegisterReadObject(const lldb::IOObjectSP &object_sp,
                                  const Callback &callback,
                                  Status &error) override;

  // Listening for signals from multiple MainLoop instances is perfectly safe
  // as long as they don't try to listen for the same signal. The callback
  // function is invoked when the control returns to the Run() function, not
  // when the hander is executed. This mean that you can treat the callback as
  // a normal function and perform things which would not be safe in a signal
  // handler. However, since the callback is not invoked synchronously, you
  // cannot use this mechanism to handle SIGSEGV and the like.
  SignalHandleUP RegisterSignal(int signo, const Callback &callback,
                                Status &error);

  Status Run() override;

  // This should only be performed from a callback. Do not attempt to terminate
  // the processing from another thread.
  // TODO: Add synchronization if we want to be terminated from another thread.
  void RequestTermination() override { m_terminate_request = true; }

protected:
  void UnregisterReadObject(IOObject::WaitableHandle handle) override;

  void UnregisterSignal(int signo);

private:
  void ProcessReadObject(IOObject::WaitableHandle handle);
  void ProcessSignal(int signo);

  class SignalHandle {
  public:
    ~SignalHandle() { m_mainloop.UnregisterSignal(m_signo); }

  private:
    SignalHandle(MainLoop &mainloop, int signo)
        : m_mainloop(mainloop), m_signo(signo) {}

    MainLoop &m_mainloop;
    int m_signo;

    friend class MainLoop;
    DISALLOW_COPY_AND_ASSIGN(SignalHandle);
  };

  struct SignalInfo {
    Callback callback;
#if HAVE_SIGACTION
    struct sigaction old_action;
#endif
    bool was_blocked : 1;
  };
  class RunImpl;

  llvm::DenseMap<IOObject::WaitableHandle, Callback> m_read_fds;
  llvm::DenseMap<int, SignalInfo> m_signals;
#if HAVE_SYS_EVENT_H
  int m_kqueue;
#endif
  bool m_terminate_request : 1;
};

} // namespace lldb_private

#endif // lldb_Host_MainLoop_h_
