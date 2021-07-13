// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Linaro Limited
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smd-rpm.h>

#include <dt-bindings/clock/qcom,rpmcc.h>
#include <dt-bindings/mfd/qcom-rpm.h>

#define QCOM_RPM_KEY_SOFTWARE_ENABLE			0x6e657773
#define QCOM_RPM_KEY_PIN_CTRL_CLK_BUFFER_ENABLE_KEY	0x62636370
#define QCOM_RPM_SMD_KEY_RATE				0x007a484b
#define QCOM_RPM_SMD_KEY_ENABLE				0x62616e45
#define QCOM_RPM_SMD_KEY_STATE				0x54415453
#define QCOM_RPM_SCALING_ENABLE_ID			0x2

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
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm _platform##_##_active = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.active_only = true,					      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_name,				      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_ops,			      \
			.name = #_active,				      \
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
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
		.branch = true,						      \
		.peer = &_platform##_##_active,				      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_name,					      \
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm _platform##_##_active = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.active_only = true,					      \
		.rpm_key = (key),					      \
		.branch = true,						      \
		.peer = &_platform##_##_name,				      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_active,				      \
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
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

#define to_clk_smd_rpm(_hw) container_of(_hw, struct clk_smd_rpm, hw)

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

struct rpm_cc {
	struct qcom_rpm *rpm;
	struct clk_smd_rpm **clks;
	size_t num_clks;
};

struct rpm_smd_clk_desc {
	struct clk_smd_rpm **clks;
	size_t num_clks;
};

static DEFINE_MUTEX(rpm_smd_clk_lock);

static int clk_smd_rpm_handoff(struct clk_smd_rpm *r)
{
	int ret;
	struct clk_smd_rpm_req req = {
		.key = cpu_to_le32(r->rpm_key),
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(r->branch ? 1 : INT_MAX),
	};

	ret = qcom_rpm_smd_write(r->rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				 r->rpm_res_type, r->rpm_clk_id, &req,
				 sizeof(req));
	if (ret)
		return ret;
	ret = qcom_rpm_smd_write(r->rpm, QCOM_SMD_RPM_SLEEP_STATE,
				 r->rpm_res_type, r->rpm_clk_id, &req,
				 sizeof(req));
	if (ret)
		return ret;

	return 0;
}

static int clk_smd_rpm_set_rate_active(struct clk_smd_rpm *r,
				       unsigned long rate)
{
	struct clk_smd_rpm_req req = {
		.key = cpu_to_le32(r->rpm_key),
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(DIV_ROUND_UP(rate, 1000)), /* to kHz */
	};

	return qcom_rpm_smd_write(r->rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				  r->rpm_res_type, r->rpm_clk_id, &req,
				  sizeof(req));
}

static int clk_smd_rpm_set_rate_sleep(struct clk_smd_rpm *r,
				      unsigned long rate)
{
	struct clk_smd_rpm_req req = {
		.key = cpu_to_le32(r->rpm_key),
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(DIV_ROUND_UP(rate, 1000)), /* to kHz */
	};

	return qcom_rpm_smd_write(r->rpm, QCOM_SMD_RPM_SLEEP_STATE,
				  r->rpm_res_type, r->rpm_clk_id, &req,
				  sizeof(req));
}

static void to_active_sleep(struct clk_smd_rpm *r, unsigned long rate,
			    unsigned long *active, unsigned long *sleep)
{
	*active = rate;

	/*
	 * Active-only clocks don't care what the rate is during sleep. So,
	 * they vote for zero.
	 */
	if (r->active_only)
		*sleep = 0;
	else
		*sleep = *active;
}

static int clk_smd_rpm_prepare(struct clk_hw *hw)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	struct clk_smd_rpm *peer = r->peer;
	unsigned long this_rate = 0, this_sleep_rate = 0;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	unsigned long active_rate, sleep_rate;
	int ret = 0;

	mutex_lock(&rpm_smd_clk_lock);

	/* Don't send requests to the RPM if the rate has not been set. */
	if (!r->rate)
		goto out;

	to_active_sleep(r, r->rate, &this_rate, &this_sleep_rate);

	/* Take peer clock's rate into account only if it's enabled. */
	if (peer->enabled)
		to_active_sleep(peer, peer->rate,
				&peer_rate, &peer_sleep_rate);

	active_rate = max(this_rate, peer_rate);

	if (r->branch)
		active_rate = !!active_rate;

	ret = clk_smd_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = max(this_sleep_rate, peer_sleep_rate);
	if (r->branch)
		sleep_rate = !!sleep_rate;

	ret = clk_smd_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		/* Undo the active set vote and restore it */
		ret = clk_smd_rpm_set_rate_active(r, peer_rate);

