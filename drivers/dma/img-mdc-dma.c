/*
 * IMG Multi-threaded DMA Controller (MDC)
 *
 * Copyright (C) 2009,2012,2013 Imagination Technologies Ltd.
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define MDC_MAX_DMA_CHANNELS			32

#define MDC_GENERAL_CONFIG			0x000
#define MDC_GENERAL_CONFIG_LIST_IEN		BIT(31)
#define MDC_GENERAL_CONFIG_IEN			BIT(29)
#define MDC_GENERAL_CONFIG_LEVEL_INT		BIT(28)
#define MDC_GENERAL_CONFIG_INC_W		BIT(12)
#define MDC_GENERAL_CONFIG_INC_R		BIT(8)
#define MDC_GENERAL_CONFIG_PHYSICAL_W		BIT(7)
#define MDC_GENERAL_CONFIG_WIDTH_W_SHIFT	4
#define MDC_GENERAL_CONFIG_WIDTH_W_MASK		0x7
#define MDC_GENERAL_CONFIG_PHYSICAL_R		BIT(3)
#define MDC_GENERAL_CONFIG_WIDTH_R_SHIFT	0
#define MDC_GENERAL_CONFIG_WIDTH_R_MASK		0x7

#define MDC_READ_PORT_CONFIG			0x004
#define MDC_READ_PORT_CONFIG_STHREAD_SHIFT	28
#define MDC_READ_PORT_CONFIG_STHREAD_MASK	0xf
#define MDC_READ_PORT_CONFIG_RTHREAD_SHIFT	24
#define MDC_READ_PORT_CONFIG_RTHREAD_MASK	0xf
#define MDC_READ_PORT_CONFIG_WTHREAD_SHIFT	16
#define MDC_READ_PORT_CONFIG_WTHREAD_MASK	0xf
#define MDC_READ_PORT_CONFIG_BURST_SIZE_SHIFT	4
#define MDC_READ_PORT_CONFIG_BURST_SIZE_MASK	0xff
#define MDC_READ_PORT_CONFIG_DREQ_ENABLE	BIT(1)

#define MDC_READ_ADDRESS			0x008

#define MDC_WRITE_ADDRESS			0x00c

#define MDC_TRANSFER_SIZE			0x010
#define MDC_TRANSFER_SIZE_MASK			0xffffff

#define MDC_LIST_NODE_ADDRESS			0x014

#define MDC_CMDS_PROCESSED			0x018
#define MDC_CMDS_PROCESSED_CMDS_PROCESSED_SHIFT	16
#define MDC_CMDS_PROCESSED_CMDS_PROCESSED_MASK	0x3f
#define MDC_CMDS_PROCESSED_INT_ACTIVE		BIT(8)
#define MDC_CMDS_PROCESSED_CMDS_DONE_SHIFT	0
#define MDC_CMDS_PROCESSED_CMDS_DONE_MASK	0x3f

#define MDC_CONTROL_AND_STATUS			0x01c
#define MDC_CONTROL_AND_STATUS_CANCEL		BIT(20)
#define MDC_CONTROL_AND_STATUS_LIST_EN		BIT(4)
#define MDC_CONTROL_AND_STATUS_EN		BIT(0)

#define MDC_ACTIVE_TRANSFER_SIZE		0x030

#define MDC_GLOBAL_CONFIG_A				0x900
#define MDC_GLOBAL_CONFIG_A_THREAD_ID_WIDTH_SHIFT	16
#define MDC_GLOBAL_CONFIG_A_THREAD_ID_WIDTH_MASK	0xff
#define MDC_GLOBAL_CONFIG_A_DMA_CONTEXTS_SHIFT		8
#define MDC_GLOBAL_CONFIG_A_DMA_CONTEXTS_MASK		0xff
#define MDC_GLOBAL_CONFIG_A_SYS_DAT_WIDTH_SHIFT		0
#define MDC_GLOBAL_CONFIG_A_SYS_DAT_WIDTH_MASK		0xff

struct mdc_hw_list_desc {
	u32 gen_conf;
	u32 readport_conf;
	u32 read_addr;
	u32 write_addr;
	u32 xfer_size;
	u32 node_addr;
	u32 cmds_done;
	u32 ctrl_status;
	/*
	 * Not part of the list descriptor, but instead used by the CPU to
	 * traverse the list.
	 */
	struct mdc_hw_list_desc *next_desc;
};

struct mdc_tx_desc {
	struct mdc_chan *chan;
	struct virt_dma_desc vd;
	dma_addr_t list_phys;
	struct mdc_hw_list_desc *list;
	bool cyclic;
	bool cmd_loaded;
	unsigned int list_len;
	unsigned int list_period_len;
	size_t list_xfer_size;
	unsigned int list_cmds_done;
};

