/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_N64_KMALLOC_H
#define __ASM_MACH_N64_KMALLOC_H

/* The default of 128 bytes wastes too much, use 32 (the largest cacheline, I) */
#define ARCH_DMA_MINALIGN L1_CACHE_BYTES

#endif /* __ASM_MACH_N64_KMALLOC_H */
