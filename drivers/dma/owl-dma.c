// SPDX-License-Identifier: GPL-2.0+
//
// Actions Semi Owl SoCs DMA driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/slab.h>
#include "virt-dma.h"

#define OWL_DMA_FRAME_MAX_LENGTH		0xfffff

/* Global DMA Controller Registers */
#define OWL_DMA_IRQ_PD0				0x00
#define OWL_DMA_IRQ_PD1				0x04
#define OWL_DMA_IRQ_PD2				0x08
#define OWL_DMA_IRQ_PD3				0x0C
#define OWL_DMA_IRQ_EN0				0x10
#define OWL_DMA_IRQ_EN1				0x14
#define OWL_DMA_IRQ_EN2				0x18
#define OWL_DMA_IRQ_EN3				0x1C
#define OWL_DMA_SECURE_ACCESS_CTL		0x20
#define OWL_DMA_NIC_QOS				0x24
#define OWL_DMA_DBGSEL				0x28
#define OWL_DMA_IDLE_STAT			0x2C

/* Channel Registers */
#define OWL_DMA_CHAN_BASE(i)			(0x100 + (i) * 0x100)
#define OWL_DMAX_MODE				0x00
#define OWL_DMAX_SOURCE				0x04
#define OWL_DMAX_DESTINATION			0x08
#define OWL_DMAX_FRAME_LEN			0x0C
#define OWL_DMAX_FRAME_CNT			0x10
#define OWL_DMAX_REMAIN_FRAME_CNT		0x14
#define OWL_DMAX_REMAIN_CNT			0x18
#define OWL_DMAX_SOURCE_STRIDE			0x1C
#define OWL_DMAX_DESTINATION_STRIDE		0x20
#define OWL_DMAX_START				0x24
#define OWL_DMAX_PAUSE				0x28
#define OWL_DMAX_CHAINED_CTL			0x2C
#define OWL_DMAX_CONSTANT			0x30
#define OWL_DMAX_LINKLIST_CTL			0x34
#define OWL_DMAX_NEXT_DESCRIPTOR		0x38
#define OWL_DMAX_CURRENT_DESCRIPTOR_NUM		0x3C
#define OWL_DMAX_INT_CTL			0x40
#define OWL_DMAX_INT_STATUS			0x44
#define OWL_DMAX_CURRENT_SOURCE_POINTER		0x48
#define OWL_DMAX_CURRENT_DESTINATION_POINTER	0x4C

/* OWL_DMAX_MODE Bits */
#define OWL_DMA_MODE_TS(x)			(((x) & GENMASK(5, 0)) << 0)
#define OWL_DMA_MODE_ST(x)			(((x) & GENMASK(1, 0)) << 8)
#define	OWL_DMA_MODE_ST_DEV			OWL_DMA_MODE_ST(0)
#define	OWL_DMA_MODE_ST_DCU			OWL_DMA_MODE_ST(2)
#define	OWL_DMA_MODE_ST_SRAM			OWL_DMA_MODE_ST(3)
#define OWL_DMA_MODE_DT(x)			(((x) & GENMASK(1, 0)) << 10)
#define	OWL_DMA_MODE_DT_DEV			OWL_DMA_MODE_DT(0)
#define	OWL_DMA_MODE_DT_DCU			OWL_DMA_MODE_DT(2)
#define	OWL_DMA_MODE_DT_SRAM			OWL_DMA_MODE_DT(3)
#define OWL_DMA_MODE_SAM(x)			(((x) & GENMASK(1, 0)) << 16)
#define	OWL_DMA_MODE_SAM_CONST			OWL_DMA_MODE_SAM(0)
#define	OWL_DMA_MODE_SAM_INC			OWL_DMA_MODE_SAM(1)
#define	OWL_DMA_MODE_SAM_STRIDE			OWL_DMA_MODE_SAM(2)
#define OWL_DMA_MODE_DAM(x)			(((x) & GENMASK(1, 0)) << 18)
#define	OWL_DMA_MODE_DAM_CONST			OWL_DMA_MODE_DAM(0)
#define	OWL_DMA_MODE_DAM_INC			OWL_DMA_MODE_DAM(1)
#define	OWL_DMA_MODE_DAM_STRIDE			OWL_DMA_MODE_DAM(2)
#define OWL_DMA_MODE_PW(x)			(((x) & GENMASK(2, 0)) << 20)
#define OWL_DMA_MODE_CB				BIT(23)
#define OWL_DMA_MODE_NDDBW(x)			(((x) & 0x1) << 28)
#define	OWL_DMA_MODE_NDDBW_32BIT		OWL_DMA_MODE_NDDBW(0)
#define	OWL_DMA_MODE_NDDBW_8BIT			OWL_DMA_MODE_NDDBW(1)
#define OWL_DMA_MODE_CFE			BIT(29)
#define OWL_DMA_MODE_LME			BIT(30)
#define OWL_DMA_MODE_CME			BIT(31)

