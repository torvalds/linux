/* linux/arch/arm/mach-s5pv210/pm.c
 *
 * Copyright (c) 2010-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV210 - Power Management support
 *
 * Based on arch/arm/mach-s3c2410/pm.c
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/suspend.h>

#include <plat/pm-common.h>

#include "common.h"
#include "regs-clock.h"

static struct sleep_save s5pv210_core_save[] = {
	/* Clock ETC */
	SAVE_ITEM(S5P_MDNIE_SEL),
};

/*
 * VIC wake-up support (TODO)
 */
static u32 s5pv210_irqwake_intmask = 0xffffffff;

/*
 * Suspend helpers.
 */
static int s5pv210_cpu_suspend(unsigned long arg)
{
	unsigned long tmp;

	/* issue the standby signal into the pm unit. Note, we
	 * issue a write-buffer drain just in case */

	tmp = 0;

	asm("b 1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    "mcr p15, 0, %0, c7, c10, 5\n\t"
	    "mcr p15, 0, %0, c7, c10, 4\n\t"
	    "wfi" : : "r" (tmp));

	pr_info("Failed to suspend the system\n");
	return 1; /* Aborting suspend */
}

static void s5pv210_pm_prepare(void)
{
	unsigned int tmp;

	/* Set wake-up mask registers */
	__raw_writel(exynos_get_eint_wake_mask(), S5P_EINT_WAKEUP_MASK);
	__raw_writel(s5pv210_irqwake_intmask, S5P_WAKEUP_MASK);

	/* ensure at least INFORM0 has the resume address */
	__raw_writel(virt_to_phys(s5pv210_cpu_resume), S5P_INFORM0);

	tmp = __raw_readl(S5P_SLEEP_CFG);
	tmp &= ~(S5P_SLEEP_CFG_OSC_EN | S5P_SLEEP_CFG_USBOSC_EN);
	__raw_writel(tmp, S5P_SLEEP_CFG);

	/* WFI for SLEEP mode configuration by SYSCON */
	tmp = __raw_readl(S5P_PWR_CFG);
	tmp &= S5P_CFG_WFI_CLEAN;
	tmp |= S5P_CFG_WFI_SLEEP;
	__raw_writel(tmp, S5P_PWR_CFG);

	/* SYSCON interrupt handling disable */
	tmp = __raw_readl(S5P_OTHERS);
	tmp |= S5P_OTHER_SYSC_INTOFF;
	__raw_writel(tmp, S5P_OTHERS);

	s3c_pm_do_save(s5pv210_core_save, ARRAY_SIZE(s5pv210_core_save));
}

/*
 * Suspend operations.
 */
static int s5pv210_suspend_enter(suspend_state_t state)
{
	int ret;

	s3c_pm_debug_init();

	S3C_PMDBG("%s: suspending the system...\n", __func__);

	S3C_PMDBG("%s: wakeup masks: %08x,%08x\n", __func__,
			s5pv210_irqwake_intmask, exynos_get_eint_wake_mask());

	if (s5pv210_irqwake_intmask == -1U
	    && exynos_get_eint_wake_mask() == -1U) {
		pr_err("%s: No wake-up sources!\n", __func__);
		pr_err("%s: Aborting sleep\n", __func__);
		return -EINVAL;
	}

	s3c_pm_save_uarts();
	s5pv210_pm_prepare();
	flush_cache_all();
	s3c_pm_check_store();

	ret = cpu_suspend(0, s5pv210_cpu_suspend);
	if (ret)
		return ret;

	s3c_pm_restore_uarts();

	S3C_PMDBG("%s: wakeup stat: %08x\n", __func__,
			__raw_readl(S5P_WAKEUP_STAT));

	s3c_pm_check_restore();

	S3C_PMDBG("%s: resuming the system...\n", __func__);

	return 0;
}

static int s5pv210_suspend_prepare(void)
{
	s3c_pm_check_prepare();

	return 0;
}

static void s5pv210_suspend_finish(void)
{
	s3c_pm_check_cleanup();
}

static const struct platform_suspend_ops s5pv210_suspend_ops = {
	.enter		= s5pv210_suspend_enter,
	.prepare	= s5pv210_suspend_prepare,
	.finish		= s5pv210_suspend_finish,
	.valid		= suspend_valid_only_mem,
};

/*
 * Syscore operations used to delay restore of certain registers.
 */
static void s5pv210_pm_resume(void)
{
	u32 tmp;

	tmp = __raw_readl(S5P_OTHERS);
	tmp |= (S5P_OTHERS_RET_IO | S5P_OTHERS_RET_CF |\
		S5P_OTHERS_RET_MMC | S5P_OTHERS_RET_UART);
	__raw_writel(tmp , S5P_OTHERS);

	s3c_pm_do_restore_core(s5pv210_core_save, ARRAY_SIZE(s5pv210_core_save));
}

static struct syscore_ops s5pv210_pm_syscore_ops = {
	.resume		= s5pv210_pm_resume,
};

/*
 * Initialization entry point.
 */
void __init s5pv210_pm_init(void)
{
	register_syscore_ops(&s5pv210_pm_syscore_ops);
	suspend_set_ops(&s5pv210_suspend_ops);
}
