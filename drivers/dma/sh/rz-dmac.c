// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L DMA Controller Driver
 *
 * Based on imx-dma.c
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 * Copyright 2010 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2012 Javier Martin, Vista Silicon <javier.martin@vista-silicon.com>
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

enum  rz_dmac_prep_type {
	RZ_DMAC_DESC_MEMCPY,
	RZ_DMAC_DESC_SLAVE_SG,
};

struct rz_lmdesc {
	u32 header;
	u32 sa;
	u32 da;
	u32 tb;
	u32 chcfg;
	u32 chitvl;
	u32 chext;
	u32 nxla;
};

struct rz_dmac_desc {
	struct virt_dma_desc vd;
	dma_addr_t src;
	dma_addr_t dest;
	size_t len;
	struct list_head node;
	enum dma_transfer_direction direction;
	enum rz_dmac_prep_type type;
	/* For slave sg */
	struct scatterlist *sg;
	unsigned int sgcount;
};

#define to_rz_dmac_desc(d)	container_of(d, struct rz_dmac_desc, vd)

struct rz_dmac_chan {
	struct virt_dma_chan vc;
	void __iomem *ch_base;
	void __iomem *ch_cmn_base;
	unsigned int index;
	int irq;
	struct rz_dmac_desc *desc;
	int descs_allocated;

	dma_addr_t src_per_address;
	dma_addr_t dst_per_address;

	u32 chcfg;
	u32 chctrl;
	int mid_rid;

	struct list_head ld_free;
	struct list_head ld_queue;
	struct list_head ld_active;

	struct {
		struct rz_lmdesc *base;
		struct rz_lmdesc *head;
		struct rz_lmdesc *tail;
		dma_addr_t base_dma;
	} lmdesc;
};

#define to_rz_dmac_chan(c)	container_of(c, struct rz_dmac_chan, vc.chan)

struct rz_dmac {
	struct dma_device engine;
	struct device *dev;
	struct reset_control *rstc;
	void __iomem *base;
	void __iomem *ext_base;

	unsigned int n_channels;
	struct rz_dmac_chan *channels;

	DECLARE_BITMAP(modules, 1024);
};

#define to_rz_dmac(d)	container_of(d, struct rz_dmac, engine)

/*
 * -----------------------------------------------------------------------------
 * Registers
 */

#define CHSTAT				0x0024
#define CHCTRL				0x0028
#define CHCFG				0x002c
#define NXLA				0x0038

#define DCTRL				0x0000

#define EACH_CHANNEL_OFFSET		0x0040
#define CHANNEL_0_7_OFFSET		0x0000
#define CHANNEL_0_7_COMMON_BASE		0x0300
#define CHANNEL_8_15_OFFSET		0x0400
#define CHANNEL_8_15_COMMON_BASE	0x0700

#define CHSTAT_ER			BIT(4)
#define CHSTAT_EN			BIT(0)

#define CHCTRL_CLRINTMSK		BIT(17)
#define CHCTRL_CLRSUS			BIT(9)
#define CHCTRL_CLRTC			BIT(6)
#define CHCTRL_CLREND			BIT(5)
#define CHCTRL_CLRRQ			BIT(4)
#define CHCTRL_SWRST			BIT(3)
#define CHCTRL_STG			BIT(2)
#define CHCTRL_CLREN			BIT(1)
#define CHCTRL_SETEN			BIT(0)
#define CHCTRL_DEFAULT			(CHCTRL_CLRINTMSK | CHCTRL_CLRSUS | \
					 CHCTRL_CLRTC |	CHCTRL_CLREND | \
					 CHCTRL_CLRRQ | CHCTRL_SWRST | \
					 CHCTRL_CLREN)

