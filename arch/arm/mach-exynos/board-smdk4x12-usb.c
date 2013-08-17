/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <plat/gpio-cfg.h>
#include <plat/ehci.h>
#include <plat/devs.h>
#include <plat/udc-hs.h>
#include <mach/ohci.h>

static struct exynos4_ohci_platdata smdk4x12_ohci_pdata __initdata;
static struct s5p_ehci_platdata smdk4x12_ehci_pdata __initdata;
static struct s3c_hsotg_plat smdk4x12_hsotg_pdata __initdata;

static void __init smdk4x12_ohci_init(void)
{
	exynos4_ohci_set_platdata(&smdk4x12_ohci_pdata);
}

static void __init smdk4x12_ehci_init(void)
{
	s5p_ehci_set_platdata(&smdk4x12_ehci_pdata);
}

static void __init smdk4x12_hsotg_init(void)
{
	s3c_hsotg_set_platdata(&smdk4x12_hsotg_pdata);
}

static struct platform_device *smdk4x12_usb_devices[] __initdata = {
	&s5p_device_ehci,
	&exynos4_device_ohci,
	&s3c_device_usb_hsotg,
};

void __init exynos4_smdk4x12_usb_init(void)
{
#ifndef CONFIG_EXYNOS_USB_SWITCH
#if defined(CONFIG_USB_EHCI_S5P) || defined(CONFIG_USB_OHCI_EXYNOS)
	if (gpio_request_one(EXYNOS4_GPL2(0), GPIOF_OUT_INIT_HIGH,
		"UHOST_VBUSCTRL") < 0)
		printk(KERN_ERR "failed to request UHOST_VBUSCTRL\n");
	else
		gpio_free(EXYNOS4_GPL2(0));
#endif
#endif
	smdk4x12_ohci_init();
	smdk4x12_ehci_init();
	smdk4x12_hsotg_init();

	platform_add_devices(smdk4x12_usb_devices,
			ARRAY_SIZE(smdk4x12_usb_devices));
}
