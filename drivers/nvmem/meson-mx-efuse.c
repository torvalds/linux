/*
 * Amlogic Meson6, Meson8 and Meson8b eFuse Driver
 *
 * Copyright (c) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
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

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#define MESON_MX_EFUSE_CNTL1					0x04
#define MESON_MX_EFUSE_CNTL1_PD_ENABLE				BIT(27)
#define MESON_MX_EFUSE_CNTL1_AUTO_RD_BUSY			BIT(26)
#define MESON_MX_EFUSE_CNTL1_AUTO_RD_START			BIT(25)
#define MESON_MX_EFUSE_CNTL1_AUTO_RD_ENABLE			BIT(24)
#define MESON_MX_EFUSE_CNTL1_BYTE_WR_DATA			GENMASK(23, 16)
#define MESON_MX_EFUSE_CNTL1_AUTO_WR_BUSY			BIT(14)
#define MESON_MX_EFUSE_CNTL1_AUTO_WR_START			BIT(13)
#define MESON_MX_EFUSE_CNTL1_AUTO_WR_ENABLE			BIT(12)
#define MESON_MX_EFUSE_CNTL1_BYTE_ADDR_SET			BIT(11)
#define MESON_MX_EFUSE_CNTL1_BYTE_ADDR_MASK			GENMASK(10, 0)

#define MESON_MX_EFUSE_CNTL2					0x08

#define MESON_MX_EFUSE_CNTL4					0x10
#define MESON_MX_EFUSE_CNTL4_ENCRYPT_ENABLE			BIT(10)

struct meson_mx_efuse_platform_data {
	const char *name;
	unsigned int word_size;
};

struct meson_mx_efuse {
	void __iomem *base;
	struct clk *core_clk;
	struct nvmem_device *nvmem;
	struct nvmem_config config;
};

static void meson_mx_efuse_mask_bits(struct meson_mx_efuse *efuse, u32 reg,
				     u32 mask, u32 set)
{
	u32 data;

	data = readl(efuse->base + reg);
	data &= ~mask;
	data |= (set & mask);

	writel(data, efuse->base + reg);
}

static int meson_mx_efuse_hw_enable(struct meson_mx_efuse *efuse)
{
	int err;

	err = clk_prepare_enable(efuse->core_clk);
	if (err)
		return err;

	/* power up the efuse */
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_PD_ENABLE, 0);

	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL4,
				 MESON_MX_EFUSE_CNTL4_ENCRYPT_ENABLE, 0);

	return 0;
}

static void meson_mx_efuse_hw_disable(struct meson_mx_efuse *efuse)
{
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_PD_ENABLE,
				 MESON_MX_EFUSE_CNTL1_PD_ENABLE);

	clk_disable_unprepare(efuse->core_clk);
}

static int meson_mx_efuse_read_addr(struct meson_mx_efuse *efuse,
				    unsigned int addr, u32 *value)
{
	int err;
	u32 regval;

	/* write the address to read */
	regval = FIELD_PREP(MESON_MX_EFUSE_CNTL1_BYTE_ADDR_MASK, addr);
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_BYTE_ADDR_MASK, regval);

	/* inform the hardware that we changed the address */
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_BYTE_ADDR_SET,
				 MESON_MX_EFUSE_CNTL1_BYTE_ADDR_SET);
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_BYTE_ADDR_SET, 0);

	/* start the read process */
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_START,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_START);
	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_START, 0);

	/*
	 * perform a dummy read to ensure that the HW has the RD_BUSY bit set
	 * when polling for the status below.
	 */
	readl(efuse->base + MESON_MX_EFUSE_CNTL1);

	err = readl_poll_timeout_atomic(efuse->base + MESON_MX_EFUSE_CNTL1,
			regval,
			(!(regval & MESON_MX_EFUSE_CNTL1_AUTO_RD_BUSY)),
			1, 1000);
	if (err) {
		dev_err(efuse->config.dev,
			"Timeout while reading efuse address %u\n", addr);
		return err;
	}

	*value = readl(efuse->base + MESON_MX_EFUSE_CNTL2);

	return 0;
}

static int meson_mx_efuse_read(void *context, unsigned int offset,
			       void *buf, size_t bytes)
{
	struct meson_mx_efuse *efuse = context;
	u32 tmp;
	int err, i, addr;

	err = meson_mx_efuse_hw_enable(efuse);
	if (err)
		return err;

	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_ENABLE,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_ENABLE);

	for (i = 0; i < bytes; i += efuse->config.word_size) {
		addr = (offset + i) / efuse->config.word_size;

		err = meson_mx_efuse_read_addr(efuse, addr, &tmp);
		if (err)
			break;

		memcpy(buf + i, &tmp, efuse->config.word_size);
	}

	meson_mx_efuse_mask_bits(efuse, MESON_MX_EFUSE_CNTL1,
				 MESON_MX_EFUSE_CNTL1_AUTO_RD_ENABLE, 0);

	meson_mx_efuse_hw_disable(efuse);

	return err;
}

static const struct meson_mx_efuse_platform_data meson6_efuse_data = {
	.name = "meson6-efuse",
	.word_size = 1,
};

static const struct meson_mx_efuse_platform_data meson8_efuse_data = {
	.name = "meson8-efuse",
	.word_size = 4,
};

static const struct meson_mx_efuse_platform_data meson8b_efuse_data = {
	.name = "meson8b-efuse",
	.word_size = 4,
};

static const struct of_device_id meson_mx_efuse_match[] = {
	{ .compatible = "amlogic,meson6-efuse", .data = &meson6_efuse_data },
	{ .compatible = "amlogic,meson8-efuse", .data = &meson8_efuse_data },
	{ .compatible = "amlogic,meson8b-efuse", .data = &meson8b_efuse_data },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_mx_efuse_match);

static int meson_mx_efuse_probe(struct platform_device *pdev)
{
	const struct meson_mx_efuse_platform_data *drvdata;
	struct meson_mx_efuse *efuse;
	struct resource *res;

	drvdata = of_device_get_match_data(&pdev->dev);
	if (!drvdata)
		return -EINVAL;

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(efuse->base))
		return PTR_ERR(efuse->base);

	efuse->config.name = devm_kstrdup(&pdev->dev, drvdata->name,
					  GFP_KERNEL);
	efuse->config.owner = THIS_MODULE;
	efuse->config.dev = &pdev->dev;
	efuse->config.priv = efuse;
	efuse->config.stride = drvdata->word_size;
	efuse->config.word_size = drvdata->word_size;
	efuse->config.size = SZ_512;
	efuse->config.read_only = true;
	efuse->config.reg_read = meson_mx_efuse_read;

	efuse->core_clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(efuse->core_clk)) {
		dev_err(&pdev->dev, "Failed to get core clock\n");
		return PTR_ERR(efuse->core_clk);
	}

	efuse->nvmem = devm_nvmem_register(&pdev->dev, &efuse->config);

	return PTR_ERR_OR_ZERO(efuse->nvmem);
}

static struct platform_driver meson_mx_efuse_driver = {
	.probe = meson_mx_efuse_probe,
	.driver = {
		.name = "meson-mx-efuse",
		.of_match_table = meson_mx_efuse_match,
	},
};

module_platform_driver(meson_mx_efuse_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Amlogic Meson MX eFuse NVMEM driver");
MODULE_LICENSE("GPL v2");
