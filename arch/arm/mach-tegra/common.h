/*
 * Copyright (c) 2011, ARM Ltd.
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
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

#ifndef __MACH_TEGRA_COMMON_H
#define __MACH_TEGRA_COMMON_H

extern const struct smp_operations tegra_smp_ops;

extern int tegra_cpu_kill(unsigned int cpu);
extern void tegra_cpu_die(unsigned int cpu);

#endif
