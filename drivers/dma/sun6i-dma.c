// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2014 Allwinner Tech Co., Ltd
 * Author: Sugar <shuge@allwinnertech.com>
 *
 * Copyright (C) 2014 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "virt-dma.h"

/*
 * Common registers
 */
#define DMA_IRQ_EN(x)		((x) * 0x04)
#define DMA_IRQ_HALF			BIT(0)
#define DMA_IRQ_PKG			BIT(1)
#define DMA_IRQ_QUEUE			BIT(2)

#define DMA_IRQ_CHAN_NR			8
#define DMA_IRQ_CHAN_WIDTH		4


#define DMA_IRQ_STAT(x)		((x) * 0x04 + 0x10)

#define DMA_STAT		0x30

/* Offset between DMA_IRQ_EN and DMA_IRQ_STAT limits number of channels */
#define DMA_MAX_CHANNELS	(DMA_IRQ_CHAN_NR * 0x10 / 4)

/*
 * sun8i specific registers
 */
#define SUN8I_DMA_GATE		0x20
#define SUN8I_DMA_GATE_ENABLE	0x4

#define SUNXI_H3_SECURE_REG		0x20
#define SUNXI_H3_DMA_GATE		0x28
#define SUNXI_H3_DMA_GATE_ENABLE	0x4
/*
 * Channels specific registers
 */
#define DMA_CHAN_ENABLE		0x00
#define DMA_CHAN_ENABLE_START		BIT(0)
#define DMA_CHAN_ENABLE_STOP		0

#define DMA_CHAN_PAUSE		0x04
#define DMA_CHAN_PAUSE_PAUSE		BIT(1)
#define DMA_CHAN_PAUSE_RESUME		0

#define DMA_CHAN_LLI_ADDR	0x08

#define DMA_CHAN_CUR_CFG	0x0c
#define DMA_CHAN_MAX_DRQ		0x1f
#define DMA_CHAN_CFG_SRC_DRQ(x)		((x) & DMA_CHAN_MAX_DRQ)
#define DMA_CHAN_CFG_SRC_IO_MODE	BIT(5)
#define DMA_CHAN_CFG_SRC_LINEAR_MODE	(0 << 5)
#define DMA_CHAN_CFG_SRC_BURST_A31(x)	(((x) & 0x3) << 7)
#define DMA_CHAN_CFG_SRC_BURST_H3(x)	(((x) & 0x3) << 6)
#define DMA_CHAN_CFG_SRC_WIDTH(x)	(((x) & 0x3) << 9)

#define DMA_CHAN_CFG_DST_DRQ(x)		(DMA_CHAN_CFG_SRC_DRQ(x) << 16)
#define DMA_CHAN_CFG_DST_IO_MODE	(DMA_CHAN_CFG_SRC_IO_MODE << 16)
#define DMA_CHAN_CFG_DST_LINEAR_MODE	(DMA_CHAN_CFG_SRC_LINEAR_MODE << 16)
#define DMA_CHAN_CFG_DST_BURST_A31(x)	(DMA_CHAN_CFG_SRC_BURST_A31(x) << 16)
#define DMA_CHAN_CFG_DST_BURST_H3(x)	(DMA_CHAN_CFG_SRC_BURST_H3(x) << 16)
#define DMA_CHAN_CFG_DST_WIDTH(x)	(DMA_CHAN_CFG_SRC_WIDTH(x) << 16)

#define DMA_CHAN_CUR_SRC	0x10

#define DMA_CHAN_CUR_DST	0x14

#define DMA_CHAN_CUR_CNT	0x18

#define DMA_CHAN_CUR_PARA	0x1c


/*
 * Various hardware related defines
 */
#define LLI_LAST_ITEM	0xfffff800
#define NORMAL_WAIT	8
#define DRQ_SDRAM	1

/* forward declaration */
struct sun6i_dma_dev;

/*
 * Hardware channels / ports representation
 *
 * The hardware is used in several SoCs, with differing numbers
 * of channels and endpoints. This structure ties those numbers
 * to a certain compatible string.
 */
struct sun6i_dma_config {
	u32 nr_max_channels;
	u32 nr_max_requests;
	u32 nr_max_vchans;
	/*
	 * In the datasheets/user manuals of newer Allwinner SoCs, a special
	 * bit (bit 2 at register 0x20) is present.
	 * It's named "DMA MCLK interface circuit auto gating bit" in the
	 * documents, and the footnote of this register says that this bit
	 * should be set up when initializing the DMA controller.
	 * Allwinner A23/A33 user manuals do not have this bit documented,
	 * however these SoCs really have and need this bit, as seen in the
	 * BSP kernel source code.
	 */
	void (*clock_autogate_enable)(struct sun6i_dma_dev *);
	void (*set_burst_length)(u32 *p_cfg, s8 src_burst, s8 dst_burst);
	u32 src_burst_lengths;
	u32 dst_burst_lengths;
	u32 src_addr_widths;
	u32 dst_addr_widths;
};

/*
 * Hardware representation of the LLI
 *
 * The hardware will be fed the physical address of this structure,
 * and read its content in order to start the transfer.
 */
struct sun6i_dma_lli {
	u32			cfg;
	u32			src;
	u32			dst;
	u32			len;
	u32			para;
	u32			p_lli_next;

	/*
	 * This field is not used by the DMA controller, but will be
	 * used by the CPU to go through the list (mostly for dumping
	 * or freeing it).
	 */
	struct sun6i_dma_lli	*v_lli_next;
};


struct sun6i_desc {
	struct virt_dma_desc	vd;
	dma_addr_t		p_lli;
	struct sun6i_dma_lli	*v_lli;
};

struct sun6i_pchan {
	u32			idx;
	void __iomem		*base;
	struct sun6i_vchan	*vchan;
	struct sun6i_desc	*desc;
	struct sun6i_desc	*done;
};

