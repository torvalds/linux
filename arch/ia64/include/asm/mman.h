#ifndef _ASM_IA64_MMAN_H
#define _ASM_IA64_MMAN_H

/*
 * Based on <asm-i386/mman.h>.
 *
 * Modified 1998-2000, 2002
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

#include <asm-generic/mman.h>

#define MAP_GROWSDOWN	0x00100		/* stack-like segment */
#define MAP_GROWSUP	0x00200		/* register stack-like segment */
#define MAP_DENYWRITE	0x00800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x01000		/* mark it as an executable */
#define MAP_LOCKED	0x02000		/* pages are locked */
#define MAP_NORESERVE	0x04000		/* don't check for reservations */
#define MAP_POPULATE	0x08000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#define arch_mmap_check	ia64_mmap_check
int ia64_mmap_check(unsigned long addr, unsigned long len,
		unsigned long flags);
#endif
#endif

#endif /* _ASM_IA64_MMAN_H */
