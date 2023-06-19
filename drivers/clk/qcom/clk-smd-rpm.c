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

#define __DEFINE_CLK_SMD_RPM_PREFIX(_prefix, _name, _active,		      \
				    type, r_id, key)			      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_active;	      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_name = {	      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_key = (key),					      \
		.peer = &clk_smd_rpm_##_prefix##_active,		      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_ops,			      \
			.name = #_name,					      \
			.parent_data =  &(const struct clk_parent_data){      \
					.fw_name = "xo",		      \
					.name = "xo_board",		      \
			},						      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_active = {	      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.active_only = true,					      \
		.rpm_key = (key),					      \
		.peer = &clk_smd_rpm_##_prefix##_name,			      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_ops,			      \
			.name = #_active,				      \
			.parent_data =  &(const struct clk_parent_data){      \
					.fw_name = "xo",		      \
					.name = "xo_board",		      \
			},						      \
			.num_parents = 1,				      \
		},							      \
	}

#define __DEFINE_CLK_SMD_RPM(_name, _active, type, r_id, key)		      \
	__DEFINE_CLK_SMD_RPM_PREFIX(/* empty */, _name, _active,	      \
				    type, r_id, key)

#define __DEFINE_CLK_SMD_RPM_BRANCH_PREFIX(_prefix, _name, _active,\
					   type, r_id, r, key, ao_flags)      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_active;	      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_name = {	      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.rpm_key = (key),					      \
		.branch = true,						      \
		.peer = &clk_smd_rpm_##_prefix##_active,		      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_name,					      \
			.parent_data =  &(const struct clk_parent_data){      \
					.fw_name = "xo",		      \
					.name = "xo_board",		      \
			},						      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_smd_rpm clk_smd_rpm_##_prefix##_active = {	      \
		.rpm_res_type = (type),					      \
		.rpm_clk_id = (r_id),					      \
		.active_only = true,					      \
		.rpm_key = (key),					      \
		.branch = true,						      \
		.peer = &clk_smd_rpm_##_prefix##_name,			      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_smd_rpm_branch_ops,			      \
			.name = #_active,				      \
			.parent_data =  &(const struct clk_parent_data){      \
					.fw_name = "xo",		      \
					.name = "xo_board",		      \
			},						      \
			.num_parents = 1,				      \
			.flags = (ao_flags),				      \
		},							      \
	}

#define __DEFINE_CLK_SMD_RPM_BRANCH(_name, _active, type, r_id, r, key)	      \
		__DEFINE_CLK_SMD_RPM_BRANCH_PREFIX(/* empty */,		      \
		_name, _active, type, r_id, r, key, 0)

#define DEFINE_CLK_SMD_RPM(_name, type, r_id)				      \
		__DEFINE_CLK_SMD_RPM(_name##_clk, _name##_a_clk,	      \
		type, r_id, QCOM_RPM_SMD_KEY_RATE)

#define DEFINE_CLK_SMD_RPM_BUS(_name, r_id)				      \
		__DEFINE_CLK_SMD_RPM_PREFIX(bus_##r_id##_,		      \
		_name##_clk, _name##_a_clk, QCOM_SMD_RPM_BUS_CLK, r_id,	      \
		QCOM_RPM_SMD_KEY_RATE)

#define DEFINE_CLK_SMD_RPM_CLK_SRC(_name, type, r_id)			      \
		__DEFINE_CLK_SMD_RPM(					      \
		_name##_clk_src, _name##_a_clk_src,			      \
		type, r_id, QCOM_RPM_SMD_KEY_RATE)

#define DEFINE_CLK_SMD_RPM_BRANCH(_name, type, r_id, r)			      \
		__DEFINE_CLK_SMD_RPM_BRANCH_PREFIX(branch_,		      \
		_name##_clk, _name##_a_clk,				      \
		type, r_id, r, QCOM_RPM_SMD_KEY_ENABLE, 0)

#define DEFINE_CLK_SMD_RPM_BRANCH_A(_name, type, r_id, r, ao_flags)	      \
		__DEFINE_CLK_SMD_RPM_BRANCH_PREFIX(branch_,		      \
		_name, _name##_a, type,					      \
		r_id, r, QCOM_RPM_SMD_KEY_ENABLE, ao_flags)

#define DEFINE_CLK_SMD_RPM_QDSS(_name, type, r_id)			      \
		__DEFINE_CLK_SMD_RPM(_name##_clk, _name##_a_clk,	      \
		type, r_id, QCOM_RPM_SMD_KEY_STATE)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER(_name, r_id, r)			      \
		__DEFINE_CLK_SMD_RPM_BRANCH(_name, _name##_a,		      \
		QCOM_SMD_RPM_CLK_BUF_A, r_id, r,			      \
		QCOM_RPM_KEY_SOFTWARE_ENABLE)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER_PREFIX(_prefix, _name, r_id, r)	      \
		__DEFINE_CLK_SMD_RPM_BRANCH_PREFIX(_prefix,		      \
		_name, _name##_a,					      \
		QCOM_SMD_RPM_CLK_BUF_A, r_id, r,			      \
		QCOM_RPM_KEY_SOFTWARE_ENABLE, 0)

#define DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(_name, r_id, r)		      \
		DEFINE_CLK_SMD_RPM_XO_BUFFER(_name, r_id, r);		      \
		__DEFINE_CLK_SMD_RPM_BRANCH(_name##_pin, _name##_a##_pin,     \
		QCOM_SMD_RPM_CLK_BUF_A, r_id, r,			      \
		QCOM_RPM_KEY_PIN_CTRL_CLK_BUFFER_ENABLE_KEY)

