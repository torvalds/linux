// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMA driver for Xilinx Video DMA Engine
 *
 * Copyright (C) 2010-2014 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 * The AXI Video Direct Memory Access (AXI VDMA) core is a soft Xilinx IP
 * core that provides high-bandwidth direct memory access between memory
 * and AXI4-Stream type video target peripherals. The core provides efficient
 * two dimensional DMA operations with independent asynchronous read (S2MM)
 * and write (MM2S) channel operation. It can be configured to have either
 * one channel or two channels. If configured as two channels, one is to
 * transmit to the video device (MM2S) and another is to receive from the
 * video device (S2MM). Initialization, status, interrupt and management
 * registers are accessed through an AXI4-Lite slave interface.
 *
 * The AXI Direct Memory Access (AXI DMA) core is a soft Xilinx IP core that
 * provides high-bandwidth one dimensional direct memory access between memory
 * and AXI4-Stream target peripherals. It supports one receive and one
 * transmit channel, both of them optional at synthesis time.
 *
 * The AXI CDMA, is a soft IP, which provides high-bandwidth Direct Memory
 * Access (DMA) between a memory-mapped source address and a memory-mapped
 * destination address.
 *
 * The AXI Multichannel Direct Memory Access (AXI MCDMA) core is a soft
 * Xilinx IP that provides high-bandwidth direct memory access between
 * memory and AXI4-Stream target peripherals. It provides scatter gather
 * (SG) interface with multiple channels independent configuration support.
 *
 */

#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "../dmaengine.h"

/* Register/Descriptor Offsets */
#define XILINX_DMA_MM2S_CTRL_OFFSET		0x0000
#define XILINX_DMA_S2MM_CTRL_OFFSET		0x0030
#define XILINX_VDMA_MM2S_DESC_OFFSET		0x0050
#define XILINX_VDMA_S2MM_DESC_OFFSET		0x00a0

/* Control Registers */
#define XILINX_DMA_REG_DMACR			0x0000
#define XILINX_DMA_DMACR_DELAY_MAX		0xff
#define XILINX_DMA_DMACR_DELAY_SHIFT		24
#define XILINX_DMA_DMACR_FRAME_COUNT_MAX	0xff
#define XILINX_DMA_DMACR_FRAME_COUNT_SHIFT	16
#define XILINX_DMA_DMACR_ERR_IRQ		BIT(14)
#define XILINX_DMA_DMACR_DLY_CNT_IRQ		BIT(13)
#define XILINX_DMA_DMACR_FRM_CNT_IRQ		BIT(12)
#define XILINX_DMA_DMACR_MASTER_SHIFT		8
#define XILINX_DMA_DMACR_FSYNCSRC_SHIFT	5
#define XILINX_DMA_DMACR_FRAMECNT_EN		BIT(4)
#define XILINX_DMA_DMACR_GENLOCK_EN		BIT(3)
#define XILINX_DMA_DMACR_RESET			BIT(2)
#define XILINX_DMA_DMACR_CIRC_EN		BIT(1)
#define XILINX_DMA_DMACR_RUNSTOP		BIT(0)
#define XILINX_DMA_DMACR_FSYNCSRC_MASK		GENMASK(6, 5)
#define XILINX_DMA_DMACR_DELAY_MASK		GENMASK(31, 24)
#define XILINX_DMA_DMACR_FRAME_COUNT_MASK	GENMASK(23, 16)
#define XILINX_DMA_DMACR_MASTER_MASK		GENMASK(11, 8)

#define XILINX_DMA_REG_DMASR			0x0004
#define XILINX_DMA_DMASR_EOL_LATE_ERR		BIT(15)
#define XILINX_DMA_DMASR_ERR_IRQ		BIT(14)
#define XILINX_DMA_DMASR_DLY_CNT_IRQ		BIT(13)
#define XILINX_DMA_DMASR_FRM_CNT_IRQ		BIT(12)
#define XILINX_DMA_DMASR_SOF_LATE_ERR		BIT(11)
#define XILINX_DMA_DMASR_SG_DEC_ERR		BIT(10)
#define XILINX_DMA_DMASR_SG_SLV_ERR		BIT(9)
#define XILINX_DMA_DMASR_EOF_EARLY_ERR		BIT(8)
#define XILINX_DMA_DMASR_SOF_EARLY_ERR		BIT(7)
#define XILINX_DMA_DMASR_DMA_DEC_ERR		BIT(6)
#define XILINX_DMA_DMASR_DMA_SLAVE_ERR		BIT(5)
#define XILINX_DMA_DMASR_DMA_INT_ERR		BIT(4)
#define XILINX_DMA_DMASR_SG_MASK		BIT(3)
#define XILINX_DMA_DMASR_IDLE			BIT(1)
#define XILINX_DMA_DMASR_HALTED		BIT(0)
#define XILINX_DMA_DMASR_DELAY_MASK		GENMASK(31, 24)
#define XILINX_DMA_DMASR_FRAME_COUNT_MASK	GENMASK(23, 16)

#define XILINX_DMA_REG_CURDESC			0x0008
#define XILINX_DMA_REG_TAILDESC		0x0010
#define XILINX_DMA_REG_REG_INDEX		0x0014
#define XILINX_DMA_REG_FRMSTORE		0x0018
#define XILINX_DMA_REG_THRESHOLD		0x001c
#define XILINX_DMA_REG_FRMPTR_STS		0x0024
#define XILINX_DMA_REG_PARK_PTR		0x0028
#define XILINX_DMA_PARK_PTR_WR_REF_SHIFT	8
#define XILINX_DMA_PARK_PTR_WR_REF_MASK		GENMASK(12, 8)
#define XILINX_DMA_PARK_PTR_RD_REF_SHIFT	0
#define XILINX_DMA_PARK_PTR_RD_REF_MASK		GENMASK(4, 0)
#define XILINX_DMA_REG_VDMA_VERSION		0x002c

/* Register Direct Mode Registers */
#define XILINX_DMA_REG_VSIZE			0x0000
#define XILINX_DMA_REG_HSIZE			0x0004

#define XILINX_DMA_REG_FRMDLY_STRIDE		0x0008
#define XILINX_DMA_FRMDLY_STRIDE_FRMDLY_SHIFT	24
#define XILINX_DMA_FRMDLY_STRIDE_STRIDE_SHIFT	0

#define XILINX_VDMA_REG_START_ADDRESS(n)	(0x000c + 4 * (n))
#define XILINX_VDMA_REG_START_ADDRESS_64(n)	(0x000c + 8 * (n))

#define XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP	0x00ec
#define XILINX_VDMA_ENABLE_VERTICAL_FLIP	BIT(0)

/* HW specific definitions */
#define XILINX_MCDMA_MAX_CHANS_PER_DEVICE	0x20
#define XILINX_DMA_MAX_CHANS_PER_DEVICE		0x2
#define XILINX_CDMA_MAX_CHANS_PER_DEVICE	0x1

#define XILINX_DMA_DMAXR_ALL_IRQ_MASK	\
		(XILINX_DMA_DMASR_FRM_CNT_IRQ | \
		 XILINX_DMA_DMASR_DLY_CNT_IRQ | \
		 XILINX_DMA_DMASR_ERR_IRQ)

#define XILINX_DMA_DMASR_ALL_ERR_MASK	\
		(XILINX_DMA_DMASR_EOL_LATE_ERR | \
		 XILINX_DMA_DMASR_SOF_LATE_ERR | \
		 XILINX_DMA_DMASR_SG_DEC_ERR | \
		 XILINX_DMA_DMASR_SG_SLV_ERR | \
		 XILINX_DMA_DMASR_EOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_SOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_DMA_DEC_ERR | \
		 XILINX_DMA_DMASR_DMA_SLAVE_ERR | \
		 XILINX_DMA_DMASR_DMA_INT_ERR)

/*
 * Recoverable errors are DMA Internal error, SOF Early, EOF Early
 * and SOF Late. They are only recoverable when C_FLUSH_ON_FSYNC
 * is enabled in the h/w system.
 */
#define XILINX_DMA_DMASR_ERR_RECOVER_MASK	\
		(XILINX_DMA_DMASR_SOF_LATE_ERR | \
		 XILINX_DMA_DMASR_EOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_SOF_EARLY_ERR | \
		 XILINX_DMA_DMASR_DMA_INT_ERR)

/* Axi VDMA Flush on Fsync bits */
#define XILINX_DMA_FLUSH_S2MM		3
#define XILINX_DMA_FLUSH_MM2S		2
#define XILINX_DMA_FLUSH_BOTH		1

/* Delay loop counter to prevent hardware failure */
#define XILINX_DMA_LOOP_COUNT		1000000

/* AXI DMA Specific Registers/Offsets */
#define XILINX_DMA_REG_SRCDSTADDR	0x18
#define XILINX_DMA_REG_BTT		0x28

/* AXI DMA Specific Masks/Bit fields */
#define XILINX_DMA_MAX_TRANS_LEN_MIN	8
#define XILINX_DMA_MAX_TRANS_LEN_MAX	23
#define XILINX_DMA_V2_MAX_TRANS_LEN_MAX	26
#define XILINX_DMA_CR_COALESCE_MAX	GENMASK(23, 16)
#define XILINX_DMA_CR_CYCLIC_BD_EN_MASK	BIT(4)
#define XILINX_DMA_CR_COALESCE_SHIFT	16
#define XILINX_DMA_BD_SOP		BIT(27)
#define XILINX_DMA_BD_EOP		BIT(26)
#define XILINX_DMA_COALESCE_MAX		255
#define XILINX_DMA_NUM_DESCS		255
#define XILINX_DMA_NUM_APP_WORDS	5

/* AXI CDMA Specific Registers/Offsets */
#define XILINX_CDMA_REG_SRCADDR		0x18
#define XILINX_CDMA_REG_DSTADDR		0x20

/* AXI CDMA Specific Masks */
#define XILINX_CDMA_CR_SGMODE          BIT(3)

#define xilinx_prep_dma_addr_t(addr)	\
	((dma_addr_t)((u64)addr##_##msb << 32 | (addr)))

/* AXI MCDMA Specific Registers/Offsets */
#define XILINX_MCDMA_MM2S_CTRL_OFFSET		0x0000
#define XILINX_MCDMA_S2MM_CTRL_OFFSET		0x0500
#define XILINX_MCDMA_CHEN_OFFSET		0x0008
#define XILINX_MCDMA_CH_ERR_OFFSET		0x0010
#define XILINX_MCDMA_RXINT_SER_OFFSET		0x0020
#define XILINX_MCDMA_TXINT_SER_OFFSET		0x0028
#define XILINX_MCDMA_CHAN_CR_OFFSET(x)		(0x40 + (x) * 0x40)
#define XILINX_MCDMA_CHAN_SR_OFFSET(x)		(0x44 + (x) * 0x40)
#define XILINX_MCDMA_CHAN_CDESC_OFFSET(x)	(0x48 + (x) * 0x40)
#define XILINX_MCDMA_CHAN_TDESC_OFFSET(x)	(0x50 + (x) * 0x40)

/* AXI MCDMA Specific Masks/Shifts */
#define XILINX_MCDMA_COALESCE_SHIFT		16
#define XILINX_MCDMA_COALESCE_MAX		24
#define XILINX_MCDMA_IRQ_ALL_MASK		GENMASK(7, 5)
#define XILINX_MCDMA_COALESCE_MASK		GENMASK(23, 16)
#define XILINX_MCDMA_CR_RUNSTOP_MASK		BIT(0)
#define XILINX_MCDMA_IRQ_IOC_MASK		BIT(5)
#define XILINX_MCDMA_IRQ_DELAY_MASK		BIT(6)
#define XILINX_MCDMA_IRQ_ERR_MASK		BIT(7)
#define XILINX_MCDMA_BD_EOP			BIT(30)
#define XILINX_MCDMA_BD_SOP			BIT(31)

/**
 * struct xilinx_vdma_desc_hw - Hardware Descriptor
 * @next_desc: Next Descriptor Pointer @0x00
 * @pad1: Reserved @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @vsize: Vertical Size @0x10
 * @hsize: Horizontal Size @0x14
 * @stride: Number of bytes between the first
 *	    pixels of each horizontal line @0x18
 */
struct xilinx_vdma_desc_hw {
	u32 next_desc;
	u32 pad1;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 vsize;
	u32 hsize;
	u32 stride;
} __aligned(64);

/**
 * struct xilinx_axidma_desc_hw - Hardware Descriptor for AXI DMA
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: MSB of Next Descriptor Pointer @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @reserved1: Reserved @0x10
 * @reserved2: Reserved @0x14
 * @control: Control field @0x18
 * @status: Status field @0x1C
 * @app: APP Fields @0x20 - 0x30
 */
struct xilinx_axidma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 reserved1;
	u32 reserved2;
	u32 control;
	u32 status;
	u32 app[XILINX_DMA_NUM_APP_WORDS];
} __aligned(64);

/**
 * struct xilinx_aximcdma_desc_hw - Hardware Descriptor for AXI MCDMA
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: MSB of Next Descriptor Pointer @0x04
 * @buf_addr: Buffer address @0x08
 * @buf_addr_msb: MSB of Buffer address @0x0C
 * @rsvd: Reserved field @0x10
 * @control: Control Information field @0x14
 * @status: Status field @0x18
 * @sideband_status: Status of sideband signals @0x1C
 * @app: APP Fields @0x20 - 0x30
 */
struct xilinx_aximcdma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 buf_addr;
	u32 buf_addr_msb;
	u32 rsvd;
	u32 control;
	u32 status;
	u32 sideband_status;
	u32 app[XILINX_DMA_NUM_APP_WORDS];
} __aligned(64);

