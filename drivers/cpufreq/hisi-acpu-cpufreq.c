/*
 * Hisilicon Platforms Using ACPU CPUFreq Support
 *
 * Copyright (c) 2015 Hisilicon Limited.
 * Copyright (c) 2015 Linaro Limited.
 *
 * Leo Yan <leo.yan@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static int __init hisi_acpu_cpufreq_driver_init(void)
{
	struct platform_device *pdev;

	if (!of_machine_is_compatible("hisilicon,hi6220"))
		return -ENODEV;

	pdev = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	return PTR_ERR_OR_ZERO(pdev);
}
module_init(hisi_acpu_cpufreq_driver_init);

MODULE_AUTHOR("Leo Yan <leo.yan@linaro.org>");
MODULE_DESCRIPTION("Hisilicon acpu cpufreq driver");
MODULE_LICENSE("GPL v2");