#define to_clk_smd_rpm(_hw) container_of(_hw, struct clk_smd_rpm, hw)

static struct qcom_smd_rpm *rpmcc_smd_rpm;

struct clk_smd_rpm {
	const int rpm_res_type;
	const int rpm_key;
	const int rpm_clk_id;
	const bool active_only;
	bool enabled;
	bool branch;
	struct clk_smd_rpm *peer;
	struct clk_hw hw;
	unsigned long rate;
};

struct rpm_smd_clk_desc {
	struct clk_smd_rpm **clks;
	size_t num_clks;
	bool scaling_before_handover;
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

	ret = qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				 r->rpm_res_type, r->rpm_clk_id, &req,
				 sizeof(req));
	if (ret)
		return ret;
	ret = qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_SLEEP_STATE,
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

	return qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_ACTIVE_STATE,
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

	return qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_SLEEP_STATE,
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

static int clk_smd_rpm_enable_scaling(void)
{
	int ret;
	struct clk_smd_rpm_req req = {
		.key = cpu_to_le32(QCOM_RPM_SMD_KEY_ENABLE),
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(1),
	};

	ret = qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_SLEEP_STATE,
				 QCOM_SMD_RPM_MISC_CLK,
				 QCOM_RPM_SCALING_ENABLE_ID, &req, sizeof(req));
	if (ret) {
		pr_err("RPM clock scaling (sleep set) not enabled!\n");
		return ret;
	}

	ret = qcom_rpm_smd_write(rpmcc_smd_rpm, QCOM_SMD_RPM_ACTIVE_STATE,
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
	.recalc_rate	= clk_smd_rpm_recalc_rate,
};

/* Disabling BI_TCXO_AO could gate the root clock source of the entire system. */
DEFINE_CLK_SMD_RPM_BRANCH_A(bi_tcxo, QCOM_SMD_RPM_MISC_CLK, 0, 19200000, CLK_IS_CRITICAL);
DEFINE_CLK_SMD_RPM_BRANCH(qdss, QCOM_SMD_RPM_MISC_CLK, 1, 19200000);
DEFINE_CLK_SMD_RPM_QDSS(qdss, QCOM_SMD_RPM_MISC_CLK, 1);
DEFINE_CLK_SMD_RPM_BRANCH_A(bimc_freq_log, QCOM_SMD_RPM_MISC_CLK, 4, 1, 0);

DEFINE_CLK_SMD_RPM_BRANCH(mss_cfg_ahb, QCOM_SMD_RPM_MCFG_CLK, 0, 19200000);

DEFINE_CLK_SMD_RPM_BRANCH(aggre1_noc, QCOM_SMD_RPM_AGGR_CLK, 1, 1000);
DEFINE_CLK_SMD_RPM_BRANCH(aggre2_noc, QCOM_SMD_RPM_AGGR_CLK, 2, 1000);
DEFINE_CLK_SMD_RPM(aggre1_noc, QCOM_SMD_RPM_AGGR_CLK, 1);
DEFINE_CLK_SMD_RPM(aggre2_noc, QCOM_SMD_RPM_AGGR_CLK, 2);

DEFINE_CLK_SMD_RPM_BUS(pcnoc, 0);
DEFINE_CLK_SMD_RPM_BUS(snoc, 1);
DEFINE_CLK_SMD_RPM_BUS(sysmmnoc, 2);
DEFINE_CLK_SMD_RPM_BUS(cnoc, 2);
DEFINE_CLK_SMD_RPM_BUS(mmssnoc_ahb, 3);
DEFINE_CLK_SMD_RPM_BUS(snoc_periph, 0);
DEFINE_CLK_SMD_RPM_BUS(cnoc, 1);
DEFINE_CLK_SMD_RPM_BUS(snoc, 2);
DEFINE_CLK_SMD_RPM_BUS(snoc_lpass, 5);

DEFINE_CLK_SMD_RPM(bimc, QCOM_SMD_RPM_MEM_CLK, 0);
DEFINE_CLK_SMD_RPM(cpuss_gnoc, QCOM_SMD_RPM_MEM_CLK, 1);
DEFINE_CLK_SMD_RPM_CLK_SRC(gfx3d, QCOM_SMD_RPM_MEM_CLK, 1);
DEFINE_CLK_SMD_RPM(ocmemgx, QCOM_SMD_RPM_MEM_CLK, 2);
DEFINE_CLK_SMD_RPM(bimc_gpu, QCOM_SMD_RPM_MEM_CLK, 2);

DEFINE_CLK_SMD_RPM(ce1, QCOM_SMD_RPM_CE_CLK, 0);
DEFINE_CLK_SMD_RPM(ce2, QCOM_SMD_RPM_CE_CLK, 1);
DEFINE_CLK_SMD_RPM(ce3, QCOM_SMD_RPM_CE_CLK, 2);

DEFINE_CLK_SMD_RPM(ipa, QCOM_SMD_RPM_IPA_CLK, 0);

DEFINE_CLK_SMD_RPM(hwkm, QCOM_SMD_RPM_HWKM_CLK, 0);

DEFINE_CLK_SMD_RPM(mmssnoc_axi_rpm, QCOM_SMD_RPM_MMAXI_CLK, 0);
DEFINE_CLK_SMD_RPM(mmnrt, QCOM_SMD_RPM_MMAXI_CLK, 0);
DEFINE_CLK_SMD_RPM(mmrt, QCOM_SMD_RPM_MMAXI_CLK, 1);

