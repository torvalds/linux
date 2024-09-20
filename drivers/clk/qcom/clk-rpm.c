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
#include <linux/mfd/qcom_rpm.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/mfd/qcom-rpm.h>
#include <dt-bindings/clock/qcom,rpmcc.h>

#define QCOM_RPM_MISC_CLK_TYPE				0x306b6c63
#define QCOM_RPM_SCALING_ENABLE_ID			0x2
#define QCOM_RPM_XO_MODE_ON				0x2

static const struct clk_parent_data gcc_pxo[] = {
	{ .fw_name = "pxo", .name = "pxo_board" },
};

static const struct clk_parent_data gcc_cxo[] = {
	{ .fw_name = "cxo", .name = "cxo_board" },
};

#define DEFINE_CLK_RPM(_name, r_id)					      \
	static struct clk_rpm clk_rpm_##_name##_a_clk;			      \
	static struct clk_rpm clk_rpm_##_name##_clk = {			      \
		.rpm_clk_id = (r_id),					      \
		.peer = &clk_rpm_##_name##_a_clk,			      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_ops,				      \
			.name = #_name "_clk",				      \
			.parent_data = gcc_pxo,				      \
			.num_parents = ARRAY_SIZE(gcc_pxo),		      \
		},							      \
	};								      \
	static struct clk_rpm clk_rpm_##_name##_a_clk = {		      \
		.rpm_clk_id = (r_id),					      \
		.peer = &clk_rpm_##_name##_clk,				      \
		.active_only = true,					      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_ops,				      \
			.name = #_name "_a_clk",			      \
			.parent_data = gcc_pxo,				      \
			.num_parents = ARRAY_SIZE(gcc_pxo),		      \
		},							      \
	}

#define DEFINE_CLK_RPM_XO_BUFFER(_name, offset)				      \
	static struct clk_rpm clk_rpm_##_name##_clk = {			      \
		.rpm_clk_id = QCOM_RPM_CXO_BUFFERS,			      \
		.xo_offset = (offset),					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_xo_ops,				      \
			.name = #_name "_clk",				      \
			.parent_data = gcc_cxo,				      \
			.num_parents = ARRAY_SIZE(gcc_cxo),		      \
		},							      \
	}

#define DEFINE_CLK_RPM_FIXED(_name, r_id, r)				      \
	static struct clk_rpm clk_rpm_##_name##_clk = {			      \
		.rpm_clk_id = (r_id),					      \
		.rate = (r),						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_fixed_ops,			      \
			.name = #_name "_clk",				      \
			.parent_data = gcc_pxo,				      \
			.num_parents = ARRAY_SIZE(gcc_pxo),		      \
		},							      \
	}

#define to_clk_rpm(_hw) container_of(_hw, struct clk_rpm, hw)

struct rpm_cc;

struct clk_rpm {
	const int rpm_clk_id;
	const int xo_offset;
	const bool active_only;
	unsigned long rate;
	bool enabled;
	bool branch;
	struct clk_rpm *peer;
	struct clk_hw hw;
	struct qcom_rpm *rpm;
	struct rpm_cc *rpm_cc;
};

struct rpm_cc {
	struct clk_rpm **clks;
	size_t num_clks;
	u32 xo_buffer_value;
	struct mutex xo_lock;
};

struct rpm_clk_desc {
	struct clk_rpm **clks;
	size_t num_clks;
};

static DEFINE_MUTEX(rpm_clk_lock);

static int clk_rpm_handoff(struct clk_rpm *r)
{
	int ret;
	u32 value = INT_MAX;

	/*
	 * The vendor tree simply reads the status for this
	 * RPM clock.
	 */
	if (r->rpm_clk_id == QCOM_RPM_PLL_4 ||
		r->rpm_clk_id == QCOM_RPM_CXO_BUFFERS)
		return 0;

	ret = qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE,
			     r->rpm_clk_id, &value, 1);
	if (ret)
		return ret;
	ret = qcom_rpm_write(r->rpm, QCOM_RPM_SLEEP_STATE,
			     r->rpm_clk_id, &value, 1);
	if (ret)
		return ret;

	return 0;
}

