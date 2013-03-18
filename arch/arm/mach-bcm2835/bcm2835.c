/*
 * Copyright (C) 2010 Broadcom
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
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irqchip/bcm2835.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk/bcm2835.h>
#include <linux/clocksource.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/bcm2835_soc.h>

#define PM_RSTC				0x1c
#define PM_RSTS				0x20
#define PM_WDOG				0x24

#define PM_PASSWORD			0x5a000000
#define PM_RSTC_WRCFG_MASK		0x00000030
#define PM_RSTC_WRCFG_FULL_RESET	0x00000020
#define PM_RSTS_HADWRH_SET		0x00000040

static void __iomem *wdt_regs;

/*
 * The machine restart method can be called from an atomic context so we won't
 * be able to ioremap the regs then.
 */
static void bcm2835_setup_restart(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL,
						"brcm,bcm2835-pm-wdt");
	if (WARN(!np, "unable to setup watchdog restart"))
		return;

	wdt_regs = of_iomap(np, 0);
	WARN(!wdt_regs, "failed to remap watchdog regs");
}

static void bcm2835_restart(char mode, const char *cmd)
{
	u32 val;

	if (!wdt_regs)
		return;

	/* use a timeout of 10 ticks (~150us) */
	writel_relaxed(10 | PM_PASSWORD, wdt_regs + PM_WDOG);
	val = readl_relaxed(wdt_regs + PM_RSTC);
	val &= ~PM_RSTC_WRCFG_MASK;
	val |= PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
	writel_relaxed(val, wdt_regs + PM_RSTC);

	/* No sleeping, possibly atomic. */
	mdelay(1);
}

/*
 * We can't really power off, but if we do the normal reset scheme, and
 * indicate to bootcode.bin not to reboot, then most of the chip will be
 * powered off.
 */
static void bcm2835_power_off(void)
{
	u32 val;

	/*
	 * We set the watchdog hard reset bit here to distinguish this reset
	 * from the normal (full) reset. bootcode.bin will not reboot after a
	 * hard reset.
	 */
	val = readl_relaxed(wdt_regs + PM_RSTS);
	val &= ~PM_RSTC_WRCFG_MASK;
	val |= PM_PASSWORD | PM_RSTS_HADWRH_SET;
	writel_relaxed(val, wdt_regs + PM_RSTS);

	/* Continue with normal reset mechanism */
	bcm2835_restart(0, "");
}

static struct map_desc io_map __initdata = {
	.virtual = BCM2835_PERIPH_VIRT,
	.pfn = __phys_to_pfn(BCM2835_PERIPH_PHYS),
	.length = BCM2835_PERIPH_SIZE,
	.type = MT_DEVICE
};

static void __init bcm2835_map_io(void)
{
	iotable_init(&io_map, 1);
}

static void __init bcm2835_init(void)
{
	int ret;

	bcm2835_setup_restart();
	if (wdt_regs)
		pm_power_off = bcm2835_power_off;

	bcm2835_init_clocks();

	ret = of_platform_populate(NULL, of_default_bus_match_table, NULL,
				   NULL);
	if (ret) {
		pr_err("of_platform_populate failed: %d\n", ret);
		BUG();
	}
}

static const char * const bcm2835_compat[] = {
	"brcm,bcm2835",
	NULL
};

DT_MACHINE_START(BCM2835, "BCM2835")
	.map_io = bcm2835_map_io,
	.init_irq = bcm2835_init_irq,
	.handle_irq = bcm2835_handle_irq,
	.init_machine = bcm2835_init,
	.init_time = clocksource_of_init,
	.restart = bcm2835_restart,
	.dt_compat = bcm2835_compat
MACHINE_END
