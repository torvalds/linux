/*
 * SHDMA Device Tree glue
 *
 * Copyright (C) 2013 Renesas Electronics Inc.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/shdma-base.h>

#define to_shdma_chan(c) container_of(c, struct shdma_chan, dma_chan)

static struct dma_chan *shdma_of_xlate(struct of_phandle_args *dma_spec,
				       struct of_dma *ofdma)
{
	u32 id = dma_spec->args[0];
	dma_cap_mask_t mask;
	struct dma_chan *chan;

	if (dma_spec->args_count != 1)
		return NULL;

	dma_cap_zero(mask);
	/* Only slave DMA channels can be allocated via DT */
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_channel(mask, shdma_chan_filter,
				   (void *)(uintptr_t)id);
	if (chan)
		to_shdma_chan(chan)->hw_req = id;

	return chan;
}

static int shdma_of_probe(struct platform_device *pdev)
{
	const struct of_dev_auxdata *lookup = dev_get_platdata(&pdev->dev);
	int ret;

	ret = of_dma_controller_register(pdev->dev.of_node,
					 shdma_of_xlate, pdev);
	if (ret < 0)
		return ret;

	ret = of_platform_populate(pdev->dev.of_node, NULL, lookup, &pdev->dev);
	if (ret < 0)
		of_dma_controller_free(pdev->dev.of_node);

	return ret;
}

static const struct of_device_id shdma_of_match[] = {
	{ .compatible = "renesas,shdma-mux", },
	{ }
};
MODULE_DEVICE_TABLE(of, sh_dmae_of_match);

static struct platform_driver shdma_of = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "shdma-of",
		.of_match_table = shdma_of_match,
	},
	.probe		= shdma_of_probe,
};

module_platform_driver(shdma_of);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SH-DMA driver DT glue");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
