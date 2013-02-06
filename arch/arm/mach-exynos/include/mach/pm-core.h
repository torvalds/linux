/* linux/arch/arm/mach-exynos/include/mach/pm-core.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on arch/arm/mach-s3c2410/include/mach/pm-core.h,
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * EXYNOS4210 - PM core support for arch/arm/plat-s5p/pm.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <mach/regs-pmu.h>
#include <mach/regs-gpio.h>

static inline void s3c_pm_debug_init_uart(void)
{
	/* nothing here yet */
}

static inline void s3c_pm_arch_prepare_irqs(void)
{
#if defined(CONFIG_EXYNOS4212) || defined(CONFIG_EXYNOS4412)
       /* Mask externel GIC and GPS_ALIVE wakeup source */
       s3c_irqwake_intmask |= 0x3BF0000;
#endif
	__raw_writel((s3c_irqwake_intmask & S5P_WAKEUP_MASK_BIT), S5P_WAKEUP_MASK);
	__raw_writel(s3c_irqwake_eintmask, S5P_EINT_WAKEUP_MASK);
}

static inline void s3c_pm_arch_stop_clocks(void)
{
	/* nothing here yet */
}

static inline void s3c_pm_arch_show_resume_irqs(void)
{
#if defined(CONFIG_CPU_EXYNOS4210) || defined(CONFIG_CPU_EXYNOS4412)\
	|| defined(CONFIG_CPU_EXYNOS5250)
	pr_info("WAKEUP_STAT: 0x%x\n", __raw_readl(S5P_WAKEUP_STAT));
	pr_info("WAKEUP_INTx_PEND: 0x%x, 0x%x, 0x%x, 0x%x\n",
				__raw_readl(S5P_EINT_PEND(0)),
				__raw_readl(S5P_EINT_PEND(1)),
				__raw_readl(S5P_EINT_PEND(2)),
				__raw_readl(S5P_EINT_PEND(3)));
#endif
}

static inline void s3c_pm_arch_update_uart(void __iomem *regs,
					   struct pm_uart_save *save)
{
	/* nothing here yet */
}

static inline void s3c_pm_restored_gpios(void)
{
	/* nothing here yet */
}

static inline void s3c_pm_saved_gpios(void)
{
	/* nothing here yet */
}
