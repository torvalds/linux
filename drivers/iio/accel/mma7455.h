/*
 * IIO accel driver for Freescale MMA7455L 3-axis 10-bit accelerometer
 * Copyright 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MMA7455_H
#define __MMA7455_H

extern const struct regmap_config mma7455_core_regmap;

int mma7455_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name);
int mma7455_core_remove(struct device *dev);

#endif