struct sun6i_vchan {
	struct virt_dma_chan	vc;
	struct list_head	node;
	struct dma_slave_config	cfg;
	struct sun6i_pchan	*phy;
	u8			port;
	u8			irq_type;
	bool			cyclic;
};

struct sun6i_dma_dev {
	struct dma_device	slave;
	void __iomem		*base;
	struct clk		*clk;
	int			irq;
	spinlock_t		lock;
	struct reset_control	*rstc;
	struct tasklet_struct	task;
	atomic_t		tasklet_shutdown;
	struct list_head	pending;
	struct dma_pool		*pool;
	struct sun6i_pchan	*pchans;
	struct sun6i_vchan	*vchans;
	const struct sun6i_dma_config *cfg;
	u32			num_pchans;
	u32			num_vchans;
	u32			max_request;
};

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct sun6i_dma_dev *to_sun6i_dma_dev(struct dma_device *d)
{
	return container_of(d, struct sun6i_dma_dev, slave);
}

static inline struct sun6i_vchan *to_sun6i_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct sun6i_vchan, vc.chan);
}

static inline struct sun6i_desc *
to_sun6i_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct sun6i_desc, vd.tx);
}

static inline void sun6i_dma_dump_com_regs(struct sun6i_dma_dev *sdev)
{
	dev_dbg(sdev->slave.dev, "Common register:\n"
		"\tmask0(%04x): 0x%08x\n"
		"\tmask1(%04x): 0x%08x\n"
		"\tpend0(%04x): 0x%08x\n"
		"\tpend1(%04x): 0x%08x\n"
		"\tstats(%04x): 0x%08x\n",
		DMA_IRQ_EN(0), readl(sdev->base + DMA_IRQ_EN(0)),
		DMA_IRQ_EN(1), readl(sdev->base + DMA_IRQ_EN(1)),
		DMA_IRQ_STAT(0), readl(sdev->base + DMA_IRQ_STAT(0)),
		DMA_IRQ_STAT(1), readl(sdev->base + DMA_IRQ_STAT(1)),
		DMA_STAT, readl(sdev->base + DMA_STAT));
}

static inline void sun6i_dma_dump_chan_regs(struct sun6i_dma_dev *sdev,
					    struct sun6i_pchan *pchan)
{
	phys_addr_t reg = virt_to_phys(pchan->base);

	dev_dbg(sdev->slave.dev, "Chan %d reg: %pa\n"
		"\t___en(%04x): \t0x%08x\n"
		"\tpause(%04x): \t0x%08x\n"
		"\tstart(%04x): \t0x%08x\n"
		"\t__cfg(%04x): \t0x%08x\n"
		"\t__src(%04x): \t0x%08x\n"
		"\t__dst(%04x): \t0x%08x\n"
		"\tcount(%04x): \t0x%08x\n"
		"\t_para(%04x): \t0x%08x\n\n",
		pchan->idx, &reg,
		DMA_CHAN_ENABLE,
		readl(pchan->base + DMA_CHAN_ENABLE),
		DMA_CHAN_PAUSE,
		readl(pchan->base + DMA_CHAN_PAUSE),
		DMA_CHAN_LLI_ADDR,
		readl(pchan->base + DMA_CHAN_LLI_ADDR),
		DMA_CHAN_CUR_CFG,
		readl(pchan->base + DMA_CHAN_CUR_CFG),
		DMA_CHAN_CUR_SRC,
		readl(pchan->base + DMA_CHAN_CUR_SRC),
		DMA_CHAN_CUR_DST,
		readl(pchan->base + DMA_CHAN_CUR_DST),
		DMA_CHAN_CUR_CNT,
		readl(pchan->base + DMA_CHAN_CUR_CNT),
		DMA_CHAN_CUR_PARA,
		readl(pchan->base + DMA_CHAN_CUR_PARA));
}

static inline s8 convert_burst(u32 maxburst)
{
	switch (maxburst) {
	case 1:
		return 0;
	case 4:
		return 1;
	case 8:
		return 2;
	case 16:
		return 3;
	default:
		return -EINVAL;
	}
}

static inline s8 convert_buswidth(enum dma_slave_buswidth addr_width)
{
	return ilog2(addr_width);
}

static void sun6i_enable_clock_autogate_a23(struct sun6i_dma_dev *sdev)
{
	writel(SUN8I_DMA_GATE_ENABLE, sdev->base + SUN8I_DMA_GATE);
}

static void sun6i_enable_clock_autogate_h3(struct sun6i_dma_dev *sdev)
{
	writel(SUNXI_H3_DMA_GATE_ENABLE, sdev->base + SUNXI_H3_DMA_GATE);
}

static void sun6i_set_burst_length_a31(u32 *p_cfg, s8 src_burst, s8 dst_burst)
{
	*p_cfg |= DMA_CHAN_CFG_SRC_BURST_A31(src_burst) |
		  DMA_CHAN_CFG_DST_BURST_A31(dst_burst);
}

static void sun6i_set_burst_length_h3(u32 *p_cfg, s8 src_burst, s8 dst_burst)
{
	*p_cfg |= DMA_CHAN_CFG_SRC_BURST_H3(src_burst) |
		  DMA_CHAN_CFG_DST_BURST_H3(dst_burst);
}

static size_t sun6i_get_chan_size(struct sun6i_pchan *pchan)
{
	struct sun6i_desc *txd = pchan->desc;
	struct sun6i_dma_lli *lli;
	size_t bytes;
	dma_addr_t pos;

	pos = readl(pchan->base + DMA_CHAN_LLI_ADDR);
	bytes = readl(pchan->base + DMA_CHAN_CUR_CNT);

	if (pos == LLI_LAST_ITEM)
		return bytes;

	for (lli = txd->v_lli; lli; lli = lli->v_lli_next) {
		if (lli->p_lli_next == pos) {
			for (lli = lli->v_lli_next; lli; lli = lli->v_lli_next)
				bytes += lli->len;
			break;
		}
	}

	return bytes;
}

