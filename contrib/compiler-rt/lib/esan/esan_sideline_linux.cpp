//===-- esan_sideline_linux.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Support for a separate or "sideline" tool thread on Linux.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX

#include "esan_sideline.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_linux.h"
#include <errno.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace __esan {

static const int SigAltStackSize = 4*1024;
static const int SidelineStackSize = 4*1024;
static const uptr SidelineIdUninitialized = 1;

// FIXME: we'll need some kind of TLS (can we trust that a pthread key will
// work in our non-POSIX thread?) to access our data in our signal handler
// with multiple sideline threads.  For now we assume there is only one
// sideline thread and we use a dirty solution of a global var.
static SidelineThread *TheThread;

// We aren't passing SA_NODEFER so the same signal is blocked while here.
void SidelineThread::handleSidelineSignal(int SigNum,
                                          __sanitizer_siginfo *SigInfo,
                                          void *Ctx) {
  VPrintf(3, "Sideline signal %d\n", SigNum);
  CHECK_EQ(SigNum, SIGALRM);
  // See above about needing TLS to avoid this global var.
  SidelineThread *Thread = TheThread;
  if (atomic_load(&Thread->SidelineExit, memory_order_relaxed) != 0)
    return;
  Thread->sampleFunc(Thread->FuncArg);
}

void SidelineThread::registerSignal(int SigNum) {
  __sanitizer_sigaction SigAct;
  internal_memset(&SigAct, 0, sizeof(SigAct));
  SigAct.sigaction = handleSidelineSignal;
  // We do not pass SA_NODEFER as we want to block the same signal.
  SigAct.sa_flags = SA_ONSTACK | SA_SIGINFO;
  int Res = internal_sigaction(SigNum, &SigAct, nullptr);
  CHECK_EQ(Res, 0);
}

int SidelineThread::runSideline(void *Arg) {
  VPrintf(1, "Sideline thread starting\n");
  SidelineThread *Thread = static_cast<SidelineThread*>(Arg);

  // If the parent dies, we want to exit also.
  internal_prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);

  // Set up a signal handler on an alternate stack for safety.
  InternalMmapVector<char> StackMap(SigAltStackSize);
  stack_t SigAltStack;
  SigAltStack.ss_sp = StackMap.data();
  SigAltStack.ss_size = SigAltStackSize;
  SigAltStack.ss_flags = 0;
  internal_sigaltstack(&SigAltStack, nullptr);

  // We inherit the signal mask from the app thread.  In case
  // we weren't created at init time, we ensure the mask is empty.
  __sanitizer_sigset_t SigSet;
  internal_sigfillset(&SigSet);
  int Res = internal_sigprocmask(SIG_UNBLOCK, &SigSet, nullptr);
  CHECK_EQ(Res, 0);

  registerSignal(SIGALRM);

  bool TimerSuccess = Thread->adjustTimer(Thread->Freq);
  CHECK(TimerSuccess);

  // We loop, doing nothing but handling itimer signals.
  while (atomic_load(&TheThread->SidelineExit, memory_order_relaxed) == 0)
    sched_yield();

  if (!Thread->adjustTimer(0))
    VPrintf(1, "Failed to disable timer\n");

  VPrintf(1, "Sideline thread exiting\n");
  return 0;
}

bool SidelineThread::launchThread(SidelineFunc takeSample, void *Arg,
                                  u32 FreqMilliSec) {
  // This can only be called once.  However, we can't clear a field in
  // the constructor and check for that here as the constructor for
  // a static instance is called *after* our module_ctor and thus after
  // this routine!  Thus we rely on the TheThread check below.
  CHECK(TheThread == nullptr); // Only one sideline thread is supported.
  TheThread = this;
  sampleFunc = takeSample;
  FuncArg = Arg;
  Freq = FreqMilliSec;
  atomic_store(&SidelineExit, 0, memory_order_relaxed);

  // We do without a guard page.
  Stack = static_cast<char*>(MmapOrDie(SidelineStackSize, "SidelineStack"));
  // We need to handle the return value from internal_clone() not having been
  // assigned yet (for our CHECK in adjustTimer()) so we ensure this has a
  // sentinel value.
  SidelineId = SidelineIdUninitialized;
  // By omitting CLONE_THREAD, the child is in its own thread group and will not
  // receive any of the application's signals.
  SidelineId = internal_clone(
      runSideline, Stack + SidelineStackSize,
      CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_UNTRACED,
      this, nullptr /* parent_tidptr */,
      nullptr /* newtls */, nullptr /* child_tidptr */);
  int ErrCode;
  if (internal_iserror(SidelineId, &ErrCode)) {
    Printf("FATAL: EfficiencySanitizer failed to spawn a thread (code %d).\n",
           ErrCode);
    Die();
    return false; // Not reached.
  }
  return true;
}

bool SidelineThread::joinThread() {
  VPrintf(1, "Joining sideline thread\n");
  bool Res = true;
  atomic_store(&SidelineExit, 1, memory_order_relaxed);
  while (true) {
    uptr Status = internal_waitpid(SidelineId, nullptr, __WALL);
    int ErrCode;
    if (!internal_iserror(Status, &ErrCode))
      break;
    if (ErrCode == EINTR)
      continue;
    VPrintf(1, "Failed to join sideline thread (errno %d)\n", ErrCode);
    Res = false;
    break;
  }
  UnmapOrDie(Stack, SidelineStackSize);
  return Res;
}

// Must be called from the sideline thread itself.
bool SidelineThread::adjustTimer(u32 FreqMilliSec) {
  // The return value of internal_clone() may not have been assigned yet:
  CHECK(internal_getpid() == SidelineId ||
        SidelineId == SidelineIdUninitialized);
  Freq = FreqMilliSec;
  struct itimerval TimerVal;
  TimerVal.it_interval.tv_sec = (time_t) Freq / 1000;
  TimerVal.it_interval.tv_usec = (time_t) (Freq % 1000) * 1000;
  TimerVal.it_value.tv_sec = (time_t) Freq / 1000;
  TimerVal.it_value.tv_usec = (time_t) (Freq % 1000) * 1000;
  // As we're in a different thread group, we cannot use either
  // ITIMER_PROF or ITIMER_VIRTUAL without taking up scheduled
  // time ourselves: thus we must use real time.
  int Res = setitimer(ITIMER_REAL, &TimerVal, nullptr);
  return (Res == 0);
}

} // namespace __esan

#endif // SANITIZER_LINUX