DEFINE_CLK_SMD_RPM(pka, QCOM_SMD_RPM_PKA_CLK, 0);

DEFINE_CLK_SMD_RPM(qpic, QCOM_SMD_RPM_QPIC_CLK, 0);

DEFINE_CLK_SMD_RPM(qup, QCOM_SMD_RPM_QUP_CLK, 0);

DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(bb_clk1, 1, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(bb_clk2, 2, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(ln_bb_clk1, 1, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(ln_bb_clk2, 2, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(ln_bb_clk3, 3, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(rf_clk1, 4, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(rf_clk2, 5, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(rf_clk3, 6, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(ln_bb_clk, 8, 19200000);

DEFINE_CLK_SMD_RPM_XO_BUFFER_PREFIX(38m4_, rf_clk3, 6, 38400000);

DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(cxo_d0, 1, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(cxo_d1, 2, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(cxo_a0, 4, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(cxo_a1, 5, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER_PINCTRL(cxo_a2, 6, 19200000);

DEFINE_CLK_SMD_RPM_XO_BUFFER(diff_clk, 7, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER(div_clk1, 11, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER(div_clk2, 12, 19200000);
DEFINE_CLK_SMD_RPM_XO_BUFFER(div_clk3, 13, 19200000);

static struct clk_smd_rpm *msm8909_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QPIC_CLK]		= &clk_smd_rpm_qpic_clk,
	[RPM_SMD_QPIC_CLK_A]		= &clk_smd_rpm_qpic_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK1]		= &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A]		= &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2]		= &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN]		= &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN]		= &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN]		= &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN]		= &clk_smd_rpm_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8909 = {
	.clks = msm8909_clks,
	.num_clks = ARRAY_SIZE(msm8909_clks),
};

static struct clk_smd_rpm *msm8916_clks[] = {
	[RPM_SMD_PCNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK1]		= &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A]		= &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2]		= &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN]		= &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN]		= &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN]		= &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN]		= &clk_smd_rpm_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8916 = {
	.clks = msm8916_clks,
	.num_clks = ARRAY_SIZE(msm8916_clks),
};

static struct clk_smd_rpm *msm8917_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_BIMC_GPU_CLK]		= &clk_smd_rpm_bimc_gpu_clk,
	[RPM_SMD_BIMC_GPU_A_CLK]	= &clk_smd_rpm_bimc_gpu_a_clk,
	[RPM_SMD_SYSMMNOC_CLK]		= &clk_smd_rpm_bus_2_sysmmnoc_clk,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &clk_smd_rpm_bus_2_sysmmnoc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK2]		= &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_DIV_CLK2]		= &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2]		= &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &clk_smd_rpm_bb_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8917 = {
	.clks = msm8917_clks,
	.num_clks = ARRAY_SIZE(msm8917_clks),
};

static struct clk_smd_rpm *msm8936_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PCNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_SYSMMNOC_CLK]		= &clk_smd_rpm_bus_2_sysmmnoc_clk,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &clk_smd_rpm_bus_2_sysmmnoc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK1]		= &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A]		= &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2]		= &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN]		= &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN]		= &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN]		= &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN]		= &clk_smd_rpm_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8936 = {
		.clks = msm8936_clks,
		.num_clks = ARRAY_SIZE(msm8936_clks),
};

static struct clk_smd_rpm *msm8974_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_CNOC_CLK]		= &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK]		= &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_MMSSNOC_AHB_CLK]	= &clk_smd_rpm_bus_3_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK]	= &clk_smd_rpm_bus_3_mmssnoc_ahb_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_GFX3D_CLK_SRC]		= &clk_smd_rpm_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC]	= &clk_smd_rpm_gfx3d_a_clk_src,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_OCMEMGX_CLK]		= &clk_smd_rpm_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK]		= &clk_smd_rpm_ocmemgx_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_CXO_D0]		= &clk_smd_rpm_cxo_d0,
	[RPM_SMD_CXO_D0_A]		= &clk_smd_rpm_cxo_d0_a,
	[RPM_SMD_CXO_D1]		= &clk_smd_rpm_cxo_d1,
	[RPM_SMD_CXO_D1_A]		= &clk_smd_rpm_cxo_d1_a,
	[RPM_SMD_CXO_A0]		= &clk_smd_rpm_cxo_a0,
	[RPM_SMD_CXO_A0_A]		= &clk_smd_rpm_cxo_a0_a,
	[RPM_SMD_CXO_A1]		= &clk_smd_rpm_cxo_a1,
	[RPM_SMD_CXO_A1_A]		= &clk_smd_rpm_cxo_a1_a,
	[RPM_SMD_CXO_A2]		= &clk_smd_rpm_cxo_a2,
	[RPM_SMD_CXO_A2_A]		= &clk_smd_rpm_cxo_a2_a,
	[RPM_SMD_DIFF_CLK]		= &clk_smd_rpm_diff_clk,
	[RPM_SMD_DIFF_A_CLK]		= &clk_smd_rpm_diff_clk_a,
	[RPM_SMD_DIV_CLK1]		= &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1]		= &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_DIV_CLK2]		= &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2]		= &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_CXO_D0_PIN]		= &clk_smd_rpm_cxo_d0_pin,
	[RPM_SMD_CXO_D0_A_PIN]		= &clk_smd_rpm_cxo_d0_a_pin,
	[RPM_SMD_CXO_D1_PIN]		= &clk_smd_rpm_cxo_d1_pin,
	[RPM_SMD_CXO_D1_A_PIN]		= &clk_smd_rpm_cxo_d1_a_pin,
	[RPM_SMD_CXO_A0_PIN]		= &clk_smd_rpm_cxo_a0_pin,
	[RPM_SMD_CXO_A0_A_PIN]		= &clk_smd_rpm_cxo_a0_a_pin,
	[RPM_SMD_CXO_A1_PIN]		= &clk_smd_rpm_cxo_a1_pin,
	[RPM_SMD_CXO_A1_A_PIN]		= &clk_smd_rpm_cxo_a1_a_pin,
	[RPM_SMD_CXO_A2_PIN]		= &clk_smd_rpm_cxo_a2_pin,
	[RPM_SMD_CXO_A2_A_PIN]		= &clk_smd_rpm_cxo_a2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8974 = {
	.clks = msm8974_clks,
	.num_clks = ARRAY_SIZE(msm8974_clks),
	.scaling_before_handover = true,
};