static void *sun6i_dma_lli_add(struct sun6i_dma_lli *prev,
			       struct sun6i_dma_lli *next,
			       dma_addr_t next_phy,
			       struct sun6i_desc *txd)
{
	if ((!prev && !txd) || !next)
		return NULL;

	if (!prev) {
		txd->p_lli = next_phy;
		txd->v_lli = next;
	} else {
		prev->p_lli_next = next_phy;
		prev->v_lli_next = next;
	}

	next->p_lli_next = LLI_LAST_ITEM;
	next->v_lli_next = NULL;

	return next;
}

static inline void sun6i_dma_dump_lli(struct sun6i_vchan *vchan,
				      struct sun6i_dma_lli *lli)
{
	phys_addr_t p_lli = virt_to_phys(lli);

	dev_dbg(chan2dev(&vchan->vc.chan),
		"\n\tdesc:   p - %pa v - 0x%p\n"
		"\t\tc - 0x%08x s - 0x%08x d - 0x%08x\n"
		"\t\tl - 0x%08x p - 0x%08x n - 0x%08x\n",
		&p_lli, lli,
		lli->cfg, lli->src, lli->dst,
		lli->len, lli->para, lli->p_lli_next);
}

static void sun6i_dma_free_desc(struct virt_dma_desc *vd)
{
	struct sun6i_desc *txd = to_sun6i_desc(&vd->tx);
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(vd->tx.chan->device);
	struct sun6i_dma_lli *v_lli, *v_next;
	dma_addr_t p_lli, p_next;

	if (unlikely(!txd))
		return;

	p_lli = txd->p_lli;
	v_lli = txd->v_lli;

	while (v_lli) {
		v_next = v_lli->v_lli_next;
		p_next = v_lli->p_lli_next;

		dma_pool_free(sdev->pool, v_lli, p_lli);

		v_lli = v_next;
		p_lli = p_next;
	}

	kfree(txd);
}

static int sun6i_dma_start_desc(struct sun6i_vchan *vchan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(vchan->vc.chan.device);
	struct virt_dma_desc *desc = vchan_next_desc(&vchan->vc);
	struct sun6i_pchan *pchan = vchan->phy;
	u32 irq_val, irq_reg, irq_offset;

	if (!pchan)
		return -EAGAIN;

	if (!desc) {
		pchan->desc = NULL;
		pchan->done = NULL;
		return -EAGAIN;
	}

	list_del(&desc->node);

	pchan->desc = to_sun6i_desc(&desc->tx);
	pchan->done = NULL;

	sun6i_dma_dump_lli(vchan, pchan->desc->v_lli);

	irq_reg = pchan->idx / DMA_IRQ_CHAN_NR;
	irq_offset = pchan->idx % DMA_IRQ_CHAN_NR;

	vchan->irq_type = vchan->cyclic ? DMA_IRQ_PKG : DMA_IRQ_QUEUE;

	irq_val = readl(sdev->base + DMA_IRQ_EN(irq_reg));
	irq_val &= ~((DMA_IRQ_HALF | DMA_IRQ_PKG | DMA_IRQ_QUEUE) <<
			(irq_offset * DMA_IRQ_CHAN_WIDTH));
	irq_val |= vchan->irq_type << (irq_offset * DMA_IRQ_CHAN_WIDTH);
	writel(irq_val, sdev->base + DMA_IRQ_EN(irq_reg));

	writel(pchan->desc->p_lli, pchan->base + DMA_CHAN_LLI_ADDR);
	writel(DMA_CHAN_ENABLE_START, pchan->base + DMA_CHAN_ENABLE);

	sun6i_dma_dump_com_regs(sdev);
	sun6i_dma_dump_chan_regs(sdev, pchan);

	return 0;
}

static void sun6i_dma_tasklet(unsigned long data)
{
	struct sun6i_dma_dev *sdev = (struct sun6i_dma_dev *)data;
	struct sun6i_vchan *vchan;
	struct sun6i_pchan *pchan;
	unsigned int pchan_alloc = 0;
	unsigned int pchan_idx;

	list_for_each_entry(vchan, &sdev->slave.channels, vc.chan.device_node) {
		spin_lock_irq(&vchan->vc.lock);

		pchan = vchan->phy;

		if (pchan && pchan->done) {
			if (sun6i_dma_start_desc(vchan)) {
				/*
				 * No current txd associated with this channel
				 */
				dev_dbg(sdev->slave.dev, "pchan %u: free\n",
					pchan->idx);

				/* Mark this channel free */
				vchan->phy = NULL;
				pchan->vchan = NULL;
			}
		}
		spin_unlock_irq(&vchan->vc.lock);
	}

	spin_lock_irq(&sdev->lock);
	for (pchan_idx = 0; pchan_idx < sdev->num_pchans; pchan_idx++) {
		pchan = &sdev->pchans[pchan_idx];

		if (pchan->vchan || list_empty(&sdev->pending))
			continue;

		vchan = list_first_entry(&sdev->pending,
					 struct sun6i_vchan, node);

		/* Remove from pending channels */
		list_del_init(&vchan->node);
		pchan_alloc |= BIT(pchan_idx);

		/* Mark this channel allocated */
		pchan->vchan = vchan;
		vchan->phy = pchan;
		dev_dbg(sdev->slave.dev, "pchan %u: alloc vchan %p\n",
			pchan->idx, &vchan->vc);
	}
	spin_unlock_irq(&sdev->lock);

	for (pchan_idx = 0; pchan_idx < sdev->num_pchans; pchan_idx++) {
		if (!(pchan_alloc & BIT(pchan_idx)))
			continue;

		pchan = sdev->pchans + pchan_idx;
		vchan = pchan->vchan;
		if (vchan) {
			spin_lock_irq(&vchan->vc.lock);
			sun6i_dma_start_desc(vchan);
			spin_unlock_irq(&vchan->vc.lock);
		}
	}
}

