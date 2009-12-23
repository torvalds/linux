/*
 *
 * include/linux/coh901318.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * DMA driver for COH 901 318
 * Author: Per Friden <per.friden@stericsson.com>
 */

#ifndef COH901318_H
#define COH901318_H

#include <linux/device.h>
#include <linux/dmaengine.h>

#define MAX_DMA_PACKET_SIZE_SHIFT 11
#define MAX_DMA_PACKET_SIZE (1 << MAX_DMA_PACKET_SIZE_SHIFT)

/**
 * struct coh901318_lli - linked list item for DMAC
 * @control: control settings for DMAC
 * @src_addr: transfer source address
 * @dst_addr: transfer destination address
 * @link_addr:  physical address to next lli
 * @virt_link_addr: virtual addres of next lli (only used by pool_free)
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
 * struct coh901318_params - parameters for DMAC configuration
 * @config: DMA config register
 * @ctrl_lli_last: DMA control register for the last lli in the list
 * @ctrl_lli: DMA control register for an lli
 * @ctrl_lli_chained: DMA control register for a chained lli
 */
struct coh901318_params {
	u32 config;
	u32 ctrl_lli_last;
	u32 ctrl_lli;
	u32 ctrl_lli_chained;
};
/**
 * struct coh_dma_channel - dma channel base
 * @name: ascii name of dma channel
 * @number: channel id number
 * @desc_nbr_max: number of preallocated descriptortors
 * @priority_high: prio of channel, 0 low otherwise high.
 * @param: configuration parameters
 * @dev_addr: physical address of periphal connected to channel
 */
struct coh_dma_channel {
	const char name[32];
	const int number;
	const int desc_nbr_max;
	const int priority_high;
	const struct coh901318_params param;
	const dma_addr_t dev_addr;
};

/**
 * dma_access_memory_state_t - register dma for memory access
 *
 * @dev: The dma device
 * @active:  1 means dma intends to access memory
 *           0 means dma wont access memory
 */
typedef void (*dma_access_memory_state_t)(struct device *dev,
					  bool active);

/**
 * struct powersave - DMA power save structure
 * @lock: lock protecting data in this struct
 * @started_channels: bit mask indicating active dma channels
 */
struct powersave {
	spinlock_t lock;
	u64 started_channels;
};
/**
 * struct coh901318_platform - platform arch structure
 * @chans_slave: specifying dma slave channels
 * @chans_memcpy: specifying dma memcpy channels
 * @access_memory_state: requesting DMA memeory access (on / off)
 * @chan_conf: dma channel configurations
 * @max_channels: max number of dma chanenls
 */
struct coh901318_platform {
	const int *chans_slave;
	const int *chans_memcpy;
	const dma_access_memory_state_t access_memory_state;
	const struct coh_dma_channel *chan_conf;
	const int max_channels;
};

/**
 * coh901318_get_bytes_left() - Get number of bytes left on a current transfer
 * @chan: dma channel handle
 * return number of bytes left, or negative on error
 */
u32 coh901318_get_bytes_left(struct dma_chan *chan);

/**
 * coh901318_stop() - Stops dma transfer
 * @chan: dma channel handle
 * return 0 on success otherwise negative value
 */
void coh901318_stop(struct dma_chan *chan);

/**
 * coh901318_continue() - Resumes a stopped dma transfer
 * @chan: dma channel handle
 * return 0 on success otherwise negative value
 */
void coh901318_continue(struct dma_chan *chan);

/**
 * coh901318_filter_id() - DMA channel filter function
 * @chan: dma channel handle
 * @chan_id: id of dma channel to be filter out
 *
 * In dma_request_channel() it specifies what channel id to be requested
 */
bool coh901318_filter_id(struct dma_chan *chan, void *chan_id);

/*
 * DMA Controller - this access the static mappings of the coh901318 dma.
 *
 */

