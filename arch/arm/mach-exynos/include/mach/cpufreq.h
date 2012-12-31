/* linux/arch/arm/mach-exynos/include/mach/cpufreq.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - CPUFreq support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* CPU frequency level index for using cpufreq lock API
 * This should be same with cpufreq_frequency_table
*/

enum cpufreq_level_index {
	L0, L1, L2, L3, L4,
	L5, L6, L7, L8, L9,
	L10, L11, L12, L13, L14,
	L15, L16, L17, L18, L19,
	L20,
};

enum busfreq_level_request {
	BUS_L0,		/* MEM 400MHz BUS 200MHz */
	BUS_L1,		/* MEM 267MHz BUS 160MHz */
	BUS_L2,		/* MEM 133MHz BUS 133MHz */
	BUS_LEVEL_END,
};

enum cpufreq_lock_ID {
	DVFS_LOCK_ID_G2D,	/* G2D */
	DVFS_LOCK_ID_TV,	/* TV */
	DVFS_LOCK_ID_MFC,	/* MFC */
	DVFS_LOCK_ID_USB,	/* USB */
	DVFS_LOCK_ID_CAM,	/* CAM */
	DVFS_LOCK_ID_PM,	/* PM */
	DVFS_LOCK_ID_USER,	/* USER */
	DVFS_LOCK_ID_TMU,	/* TMU */
	DVFS_LOCK_ID_LPA,	/* LPA */
	DVFS_LOCK_ID_DRM,	/* DRM */
	DVFS_LOCK_ID_G3D,	/* G3D */
	DVFS_LOCK_ID_END,
};

int exynos_cpufreq_get_level(unsigned int freq,
			unsigned int *level);
int exynos_find_cpufreq_level_by_volt(unsigned int arm_volt,
			unsigned int *level);
int exynos_cpufreq_lock(unsigned int nId,
			enum cpufreq_level_index cpufreq_level);
void exynos_cpufreq_lock_free(unsigned int nId);

int exynos4_busfreq_lock(unsigned int nId,
			enum busfreq_level_request busfreq_level);
void exynos4_busfreq_lock_free(unsigned int nId);

int exynos_cpufreq_upper_limit(unsigned int nId,
			enum cpufreq_level_index cpufreq_level);
void exynos_cpufreq_upper_limit_free(unsigned int nId);

/*
 * This level fix API set highset priority level lock.
 * Please use this carefully, with other lock API
 */
int exynos_cpufreq_level_fix(unsigned int freq);
void exynos_cpufreq_level_unfix(void);
int exynos_cpufreq_is_fixed(void);

#define MAX_INDEX	10

struct exynos_dvfs_info {
	unsigned long	mpll_freq_khz;
	unsigned int	pll_safe_idx;
	unsigned int	pm_lock_idx;
	unsigned int	max_support_idx;
	unsigned int	min_support_idx;
	struct clk	*cpu_clk;
	unsigned int	*volt_table;
	struct cpufreq_frequency_table	*freq_table;
	void (*set_freq)(unsigned int, unsigned int);
	bool (*need_apll_change)(unsigned int, unsigned int);
};

#define SUPPORT_1400MHZ	(1<<31)
#define SUPPORT_1200MHZ	(1<<30)
#define SUPPORT_1000MHZ	(1<<29)
#define SUPPORT_FREQ_SHIFT	29
#define SUPPORT_FREQ_MASK	7

#if defined(CONFIG_ARCH_EXYNOS4)
extern int exynos4210_cpufreq_init(struct exynos_dvfs_info *);
extern int exynos4x12_cpufreq_init(struct exynos_dvfs_info *);
static inline int exynos5250_cpufreq_init(struct exynos_dvfs_info *info)
{
	return 0;
}

#elif defined(CONFIG_ARCH_EXYNOS5)
static inline int exynos4210_cpufreq_init(struct exynos_dvfs_info *info)
{
	return 0;
}

static inline int exynos4x12_cpufreq_init(struct exynos_dvfs_info *info)
{
	return 0;
}

extern int exynos5250_cpufreq_init(struct exynos_dvfs_info *);
#else
	#warning "Should define CONFIG_ARCH_EXYNOS4(5)\n"
#endif

#if defined(CONFIG_EXYNOS5250_ABB_WA)
/* These function and variables should be removed in EVT1 */
void exynos5250_set_arm_abbg(unsigned int arm_volt, unsigned int int_volt);
#endif
