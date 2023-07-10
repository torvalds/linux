/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_VDD_CLASS_H__
#define __QCOM_CLK_VDD_CLASS_H__

#include <linux/regulator/consumer.h>
#include <linux/device.h>
#include "common.h"
/**
 * struct clk_vdd_class - Voltage scaling class
 *
 * @class_name:		name of the class
 * @regulator:		array of regulators
 * @regulator_names:	regulator names
 * @num_regulators:	size of regulator array
 * @vdd_uv:		sorted 2D array of legal voltage settings. Indexed by
 *			level, then regulator
 * @level_votes:	array of votes for each level
 * @num_levels:		size of level_votes array
 * @cur_level:		the currently set voltage level
 */
struct clk_vdd_class {
	const char		*class_name;
	struct regulator	**regulator;
	const char		**regulator_names;
	int			num_regulators;
	int			*vdd_uv;
	int			*level_votes;
	int			num_levels;
	int			cur_level;
};

/**
 * struct clk_vdd_class_data - per-clock vdd_class rate data
 *
 * @vdd_class:		voltage scaling requirement class
 * @vdd_classes:	array of voltage scaling requirement class
 * @rate_max:		array of maximum clock rate in Hz supported at each
 *			voltage level, indexed by voltage level
 * @num_rate_max:	size of rate_max array
 * @vdd_rate:		cached rate of current vdd vote
 * @num_vdd_classes:	size of vdd_classes array
 */
struct clk_vdd_class_data {
	struct clk_vdd_class	*vdd_class;
	struct clk_vdd_class	**vdd_classes;
	unsigned long		*rate_max;
	int			num_rate_max;
	int			vdd_level;
	int			num_vdd_classes;
};

#define DEFINE_VDD_REGULATORS(_name, _num_levels, _num_regulators, _vdd_uv) \
	struct clk_vdd_class _name = { \
		.class_name = #_name, \
		.vdd_uv = _vdd_uv, \
		.regulator_names = (const char * [_num_regulators]) { #_name}, \
		.regulator = (struct regulator * [_num_regulators]) {}, \
		.num_regulators = _num_regulators, \
		.level_votes = (int [_num_levels]) {}, \
		.num_levels = _num_levels, \
		.cur_level = _num_levels, \
	}

#define DEFINE_VDD_REGS_INIT(_name, _num_regulators) \
	struct clk_vdd_class _name = { \
		.class_name = #_name, \
		.regulator = (struct regulator * [_num_regulators]) {}, \
		.num_regulators = _num_regulators, \
	}

int clk_find_vdd_level(struct clk_hw *hw, struct clk_vdd_class_data *vdd_data,
				unsigned long rate);
int clk_vote_vdd_level(struct clk_vdd_class_data *vdd_class, int level);
int clk_unvote_vdd_level(struct clk_vdd_class_data *vdd_class, int level);
int clk_get_vdd_voltage(struct clk_vdd_class_data *vdd_data, int vdd_level);
int clk_regulator_init(struct device *dev, const struct qcom_cc_desc *desc);
void clk_regulator_deinit(const struct qcom_cc_desc *desc);
int clk_vdd_proxy_vote(struct device *dev, const struct qcom_cc_desc *desc);
int clk_vdd_proxy_unvote(struct device *dev, const struct qcom_cc_desc *desc);
#endif
