#ifndef _ASM_GENERIC_TYPES_H
#define _ASM_GENERIC_TYPES_H
/*
 * int-ll64 is used practically everywhere now,
 * so use it as a reasonable default.
 */
#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__
#ifndef __ASSEMBLY__
/*
 * DMA addresses may be very different from physical addresses
 * and pointers. i386 and powerpc may have 64 bit DMA on 32 bit
 * systems, while sparc64 uses 32 bit DMA addresses for 64 bit
 * physical addresses.
 * This default defines dma_addr_t to have the same size as
 * phys_addr_t, which is the most common way.
 * Do not define the dma64_addr_t type, which never really
 * worked.
 */
#ifndef dma_addr_t
#ifdef CONFIG_PHYS_ADDR_T_64BIT
typedef u64 dma_addr_t;
#else
typedef u32 dma_addr_t;
#endif /* CONFIG_PHYS_ADDR_T_64BIT */
#endif /* dma_addr_t */

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_GENERIC_TYPES_H */