/* OWL_DMAX_LINKLIST_CTL Bits */
#define OWL_DMA_LLC_SAV(x)			(((x) & GENMASK(1, 0)) << 8)
#define	OWL_DMA_LLC_SAV_INC			OWL_DMA_LLC_SAV(0)
#define	OWL_DMA_LLC_SAV_LOAD_NEXT		OWL_DMA_LLC_SAV(1)
#define	OWL_DMA_LLC_SAV_LOAD_PREV		OWL_DMA_LLC_SAV(2)
#define OWL_DMA_LLC_DAV(x)			(((x) & GENMASK(1, 0)) << 10)
#define	OWL_DMA_LLC_DAV_INC			OWL_DMA_LLC_DAV(0)
#define	OWL_DMA_LLC_DAV_LOAD_NEXT		OWL_DMA_LLC_DAV(1)
#define	OWL_DMA_LLC_DAV_LOAD_PREV		OWL_DMA_LLC_DAV(2)
#define OWL_DMA_LLC_SUSPEND			BIT(16)

/* OWL_DMAX_INT_CTL Bits */
#define OWL_DMA_INTCTL_BLOCK			BIT(0)
#define OWL_DMA_INTCTL_SUPER_BLOCK		BIT(1)
#define OWL_DMA_INTCTL_FRAME			BIT(2)
#define OWL_DMA_INTCTL_HALF_FRAME		BIT(3)
#define OWL_DMA_INTCTL_LAST_FRAME		BIT(4)

/* OWL_DMAX_INT_STATUS Bits */
#define OWL_DMA_INTSTAT_BLOCK			BIT(0)
#define OWL_DMA_INTSTAT_SUPER_BLOCK		BIT(1)
#define OWL_DMA_INTSTAT_FRAME			BIT(2)
#define OWL_DMA_INTSTAT_HALF_FRAME		BIT(3)
#define OWL_DMA_INTSTAT_LAST_FRAME		BIT(4)

/* Pack shift and newshift in a single word */
#define BIT_FIELD(val, width, shift, newshift)	\
		((((val) >> (shift)) & ((BIT(width)) - 1)) << (newshift))

/* Frame count value is fixed as 1 */
#define FCNT_VAL				0x1

/**
 * enum owl_dmadesc_offsets - Describe DMA descriptor, hardware link
 * list for dma transfer
 * @OWL_DMADESC_NEXT_LLI: physical address of the next link list
 * @OWL_DMADESC_SADDR: source physical address
 * @OWL_DMADESC_DADDR: destination physical address
 * @OWL_DMADESC_FLEN: frame length
 * @OWL_DMADESC_SRC_STRIDE: source stride
 * @OWL_DMADESC_DST_STRIDE: destination stride
 * @OWL_DMADESC_CTRLA: dma_mode and linklist ctrl config
 * @OWL_DMADESC_CTRLB: interrupt config
 * @OWL_DMADESC_CONST_NUM: data for constant fill
 * @OWL_DMADESC_SIZE: max size of this enum
 */
enum owl_dmadesc_offsets {
	OWL_DMADESC_NEXT_LLI = 0,
	OWL_DMADESC_SADDR,
	OWL_DMADESC_DADDR,
	OWL_DMADESC_FLEN,
	OWL_DMADESC_SRC_STRIDE,
	OWL_DMADESC_DST_STRIDE,
	OWL_DMADESC_CTRLA,
	OWL_DMADESC_CTRLB,
	OWL_DMADESC_CONST_NUM,
	OWL_DMADESC_SIZE
};

enum owl_dma_id {
	S900_DMA,
	S700_DMA,
};

/**
 * struct owl_dma_lli - Link list for dma transfer
 * @hw: hardware link list
 * @phys: physical address of hardware link list
 * @node: node for txd's lli_list
 */
struct owl_dma_lli {
	u32			hw[OWL_DMADESC_SIZE];
	dma_addr_t		phys;
	struct list_head	node;
};

/**
 * struct owl_dma_txd - Wrapper for struct dma_async_tx_descriptor
 * @vd: virtual DMA descriptor
 * @lli_list: link list of lli nodes
 * @cyclic: flag to indicate cyclic transfers
 */
struct owl_dma_txd {
	struct virt_dma_desc	vd;
	struct list_head	lli_list;
	bool			cyclic;
};

/**
 * struct owl_dma_pchan - Holder for the physical channels
 * @id: physical index to this channel
 * @base: virtual memory base for the dma channel
 * @vchan: the virtual channel currently being served by this physical channel
 */
struct owl_dma_pchan {
	u32			id;
	void __iomem		*base;
	struct owl_dma_vchan	*vchan;
};

/**
 * struct owl_dma_pchan - Wrapper for DMA ENGINE channel
 * @vc: wrapped virtual channel
 * @pchan: the physical channel utilized by this channel
 * @txd: active transaction on this channel
 * @cfg: slave configuration for this channel
 * @drq: physical DMA request ID for this channel
 */
struct owl_dma_vchan {
	struct virt_dma_chan	vc;
	struct owl_dma_pchan	*pchan;
	struct owl_dma_txd	*txd;
	struct dma_slave_config cfg;
	u8			drq;
};

/**
 * struct owl_dma - Holder for the Owl DMA controller
 * @dma: dma engine for this instance
 * @base: virtual memory base for the DMA controller
 * @clk: clock for the DMA controller
 * @lock: a lock to use when change DMA controller global register
 * @lli_pool: a pool for the LLI descriptors
 * @irq: interrupt ID for the DMA controller
 * @nr_pchans: the number of physical channels
 * @pchans: array of data for the physical channels
 * @nr_vchans: the number of physical channels
 * @vchans: array of data for the physical channels
 * @devid: device id based on OWL SoC
 */
