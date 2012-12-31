/* linux/drivers/media/video/samsung/fimg2d4x/fimg2d_cache.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <plat/cpu.h>
#include "fimg2d.h"

#define L1_CACHE_SIZE		SZ_64K
#define L2_CACHE_SIZE		SZ_1M
#define LINE_FLUSH_THRESHOLD	SZ_1K

/**
 * cache_opr - [kernel] cache operation mode
 * @CACHE_INVAL: do cache invalidate
 * @CACHE_CLEAN: do cache clean for src and msk image
 * @CACHE_FLUSH: do cache clean and invalidate for dst image
 * @CACHE_FLUSH_INNER_ALL: clean and invalidate for innercache
 * @CACHE_FLUSH_ALL: clean and invalidate for whole caches
 */
enum cache_opr {
	CACHE_INVAL,
	CACHE_CLEAN,
	CACHE_FLUSH,
	CACHE_FLUSH_INNER_ALL,
	CACHE_FLUSH_ALL
};

/**
 * @PT_NORMAL: pagetable exists
 * @PT_FAULT: invalid pagetable
 */
enum pt_status {
	PT_NORMAL,
	PT_FAULT,
};

static inline bool is_inner_flushall(size_t size)
{
	if (soc_is_exynos5250())
		return (size >= SZ_1M * 25) ? true : false;
	else
		return (size >= L1_CACHE_SIZE) ? true : false;
}

static inline bool is_outer_flushall(size_t size)
{
	return (size >= L2_CACHE_SIZE) ? true : false;
}

static inline bool is_inner_flushrange(size_t hole)
{
	if (!soc_is_exynos5250())
		return true;
	else {
		if (hole < LINE_FLUSH_THRESHOLD)
			return true;
		else
			return false;	/* line-by-line flush */
	}
}

static inline bool is_outer_flushrange(size_t hole)
{
	if (hole < LINE_FLUSH_THRESHOLD)
		return true;
	else
		return false;	/* line-by-line flush */
}

static inline void fimg2d_dma_sync_inner(unsigned long addr, size_t size, int dir)
{
	if (dir == DMA_TO_DEVICE)
		dmac_map_area((void *)addr, size, dir);
	else if (dir == DMA_BIDIRECTIONAL)
		dmac_flush_range((void *)addr, (void *)(addr + size));
}

static inline void fimg2d_dma_unsync_inner(unsigned long addr, size_t size, int dir)
{
	if (dir == DMA_TO_DEVICE)
		dmac_unmap_area((void *)addr, size, dir);
}

void fimg2d_clean_outer_pagetable(struct mm_struct *mm, unsigned long addr, size_t size);
void fimg2d_dma_sync_outer(struct mm_struct *mm, unsigned long addr, size_t size, enum cache_opr opr);
enum pt_status fimg2d_check_pagetable(struct mm_struct *mm, unsigned long addr, size_t size);
