/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015, 2018, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */


#ifndef __QCOM_CLK_ALPHA_PLL_H__
#define __QCOM_CLK_ALPHA_PLL_H__

#include <linux/clk-provider.h>
#include "clk-regmap.h"

/* Alpha PLL types */
enum {
	CLK_ALPHA_PLL_TYPE_DEFAULT,
	CLK_ALPHA_PLL_TYPE_HUAYRA,
	CLK_ALPHA_PLL_TYPE_BRAMMO,
	CLK_ALPHA_PLL_TYPE_FABIA,
	CLK_ALPHA_PLL_TYPE_TRION,
	CLK_ALPHA_PLL_TYPE_LUCID = CLK_ALPHA_PLL_TYPE_TRION,
	CLK_ALPHA_PLL_TYPE_AGERA,
	CLK_ALPHA_PLL_TYPE_ZONDA,
	CLK_ALPHA_PLL_TYPE_LUCID_EVO,
	CLK_ALPHA_PLL_TYPE_LUCID_OLE,
	CLK_ALPHA_PLL_TYPE_RIVIAN_EVO,
	CLK_ALPHA_PLL_TYPE_DEFAULT_EVO,
	CLK_ALPHA_PLL_TYPE_BRAMMO_EVO,
	CLK_ALPHA_PLL_TYPE_STROMER,
	CLK_ALPHA_PLL_TYPE_STROMER_PLUS,
	CLK_ALPHA_PLL_TYPE_MAX,
};

enum {
	PLL_OFF_L_VAL,
	PLL_OFF_CAL_L_VAL,
	PLL_OFF_ALPHA_VAL,
	PLL_OFF_ALPHA_VAL_U,
	PLL_OFF_USER_CTL,
	PLL_OFF_USER_CTL_U,
	PLL_OFF_USER_CTL_U1,
	PLL_OFF_CONFIG_CTL,
	PLL_OFF_CONFIG_CTL_U,
	PLL_OFF_CONFIG_CTL_U1,
	PLL_OFF_TEST_CTL,
	PLL_OFF_TEST_CTL_U,
	PLL_OFF_TEST_CTL_U1,
	PLL_OFF_TEST_CTL_U2,
	PLL_OFF_STATE,
	PLL_OFF_STATUS,
	PLL_OFF_OPMODE,
	PLL_OFF_FRAC,
	PLL_OFF_CAL_VAL,
	PLL_OFF_MAX_REGS
};

extern const u8 clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_MAX][PLL_OFF_MAX_REGS];

struct pll_vco {
	unsigned long min_freq;
	unsigned long max_freq;
	u32 val;
};

#define VCO(a, b, c) { \
	.val = a,\
	.min_freq = b,\
	.max_freq = c,\
}

/**
 * struct clk_alpha_pll - phase locked loop (PLL)
 * @offset: base address of registers
 * @vco_table: array of VCO settings
 * @regs: alpha pll register map (see @clk_alpha_pll_regs)
 * @clkr: regmap clock handle
 */
struct clk_alpha_pll {
	u32 offset;
	const u8 *regs;

	const struct pll_vco *vco_table;
	size_t num_vco;
#define SUPPORTS_OFFLINE_REQ		BIT(0)
#define SUPPORTS_FSM_MODE		BIT(2)
#define SUPPORTS_DYNAMIC_UPDATE	BIT(3)
#define SUPPORTS_FSM_LEGACY_MODE	BIT(4)
	u8 flags;

	struct clk_regmap clkr;
};

/**
 * struct clk_alpha_pll_postdiv - phase locked loop (PLL) post-divider
 * @offset: base address of registers
 * @regs: alpha pll register map (see @clk_alpha_pll_regs)
 * @width: width of post-divider
 * @post_div_shift: shift to differentiate between odd & even post-divider
 * @post_div_table: table with PLL odd and even post-divider settings
 * @num_post_div: Number of PLL post-divider settings
 *
 * @clkr: regmap clock handle
 */
struct clk_alpha_pll_postdiv {
	u32 offset;
	u8 width;
	const u8 *regs;

	struct clk_regmap clkr;
	int post_div_shift;
	const struct clk_div_table *post_div_table;
	size_t num_post_div;
};