struct owl_dma {
	struct dma_device	dma;
	void __iomem		*base;
	struct clk		*clk;
	spinlock_t		lock;
	struct dma_pool		*lli_pool;
	int			irq;

	unsigned int		nr_pchans;
	struct owl_dma_pchan	*pchans;

	unsigned int		nr_vchans;
	struct owl_dma_vchan	*vchans;
	enum owl_dma_id		devid;
};

static void pchan_update(struct owl_dma_pchan *pchan, u32 reg,
			 u32 val, bool state)
{
	u32 regval;

	regval = readl(pchan->base + reg);

	if (state)
		regval |= val;
	else
		regval &= ~val;

	writel(val, pchan->base + reg);
}

static void pchan_writel(struct owl_dma_pchan *pchan, u32 reg, u32 data)
{
	writel(data, pchan->base + reg);
}

static u32 pchan_readl(struct owl_dma_pchan *pchan, u32 reg)
{
	return readl(pchan->base + reg);
}

static void dma_update(struct owl_dma *od, u32 reg, u32 val, bool state)
{
	u32 regval;

	regval = readl(od->base + reg);

	if (state)
		regval |= val;
	else
		regval &= ~val;

	writel(val, od->base + reg);
}

static void dma_writel(struct owl_dma *od, u32 reg, u32 data)
{
	writel(data, od->base + reg);
}

static u32 dma_readl(struct owl_dma *od, u32 reg)
{
	return readl(od->base + reg);
}

static inline struct owl_dma *to_owl_dma(struct dma_device *dd)
{
	return container_of(dd, struct owl_dma, dma);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct owl_dma_vchan *to_owl_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct owl_dma_vchan, vc.chan);
}

static inline struct owl_dma_txd *to_owl_txd(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct owl_dma_txd, vd.tx);
}

static inline u32 llc_hw_ctrla(u32 mode, u32 llc_ctl)
{
	u32 ctl;

	ctl = BIT_FIELD(mode, 4, 28, 28) |
	      BIT_FIELD(mode, 8, 16, 20) |
	      BIT_FIELD(mode, 4, 8, 16) |
	      BIT_FIELD(mode, 6, 0, 10) |
	      BIT_FIELD(llc_ctl, 2, 10, 8) |
	      BIT_FIELD(llc_ctl, 2, 8, 6);

	return ctl;
}

static inline u32 llc_hw_ctrlb(u32 int_ctl)
{
	u32 ctl;

	/*
	 * Irrespective of the SoC, ctrlb value starts filling from
	 * bit 18.
	 */
	ctl = BIT_FIELD(int_ctl, 7, 0, 18);

	return ctl;
}

static u32 llc_hw_flen(struct owl_dma_lli *lli)
{
	return lli->hw[OWL_DMADESC_FLEN] & GENMASK(19, 0);
}

static void owl_dma_free_lli(struct owl_dma *od,
			     struct owl_dma_lli *lli)
{
	list_del(&lli->node);
	dma_pool_free(od->lli_pool, lli, lli->phys);
}

static struct owl_dma_lli *owl_dma_alloc_lli(struct owl_dma *od)
{
	struct owl_dma_lli *lli;
	dma_addr_t phys;

	lli = dma_pool_alloc(od->lli_pool, GFP_NOWAIT, &phys);
	if (!lli)
		return NULL;

	INIT_LIST_HEAD(&lli->node);
	lli->phys = phys;

	return lli;
}

static struct owl_dma_lli *owl_dma_add_lli(struct owl_dma_txd *txd,
					   struct owl_dma_lli *prev,
					   struct owl_dma_lli *next,
					   bool is_cyclic)
{
	if (!is_cyclic)
		list_add_tail(&next->node, &txd->lli_list);

	if (prev) {
		prev->hw[OWL_DMADESC_NEXT_LLI] = next->phys;
		prev->hw[OWL_DMADESC_CTRLA] |=
					llc_hw_ctrla(OWL_DMA_MODE_LME, 0);
	}

	return next;
}

