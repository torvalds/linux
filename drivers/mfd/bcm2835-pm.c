// SPDX-License-Identifier: GPL-2.0+
/*
 * PM MFD driver for Broadcom BCM2835
 *
 * This driver binds to the PM block and creates the MFD device for
 * the WDT and power drivers.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/bcm2835-pm.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>

static const struct mfd_cell bcm2835_pm_devs[] = {
	{ .name = "bcm2835-wdt" },
};

static const struct mfd_cell bcm2835_power_devs[] = {
	{ .name = "bcm2835-power" },
};

static int bcm2835_pm_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct bcm2835_pm *pm;
	int ret;

	pm = devm_kzalloc(dev, sizeof(*pm), GFP_KERNEL);
	if (!pm)
		return -ENOMEM;
	platform_set_drvdata(pdev, pm);

	pm->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pm->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pm->base))
		return PTR_ERR(pm->base);

	ret = devm_mfd_add_devices(dev, -1,
				   bcm2835_pm_devs, ARRAY_SIZE(bcm2835_pm_devs),
				   NULL, 0, NULL);
	if (ret)
		return ret;

	/* We'll use the presence of the AXI ASB regs in the
	 * bcm2835-pm binding as the key for whether we can reference
	 * the full PM register range and support power domains.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		pm->asb = devm_ioremap_resource(dev, res);
		if (IS_ERR(pm->asb))
			return PTR_ERR(pm->asb);

		ret = devm_mfd_add_devices(dev, -1,
					   bcm2835_power_devs,
					   ARRAY_SIZE(bcm2835_power_devs),
					   NULL, 0, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id bcm2835_pm_of_match[] = {
	{ .compatible = "brcm,bcm2835-pm-wdt", },
	{ .compatible = "brcm,bcm2835-pm", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_pm_of_match);

static struct platform_driver bcm2835_pm_driver = {
	.probe		= bcm2835_pm_probe,
	.driver = {
		.name =	"bcm2835-pm",
		.of_match_table = bcm2835_pm_of_match,
	},
};
module_platform_driver(bcm2835_pm_driver);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Driver for Broadcom BCM2835 PM MFD");
MODULE_LICENSE("GPL");
