// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/device.h>

#include "ipu3.h"
#include "ipu3-css-pool.h"
#include "ipu3-dmamap.h"

int ipu3_css_dma_buffer_resize(struct imgu_device *imgu,
			       struct ipu3_css_map *map, size_t size)
{
	if (map->size < size && map->vaddr) {
		dev_warn(&imgu->pci_dev->dev, "dma buf resized from %zu to %zu",
			 map->size, size);

		ipu3_dmamap_free(imgu, map);
		if (!ipu3_dmamap_alloc(imgu, map, size))
			return -ENOMEM;
	}

	return 0;
}

void ipu3_css_pool_cleanup(struct imgu_device *imgu, struct ipu3_css_pool *pool)
{
	unsigned int i;

	for (i = 0; i < IPU3_CSS_POOL_SIZE; i++)
		ipu3_dmamap_free(imgu, &pool->entry[i].param);
}

int ipu3_css_pool_init(struct imgu_device *imgu, struct ipu3_css_pool *pool,
		       size_t size)
{
	unsigned int i;

	for (i = 0; i < IPU3_CSS_POOL_SIZE; i++) {
		pool->entry[i].valid = false;
		if (size == 0) {
			pool->entry[i].param.vaddr = NULL;
			continue;
		}

		if (!ipu3_dmamap_alloc(imgu, &pool->entry[i].param, size))
			goto fail;
	}

	pool->last = IPU3_CSS_POOL_SIZE;

	return 0;

fail:
	ipu3_css_pool_cleanup(imgu, pool);
	return -ENOMEM;
}

/*
 * Allocate a new parameter via recycling the oldest entry in the pool.
 */
void ipu3_css_pool_get(struct ipu3_css_pool *pool)
{
	/* Get the oldest entry */
	u32 n = (pool->last + 1) % IPU3_CSS_POOL_SIZE;

	pool->entry[n].valid = true;
	pool->last = n;
}

/*
 * Undo, for all practical purposes, the effect of pool_get().
 */
void ipu3_css_pool_put(struct ipu3_css_pool *pool)
{
	pool->entry[pool->last].valid = false;
	pool->last = (pool->last + IPU3_CSS_POOL_SIZE - 1) % IPU3_CSS_POOL_SIZE;
}

/**
 * ipu3_css_pool_last - Retrieve the nth pool entry from last
 *
 * @pool: a pointer to &struct ipu3_css_pool.
 * @n: the distance to the last index.
 *
 * Returns:
 *  The nth entry from last or null map to indicate no frame stored.
 */
const struct ipu3_css_map *
ipu3_css_pool_last(struct ipu3_css_pool *pool, unsigned int n)
{
	static const struct ipu3_css_map null_map = { 0 };
	int i = (pool->last + IPU3_CSS_POOL_SIZE - n) % IPU3_CSS_POOL_SIZE;

	WARN_ON(n >= IPU3_CSS_POOL_SIZE);

	if (!pool->entry[i].valid)
		return &null_map;

	return &pool->entry[i].param;
}
