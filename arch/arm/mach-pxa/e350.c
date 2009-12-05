/*
 * Hardware definitions for the Toshiba e350 PDAs
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
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/mfd/t7l66xb.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/irqs.h>
#include <mach/pxa25x.h>
#include <mach/eseries-gpio.h>
#include <mach/udc.h>

#include "generic.h"
#include "eseries.h"
#include "clock.h"

/* -------------------- e350 t7l66xb parameters -------------------- */

static struct t7l66xb_platform_data e350_t7l66xb_info = {
	.irq_base               = IRQ_BOARD_START,
	.enable                 = &eseries_tmio_enable,
	.suspend                = &eseries_tmio_suspend,
	.resume                 = &eseries_tmio_resume,
};

static struct platform_device e350_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e350_t7l66xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

/* ---------------------------------------------------------- */

static struct platform_device *devices[] __initdata = {
	&e350_t7l66xb_device,
};

static void __init e350_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	eseries_register_clks();
	eseries_get_tmio_gpios();
	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_udc_info(&e7xx_udc_mach_info);
}

MACHINE_START(E350, "Toshiba e350")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e350_init,
	.timer		= &pxa_timer,
MACHINE_END