static inline int owl_dma_cfg_lli(struct owl_dma_vchan *vchan,
				  struct owl_dma_lli *lli,
				  dma_addr_t src, dma_addr_t dst,
				  u32 len, enum dma_transfer_direction dir,
				  struct dma_slave_config *sconfig,
				  bool is_cyclic)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	u32 mode, ctrlb;

	mode = OWL_DMA_MODE_PW(0);

	switch (dir) {
	case DMA_MEM_TO_MEM:
		mode |= OWL_DMA_MODE_TS(0) | OWL_DMA_MODE_ST_DCU |
			OWL_DMA_MODE_DT_DCU | OWL_DMA_MODE_SAM_INC |
			OWL_DMA_MODE_DAM_INC;

		break;
	case DMA_MEM_TO_DEV:
		mode |= OWL_DMA_MODE_TS(vchan->drq)
			| OWL_DMA_MODE_ST_DCU | OWL_DMA_MODE_DT_DEV
			| OWL_DMA_MODE_SAM_INC | OWL_DMA_MODE_DAM_CONST;

		/*
		 * Hardware only supports 32bit and 8bit buswidth. Since the
		 * default is 32bit, select 8bit only when requested.
		 */
		if (sconfig->dst_addr_width == DMA_SLAVE_BUSWIDTH_1_BYTE)
			mode |= OWL_DMA_MODE_NDDBW_8BIT;

		break;
	case DMA_DEV_TO_MEM:
		 mode |= OWL_DMA_MODE_TS(vchan->drq)
			| OWL_DMA_MODE_ST_DEV | OWL_DMA_MODE_DT_DCU
			| OWL_DMA_MODE_SAM_CONST | OWL_DMA_MODE_DAM_INC;

		/*
		 * Hardware only supports 32bit and 8bit buswidth. Since the
		 * default is 32bit, select 8bit only when requested.
		 */
		if (sconfig->src_addr_width == DMA_SLAVE_BUSWIDTH_1_BYTE)
			mode |= OWL_DMA_MODE_NDDBW_8BIT;

		break;
	default:
		return -EINVAL;
	}

	lli->hw[OWL_DMADESC_CTRLA] = llc_hw_ctrla(mode,
						  OWL_DMA_LLC_SAV_LOAD_NEXT |
						  OWL_DMA_LLC_DAV_LOAD_NEXT);

	if (is_cyclic)
		ctrlb = llc_hw_ctrlb(OWL_DMA_INTCTL_BLOCK);
	else
		ctrlb = llc_hw_ctrlb(OWL_DMA_INTCTL_SUPER_BLOCK);

	lli->hw[OWL_DMADESC_NEXT_LLI] = 0; /* One link list by default */
	lli->hw[OWL_DMADESC_SADDR] = src;
	lli->hw[OWL_DMADESC_DADDR] = dst;
	lli->hw[OWL_DMADESC_SRC_STRIDE] = 0;
	lli->hw[OWL_DMADESC_DST_STRIDE] = 0;

	if (od->devid == S700_DMA) {
		/* Max frame length is 1MB */
		lli->hw[OWL_DMADESC_FLEN] = len;
		/*
		 * On S700, word starts from offset 0x1C is shared between
		 * frame count and ctrlb, where first 12 bits are for frame
		 * count and rest of 20 bits are for ctrlb.
		 */
		lli->hw[OWL_DMADESC_CTRLB] = FCNT_VAL | ctrlb;
	} else {
		/*
		 * On S900, word starts from offset 0xC is shared between
		 * frame length (max frame length is 1MB) and frame count,
		 * where first 20 bits are for frame length and rest of
		 * 12 bits are for frame count.
		 */
		lli->hw[OWL_DMADESC_FLEN] = len | FCNT_VAL << 20;
		lli->hw[OWL_DMADESC_CTRLB] = ctrlb;
	}

	return 0;
}

static struct owl_dma_pchan *owl_dma_get_pchan(struct owl_dma *od,
					       struct owl_dma_vchan *vchan)
{
	struct owl_dma_pchan *pchan = NULL;
	unsigned long flags;
	int i;

	for (i = 0; i < od->nr_pchans; i++) {
		pchan = &od->pchans[i];

		spin_lock_irqsave(&od->lock, flags);
		if (!pchan->vchan) {
			pchan->vchan = vchan;
			spin_unlock_irqrestore(&od->lock, flags);
			break;
		}

		spin_unlock_irqrestore(&od->lock, flags);
	}

	return pchan;
}

static int owl_dma_pchan_busy(struct owl_dma *od, struct owl_dma_pchan *pchan)
{
	unsigned int val;

	val = dma_readl(od, OWL_DMA_IDLE_STAT);

	return !(val & (1 << pchan->id));
}

static void owl_dma_terminate_pchan(struct owl_dma *od,
				    struct owl_dma_pchan *pchan)
{
	unsigned long flags;
	u32 irq_pd;

	pchan_writel(pchan, OWL_DMAX_START, 0);
	pchan_update(pchan, OWL_DMAX_INT_STATUS, 0xff, false);

	spin_lock_irqsave(&od->lock, flags);
	dma_update(od, OWL_DMA_IRQ_EN0, (1 << pchan->id), false);

	irq_pd = dma_readl(od, OWL_DMA_IRQ_PD0);
	if (irq_pd & (1 << pchan->id)) {
		dev_warn(od->dma.dev,
			 "terminating pchan %d that still has pending irq\n",
			 pchan->id);
		dma_writel(od, OWL_DMA_IRQ_PD0, (1 << pchan->id));
	}

	pchan->vchan = NULL;

	spin_unlock_irqrestore(&od->lock, flags);
}

static void owl_dma_pause_pchan(struct owl_dma_pchan *pchan)
{
	pchan_writel(pchan, 1, OWL_DMAX_PAUSE);
}

static void owl_dma_resume_pchan(struct owl_dma_pchan *pchan)
{
	pchan_writel(pchan, 0, OWL_DMAX_PAUSE);
}

