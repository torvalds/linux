/*
 * ADXL345 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 */

#ifndef _ADXL345_H_
#define _ADXL345_H_

int adxl345_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name);
int adxl345_core_remove(struct device *dev);

#endif /* _ADXL345_H_ */