static struct clk_smd_rpm *msm8976_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PCNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_SYSMMNOC_CLK]	= &clk_smd_rpm_bus_2_sysmmnoc_clk,
	[RPM_SMD_SYSMMNOC_A_CLK] = &clk_smd_rpm_bus_2_sysmmnoc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1] = &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2] = &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_BB_CLK1_PIN] = &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN] = &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_DIV_CLK2] = &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8976 = {
	.clks = msm8976_clks,
	.num_clks = ARRAY_SIZE(msm8976_clks),
};

static struct clk_smd_rpm *msm8992_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_OCMEMGX_CLK] = &clk_smd_rpm_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK] = &clk_smd_rpm_ocmemgx_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_GFX3D_CLK_SRC] = &clk_smd_rpm_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &clk_smd_rpm_gfx3d_a_clk_src,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BB_CLK1] = &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK1_PIN] = &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2] = &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_BB_CLK2_PIN] = &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_DIV_CLK1] = &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_DIV_CLK2] = &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_DIV_CLK3] = &clk_smd_rpm_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &clk_smd_rpm_div_clk3_a,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK] = &clk_smd_rpm_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &clk_smd_rpm_ln_bb_clk_a,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &clk_smd_rpm_bus_3_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &clk_smd_rpm_bus_3_mmssnoc_ahb_a_clk,
	[RPM_SMD_MSS_CFG_AHB_CLK] = &clk_smd_rpm_branch_mss_cfg_ahb_clk,
	[RPM_SMD_MSS_CFG_AHB_A_CLK] = &clk_smd_rpm_branch_mss_cfg_ahb_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &clk_smd_rpm_rf_clk2_a_pin,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_CE2_CLK] = &clk_smd_rpm_ce2_clk,
	[RPM_SMD_CE2_A_CLK] = &clk_smd_rpm_ce2_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8992 = {
	.clks = msm8992_clks,
	.num_clks = ARRAY_SIZE(msm8992_clks),
};

static struct clk_smd_rpm *msm8994_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_OCMEMGX_CLK] = &clk_smd_rpm_ocmemgx_clk,
	[RPM_SMD_OCMEMGX_A_CLK] = &clk_smd_rpm_ocmemgx_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_GFX3D_CLK_SRC] = &clk_smd_rpm_gfx3d_clk_src,
	[RPM_SMD_GFX3D_A_CLK_SRC] = &clk_smd_rpm_gfx3d_a_clk_src,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BB_CLK1] = &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK1_PIN] = &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2] = &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_BB_CLK2_PIN] = &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_DIV_CLK1] = &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_DIV_CLK2] = &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_DIV_CLK3] = &clk_smd_rpm_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &clk_smd_rpm_div_clk3_a,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK] = &clk_smd_rpm_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &clk_smd_rpm_ln_bb_clk_a,
	[RPM_SMD_MMSSNOC_AHB_CLK] = &clk_smd_rpm_bus_3_mmssnoc_ahb_clk,
	[RPM_SMD_MMSSNOC_AHB_A_CLK] = &clk_smd_rpm_bus_3_mmssnoc_ahb_a_clk,
	[RPM_SMD_MSS_CFG_AHB_CLK] = &clk_smd_rpm_branch_mss_cfg_ahb_clk,
	[RPM_SMD_MSS_CFG_AHB_A_CLK] = &clk_smd_rpm_branch_mss_cfg_ahb_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &clk_smd_rpm_rf_clk2_a_pin,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_CE2_CLK] = &clk_smd_rpm_ce2_clk,
	[RPM_SMD_CE2_A_CLK] = &clk_smd_rpm_ce2_a_clk,
	[RPM_SMD_CE3_CLK] = &clk_smd_rpm_ce3_clk,
	[RPM_SMD_CE3_A_CLK] = &clk_smd_rpm_ce3_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8994 = {
	.clks = msm8994_clks,
	.num_clks = ARRAY_SIZE(msm8994_clks),
};

static struct clk_smd_rpm *msm8996_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PCNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_MMAXI_CLK] = &clk_smd_rpm_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMAXI_A_CLK] = &clk_smd_rpm_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_AGGR1_NOC_CLK] = &clk_smd_rpm_branch_aggre1_noc_clk,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &clk_smd_rpm_branch_aggre1_noc_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &clk_smd_rpm_branch_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &clk_smd_rpm_branch_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1] = &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A] = &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2] = &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A] = &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_LN_BB_CLK] = &clk_smd_rpm_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &clk_smd_rpm_ln_bb_clk_a,
	[RPM_SMD_DIV_CLK1] = &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_DIV_CLK2] = &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_DIV_CLK3] = &clk_smd_rpm_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &clk_smd_rpm_div_clk3_a,
	[RPM_SMD_BB_CLK1_PIN] = &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN] = &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN] = &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN] = &clk_smd_rpm_bb_clk2_a_pin,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &clk_smd_rpm_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8996 = {
	.clks = msm8996_clks,
	.num_clks = ARRAY_SIZE(msm8996_clks),
};