struct mdc_chan {
	struct mdc_dma *mdma;
	struct virt_dma_chan vc;
	struct dma_slave_config config;
	struct mdc_tx_desc *desc;
	int irq;
	unsigned int periph;
	unsigned int thread;
	unsigned int chan_nr;
};

struct mdc_dma_soc_data {
	void (*enable_chan)(struct mdc_chan *mchan);
	void (*disable_chan)(struct mdc_chan *mchan);
};

struct mdc_dma {
	struct dma_device dma_dev;
	void __iomem *regs;
	struct clk *clk;
	struct dma_pool *desc_pool;
	struct regmap *periph_regs;
	spinlock_t lock;
	unsigned int nr_threads;
	unsigned int nr_channels;
	unsigned int bus_width;
	unsigned int max_burst_mult;
	unsigned int max_xfer_size;
	const struct mdc_dma_soc_data *soc;
	struct mdc_chan channels[MDC_MAX_DMA_CHANNELS];
};

static inline u32 mdc_readl(struct mdc_dma *mdma, u32 reg)
{
	return readl(mdma->regs + reg);
}

static inline void mdc_writel(struct mdc_dma *mdma, u32 val, u32 reg)
{
	writel(val, mdma->regs + reg);
}

static inline u32 mdc_chan_readl(struct mdc_chan *mchan, u32 reg)
{
	return mdc_readl(mchan->mdma, mchan->chan_nr * 0x040 + reg);
}

static inline void mdc_chan_writel(struct mdc_chan *mchan, u32 val, u32 reg)
{
	mdc_writel(mchan->mdma, val, mchan->chan_nr * 0x040 + reg);
}

static inline struct mdc_chan *to_mdc_chan(struct dma_chan *c)
{
	return container_of(to_virt_chan(c), struct mdc_chan, vc);
}

static inline struct mdc_tx_desc *to_mdc_desc(struct dma_async_tx_descriptor *t)
{
	struct virt_dma_desc *vdesc = container_of(t, struct virt_dma_desc, tx);

	return container_of(vdesc, struct mdc_tx_desc, vd);
}

static inline struct device *mdma2dev(struct mdc_dma *mdma)
{
	return mdma->dma_dev.dev;
}

static inline unsigned int to_mdc_width(unsigned int bytes)
{
	return ffs(bytes) - 1;
}

static inline void mdc_set_read_width(struct mdc_hw_list_desc *ldesc,
				      unsigned int bytes)
{
	ldesc->gen_conf |= to_mdc_width(bytes) <<
		MDC_GENERAL_CONFIG_WIDTH_R_SHIFT;
}

static inline void mdc_set_write_width(struct mdc_hw_list_desc *ldesc,
				       unsigned int bytes)
{
	ldesc->gen_conf |= to_mdc_width(bytes) <<
		MDC_GENERAL_CONFIG_WIDTH_W_SHIFT;
}

static void mdc_list_desc_config(struct mdc_chan *mchan,
				 struct mdc_hw_list_desc *ldesc,
				 enum dma_transfer_direction dir,
				 dma_addr_t src, dma_addr_t dst, size_t len)
{
	struct mdc_dma *mdma = mchan->mdma;
	unsigned int max_burst, burst_size;

	ldesc->gen_conf = MDC_GENERAL_CONFIG_IEN | MDC_GENERAL_CONFIG_LIST_IEN |
		MDC_GENERAL_CONFIG_LEVEL_INT | MDC_GENERAL_CONFIG_PHYSICAL_W |
		MDC_GENERAL_CONFIG_PHYSICAL_R;
	ldesc->readport_conf =
		(mchan->thread << MDC_READ_PORT_CONFIG_STHREAD_SHIFT) |
		(mchan->thread << MDC_READ_PORT_CONFIG_RTHREAD_SHIFT) |
		(mchan->thread << MDC_READ_PORT_CONFIG_WTHREAD_SHIFT);
	ldesc->read_addr = src;
	ldesc->write_addr = dst;
	ldesc->xfer_size = len - 1;
	ldesc->node_addr = 0;
	ldesc->cmds_done = 0;
	ldesc->ctrl_status = MDC_CONTROL_AND_STATUS_LIST_EN |
		MDC_CONTROL_AND_STATUS_EN;
	ldesc->next_desc = NULL;

	if (IS_ALIGNED(dst, mdma->bus_width) &&
	    IS_ALIGNED(src, mdma->bus_width))
		max_burst = mdma->bus_width * mdma->max_burst_mult;
	else
		max_burst = mdma->bus_width * (mdma->max_burst_mult - 1);

	if (dir == DMA_MEM_TO_DEV) {
		ldesc->gen_conf |= MDC_GENERAL_CONFIG_INC_R;
		ldesc->readport_conf |= MDC_READ_PORT_CONFIG_DREQ_ENABLE;
		mdc_set_read_width(ldesc, mdma->bus_width);
		mdc_set_write_width(ldesc, mchan->config.dst_addr_width);
		burst_size = min(max_burst, mchan->config.dst_maxburst *
				 mchan->config.dst_addr_width);
	} else if (dir == DMA_DEV_TO_MEM) {
		ldesc->gen_conf |= MDC_GENERAL_CONFIG_INC_W;
		ldesc->readport_conf |= MDC_READ_PORT_CONFIG_DREQ_ENABLE;
		mdc_set_read_width(ldesc, mchan->config.src_addr_width);
		mdc_set_write_width(ldesc, mdma->bus_width);
		burst_size = min(max_burst, mchan->config.src_maxburst *
				 mchan->config.src_addr_width);
	} else {
		ldesc->gen_conf |= MDC_GENERAL_CONFIG_INC_R |
			MDC_GENERAL_CONFIG_INC_W;
		mdc_set_read_width(ldesc, mdma->bus_width);
		mdc_set_write_width(ldesc, mdma->bus_width);
		burst_size = max_burst;
	}
	ldesc->readport_conf |= (burst_size - 1) <<
		MDC_READ_PORT_CONFIG_BURST_SIZE_SHIFT;
}

