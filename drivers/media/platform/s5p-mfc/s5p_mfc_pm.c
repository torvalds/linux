/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_pm.h"

#define MFC_GATE_CLK_NAME	"mfc"
#define MFC_SCLK_NAME		"sclk_mfc"

static struct s5p_mfc_pm *pm;
static struct s5p_mfc_dev *p_dev;
static atomic_t clk_ref;

int s5p_mfc_init_pm(struct s5p_mfc_dev *dev)
{
	int ret = 0;

	pm = &dev->pm;
	p_dev = dev;
	pm->use_clock_gating = dev->variant->use_clock_gating;
	pm->clock_gate = clk_get(&dev->plat_dev->dev, MFC_GATE_CLK_NAME);
	if (IS_ERR(pm->clock_gate)) {
		mfc_err("Failed to get clock-gating control\n");
		ret = PTR_ERR(pm->clock_gate);
		goto err_g_ip_clk;
	}

	if (dev->variant->version != MFC_VERSION_V6) {
		pm->clock = clk_get(&dev->plat_dev->dev, MFC_SCLK_NAME);
		if (IS_ERR(pm->clock)) {
			mfc_info("Failed to get MFC special clock control\n");
			pm->clock = NULL;
		}
	}

	pm->device = &dev->plat_dev->dev;
	pm_runtime_enable(pm->device);
	atomic_set(&clk_ref, 0);

	return 0;

	clk_put(pm->clock_gate);
	pm->clock_gate = NULL;
err_g_ip_clk:
	return ret;
}

void s5p_mfc_final_pm(struct s5p_mfc_dev *dev)
{
	if (dev->variant->version != MFC_VERSION_V6 &&
	    pm->clock) {
		clk_put(pm->clock);
		pm->clock = NULL;
	}
	clk_put(pm->clock_gate);
	pm->clock_gate = NULL;
	pm_runtime_disable(pm->device);
}

int s5p_mfc_clock_on(void)
{
	atomic_inc(&clk_ref);
	mfc_debug(3, "+ %d\n", atomic_read(&clk_ref));

	if (!pm->use_clock_gating)
		return 0;
	return clk_enable(pm->clock_gate);
}

void s5p_mfc_clock_off(void)
{
	atomic_dec(&clk_ref);
	mfc_debug(3, "- %d\n", atomic_read(&clk_ref));

	if (!pm->use_clock_gating)
		return;
	clk_disable(pm->clock_gate);
}

int s5p_mfc_power_on(void)
{
	int ret;

	ret = pm_runtime_get_sync(pm->device);
	if (ret)
		return ret;

	ret = clk_prepare_enable(pm->clock_gate);
	if (ret)
		goto err_pm;

	if (pm->clock) {
		ret = clk_prepare_enable(pm->clock);
		if (ret)
			goto err_gate;
	}

	if (pm->use_clock_gating)
		clk_disable(pm->clock_gate);
	return 0;

err_gate:
	clk_disable_unprepare(pm->clock_gate);
err_pm:
	pm_runtime_put_sync(pm->device);
	return ret;

}

int s5p_mfc_power_off(void)
{
	if (pm->clock)
		clk_disable_unprepare(pm->clock);

	if (pm->use_clock_gating)
		clk_unprepare(pm->clock_gate);
	else
		clk_disable_unprepare(pm->clock_gate);

	return pm_runtime_put_sync(pm->device);
}

