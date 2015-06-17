/*
 * Copyright (C) 2012 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef PM_RMOBILE_H
#define PM_RMOBILE_H

#include <linux/pm_domain.h>

#define DEFAULT_DEV_LATENCY_NS	250000

struct platform_device;

struct rmobile_pm_domain {
	struct generic_pm_domain genpd;
	struct dev_power_governor *gov;
	int (*suspend)(void);
	void (*resume)(void);
	void __iomem *base;
	unsigned int bit_shift;
	bool no_debug;
};

struct pm_domain_device {
	const char *domain_name;
	struct platform_device *pdev;
};

#endif /* PM_RMOBILE_H */