#define COH901318_MOD32_MASK					(0x1F)
#define COH901318_WORD_MASK					(0xFFFFFFFF)
/* INT_STATUS - Interrupt Status Registers 32bit (R/-) */
#define COH901318_INT_STATUS1					(0x0000)
#define COH901318_INT_STATUS2					(0x0004)
/* TC_INT_STATUS - Terminal Count Interrupt Status Registers 32bit (R/-) */
#define COH901318_TC_INT_STATUS1				(0x0008)
#define COH901318_TC_INT_STATUS2				(0x000C)
/* TC_INT_CLEAR - Terminal Count Interrupt Clear Registers 32bit (-/W) */
#define COH901318_TC_INT_CLEAR1					(0x0010)
#define COH901318_TC_INT_CLEAR2					(0x0014)
/* RAW_TC_INT_STATUS - Raw Term Count Interrupt Status Registers 32bit (R/-) */
#define COH901318_RAW_TC_INT_STATUS1				(0x0018)
#define COH901318_RAW_TC_INT_STATUS2				(0x001C)
/* BE_INT_STATUS - Bus Error Interrupt Status Registers 32bit (R/-) */
#define COH901318_BE_INT_STATUS1				(0x0020)
#define COH901318_BE_INT_STATUS2				(0x0024)
/* BE_INT_CLEAR - Bus Error Interrupt Clear Registers 32bit (-/W) */
#define COH901318_BE_INT_CLEAR1					(0x0028)
#define COH901318_BE_INT_CLEAR2					(0x002C)
/* RAW_BE_INT_STATUS - Raw Term Count Interrupt Status Registers 32bit (R/-) */
#define COH901318_RAW_BE_INT_STATUS1				(0x0030)
#define COH901318_RAW_BE_INT_STATUS2				(0x0034)

/*
 * CX_CFG - Channel Configuration Registers 32bit (R/W)
 */
#define COH901318_CX_CFG					(0x0100)
#define COH901318_CX_CFG_SPACING				(0x04)
/* Channel enable activates tha dma job */
#define COH901318_CX_CFG_CH_ENABLE				(0x00000001)
#define COH901318_CX_CFG_CH_DISABLE				(0x00000000)
/* Request Mode */
#define COH901318_CX_CFG_RM_MASK				(0x00000006)
#define COH901318_CX_CFG_RM_MEMORY_TO_MEMORY			(0x0 << 1)
#define COH901318_CX_CFG_RM_PRIMARY_TO_MEMORY			(0x1 << 1)
#define COH901318_CX_CFG_RM_MEMORY_TO_PRIMARY			(0x1 << 1)
#define COH901318_CX_CFG_RM_PRIMARY_TO_SECONDARY		(0x3 << 1)
#define COH901318_CX_CFG_RM_SECONDARY_TO_PRIMARY		(0x3 << 1)
/* Linked channel request field. RM must == 11 */
#define COH901318_CX_CFG_LCRF_SHIFT				3
#define COH901318_CX_CFG_LCRF_MASK				(0x000001F8)
#define COH901318_CX_CFG_LCR_DISABLE				(0x00000000)
/* Terminal Counter Interrupt Request Mask */
#define COH901318_CX_CFG_TC_IRQ_ENABLE				(0x00000200)
#define COH901318_CX_CFG_TC_IRQ_DISABLE				(0x00000000)
/* Bus Error interrupt Mask */
#define COH901318_CX_CFG_BE_IRQ_ENABLE				(0x00000400)
#define COH901318_CX_CFG_BE_IRQ_DISABLE				(0x00000000)

/*
 * CX_STAT - Channel Status Registers 32bit (R/-)
 */
#define COH901318_CX_STAT					(0x0200)
#define COH901318_CX_STAT_SPACING				(0x04)
#define COH901318_CX_STAT_RBE_IRQ_IND				(0x00000008)
#define COH901318_CX_STAT_RTC_IRQ_IND				(0x00000004)
#define COH901318_CX_STAT_ACTIVE				(0x00000002)
#define COH901318_CX_STAT_ENABLED				(0x00000001)

/*
 * CX_CTRL - Channel Control Registers 32bit (R/W)
 */
