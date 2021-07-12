/* SPDX-License-Identifier: GPL-2.0-only */
// STMicroelectronics LSM9DS0 IMU driver

#ifndef ST_LSM9DS0_H
#define ST_LSM9DS0_H

struct iio_dev;
struct regulator;

struct st_lsm9ds0 {
	struct device *dev;
	const char *name;
	int irq;
	struct iio_dev *accel;
	struct iio_dev *magn;
	struct regulator *vdd;
	struct regulator *vdd_io;
};

int st_lsm9ds0_probe(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap);
int st_lsm9ds0_remove(struct st_lsm9ds0 *lsm9ds0);

#endif /* ST_LSM9DS0_H */
