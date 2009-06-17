#ifndef _PARISC_TYPES_H
#define _PARISC_TYPES_H

#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* Dma addresses are 32-bits wide.  */

typedef u32 dma_addr_t;
typedef u64 dma64_addr_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif
