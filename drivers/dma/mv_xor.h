/*
 * Copyright (C) 2007, 2008, Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef MV_XOR_H
#define MV_XOR_H

#include <linux/types.h>
#include <linux/io.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>

#define MV_XOR_POOL_SIZE		PAGE_SIZE
#define MV_XOR_SLOT_SIZE		64
#define MV_XOR_THRESHOLD		1
#define MV_XOR_MAX_CHANNELS             2

#define MV_XOR_MIN_BYTE_COUNT		SZ_128
#define MV_XOR_MAX_BYTE_COUNT		(SZ_16M - 1)

/* Values for the XOR_CONFIG register */
#define XOR_OPERATION_MODE_XOR		0
#define XOR_OPERATION_MODE_MEMCPY	2
#define XOR_DESCRIPTOR_SWAP		BIT(14)
#define XOR_DESC_SUCCESS		0x40000000

#define XOR_DESC_DMA_OWNED		BIT(31)
#define XOR_DESC_EOD_INT_EN		BIT(31)

#define XOR_CURR_DESC(chan)	(chan->mmr_high_base + 0x10 + (chan->idx * 4))
#define XOR_NEXT_DESC(chan)	(chan->mmr_high_base + 0x00 + (chan->idx * 4))
#define XOR_BYTE_COUNT(chan)	(chan->mmr_high_base + 0x20 + (chan->idx * 4))
#define XOR_DEST_POINTER(chan)	(chan->mmr_high_base + 0xB0 + (chan->idx * 4))
#define XOR_BLOCK_SIZE(chan)	(chan->mmr_high_base + 0xC0 + (chan->idx * 4))
#define XOR_INIT_VALUE_LOW(chan)	(chan->mmr_high_base + 0xE0)
#define XOR_INIT_VALUE_HIGH(chan)	(chan->mmr_high_base + 0xE4)

#define XOR_CONFIG(chan)	(chan->mmr_base + 0x10 + (chan->idx * 4))
#define XOR_ACTIVATION(chan)	(chan->mmr_base + 0x20 + (chan->idx * 4))
#define XOR_INTR_CAUSE(chan)	(chan->mmr_base + 0x30)
#define XOR_INTR_MASK(chan)	(chan->mmr_base + 0x40)
#define XOR_ERROR_CAUSE(chan)	(chan->mmr_base + 0x50)
#define XOR_ERROR_ADDR(chan)	(chan->mmr_base + 0x60)

#define XOR_INT_END_OF_DESC	BIT(0)
#define XOR_INT_END_OF_CHAIN	BIT(1)
#define XOR_INT_STOPPED		BIT(2)
#define XOR_INT_PAUSED		BIT(3)
#define XOR_INT_ERR_DECODE	BIT(4)
#define XOR_INT_ERR_RDPROT	BIT(5)
#define XOR_INT_ERR_WRPROT	BIT(6)
#define XOR_INT_ERR_OWN		BIT(7)
#define XOR_INT_ERR_PAR		BIT(8)
#define XOR_INT_ERR_MBUS	BIT(9)

#define XOR_INTR_ERRORS		(XOR_INT_ERR_DECODE | XOR_INT_ERR_RDPROT | \
				 XOR_INT_ERR_WRPROT | XOR_INT_ERR_OWN    | \
				 XOR_INT_ERR_PAR    | XOR_INT_ERR_MBUS)

#define XOR_INTR_MASK_VALUE	(XOR_INT_END_OF_DESC | XOR_INT_END_OF_CHAIN | \
				 XOR_INT_STOPPED     | XOR_INTR_ERRORS)

#define WINDOW_BASE(w)		(0x50 + ((w) << 2))
#define WINDOW_SIZE(w)		(0x70 + ((w) << 2))
#define WINDOW_REMAP_HIGH(w)	(0x90 + ((w) << 2))
#define WINDOW_BAR_ENABLE(chan)	(0x40 + ((chan) << 2))
#define WINDOW_OVERRIDE_CTRL(chan)	(0xA0 + ((chan) << 2))

struct mv_xor_device {
	void __iomem	     *xor_base;
	void __iomem	     *xor_high_base;
	struct clk	     *clk;
	struct mv_xor_chan   *channels[MV_XOR_MAX_CHANNELS];
};

