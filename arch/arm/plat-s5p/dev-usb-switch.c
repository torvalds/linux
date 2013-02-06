/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <plat/devs.h>
#include <plat/usb-switch.h>

/* USB Switch */
static struct resource s5p_usbswitch_res[4];

struct platform_device s5p_device_usbswitch = {
	.name		= "exynos-usb-switch",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_usbswitch_res),
	.resource	= s5p_usbswitch_res,
};

void __init s5p_usbswitch_set_platdata(struct s5p_usbswitch_platdata *pd)
{
	struct s5p_usbswitch_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_usbswitch_platdata),
			&s5p_device_usbswitch);

	s5p_usbswitch_res[0].start = gpio_to_irq(npd->gpio_host_detect);
	s5p_usbswitch_res[0].end = gpio_to_irq(npd->gpio_host_detect);
	s5p_usbswitch_res[0].flags = IORESOURCE_IRQ;

	s5p_usbswitch_res[1].start = gpio_to_irq(npd->gpio_device_detect);
	s5p_usbswitch_res[1].end = gpio_to_irq(npd->gpio_device_detect);
	s5p_usbswitch_res[1].flags = IORESOURCE_IRQ;

	s5p_usbswitch_res[2].start = gpio_to_irq(npd->gpio_drd_host_detect);
	s5p_usbswitch_res[2].end = gpio_to_irq(npd->gpio_drd_host_detect);
	s5p_usbswitch_res[2].flags = IORESOURCE_IRQ;

	s5p_usbswitch_res[3].start = gpio_to_irq(npd->gpio_drd_device_detect);
	s5p_usbswitch_res[3].end = gpio_to_irq(npd->gpio_drd_device_detect);
	s5p_usbswitch_res[3].flags = IORESOURCE_IRQ;
#ifdef CONFIG_USB_EHCI_S5P
	npd->ehci_dev = &s5p_device_ehci.dev;
#endif
#ifdef CONFIG_USB_OHCI_S5P
	npd->ohci_dev = &s5p_device_ohci.dev;
#endif
#ifdef CONFIG_USB_XHCI_EXYNOS
	npd->xhci_dev = &exynos_device_xhci.dev;
#endif
#ifdef CONFIG_USB_S3C_OTGD
	npd->s3c_udc_dev = &s3c_device_usbgadget.dev;
#endif
#ifdef CONFIG_USB_EXYNOS_SS_UDC
	npd->exynos_udc_dev = &exynos_device_ss_udc.dev;
#endif
}
