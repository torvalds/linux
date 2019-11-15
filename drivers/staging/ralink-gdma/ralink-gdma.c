// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2013, Lars-Peter Clausen <lars@metafoo.de>
 *  GDMA4740 DMAC support
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/of_dma.h>
#include <linux/reset.h>
#include <linux/of_device.h>

#include "virt-dma.h"

#define GDMA_REG_SRC_ADDR(x)		(0x00 + (x) * 0x10)
#define GDMA_REG_DST_ADDR(x)		(0x04 + (x) * 0x10)

#define GDMA_REG_CTRL0(x)		(0x08 + (x) * 0x10)
#define GDMA_REG_CTRL0_TX_MASK		0xffff
#define GDMA_REG_CTRL0_TX_SHIFT		16
#define GDMA_REG_CTRL0_CURR_MASK	0xff
#define GDMA_REG_CTRL0_CURR_SHIFT	8
#define	GDMA_REG_CTRL0_SRC_ADDR_FIXED	BIT(7)
#define GDMA_REG_CTRL0_DST_ADDR_FIXED	BIT(6)
#define GDMA_REG_CTRL0_BURST_MASK	0x7
#define GDMA_REG_CTRL0_BURST_SHIFT	3
#define	GDMA_REG_CTRL0_DONE_INT		BIT(2)
#define	GDMA_REG_CTRL0_ENABLE		BIT(1)
#define GDMA_REG_CTRL0_SW_MODE          BIT(0)

#define GDMA_REG_CTRL1(x)		(0x0c + (x) * 0x10)
#define GDMA_REG_CTRL1_SEG_MASK		0xf
#define GDMA_REG_CTRL1_SEG_SHIFT	22
#define GDMA_REG_CTRL1_REQ_MASK		0x3f
#define GDMA_REG_CTRL1_SRC_REQ_SHIFT	16
#define GDMA_REG_CTRL1_DST_REQ_SHIFT	8
#define GDMA_REG_CTRL1_NEXT_MASK	0x1f
#define GDMA_REG_CTRL1_NEXT_SHIFT	3
#define GDMA_REG_CTRL1_COHERENT		BIT(2)
#define GDMA_REG_CTRL1_FAIL		BIT(1)
#define GDMA_REG_CTRL1_MASK		BIT(0)

#define GDMA_REG_UNMASK_INT		0x200
#define GDMA_REG_DONE_INT		0x204

#define GDMA_REG_GCT			0x220
#define GDMA_REG_GCT_CHAN_MASK		0x3
#define GDMA_REG_GCT_CHAN_SHIFT		3
#define GDMA_REG_GCT_VER_MASK		0x3
#define GDMA_REG_GCT_VER_SHIFT		1
#define GDMA_REG_GCT_ARBIT_RR		BIT(0)

#define GDMA_REG_REQSTS			0x2a0
#define GDMA_REG_ACKSTS			0x2a4
#define GDMA_REG_FINSTS			0x2a8

/* for RT305X gdma registers */
#define GDMA_RT305X_CTRL0_REQ_MASK	0xf
#define GDMA_RT305X_CTRL0_SRC_REQ_SHIFT	12
#define GDMA_RT305X_CTRL0_DST_REQ_SHIFT	8

#define GDMA_RT305X_CTRL1_FAIL		BIT(4)
#define GDMA_RT305X_CTRL1_NEXT_MASK	0x7
#define GDMA_RT305X_CTRL1_NEXT_SHIFT	1

#define GDMA_RT305X_STATUS_INT		0x80
#define GDMA_RT305X_STATUS_SIGNAL	0x84
#define GDMA_RT305X_GCT			0x88

/* for MT7621 gdma registers */
#define GDMA_REG_PERF_START(x)		(0x230 + (x) * 0x8)
#define GDMA_REG_PERF_END(x)		(0x234 + (x) * 0x8)

