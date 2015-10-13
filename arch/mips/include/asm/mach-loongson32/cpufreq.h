/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 CPUFreq platform support.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
