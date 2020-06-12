// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 */
/*
 * QCOM BAM DMA engine driver
 *
 * QCOM BAM DMA blocks are distributed amongst a number of the on-chip
 * peripherals on the MSM 8x74.  The configuration of the channels are dependent
 * on the way they are hard wired to that specific peripheral.  The peripheral
 * device tree entries specify the configuration of each channel.
 *
 * The DMA controller requires the use of external memory for storage of the
 * hardware descriptors for each channel.  The descriptor FIFO is accessed as a
 * circular buffer and operations are managed according to the offset within the
 * FIFO.  After pipe/channel reset, all of the pipe registers and internal state
 * are back to defaults.
 *
 * During DMA operations, we write descriptors to the FIFO, being careful to
 * handle wrapping and then write the last FIFO offset to that channel's
 * P_EVNT_REG register to kick off the transaction.  The P_SW_OFSTS register
 * indicates the current FIFO offset that is being processed, so there is some
 * indication of where the hardware is currently working.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_dma.h>
#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/pm_runtime.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

struct bam_desc_hw {
	__le32 addr;		/* Buffer physical address */
	__le16 size;		/* Buffer size in bytes */
	__le16 flags;
};

#define BAM_DMA_AUTOSUSPEND_DELAY 100

#define DESC_FLAG_INT BIT(15)
#define DESC_FLAG_EOT BIT(14)
#define DESC_FLAG_EOB BIT(13)
#define DESC_FLAG_NWD BIT(12)
#define DESC_FLAG_CMD BIT(11)

struct bam_async_desc {
	struct virt_dma_desc vd;

	u32 num_desc;
	u32 xfer_len;

	/* transaction flags, EOT|EOB|NWD */
	u16 flags;

	struct bam_desc_hw *curr_desc;

	/* list node for the desc in the bam_chan list of descriptors */
	struct list_head desc_node;
	enum dma_transfer_direction dir;
	size_t length;
	struct bam_desc_hw desc[];
};

enum bam_reg {
	BAM_CTRL,
	BAM_REVISION,
	BAM_NUM_PIPES,
	BAM_DESC_CNT_TRSHLD,
	BAM_IRQ_SRCS,
	BAM_IRQ_SRCS_MSK,
	BAM_IRQ_SRCS_UNMASKED,
	BAM_IRQ_STTS,
	BAM_IRQ_CLR,
	BAM_IRQ_EN,
	BAM_CNFG_BITS,
	BAM_IRQ_SRCS_EE,
	BAM_IRQ_SRCS_MSK_EE,
	BAM_P_CTRL,
	BAM_P_RST,
	BAM_P_HALT,
	BAM_P_IRQ_STTS,
	BAM_P_IRQ_CLR,
	BAM_P_IRQ_EN,
	BAM_P_EVNT_DEST_ADDR,
	BAM_P_EVNT_REG,
	BAM_P_SW_OFSTS,
	BAM_P_DATA_FIFO_ADDR,
	BAM_P_DESC_FIFO_ADDR,
	BAM_P_EVNT_GEN_TRSHLD,
	BAM_P_FIFO_SIZES,
};

struct reg_offset_data {
	u32 base_offset;
	unsigned int pipe_mult, evnt_mult, ee_mult;
};

static const struct reg_offset_data bam_v1_3_reg_info[] = {
	[BAM_CTRL]		= { 0x0F80, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x0F84, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x0FBC, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x0F88, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x0F8C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x0F90, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x0FB0, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x0F94, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x0F98, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x0F9C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x0FFC, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x1800, 0x00, 0x00, 0x80 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x1804, 0x00, 0x00, 0x80 },
	[BAM_P_CTRL]		= { 0x0000, 0x80, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x0004, 0x80, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x0008, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x0010, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x0014, 0x80, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x0018, 0x80, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x102C, 0x00, 0x40, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x1018, 0x00, 0x40, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x1000, 0x00, 0x40, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x1024, 0x00, 0x40, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x101C, 0x00, 0x40, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x1028, 0x00, 0x40, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x1020, 0x00, 0x40, 0x00 },
};

static const struct reg_offset_data bam_v1_4_reg_info[] = {
	[BAM_CTRL]		= { 0x0000, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x0004, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x003C, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x0008, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x000C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x0010, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x0030, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x0014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x0018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x001C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x007C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x0800, 0x00, 0x00, 0x80 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x0804, 0x00, 0x00, 0x80 },
	[BAM_P_CTRL]		= { 0x1000, 0x1000, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x1004, 0x1000, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x1008, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x1010, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x1014, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x1018, 0x1000, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x182C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x1818, 0x00, 0x1000, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x1800, 0x00, 0x1000, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x1824, 0x00, 0x1000, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x181C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x1828, 0x00, 0x1000, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x1820, 0x00, 0x1000, 0x00 },
};

static const struct reg_offset_data bam_v1_7_reg_info[] = {
	[BAM_CTRL]		= { 0x00000, 0x00, 0x00, 0x00 },
	[BAM_REVISION]		= { 0x01000, 0x00, 0x00, 0x00 },
	[BAM_NUM_PIPES]		= { 0x01008, 0x00, 0x00, 0x00 },
	[BAM_DESC_CNT_TRSHLD]	= { 0x00008, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS]		= { 0x03010, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_MSK]	= { 0x03014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_UNMASKED]	= { 0x03018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_STTS]		= { 0x00014, 0x00, 0x00, 0x00 },
	[BAM_IRQ_CLR]		= { 0x00018, 0x00, 0x00, 0x00 },
	[BAM_IRQ_EN]		= { 0x0001C, 0x00, 0x00, 0x00 },
	[BAM_CNFG_BITS]		= { 0x0007C, 0x00, 0x00, 0x00 },
	[BAM_IRQ_SRCS_EE]	= { 0x03000, 0x00, 0x00, 0x1000 },
	[BAM_IRQ_SRCS_MSK_EE]	= { 0x03004, 0x00, 0x00, 0x1000 },
	[BAM_P_CTRL]		= { 0x13000, 0x1000, 0x00, 0x00 },
	[BAM_P_RST]		= { 0x13004, 0x1000, 0x00, 0x00 },
	[BAM_P_HALT]		= { 0x13008, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_STTS]	= { 0x13010, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_CLR]		= { 0x13014, 0x1000, 0x00, 0x00 },
	[BAM_P_IRQ_EN]		= { 0x13018, 0x1000, 0x00, 0x00 },
	[BAM_P_EVNT_DEST_ADDR]	= { 0x1382C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_REG]	= { 0x13818, 0x00, 0x1000, 0x00 },
	[BAM_P_SW_OFSTS]	= { 0x13800, 0x00, 0x1000, 0x00 },
	[BAM_P_DATA_FIFO_ADDR]	= { 0x13824, 0x00, 0x1000, 0x00 },
	[BAM_P_DESC_FIFO_ADDR]	= { 0x1381C, 0x00, 0x1000, 0x00 },
	[BAM_P_EVNT_GEN_TRSHLD]	= { 0x13828, 0x00, 0x1000, 0x00 },
	[BAM_P_FIFO_SIZES]	= { 0x13820, 0x00, 0x1000, 0x00 },
};

