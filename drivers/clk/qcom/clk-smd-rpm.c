// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Linaro Limited
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <soc/qcom/rpm-smd.h>
#include <linux/clk.h>
#include <linux/panic_notifier.h>

#include <dt-bindings/clock/qcom,rpmcc.h>

#include "clk-debug.h"
#include "common.h"

#define QCOM_RPM_KEY_SOFTWARE_ENABLE			0x6e657773
#define QCOM_RPM_KEY_PIN_CTRL_CLK_BUFFER_ENABLE_KEY	0x62636370
#define QCOM_RPM_SMD_KEY_RATE				0x007a484b
#define QCOM_RPM_SMD_KEY_ENABLE				0x62616e45
#define QCOM_RPM_SMD_KEY_STATE				0x54415453
#define QCOM_RPM_SCALING_ENABLE_ID			0x2

#define __DEFINE_CLK_SMD_RPM(_platform, _name, _active, type, r_id, stat_id,  \
			     key)					      \
	static struct clk_smd_rpm _platform##_##_active;		      \
	static unsigned long _name##_##last_active_set_vote;		      \
	static unsigned long _name##_##last_sleep_set_vote;		      \
	static struct clk_smd_rpm _platform##_##_name = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.peer = &_platform##_##_active,				      \
		.rate = INT_MAX,					      \
		.last_active_set_vote = &_name##_##last_active_set_vote,      \
		.last_sleep_set_vote = &_name##_##last_sleep_set_vote,	      \
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
		.last_active_set_vote = &_name##_##last_active_set_vote,      \
		.last_sleep_set_vote = &_name##_##last_sleep_set_vote,	      \
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
	static unsigned long _name##_##last_active_set_vote;		      \
	static unsigned long _name##_##last_sleep_set_vote;		      \
	static struct clk_smd_rpm _platform##_##_name = {		      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_status_id = (stat_id),				      \
		.rpm_key = (key),					      \
		.branch = true,						      \
		.peer = &_platform##_##_active,				      \
		.rate = (r),						      \
		.last_active_set_vote = &_name##_##last_active_set_vote,      \
		.last_sleep_set_vote = &_name##_##last_sleep_set_vote,	      \
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
		.last_active_set_vote = &_name##_##last_active_set_vote,      \
		.last_sleep_set_vote = &_name##_##last_sleep_set_vote,	      \
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

#define DEFINE_CLK_SMD_RPM_XO_BUFFER(_platform, _name, _active, type, r_id) \
		__DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active,	      \
		type, r_id, 0, 1000,			      \
		QCOM_RPM_KEY_SOFTWARE_ENABLE)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(_platform, _name, _active, type, \
		r_id)							      \
		__DEFINE_CLK_SMD_RPM_BRANCH(_platform, _name, _active,	      \
		type, r_id, 0, 1000,			      \
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
	unsigned long *last_active_set_vote;
	unsigned long *last_sleep_set_vote;
};

struct clk_smd_rpm_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

struct rpm_smd_clk_desc {
	struct clk_hw **clks;
	size_t num_clks;
};

static DEFINE_MUTEX(rpm_smd_clk_lock);

static int clk_smd_rpm_prepare(struct clk_hw *hw);

static int clk_smd_rpm_handoff(struct clk_hw *hw)
{
	return clk_smd_rpm_prepare(hw);
}

static int clk_smd_rpm_set_rate_active(struct clk_smd_rpm *r,
					uint32_t rate)
{
	int ret = 0;
	struct msm_rpm_kvp req = {
		.key = cpu_to_le32(r->rpm_key),
		.data = (void *)&rate,
		.length = sizeof(rate),
	};

	if (*r->last_active_set_vote == rate)
		return ret;

	ret = msm_rpm_send_message(QCOM_SMD_RPM_ACTIVE_STATE, r->rpm_res_type,
			r->rpm_clk_id, &req, 1);
	if (ret)
		return ret;

	*r->last_active_set_vote = rate;

	return ret;
}

static int clk_smd_rpm_set_rate_sleep(struct clk_smd_rpm *r,
					uint32_t rate)
{
	int ret = 0;
	struct msm_rpm_kvp req = {
		.key = cpu_to_le32(r->rpm_key),
		.data = (void *)&rate,
		.length = sizeof(rate),
	};

	if (*r->last_sleep_set_vote == rate)
		return ret;

	ret = msm_rpm_send_message(QCOM_SMD_RPM_SLEEP_STATE, r->rpm_res_type,
			r->rpm_clk_id, &req, 1);
	if (ret)
		return ret;

	*r->last_sleep_set_vote = rate;

	return ret;
}

static void to_active_sleep(struct clk_smd_rpm *r, unsigned long rate,
			    unsigned long *active, unsigned long *sleep)
{
	/* Convert the rate (hz) to khz */
	*active = DIV_ROUND_UP(rate, 1000);

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
	uint32_t active_rate, sleep_rate;
	int ret = 0;

	mutex_lock(&rpm_smd_clk_lock);

	to_active_sleep(r, r->rate, &this_rate, &this_sleep_rate);

	/* Don't send requests to the RPM if the rate has not been set. */
	if (this_rate == 0)
		goto out;

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
	uint32_t active_rate, sleep_rate;
	int ret;

	mutex_lock(&rpm_smd_clk_lock);

	if (!r->rate)
		goto enable;

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

enable:
	r->enabled = false;

out:
	mutex_unlock(&rpm_smd_clk_lock);
}

static int clk_smd_rpm_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	struct clk_smd_rpm *peer = r->peer;
	uint32_t active_rate, sleep_rate;
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

static int clk_smd_rpm_enable_scaling(void)
{
	int ret = 0;
	uint32_t value = cpu_to_le32(1);
	struct msm_rpm_kvp req = {
		.key = cpu_to_le32(QCOM_RPM_SMD_KEY_ENABLE),
		.data = (void *)&value,
		.length = sizeof(value),
	};

	ret = msm_rpm_send_message(QCOM_SMD_RPM_SLEEP_STATE,
			QCOM_SMD_RPM_MISC_CLK,
			QCOM_RPM_SCALING_ENABLE_ID, &req, 1);
	if (ret) {
		pr_err("RPM clock scaling (sleep set) not enabled!\n");
		return ret;
	}

	ret = msm_rpm_send_message(QCOM_SMD_RPM_ACTIVE_STATE,
			QCOM_SMD_RPM_MISC_CLK,
			QCOM_RPM_SCALING_ENABLE_ID, &req, 1);
	if (ret) {
		pr_err("RPM clock scaling (active set) not enabled!\n");
		return ret;
	}

	pr_debug("%s: RPM clock scaling is enabled\n", __func__);
	return ret;
}

static int clk_vote_bimc(struct clk_hw *hw, uint32_t rate)
{
	int ret;
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	struct msm_rpm_kvp req = {
		.key = r->rpm_key,
		.data = (void *)&rate,
		.length = sizeof(rate),
	};

	ret = msm_rpm_send_message(QCOM_SMD_RPM_ACTIVE_STATE,
		r->rpm_res_type, r->rpm_clk_id, &req, 1);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			WARN(1, "BIMC vote not sent!\n");
	}

	return ret;
}

