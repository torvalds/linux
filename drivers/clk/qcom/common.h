/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __QCOM_CLK_COMMON_H__
#define __QCOM_CLK_COMMON_H__

struct platform_device;
struct regmap_config;
struct clk_regmap;
struct qcom_reset_map;

struct qcom_cc_desc {
	const struct regmap_config *config;
	struct clk_regmap **clks;
	size_t num_clks;
	const struct qcom_reset_map *resets;
	size_t num_resets;
};

extern int qcom_cc_probe(struct platform_device *pdev,
			 const struct qcom_cc_desc *desc);

extern void qcom_cc_remove(struct platform_device *pdev);

#endif
