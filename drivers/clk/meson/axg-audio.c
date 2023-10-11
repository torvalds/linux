// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include "meson-clkc-utils.h"
#include "axg-audio.h"
#include "clk-regmap.h"
#include "clk-phase.h"
#include "sclk-div.h"

#include <dt-bindings/clock/axg-audio-clkc.h>

#define AUD_GATE(_name, _reg, _bit, _pname, _iflags) {			\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AUD_MUX(_name, _reg, _mask, _shift, _dflags, _pdata, _iflags) {	\
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

#define AUD_DIV(_name, _reg, _shift, _width, _dflags, _pname, _iflags) { \
	.data = &(struct clk_regmap_div_data){				\
		.offset = (_reg),					\
		.shift = (_shift),					\
		.width = (_width),					\
		.flags = (_dflags),					\
	},								\
	.hw.init = &(struct clk_init_data){				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_divider_ops,				\
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AUD_PCLK_GATE(_name, _reg, _bit) {				\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_names = (const char *[]){ "aud_top" },		\
		.num_parents = 1,					\
	},								\
}

#define AUD_SCLK_DIV(_name, _reg, _div_shift, _div_width,		\
		     _hi_shift, _hi_width, _pname, _iflags) {		\
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
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AUD_TRIPHASE(_name, _reg, _width, _shift0, _shift1, _shift2,	\
		     _pname, _iflags) {					\
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
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = CLK_DUTY_CYCLE_PARENT | (_iflags),		\
	},								\
}

