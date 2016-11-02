/*
 * Copyright (c) 2016, Linaro Limited
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

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mfd/qcom_rpm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/mfd/qcom-rpm.h>
#include <dt-bindings/clock/qcom,rpmcc.h>

#define QCOM_RPM_MISC_CLK_TYPE				0x306b6c63
#define QCOM_RPM_SCALING_ENABLE_ID			0x2

#define DEFINE_CLK_RPM(_platform, _name, _active, r_id)			      \
	static struct clk_rpm _platform##_##_active;			      \
	static struct clk_rpm _platform##_##_name = {			      \
		.rpm_clk_id = (r_id),					      \
		.peer = &_platform##_##_active,				      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_ops,				      \
			.name = #_name,					      \
			.parent_names = (const char *[]){ "pxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_rpm _platform##_##_active = {			      \
		.rpm_clk_id = (r_id),					      \
		.peer = &_platform##_##_name,				      \
		.active_only = true,					      \
		.rate = INT_MAX,					      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_ops,				      \
			.name = #_active,				      \
			.parent_names = (const char *[]){ "pxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	}

#define DEFINE_CLK_RPM_PXO_BRANCH(_platform, _name, _active, r_id, r)	      \
	static struct clk_rpm _platform##_##_active;			      \
	static struct clk_rpm _platform##_##_name = {			      \
		.rpm_clk_id = (r_id),					      \
		.active_only = true,					      \
		.peer = &_platform##_##_active,				      \
		.rate = (r),						      \
		.branch = true,						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_branch_ops,			      \
			.name = #_name,					      \
			.parent_names = (const char *[]){ "pxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_rpm _platform##_##_active = {			      \
		.rpm_clk_id = (r_id),					      \
		.peer = &_platform##_##_name,				      \
		.rate = (r),						      \
		.branch = true,						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_branch_ops,			      \
			.name = #_active,				      \
			.parent_names = (const char *[]){ "pxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	}

#define DEFINE_CLK_RPM_CXO_BRANCH(_platform, _name, _active, r_id, r)	      \
	static struct clk_rpm _platform##_##_active;			      \
	static struct clk_rpm _platform##_##_name = {			      \
		.rpm_clk_id = (r_id),					      \
		.peer = &_platform##_##_active,				      \
		.rate = (r),						      \
		.branch = true,						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_branch_ops,			      \
			.name = #_name,					      \
			.parent_names = (const char *[]){ "cxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	};								      \
	static struct clk_rpm _platform##_##_active = {			      \
		.rpm_clk_id = (r_id),					      \
		.active_only = true,					      \
		.peer = &_platform##_##_name,				      \
		.rate = (r),						      \
		.branch = true,						      \
		.hw.init = &(struct clk_init_data){			      \
			.ops = &clk_rpm_branch_ops,			      \
			.name = #_active,				      \
			.parent_names = (const char *[]){ "cxo_board" },      \
			.num_parents = 1,				      \
		},							      \
	}

#define to_clk_rpm(_hw) container_of(_hw, struct clk_rpm, hw)

struct clk_rpm {
	const int rpm_clk_id;
	const bool active_only;
	unsigned long rate;
	bool enabled;
	bool branch;
	struct clk_rpm *peer;
	struct clk_hw hw;
	struct qcom_rpm *rpm;
};

struct rpm_cc {
	struct qcom_rpm *rpm;
	struct clk_hw_onecell_data data;
	struct clk_hw *hws[];
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

static const struct clk_ops clk_rpm_ops = {
	.prepare	= clk_rpm_prepare,
	.unprepare	= clk_rpm_unprepare,
	.set_rate	= clk_rpm_set_rate,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};

static const struct clk_ops clk_rpm_branch_ops = {
	.prepare	= clk_rpm_prepare,
	.unprepare	= clk_rpm_unprepare,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};

/* apq8064 */
DEFINE_CLK_RPM(apq8064, afab_clk, afab_a_clk, QCOM_RPM_APPS_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, cfpb_clk, cfpb_a_clk, QCOM_RPM_CFPB_CLK);
DEFINE_CLK_RPM(apq8064, daytona_clk, daytona_a_clk, QCOM_RPM_DAYTONA_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, ebi1_clk, ebi1_a_clk, QCOM_RPM_EBI1_CLK);
DEFINE_CLK_RPM(apq8064, mmfab_clk, mmfab_a_clk, QCOM_RPM_MM_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, mmfpb_clk, mmfpb_a_clk, QCOM_RPM_MMFPB_CLK);
DEFINE_CLK_RPM(apq8064, sfab_clk, sfab_a_clk, QCOM_RPM_SYS_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, sfpb_clk, sfpb_a_clk, QCOM_RPM_SFPB_CLK);
DEFINE_CLK_RPM(apq8064, qdss_clk, qdss_a_clk, QCOM_RPM_QDSS_CLK);

