#ifndef _XTENSA_UNISTD_H
#define _XTENSA_UNISTD_H

#define __ARCH_WANT_SYS_CLONE
#include <uapi/asm/unistd.h>

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall");

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
#define __ARCH_WANT_SYS_GETPGRP

/* 
 * Ignore legacy system calls in the checksyscalls.sh script
 */

#define __IGNORE_fork				/* use clone */
#define __IGNORE_time
#define __IGNORE_alarm				/* use setitimer */
#define __IGNORE_pause
#define __IGNORE_mmap				/* use mmap2 */
#define __IGNORE_vfork				/* use clone */
#define __IGNORE_fadvise64			/* use fadvise64_64 */

#endif /* _XTENSA_UNISTD_H */
