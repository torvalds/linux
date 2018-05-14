/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_ASM_POWERPC_MMAN_H
#define _UAPI_ASM_POWERPC_MMAN_H

#include <asm-generic/mman-common.h>


#define PROT_SAO	0x10		/* Strong Access Ordering */

#define MAP_RENAME      MAP_ANONYMOUS   /* In SunOS terminology */
#define MAP_NORESERVE   0x40            /* don't reserve swap pages */
#define MAP_LOCKED	0x80

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */

#define MCL_CURRENT     0x2000          /* lock all currently mapped pages */
#define MCL_FUTURE      0x4000          /* lock all additions to address space */
#define MCL_ONFAULT	0x8000		/* lock all pages that are faulted in */

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */
#define MAP_STACK	0x20000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x40000		/* create a huge page mapping */

/* Override any generic PKEY permission defines */
#define PKEY_DISABLE_EXECUTE   0x4
#undef PKEY_ACCESS_MASK
#define PKEY_ACCESS_MASK       (PKEY_DISABLE_ACCESS |\
				PKEY_DISABLE_WRITE  |\
				PKEY_DISABLE_EXECUTE)
#endif /* _UAPI_ASM_POWERPC_MMAN_H */