static void mdc_list_desc_free(struct mdc_tx_desc *mdesc)
{
	struct mdc_dma *mdma = mdesc->chan->mdma;
	struct mdc_hw_list_desc *curr, *next;
	dma_addr_t curr_phys, next_phys;

	curr = mdesc->list;
	curr_phys = mdesc->list_phys;
	while (curr) {
		next = curr->next_desc;
		next_phys = curr->node_addr;
		dma_pool_free(mdma->desc_pool, curr, curr_phys);
		curr = next;
		curr_phys = next_phys;
	}
}

static void mdc_desc_free(struct virt_dma_desc *vd)
{
	struct mdc_tx_desc *mdesc = to_mdc_desc(&vd->tx);

	mdc_list_desc_free(mdesc);
	kfree(mdesc);
}

static struct dma_async_tx_descriptor *mdc_prep_dma_memcpy(
	struct dma_chan *chan, dma_addr_t dest, dma_addr_t src, size_t len,
	unsigned long flags)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma *mdma = mchan->mdma;
	struct mdc_tx_desc *mdesc;
	struct mdc_hw_list_desc *curr, *prev = NULL;
	dma_addr_t curr_phys, prev_phys;

	if (!len)
		return NULL;

	mdesc = kzalloc(sizeof(*mdesc), GFP_NOWAIT);
	if (!mdesc)
		return NULL;
	mdesc->chan = mchan;
	mdesc->list_xfer_size = len;

	while (len > 0) {
		size_t xfer_size;

		curr = dma_pool_alloc(mdma->desc_pool, GFP_NOWAIT, &curr_phys);
		if (!curr)
			goto free_desc;

		if (prev) {
			prev->node_addr = curr_phys;
			prev->next_desc = curr;
		} else {
			mdesc->list_phys = curr_phys;
			mdesc->list = curr;
		}

		xfer_size = min_t(size_t, mdma->max_xfer_size, len);

		mdc_list_desc_config(mchan, curr, DMA_MEM_TO_MEM, src, dest,
				     xfer_size);

		prev = curr;
		prev_phys = curr_phys;

		mdesc->list_len++;
		src += xfer_size;
		dest += xfer_size;
		len -= xfer_size;
	}

	return vchan_tx_prep(&mchan->vc, &mdesc->vd, flags);

free_desc:
	mdc_desc_free(&mdesc->vd);

	return NULL;
}

static int mdc_check_slave_width(struct mdc_chan *mchan,
				 enum dma_transfer_direction dir)
{
	enum dma_slave_buswidth width;

	if (dir == DMA_MEM_TO_DEV)
		width = mchan->config.dst_addr_width;
	else
		width = mchan->config.src_addr_width;

	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		break;
	default:
		return -EINVAL;
	}

	if (width > mchan->mdma->bus_width)
		return -EINVAL;

	return 0;
}

