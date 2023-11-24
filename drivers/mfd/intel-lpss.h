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
	bool ignore_resource_conflicts;
	int irq;
	unsigned long clk_rate;
	const char *clk_con_id;
	const struct software_node *swnode;
};

int intel_lpss_probe(struct device *dev,
		     const struct intel_lpss_platform_info *info);
void intel_lpss_remove(struct device *dev);

extern const struct dev_pm_ops intel_lpss_pm_ops;

#endif /* __MFD_INTEL_LPSS_H */
