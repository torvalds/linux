/*
 * arch/arm/mach-tegra/reset.h
 *
 * CPU reset dispatcher.
 *
 * Copyright (c) 2011, NVIDIA Corporation.
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

#ifndef __MACH_TEGRA_RESET_H
#define __MACH_TEGRA_RESET_H

#define TEGRA_RESET_MASK_PRESENT	0
#define TEGRA_RESET_MASK_LP1		1
#define TEGRA_RESET_MASK_LP2		2
#define TEGRA_RESET_STARTUP_SECONDARY	3
#define TEGRA_RESET_STARTUP_LP2		4
#define TEGRA_RESET_STARTUP_LP1		5
#define TEGRA_RESET_RESETTABLE_STATUS	6
#define TEGRA_RESET_TF_PRESENT		7
#define TEGRA_RESET_DATA_SIZE		8

#define RESET_DATA(x)	((TEGRA_RESET_##x)*4)

#ifndef __ASSEMBLY__

#include "irammap.h"

extern unsigned long __tegra_cpu_reset_handler_data[TEGRA_RESET_DATA_SIZE];

void __tegra_cpu_reset_handler_start(void);
void __tegra_cpu_reset_handler(void);
void __tegra20_cpu1_resettable_status_offset(void);
void __tegra_cpu_reset_handler_end(void);

#ifdef CONFIG_PM_SLEEP
#define tegra_cpu_lp1_mask \
	(IO_ADDRESS(TEGRA_IRAM_BASE + TEGRA_IRAM_RESET_HANDLER_OFFSET + \
	((u32)&__tegra_cpu_reset_handler_data[TEGRA_RESET_MASK_LP1] - \
	 (u32)__tegra_cpu_reset_handler_start)))
#define tegra_cpu_lp2_mask \
	(IO_ADDRESS(TEGRA_IRAM_BASE + TEGRA_IRAM_RESET_HANDLER_OFFSET + \
	((u32)&__tegra_cpu_reset_handler_data[TEGRA_RESET_MASK_LP2] - \
	 (u32)__tegra_cpu_reset_handler_start)))
#define tegra20_cpu1_resettable_status \
	(IO_ADDRESS(TEGRA_IRAM_BASE + TEGRA_IRAM_RESET_HANDLER_OFFSET + \
	((u32)&__tegra_cpu_reset_handler_data[TEGRA_RESET_RESETTABLE_STATUS] - \
	 (u32)__tegra_cpu_reset_handler_start)))
#endif

#define tegra_cpu_reset_handler_offset \
		((u32)__tegra_cpu_reset_handler - \
		 (u32)__tegra_cpu_reset_handler_start)

#define tegra_cpu_reset_handler_size \
		(__tegra_cpu_reset_handler_end - \
		 __tegra_cpu_reset_handler_start)

void __init tegra_cpu_reset_handler_init(void);

#endif
#endif