static irqreturn_t sun6i_dma_interrupt(int irq, void *dev_id)
{
	struct sun6i_dma_dev *sdev = dev_id;
	struct sun6i_vchan *vchan;
	struct sun6i_pchan *pchan;
	int i, j, ret = IRQ_NONE;
	u32 status;

	for (i = 0; i < sdev->num_pchans / DMA_IRQ_CHAN_NR; i++) {
		status = readl(sdev->base + DMA_IRQ_STAT(i));
		if (!status)
			continue;

		dev_dbg(sdev->slave.dev, "DMA irq status %s: 0x%x\n",
			i ? "high" : "low", status);

		writel(status, sdev->base + DMA_IRQ_STAT(i));

		for (j = 0; (j < DMA_IRQ_CHAN_NR) && status; j++) {
			pchan = sdev->pchans + j;
			vchan = pchan->vchan;
			if (vchan && (status & vchan->irq_type)) {
				if (vchan->cyclic) {
					vchan_cyclic_callback(&pchan->desc->vd);
				} else {
					spin_lock(&vchan->vc.lock);
					vchan_cookie_complete(&pchan->desc->vd);
					pchan->done = pchan->desc;
					spin_unlock(&vchan->vc.lock);
				}
			}

			status = status >> DMA_IRQ_CHAN_WIDTH;
		}

		if (!atomic_read(&sdev->tasklet_shutdown))
			tasklet_schedule(&sdev->task);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int set_config(struct sun6i_dma_dev *sdev,
			struct dma_slave_config *sconfig,
			enum dma_transfer_direction direction,
			u32 *p_cfg)
{
	enum dma_slave_buswidth src_addr_width, dst_addr_width;
	u32 src_maxburst, dst_maxburst;
	s8 src_width, dst_width, src_burst, dst_burst;

	src_addr_width = sconfig->src_addr_width;
	dst_addr_width = sconfig->dst_addr_width;
	src_maxburst = sconfig->src_maxburst;
	dst_maxburst = sconfig->dst_maxburst;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		if (src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		src_maxburst = src_maxburst ? src_maxburst : 8;
		break;
	case DMA_DEV_TO_MEM:
		if (dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dst_maxburst = dst_maxburst ? dst_maxburst : 8;
		break;
	default:
		return -EINVAL;
	}

	if (!(BIT(src_addr_width) & sdev->slave.src_addr_widths))
		return -EINVAL;
	if (!(BIT(dst_addr_width) & sdev->slave.dst_addr_widths))
		return -EINVAL;
	if (!(BIT(src_maxburst) & sdev->cfg->src_burst_lengths))
		return -EINVAL;
	if (!(BIT(dst_maxburst) & sdev->cfg->dst_burst_lengths))
		return -EINVAL;

	src_width = convert_buswidth(src_addr_width);
	dst_width = convert_buswidth(dst_addr_width);
	dst_burst = convert_burst(dst_maxburst);
	src_burst = convert_burst(src_maxburst);

	*p_cfg = DMA_CHAN_CFG_SRC_WIDTH(src_width) |
		DMA_CHAN_CFG_DST_WIDTH(dst_width);

	sdev->cfg->set_burst_length(p_cfg, src_burst, dst_burst);

	return 0;
}

static struct dma_async_tx_descriptor *sun6i_dma_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct sun6i_dma_lli *v_lli;
	struct sun6i_desc *txd;
	dma_addr_t p_lli;
	s8 burst, width;

	dev_dbg(chan2dev(chan),
		"%s; chan: %d, dest: %pad, src: %pad, len: %zu. flags: 0x%08lx\n",
		__func__, vchan->vc.chan.chan_id, &dest, &src, len, flags);

	if (!len)
		return NULL;

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	v_lli = dma_pool_alloc(sdev->pool, GFP_NOWAIT, &p_lli);
	if (!v_lli) {
		dev_err(sdev->slave.dev, "Failed to alloc lli memory\n");
		goto err_txd_free;
	}

	v_lli->src = src;
	v_lli->dst = dest;
	v_lli->len = len;
	v_lli->para = NORMAL_WAIT;

	burst = convert_burst(8);
	width = convert_buswidth(DMA_SLAVE_BUSWIDTH_4_BYTES);
	v_lli->cfg = DMA_CHAN_CFG_SRC_DRQ(DRQ_SDRAM) |
		DMA_CHAN_CFG_DST_DRQ(DRQ_SDRAM) |
		DMA_CHAN_CFG_DST_LINEAR_MODE |
		DMA_CHAN_CFG_SRC_LINEAR_MODE |
		DMA_CHAN_CFG_SRC_WIDTH(width) |
		DMA_CHAN_CFG_DST_WIDTH(width);

	sdev->cfg->set_burst_length(&v_lli->cfg, burst, burst);

	sun6i_dma_lli_add(NULL, v_lli, p_lli, txd);

	sun6i_dma_dump_lli(vchan, v_lli);

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	kfree(txd);
	return NULL;
}

static struct dma_async_tx_descriptor *sun6i_dma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction dir,
		unsigned long flags, void *context)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct sun6i_dma_lli *v_lli, *prev = NULL;
	struct sun6i_desc *txd;
	struct scatterlist *sg;
	dma_addr_t p_lli;
	u32 lli_cfg;
	int i, ret;

	if (!sgl)
		return NULL;

	ret = set_config(sdev, sconfig, dir, &lli_cfg);
	if (ret) {
		dev_err(chan2dev(chan), "Invalid DMA configuration\n");
		return NULL;
	}

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		v_lli = dma_pool_alloc(sdev->pool, GFP_NOWAIT, &p_lli);
		if (!v_lli)
			goto err_lli_free;

		v_lli->len = sg_dma_len(sg);
		v_lli->para = NORMAL_WAIT;

		if (dir == DMA_MEM_TO_DEV) {
			v_lli->src = sg_dma_address(sg);
			v_lli->dst = sconfig->dst_addr;
			v_lli->cfg = lli_cfg |
				DMA_CHAN_CFG_DST_IO_MODE |
				DMA_CHAN_CFG_SRC_LINEAR_MODE |
				DMA_CHAN_CFG_SRC_DRQ(DRQ_SDRAM) |
				DMA_CHAN_CFG_DST_DRQ(vchan->port);

			dev_dbg(chan2dev(chan),
				"%s; chan: %d, dest: %pad, src: %pad, len: %u. flags: 0x%08lx\n",
				__func__, vchan->vc.chan.chan_id,
				&sconfig->dst_addr, &sg_dma_address(sg),
				sg_dma_len(sg), flags);

		} else {
			v_lli->src = sconfig->src_addr;
			v_lli->dst = sg_dma_address(sg);
			v_lli->cfg = lli_cfg |
				DMA_CHAN_CFG_DST_LINEAR_MODE |
				DMA_CHAN_CFG_SRC_IO_MODE |
				DMA_CHAN_CFG_DST_DRQ(DRQ_SDRAM) |
				DMA_CHAN_CFG_SRC_DRQ(vchan->port);

			dev_dbg(chan2dev(chan),
				"%s; chan: %d, dest: %pad, src: %pad, len: %u. flags: 0x%08lx\n",
				__func__, vchan->vc.chan.chan_id,
				&sg_dma_address(sg), &sconfig->src_addr,
				sg_dma_len(sg), flags);
		}

		prev = sun6i_dma_lli_add(prev, v_lli, p_lli, txd);
	}

	dev_dbg(chan2dev(chan), "First: %pad\n", &txd->p_lli);
	for (prev = txd->v_lli; prev; prev = prev->v_lli_next)
		sun6i_dma_dump_lli(vchan, prev);

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_lli_free:
	for (prev = txd->v_lli; prev; prev = prev->v_lli_next)
		dma_pool_free(sdev->pool, prev, virt_to_phys(prev));
	kfree(txd);
	return NULL;
}

