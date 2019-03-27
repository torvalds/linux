//===-- MachException.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/18/07.
//
//===----------------------------------------------------------------------===//

#include "MachException.h"

// C includes
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/types.h>

// C++ includes
#include <mutex>

// LLDB includes
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_darwin;

// Routine mach_exception_raise
extern "C" kern_return_t
catch_mach_exception_raise(mach_port_t exception_port, mach_port_t thread,
                           mach_port_t task, exception_type_t exception,
                           mach_exception_data_t code,
                           mach_msg_type_number_t codeCnt);

extern "C" kern_return_t catch_mach_exception_raise_state(
    mach_port_t exception_port, exception_type_t exception,
    const mach_exception_data_t code, mach_msg_type_number_t codeCnt,
    int *flavor, const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt, thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt);

// Routine mach_exception_raise_state_identity
extern "C" kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exception_port, mach_port_t thread, mach_port_t task,
    exception_type_t exception, mach_exception_data_t code,
    mach_msg_type_number_t codeCnt, int *flavor, thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt, thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt);

extern "C" boolean_t mach_exc_server(mach_msg_header_t *InHeadP,
                                     mach_msg_header_t *OutHeadP);

static MachException::Data *g_message = NULL;

extern "C" kern_return_t catch_mach_exception_raise_state(
    mach_port_t exc_port, exception_type_t exc_type,
    const mach_exception_data_t exc_data, mach_msg_type_number_t exc_data_count,
    int *flavor, const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt, thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt) {
  // TODO change to LIBLLDB_LOG_EXCEPTION
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
  if (log) {
    log->Printf("::%s(exc_port = 0x%4.4x, exc_type = %d (%s), "
                "exc_data = 0x%llx, exc_data_count = %d)",
                __FUNCTION__, exc_port, exc_type, MachException::Name(exc_type),
                (uint64_t)exc_data, exc_data_count);
  }
  return KERN_FAILURE;
}

extern "C" kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exc_port, mach_port_t thread_port, mach_port_t task_port,
    exception_type_t exc_type, mach_exception_data_t exc_data,
    mach_msg_type_number_t exc_data_count, int *flavor,
    thread_state_t old_state, mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state, mach_msg_type_number_t *new_stateCnt) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
  if (log) {
    log->Printf("::%s(exc_port = 0x%4.4x, thd_port = 0x%4.4x, "
                "tsk_port = 0x%4.4x, exc_type = %d (%s), exc_data[%d] = "
                "{ 0x%llx, 0x%llx })",
                __FUNCTION__, exc_port, thread_port, task_port, exc_type,
                MachException::Name(exc_type), exc_data_count,
                (uint64_t)(exc_data_count > 0 ? exc_data[0] : 0xBADDBADD),
                (uint64_t)(exc_data_count > 1 ? exc_data[1] : 0xBADDBADD));
  }

  return KERN_FAILURE;
}

extern "C" kern_return_t
catch_mach_exception_raise(mach_port_t exc_port, mach_port_t thread_port,
                           mach_port_t task_port, exception_type_t exc_type,
                           mach_exception_data_t exc_data,
                           mach_msg_type_number_t exc_data_count) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
  if (log) {
    log->Printf("::%s(exc_port = 0x%4.4x, thd_port = 0x%4.4x, "
                "tsk_port = 0x%4.4x, exc_type = %d (%s), exc_data[%d] "
                "= { 0x%llx, 0x%llx })",
                __FUNCTION__, exc_port, thread_port, task_port, exc_type,
                MachException::Name(exc_type), exc_data_count,
                (uint64_t)(exc_data_count > 0 ? exc_data[0] : 0xBADDBADD),
                (uint64_t)(exc_data_count > 1 ? exc_data[1] : 0xBADDBADD));
  }

  if (task_port == g_message->task_port) {
    g_message->task_port = task_port;
    g_message->thread_port = thread_port;
    g_message->exc_type = exc_type;
    g_message->exc_data.resize(exc_data_count);
    ::memcpy(&g_message->exc_data[0], exc_data,
             g_message->exc_data.size() * sizeof(mach_exception_data_type_t));
    return KERN_SUCCESS;
  }
  return KERN_FAILURE;
}

