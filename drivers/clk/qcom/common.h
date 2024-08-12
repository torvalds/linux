/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2014, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_COMMON_H__
#define __QCOM_CLK_COMMON_H__

struct platform_device;
struct regmap_config;
struct clk_regmap;
struct qcom_reset_map;
struct regmap;
struct freq_tbl;
struct clk_hw;

#define PLL_LOCK_COUNT_SHIFT	8
#define PLL_LOCK_COUNT_MASK	0x3f
#define PLL_BIAS_COUNT_SHIFT	14
#define PLL_BIAS_COUNT_MASK	0x3f
#define PLL_VOTE_FSM_ENA	BIT(20)
#define PLL_VOTE_FSM_RESET	BIT(21)

struct qcom_icc_hws_data {
	int master_id;
	int slave_id;
	int clk_id;
};

struct qcom_cc_desc {
	const struct regmap_config *config;
	struct clk_regmap **clks;
	size_t num_clks;
	const struct qcom_reset_map *resets;
	size_t num_resets;
	struct gdsc **gdscs;
	size_t num_gdscs;
	struct clk_hw **clk_hws;
	size_t num_clk_hws;
	struct qcom_icc_hws_data *icc_hws;
	size_t num_icc_hws;
	unsigned int icc_first_node_id;
};

/**
 * struct parent_map - map table for source select configuration values
 * @src: source
 * @cfg: configuration value
 */
struct parent_map {
	u8 src;
	u8 cfg;
};

extern const struct freq_tbl *qcom_find_freq(const struct freq_tbl *f,
					     unsigned long rate);
extern const struct freq_tbl *qcom_find_freq_floor(const struct freq_tbl *f,
						   unsigned long rate);
extern const struct freq_multi_tbl *qcom_find_freq_multi(const struct freq_multi_tbl *f,
							 unsigned long rate);
extern void
qcom_pll_set_fsm_mode(struct regmap *m, u32 reg, u8 bias_count, u8 lock_count);
extern int qcom_find_src_index(struct clk_hw *hw, const struct parent_map *map,
			       u8 src);
extern int qcom_find_cfg_index(struct clk_hw *hw, const struct parent_map *map,
			       u8 cfg);

extern int qcom_cc_register_board_clk(struct device *dev, const char *path,
				      const char *name, unsigned long rate);
extern int qcom_cc_register_sleep_clk(struct device *dev);

extern struct regmap *qcom_cc_map(struct platform_device *pdev,
				  const struct qcom_cc_desc *desc);
extern int qcom_cc_really_probe(struct device *dev,
				const struct qcom_cc_desc *desc,
				struct regmap *regmap);
extern int qcom_cc_probe(struct platform_device *pdev,
			 const struct qcom_cc_desc *desc);
extern int qcom_cc_probe_by_index(struct platform_device *pdev, int index,
				  const struct qcom_cc_desc *desc);

#endif
