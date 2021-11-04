/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADXL355 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Puranjay Mohan <puranjay12@gmail.com>
 */

#ifndef _ADXL355_H_
#define _ADXL355_H_

#include <linux/regmap.h>

struct device;

extern const struct regmap_access_table adxl355_readable_regs_tbl;
extern const struct regmap_access_table adxl355_writeable_regs_tbl;

int adxl355_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name);

#endif /* _ADXL355_H_ */
