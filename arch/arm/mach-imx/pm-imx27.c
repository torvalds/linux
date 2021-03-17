/*
 * i.MX27 Power Management Routines
 *
 * Based on Freescale's BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include <linux/of_address.h>
#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/io.h>

#include "hardware.h"

static int mx27_suspend_enter(suspend_state_t state)
{
	void __iomem *ccm_base;
	struct device_node *np;
	u32 cscr;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx27-ccm");
	ccm_base = of_iomap(np, 0);
	BUG_ON(!ccm_base);

	switch (state) {
	case PM_SUSPEND_MEM:
		/* Clear MPEN and SPEN to disable MPLL/SPLL */
		cscr = imx_readl(ccm_base);
		cscr &= 0xFFFFFFFC;
		imx_writel(cscr, ccm_base);
		/* Executes WFI */
		cpu_do_idle();
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static const struct platform_suspend_ops mx27_suspend_ops = {
	.enter = mx27_suspend_enter,
	.valid = suspend_valid_only_mem,
};

void __init imx27_pm_init(void)
{
	suspend_set_ops(&mx27_suspend_ops);
}
