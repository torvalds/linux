/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/common.h>
#include <mach/hardware.h>

extern unsigned long phys_l2x0_saved_regs;

static int imx6q_suspend_finish(unsigned long val)
{
	cpu_do_idle();
	return 0;
}

static int imx6q_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		imx6q_set_lpm(STOP_POWER_OFF);
		imx_gpc_pre_suspend();
		imx_set_cpu_jump(0, v7_cpu_resume);
		/* Zzz ... */
		cpu_suspend(0, imx6q_suspend_finish);
		imx_smp_prepare();
		imx_gpc_post_resume();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct platform_suspend_ops imx6q_pm_ops = {
	.enter = imx6q_pm_enter,
	.valid = suspend_valid_only_mem,
};

void __init imx6q_pm_init(void)
{
	/*
	 * The l2x0 core code provides an infrastucture to save and restore
	 * l2x0 registers across suspend/resume cycle.  But because imx6q
	 * retains L2 content during suspend and needs to resume L2 before
	 * MMU is enabled, it can only utilize register saving support and
	 * have to take care of restoring on its own.  So we save physical
	 * address of the data structure used by l2x0 core to save registers,
	 * and later restore the necessary ones in imx6q resume entry.
	 */
	phys_l2x0_saved_regs = __pa(&l2x0_saved_regs);

	suspend_set_ops(&imx6q_pm_ops);
}