static struct dma_async_tx_descriptor *mdc_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction dir,
	unsigned long flags)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma *mdma = mchan->mdma;
	struct mdc_tx_desc *mdesc;
	struct mdc_hw_list_desc *curr, *prev = NULL;
	dma_addr_t curr_phys, prev_phys;

	if (!buf_len && !period_len)
		return NULL;

	if (!is_slave_direction(dir))
		return NULL;

	if (mdc_check_slave_width(mchan, dir) < 0)
		return NULL;

	mdesc = kzalloc(sizeof(*mdesc), GFP_NOWAIT);
	if (!mdesc)
		return NULL;
	mdesc->chan = mchan;
	mdesc->cyclic = true;
	mdesc->list_xfer_size = buf_len;
	mdesc->list_period_len = DIV_ROUND_UP(period_len,
					      mdma->max_xfer_size);

	while (buf_len > 0) {
		size_t remainder = min(period_len, buf_len);

		while (remainder > 0) {
			size_t xfer_size;

			curr = dma_pool_alloc(mdma->desc_pool, GFP_NOWAIT,
					      &curr_phys);
			if (!curr)
				goto free_desc;

			if (!prev) {
				mdesc->list_phys = curr_phys;
				mdesc->list = curr;
			} else {
				prev->node_addr = curr_phys;
				prev->next_desc = curr;
			}

			xfer_size = min_t(size_t, mdma->max_xfer_size,
					  remainder);

			if (dir == DMA_MEM_TO_DEV) {
				mdc_list_desc_config(mchan, curr, dir,
						     buf_addr,
						     mchan->config.dst_addr,
						     xfer_size);
			} else {
				mdc_list_desc_config(mchan, curr, dir,
						     mchan->config.src_addr,
						     buf_addr,
						     xfer_size);
			}

			prev = curr;
			prev_phys = curr_phys;

			mdesc->list_len++;
			buf_addr += xfer_size;
			buf_len -= xfer_size;
			remainder -= xfer_size;
		}
	}
	prev->node_addr = mdesc->list_phys;

	return vchan_tx_prep(&mchan->vc, &mdesc->vd, flags);

free_desc:
	mdc_desc_free(&mdesc->vd);

	return NULL;
}

static struct dma_async_tx_descriptor *mdc_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction dir,
	unsigned long flags, void *context)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma *mdma = mchan->mdma;
	struct mdc_tx_desc *mdesc;
	struct scatterlist *sg;
	struct mdc_hw_list_desc *curr, *prev = NULL;
	dma_addr_t curr_phys, prev_phys;
	unsigned int i;

	if (!sgl)
		return NULL;

	if (!is_slave_direction(dir))
		return NULL;

	if (mdc_check_slave_width(mchan, dir) < 0)
		return NULL;

	mdesc = kzalloc(sizeof(*mdesc), GFP_NOWAIT);
	if (!mdesc)
		return NULL;
	mdesc->chan = mchan;

	for_each_sg(sgl, sg, sg_len, i) {
		dma_addr_t buf = sg_dma_address(sg);
		size_t buf_len = sg_dma_len(sg);

		while (buf_len > 0) {
			size_t xfer_size;

			curr = dma_pool_alloc(mdma->desc_pool, GFP_NOWAIT,
					      &curr_phys);
			if (!curr)
				goto free_desc;

			if (!prev) {
				mdesc->list_phys = curr_phys;
				mdesc->list = curr;
			} else {
				prev->node_addr = curr_phys;
				prev->next_desc = curr;
			}

			xfer_size = min_t(size_t, mdma->max_xfer_size,
					  buf_len);

			if (dir == DMA_MEM_TO_DEV) {
				mdc_list_desc_config(mchan, curr, dir, buf,
						     mchan->config.dst_addr,
						     xfer_size);
			} else {
				mdc_list_desc_config(mchan, curr, dir,
						     mchan->config.src_addr,
						     buf, xfer_size);
			}

			prev = curr;
			prev_phys = curr_phys;

			mdesc->list_len++;
			mdesc->list_xfer_size += xfer_size;
			buf += xfer_size;
			buf_len -= xfer_size;
		}
	}

	return vchan_tx_prep(&mchan->vc, &mdesc->vd, flags);

free_desc:
	mdc_desc_free(&mdesc->vd);

	return NULL;
}

