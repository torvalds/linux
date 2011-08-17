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
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mach/irqs.h>
#include <plat/dma.h>
#include <plat/cpu.h>
#include <plat/mcbsp.h>
#include <plat/omap_device.h>
#include <linux/pm_runtime.h>

#include "control.h"

/* McBSP internal signal muxing functions */

void omap2_mcbsp1_mux_clkr_src(u8 mux)
{
	u32 v;

	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	if (mux == CLKR_SRC_CLKR)
		v &= ~OMAP2_MCBSP1_CLKR_MASK;
	else if (mux == CLKR_SRC_CLKX)
		v |= OMAP2_MCBSP1_CLKR_MASK;
	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);
}
EXPORT_SYMBOL(omap2_mcbsp1_mux_clkr_src);

void omap2_mcbsp1_mux_fsr_src(u8 mux)
{
	u32 v;

	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	if (mux == FSR_SRC_FSR)
		v &= ~OMAP2_MCBSP1_FSR_MASK;
	else if (mux == FSR_SRC_FSX)
		v |= OMAP2_MCBSP1_FSR_MASK;
	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);
}
EXPORT_SYMBOL(omap2_mcbsp1_mux_fsr_src);

/* McBSP CLKS source switching function */

int omap2_mcbsp_set_clks_src(u8 id, u8 fck_src_id)
{
	struct omap_mcbsp *mcbsp;
	struct clk *fck_src;
	char *fck_src_name;
	int r;

	if (!omap_mcbsp_check_valid_id(id)) {
		pr_err("%s: Invalid id (%d)\n", __func__, id + 1);
		return -EINVAL;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (fck_src_id == MCBSP_CLKS_PAD_SRC)
		fck_src_name = "pad_fck";
	else if (fck_src_id == MCBSP_CLKS_PRCM_SRC)
		fck_src_name = "prcm_fck";
	else
		return -EINVAL;

	fck_src = clk_get(mcbsp->dev, fck_src_name);
	if (IS_ERR_OR_NULL(fck_src)) {
		pr_err("omap-mcbsp: %s: could not clk_get() %s\n", "clks",
		       fck_src_name);
		return -EINVAL;
	}

	pm_runtime_put_sync(mcbsp->dev);

	r = clk_set_parent(mcbsp->fclk, fck_src);
	if (IS_ERR_VALUE(r)) {
		pr_err("omap-mcbsp: %s: could not clk_set_parent() to %s\n",
		       "clks", fck_src_name);
		clk_put(fck_src);
		return -EINVAL;
	}

	pm_runtime_get_sync(mcbsp->dev);

	clk_put(fck_src);

	return 0;
}
EXPORT_SYMBOL(omap2_mcbsp_set_clks_src);

struct omap_device_pm_latency omap2_mcbsp_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func   = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static int omap_init_mcbsp(struct omap_hwmod *oh, void *unused)
{
	int id, count = 1;
	char *name = "omap-mcbsp";
	struct omap_hwmod *oh_device[2];
	struct omap_mcbsp_platform_data *pdata = NULL;
	struct omap_device *od;

	sscanf(oh->name, "mcbsp%d", &id);

	pdata = kzalloc(sizeof(struct omap_mcbsp_platform_data), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: No memory for mcbsp\n", __func__);
		return -ENOMEM;
	}

	pdata->mcbsp_config_type = oh->class->rev;

	if (oh->class->rev == MCBSP_CONFIG_TYPE3) {
		if (id == 2)
			/* The FIFO has 1024 + 256 locations */
			pdata->buffer_size = 0x500;
		else
			/* The FIFO has 128 locations */
			pdata->buffer_size = 0x80;
	}

	oh_device[0] = oh;

	if (oh->dev_attr) {
		oh_device[1] = omap_hwmod_lookup((
		(struct omap_mcbsp_dev_attr *)(oh->dev_attr))->sidetone);
		count++;
	}
	od = omap_device_build_ss(name, id, oh_device, count, pdata,
				sizeof(*pdata), omap2_mcbsp_latency,
				ARRAY_SIZE(omap2_mcbsp_latency), false);
	kfree(pdata);
	if (IS_ERR(od))  {
		pr_err("%s: Can't build omap_device for %s:%s.\n", __func__,
					name, oh->name);
		return PTR_ERR(od);
	}
	omap_mcbsp_count++;
	return 0;
}

static int __init omap2_mcbsp_init(void)
{
	omap_hwmod_for_each_by_class("mcbsp", omap_init_mcbsp, NULL);

	mcbsp_ptr = kzalloc(omap_mcbsp_count * sizeof(struct omap_mcbsp *),
								GFP_KERNEL);
	if (!mcbsp_ptr)
		return -ENOMEM;

	return omap_mcbsp_init();
}
arch_initcall(omap2_mcbsp_init);