#define CHCFG_DMS			BIT(31)
#define CHCFG_DEM			BIT(24)
#define CHCFG_DAD			BIT(21)
#define CHCFG_SAD			BIT(20)
#define CHCFG_REQD			BIT(3)
#define CHCFG_SEL(bits)			((bits) & 0x07)
#define CHCFG_MEM_COPY			(0x80400008)
#define CHCFG_FILL_DDS(a)		(((a) << 16) & GENMASK(19, 16))
#define CHCFG_FILL_SDS(a)		(((a) << 12) & GENMASK(15, 12))
#define CHCFG_FILL_TM(a)		(((a) & BIT(5)) << 22)
#define CHCFG_FILL_AM(a)		(((a) & GENMASK(4, 2)) << 6)
#define CHCFG_FILL_LVL(a)		(((a) & BIT(1)) << 5)
#define CHCFG_FILL_HIEN(a)		(((a) & BIT(0)) << 5)

#define MID_RID_MASK			GENMASK(9, 0)
#define CHCFG_MASK			GENMASK(15, 10)
#define CHCFG_DS_INVALID		0xFF
#define DCTRL_LVINT			BIT(1)
#define DCTRL_PR			BIT(0)
#define DCTRL_DEFAULT			(DCTRL_LVINT | DCTRL_PR)

/* LINK MODE DESCRIPTOR */
#define HEADER_LV			BIT(0)

#define RZ_DMAC_MAX_CHAN_DESCRIPTORS	16
#define RZ_DMAC_MAX_CHANNELS		16
#define DMAC_NR_LMDESC			64

/*
 * -----------------------------------------------------------------------------
 * Device access
 */

static void rz_dmac_writel(struct rz_dmac *dmac, unsigned int val,
			   unsigned int offset)
{
	writel(val, dmac->base + offset);
}

static void rz_dmac_ext_writel(struct rz_dmac *dmac, unsigned int val,
			       unsigned int offset)
{
	writel(val, dmac->ext_base + offset);
}

static u32 rz_dmac_ext_readl(struct rz_dmac *dmac, unsigned int offset)
{
	return readl(dmac->ext_base + offset);
}

static void rz_dmac_ch_writel(struct rz_dmac_chan *channel, unsigned int val,
			      unsigned int offset, int which)
{
	if (which)
		writel(val, channel->ch_base + offset);
	else
		writel(val, channel->ch_cmn_base + offset);
}

static u32 rz_dmac_ch_readl(struct rz_dmac_chan *channel,
			    unsigned int offset, int which)
{
	if (which)
		return readl(channel->ch_base + offset);
	else
		return readl(channel->ch_cmn_base + offset);
}

/*
 * -----------------------------------------------------------------------------
 * Initialization
 */

static void rz_lmdesc_setup(struct rz_dmac_chan *channel,
			    struct rz_lmdesc *lmdesc)
{
	u32 nxla;

	channel->lmdesc.base = lmdesc;
	channel->lmdesc.head = lmdesc;
	channel->lmdesc.tail = lmdesc;
	nxla = channel->lmdesc.base_dma;
	while (lmdesc < (channel->lmdesc.base + (DMAC_NR_LMDESC - 1))) {
		lmdesc->header = 0;
		nxla += sizeof(*lmdesc);
		lmdesc->nxla = nxla;
		lmdesc++;
	}

	lmdesc->header = 0;
	lmdesc->nxla = channel->lmdesc.base_dma;
}

/*
 * -----------------------------------------------------------------------------
 * Descriptors preparation
 */

static void rz_dmac_lmdesc_recycle(struct rz_dmac_chan *channel)
{
	struct rz_lmdesc *lmdesc = channel->lmdesc.head;

	while (!(lmdesc->header & HEADER_LV)) {
		lmdesc->header = 0;
		lmdesc++;
		if (lmdesc >= (channel->lmdesc.base + DMAC_NR_LMDESC))
			lmdesc = channel->lmdesc.base;
	}
	channel->lmdesc.head = lmdesc;
}