/* BAM CTRL */
#define BAM_SW_RST			BIT(0)
#define BAM_EN				BIT(1)
#define BAM_EN_ACCUM			BIT(4)
#define BAM_TESTBUS_SEL_SHIFT		5
#define BAM_TESTBUS_SEL_MASK		0x3F
#define BAM_DESC_CACHE_SEL_SHIFT	13
#define BAM_DESC_CACHE_SEL_MASK		0x3
#define BAM_CACHED_DESC_STORE		BIT(15)
#define IBC_DISABLE			BIT(16)

/* BAM REVISION */
#define REVISION_SHIFT		0
#define REVISION_MASK		0xFF
#define NUM_EES_SHIFT		8
#define NUM_EES_MASK		0xF
#define CE_BUFFER_SIZE		BIT(13)
#define AXI_ACTIVE		BIT(14)
#define USE_VMIDMT		BIT(15)
#define SECURED			BIT(16)
#define BAM_HAS_NO_BYPASS	BIT(17)
#define HIGH_FREQUENCY_BAM	BIT(18)
#define INACTIV_TMRS_EXST	BIT(19)
#define NUM_INACTIV_TMRS	BIT(20)
#define DESC_CACHE_DEPTH_SHIFT	21
#define DESC_CACHE_DEPTH_1	(0 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_2	(1 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_3	(2 << DESC_CACHE_DEPTH_SHIFT)
#define DESC_CACHE_DEPTH_4	(3 << DESC_CACHE_DEPTH_SHIFT)
#define CMD_DESC_EN		BIT(23)
#define INACTIV_TMR_BASE_SHIFT	24
#define INACTIV_TMR_BASE_MASK	0xFF

/* BAM NUM PIPES */
#define BAM_NUM_PIPES_SHIFT		0
#define BAM_NUM_PIPES_MASK		0xFF
#define PERIPH_NON_PIPE_GRP_SHIFT	16
#define PERIPH_NON_PIP_GRP_MASK		0xFF
#define BAM_NON_PIPE_GRP_SHIFT		24
#define BAM_NON_PIPE_GRP_MASK		0xFF

/* BAM CNFG BITS */
#define BAM_PIPE_CNFG		BIT(2)
#define BAM_FULL_PIPE		BIT(11)
#define BAM_NO_EXT_P_RST	BIT(12)
#define BAM_IBC_DISABLE		BIT(13)
#define BAM_SB_CLK_REQ		BIT(14)
#define BAM_PSM_CSW_REQ		BIT(15)
#define BAM_PSM_P_RES		BIT(16)
#define BAM_AU_P_RES		BIT(17)
#define BAM_SI_P_RES		BIT(18)
#define BAM_WB_P_RES		BIT(19)
#define BAM_WB_BLK_CSW		BIT(20)
#define BAM_WB_CSW_ACK_IDL	BIT(21)
#define BAM_WB_RETR_SVPNT	BIT(22)
#define BAM_WB_DSC_AVL_P_RST	BIT(23)
#define BAM_REG_P_EN		BIT(24)
#define BAM_PSM_P_HD_DATA	BIT(25)
#define BAM_AU_ACCUMED		BIT(26)
#define BAM_CMD_ENABLE		BIT(27)

#define BAM_CNFG_BITS_DEFAULT	(BAM_PIPE_CNFG |	\
				 BAM_NO_EXT_P_RST |	\
				 BAM_IBC_DISABLE |	\
				 BAM_SB_CLK_REQ |	\
				 BAM_PSM_CSW_REQ |	\
				 BAM_PSM_P_RES |	\
				 BAM_AU_P_RES |		\
				 BAM_SI_P_RES |		\
				 BAM_WB_P_RES |		\
				 BAM_WB_BLK_CSW |	\
				 BAM_WB_CSW_ACK_IDL |	\
				 BAM_WB_RETR_SVPNT |	\
				 BAM_WB_DSC_AVL_P_RST |	\
				 BAM_REG_P_EN |		\
				 BAM_PSM_P_HD_DATA |	\
				 BAM_AU_ACCUMED |	\
				 BAM_CMD_ENABLE)

/* PIPE CTRL */
#define P_EN			BIT(1)
#define P_DIRECTION		BIT(3)
#define P_SYS_STRM		BIT(4)
#define P_SYS_MODE		BIT(5)
#define P_AUTO_EOB		BIT(6)
#define P_AUTO_EOB_SEL_SHIFT	7
#define P_AUTO_EOB_SEL_512	(0 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_256	(1 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_128	(2 << P_AUTO_EOB_SEL_SHIFT)
#define P_AUTO_EOB_SEL_64	(3 << P_AUTO_EOB_SEL_SHIFT)
#define P_PREFETCH_LIMIT_SHIFT	9
#define P_PREFETCH_LIMIT_32	(0 << P_PREFETCH_LIMIT_SHIFT)
#define P_PREFETCH_LIMIT_16	(1 << P_PREFETCH_LIMIT_SHIFT)
#define P_PREFETCH_LIMIT_4	(2 << P_PREFETCH_LIMIT_SHIFT)
#define P_WRITE_NWD		BIT(11)
#define P_LOCK_GROUP_SHIFT	16
#define P_LOCK_GROUP_MASK	0x1F

