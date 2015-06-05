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

#include <linux/of.h>
#include <linux/platform_data/cpuidle-exynos.h>

#define EXYNOS3250_SOC_ID	0xE3472000
#define EXYNOS3_SOC_MASK	0xFFFFF000

#define EXYNOS4210_CPU_ID	0x43210000
#define EXYNOS4212_CPU_ID	0x43220000
#define EXYNOS4412_CPU_ID	0xE4412200
#define EXYNOS4_CPU_MASK	0xFFFE0000

#define EXYNOS5250_SOC_ID	0x43520000
#define EXYNOS5410_SOC_ID	0xE5410000
#define EXYNOS5420_SOC_ID	0xE5420000
#define EXYNOS5440_SOC_ID	0xE5440000
#define EXYNOS5800_SOC_ID	0xE5422000
#define EXYNOS5_SOC_MASK	0xFFFFF000

extern unsigned long samsung_cpu_id;

#define IS_SAMSUNG_CPU(name, id, mask)		\
static inline int is_samsung_##name(void)	\
{						\
	return ((samsung_cpu_id & mask) == (id & mask));	\
}

IS_SAMSUNG_CPU(exynos3250, EXYNOS3250_SOC_ID, EXYNOS3_SOC_MASK)
IS_SAMSUNG_CPU(exynos4210, EXYNOS4210_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos4212, EXYNOS4212_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos4412, EXYNOS4412_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exynos5250, EXYNOS5250_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5410, EXYNOS5410_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5420, EXYNOS5420_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5440, EXYNOS5440_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exynos5800, EXYNOS5800_SOC_ID, EXYNOS5_SOC_MASK)

#if defined(CONFIG_SOC_EXYNOS3250)
# define soc_is_exynos3250()	is_samsung_exynos3250()
#else
# define soc_is_exynos3250()	0
#endif

#if defined(CONFIG_CPU_EXYNOS4210)
# define soc_is_exynos4210()	is_samsung_exynos4210()
#else
# define soc_is_exynos4210()	0
#endif

#if defined(CONFIG_SOC_EXYNOS4212)
# define soc_is_exynos4212()	is_samsung_exynos4212()
#else
# define soc_is_exynos4212()	0
#endif

#if defined(CONFIG_SOC_EXYNOS4412)
# define soc_is_exynos4412()	is_samsung_exynos4412()
#else
# define soc_is_exynos4412()	0
#endif

#define EXYNOS4210_REV_0	(0x0)
#define EXYNOS4210_REV_1_0	(0x10)
#define EXYNOS4210_REV_1_1	(0x11)

#if defined(CONFIG_SOC_EXYNOS5250)
# define soc_is_exynos5250()	is_samsung_exynos5250()
#else
# define soc_is_exynos5250()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5410)
# define soc_is_exynos5410()	is_samsung_exynos5410()
#else
# define soc_is_exynos5410()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5420)
# define soc_is_exynos5420()	is_samsung_exynos5420()
#else
# define soc_is_exynos5420()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5440)
# define soc_is_exynos5440()	is_samsung_exynos5440()
#else
# define soc_is_exynos5440()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5800)
# define soc_is_exynos5800()	is_samsung_exynos5800()
#else
# define soc_is_exynos5800()	0
#endif

#define soc_is_exynos4() (soc_is_exynos4210() || soc_is_exynos4212() || \
			  soc_is_exynos4412())
#define soc_is_exynos5() (soc_is_exynos5250() || soc_is_exynos5410() || \
			  soc_is_exynos5420() || soc_is_exynos5800())

extern u32 cp15_save_diag;
extern u32 cp15_save_power;

extern void __iomem *sysram_ns_base_addr;
extern void __iomem *sysram_base_addr;
extern void __iomem *pmu_base_addr;
void exynos_sysram_init(void);

enum {
	FW_DO_IDLE_SLEEP,
	FW_DO_IDLE_AFTR,
};

void exynos_firmware_init(void);

/* CPU BOOT mode flag for Exynos3250 SoC bootloader */
#define C2_STATE	(1 << 3)

void exynos_set_boot_flag(unsigned int cpu, unsigned int mode);
void exynos_clear_boot_flag(unsigned int cpu, unsigned int mode);

extern u32 exynos_get_eint_wake_mask(void);

#ifdef CONFIG_PM_SLEEP
extern void __init exynos_pm_init(void);
#else
static inline void exynos_pm_init(void) {}
#endif

extern void exynos_cpu_resume(void);
extern void exynos_cpu_resume_ns(void);

extern struct smp_operations exynos_smp_ops;

extern void exynos_cpu_power_down(int cpu);
extern void exynos_cpu_power_up(int cpu);
extern int  exynos_cpu_power_state(int cpu);
extern void exynos_cluster_power_down(int cluster);
extern void exynos_cluster_power_up(int cluster);
extern int  exynos_cluster_power_state(int cluster);
extern void exynos_cpu_save_register(void);
extern void exynos_cpu_restore_register(void);
extern void exynos_pm_central_suspend(void);
extern int exynos_pm_central_resume(void);
extern void exynos_enter_aftr(void);

extern struct cpuidle_exynos_data cpuidle_coupled_exynos_data;

extern void exynos_set_delayed_reset_assertion(bool enable);

extern void s5p_init_cpu(void __iomem *cpuid_addr);
extern unsigned int samsung_rev(void);
extern void __iomem *cpu_boot_reg_base(void);

static inline void pmu_raw_writel(u32 val, u32 offset)
{
	__raw_writel(val, pmu_base_addr + offset);
}

static inline u32 pmu_raw_readl(u32 offset)
{
	return __raw_readl(pmu_base_addr + offset);
}

#endif /* __ARCH_ARM_MACH_EXYNOS_COMMON_H */
