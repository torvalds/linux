/*
 * i.MX27 Power Management Routines
 *
 * Based on Freescale's BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include <mach/system.h>
#include <mach/mx27.h>

static int mx27_suspend_enter(suspend_state_t state)
{
	u32 cscr;
	switch (state) {
	case PM_SUSPEND_MEM:
		/* Clear MPEN and SPEN to disable MPLL/SPLL */
		cscr = __raw_readl(MX27_IO_ADDRESS(MX27_CCM_BASE_ADDR));
		cscr &= 0xFFFFFFFC;
		__raw_writel(cscr, MX27_IO_ADDRESS(MX27_CCM_BASE_ADDR));
		/* Executes WFI */
		arch_idle();
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static struct platform_suspend_ops mx27_suspend_ops = {
	.enter = mx27_suspend_enter,
	.valid = suspend_valid_only_mem,
};

static int __init mx27_pm_init(void)
{
	suspend_set_ops(&mx27_suspend_ops);
	return 0;
}

device_initcall(mx27_pm_init);
