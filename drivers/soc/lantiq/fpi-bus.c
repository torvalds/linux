/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2011-2015 John Crispin <blogic@phrozen.org>
 *  Copyright (C) 2015 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *  Copyright (C) 2017 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <lantiq_soc.h>

#define XBAR_ALWAYS_LAST	0x430
#define XBAR_FPI_BURST_EN	BIT(1)
#define XBAR_AHB_BURST_EN	BIT(2)

#define RCU_VR9_BE_AHB1S	0x00000008

static int ltq_fpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res_xbar;
	struct regmap *rcu_regmap;
	void __iomem *xbar_membase;
	u32 rcu_ahb_endianness_reg_offset;
	int ret;

	res_xbar = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xbar_membase = devm_ioremap_resource(dev, res_xbar);
	if (IS_ERR(xbar_membase))
		return PTR_ERR(xbar_membase);

	/* RCU configuration is optional */
	rcu_regmap = syscon_regmap_lookup_by_phandle(np, "lantiq,rcu");
	if (IS_ERR(rcu_regmap))
		return PTR_ERR(rcu_regmap);

	ret = device_property_read_u32(dev, "lantiq,offset-endianness",
				       &rcu_ahb_endianness_reg_offset);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get RCU reg offset\n");
		return ret;
	}

	ret = regmap_update_bits(rcu_regmap, rcu_ahb_endianness_reg_offset,
				 RCU_VR9_BE_AHB1S, RCU_VR9_BE_AHB1S);
	if (ret) {
		dev_warn(&pdev->dev,
			 "Failed to configure RCU AHB endianness\n");
		return ret;
	}

	/* disable fpi burst */
	ltq_w32_mask(XBAR_FPI_BURST_EN, 0, xbar_membase + XBAR_ALWAYS_LAST);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static const struct of_device_id ltq_fpi_match[] = {
	{ .compatible = "lantiq,xrx200-fpi" },
	{},
};
MODULE_DEVICE_TABLE(of, ltq_fpi_match);

static struct platform_driver ltq_fpi_driver = {
	.probe = ltq_fpi_probe,
	.driver = {
		.name = "fpi-xway",
		.of_match_table = ltq_fpi_match,
	},
};

module_platform_driver(ltq_fpi_driver);

MODULE_DESCRIPTION("Lantiq FPI bus driver");
MODULE_LICENSE("GPL");