/**
 * struct xilinx_cdma_desc_hw - Hardware Descriptor
 * @next_desc: Next Descriptor Pointer @0x00
 * @next_desc_msb: Next Descriptor Pointer MSB @0x04
 * @src_addr: Source address @0x08
 * @src_addr_msb: Source address MSB @0x0C
 * @dest_addr: Destination address @0x10
 * @dest_addr_msb: Destination address MSB @0x14
 * @control: Control field @0x18
 * @status: Status field @0x1C
 */
struct xilinx_cdma_desc_hw {
	u32 next_desc;
	u32 next_desc_msb;
	u32 src_addr;
	u32 src_addr_msb;
	u32 dest_addr;
	u32 dest_addr_msb;
	u32 control;
	u32 status;
} __aligned(64);

/**
 * struct xilinx_vdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_vdma_tx_segment {
	struct xilinx_vdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_axidma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_axidma_tx_segment {
	struct xilinx_axidma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_aximcdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_aximcdma_tx_segment {
	struct xilinx_aximcdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_cdma_tx_segment - Descriptor segment
 * @hw: Hardware descriptor
 * @node: Node in the descriptor segments list
 * @phys: Physical address of segment
 */
struct xilinx_cdma_tx_segment {
	struct xilinx_cdma_desc_hw hw;
	struct list_head node;
	dma_addr_t phys;
} __aligned(64);

/**
 * struct xilinx_dma_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @segments: TX segments list
 * @node: Node in the channel descriptors list
 * @cyclic: Check for cyclic transfers.
 * @err: Whether the descriptor has an error.
 * @residue: Residue of the completed descriptor
 */
struct xilinx_dma_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct list_head segments;
	struct list_head node;
	bool cyclic;
	bool err;
	u32 residue;
};

/**
 * struct xilinx_dma_chan - Driver specific DMA channel structure
 * @xdev: Driver specific device structure
 * @ctrl_offset: Control registers offset
 * @desc_offset: TX descriptor registers offset
 * @lock: Descriptor operation lock
 * @pending_list: Descriptors waiting
 * @active_list: Descriptors ready to submit
 * @done_list: Complete descriptors
 * @free_seg_list: Free descriptors
 * @common: DMA common channel
 * @desc_pool: Descriptors pool
 * @dev: The dma device
 * @irq: Channel IRQ
 * @id: Channel ID
 * @direction: Transfer direction
 * @num_frms: Number of frames
 * @has_sg: Support scatter transfers
 * @cyclic: Check for cyclic transfers.
 * @genlock: Support genlock mode
 * @err: Channel has errors
 * @idle: Check for channel idle
 * @terminating: Check for channel being synchronized by user
 * @tasklet: Cleanup work after irq
 * @config: Device configuration info
 * @flush_on_fsync: Flush on Frame sync
 * @desc_pendingcount: Descriptor pending count
 * @ext_addr: Indicates 64 bit addressing is supported by dma channel
 * @desc_submitcount: Descriptor h/w submitted count
 * @seg_v: Statically allocated segments base
 * @seg_mv: Statically allocated segments base for MCDMA
 * @seg_p: Physical allocated segments base
 * @cyclic_seg_v: Statically allocated segment base for cyclic transfers
 * @cyclic_seg_p: Physical allocated segments base for cyclic dma
 * @start_transfer: Differentiate b/w DMA IP's transfer
 * @stop_transfer: Differentiate b/w DMA IP's quiesce
 * @tdest: TDEST value for mcdma
 * @has_vflip: S2MM vertical flip
 */
struct xilinx_dma_chan {
	struct xilinx_dma_device *xdev;
	u32 ctrl_offset;
	u32 desc_offset;
	spinlock_t lock;
	struct list_head pending_list;
	struct list_head active_list;
	struct list_head done_list;
	struct list_head free_seg_list;
	struct dma_chan common;
	struct dma_pool *desc_pool;
	struct device *dev;
	int irq;
	int id;
	enum dma_transfer_direction direction;
	int num_frms;
	bool has_sg;
	bool cyclic;
	bool genlock;
	bool err;
	bool idle;
	bool terminating;
	struct tasklet_struct tasklet;
	struct xilinx_vdma_config config;
	bool flush_on_fsync;
	u32 desc_pendingcount;
	bool ext_addr;
	u32 desc_submitcount;
	struct xilinx_axidma_tx_segment *seg_v;
	struct xilinx_aximcdma_tx_segment *seg_mv;
	dma_addr_t seg_p;
	struct xilinx_axidma_tx_segment *cyclic_seg_v;
	dma_addr_t cyclic_seg_p;
	void (*start_transfer)(struct xilinx_dma_chan *chan);
	int (*stop_transfer)(struct xilinx_dma_chan *chan);
	u16 tdest;
	bool has_vflip;
};

/**
 * enum xdma_ip_type - DMA IP type.
 *
 * @XDMA_TYPE_AXIDMA: Axi dma ip.
 * @XDMA_TYPE_CDMA: Axi cdma ip.
 * @XDMA_TYPE_VDMA: Axi vdma ip.
 * @XDMA_TYPE_AXIMCDMA: Axi MCDMA ip.
 *
 */
enum xdma_ip_type {
	XDMA_TYPE_AXIDMA = 0,
	XDMA_TYPE_CDMA,
	XDMA_TYPE_VDMA,
	XDMA_TYPE_AXIMCDMA
};

struct xilinx_dma_config {
	enum xdma_ip_type dmatype;
	int (*clk_init)(struct platform_device *pdev, struct clk **axi_clk,
			struct clk **tx_clk, struct clk **txs_clk,
			struct clk **rx_clk, struct clk **rxs_clk);
	irqreturn_t (*irq_handler)(int irq, void *data);
	const int max_channels;
};

/**
 * struct xilinx_dma_device - DMA device structure
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 * @flush_on_fsync: Flush on frame sync
 * @ext_addr: Indicates 64 bit addressing is supported by dma device
 * @pdev: Platform device structure pointer
 * @dma_config: DMA config structure
 * @axi_clk: DMA Axi4-lite interace clock
 * @tx_clk: DMA mm2s clock
 * @txs_clk: DMA mm2s stream clock
 * @rx_clk: DMA s2mm clock
 * @rxs_clk: DMA s2mm stream clock
 * @s2mm_chan_id: DMA s2mm channel identifier
 * @mm2s_chan_id: DMA mm2s channel identifier
 * @max_buffer_len: Max buffer length
 */
struct xilinx_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_dma_chan *chan[XILINX_MCDMA_MAX_CHANS_PER_DEVICE];
	u32 flush_on_fsync;
	bool ext_addr;
	struct platform_device  *pdev;
	const struct xilinx_dma_config *dma_config;
	struct clk *axi_clk;
	struct clk *tx_clk;
	struct clk *txs_clk;
	struct clk *rx_clk;
	struct clk *rxs_clk;
	u32 s2mm_chan_id;
	u32 mm2s_chan_id;
	u32 max_buffer_len;
};

/* Macros */
#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_dma_chan, common)
#define to_dma_tx_descriptor(tx) \
	container_of(tx, struct xilinx_dma_tx_descriptor, async_tx)
#define xilinx_dma_poll_timeout(chan, reg, val, cond, delay_us, timeout_us) \
	readl_poll_timeout_atomic(chan->xdev->regs + chan->ctrl_offset + reg, \
				  val, cond, delay_us, timeout_us)

/* IO accessors */
static inline u32 dma_read(struct xilinx_dma_chan *chan, u32 reg)
{
	return ioread32(chan->xdev->regs + reg);
}

static inline void dma_write(struct xilinx_dma_chan *chan, u32 reg, u32 value)
{
	iowrite32(value, chan->xdev->regs + reg);
}

static inline void vdma_desc_write(struct xilinx_dma_chan *chan, u32 reg,
				   u32 value)
{
	dma_write(chan, chan->desc_offset + reg, value);
}

static inline u32 dma_ctrl_read(struct xilinx_dma_chan *chan, u32 reg)
{
	return dma_read(chan, chan->ctrl_offset + reg);
}

static inline void dma_ctrl_write(struct xilinx_dma_chan *chan, u32 reg,
				   u32 value)
{
	dma_write(chan, chan->ctrl_offset + reg, value);
}

static inline void dma_ctrl_clr(struct xilinx_dma_chan *chan, u32 reg,
				 u32 clr)
{
	dma_ctrl_write(chan, reg, dma_ctrl_read(chan, reg) & ~clr);
}

static inline void dma_ctrl_set(struct xilinx_dma_chan *chan, u32 reg,
				 u32 set)
{
	dma_ctrl_write(chan, reg, dma_ctrl_read(chan, reg) | set);
}

/**
 * vdma_desc_write_64 - 64-bit descriptor write
 * @chan: Driver specific VDMA channel
 * @reg: Register to write
 * @value_lsb: lower address of the descriptor.
 * @value_msb: upper address of the descriptor.
 *
 * Since vdma driver is trying to write to a register offset which is not a
 * multiple of 64 bits(ex : 0x5c), we are writing as two separate 32 bits
 * instead of a single 64 bit register write.
 */
static inline void vdma_desc_write_64(struct xilinx_dma_chan *chan, u32 reg,
				      u32 value_lsb, u32 value_msb)
{
	/* Write the lsb 32 bits*/
	writel(value_lsb, chan->xdev->regs + chan->desc_offset + reg);

	/* Write the msb 32 bits */
	writel(value_msb, chan->xdev->regs + chan->desc_offset + reg + 4);
}

static inline void dma_writeq(struct xilinx_dma_chan *chan, u32 reg, u64 value)
{
	lo_hi_writeq(value, chan->xdev->regs + chan->ctrl_offset + reg);
}

static inline void xilinx_write(struct xilinx_dma_chan *chan, u32 reg,
				dma_addr_t addr)
{
	if (chan->ext_addr)
		dma_writeq(chan, reg, addr);
	else
		dma_ctrl_write(chan, reg, addr);
}

static inline void xilinx_axidma_buf(struct xilinx_dma_chan *chan,
				     struct xilinx_axidma_desc_hw *hw,
				     dma_addr_t buf_addr, size_t sg_used,
				     size_t period_len)
{
	if (chan->ext_addr) {
		hw->buf_addr = lower_32_bits(buf_addr + sg_used + period_len);
		hw->buf_addr_msb = upper_32_bits(buf_addr + sg_used +
						 period_len);
	} else {
		hw->buf_addr = buf_addr + sg_used + period_len;
	}
}

static inline void xilinx_aximcdma_buf(struct xilinx_dma_chan *chan,
				       struct xilinx_aximcdma_desc_hw *hw,
				       dma_addr_t buf_addr, size_t sg_used)
{
	if (chan->ext_addr) {
		hw->buf_addr = lower_32_bits(buf_addr + sg_used);
		hw->buf_addr_msb = upper_32_bits(buf_addr + sg_used);
	} else {
		hw->buf_addr = buf_addr + sg_used;
	}
}

/* -----------------------------------------------------------------------------
 * Descriptors and segments alloc and free
 */

/**
 * xilinx_vdma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_vdma_tx_segment *
xilinx_vdma_alloc_tx_segment(struct xilinx_dma_chan *chan)
{
	struct xilinx_vdma_tx_segment *segment;
	dma_addr_t phys;

	segment = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &phys);
	if (!segment)
		return NULL;

	segment->phys = phys;

	return segment;
}

/**
 * xilinx_cdma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_cdma_tx_segment *
xilinx_cdma_alloc_tx_segment(struct xilinx_dma_chan *chan)
{
	struct xilinx_cdma_tx_segment *segment;
	dma_addr_t phys;

	segment = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &phys);
	if (!segment)
		return NULL;

	segment->phys = phys;

	return segment;
}

/**
 * xilinx_axidma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_axidma_tx_segment *
xilinx_axidma_alloc_tx_segment(struct xilinx_dma_chan *chan)
{
	struct xilinx_axidma_tx_segment *segment = NULL;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (!list_empty(&chan->free_seg_list)) {
		segment = list_first_entry(&chan->free_seg_list,
					   struct xilinx_axidma_tx_segment,
					   node);
		list_del(&segment->node);
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	if (!segment)
		dev_dbg(chan->dev, "Could not find free tx segment\n");

	return segment;
}

/**
 * xilinx_aximcdma_alloc_tx_segment - Allocate transaction segment
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated segment on success and NULL on failure.
 */
