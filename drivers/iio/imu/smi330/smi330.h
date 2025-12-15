/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Copyright (c) 2025 Robert Bosch GmbH.
 */
#ifndef _SMI330_H
#define _SMI330_H

#include <linux/iio/iio.h>

enum {
	SMI330_SCAN_ACCEL_X,
	SMI330_SCAN_ACCEL_Y,
	SMI330_SCAN_ACCEL_Z,
	SMI330_SCAN_GYRO_X,
	SMI330_SCAN_GYRO_Y,
	SMI330_SCAN_GYRO_Z,
	SMI330_SCAN_TIMESTAMP,
	SMI330_SCAN_LEN = SMI330_SCAN_TIMESTAMP,
};

extern const struct regmap_config smi330_regmap_config;

int smi330_core_probe(struct device *dev, struct regmap *regmap);

#endif /* _SMI330_H */
