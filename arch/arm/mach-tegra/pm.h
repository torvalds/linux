/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2012 NVIDIA Corporation. All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 */

#ifndef _MACH_TEGRA_PM_H_
#define _MACH_TEGRA_PM_H_

struct tegra_lp1_iram {
	void	*start_addr;
	void	*end_addr;
};

extern struct tegra_lp1_iram tegra_lp1_iram;
extern void (*tegra_sleep_core_finish)(unsigned long v2p);

void tegra20_lp1_iram_hook(void);
void tegra20_sleep_core_init(void);
void tegra30_lp1_iram_hook(void);
void tegra30_sleep_core_init(void);

extern void (*tegra_tear_down_cpu)(void);

#ifdef CONFIG_PM_SLEEP
void tegra_init_suspend(void);
#else
static inline void tegra_init_suspend(void) {}
#endif

#endif /* _MACH_TEGRA_PM_H_ */