static struct clk_smd_rpm *qcs404_clks[] = {
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_PNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_BIMC_GPU_CLK] = &clk_smd_rpm_bimc_gpu_clk,
	[RPM_SMD_BIMC_GPU_A_CLK] = &clk_smd_rpm_bimc_gpu_a_clk,
	[RPM_SMD_QPIC_CLK] = &clk_smd_rpm_qpic_clk,
	[RPM_SMD_QPIC_CLK_A] = &clk_smd_rpm_qpic_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_LN_BB_CLK] = &clk_smd_rpm_ln_bb_clk,
	[RPM_SMD_LN_BB_A_CLK] = &clk_smd_rpm_ln_bb_clk_a,
	[RPM_SMD_LN_BB_CLK_PIN] = &clk_smd_rpm_ln_bb_clk_pin,
	[RPM_SMD_LN_BB_A_CLK_PIN] = &clk_smd_rpm_ln_bb_clk_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_qcs404 = {
	.clks = qcs404_clks,
	.num_clks = ARRAY_SIZE(qcs404_clks),
};

static struct clk_smd_rpm *msm8998_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_PCNOC_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_DIV_CLK1] = &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_DIV_CLK2] = &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2] = &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_DIV_CLK3] = &clk_smd_rpm_div_clk3,
	[RPM_SMD_DIV_A_CLK3] = &clk_smd_rpm_div_clk3_a,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_LN_BB_CLK1] = &clk_smd_rpm_ln_bb_clk1,
	[RPM_SMD_LN_BB_CLK1_A] = &clk_smd_rpm_ln_bb_clk1_a,
	[RPM_SMD_LN_BB_CLK2] = &clk_smd_rpm_ln_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &clk_smd_rpm_ln_bb_clk2_a,
	[RPM_SMD_LN_BB_CLK3] = &clk_smd_rpm_ln_bb_clk3,
	[RPM_SMD_LN_BB_CLK3_A] = &clk_smd_rpm_ln_bb_clk3_a,
	[RPM_SMD_LN_BB_CLK1_PIN] = &clk_smd_rpm_ln_bb_clk1_pin,
	[RPM_SMD_LN_BB_CLK1_A_PIN] = &clk_smd_rpm_ln_bb_clk1_a_pin,
	[RPM_SMD_LN_BB_CLK2_PIN] = &clk_smd_rpm_ln_bb_clk2_pin,
	[RPM_SMD_LN_BB_CLK2_A_PIN] = &clk_smd_rpm_ln_bb_clk2_a_pin,
	[RPM_SMD_LN_BB_CLK3_PIN] = &clk_smd_rpm_ln_bb_clk3_pin,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &clk_smd_rpm_ln_bb_clk3_a_pin,
	[RPM_SMD_MMAXI_CLK] = &clk_smd_rpm_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMAXI_A_CLK] = &clk_smd_rpm_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_AGGR1_NOC_CLK] = &clk_smd_rpm_aggre1_noc_clk,
	[RPM_SMD_AGGR1_NOC_A_CLK] = &clk_smd_rpm_aggre1_noc_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &clk_smd_rpm_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &clk_smd_rpm_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_RF_CLK3] = &clk_smd_rpm_rf_clk3,
	[RPM_SMD_RF_CLK3_A] = &clk_smd_rpm_rf_clk3_a,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &clk_smd_rpm_rf_clk2_a_pin,
	[RPM_SMD_RF_CLK3_PIN] = &clk_smd_rpm_rf_clk3_pin,
	[RPM_SMD_RF_CLK3_A_PIN] = &clk_smd_rpm_rf_clk3_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8998 = {
	.clks = msm8998_clks,
	.num_clks = ARRAY_SIZE(msm8998_clks),
};

static struct clk_smd_rpm *sdm660_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_2_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_2_cnoc_a_clk,
	[RPM_SMD_CNOC_PERIPH_CLK] = &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_CNOC_PERIPH_A_CLK] = &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_MMSSNOC_AXI_CLK] = &clk_smd_rpm_mmssnoc_axi_rpm_clk,
	[RPM_SMD_MMSSNOC_AXI_CLK_A] = &clk_smd_rpm_mmssnoc_axi_rpm_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_AGGR2_NOC_CLK] = &clk_smd_rpm_aggre2_noc_clk,
	[RPM_SMD_AGGR2_NOC_A_CLK] = &clk_smd_rpm_aggre2_noc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_DIV_CLK1] = &clk_smd_rpm_div_clk1,
	[RPM_SMD_DIV_A_CLK1] = &clk_smd_rpm_div_clk1_a,
	[RPM_SMD_LN_BB_CLK] = &clk_smd_rpm_ln_bb_clk1,
	[RPM_SMD_LN_BB_A_CLK] = &clk_smd_rpm_ln_bb_clk1_a,
	[RPM_SMD_LN_BB_CLK2] = &clk_smd_rpm_ln_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &clk_smd_rpm_ln_bb_clk2_a,
	[RPM_SMD_LN_BB_CLK3] = &clk_smd_rpm_ln_bb_clk3,
	[RPM_SMD_LN_BB_CLK3_A] = &clk_smd_rpm_ln_bb_clk3_a,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_LN_BB_CLK1_PIN] = &clk_smd_rpm_ln_bb_clk1_pin,
	[RPM_SMD_LN_BB_CLK1_A_PIN] = &clk_smd_rpm_ln_bb_clk1_a_pin,
	[RPM_SMD_LN_BB_CLK2_PIN] = &clk_smd_rpm_ln_bb_clk2_pin,
	[RPM_SMD_LN_BB_CLK2_A_PIN] = &clk_smd_rpm_ln_bb_clk2_a_pin,
	[RPM_SMD_LN_BB_CLK3_PIN] = &clk_smd_rpm_ln_bb_clk3_pin,
	[RPM_SMD_LN_BB_CLK3_A_PIN] = &clk_smd_rpm_ln_bb_clk3_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_sdm660 = {
	.clks = sdm660_clks,
	.num_clks = ARRAY_SIZE(sdm660_clks),
};