static int clk_smd_rpm_is_enabled(struct clk_hw *hw)
{
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);

	return r->enabled;
}

static const struct clk_ops clk_smd_rpm_ops = {
	.prepare	= clk_smd_rpm_prepare,
	.unprepare	= clk_smd_rpm_unprepare,
	.set_rate	= clk_smd_rpm_set_rate,
	.round_rate	= clk_smd_rpm_round_rate,
	.recalc_rate	= clk_smd_rpm_recalc_rate,
	.is_enabled	= clk_smd_rpm_is_enabled,
	.debug_init	= clk_debug_measure_add,
};

static const struct clk_ops clk_smd_rpm_branch_ops = {
	.prepare	= clk_smd_rpm_prepare,
	.unprepare	= clk_smd_rpm_unprepare,
	.recalc_rate	= clk_smd_rpm_recalc_rate,
	.round_rate	= clk_smd_rpm_round_rate,
	.recalc_rate	= clk_smd_rpm_recalc_rate,
	.is_enabled	= clk_smd_rpm_is_enabled,
	.debug_init	= clk_debug_measure_add,
};

/*MSM8916*/
DEFINE_CLK_SMD_RPM(msm8916, pcnoc_clk, pcnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8916, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8916, bimc_clk, bimc_a_clk, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM(qcs404, qpic_clk, qpic_a_clk, QCOM_SMD_RPM_QPIC_CLK, 0);
DEFINE_CLK_SMD_RPM_QDSS(msm8916, qdss_clk, qdss_a_clk, QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, bb_clk1, bb_clk1_a, QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, bb_clk2, bb_clk2_a, QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, rf_clk1, rf_clk1_a, QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8916, rf_clk2, rf_clk2_a, QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, bb_clk1_pin, bb_clk1_a_pin,
							QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, bb_clk2_pin, bb_clk2_a_pin,
							QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, rf_clk1_pin, rf_clk1_a_pin,
							QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8916, rf_clk2_pin, rf_clk2_a_pin,
							QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_BRANCH(sdm660, bi_tcxo, bi_tcxo_a, QCOM_SMD_RPM_MISC_CLK, 0, 19200000);

static struct clk_hw *msm8916_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK1]		= &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A]		= &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a.hw,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_RF_CLK1_PIN]		= &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN]		= &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN]		= &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN]		= &msm8916_rf_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8916 = {
	.clks = msm8916_clks,
	.num_clks = ARRAY_SIZE(msm8916_clks),
};

static struct clk_hw *msm8909_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QPIC_CLK]		= &qcs404_qpic_clk.hw,
	[RPM_SMD_QPIC_CLK_A]		= &qcs404_qpic_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK1]		= &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A]		= &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a.hw,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_RF_CLK1_PIN]		= &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN]		= &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN]		= &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN]		= &msm8916_rf_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8909 = {
	.clks = msm8909_clks,
	.num_clks = ARRAY_SIZE(msm8909_clks),
};

/*MSM8936*/
DEFINE_CLK_SMD_RPM(msm8936, sysmmnoc_clk, sysmmnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);

static struct clk_hw *msm8936_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk.hw,
	[RPM_SMD_SYSMMNOC_CLK]		= &msm8936_sysmmnoc_clk.hw,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &msm8936_sysmmnoc_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK1]		= &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A]		= &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a.hw,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_RF_CLK1_PIN]		= &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN]		= &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN]		= &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN]		= &msm8916_rf_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8936 = {
		.clks = msm8936_clks,
		.num_clks = ARRAY_SIZE(msm8936_clks),
};

/*MSM8974*/
DEFINE_CLK_SMD_RPM(msm8974, pnoc_clk, pnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8974, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8974, cnoc_clk, cnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM(msm8974, mmssnoc_ahb_clk, mmssnoc_ahb_a_clk, QCOM_SMD_RPM_BUS_CLK, 3);
DEFINE_CLK_SMD_RPM(msm8974, bimc_clk, bimc_a_clk, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8974, gfx3d_clk_src, gfx3d_a_clk_src, QCOM_SMD_RPM_MEM_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8974, ocmemgx_clk, ocmemgx_a_clk, QCOM_SMD_RPM_MEM_CLK, 2);
DEFINE_CLK_SMD_RPM_QDSS(msm8974, qdss_clk, qdss_a_clk, QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_d0, cxo_d0_a, QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_d1, cxo_d1_a, QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a0, cxo_a0_a, QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a1, cxo_a1_a, QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, cxo_a2, cxo_a2_a, QCOM_SMD_RPM_CLK_BUF_A, 6);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, diff_clk, diff_a_clk, QCOM_SMD_RPM_CLK_BUF_A, 7);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, div_clk1, div_a_clk1, QCOM_SMD_RPM_CLK_BUF_A, 11);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8974, div_clk2, div_a_clk2, QCOM_SMD_RPM_CLK_BUF_A, 12);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_d0_pin, cxo_d0_a_pin, QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_d1_pin, cxo_d1_a_pin, QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a0_pin, cxo_a0_a_pin, QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a1_pin, cxo_a1_a_pin, QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8974, cxo_a2_pin, cxo_a2_a_pin, QCOM_SMD_RPM_CLK_BUF_A, 6);

static struct clk_hw *msm8974_clks[] = {
	[RPM_SMD_PNOC_CLK]		= &msm8974_pnoc_clk.hw,
	[RPM_SMD_PNOC_A_CLK]		= &msm8974_pnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK]		= &msm8974_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK]		= &msm8974_snoc_a_clk.hw,
	[RPM_SMD_CNOC_CLK]		= &msm8974_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK]		= &msm8974_cnoc_a_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_CLK]	= &msm8974_mmssnoc_ahb_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_A_CLK]	= &msm8974_mmssnoc_ahb_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8974_bimc_clk.hw,
	[RPM_SMD_GFX3D_CLK_SRC]		= &msm8974_gfx3d_clk_src.hw,
	[RPM_SMD_GFX3D_A_CLK_SRC]	= &msm8974_gfx3d_a_clk_src.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8974_bimc_a_clk.hw,
	[RPM_SMD_OCMEMGX_CLK]		= &msm8974_ocmemgx_clk.hw,
	[RPM_SMD_OCMEMGX_A_CLK]		= &msm8974_ocmemgx_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8974_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8974_qdss_a_clk.hw,
	[RPM_SMD_CXO_D0]		= &msm8974_cxo_d0.hw,
	[RPM_SMD_CXO_D0_A]		= &msm8974_cxo_d0_a.hw,
	[RPM_SMD_CXO_D1]		= &msm8974_cxo_d1.hw,
	[RPM_SMD_CXO_D1_A]		= &msm8974_cxo_d1_a.hw,
	[RPM_SMD_CXO_A0]		= &msm8974_cxo_a0.hw,
	[RPM_SMD_CXO_A0_A]		= &msm8974_cxo_a0_a.hw,
	[RPM_SMD_CXO_A1]		= &msm8974_cxo_a1.hw,
	[RPM_SMD_CXO_A1_A]		= &msm8974_cxo_a1_a.hw,
	[RPM_SMD_CXO_A2]		= &msm8974_cxo_a2.hw,
	[RPM_SMD_CXO_A2_A]		= &msm8974_cxo_a2_a.hw,
	[RPM_SMD_DIFF_CLK]		= &msm8974_diff_clk.hw,
	[RPM_SMD_DIFF_A_CLK]		= &msm8974_diff_a_clk.hw,
	[RPM_SMD_DIV_CLK1]		= &msm8974_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1]		= &msm8974_div_a_clk1.hw,
	[RPM_SMD_DIV_CLK2]		= &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2]		= &msm8974_div_a_clk2.hw,
	[RPM_SMD_CXO_D0_PIN]		= &msm8974_cxo_d0_pin.hw,
	[RPM_SMD_CXO_D0_A_PIN]		= &msm8974_cxo_d0_a_pin.hw,
	[RPM_SMD_CXO_D1_PIN]		= &msm8974_cxo_d1_pin.hw,
	[RPM_SMD_CXO_D1_A_PIN]		= &msm8974_cxo_d1_a_pin.hw,
	[RPM_SMD_CXO_A0_PIN]		= &msm8974_cxo_a0_pin.hw,
	[RPM_SMD_CXO_A0_A_PIN]		= &msm8974_cxo_a0_a_pin.hw,
	[RPM_SMD_CXO_A1_PIN]		= &msm8974_cxo_a1_pin.hw,
	[RPM_SMD_CXO_A1_A_PIN]		= &msm8974_cxo_a1_a_pin.hw,
	[RPM_SMD_CXO_A2_PIN]		= &msm8974_cxo_a2_pin.hw,
	[RPM_SMD_CXO_A2_A_PIN]		= &msm8974_cxo_a2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8974 = {
	.clks = msm8974_clks,
	.num_clks = ARRAY_SIZE(msm8974_clks),
};