enum gdma_dma_transfer_size {
	GDMA_TRANSFER_SIZE_4BYTE	= 0,
	GDMA_TRANSFER_SIZE_8BYTE	= 1,
	GDMA_TRANSFER_SIZE_16BYTE	= 2,
	GDMA_TRANSFER_SIZE_32BYTE	= 3,
	GDMA_TRANSFER_SIZE_64BYTE	= 4,
};

struct gdma_dma_sg {
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	u32 len;
};

struct gdma_dma_desc {
	struct virt_dma_desc vdesc;

	enum dma_transfer_direction direction;
	bool cyclic;

	u32 residue;
	unsigned int num_sgs;
	struct gdma_dma_sg sg[];
};

struct gdma_dmaengine_chan {
	struct virt_dma_chan vchan;
	unsigned int id;
	unsigned int slave_id;

	dma_addr_t fifo_addr;
	enum gdma_dma_transfer_size burst_size;

	struct gdma_dma_desc *desc;
	unsigned int next_sg;
};

struct gdma_dma_dev {
	struct dma_device ddev;
	struct device_dma_parameters dma_parms;
	struct gdma_data *data;
	void __iomem *base;
	struct tasklet_struct task;
	volatile unsigned long chan_issued;
	atomic_t cnt;

	struct gdma_dmaengine_chan chan[];
};

struct gdma_data {
	int chancnt;
	u32 done_int_reg;
	void (*init)(struct gdma_dma_dev *dma_dev);
	int (*start_transfer)(struct gdma_dmaengine_chan *chan);
};

static struct gdma_dma_dev *gdma_dma_chan_get_dev(
	struct gdma_dmaengine_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct gdma_dma_dev,
		ddev);
}

static struct gdma_dmaengine_chan *to_gdma_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct gdma_dmaengine_chan, vchan.chan);
}

static struct gdma_dma_desc *to_gdma_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct gdma_dma_desc, vdesc);
}

static inline uint32_t gdma_dma_read(struct gdma_dma_dev *dma_dev,
				     unsigned int reg)
{
	return readl(dma_dev->base + reg);
}

static inline void gdma_dma_write(struct gdma_dma_dev *dma_dev,
				  unsigned int reg, uint32_t val)
{
	writel(val, dma_dev->base + reg);
}

static enum gdma_dma_transfer_size gdma_dma_maxburst(u32 maxburst)
{
	if (maxburst < 2)
		return GDMA_TRANSFER_SIZE_4BYTE;
	else if (maxburst < 4)
		return GDMA_TRANSFER_SIZE_8BYTE;
	else if (maxburst < 8)
		return GDMA_TRANSFER_SIZE_16BYTE;
	else if (maxburst < 16)
		return GDMA_TRANSFER_SIZE_32BYTE;
	else
		return GDMA_TRANSFER_SIZE_64BYTE;
}

static int gdma_dma_config(struct dma_chan *c,
			   struct dma_slave_config *config)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_dev *dma_dev = gdma_dma_chan_get_dev(chan);

	if (config->device_fc) {
		dev_err(dma_dev->ddev.dev, "not support flow controller\n");
		return -EINVAL;
	}

	switch (config->direction) {
	case DMA_MEM_TO_DEV:
		if (config->dst_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES) {
			dev_err(dma_dev->ddev.dev, "only support 4 byte buswidth\n");
			return -EINVAL;
		}
		chan->slave_id = config->slave_id;
		chan->fifo_addr = config->dst_addr;
		chan->burst_size = gdma_dma_maxburst(config->dst_maxburst);
		break;
	case DMA_DEV_TO_MEM:
		if (config->src_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES) {
			dev_err(dma_dev->ddev.dev, "only support 4 byte buswidth\n");
			return -EINVAL;
		}
		chan->slave_id = config->slave_id;
		chan->fifo_addr = config->src_addr;
		chan->burst_size = gdma_dma_maxburst(config->src_maxburst);
		break;
	default:
		dev_err(dma_dev->ddev.dev, "direction type %d error\n",
			config->direction);
		return -EINVAL;
	}

	return 0;
}

