/*
 *  Copyright (C) 2013, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 DMAC support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
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

#include <asm/mach-jz4740/dma.h>

#include "virt-dma.h"

#define JZ_DMA_NR_CHANS 6

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
	struct jz4740_dma_chan *jz_chan;

	dma_addr_t fifo_addr;

	struct jz4740_dma_desc *desc;
	unsigned int next_sg;
};

struct jz4740_dma_dev {
	struct dma_device ddev;

	struct jz4740_dmaengine_chan chan[JZ_DMA_NR_CHANS];
};

static struct jz4740_dmaengine_chan *to_jz4740_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct jz4740_dmaengine_chan, vchan.chan);
}

static struct jz4740_dma_desc *to_jz4740_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct jz4740_dma_desc, vdesc);
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
	const struct dma_slave_config *config)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	struct jz4740_dma_config jzcfg;

	switch (config->direction) {
	case DMA_MEM_TO_DEV:
		jzcfg.flags = JZ4740_DMA_SRC_AUTOINC;
		jzcfg.transfer_size = jz4740_dma_maxburst(config->dst_maxburst);
		chan->fifo_addr = config->dst_addr;
		break;
	case DMA_DEV_TO_MEM:
		jzcfg.flags = JZ4740_DMA_DST_AUTOINC;
		jzcfg.transfer_size = jz4740_dma_maxburst(config->src_maxburst);
		chan->fifo_addr = config->src_addr;
		break;
	default:
		return -EINVAL;
	}


	jzcfg.src_width = jz4740_dma_width(config->src_addr_width);
	jzcfg.dst_width = jz4740_dma_width(config->dst_addr_width);
	jzcfg.mode = JZ4740_DMA_MODE_SINGLE;
	jzcfg.request_type = config->slave_id;

	jz4740_dma_configure(chan->jz_chan, &jzcfg);

	return 0;
}

static int jz4740_dma_terminate_all(struct dma_chan *c)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	jz4740_dma_disable(chan->jz_chan);
	chan->desc = NULL;
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static int jz4740_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct dma_slave_config *config = (struct dma_slave_config *)arg;

	switch (cmd) {
	case DMA_SLAVE_CONFIG:
		return jz4740_dma_slave_config(chan, config);
	case DMA_TERMINATE_ALL:
		return jz4740_dma_terminate_all(chan);
	default:
		return -ENOSYS;
	}
}

static int jz4740_dma_start_transfer(struct jz4740_dmaengine_chan *chan)
{
	dma_addr_t src_addr, dst_addr;
	struct virt_dma_desc *vdesc;
	struct jz4740_dma_sg *sg;

	jz4740_dma_disable(chan->jz_chan);

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
	jz4740_dma_set_src_addr(chan->jz_chan, src_addr);
	jz4740_dma_set_dst_addr(chan->jz_chan, dst_addr);
	jz4740_dma_set_transfer_count(chan->jz_chan, sg->len);

	chan->next_sg++;

	jz4740_dma_enable(chan->jz_chan);

	return 0;
}

static void jz4740_dma_complete_cb(struct jz4740_dma_chan *jz_chan, int error,
	void *devid)
{
	struct jz4740_dmaengine_chan *chan = devid;

	spin_lock(&chan->vchan.lock);
	if (chan->desc) {
		if (chan->desc && chan->desc->cyclic) {
			vchan_cyclic_callback(&chan->desc->vdesc);
		} else {
			if (chan->next_sg == chan->desc->num_sgs) {
				chan->desc = NULL;
				vchan_cookie_complete(&chan->desc->vdesc);
			}
		}
	}
	jz4740_dma_start_transfer(chan);
	spin_unlock(&chan->vchan.lock);
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
	unsigned long flags, void *context)
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
	size_t residue = 0;
	unsigned int i;

	residue = 0;

	for (i = next_sg; i < desc->num_sgs; i++)
		residue += desc->sg[i].len;

	if (next_sg != 0)
		residue += jz4740_dma_get_residue(chan->jz_chan);

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
	if (status == DMA_SUCCESS || !state)
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

static int jz4740_dma_alloc_chan_resources(struct dma_chan *c)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);

	chan->jz_chan = jz4740_dma_request(chan, NULL);
	if (!chan->jz_chan)
		return -EBUSY;

	jz4740_dma_set_complete_cb(chan->jz_chan, jz4740_dma_complete_cb);

	return 0;
}

static void jz4740_dma_free_chan_resources(struct dma_chan *c)
{
	struct jz4740_dmaengine_chan *chan = to_jz4740_dma_chan(c);

	vchan_free_chan_resources(&chan->vchan);
	jz4740_dma_free(chan->jz_chan);
	chan->jz_chan = NULL;
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
	int ret;

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if (!dmadev)
		return -EINVAL;

	dd = &dmadev->ddev;

	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dd->device_alloc_chan_resources = jz4740_dma_alloc_chan_resources;
	dd->device_free_chan_resources = jz4740_dma_free_chan_resources;
	dd->device_tx_status = jz4740_dma_tx_status;
	dd->device_issue_pending = jz4740_dma_issue_pending;
	dd->device_prep_slave_sg = jz4740_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = jz4740_dma_prep_dma_cyclic;
	dd->device_control = jz4740_dma_control;
	dd->dev = &pdev->dev;
	dd->chancnt = JZ_DMA_NR_CHANS;
	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < dd->chancnt; i++) {
		chan = &dmadev->chan[i];
		chan->vchan.desc_free = jz4740_dma_desc_free;
		vchan_init(&chan->vchan, dd);
	}

	ret = dma_async_device_register(dd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dmadev);

	return 0;
}

static int jz4740_dma_remove(struct platform_device *pdev)
{
	struct jz4740_dma_dev *dmadev = platform_get_drvdata(pdev);

	dma_async_device_unregister(&dmadev->ddev);

	return 0;
}

static struct platform_driver jz4740_dma_driver = {
	.probe = jz4740_dma_probe,
	.remove = jz4740_dma_remove,
	.driver = {
		.name = "jz4740-dma",
		.owner = THIS_MODULE,
	},
};
module_platform_driver(jz4740_dma_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("JZ4740 DMA driver");
MODULE_LICENSE("GPLv2");
