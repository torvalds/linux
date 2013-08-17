/*
 * 2006-2009 (C) DENX Software Engineering.
 *
 * Author: Yuri Tikhonov <yur@emcraft.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */

#ifndef _PPC440SPE_ADMA_H
#define _PPC440SPE_ADMA_H

#include <linux/types.h>
#include "dma.h"
#include "xor.h"

#define to_ppc440spe_adma_chan(chan) \
		container_of(chan, struct ppc440spe_adma_chan, common)
#define to_ppc440spe_adma_device(dev) \
		container_of(dev, struct ppc440spe_adma_device, common)
#define tx_to_ppc440spe_adma_slot(tx) \
		container_of(tx, struct ppc440spe_adma_desc_slot, async_tx)

/* Default polynomial (for 440SP is only available) */
#define PPC440SPE_DEFAULT_POLY	0x4d

#define PPC440SPE_ADMA_ENGINES_NUM	(XOR_ENGINES_NUM + DMA_ENGINES_NUM)

#define PPC440SPE_ADMA_WATCHDOG_MSEC	3
#define PPC440SPE_ADMA_THRESHOLD	1

#define PPC440SPE_DMA0_ID	0
#define PPC440SPE_DMA1_ID	1
#define PPC440SPE_XOR_ID	2

#define PPC440SPE_ADMA_DMA_MAX_BYTE_COUNT	0xFFFFFFUL
/* this is the XOR_CBBCR width */
#define PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT	(1 << 31)
#define PPC440SPE_ADMA_ZERO_SUM_MAX_BYTE_COUNT PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT

#define PPC440SPE_RXOR_RUN	0

#define MQ0_CF2H_RXOR_BS_MASK	0x1FF

#undef ADMA_LL_DEBUG

/**
 * struct ppc440spe_adma_device - internal representation of an ADMA device
 * @dev: device
 * @dma_reg: base for DMAx register access
 * @xor_reg: base for XOR register access
 * @i2o_reg: base for I2O register access
 * @id: HW ADMA Device selector
 * @dma_desc_pool_virt: base of DMA descriptor region (CPU address)
 * @dma_desc_pool: base of DMA descriptor region (DMA address)
 * @pool_size: size of the pool
 * @irq: DMAx or XOR irq number
 * @err_irq: DMAx error irq number
 * @common: embedded struct dma_device
 */
struct ppc440spe_adma_device {
	struct device *dev;
	struct dma_regs __iomem *dma_reg;
	struct xor_regs __iomem *xor_reg;
	struct i2o_regs __iomem *i2o_reg;
	int id;
	void *dma_desc_pool_virt;
	dma_addr_t dma_desc_pool;
	size_t pool_size;
	int irq;
	int err_irq;
	struct dma_device common;
};

/**
 * struct ppc440spe_adma_chan - internal representation of an ADMA channel
 * @lock: serializes enqueue/dequeue operations to the slot pool
 * @device: parent device
 * @chain: device chain view of the descriptors
 * @common: common dmaengine channel object members
 * @all_slots: complete domain of slots usable by the channel
 * @pending: allows batching of hardware operations
 * @slots_allocated: records the actual size of the descriptor slot pool
 * @hw_chain_inited: h/w descriptor chain initialization flag
 * @irq_tasklet: bottom half where ppc440spe_adma_slot_cleanup runs
 * @needs_unmap: if buffers should not be unmapped upon final processing
 * @pdest_page: P destination page for async validate operation
 * @qdest_page: Q destination page for async validate operation
 * @pdest: P dma addr for async validate operation
 * @qdest: Q dma addr for async validate operation
 */
struct ppc440spe_adma_chan {
	spinlock_t lock;
	struct ppc440spe_adma_device *device;
	struct list_head chain;
	struct dma_chan common;
	struct list_head all_slots;
	struct ppc440spe_adma_desc_slot *last_used;
	int pending;
	int slots_allocated;
	int hw_chain_inited;
	struct tasklet_struct irq_tasklet;
	u8 needs_unmap;
	struct page *pdest_page;
	struct page *qdest_page;
	dma_addr_t pdest;
	dma_addr_t qdest;
};

struct ppc440spe_rxor {
	u32 addrl;
	u32 addrh;
	int len;
	int xor_count;
	int addr_count;
	int desc_count;
	int state;
};

/**
 * struct ppc440spe_adma_desc_slot - PPC440SPE-ADMA software descriptor
 * @phys: hardware address of the hardware descriptor chain
 * @group_head: first operation in a transaction
 * @hw_next: pointer to the next descriptor in chain
 * @async_tx: support for the async_tx api
 * @slot_node: node on the iop_adma_chan.all_slots list
 * @chain_node: node on the op_adma_chan.chain list
 * @group_list: list of slots that make up a multi-descriptor transaction
 *              for example transfer lengths larger than the supported hw max
 * @unmap_len: transaction bytecount
 * @hw_desc: virtual address of the hardware descriptor chain
 * @stride: currently chained or not
 * @idx: pool index
 * @slot_cnt: total slots used in an transaction (group of operations)
 * @src_cnt: number of sources set in this descriptor
 * @dst_cnt: number of destinations set in the descriptor
 * @slots_per_op: number of slots per operation
 * @descs_per_op: number of slot per P/Q operation see comment
 *                for ppc440spe_prep_dma_pqxor function
 * @flags: desc state/type
 * @reverse_flags: 1 if a corresponding rxor address uses reversed address order
 * @xor_check_result: result of zero sum
 * @crc32_result: result crc calculation
 */
struct ppc440spe_adma_desc_slot {
	dma_addr_t phys;
	struct ppc440spe_adma_desc_slot *group_head;
	struct ppc440spe_adma_desc_slot *hw_next;
	struct dma_async_tx_descriptor async_tx;
	struct list_head slot_node;
	struct list_head chain_node; /* node in channel ops list */
	struct list_head group_list; /* list */
	unsigned int unmap_len;
	void *hw_desc;
	u16 stride;
	u16 idx;
	u16 slot_cnt;
	u8 src_cnt;
	u8 dst_cnt;
	u8 slots_per_op;
	u8 descs_per_op;
	unsigned long flags;
	unsigned long reverse_flags[8];

#define PPC440SPE_DESC_INT	0	/* generate interrupt on complete */
#define PPC440SPE_ZERO_P	1	/* clear P destionaion */
#define PPC440SPE_ZERO_Q	2	/* clear Q destination */
#define PPC440SPE_COHERENT	3	/* src/dst are coherent */

#define PPC440SPE_DESC_WXOR	4	/* WXORs are in chain */
#define PPC440SPE_DESC_RXOR	5	/* RXOR is in chain */

#define PPC440SPE_DESC_RXOR123	8	/* CDB for RXOR123 operation */
#define PPC440SPE_DESC_RXOR124	9	/* CDB for RXOR124 operation */
#define PPC440SPE_DESC_RXOR125	10	/* CDB for RXOR125 operation */
#define PPC440SPE_DESC_RXOR12	11	/* CDB for RXOR12 operation */
#define PPC440SPE_DESC_RXOR_REV	12	/* CDB has srcs in reversed order */

#define PPC440SPE_DESC_PCHECK	13
#define PPC440SPE_DESC_QCHECK	14

#define PPC440SPE_DESC_RXOR_MSK	0x3

	struct ppc440spe_rxor rxor_cursor;

	union {
		u32 *xor_check_result;
		u32 *crc32_result;
	};
};

#endif /* _PPC440SPE_ADMA_H */
