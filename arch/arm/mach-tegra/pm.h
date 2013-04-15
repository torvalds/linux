/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2012 NVIDIA Corporation. All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MACH_TEGRA_PM_H_
#define _MACH_TEGRA_PM_H_

#include "pmc.h"

extern unsigned long l2x0_saved_regs_addr;

void save_cpu_arch_register(void);
void restore_cpu_arch_register(void);

void tegra_clear_cpu_in_lp2(int phy_cpu_id);
bool tegra_set_cpu_in_lp2(int phy_cpu_id);

void tegra_idle_lp2_last(void);
extern void (*tegra_tear_down_cpu)(void);

#ifdef CONFIG_PM_SLEEP
enum tegra_suspend_mode tegra_pm_validate_suspend_mode(
				enum tegra_suspend_mode mode);
void tegra_init_suspend(void);
#else
static inline enum tegra_suspend_mode tegra_pm_validate_suspend_mode(
				enum tegra_suspend_mode mode)
{
	return TEGRA_SUSPEND_NONE;
}
static inline void tegra_init_suspend(void) {}
#endif

#endif /* _MACH_TEGRA_PM_H_ */
