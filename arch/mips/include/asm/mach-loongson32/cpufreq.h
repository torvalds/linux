/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 CPUFreq platform support.
 */

#ifndef __ASM_MACH_LOONGSON32_CPUFREQ_H
#define __ASM_MACH_LOONGSON32_CPUFREQ_H

struct plat_ls1x_cpufreq {
	const char	*clk_name;	/* CPU clk */
	const char	*osc_clk_name;	/* OSC clk */
	unsigned int	max_freq;	/* in kHz */
	unsigned int	min_freq;	/* in kHz */
};

#endif /* __ASM_MACH_LOONGSON32_CPUFREQ_H */
