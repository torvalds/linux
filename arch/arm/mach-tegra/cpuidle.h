/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012, NVIDIA Corporation. All rights reserved.
 */

#ifndef __MACH_TEGRA_CPUIDLE_H
#define __MACH_TEGRA_CPUIDLE_H

#ifdef CONFIG_CPU_IDLE
int tegra20_cpuidle_init(void);
void tegra20_cpuidle_pcie_irqs_in_use(void);
int tegra30_cpuidle_init(void);
int tegra114_cpuidle_init(void);
void tegra_cpuidle_init(void);
void tegra_cpuidle_pcie_irqs_in_use(void);
#else
static inline void tegra_cpuidle_init(void) {}
static inline void tegra_cpuidle_pcie_irqs_in_use(void) {}
#endif

#endif
