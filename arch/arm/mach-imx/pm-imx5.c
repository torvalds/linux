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
#include <linux/export.h>
#include <asm/cacheflush.h>
#include <asm/system_misc.h>
#include <asm/tlbflush.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define MXC_CCM_CLPCR			0x54
#define MXC_CCM_CLPCR_LPM_OFFSET	0
#define MXC_CCM_CLPCR_LPM_MASK		0x3
#define MXC_CCM_CLPCR_STBY_COUNT_OFFSET	9
#define MXC_CCM_CLPCR_VSTBY		(0x1 << 8)
#define MXC_CCM_CLPCR_SBYOS		(0x1 << 6)

#define MXC_CORTEXA8_PLAT_LPC		0xc
#define MXC_CORTEXA8_PLAT_LPC_DSM	(1 << 0)
#define MXC_CORTEXA8_PLAT_LPC_DBG_DSM	(1 << 1)

#define MXC_SRPG_NEON_SRPGCR		0x280
#define MXC_SRPG_ARM_SRPGCR		0x2a0
#define MXC_SRPG_EMPGC0_SRPGCR		0x2c0
#define MXC_SRPG_EMPGC1_SRPGCR		0x2d0

#define MXC_SRPGCR_PCR			1

/*
 * The WAIT_UNCLOCKED_POWER_OFF state only requires <= 500ns to exit.
 * This is also the lowest power state possible without affecting
 * non-cpu parts of the system.  For these reasons, imx5 should default
 * to always using this state for cpu idling.  The PM_SUSPEND_STANDBY also
 * uses this state and needs to take no action when registers remain confgiured
 * for this state.
 */
#define IMX5_DEFAULT_CPU_IDLE_STATE WAIT_UNCLOCKED_POWER_OFF

struct imx5_pm_data {
	phys_addr_t ccm_addr;
	phys_addr_t cortex_addr;
	phys_addr_t gpc_addr;
};

static const struct imx5_pm_data imx51_pm_data __initconst = {
	.ccm_addr = 0x73fd4000,
	.cortex_addr = 0x83fa0000,
	.gpc_addr = 0x73fd8000,
};

static const struct imx5_pm_data imx53_pm_data __initconst = {
	.ccm_addr = 0x53fd4000,
	.cortex_addr = 0x63fa0000,
	.gpc_addr = 0x53fd8000,
};

static void __iomem *ccm_base;
static void __iomem *cortex_base;
static void __iomem *gpc_base;

/*
 * set cpu low power mode before WFI instruction. This function is called
 * mx5 because it can be used for mx51, and mx53.
 */
static void mx5_cpu_lp_set(enum mxc_cpu_pwr_mode mode)
{
	u32 plat_lpc, arm_srpgcr, ccm_clpcr;
	u32 empgc0, empgc1;
	int stop_mode = 0;

	/* always allow platform to issue a deep sleep mode request */
	plat_lpc = __raw_readl(cortex_base + MXC_CORTEXA8_PLAT_LPC) &
	    ~(MXC_CORTEXA8_PLAT_LPC_DSM);
	ccm_clpcr = __raw_readl(ccm_base + MXC_CCM_CLPCR) &
		    ~(MXC_CCM_CLPCR_LPM_MASK);
	arm_srpgcr = __raw_readl(gpc_base + MXC_SRPG_ARM_SRPGCR) &
		     ~(MXC_SRPGCR_PCR);
	empgc0 = __raw_readl(gpc_base + MXC_SRPG_EMPGC0_SRPGCR) &
		 ~(MXC_SRPGCR_PCR);
	empgc1 = __raw_readl(gpc_base + MXC_SRPG_EMPGC1_SRPGCR) &
		 ~(MXC_SRPGCR_PCR);

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

	__raw_writel(plat_lpc, cortex_base + MXC_CORTEXA8_PLAT_LPC);
	__raw_writel(ccm_clpcr, ccm_base + MXC_CCM_CLPCR);
	__raw_writel(arm_srpgcr, gpc_base + MXC_SRPG_ARM_SRPGCR);
	__raw_writel(arm_srpgcr, gpc_base + MXC_SRPG_NEON_SRPGCR);

	if (stop_mode) {
		empgc0 |= MXC_SRPGCR_PCR;
		empgc1 |= MXC_SRPGCR_PCR;

		__raw_writel(empgc0, gpc_base + MXC_SRPG_EMPGC0_SRPGCR);
		__raw_writel(empgc1, gpc_base + MXC_SRPG_EMPGC1_SRPGCR);
	}
}

static int mx5_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		mx5_cpu_lp_set(STOP_POWER_OFF);
		break;
	case PM_SUSPEND_STANDBY:
		/* DEFAULT_IDLE_STATE already configured */
		break;
	default:
		return -EINVAL;
	}

	if (state == PM_SUSPEND_MEM) {
		local_flush_tlb_all();
		flush_cache_all();

		/*clear the EMPGC0/1 bits */
		__raw_writel(0, gpc_base + MXC_SRPG_EMPGC0_SRPGCR);
		__raw_writel(0, gpc_base + MXC_SRPG_EMPGC1_SRPGCR);
	}
	cpu_do_idle();

	/* return registers to default idle state */
	mx5_cpu_lp_set(IMX5_DEFAULT_CPU_IDLE_STATE);
	return 0;
}

static int mx5_pm_valid(suspend_state_t state)
{
	return (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX);
}

static const struct platform_suspend_ops mx5_suspend_ops = {
	.valid = mx5_pm_valid,
	.enter = mx5_suspend_enter,
};

static inline int imx5_cpu_do_idle(void)
{
	int ret = tzic_enable_wake();

	if (likely(!ret))
		cpu_do_idle();

	return ret;
}

static void imx5_pm_idle(void)
{
	imx5_cpu_do_idle();
}

static int __init imx5_pm_common_init(const struct imx5_pm_data *data)
{
	int ret;
	struct clk *gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs");

	if (IS_ERR(gpc_dvfs_clk))
		return PTR_ERR(gpc_dvfs_clk);

	ret = clk_prepare_enable(gpc_dvfs_clk);
	if (ret)
		return ret;

	arm_pm_idle = imx5_pm_idle;

	ccm_base = ioremap(data->ccm_addr, SZ_16K);
	cortex_base = ioremap(data->cortex_addr, SZ_16K);
	gpc_base = ioremap(data->gpc_addr, SZ_16K);
	WARN_ON(!ccm_base || !cortex_base || !gpc_base);

	/* Set the registers to the default cpu idle state. */
	mx5_cpu_lp_set(IMX5_DEFAULT_CPU_IDLE_STATE);

	ret = imx5_cpuidle_init();
	if (ret)
		pr_warn("%s: cpuidle init failed %d\n", __func__, ret);

	suspend_set_ops(&mx5_suspend_ops);

	return 0;
}

void __init imx51_pm_init(void)
{
	imx5_pm_common_init(&imx51_pm_data);
}

void __init imx53_pm_init(void)
{
	imx5_pm_common_init(&imx53_pm_data);
}