/*MSM8976*/
DEFINE_CLK_SMD_RPM(msm8976, ipa_clk, ipa_a_clk, QCOM_SMD_RPM_IPA_CLK, 0);

static struct clk_hw *msm8976_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_PCNOC_CLK] = &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK] = &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_SYSMMNOC_CLK]	= &msm8936_sysmmnoc_clk.hw,
	[RPM_SMD_SYSMMNOC_A_CLK] = &msm8936_sysmmnoc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8976 = {
	.clks = msm8976_clks,
	.num_clks = ARRAY_SIZE(msm8976_clks),
};

/*MSM8992*/
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8992, div_clk3, div_clk3_a, QCOM_SMD_RPM_CLK_BUF_A, 13);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8992, ln_bb_clk, ln_bb_a_clk, QCOM_SMD_RPM_CLK_BUF_A, 8);

DEFINE_CLK_SMD_RPM(msm8992, ce1_clk, ce1_a_clk, QCOM_SMD_RPM_CE_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8992, ce2_clk, ce2_a_clk, QCOM_SMD_RPM_CE_CLK, 1);

DEFINE_CLK_SMD_RPM_BRANCH(msm8992, mss_cfg_ahb_clk, mss_cfg_ahb_a_clk,
			  QCOM_SMD_RPM_MCFG_CLK, 0, 19200000);
static struct clk_hw *msm8992_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_PNOC_CLK] = &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PNOC_A_CLK] = &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_OCMEMGX_CLK] = &msm8974_ocmemgx_clk.hw,
	[RPM_SMD_OCMEMGX_A_CLK] = &msm8974_ocmemgx_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk.hw,
	[RPM_SMD_GFX3D_CLK_SRC] = &msm8974_gfx3d_clk_src.hw,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &msm8974_gfx3d_a_clk_src.hw,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1.hw,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2.hw,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3.hw,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk.hw,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &msm8974_mmssnoc_ahb_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &msm8974_mmssnoc_ahb_a_clk.hw,
	[RPM_SMD_MSS_CFG_AHB_CLK] = &msm8992_mss_cfg_ahb_clk.hw,
	[RPM_SMD_MSS_CFG_AHB_A_CLK] = &msm8992_mss_cfg_ahb_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_CE2_CLK] = &msm8992_ce2_clk.hw,
	[RPM_SMD_CE2_A_CLK] = &msm8992_ce2_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8992 = {
	.clks = msm8992_clks,
	.num_clks = ARRAY_SIZE(msm8992_clks),
};

/*MSM8994*/
DEFINE_CLK_SMD_RPM(msm8994, ce3_clk, ce3_a_clk, QCOM_SMD_RPM_CE_CLK, 2);

static struct clk_hw *msm8994_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_PNOC_CLK] = &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PNOC_A_CLK] = &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_OCMEMGX_CLK] = &msm8974_ocmemgx_clk.hw,
	[RPM_SMD_OCMEMGX_A_CLK] = &msm8974_ocmemgx_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk.hw,
	[RPM_SMD_GFX3D_CLK_SRC] = &msm8974_gfx3d_clk_src.hw,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &msm8974_gfx3d_a_clk_src.hw,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BB_CLK1] = &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK1_PIN] = &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_BB_CLK2_PIN] = &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1.hw,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2.hw,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3.hw,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_LN_BB_CLK] = &msm8992_ln_bb_clk.hw,
	[RPM_SMD_LN_BB_A_CLK] = &msm8992_ln_bb_a_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &msm8974_mmssnoc_ahb_clk.hw,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &msm8974_mmssnoc_ahb_a_clk.hw,
	[RPM_SMD_MSS_CFG_AHB_CLK] = &msm8992_mss_cfg_ahb_clk.hw,
	[RPM_SMD_MSS_CFG_AHB_A_CLK] = &msm8992_mss_cfg_ahb_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_CE2_CLK] = &msm8992_ce2_clk.hw,
	[RPM_SMD_CE2_A_CLK] = &msm8992_ce2_a_clk.hw,
	[RPM_SMD_CE3_CLK] = &msm8994_ce3_clk.hw,
	[RPM_SMD_CE3_A_CLK] = &msm8994_ce3_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8994 = {
	.clks = msm8994_clks,
	.num_clks = ARRAY_SIZE(msm8994_clks),
};

/*MSM8996*/
DEFINE_CLK_SMD_RPM(msm8996, pcnoc_clk, pcnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8996, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8996, cnoc_clk, cnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM(msm8996, bimc_clk, bimc_a_clk, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8996, mmssnoc_axi_rpm_clk, mmssnoc_axi_rpm_a_clk,
		   QCOM_SMD_RPM_MMAXI_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8996, ipa_clk, ipa_a_clk, QCOM_SMD_RPM_IPA_CLK, 0);
DEFINE_CLK_SMD_RPM(msm8996, ce1_clk, ce1_a_clk, QCOM_SMD_RPM_CE_CLK, 0);
DEFINE_CLK_SMD_RPM_BRANCH(msm8996, aggre1_noc_clk, aggre1_noc_a_clk,
			  QCOM_SMD_RPM_AGGR_CLK, 1, 1000);
DEFINE_CLK_SMD_RPM_BRANCH(msm8996, aggre2_noc_clk, aggre2_noc_a_clk,
			  QCOM_SMD_RPM_AGGR_CLK, 2, 1000);
DEFINE_CLK_SMD_RPM_QDSS(msm8996, qdss_clk, qdss_a_clk,
					QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, bb_clk1, bb_clk1_a,
						QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, bb_clk2, bb_clk2_a,
						QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, rf_clk1, rf_clk1_a,
						QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, rf_clk2, rf_clk2_a,
						QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, ln_bb_clk, ln_bb_a_clk,
						QCOM_SMD_RPM_CLK_BUF_A, 8);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, div_clk1, div_clk1_a,
						QCOM_SMD_RPM_CLK_BUF_A, 0xb);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, div_clk2, div_clk2_a,
						QCOM_SMD_RPM_CLK_BUF_A, 0xc);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8996, div_clk3, div_clk3_a,
						QCOM_SMD_RPM_CLK_BUF_A, 0xd);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8996, bb_clk1_pin, bb_clk1_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 1);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8996, bb_clk2_pin, bb_clk2_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8996, rf_clk1_pin, rf_clk1_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8996, rf_clk2_pin, rf_clk2_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 5);

