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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/platform_data/b53.h>

#include "b53_priv.h"

struct b53_mmap_priv {
	void __iomem *regs;
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
};

static int b53_mmap_probe(struct platform_device *pdev)
{
	struct b53_platform_data *pdata = pdev->dev.platform_data;
	struct b53_mmap_priv *priv;
	struct b53_device *dev;

	if (!pdata)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = pdata->regs;

	dev = b53_switch_alloc(&pdev->dev, &b53_mmap_ops, priv);
	if (!dev)
		return -ENOMEM;

	dev->pdata = pdata;

	platform_set_drvdata(pdev, dev);

	return b53_switch_register(dev);
}

static int b53_mmap_remove(struct platform_device *pdev)
{
	struct b53_device *dev = platform_get_drvdata(pdev);

	if (dev)
		b53_switch_remove(dev);

	return 0;
}

static const struct of_device_id b53_mmap_of_table[] = {
	{ .compatible = "brcm,bcm3384-switch" },
	{ .compatible = "brcm,bcm6328-switch" },
	{ .compatible = "brcm,bcm6368-switch" },
	{ .compatible = "brcm,bcm63xx-switch" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, b53_mmap_of_table);

static struct platform_driver b53_mmap_driver = {
	.probe = b53_mmap_probe,
	.remove = b53_mmap_remove,
	.driver = {
		.name = "b53-switch",
		.of_match_table = b53_mmap_of_table,
	},
};

module_platform_driver(b53_mmap_driver);
MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("B53 MMAP access driver");
MODULE_LICENSE("Dual BSD/GPL");
