/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011, ARM Ltd.
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 */

#ifndef __MACH_TEGRA_COMMON_H
#define __MACH_TEGRA_COMMON_H

extern const struct smp_operations tegra_smp_ops;

extern int tegra_cpu_kill(unsigned int cpu);
extern void tegra_cpu_die(unsigned int cpu);

#endif
