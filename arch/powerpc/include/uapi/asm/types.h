/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_ASM_POWERPC_TYPES_H
#define _UAPI_ASM_POWERPC_TYPES_H

/*
 * This is here because we used to use l64 for 64bit powerpc
 * and we don't want to impact user mode with our change to ll64
 * in the kernel.
 *
 * However, some user programs are fine with this.  They can
 * flag __SANE_USERSPACE_TYPES__ to get int-ll64.h here.
 */
#if !defined(__SANE_USERSPACE_TYPES__) && defined(__powerpc64__) && !defined(__KERNEL__)
# include <asm-generic/int-l64.h>
#else
# include <asm-generic/int-ll64.h>
#endif

#ifndef __ASSEMBLY__


typedef struct {
	__u32 u[4];
} __attribute__((aligned(16))) __vector128;

#endif /* __ASSEMBLY__ */


#endif /* _UAPI_ASM_POWERPC_TYPES_H */