static int owl_dma_start_next_txd(struct owl_dma_vchan *vchan)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	struct virt_dma_desc *vd = vchan_next_desc(&vchan->vc);
	struct owl_dma_pchan *pchan = vchan->pchan;
	struct owl_dma_txd *txd = to_owl_txd(&vd->tx);
	struct owl_dma_lli *lli;
	unsigned long flags;
	u32 int_ctl;

	list_del(&vd->node);

	vchan->txd = txd;

	/* Wait for channel inactive */
	while (owl_dma_pchan_busy(od, pchan))
		cpu_relax();

	lli = list_first_entry(&txd->lli_list,
			       struct owl_dma_lli, node);

	if (txd->cyclic)
		int_ctl = OWL_DMA_INTCTL_BLOCK;
	else
		int_ctl = OWL_DMA_INTCTL_SUPER_BLOCK;

	pchan_writel(pchan, OWL_DMAX_MODE, OWL_DMA_MODE_LME);
	pchan_writel(pchan, OWL_DMAX_LINKLIST_CTL,
		     OWL_DMA_LLC_SAV_LOAD_NEXT | OWL_DMA_LLC_DAV_LOAD_NEXT);
	pchan_writel(pchan, OWL_DMAX_NEXT_DESCRIPTOR, lli->phys);
	pchan_writel(pchan, OWL_DMAX_INT_CTL, int_ctl);

	/* Clear IRQ status for this pchan */
	pchan_update(pchan, OWL_DMAX_INT_STATUS, 0xff, false);

	spin_lock_irqsave(&od->lock, flags);

	dma_update(od, OWL_DMA_IRQ_EN0, (1 << pchan->id), true);

	spin_unlock_irqrestore(&od->lock, flags);

	dev_dbg(chan2dev(&vchan->vc.chan), "starting pchan %d\n", pchan->id);

	/* Start DMA transfer for this pchan */
	pchan_writel(pchan, OWL_DMAX_START, 0x1);

	return 0;
}

static void owl_dma_phy_free(struct owl_dma *od, struct owl_dma_vchan *vchan)
{
	/* Ensure that the physical channel is stopped */
	owl_dma_terminate_pchan(od, vchan->pchan);

	vchan->pchan = NULL;
}

static irqreturn_t owl_dma_interrupt(int irq, void *dev_id)
{
	struct owl_dma *od = dev_id;
	struct owl_dma_vchan *vchan;
	struct owl_dma_pchan *pchan;
	unsigned long pending;
	int i;
	unsigned int global_irq_pending, chan_irq_pending;

	spin_lock(&od->lock);

	pending = dma_readl(od, OWL_DMA_IRQ_PD0);

	/* Clear IRQ status for each pchan */
	for_each_set_bit(i, &pending, od->nr_pchans) {
		pchan = &od->pchans[i];
		pchan_update(pchan, OWL_DMAX_INT_STATUS, 0xff, false);
	}

	/* Clear pending IRQ */
	dma_writel(od, OWL_DMA_IRQ_PD0, pending);

	/* Check missed pending IRQ */
	for (i = 0; i < od->nr_pchans; i++) {
		pchan = &od->pchans[i];
		chan_irq_pending = pchan_readl(pchan, OWL_DMAX_INT_CTL) &
				   pchan_readl(pchan, OWL_DMAX_INT_STATUS);

		/* Dummy read to ensure OWL_DMA_IRQ_PD0 value is updated */
		dma_readl(od, OWL_DMA_IRQ_PD0);

		global_irq_pending = dma_readl(od, OWL_DMA_IRQ_PD0);

		if (chan_irq_pending && !(global_irq_pending & BIT(i))) {
			dev_dbg(od->dma.dev,
				"global and channel IRQ pending match err\n");

			/* Clear IRQ status for this pchan */
			pchan_update(pchan, OWL_DMAX_INT_STATUS,
				     0xff, false);

			/* Update global IRQ pending */
			pending |= BIT(i);
		}
	}

	spin_unlock(&od->lock);

	for_each_set_bit(i, &pending, od->nr_pchans) {
		struct owl_dma_txd *txd;

		pchan = &od->pchans[i];

		vchan = pchan->vchan;
		if (!vchan) {
			dev_warn(od->dma.dev, "no vchan attached on pchan %d\n",
				 pchan->id);
			continue;
		}

		spin_lock(&vchan->vc.lock);

		txd = vchan->txd;
		if (txd) {
			vchan->txd = NULL;

			vchan_cookie_complete(&txd->vd);

			/*
			 * Start the next descriptor (if any),
			 * otherwise free this channel.
			 */
			if (vchan_next_desc(&vchan->vc))
				owl_dma_start_next_txd(vchan);
			else
				owl_dma_phy_free(od, vchan);
		}

		spin_unlock(&vchan->vc.lock);
	}

	return IRQ_HANDLED;
}

static void owl_dma_free_txd(struct owl_dma *od, struct owl_dma_txd *txd)
{
	struct owl_dma_lli *lli, *_lli;

	if (unlikely(!txd))
		return;

	list_for_each_entry_safe(lli, _lli, &txd->lli_list, node)
		owl_dma_free_lli(od, lli);

	kfree(txd);
}

static void owl_dma_desc_free(struct virt_dma_desc *vd)
{
	struct owl_dma *od = to_owl_dma(vd->tx.chan->device);
	struct owl_dma_txd *txd = to_owl_txd(&vd->tx);

	owl_dma_free_txd(od, txd);
}

