/*
 *  linux/arch/arm/mach-mmp/teton_bga.c
 *
 *  Support for the Marvell PXA168 Teton BGA Development Platform.
 *
 *  Author: Mark F. Brown <mark.brown314@gmail.com>
 *
 *  This code is based on aspenite.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/addr-map.h>
#include <mach/mfp-pxa168.h>
#include <mach/pxa168.h>
#include <mach/teton_bga.h>

#include "common.h"

static unsigned long teton_bga_pin_config[] __initdata = {
	/* UART1 */
	GPIO107_UART1_TXD,
	GPIO108_UART1_RXD,
};

static void __init teton_bga_init(void)
{
	mfp_config(ARRAY_AND_SIZE(teton_bga_pin_config));

	/* on-chip devices */
	pxa168_add_uart(1);
}

MACHINE_START(TETON_BGA, "PXA168-based Teton BGA Development Platform")
	.phys_io        = APB_PHYS_BASE,
	.io_pg_offst    = (APB_VIRT_BASE >> 18) & 0xfffc,
	.map_io		= mmp_map_io,
	.nr_irqs	= IRQ_BOARD_START,
	.init_irq       = pxa168_init_irq,
	.timer          = &pxa168_timer,
	.init_machine   = teton_bga_init,
MACHINE_END
