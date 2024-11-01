/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics hts221 sensor driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 */

#ifndef HTS221_H
#define HTS221_H

#define HTS221_DEV_NAME		"hts221"

#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

enum hts221_sensor_type {
	HTS221_SENSOR_H,
	HTS221_SENSOR_T,
	HTS221_SENSOR_MAX,
};

struct hts221_sensor {
	u8 cur_avg_idx;
	int slope, b_gen;
};

struct hts221_hw {
	const char *name;
	struct device *dev;
	struct regmap *regmap;
	struct regulator *vdd;

	struct iio_trigger *trig;
	int irq;

	struct hts221_sensor sensors[HTS221_SENSOR_MAX];

	bool enabled;
	u8 odr;
	/* Ensure natural alignment of timestamp */
	struct {
		__le16 channels[2];
		s64 ts __aligned(8);
	} scan;
};

extern const struct dev_pm_ops hts221_pm_ops;

int hts221_probe(struct device *dev, int irq, const char *name,
		 struct regmap *regmap);
int hts221_set_enable(struct hts221_hw *hw, bool enable);
int hts221_allocate_buffers(struct iio_dev *iio_dev);
int hts221_allocate_trigger(struct iio_dev *iio_dev);

#endif /* HTS221_H */
