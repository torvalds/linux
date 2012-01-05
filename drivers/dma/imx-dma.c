/*
 * drivers/dma/imx-dma.c
 *
 * This file contains a driver for the Freescale i.MX DMA engine
 * found on i.MX1/21/27
 *
 * Copyright 2010 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/module.h>

#include <asm/irq.h>
#include <mach/dma-v1.h>
#include <mach/hardware.h>

struct imxdma_channel {
	struct imxdma_engine		*imxdma;
	unsigned int			channel;
	unsigned int			imxdma_channel;

	enum dma_slave_buswidth		word_size;
	dma_addr_t			per_address;
	u32				watermark_level;
	struct dma_chan			chan;
	spinlock_t			lock;
	struct dma_async_tx_descriptor	desc;
	dma_cookie_t			last_completed;
	enum dma_status			status;
	int				dma_request;
	struct scatterlist		*sg_list;
};

#define MAX_DMA_CHANNELS 8

struct imxdma_engine {
	struct device			*dev;
	struct device_dma_parameters	dma_parms;
	struct dma_device		dma_device;
	struct imxdma_channel		channel[MAX_DMA_CHANNELS];
};

static struct imxdma_channel *to_imxdma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct imxdma_channel, chan);
}

static void imxdma_handle(struct imxdma_channel *imxdmac)
{
	if (imxdmac->desc.callback)
		imxdmac->desc.callback(imxdmac->desc.callback_param);
	imxdmac->last_completed = imxdmac->desc.cookie;
}

static void imxdma_irq_handler(int channel, void *data)
{
	struct imxdma_channel *imxdmac = data;

	imxdmac->status = DMA_SUCCESS;
	imxdma_handle(imxdmac);
}

static void imxdma_err_handler(int channel, void *data, int error)
{
	struct imxdma_channel *imxdmac = data;

	imxdmac->status = DMA_ERROR;
	imxdma_handle(imxdmac);
}

static void imxdma_progression(int channel, void *data,
		struct scatterlist *sg)
{
	struct imxdma_channel *imxdmac = data;

	imxdmac->status = DMA_SUCCESS;
	imxdma_handle(imxdmac);
}

static int imxdma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		unsigned long arg)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct dma_slave_config *dmaengine_cfg = (void *)arg;
	int ret;
	unsigned int mode = 0;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		imxdmac->status = DMA_ERROR;
		imx_dma_disable(imxdmac->imxdma_channel);
		return 0;
	case DMA_SLAVE_CONFIG:
		if (dmaengine_cfg->direction == DMA_FROM_DEVICE) {
			imxdmac->per_address = dmaengine_cfg->src_addr;
			imxdmac->watermark_level = dmaengine_cfg->src_maxburst;
			imxdmac->word_size = dmaengine_cfg->src_addr_width;
		} else {
			imxdmac->per_address = dmaengine_cfg->dst_addr;
			imxdmac->watermark_level = dmaengine_cfg->dst_maxburst;
			imxdmac->word_size = dmaengine_cfg->dst_addr_width;
		}

		switch (imxdmac->word_size) {
		case DMA_SLAVE_BUSWIDTH_1_BYTE:
			mode = IMX_DMA_MEMSIZE_8;
			break;
		case DMA_SLAVE_BUSWIDTH_2_BYTES:
			mode = IMX_DMA_MEMSIZE_16;
			break;
		default:
		case DMA_SLAVE_BUSWIDTH_4_BYTES:
			mode = IMX_DMA_MEMSIZE_32;
			break;
		}
		ret = imx_dma_config_channel(imxdmac->imxdma_channel,
				mode | IMX_DMA_TYPE_FIFO,
				IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
				imxdmac->dma_request, 1);

		if (ret)
			return ret;

		imx_dma_config_burstlen(imxdmac->imxdma_channel,
				imxdmac->watermark_level * imxdmac->word_size);

		return 0;
	default:
		return -ENOSYS;
	}

	return -EINVAL;
}

static enum dma_status imxdma_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	dma_cookie_t last_used;
	enum dma_status ret;

	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, imxdmac->last_completed, last_used);
	dma_set_tx_state(txstate, imxdmac->last_completed, last_used, 0);

	return ret;
}

static dma_cookie_t imxdma_assign_cookie(struct imxdma_channel *imxdma)
{
	dma_cookie_t cookie = imxdma->chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	imxdma->chan.cookie = cookie;
	imxdma->desc.cookie = cookie;

	return cookie;
}

static dma_cookie_t imxdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(tx->chan);
	dma_cookie_t cookie;

	spin_lock_irq(&imxdmac->lock);

	cookie = imxdma_assign_cookie(imxdmac);

	imx_dma_enable(imxdmac->imxdma_channel);

	spin_unlock_irq(&imxdmac->lock);

	return cookie;
}

static int imxdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imx_dma_data *data = chan->private;

	imxdmac->dma_request = data->dma_request;

	dma_async_tx_descriptor_init(&imxdmac->desc, chan);
	imxdmac->desc.tx_submit = imxdma_tx_submit;
	/* txd.flags will be overwritten in prep funcs */
	imxdmac->desc.flags = DMA_CTRL_ACK;

	imxdmac->status = DMA_SUCCESS;

	return 0;
}