/* BAM_DESC_CNT_TRSHLD */
#define CNT_TRSHLD		0xffff
#define DEFAULT_CNT_THRSHLD	0x4

/* BAM_IRQ_SRCS */
#define BAM_IRQ			BIT(31)
#define P_IRQ			0x7fffffff

/* BAM_IRQ_SRCS_MSK */
#define BAM_IRQ_MSK		BAM_IRQ
#define P_IRQ_MSK		P_IRQ

/* BAM_IRQ_STTS */
#define BAM_TIMER_IRQ		BIT(4)
#define BAM_EMPTY_IRQ		BIT(3)
#define BAM_ERROR_IRQ		BIT(2)
#define BAM_HRESP_ERR_IRQ	BIT(1)

/* BAM_IRQ_CLR */
#define BAM_TIMER_CLR		BIT(4)
#define BAM_EMPTY_CLR		BIT(3)
#define BAM_ERROR_CLR		BIT(2)
#define BAM_HRESP_ERR_CLR	BIT(1)

/* BAM_IRQ_EN */
#define BAM_TIMER_EN		BIT(4)
#define BAM_EMPTY_EN		BIT(3)
#define BAM_ERROR_EN		BIT(2)
#define BAM_HRESP_ERR_EN	BIT(1)

/* BAM_P_IRQ_EN */
#define P_PRCSD_DESC_EN		BIT(0)
#define P_TIMER_EN		BIT(1)
#define P_WAKE_EN		BIT(2)
#define P_OUT_OF_DESC_EN	BIT(3)
#define P_ERR_EN		BIT(4)
#define P_TRNSFR_END_EN		BIT(5)
#define P_DEFAULT_IRQS_EN	(P_PRCSD_DESC_EN | P_ERR_EN | P_TRNSFR_END_EN)

/* BAM_P_SW_OFSTS */
#define P_SW_OFSTS_MASK		0xffff

#define BAM_DESC_FIFO_SIZE	SZ_32K
#define MAX_DESCRIPTORS (BAM_DESC_FIFO_SIZE / sizeof(struct bam_desc_hw) - 1)
#define BAM_FIFO_SIZE	(SZ_32K - 8)
#define IS_BUSY(chan)	(CIRC_SPACE(bchan->tail, bchan->head,\
			 MAX_DESCRIPTORS + 1) == 0)

struct bam_chan {
	struct virt_dma_chan vc;

	struct bam_device *bdev;

	/* configuration from device tree */
	u32 id;

	/* runtime configuration */
	struct dma_slave_config slave;

	/* fifo storage */
	struct bam_desc_hw *fifo_virt;
	dma_addr_t fifo_phys;

	/* fifo markers */
	unsigned short head;		/* start of active descriptor entries */
	unsigned short tail;		/* end of active descriptor entries */

	unsigned int initialized;	/* is the channel hw initialized? */
	unsigned int paused;		/* is the channel paused? */
	unsigned int reconfigure;	/* new slave config? */
	/* list of descriptors currently processed */
	struct list_head desc_list;

	struct list_head node;
};

static inline struct bam_chan *to_bam_chan(struct dma_chan *common)
{
	return container_of(common, struct bam_chan, vc.chan);
}

struct bam_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct device_dma_parameters dma_parms;
	struct bam_chan *channels;
	u32 num_channels;
	u32 num_ees;

	/* execution environment ID, from DT */
	u32 ee;
	bool controlled_remotely;

	const struct reg_offset_data *layout;

	struct clk *bamclk;
	int irq;

	/* dma start transaction tasklet */
	struct tasklet_struct task;
};

/**
 * bam_addr - returns BAM register address
 * @bdev: bam device
 * @pipe: pipe instance (ignored when register doesn't have multiple instances)
 * @reg:  register enum
 */
static inline void __iomem *bam_addr(struct bam_device *bdev, u32 pipe,
		enum bam_reg reg)
{
	const struct reg_offset_data r = bdev->layout[reg];

	return bdev->regs + r.base_offset +
		r.pipe_mult * pipe +
		r.evnt_mult * pipe +
		r.ee_mult * bdev->ee;
}

/**
 * bam_reset_channel - Reset individual BAM DMA channel
 * @bchan: bam channel
 *
 * This function resets a specific BAM channel
 */
static void bam_reset_channel(struct bam_chan *bchan)
{
	struct bam_device *bdev = bchan->bdev;

	lockdep_assert_held(&bchan->vc.lock);

	/* reset channel */
	writel_relaxed(1, bam_addr(bdev, bchan->id, BAM_P_RST));
	writel_relaxed(0, bam_addr(bdev, bchan->id, BAM_P_RST));

	/* don't allow cpu to reorder BAM register accesses done after this */
	wmb();

	/* make sure hw is initialized when channel is used the first time  */
	bchan->initialized = 0;
}

/**
 * bam_chan_init_hw - Initialize channel hardware
 * @bchan: bam channel
 * @dir: DMA transfer direction
 *
 * This function resets and initializes the BAM channel
 */
static void bam_chan_init_hw(struct bam_chan *bchan,
	enum dma_transfer_direction dir)
{
	struct bam_device *bdev = bchan->bdev;
	u32 val;

	/* Reset the channel to clear internal state of the FIFO */
	bam_reset_channel(bchan);

	/*
	 * write out 8 byte aligned address.  We have enough space for this
	 * because we allocated 1 more descriptor (8 bytes) than we can use
	 */
	writel_relaxed(ALIGN(bchan->fifo_phys, sizeof(struct bam_desc_hw)),
			bam_addr(bdev, bchan->id, BAM_P_DESC_FIFO_ADDR));
	writel_relaxed(BAM_FIFO_SIZE,
			bam_addr(bdev, bchan->id, BAM_P_FIFO_SIZES));

	/* enable the per pipe interrupts, enable EOT, ERR, and INT irqs */
	writel_relaxed(P_DEFAULT_IRQS_EN,
			bam_addr(bdev, bchan->id, BAM_P_IRQ_EN));

