#ifndef _ASM_POWERPC_POSIX_TYPES_H
#define _ASM_POWERPC_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

#ifdef __powerpc64__
typedef unsigned long	__kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t
#else
typedef unsigned int	__kernel_size_t;
typedef int		__kernel_ssize_t;
typedef long		__kernel_ptrdiff_t;
#define __kernel_size_t __kernel_size_t

typedef unsigned short	__kernel_nlink_t;
#define __kernel_nlink_t __kernel_nlink_t

typedef short		__kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t
#endif

#include <asm-generic/posix_types.h>

#endif /* _ASM_POWERPC_POSIX_TYPES_H */
