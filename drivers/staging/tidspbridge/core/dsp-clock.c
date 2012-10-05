/*
 * clk.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Clock and Timer services.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <plat/dmtimer.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>
#include "_tiomap.h"

/*  ----------------------------------- This */
#include <dspbridge/clk.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */

#define OMAP_SSI_OFFSET			0x58000
#define OMAP_SSI_SIZE			0x1000
#define OMAP_SSI_SYSCONFIG_OFFSET	0x10

#define SSI_AUTOIDLE			(1 << 0)
#define SSI_SIDLE_SMARTIDLE		(2 << 3)
#define SSI_MIDLE_NOIDLE		(1 << 12)

/* Clk types requested by the dsp */
#define IVA2_CLK	0
#define GPT_CLK		1
#define WDT_CLK		2
#define MCBSP_CLK	3
#define SSI_CLK		4

/* Bridge GPT id (1 - 4), DM Timer id (5 - 8) */
#define DMT_ID(id) ((id) + 4)
#define DM_TIMER_CLOCKS		4

/* Bridge MCBSP id (6 - 10), OMAP Mcbsp id (0 - 4) */
#define MCBSP_ID(id) ((id) - 6)

static struct omap_dm_timer *timer[4];

struct clk *iva2_clk;

struct dsp_ssi {
	struct clk *sst_fck;
	struct clk *ssr_fck;
	struct clk *ick;
};

static struct dsp_ssi ssi;

static u32 dsp_clocks;

static inline u32 is_dsp_clk_active(u32 clk, u8 id)
{
	return clk & (1 << id);
}

static inline void set_dsp_clk_active(u32 *clk, u8 id)
{
	*clk |= (1 << id);
}

static inline void set_dsp_clk_inactive(u32 *clk, u8 id)
{
	*clk &= ~(1 << id);
}

static s8 get_clk_type(u8 id)
{
	s8 type;

	if (id == DSP_CLK_IVA2)
		type = IVA2_CLK;
	else if (id <= DSP_CLK_GPT8)
		type = GPT_CLK;
	else if (id == DSP_CLK_WDT3)
		type = WDT_CLK;
	else if (id <= DSP_CLK_MCBSP5)
		type = MCBSP_CLK;
	else if (id == DSP_CLK_SSI)
		type = SSI_CLK;
	else
		type = -1;

	return type;
}

/*
 *  ======== dsp_clk_exit ========
 *  Purpose:
 *      Cleanup CLK module.
 */
void dsp_clk_exit(void)
{
	int i;

	dsp_clock_disable_all(dsp_clocks);

	for (i = 0; i < DM_TIMER_CLOCKS; i++)
		omap_dm_timer_free(timer[i]);

	clk_put(iva2_clk);
	clk_put(ssi.sst_fck);
	clk_put(ssi.ssr_fck);
	clk_put(ssi.ick);
}

/*
 *  ======== dsp_clk_init ========
 *  Purpose:
 *      Initialize CLK module.
 */
void dsp_clk_init(void)
{
	static struct platform_device dspbridge_device;
	int i, id;

	dspbridge_device.dev.bus = &platform_bus_type;

	for (i = 0, id = 5; i < DM_TIMER_CLOCKS; i++, id++)
		timer[i] = omap_dm_timer_request_specific(id);

	iva2_clk = clk_get(&dspbridge_device.dev, "iva2_ck");
	if (IS_ERR(iva2_clk))
		dev_err(bridge, "failed to get iva2 clock %p\n", iva2_clk);

	ssi.sst_fck = clk_get(&dspbridge_device.dev, "ssi_sst_fck");
	ssi.ssr_fck = clk_get(&dspbridge_device.dev, "ssi_ssr_fck");
	ssi.ick = clk_get(&dspbridge_device.dev, "ssi_ick");

	if (IS_ERR(ssi.sst_fck) || IS_ERR(ssi.ssr_fck) || IS_ERR(ssi.ick))
		dev_err(bridge, "failed to get ssi: sst %p, ssr %p, ick %p\n",
					ssi.sst_fck, ssi.ssr_fck, ssi.ick);
}

/**
 * dsp_gpt_wait_overflow - set gpt overflow and wait for fixed timeout
 * @clk_id:      GP Timer clock id.
 * @load:        Overflow value.
 *
 * Sets an overflow interrupt for the desired GPT waiting for a timeout
 * of 5 msecs for the interrupt to occur.
 */
void dsp_gpt_wait_overflow(short int clk_id, unsigned int load)
{
	struct omap_dm_timer *gpt = timer[clk_id - 1];
	unsigned long timeout;

	if (!gpt)
		return;

	/* Enable overflow interrupt */
	omap_dm_timer_set_int_enable(gpt, OMAP_TIMER_INT_OVERFLOW);

	/*
	 * Set counter value to overflow counter after
	 * one tick and start timer.
	 */
	omap_dm_timer_set_load_start(gpt, 0, load);

	/* Wait 80us for timer to overflow */
	udelay(80);

	timeout = msecs_to_jiffies(5);
	/* Check interrupt status and wait for interrupt */
	while (!(omap_dm_timer_read_status(gpt) & OMAP_TIMER_INT_OVERFLOW)) {
		if (time_is_after_jiffies(timeout)) {
			pr_err("%s: GPTimer interrupt failed\n", __func__);
			break;
		}
	}
}

/*
 *  ======== dsp_clk_enable ========
 *  Purpose:
 *      Enable Clock .
 *
 */