static struct dma_async_tx_descriptor *sun6i_dma_prep_dma_cyclic(
					struct dma_chan *chan,
					dma_addr_t buf_addr,
					size_t buf_len,
					size_t period_len,
					enum dma_transfer_direction dir,
					unsigned long flags)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct sun6i_dma_lli *v_lli, *prev = NULL;
	struct sun6i_desc *txd;
	dma_addr_t p_lli;
	u32 lli_cfg;
	unsigned int i, periods = buf_len / period_len;
	int ret;

	ret = set_config(sdev, sconfig, dir, &lli_cfg);
	if (ret) {
		dev_err(chan2dev(chan), "Invalid DMA configuration\n");
		return NULL;
	}

	txd = kzalloc(sizeof(*txd), GFP_NOWAIT);
	if (!txd)
		return NULL;

	for (i = 0; i < periods; i++) {
		v_lli = dma_pool_alloc(sdev->pool, GFP_NOWAIT, &p_lli);
		if (!v_lli) {
			dev_err(sdev->slave.dev, "Failed to alloc lli memory\n");
			goto err_lli_free;
		}

		v_lli->len = period_len;
		v_lli->para = NORMAL_WAIT;

		if (dir == DMA_MEM_TO_DEV) {
			v_lli->src = buf_addr + period_len * i;
			v_lli->dst = sconfig->dst_addr;
			v_lli->cfg = lli_cfg |
				DMA_CHAN_CFG_DST_IO_MODE |
				DMA_CHAN_CFG_SRC_LINEAR_MODE |
				DMA_CHAN_CFG_SRC_DRQ(DRQ_SDRAM) |
				DMA_CHAN_CFG_DST_DRQ(vchan->port);
		} else {
			v_lli->src = sconfig->src_addr;
			v_lli->dst = buf_addr + period_len * i;
			v_lli->cfg = lli_cfg |
				DMA_CHAN_CFG_DST_LINEAR_MODE |
				DMA_CHAN_CFG_SRC_IO_MODE |
				DMA_CHAN_CFG_DST_DRQ(DRQ_SDRAM) |
				DMA_CHAN_CFG_SRC_DRQ(vchan->port);
		}

		prev = sun6i_dma_lli_add(prev, v_lli, p_lli, txd);
	}

	prev->p_lli_next = txd->p_lli;		/* cyclic list */

	vchan->cyclic = true;

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_lli_free:
	for (prev = txd->v_lli; prev; prev = prev->v_lli_next)
		dma_pool_free(sdev->pool, prev, virt_to_phys(prev));
	kfree(txd);
	return NULL;
}

static int sun6i_dma_config(struct dma_chan *chan,
			    struct dma_slave_config *config)
{
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);

	memcpy(&vchan->cfg, config, sizeof(*config));

	return 0;
}

static int sun6i_dma_pause(struct dma_chan *chan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct sun6i_pchan *pchan = vchan->phy;

	dev_dbg(chan2dev(chan), "vchan %p: pause\n", &vchan->vc);

	if (pchan) {
		writel(DMA_CHAN_PAUSE_PAUSE,
		       pchan->base + DMA_CHAN_PAUSE);
	} else {
		spin_lock(&sdev->lock);
		list_del_init(&vchan->node);
		spin_unlock(&sdev->lock);
	}

	return 0;
}

