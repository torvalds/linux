/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_CACHE_H
#define __ASM_CSKY_CACHE_H

/* bytes per L1 cache line */
#define L1_CACHE_SHIFT	CONFIG_L1_CACHE_SHIFT

#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#ifndef __ASSEMBLY__

void dcache_wb_line(unsigned long start);

void icache_inv_range(unsigned long start, unsigned long end);
void icache_inv_all(void);

void dcache_wb_range(unsigned long start, unsigned long end);
void dcache_wbinv_all(void);

void cache_wbinv_range(unsigned long start, unsigned long end);
void cache_wbinv_all(void);

void dma_wbinv_range(unsigned long start, unsigned long end);
void dma_wb_range(unsigned long start, unsigned long end);

#endif
#endif  /* __ASM_CSKY_CACHE_H */
