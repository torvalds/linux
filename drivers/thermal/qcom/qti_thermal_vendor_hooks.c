// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/thermal.h>
#include <trace/hooks/thermal.h>

static void qti_thermal_pm_notify(void *unused,
		struct thermal_zone_device *tz, int *irq_wakeable)
{
	*irq_wakeable = true;
}

static int __init qcom_thermal_vendor_hook_driver_init(void)
{
	int ret;

	ret = register_trace_android_vh_thermal_pm_notify_suspend(
			qti_thermal_pm_notify, NULL);
	if (ret)
		pr_err("Failed to register thermal_pm_notify hook, err:%d\n",
			ret);

	return 0;
}

static void __exit qcom_thermal_vendor_hook_driver_exit(void)
{
	unregister_trace_android_vh_thermal_pm_notify_suspend(
			qti_thermal_pm_notify, NULL);
}

#if IS_MODULE(CONFIG_QTI_THERMAL_VENDOR_HOOK)
module_init(qcom_thermal_vendor_hook_driver_init);
#else
pure_initcall(qcom_thermal_vendor_hook_driver_init);
#endif
module_exit(qcom_thermal_vendor_hook_driver_exit);

MODULE_DESCRIPTION("QCOM Thermal Vendor Hooks Driver");
MODULE_LICENSE("GPL");
