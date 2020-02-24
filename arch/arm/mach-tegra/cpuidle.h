/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012, NVIDIA Corporation. All rights reserved.
 */

#ifndef __MACH_TEGRA_CPUIDLE_H
#define __MACH_TEGRA_CPUIDLE_H

#ifdef CONFIG_CPU_IDLE
int tegra30_cpuidle_init(void);
int tegra114_cpuidle_init(void);
void tegra_cpuidle_init(void);
#else
static inline void tegra_cpuidle_init(void) {}
#endif

#endif
