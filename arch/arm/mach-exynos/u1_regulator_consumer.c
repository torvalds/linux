/* u1-regulator-consumer.c
 *
 * Copyright (C) 2011 Samsung Electronics
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>

static int u1_enable_regulator_for_usb_mipi(bool enable)
{
	struct regulator *mipi11_regulator;
	struct regulator *mipi18_regulator;
#ifndef CONFIG_MACH_U1_KOR_LGT
	struct regulator *hsic12_regulator;
#endif
	struct regulator *usb33_regulator;
	int ret = 0;

	mipi11_regulator = regulator_get(NULL, "vmipi_1.1v");
	if (IS_ERR(mipi11_regulator)) {
		pr_err("%s: failed to get %s\n", __func__, "vmipi_1.1v");
		ret = -ENODEV;
		goto out4;
	}

	mipi18_regulator = regulator_get(NULL, "vmipi_1.8v");
	if (IS_ERR(mipi18_regulator)) {
		pr_err("%s: failed to get %s\n", __func__, "vmipi_1.8v");
		ret = -ENODEV;
		goto out3;
	}

#ifndef CONFIG_MACH_U1_KOR_LGT
	hsic12_regulator = regulator_get(NULL, "vhsic");
	if (IS_ERR(hsic12_regulator)) {
		pr_err("%s: failed to get %s\n", __func__, "vhsic 1.2v");
		ret = -ENODEV;
		goto out2;
	}
#endif

	usb33_regulator = regulator_get(NULL, "vusb_3.3v");
	if (IS_ERR(usb33_regulator)) {
		pr_err("%s: failed to get %s\n", __func__, "vusb_3.3v");
		ret = -ENODEV;
		goto out1;
	}

	if (enable) {
		/* Power On Sequence
		 * MIPI 1.1V -> HSIC 1.2V -> MIPI 1.8V -> USB 3.3V
		 */
		pr_info("%s: enable LDOs\n", __func__);
		if (!regulator_is_enabled(mipi11_regulator))
			regulator_enable(mipi11_regulator);
#ifndef CONFIG_MACH_U1_KOR_LGT
		if (!regulator_is_enabled(hsic12_regulator))
			regulator_enable(hsic12_regulator);
#endif
		if (!regulator_is_enabled(mipi18_regulator))
			regulator_enable(mipi18_regulator);
		if (!regulator_is_enabled(usb33_regulator))
			regulator_enable(usb33_regulator);
	} else {
		/* Power Off Sequence
		 * USB 3.3V -> MIPI 18V -> HSIC 1.2V -> MIPI 1.1V
		 */
		pr_info("%s: disable LDOs\n", __func__);
		regulator_force_disable(usb33_regulator);
		regulator_force_disable(mipi18_regulator);
#ifndef CONFIG_MACH_U1_KOR_LGT
		regulator_force_disable(hsic12_regulator);
#endif
		regulator_force_disable(mipi11_regulator);
	}

	regulator_put(usb33_regulator);
out1:
#ifndef CONFIG_MACH_U1_KOR_LGT
	regulator_put(hsic12_regulator);
#endif
out2:
	regulator_put(mipi18_regulator);
out3:
	regulator_put(mipi11_regulator);
out4:
	return ret;
}


static int regulator_consumer_probe(struct platform_device *pdev)
{
	pr_info("%s: loading u1-regulator-consumer\n", __func__);
	return 0;
}

#ifdef CONFIG_PM
static int regulator_consumer_suspend(struct device *dev)
{
	u1_enable_regulator_for_usb_mipi(false);
	return 0;
}

static int regulator_consumer_resume(struct device *dev)
{
	u1_enable_regulator_for_usb_mipi(true);
	return 0;
}
#else
#define regulator_consumer_suspend	NULL
#define regulator_consumer_resume	NULL
#endif /* CONFIG_PM */

static const struct dev_pm_ops regulator_consumer_pm_ops = {
	.suspend = regulator_consumer_suspend,
	.resume = regulator_consumer_resume,
};

static struct platform_driver regulator_consumer_driver = {
	.probe = regulator_consumer_probe,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "u1-regulator-consumer",
		   .pm = &regulator_consumer_pm_ops,
	},
};

static int __init regulator_consumer_init(void)
{
	return platform_driver_register(&regulator_consumer_driver);
}
module_init(regulator_consumer_init);

MODULE_DESCRIPTION("U1 regulator consumer driver");
MODULE_AUTHOR("ms925.kim@samsung.com");
MODULE_LICENSE("GPL");
