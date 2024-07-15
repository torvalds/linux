// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale MXS On-Chip OTP driver
 *
 * Copyright (C) 2015 Stefan Wahren <stefan.wahren@i2se.com>
 *
 * Based on the driver from Huang Shijie and Christoph G. Baumann
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
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

static int mxs_ocotp_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct mxs_ocotp *otp = context;
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

	while (bytes) {
		if ((offset < OCOTP_DATA_OFFSET) || (offset % 16)) {
			/* fill up non-data register */
			*buf++ = 0;
		} else {
			*buf++ = readl(otp->base + offset);
		}

		bytes -= 4;
		offset += 4;
	}

close_banks:
	/* close banks for power saving */
	writel(BM_OCOTP_CTRL_RD_BANK_OPEN, otp->base + STMP_OFFSET_REG_CLR);

disable_clk:
	clk_disable(otp->clk);

	return ret;
}

static struct nvmem_config ocotp_config = {
	.name = "mxs-ocotp",
	.stride = 16,
	.word_size = 4,
	.reg_read = mxs_ocotp_read,
};

struct mxs_data {
	int size;
};

static const struct mxs_data imx23_data = {
	.size = 0x220,
};

static const struct mxs_data imx28_data = {
	.size = 0x2a0,
};

static const struct of_device_id mxs_ocotp_match[] = {
	{ .compatible = "fsl,imx23-ocotp", .data = &imx23_data },
	{ .compatible = "fsl,imx28-ocotp", .data = &imx28_data },
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, mxs_ocotp_match);

static void mxs_ocotp_action(void *data)
{
	clk_unprepare(data);
}

static int mxs_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct mxs_data *data;
	struct mxs_ocotp *otp;
	int ret;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	otp = devm_kzalloc(dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->base = devm_platform_ioremap_resource(pdev, 0);
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

	ret = devm_add_action_or_reset(&pdev->dev, mxs_ocotp_action, otp->clk);
	if (ret)
		return ret;

	ocotp_config.size = data->size;
	ocotp_config.priv = otp;
	ocotp_config.dev = dev;
	otp->nvmem = devm_nvmem_register(dev, &ocotp_config);
	if (IS_ERR(otp->nvmem))
		return PTR_ERR(otp->nvmem);

	platform_set_drvdata(pdev, otp);

	return 0;
}

static struct platform_driver mxs_ocotp_driver = {
	.probe = mxs_ocotp_probe,
	.driver = {
		.name = "mxs-ocotp",
		.of_match_table = mxs_ocotp_match,
	},
};

module_platform_driver(mxs_ocotp_driver);
MODULE_AUTHOR("Stefan Wahren <wahrenst@gmx.net");
MODULE_DESCRIPTION("driver for OCOTP in i.MX23/i.MX28");
MODULE_LICENSE("GPL v2");
