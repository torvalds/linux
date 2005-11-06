/*
 *  linux/arch/arm/mach-epxa10db/arch.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_8250.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.iobase		= 0x3f8,
		.irq		= IRQ_UARTINT0,
#error FIXME
		.uartclk	= 0,
		.regshift	= 0,
		.iotype		= UPIO_PORT,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{
		.iobase		= 0x2f8,
		.irq		= IRQ_UARTINT1,
#error FIXME
		.uartclk	= 0,
		.regshift	= 0,
		.iotype		= UPIO_PORT,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

extern void epxa10db_map_io(void);
extern void epxa10db_init_irq(void);
extern struct sys_timer epxa10db_timer;

MACHINE_START(CAMELOT, "Altera Epxa10db")
	/* Maintainer: Altera Corporation */
	.phys_ram	= 0x00000000,
	.phys_io	= 0x7fffc000,
	.io_pg_offst	= ((0xffffc000) >> 18) & 0xfffc,
	.map_io		= epxa10db_map_io,
	.init_irq	= epxa10db_init_irq,
	.timer		= &epxa10db_timer,
MACHINE_END