static int clk_rpm_set_rate_active(struct clk_rpm *r, unsigned long rate)
{
	u32 value = DIV_ROUND_UP(rate, 1000); /* to kHz */

	return qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE,
			      r->rpm_clk_id, &value, 1);
}

static int clk_rpm_set_rate_sleep(struct clk_rpm *r, unsigned long rate)
{
	u32 value = DIV_ROUND_UP(rate, 1000); /* to kHz */

	return qcom_rpm_write(r->rpm, QCOM_RPM_SLEEP_STATE,
			      r->rpm_clk_id, &value, 1);
}

static void to_active_sleep(struct clk_rpm *r, unsigned long rate,
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

static int clk_rpm_prepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	struct clk_rpm *peer = r->peer;
	unsigned long this_rate = 0, this_sleep_rate = 0;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	unsigned long active_rate, sleep_rate;
	int ret = 0;

	mutex_lock(&rpm_clk_lock);

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

	ret = clk_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = max(this_sleep_rate, peer_sleep_rate);
	if (r->branch)
		sleep_rate = !!sleep_rate;

	ret = clk_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		/* Undo the active set vote and restore it */
		ret = clk_rpm_set_rate_active(r, peer_rate);

out:
	if (!ret)
		r->enabled = true;

	mutex_unlock(&rpm_clk_lock);

	return ret;
}

static void clk_rpm_unprepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	struct clk_rpm *peer = r->peer;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	unsigned long active_rate, sleep_rate;
	int ret;

	mutex_lock(&rpm_clk_lock);

	if (!r->rate)
		goto out;

	/* Take peer clock's rate into account only if it's enabled. */
	if (peer->enabled)
		to_active_sleep(peer, peer->rate, &peer_rate,
				&peer_sleep_rate);

	active_rate = r->branch ? !!peer_rate : peer_rate;
	ret = clk_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = r->branch ? !!peer_sleep_rate : peer_sleep_rate;
	ret = clk_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		goto out;

	r->enabled = false;

out:
	mutex_unlock(&rpm_clk_lock);
}

static int clk_rpm_xo_prepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	struct rpm_cc *rcc = r->rpm_cc;
	int ret, clk_id = r->rpm_clk_id;
	u32 value;

	mutex_lock(&rcc->xo_lock);

	value = rcc->xo_buffer_value | (QCOM_RPM_XO_MODE_ON << r->xo_offset);
	ret = qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE, clk_id, &value, 1);
	if (!ret) {
		r->enabled = true;
		rcc->xo_buffer_value = value;
	}

	mutex_unlock(&rcc->xo_lock);

	return ret;
}

static void clk_rpm_xo_unprepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	struct rpm_cc *rcc = r->rpm_cc;
	int ret, clk_id = r->rpm_clk_id;
	u32 value;

	mutex_lock(&rcc->xo_lock);

	value = rcc->xo_buffer_value & ~(QCOM_RPM_XO_MODE_ON << r->xo_offset);
	ret = qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE, clk_id, &value, 1);
	if (!ret) {
		r->enabled = false;
		rcc->xo_buffer_value = value;
	}

	mutex_unlock(&rcc->xo_lock);
}

static int clk_rpm_fixed_prepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	u32 value = 1;
	int ret;

	ret = qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE,
			     r->rpm_clk_id, &value, 1);
	if (!ret)
		r->enabled = true;

	return ret;
}

static void clk_rpm_fixed_unprepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	u32 value = 0;
	int ret;

	ret = qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE,
			     r->rpm_clk_id, &value, 1);
	if (!ret)
		r->enabled = false;
}

