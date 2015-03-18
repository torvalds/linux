/*
 *  Copyright (C) 2013, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 DMAC support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
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
#include <linux/clk.h>

#include <asm/mach-jz4740/dma.h>

#include "virt-dma.h"

#define JZ_DMA_NR_CHANS 6

#define JZ_REG_DMA_SRC_ADDR(x)		(0x00 + (x) * 0x20)
#define JZ_REG_DMA_DST_ADDR(x)		(0x04 + (x) * 0x20)
#define JZ_REG_DMA_TRANSFER_COUNT(x)	(0x08 + (x) * 0x20)
#define JZ_REG_DMA_REQ_TYPE(x)		(0x0C + (x) * 0x20)
#define JZ_REG_DMA_STATUS_CTRL(x)	(0x10 + (x) * 0x20)
#define JZ_REG_DMA_CMD(x)		(0x14 + (x) * 0x20)
#define JZ_REG_DMA_DESC_ADDR(x)		(0x18 + (x) * 0x20)

#define JZ_REG_DMA_CTRL			0x300
#define JZ_REG_DMA_IRQ			0x304
#define JZ_REG_DMA_DOORBELL		0x308
#define JZ_REG_DMA_DOORBELL_SET		0x30C

#define JZ_DMA_STATUS_CTRL_NO_DESC		BIT(31)
#define JZ_DMA_STATUS_CTRL_DESC_INV		BIT(6)
#define JZ_DMA_STATUS_CTRL_ADDR_ERR		BIT(4)
#define JZ_DMA_STATUS_CTRL_TRANSFER_DONE	BIT(3)
#define JZ_DMA_STATUS_CTRL_HALT			BIT(2)
#define JZ_DMA_STATUS_CTRL_COUNT_TERMINATE	BIT(1)
#define JZ_DMA_STATUS_CTRL_ENABLE		BIT(0)

#define JZ_DMA_CMD_SRC_INC			BIT(23)
#define JZ_DMA_CMD_DST_INC			BIT(22)
#define JZ_DMA_CMD_RDIL_MASK			(0xf << 16)
#define JZ_DMA_CMD_SRC_WIDTH_MASK		(0x3 << 14)
#define JZ_DMA_CMD_DST_WIDTH_MASK		(0x3 << 12)
#define JZ_DMA_CMD_INTERVAL_LENGTH_MASK		(0x7 << 8)
#define JZ_DMA_CMD_BLOCK_MODE			BIT(7)
#define JZ_DMA_CMD_DESC_VALID			BIT(4)
#define JZ_DMA_CMD_DESC_VALID_MODE		BIT(3)
#define JZ_DMA_CMD_VALID_IRQ_ENABLE		BIT(2)
#define JZ_DMA_CMD_TRANSFER_IRQ_ENABLE		BIT(1)
#define JZ_DMA_CMD_LINK_ENABLE			BIT(0)

#define JZ_DMA_CMD_FLAGS_OFFSET 22
#define JZ_DMA_CMD_RDIL_OFFSET 16
#define JZ_DMA_CMD_SRC_WIDTH_OFFSET 14
#define JZ_DMA_CMD_DST_WIDTH_OFFSET 12
#define JZ_DMA_CMD_TRANSFER_SIZE_OFFSET 8
#define JZ_DMA_CMD_MODE_OFFSET 7

#define JZ_DMA_CTRL_PRIORITY_MASK		(0x3 << 8)
#define JZ_DMA_CTRL_HALT			BIT(3)
#define JZ_DMA_CTRL_ADDRESS_ERROR		BIT(2)
#define JZ_DMA_CTRL_ENABLE			BIT(0)

enum jz4740_dma_width {
	JZ4740_DMA_WIDTH_32BIT	= 0,
	JZ4740_DMA_WIDTH_8BIT	= 1,
	JZ4740_DMA_WIDTH_16BIT	= 2,
};

enum jz4740_dma_transfer_size {
	JZ4740_DMA_TRANSFER_SIZE_4BYTE	= 0,
	JZ4740_DMA_TRANSFER_SIZE_1BYTE	= 1,
	JZ4740_DMA_TRANSFER_SIZE_2BYTE	= 2,
	JZ4740_DMA_TRANSFER_SIZE_16BYTE = 3,
	JZ4740_DMA_TRANSFER_SIZE_32BYTE = 4,
};

enum jz4740_dma_flags {
	JZ4740_DMA_SRC_AUTOINC = 0x2,
	JZ4740_DMA_DST_AUTOINC = 0x1,
};

enum jz4740_dma_mode {
	JZ4740_DMA_MODE_SINGLE	= 0,
	JZ4740_DMA_MODE_BLOCK	= 1,
};

struct jz4740_dma_sg {
	dma_addr_t addr;
	unsigned int len;
};

struct jz4740_dma_desc {
	struct virt_dma_desc vdesc;

	enum dma_transfer_direction direction;
	bool cyclic;

	unsigned int num_sgs;
	struct jz4740_dma_sg sg[];
};

struct jz4740_dmaengine_chan {
	struct virt_dma_chan vchan;
	unsigned int id;

	dma_addr_t fifo_addr;
	unsigned int transfer_shift;

	struct jz4740_dma_desc *desc;
	unsigned int next_sg;
};

struct jz4740_dma_dev {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *clk;

	struct jz4740_dmaengine_chan chan[JZ_DMA_NR_CHANS];
};

static struct jz4740_dma_dev *jz4740_dma_chan_get_dev(
	struct jz4740_dmaengine_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct jz4740_dma_dev,
		ddev);
}

static struct jz4740_dmaengine_chan *to_jz4740_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct jz4740_dmaengine_chan, vchan.chan);
}

static struct jz4740_dma_desc *to_jz4740_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct jz4740_dma_desc, vdesc);
}

static inline uint32_t jz4740_dma_read(struct jz4740_dma_dev *dmadev,
	unsigned int reg)
{
	return readl(dmadev->base + reg);
}

static inline void jz4740_dma_write(struct jz4740_dma_dev *dmadev,
	unsigned reg, uint32_t val)
{
	writel(val, dmadev->base + reg);
}

static inline void jz4740_dma_write_mask(struct jz4740_dma_dev *dmadev,
	unsigned int reg, uint32_t val, uint32_t mask)
{
	uint32_t tmp;

	tmp = jz4740_dma_read(dmadev, reg);
	tmp &= ~mask;
	tmp |= val;
	jz4740_dma_write(dmadev, reg, tmp);
}

static struct jz4740_dma_desc *jz4740_dma_alloc_desc(unsigned int num_sgs)
{
	return kzalloc(sizeof(struct jz4740_dma_desc) +
		sizeof(struct jz4740_dma_sg) * num_sgs, GFP_ATOMIC);
}

static enum jz4740_dma_width jz4740_dma_width(enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return JZ4740_DMA_WIDTH_8BIT;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return JZ4740_DMA_WIDTH_16BIT;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return JZ4740_DMA_WIDTH_32BIT;
	default:
		return JZ4740_DMA_WIDTH_32BIT;
	}
}

static enum jz4740_dma_transfer_size jz4740_dma_maxburst(u32 maxburst)
{
	if (maxburst <= 1)
		return JZ4740_DMA_TRANSFER_SIZE_1BYTE;
	else if (maxburst <= 3)
		return JZ4740_DMA_TRANSFER_SIZE_2BYTE;
	else if (maxburst <= 15)
		return JZ4740_DMA_TRANSFER_SIZE_4BYTE;
	else if (maxburst <= 31)
		return JZ4740_DMA_TRANSFER_SIZE_16BYTE;

	return JZ4740_DMA_TRANSFER_SIZE_32BYTE;
}

static int jz4740_dma_slave_config(struct dma_chan *c,
				   struct dma_slave_config *config)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct jz4740_dma_dev *dmadev = jz4740_dma_chan_get_dev(chan);
	enum jz4740_dma_width src_width;
	enum jz4740_dma_width dst_width;
	enum jz4740_dma_transfer_size transfer_size;
	enum jz4740_dma_flags flags;
	uint32_t cmd;

	switch (config->direction) {
	case DMA_MEM_TO_DEV:
		flags = JZ4740_DMA_SRC_AUTOINC;
		transfer_size = jz4740_dma_maxburst(config->dst_maxburst);
		chan->fifo_addr = config->dst_addr;
		break;
	case DMA_DEV_TO_MEM:
		flags = JZ4740_DMA_DST_AUTOINC;
		transfer_size = jz4740_dma_maxburst(config->src_maxburst);
		chan->fifo_addr = config->src_addr;
		break;
	default:
		return -EINVAL;
	}

	src_width = jz4740_dma_width(config->src_addr_width);
	dst_width = jz4740_dma_width(config->dst_addr_width);

	switch (transfer_size) {
	case JZ4740_DMA_TRANSFER_SIZE_2BYTE:
		chan->transfer_shift = 1;
		break;
	case JZ4740_DMA_TRANSFER_SIZE_4BYTE:
		chan->transfer_shift = 2;
		break;
	case JZ4740_DMA_TRANSFER_SIZE_16BYTE:
		chan->transfer_shift = 4;
		break;
	case JZ4740_DMA_TRANSFER_SIZE_32BYTE:
		chan->transfer_shift = 5;
		break;
	default:
		chan->transfer_shift = 0;
		break;
	}

	cmd = flags << JZ_DMA_CMD_FLAGS_OFFSET;
	cmd |= src_width << JZ_DMA_CMD_SRC_WIDTH_OFFSET;
	cmd |= dst_width << JZ_DMA_CMD_DST_WIDTH_OFFSET;
	cmd |= transfer_size << JZ_DMA_CMD_TRANSFER_SIZE_OFFSET;
	cmd |= JZ4740_DMA_MODE_SINGLE << JZ_DMA_CMD_MODE_OFFSET;
	cmd |= JZ_DMA_CMD_TRANSFER_IRQ_ENABLE;

	jz4740_dma_write(dmadev, JZ_REG_DMA_CMD(chan->id), cmd);
	jz4740_dma_write(dmadev, JZ_REG_DMA_STATUS_CTRL(chan->id), 0);
	jz4740_dma_write(dmadev, JZ_REG_DMA_REQ_TYPE(chan->id),
		config->slave_id);

	return 0;
}

static int jz4740_dma_terminate_all(struct dma_chan *c)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct jz4740_dma_dev *dmadev = jz4740_dma_chan_get_dev(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	jz4740_dma_write_mask(dmadev, JZ_REG_DMA_STATUS_CTRL(chan->id), 0,
			JZ_DMA_STATUS_CTRL_ENABLE);
	chan->desc = NULL;
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static int jz4740_dma_start_transfer(struct jz4740_dmaengine_chan *chan)
{
	struct jz4740_dma_dev *dmadev = jz4740_dma_chan_get_dev(chan);
	dma_addr_t src_addr, dst_addr;
	struct virt_dma_desc *vdesc;
	struct jz4740_dma_sg *sg;

	jz4740_dma_write_mask(dmadev, JZ_REG_DMA_STATUS_CTRL(chan->id), 0,
			JZ_DMA_STATUS_CTRL_ENABLE);

	if (!chan->desc) {
		vdesc = vchan_next_desc(&chan->vchan);
		if (!vdesc)
			return 0;
		chan->desc = to_jz4740_dma_desc(vdesc);
		chan->next_sg = 0;
	}

	if (chan->next_sg == chan->desc->num_sgs)
		chan->next_sg = 0;

	sg = &chan->desc->sg[chan->next_sg];

	if (chan->desc->direction == DMA_MEM_TO_DEV) {
		src_addr = sg->addr;
		dst_addr = chan->fifo_addr;
	} else {
		src_addr = chan->fifo_addr;
		dst_addr = sg->addr;
	}
	jz4740_dma_write(dmadev, JZ_REG_DMA_SRC_ADDR(chan->id), src_addr);
	jz4740_dma_write(dmadev, JZ_REG_DMA_DST_ADDR(chan->id), dst_addr);
	jz4740_dma_write(dmadev, JZ_REG_DMA_TRANSFER_COUNT(chan->id),
			sg->len >> chan->transfer_shift);

	chan->next_sg++;

	jz4740_dma_write_mask(dmadev, JZ_REG_DMA_STATUS_CTRL(chan->id),
			JZ_DMA_STATUS_CTRL_NO_DESC | JZ_DMA_STATUS_CTRL_ENABLE,
			JZ_DMA_STATUS_CTRL_HALT | JZ_DMA_STATUS_CTRL_NO_DESC |
			JZ_DMA_STATUS_CTRL_ENABLE);

	jz4740_dma_write_mask(dmadev, JZ_REG_DMA_CTRL,
			JZ_DMA_CTRL_ENABLE,
			JZ_DMA_CTRL_HALT | JZ_DMA_CTRL_ENABLE);

	return 0;
}

static void jz4740_dma_chan_irq(struct jz4740_dmaengine_chan *chan)
{
	spin_lock(&chan->vchan.lock);
	if (chan->desc) {
		if (chan->desc->cyclic) {
			vchan_cyclic_callback(&chan->desc->vdesc);
		} else {
			if (chan->next_sg == chan->desc->num_sgs) {
				list_del(&chan->desc->vdesc.node);
				vchan_cookie_complete(&chan->desc->vdesc);
				chan->desc = NULL;
			}
		}
	}
	jz4740_dma_start_transfer(chan);
	spin_unlock(&chan->vchan.lock);
}

static irqreturn_t jz4740_dma_irq(int irq, void *devid)
{
	struct jz4740_dma_dev *dmadev = devid;
	uint32_t irq_status;
	unsigned int i;

	irq_status = readl(dmadev->base + JZ_REG_DMA_IRQ);

	for (i = 0; i < 6; ++i) {
		if (irq_status & (1 << i)) {
			jz4740_dma_write_mask(dmadev,
				JZ_REG_DMA_STATUS_CTRL(i), 0,
				JZ_DMA_STATUS_CTRL_ENABLE |
				JZ_DMA_STATUS_CTRL_TRANSFER_DONE);

			jz4740_dma_chan_irq(&dmadev->chan[i]);
		}
	}

	return IRQ_HANDLED;
}

static void jz4740_dma_issue_pending(struct dma_chan *c)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (vchan_issue_pending(&chan->vchan) && !chan->desc)
		jz4740_dma_start_transfer(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static struct dma_async_tx_descriptor *jz4740_dma_prep_slave_sg(
	struct dma_chan *c, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct jz4740_dma_desc *desc;
	struct scatterlist *sg;
	unsigned int i;

	desc = jz4740_dma_alloc_desc(sg_len);
	if (!desc)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		desc->sg[i].addr = sg_dma_address(sg);
		desc->sg[i].len = sg_dma_len(sg);
	}

	desc->num_sgs = sg_len;
	desc->direction = direction;
	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *jz4740_dma_prep_dma_cyclic(
	struct dma_chan *c, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct jz4740_dma_desc *desc;
	unsigned int num_periods, i;

	if (buf_len % period_len)
		return NULL;

	num_periods = buf_len / period_len;

	desc = jz4740_dma_alloc_desc(num_periods);
	if (!desc)
		return NULL;

	for (i = 0; i < num_periods; i++) {
		desc->sg[i].addr = buf_addr;
		desc->sg[i].len = period_len;
		buf_addr += period_len;
	}

	desc->num_sgs = num_periods;
	desc->direction = direction;
	desc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static size_t jz4740_dma_desc_residue(struct jz4740_dmaengine_chan *chan,
	struct jz4740_dma_desc *desc, unsigned int next_sg)
{
	struct jz4740_dma_dev *dmadev = jz4740_dma_chan_get_dev(chan);
	unsigned int residue, count;
	unsigned int i;

	residue = 0;

	for (i = next_sg; i < desc->num_sgs; i++)
		residue += desc->sg[i].len;

	if (next_sg != 0) {
		count = jz4740_dma_read(dmadev,
			JZ_REG_DMA_TRANSFER_COUNT(chan->id));
		residue += count << chan->transfer_shift;
	}

	return residue;
}

static enum dma_status jz4740_dma_tx_status(struct dma_chan *c,
	dma_cookie_t cookie, struct dma_tx_state *state)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;

	status = dma_cookie_status(c, cookie, state);
	if (status == DMA_COMPLETE || !state)
		return status;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	vdesc = vchan_find_desc(&chan->vchan, cookie);
	if (cookie == chan->desc->vdesc.tx.cookie) {
		state->residue = jz4740_dma_desc_residue(chan, chan->desc,
				chan->next_sg);
	} else if (vdesc) {
		state->residue = jz4740_dma_desc_residue(chan,
				to_jz4740_dma_desc(vdesc), 0);
	} else {
		state->residue = 0;
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	return status;
}

static void jz4740_dma_free_chan_resources(struct dma_chan *c)
{
	vchan_free_chan_resources(to_virt_chan(c));
}

static void jz4740_dma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct jz4740_dma_desc, vdesc));
}

static int jz4740_dma_probe(struct platform_device *pdev)
{
	struct jz4740_dmaengine_chan *chan;
	struct jz4740_dma_dev *dmadev;
	struct dma_device *dd;
	unsigned int i;
	struct resource *res;
	int ret;
	int irq;

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if (!dmadev)
		return -EINVAL;

	dd = &dmadev->ddev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmadev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->base))
		return PTR_ERR(dmadev->base);

	dmadev->clk = clk_get(&pdev->dev, "dma");
	if (IS_ERR(dmadev->clk))
		return PTR_ERR(dmadev->clk);

	clk_prepare_enable(dmadev->clk);

	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dd->device_free_chan_resources = jz4740_dma_free_chan_resources;
	dd->device_tx_status = jz4740_dma_tx_status;
	dd->device_issue_pending = jz4740_dma_issue_pending;
	dd->device_prep_slave_sg = jz4740_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = jz4740_dma_prep_dma_cyclic;
	dd->device_config = jz4740_dma_slave_config;
	dd->device_terminate_all = jz4740_dma_terminate_all;
	dd->dev = &pdev->dev;
	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < JZ_DMA_NR_CHANS; i++) {
		chan = &dmadev->chan[i];
		chan->id = i;
		chan->vchan.desc_free = jz4740_dma_desc_free;
		vchan_init(&chan->vchan, dd);
	}

	ret = dma_async_device_register(dd);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	ret = request_irq(irq, jz4740_dma_irq, 0, dev_name(&pdev->dev), dmadev);
	if (ret)
		goto err_unregister;

	platform_set_drvdata(pdev, dmadev);

	return 0;

err_unregister:
	dma_async_device_unregister(dd);
	return ret;
}

static int jz4740_dma_remove(struct platform_device *pdev)
{
	struct jz4740_dma_dev *dmadev = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, dmadev);
	dma_async_device_unregister(&dmadev->ddev);
	clk_disable_unprepare(dmadev->clk);

	return 0;
}

static struct platform_driver jz4740_dma_driver = {
	.probe = jz4740_dma_probe,
	.remove = jz4740_dma_remove,
	.driver = {
		.name = "jz4740-dma",
	},
};
module_platform_driver(jz4740_dma_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("JZ4740 DMA driver");
MODULE_LICENSE("GPL v2");
