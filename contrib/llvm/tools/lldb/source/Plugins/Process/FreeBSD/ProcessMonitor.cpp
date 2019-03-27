//===-- ProcessMonitor.cpp ------------------------------------ -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lldb/Host/Host.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "llvm/Support/Errno.h"

#include "FreeBSDThread.h"
#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "ProcessFreeBSD.h"
#include "ProcessMonitor.h"

using namespace lldb;
using namespace lldb_private;

// We disable the tracing of ptrace calls for integration builds to avoid the
// additional indirection and checks.
#ifndef LLDB_CONFIGURATION_BUILDANDINTEGRATION
// Wrapper for ptrace to catch errors and log calls.

const char *Get_PT_IO_OP(int op) {
  switch (op) {
  case PIOD_READ_D:
    return "READ_D";
  case PIOD_WRITE_D:
    return "WRITE_D";
  case PIOD_READ_I:
    return "READ_I";
  case PIOD_WRITE_I:
    return "WRITE_I";
  default:
    return "Unknown op";
  }
}

// Wrapper for ptrace to catch errors and log calls. Note that ptrace sets
// errno on error because -1 is reserved as a valid result.
extern long PtraceWrapper(int req, lldb::pid_t pid, void *addr, int data,
                          const char *reqName, const char *file, int line) {
  long int result;

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PTRACE));

  if (log) {
    log->Printf("ptrace(%s, %" PRIu64 ", %p, %x) called from file %s line %d",
                reqName, pid, addr, data, file, line);
    if (req == PT_IO) {
      struct ptrace_io_desc *pi = (struct ptrace_io_desc *)addr;

      log->Printf("PT_IO: op=%s offs=%zx size=%zu", Get_PT_IO_OP(pi->piod_op),
                  (size_t)pi->piod_offs, pi->piod_len);
    }
  }

  // PtraceDisplayBytes(req, data);

  errno = 0;
  result = ptrace(req, pid, (caddr_t)addr, data);

  // PtraceDisplayBytes(req, data);

  if (log && errno != 0) {
    const char *str;
    switch (errno) {
    case ESRCH:
      str = "ESRCH";
      break;
    case EINVAL:
      str = "EINVAL";
      break;
    case EBUSY:
      str = "EBUSY";
      break;
    case EPERM:
      str = "EPERM";
      break;
    default:
      str = "<unknown>";
    }
    log->Printf("ptrace() failed; errno=%d (%s)", errno, str);
  }

  if (log) {
#ifdef __amd64__
    if (req == PT_GETREGS) {
      struct reg *r = (struct reg *)addr;

      log->Printf("PT_GETREGS: rip=0x%lx rsp=0x%lx rbp=0x%lx rax=0x%lx",
                  r->r_rip, r->r_rsp, r->r_rbp, r->r_rax);
    }
    if (req == PT_GETDBREGS || req == PT_SETDBREGS) {
      struct dbreg *r = (struct dbreg *)addr;
      char setget = (req == PT_GETDBREGS) ? 'G' : 'S';

      for (int i = 0; i <= 7; i++)
        log->Printf("PT_%cETDBREGS: dr[%d]=0x%lx", setget, i, r->dr[i]);
    }
#endif
  }

  return result;
}

// Wrapper for ptrace when logging is not required. Sets errno to 0 prior to
// calling ptrace.
extern long PtraceWrapper(int req, lldb::pid_t pid, void *addr, int data) {
  long result = 0;
  errno = 0;
  result = ptrace(req, pid, (caddr_t)addr, data);
  return result;
}

#define PTRACE(req, pid, addr, data)                                           \
  PtraceWrapper((req), (pid), (addr), (data), #req, __FILE__, __LINE__)
#else
PtraceWrapper((req), (pid), (addr), (data))
#endif

//------------------------------------------------------------------------------
// Static implementations of ProcessMonitor::ReadMemory and
// ProcessMonitor::WriteMemory.  This enables mutual recursion between these
// functions without needed to go thru the thread funnel.

static size_t DoReadMemory(lldb::pid_t pid, lldb::addr_t vm_addr, void *buf,
                           size_t size, Status &error) {
  struct ptrace_io_desc pi_desc;

  pi_desc.piod_op = PIOD_READ_D;
  pi_desc.piod_offs = (void *)vm_addr;
  pi_desc.piod_addr = buf;
  pi_desc.piod_len = size;

  if (PTRACE(PT_IO, pid, (caddr_t)&pi_desc, 0) < 0) {
    error.SetErrorToErrno();
    return 0;
  }
  return pi_desc.piod_len;
}

static size_t DoWriteMemory(lldb::pid_t pid, lldb::addr_t vm_addr,
                            const void *buf, size_t size, Status &error) {
  struct ptrace_io_desc pi_desc;

  pi_desc.piod_op = PIOD_WRITE_D;
  pi_desc.piod_offs = (void *)vm_addr;
  pi_desc.piod_addr = (void *)buf;
  pi_desc.piod_len = size;

  if (PTRACE(PT_IO, pid, (caddr_t)&pi_desc, 0) < 0) {
    error.SetErrorToErrno();
    return 0;
  }
  return pi_desc.piod_len;
}

// Simple helper function to ensure flags are enabled on the given file
// descriptor.
static bool EnsureFDFlags(int fd, int flags, Status &error) {
  int status;

  if ((status = fcntl(fd, F_GETFL)) == -1) {
    error.SetErrorToErrno();
    return false;
  }

  if (fcntl(fd, F_SETFL, status | flags) == -1) {
    error.SetErrorToErrno();
    return false;
  }

  return true;
}

//------------------------------------------------------------------------------
/// @class Operation
/// Represents a ProcessMonitor operation.
///
/// Under FreeBSD, it is not possible to ptrace() from any other thread but
/// the one that spawned or attached to the process from the start.
/// Therefore, when a ProcessMonitor is asked to deliver or change the state
/// of an inferior process the operation must be "funneled" to a specific
/// thread to perform the task.  The Operation class provides an abstract base
/// for all services the ProcessMonitor must perform via the single virtual
/// function Execute, thus encapsulating the code that needs to run in the
/// privileged context.
class Operation {
public:
  virtual ~Operation() {}
  virtual void Execute(ProcessMonitor *monitor) = 0;
};

//------------------------------------------------------------------------------
/// @class ReadOperation
/// Implements ProcessMonitor::ReadMemory.
class ReadOperation : public Operation {
public:
  ReadOperation(lldb::addr_t addr, void *buff, size_t size, Status &error,
                size_t &result)
      : m_addr(addr), m_buff(buff), m_size(size), m_error(error),
        m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::addr_t m_addr;
  void *m_buff;
  size_t m_size;
  Status &m_error;
  size_t &m_result;
};

void ReadOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();

  m_result = DoReadMemory(pid, m_addr, m_buff, m_size, m_error);
}

