/*
 * arch/arm/mach-tegra/board-trimslice.c
 *
 * Copyright (C) 2011 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on board-harmony.c
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/io.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>

#include <mach/iomap.h>
#include <mach/sdhci.h>

#include "board.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

#include "board-trimslice.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTA_BASE),
		.mapbase	= TEGRA_UARTA_BASE,
		.irq		= INT_UARTA,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0
	}
};

static struct platform_device debug_uart = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM,
	.dev	= {
		.platform_data	= debug_uart_platform_data,
	},
};
static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= TRIMSLICE_GPIO_SD4_CD,
	.wp_gpio	= TRIMSLICE_GPIO_SD4_WP,
	.power_gpio	= -1,
};

static struct platform_device *trimslice_devices[] __initdata = {
	&debug_uart,
	&tegra_sdhci_device1,
	&tegra_sdhci_device4,
};

static void __init tegra_trimslice_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
	mi->bank[1].start = SZ_512M;
	mi->bank[1].size = SZ_512M;
}

static __initdata struct tegra_clk_init_table trimslice_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	true },
	{ NULL,		NULL,		0,		0},
};

static int __init tegra_trimslice_pci_init(void)
{
	if (!machine_is_trimslice())
		return 0;

	return tegra_pcie_init(true, true);
}
subsys_initcall(tegra_trimslice_pci_init);

static void __init tegra_trimslice_init(void)
{
	tegra_clk_init_from_table(trimslice_clk_init_table);

	trimslice_pinmux_init();

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(trimslice_devices, ARRAY_SIZE(trimslice_devices));
}

MACHINE_START(TRIMSLICE, "trimslice")
	.boot_params	= 0x00000100,
	.fixup		= tegra_trimslice_fixup,
	.map_io         = tegra_map_common_io,
	.init_early	= tegra_init_early,
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_trimslice_init,
MACHINE_END