	/* unmask the specific pipe and EE combo */
	val = readl_relaxed(bam_addr(bdev, 0, BAM_IRQ_SRCS_MSK_EE));
	val |= BIT(bchan->id);
	writel_relaxed(val, bam_addr(bdev, 0, BAM_IRQ_SRCS_MSK_EE));

	/* don't allow cpu to reorder the channel enable done below */
	wmb();

	/* set fixed direction and mode, then enable channel */
	val = P_EN | P_SYS_MODE;
	if (dir == DMA_DEV_TO_MEM)
		val |= P_DIRECTION;

	writel_relaxed(val, bam_addr(bdev, bchan->id, BAM_P_CTRL));

	bchan->initialized = 1;

	/* init FIFO pointers */
	bchan->head = 0;
	bchan->tail = 0;
}

/**
 * bam_alloc_chan - Allocate channel resources for DMA channel.
 * @chan: specified channel
 *
 * This function allocates the FIFO descriptor memory
 */
static int bam_alloc_chan(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_device *bdev = bchan->bdev;

	if (bchan->fifo_virt)
		return 0;

	/* allocate FIFO descriptor space, but only if necessary */
	bchan->fifo_virt = dma_alloc_wc(bdev->dev, BAM_DESC_FIFO_SIZE,
					&bchan->fifo_phys, GFP_KERNEL);

	if (!bchan->fifo_virt) {
		dev_err(bdev->dev, "Failed to allocate desc fifo\n");
		return -ENOMEM;
	}

	return 0;
}

static int bam_pm_runtime_get_sync(struct device *dev)
{
	if (pm_runtime_enabled(dev))
		return pm_runtime_get_sync(dev);

	return 0;
}

/**
 * bam_free_chan - Frees dma resources associated with specific channel
 * @chan: specified channel
 *
 * Free the allocated fifo descriptor memory and channel resources
 *
 */
static void bam_free_chan(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_device *bdev = bchan->bdev;
	u32 val;
	unsigned long flags;
	int ret;

	ret = bam_pm_runtime_get_sync(bdev->dev);
	if (ret < 0)
		return;

	vchan_free_chan_resources(to_virt_chan(chan));

	if (!list_empty(&bchan->desc_list)) {
		dev_err(bchan->bdev->dev, "Cannot free busy channel\n");
		goto err;
	}

	spin_lock_irqsave(&bchan->vc.lock, flags);
	bam_reset_channel(bchan);
	spin_unlock_irqrestore(&bchan->vc.lock, flags);

	dma_free_wc(bdev->dev, BAM_DESC_FIFO_SIZE, bchan->fifo_virt,
		    bchan->fifo_phys);
	bchan->fifo_virt = NULL;

	/* mask irq for pipe/channel */
	val = readl_relaxed(bam_addr(bdev, 0, BAM_IRQ_SRCS_MSK_EE));
	val &= ~BIT(bchan->id);
	writel_relaxed(val, bam_addr(bdev, 0, BAM_IRQ_SRCS_MSK_EE));

	/* disable irq */
	writel_relaxed(0, bam_addr(bdev, bchan->id, BAM_P_IRQ_EN));

err:
	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

/**
 * bam_slave_config - set slave configuration for channel
 * @chan: dma channel
 * @cfg: slave configuration
 *
 * Sets slave configuration for channel
 *
 */
static int bam_slave_config(struct dma_chan *chan,
			    struct dma_slave_config *cfg)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	unsigned long flag;

	spin_lock_irqsave(&bchan->vc.lock, flag);
	memcpy(&bchan->slave, cfg, sizeof(*cfg));
	bchan->reconfigure = 1;
	spin_unlock_irqrestore(&bchan->vc.lock, flag);

	return 0;
}

/**
 * bam_prep_slave_sg - Prep slave sg transaction
 *
 * @chan: dma channel
 * @sgl: scatter gather list
 * @sg_len: length of sg
 * @direction: DMA transfer direction
 * @flags: DMA flags
 * @context: transfer context (unused)
 */
static struct dma_async_tx_descriptor *bam_prep_slave_sg(struct dma_chan *chan,
	struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_device *bdev = bchan->bdev;
	struct bam_async_desc *async_desc;
	struct scatterlist *sg;
	u32 i;
	struct bam_desc_hw *desc;
	unsigned int num_alloc = 0;


	if (!is_slave_direction(direction)) {
		dev_err(bdev->dev, "invalid dma direction\n");
		return NULL;
	}

	/* calculate number of required entries */
	for_each_sg(sgl, sg, sg_len, i)
		num_alloc += DIV_ROUND_UP(sg_dma_len(sg), BAM_FIFO_SIZE);

	/* allocate enough room to accomodate the number of entries */
	async_desc = kzalloc(struct_size(async_desc, desc, num_alloc),
			     GFP_NOWAIT);

	if (!async_desc)
		goto err_out;

	if (flags & DMA_PREP_FENCE)
		async_desc->flags |= DESC_FLAG_NWD;

	if (flags & DMA_PREP_INTERRUPT)
		async_desc->flags |= DESC_FLAG_EOT;

	async_desc->num_desc = num_alloc;
	async_desc->curr_desc = async_desc->desc;
	async_desc->dir = direction;

	/* fill in temporary descriptors */
	desc = async_desc->desc;
	for_each_sg(sgl, sg, sg_len, i) {
		unsigned int remainder = sg_dma_len(sg);
		unsigned int curr_offset = 0;

		do {
			if (flags & DMA_PREP_CMD)
				desc->flags |= cpu_to_le16(DESC_FLAG_CMD);

			desc->addr = cpu_to_le32(sg_dma_address(sg) +
						 curr_offset);

			if (remainder > BAM_FIFO_SIZE) {
				desc->size = cpu_to_le16(BAM_FIFO_SIZE);
				remainder -= BAM_FIFO_SIZE;
				curr_offset += BAM_FIFO_SIZE;
			} else {
				desc->size = cpu_to_le16(remainder);
				remainder = 0;
			}

			async_desc->length += le16_to_cpu(desc->size);
			desc++;
		} while (remainder > 0);
	}

	return vchan_tx_prep(&bchan->vc, &async_desc->vd, flags);

err_out:
	kfree(async_desc);
	return NULL;
}

