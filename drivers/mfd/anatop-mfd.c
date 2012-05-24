/*
 * Anatop MFD driver
 *
 * Copyright (C) 2012 Ying-Chun Liu (PaulLiu) <paul.liu@linaro.org>
 * Copyright (C) 2012 Linaro
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/mfd/anatop.h>

u32 anatop_get_bits(struct anatop *adata, u32 addr, int bit_shift,
		    int bit_width)
{
	u32 val, mask;

	if (bit_width == 32)
		mask = ~0;
	else
		mask = (1 << bit_width) - 1;

	val = readl(adata->ioreg + addr);
	val = (val >> bit_shift) & mask;

	return val;
}
EXPORT_SYMBOL_GPL(anatop_get_bits);

void anatop_set_bits(struct anatop *adata, u32 addr, int bit_shift,
		     int bit_width, u32 data)
{
	u32 val, mask;

	if (bit_width == 32)
		mask = ~0;
	else
		mask = (1 << bit_width) - 1;

	spin_lock(&adata->reglock);
	val = readl(adata->ioreg + addr) & ~(mask << bit_shift);
	writel((data << bit_shift) | val, adata->ioreg + addr);
	spin_unlock(&adata->reglock);
}
EXPORT_SYMBOL_GPL(anatop_set_bits);

static const struct of_device_id of_anatop_match[] = {
	{ .compatible = "fsl,imx6q-anatop", },
	{ },
};

static int __devinit of_anatop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void *ioreg;
	struct anatop *drvdata;

	ioreg = of_iomap(np, 0);
	if (!ioreg)
		return -EADDRNOTAVAIL;
	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	drvdata->ioreg = ioreg;
	spin_lock_init(&drvdata->reglock);
	platform_set_drvdata(pdev, drvdata);
	of_platform_populate(np, of_anatop_match, NULL, dev);

	return 0;
}

static int __devexit of_anatop_remove(struct platform_device *pdev)
{
	struct anatop *drvdata;
	drvdata = platform_get_drvdata(pdev);
	iounmap(drvdata->ioreg);

	return 0;
}

static struct platform_driver anatop_of_driver = {
	.driver = {
		.name = "anatop-mfd",
		.owner = THIS_MODULE,
		.of_match_table = of_anatop_match,
	},
	.probe		= of_anatop_probe,
	.remove		= of_anatop_remove,
};

static int __init anatop_init(void)
{
	return platform_driver_register(&anatop_of_driver);
}
postcore_initcall(anatop_init);

static void __exit anatop_exit(void)
{
	platform_driver_unregister(&anatop_of_driver);
}
module_exit(anatop_exit);

MODULE_AUTHOR("Ying-Chun Liu (PaulLiu) <paul.liu@linaro.org>");
MODULE_DESCRIPTION("ANATOP MFD driver");
MODULE_LICENSE("GPL v2");
