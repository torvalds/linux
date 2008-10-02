/*
 * UDC functions for the Toshiba e-series PDAs
 *
 * Copyright (c) Ian Molton 2003
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

#include <mach/udc.h>
#include <mach/eseries-gpio.h>
#include <mach/hardware.h>
#include <mach/pxa-regs.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/domain.h>

/* local PXA generic code */
#include "generic.h"

static struct pxa2xx_udc_mach_info e7xx_udc_mach_info = {
	.gpio_vbus   = GPIO_E7XX_USB_DISC,
	.gpio_pullup = GPIO_E7XX_USB_PULLUP,
	.gpio_pullup_inverted = 1
};

static struct pxa2xx_udc_mach_info e800_udc_mach_info = {
	.gpio_vbus   = GPIO_E800_USB_DISC,
	.gpio_pullup = GPIO_E800_USB_PULLUP,
	.gpio_pullup_inverted = 1
};

static int __init eseries_udc_init(void)
{
	if (machine_is_e330() || machine_is_e350() ||
	    machine_is_e740() || machine_is_e750() ||
	    machine_is_e400())
		pxa_set_udc_info(&e7xx_udc_mach_info);
	else if (machine_is_e800())
		pxa_set_udc_info(&e800_udc_mach_info);

	return 0;
}

module_init(eseries_udc_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("eseries UDC support");
MODULE_LICENSE("GPLv2");