/**
 * bam_dma_terminate_all - terminate all transactions on a channel
 * @chan: bam dma channel
 *
 * Dequeues and frees all transactions
 * No callbacks are done
 *
 */
static int bam_dma_terminate_all(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_async_desc *async_desc, *tmp;
	unsigned long flag;
	LIST_HEAD(head);

	/* remove all transactions, including active transaction */
	spin_lock_irqsave(&bchan->vc.lock, flag);
	/*
	 * If we have transactions queued, then some might be committed to the
	 * hardware in the desc fifo.  The only way to reset the desc fifo is
	 * to do a hardware reset (either by pipe or the entire block).
	 * bam_chan_init_hw() will trigger a pipe reset, and also reinit the
	 * pipe.  If the pipe is left disabled (default state after pipe reset)
	 * and is accessed by a connected hardware engine, a fatal error in
	 * the BAM will occur.  There is a small window where this could happen
	 * with bam_chan_init_hw(), but it is assumed that the caller has
	 * stopped activity on any attached hardware engine.  Make sure to do
	 * this first so that the BAM hardware doesn't cause memory corruption
	 * by accessing freed resources.
	 */
	if (!list_empty(&bchan->desc_list)) {
		async_desc = list_first_entry(&bchan->desc_list,
					      struct bam_async_desc, desc_node);
		bam_chan_init_hw(bchan, async_desc->dir);
	}

	list_for_each_entry_safe(async_desc, tmp,
				 &bchan->desc_list, desc_node) {
		list_add(&async_desc->vd.node, &bchan->vc.desc_issued);
		list_del(&async_desc->desc_node);
	}

	vchan_get_all_descriptors(&bchan->vc, &head);
	spin_unlock_irqrestore(&bchan->vc.lock, flag);

	vchan_dma_desc_free_list(&bchan->vc, &head);

	return 0;
}

/**
 * bam_pause - Pause DMA channel
 * @chan: dma channel
 *
 */
static int bam_pause(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_device *bdev = bchan->bdev;
	unsigned long flag;
	int ret;

	ret = bam_pm_runtime_get_sync(bdev->dev);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&bchan->vc.lock, flag);
	writel_relaxed(1, bam_addr(bdev, bchan->id, BAM_P_HALT));
	bchan->paused = 1;
	spin_unlock_irqrestore(&bchan->vc.lock, flag);
	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);

	return 0;
}

/**
 * bam_resume - Resume DMA channel operations
 * @chan: dma channel
 *
 */
static int bam_resume(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_device *bdev = bchan->bdev;
	unsigned long flag;
	int ret;

	ret = bam_pm_runtime_get_sync(bdev->dev);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&bchan->vc.lock, flag);
	writel_relaxed(0, bam_addr(bdev, bchan->id, BAM_P_HALT));
	bchan->paused = 0;
	spin_unlock_irqrestore(&bchan->vc.lock, flag);
	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);

	return 0;
}

/**
 * process_channel_irqs - processes the channel interrupts
 * @bdev: bam controller
 *
 * This function processes the channel interrupts
 *
 */
static u32 process_channel_irqs(struct bam_device *bdev)
{
	u32 i, srcs, pipe_stts, offset, avail;
	unsigned long flags;
	struct bam_async_desc *async_desc, *tmp;

	srcs = readl_relaxed(bam_addr(bdev, 0, BAM_IRQ_SRCS_EE));

	/* return early if no pipe/channel interrupts are present */
	if (!(srcs & P_IRQ))
		return srcs;

	for (i = 0; i < bdev->num_channels; i++) {
		struct bam_chan *bchan = &bdev->channels[i];

		if (!(srcs & BIT(i)))
			continue;

		/* clear pipe irq */
		pipe_stts = readl_relaxed(bam_addr(bdev, i, BAM_P_IRQ_STTS));

		writel_relaxed(pipe_stts, bam_addr(bdev, i, BAM_P_IRQ_CLR));

		spin_lock_irqsave(&bchan->vc.lock, flags);

		offset = readl_relaxed(bam_addr(bdev, i, BAM_P_SW_OFSTS)) &
				       P_SW_OFSTS_MASK;
		offset /= sizeof(struct bam_desc_hw);

		/* Number of bytes available to read */
		avail = CIRC_CNT(offset, bchan->head, MAX_DESCRIPTORS + 1);

		if (offset < bchan->head)
			avail--;

		list_for_each_entry_safe(async_desc, tmp,
					 &bchan->desc_list, desc_node) {
			/* Not enough data to read */
			if (avail < async_desc->xfer_len)
				break;

			/* manage FIFO */
			bchan->head += async_desc->xfer_len;
			bchan->head %= MAX_DESCRIPTORS;

			async_desc->num_desc -= async_desc->xfer_len;
			async_desc->curr_desc += async_desc->xfer_len;
			avail -= async_desc->xfer_len;

			/*
			 * if complete, process cookie. Otherwise
			 * push back to front of desc_issued so that
			 * it gets restarted by the tasklet
			 */
			if (!async_desc->num_desc) {
				vchan_cookie_complete(&async_desc->vd);
			} else {
				list_add(&async_desc->vd.node,
					 &bchan->vc.desc_issued);
			}
			list_del(&async_desc->desc_node);
		}

		spin_unlock_irqrestore(&bchan->vc.lock, flags);
	}

	return srcs;
}

/**
 * bam_dma_irq - irq handler for bam controller
 * @irq: IRQ of interrupt
 * @data: callback data
 *
 * IRQ handler for the bam controller
 */
static irqreturn_t bam_dma_irq(int irq, void *data)
{
	struct bam_device *bdev = data;
	u32 clr_mask = 0, srcs = 0;
	int ret;

	srcs |= process_channel_irqs(bdev);

	/* kick off tasklet to start next dma transfer */
	if (srcs & P_IRQ)
		tasklet_schedule(&bdev->task);

	ret = bam_pm_runtime_get_sync(bdev->dev);
	if (ret < 0)
		return ret;

	if (srcs & BAM_IRQ) {
		clr_mask = readl_relaxed(bam_addr(bdev, 0, BAM_IRQ_STTS));

		/*
		 * don't allow reorder of the various accesses to the BAM
		 * registers
		 */
		mb();

		writel_relaxed(clr_mask, bam_addr(bdev, 0, BAM_IRQ_CLR));
	}

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);

	return IRQ_HANDLED;
}