static int clk_rpm_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long parent_rate)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	struct clk_rpm *peer = r->peer;
	unsigned long active_rate, sleep_rate;
	unsigned long this_rate = 0, this_sleep_rate = 0;
	unsigned long peer_rate = 0, peer_sleep_rate = 0;
	int ret = 0;

	mutex_lock(&rpm_clk_lock);

	if (!r->enabled)
		goto out;

	to_active_sleep(r, rate, &this_rate, &this_sleep_rate);

	/* Take peer clock's rate into account only if it's enabled. */
	if (peer->enabled)
		to_active_sleep(peer, peer->rate,
				&peer_rate, &peer_sleep_rate);

	active_rate = max(this_rate, peer_rate);
	ret = clk_rpm_set_rate_active(r, active_rate);
	if (ret)
		goto out;

	sleep_rate = max(this_sleep_rate, peer_sleep_rate);
	ret = clk_rpm_set_rate_sleep(r, sleep_rate);
	if (ret)
		goto out;

	r->rate = rate;

out:
	mutex_unlock(&rpm_clk_lock);

	return ret;
}

static long clk_rpm_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	/*
	 * RPM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate is requested.
	 */
	return rate;
}

static unsigned long clk_rpm_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_rpm *r = to_clk_rpm(hw);

	/*
	 * RPM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate was set.
	 */
	return r->rate;
}

static const struct clk_ops clk_rpm_xo_ops = {
	.prepare	= clk_rpm_xo_prepare,
	.unprepare	= clk_rpm_xo_unprepare,
};

static const struct clk_ops clk_rpm_fixed_ops = {
	.prepare	= clk_rpm_fixed_prepare,
	.unprepare	= clk_rpm_fixed_unprepare,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};

static const struct clk_ops clk_rpm_ops = {
	.prepare	= clk_rpm_prepare,
	.unprepare	= clk_rpm_unprepare,
	.set_rate	= clk_rpm_set_rate,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};

DEFINE_CLK_RPM(afab, QCOM_RPM_APPS_FABRIC_CLK);
DEFINE_CLK_RPM(sfab, QCOM_RPM_SYS_FABRIC_CLK);
DEFINE_CLK_RPM(mmfab, QCOM_RPM_MM_FABRIC_CLK);
DEFINE_CLK_RPM(daytona, QCOM_RPM_DAYTONA_FABRIC_CLK);
DEFINE_CLK_RPM(sfpb, QCOM_RPM_SFPB_CLK);
DEFINE_CLK_RPM(cfpb, QCOM_RPM_CFPB_CLK);
DEFINE_CLK_RPM(mmfpb, QCOM_RPM_MMFPB_CLK);
DEFINE_CLK_RPM(smi, QCOM_RPM_SMI_CLK);
DEFINE_CLK_RPM(ebi1, QCOM_RPM_EBI1_CLK);

DEFINE_CLK_RPM(qdss, QCOM_RPM_QDSS_CLK);
DEFINE_CLK_RPM(nss_fabric_0, QCOM_RPM_NSS_FABRIC_0_CLK);
DEFINE_CLK_RPM(nss_fabric_1, QCOM_RPM_NSS_FABRIC_1_CLK);

DEFINE_CLK_RPM_FIXED(pll4, QCOM_RPM_PLL_4, 540672000);

DEFINE_CLK_RPM_XO_BUFFER(xo_d0, 0);
DEFINE_CLK_RPM_XO_BUFFER(xo_d1, 8);
DEFINE_CLK_RPM_XO_BUFFER(xo_a0, 16);
DEFINE_CLK_RPM_XO_BUFFER(xo_a1, 24);
DEFINE_CLK_RPM_XO_BUFFER(xo_a2, 28);