static struct clk_smd_rpm *mdm9607_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PCNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QPIC_CLK]		= &clk_smd_rpm_qpic_clk,
	[RPM_SMD_QPIC_CLK_A]		= &clk_smd_rpm_qpic_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_mdm9607 = {
	.clks = mdm9607_clks,
	.num_clks = ARRAY_SIZE(mdm9607_clks),
};

static struct clk_smd_rpm *msm8953_clks[] = {
	[RPM_SMD_XO_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC]		= &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_PCNOC_CLK]		= &clk_smd_rpm_bus_0_pcnoc_clk,
	[RPM_SMD_PCNOC_A_CLK]		= &clk_smd_rpm_bus_0_pcnoc_a_clk,
	[RPM_SMD_SNOC_CLK]		= &clk_smd_rpm_bus_1_snoc_clk,
	[RPM_SMD_SNOC_A_CLK]		= &clk_smd_rpm_bus_1_snoc_a_clk,
	[RPM_SMD_BIMC_CLK]		= &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK]		= &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_IPA_CLK]		= &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK]		= &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_SYSMMNOC_CLK]		= &clk_smd_rpm_bus_2_sysmmnoc_clk,
	[RPM_SMD_SYSMMNOC_A_CLK]	= &clk_smd_rpm_bus_2_sysmmnoc_a_clk,
	[RPM_SMD_QDSS_CLK]		= &clk_smd_rpm_qdss_clk,
	[RPM_SMD_QDSS_A_CLK]		= &clk_smd_rpm_qdss_a_clk,
	[RPM_SMD_BB_CLK1]		= &clk_smd_rpm_bb_clk1,
	[RPM_SMD_BB_CLK1_A]		= &clk_smd_rpm_bb_clk1_a,
	[RPM_SMD_BB_CLK2]		= &clk_smd_rpm_bb_clk2,
	[RPM_SMD_BB_CLK2_A]		= &clk_smd_rpm_bb_clk2_a,
	[RPM_SMD_RF_CLK2]		= &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A]		= &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_RF_CLK3]		= &clk_smd_rpm_ln_bb_clk,
	[RPM_SMD_RF_CLK3_A]		= &clk_smd_rpm_ln_bb_clk_a,
	[RPM_SMD_DIV_CLK2]		= &clk_smd_rpm_div_clk2,
	[RPM_SMD_DIV_A_CLK2]		= &clk_smd_rpm_div_clk2_a,
	[RPM_SMD_BB_CLK1_PIN]		= &clk_smd_rpm_bb_clk1_pin,
	[RPM_SMD_BB_CLK1_A_PIN]		= &clk_smd_rpm_bb_clk1_a_pin,
	[RPM_SMD_BB_CLK2_PIN]		= &clk_smd_rpm_bb_clk2_pin,
	[RPM_SMD_BB_CLK2_A_PIN]		= &clk_smd_rpm_bb_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_msm8953 = {
	.clks = msm8953_clks,
	.num_clks = ARRAY_SIZE(msm8953_clks),
};

