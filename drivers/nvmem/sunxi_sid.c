/*
 * Allwinner sunXi SoCs Security ID support.
 *
 * Copyright (c) 2013 Oliver Schinagl <oliver@schinagl.nl>
 * Copyright (C) 2014 Maxime Ripard <maxime.ripard@free-electrons.com>
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
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/random.h>

/* Registers and special values for doing register-based SID readout on H3 */
#define SUN8I_SID_PRCTL		0x40
#define SUN8I_SID_RDKEY		0x60

#define SUN8I_SID_OFFSET_MASK	0x1FF
#define SUN8I_SID_OFFSET_SHIFT	16
#define SUN8I_SID_OP_LOCK	(0xAC << 8)
#define SUN8I_SID_READ		BIT(1)

static struct nvmem_config econfig = {
	.name = "sunxi-sid",
	.read_only = true,
	.stride = 4,
	.word_size = 1,
};

struct sunxi_sid_cfg {
	u32	value_offset;
	u32	size;
	bool	need_register_readout;
};

struct sunxi_sid {
	void __iomem		*base;
	u32			value_offset;
};

/* We read the entire key, due to a 32 bit read alignment requirement. Since we
 * want to return the requested byte, this results in somewhat slower code and
 * uses 4 times more reads as needed but keeps code simpler. Since the SID is
 * only very rarely probed, this is not really an issue.
 */
static u8 sunxi_sid_read_byte(const struct sunxi_sid *sid,
			      const unsigned int offset)
{
	u32 sid_key;

	sid_key = ioread32be(sid->base + round_down(offset, 4));
	sid_key >>= (offset % 4) * 8;

	return sid_key; /* Only return the last byte */
}

static int sunxi_sid_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct sunxi_sid *sid = context;
	u8 *buf = val;

	/* Offset the read operation to the real position of SID */
	offset += sid->value_offset;

	while (bytes--)
		*buf++ = sunxi_sid_read_byte(sid, offset++);

	return 0;
}

static int sun8i_sid_register_readout(const struct sunxi_sid *sid,
				      const unsigned int offset,
				      u32 *out)
{
	u32 reg_val;
	int ret;

	/* Set word, lock access, and set read command */
	reg_val = (offset & SUN8I_SID_OFFSET_MASK)
		  << SUN8I_SID_OFFSET_SHIFT;
	reg_val |= SUN8I_SID_OP_LOCK | SUN8I_SID_READ;
	writel(reg_val, sid->base + SUN8I_SID_PRCTL);

	ret = readl_poll_timeout(sid->base + SUN8I_SID_PRCTL, reg_val,
				 !(reg_val & SUN8I_SID_READ), 100, 250000);
	if (ret)
		return ret;

	if (out)
		*out = readl(sid->base + SUN8I_SID_RDKEY);

	writel(0, sid->base + SUN8I_SID_PRCTL);

	return 0;
}

/*
 * On Allwinner H3, the value on the 0x200 offset of the SID controller seems
 * to be not reliable at all.
 * Read by the registers instead.
 */
static int sun8i_sid_read_byte_by_reg(const struct sunxi_sid *sid,
				      const unsigned int offset,
				      u8 *out)
{
	u32 word;
	int ret;

	ret = sun8i_sid_register_readout(sid, offset & ~0x03, &word);

	if (ret)
		return ret;

	*out = (word >> ((offset & 0x3) * 8)) & 0xff;

	return 0;
}

static int sun8i_sid_read_by_reg(void *context, unsigned int offset,
				 void *val, size_t bytes)
{
	struct sunxi_sid *sid = context;
	u8 *buf = val;
	int ret;

	while (bytes--) {
		ret = sun8i_sid_read_byte_by_reg(sid, offset++, buf++);
		if (ret)
			return ret;
	}

	return 0;
}

static int sunxi_sid_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct sunxi_sid *sid;
	int ret, i, size;
	char *randomness;
	const struct sunxi_sid_cfg *cfg;

	sid = devm_kzalloc(dev, sizeof(*sid), GFP_KERNEL);
	if (!sid)
		return -ENOMEM;

	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -EINVAL;
	sid->value_offset = cfg->value_offset;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sid->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(sid->base))
		return PTR_ERR(sid->base);

	size = cfg->size;

	econfig.size = size;
	econfig.dev = dev;
	if (cfg->need_register_readout)
		econfig.reg_read = sun8i_sid_read_by_reg;
	else
		econfig.reg_read = sunxi_sid_read;
	econfig.priv = sid;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	randomness = kzalloc(size, GFP_KERNEL);
	if (!randomness) {
		ret = -ENOMEM;
		goto err_unreg_nvmem;
	}

	for (i = 0; i < size; i++)
		econfig.reg_read(sid, i, &randomness[i], 1);

	add_device_randomness(randomness, size);
	kfree(randomness);

	platform_set_drvdata(pdev, nvmem);

	return 0;

err_unreg_nvmem:
	nvmem_unregister(nvmem);
	return ret;
}

static int sunxi_sid_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct sunxi_sid_cfg sun4i_a10_cfg = {
	.size = 0x10,
};

static const struct sunxi_sid_cfg sun7i_a20_cfg = {
	.size = 0x200,
};

static const struct sunxi_sid_cfg sun8i_h3_cfg = {
	.value_offset = 0x200,
	.size = 0x100,
	.need_register_readout = true,
};

static const struct sunxi_sid_cfg sun50i_a64_cfg = {
	.value_offset = 0x200,
	.size = 0x100,
};

static const struct of_device_id sunxi_sid_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-sid", .data = &sun4i_a10_cfg },
	{ .compatible = "allwinner,sun7i-a20-sid", .data = &sun7i_a20_cfg },
	{ .compatible = "allwinner,sun8i-h3-sid", .data = &sun8i_h3_cfg },
	{ .compatible = "allwinner,sun50i-a64-sid", .data = &sun50i_a64_cfg },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sunxi_sid_of_match);

static struct platform_driver sunxi_sid_driver = {
	.probe = sunxi_sid_probe,
	.remove = sunxi_sid_remove,
	.driver = {
		.name = "eeprom-sunxi-sid",
		.of_match_table = sunxi_sid_of_match,
	},
};
module_platform_driver(sunxi_sid_driver);

MODULE_AUTHOR("Oliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Allwinner sunxi security id driver");
MODULE_LICENSE("GPL");
