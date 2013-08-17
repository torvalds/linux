/* linux/arch/arm/mach-exynos/include/mach/cpufreq.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - CPUFreq support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

enum cpufreq_level_index {
	L0, L1, L2, L3, L4,
	L5, L6, L7, L8, L9,
	L10, L11, L12, L13, L14,
	L15, L16, L17, L18, L19,
	L20,
};

struct exynos_dvfs_info {
	unsigned long	mpll_freq_khz;
	unsigned int	pll_safe_idx;
	unsigned int	pm_lock_idx;
	unsigned int	max_support_idx;
	unsigned int	min_support_idx;
	unsigned int	cluster_num;
	unsigned int	boot_freq;
	bool		blocked;
	struct clk	*cpu_clk;
	unsigned int	*volt_table;
	const unsigned int	*max_op_freqs;
	struct cpufreq_frequency_table	*freq_table;
	struct regulator *regulator;
	void (*set_freq)(unsigned int, unsigned int);
	void (*set_ema)(unsigned int);
	bool (*need_apll_change)(unsigned int, unsigned int);
};

#if defined(CONFIG_ARCH_EXYNOS4)
extern int exynos4210_cpufreq_init(struct exynos_dvfs_info *);
extern int exynos4x12_cpufreq_init(struct exynos_dvfs_info *);
static inline int exynos5250_cpufreq_init(struct exynos_dvfs_info *info)
{
	return 0;
}

static inline int exynos5410_cpufreq_CA7_init(struct exynos_dvfs_info *info)
{
	return 0;
}

static inline int exynos5410_cpufreq_CA15_init(struct exynos_dvfs_info *info)
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
extern int exynos5410_cpufreq_CA7_init(struct exynos_dvfs_info *);
extern int exynos5410_cpufreq_CA15_init(struct exynos_dvfs_info *);
#else
	#warning "Should define CONFIG_ARCH_EXYNOS4(5)\n"
#endif
extern void exynos_thermal_throttle(void);
extern void exynos_thermal_unthrottle(void);

#if defined(CONFIG_SOC_EXYNOS5410)
/*
 * CPU usage threshold value to determine changing from b to L
 * Assumption: A15(500MHz min), A7(1GHz max) has almost same performance
 * in DMIPS. If a A15 is working at minimum DVFS level and current cpu usage
 * is less than b_to_L_threshold, try to change this A15 to A7.
 */
#define DMIPS_A15	35	/* 3.5 * 10 (factor) */
#define DMIPS_A7	19	/* 1.9 * 10 (factor) */

/*
 * Performance factor A15 vs A7 at same frequency
 * DMIPS_A15/DMIPS_A7 * 10 = 35 / 19 * 10
 */
#define PERF_FACTOR		18

#define b_to_L_threshold	80

typedef enum {
	CA7,
	CA15,
	CA_END,
} cluster_type;

enum op_state {
	NORMAL,		/* Operation : Normal */
	SUSPEND,	/* Direct API will be blocked in this state */
	RESUME,		/* Re-enabling DVFS using direct API after resume */
};

/*
 * Keep frequency value for counterpart cluster DVFS
 * cur, min, max : Frequency (KHz),
 * c_id : Counter cluster with booting cluster, if booting cluster is
 * A15, c_id will be A7.
 */
struct cpu_info_alter {
	unsigned int cur;
	unsigned int min;
	unsigned int max;
	cluster_type boot_cluster;
	cluster_type c_id;
};

extern unsigned int exynos_cpufreq_direct_scale(unsigned int target_freq,
						unsigned int curr_freq,
						enum op_state state);
extern int exynos_init_bL_info(struct cpu_info_alter *info);
#if defined(CONFIG_ARM_EXYNOS_IKS_CPUFREQ) || defined(CONFIG_ARM_EXYNOS_CPUFREQ)
extern void exynos_lowpower_for_cluster(cluster_type cluster, bool on);
extern void reset_lpj_for_cluster(cluster_type cluster);
extern struct pm_qos_request max_cpu_qos_blank;
#else
static inline void reset_lpj_for_cluster(cluster_type cluster) {}
static inline void exynos_lowpower_for_cluster(cluster_type cluster, bool on) {}
#endif
#endif
