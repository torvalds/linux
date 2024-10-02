/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef BMI270_H_
#define BMI270_H_

#include <linux/regmap.h>
#include <linux/iio/iio.h>

struct device;
struct bmi270_data {
	struct device *dev;
	struct regmap *regmap;
};

extern const struct regmap_config bmi270_regmap_config;

int bmi270_core_probe(struct device *dev, struct regmap *regmap);

#endif  /* BMI270_H_ */
