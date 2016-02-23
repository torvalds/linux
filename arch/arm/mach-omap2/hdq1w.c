/*
 * IP block integration code for the HDQ1W/1-wire IP block
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * Based on the I2C reset code in arch/arm/mach-omap2/i2c.c by
 *     Avinash.H.M <avinashhm@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

#ifndef CONFIG_OF
static int __init omap_init_hdq(void)
{
	int id = -1;
	struct platform_device *pdev;
	struct omap_hwmod *oh;
	char *oh_name = "hdq1w";
	char *devname = "omap_hdq";

	oh = omap_hwmod_lookup(oh_name);
	if (!oh)
		return 0;

	pdev = omap_device_build(devname, id, oh, NULL, 0);
	WARN(IS_ERR(pdev), "Can't build omap_device for %s:%s.\n",
	     devname, oh->name);

	return 0;
}
omap_arch_initcall(omap_init_hdq);
#endif
