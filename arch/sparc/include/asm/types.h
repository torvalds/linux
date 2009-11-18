#ifndef _SPARC_TYPES_H
#define _SPARC_TYPES_H
/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */

#if defined(__sparc__)

#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* Dma addresses come in generic and 64-bit flavours.  */

typedef u32 dma_addr_t;

#if defined(__arch64__)

/*** SPARC 64 bit ***/
typedef u64 dma64_addr_t;
#else
/*** SPARC 32 bit ***/
typedef u32 dma64_addr_t;

#endif /* defined(__arch64__) */

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* defined(__sparc__) */

#endif /* defined(_SPARC_TYPES_H) */
