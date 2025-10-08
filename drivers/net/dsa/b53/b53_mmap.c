/*
 * B53 register access through memory mapped registers
 *
 * Copyright (C) 2012-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/platform_data/b53.h>
#include <linux/regmap.h>

#include "b53_priv.h"

#define BCM63XX_EPHY_REG 0x3C

struct b53_phy_info {
	u32 ephy_enable_mask;
	u32 ephy_port_mask;
	u32 ephy_bias_bit;
	const u32 *ephy_offset;
};

struct b53_mmap_priv {
	void __iomem *regs;
	struct regmap *gpio_ctrl;
	const struct b53_phy_info *phy_info;
	u32 phys_enabled;
};

static const u32 bcm6318_ephy_offsets[] = {4, 5, 6, 7};

static const struct b53_phy_info bcm6318_ephy_info = {
	.ephy_enable_mask = BIT(0) | BIT(4) | BIT(8) | BIT(12) | BIT(16),
	.ephy_port_mask = GENMASK((ARRAY_SIZE(bcm6318_ephy_offsets) - 1), 0),
	.ephy_bias_bit = 24,
	.ephy_offset = bcm6318_ephy_offsets,
};

static const u32 bcm6368_ephy_offsets[] = {2, 3, 4, 5};

static const struct b53_phy_info bcm6368_ephy_info = {
	.ephy_enable_mask = BIT(0),
	.ephy_port_mask = GENMASK((ARRAY_SIZE(bcm6368_ephy_offsets) - 1), 0),
	.ephy_bias_bit = 0,
	.ephy_offset = bcm6368_ephy_offsets,
};

static const u32 bcm63268_ephy_offsets[] = {4, 9, 14};

static const struct b53_phy_info bcm63268_ephy_info = {
	.ephy_enable_mask = GENMASK(4, 0),
	.ephy_port_mask = GENMASK((ARRAY_SIZE(bcm63268_ephy_offsets) - 1), 0),
	.ephy_bias_bit = 24,
	.ephy_offset = bcm63268_ephy_offsets,
};

static int b53_mmap_read8(struct b53_device *dev, u8 page, u8 reg, u8 *val)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	*val = readb(regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_read16(struct b53_device *dev, u8 page, u8 reg, u16 *val)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	if (WARN_ON(reg % 2))
		return -EINVAL;

	if (dev->pdata && dev->pdata->big_endian)
		*val = ioread16be(regs + (page << 8) + reg);
	else
		*val = readw(regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_read32(struct b53_device *dev, u8 page, u8 reg, u32 *val)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	if (WARN_ON(reg % 4))
		return -EINVAL;

	if (dev->pdata && dev->pdata->big_endian)
		*val = ioread32be(regs + (page << 8) + reg);
	else
		*val = readl(regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_read48(struct b53_device *dev, u8 page, u8 reg, u64 *val)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	if (WARN_ON(reg % 2))
		return -EINVAL;

	if (reg % 4) {
		u16 lo;
		u32 hi;

		if (dev->pdata && dev->pdata->big_endian) {
			lo = ioread16be(regs + (page << 8) + reg);
			hi = ioread32be(regs + (page << 8) + reg + 2);
		} else {
			lo = readw(regs + (page << 8) + reg);
			hi = readl(regs + (page << 8) + reg + 2);
		}

		*val = ((u64)hi << 16) | lo;
	} else {
		u32 lo;
		u16 hi;

		if (dev->pdata && dev->pdata->big_endian) {
			lo = ioread32be(regs + (page << 8) + reg);
			hi = ioread16be(regs + (page << 8) + reg + 4);
		} else {
			lo = readl(regs + (page << 8) + reg);
			hi = readw(regs + (page << 8) + reg + 4);
		}

		*val = ((u64)hi << 32) | lo;
	}

	return 0;
}

static int b53_mmap_read64(struct b53_device *dev, u8 page, u8 reg, u64 *val)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;
	u32 hi, lo;

	if (WARN_ON(reg % 4))
		return -EINVAL;

	if (dev->pdata && dev->pdata->big_endian) {
		lo = ioread32be(regs + (page << 8) + reg);
		hi = ioread32be(regs + (page << 8) + reg + 4);
	} else {
		lo = readl(regs + (page << 8) + reg);
		hi = readl(regs + (page << 8) + reg + 4);
	}

	*val = ((u64)hi << 32) | lo;

	return 0;
}

static int b53_mmap_write8(struct b53_device *dev, u8 page, u8 reg, u8 value)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	writeb(value, regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_write16(struct b53_device *dev, u8 page, u8 reg,
			    u16 value)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	if (WARN_ON(reg % 2))
		return -EINVAL;

	if (dev->pdata && dev->pdata->big_endian)
		iowrite16be(value, regs + (page << 8) + reg);
	else
		writew(value, regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_write32(struct b53_device *dev, u8 page, u8 reg,
			    u32 value)
{
	struct b53_mmap_priv *priv = dev->priv;
	void __iomem *regs = priv->regs;

	if (WARN_ON(reg % 4))
		return -EINVAL;

	if (dev->pdata && dev->pdata->big_endian)
		iowrite32be(value, regs + (page << 8) + reg);
	else
		writel(value, regs + (page << 8) + reg);

	return 0;
}

static int b53_mmap_write48(struct b53_device *dev, u8 page, u8 reg,
			    u64 value)
{
	if (WARN_ON(reg % 2))
		return -EINVAL;

	if (reg % 4) {
		u32 hi = (u32)(value >> 16);
		u16 lo = (u16)value;

		b53_mmap_write16(dev, page, reg, lo);
		b53_mmap_write32(dev, page, reg + 2, hi);
	} else {
		u16 hi = (u16)(value >> 32);
		u32 lo = (u32)value;

		b53_mmap_write32(dev, page, reg, lo);
		b53_mmap_write16(dev, page, reg + 4, hi);
	}

	return 0;
}

static int b53_mmap_write64(struct b53_device *dev, u8 page, u8 reg,
			    u64 value)
{
	u32 hi, lo;

	hi = upper_32_bits(value);
	lo = lower_32_bits(value);

	if (WARN_ON(reg % 4))
		return -EINVAL;

	b53_mmap_write32(dev, page, reg, lo);
	b53_mmap_write32(dev, page, reg + 4, hi);

	return 0;
}

static int b53_mmap_phy_read16(struct b53_device *dev, int addr, int reg,
			       u16 *value)
{
	return -EIO;
}

static int b53_mmap_phy_write16(struct b53_device *dev, int addr, int reg,
				u16 value)
{
	return -EIO;
}

static int bcm63xx_ephy_set(struct b53_device *dev, int port, bool enable)
{
	struct b53_mmap_priv *priv = dev->priv;
	const struct b53_phy_info *info = priv->phy_info;
	struct regmap *gpio_ctrl = priv->gpio_ctrl;
	u32 mask, val;

	if (enable) {
		mask = (info->ephy_enable_mask << info->ephy_offset[port])
				| BIT(info->ephy_bias_bit);
		val = 0;
	} else {
		mask = (info->ephy_enable_mask << info->ephy_offset[port]);
		if (!((priv->phys_enabled & ~BIT(port)) & info->ephy_port_mask))
			mask |= BIT(info->ephy_bias_bit);
		val = mask;
	}
	return regmap_update_bits(gpio_ctrl, BCM63XX_EPHY_REG, mask, val);
}

static void b53_mmap_phy_enable(struct b53_device *dev, int port)
{
	struct b53_mmap_priv *priv = dev->priv;
	int ret = 0;

	if (priv->phy_info && (BIT(port) & priv->phy_info->ephy_port_mask))
		ret = bcm63xx_ephy_set(dev, port, true);

	if (!ret)
		priv->phys_enabled |= BIT(port);
}

static void b53_mmap_phy_disable(struct b53_device *dev, int port)
{
	struct b53_mmap_priv *priv = dev->priv;
	int ret = 0;

	if (priv->phy_info && (BIT(port) & priv->phy_info->ephy_port_mask))
		ret = bcm63xx_ephy_set(dev, port, false);

	if (!ret)
		priv->phys_enabled &= ~BIT(port);
}

static const struct b53_io_ops b53_mmap_ops = {
	.read8 = b53_mmap_read8,
	.read16 = b53_mmap_read16,
	.read32 = b53_mmap_read32,
	.read48 = b53_mmap_read48,
	.read64 = b53_mmap_read64,
	.write8 = b53_mmap_write8,
	.write16 = b53_mmap_write16,
	.write32 = b53_mmap_write32,
	.write48 = b53_mmap_write48,
	.write64 = b53_mmap_write64,
	.phy_read16 = b53_mmap_phy_read16,
	.phy_write16 = b53_mmap_phy_write16,
	.phy_enable = b53_mmap_phy_enable,
	.phy_disable = b53_mmap_phy_disable,
};

static int b53_mmap_probe_of(struct platform_device *pdev,
			     struct b53_platform_data **ppdata)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *of_ports, *of_port;
	struct device *dev = &pdev->dev;
	struct b53_platform_data *pdata;
	void __iomem *mem;

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	pdata = devm_kzalloc(dev, sizeof(struct b53_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->regs = mem;
	pdata->chip_id = (u32)(unsigned long)device_get_match_data(dev);
	pdata->big_endian = of_property_read_bool(np, "big-endian");

	of_ports = of_get_child_by_name(np, "ports");
	if (!of_ports) {
		dev_err(dev, "no ports child node found\n");
		return -EINVAL;
	}

	for_each_available_child_of_node(of_ports, of_port) {
		u32 reg;

		if (of_property_read_u32(of_port, "reg", &reg))
			continue;

		if (reg < B53_N_PORTS)
			pdata->enabled_ports |= BIT(reg);
	}

	of_node_put(of_ports);
	*ppdata = pdata;

	return 0;
}

static int b53_mmap_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct b53_platform_data *pdata = pdev->dev.platform_data;
	struct b53_mmap_priv *priv;
	struct b53_device *dev;
	int ret;

	if (!pdata && np) {
		ret = b53_mmap_probe_of(pdev, &pdata);
		if (ret) {
			dev_err(&pdev->dev, "OF probe error\n");
			return ret;
		}
	}

	if (!pdata)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = pdata->regs;

	priv->gpio_ctrl = syscon_regmap_lookup_by_phandle(np, "brcm,gpio-ctrl");
	if (!IS_ERR(priv->gpio_ctrl)) {
		if (pdata->chip_id == BCM6318_DEVICE_ID ||
		    pdata->chip_id == BCM6328_DEVICE_ID ||
		    pdata->chip_id == BCM6362_DEVICE_ID)
			priv->phy_info = &bcm6318_ephy_info;
		else if (pdata->chip_id == BCM6368_DEVICE_ID)
			priv->phy_info = &bcm6368_ephy_info;
		else if (pdata->chip_id == BCM63268_DEVICE_ID)
			priv->phy_info = &bcm63268_ephy_info;
	}

	dev = b53_switch_alloc(&pdev->dev, &b53_mmap_ops, priv);
	if (!dev)
		return -ENOMEM;

	dev->pdata = pdata;

	platform_set_drvdata(pdev, dev);

	return b53_switch_register(dev);
}

static void b53_mmap_remove(struct platform_device *pdev)
{
	struct b53_device *dev = platform_get_drvdata(pdev);

	if (dev)
		b53_switch_remove(dev);
}

static void b53_mmap_shutdown(struct platform_device *pdev)
{
	struct b53_device *dev = platform_get_drvdata(pdev);

	if (dev)
		b53_switch_shutdown(dev);

	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id b53_mmap_of_table[] = {
	{
		.compatible = "brcm,bcm3384-switch",
		.data = (void *)BCM63XX_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm6318-switch",
		.data = (void *)BCM6318_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm6328-switch",
		.data = (void *)BCM6328_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm6362-switch",
		.data = (void *)BCM6362_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm6368-switch",
		.data = (void *)BCM6368_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm63268-switch",
		.data = (void *)BCM63268_DEVICE_ID,
	}, {
		.compatible = "brcm,bcm63xx-switch",
		.data = (void *)BCM63XX_DEVICE_ID,
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, b53_mmap_of_table);

static struct platform_driver b53_mmap_driver = {
	.probe = b53_mmap_probe,
	.remove = b53_mmap_remove,
	.shutdown = b53_mmap_shutdown,
	.driver = {
		.name = "b53-switch",
		.of_match_table = b53_mmap_of_table,
	},
};

module_platform_driver(b53_mmap_driver);
MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("B53 MMAP access driver");
MODULE_LICENSE("Dual BSD/GPL");
