// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <asm/cache.h>
#include <asm/barrier.h>

/* for L1-cache */
#define INS_CACHE		(1 << 0)
#define DATA_CACHE		(1 << 1)
#define CACHE_INV		(1 << 4)
#define CACHE_CLR		(1 << 5)
#define CACHE_OMS		(1 << 6)

void local_icache_inv_all(void *priv)
{
	mtcr("cr17", INS_CACHE|CACHE_INV);
	sync_is();
}

#ifdef CONFIG_CPU_HAS_ICACHE_INS
void icache_inv_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("icache.iva %0\n"::"r"(i):"memory");
	sync_is();
}
#else
struct cache_range {
	unsigned long start;
	unsigned long end;
};

static DEFINE_SPINLOCK(cache_lock);

static inline void cache_op_line(unsigned long i, unsigned int val)
{
	mtcr("cr22", i);
	mtcr("cr17", val);
}

void local_icache_inv_range(void *priv)
{
	struct cache_range *param = priv;
	unsigned long i = param->start & ~(L1_CACHE_BYTES - 1);
	unsigned long flags;

	spin_lock_irqsave(&cache_lock, flags);

	for (; i < param->end; i += L1_CACHE_BYTES)
		cache_op_line(i, INS_CACHE | CACHE_INV | CACHE_OMS);

	spin_unlock_irqrestore(&cache_lock, flags);

	sync_is();
}

void icache_inv_range(unsigned long start, unsigned long end)
{
	struct cache_range param = { start, end };

	if (irqs_disabled())
		local_icache_inv_range(&param);
	else
		on_each_cpu(local_icache_inv_range, &param, 1);
}
#endif

inline void dcache_wb_line(unsigned long start)
{
	asm volatile("dcache.cval1 %0\n"::"r"(start):"memory");
	sync_is();
}

void dcache_wb_range(unsigned long start, unsigned long end)
{
	unsigned long i = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile("dcache.cval1 %0\n"::"r"(i):"memory");
	sync_is();
}

void cache_wbinv_range(unsigned long start, unsigned long end)
{
	dcache_wb_range(start, end);
	icache_inv_range(start, end);
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
