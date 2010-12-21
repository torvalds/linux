/*
 * OMAP2+ MPU WD_TIMER-specific code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>

#include <plat/omap_hwmod.h>

/*
 * In order to avoid any assumptions from bootloader regarding WDT
 * settings, WDT module is reset during init. This enables the watchdog
 * timer. Hence it is required to disable the watchdog after the WDT reset
 * during init. Otherwise the system would reboot as per the default
 * watchdog timer registers settings.
 */
#define OMAP_WDT_WPS		0x34
#define OMAP_WDT_SPR		0x48


int omap2_wd_timer_disable(struct omap_hwmod *oh)
{
	void __iomem *base;

	if (!oh) {
		pr_err("%s: Could not look up wdtimer_hwmod\n", __func__);
		return -EINVAL;
	}

	base = omap_hwmod_get_mpu_rt_va(oh);
	if (!base) {
		pr_err("%s: Could not get the base address for %s\n",
				oh->name, __func__);
		return -EINVAL;
	}

	/* sequence required to disable watchdog */
	__raw_writel(0xAAAA, base + OMAP_WDT_SPR);
	while (__raw_readl(base + OMAP_WDT_WPS) & 0x10)
		cpu_relax();

	__raw_writel(0x5555, base + OMAP_WDT_SPR);
	while (__raw_readl(base + OMAP_WDT_WPS) & 0x10)
		cpu_relax();

	return 0;
}

