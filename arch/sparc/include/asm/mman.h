#ifndef __SPARC_MMAN_H__
#define __SPARC_MMAN_H__

#include <asm-generic/mman.h>

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

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#define arch_mmap_check(addr,len,flags)	sparc_mmap_check(addr,len)
int sparc_mmap_check(unsigned long addr, unsigned long len);
#endif
#endif

#endif /* __SPARC_MMAN_H__ */
