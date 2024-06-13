// SPDX-License-Identifier: GPL-2.0-only
/*
 * IP block integration code for the HDQ1W/1-wire IP block
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * Based on the I2C reset code in arch/arm/mach-omap2/i2c.c by
 *     Avinash.H.M <avinashhm@ti.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"
#include "hdq1w.h"

#include "prm.h"
#include "common.h"

/**
 * omap_hdq1w_reset - reset the OMAP HDQ1W module
 * @oh: struct omap_hwmod *
 *
 * OCP soft reset the HDQ1W IP block.  Section 20.6.1.4 "HDQ1W/1-Wire
 * Software Reset" of the OMAP34xx Technical Reference Manual Revision
 * ZR (SWPU223R) does not include the rather important fact that, for
 * the reset to succeed, the HDQ1W module's internal clock gate must be
 * programmed to allow the clock to propagate to the rest of the
 * module.  In this sense, it's rather similar to the I2C custom reset
 * function.  Returns 0.
 */
int omap_hdq1w_reset(struct omap_hwmod *oh)
{
	u32 v;
	int c = 0;

	/* Write to the SOFTRESET bit */
	omap_hwmod_softreset(oh);

	/* Enable the module's internal clocks */
	v = omap_hwmod_read(oh, HDQ_CTRL_STATUS_OFFSET);
	v |= 1 << HDQ_CTRL_STATUS_CLOCKENABLE_SHIFT;
	omap_hwmod_write(v, oh, HDQ_CTRL_STATUS_OFFSET);

	/* Poll on RESETDONE bit */
	omap_test_timeout((omap_hwmod_read(oh,
					   oh->class->sysc->syss_offs)
			   & SYSS_RESETDONE_MASK),
			  MAX_MODULE_SOFTRESET_WAIT, c);

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warn("%s: %s: softreset failed (waited %d usec)\n",
			__func__, oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("%s: %s: softreset in %d usec\n", __func__,
			 oh->name, c);

	return 0;
}
