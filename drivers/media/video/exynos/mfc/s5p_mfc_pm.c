/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_pm.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include <plat/cpu.h>

#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_reg.h"

#define CLK_DEBUG

static struct s5p_mfc_pm *pm;
atomic_t clk_ref;

#if defined(CONFIG_ARCH_EXYNOS4)

#define MFC_PARENT_CLK_NAME	"mout_mfc0"
#define MFC_CLKNAME		"sclk_mfc"
#define MFC_GATE_CLK_NAME	"mfc"

int s5p_mfc_init_pm(struct s5p_mfc_dev *dev)
{
	struct clk *parent, *sclk;
	int ret = 0;

	pm = &dev->pm;

	parent = clk_get(dev->device, MFC_PARENT_CLK_NAME);
	if (IS_ERR(parent)) {
		printk(KERN_ERR "failed to get parent clock\n");
		ret = -ENOENT;
		goto err_p_clk;
	}

	sclk = clk_get(dev->device, MFC_CLKNAME);
	if (IS_ERR(sclk)) {
		printk(KERN_ERR "failed to get source clock\n");
		ret = -ENOENT;
		goto err_s_clk;
	}

	clk_set_parent(sclk, parent);
	clk_set_rate(sclk, 200 * 1000000);

	/* clock for gating */
	pm->clock = clk_get(dev->device, MFC_GATE_CLK_NAME);
	if (IS_ERR(pm->clock)) {
		printk(KERN_ERR "failed to get clock-gating control\n");
		ret = -ENOENT;
		goto err_g_clk;
	}

	atomic_set(&pm->power, 0);
	atomic_set(&clk_ref, 0);

	pm->device = dev->device;
	pm_runtime_enable(pm->device);

	return 0;

err_g_clk:
	clk_put(sclk);
err_s_clk:
	clk_put(parent);
err_p_clk:
	return ret;
}

#elif defined(CONFIG_ARCH_EXYNOS5)

#define MFC_PARENT_CLK_NAME	"aclk_333"
#define MFC_CLKNAME		"sclk_mfc"
#define MFC_GATE_CLK_NAME	"mfc"

int s5p_mfc_init_pm(struct s5p_mfc_dev *dev)
{
	struct clk *parent_clk = NULL;
	int ret = 0;

	pm = &dev->pm;

	/* clock for gating */
	pm->clock = clk_get(dev->device, MFC_GATE_CLK_NAME);
	if (IS_ERR(pm->clock)) {
		printk(KERN_ERR "failed to get clock-gating control\n");
		ret = PTR_ERR(pm->clock);
		goto err_g_clk;
	}

	if ((dev->pdata->ip_ver == IP_VER_MFC_5G_0) ||
	    (dev->pdata->ip_ver == IP_VER_MFC_5G_1)) {
		parent_clk = clk_get(dev->device, MFC_PARENT_CLK_NAME);
		if (IS_ERR(parent_clk)) {
			printk(KERN_ERR "failed to get parent clock %s.\n",
					MFC_PARENT_CLK_NAME);
			ret = PTR_ERR(parent_clk);
			goto err_p_clk;
		}

		clk_set_rate(parent_clk, dev->pdata->clock_rate);
	}

	spin_lock_init(&pm->clklock);
	atomic_set(&pm->power, 0);
	atomic_set(&clk_ref, 0);

	pm->device = dev->device;
	pm_runtime_enable(pm->device);

	clk_put(parent_clk);

	return 0;

err_p_clk:
	clk_put(pm->clock);
err_g_clk:
	return ret;
}

#endif

void s5p_mfc_final_pm(struct s5p_mfc_dev *dev)
{
	clk_put(pm->clock);

	pm_runtime_disable(pm->device);
}

int s5p_mfc_clock_on(void)
{
	int ret = 0;
	int state, val;
	struct s5p_mfc_dev *dev = platform_get_drvdata(to_platform_device(pm->device));
	unsigned long flags;

	ret = clk_enable(pm->clock);
	if (ret < 0)
		return ret;

	if (!dev->curr_ctx_drm) {
		ret = s5p_mfc_mem_resume(dev->alloc_ctx[0]);
		if (ret < 0) {
			clk_disable(pm->clock);
			return ret;
		}
	}

	if (IS_MFCV6(dev)) {
		spin_lock_irqsave(&pm->clklock, flags);
		if ((atomic_inc_return(&clk_ref) == 1) &&
				FW_HAS_BUS_RESET(dev)) {
			val = s5p_mfc_read_reg(S5P_FIMV_MFC_BUS_RESET_CTRL);
			val &= ~(0x1);
			s5p_mfc_write_reg(val, S5P_FIMV_MFC_BUS_RESET_CTRL);
		}
		spin_unlock_irqrestore(&pm->clklock, flags);
	} else {
		atomic_inc_return(&clk_ref);
	}

	state = atomic_read(&clk_ref);
	mfc_debug(3, "+ %d", state);

	return 0;
}

void s5p_mfc_clock_off(void)
{
	int state, val;
	unsigned long timeout, flags;
	struct s5p_mfc_dev *dev = platform_get_drvdata(to_platform_device(pm->device));

	if (IS_MFCV6(dev)) {
		spin_lock_irqsave(&pm->clklock, flags);
		if ((atomic_dec_return(&clk_ref) == 0) &&
				FW_HAS_BUS_RESET(dev)) {
			s5p_mfc_write_reg(0x1, S5P_FIMV_MFC_BUS_RESET_CTRL);

			timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT);
			/* Check bus status */
			do {
				if (time_after(jiffies, timeout)) {
					mfc_err("Timeout while resetting MFC.\n");
					break;
				}
				val = s5p_mfc_read_reg(
						S5P_FIMV_MFC_BUS_RESET_CTRL);
			} while ((val & 0x2) == 0);
		}
		spin_unlock_irqrestore(&pm->clklock, flags);
	} else {
		atomic_dec_return(&clk_ref);
	}

	state = atomic_read(&clk_ref);
	if (state < 0)
		mfc_err("Clock state is wrong(%d)\n", state);

	if (!dev->curr_ctx_drm)
		s5p_mfc_mem_suspend(dev->alloc_ctx[0]);
	clk_disable(pm->clock);
}

int s5p_mfc_power_on(void)
{
	atomic_set(&pm->power, 1);

	return pm_runtime_get_sync(pm->device);
}

int s5p_mfc_power_off(void)
{
	atomic_set(&pm->power, 0);

	return pm_runtime_put_sync(pm->device);
}

int s5p_mfc_get_clk_ref_cnt(void)
{
	return atomic_read(&clk_ref);
}
