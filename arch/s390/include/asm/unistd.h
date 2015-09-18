/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/unistd.h"
 */
#ifndef _ASM_S390_UNISTD_H_
#define _ASM_S390_UNISTD_H_

#include <uapi/asm/unistd.h>


#define __IGNORE_time

/* Ignore system calls that are also reachable via sys_socketcall */
#define __IGNORE_recvmmsg
#define __IGNORE_sendmmsg
#define __IGNORE_socket
#define __IGNORE_socketpair
#define __IGNORE_bind
#define __IGNORE_connect
#define __IGNORE_listen
#define __IGNORE_accept4
#define __IGNORE_getsockopt
#define __IGNORE_setsockopt
#define __IGNORE_getsockname
#define __IGNORE_getpeername
#define __IGNORE_sendto
#define __IGNORE_sendmsg
#define __IGNORE_recvfrom
#define __IGNORE_recvmsg
#define __IGNORE_shutdown

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
# ifdef CONFIG_COMPAT
#   define __ARCH_WANT_COMPAT_SYS_TIME
# endif
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

#endif /* _ASM_S390_UNISTD_H_ */