out:
	if (!ret)
		r->enabled = true;

	mutex_unlock(&rpm_smd_clk_lock);

	return ret;
}

static void clk_smd_rpm_unprepare(struct clk_hw *hw)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	struct clk_smd_rpm *peer = r->peer;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	unsigned long active_rate, sleep_rate;
	int ret;

	mutex_lock(&rpm_smd_clk_lock);

	if (!r->rate)
		goto out;

	/* Take peer clock's rate into account only if it's enabled. */
	if (peer->enabled)
		to_active_sleep(peer, peer->rate, &peer_rate,
				&peer_sleep_rate);

	active_rate = r->branch ? !!peer_rate : peer_rate;
	ret = clk_smd_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = r->branch ? !!peer_sleep_rate : peer_sleep_rate;
	ret = clk_smd_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		goto out;

	r->enabled = false;

out:
	mutex_unlock(&rpm_smd_clk_lock);
}

static int clk_smd_rpm_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	struct clk_smd_rpm *peer = r->peer;
	unsigned long active_rate, sleep_rate;
	unsigned long this_rate = 0, this_sleep_rate = 0;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	int ret = 0;

	mutex_lock(&rpm_smd_clk_lock);

	if (!r->enabled)
		goto out;

	to_active_sleep(r, rate, &this_rate, &this_sleep_rate);

	/* Take peer clock's rate into account only if it's enabled. */
	if (peer->enabled)
		to_active_sleep(peer, peer->rate,
				&peer_rate, &peer_sleep_rate);

	active_rate = max(this_rate, peer_rate);
	ret = clk_smd_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = max(this_sleep_rate, peer_sleep_rate);
	ret = clk_smd_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		goto out;

	r->rate = rate;

out:
	mutex_unlock(&rpm_smd_clk_lock);

	return ret;
}

static long clk_smd_rpm_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	/*
	 * RPM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate is requested.
	 */
	return rate;
}

static unsigned long clk_smd_rpm_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);

	/*
	 * RPM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate was set.
	 */
	return r->rate;
}

static int clk_smd_rpm_enable_scaling(struct qcom_smd_rpm *rpm)
{
	int ret;
	struct clk_smd_rpm_req req = {
		.key = cpu_to_le32(QCOM_RPM_SMD_KEY_ENABLE),
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(1),
	};

	ret = qcom_rpm_smd_write(rpm, QCOM_SMD_RPM_SLEEP_STATE,
				 QCOM_SMD_RPM_MISC_CLK,
				 QCOM_RPM_SCALING_ENABLE_ID, &req, sizeof(req));
	if (ret) {
		pr_err("RPM clock scaling (sleep set) not enabled!\n");
		return ret;
	}

	ret = qcom_rpm_smd_write(rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				 QCOM_SMD_RPM_MISC_CLK,
				 QCOM_RPM_SCALING_ENABLE_ID, &req, sizeof(req));
	if (ret) {
		pr_err("RPM clock scaling (active set) not enabled!\n");
		return ret;
	}

	pr_debug("%s: RPM clock scaling is enabled\n", __func__);
	return 0;
}

static const struct clk_ops clk_smd_rpm_ops = {
	.prepare	= clk_smd_rpm_prepare,
	.unprepare	= clk_smd_rpm_unprepare,
	.set_rate	= clk_smd_rpm_set_rate,
	.round_rate	= clk_smd_rpm_round_rate,
	.recalc_rate	= clk_smd_rpm_recalc_rate,
};

static const struct clk_ops clk_smd_rpm_branch_ops = {
	.prepare	= clk_smd_rpm_prepare,
	.unprepare	= clk_smd_rpm_unprepare,
};

