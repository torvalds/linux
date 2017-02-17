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
struct regmap;

extern const struct dev_pm_ops ad7879_pm_ops;

struct ad7879 *ad7879_probe(struct device *dev, struct regmap *regmap,
			    int irq, u16 bustype, u8 devid);
void ad7879_remove(struct ad7879 *);

#endif
