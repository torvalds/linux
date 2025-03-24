// SPDX-License-Identifier: GPL-2.0-only
/*
 * i.MX IIM driver
 *
 * Copyright (c) 2017 Pengutronix, Michael Grzeschik <m.grzeschik@pengutronix.de>
 *
 * Based on the barebox iim driver,
 * Copyright (c) 2010 Baruch Siach <baruch@tkos.co.il>,
 *	Orex Computed Radiography
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#define IIM_BANK_BASE(n)	(0x800 + 0x400 * (n))

struct imx_iim_drvdata {
	unsigned int nregs;
};

struct iim_priv {
	void __iomem *base;
	struct clk *clk;
};

static int imx_iim_read(void *context, unsigned int offset,
			  void *buf, size_t bytes)
{
	struct iim_priv *iim = context;
	int i, ret;
	u8 *buf8 = buf;

	ret = clk_prepare_enable(iim->clk);
	if (ret)
		return ret;

	for (i = offset; i < offset + bytes; i++) {
		int bank = i >> 5;
		int reg = i & 0x1f;

		*buf8++ = readl(iim->base + IIM_BANK_BASE(bank) + reg * 4);
	}

	clk_disable_unprepare(iim->clk);

	return 0;
}

static struct imx_iim_drvdata imx27_drvdata = {
	.nregs = 2 * 32,
};

static struct imx_iim_drvdata imx25_imx31_imx35_drvdata = {
	.nregs = 3 * 32,
};

static struct imx_iim_drvdata imx51_drvdata = {
	.nregs = 4 * 32,
};

static struct imx_iim_drvdata imx53_drvdata = {
	.nregs = 4 * 32 + 16,
};

static const struct of_device_id imx_iim_dt_ids[] = {
	{
		.compatible = "fsl,imx25-iim",
		.data = &imx25_imx31_imx35_drvdata,
	}, {
		.compatible = "fsl,imx27-iim",
		.data = &imx27_drvdata,
	}, {
		.compatible = "fsl,imx31-iim",
		.data = &imx25_imx31_imx35_drvdata,
	}, {
		.compatible = "fsl,imx35-iim",
		.data = &imx25_imx31_imx35_drvdata,
	}, {
		.compatible = "fsl,imx51-iim",
		.data = &imx51_drvdata,
	}, {
		.compatible = "fsl,imx53-iim",
		.data = &imx53_drvdata,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, imx_iim_dt_ids);

static int imx_iim_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iim_priv *iim;
	struct nvmem_device *nvmem;
	struct nvmem_config cfg = {};
	const struct imx_iim_drvdata *drvdata = NULL;

	iim = devm_kzalloc(dev, sizeof(*iim), GFP_KERNEL);
	if (!iim)
		return -ENOMEM;

	iim->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(iim->base))
		return PTR_ERR(iim->base);

	drvdata = of_device_get_match_data(&pdev->dev);

	iim->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(iim->clk))
		return PTR_ERR(iim->clk);

	cfg.name = "imx-iim";
	cfg.read_only = true;
	cfg.word_size = 1;
	cfg.stride = 1;
	cfg.reg_read = imx_iim_read;
	cfg.dev = dev;
	cfg.size = drvdata->nregs;
	cfg.priv = iim;

	nvmem = devm_nvmem_register(dev, &cfg);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver imx_iim_driver = {
	.probe	= imx_iim_probe,
	.driver = {
		.name	= "imx-iim",
		.of_match_table = imx_iim_dt_ids,
	},
};
module_platform_driver(imx_iim_driver);

MODULE_AUTHOR("Michael Grzeschik <m.grzeschik@pengutronix.de>");
MODULE_DESCRIPTION("i.MX IIM driver");
MODULE_LICENSE("GPL v2");
