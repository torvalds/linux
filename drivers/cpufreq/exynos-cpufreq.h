/*
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

#define APLL_FREQ(f, a0, a1, a2, a3, a4, a5, a6, a7, b0, b1, b2, m, p, s) \
	{ \
		.freq = (f) * 1000, \
		.clk_div_cpu0 = ((a0) | (a1) << 4 | (a2) << 8 | (a3) << 12 | \
			(a4) << 16 | (a5) << 20 | (a6) << 24 | (a7) << 28), \
		.clk_div_cpu1 = (b0 << 0 | b1 << 4 | b2 << 8), \
		.mps = ((m) << 16 | (p) << 8 | (s)), \
	}

struct apll_freq {
	unsigned int freq;
	u32 clk_div_cpu0;
	u32 clk_div_cpu1;
	u32 mps;
};

struct exynos_dvfs_info {
	unsigned long	mpll_freq_khz;
	unsigned int	pll_safe_idx;
	struct clk	*cpu_clk;
	unsigned int	*volt_table;
	struct cpufreq_frequency_table	*freq_table;
	void (*set_freq)(unsigned int, unsigned int);
	bool (*need_apll_change)(unsigned int, unsigned int);
};

#ifdef CONFIG_ARM_EXYNOS4210_CPUFREQ
extern int exynos4210_cpufreq_init(struct exynos_dvfs_info *);
#else
static inline int exynos4210_cpufreq_init(struct exynos_dvfs_info *info)
{
	return -EOPNOTSUPP;
}
#endif
#ifdef CONFIG_ARM_EXYNOS4X12_CPUFREQ
extern int exynos4x12_cpufreq_init(struct exynos_dvfs_info *);
#else
static inline int exynos4x12_cpufreq_init(struct exynos_dvfs_info *info)
{
	return -EOPNOTSUPP;
}
#endif
#ifdef CONFIG_ARM_EXYNOS5250_CPUFREQ
extern int exynos5250_cpufreq_init(struct exynos_dvfs_info *);
#else
static inline int exynos5250_cpufreq_init(struct exynos_dvfs_info *info)
{
	return -EOPNOTSUPP;
}
#endif

#include <plat/cpu.h>
#include <mach/map.h>

#define EXYNOS4_CLKSRC_CPU			(S5P_VA_CMU + 0x14200)
#define EXYNOS4_CLKMUX_STATCPU			(S5P_VA_CMU + 0x14400)

#define EXYNOS4_CLKDIV_CPU			(S5P_VA_CMU + 0x14500)
#define EXYNOS4_CLKDIV_CPU1			(S5P_VA_CMU + 0x14504)
#define EXYNOS4_CLKDIV_STATCPU			(S5P_VA_CMU + 0x14600)
#define EXYNOS4_CLKDIV_STATCPU1			(S5P_VA_CMU + 0x14604)

#define EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT	(16)
#define EXYNOS4_CLKMUX_STATCPU_MUXCORE_MASK	(0x7 << EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT)

#define EXYNOS5_APLL_LOCK			(S5P_VA_CMU + 0x00000)
#define EXYNOS5_APLL_CON0			(S5P_VA_CMU + 0x00100)
#define EXYNOS5_CLKMUX_STATCPU			(S5P_VA_CMU + 0x00400)
#define EXYNOS5_CLKDIV_CPU0			(S5P_VA_CMU + 0x00500)
#define EXYNOS5_CLKDIV_CPU1			(S5P_VA_CMU + 0x00504)
#define EXYNOS5_CLKDIV_STATCPU0			(S5P_VA_CMU + 0x00600)
#define EXYNOS5_CLKDIV_STATCPU1			(S5P_VA_CMU + 0x00604)
