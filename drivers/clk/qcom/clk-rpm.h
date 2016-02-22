/*
 * Copyright (c) 2015, Linaro Limited
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

#ifndef __QCOM_CLK_RPM_H__
#define __QCOM_CLK_RPM_H__

#include <linux/clk-provider.h>

struct qcom_rpm;

struct clk_rpm {
	const int rpm_clk_id;
	unsigned long rate;
	bool enabled;
	bool branch;
	struct clk_hw hw;
	struct qcom_rpm *rpm;
};

extern const struct clk_ops clk_rpm_ops;
extern const struct clk_ops clk_rpm_branch_ops;

#define DEFINE_CLK_RPM(_platform, _name, r_id)				     \
	static struct clk_rpm _platform##_##_name = {			     \
		.rpm_clk_id = (r_id),					     \
		.rate = INT_MAX,					     \
		.hw.init = &(struct clk_init_data){			     \
			.name = #_name,					     \
			.parent_names = (const char *[]){ "pxo_board" },     \
			.num_parents = 1,				     \
			.ops = &clk_rpm_ops,				     \
		},							     \
	}

#define DEFINE_CLK_RPM_PXO_BRANCH(_platform, _name, r_id, r)		     \
	static struct clk_rpm _platform##_##_name = {			     \
		.rpm_clk_id = (r_id),					     \
		.branch = true,						     \
		.rate = (r),						     \
		.hw.init = &(struct clk_init_data){			     \
			.name = #_name,					     \
			.parent_names = (const char *[]){ "pxo_board" },     \
			.num_parents = 1,				     \
			.ops = &clk_rpm_branch_ops,			     \
		},							     \
	}

#define DEFINE_CLK_RPM_CXO_BRANCH(_platform, _name, r_id, r)		     \
	static struct clk_rpm _platform##_##_name = {			     \
		.rpm_clk_id = (r_id),					     \
		.branch = true,						     \
		.rate = (r),						     \
		.hw.init = &(struct clk_init_data){			     \
			.name = #_name,					     \
			.parent_names = (const char *[]){ "cxo_board" },     \
			.num_parents = 1,				     \
			.ops = &clk_rpm_branch_ops,			     \
		},							     \
	}
#endif
