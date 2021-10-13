/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BMI088_ACCEL_H
#define BMI088_ACCEL_H

#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct device;

extern const struct regmap_config bmi088_regmap_conf;
extern const struct dev_pm_ops bmi088_accel_pm_ops;

int bmi088_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    const char *name, bool block_supported);
void bmi088_accel_core_remove(struct device *dev);

#endif /* BMI088_ACCEL_H */