static void rz_dmac_enable_hw(struct rz_dmac_chan *channel)
{
	struct dma_chan *chan = &channel->vc.chan;
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	unsigned long flags;
	u32 nxla;
	u32 chctrl;
	u32 chstat;

	dev_dbg(dmac->dev, "%s channel %d\n", __func__, channel->index);

	local_irq_save(flags);

	rz_dmac_lmdesc_recycle(channel);

	nxla = channel->lmdesc.base_dma +
		(sizeof(struct rz_lmdesc) * (channel->lmdesc.head -
					     channel->lmdesc.base));

	chstat = rz_dmac_ch_readl(channel, CHSTAT, 1);
	if (!(chstat & CHSTAT_EN)) {
		chctrl = (channel->chctrl | CHCTRL_SETEN);
		rz_dmac_ch_writel(channel, nxla, NXLA, 1);
		rz_dmac_ch_writel(channel, channel->chcfg, CHCFG, 1);
		rz_dmac_ch_writel(channel, CHCTRL_SWRST, CHCTRL, 1);
		rz_dmac_ch_writel(channel, chctrl, CHCTRL, 1);
	}

	local_irq_restore(flags);
}

static void rz_dmac_disable_hw(struct rz_dmac_chan *channel)
{
	struct dma_chan *chan = &channel->vc.chan;
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	unsigned long flags;

	dev_dbg(dmac->dev, "%s channel %d\n", __func__, channel->index);

	local_irq_save(flags);
	rz_dmac_ch_writel(channel, CHCTRL_DEFAULT, CHCTRL, 1);
	local_irq_restore(flags);
}

static void rz_dmac_set_dmars_register(struct rz_dmac *dmac, int nr, u32 dmars)
{
	u32 dmars_offset = (nr / 2) * 4;
	u32 shift = (nr % 2) * 16;
	u32 dmars32;

	dmars32 = rz_dmac_ext_readl(dmac, dmars_offset);
	dmars32 &= ~(0xffff << shift);
	dmars32 |= dmars << shift;

	rz_dmac_ext_writel(dmac, dmars32, dmars_offset);
}

static void rz_dmac_prepare_desc_for_memcpy(struct rz_dmac_chan *channel)
{
	struct dma_chan *chan = &channel->vc.chan;
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct rz_lmdesc *lmdesc = channel->lmdesc.tail;
	struct rz_dmac_desc *d = channel->desc;
	u32 chcfg = CHCFG_MEM_COPY;

	/* prepare descriptor */
	lmdesc->sa = d->src;
	lmdesc->da = d->dest;
	lmdesc->tb = d->len;
	lmdesc->chcfg = chcfg;
	lmdesc->chitvl = 0;
	lmdesc->chext = 0;
	lmdesc->header = HEADER_LV;

	rz_dmac_set_dmars_register(dmac, channel->index, 0);

	channel->chcfg = chcfg;
	channel->chctrl = CHCTRL_STG | CHCTRL_SETEN;
}

static void rz_dmac_prepare_descs_for_slave_sg(struct rz_dmac_chan *channel)
{
	struct dma_chan *chan = &channel->vc.chan;
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct rz_dmac_desc *d = channel->desc;
	struct scatterlist *sg, *sgl = d->sg;
	struct rz_lmdesc *lmdesc;
	unsigned int i, sg_len = d->sgcount;

	channel->chcfg |= CHCFG_SEL(channel->index) | CHCFG_DEM | CHCFG_DMS;

	if (d->direction == DMA_DEV_TO_MEM) {
		channel->chcfg |= CHCFG_SAD;
		channel->chcfg &= ~CHCFG_REQD;
	} else {
		channel->chcfg |= CHCFG_DAD | CHCFG_REQD;
	}

	lmdesc = channel->lmdesc.tail;

	for (i = 0, sg = sgl; i < sg_len; i++, sg = sg_next(sg)) {
		if (d->direction == DMA_DEV_TO_MEM) {
			lmdesc->sa = channel->src_per_address;
			lmdesc->da = sg_dma_address(sg);
		} else {
			lmdesc->sa = sg_dma_address(sg);
			lmdesc->da = channel->dst_per_address;
		}

		lmdesc->tb = sg_dma_len(sg);
		lmdesc->chitvl = 0;
		lmdesc->chext = 0;
		if (i == (sg_len - 1)) {
			lmdesc->chcfg = (channel->chcfg & ~CHCFG_DEM);
			lmdesc->header = HEADER_LV;
		} else {
			lmdesc->chcfg = channel->chcfg;
			lmdesc->header = HEADER_LV;
		}
		if (++lmdesc >= (channel->lmdesc.base + DMAC_NR_LMDESC))
			lmdesc = channel->lmdesc.base;
	}

	channel->lmdesc.tail = lmdesc;

	rz_dmac_set_dmars_register(dmac, channel->index, channel->mid_rid);
	channel->chctrl = CHCTRL_SETEN;
}

