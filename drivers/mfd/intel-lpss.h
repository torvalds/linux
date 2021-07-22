/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel LPSS core support.
 *
 * Copyright (C) 2015, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef __MFD_INTEL_LPSS_H
#define __MFD_INTEL_LPSS_H

#include <linux/pm.h>

struct device;
struct resource;
struct software_node;

struct intel_lpss_platform_info {
	struct resource *mem;
	int irq;
	unsigned long clk_rate;
	const char *clk_con_id;
	const struct software_node *swnode;
};

int intel_lpss_probe(struct device *dev,
		     const struct intel_lpss_platform_info *info);
void intel_lpss_remove(struct device *dev);

#ifdef CONFIG_PM
int intel_lpss_prepare(struct device *dev);
int intel_lpss_suspend(struct device *dev);
int intel_lpss_resume(struct device *dev);

#ifdef CONFIG_PM_SLEEP
#define INTEL_LPSS_SLEEP_PM_OPS			\
	.prepare = intel_lpss_prepare,		\
	SET_LATE_SYSTEM_SLEEP_PM_OPS(intel_lpss_suspend, intel_lpss_resume)
#else
#define INTEL_LPSS_SLEEP_PM_OPS
#endif

#define INTEL_LPSS_RUNTIME_PM_OPS		\
	.runtime_suspend = intel_lpss_suspend,	\
	.runtime_resume = intel_lpss_resume,

#else /* !CONFIG_PM */
#define INTEL_LPSS_SLEEP_PM_OPS
#define INTEL_LPSS_RUNTIME_PM_OPS
#endif /* CONFIG_PM */

#define INTEL_LPSS_PM_OPS(name)			\
const struct dev_pm_ops name = {		\
	INTEL_LPSS_SLEEP_PM_OPS			\
	INTEL_LPSS_RUNTIME_PM_OPS		\
}

#endif /* __MFD_INTEL_LPSS_H */
