/*
 * Copyright 2012 Creative Product Design
 * Marc Reilly <marc@cpdesign.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#ifndef __DRIVERS_MFD_MC13XXX_H
#define __DRIVERS_MFD_MC13XXX_H

#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/mfd/mc13xxx.h>

enum mc13xxx_id {
	MC13XXX_ID_MC13783,
	MC13XXX_ID_MC13892,
	MC13XXX_ID_INVALID,
};

#define MC13XXX_NUMREGS 0x3f

struct mc13xxx {
	struct regmap *regmap;

	struct device *dev;
	enum mc13xxx_id ictype;

	struct mutex lock;
	int irq;
	int flags;

	irq_handler_t irqhandler[MC13XXX_NUM_IRQ];
	void *irqdata[MC13XXX_NUM_IRQ];

	int adcflags;
};

int mc13xxx_common_init(struct mc13xxx *mc13xxx,
		struct mc13xxx_platform_data *pdata, int irq);

void mc13xxx_common_cleanup(struct mc13xxx *mc13xxx);

#endif /* __DRIVERS_MFD_MC13XXX_H */