static int rz_dmac_xfer_desc(struct rz_dmac_chan *chan)
{
	struct rz_dmac_desc *d = chan->desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd)
		return 0;

	list_del(&vd->node);

	switch (d->type) {
	case RZ_DMAC_DESC_MEMCPY:
		rz_dmac_prepare_desc_for_memcpy(chan);
		break;

	case RZ_DMAC_DESC_SLAVE_SG:
		rz_dmac_prepare_descs_for_slave_sg(chan);
		break;

	default:
		return -EINVAL;
	}

	rz_dmac_enable_hw(chan);

	return 0;
}

/*
 * -----------------------------------------------------------------------------
 * DMA engine operations
 */

static int rz_dmac_alloc_chan_resources(struct dma_chan *chan)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);

	while (channel->descs_allocated < RZ_DMAC_MAX_CHAN_DESCRIPTORS) {
		struct rz_dmac_desc *desc;

		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc)
			break;

		list_add_tail(&desc->node, &channel->ld_free);
		channel->descs_allocated++;
	}

	if (!channel->descs_allocated)
		return -ENOMEM;

	return channel->descs_allocated;
}

static void rz_dmac_free_chan_resources(struct dma_chan *chan)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct rz_lmdesc *lmdesc = channel->lmdesc.base;
	struct rz_dmac_desc *desc, *_desc;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&channel->vc.lock, flags);

	for (i = 0; i < DMAC_NR_LMDESC; i++)
		lmdesc[i].header = 0;

	rz_dmac_disable_hw(channel);
	list_splice_tail_init(&channel->ld_active, &channel->ld_free);
	list_splice_tail_init(&channel->ld_queue, &channel->ld_free);

	if (channel->mid_rid >= 0) {
		clear_bit(channel->mid_rid, dmac->modules);
		channel->mid_rid = -EINVAL;
	}

	spin_unlock_irqrestore(&channel->vc.lock, flags);

	list_for_each_entry_safe(desc, _desc, &channel->ld_free, node) {
		kfree(desc);
		channel->descs_allocated--;
	}

	INIT_LIST_HEAD(&channel->ld_free);
	vchan_free_chan_resources(&channel->vc);
}

static struct dma_async_tx_descriptor *
rz_dmac_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
			size_t len, unsigned long flags)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct rz_dmac_desc *desc;

	dev_dbg(dmac->dev, "%s channel: %d src=0x%pad dst=0x%pad len=%zu\n",
		__func__, channel->index, &src, &dest, len);

	if (list_empty(&channel->ld_free))
		return NULL;

	desc = list_first_entry(&channel->ld_free, struct rz_dmac_desc, node);

	desc->type = RZ_DMAC_DESC_MEMCPY;
	desc->src = src;
	desc->dest = dest;
	desc->len = len;
	desc->direction = DMA_MEM_TO_MEM;

	list_move_tail(channel->ld_free.next, &channel->ld_queue);
	return vchan_tx_prep(&channel->vc, &desc->vd, flags);
}

