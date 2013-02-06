/* linux/arch/arm/mach-exynos/board-smdk5250-usb.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/platform_data/exynos_usb3_drd.h>

#include <plat/gpio-cfg.h>
#include <plat/cpu.h>
#include <plat/devs.h>

#include <plat/ehci.h>
#include <plat/usbgadget.h>
#include <plat/usb-switch.h>

#include "board-smdk5250.h"

static struct s5p_ehci_platdata smdk5250_ehci_pdata;

static void __init smdk5250_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdk5250_ehci_pdata;

#ifndef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_rev() >= EXYNOS5250_REV_1_0) {
		if (gpio_request_one(EXYNOS5_GPX2(6), GPIOF_OUT_INIT_HIGH,
			"HOST_VBUS_CONTROL"))
			printk(KERN_ERR "failed to request gpio_host_vbus\n");
		else {
			s3c_gpio_setpull(EXYNOS5_GPX2(6), S3C_GPIO_PULL_NONE);
			gpio_free(EXYNOS5_GPX2(6));
		}
	}
#endif
	s5p_ehci_set_platdata(pdata);
}

static struct s5p_ohci_platdata smdk5250_ohci_pdata;

static void __init smdk5250_ohci_init(void)
{
	struct s5p_ohci_platdata *pdata = &smdk5250_ohci_pdata;

	s5p_ohci_set_platdata(pdata);
}

/* USB GADGET */
static struct s5p_usbgadget_platdata smdk5250_usbgadget_pdata;

static void __init smdk5250_usbgadget_init(void)
{
	struct s5p_usbgadget_platdata *pdata = &smdk5250_usbgadget_pdata;

	s5p_usbgadget_set_platdata(pdata);
}

static struct exynos_usb3_drd_pdata smdk5250_ss_udc_pdata;

static void __init smdk5250_ss_udc_init(void)
{
	struct exynos_usb3_drd_pdata *pdata = &smdk5250_ss_udc_pdata;

	exynos_ss_udc_set_platdata(pdata);
}

static struct exynos_usb3_drd_pdata smdk5250_xhci_pdata;

static void __init smdk5250_xhci_init(void)
{
	struct exynos_usb3_drd_pdata *pdata = &smdk5250_xhci_pdata;

	exynos_xhci_set_platdata(pdata);
}

static struct s5p_usbswitch_platdata smdk5250_usbswitch_pdata;

static void __init smdk5250_usbswitch_init(void)
{
	struct s5p_usbswitch_platdata *pdata = &smdk5250_usbswitch_pdata;
	int err;

	/* USB 2.0 detect GPIO */
	if (samsung_rev() < EXYNOS5250_REV_1_0) {
		pdata->gpio_device_detect = 0;
		pdata->gpio_host_vbus = 0;
	} else {
#if defined(CONFIG_USB_EHCI_S5P) || defined(CONFIG_USB_OHCI_S5P)
		pdata->gpio_host_detect = EXYNOS5_GPX1(6);
		err = gpio_request_one(pdata->gpio_host_detect, GPIOF_IN,
			"HOST_DETECT");
		if (err) {
			printk(KERN_ERR "failed to request host gpio\n");
			return;
		}

		s3c_gpio_cfgpin(pdata->gpio_host_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(pdata->gpio_host_detect, S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_host_detect);

		pdata->gpio_host_vbus = EXYNOS5_GPX2(6);
		err = gpio_request_one(pdata->gpio_host_vbus,
			GPIOF_OUT_INIT_LOW,
			"HOST_VBUS_CONTROL");
		if (err) {
			printk(KERN_ERR "failed to request host_vbus gpio\n");
			return;
		}

		s3c_gpio_setpull(pdata->gpio_host_vbus, S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_host_vbus);
#endif

#ifdef CONFIG_USB_S3C_OTGD
		pdata->gpio_device_detect = EXYNOS5_GPX3(4);
		err = gpio_request_one(pdata->gpio_device_detect, GPIOF_IN,
			"DEVICE_DETECT");
		if (err) {
			printk(KERN_ERR "failed to request device gpio\n");
			return;
		}

		s3c_gpio_cfgpin(pdata->gpio_device_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(pdata->gpio_device_detect, S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_device_detect);
#endif
	}

	/* USB 3.0 DRD detect GPIO */
	if (samsung_rev() < EXYNOS5250_REV_1_0) {
		pdata->gpio_drd_host_detect = 0;
		pdata->gpio_drd_device_detect = 0;
	} else {
#ifdef CONFIG_USB_XHCI_EXYNOS
		pdata->gpio_drd_host_detect = EXYNOS5_GPX1(7);
		err = gpio_request_one(pdata->gpio_drd_host_detect, GPIOF_IN,
			"DRD_HOST_DETECT");
		if (err) {
			printk(KERN_ERR "failed to request drd_host gpio\n");
			return;
		}

		s3c_gpio_cfgpin(pdata->gpio_drd_host_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(pdata->gpio_drd_host_detect,
			S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_drd_host_detect);
#endif

#ifdef CONFIG_USB_EXYNOS_SS_UDC
		pdata->gpio_drd_device_detect = EXYNOS5_GPX0(6);
		err = gpio_request_one(pdata->gpio_drd_device_detect, GPIOF_IN,
			"DRD_DEVICE_DETECT");
		if (err) {
			printk(KERN_ERR "failed to request drd_device\n");
			return;
		}

		s3c_gpio_cfgpin(pdata->gpio_drd_device_detect,
			S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(pdata->gpio_drd_device_detect,
			S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_drd_device_detect);
#endif
	}

	s5p_usbswitch_set_platdata(pdata);
}

static struct platform_device *smdk5250_usb_devices[] __initdata = {
	&s5p_device_ehci,
	&s5p_device_ohci,
	&s3c_device_usbgadget,
#ifdef CONFIG_USB_ANDROID_RNDIS
	&s3c_device_rndis,
#endif
#ifdef CONFIG_USB_ANDROID
	&s3c_device_android_usb,
	&s3c_device_usb_mass_storage,
#endif
	&exynos_device_ss_udc,
	&exynos_device_xhci,
};

void __init exynos5_smdk5250_usb_init(void)
{
	smdk5250_ehci_init();
	smdk5250_ohci_init();
	smdk5250_usbgadget_init();
	smdk5250_ss_udc_init();
	smdk5250_xhci_init();
	smdk5250_usbswitch_init();

	platform_add_devices(smdk5250_usb_devices,
			ARRAY_SIZE(smdk5250_usb_devices));
}