static struct xilinx_aximcdma_tx_segment *
xilinx_aximcdma_alloc_tx_segment(struct xilinx_dma_chan *chan)
{
	struct xilinx_aximcdma_tx_segment *segment = NULL;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (!list_empty(&chan->free_seg_list)) {
		segment = list_first_entry(&chan->free_seg_list,
					   struct xilinx_aximcdma_tx_segment,
					   node);
		list_del(&segment->node);
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	return segment;
}

static void xilinx_dma_clean_hw_desc(struct xilinx_axidma_desc_hw *hw)
{
	u32 next_desc = hw->next_desc;
	u32 next_desc_msb = hw->next_desc_msb;

	memset(hw, 0, sizeof(struct xilinx_axidma_desc_hw));

	hw->next_desc = next_desc;
	hw->next_desc_msb = next_desc_msb;
}

static void xilinx_mcdma_clean_hw_desc(struct xilinx_aximcdma_desc_hw *hw)
{
	u32 next_desc = hw->next_desc;
	u32 next_desc_msb = hw->next_desc_msb;

	memset(hw, 0, sizeof(struct xilinx_aximcdma_desc_hw));

	hw->next_desc = next_desc;
	hw->next_desc_msb = next_desc_msb;
}

/**
 * xilinx_dma_free_tx_segment - Free transaction segment
 * @chan: Driver specific DMA channel
 * @segment: DMA transaction segment
 */
static void xilinx_dma_free_tx_segment(struct xilinx_dma_chan *chan,
				struct xilinx_axidma_tx_segment *segment)
{
	xilinx_dma_clean_hw_desc(&segment->hw);

	list_add_tail(&segment->node, &chan->free_seg_list);
}

/**
 * xilinx_mcdma_free_tx_segment - Free transaction segment
 * @chan: Driver specific DMA channel
 * @segment: DMA transaction segment
 */
static void xilinx_mcdma_free_tx_segment(struct xilinx_dma_chan *chan,
					 struct xilinx_aximcdma_tx_segment *
					 segment)
{
	xilinx_mcdma_clean_hw_desc(&segment->hw);

	list_add_tail(&segment->node, &chan->free_seg_list);
}

/**
 * xilinx_cdma_free_tx_segment - Free transaction segment
 * @chan: Driver specific DMA channel
 * @segment: DMA transaction segment
 */
static void xilinx_cdma_free_tx_segment(struct xilinx_dma_chan *chan,
				struct xilinx_cdma_tx_segment *segment)
{
	dma_pool_free(chan->desc_pool, segment, segment->phys);
}

/**
 * xilinx_vdma_free_tx_segment - Free transaction segment
 * @chan: Driver specific DMA channel
 * @segment: DMA transaction segment
 */
static void xilinx_vdma_free_tx_segment(struct xilinx_dma_chan *chan,
					struct xilinx_vdma_tx_segment *segment)
{
	dma_pool_free(chan->desc_pool, segment, segment->phys);
}

/**
 * xilinx_dma_tx_descriptor - Allocate transaction descriptor
 * @chan: Driver specific DMA channel
 *
 * Return: The allocated descriptor on success and NULL on failure.
 */
static struct xilinx_dma_tx_descriptor *
xilinx_dma_alloc_tx_descriptor(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->segments);

	return desc;
}

/**
 * xilinx_dma_free_tx_descriptor - Free transaction descriptor
 * @chan: Driver specific DMA channel
 * @desc: DMA transaction descriptor
 */
static void
xilinx_dma_free_tx_descriptor(struct xilinx_dma_chan *chan,
			       struct xilinx_dma_tx_descriptor *desc)
{
	struct xilinx_vdma_tx_segment *segment, *next;
	struct xilinx_cdma_tx_segment *cdma_segment, *cdma_next;
	struct xilinx_axidma_tx_segment *axidma_segment, *axidma_next;
	struct xilinx_aximcdma_tx_segment *aximcdma_segment, *aximcdma_next;

	if (!desc)
		return;

	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
		list_for_each_entry_safe(segment, next, &desc->segments, node) {
			list_del(&segment->node);
			xilinx_vdma_free_tx_segment(chan, segment);
		}
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
		list_for_each_entry_safe(cdma_segment, cdma_next,
					 &desc->segments, node) {
			list_del(&cdma_segment->node);
			xilinx_cdma_free_tx_segment(chan, cdma_segment);
		}
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		list_for_each_entry_safe(axidma_segment, axidma_next,
					 &desc->segments, node) {
			list_del(&axidma_segment->node);
			xilinx_dma_free_tx_segment(chan, axidma_segment);
		}
	} else {
		list_for_each_entry_safe(aximcdma_segment, aximcdma_next,
					 &desc->segments, node) {
			list_del(&aximcdma_segment->node);
			xilinx_mcdma_free_tx_segment(chan, aximcdma_segment);
		}
	}

	kfree(desc);
}

/* Required functions */

/**
 * xilinx_dma_free_desc_list - Free descriptors list
 * @chan: Driver specific DMA channel
 * @list: List to parse and delete the descriptor
 */
static void xilinx_dma_free_desc_list(struct xilinx_dma_chan *chan,
					struct list_head *list)
{
	struct xilinx_dma_tx_descriptor *desc, *next;

	list_for_each_entry_safe(desc, next, list, node) {
		list_del(&desc->node);
		xilinx_dma_free_tx_descriptor(chan, desc);
	}
}

/**
 * xilinx_dma_free_descriptors - Free channel descriptors
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_free_descriptors(struct xilinx_dma_chan *chan)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	xilinx_dma_free_desc_list(chan, &chan->pending_list);
	xilinx_dma_free_desc_list(chan, &chan->done_list);
	xilinx_dma_free_desc_list(chan, &chan->active_list);

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_free_chan_resources - Free channel resources
 * @dchan: DMA channel
 */
static void xilinx_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	dev_dbg(chan->dev, "Free all channel resources.\n");

	xilinx_dma_free_descriptors(chan);

	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		spin_lock_irqsave(&chan->lock, flags);
		INIT_LIST_HEAD(&chan->free_seg_list);
		spin_unlock_irqrestore(&chan->lock, flags);

		/* Free memory that is allocated for BD */
		dma_free_coherent(chan->dev, sizeof(*chan->seg_v) *
				  XILINX_DMA_NUM_DESCS, chan->seg_v,
				  chan->seg_p);

		/* Free Memory that is allocated for cyclic DMA Mode */
		dma_free_coherent(chan->dev, sizeof(*chan->cyclic_seg_v),
				  chan->cyclic_seg_v, chan->cyclic_seg_p);
	}

	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA) {
		spin_lock_irqsave(&chan->lock, flags);
		INIT_LIST_HEAD(&chan->free_seg_list);
		spin_unlock_irqrestore(&chan->lock, flags);

		/* Free memory that is allocated for BD */
		dma_free_coherent(chan->dev, sizeof(*chan->seg_mv) *
				  XILINX_DMA_NUM_DESCS, chan->seg_mv,
				  chan->seg_p);
	}

	if (chan->xdev->dma_config->dmatype != XDMA_TYPE_AXIDMA &&
	    chan->xdev->dma_config->dmatype != XDMA_TYPE_AXIMCDMA) {
		dma_pool_destroy(chan->desc_pool);
		chan->desc_pool = NULL;
	}

}

/**
 * xilinx_dma_get_residue - Compute residue for a given descriptor
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 *
 * Return: The number of residue bytes for the descriptor.
 */
static u32 xilinx_dma_get_residue(struct xilinx_dma_chan *chan,
				  struct xilinx_dma_tx_descriptor *desc)
{
	struct xilinx_cdma_tx_segment *cdma_seg;
	struct xilinx_axidma_tx_segment *axidma_seg;
	struct xilinx_aximcdma_tx_segment *aximcdma_seg;
	struct xilinx_cdma_desc_hw *cdma_hw;
	struct xilinx_axidma_desc_hw *axidma_hw;
	struct xilinx_aximcdma_desc_hw *aximcdma_hw;
	struct list_head *entry;
	u32 residue = 0;

	list_for_each(entry, &desc->segments) {
		if (chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
			cdma_seg = list_entry(entry,
					      struct xilinx_cdma_tx_segment,
					      node);
			cdma_hw = &cdma_seg->hw;
			residue += (cdma_hw->control - cdma_hw->status) &
				   chan->xdev->max_buffer_len;
		} else if (chan->xdev->dma_config->dmatype ==
			   XDMA_TYPE_AXIDMA) {
			axidma_seg = list_entry(entry,
						struct xilinx_axidma_tx_segment,
						node);
			axidma_hw = &axidma_seg->hw;
			residue += (axidma_hw->control - axidma_hw->status) &
				   chan->xdev->max_buffer_len;
		} else {
			aximcdma_seg =
				list_entry(entry,
					   struct xilinx_aximcdma_tx_segment,
					   node);
			aximcdma_hw = &aximcdma_seg->hw;
			residue +=
				(aximcdma_hw->control - aximcdma_hw->status) &
				chan->xdev->max_buffer_len;
		}
	}

	return residue;
}

/**
 * xilinx_dma_chan_handle_cyclic - Cyclic dma callback
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 * @flags: flags for spin lock
 */
static void xilinx_dma_chan_handle_cyclic(struct xilinx_dma_chan *chan,
					  struct xilinx_dma_tx_descriptor *desc,
					  unsigned long *flags)
{
	dma_async_tx_callback callback;
	void *callback_param;

	callback = desc->async_tx.callback;
	callback_param = desc->async_tx.callback_param;
	if (callback) {
		spin_unlock_irqrestore(&chan->lock, *flags);
		callback(callback_param);
		spin_lock_irqsave(&chan->lock, *flags);
	}
}

/**
 * xilinx_dma_chan_desc_cleanup - Clean channel descriptors
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_chan_desc_cleanup(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc, *next;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, next, &chan->done_list, node) {
		struct dmaengine_result result;

		if (desc->cyclic) {
			xilinx_dma_chan_handle_cyclic(chan, desc, &flags);
			break;
		}

		/* Remove from the list of running transactions */
		list_del(&desc->node);

		if (unlikely(desc->err)) {
			if (chan->direction == DMA_DEV_TO_MEM)
				result.result = DMA_TRANS_READ_FAILED;
			else
				result.result = DMA_TRANS_WRITE_FAILED;
		} else {
			result.result = DMA_TRANS_NOERROR;
		}

		result.residue = desc->residue;

		/* Run the link descriptor callback function */
		spin_unlock_irqrestore(&chan->lock, flags);
		dmaengine_desc_get_callback_invoke(&desc->async_tx, &result);
		spin_lock_irqsave(&chan->lock, flags);

		/* Run any dependencies, then free the descriptor */
		dma_run_dependencies(&desc->async_tx);
		xilinx_dma_free_tx_descriptor(chan, desc);

		/*
		 * While we ran a callback the user called a terminate function,
		 * which takes care of cleaning up any remaining descriptors
		 */
		if (chan->terminating)
			break;
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_do_tasklet - Schedule completion tasklet
 * @t: Pointer to the Xilinx DMA channel structure
 */
static void xilinx_dma_do_tasklet(struct tasklet_struct *t)
{
	struct xilinx_dma_chan *chan = from_tasklet(chan, t, tasklet);

	xilinx_dma_chan_desc_cleanup(chan);
}