DEFINE_CLK_SMD_RPM(msm8916, pcnoc_clk, pcnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8916, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8916, bimc_clk, bimc_a_clk, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM_QDSS(msm8916, qdss_clk, qdss_a_clk, QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, bb_clk1, bb_clk1_a, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, bb_clk2, bb_clk2_a, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, rf_clk1, rf_clk1_a, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, rf_clk2, rf_clk2_a, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, bb_clk1_pin, bb_clk1_a_pin, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, bb_clk2_pin, bb_clk2_a_pin, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, rf_clk1_pin, rf_clk1_a_pin, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, rf_clk2_pin, rf_clk2_a_pin, 5);

static struct clk_smd_rpm *msm8916_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a,
	[RPM_SMD_RF_CLK1]		= &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A]		= &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN]		= &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN]		= &msm8916_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN]		= &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN]		= &msm8916_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8916 = {
	.clks = msm8916_clks,
	.num_clks = ARRAY_SIZE(msm8916_clks),
};

DEFINE_CLK_SMD_RPM(msm8936, sysmmnoc_clk, sysmmnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);

static struct clk_smd_rpm *msm8936_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_clk,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk,
	[RPM_SMD_SYSMMNOC_CLK]		= &msm8936_sysmmnoc_clk,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &msm8936_sysmmnoc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a,
	[RPM_SMD_RF_CLK1]		= &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A]		= &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN]		= &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN]		= &msm8916_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN]		= &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN]		= &msm8916_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8936 = {
		.clks = msm8936_clks,
		.num_clks = ARRAY_SIZE(msm8936_clks),
};

DEFINE_CLK_SMD_RPM(msm8974, cnoc_clk, cnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM(msm8974, mmssnoc_ahb_clk, mmssnoc_ahb_a_clk, QCOM_SMD_RPM_BUS_CLK, 3);
DEFINE_CLK_SMD_RPM(msm8974, gfx3d_clk_src, gfx3d_a_clk_src, QCOM_SMD_RPM_MEM_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8974, ocmemgx_clk, ocmemgx_a_clk, QCOM_SMD_RPM_MEM_CLK, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_d0, cxo_d0_a, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_d1, cxo_d1_a, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a0, cxo_a0_a, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a1, cxo_a1_a, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a2, cxo_a2_a, 6);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, diff_clk, diff_a_clk, 7);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, div_clk1, div_a_clk1, 11);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, div_clk2, div_a_clk2, 12);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_d0_pin, cxo_d0_a_pin, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_d1_pin, cxo_d1_a_pin, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a0_pin, cxo_a0_a_pin, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a1_pin, cxo_a1_a_pin, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a2_pin, cxo_a2_a_pin, 6);

static struct clk_smd_rpm *msm8974_clks[] = {
	[RPM_SMD_PNOC_CLK]		= &msm8916_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK]		= &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk,
	[RPM_SMD_CNOC_CLK]		= &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK]		= &msm8974_cnoc_a_clk,
	[RPM_SMD_MMSSNOC_AHB_CLK]	= &msm8974_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK]	= &msm8974_mmssnoc_ahb_a_clk,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk,
	[RPM_SMD_GFX3D_CLK_SRC]		= &msm8974_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC]	= &msm8974_gfx3d_a_clk_src,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk,
	[RPM_SMD_OCMEMGX_CLK]		= &msm8974_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK]		= &msm8974_ocmemgx_a_clk,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk,
	[RPM_SMD_CXO_D0]		= &msm8974_cxo_d0,
	[RPM_SMD_CXO_D0_A]		= &msm8974_cxo_d0_a,
	[RPM_SMD_CXO_D1]		= &msm8974_cxo_d1,
	[RPM_SMD_CXO_D1_A]		= &msm8974_cxo_d1_a,
	[RPM_SMD_CXO_A0]		= &msm8974_cxo_a0,
	[RPM_SMD_CXO_A0_A]		= &msm8974_cxo_a0_a,
	[RPM_SMD_CXO_A1]		= &msm8974_cxo_a1,
	[RPM_SMD_CXO_A1_A]		= &msm8974_cxo_a1_a,
	[RPM_SMD_CXO_A2]		= &msm8974_cxo_a2,
	[RPM_SMD_CXO_A2_A]		= &msm8974_cxo_a2_a,
	[RPM_SMD_DIFF_CLK]		= &msm8974_diff_clk,
	[RPM_SMD_DIFF_A_CLK]		= &msm8974_diff_a_clk,
	[RPM_SMD_DIV_CLK1]		= &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1]		= &msm8974_div_a_clk1,
	[RPM_SMD_DIV_CLK2]		= &msm8974_div_clk2,
	[RPM_SMD_DIV_A_CLK2]		= &msm8974_div_a_clk2,
	[RPM_SMD_CXO_D0_PIN]		= &msm8974_cxo_d0_pin,
	[RPM_SMD_CXO_D0_A_PIN]		= &msm8974_cxo_d0_a_pin,
	[RPM_SMD_CXO_D1_PIN]		= &msm8974_cxo_d1_pin,
	[RPM_SMD_CXO_D1_A_PIN]		= &msm8974_cxo_d1_a_pin,
	[RPM_SMD_CXO_A0_PIN]		= &msm8974_cxo_a0_pin,
	[RPM_SMD_CXO_A0_A_PIN]		= &msm8974_cxo_a0_a_pin,
	[RPM_SMD_CXO_A1_PIN]		= &msm8974_cxo_a1_pin,
	[RPM_SMD_CXO_A1_A_PIN]		= &msm8974_cxo_a1_a_pin,
	[RPM_SMD_CXO_A2_PIN]		= &msm8974_cxo_a2_pin,
	[RPM_SMD_CXO_A2_A_PIN]		= &msm8974_cxo_a2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8974 = {
	.clks = msm8974_clks,
	.num_clks = ARRAY_SIZE(msm8974_clks),
};