static struct clk_rpm *apq8064_clks[] = {
	[RPM_APPS_FABRIC_CLK] = &apq8064_afab_clk,
	[RPM_APPS_FABRIC_A_CLK] = &apq8064_afab_a_clk,
	[RPM_CFPB_CLK] = &apq8064_cfpb_clk,
	[RPM_CFPB_A_CLK] = &apq8064_cfpb_a_clk,
	[RPM_DAYTONA_FABRIC_CLK] = &apq8064_daytona_clk,
	[RPM_DAYTONA_FABRIC_A_CLK] = &apq8064_daytona_a_clk,
	[RPM_EBI1_CLK] = &apq8064_ebi1_clk,
	[RPM_EBI1_A_CLK] = &apq8064_ebi1_a_clk,
	[RPM_MM_FABRIC_CLK] = &apq8064_mmfab_clk,
	[RPM_MM_FABRIC_A_CLK] = &apq8064_mmfab_a_clk,
	[RPM_MMFPB_CLK] = &apq8064_mmfpb_clk,
	[RPM_MMFPB_A_CLK] = &apq8064_mmfpb_a_clk,
	[RPM_SYS_FABRIC_CLK] = &apq8064_sfab_clk,
	[RPM_SYS_FABRIC_A_CLK] = &apq8064_sfab_a_clk,
	[RPM_SFPB_CLK] = &apq8064_sfpb_clk,
	[RPM_SFPB_A_CLK] = &apq8064_sfpb_a_clk,
	[RPM_QDSS_CLK] = &apq8064_qdss_clk,
	[RPM_QDSS_A_CLK] = &apq8064_qdss_a_clk,
};

static const struct rpm_clk_desc rpm_clk_apq8064 = {
	.clks = apq8064_clks,
	.num_clks = ARRAY_SIZE(apq8064_clks),
};

static const struct of_device_id rpm_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-apq8064", .data = &rpm_clk_apq8064 },
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_clk_match_table);

static int rpm_clk_probe(struct platform_device *pdev)
{
	struct clk_hw **hws;
	struct rpm_cc *rcc;
	struct clk_hw_onecell_data *data;
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

	rcc = devm_kzalloc(&pdev->dev, sizeof(*rcc) + sizeof(*hws) * num_clks,
			   GFP_KERNEL);
	if (!rcc)
		return -ENOMEM;

	hws = rcc->hws;
	data = &rcc->data;
	data->num = num_clks;

	for (i = 0; i < num_clks; i++) {
		if (!rpm_clks[i])
			continue;

		rpm_clks[i]->rpm = rpm;

		ret = clk_rpm_handoff(rpm_clks[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rpm_clks[i]) {
			data->hws[i] = ERR_PTR(-ENOENT);
			continue;
		}

		ret = devm_clk_hw_register(&pdev->dev, &rpm_clks[i]->hw);
		if (ret)
			goto err;
	}

	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get,
				     data);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&pdev->dev, "Error registering RPM Clock driver (%d)\n", ret);
	return ret;
}

static int rpm_clk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
	return 0;
}

static struct platform_driver rpm_clk_driver = {
	.driver = {
		.name = "qcom-clk-rpm",
		.of_match_table = rpm_clk_match_table,
	},
	.probe = rpm_clk_probe,
	.remove = rpm_clk_remove,
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
