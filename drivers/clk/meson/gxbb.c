/*
 * AmLogic S905 / GXBB Clock Controller Driver
 *
 * Copyright (c) 2016 AmLogic, Inc.
 * Michael Turquette <mturquette@baylibre.com>
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#include "clkc.h"
#include "gxbb.h"

static DEFINE_SPINLOCK(clk_lock);

static const struct pll_rate_table sys_pll_rate_table[] = {
	PLL_RATE(24000000, 56, 1, 2),
	PLL_RATE(48000000, 64, 1, 2),
	PLL_RATE(72000000, 72, 1, 2),
	PLL_RATE(96000000, 64, 1, 2),
	PLL_RATE(120000000, 80, 1, 2),
	PLL_RATE(144000000, 96, 1, 2),
	PLL_RATE(168000000, 56, 1, 1),
	PLL_RATE(192000000, 64, 1, 1),
	PLL_RATE(216000000, 72, 1, 1),
	PLL_RATE(240000000, 80, 1, 1),
	PLL_RATE(264000000, 88, 1, 1),
	PLL_RATE(288000000, 96, 1, 1),
	PLL_RATE(312000000, 52, 1, 2),
	PLL_RATE(336000000, 56, 1, 2),
	PLL_RATE(360000000, 60, 1, 2),
	PLL_RATE(384000000, 64, 1, 2),
	PLL_RATE(408000000, 68, 1, 2),
	PLL_RATE(432000000, 72, 1, 2),
	PLL_RATE(456000000, 76, 1, 2),
	PLL_RATE(480000000, 80, 1, 2),
	PLL_RATE(504000000, 84, 1, 2),
	PLL_RATE(528000000, 88, 1, 2),
	PLL_RATE(552000000, 92, 1, 2),
	PLL_RATE(576000000, 96, 1, 2),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(768000000, 64, 1, 1),
	PLL_RATE(792000000, 66, 1, 1),
	PLL_RATE(816000000, 68, 1, 1),
	PLL_RATE(840000000, 70, 1, 1),
	PLL_RATE(864000000, 72, 1, 1),
	PLL_RATE(888000000, 74, 1, 1),
	PLL_RATE(912000000, 76, 1, 1),
	PLL_RATE(936000000, 78, 1, 1),
	PLL_RATE(960000000, 80, 1, 1),
	PLL_RATE(984000000, 82, 1, 1),
	PLL_RATE(1008000000, 84, 1, 1),
	PLL_RATE(1032000000, 86, 1, 1),
	PLL_RATE(1056000000, 88, 1, 1),
	PLL_RATE(1080000000, 90, 1, 1),
	PLL_RATE(1104000000, 92, 1, 1),
	PLL_RATE(1128000000, 94, 1, 1),
	PLL_RATE(1152000000, 96, 1, 1),
	PLL_RATE(1176000000, 98, 1, 1),
	PLL_RATE(1200000000, 50, 1, 0),
	PLL_RATE(1224000000, 51, 1, 0),
	PLL_RATE(1248000000, 52, 1, 0),
	PLL_RATE(1272000000, 53, 1, 0),
	PLL_RATE(1296000000, 54, 1, 0),
	PLL_RATE(1320000000, 55, 1, 0),
	PLL_RATE(1344000000, 56, 1, 0),
	PLL_RATE(1368000000, 57, 1, 0),
	PLL_RATE(1392000000, 58, 1, 0),
	PLL_RATE(1416000000, 59, 1, 0),
	PLL_RATE(1440000000, 60, 1, 0),
	PLL_RATE(1464000000, 61, 1, 0),
	PLL_RATE(1488000000, 62, 1, 0),
	PLL_RATE(1512000000, 63, 1, 0),
	PLL_RATE(1536000000, 64, 1, 0),
	PLL_RATE(1560000000, 65, 1, 0),
	PLL_RATE(1584000000, 66, 1, 0),
	PLL_RATE(1608000000, 67, 1, 0),
	PLL_RATE(1632000000, 68, 1, 0),
	PLL_RATE(1656000000, 68, 1, 0),
	PLL_RATE(1680000000, 68, 1, 0),
	PLL_RATE(1704000000, 68, 1, 0),
	PLL_RATE(1728000000, 69, 1, 0),
	PLL_RATE(1752000000, 69, 1, 0),
	PLL_RATE(1776000000, 69, 1, 0),
	PLL_RATE(1800000000, 69, 1, 0),
	PLL_RATE(1824000000, 70, 1, 0),
	PLL_RATE(1848000000, 70, 1, 0),
	PLL_RATE(1872000000, 70, 1, 0),
	PLL_RATE(1896000000, 70, 1, 0),
	PLL_RATE(1920000000, 71, 1, 0),
	PLL_RATE(1944000000, 71, 1, 0),
	PLL_RATE(1968000000, 71, 1, 0),
	PLL_RATE(1992000000, 71, 1, 0),
	PLL_RATE(2016000000, 72, 1, 0),
	PLL_RATE(2040000000, 72, 1, 0),
	PLL_RATE(2064000000, 72, 1, 0),
	PLL_RATE(2088000000, 72, 1, 0),
	PLL_RATE(2112000000, 73, 1, 0),
	{ /* sentinel */ },
};

