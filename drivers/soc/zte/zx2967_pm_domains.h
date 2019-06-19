/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for ZTE's Power Domain Driver support
 *
 * Copyright (C) 2017 ZTE Ltd.
 *
 * Author: Baoyou Xie <baoyou.xie@linaro.org>
 */

#ifndef __ZTE_ZX2967_PM_DOMAIN_H
#define __ZTE_ZX2967_PM_DOMAIN_H

#include <linux/platform_device.h>
#include <linux/pm_domain.h>

enum {
	REG_CLKEN,
	REG_ISOEN,
	REG_RSTEN,
	REG_PWREN,
	REG_PWRDN,
	REG_ACK_SYNC,

	/* The size of the array - must be last */
	REG_ARRAY_SIZE,
};

enum zx2967_power_polarity {
	PWREN,
	PWRDN,
};

struct zx2967_pm_domain {
	struct generic_pm_domain dm;
	const u16 bit;
	const enum zx2967_power_polarity polarity;
	const u16 *reg_offset;
};

int zx2967_pd_probe(struct platform_device *pdev,
		    struct generic_pm_domain **zx_pm_domains,
		    int domain_num);

#endif /* __ZTE_ZX2967_PM_DOMAIN_H */
