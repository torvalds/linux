/*
 * arch/arm/mach-tegra/board-ventana.c
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "clock.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.mapbase	= TEGRA_UARTD_BASE,
		.irq		= INT_UARTD,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static __initdata struct tegra_clk_init_table ventana_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true},
	{ NULL,		NULL,		0,		0},
};

static struct platform_device *ventana_devices[] __initdata = {
	&debug_uart,
};

extern int __init ventana_sdhci_init(void);
extern int __init ventana_pinmux_init(void);

static void __init tegra_ventana_init(void)
{
	tegra_common_init();

	tegra_clk_init_from_table(ventana_clk_init_table);
	ventana_pinmux_init();

	platform_add_devices(ventana_devices, ARRAY_SIZE(ventana_devices));
	ventana_sdhci_init();
}

MACHINE_START(VENTANA, "ventana")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.timer          = &tegra_timer,
MACHINE_END
