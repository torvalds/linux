/* arch/arm/mach-rk29/include/mach/cpufreq.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_RK29_CPUFREQ_H
#define __ASM_ARCH_RK29_CPUFREQ_H

#include <linux/cpufreq.h>

/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
#define CPUFREQ_FORCE_CHANGE            0x40

#ifdef CONFIG_CPU_FREQ
int board_update_cpufreq_table(struct cpufreq_frequency_table *table);
#else
static inline int board_update_cpufreq_table(struct cpufreq_frequency_table *table) { return 0; }
#endif

#endif
