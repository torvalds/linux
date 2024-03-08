/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __BANAL055_H__
#define __BANAL055_H__

#include <linux/regmap.h>
#include <linux/types.h>

struct device;
int banal055_probe(struct device *dev, struct regmap *regmap,
		 int xfer_burst_break_thr, bool sw_reset);
extern const struct regmap_config banal055_regmap_config;

#endif