static struct clk_hw *msm8996_clks[] = {
	[RPM_SMD_PCNOC_CLK] = &msm8996_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK] = &msm8996_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK] = &msm8996_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8996_snoc_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &msm8996_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &msm8996_cnoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8996_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8996_bimc_a_clk.hw,
	[RPM_SMD_MMAXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk.hw,
	[RPM_SMD_MMAXI_A_CLK] = &msm8996_mmssnoc_axi_rpm_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8996_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8996_ipa_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8996_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8996_ce1_a_clk.hw,
	[RPM_SMD_AGGR1_NOC_CLK] = &msm8996_aggre1_noc_clk.hw,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &msm8996_aggre1_noc_a_clk.hw,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8996_aggre2_noc_clk.hw,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8996_aggre2_noc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8996_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8996_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1] = &msm8996_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A] = &msm8996_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2] = &msm8996_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A] = &msm8996_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK1] = &msm8996_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8996_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8996_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8996_rf_clk2_a.hw,
	[RPM_SMD_LN_BB_CLK] = &msm8996_ln_bb_clk.hw,
	[RPM_SMD_LN_BB_A_CLK] = &msm8996_ln_bb_a_clk.hw,
	[RPM_SMD_DIV_CLK1] = &msm8996_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1] = &msm8996_div_clk1_a.hw,
	[RPM_SMD_DIV_CLK2] = &msm8996_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2] = &msm8996_div_clk2_a.hw,
	[RPM_SMD_DIV_CLK3] = &msm8996_div_clk3.hw,
	[RPM_SMD_DIV_A_CLK3] = &msm8996_div_clk3_a.hw,
	[RPM_SMD_BB_CLK1_PIN] = &msm8996_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN] = &msm8996_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN] = &msm8996_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN] = &msm8996_bb_clk2_a_pin.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8996_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8996_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN] = &msm8996_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8996_rf_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8996 = {
	.clks = msm8996_clks,
	.num_clks = ARRAY_SIZE(msm8996_clks),
};

/* QCS404 */
DEFINE_CLK_SMD_RPM_QDSS(qcs404, qdss_clk, qdss_a_clk, QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM(qcs404, pnoc_clk, pnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(qcs404, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(qcs404, bimc_gpu_clk, bimc_gpu_a_clk, QCOM_SMD_RPM_MEM_CLK, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(qcs404, rf_clk1, rf_clk1_a,
				QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(qcs404, rf_clk1_pin, rf_clk1_a_pin,
				QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(qcs404, ln_bb_clk, ln_bb_a_clk,
				QCOM_SMD_RPM_CLK_BUF_A, 8);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(qcs404, ln_bb_clk_pin, ln_bb_clk_a_pin,
				QCOM_SMD_RPM_CLK_BUF_A, 8);

static struct clk_hw *qcs404_clks[] = {
	[RPM_SMD_QDSS_CLK] = &qcs404_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &qcs404_qdss_a_clk.hw,
	[RPM_SMD_PNOC_CLK] = &qcs404_pnoc_clk.hw,
	[RPM_SMD_PNOC_A_CLK] = &qcs404_pnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK] = &qcs404_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &qcs404_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8996_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8996_bimc_a_clk.hw,
	[RPM_SMD_BIMC_GPU_CLK] = &qcs404_bimc_gpu_clk.hw,
	[RPM_SMD_BIMC_GPU_A_CLK] = &qcs404_bimc_gpu_a_clk.hw,
	[RPM_SMD_QPIC_CLK] = &qcs404_qpic_clk.hw,
	[RPM_SMD_QPIC_CLK_A] = &qcs404_qpic_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &qcs404_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &qcs404_rf_clk1_a.hw,
	[RPM_SMD_LN_BB_CLK] = &qcs404_ln_bb_clk.hw,
	[RPM_SMD_LN_BB_A_CLK] = &qcs404_ln_bb_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_qcs404 = {
	.clks = qcs404_clks,
	.num_clks = ARRAY_SIZE(qcs404_clks),
};

/*MSM8998*/
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8998, ln_bb_clk3, ln_bb_clk3_a, QCOM_SMD_RPM_CLK_BUF_A, 3);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8998, ln_bb_clk3_pin, ln_bb_clk3_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 3);
DEFINE_CLK_SMD_RPM(msm8998, aggre1_noc_clk, aggre1_noc_a_clk,
		   QCOM_SMD_RPM_AGGR_CLK, 1);
DEFINE_CLK_SMD_RPM(msm8998, aggre2_noc_clk, aggre2_noc_a_clk,
		   QCOM_SMD_RPM_AGGR_CLK, 2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(msm8998, rf_clk3, rf_clk3_a, QCOM_SMD_RPM_CLK_BUF_A, 6);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(msm8998, rf_clk3_pin, rf_clk3_a_pin,
						QCOM_SMD_RPM_CLK_BUF_A, 6);

static struct clk_hw *msm8998_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_PCNOC_CLK] = &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK] = &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1.hw,
	[RPM_SMD_DIV_CLK2] = &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2] = &msm8974_div_a_clk2.hw,
	[RPM_SMD_DIV_CLK3] = &msm8992_div_clk3.hw,
	[RPM_SMD_DIV_A_CLK3] = &msm8992_div_clk3_a.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_LN_BB_CLK1] = &msm8916_bb_clk1.hw,
	[RPM_SMD_LN_BB_CLK1_A] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_LN_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_LN_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_LN_BB_CLK3] = &msm8998_ln_bb_clk3.hw,
	[RPM_SMD_LN_BB_CLK3_A] = &msm8998_ln_bb_clk3_a.hw,
	[RPM_SMD_LN_BB_CLK1_PIN] = &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_LN_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_LN_BB_CLK2_PIN] = &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_LN_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_LN_BB_CLK3_PIN] = &msm8998_ln_bb_clk3_pin.hw,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &msm8998_ln_bb_clk3_a_pin.hw,
	[RPM_SMD_MMAXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk.hw,
	[RPM_SMD_MMAXI_A_CLK] = &msm8996_mmssnoc_axi_rpm_a_clk.hw,
	[RPM_SMD_AGGR1_NOC_CLK] = &msm8998_aggre1_noc_clk.hw,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &msm8998_aggre1_noc_a_clk.hw,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8998_aggre2_noc_clk.hw,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8998_aggre2_noc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_RF_CLK3] = &msm8998_rf_clk3.hw,
	[RPM_SMD_RF_CLK3_A] = &msm8998_rf_clk3_a.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin.hw,
	[RPM_SMD_RF_CLK3_PIN] = &msm8998_rf_clk3_pin.hw,
	[RPM_SMD_RF_CLK3_A_PIN] = &msm8998_rf_clk3_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8998 = {
	.clks = msm8998_clks,
	.num_clks = ARRAY_SIZE(msm8998_clks),
};