static void mdc_issue_desc(struct mdc_chan *mchan)
{
	struct mdc_dma *mdma = mchan->mdma;
	struct virt_dma_desc *vd;
	struct mdc_tx_desc *mdesc;
	u32 val;

	vd = vchan_next_desc(&mchan->vc);
	if (!vd)
		return;

	list_del(&vd->node);

	mdesc = to_mdc_desc(&vd->tx);
	mchan->desc = mdesc;

	dev_dbg(mdma2dev(mdma), "Issuing descriptor on channel %d\n",
		mchan->chan_nr);

	mdma->soc->enable_chan(mchan);

	val = mdc_chan_readl(mchan, MDC_GENERAL_CONFIG);
	val |= MDC_GENERAL_CONFIG_LIST_IEN | MDC_GENERAL_CONFIG_IEN |
		MDC_GENERAL_CONFIG_LEVEL_INT | MDC_GENERAL_CONFIG_PHYSICAL_W |
		MDC_GENERAL_CONFIG_PHYSICAL_R;
	mdc_chan_writel(mchan, val, MDC_GENERAL_CONFIG);
	val = (mchan->thread << MDC_READ_PORT_CONFIG_STHREAD_SHIFT) |
		(mchan->thread << MDC_READ_PORT_CONFIG_RTHREAD_SHIFT) |
		(mchan->thread << MDC_READ_PORT_CONFIG_WTHREAD_SHIFT);
	mdc_chan_writel(mchan, val, MDC_READ_PORT_CONFIG);
	mdc_chan_writel(mchan, mdesc->list_phys, MDC_LIST_NODE_ADDRESS);
	val = mdc_chan_readl(mchan, MDC_CONTROL_AND_STATUS);
	val |= MDC_CONTROL_AND_STATUS_LIST_EN;
	mdc_chan_writel(mchan, val, MDC_CONTROL_AND_STATUS);
}

static void mdc_issue_pending(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&mchan->vc.lock, flags);
	if (vchan_issue_pending(&mchan->vc) && !mchan->desc)
		mdc_issue_desc(mchan);
	spin_unlock_irqrestore(&mchan->vc.lock, flags);
}

static enum dma_status mdc_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_tx_desc *mdesc;
	struct virt_dma_desc *vd;
	unsigned long flags;
	size_t bytes = 0;
	int ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (!txstate)
		return ret;

	spin_lock_irqsave(&mchan->vc.lock, flags);
	vd = vchan_find_desc(&mchan->vc, cookie);
	if (vd) {
		mdesc = to_mdc_desc(&vd->tx);
		bytes = mdesc->list_xfer_size;
	} else if (mchan->desc && mchan->desc->vd.tx.cookie == cookie) {
		struct mdc_hw_list_desc *ldesc;
		u32 val1, val2, done, processed, residue;
		int i, cmds;

		mdesc = mchan->desc;

		/*
		 * Determine the number of commands that haven't been
		 * processed (handled by the IRQ handler) yet.
		 */
		do {
			val1 = mdc_chan_readl(mchan, MDC_CMDS_PROCESSED) &
				~MDC_CMDS_PROCESSED_INT_ACTIVE;
			residue = mdc_chan_readl(mchan,
						 MDC_ACTIVE_TRANSFER_SIZE);
			val2 = mdc_chan_readl(mchan, MDC_CMDS_PROCESSED) &
				~MDC_CMDS_PROCESSED_INT_ACTIVE;
		} while (val1 != val2);

		done = (val1 >> MDC_CMDS_PROCESSED_CMDS_DONE_SHIFT) &
			MDC_CMDS_PROCESSED_CMDS_DONE_MASK;
		processed = (val1 >> MDC_CMDS_PROCESSED_CMDS_PROCESSED_SHIFT) &
			MDC_CMDS_PROCESSED_CMDS_PROCESSED_MASK;
		cmds = (done - processed) %
			(MDC_CMDS_PROCESSED_CMDS_DONE_MASK + 1);

		/*
		 * If the command loaded event hasn't been processed yet, then
		 * the difference above includes an extra command.
		 */
		if (!mdesc->cmd_loaded)
			cmds--;
		else
			cmds += mdesc->list_cmds_done;

		bytes = mdesc->list_xfer_size;
		ldesc = mdesc->list;
		for (i = 0; i < cmds; i++) {
			bytes -= ldesc->xfer_size + 1;
			ldesc = ldesc->next_desc;
		}
		if (ldesc) {
			if (residue != MDC_TRANSFER_SIZE_MASK)
				bytes -= ldesc->xfer_size - residue;
			else
				bytes -= ldesc->xfer_size + 1;
		}
	}
	spin_unlock_irqrestore(&mchan->vc.lock, flags);

	dma_set_residue(txstate, bytes);

	return ret;
}

