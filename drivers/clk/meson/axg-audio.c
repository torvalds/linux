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
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include "axg-audio.h"
#include "clk-regmap.h"
#include "clk-phase.h"
#include "sclk-div.h"

#define AUD_MST_IN_COUNT	8
#define AUD_SLV_SCLK_COUNT	10
#define AUD_SLV_LRCLK_COUNT	10

#define AUD_GATE(_name, _reg, _bit, _phws, _iflags)			\
struct clk_regmap aud_##_name = {					\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_hws = (const struct clk_hw *[]) { &_phws.hw },	\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AUD_MUX(_name, _reg, _mask, _shift, _dflags, _pdata, _iflags)	\
struct clk_regmap aud_##_name = {					\
	.data = &(struct clk_regmap_mux_data){				\
		.offset = (_reg),					\
		.mask = (_mask),					\
		.shift = (_shift),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_mux_ops,				\
		.parent_data = _pdata,					\
		.num_parents = ARRAY_SIZE(_pdata),			\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AUD_DIV(_name, _reg, _shift, _width, _dflags, _phws, _iflags)	\
struct clk_regmap aud_##_name = {					\
	.data = &(struct clk_regmap_div_data){				\
		.offset = (_reg),					\
		.shift = (_shift),					\
		.width = (_width),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_divider_ops,				\
		.parent_hws = (const struct clk_hw *[]) { &_phws.hw },	\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AUD_PCLK_GATE(_name, _bit)				\
struct clk_regmap aud_##_name = {					\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (AUDIO_CLK_GATE_EN),				\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_data = &(const struct clk_parent_data) {	\
			.fw_name = "pclk",				\
		},							\
		.num_parents = 1,					\
	},								\
}
/* Audio peripheral clocks */
static AUD_PCLK_GATE(ddr_arb,	   0);
static AUD_PCLK_GATE(pdm,	   1);
static AUD_PCLK_GATE(tdmin_a,	   2);
static AUD_PCLK_GATE(tdmin_b,	   3);
static AUD_PCLK_GATE(tdmin_c,	   4);
static AUD_PCLK_GATE(tdmin_lb,	   5);
static AUD_PCLK_GATE(tdmout_a,	   6);
static AUD_PCLK_GATE(tdmout_b,	   7);
static AUD_PCLK_GATE(tdmout_c,	   8);
static AUD_PCLK_GATE(frddr_a,	   9);
static AUD_PCLK_GATE(frddr_b,	   10);
static AUD_PCLK_GATE(frddr_c,	   11);
static AUD_PCLK_GATE(toddr_a,	   12);
static AUD_PCLK_GATE(toddr_b,	   13);
static AUD_PCLK_GATE(toddr_c,	   14);
static AUD_PCLK_GATE(loopback,	   15);
static AUD_PCLK_GATE(spdifin,	   16);
static AUD_PCLK_GATE(spdifout,	   17);
static AUD_PCLK_GATE(resample,	   18);
static AUD_PCLK_GATE(power_detect, 19);
static AUD_PCLK_GATE(spdifout_b,   21);

/* Audio Master Clocks */
static const struct clk_parent_data mst_mux_parent_data[] = {
	{ .fw_name = "mst_in0", },
	{ .fw_name = "mst_in1", },
	{ .fw_name = "mst_in2", },
	{ .fw_name = "mst_in3", },
	{ .fw_name = "mst_in4", },
	{ .fw_name = "mst_in5", },
	{ .fw_name = "mst_in6", },
	{ .fw_name = "mst_in7", },
};

#define AUD_MST_MUX(_name, _reg, _flag)				\
	AUD_MUX(_name##_sel, _reg, 0x7, 24, _flag,		\
		mst_mux_parent_data, 0)

#define AUD_MST_MCLK_MUX(_name, _reg)				\
	AUD_MST_MUX(_name, _reg, CLK_MUX_ROUND_CLOSEST)

#define AUD_MST_SYS_MUX(_name, _reg)				\
	AUD_MST_MUX(_name, _reg, 0)