/**
 * xilinx_dma_alloc_chan_resources - Allocate channel resources
 * @dchan: DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	int i;

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 0;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx VDMA specification requirement.
	 */
	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		/* Allocate the buffer descriptors. */
		chan->seg_v = dma_alloc_coherent(chan->dev,
						 sizeof(*chan->seg_v) * XILINX_DMA_NUM_DESCS,
						 &chan->seg_p, GFP_KERNEL);
		if (!chan->seg_v) {
			dev_err(chan->dev,
				"unable to allocate channel %d descriptors\n",
				chan->id);
			return -ENOMEM;
		}
		/*
		 * For cyclic DMA mode we need to program the tail Descriptor
		 * register with a value which is not a part of the BD chain
		 * so allocating a desc segment during channel allocation for
		 * programming tail descriptor.
		 */
		chan->cyclic_seg_v = dma_alloc_coherent(chan->dev,
							sizeof(*chan->cyclic_seg_v),
							&chan->cyclic_seg_p,
							GFP_KERNEL);
		if (!chan->cyclic_seg_v) {
			dev_err(chan->dev,
				"unable to allocate desc segment for cyclic DMA\n");
			dma_free_coherent(chan->dev, sizeof(*chan->seg_v) *
				XILINX_DMA_NUM_DESCS, chan->seg_v,
				chan->seg_p);
			return -ENOMEM;
		}
		chan->cyclic_seg_v->phys = chan->cyclic_seg_p;

		for (i = 0; i < XILINX_DMA_NUM_DESCS; i++) {
			chan->seg_v[i].hw.next_desc =
			lower_32_bits(chan->seg_p + sizeof(*chan->seg_v) *
				((i + 1) % XILINX_DMA_NUM_DESCS));
			chan->seg_v[i].hw.next_desc_msb =
			upper_32_bits(chan->seg_p + sizeof(*chan->seg_v) *
				((i + 1) % XILINX_DMA_NUM_DESCS));
			chan->seg_v[i].phys = chan->seg_p +
				sizeof(*chan->seg_v) * i;
			list_add_tail(&chan->seg_v[i].node,
				      &chan->free_seg_list);
		}
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA) {
		/* Allocate the buffer descriptors. */
		chan->seg_mv = dma_alloc_coherent(chan->dev,
						  sizeof(*chan->seg_mv) *
						  XILINX_DMA_NUM_DESCS,
						  &chan->seg_p, GFP_KERNEL);
		if (!chan->seg_mv) {
			dev_err(chan->dev,
				"unable to allocate channel %d descriptors\n",
				chan->id);
			return -ENOMEM;
		}
		for (i = 0; i < XILINX_DMA_NUM_DESCS; i++) {
			chan->seg_mv[i].hw.next_desc =
			lower_32_bits(chan->seg_p + sizeof(*chan->seg_mv) *
				((i + 1) % XILINX_DMA_NUM_DESCS));
			chan->seg_mv[i].hw.next_desc_msb =
			upper_32_bits(chan->seg_p + sizeof(*chan->seg_mv) *
				((i + 1) % XILINX_DMA_NUM_DESCS));
			chan->seg_mv[i].phys = chan->seg_p +
				sizeof(*chan->seg_mv) * i;
			list_add_tail(&chan->seg_mv[i].node,
				      &chan->free_seg_list);
		}
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
		chan->desc_pool = dma_pool_create("xilinx_cdma_desc_pool",
				   chan->dev,
				   sizeof(struct xilinx_cdma_tx_segment),
				   __alignof__(struct xilinx_cdma_tx_segment),
				   0);
	} else {
		chan->desc_pool = dma_pool_create("xilinx_vdma_desc_pool",
				     chan->dev,
				     sizeof(struct xilinx_vdma_tx_segment),
				     __alignof__(struct xilinx_vdma_tx_segment),
				     0);
	}

	if (!chan->desc_pool &&
	    ((chan->xdev->dma_config->dmatype != XDMA_TYPE_AXIDMA) &&
		chan->xdev->dma_config->dmatype != XDMA_TYPE_AXIMCDMA)) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	dma_cookie_init(dchan);

	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		/* For AXI DMA resetting once channel will reset the
		 * other channel as well so enable the interrupts here.
		 */
		dma_ctrl_set(chan, XILINX_DMA_REG_DMACR,
			      XILINX_DMA_DMAXR_ALL_IRQ_MASK);
	}

	if ((chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) && chan->has_sg)
		dma_ctrl_set(chan, XILINX_DMA_REG_DMACR,
			     XILINX_CDMA_CR_SGMODE);

	return 0;
}

/**
 * xilinx_dma_calc_copysize - Calculate the amount of data to copy
 * @chan: Driver specific DMA channel
 * @size: Total data that needs to be copied
 * @done: Amount of data that has been already copied
 *
 * Return: Amount of data that has to be copied
 */
static int xilinx_dma_calc_copysize(struct xilinx_dma_chan *chan,
				    int size, int done)
{
	size_t copy;

	copy = min_t(size_t, size - done,
		     chan->xdev->max_buffer_len);

	if ((copy + done < size) &&
	    chan->xdev->common.copy_align) {
		/*
		 * If this is not the last descriptor, make sure
		 * the next one will be properly aligned
		 */
		copy = rounddown(copy,
				 (1 << chan->xdev->common.copy_align));
	}
	return copy;
}

/**
 * xilinx_dma_tx_status - Get DMA transaction status
 * @dchan: DMA channel
 * @cookie: Transaction identifier
 * @txstate: Transaction state
 *
 * Return: DMA transaction status
 */
static enum dma_status xilinx_dma_tx_status(struct dma_chan *dchan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	enum dma_status ret;
	unsigned long flags;
	u32 residue = 0;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&chan->lock, flags);
	if (!list_empty(&chan->active_list)) {
		desc = list_last_entry(&chan->active_list,
				       struct xilinx_dma_tx_descriptor, node);
		/*
		 * VDMA and simple mode do not support residue reporting, so the
		 * residue field will always be 0.
		 */
		if (chan->has_sg && chan->xdev->dma_config->dmatype != XDMA_TYPE_VDMA)
			residue = xilinx_dma_get_residue(chan, desc);
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	dma_set_residue(txstate, residue);

	return ret;
}

/**
 * xilinx_dma_stop_transfer - Halt DMA channel
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_stop_transfer(struct xilinx_dma_chan *chan)
{
	u32 val;

	dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RUNSTOP);

	/* Wait for the hardware to halt */
	return xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMASR, val,
				       val & XILINX_DMA_DMASR_HALTED, 0,
				       XILINX_DMA_LOOP_COUNT);
}

/**
 * xilinx_cdma_stop_transfer - Wait for the current transfer to complete
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_cdma_stop_transfer(struct xilinx_dma_chan *chan)
{
	u32 val;

	return xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMASR, val,
				       val & XILINX_DMA_DMASR_IDLE, 0,
				       XILINX_DMA_LOOP_COUNT);
}

/**
 * xilinx_dma_start - Start DMA channel
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_start(struct xilinx_dma_chan *chan)
{
	int err;
	u32 val;

	dma_ctrl_set(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RUNSTOP);

	/* Wait for the hardware to start */
	err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMASR, val,
				      !(val & XILINX_DMA_DMASR_HALTED), 0,
				      XILINX_DMA_LOOP_COUNT);

	if (err) {
		dev_err(chan->dev, "Cannot start channel %p: %x\n",
			chan, dma_ctrl_read(chan, XILINX_DMA_REG_DMASR));

		chan->err = true;
	}
}

/**
 * xilinx_vdma_start_transfer - Starts VDMA transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_vdma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_vdma_config *config = &chan->config;
	struct xilinx_dma_tx_descriptor *desc;
	u32 reg, j;
	struct xilinx_vdma_tx_segment *segment, *last = NULL;
	int i = 0;

	/* This function was invoked with lock held */
	if (chan->err)
		return;

	if (!chan->idle)
		return;

	if (list_empty(&chan->pending_list))
		return;

	desc = list_first_entry(&chan->pending_list,
				struct xilinx_dma_tx_descriptor, node);

	/* Configure the hardware using info in the config structure */
	if (chan->has_vflip) {
		reg = dma_read(chan, XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP);
		reg &= ~XILINX_VDMA_ENABLE_VERTICAL_FLIP;
		reg |= config->vflip_en;
		dma_write(chan, XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP,
			  reg);
	}

	reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);

	if (config->frm_cnt_en)
		reg |= XILINX_DMA_DMACR_FRAMECNT_EN;
	else
		reg &= ~XILINX_DMA_DMACR_FRAMECNT_EN;

	/* If not parking, enable circular mode */
	if (config->park)
		reg &= ~XILINX_DMA_DMACR_CIRC_EN;
	else
		reg |= XILINX_DMA_DMACR_CIRC_EN;

	dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);

	j = chan->desc_submitcount;
	reg = dma_read(chan, XILINX_DMA_REG_PARK_PTR);
	if (chan->direction == DMA_MEM_TO_DEV) {
		reg &= ~XILINX_DMA_PARK_PTR_RD_REF_MASK;
		reg |= j << XILINX_DMA_PARK_PTR_RD_REF_SHIFT;
	} else {
		reg &= ~XILINX_DMA_PARK_PTR_WR_REF_MASK;
		reg |= j << XILINX_DMA_PARK_PTR_WR_REF_SHIFT;
	}
	dma_write(chan, XILINX_DMA_REG_PARK_PTR, reg);

	/* Start the hardware */
	xilinx_dma_start(chan);

	if (chan->err)
		return;

	/* Start the transfer */
	if (chan->desc_submitcount < chan->num_frms)
		i = chan->desc_submitcount;

	list_for_each_entry(segment, &desc->segments, node) {
		if (chan->ext_addr)
			vdma_desc_write_64(chan,
				   XILINX_VDMA_REG_START_ADDRESS_64(i++),
				   segment->hw.buf_addr,
				   segment->hw.buf_addr_msb);
		else
			vdma_desc_write(chan,
					XILINX_VDMA_REG_START_ADDRESS(i++),
					segment->hw.buf_addr);

		last = segment;
	}

	if (!last)
		return;

	/* HW expects these parameters to be same for one transaction */
	vdma_desc_write(chan, XILINX_DMA_REG_HSIZE, last->hw.hsize);
	vdma_desc_write(chan, XILINX_DMA_REG_FRMDLY_STRIDE,
			last->hw.stride);
	vdma_desc_write(chan, XILINX_DMA_REG_VSIZE, last->hw.vsize);

	chan->desc_submitcount++;
	chan->desc_pendingcount--;
	list_del(&desc->node);
	list_add_tail(&desc->node, &chan->active_list);
	if (chan->desc_submitcount == chan->num_frms)
		chan->desc_submitcount = 0;

	chan->idle = false;
}

/**
 * xilinx_cdma_start_transfer - Starts cdma transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_cdma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *head_desc, *tail_desc;
	struct xilinx_cdma_tx_segment *tail_segment;
	u32 ctrl_reg = dma_read(chan, XILINX_DMA_REG_DMACR);

	if (chan->err)
		return;

	if (!chan->idle)
		return;

	if (list_empty(&chan->pending_list))
		return;

	head_desc = list_first_entry(&chan->pending_list,
				     struct xilinx_dma_tx_descriptor, node);
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	tail_segment = list_last_entry(&tail_desc->segments,
				       struct xilinx_cdma_tx_segment, node);

	if (chan->desc_pendingcount <= XILINX_DMA_COALESCE_MAX) {
		ctrl_reg &= ~XILINX_DMA_CR_COALESCE_MAX;
		ctrl_reg |= chan->desc_pendingcount <<
				XILINX_DMA_CR_COALESCE_SHIFT;
		dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, ctrl_reg);
	}

	if (chan->has_sg) {
		dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR,
			     XILINX_CDMA_CR_SGMODE);

		dma_ctrl_set(chan, XILINX_DMA_REG_DMACR,
			     XILINX_CDMA_CR_SGMODE);

		xilinx_write(chan, XILINX_DMA_REG_CURDESC,
			     head_desc->async_tx.phys);

		/* Update tail ptr register which will start the transfer */
		xilinx_write(chan, XILINX_DMA_REG_TAILDESC,
			     tail_segment->phys);
	} else {
		/* In simple mode */
		struct xilinx_cdma_tx_segment *segment;
		struct xilinx_cdma_desc_hw *hw;

		segment = list_first_entry(&head_desc->segments,
					   struct xilinx_cdma_tx_segment,
					   node);

		hw = &segment->hw;

		xilinx_write(chan, XILINX_CDMA_REG_SRCADDR,
			     xilinx_prep_dma_addr_t(hw->src_addr));
		xilinx_write(chan, XILINX_CDMA_REG_DSTADDR,
			     xilinx_prep_dma_addr_t(hw->dest_addr));

		/* Start the transfer */
		dma_ctrl_write(chan, XILINX_DMA_REG_BTT,
				hw->control & chan->xdev->max_buffer_len);
	}

	list_splice_tail_init(&chan->pending_list, &chan->active_list);
	chan->desc_pendingcount = 0;
	chan->idle = false;
}