/**
 * struct mv_xor_chan - internal representation of a XOR channel
 * @pending: allows batching of hardware operations
 * @lock: serializes enqueue/dequeue operations to the descriptors pool
 * @mmr_base: memory mapped register base
 * @idx: the index of the xor channel
 * @chain: device chain view of the descriptors
 * @completed_slots: slots completed by HW but still need to be acked
 * @device: parent device
 * @common: common dmaengine channel object members
 * @last_used: place holder for allocation to continue from where it left off
 * @all_slots: complete domain of slots usable by the channel
 * @slots_allocated: records the actual size of the descriptor slot pool
 * @irq_tasklet: bottom half where mv_xor_slot_cleanup runs
 */
struct mv_xor_chan {
	int			pending;
	spinlock_t		lock; /* protects the descriptor slot pool */
	void __iomem		*mmr_base;
	void __iomem		*mmr_high_base;
	unsigned int		idx;
	int                     irq;
	enum dma_transaction_type	current_type;
	struct list_head	chain;
	struct list_head	completed_slots;
	dma_addr_t		dma_desc_pool;
	void			*dma_desc_pool_virt;
	size_t                  pool_size;
	struct dma_device	dmadev;
	struct dma_chan		dmachan;
	struct mv_xor_desc_slot	*last_used;
	struct list_head	all_slots;
	int			slots_allocated;
	struct tasklet_struct	irq_tasklet;
	char			dummy_src[MV_XOR_MIN_BYTE_COUNT];
	char			dummy_dst[MV_XOR_MIN_BYTE_COUNT];
	dma_addr_t		dummy_src_addr, dummy_dst_addr;
};

/**
 * struct mv_xor_desc_slot - software descriptor
 * @slot_node: node on the mv_xor_chan.all_slots list
 * @chain_node: node on the mv_xor_chan.chain list
 * @completed_node: node on the mv_xor_chan.completed_slots list
 * @hw_desc: virtual address of the hardware descriptor chain
 * @phys: hardware address of the hardware descriptor chain
 * @slot_used: slot in use or not
 * @idx: pool index
 * @tx_list: list of slots that make up a multi-descriptor transaction
 * @async_tx: support for the async_tx api
 */
struct mv_xor_desc_slot {
	struct list_head	slot_node;
	struct list_head	chain_node;
	struct list_head	completed_node;
	enum dma_transaction_type	type;
	void			*hw_desc;
	u16			slot_used;
	u16			idx;
	struct dma_async_tx_descriptor	async_tx;
};

/*
 * This structure describes XOR descriptor size 64bytes. The
 * mv_phy_src_idx() macro must be used when indexing the values of the
 * phy_src_addr[] array. This is due to the fact that the 'descriptor
 * swap' feature, used on big endian systems, swaps descriptors data
 * within blocks of 8 bytes. So two consecutive values of the
 * phy_src_addr[] array are actually swapped in big-endian, which
 * explains the different mv_phy_src_idx() implementation.
 */
#if defined(__LITTLE_ENDIAN)
struct mv_xor_desc {
	u32 status;		/* descriptor execution status */
	u32 crc32_result;	/* result of CRC-32 calculation */
	u32 desc_command;	/* type of operation to be carried out */
	u32 phy_next_desc;	/* next descriptor address pointer */
	u32 byte_count;		/* size of src/dst blocks in bytes */
	u32 phy_dest_addr;	/* destination block address */
	u32 phy_src_addr[8];	/* source block addresses */
	u32 reserved0;
	u32 reserved1;
};
#define mv_phy_src_idx(src_idx) (src_idx)
#else
struct mv_xor_desc {
	u32 crc32_result;	/* result of CRC-32 calculation */
	u32 status;		/* descriptor execution status */
	u32 phy_next_desc;	/* next descriptor address pointer */
	u32 desc_command;	/* type of operation to be carried out */
	u32 phy_dest_addr;	/* destination block address */
	u32 byte_count;		/* size of src/dst blocks in bytes */
	u32 phy_src_addr[8];	/* source block addresses */
	u32 reserved1;
	u32 reserved0;
};
#define mv_phy_src_idx(src_idx) (src_idx ^ 1)
#endif

#define to_mv_sw_desc(addr_hw_desc)		\
	container_of(addr_hw_desc, struct mv_xor_desc_slot, hw_desc)

#define mv_hw_desc_slot_idx(hw_desc, idx)	\
	((void *)(((unsigned long)hw_desc) + ((idx) << 5)))

#endif