static int gdma_dma_terminate_all(struct dma_chan *c)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_dev *dma_dev = gdma_dma_chan_get_dev(chan);
	unsigned long flags, timeout;
	LIST_HEAD(head);
	int i = 0;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	chan->desc = NULL;
	clear_bit(chan->id, &dma_dev->chan_issued);
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	/* wait dma transfer complete */
	timeout = jiffies + msecs_to_jiffies(5000);
	while (gdma_dma_read(dma_dev, GDMA_REG_CTRL0(chan->id)) &
			GDMA_REG_CTRL0_ENABLE) {
		if (time_after_eq(jiffies, timeout)) {
			dev_err(dma_dev->ddev.dev, "chan %d wait timeout\n",
				chan->id);
			/* restore to init value */
			gdma_dma_write(dma_dev, GDMA_REG_CTRL0(chan->id), 0);
			break;
		}
		cpu_relax();
		i++;
	}

	if (i)
		dev_dbg(dma_dev->ddev.dev, "terminate chan %d loops %d\n",
			chan->id, i);

	return 0;
}

static void rt305x_dump_reg(struct gdma_dma_dev *dma_dev, int id)
{
	dev_dbg(dma_dev->ddev.dev, "chan %d, src %08x, dst %08x, ctr0 %08x, ctr1 %08x, intr %08x, signal %08x\n",
		id,
		gdma_dma_read(dma_dev, GDMA_REG_SRC_ADDR(id)),
		gdma_dma_read(dma_dev, GDMA_REG_DST_ADDR(id)),
		gdma_dma_read(dma_dev, GDMA_REG_CTRL0(id)),
		gdma_dma_read(dma_dev, GDMA_REG_CTRL1(id)),
		gdma_dma_read(dma_dev, GDMA_RT305X_STATUS_INT),
		gdma_dma_read(dma_dev, GDMA_RT305X_STATUS_SIGNAL));
}

static int rt305x_gdma_start_transfer(struct gdma_dmaengine_chan *chan)
{
	struct gdma_dma_dev *dma_dev = gdma_dma_chan_get_dev(chan);
	dma_addr_t src_addr, dst_addr;
	struct gdma_dma_sg *sg;
	u32 ctrl0, ctrl1;

	/* verify chan is already stopped */
	ctrl0 = gdma_dma_read(dma_dev, GDMA_REG_CTRL0(chan->id));
	if (unlikely(ctrl0 & GDMA_REG_CTRL0_ENABLE)) {
		dev_err(dma_dev->ddev.dev, "chan %d is start(%08x).\n",
			chan->id, ctrl0);
		rt305x_dump_reg(dma_dev, chan->id);
		return -EINVAL;
	}

	sg = &chan->desc->sg[chan->next_sg];
	if (chan->desc->direction == DMA_MEM_TO_DEV) {
		src_addr = sg->src_addr;
		dst_addr = chan->fifo_addr;
		ctrl0 = GDMA_REG_CTRL0_DST_ADDR_FIXED |
			(8 << GDMA_RT305X_CTRL0_SRC_REQ_SHIFT) |
			(chan->slave_id << GDMA_RT305X_CTRL0_DST_REQ_SHIFT);
	} else if (chan->desc->direction == DMA_DEV_TO_MEM) {
		src_addr = chan->fifo_addr;
		dst_addr = sg->dst_addr;
		ctrl0 = GDMA_REG_CTRL0_SRC_ADDR_FIXED |
			(chan->slave_id << GDMA_RT305X_CTRL0_SRC_REQ_SHIFT) |
			(8 << GDMA_RT305X_CTRL0_DST_REQ_SHIFT);
	} else if (chan->desc->direction == DMA_MEM_TO_MEM) {
		/*
		 * TODO: memcpy function have bugs. sometime it will copy
		 * more 8 bytes data when using dmatest verify.
		 */
		src_addr = sg->src_addr;
		dst_addr = sg->dst_addr;
		ctrl0 = GDMA_REG_CTRL0_SW_MODE |
			(8 << GDMA_REG_CTRL1_SRC_REQ_SHIFT) |
			(8 << GDMA_REG_CTRL1_DST_REQ_SHIFT);
	} else {
		dev_err(dma_dev->ddev.dev, "direction type %d error\n",
			chan->desc->direction);
		return -EINVAL;
	}

	ctrl0 |= (sg->len << GDMA_REG_CTRL0_TX_SHIFT) |
		 (chan->burst_size << GDMA_REG_CTRL0_BURST_SHIFT) |
		 GDMA_REG_CTRL0_DONE_INT | GDMA_REG_CTRL0_ENABLE;
	ctrl1 = chan->id << GDMA_REG_CTRL1_NEXT_SHIFT;

	chan->next_sg++;
	gdma_dma_write(dma_dev, GDMA_REG_SRC_ADDR(chan->id), src_addr);
	gdma_dma_write(dma_dev, GDMA_REG_DST_ADDR(chan->id), dst_addr);
	gdma_dma_write(dma_dev, GDMA_REG_CTRL1(chan->id), ctrl1);

	/* make sure next_sg is update */
	wmb();
	gdma_dma_write(dma_dev, GDMA_REG_CTRL0(chan->id), ctrl0);

	return 0;
}

