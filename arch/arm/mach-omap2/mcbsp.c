/*
 * linux/arch/arm/mach-omap2/mcbsp.c
 *
 * Copyright (C) 2008 Instituto Nokia de Tecnologia
 * Contact: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include <linux/pm_runtime.h>

#include <linux/omap-dma.h>

#include "soc.h"
#include "omap_device.h"
#include "clock.h"

/*
 * FIXME: Find a mechanism to enable/disable runtime the McBSP ICLK autoidle.
 * Sidetone needs non-gated ICLK and sidetone autoidle is broken.
 */
#include "cm3xxx.h"
#include "cm-regbits-34xx.h"

static int omap3_mcbsp_force_ick_on(struct clk *clk, bool force_on)
{
	if (!clk)
		return 0;

	if (force_on)
		return omap2_clk_deny_idle(clk);
	else
		return omap2_clk_allow_idle(clk);
}

void __init omap3_mcbsp_init_pdata_callback(
					struct omap_mcbsp_platform_data *pdata)
{
	if (!pdata)
		return;

	pdata->force_ick_on = omap3_mcbsp_force_ick_on;
}

static int __init omap_init_mcbsp(struct omap_hwmod *oh, void *unused)
{
	int id, count = 1;
	char *name = "omap-mcbsp";
	struct omap_hwmod *oh_device[2];
	struct omap_mcbsp_platform_data *pdata = NULL;
	struct platform_device *pdev;

	sscanf(oh->name, "mcbsp%d", &id);

	pdata = kzalloc(sizeof(struct omap_mcbsp_platform_data), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: No memory for mcbsp\n", __func__);
		return -ENOMEM;
	}

	pdata->reg_step = 4;
	if (oh->class->rev < MCBSP_CONFIG_TYPE2) {
		pdata->reg_size = 2;
	} else {
		pdata->reg_size = 4;
		pdata->has_ccr = true;
	}

	if (oh->class->rev == MCBSP_CONFIG_TYPE2) {
		/* The FIFO has 128 locations */
		pdata->buffer_size = 0x80;
	} else if (oh->class->rev == MCBSP_CONFIG_TYPE3) {
		if (id == 2)
			/* The FIFO has 1024 + 256 locations */
			pdata->buffer_size = 0x500;
		else
			/* The FIFO has 128 locations */
			pdata->buffer_size = 0x80;
	} else if (oh->class->rev == MCBSP_CONFIG_TYPE4) {
		/* The FIFO has 128 locations for all instances */
		pdata->buffer_size = 0x80;
	}

	if (oh->class->rev >= MCBSP_CONFIG_TYPE3)
		pdata->has_wakeup = true;

	oh_device[0] = oh;

	if (oh->dev_attr) {
		oh_device[1] = omap_hwmod_lookup((
		(struct omap_mcbsp_dev_attr *)(oh->dev_attr))->sidetone);
		pdata->force_ick_on = omap3_mcbsp_force_ick_on;
		count++;
	}
	pdev = omap_device_build_ss(name, id, oh_device, count, pdata,
				    sizeof(*pdata));
	kfree(pdata);
	if (IS_ERR(pdev))  {
		pr_err("%s: Can't build omap_device for %s:%s.\n", __func__,
					name, oh->name);
		return PTR_ERR(pdev);
	}
	return 0;
}

static int __init omap2_mcbsp_init(void)
{
	if (!of_have_populated_dt())
		omap_hwmod_for_each_by_class("mcbsp", omap_init_mcbsp, NULL);

	return 0;
}
omap_arch_initcall(omap2_mcbsp_init);