static struct clk_smd_rpm *sm6125_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_2_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_2_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_branch_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_branch_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_1_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_1_cnoc_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_LN_BB_CLK1] = &clk_smd_rpm_ln_bb_clk1,
	[RPM_SMD_LN_BB_CLK1_A] = &clk_smd_rpm_ln_bb_clk1_a,
	[RPM_SMD_LN_BB_CLK2] = &clk_smd_rpm_ln_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &clk_smd_rpm_ln_bb_clk2_a,
	[RPM_SMD_LN_BB_CLK3] = &clk_smd_rpm_ln_bb_clk3,
	[RPM_SMD_LN_BB_CLK3_A] = &clk_smd_rpm_ln_bb_clk3_a,
	[RPM_SMD_QUP_CLK] = &clk_smd_rpm_qup_clk,
	[RPM_SMD_QUP_A_CLK] = &clk_smd_rpm_qup_a_clk,
	[RPM_SMD_MMRT_CLK] = &clk_smd_rpm_mmrt_clk,
	[RPM_SMD_MMRT_A_CLK] = &clk_smd_rpm_mmrt_a_clk,
	[RPM_SMD_MMNRT_CLK] = &clk_smd_rpm_mmnrt_clk,
	[RPM_SMD_MMNRT_A_CLK] = &clk_smd_rpm_mmnrt_a_clk,
	[RPM_SMD_SNOC_PERIPH_CLK] = &clk_smd_rpm_bus_0_snoc_periph_clk,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &clk_smd_rpm_bus_0_snoc_periph_a_clk,
	[RPM_SMD_SNOC_LPASS_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_clk,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6125 = {
	.clks = sm6125_clks,
	.num_clks = ARRAY_SIZE(sm6125_clks),
};

/* SM6115 */
static struct clk_smd_rpm *sm6115_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_2_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_2_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_branch_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_branch_qdss_a_clk,
	[RPM_SMD_RF_CLK1] = &clk_smd_rpm_rf_clk1,
	[RPM_SMD_RF_CLK1_A] = &clk_smd_rpm_rf_clk1_a,
	[RPM_SMD_RF_CLK2] = &clk_smd_rpm_rf_clk2,
	[RPM_SMD_RF_CLK2_A] = &clk_smd_rpm_rf_clk2_a,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_1_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_1_cnoc_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_QUP_CLK] = &clk_smd_rpm_qup_clk,
	[RPM_SMD_QUP_A_CLK] = &clk_smd_rpm_qup_a_clk,
	[RPM_SMD_MMRT_CLK] = &clk_smd_rpm_mmrt_clk,
	[RPM_SMD_MMRT_A_CLK] = &clk_smd_rpm_mmrt_a_clk,
	[RPM_SMD_MMNRT_CLK] = &clk_smd_rpm_mmnrt_clk,
	[RPM_SMD_MMNRT_A_CLK] = &clk_smd_rpm_mmnrt_a_clk,
	[RPM_SMD_SNOC_PERIPH_CLK] = &clk_smd_rpm_bus_0_snoc_periph_clk,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &clk_smd_rpm_bus_0_snoc_periph_a_clk,
	[RPM_SMD_SNOC_LPASS_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_clk,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_a_clk,
	[RPM_SMD_RF_CLK1_PIN] = &clk_smd_rpm_rf_clk1_pin,
	[RPM_SMD_RF_CLK1_A_PIN] = &clk_smd_rpm_rf_clk1_a_pin,
	[RPM_SMD_RF_CLK2_PIN] = &clk_smd_rpm_rf_clk2_pin,
	[RPM_SMD_RF_CLK2_A_PIN] = &clk_smd_rpm_rf_clk2_a_pin,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6115 = {
	.clks = sm6115_clks,
	.num_clks = ARRAY_SIZE(sm6115_clks),
};

static struct clk_smd_rpm *sm6375_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_2_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_2_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_branch_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_branch_qdss_a_clk,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_1_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_1_cnoc_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_QUP_CLK] = &clk_smd_rpm_qup_clk,
	[RPM_SMD_QUP_A_CLK] = &clk_smd_rpm_qup_a_clk,
	[RPM_SMD_MMRT_CLK] = &clk_smd_rpm_mmrt_clk,
	[RPM_SMD_MMRT_A_CLK] = &clk_smd_rpm_mmrt_a_clk,
	[RPM_SMD_MMNRT_CLK] = &clk_smd_rpm_mmnrt_clk,
	[RPM_SMD_MMNRT_A_CLK] = &clk_smd_rpm_mmnrt_a_clk,
	[RPM_SMD_SNOC_PERIPH_CLK] = &clk_smd_rpm_bus_0_snoc_periph_clk,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &clk_smd_rpm_bus_0_snoc_periph_a_clk,
	[RPM_SMD_SNOC_LPASS_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_clk,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_HWKM_CLK] = &clk_smd_rpm_hwkm_clk,
	[RPM_SMD_HWKM_A_CLK] = &clk_smd_rpm_hwkm_a_clk,
	[RPM_SMD_PKA_CLK] = &clk_smd_rpm_pka_clk,
	[RPM_SMD_PKA_A_CLK] = &clk_smd_rpm_pka_a_clk,
	[RPM_SMD_BIMC_FREQ_LOG] = &clk_smd_rpm_branch_bimc_freq_log,
};

static const struct rpm_smd_clk_desc rpm_clk_sm6375 = {
	.clks = sm6375_clks,
	.num_clks = ARRAY_SIZE(sm6375_clks),
};

