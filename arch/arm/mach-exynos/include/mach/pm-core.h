/* linux/arch/arm/mach-exynos4/include/mach/pm-core.h
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

#ifndef __ASM_ARCH_PM_CORE_H
#define __ASM_ARCH_PM_CORE_H __FILE__

#include <linux/of.h>
#include <mach/map.h>

#define S5P_EINT_WAKEUP_MASK			(S5P_VA_PMU + 0x0604)
#define S5P_WAKEUP_MASK				(S5P_VA_PMU + 0x0608)

#ifdef CONFIG_PINCTRL_EXYNOS
extern u32 exynos_get_eint_wake_mask(void);
#else
static inline u32 exynos_get_eint_wake_mask(void) { return 0xffffffff; }
#endif

static inline void s3c_pm_debug_init_uart(void)
{
	/* nothing here yet */
}

static inline void s3c_pm_arch_prepare_irqs(void)
{
	__raw_writel(exynos_get_eint_wake_mask(), S5P_EINT_WAKEUP_MASK);
	__raw_writel(s3c_irqwake_intmask & ~(1 << 31), S5P_WAKEUP_MASK);
}

static inline void s3c_pm_arch_stop_clocks(void)
{
	/* nothing here yet */
}

static inline void s3c_pm_arch_show_resume_irqs(void)
{
	/* nothing here yet */
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

static inline void samsung_pm_saved_gpios(void)
{
	/* nothing here yet */
}

/* Compatibility definitions to make plat-samsung/pm.c compile */
#define IRQ_EINT_BIT(x)		1
#define s3c_irqwake_intallow	0
#define s3c_irqwake_eintallow	0

#endif /* __ASM_ARCH_PM_CORE_H */
