/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BMI088_ACCEL_H
#define BMI088_ACCEL_H

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct device;

enum bmi_device_type {
	BOSCH_BMI085,
	BOSCH_BMI088,
	BOSCH_BMI090L,
	BOSCH_UNKNOWN,
};

extern const struct regmap_config bmi088_regmap_conf;
extern const struct dev_pm_ops bmi088_accel_pm_ops;

int bmi088_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    enum bmi_device_type type);
void bmi088_accel_core_remove(struct device *dev);

#endif /* BMI088_ACCEL_H */
