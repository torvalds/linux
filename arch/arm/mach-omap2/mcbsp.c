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

#include <plat/dma.h>
#include <plat/mcbsp.h>
#include <plat/omap_device.h>
#include <linux/pm_runtime.h>

#include "control.h"

/*
 * FIXME: Find a mechanism to enable/disable runtime the McBSP ICLK autoidle.
 * Sidetone needs non-gated ICLK and sidetone autoidle is broken.
 */
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"

/* McBSP1 internal signal muxing function for OMAP2/3 */
static int omap2_mcbsp1_mux_rx_clk(struct device *dev, const char *signal,
				   const char *src)
{
	u32 v;

	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);

	if (!strcmp(signal, "clkr")) {
		if (!strcmp(src, "clkr"))
			v &= ~OMAP2_MCBSP1_CLKR_MASK;
		else if (!strcmp(src, "clkx"))
			v |= OMAP2_MCBSP1_CLKR_MASK;
		else
			return -EINVAL;
	} else if (!strcmp(signal, "fsr")) {
		if (!strcmp(src, "fsr"))
			v &= ~OMAP2_MCBSP1_FSR_MASK;
		else if (!strcmp(src, "fsx"))
			v |= OMAP2_MCBSP1_FSR_MASK;
		else
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);

	return 0;
}

/* McBSP4 internal signal muxing function for OMAP4 */
#define OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_FSX	(1 << 31)
#define OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_CLKX	(1 << 30)
static int omap4_mcbsp4_mux_rx_clk(struct device *dev, const char *signal,
				   const char *src)
{
	u32 v;

	/*
	 * In CONTROL_MCBSPLP register only bit 30 (CLKR mux), and bit 31 (FSR
	 * mux) is used */
	v = omap4_ctrl_pad_readl(OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_MCBSPLP);

	if (!strcmp(signal, "clkr")) {
		if (!strcmp(src, "clkr"))
			v &= ~OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_CLKX;
		else if (!strcmp(src, "clkx"))
			v |= OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_CLKX;
		else
			return -EINVAL;
	} else if (!strcmp(signal, "fsr")) {
		if (!strcmp(src, "fsr"))
			v &= ~OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_FSX;
		else if (!strcmp(src, "fsx"))
			v |= OMAP4_CONTROL_MCBSPLP_ALBCTRLRX_FSX;
		else
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	omap4_ctrl_pad_writel(v, OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_MCBSPLP);

	return 0;
}

/* McBSP CLKS source switching function */
static int omap2_mcbsp_set_clk_src(struct device *dev, struct clk *clk,
				   const char *src)
{
	struct clk *fck_src;
	char *fck_src_name;
	int r;

	if (!strcmp(src, "clks_ext"))
		fck_src_name = "pad_fck";
	else if (!strcmp(src, "clks_fclk"))
		fck_src_name = "prcm_fck";
	else
		return -EINVAL;

	fck_src = clk_get(dev, fck_src_name);
	if (IS_ERR_OR_NULL(fck_src)) {
		pr_err("omap-mcbsp: %s: could not clk_get() %s\n", "clks",
		       fck_src_name);
		return -EINVAL;
	}

	pm_runtime_put_sync(dev);

	r = clk_set_parent(clk, fck_src);
	if (IS_ERR_VALUE(r)) {
		pr_err("omap-mcbsp: %s: could not clk_set_parent() to %s\n",
		       "clks", fck_src_name);
		clk_put(fck_src);
		return -EINVAL;
	}

	pm_runtime_get_sync(dev);

	clk_put(fck_src);

	return 0;
}

static int omap3_enable_st_clock(unsigned int id, bool enable)
{
	unsigned int w;

	/*
	 * Sidetone uses McBSP ICLK - which must not idle when sidetones
	 * are enabled or sidetones start sounding ugly.
	 */
	w = omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	if (enable)
		w &= ~(1 << (id - 2));
	else
		w |= 1 << (id - 2);
	omap2_cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);

	return 0;
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
	pdata->set_clk_src = omap2_mcbsp_set_clk_src;

	/* On OMAP2/3 the McBSP1 port has 6 pin configuration */
	if (id == 1 && oh->class->rev < MCBSP_CONFIG_TYPE4)
		pdata->mux_signal = omap2_mcbsp1_mux_rx_clk;

	/* On OMAP4 the McBSP4 port has 6 pin configuration */
	if (id == 4 && oh->class->rev == MCBSP_CONFIG_TYPE4)
		pdata->mux_signal = omap4_mcbsp4_mux_rx_clk;

	if (oh->class->rev == MCBSP_CONFIG_TYPE3) {
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
		pdata->enable_st_clock = omap3_enable_st_clock;
		count++;
	}
	pdev = omap_device_build_ss(name, id, oh_device, count, pdata,
				sizeof(*pdata), NULL, 0, false);
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
	omap_hwmod_for_each_by_class("mcbsp", omap_init_mcbsp, NULL);

	return 0;
}
arch_initcall(omap2_mcbsp_init);
