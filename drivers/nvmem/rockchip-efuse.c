/*
 * Rockchip eFuse Driver
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Caesar Wang <wxt@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define EFUSE_A_SHIFT			6
#define EFUSE_A_MASK			0x3ff
#define EFUSE_PGENB			BIT(3)
#define EFUSE_LOAD			BIT(2)
#define EFUSE_STROBE			BIT(1)
#define EFUSE_CSB			BIT(0)

#define REG_EFUSE_CTRL			0x0000
#define REG_EFUSE_DOUT			0x0004

struct rockchip_efuse_chip {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
};

static int rockchip_efuse_read(void *context, unsigned int offset,
			       void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	u8 *buf = val;
	int ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	writel(EFUSE_LOAD | EFUSE_PGENB, efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (bytes--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
			     (~(EFUSE_A_MASK << EFUSE_A_SHIFT)),
			     efuse->base + REG_EFUSE_CTRL);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     ((offset++ & EFUSE_A_MASK) << EFUSE_A_SHIFT),
			     efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     EFUSE_STROBE, efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		     (~EFUSE_STROBE), efuse->base + REG_EFUSE_CTRL);
		udelay(1);
	}

	/* Switch to standby mode */
	writel(EFUSE_PGENB | EFUSE_CSB, efuse->base + REG_EFUSE_CTRL);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static struct nvmem_config econfig = {
	.name = "rockchip-efuse",
	.owner = THIS_MODULE,
	.stride = 1,
	.word_size = 1,
	.read_only = true,
};

static const struct of_device_id rockchip_efuse_match[] = {
	{ .compatible = "rockchip,rockchip-efuse", },
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, rockchip_efuse_match);

static int rockchip_efuse_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct nvmem_device *nvmem;
	struct rockchip_efuse_chip *efuse;

	efuse = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_efuse_chip),
			     GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(efuse->base))
		return PTR_ERR(efuse->base);

	efuse->clk = devm_clk_get(&pdev->dev, "pclk_efuse");
	if (IS_ERR(efuse->clk))
		return PTR_ERR(efuse->clk);

	efuse->dev = &pdev->dev;
	econfig.size = resource_size(res);
	econfig.reg_read = rockchip_efuse_read;
	econfig.priv = efuse;
	econfig.dev = efuse->dev;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int rockchip_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver rockchip_efuse_driver = {
	.probe = rockchip_efuse_probe,
	.remove = rockchip_efuse_remove,
	.driver = {
		.name = "rockchip-efuse",
		.of_match_table = rockchip_efuse_match,
	},
};

module_platform_driver(rockchip_efuse_driver);
MODULE_DESCRIPTION("rockchip_efuse driver");
MODULE_LICENSE("GPL v2");
