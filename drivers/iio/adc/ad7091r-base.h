/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AD7091RX Analog to Digital converter driver
 *
 * Copyright 2014-2019 Analog Devices Inc.
 */

#ifndef __DRIVERS_IIO_ADC_AD7091R_BASE_H__
#define __DRIVERS_IIO_ADC_AD7091R_BASE_H__

#define AD7091R_REG_CONF_INT_VREF	BIT(0)

/* AD7091R_REG_CH_LIMIT */
#define AD7091R_HIGH_LIMIT		0xFFF
#define AD7091R_LOW_LIMIT		0x0

struct device;
struct ad7091r_state;

struct ad7091r_chip_info {
	unsigned int num_channels;
	const struct iio_chan_spec *channels;
	unsigned int vref_mV;
};

extern const struct iio_event_spec ad7091r_events[3];

extern const struct regmap_config ad7091r_regmap_config;

int ad7091r_probe(struct device *dev, const char *name,
		const struct ad7091r_chip_info *chip_info,
		struct regmap *map, int irq);

#endif /* __DRIVERS_IIO_ADC_AD7091R_BASE_H__ */