int dsp_clk_enable(enum dsp_clk_id clk_id)
{
	int status = 0;

	if (is_dsp_clk_active(dsp_clocks, clk_id)) {
		dev_err(bridge, "WARN: clock id %d already enabled\n", clk_id);
		goto out;
	}

	switch (get_clk_type(clk_id)) {
	case IVA2_CLK:
		clk_enable(iva2_clk);
		break;
	case GPT_CLK:
		status = omap_dm_timer_start(timer[clk_id - 1]);
		break;
#ifdef CONFIG_OMAP_MCBSP
	case MCBSP_CLK:
		omap_mcbsp_request(MCBSP_ID(clk_id));
		omap2_mcbsp_set_clks_src(MCBSP_ID(clk_id), MCBSP_CLKS_PAD_SRC);
		break;
#endif
	case WDT_CLK:
		dev_err(bridge, "ERROR: DSP requested to enable WDT3 clk\n");
		break;
	case SSI_CLK:
		clk_enable(ssi.sst_fck);
		clk_enable(ssi.ssr_fck);
		clk_enable(ssi.ick);

		/*
		 * The SSI module need to configured not to have the Forced
		 * idle for master interface. If it is set to forced idle,
		 * the SSI module is transitioning to standby thereby causing
		 * the client in the DSP hang waiting for the SSI module to
		 * be active after enabling the clocks
		 */
		ssi_clk_prepare(true);
		break;
	default:
		dev_err(bridge, "Invalid clock id for enable\n");
		status = -EPERM;
	}

	if (!status)
		set_dsp_clk_active(&dsp_clocks, clk_id);

out:
	return status;
}

/**
 * dsp_clock_enable_all - Enable clocks used by the DSP
 * @dev_context		Driver's device context strucure
 *
 * This function enables all the peripheral clocks that were requested by DSP.
 */
u32 dsp_clock_enable_all(u32 dsp_per_clocks)
{
	u32 clk_id;
	u32 status = -EPERM;

	for (clk_id = 0; clk_id < DSP_CLK_NOT_DEFINED; clk_id++) {
		if (is_dsp_clk_active(dsp_per_clocks, clk_id))
			status = dsp_clk_enable(clk_id);
	}

	return status;
}

/*
 *  ======== dsp_clk_disable ========
 *  Purpose:
 *      Disable the clock.
 *
 */
int dsp_clk_disable(enum dsp_clk_id clk_id)
{
	int status = 0;

	if (!is_dsp_clk_active(dsp_clocks, clk_id)) {
		dev_err(bridge, "ERR: clock id %d already disabled\n", clk_id);
		goto out;
	}

	switch (get_clk_type(clk_id)) {
	case IVA2_CLK:
		clk_disable(iva2_clk);
		break;
	case GPT_CLK:
		status = omap_dm_timer_stop(timer[clk_id - 1]);
		break;
#ifdef CONFIG_OMAP_MCBSP
	case MCBSP_CLK:
		omap2_mcbsp_set_clks_src(MCBSP_ID(clk_id), MCBSP_CLKS_PRCM_SRC);
		omap_mcbsp_free(MCBSP_ID(clk_id));
		break;
#endif
	case WDT_CLK:
		dev_err(bridge, "ERROR: DSP requested to disable WDT3 clk\n");
		break;
	case SSI_CLK:
		ssi_clk_prepare(false);
		ssi_clk_prepare(false);
		clk_disable(ssi.sst_fck);
		clk_disable(ssi.ssr_fck);
		clk_disable(ssi.ick);
		break;
	default:
		dev_err(bridge, "Invalid clock id for disable\n");
		status = -EPERM;
	}

	if (!status)
		set_dsp_clk_inactive(&dsp_clocks, clk_id);

out:
	return status;
}

/**
 * dsp_clock_disable_all - Disable all active clocks
 * @dev_context		Driver's device context structure
 *
 * This function disables all the peripheral clocks that were enabled by DSP.
 * It is meant to be called only when DSP is entering hibernation or when DSP
 * is in error state.
 */
u32 dsp_clock_disable_all(u32 dsp_per_clocks)
{
	u32 clk_id;
	u32 status = -EPERM;

	for (clk_id = 0; clk_id < DSP_CLK_NOT_DEFINED; clk_id++) {
		if (is_dsp_clk_active(dsp_per_clocks, clk_id))
			status = dsp_clk_disable(clk_id);
	}

	return status;
}

u32 dsp_clk_get_iva2_rate(void)
{
	u32 clk_speed_khz;

	clk_speed_khz = clk_get_rate(iva2_clk);
	clk_speed_khz /= 1000;
	dev_dbg(bridge, "%s: clk speed Khz = %d\n", __func__, clk_speed_khz);

	return clk_speed_khz;
}

void ssi_clk_prepare(bool FLAG)
{
	void __iomem *ssi_base;
	unsigned int value;

	ssi_base = ioremap(L4_34XX_BASE + OMAP_SSI_OFFSET, OMAP_SSI_SIZE);
	if (!ssi_base) {
		pr_err("%s: error, SSI not configured\n", __func__);
		return;
	}

	if (FLAG) {
		/* Set Autoidle, SIDLEMode to smart idle, and MIDLEmode to
		 * no idle
		 */
		value = SSI_AUTOIDLE | SSI_SIDLE_SMARTIDLE | SSI_MIDLE_NOIDLE;
	} else {
		/* Set Autoidle, SIDLEMode to forced idle, and MIDLEmode to
		 * forced idle
		 */
		value = SSI_AUTOIDLE;
	}

	__raw_writel(value, ssi_base + OMAP_SSI_SYSCONFIG_OFFSET);
	iounmap(ssi_base);
}

