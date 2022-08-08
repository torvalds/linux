/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_SG_H
#define BCM_VK_SG_H

#include <linux/dma-mapping.h>

struct bcm_vk_dma {
	/* for userland buffer */
	struct page **pages;
	int nr_pages;

	/* common */
	dma_addr_t handle;
	/*
	 * sglist is of the following LE format
	 * [U32] num_sg  = number of sg addresses (N)
	 * [U32] totalsize = totalsize of data being transferred in sglist
	 * [U32] size[0] = size of data in address0
	 * [U32] addr_l[0] = lower 32-bits of address0
	 * [U32] addr_h[0] = higher 32-bits of address0
	 * ..
	 * [U32] size[N-1] = size of data in addressN-1
	 * [U32] addr_l[N-1] = lower 32-bits of addressN-1
	 * [U32] addr_h[N-1] = higher 32-bits of addressN-1
	 */
	u32 *sglist;
#define SGLIST_NUM_SG		0
#define SGLIST_TOTALSIZE	1
#define SGLIST_VKDATA_START	2

	int sglen; /* Length (bytes) of sglist */
	int direction;
};

struct _vk_data {
	u32 size;    /* data size in bytes */
	u64 address; /* Pointer to data     */
} __packed;

/*
 * Scatter-gather DMA buffer API.
 *
 * These functions provide a simple way to create a page list and a
 * scatter-gather list from userspace address and map the memory
 * for DMA operation.
 */
int bcm_vk_sg_alloc(struct device *dev,
		    struct bcm_vk_dma *dma,
		    int dir,
		    struct _vk_data *vkdata,
		    int num);

int bcm_vk_sg_free(struct device *dev, struct bcm_vk_dma *dma, int num,
		   int *proc_cnt);

#endif

