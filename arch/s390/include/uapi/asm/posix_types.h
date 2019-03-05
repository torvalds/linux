/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *
 */

#ifndef __ARCH_S390_POSIX_TYPES_H
#define __ARCH_S390_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned long   __kernel_size_t;
typedef long            __kernel_ssize_t;
#define __kernel_size_t __kernel_size_t

typedef unsigned short	__kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t

#ifdef __KERNEL__
typedef unsigned short __kernel_old_uid_t;
typedef unsigned short __kernel_old_gid_t;
#define __kernel_old_uid_t __kernel_old_uid_t
#endif

#ifndef __s390x__

typedef unsigned long   __kernel_ino_t;
typedef unsigned short  __kernel_mode_t;
typedef unsigned short  __kernel_ipc_pid_t;
typedef unsigned short  __kernel_uid_t;
typedef unsigned short  __kernel_gid_t;
typedef int             __kernel_ptrdiff_t;

#else /* __s390x__ */

typedef unsigned int    __kernel_ino_t;
typedef unsigned int    __kernel_mode_t;
typedef int             __kernel_ipc_pid_t;
typedef unsigned int    __kernel_uid_t;
typedef unsigned int    __kernel_gid_t;
typedef long            __kernel_ptrdiff_t;
typedef unsigned long   __kernel_sigset_t;      /* at least 32 bits */

#endif /* __s390x__ */

#define __kernel_ino_t  __kernel_ino_t
#define __kernel_mode_t __kernel_mode_t
#define __kernel_ipc_pid_t __kernel_ipc_pid_t
#define __kernel_uid_t __kernel_uid_t
#define __kernel_gid_t __kernel_gid_t

#include <asm-generic/posix_types.h>

#endif
