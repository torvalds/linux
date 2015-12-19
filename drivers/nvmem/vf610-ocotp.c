/*
 * Copyright (C) 2015 Toradex AG.
 *
 * Author: Sanchayan Maity <sanchayan.maity@toradex.com>
 *
 * Based on the barebox ocotp driver,
 * Copyright (c) 2010 Baruch Siach <baruch@tkos.co.il>
 *	Orex Computed Radiography
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* OCOTP Register Offsets */
#define OCOTP_CTRL_REG				0x00
#define OCOTP_CTRL_SET				0x04
#define OCOTP_CTRL_CLR				0x08
#define OCOTP_TIMING				0x10
#define OCOTP_DATA				0x20
#define OCOTP_READ_CTRL_REG			0x30
#define OCOTP_READ_FUSE_DATA			0x40

/* OCOTP Register bits and masks */
#define OCOTP_CTRL_WR_UNLOCK			16
#define OCOTP_CTRL_WR_UNLOCK_KEY		0x3E77
#define OCOTP_CTRL_WR_UNLOCK_MASK		GENMASK(31, 16)
#define OCOTP_CTRL_ADDR				0
#define OCOTP_CTRL_ADDR_MASK			GENMASK(6, 0)
#define OCOTP_CTRL_RELOAD_SHADOWS		BIT(10)
#define OCOTP_CTRL_ERR				BIT(9)
#define OCOTP_CTRL_BUSY				BIT(8)

#define OCOTP_TIMING_STROBE_READ		16
#define OCOTP_TIMING_STROBE_READ_MASK		GENMASK(21, 16)
#define OCOTP_TIMING_RELAX			12
#define OCOTP_TIMING_RELAX_MASK			GENMASK(15, 12)
#define OCOTP_TIMING_STROBE_PROG		0
#define OCOTP_TIMING_STROBE_PROG_MASK		GENMASK(11, 0)

#define OCOTP_READ_CTRL_READ_FUSE		0x1

#define VF610_OCOTP_TIMEOUT			100000

#define BF(value, field)		(((value) << field) & field##_MASK)

#define DEF_RELAX				20

static const int base_to_fuse_addr_mappings[][2] = {
	{0x400, 0x00},
	{0x410, 0x01},
	{0x420, 0x02},
	{0x450, 0x05},
	{0x4F0, 0x0F},
	{0x600, 0x20},
	{0x610, 0x21},
	{0x620, 0x22},
	{0x630, 0x23},
	{0x640, 0x24},
	{0x650, 0x25},
	{0x660, 0x26},
	{0x670, 0x27},
	{0x6F0, 0x2F},
	{0x880, 0x38},
	{0x890, 0x39},
	{0x8A0, 0x3A},
	{0x8B0, 0x3B},
	{0x8C0, 0x3C},
	{0x8D0, 0x3D},
	{0x8E0, 0x3E},
	{0x8F0, 0x3F},
	{0xC80, 0x78},
	{0xC90, 0x79},
	{0xCA0, 0x7A},
	{0xCB0, 0x7B},
	{0xCC0, 0x7C},
	{0xCD0, 0x7D},
	{0xCE0, 0x7E},
	{0xCF0, 0x7F},
};

struct vf610_ocotp {
	void __iomem *base;
	struct clk *clk;
	struct device *dev;
	struct nvmem_device *nvmem;
	int timing;
};

static int vf610_ocotp_wait_busy(void __iomem *base)
{
	int timeout = VF610_OCOTP_TIMEOUT;

	while ((readl(base) & OCOTP_CTRL_BUSY) && --timeout)
		udelay(10);

	if (!timeout) {
		writel(OCOTP_CTRL_ERR, base + OCOTP_CTRL_CLR);
		return -ETIMEDOUT;
	}

	udelay(10);

	return 0;
}

static int vf610_ocotp_calculate_timing(struct vf610_ocotp *ocotp_dev)
{
	u32 clk_rate;
	u32 relax, strobe_read, strobe_prog;
	u32 timing;

	clk_rate = clk_get_rate(ocotp_dev->clk);

	/* Refer section OTP read/write timing parameters in TRM */
	relax = clk_rate / (1000000000 / DEF_RELAX) - 1;
	strobe_prog = clk_rate / (1000000000 / 10000) + 2 * (DEF_RELAX + 1) - 1;
	strobe_read = clk_rate / (1000000000 / 40) + 2 * (DEF_RELAX + 1) - 1;

	timing = BF(relax, OCOTP_TIMING_RELAX);
	timing |= BF(strobe_read, OCOTP_TIMING_STROBE_READ);
	timing |= BF(strobe_prog, OCOTP_TIMING_STROBE_PROG);

	return timing;
}