static void rt3883_dump_reg(struct gdma_dma_dev *dma_dev, int id)
{
	dev_dbg(dma_dev->ddev.dev, "chan %d, src %08x, dst %08x, ctr0 %08x, ctr1 %08x, unmask %08x, done %08x, req %08x, ack %08x, fin %08x\n",
		id,
		gdma_dma_read(dma_dev, GDMA_REG_SRC_ADDR(id)),
		gdma_dma_read(dma_dev, GDMA_REG_DST_ADDR(id)),
		gdma_dma_read(dma_dev, GDMA_REG_CTRL0(id)),
		gdma_dma_read(dma_dev, GDMA_REG_CTRL1(id)),
		gdma_dma_read(dma_dev, GDMA_REG_UNMASK_INT),
		gdma_dma_read(dma_dev, GDMA_REG_DONE_INT),
		gdma_dma_read(dma_dev, GDMA_REG_REQSTS),
		gdma_dma_read(dma_dev, GDMA_REG_ACKSTS),
		gdma_dma_read(dma_dev, GDMA_REG_FINSTS));
}

static int rt3883_gdma_start_transfer(struct gdma_dmaengine_chan *chan)
{
	struct gdma_dma_dev *dma_dev = gdma_dma_chan_get_dev(chan);
	dma_addr_t src_addr, dst_addr;
	struct gdma_dma_sg *sg;
	u32 ctrl0, ctrl1;

	/* verify chan is already stopped */
	ctrl0 = gdma_dma_read(dma_dev, GDMA_REG_CTRL0(chan->id));
	if (unlikely(ctrl0 & GDMA_REG_CTRL0_ENABLE)) {
		dev_err(dma_dev->ddev.dev, "chan %d is start(%08x).\n",
			chan->id, ctrl0);
		rt3883_dump_reg(dma_dev, chan->id);
		return -EINVAL;
	}

	sg = &chan->desc->sg[chan->next_sg];
	if (chan->desc->direction == DMA_MEM_TO_DEV) {
		src_addr = sg->src_addr;
		dst_addr = chan->fifo_addr;
		ctrl0 = GDMA_REG_CTRL0_DST_ADDR_FIXED;
		ctrl1 = (32 << GDMA_REG_CTRL1_SRC_REQ_SHIFT) |
			(chan->slave_id << GDMA_REG_CTRL1_DST_REQ_SHIFT);
	} else if (chan->desc->direction == DMA_DEV_TO_MEM) {
		src_addr = chan->fifo_addr;
		dst_addr = sg->dst_addr;
		ctrl0 = GDMA_REG_CTRL0_SRC_ADDR_FIXED;
		ctrl1 = (chan->slave_id << GDMA_REG_CTRL1_SRC_REQ_SHIFT) |
			(32 << GDMA_REG_CTRL1_DST_REQ_SHIFT) |
			GDMA_REG_CTRL1_COHERENT;
	} else if (chan->desc->direction == DMA_MEM_TO_MEM) {
		src_addr = sg->src_addr;
		dst_addr = sg->dst_addr;
		ctrl0 = GDMA_REG_CTRL0_SW_MODE;
		ctrl1 = (32 << GDMA_REG_CTRL1_SRC_REQ_SHIFT) |
			(32 << GDMA_REG_CTRL1_DST_REQ_SHIFT) |
			GDMA_REG_CTRL1_COHERENT;
	} else {
		dev_err(dma_dev->ddev.dev, "direction type %d error\n",
			chan->desc->direction);
		return -EINVAL;
	}

	ctrl0 |= (sg->len << GDMA_REG_CTRL0_TX_SHIFT) |
		 (chan->burst_size << GDMA_REG_CTRL0_BURST_SHIFT) |
		 GDMA_REG_CTRL0_DONE_INT | GDMA_REG_CTRL0_ENABLE;
	ctrl1 |= chan->id << GDMA_REG_CTRL1_NEXT_SHIFT;

	chan->next_sg++;
	gdma_dma_write(dma_dev, GDMA_REG_SRC_ADDR(chan->id), src_addr);
	gdma_dma_write(dma_dev, GDMA_REG_DST_ADDR(chan->id), dst_addr);
	gdma_dma_write(dma_dev, GDMA_REG_CTRL1(chan->id), ctrl1);

	/* make sure next_sg is update */
	wmb();
	gdma_dma_write(dma_dev, GDMA_REG_CTRL0(chan->id), ctrl0);

	return 0;
}

