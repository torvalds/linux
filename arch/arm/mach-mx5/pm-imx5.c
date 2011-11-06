/*
 *  Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/suspend.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include "crm_regs.h"

static struct clk *gpc_dvfs_clk;

static int mx5_suspend_prepare(void)
{
	return clk_enable(gpc_dvfs_clk);
}

static int mx5_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		mx5_cpu_lp_set(STOP_POWER_OFF);
		break;
	case PM_SUSPEND_STANDBY:
		mx5_cpu_lp_set(WAIT_UNCLOCKED_POWER_OFF);
		break;
	default:
		return -EINVAL;
	}

	if (state == PM_SUSPEND_MEM) {
		local_flush_tlb_all();
		flush_cache_all();

		/*clear the EMPGC0/1 bits */
		__raw_writel(0, MXC_SRPG_EMPGC0_SRPGCR);
		__raw_writel(0, MXC_SRPG_EMPGC1_SRPGCR);
	}
	cpu_do_idle();
	return 0;
}

static void mx5_suspend_finish(void)
{
	clk_disable(gpc_dvfs_clk);
}

static int mx5_pm_valid(suspend_state_t state)
{
	return (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX);
}

static const struct platform_suspend_ops mx5_suspend_ops = {
	.valid = mx5_pm_valid,
	.prepare = mx5_suspend_prepare,
	.enter = mx5_suspend_enter,
	.finish = mx5_suspend_finish,
};

static int __init mx5_pm_init(void)
{
	if (gpc_dvfs_clk == NULL)
		gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs");

	if (!IS_ERR(gpc_dvfs_clk)) {
		if (cpu_is_mx51())
			suspend_set_ops(&mx5_suspend_ops);
	} else
		return -EPERM;

	return 0;
}
device_initcall(mx5_pm_init);