static struct clk_rpm *msm8660_clks[] = {
	[RPM_APPS_FABRIC_CLK] = &clk_rpm_afab_clk,
	[RPM_APPS_FABRIC_A_CLK] = &clk_rpm_afab_a_clk,
	[RPM_SYS_FABRIC_CLK] = &clk_rpm_sfab_clk,
	[RPM_SYS_FABRIC_A_CLK] = &clk_rpm_sfab_a_clk,
	[RPM_MM_FABRIC_CLK] = &clk_rpm_mmfab_clk,
	[RPM_MM_FABRIC_A_CLK] = &clk_rpm_mmfab_a_clk,
	[RPM_DAYTONA_FABRIC_CLK] = &clk_rpm_daytona_clk,
	[RPM_DAYTONA_FABRIC_A_CLK] = &clk_rpm_daytona_a_clk,
	[RPM_SFPB_CLK] = &clk_rpm_sfpb_clk,
	[RPM_SFPB_A_CLK] = &clk_rpm_sfpb_a_clk,
	[RPM_CFPB_CLK] = &clk_rpm_cfpb_clk,
	[RPM_CFPB_A_CLK] = &clk_rpm_cfpb_a_clk,
	[RPM_MMFPB_CLK] = &clk_rpm_mmfpb_clk,
	[RPM_MMFPB_A_CLK] = &clk_rpm_mmfpb_a_clk,
	[RPM_SMI_CLK] = &clk_rpm_smi_clk,
	[RPM_SMI_A_CLK] = &clk_rpm_smi_a_clk,
	[RPM_EBI1_CLK] = &clk_rpm_ebi1_clk,
	[RPM_EBI1_A_CLK] = &clk_rpm_ebi1_a_clk,
	[RPM_PLL4_CLK] = &clk_rpm_pll4_clk,
};

static const struct rpm_clk_desc rpm_clk_msm8660 = {
	.clks = msm8660_clks,
	.num_clks = ARRAY_SIZE(msm8660_clks),
};

static struct clk_rpm *apq8064_clks[] = {
	[RPM_APPS_FABRIC_CLK] = &clk_rpm_afab_clk,
	[RPM_APPS_FABRIC_A_CLK] = &clk_rpm_afab_a_clk,
	[RPM_CFPB_CLK] = &clk_rpm_cfpb_clk,
	[RPM_CFPB_A_CLK] = &clk_rpm_cfpb_a_clk,
	[RPM_DAYTONA_FABRIC_CLK] = &clk_rpm_daytona_clk,
	[RPM_DAYTONA_FABRIC_A_CLK] = &clk_rpm_daytona_a_clk,
	[RPM_EBI1_CLK] = &clk_rpm_ebi1_clk,
	[RPM_EBI1_A_CLK] = &clk_rpm_ebi1_a_clk,
	[RPM_MM_FABRIC_CLK] = &clk_rpm_mmfab_clk,
	[RPM_MM_FABRIC_A_CLK] = &clk_rpm_mmfab_a_clk,
	[RPM_MMFPB_CLK] = &clk_rpm_mmfpb_clk,
	[RPM_MMFPB_A_CLK] = &clk_rpm_mmfpb_a_clk,
	[RPM_SYS_FABRIC_CLK] = &clk_rpm_sfab_clk,
	[RPM_SYS_FABRIC_A_CLK] = &clk_rpm_sfab_a_clk,
	[RPM_SFPB_CLK] = &clk_rpm_sfpb_clk,
	[RPM_SFPB_A_CLK] = &clk_rpm_sfpb_a_clk,
	[RPM_QDSS_CLK] = &clk_rpm_qdss_clk,
	[RPM_QDSS_A_CLK] = &clk_rpm_qdss_a_clk,
	[RPM_XO_D0] = &clk_rpm_xo_d0_clk,
	[RPM_XO_D1] = &clk_rpm_xo_d1_clk,
	[RPM_XO_A0] = &clk_rpm_xo_a0_clk,
	[RPM_XO_A1] = &clk_rpm_xo_a1_clk,
	[RPM_XO_A2] = &clk_rpm_xo_a2_clk,
};

static const struct rpm_clk_desc rpm_clk_apq8064 = {
	.clks = apq8064_clks,
	.num_clks = ARRAY_SIZE(apq8064_clks),
};

