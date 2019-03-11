// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "clkc-audio.h"
#include "axg-audio.h"

#define AXG_MST_IN_COUNT	8
#define AXG_SLV_SCLK_COUNT	10
#define AXG_SLV_LRCLK_COUNT	10

#define AXG_AUD_GATE(_name, _reg, _bit, _pname, _iflags)		\
struct clk_regmap axg_##_name = {					\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "axg_"#_name,					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_names = (const char *[]){ _pname },		\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AXG_AUD_MUX(_name, _reg, _mask, _shift, _dflags, _pnames, _iflags) \
struct clk_regmap axg_##_name = {					\
	.data = &(struct clk_regmap_mux_data){				\
		.offset = (_reg),					\
		.mask = (_mask),					\
		.shift = (_shift),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = "axg_"#_name,					\
		.ops = &clk_regmap_mux_ops,				\
		.parent_names = (_pnames),				\
		.num_parents = ARRAY_SIZE(_pnames),			\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AXG_AUD_DIV(_name, _reg, _shift, _width, _dflags, _pname, _iflags) \
struct clk_regmap axg_##_name = {					\
	.data = &(struct clk_regmap_div_data){				\
		.offset = (_reg),					\
		.shift = (_shift),					\
		.width = (_width),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = "axg_"#_name,					\
		.ops = &clk_regmap_divider_ops,				\
		.parent_names = (const char *[]) { _pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AXG_PCLK_GATE(_name, _bit)				\
	AXG_AUD_GATE(_name, AUDIO_CLK_GATE_EN, _bit, "axg_audio_pclk", 0)

/* Audio peripheral clocks */
static AXG_PCLK_GATE(ddr_arb,	   0);
static AXG_PCLK_GATE(pdm,	   1);
static AXG_PCLK_GATE(tdmin_a,	   2);
static AXG_PCLK_GATE(tdmin_b,	   3);
static AXG_PCLK_GATE(tdmin_c,	   4);
static AXG_PCLK_GATE(tdmin_lb,	   5);
static AXG_PCLK_GATE(tdmout_a,	   6);
static AXG_PCLK_GATE(tdmout_b,	   7);
static AXG_PCLK_GATE(tdmout_c,	   8);
static AXG_PCLK_GATE(frddr_a,	   9);
static AXG_PCLK_GATE(frddr_b,	   10);
static AXG_PCLK_GATE(frddr_c,	   11);
static AXG_PCLK_GATE(toddr_a,	   12);
static AXG_PCLK_GATE(toddr_b,	   13);
static AXG_PCLK_GATE(toddr_c,	   14);
static AXG_PCLK_GATE(loopback,	   15);
static AXG_PCLK_GATE(spdifin,	   16);
static AXG_PCLK_GATE(spdifout,	   17);
static AXG_PCLK_GATE(resample,	   18);
static AXG_PCLK_GATE(power_detect, 19);

/* Audio Master Clocks */
static const char * const mst_mux_parent_names[] = {
	"axg_mst_in0", "axg_mst_in1", "axg_mst_in2", "axg_mst_in3",
	"axg_mst_in4", "axg_mst_in5", "axg_mst_in6", "axg_mst_in7",
};

#define AXG_MST_MUX(_name, _reg, _flag)				\
	AXG_AUD_MUX(_name##_sel, _reg, 0x7, 24, _flag,		\
		    mst_mux_parent_names, CLK_SET_RATE_PARENT)

#define AXG_MST_MCLK_MUX(_name, _reg)				\
	AXG_MST_MUX(_name, _reg, CLK_MUX_ROUND_CLOSEST)

#define AXG_MST_SYS_MUX(_name, _reg)				\
	AXG_MST_MUX(_name, _reg, 0)

static AXG_MST_MCLK_MUX(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AXG_MST_MCLK_MUX(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AXG_MST_MCLK_MUX(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AXG_MST_MCLK_MUX(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AXG_MST_MCLK_MUX(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AXG_MST_MCLK_MUX(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AXG_MST_MCLK_MUX(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AXG_MST_MCLK_MUX(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AXG_MST_SYS_MUX(spdifin_clk,   AUDIO_CLK_SPDIFIN_CTRL);
static AXG_MST_SYS_MUX(pdm_sysclk,    AUDIO_CLK_PDMIN_CTRL1);

#define AXG_MST_DIV(_name, _reg, _flag)				\
	AXG_AUD_DIV(_name##_div, _reg, 0, 16, _flag,		\
		    "axg_"#_name"_sel", CLK_SET_RATE_PARENT)	\

#define AXG_MST_MCLK_DIV(_name, _reg)				\
	AXG_MST_DIV(_name, _reg, CLK_DIVIDER_ROUND_CLOSEST)

#define AXG_MST_SYS_DIV(_name, _reg)				\
	AXG_MST_DIV(_name, _reg, 0)

static AXG_MST_MCLK_DIV(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AXG_MST_MCLK_DIV(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AXG_MST_MCLK_DIV(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AXG_MST_MCLK_DIV(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AXG_MST_MCLK_DIV(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AXG_MST_MCLK_DIV(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AXG_MST_MCLK_DIV(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AXG_MST_MCLK_DIV(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AXG_MST_SYS_DIV(spdifin_clk,   AUDIO_CLK_SPDIFIN_CTRL);
static AXG_MST_SYS_DIV(pdm_sysclk,    AUDIO_CLK_PDMIN_CTRL1);

#define AXG_MST_MCLK_GATE(_name, _reg)				\
	AXG_AUD_GATE(_name, _reg, 31,  "axg_"#_name"_div",	\
		     CLK_SET_RATE_PARENT)

static AXG_MST_MCLK_GATE(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AXG_MST_MCLK_GATE(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AXG_MST_MCLK_GATE(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AXG_MST_MCLK_GATE(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AXG_MST_MCLK_GATE(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AXG_MST_MCLK_GATE(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AXG_MST_MCLK_GATE(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AXG_MST_MCLK_GATE(spdifin_clk,  AUDIO_CLK_SPDIFIN_CTRL);
static AXG_MST_MCLK_GATE(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AXG_MST_MCLK_GATE(pdm_sysclk,   AUDIO_CLK_PDMIN_CTRL1);

/* Sample Clocks */
#define AXG_MST_SCLK_PRE_EN(_name, _reg)			\
	AXG_AUD_GATE(mst_##_name##_sclk_pre_en, _reg, 31,	\
		     "axg_mst_"#_name"_mclk", 0)

static AXG_MST_SCLK_PRE_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static AXG_MST_SCLK_PRE_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static AXG_MST_SCLK_PRE_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static AXG_MST_SCLK_PRE_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static AXG_MST_SCLK_PRE_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static AXG_MST_SCLK_PRE_EN(f, AUDIO_MST_F_SCLK_CTRL0);

#define AXG_AUD_SCLK_DIV(_name, _reg, _div_shift, _div_width,		\
			 _hi_shift, _hi_width, _pname, _iflags)		\
struct clk_regmap axg_##_name = {					\
	.data = &(struct meson_sclk_div_data) {				\
		.div = {						\
			.reg_off = (_reg),				\
			.shift   = (_div_shift),			\
			.width   = (_div_width),			\
		},							\
		.hi = {							\
			.reg_off = (_reg),				\
			.shift   = (_hi_shift),				\
			.width   = (_hi_width),				\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "axg_"#_name,					\
		.ops = &meson_sclk_div_ops,				\
		.parent_names = (const char *[]) { _pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AXG_MST_SCLK_DIV(_name, _reg)					\
	AXG_AUD_SCLK_DIV(mst_##_name##_sclk_div, _reg, 20, 10, 0, 0,	\
			 "axg_mst_"#_name"_sclk_pre_en",		\
			 CLK_SET_RATE_PARENT)

static AXG_MST_SCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static AXG_MST_SCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static AXG_MST_SCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static AXG_MST_SCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static AXG_MST_SCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static AXG_MST_SCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

#define AXG_MST_SCLK_POST_EN(_name, _reg)				\
	AXG_AUD_GATE(mst_##_name##_sclk_post_en, _reg, 30,		\
		     "axg_mst_"#_name"_sclk_div", CLK_SET_RATE_PARENT)

static AXG_MST_SCLK_POST_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static AXG_MST_SCLK_POST_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static AXG_MST_SCLK_POST_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static AXG_MST_SCLK_POST_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static AXG_MST_SCLK_POST_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static AXG_MST_SCLK_POST_EN(f, AUDIO_MST_F_SCLK_CTRL0);

#define AXG_AUD_TRIPHASE(_name, _reg, _width, _shift0, _shift1, _shift2, \
			 _pname, _iflags)				\
struct clk_regmap axg_##_name = {					\
	.data = &(struct meson_clk_triphase_data) {			\
		.ph0 = {						\
			.reg_off = (_reg),				\
			.shift   = (_shift0),				\
			.width   = (_width),				\
		},							\
		.ph1 = {						\
			.reg_off = (_reg),				\
			.shift   = (_shift1),				\
			.width   = (_width),				\
		},							\
		.ph2 = {						\
			.reg_off = (_reg),				\
			.shift   = (_shift2),				\
			.width   = (_width),				\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "axg_"#_name,					\
		.ops = &meson_clk_triphase_ops,				\
		.parent_names = (const char *[]) { _pname },		\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AXG_MST_SCLK(_name, _reg)					\
	AXG_AUD_TRIPHASE(mst_##_name##_sclk, _reg, 1, 0, 2, 4,		\
			 "axg_mst_"#_name"_sclk_post_en", CLK_SET_RATE_PARENT)

static AXG_MST_SCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static AXG_MST_SCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static AXG_MST_SCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static AXG_MST_SCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static AXG_MST_SCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static AXG_MST_SCLK(f, AUDIO_MST_F_SCLK_CTRL1);

#define AXG_MST_LRCLK_DIV(_name, _reg)					\
	AXG_AUD_SCLK_DIV(mst_##_name##_lrclk_div, _reg, 0, 10, 10, 10,	\
		    "axg_mst_"#_name"_sclk_post_en", 0)			\

static AXG_MST_LRCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static AXG_MST_LRCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static AXG_MST_LRCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static AXG_MST_LRCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static AXG_MST_LRCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static AXG_MST_LRCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

#define AXG_MST_LRCLK(_name, _reg)					\
	AXG_AUD_TRIPHASE(mst_##_name##_lrclk, _reg, 1, 1, 3, 5,		\
			 "axg_mst_"#_name"_lrclk_div", CLK_SET_RATE_PARENT)

static AXG_MST_LRCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static AXG_MST_LRCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static AXG_MST_LRCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static AXG_MST_LRCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static AXG_MST_LRCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static AXG_MST_LRCLK(f, AUDIO_MST_F_SCLK_CTRL1);

static const char * const tdm_sclk_parent_names[] = {
	"axg_mst_a_sclk", "axg_mst_b_sclk", "axg_mst_c_sclk",
	"axg_mst_d_sclk", "axg_mst_e_sclk", "axg_mst_f_sclk",
	"axg_slv_sclk0", "axg_slv_sclk1", "axg_slv_sclk2",
	"axg_slv_sclk3", "axg_slv_sclk4", "axg_slv_sclk5",
	"axg_slv_sclk6", "axg_slv_sclk7", "axg_slv_sclk8",
	"axg_slv_sclk9"
};

#define AXG_TDM_SCLK_MUX(_name, _reg)				\
	AXG_AUD_MUX(tdm##_name##_sclk_sel, _reg, 0xf, 24,	\
		    CLK_MUX_ROUND_CLOSEST,			\
		    tdm_sclk_parent_names, 0)

static AXG_TDM_SCLK_MUX(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AXG_TDM_SCLK_MUX(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AXG_TDM_SCLK_MUX(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AXG_TDM_SCLK_MUX(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AXG_TDM_SCLK_MUX(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AXG_TDM_SCLK_MUX(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AXG_TDM_SCLK_MUX(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AXG_TDM_SCLK_PRE_EN(_name, _reg)				\
	AXG_AUD_GATE(tdm##_name##_sclk_pre_en, _reg, 31,		\
		     "axg_tdm"#_name"_sclk_sel", CLK_SET_RATE_PARENT)

static AXG_TDM_SCLK_PRE_EN(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AXG_TDM_SCLK_PRE_EN(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AXG_TDM_SCLK_PRE_EN(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AXG_TDM_SCLK_PRE_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AXG_TDM_SCLK_PRE_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AXG_TDM_SCLK_PRE_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AXG_TDM_SCLK_PRE_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AXG_TDM_SCLK_POST_EN(_name, _reg)				\
	AXG_AUD_GATE(tdm##_name##_sclk_post_en, _reg, 30,		\
		     "axg_tdm"#_name"_sclk_pre_en", CLK_SET_RATE_PARENT)

static AXG_TDM_SCLK_POST_EN(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AXG_TDM_SCLK_POST_EN(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AXG_TDM_SCLK_POST_EN(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AXG_TDM_SCLK_POST_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AXG_TDM_SCLK_POST_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AXG_TDM_SCLK_POST_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AXG_TDM_SCLK_POST_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AXG_TDM_SCLK(_name, _reg)					\
	struct clk_regmap axg_tdm##_name##_sclk = {			\
	.data = &(struct meson_clk_phase_data) {			\
		.ph = {							\
			.reg_off = (_reg),				\
			.shift   = 29,					\
			.width   = 1,					\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "axg_tdm"#_name"_sclk",				\
		.ops = &meson_clk_phase_ops,				\
		.parent_names = (const char *[])			\
		{ "axg_tdm"#_name"_sclk_post_en" },			\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | CLK_SET_RATE_PARENT,	\
	},								\
}

static AXG_TDM_SCLK(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AXG_TDM_SCLK(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AXG_TDM_SCLK(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AXG_TDM_SCLK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AXG_TDM_SCLK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AXG_TDM_SCLK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AXG_TDM_SCLK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static const char * const tdm_lrclk_parent_names[] = {
	"axg_mst_a_lrclk", "axg_mst_b_lrclk", "axg_mst_c_lrclk",
	"axg_mst_d_lrclk", "axg_mst_e_lrclk", "axg_mst_f_lrclk",
	"axg_slv_lrclk0", "axg_slv_lrclk1", "axg_slv_lrclk2",
	"axg_slv_lrclk3", "axg_slv_lrclk4", "axg_slv_lrclk5",
	"axg_slv_lrclk6", "axg_slv_lrclk7", "axg_slv_lrclk8",
	"axg_slv_lrclk9"
};

#define AXG_TDM_LRLCK(_name, _reg)		       \
	AXG_AUD_MUX(tdm##_name##_lrclk, _reg, 0xf, 20, \
		    CLK_MUX_ROUND_CLOSEST,	       \
		    tdm_lrclk_parent_names, 0)

static AXG_TDM_LRLCK(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AXG_TDM_LRLCK(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AXG_TDM_LRLCK(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AXG_TDM_LRLCK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AXG_TDM_LRLCK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AXG_TDM_LRLCK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AXG_TDM_LRLCK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

/*
 * Array of all clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw_onecell_data axg_audio_hw_onecell_data = {
	.hws = {
		[AUD_CLKID_DDR_ARB]		= &axg_ddr_arb.hw,
		[AUD_CLKID_PDM]			= &axg_pdm.hw,
		[AUD_CLKID_TDMIN_A]		= &axg_tdmin_a.hw,
		[AUD_CLKID_TDMIN_B]		= &axg_tdmin_b.hw,
		[AUD_CLKID_TDMIN_C]		= &axg_tdmin_c.hw,
		[AUD_CLKID_TDMIN_LB]		= &axg_tdmin_lb.hw,
		[AUD_CLKID_TDMOUT_A]		= &axg_tdmout_a.hw,
		[AUD_CLKID_TDMOUT_B]		= &axg_tdmout_b.hw,
		[AUD_CLKID_TDMOUT_C]		= &axg_tdmout_c.hw,
		[AUD_CLKID_FRDDR_A]		= &axg_frddr_a.hw,
		[AUD_CLKID_FRDDR_B]		= &axg_frddr_b.hw,
		[AUD_CLKID_FRDDR_C]		= &axg_frddr_c.hw,
		[AUD_CLKID_TODDR_A]		= &axg_toddr_a.hw,
		[AUD_CLKID_TODDR_B]		= &axg_toddr_b.hw,
		[AUD_CLKID_TODDR_C]		= &axg_toddr_c.hw,
		[AUD_CLKID_LOOPBACK]		= &axg_loopback.hw,
		[AUD_CLKID_SPDIFIN]		= &axg_spdifin.hw,
		[AUD_CLKID_SPDIFOUT]		= &axg_spdifout.hw,
		[AUD_CLKID_RESAMPLE]		= &axg_resample.hw,
		[AUD_CLKID_POWER_DETECT]	= &axg_power_detect.hw,
		[AUD_CLKID_MST_A_MCLK_SEL]	= &axg_mst_a_mclk_sel.hw,
		[AUD_CLKID_MST_B_MCLK_SEL]	= &axg_mst_b_mclk_sel.hw,
		[AUD_CLKID_MST_C_MCLK_SEL]	= &axg_mst_c_mclk_sel.hw,
		[AUD_CLKID_MST_D_MCLK_SEL]	= &axg_mst_d_mclk_sel.hw,
		[AUD_CLKID_MST_E_MCLK_SEL]	= &axg_mst_e_mclk_sel.hw,
		[AUD_CLKID_MST_F_MCLK_SEL]	= &axg_mst_f_mclk_sel.hw,
		[AUD_CLKID_MST_A_MCLK_DIV]	= &axg_mst_a_mclk_div.hw,
		[AUD_CLKID_MST_B_MCLK_DIV]	= &axg_mst_b_mclk_div.hw,
		[AUD_CLKID_MST_C_MCLK_DIV]	= &axg_mst_c_mclk_div.hw,
		[AUD_CLKID_MST_D_MCLK_DIV]	= &axg_mst_d_mclk_div.hw,
		[AUD_CLKID_MST_E_MCLK_DIV]	= &axg_mst_e_mclk_div.hw,
		[AUD_CLKID_MST_F_MCLK_DIV]	= &axg_mst_f_mclk_div.hw,
		[AUD_CLKID_MST_A_MCLK]		= &axg_mst_a_mclk.hw,
		[AUD_CLKID_MST_B_MCLK]		= &axg_mst_b_mclk.hw,
		[AUD_CLKID_MST_C_MCLK]		= &axg_mst_c_mclk.hw,
		[AUD_CLKID_MST_D_MCLK]		= &axg_mst_d_mclk.hw,
		[AUD_CLKID_MST_E_MCLK]		= &axg_mst_e_mclk.hw,
		[AUD_CLKID_MST_F_MCLK]		= &axg_mst_f_mclk.hw,
		[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &axg_spdifout_clk_sel.hw,
		[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &axg_spdifout_clk_div.hw,
		[AUD_CLKID_SPDIFOUT_CLK]	= &axg_spdifout_clk.hw,
		[AUD_CLKID_SPDIFIN_CLK_SEL]	= &axg_spdifin_clk_sel.hw,
		[AUD_CLKID_SPDIFIN_CLK_DIV]	= &axg_spdifin_clk_div.hw,
		[AUD_CLKID_SPDIFIN_CLK]		= &axg_spdifin_clk.hw,
		[AUD_CLKID_PDM_DCLK_SEL]	= &axg_pdm_dclk_sel.hw,
		[AUD_CLKID_PDM_DCLK_DIV]	= &axg_pdm_dclk_div.hw,
		[AUD_CLKID_PDM_DCLK]		= &axg_pdm_dclk.hw,
		[AUD_CLKID_PDM_SYSCLK_SEL]	= &axg_pdm_sysclk_sel.hw,
		[AUD_CLKID_PDM_SYSCLK_DIV]	= &axg_pdm_sysclk_div.hw,
		[AUD_CLKID_PDM_SYSCLK]		= &axg_pdm_sysclk.hw,
		[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &axg_mst_a_sclk_pre_en.hw,
		[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &axg_mst_b_sclk_pre_en.hw,
		[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &axg_mst_c_sclk_pre_en.hw,
		[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &axg_mst_d_sclk_pre_en.hw,
		[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &axg_mst_e_sclk_pre_en.hw,
		[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &axg_mst_f_sclk_pre_en.hw,
		[AUD_CLKID_MST_A_SCLK_DIV]	= &axg_mst_a_sclk_div.hw,
		[AUD_CLKID_MST_B_SCLK_DIV]	= &axg_mst_b_sclk_div.hw,
		[AUD_CLKID_MST_C_SCLK_DIV]	= &axg_mst_c_sclk_div.hw,
		[AUD_CLKID_MST_D_SCLK_DIV]	= &axg_mst_d_sclk_div.hw,
		[AUD_CLKID_MST_E_SCLK_DIV]	= &axg_mst_e_sclk_div.hw,
		[AUD_CLKID_MST_F_SCLK_DIV]	= &axg_mst_f_sclk_div.hw,
		[AUD_CLKID_MST_A_SCLK_POST_EN]	= &axg_mst_a_sclk_post_en.hw,
		[AUD_CLKID_MST_B_SCLK_POST_EN]	= &axg_mst_b_sclk_post_en.hw,
		[AUD_CLKID_MST_C_SCLK_POST_EN]	= &axg_mst_c_sclk_post_en.hw,
		[AUD_CLKID_MST_D_SCLK_POST_EN]	= &axg_mst_d_sclk_post_en.hw,
		[AUD_CLKID_MST_E_SCLK_POST_EN]	= &axg_mst_e_sclk_post_en.hw,
		[AUD_CLKID_MST_F_SCLK_POST_EN]	= &axg_mst_f_sclk_post_en.hw,
		[AUD_CLKID_MST_A_SCLK]		= &axg_mst_a_sclk.hw,
		[AUD_CLKID_MST_B_SCLK]		= &axg_mst_b_sclk.hw,
		[AUD_CLKID_MST_C_SCLK]		= &axg_mst_c_sclk.hw,
		[AUD_CLKID_MST_D_SCLK]		= &axg_mst_d_sclk.hw,
		[AUD_CLKID_MST_E_SCLK]		= &axg_mst_e_sclk.hw,
		[AUD_CLKID_MST_F_SCLK]		= &axg_mst_f_sclk.hw,
		[AUD_CLKID_MST_A_LRCLK_DIV]	= &axg_mst_a_lrclk_div.hw,
		[AUD_CLKID_MST_B_LRCLK_DIV]	= &axg_mst_b_lrclk_div.hw,
		[AUD_CLKID_MST_C_LRCLK_DIV]	= &axg_mst_c_lrclk_div.hw,
		[AUD_CLKID_MST_D_LRCLK_DIV]	= &axg_mst_d_lrclk_div.hw,
		[AUD_CLKID_MST_E_LRCLK_DIV]	= &axg_mst_e_lrclk_div.hw,
		[AUD_CLKID_MST_F_LRCLK_DIV]	= &axg_mst_f_lrclk_div.hw,
		[AUD_CLKID_MST_A_LRCLK]		= &axg_mst_a_lrclk.hw,
		[AUD_CLKID_MST_B_LRCLK]		= &axg_mst_b_lrclk.hw,
		[AUD_CLKID_MST_C_LRCLK]		= &axg_mst_c_lrclk.hw,
		[AUD_CLKID_MST_D_LRCLK]		= &axg_mst_d_lrclk.hw,
		[AUD_CLKID_MST_E_LRCLK]		= &axg_mst_e_lrclk.hw,
		[AUD_CLKID_MST_F_LRCLK]		= &axg_mst_f_lrclk.hw,
		[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &axg_tdmin_a_sclk_sel.hw,
		[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &axg_tdmin_b_sclk_sel.hw,
		[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &axg_tdmin_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &axg_tdmin_lb_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &axg_tdmout_a_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &axg_tdmout_b_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &axg_tdmout_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &axg_tdmin_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &axg_tdmin_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &axg_tdmin_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &axg_tdmin_lb_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &axg_tdmout_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &axg_tdmout_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &axg_tdmout_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &axg_tdmin_a_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &axg_tdmin_b_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &axg_tdmin_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &axg_tdmin_lb_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &axg_tdmout_a_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &axg_tdmout_b_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &axg_tdmout_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK]	= &axg_tdmin_a_sclk.hw,
		[AUD_CLKID_TDMIN_B_SCLK]	= &axg_tdmin_b_sclk.hw,
		[AUD_CLKID_TDMIN_C_SCLK]	= &axg_tdmin_c_sclk.hw,
		[AUD_CLKID_TDMIN_LB_SCLK]	= &axg_tdmin_lb_sclk.hw,
		[AUD_CLKID_TDMOUT_A_SCLK]	= &axg_tdmout_a_sclk.hw,
		[AUD_CLKID_TDMOUT_B_SCLK]	= &axg_tdmout_b_sclk.hw,
		[AUD_CLKID_TDMOUT_C_SCLK]	= &axg_tdmout_c_sclk.hw,
		[AUD_CLKID_TDMIN_A_LRCLK]	= &axg_tdmin_a_lrclk.hw,
		[AUD_CLKID_TDMIN_B_LRCLK]	= &axg_tdmin_b_lrclk.hw,
		[AUD_CLKID_TDMIN_C_LRCLK]	= &axg_tdmin_c_lrclk.hw,
		[AUD_CLKID_TDMIN_LB_LRCLK]	= &axg_tdmin_lb_lrclk.hw,
		[AUD_CLKID_TDMOUT_A_LRCLK]	= &axg_tdmout_a_lrclk.hw,
		[AUD_CLKID_TDMOUT_B_LRCLK]	= &axg_tdmout_b_lrclk.hw,
		[AUD_CLKID_TDMOUT_C_LRCLK]	= &axg_tdmout_c_lrclk.hw,
		[NR_CLKS] = NULL,
	},
	.num = NR_CLKS,
};

/* Convenience table to populate regmap in .probe() */
static struct clk_regmap *const axg_audio_clk_regmaps[] = {
	&axg_ddr_arb,
	&axg_pdm,
	&axg_tdmin_a,
	&axg_tdmin_b,
	&axg_tdmin_c,
	&axg_tdmin_lb,
	&axg_tdmout_a,
	&axg_tdmout_b,
	&axg_tdmout_c,
	&axg_frddr_a,
	&axg_frddr_b,
	&axg_frddr_c,
	&axg_toddr_a,
	&axg_toddr_b,
	&axg_toddr_c,
	&axg_loopback,
	&axg_spdifin,
	&axg_spdifout,
	&axg_resample,
	&axg_power_detect,
	&axg_mst_a_mclk_sel,
	&axg_mst_b_mclk_sel,
	&axg_mst_c_mclk_sel,
	&axg_mst_d_mclk_sel,
	&axg_mst_e_mclk_sel,
	&axg_mst_f_mclk_sel,
	&axg_mst_a_mclk_div,
	&axg_mst_b_mclk_div,
	&axg_mst_c_mclk_div,
	&axg_mst_d_mclk_div,
	&axg_mst_e_mclk_div,
	&axg_mst_f_mclk_div,
	&axg_mst_a_mclk,
	&axg_mst_b_mclk,
	&axg_mst_c_mclk,
	&axg_mst_d_mclk,
	&axg_mst_e_mclk,
	&axg_mst_f_mclk,
	&axg_spdifout_clk_sel,
	&axg_spdifout_clk_div,
	&axg_spdifout_clk,
	&axg_spdifin_clk_sel,
	&axg_spdifin_clk_div,
	&axg_spdifin_clk,
	&axg_pdm_dclk_sel,
	&axg_pdm_dclk_div,
	&axg_pdm_dclk,
	&axg_pdm_sysclk_sel,
	&axg_pdm_sysclk_div,
	&axg_pdm_sysclk,
	&axg_mst_a_sclk_pre_en,
	&axg_mst_b_sclk_pre_en,
	&axg_mst_c_sclk_pre_en,
	&axg_mst_d_sclk_pre_en,
	&axg_mst_e_sclk_pre_en,
	&axg_mst_f_sclk_pre_en,
	&axg_mst_a_sclk_div,
	&axg_mst_b_sclk_div,
	&axg_mst_c_sclk_div,
	&axg_mst_d_sclk_div,
	&axg_mst_e_sclk_div,
	&axg_mst_f_sclk_div,
	&axg_mst_a_sclk_post_en,
	&axg_mst_b_sclk_post_en,
	&axg_mst_c_sclk_post_en,
	&axg_mst_d_sclk_post_en,
	&axg_mst_e_sclk_post_en,
	&axg_mst_f_sclk_post_en,
	&axg_mst_a_sclk,
	&axg_mst_b_sclk,
	&axg_mst_c_sclk,
	&axg_mst_d_sclk,
	&axg_mst_e_sclk,
	&axg_mst_f_sclk,
	&axg_mst_a_lrclk_div,
	&axg_mst_b_lrclk_div,
	&axg_mst_c_lrclk_div,
	&axg_mst_d_lrclk_div,
	&axg_mst_e_lrclk_div,
	&axg_mst_f_lrclk_div,
	&axg_mst_a_lrclk,
	&axg_mst_b_lrclk,
	&axg_mst_c_lrclk,
	&axg_mst_d_lrclk,
	&axg_mst_e_lrclk,
	&axg_mst_f_lrclk,
	&axg_tdmin_a_sclk_sel,
	&axg_tdmin_b_sclk_sel,
	&axg_tdmin_c_sclk_sel,
	&axg_tdmin_lb_sclk_sel,
	&axg_tdmout_a_sclk_sel,
	&axg_tdmout_b_sclk_sel,
	&axg_tdmout_c_sclk_sel,
	&axg_tdmin_a_sclk_pre_en,
	&axg_tdmin_b_sclk_pre_en,
	&axg_tdmin_c_sclk_pre_en,
	&axg_tdmin_lb_sclk_pre_en,
	&axg_tdmout_a_sclk_pre_en,
	&axg_tdmout_b_sclk_pre_en,
	&axg_tdmout_c_sclk_pre_en,
	&axg_tdmin_a_sclk_post_en,
	&axg_tdmin_b_sclk_post_en,
	&axg_tdmin_c_sclk_post_en,
	&axg_tdmin_lb_sclk_post_en,
	&axg_tdmout_a_sclk_post_en,
	&axg_tdmout_b_sclk_post_en,
	&axg_tdmout_c_sclk_post_en,
	&axg_tdmin_a_sclk,
	&axg_tdmin_b_sclk,
	&axg_tdmin_c_sclk,
	&axg_tdmin_lb_sclk,
	&axg_tdmout_a_sclk,
	&axg_tdmout_b_sclk,
	&axg_tdmout_c_sclk,
	&axg_tdmin_a_lrclk,
	&axg_tdmin_b_lrclk,
	&axg_tdmin_c_lrclk,
	&axg_tdmin_lb_lrclk,
	&axg_tdmout_a_lrclk,
	&axg_tdmout_b_lrclk,
	&axg_tdmout_c_lrclk,
};

static int devm_clk_get_enable(struct device *dev, char *id)
{
	struct clk *clk;
	int ret;

	clk = devm_clk_get(dev, id);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get %s", id);
		return ret;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(dev, "failed to enable %s", id);
		return ret;
	}

	ret = devm_add_action_or_reset(dev,
				       (void(*)(void *))clk_disable_unprepare,
				       clk);
	if (ret) {
		dev_err(dev, "failed to add reset action on %s", id);
		return ret;
	}

	return 0;
}

static int axg_register_clk_hw_input(struct device *dev,
				     const char *name,
				     unsigned int clkid)
{
	char *clk_name;
	struct clk_hw *hw;
	int err = 0;

	clk_name = kasprintf(GFP_KERNEL, "axg_%s", name);
	if (!clk_name)
		return -ENOMEM;

	hw = meson_clk_hw_register_input(dev, name, clk_name, 0);
	if (IS_ERR(hw)) {
		/* It is ok if an input clock is missing */
		if (PTR_ERR(hw) == -ENOENT) {
			dev_dbg(dev, "%s not provided", name);
		} else {
			err = PTR_ERR(hw);
			if (err != -EPROBE_DEFER)
				dev_err(dev, "failed to get %s clock", name);
		}
	} else {
		axg_audio_hw_onecell_data.hws[clkid] = hw;
	}

	kfree(clk_name);
	return err;
}

static int axg_register_clk_hw_inputs(struct device *dev,
				      const char *basename,
				      unsigned int count,
				      unsigned int clkid)
{
	char *name;
	int i, ret;

	for (i = 0; i < count; i++) {
		name = kasprintf(GFP_KERNEL, "%s%d", basename, i);
		if (!name)
			return -ENOMEM;

		ret = axg_register_clk_hw_input(dev, name, clkid + i);
		kfree(name);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct regmap_config axg_audio_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= AUDIO_CLK_PDMIN_CTRL1,
};

static int axg_audio_clkc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *map;
	struct resource *res;
	void __iomem *regs;
	struct clk_hw *hw;
	int ret, i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &axg_audio_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n", PTR_ERR(map));
		return PTR_ERR(map);
	}

	/* Get the mandatory peripheral clock */
	ret = devm_clk_get_enable(dev, "pclk");
	if (ret)
		return ret;

	ret = device_reset(dev);
	if (ret) {
		dev_err(dev, "failed to reset device\n");
		return ret;
	}

	/* Register the peripheral input clock */
	hw = meson_clk_hw_register_input(dev, "pclk", "axg_audio_pclk", 0);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	axg_audio_hw_onecell_data.hws[AUD_CLKID_PCLK] = hw;

	/* Register optional input master clocks */
	ret = axg_register_clk_hw_inputs(dev, "mst_in",
					 AXG_MST_IN_COUNT,
					 AUD_CLKID_MST0);
	if (ret)
		return ret;

	/* Register optional input slave sclks */
	ret = axg_register_clk_hw_inputs(dev, "slv_sclk",
					 AXG_SLV_SCLK_COUNT,
					 AUD_CLKID_SLV_SCLK0);
	if (ret)
		return ret;

	/* Register optional input slave lrclks */
	ret = axg_register_clk_hw_inputs(dev, "slv_lrclk",
					 AXG_SLV_LRCLK_COUNT,
					 AUD_CLKID_SLV_LRCLK0);
	if (ret)
		return ret;

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(axg_audio_clk_regmaps); i++)
		axg_audio_clk_regmaps[i]->map = map;

	/* Take care to skip the registered input clocks */
	for (i = AUD_CLKID_DDR_ARB; i < axg_audio_hw_onecell_data.num; i++) {
		hw = axg_audio_hw_onecell_data.hws[i];
		/* array might be sparse */
		if (!hw)
			continue;

		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "failed to register clock %s\n",
				hw->init->name);
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &axg_audio_hw_onecell_data);
}

static const struct of_device_id clkc_match_table[] = {
	{ .compatible = "amlogic,axg-audio-clkc" },
	{}
};
MODULE_DEVICE_TABLE(of, clkc_match_table);

static struct platform_driver axg_audio_driver = {
	.probe		= axg_audio_clkc_probe,
	.driver		= {
		.name	= "axg-audio-clkc",
		.of_match_table = clkc_match_table,
	},
};
module_platform_driver(axg_audio_driver);

MODULE_DESCRIPTION("Amlogic A113x Audio Clock driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
