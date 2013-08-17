/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 * Author: Hyunki Koo <hyunki00.koo@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_SAMSUNG_WATCHDOG_H
#define __PLAT_SAMSUNG_WATCHDOG_H __FILE__

struct s3c_watchdog_platdata {
	void (*pmu_wdt_control)(bool on, unsigned int pmu_wdt_reset_type);
	unsigned int pmu_wdt_reset_type;
};

extern void s3c_watchdog_set_platdata(struct s3c_watchdog_platdata *pd);

#endif /* __PLAT_SAMSUNG_WATCHDOG_H */