#define COH901318_CX_CTRL					(0x0400)
#define COH901318_CX_CTRL_SPACING				(0x10)
/* Transfer Count Enable */
#define COH901318_CX_CTRL_TC_ENABLE				(0x00001000)
#define COH901318_CX_CTRL_TC_DISABLE				(0x00000000)
/* Transfer Count Value 0 - 4095 */
#define COH901318_CX_CTRL_TC_VALUE_MASK				(0x00000FFF)
/* Burst count */
#define COH901318_CX_CTRL_BURST_COUNT_MASK			(0x0000E000)
#define COH901318_CX_CTRL_BURST_COUNT_64_BYTES			(0x7 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_48_BYTES			(0x6 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_32_BYTES			(0x5 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_16_BYTES			(0x4 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_8_BYTES			(0x3 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_4_BYTES			(0x2 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_2_BYTES			(0x1 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_1_BYTE			(0x0 << 13)
/* Source bus size  */
#define COH901318_CX_CTRL_SRC_BUS_SIZE_MASK			(0x00030000)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_32_BITS			(0x2 << 16)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_16_BITS			(0x1 << 16)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_8_BITS			(0x0 << 16)
/* Source address increment */
#define COH901318_CX_CTRL_SRC_ADDR_INC_ENABLE			(0x00040000)
#define COH901318_CX_CTRL_SRC_ADDR_INC_DISABLE			(0x00000000)
/* Destination Bus Size */
#define COH901318_CX_CTRL_DST_BUS_SIZE_MASK			(0x00180000)
#define COH901318_CX_CTRL_DST_BUS_SIZE_32_BITS			(0x2 << 19)
#define COH901318_CX_CTRL_DST_BUS_SIZE_16_BITS			(0x1 << 19)
#define COH901318_CX_CTRL_DST_BUS_SIZE_8_BITS			(0x0 << 19)
/* Destination address increment */
#define COH901318_CX_CTRL_DST_ADDR_INC_ENABLE			(0x00200000)
#define COH901318_CX_CTRL_DST_ADDR_INC_DISABLE			(0x00000000)
/* Master Mode (Master2 is only connected to MSL) */
#define COH901318_CX_CTRL_MASTER_MODE_MASK			(0x00C00000)
#define COH901318_CX_CTRL_MASTER_MODE_M2R_M1W			(0x3 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M1R_M2W			(0x2 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M2RW			(0x1 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M1RW			(0x0 << 22)
/* Terminal Count flag to PER enable */
#define COH901318_CX_CTRL_TCP_ENABLE				(0x01000000)
#define COH901318_CX_CTRL_TCP_DISABLE				(0x00000000)
/* Terminal Count flags to CPU enable */
#define COH901318_CX_CTRL_TC_IRQ_ENABLE				(0x02000000)
#define COH901318_CX_CTRL_TC_IRQ_DISABLE			(0x00000000)
/* Hand shake to peripheral */
#define COH901318_CX_CTRL_HSP_ENABLE				(0x04000000)
#define COH901318_CX_CTRL_HSP_DISABLE				(0x00000000)
#define COH901318_CX_CTRL_HSS_ENABLE				(0x08000000)
#define COH901318_CX_CTRL_HSS_DISABLE				(0x00000000)
/* DMA mode */
#define COH901318_CX_CTRL_DDMA_MASK				(0x30000000)
#define COH901318_CX_CTRL_DDMA_LEGACY				(0x0 << 28)
#define COH901318_CX_CTRL_DDMA_DEMAND_DMA1			(0x1 << 28)
#define COH901318_CX_CTRL_DDMA_DEMAND_DMA2			(0x2 << 28)
/* Primary Request Data Destination */
#define COH901318_CX_CTRL_PRDD_MASK				(0x40000000)
#define COH901318_CX_CTRL_PRDD_DEST				(0x1 << 30)
#define COH901318_CX_CTRL_PRDD_SOURCE				(0x0 << 30)

/*
 * CX_SRC_ADDR - Channel Source Address Registers 32bit (R/W)
 */
#define COH901318_CX_SRC_ADDR					(0x0404)
#define COH901318_CX_SRC_ADDR_SPACING				(0x10)

/*
 * CX_DST_ADDR - Channel Destination Address Registers 32bit R/W
 */
#define COH901318_CX_DST_ADDR					(0x0408)
#define COH901318_CX_DST_ADDR_SPACING				(0x10)

/*
 * CX_LNK_ADDR - Channel Link Address Registers 32bit (R/W)
 */
#define COH901318_CX_LNK_ADDR					(0x040C)
#define COH901318_CX_LNK_ADDR_SPACING				(0x10)
#define COH901318_CX_LNK_LINK_IMMEDIATE				(0x00000001)
#endif /* COH901318_H */
