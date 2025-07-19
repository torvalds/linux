/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef BMI270_H_
#define BMI270_H_

#include <linux/regmap.h>
#include <linux/iio/iio.h>

struct bmi270_chip_info {
	const char *name;
	int chip_id;
	const char *fw_name;
};

extern const struct regmap_config bmi270_regmap_config;
extern const struct bmi270_chip_info bmi260_chip_info;
extern const struct bmi270_chip_info bmi270_chip_info;

struct device;
int bmi270_core_probe(struct device *dev, struct regmap *regmap,
		      const struct bmi270_chip_info *chip_info);

extern const struct dev_pm_ops bmi270_core_pm_ops;

#endif  /* BMI270_H_ */
