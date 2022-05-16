/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BMI160_H_
#define BMI160_H_

#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

struct bmi160_data {
	struct regmap *regmap;
	struct iio_trigger *trig;
	struct regulator_bulk_data supplies[2];
	struct iio_mount_matrix orientation;
};

extern const struct regmap_config bmi160_regmap_config;

int bmi160_core_probe(struct device *dev, struct regmap *regmap,
		      const char *name, bool use_spi);

int bmi160_enable_irq(struct regmap *regmap, bool enable);

int bmi160_probe_trigger(struct iio_dev *indio_dev, int irq, u32 irq_type);

#endif  /* BMI160_H_ */
