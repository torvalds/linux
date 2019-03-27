//===-- sanitizer_openbsd.cc ----------------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements Solaris-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_OPENBSD

#include <stdio.h>

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_procmaps.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

extern char **environ;

namespace __sanitizer {

uptr internal_mmap(void *addr, size_t length, int prot, int flags, int fd,
                   u64 offset) {
  return (uptr)mmap(addr, length, prot, flags, fd, offset);
}

uptr internal_munmap(void *addr, uptr length) { return munmap(addr, length); }

int internal_mprotect(void *addr, uptr length, int prot) {
  return mprotect(addr, length, prot);
}

int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen) {
  Printf("internal_sysctlbyname not implemented for OpenBSD");
  Die();
  return 0;
}

uptr ReadBinaryName(/*out*/char *buf, uptr buf_len) {
  // On OpenBSD we cannot get the full path
  struct kinfo_proc kp;
  uptr kl;
  const int Mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  if (internal_sysctl(Mib, ARRAY_SIZE(Mib), &kp, &kl, NULL, 0) != -1)
    return internal_snprintf(buf,
                             (KI_MAXCOMLEN < buf_len ? KI_MAXCOMLEN : buf_len),
                             "%s", kp.p_comm);
  return (uptr)0;
}

static void GetArgsAndEnv(char ***argv, char ***envp) {
  uptr nargv;
  uptr nenv;
  int argvmib[4] = {CTL_KERN, KERN_PROC_ARGS, getpid(), KERN_PROC_ARGV};
  int envmib[4] = {CTL_KERN, KERN_PROC_ARGS, getpid(), KERN_PROC_ENV};
  if (internal_sysctl(argvmib, 4, NULL, &nargv, NULL, 0) == -1) {
    Printf("sysctl KERN_PROC_NARGV failed\n");
    Die();
  }
  if (internal_sysctl(envmib, 4, NULL, &nenv, NULL, 0) == -1) {
    Printf("sysctl KERN_PROC_NENV failed\n");
    Die();
  }
  if (internal_sysctl(argvmib, 4, &argv, &nargv, NULL, 0) == -1) {
    Printf("sysctl KERN_PROC_ARGV failed\n");
    Die();
  }
  if (internal_sysctl(envmib, 4, &envp, &nenv, NULL, 0) == -1) {
    Printf("sysctl KERN_PROC_ENV failed\n");
    Die();
  }
}

char **GetArgv() {
  char **argv, **envp;
  GetArgsAndEnv(&argv, &envp);
  return argv;
}

char **GetEnviron() {
  char **argv, **envp;
  GetArgsAndEnv(&argv, &envp);
  return envp;
}

void ReExec() {
  UNIMPLEMENTED();
}

}  // namespace __sanitizer

#endif  // SANITIZER_OPENBSD