static struct clk_hw *sdm660_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_SNOC_CLK] = &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &msm8916_snoc_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &msm8974_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &msm8974_cnoc_a_clk.hw,
	[RPM_SMD_CNOC_PERIPH_CLK] = &msm8916_pcnoc_clk.hw,
	[RPM_SMD_CNOC_PERIPH_A_CLK] = &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_MMSSNOC_AXI_CLK] = &msm8996_mmssnoc_axi_rpm_clk.hw,
	[RPM_SMD_MMSSNOC_AXI_CLK_A] = &msm8996_mmssnoc_axi_rpm_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_AGGR2_NOC_CLK] = &msm8998_aggre2_noc_clk.hw,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &msm8998_aggre2_noc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &msm8916_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_DIV_CLK1] = &msm8974_div_clk1.hw,
	[RPM_SMD_DIV_A_CLK1] = &msm8974_div_a_clk1.hw,
	[RPM_SMD_LN_BB_CLK] = &msm8916_bb_clk1.hw,
	[RPM_SMD_LN_BB_A_CLK] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_LN_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_LN_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_LN_BB_CLK3] = &msm8998_ln_bb_clk3.hw,
	[RPM_SMD_LN_BB_CLK3_A] = &msm8998_ln_bb_clk3_a.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_LN_BB_CLK1_PIN] = &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_LN_BB_CLK1_A_PIN] = &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_LN_BB_CLK2_PIN] = &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_LN_BB_CLK2_A_PIN] = &msm8916_bb_clk2_a_pin.hw,
	[RPM_SMD_LN_BB_CLK3_PIN] = &msm8998_ln_bb_clk3_pin.hw,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &msm8998_ln_bb_clk3_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_sdm660 = {
	.clks = sdm660_clks,
	.num_clks = ARRAY_SIZE(sdm660_clks),
};

static struct clk_hw *mdm9607_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC]		= &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QPIC_CLK]		= &qcs404_qpic_clk.hw,
	[RPM_SMD_QPIC_CLK_A]		= &qcs404_qpic_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_mdm9607 = {
	.clks = mdm9607_clks,
	.num_clks = ARRAY_SIZE(mdm9607_clks),
};

static struct clk_hw *msm8953_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC]		= &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_PCNOC_CLK]		= &msm8916_pcnoc_clk.hw,
	[RPM_SMD_PCNOC_A_CLK]		= &msm8916_pcnoc_a_clk.hw,
	[RPM_SMD_SNOC_CLK]		= &msm8916_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK]		= &msm8916_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK]		= &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK]		= &msm8916_bimc_a_clk.hw,
	[RPM_SMD_IPA_CLK]		= &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK]		= &msm8976_ipa_a_clk.hw,
	[RPM_SMD_SYSMMNOC_CLK]		= &msm8936_sysmmnoc_clk.hw,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &msm8936_sysmmnoc_a_clk.hw,
	[RPM_SMD_QDSS_CLK]		= &msm8916_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK]		= &msm8916_qdss_a_clk.hw,
	[RPM_SMD_BB_CLK1]		= &msm8916_bb_clk1.hw,
	[RPM_SMD_BB_CLK1_A]		= &msm8916_bb_clk1_a.hw,
	[RPM_SMD_BB_CLK2]		= &msm8916_bb_clk2.hw,
	[RPM_SMD_BB_CLK2_A]		= &msm8916_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK2]		= &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A]		= &msm8916_rf_clk2_a.hw,
	[RPM_SMD_RF_CLK3]		= &msm8992_ln_bb_clk.hw,
	[RPM_SMD_RF_CLK3_A]		= &msm8992_ln_bb_a_clk.hw,
	[RPM_SMD_DIV_CLK2]		= &msm8974_div_clk2.hw,
	[RPM_SMD_DIV_A_CLK2]		= &msm8974_div_a_clk2.hw,
	[RPM_SMD_BB_CLK1_PIN]		= &msm8916_bb_clk1_pin.hw,
	[RPM_SMD_BB_CLK1_A_PIN]		= &msm8916_bb_clk1_a_pin.hw,
	[RPM_SMD_BB_CLK2_PIN]		= &msm8916_bb_clk2_pin.hw,
	[RPM_SMD_BB_CLK2_A_PIN]		= &msm8916_bb_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8953 = {
	.clks = msm8953_clks,
	.num_clks = ARRAY_SIZE(msm8953_clks),
};

/* SM6125 */
DEFINE_CLK_SMD_RPM(sm6125, cnoc_clk, cnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(sm6125, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM_BRANCH(sm6125, qdss_clk, qdss_a_clk,
					QCOM_SMD_RPM_MISC_CLK, 1, 19200000);
DEFINE_CLK_SMD_RPM(sm6125, qup_clk, qup_a_clk, QCOM_SMD_RPM_QUP_CLK, 0);
DEFINE_CLK_SMD_RPM(sm6125, mmnrt_clk, mmnrt_a_clk, QCOM_SMD_RPM_MMAXI_CLK, 0);
DEFINE_CLK_SMD_RPM(sm6125, mmrt_clk, mmrt_a_clk, QCOM_SMD_RPM_MMAXI_CLK, 1);
DEFINE_CLK_SMD_RPM(sm6125, snoc_periph_clk, snoc_periph_a_clk,
						QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(sm6125, snoc_lpass_clk, snoc_lpass_a_clk,
						QCOM_SMD_RPM_BUS_CLK, 5);

static struct clk_hw *sm6125_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_SNOC_CLK] = &sm6125_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &sm6125_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &sm6125_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &sm6125_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_CNOC_CLK] = &sm6125_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &sm6125_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_LN_BB_CLK1] = &msm8916_bb_clk1.hw,
	[RPM_SMD_LN_BB_CLK1_A] = &msm8916_bb_clk1_a.hw,
	[RPM_SMD_LN_BB_CLK2] = &msm8916_bb_clk2.hw,
	[RPM_SMD_LN_BB_CLK2_A] = &msm8916_bb_clk2_a.hw,
	[RPM_SMD_LN_BB_CLK3] = &msm8998_ln_bb_clk3.hw,
	[RPM_SMD_LN_BB_CLK3_A] = &msm8998_ln_bb_clk3_a.hw,
	[RPM_SMD_QUP_CLK] = &sm6125_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &sm6125_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &sm6125_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &sm6125_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &sm6125_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &sm6125_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &sm6125_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &sm6125_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &sm6125_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &sm6125_snoc_lpass_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6125 = {
	.clks = sm6125_clks,
	.num_clks = ARRAY_SIZE(sm6125_clks),
};

