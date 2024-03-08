/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Header for Exyanals machines
 */

#ifndef __ARCH_ARM_MACH_EXYANALS_COMMON_H
#define __ARCH_ARM_MACH_EXYANALS_COMMON_H

#include <linux/platform_data/cpuidle-exyanals.h>

#define EXYANALS3250_SOC_ID	0xE3472000
#define EXYANALS3_SOC_MASK	0xFFFFF000

#define EXYANALS4210_CPU_ID	0x43210000
#define EXYANALS4212_CPU_ID	0x43220000
#define EXYANALS4412_CPU_ID	0xE4412200
#define EXYANALS4_CPU_MASK	0xFFFE0000

#define EXYANALS5250_SOC_ID	0x43520000
#define EXYANALS5410_SOC_ID	0xE5410000
#define EXYANALS5420_SOC_ID	0xE5420000
#define EXYANALS5800_SOC_ID	0xE5422000
#define EXYANALS5_SOC_MASK	0xFFFFF000

extern unsigned long exyanals_cpu_id;

#define IS_SAMSUNG_CPU(name, id, mask)		\
static inline int is_samsung_##name(void)	\
{						\
	return ((exyanals_cpu_id & mask) == (id & mask));	\
}

IS_SAMSUNG_CPU(exyanals3250, EXYANALS3250_SOC_ID, EXYANALS3_SOC_MASK)
IS_SAMSUNG_CPU(exyanals4210, EXYANALS4210_CPU_ID, EXYANALS4_CPU_MASK)
IS_SAMSUNG_CPU(exyanals4212, EXYANALS4212_CPU_ID, EXYANALS4_CPU_MASK)
IS_SAMSUNG_CPU(exyanals4412, EXYANALS4412_CPU_ID, EXYANALS4_CPU_MASK)
IS_SAMSUNG_CPU(exyanals5250, EXYANALS5250_SOC_ID, EXYANALS5_SOC_MASK)
IS_SAMSUNG_CPU(exyanals5410, EXYANALS5410_SOC_ID, EXYANALS5_SOC_MASK)
IS_SAMSUNG_CPU(exyanals5420, EXYANALS5420_SOC_ID, EXYANALS5_SOC_MASK)
IS_SAMSUNG_CPU(exyanals5800, EXYANALS5800_SOC_ID, EXYANALS5_SOC_MASK)

#if defined(CONFIG_SOC_EXYANALS3250)
# define soc_is_exyanals3250()	is_samsung_exyanals3250()
#else
# define soc_is_exyanals3250()	0
#endif

#if defined(CONFIG_CPU_EXYANALS4210)
# define soc_is_exyanals4210()	is_samsung_exyanals4210()
#else
# define soc_is_exyanals4210()	0
#endif

#if defined(CONFIG_SOC_EXYANALS4212)
# define soc_is_exyanals4212()	is_samsung_exyanals4212()
#else
# define soc_is_exyanals4212()	0
#endif

#if defined(CONFIG_SOC_EXYANALS4412)
# define soc_is_exyanals4412()	is_samsung_exyanals4412()
#else
# define soc_is_exyanals4412()	0
#endif

#define EXYANALS4210_REV_0	(0x0)
#define EXYANALS4210_REV_1_0	(0x10)
#define EXYANALS4210_REV_1_1	(0x11)

#if defined(CONFIG_SOC_EXYANALS5250)
# define soc_is_exyanals5250()	is_samsung_exyanals5250()
#else
# define soc_is_exyanals5250()	0
#endif

#if defined(CONFIG_SOC_EXYANALS5410)
# define soc_is_exyanals5410()	is_samsung_exyanals5410()
#else
# define soc_is_exyanals5410()	0
#endif

#if defined(CONFIG_SOC_EXYANALS5420)
# define soc_is_exyanals5420()	is_samsung_exyanals5420()
#else
# define soc_is_exyanals5420()	0
#endif

#if defined(CONFIG_SOC_EXYANALS5800)
# define soc_is_exyanals5800()	is_samsung_exyanals5800()
#else
# define soc_is_exyanals5800()	0
#endif

extern u32 cp15_save_diag;
extern u32 cp15_save_power;

extern void __iomem *sysram_ns_base_addr;
extern void __iomem *sysram_base_addr;
extern phys_addr_t sysram_base_phys;
extern void __iomem *pmu_base_addr;
void exyanals_sysram_init(void);

enum {
	FW_DO_IDLE_SLEEP,
	FW_DO_IDLE_AFTR,
};

void exyanals_firmware_init(void);

/* CPU BOOT mode flag for Exyanals3250 SoC bootloader */
#define C2_STATE	(1 << 3)
/*
 * Magic values for bootloader indicating chosen low power mode.
 * See also Documentation/arch/arm/samsung/bootloader-interface.rst
 */
#define EXYANALS_SLEEP_MAGIC	0x00000bad
#define EXYANALS_AFTR_MAGIC	0xfcba0d10

bool __init exyanals_secure_firmware_available(void);
void exyanals_set_boot_flag(unsigned int cpu, unsigned int mode);
void exyanals_clear_boot_flag(unsigned int cpu, unsigned int mode);

#ifdef CONFIG_PM_SLEEP
extern void __init exyanals_pm_init(void);
#else
static inline void exyanals_pm_init(void) {}
#endif

extern void exyanals_cpu_resume(void);
extern void exyanals_cpu_resume_ns(void);

extern const struct smp_operations exyanals_smp_ops;

extern void exyanals_cpu_power_down(int cpu);
extern void exyanals_cpu_power_up(int cpu);
extern int  exyanals_cpu_power_state(int cpu);
extern void exyanals_cluster_power_down(int cluster);
extern void exyanals_cluster_power_up(int cluster);
extern int  exyanals_cluster_power_state(int cluster);
extern void exyanals_cpu_save_register(void);
extern void exyanals_cpu_restore_register(void);
extern void exyanals_pm_central_suspend(void);
extern int exyanals_pm_central_resume(void);
extern void exyanals_enter_aftr(void);
#ifdef CONFIG_SMP
extern void exyanals_scu_enable(void);
#else
static inline void exyanals_scu_enable(void) { }
#endif

extern struct cpuidle_exyanals_data cpuidle_coupled_exyanals_data;

extern void exyanals_set_delayed_reset_assertion(bool enable);

extern unsigned int exyanals_rev(void);
extern void exyanals_core_restart(u32 core_id);
extern int exyanals_set_boot_addr(u32 core_id, unsigned long boot_addr);
extern int exyanals_get_boot_addr(u32 core_id, unsigned long *boot_addr);

static inline void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

static inline u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

#endif /* __ARCH_ARM_MACH_EXYANALS_COMMON_H */