static int sun6i_dma_resume(struct dma_chan *chan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct sun6i_pchan *pchan = vchan->phy;
	unsigned long flags;

	dev_dbg(chan2dev(chan), "vchan %p: resume\n", &vchan->vc);

	spin_lock_irqsave(&vchan->vc.lock, flags);

	if (pchan) {
		writel(DMA_CHAN_PAUSE_RESUME,
		       pchan->base + DMA_CHAN_PAUSE);
	} else if (!list_empty(&vchan->vc.desc_issued)) {
		spin_lock(&sdev->lock);
		list_add_tail(&vchan->node, &sdev->pending);
		spin_unlock(&sdev->lock);
	}

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int sun6i_dma_terminate_all(struct dma_chan *chan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct sun6i_pchan *pchan = vchan->phy;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock(&sdev->lock);
	list_del_init(&vchan->node);
	spin_unlock(&sdev->lock);

	spin_lock_irqsave(&vchan->vc.lock, flags);

	if (vchan->cyclic) {
		vchan->cyclic = false;
		if (pchan && pchan->desc) {
			struct virt_dma_desc *vd = &pchan->desc->vd;
			struct virt_dma_chan *vc = &vchan->vc;

			list_add_tail(&vd->node, &vc->desc_completed);
		}
	}

	vchan_get_all_descriptors(&vchan->vc, &head);

	if (pchan) {
		writel(DMA_CHAN_ENABLE_STOP, pchan->base + DMA_CHAN_ENABLE);
		writel(DMA_CHAN_PAUSE_RESUME, pchan->base + DMA_CHAN_PAUSE);

		vchan->phy = NULL;
		pchan->vchan = NULL;
		pchan->desc = NULL;
		pchan->done = NULL;
	}

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	vchan_dma_desc_free_list(&vchan->vc, &head);

	return 0;
}

static enum dma_status sun6i_dma_tx_status(struct dma_chan *chan,
					   dma_cookie_t cookie,
					   struct dma_tx_state *state)
{
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	struct sun6i_pchan *pchan = vchan->phy;
	struct sun6i_dma_lli *lli;
	struct virt_dma_desc *vd;
	struct sun6i_desc *txd;
	enum dma_status ret;
	unsigned long flags;
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, state);
	if (ret == DMA_COMPLETE || !state)
		return ret;

	spin_lock_irqsave(&vchan->vc.lock, flags);

	vd = vchan_find_desc(&vchan->vc, cookie);
	txd = to_sun6i_desc(&vd->tx);

	if (vd) {
		for (lli = txd->v_lli; lli != NULL; lli = lli->v_lli_next)
			bytes += lli->len;
	} else if (!pchan || !pchan->desc) {
		bytes = 0;
	} else {
		bytes = sun6i_get_chan_size(pchan);
	}

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	dma_set_residue(state, bytes);

	return ret;
}

static void sun6i_dma_issue_pending(struct dma_chan *chan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);

	if (vchan_issue_pending(&vchan->vc)) {
		spin_lock(&sdev->lock);

		if (!vchan->phy && list_empty(&vchan->node)) {
			list_add_tail(&vchan->node, &sdev->pending);
			tasklet_schedule(&sdev->task);
			dev_dbg(chan2dev(chan), "vchan %p: issued\n",
				&vchan->vc);
		}

		spin_unlock(&sdev->lock);
	} else {
		dev_dbg(chan2dev(chan), "vchan %p: nothing to issue\n",
			&vchan->vc);
	}

	spin_unlock_irqrestore(&vchan->vc.lock, flags);
}

static void sun6i_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sun6i_dma_dev *sdev = to_sun6i_dma_dev(chan->device);
	struct sun6i_vchan *vchan = to_sun6i_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&sdev->lock, flags);
	list_del_init(&vchan->node);
	spin_unlock_irqrestore(&sdev->lock, flags);

	vchan_free_chan_resources(&vchan->vc);
}

static struct dma_chan *sun6i_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct sun6i_dma_dev *sdev = ofdma->of_dma_data;
	struct sun6i_vchan *vchan;
	struct dma_chan *chan;
	u8 port = dma_spec->args[0];

	if (port > sdev->max_request)
		return NULL;

	chan = dma_get_any_slave_channel(&sdev->slave);
	if (!chan)
		return NULL;

	vchan = to_sun6i_vchan(chan);
	vchan->port = port;

	return chan;
}

static inline void sun6i_kill_tasklet(struct sun6i_dma_dev *sdev)
{
	/* Disable all interrupts from DMA */
	writel(0, sdev->base + DMA_IRQ_EN(0));
	writel(0, sdev->base + DMA_IRQ_EN(1));

	/* Prevent spurious interrupts from scheduling the tasklet */
	atomic_inc(&sdev->tasklet_shutdown);

	/* Make sure we won't have any further interrupts */
	devm_free_irq(sdev->slave.dev, sdev->irq, sdev);

	/* Actually prevent the tasklet from being scheduled */
	tasklet_kill(&sdev->task);
}

static inline void sun6i_dma_free(struct sun6i_dma_dev *sdev)
{
	int i;

	for (i = 0; i < sdev->num_vchans; i++) {
		struct sun6i_vchan *vchan = &sdev->vchans[i];

		list_del(&vchan->vc.chan.device_node);
		tasklet_kill(&vchan->vc.task);
	}
}

/*
 * For A31:
 *
 * There's 16 physical channels that can work in parallel.
 *
 * However we have 30 different endpoints for our requests.
 *
 * Since the channels are able to handle only an unidirectional
 * transfer, we need to allocate more virtual channels so that
 * everyone can grab one channel.
 *
 * Some devices can't work in both direction (mostly because it
 * wouldn't make sense), so we have a bit fewer virtual channels than
 * 2 channels per endpoints.
 */

