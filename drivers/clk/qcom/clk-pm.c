// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>

#include "clk-regmap.h"
#include "clk-pm.h"
#include "common.h"

/* Restores the clocks configuration while coming out of Hibernation */
static int clock_pm_restore_early(struct device *dev)
{
	clk_restore_context();
	clk_restore_critical_clocks(dev);
	return 0;
}

/* Restores the clocks configuration while coming out of DeepSleep */
static int clock_pm_resume_early(struct device *dev)
{
#ifdef CONFIG_DEEPSLEEP
	if (pm_suspend_via_firmware()) {
		clk_restore_context();
		clk_restore_critical_clocks(dev);
	}
#endif
	return 0;
}

static const struct dev_pm_ops clock_pm_ops = {
	.restore_early = clock_pm_restore_early,
	.resume_early = clock_pm_resume_early,
};

static const struct dev_pm_ops clock_pm_rt_ops = {
	.restore = clock_pm_restore_early,
	.resume = clock_pm_resume_early,
	SET_RUNTIME_PM_OPS(qcom_cc_runtime_suspend, qcom_cc_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

int register_qcom_clks_pm(struct platform_device *pdev, bool runtime,
						struct qcom_cc_desc *desc)
{
	int ret;

	if (IS_ERR_OR_NULL(pdev))
		return PTR_ERR(pdev);

	if (runtime) {

		pdev->dev.driver->pm = &clock_pm_rt_ops;

		ret = qcom_cc_runtime_init(pdev, desc);
		if (ret)
			return ret;

		ret = pm_runtime_get_sync(&pdev->dev);
		if (ret)
			return ret;
	} else {
		pdev->dev.driver->pm = &clock_pm_ops;
		platform_set_drvdata(pdev, desc);
	}

	return 0;
}
EXPORT_SYMBOL(register_qcom_clks_pm);
