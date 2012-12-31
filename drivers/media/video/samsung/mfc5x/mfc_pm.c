/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Power management module for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif

#include <plat/clock.h>
#include <plat/s5p-mfc.h>
#include <plat/cpu.h>

#include <mach/regs-pmu.h>

#include <asm/io.h>

#include "mfc_dev.h"
#include "mfc_log.h"

#define MFC_PARENT_CLK_NAME	"mout_mfc0"
#define MFC_CLKNAME		"sclk_mfc"
#define MFC_GATE_CLK_NAME	"mfc"

#undef CLK_DEBUG

static struct mfc_pm *pm;

#ifdef CLK_DEBUG
atomic_t clk_ref;
#endif

int mfc_init_pm(struct mfc_dev *mfcdev)
{
	struct clk *parent, *sclk;
	int ret = 0;

	pm = &mfcdev->pm;

	parent = clk_get(mfcdev->device, MFC_PARENT_CLK_NAME);
	if (IS_ERR(parent)) {
		printk(KERN_ERR "failed to get parent clock\n");
		ret = -ENOENT;
		goto err_gp_clk;
	}

	sclk = clk_get(mfcdev->device, MFC_CLKNAME);
	if (IS_ERR(sclk)) {
		printk(KERN_ERR "failed to get source clock\n");
		ret = -ENOENT;
		goto err_gs_clk;
	}

	ret = clk_set_parent(sclk, parent);
	if (ret) {
		printk(KERN_ERR "unable to set parent %s of clock %s\n",
				parent->name, sclk->name);
		goto err_sp_clk;
	}

	/* FIXME: clock name & rate have to move to machine code */
	ret = clk_set_rate(sclk, mfc_clk_rate);
	if (ret) {
		printk(KERN_ERR "%s rate change failed: %u\n", sclk->name, 200 * 1000000);
		goto err_ss_clk;
	}

	/* clock for gating */
	pm->clock = clk_get(mfcdev->device, MFC_GATE_CLK_NAME);
	if (IS_ERR(pm->clock)) {
		printk(KERN_ERR "failed to get clock-gating control\n");
		ret = -ENOENT;
		goto err_gg_clk;
	}

	atomic_set(&pm->power, 0);

#ifdef CONFIG_PM_RUNTIME
	pm->device = mfcdev->device;
	pm_runtime_enable(pm->device);
#endif

#ifdef CLK_DEBUG
	atomic_set(&clk_ref, 0);
#endif

	return 0;

err_gg_clk:
err_ss_clk:
err_sp_clk:
	clk_put(sclk);
err_gs_clk:
	clk_put(parent);
err_gp_clk:
	return ret;
}

void mfc_final_pm(struct mfc_dev *mfcdev)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(pm->device);
#endif
}

int mfc_clock_on(void)
{
#ifdef CLK_DEBUG
	atomic_inc(&clk_ref);
	mfc_dbg("+ %d", atomic_read(&clk_ref));
#endif

	return clk_enable(pm->clock);
}

void mfc_clock_off(void)
{
#ifdef CLK_DEBUG
	atomic_dec(&clk_ref);
	mfc_dbg("- %d", atomic_read(&clk_ref));
#endif

	clk_disable(pm->clock);
}

int mfc_power_on(void)
{
#ifdef CONFIG_PM_RUNTIME
	if ((soc_is_exynos4212() && (samsung_rev() < EXYNOS4212_REV_1_0)) ||
		(soc_is_exynos4412() && (samsung_rev() < EXYNOS4412_REV_1_1)))
		return 0;
	else
		return pm_runtime_get_sync(pm->device);
#else
	atomic_set(&pm->power, 1);

	return 0;
#endif
}

int mfc_power_off(void)
{
#ifdef CONFIG_PM_RUNTIME
	if ((soc_is_exynos4212() && (samsung_rev() < EXYNOS4212_REV_1_0)) ||
		(soc_is_exynos4412() && (samsung_rev() < EXYNOS4412_REV_1_1)))
		return 0;
	else
		return pm_runtime_put_sync(pm->device);
#else
	atomic_set(&pm->power, 0);

	return 0;
#endif
}

bool mfc_power_chk(void)
{
	mfc_dbg("%s", atomic_read(&pm->power) ? "on" : "off");

	return atomic_read(&pm->power) ? true : false;
}

void mfc_pd_enable(void)
{
	u32 timeout;

	__raw_writel(S5P_INT_LOCAL_PWR_EN, S5P_PMU_MFC_CONF);

	/* Wait max 1ms */
	timeout = 10;
	while ((__raw_readl(S5P_PMU_MFC_CONF + 0x4) & S5P_INT_LOCAL_PWR_EN)
		!= S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain MFC enable failed.\n");
			break;
		}

		timeout--;

		udelay(100);
	}
}