static struct dma_async_tx_descriptor *
rz_dmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		      unsigned int sg_len,
		      enum dma_transfer_direction direction,
		      unsigned long flags, void *context)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac_desc *desc;
	struct scatterlist *sg;
	int dma_length = 0;
	int i = 0;

	if (list_empty(&channel->ld_free))
		return NULL;

	desc = list_first_entry(&channel->ld_free, struct rz_dmac_desc, node);

	for_each_sg(sgl, sg, sg_len, i) {
		dma_length += sg_dma_len(sg);
	}

	desc->type = RZ_DMAC_DESC_SLAVE_SG;
	desc->sg = sgl;
	desc->sgcount = sg_len;
	desc->len = dma_length;
	desc->direction = direction;

	if (direction == DMA_DEV_TO_MEM)
		desc->src = channel->src_per_address;
	else
		desc->dest = channel->dst_per_address;

	list_move_tail(channel->ld_free.next, &channel->ld_queue);
	return vchan_tx_prep(&channel->vc, &desc->vd, flags);
}

static int rz_dmac_terminate_all(struct dma_chan *chan)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	rz_dmac_disable_hw(channel);
	spin_lock_irqsave(&channel->vc.lock, flags);
	list_splice_tail_init(&channel->ld_active, &channel->ld_free);
	list_splice_tail_init(&channel->ld_queue, &channel->ld_free);
	spin_unlock_irqrestore(&channel->vc.lock, flags);
	vchan_get_all_descriptors(&channel->vc, &head);
	vchan_dma_desc_free_list(&channel->vc, &head);

	return 0;
}

static void rz_dmac_issue_pending(struct dma_chan *chan)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct rz_dmac_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&channel->vc.lock, flags);

	if (!list_empty(&channel->ld_queue)) {
		desc = list_first_entry(&channel->ld_queue,
					struct rz_dmac_desc, node);
		channel->desc = desc;
		if (vchan_issue_pending(&channel->vc)) {
			if (rz_dmac_xfer_desc(channel) < 0)
				dev_warn(dmac->dev, "ch: %d couldn't issue DMA xfer\n",
					 channel->index);
			else
				list_move_tail(channel->ld_queue.next,
					       &channel->ld_active);
		}
	}

	spin_unlock_irqrestore(&channel->vc.lock, flags);
}

static u8 rz_dmac_ds_to_val_mapping(enum dma_slave_buswidth ds)
{
	u8 i;
	static const enum dma_slave_buswidth ds_lut[] = {
		DMA_SLAVE_BUSWIDTH_1_BYTE,
		DMA_SLAVE_BUSWIDTH_2_BYTES,
		DMA_SLAVE_BUSWIDTH_4_BYTES,
		DMA_SLAVE_BUSWIDTH_8_BYTES,
		DMA_SLAVE_BUSWIDTH_16_BYTES,
		DMA_SLAVE_BUSWIDTH_32_BYTES,
		DMA_SLAVE_BUSWIDTH_64_BYTES,
		DMA_SLAVE_BUSWIDTH_128_BYTES,
	};

	for (i = 0; i < ARRAY_SIZE(ds_lut); i++) {
		if (ds_lut[i] == ds)
			return i;
	}

	return CHCFG_DS_INVALID;
}

static int rz_dmac_config(struct dma_chan *chan,
			  struct dma_slave_config *config)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	u32 val;

	channel->src_per_address = config->src_addr;
	channel->dst_per_address = config->dst_addr;

	val = rz_dmac_ds_to_val_mapping(config->dst_addr_width);
	if (val == CHCFG_DS_INVALID)
		return -EINVAL;

	channel->chcfg |= CHCFG_FILL_DDS(val);

	val = rz_dmac_ds_to_val_mapping(config->src_addr_width);
	if (val == CHCFG_DS_INVALID)
		return -EINVAL;

	channel->chcfg |= CHCFG_FILL_SDS(val);

	return 0;
}

static void rz_dmac_virt_desc_free(struct virt_dma_desc *vd)
{
	/*
	 * Place holder
	 * Descriptor allocation is done during alloc_chan_resources and
	 * get freed during free_chan_resources.
	 * list is used to manage the descriptors and avoid any memory
	 * allocation/free during DMA read/write.
	 */
}

