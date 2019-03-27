//===- FuzzerUtilFuchsia.cpp - Misc utils for Fuchsia. --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Misc utils implementation using Fuchsia/Zircon APIs.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"

#if LIBFUZZER_FUCHSIA

#include "FuzzerInternal.h"
#include "FuzzerUtil.h"
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <fcntl.h>
#include <lib/fdio/spawn.h>
#include <string>
#include <sys/select.h>
#include <thread>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

namespace fuzzer {

// Given that Fuchsia doesn't have the POSIX signals that libFuzzer was written
// around, the general approach is to spin up dedicated threads to watch for
// each requested condition (alarm, interrupt, crash).  Of these, the crash
// handler is the most involved, as it requires resuming the crashed thread in
// order to invoke the sanitizers to get the needed state.

// Forward declaration of assembly trampoline needed to resume crashed threads.
// This appears to have external linkage to  C++, which is why it's not in the
// anonymous namespace.  The assembly definition inside MakeTrampoline()
// actually defines the symbol with internal linkage only.
void CrashTrampolineAsm() __asm__("CrashTrampolineAsm");

namespace {

// A magic value for the Zircon exception port, chosen to spell 'FUZZING'
// when interpreted as a byte sequence on little-endian platforms.
const uint64_t kFuzzingCrash = 0x474e495a5a5546;

// Helper function to handle Zircon syscall failures.
void ExitOnErr(zx_status_t Status, const char *Syscall) {
  if (Status != ZX_OK) {
    Printf("libFuzzer: %s failed: %s\n", Syscall,
           _zx_status_get_string(Status));
    exit(1);
  }
}

void AlarmHandler(int Seconds) {
  while (true) {
    SleepSeconds(Seconds);
    Fuzzer::StaticAlarmCallback();
  }
}

void InterruptHandler() {
  fd_set readfds;
  // Ctrl-C sends ETX in Zircon.
  do {
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, nullptr);
  } while(!FD_ISSET(STDIN_FILENO, &readfds) || getchar() != 0x03);
  Fuzzer::StaticInterruptCallback();
}

// For the crash handler, we need to call Fuzzer::StaticCrashSignalCallback
// without POSIX signal handlers.  To achieve this, we use an assembly function
// to add the necessary CFI unwinding information and a C function to bridge
// from that back into C++.

// FIXME: This works as a short-term solution, but this code really shouldn't be
// architecture dependent. A better long term solution is to implement remote
// unwinding and expose the necessary APIs through sanitizer_common and/or ASAN
// to allow the exception handling thread to gather the crash state directly.
//
// Alternatively, Fuchsia may in future actually implement basic signal
// handling for the machine trap signals.
#if defined(__x86_64__)
#define FOREACH_REGISTER(OP_REG, OP_NUM) \
  OP_REG(rax)                            \
  OP_REG(rbx)                            \
  OP_REG(rcx)                            \
  OP_REG(rdx)                            \
  OP_REG(rsi)                            \
  OP_REG(rdi)                            \
  OP_REG(rbp)                            \
  OP_REG(rsp)                            \
  OP_REG(r8)                             \
  OP_REG(r9)                             \
  OP_REG(r10)                            \
  OP_REG(r11)                            \
  OP_REG(r12)                            \
  OP_REG(r13)                            \
  OP_REG(r14)                            \
  OP_REG(r15)                            \
  OP_REG(rip)

#elif defined(__aarch64__)
#define FOREACH_REGISTER(OP_REG, OP_NUM) \
  OP_NUM(0)                              \
  OP_NUM(1)                              \
  OP_NUM(2)                              \
  OP_NUM(3)                              \
  OP_NUM(4)                              \
  OP_NUM(5)                              \
  OP_NUM(6)                              \
  OP_NUM(7)                              \
  OP_NUM(8)                              \
  OP_NUM(9)                              \
  OP_NUM(10)                             \
  OP_NUM(11)                             \
  OP_NUM(12)                             \
  OP_NUM(13)                             \
  OP_NUM(14)                             \
  OP_NUM(15)                             \
  OP_NUM(16)                             \
  OP_NUM(17)                             \
  OP_NUM(18)                             \
  OP_NUM(19)                             \
  OP_NUM(20)                             \
  OP_NUM(21)                             \
  OP_NUM(22)                             \
  OP_NUM(23)                             \
  OP_NUM(24)                             \
  OP_NUM(25)                             \
  OP_NUM(26)                             \
  OP_NUM(27)                             \
  OP_NUM(28)                             \
  OP_NUM(29)                             \
  OP_NUM(30)                             \
  OP_REG(sp)

#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif

// Produces a CFI directive for the named or numbered register.
#define CFI_OFFSET_REG(reg) ".cfi_offset " #reg ", %c[" #reg "]\n"
#define CFI_OFFSET_NUM(num) CFI_OFFSET_REG(r##num)

// Produces an assembler input operand for the named or numbered register.
#define ASM_OPERAND_REG(reg) \
  [reg] "i"(offsetof(zx_thread_state_general_regs_t, reg)),
#define ASM_OPERAND_NUM(num)                                 \
  [r##num] "i"(offsetof(zx_thread_state_general_regs_t, r[num])),

// Trampoline to bridge from the assembly below to the static C++ crash
// callback.
__attribute__((noreturn))
static void StaticCrashHandler() {
  Fuzzer::StaticCrashSignalCallback();
  for (;;) {
    _Exit(1);
  }
}

// Creates the trampoline with the necessary CFI information to unwind through
// to the crashing call stack.  The attribute is necessary because the function
// is never called; it's just a container around the assembly to allow it to
// use operands for compile-time computed constants.
__attribute__((used))
void MakeTrampoline() {
  __asm__(".cfi_endproc\n"
    ".pushsection .text.CrashTrampolineAsm\n"
    ".type CrashTrampolineAsm,STT_FUNC\n"
"CrashTrampolineAsm:\n"
    ".cfi_startproc simple\n"
    ".cfi_signal_frame\n"
#if defined(__x86_64__)
    ".cfi_return_column rip\n"
    ".cfi_def_cfa rsp, 0\n"
    FOREACH_REGISTER(CFI_OFFSET_REG, CFI_OFFSET_NUM)
    "call %c[StaticCrashHandler]\n"
    "ud2\n"
#elif defined(__aarch64__)
    ".cfi_return_column 33\n"
    ".cfi_def_cfa sp, 0\n"
    ".cfi_offset 33, %c[pc]\n"
    FOREACH_REGISTER(CFI_OFFSET_REG, CFI_OFFSET_NUM)
    "bl %[StaticCrashHandler]\n"
#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif
    ".cfi_endproc\n"
    ".size CrashTrampolineAsm, . - CrashTrampolineAsm\n"
    ".popsection\n"
    ".cfi_startproc\n"
    : // No outputs
    : FOREACH_REGISTER(ASM_OPERAND_REG, ASM_OPERAND_NUM)
#if defined(__aarch64__)
      ASM_OPERAND_REG(pc)
#endif
      [StaticCrashHandler] "i" (StaticCrashHandler));
}

void CrashHandler(zx_handle_t *Event) {
  // This structure is used to ensure we close handles to objects we create in
  // this handler.
  struct ScopedHandle {
    ~ScopedHandle() { _zx_handle_close(Handle); }
    zx_handle_t Handle = ZX_HANDLE_INVALID;
  };

  // Create and bind the exception port.  We need to claim to be a "debugger" so
  // the kernel will allow us to modify and resume dying threads (see below).
  // Once the port is set, we can signal the main thread to continue and wait
  // for the exception to arrive.
  ScopedHandle Port;
  ExitOnErr(_zx_port_create(0, &Port.Handle), "_zx_port_create");
  zx_handle_t Self = _zx_process_self();

  ExitOnErr(_zx_task_bind_exception_port(Self, Port.Handle, kFuzzingCrash,
                                         ZX_EXCEPTION_PORT_DEBUGGER),
            "_zx_task_bind_exception_port");

  ExitOnErr(_zx_object_signal(*Event, 0, ZX_USER_SIGNAL_0),
            "_zx_object_signal");

  zx_port_packet_t Packet;
  ExitOnErr(_zx_port_wait(Port.Handle, ZX_TIME_INFINITE, &Packet),
            "_zx_port_wait");

  // At this point, we want to get the state of the crashing thread, but
  // libFuzzer and the sanitizers assume this will happen from that same thread
  // via a POSIX signal handler. "Resurrecting" the thread in the middle of the
  // appropriate callback is as simple as forcibly setting the instruction
  // pointer/program counter, provided we NEVER EVER return from that function
  // (since otherwise our stack will not be valid).
  ScopedHandle Thread;
  ExitOnErr(_zx_object_get_child(Self, Packet.exception.tid,
                                 ZX_RIGHT_SAME_RIGHTS, &Thread.Handle),
            "_zx_object_get_child");

  zx_thread_state_general_regs_t GeneralRegisters;
  ExitOnErr(_zx_thread_read_state(Thread.Handle, ZX_THREAD_STATE_GENERAL_REGS,
                                  &GeneralRegisters, sizeof(GeneralRegisters)),
            "_zx_thread_read_state");

  // To unwind properly, we need to push the crashing thread's register state
  // onto the stack and jump into a trampoline with CFI instructions on how
  // to restore it.
#if defined(__x86_64__)
  uintptr_t StackPtr =
      (GeneralRegisters.rsp - (128 + sizeof(GeneralRegisters))) &
      -(uintptr_t)16;
  __unsanitized_memcpy(reinterpret_cast<void *>(StackPtr), &GeneralRegisters,
         sizeof(GeneralRegisters));
  GeneralRegisters.rsp = StackPtr;
  GeneralRegisters.rip = reinterpret_cast<zx_vaddr_t>(CrashTrampolineAsm);

#elif defined(__aarch64__)
  uintptr_t StackPtr =
      (GeneralRegisters.sp - sizeof(GeneralRegisters)) & -(uintptr_t)16;
  __unsanitized_memcpy(reinterpret_cast<void *>(StackPtr), &GeneralRegisters,
                       sizeof(GeneralRegisters));
  GeneralRegisters.sp = StackPtr;
  GeneralRegisters.pc = reinterpret_cast<zx_vaddr_t>(CrashTrampolineAsm);

#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif

  // Now force the crashing thread's state.
  ExitOnErr(_zx_thread_write_state(Thread.Handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &GeneralRegisters, sizeof(GeneralRegisters)),
            "_zx_thread_write_state");

  ExitOnErr(_zx_task_resume_from_exception(Thread.Handle, Port.Handle, 0),
            "_zx_task_resume_from_exception");
}

} // namespace

// Platform specific functions.
void SetSignalHandler(const FuzzingOptions &Options) {
  // Set up alarm handler if needed.
  if (Options.UnitTimeoutSec > 0) {
    std::thread T(AlarmHandler, Options.UnitTimeoutSec / 2 + 1);
    T.detach();
  }

  // Set up interrupt handler if needed.
  if (Options.HandleInt || Options.HandleTerm) {
    std::thread T(InterruptHandler);
    T.detach();
  }

  // Early exit if no crash handler needed.
  if (!Options.HandleSegv && !Options.HandleBus && !Options.HandleIll &&
      !Options.HandleFpe && !Options.HandleAbrt)
    return;

  // Set up the crash handler and wait until it is ready before proceeding.
  zx_handle_t Event;
  ExitOnErr(_zx_event_create(0, &Event), "_zx_event_create");

  std::thread T(CrashHandler, &Event);
  zx_status_t Status =
      _zx_object_wait_one(Event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, nullptr);
  _zx_handle_close(Event);
  ExitOnErr(Status, "_zx_object_wait_one");

  T.detach();
}

void SleepSeconds(int Seconds) {
  _zx_nanosleep(_zx_deadline_after(ZX_SEC(Seconds)));
}

unsigned long GetPid() {
  zx_status_t rc;
  zx_info_handle_basic_t Info;
  if ((rc = _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &Info,
                                sizeof(Info), NULL, NULL)) != ZX_OK) {
    Printf("libFuzzer: unable to get info about self: %s\n",
           _zx_status_get_string(rc));
    exit(1);
  }
  return Info.koid;
}