/**
 * xilinx_dma_start_transfer - Starts DMA transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_dma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *head_desc, *tail_desc;
	struct xilinx_axidma_tx_segment *tail_segment;
	u32 reg;

	if (chan->err)
		return;

	if (list_empty(&chan->pending_list))
		return;

	if (!chan->idle)
		return;

	head_desc = list_first_entry(&chan->pending_list,
				     struct xilinx_dma_tx_descriptor, node);
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	tail_segment = list_last_entry(&tail_desc->segments,
				       struct xilinx_axidma_tx_segment, node);

	reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);

	if (chan->desc_pendingcount <= XILINX_DMA_COALESCE_MAX) {
		reg &= ~XILINX_DMA_CR_COALESCE_MAX;
		reg |= chan->desc_pendingcount <<
				  XILINX_DMA_CR_COALESCE_SHIFT;
		dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);
	}

	if (chan->has_sg)
		xilinx_write(chan, XILINX_DMA_REG_CURDESC,
			     head_desc->async_tx.phys);

	xilinx_dma_start(chan);

	if (chan->err)
		return;

	/* Start the transfer */
	if (chan->has_sg) {
		if (chan->cyclic)
			xilinx_write(chan, XILINX_DMA_REG_TAILDESC,
				     chan->cyclic_seg_v->phys);
		else
			xilinx_write(chan, XILINX_DMA_REG_TAILDESC,
				     tail_segment->phys);
	} else {
		struct xilinx_axidma_tx_segment *segment;
		struct xilinx_axidma_desc_hw *hw;

		segment = list_first_entry(&head_desc->segments,
					   struct xilinx_axidma_tx_segment,
					   node);
		hw = &segment->hw;

		xilinx_write(chan, XILINX_DMA_REG_SRCDSTADDR,
			     xilinx_prep_dma_addr_t(hw->buf_addr));

		/* Start the transfer */
		dma_ctrl_write(chan, XILINX_DMA_REG_BTT,
			       hw->control & chan->xdev->max_buffer_len);
	}

	list_splice_tail_init(&chan->pending_list, &chan->active_list);
	chan->desc_pendingcount = 0;
	chan->idle = false;
}

/**
 * xilinx_mcdma_start_transfer - Starts MCDMA transfer
 * @chan: Driver specific channel struct pointer
 */
static void xilinx_mcdma_start_transfer(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *head_desc, *tail_desc;
	struct xilinx_aximcdma_tx_segment *tail_segment;
	u32 reg;

	/*
	 * lock has been held by calling functions, so we don't need it
	 * to take it here again.
	 */

	if (chan->err)
		return;

	if (!chan->idle)
		return;

	if (list_empty(&chan->pending_list))
		return;

	head_desc = list_first_entry(&chan->pending_list,
				     struct xilinx_dma_tx_descriptor, node);
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	tail_segment = list_last_entry(&tail_desc->segments,
				       struct xilinx_aximcdma_tx_segment, node);

	reg = dma_ctrl_read(chan, XILINX_MCDMA_CHAN_CR_OFFSET(chan->tdest));

	if (chan->desc_pendingcount <= XILINX_MCDMA_COALESCE_MAX) {
		reg &= ~XILINX_MCDMA_COALESCE_MASK;
		reg |= chan->desc_pendingcount <<
			XILINX_MCDMA_COALESCE_SHIFT;
	}

	reg |= XILINX_MCDMA_IRQ_ALL_MASK;
	dma_ctrl_write(chan, XILINX_MCDMA_CHAN_CR_OFFSET(chan->tdest), reg);

	/* Program current descriptor */
	xilinx_write(chan, XILINX_MCDMA_CHAN_CDESC_OFFSET(chan->tdest),
		     head_desc->async_tx.phys);

	/* Program channel enable register */
	reg = dma_ctrl_read(chan, XILINX_MCDMA_CHEN_OFFSET);
	reg |= BIT(chan->tdest);
	dma_ctrl_write(chan, XILINX_MCDMA_CHEN_OFFSET, reg);

	/* Start the fetch of BDs for the channel */
	reg = dma_ctrl_read(chan, XILINX_MCDMA_CHAN_CR_OFFSET(chan->tdest));
	reg |= XILINX_MCDMA_CR_RUNSTOP_MASK;
	dma_ctrl_write(chan, XILINX_MCDMA_CHAN_CR_OFFSET(chan->tdest), reg);

	xilinx_dma_start(chan);

	if (chan->err)
		return;

	/* Start the transfer */
	xilinx_write(chan, XILINX_MCDMA_CHAN_TDESC_OFFSET(chan->tdest),
		     tail_segment->phys);

	list_splice_tail_init(&chan->pending_list, &chan->active_list);
	chan->desc_pendingcount = 0;
	chan->idle = false;
}

/**
 * xilinx_dma_issue_pending - Issue pending transactions
 * @dchan: DMA channel
 */
static void xilinx_dma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	chan->start_transfer(chan);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/**
 * xilinx_dma_complete_descriptor - Mark the active descriptor as complete
 * @chan : xilinx DMA channel
 *
 * CONTEXT: hardirq
 */
static void xilinx_dma_complete_descriptor(struct xilinx_dma_chan *chan)
{
	struct xilinx_dma_tx_descriptor *desc, *next;

	/* This function was invoked with lock held */
	if (list_empty(&chan->active_list))
		return;

	list_for_each_entry_safe(desc, next, &chan->active_list, node) {
		if (chan->has_sg && chan->xdev->dma_config->dmatype !=
		    XDMA_TYPE_VDMA)
			desc->residue = xilinx_dma_get_residue(chan, desc);
		else
			desc->residue = 0;
		desc->err = chan->err;

		list_del(&desc->node);
		if (!desc->cyclic)
			dma_cookie_complete(&desc->async_tx);
		list_add_tail(&desc->node, &chan->done_list);
	}
}

/**
 * xilinx_dma_reset - Reset DMA channel
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_reset(struct xilinx_dma_chan *chan)
{
	int err;
	u32 tmp;

	dma_ctrl_set(chan, XILINX_DMA_REG_DMACR, XILINX_DMA_DMACR_RESET);

	/* Wait for the hardware to finish reset */
	err = xilinx_dma_poll_timeout(chan, XILINX_DMA_REG_DMACR, tmp,
				      !(tmp & XILINX_DMA_DMACR_RESET), 0,
				      XILINX_DMA_LOOP_COUNT);

	if (err) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			dma_ctrl_read(chan, XILINX_DMA_REG_DMACR),
			dma_ctrl_read(chan, XILINX_DMA_REG_DMASR));
		return -ETIMEDOUT;
	}

	chan->err = false;
	chan->idle = true;
	chan->desc_pendingcount = 0;
	chan->desc_submitcount = 0;

	return err;
}

/**
 * xilinx_dma_chan_reset - Reset DMA channel and enable interrupts
 * @chan: Driver specific DMA channel
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_reset(struct xilinx_dma_chan *chan)
{
	int err;

	/* Reset VDMA */
	err = xilinx_dma_reset(chan);
	if (err)
		return err;

	/* Enable interrupts */
	dma_ctrl_set(chan, XILINX_DMA_REG_DMACR,
		      XILINX_DMA_DMAXR_ALL_IRQ_MASK);

	return 0;
}