static struct clk_rpm *ipq806x_clks[] = {
	[RPM_APPS_FABRIC_CLK] = &clk_rpm_afab_clk,
	[RPM_APPS_FABRIC_A_CLK] = &clk_rpm_afab_a_clk,
	[RPM_CFPB_CLK] = &clk_rpm_cfpb_clk,
	[RPM_CFPB_A_CLK] = &clk_rpm_cfpb_a_clk,
	[RPM_DAYTONA_FABRIC_CLK] = &clk_rpm_daytona_clk,
	[RPM_DAYTONA_FABRIC_A_CLK] = &clk_rpm_daytona_a_clk,
	[RPM_EBI1_CLK] = &clk_rpm_ebi1_clk,
	[RPM_EBI1_A_CLK] = &clk_rpm_ebi1_a_clk,
	[RPM_SYS_FABRIC_CLK] = &clk_rpm_sfab_clk,
	[RPM_SYS_FABRIC_A_CLK] = &clk_rpm_sfab_a_clk,
	[RPM_SFPB_CLK] = &clk_rpm_sfpb_clk,
	[RPM_SFPB_A_CLK] = &clk_rpm_sfpb_a_clk,
	[RPM_NSS_FABRIC_0_CLK] = &clk_rpm_nss_fabric_0_clk,
	[RPM_NSS_FABRIC_0_A_CLK] = &clk_rpm_nss_fabric_0_a_clk,
	[RPM_NSS_FABRIC_1_CLK] = &clk_rpm_nss_fabric_1_clk,
	[RPM_NSS_FABRIC_1_A_CLK] = &clk_rpm_nss_fabric_1_a_clk,
};

static const struct rpm_clk_desc rpm_clk_ipq806x = {
	.clks = ipq806x_clks,
	.num_clks = ARRAY_SIZE(ipq806x_clks),
};

static const struct of_device_id rpm_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-msm8660", .data = &rpm_clk_msm8660 },
	{ .compatible = "qcom,rpmcc-apq8060", .data = &rpm_clk_msm8660 },
	{ .compatible = "qcom,rpmcc-apq8064", .data = &rpm_clk_apq8064 },
	{ .compatible = "qcom,rpmcc-ipq806x", .data = &rpm_clk_ipq806x },
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_clk_match_table);

static struct clk_hw *qcom_rpm_clk_hw_get(struct of_phandle_args *clkspec,
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

static int rpm_clk_probe(struct platform_device *pdev)
{
	struct rpm_cc *rcc;
	int ret;
	size_t num_clks, i;
	struct qcom_rpm *rpm;
	struct clk_rpm **rpm_clks;
	const struct rpm_clk_desc *desc;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "Unable to retrieve handle to RPM\n");
		return -ENODEV;
	}

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rpm_clks = desc->clks;
	num_clks = desc->num_clks;

	rcc = devm_kzalloc(&pdev->dev, sizeof(*rcc), GFP_KERNEL);
	if (!rcc)
		return -ENOMEM;

	rcc->clks = rpm_clks;
	rcc->num_clks = num_clks;
	mutex_init(&rcc->xo_lock);

	for (i = 0; i < num_clks; i++) {
		if (!rpm_clks[i])
			continue;

		rpm_clks[i]->rpm = rpm;
		rpm_clks[i]->rpm_cc = rcc;

		ret = clk_rpm_handoff(rpm_clks[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rpm_clks[i])
			continue;

		ret = devm_clk_hw_register(&pdev->dev, &rpm_clks[i]->hw);
		if (ret)
			goto err;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, qcom_rpm_clk_hw_get,
					  rcc);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&pdev->dev, "Error registering RPM Clock driver (%d)\n", ret);
	return ret;
}

static struct platform_driver rpm_clk_driver = {
	.driver = {
		.name = "qcom-clk-rpm",
		.of_match_table = rpm_clk_match_table,
	},
	.probe = rpm_clk_probe,
};

static int __init rpm_clk_init(void)
{
	return platform_driver_register(&rpm_clk_driver);
}
core_initcall(rpm_clk_init);

static void __exit rpm_clk_exit(void)
{
	platform_driver_unregister(&rpm_clk_driver);
}
module_exit(rpm_clk_exit);

MODULE_DESCRIPTION("Qualcomm RPM Clock Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-clk-rpm");
