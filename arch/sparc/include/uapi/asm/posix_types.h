/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

#ifndef __SPARC_POSIX_TYPES_H
#define __SPARC_POSIX_TYPES_H

#if defined(__sparc__) && defined(__arch64__)
/* sparc 64 bit */

typedef unsigned short 	       __kernel_old_uid_t;
typedef unsigned short         __kernel_old_gid_t;
#define __kernel_old_uid_t __kernel_old_uid_t

/* Note this piece of asymmetry from the v9 ABI.  */
typedef int		       __kernel_suseconds_t;
#define __kernel_suseconds_t __kernel_suseconds_t

#else
/* sparc 32 bit */

typedef unsigned int           __kernel_size_t;
typedef int                    __kernel_ssize_t;
typedef long int               __kernel_ptrdiff_t;
#define __kernel_size_t __kernel_size_t

typedef unsigned short         __kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t

typedef unsigned short         __kernel_uid_t;
typedef unsigned short         __kernel_gid_t;
#define __kernel_uid_t __kernel_uid_t

typedef unsigned short         __kernel_mode_t;
#define __kernel_mode_t __kernel_mode_t

typedef long                   __kernel_daddr_t;
#define __kernel_daddr_t __kernel_daddr_t

typedef unsigned short	       __kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t

#endif /* defined(__sparc__) && defined(__arch64__) */

#include <asm-generic/posix_types.h>

#endif /* __SPARC_POSIX_TYPES_H */