/**
 * xilinx_mcdma_irq_handler - MCDMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the Xilinx MCDMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t xilinx_mcdma_irq_handler(int irq, void *data)
{
	struct xilinx_dma_chan *chan = data;
	u32 status, ser_offset, chan_sermask, chan_offset = 0, chan_id;

	if (chan->direction == DMA_DEV_TO_MEM)
		ser_offset = XILINX_MCDMA_RXINT_SER_OFFSET;
	else
		ser_offset = XILINX_MCDMA_TXINT_SER_OFFSET;

	/* Read the channel id raising the interrupt*/
	chan_sermask = dma_ctrl_read(chan, ser_offset);
	chan_id = ffs(chan_sermask);

	if (!chan_id)
		return IRQ_NONE;

	if (chan->direction == DMA_DEV_TO_MEM)
		chan_offset = chan->xdev->dma_config->max_channels / 2;

	chan_offset = chan_offset + (chan_id - 1);
	chan = chan->xdev->chan[chan_offset];
	/* Read the status and ack the interrupts. */
	status = dma_ctrl_read(chan, XILINX_MCDMA_CHAN_SR_OFFSET(chan->tdest));
	if (!(status & XILINX_MCDMA_IRQ_ALL_MASK))
		return IRQ_NONE;

	dma_ctrl_write(chan, XILINX_MCDMA_CHAN_SR_OFFSET(chan->tdest),
		       status & XILINX_MCDMA_IRQ_ALL_MASK);

	if (status & XILINX_MCDMA_IRQ_ERR_MASK) {
		dev_err(chan->dev, "Channel %p has errors %x cdr %x tdr %x\n",
			chan,
			dma_ctrl_read(chan, XILINX_MCDMA_CH_ERR_OFFSET),
			dma_ctrl_read(chan, XILINX_MCDMA_CHAN_CDESC_OFFSET
				      (chan->tdest)),
			dma_ctrl_read(chan, XILINX_MCDMA_CHAN_TDESC_OFFSET
				      (chan->tdest)));
		chan->err = true;
	}

	if (status & XILINX_MCDMA_IRQ_DELAY_MASK) {
		/*
		 * Device takes too long to do the transfer when user requires
		 * responsiveness.
		 */
		dev_dbg(chan->dev, "Inter-packet latency too long\n");
	}

	if (status & XILINX_MCDMA_IRQ_IOC_MASK) {
		spin_lock(&chan->lock);
		xilinx_dma_complete_descriptor(chan);
		chan->idle = true;
		chan->start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

/**
 * xilinx_dma_irq_handler - DMA Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the Xilinx DMA channel structure
 *
 * Return: IRQ_HANDLED/IRQ_NONE
 */
static irqreturn_t xilinx_dma_irq_handler(int irq, void *data)
{
	struct xilinx_dma_chan *chan = data;
	u32 status;

	/* Read the status and ack the interrupts. */
	status = dma_ctrl_read(chan, XILINX_DMA_REG_DMASR);
	if (!(status & XILINX_DMA_DMAXR_ALL_IRQ_MASK))
		return IRQ_NONE;

	dma_ctrl_write(chan, XILINX_DMA_REG_DMASR,
			status & XILINX_DMA_DMAXR_ALL_IRQ_MASK);

	if (status & XILINX_DMA_DMASR_ERR_IRQ) {
		/*
		 * An error occurred. If C_FLUSH_ON_FSYNC is enabled and the
		 * error is recoverable, ignore it. Otherwise flag the error.
		 *
		 * Only recoverable errors can be cleared in the DMASR register,
		 * make sure not to write to other error bits to 1.
		 */
		u32 errors = status & XILINX_DMA_DMASR_ALL_ERR_MASK;

		dma_ctrl_write(chan, XILINX_DMA_REG_DMASR,
				errors & XILINX_DMA_DMASR_ERR_RECOVER_MASK);

		if (!chan->flush_on_fsync ||
		    (errors & ~XILINX_DMA_DMASR_ERR_RECOVER_MASK)) {
			dev_err(chan->dev,
				"Channel %p has errors %x, cdr %x tdr %x\n",
				chan, errors,
				dma_ctrl_read(chan, XILINX_DMA_REG_CURDESC),
				dma_ctrl_read(chan, XILINX_DMA_REG_TAILDESC));
			chan->err = true;
		}
	}

	if (status & XILINX_DMA_DMASR_DLY_CNT_IRQ) {
		/*
		 * Device takes too long to do the transfer when user requires
		 * responsiveness.
		 */
		dev_dbg(chan->dev, "Inter-packet latency too long\n");
	}

	if (status & XILINX_DMA_DMASR_FRM_CNT_IRQ) {
		spin_lock(&chan->lock);
		xilinx_dma_complete_descriptor(chan);
		chan->idle = true;
		chan->start_transfer(chan);
		spin_unlock(&chan->lock);
	}

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

/**
 * append_desc_queue - Queuing descriptor
 * @chan: Driver specific dma channel
 * @desc: dma transaction descriptor
 */
static void append_desc_queue(struct xilinx_dma_chan *chan,
			      struct xilinx_dma_tx_descriptor *desc)
{
	struct xilinx_vdma_tx_segment *tail_segment;
	struct xilinx_dma_tx_descriptor *tail_desc;
	struct xilinx_axidma_tx_segment *axidma_tail_segment;
	struct xilinx_aximcdma_tx_segment *aximcdma_tail_segment;
	struct xilinx_cdma_tx_segment *cdma_tail_segment;

	if (list_empty(&chan->pending_list))
		goto append;

	/*
	 * Add the hardware descriptor to the chain of hardware descriptors
	 * that already exists in memory.
	 */
	tail_desc = list_last_entry(&chan->pending_list,
				    struct xilinx_dma_tx_descriptor, node);
	if (chan->xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
		tail_segment = list_last_entry(&tail_desc->segments,
					       struct xilinx_vdma_tx_segment,
					       node);
		tail_segment->hw.next_desc = (u32)desc->async_tx.phys;
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
		cdma_tail_segment = list_last_entry(&tail_desc->segments,
						struct xilinx_cdma_tx_segment,
						node);
		cdma_tail_segment->hw.next_desc = (u32)desc->async_tx.phys;
	} else if (chan->xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		axidma_tail_segment = list_last_entry(&tail_desc->segments,
					       struct xilinx_axidma_tx_segment,
					       node);
		axidma_tail_segment->hw.next_desc = (u32)desc->async_tx.phys;
	} else {
		aximcdma_tail_segment =
			list_last_entry(&tail_desc->segments,
					struct xilinx_aximcdma_tx_segment,
					node);
		aximcdma_tail_segment->hw.next_desc = (u32)desc->async_tx.phys;
	}

	/*
	 * Add the software descriptor and all children to the list
	 * of pending transactions
	 */
append:
	list_add_tail(&desc->node, &chan->pending_list);
	chan->desc_pendingcount++;

	if (chan->has_sg && (chan->xdev->dma_config->dmatype == XDMA_TYPE_VDMA)
	    && unlikely(chan->desc_pendingcount > chan->num_frms)) {
		dev_dbg(chan->dev, "desc pendingcount is too high\n");
		chan->desc_pendingcount = chan->num_frms;
	}
}

/**
 * xilinx_dma_tx_submit - Submit DMA transaction
 * @tx: Async transaction descriptor
 *
 * Return: cookie value on success and failure value on error
 */
static dma_cookie_t xilinx_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_dma_tx_descriptor *desc = to_dma_tx_descriptor(tx);
	struct xilinx_dma_chan *chan = to_xilinx_chan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;
	int err;

	if (chan->cyclic) {
		xilinx_dma_free_tx_descriptor(chan, desc);
		return -EBUSY;
	}

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		err = xilinx_dma_chan_reset(chan);
		if (err < 0)
			return err;
	}

	spin_lock_irqsave(&chan->lock, flags);

	cookie = dma_cookie_assign(tx);

	/* Put this transaction onto the tail of the pending queue */
	append_desc_queue(chan, desc);

	if (desc->cyclic)
		chan->cyclic = true;

	chan->terminating = false;

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

/**
 * xilinx_vdma_dma_prep_interleaved - prepare a descriptor for a
 *	DMA_SLAVE transaction
 * @dchan: DMA channel
 * @xt: Interleaved template pointer
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_vdma_dma_prep_interleaved(struct dma_chan *dchan,
				 struct dma_interleaved_template *xt,
				 unsigned long flags)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_vdma_tx_segment *segment;
	struct xilinx_vdma_desc_hw *hw;

	if (!is_slave_direction(xt->dir))
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	if (xt->frame_size != 1)
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;
	async_tx_ack(&desc->async_tx);

	/* Allocate the link descriptor from DMA pool */
	segment = xilinx_vdma_alloc_tx_segment(chan);
	if (!segment)
		goto error;

	/* Fill in the hardware descriptor */
	hw = &segment->hw;
	hw->vsize = xt->numf;
	hw->hsize = xt->sgl[0].size;
	hw->stride = (xt->sgl[0].icg + xt->sgl[0].size) <<
			XILINX_DMA_FRMDLY_STRIDE_STRIDE_SHIFT;
	hw->stride |= chan->config.frm_dly <<
			XILINX_DMA_FRMDLY_STRIDE_FRMDLY_SHIFT;

	if (xt->dir != DMA_MEM_TO_DEV) {
		if (chan->ext_addr) {
			hw->buf_addr = lower_32_bits(xt->dst_start);
			hw->buf_addr_msb = upper_32_bits(xt->dst_start);
		} else {
			hw->buf_addr = xt->dst_start;
		}
	} else {
		if (chan->ext_addr) {
			hw->buf_addr = lower_32_bits(xt->src_start);
			hw->buf_addr_msb = upper_32_bits(xt->src_start);
		} else {
			hw->buf_addr = xt->src_start;
		}
	}

	/* Insert the segment into the descriptor segments list. */
	list_add_tail(&segment->node, &desc->segments);

	/* Link the last hardware descriptor with the first. */
	segment = list_first_entry(&desc->segments,
				   struct xilinx_vdma_tx_segment, node);
	desc->async_tx.phys = segment->phys;

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_cdma_prep_memcpy - prepare descriptors for a memcpy transaction
 * @dchan: DMA channel
 * @dma_dst: destination address
 * @dma_src: source address
 * @len: transfer length
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_cdma_prep_memcpy(struct dma_chan *dchan, dma_addr_t dma_dst,
			dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_cdma_tx_segment *segment;
	struct xilinx_cdma_desc_hw *hw;

	if (!len || len > chan->xdev->max_buffer_len)
		return NULL;

	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	/* Allocate the link descriptor from DMA pool */
	segment = xilinx_cdma_alloc_tx_segment(chan);
	if (!segment)
		goto error;

	hw = &segment->hw;
	hw->control = len;
	hw->src_addr = dma_src;
	hw->dest_addr = dma_dst;
	if (chan->ext_addr) {
		hw->src_addr_msb = upper_32_bits(dma_src);
		hw->dest_addr_msb = upper_32_bits(dma_dst);
	}

	/* Insert the segment into the descriptor segments list. */
	list_add_tail(&segment->node, &desc->segments);

	desc->async_tx.phys = segment->phys;
	hw->next_desc = segment->phys;

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_dma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 * @context: APP words of the descriptor
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_slave_sg(
	struct dma_chan *dchan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_axidma_tx_segment *segment = NULL;
	u32 *app_w = (u32 *)context;
	struct scatterlist *sg;
	size_t copy;
	size_t sg_used;
	unsigned int i;

	if (!is_slave_direction(direction))
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	/* Build transactions using information in the scatter gather list */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {
			struct xilinx_axidma_desc_hw *hw;

			/* Get a free segment */
			segment = xilinx_axidma_alloc_tx_segment(chan);
			if (!segment)
				goto error;

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = xilinx_dma_calc_copysize(chan, sg_dma_len(sg),
							sg_used);
			hw = &segment->hw;

			/* Fill in the descriptor */
			xilinx_axidma_buf(chan, hw, sg_dma_address(sg),
					  sg_used, 0);

			hw->control = copy;

			if (chan->direction == DMA_MEM_TO_DEV) {
				if (app_w)
					memcpy(hw->app, app_w, sizeof(u32) *
					       XILINX_DMA_NUM_APP_WORDS);
			}

			sg_used += copy;

			/*
			 * Insert the segment into the descriptor segments
			 * list.
			 */
			list_add_tail(&segment->node, &desc->segments);
		}
	}

	segment = list_first_entry(&desc->segments,
				   struct xilinx_axidma_tx_segment, node);
	desc->async_tx.phys = segment->phys;

	/* For the last DMA_MEM_TO_DEV transfer, set EOP */
	if (chan->direction == DMA_MEM_TO_DEV) {
		segment->hw.control |= XILINX_DMA_BD_SOP;
		segment = list_last_entry(&desc->segments,
					  struct xilinx_axidma_tx_segment,
					  node);
		segment->hw.control |= XILINX_DMA_BD_EOP;
	}

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_dma_prep_dma_cyclic - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @buf_addr: Physical address of the buffer
 * @buf_len: Total length of the cyclic buffers
 * @period_len: length of individual cyclic buffer
 * @direction: DMA direction
 * @flags: transfer ack flags
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *xilinx_dma_prep_dma_cyclic(
	struct dma_chan *dchan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_axidma_tx_segment *segment, *head_segment, *prev = NULL;
	size_t copy, sg_used;
	unsigned int num_periods;
	int i;
	u32 reg;

	if (!period_len)
		return NULL;

	num_periods = buf_len / period_len;

	if (!num_periods)
		return NULL;

	if (!is_slave_direction(direction))
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	chan->direction = direction;
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	for (i = 0; i < num_periods; ++i) {
		sg_used = 0;

		while (sg_used < period_len) {
			struct xilinx_axidma_desc_hw *hw;

			/* Get a free segment */
			segment = xilinx_axidma_alloc_tx_segment(chan);
			if (!segment)
				goto error;

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = xilinx_dma_calc_copysize(chan, period_len,
							sg_used);
			hw = &segment->hw;
			xilinx_axidma_buf(chan, hw, buf_addr, sg_used,
					  period_len * i);
			hw->control = copy;

			if (prev)
				prev->hw.next_desc = segment->phys;

			prev = segment;
			sg_used += copy;

			/*
			 * Insert the segment into the descriptor segments
			 * list.
			 */
			list_add_tail(&segment->node, &desc->segments);
		}
	}

	head_segment = list_first_entry(&desc->segments,
				   struct xilinx_axidma_tx_segment, node);
	desc->async_tx.phys = head_segment->phys;

	desc->cyclic = true;
	reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);
	reg |= XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
	dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);

	segment = list_last_entry(&desc->segments,
				  struct xilinx_axidma_tx_segment,
				  node);
	segment->hw.next_desc = (u32) head_segment->phys;

	/* For the last DMA_MEM_TO_DEV transfer, set EOP */
	if (direction == DMA_MEM_TO_DEV) {
		head_segment->hw.control |= XILINX_DMA_BD_SOP;
		segment->hw.control |= XILINX_DMA_BD_EOP;
	}

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);
	return NULL;
}

/**
 * xilinx_mcdma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @dchan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: transfer ack flags
 * @context: APP words of the descriptor
 *
 * Return: Async transaction descriptor on success and NULL on failure
 */
static struct dma_async_tx_descriptor *
xilinx_mcdma_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
			   unsigned int sg_len,
			   enum dma_transfer_direction direction,
			   unsigned long flags, void *context)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	struct xilinx_dma_tx_descriptor *desc;
	struct xilinx_aximcdma_tx_segment *segment = NULL;
	u32 *app_w = (u32 *)context;
	struct scatterlist *sg;
	size_t copy;
	size_t sg_used;
	unsigned int i;

	if (!is_slave_direction(direction))
		return NULL;

	/* Allocate a transaction descriptor. */
	desc = xilinx_dma_alloc_tx_descriptor(chan);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_dma_tx_submit;

	/* Build transactions using information in the scatter gather list */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {
			struct xilinx_aximcdma_desc_hw *hw;

			/* Get a free segment */
			segment = xilinx_aximcdma_alloc_tx_segment(chan);
			if (!segment)
				goto error;

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the hw limit
			 */
			copy = min_t(size_t, sg_dma_len(sg) - sg_used,
				     chan->xdev->max_buffer_len);
			hw = &segment->hw;

			/* Fill in the descriptor */
			xilinx_aximcdma_buf(chan, hw, sg_dma_address(sg),
					    sg_used);
			hw->control = copy;

			if (chan->direction == DMA_MEM_TO_DEV && app_w) {
				memcpy(hw->app, app_w, sizeof(u32) *
				       XILINX_DMA_NUM_APP_WORDS);
			}

			sg_used += copy;
			/*
			 * Insert the segment into the descriptor segments
			 * list.
			 */
			list_add_tail(&segment->node, &desc->segments);
		}
	}

	segment = list_first_entry(&desc->segments,
				   struct xilinx_aximcdma_tx_segment, node);
	desc->async_tx.phys = segment->phys;

	/* For the last DMA_MEM_TO_DEV transfer, set EOP */
	if (chan->direction == DMA_MEM_TO_DEV) {
		segment->hw.control |= XILINX_MCDMA_BD_SOP;
		segment = list_last_entry(&desc->segments,
					  struct xilinx_aximcdma_tx_segment,
					  node);
		segment->hw.control |= XILINX_MCDMA_BD_EOP;
	}

	return &desc->async_tx;

error:
	xilinx_dma_free_tx_descriptor(chan, desc);

	return NULL;
}

/**
 * xilinx_dma_terminate_all - Halt the channel and free descriptors
 * @dchan: Driver specific DMA Channel pointer
 *
 * Return: '0' always.
 */
static int xilinx_dma_terminate_all(struct dma_chan *dchan)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	u32 reg;
	int err;

	if (!chan->cyclic) {
		err = chan->stop_transfer(chan);
		if (err) {
			dev_err(chan->dev, "Cannot stop channel %p: %x\n",
				chan, dma_ctrl_read(chan,
				XILINX_DMA_REG_DMASR));
			chan->err = true;
		}
	}

	xilinx_dma_chan_reset(chan);
	/* Remove and free all of the descriptors in the lists */
	chan->terminating = true;
	xilinx_dma_free_descriptors(chan);
	chan->idle = true;

	if (chan->cyclic) {
		reg = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);
		reg &= ~XILINX_DMA_CR_CYCLIC_BD_EN_MASK;
		dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, reg);
		chan->cyclic = false;
	}

	if ((chan->xdev->dma_config->dmatype == XDMA_TYPE_CDMA) && chan->has_sg)
		dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR,
			     XILINX_CDMA_CR_SGMODE);

	return 0;
}