bool MachException::Data::GetStopInfo(struct ThreadStopInfo *stop_info,
                                      const UnixSignals &signals,
                                      Stream &stream) const {
  if (!stop_info)
    return false;

  // Zero out the structure.
  memset(stop_info, 0, sizeof(struct ThreadStopInfo));

  if (exc_type == 0) {
    stop_info->reason = eStopReasonInvalid;
    return true;
  }

  // We always stop with a mach exception.
  stop_info->reason = eStopReasonException;
  // Save the EXC_XXXX exception type.
  stop_info->details.exception.type = exc_type;

  // Fill in a text description
  const char *exc_name = MachException::Name(exc_type);
  if (exc_name)
    stream.Printf("%s", exc_name);
  else
    stream.Printf("%i", exc_type);

  stop_info->details.exception.data_count = exc_data.size();

  int soft_signal = SoftSignal();
  if (soft_signal) {
    const char *sig_str = signals.GetSignalAsCString(soft_signal);
    stream.Printf(" EXC_SOFT_SIGNAL( %i ( %s ))", soft_signal,
                  sig_str ? sig_str : "unknown signal");
  } else {
    // No special disassembly for exception data, just print it.
    size_t idx;
    stream.Printf(" data[%llu] = {",
                  (uint64_t)stop_info->details.exception.data_count);

    for (idx = 0; idx < stop_info->details.exception.data_count; ++idx) {
      stream.Printf(
          "0x%llx%c", (uint64_t)exc_data[idx],
          ((idx + 1 == stop_info->details.exception.data_count) ? '}' : ','));
    }
  }

  // Copy the exception data
  for (size_t i = 0; i < stop_info->details.exception.data_count; i++)
    stop_info->details.exception.data[i] = exc_data[i];

  return true;
}

Status MachException::Message::Receive(mach_port_t port,
                                       mach_msg_option_t options,
                                       mach_msg_timeout_t timeout,
                                       mach_port_t notify_port) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  mach_msg_timeout_t mach_msg_timeout =
      options & MACH_RCV_TIMEOUT ? timeout : 0;
  if (log && ((options & MACH_RCV_TIMEOUT) == 0)) {
    // Dump this log message if we have no timeout in case it never returns
    log->Printf("::mach_msg(msg->{bits = %#x, size = %u remote_port = %#x, "
                "local_port = %#x, reserved = 0x%x, id = 0x%x}, "
                "option = %#x, send_size = 0, rcv_size = %llu, "
                "rcv_name = %#x, timeout = %u, notify = %#x)",
                exc_msg.hdr.msgh_bits, exc_msg.hdr.msgh_size,
                exc_msg.hdr.msgh_remote_port, exc_msg.hdr.msgh_local_port,
                exc_msg.hdr.msgh_reserved, exc_msg.hdr.msgh_id, options,
                (uint64_t)sizeof(exc_msg.data), port, mach_msg_timeout,
                notify_port);
  }

  mach_msg_return_t mach_err =
      ::mach_msg(&exc_msg.hdr,
                 options,              // options
                 0,                    // Send size
                 sizeof(exc_msg.data), // Receive size
                 port,                 // exception port to watch for
                                       // exception on
                 mach_msg_timeout,     // timeout in msec (obeyed only
                                       // if MACH_RCV_TIMEOUT is ORed
                                       // into the options parameter)
                 notify_port);
  error.SetError(mach_err, eErrorTypeMachKernel);

  // Dump any errors we get
  if (error.Fail() && log) {
    log->Printf("::mach_msg(msg->{bits = %#x, size = %u remote_port = %#x, "
                "local_port = %#x, reserved = 0x%x, id = 0x%x}, "
                "option = %#x, send_size = %u, rcv_size = %lu, rcv_name "
                "= %#x, timeout = %u, notify = %#x) failed: %s",
                exc_msg.hdr.msgh_bits, exc_msg.hdr.msgh_size,
                exc_msg.hdr.msgh_remote_port, exc_msg.hdr.msgh_local_port,
                exc_msg.hdr.msgh_reserved, exc_msg.hdr.msgh_id, options, 0,
                sizeof(exc_msg.data), port, mach_msg_timeout, notify_port,
                error.AsCString());
  }
  return error;
}

void MachException::Message::Dump(Stream &stream) const {
  stream.Printf("  exc_msg { bits = 0x%8.8x size = 0x%8.8x remote-port = "
                "0x%8.8x local-port = 0x%8.8x reserved = 0x%8.8x id = "
                "0x%8.8x }\n",
                exc_msg.hdr.msgh_bits, exc_msg.hdr.msgh_size,
                exc_msg.hdr.msgh_remote_port, exc_msg.hdr.msgh_local_port,
                exc_msg.hdr.msgh_reserved, exc_msg.hdr.msgh_id);

  stream.Printf("  reply_msg { bits = 0x%8.8x size = 0x%8.8x remote-port = "
                "0x%8.8x local-port = 0x%8.8x reserved = 0x%8.8x id = "
                "0x%8.8x }",
                reply_msg.hdr.msgh_bits, reply_msg.hdr.msgh_size,
                reply_msg.hdr.msgh_remote_port, reply_msg.hdr.msgh_local_port,
                reply_msg.hdr.msgh_reserved, reply_msg.hdr.msgh_id);
}

