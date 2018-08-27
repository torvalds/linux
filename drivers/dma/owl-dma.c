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

/**
 * struct owl_dma_lli_hw - Hardware link list for dma transfer
 * @next_lli: physical address of the next link list
 * @saddr: source physical address
 * @daddr: destination physical address
 * @flen: frame length
 * @fcnt: frame count
 * @src_stride: source stride
 * @dst_stride: destination stride
 * @ctrla: dma_mode and linklist ctrl config
 * @ctrlb: interrupt config
 * @const_num: data for constant fill
 */
struct owl_dma_lli_hw {
	u32	next_lli;
	u32	saddr;
	u32	daddr;
	u32	flen:20;
	u32	fcnt:12;
	u32	src_stride;
	u32	dst_stride;
	u32	ctrla;
	u32	ctrlb;
	u32	const_num;
};

/**
 * struct owl_dma_lli - Link list for dma transfer
 * @hw: hardware link list
 * @phys: physical address of hardware link list
 * @node: node for txd's lli_list
 */
struct owl_dma_lli {
	struct  owl_dma_lli_hw	hw;
	dma_addr_t		phys;
	struct list_head	node;
};

/**
 * struct owl_dma_txd - Wrapper for struct dma_async_tx_descriptor
 * @vd: virtual DMA descriptor
 * @lli_list: link list of lli nodes
 */
struct owl_dma_txd {
	struct virt_dma_desc	vd;
	struct list_head	lli_list;
};

/**
 * struct owl_dma_pchan - Holder for the physical channels
 * @id: physical index to this channel
 * @base: virtual memory base for the dma channel
 * @vchan: the virtual channel currently being served by this physical channel
 * @lock: a lock to use when altering an instance of this struct
 */
struct owl_dma_pchan {
	u32			id;
	void __iomem		*base;
	struct owl_dma_vchan	*vchan;
	spinlock_t		lock;
};

/**
 * struct owl_dma_pchan - Wrapper for DMA ENGINE channel
 * @vc: wrappped virtual channel
 * @pchan: the physical channel utilized by this channel
 * @txd: active transaction on this channel
 */
struct owl_dma_vchan {
	struct virt_dma_chan	vc;
	struct owl_dma_pchan	*pchan;
	struct owl_dma_txd	*txd;
};

/**
 * struct owl_dma - Holder for the Owl DMA controller
 * @dma: dma engine for this instance
 * @base: virtual memory base for the DMA controller
 * @clk: clock for the DMA controller
 * @lock: a lock to use when change DMA controller global register
 * @lli_pool: a pool for the LLI descriptors
 * @nr_pchans: the number of physical channels
 * @pchans: array of data for the physical channels
 * @nr_vchans: the number of physical channels
 * @vchans: array of data for the physical channels
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

	ctl = BIT_FIELD(int_ctl, 7, 0, 18);

	return ctl;
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
					   struct owl_dma_lli *next)
{
	list_add_tail(&next->node, &txd->lli_list);

	if (prev) {
		prev->hw.next_lli = next->phys;
		prev->hw.ctrla |= llc_hw_ctrla(OWL_DMA_MODE_LME, 0);
	}

	return next;
}

static inline int owl_dma_cfg_lli(struct owl_dma_vchan *vchan,
				  struct owl_dma_lli *lli,
				  dma_addr_t src, dma_addr_t dst,
				  u32 len, enum dma_transfer_direction dir)
{
	struct owl_dma_lli_hw *hw = &lli->hw;
	u32 mode;

	mode = OWL_DMA_MODE_PW(0);

	switch (dir) {
	case DMA_MEM_TO_MEM:
		mode |= OWL_DMA_MODE_TS(0) | OWL_DMA_MODE_ST_DCU |
			OWL_DMA_MODE_DT_DCU | OWL_DMA_MODE_SAM_INC |
			OWL_DMA_MODE_DAM_INC;

		break;
	default:
		return -EINVAL;
	}

	hw->next_lli = 0; /* One link list by default */
	hw->saddr = src;
	hw->daddr = dst;

	hw->fcnt = 1; /* Frame count fixed as 1 */
	hw->flen = len; /* Max frame length is 1MB */
	hw->src_stride = 0;
	hw->dst_stride = 0;
	hw->ctrla = llc_hw_ctrla(mode,
				 OWL_DMA_LLC_SAV_LOAD_NEXT |
				 OWL_DMA_LLC_DAV_LOAD_NEXT);

	hw->ctrlb = llc_hw_ctrlb(OWL_DMA_INTCTL_SUPER_BLOCK);

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

		spin_lock_irqsave(&pchan->lock, flags);
		if (!pchan->vchan) {
			pchan->vchan = vchan;
			spin_unlock_irqrestore(&pchan->lock, flags);
			break;
		}

		spin_unlock_irqrestore(&pchan->lock, flags);
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

		if (chan_irq_pending && !(global_irq_pending & BIT(i)))	{
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
	vchan_dma_desc_free_list(&vchan->vc, &head);

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
					bytes += lli->hw.flen;
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
			bytes += lli->hw.flen;
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
				      bytes, DMA_MEM_TO_MEM);
		if (ret) {
			dev_warn(chan2dev(chan), "failed to config lli\n");
			goto err_txd_free;
		}

		prev = owl_dma_add_lli(txd, prev, lli);
	}

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

static int owl_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct owl_dma *od;
	struct resource *res;
	int ret, i, nr_channels, nr_requests;

	od = devm_kzalloc(&pdev->dev, sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	od->base = devm_ioremap_resource(&pdev->dev, res);
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

	od->nr_pchans = nr_channels;
	od->nr_vchans = nr_requests;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, od);
	spin_lock_init(&od->lock);

	dma_cap_set(DMA_MEMCPY, od->dma.cap_mask);

	od->dma.dev = &pdev->dev;
	od->dma.device_free_chan_resources = owl_dma_free_chan_resources;
	od->dma.device_tx_status = owl_dma_tx_status;
	od->dma.device_issue_pending = owl_dma_issue_pending;
	od->dma.device_prep_dma_memcpy = owl_dma_prep_memcpy;
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

	return 0;

err_pool_free:
	clk_disable_unprepare(od->clk);
	dma_pool_destroy(od->lli_pool);

	return ret;
}

static int owl_dma_remove(struct platform_device *pdev)
{
	struct owl_dma *od = platform_get_drvdata(pdev);

	dma_async_device_unregister(&od->dma);

	/* Mask all interrupts for this execution environment */
	dma_writel(od, OWL_DMA_IRQ_EN0, 0x0);

	/* Make sure we won't have any further interrupts */
	devm_free_irq(od->dma.dev, od->irq, od);

	owl_dma_free(od);

	clk_disable_unprepare(od->clk);

	return 0;
}

static const struct of_device_id owl_dma_match[] = {
	{ .compatible = "actions,s900-dma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, owl_dma_match);

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
