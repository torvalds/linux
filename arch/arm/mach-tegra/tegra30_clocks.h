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

#ifndef __MACH_TEGRA30_CLOCK_H
#define __MACH_TEGRA30_CLOCK_H

extern struct clk_ops tegra30_clk_32k_ops;
extern struct clk_ops tegra30_clk_m_ops;
extern struct clk_ops tegra_clk_m_div_ops;
extern struct clk_ops tegra_pll_ref_ops;
extern struct clk_ops tegra30_pll_ops;
extern struct clk_ops tegra30_pll_div_ops;
extern struct clk_ops tegra_plld_ops;
extern struct clk_ops tegra30_plle_ops;
extern struct clk_ops tegra_cml_clk_ops;
extern struct clk_ops tegra_pciex_clk_ops;
extern struct clk_ops tegra_sync_source_ops;
extern struct clk_ops tegra_audio_sync_clk_ops;
extern struct clk_ops tegra30_clk_double_ops;
extern struct clk_ops tegra_clk_out_ops;
extern struct clk_ops tegra30_super_ops;
extern struct clk_ops tegra30_blink_clk_ops;
extern struct clk_ops tegra30_twd_ops;
extern struct clk_ops tegra30_periph_clk_ops;
extern struct clk_ops tegra30_dsib_clk_ops;
extern struct clk_ops tegra_nand_clk_ops;
extern struct clk_ops tegra_vi_clk_ops;
extern struct clk_ops tegra_dtv_clk_ops;
extern struct clk_ops tegra_clk_shared_bus_ops;

int tegra30_plld_clk_cfg_ex(struct clk_hw *hw,
				enum tegra_clk_ex_param p, u32 setting);
void tegra30_periph_clk_reset(struct clk_hw *hw, bool assert);
int tegra30_vi_clk_cfg_ex(struct clk_hw *hw,
				enum tegra_clk_ex_param p, u32 setting);
int tegra30_nand_clk_cfg_ex(struct clk_hw *hw,
				enum tegra_clk_ex_param p, u32 setting);
int tegra30_dtv_clk_cfg_ex(struct clk_hw *hw,
				enum tegra_clk_ex_param p, u32 setting);
#endif