DEFINE_CLK_SMD_RPM(msm8976, mmssnoc_ahb_clk, mmssnoc_ahb_a_clk,
		   QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM(msm8976, ipa_clk, ipa_a_clk, QCOM_SMD_RPM_IPA_CLK, 0);

static struct clk_smd_rpm *msm8976_clks[] = {
	[RPM_SMD_PCNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &msm8976_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &msm8976_mmssnoc_ahb_a_clk,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8976 = {
	.clks = msm8976_clks,
	.num_clks = ARRAY_SIZE(msm8976_clks),
};

DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8992, div_clk3, div_clk3_a, 13);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8992, ln_bb_clk, ln_bb_a_clk, 8);

DEFINE_CLK_SMD_RPM(msm8992, ce1_clk, ce1_a_clk, QCOM_SMD_RPM_CE_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8992, ce2_clk, ce2_a_clk, QCOM_SMD_RPM_CE_CLK, 1);

static struct clk_smd_rpm *msm8992_clks[] = {
	[RPM_SMD_PNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_OCMEMGX_CLK] = &msm8974_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK] = &msm8974_ocmemgx_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk,
	[RPM_SMD_GFX3D_CLK_SRC] = &msm8974_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &msm8974_gfx3d_a_clk_src,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &msm8974_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &msm8974_mmssnoc_ahb_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_CE2_CLK] = &msm8992_ce2_clk,
	[RPM_SMD_CE2_A_CLK] = &msm8992_ce2_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8992 = {
	.clks = msm8992_clks,
	.num_clks = ARRAY_SIZE(msm8992_clks),
};

DEFINE_CLK_SMD_RPM(msm8994, ce3_clk, ce3_a_clk, QCOM_SMD_RPM_CE_CLK, 2);

static struct clk_smd_rpm *msm8994_clks[] = {
	[RPM_SMD_PNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_OCMEMGX_CLK] = &msm8974_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK] = &msm8974_ocmemgx_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk,
	[RPM_SMD_GFX3D_CLK_SRC] = &msm8974_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &msm8974_gfx3d_a_clk_src,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &msm8974_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &msm8974_mmssnoc_ahb_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_CE2_CLK] = &msm8992_ce2_clk,
	[RPM_SMD_CE2_A_CLK] = &msm8992_ce2_a_clk,
	[RPM_SMD_CE3_CLK] = &msm8994_ce3_clk,
	[RPM_SMD_CE3_A_CLK] = &msm8994_ce3_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8994 = {
	.clks = msm8994_clks,
	.num_clks = ARRAY_SIZE(msm8994_clks),
};

DEFINE_CLK_SMD_RPM(msm8996, mmssnoc_axi_rpm_clk, mmssnoc_axi_rpm_a_clk,
		   QCOM_SMD_RPM_MMAXI_CLK, 0);
DEFINE_CLK_SMD_RPM_BRANCH(msm8996, aggre1_noc_clk, aggre1_noc_a_clk,
			  QCOM_SMD_RPM_AGGR_CLK, 1, 1000);
DEFINE_CLK_SMD_RPM_BRANCH(msm8996, aggre2_noc_clk, aggre2_noc_a_clk,
			  QCOM_SMD_RPM_AGGR_CLK, 2, 1000);

