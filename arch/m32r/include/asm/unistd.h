#ifndef _ASM_M32R_UNISTD_H
#define _ASM_M32R_UNISTD_H

#include <uapi/asm/unistd.h>


#define NR_syscalls 326

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_OLD_GETRLIMIT /*will be unused*/
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK

#define __IGNORE_lchown
#define __IGNORE_setuid
#define __IGNORE_getuid
#define __IGNORE_setgid
#define __IGNORE_getgid
#define __IGNORE_geteuid
#define __IGNORE_getegid
#define __IGNORE_fcntl
#define __IGNORE_setreuid
#define __IGNORE_setregid
#define __IGNORE_getrlimit
#define __IGNORE_getgroups
#define __IGNORE_setgroups
#define __IGNORE_select
#define __IGNORE_mmap
#define __IGNORE_fchown
#define __IGNORE_setfsuid
#define __IGNORE_setfsgid
#define __IGNORE_setresuid
#define __IGNORE_getresuid
#define __IGNORE_setresgid
#define __IGNORE_getresgid
#define __IGNORE_chown

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#ifndef cond_syscall
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")
#endif

#endif /* _ASM_M32R_UNISTD_H */
