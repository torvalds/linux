// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 *  Copyright (C) 2010 Sameer Ahmad, Lantiq GmbH
 */

#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

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
	dcdc_membase = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
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
		.of_match_table = dcdc_match,
	},
};

static int __init dcdc_init(void)
{
	int ret = platform_driver_register(&dcdc_driver);

	if (ret)
		pr_info("dcdc: Error registering platform driver\n");
	return ret;
}

arch_initcall(dcdc_init);