/**
 * xilinx_dma_channel_set_config - Configure VDMA channel
 * Run-time configuration for Axi VDMA, supports:
 * . halt the channel
 * . configure interrupt coalescing and inter-packet delay threshold
 * . start/stop parking
 * . enable genlock
 *
 * @dchan: DMA channel
 * @cfg: VDMA device configuration pointer
 *
 * Return: '0' on success and failure value on error
 */
int xilinx_vdma_channel_set_config(struct dma_chan *dchan,
					struct xilinx_vdma_config *cfg)
{
	struct xilinx_dma_chan *chan = to_xilinx_chan(dchan);
	u32 dmacr;

	if (cfg->reset)
		return xilinx_dma_chan_reset(chan);

	dmacr = dma_ctrl_read(chan, XILINX_DMA_REG_DMACR);

	chan->config.frm_dly = cfg->frm_dly;
	chan->config.park = cfg->park;

	/* genlock settings */
	chan->config.gen_lock = cfg->gen_lock;
	chan->config.master = cfg->master;

	dmacr &= ~XILINX_DMA_DMACR_GENLOCK_EN;
	if (cfg->gen_lock && chan->genlock) {
		dmacr |= XILINX_DMA_DMACR_GENLOCK_EN;
		dmacr &= ~XILINX_DMA_DMACR_MASTER_MASK;
		dmacr |= cfg->master << XILINX_DMA_DMACR_MASTER_SHIFT;
	}

	chan->config.frm_cnt_en = cfg->frm_cnt_en;
	chan->config.vflip_en = cfg->vflip_en;

	if (cfg->park)
		chan->config.park_frm = cfg->park_frm;
	else
		chan->config.park_frm = -1;

	chan->config.coalesc = cfg->coalesc;
	chan->config.delay = cfg->delay;

	if (cfg->coalesc <= XILINX_DMA_DMACR_FRAME_COUNT_MAX) {
		dmacr &= ~XILINX_DMA_DMACR_FRAME_COUNT_MASK;
		dmacr |= cfg->coalesc << XILINX_DMA_DMACR_FRAME_COUNT_SHIFT;
		chan->config.coalesc = cfg->coalesc;
	}

	if (cfg->delay <= XILINX_DMA_DMACR_DELAY_MAX) {
		dmacr &= ~XILINX_DMA_DMACR_DELAY_MASK;
		dmacr |= cfg->delay << XILINX_DMA_DMACR_DELAY_SHIFT;
		chan->config.delay = cfg->delay;
	}

	/* FSync Source selection */
	dmacr &= ~XILINX_DMA_DMACR_FSYNCSRC_MASK;
	dmacr |= cfg->ext_fsync << XILINX_DMA_DMACR_FSYNCSRC_SHIFT;

	dma_ctrl_write(chan, XILINX_DMA_REG_DMACR, dmacr);

	return 0;
}
EXPORT_SYMBOL(xilinx_vdma_channel_set_config);

/* -----------------------------------------------------------------------------
 * Probe and remove
 */

/**
 * xilinx_dma_chan_remove - Per Channel remove function
 * @chan: Driver specific DMA channel
 */
static void xilinx_dma_chan_remove(struct xilinx_dma_chan *chan)
{
	/* Disable all interrupts */
	dma_ctrl_clr(chan, XILINX_DMA_REG_DMACR,
		      XILINX_DMA_DMAXR_ALL_IRQ_MASK);

	if (chan->irq > 0)
		free_irq(chan->irq, chan);

	tasklet_kill(&chan->tasklet);

	list_del(&chan->common.device_node);
}

static int axidma_clk_init(struct platform_device *pdev, struct clk **axi_clk,
			    struct clk **tx_clk, struct clk **rx_clk,
			    struct clk **sg_clk, struct clk **tmp_clk)
{
	int err;

	*tmp_clk = NULL;

	*axi_clk = devm_clk_get(&pdev->dev, "s_axi_lite_aclk");
	if (IS_ERR(*axi_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(*axi_clk), "failed to get axi_aclk\n");

	*tx_clk = devm_clk_get(&pdev->dev, "m_axi_mm2s_aclk");
	if (IS_ERR(*tx_clk))
		*tx_clk = NULL;

	*rx_clk = devm_clk_get(&pdev->dev, "m_axi_s2mm_aclk");
	if (IS_ERR(*rx_clk))
		*rx_clk = NULL;

	*sg_clk = devm_clk_get(&pdev->dev, "m_axi_sg_aclk");
	if (IS_ERR(*sg_clk))
		*sg_clk = NULL;

	err = clk_prepare_enable(*axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*tx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable tx_clk (%d)\n", err);
		goto err_disable_axiclk;
	}

	err = clk_prepare_enable(*rx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable rx_clk (%d)\n", err);
		goto err_disable_txclk;
	}

	err = clk_prepare_enable(*sg_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable sg_clk (%d)\n", err);
		goto err_disable_rxclk;
	}

	return 0;

err_disable_rxclk:
	clk_disable_unprepare(*rx_clk);
err_disable_txclk:
	clk_disable_unprepare(*tx_clk);
err_disable_axiclk:
	clk_disable_unprepare(*axi_clk);

	return err;
}

static int axicdma_clk_init(struct platform_device *pdev, struct clk **axi_clk,
			    struct clk **dev_clk, struct clk **tmp_clk,
			    struct clk **tmp1_clk, struct clk **tmp2_clk)
{
	int err;

	*tmp_clk = NULL;
	*tmp1_clk = NULL;
	*tmp2_clk = NULL;

	*axi_clk = devm_clk_get(&pdev->dev, "s_axi_lite_aclk");
	if (IS_ERR(*axi_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(*axi_clk), "failed to get axi_aclk\n");

	*dev_clk = devm_clk_get(&pdev->dev, "m_axi_aclk");
	if (IS_ERR(*dev_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(*dev_clk), "failed to get dev_clk\n");

	err = clk_prepare_enable(*axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*dev_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dev_clk (%d)\n", err);
		goto err_disable_axiclk;
	}

	return 0;

err_disable_axiclk:
	clk_disable_unprepare(*axi_clk);

	return err;
}

static int axivdma_clk_init(struct platform_device *pdev, struct clk **axi_clk,
			    struct clk **tx_clk, struct clk **txs_clk,
			    struct clk **rx_clk, struct clk **rxs_clk)
{
	int err;

	*axi_clk = devm_clk_get(&pdev->dev, "s_axi_lite_aclk");
	if (IS_ERR(*axi_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(*axi_clk), "failed to get axi_aclk\n");

	*tx_clk = devm_clk_get(&pdev->dev, "m_axi_mm2s_aclk");
	if (IS_ERR(*tx_clk))
		*tx_clk = NULL;

	*txs_clk = devm_clk_get(&pdev->dev, "m_axis_mm2s_aclk");
	if (IS_ERR(*txs_clk))
		*txs_clk = NULL;

	*rx_clk = devm_clk_get(&pdev->dev, "m_axi_s2mm_aclk");
	if (IS_ERR(*rx_clk))
		*rx_clk = NULL;

	*rxs_clk = devm_clk_get(&pdev->dev, "s_axis_s2mm_aclk");
	if (IS_ERR(*rxs_clk))
		*rxs_clk = NULL;

	err = clk_prepare_enable(*axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n",
			err);
		return err;
	}

	err = clk_prepare_enable(*tx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable tx_clk (%d)\n", err);
		goto err_disable_axiclk;
	}

	err = clk_prepare_enable(*txs_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable txs_clk (%d)\n", err);
		goto err_disable_txclk;
	}

	err = clk_prepare_enable(*rx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable rx_clk (%d)\n", err);
		goto err_disable_txsclk;
	}

	err = clk_prepare_enable(*rxs_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable rxs_clk (%d)\n", err);
		goto err_disable_rxclk;
	}

	return 0;

err_disable_rxclk:
	clk_disable_unprepare(*rx_clk);
err_disable_txsclk:
	clk_disable_unprepare(*txs_clk);
err_disable_txclk:
	clk_disable_unprepare(*tx_clk);
err_disable_axiclk:
	clk_disable_unprepare(*axi_clk);

	return err;
}

static void xdma_disable_allclks(struct xilinx_dma_device *xdev)
{
	clk_disable_unprepare(xdev->rxs_clk);
	clk_disable_unprepare(xdev->rx_clk);
	clk_disable_unprepare(xdev->txs_clk);
	clk_disable_unprepare(xdev->tx_clk);
	clk_disable_unprepare(xdev->axi_clk);
}

/**
 * xilinx_dma_chan_probe - Per Channel Probing
 * It get channel features from the device tree entry and
 * initialize special channel handling routines
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_chan_probe(struct xilinx_dma_device *xdev,
				  struct device_node *node)
{
	struct xilinx_dma_chan *chan;
	bool has_dre = false;
	u32 value, width;
	int err;

	/* Allocate and initialize the channel structure */
	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->dev = xdev->dev;
	chan->xdev = xdev;
	chan->desc_pendingcount = 0x0;
	chan->ext_addr = xdev->ext_addr;
	/* This variable ensures that descriptors are not
	 * Submitted when dma engine is in progress. This variable is
	 * Added to avoid polling for a bit in the status register to
	 * Know dma state in the driver hot path.
	 */
	chan->idle = true;

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->done_list);
	INIT_LIST_HEAD(&chan->active_list);
	INIT_LIST_HEAD(&chan->free_seg_list);

	/* Retrieve the channel properties from the device tree */
	has_dre = of_property_read_bool(node, "xlnx,include-dre");

	chan->genlock = of_property_read_bool(node, "xlnx,genlock-mode");

	err = of_property_read_u32(node, "xlnx,datawidth", &value);
	if (err) {
		dev_err(xdev->dev, "missing xlnx,datawidth property\n");
		return err;
	}
	width = value >> 3; /* Convert bits to bytes */

	/* If data width is greater than 8 bytes, DRE is not in hw */
	if (width > 8)
		has_dre = false;

	if (!has_dre)
		xdev->common.copy_align = (enum dmaengine_alignment)fls(width - 1);

	if (of_device_is_compatible(node, "xlnx,axi-vdma-mm2s-channel") ||
	    of_device_is_compatible(node, "xlnx,axi-dma-mm2s-channel") ||
	    of_device_is_compatible(node, "xlnx,axi-cdma-channel")) {
		chan->direction = DMA_MEM_TO_DEV;
		chan->id = xdev->mm2s_chan_id++;
		chan->tdest = chan->id;

		chan->ctrl_offset = XILINX_DMA_MM2S_CTRL_OFFSET;
		if (xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
			chan->desc_offset = XILINX_VDMA_MM2S_DESC_OFFSET;
			chan->config.park = 1;

			if (xdev->flush_on_fsync == XILINX_DMA_FLUSH_BOTH ||
			    xdev->flush_on_fsync == XILINX_DMA_FLUSH_MM2S)
				chan->flush_on_fsync = true;
		}
	} else if (of_device_is_compatible(node,
					   "xlnx,axi-vdma-s2mm-channel") ||
		   of_device_is_compatible(node,
					   "xlnx,axi-dma-s2mm-channel")) {
		chan->direction = DMA_DEV_TO_MEM;
		chan->id = xdev->s2mm_chan_id++;
		chan->tdest = chan->id - xdev->dma_config->max_channels / 2;
		chan->has_vflip = of_property_read_bool(node,
					"xlnx,enable-vert-flip");
		if (chan->has_vflip) {
			chan->config.vflip_en = dma_read(chan,
				XILINX_VDMA_REG_ENABLE_VERTICAL_FLIP) &
				XILINX_VDMA_ENABLE_VERTICAL_FLIP;
		}

		if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA)
			chan->ctrl_offset = XILINX_MCDMA_S2MM_CTRL_OFFSET;
		else
			chan->ctrl_offset = XILINX_DMA_S2MM_CTRL_OFFSET;

		if (xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
			chan->desc_offset = XILINX_VDMA_S2MM_DESC_OFFSET;
			chan->config.park = 1;

			if (xdev->flush_on_fsync == XILINX_DMA_FLUSH_BOTH ||
			    xdev->flush_on_fsync == XILINX_DMA_FLUSH_S2MM)
				chan->flush_on_fsync = true;
		}
	} else {
		dev_err(xdev->dev, "Invalid channel compatible node\n");
		return -EINVAL;
	}

