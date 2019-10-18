// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/spinlock.h>
#include <linux/smp.h>
#include <asm/cache.h>
#include <asm/barrier.h>

inline void dcache_wb_line(unsigned long start)
{
	asm volatile("dcache.cval1 %0\n"::"r"(start):"memory");
	sync_is();
}

void icache_inv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("icache.iva %0\n"::"r"(i):"memory");
	sync_is();
}

void icache_inv_all(void)
{
	asm volatile("icache.ialls\n":::"memory");
	sync_is();
}

void dcache_wb_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.cval1 %0\n"::"r"(i):"memory");
	sync_is();
}

void dcache_inv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.civa %0\n"::"r"(i):"memory");
	sync_is();
}

void cache_wbinv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.cval1 %0\n"::"r"(i):"memory");
	sync_is();

	i = start & ~(L1_CACHE_BYTES - 1);
	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("icache.iva %0\n"::"r"(i):"memory");
	sync_is();
}
EXPORT_SYMBOL(cache_wbinv_range);

void dma_wbinv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.civa %0\n"::"r"(i):"memory");
	sync_is();
}

void dma_inv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.iva %0\n"::"r"(i):"memory");
	sync_is();
}

void dma_wb_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.cva %0\n"::"r"(i):"memory");
	sync_is();
}
