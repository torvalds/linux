/*
 * Device Tree support for Allwinner A1X SoCs
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sunxi_timer.h>

#include <linux/irqchip/sunxi.h>

#include <asm/hardware/vic.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "sunxi.h"

#define WATCHDOG_CTRL_REG	0x00
#define WATCHDOG_MODE_REG	0x04

static void __iomem *wdt_base;

static void sunxi_setup_restart(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL,
						"allwinner,sunxi-wdt");
	if (WARN(!np, "unable to setup watchdog restart"))
		return;

	wdt_base = of_iomap(np, 0);
	WARN(!wdt_base, "failed to map watchdog base address");
}

static void sunxi_restart(char mode, const char *cmd)
{
	if (!wdt_base)
		return;

	/* Enable timer and set reset bit in the watchdog */
	writel(3, wdt_base + WATCHDOG_MODE_REG);
	writel(0xa57 << 1 | 1, wdt_base + WATCHDOG_CTRL_REG);
	while(1) {
		mdelay(5);
		writel(3, wdt_base + WATCHDOG_MODE_REG);
	}
}

static struct map_desc sunxi_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) SUNXI_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(SUNXI_REGS_PHYS_BASE),
		.length		= SUNXI_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init sunxi_map_io(void)
{
	iotable_init(sunxi_io_desc, ARRAY_SIZE(sunxi_io_desc));
}

static void __init sunxi_dt_init(void)
{
	sunxi_setup_restart();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const sunxi_board_dt_compat[] = {
	"allwinner,sun4i",
	"allwinner,sun5i",
	NULL,
};

DT_MACHINE_START(SUNXI_DT, "Allwinner A1X (Device Tree)")
	.init_machine	= sunxi_dt_init,
	.map_io		= sunxi_map_io,
	.init_irq	= sunxi_init_irq,
	.handle_irq	= sunxi_handle_irq,
	.restart	= sunxi_restart,
	.timer		= &sunxi_timer,
	.dt_compat	= sunxi_board_dt_compat,
MACHINE_END