	/* Request the interrupt */
	chan->irq = irq_of_parse_and_map(node, chan->tdest);
	err = request_irq(chan->irq, xdev->dma_config->irq_handler,
			  IRQF_SHARED, "xilinx-dma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ %d\n", chan->irq);
		return err;
	}

	if (xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		chan->start_transfer = xilinx_dma_start_transfer;
		chan->stop_transfer = xilinx_dma_stop_transfer;
	} else if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA) {
		chan->start_transfer = xilinx_mcdma_start_transfer;
		chan->stop_transfer = xilinx_dma_stop_transfer;
	} else if (xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
		chan->start_transfer = xilinx_cdma_start_transfer;
		chan->stop_transfer = xilinx_cdma_stop_transfer;
	} else {
		chan->start_transfer = xilinx_vdma_start_transfer;
		chan->stop_transfer = xilinx_dma_stop_transfer;
	}

	/* check if SG is enabled (only for AXIDMA, AXIMCDMA, and CDMA) */
	if (xdev->dma_config->dmatype != XDMA_TYPE_VDMA) {
		if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA ||
		    dma_ctrl_read(chan, XILINX_DMA_REG_DMASR) &
			    XILINX_DMA_DMASR_SG_MASK)
			chan->has_sg = true;
		dev_dbg(chan->dev, "ch %d: SG %s\n", chan->id,
			chan->has_sg ? "enabled" : "disabled");
	}

	/* Initialize the tasklet */
	tasklet_setup(&chan->tasklet, xilinx_dma_do_tasklet);

	/*
	 * Initialize the DMA channel and add it to the DMA engine channels
	 * list.
	 */
	chan->common.device = &xdev->common;

	list_add_tail(&chan->common.device_node, &xdev->common.channels);
	xdev->chan[chan->id] = chan;

	/* Reset the channel */
	err = xilinx_dma_chan_reset(chan);
	if (err < 0) {
		dev_err(xdev->dev, "Reset channel failed\n");
		return err;
	}

	return 0;
}

/**
 * xilinx_dma_child_probe - Per child node probe
 * It get number of dma-channels per child node from
 * device-tree and initializes all the channels.
 *
 * @xdev: Driver specific device structure
 * @node: Device node
 *
 * Return: 0 always.
 */
static int xilinx_dma_child_probe(struct xilinx_dma_device *xdev,
				    struct device_node *node)
{
	int ret, i;
	u32 nr_channels = 1;

	ret = of_property_read_u32(node, "dma-channels", &nr_channels);
	if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA && ret < 0)
		dev_warn(xdev->dev, "missing dma-channels property\n");

	for (i = 0; i < nr_channels; i++)
		xilinx_dma_chan_probe(xdev, node);

	return 0;
}

/**
 * of_dma_xilinx_xlate - Translation function
 * @dma_spec: Pointer to DMA specifier as found in the device tree
 * @ofdma: Pointer to DMA controller data
 *
 * Return: DMA channel pointer on success and NULL on error
 */
static struct dma_chan *of_dma_xilinx_xlate(struct of_phandle_args *dma_spec,
						struct of_dma *ofdma)
{
	struct xilinx_dma_device *xdev = ofdma->of_dma_data;
	int chan_id = dma_spec->args[0];

	if (chan_id >= xdev->dma_config->max_channels || !xdev->chan[chan_id])
		return NULL;

	return dma_get_slave_channel(&xdev->chan[chan_id]->common);
}

static const struct xilinx_dma_config axidma_config = {
	.dmatype = XDMA_TYPE_AXIDMA,
	.clk_init = axidma_clk_init,
	.irq_handler = xilinx_dma_irq_handler,
	.max_channels = XILINX_DMA_MAX_CHANS_PER_DEVICE,
};

static const struct xilinx_dma_config aximcdma_config = {
	.dmatype = XDMA_TYPE_AXIMCDMA,
	.clk_init = axidma_clk_init,
	.irq_handler = xilinx_mcdma_irq_handler,
	.max_channels = XILINX_MCDMA_MAX_CHANS_PER_DEVICE,
};
static const struct xilinx_dma_config axicdma_config = {
	.dmatype = XDMA_TYPE_CDMA,
	.clk_init = axicdma_clk_init,
	.irq_handler = xilinx_dma_irq_handler,
	.max_channels = XILINX_CDMA_MAX_CHANS_PER_DEVICE,
};

static const struct xilinx_dma_config axivdma_config = {
	.dmatype = XDMA_TYPE_VDMA,
	.clk_init = axivdma_clk_init,
	.irq_handler = xilinx_dma_irq_handler,
	.max_channels = XILINX_DMA_MAX_CHANS_PER_DEVICE,
};

static const struct of_device_id xilinx_dma_of_ids[] = {
	{ .compatible = "xlnx,axi-dma-1.00.a", .data = &axidma_config },
	{ .compatible = "xlnx,axi-cdma-1.00.a", .data = &axicdma_config },
	{ .compatible = "xlnx,axi-vdma-1.00.a", .data = &axivdma_config },
	{ .compatible = "xlnx,axi-mcdma-1.00.a", .data = &aximcdma_config },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_dma_of_ids);

/**
 * xilinx_dma_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: '0' on success and failure value on error
 */
static int xilinx_dma_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *, struct clk **, struct clk **,
			struct clk **, struct clk **, struct clk **)
					= axivdma_clk_init;
	struct device_node *node = pdev->dev.of_node;
	struct xilinx_dma_device *xdev;
	struct device_node *child, *np = pdev->dev.of_node;
	u32 num_frames, addr_width, len_width;
	int i, err;

	/* Allocate and initialize the DMA engine structure */
	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &pdev->dev;
	if (np) {
		const struct of_device_id *match;

		match = of_match_node(xilinx_dma_of_ids, np);
		if (match && match->data) {
			xdev->dma_config = match->data;
			clk_init = xdev->dma_config->clk_init;
		}
	}

	err = clk_init(pdev, &xdev->axi_clk, &xdev->tx_clk, &xdev->txs_clk,
		       &xdev->rx_clk, &xdev->rxs_clk);
	if (err)
		return err;

	/* Request and map I/O memory */
	xdev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->regs)) {
		err = PTR_ERR(xdev->regs);
		goto disable_clks;
	}
	/* Retrieve the DMA engine properties from the device tree */
	xdev->max_buffer_len = GENMASK(XILINX_DMA_MAX_TRANS_LEN_MAX - 1, 0);
	xdev->s2mm_chan_id = xdev->dma_config->max_channels / 2;

	if (xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA ||
	    xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA) {
		if (!of_property_read_u32(node, "xlnx,sg-length-width",
					  &len_width)) {
			if (len_width < XILINX_DMA_MAX_TRANS_LEN_MIN ||
			    len_width > XILINX_DMA_V2_MAX_TRANS_LEN_MAX) {
				dev_warn(xdev->dev,
					 "invalid xlnx,sg-length-width property value. Using default width\n");
			} else {
				if (len_width > XILINX_DMA_MAX_TRANS_LEN_MAX)
					dev_warn(xdev->dev, "Please ensure that IP supports buffer length > 23 bits\n");
				xdev->max_buffer_len =
					GENMASK(len_width - 1, 0);
			}
		}
	}

	if (xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
		err = of_property_read_u32(node, "xlnx,num-fstores",
					   &num_frames);
		if (err < 0) {
			dev_err(xdev->dev,
				"missing xlnx,num-fstores property\n");
			goto disable_clks;
		}

		err = of_property_read_u32(node, "xlnx,flush-fsync",
					   &xdev->flush_on_fsync);
		if (err < 0)
			dev_warn(xdev->dev,
				 "missing xlnx,flush-fsync property\n");
	}

	err = of_property_read_u32(node, "xlnx,addrwidth", &addr_width);
	if (err < 0)
		dev_warn(xdev->dev, "missing xlnx,addrwidth property\n");

	if (addr_width > 32)
		xdev->ext_addr = true;
	else
		xdev->ext_addr = false;

	/* Set the dma mask bits */
	err = dma_set_mask_and_coherent(xdev->dev, DMA_BIT_MASK(addr_width));
	if (err < 0) {
		dev_err(xdev->dev, "DMA mask error %d\n", err);
		goto disable_clks;
	}

	/* Initialize the DMA engine */
	xdev->common.dev = &pdev->dev;

	INIT_LIST_HEAD(&xdev->common.channels);
	if (!(xdev->dma_config->dmatype == XDMA_TYPE_CDMA)) {
		dma_cap_set(DMA_SLAVE, xdev->common.cap_mask);
		dma_cap_set(DMA_PRIVATE, xdev->common.cap_mask);
	}

	xdev->common.device_alloc_chan_resources =
				xilinx_dma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
				xilinx_dma_free_chan_resources;
	xdev->common.device_terminate_all = xilinx_dma_terminate_all;
	xdev->common.device_tx_status = xilinx_dma_tx_status;
	xdev->common.device_issue_pending = xilinx_dma_issue_pending;
	if (xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA) {
		dma_cap_set(DMA_CYCLIC, xdev->common.cap_mask);
		xdev->common.device_prep_slave_sg = xilinx_dma_prep_slave_sg;
		xdev->common.device_prep_dma_cyclic =
					  xilinx_dma_prep_dma_cyclic;
		/* Residue calculation is supported by only AXI DMA and CDMA */
		xdev->common.residue_granularity =
					  DMA_RESIDUE_GRANULARITY_SEGMENT;
	} else if (xdev->dma_config->dmatype == XDMA_TYPE_CDMA) {
		dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);
		xdev->common.device_prep_dma_memcpy = xilinx_cdma_prep_memcpy;
		/* Residue calculation is supported by only AXI DMA and CDMA */
		xdev->common.residue_granularity =
					  DMA_RESIDUE_GRANULARITY_SEGMENT;
	} else if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA) {
		xdev->common.device_prep_slave_sg = xilinx_mcdma_prep_slave_sg;
	} else {
		xdev->common.device_prep_interleaved_dma =
				xilinx_vdma_dma_prep_interleaved;
	}

	platform_set_drvdata(pdev, xdev);

	/* Initialize the channels */
	for_each_child_of_node(node, child) {
		err = xilinx_dma_child_probe(xdev, child);
		if (err < 0) {
			of_node_put(child);
			goto error;
		}
	}

	if (xdev->dma_config->dmatype == XDMA_TYPE_VDMA) {
		for (i = 0; i < xdev->dma_config->max_channels; i++)
			if (xdev->chan[i])
				xdev->chan[i]->num_frms = num_frames;
	}

	/* Register the DMA engine with the core */
	err = dma_async_device_register(&xdev->common);
	if (err) {
		dev_err(xdev->dev, "failed to register the dma device\n");
		goto error;
	}

	err = of_dma_controller_register(node, of_dma_xilinx_xlate,
					 xdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to register DMA to DT\n");
		dma_async_device_unregister(&xdev->common);
		goto error;
	}

	if (xdev->dma_config->dmatype == XDMA_TYPE_AXIDMA)
		dev_info(&pdev->dev, "Xilinx AXI DMA Engine Driver Probed!!\n");
	else if (xdev->dma_config->dmatype == XDMA_TYPE_CDMA)
		dev_info(&pdev->dev, "Xilinx AXI CDMA Engine Driver Probed!!\n");
	else if (xdev->dma_config->dmatype == XDMA_TYPE_AXIMCDMA)
		dev_info(&pdev->dev, "Xilinx AXI MCDMA Engine Driver Probed!!\n");
	else
		dev_info(&pdev->dev, "Xilinx AXI VDMA Engine Driver Probed!!\n");

	return 0;

error:
	for (i = 0; i < xdev->dma_config->max_channels; i++)
		if (xdev->chan[i])
			xilinx_dma_chan_remove(xdev->chan[i]);
disable_clks:
	xdma_disable_allclks(xdev);

	return err;
}

/**
 * xilinx_dma_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Return: Always '0'
 */
static int xilinx_dma_remove(struct platform_device *pdev)
{
	struct xilinx_dma_device *xdev = platform_get_drvdata(pdev);
	int i;

	of_dma_controller_free(pdev->dev.of_node);

	dma_async_device_unregister(&xdev->common);

	for (i = 0; i < xdev->dma_config->max_channels; i++)
		if (xdev->chan[i])
			xilinx_dma_chan_remove(xdev->chan[i]);

	xdma_disable_allclks(xdev);

	return 0;
}

static struct platform_driver xilinx_vdma_driver = {
	.driver = {
		.name = "xilinx-vdma",
		.of_match_table = xilinx_dma_of_ids,
	},
	.probe = xilinx_dma_probe,
	.remove = xilinx_dma_remove,
};

module_platform_driver(xilinx_vdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx VDMA driver");
MODULE_LICENSE("GPL v2");