#define AUD_PHASE(_name, _reg, _width, _shift, _pname, _iflags) {	\
	.data = &(struct meson_clk_phase_data) {			\
		.ph = {							\
			.reg_off = (_reg),				\
			.shift   = (_shift),				\
			.width   = (_width),				\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &meson_clk_phase_ops,				\
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

#define AUD_SCLK_WS(_name, _reg, _width, _shift_ph, _shift_ws, _pname,	\
		    _iflags) {						\
	.data = &(struct meson_sclk_ws_inv_data) {			\
		.ph = {							\
			.reg_off = (_reg),				\
			.shift   = (_shift_ph),				\
			.width   = (_width),				\
		},							\
		.ws = {							\
			.reg_off = (_reg),				\
			.shift   = (_shift_ws),				\
			.width   = (_width),				\
		},							\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = "aud_"#_name,					\
		.ops = &meson_clk_phase_ops,				\
		.parent_names = (const char *[]){ #_pname },		\
		.num_parents = 1,					\
		.flags = (_iflags),					\
	},								\
}

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

#define AUD_MST_MUX(_name, _reg, _flag)					\
	AUD_MUX(_name##_sel, _reg, 0x7, 24, _flag,			\
		mst_mux_parent_data, 0)
#define AUD_MST_DIV(_name, _reg, _flag)					\
	AUD_DIV(_name##_div, _reg, 0, 16, _flag,			\
		aud_##_name##_sel, CLK_SET_RATE_PARENT)
#define AUD_MST_MCLK_GATE(_name, _reg)					\
	AUD_GATE(_name, _reg, 31, aud_##_name##_div,			\
		 CLK_SET_RATE_PARENT)

#define AUD_MST_MCLK_MUX(_name, _reg)					\
	AUD_MST_MUX(_name, _reg, CLK_MUX_ROUND_CLOSEST)
#define AUD_MST_MCLK_DIV(_name, _reg)					\
	AUD_MST_DIV(_name, _reg, CLK_DIVIDER_ROUND_CLOSEST)

#define AUD_MST_SYS_MUX(_name, _reg)					\
	AUD_MST_MUX(_name, _reg, 0)
#define AUD_MST_SYS_DIV(_name, _reg)					\
	AUD_MST_DIV(_name, _reg, 0)

/* Sample Clocks */
#define AUD_MST_SCLK_PRE_EN(_name, _reg)				\
	AUD_GATE(mst_##_name##_sclk_pre_en, _reg, 31,			\
		 aud_mst_##_name##_mclk, 0)
#define AUD_MST_SCLK_DIV(_name, _reg)					\
	AUD_SCLK_DIV(mst_##_name##_sclk_div, _reg, 20, 10, 0, 0,	\
		     aud_mst_##_name##_sclk_pre_en,			\
		     CLK_SET_RATE_PARENT)
#define AUD_MST_SCLK_POST_EN(_name, _reg)				\
	AUD_GATE(mst_##_name##_sclk_post_en, _reg, 30,			\
		 aud_mst_##_name##_sclk_div, CLK_SET_RATE_PARENT)
#define AUD_MST_SCLK(_name, _reg)					\
	AUD_TRIPHASE(mst_##_name##_sclk, _reg, 1, 0, 2, 4,		\
		     aud_mst_##_name##_sclk_post_en, CLK_SET_RATE_PARENT)

#define AUD_MST_LRCLK_DIV(_name, _reg)					\
	AUD_SCLK_DIV(mst_##_name##_lrclk_div, _reg, 0, 10, 10, 10,	\
		     aud_mst_##_name##_sclk_post_en, 0)
#define AUD_MST_LRCLK(_name, _reg)					\
	AUD_TRIPHASE(mst_##_name##_lrclk, _reg, 1, 1, 3, 5,		\
		     aud_mst_##_name##_lrclk_div, CLK_SET_RATE_PARENT)

/* TDM bit clock sources */
static const struct clk_parent_data tdm_sclk_parent_data[] = {
	{ .name = "aud_mst_a_sclk", .index = -1, },
	{ .name = "aud_mst_b_sclk", .index = -1, },
	{ .name = "aud_mst_c_sclk", .index = -1, },
	{ .name = "aud_mst_d_sclk", .index = -1, },
	{ .name = "aud_mst_e_sclk", .index = -1, },
	{ .name = "aud_mst_f_sclk", .index = -1, },
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

/* TDM sample clock sources */
static const struct clk_parent_data tdm_lrclk_parent_data[] = {
	{ .name = "aud_mst_a_lrclk", .index = -1, },
	{ .name = "aud_mst_b_lrclk", .index = -1, },
	{ .name = "aud_mst_c_lrclk", .index = -1, },
	{ .name = "aud_mst_d_lrclk", .index = -1, },
	{ .name = "aud_mst_e_lrclk", .index = -1, },
	{ .name = "aud_mst_f_lrclk", .index = -1, },
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

#define AUD_TDM_SCLK_MUX(_name, _reg)					\
	AUD_MUX(tdm##_name##_sclk_sel, _reg, 0xf, 24,			\
		CLK_MUX_ROUND_CLOSEST, tdm_sclk_parent_data, 0)
#define AUD_TDM_SCLK_PRE_EN(_name, _reg)				\
	AUD_GATE(tdm##_name##_sclk_pre_en, _reg, 31,			\
		 aud_tdm##_name##_sclk_sel, CLK_SET_RATE_PARENT)
#define AUD_TDM_SCLK_POST_EN(_name, _reg)				\
	AUD_GATE(tdm##_name##_sclk_post_en, _reg, 30,			\
		 aud_tdm##_name##_sclk_pre_en, CLK_SET_RATE_PARENT)
#define AUD_TDM_SCLK(_name, _reg)					\
	AUD_PHASE(tdm##_name##_sclk, _reg, 1, 29,			\
		  aud_tdm##_name##_sclk_post_en,			\
		  CLK_DUTY_CYCLE_PARENT | CLK_SET_RATE_PARENT)
#define AUD_TDM_SCLK_WS(_name, _reg)					\
	AUD_SCLK_WS(tdm##_name##_sclk, _reg, 1, 29, 28,			\
		    aud_tdm##_name##_sclk_post_en,			\
		    CLK_DUTY_CYCLE_PARENT | CLK_SET_RATE_PARENT)

#define AUD_TDM_LRLCK(_name, _reg)					\
	AUD_MUX(tdm##_name##_lrclk, _reg, 0xf, 20,			\
		CLK_MUX_ROUND_CLOSEST, tdm_lrclk_parent_data, 0)

/* Pad master clock sources */
static const struct clk_parent_data mclk_pad_ctrl_parent_data[] = {
	{ .name = "aud_mst_a_mclk", .index = -1,  },
	{ .name = "aud_mst_b_mclk", .index = -1,  },
	{ .name = "aud_mst_c_mclk", .index = -1,  },
	{ .name = "aud_mst_d_mclk", .index = -1,  },
	{ .name = "aud_mst_e_mclk", .index = -1,  },
	{ .name = "aud_mst_f_mclk", .index = -1,  },
};

/* Pad bit clock sources */
static const struct clk_parent_data sclk_pad_ctrl_parent_data[] = {
	{ .name = "aud_mst_a_sclk", .index = -1, },
	{ .name = "aud_mst_b_sclk", .index = -1, },
	{ .name = "aud_mst_c_sclk", .index = -1, },
	{ .name = "aud_mst_d_sclk", .index = -1, },
	{ .name = "aud_mst_e_sclk", .index = -1, },
	{ .name = "aud_mst_f_sclk", .index = -1, },
};

/* Pad sample clock sources */
static const struct clk_parent_data lrclk_pad_ctrl_parent_data[] = {
	{ .name = "aud_mst_a_lrclk", .index = -1, },
	{ .name = "aud_mst_b_lrclk", .index = -1, },
	{ .name = "aud_mst_c_lrclk", .index = -1, },
	{ .name = "aud_mst_d_lrclk", .index = -1, },
	{ .name = "aud_mst_e_lrclk", .index = -1, },
	{ .name = "aud_mst_f_lrclk", .index = -1, },
};

#define AUD_TDM_PAD_CTRL(_name, _reg, _shift, _parents)		\
	AUD_MUX(_name, _reg, 0x7, _shift, 0, _parents,		\
		CLK_SET_RATE_NO_REPARENT)

/* Common Clocks */
static struct clk_regmap ddr_arb =
	AUD_PCLK_GATE(ddr_arb, AUDIO_CLK_GATE_EN, 0);
static struct clk_regmap pdm =
	AUD_PCLK_GATE(pdm, AUDIO_CLK_GATE_EN, 1);
static struct clk_regmap tdmin_a =
	AUD_PCLK_GATE(tdmin_a, AUDIO_CLK_GATE_EN, 2);
static struct clk_regmap tdmin_b =
	AUD_PCLK_GATE(tdmin_b, AUDIO_CLK_GATE_EN, 3);
static struct clk_regmap tdmin_c =
	AUD_PCLK_GATE(tdmin_c, AUDIO_CLK_GATE_EN, 4);
static struct clk_regmap tdmin_lb =
	AUD_PCLK_GATE(tdmin_lb, AUDIO_CLK_GATE_EN, 5);
static struct clk_regmap tdmout_a =
	AUD_PCLK_GATE(tdmout_a, AUDIO_CLK_GATE_EN, 6);
static struct clk_regmap tdmout_b =
	AUD_PCLK_GATE(tdmout_b, AUDIO_CLK_GATE_EN, 7);
static struct clk_regmap tdmout_c =
	AUD_PCLK_GATE(tdmout_c, AUDIO_CLK_GATE_EN, 8);
static struct clk_regmap frddr_a =
	AUD_PCLK_GATE(frddr_a, AUDIO_CLK_GATE_EN, 9);
static struct clk_regmap frddr_b =
	AUD_PCLK_GATE(frddr_b, AUDIO_CLK_GATE_EN, 10);
static struct clk_regmap frddr_c =
	AUD_PCLK_GATE(frddr_c, AUDIO_CLK_GATE_EN, 11);
static struct clk_regmap toddr_a =
	AUD_PCLK_GATE(toddr_a, AUDIO_CLK_GATE_EN, 12);
static struct clk_regmap toddr_b =
	AUD_PCLK_GATE(toddr_b, AUDIO_CLK_GATE_EN, 13);
static struct clk_regmap toddr_c =
	AUD_PCLK_GATE(toddr_c, AUDIO_CLK_GATE_EN, 14);
static struct clk_regmap loopback =
	AUD_PCLK_GATE(loopback, AUDIO_CLK_GATE_EN, 15);
static struct clk_regmap spdifin =
	AUD_PCLK_GATE(spdifin, AUDIO_CLK_GATE_EN, 16);
static struct clk_regmap spdifout =
	AUD_PCLK_GATE(spdifout, AUDIO_CLK_GATE_EN, 17);
static struct clk_regmap resample =
	AUD_PCLK_GATE(resample, AUDIO_CLK_GATE_EN, 18);
static struct clk_regmap power_detect =
	AUD_PCLK_GATE(power_detect, AUDIO_CLK_GATE_EN, 19);

static struct clk_regmap spdifout_clk_sel =
	AUD_MST_MCLK_MUX(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static struct clk_regmap pdm_dclk_sel =
	AUD_MST_MCLK_MUX(pdm_dclk, AUDIO_CLK_PDMIN_CTRL0);
static struct clk_regmap spdifin_clk_sel =
	AUD_MST_SYS_MUX(spdifin_clk, AUDIO_CLK_SPDIFIN_CTRL);
static struct clk_regmap pdm_sysclk_sel =
	AUD_MST_SYS_MUX(pdm_sysclk, AUDIO_CLK_PDMIN_CTRL1);
static struct clk_regmap spdifout_b_clk_sel =
	AUD_MST_MCLK_MUX(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

static struct clk_regmap spdifout_clk_div =
	AUD_MST_MCLK_DIV(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static struct clk_regmap pdm_dclk_div =
	AUD_MST_MCLK_DIV(pdm_dclk, AUDIO_CLK_PDMIN_CTRL0);
static struct clk_regmap spdifin_clk_div =
	AUD_MST_SYS_DIV(spdifin_clk, AUDIO_CLK_SPDIFIN_CTRL);
static struct clk_regmap pdm_sysclk_div =
	AUD_MST_SYS_DIV(pdm_sysclk, AUDIO_CLK_PDMIN_CTRL1);
static struct clk_regmap spdifout_b_clk_div =
	AUD_MST_MCLK_DIV(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

static struct clk_regmap spdifout_clk =
	AUD_MST_MCLK_GATE(spdifout_clk, AUDIO_CLK_SPDIFOUT_CTRL);
static struct clk_regmap spdifin_clk =
	AUD_MST_MCLK_GATE(spdifin_clk, AUDIO_CLK_SPDIFIN_CTRL);
static struct clk_regmap pdm_dclk =
	AUD_MST_MCLK_GATE(pdm_dclk, AUDIO_CLK_PDMIN_CTRL0);
static struct clk_regmap pdm_sysclk =
	AUD_MST_MCLK_GATE(pdm_sysclk, AUDIO_CLK_PDMIN_CTRL1);
static struct clk_regmap spdifout_b_clk =
	AUD_MST_MCLK_GATE(spdifout_b_clk, AUDIO_CLK_SPDIFOUT_B_CTRL);

static struct clk_regmap mst_a_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static struct clk_regmap mst_b_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static struct clk_regmap mst_c_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static struct clk_regmap mst_d_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static struct clk_regmap mst_e_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static struct clk_regmap mst_f_sclk_pre_en =
	AUD_MST_SCLK_PRE_EN(f, AUDIO_MST_F_SCLK_CTRL0);

static struct clk_regmap mst_a_sclk_div =
	AUD_MST_SCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static struct clk_regmap mst_b_sclk_div =
	AUD_MST_SCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static struct clk_regmap mst_c_sclk_div =
	AUD_MST_SCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static struct clk_regmap mst_d_sclk_div =
	AUD_MST_SCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static struct clk_regmap mst_e_sclk_div =
	AUD_MST_SCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static struct clk_regmap mst_f_sclk_div =
	AUD_MST_SCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

static struct clk_regmap mst_a_sclk_post_en =
	AUD_MST_SCLK_POST_EN(a, AUDIO_MST_A_SCLK_CTRL0);
static struct clk_regmap mst_b_sclk_post_en =
	AUD_MST_SCLK_POST_EN(b, AUDIO_MST_B_SCLK_CTRL0);
static struct clk_regmap mst_c_sclk_post_en =
	AUD_MST_SCLK_POST_EN(c, AUDIO_MST_C_SCLK_CTRL0);
static struct clk_regmap mst_d_sclk_post_en =
	AUD_MST_SCLK_POST_EN(d, AUDIO_MST_D_SCLK_CTRL0);
static struct clk_regmap mst_e_sclk_post_en =
	AUD_MST_SCLK_POST_EN(e, AUDIO_MST_E_SCLK_CTRL0);
static struct clk_regmap mst_f_sclk_post_en =
	AUD_MST_SCLK_POST_EN(f, AUDIO_MST_F_SCLK_CTRL0);

static struct clk_regmap mst_a_sclk =
	AUD_MST_SCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static struct clk_regmap mst_b_sclk =
	AUD_MST_SCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static struct clk_regmap mst_c_sclk =
	AUD_MST_SCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static struct clk_regmap mst_d_sclk =
	AUD_MST_SCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static struct clk_regmap mst_e_sclk =
	AUD_MST_SCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static struct clk_regmap mst_f_sclk =
	AUD_MST_SCLK(f, AUDIO_MST_F_SCLK_CTRL1);

static struct clk_regmap mst_a_lrclk_div =
	AUD_MST_LRCLK_DIV(a, AUDIO_MST_A_SCLK_CTRL0);
static struct clk_regmap mst_b_lrclk_div =
	AUD_MST_LRCLK_DIV(b, AUDIO_MST_B_SCLK_CTRL0);
static struct clk_regmap mst_c_lrclk_div =
	AUD_MST_LRCLK_DIV(c, AUDIO_MST_C_SCLK_CTRL0);
static struct clk_regmap mst_d_lrclk_div =
	AUD_MST_LRCLK_DIV(d, AUDIO_MST_D_SCLK_CTRL0);
static struct clk_regmap mst_e_lrclk_div =
	AUD_MST_LRCLK_DIV(e, AUDIO_MST_E_SCLK_CTRL0);
static struct clk_regmap mst_f_lrclk_div =
	AUD_MST_LRCLK_DIV(f, AUDIO_MST_F_SCLK_CTRL0);

static struct clk_regmap mst_a_lrclk =
	AUD_MST_LRCLK(a, AUDIO_MST_A_SCLK_CTRL1);
static struct clk_regmap mst_b_lrclk =
	AUD_MST_LRCLK(b, AUDIO_MST_B_SCLK_CTRL1);
static struct clk_regmap mst_c_lrclk =
	AUD_MST_LRCLK(c, AUDIO_MST_C_SCLK_CTRL1);
static struct clk_regmap mst_d_lrclk =
	AUD_MST_LRCLK(d, AUDIO_MST_D_SCLK_CTRL1);
static struct clk_regmap mst_e_lrclk =
	AUD_MST_LRCLK(e, AUDIO_MST_E_SCLK_CTRL1);
static struct clk_regmap mst_f_lrclk =
	AUD_MST_LRCLK(f, AUDIO_MST_F_SCLK_CTRL1);

static struct clk_regmap tdmin_a_sclk_sel =
	AUD_TDM_SCLK_MUX(in_a, AUDIO_CLK_TDMIN_A_CTRL);
static struct clk_regmap tdmin_b_sclk_sel =
	AUD_TDM_SCLK_MUX(in_b, AUDIO_CLK_TDMIN_B_CTRL);
static struct clk_regmap tdmin_c_sclk_sel =
	AUD_TDM_SCLK_MUX(in_c, AUDIO_CLK_TDMIN_C_CTRL);
static struct clk_regmap tdmin_lb_sclk_sel =
	AUD_TDM_SCLK_MUX(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static struct clk_regmap tdmout_a_sclk_sel =
	AUD_TDM_SCLK_MUX(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap tdmout_b_sclk_sel =
	AUD_TDM_SCLK_MUX(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap tdmout_c_sclk_sel =
	AUD_TDM_SCLK_MUX(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static struct clk_regmap tdmin_a_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(in_a, AUDIO_CLK_TDMIN_A_CTRL);
static struct clk_regmap tdmin_b_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(in_b, AUDIO_CLK_TDMIN_B_CTRL);
static struct clk_regmap tdmin_c_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(in_c, AUDIO_CLK_TDMIN_C_CTRL);
static struct clk_regmap tdmin_lb_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static struct clk_regmap tdmout_a_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap tdmout_b_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap tdmout_c_sclk_pre_en =
	AUD_TDM_SCLK_PRE_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static struct clk_regmap tdmin_a_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(in_a, AUDIO_CLK_TDMIN_A_CTRL);
static struct clk_regmap tdmin_b_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(in_b, AUDIO_CLK_TDMIN_B_CTRL);
static struct clk_regmap tdmin_c_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(in_c, AUDIO_CLK_TDMIN_C_CTRL);
static struct clk_regmap tdmin_lb_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static struct clk_regmap tdmout_a_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap tdmout_b_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap tdmout_c_sclk_post_en =
	AUD_TDM_SCLK_POST_EN(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static struct clk_regmap tdmin_a_sclk =
	AUD_TDM_SCLK(in_a, AUDIO_CLK_TDMIN_A_CTRL);
static struct clk_regmap tdmin_b_sclk =
	AUD_TDM_SCLK(in_b, AUDIO_CLK_TDMIN_B_CTRL);
static struct clk_regmap tdmin_c_sclk =
	AUD_TDM_SCLK(in_c, AUDIO_CLK_TDMIN_C_CTRL);
static struct clk_regmap tdmin_lb_sclk =
	AUD_TDM_SCLK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);

static struct clk_regmap tdmin_a_lrclk =
	AUD_TDM_LRLCK(in_a, AUDIO_CLK_TDMIN_A_CTRL);
static struct clk_regmap tdmin_b_lrclk =
	AUD_TDM_LRLCK(in_b, AUDIO_CLK_TDMIN_B_CTRL);
static struct clk_regmap tdmin_c_lrclk =
	AUD_TDM_LRLCK(in_c, AUDIO_CLK_TDMIN_C_CTRL);
static struct clk_regmap tdmin_lb_lrclk =
	AUD_TDM_LRLCK(in_lb, AUDIO_CLK_TDMIN_LB_CTRL);
static struct clk_regmap tdmout_a_lrclk =
	AUD_TDM_LRLCK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap tdmout_b_lrclk =
	AUD_TDM_LRLCK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap tdmout_c_lrclk =
	AUD_TDM_LRLCK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

/* AXG Clocks */
static struct clk_regmap axg_tdmout_a_sclk =
	AUD_TDM_SCLK(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap axg_tdmout_b_sclk =
	AUD_TDM_SCLK(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap axg_tdmout_c_sclk =
	AUD_TDM_SCLK(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

/* AXG/G12A Clocks */
static struct clk_hw axg_aud_top = {
	.init = &(struct clk_init_data) {
		/* Provide aud_top signal name on axg and g12a */
		.name = "aud_top",
		.ops = &(const struct clk_ops) {},
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "pclk",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap mst_a_mclk_sel =
	AUD_MST_MCLK_MUX(mst_a_mclk, AUDIO_MCLK_A_CTRL);
static struct clk_regmap mst_b_mclk_sel =
	AUD_MST_MCLK_MUX(mst_b_mclk, AUDIO_MCLK_B_CTRL);
static struct clk_regmap mst_c_mclk_sel =
	AUD_MST_MCLK_MUX(mst_c_mclk, AUDIO_MCLK_C_CTRL);
static struct clk_regmap mst_d_mclk_sel =
	AUD_MST_MCLK_MUX(mst_d_mclk, AUDIO_MCLK_D_CTRL);
static struct clk_regmap mst_e_mclk_sel =
	AUD_MST_MCLK_MUX(mst_e_mclk, AUDIO_MCLK_E_CTRL);
static struct clk_regmap mst_f_mclk_sel =
	AUD_MST_MCLK_MUX(mst_f_mclk, AUDIO_MCLK_F_CTRL);

static struct clk_regmap mst_a_mclk_div =
	AUD_MST_MCLK_DIV(mst_a_mclk, AUDIO_MCLK_A_CTRL);
static struct clk_regmap mst_b_mclk_div =
	AUD_MST_MCLK_DIV(mst_b_mclk, AUDIO_MCLK_B_CTRL);
static struct clk_regmap mst_c_mclk_div =
	AUD_MST_MCLK_DIV(mst_c_mclk, AUDIO_MCLK_C_CTRL);
static struct clk_regmap mst_d_mclk_div =
	AUD_MST_MCLK_DIV(mst_d_mclk, AUDIO_MCLK_D_CTRL);
static struct clk_regmap mst_e_mclk_div =
	AUD_MST_MCLK_DIV(mst_e_mclk, AUDIO_MCLK_E_CTRL);
static struct clk_regmap mst_f_mclk_div =
	AUD_MST_MCLK_DIV(mst_f_mclk, AUDIO_MCLK_F_CTRL);

static struct clk_regmap mst_a_mclk =
	AUD_MST_MCLK_GATE(mst_a_mclk, AUDIO_MCLK_A_CTRL);
static struct clk_regmap mst_b_mclk =
	AUD_MST_MCLK_GATE(mst_b_mclk, AUDIO_MCLK_B_CTRL);
static struct clk_regmap mst_c_mclk =
	AUD_MST_MCLK_GATE(mst_c_mclk, AUDIO_MCLK_C_CTRL);
static struct clk_regmap mst_d_mclk =
	AUD_MST_MCLK_GATE(mst_d_mclk, AUDIO_MCLK_D_CTRL);
static struct clk_regmap mst_e_mclk =
	AUD_MST_MCLK_GATE(mst_e_mclk, AUDIO_MCLK_E_CTRL);
static struct clk_regmap mst_f_mclk =
	AUD_MST_MCLK_GATE(mst_f_mclk, AUDIO_MCLK_F_CTRL);

/* G12a clocks */
static struct clk_regmap g12a_tdm_mclk_pad_0 = AUD_TDM_PAD_CTRL(
	mclk_pad_0, AUDIO_MST_PAD_CTRL0, 0, mclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_mclk_pad_1 = AUD_TDM_PAD_CTRL(
	mclk_pad_1, AUDIO_MST_PAD_CTRL0, 4, mclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_lrclk_pad_0 = AUD_TDM_PAD_CTRL(
	lrclk_pad_0, AUDIO_MST_PAD_CTRL1, 16, lrclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_lrclk_pad_1 = AUD_TDM_PAD_CTRL(
	lrclk_pad_1, AUDIO_MST_PAD_CTRL1, 20, lrclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_lrclk_pad_2 = AUD_TDM_PAD_CTRL(
	lrclk_pad_2, AUDIO_MST_PAD_CTRL1, 24, lrclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_sclk_pad_0 = AUD_TDM_PAD_CTRL(
	sclk_pad_0, AUDIO_MST_PAD_CTRL1, 0, sclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_sclk_pad_1 = AUD_TDM_PAD_CTRL(
	sclk_pad_1, AUDIO_MST_PAD_CTRL1, 4, sclk_pad_ctrl_parent_data);
static struct clk_regmap g12a_tdm_sclk_pad_2 = AUD_TDM_PAD_CTRL(
	sclk_pad_2, AUDIO_MST_PAD_CTRL1, 8, sclk_pad_ctrl_parent_data);

static struct clk_regmap g12a_tdmout_a_sclk =
	AUD_TDM_SCLK_WS(out_a, AUDIO_CLK_TDMOUT_A_CTRL);
static struct clk_regmap g12a_tdmout_b_sclk =
	AUD_TDM_SCLK_WS(out_b, AUDIO_CLK_TDMOUT_B_CTRL);
static struct clk_regmap g12a_tdmout_c_sclk =
	AUD_TDM_SCLK_WS(out_c, AUDIO_CLK_TDMOUT_C_CTRL);

static struct clk_regmap toram =
	AUD_PCLK_GATE(toram, AUDIO_CLK_GATE_EN, 20);
static struct clk_regmap spdifout_b =
	AUD_PCLK_GATE(spdifout_b, AUDIO_CLK_GATE_EN, 21);
static struct clk_regmap eqdrc =
	AUD_PCLK_GATE(eqdrc, AUDIO_CLK_GATE_EN, 22);

/* SM1 Clocks */
static struct clk_regmap sm1_clk81_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AUDIO_CLK81_EN,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "aud_clk81_en",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "pclk",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap sm1_sysclk_a_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = AUDIO_CLK81_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "aud_sysclk_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sm1_clk81_en.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sm1_sysclk_a_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AUDIO_CLK81_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "aud_sysclk_a_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sm1_sysclk_a_div.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sm1_sysclk_b_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = AUDIO_CLK81_CTRL,
		.shift = 16,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "aud_sysclk_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sm1_clk81_en.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sm1_sysclk_b_en = {
	.data = &(struct clk_regmap_gate_data){
		.offset = AUDIO_CLK81_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "aud_sysclk_b_en",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sm1_sysclk_b_div.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *sm1_aud_top_parents[] = {
	&sm1_sysclk_a_en.hw,
	&sm1_sysclk_b_en.hw,
};

static struct clk_regmap sm1_aud_top = {
	.data = &(struct clk_regmap_mux_data){
		.offset = AUDIO_CLK81_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "aud_top",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = sm1_aud_top_parents,
		.num_parents = ARRAY_SIZE(sm1_aud_top_parents),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap resample_b =
	AUD_PCLK_GATE(resample_b, AUDIO_CLK_GATE_EN, 26);
static struct clk_regmap tovad =
	AUD_PCLK_GATE(tovad, AUDIO_CLK_GATE_EN, 27);
static struct clk_regmap locker =
	AUD_PCLK_GATE(locker, AUDIO_CLK_GATE_EN, 28);
static struct clk_regmap spdifin_lb =
	AUD_PCLK_GATE(spdifin_lb, AUDIO_CLK_GATE_EN, 29);
static struct clk_regmap frddr_d =
	AUD_PCLK_GATE(frddr_d, AUDIO_CLK_GATE_EN1, 0);
static struct clk_regmap toddr_d =
	AUD_PCLK_GATE(toddr_d, AUDIO_CLK_GATE_EN1, 1);
static struct clk_regmap loopback_b =
	AUD_PCLK_GATE(loopback_b, AUDIO_CLK_GATE_EN1, 2);

static struct clk_regmap sm1_mst_a_mclk_sel =
	AUD_MST_MCLK_MUX(mst_a_mclk, AUDIO_SM1_MCLK_A_CTRL);
static struct clk_regmap sm1_mst_b_mclk_sel =
	AUD_MST_MCLK_MUX(mst_b_mclk, AUDIO_SM1_MCLK_B_CTRL);
static struct clk_regmap sm1_mst_c_mclk_sel =
	AUD_MST_MCLK_MUX(mst_c_mclk, AUDIO_SM1_MCLK_C_CTRL);
static struct clk_regmap sm1_mst_d_mclk_sel =
	AUD_MST_MCLK_MUX(mst_d_mclk, AUDIO_SM1_MCLK_D_CTRL);
static struct clk_regmap sm1_mst_e_mclk_sel =
	AUD_MST_MCLK_MUX(mst_e_mclk, AUDIO_SM1_MCLK_E_CTRL);
static struct clk_regmap sm1_mst_f_mclk_sel =
	AUD_MST_MCLK_MUX(mst_f_mclk, AUDIO_SM1_MCLK_F_CTRL);

static struct clk_regmap sm1_mst_a_mclk_div =
	AUD_MST_MCLK_DIV(mst_a_mclk, AUDIO_SM1_MCLK_A_CTRL);
static struct clk_regmap sm1_mst_b_mclk_div =
	AUD_MST_MCLK_DIV(mst_b_mclk, AUDIO_SM1_MCLK_B_CTRL);
static struct clk_regmap sm1_mst_c_mclk_div =
	AUD_MST_MCLK_DIV(mst_c_mclk, AUDIO_SM1_MCLK_C_CTRL);
static struct clk_regmap sm1_mst_d_mclk_div =
	AUD_MST_MCLK_DIV(mst_d_mclk, AUDIO_SM1_MCLK_D_CTRL);
static struct clk_regmap sm1_mst_e_mclk_div =
	AUD_MST_MCLK_DIV(mst_e_mclk, AUDIO_SM1_MCLK_E_CTRL);
static struct clk_regmap sm1_mst_f_mclk_div =
	AUD_MST_MCLK_DIV(mst_f_mclk, AUDIO_SM1_MCLK_F_CTRL);

static struct clk_regmap sm1_mst_a_mclk =
	AUD_MST_MCLK_GATE(mst_a_mclk, AUDIO_SM1_MCLK_A_CTRL);
static struct clk_regmap sm1_mst_b_mclk =
	AUD_MST_MCLK_GATE(mst_b_mclk, AUDIO_SM1_MCLK_B_CTRL);
static struct clk_regmap sm1_mst_c_mclk =
	AUD_MST_MCLK_GATE(mst_c_mclk, AUDIO_SM1_MCLK_C_CTRL);
static struct clk_regmap sm1_mst_d_mclk =
	AUD_MST_MCLK_GATE(mst_d_mclk, AUDIO_SM1_MCLK_D_CTRL);
static struct clk_regmap sm1_mst_e_mclk =
	AUD_MST_MCLK_GATE(mst_e_mclk, AUDIO_SM1_MCLK_E_CTRL);
static struct clk_regmap sm1_mst_f_mclk =
	AUD_MST_MCLK_GATE(mst_f_mclk, AUDIO_SM1_MCLK_F_CTRL);

static struct clk_regmap sm1_tdm_mclk_pad_0 = AUD_TDM_PAD_CTRL(
	tdm_mclk_pad_0, AUDIO_SM1_MST_PAD_CTRL0, 0, mclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_mclk_pad_1 = AUD_TDM_PAD_CTRL(
	tdm_mclk_pad_1, AUDIO_SM1_MST_PAD_CTRL0, 4, mclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_lrclk_pad_0 = AUD_TDM_PAD_CTRL(
	tdm_lrclk_pad_0, AUDIO_SM1_MST_PAD_CTRL1, 16, lrclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_lrclk_pad_1 = AUD_TDM_PAD_CTRL(
	tdm_lrclk_pad_1, AUDIO_SM1_MST_PAD_CTRL1, 20, lrclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_lrclk_pad_2 = AUD_TDM_PAD_CTRL(
	tdm_lrclk_pad_2, AUDIO_SM1_MST_PAD_CTRL1, 24, lrclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_sclk_pad_0 = AUD_TDM_PAD_CTRL(
	tdm_sclk_pad_0, AUDIO_SM1_MST_PAD_CTRL1, 0, sclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_sclk_pad_1 = AUD_TDM_PAD_CTRL(
	tdm_sclk_pad_1, AUDIO_SM1_MST_PAD_CTRL1, 4, sclk_pad_ctrl_parent_data);
static struct clk_regmap sm1_tdm_sclk_pad_2 = AUD_TDM_PAD_CTRL(
	tdm_sclk_pad_2, AUDIO_SM1_MST_PAD_CTRL1, 8, sclk_pad_ctrl_parent_data);

/*
 * Array of all clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw *axg_audio_hw_clks[] = {
	[AUD_CLKID_DDR_ARB]		= &ddr_arb.hw,
	[AUD_CLKID_PDM]			= &pdm.hw,
	[AUD_CLKID_TDMIN_A]		= &tdmin_a.hw,
	[AUD_CLKID_TDMIN_B]		= &tdmin_b.hw,
	[AUD_CLKID_TDMIN_C]		= &tdmin_c.hw,
	[AUD_CLKID_TDMIN_LB]		= &tdmin_lb.hw,
	[AUD_CLKID_TDMOUT_A]		= &tdmout_a.hw,
	[AUD_CLKID_TDMOUT_B]		= &tdmout_b.hw,
	[AUD_CLKID_TDMOUT_C]		= &tdmout_c.hw,
	[AUD_CLKID_FRDDR_A]		= &frddr_a.hw,
	[AUD_CLKID_FRDDR_B]		= &frddr_b.hw,
	[AUD_CLKID_FRDDR_C]		= &frddr_c.hw,
	[AUD_CLKID_TODDR_A]		= &toddr_a.hw,
	[AUD_CLKID_TODDR_B]		= &toddr_b.hw,
	[AUD_CLKID_TODDR_C]		= &toddr_c.hw,
	[AUD_CLKID_LOOPBACK]		= &loopback.hw,
	[AUD_CLKID_SPDIFIN]		= &spdifin.hw,
	[AUD_CLKID_SPDIFOUT]		= &spdifout.hw,
	[AUD_CLKID_RESAMPLE]		= &resample.hw,
	[AUD_CLKID_POWER_DETECT]	= &power_detect.hw,
	[AUD_CLKID_MST_A_MCLK_SEL]	= &mst_a_mclk_sel.hw,
	[AUD_CLKID_MST_B_MCLK_SEL]	= &mst_b_mclk_sel.hw,
	[AUD_CLKID_MST_C_MCLK_SEL]	= &mst_c_mclk_sel.hw,
	[AUD_CLKID_MST_D_MCLK_SEL]	= &mst_d_mclk_sel.hw,
	[AUD_CLKID_MST_E_MCLK_SEL]	= &mst_e_mclk_sel.hw,
	[AUD_CLKID_MST_F_MCLK_SEL]	= &mst_f_mclk_sel.hw,
	[AUD_CLKID_MST_A_MCLK_DIV]	= &mst_a_mclk_div.hw,
	[AUD_CLKID_MST_B_MCLK_DIV]	= &mst_b_mclk_div.hw,
	[AUD_CLKID_MST_C_MCLK_DIV]	= &mst_c_mclk_div.hw,
	[AUD_CLKID_MST_D_MCLK_DIV]	= &mst_d_mclk_div.hw,
	[AUD_CLKID_MST_E_MCLK_DIV]	= &mst_e_mclk_div.hw,
	[AUD_CLKID_MST_F_MCLK_DIV]	= &mst_f_mclk_div.hw,
	[AUD_CLKID_MST_A_MCLK]		= &mst_a_mclk.hw,
	[AUD_CLKID_MST_B_MCLK]		= &mst_b_mclk.hw,
	[AUD_CLKID_MST_C_MCLK]		= &mst_c_mclk.hw,
	[AUD_CLKID_MST_D_MCLK]		= &mst_d_mclk.hw,
	[AUD_CLKID_MST_E_MCLK]		= &mst_e_mclk.hw,
	[AUD_CLKID_MST_F_MCLK]		= &mst_f_mclk.hw,
	[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &spdifout_clk_sel.hw,
	[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &spdifout_clk_div.hw,
	[AUD_CLKID_SPDIFOUT_CLK]	= &spdifout_clk.hw,
	[AUD_CLKID_SPDIFIN_CLK_SEL]	= &spdifin_clk_sel.hw,
	[AUD_CLKID_SPDIFIN_CLK_DIV]	= &spdifin_clk_div.hw,
	[AUD_CLKID_SPDIFIN_CLK]		= &spdifin_clk.hw,
	[AUD_CLKID_PDM_DCLK_SEL]	= &pdm_dclk_sel.hw,
	[AUD_CLKID_PDM_DCLK_DIV]	= &pdm_dclk_div.hw,
	[AUD_CLKID_PDM_DCLK]		= &pdm_dclk.hw,
	[AUD_CLKID_PDM_SYSCLK_SEL]	= &pdm_sysclk_sel.hw,
	[AUD_CLKID_PDM_SYSCLK_DIV]	= &pdm_sysclk_div.hw,
	[AUD_CLKID_PDM_SYSCLK]		= &pdm_sysclk.hw,
	[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &mst_a_sclk_pre_en.hw,
	[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &mst_b_sclk_pre_en.hw,
	[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &mst_c_sclk_pre_en.hw,
	[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &mst_d_sclk_pre_en.hw,
	[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &mst_e_sclk_pre_en.hw,
	[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &mst_f_sclk_pre_en.hw,
	[AUD_CLKID_MST_A_SCLK_DIV]	= &mst_a_sclk_div.hw,
	[AUD_CLKID_MST_B_SCLK_DIV]	= &mst_b_sclk_div.hw,
	[AUD_CLKID_MST_C_SCLK_DIV]	= &mst_c_sclk_div.hw,
	[AUD_CLKID_MST_D_SCLK_DIV]	= &mst_d_sclk_div.hw,
	[AUD_CLKID_MST_E_SCLK_DIV]	= &mst_e_sclk_div.hw,
	[AUD_CLKID_MST_F_SCLK_DIV]	= &mst_f_sclk_div.hw,
	[AUD_CLKID_MST_A_SCLK_POST_EN]	= &mst_a_sclk_post_en.hw,
	[AUD_CLKID_MST_B_SCLK_POST_EN]	= &mst_b_sclk_post_en.hw,
	[AUD_CLKID_MST_C_SCLK_POST_EN]	= &mst_c_sclk_post_en.hw,
	[AUD_CLKID_MST_D_SCLK_POST_EN]	= &mst_d_sclk_post_en.hw,
	[AUD_CLKID_MST_E_SCLK_POST_EN]	= &mst_e_sclk_post_en.hw,
	[AUD_CLKID_MST_F_SCLK_POST_EN]	= &mst_f_sclk_post_en.hw,
	[AUD_CLKID_MST_A_SCLK]		= &mst_a_sclk.hw,
	[AUD_CLKID_MST_B_SCLK]		= &mst_b_sclk.hw,
	[AUD_CLKID_MST_C_SCLK]		= &mst_c_sclk.hw,
	[AUD_CLKID_MST_D_SCLK]		= &mst_d_sclk.hw,
	[AUD_CLKID_MST_E_SCLK]		= &mst_e_sclk.hw,
	[AUD_CLKID_MST_F_SCLK]		= &mst_f_sclk.hw,
	[AUD_CLKID_MST_A_LRCLK_DIV]	= &mst_a_lrclk_div.hw,
	[AUD_CLKID_MST_B_LRCLK_DIV]	= &mst_b_lrclk_div.hw,
	[AUD_CLKID_MST_C_LRCLK_DIV]	= &mst_c_lrclk_div.hw,
	[AUD_CLKID_MST_D_LRCLK_DIV]	= &mst_d_lrclk_div.hw,
	[AUD_CLKID_MST_E_LRCLK_DIV]	= &mst_e_lrclk_div.hw,
	[AUD_CLKID_MST_F_LRCLK_DIV]	= &mst_f_lrclk_div.hw,
	[AUD_CLKID_MST_A_LRCLK]		= &mst_a_lrclk.hw,
	[AUD_CLKID_MST_B_LRCLK]		= &mst_b_lrclk.hw,
	[AUD_CLKID_MST_C_LRCLK]		= &mst_c_lrclk.hw,
	[AUD_CLKID_MST_D_LRCLK]		= &mst_d_lrclk.hw,
	[AUD_CLKID_MST_E_LRCLK]		= &mst_e_lrclk.hw,
	[AUD_CLKID_MST_F_LRCLK]		= &mst_f_lrclk.hw,
	[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &tdmin_a_sclk_sel.hw,
	[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &tdmin_b_sclk_sel.hw,
	[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &tdmin_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &tdmin_lb_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &tdmout_a_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &tdmout_b_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &tdmout_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &tdmin_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &tdmin_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &tdmin_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &tdmin_lb_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &tdmout_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &tdmout_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &tdmout_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &tdmin_a_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &tdmin_b_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &tdmin_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &tdmin_lb_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &tdmout_a_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &tdmout_b_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &tdmout_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK]	= &tdmin_a_sclk.hw,
	[AUD_CLKID_TDMIN_B_SCLK]	= &tdmin_b_sclk.hw,
	[AUD_CLKID_TDMIN_C_SCLK]	= &tdmin_c_sclk.hw,
	[AUD_CLKID_TDMIN_LB_SCLK]	= &tdmin_lb_sclk.hw,
	[AUD_CLKID_TDMOUT_A_SCLK]	= &axg_tdmout_a_sclk.hw,
	[AUD_CLKID_TDMOUT_B_SCLK]	= &axg_tdmout_b_sclk.hw,
	[AUD_CLKID_TDMOUT_C_SCLK]	= &axg_tdmout_c_sclk.hw,
	[AUD_CLKID_TDMIN_A_LRCLK]	= &tdmin_a_lrclk.hw,
	[AUD_CLKID_TDMIN_B_LRCLK]	= &tdmin_b_lrclk.hw,
	[AUD_CLKID_TDMIN_C_LRCLK]	= &tdmin_c_lrclk.hw,
	[AUD_CLKID_TDMIN_LB_LRCLK]	= &tdmin_lb_lrclk.hw,
	[AUD_CLKID_TDMOUT_A_LRCLK]	= &tdmout_a_lrclk.hw,
	[AUD_CLKID_TDMOUT_B_LRCLK]	= &tdmout_b_lrclk.hw,
	[AUD_CLKID_TDMOUT_C_LRCLK]	= &tdmout_c_lrclk.hw,
	[AUD_CLKID_TOP]			= &axg_aud_top,
};

/*
 * Array of all G12A clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw *g12a_audio_hw_clks[] = {
	[AUD_CLKID_DDR_ARB]		= &ddr_arb.hw,
	[AUD_CLKID_PDM]			= &pdm.hw,
	[AUD_CLKID_TDMIN_A]		= &tdmin_a.hw,
	[AUD_CLKID_TDMIN_B]		= &tdmin_b.hw,
	[AUD_CLKID_TDMIN_C]		= &tdmin_c.hw,
	[AUD_CLKID_TDMIN_LB]		= &tdmin_lb.hw,
	[AUD_CLKID_TDMOUT_A]		= &tdmout_a.hw,
	[AUD_CLKID_TDMOUT_B]		= &tdmout_b.hw,
	[AUD_CLKID_TDMOUT_C]		= &tdmout_c.hw,
	[AUD_CLKID_FRDDR_A]		= &frddr_a.hw,
	[AUD_CLKID_FRDDR_B]		= &frddr_b.hw,
	[AUD_CLKID_FRDDR_C]		= &frddr_c.hw,
	[AUD_CLKID_TODDR_A]		= &toddr_a.hw,
	[AUD_CLKID_TODDR_B]		= &toddr_b.hw,
	[AUD_CLKID_TODDR_C]		= &toddr_c.hw,
	[AUD_CLKID_LOOPBACK]		= &loopback.hw,
	[AUD_CLKID_SPDIFIN]		= &spdifin.hw,
	[AUD_CLKID_SPDIFOUT]		= &spdifout.hw,
	[AUD_CLKID_RESAMPLE]		= &resample.hw,
	[AUD_CLKID_POWER_DETECT]	= &power_detect.hw,
	[AUD_CLKID_SPDIFOUT_B]		= &spdifout_b.hw,
	[AUD_CLKID_MST_A_MCLK_SEL]	= &mst_a_mclk_sel.hw,
	[AUD_CLKID_MST_B_MCLK_SEL]	= &mst_b_mclk_sel.hw,
	[AUD_CLKID_MST_C_MCLK_SEL]	= &mst_c_mclk_sel.hw,
	[AUD_CLKID_MST_D_MCLK_SEL]	= &mst_d_mclk_sel.hw,
	[AUD_CLKID_MST_E_MCLK_SEL]	= &mst_e_mclk_sel.hw,
	[AUD_CLKID_MST_F_MCLK_SEL]	= &mst_f_mclk_sel.hw,
	[AUD_CLKID_MST_A_MCLK_DIV]	= &mst_a_mclk_div.hw,
	[AUD_CLKID_MST_B_MCLK_DIV]	= &mst_b_mclk_div.hw,
	[AUD_CLKID_MST_C_MCLK_DIV]	= &mst_c_mclk_div.hw,
	[AUD_CLKID_MST_D_MCLK_DIV]	= &mst_d_mclk_div.hw,
	[AUD_CLKID_MST_E_MCLK_DIV]	= &mst_e_mclk_div.hw,
	[AUD_CLKID_MST_F_MCLK_DIV]	= &mst_f_mclk_div.hw,
	[AUD_CLKID_MST_A_MCLK]		= &mst_a_mclk.hw,
	[AUD_CLKID_MST_B_MCLK]		= &mst_b_mclk.hw,
	[AUD_CLKID_MST_C_MCLK]		= &mst_c_mclk.hw,
	[AUD_CLKID_MST_D_MCLK]		= &mst_d_mclk.hw,
	[AUD_CLKID_MST_E_MCLK]		= &mst_e_mclk.hw,
	[AUD_CLKID_MST_F_MCLK]		= &mst_f_mclk.hw,
	[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &spdifout_clk_sel.hw,
	[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &spdifout_clk_div.hw,
	[AUD_CLKID_SPDIFOUT_CLK]	= &spdifout_clk.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK_SEL]	= &spdifout_b_clk_sel.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK_DIV]	= &spdifout_b_clk_div.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK]	= &spdifout_b_clk.hw,
	[AUD_CLKID_SPDIFIN_CLK_SEL]	= &spdifin_clk_sel.hw,
	[AUD_CLKID_SPDIFIN_CLK_DIV]	= &spdifin_clk_div.hw,
	[AUD_CLKID_SPDIFIN_CLK]		= &spdifin_clk.hw,
	[AUD_CLKID_PDM_DCLK_SEL]	= &pdm_dclk_sel.hw,
	[AUD_CLKID_PDM_DCLK_DIV]	= &pdm_dclk_div.hw,
	[AUD_CLKID_PDM_DCLK]		= &pdm_dclk.hw,
	[AUD_CLKID_PDM_SYSCLK_SEL]	= &pdm_sysclk_sel.hw,
	[AUD_CLKID_PDM_SYSCLK_DIV]	= &pdm_sysclk_div.hw,
	[AUD_CLKID_PDM_SYSCLK]		= &pdm_sysclk.hw,
	[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &mst_a_sclk_pre_en.hw,
	[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &mst_b_sclk_pre_en.hw,
	[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &mst_c_sclk_pre_en.hw,
	[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &mst_d_sclk_pre_en.hw,
	[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &mst_e_sclk_pre_en.hw,
	[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &mst_f_sclk_pre_en.hw,
	[AUD_CLKID_MST_A_SCLK_DIV]	= &mst_a_sclk_div.hw,
	[AUD_CLKID_MST_B_SCLK_DIV]	= &mst_b_sclk_div.hw,
	[AUD_CLKID_MST_C_SCLK_DIV]	= &mst_c_sclk_div.hw,
	[AUD_CLKID_MST_D_SCLK_DIV]	= &mst_d_sclk_div.hw,
	[AUD_CLKID_MST_E_SCLK_DIV]	= &mst_e_sclk_div.hw,
	[AUD_CLKID_MST_F_SCLK_DIV]	= &mst_f_sclk_div.hw,
	[AUD_CLKID_MST_A_SCLK_POST_EN]	= &mst_a_sclk_post_en.hw,
	[AUD_CLKID_MST_B_SCLK_POST_EN]	= &mst_b_sclk_post_en.hw,
	[AUD_CLKID_MST_C_SCLK_POST_EN]	= &mst_c_sclk_post_en.hw,
	[AUD_CLKID_MST_D_SCLK_POST_EN]	= &mst_d_sclk_post_en.hw,
	[AUD_CLKID_MST_E_SCLK_POST_EN]	= &mst_e_sclk_post_en.hw,
	[AUD_CLKID_MST_F_SCLK_POST_EN]	= &mst_f_sclk_post_en.hw,
	[AUD_CLKID_MST_A_SCLK]		= &mst_a_sclk.hw,
	[AUD_CLKID_MST_B_SCLK]		= &mst_b_sclk.hw,
	[AUD_CLKID_MST_C_SCLK]		= &mst_c_sclk.hw,
	[AUD_CLKID_MST_D_SCLK]		= &mst_d_sclk.hw,
	[AUD_CLKID_MST_E_SCLK]		= &mst_e_sclk.hw,
	[AUD_CLKID_MST_F_SCLK]		= &mst_f_sclk.hw,
	[AUD_CLKID_MST_A_LRCLK_DIV]	= &mst_a_lrclk_div.hw,
	[AUD_CLKID_MST_B_LRCLK_DIV]	= &mst_b_lrclk_div.hw,
	[AUD_CLKID_MST_C_LRCLK_DIV]	= &mst_c_lrclk_div.hw,
	[AUD_CLKID_MST_D_LRCLK_DIV]	= &mst_d_lrclk_div.hw,
	[AUD_CLKID_MST_E_LRCLK_DIV]	= &mst_e_lrclk_div.hw,
	[AUD_CLKID_MST_F_LRCLK_DIV]	= &mst_f_lrclk_div.hw,
	[AUD_CLKID_MST_A_LRCLK]		= &mst_a_lrclk.hw,
	[AUD_CLKID_MST_B_LRCLK]		= &mst_b_lrclk.hw,
	[AUD_CLKID_MST_C_LRCLK]		= &mst_c_lrclk.hw,
	[AUD_CLKID_MST_D_LRCLK]		= &mst_d_lrclk.hw,
	[AUD_CLKID_MST_E_LRCLK]		= &mst_e_lrclk.hw,
	[AUD_CLKID_MST_F_LRCLK]		= &mst_f_lrclk.hw,
	[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &tdmin_a_sclk_sel.hw,
	[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &tdmin_b_sclk_sel.hw,
	[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &tdmin_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &tdmin_lb_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &tdmout_a_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &tdmout_b_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &tdmout_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &tdmin_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &tdmin_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &tdmin_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &tdmin_lb_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &tdmout_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &tdmout_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &tdmout_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &tdmin_a_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &tdmin_b_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &tdmin_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &tdmin_lb_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &tdmout_a_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &tdmout_b_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &tdmout_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK]	= &tdmin_a_sclk.hw,
	[AUD_CLKID_TDMIN_B_SCLK]	= &tdmin_b_sclk.hw,
	[AUD_CLKID_TDMIN_C_SCLK]	= &tdmin_c_sclk.hw,
	[AUD_CLKID_TDMIN_LB_SCLK]	= &tdmin_lb_sclk.hw,
	[AUD_CLKID_TDMOUT_A_SCLK]	= &g12a_tdmout_a_sclk.hw,
	[AUD_CLKID_TDMOUT_B_SCLK]	= &g12a_tdmout_b_sclk.hw,
	[AUD_CLKID_TDMOUT_C_SCLK]	= &g12a_tdmout_c_sclk.hw,
	[AUD_CLKID_TDMIN_A_LRCLK]	= &tdmin_a_lrclk.hw,
	[AUD_CLKID_TDMIN_B_LRCLK]	= &tdmin_b_lrclk.hw,
	[AUD_CLKID_TDMIN_C_LRCLK]	= &tdmin_c_lrclk.hw,
	[AUD_CLKID_TDMIN_LB_LRCLK]	= &tdmin_lb_lrclk.hw,
	[AUD_CLKID_TDMOUT_A_LRCLK]	= &tdmout_a_lrclk.hw,
	[AUD_CLKID_TDMOUT_B_LRCLK]	= &tdmout_b_lrclk.hw,
	[AUD_CLKID_TDMOUT_C_LRCLK]	= &tdmout_c_lrclk.hw,
	[AUD_CLKID_TDM_MCLK_PAD0]	= &g12a_tdm_mclk_pad_0.hw,
	[AUD_CLKID_TDM_MCLK_PAD1]	= &g12a_tdm_mclk_pad_1.hw,
	[AUD_CLKID_TDM_LRCLK_PAD0]	= &g12a_tdm_lrclk_pad_0.hw,
	[AUD_CLKID_TDM_LRCLK_PAD1]	= &g12a_tdm_lrclk_pad_1.hw,
	[AUD_CLKID_TDM_LRCLK_PAD2]	= &g12a_tdm_lrclk_pad_2.hw,
	[AUD_CLKID_TDM_SCLK_PAD0]	= &g12a_tdm_sclk_pad_0.hw,
	[AUD_CLKID_TDM_SCLK_PAD1]	= &g12a_tdm_sclk_pad_1.hw,
	[AUD_CLKID_TDM_SCLK_PAD2]	= &g12a_tdm_sclk_pad_2.hw,
	[AUD_CLKID_TOP]			= &axg_aud_top,
};

/*
 * Array of all SM1 clocks provided by this provider
 * The input clocks of the controller will be populated at runtime
 */
static struct clk_hw *sm1_audio_hw_clks[] = {
	[AUD_CLKID_DDR_ARB]		= &ddr_arb.hw,
	[AUD_CLKID_PDM]			= &pdm.hw,
	[AUD_CLKID_TDMIN_A]		= &tdmin_a.hw,
	[AUD_CLKID_TDMIN_B]		= &tdmin_b.hw,
	[AUD_CLKID_TDMIN_C]		= &tdmin_c.hw,
	[AUD_CLKID_TDMIN_LB]		= &tdmin_lb.hw,
	[AUD_CLKID_TDMOUT_A]		= &tdmout_a.hw,
	[AUD_CLKID_TDMOUT_B]		= &tdmout_b.hw,
	[AUD_CLKID_TDMOUT_C]		= &tdmout_c.hw,
	[AUD_CLKID_FRDDR_A]		= &frddr_a.hw,
	[AUD_CLKID_FRDDR_B]		= &frddr_b.hw,
	[AUD_CLKID_FRDDR_C]		= &frddr_c.hw,
	[AUD_CLKID_TODDR_A]		= &toddr_a.hw,
	[AUD_CLKID_TODDR_B]		= &toddr_b.hw,
	[AUD_CLKID_TODDR_C]		= &toddr_c.hw,
	[AUD_CLKID_LOOPBACK]		= &loopback.hw,
	[AUD_CLKID_SPDIFIN]		= &spdifin.hw,
	[AUD_CLKID_SPDIFOUT]		= &spdifout.hw,
	[AUD_CLKID_RESAMPLE]		= &resample.hw,
	[AUD_CLKID_SPDIFOUT_B]		= &spdifout_b.hw,
	[AUD_CLKID_MST_A_MCLK_SEL]	= &sm1_mst_a_mclk_sel.hw,
	[AUD_CLKID_MST_B_MCLK_SEL]	= &sm1_mst_b_mclk_sel.hw,
	[AUD_CLKID_MST_C_MCLK_SEL]	= &sm1_mst_c_mclk_sel.hw,
	[AUD_CLKID_MST_D_MCLK_SEL]	= &sm1_mst_d_mclk_sel.hw,
	[AUD_CLKID_MST_E_MCLK_SEL]	= &sm1_mst_e_mclk_sel.hw,
	[AUD_CLKID_MST_F_MCLK_SEL]	= &sm1_mst_f_mclk_sel.hw,
	[AUD_CLKID_MST_A_MCLK_DIV]	= &sm1_mst_a_mclk_div.hw,
	[AUD_CLKID_MST_B_MCLK_DIV]	= &sm1_mst_b_mclk_div.hw,
	[AUD_CLKID_MST_C_MCLK_DIV]	= &sm1_mst_c_mclk_div.hw,
	[AUD_CLKID_MST_D_MCLK_DIV]	= &sm1_mst_d_mclk_div.hw,
	[AUD_CLKID_MST_E_MCLK_DIV]	= &sm1_mst_e_mclk_div.hw,
	[AUD_CLKID_MST_F_MCLK_DIV]	= &sm1_mst_f_mclk_div.hw,
	[AUD_CLKID_MST_A_MCLK]		= &sm1_mst_a_mclk.hw,
	[AUD_CLKID_MST_B_MCLK]		= &sm1_mst_b_mclk.hw,
	[AUD_CLKID_MST_C_MCLK]		= &sm1_mst_c_mclk.hw,
	[AUD_CLKID_MST_D_MCLK]		= &sm1_mst_d_mclk.hw,
	[AUD_CLKID_MST_E_MCLK]		= &sm1_mst_e_mclk.hw,
	[AUD_CLKID_MST_F_MCLK]		= &sm1_mst_f_mclk.hw,
	[AUD_CLKID_SPDIFOUT_CLK_SEL]	= &spdifout_clk_sel.hw,
	[AUD_CLKID_SPDIFOUT_CLK_DIV]	= &spdifout_clk_div.hw,
	[AUD_CLKID_SPDIFOUT_CLK]	= &spdifout_clk.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK_SEL]	= &spdifout_b_clk_sel.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK_DIV]	= &spdifout_b_clk_div.hw,
	[AUD_CLKID_SPDIFOUT_B_CLK]	= &spdifout_b_clk.hw,
	[AUD_CLKID_SPDIFIN_CLK_SEL]	= &spdifin_clk_sel.hw,
	[AUD_CLKID_SPDIFIN_CLK_DIV]	= &spdifin_clk_div.hw,
	[AUD_CLKID_SPDIFIN_CLK]		= &spdifin_clk.hw,
	[AUD_CLKID_PDM_DCLK_SEL]	= &pdm_dclk_sel.hw,
	[AUD_CLKID_PDM_DCLK_DIV]	= &pdm_dclk_div.hw,
	[AUD_CLKID_PDM_DCLK]		= &pdm_dclk.hw,
	[AUD_CLKID_PDM_SYSCLK_SEL]	= &pdm_sysclk_sel.hw,
	[AUD_CLKID_PDM_SYSCLK_DIV]	= &pdm_sysclk_div.hw,
	[AUD_CLKID_PDM_SYSCLK]		= &pdm_sysclk.hw,
	[AUD_CLKID_MST_A_SCLK_PRE_EN]	= &mst_a_sclk_pre_en.hw,
	[AUD_CLKID_MST_B_SCLK_PRE_EN]	= &mst_b_sclk_pre_en.hw,
	[AUD_CLKID_MST_C_SCLK_PRE_EN]	= &mst_c_sclk_pre_en.hw,
	[AUD_CLKID_MST_D_SCLK_PRE_EN]	= &mst_d_sclk_pre_en.hw,
	[AUD_CLKID_MST_E_SCLK_PRE_EN]	= &mst_e_sclk_pre_en.hw,
	[AUD_CLKID_MST_F_SCLK_PRE_EN]	= &mst_f_sclk_pre_en.hw,
	[AUD_CLKID_MST_A_SCLK_DIV]	= &mst_a_sclk_div.hw,
	[AUD_CLKID_MST_B_SCLK_DIV]	= &mst_b_sclk_div.hw,
	[AUD_CLKID_MST_C_SCLK_DIV]	= &mst_c_sclk_div.hw,
	[AUD_CLKID_MST_D_SCLK_DIV]	= &mst_d_sclk_div.hw,
	[AUD_CLKID_MST_E_SCLK_DIV]	= &mst_e_sclk_div.hw,
	[AUD_CLKID_MST_F_SCLK_DIV]	= &mst_f_sclk_div.hw,
	[AUD_CLKID_MST_A_SCLK_POST_EN]	= &mst_a_sclk_post_en.hw,
	[AUD_CLKID_MST_B_SCLK_POST_EN]	= &mst_b_sclk_post_en.hw,
	[AUD_CLKID_MST_C_SCLK_POST_EN]	= &mst_c_sclk_post_en.hw,
	[AUD_CLKID_MST_D_SCLK_POST_EN]	= &mst_d_sclk_post_en.hw,
	[AUD_CLKID_MST_E_SCLK_POST_EN]	= &mst_e_sclk_post_en.hw,
	[AUD_CLKID_MST_F_SCLK_POST_EN]	= &mst_f_sclk_post_en.hw,
	[AUD_CLKID_MST_A_SCLK]		= &mst_a_sclk.hw,
	[AUD_CLKID_MST_B_SCLK]		= &mst_b_sclk.hw,
	[AUD_CLKID_MST_C_SCLK]		= &mst_c_sclk.hw,
	[AUD_CLKID_MST_D_SCLK]		= &mst_d_sclk.hw,
	[AUD_CLKID_MST_E_SCLK]		= &mst_e_sclk.hw,
	[AUD_CLKID_MST_F_SCLK]		= &mst_f_sclk.hw,
	[AUD_CLKID_MST_A_LRCLK_DIV]	= &mst_a_lrclk_div.hw,
	[AUD_CLKID_MST_B_LRCLK_DIV]	= &mst_b_lrclk_div.hw,
	[AUD_CLKID_MST_C_LRCLK_DIV]	= &mst_c_lrclk_div.hw,
	[AUD_CLKID_MST_D_LRCLK_DIV]	= &mst_d_lrclk_div.hw,
	[AUD_CLKID_MST_E_LRCLK_DIV]	= &mst_e_lrclk_div.hw,
	[AUD_CLKID_MST_F_LRCLK_DIV]	= &mst_f_lrclk_div.hw,
	[AUD_CLKID_MST_A_LRCLK]		= &mst_a_lrclk.hw,
	[AUD_CLKID_MST_B_LRCLK]		= &mst_b_lrclk.hw,
	[AUD_CLKID_MST_C_LRCLK]		= &mst_c_lrclk.hw,
	[AUD_CLKID_MST_D_LRCLK]		= &mst_d_lrclk.hw,
	[AUD_CLKID_MST_E_LRCLK]		= &mst_e_lrclk.hw,
	[AUD_CLKID_MST_F_LRCLK]		= &mst_f_lrclk.hw,
	[AUD_CLKID_TDMIN_A_SCLK_SEL]	= &tdmin_a_sclk_sel.hw,
	[AUD_CLKID_TDMIN_B_SCLK_SEL]	= &tdmin_b_sclk_sel.hw,
	[AUD_CLKID_TDMIN_C_SCLK_SEL]	= &tdmin_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_SEL]	= &tdmin_lb_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_SEL]	= &tdmout_a_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_SEL]	= &tdmout_b_sclk_sel.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_SEL]	= &tdmout_c_sclk_sel.hw,
	[AUD_CLKID_TDMIN_A_SCLK_PRE_EN]	= &tdmin_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_PRE_EN]	= &tdmin_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_PRE_EN]	= &tdmin_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_PRE_EN] = &tdmin_lb_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_PRE_EN] = &tdmout_a_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_PRE_EN] = &tdmout_b_sclk_pre_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_PRE_EN] = &tdmout_c_sclk_pre_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK_POST_EN] = &tdmin_a_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_B_SCLK_POST_EN] = &tdmin_b_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_C_SCLK_POST_EN] = &tdmin_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_LB_SCLK_POST_EN] = &tdmin_lb_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_A_SCLK_POST_EN] = &tdmout_a_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_B_SCLK_POST_EN] = &tdmout_b_sclk_post_en.hw,
	[AUD_CLKID_TDMOUT_C_SCLK_POST_EN] = &tdmout_c_sclk_post_en.hw,
	[AUD_CLKID_TDMIN_A_SCLK]	= &tdmin_a_sclk.hw,
	[AUD_CLKID_TDMIN_B_SCLK]	= &tdmin_b_sclk.hw,
	[AUD_CLKID_TDMIN_C_SCLK]	= &tdmin_c_sclk.hw,
	[AUD_CLKID_TDMIN_LB_SCLK]	= &tdmin_lb_sclk.hw,
	[AUD_CLKID_TDMOUT_A_SCLK]	= &g12a_tdmout_a_sclk.hw,
	[AUD_CLKID_TDMOUT_B_SCLK]	= &g12a_tdmout_b_sclk.hw,
	[AUD_CLKID_TDMOUT_C_SCLK]	= &g12a_tdmout_c_sclk.hw,
	[AUD_CLKID_TDMIN_A_LRCLK]	= &tdmin_a_lrclk.hw,
	[AUD_CLKID_TDMIN_B_LRCLK]	= &tdmin_b_lrclk.hw,
	[AUD_CLKID_TDMIN_C_LRCLK]	= &tdmin_c_lrclk.hw,
	[AUD_CLKID_TDMIN_LB_LRCLK]	= &tdmin_lb_lrclk.hw,
	[AUD_CLKID_TDMOUT_A_LRCLK]	= &tdmout_a_lrclk.hw,
	[AUD_CLKID_TDMOUT_B_LRCLK]	= &tdmout_b_lrclk.hw,
	[AUD_CLKID_TDMOUT_C_LRCLK]	= &tdmout_c_lrclk.hw,
	[AUD_CLKID_TDM_MCLK_PAD0]	= &sm1_tdm_mclk_pad_0.hw,
	[AUD_CLKID_TDM_MCLK_PAD1]	= &sm1_tdm_mclk_pad_1.hw,
	[AUD_CLKID_TDM_LRCLK_PAD0]	= &sm1_tdm_lrclk_pad_0.hw,
	[AUD_CLKID_TDM_LRCLK_PAD1]	= &sm1_tdm_lrclk_pad_1.hw,
	[AUD_CLKID_TDM_LRCLK_PAD2]	= &sm1_tdm_lrclk_pad_2.hw,
	[AUD_CLKID_TDM_SCLK_PAD0]	= &sm1_tdm_sclk_pad_0.hw,
	[AUD_CLKID_TDM_SCLK_PAD1]	= &sm1_tdm_sclk_pad_1.hw,
	[AUD_CLKID_TDM_SCLK_PAD2]	= &sm1_tdm_sclk_pad_2.hw,
	[AUD_CLKID_TOP]			= &sm1_aud_top.hw,
	[AUD_CLKID_TORAM]		= &toram.hw,
	[AUD_CLKID_EQDRC]		= &eqdrc.hw,
	[AUD_CLKID_RESAMPLE_B]		= &resample_b.hw,
	[AUD_CLKID_TOVAD]		= &tovad.hw,
	[AUD_CLKID_LOCKER]		= &locker.hw,
	[AUD_CLKID_SPDIFIN_LB]		= &spdifin_lb.hw,
	[AUD_CLKID_FRDDR_D]		= &frddr_d.hw,
	[AUD_CLKID_TODDR_D]		= &toddr_d.hw,
	[AUD_CLKID_LOOPBACK_B]		= &loopback_b.hw,
	[AUD_CLKID_CLK81_EN]		= &sm1_clk81_en.hw,
	[AUD_CLKID_SYSCLK_A_DIV]	= &sm1_sysclk_a_div.hw,
	[AUD_CLKID_SYSCLK_A_EN]		= &sm1_sysclk_a_en.hw,
	[AUD_CLKID_SYSCLK_B_DIV]	= &sm1_sysclk_b_div.hw,
	[AUD_CLKID_SYSCLK_B_EN]		= &sm1_sysclk_b_en.hw,
};


/* Convenience table to populate regmap in .probe(). */
static struct clk_regmap *const axg_clk_regmaps[] = {
	&ddr_arb,
	&pdm,
	&tdmin_a,
	&tdmin_b,
	&tdmin_c,
	&tdmin_lb,
	&tdmout_a,
	&tdmout_b,
	&tdmout_c,
	&frddr_a,
	&frddr_b,
	&frddr_c,
	&toddr_a,
	&toddr_b,
	&toddr_c,
	&loopback,
	&spdifin,
	&spdifout,
	&resample,
	&power_detect,
	&mst_a_mclk_sel,
	&mst_b_mclk_sel,
	&mst_c_mclk_sel,
	&mst_d_mclk_sel,
	&mst_e_mclk_sel,
	&mst_f_mclk_sel,
	&mst_a_mclk_div,
	&mst_b_mclk_div,
	&mst_c_mclk_div,
	&mst_d_mclk_div,
	&mst_e_mclk_div,
	&mst_f_mclk_div,
	&mst_a_mclk,
	&mst_b_mclk,
	&mst_c_mclk,
	&mst_d_mclk,
	&mst_e_mclk,
	&mst_f_mclk,
	&spdifout_clk_sel,
	&spdifout_clk_div,
	&spdifout_clk,
	&spdifin_clk_sel,
	&spdifin_clk_div,
	&spdifin_clk,
	&pdm_dclk_sel,
	&pdm_dclk_div,
	&pdm_dclk,
	&pdm_sysclk_sel,
	&pdm_sysclk_div,
	&pdm_sysclk,
	&mst_a_sclk_pre_en,
	&mst_b_sclk_pre_en,
	&mst_c_sclk_pre_en,
	&mst_d_sclk_pre_en,
	&mst_e_sclk_pre_en,
	&mst_f_sclk_pre_en,
	&mst_a_sclk_div,
	&mst_b_sclk_div,
	&mst_c_sclk_div,
	&mst_d_sclk_div,
	&mst_e_sclk_div,
	&mst_f_sclk_div,
	&mst_a_sclk_post_en,
	&mst_b_sclk_post_en,
	&mst_c_sclk_post_en,
	&mst_d_sclk_post_en,
	&mst_e_sclk_post_en,
	&mst_f_sclk_post_en,
	&mst_a_sclk,
	&mst_b_sclk,
	&mst_c_sclk,
	&mst_d_sclk,
	&mst_e_sclk,
	&mst_f_sclk,
	&mst_a_lrclk_div,
	&mst_b_lrclk_div,
	&mst_c_lrclk_div,
	&mst_d_lrclk_div,
	&mst_e_lrclk_div,
	&mst_f_lrclk_div,
	&mst_a_lrclk,
	&mst_b_lrclk,
	&mst_c_lrclk,
	&mst_d_lrclk,
	&mst_e_lrclk,
	&mst_f_lrclk,
	&tdmin_a_sclk_sel,
	&tdmin_b_sclk_sel,
	&tdmin_c_sclk_sel,
	&tdmin_lb_sclk_sel,
	&tdmout_a_sclk_sel,
	&tdmout_b_sclk_sel,
	&tdmout_c_sclk_sel,
	&tdmin_a_sclk_pre_en,
	&tdmin_b_sclk_pre_en,
	&tdmin_c_sclk_pre_en,
	&tdmin_lb_sclk_pre_en,
	&tdmout_a_sclk_pre_en,
	&tdmout_b_sclk_pre_en,
	&tdmout_c_sclk_pre_en,
	&tdmin_a_sclk_post_en,
	&tdmin_b_sclk_post_en,
	&tdmin_c_sclk_post_en,
	&tdmin_lb_sclk_post_en,
	&tdmout_a_sclk_post_en,
	&tdmout_b_sclk_post_en,
	&tdmout_c_sclk_post_en,
	&tdmin_a_sclk,
	&tdmin_b_sclk,
	&tdmin_c_sclk,
	&tdmin_lb_sclk,
	&axg_tdmout_a_sclk,
	&axg_tdmout_b_sclk,
	&axg_tdmout_c_sclk,
	&tdmin_a_lrclk,
	&tdmin_b_lrclk,
	&tdmin_c_lrclk,
	&tdmin_lb_lrclk,
	&tdmout_a_lrclk,
	&tdmout_b_lrclk,
	&tdmout_c_lrclk,
};

static struct clk_regmap *const g12a_clk_regmaps[] = {
	&ddr_arb,
	&pdm,
	&tdmin_a,
	&tdmin_b,
	&tdmin_c,
	&tdmin_lb,
	&tdmout_a,
	&tdmout_b,
	&tdmout_c,
	&frddr_a,
	&frddr_b,
	&frddr_c,
	&toddr_a,
	&toddr_b,
	&toddr_c,
	&loopback,
	&spdifin,
	&spdifout,
	&resample,
	&power_detect,
	&spdifout_b,
	&mst_a_mclk_sel,
	&mst_b_mclk_sel,
	&mst_c_mclk_sel,
	&mst_d_mclk_sel,
	&mst_e_mclk_sel,
	&mst_f_mclk_sel,
	&mst_a_mclk_div,
	&mst_b_mclk_div,
	&mst_c_mclk_div,
	&mst_d_mclk_div,
	&mst_e_mclk_div,
	&mst_f_mclk_div,
	&mst_a_mclk,
	&mst_b_mclk,
	&mst_c_mclk,
	&mst_d_mclk,
	&mst_e_mclk,
	&mst_f_mclk,
	&spdifout_clk_sel,
	&spdifout_clk_div,
	&spdifout_clk,
	&spdifin_clk_sel,
	&spdifin_clk_div,
	&spdifin_clk,
	&pdm_dclk_sel,
	&pdm_dclk_div,
	&pdm_dclk,
	&pdm_sysclk_sel,
	&pdm_sysclk_div,
	&pdm_sysclk,
	&mst_a_sclk_pre_en,
	&mst_b_sclk_pre_en,
	&mst_c_sclk_pre_en,
	&mst_d_sclk_pre_en,
	&mst_e_sclk_pre_en,
	&mst_f_sclk_pre_en,
	&mst_a_sclk_div,
	&mst_b_sclk_div,
	&mst_c_sclk_div,
	&mst_d_sclk_div,
	&mst_e_sclk_div,
	&mst_f_sclk_div,
	&mst_a_sclk_post_en,
	&mst_b_sclk_post_en,
	&mst_c_sclk_post_en,
	&mst_d_sclk_post_en,
	&mst_e_sclk_post_en,
	&mst_f_sclk_post_en,
	&mst_a_sclk,
	&mst_b_sclk,
	&mst_c_sclk,
	&mst_d_sclk,
	&mst_e_sclk,
	&mst_f_sclk,
	&mst_a_lrclk_div,
	&mst_b_lrclk_div,
	&mst_c_lrclk_div,
	&mst_d_lrclk_div,
	&mst_e_lrclk_div,
	&mst_f_lrclk_div,
	&mst_a_lrclk,
	&mst_b_lrclk,
	&mst_c_lrclk,
	&mst_d_lrclk,
	&mst_e_lrclk,
	&mst_f_lrclk,
	&tdmin_a_sclk_sel,
	&tdmin_b_sclk_sel,
	&tdmin_c_sclk_sel,
	&tdmin_lb_sclk_sel,
	&tdmout_a_sclk_sel,
	&tdmout_b_sclk_sel,
	&tdmout_c_sclk_sel,
	&tdmin_a_sclk_pre_en,
	&tdmin_b_sclk_pre_en,
	&tdmin_c_sclk_pre_en,
	&tdmin_lb_sclk_pre_en,
	&tdmout_a_sclk_pre_en,
	&tdmout_b_sclk_pre_en,
	&tdmout_c_sclk_pre_en,
	&tdmin_a_sclk_post_en,
	&tdmin_b_sclk_post_en,
	&tdmin_c_sclk_post_en,
	&tdmin_lb_sclk_post_en,
	&tdmout_a_sclk_post_en,
	&tdmout_b_sclk_post_en,
	&tdmout_c_sclk_post_en,
	&tdmin_a_sclk,
	&tdmin_b_sclk,
	&tdmin_c_sclk,
	&tdmin_lb_sclk,
	&g12a_tdmout_a_sclk,
	&g12a_tdmout_b_sclk,
	&g12a_tdmout_c_sclk,
	&tdmin_a_lrclk,
	&tdmin_b_lrclk,
	&tdmin_c_lrclk,
	&tdmin_lb_lrclk,
	&tdmout_a_lrclk,
	&tdmout_b_lrclk,
	&tdmout_c_lrclk,
	&spdifout_b_clk_sel,
	&spdifout_b_clk_div,
	&spdifout_b_clk,
	&g12a_tdm_mclk_pad_0,
	&g12a_tdm_mclk_pad_1,
	&g12a_tdm_lrclk_pad_0,
	&g12a_tdm_lrclk_pad_1,
	&g12a_tdm_lrclk_pad_2,
	&g12a_tdm_sclk_pad_0,
	&g12a_tdm_sclk_pad_1,
	&g12a_tdm_sclk_pad_2,
	&toram,
	&eqdrc,
};

static struct clk_regmap *const sm1_clk_regmaps[] = {
	&ddr_arb,
	&pdm,
	&tdmin_a,
	&tdmin_b,
	&tdmin_c,
	&tdmin_lb,
	&tdmout_a,
	&tdmout_b,
	&tdmout_c,
	&frddr_a,
	&frddr_b,
	&frddr_c,
	&toddr_a,
	&toddr_b,
	&toddr_c,
	&loopback,
	&spdifin,
	&spdifout,
	&resample,
	&spdifout_b,
	&sm1_mst_a_mclk_sel,
	&sm1_mst_b_mclk_sel,
	&sm1_mst_c_mclk_sel,
	&sm1_mst_d_mclk_sel,
	&sm1_mst_e_mclk_sel,
	&sm1_mst_f_mclk_sel,
	&sm1_mst_a_mclk_div,
	&sm1_mst_b_mclk_div,
	&sm1_mst_c_mclk_div,
	&sm1_mst_d_mclk_div,
	&sm1_mst_e_mclk_div,
	&sm1_mst_f_mclk_div,
	&sm1_mst_a_mclk,
	&sm1_mst_b_mclk,
	&sm1_mst_c_mclk,
	&sm1_mst_d_mclk,
	&sm1_mst_e_mclk,
	&sm1_mst_f_mclk,
	&spdifout_clk_sel,
	&spdifout_clk_div,
	&spdifout_clk,
	&spdifin_clk_sel,
	&spdifin_clk_div,
	&spdifin_clk,
	&pdm_dclk_sel,
	&pdm_dclk_div,
	&pdm_dclk,
	&pdm_sysclk_sel,
	&pdm_sysclk_div,
	&pdm_sysclk,
	&mst_a_sclk_pre_en,
	&mst_b_sclk_pre_en,
	&mst_c_sclk_pre_en,
	&mst_d_sclk_pre_en,
	&mst_e_sclk_pre_en,
	&mst_f_sclk_pre_en,
	&mst_a_sclk_div,
	&mst_b_sclk_div,
	&mst_c_sclk_div,
	&mst_d_sclk_div,
	&mst_e_sclk_div,
	&mst_f_sclk_div,
	&mst_a_sclk_post_en,
	&mst_b_sclk_post_en,
	&mst_c_sclk_post_en,
	&mst_d_sclk_post_en,
	&mst_e_sclk_post_en,
	&mst_f_sclk_post_en,
	&mst_a_sclk,
	&mst_b_sclk,
	&mst_c_sclk,
	&mst_d_sclk,
	&mst_e_sclk,
	&mst_f_sclk,
	&mst_a_lrclk_div,
	&mst_b_lrclk_div,
	&mst_c_lrclk_div,
	&mst_d_lrclk_div,
	&mst_e_lrclk_div,
	&mst_f_lrclk_div,
	&mst_a_lrclk,
	&mst_b_lrclk,
	&mst_c_lrclk,
	&mst_d_lrclk,
	&mst_e_lrclk,
	&mst_f_lrclk,
	&tdmin_a_sclk_sel,
	&tdmin_b_sclk_sel,
	&tdmin_c_sclk_sel,
	&tdmin_lb_sclk_sel,
	&tdmout_a_sclk_sel,
	&tdmout_b_sclk_sel,
	&tdmout_c_sclk_sel,
	&tdmin_a_sclk_pre_en,
	&tdmin_b_sclk_pre_en,
	&tdmin_c_sclk_pre_en,
	&tdmin_lb_sclk_pre_en,
	&tdmout_a_sclk_pre_en,
	&tdmout_b_sclk_pre_en,
	&tdmout_c_sclk_pre_en,
	&tdmin_a_sclk_post_en,
	&tdmin_b_sclk_post_en,
	&tdmin_c_sclk_post_en,
	&tdmin_lb_sclk_post_en,
	&tdmout_a_sclk_post_en,
	&tdmout_b_sclk_post_en,
	&tdmout_c_sclk_post_en,
	&tdmin_a_sclk,
	&tdmin_b_sclk,
	&tdmin_c_sclk,
	&tdmin_lb_sclk,
	&g12a_tdmout_a_sclk,
	&g12a_tdmout_b_sclk,
	&g12a_tdmout_c_sclk,
	&tdmin_a_lrclk,
	&tdmin_b_lrclk,
	&tdmin_c_lrclk,
	&tdmin_lb_lrclk,
	&tdmout_a_lrclk,
	&tdmout_b_lrclk,
	&tdmout_c_lrclk,
	&spdifout_b_clk_sel,
	&spdifout_b_clk_div,
	&spdifout_b_clk,
	&sm1_tdm_mclk_pad_0,
	&sm1_tdm_mclk_pad_1,
	&sm1_tdm_lrclk_pad_0,
	&sm1_tdm_lrclk_pad_1,
	&sm1_tdm_lrclk_pad_2,
	&sm1_tdm_sclk_pad_0,
	&sm1_tdm_sclk_pad_1,
	&sm1_tdm_sclk_pad_2,
	&sm1_aud_top,
	&toram,
	&eqdrc,
	&resample_b,
	&tovad,
	&locker,
	&spdifin_lb,
	&frddr_d,
	&toddr_d,
	&loopback_b,
	&sm1_clk81_en,
	&sm1_sysclk_a_div,
	&sm1_sysclk_a_en,
	&sm1_sysclk_b_div,
	&sm1_sysclk_b_en,
};

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
	.max_register	= AUDIO_CLK_SPDIFOUT_B_CTRL,
};

struct audioclk_data {
	struct clk_regmap *const *regmap_clks;
	unsigned int regmap_clk_num;
	struct meson_clk_hw_data hw_clks;
	unsigned int reset_offset;
	unsigned int reset_num;
};

static int axg_audio_clkc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct audioclk_data *data;
	struct axg_audio_reset_data *rst;
	struct regmap *map;
	void __iomem *regs;
	struct clk_hw *hw;
	struct clk *clk;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &axg_audio_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n", PTR_ERR(map));
		return PTR_ERR(map);
	}

	/* Get the mandatory peripheral clock */
	clk = devm_clk_get_enabled(dev, "pclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = device_reset(dev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to reset device\n");
		return ret;
	}

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < data->regmap_clk_num; i++)
		data->regmap_clks[i]->map = map;

	/* Take care to skip the registered input clocks */
	for (i = AUD_CLKID_DDR_ARB; i < data->hw_clks.num; i++) {
		const char *name;

		hw = data->hw_clks.hws[i];
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

	ret = devm_of_clk_add_hw_provider(dev, meson_clk_hw_get, (void *)&data->hw_clks);
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
	.regmap_clks = axg_clk_regmaps,
	.regmap_clk_num = ARRAY_SIZE(axg_clk_regmaps),
	.hw_clks = {
		.hws = axg_audio_hw_clks,
		.num = ARRAY_SIZE(axg_audio_hw_clks),
	},
};

static const struct audioclk_data g12a_audioclk_data = {
	.regmap_clks = g12a_clk_regmaps,
	.regmap_clk_num = ARRAY_SIZE(g12a_clk_regmaps),
	.hw_clks = {
		.hws = g12a_audio_hw_clks,
		.num = ARRAY_SIZE(g12a_audio_hw_clks),
	},
	.reset_offset = AUDIO_SW_RESET,
	.reset_num = 26,
};

static const struct audioclk_data sm1_audioclk_data = {
	.regmap_clks = sm1_clk_regmaps,
	.regmap_clk_num = ARRAY_SIZE(sm1_clk_regmaps),
	.hw_clks = {
		.hws = sm1_audio_hw_clks,
		.num = ARRAY_SIZE(sm1_audio_hw_clks),
	},
	.reset_offset = AUDIO_SM1_SW_RESET0,
	.reset_num = 39,
};

static const struct of_device_id clkc_match_table[] = {
	{
		.compatible = "amlogic,axg-audio-clkc",
		.data = &axg_audioclk_data
	}, {
		.compatible = "amlogic,g12a-audio-clkc",
		.data = &g12a_audioclk_data
	}, {
		.compatible = "amlogic,sm1-audio-clkc",
		.data = &sm1_audioclk_data
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

MODULE_DESCRIPTION("Amlogic AXG/G12A/SM1 Audio Clock driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
