/*
 * This is for Renesas R-Car Audio-DMAC-peri-peri.
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 * Copyright (C) 2014 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * based on the drivers/dma/sh/shdma.c
 *
 * Copyright (C) 2011-2012 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 * Copyright (C) 2009 Renesas Solutions, Inc. All rights reserved.
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/of_dma.h>
#include <linux/platform_data/dma-rcar-audmapp.h>
#include <linux/platform_device.h>
#include <linux/shdma-base.h>

/*
 * DMA register
 */
#define PDMASAR		0x00
#define PDMADAR		0x04
#define PDMACHCR	0x0c

/* PDMACHCR */
#define PDMACHCR_DE		(1 << 0)

#define AUDMAPP_MAX_CHANNELS	29

/* Default MEMCPY transfer size = 2^2 = 4 bytes */
#define LOG2_DEFAULT_XFER_SIZE	2
#define AUDMAPP_SLAVE_NUMBER	256
#define AUDMAPP_LEN_MAX		(16 * 1024 * 1024)

struct audmapp_chan {
	struct shdma_chan shdma_chan;
	void __iomem *base;
	dma_addr_t slave_addr;
	u32 chcr;
};

struct audmapp_device {
	struct shdma_dev shdma_dev;
	struct audmapp_pdata *pdata;
	struct device *dev;
	void __iomem *chan_reg;
};

struct audmapp_desc {
	struct shdma_desc shdma_desc;
	dma_addr_t src;
	dma_addr_t dst;
};

#define to_shdma_chan(c) container_of(c, struct shdma_chan, dma_chan)

#define to_chan(chan) container_of(chan, struct audmapp_chan, shdma_chan)
#define to_desc(sdesc) container_of(sdesc, struct audmapp_desc, shdma_desc)
#define to_dev(chan) container_of(chan->shdma_chan.dma_chan.device,	\
				  struct audmapp_device, shdma_dev.dma_dev)

static void audmapp_write(struct audmapp_chan *auchan, u32 data, u32 reg)
{
	struct audmapp_device *audev = to_dev(auchan);
	struct device *dev = audev->dev;

	dev_dbg(dev, "w %p : %08x\n", auchan->base + reg, data);

	iowrite32(data, auchan->base + reg);
}

static u32 audmapp_read(struct audmapp_chan *auchan, u32 reg)
{
	return ioread32(auchan->base + reg);
}

static void audmapp_halt(struct shdma_chan *schan)
{
	struct audmapp_chan *auchan = to_chan(schan);
	int i;

	audmapp_write(auchan, 0, PDMACHCR);

	for (i = 0; i < 1024; i++) {
		if (0 == audmapp_read(auchan, PDMACHCR))
			return;
		udelay(1);
	}
}

static void audmapp_start_xfer(struct shdma_chan *schan,
			       struct shdma_desc *sdesc)
{
	struct audmapp_chan *auchan = to_chan(schan);
	struct audmapp_device *audev = to_dev(auchan);
	struct audmapp_desc *desc = to_desc(sdesc);
	struct device *dev = audev->dev;
	u32 chcr = auchan->chcr | PDMACHCR_DE;

	dev_dbg(dev, "src/dst/chcr = %pad/%pad/%08x\n",
		&desc->src, &desc->dst, chcr);

	audmapp_write(auchan, desc->src,	PDMASAR);
	audmapp_write(auchan, desc->dst,	PDMADAR);
	audmapp_write(auchan, chcr,	PDMACHCR);
}

static void audmapp_get_config(struct audmapp_chan *auchan, int slave_id,
			      u32 *chcr, dma_addr_t *dst)
{
	struct audmapp_device *audev = to_dev(auchan);
	struct audmapp_pdata *pdata = audev->pdata;
	struct audmapp_slave_config *cfg;
	int i;

	*chcr	= 0;
	*dst	= 0;

	if (!pdata) { /* DT */
		*chcr = ((u32)slave_id) << 16;
		auchan->shdma_chan.slave_id = (slave_id) >> 8;
		return;
	}

	/* non-DT */

	if (slave_id >= AUDMAPP_SLAVE_NUMBER)
		return;

	for (i = 0, cfg = pdata->slave; i < pdata->slave_num; i++, cfg++)
		if (cfg->slave_id == slave_id) {
			*chcr	= cfg->chcr;
			*dst	= cfg->dst;
			break;
		}
}

static int audmapp_set_slave(struct shdma_chan *schan, int slave_id,
			     dma_addr_t slave_addr, bool try)
{
	struct audmapp_chan *auchan = to_chan(schan);
	u32 chcr;
	dma_addr_t dst;

	audmapp_get_config(auchan, slave_id, &chcr, &dst);

	if (try)
		return 0;

	auchan->chcr		= chcr;
	auchan->slave_addr	= slave_addr ? : dst;

	return 0;
}

static int audmapp_desc_setup(struct shdma_chan *schan,
			      struct shdma_desc *sdesc,
			      dma_addr_t src, dma_addr_t dst, size_t *len)
{
	struct audmapp_desc *desc = to_desc(sdesc);

	if (*len > (size_t)AUDMAPP_LEN_MAX)
		*len = (size_t)AUDMAPP_LEN_MAX;

	desc->src = src;
	desc->dst = dst;

	return 0;
}

static void audmapp_setup_xfer(struct shdma_chan *schan,
			       int slave_id)
{
}