static unsigned int mdc_get_new_events(struct mdc_chan *mchan)
{
	u32 val, processed, done1, done2;
	unsigned int ret;

	val = mdc_chan_readl(mchan, MDC_CMDS_PROCESSED);
	processed = (val >> MDC_CMDS_PROCESSED_CMDS_PROCESSED_SHIFT) &
				MDC_CMDS_PROCESSED_CMDS_PROCESSED_MASK;
	/*
	 * CMDS_DONE may have incremented between reading CMDS_PROCESSED
	 * and clearing INT_ACTIVE.  Re-read CMDS_PROCESSED to ensure we
	 * didn't miss a command completion.
	 */
	do {
		val = mdc_chan_readl(mchan, MDC_CMDS_PROCESSED);

		done1 = (val >> MDC_CMDS_PROCESSED_CMDS_DONE_SHIFT) &
			MDC_CMDS_PROCESSED_CMDS_DONE_MASK;

		val &= ~((MDC_CMDS_PROCESSED_CMDS_PROCESSED_MASK <<
			  MDC_CMDS_PROCESSED_CMDS_PROCESSED_SHIFT) |
			 MDC_CMDS_PROCESSED_INT_ACTIVE);

		val |= done1 << MDC_CMDS_PROCESSED_CMDS_PROCESSED_SHIFT;

		mdc_chan_writel(mchan, val, MDC_CMDS_PROCESSED);

		val = mdc_chan_readl(mchan, MDC_CMDS_PROCESSED);

		done2 = (val >> MDC_CMDS_PROCESSED_CMDS_DONE_SHIFT) &
			MDC_CMDS_PROCESSED_CMDS_DONE_MASK;
	} while (done1 != done2);

	if (done1 >= processed)
		ret = done1 - processed;
	else
		ret = ((MDC_CMDS_PROCESSED_CMDS_PROCESSED_MASK + 1) -
			processed) + done1;

	return ret;
}

static int mdc_terminate_all(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_tx_desc *mdesc;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&mchan->vc.lock, flags);

	mdc_chan_writel(mchan, MDC_CONTROL_AND_STATUS_CANCEL,
			MDC_CONTROL_AND_STATUS);

	mdesc = mchan->desc;
	mchan->desc = NULL;
	vchan_get_all_descriptors(&mchan->vc, &head);

	mdc_get_new_events(mchan);

	spin_unlock_irqrestore(&mchan->vc.lock, flags);

	if (mdesc)
		mdc_desc_free(&mdesc->vd);
	vchan_dma_desc_free_list(&mchan->vc, &head);

	return 0;
}

static int mdc_slave_config(struct dma_chan *chan,
			    struct dma_slave_config *config)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&mchan->vc.lock, flags);
	mchan->config = *config;
	spin_unlock_irqrestore(&mchan->vc.lock, flags);

	return 0;
}

static void mdc_free_chan_resources(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma *mdma = mchan->mdma;

	mdc_terminate_all(chan);

	mdma->soc->disable_chan(mchan);
}

static irqreturn_t mdc_chan_irq(int irq, void *dev_id)
{
	struct mdc_chan *mchan = (struct mdc_chan *)dev_id;
	struct mdc_tx_desc *mdesc;
	unsigned int i, new_events;

	spin_lock(&mchan->vc.lock);

	dev_dbg(mdma2dev(mchan->mdma), "IRQ on channel %d\n", mchan->chan_nr);

	new_events = mdc_get_new_events(mchan);

	if (!new_events)
		goto out;

	mdesc = mchan->desc;
	if (!mdesc) {
		dev_warn(mdma2dev(mchan->mdma),
			 "IRQ with no active descriptor on channel %d\n",
			 mchan->chan_nr);
		goto out;
	}

	for (i = 0; i < new_events; i++) {
		/*
		 * The first interrupt in a transfer indicates that the
		 * command list has been loaded, not that a command has
		 * been completed.
		 */
		if (!mdesc->cmd_loaded) {
			mdesc->cmd_loaded = true;
			continue;
		}

		mdesc->list_cmds_done++;
		if (mdesc->cyclic) {
			mdesc->list_cmds_done %= mdesc->list_len;
			if (mdesc->list_cmds_done % mdesc->list_period_len == 0)
				vchan_cyclic_callback(&mdesc->vd);
		} else if (mdesc->list_cmds_done == mdesc->list_len) {
			mchan->desc = NULL;
			vchan_cookie_complete(&mdesc->vd);
			mdc_issue_desc(mchan);
			break;
		}
	}
out:
	spin_unlock(&mchan->vc.lock);

	return IRQ_HANDLED;
}

static struct dma_chan *mdc_of_xlate(struct of_phandle_args *dma_spec,
				     struct of_dma *ofdma)
{
	struct mdc_dma *mdma = ofdma->of_dma_data;
	struct dma_chan *chan;

	if (dma_spec->args_count != 3)
		return NULL;

