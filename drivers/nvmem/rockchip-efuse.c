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
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define RK3288_A_SHIFT		6
#define RK3288_A_MASK		0x3ff
#define RK3288_PGENB		BIT(3)
#define RK3288_LOAD		BIT(2)
#define RK3288_STROBE		BIT(1)
#define RK3288_CSB		BIT(0)

#define RK3399_A_SHIFT		16
#define RK3399_A_MASK		0x3ff
#define RK3399_NBYTES		4
#define RK3399_STROBSFTSEL	BIT(9)
#define RK3399_RSB		BIT(7)
#define RK3399_PD		BIT(5)
#define RK3399_PGENB		BIT(3)
#define RK3399_LOAD		BIT(2)
#define RK3399_STROBE		BIT(1)
#define RK3399_CSB		BIT(0)

#define REG_EFUSE_CTRL		0x0000
#define REG_EFUSE_DOUT		0x0004

struct rockchip_efuse_chip {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
};

static int rockchip_rk3288_efuse_read(void *context, unsigned int offset,
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

	writel(RK3288_LOAD | RK3288_PGENB, efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (bytes--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
			     (~(RK3288_A_MASK << RK3288_A_SHIFT)),
			     efuse->base + REG_EFUSE_CTRL);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     ((offset++ & RK3288_A_MASK) << RK3288_A_SHIFT),
			     efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		writel(readl(efuse->base + REG_EFUSE_CTRL) |
			     RK3288_STROBE, efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		*buf++ = readb(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) &
		       (~RK3288_STROBE), efuse->base + REG_EFUSE_CTRL);
		udelay(1);
	}

	/* Switch to standby mode */
	writel(RK3288_PGENB | RK3288_CSB, efuse->base + REG_EFUSE_CTRL);

	clk_disable_unprepare(efuse->clk);

	return 0;
}

static int rockchip_rk3399_efuse_read(void *context, unsigned int offset,
				      void *val, size_t bytes)
{
	struct rockchip_efuse_chip *efuse = context;
	unsigned int addr_start, addr_end, addr_offset, addr_len;
	u32 out_value;
	u8 *buf;
	int ret, i = 0;

	ret = clk_prepare_enable(efuse->clk);
	if (ret < 0) {
		dev_err(efuse->dev, "failed to prepare/enable efuse clk\n");
		return ret;
	}

	addr_start = rounddown(offset, RK3399_NBYTES) / RK3399_NBYTES;
	addr_end = roundup(offset + bytes, RK3399_NBYTES) / RK3399_NBYTES;
	addr_offset = offset % RK3399_NBYTES;
	addr_len = addr_end - addr_start;

	buf = kzalloc(sizeof(*buf) * addr_len * RK3399_NBYTES, GFP_KERNEL);
	if (!buf) {
		clk_disable_unprepare(efuse->clk);
		return -ENOMEM;
	}

	writel(RK3399_LOAD | RK3399_PGENB | RK3399_STROBSFTSEL | RK3399_RSB,
	       efuse->base + REG_EFUSE_CTRL);
	udelay(1);
	while (addr_len--) {
		writel(readl(efuse->base + REG_EFUSE_CTRL) | RK3399_STROBE |
		       ((addr_start++ & RK3399_A_MASK) << RK3399_A_SHIFT),
		       efuse->base + REG_EFUSE_CTRL);
		udelay(1);
		out_value = readl(efuse->base + REG_EFUSE_DOUT);
		writel(readl(efuse->base + REG_EFUSE_CTRL) & (~RK3399_STROBE),
		       efuse->base + REG_EFUSE_CTRL);
		udelay(1);

		memcpy(&buf[i], &out_value, RK3399_NBYTES);
		i += RK3399_NBYTES;
	}

	/* Switch to standby mode */
	writel(RK3399_PD | RK3399_CSB, efuse->base + REG_EFUSE_CTRL);

	memcpy(val, buf + addr_offset, bytes);

	kfree(buf);

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
	/* deprecated but kept around for dts binding compatibility */
	{
		.compatible = "rockchip,rockchip-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3066a-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3188-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk322x-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3288-efuse",
		.data = (void *)&rockchip_rk3288_efuse_read,
	},
	{
		.compatible = "rockchip,rk3399-efuse",
		.data = (void *)&rockchip_rk3399_efuse_read,
	},
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, rockchip_efuse_match);

static int rockchip_efuse_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct nvmem_device *nvmem;
	struct rockchip_efuse_chip *efuse;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "failed to get match data\n");
		return -EINVAL;
	}

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
	econfig.reg_read = match->data;
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
