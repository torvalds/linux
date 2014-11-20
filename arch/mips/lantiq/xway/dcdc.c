/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 *  Copyright (C) 2010 Sameer Ahmad, Lantiq GmbH
 */

#include <linux/ioport.h>
#include <linux/of_platform.h>

#include <lantiq_soc.h>

/* Bias and regulator Setup Register */
#define DCDC_BIAS_VREG0	0xa
/* Bias and regulator Setup Register */
#define DCDC_BIAS_VREG1	0xb

#define dcdc_w8(x, y)	ltq_w8((x), dcdc_membase + (y))
#define dcdc_r8(x)	ltq_r8(dcdc_membase + (x))

static void __iomem *dcdc_membase;

static int dcdc_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dcdc_membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dcdc_membase))
		return PTR_ERR(dcdc_membase);

	dev_info(&pdev->dev, "Core Voltage : %d mV\n",
		dcdc_r8(DCDC_BIAS_VREG1) * 8);

	return 0;
}

static const struct of_device_id dcdc_match[] = {
	{ .compatible = "lantiq,dcdc-xrx200" },
	{},
};

static struct platform_driver dcdc_driver = {
	.probe = dcdc_probe,
	.driver = {
		.name = "dcdc-xrx200",
		.owner = THIS_MODULE,
		.of_match_table = dcdc_match,
	},
};

int __init dcdc_init(void)
{
	int ret = platform_driver_register(&dcdc_driver);

	if (ret)
		pr_info("dcdc: Error registering platform driver\n");
	return ret;
}

arch_initcall(dcdc_init);
