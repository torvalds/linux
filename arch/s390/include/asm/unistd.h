/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/unistd.h"
 */
#ifndef _ASM_S390_UNISTD_H_
#define _ASM_S390_UNISTD_H_

#include <uapi/asm/unistd.h>


#ifndef CONFIG_64BIT
#define __IGNORE_select
#else
#define __IGNORE_time
#endif

/* Ignore NUMA system calls. Not wired up on s390. */
#define __IGNORE_mbind
#define __IGNORE_get_mempolicy
#define __IGNORE_set_mempolicy
#define __IGNORE_migrate_pages
#define __IGNORE_move_pages

/* Ignore system calls that are also reachable via sys_socket */
#define __IGNORE_recvmmsg
#define __IGNORE_sendmmsg

#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLD_MMAP
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
# ifndef CONFIG_64BIT
#   define __ARCH_WANT_STAT64
#   define __ARCH_WANT_SYS_TIME
# endif
# ifdef CONFIG_COMPAT
#   define __ARCH_WANT_COMPAT_SYS_TIME
#   define __ARCH_WANT_COMPAT_SYS_RT_SIGSUSPEND
# endif
#define __ARCH_WANT_SYS_EXECVE

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#endif /* _ASM_S390_UNISTD_H_ */
