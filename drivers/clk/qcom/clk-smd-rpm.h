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

#ifndef __QCOM_CLK_SMD_RPM_H__
#define __QCOM_CLK_SMD_RPM_H__

#include <linux/clk-provider.h>

#define QCOM_RPM_KEY_SOFTWARE_ENABLE			0x6e657773
#define QCOM_RPM_KEY_PIN_CTRL_CLK_BUFFER_ENABLE_KEY	0x62636370
#define QCOM_RPM_SMD_KEY_RATE				0x007a484b
#define QCOM_RPM_SMD_KEY_ENABLE				0x62616e45
#define QCOM_RPM_SMD_KEY_STATE				0x54415453
#define QCOM_RPM_SCALING_ENABLE_ID			0x2

struct qcom_smd_rpm;

struct clk_smd_rpm {
	const int rpm_res_type;
	const int rpm_key;
	const int rpm_clk_id;
	const int rpm_status_id;
	const bool active_only;
	bool enabled;
	bool branch;
	struct clk_smd_rpm *peer;
	struct clk_hw hw;
	unsigned long rate;
	struct qcom_smd_rpm *rpm;
};

struct clk_smd_rpm_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

extern const struct clk_ops clk_smd_rpm_ops;
extern const struct clk_ops clk_smd_rpm_branch_ops;

#define __DEFINE_CLK_SMD_RPM(_platform, _name, _active, type, r_id, stat_id,  \
			     key)					      \
	static struct clk_smd_rpm _platform##_##_active;		      \
	static struct clk_smd_rpm _platform##_##_name = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_active,				      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_ops,			      \
			.name = #_name,					      \
			.parent_names = (const char *[]){ "xo_board" },       \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm _platform##_##_active = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_name,				      \
		.active_only = true,					      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_ops,			      \
			.name = #_active,				      \
			.parent_names = (const char *[]){ "xo_board" },	      \
			.num_parents = 1,				      \
		},							      \
	}

#define __DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active, type, r_id,    \
				    stat_id, r, key)			      \
	static struct clk_smd_rpm _platform##_##_active;		      \
	static struct clk_smd_rpm _platform##_##_name = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_active,				      \
		.branch = true,						      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_name,					      \
			.parent_names = (const char *[]){ "xo_board" },	      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm _platform##_##_active = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_name,				      \
		.active_only = true,					      \
		.branch = true,						      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_active,				      \
			.parent_names = (const char *[]){ "xo_board" },	      \
			.num_parents = 1,				      \
		},							      \
	}

#define DEFINE_CLK_SMD_RPM(_platform, _name, _active, type, r_id)	      \
		__DEFINE_CLK_SMD_RPM(_platform, _name, _active, type, r_id,   \
		0, QCOM_RPM_SMD_KEY_RATE)

#define DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active, type, r_id, r)   \
		__DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active, type,  \
		r_id, 0, r, QCOM_RPM_SMD_KEY_ENABLE)

#define DEFINE_CLK_SMD_RPM_QDSS(_platform, _name, _active, type, r_id)	      \
		__DEFINE_CLK_SMD_RPM(_platform, _name, _active, type, r_id,   \
		0, QCOM_RPM_SMD_KEY_STATE)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER(_platform, _name, _active, r_id)	      \
		__DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active,	      \
		QCOM_SMD_RPM_CLK_BUF_A, r_id, 0, 1000,			      \
		QCOM_RPM_KEY_SOFTWARE_ENABLE)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(_platform, _name, _active, r_id) \
		__DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active,	      \
		QCOM_SMD_RPM_CLK_BUF_A, r_id, 0, 1000,			      \
		QCOM_RPM_KEY_PIN_CTRL_CLK_BUFFER_ENABLE_KEY)

#endif
