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

#include "clk-rpm.h"
#include <dt-bindings/mfd/qcom-rpm.h>

#define to_clk_rpm(_hw) container_of(_hw, struct clk_rpm, hw)

static DEFINE_MUTEX(rpm_clk_lock);

static int clk_rpm_set_rate_active(struct clk_rpm *r, unsigned long rate)
{
	u32 value = DIV_ROUND_UP(rate, 1000); /* to kHz */

	return qcom_rpm_write(r->rpm, QCOM_RPM_ACTIVE_STATE,
			      r->rpm_clk_id, &value, 1);
}

static int clk_rpm_prepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	unsigned long rate = r->rate;
	int ret = 0;

	mutex_lock(&rpm_clk_lock);

	if (!rate)
		goto out;

	if (r->branch)
		rate = !!rate;

	ret = clk_rpm_set_rate_active(r, rate);

	if (ret)
		goto out;

out:
	if (!ret)
		r->enabled = true;

	mutex_unlock(&rpm_clk_lock);

	return ret;
}

static void clk_rpm_unprepare(struct clk_hw *hw)
{
	struct clk_rpm *r = to_clk_rpm(hw);
	int ret;

	mutex_lock(&rpm_clk_lock);

	if (!r->rate)
		goto out;

	ret = clk_rpm_set_rate_active(r, r->rate);
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
	int ret = 0;

	mutex_lock(&rpm_clk_lock);

	if (r->enabled)
		ret = clk_rpm_set_rate_active(r, rate);

	if (!ret)
		r->rate = rate;

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

const struct clk_ops clk_rpm_ops = {
	.prepare	= clk_rpm_prepare,
	.unprepare	= clk_rpm_unprepare,
	.set_rate	= clk_rpm_set_rate,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_rpm_ops);

const struct clk_ops clk_rpm_branch_ops = {
	.prepare	= clk_rpm_prepare,
	.unprepare	= clk_rpm_unprepare,
	.round_rate	= clk_rpm_round_rate,
	.recalc_rate	= clk_rpm_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_rpm_branch_ops);

struct rpm_cc {
	struct qcom_rpm *rpm;
	struct clk_onecell_data data;
	struct clk *clks[];
};

struct rpm_clk_desc {
	struct clk_rpm **clks;
	size_t num_clks;
};

/* apq8064 */
DEFINE_CLK_RPM_PXO_BRANCH(apq8064, pxo, QCOM_RPM_PXO_CLK, 27000000);
DEFINE_CLK_RPM_CXO_BRANCH(apq8064, cxo, QCOM_RPM_CXO_CLK, 19200000);
DEFINE_CLK_RPM(apq8064, afab_clk, QCOM_RPM_APPS_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, cfpb_clk, QCOM_RPM_CFPB_CLK);
DEFINE_CLK_RPM(apq8064, daytona_clk, QCOM_RPM_DAYTONA_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, ebi1_clk, QCOM_RPM_EBI1_CLK);
DEFINE_CLK_RPM(apq8064, mmfab_clk, QCOM_RPM_MM_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, mmfpb_clk, QCOM_RPM_MMFPB_CLK);
DEFINE_CLK_RPM(apq8064, sfab_clk, QCOM_RPM_SYS_FABRIC_CLK);
DEFINE_CLK_RPM(apq8064, sfpb_clk, QCOM_RPM_SFPB_CLK);
DEFINE_CLK_RPM(apq8064, qdss_clk, QCOM_RPM_QDSS_CLK);

static struct clk_rpm *apq8064_clks[] = {
	[QCOM_RPM_PXO_CLK] = &apq8064_pxo,
	[QCOM_RPM_CXO_CLK] = &apq8064_cxo,
	[QCOM_RPM_APPS_FABRIC_CLK] = &apq8064_afab_clk,
	[QCOM_RPM_CFPB_CLK] = &apq8064_cfpb_clk,
	[QCOM_RPM_DAYTONA_FABRIC_CLK] = &apq8064_daytona_clk,
	[QCOM_RPM_EBI1_CLK] = &apq8064_ebi1_clk,
	[QCOM_RPM_MM_FABRIC_CLK] = &apq8064_mmfab_clk,
	[QCOM_RPM_MMFPB_CLK] = &apq8064_mmfpb_clk,
	[QCOM_RPM_SYS_FABRIC_CLK] = &apq8064_sfab_clk,
	[QCOM_RPM_SFPB_CLK] = &apq8064_sfpb_clk,
	[QCOM_RPM_QDSS_CLK] = &apq8064_qdss_clk,
};

static const struct rpm_clk_desc rpm_clk_apq8064 = {
	.clks = apq8064_clks,
	.num_clks = ARRAY_SIZE(apq8064_clks),
};

static const struct of_device_id rpm_clk_match_table[] = {
	{ .compatible = "qcom,rpmcc-apq8064", .data = &rpm_clk_apq8064},
	{ }
};
MODULE_DEVICE_TABLE(of, rpm_clk_match_table);

static int rpm_clk_probe(struct platform_device *pdev)
{
	struct clk **clks;
	struct clk *clk;
	struct rpm_cc *rcc;
	struct clk_onecell_data *data;
	int ret, i;
	size_t num_clks;
	struct qcom_rpm *rpm;
	struct clk_rpm  **rpm_clks;
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

	rcc = devm_kzalloc(&pdev->dev, sizeof(*rcc) + sizeof(*clks) * num_clks,
			   GFP_KERNEL);
	if (!rcc)
		return -ENOMEM;

	clks = rcc->clks;
	data = &rcc->data;
	data->clks = clks;
	data->clk_num = num_clks;

	for (i = 0; i < num_clks; i++) {
		if (!rpm_clks[i]) {
			clks[i] = ERR_PTR(-ENOENT);
			continue;
		}

		rpm_clks[i]->rpm = rpm;
		clk = devm_clk_register(&pdev->dev, &rpm_clks[i]->hw);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto err;
		}

		clks[i] = clk;
	}

	ret = of_clk_add_provider(pdev->dev.of_node, of_clk_src_onecell_get,
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
