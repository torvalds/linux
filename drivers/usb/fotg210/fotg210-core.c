// SPDX-License-Identifier: GPL-2.0+
/*
 * Central probing code for the FOTG210 dual role driver
 * We register one driver for the hardware and then we decide
 * whether to proceed with probing the host or the peripheral
 * driver.
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb.h>

#include "fotg210.h"

static int fotg210_probe(struct platform_device *pdev)
{
	int ret;

	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD)) {
		ret = fotg210_hcd_probe(pdev);
		if (ret)
			return ret;
	}
	if (IS_ENABLED(CONFIG_USB_FOTG210_UDC))
		ret = fotg210_udc_probe(pdev);

	return ret;
}

static int fotg210_remove(struct platform_device *pdev)
{
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD))
		fotg210_hcd_remove(pdev);
	if (IS_ENABLED(CONFIG_USB_FOTG210_UDC))
		fotg210_udc_remove(pdev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id fotg210_of_match[] = {
	{ .compatible = "faraday,fotg210" },
	{},
};
MODULE_DEVICE_TABLE(of, fotg210_of_match);
#endif

static struct platform_driver fotg210_driver = {
	.driver = {
		.name   = "fotg210",
		.of_match_table = of_match_ptr(fotg210_of_match),
	},
	.probe  = fotg210_probe,
	.remove = fotg210_remove,
};

static int __init fotg210_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD))
		fotg210_hcd_init();
	return platform_driver_register(&fotg210_driver);
}
module_init(fotg210_init);

static void __exit fotg210_cleanup(void)
{
	platform_driver_unregister(&fotg210_driver);
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD))
		fotg210_hcd_cleanup();
}
module_exit(fotg210_cleanup);

MODULE_AUTHOR("Yuan-Hsin Chen, Feng-Hsin Chiang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FOTG210 Dual Role Controller Driver");
