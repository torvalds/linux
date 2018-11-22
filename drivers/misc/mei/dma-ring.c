// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2016 - 2018 Intel Corporation. All rights reserved.
 */
#include <linux/dma-mapping.h>
#include <linux/mei.h>

#include "mei_dev.h"

/**
 * mei_dmam_dscr_alloc() - allocate a managed coherent buffer
 *     for the dma descriptor
 * @dev: mei_device
 * @dscr: dma descriptor
 *
 * Return:
 * * 0       - on success or zero allocation request
 * * -EINVAL - if size is not power of 2
 * * -ENOMEM - of allocation has failed
 */
static int mei_dmam_dscr_alloc(struct mei_device *dev,
			       struct mei_dma_dscr *dscr)
{
	if (!dscr->size)
		return 0;

	if (WARN_ON(!is_power_of_2(dscr->size)))
		return -EINVAL;

	if (dscr->vaddr)
		return 0;

	dscr->vaddr = dmam_alloc_coherent(dev->dev, dscr->size, &dscr->daddr,
					  GFP_KERNEL);
	if (!dscr->vaddr)
		return -ENOMEM;

	return 0;
}

/**
 * mei_dmam_dscr_free() - free a managed coherent buffer
 *     from the dma descriptor
 * @dev: mei_device
 * @dscr: dma descriptor
 */
static void mei_dmam_dscr_free(struct mei_device *dev,
			       struct mei_dma_dscr *dscr)
{
	if (!dscr->vaddr)
		return;

	dmam_free_coherent(dev->dev, dscr->size, dscr->vaddr, dscr->daddr);
	dscr->vaddr = NULL;
}

/**
 * mei_dmam_ring_free() - free dma ring buffers
 * @dev: mei device
 */
void mei_dmam_ring_free(struct mei_device *dev)
{
	int i;

	for (i = 0; i < DMA_DSCR_NUM; i++)
		mei_dmam_dscr_free(dev, &dev->dr_dscr[i]);
}

/**
 * mei_dmam_ring_alloc() - allocate dma ring buffers
 * @dev: mei device
 *
 * Return: -ENOMEM on allocation failure 0 otherwise
 */
int mei_dmam_ring_alloc(struct mei_device *dev)
{
	int i;

	for (i = 0; i < DMA_DSCR_NUM; i++)
		if (mei_dmam_dscr_alloc(dev, &dev->dr_dscr[i]))
			goto err;

	return 0;

err:
	mei_dmam_ring_free(dev);
	return -ENOMEM;
}

/**
 * mei_dma_ring_is_allocated() - check if dma ring is allocated
 * @dev: mei device
 *
 * Return: true if dma ring is allocated
 */
bool mei_dma_ring_is_allocated(struct mei_device *dev)
{
	return !!dev->dr_dscr[DMA_DSCR_HOST].vaddr;
}