struct alpha_pll_config {
	u32 l;
	u32 alpha;
	u32 alpha_hi;
	u32 config_ctl_val;
	u32 config_ctl_hi_val;
	u32 config_ctl_hi1_val;
	u32 user_ctl_val;
	u32 user_ctl_hi_val;
	u32 user_ctl_hi1_val;
	u32 test_ctl_val;
	u32 test_ctl_mask;
	u32 test_ctl_hi_val;
	u32 test_ctl_hi_mask;
	u32 test_ctl_hi1_val;
	u32 test_ctl_hi2_val;
	u32 main_output_mask;
	u32 aux_output_mask;
	u32 aux2_output_mask;
	u32 early_output_mask;
	u32 alpha_en_mask;
	u32 alpha_mode_mask;
	u32 pre_div_val;
	u32 pre_div_mask;
	u32 post_div_val;
	u32 post_div_mask;
	u32 vco_val;
	u32 vco_mask;
	u32 status_val;
	u32 status_mask;
	u32 lock_det;
};

extern const struct clk_ops clk_alpha_pll_ops;
extern const struct clk_ops clk_alpha_pll_fixed_ops;
extern const struct clk_ops clk_alpha_pll_hwfsm_ops;
extern const struct clk_ops clk_alpha_pll_postdiv_ops;
extern const struct clk_ops clk_alpha_pll_huayra_ops;
extern const struct clk_ops clk_alpha_pll_postdiv_ro_ops;
extern const struct clk_ops clk_alpha_pll_stromer_ops;

extern const struct clk_ops clk_alpha_pll_fabia_ops;
extern const struct clk_ops clk_alpha_pll_fixed_fabia_ops;
extern const struct clk_ops clk_alpha_pll_postdiv_fabia_ops;

extern const struct clk_ops clk_alpha_pll_trion_ops;
extern const struct clk_ops clk_alpha_pll_fixed_trion_ops;
extern const struct clk_ops clk_alpha_pll_postdiv_trion_ops;

extern const struct clk_ops clk_alpha_pll_lucid_ops;
#define clk_alpha_pll_fixed_lucid_ops clk_alpha_pll_fixed_trion_ops
extern const struct clk_ops clk_alpha_pll_postdiv_lucid_ops;
extern const struct clk_ops clk_alpha_pll_agera_ops;

extern const struct clk_ops clk_alpha_pll_lucid_5lpe_ops;
extern const struct clk_ops clk_alpha_pll_fixed_lucid_5lpe_ops;
extern const struct clk_ops clk_alpha_pll_postdiv_lucid_5lpe_ops;

extern const struct clk_ops clk_alpha_pll_zonda_ops;
#define clk_alpha_pll_postdiv_zonda_ops clk_alpha_pll_postdiv_fabia_ops

extern const struct clk_ops clk_alpha_pll_lucid_evo_ops;
extern const struct clk_ops clk_alpha_pll_reset_lucid_evo_ops;
#define clk_alpha_pll_reset_lucid_ole_ops clk_alpha_pll_reset_lucid_evo_ops
extern const struct clk_ops clk_alpha_pll_fixed_lucid_evo_ops;
#define clk_alpha_pll_fixed_lucid_ole_ops clk_alpha_pll_fixed_lucid_evo_ops
extern const struct clk_ops clk_alpha_pll_postdiv_lucid_evo_ops;
#define clk_alpha_pll_postdiv_lucid_ole_ops clk_alpha_pll_postdiv_lucid_evo_ops

extern const struct clk_ops clk_alpha_pll_rivian_evo_ops;
#define clk_alpha_pll_postdiv_rivian_evo_ops clk_alpha_pll_postdiv_fabia_ops

void clk_alpha_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config);
void clk_fabia_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
				const struct alpha_pll_config *config);
void clk_trion_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config);
void clk_agera_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
				const struct alpha_pll_config *config);
#define clk_lucid_pll_configure(pll, regmap, config) \
	clk_trion_pll_configure(pll, regmap, config)

void clk_zonda_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			     const struct alpha_pll_config *config);
void clk_lucid_evo_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
				 const struct alpha_pll_config *config);
void clk_rivian_evo_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
				  const struct alpha_pll_config *config);
void clk_stromer_pll_configure(struct clk_alpha_pll *pll, struct regmap *regmap,
			       const struct alpha_pll_config *config);

#endif
