/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal header for PM domain core
 *
 * Copyright (C) 2026 Kevin Hilman <khilman@baylibre.com>, Texas Instruments
 */

#ifndef __PM_DOMAIN_CORE_H__
#define __PM_DOMAIN_CORE_H__

struct device;
struct generic_pm_domain;

int genpd_for_each_child(struct generic_pm_domain *genpd,
			 int (*fn)(struct device *dev, void *data),
			 void *data);

#endif /* __PM_DOMAIN_CORE_H__ */