static int owl_dma_terminate_all(struct dma_chan *chan)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&vchan->vc.lock, flags);

	if (vchan->pchan)
		owl_dma_phy_free(od, vchan);

	if (vchan->txd) {
		owl_dma_desc_free(&vchan->txd->vd);
		vchan->txd = NULL;
	}

	vchan_get_all_descriptors(&vchan->vc, &head);

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	vchan_dma_desc_free_list(&vchan->vc, &head);

	return 0;
}

static int owl_dma_config(struct dma_chan *chan,
			  struct dma_slave_config *config)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);

	/* Reject definitely invalid configurations */
	if (config->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    config->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	memcpy(&vchan->cfg, config, sizeof(struct dma_slave_config));

	return 0;
}

static int owl_dma_pause(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);

	owl_dma_pause_pchan(vchan->pchan);

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int owl_dma_resume(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	if (!vchan->pchan && !vchan->txd)
		return 0;

	dev_dbg(chan2dev(chan), "vchan %p: resume\n", &vchan->vc);

	spin_lock_irqsave(&vchan->vc.lock, flags);

	owl_dma_resume_pchan(vchan->pchan);

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static u32 owl_dma_getbytes_chan(struct owl_dma_vchan *vchan)
{
	struct owl_dma_pchan *pchan;
	struct owl_dma_txd *txd;
	struct owl_dma_lli *lli;
	unsigned int next_lli_phy;
	size_t bytes;

	pchan = vchan->pchan;
	txd = vchan->txd;

	if (!pchan || !txd)
		return 0;

	/* Get remain count of current node in link list */
	bytes = pchan_readl(pchan, OWL_DMAX_REMAIN_CNT);

	/* Loop through the preceding nodes to get total remaining bytes */
	if (pchan_readl(pchan, OWL_DMAX_MODE) & OWL_DMA_MODE_LME) {
		next_lli_phy = pchan_readl(pchan, OWL_DMAX_NEXT_DESCRIPTOR);
		list_for_each_entry(lli, &txd->lli_list, node) {
			/* Start from the next active node */
			if (lli->phys == next_lli_phy) {
				list_for_each_entry(lli, &txd->lli_list, node)
					bytes += llc_hw_flen(lli);
				break;
			}
		}
	}

	return bytes;
}

static enum dma_status owl_dma_tx_status(struct dma_chan *chan,
					 dma_cookie_t cookie,
					 struct dma_tx_state *state)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct owl_dma_lli *lli;
	struct virt_dma_desc *vd;
	struct owl_dma_txd *txd;
	enum dma_status ret;
	unsigned long flags;
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, state);
	if (ret == DMA_COMPLETE || !state)
		return ret;

	spin_lock_irqsave(&vchan->vc.lock, flags);

	vd = vchan_find_desc(&vchan->vc, cookie);
	if (vd) {
		txd = to_owl_txd(&vd->tx);
		list_for_each_entry(lli, &txd->lli_list, node)
			bytes += llc_hw_flen(lli);
	} else {
		bytes = owl_dma_getbytes_chan(vchan);
	}

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	dma_set_residue(state, bytes);

	return ret;
}

static void owl_dma_phy_alloc_and_start(struct owl_dma_vchan *vchan)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	struct owl_dma_pchan *pchan;

	pchan = owl_dma_get_pchan(od, vchan);
	if (!pchan)
		return;

	dev_dbg(od->dma.dev, "allocated pchan %d\n", pchan->id);

	vchan->pchan = pchan;
	owl_dma_start_next_txd(vchan);
}

static void owl_dma_issue_pending(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);
	if (vchan_issue_pending(&vchan->vc)) {
		if (!vchan->pchan)
			owl_dma_phy_alloc_and_start(vchan);
	}
	spin_unlock_irqrestore(&vchan->vc.lock, flags);
}

static struct dma_async_tx_descriptor
		*owl_dma_prep_memcpy(struct dma_chan *chan,
				     dma_addr_t dst, dma_addr_t src,
				     size_t len, unsigned long flags)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct owl_dma_txd *txd;
	struct owl_dma_lli *lli, *prev = NULL;
	size_t offset, bytes;
	int ret;

	if (!len)
		return NULL;

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	INIT_LIST_HEAD(&txd->lli_list);

	/* Process the transfer as frame by frame */
	for (offset = 0; offset < len; offset += bytes) {
		lli = owl_dma_alloc_lli(od);
		if (!lli) {
			dev_warn(chan2dev(chan), "failed to allocate lli\n");
			goto err_txd_free;
		}

		bytes = min_t(size_t, (len - offset), OWL_DMA_FRAME_MAX_LENGTH);

		ret = owl_dma_cfg_lli(vchan, lli, src + offset, dst + offset,
				      bytes, DMA_MEM_TO_MEM,
				      &vchan->cfg, txd->cyclic);
		if (ret) {
			dev_warn(chan2dev(chan), "failed to config lli\n");
			goto err_txd_free;
		}

		prev = owl_dma_add_lli(txd, prev, lli, false);
	}

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	owl_dma_free_txd(od, txd);
	return NULL;
}

