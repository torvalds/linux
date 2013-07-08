/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#define PHYS_TO_IO(x)			(((x) & 0x00ffffff) | 0xfe000000)
#define PICOXCELL_PERIPH_BASE		0x80000000
#define PICOXCELL_PERIPH_LENGTH		SZ_4M

#define WDT_CTRL_REG_EN_MASK		(1 << 0)
#define WDT_CTRL_REG_OFFS		(0x00)
#define WDT_TIMEOUT_REG_OFFS		(0x04)
static void __iomem *wdt_regs;

/*
 * The machine restart method can be called from an atomic context so we won't
 * be able to ioremap the regs then.
 */
static void picoxcell_setup_restart(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL,
							 "snps,dw-apb-wdg");
	if (WARN(!np, "unable to setup watchdog restart"))
		return;

	wdt_regs = of_iomap(np, 0);
	WARN(!wdt_regs, "failed to remap watchdog regs");
}

static struct map_desc io_map __initdata = {
	.virtual	= PHYS_TO_IO(PICOXCELL_PERIPH_BASE),
	.pfn		= __phys_to_pfn(PICOXCELL_PERIPH_BASE),
	.length		= PICOXCELL_PERIPH_LENGTH,
	.type		= MT_DEVICE,
};

static void __init picoxcell_map_io(void)
{
	iotable_init(&io_map, 1);
}

static void __init picoxcell_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	picoxcell_setup_restart();
}

static const char *picoxcell_dt_match[] = {
	"picochip,pc3x2",
	"picochip,pc3x3",
	NULL
};

static void picoxcell_wdt_restart(char mode, const char *cmd)
{
	/*
	 * Configure the watchdog to reset with the shortest possible timeout
	 * and give it chance to do the reset.
	 */
	if (wdt_regs) {
		writel_relaxed(WDT_CTRL_REG_EN_MASK, wdt_regs + WDT_CTRL_REG_OFFS);
		writel_relaxed(0, wdt_regs + WDT_TIMEOUT_REG_OFFS);
		/* No sleeping, possibly atomic. */
		mdelay(500);
	}
}

DT_MACHINE_START(PICOXCELL, "Picochip picoXcell")
	.map_io		= picoxcell_map_io,
	.init_machine	= picoxcell_init_machine,
	.dt_compat	= picoxcell_dt_match,
	.restart	= picoxcell_wdt_restart,
MACHINE_END