/**
 * bam_tx_status - returns status of transaction
 * @chan: dma channel
 * @cookie: transaction cookie
 * @txstate: DMA transaction state
 *
 * Return status of dma transaction
 */
static enum dma_status bam_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	struct bam_async_desc *async_desc;
	struct virt_dma_desc *vd;
	int ret;
	size_t residue = 0;
	unsigned int i;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (!txstate)
		return bchan->paused ? DMA_PAUSED : ret;

	spin_lock_irqsave(&bchan->vc.lock, flags);
	vd = vchan_find_desc(&bchan->vc, cookie);
	if (vd) {
		residue = container_of(vd, struct bam_async_desc, vd)->length;
	} else {
		list_for_each_entry(async_desc, &bchan->desc_list, desc_node) {
			if (async_desc->vd.tx.cookie != cookie)
				continue;

			for (i = 0; i < async_desc->num_desc; i++)
				residue += le16_to_cpu(
						async_desc->curr_desc[i].size);
		}
	}

	spin_unlock_irqrestore(&bchan->vc.lock, flags);

	dma_set_residue(txstate, residue);

	if (ret == DMA_IN_PROGRESS && bchan->paused)
		ret = DMA_PAUSED;

	return ret;
}

/**
 * bam_apply_new_config
 * @bchan: bam dma channel
 * @dir: DMA direction
 */
static void bam_apply_new_config(struct bam_chan *bchan,
	enum dma_transfer_direction dir)
{
	struct bam_device *bdev = bchan->bdev;
	u32 maxburst;

	if (!bdev->controlled_remotely) {
		if (dir == DMA_DEV_TO_MEM)
			maxburst = bchan->slave.src_maxburst;
		else
			maxburst = bchan->slave.dst_maxburst;

		writel_relaxed(maxburst,
			       bam_addr(bdev, 0, BAM_DESC_CNT_TRSHLD));
	}

	bchan->reconfigure = 0;
}

/**
 * bam_start_dma - start next transaction
 * @bchan: bam dma channel
 */
static void bam_start_dma(struct bam_chan *bchan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&bchan->vc);
	struct bam_device *bdev = bchan->bdev;
	struct bam_async_desc *async_desc = NULL;
	struct bam_desc_hw *desc;
	struct bam_desc_hw *fifo = PTR_ALIGN(bchan->fifo_virt,
					sizeof(struct bam_desc_hw));
	int ret;
	unsigned int avail;
	struct dmaengine_desc_callback cb;

	lockdep_assert_held(&bchan->vc.lock);

	if (!vd)
		return;

	ret = bam_pm_runtime_get_sync(bdev->dev);
	if (ret < 0)
		return;

	while (vd && !IS_BUSY(bchan)) {
		list_del(&vd->node);

		async_desc = container_of(vd, struct bam_async_desc, vd);

		/* on first use, initialize the channel hardware */
		if (!bchan->initialized)
			bam_chan_init_hw(bchan, async_desc->dir);

		/* apply new slave config changes, if necessary */
		if (bchan->reconfigure)
			bam_apply_new_config(bchan, async_desc->dir);

		desc = async_desc->curr_desc;
		avail = CIRC_SPACE(bchan->tail, bchan->head,
				   MAX_DESCRIPTORS + 1);

		if (async_desc->num_desc > avail)
			async_desc->xfer_len = avail;
		else
			async_desc->xfer_len = async_desc->num_desc;

		/* set any special flags on the last descriptor */
		if (async_desc->num_desc == async_desc->xfer_len)
			desc[async_desc->xfer_len - 1].flags |=
						cpu_to_le16(async_desc->flags);

		vd = vchan_next_desc(&bchan->vc);

		dmaengine_desc_get_callback(&async_desc->vd.tx, &cb);

		/*
		 * An interrupt is generated at this desc, if
		 *  - FIFO is FULL.
		 *  - No more descriptors to add.
		 *  - If a callback completion was requested for this DESC,
		 *     In this case, BAM will deliver the completion callback
		 *     for this desc and continue processing the next desc.
		 */
		if (((avail <= async_desc->xfer_len) || !vd ||
		     dmaengine_desc_callback_valid(&cb)) &&
		    !(async_desc->flags & DESC_FLAG_EOT))
			desc[async_desc->xfer_len - 1].flags |=
				cpu_to_le16(DESC_FLAG_INT);

		if (bchan->tail + async_desc->xfer_len > MAX_DESCRIPTORS) {
			u32 partial = MAX_DESCRIPTORS - bchan->tail;

			memcpy(&fifo[bchan->tail], desc,
			       partial * sizeof(struct bam_desc_hw));
			memcpy(fifo, &desc[partial],
			       (async_desc->xfer_len - partial) *
				sizeof(struct bam_desc_hw));
		} else {
			memcpy(&fifo[bchan->tail], desc,
			       async_desc->xfer_len *
			       sizeof(struct bam_desc_hw));
		}

		bchan->tail += async_desc->xfer_len;
		bchan->tail %= MAX_DESCRIPTORS;
		list_add_tail(&async_desc->desc_node, &bchan->desc_list);
	}

	/* ensure descriptor writes and dma start not reordered */
	wmb();
	writel_relaxed(bchan->tail * sizeof(struct bam_desc_hw),
			bam_addr(bdev, bchan->id, BAM_P_EVNT_REG));

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

/**
 * dma_tasklet - DMA IRQ tasklet
 * @data: tasklet argument (bam controller structure)
 *
 * Sets up next DMA operation and then processes all completed transactions
 */