static const struct pll_rate_table gxbb_gp0_pll_rate_table[] = {
	PLL_RATE(96000000, 32, 1, 3),
	PLL_RATE(99000000, 33, 1, 3),
	PLL_RATE(102000000, 34, 1, 3),
	PLL_RATE(105000000, 35, 1, 3),
	PLL_RATE(108000000, 36, 1, 3),
	PLL_RATE(111000000, 37, 1, 3),
	PLL_RATE(114000000, 38, 1, 3),
	PLL_RATE(117000000, 39, 1, 3),
	PLL_RATE(120000000, 40, 1, 3),
	PLL_RATE(123000000, 41, 1, 3),
	PLL_RATE(126000000, 42, 1, 3),
	PLL_RATE(129000000, 43, 1, 3),
	PLL_RATE(132000000, 44, 1, 3),
	PLL_RATE(135000000, 45, 1, 3),
	PLL_RATE(138000000, 46, 1, 3),
	PLL_RATE(141000000, 47, 1, 3),
	PLL_RATE(144000000, 48, 1, 3),
	PLL_RATE(147000000, 49, 1, 3),
	PLL_RATE(150000000, 50, 1, 3),
	PLL_RATE(153000000, 51, 1, 3),
	PLL_RATE(156000000, 52, 1, 3),
	PLL_RATE(159000000, 53, 1, 3),
	PLL_RATE(162000000, 54, 1, 3),
	PLL_RATE(165000000, 55, 1, 3),
	PLL_RATE(168000000, 56, 1, 3),
	PLL_RATE(171000000, 57, 1, 3),
	PLL_RATE(174000000, 58, 1, 3),
	PLL_RATE(177000000, 59, 1, 3),
	PLL_RATE(180000000, 60, 1, 3),
	PLL_RATE(183000000, 61, 1, 3),
	PLL_RATE(186000000, 62, 1, 3),
	PLL_RATE(192000000, 32, 1, 2),
	PLL_RATE(198000000, 33, 1, 2),
	PLL_RATE(204000000, 34, 1, 2),
	PLL_RATE(210000000, 35, 1, 2),
	PLL_RATE(216000000, 36, 1, 2),
	PLL_RATE(222000000, 37, 1, 2),
	PLL_RATE(228000000, 38, 1, 2),
	PLL_RATE(234000000, 39, 1, 2),
	PLL_RATE(240000000, 40, 1, 2),
	PLL_RATE(246000000, 41, 1, 2),
	PLL_RATE(252000000, 42, 1, 2),
	PLL_RATE(258000000, 43, 1, 2),
	PLL_RATE(264000000, 44, 1, 2),
	PLL_RATE(270000000, 45, 1, 2),
	PLL_RATE(276000000, 46, 1, 2),
	PLL_RATE(282000000, 47, 1, 2),
	PLL_RATE(288000000, 48, 1, 2),
	PLL_RATE(294000000, 49, 1, 2),
	PLL_RATE(300000000, 50, 1, 2),
	PLL_RATE(306000000, 51, 1, 2),
	PLL_RATE(312000000, 52, 1, 2),
	PLL_RATE(318000000, 53, 1, 2),
	PLL_RATE(324000000, 54, 1, 2),
	PLL_RATE(330000000, 55, 1, 2),
	PLL_RATE(336000000, 56, 1, 2),
	PLL_RATE(342000000, 57, 1, 2),
	PLL_RATE(348000000, 58, 1, 2),
	PLL_RATE(354000000, 59, 1, 2),
	PLL_RATE(360000000, 60, 1, 2),
	PLL_RATE(366000000, 61, 1, 2),
	PLL_RATE(372000000, 62, 1, 2),
	PLL_RATE(384000000, 32, 1, 1),
	PLL_RATE(396000000, 33, 1, 1),
	PLL_RATE(408000000, 34, 1, 1),
	PLL_RATE(420000000, 35, 1, 1),
	PLL_RATE(432000000, 36, 1, 1),
	PLL_RATE(444000000, 37, 1, 1),
	PLL_RATE(456000000, 38, 1, 1),
	PLL_RATE(468000000, 39, 1, 1),
	PLL_RATE(480000000, 40, 1, 1),
	PLL_RATE(492000000, 41, 1, 1),
	PLL_RATE(504000000, 42, 1, 1),
	PLL_RATE(516000000, 43, 1, 1),
	PLL_RATE(528000000, 44, 1, 1),
	PLL_RATE(540000000, 45, 1, 1),
	PLL_RATE(552000000, 46, 1, 1),
	PLL_RATE(564000000, 47, 1, 1),
	PLL_RATE(576000000, 48, 1, 1),
	PLL_RATE(588000000, 49, 1, 1),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(612000000, 51, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(636000000, 53, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(660000000, 55, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(684000000, 57, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(708000000, 59, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(732000000, 61, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(768000000, 32, 1, 0),
	PLL_RATE(792000000, 33, 1, 0),
	PLL_RATE(816000000, 34, 1, 0),
	PLL_RATE(840000000, 35, 1, 0),
	PLL_RATE(864000000, 36, 1, 0),
	PLL_RATE(888000000, 37, 1, 0),
	PLL_RATE(912000000, 38, 1, 0),
	PLL_RATE(936000000, 39, 1, 0),
	PLL_RATE(960000000, 40, 1, 0),
	PLL_RATE(984000000, 41, 1, 0),
	PLL_RATE(1008000000, 42, 1, 0),
	PLL_RATE(1032000000, 43, 1, 0),
	PLL_RATE(1056000000, 44, 1, 0),
	PLL_RATE(1080000000, 45, 1, 0),
	PLL_RATE(1104000000, 46, 1, 0),
	PLL_RATE(1128000000, 47, 1, 0),
	PLL_RATE(1152000000, 48, 1, 0),
	PLL_RATE(1176000000, 49, 1, 0),
	PLL_RATE(1200000000, 50, 1, 0),
	PLL_RATE(1224000000, 51, 1, 0),
	PLL_RATE(1248000000, 52, 1, 0),
	PLL_RATE(1272000000, 53, 1, 0),
	PLL_RATE(1296000000, 54, 1, 0),
	PLL_RATE(1320000000, 55, 1, 0),
	PLL_RATE(1344000000, 56, 1, 0),
	PLL_RATE(1368000000, 57, 1, 0),
	PLL_RATE(1392000000, 58, 1, 0),
	PLL_RATE(1416000000, 59, 1, 0),
	PLL_RATE(1440000000, 60, 1, 0),
	PLL_RATE(1464000000, 61, 1, 0),
	PLL_RATE(1488000000, 62, 1, 0),
	{ /* sentinel */ },
};

static const struct pll_rate_table gxl_gp0_pll_rate_table[] = {
	PLL_RATE(504000000, 42, 1, 1),
	PLL_RATE(516000000, 43, 1, 1),
	PLL_RATE(528000000, 44, 1, 1),
	PLL_RATE(540000000, 45, 1, 1),
	PLL_RATE(552000000, 46, 1, 1),
	PLL_RATE(564000000, 47, 1, 1),
	PLL_RATE(576000000, 48, 1, 1),
	PLL_RATE(588000000, 49, 1, 1),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(612000000, 51, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(636000000, 53, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(660000000, 55, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(684000000, 57, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(708000000, 59, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(732000000, 61, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(756000000, 63, 1, 1),
	PLL_RATE(768000000, 64, 1, 1),
	PLL_RATE(780000000, 65, 1, 1),
	PLL_RATE(792000000, 66, 1, 1),
	{ /* sentinel */ },
};

static struct meson_clk_pll gxbb_fixed_pll = {
	.m = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct meson_clk_pll gxbb_hdmi_pll = {
	.m = {
		.reg_off = HHI_HDMI_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_HDMI_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.frac = {
		.reg_off = HHI_HDMI_PLL_CNTL2,
		.shift   = 0,
		.width   = 12,
	},
	.od = {
		.reg_off = HHI_HDMI_PLL_CNTL2,
		.shift   = 16,
		.width   = 2,
	},
	.od2 = {
		.reg_off = HHI_HDMI_PLL_CNTL2,
		.shift   = 22,
		.width   = 2,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "hdmi_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct meson_clk_pll gxbb_sys_pll = {
	.m = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 10,
		.width   = 2,
	},
	.rate_table = sys_pll_rate_table,
	.rate_count = ARRAY_SIZE(sys_pll_rate_table),
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

struct pll_params_table gxbb_gp0_params_table[] = {
	PLL_PARAM(HHI_GP0_PLL_CNTL, 0x6a000228),
	PLL_PARAM(HHI_GP0_PLL_CNTL2, 0x69c80000),
	PLL_PARAM(HHI_GP0_PLL_CNTL3, 0x0a5590c4),
	PLL_PARAM(HHI_GP0_PLL_CNTL4, 0x0000500d),
};

static struct meson_clk_pll gxbb_gp0_pll = {
	.m = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.params = {
		.params_table = gxbb_gp0_params_table,
		.params_count =	ARRAY_SIZE(gxbb_gp0_params_table),
		.no_init_reset = true,
		.clear_reset_for_lock = true,
	},
	.rate_table = gxbb_gp0_pll_rate_table,
	.rate_count = ARRAY_SIZE(gxbb_gp0_pll_rate_table),
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

struct pll_params_table gxl_gp0_params_table[] = {
	PLL_PARAM(HHI_GP0_PLL_CNTL, 0x40010250),
	PLL_PARAM(HHI_GP0_PLL_CNTL1, 0xc084a000),
	PLL_PARAM(HHI_GP0_PLL_CNTL2, 0xb75020be),
	PLL_PARAM(HHI_GP0_PLL_CNTL3, 0x0a59a288),
	PLL_PARAM(HHI_GP0_PLL_CNTL4, 0xc000004d),
	PLL_PARAM(HHI_GP0_PLL_CNTL5, 0x00078000),
};

static struct meson_clk_pll gxl_gp0_pll = {
	.m = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_GP0_PLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.params = {
		.params_table = gxl_gp0_params_table,
		.params_count =	ARRAY_SIZE(gxl_gp0_params_table),
		.no_init_reset = true,
		.reset_lock_loop = true,
	},
	.rate_table = gxl_gp0_pll_rate_table,
	.rate_count = ARRAY_SIZE(gxl_gp0_pll_rate_table),
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_fixed_factor gxbb_fclk_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div3 = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div4 = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div5 = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor gxbb_fclk_div7 = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll gxbb_mpll0 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 15,
		.width	 = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 14,
		.width	 = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll gxbb_mpll1 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 15,
		.width	 = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 14,
		.width	 = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll gxbb_mpll2 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 15,
		.width	 = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 14,
		.width	 = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

/*
 * FIXME The legacy composite clocks (e.g. clk81) are both PLL post-dividers
 * and should be modeled with their respective PLLs via the forthcoming
 * coordinated clock rates feature
 */

static u32 mux_table_clk81[]	= { 6, 5, 7 };

static struct clk_mux gxbb_mpeg_clk_sel = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.mask = 0x7,
	.shift = 12,
	.flags = CLK_MUX_READ_ONLY,
	.table = mux_table_clk81,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_sel",
		.ops = &clk_mux_ro_ops,
		/*
		 * FIXME bits 14:12 selects from 8 possible parents:
		 * xtal, 1'b0 (wtf), fclk_div7, mpll_clkout1, mpll_clkout2,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_names = (const char *[]){ "fclk_div3", "fclk_div4",
			"fclk_div5" },
		.num_parents = 3,
		.flags = (CLK_SET_RATE_NO_REPARENT | CLK_IGNORE_UNUSED),
	},
};

static struct clk_divider gxbb_mpeg_clk_div = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.shift = 0,
	.width = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "mpeg_clk_sel" },
		.num_parents = 1,
		.flags = (CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

/* the mother of dragons^W gates */
static struct clk_gate gxbb_clk81 = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.bit_idx = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "clk81",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "mpeg_clk_div" },
		.num_parents = 1,
		.flags = (CLK_SET_RATE_PARENT | CLK_IS_CRITICAL),
	},
};

static struct clk_mux gxbb_sar_adc_clk_sel = {
	.reg = (void *)HHI_SAR_CLK_CNTL,
	.mask = 0x3,
	.shift = 9,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk_sel",
		.ops = &clk_mux_ops,
		/* NOTE: The datasheet doesn't list the parents for bit 10 */
		.parent_names = (const char *[]){ "xtal", "clk81", },
		.num_parents = 2,
	},
};

static struct clk_divider gxbb_sar_adc_clk_div = {
	.reg = (void *)HHI_SAR_CLK_CNTL,
	.shift = 0,
	.width = 8,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "sar_adc_clk_sel" },
		.num_parents = 1,
	},
};

static struct clk_gate gxbb_sar_adc_clk = {
	.reg = (void *)HHI_SAR_CLK_CNTL,
	.bit_idx = 8,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "sar_adc_clk",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "sar_adc_clk_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * The MALI IP is clocked by two identical clocks (mali_0 and mali_1)
 * muxed by a glitch-free switch.
 */

static u32 mux_table_mali_0_1[] = {0, 1, 2, 3, 4, 5, 6, 7};
static const char * const gxbb_mali_0_1_parent_names[] = {
	"xtal", "gp0_pll", "mpll2", "mpll1", "fclk_div7",
	"fclk_div4", "fclk_div3", "fclk_div5"
};

static struct clk_mux gxbb_mali_0_sel = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.mask = 0x7,
	.shift = 9,
	.table = mux_table_mali_0_1,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_sel",
		.ops = &clk_mux_ops,
		/*
		 * bits 10:9 selects from 8 possible parents:
		 * xtal, gp0_pll, mpll2, mpll1, fclk_div7,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_names = gxbb_mali_0_1_parent_names,
		.num_parents = 8,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_divider gxbb_mali_0_div = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.shift = 0,
	.width = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_0_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "mali_0_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_gate gxbb_mali_0 = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.bit_idx = 8,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_0",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "mali_0_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_mux gxbb_mali_1_sel = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.mask = 0x7,
	.shift = 25,
	.table = mux_table_mali_0_1,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_sel",
		.ops = &clk_mux_ops,
		/*
		 * bits 10:9 selects from 8 possible parents:
		 * xtal, gp0_pll, mpll2, mpll1, fclk_div7,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_names = gxbb_mali_0_1_parent_names,
		.num_parents = 8,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_divider gxbb_mali_1_div = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.shift = 16,
	.width = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_1_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "mali_1_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_gate gxbb_mali_1 = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.bit_idx = 24,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali_1",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "mali_1_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_mali[] = {0, 1};
static const char * const gxbb_mali_parent_names[] = {
	"mali_0", "mali_1"
};

static struct clk_mux gxbb_mali = {
	.reg = (void *)HHI_MALI_CLK_CNTL,
	.mask = 1,
	.shift = 31,
	.table = mux_table_mali,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mali",
		.ops = &clk_mux_ops,
		.parent_names = gxbb_mali_parent_names,
		.num_parents = 2,
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_mux gxbb_cts_amclk_sel = {
	.reg = (void *) HHI_AUD_CLK_CNTL,
	.mask = 0x3,
	.shift = 9,
	/* Default parent unknown (register reset value: 0) */
	.table = (u32[]){ 1, 2, 3 },
	.lock = &clk_lock,
		.hw.init = &(struct clk_init_data){
		.name = "cts_amclk_sel",
		.ops = &clk_mux_ops,
		.parent_names = (const char *[]){ "mpll0", "mpll1", "mpll2" },
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct meson_clk_audio_divider gxbb_cts_amclk_div = {
	.div = {
		.reg_off = HHI_AUD_CLK_CNTL,
		.shift   = 0,
		.width   = 8,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk_div",
		.ops = &meson_clk_audio_divider_ops,
		.parent_names = (const char *[]){ "cts_amclk_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_DIVIDER_ROUND_CLOSEST,
	},
};

static struct clk_gate gxbb_cts_amclk = {
	.reg = (void *) HHI_AUD_CLK_CNTL,
	.bit_idx = 8,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "cts_amclk",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "cts_amclk_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_mux gxbb_cts_mclk_i958_sel = {
	.reg = (void *)HHI_AUD_CLK_CNTL2,
	.mask = 0x3,
	.shift = 25,
	/* Default parent unknown (register reset value: 0) */
	.table = (u32[]){ 1, 2, 3 },
	.lock = &clk_lock,
		.hw.init = &(struct clk_init_data){
		.name = "cts_mclk_i958_sel",
		.ops = &clk_mux_ops,
		.parent_names = (const char *[]){ "mpll0", "mpll1", "mpll2" },
		.num_parents = 3,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_divider gxbb_cts_mclk_i958_div = {
	.reg = (void *)HHI_AUD_CLK_CNTL2,
	.shift = 16,
	.width = 8,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "cts_mclk_i958_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "cts_mclk_i958_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_DIVIDER_ROUND_CLOSEST,
	},
};

static struct clk_gate gxbb_cts_mclk_i958 = {
	.reg = (void *)HHI_AUD_CLK_CNTL2,
	.bit_idx = 24,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "cts_mclk_i958",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "cts_mclk_i958_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_mux gxbb_cts_i958 = {
	.reg = (void *)HHI_AUD_CLK_CNTL2,
	.mask = 0x1,
	.shift = 27,
	.lock = &clk_lock,
		.hw.init = &(struct clk_init_data){
		.name = "cts_i958",
		.ops = &clk_mux_ops,
		.parent_names = (const char *[]){ "cts_amclk", "cts_mclk_i958" },
		.num_parents = 2,
		/*
		 *The parent is specific to origin of the audio data. Let the
		 * consumer choose the appropriate parent
		 */
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_divider gxbb_32k_clk_div = {
	.reg = (void *)HHI_32K_CLK_CNTL,
	.shift = 0,
	.width = 14,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "32k_clk_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "32k_clk_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT | CLK_DIVIDER_ROUND_CLOSEST,
	},
};

static struct clk_gate gxbb_32k_clk = {
	.reg = (void *)HHI_32K_CLK_CNTL,
	.bit_idx = 15,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "32k_clk",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "32k_clk_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const char * const gxbb_32k_clk_parent_names[] = {
	"xtal", "cts_slow_oscin", "fclk_div3", "fclk_div5"
};

static struct clk_mux gxbb_32k_clk_sel = {
	.reg = (void *)HHI_32K_CLK_CNTL,
	.mask = 0x3,
	.shift = 16,
	.lock = &clk_lock,
		.hw.init = &(struct clk_init_data){
		.name = "32k_clk_sel",
		.ops = &clk_mux_ops,
		.parent_names = gxbb_32k_clk_parent_names,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* Everything Else (EE) domain gates */
static MESON_GATE(gxbb_ddr, HHI_GCLK_MPEG0, 0);
static MESON_GATE(gxbb_dos, HHI_GCLK_MPEG0, 1);
static MESON_GATE(gxbb_isa, HHI_GCLK_MPEG0, 5);
static MESON_GATE(gxbb_pl301, HHI_GCLK_MPEG0, 6);
static MESON_GATE(gxbb_periphs, HHI_GCLK_MPEG0, 7);
static MESON_GATE(gxbb_spicc, HHI_GCLK_MPEG0, 8);
static MESON_GATE(gxbb_i2c, HHI_GCLK_MPEG0, 9);
static MESON_GATE(gxbb_sar_adc, HHI_GCLK_MPEG0, 10);
static MESON_GATE(gxbb_smart_card, HHI_GCLK_MPEG0, 11);
static MESON_GATE(gxbb_rng0, HHI_GCLK_MPEG0, 12);
static MESON_GATE(gxbb_uart0, HHI_GCLK_MPEG0, 13);
static MESON_GATE(gxbb_sdhc, HHI_GCLK_MPEG0, 14);
static MESON_GATE(gxbb_stream, HHI_GCLK_MPEG0, 15);
static MESON_GATE(gxbb_async_fifo, HHI_GCLK_MPEG0, 16);
static MESON_GATE(gxbb_sdio, HHI_GCLK_MPEG0, 17);
static MESON_GATE(gxbb_abuf, HHI_GCLK_MPEG0, 18);
static MESON_GATE(gxbb_hiu_iface, HHI_GCLK_MPEG0, 19);
static MESON_GATE(gxbb_assist_misc, HHI_GCLK_MPEG0, 23);
static MESON_GATE(gxbb_emmc_a, HHI_GCLK_MPEG0, 24);
static MESON_GATE(gxbb_emmc_b, HHI_GCLK_MPEG0, 25);
static MESON_GATE(gxbb_emmc_c, HHI_GCLK_MPEG0, 26);
static MESON_GATE(gxbb_spi, HHI_GCLK_MPEG0, 30);

static MESON_GATE(gxbb_i2s_spdif, HHI_GCLK_MPEG1, 2);
static MESON_GATE(gxbb_eth, HHI_GCLK_MPEG1, 3);
static MESON_GATE(gxbb_demux, HHI_GCLK_MPEG1, 4);
static MESON_GATE(gxbb_aiu_glue, HHI_GCLK_MPEG1, 6);
static MESON_GATE(gxbb_iec958, HHI_GCLK_MPEG1, 7);
static MESON_GATE(gxbb_i2s_out, HHI_GCLK_MPEG1, 8);
static MESON_GATE(gxbb_amclk, HHI_GCLK_MPEG1, 9);
static MESON_GATE(gxbb_aififo2, HHI_GCLK_MPEG1, 10);
static MESON_GATE(gxbb_mixer, HHI_GCLK_MPEG1, 11);
static MESON_GATE(gxbb_mixer_iface, HHI_GCLK_MPEG1, 12);
static MESON_GATE(gxbb_adc, HHI_GCLK_MPEG1, 13);
static MESON_GATE(gxbb_blkmv, HHI_GCLK_MPEG1, 14);
static MESON_GATE(gxbb_aiu, HHI_GCLK_MPEG1, 15);
static MESON_GATE(gxbb_uart1, HHI_GCLK_MPEG1, 16);
static MESON_GATE(gxbb_g2d, HHI_GCLK_MPEG1, 20);
static MESON_GATE(gxbb_usb0, HHI_GCLK_MPEG1, 21);
static MESON_GATE(gxbb_usb1, HHI_GCLK_MPEG1, 22);
static MESON_GATE(gxbb_reset, HHI_GCLK_MPEG1, 23);
static MESON_GATE(gxbb_nand, HHI_GCLK_MPEG1, 24);
static MESON_GATE(gxbb_dos_parser, HHI_GCLK_MPEG1, 25);
static MESON_GATE(gxbb_usb, HHI_GCLK_MPEG1, 26);
static MESON_GATE(gxbb_vdin1, HHI_GCLK_MPEG1, 28);
static MESON_GATE(gxbb_ahb_arb0, HHI_GCLK_MPEG1, 29);
static MESON_GATE(gxbb_efuse, HHI_GCLK_MPEG1, 30);
static MESON_GATE(gxbb_boot_rom, HHI_GCLK_MPEG1, 31);

static MESON_GATE(gxbb_ahb_data_bus, HHI_GCLK_MPEG2, 1);
static MESON_GATE(gxbb_ahb_ctrl_bus, HHI_GCLK_MPEG2, 2);
static MESON_GATE(gxbb_hdmi_intr_sync, HHI_GCLK_MPEG2, 3);
static MESON_GATE(gxbb_hdmi_pclk, HHI_GCLK_MPEG2, 4);
static MESON_GATE(gxbb_usb1_ddr_bridge, HHI_GCLK_MPEG2, 8);
static MESON_GATE(gxbb_usb0_ddr_bridge, HHI_GCLK_MPEG2, 9);
static MESON_GATE(gxbb_mmc_pclk, HHI_GCLK_MPEG2, 11);
static MESON_GATE(gxbb_dvin, HHI_GCLK_MPEG2, 12);
static MESON_GATE(gxbb_uart2, HHI_GCLK_MPEG2, 15);
static MESON_GATE(gxbb_sana, HHI_GCLK_MPEG2, 22);
static MESON_GATE(gxbb_vpu_intr, HHI_GCLK_MPEG2, 25);
static MESON_GATE(gxbb_sec_ahb_ahb3_bridge, HHI_GCLK_MPEG2, 26);
static MESON_GATE(gxbb_clk81_a53, HHI_GCLK_MPEG2, 29);

static MESON_GATE(gxbb_vclk2_venci0, HHI_GCLK_OTHER, 1);
static MESON_GATE(gxbb_vclk2_venci1, HHI_GCLK_OTHER, 2);
static MESON_GATE(gxbb_vclk2_vencp0, HHI_GCLK_OTHER, 3);
static MESON_GATE(gxbb_vclk2_vencp1, HHI_GCLK_OTHER, 4);
static MESON_GATE(gxbb_gclk_venci_int0, HHI_GCLK_OTHER, 8);
static MESON_GATE(gxbb_gclk_vencp_int, HHI_GCLK_OTHER, 9);
static MESON_GATE(gxbb_dac_clk, HHI_GCLK_OTHER, 10);
static MESON_GATE(gxbb_aoclk_gate, HHI_GCLK_OTHER, 14);
static MESON_GATE(gxbb_iec958_gate, HHI_GCLK_OTHER, 16);
static MESON_GATE(gxbb_enc480p, HHI_GCLK_OTHER, 20);
static MESON_GATE(gxbb_rng1, HHI_GCLK_OTHER, 21);
static MESON_GATE(gxbb_gclk_venci_int1, HHI_GCLK_OTHER, 22);
static MESON_GATE(gxbb_vclk2_venclmcc, HHI_GCLK_OTHER, 24);
static MESON_GATE(gxbb_vclk2_vencl, HHI_GCLK_OTHER, 25);
static MESON_GATE(gxbb_vclk_other, HHI_GCLK_OTHER, 26);
static MESON_GATE(gxbb_edp, HHI_GCLK_OTHER, 31);

/* Always On (AO) domain gates */

static MESON_GATE(gxbb_ao_media_cpu, HHI_GCLK_AO, 0);
static MESON_GATE(gxbb_ao_ahb_sram, HHI_GCLK_AO, 1);
static MESON_GATE(gxbb_ao_ahb_bus, HHI_GCLK_AO, 2);
static MESON_GATE(gxbb_ao_iface, HHI_GCLK_AO, 3);
static MESON_GATE(gxbb_ao_i2c, HHI_GCLK_AO, 4);

/* Array of all clocks provided by this provider */

static struct clk_hw_onecell_data gxbb_hw_onecell_data = {
	.hws = {
		[CLKID_SYS_PLL]		    = &gxbb_sys_pll.hw,
		[CLKID_HDMI_PLL]	    = &gxbb_hdmi_pll.hw,
		[CLKID_FIXED_PLL]	    = &gxbb_fixed_pll.hw,
		[CLKID_FCLK_DIV2]	    = &gxbb_fclk_div2.hw,
		[CLKID_FCLK_DIV3]	    = &gxbb_fclk_div3.hw,
		[CLKID_FCLK_DIV4]	    = &gxbb_fclk_div4.hw,
		[CLKID_FCLK_DIV5]	    = &gxbb_fclk_div5.hw,
		[CLKID_FCLK_DIV7]	    = &gxbb_fclk_div7.hw,
		[CLKID_GP0_PLL]		    = &gxbb_gp0_pll.hw,
		[CLKID_MPEG_SEL]	    = &gxbb_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV]	    = &gxbb_mpeg_clk_div.hw,
		[CLKID_CLK81]		    = &gxbb_clk81.hw,
		[CLKID_MPLL0]		    = &gxbb_mpll0.hw,
		[CLKID_MPLL1]		    = &gxbb_mpll1.hw,
		[CLKID_MPLL2]		    = &gxbb_mpll2.hw,
		[CLKID_DDR]		    = &gxbb_ddr.hw,
		[CLKID_DOS]		    = &gxbb_dos.hw,
		[CLKID_ISA]		    = &gxbb_isa.hw,
		[CLKID_PL301]		    = &gxbb_pl301.hw,
		[CLKID_PERIPHS]		    = &gxbb_periphs.hw,
		[CLKID_SPICC]		    = &gxbb_spicc.hw,
		[CLKID_I2C]		    = &gxbb_i2c.hw,
		[CLKID_SAR_ADC]		    = &gxbb_sar_adc.hw,
		[CLKID_SMART_CARD]	    = &gxbb_smart_card.hw,
		[CLKID_RNG0]		    = &gxbb_rng0.hw,
		[CLKID_UART0]		    = &gxbb_uart0.hw,
		[CLKID_SDHC]		    = &gxbb_sdhc.hw,
		[CLKID_STREAM]		    = &gxbb_stream.hw,
		[CLKID_ASYNC_FIFO]	    = &gxbb_async_fifo.hw,
		[CLKID_SDIO]		    = &gxbb_sdio.hw,
		[CLKID_ABUF]		    = &gxbb_abuf.hw,
		[CLKID_HIU_IFACE]	    = &gxbb_hiu_iface.hw,
		[CLKID_ASSIST_MISC]	    = &gxbb_assist_misc.hw,
		[CLKID_SPI]		    = &gxbb_spi.hw,
		[CLKID_I2S_SPDIF]	    = &gxbb_i2s_spdif.hw,
		[CLKID_ETH]		    = &gxbb_eth.hw,
		[CLKID_DEMUX]		    = &gxbb_demux.hw,
		[CLKID_AIU_GLUE]	    = &gxbb_aiu_glue.hw,
		[CLKID_IEC958]		    = &gxbb_iec958.hw,
		[CLKID_I2S_OUT]		    = &gxbb_i2s_out.hw,
		[CLKID_AMCLK]		    = &gxbb_amclk.hw,
		[CLKID_AIFIFO2]		    = &gxbb_aififo2.hw,
		[CLKID_MIXER]		    = &gxbb_mixer.hw,
		[CLKID_MIXER_IFACE]	    = &gxbb_mixer_iface.hw,
		[CLKID_ADC]		    = &gxbb_adc.hw,
		[CLKID_BLKMV]		    = &gxbb_blkmv.hw,
		[CLKID_AIU]		    = &gxbb_aiu.hw,
		[CLKID_UART1]		    = &gxbb_uart1.hw,
		[CLKID_G2D]		    = &gxbb_g2d.hw,
		[CLKID_USB0]		    = &gxbb_usb0.hw,
		[CLKID_USB1]		    = &gxbb_usb1.hw,
		[CLKID_RESET]		    = &gxbb_reset.hw,
		[CLKID_NAND]		    = &gxbb_nand.hw,
		[CLKID_DOS_PARSER]	    = &gxbb_dos_parser.hw,
		[CLKID_USB]		    = &gxbb_usb.hw,
		[CLKID_VDIN1]		    = &gxbb_vdin1.hw,
		[CLKID_AHB_ARB0]	    = &gxbb_ahb_arb0.hw,
		[CLKID_EFUSE]		    = &gxbb_efuse.hw,
		[CLKID_BOOT_ROM]	    = &gxbb_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]	    = &gxbb_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]	    = &gxbb_ahb_ctrl_bus.hw,
		[CLKID_HDMI_INTR_SYNC]	    = &gxbb_hdmi_intr_sync.hw,
		[CLKID_HDMI_PCLK]	    = &gxbb_hdmi_pclk.hw,
		[CLKID_USB1_DDR_BRIDGE]	    = &gxbb_usb1_ddr_bridge.hw,
		[CLKID_USB0_DDR_BRIDGE]	    = &gxbb_usb0_ddr_bridge.hw,
		[CLKID_MMC_PCLK]	    = &gxbb_mmc_pclk.hw,
		[CLKID_DVIN]		    = &gxbb_dvin.hw,
		[CLKID_UART2]		    = &gxbb_uart2.hw,
		[CLKID_SANA]		    = &gxbb_sana.hw,
		[CLKID_VPU_INTR]	    = &gxbb_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE] = &gxbb_sec_ahb_ahb3_bridge.hw,
		[CLKID_CLK81_A53]	    = &gxbb_clk81_a53.hw,
		[CLKID_VCLK2_VENCI0]	    = &gxbb_vclk2_venci0.hw,
		[CLKID_VCLK2_VENCI1]	    = &gxbb_vclk2_venci1.hw,
		[CLKID_VCLK2_VENCP0]	    = &gxbb_vclk2_vencp0.hw,
		[CLKID_VCLK2_VENCP1]	    = &gxbb_vclk2_vencp1.hw,
		[CLKID_GCLK_VENCI_INT0]	    = &gxbb_gclk_venci_int0.hw,
		[CLKID_GCLK_VENCI_INT]	    = &gxbb_gclk_vencp_int.hw,
		[CLKID_DAC_CLK]		    = &gxbb_dac_clk.hw,
		[CLKID_AOCLK_GATE]	    = &gxbb_aoclk_gate.hw,
		[CLKID_IEC958_GATE]	    = &gxbb_iec958_gate.hw,
		[CLKID_ENC480P]		    = &gxbb_enc480p.hw,
		[CLKID_RNG1]		    = &gxbb_rng1.hw,
		[CLKID_GCLK_VENCI_INT1]	    = &gxbb_gclk_venci_int1.hw,
		[CLKID_VCLK2_VENCLMCC]	    = &gxbb_vclk2_venclmcc.hw,
		[CLKID_VCLK2_VENCL]	    = &gxbb_vclk2_vencl.hw,
		[CLKID_VCLK_OTHER]	    = &gxbb_vclk_other.hw,
		[CLKID_EDP]		    = &gxbb_edp.hw,
		[CLKID_AO_MEDIA_CPU]	    = &gxbb_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]	    = &gxbb_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]	    = &gxbb_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]	    = &gxbb_ao_iface.hw,
		[CLKID_AO_I2C]		    = &gxbb_ao_i2c.hw,
		[CLKID_SD_EMMC_A]	    = &gxbb_emmc_a.hw,
		[CLKID_SD_EMMC_B]	    = &gxbb_emmc_b.hw,
		[CLKID_SD_EMMC_C]	    = &gxbb_emmc_c.hw,
		[CLKID_SAR_ADC_CLK]	    = &gxbb_sar_adc_clk.hw,
		[CLKID_SAR_ADC_SEL]	    = &gxbb_sar_adc_clk_sel.hw,
		[CLKID_SAR_ADC_DIV]	    = &gxbb_sar_adc_clk_div.hw,
		[CLKID_MALI_0_SEL]	    = &gxbb_mali_0_sel.hw,
		[CLKID_MALI_0_DIV]	    = &gxbb_mali_0_div.hw,
		[CLKID_MALI_0]		    = &gxbb_mali_0.hw,
		[CLKID_MALI_1_SEL]	    = &gxbb_mali_1_sel.hw,
		[CLKID_MALI_1_DIV]	    = &gxbb_mali_1_div.hw,
		[CLKID_MALI_1]		    = &gxbb_mali_1.hw,
		[CLKID_MALI]		    = &gxbb_mali.hw,
		[CLKID_CTS_AMCLK]	    = &gxbb_cts_amclk.hw,
		[CLKID_CTS_AMCLK_SEL]	    = &gxbb_cts_amclk_sel.hw,
		[CLKID_CTS_AMCLK_DIV]	    = &gxbb_cts_amclk_div.hw,
		[CLKID_CTS_MCLK_I958]	    = &gxbb_cts_mclk_i958.hw,
		[CLKID_CTS_MCLK_I958_SEL]   = &gxbb_cts_mclk_i958_sel.hw,
		[CLKID_CTS_MCLK_I958_DIV]   = &gxbb_cts_mclk_i958_div.hw,
		[CLKID_CTS_I958]	    = &gxbb_cts_i958.hw,
		[CLKID_32K_CLK]		    = &gxbb_32k_clk.hw,
		[CLKID_32K_CLK_SEL]	    = &gxbb_32k_clk_sel.hw,
		[CLKID_32K_CLK_DIV]	    = &gxbb_32k_clk_div.hw,
	},
	.num = NR_CLKS,
};

static struct clk_hw_onecell_data gxl_hw_onecell_data = {
	.hws = {
		[CLKID_SYS_PLL]		    = &gxbb_sys_pll.hw,
		[CLKID_HDMI_PLL]	    = &gxbb_hdmi_pll.hw,
		[CLKID_FIXED_PLL]	    = &gxbb_fixed_pll.hw,
		[CLKID_FCLK_DIV2]	    = &gxbb_fclk_div2.hw,
		[CLKID_FCLK_DIV3]	    = &gxbb_fclk_div3.hw,
		[CLKID_FCLK_DIV4]	    = &gxbb_fclk_div4.hw,
		[CLKID_FCLK_DIV5]	    = &gxbb_fclk_div5.hw,
		[CLKID_FCLK_DIV7]	    = &gxbb_fclk_div7.hw,
		[CLKID_GP0_PLL]		    = &gxl_gp0_pll.hw,
		[CLKID_MPEG_SEL]	    = &gxbb_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV]	    = &gxbb_mpeg_clk_div.hw,
		[CLKID_CLK81]		    = &gxbb_clk81.hw,
		[CLKID_MPLL0]		    = &gxbb_mpll0.hw,
		[CLKID_MPLL1]		    = &gxbb_mpll1.hw,
		[CLKID_MPLL2]		    = &gxbb_mpll2.hw,
		[CLKID_DDR]		    = &gxbb_ddr.hw,
		[CLKID_DOS]		    = &gxbb_dos.hw,
		[CLKID_ISA]		    = &gxbb_isa.hw,
		[CLKID_PL301]		    = &gxbb_pl301.hw,
		[CLKID_PERIPHS]		    = &gxbb_periphs.hw,
		[CLKID_SPICC]		    = &gxbb_spicc.hw,
		[CLKID_I2C]		    = &gxbb_i2c.hw,
		[CLKID_SAR_ADC]		    = &gxbb_sar_adc.hw,
		[CLKID_SMART_CARD]	    = &gxbb_smart_card.hw,
		[CLKID_RNG0]		    = &gxbb_rng0.hw,
		[CLKID_UART0]		    = &gxbb_uart0.hw,
		[CLKID_SDHC]		    = &gxbb_sdhc.hw,
		[CLKID_STREAM]		    = &gxbb_stream.hw,
		[CLKID_ASYNC_FIFO]	    = &gxbb_async_fifo.hw,
		[CLKID_SDIO]		    = &gxbb_sdio.hw,
		[CLKID_ABUF]		    = &gxbb_abuf.hw,
		[CLKID_HIU_IFACE]	    = &gxbb_hiu_iface.hw,
		[CLKID_ASSIST_MISC]	    = &gxbb_assist_misc.hw,
		[CLKID_SPI]		    = &gxbb_spi.hw,
		[CLKID_I2S_SPDIF]	    = &gxbb_i2s_spdif.hw,
		[CLKID_ETH]		    = &gxbb_eth.hw,
		[CLKID_DEMUX]		    = &gxbb_demux.hw,
		[CLKID_AIU_GLUE]	    = &gxbb_aiu_glue.hw,
		[CLKID_IEC958]		    = &gxbb_iec958.hw,
		[CLKID_I2S_OUT]		    = &gxbb_i2s_out.hw,
		[CLKID_AMCLK]		    = &gxbb_amclk.hw,
		[CLKID_AIFIFO2]		    = &gxbb_aififo2.hw,
		[CLKID_MIXER]		    = &gxbb_mixer.hw,
		[CLKID_MIXER_IFACE]	    = &gxbb_mixer_iface.hw,
		[CLKID_ADC]		    = &gxbb_adc.hw,
		[CLKID_BLKMV]		    = &gxbb_blkmv.hw,
		[CLKID_AIU]		    = &gxbb_aiu.hw,
		[CLKID_UART1]		    = &gxbb_uart1.hw,
		[CLKID_G2D]		    = &gxbb_g2d.hw,
		[CLKID_USB0]		    = &gxbb_usb0.hw,
		[CLKID_USB1]		    = &gxbb_usb1.hw,
		[CLKID_RESET]		    = &gxbb_reset.hw,
		[CLKID_NAND]		    = &gxbb_nand.hw,
		[CLKID_DOS_PARSER]	    = &gxbb_dos_parser.hw,
		[CLKID_USB]		    = &gxbb_usb.hw,
		[CLKID_VDIN1]		    = &gxbb_vdin1.hw,
		[CLKID_AHB_ARB0]	    = &gxbb_ahb_arb0.hw,
		[CLKID_EFUSE]		    = &gxbb_efuse.hw,
		[CLKID_BOOT_ROM]	    = &gxbb_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]	    = &gxbb_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]	    = &gxbb_ahb_ctrl_bus.hw,
		[CLKID_HDMI_INTR_SYNC]	    = &gxbb_hdmi_intr_sync.hw,
		[CLKID_HDMI_PCLK]	    = &gxbb_hdmi_pclk.hw,
		[CLKID_USB1_DDR_BRIDGE]	    = &gxbb_usb1_ddr_bridge.hw,
		[CLKID_USB0_DDR_BRIDGE]	    = &gxbb_usb0_ddr_bridge.hw,
		[CLKID_MMC_PCLK]	    = &gxbb_mmc_pclk.hw,
		[CLKID_DVIN]		    = &gxbb_dvin.hw,
		[CLKID_UART2]		    = &gxbb_uart2.hw,
		[CLKID_SANA]		    = &gxbb_sana.hw,
		[CLKID_VPU_INTR]	    = &gxbb_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE] = &gxbb_sec_ahb_ahb3_bridge.hw,
		[CLKID_CLK81_A53]	    = &gxbb_clk81_a53.hw,
		[CLKID_VCLK2_VENCI0]	    = &gxbb_vclk2_venci0.hw,
		[CLKID_VCLK2_VENCI1]	    = &gxbb_vclk2_venci1.hw,
		[CLKID_VCLK2_VENCP0]	    = &gxbb_vclk2_vencp0.hw,
		[CLKID_VCLK2_VENCP1]	    = &gxbb_vclk2_vencp1.hw,
		[CLKID_GCLK_VENCI_INT0]	    = &gxbb_gclk_venci_int0.hw,
		[CLKID_GCLK_VENCI_INT]	    = &gxbb_gclk_vencp_int.hw,
		[CLKID_DAC_CLK]		    = &gxbb_dac_clk.hw,
		[CLKID_AOCLK_GATE]	    = &gxbb_aoclk_gate.hw,
		[CLKID_IEC958_GATE]	    = &gxbb_iec958_gate.hw,
		[CLKID_ENC480P]		    = &gxbb_enc480p.hw,
		[CLKID_RNG1]		    = &gxbb_rng1.hw,
		[CLKID_GCLK_VENCI_INT1]	    = &gxbb_gclk_venci_int1.hw,
		[CLKID_VCLK2_VENCLMCC]	    = &gxbb_vclk2_venclmcc.hw,
		[CLKID_VCLK2_VENCL]	    = &gxbb_vclk2_vencl.hw,
		[CLKID_VCLK_OTHER]	    = &gxbb_vclk_other.hw,
		[CLKID_EDP]		    = &gxbb_edp.hw,
		[CLKID_AO_MEDIA_CPU]	    = &gxbb_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]	    = &gxbb_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]	    = &gxbb_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]	    = &gxbb_ao_iface.hw,
		[CLKID_AO_I2C]		    = &gxbb_ao_i2c.hw,
		[CLKID_SD_EMMC_A]	    = &gxbb_emmc_a.hw,
		[CLKID_SD_EMMC_B]	    = &gxbb_emmc_b.hw,
		[CLKID_SD_EMMC_C]	    = &gxbb_emmc_c.hw,
		[CLKID_SAR_ADC_CLK]	    = &gxbb_sar_adc_clk.hw,
		[CLKID_SAR_ADC_SEL]	    = &gxbb_sar_adc_clk_sel.hw,
		[CLKID_SAR_ADC_DIV]	    = &gxbb_sar_adc_clk_div.hw,
		[CLKID_MALI_0_SEL]	    = &gxbb_mali_0_sel.hw,
		[CLKID_MALI_0_DIV]	    = &gxbb_mali_0_div.hw,
		[CLKID_MALI_0]		    = &gxbb_mali_0.hw,
		[CLKID_MALI_1_SEL]	    = &gxbb_mali_1_sel.hw,
		[CLKID_MALI_1_DIV]	    = &gxbb_mali_1_div.hw,
		[CLKID_MALI_1]		    = &gxbb_mali_1.hw,
		[CLKID_MALI]		    = &gxbb_mali.hw,
		[CLKID_CTS_AMCLK]	    = &gxbb_cts_amclk.hw,
		[CLKID_CTS_AMCLK_SEL]	    = &gxbb_cts_amclk_sel.hw,
		[CLKID_CTS_AMCLK_DIV]	    = &gxbb_cts_amclk_div.hw,
		[CLKID_CTS_MCLK_I958]	    = &gxbb_cts_mclk_i958.hw,
		[CLKID_CTS_MCLK_I958_SEL]   = &gxbb_cts_mclk_i958_sel.hw,
		[CLKID_CTS_MCLK_I958_DIV]   = &gxbb_cts_mclk_i958_div.hw,
		[CLKID_CTS_I958]	    = &gxbb_cts_i958.hw,
		[CLKID_32K_CLK]		    = &gxbb_32k_clk.hw,
		[CLKID_32K_CLK_SEL]	    = &gxbb_32k_clk_sel.hw,
		[CLKID_32K_CLK_DIV]	    = &gxbb_32k_clk_div.hw,
	},
	.num = NR_CLKS,
};

/* Convenience tables to populate base addresses in .probe */

static struct meson_clk_pll *const gxbb_clk_plls[] = {
	&gxbb_fixed_pll,
	&gxbb_hdmi_pll,
	&gxbb_sys_pll,
	&gxbb_gp0_pll,
};

static struct meson_clk_pll *const gxl_clk_plls[] = {
	&gxbb_fixed_pll,
	&gxbb_hdmi_pll,
	&gxbb_sys_pll,
	&gxl_gp0_pll,
};

static struct meson_clk_mpll *const gxbb_clk_mplls[] = {
	&gxbb_mpll0,
	&gxbb_mpll1,
	&gxbb_mpll2,
};

static struct clk_gate *const gxbb_clk_gates[] = {
	&gxbb_clk81,
	&gxbb_ddr,
	&gxbb_dos,
	&gxbb_isa,
	&gxbb_pl301,
	&gxbb_periphs,
	&gxbb_spicc,
	&gxbb_i2c,
	&gxbb_sar_adc,
	&gxbb_smart_card,
	&gxbb_rng0,
	&gxbb_uart0,
	&gxbb_sdhc,
	&gxbb_stream,
	&gxbb_async_fifo,
	&gxbb_sdio,
	&gxbb_abuf,
	&gxbb_hiu_iface,
	&gxbb_assist_misc,
	&gxbb_spi,
	&gxbb_i2s_spdif,
	&gxbb_eth,
	&gxbb_demux,
	&gxbb_aiu_glue,
	&gxbb_iec958,
	&gxbb_i2s_out,
	&gxbb_amclk,
	&gxbb_aififo2,
	&gxbb_mixer,
	&gxbb_mixer_iface,
	&gxbb_adc,
	&gxbb_blkmv,
	&gxbb_aiu,
	&gxbb_uart1,
	&gxbb_g2d,
	&gxbb_usb0,
	&gxbb_usb1,
	&gxbb_reset,
	&gxbb_nand,
	&gxbb_dos_parser,
	&gxbb_usb,
	&gxbb_vdin1,
	&gxbb_ahb_arb0,
	&gxbb_efuse,
	&gxbb_boot_rom,
	&gxbb_ahb_data_bus,
	&gxbb_ahb_ctrl_bus,
	&gxbb_hdmi_intr_sync,
	&gxbb_hdmi_pclk,
	&gxbb_usb1_ddr_bridge,
	&gxbb_usb0_ddr_bridge,
	&gxbb_mmc_pclk,
	&gxbb_dvin,
	&gxbb_uart2,
	&gxbb_sana,
	&gxbb_vpu_intr,
	&gxbb_sec_ahb_ahb3_bridge,
	&gxbb_clk81_a53,
	&gxbb_vclk2_venci0,
	&gxbb_vclk2_venci1,
	&gxbb_vclk2_vencp0,
	&gxbb_vclk2_vencp1,
	&gxbb_gclk_venci_int0,
	&gxbb_gclk_vencp_int,
	&gxbb_dac_clk,
	&gxbb_aoclk_gate,
	&gxbb_iec958_gate,
	&gxbb_enc480p,
	&gxbb_rng1,
	&gxbb_gclk_venci_int1,
	&gxbb_vclk2_venclmcc,
	&gxbb_vclk2_vencl,
	&gxbb_vclk_other,
	&gxbb_edp,
	&gxbb_ao_media_cpu,
	&gxbb_ao_ahb_sram,
	&gxbb_ao_ahb_bus,
	&gxbb_ao_iface,
	&gxbb_ao_i2c,
	&gxbb_emmc_a,
	&gxbb_emmc_b,
	&gxbb_emmc_c,
	&gxbb_sar_adc_clk,
	&gxbb_mali_0,
	&gxbb_mali_1,
	&gxbb_cts_amclk,
	&gxbb_cts_mclk_i958,
	&gxbb_32k_clk,
};

static struct clk_mux *const gxbb_clk_muxes[] = {
	&gxbb_mpeg_clk_sel,
	&gxbb_sar_adc_clk_sel,
	&gxbb_mali_0_sel,
	&gxbb_mali_1_sel,
	&gxbb_mali,
	&gxbb_cts_amclk_sel,
	&gxbb_cts_mclk_i958_sel,
	&gxbb_cts_i958,
	&gxbb_32k_clk_sel,
};

static struct clk_divider *const gxbb_clk_dividers[] = {
	&gxbb_mpeg_clk_div,
	&gxbb_sar_adc_clk_div,
	&gxbb_mali_0_div,
	&gxbb_mali_1_div,
	&gxbb_cts_mclk_i958_div,
	&gxbb_32k_clk_div,
};

static struct meson_clk_audio_divider *const gxbb_audio_dividers[] = {
	&gxbb_cts_amclk_div,
};

struct clkc_data {
	struct clk_gate *const *clk_gates;
	unsigned int clk_gates_count;
	struct meson_clk_mpll *const *clk_mplls;
	unsigned int clk_mplls_count;
	struct meson_clk_pll *const *clk_plls;
	unsigned int clk_plls_count;
	struct clk_mux *const *clk_muxes;
	unsigned int clk_muxes_count;
	struct clk_divider *const *clk_dividers;
	unsigned int clk_dividers_count;
	struct meson_clk_audio_divider *const *clk_audio_dividers;
	unsigned int clk_audio_dividers_count;
	struct clk_hw_onecell_data *hw_onecell_data;
};

static const struct clkc_data gxbb_clkc_data = {
	.clk_gates = gxbb_clk_gates,
	.clk_gates_count = ARRAY_SIZE(gxbb_clk_gates),
	.clk_mplls = gxbb_clk_mplls,
	.clk_mplls_count = ARRAY_SIZE(gxbb_clk_mplls),
	.clk_plls = gxbb_clk_plls,
	.clk_plls_count = ARRAY_SIZE(gxbb_clk_plls),
	.clk_muxes = gxbb_clk_muxes,
	.clk_muxes_count = ARRAY_SIZE(gxbb_clk_muxes),
	.clk_dividers = gxbb_clk_dividers,
	.clk_dividers_count = ARRAY_SIZE(gxbb_clk_dividers),
	.clk_audio_dividers = gxbb_audio_dividers,
	.clk_audio_dividers_count = ARRAY_SIZE(gxbb_audio_dividers),
	.hw_onecell_data = &gxbb_hw_onecell_data,
};

static const struct clkc_data gxl_clkc_data = {
	.clk_gates = gxbb_clk_gates,
	.clk_gates_count = ARRAY_SIZE(gxbb_clk_gates),
	.clk_mplls = gxbb_clk_mplls,
	.clk_mplls_count = ARRAY_SIZE(gxbb_clk_mplls),
	.clk_plls = gxl_clk_plls,
	.clk_plls_count = ARRAY_SIZE(gxl_clk_plls),
	.clk_muxes = gxbb_clk_muxes,
	.clk_muxes_count = ARRAY_SIZE(gxbb_clk_muxes),
	.clk_dividers = gxbb_clk_dividers,
	.clk_dividers_count = ARRAY_SIZE(gxbb_clk_dividers),
	.clk_audio_dividers = gxbb_audio_dividers,
	.clk_audio_dividers_count = ARRAY_SIZE(gxbb_audio_dividers),
	.hw_onecell_data = &gxl_hw_onecell_data,
};

static const struct of_device_id clkc_match_table[] = {
	{ .compatible = "amlogic,gxbb-clkc", .data = &gxbb_clkc_data },
	{ .compatible = "amlogic,gxl-clkc", .data = &gxl_clkc_data },
	{},
};

static int gxbb_clkc_probe(struct platform_device *pdev)
{
	const struct clkc_data *clkc_data;
	void __iomem *clk_base;
	int ret, clkid, i;
	struct device *dev = &pdev->dev;

	clkc_data = of_device_get_match_data(&pdev->dev);
	if (!clkc_data)
		return -EINVAL;

	/*  Generic clocks and PLLs */
	clk_base = of_iomap(dev->of_node, 0);
	if (!clk_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	/* Populate base address for PLLs */
	for (i = 0; i < clkc_data->clk_plls_count; i++)
		clkc_data->clk_plls[i]->base = clk_base;

	/* Populate base address for MPLLs */
	for (i = 0; i < clkc_data->clk_mplls_count; i++)
		clkc_data->clk_mplls[i]->base = clk_base;

	/* Populate base address for gates */
	for (i = 0; i < clkc_data->clk_gates_count; i++)
		clkc_data->clk_gates[i]->reg = clk_base +
			(u64)clkc_data->clk_gates[i]->reg;

	/* Populate base address for muxes */
	for (i = 0; i < clkc_data->clk_muxes_count; i++)
		clkc_data->clk_muxes[i]->reg = clk_base +
			(u64)clkc_data->clk_muxes[i]->reg;

	/* Populate base address for dividers */
	for (i = 0; i < clkc_data->clk_dividers_count; i++)
		clkc_data->clk_dividers[i]->reg = clk_base +
			(u64)clkc_data->clk_dividers[i]->reg;

	/* Populate base address for the audio dividers */
	for (i = 0; i < clkc_data->clk_audio_dividers_count; i++)
		clkc_data->clk_audio_dividers[i]->base = clk_base;

	/*
	 * register all clks
	 */
	for (clkid = 0; clkid < clkc_data->hw_onecell_data->num; clkid++) {
		/* array might be sparse */
		if (!clkc_data->hw_onecell_data->hws[clkid])
			continue;

		ret = devm_clk_hw_register(dev,
					clkc_data->hw_onecell_data->hws[clkid]);
		if (ret)
			goto iounmap;
	}

	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
			clkc_data->hw_onecell_data);

iounmap:
	iounmap(clk_base);
	return ret;
}

static struct platform_driver gxbb_driver = {
	.probe		= gxbb_clkc_probe,
	.driver		= {
		.name	= "gxbb-clkc",
		.of_match_table = clkc_match_table,
	},
};

builtin_platform_driver(gxbb_driver);