static void imxdma_free_chan_resources(struct dma_chan *chan)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);

	imx_dma_disable(imxdmac->imxdma_channel);

	if (imxdmac->sg_list) {
		kfree(imxdmac->sg_list);
		imxdmac->sg_list = NULL;
	}
}

static struct dma_async_tx_descriptor *imxdma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct scatterlist *sg;
	int i, ret, dma_length = 0;
	unsigned int dmamode;

	if (imxdmac->status == DMA_IN_PROGRESS)
		return NULL;

	imxdmac->status = DMA_IN_PROGRESS;

	for_each_sg(sgl, sg, sg_len, i) {
		dma_length += sg->length;
	}

	if (direction == DMA_FROM_DEVICE)
		dmamode = DMA_MODE_READ;
	else
		dmamode = DMA_MODE_WRITE;

	switch (imxdmac->word_size) {
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		if (sgl->length & 3 || sgl->dma_address & 3)
			return NULL;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		if (sgl->length & 1 || sgl->dma_address & 1)
			return NULL;
		break;
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		break;
	default:
		return NULL;
	}

	ret = imx_dma_setup_sg(imxdmac->imxdma_channel, sgl, sg_len,
		 dma_length, imxdmac->per_address, dmamode);
	if (ret)
		return NULL;

	return &imxdmac->desc;
}

static struct dma_async_tx_descriptor *imxdma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_data_direction direction)
{
	struct imxdma_channel *imxdmac = to_imxdma_chan(chan);
	struct imxdma_engine *imxdma = imxdmac->imxdma;
	int i, ret;
	unsigned int periods = buf_len / period_len;
	unsigned int dmamode;

	dev_dbg(imxdma->dev, "%s channel: %d buf_len=%d period_len=%d\n",
			__func__, imxdmac->channel, buf_len, period_len);

	if (imxdmac->status == DMA_IN_PROGRESS)
		return NULL;
	imxdmac->status = DMA_IN_PROGRESS;

	ret = imx_dma_setup_progression_handler(imxdmac->imxdma_channel,
			imxdma_progression);
	if (ret) {
		dev_err(imxdma->dev, "Failed to setup the DMA handler\n");
		return NULL;
	}

	if (imxdmac->sg_list)
		kfree(imxdmac->sg_list);

	imxdmac->sg_list = kcalloc(periods + 1,
			sizeof(struct scatterlist), GFP_KERNEL);
	if (!imxdmac->sg_list)
		return NULL;

	sg_init_table(imxdmac->sg_list, periods);

	for (i = 0; i < periods; i++) {
		imxdmac->sg_list[i].page_link = 0;
		imxdmac->sg_list[i].offset = 0;
		imxdmac->sg_list[i].dma_address = dma_addr;
		imxdmac->sg_list[i].length = period_len;
		dma_addr += period_len;
	}

	/* close the loop */
	imxdmac->sg_list[periods].offset = 0;
	imxdmac->sg_list[periods].length = 0;
	imxdmac->sg_list[periods].page_link =
		((unsigned long)imxdmac->sg_list | 0x01) & ~0x02;

