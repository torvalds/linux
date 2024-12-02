// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 */

#include <linux/kernel.h>

#include "pm.h"

#ifdef CONFIG_PM_SLEEP
extern u32 tegra20_iram_start, tegra20_iram_end;
extern void tegra20_sleep_core_finish(unsigned long);

void tegra20_lp1_iram_hook(void)
{
	tegra_lp1_iram.start_addr = &tegra20_iram_start;
	tegra_lp1_iram.end_addr = &tegra20_iram_end;
}

void tegra20_sleep_core_init(void)
{
	tegra_sleep_core_finish = tegra20_sleep_core_finish;
}
#endif
