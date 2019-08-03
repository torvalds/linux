/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_VDD_CLASS_H__
#define __QCOM_CLK_VDD_CLASS_H__

#include <linux/regulator/consumer.h>

/**
 * struct clk_vdd_class - Voltage scaling class
 *
 * @class_name:		name of the class
 * @regulator:		array of regulators
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
 * @rate_max:		array of maximum clock rate in Hz supported at each
 *			voltage level, indexed by voltage level
 * @num_rate_max:	size of rate_max array
 * @vdd_rate:		cached rate of current vdd vote
 */
struct clk_vdd_class_data {
	struct clk_vdd_class	*vdd_class;
	unsigned long		*rate_max;
	int			num_rate_max;
	int			vdd_level;
};

#define DEFINE_VDD_REGULATORS(_name, _num_levels, _num_regulators, _vdd_uv) \
	struct clk_vdd_class _name = { \
		.class_name = #_name, \
		.vdd_uv = _vdd_uv, \
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

#endif
