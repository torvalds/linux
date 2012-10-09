# ifdef CONFIG_SUPERH32
#  include <asm/unistd_32.h>
# else
#  include <asm/unistd_64.h>
# endif

# define __ARCH_WANT_SYS_RT_SIGSUSPEND
# define __ARCH_WANT_OLD_READDIR
# define __ARCH_WANT_OLD_STAT
# define __ARCH_WANT_STAT64
# define __ARCH_WANT_SYS_ALARM
# define __ARCH_WANT_SYS_GETHOSTNAME
# define __ARCH_WANT_SYS_IPC
# define __ARCH_WANT_SYS_PAUSE
# define __ARCH_WANT_SYS_SGETMASK
# define __ARCH_WANT_SYS_SIGNAL
# define __ARCH_WANT_SYS_TIME
# define __ARCH_WANT_SYS_UTIME
# define __ARCH_WANT_SYS_WAITPID
# define __ARCH_WANT_SYS_SOCKETCALL
# define __ARCH_WANT_SYS_FADVISE64
# define __ARCH_WANT_SYS_GETPGRP
# define __ARCH_WANT_SYS_LLSEEK
# define __ARCH_WANT_SYS_NICE
# define __ARCH_WANT_SYS_OLD_GETRLIMIT
# define __ARCH_WANT_SYS_OLD_UNAME
# define __ARCH_WANT_SYS_OLDUMOUNT
# define __ARCH_WANT_SYS_SIGPENDING
# define __ARCH_WANT_SYS_SIGPROCMASK
# define __ARCH_WANT_SYS_RT_SIGACTION

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
# define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#include <uapi/asm/unistd.h>