/* SM6115 */
static struct clk_hw *sm6115_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_SNOC_CLK] = &sm6125_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &sm6125_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &sm6125_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &sm6125_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &msm8916_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &msm8916_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &msm8916_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &msm8916_rf_clk2_a.hw,
	[RPM_SMD_CNOC_CLK] = &sm6125_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &sm6125_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_QUP_CLK] = &sm6125_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &sm6125_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &sm6125_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &sm6125_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &sm6125_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &sm6125_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &sm6125_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &sm6125_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &sm6125_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &sm6125_snoc_lpass_a_clk.hw,
	[RPM_SMD_RF_CLK1_PIN] = &msm8916_rf_clk1_pin.hw,
	[RPM_SMD_RF_CLK1_A_PIN] = &msm8916_rf_clk1_a_pin.hw,
	[RPM_SMD_RF_CLK2_PIN] = &msm8916_rf_clk2_pin.hw,
	[RPM_SMD_RF_CLK2_A_PIN] = &msm8916_rf_clk2_a_pin.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6115 = {
	.clks = sm6115_clks,
	.num_clks = ARRAY_SIZE(sm6115_clks),
};

/* SM6375 */
DEFINE_CLK_SMD_RPM(sm6375, mmnrt_clk, mmnrt_a_clk, QCOM_SMD_RPM_MMXI_CLK, 0);
DEFINE_CLK_SMD_RPM(sm6375, mmrt_clk, mmrt_a_clk, QCOM_SMD_RPM_MMXI_CLK, 1);
DEFINE_CLK_SMD_RPM(qcm2290, hwkm_clk, hwkm_a_clk, QCOM_SMD_RPM_HWKM_CLK, 0);
DEFINE_CLK_SMD_RPM(qcm2290, pka_clk, pka_a_clk, QCOM_SMD_RPM_PKA_CLK, 0);
DEFINE_CLK_SMD_RPM_BRANCH(sm6375, bimc_freq_log, bimc_freq_log_a, QCOM_SMD_RPM_MISC_CLK, 4, 1);

static struct clk_hw *sm6375_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_SNOC_CLK] = &sm6125_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &sm6125_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &sm6125_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &sm6125_qdss_a_clk.hw,
	[RPM_SMD_CNOC_CLK] = &sm6125_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &sm6125_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_QUP_CLK] = &sm6125_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &sm6125_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &sm6375_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &sm6375_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &sm6375_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &sm6375_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &sm6125_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &sm6125_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &sm6125_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &sm6125_snoc_lpass_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_HWKM_CLK] = &qcm2290_hwkm_clk.hw,
	[RPM_SMD_HWKM_A_CLK] = &qcm2290_hwkm_a_clk.hw,
	[RPM_SMD_PKA_CLK] = &qcm2290_pka_clk.hw,
	[RPM_SMD_PKA_A_CLK] = &qcm2290_pka_a_clk.hw,
	[RPM_SMD_BIMC_FREQ_LOG] = &sm6375_bimc_freq_log.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6375 = {
	.clks = sm6375_clks,
	.num_clks = ARRAY_SIZE(sm6375_clks),
};

/* QCM2290 */
DEFINE_CLK_SMD_RPM_XO_BUFFER(qcm2290, ln_bb_clk2, ln_bb_clk2_a, QCOM_SMD_RPM_CLK_BUF_A, 0x2);
DEFINE_CLK_SMD_RPM_XO_BUFFER(qcm2290, rf_clk3, rf_clk3_a, QCOM_SMD_RPM_CLK_BUF_A, 6);

DEFINE_CLK_SMD_RPM(qcm2290, qpic_clk, qpic_a_clk, QCOM_SMD_RPM_QPIC_CLK, 0);
DEFINE_CLK_SMD_RPM(qcm2290, cpuss_gnoc_clk, cpuss_gnoc_a_clk,
		   QCOM_SMD_RPM_MEM_CLK, 1);
DEFINE_CLK_SMD_RPM(qcm2290, bimc_gpu_clk, bimc_gpu_a_clk,
		   QCOM_SMD_RPM_MEM_CLK, 2);

static struct clk_hw *qcm2290_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &sdm660_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &sdm660_bi_tcxo_a.hw,
	[RPM_SMD_SNOC_CLK] = &sm6125_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &sm6125_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &msm8916_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &msm8916_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &sm6125_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &sm6125_qdss_a_clk.hw,
	[RPM_SMD_LN_BB_CLK2] = &qcm2290_ln_bb_clk2.hw,
	[RPM_SMD_LN_BB_CLK2_A] = &qcm2290_ln_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK3] = &qcm2290_rf_clk3.hw,
	[RPM_SMD_RF_CLK3_A] = &qcm2290_rf_clk3_a.hw,
	[RPM_SMD_CNOC_CLK] = &sm6125_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &sm6125_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &msm8976_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &msm8976_ipa_a_clk.hw,
	[RPM_SMD_QUP_CLK] = &sm6125_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &sm6125_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &sm6125_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &sm6125_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &sm6125_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &sm6125_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &sm6125_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &sm6125_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &sm6125_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &sm6125_snoc_lpass_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &msm8992_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &msm8992_ce1_a_clk.hw,
	[RPM_SMD_QPIC_CLK] = &qcm2290_qpic_clk.hw,
	[RPM_SMD_QPIC_CLK_A] = &qcm2290_qpic_a_clk.hw,
	[RPM_SMD_HWKM_CLK] = &qcm2290_hwkm_clk.hw,
	[RPM_SMD_HWKM_A_CLK] = &qcm2290_hwkm_a_clk.hw,
	[RPM_SMD_PKA_CLK] = &qcm2290_pka_clk.hw,
	[RPM_SMD_PKA_A_CLK] = &qcm2290_pka_a_clk.hw,
	[RPM_SMD_BIMC_GPU_CLK] = &qcm2290_bimc_gpu_clk.hw,
	[RPM_SMD_BIMC_GPU_A_CLK] = &qcm2290_bimc_gpu_a_clk.hw,
	[RPM_SMD_CPUSS_GNOC_CLK] = &qcm2290_cpuss_gnoc_clk.hw,
	[RPM_SMD_CPUSS_GNOC_A_CLK] = &qcm2290_cpuss_gnoc_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_qcm2290 = {
	.clks = qcm2290_clks,
	.num_clks = ARRAY_SIZE(qcm2290_clks),
};

/* holi */
DEFINE_CLK_SMD_RPM_BRANCH(holi, bi_tcxo, bi_tcxo_ao,
					QCOM_SMD_RPM_MISC_CLK, 0, 19200000);
DEFINE_CLK_SMD_RPM(holi, cnoc_clk, cnoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 1);
DEFINE_CLK_SMD_RPM(holi, bimc_clk, bimc_a_clk, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, snoc_clk, snoc_a_clk, QCOM_SMD_RPM_BUS_CLK, 2);
DEFINE_CLK_SMD_RPM_BRANCH(holi, qdss_clk, qdss_a_clk,
					QCOM_SMD_RPM_MISC_CLK, 1, 19200000);
