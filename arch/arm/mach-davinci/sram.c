// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mach-davinci/sram.c - DaVinci simple SRAM allocator
 *
 * Copyright (C) 2009 David Brownell
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/genalloc.h>

#include <mach/common.h>
#include "sram.h"

static struct gen_pool *sram_pool;

struct gen_pool *sram_get_gen_pool(void)
{
	return sram_pool;
}

void *sram_alloc(size_t len, dma_addr_t *dma)
{
	dma_addr_t dma_base = davinci_soc_info.sram_dma;

	if (dma)
		*dma = 0;
	if (!sram_pool || (dma && !dma_base))
		return NULL;

	return gen_pool_dma_alloc(sram_pool, len, dma);

}
EXPORT_SYMBOL(sram_alloc);

void sram_free(void *addr, size_t len)
{
	gen_pool_free(sram_pool, (unsigned long) addr, len);
}
EXPORT_SYMBOL(sram_free);


/*
 * REVISIT This supports CPU and DMA access to/from SRAM, but it
 * doesn't (yet?) support some other notable uses of SRAM:  as TCM
 * for data and/or instructions; and holding code needed to enter
 * and exit suspend states (while DRAM can't be used).
 */
static int __init sram_init(void)
{
	phys_addr_t phys = davinci_soc_info.sram_dma;
	unsigned len = davinci_soc_info.sram_len;
	int status = 0;
	void __iomem *addr;

	if (len) {
		len = min_t(unsigned, len, SRAM_SIZE);
		sram_pool = gen_pool_create(ilog2(SRAM_GRANULARITY), -1);
		if (!sram_pool)
			status = -ENOMEM;
	}

	if (sram_pool) {
		addr = ioremap(phys, len);
		if (!addr)
			return -ENOMEM;
		status = gen_pool_add_virt(sram_pool, (unsigned long) addr,
					   phys, len, -1);
		if (status < 0)
			iounmap(addr);
	}

	WARN_ON(status < 0);
	return status;
}
core_initcall(sram_init);

