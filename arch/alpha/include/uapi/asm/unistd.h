/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ALPHA_UNISTD_H
#define _UAPI_ALPHA_UNISTD_H

/* These are traditionally the names linux-alpha uses for
 * the two otherwise generic system calls */
#define __NR_umount	__NR_umount2
#define __NR_osf_shmat	__NR_shmat

/* These return an extra value but can be used as aliases */
#define __NR_getpid	__NR_getxpid
#define __NR_getuid	__NR_getxuid
#define __NR_getgid	__NR_getxgid

#include <asm/unistd_32.h>

#endif /* _UAPI_ALPHA_UNISTD_H */
