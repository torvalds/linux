/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef BMI270_H_
#define BMI270_H_

#include <linux/regmap.h>
#include <linux/iio/iio.h>

struct device;
struct bmi270_data {
	struct device *dev;
	struct regmap *regmap;
	const struct bmi270_chip_info *chip_info;

	/*
	 * Where IIO_DMA_MINALIGN may be larger than 8 bytes, align to
	 * that to ensure a DMA safe buffer.
	 */
	struct {
		__le16 channels[6];
		aligned_s64 timestamp;
	} data __aligned(IIO_DMA_MINALIGN);
};

struct bmi270_chip_info {
	const char *name;
	int chip_id;
	const char *fw_name;
};

extern const struct regmap_config bmi270_regmap_config;
extern const struct bmi270_chip_info bmi260_chip_info;
extern const struct bmi270_chip_info bmi270_chip_info;

int bmi270_core_probe(struct device *dev, struct regmap *regmap,
		      const struct bmi270_chip_info *chip_info);

#endif  /* BMI270_H_ */