static void rz_dmac_device_synchronize(struct dma_chan *chan)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	u32 chstat;
	int ret;

	ret = read_poll_timeout(rz_dmac_ch_readl, chstat, !(chstat & CHSTAT_EN),
				100, 100000, false, channel, CHSTAT, 1);
	if (ret < 0)
		dev_warn(dmac->dev, "DMA Timeout");

	rz_dmac_set_dmars_register(dmac, channel->index, 0);
}

/*
 * -----------------------------------------------------------------------------
 * IRQ handling
 */

static void rz_dmac_irq_handle_channel(struct rz_dmac_chan *channel)
{
	struct dma_chan *chan = &channel->vc.chan;
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	u32 chstat, chctrl;

	chstat = rz_dmac_ch_readl(channel, CHSTAT, 1);
	if (chstat & CHSTAT_ER) {
		dev_err(dmac->dev, "DMAC err CHSTAT_%d = %08X\n",
			channel->index, chstat);
		rz_dmac_ch_writel(channel, CHCTRL_DEFAULT, CHCTRL, 1);
		goto done;
	}

	chctrl = rz_dmac_ch_readl(channel, CHCTRL, 1);
	rz_dmac_ch_writel(channel, chctrl | CHCTRL_CLREND, CHCTRL, 1);
done:
	return;
}

static irqreturn_t rz_dmac_irq_handler(int irq, void *dev_id)
{
	struct rz_dmac_chan *channel = dev_id;

	if (channel) {
		rz_dmac_irq_handle_channel(channel);
		return IRQ_WAKE_THREAD;
	}
	/* handle DMAERR irq */
	return IRQ_HANDLED;
}

static irqreturn_t rz_dmac_irq_handler_thread(int irq, void *dev_id)
{
	struct rz_dmac_chan *channel = dev_id;
	struct rz_dmac_desc *desc = NULL;
	unsigned long flags;

	spin_lock_irqsave(&channel->vc.lock, flags);

	if (list_empty(&channel->ld_active)) {
		/* Someone might have called terminate all */
		goto out;
	}

	desc = list_first_entry(&channel->ld_active, struct rz_dmac_desc, node);
	vchan_cookie_complete(&desc->vd);
	list_move_tail(channel->ld_active.next, &channel->ld_free);
	if (!list_empty(&channel->ld_queue)) {
		desc = list_first_entry(&channel->ld_queue, struct rz_dmac_desc,
					node);
		channel->desc = desc;
		if (rz_dmac_xfer_desc(channel) == 0)
			list_move_tail(channel->ld_queue.next, &channel->ld_active);
	}
out:
	spin_unlock_irqrestore(&channel->vc.lock, flags);

	return IRQ_HANDLED;
}

/*
 * -----------------------------------------------------------------------------
 * OF xlate and channel filter
 */

static bool rz_dmac_chan_filter(struct dma_chan *chan, void *arg)
{
	struct rz_dmac_chan *channel = to_rz_dmac_chan(chan);
	struct rz_dmac *dmac = to_rz_dmac(chan->device);
	struct of_phandle_args *dma_spec = arg;
	u32 ch_cfg;

	channel->mid_rid = dma_spec->args[0] & MID_RID_MASK;
	ch_cfg = (dma_spec->args[0] & CHCFG_MASK) >> 10;
	channel->chcfg = CHCFG_FILL_TM(ch_cfg) | CHCFG_FILL_AM(ch_cfg) |
			 CHCFG_FILL_LVL(ch_cfg) | CHCFG_FILL_HIEN(ch_cfg);

	return !test_and_set_bit(channel->mid_rid, dmac->modules);
}

static struct dma_chan *rz_dmac_of_xlate(struct of_phandle_args *dma_spec,
					 struct of_dma *ofdma)
{
	dma_cap_mask_t mask;

	if (dma_spec->args_count != 1)
		return NULL;

	/* Only slave DMA channels can be allocated via DT */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	return dma_request_channel(mask, rz_dmac_chan_filter, dma_spec);
}

/*
 * -----------------------------------------------------------------------------
 * Probe and remove
 */

