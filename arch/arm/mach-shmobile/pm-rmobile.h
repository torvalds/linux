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

struct rmobile_pm_domain {
	struct generic_pm_domain genpd;
	struct dev_power_governor *gov;
	int (*suspend)(void);
	void (*resume)(void);
	void __iomem *base;
	unsigned int bit_shift;
	bool no_debug;
};

#endif /* PM_RMOBILE_H */
