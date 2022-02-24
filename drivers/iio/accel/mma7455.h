/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IIO accel driver for Freescale MMA7455L 3-axis 10-bit accelerometer
 * Copyright 2015 Joachim Eastwood <manabian@gmail.com>
 */

#ifndef __MMA7455_H
#define __MMA7455_H

extern const struct regmap_config mma7455_core_regmap;

int mma7455_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name);
void mma7455_core_remove(struct device *dev);

#endif
