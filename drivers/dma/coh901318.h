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

struct device;

struct coh901318_pool {
	spinlock_t lock;
	struct dma_pool *dmapool;
	struct device *dev;

#ifdef CONFIG_DEBUG_FS
	int debugfs_pool_counter;
#endif
};

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

/**
 * coh901318_pool_create() - Creates an dma pool for lli:s
 * @pool: pool handle
 * @dev: dma device
 * @lli_nbr: number of lli:s in the pool
 * @algin: address alignemtn of lli:s
 * returns 0 on success otherwise none zero
 */
int coh901318_pool_create(struct coh901318_pool *pool,
			  struct device *dev,
			  size_t lli_nbr, size_t align);

/**
 * coh901318_pool_destroy() - Destroys the dma pool
 * @pool: pool handle
 * returns 0 on success otherwise none zero
 */
int coh901318_pool_destroy(struct coh901318_pool *pool);

/**
 * coh901318_lli_alloc() - Allocates a linked list
 *
 * @pool: pool handle
 * @len: length to list
 * return: none NULL if success otherwise NULL
 */
struct coh901318_lli *
coh901318_lli_alloc(struct coh901318_pool *pool,
		    unsigned int len);

/**
 * coh901318_lli_free() - Returns the linked list items to the pool
 * @pool: pool handle
 * @lli: reference to lli pointer to be freed
 */
void coh901318_lli_free(struct coh901318_pool *pool,
			struct coh901318_lli **lli);

/**
 * coh901318_lli_fill_memcpy() - Prepares the lli:s for dma memcpy
 * @pool: pool handle
 * @lli: allocated lli
 * @src: src address
 * @size: transfer size
 * @dst: destination address
 * @ctrl_chained: ctrl for chained lli
 * @ctrl_last: ctrl for the last lli
 * returns number of CPU interrupts for the lli, negative on error.
 */
int
coh901318_lli_fill_memcpy(struct coh901318_pool *pool,
			  struct coh901318_lli *lli,
			  dma_addr_t src, unsigned int size,
			  dma_addr_t dst, u32 ctrl_chained, u32 ctrl_last);

/**
 * coh901318_lli_fill_single() - Prepares the lli:s for dma single transfer
 * @pool: pool handle
 * @lli: allocated lli
 * @buf: transfer buffer
 * @size: transfer size
 * @dev_addr: address of periphal
 * @ctrl_chained: ctrl for chained lli
 * @ctrl_last: ctrl for the last lli
 * @dir: direction of transfer (to or from device)
 * returns number of CPU interrupts for the lli, negative on error.
 */
int
coh901318_lli_fill_single(struct coh901318_pool *pool,
			  struct coh901318_lli *lli,
			  dma_addr_t buf, unsigned int size,
			  dma_addr_t dev_addr, u32 ctrl_chained, u32 ctrl_last,
			  enum dma_transfer_direction dir);

/**
 * coh901318_lli_fill_single() - Prepares the lli:s for dma scatter list transfer
 * @pool: pool handle
 * @lli: allocated lli
 * @sg: scatter gather list
 * @nents: number of entries in sg
 * @dev_addr: address of periphal
 * @ctrl_chained: ctrl for chained lli
 * @ctrl: ctrl of middle lli
 * @ctrl_last: ctrl for the last lli
 * @dir: direction of transfer (to or from device)
 * @ctrl_irq_mask: ctrl mask for CPU interrupt
 * returns number of CPU interrupts for the lli, negative on error.
 */
int
coh901318_lli_fill_sg(struct coh901318_pool *pool,
		      struct coh901318_lli *lli,
		      struct scatterlist *sg, unsigned int nents,
		      dma_addr_t dev_addr, u32 ctrl_chained,
		      u32 ctrl, u32 ctrl_last,
		      enum dma_transfer_direction dir, u32 ctrl_irq_mask);

#endif /* COH901318_H */
