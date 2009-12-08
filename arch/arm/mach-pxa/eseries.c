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
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/pxa25x.h>
#include <mach/eseries-gpio.h>
#include <mach/udc.h>
#include <mach/irda.h>

#include "generic.h"
#include "clock.h"

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

struct pxaficp_platform_data e7xx_ficp_platform_data = {
	.gpio_pwdown		= GPIO_E7XX_IR_OFF,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

int eseries_tmio_enable(struct platform_device *dev)
{
	/* Reset - bring SUSPEND high before PCLR */
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 0);
	msleep(1);
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 1);
	msleep(1);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 1);
	msleep(1);
	return 0;
}

int eseries_tmio_disable(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 0);
	return 0;
}

int eseries_tmio_suspend(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	return 0;
}

int eseries_tmio_resume(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 1);
	msleep(1);
	return 0;
}

void eseries_get_tmio_gpios(void)
{
	gpio_request(GPIO_ESERIES_TMIO_SUSPEND, NULL);
	gpio_request(GPIO_ESERIES_TMIO_PCLR, NULL);
	gpio_direction_output(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_direction_output(GPIO_ESERIES_TMIO_PCLR, 0);
}

/* TMIO controller uses the same resources on all e-series machines. */
struct resource eseries_tmio_resources[] = {
	[0] = {
		.start  = PXA_CS4_PHYS,
		.end    = PXA_CS4_PHYS + 0x1fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_GPIO(GPIO_ESERIES_TMIO_IRQ),
		.end    = IRQ_GPIO(GPIO_ESERIES_TMIO_IRQ),
		.flags  = IORESOURCE_IRQ,
	},
};

/* Some e-series hardware cannot control the 32K clock */
static void clk_32k_dummy(struct clk *clk)
{
}

static const struct clkops clk_32k_dummy_ops = {
	.enable         = clk_32k_dummy,
	.disable        = clk_32k_dummy,
};

static struct clk tmio_dummy_clk = {
	.ops	= &clk_32k_dummy_ops,
	.rate	= 32768,
};

static struct clk_lookup eseries_clkregs[] = {
	INIT_CLKREG(&tmio_dummy_clk, NULL, "CLK_CK32K"),
};

void eseries_register_clks(void)
{
	clks_register(eseries_clkregs, ARRAY_SIZE(eseries_clkregs));
}

