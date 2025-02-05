// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2024 Timesys Corporation <piotr.wojtaszczyk@timesys.com>
//
// Based on TI DMA Crossbar driver by:
//   Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com
//   Author: Peter Ujfalusi <peter.ujfalusi@ti.com>

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#define LPC32XX_SSP_CLK_CTRL 0x78
#define LPC32XX_I2S_CLK_CTRL 0x7c

struct lpc32xx_dmamux {
	int signal;
	char *name_sel0;
	char *name_sel1;
	int muxval;
	int muxreg;
	int bit;
	bool busy;
};

struct lpc32xx_dmamux_data {
	struct dma_router dmarouter;
	struct regmap *reg;
	spinlock_t lock; /* protects busy status flag */
};

/* From LPC32x0 User manual "3.2.1 DMA request signals" */
static struct lpc32xx_dmamux lpc32xx_muxes[] = {
	{
		.signal = 3,
		.name_sel0 = "spi2-rx-tx",
		.name_sel1 = "ssp1-rx",
		.muxreg = LPC32XX_SSP_CLK_CTRL,
		.bit = 5,
	},
	{
		.signal = 10,
		.name_sel0 = "uart7-rx",
		.name_sel1 = "i2s1-dma1",
		.muxreg = LPC32XX_I2S_CLK_CTRL,
		.bit = 4,
	},
	{
		.signal = 11,
		.name_sel0 = "spi1-rx-tx",
		.name_sel1 = "ssp1-tx",
		.muxreg = LPC32XX_SSP_CLK_CTRL,
		.bit = 4,
	},
	{
		.signal = 14,
		.name_sel0 = "none",
		.name_sel1 = "ssp0-rx",
		.muxreg = LPC32XX_SSP_CLK_CTRL,
		.bit = 3,
	},
	{
		.signal = 15,
		.name_sel0 = "none",
		.name_sel1 = "ssp0-tx",
		.muxreg = LPC32XX_SSP_CLK_CTRL,
		.bit = 2,
	},
};

static void lpc32xx_dmamux_release(struct device *dev, void *route_data)
{
	struct lpc32xx_dmamux_data *dmamux = dev_get_drvdata(dev);
	struct lpc32xx_dmamux *mux = route_data;

	dev_dbg(dev, "releasing dma request signal %d routed to %s\n",
		mux->signal, mux->muxval ? mux->name_sel1 : mux->name_sel1);

	guard(spinlock)(&dmamux->lock);

	mux->busy = false;
}

static void *lpc32xx_dmamux_reserve(struct of_phandle_args *dma_spec,
				    struct of_dma *ofdma)
{
	struct platform_device *pdev = of_find_device_by_node(ofdma->of_node);
	struct device *dev = &pdev->dev;
	struct lpc32xx_dmamux_data *dmamux = platform_get_drvdata(pdev);
	unsigned long flags;
	struct lpc32xx_dmamux *mux = NULL;
	int i;

	if (dma_spec->args_count != 3) {
		dev_err(&pdev->dev, "invalid number of dma mux args\n");
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < ARRAY_SIZE(lpc32xx_muxes); i++) {
		if (lpc32xx_muxes[i].signal == dma_spec->args[0]) {
			mux = &lpc32xx_muxes[i];
			break;
		}
	}
	if (!mux) {
		dev_err(&pdev->dev, "invalid mux request number: %d\n",
			dma_spec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (dma_spec->args[2] > 1) {
		dev_err(&pdev->dev, "invalid dma mux value: %d\n",
			dma_spec->args[1]);
		return ERR_PTR(-EINVAL);
	}

	/* The of_node_put() will be done in the core for the node */
	dma_spec->np = of_parse_phandle(ofdma->of_node, "dma-masters", 0);
	if (!dma_spec->np) {
		dev_err(&pdev->dev, "can't get dma master\n");
		return ERR_PTR(-EINVAL);
	}

	spin_lock_irqsave(&dmamux->lock, flags);
	if (mux->busy) {
		spin_unlock_irqrestore(&dmamux->lock, flags);
		dev_err(dev, "dma request signal %d busy, routed to %s\n",
			mux->signal, mux->muxval ? mux->name_sel1 : mux->name_sel1);
		of_node_put(dma_spec->np);
		return ERR_PTR(-EBUSY);
	}

	mux->busy = true;
	mux->muxval = dma_spec->args[2] ? BIT(mux->bit) : 0;

	regmap_update_bits(dmamux->reg, mux->muxreg, BIT(mux->bit), mux->muxval);
	spin_unlock_irqrestore(&dmamux->lock, flags);

	dma_spec->args[2] = 0;
	dma_spec->args_count = 2;

	dev_dbg(dev, "dma request signal %d routed to %s\n",
		mux->signal, mux->muxval ? mux->name_sel1 : mux->name_sel1);

	return mux;
}

static int lpc32xx_dmamux_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct lpc32xx_dmamux_data *dmamux;

	dmamux = devm_kzalloc(&pdev->dev, sizeof(*dmamux), GFP_KERNEL);
	if (!dmamux)
		return -ENOMEM;

	dmamux->reg = syscon_node_to_regmap(np->parent);
	if (IS_ERR(dmamux->reg)) {
		dev_err(&pdev->dev, "syscon lookup failed\n");
		return PTR_ERR(dmamux->reg);
	}

	spin_lock_init(&dmamux->lock);
	platform_set_drvdata(pdev, dmamux);
	dmamux->dmarouter.dev = &pdev->dev;
	dmamux->dmarouter.route_free = lpc32xx_dmamux_release;

	return of_dma_router_register(np, lpc32xx_dmamux_reserve,
				      &dmamux->dmarouter);
}

static const struct of_device_id lpc32xx_dmamux_match[] = {
	{ .compatible = "nxp,lpc3220-dmamux" },
	{},
};

static struct platform_driver lpc32xx_dmamux_driver = {
	.probe	= lpc32xx_dmamux_probe,
	.driver = {
		.name = "lpc32xx-dmamux",
		.of_match_table = lpc32xx_dmamux_match,
	},
};

static int __init lpc32xx_dmamux_init(void)
{
	return platform_driver_register(&lpc32xx_dmamux_driver);
}
arch_initcall(lpc32xx_dmamux_init);
