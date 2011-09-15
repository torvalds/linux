/* arch/arm/mach-rk29/ddrfreq.c
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <mach/clock.h>
#include <mach/ddr.h>

#define MHZ	(1000*1000)

enum {
	DEBUG_CHANGE	= 1U << 0,
	DEBUG_EVENT	= 1U << 1,
};
static uint debug_mask = DEBUG_CHANGE;
module_param(debug_mask, uint, 0644);
#define dprintk(mask, fmt, ...) do { if (mask & debug_mask) printk(KERN_DEBUG "ddrfreq: " fmt, ##__VA_ARGS__); } while (0)

static struct clk *clk_ddr;
static struct clk *ddr_pll_clk;
static struct clk *aclk_lcdc;
static bool ddr_pll_can_change;
static bool aclk_lcdc_disabled;
static bool disable_ddr_freq;
module_param(ddr_pll_can_change, bool, 0644);
module_param(aclk_lcdc_disabled, bool, 0644);
module_param(disable_ddr_freq, bool, 0644);

static unsigned long ddr_max_mhz;
static unsigned long ddr_min_mhz = 96;
static void rk29_ddrfreq_change_freq(void);
static int rk29_ddrfreq_set_ddr_mhz(const char *val, struct kernel_param *kp)
{
	int err = param_set_uint(val, kp);
	if (!err) {
		rk29_ddrfreq_change_freq();
	}
	return err;
}
module_param_call(ddr_max_mhz, rk29_ddrfreq_set_ddr_mhz, param_get_uint, &ddr_max_mhz, 0644);
module_param_call(ddr_min_mhz, rk29_ddrfreq_set_ddr_mhz, param_get_uint, &ddr_min_mhz, 0644);

static DEFINE_SPINLOCK(ddr_lock);

static void rk29_ddrfreq_change_freq(void)
{
	unsigned long ddr_rate, mhz;

	if (disable_ddr_freq)
		return;

	ddr_rate = clk_get_rate(clk_ddr);
	mhz = (ddr_pll_can_change && aclk_lcdc_disabled) ? ddr_min_mhz : ddr_max_mhz;
	if ((mhz * MHZ) != ddr_rate) {
		dprintk(DEBUG_CHANGE, "%lu -> %lu Hz\n", ddr_rate, mhz * MHZ);
		ddr_change_freq(mhz);
		dprintk(DEBUG_CHANGE, "got %lu Hz\n", clk_get_rate(clk_ddr));
	}
}

static int rk29_ddrfreq_ddr_pll_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	spin_lock_bh(&ddr_lock);
	switch (event) {
	case CLK_PRE_ENABLE:
		ddr_pll_can_change = false;
		break;
	case CLK_ABORT_ENABLE:
	case CLK_POST_DISABLE:
		ddr_pll_can_change = true;
		break;
	default:
		goto out;
	}

	if (!disable_ddr_freq) {
		dprintk(DEBUG_EVENT, "event: %lu ddr_pll_can_change: %d\n", event, ddr_pll_can_change);
		rk29_ddrfreq_change_freq();
	}
out:
	spin_unlock_bh(&ddr_lock);
	return NOTIFY_OK;
}

static struct notifier_block rk29_ddrfreq_ddr_pll_notifier = {
	.notifier_call = rk29_ddrfreq_ddr_pll_notifier_event,
};

static int rk29_ddrfreq_aclk_lcdc_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	spin_lock_bh(&ddr_lock);
	switch (event) {
	case CLK_PRE_ENABLE:
		aclk_lcdc_disabled = false;
		break;
	case CLK_ABORT_ENABLE:
	case CLK_POST_DISABLE:
		aclk_lcdc_disabled = true;
		break;
	default:
		goto out;
	}

	if (!disable_ddr_freq) {
		dprintk(DEBUG_EVENT, "event: %lu aclk_lcdc_disabled: %d\n", event, aclk_lcdc_disabled);
		rk29_ddrfreq_change_freq();
	}
out:
	spin_unlock_bh(&ddr_lock);
	return NOTIFY_OK;
}

static struct notifier_block rk29_ddrfreq_aclk_lcdc_notifier = {
	.notifier_call = rk29_ddrfreq_aclk_lcdc_notifier_event,
};

static int __init rk29_ddrfreq_init(void)
{
	clk_ddr = clk_get(NULL, "ddr");
	if (IS_ERR(clk_ddr)) {
		int err = PTR_ERR(clk_ddr);
		pr_err("fail to get ddr clk: %d\n", err);
		clk_ddr = NULL;
		return err;
	}

	ddr_max_mhz = clk_get_rate(clk_ddr) / MHZ;

	ddr_pll_clk = clk_get(NULL, "ddr_pll");
	aclk_lcdc = clk_get(NULL, "aclk_lcdc");

	clk_notifier_register(ddr_pll_clk, &rk29_ddrfreq_ddr_pll_notifier);
	clk_notifier_register(aclk_lcdc, &rk29_ddrfreq_aclk_lcdc_notifier);

	printk("ddrfreq: version 1.0\n");
	return 0;
}

late_initcall(rk29_ddrfreq_init);
