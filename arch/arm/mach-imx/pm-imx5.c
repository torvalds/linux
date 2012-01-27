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
#include "crm-regs-imx5.h"

static struct clk *gpc_dvfs_clk;

/*
 * set cpu low power mode before WFI instruction. This function is called
 * mx5 because it can be used for mx50, mx51, and mx53.
 */
void mx5_cpu_lp_set(enum mxc_cpu_pwr_mode mode)
{
	u32 plat_lpc, arm_srpgcr, ccm_clpcr;
	u32 empgc0, empgc1;
	int stop_mode = 0;

	/* always allow platform to issue a deep sleep mode request */
	plat_lpc = __raw_readl(MXC_CORTEXA8_PLAT_LPC) &
	    ~(MXC_CORTEXA8_PLAT_LPC_DSM);
	ccm_clpcr = __raw_readl(MXC_CCM_CLPCR) & ~(MXC_CCM_CLPCR_LPM_MASK);
	arm_srpgcr = __raw_readl(MXC_SRPG_ARM_SRPGCR) & ~(MXC_SRPGCR_PCR);
	empgc0 = __raw_readl(MXC_SRPG_EMPGC0_SRPGCR) & ~(MXC_SRPGCR_PCR);
	empgc1 = __raw_readl(MXC_SRPG_EMPGC1_SRPGCR) & ~(MXC_SRPGCR_PCR);

	switch (mode) {
	case WAIT_CLOCKED:
		break;
	case WAIT_UNCLOCKED:
		ccm_clpcr |= 0x1 << MXC_CCM_CLPCR_LPM_OFFSET;
		break;
	case WAIT_UNCLOCKED_POWER_OFF:
	case STOP_POWER_OFF:
		plat_lpc |= MXC_CORTEXA8_PLAT_LPC_DSM
			    | MXC_CORTEXA8_PLAT_LPC_DBG_DSM;
		if (mode == WAIT_UNCLOCKED_POWER_OFF) {
			ccm_clpcr |= 0x1 << MXC_CCM_CLPCR_LPM_OFFSET;
			ccm_clpcr &= ~MXC_CCM_CLPCR_VSTBY;
			ccm_clpcr &= ~MXC_CCM_CLPCR_SBYOS;
			stop_mode = 0;
		} else {
			ccm_clpcr |= 0x2 << MXC_CCM_CLPCR_LPM_OFFSET;
			ccm_clpcr |= 0x3 << MXC_CCM_CLPCR_STBY_COUNT_OFFSET;
			ccm_clpcr |= MXC_CCM_CLPCR_VSTBY;
			ccm_clpcr |= MXC_CCM_CLPCR_SBYOS;
			stop_mode = 1;
		}
		arm_srpgcr |= MXC_SRPGCR_PCR;
		break;
	case STOP_POWER_ON:
		ccm_clpcr |= 0x2 << MXC_CCM_CLPCR_LPM_OFFSET;
		break;
	default:
		printk(KERN_WARNING "UNKNOWN cpu power mode: %d\n", mode);
		return;
	}

	__raw_writel(plat_lpc, MXC_CORTEXA8_PLAT_LPC);
	__raw_writel(ccm_clpcr, MXC_CCM_CLPCR);
	__raw_writel(arm_srpgcr, MXC_SRPG_ARM_SRPGCR);

	/* Enable NEON SRPG for all but MX50TO1.0. */
	if (mx50_revision() != IMX_CHIP_REVISION_1_0)
		__raw_writel(arm_srpgcr, MXC_SRPG_NEON_SRPGCR);

	if (stop_mode) {
		empgc0 |= MXC_SRPGCR_PCR;
		empgc1 |= MXC_SRPGCR_PCR;

		__raw_writel(empgc0, MXC_SRPG_EMPGC0_SRPGCR);
		__raw_writel(empgc1, MXC_SRPG_EMPGC1_SRPGCR);
	}
}

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
	if (!cpu_is_mx51() && !cpu_is_mx53())
		return 0;

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
