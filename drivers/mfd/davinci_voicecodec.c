// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DaVinci Voice Codec Core Interface for TI platforms
 *
 * Copyright (C) 2010 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regmap.h>

#include <sound/pcm.h>

#include <linux/mfd/davinci_voicecodec.h>
#include <mach/hardware.h>

static const struct regmap_config davinci_vc_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
};

static int __init davinci_vc_probe(struct platform_device *pdev)
{
	struct davinci_vc *davinci_vc;
	struct resource *res;
	struct mfd_cell *cell = NULL;
	int ret;

	davinci_vc = devm_kzalloc(&pdev->dev,
				  sizeof(struct davinci_vc), GFP_KERNEL);
	if (!davinci_vc)
		return -ENOMEM;

	davinci_vc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(davinci_vc->clk)) {
		dev_dbg(&pdev->dev,
			    "could not get the clock for voice codec\n");
		return -ENODEV;
	}
	clk_enable(davinci_vc->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	davinci_vc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(davinci_vc->base)) {
		ret = PTR_ERR(davinci_vc->base);
		goto fail;
	}

	davinci_vc->regmap = devm_regmap_init_mmio(&pdev->dev,
						   davinci_vc->base,
						   &davinci_vc_regmap);
	if (IS_ERR(davinci_vc->regmap)) {
		ret = PTR_ERR(davinci_vc->regmap);
		goto fail;
	}

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto fail;
	}

	davinci_vc->davinci_vcif.dma_tx_channel = res->start;
	davinci_vc->davinci_vcif.dma_tx_addr =
		(dma_addr_t)(io_v2p(davinci_vc->base) + DAVINCI_VC_WFIFO);

	res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto fail;
	}

	davinci_vc->davinci_vcif.dma_rx_channel = res->start;
	davinci_vc->davinci_vcif.dma_rx_addr =
		(dma_addr_t)(io_v2p(davinci_vc->base) + DAVINCI_VC_RFIFO);

	davinci_vc->dev = &pdev->dev;
	davinci_vc->pdev = pdev;

	/* Voice codec interface client */
	cell = &davinci_vc->cells[DAVINCI_VC_VCIF_CELL];
	cell->name = "davinci-vcif";
	cell->platform_data = davinci_vc;
	cell->pdata_size = sizeof(*davinci_vc);

	/* Voice codec CQ93VC client */
	cell = &davinci_vc->cells[DAVINCI_VC_CQ93VC_CELL];
	cell->name = "cq93vc-codec";
	cell->platform_data = davinci_vc;
	cell->pdata_size = sizeof(*davinci_vc);

	ret = mfd_add_devices(&pdev->dev, pdev->id, davinci_vc->cells,
			      DAVINCI_VC_CELLS, NULL, 0, NULL);
	if (ret != 0) {
		dev_err(&pdev->dev, "fail to register client devices\n");
		goto fail;
	}

	return 0;

fail:
	clk_disable(davinci_vc->clk);

	return ret;
}

static int davinci_vc_remove(struct platform_device *pdev)
{
	struct davinci_vc *davinci_vc = platform_get_drvdata(pdev);

	mfd_remove_devices(&pdev->dev);

	clk_disable(davinci_vc->clk);

	return 0;
}

static struct platform_driver davinci_vc_driver = {
	.driver	= {
		.name = "davinci_voicecodec",
	},
	.remove	= davinci_vc_remove,
};

module_platform_driver_probe(davinci_vc_driver, davinci_vc_probe);

MODULE_AUTHOR("Miguel Aguilar");
MODULE_DESCRIPTION("Texas Instruments DaVinci Voice Codec Core Interface");
MODULE_LICENSE("GPL");