static struct dma_async_tx_descriptor
		*owl_dma_prep_slave_sg(struct dma_chan *chan,
				       struct scatterlist *sgl,
				       unsigned int sg_len,
				       enum dma_transfer_direction dir,
				       unsigned long flags, void *context)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct owl_dma_txd *txd;
	struct owl_dma_lli *lli, *prev = NULL;
	struct scatterlist *sg;
	dma_addr_t addr, src = 0, dst = 0;
	size_t len;
	int ret, i;

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	INIT_LIST_HEAD(&txd->lli_list);

	for_each_sg(sgl, sg, sg_len, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);

		if (len > OWL_DMA_FRAME_MAX_LENGTH) {
			dev_err(od->dma.dev,
				"frame length exceeds max supported length");
			goto err_txd_free;
		}

		lli = owl_dma_alloc_lli(od);
		if (!lli) {
			dev_err(chan2dev(chan), "failed to allocate lli");
			goto err_txd_free;
		}

		if (dir == DMA_MEM_TO_DEV) {
			src = addr;
			dst = sconfig->dst_addr;
		} else {
			src = sconfig->src_addr;
			dst = addr;
		}

		ret = owl_dma_cfg_lli(vchan, lli, src, dst, len, dir, sconfig,
				      txd->cyclic);
		if (ret) {
			dev_warn(chan2dev(chan), "failed to config lli");
			goto err_txd_free;
		}

		prev = owl_dma_add_lli(txd, prev, lli, false);
	}

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	owl_dma_free_txd(od, txd);

	return NULL;
}

static struct dma_async_tx_descriptor
		*owl_prep_dma_cyclic(struct dma_chan *chan,
				     dma_addr_t buf_addr, size_t buf_len,
				     size_t period_len,
				     enum dma_transfer_direction dir,
				     unsigned long flags)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct owl_dma_txd *txd;
	struct owl_dma_lli *lli, *prev = NULL, *first = NULL;
	dma_addr_t src = 0, dst = 0;
	unsigned int periods = buf_len / period_len;
	int ret, i;

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	INIT_LIST_HEAD(&txd->lli_list);
	txd->cyclic = true;

	for (i = 0; i < periods; i++) {
		lli = owl_dma_alloc_lli(od);
		if (!lli) {
			dev_warn(chan2dev(chan), "failed to allocate lli");
			goto err_txd_free;
		}

		if (dir == DMA_MEM_TO_DEV) {
			src = buf_addr + (period_len * i);
			dst = sconfig->dst_addr;
		} else if (dir == DMA_DEV_TO_MEM) {
			src = sconfig->src_addr;
			dst = buf_addr + (period_len * i);
		}

		ret = owl_dma_cfg_lli(vchan, lli, src, dst, period_len,
				      dir, sconfig, txd->cyclic);
		if (ret) {
			dev_warn(chan2dev(chan), "failed to config lli");
			goto err_txd_free;
		}

		if (!first)
			first = lli;

		prev = owl_dma_add_lli(txd, prev, lli, false);
	}

	/* close the cyclic list */
	owl_dma_add_lli(txd, prev, first, true);

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	owl_dma_free_txd(od, txd);

	return NULL;
}

static void owl_dma_free_chan_resources(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);

	/* Ensure all queued descriptors are freed */
	vchan_free_chan_resources(&vchan->vc);
}

static inline void owl_dma_free(struct owl_dma *od)
{
	struct owl_dma_vchan *vchan = NULL;
	struct owl_dma_vchan *next;

	list_for_each_entry_safe(vchan,
				 next, &od->dma.channels, vc.chan.device_node) {
		list_del(&vchan->vc.chan.device_node);
		tasklet_kill(&vchan->vc.task);
	}
}

static struct dma_chan *owl_dma_of_xlate(struct of_phandle_args *dma_spec,
					 struct of_dma *ofdma)
{
	struct owl_dma *od = ofdma->of_dma_data;
	struct owl_dma_vchan *vchan;
	struct dma_chan *chan;
	u8 drq = dma_spec->args[0];

	if (drq > od->nr_vchans)
		return NULL;

	chan = dma_get_any_slave_channel(&od->dma);
	if (!chan)
		return NULL;

	vchan = to_owl_vchan(chan);
	vchan->drq = drq;

	return chan;
}

static const struct of_device_id owl_dma_match[] = {
	{ .compatible = "actions,s500-dma", .data = (void *)S900_DMA,},
	{ .compatible = "actions,s700-dma", .data = (void *)S700_DMA,},
	{ .compatible = "actions,s900-dma", .data = (void *)S900_DMA,},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, owl_dma_match);

