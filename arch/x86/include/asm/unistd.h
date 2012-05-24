#ifndef _ASM_X86_UNISTD_H
#define _ASM_X86_UNISTD_H 1

/* x32 syscall flag bit */
#define __X32_SYSCALL_BIT	0x40000000

#ifdef __KERNEL__

# ifdef CONFIG_X86_X32_ABI
#  define __SYSCALL_MASK (~(__X32_SYSCALL_BIT))
# else
#  define __SYSCALL_MASK (~0)
# endif

# ifdef CONFIG_X86_32

#  include <asm/unistd_32.h>
#  define __ARCH_WANT_IPC_PARSE_VERSION
#  define __ARCH_WANT_STAT64
#  define __ARCH_WANT_SYS_IPC
#  define __ARCH_WANT_SYS_OLD_MMAP
#  define __ARCH_WANT_SYS_OLD_SELECT

# else

#  include <asm/unistd_64.h>
#  include <asm/unistd_64_x32.h>
#  define __ARCH_WANT_COMPAT_SYS_TIME

# endif

# define __ARCH_WANT_OLD_READDIR
# define __ARCH_WANT_OLD_STAT
# define __ARCH_WANT_SYS_ALARM
# define __ARCH_WANT_SYS_FADVISE64
# define __ARCH_WANT_SYS_GETHOSTNAME
# define __ARCH_WANT_SYS_GETPGRP
# define __ARCH_WANT_SYS_LLSEEK
# define __ARCH_WANT_SYS_NICE
# define __ARCH_WANT_SYS_OLDUMOUNT
# define __ARCH_WANT_SYS_OLD_GETRLIMIT
# define __ARCH_WANT_SYS_OLD_UNAME
# define __ARCH_WANT_SYS_PAUSE
# define __ARCH_WANT_SYS_RT_SIGACTION
# define __ARCH_WANT_SYS_RT_SIGSUSPEND
# define __ARCH_WANT_SYS_SGETMASK
# define __ARCH_WANT_SYS_SIGNAL
# define __ARCH_WANT_SYS_SIGPENDING
# define __ARCH_WANT_SYS_SIGPROCMASK
# define __ARCH_WANT_SYS_SOCKETCALL
# define __ARCH_WANT_SYS_TIME
# define __ARCH_WANT_SYS_UTIME
# define __ARCH_WANT_SYS_WAITPID

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
# define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#else
# ifdef __i386__
#  include <asm/unistd_32.h>
# elif defined(__LP64__)
#  include <asm/unistd_64.h>
# else
#  include <asm/unistd_x32.h>
# endif
#endif

#endif /* _ASM_X86_UNISTD_H */