	if (direction == DMA_FROM_DEVICE)
		dmamode = DMA_MODE_READ;
	else
		dmamode = DMA_MODE_WRITE;

	ret = imx_dma_setup_sg(imxdmac->imxdma_channel, imxdmac->sg_list, periods,
		 IMX_DMA_LENGTH_LOOP, imxdmac->per_address, dmamode);
	if (ret)
		return NULL;

	return &imxdmac->desc;
}

static void imxdma_issue_pending(struct dma_chan *chan)
{
	/*
	 * Nothing to do. We only have a single descriptor
	 */
}

static int __init imxdma_probe(struct platform_device *pdev)
{
	struct imxdma_engine *imxdma;
	int ret, i;

	imxdma = kzalloc(sizeof(*imxdma), GFP_KERNEL);
	if (!imxdma)
		return -ENOMEM;

	INIT_LIST_HEAD(&imxdma->dma_device.channels);

	dma_cap_set(DMA_SLAVE, imxdma->dma_device.cap_mask);
	dma_cap_set(DMA_CYCLIC, imxdma->dma_device.cap_mask);

	/* Initialize channel parameters */
	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		struct imxdma_channel *imxdmac = &imxdma->channel[i];

		imxdmac->imxdma_channel = imx_dma_request_by_prio("dmaengine",
				DMA_PRIO_MEDIUM);
		if ((int)imxdmac->channel < 0) {
			ret = -ENODEV;
			goto err_init;
		}

		imx_dma_setup_handlers(imxdmac->imxdma_channel,
		       imxdma_irq_handler, imxdma_err_handler, imxdmac);

		imxdmac->imxdma = imxdma;
		spin_lock_init(&imxdmac->lock);

		imxdmac->chan.device = &imxdma->dma_device;
		imxdmac->channel = i;

		/* Add the channel to the DMAC list */
		list_add_tail(&imxdmac->chan.device_node, &imxdma->dma_device.channels);
	}

	imxdma->dev = &pdev->dev;
	imxdma->dma_device.dev = &pdev->dev;

	imxdma->dma_device.device_alloc_chan_resources = imxdma_alloc_chan_resources;
	imxdma->dma_device.device_free_chan_resources = imxdma_free_chan_resources;
	imxdma->dma_device.device_tx_status = imxdma_tx_status;
	imxdma->dma_device.device_prep_slave_sg = imxdma_prep_slave_sg;
	imxdma->dma_device.device_prep_dma_cyclic = imxdma_prep_dma_cyclic;
	imxdma->dma_device.device_control = imxdma_control;
	imxdma->dma_device.device_issue_pending = imxdma_issue_pending;

	platform_set_drvdata(pdev, imxdma);

	imxdma->dma_device.dev->dma_parms = &imxdma->dma_parms;
	dma_set_max_seg_size(imxdma->dma_device.dev, 0xffffff);

	ret = dma_async_device_register(&imxdma->dma_device);
	if (ret) {
		dev_err(&pdev->dev, "unable to register\n");
		goto err_init;
	}

	return 0;

err_init:
	while (--i >= 0) {
		struct imxdma_channel *imxdmac = &imxdma->channel[i];
		imx_dma_free(imxdmac->imxdma_channel);
	}

	kfree(imxdma);
	return ret;
}

static int __exit imxdma_remove(struct platform_device *pdev)
{
	struct imxdma_engine *imxdma = platform_get_drvdata(pdev);
	int i;

        dma_async_device_unregister(&imxdma->dma_device);

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		struct imxdma_channel *imxdmac = &imxdma->channel[i];

		 imx_dma_free(imxdmac->imxdma_channel);
	}

        kfree(imxdma);

        return 0;
}

static struct platform_driver imxdma_driver = {
	.driver		= {
		.name	= "imx-dma",
	},
	.remove		= __exit_p(imxdma_remove),
};

static int __init imxdma_module_init(void)
{
	return platform_driver_probe(&imxdma_driver, imxdma_probe);
}
subsys_initcall(imxdma_module_init);

MODULE_AUTHOR("Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX dma driver");
MODULE_LICENSE("GPL");