static int rz_dmac_chan_probe(struct rz_dmac *dmac,
			      struct rz_dmac_chan *channel,
			      unsigned int index)
{
	struct platform_device *pdev = to_platform_device(dmac->dev);
	struct rz_lmdesc *lmdesc;
	char pdev_irqname[5];
	char *irqname;
	int ret;

	channel->index = index;
	channel->mid_rid = -EINVAL;

	/* Request the channel interrupt. */
	sprintf(pdev_irqname, "ch%u", index);
	channel->irq = platform_get_irq_byname(pdev, pdev_irqname);
	if (channel->irq < 0)
		return channel->irq;

	irqname = devm_kasprintf(dmac->dev, GFP_KERNEL, "%s:%u",
				 dev_name(dmac->dev), index);
	if (!irqname)
		return -ENOMEM;

	ret = devm_request_threaded_irq(dmac->dev, channel->irq,
					rz_dmac_irq_handler,
					rz_dmac_irq_handler_thread, 0,
					irqname, channel);
	if (ret) {
		dev_err(dmac->dev, "failed to request IRQ %u (%d)\n",
			channel->irq, ret);
		return ret;
	}

	/* Set io base address for each channel */
	if (index < 8) {
		channel->ch_base = dmac->base + CHANNEL_0_7_OFFSET +
			EACH_CHANNEL_OFFSET * index;
		channel->ch_cmn_base = dmac->base + CHANNEL_0_7_COMMON_BASE;
	} else {
		channel->ch_base = dmac->base + CHANNEL_8_15_OFFSET +
			EACH_CHANNEL_OFFSET * (index - 8);
		channel->ch_cmn_base = dmac->base + CHANNEL_8_15_COMMON_BASE;
	}

	/* Allocate descriptors */
	lmdesc = dma_alloc_coherent(&pdev->dev,
				    sizeof(struct rz_lmdesc) * DMAC_NR_LMDESC,
				    &channel->lmdesc.base_dma, GFP_KERNEL);
	if (!lmdesc) {
		dev_err(&pdev->dev, "Can't allocate memory (lmdesc)\n");
		return -ENOMEM;
	}
	rz_lmdesc_setup(channel, lmdesc);

	/* Initialize register for each channel */
	rz_dmac_ch_writel(channel, CHCTRL_DEFAULT, CHCTRL, 1);

	channel->vc.desc_free = rz_dmac_virt_desc_free;
	vchan_init(&channel->vc, &dmac->engine);
	INIT_LIST_HEAD(&channel->ld_queue);
	INIT_LIST_HEAD(&channel->ld_free);
	INIT_LIST_HEAD(&channel->ld_active);

	return 0;
}

static int rz_dmac_parse_of(struct device *dev, struct rz_dmac *dmac)
{
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "dma-channels", &dmac->n_channels);
	if (ret < 0) {
		dev_err(dev, "unable to read dma-channels property\n");
		return ret;
	}

	if (!dmac->n_channels || dmac->n_channels > RZ_DMAC_MAX_CHANNELS) {
		dev_err(dev, "invalid number of channels %u\n", dmac->n_channels);
		return -EINVAL;
	}

	return 0;
}

