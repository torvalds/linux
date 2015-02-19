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

#define MC13XXX_NUMREGS		0x3f
#define MC13XXX_IRQ_REG_CNT	2
#define MC13XXX_IRQ_PER_REG	24

struct mc13xxx;

struct mc13xxx_variant {
	const char *name;
	void (*print_revision)(struct mc13xxx *mc13xxx, u32 revision);
};

extern struct mc13xxx_variant
		mc13xxx_variant_mc13783,
		mc13xxx_variant_mc13892,
		mc13xxx_variant_mc34708;

struct mc13xxx {
	struct regmap *regmap;

	struct device *dev;
	const struct mc13xxx_variant *variant;

	struct regmap_irq irqs[MC13XXX_IRQ_PER_REG * MC13XXX_IRQ_REG_CNT];
	struct regmap_irq_chip irq_chip;
	struct regmap_irq_chip_data *irq_data;

	struct mutex lock;
	int irq;
	int flags;

	int adcflags;
};

int mc13xxx_common_init(struct device *dev);
int mc13xxx_common_exit(struct device *dev);

#endif /* __DRIVERS_MFD_MC13XXX_H */
