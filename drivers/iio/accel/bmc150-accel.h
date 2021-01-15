/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BMC150_ACCEL_H_
#define _BMC150_ACCEL_H_

struct regmap;

enum {
	bmc150,
	bmi055,
	bma255,
	bma250e,
	bma222,
	bma222e,
	bma280,
};

int bmc150_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    const char *name, bool block_supported);
int bmc150_accel_core_remove(struct device *dev);
struct i2c_client *bmc150_get_second_device(struct i2c_client *second_device);
void bmc150_set_second_device(struct i2c_client *second_device);
extern const struct dev_pm_ops bmc150_accel_pm_ops;
extern const struct regmap_config bmc150_regmap_conf;

#endif  /* _BMC150_ACCEL_H_ */
