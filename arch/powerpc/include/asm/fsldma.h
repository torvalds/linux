/*
 * Freescale MPC83XX / MPC85XX DMA Controller
 *
 * Copyright (c) 2009 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ARCH_POWERPC_ASM_FSLDMA_H__
#define __ARCH_POWERPC_ASM_FSLDMA_H__

#include <linux/dmaengine.h>

/*
 * Definitions for the Freescale DMA controller's DMA_SLAVE implemention
 *
 * The Freescale DMA_SLAVE implementation was designed to handle many-to-many
 * transfers. An example usage would be an accelerated copy between two
 * scatterlists. Another example use would be an accelerated copy from
 * multiple non-contiguous device buffers into a single scatterlist.
 *
 * A DMA_SLAVE transaction is defined by a struct fsl_dma_slave. This
 * structure contains a list of hardware addresses that should be copied
 * to/from the scatterlist passed into device_prep_slave_sg(). The structure
 * also has some fields to enable hardware-specific features.
 */

/**
 * struct fsl_dma_hw_addr
 * @entry: linked list entry
 * @address: the hardware address
 * @length: length to transfer
 *
 * Holds a single physical hardware address / length pair for use
 * with the DMAEngine DMA_SLAVE API.
 */
struct fsl_dma_hw_addr {
	struct list_head entry;

	dma_addr_t address;
	size_t length;
};

/**
 * struct fsl_dma_slave
 * @addresses: a linked list of struct fsl_dma_hw_addr structures
 * @request_count: value for DMA request count
 * @src_loop_size: setup and enable constant source-address DMA transfers
 * @dst_loop_size: setup and enable constant destination address DMA transfers
 * @external_start: enable externally started DMA transfers
 * @external_pause: enable externally paused DMA transfers
 *
 * Holds a list of address / length pairs for use with the DMAEngine
 * DMA_SLAVE API implementation for the Freescale DMA controller.
 */
struct fsl_dma_slave {

	/* List of hardware address/length pairs */
	struct list_head addresses;

	/* Support for extra controller features */
	unsigned int request_count;
	unsigned int src_loop_size;
	unsigned int dst_loop_size;
	bool external_start;
	bool external_pause;
};

/**
 * fsl_dma_slave_append - add an address/length pair to a struct fsl_dma_slave
 * @slave: the &struct fsl_dma_slave to add to
 * @address: the hardware address to add
 * @length: the length of bytes to transfer from @address
 *
 * Add a hardware address/length pair to a struct fsl_dma_slave. Returns 0 on
 * success, -ERRNO otherwise.
 */
static inline int fsl_dma_slave_append(struct fsl_dma_slave *slave,
				       dma_addr_t address, size_t length)
{
	struct fsl_dma_hw_addr *addr;

	addr = kzalloc(sizeof(*addr), GFP_ATOMIC);
	if (!addr)
		return -ENOMEM;

	INIT_LIST_HEAD(&addr->entry);
	addr->address = address;
	addr->length = length;

	list_add_tail(&addr->entry, &slave->addresses);
	return 0;
}

/**
 * fsl_dma_slave_free - free a struct fsl_dma_slave
 * @slave: the struct fsl_dma_slave to free
 *
 * Free a struct fsl_dma_slave and all associated address/length pairs
 */
static inline void fsl_dma_slave_free(struct fsl_dma_slave *slave)
{
	struct fsl_dma_hw_addr *addr, *tmp;

	if (slave) {
		list_for_each_entry_safe(addr, tmp, &slave->addresses, entry) {
			list_del(&addr->entry);
			kfree(addr);
		}

		kfree(slave);
	}
}

/**
 * fsl_dma_slave_alloc - allocate a struct fsl_dma_slave
 * @gfp: the flags to pass to kmalloc when allocating this structure
 *
 * Allocate a struct fsl_dma_slave for use by the DMA_SLAVE API. Returns a new
 * struct fsl_dma_slave on success, or NULL on failure.
 */
static inline struct fsl_dma_slave *fsl_dma_slave_alloc(gfp_t gfp)
{
	struct fsl_dma_slave *slave;

	slave = kzalloc(sizeof(*slave), gfp);
	if (!slave)
		return NULL;

	INIT_LIST_HEAD(&slave->addresses);
	return slave;
}

#endif /* __ARCH_POWERPC_ASM_FSLDMA_H__ */
