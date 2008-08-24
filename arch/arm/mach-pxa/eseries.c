/*
 * Hardware definitions for the Toshiba eseries PDAs
 *
 * Copyright (c) 2003 Ian Molton <spyro@f2s.com>
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/mfp-pxa25x.h>
#include <mach/hardware.h>
#include <mach/eseries-gpio.h>
#include <mach/udc.h>
#include <mach/irda.h>

#include "generic.h"

/* Only e800 has 128MB RAM */
void __init eseries_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	if (machine_is_e800())
		mi->bank[0].size = (128*1024*1024);
	else
		mi->bank[0].size = (64*1024*1024);
}

struct pxa2xx_udc_mach_info e7xx_udc_mach_info = {
	.gpio_vbus   = GPIO_E7XX_USB_DISC,
	.gpio_pullup = GPIO_E7XX_USB_PULLUP,
	.gpio_pullup_inverted = 1
};

static void e7xx_irda_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF) {
		gpio_set_value(GPIO_E7XX_IR_OFF, 1);
		pxa2xx_transceiver_mode(dev, mode);
	} else {
		pxa2xx_transceiver_mode(dev, mode);
		gpio_set_value(GPIO_E7XX_IR_OFF, 0);
	}
}

int e7xx_irda_init(void)
{
	int ret;

	ret = gpio_request(GPIO_E7XX_IR_OFF, "IrDA power");
	if (ret)
		goto out;

	ret = gpio_direction_output(GPIO_E7XX_IR_OFF, 0);
	if (ret)
		goto out;

	e7xx_irda_transceiver_mode(NULL, IR_SIRMODE | IR_OFF);
out:
	return ret;
}

static void e7xx_irda_shutdown(struct device *dev)
{
	e7xx_irda_transceiver_mode(dev, IR_SIRMODE | IR_OFF);
	gpio_free(GPIO_E7XX_IR_OFF);
}

struct pxaficp_platform_data e7xx_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = e7xx_irda_transceiver_mode,
	.shutdown = e7xx_irda_shutdown,
};