static void dma_tasklet(unsigned long data)
{
	struct bam_device *bdev = (struct bam_device *)data;
	struct bam_chan *bchan;
	unsigned long flags;
	unsigned int i;

	/* go through the channels and kick off transactions */
	for (i = 0; i < bdev->num_channels; i++) {
		bchan = &bdev->channels[i];
		spin_lock_irqsave(&bchan->vc.lock, flags);

		if (!list_empty(&bchan->vc.desc_issued) && !IS_BUSY(bchan))
			bam_start_dma(bchan);
		spin_unlock_irqrestore(&bchan->vc.lock, flags);
	}

}

/**
 * bam_issue_pending - starts pending transactions
 * @chan: dma channel
 *
 * Calls tasklet directly which in turn starts any pending transactions
 */
static void bam_issue_pending(struct dma_chan *chan)
{
	struct bam_chan *bchan = to_bam_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&bchan->vc.lock, flags);

	/* if work pending and idle, start a transaction */
	if (vchan_issue_pending(&bchan->vc) && !IS_BUSY(bchan))
		bam_start_dma(bchan);

	spin_unlock_irqrestore(&bchan->vc.lock, flags);
}

/**
 * bam_dma_free_desc - free descriptor memory
 * @vd: virtual descriptor
 *
 */
static void bam_dma_free_desc(struct virt_dma_desc *vd)
{
	struct bam_async_desc *async_desc = container_of(vd,
			struct bam_async_desc, vd);

	kfree(async_desc);
}

static struct dma_chan *bam_dma_xlate(struct of_phandle_args *dma_spec,
		struct of_dma *of)
{
	struct bam_device *bdev = container_of(of->of_dma_data,
					struct bam_device, common);
	unsigned int request;

	if (dma_spec->args_count != 1)
		return NULL;

	request = dma_spec->args[0];
	if (request >= bdev->num_channels)
		return NULL;

	return dma_get_slave_channel(&(bdev->channels[request].vc.chan));
}

/**
 * bam_init
 * @bdev: bam device
 *
 * Initialization helper for global bam registers
 */
static int bam_init(struct bam_device *bdev)
{
	u32 val;

	/* read revision and configuration information */
	if (!bdev->num_ees) {
		val = readl_relaxed(bam_addr(bdev, 0, BAM_REVISION));
		bdev->num_ees = (val >> NUM_EES_SHIFT) & NUM_EES_MASK;
	}

	/* check that configured EE is within range */
	if (bdev->ee >= bdev->num_ees)
		return -EINVAL;

	if (!bdev->num_channels) {
		val = readl_relaxed(bam_addr(bdev, 0, BAM_NUM_PIPES));
		bdev->num_channels = val & BAM_NUM_PIPES_MASK;
	}

	if (bdev->controlled_remotely)
		return 0;

	/* s/w reset bam */
	/* after reset all pipes are disabled and idle */
	val = readl_relaxed(bam_addr(bdev, 0, BAM_CTRL));
	val |= BAM_SW_RST;
	writel_relaxed(val, bam_addr(bdev, 0, BAM_CTRL));
	val &= ~BAM_SW_RST;
	writel_relaxed(val, bam_addr(bdev, 0, BAM_CTRL));

	/* make sure previous stores are visible before enabling BAM */
	wmb();

	/* enable bam */
	val |= BAM_EN;
	writel_relaxed(val, bam_addr(bdev, 0, BAM_CTRL));

	/* set descriptor threshhold, start with 4 bytes */
	writel_relaxed(DEFAULT_CNT_THRSHLD,
			bam_addr(bdev, 0, BAM_DESC_CNT_TRSHLD));

	/* Enable default set of h/w workarounds, ie all except BAM_FULL_PIPE */
	writel_relaxed(BAM_CNFG_BITS_DEFAULT, bam_addr(bdev, 0, BAM_CNFG_BITS));

	/* enable irqs for errors */
	writel_relaxed(BAM_ERROR_EN | BAM_HRESP_ERR_EN,
			bam_addr(bdev, 0, BAM_IRQ_EN));

	/* unmask global bam interrupt */
	writel_relaxed(BAM_IRQ_MSK, bam_addr(bdev, 0, BAM_IRQ_SRCS_MSK_EE));

	return 0;
}

static void bam_channel_init(struct bam_device *bdev, struct bam_chan *bchan,
	u32 index)
{
	bchan->id = index;
	bchan->bdev = bdev;

	vchan_init(&bchan->vc, &bdev->common);
	bchan->vc.desc_free = bam_dma_free_desc;
	INIT_LIST_HEAD(&bchan->desc_list);
}

static const struct of_device_id bam_of_match[] = {
	{ .compatible = "qcom,bam-v1.3.0", .data = &bam_v1_3_reg_info },
	{ .compatible = "qcom,bam-v1.4.0", .data = &bam_v1_4_reg_info },
	{ .compatible = "qcom,bam-v1.7.0", .data = &bam_v1_7_reg_info },
	{}
};

MODULE_DEVICE_TABLE(of, bam_of_match);