	list_for_each_entry(chan, &mdma->dma_dev.channels, device_node) {
		struct mdc_chan *mchan = to_mdc_chan(chan);

		if (!(dma_spec->args[1] & BIT(mchan->chan_nr)))
			continue;
		if (dma_get_slave_channel(chan)) {
			mchan->periph = dma_spec->args[0];
			mchan->thread = dma_spec->args[2];
			return chan;
		}
	}

	return NULL;
}

#define PISTACHIO_CR_PERIPH_DMA_ROUTE(ch)	(0x120 + 0x4 * ((ch) / 4))
#define PISTACHIO_CR_PERIPH_DMA_ROUTE_SHIFT(ch) (8 * ((ch) % 4))
#define PISTACHIO_CR_PERIPH_DMA_ROUTE_MASK	0x3f

static void pistachio_mdc_enable_chan(struct mdc_chan *mchan)
{
	struct mdc_dma *mdma = mchan->mdma;

	regmap_update_bits(mdma->periph_regs,
			   PISTACHIO_CR_PERIPH_DMA_ROUTE(mchan->chan_nr),
			   PISTACHIO_CR_PERIPH_DMA_ROUTE_MASK <<
			   PISTACHIO_CR_PERIPH_DMA_ROUTE_SHIFT(mchan->chan_nr),
			   mchan->periph <<
			   PISTACHIO_CR_PERIPH_DMA_ROUTE_SHIFT(mchan->chan_nr));
}

static void pistachio_mdc_disable_chan(struct mdc_chan *mchan)
{
	struct mdc_dma *mdma = mchan->mdma;

	regmap_update_bits(mdma->periph_regs,
			   PISTACHIO_CR_PERIPH_DMA_ROUTE(mchan->chan_nr),
			   PISTACHIO_CR_PERIPH_DMA_ROUTE_MASK <<
			   PISTACHIO_CR_PERIPH_DMA_ROUTE_SHIFT(mchan->chan_nr),
			   0);
}

static const struct mdc_dma_soc_data pistachio_mdc_data = {
	.enable_chan = pistachio_mdc_enable_chan,
	.disable_chan = pistachio_mdc_disable_chan,
};