static int owl_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct owl_dma *od;
	int ret, i, nr_channels, nr_requests;

	od = devm_kzalloc(&pdev->dev, sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	od->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(od->base))
		return PTR_ERR(od->base);

	ret = of_property_read_u32(np, "dma-channels", &nr_channels);
	if (ret) {
		dev_err(&pdev->dev, "can't get dma-channels\n");
		return ret;
	}

	ret = of_property_read_u32(np, "dma-requests", &nr_requests);
	if (ret) {
		dev_err(&pdev->dev, "can't get dma-requests\n");
		return ret;
	}

	dev_info(&pdev->dev, "dma-channels %d, dma-requests %d\n",
		 nr_channels, nr_requests);

	od->devid = (enum owl_dma_id)of_device_get_match_data(&pdev->dev);

	od->nr_pchans = nr_channels;
	od->nr_vchans = nr_requests;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, od);
	spin_lock_init(&od->lock);

	dma_cap_set(DMA_MEMCPY, od->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, od->dma.cap_mask);
	dma_cap_set(DMA_CYCLIC, od->dma.cap_mask);

	od->dma.dev = &pdev->dev;
	od->dma.device_free_chan_resources = owl_dma_free_chan_resources;
	od->dma.device_tx_status = owl_dma_tx_status;
	od->dma.device_issue_pending = owl_dma_issue_pending;
	od->dma.device_prep_dma_memcpy = owl_dma_prep_memcpy;
	od->dma.device_prep_slave_sg = owl_dma_prep_slave_sg;
	od->dma.device_prep_dma_cyclic = owl_prep_dma_cyclic;
	od->dma.device_config = owl_dma_config;
	od->dma.device_pause = owl_dma_pause;
	od->dma.device_resume = owl_dma_resume;
	od->dma.device_terminate_all = owl_dma_terminate_all;
	od->dma.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	od->dma.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	od->dma.directions = BIT(DMA_MEM_TO_MEM);
	od->dma.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	INIT_LIST_HEAD(&od->dma.channels);

	od->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(od->clk)) {
		dev_err(&pdev->dev, "unable to get clock\n");
		return PTR_ERR(od->clk);
	}

	/*
	 * Eventhough the DMA controller is capable of generating 4
	 * IRQ's for DMA priority feature, we only use 1 IRQ for
	 * simplification.
	 */
	od->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, od->irq, owl_dma_interrupt, 0,
			       dev_name(&pdev->dev), od);
	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ\n");
		return ret;
	}

	/* Init physical channel */
	od->pchans = devm_kcalloc(&pdev->dev, od->nr_pchans,
				  sizeof(struct owl_dma_pchan), GFP_KERNEL);
	if (!od->pchans)
		return -ENOMEM;

	for (i = 0; i < od->nr_pchans; i++) {
		struct owl_dma_pchan *pchan = &od->pchans[i];

		pchan->id = i;
		pchan->base = od->base + OWL_DMA_CHAN_BASE(i);
	}

	/* Init virtual channel */
	od->vchans = devm_kcalloc(&pdev->dev, od->nr_vchans,
				  sizeof(struct owl_dma_vchan), GFP_KERNEL);
	if (!od->vchans)
		return -ENOMEM;

	for (i = 0; i < od->nr_vchans; i++) {
		struct owl_dma_vchan *vchan = &od->vchans[i];

		vchan->vc.desc_free = owl_dma_desc_free;
		vchan_init(&vchan->vc, &od->dma);
	}

	/* Create a pool of consistent memory blocks for hardware descriptors */
	od->lli_pool = dma_pool_create(dev_name(od->dma.dev), od->dma.dev,
				       sizeof(struct owl_dma_lli),
				       __alignof__(struct owl_dma_lli),
				       0);
	if (!od->lli_pool) {
		dev_err(&pdev->dev, "unable to allocate DMA descriptor pool\n");
		return -ENOMEM;
	}

	clk_prepare_enable(od->clk);

	ret = dma_async_device_register(&od->dma);
	if (ret) {
		dev_err(&pdev->dev, "failed to register DMA engine device\n");
		goto err_pool_free;
	}

	/* Device-tree DMA controller registration */
	ret = of_dma_controller_register(pdev->dev.of_node,
					 owl_dma_of_xlate, od);
	if (ret) {
		dev_err(&pdev->dev, "of_dma_controller_register failed\n");
		goto err_dma_unregister;
	}

	return 0;

err_dma_unregister:
	dma_async_device_unregister(&od->dma);
err_pool_free:
	clk_disable_unprepare(od->clk);
	dma_pool_destroy(od->lli_pool);

	return ret;
}

static int owl_dma_remove(struct platform_device *pdev)
{
	struct owl_dma *od = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&od->dma);

	/* Mask all interrupts for this execution environment */
	dma_writel(od, OWL_DMA_IRQ_EN0, 0x0);

	/* Make sure we won't have any further interrupts */
	devm_free_irq(od->dma.dev, od->irq, od);

	owl_dma_free(od);

	clk_disable_unprepare(od->clk);
	dma_pool_destroy(od->lli_pool);

	return 0;
}

static struct platform_driver owl_dma_driver = {
	.probe	= owl_dma_probe,
	.remove	= owl_dma_remove,
	.driver = {
		.name = "dma-owl",
		.of_match_table = of_match_ptr(owl_dma_match),
	},
};

static int owl_dma_init(void)
{
	return platform_driver_register(&owl_dma_driver);
}
subsys_initcall(owl_dma_init);

static void __exit owl_dma_exit(void)
{
	platform_driver_unregister(&owl_dma_driver);
}
module_exit(owl_dma_exit);

MODULE_AUTHOR("David Liu <liuwei@actions-semi.com>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Actions Semi Owl SoCs DMA driver");
MODULE_LICENSE("GPL");