static dma_addr_t audmapp_slave_addr(struct shdma_chan *schan)
{
	struct audmapp_chan *auchan = to_chan(schan);

	return auchan->slave_addr;
}

static bool audmapp_channel_busy(struct shdma_chan *schan)
{
	struct audmapp_chan *auchan = to_chan(schan);
	u32 chcr = audmapp_read(auchan, PDMACHCR);

	return chcr & ~PDMACHCR_DE;
}

static bool audmapp_desc_completed(struct shdma_chan *schan,
				   struct shdma_desc *sdesc)
{
	return true;
}

static struct shdma_desc *audmapp_embedded_desc(void *buf, int i)
{
	return &((struct audmapp_desc *)buf)[i].shdma_desc;
}

static const struct shdma_ops audmapp_shdma_ops = {
	.halt_channel	= audmapp_halt,
	.desc_setup	= audmapp_desc_setup,
	.set_slave	= audmapp_set_slave,
	.start_xfer	= audmapp_start_xfer,
	.embedded_desc	= audmapp_embedded_desc,
	.setup_xfer	= audmapp_setup_xfer,
	.slave_addr	= audmapp_slave_addr,
	.channel_busy	= audmapp_channel_busy,
	.desc_completed	= audmapp_desc_completed,
};

static int audmapp_chan_probe(struct platform_device *pdev,
			      struct audmapp_device *audev, int id)
{
	struct shdma_dev *sdev = &audev->shdma_dev;
	struct audmapp_chan *auchan;
	struct shdma_chan *schan;
	struct device *dev = audev->dev;

	auchan = devm_kzalloc(dev, sizeof(*auchan), GFP_KERNEL);
	if (!auchan)
		return -ENOMEM;

	schan = &auchan->shdma_chan;
	schan->max_xfer_len = AUDMAPP_LEN_MAX;

	shdma_chan_probe(sdev, schan, id);

	auchan->base = audev->chan_reg + 0x20 + (0x10 * id);
	dev_dbg(dev, "%02d : %p / %p", id, auchan->base, audev->chan_reg);

	return 0;
}

static void audmapp_chan_remove(struct audmapp_device *audev)
{
	struct dma_device *dma_dev = &audev->shdma_dev.dma_dev;
	struct shdma_chan *schan;
	int i;

	shdma_for_each_chan(schan, &audev->shdma_dev, i) {
		BUG_ON(!schan);
		shdma_chan_remove(schan);
	}
	dma_dev->chancnt = 0;
}

static struct dma_chan *audmapp_of_xlate(struct of_phandle_args *dma_spec,
					 struct of_dma *ofdma)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	u32 chcr = dma_spec->args[0];

	if (dma_spec->args_count != 1)
		return NULL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_channel(mask, shdma_chan_filter, NULL);
	if (chan)
		to_shdma_chan(chan)->hw_req = chcr;

	return chan;
}

static int audmapp_probe(struct platform_device *pdev)
{
	struct audmapp_pdata *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	struct audmapp_device *audev;
	struct shdma_dev *sdev;
	struct dma_device *dma_dev;
	struct resource *res;
	int err, i;

	if (np)
		of_dma_controller_register(np, audmapp_of_xlate, pdev);
	else if (!pdata)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	audev = devm_kzalloc(&pdev->dev, sizeof(*audev), GFP_KERNEL);
	if (!audev)
		return -ENOMEM;

	audev->dev	= &pdev->dev;
	audev->pdata	= pdata;
	audev->chan_reg	= devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(audev->chan_reg))
		return PTR_ERR(audev->chan_reg);

	sdev		= &audev->shdma_dev;
	sdev->ops	= &audmapp_shdma_ops;
	sdev->desc_size	= sizeof(struct audmapp_desc);

	dma_dev			= &sdev->dma_dev;
	dma_dev->copy_align	= LOG2_DEFAULT_XFER_SIZE;
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);

	err = shdma_init(&pdev->dev, sdev, AUDMAPP_MAX_CHANNELS);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, audev);

	/* Create DMA Channel */
	for (i = 0; i < AUDMAPP_MAX_CHANNELS; i++) {
		err = audmapp_chan_probe(pdev, audev, i);
		if (err)
			goto chan_probe_err;
	}

	err = dma_async_device_register(dma_dev);
	if (err < 0)
		goto chan_probe_err;

	return err;

chan_probe_err:
	audmapp_chan_remove(audev);
	shdma_cleanup(sdev);

	return err;
}

static int audmapp_remove(struct platform_device *pdev)
{
	struct audmapp_device *audev = platform_get_drvdata(pdev);
	struct dma_device *dma_dev = &audev->shdma_dev.dma_dev;

	dma_async_device_unregister(dma_dev);

	audmapp_chan_remove(audev);
	shdma_cleanup(&audev->shdma_dev);

	return 0;
}

static const struct of_device_id audmapp_of_match[] = {
	{ .compatible = "renesas,rcar-audmapp", },
	{},
};

static struct platform_driver audmapp_driver = {
	.probe		= audmapp_probe,
	.remove		= audmapp_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rcar-audmapp-engine",
		.of_match_table = audmapp_of_match,
	},
};
module_platform_driver(audmapp_driver);

MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car Audio DMAC peri-peri driver");
MODULE_LICENSE("GPL");
