/*
 * Copyright (C) 2007-2013 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * DMA driver for COH 901 318
 * Author: Per Friden <per.friden@stericsson.com>
 */

#ifndef COH901318_H
#define COH901318_H

#define MAX_DMA_PACKET_SIZE_SHIFT 11
#define MAX_DMA_PACKET_SIZE (1 << MAX_DMA_PACKET_SIZE_SHIFT)

/**
 * struct coh901318_lli - linked list item for DMAC
 * @control: control settings for DMAC
 * @src_addr: transfer source address
 * @dst_addr: transfer destination address
 * @link_addr:  physical address to next lli
 * @virt_link_addr: virtual address of next lli (only used by pool_free)
 * @phy_this: physical address of current lli (only used by pool_free)
 */
struct coh901318_lli {
	u32 control;
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	dma_addr_t link_addr;

	void *virt_link_addr;
	dma_addr_t phy_this;
};

#endif /* COH901318_H */