static AUD_MST_MCLK_MUX(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AUD_MST_MCLK_MUX(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AUD_MST_MCLK_MUX(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AUD_MST_MCLK_MUX(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AUD_MST_MCLK_MUX(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AUD_MST_MCLK_MUX(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AUD_MST_MCLK_MUX(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AUD_MST_MCLK_MUX(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AUD_MST_SYS_MUX(spdifin_clk,   AUDIO_CLK_SPDIFIN_CTRL);
static AUD_MST_SYS_MUX(pdm_sysclk,    AUDIO_CLK_PDMIN_CTRL1);
static AUD_MST_MCLK_MUX(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

#define AUD_MST_DIV(_name, _reg, _flag)				\
	AUD_DIV(_name##_div, _reg, 0, 16, _flag,		\
		    aud_##_name##_sel, CLK_SET_RATE_PARENT)	\

#define AUD_MST_MCLK_DIV(_name, _reg)				\
	AUD_MST_DIV(_name, _reg, CLK_DIVIDER_ROUND_CLOSEST)

#define AUD_MST_SYS_DIV(_name, _reg)				\
	AUD_MST_DIV(_name, _reg, 0)

static AUD_MST_MCLK_DIV(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AUD_MST_MCLK_DIV(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AUD_MST_MCLK_DIV(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AUD_MST_MCLK_DIV(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AUD_MST_MCLK_DIV(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AUD_MST_MCLK_DIV(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AUD_MST_MCLK_DIV(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AUD_MST_MCLK_DIV(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AUD_MST_SYS_DIV(spdifin_clk,   AUDIO_CLK_SPDIFIN_CTRL);
static AUD_MST_SYS_DIV(pdm_sysclk,    AUDIO_CLK_PDMIN_CTRL1);
static AUD_MST_MCLK_DIV(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

#define AUD_MST_MCLK_GATE(_name, _reg)				\
	AUD_GATE(_name, _reg, 31,  aud_##_name##_div,		\
		 CLK_SET_RATE_PARENT)

static AUD_MST_MCLK_GATE(mst_a_mclk,   AUDIO_MCLK_A_CTRL);
static AUD_MST_MCLK_GATE(mst_b_mclk,   AUDIO_MCLK_B_CTRL);
static AUD_MST_MCLK_GATE(mst_c_mclk,   AUDIO_MCLK_C_CTRL);
static AUD_MST_MCLK_GATE(mst_d_mclk,   AUDIO_MCLK_D_CTRL);
static AUD_MST_MCLK_GATE(mst_e_mclk,   AUDIO_MCLK_E_CTRL);
static AUD_MST_MCLK_GATE(mst_f_mclk,   AUDIO_MCLK_F_CTRL);
static AUD_MST_MCLK_GATE(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static AUD_MST_MCLK_GATE(spdifin_clk,  AUDIO_CLK_SPDIFIN_CTRL);
static AUD_MST_MCLK_GATE(pdm_dclk,     AUDIO_CLK_PDMIN_CTRL0);
static AUD_MST_MCLK_GATE(pdm_sysclk,   AUDIO_CLK_PDMIN_CTRL1);
static AUD_MST_MCLK_GATE(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

/* Sample Clocks */
#define AUD_MST_SCLK_PRE_EN(_name, _reg)			\
	AUD_GATE(mst_##_name##_sclk_pre_en, _reg, 31,		\
		 aud_mst_##_name##_mclk, 0)

static AUD_MST_SCLK_PRE_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static AUD_MST_SCLK_PRE_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static AUD_MST_SCLK_PRE_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static AUD_MST_SCLK_PRE_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static AUD_MST_SCLK_PRE_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static AUD_MST_SCLK_PRE_EN(f, AUDIO_MST_F_SCLK_CTRL0);

#define AUD_SCLK_DIV(_name, _reg, _div_shift, _div_width,		\
			 _hi_shift, _hi_width, _phws, _iflags)		\
struct clk_regmap aud_##_name = {					\
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
		.name = "aud_"#_name,					\
		.ops = &meson_sclk_div_ops,				\
		.parent_hws = (const struct clk_hw *[]) { &_phws.hw },	\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AUD_MST_SCLK_DIV(_name, _reg)					\
	AUD_SCLK_DIV(mst_##_name##_sclk_div, _reg, 20, 10, 0, 0,	\
		     aud_mst_##_name##_sclk_pre_en,			\
		     CLK_SET_RATE_PARENT)

static AUD_MST_SCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static AUD_MST_SCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static AUD_MST_SCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static AUD_MST_SCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static AUD_MST_SCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static AUD_MST_SCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

#define AUD_MST_SCLK_POST_EN(_name, _reg)				\
	AUD_GATE(mst_##_name##_sclk_post_en, _reg, 30,			\
		 aud_mst_##_name##_sclk_div, CLK_SET_RATE_PARENT)

static AUD_MST_SCLK_POST_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static AUD_MST_SCLK_POST_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static AUD_MST_SCLK_POST_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static AUD_MST_SCLK_POST_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static AUD_MST_SCLK_POST_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static AUD_MST_SCLK_POST_EN(f, AUDIO_MST_F_SCLK_CTRL0);

#define AUD_TRIPHASE(_name, _reg, _width, _shift0, _shift1, _shift2,	\
			 _phws, _iflags)				\
struct clk_regmap aud_##_name = {					\
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
		.name = "aud_"#_name,					\
		.ops = &meson_clk_triphase_ops,				\
		.parent_hws = (const struct clk_hw *[]) { &_phws.hw },	\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AUD_MST_SCLK(_name, _reg)					\
	AUD_TRIPHASE(mst_##_name##_sclk, _reg, 1, 0, 2, 4,		\
		     aud_mst_##_name##_sclk_post_en, CLK_SET_RATE_PARENT)

static AUD_MST_SCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static AUD_MST_SCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static AUD_MST_SCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static AUD_MST_SCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static AUD_MST_SCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static AUD_MST_SCLK(f, AUDIO_MST_F_SCLK_CTRL1);

#define AUD_MST_LRCLK_DIV(_name, _reg)					\
	AUD_SCLK_DIV(mst_##_name##_lrclk_div, _reg, 0, 10, 10, 10,	\
		     aud_mst_##_name##_sclk_post_en, 0)			\

static AUD_MST_LRCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static AUD_MST_LRCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static AUD_MST_LRCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static AUD_MST_LRCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static AUD_MST_LRCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static AUD_MST_LRCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

#define AUD_MST_LRCLK(_name, _reg)					\
	AUD_TRIPHASE(mst_##_name##_lrclk, _reg, 1, 1, 3, 5,		\
		     aud_mst_##_name##_lrclk_div, CLK_SET_RATE_PARENT)

static AUD_MST_LRCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static AUD_MST_LRCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static AUD_MST_LRCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static AUD_MST_LRCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static AUD_MST_LRCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static AUD_MST_LRCLK(f, AUDIO_MST_F_SCLK_CTRL1);

static const struct clk_parent_data tdm_sclk_parent_data[] = {
	{ .hw = &aud_mst_a_sclk.hw, },
	{ .hw = &aud_mst_b_sclk.hw, },
	{ .hw = &aud_mst_c_sclk.hw, },
	{ .hw = &aud_mst_d_sclk.hw, },
	{ .hw = &aud_mst_e_sclk.hw, },
	{ .hw = &aud_mst_f_sclk.hw, },
	{ .fw_name = "slv_sclk0", },
	{ .fw_name = "slv_sclk1", },
	{ .fw_name = "slv_sclk2", },
	{ .fw_name = "slv_sclk3", },
	{ .fw_name = "slv_sclk4", },
	{ .fw_name = "slv_sclk5", },
	{ .fw_name = "slv_sclk6", },
	{ .fw_name = "slv_sclk7", },
	{ .fw_name = "slv_sclk8", },
	{ .fw_name = "slv_sclk9", },
};

#define AUD_TDM_SCLK_MUX(_name, _reg)				\
	AUD_MUX(tdm##_name##_sclk_sel, _reg, 0xf, 24,		\
		    CLK_MUX_ROUND_CLOSEST,			\
		    tdm_sclk_parent_data, 0)

static AUD_TDM_SCLK_MUX(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AUD_TDM_SCLK_MUX(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AUD_TDM_SCLK_MUX(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AUD_TDM_SCLK_MUX(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AUD_TDM_SCLK_MUX(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AUD_TDM_SCLK_MUX(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AUD_TDM_SCLK_MUX(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AUD_TDM_SCLK_PRE_EN(_name, _reg)				\
	AUD_GATE(tdm##_name##_sclk_pre_en, _reg, 31,			\
		 aud_tdm##_name##_sclk_sel, CLK_SET_RATE_PARENT)

static AUD_TDM_SCLK_PRE_EN(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AUD_TDM_SCLK_PRE_EN(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AUD_TDM_SCLK_PRE_EN(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AUD_TDM_SCLK_PRE_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AUD_TDM_SCLK_PRE_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AUD_TDM_SCLK_PRE_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AUD_TDM_SCLK_PRE_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AUD_TDM_SCLK_POST_EN(_name, _reg)				\
	AUD_GATE(tdm##_name##_sclk_post_en, _reg, 30,			\
		 aud_tdm##_name##_sclk_pre_en, CLK_SET_RATE_PARENT)

static AUD_TDM_SCLK_POST_EN(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AUD_TDM_SCLK_POST_EN(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AUD_TDM_SCLK_POST_EN(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AUD_TDM_SCLK_POST_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AUD_TDM_SCLK_POST_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AUD_TDM_SCLK_POST_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AUD_TDM_SCLK_POST_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

#define AUD_TDM_SCLK(_name, _reg)					\
	struct clk_regmap aud_tdm##_name##_sclk = {			\
	.data = &(struct meson_clk_phase_data) {			\
		.ph = {							\
			.reg_off = (_reg),				\
			.shift   = 29,					\
			.width   = 1,					\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_tdm"#_name"_sclk",				\
		.ops = &meson_clk_phase_ops,				\
		.parent_hws = (const struct clk_hw *[]) {		\
			&aud_tdm##_name##_sclk_post_en.hw		\
		},							\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | CLK_SET_RATE_PARENT,	\
	},								\
}

static AUD_TDM_SCLK(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AUD_TDM_SCLK(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AUD_TDM_SCLK(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AUD_TDM_SCLK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AUD_TDM_SCLK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AUD_TDM_SCLK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AUD_TDM_SCLK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static const struct clk_parent_data tdm_lrclk_parent_data[] = {
	{ .hw = &aud_mst_a_lrclk.hw, },
	{ .hw = &aud_mst_b_lrclk.hw, },
	{ .hw = &aud_mst_c_lrclk.hw, },
	{ .hw = &aud_mst_d_lrclk.hw, },
	{ .hw = &aud_mst_e_lrclk.hw, },
	{ .hw = &aud_mst_f_lrclk.hw, },
	{ .fw_name = "slv_lrclk0", },
	{ .fw_name = "slv_lrclk1", },
	{ .fw_name = "slv_lrclk2", },
	{ .fw_name = "slv_lrclk3", },
	{ .fw_name = "slv_lrclk4", },
	{ .fw_name = "slv_lrclk5", },
	{ .fw_name = "slv_lrclk6", },
	{ .fw_name = "slv_lrclk7", },
	{ .fw_name = "slv_lrclk8", },
	{ .fw_name = "slv_lrclk9", },
};

#define AUD_TDM_LRLCK(_name, _reg)			\
	AUD_MUX(tdm##_name##_lrclk, _reg, 0xf, 20,	\
		CLK_MUX_ROUND_CLOSEST,			\
		tdm_lrclk_parent_data, 0)

static AUD_TDM_LRLCK(in_a,  AUDIO_CLK_TDMIN_A_CTRL);
static AUD_TDM_LRLCK(in_b,  AUDIO_CLK_TDMIN_B_CTRL);
static AUD_TDM_LRLCK(in_c,  AUDIO_CLK_TDMIN_C_CTRL);
static AUD_TDM_LRLCK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static AUD_TDM_LRLCK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static AUD_TDM_LRLCK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static AUD_TDM_LRLCK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

/* G12a Pad control */
#define AUD_TDM_PAD_CTRL(_name, _reg, _shift, _parents)		\
	AUD_MUX(tdm_##_name, _reg, 0x7, _shift, 0, _parents,	\
		CLK_SET_RATE_NO_REPARENT)

static const struct clk_parent_data mclk_pad_ctrl_parent_data[] = {
	{ .hw = &aud_mst_a_mclk.hw },
	{ .hw = &aud_mst_b_mclk.hw },
	{ .hw = &aud_mst_c_mclk.hw },
	{ .hw = &aud_mst_d_mclk.hw },
	{ .hw = &aud_mst_e_mclk.hw },
	{ .hw = &aud_mst_f_mclk.hw },
};

static AUD_TDM_PAD_CTRL(mclk_pad_0, AUDIO_MST_PAD_CTRL0, 0,
			mclk_pad_ctrl_parent_data);
static AUD_TDM_PAD_CTRL(mclk_pad_1, AUDIO_MST_PAD_CTRL0, 4,
			mclk_pad_ctrl_parent_data);

static const struct clk_parent_data lrclk_pad_ctrl_parent_data[] = {
	{ .hw = &aud_mst_a_lrclk.hw },
	{ .hw = &aud_mst_b_lrclk.hw },
	{ .hw = &aud_mst_c_lrclk.hw },
	{ .hw = &aud_mst_d_lrclk.hw },
	{ .hw = &aud_mst_e_lrclk.hw },
	{ .hw = &aud_mst_f_lrclk.hw },
};

static AUD_TDM_PAD_CTRL(lrclk_pad_0, AUDIO_MST_PAD_CTRL1, 16,
			lrclk_pad_ctrl_parent_data);
static AUD_TDM_PAD_CTRL(lrclk_pad_1, AUDIO_MST_PAD_CTRL1, 20,
			lrclk_pad_ctrl_parent_data);
static AUD_TDM_PAD_CTRL(lrclk_pad_2, AUDIO_MST_PAD_CTRL1, 24,
			lrclk_pad_ctrl_parent_data);

static const struct clk_parent_data sclk_pad_ctrl_parent_data[] = {
	{ .hw = &aud_mst_a_sclk.hw },
	{ .hw = &aud_mst_b_sclk.hw },
	{ .hw = &aud_mst_c_sclk.hw },
	{ .hw = &aud_mst_d_sclk.hw },
	{ .hw = &aud_mst_e_sclk.hw },
	{ .hw = &aud_mst_f_sclk.hw },
};

static AUD_TDM_PAD_CTRL(sclk_pad_0, AUDIO_MST_PAD_CTRL1, 0,
			sclk_pad_ctrl_parent_data);
static AUD_TDM_PAD_CTRL(sclk_pad_1, AUDIO_MST_PAD_CTRL1, 4,
			sclk_pad_ctrl_parent_data);
static AUD_TDM_PAD_CTRL(sclk_pad_2, AUDIO_MST_PAD_CTRL1, 8,
			sclk_pad_ctrl_parent_data);

/*
 * Array of all clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw_onecell_data axg_audio_hw_onecell_data = {
	.hws = {
		[AUD_CLKID_DDR_ARB]		= &aud_ddr_arb.hw,
		[AUD_CLKID_PDM]			= &aud_pdm.hw,
		[AUD_CLKID_TDMIN_A]		= &aud_tdmin_a.hw,
		[AUD_CLKID_TDMIN_B]		= &aud_tdmin_b.hw,
		[AUD_CLKID_TDMIN_C]		= &aud_tdmin_c.hw,
		[AUD_CLKID_TDMIN_LB]		= &aud_tdmin_lb.hw,
		[AUD_CLKID_TDMOUT_A]		= &aud_tdmout_a.hw,
		[AUD_CLKID_TDMOUT_B]		= &aud_tdmout_b.hw,
		[AUD_CLKID_TDMOUT_C]		= &aud_tdmout_c.hw,
		[AUD_CLKID_FRDDR_A]		= &aud_frddr_a.hw,
		[AUD_CLKID_FRDDR_B]		= &aud_frddr_b.hw,
		[AUD_CLKID_FRDDR_C]		= &aud_frddr_c.hw,
		[AUD_CLKID_TODDR_A]		= &aud_toddr_a.hw,
		[AUD_CLKID_TODDR_B]		= &aud_toddr_b.hw,
		[AUD_CLKID_TODDR_C]		= &aud_toddr_c.hw,
		[AUD_CLKID_LOOPBACK]		= &aud_loopback.hw,
		[AUD_CLKID_SPDIFIN]		= &aud_spdifin.hw,
		[AUD_CLKID_SPDIFOUT]		= &aud_spdifout.hw,
		[AUD_CLKID_RESAMPLE]		= &aud_resample.hw,
		[AUD_CLKID_POWER_DETECT]	= &aud_power_detect.hw,
		[AUD_CLKID_MST_A_MCLK_SEL]	= &aud_mst_a_mclk_sel.hw,
		[AUD_CLKID_MST_B_MCLK_SEL]	= &aud_mst_b_mclk_sel.hw,
		[AUD_CLKID_MST_C_MCLK_SEL]	= &aud_mst_c_mclk_sel.hw,
		[AUD_CLKID_MST_D_MCLK_SEL]	= &aud_mst_d_mclk_sel.hw,
		[AUD_CLKID_MST_E_MCLK_SEL]	= &aud_mst_e_mclk_sel.hw,
		[AUD_CLKID_MST_F_MCLK_SEL]	= &aud_mst_f_mclk_sel.hw,
		[AUD_CLKID_MST_A_MCLK_DIV]	= &aud_mst_a_mclk_div.hw,
		[AUD_CLKID_MST_B_MCLK_DIV]	= &aud_mst_b_mclk_div.hw,
		[AUD_CLKID_MST_C_MCLK_DIV]	= &aud_mst_c_mclk_div.hw,
		[AUD_CLKID_MST_D_MCLK_DIV]	= &aud_mst_d_mclk_div.hw,
		[AUD_CLKID_MST_E_MCLK_DIV]	= &aud_mst_e_mclk_div.hw,
		[AUD_CLKID_MST_F_MCLK_DIV]	= &aud_mst_f_mclk_div.hw,
		[AUD_CLKID_MST_A_MCLK]		= &aud_mst_a_mclk.hw,
		[AUD_CLKID_MST_B_MCLK]		= &aud_mst_b_mclk.hw,
		[AUD_CLKID_MST_C_MCLK]		= &aud_mst_c_mclk.hw,
		[AUD_CLKID_MST_D_MCLK]		= &aud_mst_d_mclk.hw,
		[AUD_CLKID_MST_E_MCLK]		= &aud_mst_e_mclk.hw,
		[AUD_CLKID_MST_F_MCLK]		= &aud_mst_f_mclk.hw,
		[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &aud_spdifout_clk_sel.hw,
		[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &aud_spdifout_clk_div.hw,
		[AUD_CLKID_SPDIFOUT_CLK]	= &aud_spdifout_clk.hw,
		[AUD_CLKID_SPDIFIN_CLK_SEL]	= &aud_spdifin_clk_sel.hw,
		[AUD_CLKID_SPDIFIN_CLK_DIV]	= &aud_spdifin_clk_div.hw,
		[AUD_CLKID_SPDIFIN_CLK]		= &aud_spdifin_clk.hw,
		[AUD_CLKID_PDM_DCLK_SEL]	= &aud_pdm_dclk_sel.hw,
		[AUD_CLKID_PDM_DCLK_DIV]	= &aud_pdm_dclk_div.hw,
		[AUD_CLKID_PDM_DCLK]		= &aud_pdm_dclk.hw,
		[AUD_CLKID_PDM_SYSCLK_SEL]	= &aud_pdm_sysclk_sel.hw,
		[AUD_CLKID_PDM_SYSCLK_DIV]	= &aud_pdm_sysclk_div.hw,
		[AUD_CLKID_PDM_SYSCLK]		= &aud_pdm_sysclk.hw,
		[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &aud_mst_a_sclk_pre_en.hw,
		[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &aud_mst_b_sclk_pre_en.hw,
		[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &aud_mst_c_sclk_pre_en.hw,
		[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &aud_mst_d_sclk_pre_en.hw,
		[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &aud_mst_e_sclk_pre_en.hw,
		[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &aud_mst_f_sclk_pre_en.hw,
		[AUD_CLKID_MST_A_SCLK_DIV]	= &aud_mst_a_sclk_div.hw,
		[AUD_CLKID_MST_B_SCLK_DIV]	= &aud_mst_b_sclk_div.hw,
		[AUD_CLKID_MST_C_SCLK_DIV]	= &aud_mst_c_sclk_div.hw,
		[AUD_CLKID_MST_D_SCLK_DIV]	= &aud_mst_d_sclk_div.hw,
		[AUD_CLKID_MST_E_SCLK_DIV]	= &aud_mst_e_sclk_div.hw,
		[AUD_CLKID_MST_F_SCLK_DIV]	= &aud_mst_f_sclk_div.hw,
		[AUD_CLKID_MST_A_SCLK_POST_EN]	= &aud_mst_a_sclk_post_en.hw,
		[AUD_CLKID_MST_B_SCLK_POST_EN]	= &aud_mst_b_sclk_post_en.hw,
		[AUD_CLKID_MST_C_SCLK_POST_EN]	= &aud_mst_c_sclk_post_en.hw,
		[AUD_CLKID_MST_D_SCLK_POST_EN]	= &aud_mst_d_sclk_post_en.hw,
		[AUD_CLKID_MST_E_SCLK_POST_EN]	= &aud_mst_e_sclk_post_en.hw,
		[AUD_CLKID_MST_F_SCLK_POST_EN]	= &aud_mst_f_sclk_post_en.hw,
		[AUD_CLKID_MST_A_SCLK]		= &aud_mst_a_sclk.hw,
		[AUD_CLKID_MST_B_SCLK]		= &aud_mst_b_sclk.hw,
		[AUD_CLKID_MST_C_SCLK]		= &aud_mst_c_sclk.hw,
		[AUD_CLKID_MST_D_SCLK]		= &aud_mst_d_sclk.hw,
		[AUD_CLKID_MST_E_SCLK]		= &aud_mst_e_sclk.hw,
		[AUD_CLKID_MST_F_SCLK]		= &aud_mst_f_sclk.hw,
		[AUD_CLKID_MST_A_LRCLK_DIV]	= &aud_mst_a_lrclk_div.hw,
		[AUD_CLKID_MST_B_LRCLK_DIV]	= &aud_mst_b_lrclk_div.hw,
		[AUD_CLKID_MST_C_LRCLK_DIV]	= &aud_mst_c_lrclk_div.hw,
		[AUD_CLKID_MST_D_LRCLK_DIV]	= &aud_mst_d_lrclk_div.hw,
		[AUD_CLKID_MST_E_LRCLK_DIV]	= &aud_mst_e_lrclk_div.hw,
		[AUD_CLKID_MST_F_LRCLK_DIV]	= &aud_mst_f_lrclk_div.hw,
		[AUD_CLKID_MST_A_LRCLK]		= &aud_mst_a_lrclk.hw,
		[AUD_CLKID_MST_B_LRCLK]		= &aud_mst_b_lrclk.hw,
		[AUD_CLKID_MST_C_LRCLK]		= &aud_mst_c_lrclk.hw,
		[AUD_CLKID_MST_D_LRCLK]		= &aud_mst_d_lrclk.hw,
		[AUD_CLKID_MST_E_LRCLK]		= &aud_mst_e_lrclk.hw,
		[AUD_CLKID_MST_F_LRCLK]		= &aud_mst_f_lrclk.hw,
		[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &aud_tdmin_a_sclk_sel.hw,
		[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &aud_tdmin_b_sclk_sel.hw,
		[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &aud_tdmin_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &aud_tdmin_lb_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &aud_tdmout_a_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &aud_tdmout_b_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &aud_tdmout_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &aud_tdmin_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &aud_tdmin_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &aud_tdmin_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &aud_tdmin_lb_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &aud_tdmout_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &aud_tdmout_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &aud_tdmout_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &aud_tdmin_a_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &aud_tdmin_b_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &aud_tdmin_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &aud_tdmin_lb_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &aud_tdmout_a_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &aud_tdmout_b_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &aud_tdmout_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK]	= &aud_tdmin_a_sclk.hw,
		[AUD_CLKID_TDMIN_B_SCLK]	= &aud_tdmin_b_sclk.hw,
		[AUD_CLKID_TDMIN_C_SCLK]	= &aud_tdmin_c_sclk.hw,
		[AUD_CLKID_TDMIN_LB_SCLK]	= &aud_tdmin_lb_sclk.hw,
		[AUD_CLKID_TDMOUT_A_SCLK]	= &aud_tdmout_a_sclk.hw,
		[AUD_CLKID_TDMOUT_B_SCLK]	= &aud_tdmout_b_sclk.hw,
		[AUD_CLKID_TDMOUT_C_SCLK]	= &aud_tdmout_c_sclk.hw,
		[AUD_CLKID_TDMIN_A_LRCLK]	= &aud_tdmin_a_lrclk.hw,
		[AUD_CLKID_TDMIN_B_LRCLK]	= &aud_tdmin_b_lrclk.hw,
		[AUD_CLKID_TDMIN_C_LRCLK]	= &aud_tdmin_c_lrclk.hw,
		[AUD_CLKID_TDMIN_LB_LRCLK]	= &aud_tdmin_lb_lrclk.hw,
		[AUD_CLKID_TDMOUT_A_LRCLK]	= &aud_tdmout_a_lrclk.hw,
		[AUD_CLKID_TDMOUT_B_LRCLK]	= &aud_tdmout_b_lrclk.hw,
		[AUD_CLKID_TDMOUT_C_LRCLK]	= &aud_tdmout_c_lrclk.hw,
		[NR_CLKS] = NULL,
	},
	.num = NR_CLKS,
};

/*
 * Array of all G12A clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw_onecell_data g12a_audio_hw_onecell_data = {
	.hws = {
		[AUD_CLKID_DDR_ARB]		= &aud_ddr_arb.hw,
		[AUD_CLKID_PDM]			= &aud_pdm.hw,
		[AUD_CLKID_TDMIN_A]		= &aud_tdmin_a.hw,
		[AUD_CLKID_TDMIN_B]		= &aud_tdmin_b.hw,
		[AUD_CLKID_TDMIN_C]		= &aud_tdmin_c.hw,
		[AUD_CLKID_TDMIN_LB]		= &aud_tdmin_lb.hw,
		[AUD_CLKID_TDMOUT_A]		= &aud_tdmout_a.hw,
		[AUD_CLKID_TDMOUT_B]		= &aud_tdmout_b.hw,
		[AUD_CLKID_TDMOUT_C]		= &aud_tdmout_c.hw,
		[AUD_CLKID_FRDDR_A]		= &aud_frddr_a.hw,
		[AUD_CLKID_FRDDR_B]		= &aud_frddr_b.hw,
		[AUD_CLKID_FRDDR_C]		= &aud_frddr_c.hw,
		[AUD_CLKID_TODDR_A]		= &aud_toddr_a.hw,
		[AUD_CLKID_TODDR_B]		= &aud_toddr_b.hw,
		[AUD_CLKID_TODDR_C]		= &aud_toddr_c.hw,
		[AUD_CLKID_LOOPBACK]		= &aud_loopback.hw,
		[AUD_CLKID_SPDIFIN]		= &aud_spdifin.hw,
		[AUD_CLKID_SPDIFOUT]		= &aud_spdifout.hw,
		[AUD_CLKID_RESAMPLE]		= &aud_resample.hw,
		[AUD_CLKID_POWER_DETECT]	= &aud_power_detect.hw,
		[AUD_CLKID_SPDIFOUT_B]		= &aud_spdifout_b.hw,
		[AUD_CLKID_MST_A_MCLK_SEL]	= &aud_mst_a_mclk_sel.hw,
		[AUD_CLKID_MST_B_MCLK_SEL]	= &aud_mst_b_mclk_sel.hw,
		[AUD_CLKID_MST_C_MCLK_SEL]	= &aud_mst_c_mclk_sel.hw,
		[AUD_CLKID_MST_D_MCLK_SEL]	= &aud_mst_d_mclk_sel.hw,
		[AUD_CLKID_MST_E_MCLK_SEL]	= &aud_mst_e_mclk_sel.hw,
		[AUD_CLKID_MST_F_MCLK_SEL]	= &aud_mst_f_mclk_sel.hw,
		[AUD_CLKID_MST_A_MCLK_DIV]	= &aud_mst_a_mclk_div.hw,
		[AUD_CLKID_MST_B_MCLK_DIV]	= &aud_mst_b_mclk_div.hw,
		[AUD_CLKID_MST_C_MCLK_DIV]	= &aud_mst_c_mclk_div.hw,
		[AUD_CLKID_MST_D_MCLK_DIV]	= &aud_mst_d_mclk_div.hw,
		[AUD_CLKID_MST_E_MCLK_DIV]	= &aud_mst_e_mclk_div.hw,
		[AUD_CLKID_MST_F_MCLK_DIV]	= &aud_mst_f_mclk_div.hw,
		[AUD_CLKID_MST_A_MCLK]		= &aud_mst_a_mclk.hw,
		[AUD_CLKID_MST_B_MCLK]		= &aud_mst_b_mclk.hw,
		[AUD_CLKID_MST_C_MCLK]		= &aud_mst_c_mclk.hw,
		[AUD_CLKID_MST_D_MCLK]		= &aud_mst_d_mclk.hw,
		[AUD_CLKID_MST_E_MCLK]		= &aud_mst_e_mclk.hw,
		[AUD_CLKID_MST_F_MCLK]		= &aud_mst_f_mclk.hw,
		[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &aud_spdifout_clk_sel.hw,
		[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &aud_spdifout_clk_div.hw,
		[AUD_CLKID_SPDIFOUT_CLK]	= &aud_spdifout_clk.hw,
		[AUD_CLKID_SPDIFOUT_B_CLK_SEL]	= &aud_spdifout_b_clk_sel.hw,
		[AUD_CLKID_SPDIFOUT_B_CLK_DIV]	= &aud_spdifout_b_clk_div.hw,
		[AUD_CLKID_SPDIFOUT_B_CLK]	= &aud_spdifout_b_clk.hw,
		[AUD_CLKID_SPDIFIN_CLK_SEL]	= &aud_spdifin_clk_sel.hw,
		[AUD_CLKID_SPDIFIN_CLK_DIV]	= &aud_spdifin_clk_div.hw,
		[AUD_CLKID_SPDIFIN_CLK]		= &aud_spdifin_clk.hw,
		[AUD_CLKID_PDM_DCLK_SEL]	= &aud_pdm_dclk_sel.hw,
		[AUD_CLKID_PDM_DCLK_DIV]	= &aud_pdm_dclk_div.hw,
		[AUD_CLKID_PDM_DCLK]		= &aud_pdm_dclk.hw,
		[AUD_CLKID_PDM_SYSCLK_SEL]	= &aud_pdm_sysclk_sel.hw,
		[AUD_CLKID_PDM_SYSCLK_DIV]	= &aud_pdm_sysclk_div.hw,
		[AUD_CLKID_PDM_SYSCLK]		= &aud_pdm_sysclk.hw,
		[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &aud_mst_a_sclk_pre_en.hw,
		[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &aud_mst_b_sclk_pre_en.hw,
		[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &aud_mst_c_sclk_pre_en.hw,
		[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &aud_mst_d_sclk_pre_en.hw,
		[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &aud_mst_e_sclk_pre_en.hw,
		[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &aud_mst_f_sclk_pre_en.hw,
		[AUD_CLKID_MST_A_SCLK_DIV]	= &aud_mst_a_sclk_div.hw,
		[AUD_CLKID_MST_B_SCLK_DIV]	= &aud_mst_b_sclk_div.hw,
		[AUD_CLKID_MST_C_SCLK_DIV]	= &aud_mst_c_sclk_div.hw,
		[AUD_CLKID_MST_D_SCLK_DIV]	= &aud_mst_d_sclk_div.hw,
		[AUD_CLKID_MST_E_SCLK_DIV]	= &aud_mst_e_sclk_div.hw,
		[AUD_CLKID_MST_F_SCLK_DIV]	= &aud_mst_f_sclk_div.hw,
		[AUD_CLKID_MST_A_SCLK_POST_EN]	= &aud_mst_a_sclk_post_en.hw,
		[AUD_CLKID_MST_B_SCLK_POST_EN]	= &aud_mst_b_sclk_post_en.hw,
		[AUD_CLKID_MST_C_SCLK_POST_EN]	= &aud_mst_c_sclk_post_en.hw,
		[AUD_CLKID_MST_D_SCLK_POST_EN]	= &aud_mst_d_sclk_post_en.hw,
		[AUD_CLKID_MST_E_SCLK_POST_EN]	= &aud_mst_e_sclk_post_en.hw,
		[AUD_CLKID_MST_F_SCLK_POST_EN]	= &aud_mst_f_sclk_post_en.hw,
		[AUD_CLKID_MST_A_SCLK]		= &aud_mst_a_sclk.hw,
		[AUD_CLKID_MST_B_SCLK]		= &aud_mst_b_sclk.hw,
		[AUD_CLKID_MST_C_SCLK]		= &aud_mst_c_sclk.hw,
		[AUD_CLKID_MST_D_SCLK]		= &aud_mst_d_sclk.hw,
		[AUD_CLKID_MST_E_SCLK]		= &aud_mst_e_sclk.hw,
		[AUD_CLKID_MST_F_SCLK]		= &aud_mst_f_sclk.hw,
		[AUD_CLKID_MST_A_LRCLK_DIV]	= &aud_mst_a_lrclk_div.hw,
		[AUD_CLKID_MST_B_LRCLK_DIV]	= &aud_mst_b_lrclk_div.hw,
		[AUD_CLKID_MST_C_LRCLK_DIV]	= &aud_mst_c_lrclk_div.hw,
		[AUD_CLKID_MST_D_LRCLK_DIV]	= &aud_mst_d_lrclk_div.hw,
		[AUD_CLKID_MST_E_LRCLK_DIV]	= &aud_mst_e_lrclk_div.hw,
		[AUD_CLKID_MST_F_LRCLK_DIV]	= &aud_mst_f_lrclk_div.hw,
		[AUD_CLKID_MST_A_LRCLK]		= &aud_mst_a_lrclk.hw,
		[AUD_CLKID_MST_B_LRCLK]		= &aud_mst_b_lrclk.hw,
		[AUD_CLKID_MST_C_LRCLK]		= &aud_mst_c_lrclk.hw,
		[AUD_CLKID_MST_D_LRCLK]		= &aud_mst_d_lrclk.hw,
		[AUD_CLKID_MST_E_LRCLK]		= &aud_mst_e_lrclk.hw,
		[AUD_CLKID_MST_F_LRCLK]		= &aud_mst_f_lrclk.hw,
		[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &aud_tdmin_a_sclk_sel.hw,
		[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &aud_tdmin_b_sclk_sel.hw,
		[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &aud_tdmin_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &aud_tdmin_lb_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &aud_tdmout_a_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &aud_tdmout_b_sclk_sel.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &aud_tdmout_c_sclk_sel.hw,
		[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &aud_tdmin_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &aud_tdmin_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &aud_tdmin_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &aud_tdmin_lb_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &aud_tdmout_a_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &aud_tdmout_b_sclk_pre_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &aud_tdmout_c_sclk_pre_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &aud_tdmin_a_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &aud_tdmin_b_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &aud_tdmin_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &aud_tdmin_lb_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &aud_tdmout_a_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &aud_tdmout_b_sclk_post_en.hw,
		[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &aud_tdmout_c_sclk_post_en.hw,
		[AUD_CLKID_TDMIN_A_SCLK]	= &aud_tdmin_a_sclk.hw,
		[AUD_CLKID_TDMIN_B_SCLK]	= &aud_tdmin_b_sclk.hw,
		[AUD_CLKID_TDMIN_C_SCLK]	= &aud_tdmin_c_sclk.hw,
		[AUD_CLKID_TDMIN_LB_SCLK]	= &aud_tdmin_lb_sclk.hw,
		[AUD_CLKID_TDMOUT_A_SCLK]	= &aud_tdmout_a_sclk.hw,
		[AUD_CLKID_TDMOUT_B_SCLK]	= &aud_tdmout_b_sclk.hw,
		[AUD_CLKID_TDMOUT_C_SCLK]	= &aud_tdmout_c_sclk.hw,
		[AUD_CLKID_TDMIN_A_LRCLK]	= &aud_tdmin_a_lrclk.hw,
		[AUD_CLKID_TDMIN_B_LRCLK]	= &aud_tdmin_b_lrclk.hw,
		[AUD_CLKID_TDMIN_C_LRCLK]	= &aud_tdmin_c_lrclk.hw,
		[AUD_CLKID_TDMIN_LB_LRCLK]	= &aud_tdmin_lb_lrclk.hw,
		[AUD_CLKID_TDMOUT_A_LRCLK]	= &aud_tdmout_a_lrclk.hw,
		[AUD_CLKID_TDMOUT_B_LRCLK]	= &aud_tdmout_b_lrclk.hw,
		[AUD_CLKID_TDMOUT_C_LRCLK]	= &aud_tdmout_c_lrclk.hw,
		[AUD_CLKID_TDM_MCLK_PAD0]	= &aud_tdm_mclk_pad_0.hw,
		[AUD_CLKID_TDM_MCLK_PAD1]	= &aud_tdm_mclk_pad_1.hw,
		[AUD_CLKID_TDM_LRCLK_PAD0]	= &aud_tdm_lrclk_pad_0.hw,
		[AUD_CLKID_TDM_LRCLK_PAD1]	= &aud_tdm_lrclk_pad_1.hw,
		[AUD_CLKID_TDM_LRCLK_PAD2]	= &aud_tdm_lrclk_pad_2.hw,
		[AUD_CLKID_TDM_SCLK_PAD0]	= &aud_tdm_sclk_pad_0.hw,
		[AUD_CLKID_TDM_SCLK_PAD1]	= &aud_tdm_sclk_pad_1.hw,
		[AUD_CLKID_TDM_SCLK_PAD2]	= &aud_tdm_sclk_pad_2.hw,
		[NR_CLKS] = NULL,
	},
	.num = NR_CLKS,
};

/* Convenience table to populate regmap in .probe()
 * Note that this table is shared between both AXG and G12A,
 * with spdifout_b clocks being exclusive to G12A. Since those
 * clocks are not declared within the AXG onecell table, we do not
 * feel the need to have separate AXG/G12A regmap tables.
 */
static struct clk_regmap *const aud_clk_regmaps[] = {
	&aud_ddr_arb,
	&aud_pdm,
	&aud_tdmin_a,
	&aud_tdmin_b,
	&aud_tdmin_c,
	&aud_tdmin_lb,
	&aud_tdmout_a,
	&aud_tdmout_b,
	&aud_tdmout_c,
	&aud_frddr_a,
	&aud_frddr_b,
	&aud_frddr_c,
	&aud_toddr_a,
	&aud_toddr_b,
	&aud_toddr_c,
	&aud_loopback,
	&aud_spdifin,
	&aud_spdifout,
	&aud_resample,
	&aud_power_detect,
	&aud_spdifout_b,
	&aud_mst_a_mclk_sel,
	&aud_mst_b_mclk_sel,
	&aud_mst_c_mclk_sel,
	&aud_mst_d_mclk_sel,
	&aud_mst_e_mclk_sel,
	&aud_mst_f_mclk_sel,
	&aud_mst_a_mclk_div,
	&aud_mst_b_mclk_div,
	&aud_mst_c_mclk_div,
	&aud_mst_d_mclk_div,
	&aud_mst_e_mclk_div,
	&aud_mst_f_mclk_div,
	&aud_mst_a_mclk,
	&aud_mst_b_mclk,
	&aud_mst_c_mclk,
	&aud_mst_d_mclk,
	&aud_mst_e_mclk,
	&aud_mst_f_mclk,
	&aud_spdifout_clk_sel,
	&aud_spdifout_clk_div,
	&aud_spdifout_clk,
	&aud_spdifin_clk_sel,
	&aud_spdifin_clk_div,
	&aud_spdifin_clk,
	&aud_pdm_dclk_sel,
	&aud_pdm_dclk_div,
	&aud_pdm_dclk,
	&aud_pdm_sysclk_sel,
	&aud_pdm_sysclk_div,
	&aud_pdm_sysclk,
	&aud_mst_a_sclk_pre_en,
	&aud_mst_b_sclk_pre_en,
	&aud_mst_c_sclk_pre_en,
	&aud_mst_d_sclk_pre_en,
	&aud_mst_e_sclk_pre_en,
	&aud_mst_f_sclk_pre_en,
	&aud_mst_a_sclk_div,
	&aud_mst_b_sclk_div,
	&aud_mst_c_sclk_div,
	&aud_mst_d_sclk_div,
	&aud_mst_e_sclk_div,
	&aud_mst_f_sclk_div,
	&aud_mst_a_sclk_post_en,
	&aud_mst_b_sclk_post_en,
	&aud_mst_c_sclk_post_en,
	&aud_mst_d_sclk_post_en,
	&aud_mst_e_sclk_post_en,
	&aud_mst_f_sclk_post_en,
	&aud_mst_a_sclk,
	&aud_mst_b_sclk,
	&aud_mst_c_sclk,
	&aud_mst_d_sclk,
	&aud_mst_e_sclk,
	&aud_mst_f_sclk,
	&aud_mst_a_lrclk_div,
	&aud_mst_b_lrclk_div,
	&aud_mst_c_lrclk_div,
	&aud_mst_d_lrclk_div,
	&aud_mst_e_lrclk_div,
	&aud_mst_f_lrclk_div,
	&aud_mst_a_lrclk,
	&aud_mst_b_lrclk,
	&aud_mst_c_lrclk,
	&aud_mst_d_lrclk,
	&aud_mst_e_lrclk,
	&aud_mst_f_lrclk,
	&aud_tdmin_a_sclk_sel,
	&aud_tdmin_b_sclk_sel,
	&aud_tdmin_c_sclk_sel,
	&aud_tdmin_lb_sclk_sel,
	&aud_tdmout_a_sclk_sel,
	&aud_tdmout_b_sclk_sel,
	&aud_tdmout_c_sclk_sel,
	&aud_tdmin_a_sclk_pre_en,
	&aud_tdmin_b_sclk_pre_en,
	&aud_tdmin_c_sclk_pre_en,
	&aud_tdmin_lb_sclk_pre_en,
	&aud_tdmout_a_sclk_pre_en,
	&aud_tdmout_b_sclk_pre_en,
	&aud_tdmout_c_sclk_pre_en,
	&aud_tdmin_a_sclk_post_en,
	&aud_tdmin_b_sclk_post_en,
	&aud_tdmin_c_sclk_post_en,
	&aud_tdmin_lb_sclk_post_en,
	&aud_tdmout_a_sclk_post_en,
	&aud_tdmout_b_sclk_post_en,
	&aud_tdmout_c_sclk_post_en,
	&aud_tdmin_a_sclk,
	&aud_tdmin_b_sclk,
	&aud_tdmin_c_sclk,
	&aud_tdmin_lb_sclk,
	&aud_tdmout_a_sclk,
	&aud_tdmout_b_sclk,
	&aud_tdmout_c_sclk,
	&aud_tdmin_a_lrclk,
	&aud_tdmin_b_lrclk,
	&aud_tdmin_c_lrclk,
	&aud_tdmin_lb_lrclk,
	&aud_tdmout_a_lrclk,
	&aud_tdmout_b_lrclk,
	&aud_tdmout_c_lrclk,
	&aud_spdifout_b_clk_sel,
	&aud_spdifout_b_clk_div,
	&aud_spdifout_b_clk,
	&aud_tdm_mclk_pad_0,
	&aud_tdm_mclk_pad_1,
	&aud_tdm_lrclk_pad_0,
	&aud_tdm_lrclk_pad_1,
	&aud_tdm_lrclk_pad_2,
	&aud_tdm_sclk_pad_0,
	&aud_tdm_sclk_pad_1,
	&aud_tdm_sclk_pad_2,
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

struct axg_audio_reset_data {
	struct reset_controller_dev rstc;
	struct regmap *map;
	unsigned int offset;
};

static void axg_audio_reset_reg_and_bit(struct axg_audio_reset_data *rst,
					unsigned long id,
					unsigned int *reg,
					unsigned int *bit)
{
	unsigned int stride = regmap_get_reg_stride(rst->map);

	*reg = (id / (stride * BITS_PER_BYTE)) * stride;
	*reg += rst->offset;
	*bit = id % (stride * BITS_PER_BYTE);
}

static int axg_audio_reset_update(struct reset_controller_dev *rcdev,
				unsigned long id, bool assert)
{
	struct axg_audio_reset_data *rst =
		container_of(rcdev, struct axg_audio_reset_data, rstc);
	unsigned int offset, bit;

	axg_audio_reset_reg_and_bit(rst, id, &offset, &bit);

	regmap_update_bits(rst->map, offset, BIT(bit),
			assert ? BIT(bit) : 0);

	return 0;
}

static int axg_audio_reset_status(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct axg_audio_reset_data *rst =
		container_of(rcdev, struct axg_audio_reset_data, rstc);
	unsigned int val, offset, bit;

	axg_audio_reset_reg_and_bit(rst, id, &offset, &bit);

	regmap_read(rst->map, offset, &val);

	return !!(val & BIT(bit));
}

static int axg_audio_reset_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return axg_audio_reset_update(rcdev, id, true);
}

static int axg_audio_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return axg_audio_reset_update(rcdev, id, false);
}

static int axg_audio_reset_toggle(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	int ret;

	ret = axg_audio_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return axg_audio_reset_deassert(rcdev, id);
}

static const struct reset_control_ops axg_audio_rstc_ops = {
	.assert = axg_audio_reset_assert,
	.deassert = axg_audio_reset_deassert,
	.reset = axg_audio_reset_toggle,
	.status = axg_audio_reset_status,
};

static const struct regmap_config axg_audio_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= AUDIO_CLK_PDMIN_CTRL1,
};

struct audioclk_data {
	struct clk_hw_onecell_data *hw_onecell_data;
	unsigned int reset_offset;
	unsigned int reset_num;
};

static int axg_audio_clkc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct audioclk_data *data;
	struct axg_audio_reset_data *rst;
	struct regmap *map;
	struct resource *res;
	void __iomem *regs;
	struct clk_hw *hw;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

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

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(aud_clk_regmaps); i++)
		aud_clk_regmaps[i]->map = map;

	/* Take care to skip the registered input clocks */
	for (i = AUD_CLKID_DDR_ARB; i < data->hw_onecell_data->num; i++) {
		const char *name;

		hw = data->hw_onecell_data->hws[i];
		/* array might be sparse */
		if (!hw)
			continue;

		name = hw->init->name;

		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "failed to register clock %s\n", name);
			return ret;
		}
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					data->hw_onecell_data);
	if (ret)
		return ret;

	/* Stop here if there is no reset */
	if (!data->reset_num)
		return 0;

	rst = devm_kzalloc(dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->map = map;
	rst->offset = data->reset_offset;
	rst->rstc.nr_resets = data->reset_num;
	rst->rstc.ops = &axg_audio_rstc_ops;
	rst->rstc.of_node = dev->of_node;
	rst->rstc.owner = THIS_MODULE;

	return devm_reset_controller_register(dev, &rst->rstc);
}

static const struct audioclk_data axg_audioclk_data = {
	.hw_onecell_data = &axg_audio_hw_onecell_data,
};

static const struct audioclk_data g12a_audioclk_data = {
	.hw_onecell_data = &g12a_audio_hw_onecell_data,
	.reset_offset = AUDIO_SW_RESET,
	.reset_num = 26,
};

static const struct of_device_id clkc_match_table[] = {
	{
		.compatible = "amlogic,axg-audio-clkc",
		.data = &axg_audioclk_data
	}, {
		.compatible = "amlogic,g12a-audio-clkc",
		.data = &g12a_audioclk_data
	}, {}
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

MODULE_DESCRIPTION("Amlogic AXG/G12A Audio Clock driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
