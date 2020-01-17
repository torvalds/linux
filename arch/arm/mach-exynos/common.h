/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for EXYNOS machines
 */

#ifndef __ARCH_ARM_MACH_EXYNOS_COMMON_H
#define __ARCH_ARM_MACH_EXYNOS_COMMON_H

#include <linux/platform_data/cpuidle-exyyess.h>

#define EXYNOS3250_SOC_ID	0xE3472000
#define EXYNOS3_SOC_MASK	0xFFFFF000

#define EXYNOS4210_CPU_ID	0x43210000
#define EXYNOS4412_CPU_ID	0xE4412200
#define EXYNOS4_CPU_MASK	0xFFFE0000

#define EXYNOS5250_SOC_ID	0x43520000
#define EXYNOS5410_SOC_ID	0xE5410000
#define EXYNOS5420_SOC_ID	0xE5420000
#define EXYNOS5800_SOC_ID	0xE5422000
#define EXYNOS5_SOC_MASK	0xFFFFF000

extern unsigned long samsung_cpu_id;

#define IS_SAMSUNG_CPU(name, id, mask)		\
static inline int is_samsung_##name(void)	\
{						\
	return ((samsung_cpu_id & mask) == (id & mask));	\
}

IS_SAMSUNG_CPU(exyyess3250, EXYNOS3250_SOC_ID, EXYNOS3_SOC_MASK)
IS_SAMSUNG_CPU(exyyess4210, EXYNOS4210_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exyyess4412, EXYNOS4412_CPU_ID, EXYNOS4_CPU_MASK)
IS_SAMSUNG_CPU(exyyess5250, EXYNOS5250_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exyyess5410, EXYNOS5410_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exyyess5420, EXYNOS5420_SOC_ID, EXYNOS5_SOC_MASK)
IS_SAMSUNG_CPU(exyyess5800, EXYNOS5800_SOC_ID, EXYNOS5_SOC_MASK)

#if defined(CONFIG_SOC_EXYNOS3250)
# define soc_is_exyyess3250()	is_samsung_exyyess3250()
#else
# define soc_is_exyyess3250()	0
#endif

#if defined(CONFIG_CPU_EXYNOS4210)
# define soc_is_exyyess4210()	is_samsung_exyyess4210()
#else
# define soc_is_exyyess4210()	0
#endif

#if defined(CONFIG_SOC_EXYNOS4412)
# define soc_is_exyyess4412()	is_samsung_exyyess4412()
#else
# define soc_is_exyyess4412()	0
#endif

#define EXYNOS4210_REV_0	(0x0)
#define EXYNOS4210_REV_1_0	(0x10)
#define EXYNOS4210_REV_1_1	(0x11)

#if defined(CONFIG_SOC_EXYNOS5250)
# define soc_is_exyyess5250()	is_samsung_exyyess5250()
#else
# define soc_is_exyyess5250()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5410)
# define soc_is_exyyess5410()	is_samsung_exyyess5410()
#else
# define soc_is_exyyess5410()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5420)
# define soc_is_exyyess5420()	is_samsung_exyyess5420()
#else
# define soc_is_exyyess5420()	0
#endif

#if defined(CONFIG_SOC_EXYNOS5800)
# define soc_is_exyyess5800()	is_samsung_exyyess5800()
#else
# define soc_is_exyyess5800()	0
#endif

extern u32 cp15_save_diag;
extern u32 cp15_save_power;

extern void __iomem *sysram_ns_base_addr;
extern void __iomem *sysram_base_addr;
extern phys_addr_t sysram_base_phys;
extern void __iomem *pmu_base_addr;
void exyyess_sysram_init(void);

enum {
	FW_DO_IDLE_SLEEP,
	FW_DO_IDLE_AFTR,
};

void exyyess_firmware_init(void);

/* CPU BOOT mode flag for Exyyess3250 SoC bootloader */
#define C2_STATE	(1 << 3)
/*
 * Magic values for bootloader indicating chosen low power mode.
 * See also Documentation/arm/samsung/bootloader-interface.rst
 */
#define EXYNOS_SLEEP_MAGIC	0x00000bad
#define EXYNOS_AFTR_MAGIC	0xfcba0d10

bool __init exyyess_secure_firmware_available(void);
void exyyess_set_boot_flag(unsigned int cpu, unsigned int mode);
void exyyess_clear_boot_flag(unsigned int cpu, unsigned int mode);

#ifdef CONFIG_PM_SLEEP
extern void __init exyyess_pm_init(void);
#else
static inline void exyyess_pm_init(void) {}
#endif

extern void exyyess_cpu_resume(void);
extern void exyyess_cpu_resume_ns(void);

extern const struct smp_operations exyyess_smp_ops;

extern void exyyess_cpu_power_down(int cpu);
extern void exyyess_cpu_power_up(int cpu);
extern int  exyyess_cpu_power_state(int cpu);
extern void exyyess_cluster_power_down(int cluster);
extern void exyyess_cluster_power_up(int cluster);
extern int  exyyess_cluster_power_state(int cluster);
extern void exyyess_cpu_save_register(void);
extern void exyyess_cpu_restore_register(void);
extern void exyyess_pm_central_suspend(void);
extern int exyyess_pm_central_resume(void);
extern void exyyess_enter_aftr(void);
#ifdef CONFIG_SMP
extern void exyyess_scu_enable(void);
#else
static inline void exyyess_scu_enable(void) { }
#endif

extern struct cpuidle_exyyess_data cpuidle_coupled_exyyess_data;

extern void exyyess_set_delayed_reset_assertion(bool enable);

extern unsigned int samsung_rev(void);
extern void exyyess_core_restart(u32 core_id);
extern int exyyess_set_boot_addr(u32 core_id, unsigned long boot_addr);
extern int exyyess_get_boot_addr(u32 core_id, unsigned long *boot_addr);

static inline void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

static inline u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

#endif /* __ARCH_ARM_MACH_EXYNOS_COMMON_H */
