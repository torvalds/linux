/*
 * OMAP2+ MPU WD_TIMER-specific code
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>

#include <linux/platform_data/omap-wd-timer.h>

#include "omap_hwmod.h"
#include "omap_device.h"
#include "wd_timer.h"
#include "common.h"
#include "prm.h"
#include "soc.h"

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
	writel_relaxed(0xAAAA, base + OMAP_WDT_SPR);
	while (readl_relaxed(base + OMAP_WDT_WPS) & 0x10)
		cpu_relax();

	writel_relaxed(0x5555, base + OMAP_WDT_SPR);
	while (readl_relaxed(base + OMAP_WDT_WPS) & 0x10)
		cpu_relax();

	return 0;
}

/**
 * omap2_wdtimer_reset - reset and disable the WDTIMER IP block
 * @oh: struct omap_hwmod *
 *
 * After the WDTIMER IP blocks are reset on OMAP2/3, we must also take
 * care to execute the special watchdog disable sequence.  This is
 * because the watchdog is re-armed upon OCP softreset.  (On OMAP4,
 * this behavior was apparently changed and the watchdog is no longer
 * re-armed after an OCP soft-reset.)  Returns -ETIMEDOUT if the reset
 * did not complete, or 0 upon success.
 *
 * XXX Most of this code should be moved to the omap_hwmod.c layer
 * during a normal merge window.  omap_hwmod_softreset() should be
 * renamed to omap_hwmod_set_ocp_softreset(), and omap_hwmod_softreset()
 * should call the hwmod _ocp_softreset() code.
 */
int omap2_wd_timer_reset(struct omap_hwmod *oh)
{
	int c = 0;

	/* Write to the SOFTRESET bit */
	omap_hwmod_softreset(oh);

	/* Poll on RESETDONE bit */
	omap_test_timeout((omap_hwmod_read(oh,
					   oh->class->sysc->syss_offs)
			   & SYSS_RESETDONE_MASK),
			  MAX_MODULE_SOFTRESET_WAIT, c);

	if (oh->class->sysc->srst_udelay)
		udelay(oh->class->sysc->srst_udelay);

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warn("%s: %s: softreset failed (waited %d usec)\n",
			__func__, oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("%s: %s: softreset in %d usec\n", __func__,
			 oh->name, c);

	return (c == MAX_MODULE_SOFTRESET_WAIT) ? -ETIMEDOUT :
		omap2_wd_timer_disable(oh);
}
