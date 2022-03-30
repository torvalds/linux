// SPDX-License-Identifier: GPL-2.0
/*
 * Early setup for Rockchip DMA CMA
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/cma.h>
#include <linux/dma-map-ops.h>

#include "rk-dma-heap.h"

#define RK_DMA_HEAP_CMA_DEFAULT_SIZE SZ_32M

static unsigned long rk_dma_heap_size __initdata;
static unsigned long rk_dma_heap_base __initdata;

static struct cma *rk_dma_heap_cma;

static int __init early_dma_heap_cma(char *p)
{
	if (!p) {
		pr_err("Config string not provided\n");
		return -EINVAL;
	}

	rk_dma_heap_size = memparse(p, &p);
	if (*p != '@')
		return 0;

	rk_dma_heap_base = memparse(p + 1, &p);

	return 0;
}
early_param("rk_dma_heap_cma", early_dma_heap_cma);

#ifndef CONFIG_DMA_CMA
void __weak
dma_contiguous_early_fixup(phys_addr_t base, unsigned long size)
{
}
#endif

int __init rk_dma_heap_cma_setup(void)
{
	unsigned long size;
	int ret;
	bool fix = false;

	if (rk_dma_heap_size)
		size = rk_dma_heap_size;
	else
		size = RK_DMA_HEAP_CMA_DEFAULT_SIZE;

	if (rk_dma_heap_base)
		fix = true;

	ret = cma_declare_contiguous(rk_dma_heap_base, PAGE_ALIGN(size), 0x0,
				     PAGE_SIZE, 0, fix, "rk-dma-heap-cma",
				     &rk_dma_heap_cma);
	if (ret)
		return ret;

#if !IS_ENABLED(CONFIG_CMA_INACTIVE)
	/* Architecture specific contiguous memory fixup. */
	dma_contiguous_early_fixup(cma_get_base(rk_dma_heap_cma),
	cma_get_size(rk_dma_heap_cma));
#endif

	return 0;
}

struct cma *rk_dma_heap_get_cma(void)
{
	return rk_dma_heap_cma;
}
