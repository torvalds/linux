/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BMI160_H_
#define BMI160_H_

extern const struct regmap_config bmi160_regmap_config;

int bmi160_core_probe(struct device *dev, struct regmap *regmap,
		      const char *name, bool use_spi);

#endif  /* BMI160_H_ */
