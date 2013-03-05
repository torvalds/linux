#ifndef _ALPHA_UNISTD_H
#define _ALPHA_UNISTD_H

#include <uapi/asm/unistd.h>


#define NR_SYSCALLS			506

#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

/* "Conditional" syscalls.  What we want is

	__attribute__((weak,alias("sys_ni_syscall")))

   but that raises the problem of what type to give the symbol.  If we use
   a prototype, it'll conflict with the definition given in this file and
   others.  If we use __typeof, we discover that not all symbols actually
   have declarations.  If we use no prototype, then we get warnings from
   -Wstrict-prototypes.  Ho hum.  */

#define cond_syscall(x)  asm(".weak\t" #x "\n" #x " = sys_ni_syscall")

#endif /* _ALPHA_UNISTD_H */