static struct clk_smd_rpm *qcm2290_clks[] = {
	[RPM_SMD_XO_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo,
	[RPM_SMD_XO_A_CLK_SRC] = &clk_smd_rpm_branch_bi_tcxo_a,
	[RPM_SMD_SNOC_CLK] = &clk_smd_rpm_bus_2_snoc_clk,
	[RPM_SMD_SNOC_A_CLK] = &clk_smd_rpm_bus_2_snoc_a_clk,
	[RPM_SMD_BIMC_CLK] = &clk_smd_rpm_bimc_clk,
	[RPM_SMD_BIMC_A_CLK] = &clk_smd_rpm_bimc_a_clk,
	[RPM_SMD_QDSS_CLK] = &clk_smd_rpm_branch_qdss_clk,
	[RPM_SMD_QDSS_A_CLK] = &clk_smd_rpm_branch_qdss_a_clk,
	[RPM_SMD_LN_BB_CLK2] = &clk_smd_rpm_ln_bb_clk2,
	[RPM_SMD_LN_BB_CLK2_A] = &clk_smd_rpm_ln_bb_clk2_a,
	[RPM_SMD_RF_CLK3] = &clk_smd_rpm_38m4_rf_clk3,
	[RPM_SMD_RF_CLK3_A] = &clk_smd_rpm_38m4_rf_clk3_a,
	[RPM_SMD_CNOC_CLK] = &clk_smd_rpm_bus_1_cnoc_clk,
	[RPM_SMD_CNOC_A_CLK] = &clk_smd_rpm_bus_1_cnoc_a_clk,
	[RPM_SMD_IPA_CLK] = &clk_smd_rpm_ipa_clk,
	[RPM_SMD_IPA_A_CLK] = &clk_smd_rpm_ipa_a_clk,
	[RPM_SMD_QUP_CLK] = &clk_smd_rpm_qup_clk,
	[RPM_SMD_QUP_A_CLK] = &clk_smd_rpm_qup_a_clk,
	[RPM_SMD_MMRT_CLK] = &clk_smd_rpm_mmrt_clk,
	[RPM_SMD_MMRT_A_CLK] = &clk_smd_rpm_mmrt_a_clk,
	[RPM_SMD_MMNRT_CLK] = &clk_smd_rpm_mmnrt_clk,
	[RPM_SMD_MMNRT_A_CLK] = &clk_smd_rpm_mmnrt_a_clk,
	[RPM_SMD_SNOC_PERIPH_CLK] = &clk_smd_rpm_bus_0_snoc_periph_clk,
	[RPM_SMD_SNOC_PERIPH_A_CLK] = &clk_smd_rpm_bus_0_snoc_periph_a_clk,
	[RPM_SMD_SNOC_LPASS_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_clk,
	[RPM_SMD_SNOC_LPASS_A_CLK] = &clk_smd_rpm_bus_5_snoc_lpass_a_clk,
	[RPM_SMD_CE1_CLK] = &clk_smd_rpm_ce1_clk,
	[RPM_SMD_CE1_A_CLK] = &clk_smd_rpm_ce1_a_clk,
	[RPM_SMD_QPIC_CLK] = &clk_smd_rpm_qpic_clk,
	[RPM_SMD_QPIC_CLK_A] = &clk_smd_rpm_qpic_a_clk,
	[RPM_SMD_HWKM_CLK] = &clk_smd_rpm_hwkm_clk,
	[RPM_SMD_HWKM_A_CLK] = &clk_smd_rpm_hwkm_a_clk,
	[RPM_SMD_PKA_CLK] = &clk_smd_rpm_pka_clk,
	[RPM_SMD_PKA_A_CLK] = &clk_smd_rpm_pka_a_clk,
	[RPM_SMD_BIMC_GPU_CLK] = &clk_smd_rpm_bimc_gpu_clk,
	[RPM_SMD_BIMC_GPU_A_CLK] = &clk_smd_rpm_bimc_gpu_a_clk,
	[RPM_SMD_CPUSS_GNOC_CLK] = &clk_smd_rpm_cpuss_gnoc_clk,
	[RPM_SMD_CPUSS_GNOC_A_CLK] = &clk_smd_rpm_cpuss_gnoc_a_clk,
};

static const struct rpm_smd_clk_desc rpm_clk_qcm2290 = {
	.clks = qcm2290_clks,
	.num_clks = ARRAY_SIZE(qcm2290_clks),
};

static const struct of_device_id rpm_smd_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-mdm9607", .data = &rpm_clk_mdm9607 },
	{ .compatible = "qcom,rpmcc-msm8226", .data = &rpm_clk_msm8974 },
	{ .compatible = "qcom,rpmcc-msm8909", .data = &rpm_clk_msm8909 },
	{ .compatible = "qcom,rpmcc-msm8916", .data = &rpm_clk_msm8916 },
	{ .compatible = "qcom,rpmcc-msm8917", .data = &rpm_clk_msm8917 },
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
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_smd_clk_match_table);

static struct clk_hw *qcom_smdrpm_clk_hw_get(struct of_phandle_args *clkspec,
					     void *data)
{
	const struct rpm_smd_clk_desc *desc = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= desc->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return desc->clks[idx] ? &desc->clks[idx]->hw : ERR_PTR(-ENOENT);
}

static void rpm_smd_unregister_icc(void *data)
{
	struct platform_device *icc_pdev = data;

	platform_device_unregister(icc_pdev);
}

static int rpm_smd_clk_probe(struct platform_device *pdev)
{
	int ret;
	size_t num_clks, i;
	struct clk_smd_rpm **rpm_smd_clks;
	const struct rpm_smd_clk_desc *desc;
	struct platform_device *icc_pdev;

	rpmcc_smd_rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpmcc_smd_rpm) {
		dev_err(&pdev->dev, "Unable to retrieve handle to RPM\n");
		return -ENODEV;
	}

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rpm_smd_clks = desc->clks;
	num_clks = desc->num_clks;

	if (desc->scaling_before_handover) {
		ret = clk_smd_rpm_enable_scaling();
		if (ret)
			goto err;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rpm_smd_clks[i])
			continue;

		ret = clk_smd_rpm_handoff(rpm_smd_clks[i]);
		if (ret)
			goto err;
	}

	if (!desc->scaling_before_handover) {
		ret = clk_smd_rpm_enable_scaling();
		if (ret)
			goto err;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rpm_smd_clks[i])
			continue;

		ret = devm_clk_hw_register(&pdev->dev, &rpm_smd_clks[i]->hw);
		if (ret)
			goto err;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, qcom_smdrpm_clk_hw_get,
					  (void *)desc);
	if (ret)
		goto err;

	icc_pdev = platform_device_register_data(pdev->dev.parent,
						 "icc_smd_rpm", -1, NULL, 0);
	if (IS_ERR(icc_pdev)) {
		dev_err(&pdev->dev, "Failed to register icc_smd_rpm device: %pE\n",
			icc_pdev);
		/* No need to unregister clocks because of this */
	} else {
		ret = devm_add_action_or_reset(&pdev->dev, rpm_smd_unregister_icc,
					       icc_pdev);
		if (ret)
			goto err;
	}

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
