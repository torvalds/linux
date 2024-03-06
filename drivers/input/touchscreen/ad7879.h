/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AD7879/AD7889 touchscreen (bus interfaces)
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 */

#ifndef _AD7879_H_
#define _AD7879_H_

#include <linux/pm.h>
#include <linux/types.h>

struct attribute_group;
struct device;
struct regmap;

extern const struct attribute_group *ad7879_groups[];
extern const struct dev_pm_ops ad7879_pm_ops;

int ad7879_probe(struct device *dev, struct regmap *regmap,
		 int irq, u16 bustype, u8 devid);

#endif