static struct sun6i_dma_config sun6i_a31_dma_cfg = {
	.nr_max_channels = 16,
	.nr_max_requests = 30,
	.nr_max_vchans   = 53,
	.set_burst_length = sun6i_set_burst_length_a31,
	.src_burst_lengths = BIT(1) | BIT(8),
	.dst_burst_lengths = BIT(1) | BIT(8),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
};

/*
 * The A23 only has 8 physical channels, a maximum DRQ port id of 24,
 * and a total of 37 usable source and destination endpoints.
 */

static struct sun6i_dma_config sun8i_a23_dma_cfg = {
	.nr_max_channels = 8,
	.nr_max_requests = 24,
	.nr_max_vchans   = 37,
	.clock_autogate_enable = sun6i_enable_clock_autogate_a23,
	.set_burst_length = sun6i_set_burst_length_a31,
	.src_burst_lengths = BIT(1) | BIT(8),
	.dst_burst_lengths = BIT(1) | BIT(8),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
};

static struct sun6i_dma_config sun8i_a83t_dma_cfg = {
	.nr_max_channels = 8,
	.nr_max_requests = 28,
	.nr_max_vchans   = 39,
	.clock_autogate_enable = sun6i_enable_clock_autogate_a23,
	.set_burst_length = sun6i_set_burst_length_a31,
	.src_burst_lengths = BIT(1) | BIT(8),
	.dst_burst_lengths = BIT(1) | BIT(8),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
};

/*
 * The H3 has 12 physical channels, a maximum DRQ port id of 27,
 * and a total of 34 usable source and destination endpoints.
 * It also supports additional burst lengths and bus widths,
 * and the burst length fields have different offsets.
 */

static struct sun6i_dma_config sun8i_h3_dma_cfg = {
	.nr_max_channels = 12,
	.nr_max_requests = 27,
	.nr_max_vchans   = 34,
	.clock_autogate_enable = sun6i_enable_clock_autogate_h3,
	.set_burst_length = sun6i_set_burst_length_h3,
	.src_burst_lengths = BIT(1) | BIT(4) | BIT(8) | BIT(16),
	.dst_burst_lengths = BIT(1) | BIT(4) | BIT(8) | BIT(16),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_8_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_8_BYTES),
};

/*
 * The A64 binding uses the number of dma channels from the
 * device tree node.
 */
static struct sun6i_dma_config sun50i_a64_dma_cfg = {
	.clock_autogate_enable = sun6i_enable_clock_autogate_h3,
	.set_burst_length = sun6i_set_burst_length_h3,
	.src_burst_lengths = BIT(1) | BIT(4) | BIT(8) | BIT(16),
	.dst_burst_lengths = BIT(1) | BIT(4) | BIT(8) | BIT(16),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_8_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_8_BYTES),
};

/*
 * The V3s have only 8 physical channels, a maximum DRQ port id of 23,
 * and a total of 24 usable source and destination endpoints.
 */

static struct sun6i_dma_config sun8i_v3s_dma_cfg = {
	.nr_max_channels = 8,
	.nr_max_requests = 23,
	.nr_max_vchans   = 24,
	.clock_autogate_enable = sun6i_enable_clock_autogate_a23,
	.set_burst_length = sun6i_set_burst_length_a31,
	.src_burst_lengths = BIT(1) | BIT(8),
	.dst_burst_lengths = BIT(1) | BIT(8),
	.src_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
	.dst_addr_widths   = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			     BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			     BIT(DMA_SLAVE_BUSWIDTH_4_BYTES),
};

static const struct of_device_id sun6i_dma_match[] = {
	{ .compatible = "allwinner,sun6i-a31-dma", .data = &sun6i_a31_dma_cfg },
	{ .compatible = "allwinner,sun8i-a23-dma", .data = &sun8i_a23_dma_cfg },
	{ .compatible = "allwinner,sun8i-a83t-dma", .data = &sun8i_a83t_dma_cfg },
	{ .compatible = "allwinner,sun8i-h3-dma", .data = &sun8i_h3_dma_cfg },
	{ .compatible = "allwinner,sun8i-v3s-dma", .data = &sun8i_v3s_dma_cfg },
	{ .compatible = "allwinner,sun50i-a64-dma", .data = &sun50i_a64_dma_cfg },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun6i_dma_match);

