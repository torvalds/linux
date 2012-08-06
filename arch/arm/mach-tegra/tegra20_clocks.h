/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __MACH_TEGRA20_CLOCK_H
#define __MACH_TEGRA20_CLOCK_H

extern struct clk_ops tegra_pll_ops;
extern struct clk_ops tegra_clk_m_ops;
extern struct clk_ops tegra_pll_div_ops;
extern struct clk_ops tegra_pllx_ops;
extern struct clk_ops tegra_plle_ops;
extern struct clk_ops tegra_clk_double_ops;
extern struct clk_ops tegra_cdev_clk_ops;
extern struct clk_ops tegra_audio_sync_clk_ops;
extern struct clk_ops tegra_super_ops;
extern struct clk_ops tegra_cpu_ops;
extern struct clk_ops tegra_cop_ops;
extern struct clk_ops tegra_bus_ops;
extern struct clk_ops tegra_blink_clk_ops;
extern struct clk_ops tegra_emc_clk_ops;
extern struct clk_ops tegra_periph_clk_ops;
extern struct clk_ops tegra_clk_shared_bus_ops;

#endif
