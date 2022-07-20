/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_DMA_H
#define _M68K_DMA_H 1

/* it's useless on the m68k, but unfortunately needed by the new
   bootmem allocator (but this should do it for this) */
#define MAX_DMA_ADDRESS PAGE_OFFSET

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy    (0)
#endif

#endif /* _M68K_DMA_H */