static struct clk_smd_rpm *msm8996_clks[] = {
	[RPM_SMD_PCNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_MMAXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMAXI_A_CLK] = &msm8996_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_AGGR1_NOC_CLK] = &msm8996_aggre1_noc_clk,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &msm8996_aggre1_noc_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8996_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8996_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8996 = {
	.clks = msm8996_clks,
	.num_clks = ARRAY_SIZE(msm8996_clks),
};

DEFINE_CLK_SMD_RPM(qcs404, bimc_gpu_clk, bimc_gpu_a_clk, QCOM_SMD_RPM_MEM_CLK, 2);
DEFINE_CLK_SMD_RPM(qcs404, qpic_clk, qpic_a_clk, QCOM_SMD_RPM_QPIC_CLK, 0);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(qcs404, ln_bb_clk_pin, ln_bb_clk_a_pin, 8);

static struct clk_smd_rpm *qcs404_clks[] = {
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_PNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_BIMC_GPU_CLK] = &qcs404_bimc_gpu_clk,
	[RPM_SMD_BIMC_GPU_A_CLK] = &qcs404_bimc_gpu_a_clk,
	[RPM_SMD_QPIC_CLK] = &qcs404_qpic_clk,
	[RPM_SMD_QPIC_CLK_A] = &qcs404_qpic_a_clk,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_qcs404 = {
	.clks = qcs404_clks,
	.num_clks = ARRAY_SIZE(qcs404_clks),
};

DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8998, ln_bb_clk3_pin, ln_bb_clk3_a_pin,
				     3);
DEFINE_CLK_SMD_RPM(msm8998, aggre1_noc_clk, aggre1_noc_a_clk,
		   QCOM_SMD_RPM_AGGR_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8998, aggre2_noc_clk, aggre2_noc_a_clk,
		   QCOM_SMD_RPM_AGGR_CLK, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8998, rf_clk3, rf_clk3_a, 6);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8998, rf_clk3_pin, rf_clk3_a_pin, 6);
static struct clk_smd_rpm *msm8998_clks[] = {
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_PCNOC_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK1] = &msm8916_bb_clk1,
	[RPM_SMD_LN_BB_CLK1_A] = &msm8916_bb_clk1_a,
	[RPM_SMD_LN_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_LN_BB_CLK3_PIN] = &msm8998_ln_bb_clk3_pin,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &msm8998_ln_bb_clk3_a_pin,
	[RPM_SMD_MMAXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMAXI_A_CLK] = &msm8996_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_AGGR1_NOC_CLK] = &msm8998_aggre1_noc_clk,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &msm8998_aggre1_noc_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8998_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8998_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin,
	[RPM_SMD_RF_CLK3] = &msm8998_rf_clk3,
	[RPM_SMD_RF_CLK3_A] = &msm8998_rf_clk3_a,
	[RPM_SMD_RF_CLK3_PIN] = &msm8998_rf_clk3_pin,
	[RPM_SMD_RF_CLK3_A_PIN] = &msm8998_rf_clk3_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8998 = {
	.clks = msm8998_clks,
	.num_clks = ARRAY_SIZE(msm8998_clks),
};

