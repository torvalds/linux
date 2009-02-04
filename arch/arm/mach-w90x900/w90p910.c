/*
 * linux/arch/arm/mach-w90x900/w90p910.c
 *
 * Based on linux/arch/arm/plat-s3c24xx/s3c244x.c by Ben Dooks
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * W90P910 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/serial_8250.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/regs-serial.h>

#include "cpu.h"

/* Initial IO mappings */

static struct map_desc w90p910_iodesc[] __initdata = {
	IODESC_ENT(IRQ),
	IODESC_ENT(GCR),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(EBI),
	/*IODESC_ENT(LCD),*/
};

/* Initial serial platform data */

struct plat_serial8250_port w90p910_uart_data[] = {
	W90X900_8250PORT(UART0),
};

struct platform_device w90p910_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= w90p910_uart_data,
	},
};

/*Init W90P910 evb io*/

void __init w90p910_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long idcode = 0x0;

	iotable_init(w90p910_iodesc, ARRAY_SIZE(w90p910_iodesc));

	idcode = __raw_readl(W90X900PDID);
	if (idcode != W90P910_CPUID)
		printk(KERN_ERR "CPU type 0x%08lx is not W90P910\n", idcode);
}

/*Init W90P910 clock*/

void __init w90p910_init_clocks(int xtal)
{
}

static int __init w90p910_init_cpu(void)
{
	return 0;
}

static int __init w90x900_arch_init(void)
{
	return w90p910_init_cpu();
}
arch_initcall(w90x900_arch_init);