//------------------------------------------------------------------------------
/// @class WriteOperation
/// Implements ProcessMonitor::WriteMemory.
class WriteOperation : public Operation {
public:
  WriteOperation(lldb::addr_t addr, const void *buff, size_t size,
                 Status &error, size_t &result)
      : m_addr(addr), m_buff(buff), m_size(size), m_error(error),
        m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::addr_t m_addr;
  const void *m_buff;
  size_t m_size;
  Status &m_error;
  size_t &m_result;
};

void WriteOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();

  m_result = DoWriteMemory(pid, m_addr, m_buff, m_size, m_error);
}

//------------------------------------------------------------------------------
/// @class ReadRegOperation
/// Implements ProcessMonitor::ReadRegisterValue.
class ReadRegOperation : public Operation {
public:
  ReadRegOperation(lldb::tid_t tid, unsigned offset, unsigned size,
                   RegisterValue &value, bool &result)
      : m_tid(tid), m_offset(offset), m_size(size), m_value(value),
        m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  unsigned m_offset;
  unsigned m_size;
  RegisterValue &m_value;
  bool &m_result;
};

void ReadRegOperation::Execute(ProcessMonitor *monitor) {
  struct reg regs;
  int rc;

  if ((rc = PTRACE(PT_GETREGS, m_tid, (caddr_t)&regs, 0)) < 0) {
    m_result = false;
  } else {
    // 'struct reg' contains only 32- or 64-bit register values.  Punt on
    // others.  Also, not all entries may be uintptr_t sized, such as 32-bit
    // processes on powerpc64 (probably the same for i386 on amd64)
    if (m_size == sizeof(uint32_t))
      m_value = *(uint32_t *)(((caddr_t)&regs) + m_offset);
    else if (m_size == sizeof(uint64_t))
      m_value = *(uint64_t *)(((caddr_t)&regs) + m_offset);
    else
      memcpy((void *)&m_value, (((caddr_t)&regs) + m_offset), m_size);
    m_result = true;
  }
}

//------------------------------------------------------------------------------
/// @class WriteRegOperation
/// Implements ProcessMonitor::WriteRegisterValue.
class WriteRegOperation : public Operation {
public:
  WriteRegOperation(lldb::tid_t tid, unsigned offset,
                    const RegisterValue &value, bool &result)
      : m_tid(tid), m_offset(offset), m_value(value), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  unsigned m_offset;
  const RegisterValue &m_value;
  bool &m_result;
};

