/*
 * MS5611 pressure and temperature sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _MS5611_H
#define _MS5611_H

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/mutex.h>

#define MS5611_RESET			0x1e
#define MS5611_READ_ADC			0x00
#define MS5611_READ_PROM_WORD		0xA0
#define MS5611_START_TEMP_CONV		0x58
#define MS5611_START_PRESSURE_CONV	0x48

#define MS5611_CONV_TIME_MIN		9040
#define MS5611_CONV_TIME_MAX		10000

#define MS5611_PROM_WORDS_NB		8

struct ms5611_state {
	void *client;
	struct mutex lock;

	int (*reset)(struct device *dev);
	int (*read_prom_word)(struct device *dev, int index, u16 *word);
	int (*read_adc_temp_and_pressure)(struct device *dev,
					  s32 *temp, s32 *pressure);

	u16 prom[MS5611_PROM_WORDS_NB];
};

int ms5611_probe(struct iio_dev *indio_dev, struct device *dev);

#endif /* _MS5611_H */