static int rz_dmac_probe(struct platform_device *pdev)
{
	const char *irqname = "error";
	struct dma_device *engine;
	struct rz_dmac *dmac;
	int channel_num;
	unsigned int i;
	int ret;
	int irq;

	dmac = devm_kzalloc(&pdev->dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac)
		return -ENOMEM;

	dmac->dev = &pdev->dev;
	platform_set_drvdata(pdev, dmac);

	ret = rz_dmac_parse_of(&pdev->dev, dmac);
	if (ret < 0)
		return ret;

	dmac->channels = devm_kcalloc(&pdev->dev, dmac->n_channels,
				      sizeof(*dmac->channels), GFP_KERNEL);
	if (!dmac->channels)
		return -ENOMEM;

	/* Request resources */
	dmac->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dmac->base))
		return PTR_ERR(dmac->base);

	dmac->ext_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dmac->ext_base))
		return PTR_ERR(dmac->ext_base);

	/* Register interrupt handler for error */
	irq = platform_get_irq_byname(pdev, irqname);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rz_dmac_irq_handler, 0,
			       irqname, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %u (%d)\n",
			irq, ret);
		return ret;
	}

	/* Initialize the channels. */
	INIT_LIST_HEAD(&dmac->engine.channels);

	dmac->rstc = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(dmac->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(dmac->rstc),
				     "failed to get resets\n");

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_resume_and_get failed\n");
		goto err_pm_disable;
	}

	ret = reset_control_deassert(dmac->rstc);
	if (ret)
		goto err_pm_runtime_put;

	for (i = 0; i < dmac->n_channels; i++) {
		ret = rz_dmac_chan_probe(dmac, &dmac->channels[i], i);
		if (ret < 0)
			goto err;
	}

	/* Register the DMAC as a DMA provider for DT. */
	ret = of_dma_controller_register(pdev->dev.of_node, rz_dmac_of_xlate,
					 NULL);
	if (ret < 0)
		goto err;

	/* Register the DMA engine device. */
	engine = &dmac->engine;
	dma_cap_set(DMA_SLAVE, engine->cap_mask);
	dma_cap_set(DMA_MEMCPY, engine->cap_mask);
	rz_dmac_writel(dmac, DCTRL_DEFAULT, CHANNEL_0_7_COMMON_BASE + DCTRL);
	rz_dmac_writel(dmac, DCTRL_DEFAULT, CHANNEL_8_15_COMMON_BASE + DCTRL);

	engine->dev = &pdev->dev;

	engine->device_alloc_chan_resources = rz_dmac_alloc_chan_resources;
	engine->device_free_chan_resources = rz_dmac_free_chan_resources;
	engine->device_tx_status = dma_cookie_status;
	engine->device_prep_slave_sg = rz_dmac_prep_slave_sg;
	engine->device_prep_dma_memcpy = rz_dmac_prep_dma_memcpy;
	engine->device_config = rz_dmac_config;
	engine->device_terminate_all = rz_dmac_terminate_all;
	engine->device_issue_pending = rz_dmac_issue_pending;
	engine->device_synchronize = rz_dmac_device_synchronize;

	engine->copy_align = DMAENGINE_ALIGN_1_BYTE;
	dma_set_max_seg_size(engine->dev, U32_MAX);

	ret = dma_async_device_register(engine);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register\n");
		goto dma_register_err;
	}
	return 0;

dma_register_err:
	of_dma_controller_free(pdev->dev.of_node);
err:
	reset_control_assert(dmac->rstc);
	channel_num = i ? i - 1 : 0;
	for (i = 0; i < channel_num; i++) {
		struct rz_dmac_chan *channel = &dmac->channels[i];

		dma_free_coherent(&pdev->dev,
				  sizeof(struct rz_lmdesc) * DMAC_NR_LMDESC,
				  channel->lmdesc.base,
				  channel->lmdesc.base_dma);
	}

err_pm_runtime_put:
	pm_runtime_put(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rz_dmac_remove(struct platform_device *pdev)
{
	struct rz_dmac *dmac = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < dmac->n_channels; i++) {
		struct rz_dmac_chan *channel = &dmac->channels[i];

		dma_free_coherent(&pdev->dev,
				  sizeof(struct rz_lmdesc) * DMAC_NR_LMDESC,
				  channel->lmdesc.base,
				  channel->lmdesc.base_dma);
	}
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&dmac->engine);
	reset_control_assert(dmac->rstc);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id of_rz_dmac_match[] = {
	{ .compatible = "renesas,rz-dmac", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_rz_dmac_match);

static struct platform_driver rz_dmac_driver = {
	.driver		= {
		.name	= "rz-dmac",
		.of_match_table = of_rz_dmac_match,
	},
	.probe		= rz_dmac_probe,
	.remove		= rz_dmac_remove,
};

module_platform_driver(rz_dmac_driver);

MODULE_DESCRIPTION("Renesas RZ/G2L DMA Controller Driver");
MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_LICENSE("GPL v2");