void WriteRegOperation::Execute(ProcessMonitor *monitor) {
  struct reg regs;

  if (PTRACE(PT_GETREGS, m_tid, (caddr_t)&regs, 0) < 0) {
    m_result = false;
    return;
  }
  *(uintptr_t *)(((caddr_t)&regs) + m_offset) =
      (uintptr_t)m_value.GetAsUInt64();
  if (PTRACE(PT_SETREGS, m_tid, (caddr_t)&regs, 0) < 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class ReadDebugRegOperation
/// Implements ProcessMonitor::ReadDebugRegisterValue.
class ReadDebugRegOperation : public Operation {
public:
  ReadDebugRegOperation(lldb::tid_t tid, unsigned offset, unsigned size,
                        RegisterValue &value, bool &result)
      : m_tid(tid), m_offset(offset), m_size(size), m_value(value),
        m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  unsigned m_offset;
  unsigned m_size;
  RegisterValue &m_value;
  bool &m_result;
};

void ReadDebugRegOperation::Execute(ProcessMonitor *monitor) {
  struct dbreg regs;
  int rc;

  if ((rc = PTRACE(PT_GETDBREGS, m_tid, (caddr_t)&regs, 0)) < 0) {
    m_result = false;
  } else {
    if (m_size == sizeof(uintptr_t))
      m_value = *(uintptr_t *)(((caddr_t)&regs) + m_offset);
    else
      memcpy((void *)&m_value, (((caddr_t)&regs) + m_offset), m_size);
    m_result = true;
  }
}

//------------------------------------------------------------------------------
/// @class WriteDebugRegOperation
/// Implements ProcessMonitor::WriteDebugRegisterValue.
class WriteDebugRegOperation : public Operation {
public:
  WriteDebugRegOperation(lldb::tid_t tid, unsigned offset,
                         const RegisterValue &value, bool &result)
      : m_tid(tid), m_offset(offset), m_value(value), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  unsigned m_offset;
  const RegisterValue &m_value;
  bool &m_result;
};

void WriteDebugRegOperation::Execute(ProcessMonitor *monitor) {
  struct dbreg regs;

  if (PTRACE(PT_GETDBREGS, m_tid, (caddr_t)&regs, 0) < 0) {
    m_result = false;
    return;
  }
  *(uintptr_t *)(((caddr_t)&regs) + m_offset) =
      (uintptr_t)m_value.GetAsUInt64();
  if (PTRACE(PT_SETDBREGS, m_tid, (caddr_t)&regs, 0) < 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class ReadGPROperation
/// Implements ProcessMonitor::ReadGPR.
class ReadGPROperation : public Operation {
public:
  ReadGPROperation(lldb::tid_t tid, void *buf, bool &result)
      : m_tid(tid), m_buf(buf), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  void *m_buf;
  bool &m_result;
};

void ReadGPROperation::Execute(ProcessMonitor *monitor) {
  int rc;

  errno = 0;
  rc = PTRACE(PT_GETREGS, m_tid, (caddr_t)m_buf, 0);
  if (errno != 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class ReadFPROperation
/// Implements ProcessMonitor::ReadFPR.
class ReadFPROperation : public Operation {
public:
  ReadFPROperation(lldb::tid_t tid, void *buf, bool &result)
      : m_tid(tid), m_buf(buf), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  void *m_buf;
  bool &m_result;
};

void ReadFPROperation::Execute(ProcessMonitor *monitor) {
  if (PTRACE(PT_GETFPREGS, m_tid, (caddr_t)m_buf, 0) < 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class WriteGPROperation
/// Implements ProcessMonitor::WriteGPR.
class WriteGPROperation : public Operation {
public:
  WriteGPROperation(lldb::tid_t tid, void *buf, bool &result)
      : m_tid(tid), m_buf(buf), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  void *m_buf;
  bool &m_result;
};

void WriteGPROperation::Execute(ProcessMonitor *monitor) {
  if (PTRACE(PT_SETREGS, m_tid, (caddr_t)m_buf, 0) < 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class WriteFPROperation
/// Implements ProcessMonitor::WriteFPR.
class WriteFPROperation : public Operation {
public:
  WriteFPROperation(lldb::tid_t tid, void *buf, bool &result)
      : m_tid(tid), m_buf(buf), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  void *m_buf;
  bool &m_result;
};

void WriteFPROperation::Execute(ProcessMonitor *monitor) {
  if (PTRACE(PT_SETFPREGS, m_tid, (caddr_t)m_buf, 0) < 0)
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class ResumeOperation
/// Implements ProcessMonitor::Resume.
class ResumeOperation : public Operation {
public:
  ResumeOperation(uint32_t signo, bool &result)
      : m_signo(signo), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  uint32_t m_signo;
  bool &m_result;
};

void ResumeOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();
  int data = 0;

  if (m_signo != LLDB_INVALID_SIGNAL_NUMBER)
    data = m_signo;

  if (PTRACE(PT_CONTINUE, pid, (caddr_t)1, data)) {
    Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));
    LLDB_LOG(log, "ResumeOperation ({0}) failed: {1}", pid,
             llvm::sys::StrError(errno));
    m_result = false;
  } else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class SingleStepOperation
/// Implements ProcessMonitor::SingleStep.
class SingleStepOperation : public Operation {
public:
  SingleStepOperation(uint32_t signo, bool &result)
      : m_signo(signo), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  uint32_t m_signo;
  bool &m_result;
};

void SingleStepOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();
  int data = 0;

  if (m_signo != LLDB_INVALID_SIGNAL_NUMBER)
    data = m_signo;

  if (PTRACE(PT_STEP, pid, NULL, data))
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class LwpInfoOperation
/// Implements ProcessMonitor::GetLwpInfo.
class LwpInfoOperation : public Operation {
public:
  LwpInfoOperation(lldb::tid_t tid, void *info, bool &result, int &ptrace_err)
      : m_tid(tid), m_info(info), m_result(result), m_err(ptrace_err) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  void *m_info;
  bool &m_result;
  int &m_err;
};

void LwpInfoOperation::Execute(ProcessMonitor *monitor) {
  struct ptrace_lwpinfo plwp;

  if (PTRACE(PT_LWPINFO, m_tid, (caddr_t)&plwp, sizeof(plwp))) {
    m_result = false;
    m_err = errno;
  } else {
    memcpy(m_info, &plwp, sizeof(plwp));
    m_result = true;
  }
}

//------------------------------------------------------------------------------
/// @class ThreadSuspendOperation
/// Implements ProcessMonitor::ThreadSuspend.
class ThreadSuspendOperation : public Operation {
public:
  ThreadSuspendOperation(lldb::tid_t tid, bool suspend, bool &result)
      : m_tid(tid), m_suspend(suspend), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  bool m_suspend;
  bool &m_result;
};

void ThreadSuspendOperation::Execute(ProcessMonitor *monitor) {
  m_result = !PTRACE(m_suspend ? PT_SUSPEND : PT_RESUME, m_tid, NULL, 0);
}

//------------------------------------------------------------------------------
/// @class EventMessageOperation
/// Implements ProcessMonitor::GetEventMessage.
class EventMessageOperation : public Operation {
public:
  EventMessageOperation(lldb::tid_t tid, unsigned long *message, bool &result)
      : m_tid(tid), m_message(message), m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  lldb::tid_t m_tid;
  unsigned long *m_message;
  bool &m_result;
};

void EventMessageOperation::Execute(ProcessMonitor *monitor) {
  struct ptrace_lwpinfo plwp;

  if (PTRACE(PT_LWPINFO, m_tid, (caddr_t)&plwp, sizeof(plwp)))
    m_result = false;
  else {
    if (plwp.pl_flags & PL_FLAG_FORKED) {
      *m_message = plwp.pl_child_pid;
      m_result = true;
    } else
      m_result = false;
  }
}

//------------------------------------------------------------------------------
/// @class KillOperation
/// Implements ProcessMonitor::Kill.
class KillOperation : public Operation {
public:
  KillOperation(bool &result) : m_result(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  bool &m_result;
};

void KillOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();

  if (PTRACE(PT_KILL, pid, NULL, 0))
    m_result = false;
  else
    m_result = true;
}

//------------------------------------------------------------------------------
/// @class DetachOperation
/// Implements ProcessMonitor::Detach.
class DetachOperation : public Operation {
public:
  DetachOperation(Status &result) : m_error(result) {}

  void Execute(ProcessMonitor *monitor);

private:
  Status &m_error;
};

void DetachOperation::Execute(ProcessMonitor *monitor) {
  lldb::pid_t pid = monitor->GetPID();

  if (PTRACE(PT_DETACH, pid, NULL, 0) < 0)
    m_error.SetErrorToErrno();
}

ProcessMonitor::OperationArgs::OperationArgs(ProcessMonitor *monitor)
    : m_monitor(monitor) {
  sem_init(&m_semaphore, 0, 0);
}

ProcessMonitor::OperationArgs::~OperationArgs() { sem_destroy(&m_semaphore); }

ProcessMonitor::LaunchArgs::LaunchArgs(ProcessMonitor *monitor,
                                       lldb_private::Module *module,
                                       char const **argv, Environment env,
                                       const FileSpec &stdin_file_spec,
                                       const FileSpec &stdout_file_spec,
                                       const FileSpec &stderr_file_spec,
                                       const FileSpec &working_dir)
    : OperationArgs(monitor), m_module(module), m_argv(argv),
      m_env(std::move(env)), m_stdin_file_spec(stdin_file_spec),
      m_stdout_file_spec(stdout_file_spec),
      m_stderr_file_spec(stderr_file_spec), m_working_dir(working_dir) {}

ProcessMonitor::LaunchArgs::~LaunchArgs() {}

ProcessMonitor::AttachArgs::AttachArgs(ProcessMonitor *monitor, lldb::pid_t pid)
    : OperationArgs(monitor), m_pid(pid) {}

ProcessMonitor::AttachArgs::~AttachArgs() {}

//------------------------------------------------------------------------------
/// The basic design of the ProcessMonitor is built around two threads.
///
/// One thread (@see SignalThread) simply blocks on a call to waitpid()
/// looking for changes in the debugee state.  When a change is detected a
/// ProcessMessage is sent to the associated ProcessFreeBSD instance.  This
/// thread "drives" state changes in the debugger.
///
/// The second thread (@see OperationThread) is responsible for two things 1)
/// launching or attaching to the inferior process, and then 2) servicing
/// operations such as register reads/writes, stepping, etc.  See the comments
/// on the Operation class for more info as to why this is needed.
ProcessMonitor::ProcessMonitor(
    ProcessFreeBSD *process, Module *module, const char *argv[],
    Environment env, const FileSpec &stdin_file_spec,
    const FileSpec &stdout_file_spec, const FileSpec &stderr_file_spec,
    const FileSpec &working_dir,
    const lldb_private::ProcessLaunchInfo & /* launch_info */,
    lldb_private::Status &error)
    : m_process(static_cast<ProcessFreeBSD *>(process)),
      m_pid(LLDB_INVALID_PROCESS_ID), m_terminal_fd(-1), m_operation(0) {
  using namespace std::placeholders;

  std::unique_ptr<LaunchArgs> args(
      new LaunchArgs(this, module, argv, std::move(env), stdin_file_spec,
                     stdout_file_spec, stderr_file_spec, working_dir));

  sem_init(&m_operation_pending, 0, 0);
  sem_init(&m_operation_done, 0, 0);

  StartLaunchOpThread(args.get(), error);
  if (!error.Success())
    return;

  if (llvm::sys::RetryAfterSignal(-1, sem_wait, &args->m_semaphore) == -1) {
    error.SetErrorToErrno();
    return;
  }

  // Check that the launch was a success.
  if (!args->m_error.Success()) {
    StopOpThread();
    error = args->m_error;
    return;
  }

  // Finally, start monitoring the child process for change in state.
  m_monitor_thread = Host::StartMonitoringChildProcess(
      std::bind(&ProcessMonitor::MonitorCallback, this, _1, _2, _3, _4),
      GetPID(), true);
  if (!m_monitor_thread.IsJoinable()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Process launch failed.");
    return;
  }
}

ProcessMonitor::ProcessMonitor(ProcessFreeBSD *process, lldb::pid_t pid,
                               lldb_private::Status &error)
    : m_process(static_cast<ProcessFreeBSD *>(process)), m_pid(pid),
      m_terminal_fd(-1), m_operation(0) {
  using namespace std::placeholders;

  sem_init(&m_operation_pending, 0, 0);
  sem_init(&m_operation_done, 0, 0);

  std::unique_ptr<AttachArgs> args(new AttachArgs(this, pid));

  StartAttachOpThread(args.get(), error);
  if (!error.Success())
    return;

  if (llvm::sys::RetryAfterSignal(-1, sem_wait, &args->m_semaphore) == -1) {
    error.SetErrorToErrno();
    return;
  }

  // Check that the attach was a success.
  if (!args->m_error.Success()) {
    StopOpThread();
    error = args->m_error;
    return;
  }

  // Finally, start monitoring the child process for change in state.
  m_monitor_thread = Host::StartMonitoringChildProcess(
      std::bind(&ProcessMonitor::MonitorCallback, this, _1, _2, _3, _4),
      GetPID(), true);
  if (!m_monitor_thread.IsJoinable()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Process attach failed.");
    return;
  }
}

ProcessMonitor::~ProcessMonitor() { StopMonitor(); }

//------------------------------------------------------------------------------
// Thread setup and tear down.
void ProcessMonitor::StartLaunchOpThread(LaunchArgs *args, Status &error) {
  static const char *g_thread_name = "lldb.process.freebsd.operation";

  if (m_operation_thread.IsJoinable())
    return;

  m_operation_thread =
      ThreadLauncher::LaunchThread(g_thread_name, LaunchOpThread, args, &error);
}

void *ProcessMonitor::LaunchOpThread(void *arg) {
  LaunchArgs *args = static_cast<LaunchArgs *>(arg);

  if (!Launch(args)) {
    sem_post(&args->m_semaphore);
    return NULL;
  }

  ServeOperation(args);
  return NULL;
}

bool ProcessMonitor::Launch(LaunchArgs *args) {
  ProcessMonitor *monitor = args->m_monitor;
  ProcessFreeBSD &process = monitor->GetProcess();
  const char **argv = args->m_argv;
  const FileSpec &stdin_file_spec = args->m_stdin_file_spec;
  const FileSpec &stdout_file_spec = args->m_stdout_file_spec;
  const FileSpec &stderr_file_spec = args->m_stderr_file_spec;
  const FileSpec &working_dir = args->m_working_dir;

  PseudoTerminal terminal;
  const size_t err_len = 1024;
  char err_str[err_len];
  ::pid_t pid;

  // Propagate the environment if one is not supplied.
  Environment::Envp envp =
      (args->m_env.empty() ? Host::GetEnvironment() : args->m_env).getEnvp();

  if ((pid = terminal.Fork(err_str, err_len)) == -1) {
    args->m_error.SetErrorToGenericError();
    args->m_error.SetErrorString("Process fork failed.");
    goto FINISH;
  }

  // Recognized child exit status codes.
  enum {
    ePtraceFailed = 1,
    eDupStdinFailed,
    eDupStdoutFailed,
    eDupStderrFailed,
    eChdirFailed,
    eExecFailed,
    eSetGidFailed
  };

  // Child process.
  if (pid == 0) {
    // Trace this process.
    if (PTRACE(PT_TRACE_ME, 0, NULL, 0) < 0)
      exit(ePtraceFailed);

    // terminal has already dupped the tty descriptors to stdin/out/err. This
    // closes original fd from which they were copied (and avoids leaking
    // descriptors to the debugged process.
    terminal.CloseSlaveFileDescriptor();

    // Do not inherit setgid powers.
    if (setgid(getgid()) != 0)
      exit(eSetGidFailed);

    // Let us have our own process group.
    setpgid(0, 0);

    // Dup file descriptors if needed.
    //
    // FIXME: If two or more of the paths are the same we needlessly open
    // the same file multiple times.
    if (stdin_file_spec)
      if (!DupDescriptor(stdin_file_spec, STDIN_FILENO, O_RDONLY))
        exit(eDupStdinFailed);

    if (stdout_file_spec)
      if (!DupDescriptor(stdout_file_spec, STDOUT_FILENO, O_WRONLY | O_CREAT))
        exit(eDupStdoutFailed);

    if (stderr_file_spec)
      if (!DupDescriptor(stderr_file_spec, STDERR_FILENO, O_WRONLY | O_CREAT))
        exit(eDupStderrFailed);

    // Change working directory
    if (working_dir && 0 != ::chdir(working_dir.GetCString()))
      exit(eChdirFailed);

    // Execute.  We should never return.
    execve(argv[0], const_cast<char *const *>(argv), envp);
    exit(eExecFailed);
  }

  // Wait for the child process to to trap on its call to execve.
  ::pid_t wpid;
  int status;
  if ((wpid = waitpid(pid, &status, 0)) < 0) {
    args->m_error.SetErrorToErrno();
    goto FINISH;
  } else if (WIFEXITED(status)) {
    // open, dup or execve likely failed for some reason.
    args->m_error.SetErrorToGenericError();
    switch (WEXITSTATUS(status)) {
    case ePtraceFailed:
      args->m_error.SetErrorString("Child ptrace failed.");
      break;
    case eDupStdinFailed:
      args->m_error.SetErrorString("Child open stdin failed.");
      break;
    case eDupStdoutFailed:
      args->m_error.SetErrorString("Child open stdout failed.");
      break;
    case eDupStderrFailed:
      args->m_error.SetErrorString("Child open stderr failed.");
      break;
    case eChdirFailed:
      args->m_error.SetErrorString("Child failed to set working directory.");
      break;
    case eExecFailed:
      args->m_error.SetErrorString("Child exec failed.");
      break;
    case eSetGidFailed:
      args->m_error.SetErrorString("Child setgid failed.");
      break;
    default:
      args->m_error.SetErrorString("Child returned unknown exit status.");
      break;
    }
    goto FINISH;
  }
  assert(WIFSTOPPED(status) && wpid == (::pid_t)pid &&
         "Could not sync with inferior process.");

#ifdef notyet
  // Have the child raise an event on exit.  This is used to keep the child in
  // limbo until it is destroyed.
  if (PTRACE(PTRACE_SETOPTIONS, pid, NULL, PTRACE_O_TRACEEXIT) < 0) {
    args->m_error.SetErrorToErrno();
    goto FINISH;
  }
#endif
  // Release the master terminal descriptor and pass it off to the
  // ProcessMonitor instance.  Similarly stash the inferior pid.
  monitor->m_terminal_fd = terminal.ReleaseMasterFileDescriptor();
  monitor->m_pid = pid;

  // Set the terminal fd to be in non blocking mode (it simplifies the
  // implementation of ProcessFreeBSD::GetSTDOUT to have a non-blocking
  // descriptor to read from).
  if (!EnsureFDFlags(monitor->m_terminal_fd, O_NONBLOCK, args->m_error))
    goto FINISH;

  process.SendMessage(ProcessMessage::Attach(pid));

FINISH:
  return args->m_error.Success();
}

void ProcessMonitor::StartAttachOpThread(AttachArgs *args,
                                         lldb_private::Status &error) {
  static const char *g_thread_name = "lldb.process.freebsd.operation";

  if (m_operation_thread.IsJoinable())
    return;

  m_operation_thread =
      ThreadLauncher::LaunchThread(g_thread_name, AttachOpThread, args, &error);
}

void *ProcessMonitor::AttachOpThread(void *arg) {
  AttachArgs *args = static_cast<AttachArgs *>(arg);

  Attach(args);

  ServeOperation(args);
  return NULL;
}

void ProcessMonitor::Attach(AttachArgs *args) {
  lldb::pid_t pid = args->m_pid;

  ProcessMonitor *monitor = args->m_monitor;
  ProcessFreeBSD &process = monitor->GetProcess();

  if (pid <= 1) {
    args->m_error.SetErrorToGenericError();
    args->m_error.SetErrorString("Attaching to process 1 is not allowed.");
    return;
  }

  // Attach to the requested process.
  if (PTRACE(PT_ATTACH, pid, NULL, 0) < 0) {
    args->m_error.SetErrorToErrno();
    return;
  }

  int status;
  if ((status = waitpid(pid, NULL, 0)) < 0) {
    args->m_error.SetErrorToErrno();
    return;
  }

  process.SendMessage(ProcessMessage::Attach(pid));
}

size_t
ProcessMonitor::GetCurrentThreadIDs(std::vector<lldb::tid_t> &thread_ids) {
  lwpid_t *tids;
  int tdcnt;

  thread_ids.clear();

  tdcnt = PTRACE(PT_GETNUMLWPS, m_pid, NULL, 0);
  if (tdcnt <= 0)
    return 0;
  tids = (lwpid_t *)malloc(tdcnt * sizeof(*tids));
  if (tids == NULL)
    return 0;
  if (PTRACE(PT_GETLWPLIST, m_pid, (void *)tids, tdcnt) < 0) {
    free(tids);
    return 0;
  }
  thread_ids = std::vector<lldb::tid_t>(tids, tids + tdcnt);
  free(tids);
  return thread_ids.size();
}

bool ProcessMonitor::MonitorCallback(ProcessMonitor *monitor, lldb::pid_t pid,
                                     bool exited, int signal, int status) {
  ProcessMessage message;
  ProcessFreeBSD *process = monitor->m_process;
  assert(process);
  bool stop_monitoring;
  struct ptrace_lwpinfo plwp;
  int ptrace_err;

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));

  if (exited) {
    if (log)
      log->Printf("ProcessMonitor::%s() got exit signal, tid = %" PRIu64,
                  __FUNCTION__, pid);
    message = ProcessMessage::Exit(pid, status);
    process->SendMessage(message);
    return pid == process->GetID();
  }

  if (!monitor->GetLwpInfo(pid, &plwp, ptrace_err))
    stop_monitoring = true; // pid is gone.  Bail.
  else {
    switch (plwp.pl_siginfo.si_signo) {
    case SIGTRAP:
      message = MonitorSIGTRAP(monitor, &plwp.pl_siginfo, plwp.pl_lwpid);
      break;

    default:
      message = MonitorSignal(monitor, &plwp.pl_siginfo, plwp.pl_lwpid);
      break;
    }

    process->SendMessage(message);
    stop_monitoring = message.GetKind() == ProcessMessage::eExitMessage;
  }

  return stop_monitoring;
}

ProcessMessage ProcessMonitor::MonitorSIGTRAP(ProcessMonitor *monitor,
                                              const siginfo_t *info,
                                              lldb::tid_t tid) {
  ProcessMessage message;

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));

  assert(monitor);
  assert(info && info->si_signo == SIGTRAP && "Unexpected child signal!");

  switch (info->si_code) {
  default:
    assert(false && "Unexpected SIGTRAP code!");
    break;

  case (SIGTRAP /* | (PTRACE_EVENT_EXIT << 8) */): {
    // The inferior process is about to exit.  Maintain the process in a state
    // of "limbo" until we are explicitly commanded to detach, destroy, resume,
    // etc.
    unsigned long data = 0;
    if (!monitor->GetEventMessage(tid, &data))
      data = -1;
    if (log)
      log->Printf("ProcessMonitor::%s() received exit? event, data = %lx, tid "
                  "= %" PRIu64,
                  __FUNCTION__, data, tid);
    message = ProcessMessage::Limbo(tid, (data >> 8));
    break;
  }

  case 0:
  case TRAP_TRACE:
#ifdef TRAP_CAP
  // Map TRAP_CAP to a trace trap in the absense of a more specific handler.
  case TRAP_CAP:
#endif
    if (log)
      log->Printf("ProcessMonitor::%s() received trace event, tid = %" PRIu64
                  "  : si_code = %d",
                  __FUNCTION__, tid, info->si_code);
    message = ProcessMessage::Trace(tid);
    break;

  case SI_KERNEL:
  case TRAP_BRKPT:
    if (monitor->m_process->IsSoftwareStepBreakpoint(tid)) {
      if (log)
        log->Printf("ProcessMonitor::%s() received sw single step breakpoint "
                    "event, tid = %" PRIu64,
                    __FUNCTION__, tid);
      message = ProcessMessage::Trace(tid);
    } else {
      if (log)
        log->Printf(
            "ProcessMonitor::%s() received breakpoint event, tid = %" PRIu64,
            __FUNCTION__, tid);
      message = ProcessMessage::Break(tid);
    }
    break;
  }

  return message;
}

ProcessMessage ProcessMonitor::MonitorSignal(ProcessMonitor *monitor,
                                             const siginfo_t *info,
                                             lldb::tid_t tid) {
  ProcessMessage message;
  int signo = info->si_signo;

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));

  // POSIX says that process behaviour is undefined after it ignores a SIGFPE,
  // SIGILL, SIGSEGV, or SIGBUS *unless* that signal was generated by a kill(2)
  // or raise(3).  Similarly for tgkill(2) on FreeBSD.
  //
  // IOW, user generated signals never generate what we consider to be a
  // "crash".
  //
  // Similarly, ACK signals generated by this monitor.
  if (info->si_code == SI_USER) {
    if (log)
      log->Printf(
          "ProcessMonitor::%s() received signal %s with code %s, pid = %d",
          __FUNCTION__,
          monitor->m_process->GetUnixSignals()->GetSignalAsCString(signo),
          "SI_USER", info->si_pid);
    if (info->si_pid == getpid())
      return ProcessMessage::SignalDelivered(tid, signo);
    else
      return ProcessMessage::Signal(tid, signo);
  }

  if (log)
    log->Printf(
        "ProcessMonitor::%s() received signal %s", __FUNCTION__,
        monitor->m_process->GetUnixSignals()->GetSignalAsCString(signo));

  switch (signo) {
  case SIGSEGV:
  case SIGILL:
  case SIGFPE:
  case SIGBUS:
    lldb::addr_t fault_addr = reinterpret_cast<lldb::addr_t>(info->si_addr);
    const auto reason = GetCrashReason(*info);
    if (reason != CrashReason::eInvalidCrashReason) {
      return ProcessMessage::Crash(tid, reason, signo, fault_addr);
    } // else; Use atleast si_signo info for other si_code
  }

  // Everything else is "normal" and does not require any special action on our
  // part.
  return ProcessMessage::Signal(tid, signo);
}

void ProcessMonitor::ServeOperation(OperationArgs *args) {
  ProcessMonitor *monitor = args->m_monitor;

  // We are finised with the arguments and are ready to go.  Sync with the
  // parent thread and start serving operations on the inferior.
  sem_post(&args->m_semaphore);

  for (;;) {
    // wait for next pending operation
    sem_wait(&monitor->m_operation_pending);

    monitor->m_operation->Execute(monitor);

    // notify calling thread that operation is complete
    sem_post(&monitor->m_operation_done);
  }
}

void ProcessMonitor::DoOperation(Operation *op) {
  std::lock_guard<std::mutex> guard(m_operation_mutex);

  m_operation = op;

  // notify operation thread that an operation is ready to be processed
  sem_post(&m_operation_pending);

  // wait for operation to complete
  sem_wait(&m_operation_done);
}

size_t ProcessMonitor::ReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                                  Status &error) {
  size_t result;
  ReadOperation op(vm_addr, buf, size, error, result);
  DoOperation(&op);
  return result;
}

size_t ProcessMonitor::WriteMemory(lldb::addr_t vm_addr, const void *buf,
                                   size_t size, lldb_private::Status &error) {
  size_t result;
  WriteOperation op(vm_addr, buf, size, error, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ReadRegisterValue(lldb::tid_t tid, unsigned offset,
                                       const char *reg_name, unsigned size,
                                       RegisterValue &value) {
  bool result;
  ReadRegOperation op(tid, offset, size, value, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::WriteRegisterValue(lldb::tid_t tid, unsigned offset,
                                        const char *reg_name,
                                        const RegisterValue &value) {
  bool result;
  WriteRegOperation op(tid, offset, value, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ReadDebugRegisterValue(
    lldb::tid_t tid, unsigned offset, const char *reg_name, unsigned size,
    lldb_private::RegisterValue &value) {
  bool result;
  ReadDebugRegOperation op(tid, offset, size, value, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::WriteDebugRegisterValue(
    lldb::tid_t tid, unsigned offset, const char *reg_name,
    const lldb_private::RegisterValue &value) {
  bool result;
  WriteDebugRegOperation op(tid, offset, value, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ReadGPR(lldb::tid_t tid, void *buf, size_t buf_size) {
  bool result;
  ReadGPROperation op(tid, buf, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ReadFPR(lldb::tid_t tid, void *buf, size_t buf_size) {
  bool result;
  ReadFPROperation op(tid, buf, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ReadRegisterSet(lldb::tid_t tid, void *buf,
                                     size_t buf_size, unsigned int regset) {
  return false;
}

bool ProcessMonitor::WriteGPR(lldb::tid_t tid, void *buf, size_t buf_size) {
  bool result;
  WriteGPROperation op(tid, buf, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::WriteFPR(lldb::tid_t tid, void *buf, size_t buf_size) {
  bool result;
  WriteFPROperation op(tid, buf, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::WriteRegisterSet(lldb::tid_t tid, void *buf,
                                      size_t buf_size, unsigned int regset) {
  return false;
}

bool ProcessMonitor::ReadThreadPointer(lldb::tid_t tid, lldb::addr_t &value) {
  return false;
}

bool ProcessMonitor::Resume(lldb::tid_t unused, uint32_t signo) {
  bool result;
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));

  if (log) {
    const char *signame =
        m_process->GetUnixSignals()->GetSignalAsCString(signo);
    if (signame == nullptr)
      signame = "<none>";
    log->Printf("ProcessMonitor::%s() resuming pid %" PRIu64 " with signal %s",
                __FUNCTION__, GetPID(), signame);
  }
  ResumeOperation op(signo, result);
  DoOperation(&op);
  if (log)
    log->Printf("ProcessMonitor::%s() resuming result = %s", __FUNCTION__,
                result ? "true" : "false");
  return result;
}

bool ProcessMonitor::SingleStep(lldb::tid_t unused, uint32_t signo) {
  bool result;
  SingleStepOperation op(signo, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::Kill() {
  bool result;
  KillOperation op(result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::GetLwpInfo(lldb::tid_t tid, void *lwpinfo,
                                int &ptrace_err) {
  bool result;
  LwpInfoOperation op(tid, lwpinfo, result, ptrace_err);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::ThreadSuspend(lldb::tid_t tid, bool suspend) {
  bool result;
  ThreadSuspendOperation op(tid, suspend, result);
  DoOperation(&op);
  return result;
}

bool ProcessMonitor::GetEventMessage(lldb::tid_t tid, unsigned long *message) {
  bool result;
  EventMessageOperation op(tid, message, result);
  DoOperation(&op);
  return result;
}

lldb_private::Status ProcessMonitor::Detach(lldb::tid_t tid) {
  lldb_private::Status error;
  if (tid != LLDB_INVALID_THREAD_ID) {
    DetachOperation op(error);
    DoOperation(&op);
  }
  return error;
}

bool ProcessMonitor::DupDescriptor(const FileSpec &file_spec, int fd,
                                   int flags) {
  int target_fd = open(file_spec.GetCString(), flags, 0666);

  if (target_fd == -1)
    return false;

  if (dup2(target_fd, fd) == -1)
    return false;

  return (close(target_fd) == -1) ? false : true;
}

void ProcessMonitor::StopMonitoringChildProcess() {
  if (m_monitor_thread.IsJoinable()) {
    m_monitor_thread.Cancel();
    m_monitor_thread.Join(nullptr);
    m_monitor_thread.Reset();
  }
}

void ProcessMonitor::StopMonitor() {
  StopMonitoringChildProcess();
  StopOpThread();
  sem_destroy(&m_operation_pending);
  sem_destroy(&m_operation_done);
  if (m_terminal_fd >= 0) {
    close(m_terminal_fd);
    m_terminal_fd = -1;
  }
}

// FIXME: On Linux, when a new thread is created, we receive to notifications,
// (1) a SIGTRAP|PTRACE_EVENT_CLONE from the main process thread with the child
// thread id as additional information, and (2) a SIGSTOP|SI_USER from the new
// child thread indicating that it has is stopped because we attached. We have
// no guarantee of the order in which these arrive, but we need both before we
// are ready to proceed.  We currently keep a list of threads which have sent
// the initial SIGSTOP|SI_USER event.  Then when we receive the
// SIGTRAP|PTRACE_EVENT_CLONE notification, if the initial stop has not
// occurred we call ProcessMonitor::WaitForInitialTIDStop() to wait for it.
//
// Right now, the above logic is in ProcessPOSIX, so we need a definition of
// this function in the FreeBSD ProcessMonitor implementation even if it isn't
// logically needed.
//
// We really should figure out what actually happens on FreeBSD and move the
// Linux-specific logic out of ProcessPOSIX as needed.

bool ProcessMonitor::WaitForInitialTIDStop(lldb::tid_t tid) { return true; }

void ProcessMonitor::StopOpThread() {
  if (!m_operation_thread.IsJoinable())
    return;

  m_operation_thread.Cancel();
  m_operation_thread.Join(nullptr);
  m_operation_thread.Reset();
}
