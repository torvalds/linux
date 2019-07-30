// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_pm.h"

static struct s5p_mfc_pm *pm;
static struct s5p_mfc_dev *p_dev;
static atomic_t clk_ref;

int s5p_mfc_init_pm(struct s5p_mfc_dev *dev)
{
	int i;

	pm = &dev->pm;
	p_dev = dev;

	pm->num_clocks = dev->variant->num_clocks;
	pm->clk_names = dev->variant->clk_names;
	pm->device = &dev->plat_dev->dev;
	pm->clock_gate = NULL;

	/* clock control */
	for (i = 0; i < pm->num_clocks; i++) {
		pm->clocks[i] = devm_clk_get(pm->device, pm->clk_names[i]);
		if (IS_ERR(pm->clocks[i])) {
			mfc_err("Failed to get clock: %s\n",
				pm->clk_names[i]);
			return PTR_ERR(pm->clocks[i]);
		}
	}

	if (dev->variant->use_clock_gating)
		pm->clock_gate = pm->clocks[0];

	pm_runtime_enable(pm->device);
	atomic_set(&clk_ref, 0);
	return 0;
}

void s5p_mfc_final_pm(struct s5p_mfc_dev *dev)
{
	pm_runtime_disable(pm->device);
}

int s5p_mfc_clock_on(void)
{
	atomic_inc(&clk_ref);
	mfc_debug(3, "+ %d\n", atomic_read(&clk_ref));

	return clk_enable(pm->clock_gate);
}

void s5p_mfc_clock_off(void)
{
	atomic_dec(&clk_ref);
	mfc_debug(3, "- %d\n", atomic_read(&clk_ref));

	clk_disable(pm->clock_gate);
}

int s5p_mfc_power_on(void)
{
	int i, ret = 0;

	ret = pm_runtime_get_sync(pm->device);
	if (ret < 0)
		return ret;

	/* clock control */
	for (i = 0; i < pm->num_clocks; i++) {
		ret = clk_prepare_enable(pm->clocks[i]);
		if (ret < 0) {
			mfc_err("clock prepare failed for clock: %s\n",
				pm->clk_names[i]);
			i++;
			goto err;
		}
	}

	/* prepare for software clock gating */
	clk_disable(pm->clock_gate);

	return 0;
err:
	while (--i > 0)
		clk_disable_unprepare(pm->clocks[i]);
	pm_runtime_put(pm->device);
	return ret;
}

int s5p_mfc_power_off(void)
{
	int i;

	/* finish software clock gating */
	clk_enable(pm->clock_gate);

	for (i = 0; i < pm->num_clocks; i++)
		clk_disable_unprepare(pm->clocks[i]);

	return pm_runtime_put_sync(pm->device);
}

