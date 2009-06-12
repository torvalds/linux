#ifndef __ASM_SH_TYPES_H
#define __ASM_SH_TYPES_H

#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#define BITS_PER_LONG 32

#ifndef __ASSEMBLY__

/* Dma addresses are 32-bits wide.  */

typedef u32 dma_addr_t;

#ifdef CONFIG_SUPERH32
typedef u16 insn_size_t;
#else
typedef u32 insn_size_t;
#endif

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_SH_TYPES_H */