static inline int gdma_start_transfer(struct gdma_dma_dev *dma_dev,
				      struct gdma_dmaengine_chan *chan)
{
	return dma_dev->data->start_transfer(chan);
}

static int gdma_next_desc(struct gdma_dmaengine_chan *chan)
{
	struct virt_dma_desc *vdesc;

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc) {
		chan->desc = NULL;
		return 0;
	}
	chan->desc = to_gdma_dma_desc(vdesc);
	chan->next_sg = 0;

	return 1;
}

static void gdma_dma_chan_irq(struct gdma_dma_dev *dma_dev,
			      struct gdma_dmaengine_chan *chan)
{
	struct gdma_dma_desc *desc;
	unsigned long flags;
	int chan_issued;

	chan_issued = 0;
	spin_lock_irqsave(&chan->vchan.lock, flags);
	desc = chan->desc;
	if (desc) {
		if (desc->cyclic) {
			vchan_cyclic_callback(&desc->vdesc);
			if (chan->next_sg == desc->num_sgs)
				chan->next_sg = 0;
			chan_issued = 1;
		} else {
			desc->residue -= desc->sg[chan->next_sg - 1].len;
			if (chan->next_sg == desc->num_sgs) {
				list_del(&desc->vdesc.node);
				vchan_cookie_complete(&desc->vdesc);
				chan_issued = gdma_next_desc(chan);
			} else {
				chan_issued = 1;
			}
		}
	} else {
		dev_dbg(dma_dev->ddev.dev, "chan %d no desc to complete\n",
			chan->id);
	}
	if (chan_issued)
		set_bit(chan->id, &dma_dev->chan_issued);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static irqreturn_t gdma_dma_irq(int irq, void *devid)
{
	struct gdma_dma_dev *dma_dev = devid;
	u32 done, done_reg;
	unsigned int i;

	done_reg = dma_dev->data->done_int_reg;
	done = gdma_dma_read(dma_dev, done_reg);
	if (unlikely(!done))
		return IRQ_NONE;

	/* clean done bits */
	gdma_dma_write(dma_dev, done_reg, done);

	i = 0;
	while (done) {
		if (done & 0x1) {
			gdma_dma_chan_irq(dma_dev, &dma_dev->chan[i]);
			atomic_dec(&dma_dev->cnt);
		}
		done >>= 1;
		i++;
	}

	/* start only have work to do */
	if (dma_dev->chan_issued)
		tasklet_schedule(&dma_dev->task);

	return IRQ_HANDLED;
}

static void gdma_dma_issue_pending(struct dma_chan *c)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_dev *dma_dev = gdma_dma_chan_get_dev(chan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (vchan_issue_pending(&chan->vchan) && !chan->desc) {
		if (gdma_next_desc(chan)) {
			set_bit(chan->id, &dma_dev->chan_issued);
			tasklet_schedule(&dma_dev->task);
		} else {
			dev_dbg(dma_dev->ddev.dev, "chan %d no desc to issue\n",
				chan->id);
		}
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static struct dma_async_tx_descriptor *gdma_dma_prep_slave_sg(
		struct dma_chan *c, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_desc *desc;
	struct scatterlist *sg;
	unsigned int i;

	desc = kzalloc(struct_size(desc, sg, sg_len), GFP_ATOMIC);
	if (!desc) {
		dev_err(c->device->dev, "alloc sg decs error\n");
		return NULL;
	}
	desc->residue = 0;

	for_each_sg(sgl, sg, sg_len, i) {
		if (direction == DMA_MEM_TO_DEV) {
			desc->sg[i].src_addr = sg_dma_address(sg);
		} else if (direction == DMA_DEV_TO_MEM) {
			desc->sg[i].dst_addr = sg_dma_address(sg);
		} else {
			dev_err(c->device->dev, "direction type %d error\n",
				direction);
			goto free_desc;
		}

		if (unlikely(sg_dma_len(sg) > GDMA_REG_CTRL0_TX_MASK)) {
			dev_err(c->device->dev, "sg len too large %d\n",
				sg_dma_len(sg));
			goto free_desc;
		}
		desc->sg[i].len = sg_dma_len(sg);
		desc->residue += sg_dma_len(sg);
	}

	desc->num_sgs = sg_len;
	desc->direction = direction;
	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

free_desc:
	kfree(desc);
	return NULL;
}

static struct dma_async_tx_descriptor *gdma_dma_prep_dma_memcpy(
		struct dma_chan *c, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_desc *desc;
	unsigned int num_periods, i;
	size_t xfer_count;

	if (len <= 0)
		return NULL;

	chan->burst_size = gdma_dma_maxburst(len >> 2);

	xfer_count = GDMA_REG_CTRL0_TX_MASK;
	num_periods = DIV_ROUND_UP(len, xfer_count);

	desc = kzalloc(struct_size(desc, sg, num_periods), GFP_ATOMIC);
	if (!desc) {
		dev_err(c->device->dev, "alloc memcpy decs error\n");
		return NULL;
	}
	desc->residue = len;

	for (i = 0; i < num_periods; i++) {
		desc->sg[i].src_addr = src;
		desc->sg[i].dst_addr = dest;
		if (len > xfer_count)
			desc->sg[i].len = xfer_count;
		else
			desc->sg[i].len = len;
		src += desc->sg[i].len;
		dest += desc->sg[i].len;
		len -= desc->sg[i].len;
	}

	desc->num_sgs = num_periods;
	desc->direction = DMA_MEM_TO_MEM;
	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *gdma_dma_prep_dma_cyclic(
	struct dma_chan *c, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct gdma_dma_desc *desc;
	unsigned int num_periods, i;

	if (buf_len % period_len)
		return NULL;

	if (period_len > GDMA_REG_CTRL0_TX_MASK) {
		dev_err(c->device->dev, "cyclic len too large %d\n",
			period_len);
		return NULL;
	}

	num_periods = buf_len / period_len;
	desc = kzalloc(struct_size(desc, sg, num_periods), GFP_ATOMIC);
	if (!desc) {
		dev_err(c->device->dev, "alloc cyclic decs error\n");
		return NULL;
	}
	desc->residue = buf_len;

	for (i = 0; i < num_periods; i++) {
		if (direction == DMA_MEM_TO_DEV) {
			desc->sg[i].src_addr = buf_addr;
		} else if (direction == DMA_DEV_TO_MEM) {
			desc->sg[i].dst_addr = buf_addr;
		} else {
			dev_err(c->device->dev, "direction type %d error\n",
				direction);
			goto free_desc;
		}
		desc->sg[i].len = period_len;
		buf_addr += period_len;
	}

	desc->num_sgs = num_periods;
	desc->direction = direction;
	desc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

free_desc:
	kfree(desc);
	return NULL;
}

static enum dma_status gdma_dma_tx_status(struct dma_chan *c,
					  dma_cookie_t cookie,
					  struct dma_tx_state *state)
{
	struct gdma_dmaengine_chan *chan = to_gdma_dma_chan(c);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;
	struct gdma_dma_desc *desc;

	status = dma_cookie_status(c, cookie, state);
	if (status == DMA_COMPLETE || !state)
		return status;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	desc = chan->desc;
	if (desc && (cookie == desc->vdesc.tx.cookie)) {
		/*
		 * We never update edesc->residue in the cyclic case, so we
		 * can tell the remaining room to the end of the circular
		 * buffer.
		 */
		if (desc->cyclic)
			state->residue = desc->residue -
				((chan->next_sg - 1) * desc->sg[0].len);
		else
			state->residue = desc->residue;
	} else {
		vdesc = vchan_find_desc(&chan->vchan, cookie);
		if (vdesc)
			state->residue = to_gdma_dma_desc(vdesc)->residue;
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	dev_dbg(c->device->dev, "tx residue %d bytes\n", state->residue);

	return status;
}

static void gdma_dma_free_chan_resources(struct dma_chan *c)
{
	vchan_free_chan_resources(to_virt_chan(c));
}

static void gdma_dma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct gdma_dma_desc, vdesc));
}

static void gdma_dma_tasklet(unsigned long arg)
{
	struct gdma_dma_dev *dma_dev = (struct gdma_dma_dev *)arg;
	struct gdma_dmaengine_chan *chan;
	static unsigned int last_chan;
	unsigned int i, chan_mask;

	/* record last chan to round robin all chans */
	i = last_chan;
	chan_mask = dma_dev->data->chancnt - 1;
	do {
		/*
		 * on mt7621. when verify with dmatest with all
		 * channel is enable. we need to limit only two
		 * channel is working at the same time. otherwise the
		 * data will have problem.
		 */
		if (atomic_read(&dma_dev->cnt) >= 2) {
			last_chan = i;
			break;
		}

		if (test_and_clear_bit(i, &dma_dev->chan_issued)) {
			chan = &dma_dev->chan[i];
			if (chan->desc) {
				atomic_inc(&dma_dev->cnt);
				gdma_start_transfer(dma_dev, chan);
			} else {
				dev_dbg(dma_dev->ddev.dev,
					"chan %d no desc to issue\n",
					chan->id);
			}
			if (!dma_dev->chan_issued)
				break;
		}

		i = (i + 1) & chan_mask;
	} while (i != last_chan);
}

static void rt305x_gdma_init(struct gdma_dma_dev *dma_dev)
{
	u32 gct;

	/* all chans round robin */
	gdma_dma_write(dma_dev, GDMA_RT305X_GCT, GDMA_REG_GCT_ARBIT_RR);

	gct = gdma_dma_read(dma_dev, GDMA_RT305X_GCT);
	dev_info(dma_dev->ddev.dev, "revision: %d, channels: %d\n",
		 (gct >> GDMA_REG_GCT_VER_SHIFT) & GDMA_REG_GCT_VER_MASK,
		 8 << ((gct >> GDMA_REG_GCT_CHAN_SHIFT) &
			GDMA_REG_GCT_CHAN_MASK));
}

static void rt3883_gdma_init(struct gdma_dma_dev *dma_dev)
{
	u32 gct;

	/* all chans round robin */
	gdma_dma_write(dma_dev, GDMA_REG_GCT, GDMA_REG_GCT_ARBIT_RR);

	gct = gdma_dma_read(dma_dev, GDMA_REG_GCT);
	dev_info(dma_dev->ddev.dev, "revision: %d, channels: %d\n",
		 (gct >> GDMA_REG_GCT_VER_SHIFT) & GDMA_REG_GCT_VER_MASK,
		 8 << ((gct >> GDMA_REG_GCT_CHAN_SHIFT) &
			GDMA_REG_GCT_CHAN_MASK));
}

static struct gdma_data rt305x_gdma_data = {
	.chancnt = 8,
	.done_int_reg = GDMA_RT305X_STATUS_INT,
	.init = rt305x_gdma_init,
	.start_transfer = rt305x_gdma_start_transfer,
};

static struct gdma_data rt3883_gdma_data = {
	.chancnt = 16,
	.done_int_reg = GDMA_REG_DONE_INT,
	.init = rt3883_gdma_init,
	.start_transfer = rt3883_gdma_start_transfer,
};

static const struct of_device_id gdma_of_match_table[] = {
	{ .compatible = "ralink,rt305x-gdma", .data = &rt305x_gdma_data },
	{ .compatible = "ralink,rt3883-gdma", .data = &rt3883_gdma_data },
	{ },
};

static int gdma_dma_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct gdma_dmaengine_chan *chan;
	struct gdma_dma_dev *dma_dev;
	struct dma_device *dd;
	unsigned int i;
	struct resource *res;
	int ret;
	int irq;
	void __iomem *base;
	struct gdma_data *data;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	match = of_match_device(gdma_of_match_table, &pdev->dev);
	if (!match)
		return -EINVAL;
	data = (struct gdma_data *)match->data;

	dma_dev = devm_kzalloc(&pdev->dev,
			       struct_size(dma_dev, chan, data->chancnt),
			       GFP_KERNEL);
	if (!dma_dev)
		return -EINVAL;
	dma_dev->data = data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	dma_dev->base = base;
	tasklet_init(&dma_dev->task, gdma_dma_tasklet, (unsigned long)dma_dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;
	ret = devm_request_irq(&pdev->dev, irq, gdma_dma_irq,
			       0, dev_name(&pdev->dev), dma_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return ret;
	}

	device_reset(&pdev->dev);

	dd = &dma_dev->ddev;
	dma_cap_set(DMA_MEMCPY, dd->cap_mask);
	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dd->device_free_chan_resources = gdma_dma_free_chan_resources;
	dd->device_prep_dma_memcpy = gdma_dma_prep_dma_memcpy;
	dd->device_prep_slave_sg = gdma_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = gdma_dma_prep_dma_cyclic;
	dd->device_config = gdma_dma_config;
	dd->device_terminate_all = gdma_dma_terminate_all;
	dd->device_tx_status = gdma_dma_tx_status;
	dd->device_issue_pending = gdma_dma_issue_pending;

	dd->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;

	dd->dev = &pdev->dev;
	dd->dev->dma_parms = &dma_dev->dma_parms;
	dma_set_max_seg_size(dd->dev, GDMA_REG_CTRL0_TX_MASK);
	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < data->chancnt; i++) {
		chan = &dma_dev->chan[i];
		chan->id = i;
		chan->vchan.desc_free = gdma_dma_desc_free;
		vchan_init(&chan->vchan, dd);
	}

	/* init hardware */
	data->init(dma_dev);

	ret = dma_async_device_register(dd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register dma device\n");
		return ret;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id, dma_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register of dma controller\n");
		goto err_unregister;
	}

	platform_set_drvdata(pdev, dma_dev);

	return 0;

err_unregister:
	dma_async_device_unregister(dd);
	return ret;
}

static int gdma_dma_remove(struct platform_device *pdev)
{
	struct gdma_dma_dev *dma_dev = platform_get_drvdata(pdev);

	tasklet_kill(&dma_dev->task);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&dma_dev->ddev);

	return 0;
}

static struct platform_driver gdma_dma_driver = {
	.probe = gdma_dma_probe,
	.remove = gdma_dma_remove,
	.driver = {
		.name = "gdma-rt2880",
		.of_match_table = gdma_of_match_table,
	},
};
module_platform_driver(gdma_dma_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ralink/MTK DMA driver");
MODULE_LICENSE("GPL v2");