DEFINE_CLK_SMD_RPM(holi, ce1_clk, ce1_a_clk, QCOM_SMD_RPM_CE_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, ipa_clk, ipa_a_clk, QCOM_SMD_RPM_IPA_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, qup_clk, qup_a_clk, QCOM_SMD_RPM_QUP_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, mmnrt_clk, mmnrt_a_clk, QCOM_SMD_RPM_MMXI_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, mmrt_clk, mmrt_a_clk, QCOM_SMD_RPM_MMXI_CLK, 1);
DEFINE_CLK_SMD_RPM(holi, snoc_periph_clk, snoc_periph_a_clk,
						QCOM_SMD_RPM_BUS_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, snoc_lpass_clk, snoc_lpass_a_clk,
						QCOM_SMD_RPM_BUS_CLK, 5);
DEFINE_CLK_SMD_RPM(holi, hwkm_clk, hwkm_a_clk, QCOM_SMD_RPM_HWKM_CLK, 0);
DEFINE_CLK_SMD_RPM(holi, pka_clk, pka_a_clk, QCOM_SMD_RPM_PKA_CLK, 0);
DEFINE_CLK_SMD_RPM_BRANCH(holi, bimc_freq_log, bimc_freq_log_a,
						QCOM_SMD_RPM_MISC_CLK, 4, 1);

/* SMD_XO_BUFFER */
DEFINE_CLK_SMD_RPM_XO_BUFFER(holi, ln_bb_clk2, ln_bb_clk2_a,
						QCOM_SMD_RPM_CLK_BUF_G, 8);
DEFINE_CLK_SMD_RPM_XO_BUFFER(holi, rf_clk5, rf_clk5_a,
						QCOM_SMD_RPM_CLK_BUF_G, 6);

static struct clk_hw *holi_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &holi_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &holi_bi_tcxo_ao.hw,
	[RPM_SMD_SNOC_CLK] = &holi_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &holi_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &holi_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &holi_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &holi_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &holi_qdss_a_clk.hw,
	[RPM_SMD_LN_BB_CLK2] = &holi_ln_bb_clk2.hw,
	[RPM_SMD_LN_BB_CLK2_A] = &holi_ln_bb_clk2_a.hw,
	[RPM_SMD_RF_CLK5] = &holi_rf_clk5.hw,
	[RPM_SMD_RF_CLK5_A] = &holi_rf_clk5_a.hw,
	[RPM_SMD_CNOC_CLK] = &holi_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &holi_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &holi_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &holi_ipa_a_clk.hw,
	[RPM_SMD_QUP_CLK] = &holi_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &holi_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &holi_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &holi_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &holi_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &holi_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &holi_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &holi_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &holi_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &holi_snoc_lpass_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &holi_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &holi_ce1_a_clk.hw,
	[RPM_SMD_HWKM_CLK] = &holi_hwkm_clk.hw,
	[RPM_SMD_HWKM_A_CLK] = &holi_hwkm_a_clk.hw,
	[RPM_SMD_PKA_CLK] = &holi_pka_clk.hw,
	[RPM_SMD_PKA_A_CLK] = &holi_pka_a_clk.hw,
	[RPM_SMD_BIMC_FREQ_LOG] = &holi_bimc_freq_log.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_holi = {
	.clks = holi_clks,
	.num_clks = ARRAY_SIZE(holi_clks),
};

/* Pitti */
DEFINE_CLK_SMD_RPM_XO_BUFFER(pitti, rf_clk1, rf_clk1_a, QCOM_SMD_RPM_CLK_BUF_A, 4);
DEFINE_CLK_SMD_RPM_XO_BUFFER(pitti, rf_clk2, rf_clk2_a, QCOM_SMD_RPM_CLK_BUF_A, 5);
DEFINE_CLK_SMD_RPM_XO_BUFFER(pitti, rf_clk3, rf_clk3_a, QCOM_SMD_RPM_CLK_BUF_G, 6);

static struct clk_hw *pitti_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &holi_bi_tcxo.hw,
	[RPM_SMD_XO_A_CLK_SRC] = &holi_bi_tcxo_ao.hw,
	[RPM_SMD_SNOC_CLK] = &holi_snoc_clk.hw,
	[RPM_SMD_SNOC_A_CLK] = &holi_snoc_a_clk.hw,
	[RPM_SMD_BIMC_CLK] = &holi_bimc_clk.hw,
	[RPM_SMD_BIMC_A_CLK] = &holi_bimc_a_clk.hw,
	[RPM_SMD_QDSS_CLK] = &holi_qdss_clk.hw,
	[RPM_SMD_QDSS_A_CLK] = &holi_qdss_a_clk.hw,
	[RPM_SMD_RF_CLK1] = &pitti_rf_clk1.hw,
	[RPM_SMD_RF_CLK1_A] = &pitti_rf_clk1_a.hw,
	[RPM_SMD_RF_CLK2] = &pitti_rf_clk2.hw,
	[RPM_SMD_RF_CLK2_A] = &pitti_rf_clk2_a.hw,
	[RPM_SMD_RF_CLK3] = &pitti_rf_clk3.hw,
	[RPM_SMD_RF_CLK3_A] = &pitti_rf_clk3_a.hw,
	[RPM_SMD_CNOC_CLK] = &holi_cnoc_clk.hw,
	[RPM_SMD_CNOC_A_CLK] = &holi_cnoc_a_clk.hw,
	[RPM_SMD_IPA_CLK] = &holi_ipa_clk.hw,
	[RPM_SMD_IPA_A_CLK] = &holi_ipa_a_clk.hw,
	[RPM_SMD_QUP_CLK] = &holi_qup_clk.hw,
	[RPM_SMD_QUP_A_CLK] = &holi_qup_a_clk.hw,
	[RPM_SMD_MMRT_CLK] = &holi_mmrt_clk.hw,
	[RPM_SMD_MMRT_A_CLK] = &holi_mmrt_a_clk.hw,
	[RPM_SMD_MMNRT_CLK] = &holi_mmnrt_clk.hw,
	[RPM_SMD_MMNRT_A_CLK] = &holi_mmnrt_a_clk.hw,
	[RPM_SMD_SNOC_PERIPH_CLK] = &holi_snoc_periph_clk.hw,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &holi_snoc_periph_a_clk.hw,
	[RPM_SMD_SNOC_LPASS_CLK] = &holi_snoc_lpass_clk.hw,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &holi_snoc_lpass_a_clk.hw,
	[RPM_SMD_CE1_CLK] = &holi_ce1_clk.hw,
	[RPM_SMD_CE1_A_CLK] = &holi_ce1_a_clk.hw,
	[RPM_SMD_BIMC_GPU_CLK] = &qcm2290_bimc_gpu_clk.hw,
	[RPM_SMD_BIMC_GPU_A_CLK] = &qcm2290_bimc_gpu_a_clk.hw,
	[RPM_SMD_CPUSS_GNOC_CLK] = &qcm2290_cpuss_gnoc_clk.hw,
	[RPM_SMD_CPUSS_GNOC_A_CLK] = &qcm2290_cpuss_gnoc_a_clk.hw,
};

static const struct rpm_smd_clk_desc rpm_clk_pitti = {
	.clks = pitti_clks,
	.num_clks = ARRAY_SIZE(pitti_clks),
};


