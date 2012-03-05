/*
 * AD7879/AD7889 touchscreen (bus interfaces)
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _AD7879_H_
#define _AD7879_H_

#include <linux/types.h>

struct ad7879;
struct device;

struct ad7879_bus_ops {
	u16 bustype;
	int (*read)(struct device *dev, u8 reg);
	int (*multi_read)(struct device *dev, u8 first_reg, u8 count, u16 *buf);
	int (*write)(struct device *dev, u8 reg, u16 val);
};

extern const struct dev_pm_ops ad7879_pm_ops;

struct ad7879 *ad7879_probe(struct device *dev, u8 devid, unsigned irq,
			    const struct ad7879_bus_ops *bops);
void ad7879_remove(struct ad7879 *);

#endif
