// SPDX-License-Identifier: GPL-2.0+
/*
 * Central probing code for the FOTG210 dual role driver
 * We register one driver for the hardware and then we decide
 * whether to proceed with probing the host or the peripheral
 * driver.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>

#include "fotg210.h"

/*
 * Gemini-specific initialization function, only executed on the
 * Gemini SoC using the global misc control register.
 *
 * The gemini USB blocks are connected to either Mini-A (host mode) or
 * Mini-B (peripheral mode) plugs. There is no role switch support on the
 * Gemini SoC, just either-or.
 */
#define GEMINI_GLOBAL_MISC_CTRL		0x30
#define GEMINI_MISC_USB0_WAKEUP		BIT(14)
#define GEMINI_MISC_USB1_WAKEUP		BIT(15)
#define GEMINI_MISC_USB0_VBUS_ON	BIT(22)
#define GEMINI_MISC_USB1_VBUS_ON	BIT(23)
#define GEMINI_MISC_USB0_MINI_B		BIT(29)
#define GEMINI_MISC_USB1_MINI_B		BIT(30)

static int fotg210_gemini_init(struct device *dev, struct resource *res,
			       enum usb_dr_mode mode)
{
	struct device_node *np = dev->of_node;
	struct regmap *map;
	bool wakeup;
	u32 mask, val;
	int ret;

	map = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(map)) {
		dev_err(dev, "no syscon\n");
		return PTR_ERR(map);
	}
	wakeup = of_property_read_bool(np, "wakeup-source");

	/*
	 * Figure out if this is USB0 or USB1 by simply checking the
	 * physical base address.
	 */
	mask = 0;
	if (res->start == 0x69000000) {
		mask = GEMINI_MISC_USB1_VBUS_ON | GEMINI_MISC_USB1_MINI_B |
			GEMINI_MISC_USB1_WAKEUP;
		if (mode == USB_DR_MODE_HOST)
			val = GEMINI_MISC_USB1_VBUS_ON;
		else
			val = GEMINI_MISC_USB1_MINI_B;
		if (wakeup)
			val |= GEMINI_MISC_USB1_WAKEUP;
	} else {
		mask = GEMINI_MISC_USB0_VBUS_ON | GEMINI_MISC_USB0_MINI_B |
			GEMINI_MISC_USB0_WAKEUP;
		if (mode == USB_DR_MODE_HOST)
			val = GEMINI_MISC_USB0_VBUS_ON;
		else
			val = GEMINI_MISC_USB0_MINI_B;
		if (wakeup)
			val |= GEMINI_MISC_USB0_WAKEUP;
	}

	ret = regmap_update_bits(map, GEMINI_GLOBAL_MISC_CTRL, mask, val);
	if (ret) {
		dev_err(dev, "failed to initialize Gemini PHY\n");
		return ret;
	}

	dev_info(dev, "initialized Gemini PHY in %s mode\n",
		 (mode == USB_DR_MODE_HOST) ? "host" : "gadget");
	return 0;
}

static int fotg210_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	enum usb_dr_mode mode;
	int ret;

	mode = usb_get_dr_mode(dev);

	if (of_device_is_compatible(dev->of_node, "cortina,gemini-usb")) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		ret = fotg210_gemini_init(dev, res, mode);
		if (ret)
			return ret;
	}

	if (mode == USB_DR_MODE_PERIPHERAL)
		ret = fotg210_udc_probe(pdev);
	else
		ret = fotg210_hcd_probe(pdev);

	return ret;
}

static int fotg210_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	enum usb_dr_mode mode;

	mode = usb_get_dr_mode(dev);

	if (mode == USB_DR_MODE_PERIPHERAL)
		fotg210_udc_remove(pdev);
	else
		fotg210_hcd_remove(pdev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id fotg210_of_match[] = {
	{ .compatible = "faraday,fotg200" },
	{ .compatible = "faraday,fotg210" },
	/* TODO: can we also handle FUSB220? */
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
	if (IS_ENABLED(CONFIG_USB_FOTG210_HCD) && !usb_disabled())
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