static const struct of_device_id rpm_smd_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-mdm9607", .data = &rpm_clk_mdm9607 },
	{ .compatible = "qcom,rpmcc-msm8226", .data = &rpm_clk_msm8974 },
	{ .compatible = "qcom,rpmcc-msm8909", .data = &rpm_clk_msm8909 },
	{ .compatible = "qcom,rpmcc-msm8916", .data = &rpm_clk_msm8916 },
	{ .compatible = "qcom,rpmcc-msm8936", .data = &rpm_clk_msm8936 },
	{ .compatible = "qcom,rpmcc-msm8953", .data = &rpm_clk_msm8953 },
	{ .compatible = "qcom,rpmcc-msm8974", .data = &rpm_clk_msm8974 },
	{ .compatible = "qcom,rpmcc-msm8976", .data = &rpm_clk_msm8976 },
	{ .compatible = "qcom,rpmcc-msm8992", .data = &rpm_clk_msm8992 },
	{ .compatible = "qcom,rpmcc-msm8994", .data = &rpm_clk_msm8994 },
	{ .compatible = "qcom,rpmcc-msm8996", .data = &rpm_clk_msm8996 },
	{ .compatible = "qcom,rpmcc-msm8998", .data = &rpm_clk_msm8998 },
	{ .compatible = "qcom,rpmcc-qcm2290", .data = &rpm_clk_qcm2290 },
	{ .compatible = "qcom,rpmcc-qcs404",  .data = &rpm_clk_qcs404  },
	{ .compatible = "qcom,rpmcc-sdm660",  .data = &rpm_clk_sdm660  },
	{ .compatible = "qcom,rpmcc-sm6115",  .data = &rpm_clk_sm6115  },
	{ .compatible = "qcom,rpmcc-sm6125",  .data = &rpm_clk_sm6125  },
	{ .compatible = "qcom,rpmcc-sm6375",  .data = &rpm_clk_sm6375  },
	{ .compatible = "qcom,rpmcc-holi", .data = &rpm_clk_holi},
	{ .compatible = "qcom,rpmcc-pitti", .data = &rpm_clk_pitti},
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_smd_clk_match_table);

static int smd_rpm_clk_panic_callback(struct notifier_block *nfb,
					unsigned long event, void *unused)
{
	struct clk_hw *hw = &holi_bimc_freq_log.hw;
	struct clk_smd_rpm *r = to_clk_smd_rpm(hw);
	uint32_t rate = 1;
	void *ret;

	struct msm_rpm_kvp req = {
		.key = r->rpm_key,
		.data = (void *)&rate,
		.length = sizeof(rate),
	};

	ret = msm_rpm_send_message_noack(QCOM_SMD_RPM_ACTIVE_STATE,
		 r->rpm_res_type, r->rpm_clk_id, &req, 1);

	if (IS_ERR(ret))
		pr_err("BIMC Stop logging request failed\n");

	return NOTIFY_OK;
}

static struct notifier_block smd_rpm_clk_panic_notifier = {
	.notifier_call = smd_rpm_clk_panic_callback,
	.priority = 1,
};

static struct clk_hw *qcom_smdrpm_clk_hw_get(struct of_phandle_args *clkspec,
						void *data)
{
	struct rpm_smd_clk_desc *rpmcc = data;
	struct clk_smd_rpm *c;
	unsigned int idx = clkspec->args[0];

	if (idx >= rpmcc->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!rpmcc->clks[idx])
		return ERR_PTR(-ENOENT);

	c = to_clk_smd_rpm(rpmcc->clks[idx]);
	if (!c->rpm_res_type)
		return ERR_PTR(-ENODEV);

	return rpmcc->clks[idx];
}

static int rpm_smd_clk_probe(struct platform_device *pdev)
{
	struct clk_hw **hw_clks;
	const struct rpm_smd_clk_desc *desc;
	int ret, i, is_holi, is_pitti, is_mdm9607, hw_clk_handoff = false;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	is_holi = of_device_is_compatible(pdev->dev.of_node,
						"qcom,rpmcc-holi");

	is_pitti = of_device_is_compatible(pdev->dev.of_node,
						"qcom,rpmcc-pitti");

	is_mdm9607 = of_device_is_compatible(pdev->dev.of_node,
						"qcom,rpmcc-mdm9607");

	if (is_holi || is_pitti || is_mdm9607) {
		ret = clk_vote_bimc(&holi_bimc_clk.hw, INT_MAX);
		if (ret < 0)
			return ret;
	}

	hw_clks = desc->clks;

	hw_clk_handoff = of_property_read_bool(pdev->dev.of_node,
						"qcom,hw-clk-handoff");
	if (hw_clk_handoff) {
		for (i = 0; i < desc->num_clks; i++) {
			if (!hw_clks[i])
				continue;

			ret = clk_smd_rpm_handoff(hw_clks[i]);
			if (ret)
				goto err;
		}
	}

	ret = clk_smd_rpm_enable_scaling();
	if (ret)
		goto err;

	for (i = 0; i < desc->num_clks; i++) {
		const char *name;

		if (!hw_clks[i])
			continue;

		name =  hw_clks[i]->init->name;
		ret = devm_clk_hw_register(&pdev->dev, hw_clks[i]);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register %s\n", name);
			return ret;
		}

		ret = clk_hw_debug_register(&pdev->dev, hw_clks[i]);
		if (ret)
			dev_warn(&pdev->dev, "Failed to add %s to debug list\n",
									name);
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, qcom_smdrpm_clk_hw_get,
					  (void *)desc);
	if (ret)
		goto err;

	if (is_holi || is_pitti) {
		/*
		 * Keep an active vote on CXO in case no other driver
		 * votes for it.
		 */
		clk_prepare_enable(holi_bi_tcxo_ao.hw.clk);

		/* Hold an active set vote for the cnoc_keepalive_a_clk */
		clk_set_rate(holi_cnoc_a_clk.hw.clk, 19200000);
		clk_prepare_enable(holi_cnoc_a_clk.hw.clk);

		/* Hold an active set vote for the snoc_keepalive_a_clk */
		clk_set_rate(holi_snoc_a_clk.hw.clk, 19200000);
		clk_prepare_enable(holi_snoc_a_clk.hw.clk);

		/* Hold an active set vote for qup clock */
		clk_prepare_enable(holi_qup_a_clk.hw.clk);
		clk_set_rate(holi_qup_a_clk.hw.clk, 19200000);
	}

	if (is_mdm9607) {
		/*
		 * Keep an active vote on CXO in case no other driver
		 * votes for it.
		 */
		clk_prepare_enable(sdm660_bi_tcxo_a.hw.clk);

		/* Hold an active set vote for the pcnoc_keepalive_a_clk */
		clk_prepare_enable(msm8916_pcnoc_a_clk.hw.clk);
		clk_set_rate(msm8916_pcnoc_a_clk.hw.clk, 19200000);

	}

	if (of_property_read_bool(pdev->dev.of_node, "qcom,bimc-log-stop"))
		atomic_notifier_chain_register(&panic_notifier_list,
						&smd_rpm_clk_panic_notifier);

	dev_info(&pdev->dev, "Registered RPM clocks\n");

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
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:qcom-clk-smd-rpm");
