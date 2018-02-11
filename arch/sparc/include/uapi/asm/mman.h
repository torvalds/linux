/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__SPARC_MMAN_H__
#define _UAPI__SPARC_MMAN_H__

#include <asm-generic/mman-common.h>

/* SunOS'ified... */

#define MAP_RENAME      MAP_ANONYMOUS   /* In SunOS terminology */
#define MAP_NORESERVE   0x40            /* don't reserve swap pages */
#define MAP_INHERIT     0x80            /* SunOS doesn't do this, but... */
#define MAP_LOCKED      0x100           /* lock the mapping */
#define _MAP_NEW        0x80000000      /* Binary compatibility is fun... */

#define MAP_GROWSDOWN	0x0200		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */

#define MCL_CURRENT     0x2000          /* lock all currently mapped pages */
#define MCL_FUTURE      0x4000          /* lock all additions to address space */
#define MCL_ONFAULT	0x8000		/* lock all pages that are faulted in */

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */
#define MAP_STACK	0x20000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x40000		/* create a huge page mapping */


#endif /* _UAPI__SPARC_MMAN_H__ */
