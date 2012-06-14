/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for EXYNOS machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_EXYNOS_COMMON_H
#define __ARCH_ARM_MACH_EXYNOS_COMMON_H

extern struct sys_timer exynos4_timer;

void exynos_init_io(struct map_desc *mach_desc, int size);
void exynos4_init_irq(void);
void exynos5_init_irq(void);
void exynos4_restart(char mode, const char *cmd);
void exynos5_restart(char mode, const char *cmd);
void exynos_init_late(void);

#ifdef CONFIG_PM_GENERIC_DOMAINS
int exynos_pm_late_initcall(void);
#else
static int exynos_pm_late_initcall(void) { return 0; }
#endif

#ifdef CONFIG_ARCH_EXYNOS4
void exynos4_register_clocks(void);
void exynos4_setup_clocks(void);

#else
#define exynos4_register_clocks()
#define exynos4_setup_clocks()
#endif

#ifdef CONFIG_ARCH_EXYNOS5
void exynos5_register_clocks(void);
void exynos5_setup_clocks(void);

#else
#define exynos5_register_clocks()
#define exynos5_setup_clocks()
#endif

#ifdef CONFIG_CPU_EXYNOS4210
void exynos4210_register_clocks(void);

#else
#define exynos4210_register_clocks()
#endif

#ifdef CONFIG_SOC_EXYNOS4212
void exynos4212_register_clocks(void);

#else
#define exynos4212_register_clocks()
#endif

#endif /* __ARCH_ARM_MACH_EXYNOS_COMMON_H */
