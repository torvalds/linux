// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019, The Linux Foundation. All rights reserved. */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/regulator/consumer.h>

#include "vdd-class.h"

/*
 * Aggregate the vdd_class level votes and call regulator framework functions
 * to enforce the highest vote.
 */
static int clk_aggregate_vdd(struct clk_vdd_class *vdd_class)
{
	struct regulator **r = vdd_class->regulator;
	int level, ret, i, ignore;
	int *uv = vdd_class->vdd_uv;
	int n_reg = vdd_class->num_regulators;
	int cur_lvl = vdd_class->cur_level;
	int max_lvl = vdd_class->num_levels - 1;
	int cur_base = cur_lvl * n_reg;
	int new_base;

	/* aggregate votes */
	for (level = max_lvl; level > 0; level--)
		if (vdd_class->level_votes[level])
			break;

	if (level == cur_lvl)
		return 0;

	new_base = level * n_reg;

	for (i = 0; i < vdd_class->num_regulators; i++) {
		pr_debug("Set voltage level %d\n", uv[new_base + i]);
		ret = regulator_set_voltage(r[i], uv[new_base + i], INT_MAX);
		if (ret)
			goto set_voltage_fail;

		if (cur_lvl == 0 || cur_lvl == vdd_class->num_levels)
			ret = regulator_enable(r[i]);
		else if (level == 0)
			ret = regulator_disable(r[i]);
		if (ret)
			goto enable_disable_fail;
	}

	vdd_class->cur_level = level;

	return 0;

enable_disable_fail:
	regulator_set_voltage(r[i], uv[cur_base + i], INT_MAX);

set_voltage_fail:
	for (i--; i >= 0; i--) {
		regulator_set_voltage(r[i], uv[cur_base + i], INT_MAX);
		if (cur_lvl == 0 || cur_lvl == vdd_class->num_levels)
			regulator_disable(r[i]);
		else if (level == 0)
			ignore = regulator_enable(r[i]);
	}

	return ret;
}

/**
 * clk_vote_vdd_level - Add a vote for a given voltage level
 * @vdd_data:	vdd_class data for the clock
 * @level:	voltage level to add a vote for
 *
 * Add a regulator framework voltage request for the vdd_class regulator(s)
 * by decrementing the reference count for a given voltage level. This ensures
 * that the supply regulator is allowed to potentially lower its voltage after
 * re-aggregating overall voltage requests.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_vote_vdd_level(struct clk_vdd_class_data *vdd_data, int level)
{
	struct clk_vdd_class *vdd_class = vdd_data->vdd_class;
	int ret = 0;

	if (level >= vdd_class->num_levels)
		return -EINVAL;

	vdd_class->level_votes[level]++;

	ret = clk_aggregate_vdd(vdd_class);
	if (ret)
		vdd_class->level_votes[level]--;

	return ret;
}
EXPORT_SYMBOL(clk_vote_vdd_level);

/**
 * clk_unvote_vdd_level - Remove a vote for a given voltage level
 * @vdd_data:	vdd_class data for the clock
 * @level:	voltage level to remove a vote for
 *
 * Removes a regulator framework voltage request for the vdd_class regulator(s)
 * by incrementing the reference count for a given voltage level. This ensures
 * that the supply regulator is outputting at least at the requested level.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_unvote_vdd_level(struct clk_vdd_class_data *vdd_data, int level)
{
	struct clk_vdd_class *vdd_class = vdd_data->vdd_class;
	int ret = 0;

	if (level >= vdd_class->num_levels)
		return -EINVAL;

	if (WARN(!vdd_class->level_votes[level],
			"Reference counts are incorrect for %s level %d\n",
			vdd_class->class_name, level)) {
		return -EINVAL;
	}

	vdd_class->level_votes[level]--;

	ret = clk_aggregate_vdd(vdd_class);
	if (ret)
		vdd_class->level_votes[level]++;

	return ret;
}
EXPORT_SYMBOL(clk_unvote_vdd_level);

/**
 * clk_find_vdd_level - Find the voltage level required for a given clock rate.
 * @hw:		clk_hw pointer of clock being voted on
 * @vdd_data:	vdd_class data for the clock
 * @rate:	Clock rate in Hz to vote for
 *
 * Finds the matching required voltage level for a rate from a list
 * of well characterized frequency to voltage points for a given clock.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_find_vdd_level(struct clk_hw *hw,
				struct clk_vdd_class_data *vdd_data,
				unsigned long rate)
{
	int level;

	/*
	 * For certain PLLs, due to the limitation in the bits allocated for
	 * programming the fractional divider, the actual rate of the PLL will
	 * be slightly higher than the requested rate (in the order of several
	 * Hz). To accommodate this difference, convert the FMAX rate and the
	 * clock frequency to KHz and use that for deriving the voltage level.
	 */
	for (level = 0; level < vdd_data->num_rate_max; level++)
		if (DIV_ROUND_CLOSEST(rate, 1000) <=
		    DIV_ROUND_CLOSEST(vdd_data->rate_max[level], 1000) &&
		     vdd_data->rate_max[level] > 0)
			break;

	if (level == vdd_data->num_rate_max) {
		pr_err("Rate %lu for %s is greater than highest Fmax\n",
			rate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	return level;
}
EXPORT_SYMBOL(clk_find_vdd_level);