static int sun6i_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sun6i_dma_dev *sdc;
	struct resource *res;
	int ret, i;

	sdc = devm_kzalloc(&pdev->dev, sizeof(*sdc), GFP_KERNEL);
	if (!sdc)
		return -ENOMEM;

	sdc->cfg = of_device_get_match_data(&pdev->dev);
	if (!sdc->cfg)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sdc->base))
		return PTR_ERR(sdc->base);

	sdc->irq = platform_get_irq(pdev, 0);
	if (sdc->irq < 0) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return sdc->irq;
	}

	sdc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdc->clk)) {
		dev_err(&pdev->dev, "No clock specified\n");
		return PTR_ERR(sdc->clk);
	}

	sdc->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(sdc->rstc)) {
		dev_err(&pdev->dev, "No reset controller specified\n");
		return PTR_ERR(sdc->rstc);
	}

	sdc->pool = dmam_pool_create(dev_name(&pdev->dev), &pdev->dev,
				     sizeof(struct sun6i_dma_lli), 4, 0);
	if (!sdc->pool) {
		dev_err(&pdev->dev, "No memory for descriptors dma pool\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, sdc);
	INIT_LIST_HEAD(&sdc->pending);
	spin_lock_init(&sdc->lock);

	dma_cap_set(DMA_PRIVATE, sdc->slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, sdc->slave.cap_mask);
	dma_cap_set(DMA_SLAVE, sdc->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, sdc->slave.cap_mask);

	INIT_LIST_HEAD(&sdc->slave.channels);
	sdc->slave.device_free_chan_resources	= sun6i_dma_free_chan_resources;
	sdc->slave.device_tx_status		= sun6i_dma_tx_status;
	sdc->slave.device_issue_pending		= sun6i_dma_issue_pending;
	sdc->slave.device_prep_slave_sg		= sun6i_dma_prep_slave_sg;
	sdc->slave.device_prep_dma_memcpy	= sun6i_dma_prep_dma_memcpy;
	sdc->slave.device_prep_dma_cyclic	= sun6i_dma_prep_dma_cyclic;
	sdc->slave.copy_align			= DMAENGINE_ALIGN_4_BYTES;
	sdc->slave.device_config		= sun6i_dma_config;
	sdc->slave.device_pause			= sun6i_dma_pause;
	sdc->slave.device_resume		= sun6i_dma_resume;
	sdc->slave.device_terminate_all		= sun6i_dma_terminate_all;
	sdc->slave.src_addr_widths		= sdc->cfg->src_addr_widths;
	sdc->slave.dst_addr_widths		= sdc->cfg->dst_addr_widths;
	sdc->slave.directions			= BIT(DMA_DEV_TO_MEM) |
						  BIT(DMA_MEM_TO_DEV);
	sdc->slave.residue_granularity		= DMA_RESIDUE_GRANULARITY_BURST;
	sdc->slave.dev = &pdev->dev;

	sdc->num_pchans = sdc->cfg->nr_max_channels;
	sdc->num_vchans = sdc->cfg->nr_max_vchans;
	sdc->max_request = sdc->cfg->nr_max_requests;

	ret = of_property_read_u32(np, "dma-channels", &sdc->num_pchans);
	if (ret && !sdc->num_pchans) {
		dev_err(&pdev->dev, "Can't get dma-channels.\n");
		return ret;
	}

	ret = of_property_read_u32(np, "dma-requests", &sdc->max_request);
	if (ret && !sdc->max_request) {
		dev_info(&pdev->dev, "Missing dma-requests, using %u.\n",
			 DMA_CHAN_MAX_DRQ);
		sdc->max_request = DMA_CHAN_MAX_DRQ;
	}

	/*
	 * If the number of vchans is not specified, derive it from the
	 * highest port number, at most one channel per port and direction.
	 */
	if (!sdc->num_vchans)
		sdc->num_vchans = 2 * (sdc->max_request + 1);

	sdc->pchans = devm_kcalloc(&pdev->dev, sdc->num_pchans,
				   sizeof(struct sun6i_pchan), GFP_KERNEL);
	if (!sdc->pchans)
		return -ENOMEM;

	sdc->vchans = devm_kcalloc(&pdev->dev, sdc->num_vchans,
				   sizeof(struct sun6i_vchan), GFP_KERNEL);
	if (!sdc->vchans)
		return -ENOMEM;

	tasklet_init(&sdc->task, sun6i_dma_tasklet, (unsigned long)sdc);

	for (i = 0; i < sdc->num_pchans; i++) {
		struct sun6i_pchan *pchan = &sdc->pchans[i];

		pchan->idx = i;
		pchan->base = sdc->base + 0x100 + i * 0x40;
	}

	for (i = 0; i < sdc->num_vchans; i++) {
		struct sun6i_vchan *vchan = &sdc->vchans[i];

		INIT_LIST_HEAD(&vchan->node);
		vchan->vc.desc_free = sun6i_dma_free_desc;
		vchan_init(&vchan->vc, &sdc->slave);
	}

	ret = reset_control_deassert(sdc->rstc);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't deassert the device from reset\n");
		goto err_chan_free;
	}

	ret = clk_prepare_enable(sdc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't enable the clock\n");
		goto err_reset_assert;
	}

	ret = devm_request_irq(&pdev->dev, sdc->irq, sun6i_dma_interrupt, 0,
			       dev_name(&pdev->dev), sdc);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request IRQ\n");
		goto err_clk_disable;
	}

	ret = dma_async_device_register(&sdc->slave);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to register DMA engine device\n");
		goto err_irq_disable;
	}

	ret = of_dma_controller_register(pdev->dev.of_node, sun6i_dma_of_xlate,
					 sdc);
	if (ret) {
		dev_err(&pdev->dev, "of_dma_controller_register failed\n");
		goto err_dma_unregister;
	}

	if (sdc->cfg->clock_autogate_enable)
		sdc->cfg->clock_autogate_enable(sdc);

	return 0;

err_dma_unregister:
	dma_async_device_unregister(&sdc->slave);
err_irq_disable:
	sun6i_kill_tasklet(sdc);
err_clk_disable:
	clk_disable_unprepare(sdc->clk);
err_reset_assert:
	reset_control_assert(sdc->rstc);
err_chan_free:
	sun6i_dma_free(sdc);
	return ret;
}

static int sun6i_dma_remove(struct platform_device *pdev)
{
	struct sun6i_dma_dev *sdc = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&sdc->slave);

	sun6i_kill_tasklet(sdc);

	clk_disable_unprepare(sdc->clk);
	reset_control_assert(sdc->rstc);

	sun6i_dma_free(sdc);

	return 0;
}

static struct platform_driver sun6i_dma_driver = {
	.probe		= sun6i_dma_probe,
	.remove		= sun6i_dma_remove,
	.driver = {
		.name		= "sun6i-dma",
		.of_match_table	= sun6i_dma_match,
	},
};
module_platform_driver(sun6i_dma_driver);

MODULE_DESCRIPTION("Allwinner A31 DMA Controller Driver");
MODULE_AUTHOR("Sugar <shuge@allwinnertech.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_LICENSE("GPL");
