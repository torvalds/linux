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

#include <linux/platform_device.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/clk.h>

#define EFUSE_A_SHIFT			6
#define EFUSE_A_MASK			0x3ff
#define EFUSE_PGENB			BIT(3)
#define EFUSE_LOAD			BIT(2)
#define EFUSE_STROBE			BIT(1)
#define EFUSE_CSB			BIT(0)

#define REG_EFUSE_CTRL			0x0000
#define REG_EFUSE_DOUT			0x0004

struct rockchip_efuse_context {
	struct device *dev;
	void __iomem *base;
	struct clk *efuse_clk;
};

static int rockchip_efuse_write(void *context, const void *data, size_t count)
{
	/* Nothing TBD, Read-Only */
	return 0;
}

static int rockchip_efuse_read(void *context,
			       const void *reg, size_t reg_size,
			       void *val, size_t val_size)
{
	unsigned int offset = *(u32 *)reg;
	struct rockchip_efuse_context *_context = context;
	void __iomem *base = _context->base;
	struct clk *clk = _context->efuse_clk;
	u8 *buf = val;
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret < 0) {
		dev_err(_context->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	writel(EFUSE_LOAD | EFUSE_PGENB, base + REG_EFUSE_CTRL);
	udelay(1);
	while (val_size) {
		writel(readl(base + REG_EFUSE_CTRL) &
			     (~(EFUSE_A_MASK << EFUSE_A_SHIFT)),
			     base + REG_EFUSE_CTRL);
		writel(readl(base + REG_EFUSE_CTRL) |
			     ((offset & EFUSE_A_MASK) << EFUSE_A_SHIFT),
			     base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(base + REG_EFUSE_CTRL) |
			     EFUSE_STROBE, base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(base + REG_EFUSE_DOUT);
		writel(readl(base + REG_EFUSE_CTRL) &
		     (~EFUSE_STROBE), base + REG_EFUSE_CTRL);
		udelay(1);

		val_size -= 1;
		offset += 1;
	}

	/* Switch to standby mode */
	writel(EFUSE_PGENB | EFUSE_CSB, base + REG_EFUSE_CTRL);

	clk_disable_unprepare(clk);

	return 0;
}

static struct regmap_bus rockchip_efuse_bus = {
	.read = rockchip_efuse_read,
	.write = rockchip_efuse_write,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

struct regmap_config rockchip_efuse_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 1,
	.val_bits = 8,
};

static struct nvmem_config econfig = {
	.name = "rockchip-efuse",
	.owner = THIS_MODULE,
	.read_only = true,
};

static const struct of_device_id rockchip_efuse_match[] = {
	{ .compatible = "rockchip,rockchip-efuse",},
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, rockchip_efuse_match);

int rockchip_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct regmap *regmap;
	void __iomem *base;
	struct clk *clk;
	struct rockchip_efuse_context *context;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	context = devm_kzalloc(dev, sizeof(struct rockchip_efuse_context),
			       GFP_KERNEL);
	if (IS_ERR(context))
		return PTR_ERR(context);

	clk = devm_clk_get(dev, "pclk_efuse");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	context->dev = dev;
	context->base = base;
	context->efuse_clk = clk;

	rockchip_efuse_regmap_config.max_register = resource_size(res) - 1;

	regmap = devm_regmap_init(dev, &rockchip_efuse_bus,
				  context, &rockchip_efuse_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(regmap);
	}
	econfig.dev = dev;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

int rockchip_efuse_remove(struct platform_device *pdev)
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