static const struct of_device_id mdc_dma_of_match[] = {
	{ .compatible = "img,pistachio-mdc-dma", .data = &pistachio_mdc_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, mdc_dma_of_match);

static int mdc_dma_probe(struct platform_device *pdev)
{
	struct mdc_dma *mdma;
	struct resource *res;
	const struct of_device_id *match;
	unsigned int i;
	u32 val;
	int ret;

	mdma = devm_kzalloc(&pdev->dev, sizeof(*mdma), GFP_KERNEL);
	if (!mdma)
		return -ENOMEM;
	platform_set_drvdata(pdev, mdma);

	match = of_match_device(mdc_dma_of_match, &pdev->dev);
	mdma->soc = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdma->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdma->regs))
		return PTR_ERR(mdma->regs);

	mdma->periph_regs = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							    "img,cr-periph");
	if (IS_ERR(mdma->periph_regs))
		return PTR_ERR(mdma->periph_regs);

	mdma->clk = devm_clk_get(&pdev->dev, "sys");
	if (IS_ERR(mdma->clk))
		return PTR_ERR(mdma->clk);

	ret = clk_prepare_enable(mdma->clk);
	if (ret)
		return ret;

	dma_cap_zero(mdma->dma_dev.cap_mask);
	dma_cap_set(DMA_SLAVE, mdma->dma_dev.cap_mask);
	dma_cap_set(DMA_PRIVATE, mdma->dma_dev.cap_mask);
	dma_cap_set(DMA_CYCLIC, mdma->dma_dev.cap_mask);
	dma_cap_set(DMA_MEMCPY, mdma->dma_dev.cap_mask);

	val = mdc_readl(mdma, MDC_GLOBAL_CONFIG_A);
	mdma->nr_channels = (val >> MDC_GLOBAL_CONFIG_A_DMA_CONTEXTS_SHIFT) &
		MDC_GLOBAL_CONFIG_A_DMA_CONTEXTS_MASK;
	mdma->nr_threads =
		1 << ((val >> MDC_GLOBAL_CONFIG_A_THREAD_ID_WIDTH_SHIFT) &
		      MDC_GLOBAL_CONFIG_A_THREAD_ID_WIDTH_MASK);
	mdma->bus_width =
		(1 << ((val >> MDC_GLOBAL_CONFIG_A_SYS_DAT_WIDTH_SHIFT) &
		       MDC_GLOBAL_CONFIG_A_SYS_DAT_WIDTH_MASK)) / 8;
	/*
	 * Although transfer sizes of up to MDC_TRANSFER_SIZE_MASK + 1 bytes
	 * are supported, this makes it possible for the value reported in
	 * MDC_ACTIVE_TRANSFER_SIZE to be ambiguous - an active transfer size
	 * of MDC_TRANSFER_SIZE_MASK may indicate either that 0 bytes or
	 * MDC_TRANSFER_SIZE_MASK + 1 bytes are remaining.  To eliminate this
	 * ambiguity, restrict transfer sizes to one bus-width less than the
	 * actual maximum.
	 */
	mdma->max_xfer_size = MDC_TRANSFER_SIZE_MASK + 1 - mdma->bus_width;

	of_property_read_u32(pdev->dev.of_node, "dma-channels",
			     &mdma->nr_channels);
	ret = of_property_read_u32(pdev->dev.of_node,
				   "img,max-burst-multiplier",
				   &mdma->max_burst_mult);
	if (ret)
		goto disable_clk;

	mdma->dma_dev.dev = &pdev->dev;
	mdma->dma_dev.device_prep_slave_sg = mdc_prep_slave_sg;
	mdma->dma_dev.device_prep_dma_cyclic = mdc_prep_dma_cyclic;
	mdma->dma_dev.device_prep_dma_memcpy = mdc_prep_dma_memcpy;
	mdma->dma_dev.device_free_chan_resources = mdc_free_chan_resources;
	mdma->dma_dev.device_tx_status = mdc_tx_status;
	mdma->dma_dev.device_issue_pending = mdc_issue_pending;
	mdma->dma_dev.device_terminate_all = mdc_terminate_all;
	mdma->dma_dev.device_config = mdc_slave_config;

	mdma->dma_dev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	mdma->dma_dev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	for (i = 1; i <= mdma->bus_width; i <<= 1) {
		mdma->dma_dev.src_addr_widths |= BIT(i);
		mdma->dma_dev.dst_addr_widths |= BIT(i);
	}

	INIT_LIST_HEAD(&mdma->dma_dev.channels);
	for (i = 0; i < mdma->nr_channels; i++) {
		struct mdc_chan *mchan = &mdma->channels[i];

		mchan->mdma = mdma;
		mchan->chan_nr = i;
		mchan->irq = platform_get_irq(pdev, i);
		if (mchan->irq < 0) {
			ret = mchan->irq;
			goto disable_clk;
		}
		ret = devm_request_irq(&pdev->dev, mchan->irq, mdc_chan_irq,
				       IRQ_TYPE_LEVEL_HIGH,
				       dev_name(&pdev->dev), mchan);
		if (ret < 0)
			goto disable_clk;

		mchan->vc.desc_free = mdc_desc_free;
		vchan_init(&mchan->vc, &mdma->dma_dev);
	}

	mdma->desc_pool = dmam_pool_create(dev_name(&pdev->dev), &pdev->dev,
					   sizeof(struct mdc_hw_list_desc),
					   4, 0);
	if (!mdma->desc_pool) {
		ret = -ENOMEM;
		goto disable_clk;
	}

	ret = dma_async_device_register(&mdma->dma_dev);
	if (ret)
		goto disable_clk;

	ret = of_dma_controller_register(pdev->dev.of_node, mdc_of_xlate, mdma);
	if (ret)
		goto unregister;

	dev_info(&pdev->dev, "MDC with %u channels and %u threads\n",
		 mdma->nr_channels, mdma->nr_threads);

	return 0;

unregister:
	dma_async_device_unregister(&mdma->dma_dev);
disable_clk:
	clk_disable_unprepare(mdma->clk);
	return ret;
}

static int mdc_dma_remove(struct platform_device *pdev)
{
	struct mdc_dma *mdma = platform_get_drvdata(pdev);
	struct mdc_chan *mchan, *next;

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&mdma->dma_dev);

	list_for_each_entry_safe(mchan, next, &mdma->dma_dev.channels,
				 vc.chan.device_node) {
		list_del(&mchan->vc.chan.device_node);

		devm_free_irq(&pdev->dev, mchan->irq, mchan);

		tasklet_kill(&mchan->vc.task);
	}

	clk_disable_unprepare(mdma->clk);

	return 0;
}

static struct platform_driver mdc_dma_driver = {
	.driver = {
		.name = "img-mdc-dma",
		.of_match_table = of_match_ptr(mdc_dma_of_match),
	},
	.probe = mdc_dma_probe,
	.remove = mdc_dma_remove,
};
module_platform_driver(mdc_dma_driver);

MODULE_DESCRIPTION("IMG Multi-threaded DMA Controller (MDC) driver");
MODULE_AUTHOR("Andrew Bresticker <abrestic@chromium.org>");
MODULE_LICENSE("GPL v2");