DEFINE_CLK_SMD_RPM_BRANCH(sdm660, bi_tcxo, bi_tcxo_a, QCOM_SMD_RPM_MISC_CLK, 0,
								19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER(sdm660, ln_bb_clk3, ln_bb_clk3_a, 3);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(sdm660, ln_bb_clk3_pin, ln_bb_clk3_pin_a, 3);

static struct clk_smd_rpm *sdm660_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk,
	[RPM_SMD_CNOC_PERIPH_CLK] = &msm8916_pcnoc_clk,
	[RPM_SMD_CNOC_PERIPH_A_CLK] = &msm8916_pcnoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk,
	[RPM_SMD_MMSSNOC_AXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMSSNOC_AXI_CLK_A] = &msm8996_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8998_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8998_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1,
	[RPM_SMD_LN_BB_CLK] = &msm8916_bb_clk1,
	[RPM_SMD_LN_BB_A_CLK] = &msm8916_bb_clk1_a,
	[RPM_SMD_LN_BB_CLK2] = &msm8916_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &msm8916_bb_clk2_a,
	[RPM_SMD_LN_BB_CLK3] = &sdm660_ln_bb_clk3,
	[RPM_SMD_LN_BB_CLK3_A] = &sdm660_ln_bb_clk3_a,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin,
	[RPM_SMD_LN_BB_CLK1_PIN] = &msm8916_bb_clk1_pin,
	[RPM_SMD_LN_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin,
	[RPM_SMD_LN_BB_CLK2_PIN] = &msm8916_bb_clk2_pin,
	[RPM_SMD_LN_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin,
	[RPM_SMD_LN_BB_CLK3_PIN] = &sdm660_ln_bb_clk3_pin,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &sdm660_ln_bb_clk3_pin_a,
};

static const struct rpm_smd_clk_desc rpm_clk_sdm660 = {
	.clks = sdm660_clks,
	.num_clks = ARRAY_SIZE(sdm660_clks),
};

static const struct of_device_id rpm_smd_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-msm8226", .data = &rpm_clk_msm8974 },
	{ .compatible = "qcom,rpmcc-msm8916", .data = &rpm_clk_msm8916 },
	{ .compatible = "qcom,rpmcc-msm8936", .data = &rpm_clk_msm8936 },
	{ .compatible = "qcom,rpmcc-msm8974", .data = &rpm_clk_msm8974 },
	{ .compatible = "qcom,rpmcc-msm8976", .data = &rpm_clk_msm8976 },
	{ .compatible = "qcom,rpmcc-msm8992", .data = &rpm_clk_msm8992 },
	{ .compatible = "qcom,rpmcc-msm8994", .data = &rpm_clk_msm8994 },
	{ .compatible = "qcom,rpmcc-msm8996", .data = &rpm_clk_msm8996 },
	{ .compatible = "qcom,rpmcc-msm8998", .data = &rpm_clk_msm8998 },
	{ .compatible = "qcom,rpmcc-qcs404",  .data = &rpm_clk_qcs404  },
	{ .compatible = "qcom,rpmcc-sdm660",  .data = &rpm_clk_sdm660  },
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_smd_clk_match_table);

static struct clk_hw *qcom_smdrpm_clk_hw_get(struct of_phandle_args *clkspec,
					     void *data)
{
	struct rpm_cc *rcc = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= rcc->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return rcc->clks[idx] ? &rcc->clks[idx]->hw : ERR_PTR(-ENOENT);
}

static int rpm_smd_clk_probe(struct platform_device *pdev)
{
	struct rpm_cc *rcc;
	int ret;
	size_t num_clks, i;
	struct qcom_smd_rpm *rpm;
	struct clk_smd_rpm **rpm_smd_clks;
	const struct rpm_smd_clk_desc *desc;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "Unable to retrieve handle to RPM\n");
		return -ENODEV;
	}

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rpm_smd_clks = desc->clks;
	num_clks = desc->num_clks;

	rcc = devm_kzalloc(&pdev->dev, sizeof(*rcc), GFP_KERNEL);
	if (!rcc)
		return -ENOMEM;

	rcc->clks = rpm_smd_clks;
	rcc->num_clks = num_clks;

	for (i = 0; i < num_clks; i++) {
		if (!rpm_smd_clks[i])
			continue;

		rpm_smd_clks[i]->rpm = rpm;

		ret = clk_smd_rpm_handoff(rpm_smd_clks[i]);
		if (ret)
			goto err;
	}

	ret = clk_smd_rpm_enable_scaling(rpm);
	if (ret)
		goto err;

	for (i = 0; i < num_clks; i++) {
		if (!rpm_smd_clks[i])
			continue;

		ret = devm_clk_hw_register(&pdev->dev, &rpm_smd_clks[i]->hw);
		if (ret)
			goto err;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, qcom_smdrpm_clk_hw_get,
				     rcc);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&pdev->dev, "Error registering SMD clock driver (%d)\n", ret);
	return ret;
}

static struct platform_driver rpm_smd_clk_driver = {
	.driver = {
		.name = "qcom-clk-smd-rpm",
		.of_match_table = rpm_smd_clk_match_table,
	},
	.probe = rpm_smd_clk_probe,
};

static int __init rpm_smd_clk_init(void)
{
	return platform_driver_register(&rpm_smd_clk_driver);
}
core_initcall(rpm_smd_clk_init);

static void __exit rpm_smd_clk_exit(void)
{
	platform_driver_unregister(&rpm_smd_clk_driver);
}
module_exit(rpm_smd_clk_exit);

MODULE_DESCRIPTION("Qualcomm RPM over SMD Clock Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-clk-smd-rpm");
