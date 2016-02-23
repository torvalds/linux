/*
 * Freescale MXS On-Chip OTP driver
 *
 * Copyright (C) 2015 Stefan Wahren <stefan.wahren@i2se.com>
 *
 * Based on the driver from Huang Shijie and Christoph G. Baumann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>

/* OCOTP registers and bits */

#define BM_OCOTP_CTRL_RD_BANK_OPEN	BIT(12)
#define BM_OCOTP_CTRL_ERROR		BIT(9)
#define BM_OCOTP_CTRL_BUSY		BIT(8)

#define OCOTP_TIMEOUT		10000
#define OCOTP_DATA_OFFSET	0x20

struct mxs_ocotp {
	struct clk *clk;
	void __iomem *base;
	struct nvmem_device *nvmem;
};

static int mxs_ocotp_wait(struct mxs_ocotp *otp)
{
	int timeout = OCOTP_TIMEOUT;
	unsigned int status = 0;

	while (timeout--) {
		status = readl(otp->base);

		if (!(status & (BM_OCOTP_CTRL_BUSY | BM_OCOTP_CTRL_ERROR)))
			break;

		cpu_relax();
	}

	if (status & BM_OCOTP_CTRL_BUSY)
		return -EBUSY;
	else if (status & BM_OCOTP_CTRL_ERROR)
		return -EIO;

	return 0;
}

static int mxs_ocotp_read(void *context, const void *reg, size_t reg_size,
			  void *val, size_t val_size)
{
	struct mxs_ocotp *otp = context;
	unsigned int offset = *(u32 *)reg;
	u32 *buf = val;
	int ret;

	ret = clk_enable(otp->clk);
	if (ret)
		return ret;

	writel(BM_OCOTP_CTRL_ERROR, otp->base + STMP_OFFSET_REG_CLR);

	ret = mxs_ocotp_wait(otp);
	if (ret)
		goto disable_clk;

	/* open OCOTP banks for read */
	writel(BM_OCOTP_CTRL_RD_BANK_OPEN, otp->base + STMP_OFFSET_REG_SET);

	/* approximately wait 33 hclk cycles */
	udelay(1);

	ret = mxs_ocotp_wait(otp);
	if (ret)
		goto close_banks;

	while (val_size) {
		if ((offset < OCOTP_DATA_OFFSET) || (offset % 16)) {
			/* fill up non-data register */
			*buf = 0;
		} else {
			*buf = readl(otp->base + offset);
		}

		buf++;
		val_size--;
		offset += reg_size;
	}

close_banks:
	/* close banks for power saving */
	writel(BM_OCOTP_CTRL_RD_BANK_OPEN, otp->base + STMP_OFFSET_REG_CLR);

disable_clk:
	clk_disable(otp->clk);

	return ret;
}

static int mxs_ocotp_write(void *context, const void *data, size_t count)
{
	/* We don't want to support writing */
	return 0;
}

static bool mxs_ocotp_writeable_reg(struct device *dev, unsigned int reg)
{
	return false;
}

static struct nvmem_config ocotp_config = {
	.name = "mxs-ocotp",
	.owner = THIS_MODULE,
};

static const struct regmap_range imx23_ranges[] = {
	regmap_reg_range(OCOTP_DATA_OFFSET, 0x210),
};

static const struct regmap_access_table imx23_access = {
	.yes_ranges = imx23_ranges,
	.n_yes_ranges = ARRAY_SIZE(imx23_ranges),
};

static const struct regmap_range imx28_ranges[] = {
	regmap_reg_range(OCOTP_DATA_OFFSET, 0x290),
};

static const struct regmap_access_table imx28_access = {
	.yes_ranges = imx28_ranges,
	.n_yes_ranges = ARRAY_SIZE(imx28_ranges),
};

static struct regmap_bus mxs_ocotp_bus = {
	.read = mxs_ocotp_read,
	.write = mxs_ocotp_write, /* make regmap_init() happy */
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static struct regmap_config mxs_ocotp_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 16,
	.writeable_reg = mxs_ocotp_writeable_reg,
};

static const struct of_device_id mxs_ocotp_match[] = {
	{ .compatible = "fsl,imx23-ocotp", .data = &imx23_access },
	{ .compatible = "fsl,imx28-ocotp", .data = &imx28_access },
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, mxs_ocotp_match);

static int mxs_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxs_ocotp *otp;
	struct resource *res;
	const struct of_device_id *match;
	struct regmap *regmap;
	const struct regmap_access_table *access;
	int ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	otp = devm_kzalloc(dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	otp->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(otp->base))
		return PTR_ERR(otp->base);

	otp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(otp->clk))
		return PTR_ERR(otp->clk);

	ret = clk_prepare(otp->clk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare clk: %d\n", ret);
		return ret;
	}

	access = match->data;
	mxs_ocotp_config.rd_table = access;
	mxs_ocotp_config.max_register = access->yes_ranges[0].range_max;

	regmap = devm_regmap_init(dev, &mxs_ocotp_bus, otp, &mxs_ocotp_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "regmap init failed\n");
		ret = PTR_ERR(regmap);
		goto err_clk;
	}

	ocotp_config.dev = dev;
	otp->nvmem = nvmem_register(&ocotp_config);
	if (IS_ERR(otp->nvmem)) {
		ret = PTR_ERR(otp->nvmem);
		goto err_clk;
	}

	platform_set_drvdata(pdev, otp);

	return 0;

err_clk:
	clk_unprepare(otp->clk);

	return ret;
}

static int mxs_ocotp_remove(struct platform_device *pdev)
{
	struct mxs_ocotp *otp = platform_get_drvdata(pdev);

	clk_unprepare(otp->clk);

	return nvmem_unregister(otp->nvmem);
}

static struct platform_driver mxs_ocotp_driver = {
	.probe = mxs_ocotp_probe,
	.remove = mxs_ocotp_remove,
	.driver = {
		.name = "mxs-ocotp",
		.of_match_table = mxs_ocotp_match,
	},
};

module_platform_driver(mxs_ocotp_driver);
MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("driver for OCOTP in i.MX23/i.MX28");
MODULE_LICENSE("GPL v2");