size_t GetPeakRSSMb() {
  zx_status_t rc;
  zx_info_task_stats_t Info;
  if ((rc = _zx_object_get_info(_zx_process_self(), ZX_INFO_TASK_STATS, &Info,
                                sizeof(Info), NULL, NULL)) != ZX_OK) {
    Printf("libFuzzer: unable to get info about self: %s\n",
           _zx_status_get_string(rc));
    exit(1);
  }
  return (Info.mem_private_bytes + Info.mem_shared_bytes) >> 20;
}

template <typename Fn>
class RunOnDestruction {
 public:
  explicit RunOnDestruction(Fn fn) : fn_(fn) {}
  ~RunOnDestruction() { fn_(); }

 private:
  Fn fn_;
};

template <typename Fn>
RunOnDestruction<Fn> at_scope_exit(Fn fn) {
  return RunOnDestruction<Fn>(fn);
}

int ExecuteCommand(const Command &Cmd) {
  zx_status_t rc;

  // Convert arguments to C array
  auto Args = Cmd.getArguments();
  size_t Argc = Args.size();
  assert(Argc != 0);
  std::unique_ptr<const char *[]> Argv(new const char *[Argc + 1]);
  for (size_t i = 0; i < Argc; ++i)
    Argv[i] = Args[i].c_str();
  Argv[Argc] = nullptr;

  // Determine output.  On Fuchsia, the fuzzer is typically run as a component
  // that lacks a mutable working directory. Fortunately, when this is the case
  // a mutable output directory must be specified using "-artifact_prefix=...",
  // so write the log file(s) there.
  int FdOut = STDOUT_FILENO;
  if (Cmd.hasOutputFile()) {
    std::string Path;
    if (Cmd.hasFlag("artifact_prefix"))
      Path = Cmd.getFlagValue("artifact_prefix") + "/" + Cmd.getOutputFile();
    else
      Path = Cmd.getOutputFile();
    FdOut = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (FdOut == -1) {
      Printf("libFuzzer: failed to open %s: %s\n", Path.c_str(),
             strerror(errno));
      return ZX_ERR_IO;
    }
  }
  auto CloseFdOut = at_scope_exit([FdOut]() {
    if (FdOut != STDOUT_FILENO)
      close(FdOut);
  });

  // Determine stderr
  int FdErr = STDERR_FILENO;
  if (Cmd.isOutAndErrCombined())
    FdErr = FdOut;

  // Clone the file descriptors into the new process
  fdio_spawn_action_t SpawnAction[] = {
      {
          .action = FDIO_SPAWN_ACTION_CLONE_FD,
          .fd =
              {
                  .local_fd = STDIN_FILENO,
                  .target_fd = STDIN_FILENO,
              },
      },
      {
          .action = FDIO_SPAWN_ACTION_CLONE_FD,
          .fd =
              {
                  .local_fd = FdOut,
                  .target_fd = STDOUT_FILENO,
              },
      },
      {
          .action = FDIO_SPAWN_ACTION_CLONE_FD,
          .fd =
              {
                  .local_fd = FdErr,
                  .target_fd = STDERR_FILENO,
              },
      },
  };

  // Start the process.
  char ErrorMsg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t ProcessHandle = ZX_HANDLE_INVALID;
  rc = fdio_spawn_etc(
      ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL & (~FDIO_SPAWN_CLONE_STDIO),
      Argv[0], Argv.get(), nullptr, 3, SpawnAction, &ProcessHandle, ErrorMsg);
  if (rc != ZX_OK) {
    Printf("libFuzzer: failed to launch '%s': %s, %s\n", Argv[0], ErrorMsg,
           _zx_status_get_string(rc));
    return rc;
  }
  auto CloseHandle = at_scope_exit([&]() { _zx_handle_close(ProcessHandle); });

  // Now join the process and return the exit status.
  if ((rc = _zx_object_wait_one(ProcessHandle, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, nullptr)) != ZX_OK) {
    Printf("libFuzzer: failed to join '%s': %s\n", Argv[0],
           _zx_status_get_string(rc));
    return rc;
  }

  zx_info_process_t Info;
  if ((rc = _zx_object_get_info(ProcessHandle, ZX_INFO_PROCESS, &Info,
                                sizeof(Info), nullptr, nullptr)) != ZX_OK) {
    Printf("libFuzzer: unable to get return code from '%s': %s\n", Argv[0],
           _zx_status_get_string(rc));
    return rc;
  }

  return Info.return_code;
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  return memmem(Data, DataLen, Patt, PattLen);
}

} // namespace fuzzer

#endif // LIBFUZZER_FUCHSIA