static int vf610_get_fuse_address(int base_addr_offset)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(base_to_fuse_addr_mappings); i++) {
		if (base_to_fuse_addr_mappings[i][0] == base_addr_offset)
			return base_to_fuse_addr_mappings[i][1];
	}

	return -EINVAL;
}

static int vf610_ocotp_write(void *context, const void *data, size_t count)
{
	return 0;
}

static int vf610_ocotp_read(void *context,
			const void *off, size_t reg_size,
			void *val, size_t val_size)
{
	struct vf610_ocotp *ocotp = context;
	void __iomem *base = ocotp->base;
	unsigned int offset = *(u32 *)off;
	u32 reg, *buf = val;
	int fuse_addr;
	int ret;

	while (val_size > 0) {
		fuse_addr = vf610_get_fuse_address(offset);
		if (fuse_addr > 0) {
			writel(ocotp->timing, base + OCOTP_TIMING);
			ret = vf610_ocotp_wait_busy(base + OCOTP_CTRL_REG);
			if (ret)
				return ret;

			reg = readl(base + OCOTP_CTRL_REG);
			reg &= ~OCOTP_CTRL_ADDR_MASK;
			reg &= ~OCOTP_CTRL_WR_UNLOCK_MASK;
			reg |= BF(fuse_addr, OCOTP_CTRL_ADDR);
			writel(reg, base + OCOTP_CTRL_REG);

			writel(OCOTP_READ_CTRL_READ_FUSE,
				base + OCOTP_READ_CTRL_REG);
			ret = vf610_ocotp_wait_busy(base + OCOTP_CTRL_REG);
			if (ret)
				return ret;

			if (readl(base) & OCOTP_CTRL_ERR) {
				dev_dbg(ocotp->dev, "Error reading from fuse address %x\n",
					fuse_addr);
				writel(OCOTP_CTRL_ERR, base + OCOTP_CTRL_CLR);
			}

			/*
			 * In case of error, we do not abort and expect to read
			 * 0xBADABADA as mentioned by the TRM. We just read this
			 * value and return.
			 */
			*buf = readl(base + OCOTP_READ_FUSE_DATA);
		} else {
			*buf = 0;
		}

		buf++;
		val_size--;
		offset += reg_size;
	}

	return 0;
}

static struct regmap_bus vf610_ocotp_bus = {
	.read = vf610_ocotp_read,
	.write = vf610_ocotp_write,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static struct regmap_config ocotp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static struct nvmem_config ocotp_config = {
	.name = "ocotp",
	.owner = THIS_MODULE,
};

static const struct of_device_id ocotp_of_match[] = {
	{ .compatible = "fsl,vf610-ocotp", },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, ocotp_of_match);

static int vf610_ocotp_remove(struct platform_device *pdev)
{
	struct vf610_ocotp *ocotp_dev = platform_get_drvdata(pdev);

	return nvmem_unregister(ocotp_dev->nvmem);
}

static int vf610_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct regmap *regmap;
	struct vf610_ocotp *ocotp_dev;

	ocotp_dev = devm_kzalloc(&pdev->dev,
			sizeof(struct vf610_ocotp), GFP_KERNEL);
	if (!ocotp_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ocotp_dev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ocotp_dev->base))
		return PTR_ERR(ocotp_dev->base);

	ocotp_dev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ocotp_dev->clk)) {
		dev_err(dev, "failed getting clock, err = %ld\n",
			PTR_ERR(ocotp_dev->clk));
		return PTR_ERR(ocotp_dev->clk);
	}

	ocotp_regmap_config.max_register = resource_size(res);
	regmap = devm_regmap_init(dev,
		&vf610_ocotp_bus, ocotp_dev, &ocotp_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(regmap);
	}
	ocotp_config.dev = dev;

	ocotp_dev->nvmem = nvmem_register(&ocotp_config);
	if (IS_ERR(ocotp_dev->nvmem))
		return PTR_ERR(ocotp_dev->nvmem);

	ocotp_dev->dev = dev;
	platform_set_drvdata(pdev, ocotp_dev);

	ocotp_dev->timing = vf610_ocotp_calculate_timing(ocotp_dev);

	return 0;
}

static struct platform_driver vf610_ocotp_driver = {
	.probe = vf610_ocotp_probe,
	.remove = vf610_ocotp_remove,
	.driver = {
		.name = "vf610-ocotp",
		.of_match_table = ocotp_of_match,
	},
};
module_platform_driver(vf610_ocotp_driver);
MODULE_AUTHOR("Sanchayan Maity <sanchayan.maity@toradex.com>");
MODULE_DESCRIPTION("Vybrid OCOTP driver");
MODULE_LICENSE("GPL v2");