bool MachException::Message::CatchExceptionRaise(task_t task) {
  bool success = false;
  state.task_port = task;
  g_message = &state;
  // The exc_server function is the MIG generated server handling function to
  // handle messages from the kernel relating to the occurrence of an exception
  // in a thread. Such messages are delivered to the exception port set via
  // thread_set_exception_ports or task_set_exception_ports. When an exception
  // occurs in a thread, the thread sends an exception message to its exception
  // port, blocking in the kernel waiting for the receipt of a reply. The
  // exc_server function performs all necessary argument handling for this
  // kernel message and calls catch_exception_raise,
  // catch_exception_raise_state or catch_exception_raise_state_identity, which
  // should handle the exception. If the called routine returns KERN_SUCCESS, a
  // reply message will be sent, allowing the thread to continue from the point
  // of the exception; otherwise, no reply message is sent and the called
  // routine must have dealt with the exception thread directly.
  if (mach_exc_server(&exc_msg.hdr, &reply_msg.hdr)) {
    success = true;
  } else {
    Log *log(
        GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
    if (log)
      log->Printf("MachException::Message::%s(): mach_exc_server "
                  "returned zero...",
                  __FUNCTION__);
  }
  g_message = NULL;
  return success;
}

Status MachException::Message::Reply(::pid_t inferior_pid, task_t inferior_task,
                                     int signal) {
  // Reply to the exception...
  Status error;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  // If we had a soft signal, we need to update the thread first so it can
  // continue without signaling
  int soft_signal = state.SoftSignal();
  if (soft_signal) {
    int state_pid = -1;
    if (inferior_task == state.task_port) {
      // This is our task, so we can update the signal to send to it
      state_pid = inferior_pid;
      soft_signal = signal;
    } else {
      auto mach_err = ::pid_for_task(state.task_port, &state_pid);
      if (mach_err) {
        error.SetError(mach_err, eErrorTypeMachKernel);
        if (log)
          log->Printf("MachException::Message::%s(): pid_for_task() "
                      "failed: %s",
                      __FUNCTION__, error.AsCString());
        return error;
      }
    }

    lldbassert(state_pid != -1);
    if (state_pid != -1) {
      errno = 0;
      caddr_t thread_port_caddr = (caddr_t)(uintptr_t)state.thread_port;
      if (::ptrace(PT_THUPDATE, state_pid, thread_port_caddr, soft_signal) != 0)
        error.SetError(errno, eErrorTypePOSIX);

      if (!error.Success()) {
        if (log)
          log->Printf("::ptrace(request = PT_THUPDATE, pid = "
                      "0x%4.4x, tid = 0x%4.4x, signal = %i)",
                      state_pid, state.thread_port, soft_signal);
        return error;
      }
    }
  }

  if (log)
    log->Printf("::mach_msg ( msg->{bits = %#x, size = %u, remote_port "
                "= %#x, local_port = %#x, reserved = 0x%x, id = 0x%x}, "
                "option = %#x, send_size = %u, rcv_size = %u, rcv_name "
                "= %#x, timeout = %u, notify = %#x)",
                reply_msg.hdr.msgh_bits, reply_msg.hdr.msgh_size,
                reply_msg.hdr.msgh_remote_port, reply_msg.hdr.msgh_local_port,
                reply_msg.hdr.msgh_reserved, reply_msg.hdr.msgh_id,
                MACH_SEND_MSG | MACH_SEND_INTERRUPT, reply_msg.hdr.msgh_size, 0,
                MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

  auto mach_err =
      ::mach_msg(&reply_msg.hdr, MACH_SEND_MSG | MACH_SEND_INTERRUPT,
                 reply_msg.hdr.msgh_size, 0, MACH_PORT_NULL,
                 MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  if (mach_err)
    error.SetError(mach_err, eErrorTypeMachKernel);

  // Log our error if we have one.
  if (error.Fail() && log) {
    if (error.GetError() == MACH_SEND_INTERRUPTED) {
      log->PutCString("::mach_msg() - send interrupted");
      // TODO: keep retrying to reply???
    } else if (state.task_port == inferior_task) {
      log->Printf("mach_msg(): returned an error when replying "
                  "to a mach exception: error = %u (%s)",
                  error.GetError(), error.AsCString());
    } else {
      log->Printf("::mach_msg() - failed (child of task): %u (%s)",
                  error.GetError(), error.AsCString());
    }
  }

  return error;
}

#define PREV_EXC_MASK_ALL                                                      \
  (EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |      \
   EXC_MASK_EMULATION | EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT |              \
   EXC_MASK_SYSCALL | EXC_MASK_MACH_SYSCALL | EXC_MASK_RPC_ALERT |             \
   EXC_MASK_MACHINE)

// Don't listen for EXC_RESOURCE, it should really get handled by the system
// handler.

#ifndef EXC_RESOURCE
#define EXC_RESOURCE 11
#endif

#ifndef EXC_MASK_RESOURCE
#define EXC_MASK_RESOURCE (1 << EXC_RESOURCE)
#endif

#define LLDB_EXC_MASK (EXC_MASK_ALL & ~EXC_MASK_RESOURCE)

Status MachException::PortInfo::Save(task_t task) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  if (log)
    log->Printf("MachException::PortInfo::%s(task = 0x%4.4x)", __FUNCTION__,
                task);

  // Be careful to be able to have debugserver built on a newer OS than what it
  // is currently running on by being able to start with all exceptions and
  // back off to just what is supported on the current system
  mask = LLDB_EXC_MASK;

  count = (sizeof(ports) / sizeof(ports[0]));
  auto mach_err = ::task_get_exception_ports(task, mask, masks, &count, ports,
                                             behaviors, flavors);
  if (mach_err)
    error.SetError(mach_err, eErrorTypeMachKernel);

  if (log) {
    if (error.Success()) {
      log->Printf("::task_get_exception_ports(task = 0x%4.4x, mask = "
                  "0x%x, maskCnt => %u, ports, behaviors, flavors)",
                  task, mask, count);
    } else {
      log->Printf("::task_get_exception_ports(task = 0x%4.4x, mask = 0x%x, "
                  "maskCnt => %u, ports, behaviors, flavors) error: %u (%s)",
                  task, mask, count, error.GetError(), error.AsCString());
    }
  }

  if ((error.GetError() == KERN_INVALID_ARGUMENT) &&
      (mask != PREV_EXC_MASK_ALL)) {
    mask = PREV_EXC_MASK_ALL;
    count = (sizeof(ports) / sizeof(ports[0]));
    mach_err = ::task_get_exception_ports(task, mask, masks, &count, ports,
                                          behaviors, flavors);
    error.SetError(mach_err, eErrorTypeMachKernel);
    if (log) {
      if (error.Success()) {
        log->Printf("::task_get_exception_ports(task = 0x%4.4x, "
                    "mask = 0x%x, maskCnt => %u, ports, behaviors, "
                    "flavors)",
                    task, mask, count);
      } else {
        log->Printf("::task_get_exception_ports(task = 0x%4.4x, mask = "
                    "0x%x, maskCnt => %u, ports, behaviors, flavors) "
                    "error: %u (%s)",
                    task, mask, count, error.GetError(), error.AsCString());
      }
    }
  }
  if (error.Fail()) {
    mask = 0;
    count = 0;
  }
  return error;
}

Status MachException::PortInfo::Restore(task_t task) {
  Status error;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  if (log)
    log->Printf("MachException::PortInfo::Restore(task = 0x%4.4x)", task);

  uint32_t i = 0;
  if (count > 0) {
    for (i = 0; i < count; i++) {
      auto mach_err = ::task_set_exception_ports(task, masks[i], ports[i],
                                                 behaviors[i], flavors[i]);
      if (mach_err)
        error.SetError(mach_err, eErrorTypeMachKernel);
      if (log) {
        if (error.Success()) {
          log->Printf("::task_set_exception_ports(task = 0x%4.4x, "
                      "exception_mask = 0x%8.8x, new_port = 0x%4.4x, "
                      "behavior = 0x%8.8x, new_flavor = 0x%8.8x)",
                      task, masks[i], ports[i], behaviors[i], flavors[i]);
        } else {
          log->Printf("::task_set_exception_ports(task = 0x%4.4x, "
                      "exception_mask = 0x%8.8x, new_port = 0x%4.4x, "
                      "behavior = 0x%8.8x, new_flavor = 0x%8.8x): "
                      "error %u (%s)",
                      task, masks[i], ports[i], behaviors[i], flavors[i],
                      error.GetError(), error.AsCString());
        }
      }

      // Bail if we encounter any errors
      if (error.Fail())
        break;
    }
  }

  count = 0;
  return error;
}

const char *MachException::Name(exception_type_t exc_type) {
  switch (exc_type) {
  case EXC_BAD_ACCESS:
    return "EXC_BAD_ACCESS";
  case EXC_BAD_INSTRUCTION:
    return "EXC_BAD_INSTRUCTION";
  case EXC_ARITHMETIC:
    return "EXC_ARITHMETIC";
  case EXC_EMULATION:
    return "EXC_EMULATION";
  case EXC_SOFTWARE:
    return "EXC_SOFTWARE";
  case EXC_BREAKPOINT:
    return "EXC_BREAKPOINT";
  case EXC_SYSCALL:
    return "EXC_SYSCALL";
  case EXC_MACH_SYSCALL:
    return "EXC_MACH_SYSCALL";
  case EXC_RPC_ALERT:
    return "EXC_RPC_ALERT";
#ifdef EXC_CRASH
  case EXC_CRASH:
    return "EXC_CRASH";
#endif
  default:
    break;
  }
  return NULL;
}