static int bam_dma_probe(struct platform_device *pdev)
{
	struct bam_device *bdev;
	const struct of_device_id *match;
	struct resource *iores;
	int ret, i;

	bdev = devm_kzalloc(&pdev->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->dev = &pdev->dev;

	match = of_match_node(bam_of_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "Unsupported BAM module\n");
		return -ENODEV;
	}

	bdev->layout = match->data;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bdev->regs = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(bdev->regs))
		return PTR_ERR(bdev->regs);

	bdev->irq = platform_get_irq(pdev, 0);
	if (bdev->irq < 0)
		return bdev->irq;

	ret = of_property_read_u32(pdev->dev.of_node, "qcom,ee", &bdev->ee);
	if (ret) {
		dev_err(bdev->dev, "Execution environment unspecified\n");
		return ret;
	}

	bdev->controlled_remotely = of_property_read_bool(pdev->dev.of_node,
						"qcom,controlled-remotely");

	if (bdev->controlled_remotely) {
		ret = of_property_read_u32(pdev->dev.of_node, "num-channels",
					   &bdev->num_channels);
		if (ret)
			dev_err(bdev->dev, "num-channels unspecified in dt\n");

		ret = of_property_read_u32(pdev->dev.of_node, "qcom,num-ees",
					   &bdev->num_ees);
		if (ret)
			dev_err(bdev->dev, "num-ees unspecified in dt\n");
	}

	bdev->bamclk = devm_clk_get(bdev->dev, "bam_clk");
	if (IS_ERR(bdev->bamclk)) {
		if (!bdev->controlled_remotely)
			return PTR_ERR(bdev->bamclk);

		bdev->bamclk = NULL;
	}

	ret = clk_prepare_enable(bdev->bamclk);
	if (ret) {
		dev_err(bdev->dev, "failed to prepare/enable clock\n");
		return ret;
	}

	ret = bam_init(bdev);
	if (ret)
		goto err_disable_clk;

	tasklet_init(&bdev->task, dma_tasklet, (unsigned long)bdev);

	bdev->channels = devm_kcalloc(bdev->dev, bdev->num_channels,
				sizeof(*bdev->channels), GFP_KERNEL);

	if (!bdev->channels) {
		ret = -ENOMEM;
		goto err_tasklet_kill;
	}

	/* allocate and initialize channels */
	INIT_LIST_HEAD(&bdev->common.channels);

	for (i = 0; i < bdev->num_channels; i++)
		bam_channel_init(bdev, &bdev->channels[i], i);

	ret = devm_request_irq(bdev->dev, bdev->irq, bam_dma_irq,
			IRQF_TRIGGER_HIGH, "bam_dma", bdev);
	if (ret)
		goto err_bam_channel_exit;

	/* set max dma segment size */
	bdev->common.dev = bdev->dev;
	bdev->common.dev->dma_parms = &bdev->dma_parms;
	ret = dma_set_max_seg_size(bdev->common.dev, BAM_FIFO_SIZE);
	if (ret) {
		dev_err(bdev->dev, "cannot set maximum segment size\n");
		goto err_bam_channel_exit;
	}

	platform_set_drvdata(pdev, bdev);

	/* set capabilities */
	dma_cap_zero(bdev->common.cap_mask);
	dma_cap_set(DMA_SLAVE, bdev->common.cap_mask);

	/* initialize dmaengine apis */
	bdev->common.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	bdev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	bdev->common.src_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	bdev->common.dst_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	bdev->common.device_alloc_chan_resources = bam_alloc_chan;
	bdev->common.device_free_chan_resources = bam_free_chan;
	bdev->common.device_prep_slave_sg = bam_prep_slave_sg;
	bdev->common.device_config = bam_slave_config;
	bdev->common.device_pause = bam_pause;
	bdev->common.device_resume = bam_resume;
	bdev->common.device_terminate_all = bam_dma_terminate_all;
	bdev->common.device_issue_pending = bam_issue_pending;
	bdev->common.device_tx_status = bam_tx_status;
	bdev->common.dev = bdev->dev;

	ret = dma_async_device_register(&bdev->common);
	if (ret) {
		dev_err(bdev->dev, "failed to register dma async device\n");
		goto err_bam_channel_exit;
	}

	ret = of_dma_controller_register(pdev->dev.of_node, bam_dma_xlate,
					&bdev->common);
	if (ret)
		goto err_unregister_dma;

	if (bdev->controlled_remotely) {
		pm_runtime_disable(&pdev->dev);
		return 0;
	}

	pm_runtime_irq_safe(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, BAM_DMA_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err_unregister_dma:
	dma_async_device_unregister(&bdev->common);
err_bam_channel_exit:
	for (i = 0; i < bdev->num_channels; i++)
		tasklet_kill(&bdev->channels[i].vc.task);
err_tasklet_kill:
	tasklet_kill(&bdev->task);
err_disable_clk:
	clk_disable_unprepare(bdev->bamclk);

	return ret;
}

static int bam_dma_remove(struct platform_device *pdev)
{
	struct bam_device *bdev = platform_get_drvdata(pdev);
	u32 i;

	pm_runtime_force_suspend(&pdev->dev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&bdev->common);

	/* mask all interrupts for this execution environment */
	writel_relaxed(0, bam_addr(bdev, 0,  BAM_IRQ_SRCS_MSK_EE));

	devm_free_irq(bdev->dev, bdev->irq, bdev);

	for (i = 0; i < bdev->num_channels; i++) {
		bam_dma_terminate_all(&bdev->channels[i].vc.chan);
		tasklet_kill(&bdev->channels[i].vc.task);

		if (!bdev->channels[i].fifo_virt)
			continue;

		dma_free_wc(bdev->dev, BAM_DESC_FIFO_SIZE,
			    bdev->channels[i].fifo_virt,
			    bdev->channels[i].fifo_phys);
	}

	tasklet_kill(&bdev->task);

	clk_disable_unprepare(bdev->bamclk);

	return 0;
}

static int __maybe_unused bam_dma_runtime_suspend(struct device *dev)
{
	struct bam_device *bdev = dev_get_drvdata(dev);

	clk_disable(bdev->bamclk);

	return 0;
}

static int __maybe_unused bam_dma_runtime_resume(struct device *dev)
{
	struct bam_device *bdev = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(bdev->bamclk);
	if (ret < 0) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused bam_dma_suspend(struct device *dev)
{
	struct bam_device *bdev = dev_get_drvdata(dev);

	if (!bdev->controlled_remotely)
		pm_runtime_force_suspend(dev);

	clk_unprepare(bdev->bamclk);

	return 0;
}

static int __maybe_unused bam_dma_resume(struct device *dev)
{
	struct bam_device *bdev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare(bdev->bamclk);
	if (ret)
		return ret;

	if (!bdev->controlled_remotely)
		pm_runtime_force_resume(dev);

	return 0;
}

static const struct dev_pm_ops bam_dma_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(bam_dma_suspend, bam_dma_resume)
	SET_RUNTIME_PM_OPS(bam_dma_runtime_suspend, bam_dma_runtime_resume,
				NULL)
};

static struct platform_driver bam_dma_driver = {
	.probe = bam_dma_probe,
	.remove = bam_dma_remove,
	.driver = {
		.name = "bam-dma-engine",
		.pm = &bam_dma_pm_ops,
		.of_match_table = bam_of_match,
	},
};

module_platform_driver(bam_dma_driver);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("QCOM BAM DMA engine driver");
MODULE_LICENSE("GPL v2");
