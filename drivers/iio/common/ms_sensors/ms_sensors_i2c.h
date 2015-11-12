/*
 * Measurements Specialties common sensor driver
 *
 * Copyright (c) 2015 Measurement-Specialties
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MS_SENSORS_I2C_H
#define _MS_SENSORS_I2C_H

#include <linux/i2c.h>
#include <linux/mutex.h>

#define MS_SENSORS_TP_PROM_WORDS_NB		7

/**
 * struct ms_ht_dev - Humidity/Temperature sensor device structure
 * @client:	i2c client
 * @lock:	lock protecting the i2c conversion
 * @res_index:	index to selected sensor resolution
 */
struct ms_ht_dev {
	struct i2c_client *client;
	struct mutex lock;
	u8 res_index;
};

/**
 * struct ms_tp_dev - Temperature/Pressure sensor device structure
 * @client:	i2c client
 * @lock:	lock protecting the i2c conversion
 * @prom:	array of PROM coefficients used for conversion. Added element
 *              for CRC computation
 * @res_index:	index to selected sensor resolution
 */
struct ms_tp_dev {
	struct i2c_client *client;
	struct mutex lock;
	u16 prom[MS_SENSORS_TP_PROM_WORDS_NB + 1];
	u8 res_index;
};

int ms_sensors_reset(void *cli, u8 cmd, unsigned int delay);
int ms_sensors_read_prom_word(void *cli, int cmd, u16 *word);
int ms_sensors_convert_and_read(void *cli, u8 conv, u8 rd,
				unsigned int delay, u32 *adc);
int ms_sensors_read_serial(struct i2c_client *client, u64 *sn);
ssize_t ms_sensors_show_serial(struct ms_ht_dev *dev_data, char *buf);
ssize_t ms_sensors_write_resolution(struct ms_ht_dev *dev_data, u8 i);
ssize_t ms_sensors_show_battery_low(struct ms_ht_dev *dev_data, char *buf);
ssize_t ms_sensors_show_heater(struct ms_ht_dev *dev_data, char *buf);
ssize_t ms_sensors_write_heater(struct ms_ht_dev *dev_data,
				const char *buf, size_t len);
int ms_sensors_ht_read_temperature(struct ms_ht_dev *dev_data,
				   s32 *temperature);
int ms_sensors_ht_read_humidity(struct ms_ht_dev *dev_data,
				u32 *humidity);
int ms_sensors_tp_read_prom(struct ms_tp_dev *dev_data);
int ms_sensors_read_temp_and_pressure(struct ms_tp_dev *dev_data,
				      int *temperature,
				      unsigned int *pressure);

#endif /* _MS_SENSORS_I2C_H */
